// Telemetry of a play session (ported from RE-NFSU web/telemetry.js): one row per packet web/gpu.js runs (a Present ends
// a frame), plus events. Every 2 s the new part goes to tools/serve.py (POST /telemetry -> build/telemetry.ndjson; headless
// Chrome writes build/webcheck.telemetry.ndjson). tools/telemetry.py reads it with build/web.log (same clock: ms since the
// page started). Keys (none is a game key: rea/map/input.md; F10 is a game key only with -developerkeys, which the port
// never passes): F8 drops a mark ("this moment"), F7 shows the numbers live over the game, F10 downloads the whole session
// as one .json (rows, events, the page log). ?telemetry=0 turns it off.
'use strict';
// Row columns. Host totals (host.h Telem) go in as deltas since the previous row.
const TELEM_COLS = ['t', 'run', 'present', 'draws', 'passes', 'clears', 'upKb', 'texKb', 'pipes', 'shaders', 'targets',
  'gpuWait', 'vsync', 'gilMain', 'gilOther', 'read', 'idleMain', 'gpuRead', 'save', 'holdMain', 'holdOther',
  'reads', 'readKb', 'gpuReads', 'saves', 'saveKb', 'presents', 'clockCalls',
  'streamPm', 'starves', 'starveBuf', 'audioBehind', 'underrun', 'resyncs', 'peak', 'clips', 'mode', 'pending',
  'hp', 'x', 'y', 'z', 'weapon', 'clip', 'ammo', 'bt', 'alive', 'pflags'];
// host/gamestate.c (ghost slice) snapshot, versions 1-2, u32 words: 1 seq (odd while written), 4 flags (1 level up, 2 player,
// 4 dead, 8 bullet time), 6-8 position (Y up), 21 health, 24 weapon, 25 clip, 26 reserve, 27 bullet time meter, 30 enemies alive.
const GS_SEQ = 1, GS_FLAGS = 4, GS_POS = 6, GS_HP = 21, GS_WEAPON = 24, GS_CLIP = 25, GS_AMMO = 26, GS_BT = 27, GS_ALIVE = 30;

// Game state read from guest memory (MaxPayne.exe 1.05; rea/map/render.md "Telemetry"). mode: the mode the switch runs
// now; 'load' = game mode while its level is not ready (level init 0x44fda0 runs inside the game tick, no flag of its own).
const MODE_NAMES = ['boot', 'menu', 'comic', 'game', 'load', 'other'];
const MS_PTR = 0x8a6400;            // theApp 0x8a6338 + 0xc8: MaxPayne_ModeSwitch* (stored at 0x40b3a2)
const MS_CUR = 0x19, MS_MENU = 0x7c, MS_GAME = 0x80, MS_COMIC = 0x84;   // current mode (0x724b90); mode slots (ctor 0x415dc0)
const GM_PENDING = 0xb4;            // game mode: pending action read by 0x451960 (1 new level, 2 load game, 3/4 save)
const GM_LEVEL = 0xcc;              // std::string (MSVC6): chars at +4, length at +8; set at the end of 0x44fda0, cleared by 0x44fba0
const GM_READY = [0x1074, 0x1080];  // both non-zero once the level is up (0x1080: player wrapper, 0x451e50)

// A short note over the game (a mark, a download, a save or the telemetry refused). Outside startTelemetry: ?telemetry=0
// must not hide a failed save.
const pageNote = (() => {
  let note = null, timer = 0;
  return (text, ms) => {
    if (!note) {
      note = document.createElement('div');
      note.className = 'mp-notice'; note.style.top = '12px'; note.style.bottom = 'auto'; note.style.display = 'none'; note.setAttribute('role', 'status');
      document.body.appendChild(note);
    }
    note.textContent = text; note.style.display = 'block';
    clearTimeout(timer); timer = setTimeout(() => note.style.display = 'none', ms);
  };
})();

function startTelemetry(M, t0, log, logLines) {
  // Only tools/serve.py on this Mac takes POST /telemetry; on a static host (maxpayne.cc, R2) every post answers 401.
  const want = new URLSearchParams(location.search).get('telemetry');
  if (want === '0' || (want !== '1' && !/^(127\.0\.0\.1|localhost)$/.test(location.hostname))) return null;
  const base = M._rn_telem_addr() >>> 0, audio = M._rn_audio_ring() >>> 2;
  const F = () => M.HEAPF64, U = () => M.HEAPU32;
  // host.h Telem: 10 doubles (gpu_wait, gil_main, gil_other, read, idle_main, hold_main_max, hold_other_max, vsync,
  // gpu_read, save), then u32 presents, reads, read_kb, stream_pm, starves, starve_buf, clock_calls, gpu_reads, saves,
  // save_kb, guest_mem.
  const D = base >>> 3, W = (base + 80) >>> 2;
  const host = () => {
    const f = F(), u = U();
    return [f[D], f[D + 1], f[D + 2], f[D + 3], f[D + 4], f[D + 5], f[D + 6], f[D + 7], f[D + 8], f[D + 9],
      u[W], u[W + 1], u[W + 2], u[W + 3], u[W + 4], u[W + 5], u[W + 6], u[W + 7], u[W + 8], u[W + 9]];
  };
  const H_GPU = 0, H_GIL = 1, H_GILO = 2, H_READ = 3, H_IDLE = 4, H_HOLDM = 5, H_HOLDO = 6, H_VSYNC = 7, H_GREAD = 8,
    H_SAVE = 9, H_PRESENTS = 10, H_READS = 11, H_READKB = 12, H_STREAM = 13, H_STARVES = 14, H_STARVEBUF = 15,
    H_CLOCK = 16, H_GREADS = 17, H_SAVES = 18, H_SAVEKB = 19;

  // ---- guest memory (read only) ----
  let dv = null;
  const view = () => (dv && dv.buffer === M.HEAPU8.buffer) ? dv : (dv = new DataView(M.HEAPU8.buffer));
  const gbase = () => U()[W + 10];
  const guestLimit = (M.ENV?.RN_GUEST_MB ? +M.ENV.RN_GUEST_MB * 1048576 : 0x50000000);
  const ok = a => a >= 0x10000 && a < guestLimit;                 // host.h GUEST_MEM on the web
  const g32 = a => { const b = gbase(); return b && ok(a) ? view().getUint32(b + a, true) : 0; };
  const gsAddr = M._rn_gamestate_addr ? M._rn_gamestate_addr() >>> 2 : 0;
  const NONE = [-1, 0, 0, 0, -1, -1, -1, -1, -1, 0];
  // The player's state, or NONE (no struct, no player, or the game thread was writing it: the next row has it).
  function player() {
    if (!gsAddr) return NONE;
    const u = U(), f = new Float32Array(u.buffer), b = gsAddr, seq = Atomics.load(u, b + GS_SEQ);
    if ((seq & 1) || u[b] < 1 || u[b] > 2 || !(u[b + GS_FLAGS] & 2)) return NONE;    // v2 changed words >= 48 only
    const r = [r1(f[b + GS_HP]), r1(f[b + GS_POS]), r1(f[b + GS_POS + 1]), r1(f[b + GS_POS + 2]), u[b + GS_WEAPON] | 0,
      u[b + GS_CLIP] | 0, u[b + GS_AMMO] | 0, Math.round(f[b + GS_BT] * 100) / 100, u[b + GS_ALIVE], u[b + GS_FLAGS]];
    return Atomics.load(u, b + GS_SEQ) === seq ? r : NONE;
  }
  let levelKey = '', level = '', dead = false;
  function game() {
    const ms = g32(MS_PTR);
    if (!ms) return { mode: 0, pending: 0 };
    const cur = g32(ms + MS_CUR), gm = g32(ms + MS_GAME);
    let mode = !cur ? 0 : cur === g32(ms + MS_MENU) ? 1 : cur === g32(ms + MS_COMIC) ? 2 : cur === gm ? 3 : 5;
    if (!gm) return { mode, pending: 0 };
    const p = g32(gm + GM_LEVEL + 4), n = g32(gm + GM_LEVEL + 8);
    // load: the level not up, or the switch fading out of the loading screen ([ms+0x0d] 3, 0x601250; OWNER 2026-11-02:
    // the frames behind the chapter loading screen are load)
    if (mode === 3 && (!n || !g32(gm + GM_READY[0]) || !g32(gm + GM_READY[1]) || (g32(ms + 0x0c) >>> 8 & 0xff) === 3)) mode = 4;
    const key = p + ':' + n;
    if (key !== levelKey) {
      levelKey = key;
      level = p && n && n < 256 ? new TextDecoder().decode(M.HEAPU8.slice(gbase() + p, gbase() + p + n)) : '';
      event('level', { name: level });
    }
    return { mode, pending: g32(gm + GM_PENDING) };
  }

  let prev = host(), prevUnder = 0, prevResync = 0, prevClip = 0, rows = [], events = [];
  // The whole session, for F10. Rows go into packed Float64Array blocks, not one JS array each: 48 boxed numbers per row
  // made the page heap grow ~3 MB a minute (22 -> 110 MB in 30 min) and its major GC stop the page 20-50 ms every
  // ~85 s, a missed beat each (rea/map/hitches.md). The GC does not trace inside a typed array.
  const NC = TELEM_COLS.length, BLOCK = 4096, blocks = [], allEvents = [];
  let nAll = 0;
  const keep = r => { const k = nAll % BLOCK; if (!k) blocks.push(new Float64Array(BLOCK * NC)); blocks[blocks.length - 1].set(r, k * NC); nAll++; };
  const allRows = () => { const out = []; for (let i = 0; i < nAll; i++) { const o = (i % BLOCK) * NC; out.push(Array.from(blocks[i / BLOCK | 0].subarray(o, o + NC))); } return out; };
  const N = 300, live = [];                            // live view: the last N frames, each the sum of its rows
  let acc = null, lastPresent = 0, lastRow = 0, starves = 0, underruns = 0, clips = 0, saves = 0;
  let mode = -1, modeT = 0;
  const ms = () => Math.round(performance.now() - t0);
  const r1 = x => Math.round(x * 10) / 10;
  const push = e => { events.push(e); allEvents.push(e); };
  const event = (kind, data) => push({ t: ms(), kind, ...data });

  function frame(s, t, runMs) {
    const h = host(), u = U();
    const d = k => h[k] - prev[k];
    const under = u[audio + 4], resync = u[audio + 5];
    // [6] peak since the last row (this file clears it), [7] clipped samples, a running count (web/audio.js worklet).
    const peak = Atomics.exchange(u, audio + 6, 0), clipN = Atomics.load(u, audio + 7), clip = clipN - prevClip;
    const gs = game();
    // t at 0.01 ms: whole ms make a 60 fps cadence read as 16, 17, 17.
    const r = [Math.round((t - t0) * 100) / 100, r1(runMs), s.present, s.draws, s.passes, s.clears, s.kb, s.texKb, s.pipes,
      s.shaders, s.targets, r1(d(H_GPU)), r1(d(H_VSYNC)), r1(d(H_GIL)), r1(d(H_GILO)), r1(d(H_READ)), r1(d(H_IDLE)),
      r1(d(H_GREAD)), r1(d(H_SAVE)), r1(h[H_HOLDM]), r1(h[H_HOLDO]), d(H_READS), d(H_READKB), d(H_GREADS), d(H_SAVES),
      d(H_SAVEKB), d(H_PRESENTS), d(H_CLOCK), h[H_STREAM], d(H_STARVES), d(H_STARVES) ? h[H_STARVEBUF] : -1,
      (u[audio] - u[audio + 1]) | 0, under - prevUnder, resync - prevResync, peak, clip, gs.mode, gs.pending, ...player()];
    if (!!(r[r.length - 1] & 4) !== dead) { dead = !dead; if (dead) push({ t: Math.round(t - t0), kind: 'death', level }); }
    // The maxima are since the last row.
    const f = F(); f[D + 5] = f[D + 6] = 0; u[W + 3] = 0;
    rows.push(r); keep(r);
    lastRow = t;
    starves += r[29]; underruns += r[32]; clips += clip;
    if (r[24]) { saves += r[24]; push({ t: Math.round(t - t0), kind: 'save', files: r[24], kb: r[25], ms: r[18] }); }
    if (gs.mode !== mode) {
      push({ t: Math.round(t - t0), kind: 'mode', mode: MODE_NAMES[gs.mode] || gs.mode, from: MODE_NAMES[mode] || mode,
        prevMs: modeT ? Math.round(t - modeT) : 0 });
      mode = gs.mode; modeT = t;
    }
    acc = acc || { gpuWait: 0, vsync: 0, gil: 0, read: 0, other: 0, run: 0, draws: 0, texKb: 0, pipes: 0, readKb: 0, hold: 0 };
    acc.gpuWait += r[11]; acc.vsync += r[12]; acc.gil += r[13]; acc.read += r[15]; acc.other += r[16] + r[17] + r[18];
    acc.run += r[1]; acc.draws += r[3]; acc.texKb += r[7]; acc.pipes += r[8]; acc.readKb += r[22];
    acc.hold = Math.max(acc.hold, r[19], r[20]);
    if (s.present) {
      if (lastPresent) {
        acc.t = t; acc.dt = t - lastPresent; acc.behind = r[31]; acc.peak = peak;
        acc.game = Math.max(0, acc.dt - acc.gpuWait - acc.vsync - acc.gil - acc.read - acc.other);
        live.push(acc);
        if (live.length > N) live.shift();
      }
      lastPresent = t; acc = null;
    }
    prev = h; prevUnder = under; prevResync = resync; prevClip = clipN;
  }
  // No packet for 250 ms (a load, a save, a stall): a row anyway, so the time and the reads land where they happened.
  const idle = { present: 0, draws: 0, passes: 0, clears: 0, kb: 0, texKb: 0, pipes: 0, shaders: 0, targets: 0, reads: 0 };
  setInterval(() => { const t = performance.now(); if (t - lastRow > 250) frame(idle, t, 0); }, 250);

  // The browser's own view of a slow frame: long animation frames (Chrome) with the scripts that ran in them.
  try {
    new PerformanceObserver(list => {
      for (const e of list.getEntries()) if (e.duration >= 50)
        push({ t: Math.round(e.startTime - t0), kind: 'loaf', dur: Math.round(e.duration), block: Math.round(e.blockingDuration || 0),
          scripts: (e.scripts || []).slice(0, 3).map(x => `${x.invoker || x.name} ${Math.round(x.duration)}ms`) });
    }).observe({ type: 'long-animation-frame', buffered: true });
  } catch {}
  // Display pacing: web/gpu.js RN_PACE counts the frames that reached the canvas. Per animation frame: none = the screen
  // showed the previous frame again (a repeat); n > 1 = n - 1 frames never shown. Every 5 s: event 'vsync'.
  let vt = 0, vShown = 0, vDropped = 0, vTicks = 0, vRep = 0, vDrop = 0, vSum = 0, vMax = 0;
  const vtick = now => {
    const P = globalThis.RN_PACE || { shown: 0, dropped: 0 }, n = P.shown - vShown;
    if (vt) {
      const dt = now - vt; vTicks++; vSum += dt; vMax = Math.max(vMax, dt);
      if (n === 0) vRep++; else vDrop += n - 1;
      vDrop += P.dropped - vDropped;
      if (vSum >= 5000) {
        event('vsync', { hz: r1(1000 * vTicks / vSum), ticks: vTicks, rep: vRep, drop: vDrop, maxMs: r1(vMax) });
        vTicks = vRep = vDrop = vSum = vMax = 0;
      }
    }
    vt = now; vShown = P.shown; vDropped = P.dropped;
    requestAnimationFrame(vtick);
  };
  requestAnimationFrame(vtick);
  addEventListener('visibilitychange', () => event('visibility', { state: document.visibilityState }));
  addEventListener('blur', () => event('focus', { state: 'blur' }));
  addEventListener('focus', () => event('focus', { state: 'focus' }));

  const show = pageNote;
  // Our keys never reach the game (window capture runs before web/input.js's canvas listeners).
  const keys = {};
  for (const ev of ['keydown', 'keyup']) addEventListener(ev, e => {
    const fn = keys[e.code];
    if (!fn) return;
    e.preventDefault(); e.stopPropagation();
    if (ev === 'keydown' && !e.repeat) fn();
  }, true);
  let marks = 0;
  keys.F8 = () => { event('mark', { n: ++marks }); log(`telemetry: mark ${marks}`); show(`mark ${marks} saved`, 1500); };

  // F7: the live panel (top right). Stacked bars per frame: game code, waiting for the page (GPU), waiting for the screen
  // (vsync: the game is ahead of 60 Hz), waiting for another guest thread (GIL), file reads. Lines at 16.7 and 33 ms.
  const panel = document.createElement('div');
  Object.assign(panel.style, { position: 'fixed', top: '8px', right: '8px', maxWidth: 'calc(100vw - 32px)', padding: '8px 10px',
    background: 'var(--ink)', color: 'var(--paper2)', font: '12px/1.45 ui-monospace, Menlo, monospace', borderLeft: '3px solid var(--red)',
    pointerEvents: 'none', display: 'none', zIndex: 9, whiteSpace: 'pre' });
  const graph = document.createElement('canvas'), text = document.createElement('div');
  graph.width = 720; graph.height = 160; graph.style.width = 'min(400px, 85vw)'; graph.style.height = '80px';
  panel.append(text, graph);
  document.body.appendChild(panel);
  let shown = false;
  try { shown = localStorage.getItem('mp-live') === '1'; } catch {}
  const PARTS = [['game', '#e9e4d8'], ['gpuWait', '#aaa69f'], ['gil', '#b52a21'], ['read', '#716e69'], ['other', '#484641'], ['vsync', '#242321']];
  function draw() {
    if (!shown || live.length < 2) return;
    const g = graph.getContext('2d'), H = graph.height, sy = H / 50;           // 50 ms tall
    g.clearRect(0, 0, graph.width, H);
    const bw = graph.width / N;
    live.forEach((f, i) => {
      let y = H;
      for (const [k, c] of PARTS) {
        const h = Math.min(f[k], 50) * sy;
        g.fillStyle = c; g.fillRect(i * bw, y - h, Math.max(1, bw - .5), h); y -= h;
      }
      if (f.dt > 50) { g.fillStyle = '#b52a21'; g.fillRect(i * bw, 0, Math.max(2, bw), 6); }
    });
    g.fillStyle = 'rgba(255,255,255,.35)';
    g.fillRect(0, H - 16.7 * sy, graph.width, 1); g.fillRect(0, H - 33.3 * sy, graph.width, 1);
    const last = live.slice(-60), recent = live.slice(-300);
    const span = (last[last.length - 1].t - last[0].t) / 1000 || 1;
    const avg = k => last.reduce((a, f) => a + f[k], 0) / last.length;
    const sum = k => recent.reduce((x, f) => x + f[k], 0);
    const rspan = (recent[recent.length - 1].t - recent[0].t) / 1000 || 1;
    const f1 = x => x.toFixed(1).padStart(5);
    text.textContent =
      `${((last.length - 1) / span).toFixed(0)} fps   frame ${f1(avg('dt'))} ms   worst ${Math.max(...recent.map(f => f.dt)).toFixed(0)} ms   ` +
      `hitches ${recent.filter(f => f.dt > 50).length}\n` +
      `game ${f1(avg('game'))}  gpu ${f1(avg('gpuWait'))}  vsync ${f1(avg('vsync'))}  threads ${f1(avg('gil'))}  disk ${f1(avg('read'))} ms\n` +
      `${MODE_NAMES[mode] || mode} ${level}   draws ${Math.round(avg('draws'))}   textures ${(sum('texKb') / 1024 / rspan).toFixed(1)} MB/s   pipelines ${sum('pipes')}\n` +
      `disk ${(sum('readKb') / 1024 / rspan).toFixed(1)} MB/s   GIL max ${Math.max(...recent.map(f => f.hold)).toFixed(0)} ms   page ${f1(avg('run'))} ms\n` +
      `audio ${(last[last.length - 1].behind / 44.1).toFixed(0)} ms   peak ${Math.max(...recent.map(f => f.peak))}   underruns ${underruns}   ` +
      `clips ${clips}   starves ${starves}   saves ${saves}   marks ${marks}`;
  }
  setInterval(draw, 250);
  keys.F7 = () => {
    shown = !shown; panel.style.display = shown ? 'block' : 'none';
    try { localStorage.setItem('mp-live', shown ? '1' : '0'); } catch {}
  };
  if (shown) panel.style.display = 'block';
  setInterval(() => event('heap', { mb: performance.memory ? Math.round(performance.memory.usedJSHeapSize / 1048576) : -1 }), 5000);

  const head = { kind: 'session', url: location.href, date: new Date().toISOString(), ua: navigator.userAgent,
    screen: `${innerWidth}x${innerHeight} dpr ${devicePixelRatio}`, cols: TELEM_COLS, modes: MODE_NAMES };
  fetch('/telemetry?new', { method: 'POST', body: JSON.stringify(head) + '\n' }).then(r => {
    if (r.ok) return;
    log(`telemetry: the server answered ${r.status}: restart tools/serve.py`);
    show('telemetry not saved: restart tools/serve.py', 8000);
  }).catch(() => {});
  function flush() {
    if (!rows.length && !events.length) return;
    let body = rows.length ? JSON.stringify({ rows }) + '\n' : '';
    for (const e of events) body += JSON.stringify(e) + '\n';
    rows = []; events = [];
    fetch('/telemetry', { method: 'POST', body, keepalive: body.length < 60000 }).catch(() => {});
  }
  setInterval(flush, 2000);
  addEventListener('pagehide', flush);
  // F10: the session as one file the browser saves (Downloads): head, cols, rows, events, log.
  keys.F10 = () => {
    event('download', {});
    const name = `maxpayne-session-${head.date.replace(/[:.]/g, '-')}.json`;
    const blob = new Blob([JSON.stringify({ format: 'mp-session-1', ...head, saved: ms(), rows: allRows(), events: allEvents,
      log: logLines || [] })], { type: 'application/json' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob); a.download = name; a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 10000);
    log(`telemetry: session saved as ${name} (${nAll} rows)`);
    show(`session downloaded: ${name}`, 3000);
  };
  log('telemetry: on (build/telemetry.ndjson), F8 = mark, F7 = live panel, F10 = download the session');
  return { packet: frame, event };
}
