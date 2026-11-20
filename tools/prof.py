#!/usr/bin/env python3
"""Names the RN_PROF samples (host/main.c): self time per function of build/maxpayne, grouped per guest image.
Usage: python3 tools/prof.py <prof file> [from_s] [to_s] [top] [binary]"""
import bisect, struct, subprocess, sys, collections

raw = open(sys.argv[1], "rb").read()
lo = int(sys.argv[2]) if len(sys.argv) > 2 else 0
hi = int(sys.argv[3]) if len(sys.argv) > 3 else 1 << 30
top = int(sys.argv[4]) if len(sys.argv) > 4 else 40
syms = []
for ln in subprocess.run(["nm", "-n", "--defined-only", sys.argv[5] if len(sys.argv) > 5 else "build/maxpayne"], capture_output=True, text=True).stdout.splitlines():
    a, t, n = ln.split()[:3]
    if t in "tTwW":
        syms.append((int(a, 16), n))
addrs = [a for a, _ in syms]
_, _, mark = struct.unpack_from("<IIQ", raw, 0)          # host/main.c prof_write: its own runtime address
base = mark - next(a for a, n in syms if n.lstrip("_") == "prof_write")
cnt = collections.Counter()
total = 0
for k in range(16, len(raw) // 16 * 16, 16):
    sec, _, rip = struct.unpack_from("<IIQ", raw, k)
    if lo <= sec < hi:
        i = bisect.bisect_right(addrs, rip - base) - 1
        cnt[syms[i][1] if i >= 0 else "?"] += 1
        total += 1
print(f"{total} samples in [{lo}, {hi}) s")
for n, c in cnt.most_common(top):
    print(f"{c:7d} {100 * c / total:5.1f}%  {n}")
