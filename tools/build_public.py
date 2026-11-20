#!/usr/bin/env python3
"""Static package. Reuses RE-NFSU build_public.py manifest and data_chunks.py gzip representations.
Usage: python3 tools/build_public.py [--game ~/Games/maxpayne] [--dist build/public] [--cdn]
Two shapes: the default pieces (data-chunks/) feed tools/static_server.py, which reassembles /game/<path> itself.
--cdn writes the game tree raw at game/ instead: an object store has no reassembler, and host/files.c Range-reads
/game/<path> (web/index.html:168, web/opfs.js files()), which R2 answers with 206. tools/upload_r2.py needs --cdn.
Game bytes remain gitignored; either shape needs COOP/COEP (docs/hosting.md).
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import gzip
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BLOCK = 256 << 10  # host/files.c BLOCK: normal reads fetch exactly one piece, large reads assemble adjacent pieces.


def sha(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def write_json(path, obj):
    path.write_text(json.dumps(obj, separators=(',', ':')))


def build(game, dist, cdn=False):
    # The package is generated: a rerun replaces it (the previous one is only pieces of the same game data).
    if dist.exists():
        shutil.rmtree(dist)
    dist.mkdir(parents=True)
    web = ROOT / 'web'
    for path in sorted(web.iterdir()):
        if path.is_file() and not path.name.startswith('.') and path.name != 'manifest.json':
            shutil.copy2(path, dist / path.name)
    shutil.copytree(web / 'fonts', dist / 'fonts')
    (dist / 'launcher').mkdir()
    shutil.copy2(web / 'launcher/chapters.json', dist / 'launcher/chapters.json')
    logo = web / 'launcher/art2/private/logo.webp'
    if logo.exists():
        target = dist / 'launcher/art2/private/logo.webp'
        target.parent.mkdir(parents=True)
        shutil.copy2(logo, target)
    # Build flag is local packaging configuration, not a separate launcher or a game patch.
    index = dist / 'index.html'
    index.write_text(index.read_text().replace('<head>', '<head>\n<script>window.RN_PUBLIC=1</script>', 1)
                     .replace('href="manifest.json"', 'href="app.webmanifest"'))
    shutil.copy2(web / 'manifest.json', dist / 'app.webmanifest')
    (dist / 'out').mkdir()
    for name in ('maxpayne.js', 'maxpayne.wasm'):
        shutil.copy2(web / 'out' / name, dist / 'out' / name)
    wasm = dist / 'out/maxpayne.wasm'
    br = web / 'out/maxpayne.wasm.br'
    if br.exists() and br.stat().st_mtime >= (web / 'out/maxpayne.wasm').stat().st_mtime:
        shutil.copy2(br, Path(str(wasm) + '.br'))
    else:
        subprocess.run(['brotli', '-q', '11', '-w', '24', '-f', '-k', str(wasm)], check=True)
    # The page asks for the glue and the wasm with the same ?v=<hash>: a browser that kept an older maxpayne.js (Cloudflare gave
    # max-age=14400 once) never pairs it with a newer wasm ("import function a:I must be callable", OWNER 2026-11-20).
    v = hashlib.md5(Path(str(wasm) + '.br').read_bytes()).hexdigest()[:10]
    page = index.read_text()
    for ref in ('src="out/maxpayne.js"', "fetch('out/maxpayne.wasm'"):
        assert ref in page, ref
    index.write_text(page.replace('src="out/maxpayne.js"', f'src="out/maxpayne.js?v={v}"')
                     .replace("fetch('out/maxpayne.wasm'", f"fetch('out/maxpayne.wasm?v={v}'"))
    (dist / 'img').mkdir()
    images = []
    for path in sorted((ROOT / 'build/img').iterdir()):
        if path.is_file() and path.suffix.lower() in ('.exe', '.dll'):
            shutil.copy2(path, dist / 'img' / path.name)
            images.append([path.name, path.stat().st_size])
    write_json(dist / 'img-files.json', images)
    # Preserve serve.py's cheats overlay and sorted archive order.
    import ras
    extra = ROOT / 'build/extra/a_cheats.ras'
    extra.parent.mkdir(parents=True, exist_ok=True)
    extra.write_bytes(ras.cheats_archive(str(game)))
    files = [(p.relative_to(game).as_posix(), p) for p in sorted(game.rglob('*'))
             if p.is_file() and not p.name.startswith('.')]
    files.append(('a_cheats.ras', extra))
    files.sort()
    if cdn:
        # An object store serves bytes, not an assembly of pieces: keep the tree the page asks for, /game/<rel>.
        for rel, path in files:
            target = dist / 'game' / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            try:
                target.hardlink_to(path)      # same volume: no second copy of 2.1 GB while the upload runs
            except OSError:
                shutil.copy2(path, target)
    else:
        pieces = dist / 'data-chunks'
        pieces.mkdir()

        def pack(row):
            rel, path = row
            chunks, source = [], hashlib.sha256()
            with path.open('rb') as f:
                while raw := f.read(BLOCK):
                    source.update(raw)
                    name = hashlib.sha256(raw).hexdigest()
                    body = gzip.compress(raw, compresslevel=6, mtime=0)
                    (pieces / (name + '.gz')).write_bytes(body)
                    chunks.append(name)
            return 'game/' + rel, {'size': path.stat().st_size, 'sha256': source.hexdigest(), 'chunks': chunks}

        with ThreadPoolExecutor(max_workers=3) as pool:
            chunks = dict(pool.map(pack, files))
        write_json(dist / 'data-chunks.json', {'block': BLOCK, 'files': chunks})
    write_json(dist / 'game-files.json', [[rel, path.stat().st_size] for rel, path in files])
    write_json(dist / 'extra-files.json', [['a_cheats.ras', extra.stat().st_size]])
    write_json(dist / 'save-files.json', [])
    # Materialize only the launcher's referenced original assets; the server no longer needs an archive parser.
    import launcher_assets
    assets = launcher_assets.GameFiles(str(game))
    chapters = (web / 'launcher/chapters.json').read_text()
    paths = set(re.findall(r'"path":\s*"([^"]+)"', chapters))
    paths.update(['data/menu/bitmaps/Menu_bg_Mainmenu.tga.png', 'data/menu/bitmaps/cursor.pcx.png',
                  'data/menu/bitmaps/cursor_alpha.pcx.png'])
    audio = (web / 'launcher-audio.js').read_text().split('const FILES = {', 1)[1].split('};', 1)[0]
    for base, rel in re.findall(r"[\w]+:\s*([SM])\s*\+\s*'([^']+)'", audio):
        paths.add('data/database/' + ('sounds/' if base == 'S' else 'music/') + rel)
    for rel in sorted(paths):
        src, png = assets.find(rel)
        if src is None:
            raise ValueError('missing launcher asset: ' + rel)
        target = dist / 'gamefile' / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(assets.png(src) if png else assets.raw(src))
    # Precompress engine/images/text once. Game pieces already have an independent gzip representation, and the raw
    # game tree is read with Range, which a Content-Encoding sibling would not help (the page never asks for *.gz).
    for path in sorted(dist.rglob('*')):
        if path.is_file() and not path.is_relative_to(dist / 'game') and \
                path.suffix.lower() in ('.js', '.json', '.html', '.css', '.dll', '.exe', '.webmanifest'):
            Path(str(path) + '.gz').write_bytes(gzip.compress(path.read_bytes(), compresslevel=6, mtime=0))
    (dist / '_headers').write_text('/*\n  Cross-Origin-Opener-Policy: same-origin\n'
        '  Cross-Origin-Embedder-Policy: require-corp\n  Cross-Origin-Resource-Policy: same-origin\n'
        '/data-chunks/*\n  Content-Encoding: gzip\n  Content-Type: application/octet-stream\n')
    manifest = [[p.relative_to(dist).as_posix(), p.stat().st_size, sha(p)] for p in sorted(dist.rglob('*')) if p.is_file()]
    write_json(dist / 'manifest.json', {'format': 'mp-cdn-1' if cdn else 'mp-local-1', 'block': BLOCK,
        'sourceBytes': sum(p.stat().st_size for _, p in files), 'files': manifest, 'bytes': sum(r[1] for r in manifest),
        'credits': ['RE-NFSU tools/build_public.py and tools/data_chunks.py (4f461be)',
                    'WebGL 2: webgpu-js (MIT), adapted by RE-NFSU (12d05ab) and Max Payne']})
    print(f'{dist}: {len(manifest)} files, {sum(r[1] for r in manifest) / 1e6:.1f} MB; '
          f'{len(files)} source files, wasm br {Path(str(wasm) + ".br").stat().st_size / 1e6:.1f} MB')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=Path.home() / 'Games/maxpayne')
    parser.add_argument('--dist', type=Path, default=ROOT / 'build/public')
    parser.add_argument('--cdn', action='store_true', help='raw game/ tree for an object store (tools/upload_r2.py)')
    args = parser.parse_args()
    build(args.game.resolve(), args.dist.resolve(), args.cdn)
