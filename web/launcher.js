// Pre-game launcher over the game's own menu picture. Rows in plain DOM type, a selection plate that travels, one card that explains the focused row,
// a strip of the 25 chapters with their milestones (web/ffwd.js). web/index.html calls MPLauncher.open() while the engine
// downloads. It returns null under automation (webdriver, ?env=, ?go, ?nolauncher, ?level; ?launcher=1 forces it) or { chosen, progress, fail }:
// `chosen` resolves with MPLaunch.apply(choice) ({profile, env, files, note}) after the player confirms a Play row, once the audio
// (web/launcher-audio.js, MPAudio) has released its context.
// Data (options, chapters, saves, the mapping to the boot) comes from web/launcher-options.js.
'use strict';
(() => {
  const KEY = 'mp.launcher.v2';
  const L = window.MPLaunch || { OPTIONS: [], LEVELS: [], saves: async () => [], apply: () => ({ profile: null, env: {}, files: null, note: '' }) };
  const store = {
    get() { try { return JSON.parse(localStorage.getItem(KEY)) || {}; } catch { return {}; } },
    set(o) { try { localStorage.setItem(KEY, JSON.stringify(o)); } catch { /* private mode: no memory, still plays */ } },
  };
  const EASE = { out: 'cubic-bezier(.16,1,.3,1)', std: 'cubic-bezier(.2,.8,.2,1)', inout: 'cubic-bezier(.65,0,.35,1)', snap: 'cubic-bezier(.7,0,.2,1)', in: 'cubic-bezier(.5,0,1,.6)', blade: 'cubic-bezier(.22,1.2,.36,1)' };
  const sleep = ms => new Promise(r => setTimeout(r, ms));
  const h = (tag, props, ...kids) => {
    const e = document.createElement(tag);
    for (const [k, v] of Object.entries(props || {})) {
      if (v == null || v === false) continue;
      if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
      else if (k === 'class') e.className = v;
      else e.setAttribute(k, v === true ? '' : v);
    }
    for (const c of kids.flat()) if (c != null && c !== false) e.append(c.nodeType ? c : document.createTextNode(c));
    return e;
  };
  const norm = s => String(s).normalize('NFD').replace(/[̀-ͯ]/g, '').toLowerCase();
  const audio = (fn, ...a) => { try { const m = window.MPAudio; return m && m[fn] ? m[fn](...a) : undefined; } catch { return undefined; } };
  const partEs = p => p;
  const chapEs = l => !l ? '' : l.id === 'tutorial' ? 'Tutorial' : l.ord ? `Chapter ${l.ord}` : 'Prologue';
  const ago = ms => {
    if (!ms) return ''; const m = Math.max(0, Math.round((Date.now() - ms) / 60000));
    return m < 1 ? 'just now' : m < 60 ? `${m} min ago` : m < 1440 ? `${Math.round(m / 60)} h ago` : `${Math.round(m / 1440)} d ago`;
  };

  function open() {
    const q = new URLSearchParams(location.search);
    if (q.get('launcher') !== '1' && (navigator.webdriver || q.has('env') || q.has('go') || q.has('nolauncher') || q.has('level'))) return null;

    const LEVELS = L.LEVELS || [], OPTIONS = [...(L.OPTIONS || []), ...MPControls.init()];
    const STORY = LEVELS.filter(l => l.id !== 'tutorial');
    const lv = id => LEVELS.find(l => l.id === id);
    const reducedQ = matchMedia('(prefers-reduced-motion: reduce)');
    let RM = reducedQ.matches; reducedQ.addEventListener?.('change', e => { RM = e.matches; });
    const seed = q.has('seed') ? +q.get('seed') : undefined, freezeAt = q.has('t') ? +q.get('t') : null;
    const startState = q.get('state') || 'cold';
    const TOUCH = window.MP_DEVICE && MP_DEVICE !== 'desktop';     // phone or tablet (web/index.html): taps, no keyboard legend

    // ---- DOM ---------------------------------------------------------------------------------------------------------------
    const still = h('img', { class: 'mpl-still', alt: '', 'aria-hidden': 'true', draggable: 'false' });
    const prompt = h('div', { class: 'mpl-prompt', 'aria-hidden': 'true' }, h('i', { class: 'line' }), h('span', { class: 't' }, TOUCH ? 'TAP TO START' : 'PRESS ANY KEY'), h('span', { class: 's' }, 'sound on'));
    const backdrop = h('div', { class: 'mpl-backdrop', 'aria-hidden': 'true' });
    const shade = h('div', { class: 'mpl-shade', 'aria-hidden': 'true' });
    const heading = h('div', { class: 'mpl-head', role: 'heading', 'aria-level': '1' }), headSub = h('span', { class: 'mpl-headsub' });
    const headBar = h('div', { class: 'mpl-headbar' }, heading, headSub);
    const blade = h('i', { class: 'mpl-blade', 'aria-hidden': 'true' });
    const list = h('div', { class: 'mpl-list', role: 'menu' }, blade);
    const col = h('nav', { class: 'mpl-col', 'aria-label': 'Menu' }, list);
    // The one card every screen explains its focused row with (Hades' save slot, TLOU2's chapter sheet): picture, title, real data, milestones.
    const heroPic = h('div', { class: 'frame' }), heroVoice = h('i', { class: 'voice' });
    const heroPanel = h('div', { class: 'panel' }, heroPic, heroVoice);
    const heroKick = h('div', { class: 'kick' }), heroTitle = h('div', { class: 'title' }), heroLines = h('div', { class: 'lines' });
    const heroTicks = h('div', { class: 'ticks', 'aria-hidden': 'true' }), heroBody = h('p', { class: 'body' }), heroMiles = h('div', { class: 'miles' });
    const heroMeta = h('dl', { class: 'mpl-setting-meta' });
    const hero = h('aside', { class: 'mpl-hero' }, heroPanel, h('div', { class: 'info' }, heroKick, heroTitle, heroLines, heroTicks, heroBody, heroMeta, heroMiles));
    const dl = h('div', { class: 'mpl-dl' }, h('i'));
    const noFilesEl = h('span', { class: 'mpl-nofiles' });
    const muteBtn = h('button', { class: 'mpl-mute', 'aria-pressed': 'false', 'aria-label': 'Sound', tabindex: '0' });
    const notice = h('a', { class: 'mpl-notice', href: '/notice.html', target: '_blank', rel: 'noopener',
      title: 'Fan project: not affiliated with or endorsed by the rights holders' }, 'Fan project');
    const legend = h('div', { class: 'mpl-legend' });
    const tip = h('div', { class: 'mpl-tip', 'aria-live': 'polite' });
    const foot = h('div', { class: 'mpl-foot' }, tip, h('div', { class: 'mpl-keys' }, noFilesEl, legend, notice, muteBtn, q.has('debug') ? h('span', {}, 'F2 log') : null), dl);
    const fail = h('div', { class: 'mpl-fail', role: 'alert' });
    const wordmark = h('div', { class: 'mpl-wordmark', 'aria-hidden': 'true' }, 'Max Payne');
    const live = h('div', { class: 'mpl-live', 'aria-live': 'polite' });
    const fps = h('div', { class: 'mpl-fps' });
    const black = h('div', { class: 'mpl-black' });
    const mpl = h('div', { id: 'mpl', role: 'dialog', 'aria-modal': 'true', 'aria-label': 'Max Payne', 'data-mode': 'cold', 'data-screen': 'main', 'data-still': 'off' },
      still, backdrop, shade, h('img', { class: 'mpl-brand', src: 'launcher/art2/private/logo.webp', alt: 'Max Payne' }), wordmark, headBar, hero, col, fail, foot, prompt, live, fps, black);
    if (q.has('debug')) mpl.dataset.debug = '1';
    document.documentElement.classList.add('mpl-on');
    (document.body || document.documentElement).append(mpl);

    // ---- state: one named object, one writer per transition (K4) ------------------------------------------------------------------
    const last = store.get();
    const ui = {
      screen: 'main', active: { id: null, source: 'key' }, returnId: null, choice: null, saves: [], progress: null,
      mode: 'cold',                        // cold | intro (reveal, input skips it) | reveal (rows live) | idle | transition | exit
      entering: false,
      opts: Object.assign({}, last.opts), rows: [], failed: false, device: null,
      covers: {}, narr: {}, narrText: {}, titles: {}, order: {}, base: '/gamefile/', newest: null, queue: null, noFiles: false,
      miles: [], mile: 0,                  // milestones of the focused chapter, and which one Enter starts from
    };
    let resolveChosen; const chosen = new Promise(r => { resolveChosen = r; });
    const gate = { ptr: { x: -9, y: -9 }, from: null, keyAt: {}, lastRepeat: 0, ahead: '', aheadAt: 0, wheelAt: 0, padPrev: [], padRepeat: 0, padHold: 0 };

    const setAttr = (k, v) => { if (v == null || v === false) mpl.removeAttribute(k); else mpl.setAttribute(k, v === true ? '' : v); };
    const mode = m => { ui.mode = m; mpl.dataset.mode = m; };
    const announce = t => { live.textContent = ''; setTimeout(() => { live.textContent = t; }, 30); };

    // ---- data: web/launcher/chapters.json (26 levels in order: cover, panels, narration with its transcript), saves, progress ---------------
    async function loadAssets() {
      try {
        const c = await (await fetch('launcher/chapters.json')).json();
        ui.base = c.base || '/gamefile/';
        for (const l of c.levels) {
          if (l.cover && l.cover.path) ui.covers[l.id] = ui.base + l.cover.path;
          if (l.narration && l.narration.path) { ui.narr[l.id] = l.narration.path; if (l.narration.text) ui.narrText[l.id] = l.narration.text; }
          if (l.chapter_title) ui.titles[l.id] = l.chapter_title;
          if (l.order != null) ui.order[l.id] = l.order;
        }
      } catch { /* no chapters.json: no covers, no narration */ }
    }
    const ordOf = id => ui.order[id] != null ? ui.order[id] : STORY.findIndex(l => l.id === id) + 1 || -1;
    function computeProgress() {
      const withLevel = ui.saves.filter(s => s.level && s.level !== 'tutorial' && ordOf(s.level) >= 0);
      let furthest = 0; for (const s of withLevel) furthest = Math.max(furthest, ordOf(s.level));
      const timed = withLevel.some(s => s.when);
      ui.newest = !withLevel.length ? null : timed ? withLevel.reduce((a, b) => (b.when || 0) > (a.when || 0) ? b : a) : (withLevel.find(s => s.id === '0') || withLevel[0]);
      const current = ui.newest ? ui.newest.level : null;
      ui.progress = { reached: STORY.filter(l => ordOf(l.id) <= furthest).map(l => l.id), current, furthest };
    }
    const isReached = id => ui.progress && ordOf(id) <= ui.progress.furthest;
    async function loadSaves() {
      try { ui.saves = (await L.saves()) || []; } catch { ui.saves = []; }
      computeProgress();
    }
    const optVal = o => o.values.find(x => x.v === ui.opts[o.id]) || o.values.find(x => x.v === o.def) || o.values[0];
    const cheatCount = () => OPTIONS.filter(o => o.group === 'cheats' && optVal(o).v === '1').length;
    const persist = () => store.set({ opts: ui.opts });
    const fixedByUrl = o => o.id === 'perfil' && q.has('profile');
    const titleOf = id => ui.titles[id] || chapEs(lv(id));
    const whereOf = id => { const l = lv(id); return l ? `${partEs(l.part)}, ${chapEs(l)}` : ''; };

    // ---- rows of each screen ----------------------------------------------------------------------------------------------------
    function rowsFor(screen) {
      const R = [];
      if (screen === 'main') {
        const n = ui.saves.length;
        if (n >= 1) R.push({ id: 'continuar', kind: 'play', label: 'Continue', choice: { mode: 'continue' } });
        R.push({ id: 'nueva', kind: 'play', label: 'New Game', choice: { mode: 'new' } });
        if (n >= 2) R.push({ id: 'cargar', kind: 'nav', label: 'Load Game', to: 'saves' });
        R.push({ id: 'capitulos', kind: 'nav', label: 'Chapters', to: 'chapters' });
        R.push({ id: 'opciones', kind: 'nav', label: 'Options', to: 'options' });
        R.push({ id: 'trucos', kind: 'nav', label: 'Cheats', to: 'cheats' });
      } else if (screen === 'chapters') {
        for (const l of STORY) R.push({ id: 'level:' + l.id, kind: 'play', label: titleOf(l.id), cell: true, level: l.id, part: l.part, ord: l.ord, choice: { mode: 'chapter', level: l.id } });
      } else if (screen === 'options') {
        if (!TOUCH) R.push({ id: 'controles', kind: 'nav', label: 'Mouse and Keys', to: 'controls' });
        for (const o of OPTIONS.filter(x => x.group === 'options')) R.push({ id: 'opt:' + o.id, kind: 'option', label: o.label, opt: o, disabled: fixedByUrl(o) });
      } else if (screen === 'controls' || /^keys\d+$/.test(screen)) {
        for (const o of OPTIONS.filter(x => x.group === screen)) R.push({ id: 'opt:' + o.id, kind: 'option', label: o.label, opt: o });
        if (screen === 'controls') R.push({ id: 'keys', kind: 'nav', label: 'Key Bindings', to: 'keys' });
      } else if (screen === 'keys') {
        MPControls.actions.forEach((label, i) => R.push({ id: 'keys' + i, kind: 'nav', label, to: 'keys' + i }));
      } else if (screen === 'cheats') {
        for (const o of OPTIONS.filter(x => x.group === 'cheats')) R.push({ id: 'cheat:' + o.id, kind: 'toggle', label: o.label, opt: o });
      } else if (screen === 'saves') {
        for (const s of ui.saves) R.push({ id: 'save:' + s.id, kind: 'play', label: s.label, save: s, choice: { mode: 'save', save: s.id } });
      }
      return R;
    }
    const HEADINGS = { main: '', chapters: 'Chapters', options: 'Options', cheats: 'Cheats', saves: 'Load Game', controls: 'Mouse and Keys', keys: 'Key Bindings', ...Object.fromEntries(MPControls.actions.map((label, i) => ['keys' + i, label])) };
    const moodFor = s => s === 'chapters' || s === 'saves' ? 'comic' : (s === 'options' || s === 'controls' || s.startsWith('keys')) ? 'options' : s === 'cheats' ? 'cheats' : 'main';

    function mkRow(d) {
      const numeric = d.opt?.reg && !d.opt.other && !d.opt.values.some(v => !/^\d+$/.test(v.v)) && d.opt.values.length > 3;
      const el = h(numeric ? 'div' : 'button', { class: 'mpl-row', role: d.kind === 'toggle' ? 'menuitemcheckbox' : 'menuitem', tabindex: '-1', 'data-id': d.id, 'aria-label': d.label });
      const lab = h('span', { class: 'lab' }, d.label);
      const r = { d, el, lab };
      if (d.cell) {
        el.classList.add('cell');
        const src = ui.covers[d.level];
        el.append(src ? h('img', { src, alt: '', loading: 'lazy', draggable: 'false' }) : null, h('span', { class: 'num' }, d.ord ? String(d.ord) : 'P'));
        if (!isReached(d.level)) el.dataset.fresh = '1';
        if (ui.progress && ui.progress.current === d.level) el.dataset.current = '1';
      } else if (d.save) {
        el.classList.add('save');
        r.pic = h('img', { alt: '', draggable: 'false' });
        const src = ui.covers[d.save.level]; if (src) r.pic.src = src;
        d.save.thumb?.().then(u => { if (u) r.pic.src = u; }).catch(() => {});
        el.append(r.pic, h('span', { class: 'txt' }, lab, h('small', {}, d.save.date)));
      } else if (d.kind === 'option') {
        el.classList.add('opt');
        if (d.opt.compact && (!d.opt.reg || d.opt.values.length > 3)) el.classList.add('compact');
        r.chips = d.opt.values.map(x => h('span', { class: 'chip', 'data-v': x.v }, x.label));
        if (numeric) {
          el.classList.add('range-row');
          r.value = h('output', { class: 'range-value' });
          r.range = h('input', { type: 'range', min: '0', max: '10000', step: '500', 'aria-label': d.label, tabindex: '-1' }); /* 0x4cb3d0 */
          r.range.addEventListener('input', () => {
            const i = d.opt.values.findIndex(v => +v.v === +r.range.value);
            if (i >= 0) setOpt(r, i);
          });
          r.range.addEventListener('click', e => e.stopPropagation());
          el.append(lab, h('span', { class: 'range-control' }, r.range, r.value));
        } else {
          if (d.opt.reg && d.opt.values.length <= 3) el.classList.add('switch-row');
          el.append(lab, h('span', { class: 'chips' }, r.chips));
        }
      } else if (d.kind === 'toggle') {
        el.classList.add('opt');
        el.append(lab, h('span', { class: 'sw' }, h('i')));
      } else el.append(lab);
      if (d.kind === 'play') el.dataset.play = '1';
      if (d.disabled) el.setAttribute('aria-disabled', 'true');
      if (d.opt && fixedByUrl(d.opt)) { el.setAttribute('aria-disabled', 'true'); lab.append(h('small', { class: 'locked' }, 'Set by the URL')); }
      if (d.kind === 'play' && ui.failed) el.setAttribute('aria-disabled', 'true');
      return r;
    }
    function paintValues(r) {
      const { d, el } = r;
      if (d.kind === 'option') {
        const v = optVal(d.opt); r.chips.forEach(c => c.toggleAttribute('data-on', c.dataset.v === v.v));
        el.setAttribute('aria-label', `${d.label}: ${v.label}`);
        if (r.range) { r.range.value = v.v; r.range.style.setProperty('--fill', (+v.v / 100) + '%'); r.value.textContent = v.label; r.range.setAttribute('aria-valuetext', v.label); }
      } else if (d.kind === 'toggle') {
        const on = optVal(d.opt).v === '1'; el.classList.toggle('on', on);
        el.setAttribute('aria-checked', on ? 'true' : 'false'); el.setAttribute('aria-label', `${d.label}: ${on ? 'on' : 'off'}`);
      }
    }
    function build(screen) {
      ui.capture = null; ui.screen = screen; mpl.dataset.screen = screen; mpl.classList.toggle('mpl-wide', ['options', 'cheats', 'saves', 'controls'].includes(screen) || screen.startsWith('keys'));
      ui.rows = rowsFor(screen).map(mkRow);
      const kids = [blade]; let part = null, section = null;
      for (const r of ui.rows) {                                    // the strip of chapters: one label per Part
        if (r.d.cell && r.d.part !== part) { part = r.d.part; kids.push(h('span', { class: 'sep' }, partEs(part))); }
        if (r.d.opt?.section && r.d.opt.section !== section) {
          section = r.d.opt.section; kids.push(h('span', { class: 'opt-section' }, section));
        }
        kids.push(r.el); paintValues(r);
      }
      if (screen === 'saves' && !ui.rows.length) kids.push(h('div', { class: 'mpl-empty', role: 'status' }, h('b', {}, 'No saved games yet'), 'Save from the game menu. Your chapter, the date and a picture will show here. Esc goes back.'));
      list.replaceChildren(...kids);
      if (!ui.rows.length) hero.removeAttribute('data-show');
      list.toggleAttribute('data-fail', ui.failed);
      ui.active = { id: null, source: ui.active.source }; list.removeAttribute('data-has-active');
      heading.textContent = HEADINGS[screen] || '';
      headSub.textContent = screen === 'chapters' && ui.progress ? `${ui.progress.reached.length} of ${STORY.length} reached` : screen === 'main' ? '' : `${ui.rows.length} ${screen === 'saves' ? 'saves' : screen === 'keys' ? 'actions' : 'options'}`;
      paintLegend(); setHint('');
    }
    function setHint(t) { if (tip.textContent !== t) tip.textContent = t; }
    // Keys for this screen, always in the same corner (Sifu, TLOU2); what they do changes with the screen, and each one also takes a click.
    const LEGEND = {
      main: [['↑↓', 'Move'], ['Enter', 'Select']],
      controls: [['↑↓', 'Move'], ['←→', 'Change'], ['Enter', 'Select'], ['Esc', 'Back']],
      keys: [['↑↓', 'Move'], ['Enter', 'Select'], ['Esc', 'Back']],
      ...Object.fromEntries(MPControls.actions.map((_, i) => ['keys' + i, [['↑↓', 'Move'], ['Enter', 'Bind'], ['←→', 'Change'], ['Esc', 'Back']]])),
      chapters: [['←→', 'Chapter'], ['↑↓', 'Checkpoint'], ['Enter', 'Play'], ['Esc', 'Back']],
      options: [['↑↓', 'Move'], ['←→', 'Change'], ['Esc', 'Back']],
      cheats: [['↑↓', 'Move'], ['Enter', 'Toggle'], ['Esc', 'Back']],
      saves: [['↑↓', 'Move'], ['Enter', 'Load'], ['Esc', 'Back']],
    };
    function paintLegend() {
      // A touch screen has no keys to show: only Back stays, as a button (the phone's way out of a screen).
      legend.replaceChildren(...(LEGEND[ui.screen] || []).filter(([k]) => !TOUCH || k === 'Esc').map(([k, t]) => {
        if (TOUCH) return h('button', { class: 'lg back', 'data-act': 'back', type: 'button' }, '‹ ' + t);
        const act = k === 'Esc' ? 'back' : k === 'Enter' ? 'enter' : k === '←→' ? 'adjust' : 'move';
        return h('button', { class: 'lg', 'data-act': act, type: 'button', 'aria-label': `${k}: ${t}` }, h('kbd', {}, k), t);
      }));
    }

    // ---- active row ----------------------------------------------------------------------------------------------------------------
    const rowOf = id => ui.rows.find(r => r.d.id === id);
    function placeBlade(r, jump) {                                  // the selection is an object that travels (Control's plate), not a colour
      if (r.d.cell) return;
      const wide = mpl.classList.contains('mpl-wide');
      if (jump || RM) { blade.style.transition = 'none'; requestAnimationFrame(() => { blade.style.transition = ''; }); }
      blade.style.transform = `translateY(${r.el.offsetTop}px)`;
      blade.style.height = r.el.offsetHeight + 'px';
      blade.style.width = wide ? '100%' : `calc(${r.lab.offsetWidth}px + var(--pad) * 2)`;
    }
    // The ONLY place that writes ui.active, and sets data-active.
    function setActive(id, source, o = {}) {
      const r = rowOf(id); if (!r) return;
      const prev = ui.active.id; ui.active = { id, source };
      if (id === prev && !o.force) return;
      ui.rows.forEach(x => { const on = x === r; x.el.toggleAttribute('data-active', on); x.el.tabIndex = on ? 0 : -1; });
      muteBtn.removeAttribute('data-active'); list.setAttribute('data-has-active', '');
      if ((source === 'key' || source === 'pad') && !o.silent) try { r.el.focus({ preventScroll: true }); } catch { /* detached */ }
      if (!o.silent) audio('ui', 'move');
      placeBlade(r, prev == null || o.silent);
      // By hand: scrollIntoView also scrolls #mpl (overflow hidden is still scrollable), which shifts the whole screen.
      list.scrollTo(r.d.cell ? { left: r.el.offsetLeft - (list.clientWidth - r.el.offsetWidth) / 2, behavior: RM || o.silent ? 'auto' : 'smooth' } : { top: Math.max(0, r.el.offsetTop + r.el.offsetHeight - list.clientHeight) });
      if (!RM && !o.silent) r.lab.animate([{ transform: 'translateX(-6px)' }, { transform: 'none' }], { duration: 180, easing: EASE.out });
      const entering = ui.entering;
      ui.entering = false;
      if (entering && !RM) blade.animate([{ opacity: 0, scale: '.97 1' }, { opacity: 1, scale: '1 1' }], { duration: 240, delay: 70, easing: EASE.out });
      paintHero(r.d, prev != null && !o.silent, entering);
      dwell(r.d);
      if (r.d.kind === 'option' || r.d.kind === 'toggle') ui.lastOpt = r.d.id;
    }
    function moveFocus(dir, source) {
      if (!ui.rows.length) return;
      const i = ui.rows.findIndex(r => r.d.id === ui.active.id);
      setActive(ui.rows[(i + dir + ui.rows.length) % ui.rows.length].d.id, source);
    }

    // ---- the card: what the focused row is about ---------------------------------------------------------------------------------------
    function swapImg(frame, src, whip) {                            // keeps the old image until the new one is decoded (no flash)
      if (frame.dataset.want === src) return;
      frame.dataset.want = src || '';
      if (!src) { frame.replaceChildren(); return; }
      const img = new Image(); img.alt = ''; img.draggable = false; img.src = src;
      (img.decode ? img.decode() : new Promise((ok, no) => { img.onload = ok; img.onerror = no; })).then(() => {
        if (frame.dataset.want !== src) return;
        frame.querySelectorAll('img').forEach(old => {
          if (whip && !RM) old.animate([{ transform: 'none', opacity: 1 }, { transform: 'translateX(-5%)', opacity: 0 }], { duration: 170, easing: EASE.out, fill: 'forwards' }).onfinish = () => old.remove();
          else old.remove();
        });
        frame.append(img);
        if (!RM) img.animate(whip ? [{ transform: 'translateX(6%) scale(1.03)', opacity: 0 }, { transform: 'none', opacity: 1 }] : [{ opacity: 0 }, { opacity: 1 }], { duration: whip ? 220 : 240, easing: EASE.out });
      }).catch(() => { if (frame.dataset.want === src && !ui.noFiles) { ui.noFiles = true; showNoFiles(); } });
    }
    function showNoFiles() { noFilesEl.textContent = 'No game files: silent mode'; }
    function ticksOf(el) {
      const cur = ui.progress && ui.progress.current; let part = null;
      el.replaceChildren(...STORY.flatMap(l => {
        const s = h('s', { class: l.id === cur ? 'c' : isReached(l.id) ? 'r' : '' }), gap = part && l.part !== part; part = l.part;
        return gap ? [h('u'), s] : [s];
      }));
    }
    function cardFor(d) {
      const nw = ui.newest, cheats = OPTIONS.filter(o => o.group === 'cheats');
      if (d.cell) return { pic: ui.covers[d.level], kick: whereOf(d.level), title: d.label,
        lines: [ui.progress && ui.progress.current === d.level ? 'Your last game' : isReached(d.level) ? 'Played' : 'Not played yet'], body: ui.narrText[d.level] || '', miles: true };
      if (d.save) return { pic: d.save.thumb ? d.save.thumb() : ui.covers[d.save.level], photo: true, kick: 'Saved game', title: d.save.level ? titleOf(d.save.level) : d.label, lines: [whereOf(d.save.level), d.save.date] };
      if (d.kind === 'option') return { kick: d.label, title: optVal(d.opt).label, body: d.disabled ? 'Set by the URL' : d.opt.hint };
      if (d.kind === 'toggle') return { kick: optVal(d.opt).v === '1' ? 'On' : 'Off', title: d.label, body: d.opt.hint, lines: ['In game: º god mode, F skip ahead'] };
      if (d.to === 'controls' || d.to === 'keys' || /^keys\d+$/.test(d.to || '')) return { kick: 'Controls', title: d.label, body: 'Sensitivity, invert and four bindings per action, saved when the game starts. Esc and the function keys keep their use; F and N are kept for skipping ahead.' };
      switch (d.id) {
        case 'continuar': return { pic: nw?.thumb ? nw.thumb() : ui.covers[nw?.level], photo: true, kick: 'Where you left off', title: nw?.level ? titleOf(nw.level) : 'Last game', lines: [[whereOf(nw?.level), ago(nw?.when)].filter(Boolean).join(' · ')], ticks: true };
        case 'nueva': return { pic: ui.covers.part0_level1, kick: 'From the start', title: 'New York, three years ago', lines: ['The game asks for the difficulty'] };
        case 'cargar': return { pic: nw?.thumb ? nw.thumb() : null, photo: true, kick: `${ui.saves.length} saves`, title: 'Your saved games', lines: ui.saves.slice(0, 3).map(s => s.label) };
        case 'capitulos': { const next = STORY[Math.min(STORY.length - 1, ui.progress?.furthest ?? 0)]; return { pic: ui.covers[next.id], kick: 'The whole story', title: `${STORY.length} chapters`, lines: ['Jump to any chapter or checkpoint'], ticks: true }; }
        case 'opciones': return { kick: 'Before you play', title: 'Options', lines: OPTIONS.filter(o => o.group === 'options').map(o => `${o.label}: ${optVal(o).label}`) };
        case 'trucos': { const on = cheats.filter(o => optVal(o).v === '1'); return { kick: on.length ? `${on.length} on` : 'None on', title: 'Cheats', lines: on.length ? on.map(o => o.label) : ['God mode, weapons, ammo, bullet time'] }; }
      }
      return {};
    }
    let cardSeq = 0;
    function paintHero(d, animate, entering = false) {
      const c = cardFor(d), seq = ++cardSeq;
      heroKick.textContent = c.kick || ''; heroTitle.textContent = c.title || '';
      heroLines.replaceChildren(...(c.lines || []).filter(Boolean).map(t => h('div', {}, t)));
      heroBody.textContent = c.body || '';
      heroMeta.replaceChildren();
      if (d.opt?.reg) {
        const o = d.opt, def = o.values.find(v => v.v === o.def)?.label || 'Game value';
        const range = o.other || o.values.length <= 3 ? o.values.map(v => v.label).join(' / ') : `${o.values[0].label} – ${o.values[20].label}`;
        for (const [label, value] of [['Range', range], ['Default', def], ['Registry', o.reg]]) heroMeta.append(h('dt', {}, label), h('dd', {}, value));
      }
      const selected = rowOf(d.id);
      if (selected && ui.screen === 'main') mpl.style.setProperty('--card-y', Math.min(innerHeight - 430, selected.el.getBoundingClientRect().top) + 'px');
      heroTicks.style.display = c.ticks ? '' : 'none'; if (c.ticks) ticksOf(heroTicks);
      heroPanel.style.display = c.pic ? '' : 'none';
      heroPanel.toggleAttribute('data-photo', !!c.photo);
      hero.toggleAttribute('data-show', !!(c.title || c.pic));
      hero.toggleAttribute('data-text', !c.pic);
      hero.toggleAttribute('data-continue', d.id === 'continuar');
      if (c.pic) Promise.resolve(c.pic).then(u => { if (seq === cardSeq) { swapImg(heroPic, u, animate); if (d.cell) swapImg(backdrop, u, false); } }).catch(() => {});
      else if (!d.cell) swapImg(backdrop, null);
      if (entering && !RM) hero.animate([{ transform: 'translateY(-18px) scale(.985)', opacity: 0 }, { transform: 'translateY(2px) scale(1.008)', opacity: 1, offset: .78 }, { transform: 'none', opacity: 1 }], { duration: 240, delay: 70, easing: EASE.out });
      else if (animate && !RM) hero.querySelector('.info').animate([{ transform: 'translateY(5px)', opacity: .35 }, { transform: 'none', opacity: 1 }], { duration: 200, easing: EASE.out });
      paintMiles(c.miles ? d.level : null);
    }
    // Milestones of a chapter: the autopilot's route (web/ffwd.js). Enter plays from the first, the rest fast-forward to where the story is.
    function paintMiles(level) {
      ui.miles = !level ? [] : [{ i: -1, name: 'From the start' }, ...(window.MPFF?.milestones(level) || []).map(m => ({ ...m, name: (L.MILE_EN || {})[m.name] || m.name }))];
      ui.mile = 0; ui.mileLevel = level;
      heroMiles.replaceChildren(...ui.miles.map((m, k) => h('button', { class: 'mile', 'data-mile': k, tabindex: '-1' }, m.name)));
      markMile();
    }
    function markMile() { [...heroMiles.children].forEach((e, k) => e.toggleAttribute('data-on', k === ui.mile)); }
    function moveMile(dir) {
      if (ui.miles.length < 2) return;
      const n = ui.mile + dir; if (n < 0 || n >= ui.miles.length) { audio('ui', 'deny'); return; }
      ui.mile = n; markMile(); audio('ui', 'option', { index: n });
    }
    let dwellT = 0, voiceRaf = 0; const narrated = new Set();
    function dwell(d) {
      clearTimeout(dwellT); stopVoiceUI();
      if (!d.cell || ui.mode === 'exit') return;
      const path = ui.narr[d.level]; if (!path || narrated.has(d.level)) return;
      dwellT = setTimeout(async () => {
        if (ui.active.id !== d.id) return; narrated.add(d.level);
        const v = await audio('voice', path); if (!v) return;
        const tick = () => { heroVoice.style.transform = `scaleX(${v.pos() / v.dur})`; if (v.pos() < v.dur && ui.active.id === d.id) voiceRaf = requestAnimationFrame(tick); };
        tick();
      }, 1200);
    }
    function stopVoiceUI() { cancelAnimationFrame(voiceRaf); heroVoice.style.transform = 'scaleX(0)'; audio('stopVoice', 200); }

    // ---- layout ---------------------------------------------------------------------------------------------------------------------------
    function layout() {
      const vw = innerWidth, vh = innerHeight;
      setAttr('data-narrow', vw / vh < 1.2);
      mpl.style.setProperty('--ct', vh * .24 + 'px');
      const a = rowOf(ui.active.id);
      if (a) {
        placeBlade(a, true);
        if (a.d.cell) list.scrollLeft = Math.max(0, a.el.offsetLeft - (list.clientWidth - a.el.offsetWidth) / 2);
        else if (mpl.classList.contains('mpl-wide')) list.scrollTop = Math.max(0, a.el.offsetTop + a.el.offsetHeight - list.clientHeight);
      }
    }

    // ---- row actions ---------------------------------------------------------------------------------------------------------------------
    const busy = () => ui.mode === 'transition' || ui.mode === 'exit';
    const interactive = () => ui.mode === 'idle' || ui.mode === 'reveal';
    function press(r) {
      const el = r.el; el.setAttribute('data-pressed', '');
      setTimeout(() => el.removeAttribute('data-pressed'), 140);
      if (!RM && !r.d.cell) blade.animate([{ scale: '1 1', opacity: 1 }, { scale: '.97 1', opacity: .84, offset: .38 }, { scale: '1 1', opacity: 1 }], { duration: 210, easing: EASE.out });
    }
    function confirm(source) {
      if (!interactive()) { if (ui.mode === 'transition') ui.queue = () => confirm(source); return; }
      const r = rowOf(ui.active.id); if (!r || ui.pressing) return;
      const d = r.d;
      if (d.disabled || (d.kind === 'play' && ui.failed)) { deny(r); return; }
      if (d.kind === 'option') {
        if (d.opt.binding) { ui.capture = d.id; r.chips.forEach(c => c.removeAttribute('data-on')); setHint('Press a key. Esc cancels, Backspace clears.'); announce('Waiting for a key'); }
        else changeOpt(r, 1, true);
        return;
      }
      if (d.kind === 'toggle') { toggle(r); return; }
      ui.pressing = true; setTimeout(() => { ui.pressing = false; }, 140);
      press(r); audio('ui', 'select');
      setTimeout(() => {
        if (d.kind === 'play') play(d.cell ? Object.assign({}, d.choice, { step: ui.miles[ui.mile]?.i ?? -1 }) : d.choice);
        else if (d.kind === 'nav') navTo(d);
      }, 140);
    }
    function deny(r) {
      audio('ui', 'deny');
      if (!RM) r.el.animate([{ transform: 'translateX(0)' }, { transform: 'translateX(-3px)' }, { transform: 'translateX(3px)' }, { transform: 'translateX(0)' }], { duration: 40 });
    }
    const firstOf = s => s === 'chapters' ? 'level:' + (ui.progress?.current || STORY[0].id) : null;
    function navTo(d) {
      ui.returnIds = ui.returnIds || [];
      ui.returnIds.push({ screen: ui.screen, id: ui.active.id });
      go(d.to, d.to === 'chapters' ? 'page' : 'soft', { firstId: firstOf(d.to) });
    }
    function back() {
      if (!interactive()) { if (ui.mode === 'transition') ui.queue = () => back(); return; }
      if (ui.screen === 'main') { const r = rowOf(ui.active.id); if (r && !RM) r.lab.animate([{ transform: 'translateX(0)' }, { transform: 'translateX(-4px)' }, { transform: 'translateX(0)' }], { duration: 120 }); return; }
      audio('ui', 'back');
      const ret = (ui.returnIds || []).pop();
      go(ret?.screen || 'main', ui.screen === 'chapters' ? 'unbreak' : 'soft', { firstId: ret?.id, back: true });
    }
    // Chapters is another place, so it keeps the comic's panel break (640 ms). Options, Cheats and Load are panels of this one: the picture only
    // moves its camera and the rows swap in 240 ms (Sifu, Control).
    async function go(next, kind, o) {
      mode('transition');
      const T = performance.now(), soft = kind === 'soft';
      const total = RM ? 180 : soft ? 240 : kind === 'unbreak' ? 520 : 640;
      if (!soft) audio('ui', 'break');
      setTimeout(() => audio('mood', moodFor(next)), RM ? 0 : 200);
      ui.rows.forEach(r => { r.el.style.pointerEvents = 'none'; });
      const n = Math.min(ui.rows.length, 8), out = soft ? 90 : 140;
      [...ui.rows.slice(0, 8)].forEach((r, i) => r.el.animate([{ opacity: 1 }, { transform: 'translateX(-14px)', opacity: 0 }], { duration: RM ? 60 : out, delay: RM ? 0 : (soft ? 10 : 25) * (n - 1 - i), fill: 'forwards', easing: EASE.std }));
      blade.animate([{ opacity: 1 }, { opacity: 0 }], { duration: 80, fill: 'forwards' }).onfinish = e => e.target.cancel();
      hero.removeAttribute('data-show'); stopVoiceUI();
      await sleep(RM ? 70 : soft ? 120 : 210);
      swapImg(backdrop, null);
      build(next);
      const rows = ui.rows.slice(0, 12);
      if (!RM) rows.forEach((r, i) => r.el.animate([{ transform: next === 'chapters' ? 'translateY(16px)' : 'translateX(-18px)', opacity: 0 }, { transform: 'none', opacity: 1 }], { duration: soft ? 180 : 280, delay: 0, easing: EASE.std, fill: 'backwards' }));
      await new Promise(requestAnimationFrame);
      const first = (o.firstId && rowOf(o.firstId)) ? o.firstId : ui.rows[0]?.d.id;
      setActive(first, ui.device === 'pad' ? 'pad' : ui.device === 'key' ? 'key' : 'pointer', { silent: true, force: true });
      if (soft && !RM) {
        col.animate([{ opacity: 0, transform: 'translateX(-9px)' }, { opacity: 1, transform: 'none' }], { duration: 220, easing: EASE.out });
        hero.animate([{ opacity: 0, transform: 'translateY(7px)' }, { opacity: 1, transform: 'none' }], { duration: 240, delay: 25, easing: EASE.out });
      }
      announce(HEADINGS[next] || 'Main menu');
      layout();
      await sleep(Math.max(0, total - (performance.now() - T)));
      if (ui.mode === 'transition') mode('idle');
      const f = ui.queue; ui.queue = null; if (f) f();
    }

    // ---- options and cheats -----------------------------------------------------------------------------------------------------------------
    function setOpt(r, n) {
      const o = r.d.opt, vals = o.values;
      ui.opts[o.id] = vals[n].v;
      if (o.id === 'preset') Object.assign(ui.opts, L.PRESETS[vals[n].v]);
      else if (o.video) ui.opts.preset = 'personalizado';
      persist(); audio('ui', 'option', { index: n });
      ui.rows.forEach(paintValues); paintHero(r.d, false);
      const c = r.chips[n]; if (!RM && c) c.animate([{ transform: 'scale(.92)' }, { transform: 'none' }], { duration: 160, easing: EASE.out });
      announce(`${r.d.label}: ${vals[n].label}`);
    }
    function changeOpt(r, dir, wrap) {
      const vals = r.d.opt.values, i = vals.findIndex(v => v.v === optVal(r.d.opt).v);
      if (r.d.disabled) { deny(r); return; }
      let n = i + dir;
      if (wrap) n = (n + vals.length) % vals.length;
      else if (n < 0 || n >= vals.length) { deny(r); return; }
      setOpt(r, n);
    }
    function toggle(r, force) {
      const o = r.d.opt, on = force != null ? force : optVal(o).v !== '1';
      ui.opts[o.id] = on ? '1' : '0'; persist(); paintValues(r); paintHero(r.d, false);
      audio('ui', on ? 'on' : 'off');
      announce(`${r.d.label}: ${on ? 'on' : 'off'}`);
    }
    function adjust(dir, source) {            // Left/Right
      const r = rowOf(ui.active.id); if (!r || !interactive()) return;
      if (r.d.kind === 'option') changeOpt(r, dir, false);
      else if (r.d.kind === 'toggle') { const on = optVal(r.d.opt).v === '1'; if ((dir > 0) !== on) toggle(r); }
      else if (r.d.cell) moveFocus(dir, source);
    }
    // Up/Down: the rows of a list; in the chapter strip, the milestones of the focused chapter.
    function vertical(dir, source) { const r = rowOf(ui.active.id); if (r?.d.cell) moveMile(dir); else moveFocus(dir, source); }

    // ---- Play exit (2.9) -----------------------------------------------------------------------------------------------------------------------
    async function play(choice) {
      if (ui.mode === 'exit') return;
      if (ui.failed) { const r = rowOf(ui.active.id); r && deny(r); return; }
      mode('exit'); setAttr('data-screen', ui.screen);
      const dur = RM ? 0 : 200;
      if (!RM) {
        ui.rows.slice(0, 12).forEach(r => r.el.animate([{ opacity: r.el.hasAttribute('data-active') ? 1 : .55 }, { transform: 'translateX(-12px)', opacity: 0 }], { duration: dur, easing: EASE.in, fill: 'forwards' }));
        blade.animate([{ opacity: 1 }, { opacity: 0 }], { duration: dur, fill: 'forwards' });
        foot.animate([{ opacity: 1 }, { opacity: 0 }], { duration: dur, fill: 'forwards' });
        const pic = hero.hasAttribute('data-show') && heroPanel.style.display !== 'none' ? heroPic.querySelector('img:last-child') : null;
        if (pic?.complete && pic.naturalWidth) { try { window.MPTear?.run(mpl, pic, 240); } catch { /* no tear: the fade stays */ } }
      }
      hero.removeAttribute('data-show'); stopVoiceUI(); clearTimeout(dwellT);
      audio('playExit');
      await sleep(RM ? 0 : 250);
      await Promise.resolve(audio('stop'));
      closed = true; unlisten();
      resolveChosen(L.apply(Object.assign({ opts: ui.opts }, choice)));
      mpl.remove(); document.documentElement.classList.remove('mpl-on');
    }
    let closed = false;

    // ---- cold open (2.1) and reveal (2.2) -----------------------------------------------------------------------------------------------------
    const T0 = performance.now();
    let readyResolve; const ready = new Promise(r => { readyResolve = r; });
    function showPrompt() {
      if (ui.mode !== 'cold' || prompt.dataset.show) return; prompt.dataset.show = '1';
      setTimeout(() => { if (ui.mode === 'cold') prompt.dataset.breathe = '1'; }, 900);
    }
    function runTimeline(steps) {
      const tl = { t: 0, speed: 1, steps: steps.sort((a, b) => a[0] - b[0]), end: Math.max(...steps.map(s => s[0])) };
      let prev = performance.now();
      tl.timer = setInterval(() => {
        const n = performance.now(); tl.t += (n - prev) * tl.speed; prev = n;
        while (tl.steps.length && tl.steps[0][0] <= tl.t) tl.steps.shift()[1]();
        if (!tl.steps.length) clearInterval(tl.timer);
      }, 16);
      return tl;
    }
    let timeline = null;
    function showUI(animate) {
      build('main'); mpl.dataset.ui = 'on'; ui.entering = !!(animate && !RM);
      if (animate && !RM) ui.rows.forEach((r, i) => r.el.animate([{ transform: 'translateX(-22px)', opacity: 0 }, { transform: 'none', opacity: 1 }], { duration: 240, delay: 0, easing: EASE.out, fill: 'backwards' }));
      layout();
    }
    async function startReveal() {
      mode('intro'); ui.device = ui.device || 'key';
      prompt.dataset.gone = '1'; delete prompt.dataset.breathe;
      await Promise.race([ready, sleep(6000)]);
      if (closed) return;
      audio('reveal');
      const rowsAt = 0;
      timeline = runTimeline([
        [rowsAt, () => showUI(true)],
        [40, () => { if (ui.mode !== 'intro') return; mode('reveal'); if (ui.rows.length) setActive(ui.rows[0]?.d.id, ui.device === 'pad' ? 'pad' : 'key', { silent: true, force: true }); }],
        [120, () => { mpl.dataset.foot = 'on'; }],
        [240, () => { if (ui.mode !== 'reveal') return; mode('idle'); }],
      ]);
    }
    function skipReveal() {
      if (!timeline) return; audio('attract', false);
      timeline.speed = Math.max(4, (timeline.end - timeline.t) / 280);
    }
    // gestures: keydown (not a bare modifier / F-key / Cmd-Ctrl combo), pointerdown, touchend (spec 2.1)
    function isGestureKey(e) {
      if (e.metaKey || e.ctrlKey || e.altKey) return false;
      return !/^(Shift|Control|Alt|Meta|CapsLock|NumLock|ScrollLock|Fn|FnLock|Dead|Unidentified|ContextMenu|OS|AudioVolume|Media)/.test(e.key || '') && !/^F\d+$/.test(e.key || '');
    }
    function gesture(e) {
      if (closed) return false;
      if (e.type === 'keydown' && !isGestureKey(e)) return false;
      if (ui.mode === 'cold') {
        if (navigator.webdriver && !prompt.dataset.show && !q.has('go')) return false;       // automation clicks the page the moment it loads
        audio('init', { quiet: q.has('quiet') });
        if (e.type === 'keydown') ui.device = 'key'; else if (e.pointerType || e.type === 'touchend') ui.device = e.pointerType === 'touch' || e.type === 'touchend' ? 'touch' : 'mouse';
        e.preventDefault && e.cancelable && e.preventDefault(); ui.eatClick = performance.now() + 500;
        startReveal(); return true;
      }
      audio('init', { quiet: q.has('quiet') });                  // state= jumps: audio wakes on the first gesture anywhere
      if (ui.mode === 'intro') { e.preventDefault && e.cancelable && e.preventDefault(); ui.eatClick = performance.now() + 400; skipReveal(); return true; }
      return false;
    }

    // ---- input ---------------------------------------------------------------------------------------------------------------------------------
    function setDevice(d) { ui.device = d; }
    function typeahead(ch) {
      const now = performance.now(); gate.ahead = now - gate.aheadAt > 800 ? ch : gate.ahead + ch; gate.aheadAt = now;
      const a = norm(gate.ahead), i = ui.rows.findIndex(r => r.d.id === ui.active.id), n = ui.rows.length;
      for (let k = 1; k <= n; k++) { const r = ui.rows[(i + (gate.ahead.length > 1 ? 0 : k)) % n]; if (norm(r.d.label).startsWith(a)) { setActive(r.d.id, 'key'); return; } if (gate.ahead.length > 1 && k === n) return; }
    }
    function onKey(e) {
      if (closed) return;
      if (ui.capture) {
        if (e.repeat) { e.preventDefault(); return; }
        e.preventDefault(); e.stopImmediatePropagation();
        const r = rowOf(ui.capture);
        if (e.key === 'Escape') { ui.capture = null; paintValues(r); paintHero(r.d, false); setHint(r.d.opt.hint); return; }
        const v = e.key === 'Backspace' ? '0:0' : '1:' + DIK[e.code];
        const n = r.d.opt.values.findIndex(x => x.v === v);
        if (n < 0) { announce('That key is reserved'); return; }
        ui.capture = null; setOpt(r, n); setHint(r.d.opt.hint); return;
      }
      if (gesture(e)) return;
      if (ui.mode === 'exit') { e.preventDefault(); return; }
      if (e.metaKey || e.ctrlKey || e.altKey) return;
      if (e.key === 'F2' || e.key === 'F12') return;
      const k = e.code || e.key, now = performance.now();
      if (e.repeat) { if (now - (gate.keyAt[k] || 0) < 380 || now - gate.lastRepeat < 90) { e.preventDefault(); return; } gate.lastRepeat = now; } else gate.keyAt[k] = now;
      gate.from = { x: gate.ptr.x, y: gate.ptr.y };
      if (e.key === 'm' || e.key === 'M') { e.preventDefault(); setMute(!audio('muted')); return; }
      const onMute = document.activeElement === muteBtn;
      if (e.key === 'Tab') { setDevice('key'); return; }
      // Native buttons keep their Enter/Space activation when reached with Tab.
      if (e.target.closest?.('.lg, .mpl-mute, .mile') && (e.key === 'Enter' || e.key === ' ')) return;
      if (onMute && /^Arrow/.test(e.key)) { muteBtn.blur(); const r = rowOf(ui.active.id); if (r) setActive(r.d.id, 'key', { force: true, silent: true }); }
      if (!interactive() && !(ui.mode === 'transition')) return;
      setDevice('key'); showCursor(false);
      switch (e.key) {
        case 'ArrowDown': case 's': case 'S': e.preventDefault(); if (interactive()) vertical(1, 'key'); break;
        case 'ArrowUp': case 'w': case 'W': e.preventDefault(); if (interactive()) vertical(-1, 'key'); break;
        case 'Home': e.preventDefault(); interactive() && ui.rows[0] && setActive(ui.rows[0]?.d.id, 'key'); break;
        case 'End': e.preventDefault(); interactive() && ui.rows.length && setActive(ui.rows[ui.rows.length - 1].d.id, 'key'); break;
        case 'Enter': case ' ': e.preventDefault(); if (!e.repeat) confirm('key'); break;
        case 'Escape': case 'Backspace': e.preventDefault(); if (!e.repeat) back(); break;
        case 'ArrowLeft': case 'a': case 'A': e.preventDefault(); adjust(-1, 'key'); break;
        case 'ArrowRight': case 'd': case 'D': e.preventDefault(); adjust(1, 'key'); break;
        default: if (e.key.length === 1 && /\S/.test(e.key) && interactive()) typeahead(e.key);
      }
    }
    function setMute(on) {
      audio('mute', !!on); muteBtn.setAttribute('aria-pressed', audio('muted') ? 'true' : 'false'); muteBtn.textContent = audio('muted') ? 'muted' : 'sound';
      muteBtn.setAttribute('aria-label', audio('muted') ? 'Sound off' : 'Sound on'); announce(audio('muted') ? 'Muted' : 'Sound on');
    }
    let ptrRaf = 0, ptrN = null;
    function onPointerMove(e) {
      gate.ptr = { x: e.clientX, y: e.clientY };
      if (!ptrRaf) ptrRaf = requestAnimationFrame(() => {
        ptrRaf = 0; const px = (gate.ptr.x / innerWidth) - 0.5, py = (gate.ptr.y / innerHeight) - 0.5;
        if (!RM) { hero.style.setProperty('--px', px.toFixed(3)); hero.style.setProperty('--py', py.toFixed(3)); }   // the card leans with the mouse
      });
      showCursor(true);
      const mile = e.target.closest && e.target.closest('[data-mile]');
      if (mile && +mile.dataset.mile !== ui.mile) { ui.mile = +mile.dataset.mile; markMile(); }
      if (gate.from && Math.hypot(e.clientX - gate.from.x, e.clientY - gate.from.y) < 3) return;
      gate.from = null;
      if (!interactive() || e.pointerType === 'touch') return;
      const el = e.target.closest && e.target.closest('.mpl-row'); if (!el || el.dataset.id === ui.active.id) { if (el) ui.active.source = 'pointer'; return; }
      setDevice('mouse'); setActive(el.dataset.id, 'pointer');
    }
    function onClick(e) {
      if (closed) return;
      if (ui.capture) { const r = rowOf(ui.capture); ui.capture = null; paintValues(r); setHint(r.d.opt.hint); }
      if (performance.now() < (ui.eatClick || 0)) { e.preventDefault(); return; }
      if (e.target === muteBtn) { setMute(!audio('muted')); return; }
      if (!interactive() || !e.target.closest) return;
      const act = e.target.closest('[data-act]')?.dataset.act;
      if (act) {
        if (act === 'back') back(); else if (act === 'enter') confirm('pointer');
        else if (act === 'adjust') adjust(1, 'pointer'); else vertical(1, 'pointer');
        return;
      }
      const mile = e.target.closest('[data-mile]');
      if (mile) { ui.mile = +mile.dataset.mile; markMile(); confirm('pointer'); return; }
      const el = e.target.closest('.mpl-row'); if (!el) return;
      const r = rowOf(el.dataset.id); if (!r) return;
      if (ui.active.id !== r.d.id) setActive(r.d.id, 'pointer', { silent: ui.device === 'touch' });
      const chip = e.target.closest('[data-v]');
      if (chip && r.chips && !r.d.disabled) { setOpt(r, r.chips.indexOf(chip)); return; }
      if (r.d.cell && el !== e.target.closest('[data-active]')) return;
      confirm('pointer');
    }
    function onWheel(e) {
      if (!interactive()) return; e.preventDefault(); const now = performance.now(); if (now - gate.wheelAt < 120) return; gate.wheelAt = now;
      gate.from = { x: gate.ptr.x, y: gate.ptr.y }; moveFocus((e.deltaY || e.deltaX) > 0 ? 1 : -1, 'key');
    }
    function onContext(e) { e.preventDefault(); if (ui.mode !== 'cold' && ui.mode !== 'intro' && ui.mode !== 'reveal') back(); }
    let cursorHideT = 0;
    function showCursor(on) { if (on) { mpl.removeAttribute('data-nocursor'); clearTimeout(cursorHideT); } else if (!cursorHideT || true) { clearTimeout(cursorHideT); cursorHideT = setTimeout(() => mpl.setAttribute('data-nocursor', ''), 2500); } }
    // Gamepad (standard mapping): polled in our own rAF only while the launcher is open (spec 3.6)
    let padRaf = 0;
    function padLoop() {
      padRaf = requestAnimationFrame(padLoop);
      let gp = null; try { gp = [...(navigator.getGamepads ? navigator.getGamepads() : [])].find(g => g && g.connected); } catch { /* none */ }
      if (!gp) return;
      const now = performance.now(), b = i => !!(gp.buttons[i] && gp.buttons[i].pressed), edge = i => b(i) && !gate.padPrev[i];
      const any = gp.buttons.some((x, i) => b(i) && !gate.padPrev[i]);
      if (ui.mode === 'cold') {                              // a pad press never unlocks audio: it only pulses the prompt (2.1)
        if (any) { prompt.dataset.pulse = '1'; prompt.querySelector('.s').textContent = 'or click'; setTimeout(() => { delete prompt.dataset.pulse; }, 200); }
      } else if (interactive()) {
        let dy = 0, dx = 0;
        if (edge(12)) dy = -1; if (edge(13)) dy = 1; if (edge(14)) dx = -1; if (edge(15)) dx = 1;
        const ay = gp.axes[1] || 0, ax = gp.axes[0] || 0;
        if (Math.abs(ay) > 0.45 || Math.abs(ax) > 0.45) {
          if (!gate.padHold) { gate.padHold = now; gate.padRepeat = now + 380; if (Math.abs(ay) > Math.abs(ax)) dy = Math.sign(ay); else dx = Math.sign(ax); }
          else if (now >= gate.padRepeat) { gate.padRepeat = now + 120; if (Math.abs(ay) > Math.abs(ax)) dy = Math.sign(ay); else dx = Math.sign(ax); }
        } else gate.padHold = 0;
        if (dy || dx || any) { setDevice('pad'); showCursor(false); gate.from = { x: gate.ptr.x, y: gate.ptr.y }; }
        if (dy) vertical(dy, 'pad'); if (dx) adjust(dx, 'pad');
        if (edge(0)) confirm('pad');
        if (edge(1)) back();
        if (edge(9)) { if (ui.screen === 'main' && ui.rows[0]) setActive(ui.rows[0]?.d.id, 'pad'); confirm('pad'); }
        if (edge(4)) adjust(-1, 'pad'); if (edge(5)) adjust(1, 'pad');
        if ((dy || edge(0)) && gp.vibrationActuator && gp.vibrationActuator.playEffect) { try { gp.vibrationActuator.playEffect('dual-rumble', { duration: edge(0) ? 60 : 20, strongMagnitude: edge(0) ? 0.35 : 0.15 }); } catch { /* optional */ } }
      }
      gate.padPrev = gp.buttons.map(x => !!x.pressed);
    }

    // ---- menu picture, cursor ------------------------------------------------------------------------------------------------------------------
    let unsub = [];
    function useStill() {
      still.onload = () => { setAttr('data-still', 'on'); };
      still.onerror = () => { setAttr('data-still', 'none'); };
      still.src = '/gamefile/data/menu/bitmaps/Menu_bg_Mainmenu.tga.png';
      readyResolve();
    }
    // The 2D scene is gone (OWNER 2026-11-09): the game's own menu picture holds the screen until web/index.html shows the live level (showLive).
    async function startScene() { useStill(); }
    function unlisten() {
      removeEventListener('resize', layout); document.removeEventListener('keydown', onKey, true); document.removeEventListener('pointerdown', onGest, true);
      document.removeEventListener('touchend', onGest, true); document.removeEventListener('visibilitychange', onVis);
      cancelAnimationFrame(padRaf); unsub.forEach(f => { try { f(); } catch { /* old */ } }); clearTimeout(cursorHideT);
      if (ui.cursorUrl) URL.revokeObjectURL(ui.cursorUrl);
    }
    const onGest = e => { if (e.type === 'pointerdown' && e.target === muteBtn) { gesture(e); return; } gesture(e); };
    let hiddenAt = 0;
    function onVis() {
      if (document.hidden) { hiddenAt = performance.now(); audio('hidden', true); }
      else audio('hidden', false);
    }
    async function makeCursor() {
      try {
        const base = ui.base || '/gamefile/', ld = p => new Promise((ok, no) => { const i = new Image(); i.onload = () => ok(i); i.onerror = no; i.src = base + p; });
        const [c, a] = await Promise.all([ld('data/menu/bitmaps/cursor.pcx.png'), ld('data/menu/bitmaps/cursor_alpha.pcx.png')]);
        const w = c.width, hh = c.height, mk = i => { const k = document.createElement('canvas'); k.width = w; k.height = hh; const x = k.getContext('2d', { willReadFrequently: true }); x.drawImage(i, 0, 0, w, hh); return x.getImageData(0, 0, w, hh); };
        const cd = mk(c), ad = mk(a);
        for (let i = 0; i < cd.data.length; i += 4) cd.data[i + 3] = ad.data[i];
        const out = document.createElement('canvas'); out.width = w; out.height = hh; out.getContext('2d').putImageData(cd, 0, 0);
        const blob = await new Promise(r => out.toBlob(r, 'image/png')); if (!blob || closed) return;
        ui.cursorUrl = URL.createObjectURL(blob); mpl.style.cursor = `url(${ui.cursorUrl}) 0 0, default`;
      } catch { /* default cursor */ }
    }

    // ---- state jumps for reviewers (2.0): ?state=cold|main:<id>|chapters|part:<id>|options|cheats|saves ---------------------------------------------------
    async function jumpTo(spec) {
      await ready; mode('idle'); prompt.dataset.gone = '1';
      const [kind, arg] = spec.split(':');
      mpl.dataset.foot = 'on';
      if (kind === 'main' || spec === 'main') { showUI(false); const r = rowOf(arg) || ui.rows[0]; setActive(r.d.id, 'key', { silent: true, force: true }); }
      else {
        const scr = HEADINGS[kind] ? kind : 'main'; build(scr); mpl.dataset.ui = 'on'; layout();
        setActive(arg && rowOf('level:' + arg) ? 'level:' + arg : firstOf(scr) || ui.rows[0]?.d.id, 'key', { silent: true, force: true });
        audio('mood', moodFor(scr));
      }
      mode('idle');
    }

    // ---- boot ------------------------------------------------------------------------------------------------------------------------------------
    const dlState = {}; let dlHide = 0;
    function progress(key, frac, text) {
      dlState[key] = frac; const vals = Object.values(dlState), p = Math.min(...vals);
      dl.style.setProperty('--p', p.toFixed(3)); void text;
      clearTimeout(dlHide);
      if (p >= 1 && vals.length >= 2) dlHide = setTimeout(() => dl.removeAttribute('data-on'), 400); else dl.setAttribute('data-on', '');
    }
    function failFn(html) {
      ui.failed = true; fail.innerHTML = html; mpl.setAttribute('data-failed', '');
      ui.rows.forEach(r => { if (r.d.kind === 'play') r.el.setAttribute('aria-disabled', 'true'); });
      list.setAttribute('data-fail', '');
    }

    addEventListener('resize', layout);
    document.addEventListener('keydown', onKey, true);
    document.addEventListener('pointerdown', onGest, true);
    document.addEventListener('touchend', onGest, { capture: true, passive: false });
    document.addEventListener('visibilitychange', onVis);
    mpl.addEventListener('pointermove', onPointerMove, { passive: true });
    mpl.addEventListener('click', onClick);
    mpl.addEventListener('wheel', onWheel, { passive: false });
    mpl.addEventListener('contextmenu', onContext);

    mpl.addEventListener('focusin', e => { const row = e.target.closest?.('.mpl-row'); if (row) setActive(row.dataset.id, 'key', { silent: true }); });
    audio('on', 'nofiles', () => { ui.noFiles = true; showNoFiles(); });
    setMute(!!audio('muted'));
    padLoop(); layout();

    (async () => {
      const controlsReady = MPControls.load().then(opts => Object.assign(ui.opts, opts)).catch(e => { failFn('Could not read the controls: ' + e.message); });
      const t = Promise.all([loadAssets().then(loadSaves), window.MPFF?.ready, controlsReady]).then(() => { if (ui.rows.length) { const id = ui.active.id; build(ui.screen); id && rowOf(id) && setActive(id, ui.active.source, { silent: true, force: true }); } layout(); });
      startScene().catch(() => useStill());
      await Promise.all([t, ready]);
      makeCursor();
      if (startState !== 'cold') { await jumpTo(startState); return; }
      const wait = Math.max(0, 500 - 0);
      setTimeout(showPrompt, wait);
    })();
    setTimeout(showPrompt, 4000);                           // not ready after 4 s: the prompt appears anyway (text only)
    window.addEventListener('pagehide', () => { audio('stop'); });
    window.MPLauncher.ui = ui; window.MPLauncher.T0 = T0;
    // web/index.html: the engine has paused in a real level behind the launcher (its pause camera); the menu now sits on the live game.
    function showLive() { if (closed) return; mpl.dataset.live = 'on'; }
    const api = { chosen, progress, fail: failFn, live: showLive };
    window.MPLauncher.api = api;
    return api;
  }
  window.MPLauncher = { open };
})();
