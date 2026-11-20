// Original profiles: input 0x4cb3d0/0x4cb7d0, other controls 0x42c040/0x42c130.
'use strict';
(() => {
  const KEY = 'HKCU\\Software\\Remedy Entertainment\\Max Payne\\Input Settings'; /* 0x40a4c0, 0x7cf830 */
  const OTHER = 'HKCU\\Software\\Remedy Entertainment\\Max Payne\\Other Settings'; /* 0x42c040, 0x7cc458 */
  const VERSION = 9; /* 0x87bf64 */
  const field = (i, name) => String(i).padStart(2, ' ') + ' ' + name; /* 0x87bf68: "%2d %s" */
  const actions = ['Forward / back', 'Strafe left / right', 'Shoot', 'Reload', 'Jump', 'Crouch',
    'Dodge left', 'Dodge right', 'Dodge forward', 'Dodge back', 'Dodge modifier',
    'Look up / down', 'Look left / right', 'Use', 'Melee weapon', 'Pistol', 'Shotgun', 'Submachine gun',
    'Thrown weapon', 'Sniper rifle', 'Weapon 7', 'Weapon 8', 'Weapon 9', 'Weapon 10', 'Weapon 11', 'Best weapon',
    'Next / previous weapon', 'Pause', 'Painkiller', 'Sniper zoom', 'Slow motion', 'Bullet time']; /* 0x4cce80, 0x8a9168 */
  // Preserve every untouched direction and alternate, including the three mouse sliders.
  const defaults = Array.from({ length: 32 }, () => ({ slider: 0, buttons: ['0:0', '0:0', '0:0', '0:0'] })); /* 0x4cb090 */
  const seed = (i, buttons, slider = 0) => Object.assign(defaults[i], { buttons, slider });
  seed(0, ['1:17', '1:31', '1:200', '1:208']); /* 0x4cb0b3 */
  seed(1, ['1:30', '1:32', '1:203', '1:205']); /* 0x4cb0f9 */
  seed(2, ['2:0', '0:0', '0:0', '0:0']); /* 0x4cb13f */
  seed(3, ['1:19', '0:0', '1:82', '0:0']); /* 0x4cb14f */
  seed(4, ['1:57', '0:0', '1:54', '0:0']); /* 0x4cb16f */
  seed(5, ['1:46', '0:0', '1:157', '0:0']); /* 0x4cb18f */
  defaults[11].slider = 2; defaults[12].slider = 1; /* 0x4cb1b2, 0x4cb1be */
  seed(13, ['1:18', '0:0', '1:28', '0:0']); /* 0x4cb1ca */
  for (let i = 14; i <= 19; i++) defaults[i].buttons[0] = '1:' + (i - 12); /* 0x4cb1ea..0x4cb245 */
  defaults[25].buttons[0] = '1:34'; /* 0x4cb24a */
  seed(26, ['1:26', '1:27', '0:0', '0:0'], 3); /* 0x4cb25a */
  seed(27, ['1:25', '0:0', '1:197', '0:0']); /* 0x4cb2a6 */
  seed(28, ['1:15', '0:0', '1:14', '0:0']); /* 0x4cb286 */
  seed(29, ['1:16', '0:0', '1:79', '0:0']); /* 0x4cb2c9 */
  seed(31, ['2:1', '0:0', '1:42', '0:0']); /* 0x4cb2e9 */
  const yesno = [{ v: '0', label: 'Off' }, { v: '1', label: 'On' }];
  const options = [
    { id: 'mouseX', reg: 'Mouse Sensitivity X', label: 'Horizontal sensitivity', def: '5000' }, /* 0x7cf908, 0x8a9690 */
    { id: 'mouseY', reg: 'Mouse Sensitivity Y', label: 'Vertical sensitivity', def: '5000' }, /* 0x7cf91c, 0x8a9694 */
    { id: 'invertY', reg: 'Invert Mouse', label: 'Invert mouse', def: '0', values: yesno }, /* 0x7cf8e4, 0x8a968c */
    { id: 'invertWheel', reg: 'Invert Mouse Wheel', label: 'Invert mouse wheel', def: '0', values: yesno }, /* 0x7cf8f4, 0x8a968d */
    { id: 'doubleClick', reg: 'Double-Click Speed', label: 'Double-click speed', def: '5000' }, /* 0x4cb33e, 0x7cf958 */
  ].map(o => ({ group: 'controls', compact: true, hint: 'Saved when the game starts.',
    values: Array.from({ length: 21 }, (_, i) => ({ v: String(i * 500), label: (i * 5) + '%' })), ...o })); /* 0x4cb3d0: 0..10000 */
  options[4].values = Array.from({ length: 21 }, (_, i) => ({ v: String(i * 500), label: Math.round(150 + i * 27.5) + ' ms' })); /* 0x51ad30: seconds = 0.15 + slider * 0.55 */
  options.push(...[
    ['crosshair', 'Crosshair Toggle', 'Crosshair'], /* 0x7cc480 -> 0x89aecc */
    ['autoAim', 'AutoAim Toggle', 'Aim assist'], /* 0x7cc494 -> 0x89aecd */
    ['autoWeapon', 'AutoWeaponSelect Toggle', 'Auto weapon select'], /* 0x7cc4a4 -> 0x891b8c */
    ['bulletAid', 'BulletTimeAid Toggle', 'Bullet time assist'], /* 0x7cc4d0 -> 0x86c484 */
  ].map(([id, reg, label]) => ({ id, reg, other: true, label, group: 'controls', compact: true, def: '',
    values: [{ v: '', label: 'Game value' }, ...yesno], hint: 'Saved when the game starts. Game value keeps what the game has.' })));
  // Descriptions refer to the original consumers, not browser input transforms.
  const descriptions = {
    mouseX: 'How far the view turns when you move the mouse sideways.', /* 0x519850 */
    mouseY: 'How far the view tilts when you move the mouse up or down.', /* 0x742870 */
    invertY: 'Moving the mouse up looks down.', /* 0x519960 */
    invertWheel: 'Reverses the direction the wheel cycles weapons.', /* 0x576430 */
    doubleClick: 'The longest gap between two clicks that still counts as a double click.', /* 0x51ad30 */
    crosshair: 'Shows or hides the crosshair.', /* 0x6d8920 */
    autoAim: 'The game helps you aim.', /* 0x6d8940 */
    autoWeapon: 'The game switches to a better weapon when you pick one up.', /* 0x695890 */
    bulletAid: 'The game helps you aim in bullet time.', /* 0x4554b0 */
  };
  options.forEach(o => { o.hint = descriptions[o.id] + ' Saved when the game starts.'; });
  let initialized = false;
  function init() {
    if (initialized) return options;
    initialized = true;
    // Reuse the browser input bridge's physical scan codes, including its reserved-key aliases.
    const keys = Object.entries(DIK).filter(([code]) => !/^(Escape|F\d+|KeyF$|KeyN$|Meta|ContextMenu|Backquote|IntlBackslash)/.test(code));
    const bindings = [{ v: '0:0', label: 'None' }, ...keys.map(([code, n]) => ({ v: '1:' + n, label: code.replace(/^Key|^Digit/, '') })),
      ...Array.from({ length: 3 }, (_, n) => ({ v: '2:' + n, label: ['Left mouse', 'Right mouse', 'Middle mouse'][n] }))]; /* 0x519a00 */
    defaults.forEach((a, i) => a.buttons.forEach((v, slot) => options.push({ id: `bind${i}_${slot}`, action: i, slot, group: 'keys' + i,
      label: ['Primary +', 'Primary −', 'Secondary +', 'Secondary −'][slot], compact: true, binding: true, values: bindings.slice(), def: v,
      hint: 'Enter, then press a key. Esc cancels, Backspace clears. ← → picks a key or mouse button. + and − are the two directions.' }))); /* 0x4cdc70, 0x4ce0a0 */
    return options;
  }
  async function backend() {
    if ((globalThis.RN_PUBLIC || new URLSearchParams(location.search).get('files') !== 'server') && navigator.storage?.getDirectory) {
      const root = await navigator.storage.getDirectory();
      if (globalThis.RN_PUBLIC) return await root.getDirectoryHandle('save', { create: true });
      const game = await root.getDirectoryHandle('game').catch(() => null);
      if (game && await game.getFileHandle('.mp-copied').then(() => true, () => false))
        return await root.getDirectoryHandle('save', { create: true });
    }
    return null;
  }
  async function read(dir) {
    if (dir) {
      try { return await (await (await dir.getFileHandle('registry.tsv')).getFile()).text(); }
      catch (e) { if (e.name === 'NotFoundError') return ''; throw e; }
    }
    const r = await fetch('/save/registry.tsv', { cache: 'no-store' });
    if (r.status === 404) return '';
    if (!r.ok) throw new Error('registry.tsv: HTTP ' + r.status);
    return r.text();
  }
  const linesOf = text => text.split('\n').filter(Boolean).map(l => l.replace(/\r$/, '').split('\t'));
  const sameKey = (key, expected = KEY) => key.toLowerCase() === expected.toLowerCase();
  function profile(lines, expected = KEY) {
    const p = new Map();
    for (const [key, name, type, hex] of lines) if (sameKey(key, expected) && type === '4' && /^[\da-f]{8}$/i.test(hex || ''))
      p.set(name, parseInt(hex.match(/../g).reverse().join(''), 16));
    return p;
  }
  function original(p) {
    if ((p.get('Input Settings Version') || 0) >= VERSION) return p;
    p = new Map([['Input Settings Version', VERSION]]);
    defaults.forEach((a, i) => {
      p.set(field(i, 'Slider type'), a.slider);
      a.buttons.forEach((b, slot) => { const [type, value] = b.split(':').map(Number); p.set(field(i, `Button${slot} type`), type); p.set(field(i, `Button${slot} value`), value); });
    });
    options.filter(o => o.reg && !o.other).forEach(o => p.set(o.reg, +o.def));
    return p;
  }
  async function load() {
    const lines = linesOf(await read(await backend())), p = original(profile(lines)), other = profile(lines, OTHER), opts = {};
    for (const o of options) {
      if (o.other) { opts[o.id] = other.has(o.reg) ? String(other.get(o.reg) ? 1 : 0) : ''; continue; }
      let n = o.reg ? p.get(o.reg) ?? (o.id === 'doubleClick' ? 500 : +o.def) : 0; /* 0x4cb3d0: missing-value defaults */
      if (o.id === 'invertY' || o.id === 'invertWheel') n = n ? 1 : 0;
      else if (n > 10000) n = 5000; /* 0x4cb3d0: invalid normalized values become 0.5 */
      const v = o.reg ? String(n) : `${p.get(field(o.action, `Button${o.slot} type`)) || 0}:${p.get(field(o.action, `Button${o.slot} value`)) || 0}`;
      if (!o.values.some(x => x.v === v)) o.values.push({ v, label: o.binding ? 'Binding ' + v : o.id === 'doubleClick' ? Math.round(150 + +v * 0.055) + ' ms' : String(+v / 100) + '%' });
      opts[o.id] = v;
    }
    return opts;
  }
  async function write(opts) {
    if (!opts || !Object.keys(opts).length) return;
    const dir = await backend(), before = await read(dir), lines = linesOf(before), p = original(profile(lines)), other = new Map();
    for (const o of options) if (Object.hasOwn(opts, o.id) && o.values.some(x => x.v === opts[o.id])) {
      if (o.other) { if (opts[o.id] !== '') other.set(o.reg, +opts[o.id]); }
      else if (o.reg) p.set(o.reg, +opts[o.id]);
      else { const [type, value] = opts[o.id].split(':').map(Number); p.set(field(o.action, `Button${o.slot} type`), type); p.set(field(o.action, `Button${o.slot} value`), value); }
    }
    // Keep unrelated registry values; REG_DWORD is little endian (host/win32.c reg_init).
    const hex = n => Array.from({ length: 4 }, (_, i) => ((n >>> (i * 8)) & 255).toString(16).padStart(2, '0')).join('');
    const kept = lines.filter(([key, name]) => (!sameKey(key) || !p.has(name)) && (!sameKey(key, OTHER) || !other.has(name))).map(l => l.join('\t'));
    const text = [...kept, ...Array.from(p, ([name, n]) => [KEY, name, '4', hex(n)].join('\t')), ...Array.from(other, ([name, n]) => [OTHER, name, '4', hex(n)].join('\t'))].join('\n') + '\n';
    if (text === before) return;
    if (dir) { const w = await (await dir.getFileHandle('registry.tsv', { create: true })).createWritable(); await w.write(text); await w.close(); }
    else { const r = await fetch('/save/registry.tsv', { method: 'PUT', body: text }); if (!r.ok) throw new Error('registry.tsv: HTTP ' + r.status); }
  }
  window.MPControls = { actions, init, load, write };
})();
