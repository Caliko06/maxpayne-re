// One headless Chromium run of the page (Playwright): prints the page log until a STOP line or the timeout, then
// saves a screenshot. Usage: node tools/webgate.mjs [url] [seconds] [shot.png]
// Always headless and muted: no window, no Dock icon, no sound (OWNER rule). Mac: Playwright's chromium-headless-shell (a bare
// binary, no browser UI; `npx playwright install chromium-headless-shell`) on Metal; Vulkan flags there fall to SwiftShader
// and lose the WebGPU instance. Pod: Vulkan for its NVIDIA card. RN_GPU_ARGS / RN_CHROME override.
// The profile in build/chrome-gate-<port> persists so Chrome's code cache keeps the compiled 238 MB wasm between runs.
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const url = process.argv[2] || 'http://127.0.0.1:5181/';
const secs = Number(process.argv[3] || 120);
const shot = process.argv[4] || 'build/webgate.png';
// A 5th argument scripts input: "15:Enter,40:ArrowDown" taps that KeyboardEvent.code at that second; "14:click@365x354"
// clicks the page at x,y (CSS px). A key press clicks the canvas first (focus, pointer lock); "~KeyS" does not.
// RN_TOUCH=1: a touch screen (coarse pointer, landscape phone viewport), "14:tap@365x354" taps, "14:hold@100x300+160x300" holds a
// finger at the first point, slides it to the second and keeps it there 1.5 s (web/touch.js).
const keys = (process.argv[5] || '').split(',').filter(Boolean).map(k => { const [s, code] = k.split(':'); return [Number(s) * 1000, code]; });
const mac = process.platform === 'darwin';
const args = (process.env.RN_GPU_ARGS || (mac ? '--use-angle=metal --ignore-gpu-blocklist --enable-unsafe-webgpu'
  : '--use-angle=vulkan --enable-features=Vulkan,VulkanFromANGLE,DefaultANGLEVulkan --use-vulkan=native --ignore-gpu-blocklist --enable-unsafe-webgpu')).split(' ');
const lean = ['--mute-audio', '--no-first-run', '--disable-extensions', '--disable-sync', '--disable-default-apps',
  '--disable-component-update', '--disable-background-networking', '--disable-features=Translate,MediaRouter,OptimizationHints'];
const profile = `build/chrome-gate-${new URL(url).port || 80}`;
const touch = !!process.env.RN_TOUCH;
const browser = await chromium.launchPersistentContext(profile, { headless: true, args: [...args, ...lean],
  executablePath: process.env.RN_CHROME || undefined, viewport: process.env.RN_VIEW ? Object.fromEntries(['width', 'height'].map((k, i) => [k, +process.env.RN_VIEW.split('x')[i]]))   // RN_VIEW=390x844: a portrait phone
    : touch ? { width: 844, height: 390 } : { width: 1280, height: 900 },
  hasTouch: touch, isMobile: touch,
  // RN_HEADERS='{"CF-Access-Client-Id":..}': pass a Cloudflare Access gate (none on maxpayne.cc since 2026-11-19).
  extraHTTPHeaders: process.env.RN_HEADERS ? JSON.parse(process.env.RN_HEADERS) : undefined });
const page = browser.pages()[0] || await browser.newPage();
let stop = false;
const t0 = Date.now();
page.on('console', m => { const s = m.text(); console.log(s); if (/^STOP|wasm abort|RuntimeError/.test(s)) stop = true; });
page.on('pageerror', e => { console.log('pageerror: ' + e.message); stop = true; });
await page.goto(url);
page.on('console', m => { if (/^STOP/.test(m.text())) stop = true; });
// Optional bounded scenario for launcher choices and read-only guest-memory evidence.
if (process.env.RN_GATE_SCRIPT) {
  let timer;
  try {
    const scenario = (await import(new URL('../' + process.env.RN_GATE_SCRIPT, import.meta.url))).default;
    await Promise.race([scenario(page), new Promise((_, reject) => { timer = setTimeout(() => reject(new Error('webgate scenario timeout')), Math.max(1, secs * 1000 - (Date.now() - t0))); })]);
  } catch (e) { await browser.close(); throw e; }
  finally { clearTimeout(timer); }
}

if (!touch) await page.mouse.click(640, 450);       // the canvas takes focus, so keys reach the game
const cdp = touch ? await browser.newCDPSession(page) : null;
while (!stop && Date.now() - t0 < secs * 1000) {
  while (keys.length && Date.now() - t0 >= keys[0][0]) { const [, code] = keys.shift();
    const m = /^click@(\d+)x(\d+)$/.exec(code);
    if (m) { await page.mouse.click(+m[1], +m[2]); console.log(`click ${m[1]},${m[2]}`); continue; }
    const t = /^tap@(\d+)x(\d+)$/.exec(code);
    if (t) { await page.touchscreen.tap(+t[1], +t[2]); console.log(`tap ${t[1]},${t[2]}`); continue; }
    const g = /^hold@(\d+)x(\d+)\+(\d+)x(\d+)$/.exec(code);
    if (g) {
      const at = (x, y) => [{ x, y, id: 1 }];
      await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: at(+g[1], +g[2]) });
      await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: at(+g[3], +g[4]) });
      await new Promise(r => setTimeout(r, 1500));
      await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
      console.log(`hold ${code}`); continue;
    }
    // "~KeyS" taps the key without the focusing click (a click would also press the game's mouse button 0 in a menu).
    const bare = code.startsWith('~'); if (!bare) await page.mouse.click(640, 450);
    await page.keyboard.press(bare ? code.slice(1) : code, { delay: 80 }); console.log(`key ${code}`); }
  await new Promise(r => setTimeout(r, 100));
}
await new Promise(r => setTimeout(r, 1500));       // the log lines after the STOP
await page.screenshot({ path: shot });
console.log(`webgate: ${stop ? 'stopped' : 'timeout'} after ${((Date.now() - t0) / 1000).toFixed(1)} s, screenshot ${shot}`);
await browser.close();
