// Browser keyboard and mouse -> the input ring in the shared heap (host/input.c reads it on the game thread).
// Keys are DirectInput scan codes (DIK), from KeyboardEvent.code, so the layout does not matter.
'use strict';
const DIK = {
  Escape: 0x01, Digit1: 0x02, Digit2: 0x03, Digit3: 0x04, Digit4: 0x05, Digit5: 0x06, Digit6: 0x07, Digit7: 0x08,
  Digit8: 0x09, Digit9: 0x0a, Digit0: 0x0b, Minus: 0x0c, Equal: 0x0d, Backspace: 0x0e, Tab: 0x0f,
  KeyQ: 0x10, KeyW: 0x11, KeyE: 0x12, KeyR: 0x13, KeyT: 0x14, KeyY: 0x15, KeyU: 0x16, KeyI: 0x17, KeyO: 0x18,
  KeyP: 0x19, BracketLeft: 0x1a, BracketRight: 0x1b, Enter: 0x1c, ControlLeft: 0x1d, KeyA: 0x1e, KeyS: 0x1f,
  KeyD: 0x20, KeyF: 0x21, KeyG: 0x22, KeyH: 0x23, KeyJ: 0x24, KeyK: 0x25, KeyL: 0x26, Semicolon: 0x27,
  Quote: 0x28, Backquote: 0x29, ShiftLeft: 0x2a, Backslash: 0x2b, KeyZ: 0x2c, KeyX: 0x2d, KeyC: 0x2e, KeyV: 0x2f,
  KeyB: 0x30, KeyN: 0x31, KeyM: 0x32, Comma: 0x33, Period: 0x34, Slash: 0x35, ShiftRight: 0x36,
  NumpadMultiply: 0x37, AltLeft: 0x38, Space: 0x39, CapsLock: 0x3a, F1: 0x3b, F2: 0x3c, F3: 0x3d, F4: 0x3e,
  F5: 0x3f, F6: 0x40, F7: 0x41, F8: 0x42, F9: 0x43, F10: 0x44, NumLock: 0x45, ScrollLock: 0x46, Numpad7: 0x47,
  Numpad8: 0x48, Numpad9: 0x49, NumpadSubtract: 0x4a, Numpad4: 0x4b, Numpad5: 0x4c, Numpad6: 0x4d, NumpadAdd: 0x4e,
  Numpad1: 0x4f, Numpad2: 0x50, Numpad3: 0x51, Numpad0: 0x52, NumpadDecimal: 0x53, F11: 0x57, F12: 0x58,
  NumpadEnter: 0x9c, ControlRight: 0x9d, NumpadDivide: 0xb5, PrintScreen: 0xb7, AltRight: 0xb8, Pause: 0xc5,
  Home: 0xc7, ArrowUp: 0xc8, PageUp: 0xc9, ArrowLeft: 0xcb, ArrowRight: 0xcd, End: 0xcf, ArrowDown: 0xd0,
  PageDown: 0xd1, Insert: 0xd2, Delete: 0xd3, MetaLeft: 0xdb, MetaRight: 0xdc, ContextMenu: 0xdd,
};
// God mode on one key without fn: the key left of 1 (º on a Spanish Mac, ` elsewhere; Chrome on Mac ISO reports it as IntlBackslash)
// sends F3, which host/cheats.c toggles in a running level. The game binds neither key by default.
DIK.IntlBackslash = DIK.Backquote = DIK.F3;

// Ring (u32 words): [0] head (here), [1] tail (game), [2..4] mouse dx, dy, wheel, [5] exclusive mouse (game), [8..8+1024) events:
// bit 31 = mouse button, bit 9 = auto-repeat of a held key, bit 8 = down, low byte = DIK or button. The game thread reads it at its input calls.
function startInput(M, canvas, steerLog) {
  const base = M._rn_input_ring() >>> 2, RING = 1024;   // unsigned: the heap can sit above 2 GB
  const H = () => M.HEAP32;
  function push(e) {
    const h = H(), head = Atomics.load(h, base), tail = Atomics.load(h, base + 1);
    if (head - tail >= RING) return;                   // full: the game is not reading; drop
    h[base + 8 + (head % RING)] = e;
    Atomics.store(h, base, head + 1);
  }
  const held = new Set();
  let escAt = -1e9;                                     // last real Esc keydown (the pointerlockchange fallback below checks it)
  function key(ev, down) {
    const d = DIK[ev.code];
    if (d === undefined) return;
    ev.preventDefault();
    if (down === held.has(d)) {                         // auto-repeat: DirectInput reports changes only, but Windows hands a repeated
      if (down) push(0x200 | d);                        // WM_KEYDOWN to the typed-key ring (menus scroll and slide while a key is held)
      return;
    }
    down ? held.add(d) : held.delete(d);
    if (d === 1 && down) escAt = performance.now();
    steerLog?.(`page key ${d.toString(16)} ${down ? 'down' : 'up'} t ${ev.timeStamp.toFixed(1)}`);
    push((down ? 0x100 : 0) | d);
  }
  canvas.tabIndex = 0;
  canvas.addEventListener('pointerdown', () => canvas.focus());   // a click on the game gives it the keys
  canvas.addEventListener('keydown', e => key(e, true));
  canvas.addEventListener('keyup', e => key(e, false));
  // Buttons the game sees down. A blur releases them like the keys (the mouseup goes to another window); the late mouseup
  // or gamepad release then finds nothing to release.
  const buttons = new Set();
  function button(b, down) {
    if (down) buttons.add(b);
    else if (!buttons.delete(b)) return;
    push(down ? 0x80000100 | b : 0x80000000 | b);
  }
  canvas.addEventListener('blur', () => {
    for (const d of held) push(d);
    held.clear();
    for (const b of [...buttons]) { button(b, false); steerLog?.(`page button ${b} up at blur`); }
  });
  // Mouse: relative motion while the pointer is over the canvas; buttons 0 left, 1 right, 2 middle.
  const btn = [0, 2, 1, 3, 4];
  // DirectInput counts (rea/map/input.md 3): the menu cursor lives in a 640x480 space and moves 1.25 units per count
  // (0x6313e0); in play a count turns the view by 0.0025 rad x sensitivity. With the pointer locked the page sends the
  // raw movement, one count per CSS pixel, as a real mouse would. Unlocked (headless driving, before the first click)
  // the game's cursor follows the pointer 1:1: counts = movement x 640 / (1.25 x on-screen width).
  // The fraction is carried (Atomics takes integers).
  let remX = 0, remY = 0;
  // Locked counts x MOUSE: the game turns 0.36 deg per count at the default slider (0x5195c0, rea/map/systems.md 0),
  // tuned for a 2001 400 DPI mouse; a mouse of today sends about 2.5x the counts for the same hand move. ?mouse=<f> overrides.
  const MOUSE = +(new URLSearchParams(location.search).get('mouse') || 0.4);
  // While the game holds an exclusive mouse (host/dinput8.c), Windows shows no cursor of its own and keeps the
  // mouse in the window: hide ours and lock it at the first click, so the game's cursor is the only one.
  const exclusive = () => Atomics.load(H(), base + 5) !== 0;
  function lockPointer() {
    if (!exclusive() || document.pointerLockElement === canvas) return;
    Promise.resolve(canvas.requestPointerLock({ unadjustedMovement: true }))
      .catch(() => { console.warn('pointer lock: no unadjustedMovement, macOS acceleration applies'); return canvas.requestPointerLock()?.catch?.(() => {}); });
  }
  function mouseMode() {
    const ex = exclusive();
    canvas.style.cursor = ex ? 'none' : '';
    if (!ex && document.pointerLockElement === canvas) { lockDropped = true; document.exitPointerLock(); }
  }
  setInterval(mouseMode, 250);
  // Chrome keeps Esc for itself while the pointer is locked: it unlocks and the page gets no keydown. The game's Esc (menu,
  // pause) is the same press, so an unlock while the page has focus is sent to the game as Esc down + up.
  document.addEventListener('pointerlockchange', () => {
    const dropped = lockDropped;
    lockDropped = false;
    if (document.pointerLockElement === canvas) { steerLog?.('pointer locked'); return; }
    steerLog?.('pointer unlocked');
    // Safari delivers the Esc keydown too (Chrome does not): send the synthetic press only if no real one came within 300 ms.
    const t0 = performance.now();
    if (exclusive() && document.hasFocus() && !dropped)
      setTimeout(() => { if (escAt < t0 - 300) { push(0x100 | 0x01); push(0x01); } }, 60);
  });
  let lockDropped = false;                          // set when the page itself drops the lock (mouseMode)
  canvas.addEventListener('mousemove', e => {
    const r = canvas.getBoundingClientRect();
    if (!r.width || !r.height) return;
    const locked = document.pointerLockElement === canvas;
    // The 640x480 menus stay centered 4:3 inside the fitted buffer of any shape.
    const fit = Math.min(r.width / canvas.width, r.height / canvas.height), sh = canvas.height * fit, sw = Math.min(canvas.width * fit, sh * 4 / 3);
    remX += e.movementX * (locked ? MOUSE : 512 / sw);
    remY += e.movementY * (locked ? MOUSE : 384 / sh);
    const ix = Math.trunc(remX), iy = Math.trunc(remY);
    remX -= ix; remY -= iy;
    if (ix) Atomics.add(H(), base + 2, ix);
    if (iy) Atomics.add(H(), base + 3, iy);
  });
  canvas.addEventListener('mousedown', e => {
    canvas.focus();
    // The game counts raw device motion (DirectInput, no OS acceleration): ask for unadjusted movement, which Chrome
    // gives in device units, not CSS pixels scaled by devicePixelRatio; the plain lock is the fallback (accelerated, CSS px).
    lockPointer();
    button(btn[e.button], true);
    e.preventDefault();
  });
  canvas.addEventListener('mouseup', e => button(btn[e.button], false));
  canvas.addEventListener('wheel', e => { Atomics.add(H(), base + 4, e.deltaY < 0 ? 120 : -120); e.preventDefault(); }, { passive: false });
  canvas.addEventListener('contextmenu', e => e.preventDefault());
  canvas.focus();
  // web/touch.js and web/gamepad.js press the same keys and move the same mouse as a keyboard player.
  return {
    lock() { lockPointer(); },
    dropLock() { if (document.pointerLockElement === canvas) { lockDropped = true; document.exitPointerLock(); return true; } return false; },   // web/ffwd.js: a cursor without the Esc
    key(d, down) { if (down !== held.has(d)) { down ? held.add(d) : held.delete(d); push((down ? 0x100 : 0) | d); } },
    button,
    move(dx, dy) {
      remX += dx; remY += dy;
      const ix = Math.trunc(remX), iy = Math.trunc(remY);
      remX -= ix; remY -= iy;
      if (ix) Atomics.add(H(), base + 2, ix);
      if (iy) Atomics.add(H(), base + 3, iy);
    },
    wheel(n) { Atomics.add(H(), base + 4, n); },
    mode() {                                  // host/gamestate.c word 3: 1 menu, 2 graphic novel, 3 game, ...; 0 before it exists
      const g = M._rn_gamestate_addr ? M._rn_gamestate_addr() >>> 2 : 0;
      return g ? M.HEAPU32[g + 3] : 0;
    },
  };
}
