// Controllers (Gamepad API, standard mapping): the same keys and mouse as a keyboard player (input.js). The game has no
// joystick support of its own (MP 1.05 never creates one, rea/map/input.md). Layout: SteamInputDB templates for app 12140.
'use strict';
function startGamepad(input) {
  if (!navigator.getGamepads) return;
  const K = { W: 0x11, A: 0x1e, S: 0x1f, D: 0x20, R: 0x13, SPACE: 0x39, C: 0x2e, E: 0x12, TAB: 0x0f, Q: 0x10, ESC: 0x01, ENTER: 0x1c, UP: 0xc8, DOWN: 0xd0, LEFT: 0xcb, RIGHT: 0xcd };
  const LOOK = 14, DEAD = 0.18;      // mouse counts per frame at full stick
  // button index -> action, in play and in a menu. 'fire' and 'time' are the two mouse buttons; numbers are DIK codes.
  const PLAY = { 0: K.SPACE, 1: K.C, 2: K.E, 3: K.R, 4: 'prev', 5: 'next', 6: 'time', 7: 'fire', 8: K.TAB, 9: K.ESC, 10: K.Q, 12: K.UP, 13: K.DOWN, 14: K.LEFT, 15: K.RIGHT };
  const MENU = { 0: K.ENTER, 1: K.ESC, 9: K.ESC, 12: K.UP, 13: K.DOWN, 14: K.LEFT, 15: K.RIGHT };
  const pressed = new Map();         // button index -> the action it holds down, so a release matches its press across modes
  function act(a, down) {
    if (a === 'fire') input.button(0, down);
    else if (a === 'time') input.button(1, down);
    else if (a === 'next' || a === 'prev') { if (down) input.wheel(a === 'next' ? -120 : 120); }
    else input.key(a, down);
  }
  let moved = 0, seen = false;
  function tick() {
    requestAnimationFrame(tick);
    const pad = [...navigator.getGamepads()].find(p => p?.connected && p.mapping === 'standard');
    if (!pad) { if (seen) { for (const [i, a] of pressed) { act(a, false); pressed.delete(i); } if (moved) { [[1, K.W], [2, K.S], [4, K.A], [8, K.D]].forEach(([b, k]) => { if (moved & b) input.key(k, false); }); moved = 0; } seen = false; } return; }
    seen = true;
    const play = input.mode() === 3, map = play ? PLAY : MENU;
    pad.buttons.forEach((b, i) => {
      const on = b.pressed || b.value > 0.5, had = pressed.get(i);
      if (on && had === undefined && map[i] !== undefined) { pressed.set(i, map[i]); act(map[i], true); }
      else if (!on && had !== undefined) { pressed.delete(i); act(had, false); }
    });
    // Sticks: left walks (the game moves by keys), right aims. In a menu the left stick stays off: the D-pad moves.
    const [lx, ly, rx, ry] = pad.axes;
    const next = !play ? 0 : (ly < -0.4 ? 1 : 0) | (ly > 0.4 ? 2 : 0) | (lx < -0.4 ? 4 : 0) | (lx > 0.4 ? 8 : 0);
    [[1, K.W], [2, K.S], [4, K.A], [8, K.D]].forEach(([b, k]) => { if ((next ^ moved) & b) input.key(k, !!(next & b)); });
    moved = next;
    if (play) {
      const curve = v => Math.abs(v) < DEAD ? 0 : Math.sign(v) * ((Math.abs(v) - DEAD) / (1 - DEAD)) ** 2 * LOOK;
      input.move(curve(rx), curve(ry));
    }
  }
  requestAnimationFrame(tick);
}
