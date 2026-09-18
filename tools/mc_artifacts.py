"""Fetch and cache everything needed to name a Minecraft symbol on any supported version.

Three namespaces exist across the supported range and this module produces all three:

    official     Mojang's own names.  1.20-1.21.11 ship them as a separate ProGuard
                 mapping file; 26.x needs no mapping at all because the jar is not
                 obfuscated any more (Mojang stopped publishing client_mappings, and
                 Fabric publishes intermediary 0.0.0 for those versions).
    intermediary Fabric's stable class_NNNN / method_NNNNN names.  Only exist up to
                 1.21.11 -- and only at runtime under a Fabric loader, which remaps
                 the jar in place.
    obfuscated   what a vanilla launcher actually runs on 1.20-1.21.11.

The join that makes one symbol spec serve all three is the obfuscated name: the
ProGuard file maps official -> obfuscated, the intermediary tiny file maps
obfuscated -> intermediary.  Key a symbol by its official name and both other
namespaces fall out mechanically.

Everything is cached under tools/mappings/cache/ so a build is offline after the
first run.  The 26.x client jars are never downloaded whole: the ZIP central
directory is fetched with a range request and individual class files are pulled on
demand, which is ~2 MB instead of ~41 MB per version.
"""

import hashlib
import io
import json
import os
import struct
import sys
import time
import urllib.request
import zipfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(ROOT, "tools", "mappings", "cache")

PISTON_MANIFEST = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"
FABRIC_META = "https://meta.fabricmc.net/v2/versions"
FABRIC_MAVEN = "https://maven.fabricmc.net"

# The supported range, oldest first.  Order is significant: compat shims and the
# generated tables both use the index as the version ordinal.
VERSIONS = [
    "1.20", "1.20.1", "1.20.2", "1.20.3", "1.20.4", "1.20.5", "1.20.6",
    "1.21", "1.21.1", "1.21.2", "1.21.3", "1.21.4", "1.21.5", "1.21.6",
    "1.21.7", "1.21.8", "1.21.9", "1.21.10", "1.21.11",
    "26.1", "26.1.1", "26.1.2", "26.2", "26.3",
]

# Versions whose jars are not obfuscated.  For these, official names ARE the
# runtime names and there is no intermediary and no obfuscated namespace.
def is_deobfuscated(version):
    return not version.startswith("1.")


def ordinal(version):
    return VERSIONS.index(version)


def _cache_path(*parts):
    p = os.path.join(CACHE, *parts)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    return p


def _get(url, timeout=180, expect_sha1=None, expect_size=None, attempts=4):
    """Download with retries. A truncated body is a normal event on these CDNs and
    must never reach the cache: an 80%-complete ProGuard file parses fine and
    silently loses whatever symbols were in the missing tail."""
    last = None
    for attempt in range(attempts):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "enhance-mappings/1"})
            with urllib.request.urlopen(req, timeout=timeout) as r:
                data = r.read()
            if expect_size is not None and len(data) != expect_size:
                raise IOError("short read: %d of %d bytes" % (len(data), expect_size))
            if expect_sha1 is not None:
                got = hashlib.sha1(data).hexdigest()
                if got != expect_sha1:
                    raise IOError("sha1 mismatch: %s != %s" % (got, expect_sha1))
            return data
        except Exception as e:                      # noqa: BLE001 - retry anything
            last = e
            if attempt + 1 < attempts:
                print("   retry %d/%d after %s: %s"
                      % (attempt + 1, attempts - 1, type(e).__name__, e), file=sys.stderr)
                time.sleep(1.5 * (attempt + 1))
    raise last


def _cached(name, producer, binary=True):
    path = _cache_path(name)
    if os.path.exists(path) and os.path.getsize(path) > 0:
        mode = "rb" if binary else "r"
        with open(path, mode, **({} if binary else {"encoding": "utf-8"})) as f:
            return f.read()
    data = producer()
    mode = "wb" if binary else "w"
    with open(path, mode, **({} if binary else {"encoding": "utf-8"})) as f:
        f.write(data)
    return data


# --- Mojang -----------------------------------------------------------------

_manifest = None


def manifest():
    global _manifest
    if _manifest is None:
        raw = _cached("version_manifest_v2.json", lambda: _get(PISTON_MANIFEST))
        _manifest = json.loads(raw.decode("utf-8"))
    return _manifest


def version_json(version):
    raw = _cached(os.path.join(version, "version.json"), lambda: _version_json_dl(version))
    return json.loads(raw.decode("utf-8"))


def _version_json_dl(version):
    for v in manifest()["versions"]:
        if v["id"] == version:
            return _get(v["url"])
    raise KeyError("version %s is not in the Mojang manifest" % version)


def client_mappings(version):
    """ProGuard text mapping official -> obfuscated, or None when the jar is plain."""
    dl = version_json(version).get("downloads", {})
    if "client_mappings" not in dl:
        return None
    meta = dl["client_mappings"]
    raw = _cached(os.path.join(version, "client.txt"),
                  lambda: _get(meta["url"], expect_sha1=meta.get("sha1"),
                               expect_size=meta.get("size")))
    return raw.decode("utf-8")


# --- Fabric -----------------------------------------------------------------

def intermediary_tiny(version):
    """tiny v2 text with namespaces `official intermediary`, or None for 26.x."""
    if is_deobfuscated(version):
        return None
    raw = _cached(os.path.join(version, "intermediary-v2.jar"),
                  lambda: _get("%s/net/fabricmc/intermediary/%s/intermediary-%s-v2.jar"
                               % (FABRIC_MAVEN, version, version)))
    with zipfile.ZipFile(io.BytesIO(raw)) as zf:
        entry = next(n for n in zf.namelist() if n.endswith("mappings.tiny"))
        return zf.read(entry).decode("utf-8")


def yarn_tiny(version):
    """tiny v2 text with namespaces `intermediary named`, or None when Yarn stopped.

    Only used for human-facing tooling (yarn_lookup); the generated tables never
    depend on Yarn's chosen names.
    """
    if is_deobfuscated(version):
        return None
    builds = json.loads(_get("%s/yarn/%s" % (FABRIC_META, version)).decode("utf-8"))
    if not builds:
        return None
    build = builds[0]["version"]
    raw = _cached(os.path.join(version, "yarn-v2.jar"),
                  lambda: _get("%s/net/fabricmc/yarn/%s/yarn-%s-v2.jar"
                               % (FABRIC_MAVEN, build, build)))
    with zipfile.ZipFile(io.BytesIO(raw)) as zf:
        entry = next(n for n in zf.namelist() if n.endswith("mappings.tiny"))
        return zf.read(entry).decode("utf-8")


# --- parsers ----------------------------------------------------------------

_PRIMITIVES = {
    "void": "V", "boolean": "Z", "byte": "B", "char": "C", "short": "S",
    "int": "I", "long": "J", "float": "F", "double": "D",
}


def type_to_descriptor(java_type):
    """`net.minecraft.world.phys.Vec3[]` -> `[Lnet/minecraft/world/phys/Vec3;`"""
    arrays = 0
    while java_type.endswith("[]"):
        arrays += 1
        java_type = java_type[:-2]
    base = _PRIMITIVES.get(java_type)
    if base is None:
        base = "L" + java_type.replace(".", "/") + ";"
    return "[" * arrays + base


def parse_proguard(text):
    """official -> obfuscated, for classes, fields and methods.

    Returns (classes, members) where
        classes[official_internal_name] = obf_internal_name
        members[(official_owner, name, official_descriptor, kind)] = obf_name
    with kind in ("f", "m").

    The descriptor is not optional here.  ProGuard reuses one obfuscated name for
    several unrelated members of the same class, so an (owner, name) key alone
    resolves to the wrong member often enough to matter.
    """
    classes = {}
    members = {}
    owner = None
    for line in text.split("\n"):
        line = line.rstrip("\r")
        if not line or line.startswith("#"):
            continue
        if not line.startswith(" ") and line.rstrip().endswith(":"):
            left, right = line.rstrip()[:-1].split(" -> ")
            owner = left.strip().replace(".", "/")
            classes[owner] = right.strip().replace(".", "/")
        elif owner is not None and line.startswith(" ") and " -> " in line:
            left, obf = line.strip().split(" -> ")
            obf = obf.strip()
            # methods carry a source line range: "12:34:void tick()"
            if ":" in left.split("(")[0]:
                left = left.rsplit(":", 1)[1] if left.count(":") < 3 else left.split(":", 2)[2]
            left = left.strip()
            if "(" in left:
                head, args = left.split("(", 1)
                args = args.rsplit(")", 1)[0]
                ret_type, name = head.rsplit(" ", 1)
                arg_desc = "".join(type_to_descriptor(a) for a in args.split(",") if a)
                desc = "(%s)%s" % (arg_desc, type_to_descriptor(ret_type))
                members[(owner, name, desc, "m")] = obf
            else:
                ftype, name = left.rsplit(" ", 1)
                members[(owner, name, type_to_descriptor(ftype), "f")] = obf
    return classes, members


def parse_tiny_v2(text):
    """Returns (classes, members) for a two-namespace tiny v2 file.

        classes[ns0_class] = ns1_class
        members[(ns0_owner, ns0_name, ns0_desc, kind)] = ns1_name
    """
    classes = {}
    members = {}
    owner = None
    for line in text.split("\n"):
        if not line or line.startswith("tiny\t"):
            continue
        p = line.split("\t")
        if p[0] == "c" and len(p) >= 3:
            owner = p[1]
            classes[p[1]] = p[2]
        elif owner is not None and p and p[0] == "" and len(p) >= 5 and p[1] in ("m", "f"):
            members[(owner, p[3], p[2], p[1])] = p[4]
    return classes, members


# --- 26.x: read the real jar, ranged -----------------------------------------

class JarIndex:
    """Random access to a remote client jar through HTTP range requests.

    The central directory is cached on disk; class files are cached individually
    as they are asked for.  Nothing downloads the whole jar.
    """

    def __init__(self, version):
        self.version = version
        dl = version_json(version)["downloads"]["client"]
        self.url = dl["url"]
        self.size = dl["size"]
        self._entries = None

    def _range(self, start, end):
        req = urllib.request.Request(
            self.url, headers={"Range": "bytes=%d-%d" % (start, end),
                               "User-Agent": "enhance-mappings/1"})
        with urllib.request.urlopen(req, timeout=180) as r:
            return r.read()

    def entries(self):
        if self._entries is not None:
            return self._entries
        raw = _cached(os.path.join(self.version, "central-directory.bin"), self._fetch_cd)
        self._entries = self._parse_cd(raw)
        return self._entries

    def _fetch_cd(self):
        tail = self._range(max(0, self.size - 65600), self.size - 1)
        i = tail.rfind(b"PK\x05\x06")
        if i < 0:
            raise RuntimeError("no end-of-central-directory in %s" % self.url)
        cdsize, cdoff = struct.unpack_from("<II", tail, i + 12)
        if cdoff == 0xFFFFFFFF:
            raise RuntimeError("zip64 central directory not handled")
        return self._range(cdoff, cdoff + cdsize - 1)

    @staticmethod
    def _parse_cd(cd):
        out = {}
        p = 0
        while p + 4 <= len(cd) and cd[p:p + 4] == b"PK\x01\x02":
            method, = struct.unpack_from("<H", cd, p + 10)
            csize, _usize = struct.unpack_from("<II", cd, p + 20)
            nlen, elen, clen = struct.unpack_from("<HHH", cd, p + 28)
            lho, = struct.unpack_from("<I", cd, p + 42)
            name = cd[p + 46:p + 46 + nlen].decode("utf-8", "replace")
            out[name] = (lho, csize, method)
            p += 46 + nlen + elen + clen
        return out

    def has_class(self, internal_name):
        return internal_name + ".class" in self.entries()

    def read_class(self, internal_name):
        entry = internal_name + ".class"
        idx = self.entries()
        if entry not in idx:
            return None
        cached = os.path.join(self.version, "classes", entry)
        return _cached(cached, lambda: self._read_entry(idx[entry]))

    def _read_entry(self, meta):
        lho, csize, method = meta
        hdr = self._range(lho, lho + 29)
        nlen, elen = struct.unpack_from("<HH", hdr, 26)
        start = lho + 30 + nlen + elen
        raw = self._range(start, start + csize - 1)
        return zlib.decompress(raw, -15) if method == 8 else raw

    def members(self, internal_name):
        """Returns (fields, methods) as sets of (name, descriptor), or None."""
        data = self.read_class(internal_name)
        if data is None:
            return None
        return parse_class_members(data)


def parse_class_members(data):
    """Minimal class-file reader: constant pool, then the field and method tables."""
    cp = {}
    count, = struct.unpack_from(">H", data, 8)
    p = 10
    i = 1
    while i < count:
        tag = data[p]
        p += 1
        if tag == 1:                        # Utf8
            l, = struct.unpack_from(">H", data, p)
            cp[i] = data[p + 2:p + 2 + l].decode("utf-8", "replace")
            p += 2 + l
        elif tag in (7, 8, 16, 19, 20):     # Class, String, MethodType, Module, Package
            p += 2
        elif tag == 15:                     # MethodHandle
            p += 3
        elif tag in (5, 6):                 # Long, Double take two slots
            p += 8
            i += 1
        else:                               # 3,4,9,10,11,12,17,18
            p += 4
        i += 1

    p += 6                                   # access_flags, this_class, super_class
    ifc, = struct.unpack_from(">H", data, p)
    p += 2 + ifc * 2

    def table(pos):
        n, = struct.unpack_from(">H", data, pos)
        pos += 2
        out = set()
        for _ in range(n):
            _acc, ni, di = struct.unpack_from(">HHH", data, pos)
            pos += 6
            ac, = struct.unpack_from(">H", data, pos)
            pos += 2
            for _ in range(ac):
                al, = struct.unpack_from(">I", data, pos + 2)
                pos += 6 + al
            out.add((cp.get(ni), cp.get(di)))
        return out, pos

    fields, p = table(p)
    methods, _ = table(p)
    return fields, methods


# --- descriptor remapping ----------------------------------------------------

def remap_descriptor(desc, class_map):
    """Rewrite every Lnet/minecraft/Foo; in a descriptor through class_map."""
    out = []
    i = 0
    while i < len(desc):
        c = desc[i]
        if c == "L":
            end = desc.index(";", i)
            name = desc[i + 1:end]
            out.append("L" + class_map.get(name, name) + ";")
            i = end + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def _selftest():
    print("cache: %s" % CACHE)
    for v in ("1.20.1", "1.21.11", "26.3"):
        vj = version_json(v)
        cm = client_mappings(v)
        it = intermediary_tiny(v)
        print("%-8s java %-3s  client_mappings %-6s intermediary %s"
              % (v, vj["javaVersion"]["majorVersion"],
                 "%dKB" % (len(cm) // 1024) if cm else "none",
                 "%dKB" % (len(it) // 1024) if it else "none"))
    ji = JarIndex("26.3")
    print("26.3 jar entries: %d" % len(ji.entries()))
    f, m = ji.members("net/minecraft/client/Minecraft")
    print("26.3 Minecraft: %d fields, %d methods" % (len(f), len(m)))
    print("   player field:", [d for n, d in f if n == "player"])
    cls, mem = parse_proguard(client_mappings("1.21.11"))
    print("1.21.11 proguard: %d classes, %d members" % (len(cls), len(mem)))
    icls, imem = parse_tiny_v2(intermediary_tiny("1.21.11"))
    print("1.21.11 intermediary: %d classes, %d members" % (len(icls), len(imem)))

    # End-to-end join on three symbols whose intermediary names the client already
    # uses: a field, a method and the class itself.
    probes = [
        ("net/minecraft/client/Minecraft", "player",
         "Lnet/minecraft/client/player/LocalPlayer;", "f", "field_1724"),
        ("net/minecraft/client/player/LocalPlayer", "sendPosition", "()V", "m", "method_3136"),
        ("net/minecraft/client/renderer/GameRenderer", "pick", "(F)V", "m", "method_3190"),
    ]
    for owner, name, desc, kind, expect in probes:
        obf_owner = cls[owner]
        obf_desc = remap_descriptor(desc, cls)
        obf_name = mem.get((owner, name, desc, kind))
        got = imem.get((obf_owner, obf_name, obf_desc, kind))
        print("   %-26s %-14s -> %s.%s%s -> %-14s %s"
              % (owner.split("/")[-1], name, obf_owner, obf_name, obf_desc, got,
                 "OK" if got == expect else "MISMATCH (expected %s)" % expect))


if __name__ == "__main__":
    sys.exit(_selftest())
