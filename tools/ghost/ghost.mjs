// Ghost driver: plays Max Payne in headless Chrome (the same launch as tools/webgate.mjs: headless shell, muted, no window).
// Each tick it reads the game state that host/gamestate.c publishes (rn_gamestate_addr, layout in that file) and writes
// inputs into the ring web/input.js uses (host/input.c: DIK key events, mouse counts, mouse buttons).
// Usage: node tools/ghost/ghost.mjs [--port 5185] [--secs 600] [--out build/ghost] [--plan newgame|watch] [--cpuprofile 1]
// --cpuprofile 1: a V8 CPU profile of every worker (the guest threads) and the page from boot to the end -> <out>/worker-N.cpuprofile, page.cpuprofile
// --camera-frames N: watch mode records sniper shots and camera state every N game frames (at most 180 shots).
// (open in Chrome DevTools; guest names come from the wasm name section, build.py --profiling-funcs).
// It starts tools/serve.py on the port when nothing answers there (and stops it at the end), and writes
// <out>/report.json, <out>/state.tsv (one row per tick), <out>/page.log and shots at each phase change.
import { createRequire } from 'node:module';
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { buildGraph, nearest, path as navPath, startsWithDrop } from './nav.mjs';
import { fileURLToPath } from 'node:url';
const require = createRequire(import.meta.url);
const { chromium } = require('playwright');

const opt = { port: 5185, secs: 600, out: 'build/ghost', plan: 'newgame', game: path.join(os.homedir(), 'Games/maxpayne') };
for (let i = 2; i < process.argv.length; i += 2) opt[process.argv[i].replace(/^--/, '')] = process.argv[i + 1];
opt.port = +opt.port; opt.secs = +opt.secs; opt.rescue = +opt.rescue || 0;
if (opt.pre !== undefined && !(Number.isInteger(+opt.pre) && +opt.pre > 0)) { console.error(`--pre must be an integer > 0 (got '${opt.pre}')`); process.exit(2); }
// A proof run (--plan play without --pre) must start from a clean save dir: a death loads the last save of the port, and a
// save left by a --pre / route / earlier run would carry its position and cheats into this one (review-ghost-7 A2).
{ const dir = path.join(path.dirname(opt.game), `save-${opt.port}`), n = fs.existsSync(dir) ? fs.readdirSync(dir, { recursive: true }).filter(f => /\.mps$/i.test(String(f))) : [];
  if (opt.plan === 'play' && !opt.pre && n.length) { console.error(`REFUSED: ${dir} holds ${n.length} save(s) (${n.slice(0, 3).join(', ')}). A proof run starts with no saves: empty that folder, or use another --port.`); process.exit(2); } }
// --selfcheck <file>: the two row parsers offline, on a saved telemetry session (no browser, no game run):
//   node tools/ghost/ghost.mjs --selfcheck build/webcheck.telemetry.ndjson
if (opt.selfcheck) {
  const tel = parseTelemetry(opt.selfcheck), ft = tel.frames.map(f => f.t), n = ft.length, bad = [];
  console.log(`selfcheck ${opt.selfcheck}: ${n} presented frames, game column ${tel.game}, ${tel.landed.length} landings`);
  // The same session under two clock offsets: c = page ms - ghost ms (fixed) and page ms - ghost ms * 1000 (the old line,
  // 68 s of run -> c -68e6 ms). t0/t1 = row.t*1000 + c, so the old offset puts the map's window nowhere near its frames.
  const row = { level: 'selfcheck', t: +(ft[0] / 1000).toFixed(3), secs: +((ft[n - 1] - ft[0]) / 1000).toFixed(3) };
  const cOld = ft[n - 1] - ft[n - 1] * 1000, fixed = steadyRow(row, tel, 0);
  console.log(`  c=0: ${fixed} frames -> ${row.steady} s, ${row.fps} fps, p99 ${row.p99} ms, ${row.over33} over 33`);
  if (!(fixed > 1000 && row.fps >= 59)) bad.push(`fixed clock found ${fixed} frames (${row.fps} fps)`);
  const old = steadyRow(row, tel, cOld);
  console.log(`  c=${cOld} (old line): ${old} frames -> ${row.steady} s, ${row.fps} fps`);
  if (old) bad.push(`the old clock found ${old} frames, expected 0`);
  // Exit from the game state: a gm_init asked level, another level shown, the menu, or the last map's own credits.
  for (const [name, seen, want] of [
    ['train DO exit (next asked)', [{ level: 'part1_level3b' }, { level: 'part1_level3b', next: 'part1_level4' }], 'part1_level4'],
    ['level switch', [{ level: 'part1_level1' }, { level: 'part1_level1b', next: 'part1_level1b' }], 'part1_level1b'],
    ['ending menu', [{ level: 'part3_level7' }, { level: '', mode: 1 }], 'end'],
    ['last map credits', [{ level: 'part3_level7', mode: 3 }], 'end'],
    ['no transition', [{ level: 'part1_level3b', mode: 3 }], ''],
  ]) {
    const r = exitOf(seen, seen[0].level, { last: seen[0].level === 'part3_level7' });
    console.log(`  exit ${name}: '${r.exit}' (${r.from || 'none'})`);
    if (r.exit !== want) bad.push(`exit ${name} gave '${r.exit}', expected '${want}'`);
  }
  if (bad.length) { for (const b of bad) console.error('  FAIL ' + b); process.exit(1); }
  console.log('selfcheck: ok'); process.exit(0);
}
fs.mkdirSync(opt.out, { recursive: true });
// --env RN_X=1,RN_Y: guest env (web/index.html); --compat 1: WebGPU compatibility rules (web/gpu.js).
// RN_SOUNDLOG rides along unless --env sets it: the sound accounting of host/dsound.c is the lost-sound column's source.
const env = [opt.env, process.env.RN_PAGE_ENV, /RN_SOUNDLOG/.test(opt.env || '') ? '' : 'RN_SOUNDLOG=1'].filter(Boolean).join(',');   // RN_PAGE_ENV: the same, from tools/fleet.sh
const query = [env && "env=" + env, opt.compat && "compat", process.env.RN_PAGE_QUERY].filter(Boolean).join("&");   // RN_PAGE_QUERY: e.g. profile=low
const url = `http://127.0.0.1:${opt.port}/${query ? "?" + query : ""}`;
const logf = fs.openSync(path.join(opt.out, 'page.log'), 'w');
const say = s => { const l = `${((Date.now() - T0) / 1000).toFixed(1)} ${s}`; console.log(l); fs.writeSync(logf, l + '\n'); };
const T0 = Date.now();

// Serve the build unless something already answers on the port (kill only what we started).
let server = null;
if (!await fetch(url).then(r => r.ok, () => false)) {
  server = spawn('python3', ['tools/serve.py', opt.game, String(opt.port)], { stdio: 'ignore' });
  for (const sig of ['SIGTERM', 'SIGINT']) process.on(sig, () => { server.kill(); process.exit(1); });   // a stopped run leaves no server
  for (let k = 0; k < 50 && !await fetch(url).then(r => r.ok, () => false); k++) await new Promise(r => setTimeout(r, 200));
}

const mac = process.platform === 'darwin';
const args = (process.env.RN_GPU_ARGS || (mac ? '--use-angle=metal --ignore-gpu-blocklist --enable-unsafe-webgpu'
  // Linux Pod (RunPod RTX 2000 Ada, 2026-11-02): without --use-vulkan=native and VulkanFromANGLE vkCreateInstance fails (-7)
  // and WebGPU gets SwiftShader; with them the adapter is nvidia/lovelace.
  : '--use-angle=vulkan --enable-features=Vulkan,VulkanFromANGLE,DefaultANGLEVulkan --use-vulkan=native --ignore-gpu-blocklist --enable-unsafe-webgpu')).split(' ');
const lean = ['--mute-audio', '--no-first-run', '--disable-extensions', '--disable-sync', '--disable-default-apps',
  '--disable-component-update', '--disable-background-networking', '--disable-features=Translate,MediaRouter,OptimizationHints'];
if (opt.cpuprofile) lean.push('--remote-debugging-port=0');   // CDP on a socket: the worker targets (cpuProfile below)
// --view WxH@dpr: the page size; 1920x871@2 is the OWNER's window (RE-NFSU tools/bench.py VIEW). Default 1280x900@1.
const VIEW = (([w, h, d]) => ({ viewport: { width: +w, height: +h }, deviceScaleFactor: +d || 1 }))(
  (opt.view || `${opt.plan === 'final' || +opt['camera-frames'] > 0 ? 1000 : 1280}x900@1`).split(/[x@]/));
const browser = await chromium.launchPersistentContext(opt.profile || `build/chrome-gate-${opt.port}`,   // --profile: a fresh Chrome profile (a cached half-downloaded wasm from a rebuild is revalidated by ETag as good)
   { headless: true, args: [...args, ...lean],
  executablePath: process.env.RN_CHROME || undefined, ...VIEW });
const page = browser.pages()[0] || await browser.newPage();
let stop = '';
// Sound accounting (host/dsound.c "Sound accounting", printed by RN_SOUNDLOG every 3 s): Play calls the game made and
// how many of them came out silent, split by cause. `lost` is only what the HOST dropped: a Play the host itself ended
// before the mixer ran (not mixed), a stream the host held because the game did not refill, a buffer the guest never
// wrote (never filled). Out of it, per I_result.md §3-4: mixed zero is the game's own padded 0x80 silence (parity),
// under 1 LSB is below one LSB of a 16-bit output, DS3D max distance is the API's own mute at the game's own max
// distance, and a cancel is the game's own release inside the first pass (dsound cancelled). A COM error or a ring drop
// is a host fault too, but not a Play: both gate green in `drops`. The counters are cumulative, so a map's numbers are
// the difference across its window.
const snd = { host: [0, 0, 0, 0, 0, 0], game: 0, cancelled: 0, ring: [0, 0, 0, 0], com: 0 };   // host: not mixed, held, DS3D max distance, mixed zero, under 1 LSB, never filled
const LOST = [0, 1, 5];             // the host counters that are a lost Play: not mixed, held, never filled
const RE_SND = /^dsound report \d+ s: Play calls \d+ \(started \d+, audible \d+, silent \d+; game: volume off (\d+); host: not mixed (\d+), held (\d+), DS3D max distance (\d+), mixed zero (\d+), under 1 LSB (\d+), never filled (\d+)\)/;
const RE_CANCELLED = /^dsound cancelled: (\d+)/;
const RE_RING = /^dsound ring: mixed \d+ taken \d+, silence (\d+) frames, resyncs (\d+) \(skipped \d+ frames\), overwritten before taken (\d+), clipped (\d+)/;
const RE_COM = /^dsound COM errors: (?!none)(.+)$/;
function sndLine(s) {
  let m;
  if ((m = RE_SND.exec(s))) { snd.game = +m[1]; for (let k = 0; k < 6; k++) snd.host[k] = +m[2 + k]; }
  else if ((m = RE_CANCELLED.exec(s))) snd.cancelled = +m[1];
  else if ((m = RE_RING.exec(s))) for (let k = 0; k < 4; k++) snd.ring[k] = +m[1 + k];
  else if ((m = RE_COM.exec(s))) { let n = 0; for (const c of m[1].matchAll(/\d+ of \d+/g)) n += +c[0].split(' ')[0]; snd.com = n; }
}
// host/dsound.c rn_sound_report prints the report now instead of at the next 3 s tick (the last plays of a map would
// otherwise end after its last line); the console lines land a tick later, hence the wait. A page that already crashed
// must not take the rest of the run with it: the counters stay as they were.
const sndSnap = async () => {
  try { await page.evaluate(() => { const M = window.RN_M; if (M && M._rn_sound_report) M._rn_sound_report(); }); } catch {}
  await new Promise(r => setTimeout(r, 200));
  return { host: [...snd.host], game: snd.game, cancelled: snd.cancelled, ring: [...snd.ring], com: snd.com };
};
const sndDiff = (a, b) => ({ lost: LOST.reduce((x, k) => x + b.host[k] - a.host[k], 0), host: b.host.map((v, k) => v - a.host[k]),
  cancelled: b.cancelled - a.cancelled, game_off: b.game - a.game, ring: b.ring.map((v, k) => v - a.ring[k]), com: b.com - a.com,
  // Host faults that are not a Play: a COM method that failed and frames the ring lost (silence, a resync, overwritten
  // before the page took them). Not added to `lost` (different unit), but a row with one is never green.
  drops: (b.com - a.com) + (b.ring[0] - a.ring[0]) + (b.ring[1] - a.ring[1]) + (b.ring[2] - a.ring[2]) + (b.ring[3] - a.ring[3]) });
page.on('console', m => { const s = m.text(); fs.writeSync(logf, `${((Date.now() - T0) / 1000).toFixed(1)} page ${s}\n`); sndLine(s);
  if (/^STOP|wasm abort|RuntimeError/.test(s)) stop = s; });
page.on('pageerror', e => { say('pageerror: ' + e.message); stop = e.message; });
await page.goto(url);

// Browser-level CDP (Playwright's own session cannot address workers): attach to each worker target as it appears and
// sample it until the end. Chrome writes the port it took to DevToolsActivePort in the profile directory.
const prof = { ws: null, id: 0, wait: new Map(), sessions: new Map() };
async function cdp(method, params = {}, sessionId) {
  const id = ++prof.id; prof.ws.send(JSON.stringify({ id, method, params, sessionId }));
  return new Promise(r => prof.wait.set(id, r));
}
if (opt.cpuprofile) {
  const port = fs.readFileSync(`build/chrome-gate-${opt.port}/DevToolsActivePort`, 'utf8').split('\n')[0];
  const ver = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json();
  prof.ws = new WebSocket(ver.webSocketDebuggerUrl);
  await new Promise(r => prof.ws.onopen = r);
  prof.ws.onmessage = async ev => { const m = JSON.parse(ev.data);
    if (m.id && prof.wait.has(m.id)) { prof.wait.get(m.id)(m.result || m.error); prof.wait.delete(m.id); return; }
    if (m.method === 'Target.attachedToTarget' && m.params.targetInfo.type === 'worker') {
      const sid = m.params.sessionId; prof.sessions.set(sid, prof.sessions.size);
      await cdp('Profiler.enable', {}, sid); await cdp('Profiler.setSamplingInterval', { interval: 500 }, sid);
      await cdp('Profiler.start', {}, sid); await cdp('Runtime.runIfWaitingForDebugger', {}, sid);
    } };
  // Workers are children of the page target: auto-attach there (flat sessions on this socket).
  const t = await cdp('Target.getTargets');
  const pg = t.targetInfos.find(x => x.type === 'page' && x.url.includes(`:${opt.port}/`));
  const ps = (await cdp('Target.attachToTarget', { targetId: pg.targetId, flatten: true })).sessionId;
  await cdp('Target.setAutoAttach', { autoAttach: true, waitForDebuggerOnStart: false, flatten: true }, ps);
  // The page thread too: a frame the worker finished in time but the screen showed late is page or compositor time.
  await cdp('Profiler.enable', {}, ps); await cdp('Profiler.setSamplingInterval', { interval: 500 }, ps);
  await cdp('Profiler.start', {}, ps); prof.page = ps;
}
async function cpuProfileEnd() {
  if (prof.page) { const r = await Promise.race([cdp('Profiler.stop', {}, prof.page), sleep(20000).then(() => null)]);
    if (r && r.profile) fs.writeFileSync(path.join(opt.out, 'page.cpuprofile'), JSON.stringify(r.profile)); }
  for (const [sid, n] of prof.sessions) {
    const r = await Promise.race([cdp('Profiler.stop', {}, sid), sleep(20000).then(() => null)]);
    if (r && r.profile) fs.writeFileSync(path.join(opt.out, `worker-${n}.cpuprofile`), JSON.stringify(r.profile));
    say(`cpuprofile worker-${n}: ${r && r.profile ? r.profile.samples.length + ' samples' : 'none'}`);
  }
}

// In the page: read the state under its seqlock, and the input ring (layout: web/input.js).
await page.waitForFunction(() => window.RN_M && window.RN_M._rn_gamestate_addr && window.RN_M._rn_input_ring, null, { timeout: 120000 });
await page.evaluate(() => {
  const M = window.RN_M;
  const NAMES = ['version', 'seq', 'frame', 'mode', 'flags', 'pending'];
  const str = (u8, at) => { let s = ''; for (let k = 0; k < 32 && u8[at + k]; k++) s += String.fromCharCode(u8[at + k]); return s; };
  window.GHOST = {
    read() {
      const b = M._rn_gamestate_addr() >>> 2, u = M.HEAPU32, f = new Float32Array(u.buffer), i = new Int32Array(u.buffer);
      for (let tries = 0; tries < 8; tries++) {
        const s0 = Atomics.load(u, b + 1);
        if (s0 & 1) continue;
        const o = {};
        NAMES.forEach((n, k) => { o[n] = u[b + k]; });
        o.pos = [f[b + 6], f[b + 7], f[b + 8]]; o.rot = Array.from(f.subarray(b + 9, b + 18)); o.vel = [f[b + 18], f[b + 19], f[b + 20]];
        o.health = f[b + 21]; o.health_base = i[b + 22]; o.painkillers = i[b + 23]; o.weapon = i[b + 24]; o.clip = i[b + 25];
        o.reserve = i[b + 26]; o.bt = f[b + 27]; o.bt_active = u[b + 28]; o.enemies = u[b + 29]; o.alive = u[b + 30]; o.items = u[b + 31]; o.control = u[b + 351];
        const u8 = M.HEAPU8; o.level = str(u8, (b + 32) * 4); o.next = str(u8, (b + 40) * 4);
        o.en = [];
        for (let k = 0; k < Math.min(o.enemies, 48); k++) { const e = b + 48 + 6 * k;
          o.en.push({ pos: [f[e], f[e + 1], f[e + 2]], health: f[e + 3], flags: u[e + 4], room: u[e + 5], name: str(u8, u[b + 352] + 64 * k) }); }
        o.room = u[b + 336]; o.nav_done = u[b + 338]; o.pitch = f[b + 344];
        o.speed = f[b + 354]; o.fov = f[b + 363]; // existing gamestate ABI: speed and scripted FOV multiplier
        if (Atomics.load(u, b + 1) === s0) return o;
      }
      return null;
    },
    ring() { return M._rn_input_ring() >>> 2; },
    push(e) { const h = M.HEAP32, b = this.ring(), head = Atomics.load(h, b), tail = Atomics.load(h, b + 1);
      if (head - tail >= 1024) return false; h[b + 8 + (head % 1024)] = e; Atomics.store(h, b, head + 1); return true; },
    key(dik, down) { return this.push((down ? 0x100 : 0) | dik); },
    button(n, down) { return this.push(0x80000000 | (down ? 0x100 : 0) | n); },
    // The AI movement network: ask host/gamestate.c for a dump (words 337-340), wait a frame or two, copy it out.
    async nav() {
      const b = M._rn_gamestate_addr() >>> 2, u = M.HEAPU32, want = (u[b + 338] + 1) >>> 0;
      Atomics.store(u, b + 337, want);
      for (let k = 0; k < 100 && u[b + 338] !== want; k++) { await new Promise(r => setTimeout(r, 50)); Atomics.store(u, b + 337, want); }
      if (u[b + 338] !== want) return null;
      const n = u[b + 339], a = u[b + 340] >>> 2, i = new Int32Array(u.buffer), f = new Float32Array(u.buffer), out = [];
      for (let k = 0; k < n; k++) { const e = a + 10 * k;
        out.push({ id: i[e], kind: i[e + 1], room: u[e + 2], pos: [f[e + 3], f[e + 4], f[e + 5]], nb: [i[e + 6], i[e + 7], i[e + 8], i[e + 9]] }); }
      // Rooms (words 345-346, Room of 10 words: ptr, idx, name[32]) and exits (347-348, Portal of 19 words).
      const u8 = M.HEAPU8, rooms = [], portals = [];
      for (let k = 0, a2 = u[b + 346] >>> 2; k < u[b + 345]; k++) { const e = a2 + 10 * k; let name = '';
        for (let j = 0; j < 32 && u8[(e + 2) * 4 + j]; j++) name += String.fromCharCode(u8[(e + 2) * 4 + j]);
        rooms.push({ ptr: u[e], idx: i[e + 1], name }); }
      for (let k = 0, a3 = u[b + 348] >>> 2; k < u[b + 347]; k++) { const e = a3 + 19 * k;
        portals.push({ a: i[e], b: i[e + 1], ok: u[e + 2], enabled: u[e + 3], c: [f[e + 4], f[e + 5], f[e + 6]],
          t: [f[e + 7], f[e + 8], f[e + 9]], R: Array.from(f.subarray(e + 10, e + 19)) }); }
      return { nodes: out, rooms, portals };
    },
    // Level triggers (words 341-342, Trig of 23 words: room, pos, radius, type, enabled/fired bytes, name[64]).
    trigs() {
      const b = M._rn_gamestate_addr() >>> 2, u = M.HEAPU32, f = new Float32Array(u.buffer), i = new Int32Array(u.buffer), u8 = M.HEAPU8;
      const n = u[b + 341], a = u[b + 342] >>> 2, out = [];
      for (let k = 0; k < n; k++) { const e = a + 23 * k; let name = '';
        for (let j = 0; j < 64 && u8[(e + 7) * 4 + j]; j++) name += String.fromCharCode(u8[(e + 7) * 4 + j]);
        out.push({ room: u[e], pos: [f[e + 1], f[e + 2], f[e + 3]], r: f[e + 4], type: i[e + 5], enabled: u8[(e + 6) * 4], fired: u8[(e + 6) * 4 + 1], name }); }
      // Level items (words 349-350, Item of 15 words: room (0 = picked), pos, radius, weapon id, active byte, name[32]).
      const items = [], m = u[b + 349], ia = u[b + 350] >>> 2;
      for (let k = 0; k < m; k++) { const e = ia + 15 * k; let name = '';
        for (let j = 0; j < 32 && u8[(e + 7) * 4 + j]; j++) name += String.fromCharCode(u8[(e + 7) * 4 + j]);
        items.push({ room: u[e], pos: [f[e + 1], f[e + 2], f[e + 3]], r: f[e + 4], weapon: i[e + 5], active: u8[(e + 6) * 4], name }); }
      return { radius: f[b + 343], list: out, items };
    },
    move(dx, dy) { const h = M.HEAP32, b = this.ring(); if (dx) Atomics.add(h, b + 2, dx | 0); if (dy) Atomics.add(h, b + 3, dy | 0); },
  };
});
const read = () => page.evaluate(() => window.GHOST.read());
const key = (dik, down) => page.evaluate(([d, k]) => window.GHOST.key(d, k), [dik, down]);
const button = (n, down) => page.evaluate(([b, d]) => window.GHOST.button(b, d), [n, down]);
const move = (dx, dy) => page.evaluate(([x, y]) => window.GHOST.move(x, y), [dx, dy]);
const sleep = ms => new Promise(r => setTimeout(r, ms));
const DIK = { DOWN: 0xd0, S: 0x1f, W: 0x11, A: 0x1e, D: 0x20, SPACE: 0x39, ENTER: 0x1c, ESC: 0x01, E: 0x12, R: 0x13, TAB: 0x0f, LSHIFT: 0x2a, CTRL: 0x2e };
async function tap(d, ms = 120) { await key(d, true); await sleep(ms); await key(d, false); await sleep(250); }
const MODES = ['none', 'menu', 'novel', 'game', 'stats', 'hud', 'console', 'other'];

const tsv = fs.openSync(path.join(opt.out, 'state.tsv'), 'w');
fs.writeSync(tsv, 't\tframe\tmode\tflags\tpending\tlevel\tnext\tx\ty\tz\thealth\tweapon\tclip\treserve\tpainkillers\tbt\talive\tenemies\tcontrol\n');
// opts: what makes a run a proof (no pre/tp/god, a clean save dir) readable from the report alone.
const saveDir = path.join(path.dirname(opt.game), `save-${opt.port}`);
const savesAtStart = fs.existsSync(saveDir) ? fs.readdirSync(saveDir, { recursive: true }).filter(f => /\.mps$/i.test(String(f))).length : 0;
const report = { plan: opt.plan, result: 'running', phases: [], deaths: 0, levels: [],
  opts: { from: opt.from ?? null, pre: opt.pre ?? null, tp: opt.tp ?? null, god: opt.god ?? null, savesAtStart } };
let last = null, shotN = 0;
// --shots jpg (fleet): small JPEG shots, so a Pod batch comes back light.
async function shot(tag) { const jpg = opt.shots === 'jpg', p = path.join(opt.out, `${String(++shotN).padStart(2, '0')}_${tag}.${jpg ? 'jpg' : 'png'}`);
  await page.screenshot(jpg ? { path: p, type: 'jpeg', quality: 60 } : { path: p }); return p; }
function row(s) {
  fs.writeSync(tsv, [((Date.now() - T0) / 1000).toFixed(1), s.frame, MODES[s.mode], s.flags, s.pending, s.level, s.next,
    ...s.pos.map(v => v.toFixed(2)), s.health.toFixed(1), s.weapon, s.clip, s.reserve, s.painkillers, s.bt.toFixed(2), s.alive, s.enemies, s.control].join('\t') + '\n');
}
async function phase(name, s) {
  report.phases.push({ t: +((Date.now() - T0) / 1000).toFixed(1), name, mode: MODES[s?.mode ?? 0], level: s?.level ?? '' });
  say(`phase ${name} mode ${MODES[s?.mode ?? 0]} level '${s?.level ?? ''}'`);
  await shot(name.replace(/\W+/g, '_'));
}

// One tick: read, log on change (and every second), detect level changes and deaths.
let lastRow = 0;
async function tick() {
  const s = await read();
  if (!s) return last;
  const now = Date.now();
  if (!last || s.mode !== last.mode || s.level !== last.level || s.pending !== last.pending || now - lastRow > 1000) { row(s); lastRow = now; }
  if (last && s.level !== last.level && s.level) { report.levels.push(s.level); await phase(`level ${s.level}`, s); }
  if (last && s.mode !== last.mode) await phase(`mode ${MODES[s.mode]}`, s);
  if (last && (s.flags & 4) && !(last.flags & 4)) { report.deaths++; say(`death at ${s.pos.map(v => v.toFixed(1))}`); await shot('death'); }
  last = s;
  return s;
}

// Body frame, measured once per run in play (calibrate): which rotation row is forward, and radians of yaw per mouse
// count (rea/map/input.md: counts x (slider*1.4+0.1) x 0.0025, then x 180 x dt x pi/180 in 0x4cecd0).
const body = { row: -1, sign: 1, gain: 0 };
const fwd = s => { const r = body.row, k = 3 * r; return [s.rot[k] * body.sign, s.rot[k + 2] * body.sign]; };   // xz
const yawOf = s => { const [x, z] = fwd(s); return Math.atan2(x, z); };
const wrap = a => Math.atan2(Math.sin(a), Math.cos(a));
// A dead body still answers mode 3 / flags 2 and still moves (the death camera), but it ignores the keys and the mouse:
// merge2_p1l2 let calibrate() run on Max's corpse at 108.1 (walk 1.17 "aligned", turn 0.0000), and the zero gain sent
// every later shot wide (foes' hp never changed). Nothing "plays" while flags bit 2 (dead) is set.
async function untilPlay() { let s; while (!stop && !((s = await tick())?.mode === 3 && (s.flags & 2) && !(s.flags & 4) && ((s.flags >> 8) & 15) === 1)) await sleep(200); return s; }
async function calibrate() {
  // Only with the player's controls on (s.control bit 0, GM_SetPlayerControls): a level's opening camera eats the keys,
  // the walk measures 0 and the turn gain is NaN. Retry until Max really moved.
  // false = no live aligned walk (killed while walking, or a dead body's numbers): play() reloads and calls it again.
  let s0, s1, dx, dz, dl = 0, best = -1, bd = 0;
  for (let k = 0; !stop && Date.now() - T0 < opt.secs * 1000 && (dl < 0.3 || Math.abs(bd) < 0.9); k++) {
    do { s0 = await untilPlay(); if (!(s0.control & 1)) await sleep(300); } while (!stop && !(s0.control & 1));
    await sleep(1500); s0 = await tick();
    // Every third try backwards (a resumed save can leave Max against a wall); the measured direction is flipped back.
    const back = k % 3 === 2, kd = back ? DIK.S : DIK.W;
    await key(kd, true); await sleep(900); await key(kd, false); await sleep(400);
    s1 = await tick();
    if ((s1.flags & 4) || s1.health <= 0) { say(`calibrate: killed while walking (hp ${s1.health.toFixed(0)}), no walk to measure`); return false; }
    dx = s1.pos[0] - s0.pos[0]; dz = s1.pos[2] - s0.pos[2]; dl = Math.hypot(dx, dz);
    if (back) { dx = -dx; dz = -dz; }
    best = -1; bd = 0;
    if (s0.room === s1.room) for (let r = 0; r < 3; r++) {
      const x = s0.rot[3 * r], z = s0.rot[3 * r + 2], l = Math.hypot(x, z) || 1, d = (x * dx + z * dz) / (l * (dl || 1));
      say(`calibrate row ${r} (${x.toFixed(2)}, ${z.toFixed(2)}) dot ${d.toFixed(3)}`);
      if (Math.abs(d) > Math.abs(bd)) { bd = d; best = r; }
    }
    // A wall slides the test walk sideways (nav2 part1_level4: dot 0.76 chose row 0). Room changes also invalidate local deltas.
    // 0.9 (not 0.95): part1_level2's start room gives a clean 0.917 on the right row, and the 0.95 retry walked Max into
    // the start-room shotgunner's fire until he died there.
    if (dl < 0.3 || Math.abs(bd) < 0.9) say(`calibrate: walked ${dl.toFixed(2)}, alignment ${bd.toFixed(3)} (try ${k + 1}), again`);
  }
  if (dl < 0.3 || Math.abs(bd) < 0.9) return false;
  body.row = best; body.sign = Math.sign(bd) || 1;
  say(`calibrate walk ${dl.toFixed(2)} units: forward = ${body.sign < 0 ? '-' : ''}row ${best} (dot ${bd.toFixed(3)})`);
  const y0 = yawOf(s1), N = 200;
  for (let k = 0; k < 10; k++) { await move(N / 10, 0); await sleep(20); }
  await sleep(600);
  const s2 = await tick(), dy = wrap(yawOf(s2) - y0);
  if ((s2.flags & 4) || Math.abs(dy) < 1e-3) {                          // a corpse ignores the mouse: 0.0000 rad/count
    say(`calibrate turn ${dy.toFixed(4)} rad${s2.flags & 4 ? ' on a dead body' : ''}: no usable gain, again`);
    body.row = -1; body.gain = 0; return false; }
  body.gain = dy / N;
  say(`calibrate turn ${N} counts -> ${dy.toFixed(4)} rad (${body.gain.toExponential(3)} rad/count)`);
  // Pitch: the same chain as yaw on lY (0x5195c0 -> 0x725b30); measure rad per count and sign, then level the aim.
  for (let k = 0; k < 10; k++) { await move(0, 10); await sleep(20); }
  await sleep(600);
  const s3 = await tick();
  if (s3.flags & 4) { say('calibrate pitch on a dead body: no usable pitch, again'); body.row = -1; body.gain = 0; return false; }
  body.pgain = (s3.pitch - s2.pitch) / 100;
  say(`calibrate pitch 100 counts -> ${(s3.pitch - s2.pitch).toFixed(4)} rad (${body.pgain.toExponential(3)} rad/count)`);
  if (Math.abs(body.pgain) < 1e-5) body.pgain = 0;
  else await move(0, Math.round(-s3.pitch / body.pgain));
  report.calibration = { ...body, walk: dl };
  return true;
}

// Play: fight what is awake and close, else follow a BFS path over the AI network toward the goal (the level exit when
// known, else the nearest room not visited yet). Stops when the level key changes (the level was finished) or time runs out.
const held = new Set();
async function hold(d, on) { if (on === held.has(d)) return; on ? held.add(d) : held.delete(d); await key(d, on); }
// Bullet time toggle (rea/map/gameplay.md, button tracker GM+0x1258): LShift pressed with any move axis held is a shoot-dodge, not a
// toggle, and costs 10% of the meter (play10a: a tap while walking cost 0.1 each time). Move keys up, one game frame, then the tap.
async function btToggle() { for (const k of [DIK.W, DIK.A, DIK.D, DIK.S]) await hold(k, false); await sleep(200); await tap(DIK.LSHIFT, 150); }
async function releaseAll() { for (const d of [...held]) await hold(d, false); await button(0, false); firing = false; }
let firing = false;
let refillDue = false;   // set by a forced exit, spent at the next map's first control (play())
const dxz = (a, b) => Math.hypot(a[0] - b[0], a[2] - b[2]);
async function play(until) {
  let s = await untilPlay();
  // Calibrate on a live Max only (merge2_p1l2 read a corpse): killed during it, the same death key as the loop below
  // reloads, and the next attempt measures a body that can move and turn.
  for (; body.row < 0 && !stop && Date.now() < until; ) {
    if (await calibrate()) break;
    say('calibrate: no live aligned walk, reloading (ENTER) and again');
    await releaseAll(); await sleep(2500); await tap(DIK.ENTER); await sleep(2000); s = await untilPlay();
  }
  if (body.row < 0) throw new Error("calibration did not obtain an aligned walk on a live Max within --secs");
  const level = s.level;
  const ex0 = EXITS[level];
  const ex = ex0 && { ...ex0, steps: (ex0.steps?.length ? ex0.steps : ex0.ff || [])
    .filter(st => typeof st.t === 'string').map(st => ({ ...ex0.ff?.find(f => f.t === st.t), ...st })) };
  let ignore = new Map(), strikes = new Map(), stuckAt = new Map(), foeT = { k: -1 };
  const blocked = new Set(), edgeBlocked = new Set(), edgeStuck = new Map();   // edgeBlocked: climbs the player cannot make, kept for the whole run
  let walkQ = [], walkEnd = 0, walkJumpT = 0;
  let approachKey = null;   // last graph node reached: continue to the target instead of routing back to that node
  let thruDone = null, noStraight = 0, straightStuck = 0;   // noStraight: until when the straight aim / through are off (stuck against a wall inside `near`)
  let saveDue = false, saveT = Date.now(), saveStep = 0;   // saveStep: the step at the last quicksave of this run (0 = none: the death key restarts the level)
  let preDone = false;
  let exploreUntil = 0;   // no route to the exit: explore the reachable rooms (their triggers run the level script)
  let tunDumped = false, jitI = 0, diagT = 0, pkT = 0, strafeT = 0, strafeDir = DIK.A, nearKey = null, ctlOn = true, backI = -1, hurryUntil = 0, clears = new Map(), stepI = 0, useT = 0, btT = 0, gunT = 0, rname = new Map(), itemsSaid = false;   // EXITS steps: current step, last USE tap, room ptr -> name
  const swept = new Set();                                      // nodes of a .DO exit's room already walked over
  const seenOn = new Set();                                     // step triggers seen enabled (a latch that turns off counts as done)
  const stepSaid = { t: Date.now() };
  const stepOff = new Map();                                    // step trigger -> enabled at the last poll
  const giveUp = new Map(); let pickKey = null, pickT = 0, boxKey = null, boxT = 0;      // items the game would not give (full ammo...) or that stay out of reach
  let g = null, route = null, ri = 0, goal = null, goalKey = null, trigs = null, trigT = 0, visited = new Set(), lastProg = { t: Date.now(), d: Infinity }, unstick = 0;
  const stats = { level, start: Date.now(), shots: 0, kills: 0, painkillers: 0, stuck: 0, rows: [] };   // rows: one per step, real = played, injected = RN_JUMP got us past (--rescue)
  let rowI = -1, rowT = Date.now();
  let stallKey = '', stallT = Date.now(), stallSaid = false;
  const finishRow = () => { if (!stats.forced && rowI >= 0 && !stats.rows.some(r => r.step === rowI + 1))
    stats.rows.push({ step: rowI + 1, t: ex.steps[rowI]?.t, how: 'real', secs: Math.round((Date.now() - rowT) / 1000) }); };
  const stepStarted = new Map();   // death restores the save, not a fresh --rescue deadline for the same step
  let aliveBefore = s.alive, hpBefore = s.health;
  if (opt.step0) saveStep = stepI = +opt.step0 - 1;                     // --plan resume: the saved step (the triggers of steps long done show nothing)
  await tap(0x22);                                                    // G: best weapon (action 25)
  while (!stop && Date.now() < until) {
    s = await tick();
    if (!s) { await sleep(100); continue; }
    if (s.level && s.level !== level) { finishRow(); await releaseAll(); stats.secs = (Date.now() - stats.start) / 1000; say(`level done: ${level} -> ${s.level} ${JSON.stringify(stats)}`); return { reached: s.level, stats }; }
    if (s.mode === 2) { await releaseAll(); stats.comicSkips = (stats.comicSkips || 0) + 1;
      say('comic: exiting with Esc (same input path as route)'); await untilLevelPlay(level, Math.min(30000, until - Date.now())); continue; }
    // Objective progress, including maps with no AI nodes and waits with controls off.
    const sk = `${stepI}/${s.mode}/${s.control}`;
    if (sk !== stallKey) { stallKey = sk; stallT = Date.now(); stallSaid = false; }
    if (!stallSaid && Date.now() - stallT >= 90000) {
      stallSaid = true; stats.stalls = (stats.stalls || 0) + 1;
      say(`STALL ${level}: 90 s without objective progress, step ${stepI + 1}, mode ${MODES[s.mode]}, control ${s.control}, nav ${g ? g.nodes.length : 'pending'}`);
      // A death during a busy camera can leave the "press use to load" screen without the dead flag (n50_chain 2026-11-19:
      // 2 h idle on it). Press the death key and retry every 90 s; a live game ignores a lone Enter + click.
      if (!s.control) { await releaseAll(); await tap(DIK.ENTER); await button(0, true); await sleep(100); await button(0, false);
        ignore.clear(); strikes.clear(); foeT = { k: -1 }; g = null; route = null; approachKey = null; stepI = saveStep; backI = -1; noStraight = 0;
        clears.clear(); seenOn.clear(); stepOff.clear(); blocked.clear(); stuckAt.clear(); giveUp.clear(); thruDone = null;
        say('stall with control off: pressed the death key'); stallKey = null; }
    }
    if (s.mode !== 3 || !(s.flags & 2) || ((s.flags >> 8) & 15) !== 1) { await releaseAll(); await sleep(150); continue; }
    if (s.flags & 4) {                                                // dead: the game asks for a key to load the last save
      await releaseAll(); await sleep(2500); await tap(DIK.ENTER); await button(0, true); await sleep(100); await button(0, false);
      ignore.clear(); strikes.clear(); foeT = { k: -1 };   // the save restores enemies too; old line-of-sight failures no longer apply
      g = null; route = null; approachKey = null; stepI = saveStep; backI = -1; noStraight = 0; clears.clear(); seenOn.clear(); stepOff.clear(); blocked.clear(); stuckAt.clear(); giveUp.clear(); thruDone = null; stats.deaths = (stats.deaths || 0) + 1; say(`dead: back at step ${stepI + 1}`); await sleep(1500); continue;   // the death key loads the last quicksave (or restarts the level)
    }
    // Reporter (OWNER 2026-11-16, Talos pattern): a map that kills the ghost 5 times or runs past --mapsecs is reported and left
    // by gm_init of its next map, so one run reports every map instead of dying in one.
    if (opt.rescue && !stats.forced && (s.control & 1) && (stats.deaths >= 5 || Date.now() - stats.start > (+opt.mapsecs || 900) * 1000)) {
      const nx = EXITS[level]?.next; stats.forced = `${stats.deaths >= 5 ? stats.deaths + ' deaths' : (+opt.mapsecs || 900) + ' s'} at step ${stepI + 1}`;
      say(`FORCED ${level}: ${stats.forced}`); await releaseAll();
      if (!nx || nx === 'menu') { stats.secs = (Date.now() - stats.start) / 1000; say(`level done: ${level} -> (forced end) ${JSON.stringify(stats)}`); return { reached: null, stats }; }
      // Refill on arrival (injected, logged): a forced gm_init loads the next map with the state the game stored at its last real
      // exit, not the current one (rep_chain3: refill before the jump, part2_level1 still began at hp 7.2 and died 5 times at step 1).
      refillDue = true; await jump([`maxpayne_gamemode->gm_init(${nx});`, 'x_modeswitch->s_modeswitch(game);']); continue; }
    // Controls off (GM_SetPlayerControls(false): cutscene, scripted camera): nothing we press moves Max, so no timer runs.
    if (!(s.control & 1)) { if (ctlOn) { ctlOn = false; say(`control off (camera ${s.control & 2 ? 'busy' : 'free'})`); }
      await releaseAll(); lastProg = { t: Date.now(), d: Infinity }; pickT = boxT = Date.now(); foeT.t = Date.now(); await sleep(150); continue; }
    if (!ctlOn) { ctlOn = true; say(`control on (camera ${s.control & 2 ? 'busy' : 'free'})`); }
    // First control in a map entered by force: refill, then F5 at once so a death reloads here and not the map's stored start.
    if (refillDue) { refillDue = false; stats.refills = (stats.refills || 0) + 1;
      say(`refill (injected) in ${level}: full health, beretta + 100 ammo, painkiller + 4 ammo, then quicksave`);
      // c_addhealth(100) as web/ffwd.js MORTAL: c_sethealth(1) left hp 20/60 and the F5 below then saved a death loop (merge_p1l2: 11 deaths in 50 s).
      // Retail pickups grant ownership AND ammo (11_beretta.txt / 11_medipack_small.txt). Part2's startup removes guns.
      await releaseAll();
      await jump(['player->c_addhealth(100);', 'player->C_PickupWeapon(beretta);', 'player->C_PickupWeapon(berettadual);', 'player->C_PickupAmmo(beretta~100);',
        'player->C_PickupWeapon(painkiller);', 'player->C_PickupAmmo(painkiller~4);']);
      await tap(0x22); await tap(DIK.R); await sleep(2000);
      const q = await tick(); say(`refill observed: hp ${q.health}/${q.health_base} weapon ${q.weapon} clip ${q.clip} reserve ${q.reserve} painkillers ${q.painkillers}`);
      await tap(0x3f); await sleep(2500); saveStep = stepI; saveT = Date.now(); continue; }
    // DEV setup only (--pre N --tp "::entity"): T_Activate the steps before N, then C_Teleport to a named entity, so a late step
    // is tested in one minute. Never part of the #7 proof run.
    if (opt.pre && !preDone) { preDone = true; const n = +opt.pre;
      for (let i = 0; i < n - 1 && EXITS[level]?.steps[i]; i++) { const ts = [].concat(EXITS[level].steps[i].t);
        for (let k = 0; k < 20; k++) { const l = await trigList(); const on = ts.find(t => l.some(x => x.name === t && x.enabled)); if (on) { await jump([`${on}->T_Activate();`]); break; } await sleep(500); } }
      for (const w of (opt.tp || '').split(',').filter(Boolean)) { await jump([`player->C_Teleport("${w}");`]); await sleep(1500); const q = await tick(); say(`tp ${w} -> ${q?.pos.map(v => v.toFixed(1))}`); }
      if (opt.god) await jump(['player->c_setinvulnerable(true);']);
      stepI = n - 1; route = null; await sleep(2000); say(`pre done: steps < ${n}, tp ${opt.tp ?? '-'}`); continue; }
    // Hurt: a skipped enemy may be the shooter, so the 30 s skips end (the 2-strike ones stay).
    if (s.health < hpBefore) for (const [k, t] of ignore) if (t !== Infinity) ignore.delete(k);
    hpBefore = s.health;
    if (s.alive < aliveBefore) stats.kills += aliveBefore - s.alive;
    aliveBefore = s.alive;
    if (!g) {
      const dump = await page.evaluate(() => window.GHOST.nav());
      if (!dump) { await sleep(500); continue; }
      const { nodes, rooms, portals } = dump;
      g = buildGraph(nodes, rooms, portals, EXITS[level]?.links?.map(l => l.nodes) || []); g.blocked = blocked; g.blockedEdges = edgeBlocked; rname = new Map(rooms.map(r => [r.ptr, r.name]));   // blocks: see the death reset
      fs.writeFileSync(path.join(opt.out, `nav_${level}.json`), JSON.stringify(dump));
      const bad = portals.filter(p => p.ok !== 3).length, rot = portals.filter(p => Math.abs(p.R[0] - 1) + Math.abs(p.R[4] - 1) + Math.abs(p.R[8] - 1) > 1e-3).length;
      say(`nav: ${nodes.length} nodes, ${rooms.length} rooms (${g.T.size} placed), ${portals.length} exits (${bad} fail the pair check, ${rot} rotated); player room ${s.room} ${g.byRoom.has(s.room) ? 'in' : 'NOT in'} the graph; facing ${fwd(s).map(v => v.toFixed(2))} (rot ${s.rot.map(v => v.toFixed(2))})`);
    }
    const me = g.world(s.room, s.pos), yaw = yawOf(s);
    visited.add(s.room);
    // Heal before the next fight too (combat_p2l1_v4: entered the bar at hp 32 with eight unused painkillers).
    const nAwake = s.en.filter(e => !(e.flags & 1) && e.health > 0 && (e.flags & 2) && g.byRoom.has(e.room) && dxz(g.world(e.room, e.pos), me) < 18 && !EXITS[level]?.spare?.names.some(n => n.slice(0, 31) === e.name)).length;
    if (s.health < (nAwake ? 0.55 : 0.8) * Math.max(s.health_base, 1) && s.painkillers > 0 && Date.now() - pkT > 3000) { pkT = Date.now(); await tap(DIK.TAB); stats.painkillers++; say(`painkiller at hp ${s.health.toFixed(0)} (awake ${nAwake}, left ${s.painkillers - 1})`); }
    // Current gun empty: G picks the best weapon that has ammo (action 25), every 2 s until one does.
    if (s.clip === 0 && s.reserve === 0 && Date.now() - gunT > 2000) { gunT = Date.now(); await tap(0x22); }
    // Threat: the closest awake enemy within 18 units, or, while the level's exit waits on kills (EXITS kill), the closest
    // living enemy in the player's room (Part0's junkies stay AI-inactive until shot).
    if (!trigs || Date.now() - trigT > 1000) { trigs = await page.evaluate(() => window.GHOST.trigs()); trigT = Date.now();
      if (!itemsSaid) { itemsSaid = true; const c = {}; for (const it of trigs.items) { const k = `${it.name}/${it.weapon}${it.room ? '' : '(picked)'}${it.active ? '' : '(off)'}`; c[k] = (c[k] || 0) + 1; }
        say(`items ${trigs.items.length}: ${Object.entries(c).map(([k, n]) => `${k} x${n}`).join(', ')}`); } }
    if (stepI >= 18 && !tunDumped && rname.get(s.room) === '::p2_tunnel') { tunDumped = true;   // what a player can use around the cab (play11 dev19b: Max stands inside it, side doors shut)
      for (const t of trigs.list) if (t.room === s.room || /tunnel|cab|train/i.test(t.name)) say(`tun trig ${t.name} room ${rname.get(t.room)} pos ${t.pos.map(v => v.toFixed(1))} world ${g.world(t.room, t.pos).map(v => v.toFixed(1))} r ${t.r.toFixed(1)} type ${t.type} en ${t.enabled}/${t.fired}`); }
    if (stepI >= 15 && Date.now() - diagT > 5000) { diagT = Date.now();   // the train door (play10: 200 s at its closed/open door)
      say(`diag train ${trigs.list.filter(t => /service_train/.test(t.name)).map(t => `${t.name.split('::').pop().replace('.TRIGGER', '')} ${t.enabled}/${t.fired}`).join(' ')} me ${me.map(v => v.toFixed(2))} yaw ${yaw.toFixed(2)}`); }
    // Scripted levels (EXITS steps): the current step is the one after the last whose trigger fired (or was on and went off).
    let step = null;
    if (ex?.steps) {
      const byName = new Map(trigs.list.map(t => [t.name, t]));
      for (const st of ex.steps) if (byName.get(st.t)?.enabled) seenOn.add(st.t);
      let lastDone = -1;
      // A timed keypad (hurry) counts only as the current step: the two keypads of one door switch together, so the
      // inside one going off when the outside one is pressed says nothing about the steps between them.
      ex.steps.forEach((st, i) => { const t = byName.get(st.t); if (st.hurry && i !== stepI) return;
        if (t && (t.fired || (seenOn.has(st.t) && !t.enabled))) lastDone = i; });
      // A later step's trigger turning on means the script is past every step before it: a trigger can be enabled and
      // fired inside one poll (part1_level1 ::r01::pt1 is enabled by the death counter and fired at once, its T_Activate
      // sends T_Enable(false), which also clears the fired byte 0x71f540), so its own latches can show nothing.
      ex.steps.forEach((st, i) => { const t = byName.get(st.t); if (!t) return;
        if (i === stepI + 1 && stepOff.get(st.t) === 0 && t.enabled) { lastDone = Math.max(lastDone, i - 1); say(`step ${i + 1} trigger on: steps up to ${i} done`); }
        stepOff.set(st.t, t.enabled); });
      if (lastDone + 1 > stepI) { for (let k = stepI; k <= lastDone; k++) { if (ex.steps[k].hurry) hurryUntil = Date.now() + 1000 * ex.steps[k].hurry;
          // Step `walk`: points in the trigger's room frame, walked straight once the trigger is done, where the AI network has no nodes (jump: over a barricade).
          const wt = ex.steps[k].walk && byName.get(ex.steps[k].t); if (wt) { walkQ = ex.steps[k].walk.map(p => ({ w: g.world(wt.room, p.at), jump: p.jump })); walkEnd = Date.now() + 25000; say(`walk ${walkQ.length} points after ${ex.steps[k].t}`); } }
        stepI = lastDone + 1; saveDue = true; route = null; blocked.clear(); stuckAt.clear(); say(`step ${stepI + 1}/${ex.steps.length}: ${ex.steps[stepI]?.t ?? 'all done'}`); }
      // Back step: an earlier trigger on again (a timed door shut before we got through) while the current one has no way.
      if (backI >= 0) { const bt = byName.get(ex.steps[backI].t);
        if (!bt || bt.fired || !bt.enabled) { if (ex.steps[backI].hurry) hurryUntil = Date.now() + 1000 * ex.steps[backI].hurry;
          say(`back step ${backI + 1} done`); backI = -1; route = null; blocked.clear(); stuckAt.clear(); } }
      step = ex.steps[backI >= 0 ? backI : stepI] || null;
      step = step && { ...step, trig: byName.get(step.t), use: step.use || byName.get(step.t)?.type === 0 };
      // Rescue (OWNER 2026-11-04, NFSU pattern): a step that takes more than --rescue s is injected (T_Activate of its trigger), logged as such, and the run goes on.
      if (rowI !== stepI) { finishRow(); rowI = stepI; if (!stepStarted.has(stepI)) stepStarted.set(stepI, Date.now()); rowT = stepStarted.get(stepI); }
      // The rescue always targets the current milestone, never a back step (combat_p2l1_v5 rescued an earlier switch as step 4).
      if (opt.rescue && ex.steps[stepI] && ctlOn && Date.now() - rowT > opt.rescue * 1000) { const rescueStep = ex.steps[stepI], tt = [].concat(rescueStep.t)[0];
        say(`RESCUE step ${stepI + 1} ${tt}: step deadline ${opt.rescue} s (elapsed, not a proof of no movement) at ${me.map(v => v.toFixed(1))}`); stats.rows.push({ step: stepI + 1, t: tt, how: 'injected', secs: +opt.rescue, at: me.map(v => +v.toFixed(1)) });
        await releaseAll();
        const sent = await jump([...(rescueStep.tp ? [`player->C_Teleport("${rescueStep.tp}");`] : []), `${tt}->T_Activate();`]);
        stats.rows.at(-1).sent = sent;
        // Reusable switches keep enabled/fired unchanged: remember our injected step explicitly, never as real play.
        if (sent?.sent && !sent.failed) { stepI++; backI = -1; saveDue = true; route = null; blocked.clear(); stuckAt.clear(); }
        rowT = Date.now(); stepStarted.set(rowI, rowT); await sleep(1500); continue; }
      // A step that waits 30 s: say what its trigger looks like (a friendly at health 0 counts as no enemy: kill targets need health > 0).
      if (step && Date.now() - (stepSaid.t || 0) > 30000) { const t = step.trig; stepSaid.t = Date.now();
        if (stepI >= 17 && (stepSaid.n = (stepSaid.n || 0) + 1) <= 3) await shot(`step${stepI + 1}_wait`);   // what blocks the cab door (play8)
        say(`step ${stepI + 1} waits: ${step.t} ${t ? `en ${t.enabled} fired ${t.fired} room ${t.room} ${g.byRoom.has(t.room) ? 'in' : 'NOT in'} graph pos ${t.pos.map(v => v.toFixed(1))} d ${dxz(g.world(t.room, t.pos), me).toFixed(1)}` : 'not in the trigger list'}`); }
    }
    // A room prefix or an instance name, using the same name matching as routeMap/ffwd.
    const killRoom = e => { const list = step?.kill || ex?.kill;
      return list === true || !!list?.some(n => e.name === n.slice(0, 31) || e.name.startsWith(n + '::')); };
    // The final collision/animation needs the same deadline as intermediate steps (rep_chain3: step N+1 waited 600 s).
    if (opt.rescue && ex && !step && Date.now() - rowT > opt.rescue * 1000) {
      const lines = ex.send || ex.exit.filter(n => n.endsWith('.TRIGGER')).map(n => `${n}->T_Activate();`);
      say(`STALL exit ${level}: ${opt.rescue} s without level transition`);
      rowT = Date.now();
      if (lines.length) { await releaseAll(); say(`RESCUE exit ${level}: ${lines.join(' | ')}`);
        const row = { step: stepI + 1, t: 'exit', how: 'injected', secs: +opt.rescue }; stats.rows.push(row);
        row.sent = await jump(lines); route = null; await sleep(1500); continue; }
    }
    // A target that loses no health after 4 s of fire is out of sight or out of reach: skip it for 30 s.
    // Checkpoint (OWNER 2026-11-03): F5 after a milestone at health >= 40; periodic saves also wait until no foe is within 18.
    // Save milestones even near enemies: rep_chain3 deaths erased the attic/lift progress and restarted step 1.
    if (Date.now() - saveT > (+opt.savems || 90000)) saveDue = true;   // and every 90 s of play (--savems: NOCHE50 save-stall measure)
    if (saveDue && s.health >= 40 && (stepI !== saveStep || !s.en.some((e, k) => !(e.flags & 1) && e.health > 0 && g.byRoom.has(e.room) && dxz(g.world(e.room, e.pos), me) < 18))) {
      saveDue = false; saveT = Date.now(); saveStep = stepI; await releaseAll(); await tap(0x3f); await sleep(2500); say(`quicksave at step ${stepI + 1} hp ${s.health.toFixed(0)}`); continue; }
    let foe = null, fd = 18, fk = -1;
    const armed = s.clip > 0 || s.reserve > 0;
    s.en.forEach((e, k) => { if (!armed || e.flags & 1 || e.health <= 0 || (ignore.get(k) || 0) > Date.now() || ex?.spare?.names.some(n => n.slice(0, 31) === e.name)) return;
      if (!((e.flags & 2) || (killRoom(e) && e.room === s.room))) return;
      const d = dxz(g.world(e.room, e.pos), me);
      // Same-room threats first; keep a valid target instead of swinging between two nearly equidistant shooters.
      const betterRoom = e.room === s.room && foe?.room !== s.room;
      const sameSide = (e.room === s.room) === (foe?.room === s.room);
      if (d < 18 && (!foe || betterRoom || (sameSide && (k === foeT.k || (fk !== foeT.k && d < fd))))) { fd = d; foe = e; fk = k; } });
    if (fk !== foeT.k) { if (foe && foeT.k === -1) await tap(0x22);   // take the best carried gun into a new fight, including pickups
      foeT = { k: fk, hp: foe?.health, t: Date.now() };
      if (foe) say(`foe #${fk} ${foe.name} hp ${foe.health.toFixed(0)} d ${fd.toFixed(1)} dy ${(g.world(foe.room, foe.pos)[1] - me[1]).toFixed(1)} ${foe.flags & 2 ? 'awake' : 'asleep'} clip ${s.clip} res ${s.reserve}`); }
    else if (foe && foe.health < foeT.hp) foeT = { ...foeT, hp: foe.health, t: Date.now() };
    else if (foe && Date.now() - foeT.t > (firing ? 4000 : 8000)) { const n = (strikes.get(fk) || 0) + 1; strikes.set(fk, n);
      ignore.set(fk, n >= 2 ? Infinity : Date.now() + 30000); say(`foe #${fk} hp unchanged (impact and line of sight unknown): skipped ${n >= 2 ? 'for good' : '30 s'}`); foeT = { k: -1 }; foe = null; }
    // Target when nothing is to be shot: an enabled exit trigger of this level, else (kill-gated exit) the closest living
    // enemy the network reaches, else the nearest unvisited room.
    let tgt = null;
    // Pickups (painkillers, ammo, weapons; dead enemies' guns land as items too) within 12 units: the game decides what it
    // gives (0x68c690); one it keeps in the world 10 s after we went for it is given up for this life.
    let pick = null;
    if (!foe && Date.now() > hurryUntil) for (const it of trigs.items) { const key = `${it.name}@${it.pos.map(v => v.toFixed(1))}`;
      if (!it.room || !it.active || !g.byRoom.has(it.room) || giveUp.has(key)) continue;
      const w = g.world(it.room, it.pos), d = dxz(w, me);
      if (d < (it.weapon === 16 && s.health < 0.7 * Math.max(s.health_base, 1) ? 25 : 12) && Math.abs(w[1] - me[1]) < 0.9 && (!pick || key === pickKey || (pick.key !== pickKey && d < pick.d))) pick = { key, room: it.room, w, d, reach: Math.min(0.3, it.r) }; }   // keep the one we went for
    if (pickKey && !(pick && pick.key === pickKey) && !foe) {         // the one we went for left the world: picked (or someone else's)
      if (!trigs.items.some(it => it.room && `${it.name}@${it.pos.map(v => v.toFixed(1))}` === pickKey)) say(`picked ${pickKey} hp ${s.health.toFixed(0)} pk ${s.painkillers} res ${s.reserve}`);
      pickKey = null; }
    if (pick && pick.key !== pickKey) { pickKey = pick.key; pickT = Date.now(); say(`pick ${pick.key} d ${pick.d.toFixed(1)}`); }
    if (pick && Date.now() - pickT > 10000) { say(`gave up ${pick.key} d ${pick.d.toFixed(2)} dy ${(pick.w[1] - me[1]).toFixed(2)}`); giveUp.set(pick.key, 1); pickKey = null; pick = null; }
    if (pick) tgt = pick;
    // Containers (EXITS open): an enabled USE trigger of a closet/locker within 15 units; E at reach opens it and its items
    // become active (LI_ActivateLevelItem). One not opened 12 s after we went for it is given up for this life.
    // Same level only (< 0.9 up/down): part1_level1 p2 closet03 sits on the tracks, the detour dropped Max off the platform (play4).
    else if (!foe && ex?.open && Date.now() > hurryUntil) { const re = new RegExp(ex.open.match);
      for (const t of trigs.list) { if (t.type !== 0 || !t.enabled || t.fired || !re.test(t.name) || giveUp.has(t.name) || !g.byRoom.has(t.room)) continue;
        const w = g.world(t.room, t.pos), d = dxz(w, me);
        if (d < 15 && Math.abs(w[1] - me[1]) < 0.9 && (!tgt || t.name === boxKey || (tgt.key !== boxKey && d < tgt.d))) tgt = { key: t.name, room: t.room, w, d, reach: t.r + trigs.radius, use: true }; }
      if (tgt && tgt.key !== boxKey) { boxKey = tgt.key; boxT = Date.now(); say(`open ${boxKey} d ${tgt.d.toFixed(1)}`); }
      if (tgt && Date.now() - boxT > 12000) { say(`gave up ${boxKey}`); giveUp.set(boxKey, 1); boxKey = null; tgt = null; }
      if (tgt?.use && tgt.room === s.room && tgt.d < tgt.reach + 0.8 && Date.now() - useT > 1000) { useT = Date.now(); await tap(DIK.E); say(`use at ${tgt.key}`); }
    }
    if (!tgt && !foe && step && Date.now() > exploreUntil) {
      const t = step.trig;
      if (t && t.enabled && !t.fired && g.byRoom.has(t.room)) { const w = g.world(t.room, t.pos);
        tgt = { key: step.t, room: t.room, w, d: dxz(w, me), reach: t.r + trigs.radius, use: step.use || t.type === 0, via: step.at && g.world(t.room, step.at), thru: step.through && g.world(t.room, step.through.map((v, i) => i === 0 ? v + [0, -0.3, 0.3][jitI % 3] : v)), near: step.near }; }
      else if (step.kill) s.en.forEach((e, k) => { if (e.flags & 1 || e.health <= 0 || !killRoom(e) || !g.byRoom.has(e.room) || (ignore.get(k) || 0) > Date.now()) return;
        const w = g.world(e.room, e.pos), d = dxz(w, me), key = `enemy#${k}`, keep = key === goalKey;
        if (!tgt || keep || (!tgt.keep && d < tgt.d)) tgt = { key, room: e.room, w, d, reach: 3, keep }; });
      // Use (action button) once in reach, every second until it fires.
      // A timed step (hurry: the keypad of a door that shuts again) starts its clock at the press, not when the latch is
      // seen: no pickup detours from the press on (part1_level1: the door cutscene alone eats 7 s of the timer).
      if (tgt?.use && tgt.room === s.room && tgt.d < tgt.reach + 0.8 && Date.now() - useT > 1000) { useT = Date.now(); await tap(DIK.E); say(`use at ${step.t} d ${tgt.d.toFixed(2)} reach ${tgt.reach.toFixed(2)} dy ${(tgt.w[1] - me[1]).toFixed(2)}`);
        if (step.hurry) hurryUntil = Math.max(hurryUntil, Date.now() + 1000 * step.hurry); }
    }
    else if (!tgt && !foe && ex && Date.now() > exploreUntil) {
      for (const t of trigs.list) if (ex.exit.includes(t.name) && t.enabled && !t.fired && g.byRoom.has(t.room)) {
        const w = g.world(t.room, t.pos), d = dxz(w, me); if (!tgt || d < tgt.d) tgt = { key: t.name, room: t.room, w, d, reach: t.r + trigs.radius }; }
      // An exit that is a dynamic object (...DO: part1_level1b ::exit::end_trigger, a character-collision, no trigger in the list, no
      // position in the dump): sweep the nodes of its room, nearest first, until the collision sends the level on.
      if (!tgt && !ex.kill) for (const name of ex.exit.filter(n => n.endsWith('.DO'))) {
        const rn = name.match(/^(::[^:]+)/)?.[1], ptr = [...rname].find(([, v]) => v === rn)?.[0], list = ptr && g.byRoom.get(ptr);
        if (!list?.length) continue;
        let left = list.filter(n => !swept.has(n.id));
        if (!left.length) { swept.clear(); left = list; }
        const n = left.reduce((b, m) => dxz(m.w, me) < dxz(b.w, me) ? m : b);
        tgt = { key: name, room: ptr, w: n.w, d: dxz(n.w, me), reach: 1, nid: n.id }; if (nearKey !== `sweep${n.id}`) { nearKey = `sweep${n.id}`; say(`sweep ${name}: node ${n.id} d ${tgt.d.toFixed(1)} (${swept.size}/${list.length} swept)`); } }
      // Keep the enemy already chased while it lives (nearest-by-air flips between rooms and the route never ends).
      if (!tgt && ex.kill) s.en.forEach((e, k) => { if (e.flags & 1 || e.health <= 0 || !killRoom(e) || !g.byRoom.has(e.room) || (ignore.get(k) || 0) > Date.now()) return;
        const w = g.world(e.room, e.pos), d = dxz(w, me), key = `enemy#${k}`, keep = key === goalKey;
        if (!tgt || keep || (!tgt.keep && d < tgt.d)) tgt = { key, room: e.room, w, d, reach: 3, keep }; });
    }
    if (tgt?.nid !== undefined && tgt.d < 1.2 && tgt.room === s.room) swept.add(tgt.nid);
    let aim = null, straight = false;
    if (tgt?.thru && thruDone === tgt.key && dxz(tgt.thru, me) > tgt.near + 1) thruDone = null;   // knocked out of the door zone: the through counts again
    const straightOk = Date.now() > noStraight;
    if (foe) aim = g.world(foe.room, foe.pos);
    else if (walkQ.length && Date.now() > walkEnd) { say(`walk gave up at ${me.map(v => v.toFixed(1))}, ${walkQ.length} points left`); if (opt.stuckshots) await shot('walk_gaveup'); walkQ = []; route = null; }
    else if (walkQ.length) { aim = walkQ[0].w; straight = true;
      if (walkQ[0].jump && Date.now() - walkJumpT > 900) { walkJumpT = Date.now(); await tap(DIK.SPACE); }
      if (dxz(aim, me) < 0.7) { say(`walk point reached at ${me.map(v => v.toFixed(1))}`); if (opt.stuckshots) await shot('walk_point'); walkQ.shift(); route = null; } }
    // Step `through` + `near`: a door the AI network does not cross (part1_level1 train): from within `near` of the door
    // point straight through it, then straight at the trigger; only on the trigger's own level (|dy| < 0.9).
    else if (straightOk && tgt?.thru && tgt.room === s.room && thruDone !== tgt.key && Math.abs(tgt.thru[1] - me[1]) < 0.9 && dxz(tgt.thru, me) < tgt.near) {
      aim = tgt.thru; straight = true; if (dxz(tgt.thru, me) < 0.6) thruDone = tgt.key; }
    else if (straightOk && tgt && (tgt.room === s.room || tgt.key === approachKey) && (tgt.key === approachKey || tgt.d < (tgt.near || 3)) && (!tgt.via || Math.abs(tgt.w[1] - me[1]) < 0.9) && (!tgt.near || Math.abs(tgt.w[1] - me[1]) < 0.9)) { aim = tgt.w; straight = true;        // last steps: straight at the trigger centre
      if (tgt.key !== nearKey) { nearKey = tgt.key; const t = trigs.list.find(t => t.name === tgt.key);
        say(`at ${tgt.key} d ${tgt.d.toFixed(2)} dy ${(tgt.w[1] - me[1]).toFixed(2)} reach ${tgt.reach.toFixed(2)}${t ? ` type ${t.type} r ${t.r.toFixed(2)} en ${t.enabled} fired ${t.fired}` : ''}`); } }
    else {
      if (tgt && tgt.key !== goalKey) route = null;
      if (!route || ri >= route.length) {
        goal = tgt ? nearest(g, tgt.via || tgt.w, tgt.room) : pickGoal(g, s, me, visited); goalKey = tgt?.key ?? null;
        const from = nearest(g, me, s.room);
        route = goal && from ? navPath(g, from, goal) : null; ri = 0;
        // A route that begins by dropping while the goal is above the landing is no route (the platform over the tracks).
        if (route && startsWithDrop(g, route, goal)) { say(`nav: route ${from.id} -> ${goal.id} begins by dropping to ${route[1].id} y ${route[1].w[1].toFixed(1)}, goal y ${goal.w[1].toFixed(1)} is above: refused`); route = null; }
        // The nearest node may sit in a piece of the network with no way on (Max on the tracks below a platform): start
        // from the nearest node of this room that has one.
        if (!route && goal && from) for (const n of (g.byRoom.get(s.room) || []).filter(n => n !== from).sort((x, y) => dxz(x.w, me) - dxz(y.w, me)).slice(0, 12)) {
          const r = navPath(g, n, goal); if (r && !startsWithDrop(g, r, goal)) { route = r; say(`nav: ${from.id} has no way on; starting from ${n.id} at ${dxz(n.w, me).toFixed(1)}`); break; } }
        if (!route) {
          // Every reachable room seen: the level script may have opened something meanwhile, so forget the blocks.
          if (!goal) { say('nav: every reachable room visited; blocks cleared'); visited.clear(); visited.add(s.room); blocked.clear(); stuckAt.clear(); await releaseAll(); await sleep(500); continue; }
          // A target behind our own blocks: the blocks may be stale (a door opened since), so drop them before exploring.
          if (tgt && tgt.key === pickKey) { say(`gave up ${pickKey}: no route`); giveUp.set(pickKey, 1); pickKey = null; route = null; continue; }
          if (tgt && step && tgt.key === step.t && backI < 0 && (clears.set(tgt.key, (clears.get(tgt.key) || 0) + 1), clears.get(tgt.key) >= 2)) {
            const byName = new Map(trigs.list.map(t => [t.name, t]));
            // Only re-open a timed keypad: an always-on earlier trigger does not mean the route went backward.
            const on = j => { const bt = byName.get(ex.steps[j].t); return bt && bt.enabled && !bt.fired; };
            let j = -1;
            for (let k = stepI - 1; k >= 0 && j < 0; k--) if (ex.steps[k].hurry && on(k)) j = k;
            if (j >= 0) { backI = j; clears.delete(tgt.key); say(`no way to ${tgt.key}: back to step ${j + 1} ${ex.steps[j].t}`); }
          }
          if (tgt && blocked.size) { say(`nav: no route from ${from?.id} to ${goal.id}: ${blocked.size} blocks cleared`); blocked.clear(); stuckAt.clear(); continue; }
          say(`nav: no route from ${from?.id} to ${goal.id}${tgt ? ' (exit): exploring 90 s' : ''}`);
          if (tgt) exploreUntil = Date.now() + 90000; else visited.add(goal.room);
          await releaseAll(); await sleep(300); continue;
        }
        say(`route ${from.id} -> ${goal.id} (room ${goal.room}${tgt ? ', ' + tgt.key : ''}), ${route.length} nodes; me ${me.map(v => v.toFixed(1))} room ${s.room} hp ${s.health.toFixed(0)}`);
        say('  enemies ' + s.en.map(e => `[${e.name} ${g.world(e.room, e.pos).map(v => v.toFixed(1))} r${e.room === s.room ? '=' : (g.byRoom.has(e.room) ? 'k' : '?')} hp${e.health.toFixed(0)} f${e.flags} d${dxz(g.world(e.room, e.pos), me).toFixed(1)}]`).join(' '));
        if (ex) say('  exits ' + trigs.list.filter(t => ex.exit.includes(t.name)).map(t => `${t.name} r${t.r.toFixed(1)} type ${t.type} en ${t.enabled} fired ${t.fired} room ${g.byRoom.has(t.room) ? 'k' : '?'}`).join(' | '));
      }
      // Reached a node: aim at the next one. A short reach keeps the turn at the node instead of cutting the corner.
      while (ri < route.length - 1 && dxz(route[ri].w, me) < 0.6) ri++;
      if (ri === route.length - 1 && dxz(route[ri].w, me) < 0.6) { if (!tgt) visited.add(goal.room); else approachKey = tgt.key; route = null; if (!tgt) continue; }
      aim = route ? route[ri].w : tgt.w;
    }
    const err = wrap(Math.atan2(aim[0] - me[0], aim[2] - me[2]) - yaw);
    // Pitch: at a foe, aim at its height (+ is down; positions are the same body point for both characters); else level.
    const want = foe ? Math.atan2(me[1] - aim[1], Math.max(dxz(aim, me), 0.3)) : 0;
    const pdy = body.pgain ? Math.max(-60, Math.min(60, Math.round(0.6 * (want - s.pitch) / body.pgain))) : 0;
    await move(Math.max(-90, Math.min(90, Math.round(0.6 * err / body.gain))), pdy);
    if (foe) {
      // Bullet time (LShift toggles it, BULLETTIME 31, 0x44ebb0; the game refuses it under the meter threshold, only an active
      // state holds the key back): when two or more foes are awake near, or hurt with one close. A lone foe at good health
      // is shot in real time and the meter stays for the next fight.
      if (!s.bt_active && s.bt > 0.2 && Date.now() - btT > 2000 && (nAwake >= 2 || (s.health < 0.5 * Math.max(s.health_base, 1) && fd < 12))) { btT = Date.now(); await btToggle(); stats.bt = (stats.bt || 0) + 1; say(`bullet time on: awake ${nAwake} fd ${fd.toFixed(1)} hp ${s.health.toFixed(0)} bt ${s.bt.toFixed(2)}`); }
      // Not a sitting target: far, close in; mid range, strafe (switch side every ~0.9 s); hurt and close, back off while shooting.
      // Melee pools also report 1/1 ammo: the bat/pipe must close to their 1.2/0.8-unit blast (weapons.md).
      const melee = s.weapon === 1 || s.weapon === 2;
      const reach = melee ? (s.weapon === 1 ? 0.7 : 1) : 8;
      const hurt = !melee && s.health < 0.35 * Math.max(s.health_base, 1) && fd < 10;
      if (fd >= 3 && fd < 14 && Date.now() - strafeT > 900) { strafeT = Date.now(); strafeDir = strafeDir === DIK.A ? DIK.D : DIK.A; }
      await hold(DIK.W, !hurt && fd > reach && Math.abs(err) < 0.5);        // far: close in while turning
      await hold(DIK.S, hurt);
      await hold(strafeDir, fd >= 3 && fd < 14 && !hurt && Math.abs(err) < 0.5);
      await hold(strafeDir === DIK.A ? DIK.D : DIK.A, false);
      const on = Math.abs(err) < (melee ? 0.15 : Math.min(0.1, Math.atan2(0.35, fd))) && Math.abs(want - s.pitch) < 0.1;
      if (on !== firing) { await button(0, on); firing = on; if (on) stats.shots++; }
    } else {
      if (firing) { await button(0, false); firing = false; }
      for (const k of [DIK.A, DIK.D, DIK.S]) await hold(k, false);
      if (s.bt_active && Date.now() - btT > 2000) { btT = Date.now(); await btToggle(); }   // no foe: bullet time off
      await hold(DIK.W, Math.abs(err) < 0.7);
      // Stuck: no progress toward the current node for 2 s -> use (doors), jump, side step, then a new route.
      const d = dxz(aim, me), aimKey = straight ? `straight:${tgt?.key}:${aim}` : route?.[ri]?.id;
      if (aimKey !== lastProg.key) lastProg = { key: aimKey, t: Date.now(), d: Infinity };
      if (d < lastProg.d - 0.3) { lastProg = { key: aimKey, t: Date.now(), d }; straightStuck = 0; }
      else if (Date.now() - lastProg.t > 2000) {
        stats.stuck++; unstick++;
        // Stuck on a straight aim (against the train's side, a wall inside `near`): the graph route takes over for 10 s.
        if (straight && ++straightStuck >= 2) { noStraight = Date.now() + 10000; approachKey = null; thruDone = null; straightStuck = 0; route = null; jitI++; say(`stuck on a straight aim at d ${d.toFixed(1)}: graph route for 10 s`); }
        // Back up to the node before (the corner was cut), side step, then go on along the same route.
        await hold(DIK.W, false); await hold(DIK.S, true); await sleep(500); await hold(DIK.S, false);
        const side = unstick % 2 ? DIK.A : DIK.D; await hold(side, true); await sleep(400); await hold(side, false);
        // Twice stuck on the way to the same node: the player cannot get there (a closed gate the AI network crosses).
        const at = straight ? undefined : route?.[ri]?.id, prevN = ri > 0 ? route?.[ri - 1] : null, ek = prevN && at !== undefined ? `${prevN.id}:${at}` : null;
        // A climb edge (dropSet, up 0.5 or more) stuck twice: that edge alone is cut for the rest of the run (play11: p2 13252->13170, a 1.0 ledge).
        if (ek && g.dropSet.has(ek) && g.byId.get(at).w[1] - prevN.w[1] >= 0.5) { const n = (edgeStuck.get(ek) || 0) + 1; edgeStuck.set(ek, n);
          if (n >= 2) { edgeBlocked.add(ek); say(`nav: climb edge ${ek} cut (stuck twice)`); route = null; lastProg = { t: Date.now(), d: Infinity }; continue; } }
        else if (at !== undefined) stuckAt.set(at, (stuckAt.get(at) || 0) + 1);
        if (at !== undefined && stuckAt.get(at) >= 2 && at !== goal?.id) { g.blocked.add(at); say(`nav: node ${at} blocked (stuck twice)`); if (opt.stuckshots) await shot(`stuck_${at}`); route = null; }
        else if (route) ri = Math.max(0, ri - 1);
        if (unstick % 4 === 0) { if (!tgt?.thru) await tap(DIK.E); await tap(DIK.SPACE); }   // E toggles a sliding door: not at the train (play10, step 18)
        if (unstick % 6 === 0) { route = null; if (!tgt) visited.add(goal?.room); }
        lastProg = { key: aimKey, t: Date.now(), d: Infinity };
      }
    }
    await sleep(60);
  }
  await releaseAll();
  stats.secs = (Date.now() - stats.start) / 1000;
  return { reached: null, stats };
}
// Level exits (tools/ghost/exits.json, from the .ldb message chains in rea/map/ai.md): per level key, the player-collision
// triggers whose T_Activate leads to gm_init(next), and whether they wait on kills. Exploring: the nearest unvisited room.
const EXITS = JSON.parse(fs.readFileSync(new URL('./exits.json', import.meta.url)));
function pickGoal(g, s, me, visited) {
  let best = null, bd = Infinity;
  for (const [room, nodes] of g.byRoom) {
    if (visited.has(room)) continue;
    const c = nodes[Math.floor(nodes.length / 2)], d = dxz(c.w, me);
    if (d < bd) { bd = d; best = c; }
  }
  return best;
}

// Play level after level until --until-level is reached or time runs out.
async function levels() {
  const until = T0 + opt.secs * 1000;
  while (!stop && Date.now() < until) {
    const r = await play(until);
    report.runs = (report.runs || []).concat([{ level: r.stats.level, reached: r.reached, ...r.stats }]);
    if (r.reached && opt['until-level'] && r.reached === opt['until-level']) { report.result = 'reached'; return; }
    if (!r.reached) return;
  }
}

// Route player (WORKLIST #3): each map is played by its own script messages instead of by walking. Per map: invulnerable,
// F5/F9 quick save + load, then for each EXITS step C_Kill the living enemies of its kill rooms (instance names
// "::room::name", host/gamestate.c en_name) and T_Activate its trigger, then the exit trigger; one report row per map.
// Lines go through the RN_JUMP mailbox (host/jump.c rn_jump_box), which runs them at the game's input reads.
const PART1 = ['part0_level1', 'part1_level1', 'part1_level1b', 'part1_level2', 'part1_level2b', 'part1_level3', 'part1_level3b',
  'part1_level4', 'part1_level5', 'part1_level6', 'part2_level0', 'part2_level1', 'part2_level2', 'part2_level2b', 'part2_level3',
  'part2_level4', 'part3_level1', 'part3_level2', 'part3_level2b', 'part3_level3', 'part3_level4', 'part3_level5', 'part3_level5b',
  'part3_level6', 'part3_level7'];   // the whole game in order (each map's gm_init, exits.json next)
async function jump(lines, ms = 90000) {
  const seq = await page.evaluate(l => { const M = window.RN_M, b = M._rn_jump_box() >>> 2, u = M.HEAPU32;
    const t = new TextEncoder().encode(l).subarray(0, 4095); M.HEAPU8.set(t, b * 4 + 20); M.HEAPU8[b * 4 + 20 + t.length] = 0;
    const q = (u[b] + 1) >>> 0; Atomics.store(u, b, q); return q; }, lines.join('|'));
  say(`jump ${lines.join(' | ').slice(0, 300)}`);
  const lv0 = (await tick())?.level;
  for (const end = Date.now() + ms; !stop && Date.now() < end; await sleep(200)) {
    const s = await tick();
    // The level went on with lines still queued: they name this level's objects and would stall there for good, so the
    // batch is dropped (host/jump.c box.cancel) and the caller goes on to read the level the game asked for.
    if (lv0 && s && s.level && s.level !== lv0) {
      await page.evaluate(q => { const b = window.RN_M._rn_jump_box() >>> 2; Atomics.store(window.RN_M.HEAPU32, b + 4, q); }, seq);
      say(`jump: level ${lv0} -> ${s.level}, batch dropped`); return { sent: 0, failed: 0 };
    }
    const r = await page.evaluate(() => { const b = window.RN_M._rn_jump_box() >>> 2, u = window.RN_M.HEAPU32; return [u[b + 1], u[b + 2], u[b + 3]]; });
    if (r[0] === seq) return { sent: r[1], failed: r[2] };
  }
  // Not run in time (no control, level gone): cancel it, so it never parses in another level (host/jump.c box.cancel).
  await page.evaluate(q => { const b = window.RN_M._rn_jump_box() >>> 2; Atomics.store(window.RN_M.HEAPU32, b + 4, q); }, seq);
  say('jump: batch not run in time, cancelled'); return null;
}
const steady = s => s && s.mode === 3 && (s.flags & 2) && ((s.flags >> 8) & 15) === 1;
async function waitFor(pred, ms) { for (const end = Date.now() + ms; !stop && Date.now() < end; await sleep(250)) { const s = await tick(); if (s && pred(s)) return s; } return null; }
// Comics play by themselves (~50-80 s). Esc stops a playing page, Esc again exits to the game (rea/map/graphicnovel.md 3;
// Enter/Space is Play/Stop and would leave it stopped). One shot of the panel first.
async function untilLevelPlay(level, ms, row) {
  let novelT = 0, menuT = 0, switched = false, seen = null;
  for (const end = Date.now() + ms; !stop && Date.now() < end;) {
    const s = await tick();
    if (s) seen = s;
    if (s && s.mode === 2) {
      // A slow machine: the comic can end while the first Esc travels, and the second Esc then opens the game's menu.
      // Poll for the mode to leave the comic instead of probing once at a fixed 800 ms.
      if (!novelT) novelT = Date.now();
      else if (Date.now() - novelT > 4000) { await shot(`${s.level}_comic`); await tap(DIK.ESC);
        if (!await waitFor(x => x.mode !== 2, 2000)) await tap(DIK.ESC); novelT = Date.now(); }
      await sleep(250); continue;
    }
    // A menu in the middle of a load: Esc never leaves it (2026-11-04 Pod run: 141 s of Esc). The game's own way back
    // is a mode switch, sent once (host/jump.c:7).
    if (s && s.mode === 1) {
      if (!menuT) menuT = Date.now();
      else if (!switched && Date.now() - menuT > 2500) { switched = true; say('menu: s_modeswitch(game)');
        await jump(['x_modeswitch->s_modeswitch(game);']); await sleep(1500); continue; }
      await sleep(250); continue;
    }
    menuT = 0;
    if (steady(s) && (s.control & 1) && (!level || s.level === level)) return s;
    await sleep(250);
  }
  // An honest row on a timeout: the state the driver was stuck in, not a silent budget-length row.
  if (row) row.fails = [`stuck in ${MODES[seen?.mode ?? 0]} level '${seen?.level ?? '?'}'`];
  return null;
}
const trigList = () => page.evaluate(() => window.GHOST.trigs().list);
// A still point: two reads ms apart whose position moved less than eps, or null inside the budget. The save check needs
// one: a quicksave taken while the level moves the player settles somewhere else and the distance measures the ride, not
// the load (part1_level2b's lift, 1 u/s down and back up: 2.48 every run).
async function stillPoint(ms = 500, eps = 0.02, budget = 15000) {
  let a = await tick();
  for (const end = Date.now() + budget; !stop && Date.now() < end;) {
    await sleep(ms);
    const b = await tick();
    if (a && b && b.mode === 3 && Math.hypot(...b.pos.map((v, k) => v - a.pos[k])) < eps) return b;
    a = b;
  }
  return null;
}
async function routeMap(level, row) {
  let s = await tick();
  await jump(['player->c_setinvulnerable(true);']);
  // Quick save at a still point, walk a second, quick load: the spot and the state come back (F5/F9, the game's default
  // keys). p0 is the read that proved the stillness, so it is what the save holds; no still point, no claim.
  const p0s = await stillPoint();
  await tap(0x3f); await sleep(1500);            // F5
  const p0 = (p0s || await tick()).pos;
  await key(DIK.W, true); await sleep(1000); await key(DIK.W, false); await sleep(300);
  const moved = Math.hypot(...(await tick()).pos.map((v, k) => v - p0[k]));
  await tap(0x43); await sleep(700); await tap(0x43);          // "Press F9 again to Quickload" (0x44c7c7, rea/map/input.md)
  s = await untilLevelPlay(level, 60000);
  row.save = s ? +Math.hypot(...s.pos.map((v, k) => v - p0[k])).toFixed(2) : 'no play after F9';
  // The load brings back the spot and the state: position, health, weapon and rounds loaded.
  const same = !!(s && p0s) && Math.abs(s.health - p0s.health) < 0.01 && s.weapon === p0s.weapon && s.clip === p0s.clip;
  row.save_ok = p0s ? (!!s && row.save < 0.5 && moved > 0.5 && same) : null;
  say(`save/load: moved ${moved.toFixed(2)}, back to ${row.save}` + (p0s
    ? `, health ${p0s.health.toFixed(1)} -> ${s?.health.toFixed(1) ?? '-'}, weapon ${p0s.weapon} -> ${s?.weapon ?? '-'},` +
      ` clip ${p0s.clip} -> ${s?.clip ?? '-'}, ${same ? 'same' : 'differs'}`
    : ', no still point in 15 s: not measured (save_ok null)'));
  if (!s) return;
  await jump(['player->c_setinvulnerable(true);']);
  // The exit step: kill the map's kill list (true = every enemy), then its first enabled .TRIGGER; an exit that is a
  // .DO (a scripted object, no trigger) runs the map's 'send' lines instead.
  const ex = EXITS[level] || {}, steps = [...(ex.steps || []),
    { t: (ex.exit || []).filter(n => n.endsWith('.TRIGGER')), kill: ex.kill, send: ex.send, exit: true }];
  row.steps = 0; row.fails = [];
  // An exit step with nothing to send (a map that ends by its own script, e.g. part2_level1's frankie_death_timer) is
  // not a fail while the game leaves the level on its own: it is held here and recorded only if no exit was seen.
  let exitSoft = '';
  const spare = ex.spare?.names || [];        // allies whose death is a game over (EXITS spare)
  for (const st of steps) {
    s = await tick();
    if (s.level !== level) break;
    // Kill list entries: a room prefix ("::p2") or an instance name; three passes catch enemies teleported in late.
    for (let pass = 0; st.kill && pass < 3; pass++) {
      const names = s.en.filter(e => !(e.flags & 1) && e.name && !spare.includes(e.name) && (st.kill === true || st.kill.some(r => e.name === r || e.name.startsWith(r + '::')))).map(e => e.name);
      if (names.length) { await jump(names.map(n => `${n}->C_Kill(1);`)); await sleep(1500); await shot(`${level}_kills`); }
      await sleep(2000); s = await tick();
      if (s.level !== level) break;
    }
    // The step's trigger (the exit: the first of its triggers that is enabled), once the script enabled it.
    const ts = [].concat(st.t);
    let on = null;
    for (const end = Date.now() + (ts.length ? 20000 : 0); !stop && Date.now() < end && !on; await sleep(500)) {
      if ((await tick())?.level !== level) break;
      const l = await trigList(); on = ts.find(n => l.some(t => t.name === n && t.enabled));
    }
    if ((await tick())?.level !== level) break;
    if (!on && st.send) await jump(st.send);
    else if (ts.length) {
      if (!on) { row.fails.push(`${ts[0]} never enabled`); on = ts[0]; }
      if (!await jump([`${on}->T_Activate();`])) { row.fails.push(`stuck before ${on} (no player control)`); await shot(`${level}_stuck`); return; }
    }
    else exitSoft = 'exit has no trigger and no send';
    row.steps++;
    // Step script (EXITS steps `then`): lines the step needs after its trigger, verbatim through the RN_JUMP mailbox.
    // host/jump.c runs '@' lines (frame-gated input) itself, so a step that is a player action and not a trigger lives here.
    if (st.then) { say(`step ${row.steps} script: ${st.then.length} lines`); await jump(st.then, st.thenMs || 180000); }
    await sleep(1500);
    if (st.exit) break;
    await waitFor(x => x.level !== level || (steady(x) && (x.control & 1)), 90000);
  }
  // Done when the game asks for another map (GM_Init's asked level, gamestate next) or already shows it: read from the
  // game state, never from the trigger name. A menu is the end of the game; the last map's ending credits play inside the
  // map itself (::End_Credits, no level and no mode change: build/fleet/s1104/part3_level7-24/08_end.jpg), so the last map
  // still playing after its own exit step is 'end' too.
  const last = !PART1[PART1.indexOf(level) + 1];
  const seen = [];
  for (const end = Date.now() + 90000; !stop && Date.now() < end;) {
    const x = await tick();
    if (x) seen.push({ level: x.level, next: x.next, mode: x.mode });
    if (exitOf(seen, level).exit) break;
    await sleep(250);
  }
  const ended = exitOf(seen, level, { last, failed: row.fails.length > 0 });
  // The game left the level by itself with nothing to send: the map's exit happened, so it is not a fail. Only a map
  // that never leaves keeps it.
  if (!ended.exit && exitSoft) row.fails.push(exitSoft);
  row.exit = ended.exit; row.exit_from = ended.from;
}
// The exit of a map just finished, from the states seen after its exit step: the level GM_Init asked for ('next', gm+0xe0),
// the level the game shows, the menu that follows the ending, or 'end' for the last map's own credits. Nothing is taken
// from tools/ghost/exits.json: that table says what to send, not what the run did.
function exitOf(seen, level, { last = false, failed = false } = {}) {
  const nx = seen.find(s => s.next && s.next !== level);
  if (nx) return { exit: nx.next, from: 'next' };
  const lv = seen.find(s => s.level && s.level !== level);
  if (lv) return { exit: lv.level, from: 'level' };
  if (seen.some(s => s.mode === 1)) return { exit: 'end', from: 'menu' };
  const cur = seen[seen.length - 1];        // the map still playing (the credits run inside it) and no step failed
  if (last && !failed && cur && cur.mode === 3 && cur.level === level) return { exit: 'end', from: 'credits' };
  return { exit: '', from: '' };
}

// Steady play per route map (WORKLIST #5): the page's telemetry rows (web/telemetry.js, the same source tools/telemetry.py
// reads) inside the map's window, presented frames in game mode only (the loading screen is mode 'load', the comic
// 'comic'), with no save or pending save in the frame, the player alive (the death cam is a scripted camera) and the
// game's own steady flag set. fps = frames / span, p99 = the frame time at the 99th percentile, over33 = slower
// than 33 ms. A map's losts are the RN_SOUNDLOG diff across it (see sndDiff).
const TEL = path.join(path.dirname(fileURLToPath(import.meta.url)), '../../build',
  opt.port < 5200 ? 'webcheck.telemetry.ndjson' : `webcheck-${opt.port}.telemetry.ndjson`);   // tools/serve.py POST /telemetry
// The session in a telemetry file: header (columns, mode names), rows, events; a frame is the rows up to a present and its
// time the gap to the previous one. A line cut by the page closing is skipped.
function parseTelemetry(file) {
  const head = { cols: [], modes: [] }, rows = [], events = [];
  for (const l of fs.readFileSync(file, 'utf8').split('\n')) {
    if (!l.trim()) continue;
    let o; try { o = JSON.parse(l); } catch { continue; }
    if (o.rows) rows.push(...o.rows); else if (o.kind === 'session') Object.assign(head, o); else events.push(o);
  }
  const C = Object.fromEntries(head.cols.map((x, i) => [x, i])), col = (r, x) => (C[x] === undefined ? 0 : r[C[x]]);
  const frames = []; let lastT = null, after = false;
  for (const r of rows) if (col(r, 'present')) { const t = col(r, 't');
    // A frame's time is the gap to the previous present. A t that does not advance is not a frame: a repeated present,
    // or a second page clock in the same file (a reload: its t restarts, which used to put whole -s gaps in the span).
    // after: the previous frame wrote a save or had a pending action, so this one ends a load or a save, not play.
    if (lastT !== null && t > lastT) frames.push({ t, dt: t - lastT, mode: col(r, 'mode'), saves: col(r, 'saves'),
      pending: col(r, 'pending'), flags: col(r, 'pflags'), after });
    lastT = t; after = !!(col(r, 'saves') || col(r, 'pending')); }
  return { frames, game: head.modes.indexOf('game'),
    landed: events.filter(e => e.kind === 'mode' && e.mode === 'game').map(e => e.t) };   // comic/load/menu -> game
}
// One map's steady play: presented frames in the map's window, in game mode only (the loading screen is mode 'load', the
// comic 'comic'), with no save or pending save in the frame, the player alive (the death cam is a scripted camera) and
// the game's own steady flag on: flags bits 8-11 are the mode switch transition, 1 = steady, 2 = fade in (host/gamestate.c
// 26, from [ms+0x0d] 0x40b3a2). The level's first frame (the pipeline compile, ~350 ms) and the resume after the route's
// own F9 present inside a fade, so the game drops them and not a guess of ours; a frame in real play is always counted, so
// a genuine hitch still fails the map. Also dropped: the frame that ends a save or a pending action (tel.frames' 'after',
// WORKLIST #5: a load and an autosave are not play). part2_level2b 19.02 and part3_level5 35.86 were exactly those: no
// upload, no page time, and the F9's own load 0.2 s before. fps = frames / span, p99 = the frame time at the 99th
// percentile, over33 = slower than 33 ms. c: page ms = ghost ms + c, both in ms (page.evaluate(performance.now()) and
// Date.now() - T0).
function steadyRow(row, tel, c) {
  const t0 = row.t * 1000 + c, t1 = (row.t + row.secs) * 1000 + c;
  const win = tel.frames.filter(f => f.t >= t0 && f.t <= t1 && f.mode === tel.game && !f.saves && !f.pending && !f.after
    && !(f.flags & 4) && ((f.flags >> 8) & 15) === 1);
  row.steady = 0; row.fps = null; row.p99 = null; row.over33 = null;
  if (win.length > 1) {
    const d = win.map(f => f.dt).sort((a, b) => a - b), span = d.reduce((a, b) => a + b, 0) / 1000;
    row.steady = +span.toFixed(1); row.fps = +(win.length / span).toFixed(1);
    row.p99 = +d[Math.min(d.length - 1, Math.floor(0.99 * d.length))].toFixed(1);
    row.over33 = d.filter(x => x > 33).length;
    // GATE60 (RE-NFSU tools/bench.py): p50 at the game's 60 Hz pace and not one played frame over 33.4 ms (two vsyncs).
    row.p50 = +d[d.length >> 1].toFixed(1); row.max = +d[d.length - 1].toFixed(1);
    row.gate60 = row.p50 <= 16.75 && row.max <= 33.4;
  }
  return win.length;
}
async function steadyMaps() {
  const c = await page.evaluate(() => performance.now()) - (Date.now() - T0);   // page ms = ghost ms + c (never * 1000)
  const tel = parseTelemetry(TEL);
  for (const row of report.maps) {
    steadyRow(row, tel, c);
    // Green = the map loaded, no abort, its exit reached, the F5/F9 save came back, 59.5 fps or better in the steady
    // window, not one Play the host dropped, no COM error and no ring drop. A route map exits in 12-50 s of steady play,
    // so 60 s of steady time is not a requirement of its own: the goal is alive until the exit (no death in the map) and
    // the exit, OR the 60 s of steady play. The steady seconds stay in the row.
    row.green = !!(row.loaded && !row.abort && row.exit && row.save_ok && row.fps >= 59.5 && row.lost === 0
      && (row.sound?.drops ?? 0) === 0 && (row.steady >= 60 || row.deaths === 0));
    say(`steady ${row.level}: ${row.steady} s, ${row.fps ?? '-'} fps, p99 ${row.p99 ?? '-'} ms, ${row.over33 ?? '-'} over 33, ` +
      `lost ${row.lost} (host plays), drops ${row.sound?.drops ?? 0}, deaths ${row.deaths ?? '-'}, exit '${row.exit || '-'}'` +
      `${row.green ? ', green' : ''}`);
  }
}

// GATE60 per played level (--plan play): steadyRow over each level's window. Valid only on a quiet Mac: the 5-minute load
// average at the end must be under 6 (RE-NFSU tools/bench.py), else the run measured the machine, not the game.
// GATE50 (OWNER 2026-11-18): end to end, every presented frame of the level's window counts (load, comic, saves, death
// cams, fades): no frame over 33.4 ms (two vsyncs) and no 1 s window with fewer than 50 frames. GATE60 above stays the
// steady-play view.
function gate50Row(row, tel, c) {
  const t0 = row.t * 1000 + c, t1 = (row.t + row.secs) * 1000 + c;
  const win = tel.frames.filter(f => f.t >= t0 && f.t <= t1);
  if (win.length < 2) return { gate50: false };
  let minFps = Infinity;
  for (let i = 0, j = 0; i < win.length; i++) {        // frames whose end lies in (t - 1000, t]
    while (win[j].t <= win[i].t - 1000) j++;
    if (win[i].t - t0 >= 1000) minFps = Math.min(minFps, i - j + 1);
  }
  const over = win.filter(f => f.dt > 33.4), max = Math.max(...win.map(f => f.dt));
  return { minFps1s: minFps === Infinity ? null : minFps, max50: +max.toFixed(1), over33all: over.length,
    gate50: over.length === 0 && minFps >= 50 };
}
async function gate60Runs() {
  const c = await page.evaluate(() => performance.now()) - (Date.now() - T0), tel = parseTelemetry(TEL);
  report.load5 = +os.loadavg()[1].toFixed(1);
  for (const r of report.runs) {
    const row = { t: (r.start - T0) / 1000, secs: r.secs };
    steadyRow(row, tel, c);
    Object.assign(r, { steady: row.steady, fps: row.fps, p50: row.p50, p99: row.p99, max: row.max, over33: row.over33, gate60: row.gate60 });
    Object.assign(r, gate50Row(row, tel, c));
    say(`GATE50 ${r.level}: ${r.gate50 ? 'PASS' : 'FAIL'} min ${r.minFps1s ?? '-'} fps in 1 s, max ${r.max50 ?? '-'} ms, ${r.over33all ?? '-'} over 33.4 (load and comics included)`);
    say(`GATE60 ${r.level}: ${r.gate60 ? 'PASS' : 'FAIL'} steady ${row.steady} s, ${row.fps ?? '-'} fps, p50 ${row.p50 ?? '-'} p99 ${row.p99 ?? '-'} max ${row.max ?? '-'} ms, ${row.over33 ?? '-'} over 33`);
  }
  say(`GATE60 load ${report.load5} (valid < 6): ${report.runs.filter(r => r.gate60).length}/${report.runs.length} levels pass`);
  say(`GATE50 load ${report.load5} (valid < 6): ${report.runs.filter(r => r.gate50).length}/${report.runs.length} levels pass`);
}

// Plans. Menu clicks are CSS pixels of the 1280x900 page: unlocked, web/input.js moves the game's cursor 1:1 with the
// pointer (rea/map/input.md 3), so a page click lands where the menu draws the item.
const plans = {
  // #24: reuse the existing rooftop route, then observe the original ending without sending credits/menu messages.
  // Camera_01 animation 2 runs 218 seconds (LDB byte 24014815); its end sends menu at byte 24014929.
  async final() {
    if (!await waitFor(s => s.mode === 1 && ((s.flags >> 8) & 15) === 1, 120000)) return;
    await page.evaluate(() => {
      window.__finalDone = false;
      MPFF.run('part3_level7', 10).then(() => { window.__finalDone = true; });
    });
    let key = '', shotAt = 0, entered = false;
    while (!stop && Date.now() - T0 < opt.secs * 1000) {
      const s = await tick();
      const route = await page.evaluate(() => ({ step: MPFF.state.step, state: MPFF.state.state, done: !!window.__finalDone }));
      if (s?.mode === 3 && s.level === 'part3_level7') entered = true;
      const k = `${s?.mode}/${route.step}/${route.state}`;
      if (k !== key || Date.now() - shotAt >= 20000) {
        say(`final ${JSON.stringify({ mode: s?.mode, level: s?.level, frame: s?.frame, control: s?.control, ...route })}`);
        await shot(`final_step${route.step}_mode${s?.mode}`); key = k; shotAt = Date.now();
      }
      if (entered && route.step === 10 && route.done && s?.mode === 1 && ((s.flags >> 8) & 15) === 1) {
        await phase('returned_main_menu', s); report.result = 'reached'; return;
      }
      await sleep(250);
    }
  },
  async resume() {   // Resume Game (focused when the port has a save) -> the latest save, then calibrate in play
    let s;
    while (!stop && !((s = await tick())?.mode === 1 && ((s.flags >> 8) & 15) === 1)) await sleep(250);
    await phase('menu', s); await sleep(1000);
    await tap(DIK.ENTER);
    await levels();
  },
  // With --env RN_JUMP=... (host/jump.c) the run starts at a level or a point inside it; watch records it.
  // Trigger changes (enabled / fired) are logged once a second: the level script's progress under the RN_JUMP lines.
  async watch() { const was = new Map(); let dumped = false;
    let captureFrame = -1, captures = 0, captureClip = 0;
    for (let k = 0; !stop && Date.now() - T0 < opt.secs * 1000; k++) { await tick(); await sleep(+opt['camera-frames'] > 0 ? 16 : 100);
      // --camera-frames N: bounded cinematic evidence, keyed by presented game frames.
      if (last?.weapon === 15) captureClip = Math.max(captureClip, last.clip);
      if (+opt['camera-frames'] > 0 && last?.weapon === 15 && (captures === 0 || last.clip < captureClip || (last.control & 2))
          && last.frame - captureFrame >= +opt['camera-frames'] && captures < 180) {
        captureFrame = last.frame; captures++;
        say(`camera f${last.frame} speed ${last.speed} fov ${last.fov} control ${last.control} clip ${last.clip}`);
        await shot(`camera_f${last.frame}`);
      }
      if (k % 10) continue;
      const list = await trigList().catch(() => []);
      // --dump <regex>: once, every matching trigger (room, position in its room frame, radius, type, enabled/fired).
      if (opt.dump && !dumped && list.length) { dumped = true; const re = new RegExp(opt.dump);
        for (const t of list) if (re.test(t.name)) say(`dump ${t.name} room ${t.room} pos ${t.pos.map(v => v.toFixed(2))} r ${t.r.toFixed(2)} type ${t.type} en ${t.enabled}/${t.fired}`); }
      for (const t of list) { const v = `${t.enabled}/${t.fired}`; if (was.has(t.name) && was.get(t.name) !== v) say(`trigger ${t.name} en/fired ${was.get(t.name)} -> ${v}`); was.set(t.name, v); } } },
  // --from <level> [--to <level>]: from the menu, load the map and play the route map after map (Part I order).
  async route() {
    report.maps = [];
    let s = await waitFor(x => x.mode === 1 && ((x.flags >> 8) & 15) === 1, 120000);
    if (!s) return;
    const from = opt.from || PART1[0], to = opt.to || PART1[PART1.length - 1];
    await jump([`maxpayne_gamemode->gm_init(${from});`, 'x_modeswitch->s_modeswitch(game);']);
    let level = from;
    while (!stop && Date.now() - T0 < opt.secs * 1000) {
      const row = { level, t: +((Date.now() - T0) / 1000).toFixed(1), green: false };
      const deaths0 = report.deaths;               // deaths inside this map's own window (steadyMaps' green rule)
      const snd0 = await sndSnap(), stop0 = stop;
      report.maps.push(row);
      s = await untilLevelPlay(level, 180000, row);
      row.loaded = !!s;
      if (s) { await shot(`${level}_play`); await routeMap(level, row); }
      row.deaths = report.deaths - deaths0;
      row.secs = +((Date.now() - T0) / 1000 - row.t).toFixed(1);
      row.sound = sndDiff(snd0, await sndSnap()); row.lost = row.sound.lost;
      row.abort = stop && stop !== stop0 ? String(stop).slice(0, 200) : '';
      say(`map ${JSON.stringify(row)}`);
      fs.writeFileSync(path.join(opt.out, 'report.json'), JSON.stringify(report, null, 1));
      if (level === to) { report.result = 'reached'; return; }
      const nx = PART1[PART1.indexOf(level) + 1];
      if (!nx) return;
      if (row.exit !== nx) {                       // the route did not reach it: load it and go on
        row.next_forced = nx;
        // From the menu gm_init only queues the level; s_modeswitch(game) is what enters it (host/jump.c:7-9).
        await jump([`maxpayne_gamemode->gm_init(${nx});`, 'x_modeswitch->s_modeswitch(game);']);
      }
      level = nx;
    }
  },
  // --from <level>: from the menu, load the map and play it by walking and shooting (play()) until the map's next one
  // (exits.json next) or time runs out: the real-play test the route plan does not do.
  async play() {
    const s = await waitFor(x => x.mode === 1 && ((x.flags >> 8) & 15) === 1, 120000);
    if (!s) return;
    const from = opt.from || PART1[0];
    opt['until-level'] = opt['until-level'] || EXITS[from]?.next || PART1[PART1.indexOf(from) + 1];
    await jump([`maxpayne_gamemode->gm_init(${from});`, 'x_modeswitch->s_modeswitch(game);']);
    refillDue = !!opt.rescue;   // a direct test start has no previous level's loadout; count the same injected arrival refill
    await levels();
  },
  async newgame() {
    let s;
    // The menu is up and the mode switch is steady (flags bits 8-11 = [ms+0x0d], 1 = no fade).
    while (!stop && !((s = await tick())?.mode === 1 && ((s.flags >> 8) & 15) === 1)) await sleep(250);
    await phase('menu', s);
    await sleep(1000);
    // Keyboard menu: the menu container 0x6354e0 takes VKs from the typed-key ring (WH_KEYBOARD hook 0x518360; host:
    // key_event -> WM_KEYDOWN -> hook). Arrows and Enter are in its VK table 0x635680. Focus starts on Tutorial, or on
    // Resume Game when the port's save folder has a save.
    const saves = path.join(path.dirname(opt.game), `save-${opt.port}`);
    const hasSave = fs.existsSync(saves) && fs.readdirSync(saves, { recursive: true }).some(f => /\.mps$/i.test(String(f)));
    for (let k = 0; k < (hasSave ? 2 : 1); k++) await tap(DIK.DOWN);
    await tap(DIK.ENTER);                                                         // New Game -> difficulty list
    await sleep(1200);
    await tap(DIK.ENTER);                                                         // Fugitive (normal), focused first
    await levels();
  },
};
try { await plans[opt.plan](); } catch (e) { say('driver error: ' + e.stack); stop = stop || e.message; }
const s = await read().catch(() => null);
await shot('end').catch(() => {});
if (report.result !== 'reached') report.result = stop ? 'stopped' : 'timeout';
report.stop = stop;
report.end = s && { mode: MODES[s.mode], level: s.level, pos: s.pos, health: s.health, alive: s.alive, enemies: s.enemies };
if (report.maps?.length) await steadyMaps().catch(e => say('steady: ' + e.message));
if (report.runs?.length) await gate60Runs().catch(e => say('gate60: ' + e.message));
report.secs = +((Date.now() - T0) / 1000).toFixed(1);
fs.writeFileSync(path.join(opt.out, 'report.json'), JSON.stringify(report, null, 1));
say(`report: ${report.result} after ${report.secs} s, end ${JSON.stringify(report.end)}`);
if (opt.cpuprofile) await cpuProfileEnd();
await browser.close();
if (server) server.kill();
