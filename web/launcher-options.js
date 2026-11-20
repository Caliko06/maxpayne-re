// Launcher data and the mapping from the player's choice to what the boot already accepts. No game code here: every value
// below ends in an env var the host reads, a PROFILES name, or an RN_JUMP line host/jump.c runs. Plain script (no modules):
// window.MPLaunch = { OPTIONS, LEVELS, saves(), apply(choice) }. web/launcher.js draws it; web/index.html boots with the result.
'use strict';
(() => {
  // ---- what the boot takes -----------------------------------------------------------------------------------------------------
  // index.html preRun sets RN_SCREEN, then PROFILES[profile].env, then every ?env= pair, so what apply() returns in env wins
  // over the page's own values (web/index.html:171-181).
  //
  // Loading a save at boot (modes 'continue' and 'save'): host/jump.c runs S_LoadGame( / S_LoadLastSavedGame( from the menu as system
  // lines (jump.c "sys"). Like gm_init, the message only queues the request (GameMode +0xb4 = 2, rea/map/files.md:216, ai.md:39); the
  // switch to the game, "x_modeswitch->s_modeswitch(game);", makes the game's tick run it. Tested 2026-11-07, test port, a copy of the
  // OWNER's autosave: S_LoadLastSavedGame alone stays in the menu; with s_modeswitch(game) after it the player is in the saved level.
  // S_LoadLastSavedGame reads the registry value "Last Saved Game Filename" (written by every save/load); S_LoadGame(n) takes the
  // slot number (0 autosave, 1 quicksave, 3..12 manual).
  const HOST_LOADS = true;

  // ---- levels -------------------------------------------------------------------------------------------------------------------
  // Titles: x_english.ras stringdefines LNID_* ("Part I, Chapter Two"). Level to title: x_data 36_levels.txt LevelName/Level pairs
  // (part1_level1b = Chapter Two; part2_level0 and part3_level1 are the Prologues; part3_level2 = Chapter One), same table as
  // tools/ras.py:129-135 (the in-game Cheats page).
  const NUM = ['One', 'Two', 'Three', 'Four', 'Five', 'Six', 'Seven', 'Eight', 'Nine'];
  const LEVELS = [];
  const part = (name, ids) => ids.forEach((id, i) => {
    const prologue = i === 0, chapter = prologue ? 'Prologue' : 'Chapter ' + NUM[i - 1];
    LEVELS.push({ id, part: name, chapter, label: name + ', ' + chapter, ord: i });   // ord: 0 = Prologue, 1..9 = the chapter number (Spanish UI says "Capítulo 8")
  });
  part('Part I', ['part0_level1', 'part1_level1', 'part1_level1b', 'part1_level2', 'part1_level2b', 'part1_level3', 'part1_level3b', 'part1_level4', 'part1_level5', 'part1_level6']);
  part('Part II', ['part2_level0', 'part2_level1', 'part2_level2', 'part2_level2b', 'part2_level3', 'part2_level4']);
  part('Part III', ['part3_level1', 'part3_level2', 'part3_level2b', 'part3_level3', 'part3_level4', 'part3_level5', 'part3_level5b', 'part3_level6', 'part3_level7']);
  LEVELS.push({ id: 'tutorial', part: 'Tutorial', chapter: 'Tutorial', label: 'Tutorial' });   // 36_levels.txt:1068, not part of the story

  // Checkpoint names: route.json (web/ffwd.js) writes them in Spanish for the in-game panel; the launcher shows them in English.
  const MILE_EN = {"El almacén": "The warehouse", "El altavoz del ático": "The attic speaker", "El andén del metro": "The subway platform", "El ascensor": "The elevator", "El bar": "The bar", "El barco": "The ship", "El callejón": "The alley", "El comedor": "The dining room", "El control de energía": "The power control", "El cuarto de droga": "The drug room", "El cuarto de trabajo": "The work room", "El cuarto del guardia": "The guard's room", "El dormitorio": "The bedroom", "El escenario": "The stage", "El final": "The end", "El hueco del ascensor": "The elevator shaft", "El laberinto rojo": "The red maze", "El laboratorio": "The lab", "El montacargas": "The freight lift", "El muelle": "The dock", "El pasillo": "The hallway", "El pasillo de la mina": "The mine tunnel", "El patio": "The yard", "El patio de la grúa": "The crane yard", "El piso de Horne": "Horne's floor", "El puente": "The bridge", "El sótano": "The basement", "El sótano A1": "Basement A1", "El taller": "The workshop", "El terminal": "The terminal", "El vestíbulo": "The lobby", "La acería": "The steel mill", "La armería": "The armory", "La azotea": "The rooftop", "La biblioteca": "The library", "La cabina": "The cabin", "La cúpula": "The dome", "La entrada": "The entrance", "La entrada del hotel": "The hotel entrance", "La entrada nevada": "The snowy entrance", "La escalera": "The stairs", "La esclusa": "The lock", "La fábrica de carne": "The meat plant", "La galería": "The gallery", "La grúa": "The crane", "La habitación del bebé": "The baby's room", "La oficina": "The office", "La primera planta": "The first floor", "La puerta de salida": "The exit door", "La puerta del sótano": "The basement door", "La radio": "The radio", "La rampa": "The ramp", "La sala": "The hall", "La sala central": "The main hall", "La sala de control": "The control room", "La sala de máquinas": "The engine room", "La sala de seguridad": "The security room", "La salida": "The exit", "La segunda oleada": "The second wave", "La trampa": "The trap", "La ventana al vacío": "The window over the drop", "La última oficina": "The last office", "Las cajas": "The crates", "Las cloacas": "The sewers", "Las cubas": "The vats", "Las escaleras": "The stairways", "Las oficinas": "The offices", "Las plantas": "The floors", "Las puertas cortafuego": "The fire doors", "Las torres": "The towers", "Las vías": "The tracks", "Los almacenes": "The storerooms", "Los cables": "The cables", "Los contenedores": "The containers", "Los hornos": "The furnaces", "Los pasillos": "The corridors", "Los pasillos del sueño": "The dream corridors", "Lupino": "Lupino", "Mona": "Mona", "Salida del vestíbulo": "Lobby exit", "Segunda oleada del vestíbulo": "Lobby, second wave", "Fin del capítulo": "End of chapter"};

  // ---- options ------------------------------------------------------------------------------------------------------------------
  const auto = { v: '', label: 'Auto' };
  const values = ns => ns.map(n => ({ v: String(n), label: String(n) }));
  // Presets write the ordinary options; aspect, gamma and Retina stay the player's choice.
  const PRESETS = {
    rendimiento: { escala: '50', escalado: 'bilinear', nitidez: '0', aniso: '4', fps: '60', texturas: '1', maxtex: '2048' },
    equilibrado: { escala: '75', escalado: 'fsr', nitidez: '4', aniso: '8', fps: '60', texturas: '2', maxtex: '4096' },
    calidad: { escala: '100', escalado: 'fsr', nitidez: '4', aniso: '16', fps: '0', texturas: '2', maxtex: '4096' },
  };
  const OPTIONS = [
    { id: 'preset', group: 'options', label: 'Preset', hint: 'Sets Display and Textures in one go. Aspect, gamma and Retina stay as you set them.', type: 'select', video: true,
      values: [{ v: 'rendimiento', label: 'Light' }, { v: 'equilibrado', label: 'Balanced' }, { v: 'calidad', label: 'Quality' }, { v: 'personalizado', label: 'Custom' }], def: 'personalizado' },
    // PROFILES full | low: memory of the wasm and the host caches (web/index.html:91-94, makeMemory 99; 'full' falls back to 'low' if refused).
    { id: 'perfil', group: 'options', label: 'Memory', hint: 'Low uses 640 MB and smaller textures, for machines with little memory.', type: 'select',
      values: [auto, { v: 'full', label: 'Full (1.9 GB)' }, { v: 'low', label: 'Low (640 MB)' }], def: '' },
    { id: 'escala', group: 'options', section: 'Display', video: true, label: 'Render scale', hint: 'Share of the auto resolution. Fewer pixels, lighter rendering.', type: 'select',
      values: [50, 67, 75, 85, 100].map(n => ({ v: String(n), label: n + ' %' })), def: '100' },
    { id: 'escalado', group: 'options', section: 'Display', video: true, label: 'Upscaling', hint: 'The filter that stretches the picture to your screen.', type: 'select',
      values: [{ v: 'bilinear', label: 'Bilinear' }, { v: 'fsr', label: 'FSR1' }, { v: 'nearest', label: 'Sharp pixels' }], def: 'bilinear' },
    { id: 'nitidez', group: 'options', section: 'Display', video: true, label: 'Sharpness', hint: 'FSR1 sharpening, 0 to 20. Left and right change it.', type: 'select', compact: true,
      values: values(Array.from({ length: 21 }, (_, i) => i)), def: '0' },
    { id: 'proporcion', group: 'options', section: 'Display', video: true, label: 'Aspect', hint: '4:3 keeps the original frame with side bars. A set resolution wins over this.', type: 'select',
      values: [{ v: 'auto', label: 'Widescreen' }, { v: '4:3', label: '4:3 original' }], def: 'auto' },
    { id: 'fps', group: 'options', section: 'Display', video: true, label: 'Frame cap', hint: 'Display follows your monitor refresh rate.', type: 'select',
      values: [{ v: '0', label: 'Display' }, ...values([30, 60, 120])], def: '0' },
    { id: 'hidpi', group: 'options', section: 'Display', video: true, label: 'Retina', hint: 'Auto resolution uses the real pixel density of the screen.', type: 'select',
      values: [{ v: '1', label: 'On' }, { v: '0', label: 'Off' }], def: '1' },
    { id: 'gamma', group: 'options', section: 'Display', video: true, label: 'Gamma', hint: 'Brightness, 0.8 to 1.2. 1 is the original.', type: 'select', compact: true,
      values: Array.from({ length: 9 }, (_, i) => { const v = (0.8 + i * 0.05).toFixed(2); return { v, label: v }; }), def: '1.00' },
    // RN_SCREEN WxH: host/win32.c:1298 (saved display mode) and :1848 (screen size, 640x480 .. 7680x4320); page default playSize() index.html:81.
    { id: 'resolucion', group: 'options', section: 'Display', video: true, label: 'Resolution', hint: 'Render size. Auto follows the screen, up to 2560.', type: 'select',
      values: [auto, { v: '1280x720', label: '1280 x 720' }, { v: '1600x900', label: '1600 x 900' }, { v: '1920x1080', label: '1920 x 1080' }, { v: '2560x1440', label: '2560 x 1440' }], def: '' },
    // RN_TEXDETAIL 0..2: Level and Other Texture Detail of the game's own Advanced Settings, host/win32.c:1306-1310 (0 halves each side of a map).
    { id: 'texturas', group: 'options', section: 'Textures', video: true, label: 'Detail', hint: 'Less detail loads faster and uses less memory.', type: 'select',
      values: [auto, { v: '2', label: 'High' }, { v: '1', label: 'Medium' }, { v: '0', label: 'Low' }], def: '' },
    { id: 'aniso', group: 'options', section: 'Textures', video: true, label: 'Anisotropic filtering', hint: 'Sharper surfaces seen at an angle.', type: 'select',
      values: values([1, 2, 4, 8, 16]), def: '1' },
    // RN_MAXTEX: host/d3d8.c:216 caps the game's own texture sizes.
    { id: 'maxtex', group: 'options', section: 'Textures', video: true, label: 'Max texture size', hint: 'Caps the texture size the game keeps in memory.', type: 'select',
      values: values([1024, 2048, 4096]), def: '4096' },
    // Cheats: console aliases of x_data 2_console.txt run as RN_JUMP lines once the level is up with control (host/jump.c:121-122).
    // One statement per line ('|'), commas written '~' (jump.c:119 turns them back; the web ?env= list splits on ',').
    { id: 'dios', group: 'cheats', label: 'God mode', hint: 'Immortal, full health.', type: 'toggle',
      values: [{ v: '0', label: 'Off' }, { v: '1', label: 'On' }], def: '0' },
    { id: 'armas', group: 'cheats', label: 'All weapons', hint: 'Every weapon, with ammo.', type: 'toggle',
      values: [{ v: '0', label: 'Off' }, { v: '1', label: 'On' }], def: '0' },
    { id: 'municion', group: 'cheats', label: 'Infinite ammo', hint: 'Ammo never runs out.', type: 'toggle',
      values: [{ v: '0', label: 'Off' }, { v: '1', label: 'On' }], def: '0' },
    { id: 'bala', group: 'cheats', label: 'Free bullet time', hint: 'Bullet time whenever you want.', type: 'toggle',
      values: [{ v: '0', label: 'Off' }, { v: '1', label: 'On' }], def: '0' },
    { id: 'noclip', group: 'cheats', label: 'No clipping', hint: 'Walk through walls and objects.', type: 'toggle',
      values: [{ v: '0', label: 'Off' }, { v: '1', label: 'On' }], def: '0' },
    // group 'newgame': no option maps to anything the boot reads. Difficulty (normal/nightmare/hell/timedmode) is chosen by the game's
    // own New Game menu; at boot it would need `gm_setgamelevel(` in jump.c's `sys` test (see the report), so it is left out.
  ];

  // The lines of each cheat (statements as the console runs them: host/cheats.c:49-54 sends the same ones from F3/F4/F6, key handler :95).
  const WEAPONS = ['leadpipe', 'baseballbat', 'beretta', 'berettadual', 'deserteagle', 'sawedshotgun', 'pumpshotgun', 'ingram', 'ingramdual',
    'sniper', 'mp5', 'jackhammer', 'm79', 'grenade', 'molotov', 'painkiller'];                           // 2_console.txt:117
  const AMMO = { berettadual: 1000, deserteagle: 1000, sawedshotgun: 1000, pumpshotgun: 1000, ingramdual: 1000, sniper: 1000, mp5: 1000, jackhammer: 1000, m79: 20, grenade: 20, molotov: 20, painkiller: 20 };
  const CHEATS = {
    dios: () => ['player->C_SetImmortal(false);', 'player->C_SetHealth(1);', 'player->C_SetImmortal(true);'],      // cheats.c GOD_ON
    armas: () => WEAPONS.flatMap(w => [`player->c_pickupweapon(${w});`].concat(AMMO[w] ? [`player->c_pickupammo(${w}~${AMMO[w]});`] : [])),
    municion: () => ['empty', ...WEAPONS].map(w => `player->C_SetInfiniteAmmo(${w}~1);`),               // 2_console.txt:118
    bala: () => ['maxpayne_gamemode->GM_SetPlayerControlledSlowMotion(1);'],                              // 2_console.txt:76
    noclip: () => ['player->C_EnableDOCollisions(false);'],                                               // cheats.c NOCLIP_ON
  };

  // ---- saves --------------------------------------------------------------------------------------------------------------------
  // Where they live: OPFS save/ when the game files are in OPFS (web/opfs.js files(): save/ is walked into RN_FILES), else tools/serve.py
  // GET /save-files.json + /save/<rel> (serve.py:131-143). Path: "Max Payne Savegames/savegameNNN.mps", NNN = slot (rea/map/files.md:18-20:
  // 0 autosave, 1 quicksave, 2 quicksave backup, 3..12 manual). Header (files.md:72, read on real files): 0c, chunk 1 size at bytes 9..12,
  // then int 0x30, 0x0d 0x14 <len> "<description>" (e.g. "Auto: Fri Nov 06 18:11 - Part II, Chapter Four"); the level name string
  // ("part2_level3") sits right after chunk 1, at 13 + size.
  const SAVEDIR = 'Max Payne Savegames';
  const SLOT = ['Autosave', 'Quicksave', 'Quicksave (backup)'];
  const latin = u8 => Array.from(u8, c => String.fromCharCode(c)).join('');
  async function opfsDir() {
    if (!navigator.storage?.getDirectory) return null;
    const root = await navigator.storage.getDirectory();
    if (globalThis.RN_PUBLIC) {
      const save = await root.getDirectoryHandle('save').catch(() => null);
      return save && await save.getDirectoryHandle(SAVEDIR).catch(() => null);
    }
    const game = await root.getDirectoryHandle('game').catch(() => null);
    if (!game || !await game.getFileHandle('.mp-copied').then(() => 1, () => 0)) return null;       // the same test as MPFiles.files()
    const save = await root.getDirectoryHandle('save').catch(() => null);
    return save && await save.getDirectoryHandle(SAVEDIR).catch(() => null);
  }
  async function slotsFromOpfs(dir) {
    const out = [];
    for await (const [name, h] of dir.entries()) {
      const m = /^savegame(\d+)\.mps$/i.exec(name);
      if (!m || h.kind !== 'file') continue;
      const f = await h.getFile();
      out.push({ slot: +m[1], when: f.lastModified, read: async (a, b) => new Uint8Array(await f.slice(a, b).arrayBuffer()) });
    }
    return out;
  }
  async function slotsFromServer() {
    const list = await (await fetch('/save-files.json', { cache: 'no-store' })).json();
    return list.map(([rel]) => [rel, /(?:^|\/)savegame(\d+)\.mps$/i.exec(rel)]).filter(([, m]) => m).map(([rel, m]) => ({
      slot: +m[1], when: 0,
      read: async (a, b) => {
        const r = await fetch('/save/' + rel.split('/').map(encodeURIComponent).join('/'), { headers: { Range: `bytes=${a}-${b - 1}` }, cache: 'no-store' });
        const u = new Uint8Array(await r.arrayBuffer());
        return r.status === 206 ? u : u.subarray(a, b);
      } }));
  }
  // Newest-save order (launcher v2 spec 2.7a): the file's mtime, else the description's "Fri Nov 06 18:11" with the current year (previous
  // year if that lands in the future).
  function whenOf(s, date) {
    if (s.when) return s.when;
    const m = /\w{3} (\w{3}) (\d\d) (\d\d):(\d\d)/.exec(date || ''); if (!m) return 0;
    const mo = 'JanFebMarAprMayJunJulAugSepOctNovDec'.indexOf(m[1]) / 3; if (mo < 0 || mo % 1) return 0;
    const now = new Date(); let t = new Date(now.getFullYear(), mo, +m[2], +m[3], +m[4]).getTime();
    if (t > now.getTime() + 864e5) t = new Date(now.getFullYear() - 1, mo, +m[2], +m[3], +m[4]).getTime();
    return t;
  }
  // The screenshot the game stores in every save, right after the description: tag 0x12, an int (the size), a TGA header (18 bytes, 24 bpp, top-left),
  // BGR pixels (read on real files, 256x192). Lazy and once: 147 KB per save. Returns a function giving a promise of an image URL, null if no picture.
  function thumbOf(s, head, tag) {
    const tga = tag + 5;
    if (head[tag] !== 0x12 || head[tga + 2] !== 2 || head[tga + 16] !== 24) return null;
    const w = head[tga + 12] | head[tga + 13] << 8, h = head[tga + 14] | head[tga + 15] << 8;
    if (!w || !h || w > 1024 || h > 1024) return null;
    let got = null;
    return () => got || (got = (async () => {
      const px = await s.read(tga + 18, tga + 18 + w * h * 3), im = new ImageData(w, h);
      for (let i = 0, o = 0; i < w * h * 3; i += 3, o += 4) { im.data[o] = px[i + 2]; im.data[o + 1] = px[i + 1]; im.data[o + 2] = px[i]; im.data[o + 3] = 255; }
      const c = document.createElement('canvas'); c.width = w; c.height = h; c.getContext('2d').putImageData(im, 0, 0);
      return URL.createObjectURL(await new Promise(r => c.toBlob(r, 'image/png')));
    })());
  }
  async function describe(s) {
    const head = await s.read(0, 200);
    if (head[0] !== 0x0c) return null;                                                               // not a save (0x4109c0 wants chunk 1)
    const size = new DataView(head.buffer, head.byteOffset).getUint32(9, true);
    const at = latin(head).indexOf('\x0d\x14', 13);
    const desc = at < 0 ? '' : latin(head.subarray(at + 3, at + 3 + head[at + 2]));
    const tail = latin(await s.read(13 + size, 13 + size + 100));
    const lv = [...LEVELS].sort((a, b) => b.id.length - a.id.length).find(l => tail.includes(l.id));      // longest first: part1_level1b before part1_level1
    const date = (/\w{3} \w{3} \d\d \d\d:\d\d/.exec(desc) || [''])[0] || (s.when ? new Date(s.when).toLocaleString('en-US') : '');
    return { id: String(s.slot), label: (SLOT[s.slot] || 'Manual ' + (s.slot - 2)) + ': ' + (desc.replace(/^(Auto|Quick): [^-]*- /, '') || (lv ? lv.label : '')),
             level: lv ? lv.id : '', date, when: whenOf(s, date), thumb: at < 0 ? null : thumbOf(s, head, at + 3 + head[at + 2]) };
  }
  async function saves() {
    try {
      const dir = await opfsDir().catch(() => null);
      const slots = dir ? await slotsFromOpfs(dir) : await slotsFromServer();
      const rows = (await Promise.all(slots.sort((a, b) => a.slot - b.slot).map(s => describe(s).catch(() => null)))).filter(Boolean);
      return rows;
    } catch (e) { return []; }
  }

  // ---- choice -> boot -----------------------------------------------------------------------------------------------------------
  // choice = {mode: 'continue'|'new'|'chapter'|'save', level?, step?, save?, opts: {id: v}}  ->  {profile, env, files, ff, video, scale, aspect}
  // step >= 0: a milestone of the chapter; the page hands it to the autopilot (web/ffwd.js) when the level is up.
  function apply(choice) {
    const o = Object.assign({}, ...OPTIONS.map(x => ({ [x.id]: x.def })), choice.opts || {});
    const env = {}, jump = [];
    // Untouched options keep the existing renderer path, including old automation URLs.
    const touched = OPTIONS.some(x => x.video && Object.hasOwn(choice.opts || {}, x.id));
    const video = touched ? { up: o.escalado, sharp: +o.nitidez / 20, aniso: +o.aniso, fps: +o.fps, gamma: +o.gamma, hidpi: o.hidpi === '1' } : undefined;
    if (o.maxtex !== '4096') env.RN_MAXTEX = o.maxtex;
    // chapter: gm_init + s_modeswitch(game) is the pair runtime-proven from the menu (host/jump.c:5-9, tools/ghost/ghost.mjs:906 route()).
    if (choice.mode === 'chapter' && LEVELS.some(l => l.id === choice.level))
      jump.push(`maxpayne_gamemode->gm_init(${choice.level});`, 'x_modeswitch->s_modeswitch(game);');
    // continue / save: queue the load, then switch to the game (see HOST_LOADS above).
    else if (HOST_LOADS && (choice.mode === 'continue' || choice.mode === 'save') && /^\d+$/.test(String(choice.save)))
      jump.push(`x_modeswitch->S_LoadGame(${choice.save});`, 'x_modeswitch->s_modeswitch(game);');
    else if (HOST_LOADS && choice.mode === 'continue') jump.push('x_modeswitch->S_LoadLastSavedGame();', 'x_modeswitch->s_modeswitch(game);');
    // 'new': no line. The game's New Game menu asks for the difficulty and starts the Prologue itself.
    for (const id in CHEATS) if (o[id] === '1') jump.push(...CHEATS[id]());     // wait for the level up with control (jump.c:121-122), also after a comic
    if (jump.length) env.RN_JUMP = jump.join('|');
    if (o.resolucion) env.RN_SCREEN = o.resolucion;
    if (o.texturas !== '') env.RN_TEXDETAIL = o.texturas;
    const note = (choice.mode === 'continue' || choice.mode === 'save') && !HOST_LOADS ? 'menu' : '';   // the player picks Load in the game's menu
    const ff = choice.mode === 'chapter' && choice.step >= 0 ? { level: choice.level, step: choice.step } : null;
    return { profile: o.perfil === 'full' || o.perfil === 'low' ? o.perfil : null, env, files: null, note, ff, video, scale: +o.escala / 100, aspect: o.proporcion, controls: Object.fromEntries(Object.entries(choice.opts || {}).filter(([id]) => MPControls.init().some(x => x.id === id))) };
  }

  window.MPLaunch = { OPTIONS, PRESETS, LEVELS, MILE_EN, saves, apply, HOST_LOADS };
})();
