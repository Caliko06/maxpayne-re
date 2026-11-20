"""Read-only access to the OWNER's game data by in-archive path, for the launcher (web/launcher*.js) through tools/serve.py.

GET /gamefile/<path>        the file as the game would find it: a loose file under <game dir>/data first, else the first *.ras (sorted, as the
                            game's own *.RAS glob: rea/map/ras_overlay.md 1 and 3) holding it. Path = the archive's own, lower- or mixed-case,
                            e.g. /gamefile/data/database/music/wavs/max_theme.wav. Nothing is copied into git; decoded bytes of
                            LZSS entries are cached under build/launcher/.
GET /gamefile/<path>.png    only for a .tga or .pcx entry: the picture as PNG (browsers decode neither; JPG and WAV go out as they are).
GET /gamefile-index.json    [[path, size], ...] of everything reachable, for discovery.
WAV: every PCM wav of this install is 8-bit PCM (checked 2026-11-07: 1360 files, music 11025..44100 Hz), which Chrome and Safari decode
natively, so no conversion; Range is served so an <audio> element can stream the 19 MB themes (they are stored, not compressed).
"""
import glob, hashlib, json, os, struct, threading, zlib

import ras

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(ROOT, "build", "launcher")
TYPES = {".wav": "audio/wav", ".jpg": "image/jpeg", ".jpeg": "image/jpeg", ".png": "image/png", ".txt": "text/plain; charset=latin-1",
         ".h": "text/plain; charset=latin-1", ".tga": "application/octet-stream", ".pcx": "application/octet-stream"}


class Source:
    """One reachable file: loose (path) or inside an archive (archive path + ras entry)."""
    def __init__(self, key, size, path, entry=None):
        self.key, self.size, self.path, self.entry = key, size, path, entry
        st = os.stat(path)
        self.etag = '"%s-%x-%x"' % (hashlib.sha1(key.encode()).hexdigest()[:8], st.st_mtime_ns, st.st_size) if not entry else \
                    '"%s-%x-%x-%x"' % (hashlib.sha1(key.encode()).hexdigest()[:8], st.st_mtime_ns, st.st_size, entry["offset"])

    @property
    def stored(self):                       # bytes are in the file as they are: a Range maps straight onto it
        return self.entry is None or self.entry["method"] == 3

    def start(self):
        return 0 if self.entry is None else self.entry["offset"]


class GameFiles:
    def __init__(self, game):
        self.game, self.lock, self.idx = game, threading.Lock(), None

    # ---- index -------------------------------------------------------------------------------------------------------------
    def _build(self):
        idx = {}
        for ar in sorted(glob.glob(os.path.join(self.game, "*.[rR][aA][sS]")), key=lambda p: os.path.basename(p).lower()):
            st = os.stat(ar)
            cache = os.path.join(CACHE, "index-%s-%x-%x.json" % (os.path.basename(ar).lower(), st.st_mtime_ns, st.st_size))
            if os.path.isfile(cache):
                rows = json.load(open(cache))
            else:
                f, seed, ver, files, dt = ras.read(ar)
                f.close()
                dirs, p = [], 0
                while p < len(dt):                         # dir table: path\0 + 16 bytes (rea/map/ras_overlay.md 7)
                    e = dt.index(b"\0", p)
                    dirs.append(dt[p:e].decode("latin-1"))
                    p = e + 17
                rows = [[dirs[x["dir"]] + x["name"], x] for x in files if x["dir"] < len(dirs)]
                os.makedirs(CACHE, exist_ok=True)
                json.dump(rows, open(cache + ".tmp", "w"))
                os.replace(cache + ".tmp", cache)
            for name, x in rows:
                key = name.replace("\\", "/").strip("/").lower()
                idx.setdefault(key, Source(key, x["size"], ar, x))          # the first archive wins a path
        top = os.path.join(self.game, "data")
        for d, _, names in os.walk(top):                                   # a loose file beats every archive (R_File::load fopen first)
            for n in names:
                p = os.path.join(d, n)
                key = os.path.relpath(p, self.game).replace(os.sep, "/").lower()
                idx[key] = Source(key, os.path.getsize(p), p)
        return idx

    def index(self):
        with self.lock:
            if self.idx is None:
                self.idx = self._build()
            return self.idx

    def listing(self):
        return json.dumps(sorted([k, s.size] for k, s in self.index().items())).encode()

    # ---- lookup ------------------------------------------------------------------------------------------------------------
    def find(self, rel):
        """(Source, as_png) for a request path (without /gamefile/), or (None, False)."""
        key = rel.replace("\\", "/").strip("/").lower()
        if ".." in key.split("/"):
            return None, False
        idx = self.index()
        if key in idx:
            return idx[key], False
        if key.endswith(".png") and key[:-4] in idx and key[-8:-4] in (".tga", ".pcx"):
            return idx[key[:-4]], True
        return None, False

    def raw(self, src):
        """The decoded bytes of an entry (cached on disk for LZSS ones)."""
        if src.entry is None:
            return open(src.path, "rb").read()
        if src.stored:
            with open(src.path, "rb") as f:
                f.seek(src.start())
                return f.read(src.size)
        cache = os.path.join(CACHE, "raw", src.etag.strip('"'))
        if os.path.isfile(cache):
            return open(cache, "rb").read()
        with open(src.path, "rb") as f:
            data = ras.extract(f, src.entry)
        os.makedirs(os.path.dirname(cache), exist_ok=True)
        open(cache + ".tmp", "wb").write(data)
        os.replace(cache + ".tmp", cache)
        return data

    def png(self, src):
        """PNG bytes of a .tga/.pcx entry, cached next to the raw bytes."""
        cache = os.path.join(CACHE, "png", src.etag.strip('"') + ".png")
        if os.path.isfile(cache):
            return open(cache, "rb").read()
        data = self.raw(src)
        out = tga_png(data) if src.key.endswith(".tga") else pcx_png(data)
        os.makedirs(os.path.dirname(cache), exist_ok=True)
        open(cache + ".tmp", "wb").write(out)
        os.replace(cache + ".tmp", cache)
        return out


# ---- TGA / PCX -> PNG, no dependencies --------------------------------------------------------------------------------------------
def _png(w, h, channels, rows):
    """rows: bytes of h*w*channels, top row first. channels 1 (gray), 3 (RGB), 4 (RGBA)."""
    ct = {1: 0, 3: 2, 4: 6}[channels]
    stride = w * channels
    raw = b"".join(b"\0" + rows[y * stride:(y + 1) * stride] for y in range(h))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, ct, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def tga_png(b):
    idlen, cmap, kind = b[0], b[1], b[2]
    w, h, bpp, desc = struct.unpack_from("<HHBB", b, 12)
    if cmap or kind not in (2, 3, 10, 11) or bpp not in (8, 24, 32):
        raise ValueError("unsupported TGA type %d bpp %d" % (kind, bpp))
    n, p = bpp // 8, 18 + idlen
    if kind >= 10:                                    # RLE
        out = bytearray()
        while len(out) < w * h * n:
            c = b[p]; p += 1
            if c & 0x80:
                out += b[p:p + n] * ((c & 0x7f) + 1); p += n
            else:
                out += b[p:p + (c + 1) * n]; p += (c + 1) * n
        px = bytes(out[:w * h * n])
    else:
        px = b[p:p + w * h * n]
    if n >= 3:                                        # BGR(A) -> RGB(A)
        a = bytearray(px)
        a[0::n], a[2::n] = px[2::n], px[0::n]
        px = bytes(a)
    if not desc & 0x20:                               # bottom-up -> top-down
        px = b"".join(px[y * w * n:(y + 1) * w * n] for y in range(h - 1, -1, -1))
    return _png(w, h, n, px)


def pcx_png(b):
    _, ver, enc, bpp, x0, y0, x1, y1 = struct.unpack_from("<BBBBHHHH", b, 0)
    planes, bpl = b[65], struct.unpack_from("<H", b, 66)[0]
    w, h = x1 - x0 + 1, y1 - y0 + 1
    if enc != 1 or bpp != 8 or planes not in (1, 3):
        raise ValueError("unsupported PCX")
    pal = b[-768:] if planes == 1 and b[-769] == 12 else None
    p, scan = 128, bytearray()
    need = bpl * planes * h
    while len(scan) < need:                           # RLE: 0xC0|n, value
        c = b[p]; p += 1
        if c >= 0xc0:
            scan += b[p:p + 1] * (c & 0x3f); p += 1
        else:
            scan.append(c)
    rows = bytearray()
    for y in range(h):
        line = scan[y * bpl * planes:(y + 1) * bpl * planes]
        if planes == 3:
            px = bytearray(w * 3)
            for k in range(3):
                px[k::3] = line[k * bpl:k * bpl + w]
            rows += px
        elif pal:
            px = bytearray(w * 3)
            for k in range(3):
                px[k::3] = bytes(pal[i * 3 + k] for i in line[:w])
            rows += px
        else:
            rows += line[:w]
    return _png(w, h, 3 if planes == 3 or pal else 1, bytes(rows))
