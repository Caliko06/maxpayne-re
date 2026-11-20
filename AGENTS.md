# Max Payne in Chrome and Safari

Canary: start every reply to the OWNER with `[mp]`. Talk to the OWNER in Spanish (neutral Mexican, tú); files in simple English.

## Goal
Anyone opens a link and plays the OWNER's own Max Payne (PC 2001, v1.05) complete (Prologue, Parts I-III, final boss,
credits) in Chrome and Safari, desktop and phone (iPhone Safari, Android Chrome), in English: 50 fps without stutter,
short loads, story and comics in order, enemies and their AI, weapons, bullet time, kill and death cams, sound/music/voices, save and load (also after a page reload), menus
and cheats in the game's own menu. Game data hosted by us, same shape as nfsu.cc (docs/hosting.md). The native build and
:5181 are the bench and the oracle; the public address is the product. Spanish last, only if the OWNER provides the files.
The page detects the device: on a phone the launcher and the game are a mobile game's layout (landscape, fullscreen,
big touch controls), and every UI change is made for both (OWNER 2026-11-19, default principle). A phone always plays the
light version: the original 2001 textures (no HD pack), low memory, 30 fps (OWNER 2026-11-20, default principle).
Out: Firefox, LAN play, installer, mods. Nothing is uploaded or published without the OWNER's explicit go for that
upload (direction reopened 2026-11-19).
MaxPayne.exe and its DLLs are statically recompiled x86 -> C (tools/recomp) -> WebAssembly; host/ and web/ stand in for
Windows/D3D8/DInput/DSound.
Truth = the original binaries and the OWNER's game data. Unicorn, djpeg, gdb are oracles; they never ship.
Current state, dropped directions and the next patch: HANDOFF.md. The OWNER's open items: WORKLIST.md.

## How we work
- Product first: a stretch ends in a patch the OWNER can play on :5181 (or, after a go, on the public address). The OWNER
  judges by playing; a run driven by teleports, god mode or `send` lines shows that a map loads, not that it can be played.
- RE serves the fix in hand and lands with it in rea/map/<subsystem>.md (addresses + decisions). No coverage or naming passes.
- A change to game code or data is one entry in tools/recomp/patches.py with its why, backed by two independent sources
  (Ghidra + capstone/difftrace, or REA + a runtime oracle). Host and web code need one. Delete the path a new one replaces.
- Workers are Codex (`cdx`) and Becarios (`bec`), each in ~/maxpayne-re-wt/<slice> (branch mp-<slice>); the orchestrator
  decides what to delegate, merges to main, and reads the cited line before accepting a finding. Briefs carry HANDOFF's DROPPED block.
- HANDOFF.md stays <=15 lines and is replaced when a window closes; "Next" names a patch.
- Dev web builds use RN_BR=0 (brotli q11 is 196 s of a web build); RN_BR=11 for the build the OWNER plays or any public
  upload. Bound every run (RN_SECS) and its output (`head -c`). Runs are not deterministic: compare by events, not by second.
- Safari: the orchestrator may open a test page (`open -a Safari http://127.0.0.1:5186/...`, test port, empty save dir) and
  read build/telemetry.ndjson (copy it to out/safari_<date>/ before the next run overwrites it). Workers do not open Safari.

## Publicar
- Roadmap = web/plan/plan.json step 4 (Público); next task = its first row not VERIFIED. One go covers one upload: the
  OWNER says the address and what goes up, and nothing pushes or opens a public port before that.
- Shape: static files plus response headers on Cloudflare R2, same as nfsu.cc (docs/hosting.md). The page needs
  COOP/COEP (shared memory) and https; a static host without the three headers does not boot.
- The game tree goes up at `game/`, read with Range; the visitor's saves stay in their browser (web/opfs.js). Never upload
  `save/` or anything outside the package.
- Before: RN_BR=11 for that build, `tools/build_public.py --cdn`, `tools/upload_r2.py <bucket> --dry-run`, freshness check.
  After: the four checks in docs/hosting.md, then one level played.
- Legal face: web/notice.html (fan project, not affiliated, takedown answer) is linked from the launcher foot and the boot
  screen and must be in every upload. The takedown address is the OWNER's to give.

## Which check can fail
Run the first row the change can break, once (budget: global AGENTS section 4).
| Change | Check |
|---|---|
| docs, tsv, comments, RN_* logging, web text/CSS | none |
| host/ or web/ logic away from boot | build + the one map or path it touches, <=90 s, read the log |
| lift.py, rt.h, fast.c, patches.py semantics | difftest on the affected functions before any game run |
| boot, menus, memory profile, wasm flags, gpu.js/input.js/files.c init | webgate 120 s on a test port |
| allocator, D3D8 object lifetime, guest memory layout | one chain of 25 maps in one session with RN_HEAPPEAK |
| only Safari can break it (AudioWorklet, input.js, gpu.js, br/gzip) | Safari once, at stretch close |

## Commands
- Build: `.venv/bin/python tools/recomp/build.py native` (bench) or `... build.py web` (product, needs emsdk).
  Run tools/recomp/images.py and frontend.py first.
- Serve: `python3 tools/serve.py <game dir> 5181`. Web gate: `node tools/webgate.mjs http://127.0.0.1:5186/ 120 build/webgate.png`
  on a test port with an empty ~/Games/save-<port> (5181 writes the OWNER's saves and registry).
- One function vs Unicorn: `RN_FUNCS=<addr,..> tools/recomp/difftest.py` (RN_JOBS, RN_SECS bound it); every MMX form: tools/recomp/difftest_mmx.py.
- One real call vs Unicorn: host/difftrace.c + tools/dtlink.sh + tools/dtcmp.py. Trace the callee's AND the caller's images
  (RN_LIFT_TRACE), else the return is missed and the memory compare is void.
- Stall or slow phase: RN_SAMPLE=<s> (guest stacks over time). Out of memory: the heap report, then RN_BIGALLOC.
- Data suspected wrong: RN_DUMPCALL buffer vs a strict oracle (djpeg, never PIL).

## Map
| Need | Where |
|---|---|
| Lifter x86 -> C | tools/recomp/lift.py (mmx(), x87()), build.py, images.py, frontend.py |
| Guest CPU state, x87/MMX helpers | host/rt.h |
| Host Win32/D3D8/DInput/DSound/files/GPU | host/*.c listed in build.py HOST |
| Guest memory layout | host/host.h (web: the page builds the memory, 1920 MB desktop, web/index.html + build.py) |
| Guest heap + failure report | host/win32.c heap_alloc |
| Whole-function oracle | tools/recomp/difftest.py -> census/difftest.json |
| Function names (Hopper drops edits on close) | rea/map/names/<image>.tsv |
| Ghidra corpus / censuses | rea/c/ ; census/ (exports.tsv at the unrebased base: +0x100000 for e2mfc, etc.) |
| REA | tools/rea/q.mjs (batch), env.sh; Ghidra for everything; Hopper for the DLLs only (on MaxPayne.exe it finds 7 procedures, so the second source there is capstone or a runtime oracle) |
| Binaries | build/img/ (MaxPayne.exe md5 cda1650af370e50f12e500831a834783) |
| Game install | the OWNER's own copy, read-only (old path ~/Games/maxpayne/... is gone: ask) |
| Public package / hosting | tools/build_public.py --cdn, tools/upload_r2.py, docs/hosting.md, web/notice.html |

## Code
- Match the surrounding code. Short comments say why. Every value from the original carries its address (/* 0xADDR */).
- Debug output behind RN_* env; default runs print only errors. Bound every per-frame loop.
- Host code standing in for the original only for a real blocker, marked `/* BRIDGE: what, addr */`.
- State with 3+ stages gets one named field (enum + switch) with one writer; fix it when a bug touches it, not as a sweep.

## Hard limits
- Publish only with the OWNER's go for that upload (he names the address and what goes up); no push, no public port, no
  bucket write before it.
- Chrome is ALWAYS headless and muted, never a window or Dock icon (OWNER, mandatory): only through tools/webgate.mjs.
  Native runs off-screen. Kill only PIDs you started, never pkill by name.
- Ports: ours 5181 (web, the OWNER's saves) and 8767 (dashboard). Other projects' ports and paths belong to them.
- Game files and assets stay gitignored.
  Becarios may read everything, game files included (OWNER 2026-11-03).
