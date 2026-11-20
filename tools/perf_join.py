#!/usr/bin/env python3
"""Join one ghost telemetry session with the guest worker's V8 CPU profile by time, so the slow game-code frames
(dt > 33.4 ms whose time is guest CPU, the referee's "game code (guest CPU)" class) can be named per guest function.

Usage: python3 join.py <telemetry.ndjson> <profile dir> <out top.txt>

Clocks: V8's profile startTime is an arbitrary epoch (worker time origin), the telemetry rows carry the page's
performance.now(). The offset is found by cross-correlating two 100 ms series: lifted guest samples (f_*) per bin
against the frames' own guest time (the referee's `game` column) per bin. The end anchor (profile end vs last
present, plus the ~0.9 s the ghost spends writing the report) is printed next to it as an independent check.

If the session has no frame in the referee's "game code (guest CPU)" class (a 240 s run on a loaded Mac can end with
none: this one had 0), the same question is asked of the frames whose guest time dominates the frame (game >= 15 ms).
"""
import bisect, collections, glob, json, os, sys

tel_path, prof_dir, out_path = sys.argv[1], sys.argv[2], sys.argv[3]

# ---------------------------------------------------------------- telemetry frames (the referee's own rules)
head = None; rows = []
for line in open(tel_path):
    line = line.strip()
    if not line:
        continue
    o = json.loads(line)
    if o.get("kind") == "session":
        head = o
    elif "rows" in o:
        rows += o["rows"]
C = {c: i for i, c in enumerate(head["cols"])}; M = head["modes"]
SUM = ["vsync", "gpuWait", "gilMain", "read", "idleMain", "gpuRead", "save", "saves", "texKb", "pipes", "shaders", "reads", "upKb", "draws"]
fr = []; acc = None; lt = None
for r in rows:
    if acc is None:
        acc = {k: 0 for k in SUM}
    for k in SUM:
        acc[k] += r[C[k]]
    if r[C["present"]]:
        t = r[C["t"]]
        if lt is not None:
            acc.update(t=t, dt=t - lt, mode=M[int(r[C["mode"]])] if r[C["mode"]] >= 0 else "?",
                       bt=r[C["bt"]] if "bt" in C else 0)
            acc["game"] = max(0, acc["dt"] - acc["vsync"] - acc["gpuWait"] - acc["gilMain"] - acc["read"]
                              - acc["idleMain"] - acc["gpuRead"] - acc["save"])
            fr.append(acc)
        lt = t; acc = None


def near(i, f, w):
    return any(f(fr[j]) for j in range(max(0, i - w), min(len(fr), i + w + 1)))


cause = {}
for i, f in enumerate(fr):
    if f["mode"] not in ("game", "comic") or f["dt"] <= 33.4:
        continue
    if near(i, lambda g: g["mode"] in ("load", "menu", "boot"), 3):
        c = "tail of load/mode switch"
    elif near(i, lambda g: g["saves"] > 0 or g["save"] > 0, 30):
        c = "save (autosave/quicksave)"
    elif f["gpuRead"] > 0:
        c = "GPU readback"
    elif f["pipes"] + f["shaders"] > 0:
        c = "new shader/pipeline"
    elif f["texKb"] > 2000:
        c = "texture upload"
    elif f["read"] > 8:
        c = "file read"
    elif any(fr[j]["bt"] != fr[j - 1]["bt"] for j in range(max(1, i - 7), min(len(fr), i + 8))):
        c = "bullet time toggle"
    elif f["game"] > 33:
        c = "game code (guest CPU)"
    elif f["vsync"] > f["game"]:
        c = "vsync wait (missed beat)"
    else:
        c = "mixed game+vsync"
    cause[i] = c

referee_game = [i for i, c in cause.items() if c == "game code (guest CPU)" and fr[i]["mode"] == "game"]
mixed = [i for i, c in cause.items() if c == "mixed game+vsync"]
normal = []
for i, f in enumerate(fr):
    if f["mode"] != "game" or not (0 < f["dt"] <= 20) or f["saves"] or f["save"] > 0 or f["game"] > 8:
        continue
    if near(i, lambda g: g["mode"] in ("load", "menu", "boot"), 3) or near(i, lambda g: g["saves"] > 0 or g["save"] > 0, 30):
        continue
    normal.append(i)
HEAVY = 15.0
heavy = [i for i, f in enumerate(fr) if f["mode"] == "game" and f["game"] >= HEAVY and not f["saves"] and f["save"] <= 0]

# ---------------------------------------------------------------- the guest worker's profile
best = None
for p in sorted(glob.glob(os.path.join(prof_dir, "worker-*.cpuprofile"))):
    d = json.load(open(p))
    idx = {n["id"]: n for n in d["nodes"]}
    guest = sum(1 for s in d["samples"] if idx[s]["callFrame"]["functionName"].startswith("f_"))
    if guest and (best is None or guest > best[0]):
        best = (guest, p, d, idx)
if not best:
    sys.exit("no worker profile with lifted guest functions (f_*) in " + prof_dir)
guest_samples, ppath, d, idx = best

parent = {}
for n in d["nodes"]:
    for ch in n.get("children", []):
        parent[ch] = n["id"]
name = {n["id"]: n["callFrame"]["functionName"] for n in d["nodes"]}
guest_node = {nm for nm in name.values() if nm.startswith("f_")}   # by function name, not by node id

t = d["startTime"]; times = []; wts = []          # wts[k]: the sample's own duration in ms (the V8 interval is ~1.02 ms, not
for dt in d["timeDeltas"]:                       # the 0.5 ms asked for: weight every sample by its real timeDeltas)
    t += dt; times.append(t); wts.append(dt / 1000.0)
span_ms = (d["endTime"] - d["startTime"]) / 1000.0

# offset by correlation of f_* samples per 100 ms bin against each frame's guest time per 100 ms bin
B = 100
gb = collections.Counter(); fb = collections.Counter()
for s, ts in zip(d["samples"], times):
    if name[s] in guest_node:
        gb[int((ts - d["startTime"]) // (B * 1000))] += 1
for f in fr:
    fb[int(f["t"] // B)] += f["game"]
maxg = max(gb) if gb else 0; maxf = max(fb) if fb else 0


def corr(lag):
    xs = [gb.get(k, 0) for k in range(0, maxg + 1)]
    ys = [fb.get(k + lag, 0) for k in range(0, maxg + 1)]
    n = len(xs); mx = sum(xs) / n; my = sum(ys) / n
    a = [x - mx for x in xs]; b = [y - my for y in ys]
    den = (sum(x * x for x in a) * sum(y * y for y in b)) ** 0.5
    return sum(x * y for x, y in zip(a, b)) / den if den else 0.0


bestlag = max(range(0, 90), key=corr)                      # 0..9 s
if os.environ.get("RN_LAG"):                               # RN_LAG=ms: force an offset, for the sensitivity check
    bestlag = int(os.environ["RN_LAG"]) // B
bestr = corr(bestlag)
teardown = 900                                             # ghost: report.json + end shot before Profiler.stop
end_lag = fr[-1]["t"] + teardown - span_ms
offset_ms = bestlag * B
last_t = fr[-1]["t"]

# the slow set: the referee's own class, else the frames whose guest time dominates the frame
slow = referee_game if referee_game else heavy
slow_label = ("referee 'game code (guest CPU)' frames" if referee_game
              else f"guest-heavy frames: game >= {HEAVY:.0f} ms (guest CPU dominates the frame)")

# ---------------------------------------------------------------- per-window self time
names = {}
for tsv in glob.glob(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../rea/map/names/*.tsv")):
    with open(tsv) as fh:
        next(fh, None)
        for line in fh:
            p = line.rstrip("\n").split("\t")
            if len(p) > 6 and p[0]:
                names[int(p[0], 16)] = (p[1], p[2], p[5], p[6])


def rng_of(f):
    t1 = f["t"] - offset_ms          # page ms -> profile ms (page = profile + offset)
    t0 = t1 - f["dt"]
    a = bisect.bisect_left(times, t0 * 1000 + d["startTime"])
    b = bisect.bisect_right(times, t1 * 1000 + d["startTime"])
    return range(a, b)


def tally(idxs):
    per = collections.Counter(); tot = 0.0            # per: node id -> ms inside those frames
    for i in idxs:
        for k in rng_of(fr[i]):
            tot += wts[k]
            if name[d["samples"][k]] in guest_node:
                per[d["samples"][k]] += wts[k]
    return per, tot


slow_per, slow_all = tally(slow)
inc_slow_total = sum(slow_per.values())
norm_per, norm_all = tally(normal)
inc_norm_total = sum(norm_per.values())
nslow, nnorm = max(1, len(slow)), max(1, len(normal))


def chain(node):
    out = []
    while node is not None:
        out.append(node)
        node = parent.get(node)
    return out


def inclusive(per):
    inc = collections.Counter()
    for nodeid, c in per.items():
        for anc in chain(nodeid):
            if name[anc] in guest_node:
                inc[name[anc]] += c
    return inc


inc_slow, inc_norm = inclusive(slow_per), inclusive(norm_per)
self_slow = collections.Counter(); self_norm = collections.Counter()
for nodeid, c in slow_per.items():
    self_slow[name[nodeid]] += c
for nodeid, c in norm_per.items():
    self_norm[name[nodeid]] += c


def rowms(c, n):
    return c / n                 # c is already ms


rowsout = []
for fn, c in inc_slow.items():
    addr = int(fn[2:], 16)
    nm = names.get(addr, ("", "", "", ""))
    rowsout.append((c, fn, addr, nm, self_slow.get(fn, 0), self_norm.get(fn, 0), inc_norm.get(fn, 0)))
rowsout.sort(reverse=True)

rowsout = []
for fn in set(list(inc_slow) + list(inc_norm)):
    addr = int(fn[2:], 16) if fn.startswith("f_") else 0
    nm = names.get(addr, ("", "", "", ""))
    rowsout.append((self_slow.get(fn, 0.0), fn, addr, nm, inc_slow.get(fn, 0.0), self_norm.get(fn, 0.0), inc_norm.get(fn, 0.0)))
rowsout.sort(reverse=True)

L = []
L.append(f"session {tel_path}: {len(fr)} frames; {len(referee_game)} frames in the referee's 'game code (guest CPU)' class "
         f"(dt > 33.4 ms, guest CPU), {len(mixed)} mixed game+vsync, {len(heavy)} frames with guest CPU >= {HEAVY:.0f} ms")
L.append(f"slow set used: {len(slow)} frames, {slow_label}; baseline: {len(normal)} normal play frames "
         f"(game <= 8 ms, dt <= 20 ms, no save, not next to a mode change)")
L.append(f"profile {os.path.basename(ppath)}: {len(d['samples'])} samples over {span_ms/1000:.1f} s "
         f"(mean sample {1000*span_ms/len(d['samples']):.2f} ms), {guest_samples} of them lifted guest (f_*)")
L.append(f"alignment: {offset_ms} ms from the 100 ms cross-correlation (r={bestr:.2f}); end anchor {end_lag:.0f} ms "
         f"(profile end -> page {span_ms + end_lag:.0f} ms, last present {last_t:.0f} ms)")
L.append(f"lifted guest CPU inside the windows: {rowms(slow_all, nslow):.2f} ms per slow frame vs "
         f"{rowms(norm_all, nnorm):.2f} ms per normal frame (share of lifted code in the call tree: "
         f"{100.0*rowms(inc_slow_total, nslow)/max(1e-9, rowms(slow_all, nslow)):.0f}% / "
         f"{100.0*rowms(inc_norm_total, nnorm)/max(1e-9, rowms(norm_all, nnorm)):.0f}%)")
L.append("")
L.append("A) self time, the leaves that actually run (ranked by the extra time they take in slow frames)")
L.append(f"{'addr':>10} {'slf/slow':>9} {'slf/norm':>9} {'delta':>8} {'inc/slow':>9} {'inc/norm':>9}  name")
for s_slow, fn, addr, nm, i_slow, s_norm, i_norm in rowsout[:20]:
    L.append(f"{addr:>10x} {rowms(s_slow, nslow):9.3f} {rowms(s_norm, nnorm):9.3f} "
             f"{rowms(s_slow, nslow) - rowms(s_norm, nnorm):8.3f} {rowms(i_slow, nslow):9.3f} {rowms(i_norm, nnorm):9.3f}  "
             f"{nm[0] or fn}" + (f"  [{nm[1]}/{nm[2]}]" if nm[2] else ""))
L.append("")
L.append("B) the same, by total time in the slow frames (inclusive)")
rowsout.sort(key=lambda r: -r[4])
for s_slow, fn, addr, nm, i_slow, s_norm, i_norm in rowsout[:20]:
    L.append(f"{addr:>10x} {rowms(i_slow, nslow):9.3f} {rowms(i_norm, nnorm):9.3f}  {nm[0] or fn}")
L.append("")
L.append("C) caller chains of the hot leaves above and what each one does (names and summaries from rea/map/names/*.tsv)")
rowsout.sort(reverse=True)
for s_slow, fn, addr, nm, i_slow, s_norm, i_norm in rowsout[:8]:
    if s_slow <= 0:
        continue
    cand = sorted([(v, nid) for nid, v in slow_per.items() if name[nid] == fn], reverse=True)
    chn = []
    if cand:
        for anc in chain(cand[0][1]):
            nmx = name[anc]
            if nmx in guest_node:
                chn.append(f"{nmx}({names.get(int(nmx[2:], 16), ('',))[0]})")
            elif nmx not in ("(root)",):
                chn.append(nmx)
    L.append(f"- {addr:08x} {nm[0] or fn}: self {rowms(s_slow, nslow):.2f} ms per slow frame vs {rowms(s_norm, nnorm):.2f} "
             f"per normal; inclusive {rowms(i_slow, nslow):.2f} / {rowms(i_norm, nnorm):.2f}")
    L.append(f"    chain: {' <- '.join(chn[:7])}")
    if nm[3]:
        L.append(f"    {nm[3]}")
open(out_path, "w").write("\n".join(L) + "\n")
print("\n".join(L))
