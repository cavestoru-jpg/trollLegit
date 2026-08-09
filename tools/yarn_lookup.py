"""Look up Yarn-named Minecraft symbols and print their intermediary names.

The client is pinned to one Minecraft version, so the whole guessing problem
goes away once the official Yarn tiny v2 file is on disk: it carries the
official, intermediary and named columns side by side, which is exactly the
named -> intermediary direction the mappings header needs.

    python tools/yarn_lookup.py class Scoreboard
    python tools/yarn_lookup.py method Scoreboard getPlayersTeam
    python tools/yarn_lookup.py field Entity lastRenderX
    python tools/yarn_lookup.py dump net/minecraft/scoreboard/Scoreboard
"""

import io
import os
import sys
import zipfile

JAR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "mappings", "yarn-1.21.11+build.6-v2.jar")


def load():
    """Returns classes: named -> (intermediary, [methods], [fields])."""
    with zipfile.ZipFile(JAR) as zf:
        entry = next(n for n in zf.namelist() if n.endswith("mappings.tiny"))
        raw = zf.read(entry).decode("utf-8")

    # This artifact declares only two namespaces -- "intermediary named" -- so
    # a class line is `c <inter> <named>` and a member line is
    # `\t<m|f>\t<desc>\t<inter>\t<named>`. Parameter lines (`\t\tp\t...`) and
    # comments (`\t\tc\t...`) are indented one level deeper and are skipped.
    classes = {}
    current = None

    for line in io.StringIO(raw):
        line = line.rstrip("\n")
        if not line or line.startswith("tiny\t"):
            continue
        parts = line.split("\t")

        if parts[0] == "c" and len(parts) >= 3:
            current = {"inter": parts[1], "named": parts[2],
                       "methods": [], "fields": []}
            classes[parts[2]] = current
        elif current is not None and parts[0] == "" and len(parts) >= 5:
            kind = parts[1]
            if kind == "m":
                current["methods"].append((parts[4], parts[3], parts[2]))
            elif kind == "f":
                current["fields"].append((parts[4], parts[3], parts[2]))

    return classes


def find_classes(classes, needle):
    n = needle.replace(".", "/").lower()
    return [c for c in classes.values()
            if c["named"].lower().endswith("/" + n) or c["named"].lower() == n]


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2

    classes = load()
    mode = sys.argv[1]

    if mode == "class":
        for c in find_classes(classes, sys.argv[2]):
            print("%-70s -> %s" % (c["named"], c["inter"]))
        return 0

    if mode == "dump":
        c = classes.get(sys.argv[2].replace(".", "/"))
        if not c:
            hits = find_classes(classes, sys.argv[2])
            if len(hits) != 1:
                print("ambiguous or not found; candidates:")
                for h in hits[:20]:
                    print("  ", h["named"])
                return 1
            c = hits[0]
        print("CLASS %s -> %s" % (c["named"], c["inter"]))
        for name, inter, desc in sorted(c["methods"]):
            print("  M %-40s %-18s %s" % (name, inter, desc))
        for name, inter, desc in sorted(c["fields"]):
            print("  F %-40s %-18s %s" % (name, inter, desc))
        return 0

    if mode in ("method", "field") and len(sys.argv) >= 4:
        want = sys.argv[3]
        for c in find_classes(classes, sys.argv[2]):
            for name, inter, desc in c[mode + "s"]:
                if name == want:
                    print("%s.%s%s\n  -> %s  %s" % (c["named"], name, desc, inter, desc))
        return 0

    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
