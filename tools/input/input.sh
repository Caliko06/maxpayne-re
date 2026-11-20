#!/bin/bash
# Input parity, one scenario, both builds: does the same injected mouse counts + keys give the same turn (yaw/pitch) and
# the same movement? Fixed injected point: part1_level1, player->C_Teleport(::p5::w1). Steps: mouse x, mouse y, forward
# hold, strafe, shoot-dodge. Marks (host/jump.c '@m:') bracket every step, so the compare can read one clean state per side.
#   native: RN_MOUSE=ms:dx:dy:click (host/input.c:186) fires the counts at wall-clock ms from process start.
#   web:    tools/input/web.mjs sends a mousemove when it sees the bracket's first MARK; the page adds the counts to ring
#           word [2] (web/input.js:88) and host/input.c:159 hands them to dinput8_motion, the same call RN_MOUSE makes.
# RN_MOUSE is time based and the '@' waits are real time from the moment the level hands control over, so pass 1 measures
# that moment (MARK mx0) and pass 2 schedules the two mouse steps in the middle of their 3 s brackets.
# The two builds run at different frame rates (native ~300 fps without drawing, Chrome ~60), and a movement step measured
# from press to release carries a start transient that resolves over ~0.2 s: that window is fps dependent and is NOT a parity
# quantity. So every move is marked twice INSIDE its own hold (fwd1/fwd2, str1/str2) and the compare checks the SPEED over
# that steady window: displacement / time, both taken from the ghost dumps between the two marks.
# Why speed and not the raw displacement between two marks (run3, s1104 U): the marks are logged lines, the ghosts land on
# every 10th guest frame, and a sample is only "the first dump after the mark", so the two marks pick up to ~9 frames of slop
# each side. At 60 fps that slop is 0.15 s and the same hold reads 0.64 s on web against 0.51 s on native: the raw distance
# was 3.50 m vs 2.73 m although the strafe ran at 5.50 vs 5.27 m/s. Dividing each side by its own window time removes it.
# Time per frame comes from the 'd3d8 Present <frame>: ... at <s> s' lines (both builds print them), interpolated linearly.
# Settled positions and the wall-clamped shoot-dodge are printed as info.
# Usage: tools/input/input.sh            (PORT=5192 GAME=~/Games/maxpayne NGAME=~/Games/ghost-native/game MX=150 MY=-80)
set -u
cd "$(dirname "$0")/../.." || exit 1
PORT=${PORT:-5192}
GAME=${GAME:-$HOME/Games/maxpayne}
NGAME=${NGAME:-$HOME/Games/ghost-native/game}
MX=${MX:-150}
MY=${MY:--80}
OUT=out/input
mkdir -p "$OUT" build
[ "$PORT" = 5181 ] && { echo "input.sh: refusing port 5181 (the OWNER's saves); pick a test port"; exit 1; }
[ -x ./build/maxpayne ] || { echo "input.sh: no ./build/maxpayne (python tools/recomp/build.py native first)"; exit 1; }
[ -d "$NGAME" ] || { echo "input.sh: no native game dir $NGAME"; exit 1; }
[ -d "$GAME/data/database/levels" ] || { echo "input.sh: $GAME does not look like the game install"; exit 1; }
[ -f web/input.js ] || { echo "input.sh: no web/input.js (python tools/recomp/build.py web first)"; exit 1; }

# The scenario. '@' waits count 1/60 s of real time (host/jump.c:25-29). Brackets: 3 s for each mouse step (the body yaw has
# a 90 deg/s turn limit, 0.3594 deg/count x 150 = 54 deg -> 0.6 s), then the two holds, marked twice inside so the compare
# reads a steady window (fwd1->fwd2, str1->str2, both 0.5-1.0 s into a hold that is still down) and the settled positions
# (fwd3, str3). The shoot-dodge is measured on its launch (dodge0->dodge1, 0.33 s, ~0.5 m, before it reaches the wall at
# z -5.41) and `end` is state only: a dodge that ends against the wall is a collision slide, not a parity quantity.
J='maxpayne_gamemode->gm_init(part1_level1);|x_modeswitch->s_modeswitch(game);|player->c_setinvulnerable(true);|player->C_Teleport(::p5::w1);|@m:p0|@w:60|@m:mx0|@w:180|@m:mx1|@w:30|@m:my0|@w:180|@m:my1|@w:30|@d:W|@w:30|@m:fwd1|@w:30|@m:fwd2|@w:60|@u:W|@w:60|@m:fwd3|@d:A|@w:30|@m:str1|@w:30|@m:str2|@w:15|@u:A|@w:60|@m:str3|@w:30|@d:W|@m:dodge0|@k:LSHIFT:10|@w:20|@m:dodge1|@w:90|@u:W|@w:120|@m:end'

# --- the web server for this test port (tools/serve.py; nothing answers on 5192 normally) ---
SERVER=""
NPID=""
cleanup() { [ -n "$NPID" ] && kill "$NPID" 2>/dev/null; [ -n "$SERVER" ] && kill "$SERVER" 2>/dev/null; }
trap cleanup EXIT
http_ok() { [ "$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/" 2>/dev/null)" = 200 ]; }
if ! http_ok; then
  rm -rf "$HOME/Games/save-$PORT"                                  # a test port never reads or writes a real save
  python3 tools/serve.py "$GAME" "$PORT" > "$OUT/serve.log" 2>&1 &
  SERVER=$!
  for i in $(seq 40); do http_ok && break; sleep 0.5; done
  http_ok || { echo "input.sh: the web server did not answer on :$PORT (see $OUT/serve.log)"; exit 1; }
fi

start_native() {                                                    # $1 log, $2 RN_SECS, $3 RN_MOUSE
  RN_NODRAW=1 RN_GHOST=10 RN_SECS=$2 RN_MOUSE="$3" RN_JUMP="$J" ./build/maxpayne "$NGAME" > "$1" 2>&1 &
  NPID=$!
}
wait_mark() {                                                       # $1 log, $2 mark, $3 polls of 250 ms -> T = ms
  local n=0
  while [ "$n" -lt "$3" ]; do
    if grep -q "^MARK $2\$" "$1" 2>/dev/null; then T=$((n * 250)); return 0; fi
    sleep 0.25
    n=$((n + 1))
  done
  return 1
}

# --- pass 1: native, no mouse, to time the injected point (MARK mx0 = control just handed over) ---
echo "native anchor pass (no mouse)..."
start_native "$OUT/native_anchor.log" 240 ""
if wait_mark "$OUT/native_anchor.log" mx0 480; then
  T0=$T
else
  echo "FAIL: native never reached MARK mx0 (see $OUT/native_anchor.log)"; kill "$NPID" 2>/dev/null; exit 1
fi
kill "$NPID" 2>/dev/null; wait "$NPID" 2>/dev/null; NPID=""
echo "MARK mx0 at ${T0} ms; mx bracket 0-3000 ms, my bracket 3500-6500 ms"

# --- pass 2: native with the counts in the middle of each bracket ---
MS="$((T0 + 1500)):$MX:0:0,$((T0 + 5000)):0:$MY:0"
echo "native run, RN_MOUSE=$MS"
start_native "$OUT/native.log" 240 "$MS"
wait_mark "$OUT/native.log" end 400 || echo "warn: native did not reach MARK end"
sleep 1
kill "$NPID" 2>/dev/null; wait "$NPID" 2>/dev/null; NPID=""

# --- web: ?mouse=1 cancels web/input.js's x0.4 so movementX reaches the game as counts ---
Q=$(python3 -c "import urllib.parse,sys;print(urllib.parse.quote('RN_JUMP='+sys.argv[1]+',RN_GHOST=10',safe=''))" "$J")
echo "web run on :$PORT ..."
node tools/input/web.mjs "http://127.0.0.1:$PORT/?env=$Q&mouse=1" 240 "$OUT/web.png" "mx0:$MX:0,my0:0:$MY" > "$OUT/web.log" 2>&1

# --- compare: one row per step, yaw / pitch / position / state per side ---
MX="$MX" MY="$MY" python3 - "$OUT/web.log" "$OUT/native.log" <<'PY'
import math, os, re, sys

MX, MY = int(os.environ['MX']), int(os.environ['MY'])
YAW_TOL, PITCH_TOL, POS_TOL = 2.0, 1.0, 0.40          # deg, deg, m
HOLD = 0.5                                             # s: the steady window both marks bracket
SPEED_TOL = POS_TOL / HOLD                             # m/s: the old 0.40 m over a 0.5 s hold
DEG_PER_COUNT = 0.3594                                 # 0.0025*pi*(0.5*1.4+0.1) rad/count, rea/map/look_move.md 1
# kind: abs   one mark, absolute position (both sides must stand on the injected point)
#       yaw   the turn between the two marks
#       pitch aim pitch at the second mark, in degrees (web: word 344; native: the " pitch" field of the 'ghost   alt' line)
#       move  speed (m/s) over the ghost dumps between the two marks: displacement / time, a steady window inside the hold
#       state state/an exact; position printed as info (wall-clamped steps, no parity value)
#       info  printed, not judged
STEPS = [
    ('p0',     None,    'abs',   'injected point'),
    ('mx1',    'mx0',   'yaw',   'mouse x %+d counts' % MX),
    ('my1',    'my0',   'pitch', 'mouse y %+d counts' % MY),
    ('fwd2',   'fwd1',  'move',  'forward 0.5 s (steady)'),
    ('str2',   'str1',  'move',  'strafe A 0.5 s (steady)'),
    ('dodge1', 'dodge0', 'move', 'shoot-dodge launch 0.33 s'),
    ('fwd3',   None,    'info',  'forward settled (2 s hold)'),
    ('str3',   None,    'info',  'strafe settled (1 s hold)'),
    ('end',    None,    'state', 'shoot-dodge + settle (wall-clamped)'),
]
NATIVE_PITCH_HELP = ('native pitch: the RN_GHOST "ghost   alt" line has no " pitch" field here (a native build before '
                     '91b178a); rebuild it (python tools/recomp/build.py native). Until then pitch parity is UNPROVEN.')

def parse(path):
    if not os.path.exists(path):
        return None
    d = {'mark': {}, 's': [], 'pitch': {}, 'mouse': {}, 'pres': []}
    cur = None
    for i, line in enumerate(open(path, errors='replace')):
        if line.startswith('MARK '):
            d['mark'].setdefault(line[5:].strip(), i)
        elif line.startswith('ghost f'):
            m = re.match(r'ghost f(\d+) ', line)
            p = re.search(r'pos (-?[\d.]+) (-?[\d.]+) (-?[\d.]+) fwd (-?[\d.]+) (-?[\d.]+) (-?[\d.]+)', line)
            cur = None
            if m and p:
                v = [float(x) for x in p.groups()]
                cur = {'i': i, 'f': int(m.group(1)), 'pos': v[:3], 'fwd': v[3:], 'st': None, 'an': None, 'pitch': None}
                d['s'].append(cur)
        elif line.startswith('ghost   alt') and cur is not None:
            m = re.search(r'st (-?\d+) an (-?\d+)', line)
            if m:
                cur['st'], cur['an'] = int(m.group(1)), int(m.group(2))
            p = re.search(r' pitch (-?[\d.]+)', line)      # degrees, host/gamestate.c "ghost   alt" (commit 91b178a)
            if p:
                cur['pitch'] = float(p.group(1))
        elif line.startswith('WMARK '):
            p = line.split()
            if len(p) > 2 and p[2] != 'n/a':
                d['pitch'].setdefault(p[1], float(p[2]))
        elif line.startswith('WMOUSE '):
            p = line.split()
            if len(p) > 2:
                d['mouse'].setdefault(p[1], ' '.join(p[2:]))
        else:
            m = re.match(r'd3d8 Present (\d+): \d+x\d+ at ([\d.]+) s', line)
            if m:
                d['pres'].append((int(m.group(1)), float(m.group(2))))
    return d

def fps(d):
    seg = [(b[0] - a[0]) / (b[1] - a[1]) for a, b in zip(d['pres'], d['pres'][1:]) if b[1] > a[1]]
    return sorted(seg)[len(seg) // 2] if seg else None

def at(d, mark):
    i = d['mark'].get(mark)
    if i is None:
        return None
    for s in d['s']:
        if s['i'] > i and s['st'] is not None:
            return s
    return None

def yaw(s):
    return math.degrees(math.atan2(s['fwd'][0], s['fwd'][2]))

def ftime(d):
    """guest frame -> seconds, from the Present lines (one every 100 frames, 'at %.1f s'); linear in between,
    the first/last segment's slope outside. Both builds print them, so it is the one clock both logs share."""
    P = d['pres']
    if len(P) < 2:
        return None
    def t(f):
        for a, b in zip(P, P[1:]):
            if a[0] <= f <= b[0] and b[0] > a[0]:
                return a[1] + (b[1] - a[1]) * (f - a[0]) / (b[0] - a[0])
        a, b = (P[0], P[1]) if f < P[0][0] else (P[-2], P[-1])
        return a[1] + (b[1] - a[1]) * (f - a[0]) / (b[0] - a[0]) if b[0] > a[0] else a[1]
    return t

def speed(d, t, base, mark):
    """mean speed over every ghost dump between the two marks: total displacement / total time, plus the speed over the
    first dumps AFTER the window (a window that is still accelerating cannot be compared: it would read low on the side
    whose press landed later). Returns (v, n_dumps, window_s, v_after) or None."""
    i0, i1 = d['mark'].get(base), d['mark'].get(mark)
    if i0 is None or i1 is None:
        return None
    ss = [s for s in d['s'] if i0 < s['i'] < i1]
    dist = sum(math.dist(a['pos'], b['pos']) for a, b in zip(ss, ss[1:]))
    dt = sum(t(b['f']) - t(a['f']) for a, b in zip(ss, ss[1:]))
    if len(ss) < 3 or dt <= 0:
        return None
    tail, acc = [], 0.0
    after = [s for s in d['s'] if s['i'] > i1][:4]          # at most 4 dumps ~0.35 s of native, 0.67 s of web
    rows = [(t(b['f']) - t(a['f']), math.dist(a['pos'], b['pos'])) for a, b in zip(after, after[1:])]
    for ddt, ddist in rows:
        if acc >= 0.35:
            break
        tail.append((ddt, ddist))
        acc += ddt
    va = sum(x[1] for x in tail) / sum(x[0] for x in tail) if tail and sum(x[0] for x in tail) > 0 else None
    return dist / dt, len(ss), dt, va

web, nat = parse(sys.argv[1]), parse(sys.argv[2])
for name, d in (('web', web), ('native', nat)):
    if d is None:
        print('FAIL: no %s log (%s)' % (name, sys.argv[1] if name == 'web' else sys.argv[2]))
        sys.exit(1)

print()
print('tolerance: yaw %.1f deg, pitch %.1f deg, speed %.2f m/s (%.2f m over a %.1f s hold), state exact'
      % (YAW_TOL, PITCH_TOL, SPEED_TOL, POS_TOL, HOLD))
fw, fn = fps(web), fps(nat)
print('frame rate (median of the Present lines): web ~%s fps, native ~%s fps'
      % ('%.0f' % fw if fw else '?', '%.0f' % fn if fn else '?'))
if web['mouse']:
    print('web mouse: ' + ', '.join('%s %s' % kv for kv in sorted(web['mouse'].items())))
print()
print('%-8s %-13s %-13s %-9s %-13s %-7s %-7s %-13s %-10s %s'
      % ('step', 'yaw web/nat', 'dyaw web/nat', 'exp', 'm/s web/nat', 'drift', 'spread*', 'pitch web/nat', 'st/an w/n', 'verdict'))
print('-' * 132)

bw, bn = at(web, 'p0'), at(nat, 'p0')
if not bw or not bn:
    print('FAIL: no sample right after MARK p0 (web %s, native %s)' % (bool(bw), bool(bn)))
    sys.exit(1)
tw, tn = ftime(web), ftime(nat)
if not tw or not tn:
    print('FAIL: a log has fewer than two d3d8 Present lines, so frame -> time is unknown and a move cannot be a speed')
    sys.exit(1)
yw0, yn0 = yaw(bw), yaw(bn)
fails, notes, info, windows = 0, [], 0, []
for mark, base, kind, what in STEPS:
    sw, sn = at(web, mark), at(nat, mark)
    if not sw or not sn:
        if kind == 'info':
            print('%-8s %-12s %-12s %-8s -     missing sample (%s)' % (mark, '-', '-', '-', what))
            info += 1
        else:
            print('%-8s %-12s %-12s %-8s FAIL  missing sample (%s)' % (mark, '-', '-', '-', what))
            fails += 1
        continue

    yw, yn = yaw(sw), yaw(sn)
    dw, dn = yw - yw0, yn - yn0
    pw = web['pitch'].get(mark, sw['pitch'])
    pn = nat['pitch'].get(mark, sn['pitch'])
    exp, move, drift, spread, bad = '', '-', '-', '%.2f' % math.dist(sw['pos'], sn['pos']), []

    if kind == 'move':
        hw, hn = speed(web, tw, base, mark), speed(nat, tn, base, mark)
        if not hw or not hn:
            print('%-8s %-13s %-13s %-9s FAIL  missing steady samples (%s)' % (mark, '-', '-', '-', what))
            fails += 1
            continue
        drift = '%+.2f' % (hw[0] - hn[0])
        move = '%.2f/%.2f' % (hw[0], hn[0])
        windows.append('%s %.2f/%.2f s' % (mark, hw[2], hn[2]))
        if abs(hw[0] - hn[0]) > SPEED_TOL:
            # A window still accelerating reads low, and the press lands at a different frame on each side, so a
            # disagreement is only a divergence when both windows had settled (>= 90% of the speed right after them).
            ramp = [n for n, h in (('web', hw), ('native', hn)) if h[3] and h[0] < 0.9 * h[3]]
            if ramp:
                kind = 'info'
                notes.append('%s: window still accelerating on %s (%.2f vs %.2f m/s after it), so the %.2f vs %.2f m/s '
                             'gap is not a divergence; move the marks later' % (mark, ' and '.join(ramp), hw[0] if 'web' in ramp
                             else hn[0], hw[3] if 'web' in ramp else hn[3], hw[0], hn[0]))
            else:
                bad.append('speed %.2f vs %.2f m/s' % (hw[0], hn[0]))
    if kind == 'abs' and math.dist(sw['pos'], sn['pos']) > POS_TOL:
        bad.append('pos %.2f m' % math.dist(sw['pos'], sn['pos']))
    if kind == 'yaw':
        exp = '%+.1f' % (MX * DEG_PER_COUNT)
        if abs(dw - dn) > YAW_TOL:
            bad.append('yaw %.1f vs %.1f' % (dw, dn))
    if kind == 'pitch':
        ep = MY * DEG_PER_COUNT
        exp = 'pitch %+.1f' % ep
        if pw is None:
            bad.append('web pitch missing')
        elif abs(pw - ep) > PITCH_TOL:
            bad.append('web pitch %.2f vs %+.2f exp' % (pw, ep))
        if pw is not None and pn is None:
            bad.append('pitch unproven')
        elif pw is not None and abs(pw - pn) > PITCH_TOL:
            bad.append('pitch %.2f vs %.2f' % (pw, pn))
    if kind != 'info' and (sw['st'], sw['an']) != (sn['st'], sn['an']):
        bad.append('state')

    if kind == 'info':
        verdict, info = '-', info + 1
    else:
        verdict = 'PASS' if not bad else 'FAIL  ' + ', '.join(bad)
        if bad:
            fails += 1
    if kind == 'pitch' and pn is None:
        notes.append(mark + ': ' + NATIVE_PITCH_HELP)
    print('%-8s %-13s %-13s %-9s %-13s %-7s %-7s %-13s %-10s %s'
          % (mark, '%7.1f/%7.1f' % (yw, yn), '%+7.1f/%+7.1f' % (dw, dn), exp, move, drift, spread,
             '%s/%s' % ('%7.2f' % pw if pw is not None else '    n/a', '%7.2f' % pn if pn is not None else '    n/a'),
             '%d/%d|%d/%d' % (sw['st'], sw['an'], sn['st'], sn['an']), verdict))
print('-' * 132)
checked = len([s for s in STEPS if s[2] != 'info'])
print('%d/%d steps PASS, %d info' % (checked - fails, checked, info))
for n in dict.fromkeys(notes):
    print('note: ' + n)
if windows:
    print('note: speed rows, window web/native: ' + ', '.join(windows) +
          ' (same hold, different dump alignment: the reason a raw two-mark distance is not comparable)')
print('note: m/s rows are displacement / time over the ghost dumps between the two marks (steady window), each side divided')
print('      by its own window, not the raw two-mark distance. A row whose window is still accelerating (below 90% of the')
print('      speed right after it) is printed and not judged: the press lands at a different frame on each side.')
print('      Tolerance %.2f m/s = the old %.2f m over a %.1f s hold.' % (SPEED_TOL, POS_TOL, HOLD))
print('      spread* is a check only on the abs row (p0); fwd3 and end are wall-clamped at z -5.41 and carry no parity value.')
print('logs: %s (web), %s (native), %s (native anchor), %s' % (sys.argv[1], sys.argv[2],
      sys.argv[2].replace('native.log', 'native_anchor.log'), sys.argv[1].replace('web.log', 'web.png')))
PY