// Touch controls for phones and tablets: the same keys and mouse a keyboard player uses (input.js), so the game code does
// not change. Pattern from nfsu2-web touch.ts (MIT): the stick appears where the left thumb lands, a tap on a menu is a
// click, every touch is ours (touch-action none). Layout from Max Payne Mobile: fire button that also aims, drag to look.
'use strict';
const TK = { W: 0x11, A: 0x1e, S: 0x1f, D: 0x20, R: 0x13, SPACE: 0x39, C: 0x2e, E: 0x12, TAB: 0x0f, Q: 0x10, LSHIFT: 0x2a, ESC: 0x01 };   // rea/map/input.md 2
const LOOK = 2.5;       // mouse counts per CSS px of drag: a phone is narrower than a mouse's reach
const STICK = 56, DEAD = 12, MIN_PRESS = 70;

function startTouch(input, canvas) {
  const forced = new URLSearchParams(location.search).has('touch');
  if (!forced && window.MP_DEVICE === 'desktop') return;
  for (const t of ['gesturestart', 'gesturechange']) document.addEventListener(t, e => e.preventDefault(), { passive: false });   // Safari ignores user-scalable=no
  document.addEventListener('touchstart', e => { if (e.target === canvas || e.target.closest?.('#touch')) e.preventDefault(); }, { passive: false });   // no synthetic mouse, no pointer lock
  canvas.style.touchAction = 'none';

  // A current mobile shooter's HUD (CoD Mobile, Fortnite; OWNER chose "clúster compacto" 2026-11-20): six controls in view at
  // most (Epic's mobile guide), the primary ones packed in the bottom-right thumb zone, buttons 6 px apart around FIRE as one
  // cluster; a weapon card above it (prev, next, reload); pause and a tray for the rare keys top left; the stick appears where
  // the left thumb lands. Dim at rest, full under the finger; each hit area reaches 8 px past its disc (Apple HIG 44 pt).
  // The system is the launcher's: its tokens (web/launcher.css :root), ink glass, one red.
  const css = document.createElement('style');
  css.textContent = `/* Hallmark · component: phone game HUD · genre: atmospheric · theme: the launcher's tokens (brand) · states: rest, pressed, focus, tray open, hidden in menus · redesign: full · v2.0.0
 * critique: one compact cluster in the thumb corner, a weapon card, a tray for the rare keys; no floating discs across the screen
 * waive 24b: #nub margin -22px centres a 44px disc, geometry, not spacing */
    #touch { --k: 1; --hud-glass: color-mix(in srgb, var(--ink) 34%, transparent); --hud-line: color-mix(in srgb, var(--paper2) 34%, transparent);
             --hud-glyph: var(--paper2); --hud-fire: var(--red); --hud-fire-line: var(--red-label); --hud-shade: color-mix(in srgb, var(--ink) 75%, transparent);
             --hud-rest: .62;
             position: fixed; inset: 0; pointer-events: none; user-select: none; -webkit-user-select: none; -webkit-touch-callout: none; }
    #touch .b { position: absolute; pointer-events: auto; touch-action: none; display: grid; place-items: center; padding: 0; margin: 0; appearance: none;
                width: calc(var(--s) * var(--k)); height: calc(var(--s) * var(--k)); border-radius: 50%; color: var(--hud-glyph);
                border: 1.5px solid var(--hud-line); background: var(--hud-glass); -webkit-tap-highlight-color: transparent; opacity: var(--hud-rest);
                transition: transform 100ms var(--E-std), background-color 100ms linear, color 100ms linear, opacity 100ms linear; }
    #touch .b::after { content: ""; position: absolute; inset: -8px; border-radius: inherit; }
    #touch .b svg { width: 46%; height: 46%; fill: none; stroke: currentColor; stroke-width: 2; stroke-linecap: round; stroke-linejoin: round;
                    filter: drop-shadow(0 1px 1.5px var(--hud-shade)); }
    #touch .b:focus-visible { outline: 2px solid var(--hud-glyph); outline-offset: 3px; }
    #touch .b.on { opacity: 1; background: var(--hud-glyph); color: var(--ink); border-color: var(--hud-glyph); transform: scale(.94); }
    #touch .b.on svg { filter: none; }
    #touch .fire { opacity: .8; border: 2px solid var(--hud-fire-line); background: color-mix(in srgb, var(--hud-fire) 26%, transparent); }
    #touch .fire svg { width: 48%; height: 48%; stroke-width: 1.75; }
    #touch .fire.on { background: var(--hud-fire); color: var(--hud-glyph); border-color: var(--hud-fire-line); }
    #touch .bt { box-shadow: inset 0 0 0 3px var(--hud-glass), inset 0 0 0 4.5px var(--hud-line); }
    #touch .bar { position: absolute; pointer-events: none; display: flex; align-items: stretch; height: calc(var(--h) * var(--k));
                  border: 1.5px solid var(--hud-line); background: var(--hud-glass); color: var(--hud-glyph); opacity: var(--hud-rest);
                  transition: opacity 100ms linear; }
    #touch .bar:has(.on), #touch .bar.open { opacity: 1; }
    #touch .bar .b { position: relative; width: calc(var(--h) * var(--k) - 3px); height: auto; border: 0; border-radius: 0; background: transparent; opacity: 1; }
    #touch .bar .b + .b, #touch .bar .slot + .b { border-inline-start: 1px solid var(--hud-line); }
    #touch .bar .b.on { background: var(--hud-glyph); color: var(--ink); transform: none; }
    #touch .card { border-radius: calc(12px * var(--k)); overflow: hidden; }
    #touch .card .slot { display: grid; place-items: center; width: calc(52px * var(--k)); border-inline-start: 1px solid var(--hud-line); }
    #touch .card .slot svg { width: 30px; height: 30px; fill: none; stroke: var(--hud-glyph); stroke-width: 1.5; stroke-linejoin: round; }
    #touch .menu-bar { border-radius: calc(20px * var(--k)); overflow: hidden; }
    #touch .tray { display: none; }
    #touch .menu-bar.open .tray { display: grid; }
    #touch #stick { position: absolute; display: none; width: ${STICK * 2}px; height: ${STICK * 2}px; margin: -${STICK}px 0 0 -${STICK}px; border-radius: 50%;
                    border: 1.5px solid var(--hud-line); background: var(--hud-glass); }
    #touch #nub { position: absolute; left: 50%; top: 50%; width: 44px; height: 44px; margin: -22px; border-radius: 50%; background: var(--hud-line);
                  border: 1.5px solid var(--hud-glyph); }
    #touch.menu .play { display: none; }
    @media (max-height: 360px) { #touch { --k: .86; } }
    @media (prefers-reduced-motion: reduce) { #touch .b, #touch .bar { transition: none; } #touch .b.on { transform: none; } }`;
  document.head.append(css);
  const svg = d => `<svg viewBox="0 0 24 24" aria-hidden="true">${d}</svg>`;
  const ICON = {     // drawn here, 24-unit grid, stroked in the glyph colour
    fire: '<circle cx="12" cy="12" r="6.5"/><path d="M12 1.5v5M12 17.5v5M1.5 12h5M17.5 12h5"/><circle cx="12" cy="12" r=".6" fill="currentColor"/>',
    time: '<path d="M6.5 3h11M6.5 21h11M7.5 3c0 5 9 5 9 9s-9 4-9 9M16.5 3c0 5-9 5-9 9s9 4 9 9"/><path d="M10 18.5h4"/>',
    jump: '<path d="M12 19.5V5M6 11l6-6 6 6"/>',
    crouch: '<path d="M12 4v11M6.5 10l5.5 5.5 5.5-5.5M5 20h14"/>',
    reload: '<path d="M19.5 12a7.5 7.5 0 1 1-2.2-5.3"/><path d="M19.5 3.5v4.2h-4.2"/>',
    use: '<path d="M13.5 4h5.5v16h-5.5M4 12h11M11 8l4 4-4 4"/>',
    pill: '<g transform="rotate(-40 12 12)"><rect x="2.5" y="8.5" width="19" height="7" rx="3.5"/><path d="M12 8.5H6a3.5 3.5 0 0 0 0 7h6z" fill="currentColor"/></g>',
    zoom: '<circle cx="10.5" cy="10.5" r="6.5"/><path d="M15.5 15.5l5 5M10.5 7.5v6M7.5 10.5h6"/>',
    save: '<path d="M7 3.5h10v17l-5-4-5 4z"/>',
    pause: '<path d="M9 5.5v13M15 5.5v13"/>',
    more: '<circle cx="5.5" cy="12" r="1.2" fill="currentColor"/><circle cx="12" cy="12" r="1.2" fill="currentColor"/><circle cx="18.5" cy="12" r="1.2" fill="currentColor"/>',
    prev: '<path d="M14.5 6l-6 6 6 6"/>',
    next: '<path d="M9.5 6l6 6-6 6"/>',
  };
  const GUN = svg('<path d="M3 8h16.5v3.5H11l-1 1.5v.5H8.5L7.5 19H3.5L5 13.5 3 11.5z"/><path d="M9.5 11.5v2"/>');
  const root = document.createElement('div');
  root.id = 'touch';
  root.innerHTML = '<div id="stick"><div id="nub"></div></div>';
  document.body.append(root);
  const stick = root.querySelector('#stick'), nub = root.querySelector('#nub');

  // The cluster: FIRE in the corner, four discs on one ring 6 px out from it (angle in degrees, 180 = straight left, 90 = up).
  // Every length scales with --k on short screens; the safe areas come on top.
  const FIRE = 84, SAT = 50, GAP = 6, FX = 32 + FIRE / 2, FY = 26 + FIRE / 2, RING = FIRE / 2 + GAP + SAT / 2;
  const px = n => `calc(${n.toFixed(1)}px * var(--k)`;
  const edge = (side, n) => `${side}: ${px(n)} + env(safe-area-inset-${side}, 0px))`;
  const ring = deg => { const a = deg * Math.PI / 180; return [['right', FX - RING * Math.cos(a) - SAT / 2], ['bottom', FY + RING * Math.sin(a) - SAT / 2]]; };
  const key = k => [() => input.key(k, true), () => input.key(k, false)];
  const owned = new Map();       // pointerId -> what that finger does: {move(e), up()}
  const fingerLook = () => { let x, y; return { start(e) { x = e.clientX; y = e.clientY; },
    move(e) { input.move((e.clientX - x) * LOOK, (e.clientY - y) * LOOK); x = e.clientX; y = e.clientY; }, up() {} }; };
  // One control: a button that holds its key while the finger is down. look: dragging it also aims. after: runs on release.
  const make = (name, icon, cls, [down, up], look, after) => {
    const b = document.createElement('button');
    b.type = 'button'; b.className = 'b ' + cls; b.setAttribute('aria-label', name); b.innerHTML = svg(ICON[icon]);
    b.addEventListener('pointerdown', e => {
      e.preventDefault(); b.setPointerCapture(e.pointerId); b.classList.add('on'); down();
      const l = look ? fingerLook() : null, t0 = performance.now(); l?.start(e);
      // The game reads keys once a frame and acts on the change: a very short tap must still span a few frames.
      owned.set(e.pointerId, { move: ev => l?.move(ev), up() { b.classList.remove('on'); if (up) setTimeout(up, Math.max(0, MIN_PRESS - (performance.now() - t0))); after?.(); } });
    });
    return b;
  };
  const put = (b, s, pairs) => { b.style.cssText = `--s: ${s}px;` + pairs.map(([side, n]) => edge(side, n)).join(';'); root.append(b); };
  put(make('Fire', 'fire', 'fire play', [() => input.button(0, true), () => input.button(0, false)], true), FIRE, [['right', 32], ['bottom', 26]]);
  put(make('Bullet time', 'time', 'bt play', key(TK.LSHIFT), true), SAT, ring(200));
  put(make('Crouch', 'crouch', 'play', key(TK.C)), SAT, ring(152));
  put(make('Jump', 'jump', 'play', key(TK.SPACE)), SAT, ring(104));
  put(make('Use', 'use', 'play', key(TK.E)), SAT, ring(56));

  // A strip of buttons in one frame: the weapon card, the pause bar.
  const bar = (cls, h, pos, items) => {
    const el = document.createElement('div');
    el.className = 'bar ' + cls; el.style.cssText = `--h: ${h}px;` + pos.map(([side, n]) => edge(side, n)).join(';');
    el.append(...items); root.append(el); return el;
  };
  // The weapon card sits on the cluster: previous and next either side of the weapon (the mouse wheel, as before), reload last.
  // It carries no name or ammo: the game draws those itself, bottom right.
  const slot = document.createElement('span');
  slot.className = 'slot'; slot.innerHTML = GUN;
  const wheel = d => [() => input.wheel(d), null];
  bar('card play', 44, [['right', 16], ['bottom', FY + RING * Math.sin(104 * Math.PI / 180) + SAT / 2 + 14]],
    [make('Previous weapon', 'prev', '', wheel(120)), slot, make('Next weapon', 'next', '', wheel(-120)), make('Reload', 'reload', '', key(TK.R))]);
  // Pause, and the tray for the keys a fight rarely needs (painkillers, zoom, quick save); a key from the tray closes it.
  const close = () => menu.classList.remove('open');
  const toggle = [() => menu.classList.toggle('open'), null];
  const tray = [make('Painkillers', 'pill', 'tray play', key(TK.TAB), false, close), make('Zoom', 'zoom', 'tray play', key(TK.Q), false, close),
    make('Quick save', 'save', 'tray play', key(0x3f), false, close)];
  const menu = bar('menu-bar', 40, [['left', 16], ['top', 12]], [make('Pause', 'pause', '', key(TK.ESC), false, close), make('More', 'more', 'play', toggle), ...tray]);

  // The stick: W A S D, pressed past the dead zone, as the keyboard does (the game has no analog move).
  let wasd = 0;
  function press(next) {
    [[1, TK.W], [2, TK.S], [4, TK.A], [8, TK.D]].forEach(([bit, k]) => { if ((next ^ wasd) & bit) input.key(k, !!(next & bit)); });
    wasd = next;
  }
  const fingerStick = e => {
    const ox = e.clientX, oy = e.clientY;
    stick.style.display = 'block'; stick.style.left = ox + 'px'; stick.style.top = oy + 'px';
    return { move(ev) {
      let dx = ev.clientX - ox, dy = ev.clientY - oy;
      const n = Math.hypot(dx, dy);
      if (n > STICK) { dx *= STICK / n; dy *= STICK / n; }
      nub.style.transform = `translate(${dx}px, ${dy}px)`;
      if (n < DEAD) return press(0);
      const a = Math.atan2(dy, dx) / Math.PI * 8;     // eight ways: each key holds 3/8 of the circle
      press((a > -7 && a < -1 ? 1 : 0) | (a > 1 && a < 7 ? 2 : 0) | (Math.abs(a) > 5 ? 4 : 0) | (Math.abs(a) < 3 ? 8 : 0));
    }, up() { press(0); stick.style.display = 'none'; nub.style.transform = ''; } };
  };

  // A tap on a menu is a click at that place. The game keeps its cursor to itself, clamped to 640x480 at 1.25 units per
  // count (0x6313e0, rea/map/input.md 4): slam it into the corner, then move from there. The game reads the summed counts
  // once a frame, so each step waits for one.
  const frames = n => new Promise(r => { const f = () => n-- > 0 ? requestAnimationFrame(f) : r(); f(); });
  let tapping = false;
  async function tap(cx, cy) {
    if (tapping) return;
    tapping = true;
    const r = canvas.getBoundingClientRect(), s = Math.min(r.width / canvas.width, r.height / canvas.height);
    const w = canvas.width * s, h = canvas.height * s;
    const portrait = r.height > r.width;       // object-position in the page: top when upright, centred when sideways
    const w43 = Math.min(w, h * 4 / 3);        // the 640x480 menus stay centered 4:3 on a wide buffer
    const x = (cx - r.left - (r.width - w43) / 2) / w43 * 640, y = (cy - r.top - (portrait ? 0 : (r.height - h) / 2)) / h * 480;
    input.move(-4000, -4000);
    await frames(3);
    input.move(Math.max(0, Math.min(640, x)) / 1.25, Math.max(0, Math.min(480, y)) / 1.25);
    await frames(3);
    input.button(0, true);
    await frames(3);
    input.button(0, false);
    tapping = false;
  }

  canvas.addEventListener('pointerdown', e => {
    if (e.pointerType !== 'touch' && !forced) return;
    canvas.setPointerCapture(e.pointerId);
    const play = input.mode() === 3;
    // Graphic novel: its cursor speed depends on the frame time (0x49b6d0), so a tap cannot aim at its small buttons. Its keys
    // do the same work: right half next page, left half previous; ESC stops, then leaves (rea/map/graphicnovel.md 3).
    if (input.mode() === 2) {
      const k = e.clientX < innerWidth / 2 ? TK.A : TK.D;
      owned.set(e.pointerId, { move() {}, up() { input.key(k, true); frames(3).then(() => input.key(k, false)); }, cancel() {} });
      return;
    }
    if (!play) { const sx = e.clientX, sy = e.clientY; owned.set(e.pointerId, { move() {}, up() { tap(sx, sy); }, cancel() {} }); return; }
    if (e.clientX < innerWidth / 2) { owned.set(e.pointerId, fingerStick(e)); owned.get(e.pointerId).move(e); return; }
    const l = fingerLook(); l.start(e); owned.set(e.pointerId, l);
  });
  addEventListener('pointermove', e => { const o = owned.get(e.pointerId); if (o) { e.preventDefault(); o.move(e); } }, { passive: false });
  const end = e => { const o = owned.get(e.pointerId); if (o) { owned.delete(e.pointerId); o.up(); } };
  addEventListener('pointerup', end);
  // A cancelled touch is the phone's own gesture: it lets go of what it held, and a cancelled tap is no click.
  addEventListener('pointercancel', e => { const o = owned.get(e.pointerId); if (o) { owned.delete(e.pointerId); o.cancel ? o.cancel() : o.up(); } });

  // Fullscreen and landscape where the browser allows it (an iPhone does not: Add to Home Screen, manifest.json).
  // The screen stays on while the game runs.
  // A touch grants the gesture when the finger lifts (pointerup), not on pointerdown, so it is asked there and on every touch
  // until it holds: a refused or abandoned fullscreen comes back with the next tap.
  function fullscreenOnce() {
    if (forced || document.fullscreenElement) return;
    document.documentElement.requestFullscreen?.({ navigationUI: 'hide' }).then(() => screen.orientation?.lock?.('landscape')).catch(() => {});
  }
  addEventListener('pointerup', e => { if (e.pointerType === 'touch') fullscreenOnce(); }, true);
  let lock = null;
  const wake = () => { if (document.visibilityState === 'visible' && !lock) navigator.wakeLock?.request('screen').then(l => { lock = l; l.addEventListener('release', () => { lock = null; }); }).catch(() => {}); };
  document.addEventListener('visibilitychange', () => {
    wake();
    if (document.visibilityState !== 'visible') { for (const [id, o] of owned) { owned.delete(id); o.up(); } }   // no finger stays pressed across an app switch
  });
  wake();

  const portrait = matchMedia('(orientation: portrait)');
  const place = () => { canvas.style.objectPosition = portrait.matches ? '50% 0' : ''; };
  portrait.addEventListener('change', place);
  place();
  setInterval(() => { if (root.classList.toggle('menu', input.mode() !== 3)) close(); }, 250);
}
