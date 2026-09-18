"""Rewrite sdk/mappings/mappings.hpp from compile-time constants to runtime bindings.

    static const char* player_name = "field_1724";   ->   extern const char* player_name;

The values move into the generated table (sdk/mappings/mappings_gen.inc) and are
assigned once, after the version and namespace are known.  Every comment in the
header is kept: they carry the reasoning that made each symbol the right one, and
that reasoning outlives any single version's spelling.

The 835 call sites are unaffected -- all of them pass the pointer at runtime.

    python tools/externalize_mappings.py          # show what would change
    python tools/externalize_mappings.py --write
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HDR = os.path.join(ROOT, "sdk", "mappings", "mappings.hpp")

CONST_RE = re.compile(
    r'(?P<indent>[ \t]*)static\s+const\s+char\*\s+(?P<name>\w+)\s*=\s*'
    r'(?P<value>(?:"(?:[^"\\]|\\.)*"\s*)+);')


def main():
    write = "--write" in sys.argv
    text = open(HDR, encoding="utf-8", errors="replace").read()

    if "extern const char*" in text:
        print("header already externalised; nothing to do")
        return 0

    names = []

    def repl(m):
        names.append(m.group("name"))
        return "%sextern const char* %s;" % (m.group("indent"), m.group("name"))

    out = CONST_RE.sub(repl, text)

    banner = """// Values are bound at runtime, not at compile time.
//
// Every constant below used to hold a Yarn 1.21.11 intermediary string. The client
// now runs on 1.20 through 26.3, across three namespaces (official, intermediary,
// obfuscated), so the spelling of a symbol is a property of the detected version --
// not of this file. sdk/mappings/mappings.cpp binds every pointer here from the
// generated table once sdk::version has identified the game.
//
// A symbol the running version does not have binds to "" , which is the sentinel
// consumers already gate on (`name[0] != '\\0'`). Read the comments below for WHY a
// symbol is the right one; read tools/symbols/symbols.json for what it resolves to.
"""
    out = out.replace("#ifndef MAPPINGS_HPP", banner + "#ifndef MAPPINGS_HPP", 1)

    print("constants converted: %d" % len(names))
    if not write:
        print("(dry run -- pass --write to apply)")
        return 0

    open(HDR, "w", encoding="utf-8").write(out)
    print("rewrote %s" % HDR)
    return 0


if __name__ == "__main__":
    sys.exit(main())
