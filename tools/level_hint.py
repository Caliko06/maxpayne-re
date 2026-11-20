#!/usr/bin/env python3
"""Stats file (tools/static_server.py RN_STATS) -> byte ranges a level load reads, in first-read order.
level_hint.py <stats> <webgate.log> <level> <device> [offset_s]  >> web/level_hint.tsv
Output: level, path, offset, length, device (tab separated). Ranges already read earlier in the run are dropped (cache
re-reads), neighbours in time that touch are merged. Cut = first steady 'd3d8 Present' step in the log (gap <= 1.3x the
median of the last 3 gaps); the log clock starts ~offset_s (default 1.0, the sleep in run.sh) after the server's.
Skipped: whole files in web/prefetch.json, /game/ only: everything else (wasm, page, launcher) is not game data."""
import json, re, statistics, sys
from pathlib import Path

stats, log, level, device = sys.argv[1:5]
off = float(sys.argv[5]) if len(sys.argv) > 5 else 1.0
whole = set(json.load(open(Path(__file__).resolve().parent.parent / 'web/prefetch.json'))['whole'])
steps = [(float(m[1]), int(m[2])) for m in (re.match(r'([\d.]+) d3d8 Present (\d+):', l) for l in open(log)) if m]
gaps = [(steps[i][0] - steps[i - 1][0], steps[i][0]) for i in range(1, len(steps))]
ref = statistics.median(g for g, _ in gaps[-3:])
cut = next(t for g, t in gaps if g <= 1.3 * ref and t > 20) - off     # >20 s: skip the boot's fast first steps
seen, out, total, uniq, perfile = {}, [], {}, {}, {}
for l in open(stats):
    t, n, path, status, a, b = l.split()[:6]
    if float(t) > cut or not path.startswith('/game/') or path in whole or status not in ('200', '206'):
        continue
    a, b = int(a), int(b) + 1
    total[path] = total.get(path, 0) + b - a
    iv = seen.setdefault(path, [])
    new = [(a, b)]
    for x, y in iv:                                  # subtract what was read before
        new = [r for s, e in new for r in ((s, min(e, x)), (max(s, y), e)) if r[1] > r[0]]
    iv.extend(new)
    for s, e in new:
        uniq[path] = uniq.get(path, 0) + e - s
        if out and out[-1][0] == path and out[-1][2] == s:
            out[-1][2] = e
        else:
            out.append([path, s, e])
for p, s, e in out:
    print(f'{level}\t{p}\t{s}\t{e - s}\t{device}')
print(f'# cut {cut:.1f}s; total {sum(total.values())>>20} MB unique {sum(uniq.values())>>20} MB lines {len(out)}', file=sys.stderr)
for p in sorted(total, key=total.get, reverse=True)[:8]:
    print(f'#   {p}: total {total[p]>>10} KB unique {uniq[p]>>10} KB', file=sys.stderr)
