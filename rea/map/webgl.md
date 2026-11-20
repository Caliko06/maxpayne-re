# WebGL 2 browser renderer

Source: RE-NFSU `web/webgpu_gl.js`, commit `12d05ab` (2026-11-18), adapted from
[vExcess/webgpu-js](https://github.com/vExcess/webgpu-js), revision `4a800f6`, MIT.
Its license is preserved at the top of the copy. No original game code changed.
The host contract is `host/gpu.h` ABI 3, emitted by `host/d3d8.c:1280` through
`host/sm1.c:329`; `web/gpu.js` consumes the same packets on either backend.

`gpuAdapter(canvas)` imports the shim only if `navigator.gpu` is absent, before
requesting its adapter. The shim creates WebGL 2 on that canvas. Native WebGPU
keeps its existing path. The page's capability check must call `gpuAdapter`
unconditionally, so browsers without the native API can reach the fallback.

## Max Payne adaptations

- GLSL declares fixed uniform arrays after their field names (`GpuU.c[84]`,
  `pc[8]`) and zero-initializes shader output structs as WGSL requires.
- Struct `vertex_index`, scalar boolean constants, inferred vector comparisons
  including uniform-array operands, and reserved GLSL binding `input` translate.
- `textureGather` in EASU becomes four clamped `texelFetch` calls with WGSL's
  gather order. This matches its actual clamp-to-edge, level-zero input sampler.
- Texture uploads/readbacks swap BGRA/RGBA; D3DCOLOR vertex inputs swap B/R.
  Readbacks copy packed GL rows into WebGPU's padded `bytesPerRow` layout.
- The screen's fragment coordinates and partial viewport/scissor rectangles keep
  WebGPU's top-left origin; offscreen targets keep logical row zero at GL row zero.
- Removed NFSU-specific opaque-alpha repair, mip regeneration and depth-equal
  relaxation. Max Payne uploads its original mip chain and supplies depth state.

## Translation limits and validation

All constructs currently emitted by `host/sm1.c` and `web/gpu.js` have translation
paths, including FXAA, the gamma ramp, clears, bilinear, nearest, EASU and RCAS.
No known current source construct is deliberately dropped. This is a scoped
translator, not a complete WGSL implementation: general compute/workgroup
synchronization, atomics, storage textures and arbitrary pointer address spaces
are unsupported and unused by Max Payne's renderer. EASU gather emulation is
only defined for the actual 2D float, level-zero, clamped input used here.

FACT: `node --check web/gpu.js` and `node --check web/webgpu_gl.js` pass.
FACT: a native sample using the real `sm1_wgsl` generator translated both stages
with arithmetic, SLT/SGE/CND/CMP, bitcasts and D3D helpers; extracted screen,
clear, EASU and RCAS shaders also translated in Node without residual WGSL.
UNKNOWN: full shader-corpus GL compilation and visual equivalence until the
orchestrator's WebGPU-disabled webgate run. No browser run was spent by this worker.

## Local dist (NOCHE50 live-web W1)

`tools/build_public.py` packs the game as content-addressed gzip pieces
(`data-chunks/<sha256>.gz`, 256 KB per piece, `data-chunks.json` index) into
`build/public`; `tools/static_server.py` sends a piece as an entire gzip
representation (`Content-Encoding: gzip`) and answers `/game/<path>` by
decoding the pieces a Range needs, because `host/files.c` reads game files with
`Range: bytes=a-b` and no C code was touched (0.660 of raw bytes; game data
stays out of git). `web/gpu.js` imports `web/webgpu_gl.js` when
`navigator.gpu` is missing or `?gl=webgl2` asks for it; the shim replaces
`navigator.gpu` and its adapter reports `webgpu-js webgl2 WebGPU.js on ANGLE`.
FACT: `?gl=webgl2` on :5254 reached the main menu (out/live-web/webgl2.png).
