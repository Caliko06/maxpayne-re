# Web: more browsers and devices (research + decisions, 2026-11-03)
Prior art: nfsu2-web (github.com/th0mk/nfsu2-web, MIT): the same kind of port, plays on Chrome Android and iOS 26. Read web/src/host/memory.ts, app/touch.ts, app/device.ts, app/phonegraphics.ts, docs/web/PLAYING.md.

## Facts from sources
- WebGPU (gpuweb wiki Implementation-Status, edited 2026-10-02): Chrome/Edge desktop 113; Android 12+ ARM/Qualcomm/Intel 121; Safari 26 on macOS, iOS, iPadOS; Firefox Windows 141, macOS 145/147; Firefox Linux Nightly only; Firefox Android behind a flag.
- Compatibility mode: requestAdapter({featureLevel:'compatibility'}), Chrome 146, OpenGL ES 3.1 (developer.chrome.com/blog/new-in-webgpu-146). Direct3D 11 is only being explored.
- SharedArrayBuffer and WebGPU need a secure context. A phone on http://192.168... gets neither. tailscale serve gives https inside the tailnet only (tailscale.com/kb/1312/serve).
- Memory (nfsu2-web memory.ts, from engine sources): 32-bit Chrome Android (most phones under 8 GB) sets aside one piece, min(maximum, 1 GB) and at least initial; it refused a second gigabyte. Safari: one 16 GB buffer area, a shared memory needs 4 GB or its maximum in one piece.
- iOS kills a tab by device RAM and pressure, no fixed limit. Reports (single sources, weak): about 3 GB iPhone 15 Pro, 1.5 GB iPhone 12 Pro.
- wasm-split (emscripten Module-Splitting): prototype, the secondary module is not sent to pthreads. Not used.
- Touch: a keyboard mapped onto a phone is the weak spot. Max Payne Mobile: movable buttons, soft auto-aim, controller support.

## Decisions
- Done: compatibility adapter (Core stays Core), touch.js, gamepad.js, manifest + icons, wake lock, fullscreen, clear refusal on http and failed boot.
- Not now: WebGL2 backend (a second renderer for Firefox Linux/Android and iOS under 26; only if the OWNER wants those); software renderer; wasm-split; service worker.
- Next if a phone fails on memory: guest 1 GB -> 768 MB (route peak 554 MB) and INITIAL_MEMORY about 1.25 GB (host part about 415 MB, DEFAULT_PTHREAD_STACK_SIZE 16 MB x 9 is a share of it). Needs a rebuild and a whole-game route.
- Later if a phone is slow: render scale, lighter defaults (nfsu2-web 75 %), soft auto-aim, movable buttons.

## Runtime facts (2026-11-03, headless)
- Mac, 844x390 touch: tap opens Options; stick moved the player z 2.8 -> -4.6 in 1.5 s and stopped on release; drag turned the view; ESC leaves the comic (needs MIN_PRESS, a 0 ms tap was lost).
- Mac ?compat: part1_level1 plays 100 s, 0 webgpu errors. ?compat keeps a Core adapter under the compatibility rules.
- NOT VERIFIED: fire button effect; tap next page in the comic (the part1_level1 comic kept its 48.7 s, one page or no effect); any real phone; gamepad (no pad here).
- Dawn on Linux Pods: SwiftShader and --use-webgpu-adapter=opengles lose even a bare triangle ("Device was destroyed"), so real OpenGL ES is untested.

## Phone run (2026-11-04, moto g6 plus: SDM660, Adreno 5xx, 3.8 GB, LineageOS 15; phones paused by the OWNER)
- WebGPU core works in Chromium 159 on this phone. WebView (Jelly) is not cross-origin isolated, so no threads there.
- 32-bit Chromium: shared memory 896 MB ok, 1024 MB refused. 832 MB before the wasm compile kills the tab, 640 MB boots; at the level load a background thread died on a 35 MB allocation (OOM, termination status 6).
- 64-bit Chromium passes that point. Boot to menu 65-83 s, menu about 59 fps. Tutorial load: over 5 min with no file reads, game worker at 93 % CPU; never finished, so gameplay fps is UNMEASURED.
- Inference, not measured: the slow load is decoding the HD textures and resampling them in lifted code. Next if phones resume: let one load finish and read gameplay fps; if >= 30, a phone data pack with smaller textures made once on the Mac, replacing RN_MAXTEX/RN_TEXDETAIL at runtime.
- `?profile=low` on the Mac: 25 maps peak at 388 MB of guest, 24/25 green at 960x480 (part1_level1 58.8 fps), s low25.
- The "16 MB x 9" note above is wrong: pool workers take no stack until pthread_create; 3 stacks of 16 MB exist (out/bec/w1/C_host_heap.md).

## Phone layout (2026-11-19, OWNER: phone is in, default principle; out/phone1119)
- One flag: web/index.html sets html[data-device] = phone (touch, short side < 600 CSS px) | tablet (other touch) | desktop; ?device= forces it. Touch controls, the low memory profile, the 75 % render size and RN_NOCURSOR all read it.
- Phone: portrait shows "Turn your phone sideways" (#rotate); the launcher's landscape layout is the phone block at the end of web/launcher.css (TAP TO START, no key legend, a Back button, no "Mouse and Keys"); the HUD in web/touch.js is round see-through buttons (FIRE, BULLET TIME, JUMP, CROUCH, USE), pills on top (PILL, ZOOM, SAVE / RELOAD, PREV, NEXT, pause) and a MOVE ring.
- Headless at dpr 1 renders 640x480 (playSize keeps h >= 480), so the shots are pillarboxed; a real phone at dpr 2-3 gets 960x480 (2:1).
- NOT VERIFIED: any real phone. Load on a phone still unmeasured past the moto g6 (above).

## Light version (2026-11-20, OWNER: iPhone 15 Pro and Moto G6 Plus: endless load, low fps, broken picture or sound)
- Cause of the load: data/database is an HD pack (587 files, 1 GB, re-encoded JPEGs up to 4096). Part1_Level1: 466 Mpx of JPEG vs 3.9 Mpx
  in the retail ldb inside x_level1.ras (build/ldbjpg.py list). The game decodes them in lifted code. All 587 have a retail twin in x_*.ras.
- Fix: web/opfs.js hdPack drops data/database from a phone's listing, so the game reads the retail files; profile `phone` (index.html)
  = low memory without RN_TEXDETAIL 0; web/gpu.js paces phones at 30 fps (the guest waits on box.shown).
- Headless M4, part1_level1 load: HD + low 38 s -> phone 5 s, then 30 fps steady (out/phone1120). NOT VERIFIED: a real phone; sound.

## Downloads (2026-11-20, OWNER: faster downloads on every device and browser, AGENTS.md Goal "short loads")
- Cause: the guest reads game files with serial synchronous Range XHRs (host/files.c BLOCK 256 KB); part1_level1 cold was 740
  requests on desktop, so at 80 ms each the round trips (~59 s) cost more than the bytes (175 MB at 50 Mbit, ~28 s).
- Fix (port of RE-NFSU host/files.c pre_main, 0ab0b2d/ef0244e): the page passes the chosen level (RN_LEVELHINT, from RN_JUMP's
  gm_init in web/index.html); 4 helper threads fetch its pieces from web/level_hint.tsv (tools/level_hint.py, from a
  RN_STATS capture of tools/static_server.py) in 2 MB requests into the block cache while the engine boots. Unread prefetched
  blocks are never evicted by helpers and fill at most half the cache. Guest and helper requests are tracked in inflight[] so a
  block is not asked twice; large guest reads use the cached blocks (without that Part1_Level1.ldb came twice, 101 MB for 51).
  web_read retries a failed request after 0.3, 1, 2, 4 s (a dropped phone request hung the loading screen).
- Measured (headless Chrome, tools/static_server.py RN_RTT_MS=80 RN_MBIT=50, cold, ?level=part1_level1, out/load1120/):
  desktop level playing 94.1 s -> 37.2 s (175 -> 180 MB); phone (?device=phone) 75.3 s -> 49.8 s (156 -> 179 MB by 70 s:
  the 32 MB phone cache still evicts part of x_data.ras before use, 100 MB for 70 unique).
- Only part1_level1 has a hint so far; other levels load as before. Safari not measured.
