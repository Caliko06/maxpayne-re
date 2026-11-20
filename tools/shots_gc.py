#!/usr/bin/env python3
"""Run shots live 10 min (OWNER 2026-11-09, same rule as mafia-wasm tools/shots_gc.py).
Scope: only out/{final,sound,sniper,launcher}_<MMDD>/ from 2026-11-09 on; older evidence in out/ is never touched.
Kept: git-tracked, cited by rea/map/*.md, ctx/*.md, WORKLIST.md or HANDOFF.md, the newest png of each dir,
and everything under out/keep/ (keepers are copied there by hand)."""
import glob, os, re, subprocess, time
import sys
ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
cited = "".join(open(f, errors="ignore").read() for f in [*glob.glob("rea/map/*.md"), *glob.glob("ctx/*.md"), "WORKLIST.md", "HANDOFF.md"] if os.path.exists(f))
tracked = set(subprocess.run(["git", "ls-files", "out"], capture_output=True, text=True).stdout.split())
dirs = [d for d in glob.glob("out/*_*") if re.search(r"/(final|sound|sniper|launcher|look)_11(09|[1-3]\d)$", d)]
n = 0
for d in dirs:
    pngs = sorted(glob.glob(d + "/**/*.png", recursive=True), key=os.path.getmtime)
    for p in pngs[:-1]:
        if p in tracked or p in cited or time.time() - os.path.getmtime(p) < 600:
            continue
        os.remove(p); n += 1
print(f"shots_gc: {n} shots deleted in {len(dirs)} run dirs")
