"""Look up what a class actually declares on a given Minecraft version.

Replaces the javap recipe in AGENT.md, which only worked against one installed
instance and only in intermediary. This reads the published artifacts instead, so
it answers for any supported version in any namespace without the game installed.

    python tools/find_symbol.py members 26.3 net/minecraft/client/renderer/GameRenderer
    python tools/find_symbol.py members 1.20.1 net/minecraft/client/Camera --grep yaw
    python tools/find_symbol.py trace 1.21.11 net/minecraft/client/Minecraft player
    python tools/find_symbol.py drift get_camera          # one symbol across all versions
    python tools/find_symbol.py search 26.3 net/minecraft/client/renderer/GameRenderer --desc "()L"

`trace` prints the name in all three namespaces -- the one thing the old tooling
could never do, and the reason a wrong guess used to cost a debugging session.
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import mc_artifacts as art
from gen_mappings import VersionTables, load_spec


def members(version, owner, grep=None, desc_filter=None):
    vt = VersionTables(version)
    if vt.deobf:
        got = vt.jar.members(owner)
        if got is None:
            print("%s: class %s not in the jar" % (version, owner))
            return 1
        fields, methods = got
        rows = [("f", n, d) for n, d in fields] + [("m", n, d) for n, d in methods]
    else:
        if owner not in vt.pg_classes:
            print("%s: class %s not in the mappings" % (version, owner))
            return 1
        rows = [(k, n, d) for (o, n, d, k) in vt.pg_members if o == owner]

    rows.sort(key=lambda r: (r[0], r[1] or "", r[2] or ""))
    shown = 0
    for kind, name, desc in rows:
        if grep and grep.lower() not in (name or "").lower():
            continue
        if desc_filter and desc_filter not in (desc or ""):
            continue
        shown += 1
        line = "  %s %-34s %s" % (kind, name, desc)
        if not vt.deobf:
            obf = vt.pg_members.get((owner, name, desc, kind))
            obf_owner = vt.pg_classes.get(owner)
            obf_desc = art.remap_descriptor(desc, vt.pg_classes)
            inter = vt.i_members.get((obf_owner, obf, obf_desc, kind), obf)
            line += "   -> %s / %s" % (inter, obf)
        print(line)
    print("%s %s: %d of %d members shown" % (version, owner, shown, len(rows)))
    return 0


def trace(version, owner, member):
    """One member, every namespace, with the descriptor that makes it unambiguous."""
    vt = VersionTables(version)
    if vt.deobf:
        got = vt.jar.members(owner)
        if got is None:
            print("class %s not present on %s" % (owner, version))
            return 1
        fields, methods = got
        hits = [("f", n, d) for n, d in fields if n == member] + \
               [("m", n, d) for n, d in methods if n == member]
        for kind, name, desc in hits:
            print("%-8s %s %-28s %s   official only (jar is not obfuscated)"
                  % (version, kind, name, desc))
        if not hits:
            print("%s: %s.%s not found" % (version, owner, member))
        return 0

    hits = [(k, n, d) for (o, n, d, k) in vt.pg_members if o == owner and n == member]
    if not hits:
        print("%s: %s.%s not found" % (version, owner, member))
        return 1
    for kind, name, desc in hits:
        obf_owner = vt.pg_classes[owner]
        obf = vt.pg_members[(owner, name, desc, kind)]
        obf_desc = art.remap_descriptor(desc, vt.pg_classes)
        inter_owner = vt.i_classes.get(obf_owner, obf_owner)
        inter = vt.i_members.get((obf_owner, obf, obf_desc, kind), obf)
        print("%s %s %s%s" % (version, kind, name, desc))
        print("   official     %s.%s" % (owner, name))
        print("   intermediary %s.%s" % (inter_owner, inter))
        print("   obfuscated   %s.%s" % (obf_owner, obf))
    return 0


def drift(symbol_id):
    """Where a spec symbol exists and where it does not, across the whole range."""
    spec = {s["id"]: s for s in load_spec()}
    sym = spec.get(symbol_id)
    if not sym:
        print("no symbol %r in tools/symbols/symbols.json" % symbol_id)
        return 1

    if sym["kind"] == "class":
        print("%s  class %s" % (symbol_id, sym["class"]))
    elif sym["kind"] == "literal":
        print("%s  literal %r / %r" % (symbol_id, sym.get("literal_name"), sym.get("literal_sig")))
    else:
        print("%s  %s %s.%s%s" % (symbol_id, sym["kind"], sym["owner"], sym["name"], sym["desc"]))
    print()

    for version in art.VERSIONS:
        vt = VersionTables(version)
        res = vt.resolve(sym)
        official, sig, owner = res["official"]
        inter = res["intermediary"][0]
        if official or sig:
            print("  %-8s ok    %-30s %-16s %s"
                  % (version, official or sig, inter or "-", owner))
        else:
            print("  %-8s ABSENT" % version)
    return 0


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2

    mode = sys.argv[1]
    grep = None
    desc_filter = None
    if "--grep" in sys.argv:
        grep = sys.argv[sys.argv.index("--grep") + 1]
    if "--desc" in sys.argv:
        desc_filter = sys.argv[sys.argv.index("--desc") + 1]
    args = [a for a in sys.argv[2:] if not a.startswith("--")]
    if grep:
        args = [a for a in args if a != grep]
    if desc_filter:
        args = [a for a in args if a != desc_filter]

    if mode in ("members", "search") and len(args) >= 2:
        return members(args[0], args[1].replace(".", "/"), grep, desc_filter)
    if mode == "trace" and len(args) >= 3:
        return trace(args[0], args[1].replace(".", "/"), args[2])
    if mode == "drift" and len(args) >= 1:
        return drift(args[0])

    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
