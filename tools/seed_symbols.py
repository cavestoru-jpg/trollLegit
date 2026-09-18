"""One-shot bootstrap: turn sdk/mappings/mappings.hpp into tools/symbols/symbols.json.

The header names every symbol in one namespace (Yarn intermediary, 1.21.11).  The
multi-version tables need a key that survives the jump to 26.x, where intermediary
does not exist at all, so each constant is reversed into its **official Mojang**
triple (owner, name, descriptor) through the 1.21.11 artifacts:

    class_310         -> obf gfj          -> net/minecraft/client/Minecraft
    field_1724 + sig  -> obf gfj.s        -> Minecraft.player

Ambiguity is real and is reported rather than guessed: one intermediary method id
appears on every class in an override chain, so a candidate list is narrowed by
the javadoc URL the header already carries above most constants, and anything left
over is printed for a human to settle.

    python tools/seed_symbols.py [--write]

Without --write it prints the report and touches nothing.
"""

import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import mc_artifacts as art

ROOT = art.ROOT
# The pinned header as it stood before multi-version work, archived in the repo.
# The live sdk/mappings/mappings.hpp no longer holds values -- it declares runtime
# bindings -- so seeding reads this instead. It also keeps the comments, which are
# what made each symbol choice defensible in the first place.
HDR = os.path.join(ROOT, "tools", "symbols", "mappings_pinned_1.21.11.hpp")
OUT = os.path.join(ROOT, "tools", "symbols", "symbols.json")
BASE = "1.21.11"

CONST_RE = re.compile(
    r'static\s+const\s+char\*\s+(\w+)\s*=\s*((?:"(?:[^"\\]|\\.)*"\s*)+);', re.S)
STR_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')
URL_RE = re.compile(r'yarn-[^/]+/(net/minecraft/[\w/]+)\.html(?:#(\w+))?')


def parse_header():
    """[(identifier, value, yarn_class_hint, yarn_member_hint)] in file order."""
    text = open(HDR, encoding="utf-8", errors="replace").read()
    out = []
    for m in CONST_RE.finditer(text):
        ident = m.group(1)
        value = "".join(STR_RE.findall(m.group(2)))
        # the javadoc URL closest above this constant
        head = text[:m.start()]
        urls = URL_RE.findall(head[-600:])
        cls_hint, mem_hint = (urls[-1] if urls else (None, None))
        out.append((ident, value, cls_hint, mem_hint))
    return out


def pair_sig(entries):
    """identifier -> the descriptor constant that belongs with it.

    `foo_name` pairs with `foo_sig`.  Four groups in the header share one
    descriptor between several names (`equipment_slot_*`, `entity_last_render_*`,
    `visibility_never`), so a miss retries on progressively shorter prefixes.
    """
    by_ident = {i: v for i, v, _c, _m in entries}
    pairs = {}
    for ident, value, _c, _m in entries:
        if not ident.endswith("_name"):
            continue
        stem = ident[:-5]
        while stem:
            cand = stem + "_sig"
            if cand in by_ident:
                pairs[ident] = cand
                break
            if "_" not in stem:
                break
            stem = stem.rsplit("_", 1)[0]
    return pairs


class Reverser:
    def __init__(self, version):
        self.version = version
        pg_classes, pg_members = art.parse_proguard(art.client_mappings(version))
        i_classes, i_members = art.parse_tiny_v2(art.intermediary_tiny(version))
        self.pg_classes = pg_classes
        self.i_classes = i_classes
        self.obf_to_off_class = {v: k for k, v in pg_classes.items()}
        self.inter_to_obf_class = {v: k for k, v in i_classes.items()}

        self.off_by_obf = {}
        for (owner, name, desc, kind), obf_name in pg_members.items():
            obf_owner = pg_classes.get(owner)
            if obf_owner is None:
                continue
            key = (obf_owner, obf_name, art.remap_descriptor(desc, pg_classes), kind)
            self.off_by_obf[key] = (owner, name, desc)

        self.by_inter = {}
        for key, inter in i_members.items():
            self.by_inter.setdefault(inter, []).append(key)

        # intermediary class -> Yarn named class, for the javadoc-URL tiebreak
        self.yarn_named = {}
        yarn = art.yarn_tiny(version)
        if yarn:
            yc, _ym = art.parse_tiny_v2(yarn)
            self.yarn_named = yc

    def official_class(self, inter_class):
        obf = self.inter_to_obf_class.get(inter_class)
        if obf is None:
            return None
        return self.obf_to_off_class.get(obf)

    def official_member(self, inter_name, inter_desc, kind, class_hint, pin_owner=None):
        """-> (owner, name, desc, candidates, note)"""
        cands = self.by_inter.get(inter_name, [])
        if not cands:
            return None, None, None, [], "intermediary name not in %s" % self.version

        typed = []
        for obf_owner, obf_name, obf_desc, k in cands:
            if k != kind:
                continue
            if inter_desc and art.remap_descriptor(obf_desc, self.i_classes) != inter_desc:
                continue
            off = self.off_by_obf.get((obf_owner, obf_name, obf_desc, k))
            if off:
                typed.append(off)

        if not typed:
            return None, None, None, [], "no official member for %s %s" % (inter_name, inter_desc or "")

        uniq = sorted(set(typed))
        if pin_owner:
            pinned = [u for u in uniq if u[0] == pin_owner]
            if pinned:
                return pinned[0][0], pinned[0][1], pinned[0][2], [], ""
            return uniq[0][0], uniq[0][1], uniq[0][2], [u[0] for u in uniq], \
                "override owner %s is not a candidate" % pin_owner
        if len(uniq) == 1:
            return uniq[0][0], uniq[0][1], uniq[0][2], [], ""

        if class_hint:
            want = class_hint.split("/")[-1]
            for owner, name, desc in uniq:
                obf = self.pg_classes.get(owner)
                inter = self.i_classes.get(obf, "")
                if self.yarn_named.get(inter, "").split("/")[-1] == want:
                    return owner, name, desc, [u[0] for u in uniq], "picked by javadoc hint"
        owner, name, desc = uniq[0]
        return owner, name, desc, [u[0] for u in uniq], "AMBIGUOUS, took first"


def load_overrides():
    path = os.path.join(ROOT, "tools", "symbols", "overrides.json")
    if not os.path.exists(path):
        return {}
    doc = json.load(open(path, encoding="utf-8"))
    return {k: v for k, v in doc.items() if k != "comment"}


def check_override(ov, sym_id, problems):
    """Prove a hand-written triple actually exists on the version it claims."""
    version = ov.get("resolve_from")
    if not version or "name" not in ov:
        return
    pgc, pgm = art.parse_proguard(art.client_mappings(version))
    kind = "f" if ov["kind"] == "field" else "m"
    if (ov["owner"], ov["name"], ov["desc"], kind) not in pgm:
        problems.append((sym_id, ov["name"],
                         "override does not exist on %s" % version))


def main():
    write = "--write" in sys.argv
    entries = parse_header()
    # An empty parse means the source moved or changed shape. Writing the result
    # anyway would replace a curated spec with nothing, which is exactly what
    # happened once when this pointed at the live header after it was converted to
    # extern declarations.
    if len(entries) < 300:
        print("refusing to seed: %s yielded only %d constants (expected ~352)"
              % (HDR, len(entries)))
        return 1
    pairs = pair_sig(entries)
    rev = Reverser(BASE)
    overrides = load_overrides()
    applied = set()

    by_ident = {i: v for i, v, _c, _m in entries}
    hints = {i: (c, m) for i, _v, c, m in entries}

    symbols = []
    literals = []
    problems = []
    consumed_sigs = set()

    for ident, value, cls_hint, _mem_hint in entries:
        if ident.endswith("_sig") and value.startswith("net/minecraft/class_"):
            off = rev.official_class(value)
            if off is None:
                problems.append((ident, value, "class not resolvable in %s" % BASE))
                continue
            class_id = ident[:-4]
            class_ov = overrides.get(class_id, {})
            entry = {
                "id": class_id,
                "kind": "class", "class": class_ov.get("class", off),
                "emit_sig": ident, "emit_name": None,
                "seed": {"intermediary": value},
            }
            if class_ov.get("versions"):
                entry["versions"] = class_ov["versions"]
            if class_ov:
                applied.add(class_id)
            symbols.append(entry)
            consumed_sigs.add(ident)
            continue

        if not ident.endswith("_name"):
            continue

        sym_id = ident[:-5]
        ov = overrides.get(sym_id, {})

        if value == "":
            # The header parks a symbol the base version does not have on an empty
            # string and every consumer gates on name[0]. The spec carries the real
            # triple instead, so the generator can serve it on the versions that do.
            if "name" not in ov:
                problems.append((ident, "(empty)",
                                 "sentinel with no override in tools/symbols/overrides.json"))
                continue
            check_override(ov, sym_id, problems)
            applied.add(sym_id)
            entry = {
                "id": sym_id, "kind": ov["kind"],
                "owner": ov["owner"], "name": ov["name"], "desc": ov["desc"],
                "emit_name": ident, "emit_sig": pairs.get(ident),
                "seed": {"from": ov.get("resolve_from"), "why": ov.get("why")},
            }
            # Carried here too: a symbol described entirely by an override can
            # still move between classes later, and dropping its rules made
            # input_forward read as absent from 1.21.2 on when it had only
            # changed owner.
            if ov.get("versions"):
                entry["versions"] = ov["versions"]
            symbols.append(entry)
            if pairs.get(ident):
                consumed_sigs.add(pairs[ident])
            continue

        kind = "m" if value.startswith("method_") else "f" if value.startswith("field_") else None
        if kind is None:
            # A name that is not mangled in any namespace (netty callbacks,
            # java.lang methods). The descriptor beside it is still a real
            # descriptor and has to be carried separately -- folding both into one
            # string is how the signature silently became the name.
            sig_ident = pairs.get(ident)
            literals.append({"id": ident[:-5], "kind": "literal",
                             "literal_name": value,
                             "literal_sig": by_ident.get(sig_ident, "") if sig_ident else "",
                             "emit_name": ident, "emit_sig": sig_ident})
            if sig_ident:
                consumed_sigs.add(sig_ident)
            continue

        sig_ident = pairs.get(ident)
        inter_desc = by_ident.get(sig_ident) if sig_ident else None
        if ov.get("owner"):
            applied.add(sym_id)
        owner, name, desc, cands, note = rev.official_member(
            value, inter_desc, kind, cls_hint or (hints.get(sig_ident) or (None, None))[0],
            pin_owner=ov.get("owner"))
        if owner is None:
            problems.append((ident, value, note))
            continue
        if note:
            problems.append((ident, value, "%s (%s)" % (note, ", ".join(cands))))

        entry = {
            "id": sym_id,
            "kind": "field" if kind == "f" else "method",
            "owner": owner, "name": name, "desc": desc,
            "emit_name": ident,
            "emit_sig": sig_ident if sig_ident not in consumed_sigs else None,
            "seed": {"intermediary": value},
        }
        if ov.get("versions"):
            entry["versions"] = ov["versions"]
            applied.add(sym_id)
        symbols.append(entry)
        if sig_ident:
            consumed_sigs.add(sig_ident)

    # A _sig with no _name partner and no intermediary content is a plain JVM name
    # (netty, java.lang) that means the same thing on every version.
    unconsumed = []
    for ident, value, _c, _m in entries:
        if not ident.endswith("_sig") or ident in consumed_sigs:
            continue
        if value.startswith("net/minecraft/class_"):
            continue
        if "class_" in value or "method_" in value or "field_" in value:
            unconsumed.append(ident)
            continue
        literals.append({"id": ident[:-4], "kind": "literal",
                         "literal_name": "", "literal_sig": value,
                         "emit_name": None, "emit_sig": ident})

    print("header constants      : %d" % len(entries))
    print("resolved symbols      : %d (%d class, %d method, %d field)"
          % (len(symbols),
             sum(1 for s in symbols if s["kind"] == "class"),
             sum(1 for s in symbols if s["kind"] == "method"),
             sum(1 for s in symbols if s["kind"] == "field")))
    print("namespace-free literals: %d" % len(literals))
    print("unresolved / to review : %d" % len(problems))
    print("orphan _sig constants  : %d" % len(unconsumed))

    # Symbols that exist on no version the pinned header knew about, so there is
    # nothing to seed them from. 26.x keeps introducing API the 1.21.11 client had
    # no reason to name, and the spec has to be able to grow.
    added = []
    for sym_id, ov in sorted(overrides.items()):
        if not ov.get("new"):
            continue
        applied.add(sym_id)
        entry = {"id": sym_id, "kind": ov["kind"],
                 "emit_name": ov.get("emit_name"), "emit_sig": ov.get("emit_sig"),
                 "seed": {"why": ov.get("why")}}
        if ov["kind"] == "class":
            entry["class"] = ov["class"]
        else:
            entry.update({"owner": ov["owner"], "name": ov["name"], "desc": ov["desc"]})
        if ov.get("versions"):
            entry["versions"] = ov["versions"]
        symbols.append(entry)
        added.append(sym_id)
    if added:
        print("declared-new symbols   : %s" % ", ".join(added))

    stale = sorted(k for k in overrides if k not in applied)
    if stale:
        print("stale overrides        : %s" % ", ".join(stale))

    if problems:
        print("\n--- needs review -------------------------------------------------")
        for ident, value, why in problems:
            print("  %-46s %-16s %s" % (ident, value, why))
    if unconsumed:
        print("\n--- _sig constants with no _name partner --------------------------")
        for i in unconsumed:
            print("  %-46s %s" % (i, by_ident[i]))

    if write:
        os.makedirs(os.path.dirname(OUT), exist_ok=True)
        doc = {
            "comment": "Canonical symbol spec, keyed by official Mojang names. "
                       "Generated by tools/seed_symbols.py from mappings.hpp, then curated by hand.",
            "base_version": BASE,
            "symbols": symbols + literals,
        }
        with open(OUT, "w", encoding="utf-8") as f:
            json.dump(doc, f, indent=2)
            f.write("\n")
        print("\nwrote %s (%d entries)" % (OUT, len(doc["symbols"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
