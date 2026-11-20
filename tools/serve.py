#!/usr/bin/env python3
"""Web server for the browser build (from RE-NFSU tools/serve.py), COOP/COEP for SharedArrayBuffer.

/ -> web/. /game/... -> the game folder given on the command line (the OWNER's files; they leave this machine only in the
public package, tools/build_public.py --cdn, and only with his go for that upload);
it answers Range requests, which host/files.c reads from the game thread. /game-files.json lists it.
/gamefile/<in-archive path> = one file of the game's data as the game finds it (loose data/ first, else the first .ras), read-only, with
Content-Type, ETag and Range; <path>.png for a .tga/.pcx entry; /gamefile-index.json lists the paths (tools/launcher_assets.py).
/img/... -> build/img (the rebased guest images host/main.c maps), listed by /img-files.json.
/save/... = save/ next to the game folder (save-<port>/ on a test port) (what the game writes): GET reads, PUT writes a whole file, DELETE
removes one; /save-files.json lists it. POST /log appends the page log to build/web.log, POST /telemetry the play
telemetry (web/telemetry.js) to build/telemetry.ndjson (headless Chrome: build/webcheck.*).
Usage: python3 tools/serve.py <game dir> [port]     (default port 5181)
"""
import gzip, hashlib, http.server, json, os, re, shutil, sys, threading, urllib.parse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLAN_LOCK = threading.Lock()
GAME = os.path.abspath(sys.argv[1])
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 5181
# The OWNER's saves (port 5181) live in save/; every test port gets its own save-<port>/, so a slice run never rewrites them.
SAVE = os.path.join(os.path.dirname(GAME), "save" if PORT == 5181 else f"save-{PORT}")
os.makedirs(SAVE, exist_ok=True)
# Extra game files the OWNER's install does not hold (read-only): a_cheats.ras = x_data's menu.txt with a Cheats page (tools/ras.py).
# It sorts before x_data.ras, so the game's *.RAS glob (host/files.c) searches it first. Served as /game/a_cheats.ras, listed in
# /game-files.json and, for the page's OPFS copy (web/opfs.js), in /extra-files.json.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
EXTRA = {}
import launcher_assets
GAMEFILES = launcher_assets.GameFiles(GAME)
if os.path.isfile(os.path.join(GAME, "x_data.ras")):
    import ras
    EXTRA["a_cheats.ras"] = os.path.join(ROOT, "build", "extra", "a_cheats.ras")
    os.makedirs(os.path.dirname(EXTRA["a_cheats.ras"]), exist_ok=True)
    with open(EXTRA["a_cheats.ras"], "wb") as f:
        f.write(ras.cheats_archive(GAME))


# Static files (web/, /img/) go out compressed and revalidated (NFSU2-web ships its wasm brotli-compressed, Chrome keeps it):
# brotli from <file>.br when build.py made it and it is newer than the file (the 146 MB wasm is 15 MB that way), else
# gzip, made once per file version into build/httpcache/. ETag + "no-cache" = a reload costs a 304, and Chrome keeps the
# compiled wasm with the cache entry. Everything dynamic (listings, /save, /game ranges, /log) stays "no-store".
SQUEEZE = {".wasm", ".js", ".json", ".html", ".css", ".dll", ".exe", ".tsv"}
GZ_DIR = os.path.join(ROOT, "build", "httpcache")
gz_lock = threading.Lock()


def gz_copy(path, st):
    """build/httpcache/<hash>.gz for this version of the file (made on first use)."""
    key = hashlib.sha1(f"{path}\0{st.st_mtime_ns}\0{st.st_size}".encode()).hexdigest()[:20]
    out = os.path.join(GZ_DIR, key + ".gz")
    with gz_lock:
        if not os.path.exists(out):
            os.makedirs(GZ_DIR, exist_ok=True)
            with open(path, "rb") as f, gzip.open(out + ".tmp", "wb", compresslevel=6) as g:
                shutil.copyfileobj(f, g, 1 << 20)
            os.replace(out + ".tmp", out)
    return out


def save_path(url):
    """save/<rel> for /save/<rel>, or None when it would leave save/."""
    rel = os.path.normpath(urllib.parse.unquote(url.split("?", 1)[0][len("/save/"):]))
    if rel.startswith("..") or os.path.isabs(rel) or rel == ".":
        return None
    return os.path.join(SAVE, rel)


def listing(top, dirs=False):
    files = []
    for d, subs, names in os.walk(top, followlinks=True):
        if dirs:                            # the save folder's index keeps empty directories too (size -1, host/files.c)
            files += [[os.path.relpath(os.path.join(d, n), top), -1] for n in subs]
        for n in names:
            p = os.path.join(d, n)
            files.append([os.path.relpath(p, top), os.path.getsize(p)])
    if top == GAME:
        files += [[n, os.path.getsize(p)] for n, p in EXTRA.items()]
    return json.dumps(sorted(files)).encode()


class Handler(http.server.SimpleHTTPRequestHandler):
    # HTTP/1.1: Chrome revalidates (If-None-Match -> 304) only against a 1.1 response, and the game thread's many Range reads
    # reuse one connection. Every answer below has a Content-Length or no body (204/304).
    protocol_version = "HTTP/1.1"
    disable_nagle_algorithm = True          # headers and body are two writes: with keep-alive, Nagle + delayed ACK cost 40 ms each

    def do_PUT(self):
        self._cache = None
        p = save_path(self.path) if self.path.startswith("/save/") else None
        if not p:
            self.close_connection = True            # the unread request body would be taken for the next request
            self.send_error(403)
            return
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        if self.path.endswith("/"):         # PUT /save/dir/ makes a directory (CreateDirectoryA)
            os.makedirs(p, exist_ok=True)
        else:
            os.makedirs(os.path.dirname(p), exist_ok=True)
            tmp = f"{p}.{threading.get_ident()}.tmp"
            with open(tmp, "wb") as f:
                f.write(body)
            os.replace(tmp, p)
        self.send_response(204)
        self.end_headers()

    def plan_verdict(self):
        """The OWNER's verdict on web/plan/ (copied from RE-NFSU tools/serve.py): details[i].owner in web/plan/plan.json."""
        body = self.rfile.read(min(int(self.headers.get("Content-Length", 0)), 4096))
        try:
            v = json.loads(body)
            detail, ok, note = v["detail"], v["ok"], v.get("note", "")
            assert isinstance(detail, str) and isinstance(ok, bool) and isinstance(note, str)
        except (ValueError, KeyError, AssertionError, AttributeError, TypeError):
            self.close_connection = True
            self.send_error(400, "want {detail: str, ok: bool, note: str}")
            return
        path = os.path.join(ROOT, "web", "plan", "plan.json")
        with PLAN_LOCK:
            plan = json.load(open(path, encoding="utf-8"))
            hit = [d for d in plan.get("details", []) if d.get("name") == detail]
            if not hit:
                self.close_connection = True
                self.send_error(400, "unknown detail")
                return
            hit[0]["owner"] = {"ok": ok, "note": note[:500], "time": __import__("time").strftime("%Y-%m-%d %H:%M")}
            json.dump(plan, open(path + ".tmp", "w", encoding="utf-8"), ensure_ascii=False, indent=1)
            os.replace(path + ".tmp", path)
        self.send_response(204)
        self.end_headers()

    def do_POST(self):
        self._cache = None
        route = self.path.split("?", 1)[0]
        if route == "/plan/verdict":
            self.plan_verdict()
            return
        if route not in ("/log", "/telemetry"):
            self.close_connection = True
            self.send_error(403)
            return
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        # /log -> build/web.log, /telemetry (web/telemetry.js) -> build/telemetry.ndjson; ?new keeps the previous session
        # as *.prev.*. Headless Chrome (tools/webgate.mjs) writes webcheck.log / webcheck.telemetry.ndjson instead, so a
        # gate never replaces the OWNER's session (the two files share one clock).
        head = "HeadlessChrome" in self.headers.get("User-Agent", "")
        wc = "webcheck" if PORT < 5200 else f"webcheck-{PORT}"   # fleet players (tools/fleet.sh, ports 5200+) run side by side
        name, ext = {"/log": (wc if head else "web", ".log"),
                     "/telemetry": (wc + ".telemetry" if head else "telemetry", ".ndjson")}[route]
        log = os.path.join(ROOT, "build", name + ext)
        if self.path.endswith("?new") and os.path.exists(log):
            os.replace(log, os.path.join(ROOT, "build", name + ".prev" + ext))
        os.makedirs(os.path.dirname(log), exist_ok=True)
        with open(log, "wb" if self.path.endswith("?new") else "ab") as f:
            f.write(body)
        self.send_response(204)
        self.end_headers()

    def do_DELETE(self):
        self._cache = None
        p = save_path(self.path) if self.path.startswith("/save/") else None
        if not p or not os.path.isfile(p):
            self.send_error(404)
            return
        os.remove(p)
        self.send_response(204)
        self.end_headers()

    def do_HEAD(self):
        self._cache = None
        super().do_HEAD()

    def do_GET(self):
        self._cache = None
        route = self.path.split("?", 1)[0]
        if route == "/extra-files.json":
            body = json.dumps([[n, os.path.getsize(p)] for n, p in EXTRA.items()]).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if route in ("/game-files.json", "/save-files.json", "/img-files.json"):
            top = {"/game-files.json": GAME, "/save-files.json": SAVE, "/img-files.json": os.path.join(ROOT, "build", "img")}[route]
            body = listing(top, route == "/save-files.json") if os.path.isdir(top) else b"[]"
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if route == "/gamefile-index.json" or route.startswith("/gamefile/"):
            self.send_gamefile(route)
            return
        if route.startswith("/gamefont/"):
            self.send_gamefont(route)
            return
        rng = self.headers.get("Range")
        path = self.translate_path(self.path)
        if rng and rng.startswith("bytes=") and "," not in rng and os.path.isfile(path):    # the game thread reads byte ranges
            size = os.path.getsize(path)
            try:
                x, y = rng[6:].split("-")
                if not x and not y:
                    raise ValueError
                if not x:
                    n = int(y)
                    if n <= 0:
                        raise ValueError
                    a, b = max(0, size - n), size - 1
                else:
                    a = int(x)
                    b = min(int(y), size - 1) if y else size - 1
            except ValueError:
                a, b = size, -1
            if a < 0 or a >= size or b < a:
                self.send_response(416)
                self.send_header("Content-Range", f"bytes */{size}")
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            with open(path, "rb") as f:
                f.seek(a)
                body = f.read(b - a + 1)
            self.send_response(206)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Range", f"bytes {a}-{b}/{size}")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if os.path.isdir(path) and route.endswith("/") and os.path.isfile(os.path.join(path, "index.html")):
            path = os.path.join(path, "index.html")
        if route.startswith(("/game/", "/save/")) or not os.path.isfile(path):
            super().do_GET()
            return
        self.send_static(path)

    def send_gamefont(self, route):
        """The menu's bitmap fonts as atlas PNG + metrics JSON (tools/font_atlas.py), cut from the game data on first request into
        build/launcher/font (gitignored): the launcher draws its menu with the game's own letters."""
        name = os.path.basename(route)
        if not re.fullmatch(r"menu_font_univers[a-z_]*\.(png|json)", name):
            self.send_error(404)
            return
        path = os.path.join(ROOT, "build", "launcher", "font", name)
        if not os.path.isfile(path):
            try:
                import font_atlas
                font_atlas.main(GAME)
            except Exception as e:
                self.send_error(500, str(e)[:100])
                return
        if os.path.isfile(path):
            self.send_static(path)
        else:
            self.send_error(404)

    def send_gamefile(self, route):
        """The launcher's view of the game data (tools/launcher_assets.py): ETag + Range, PNG for .tga/.pcx, never a write."""
        try:
            if route == "/gamefile-index.json":
                data, ctype, etag, src = GAMEFILES.listing(), "application/json", None, None
            else:
                src, png = GAMEFILES.find(urllib.parse.unquote(route[len("/gamefile/"):]))
                if not src:
                    self.send_error(404)
                    return
                ctype = "image/png" if png else launcher_assets.TYPES.get(os.path.splitext(src.key)[1], "application/octet-stream")
                etag = src.etag[:-1] + ('-png"' if png else '"')
                data = None
        except Exception as e:                       # a damaged entry: say so, do not hang the connection
            self.send_error(500, str(e)[:100])
            return
        if etag and self.headers.get("If-None-Match") == etag:
            self.send_response(304)
            self.send_header("ETag", etag)
            self._cache = "no-cache"
            self.end_headers()
            return
        lazy = data is None and src.stored and ctype != "image/png"       # themes are 19 MB: a Range reads only its slice
        if data is None and not lazy:
            data = GAMEFILES.png(src) if ctype == "image/png" else GAMEFILES.raw(src)
        size = src.size if lazy else len(data)
        status, a, b = 200, 0, size - 1
        rng = self.headers.get("Range")
        if rng and rng.startswith("bytes=") and "," not in rng:
            try:
                x, y = rng[6:].split("-")
                if not x and not y:
                    raise ValueError
                a = max(0, size - int(y)) if not x else int(x)
                b = min(int(y), size - 1) if x and y else size - 1 if x or not y else b
            except ValueError:
                a, b = size, -1
            if a < 0 or a >= size or b < a:
                self.send_response(416)
                self.send_header("Content-Range", f"bytes */{size}")
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            status = 206
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(b - a + 1))
        self.send_header("Accept-Ranges", "bytes")
        if status == 206:
            self.send_header("Content-Range", f"bytes {a}-{b}/{size}")
        if etag:
            self.send_header("ETag", etag)
            self._cache = "no-cache"
        self.end_headers()
        if self.command == "HEAD":
            return
        if lazy:
            with open(src.path, "rb") as f:
                f.seek(src.start() + a)
                left = b - a + 1
                while left > 0:
                    chunk = f.read(min(left, 1 << 20))
                    if not chunk:
                        break
                    self.wfile.write(chunk)
                    left -= len(chunk)
        else:
            self.wfile.write(data[a:b + 1] if (a, b) != (0, size - 1) else data)

    def send_static(self, path):
        st = os.stat(path)
        ae = {t.split(";")[0].strip() for t in self.headers.get("Accept-Encoding", "").split(",")}
        body, enc = path, None
        if os.path.splitext(path)[1].lower() in SQUEEZE and st.st_size > 1024:
            br = path + ".br"
            if "br" in ae and os.path.isfile(br) and os.stat(br).st_mtime >= st.st_mtime:
                body, enc = br, "br"
            elif "gzip" in ae:
                body, enc = gz_copy(path, st), "gzip"
        etag = '"%x-%x-%s"' % (st.st_mtime_ns, st.st_size, enc or "id")
        self._cache = "no-cache"
        if self.headers.get("If-None-Match") == etag:
            self.send_response(304)
            self.send_header("ETag", etag)
            self.end_headers()
            return
        self.send_response(200)
        self.send_header("Content-Type", self.guess_type(path))
        self.send_header("Content-Length", str(os.path.getsize(body)))
        self.send_header("X-Raw-Length", str(st.st_size))        # the page's progress bar counts decoded bytes
        self.send_header("ETag", etag)
        self.send_header("Vary", "Accept-Encoding")
        if enc:
            self.send_header("Content-Encoding", enc)
        self.end_headers()
        with open(body, "rb") as f:
            shutil.copyfileobj(f, self.wfile, 1 << 20)

    def translate_path(self, path):
        path = urllib.parse.unquote(path.split("?", 1)[0].split("#", 1)[0])
        if path.startswith("/game/"):
            if path[6:] in EXTRA:
                return EXTRA[path[6:]]
            return os.path.join(GAME, os.path.normpath(path[6:]).lstrip("/."))
        if path.startswith("/save/"):
            return save_path(path) or os.path.join(SAVE, "..refused")
        if path.startswith("/img/"):
            return os.path.join(ROOT, "build", "img", os.path.normpath(path[5:]).lstrip("/."))
        return os.path.join(ROOT, "web", os.path.normpath(path).lstrip("/."))

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        self.send_header("Cache-Control", getattr(self, "_cache", None) or "no-store")
        super().end_headers()

    def log_message(self, fmt, *a):          # RN_HTTPLOG=1: one line per request (status, If-None-Match, Range) on stderr
        if os.environ.get("RN_HTTPLOG"):
            print(fmt % a, "INM=" + str(self.headers.get("If-None-Match")), "AE=" + str(self.headers.get("Accept-Encoding")),
                  "Range=" + str(self.headers.get("Range")), file=sys.stderr, flush=True)


Handler.extensions_map = {**Handler.extensions_map, ".wasm": "application/wasm", ".js": "text/javascript", ".json": "application/json", ".svg": "image/svg+xml"}
http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
