"""Find symbols that look absent on a version but were only renamed by Mojang.

The spec keys symbols by their official Mojang name, which is what 26.x runs and
what both other namespaces are derived from. The cost of that choice is that
Mojang renames members: `Inventory.setSelectedHotbarSlot` became `setSelectedSlot`
in 1.21.5, so a spec written against 1.21.11 reports the symbol missing on 1.21.4
even though the method is right there.

Fabric's intermediary id does not move when Mojang renames, so it is the thread
that survives the rename. For every symbol that fails to resolve on a version,
this looks its 1.21.11 intermediary id up in that version's tables and, if the
member is present, prints the rule that would fix it.

A false "absent" is not cosmetic: it disables a working feature and tells the user
their version does not support it.

    python tools/find_renames.py                 # whole range
    python tools/find_renames.py 1.21.4 1.20.1
    python tools/find_renames.py --json          # rules ready for overrides.json
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import mc_artifacts as art
from gen_mappings import VersionTables, load_spec


def build_reverse(vt):
    """intermediary member name -> [(obf_owner, obf_name, obf_desc, kind)]"""
    out = {}
    for key, inter in vt.i_members.items():
        out.setdefault(inter, []).append(key)
    return out


def official_of(vt, obf_owner, obf_name, obf_desc, kind):
    for (owner, name, desc, k), obf in vt.pg_members.items():
        if k != kind or obf != obf_name:
            continue
        if vt.pg_classes.get(owner) != obf_owner:
            continue
        if art.remap_descriptor(desc, vt.pg_classes) != obf_desc:
            continue
        return owner, name, desc
    return None


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    as_json = "--json" in sys.argv
    versions = args or [v for v in art.VERSIONS if not art.is_deobfuscated(v)]

    symbols = load_spec()
    found = {}
    unexplained = {}

    for version in versions:
        if art.is_deobfuscated(version):
            continue                      # no intermediary thread to follow
        vt = VersionTables(version)
        reverse = build_reverse(vt)

        # Index official members by obfuscated identity once; the naive scan is
        # O(symbols x members) and this range has 135k members per version.
        by_obf = {}
        for (owner, name, desc, kind), obf in vt.pg_members.items():
            obf_owner = vt.pg_classes.get(owner)
            if obf_owner is None:
                continue
            by_obf[(obf_owner, obf, art.remap_descriptor(desc, vt.pg_classes), kind)] = \
                (owner, name, desc)

        # Classes move too: AbstractHorse changed package, and a moved class takes
        # every member that lives on it with it.
        inter_to_obf_class = {v: k for k, v in vt.i_classes.items()}
        obf_to_off_class = {v: k for k, v in vt.pg_classes.items()}

        for sym in symbols:
            if sym["kind"] == "class":
                res = vt.resolve(sym)
                if res["official"][0]:
                    continue
                seed = (sym.get("seed") or {}).get("intermediary")
                obf = inter_to_obf_class.get(seed) if seed else None
                off = obf_to_off_class.get(obf) if obf else None
                if off and off != sym["class"]:
                    found.setdefault(sym["id"], []).append((version, {"class": off}))
                elif not off:
                    unexplained.setdefault(version, []).append((sym["id"], "genuinely absent"))
                continue

            if sym["kind"] not in ("method", "field"):
                continue
            res = vt.resolve(sym)
            if res["official"][0] or res["official"][1]:
                continue                  # resolves fine

            seed = (sym.get("seed") or {}).get("intermediary")
            if not seed:
                unexplained.setdefault(version, []).append((sym["id"], "no intermediary seed"))
                continue

            kind = "f" if sym["kind"] == "field" else "m"
            hits = []
            for key in reverse.get(seed, []):
                if key[3] != kind:
                    continue
                off = by_obf.get(key)
                if off:
                    hits.append(off)

            if not hits:
                unexplained.setdefault(version, []).append((sym["id"], "genuinely absent"))
                continue

            # Prefer a hit on the owner the spec already names; otherwise the member
            # moved class as well as name and every candidate is worth printing.
            preferred = [h for h in hits if h[0] == sym.get("owner")] or hits
            owner, name, desc = preferred[0]
            rule = {}
            if owner != sym.get("owner"):
                rule["owner"] = owner
            if name != sym.get("name"):
                rule["name"] = name
            if desc != sym.get("desc"):
                rule["desc"] = desc
            if not rule:
                continue
            found.setdefault(sym["id"], []).append((version, rule))

    if as_json:
        out = {}
        for sym_id, entries in found.items():
            # Collapse a run of consecutive versions into one until/since window.
            ords = sorted(art.ordinal(v) for v, _r in entries)
            rule = entries[0][1]
            window = dict(rule)
            if ords and ords[-1] < art.ordinal("1.21.11"):
                window["until"] = art.VERSIONS[ords[-1]]
            if ords and ords[0] > 0:
                window["since"] = art.VERSIONS[ords[0]]
            out[sym_id] = {"versions": [window]}
        print(json.dumps(out, indent=2))
        return 0

    if found:
        print("Renamed, not missing -- %d symbol(s):\n" % len(found))
        for sym_id, entries in sorted(found.items()):
            spans = ", ".join(v for v, _r in entries)
            rule = entries[0][1]
            detail = " ".join("%s=%s" % (k, v) for k, v in sorted(rule.items()))
            print("  %-36s %s" % (sym_id, detail))
            print("  %-36s on %s" % ("", spans))
    else:
        print("No renamed symbols found.")

    total_absent = sum(len(v) for v in unexplained.values())
    genuine = sum(1 for v in unexplained.values() for _i, why in v if why == "genuinely absent")
    print("\n%d absences explained as renames, %d genuinely absent"
          % (sum(len(e) for e in found.values()), genuine))
    if total_absent - genuine:
        print("%d absences could not be classified:" % (total_absent - genuine))
        for version, items in sorted(unexplained.items()):
            for sym_id, why in items:
                if why != "genuinely absent":
                    print("  %-8s %-34s %s" % (version, sym_id, why))
    return 0


if __name__ == "__main__":
    sys.exit(main())
