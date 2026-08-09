"""Check every intermediary name in sdk/mappings/mappings.hpp against the
official Yarn mapping file.

A wrong obfuscated name does not fail loudly — it resolves to a null id and the
feature silently does nothing. This turns that whole class of bug into a build-
time check.

    python tools/verify_mappings.py
"""

import os
import re
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JAR = os.path.join(ROOT, "tools", "mappings", "yarn-1.21.11+build.6-v2.jar")
HDR = os.path.join(ROOT, "sdk", "mappings", "mappings.hpp")


def load_mappings():
    with zipfile.ZipFile(JAR) as zf:
        raw = zf.read("mappings/mappings.tiny").decode("utf-8")

    classes = {}          # intermediary class -> named
    methods = {}          # intermediary method -> list of (owner, desc, named)
    fields = {}           # intermediary field  -> list of (owner, desc, named)
    current = None

    for line in raw.split("\n"):
        p = line.split("\t")
        if p[0] == "c" and len(p) >= 3:
            current = p[1]
            classes[p[1]] = p[2]
        elif current is not None and p[0] == "" and len(p) >= 5:
            table = methods if p[1] == "m" else fields if p[1] == "f" else None
            if table is not None:
                table.setdefault(p[3], []).append((current, p[2], p[4]))

    return classes, methods, fields


def parse_header():
    """Returns {c_identifier: value} for every `static const char* x = "...";`"""
    text = open(HDR, encoding="utf-8", errors="replace").read()
    out = {}
    for m in re.finditer(r'static\s+const\s+char\*\s+(\w+)\s*=\s*"([^"]*)"', text):
        out[m.group(1)] = m.group(2)
    return out


def main():
    classes, methods, fields = load_mappings()
    entries = parse_header()

    bad, ok, skipped = [], 0, 0

    for ident, value in sorted(entries.items()):
        if not value:
            continue

        if value.startswith("net/minecraft/class_"):
            base = value.split("$")[0]
            if base in classes:
                ok += 1
            else:
                bad.append((ident, value, "class not in mappings"))
        elif value.startswith("method_"):
            if value in methods:
                ok += 1
            else:
                bad.append((ident, value, "method not in mappings"))
        elif value.startswith("field_"):
            if value in fields:
                ok += 1
            else:
                bad.append((ident, value, "field not in mappings"))
        else:
            skipped += 1

    print("checked %d intermediary names, %d non-intermediary values skipped"
          % (ok + len(bad), skipped))

    if not bad:
        print("\nAll intermediary names resolve against Yarn 1.21.11+build.6.")
        return 0

    print("\n%d NAME(S) DO NOT RESOLVE:\n" % len(bad))
    for ident, value, why in bad:
        print("  %-46s %-16s %s" % (ident, value, why))
    return 1


if __name__ == "__main__":
    sys.exit(main())
