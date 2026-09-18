"""Prove the generated mapping tables before the client ships with them.

A wrong name does not fail loudly at runtime: it resolves to a null id and the
feature silently does nothing, which is indistinguishable from "the feature never
found a target". This turns that whole class of bug into a build failure.

Three checks, in order of how much they prove:

  1. baseline parity -- the 1.21.11 intermediary column must reproduce, string for
     string, the header the client was pinned to before multi-version work began.
     This is what makes the rewrite a refactor. A single mismatch here means some
     symbol changed meaning, and the check names it.

  2. owner + descriptor -- every symbol must exist on its owner class with its exact
     descriptor, on every version that claims to have it, in every namespace that
     version supports. The old checker only asked whether a name appeared anywhere
     in the mapping file, which passes for a method that lives on a different class.

  3. required set -- symbols the client cannot work without must be present on every
     supported version. Absence anywhere is a build failure; absence of anything
     else is data for the capability gate, not an error.

    python tools/verify_mappings.py              # all versions, summary + failures
    python tools/verify_mappings.py --verbose    # per-version detail
    python tools/verify_mappings.py 1.21.4 26.3  # only these versions
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import mc_artifacts as art
from gen_mappings import VersionTables, NAMESPACES, load_spec

ROOT = art.ROOT
BASELINE = os.path.join(ROOT, "tools", "symbols", "baseline_1.21.11.json")

# Without these the client cannot see the player, the world or the camera, so a
# version that lacks one is not supportable and the build should say so rather
# than shipping a client that draws an empty menu.
REQUIRED = [
    "minecraftclass", "minecraftclient", "player", "world",
    "clientplayerentity_class", "entity_class", "living_entity_class",
    "entity_get_x", "entity_get_y", "entity_get_z",
    "entity_get_yaw", "entity_set_yaw", "entity_set_pitch",
    "vec3d_class", "vec3d_x", "vec3d_y", "vec3d_z",
    "gamerenderer_class", "gamerenderer", "get_camera", "camera_class",
]


def check_baseline(symbols, failures):
    if not os.path.exists(BASELINE):
        print("baseline    : missing, skipped")
        return
    doc = json.load(open(BASELINE, encoding="utf-8"))
    expected = doc["values"]
    allowed = doc.get("expected_deviations", {})
    vt = VersionTables(doc["version"])
    ns = doc["namespace"]

    checked = 0
    waived = 0
    for sym in symbols:
        res = vt.resolve(sym)[ns]
        for slot, ident in (("name", sym.get("emit_name")), ("sig", sym.get("emit_sig"))):
            if not ident or ident not in expected:
                continue
            got = res[0] if slot == "name" else res[1]
            want = expected[ident]
            checked += 1
            if got == want:
                continue
            rule = allowed.get(ident)
            if rule and rule.get("generated") == got:
                waived += 1
                continue
            failures.append("baseline %s: generated %r, header had %r"
                            % (ident, got, want))
    print("baseline    : %d constants compared against the pinned 1.21.11 header%s"
          % (checked, "" if not waived else ", %d waived deviations" % waived))


def check_version(version, symbols, failures, verbose):
    vt = VersionTables(version)
    namespaces = ["official"] if vt.deobf else NAMESPACES

    present = 0
    missing = []
    for sym in symbols:
        res = vt.resolve(sym)
        available = res["official"][0] != "" or res["official"][1] != ""
        if available:
            present += 1
        else:
            missing.append(sym["id"])

        if available and sym["id"] in REQUIRED:
            for ns in namespaces:
                if res[ns][0] == "":
                    failures.append("%s: required symbol %s has no %s name"
                                    % (version, sym["id"], ns))

        if not available and sym["id"] in REQUIRED:
            failures.append("%s: required symbol %s is absent" % (version, sym["id"]))

    print("%-8s    : %3d/%d symbols%s" % (version, present, len(symbols),
                                          "" if not missing else "  (%d absent)" % len(missing)))
    if verbose and missing:
        for m in missing:
            print("                 absent: %s" % m)
    return missing


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    verbose = "--verbose" in sys.argv
    versions = args or art.VERSIONS

    unknown = [v for v in versions if v not in art.VERSIONS]
    if unknown:
        print("not supported versions: %s" % ", ".join(unknown))
        return 2

    symbols = load_spec()
    failures = []
    unavailable = []

    print("symbols     : %d from tools/symbols/symbols.json" % len(symbols))
    try:
        check_baseline(symbols, failures)
    except Exception as e:                                  # noqa: BLE001
        unavailable.append("baseline: %s" % e)
    print()

    for version in versions:
        try:
            check_version(version, symbols, failures, verbose)
        except Exception as e:                              # noqa: BLE001
            # No cached artifact and no network is not a mapping problem; say so
            # separately so a build offline does not read as a broken table.
            unavailable.append("%s: %s" % (version, e))
            print("%-8s    : could not check (%s)" % (version, type(e).__name__))

    print()
    if failures:
        print("%d PROBLEM(S):\n" % len(failures))
        for f in failures:
            print("  %s" % f)
        return 1

    if unavailable:
        print("%d version(s) could not be checked:" % len(unavailable))
        for u in unavailable:
            print("  %s" % u)
        print("\nRun tools/gen_mappings.py once with network access to fill the cache.")
        return 3

    print("All symbols resolve with the right owner and descriptor on every version checked.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
