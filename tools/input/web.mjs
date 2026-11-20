// One headless run of the web build for tools/input/input.sh: same launch as tools/webgate.mjs (Playwright, headless,
// muted, Metal, one persistent profile per port so Chrome keeps the compiled wasm), plus the one thing webgate cannot do:
// inject mouse COUNTS at the moment the game logs a MARK of the '@' scenario (host/jump.c).
// web/input.js turns a mousemove into ring word [2] (host/input.c input_poll -> dinput8_motion), so the movementX we send
// is divided by the factor the page applies (MOUSE with the pointer locked, 512/sw in the 640x480 menu path, web/input.js:84)
// and the count that reaches the game is exactly what the native run gets from RN_MOUSE.
// Usage: node tools/input/web.mjs <url> <secs> <shot.png> <mark:dx:dy,...>
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const url = process.argv[2] || 'http://127.0.0.1:5192/';
const secs = Number(process.argv[3] || 180);
const shot = process.argv[4] || 'out/input/web.png';
const mks = (process.argv[5] || '').split(',').filter(Boolean).map(s => {
  const [n, dx, dy] = s.split(':');
  return [n, Number(dx), Number(dy)];
});
const mac = process.platform === 'darwin';
const args = (process.env.RN_GPU_ARGS || (mac ? '--use-angle=metal --ignore-gpu-blocklist --enable-unsafe-webgpu'
  : '--use-angle=vulkan --enable-features=Vulkan,VulkanFromANGLE,DefaultANGLEVulkan --use-vulkan=native --ignore-gpu-blocklist --enable-unsafe-webgpu')).split(' ');
const lean = ['--mute-audio', '--no-first-run', '--disable-extensions', '--disable-sync', '--disable-default-apps',
  '--disable-component-update', '--disable-background-networking', '--disable-features=Translate,MediaRouter,OptimizationHints'];
const profile = `build/chrome-gate-${new URL(url).port || 80}`;
const browser = await chromium.launchPersistentContext(profile, { headless: true, args: [...args, ...lean],
  executablePath: process.env.RN_CHROME || undefined, viewport: { width: 1280, height: 900 } });
const page = browser.pages()[0] || await browser.newPage();
let stop = false, endAt = 0;
const t0 = Date.now();

// Aim pitch: word 344 of the snapshot host/gamestate.c publishes (f32 [[C+0x498]+0x20], which is the accumulator that
// FUN_00725b30 clamps to +-1.5533 rad and FUN_00725be0 returns, rea/c/MaxPayne.exe/00725b30.c, move_oracle.md 2).
// It is in RADIANS (host/gamestate.c:53); the log prints degrees so the compare and the native print speak the same unit.
// The native build has no reader for it: RN_GHOST prints pos/fwd/st/an/vel only (host/gamestate.c:439).
const pitch = () => page.evaluate(() => {
  const M = window.RN_M;
  if (!M || !M._rn_gamestate_addr) return null;
  const b = M._rn_gamestate_addr() >>> 2, f = new Float32Array(M.HEAPU32.buffer);
  return f[b + 344] * 180 / Math.PI;   // word 344 is radians; WMARK is degrees
});

const inject = (dx, dy) => page.evaluate(([dx, dy]) => {
  const c = document.querySelector('canvas');
  if (!c) return 'no canvas';
  const mo = +(new URLSearchParams(location.search).get('mouse') || 0.4);
  const locked = document.pointerLockElement === c;
  const r = c.getBoundingClientRect(), fit = Math.min(r.width / c.width, r.height / c.height);
  const sh = c.height * fit, sw = Math.min(c.width * fit, sh * 4 / 3);
  c.dispatchEvent(new MouseEvent('mousemove', { bubbles: true,
    movementX: dx / (locked ? mo : 512 / sw), movementY: dy / (locked ? mo : 384 / sh) }));
  return `locked=${locked} counts=${dx},${dy}`;
}, [dx, dy]);

page.on('console', m => {
  const s = m.text();
  console.log(s);
  if (/^wasm abort|RuntimeError/.test(s)) stop = true;
  const mk = /^MARK (\S+)$/.exec(s);
  if (!mk) return;
  const name = mk[1];
  pitch().then(v => console.log(`WMARK ${name} ${v === null ? 'n/a' : v.toFixed(2)}`)).catch(() => {});
  const hit = mks.find(k => k[0] === name);
  if (hit) inject(hit[1], hit[2]).then(r => console.log(`WMOUSE ${name} ${r}`)).catch(e => console.log(`WMOUSE ${name} error ${e.message}`));
  if (name === 'end') endAt = Date.now();
});
page.on('pageerror', e => { console.log('pageerror: ' + e.message); stop = true; });
await page.goto(url);
await page.mouse.click(640, 450);       // canvas focus + the user gesture pointer lock needs (webgate does the same)
while (!stop && Date.now() - t0 < secs * 1000) {
  if (endAt && Date.now() - endAt > 1500) break;
  await new Promise(r => setTimeout(r, 100));
}
await new Promise(r => setTimeout(r, 500));       // the log lines after the last MARK
await page.screenshot({ path: shot });
console.log(`web.mjs: ${stop ? 'stopped' : 'done'} after ${((Date.now() - t0) / 1000).toFixed(1)} s, screenshot ${shot}`);
await browser.close();
