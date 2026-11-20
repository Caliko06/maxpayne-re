// Launcher audio (spec out/design/launcher_v2.md section 4). Plain script: window.MPAudio.
// One AudioContext, created and resumed INSIDE the first user gesture (never before). Graph: sources -> buses (music, amb, sfx, voice)
// -> master (user level, mute) -> compressor -> destination. Files over 2 MB (theme, comic music, rooftop bed) stream through <audio>
// elements (Range requests, 8-bit stored WAV); small ones are fetched and decoded once. Every file is the OWNER's own game data served
// live by tools/serve.py GET /gamefile/<path>. Without those files the UI sounds fall back to tiny synthesized ticks (spec 2.10).
// stop() fades, stops every source, closes the context and drops the buffers: the game has its own DSound worklet and two live
// contexts fight (spec 2.9).
'use strict';
(() => {
  const BASE = '/gamefile/';
  const S = 'data/database/sounds/', M = 'data/database/music/';
  const FILES = {
    theme: M + 'wavs/max_theme.wav', themeShort: M + 'wavs/max_theme_short.wav', comic: M + 'wavs/graphic_novel.wav',
    bed: M + 'noise/Skyscraper_Rooftop_Stormy.wav', cityBed: M + 'noise/City_Ambience.wav',
    move: S + 'menu/move.wav', select: S + 'menu/select.wav', back: S + 'menu/time_out_beep.wav', slider: S + 'menu/slidermove.wav',
    heartbeat: S + 'menu/slomo_heartbeat.wav',
    cut1: S + 'camera/camera_cut_01.wav', cut2: S + 'camera/camera_cut_02.wav',
    thunder: S + 'ambient/storm/Storm_Loud2.wav', gust: S + 'ambient/wind/Wind_Small_Clanking2.wav',
    car: S + 'ambient/street/Cars_Passing_medium_muffled.wav', siren: S + 'dynamic/cars/Police_Wail_Siren_Distant.wav',
    hum: S + 'ambient/electric/Electric_Hum_Whine.wav', ember: S + 'skin_items/cigarette_burn.wav',
  };
  // Per-file gain to the group's loudness target (measured, out/bec/launcher2/sound_levels.md: music -20 LUFS, ambience -26, UI -30).
  const NORM = { theme: -7.3, themeShort: -7.7, comic: -6.0, bed: -17.2, cityBed: -14.2, move: 4.0, select: -1.5, back: -18.8, slider: -14.5, cut1: -9.2, cut2: -9.6 };
  const XFADE = 3;                                      // s: the theme has no clean loop seam (corr .17), so loops crossfade
  const MUTE_KEY = 'mp.launcher.v2.mute';
  const narrationEnabled = true;                       // spec 9.4: one boolean to silence Max's voice
  const dbv = x => Math.pow(10, x / 20);
  const rnd = (a, b) => a + Math.random() * (b - a);   // audio variation only: the scene owns the seeded weather
  const clamp = (x, a, b) => Math.min(b, Math.max(a, x));

  let ctx = null, master, comp, analyser, bus = {}, busBase = {}, duckTimer = {}, musicLP, ambLP, userMuted = false, closed = false;
  const buffers = new Map(), pending = new Map(), els = {}, trackGain = {}, live = new Set();
  let noFiles = false, humNode = null, quiet = false, moveBag = -1, lastMove = 0, emberDone = false, mood_ = 'main', voiceH = null;
  const log = [], handlers = {};
  try { userMuted = localStorage.getItem(MUTE_KEY) === '1'; } catch { /* private mode */ }
  const note = (ev, d) => { log.push({ t: Math.round(performance.now()), ev, d }); if (log.length > 400) log.shift(); (handlers[ev] || []).forEach(f => { try { f(d); } catch { /* listener */ } }); };

  function setGain(g, v, tau = 0.05) { for (const k in bus) if (bus[k] === g) busBase[k] = v; try { g.gain.cancelScheduledValues(ctx.currentTime); g.gain.setTargetAtTime(v, ctx.currentTime, tau); } catch { /* closed */ } }
  function curve(g, from, to, ms) {                     // equal-power fade (setValueCurveAtTime, spec 4.2)
    for (const k in bus) if (bus[k] === g) busBase[k] = to;
    const n = 32, c = new Float32Array(n), t = ctx.currentTime;
    for (let i = 0; i < n; i++) { const x = i / (n - 1); c[i] = from + (to - from) * (to > from ? Math.sin(x * Math.PI / 2) : 1 - Math.sin((1 - x) * Math.PI / 2)); }
    try { g.gain.cancelScheduledValues(t); g.gain.setValueAtTime(from, t); g.gain.setValueCurveAtTime(c, t, Math.max(0.02, ms / 1000)); } catch { /* closed */ }
  }

  // ---- loading ----------------------------------------------------------------------------------------------------------------
  async function load(name) {
    if (!ctx || noFiles) return null;
    if (buffers.has(name)) return buffers.get(name);
    if (pending.has(name)) return pending.get(name);
    const p = (async () => {
      try {
        const r = await fetch(name.includes('/') ? BASE + name : BASE + FILES[name]);
        if (!r.ok) throw new Error(r.status);
        const buf = await ctx.decodeAudioData(await r.arrayBuffer());
        let pk = 0; for (let c = 0; c < buf.numberOfChannels; c++) { const d = buf.getChannelData(c); for (let i = 0; i < d.length; i += 7) pk = Math.max(pk, Math.abs(d[i])); }
        buf.peak = pk || 1;
        if (name === 'thunder') buf.loud = loudest(buf, 2);
        buffers.set(name, buf); return buf;
      } catch (e) {
        if (!closed && !noFiles && (name === 'move' || name === 'select') && /^(404|403)$/.test(String(e.message))) { noFiles = true; note('nofiles', name); }
        return null;
      } finally { pending.delete(name); }
    })();
    pending.set(name, p); return p;
  }
  function loudest(buf, secs) {                          // start (s) of the loudest `secs` window by RMS, hop 0.25 s
    const d = buf.getChannelData(0), sr = buf.sampleRate, w = Math.min(d.length, Math.floor(secs * sr)), hop = Math.floor(sr / 4);
    let best = 0, at = 0;
    for (let s = 0; s + w <= d.length; s += hop) { let e = 0; for (let i = s; i < s + w; i += 16) e += d[i] * d[i]; if (e > best) { best = e; at = s; } }
    return at / sr;
  }

  // ---- one-shots (synth fallbacks keep the screen from being mute-by-bug) ------------------------------------------------------
  function synth(kind, opts = {}) {
    if (!ctx) return; note('synth', kind);
    const t = ctx.currentTime + (opts.delay || 0), g = ctx.createGain();
    g.connect(bus.sfx);
    if (kind === 'break') {                              // noise burst 180 ms, bandpass sweep 3 kHz -> 600 Hz, -20 dBFS
      const len = Math.floor(ctx.sampleRate * 0.18), nb = ctx.createBuffer(1, len, ctx.sampleRate), d = nb.getChannelData(0);
      for (let i = 0; i < len; i++) d[i] = (Math.random() * 2 - 1) * (1 - i / len);
      const s = ctx.createBufferSource(), f = ctx.createBiquadFilter(); s.buffer = nb; f.type = 'bandpass'; f.Q.value = 1.2;
      f.frequency.setValueAtTime(3000, t); f.frequency.exponentialRampToValueAtTime(600, t + 0.18);
      g.gain.value = dbv(-20); s.connect(f); f.connect(g);
      s.onended = () => { try { s.disconnect(); f.disconnect(); g.disconnect(); } catch { /* gone */ } };
      s.start(t);
    } else {
      const o = ctx.createOscillator(); o.connect(g);
      o.onended = () => { try { o.disconnect(); g.disconnect(); } catch { /* gone */ } };
      if (kind === 'thud') { o.frequency.value = 60; g.gain.setValueAtTime(dbv(-30), t); g.gain.exponentialRampToValueAtTime(0.0001, t + 0.08); o.start(t); o.stop(t + 0.09); }
      else { o.frequency.value = 1200; g.gain.setValueAtTime(dbv(-34), t); g.gain.exponentialRampToValueAtTime(0.0001, t + 0.02); o.start(t); o.stop(t + 0.03); }
    }
  }
  // sfx(name, {db, rate, pan, delay, peak, offset, dur, to}): db is relative to the file; with `peak` it is dBFS of the file's peak.
  async function sfx(name, o = {}) {
    if (!ctx || quiet) return null;
    const buf = await load(name);
    if (!buf) { if (ctx && !quiet) synth(name === 'move' || name === 'slider' || name === 'select' || name === 'back' ? 'tick' : name === 'heartbeat' ? 'thud' : name === 'cut1' || name === 'cut2' ? 'break' : null, o); return null; }
    if (closed) return null;
    const t = ctx.currentTime + (o.delay || 0), src = ctx.createBufferSource(), g = ctx.createGain();
    src.buffer = buf; src.playbackRate.value = o.rate || 1;
    g.gain.value = dbv((o.db ?? 0) + (o.peak ? 0 : (NORM[name] || 0))) * (o.peak ? 1 / buf.peak : 1);
    let tail = src, pan = null;
    if (o.pan != null && ctx.createStereoPanner) { pan = ctx.createStereoPanner(); pan.pan.value = clamp(o.pan, -1, 1); }
    src.connect(g); g.connect(pan || bus[o.bus || 'sfx']); if (pan) pan.connect(bus[o.bus || 'sfx']);
    const dur = o.dur || (buf.duration - (o.offset || 0));
    if (o.dur) { g.gain.setValueAtTime(g.gain.value, t + Math.max(0, dur - 0.25)); g.gain.linearRampToValueAtTime(0, t + dur); }
    src.start(t, o.offset || 0, o.dur ? dur : undefined);
    live.add(src); src.onended = () => { live.delete(src); try { g.disconnect(); pan && pan.disconnect(); } catch { /* gone */ } };
    return src;
  }

  // ---- streams ----------------------------------------------------------------------------------------------------------------
  // A looped stream = two <audio> elements: when the playing one is XFADE s from its end the other starts at 0 and they crossfade
  // (equal power). Both elements are created and play()ed inside the gesture (Safari). Only the active one gets the real file; the
  // others are unlocked with a 2-sample silent WAV and get their real src when first needed, so every file is requested once.
  const SILENT = 'data:audio/wav;base64,UklGRiYAAABXQVZFZm10IBAAAAABAAEARKwAAIhYAQACABAAZGF0YQIAAAAAAA==';
  function stream(name, node, active) {
    const ng = ctx.createGain(); ng.gain.value = dbv(NORM[name] || 0); ng.connect(node);
    const mk = real => {
      const a = new Audio(); a.crossOrigin = 'anonymous'; a.preload = real ? 'auto' : 'none';
      const x = { a, real };
      a.addEventListener('error', () => { if (x.real && !noFiles) { noFiles = true; note('nofiles', name); } });
      a.src = real ? BASE + FILES[name] : SILENT;
      x.g = ctx.createGain(); x.g.gain.value = 0; x.src = ctx.createMediaElementSource(a); x.src.connect(x.g); x.g.connect(ng);
      return x;
    };
    const ensure = x => { if (!x.real) { x.real = true; x.a.preload = 'auto'; x.a.src = BASE + FILES[name]; x.a.load(); } };
    const A = mk(!!active), B = mk(false); let cur = A, timer = 0; A.g.gain.value = 1;
    const L = { used: false, A, B, get a() { return cur.a; },
      pause() { clearInterval(timer); timer = 0; A.a.pause(); B.a.pause(); },
      resume(offset) {
        ensure(cur);
        if (offset) {
          if (cur.a.readyState >= 1) { try { cur.a.currentTime = offset; } catch { /* not seekable */ } }
          else cur.a.addEventListener('loadedmetadata', () => { try { cur.a.currentTime = offset; } catch { /* not seekable */ } }, { once: true });
        }
        play(cur.a); if (!timer) timer = setInterval(tick, 250);
      },
      dispose() { clearInterval(timer); for (const x of [A, B]) { try { x.a.pause(); x.src.disconnect(); x.g.disconnect(); x.a.removeAttribute('src'); x.a.load(); } catch { /* gone */ } } try { ng.disconnect(); } catch { /* gone */ } } };
    function tick() {
      const d = cur.a.duration; if (!d || !isFinite(d)) return;
      const other = cur === A ? B : A;
      if (cur.a.currentTime > d - 25) ensure(other);
      if (cur.a.currentTime >= d - XFADE) {
        ensure(other); try { other.a.currentTime = 0; } catch { /* WebKit throws before metadata: it starts at 0 anyway */ } play(other.a); curve(other.g, 0, 1, XFADE * 1000); curve(cur.g, 1, 0, XFADE * 1000);
        const old = cur; cur = other; setTimeout(() => { if (cur !== old) old.a.pause(); }, XFADE * 1000 + 150);
      }
    }
    // unlock inside the gesture: play both; keep playing only the active real one
    for (const x of [A, B]) { play(x.a); x.a.addEventListener('playing', () => { if (!(x === A && active)) x.a.pause(); }, { once: true }); }
    if (active) timer = setInterval(tick, 250);
    els[name] = L; return L;
  }
  const play = a => { const p = a.play(); if (p && p.catch) p.catch(() => note('blocked', a.src)); return p; };

  // ---- API ----------------------------------------------------------------------------------------------------------------------
  function init(opts = {}) {                             // call inside the gesture handler
    if (opts.quiet) quiet = true;
    if (quiet) return null;
    if (ctx) { if (ctx.state !== 'running') ctx.resume().catch(() => {}); return ctx; }
    const AC = window.AudioContext || window.webkitAudioContext; if (!AC) return null;
    try { ctx = new AC({ latencyHint: 'interactive' }); } catch { return null; }
    closed = false; ctx.resume().catch(() => {});
    master = ctx.createGain(); master.gain.value = userMuted ? 0 : 0.8;
    comp = ctx.createDynamicsCompressor(); comp.threshold.value = -18; comp.knee.value = 12; comp.ratio.value = 3; comp.attack.value = 0.01; comp.release.value = 0.25;
    analyser = ctx.createAnalyser(); analyser.fftSize = 2048;
    master.connect(comp); comp.connect(ctx.destination); comp.connect(analyser);
    for (const k of ['music', 'amb', 'sfx', 'voice']) { bus[k] = ctx.createGain(); busBase[k] = bus[k].gain.value = k === 'music' || k === 'amb' ? 0 : 1; }
    musicLP = ctx.createBiquadFilter(); musicLP.type = 'lowpass'; musicLP.frequency.value = Math.min(20000, ctx.sampleRate * 0.45);
    ambLP = ctx.createBiquadFilter(); ambLP.type = 'lowpass'; ambLP.frequency.value = 5000;
    bus.music.connect(musicLP); musicLP.connect(master); bus.amb.connect(ambLP); ambLP.connect(master);
    bus.sfx.connect(master); bus.voice.connect(master);
    for (const t of ['theme', 'comic']) { trackGain[t] = ctx.createGain(); trackGain[t].gain.value = 0; trackGain[t].connect(bus.music); }
    trackGain.bed = ctx.createGain(); trackGain.bed.gain.value = 1; trackGain.bed.connect(bus.amb);
    // elements are created and play() is called here, in the gesture (Safari); gains stay 0 until the choreography asks.
    stream('theme', trackGain.theme, true); stream('bed', trackGain.bed, true);
    stream('comic', trackGain.comic, false);                      // unlock only: stays paused until the first crossfade
    setTimeout(() => { if (ctx && ctx.state !== 'running') ctx.resume().catch(() => {}); }, 300);
    for (const n of ['move', 'select', 'back', 'slider']) load(n);   // short UI sounds decoded now (they are on the critical path of the first click)
    note('init', ctx.state);
    return ctx;
  }
  // Cold-open reveal (2.2): bed fades in, thunder at +900, theme at +1100.
  function reveal(opts = {}) {
    if (!ctx) return; const k = opts.fast ? 0.25 : 1;
    setGain(bus.amb, dbv(-6), 0.5 * k);
    load('thunder').then(buf => { if (buf) sfx('thunder', { db: -16, peak: true, pan: 0.2, offset: buf.loud, dur: 2.6, delay: Math.max(0, 0.9 * k - 0.02), bus: 'sfx' }); note('thunder', 'reveal'); });
    setTimeout(() => { if (!ctx) return; trackGain.theme.gain.value = 1; curve(bus.music, bus.music.gain.value, 1, 2400 * k); }, 1100 * k);
    ['gust', 'car', 'siren', 'hum', 'ember', 'cut1', 'cut2', 'heartbeat'].forEach((n, i) => setTimeout(() => load(n), 2500 + i * 400));   // warm the cache, one at a time
  }
  function mood(name, o = {}) {                         // 'main' | 'comic' | 'options' | 'cheats'
    if (!ctx) { mood_ = name; return; }
    const prev = mood_; mood_ = name; const t = ctx.currentTime;
    const comic = name === 'comic';
    if (comic !== (prev === 'comic') || o.force) {
      const inT = comic ? 'comic' : 'theme', outT = comic ? 'theme' : 'comic', ms = comic ? 1200 : 1000;
      const LL = els[inT];
      if (LL) { const first = inT === 'comic' && !LL.used; if (inT === 'comic') LL.used = true; LL.resume(first ? rnd(0, 60) : 0); }
      curve(trackGain[inT], trackGain[inT].gain.value, 1, ms); curve(trackGain[outT], trackGain[outT].gain.value, 0, ms);
      if (outT === 'comic') setTimeout(() => { if (mood_ !== 'comic') els.comic?.pause(); }, ms + 100);
    }
    const opt = name === 'options';
    try { musicLP.frequency.cancelScheduledValues(t); musicLP.frequency.setTargetAtTime(opt ? 1400 : Math.min(20000, ctx.sampleRate * 0.45), t, opt ? 0.13 : 0.2); } catch { /* closed */ }
    const mdb = opt ? -4 : 0, adb = opt ? -9 : -6;
    if (!o.keepLevels) { setGain(bus.music, dbv(mdb), 0.13); setGain(bus.amb, dbv(adb), 0.13); }
    if (name === 'cheats') hum(true); else hum(false);
    if (name === 'cheats' && !emberDone) { emberDone = true; sfx('ember', { db: -20, peak: true }); }
    if (name !== 'cheats' && prev === 'cheats') emberDone = false;       // "first time per visit"
  }
  async function hum(on) {
    if (!ctx) return;
    if (on && !humNode) {
      const buf = await load('hum'); if (!buf || !ctx || mood_ !== 'cheats' || humNode) return;
      const s = ctx.createBufferSource(), g = ctx.createGain(); s.buffer = buf; s.loop = true; g.gain.value = 0; s.connect(g); g.connect(bus.amb);
      s.start(); g.gain.setTargetAtTime(dbv(-32) / buf.peak * 0.6, ctx.currentTime, 0.12); humNode = { s, g };
    } else if (!on && humNode) { const h = humNode; humNode = null; try { h.g.gain.setTargetAtTime(0, ctx.currentTime, 0.12); setTimeout(() => { try { h.s.stop(); h.g.disconnect(); } catch { /* gone */ } }, 600); } catch { /* closed */ } }
  }
  function duck(name, db, ms) {                          // duck a bus by db for ms (attack 120 ms, release 900 ms)
    if (!ctx || !bus[name]) return;
    const g = bus[name].gain;
    if (!duckTimer[name]) busBase[name] = busBase[name] ?? g.value;
    else clearTimeout(duckTimer[name]);
    const base = busBase[name];
    g.cancelScheduledValues(ctx.currentTime); g.setTargetAtTime(base * dbv(db), ctx.currentTime, 0.04);
    duckTimer[name] = setTimeout(() => { duckTimer[name] = 0; if (ctx && bus[name]) g.setTargetAtTime(busBase[name], ctx.currentTime, 0.3); }, ms);
  }
  // UI events (4.3). `move` has the 70 ms throttle and the never-twice rate bag.
  function ui(ev, o = {}) {
    if (!ctx || quiet) return;
    switch (ev) {
      case 'move': { const now = performance.now(); if (now - lastMove < 70) return; lastMove = now;
        let i; do { i = Math.floor(Math.random() * 3); } while (i === moveBag); moveBag = i;
        return sfx('move', { db: 0, rate: [0.97, 1, 1.03][i] }); }
      case 'unreached': return sfx('move', { db: -4, rate: 0.96 });
      case 'select': return sfx('select', { db: 2, rate: Math.random() < 0.5 ? 0.98 : 1 });
      case 'back': return sfx('back', { db: -4, rate: 0.8 });
      case 'option': return sfx('slider', { db: -3, rate: 0.92 + 0.04 * (o.index || 0) });
      case 'on': return sfx('select', { db: 2, rate: 1.04 });
      case 'off': return sfx('back', { db: -8, rate: 1.1 });
      case 'deny': return sfx('back', { db: -12 });
      case 'break': return sfx('cut1', { db: -3, rate: rnd(0.96, 1.04), delay: 0.06 });
      case 'mini': return sfx('cut2', { db: -16 });
      case 'heartbeat': return sfx('heartbeat', { db: -10, peak: true });
    }
  }
  // Scene events drive the weather sounds (4.4). Audio never schedules its own weather (except the reveal thunder, 2.2).
  function attach(scene) {
    if (!scene || typeof scene.on !== 'function') return false;
    const off = [];
    const on = (n, f) => { try { const u = scene.on(n, p => { try { f(p || {}); } catch { /* scene must not die for audio */ } }); off.push(u); } catch { /* old scene */ } };
    on('flash', e => { if (e.kind === 'reveal' || !ctx) return;
      const delay = clamp((e.thunderInMs ?? 900) / 1000, 0.3, 3), far = clamp(((e.thunderInMs ?? 900) - 400) / 2000, 0, 1);
      load('thunder').then(buf => { if (!buf || !ctx) return; const len = rnd(1.5, 3);
        sfx('thunder', { db: -18 - 6 * far, peak: true, pan: clamp(((e.x ?? 0.5) - 0.5) * 1.2, -0.6, 0.6), offset: rnd(0, Math.max(0, buf.duration - len - 0.1)), dur: len, delay }); note('thunder', delay); }); });
    on('gust', e => { if (!ctx) return; const s = e.strength ?? 1;
      sfx('gust', { db: -28 + 3 * Math.min(1, s - 1), peak: true, offset: 0, delay: 0.05 });
      const g = bus.amb.gain, v = g.value; g.setTargetAtTime(v * dbv(3), ctx.currentTime, 0.4); setTimeout(() => ctx && g.setTargetAtTime(v, ctx.currentTime, 0.8), (e.ms || 2000)); });
    on('headlights', () => sfx('car', { db: -26, peak: true }));
    on('siren', () => sfx('siren', { db: -30, peak: true }));
    on('memory', () => ui('mini'));
    on('transitionstart', e => { if (e.kind === 'break' || e.kind === 'unbreak' || e.kind === 'page') ui('break'); });
    return () => off.forEach(f => { try { f(); } catch { /* old */ } });
  }
  async function voice(path) {                           // Max's narration, from the game: ducks music -7 and ambience -3
    if (!ctx || quiet || !narrationEnabled) return null;
    stopVoice(200);
    const buf = await load(path); if (!buf || !ctx) return null;
    const s = ctx.createBufferSource(), g = ctx.createGain(); s.buffer = buf; g.gain.value = dbv(-12) / buf.peak; s.connect(g); g.connect(bus.voice);
    const t0 = ctx.currentTime; s.start();
    duck('music', -7, buf.duration * 1000); duck('amb', -3, buf.duration * 1000);
    const h = { dur: buf.duration, pos: () => Math.min(buf.duration, ctx ? ctx.currentTime - t0 : 0), stop(ms = 200) { try { g.gain.setTargetAtTime(0, ctx.currentTime, ms / 3000); setTimeout(() => { try { s.stop(); g.disconnect(); } catch { /* gone */ } }, ms + 50); } catch { /* closed */ } if (voiceH === h) voiceH = null; } };
    s.onended = () => { if (voiceH === h) voiceH = null; };
    voiceH = h; return h;
  }
  function stopVoice(ms) { voiceH && voiceH.stop(ms); }
  function mute(on) { userMuted = !!on; try { localStorage.setItem(MUTE_KEY, userMuted ? '1' : '0'); } catch { /* private mode */ } if (ctx) setGain(master, userMuted ? 0 : 0.8, 0.01); note('mute', userMuted); return userMuted; }
  function ambience() { if (ctx) setGain(bus.amb, dbv(-6), 0.5); }
  function attract(on) { if (ctx) setGain(bus.music, dbv(on ? -4 : 0), on ? 1 : 0.2); }
  function hidden(on) { if (ctx) setGain(master, on || userMuted ? 0 : 0.8, on ? 0.15 : 0.27); }
  function levels() {
    if (!analyser) return { db: -120, peak: -120 };
    const d = new Float32Array(analyser.fftSize); analyser.getFloatTimeDomainData(d); let e = 0, pk = 0;
    for (const x of d) { e += x * x; pk = Math.max(pk, Math.abs(x)); }
    return { db: Math.round(20 * Math.log10(Math.sqrt(e / d.length) + 1e-9)), peak: Math.round(20 * Math.log10(pk + 1e-9)), state: ctx.state };
  }
  // Play exit (2.9): heartbeat, music and ambience to silence by ~700 ms, then stop() at +850.
  function playExit() { if (!ctx) return; ui('heartbeat'); for (const k of ['music', 'amb']) setGain(bus[k], 0, 0.18); hum(false); stopVoice(150); }
  async function stop() {
    if (!ctx || closed) return; closed = true; const c = ctx;
    for (const k in duckTimer) { clearTimeout(duckTimer[k]); delete duckTimer[k]; }
    try { for (const k of ['music', 'amb', 'sfx', 'voice']) bus[k].gain.cancelScheduledValues(c.currentTime), bus[k].gain.value = 0; } catch { /* closed */ }
    for (const s of live) { try { s.stop(); } catch { /* gone */ } } live.clear();
    if (humNode) { try { humNode.s.stop(); } catch { /* gone */ } humNode = null; }
    for (const k in els) { els[k].dispose(); delete els[k]; }
    buffers.clear(); pending.clear(); voiceH = null; ctx = null;
    try { await c.close(); } catch { /* already closed */ }
    note('closed', c.state);
  }
  window.MPAudio = {
    init, reveal, mood, duck, music: mood, ambience, sfx, ui, voice, stopVoice, stop, playExit, mute, attract, hidden, levels, attach, synth,
    on(ev, fn) { (handlers[ev] = handlers[ev] || []).push(fn); },
    state: () => ctx ? ctx.state : (closed ? 'closed' : 'none'),
    debug: { els, tracks: () => trackGain },
    muted: () => userMuted, noFiles: () => noFiles, log, FILES, load,
  };
})();
