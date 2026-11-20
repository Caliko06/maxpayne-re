#!/usr/bin/env python3
"""Reads a Max Payne play session's telemetry (web/telemetry.js -> build/telemetry.ndjson, or the F10 download
maxpayne-session-*.json) next to its log (build/web.log) and prints where the time went: frame times per game mode, the
worst frames with what happened around them, mode changes and load windows, saves, audio, the F8 marks, a 10 s timeline.
Usage: python3 tools/telemetry.py [telemetry.ndjson | maxpayne-session-*.json | webcheck] [web.log]
  (default: the OWNER's last session; "webcheck" = the last headless gate run; a .json from F10 carries its own log)
A frame's time is the page's time between two presents. In it: vsync = the game waiting for the screen to take a frame
(it is ahead of 60 Hz: free time), gpuWait = waiting for the page to run its packet (page or GPU slow), gilMain = the
main thread waiting for another guest thread, read = file reads from the server, idle = the main thread in Sleep or a
wait, other = GPU readbacks + save writes, game = the rest (the game's translated code).
"""
import bisect, json, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
B = os.path.join(ROOT, "build")
arg = sys.argv[1] if len(sys.argv) > 1 else ""
tel = {"": os.path.join(B, "telemetry.ndjson"), "webcheck": os.path.join(B, "webcheck.telemetry.ndjson")}.get(arg, arg)
logp = sys.argv[2] if len(sys.argv) > 2 else os.path.join(B, "webcheck.log" if arg == "webcheck" else "web.log")

head, rows, events, inlog = None, [], [], None
if tel.endswith(".json"):
    o = json.load(open(tel))
    head, rows, events, inlog = o, o["rows"], o["events"], o.get("log")
for line in ([] if inlog is not None else open(tel)):
    if not line.strip():
        continue
    try:
        o = json.loads(line)
    except ValueError:
        continue                        # a line cut by the page closing
    if "rows" in o:
        rows += o["rows"]
    elif o.get("kind") == "session":
        head = o
    else:
        events.append(o)
if not head or not rows:
    sys.exit(f"{tel}: no session header or no rows")
C = {c: i for i, c in enumerate(head["cols"])}
MODES = head.get("modes", [])
mname = lambda m: MODES[m] if isinstance(m, int) and 0 <= m < len(MODES) else str(m)
col = lambda r, c: r[C[c]] if c in C else 0

log = []
if inlog is not None or os.path.exists(logp):
    for l in (inlog if inlog is not None else open(logp, errors="replace")):
        p = l.split(" ", 1)
        try:
            log.append((float(p[0]) * 1000, p[1].rstrip()))
        except (ValueError, IndexError):
            pass
log_t = [t for t, _ in log]
NOISE = ("key up", "ShaderValidator", "d3d8 Present ")


def near(t, before=1500, after=300, n=4):
    a, b = bisect.bisect_left(log_t, t - before), bisect.bisect_right(log_t, t + after)
    return [f"{x / 1000:.2f} {s}" for x, s in log[a:b] if not any(k in s for k in NOISE)][-n:]


# Frames: the rows between two presents summed (a load hands packets over in parts; idle rows fill the gaps).
SUM = ["run", "draws", "passes", "upKb", "texKb", "pipes", "shaders", "targets", "gpuWait", "vsync", "gilMain", "gilOther",
       "read", "idleMain", "gpuRead", "save", "reads", "readKb", "gpuReads", "saves", "saveKb", "clockCalls", "starves",
       "underrun", "resyncs", "clips"]
MAX = ["streamPm", "holdMain", "holdOther", "peak"]
frames, acc, last_t = [], None, None
for r in rows:
    if acc is None:
        acc = {k: 0 for k in SUM + MAX}
    for k in SUM:
        acc[k] += col(r, k)
    for k in MAX:
        acc[k] = max(acc[k], col(r, k))
    if col(r, "present"):
        t = col(r, "t")
        if last_t is not None:
            acc["t"], acc["dt"], acc["mode"] = t, t - last_t, col(r, "mode")
            acc["other"] = acc["gpuRead"] + acc["save"]
            acc["game"] = max(0, acc["dt"] - acc["vsync"] - acc["gpuWait"] - acc["gilMain"] - acc["read"] - acc["idleMain"] - acc["other"])
            acc["behind"] = col(r, "audioBehind")
            frames.append(acc)
        last_t, acc = t, None
if not frames:
    sys.exit("no presented frames")

PARTS = ("game", "vsync", "gpuWait", "gilMain", "read", "idleMain", "other")


def summary(fs, label):
    dts = sorted(f["dt"] for f in fs)
    pct = lambda p: dts[min(len(dts) - 1, int(p / 100 * len(dts)))]
    span = sum(dts) / 1000 or 1
    tot = {k: sum(f[k] for f in fs) for k in PARTS + ("dt",)}
    print(f"{label:>8}: {len(fs):6} frames {span:6.0f} s {len(fs) / span:5.1f} fps  ms p50 {pct(50):5.1f} p90 {pct(90):5.1f} "
          f"p99 {pct(99):5.1f} max {dts[-1]:6.0f}  >33 {sum(d > 33 for d in dts)} >50 {sum(d > 50 for d in dts)} "
          f">250 {sum(d > 250 for d in dts)}  | " + " ".join(f"{k} {100 * tot[k] / tot['dt']:.0f}%" for k in PARTS))


span = (frames[-1]["t"] - frames[0]["t"]) / 1000
print(f"session {head['date']}  {head['screen']}  {head['url']}")
print(f"{len(frames)} frames in {span:.0f} s; per mode (time shares of the frame):")
summary(frames, "all")
for m in sorted({f["mode"] for f in frames}, key=str):
    summary([f for f in frames if f["mode"] == m], mname(m))
print(f"files: {sum(f['reads'] for f in frames)} reads {sum(f['readKb'] for f in frames) / 1024:.0f} MB "
      f"{sum(f['read'] for f in frames) / 1000:.1f} s   textures {sum(f['texKb'] for f in frames) / 1024:.0f} MB   "
      f"pipelines {sum(f['pipes'] for f in frames)}  shaders {sum(f['shaders'] for f in frames)}   "
      f"GPU reads {sum(f['gpuReads'] for f in frames)} ({sum(f['gpuRead'] for f in frames) / 1000:.1f} s)")

print("\nworst frames (ms): dt = game + vsync + gpuW + gilM + read + idle + other(GPU reads, saves)")
print(f"{'t s':>8} {'mode':>6} {'dt':>6} {'game':>5} {'vsyn':>5} {'gpuW':>5} {'gilM':>5} {'read':>5} {'idle':>5} {'othr':>5} "
      f"{'hldM':>5} {'run':>5} {'rdKB':>6} {'texKB':>6} {'gRd':>3} {'sav':>3} {'pipe':>4} {'draw':>5}")
for f in sorted(frames, key=lambda f: -f["dt"])[:15]:
    print(f"{f['t'] / 1000:8.2f} {mname(f['mode']):>6} {f['dt']:6.0f} {f['game']:5.0f} {f['vsync']:5.0f} {f['gpuWait']:5.0f} "
          f"{f['gilMain']:5.0f} {f['read']:5.0f} {f['idleMain']:5.0f} {f['other']:5.0f} {f['holdMain']:5.0f} {f['run']:5.0f} "
          f"{f['readKb']:6} {f['texKb']:6} {f['gpuReads']:3} {f['saves']:3} {f['pipes']:4} {f['draws']:5}")
    for s in near(f["t"], 1000, 100, 2):
        print(f"{'':10}| {s[:110]}")

modes = [e for e in events if e["kind"] in ("mode", "level")]
if modes:
    print("\nmode changes (time in the previous mode) and levels; load = game mode before its level is up:")
    for e in modes:
        if e["kind"] == "level":
            print(f"  {e['t'] / 1000:8.2f} s level '{e['name']}'")
        else:
            print(f"  {e['t'] / 1000:8.2f} s {e['from']} -> {e['mode']}  ({e['prevMs'] / 1000:.1f} s)")
saves = [e for e in events if e["kind"] == "save"]
if saves:
    print("\nsaves (files written; the frame around it):")
    for e in saves:
        w = [f for f in frames if e["t"] - 100 <= f["t"] <= e["t"] + 1500]
        print(f"  {e['t'] / 1000:8.2f} s {e['files']} files {e['kb']} KB in {e['ms']} ms; worst frame then "
              f"{max((f['dt'] for f in w), default=0):.0f} ms, GPU reads {sum(f['gpuReads'] for f in w)}")

print(f"\naudio: peak {max(f['peak'] for f in frames)}/32767, clipped samples {sum(f['clips'] for f in frames)}, "
      f"{sum(f['underrun'] for f in frames)} frames of silence (underrun), {sum(f['resyncs'] for f in frames)} resyncs, "
      f"{sum(f['starves'] for f in frames)} stream laps with no refill")
for f in [f for f in frames if f["starves"] or f["clips"]][:10]:
    print(f"  {f['t'] / 1000:8.2f} s {mname(f['mode'])}: starves {f['starves']} clips {f['clips']} peak {f['peak']}, "
          f"frame {f['dt']:.0f} ms, GIL hold {f['holdMain']:.0f}/{f['holdOther']:.0f} ms")
vs = [e for e in events if e["kind"] == "vsync"]
if vs:
    print(f"screen: {vs[-1]['hz']} Hz; repeats {sum(e['rep'] for e in vs)}, dropped {sum(e['drop'] for e in vs)}, "
          f"longest animation frame gap {max(e['maxMs'] for e in vs)} ms")
loafs = [e for e in events if e["kind"] == "loaf"]
if loafs:
    print(f"\nbrowser long animation frames (>= 50 ms): {len(loafs)}; worst:")
    for e in sorted(loafs, key=lambda e: -e["dur"])[:5]:
        print(f"  {e['t'] / 1000:8.2f} s {e['dur']} ms (blocking {e['block']}) {'; '.join(e['scripts'])[:100]}")
other = [e for e in events if e["kind"] in ("mark", "visibility", "focus", "download", "death")]
if other:
    print("\nmarks (F8), deaths, focus, downloads:")
    for e in other:
        line = f"  {e['t'] / 1000:8.2f} s {e['kind']} {e.get('n', e.get('state', e.get('level', '')))}"
        if e["kind"] == "mark":
            w = [g for g in frames if e["t"] - 3000 <= g["t"] <= e["t"]]
            if w:
                line += (f": 3 s before: {len(w) / 3:.0f} fps, worst {max(g['dt'] for g in w):.0f} ms, mode "
                         f"{mname(w[-1]['mode'])}, starves {sum(g['starves'] for g in w)}, read {sum(g['read'] for g in w):.0f} ms")
            r = next((r for r in reversed(rows) if col(r, "t") <= e["t"]), None)
            if r and "hp" in C and col(r, "hp") >= 0:
                line += (f"; player hp {col(r, 'hp')} at {col(r, 'x')},{col(r, 'y')},{col(r, 'z')} weapon {col(r, 'weapon')} "
                         f"clip {col(r, 'clip')}/{col(r, 'ammo')} enemies alive {col(r, 'alive')}")
        print(line)

print("\ntimeline (10 s): mode, fps, worst ms, game/vsync/gpuWait/gilMain/read ms per frame, MB read, saves, starves, draws")
t0 = frames[0]["t"]
for k in range(0, int(span) + 10, 10):
    w = [f for f in frames if t0 + k * 1000 <= f["t"] < t0 + (k + 10) * 1000]
    if not w:
        continue
    n = len(w)
    a = lambda key: sum(f[key] for f in w) / n
    print(f"  {(t0 / 1000 + k):6.0f} s {mname(w[-1]['mode']):>6} {n / 10:5.1f} fps worst {max(f['dt'] for f in w):5.0f}  "
          f"{a('game'):4.1f}/{a('vsync'):4.1f}/{a('gpuWait'):4.1f}/{a('gilMain'):4.1f}/{a('read'):4.1f}  "
          f"{sum(f['readKb'] for f in w) / 1024:5.1f} MB  {sum(f['saves'] for f in w)}  {sum(f['starves'] for f in w)}  "
          f"draws {sum(f['draws'] for f in w) // n}")
