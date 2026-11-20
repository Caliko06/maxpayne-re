#!/usr/bin/env python3
"""Upload a --cdn package (tools/build_public.py --cdn) to a Cloudflare R2 bucket, or print its blueprint.
Usage: tools/upload_r2.py <bucket> [--dist build/public-cdn] [--only page,engine] [--dry-run]
The pass list is the blueprint: every file in the package lands in exactly one pass, and the dry run fails if one does not
(the shell filters this replaces dropped out/maxpayne.js and launcher/chapters.json: rclone's * stops at a slash).
Run it for real ONLY with the OWNER's go (AGENTS.md). Needs rclone with a remote named "r2" (s3, provider Cloudflare, the
R2 token, endpoint https://<account id>.r2.cloudflarestorage.com). Port of RE-NFSU tools/upload_r2.sh.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DAY = 'Cache-Control: public, max-age=86400'
NO = 'Cache-Control: no-cache'


def classify(rel):
    """(pass, why) for one package-relative path; pass None means it never leaves this Mac."""
    name, suffix = rel.rsplit('/', 1)[-1], Path(rel).suffix.lower()
    if name.startswith('.'):
        return None, 'macOS metadata (.DS_Store)'
    if rel == '_headers':
        return None, 'Cloudflare Pages syntax; R2 ignores it (the same three headers are zone rules, docs/hosting.md)'
    if rel == 'out/maxpayne.wasm.br':
        return None, 'its bytes are uploaded as out/maxpayne.wasm'
    if rel == 'out/maxpayne.wasm':
        return 'engine', ''
    if suffix == '.gz':
        return None, 'Content-Encoding twin for tools/static_server.py; the page never requests it (the edge compresses)'
    if rel.startswith('game/'):
        return 'game', ''
    if suffix in ('.html', '.js', '.json', '.webmanifest'):
        return 'page', ''
    return 'assets', ''


def plan(dist):
    """{pass: [(rel, size)]} plus {rel: why} for what stays, and the paths that fell through."""
    passes, skipped, orphan = {}, {}, []
    for path in sorted(dist.rglob('*')):
        if not path.is_file():
            continue
        rel = path.relative_to(dist).as_posix()
        which, why = classify(rel)
        if which is None:
            skipped[rel] = why
        elif which in ('game', 'assets', 'page', 'engine'):
            passes.setdefault(which, []).append((rel, path.stat().st_size))
        else:
            orphan.append(rel)
    if orphan:
        raise ValueError('no pass for: ' + ', '.join(orphan[:5]))
    return passes, skipped


def blueprint(dist, passes, skipped):
    total = sum(size for rows in passes.values() for _, size in rows)
    print(f'{dist}: {sum(len(r) for r in passes.values())} objects, {total / 1e9:.2f} GB, {len(skipped)} files stay local')
    print('| Pass | Objects | Bytes | Header | Sent when |')
    print('|---|---|---|---|---|')
    rules = {
        'game': ('game/**', DAY, 'changed (--checksum); first, so a boot works mid-upload'),
        'assets': ('img, gamefile, fonts, launcher/*.json…', DAY, 'changed (--checksum)'),
        'page': ('*.html, *.js, *.json, *.webmanifest', NO, 'every time (--ignore-times): the header must reach unchanged bytes'),
        'engine': ('out/maxpayne.wasm ← out/maxpayne.wasm.br', NO + ', Content-Encoding: br, Content-Type: application/wasm', 'every time'),
    }
    for which, (label, header, when) in rules.items():
        rows = passes.get(which, [])
        print(f'| {which}: {label} | {len(rows)} | {sum(s for _, s in rows) / 1e6:.1f} MB | {header} | {when} |')
    for which in ('game', 'assets', 'page'):
        rows = passes.get(which, [])
        print(f'{which} first three: ' + ', '.join(rel for rel, _ in rows[:3]))
    for why in sorted(set(skipped.values())):
        print(f'stays local ({sum(1 for w in skipped.values() if w == why)}): {why}')


def upload(bucket, dist, passes, only):
    remote = f'r2:{bucket}'

    def rclone(*args):
        cmd = ['rclone', *args, '--s3-no-check-bucket', '--transfers', '16']
        print('$ ' + ' '.join(cmd[:3]) + ' …', flush=True)
        subprocess.run(cmd, check=True)

    def copy_pass(which, header, fresh):
        rows = passes.get(which, [])
        if not rows or which not in only:
            return
        with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as f:
            f.write('\n'.join(rel for rel, _ in rows) + '\n')
            listing = f.name
        rclone('copy', str(dist), remote, '--files-from-raw', listing, '--ignore-times' if fresh else '--checksum',
               '--header-upload', header, '--progress')
        Path(listing).unlink()

    copy_pass('game', DAY, fresh=False)      # 2.1 GB first: an upload cut short still boots and reaches the first level
    copy_pass('assets', DAY, fresh=False)
    copy_pass('page', NO, fresh=True)
    # out/maxpayne.wasm.br -> out/maxpayne.wasm: 125 MB raw, 14.8 MB brotli, fetched once whole by web/index.html:132 and
    # handed to instantiateStreaming, which needs application/wasm. A client with no br gets bytes it cannot read (the same
    # bet RE-NFSU made with gzip on speed.wasm); every browser that runs this build sends br.
    if 'engine' not in only:
        return
    rclone('copyto', str(dist / 'out/maxpayne.wasm.br'), f'{remote}/out/maxpayne.wasm', '--ignore-times',
           '--header-upload', NO, '--header-upload', 'Content-Encoding: br',
           '--header-upload', 'Content-Type: application/wasm')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bucket')
    parser.add_argument('--dist', type=Path, default=ROOT / 'build/public-cdn')
    parser.add_argument('--dry-run', action='store_true', help='print the blueprint, upload nothing')
    # --only page,engine: a page or engine change re-sends 15 MB, not the 2.3 GB game pass (each go names what goes up).
    parser.add_argument('--only', default='game,assets,page,engine', help='comma list of passes to send')
    args = parser.parse_args()
    dist = args.dist.resolve()
    manifest = dist / 'manifest.json'
    if not manifest.is_file():
        sys.exit(f'no {manifest}: run python3 tools/build_public.py --cdn')
    fmt = json.loads(manifest.read_text())['format']
    if fmt != 'mp-cdn-1':
        sys.exit(f'{manifest} is {fmt}: an object store needs --cdn (the page Range-reads /game/<path>, web/opfs.js files())')
    if not (dist / 'game').is_dir():
        sys.exit(f'no {dist}/game: rebuild with --cdn')
    passes, skipped = plan(dist)
    blueprint(dist, passes, skipped)
    if args.dry_run:
        print('dry run: nothing uploaded')
        return
    upload(args.bucket, dist, passes, set(args.only.split(',')))


if __name__ == '__main__':
    main()
