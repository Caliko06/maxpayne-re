#!/usr/bin/env python3
"""Local dist server, adapted from RE-NFSU tools/static_server.py (gzip pieces, RTT, Range).
RN_RTT_MS=80 python3 tools/static_server.py 5254 build/public
No game parser; the game files are the dist's gzip pieces (data-chunks.json), decoded per request and cut by Range the way
host/files.c reads them. Saves live in the browser's OPFS. Loopback only.
"""
import functools
import gzip
import http.server
import json
import os
from pathlib import Path
import re
import sys
import time

RTT = float(os.environ.get('RN_RTT_MS', 0)) / 1000
BLOCK = 256 << 10            # tools/build_public.py data-chunks block: one piece per 256 KB of game bytes
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 5254
ROOT = Path(sys.argv[2] if len(sys.argv) > 2 else Path(__file__).resolve().parents[1] / 'build/public').resolve()
BUILD = Path(__file__).resolve().parents[1] / 'build'


def index(files_dir, name):
    path = files_dir / name
    return {name: json.loads(path.read_text())} if path.is_file() else {}


CHUNKS = index(ROOT, 'data-chunks.json').get('data-chunks.json') or {'block': BLOCK, 'files': {}}


@functools.lru_cache(maxsize=256)
def piece(files_dir, name):
    """One decoded piece. 256 pieces = 64 MB, the window tools/serve.py's Range reads need in practice."""
    return gzip.decompress((Path(files_dir) / 'data-chunks' / (name + '.gz')).read_bytes())


class Handler(http.server.SimpleHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    disable_nagle_algorithm = True
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map,
                      '.wasm': 'application/wasm', '.js': 'text/javascript', '.webmanifest': 'application/manifest+json'}

    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Cross-Origin-Resource-Policy', 'same-origin')
        super().end_headers()

    def do_GET(self):
        self.serve_file()

    def do_HEAD(self):
        self.serve_file(head=True)

    # POST /log and /telemetry: the same files tools/serve.py writes, so a dist run leaves the same telemetry a dev run does.
    def do_POST(self):
        if RTT:
            time.sleep(RTT)
        route = self.path.split('?', 1)[0]
        body = self.rfile.read(int(self.headers.get('Content-Length') or 0))
        name = {"/log": f'webcheck-{PORT}.log', "/telemetry": f'webcheck-{PORT}.telemetry.ndjson'}.get(route)
        if not name:
            self.send_error(404)
            return
        BUILD.mkdir(exist_ok=True)
        with (BUILD / name).open('wb' if 'new' in self.path else 'ab') as f:
            f.write(body)
        self.send_response(204)
        self.send_header('Content-Length', '0')
        self.end_headers()

    # /game/<path>: one game file assembled from its gzip pieces. The C side asks for decoded offsets (bytes=a-b).
    def serve_game(self, route, head):
        ent = CHUNKS['files'].get(route[1:])
        if not ent:
            self.send_error(404)
            return
        size = ent['size']
        a, b = 0, size - 1
        rng = re.fullmatch(r'bytes=(\d*)-(\d*)', self.headers.get('Range') or '')
        if rng and any(rng.groups()):
            x, y = rng.groups()
            a = int(x) if x else max(0, size - int(y))
            b = min(int(y), size - 1) if x and y else size - 1
            if a >= size or b < a:
                self.send_response(416)
                self.send_header('Content-Range', f'bytes */{size}')
                self.send_header('Content-Length', '0')
                self.end_headers()
                return
        body, at = bytearray(), a
        while at <= b:
            k, start = at // BLOCK, (at // BLOCK) * BLOCK
            data = piece(str(ROOT), ent['chunks'][k])
            take = min(b, start + len(data) - 1) - at + 1
            body += data[at - start:at - start + take]
            at += take
        self.send_response(206 if rng and any(rng.groups()) else 200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Accept-Ranges', 'bytes')
        self.send_header('Cache-Control', 'public, max-age=31536000, immutable')
        if rng and any(rng.groups()):
            self.send_header('Content-Range', f'bytes {a}-{b}/{size}')
        self.end_headers()
        if not head:
            self.wfile.write(body)

    def serve_file(self, head=False):
        if RTT:
            time.sleep(RTT)
        route = self.path.split('?', 1)[0]
        if route.startswith('/game/') and CHUNKS['files']:   # a --cdn package has the raw tree: served below like R2 does
            self.serve_game(route, head)
            return
        path = Path(self.translate_path(self.path))
        if path.is_dir():
            path /= 'index.html'
        if not path.is_file():
            self.send_error(404)
            return
        size = path.stat().st_size
        body, enc = path, None
        # Piece offsets refer to decoded game bytes; each piece is an entire gzip HTTP representation.
        if path.suffix == '.gz' and path.parent.name == 'data-chunks':
            enc = 'gzip'
        elif not self.headers.get('Range'):
            accepted = self.headers.get('Accept-Encoding', '')
            for suffix, name in (('.br', 'br'), ('.gz', 'gzip')):
                if name in accepted and Path(str(path) + suffix).is_file():
                    body, enc = Path(str(path) + suffix), name
                    break
        a, b, status = 0, body.stat().st_size - 1, 200
        rng = self.headers.get('Range')
        if rng and enc is None:
            match = re.fullmatch(r'bytes=(\d*)-(\d*)', rng)
            if match and any(match.groups()):
                x, y = match.groups()
                a = int(x) if x else max(0, size - int(y))
                b = min(int(y), size - 1) if x and y else size - 1
            if not match or not any(match.groups()) or a >= size or b < a:
                self.send_response(416)
                self.send_header('Content-Range', f'bytes */{size}')
                self.send_header('Content-Length', '0')
                self.end_headers()
                return
            status = 206
        self.send_response(status)
        self.send_header('Content-Type', 'application/octet-stream' if enc == 'gzip' and path.suffix == '.gz' else self.guess_type(str(path)))
        self.send_header('Content-Length', str(b - a + 1))
        self.send_header('X-Raw-Length', str(size))
        self.send_header('Vary', 'Accept-Encoding')
        self.send_header('Cache-Control', 'public, max-age=31536000, immutable' if path.parent.name == 'data-chunks' else
                         'public, max-age=86400' if route.startswith('/game/') else 'no-cache')   # R2's game pass header
        self.send_header('Accept-Ranges', 'bytes')
        if enc:
            self.send_header('Content-Encoding', enc)
        if status == 206:
            self.send_header('Content-Range', f'bytes {a}-{b}/{size}')
        self.end_headers()
        if not head:
            with body.open('rb') as f:
                f.seek(a)
                left = b - a + 1
                while left:
                    data = f.read(min(1 << 20, left))
                    if not data:
                        break
                    self.wfile.write(data)
                    left -= len(data)

    def log_message(self, *args):
        if os.environ.get('RN_HTTPLOG'):
            super().log_message(*args)


if __name__ == '__main__':
    if not (ROOT / 'manifest.json').is_file():
        sys.exit(f'No package manifest in {ROOT}; run tools/build_public.py first')
    print(f'http://127.0.0.1:{PORT}/ ({ROOT}, RTT {RTT * 1000:g} ms)', flush=True)
    http.server.ThreadingHTTPServer(('127.0.0.1', PORT), functools.partial(Handler, directory=str(ROOT))).serve_forever()
