"""Generate sdk/mappings/mappings_gen.inc from tools/symbols/symbols.json.

For every supported version the symbol spec (official Mojang names) is projected
into the three namespaces the client can meet at runtime:

    official      what 26.x runs, and what a mojmap-remapped instance runs
    intermediary  what a Fabric instance runs on 1.20-1.21.11
    obfuscated    what a vanilla launcher runs on 1.20-1.21.11

A symbol that does not exist on a version is emitted as an empty string, which is
the sentinel every existing consumer already gates on (`name[0] != '\\0'`), so a
missing symbol degrades exactly the way the hand-written ones used to.

    python tools/gen_mappings.py            # write the header + coverage report
    python tools/gen_mappings.py --report   # print divergences, write nothing
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import mc_artifacts as art

ROOT = art.ROOT
SPEC = os.path.join(ROOT, "tools", "symbols", "symbols.json")
OUT_INC = os.path.join(ROOT, "sdk", "mappings", "mappings_gen.inc")
OUT_REPORT = os.path.join(ROOT, "tools", "symbols", "coverage.txt")

NAMESPACES = ["official", "intermediary", "obfuscated"]


def load_spec():
    doc = json.load(open(SPEC, encoding="utf-8"))
    return doc["symbols"]


class VersionTables:
    """Everything needed to name any symbol on one version."""

    def __init__(self, version):
        self.version = version
        self.deobf = art.is_deobfuscated(version)
        self.jar = art.JarIndex(version) if self.deobf else None
        self._members_cache = {}
        if not self.deobf:
            self.pg_classes, self.pg_members = art.parse_proguard(art.client_mappings(version))
            self.i_classes, self.i_members = art.parse_tiny_v2(art.intermediary_tiny(version))

    # -- deobfuscated versions: the jar is the only source of truth -------------

    def _jar_members(self, owner):
        if owner not in self._members_cache:
            self._members_cache[owner] = self.jar.members(owner)
        return self._members_cache[owner]

    # -- per-version rules ------------------------------------------------------

    def applies(self, rule):
        """A rule is in force when this version sits inside its [since, until] window."""
        ordinal = art.ordinal(self.version)
        since = rule.get("since")
        until = rule.get("until")
        if since is not None and ordinal < art.ordinal(since):
            return False
        if until is not None and ordinal > art.ordinal(until):
            return False
        return True

    def apply_rules(self, sym):
        """The owner/name/descriptor this symbol has on THIS version.

        Mojang renames and moves members, so one official triple does not hold
        across 24 releases. Later matching rules win, which lets a symbol be
        described once and then corrected for the versions that drifted.

        A rule carrying `blocked_by` records a symbol that was FOUND but must not
        be served yet, because its call shape changed and the caller would pass the
        wrong arguments. Binding it would turn a visibly missing feature into a JVM
        crash, so the triple is kept as documentation and the symbol reports absent
        until the calling code learns the new shape.
        """
        owner, name, desc = sym.get("owner"), sym.get("name"), sym.get("desc")
        for rule in sym.get("versions", []):
            if not self.applies(rule):
                continue
            if rule.get("blocked_by"):
                return None, None, None
            owner = rule.get("owner", owner)
            name = rule.get("name", name)
            desc = rule.get("desc", desc)
        return owner, name, desc

    def class_name(self, sym):
        owner = sym["class"]
        for rule in sym.get("versions", []):
            if self.applies(rule):
                owner = rule.get("class", owner)
        return owner

    # -- one symbol, three namespaces ------------------------------------------

    def resolve(self, sym):
        """-> {namespace: (name, sig, owner)}; ("", "", "") where unavailable.

        The owner travels with the symbol because members move between classes:
        GameRenderer.pick became Minecraft.pick in 26.1 and GameRenderer.getFov
        became Camera.getFov. A call site that pairs a name with a separately
        chosen class constant silently targets the wrong class when that happens.
        """
        empty = {ns: ("", "", "") for ns in NAMESPACES}

        if sym["kind"] == "literal":
            triple = (sym.get("literal_name", ""), sym.get("literal_sig", ""), "")
            return {ns: triple for ns in NAMESPACES}

        if sym["kind"] == "class":
            owner = self.class_name(sym)
            if self.deobf:
                if not self.jar.has_class(owner):
                    return empty
                return {"official": (owner, owner, owner),
                        "intermediary": ("", "", ""), "obfuscated": ("", "", "")}
            obf = self.pg_classes.get(owner)
            if obf is None:
                return empty
            inter = self.i_classes.get(obf, obf)
            return {"official": (owner, owner, owner),
                    "intermediary": (inter, inter, inter),
                    "obfuscated": (obf, obf, obf)}

        owner, name, desc = self.apply_rules(sym)
        if owner is None:
            return empty
        kind = "f" if sym["kind"] == "field" else "m"

        if self.deobf:
            members = self._jar_members(owner)
            if members is None:
                return empty
            fields, methods = members
            table = fields if kind == "f" else methods
            if (name, desc) not in table:
                return empty
            return {"official": (name, desc, owner),
                    "intermediary": ("", "", ""), "obfuscated": ("", "", "")}

        obf_owner = self.pg_classes.get(owner)
        obf_name = self.pg_members.get((owner, name, desc, kind))
        if obf_owner is None or obf_name is None:
            return empty
        obf_desc = art.remap_descriptor(desc, self.pg_classes)

        # A member Fabric did not name keeps its obfuscated name in a remapped jar,
        # so the intermediary namespace falls back to the obfuscated name rather
        # than reporting the symbol missing.
        inter_name = self.i_members.get((obf_owner, obf_name, obf_desc, kind), obf_name)
        inter_desc = art.remap_descriptor(obf_desc, self.i_classes)
        inter_owner = self.i_classes.get(obf_owner, obf_owner)

        return {"official": (name, desc, owner),
                "intermediary": (inter_name, inter_desc, inter_owner),
                "obfuscated": (obf_name, obf_desc, obf_owner)}


class StringPool:
    """Index 0 is always the empty string, which is the 'unavailable' sentinel."""

    def __init__(self):
        self.items = [""]
        self.index = {"": 0}

    def add(self, s):
        if s not in self.index:
            self.index[s] = len(self.items)
            self.items.append(s)
        return self.index[s]


def c_escape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def main():
    report_only = "--report" in sys.argv
    symbols = load_spec()

    pool = StringPool()
    # table[ns][version][symbol] = (name_idx, sig_idx)
    table = {ns: {v: [] for v in art.VERSIONS} for ns in NAMESPACES}
    coverage = {}

    for version in art.VERSIONS:
        vt = VersionTables(version)
        missing = []
        for sym in symbols:
            res = vt.resolve(sym)
            for ns in NAMESPACES:
                name, sig, owner = res[ns]
                table[ns][version].append((pool.add(name), pool.add(sig), pool.add(owner)))
            # A sig-only literal (a bare class name like the netty adapter) has no
            # name slot and is still perfectly present, so absence means both slots
            # are empty.
            if res["official"][0] == "" and res["official"][1] == "":
                missing.append(sym["id"])
        coverage[version] = missing
        print("%-8s %s  %d/%d symbols" % (
            version, "deobf " if vt.deobf else "obf   ",
            len(symbols) - len(missing), len(symbols)))

    lines = []
    lines.append("// Generated by tools/gen_mappings.py -- do not edit by hand.")
    lines.append("// Source of truth: tools/symbols/symbols.json")
    lines.append("//")
    lines.append("// %d symbols x %d versions x %d namespaces."
                 % (len(symbols), len(art.VERSIONS), len(NAMESPACES)))
    lines.append("")

    lines.append("#ifdef MAPPINGS_GEN_STRINGS")
    lines.append("static const char* const g_strings[] = {")
    for s in pool.items:
        lines.append('\t"%s",' % c_escape(s))
    lines.append("};")
    lines.append("static const unsigned g_string_count = %d;" % len(pool.items))
    lines.append("#endif")
    lines.append("")

    lines.append("#ifdef MAPPINGS_GEN_VERSIONS")
    lines.append("static const char* const g_version_names[] = {")
    for v in art.VERSIONS:
        lines.append('\t"%s",' % v)
    lines.append("};")
    lines.append("#endif")
    lines.append("")

    lines.append("#ifdef MAPPINGS_GEN_TABLE")
    lines.append("// [namespace][version][symbol] -> { name, signature, owner class }")
    lines.append("// as string-pool indices. Index 0 is the empty string: symbol absent here.")
    lines.append("static const unsigned short g_table[%d][%d][%d][3] = {"
                 % (len(NAMESPACES), len(art.VERSIONS), len(symbols)))
    for ns in NAMESPACES:
        lines.append("\t{ // %s" % ns)
        for v in art.VERSIONS:
            row = table[ns][v]
            cells = ",".join("{%d,%d,%d}" % (a, b, c) for a, b, c in row)
            lines.append("\t\t{%s}, // %s" % (cells, v))
        lines.append("\t},")
    lines.append("};")
    lines.append("#endif")
    lines.append("")

    lines.append("// MAPPINGS_SYM(index, name_identifier, sig_identifier)")
    lines.append("// MAPPINGS_SYM_NAME(index, name_identifier)   -- shares a sig with a sibling")
    lines.append("// MAPPINGS_SYM_SIG(index, sig_identifier)     -- class names and plain literals")
    lines.append("#ifdef MAPPINGS_GEN_BINDINGS")
    for i, sym in enumerate(symbols):
        nm, sg = sym.get("emit_name"), sym.get("emit_sig")
        if nm and sg:
            lines.append("MAPPINGS_SYM(%d, %s, %s)" % (i, nm, sg))
        elif nm:
            lines.append("MAPPINGS_SYM_NAME(%d, %s)" % (i, nm))
        elif sg:
            lines.append("MAPPINGS_SYM_SIG(%d, %s)" % (i, sg))
    lines.append("#endif")
    lines.append("")

    lines.append("#ifdef MAPPINGS_GEN_IDS")
    for i, sym in enumerate(symbols):
        lines.append("MAPPINGS_ID(%d, %s)" % (i, sym["id"]))
    lines.append("#endif")
    lines.append("")

    text = "\n".join(lines)

    report = []
    report.append("Symbol coverage, generated by tools/gen_mappings.py")
    report.append("%d symbols across %d versions" % (len(symbols), len(art.VERSIONS)))
    report.append("")
    for version in art.VERSIONS:
        miss = coverage[version]
        report.append("%-8s %3d missing%s" % (version, len(miss), ":" if miss else ""))
        for m in miss:
            report.append("    %s" % m)
    report_text = "\n".join(report) + "\n"

    if report_only:
        print()
        print(report_text)
        return 0

    with open(OUT_INC, "w", encoding="utf-8") as f:
        f.write(text + "\n")
    with open(OUT_REPORT, "w", encoding="utf-8") as f:
        f.write(report_text)
    print()
    print("wrote %s (%d KB, %d pooled strings)"
          % (OUT_INC, len(text) // 1024, len(pool.items)))
    print("wrote %s" % OUT_REPORT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
