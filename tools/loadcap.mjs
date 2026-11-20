// One cold visit of the local dist, headless and muted: seconds from navigation to the game's own main menu, every request
// of the page and its workers with its times, and a shot at the end. Ported from RE-NFSU tools/loadcap.mjs.
// Usage: RN_RTT_MS=80 python3 tools/static_server.py 5254 build/public & node tools/loadcap.mjs <url> [out prefix]
//   -> <out>.json (summary + rows: path, Range, start, ttfb, end, bytes), <out>.png.
// The menu is the game's own (host/gamestate.c mode 1, published from Present), so the URL gets ?nolauncher=1: the web
// launcher covers the game, and a visitor's click is not part of a load. RN_SECS bounds the run (300).
// RN_PROFILE=<dir> keeps the profile on disk (a second visit is the warm load); the default is a fresh private one.
// RN_CDP=<port> one CDP port per slice (9391); RN_URL_EXTRA=<query> is appended to the URL.
import { spawn } from 'node:child_process';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const SECS = Number(process.env.RN_SECS || 300);
// RN_UNTIL_MODE=2: stop at the first comic or level (a ?level= entry) instead of the menu (host/gamestate.c mode word).
const UNTIL = Number(process.env.RN_UNTIL_MODE || 1);
const port = Number(process.env.RN_CDP || 9391);
const KEEP = process.env.RN_PROFILE, profile = KEEP || mkdtempSync(join(tmpdir(), 'mp-loadcap-'));
let url = process.argv[2] || 'http://127.0.0.1:5254/';
if (!/[?&](nolauncher|level|go|env)=/.test(url)) url += (url.includes('?') ? '&' : '?') + 'nolauncher=1';
if (process.env.RN_URL_EXTRA) url += (url.includes('?') ? '&' : '?') + process.env.RN_URL_EXTRA;
const out = process.argv[3] || 'out/live-web2/load';

const chrome = spawn('/Users/angeldamian/.local/bin/chrome-bot', ['--headless=new', '--mute-audio', '--enable-unsafe-webgpu',
  '--use-angle=metal', '--ignore-gpu-blocklist', '--no-first-run', '--disable-extensions', '--disable-sync',
  '--disable-component-update', '--disable-background-networking', `--remote-debugging-port=${port}`,
  `--user-data-dir=${profile}`, 'about:blank'], { stdio: 'ignore' });
process.on('exit', () => { chrome.kill('SIGKILL'); if (!KEEP) try { rmSync(profile, { recursive: true, force: true, maxRetries: 10, retryDelay: 200 }); } catch (e) { console.log('left behind: ' + profile); } });
const sleep = ms => new Promise(r => setTimeout(r, ms));

let info;
for (let i = 0; i < 50 && !info; i++) { try { info = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json(); } catch { await sleep(200); } }
if (!info) { console.error('no CDP on ' + port); process.exit(1); }
const ws = new WebSocket(info.webSocketDebuggerUrl);
await new Promise(r => ws.onopen = r);
let seq = 0; const pending = new Map();
const send = (method, params = {}, sessionId) => new Promise((resolve, reject) => {
  const id = ++seq; pending.set(id, { resolve, reject, method }); ws.send(JSON.stringify({ id, method, params, sessionId }));
});
let event = () => {};
ws.onmessage = e => { const m = JSON.parse(e.data); if (pending.has(m.id)) {
  const p = pending.get(m.id); pending.delete(m.id); m.error ? p.reject(new Error(p.method + ': ' + JSON.stringify(m.error))) : p.resolve(m.result);
} else event(m); };
setTimeout(() => { console.error('timeout'); process.exit(1); }, (SECS + 40) * 1000).unref();

const { browserContextId } = KEEP ? {} : await send('Target.createBrowserContext');
const { targetId } = await send('Target.createTarget', { url: 'about:blank', browserContextId });
const { sessionId: page } = await send('Target.attachToTarget', { targetId, flatten: true });
const net = new Map(), cons = [];
let t0 = Date.now(); const now = () => (Date.now() - t0) / 1000;
async function configure(s) {
  await send('Network.enable', {}, s).catch(() => {});
  await send('Runtime.enable', {}, s).catch(() => {});
  if (s === page) await send('Target.setAutoAttach', { autoAttach: true, flatten: true, waitForDebuggerOnStart: true }, s).catch(() => {});
}
event = m => {
  const p = m.params, k = `${m.sessionId}:${p?.requestId}`;
  if (m.method === 'Target.attachedToTarget') configure(p.sessionId).then(() => send('Runtime.runIfWaitingForDebugger', {}, p.sessionId)).catch(() => {});
  if (m.method === 'Network.requestWillBeSent') net.set(k, { path: new URL(p.request.url).pathname, range: p.request.headers.Range || p.request.headers.range,
    who: m.sessionId === page ? 'page' : 'worker', start: now(), bytes: 0 });
  if (m.method === 'Network.responseReceived' && net.has(k)) Object.assign(net.get(k), { status: p.response.status, ttfb: now(), fromCache: p.response.fromDiskCache || p.response.fromPrefetchCache || false });
  if (m.method === 'Network.loadingFinished' && net.has(k)) Object.assign(net.get(k), { bytes: p.encodedDataLength, end: now() });
  if (m.method === 'Network.loadingFailed' && net.has(k)) net.get(k).error = p.errorText;
  if (m.method === 'Runtime.consoleAPICalled') cons.push([now(), p.args.map(a => a.value ?? a.description ?? '').join(' ').slice(0, 300)]);
};
await configure(page);
if (!process.env.RN_NOCLEAR) await send('Network.clearBrowserCache', {}, page);
await send('Emulation.setDeviceMetricsOverride', { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false }, page);

// Mode word of host/gamestate.c (b + 3; 1 = the game's own menu), read under its seqlock.
const MODE = `(() => { const M = window.RN_M; if (!M || !M._rn_gamestate_addr) return -1;
  const b = M._rn_gamestate_addr() >>> 2, u = M.HEAPU32, s0 = Atomics.load(u, b + 1);
  if (s0 & 1) return -2; const m = u[b + 3]; return Atomics.load(u, b + 1) === s0 ? m : -2; })()`;

t0 = Date.now();
await send('Page.navigate', { url }, page);
const modes = [];
let menu = null, stop = null;
while (now() < SECS) {
  await sleep(200);
  const r = await send('Runtime.evaluate', { expression: MODE, returnByValue: true }, page).catch(() => null);
  const mode = r?.result?.value;
  if (typeof mode === 'number' && mode >= 0) {
    const last = modes[modes.length - 1];
    if (!last || last[1] !== mode) modes.push([+now().toFixed(2), mode]);
    if (mode >= UNTIL && menu === null) { menu = +now().toFixed(3); break; }
  }
  const log = (await send('Runtime.evaluate', { expression: "document.getElementById('log')?.textContent || ''", returnByValue: true }, page).catch(() => null))?.result?.value || '';
  const bad = /STOP|JS rejection|JS error/.exec(log);
  if (bad) { stop = log.slice(Math.max(0, bad.index - 200), bad.index + 300); break; }
}
await sleep(1000);   // one second at the menu: the shot is of the menu, not of the fade into it
const { data } = await send('Page.captureScreenshot', { format: 'png' }, page);
writeFileSync(out + '.png', Buffer.from(data, 'base64'));
const rows = [...net.values()].filter(r => r.end !== undefined || r.error);
const sum = (f, list = rows) => list.reduce((a, r) => a + f(r), 0);
const game = rows.filter(r => r.path.startsWith('/game/'));
const summary = {
  url, menuSeconds: menu, stop, modes,
  requests: rows.length, gameRequests: game.length,
  bytes: sum(r => r.bytes), gameBytes: sum(r => r.bytes, game), gameWait: sum(r => (r.ttfb || r.end) - r.start, game),
  requestsWithRange: rows.filter(r => r.range).length, fromCache: rows.filter(r => r.fromCache).length,
  errors: rows.filter(r => r.error).map(r => [r.path, r.error]),
  endSeconds: +now().toFixed(2),
};
writeFileSync(out + '.json', JSON.stringify({ summary, rows, cons }, null, 1));
console.log(JSON.stringify(summary));
process.exit(0);
