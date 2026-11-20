// Runs the packets that host/gpu.c publishes (host/gpu.h; the numbers below mirror it) with WebGPU, on the page's thread.
// A packet's arenas are read in place from the shared heap: one write per arena, then the commands in order.
// Shaders arrive as WGSL (host/sm1.c), texels as BGRA8. A Present renders into a queued texture and each animation
// frame shows the oldest one; the game thread waits in Present on box.shown, the vsync it asked for in CreateDevice.
'use strict';
const GPU_ABI = 3, GPU_SLOTS = 2, GPU_STAGES = 4, GPU_USTEP = 1536;
const GPU_TARGET = 1, GPU_CLEAR = 2, GPU_PRESENT = 3, GPU_DRAW = 4, GPU_SHADER = 5, GPU_DECL = 6, GPU_TEXTURE = 7,
  GPU_TEXDATA = 8, GPU_FREE = 9, GPU_GAMMA = 10, GPU_READ = 11;
const D_TOPO = 0, D_NVERT = 1, D_NIDX = 2, D_VOFF = 3, D_IOFF = 4, D_UOFF = 5, D_STRIDE = 6, D_VS = 7, D_PS = 8, D_DECL = 9,
  D_RT = 10, D_DS = 11, D_VP = 12, D_ZEN = 18, D_ZWRITE = 19, D_ZFUNC = 20, D_CULL = 21, D_BLEND = 22, D_SRCB = 23,
  D_DSTB = 24, D_BOP = 25, D_CWRITE = 26, D_TEX = 27, D_TEXN = 7;
const T_ID = 0, T_AU = 1, T_AV = 2, T_MAG = 3, T_MIN = 4, T_MIP = 5, T_ANISO = 6;
// GpuBox, in u32 words: abi, seq, done, presents, shown, reads, then the slots (GpuPacket: 10 words).
const B_ABI = 0, B_SEQ = 1, B_DONE = 2, B_PRESENTS = 3, B_SHOWN = 4, B_READS = 5, B_SLOT = 6, B_SLOTN = 10;

const CMP = [null, 'never', 'less', 'equal', 'less-equal', 'greater', 'not-equal', 'greater-equal', 'always'];
const BLEND = [null, 'zero', 'one', 'src', 'one-minus-src', 'src-alpha', 'one-minus-src-alpha', 'dst-alpha',
  'one-minus-dst-alpha', 'dst', 'one-minus-dst', 'src-alpha-saturated'];
const BLENDOP = [null, 'add', 'subtract', 'reverse-subtract', 'min', 'max'];
// D3DTADDRESS: WRAP, MIRROR, CLAMP; BORDER and MIRRORONCE have no WebGPU mode (the driver sets 1 and 3).
const ADDRESS = [null, 'repeat', 'mirror-repeat', 'clamp-to-edge', 'clamp-to-edge', 'clamp-to-edge'];
// D3DDECLTYPE -> vertex format (the types a WGSL vec4f can take). D3DCOLOR is BGRA in memory.
const VFMT = ['float32', 'float32x2', 'float32x3', 'float32x4', 'unorm8x4-bgra', null, null, null, 'unorm8x4',
  'snorm16x2', 'snorm16x4', 'unorm16x2', 'unorm16x4', null, null, 'float16x2', 'float16x4'];
const COLOR = 'bgra8unorm', DEPTH = 'depth24plus';      // X8R8G8B8 / D24S8: stencil tests stop in host/d3d8.c
const FRAME_MS = 1000 / 60;

// featureLevel 'compatibility' (Chrome 146) also offers OpenGL ES 3.1 phones. Browsers that do not know it ignore the option.
// ?gl=webgl2 asks for the WebGL 2 backend on a browser that has WebGPU too (the dist's fallback check): the shim replaces
// navigator.gpu as soon as it is loaded, so the native path stays untaken.
async function gpuAdapter(canvas = document.getElementById('screen')) {
  if (!navigator.gpu || new URLSearchParams(location.search).get('gl') === 'webgl2') await import('./webgpu_gl.js');
  if (navigator.gpu_js) navigator.gpu_js.canvas = canvas;
  return navigator.gpu.requestAdapter({ featureLevel: 'compatibility' });
}

// onFirst: the first frame is on screen. onPacket(stats, t, runMs): after each packet, for web/telemetry.js (one row each).
async function startGpu(M, canvas, report, onFirst, onPacket) {
  const video = window.MP_VIDEO;
  // Phones and tablets pace at 30 fps unless the launcher's Frame cap says otherwise (light version, OWNER 2026-11-20; RE-NFSU's
  // phones do the same): the game waits on box.shown, so the guest also runs half the frames and a slow phone stays steady.
  const frameMs = video ? (video.fps === 0 ? 0 : 1000 / (video.fps === 30 ? 30 : 60)) : window.MP_DEVICE !== 'desktop' ? 1000 / 30 : FRAME_MS;
  const capture = new URLSearchParams(location.search).get('capture') === '1';
  let captureDraws = 0;
  const adapter = await gpuAdapter(canvas);
  if (!adapter) throw new Error('WebGPU: sin adaptador');
  // A Core adapter stays Core (the same device as before); only a phone or GPU without it runs in compatibility mode.
  // ?compat keeps a Core adapter under the compatibility rules: Chrome then validates every call as a phone's GPU would.
  const core = adapter.features.has('core-features-and-limits') && !new URLSearchParams(location.search).has('compat');
  report(`gpu.js: adapter ${adapter.info?.vendor || '?'} ${adapter.info?.architecture || ''} ${adapter.info?.description || ''} ${core ? 'core' : 'compat'}`);
  const device = await adapter.requestDevice(core ? { requiredFeatures: ['core-features-and-limits'] } : {});
  let errors = 0;
  device.addEventListener('uncapturederror', e => { if (errors++ < 20) report('webgpu: ' + e.error.message); });
  device.lost.then(i => report(`STOP webgpu: device lost: ${i.reason} ${i.message}`));
  const ctx = canvas.getContext('webgpu');
  const screenFormat = navigator.gpu.getPreferredCanvasFormat();
  ctx.configure({ device, format: screenFormat, alphaMode: 'opaque' });
  const box = M._rn_gpu_mailbox() >>> 2;    // >>>: the heap passes 2^31
  const fail = msg => { throw new Error('gpu.js: ' + msg); };
  if (M.HEAPU32[box + B_ABI] !== GPU_ABI) fail(`host/gpu.h is ABI ${M.HEAPU32[box + B_ABI]}, this file ${GPU_ABI}`);
  const res = new Map();                    // id -> {tex, view, w, h, depth, clear, used} (targets and textures)
  const shaders = new Map(), decls = new Map();

  // ---- the screen ---------------------------------------------------------------------------------
  // A Present copies the back buffer into a queued texture, shown at the next animation frame at 60 Hz at most (a 120 Hz
  // display skips every other one). The screen taking a frame is what releases the game's Present (box.shown): on Windows
  // that wait is D3D's, PresentationInterval 1 in the CreateDevice of the census.
  const RN_PACE = globalThis.RN_PACE = { shown: 0, dropped: 0 };
  const shownQ = [], freeQ = [];
  const shown = () => { Atomics.add(M.HEAP32, box + B_SHOWN, 1); Atomics.notify(M.HEAP32, box + B_SHOWN); };
  const screenMod = device.createShaderModule({ code: `
    @group(0) @binding(0) var src: texture_2d<f32>;
    @group(0) @binding(1) var lut: texture_2d<f32>;
    @group(0) @binding(2) var smp: sampler;
    const POLISH = ${new URLSearchParams(location.search).has('classic') ? 'false' : 'true'};   // ?classic: the original image
    fn luma(c: vec3f) -> f32 { return dot(c, vec3f(0.299, 0.587, 0.114)); }
    // FXAA (Lottes, the classic low-cost form): smooths polygon edges in the finished image, with no multisampled targets.
    fn fxaa(p: vec2f) -> vec3f {
      let px = 1.0 / vec2f(textureDimensions(src));
      let uv = p * px;
      let m = textureSampleLevel(src, smp, uv, 0.0).rgb;
      let lm = luma(m);
      let nw = luma(textureSampleLevel(src, smp, uv + vec2f(-1.0, -1.0) * px, 0.0).rgb);
      let ne = luma(textureSampleLevel(src, smp, uv + vec2f( 1.0, -1.0) * px, 0.0).rgb);
      let sw = luma(textureSampleLevel(src, smp, uv + vec2f(-1.0,  1.0) * px, 0.0).rgb);
      let se = luma(textureSampleLevel(src, smp, uv + vec2f( 1.0,  1.0) * px, 0.0).rgb);
      let lo = min(lm, min(min(nw, ne), min(sw, se)));
      let hi = max(lm, max(max(nw, ne), max(sw, se)));
      if (hi - lo < max(0.04, hi * 0.15)) { return m; }
      var d = vec2f(-((nw + ne) - (sw + se)), (nw + sw) - (ne + se));
      let rd = max((nw + ne + sw + se) * 0.03125, 0.0078125);
      d = clamp(d / (min(abs(d.x), abs(d.y)) + rd), vec2f(-8.0), vec2f(8.0)) * px;
      let a = 0.5 * (textureSampleLevel(src, smp, uv + d * (1.0 / 3.0 - 0.5), 0.0).rgb + textureSampleLevel(src, smp, uv + d * (2.0 / 3.0 - 0.5), 0.0).rgb);
      let b = a * 0.5 + 0.25 * (textureSampleLevel(src, smp, uv - d * 0.5, 0.0).rgb + textureSampleLevel(src, smp, uv + d * 0.5, 0.0).rgb);
      let lb = luma(b);
      return select(b, a, lb < lo || lb > hi);
    }
    @vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
      return vec4f(vec2f(f32((i << 1u) & 2u), f32(i & 2u)) * 2.0 - 1.0, 0.0, 1.0);
    }
    @fragment fn fs(@builtin(position) p: vec4f) -> @location(0) vec4f {
      let t = select(textureSampleLevel(src, smp, p.xy / vec2f(textureDimensions(src)), 0.0).rgb, fxaa(p.xy), POLISH);
      let c = vec3i(t * 255.0 + 0.5);
      var o = vec3f(textureLoad(lut, vec2i(c.x, 0), 0).r, textureLoad(lut, vec2i(c.y, 0), 0).g, textureLoad(lut, vec2i(c.z, 0), 0).b);
      ${video ? `o = pow(o, vec3f(${Math.min(1.2, Math.max(0.8, Number.isFinite(video.gamma) ? video.gamma : 1)).toFixed(6)}));` : ''}
      // A slight contrast curve and colour lift: more depth in the noir, same art. After the game's gamma ramp.
      if (POLISH) { o = mix(o, o * o * (3.0 - 2.0 * o), 0.18); o = clamp(mix(vec3f(luma(o)), o, 1.10), vec3f(0.0), vec3f(1.0)); }
      return vec4f(o, 1.0);
    }` });
  const screenPipe = device.createRenderPipeline({ layout: 'auto', vertex: { module: screenMod, entryPoint: 'vs' },
    fragment: { module: screenMod, entryPoint: 'fs', targets: [{ format: screenFormat }] }, primitive: { topology: 'triangle-list' } });
  // SetGammaRamp: the card's output ramp. One row of 256 texels, identity until the game sets it.
  const lut = device.createTexture({ size: [256, 1], format: 'rgba8unorm', usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
  const lutData = new Uint8Array(1024);
  for (let i = 0; i < 256; i++) lutData.set([i, i, i, 255], 4 * i);
  device.queue.writeTexture({ texture: lut }, lutData, { bytesPerRow: 1024 }, [256, 1]);
  const screenNear = device.createSampler({ magFilter: 'linear', minFilter: 'linear' });
  // Only opt-in output uses display pixels; the guest resolution still owns input coordinates.
  const resizeScreen = () => {
    const r = canvas.getBoundingClientRect(), dpr = video.hidpi ? window.devicePixelRatio || 1 : 1;
    const limit = device.limits.maxTextureDimension2D;
    // The buffer keeps the game's aspect (object-fit letterboxes it, web/input.js and touch.js map the mouse from it).
    const k = canvas.gameW ? Math.min(r.width / canvas.gameW, r.height / canvas.gameH) * dpr : dpr;
    const gw = canvas.gameW || r.width, gh = canvas.gameH || r.height;
    const w = Math.max(1, Math.min(limit, Math.round(gw * k))), h = Math.max(1, Math.min(limit, Math.round(gh * k)));
    if (canvas.width !== w) canvas.width = w;
    if (canvas.height !== h) canvas.height = h;
  };
  let image = null, enlarged = null, blitPipe, easuPipe, rcasPipe, outputSampler, outputSize;
  if (video) {
    const common = `
      @group(0) @binding(0) var input: texture_2d<f32>;
      @group(0) @binding(1) var sam: sampler;
      @group(0) @binding(2) var<uniform> outputSize: vec4f;
      @vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
        return vec4f(vec2f(f32((i << 1u) & 2u), f32(i & 2u)) * 2.0 - 1.0, 0.0, 1.0);
      }`;
    const makePipe = code => {
      const module = device.createShaderModule({ code: common + code });
      return device.createRenderPipeline({ layout: 'auto', vertex: { module, entryPoint: 'vs' },
        fragment: { module, entryPoint: 'fs', targets: [{ format: screenFormat }] }, primitive: { topology: 'triangle-list' } });
    };
    outputSampler = device.createSampler({ magFilter: video.up === 'nearest' ? 'nearest' : 'linear',
      minFilter: video.up === 'nearest' ? 'nearest' : 'linear' });
    outputSize = device.createBuffer({ size: 16, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    if (video.up === 'fsr') {
      const shaders = fsrShaders(Math.min(1, Math.max(0, Number.isFinite(video.sharp) ? video.sharp : 0.5)));
      easuPipe = makePipe(shaders.easu); rcasPipe = makePipe(shaders.rcas);
    } else blitPipe = makePipe(`@fragment fn fs(@builtin(position) p: vec4f) -> @location(0) vec4f {
      return textureSampleLevel(input, sam, p.xy / outputSize.xy, 0.0);
    }`);
    resizeScreen();
  }
  const outputTex = (old, w, h) => {
    if (old && old.w === w && old.h === h) return old;
    old?.tex.destroy();
    const tex = device.createTexture({ size: [w, h], format: screenFormat,
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING });
    return { w, h, tex, view: tex.createView(), binds: new Map() };
  };
  function outputPass(e, pipe, src, view) {
    let bind = src.binds.get(pipe);
    if (!bind) {
      const entries = [{ binding: 0, resource: src.view }, { binding: 1, resource: outputSampler },
        { binding: 2, resource: { buffer: outputSize } }];
      src.binds.set(pipe, bind = device.createBindGroup({ layout: pipe.getBindGroupLayout(0), entries }));
    }
    const p = e.beginRenderPass({ colorAttachments: [{ view, loadOp: 'clear', storeOp: 'store' }] });
    p.setPipeline(pipe); p.setBindGroup(0, bind); p.draw(3); p.end();
  }
  const queueTex = (w, h) => {
    for (let k = freeQ.length - 1; k >= 0; k--)    // a new resolution retires the old ones
      if (freeQ[k].w !== w || freeQ[k].h !== h) freeQ.splice(k, 1)[0].tex.destroy();
    if (freeQ.length) return freeQ.pop();
    const tex = device.createTexture({ size: [w, h], format: COLOR, usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
    return { w, h, tex, bind: device.createBindGroup({ layout: screenPipe.getBindGroupLayout(0),
      entries: [{ binding: 0, resource: tex.createView() }, { binding: 1, resource: lut.createView() }, { binding: 2, resource: screenNear }] }) };
  };
  let lastRaf = 0, nextShow = 0, firstFrame = !!onFirst;
  // What one packet did (web/telemetry.js columns); cleared after each packet.
  const pk = { present: 0, draws: 0, passes: 0, clears: 0, kb: 0, texKb: 0, pipes: 0, shaders: 0, targets: 0, reads: 0 };
  function showQueued(t) {
    requestAnimationFrame(showQueued);
    lastRaf = t;
    if (video) resizeScreen();
    if (!shownQ.length || t < nextShow - 2) return;
    nextShow = Math.max(nextShow + frameMs, t + frameMs / 2);
    const q = shownQ.shift();
    const e = device.createCommandEncoder();
    const view = ctx.getCurrentTexture().createView();
    if (video) image = outputTex(image, q.w, q.h);
    // Pass 1: the game's FXAA, ramp and grade stay at the guest resolution.
    const p = e.beginRenderPass({ colorAttachments: [{ view: video ? image.view : view, loadOp: 'clear', storeOp: 'store' }] });
    p.setPipeline(screenPipe); p.setBindGroup(0, q.bind); p.draw(3); p.end();
    if (video) {
      device.queue.writeBuffer(outputSize, 0, new Float32Array([canvas.width, canvas.height, 0, 0]));
      let src = image;
      // Pass 2: EASU only enlarges; RCAS also handles equal-size and smaller output.
      if (easuPipe && (canvas.width > q.w || canvas.height > q.h)) {
        enlarged = outputTex(enlarged, canvas.width, canvas.height);
        outputPass(e, easuPipe, image, enlarged.view); src = enlarged;
      } else if (enlarged) { enlarged.tex.destroy(); enlarged = null; }
      // Pass 3: RCAS, or a single sampled blit for bilinear/nearest.
      outputPass(e, rcasPipe || blitPipe, src, view);
    }
    device.queue.submit([e.finish()]);
    if (capture) window.RN_SCREEN_FRAME = { t: performance.now(), ...q.frame };
    freeQ.push(q);
    RN_PACE.shown++;
    shown();
    if (firstFrame) { firstFrame = false; onFirst(); }
  }
  requestAnimationFrame(showQueued);
  // A hidden page has no animation frames: the game goes on, a frame every tick of this timer.
  setInterval(() => {
    if (shownQ.length && performance.now() - lastRaf > 250) { freeQ.push(shownQ.shift()); RN_PACE.dropped++; shown(); }
  }, 100);

  // ---- clears ---------------------------------------------------------------------------------------
  // A clear of a whole target with no draws before it in the pass folds into the next pass's load op. The rest, like the
  // depth clears between the world and the weapon, are a full-screen triangle under a scissor. Uniforms: 256 steps of a ring.
  const clearMod = device.createShaderModule({ code: `
    struct U { color: vec4f, z: f32 };
    @group(0) @binding(0) var<uniform> u: U;
    @vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
      return vec4f(vec2f(f32((i << 1u) & 2u), f32(i & 2u)) * 2.0 - 1.0, u.z, 1.0);
    }
    @fragment fn fs() -> @location(0) vec4f { return u.color; }
    @fragment fn fs_none() {}` });
  const CLEAR_RING = 256;
  const clearRing = device.createBuffer({ size: 256 * CLEAR_RING, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
  const clearBgl = device.createBindGroupLayout({ entries: [{ binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
    buffer: { type: 'uniform', hasDynamicOffset: true, minBindingSize: 32 } }] });
  const clearBind = device.createBindGroup({ layout: clearBgl, entries: [{ binding: 0, resource: { buffer: clearRing, size: 32 } }] });
  const clearLayout = device.createPipelineLayout({ bindGroupLayouts: [clearBgl] });
  const clearPipes = new Map();
  function clearPipe(hasColor, hasDepth, flags) {
    const key = (hasColor ? 1 : 0) | (hasDepth ? 2 : 0) | (flags & 3) << 2;
    let p = clearPipes.get(key);
    if (!p) pk.pipes++, clearPipes.set(key, p = device.createRenderPipeline({ layout: clearLayout,
      vertex: { module: clearMod, entryPoint: 'vs' },
      fragment: { module: clearMod, entryPoint: hasColor ? 'fs' : 'fs_none',
        targets: hasColor ? [{ format: COLOR, writeMask: flags & 1 ? GPUColorWrite.ALL : 0 }] : [] },
      primitive: { topology: 'triangle-list' },
      depthStencil: hasDepth ? { format: DEPTH, depthWriteEnabled: !!(flags & 2), depthCompare: 'always' } : undefined }));
    return p;
  }
  const argb = v => ({ r: (v >>> 16 & 255) / 255, g: (v >>> 8 & 255) / 255, b: (v & 255) / 255, a: (v >>> 24) / 255 });

  // ---- draws: one uniform block per draw (dynamic offset), textures + samplers in group 1 ----------------
  const drawBgl = device.createBindGroupLayout({ entries: [{ binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
    buffer: { type: 'uniform', hasDynamicOffset: true, minBindingSize: GPU_USTEP } }] });
  const texBgls = new Map(), layouts = new Map();
  function texBgl(mask) {
    let l = texBgls.get(mask);
    if (!l) {
      const entries = [];
      for (let k = 0; k < GPU_STAGES; k++) if (mask >> k & 1) {
        entries.push({ binding: 2 * k, visibility: GPUShaderStage.FRAGMENT, sampler: { type: 'filtering' } });
        entries.push({ binding: 2 * k + 1, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: 'float' } });
      }
      texBgls.set(mask, l = device.createBindGroupLayout({ entries }));
    }
    return l;
  }
  function drawLayout(mask) {
    let l = layouts.get(mask);
    if (!l) layouts.set(mask, l = device.createPipelineLayout({ bindGroupLayouts: [drawBgl, texBgl(mask)] }));
    return l;
  }
  // Unbound stage: opaque black (what Wine binds; the CPU backend in host/gpu.c does the same).
  const dummy = device.createTexture({ size: [1, 1], format: 'rgba8unorm', usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
  device.queue.writeTexture({ texture: dummy }, new Uint8Array([0, 0, 0, 255]), { bytesPerRow: 4 }, [1, 1]);
  const dummyView = dummy.createView();
  const samplers = new Map();
  function sampler(key, w, t) {
    let s = samplers.get(key);
    if (s) return s;
    const filt = f => f >= 2 ? 'linear' : 'nearest', mip = w[t + T_MIP], min = w[t + T_MIN], mag = w[t + T_MAG];
    const d = { addressModeU: ADDRESS[w[t + T_AU]], addressModeV: ADDRESS[w[t + T_AV]], magFilter: filt(mag), minFilter: filt(min),
      mipmapFilter: mip ? 'linear' : 'nearest' };     // sharper textures at a distance than the game asked for
    if (!d.addressModeU || !d.addressModeV) fail(`texture address ${w[t + T_AU]}/${w[t + T_AV]}`);
    if (!mip) d.lodMaxClamp = 0;            // D3DTEXF_NONE: level 0 only
    if (min >= 2 && mag >= 2 && mip) d.maxAnisotropy = Math.min(video && [1, 2, 4, 8, 16].includes(video.aniso) ? video.aniso : 16, window.MP_DEVICE !== 'desktop' ? 8 : 16);
    samplers.set(key, s = device.createSampler(d));
    return s;
  }
  const bindCache = new Map();              // stages' textures and samplers -> group 1; emptied when a texture goes
  function texBind(w, a, mask) {
    let key = mask, sk = ['', '', '', ''];
    for (let k = 0; k < GPU_STAGES; k++) if (mask >> k & 1) {
      const t = a + D_TEX + D_TEXN * k;
      sk[k] = (w[t + T_AU] << 4 | w[t + T_AV]) << 12 | w[t + T_MAG] << 8 | w[t + T_MIN] << 4 | w[t + T_MIP] | w[t + T_ANISO] << 20;
      key += ':' + w[t + T_ID] + ',' + sk[k];
    }
    let b = bindCache.get(key);
    if (!b) {
      const entries = [];
      for (let k = 0; k < GPU_STAGES; k++) if (mask >> k & 1) {
        const t = a + D_TEX + D_TEXN * k, r = res.get(w[t + T_ID]);
        entries.push({ binding: 2 * k, resource: sampler(sk[k], w, t) });
        entries.push({ binding: 2 * k + 1, resource: r ? r.view : dummyView });
      }
      bindCache.set(key, b = device.createBindGroup({ layout: texBgl(mask), entries }));
    }
    return b;
  }
  const pipes = new Map(), pipeWait = new Set(), pipeBad = new Set();    // key -> pipeline; keys still compiling; keys the device refused
  function blendState(w, a) {
    if (!w[a + D_BLEND]) return undefined;
    const src = w[a + D_SRCB], dst = w[a + D_DSTB], op = w[a + D_BOP];
    let s = BLEND[src], d = BLEND[dst];
    if (src === 12) { s = 'src-alpha'; d = 'one-minus-src-alpha'; }         // BOTHSRCALPHA
    if (src === 13) { s = 'one-minus-src-alpha'; d = 'src-alpha'; }         // BOTHINVSRCALPHA
    if (op >= 4) { s = 'one'; d = 'one'; }                                  // MIN/MAX ignore the factors
    if (!s || !d || !BLENDOP[op]) fail(`blend ${src}/${dst}/${op}`);
    const color = { srcFactor: s, dstFactor: d, operation: BLENDOP[op] };
    return { color, alpha: color };
  }
  function pipeline(w, a, hasDepth) {
    const key = w[a + D_VS] + ',' + w[a + D_PS] + ',' + w[a + D_DECL] + ',' + w[a + D_STRIDE] + ',' + w[a + D_TOPO] + ',' +
      (hasDepth ? (w[a + D_ZEN] ? 1 + w[a + D_ZWRITE] * 2 + w[a + D_ZFUNC] * 4 : 16 * 4) : -1) + ',' + w[a + D_CULL] + ',' +
      (w[a + D_BLEND] ? w[a + D_SRCB] + ',' + w[a + D_DSTB] + ',' + w[a + D_BOP] : '') + ',' + w[a + D_CWRITE];
    const p = pipes.get(key);
    if (p) return p;
    if (pipeWait.has(key) || pipeBad.has(key)) return null;      // still compiling: this draw waits for a later frame (see below)
    const vs = shaders.get(w[a + D_VS]), ps = shaders.get(w[a + D_PS]), decl = decls.get(w[a + D_DECL]);
    if (!vs || !ps || !decl) fail('draw with an unknown shader or declaration');
    const attributes = [];
    for (let k = 0; k < 16; k++) {
      const inp = vs.inputs[k];
      if (!inp) continue;
      const usage = (inp >>> 8) & 255, index = inp & 255;
      const e = decl.find(e => e.stream === 0 && e.usage === usage && e.index === index);
      if (!e) fail(`vs input v${k} (usage ${usage}/${index}) not in the declaration`);
      if (!VFMT[e.type]) fail(`vertex element type ${e.type}`);
      attributes.push({ shaderLocation: k, offset: e.offset, format: VFMT[e.type] });
    }
    const topo = { 2: 'line-list', 3: 'line-strip', 4: 'triangle-list', 5: 'triangle-strip' }[w[a + D_TOPO]] || fail(`topology ${w[a + D_TOPO]}`);
    const cull = w[a + D_CULL] === 3 ? 'back' : w[a + D_CULL] === 2 ? 'front' : 'none';
    const strip = topo.endsWith('strip') ? 'uint32' : undefined;    // only drawIndexed reads it
    pk.pipes++;
    const desc = { layout: drawLayout(ps.texMask),
      vertex: { module: vs.module, entryPoint: 'main', buffers: [{ arrayStride: w[a + D_STRIDE], attributes }] },
      fragment: { module: ps.module, entryPoint: 'main',
        targets: [{ format: COLOR, blend: blendState(w, a), writeMask: w[a + D_CWRITE] & 15 }] },
      // D3D front faces are clockwise on screen; CULL_CCW (the default) culls the back ones.
      primitive: { topology: topo, frontFace: 'cw', cullMode: cull, stripIndexFormat: strip },
      depthStencil: hasDepth ? { format: DEPTH, depthWriteEnabled: !!(w[a + D_ZEN] && w[a + D_ZWRITE]),
        depthCompare: w[a + D_ZEN] ? CMP[w[a + D_ZFUNC]] : 'always' } : undefined };
    // A first draw compiles its pipeline on the device's time, and the present that uses it waits for Metal: 12 pipelines
    // at one level's first draw cost 0.6 ms of page time and 75-97 ms of stall (rea/map/render.md, s1104). So build it
    // async and let the draw wait for a later frame instead, as Dolphin's asynchronous shader compilation and yuzu's
    // async shaders do with a shader the device does not have yet. Only scene draws (this caller): the screen pass and
    // the clears keep their sync pipelines, where a skipped draw would be a visible hole.
    // A refused pipeline stays refused: a sync retry caches an invalid object, and using it drops whole command buffers
    // (one bad pipeline: 347-349 GPU errors, out/ism 2026-11-06). Only its draws go missing.
    pipeWait.add(key);
    device.createRenderPipelineAsync(desc).then(q => pipes.set(key, q), e => {
      pipeBad.add(key);
      report(`gpu.js: pipeline refused, its draws are skipped: ${e.message}`);
    }).finally(() => pipeWait.delete(key));
    return null;
  }

  // ---- encoder and passes -----------------------------------------------------------------------------
  // writeTexture runs at queue time, before the commands of the encoder still being recorded: a texture changed after a
  // draw that used it ends the encoder first (encN stamps which one used it).
  let enc = null, encN = 1, pass = null, passRt = null, passDs = null, passDraws = 0;
  let curPipe = null, curVp = '', curTex = null, clearN = 0;
  let vbuf = null, ibuf = null, ubuf = null, ubind = null, vcap = 0, icap = 0, ucap = 0;
  const encoder = () => enc || (enc = device.createCommandEncoder());
  function endPass() { if (pass) { pass.end(); pass = null; } }
  function flushEnc() {
    endPass();
    if (enc) { device.queue.submit([enc.finish()]); enc = null; encN++; clearN = 0; }
  }
  function openPass(rt, ds) {
    endPass();
    const c = rt.clear, z = ds ? ds.clear : undefined;
    pass = encoder().beginRenderPass({
      colorAttachments: [{ view: rt.view, loadOp: c ? 'clear' : 'load', clearValue: c, storeOp: 'store' }],
      depthStencilAttachment: ds ? { view: ds.view, depthLoadOp: z !== undefined ? 'clear' : 'load', depthClearValue: z, depthStoreOp: 'store' } : undefined });
    rt.clear = undefined;
    if (ds) ds.clear = undefined;
    passRt = rt; passDs = ds; passDraws = 0;
    curPipe = null; curVp = ''; curTex = null;
    pass.setIndexBuffer(ibuf, 'uint32');
    pk.passes++;
  }
  function clear(h, f32, a) {
    const rt = res.get(h[a]), ds = h[a + 1] ? res.get(h[a + 1]) : null, flags = h[a + 2];
    const x0 = h[a + 6], y0 = h[a + 7], x1 = h[a + 8], y1 = h[a + 9];
    pk.clears++;
    if (!(flags & 3)) return;                // stencil only: no target keeps stencil here
    const full = x0 === 0 && y0 === 0 && x1 === rt.w && y1 === rt.h;
    const inPass = pass && rt === passRt && ds === passDs;
    if (full && (!inPass || passDraws === 0)) {
      endPass();
      if (flags & 1) rt.clear = argb(h[a + 3]);
      if (flags & 2) ds.clear = f32[a + 4];
      return;
    }
    if (!inPass) openPass(rt, ds);
    if (clearN === CLEAR_RING) { flushEnc(); openPass(rt, ds); }
    const off = 256 * clearN++, c = argb(h[a + 3]);
    device.queue.writeBuffer(clearRing, off, new Float32Array([c.r, c.g, c.b, c.a, f32[a + 4]]));
    pass.setPipeline(clearPipe(true, !!passDs, flags)); curPipe = null;
    pass.setBindGroup(0, clearBind, [off]);
    pass.setViewport(0, 0, passRt.w, passRt.h, 0, 1); curVp = '';
    pass.setScissorRect(x0, y0, x1 - x0, y1 - y0);
    pass.draw(3);
    pass.setScissorRect(0, 0, passRt.w, passRt.h);
    passDraws++;
  }
  function draw(h, f32, a) {
    const rt = res.get(h[a + D_RT]), ds = h[a + D_DS] ? res.get(h[a + D_DS]) : null;
    const p = pipeline(h, a, !!ds);
    if (!p) return;                                  // pipeline() skipped it: its pipeline is still compiling
    const ps = shaders.get(h[a + D_PS]), mask = ps ? ps.texMask : 0;
    for (let k = 0; k < GPU_STAGES; k++) if (mask >> k & 1) {
      const t = res.get(h[a + D_TEX + D_TEXN * k + T_ID]);
      if (t) { t.used = encN; if (t.clear) settle(t); }    // a target cleared and now read
    }
    if (!pass || rt !== passRt || ds !== passDs) openPass(rt, ds);
    if (p !== curPipe) { pass.setPipeline(p); curPipe = p; }
    const vp = `${h[a + D_VP]},${h[a + D_VP + 1]},${h[a + D_VP + 2]},${h[a + D_VP + 3]},${h[a + D_VP + 4]},${h[a + D_VP + 5]}`;
    if (vp !== curVp) { pass.setViewport(h[a + D_VP], h[a + D_VP + 1], h[a + D_VP + 2], h[a + D_VP + 3], f32[a + D_VP + 4], f32[a + D_VP + 5]); curVp = vp; }
    pass.setBindGroup(0, ubind, [h[a + D_UOFF]]);
    const b = texBind(h, a, mask);
    if (b !== curTex) { pass.setBindGroup(1, b); curTex = b; }
    pass.setVertexBuffer(0, vbuf, h[a + D_VOFF], h[a + D_NVERT] * h[a + D_STRIDE]);
    if (h[a + D_NIDX]) pass.drawIndexed(h[a + D_NIDX], 1, h[a + D_IOFF] >>> 2, 0, 0);
    else pass.draw(h[a + D_NVERT]);
    passDraws++;
    pk.draws++;
    if (capture) captureDraws++;
  }
  // Runs a pending clear of this color target now.
  function settle(t) { openPass(t, null); endPass(); }
  function grow(cap, need, usage) {
    if (need <= cap) return null;
    cap = Math.max(need, cap * 2, 1 << 20);
    return device.createBuffer({ size: cap, usage: usage | GPUBufferUsage.COPY_DST });
  }
  function upload(slot) {
    const h = M.HEAPU32, u8 = M.HEAPU8, s = box + B_SLOT + slot * B_SLOTN;
    const vtx = h[s + 2], nvtx = h[s + 3] + 3 & ~3, idx = h[s + 4], nidx = h[s + 5], uni = h[s + 6], nuni = h[s + 7];
    let b;
    if ((b = grow(vcap, nvtx, GPUBufferUsage.VERTEX))) { vbuf = b; vcap = b.size; }
    if ((b = grow(icap, nidx + 4, GPUBufferUsage.INDEX)) || !ibuf) { ibuf = b || device.createBuffer({ size: 1 << 20, usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST }); icap = ibuf.size; }
    if ((b = grow(ucap, nuni + GPU_USTEP, GPUBufferUsage.UNIFORM))) {
      ubuf = b; ucap = b.size;
      ubind = device.createBindGroup({ layout: drawBgl, entries: [{ binding: 0, resource: { buffer: ubuf, size: GPU_USTEP } }] });
    }
    pk.kb += (nvtx + nidx + nuni) >> 10;
    if (nvtx) device.queue.writeBuffer(vbuf, 0, u8, vtx, nvtx);
    if (nidx) device.queue.writeBuffer(ibuf, 0, u8, idx, nidx);
    if (nuni) device.queue.writeBuffer(ubuf, 0, u8, uni, nuni);
  }

  // One packet, in order.
  function run(slot) {
    const h = M.HEAPU32, f32 = new Float32Array(h.buffer), u8 = M.HEAPU8, s = box + B_SLOT + slot * B_SLOTN;
    const blob = h[s + 8], end = (h[s] >>> 2) + h[s + 1];
    upload(slot);
    for (let i = h[s] >>> 2; i < end;) {
      const op = h[i] & 255, a = i + 1;
      i = a + (h[i] >>> 8);
      switch (op) {
      case GPU_DRAW: draw(h, f32, a); break;
      case GPU_CLEAR: clear(h, f32, a); break;
      case GPU_TEXDATA: {
        const t = res.get(h[a]);
        if (!t) fail(`data for texture ${h[a]}`);
        if (t.used === encN) flushEnc();
        const w = h[a + 2], th = h[a + 3], p = blob + h[a + 4];
        pk.texKb += (4 * w * th) >> 10;
        device.queue.writeTexture({ texture: t.tex, mipLevel: h[a + 1] }, u8.subarray(p, p + 4 * w * th), { bytesPerRow: 4 * w, rowsPerImage: th }, [w, th]);
        break;
      }
      case GPU_TEXTURE: {
        free(h[a]);
        const tex = device.createTexture({ size: [h[a + 1], h[a + 2]], mipLevelCount: h[a + 3], format: COLOR,
          usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST | GPUTextureUsage.COPY_SRC });
        res.set(h[a], { tex, view: tex.createView(), w: h[a + 1], h: h[a + 2] });
        break;
      }
      case GPU_TARGET: {
        pk.targets++;
        free(h[a]);
        const depth = h[a + 3] !== 0;
        const tex = device.createTexture({ size: [h[a + 1], h[a + 2]], format: depth ? DEPTH : COLOR,
          usage: GPUTextureUsage.RENDER_ATTACHMENT | (depth ? 0 : GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_SRC) });
        res.set(h[a], { tex, view: tex.createView(), w: h[a + 1], h: h[a + 2], depth });
        break;
      }
      case GPU_FREE: free(h[a]); break;
      case GPU_SHADER: {
        pk.shaders++;
        const n = h[a + 2], m = h[a + 3], p = (a + 21 + n) * 4;
        shaders.set(h[a], { module: device.createShaderModule({ code: new TextDecoder().decode(u8.slice(p, p + m)) }),
          inputs: Array.from(h.subarray(a + 4, a + 20)), texMask: h[a + 20] });
        break;
      }
      case GPU_DECL: {
        const list = [];
        for (let k = 0; k < h[a + 1]; k++) {
          const w0 = h[a + 2 + 2 * k], w1 = h[a + 3 + 2 * k];
          list.push({ stream: w0 & 0xffff, offset: w0 >>> 16, type: w1 & 255, usage: w1 >>> 16 & 255, index: w1 >>> 24 });
        }
        decls.set(h[a], list);
        break;
      }
      case GPU_GAMMA: {
        const r = (blob + h[a]) >>> 1, h16 = new Uint16Array(h.buffer);
        for (let k = 0; k < 256; k++) for (let c = 0; c < 3; c++) lutData[4 * k + c] = h16[r + 256 * c + k] >>> 8;
        device.queue.writeTexture({ texture: lut }, lutData, { bytesPerRow: 1024 }, [256, 1]);
        break;
      }
      case GPU_PRESENT: {
        const rt = res.get(h[a]), w = h[a + 1], th = h[a + 2];
        if (rt.clear) settle(rt);
        endPass();
        if (!video && (canvas.width !== w || canvas.height !== th)) { canvas.width = w; canvas.height = th; }
        canvas.gameW = w; canvas.gameH = th;     // the game's frame size: resizeScreen keeps the buffer at this aspect (input.js maps from canvas.width/height)
        const q = queueTex(w, th);
        if (capture) {
          const gs = M._rn_gamestate_addr() >>> 2;
          q.frame = { mode: h[gs + 3], flags: h[gs + 4], draws: captureDraws };
          captureDraws = 0;
        }
        encoder().copyTextureToTexture({ texture: rt.tex }, { texture: q.tex }, [w, th]);
        flushEnc();
        shownQ.push(q);      // at most GPU_QUEUED long: the game waits on box.shown
        pk.present = 1;
        break;
      }
      case GPU_READ: read(h, a); break;
      default: fail(`unknown command ${op}`);
      }
    }
    flushEnc();
  }
  function free(id) {
    const t = res.get(id);
    if (!t) return;
    if (t.used === encN || t === passRt || t === passDs) flushEnc();
    t.tex.destroy();
    res.delete(id);
    bindCache.clear();
  }
  // The rows reach the guest when the GPU has drawn them; the game thread waits on box.reads.
  function read(h, a) {
    const t = res.get(h[a]), level = h[a + 1], w = h[a + 2], th = h[a + 3], dst = h[a + 4];
    if (t.clear) settle(t);
    endPass();
    const row = (4 * w + 255) & ~255;
    pk.reads++;
    const buf = device.createBuffer({ size: row * th, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
    encoder().copyTextureToBuffer({ texture: t.tex, mipLevel: level }, { buffer: buf, bytesPerRow: row }, [w, th]);
    flushEnc();
    buf.mapAsync(GPUMapMode.READ).then(() => {
      const src = new Uint8Array(buf.getMappedRange());
      for (let r = 0; r < th; r++) M.HEAPU8.set(src.subarray(r * row, r * row + 4 * w), dst + r * 4 * w);
    }).catch(e => report('STOP gpu.js: read failed: ' + e.message)).finally(() => {   // a lost device still frees the game
      buf.destroy();
      Atomics.add(M.HEAP32, box + B_READS, 1);
      Atomics.notify(M.HEAP32, box + B_READS);
    });
  }

  // ---- the page's loop --------------------------------------------------------------------------------
  // Packets run as they arrive, so a frame is ready before the animation frame that shows it; a task boundary every few
  // milliseconds keeps input, timers and animation frames going during a load, when packets come back to back.
  const ch = new MessageChannel(), yields = [];
  ch.port1.onmessage = () => yields.shift()?.();
  const yieldTask = () => new Promise(f => { yields.push(f); ch.port2.postMessage(0); });
  async function pump() {
    let task = performance.now();
    for (;;) {
      const h = M.HEAP32, seq = Atomics.load(h, box + B_SEQ), done = Atomics.load(h, box + B_DONE);
      if (done !== seq) {
        const t = performance.now();
        run(done % GPU_SLOTS);
        if (onPacket) {
          onPacket(pk, t, performance.now() - t);
          for (const k in pk) pk[k] = 0;
        }
        Atomics.store(M.HEAP32, box + B_DONE, done + 1);
        Atomics.notify(M.HEAP32, box + B_DONE);
        if (performance.now() - task > 8) { await yieldTask(); task = performance.now(); }
        continue;
      }
      if (!Atomics.waitAsync) { await new Promise(ok => setTimeout(ok, 4)); task = performance.now(); continue; }   // Firefox < 145
      const r = Atomics.waitAsync(h, box + B_SEQ, seq);
      if (r.async) { await r.value; task = performance.now(); }
    }
  }
  pump().catch(e => report('STOP ' + e.message + '\n' + e.stack));   // Firefox stacks omit the message
}

// WGSL port: https://github.com/firdawolf/AMD-FSR1-wgpu-shader
/*
MIT License

Copyright (c) 2022 firdawolf

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
//
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
function fsrShaders(sharp) {
  return { easu: `
    fn min3(a:vec3<f32>,b:vec3<f32>,c:vec3<f32>)->vec3<f32>{
      return min(a, min(b,c));
    }
    fn max3(a:vec3<f32>,b:vec3<f32>,c:vec3<f32>)->vec3<f32>{
      return max(a, max(b,c));
    }
    struct FsrTap{
      aC:vec3<f32>,
      aW:f32,
    }
    struct FsrSet{
      dir:vec2<f32>,
      len:f32,
    }
    fn saturate(num:f32)->f32{
      let a : f32 = 0.0;
      let b:f32 = 1.0;
      return clamp(num,0.0,1.0);
    }
    fn matchSet(pp:vec2<f32>,biS:bool, biT:bool, biU:bool, biV:bool) -> f32{
      if(biS){
        return (1.0 - pp.x) * (1.0 - pp.y);
      }
      else if(biT){
        return pp.x * (1.0 - pp.y);
      }
      else if(biU){
        return (1.0 - pp.x) * pp.y;
      }
      else if(biV){
        return pp.x * pp.y;
      }
      else{
        return 0.0;
      }
    }
    fn FsrEasuTap(
    fsr:FsrTap, // Accumulated color, with negative lobe. // Accumulated weight.
    off:vec2<f32>, // Pixel offset from resolve position to tap.
    dir:vec2<f32>, // Gradient direction.
    len:vec2<f32>, // Length.
    lob:f32, // Negative lobe strength.
    clp:f32, // Clipping point.
    c:vec3<f32>  // Tap color.
    )-> FsrTap {
      var fsr1 : FsrTap = fsr;
      var aC : vec3<f32> = fsr1.aC;
      var aW : f32 = fsr1.aW;
      var v : vec2<f32> = vec2<f32>(0.0);
      v.x = (off.x * (dir.x)) + (off.y * dir.y);
      v.y = (off.x * (-dir.y)) + (off.y * dir.x);
      v = v * len;
      var d2 : f32 = v.x * v.x + v.y * v.y;
      d2 = min(d2, clp);
      var wB : f32 = 2.0 / 5.0 * d2 - 1.0;
      var wA : f32 = lob * d2 - 1.0;
      wB = wB * wB;
      wA = wA * wA;
      wB = 25.0 / 16.0 * wB - (25.0 / 16.0 - 1.0);
      var w : f32 = wB * wA;
      aC = aC + c * w;
      aW = aW + w;
      fsr1.aC = aC;
      fsr1.aW = aW;
      return fsr1;
    }
    fn FsrEasuSet(
    fsr : FsrSet,
    pp:vec2<f32>,
    biS:bool, biT:bool, biU:bool, biV:bool,
    lA:f32, lB:f32, lC:f32, lD:f32, lE:f32) -> FsrSet{
      var fsr1 : FsrSet = fsr;
      var dir : vec2<f32> = fsr1.dir;
      var len : f32 = fsr1.len;
      var w : f32 = 0.0;
      w = matchSet(pp,biS,biT,biU,biV);
      var dc:f32 = lD - lC;
      var cb:f32 = lC - lB;
      var lenX:f32 = max(abs(dc), abs(cb));
      lenX = 1.0 / max(lenX, 1e-8);
      var dirX :f32 = lD - lB;
      dir.x = dir.x + (dirX * w);
      lenX = saturate(abs(dirX) * lenX);
      lenX = lenX * lenX;
      len = len + (lenX * w);
      var ec :f32 = lE - lC;
      var ca : f32 = lC - lA;
      var lenY :f32 = max(abs(ec), abs(ca));
      lenY = 1.0 / max(lenY, 1e-8);
      var dirY:f32 = lE - lA;
      dir.y = dir.y + (dirY * w);
      lenY = saturate(abs(dirY) * lenY);
      lenY = lenY * lenY;
      len = len + (lenY * w);
      fsr1.dir = dir;
      fsr1.len = len;
      return fsr1;
    }
    fn gather_red_components(c: vec2<f32>) -> vec4<f32> {
      return textureGather(0,input,sam,c);
    }
    fn gather_green_components(c: vec2<f32>) -> vec4<f32> {
      return textureGather(1,input,sam,c);
    }
    fn gather_blue_components(c: vec2<f32>) -> vec4<f32> {
      return textureGather(2,input,sam,c);
    }
    @fragment
    fn fs(@builtin(position) p: vec4f) -> @location(0) vec4<f32>{
      var inputSize : vec2<f32>;
      inputSize.x = f32(textureDimensions(input).x);
      inputSize.y = f32(textureDimensions(input).y);
      var dstSize : vec2<f32>;
      dstSize.x = outputSize.x;
      dstSize.y = outputSize.y;
      var pp:vec2<f32> = (floor(p.xy) + 0.5) / dstSize * inputSize - 0.5;
      var fp : vec2<f32> = floor(pp);
      pp = pp - fp;
      var p0 :vec2<f32> = fp + vec2<f32>(1.0, -1.0);
      var p1 : vec2<f32> = p0 + vec2<f32>(-1.0, 2.0);
      var p2: vec2<f32> = p0 + vec2<f32>(1.0, 2.0);
      var p3: vec2<f32> = p0 + vec2<f32>(0.0, 4.0);
      p0 = p0 / inputSize;
      p1 = p1 / inputSize;
      p2 = p2 / inputSize;
      p3 = p3 / inputSize;
      var bczzR : vec4<f32> = gather_red_components(p0);
      var bczzG : vec4<f32> = gather_green_components(p0);
      var bczzB : vec4<f32> = gather_blue_components(p0);
      var ijfeR : vec4<f32> = gather_red_components(p1);
      var ijfeG : vec4<f32> = gather_green_components(p1);
      var ijfeB : vec4<f32> = gather_blue_components(p1);
      var klhgR : vec4<f32> = gather_red_components(p2);
      var klhgG : vec4<f32> = gather_green_components(p2);
      var klhgB : vec4<f32> = gather_blue_components(p2);
      var zzonR : vec4<f32> = gather_red_components(p3);
      var zzonG : vec4<f32> = gather_green_components(p3);
      var zzonB : vec4<f32> = gather_blue_components(p3);
      var bczzL : vec4<f32> = bczzB * 0.5 + (bczzR * 0.5 + bczzG);
      var ijfeL : vec4<f32> = ijfeB * 0.5 + (ijfeR * 0.5 + ijfeG);
      var klhgL : vec4<f32> = klhgB * 0.5 + (klhgR * 0.5 + klhgG);
      var zzonL : vec4<f32> = zzonB * 0.5 + (zzonR * 0.5 + zzonG);
      var bL :f32 = bczzL.x;
      var cL :f32 = bczzL.y;
      var iL :f32 = ijfeL.x;
      var jL :f32 = ijfeL.y;
      var fL :f32 = ijfeL.z;
      var eL :f32 = ijfeL.w;
      var kL :f32 = klhgL.x;
      var lL :f32 = klhgL.y;
      var hL :f32 = klhgL.z;
      var gL :f32 = klhgL.w;
      var oL :f32 = zzonL.z;
      var nL :f32 = zzonL.w;
      var fsr:FsrSet;
      fsr.dir = vec2<f32>(0.0,0.0);
      fsr.len = 0.0;
      fsr = FsrEasuSet(fsr, pp, true, false, false, false, bL, eL, fL, gL, jL);
      fsr = FsrEasuSet(fsr, pp, false, true, false, false, cL, fL, gL, hL, kL);
      fsr = FsrEasuSet(fsr, pp, false, false, true, false, fL, iL, jL, kL, nL);
      fsr = FsrEasuSet(fsr, pp, false, false, false, true, gL, jL, kL, lL, oL);
      var dir2 :vec2<f32> = fsr.dir * fsr.dir;
      var dirR :f32 = dir2.x + dir2.y;
      var zro : bool = dirR < 1.0 / 32768.0;
      dirR = inverseSqrt(max(dirR, 1e-8));
      if (zro) {dirR = 1.0;};
      if (zro) {fsr.dir.x = 1.0;};
      fsr.dir = fsr.dir * dirR;
      fsr.len = fsr.len * 0.5;
      fsr.len = fsr.len * fsr.len;
      var stretch :f32 = (fsr.dir.x * fsr.dir.x + fsr.dir.y * fsr.dir.y) * 1.0/(max(abs(fsr.dir.x), abs(fsr.dir.y)));
      var len2:vec2<f32> = vec2<f32>(1.0 + (stretch - 1.0) * fsr.len, 1.0 - 0.5 * fsr.len );
      var lob : f32 = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * fsr.len;
      var clp : f32 = 1.0/lob;
      var min4 = min(min3(vec3<f32>(ijfeR.z, ijfeG.z, ijfeB.z), vec3<f32>(klhgR.w, klhgG.w, klhgB.w), vec3<f32>(ijfeR.y, ijfeG.y, ijfeB.y)),
      vec3<f32>(klhgR.x, klhgG.x, klhgB.x));
      var max4:vec3<f32> = max(max3(vec3<f32>(ijfeR.z, ijfeG.z, ijfeB.z), vec3<f32>(klhgR.w, klhgG.w, klhgB.w), vec3<f32>(ijfeR.y, ijfeG.y, ijfeB.y)),
      vec3<f32>(klhgR.x, klhgG.x, klhgB.x));
      var fsr2:FsrTap;
      fsr2.aC = vec3<f32>(0.0,0.0,0.0);
      fsr2.aW =0.0;
      fsr2=FsrEasuTap(fsr2, vec2<f32>(0.0, -1.0) - pp, fsr.dir, len2, lob, clp, vec3<f32>(bczzR.x, bczzG.x, bczzB.x)); // b
      fsr2=FsrEasuTap(fsr2, vec2<f32>(1.0, -1.0) - pp, fsr.dir, len2, lob, clp, vec3<f32>(bczzR.y, bczzG.y, bczzB.y)); // c
      fsr2=FsrEasuTap(fsr2, vec2<f32>(-1.0, 1.0) - pp, fsr.dir, len2, lob, clp, vec3<f32>(ijfeR.x, ijfeG.x, ijfeB.x)); // i
      fsr2=FsrEasuTap(fsr2, vec2<f32>(0.0, 1.0) - pp, fsr.dir, len2, lob, clp, vec3<f32>(ijfeR.y, ijfeG.y, ijfeB.y)); // j
      fsr2=FsrEasuTap(fsr2, vec2<f32>(0.0, 0.0) - pp, fsr.dir, len2, lob, clp, vec3<f32>(ijfeR.z, ijfeG.z, ijfeB.z)); // f
      fsr2=FsrEasuTap(fsr2, vec2<f32>(-1.0, 0.0) - pp, fsr.dir, len2, lob, clp, vec3<f32>(ijfeR.w, ijfeG.w, ijfeB.w)); // e
      fsr2=FsrEasuTap(fsr2, vec2<f32>(1.0, 1.0) - pp, fsr.dir, len2, lob, clp, vec3<f32>(klhgR.x, klhgG.x, klhgB.x)); // k
      fsr2=FsrEasuTap(fsr2, vec2<f32>(2.0, 1.0) - pp, fsr.dir, len2, lob, clp, vec3<f32>(klhgR.y, klhgG.y, klhgB.y)); // l
      fsr2=FsrEasuTap(fsr2, vec2<f32>(2.0, 0.0) - pp, fsr.dir, len2, lob, clp, vec3<f32>(klhgR.z, klhgG.z, klhgB.z)); // h
      fsr2=FsrEasuTap(fsr2, vec2<f32>(1.0, 0.0) - pp, fsr.dir, len2, lob, clp, vec3<f32>(klhgR.w, klhgG.w, klhgB.w)); // g
      fsr2=FsrEasuTap(fsr2, vec2<f32>(1.0, 2.0) - pp, fsr.dir, len2, lob, clp, vec3<f32>(zzonR.z, zzonG.z, zzonB.z)); // o
      fsr2=FsrEasuTap(fsr2, vec2<f32>(0.0, 2.0) - pp, fsr.dir, len2, lob, clp, vec3<f32>(zzonR.w, zzonG.w, zzonB.w)); // n
      var c : vec3<f32> = min(max4, max(min4, fsr2.aC * (1.0/fsr2.aW)));
      return vec4<f32>(c, 1.0);
    }`, rcas: `
    fn tap(uv: vec2f) -> vec3f { return textureSampleLevel(input, sam, uv, 0.0).rgb; }
    fn luma(c: vec3f) -> f32 { return c.b * 0.5 + (c.r * 0.5 + c.g); }
    @fragment fn fs(@builtin(position) p: vec4f) -> @location(0) vec4f {
      let uv = p.xy / outputSize.xy;
      let px = 1.0 / vec2f(textureDimensions(input));
      let b = tap(uv - vec2f(0.0, px.y)); let d = tap(uv - vec2f(px.x, 0.0));
      let e = tap(uv); let f = tap(uv + vec2f(px.x, 0.0)); let h = tap(uv + vec2f(0.0, px.y));
      let bL = luma(b); let dL = luma(d); let eL = luma(e); let fL = luma(f); let hL = luma(h);
      let lo = min(min(min(bL, dL), min(eL, fL)), hL);
      let hi = max(max(max(bL, dL), max(eL, fL)), hL);
      let nz = 1.0 - 0.5 * clamp(abs(0.25 * (bL + dL + fL + hL) - eL) / max(hi - lo, 1e-8), 0.0, 1.0);
      let mn = min(min(b, d), min(f, h)); let mx = max(max(b, d), max(f, h));
      let hitMin = min(mn, e) / max(4.0 * mx, vec3f(1e-8));
      let hitMax = (vec3f(1.0) - max(mx, e)) / min(4.0 * mn - vec3f(4.0), vec3f(-1e-8));
      let lobes = max(-hitMin, hitMax);
      // FsrRcasCon converts stops to the linear multiplier.
      let lobe = max(-0.1875, min(max(max(lobes.r, lobes.g), lobes.b), 0.0)) * ${Math.pow(2, -(1 - sharp) * 2).toFixed(9)} * nz;
      return vec4f((lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0), 1.0);
    }` };
}
