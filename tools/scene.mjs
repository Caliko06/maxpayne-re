// One headless Chromium run of the page with periodic screenshots, to judge a scripted scene (boss, death cams, credits).
// Usage: node tools/scene.mjs <url> <seconds> <out dir> [shot every s (default 6)] [stop regex on the page log]
// Same launch as tools/webgate.mjs (headless shell, muted, no window; own profile per port). The scene itself is an RN_JUMP list in the
// url (?env=RN_JUMP=...,RN_SOUNDLOG=1; host/jump.c). Writes <out>/NN_<seconds>s.jpg and <out>/page.log.
import { createRequire } from 'node:module';
import fs from 'node:fs';
const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const [url, secsArg, out, everyArg, stopRe] = process.argv.slice(2);
const secs = Number(secsArg || 120), every = Number(everyArg || 6) * 1000;
fs.mkdirSync(out, { recursive: true });
const mac = process.platform === 'darwin';
const args = (process.env.RN_GPU_ARGS || (mac ? '--use-angle=metal --ignore-gpu-blocklist --enable-unsafe-webgpu'
  : '--use-angle=vulkan --enable-features=Vulkan,VulkanFromANGLE,DefaultANGLEVulkan --use-vulkan=native --ignore-gpu-blocklist --enable-unsafe-webgpu')).split(' ');
const lean = ['--mute-audio', '--no-first-run', '--disable-extensions', '--disable-sync', '--disable-default-apps',
  '--disable-component-update', '--disable-background-networking', '--disable-features=Translate,MediaRouter,OptimizationHints'];
const browser = await chromium.launchPersistentContext(`build/chrome-gate-${new URL(url).port || 80}`, { headless: true, args: [...args, ...lean],
  executablePath: process.env.RN_CHROME || undefined, viewport: { width: 1280, height: 900 } });
const page = browser.pages()[0] || await browser.newPage();
const log = fs.createWriteStream(`${out}/page.log`);
let stop = null;
const t0 = Date.now(), re = stopRe ? new RegExp(stopRe) : null;
page.on('console', m => { const s = m.text(); log.write(`${((Date.now() - t0) / 1000).toFixed(1)} ${s}\n`);
  if (/^STOP|wasm abort|RuntimeError/.test(s)) stop = s; else if (re && re.test(s)) stop = 'matched ' + s; });
page.on('pageerror', e => { log.write(`pageerror: ${e.message}\n`); stop = 'pageerror ' + e.message; });
await page.goto(url);
await page.mouse.click(640, 450);
let n = 0, next = every;
while (!stop && Date.now() - t0 < secs * 1000) {
  await new Promise(r => setTimeout(r, 250));
  if (Date.now() - t0 >= next) { next += every;
    await page.screenshot({ path: `${out}/${String(++n).padStart(2, '0')}_${Math.round((Date.now() - t0) / 1000)}s.jpg`, type: 'jpeg', quality: 55 }).catch(() => {}); }
}
await new Promise(r => setTimeout(r, 1500));
await page.screenshot({ path: `${out}/zz_last.jpg`, type: 'jpeg', quality: 70 }).catch(() => {});
console.log(`scene: ${stop ? 'stopped: ' + stop : 'timeout'} after ${((Date.now() - t0) / 1000).toFixed(1)} s, ${n} shots in ${out}`);
log.end();
await browser.close();
