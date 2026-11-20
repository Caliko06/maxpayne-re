# Render path: game loop to Present

Sources: Ghidra corpus rea/c/e2_d3d8_driver_mfc.dll, census/hopper, census/d3d8_vtcalls.json, census/d3d8_spec.tsv (every D3D8
call site of the driver) and the native trace below. Addresses are rebased (e2_d3d8_driver_mfc.dll at 0x10400000).
Runtime oracle: `RN_TRACE=1` native run, 90 s, menu then Tutorial, 5,644 Presents, 713,372 draws (2026-11-01).

## Path
| Step | Function | D3D8 calls |
|---|---|---|
| Frame start | P_D3D::DRV_beginScene 0x10428340 | TestCooperativeLevel, Reset on a lost device, BeginScene |
| Clears | P_Driver::clearScreenBuffers e2mfc 0x10133010 -> P_D3D::DRV_clearBuffer | Clear(1 rect; flags 1 color, 2 depth, 7 all) |
| State | P_D3D::setRenderState, setTextureStageState, setTransform 0x1042f2f0, 0x1042f3b0, 0x1042f470 | filtered by value in the driver |
| Lights, materials | P_D3DVirtualObject::prepare*Light, P_D3DOutputPrimitive::setupPass 0x10415ef0 | SetLight, LightEnable, SetMaterial, SetClipPlane |
| Light ids | P_D3DVirtualObject::prepare*Light 0x10423cb0/0x10423ec0/0x10424270 store light+0x110; 0x10416a60 LightEnable(id,0) per object | ids are per-light, not 0..7: host/d3d8.c keys lights by id (1024 slots), enabled list in enable order, first 8 drawn |
| Geometry | P_D3DOutputPrimitive::render -> draw 0x104170e0, drawLines 0x104174e0 | SetStreamSource, SetIndices, DrawIndexedPrimitive; DrawPrimitive for lines |
| Frame end | P_D3D::DRV_endScene 0x10428560 | EndScene |
| Swap | P_Driver::swapScreen e2mfc 0x10132ac0 -> P_D3D::DRV_swapBuffer 0x1042a420 | Present(0,0,0,0); windowed (this+0xf4): Present(0,&rect,0,0) |

## Buffers (P_D3DDDVertexBuffer::allocate 0x1041cf40, lock 0x1041dbb0; P_D3DDDIndexBuffer lock 0x1040da00)
- Vertex and index buffers are DEFAULT pool, usage 0x8 WRITEONLY, or 0x208 with DYNAMIC. Dynamic locks use DISCARD (0x2000),
  the others flags 0. No NOOVERWRITE, no ring. Indices are always INDEX16.
- FVF built from the vertex format; a custom layout leaves FVF 0 and uses a vertex shader.

## What the game really uses (trace)
- Menu: 16 draws, about 1 clear a frame. Tutorial level: ~132 draws, ~8,400 triangles, ~3 clears, ~8 viewports a frame,
  12 VB and 9 IB locks a frame. All 713,372 draws are DrawIndexedPrimitive with D3DPT_TRIANGLELIST.
- FVFs: 0x12, 0x112, 0x142, 0x152, 0x212. No CreateVertexShader, no SetPixelShader with a shader, no SetRenderTarget, no cube.
- Textures: 343 created, MANAGED, fmt 21 A8R8G8B8 and 22 X8R8G8B8, up to 4096x1024. 2,995 surface locks, none at runtime
  after a load.
- Render states that change per draw: 14 ZWRITE, 15 ALPHATEST, 19/20 SRC/DSTBLEND (2/1, 5/6), 22 CULL, 23 ZFUNC (4, 8), 24 ALPHAREF,
  27 ALPHABLEND, 28 FOG, 136 CLIPPING, 137 LIGHTING, 139 AMBIENT, 140 FOGVERTEXMODE (0, 3).
- Stage states: stage 0 and 1 COLOROP 1, 2, 4, 7, 13; ALPHAOP 1, 2, 4; ADDRESS 1, 3; filters 1, 2; MIPFILTER 0, 1. Stages 2 and 3 stay off.
- Present: CreateDevice 1024x768, X8R8G8B8, 2 back buffers, D24S8 (fmt 75), SwapEffect DISCARD, PresentationInterval 1.

## In the driver, not seen in the trace (census/d3d8_spec.tsv)
Vertex shaders assembled with static D3DX, skinning (XYZB1-4, INDEXEDVERTEXBLEND), N-patches, DXT1/3/5, bump formats, volume and cube
textures, render to texture. Which of them run depends on the caps we report. Each one stops loudly in host/d3d8.c or web/gpu.js.

## Readback
- P_D3D::DRV_getBackBuffer 0x1042a100: GetBackBuffer + LockRect READONLY. P_D3D::DRV_getScreen 0x10429d20: CreateImageSurface
  A8R8G8B8, GetFrontBuffer, LockRect. Called through P_Driver::getBackBuffer / getScreenShot (e2mfc 0x10132f40, 0x10132cd0).
- Decision: both go through GPU_READ; the game thread waits on box.reads.
- Texture memory: a MANAGED A8R8G8B8/X8R8G8B8 texture gives its level memory back when the last level unlocks, not earlier,
  since D3DX filtering (0x10433c73, per flow's runtime trace) reads level n to build n + 1. A later lock fetches the level through GPU_READ.
  0x1042db00 (Ghidra; LockRect flags 0, P_Driver::convertTexture, UnlockRect) is that case, from P_Map teardown 0x101209a0.
  A fetched level keeps its memory until Release. Native Tutorial heap: 71 MB used at 60 s (1.8 GB before, flow's level 1 number).

## Decisions for the port
- Keep FFP -> shader model 1 tokens -> WGSL (host/ffp.c, host/sm1.c): one source for the native CPU shots and the web renderer.
- 130 draws a frame needs no resident buffers: vertices, indices and one uniform block per draw go through arenas read in place.
- Present waits for the screen (box.shown), D3D's wait for interval 1. Before this the game ran at ~1,100 Presents a second
  with ~52 shown and the page thread saturated.
- rAF-only encoding is rejected: RE-NFSU measured 17/34 ms alternation. Packets encode on arrival, into a queued texture.
- Texture transforms (2026-11-02, ghost's halt upstairs in part0_level1: TTFF 0x103 = COUNT3|PROJECTED on stage 1). The
  driver sets them from the material's pass data, not from code: FUN_104041e0 (driver 0x100041e0 unrebased) per stage writes
  TSS 0x10-0x12 filters, TEXCOORDINDEX = byte +0xd | byte +0xe << 16 (set | texgen), TEXTURETRANSFORMFLAGS = word +0xb; the
  matrix comes through SetTransform 0x1042f470 (D3DTS_TEXTUREn). Port: host/ffp.c follows the DX8.1 "Texture Coordinate
  Processing" rules: set padded with 1 after its last component or texgen CAMERASPACEPOSITION / NORMAL as (x, y, z, 1),
  times D3DTS_TEXTUREn (FC_TEXM, 2 stages at most; lights now 7 constants each to make room); PROJECTED puts the divisor in w
  and the ps tex carries D3DSI_TEXLD_PROJECT (bit 16), so sm1.c samples xy / w per pixel. Reflection-vector texgen and
  TTFF with a game pixel shader still stop loudly.
- Stencil, ZBIAS, user clip planes, table fog, point draws: never set in the trace; they stop or log loudly.
- SetClipPlane exists in setupPass; the trace shows CLIPPLANEENABLE (152) set once, to 0.

## Measured on the web build (Metal, headless shell, 60 Hz rAF, 1024x768, 2026-11-02)
| Scene | Shown per 2 s | Gap p50 / p99 | Encode per frame | Draws | Passes |
|---|---|---|---|---|---|
| Main menu | 120 | 16.7 / 16.7 ms | 0.18 ms | 16 | 1.0 |
| Tutorial level | 120 | 16.7 / 16.7 ms | 0.39-0.42 ms | 130-132 | 1.0 |
- Level load: gaps up to 6.2 s while the game thread loads (no Present). F5 costs one gap of up to 216.7 ms; a quick load's worst gap was 50 ms.
- Tutorial start matches the native CPU shot: textures, alpha, snow, lights, HUD. F5 wrote a save, F9 F9 reloaded the level.
- New Game on Fugitive, no stop in the render: opening 3D scene 126-155 draws, graphic novel 5-7 draws, Prologue 120 draws, all at
  120 shown per 2 s with gap p99 16.7 ms. Map teardown fetched 156 texture levels; worst gap in that window 350 ms.
- Save readback is not the F5 gap: 2 reads took 2.1 ms in a window with a 216.7 ms gap, so they cost about 1 ms each.
- No .dds in the 6 archives or loose files (4,065 archive entries): textures come from jpg, tga and pcx, as in the Tutorial.
- Bullet time (LShift, native CPU backend): the HUD meter drained, no halt.
- Not measured: a real Chrome window, a 120 Hz display. Not seen: Parts II and III, movies.

## Telemetry (web/telemetry.js, tools/telemetry.py; port of RE-NFSU's)
- One row per packet web/gpu.js runs (a Present ends a frame), an idle row after 250 ms with no packet (loads, saves, stalls).
  Host totals from host.h Telem as deltas: gpuWait (page runs the packet), vsync (game ahead of the screen: free time, host/gpu.c
  gpu_wait), gilMain/gilOther, read (game files), idleMain, gpuRead (d3d8.c read_gpu), save (files.c gf_flush: files written to
  save/, settings and savegames), audio ring (behind, underrun, resyncs, peak, clipped samples in header word [7]).
- Game state, read from guest memory (Telem.guest_mem = wasm address of guest 0; Ghidra + Hopper/capstone, 2026-11-02):
  ModeSwitch* = u32[0x8a6400] (theApp 0x8a6338 + 0xc8, stored 0x40b3a2); current mode ms+0x19 (0x724b90), next ms+0x1d (0x749780),
  fade ms+0x0d (1 stable, 2 in, 3 out); mode slots ms+0x70 console, +0x74 statistics, +0x78 HUD, +0x7c menu, +0x80 game,
  +0x84 graphic novel (ctor 0x415dc0). Game mode: level std::string +0xcc (chars u32[+0xd0], length u32[+0xd4]; set at the end
  of level init 0x44fda0, cleared by 0x44fba0); pending action +0xb4 (0x451960: 1 new level, 2 load, 3/4 save); level up when
  +0x1074 and +0x1080 (player wrapper, 0x451e50) are non-zero. No loading flag exists: 'load' = game mode before its level is up.
- Files: build/telemetry.ndjson + build/web.log (OWNER), build/webcheck.* (headless), *.prev.* = the session before. Keys: F8 mark,
  F7 live panel, F10 download maxpayne-session-*.json (none is a game key; F10 only with -developerkeys, never passed).
- First gate (headless, New Game -> Fugitive, 240 s): menu 60 fps; startup_level 3.5 s load; part0_level1 load 10.1 s (worst
  idle frame 1.9 s, game code); in level p99 21 ms; one 2.1 s frame at 191 s in part0_level1 with the GIL held 2.1 s by the main
  thread (game code, no reads): 5 music laps with no refill; F5 frame 214 ms; audio peak 35538/32767 (above full scale).
  New Game writes 309 files (4.7 MB) to save/ in 0.5 s before the first load.

## Parity web vs native (2026-11-02, bc8cade)
- Comic P1L0a_004 end state (20 s after its voice Play) and part0_level1 hallway (~9 s into game mode): the same panels,
  wallpaper, lighting, alpha and filtering on both; only the camera of the moving intro differs by a few frames.
- Gamma: the game calls SetGammaRamp once, at boot, with a non-identity ramp (R = G = B; 64 -> 17152, 255 -> 65280).
  web/gpu.js applies it on screen as the monitor would; native shots (host/gpu.c GPU_GAMMA) show the raw frame buffer.
  So web is a little brighter in the mid tones than native shots, by design.
- No fog in either moment (indoor); fog and mip bias still unchecked against native.

## GPU share of boot and load hitches (2026-11-02, gate New Game -> part0_level1, 188 s)
- Page time per packet ("run": packet decode, pipeline and shader creation, writeTexture calls) summed per mode:
  boot 30 ms, load 141 ms for 1.76 GB of texture levels, game 3.9 s over 87 s; no packet above 8 ms. gpuWait is 0 in every
  frame, and the screen never dropped a frame (vsync events: 0 drops, longest rAF gap 16.7 ms).
- The 35 pipelines made in game mode cost <= 2 ms of page time each. Their frames (22.6 s 105 ms, 27.6 s 95 ms) are the
  first frames of a level, and the frame time there is guest code, not page time.
- Host side: A8R8G8B8/X8R8G8B8 levels go to the blob by memcpy (decode_texels), 1.76 GB in the load, which is a few
  hundred ms at memcpy speed spread over the ~11 s load.
- So boot and load hitches are not GPU-side. Load frames are guest code 83% and file reads 12% (the 1.5 s frame at
  112.4 s: game 1500 ms, no reads, no textures); boot frames are file reads 55%. Chunked uploads would move < 1% of that.
- The first-use pipeline compile is the one GPU wait this page does have, and page time does not show it: it is the device
  holding the present. Part III, s1104c (out/bec/s1104/P_result.md): part3_level5 creates 12 pipelines at 18.60 (0.6 ms of
  page time, `pipeline()`) and the next frame is 97.0 ms with 75.3 ms of it waiting for the screen (dt/vsync after each
  new pipe: 53.7/43.6, 48.9/39.0, 38.5/27.3, 46.6/37.3, 103.8/96.6). The 2026-11-02 gate's 35 pipelines were <= 2 ms each
  on frames that were guest code, which is why this note read "no GPU change": the cost lands on the frame whose first
  draw needs the pipeline, not on the packet that creates it.
- Since s1104 web/gpu.js builds a scene pipeline with createRenderPipelineAsync and skips that draw until the device has it
  (Dolphin "asynchronous shader compilation", yuzu async shaders): the object appears one or two frames later, once per new
  material, and the present never waits for Metal. The screen pass and the clears keep sync pipelines (a skipped blit or
  clear would be a visible hole). Not measured on a run yet: fix 2 of out/bec/s1104/R_result.md.

## RE coverage (2026-11-02; names in rea/map/names/<image>.tsv, columns in its README)
| Image | Functions | Named | Understood | Unknown |
|---|---|---|---|---|
| grphmfc.dll | 394 (303 Ghidra + 91 Ghidra missed, found by a capstone sweep) | 351 | 43 | 0 |
| e2_d3d8_driver_mfc.dll own code 0x10401000-0x1042ffff | 1222 (+8 vtable targets Ghidra missed) | 685 | 461 | 76 |
| e2_d3d8_driver_mfc.dll static D3DX/libjpeg/libpng/zlib/CRT 0x10430000-0x10456000 | 760 (695 Ghidra + 65 Hopper only) | 177 | 374 | 209 |
- Excluded: 396 Unwind@ funclets (0x10460050..0x10461bc0) and the import/export thunk block.
- Evidence: exports and RTTI; libjpeg 6a method-table slots and error codes (grphmfc: every module jdapimin..jquant2;
  0x10206010 is a memory source, not jdatasrc's FILE*); IDirect3DDevice8 vtable offsets (census/d3d8_spec.tsv) for the
  driver's wrappers; D3DX API shapes, error codes and strings. "understood" = purpose known, descriptive name.
- Unknown in the driver: 30 thunks to unnamed targets, 46 own functions without evidence, and in the static part mostly
  libjpeg/libpng/zlib internals and the per-format box filter kernels (0x10439925..0x1043a654).
- Classes and vtables. grphmfc: G_Image 0x1021210c (1 slot), ten exception classes of 8 slots
  (G_ImageLoaderException_JPGDecode 0x102121d8, ..._DDSDecode/PCX/SCX/TGA, R_MemoryException(_Allocation)).
  Driver (RTTI, ~70 vtables): P_D3D 0x10463a7c (43), P_D3DCPUDriver 0x10463728 (15), P_D3DVertexBuffer 0x10463188 (47),
  P_D3DDDVertexBuffer 0x10463930 (57), P_D3DOutputPrimitive 0x10463840 (23), P_D3DLineBuffer 0x10463624 (26),
  P_D3DTriangleBuffer 0x10463354 (13), P_D3DMaterialInfo 0x10463050 (8). D3DX (no RTTI; vtable + creator): CD3DXFont
  0x10463f3c, CD3DXSprite 0x10463f60, CD3DXRenderToSurface 0x10463f84, CD3DXRenderToEnvMap 0x10463fa0, ID3DXBuffer
  0x104646e8/0x10464700, pixel format classes 0x104647c4..0x10464914 (dtor, load_row, store_row per D3DFMT).
- Claim to check (one source): 0x10433ec5 = D3DXFilterTexture and 0x10433ff6 = D3DXFilterCubeTexture (flow.md has 0x10433ff6
  as FilterTexture).
- Hopper: census/hopper_names_*.json applies every name (made unique against Hopper's own: name_<addr>) in one q.mjs batch
  (node tools/rea/q.mjs census/hopper_names_grphmfc.json; 394 and 1705 applied, 0 failed). REA's Hopper session does not
  keep edits after close_binary, so the batch is how a session gets them.

## Open (2026-11-02)
- F5 gap: FIXED at the cause (2026-11-02). G_Image::copyWithScale 0x10203be0 (save thumbnail 256x192 from the 1024x768 screen:
  areaSample 0x102032c0 -> FUN_10203460 -> FUN_10203700, x87 + _ftol per pixel) took ~235 ms lifted. Host stand-in
  rn_copy_with_scale (host/fast.c, patches.py HOST_FUNCS; Ghidra + capstone disasm agree; RN_FASTCHECK: 4 real calls, 0 differ,
  image and globals 0x102169b0..0x102169e0). Telemetry, Fugitive F5 in level: the save frame 214 ms -> 87-134 ms. What is left:
  3 PUTs ~28 ms (files.c) and game code in 20-35 ms GIL holds (old samples: msvcrt 0x108xxxxx, unnamed; flow's area).
  The float (pc 0) path of the stand-in never ran in the check.
- Teardown GPU_READs (156 levels, worst gap 350 ms): FUN_101209a0 reads the texture into P_Map +0x2c (driver vtable +0x90, 0x1042db00),
  then releases it (vtable +0x88). P_Map::destruct 0x1011f7d0 frees that image at once (G_Image::destruct +0x2c), so that read is wasted.
  P_Driver::destroyDriverObjects 0x10133150 calls the same function and keeps the image, so the read cannot be dropped for every caller.
  The read is skipped when P_Map +0x8c != 0, +4->+0x1c != 0, or state +0x90 is 2 or 3. Not fixed: d3d8.c cannot see the caller
  without walking the guest stack, and a code patch needs a code cave the lifter can find. Hopper not checked.
  Measured by telemetry (headless gate, startup_level -> part0_level1, 2026-11-02): 154 reads, 131 ms in a 10.1 s load
  (0.85 ms each, 1.3% of it). Decision: no code patch; the cost does not justify a guest code change.
- "Narrow comic panel" (platform gn8.png): NOT a render bug. A page jpg is 1024x512 with 1-3 panels; its .kf2 scene draws one
  Plate plus a black cover per later panel (P1L0a_002.kf2: Black_01; _003: Black_01..03; strings in x_english.ras d4), and the
  covers fade out over the page time. Web gate with RN_SOUNDLOG (voice Play = page start): 15 s into P1L0a_003 (30 s voice) only
  panel 1 shows; 15 s into P1L0a_004 the right panel is mid fade-in. Not checked: a page's last frame against a native shot.
- SetRenderTarget (host/d3d8.c:1090) holds no ref on the target. The trace has no SetRenderTarget, so it is untested.
