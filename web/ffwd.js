// Fast-forward (OWNER 2026-11-07): F held = the game runs at x4, F tapped = the milestones panel; the autopilot plays the story by its own
// script messages up to the milestone picked. Same mechanism as tools/ghost/ghost.mjs routeMap: lines go through the RN_JUMP mailbox
// (host/jump.c rn_jump_box), state comes from host/gamestate.c, routes from tools/ghost/exits.json (served as /route.json, key `ff` wins over
// `steps`). Speed is the game's own message (GM_ChangeGlobalGameSpeed, the level scripts use it for their slow motion). No game code here.
// window.MPFF = { start(M, canvas, input), run(level, step), autoplay(), milestones(level), ready }. Panel drawing lives in the same file; the styles in web/ffwd.css.
'use strict';
(() => {
  const ORDER = ['part0_level1', 'part1_level1', 'part1_level1b', 'part1_level2', 'part1_level2b', 'part1_level3', 'part1_level3b', 'part1_level4',
    'part1_level5', 'part1_level6', 'part2_level0', 'part2_level1', 'part2_level2', 'part2_level2b', 'part2_level3', 'part2_level4', 'part3_level1',
    'part3_level2', 'part3_level2b', 'part3_level3', 'part3_level4', 'part3_level5', 'part3_level5b', 'part3_level6', 'part3_level7'];   // host order: tools/ghost/ghost.mjs PART1
  const TURBO = 4, TAP_MS = 220, MODE_GAME = 3;
  // The scripts switch invulnerability themselves (cin2_cam2 @24673319); immortal is host/cheats.c GOD_ON, and no level touches it.
  const GOD = ['!player->C_SetImmortal(true);', '!player->c_setinvulnerable(true);'], MORTAL = ['player->C_SetImmortal(false);', 'player->c_setinvulnerable(false);', 'player->c_addhealth(100);'];
  const sleep = ms => new Promise(r => setTimeout(r, ms));
  const godCheat = () => { try { return JSON.parse(localStorage.getItem('mp.launcher.v2'))?.opts?.dios === '1'; } catch { return false; } };   // the player's own god mode stays on
  const SPEED = v => `!maxpayne_gamemode->gm_changeglobalgamespeed(${v}~ 0);`;

  let M = null, canvas = null, input = null, ROUTES = {}, TITLES = {}, COVERS = {};
  // One state field, one writer (K4): idle | panel | running. `turbo` is a hold that lives only while idle or running.
  const ff = { state: 'idle', turbo: false, cancel: false, relock: false, level: '', step: -1, note: '', target: null };

  // ---- the game: the same words ghost.mjs reads (host/gamestate.c) ---------------------------------------------------------------------
  const str = (u8, at, n) => { let s = ''; for (let k = 0; k < n && u8[at + k]; k++) s += String.fromCharCode(u8[at + k]); return s; };
  function read() {
    const b = M._rn_gamestate_addr() >>> 2, u = M.HEAPU32, f = new Float32Array(u.buffer);
    for (let tries = 0; tries < 8; tries++) {
      const s0 = Atomics.load(u, b + 1);
      if (s0 & 1) continue;
      const o = { mode: u[b + 3], flags: u[b + 4], enemies: u[b + 29], control: u[b + 351], health: f[b + 21], pos: [f[b + 6], f[b + 7], f[b + 8]], en: [] };
      const u8 = M.HEAPU8; o.level = str(u8, (b + 32) * 4, 32); o.next = str(u8, (b + 40) * 4, 32);
      for (let k = 0; k < Math.min(o.enemies, 48); k++) { const e = b + 48 + 6 * k; o.en.push({ flags: u[e + 4], name: str(u8, u[b + 352] + 64 * k, 64) }); }
      if (Atomics.load(u, b + 1) === s0) return o;
    }
    return null;
  }
  function trigs() {
    const b = M._rn_gamestate_addr() >>> 2, u = M.HEAPU32, u8 = M.HEAPU8, n = u[b + 341], a = u[b + 342] >>> 2, out = new Map();
    for (let k = 0; k < n; k++) { const e = a + 23 * k; out.set(str(u8, (e + 7) * 4, 64), u8[(e + 6) * 4]); }
    return out;                                                     // name -> enabled
  }
  const steady = s => s && s.mode === MODE_GAME && (s.flags & 2) && ((s.flags >> 8) & 15) === 1;
  const inControl = s => steady(s) && (s.control & 1);

  // host/jump.c JumpBox: {seq, done, sent, failed, cancel, line}. A batch not run in time, or left behind by a level change, is cancelled so it
  // never parses in another level (a receiver the level lacks closes the game).
  async function cancelBatch(box, u, q) {
    Atomics.store(u, box + 4, q);
    for (let k = 0; k < 40; k++) {
      if (Atomics.load(u, box + 1) === q) return true;
      await sleep(50);
    }
    return Atomics.load(u, box + 1) === q;
  }
  async function send(lines, ms = 30000) {
    if (!lines.length) return true;
    const box = M._rn_jump_box() >>> 2, u = M.HEAPU32;
    let q = Atomics.load(u, box);
    if (q !== Atomics.load(u, box + 1)) {
      for (let k = 0; k < 40 && Atomics.load(u, box + 1) !== q; k++) await sleep(50);
      if (Atomics.load(u, box + 1) !== q && !await cancelBatch(box, u, q)) return false;
    }
    if (ff.cancel) return false;
    const lv0 = read()?.level;
    const t = new TextEncoder().encode(lines.join('|')).subarray(0, 4095);
    M.HEAPU8.set(t, box * 4 + 20); M.HEAPU8[box * 4 + 20 + t.length] = 0;
    q = (u[box] + 1) >>> 0; Atomics.store(u, box, q);
    for (const end = performance.now() + ms; performance.now() < end; await sleep(100)) {
      if (Atomics.load(u, box + 1) === q) return true;
      const s = read();
      if (ff.cancel || (lv0 && s && s.level && s.level !== lv0)) break;
    }
    await cancelBatch(box, u, q);
    return false;
  }
  async function until(pred, ms) {
    for (const end = performance.now() + ms; performance.now() < end; await sleep(150)) { if (ff.cancel) return null; const s = read(); if (s && pred(s)) return s; }
    return null;
  }
  // Fire and forget: one line for the speed, skipped while a route batch owns the box (that batch ends soon; the route re-sends the speed).
  function speedNow(v) {
    const box = M._rn_jump_box() >>> 2, u = M.HEAPU32;
    if (Atomics.load(u, box) !== Atomics.load(u, box + 1)) return false;
    const t = new TextEncoder().encode(SPEED(v));
    M.HEAPU8.set(t, box * 4 + 20); M.HEAPU8[box * 4 + 20 + t.length] = 0;
    Atomics.store(u, box, (u[box] + 1) >>> 0);
    return true;
  }
  // Last value wins and always lands: a cheat batch can own the box for 25 s, and two capped retry loops (panel open 0.02, close 1)
  // once let the 0.02 land last and the 1 give up, leaving the OWNER's game at 2% speed (session 2026-11-07T19-52).
  let speedWant = null;
  const pumpSpeed = () => { if (speedWant === null) return; if (M && speedNow(speedWant)) speedWant = null; else setTimeout(pumpSpeed, 50); };
  const setSpeed = v => { const idle = speedWant === null; speedWant = v; if (idle) pumpSpeed(); };

  // ---- the route ------------------------------------------------------------------------------------------------------------------------
  // Steps of a level: `ff` (milestones written for the autopilot) or the ghost's `steps`, then the exit step. A step: {name, kill, tp, t, wait,
  // send, then, control}. `kill`: rooms or instance names, re-read while the step waits, since the scripts teleport waves in late.
  function stepsOf(level) {
    const r = ROUTES[level] || {};
    const s = [...(r.ff || r.steps || [])];
    if (!r.ff || !r.ffEnd) {
      const exit = { exit: true, name: 'End of chapter', kill: r.kill, t: (r.exit || []).filter(n => n.endsWith('.TRIGGER')), send: r.send };
      if (s.length || exit.t.length || exit.kill?.length || exit.send?.length) s.push(exit);
    }
    return s;
  }
  const milestones = level => stepsOf(level).map((s, i) => ({ i, name: s.name })).filter(x => x.name);
  const killable = (s, st, spare) => s.en.filter(e => !(e.flags & 1) && e.name && !spare.includes(e.name)
    && (st.kill === true || st.kill.some(r => e.name === r || e.name.startsWith(r + '::')))).map(e => e.name);

  async function runStep(level, st) {
    const spare = ROUTES[level]?.spare?.names || [];
    await send(GOD);
    // The level may have ended while GOD waited (a load does not poll the mailbox): a waypoint of the old level closes the game in the next one
    // (part2_level0's ::26_BabysRoom::W01 sent in part2_level1, run 2026-11-07).
    const here = () => { const s = read(); return s && s.level === level && (!s.next || s.next === level); };
    if (!here()) return;
    if (st.tp) await send([`player->C_Teleport("${st.tp}");`]);
    const ts = [].concat(st.t || []), end = performance.now() + (st.wait || (ts.length || st.kill ? 20 : 0)) * 1000;
    for (let on = null; !ff.cancel;) {
      const s = read();
      if (!s || s.level !== level) return;
      const names = st.kill ? killable(s, st, spare) : [];
      if (names.length) await send(names.map(n => `!${n}->C_Kill(1);`));
      else if (st.dead && !ts.length) break;
      if (ts.length) { const l = trigs(); on = ts.find(n => l.get(n)); }
      if (on) { await send([`${on}->T_Activate();`]); break; }
      if (performance.now() >= end) {                                // nobody enabled it: the same way out as the ghost's route
        if (ts.length && !st.send) await send([`${ts[0]}->T_Activate();`]);
        break;
      }
      await sleep(400);
    }
    if (ff.cancel || read()?.level !== level) return;
    if (st.send && !await send(st.send, st.sendMs || 30000)) return;
    if (st.then && !await send(st.then, st.thenMs || 180000)) return;
    if (ff.state === 'running') setSpeed(TURBO);
    await sleep(300);
    if (st.control !== false) await until(x => x.level !== level || inControl(x) || x.mode === 1, st.controlMs || 150000);   // a step may run a cutscene
  }

  // Play `level` up to step index `upto` (inclusive). Returns when done or when the level is gone.
  async function runLevel(level, upto) {
    const steps = stepsOf(level);
    for (let i = Math.max(0, ff.level === level ? ff.step + 1 : 0); i < steps.length && i <= upto && !ff.cancel; i++) {
      if (read()?.level !== level) return;
      ff.level = level; ff.step = i;
      hud(`${TITLES[level] || level}`, steps[i].name || `step ${i + 1} of ${steps.length}`);
      await runStep(level, steps[i]);
    }
  }

  // Keep a line queued so jump_poll can tap Esc through the opening comic while arrive waits for control.
  async function arrive(level) {
    if (!await until(x => x.level === level || x.next === level, 60000)) return false;
    // The mailbox is not polled during a load, so a pending batch makes send give up; retry until the new level takes it.
    const end = performance.now() + 240000;
    while (!await send(GOD, 90000)) {
      if (ff.cancel || performance.now() > end) return false;
      await sleep(500);
    }
    return !!await until(x => x.level === level && inControl(x), 60000);
  }

  // The autopilot: from where the game is to {level, step}. Another chapter loads directly with the message the level exits send; `booting`: the
  // launcher's RN_JUMP is already loading it (jump.c runs the mailbox only after that list), so here it only waits for the level.
  async function run(level, step = -1, booting = false) {
    if (ff.state === 'running') return;
    setState('running'); ff.cancel = false; ff.target = { level, step };
    try {
      await until(s => s.mode === 1 || s.mode === 2 || steady(s), 60000);
      let s = read(), here = s?.level;
      if (booting) { hud(TITLES[level] || level, 'loading'); if (!await arrive(level)) return; }
      else if (!here || ORDER.indexOf(here) !== ORDER.indexOf(level)) {
        ff.level = ''; ff.step = -1;
        hud(TITLES[level] || level, 'loading');
        if (!await send([`maxpayne_gamemode->gm_init(${level});`, 'x_modeswitch->s_modeswitch(game);'])) return;
        if (!await arrive(level)) return;
      }
      if (step >= 0) { setSpeed(TURBO); await runLevel(level, step); }
      // Arrived: what the milestone starts (the ending's cameras) plays at its own speed.
      setSpeed(1); badge.dataset.on = ''; if (!ff.cancel) hud(TITLES[level] || level, 'arrived', 2600);
      if (!ff.cancel) await until(inControl, 60000);
      if (!ff.cancel) await send(godCheat() ? ['player->c_setinvulnerable(false);'] : MORTAL, 20000);
    } finally {
      setSpeed(1);
      if (ff.cancel) hud('Skip cancelled', '', 2600);
      setState('idle');
    }
  }
  // The whole story by itself: each level's route to its exit, the next level as the game asks for it, forced if the route did not get there.
  async function autoplay(lastLevel = ORDER[ORDER.length - 1]) {
    if (ff.state === 'running') return;
    setState('running'); ff.cancel = false;
    try {
      setSpeed(TURBO);
      for (;;) {
        const s = await until(x => x.mode === 1 || inControl(x), 240000); if (!s || ff.cancel || s.mode === 1) break;
        const level = s.level, nx = ORDER[ORDER.indexOf(level) + 1];
        const route = ROUTES[level] || {};
        const noRoute = !(route.ff?.length || route.steps?.length);
        await runLevel(level, Infinity);
        if (ff.cancel || level === lastLevel || !nx) break;
        hud(TITLES[nx] || nx, 'next chapter');
        if (!ff.cancel && (noRoute && !route.send || !await until(x => x.level !== level || x.next && x.next !== level, 45000))
            && read()?.level === level)
          await send([`maxpayne_gamemode->gm_init(${nx});`, 'x_modeswitch->s_modeswitch(game);']);
        if (!await arrive(nx)) break;
        setSpeed(TURBO);
      }
    } finally {
      setSpeed(1);
      hud(ff.cancel ? 'Skip cancelled' : 'Autopilot', ff.cancel ? '' : 'done', 2600);
      setState('idle');
    }
  }
  // Next milestone: the rest of this level up to the next named step; the end of the level walks into the next one.
  function nextStop() {
    const s = read(), level = s?.level; if (!level) return null;
    const steps = stepsOf(level), from = ff.level === level ? ff.step + 1 : 0;
    for (let i = from; i < steps.length; i++) if (steps[i].name && !steps[i].exit) return { level, step: i };
    const nx = ORDER[ORDER.indexOf(level) + 1];
    return nx ? { level: nx, step: -1 } : null;
  }

  // ---- HUD and panel --------------------------------------------------------------------------------------------------------------------
  let root, badge, card, panel, list = [], sel = 0, hudTimer = 0;
  const h = (tag, cls, ...kids) => { const e = document.createElement(tag); if (cls) e.className = cls; e.append(...kids.filter(k => k != null)); return e; };
  function hud(title, sub, ms) {
    clearTimeout(hudTimer);
    card.querySelector('b').textContent = title; card.querySelector('span').textContent = sub || '';
    card.dataset.on = '1';
    if (ms) hudTimer = setTimeout(() => { delete card.dataset.on; }, ms);
  }
  function setState(s) {
    ff.state = s; if (root) root.dataset.state = s;
    if (panel) { panel.inert = s !== 'panel'; panel.setAttribute('aria-hidden', String(s !== 'panel')); }
    badge.dataset.on = ff.turbo || s === 'running' ? '1' : '';
    if (s === 'idle' && !hudTimer) delete card.dataset.on;
  }
  function buildPanel() {
    const s = read(), level = s?.level || '', stop = nextStop();
    list = [];
    if (stop) list.push({ kind: 'next', title: stop.step >= 0 ? stepsOf(stop.level)[stop.step].name : TITLES[stop.level] || stop.level, sub: 'Next checkpoint', cover: COVERS[stop.level], go: () => run(stop.level, stop.step) });
    list.push({ kind: 'hito', title: 'Autopilot', sub: 'Plays to the credits. Esc cancels', cover: COVERS[level], go: () => autoplay() });
    for (const m of milestones(level)) list.push({ kind: 'hito', title: m.name, sub: 'This chapter', cover: COVERS[level], go: () => run(level, m.i) });
    for (const id of ORDER) if (id !== level) list.push({ kind: 'chapter', id, title: TITLES[id] || id, sub: 'Chapter ' + (ORDER.indexOf(id) + 1) + ' of ' + ORDER.length, cover: COVERS[id], go: () => run(id) });
    sel = 0;
    const box = panel.querySelector('.ff-list'); box.textContent = '';
    list.forEach((it, i) => {
      const cov = it.cover ? Object.assign(document.createElement('img'), { src: it.cover, alt: '', loading: 'lazy' }) : null;
      const row = h('button', 'ff-row ff-' + it.kind, cov, h('b', '', it.title), h('span', '', it.sub));
      row.type = 'button';
      if (cov) cov.addEventListener('error', () => cov.remove(), { once: true });
      row.addEventListener('pointerenter', () => paint(i));
      row.addEventListener('focus', () => paint(i));
      row.addEventListener('click', () => choose(i));
      box.append(row);
    });
    paint(0);
  }
  function paint(i) {
    sel = Math.max(0, Math.min(list.length - 1, i));
    [...panel.querySelector('.ff-list').children].forEach((e, k) => { if (k === sel) { e.dataset.on = '1'; e.scrollIntoView({ block: 'nearest', inline: 'center' }); } else delete e.dataset.on; });
  }
  function openPanel() {
    if (ff.state !== 'idle') return;
    buildPanel(); setState('panel'); ff.relock = input?.dropLock?.() || false;
    setSpeed(0.02);                                                 // the game nearly stops behind the panel, its own slow motion
    canvas.blur();                                                  // input.js releases the held keys and buttons
    panel.querySelector('.ff-row')?.focus({ preventScroll: true });
  }
  function closePanel(then) {
    const relock = ff.relock; ff.relock = false;
    setState('idle'); setSpeed(1); fDownAt = 0;
    canvas.focus(); if (relock) input?.lock?.();
    if (then) then();
  }
  function choose(i) { const it = list[i]; if (it) closePanel(it.go); }

  // ---- keys: window capture runs before web/input.js, as web/telemetry.js does -----------------------------------------------------------
  let fDownAt = 0, holdTimer = 0;
  function turboOn(on) {
    if (ff.turbo === on) return;
    ff.turbo = on; badge.dataset.on = on || ff.state === 'running' ? '1' : '';
    if (ff.state === 'idle') setSpeed(on ? TURBO : 1);
  }
  function onKey(e) {
    const down = e.type === 'keydown';
    if (ff.state === 'panel') {
      e.preventDefault(); e.stopImmediatePropagation();
      if (!down) return;
      if (e.code === 'Tab') {
        const buttons = [...panel.querySelectorAll('button')], at = buttons.indexOf(document.activeElement);
        buttons[(at + (e.shiftKey ? -1 : 1) + buttons.length) % buttons.length]?.focus();
      } else if (e.code === 'KeyF' || e.code === 'Escape') { if (!e.repeat) closePanel(); }
      else if (e.code === 'ArrowDown' || e.code === 'ArrowRight' || e.code === 'KeyS' || e.code === 'KeyD') { paint(sel + 1); panel.querySelectorAll('.ff-row')[sel]?.focus({ preventScroll: true }); }
      else if (e.code === 'ArrowUp' || e.code === 'ArrowLeft' || e.code === 'KeyW' || e.code === 'KeyA') { paint(sel - 1); panel.querySelectorAll('.ff-row')[sel]?.focus({ preventScroll: true }); }
      else if (e.code === 'Enter' || e.code === 'Space') {
        if (document.activeElement?.classList.contains('ff-close')) closePanel(); else choose(sel);
      }
      return;
    }
    if (ff.state === 'running' && e.code === 'Escape' && down) { ff.cancel = true; e.preventDefault(); e.stopImmediatePropagation(); return; }
    if (e.code === 'KeyN' && ff.state === 'idle') {                // N = the Avanzar button (OWNER 2026-11-07): the pointer is locked while playing
      const s = M && read();
      if (!s || s.mode !== MODE_GAME) return;
      e.preventDefault(); e.stopImmediatePropagation();
      if (down && !e.repeat) { const st = nextStop(); if (st) run(st.level, st.step); }
      return;
    }
    if (e.code !== 'KeyF') return;
    const s = M && read();
    if (!s || !(s.mode === MODE_GAME)) return;                      // the game's own menus keep the key
    e.preventDefault(); e.stopImmediatePropagation();
    if (e.repeat) return;
    if (down) { fDownAt = performance.now(); holdTimer = setTimeout(() => turboOn(true), TAP_MS); return; }
    clearTimeout(holdTimer);
    if (ff.turbo) turboOn(false);
    else if (performance.now() - fDownAt < TAP_MS) openPanel();
  }
  function onBlur() {
    clearTimeout(holdTimer); fDownAt = 0;
    if (ff.turbo) turboOn(false);
  }

  async function start(module, cv, inp) {
    M = module; canvas = cv; input = inp;
    badge = h('div', 'ff-badge', h('i'), h('i'), h('b', '', 'x' + TURBO));
    card = h('div', 'ff-card', h('b'), h('span'));
    const closeBtn = h('button', 'ff-btn ff-close', 'Close (Esc)'); closeBtn.type = 'button';
    closeBtn.addEventListener('click', () => closePanel());
    const heading = h('b', '', 'Skip Ahead'); heading.id = 'mpff-title';
    panel = h('div', 'ff-panel', h('header', '', heading, h('span', '', 'Pick a destination. Enter skips ahead; Esc or F returns to the game.'), closeBtn), h('div', 'ff-list'));
    panel.setAttribute('role', 'dialog'); panel.setAttribute('aria-modal', 'true'); panel.setAttribute('aria-labelledby', heading.id);
    panel.inert = true; panel.setAttribute('aria-hidden', 'true');
    card.setAttribute('role', 'status');
    // On-screen buttons (OWNER 2026-11-07): "Skip Ahead" = the panel's first row (next milestone), the menu one = the panel. Shown only while playing.
    const nextBtn = h('button', 'ff-btn ff-next', h('b', '', 'Skip Ahead \u25b8 (N)'), h('span')), menuBtn = h('button', 'ff-btn ff-menu', 'Checkpoints (F)');
    nextBtn.type = menuBtn.type = 'button'; menuBtn.title = 'Checkpoints and chapters (F)';
    menuBtn.setAttribute('aria-label', 'Open checkpoints and chapters (F)');
    nextBtn.addEventListener('click', e => { e.stopPropagation(); const st = ff.state === 'idle' && nextStop(); if (st) run(st.level, st.step); });
    menuBtn.addEventListener('click', e => { e.stopPropagation(); openPanel(); });
    const bar = h('div', 'ff-bar', nextBtn, menuBtn);
    root = h('div', '', badge, card, bar, panel); root.id = 'mpff'; root.dataset.state = 'idle';
    document.body.append(root);
    setInterval(() => {                                            // 2 Hz: the game mode and the next milestone's name
      // Mode 3 also hosts script cameras and the ending. Input link and camera busy are authoritative.
      // GM_SetPlayerControls /* 0x456650 */, LocalCharacterInput /* 0x5030e0 */; host/gamestate.c GSC_*.
      const s = M && read(), on = !!s && s.mode === MODE_GAME && (s.control & 7) === 1 && !!s.level && !(s.flags & 4) && ff.state === 'idle';
      if (on) { const st = nextStop(); nextBtn.querySelector('span').textContent = !st ? 'No checkpoints left' : st.step >= 0 ? stepsOf(st.level)[st.step].name : TITLES[st.level] || st.level; nextBtn.disabled = !st; }
      if (on) bar.dataset.on = '1'; else delete bar.dataset.on;
    }, 500);
    let revealTimer;
    window.addEventListener('keydown', e => {
      if (!bar.dataset.on || !['Tab', 'KeyF', 'KeyN'].includes(e.code)) return;
      bar.dataset.reveal = '1'; clearTimeout(revealTimer);
      revealTimer = setTimeout(() => delete bar.dataset.reveal, 2000);
    }, true);
    window.addEventListener('keydown', onKey, true);
    window.addEventListener('keyup', onKey, true);
    window.addEventListener('blur', onBlur);
    await ready;
  }
  // Loaded with the script: the launcher lists the same milestones before the game exists.
  const ready = (async () => {
    try { ROUTES = await (await fetch('route.json')).json(); } catch { ROUTES = {}; }
    try {
      const c = await (await fetch('launcher/chapters.json')).json();
      for (const l of c.levels) { if (l.chapter_title || l.title) TITLES[l.id] = l.chapter_title || l.title; if (l.cover?.path) COVERS[l.id] = (c.base || '/gamefile/') + l.cover.path; }
    } catch { /* no chapters: the rows show level ids */ }
  })();
  window.MPFF = { start, run, autoplay, milestones, read, send, trigs, module: () => M, state: ff, ORDER, ready };   // send, trigs, module: the ghost tests
})();
