// Per level, headless: `dump` writes rooms and portals; `check` teleports Max to every milestone's waypoint and records where he stands.
// node tools/ghost/ff_tp_check.mjs dump|check <outdir> [port] [jobs] [level,..]   A bad waypoint closes the game (0x69a860): logged, the level reloads and goes on.
import { createRequire } from 'node:module';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
const require = createRequire(new URL('../webgate.mjs', import.meta.url));
const { chromium } = require('playwright');
const [mode, out, port = '5195', jobs = '4', only] = process.argv.slice(2);
mkdirSync(out, { recursive: true });
const exits = JSON.parse(readFileSync(new URL('./exits.json', import.meta.url)));
const levels = only ? only.split(',') : Object.keys(exits).filter(k => exits[k].ff?.length);
const browser = await chromium.launch({ headless: true, args: ['--use-angle=metal', '--ignore-gpu-blocklist', '--enable-unsafe-webgpu', '--mute-audio', '--no-first-run'] });

async function open(level) {
  const ctx = await browser.newContext({ viewport: { width: 800, height: 450 } });
  const p = await ctx.newPage();
  p.fatal = '';
  p.on('console', m => { const s = m.text(); if (/parse failed|Could not teleport|Waypoint not found|ExitProcess|RuntimeError/.test(s)) p.fatal ||= s.slice(0, 160); });
  await p.goto(`http://127.0.0.1:${port}/?telemetry=0&level=${level}`);
  await p.waitForFunction(lv => { try { const s = MPFF.read(); return s?.level === lv && (s.control & 1) && MPFF.state.state === 'idle'; } catch { return false; } }, level, { timeout: 200000 });
  return { p, ctx };
}

const dump = p => p.evaluate(async () => {
  const M = MPFF.module(), b = M._rn_gamestate_addr() >>> 2, u = M.HEAPU32, f = new Float32Array(u.buffer), u8 = M.HEAPU8, req = u[b + 337] + 1;
  u[b + 337] = req;
  for (let i = 0; i < 200 && u[b + 338] !== req; i++) await new Promise(r => setTimeout(r, 50));
  const str = a => { let s = ''; for (let i = 0; i < 32 && u8[a + i]; i++) s += String.fromCharCode(u8[a + i]); return s; };
  const rooms = [], portals = [], ra = u[b + 346], pa = u[b + 348] >>> 2;
  for (let i = 0; i < u[b + 345]; i++) rooms.push(str(ra + i * 40 + 8));
  for (let i = 0; i < u[b + 347]; i++) { const w = pa + i * 19; portals.push({ a: u[w] | 0, b: u[w + 1] | 0, t: [...f.subarray(w + 7, w + 10)], R: [...f.subarray(w + 10, w + 19)] }); }
  return { rooms, portals };
});

async function dumpLevel(level) {
  const { p, ctx } = await open(level);
  writeFileSync(`${out}/${level}.json`, JSON.stringify(await dump(p)));
  console.log(level, 'dumped');
  await ctx.close();
}

async function checkLevel(level) {
  let steps = exits[level].ff.map((s, i) => ({ i, name: s.name, tp: s.tp })).filter(s => s.tp);
  const res = [];
  while (steps.length) {
    const { p, ctx } = await open(level);
    await p.evaluate(() => MPFF.send(['!player->C_SetImmortal(true);', '!player->c_setinvulnerable(true);']));
    while (steps.length) {
      const s = steps[0];
      const sent = await p.evaluate(async tp => { for (let k = 0; k < 600 && !(MPFF.read()?.control & 1); k++) await new Promise(r => setTimeout(r, 100)); return MPFF.send([`player->C_Teleport("${tp}");`], 30000); }, s.tp)   // an intro script may hold the camera: jump.c runs it with input only.catch(() => false);
      await p.waitForTimeout(500);
      const a = await p.evaluate(() => MPFF.read()?.pos).catch(() => null);
      await p.waitForTimeout(1500);
      const r = await p.evaluate(() => { const x = MPFF.read(); return x && { lv: x.level, pos: x.pos, c: x.control }; }).catch(() => null);
      const ok = sent && !p.fatal && r?.lv === level;
      if (!ok) { console.log(level, s.tp, 'FAILED', p.fatal); res.push({ ...s, fail: p.fatal || "no state" }); console.log("  sent", sent, "lv", r?.lv); steps.shift(); break; }
      res.push({ ...s, pos: r.pos.map(v => +v.toFixed(2)), drop: +(a[1] - r.pos[1]).toFixed(2) });
      await p.screenshot({ path: `${out}/${level}_${s.i}.jpg`, type: 'jpeg', quality: 55 }).catch(() => {});
      console.log(level, s.i, s.tp, res.at(-1).pos.join(','), 'drop', res.at(-1).drop);
      steps.shift();
    }
    await ctx.close();
  }
  writeFileSync(`${out}/${level}.json`, JSON.stringify(res));
}

const queue = [...levels], fn = mode === 'dump' ? dumpLevel : checkLevel;
await Promise.all(Array.from({ length: +jobs }, async () => {
  for (let l; (l = queue.shift());) await fn(l).catch(e => console.log(l, 'ERROR', String(e).slice(0, 140)));
}));
await browser.close();
