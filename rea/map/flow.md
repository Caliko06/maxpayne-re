# Menu -> New Game -> level 1

## Script
x_data.ras menu.txt (entry 349):
- Tutorial: `maxpayne_gamemode->gm_ResetShootDodgeBulletTimeCounters(); gm_resetlevelitems(); gm_setgamelevel("normal");
  gm_resetperformancemeasurements(); gm_init(tutorial);`
- New Game -> headline GCID_SELECTLEVEL -> MENUID_ITEM_GAMELEVEL_NORMAL (Fugitive): same messages with `gm_init(startup_level)`.
- Nightmare/Hell items: same with their level. Levels listed in x_data levels.txt (d36); Startup_level.ldb (d37, 22 MB).

## Code
1. Menu click -> message MaxPayne_GameModeMessage_Init (vtable 0x7ccbdc, slots 0x435010, 0x4350d0, 0x435160; "GM_Init" 0x86b89c).
2. Game mode tick 0x44c1b0 -> 0x451960 (pending level switch) -> level init 0x44fda0.
3. 0x44fda0: "Level init" -> 0x493f80 MaxPayne_Level ctor (LevelDB, rooms, lights, triggers, dynamic objects, FSMs, items) ->
   "Global AI object" -> "Enemy creation" -> "Player creation" 0x451e70 (jumppoint) -> "Preload geometry" (unless
   -disable3dpreloads, virtual +0x34 of FUN_0060dbe0) -> "Level init flush" (P_Map::doneLoadingMaps, FUN_005a0760, FUN_0054b710).
4. After it: FUN_0040c440 pump messages, 0x60dc00, 0x40bbf0 timer reset, 0x60de10(0.1f), 0x516cb0, then normal frames.

## Native driving
RN_MOUSE="5000:-121:-19:0,6000:0:0:1,8000:0:0:1": counts -121,-19 from center = New Game; second click selects Fugitive
(the cursor is already on it). Tutorial is about -121,-42. Native timeline before the speed work: comic pages 8-95 s, load
95-145 s without a frame, then play.
Web driving: tools/webgate.mjs "14:click@365x405,17:click@365x405" (viewport 1280x900): New Game, then Fugitive.
Web timeline at 738c991 (seconds of the run): boot gap 1-9, click -> comic gap 16-23, prologue comic to 106, level load gap 106-118,
intro comic ("Life was good") to about 200, then Max in the house. 60 fps outside the gaps. Escape in the level opens the menu with Resume Game.

## Texture path during load (where the time goes)
LDB material X_LevelDBImportMaterial#0 FUN_005dc800 -> FUN_005dc360 ("LDB: %s") -> P_Map ctor 0x1011f100 -> construct 0x1011f650.
Upload: exe 0x554aa7 -> 0x1000115f -> e2mfc FUN_10120370 -> G_Image::construct 0x10202800 -> loadBuffer 0x10202db0 -> loadJPG 0x10204200
-> libjpeg 6a (jpeg_CreateDecompress FUN_10205970(version 61), jpeg_read_header FUN_10205a50, jpeg_start_decompress FUN_10205dd0,
jpeg_read_scanlines FUN_10205f70, ISLOW IDCT 0x10210920). Then driver FUN_1042e200 -> FUN_1042e7c0: level 0 by FUN_104335c6
(D3DXLoadSurfaceFromMemory, src A8R8G8B8 0x15, filter NONE) -> FUN_1043be51 dispatcher -> FUN_1043a907 same-size float convert when
formats differ; mips by FUN_10433ec5 (D3DXFilterTexture, BOX; levels through GetSurfaceLevel +0x3c. FUN_10433ff6 is D3DXFilterCubeTexture: 6 faces, filter |= 0x70000) -> FUN_10433c73 per level -> FUN_1043acb4 integer 2x2 fast path.

## Load speed (native, New Game -> Fugitive, no frames between the first and last texture)
50 s -> 27 s -> ~10 s. Causes, in order of cost, all fixed in tools/recomp/patches.py HOST_FUNCS + host/fast.c:
- libjpeg 6a in grphmfc uses the float IDCT 0x10210920 on software x87, 64 _ftol calls per block through rn_call (~50% with those calls).
- FUN_1043a907: A8R8G8B8 -> X8R8G8B8 same-size level 0 through floats per pixel (0x10440268 + 0x1043f137) (~35% before the IDCT work).
- ycc_rgb_convert 0x1020d570, row copy 0x102045e0.
- Lifter: c->mem and c->memsz in locals of each function (a guest store may alias Ctx, they were reloaded per access).
Check: RN_FASTCHECK=1 runs host and lifted on the same call and compares. Convert, ycc, row: 0 differ. IDCT: 10 of 17.6 M blocks differ, all at
x87 precision 24-bit (likely the lifted x87: double then float rounds twice; the host float op rounds once, as the FPU does).
RN_FASTCHECK compares only the output pixels (and the convert's saved FPU word): not registers, flags, x87, stack or other writes. The lifted
IDCT calls rn_ftol too, so _ftol has no independent check here; its flags and result were read from 0x10801f14. rn_d3dx_convert skips the original's
operator new/delete of one row, so the guest allocation counters and heap layout differ from the original there. Overlapping ranges fall back to lifted.
Remaining in the load: 0x10120e20 grey -> alpha, 0x10120ee0 scaling (e2mfc), 0x10203d20 and loadJPG row fill loops.

## Translation check (difftest over the whole binary)
Every lifted function against Unicorn, 4 random cases each. Numbers are functions; `RN_JOBS=7 RN_SECS=3000 tools/recomp/difftest.py`, about 5 min plus the compile.
| | before | after |
|---|---|---|
| lifted | 68221 | 68234 |
| compared | 25120 | 67999 |
| never compared | 43101 | 235 |
| mismatch | 44 | 12, all explained below |
- The old sweep discarded every case that left mapped memory: 42973 functions were never compared, 22009 of them stopped at `fs:[0]` because Unicorn had no TEB, the rest at a pointer read from random data. Now both CPUs get a TEB (flat GDT in Unicorn), half of the random data holds pointers, and a case that leaves mapped memory or jumps out of the images is compared at that stop. The lifted side stops at the same access (rn_ref_unmapped, test build only).
- Limit: most cases check only the path up to a stop, not the whole function. Of 270479 compared cases, 26% reach the ret, 53% stop at a bad pointer, 21% at a call into an import, 1% at a jump out of the images or a fault. Inputs are random, not game values, so a passing function is not proof of the paths it never took.
- Never compared, 235: 99 reach MMX/SSE/3DNow code the CPU we present never runs, 93 are not functions (ret goes elsewhere), 39 time out at 200000 instructions, 4 only have cases the reference cannot judge.
Lifter faults fixed
- rep movsd bulk path: `rn_sz-ecx*4` wrapped for a large ecx and memcpy left guest memory. All 6 host crashes of the sweep were this.
- msvcrt sinh, cosh, tanh, exp: 8 handlers in the unaligned records 0x1083aeaa, 0x1083aeca, 0x1083aeea, 0x1083af2e were not lifted, so every regular argument halted. Only MSVCP60 imports one of them (exp); the exe imports none. LIFT_SEEDS.
- x87 rounding control on arithmetic and float stores: D3DX runs its pixel loops in chop mode (FUN_1043ecc6 fcw |= 0xc00, 23 callers); only fist honored it. FADD/FSUB/FMUL/FDIV/FSQRT and fst32 move the nearest result by one ulp from its exact error, since WebAssembly has no rounding mode.
- xchg [eax],al addressed through the new al. No compiler emits it; it was found in a junk entry.
- push, call, leave moved esp before the memory check, so esp was off at a fault.
Not lifter faults
- 245 x87 sites the reference cannot judge: QEMU 5 computes fyl2x, f2xm1, fsin and the like through host doubles, and fld m80 and fldpi-style constants hold 64 mantissa bits st[] rounds at load. A mismatch in a case that ran one counts apart: 65 cases in 42 functions. These are not verified either way, except fprem on 0x5611d0 (x87 mismatches, below).
- 12 in difftest KNOWN, listed by function, so a new mismatch in one of these would not fail the gate.
  - 4 x87 model limits, inference: st[] is a double. msvcrt _hypot 0x1082a15c differs by 1 ulp and st1 shows 64 mantissa bits (the fldcw that sets it was not traced). msvcp60 complex power 0x10920d1f, 0x10922f1d, 0x10922fb4 differs only in the sign of a zero; 7 st0 values fell below the double range in Unicorn, but the failing rows were not tied to them.
  - 4 seeds from data that are not code: 0x430001, 0x6b0063, 0x740073, 0x7a00bf.
  - 4 calls by random input to a non-entry address: 0x6f85e6 and 0x10429fd2 inside an instruction, 0x8b41e4 in .data, 0x495816 a real instruction inside the function at 0x495800.
- The MMX tag state at a fault is not compared: QEMU enters MMX mode before the load faults.

## Open
C++ EH (rea/map/game.md "C++ exceptions")
- Fact: a real throw (bad save, bool tag at file offset 0x2406d) runs through the EH to the catch at 0x40bf37, the message box and a clean ExitProcess, on native and web.
- Not exercised at runtime: nested and collided unwinds, CONTINUE_EXECUTION.
Autosave load crash (tag 0x0e)
- Fact: a clean Prologue autosave (295,896 B) loads on native and web with no raise. readTag 0x10017700 compares one byte at the stream position.
- Inference, not proven: a torn read, fixed by daf1722 (saves read whole at open). The original file was overwritten, so it cannot be replayed.
Frame cost
- Web part0_level1 from F9, W/A/D taps, no fight: 59.9 fps, p50 16.6, p99 21.1, max 164 ms, guest 4.7-6.5 ms/frame, 0 stream laps. Not a busy scene.
- Native ~1 fps is the software rasterizer (host/gpu.c, host/sm1.c, ~90%); RN_NODRAW gives ~155 fps, 6-8 ms/frame guest, flat profile.
- The 2.1 s main-thread hold in part0_level1 did not show on F9 or New Game paths here; no file with it was found.
Load (web, F9 path)
- Order: reads, then all JPEGs decoded to the heap (native 4 s, 733 images, heap 328 -> 1170 MB), then the driver convert and upload frees them (texKb 1582 MB in all).
- The 1.7-1.9 s gap between Presents is the JPEG phase: no reads, no texKb, 4 s at web t 18.4-22.5. Fact on native (RN_SAMPLE stacks: loadJPG 0x10204476 -> jpeg_read_scanlines); same place by order on web (inference).
- It is compute, not a block: longest GIL hold there 105-110 ms, starves 0, underrun 0. Holds up to 794 ms showed only in another slice's run.
- Web load 8.57 s -> 7.68 s (one run each, reads ~1.1 s) with host/fast.c at -O3 -msimd128 and R_Memory::copy; machine noise is large, earlier runs read 9.4-10.3 s.
- rn_jpeg_huff 0x1020b370 (render, merged): the host decode_mcu does not zero blocks and has no insufficient_data check, unlike stock libjpeg; it matches the game's own decode_mcu (RN_FASTCHECK 0 of 2.975M differ). Left in the JPEG phase: host IDCT (~42 ns/block, 17.6M blocks), ycc, lifted upsampling and scanline glue.
- Left, native samples of about 7 s: idct_53 742, loadJPG 0x10204200 fill loop 0x1020437f 392, decode_mcu 0x1020b370 364, ycc 257, convert 211, 0x10439a83 190.
  The fill loop sits inside a function with setjmp and SEH, so a HOST_FUNCS entry cannot replace it.
- Peak guest heap, fixed: exe 0x5dc360 decoded every JPEG into its P_Map (+0x2d) and pixels were freed only at upload, getMap 0x5dc6f0 -> validate 0x1011fed0 -> FUN_10120370. Same guest code as the original; the install's HD maps make it 1.25 GB. rn_ldb_import calls getMap right after each import: sampled peak under ~90 MB on native. Not checked: other levels, New Game path.
- longjmp, closed: only the driver imports it (0x1043c642 JPEG error exit, 0x1043d026 and 0x10444873 libpng). msvcrt 0x1081e40e reaches RtlUnwind only when the frame changed, and jumps to a setjmp return that was no lifted entry. rn_longjmp sets c->uw; patches.py LIFT_SEEDS adds 0x1043c6d2, 0x1043d0a3, 0x10444939. grphmfc's error exit 0x102044f0 throws a C++ exception, so a corrupt JPEG in loadJPG goes through the EH.
- longjmp test (native, one temporary hook, not committed): FUN_1043c68e on 64 bytes of 0x41 -> longjmp to 0x1043c6d2 -> eax 0x80004005, esp restored, no halt, game kept running. Only the JPEG path ran; PNG seeds are unrun, and not run on web.
- Combat on web costs no more than walking (one ghost run, prologue, 3 junkies, Beretta, machine loaded by other slices; no bullet time checked): fight 2104 frames p50 16.6 p99 20.3 max 53.3, guest hold p50 6.9 p99 14.9; walking before it 2439 frames p50 16.6 p99 22.7, hold p50 6.8 p99 15.8. Starves 0, underruns 0. Slowest frames at the first shots hold 20 ms and compile 1-2 shaders; the 53 ms frame holds only 12.8 ms, so it is outside the guest. No hot path to cut.
Debug
- RN_HOLD=<ms>: logs main-thread GIL holds over that length with the guest stack; its API count spans the time since the last logged hold, so it is not per hold.
- Headless page logs go to build/webcheck.log, not build/web.log. Env on the web: ?env=RN_X=1,RN_Y=2. RN_SAMPLE is native only.
- tools/prof.py names samples outside the binary as the last symbol (_f_10a081d8); /usr/bin/sample shows part of them are _sigtramp, the profiler itself.
x87 mismatches: closed, no lifter change
- 0x452b60, 0x57e5f0: difftest false positive. A popped x87 -inf has exponent 0xffff, which the compare took for an MMX write. It now checks only valid slots.
- 0x5611d0: Unicorn wrong. fprem on 0x75d2b8eb and 0xe (denormal), exponent difference 254: QEMU 5 gives 0, exact big-integer remainder 12 * 2^-149 = the lifted value. Counted now with the sites the reference cannot judge.
- Synthetic inputs from the difftest, not values seen in the game.
