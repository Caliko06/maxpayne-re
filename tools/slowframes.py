#!/usr/bin/env python3
"""Names what ran in every slow play frame: the guest worker's and the page's V8 CPU profiles (ghost --cpuprofile 1)
joined to the telemetry frame by frame, with the referee's cause class next to it.

Usage: python3 tools/slowframes.py <telemetry.ndjson> <profile dir> [min dt ms, default 33.4]

Clock: both profiles are on the renderer's TimeTicks; the telemetry is the page's performance.now(). The guest calls
IDirect3DDevice8_Present once per frame and the page logs that frame's row a moment later, so the offset is the one that
puts the most worker Present starts within 2 ms before a telemetry present (coarse 10 ms scan, then 0.1 ms).
"""
import bisect, collections, glob, json, os, sys

tel, pdir = sys.argv[1], sys.argv[2]
MIN = float(sys.argv[3]) if len(sys.argv) > 3 else 33.4

# ---- telemetry frames, the referee's classes (tools/gate50_referee.py)
head = None; rows = []
for l in open(tel):
    l = l.strip()
    if not l: continue
    o = json.loads(l)
    if o.get("kind") == "session": head = o
    elif "rows" in o: rows += o["rows"]
C = {c: i for i, c in enumerate(head["cols"])}; M = head["modes"]
SUM = ["vsync", "gpuWait", "gilMain", "read", "idleMain", "gpuRead", "save", "saves", "texKb", "pipes", "shaders", "reads", "draws"]
fr = []; acc = None; lt = None
for r in rows:
    if acc is None: acc = {k: 0 for k in SUM}
    for k in SUM: acc[k] += r[C[k]]
    if r[C["present"]]:
        t = r[C["t"]]
        if lt is not None:
            acc.update(t=t, t0=lt, dt=t - lt, mode=M[int(r[C["mode"]])] if r[C["mode"]] >= 0 else "?", bt=r[C["bt"]])
            acc["game"] = max(0, acc["dt"] - acc["vsync"] - acc["gpuWait"] - acc["gilMain"] - acc["read"] - acc["idleMain"] - acc["gpuRead"] - acc["save"])
            fr.append(acc)
        lt = t; acc = None
def near(i, f, w): return any(f(fr[j]) for j in range(max(0, i - w), min(len(fr), i + w + 1)))
def cause(i):
    f = fr[i]
    if near(i, lambda g: g["mode"] in ("load", "menu", "boot"), 3): return "load tail"
    if near(i, lambda g: g["saves"] > 0 or g["save"] > 0, 30): return "save"
    if f["gpuRead"] > 0: return "readback"
    if f["pipes"] + f["shaders"] > 0: return "new pipeline"
    if f["texKb"] > 2000: return "texture upload"
    if f["read"] > 8: return "file read"
    if any(fr[j]["bt"] != fr[j - 1]["bt"] for j in range(max(1, i - 7), min(len(fr), i + 8))): return "bt toggle"
    if f["game"] > 33: return "game code"
    if f["vsync"] > f["game"]: return "vsync"
    return "mixed"

# ---- profiles
def load(p):
    d = json.load(open(p)); nodes = {n["id"]: n for n in d["nodes"]}; par = {}
    for n in d["nodes"]:
        for c in n.get("children", []): par[c] = n["id"]
    cache = {}
    def st(i):
        if i not in cache:
            s = []; j = i
            while j in nodes: s.append(nodes[j]["callFrame"]["functionName"] or "(anon)"); j = par.get(j)
            cache[i] = s
        return cache[i]
    t = d["startTime"]; ts = []
    for dt in d["timeDeltas"]: t += dt; ts.append(t / 1000.0)
    return [(tt, st(s)) for s, tt in zip(d["samples"], ts)]
work = None
for p in sorted(glob.glob(os.path.join(pdir, "worker-*.cpuprofile"))):
    s = load(p)
    n = sum(1 for _, k in s if k[0].startswith("f_"))
    if work is None or n > work[0]: work = (n, s)
work = work[1]
page = load(os.path.join(pdir, "page.cpuprofile")) if os.path.exists(os.path.join(pdir, "page.cpuprofile")) else []
def in_present(k): return len(k) > 2 and "IDirect3DDevice8_Present" in k[:4]
pres = [t for i, (t, k) in enumerate(work) if in_present(k) and (i == 0 or not in_present(work[i - 1][1]))]
tp = [f["t"] for f in fr]
def score(off, tol):
    n = 0
    for t in pres:
        j = bisect.bisect_left(tp, t + off)
        if j < len(tp) and tp[j] - (t + off) <= tol: n += 1
    return n
# coarse: the profile stops ~1 s after the last telemetry row (ghost report + shot), scanned +-8 s at 2 ms on 1 in 10 Presents
sub = pres[::10]
def score_sub(off):
    n = 0
    for t in sub:
        j = bisect.bisect_left(tp, t + off)
        if j < len(tp) and tp[j] - (t + off) <= 2: n += 1
    return n
end = tp[-1] - work[-1][0]
best = max((score_sub(end + 2 * k), end + 2 * k) for k in range(-4000, 4001))
fine = max((score(best[1] + 0.1 * k, 2), best[1] + 0.1 * k) for k in range(-30, 31))
off = fine[1]
print(f"offset {off:.1f} ms: {fine[0]} of {len(pres)} worker Presents land <=2 ms before a telemetry present")

wt = [t + off for t, _ in work]; pt = [t + off for t, _ in page]
SKIP = {"(root)", "(program)", "handleMessage", "main", "_main_thread", "js-to-wasm", "wasm-to-js", "invokeEntryPoint", "rn_call"}
def window(samples, times, a, b):
    i, j = bisect.bisect_left(times, a), bisect.bisect_left(times, b)
    busy = 0.0; incl = collections.Counter(); leaf = collections.Counter(); prev = a
    for k in range(i, j):
        d = times[k] - prev; prev = times[k]; st = samples[k][1]
        leaf[st[0]] += d
        if st[0] in ("(idle)",) or (st[0] in ("_do_futex_wait", "__pthread_cond_timedwait") and in_present(st[1:] if st[0] == "__pthread_cond_timedwait" else st)): continue
        busy += d
        for fn in set(st):
            if fn not in SKIP: incl[fn] += d
    return busy, incl, leaf
cls = collections.Counter(); per = collections.defaultdict(list)
for i, f in enumerate(fr):
    if f["mode"] not in ("game", "comic") or f["dt"] <= MIN: continue
    c = cause(i); cls[c] += 1
    wb, wi, wl = window(work, wt, f["t0"], f["t"])
    pb, pi, pl = window(page, pt, f["t0"], f["t"])
    per[c].append((f, wb, wi, wl, pb, pi, pl))
for c, n in cls.most_common():
    print(f"\n=== {c}: {n} frames at", [round(x[0]["t"]/1000) for x in per[c]])
    for f, wb, wi, wl, pb, pi, pl in per[c][:12]:
        print(f"t {f['t']/1000:8.2f} dt {f['dt']:5.1f} tel game {f['game']:5.1f} vsync {f['vsync']:5.1f} bt {f['bt']} | worker busy {wb:5.1f} | page busy {pb:5.1f}")
        print("   worker:", ", ".join(f"{a} {b:.1f}" for a, b in wi.most_common(8)))
        print("   w-leaf:", ", ".join(f"{a} {b:.1f}" for a, b in wl.most_common(5)))
        print("   page  :", ", ".join(f"{a} {b:.1f}" for a, b in pl.most_common(6)))
# per class: mean inclusive ms per frame, guest functions only, deeper than the frame loop
print("\n--- per class, mean inclusive ms per frame (worker)")
for c, n in cls.most_common():
    agg = collections.Counter()
    for x in per[c]:
        for a, b in x[2].items(): agg[a] += b / n
    top = [(a, b) for a, b in agg.most_common(60) if a not in ("f_40c020", "f_10610f3c", "f_76fb04", "f_40bcd0", "f_783860", "f_7838b8")][:22]
    print(f"{c} ({n}):", ", ".join(f"{a} {b:.1f}" for a, b in top))
