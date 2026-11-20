#!/usr/bin/env python3
"""Sound census of the Max Payne RAS archives (read-only).

usage: sounds_census.py [game dir] [out tsv]

One row per .wav in ~/Games/maxpayne/*.ras, read with tools/ras.py (the same reader the port uses):
  path        archive:in-archive path, e.g. x_data.ras:a/database/sounds/weapons/shoot_beretta.wav
  group       sound group from the 235_*.txt files (RAS dir 235 = a\\database\\sounds\\); the group is the
              txt name without .txt, several groups joined by ","; NA when no txt names the file
  channels    WAVEFORMATEX nChannels
  bits        WAVEFORMATEX wBitsPerSample (this release: every wav is PCM tag 1)
  rate        WAVEFORMATEX nSamplesPerSec
  seconds     frames / rate, frames = data chunk bytes / nBlockAlign
  peak        max |sample - center| in LSB (center 128 for 8-bit unsigned, 0 for 16-bit signed)
  all_silent  1 when peak < 2 LSB

The tail of the file (after a blank line) is comments: totals, the all_silent list, the files no 235_*.txt
references and the dangling txt references (named but absent from the archives).
"""
import glob
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ras  # noqa: E402

BASE = 'a\\database\\sounds\\'


def dir_names(dt):
    names, p = [], 0
    while p < len(dt):
        e = dt.index(b'\0', p)
        names.append(dt[p:e].decode('latin-1'))
        p = e + 21
    return names


def wav_info(b):
    if b[:4] != b'RIFF' or b[8:12] != b'WAVE':
        raise ValueError('not RIFF/WAVE')
    p, fmt, data = 12, None, None
    while p + 8 <= len(b):
        cid = b[p:p + 4]
        size = struct.unpack_from('<I', b, p + 4)[0]
        if cid == b'fmt ':
            fmt = b[p + 8:p + 8 + size]
        elif cid == b'data':
            data = b[p + 8:p + 8 + size]
        p += 8 + size + (size & 1)
    if fmt is None or data is None:
        raise ValueError('missing fmt/data chunk')
    tag, ch, rate, avg, align, bits = struct.unpack_from('<HHIIHH', fmt, 0)
    if tag != 1:
        raise ValueError(f'codec {tag} not PCM')
    frames = len(data) // align
    if bits == 8:
        peak = max((abs(v - 128) for v in data), default=0)
    else:
        s = struct.unpack('<%dh' % (len(data) // 2), data[:len(data) // 2 * 2])
        peak = max((abs(v) for v in s), default=0)
    return tag, ch, bits, rate, frames / rate, peak


def norm_ref(v):
    v = v.strip().strip(';').strip()
    if v.startswith('"'):
        v = v.strip('"')
    return v.replace('\\', '/').lower()


def read_groups(path):
    """group name -> list of file paths (lowercase, forward slashes) named in its 235_*.txt"""
    f, seed, ver, files, dt = ras.read(path)
    dirs = dir_names(dt)
    groups, dangling = {}, {}
    for x in files:
        if x['dir'] >= len(dirs) or dirs[x['dir']].lower() != BASE.lower():
            continue
        if not x['name'].lower().endswith('.txt'):
            continue
        group = x['name'].rsplit('.', 1)[0]
        txt = ras.extract(f, x).decode('latin-1')
        refs = groups.setdefault(group, [])
        for m in re.finditer(r'(?im)^[ \t]*Filename[ \t]*=[ \t]*(.+?)[ \t]*$', txt):
            refs.append(norm_ref(m.group(1)))
    f.close()
    return groups


def main():
    game = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser('~/Games/maxpayne')
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, '..', 'census', 'sounds.tsv')

    groups = read_groups(os.path.join(game, 'x_data.ras'))
    by_file = {}
    for group, refs in groups.items():
        for r in refs:
            by_file.setdefault(r, [])
            if group not in by_file[r]:
                by_file[r].append(group)

    rows, seen = [], set()
    for arc in sorted(glob.glob(os.path.join(game, '*.ras'))):
        f, seed, ver, files, dt = ras.read(arc)
        dirs = dir_names(dt)
        for x in files:
            if not x['name'].lower().endswith('.wav'):
                continue
            path = (dirs[x['dir']] + x['name']).replace('\\', '/')
            tag, ch, bits, rate, sec, peak = wav_info(ras.extract(f, x))
            rel = path.lower().removeprefix('a/database/sounds/')
            gs = by_file.get(rel, [])
            seen.add(rel)
            rows.append({
                'path': f'{os.path.basename(arc)}:{path}',
                'group': ','.join(gs) if gs else 'NA',
                'channels': ch, 'bits': bits, 'rate': rate,
                'seconds': f'{sec:.3f}', 'peak': peak,
                'all_silent': 1 if peak < 2 else 0,
            })
        f.close()

    rows.sort(key=lambda r: r['path'])
    cols = ['path', 'group', 'channels', 'bits', 'rate', 'seconds', 'peak', 'all_silent']
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, 'w', encoding='utf-8', newline='\n') as o:
        o.write('\t'.join(cols) + '\n')
        for r in rows:
            o.write('\t'.join(str(r[c]) for c in cols) + '\n')

        silent = [r for r in rows if r['all_silent']]
        unreferenced = [r for r in rows if r['group'] == 'NA']
        dangling = sorted(set(by_file) - seen)
        o.write('\n')
        o.write(f"# totals: files={len(rows)} groups={len(groups)} referenced={len(rows) - len(unreferenced)} "
                f"unreferenced={len(unreferenced)} all_silent={len(silent)} dangling_refs={len(dangling)}\n")
        o.write(f"# all_silent ({len(silent)}):\n")
        for r in silent:
            o.write(f"#   {r['path']} peak={r['peak']} seconds={r['seconds']}\n")
        o.write(f"# unreferenced by 235_*.txt ({len(unreferenced)}):\n")
        for r in unreferenced:
            o.write(f"#   {r['path']}\n")
        if dangling:
            o.write(f"# dangling 235_*.txt refs ({len(dangling)}):\n")
            for d in dangling:
                o.write(f"#   {d} groups={','.join(by_file[d])}\n")
    print(f'{out}: {len(rows)} wav, {len(silent)} all_silent, {len(unreferenced)} unreferenced, '
          f'{len(dangling)} dangling refs')


if __name__ == '__main__':
    main()
