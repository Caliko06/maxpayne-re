# Forensic Reconciliation Book — `maxpayne-re`

**Date:** 2026-11-20 · **Scope:** End-to-end forensic audit and 4-wave reconciliation across all 5 layers of `maxpayne-re` (Recompiler/Build, Host Win32/FS/Threads, Host D3D8/DSound/DInput8, Web/Mobile/Safari/WebGPU, and Local Serve/Cloudflare R2 Deploy).

> **ELI5 — What happened here?**
> `maxpayne-re` is a 5-story building where each floor speaks a different language: x86-to-C-to-Wasm in the basement, Win32/files/threads on floor 1, D3D8/DirectSound on floor 2, WebGPU/Safari/mobile touch on floor 3, and Cloudflare R2 packaging on the roof. Over time, each floor made quiet assumptions about the others. This book documents every real bug, swallowed error, implicit state machine, structural hurdle, and cross-layer drift we found — and the exact surgical patches applied across **36 files (+618 / -443 lines)** in 4 waves.

---

## Governing Default Principles (`AGENTS.md`)

Every reconciliation patch was gated against the two mandatory default principles before touching code:

1. **Dual Layout Principle (OWNER 2026-11-19):** Every UI/input/video change is designed and verified for **both** desktop and phone (device-detected).
2. **Mandatory Phone Light Version (OWNER 2026-11-20):** A phone **always** plays the light version — original 2001 textures (no HD pack), 640 MB low memory (`RN_GUEST_MB=448`, `RN_MAXTEX=1024`, `RN_LOWMEM=1`), and a hard **30 fps** cap that no video option (such as Gamma) may defeat.

---

## Wave-by-Wave Forensic Reconciliation Log

### Wave 1 — Safari, Phone Sharpness/Touch, Audio & Save Integrity (10 files)

| ID | Forensic Finding | Root Cause | File(s) Reconciled | Resolution |
|---|---|---|---|---|
| **BUG-01** | Safari OPFS crash on saving controls | `MPControls.write()` called `FileSystemFileHandle.createWritable()`, unimplemented in Safari OPFS | `web/launcher-controls.js` | Routed OPFS writes through `MPFiles.save('PUT', 'registry.tsv', false, bytes)` (Worker `createSyncAccessHandle()`). |
| **BUG-14 / SWL-13** | OPFS `save()` sent full backing `ArrayBuffer` + hanging worker on crash | Ignored `byteOffset`/`byteLength` (breaks subarrays & `SharedArrayBuffer`); no `onerror` handler on Worker; `hasFile()` swallowed all errors | `web/opfs.js` | Copied exact view slice `[byteOffset, byteOffset + byteLength)` into a clean `ArrayBuffer`; added `onerror`/`onmessageerror` rejecting all pending `waits`; logged non-`NotFoundError` errors in `hasFile()`. |
| **BUG-02 / BUG-03 / XLY-06** | AudioWorklet startup `NaN` + resync storm + 48 kHz pitch shift + iOS unlock | `t = w - target` went negative when `0 < w < 1536` (negative array index → `NaN`, u32 wraparound); 44.1 kHz hardcoded against 48 kHz iOS/Bluetooth hardware; unhandled `ctx.resume()`; missing `touchend`; unrevoked `blob:` URL | `web/audio.js` | Clamped `t = Math.max(0, w - this.target)`; added linear resampler keyed by `ratio = 44100 / ctx.sampleRate` (zero overhead when `ratio === 1`); revoked `blob:` URL; added `.catch()` on `resume()`; added `touchend` and removed unlock listeners once running. |
| **HRD-01 / BUG-09 / XLY-10** | Blurry & aspect-distorted phone display (`960×480` 2:1) + broken 30 fps cap | `playSize()` clamped `short * k` up to 480 (crushing 19.5:9 phones to 2:1); touching *any* launcher video option set `video.fps = 0` (uncapped) on phones and capped 120 fps to 60 on desktop; 9-tap FXAA (`POLISH`) blurred phone text/HUD | `web/index.html`, `web/gpu.js`, `web/launcher-options.js` | Preserved true landscape phone aspect ratio at `480..540p` within light-version budget; enforced 30 fps unconditionally on non-desktop even when `MP_VIDEO` is set; fixed desktop 120 fps cap; disabled `POLISH` FXAA on non-desktop; prevented phone options from overriding `RN_MAXTEX=1024` or `profile='phone'`. |
| **BUG-17 / HRD-05** | `gpu_wait()` deadlock on WebGPU throw + per-frame layout thrash + buffer leaks | `showQueued()` shifted `shownQ` before submit without `try/finally` (leaking a `GPU_QUEUED` credit); `resizeScreen()` called `getBoundingClientRect()` every rAF; `grow()` dropped old `GPUBuffer`s without `.destroy()` | `web/gpu.js` | Wrapped `showQueued()` in `try/finally { freeQ.push(q); shown(); }`; cached canvas rect via `ResizeObserver`; called `.destroy()` on replaced `vbuf`/`ibuf`/`ubuf`. |
| **BUG-05..08 / HRD-08** | Broken menu taps on 120 Hz phones + double-tap release + app-switch click + dual-finger stick fight | `tap()` waited 3 display rAFs (25 ms on 120 Hz < 33.3 ms game frame at 30 fps); `MIN_PRESS` timer wasn't cleared on re-press; `visibilitychange` called `up()` instead of `cancel()`; no `stickId` lock; eternal `requestFullscreen()` retry on iOS | `web/touch.js` | Replaced rAF waits with `gameFrames(2, 120)` tracking `RN_PACE.shown`; cleared per-button `upTimer` on `pointerdown`; called `o.cancel ? o.cancel() : o.up()` on `visibilitychange`; guarded single `stickId`; skipped fullscreen retry listener when unsupported. |
| **BUG-10 / BUG-11 / BUG-15 / XLY-07 / XLY-09 / SWL-10** | `NaN` mouse poisoning, stuck mouse button, double-`PAUSE` race, `GSF_LEVEL` bit mismatch, `?files=server` ignored | Unchecked `e.movementX` poisoned `remX`/`remY`; `mouseup` bound to `canvas` instead of `window`; `live` backdrop raced `tap(PAUSE)` and checked `!(f & 2)` (`GSF_PLAYER`) instead of `(f & 3) !== 3`; `opfsDir()` ignored `?files=server` | `web/input.js`, `web/index.html`, `web/launcher-options.js` | Guarded `Number.isFinite(...)` on all deltas; moved `mouseup` to `window`; tracked `pausing` promise + `chosen` flag and fixed `(f & 3) !== 3`; respected `?files=server` in `opfsDir()`. |
| **SWL-01 / BUG-27 / BUG-28** | Potential 0-byte save truncation + lost registry dirty state + NULL derefs in file listings | `save_send()` didn't check `malloc`/`strdup`, risking empty `PUT`; `reg_flush()` cleared `reg_dirty` before `layer_store()`; `reg_load()` dropped last line without `\n` and left `x` uninitialized in `sscanf`; `save_index`/`gf_find`/`gf_delete` dereferenced `NULL`; `pre_main` spun forever if `RN_FILECACHE_MB < 4` | `host/files.c`, `host/win32.c` | Aborted `save_send()` and invoked `rnSaveFailed` on OOM; cleared `reg_dirty` only when `layer_store()` returns `0` (retrying after 0.5 s); handled last lines without `\n` and initialized `unsigned x = 0`; guarded all `strchr`/`save_get` against `NULL`; clamped `pre_main` cap to `>= PCHUNK`. |

---

### Wave 2 — Cloudflare R2 Deploy & Local Serve Contract (3 files)

| ID | Forensic Finding | Root Cause | File(s) Reconciled | Resolution |
|---|---|---|---|---|
| **XLY-01 / BUG-36** | `--dist` default mismatch & concurrent `.gz` piece corruption | `build_public.py` defaulted to `build/public` while `upload_r2.py` expected `build/public-cdn`; `ThreadPoolExecutor` workers wrote `game-z/*.gz` and `data-chunks/*.gz` non-atomically | `tools/build_public.py` | Defaulted `--dist` to `build/public-cdn` when `--cdn` is set (else `build/public`); wrote `.gz` pieces to thread-unique temp files followed by `os.replace()`. |
| **XLY-02 / XLY-03 / XLY-04** | Progress bar stuck at 0% on R2 + 24 h CSS cache skew + internal `manifest.json` leaked to R2 | `upload_r2.py` omitted `X-Raw-Length` on `out/maxpayne.wasm`, classified `.css` under `assets` (`max-age=86400`), and uploaded internal `dist/manifest.json` | `tools/upload_r2.py` | Sent `X-Raw-Length` from raw `maxpayne.wasm` size; moved `.css` into `page` (`no-cache`); excluded `manifest.json` from uploads. |
| **BUG-36 / XLY-05** | `Cache-Control` leak across HTTP/1.1 keep-alive + missing `CORP` + unsafe `PUT`/Range | `serve.py` never reset `self._cache` per request, omitted `Cross-Origin-Resource-Policy: same-origin`, shared `.tmp` on concurrent `PUT /save/`, and threw `ValueError` on suffix/invalid `Range` headers | `tools/serve.py` | Reset `self._cache = None` on every method; emitted `CORP: same-origin`; used thread-unique temp files + `os.replace()` on `PUT`; handled suffix/invalid `Range` cleanly with 416. |

---

### Wave 3 — Host Graphics, Audio, Input, Win32, Threads & Lifter (13 files)

| ID | Forensic Finding | Root Cause | File(s) Reconciled | Resolution |
|---|---|---|---|---|
| **BUG-19 / BUG-20 / BUG-21** | D3D8 VB/IB refcount leaks, VRAM counter underflow, target/shader/stateblock leaks, `COLORWRITEENABLE=0` forced to `0xf` | `DrawPrimitiveUP*` zeroed bindings without releasing refs; `vram_used` never decremented and double-counted RT textures; `SetRenderTarget` skipped refcounts; `Delete*Shader`/`DeleteStateBlock` leaked host/guest memory and `objs[]` slots; `dev.rs[168] ? ... : 0xf` broke `0` | `host/d3d8.c` | Unbound via `bind(..., 0, ...)`; tracked per-object `vram` and emitted `GPU_FREE` for released RT/surfaces; refcounted `dev.rt`/`dev.ds`; freed shader/stateblock allocations and recycled slots; changed color write mask to `dev.rs[168] & 0xf`. |
| **BUG-22** | Dead `OP_TEXKILL`, VS constant OOB read, spotlight `falloff == 0` lighting outside cone | `OP_TEXKILL` tested zero-init local `t<n>` instead of `i.t<n>`; CPU interpreter clamped VS constants to 256 vs `GPU_NVC=84`; `ffp_vs` computed `exp(-inf * 0) = 1.0` | `host/sm1.c`, `host/ffp.c` | Tested interpolated texcoords in WGSL & CPU `OP_TEXKILL`; clamped VS constants to `GPU_NVC`; masked spotlight output with `0 < r9.z`. |
| **BUG-23 / HRD-09 / SWL-09** | `DuplicateSoundBuffer` UAF + redundant mono Hermite + unchecked `pthread_create`/OOM | Shared PCM flag `0x80000000u` set only on duplicate `b`, not `src`; mono ran 4-tap Hermite twice; `pthread_create` unchecked; OOM leaked `b->iface` | `host/dsound.c` | Set `src->flags |= 0x80000000u`; reused `h0` for mono `h1`; checked `pthread_create`; reset `b->iface = -1` on OOM. |
| **BUG-29** | C operator precedence bug in `BuildActionMap` + `DIGDD_PEEK` trace OOB + `Enum*` guest leaks | `&&` bound tighter than `\|` at `dinput8.c:490`; `GetDeviceData` trace subtracted `n` even under `DIGDD_PEEK`; `Enum*` never called `guest_free` | `host/dinput8.c` | Parenthesized bitwise OR chain; recorded `start_head` before loop; called `guest_free(inst)` after `Enum*` callbacks. |
| **BUG-25 / BUG-26 / BUG-30 / HRD-02 / HRD-03 / XLY-14** | `CreateFileMappingA` UAF, allocator wraparound, 7.3 s busy-spin, 16 MB thread stack leaks, timer ID aliasing, stale named mutexes | Unrefcounted `GFile` closed under active mapping; `uint32_t` overflow in `heap_alloc`/`VirtualAlloc_`; `while (Date.now() < end)` burned 100% CPU; threads never detached and allocated 16 MB stacks on 640 MB phones; `timeKillEvent_` reused `timer_id`; `Sleep_` omitted `idle_main_ms`; `CloseHandle_` left dead mutexes in `named[16]` | `host/win32.c`, `host/files.c`, `host/threads.c`, `host/host.h` | Added `GFile.refs` + `gf_retain()`; fixed `uint32_t` overflow checks, `TlsFree_` bounds, and `RegDeleteKeyA_` overlap; zeroed shrunk region in `gf_truncate()`; replaced busy-spin with `Atomics.wait`; detached threads + 4 MB stack under `RN_LOWMEM`; monotonic `timer_id`; counted main-thread `Sleep_` in `idle_main_ms`; added `threads_close_handle()`. |
| **BUG-32 / BUG-33 / BUG-34 / BUG-35 / SWL-03** | Lifter `idiv`/`fscale`/`bt*` 16-bit UB traps, broken build deps, lost export aliases, unchecked `ESP`/pointers | `INT64_MIN / -1` and `fscale` NaN/Inf trapped Wasm; 16-bit `bt*` used 32-bit stride; `build.py` ignored `host.h`/`images.h`/`funcs.h` mtimes and exited 0 when `brotli` was missing; `images.py` collapsed VA aliases and broke `#ordinal` forwarders; `fast.c`/`dispatch.c` lacked `guest_ok` and leaked `busy[64]` | `tools/recomp/lift.py`, `tools/recomp/build.py`, `tools/recomp/images.py`, `host/fast.c`, `host/dispatch.c` | Guarded `idiv`, `fscale`, and 16-bit `bt*` in `lift.py`; added full header mtime checks, regex chunk cleanup, and hard error on missing `brotli`; preserved all export aliases and ordinal forwarders; added `guest_ok()` bounds, 64-bit offsets, `malloc` checks, and `busy[64]` slot reuse. |

---

### Wave 4 — State Machines, Launcher Polish & Orphan Cleanup (14 files)

| ID | Forensic Finding | File(s) Reconciled | Resolution |
|---|---|---|---|
| **ISM-01 / XLY-11 / XLY-16** | Scattered boot/backdrop/play flags, `F2` leaking to game, residual Spanish UI strings | `web/index.html`, `web/telemetry.js`, `web/ffwd.js`, `web/gpu.js`, `web/opfs.js` | Unified lifecycle under explicit `appPhase` + single writer `setPhase()`; added `e.stopPropagation()` on `F2`; translated remaining user-facing Spanish strings to English. |
| **XLY-08 / BUG-13 / BUG-16** | Hardcoded `0xc0000000` guest limit in telemetry, stuck 4× turbo on blur, broken multi-char typeahead | `web/telemetry.js`, `web/ffwd.js`, `web/launcher.js` | Derived `guestLimit` from `RN_GUEST_MB`; cleared `holdTimer`/`turboOn(false)` on `window` blur; fixed multi-char typeahead loop. |
| **BUG-04 / BUG-16** | Overlapping `duck()` lowering music forever, `resume(offset)` ignored when loaded, WebGL context & thumbnail `blob:` leaks | `web/launcher-audio.js`, `web/launcher-tear.js`, `web/launcher-options.js` | Tracked canonical `busBase` and cancelled prior `duckTimer`; applied `currentTime` immediately when `readyState >= 1`; disconnected synth nodes; called `WEBGL_lose_context` in `MPTear`; revoked previous save thumbnail `blob:` URLs on refresh. |
| **ISM-04 / BUG-31 / XLY-08 / XLY-17** | Desynced `F3`/`F6` cheat toggles, `gamestate_frame` clobbering `pub.nav_req`, `user32.c` dead windows & `GetKeyState_` bits, orphan NFSU1 files | `host/cheats.c`, `host/gamestate.c`, `host/widescreen.c`, `host/user32.c`, `host/host.h`, `tools/recomp/build.py`, deleted `host/bridge.c` & `host/net.c` | Read live player bits in `cheats.c`; validated pointers in `sys_log()` and split `memcpy` around `pub.nav_req`; guarded `h == 0` in `widescreen.c`; fixed `FindWindowA_`, `CreateWindowExA_` frees/dead flag, and `GetKeyState_` (`0xffff8000u`); updated stale docs; deleted orphan `bridge.c` and `net.c`. |

---

## Verification Summary

- **JS & HTML:** All 12 modified `web/*.js` files + 2 inline `<script>` blocks in `web/index.html` passed strict `vm.Script` syntax checks.
- **Python tools:** `build_public.py`, `upload_r2.py`, `serve.py`, `lift.py`, `build.py`, `images.py` passed `py_compile` and contract assertions.
- **Host C (native + Wasm):** All 14 modified `host/*.c` files passed `cc -fsyntax-only` and `emcc -fsyntax-only -pthread` with zero new warnings.
- **Lifter Oracle (`difftest.py`):** 2,000 functions / 7,940 cases compared against Unicorn — **`gate: "pass"`, 0 crashes**.
