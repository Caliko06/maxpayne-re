// Browser audio: an AudioWorklet plays the PCM ring that host/dsound.c's mixer thread fills in the shared heap.
// Ring header (u32 words, host/dsound.c ring_hdr): [0] frames written (mixer), [1] frames taken (here),
// [2] ring address (bytes), [3] ring size in stereo float frames, [4] frames of silence played (underruns),
// [5] resyncs, [6] peak level (0..32767) since web/telemetry.js last cleared it, [7] samples the mixer saturated at full scale (written by host/dsound.c emit, never cleared), [8] frames a resync skipped (audio dropped here), [9] frames the mixer overwrote before this page took them (written by emit). The mixer keeps ~2048 frames (46 ms) ahead of the wall clock; the worklet keeps its own fill
// between LOW and HIGH frames and jumps to the target when it drifts out (audio clock vs wall clock).
'use strict';
const WORKLET = `
class RingPlayer extends AudioWorkletProcessor {
  constructor(o) {
    super();
    const { sab, hdr, ring, size, target, high } = o.processorOptions;
    this.h = new Uint32Array(sab, hdr, 12);
    this.r = new Float32Array(sab, ring, 2 * size);
    this.size = size; this.target = target; this.high = high; this.synced = false; this.quiet = 0; this.ramp = 1;
  }
  process(_, outputs) {
    const L = outputs[0][0], R = outputs[0][1] || outputs[0][0], n = L.length, h = this.h;
    const w = Atomics.load(h, 0);
    let t = Atomics.load(h, 1);
    if (!w) { L.fill(0); if (R !== L) R.fill(0); return true; }          // the game has not opened DirectSound yet
    let fill = w - t;
    if (!this.synced || fill > this.high || fill < -this.size) {          // first block, or the clocks drifted apart
      if (this.synced) { Atomics.add(h, 5, 1); if (fill > this.high) Atomics.add(h, 8, fill - this.target); }
      t = w - this.target; fill = this.target; this.synced = true;
      this.ramp = 0;                                          // 3 ms fade: the jump is a step, not a click
    }
    let peak = 0;
    for (let k = 0; k < n; k++, t++) {
      if (t < w) {
        const i = 2 * (t % this.size); L[k] = this.r[i]; R[k] = this.r[i + 1];
        if (this.ramp < 1) { this.ramp = Math.min(1, this.ramp + 1 / 128); L[k] *= this.ramp; R[k] *= this.ramp; }
        const m = Math.max(Math.abs(L[k]), Math.abs(R[k])) * 32767 | 0;
        if (m > peak) peak = m;
      }
      else { L[k] = 0; R[k] = 0; Atomics.add(h, 4, 1); t = w - 1; }       // underrun: silence, wait for the mixer
    }
    Atomics.store(h, 1, t);
    if (peak > Atomics.load(h, 6)) Atomics.store(h, 6, peak);
    return true;
  }
}
registerProcessor('mp-ring', RingPlayer);
`;

async function startAudio(M, log) {
  const say = log || (() => {});
  const hdr = M._rn_audio_ring() >>> 0;                          // byte address of the header
  const H = new Uint32Array(M.HEAPU8.buffer, hdr, 12);
  const ctx = new AudioContext({ sampleRate: 44100, latencyHint: 'interactive' });
  // blob: worked in the OWNER's Safari 26.3 session (out/safari_1104); a data: URL is the fallback if a browser refuses it.
  try { await ctx.audioWorklet.addModule(URL.createObjectURL(new Blob([WORKLET], { type: 'text/javascript' }))); }
  catch (e) { say(`audio worklet blob: failed (${e.name}: ${e.message}), trying data:`); await ctx.audioWorklet.addModule('data:text/javascript,' + encodeURIComponent(WORKLET)); }
  const node = new AudioWorkletNode(ctx, 'mp-ring', {
    numberOfInputs: 0, outputChannelCount: [2],
    processorOptions: { sab: M.HEAPU8.buffer, hdr, ring: H[2], size: H[3], target: 1536, high: 6144 },
  });
  node.connect(ctx.destination);
  say(`audio worklet up, context ${ctx.state} at ${ctx.sampleRate} Hz, ring ${H[3]} frames`);
  // Chrome starts a context suspended until a gesture; the first click or key on the page resumes it.
  // Safari and Chrome keep Web Audio silent until a click or key (the first menu is mute otherwise, 4.7 s lost in the OWNER's
  // Safari session): a small banner says so and goes away when the context runs. It never blocks the boot (headless gates).
  let note = null;
  const banner = on => {
    if (on && !note && document.body) {
      note = document.createElement('div'); note.textContent = window.MP_DEVICE === 'desktop' ? 'Click or press a key for sound' : 'Tap for sound';
      note.className = 'mp-notice'; note.setAttribute('role', 'status');
      document.body.appendChild(note);
    } else if (!on && note) { note.remove(); note = null; }
  };
  const resume = () => { if (ctx.state !== 'running') ctx.resume().then(() => { say(`audio ${ctx.state} ${ctx.sampleRate} Hz`); banner(ctx.state !== 'running'); }); };
  ctx.onstatechange = () => banner(ctx.state !== 'running');
  for (const ev of ['pointerdown', 'keydown']) addEventListener(ev, resume, true);
  resume();
  banner(ctx.state !== 'running');
  let ticks = 0, lastBad = 0;
  setInterval(() => {                                            // the first minute every 5 s, then only when it got worse
    const bad = H[4] + H[5] + H[9];
    if (ticks++ < 12 || bad !== lastBad) say(`audio ${ctx.state}: mixed ${H[0]} played ${H[1]} frames, silence ${H[4]}, resyncs ${H[5]} (skipped ${H[8]}), overwritten ${H[9]}, peak ${Atomics.load(H, 6)}/32767, clipped ${Atomics.load(H, 7)}`);
    lastBad = bad;
  }, 5000);
  return { ctx, node };
}
window.startAudio = startAudio;
