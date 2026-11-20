<!-- Becario report 2026-11-04 (DeepSeek), read by the orchestrator, no second key yet: verify an address before patching on it. -->
# D_boot: process start -> first usable menu, and one level load
Sources: rea/map/{flow,game,files,engine}.md, rea/c/*.c, host/d3d8.c, host/win32.c, x_data.ras (tools/ras.py), build/prof_l4.bin
(tools/prof.py). FACT = read in code/data. INF = inference. UNK = unknown.
## A. Boot to the menu (this path loads no level)
P0 CRT/MFC: 0x76fb04 entry -> AfxWinMain 0x7838b8 -> app 0x8a6338 (vt 0x7cb77c, ctor 0x40a1d0, timer avg +0xf1=4). No .ras.
P1 globals: 0x40b500 -> 0x40a4c0: window class, single-instance 0x40c6d0, registry "Remedy Entertainment" (:88), command line
0x40cb90 -> flags 0x8a6480..8a648d (the "Additional Parameters" profile string is re-parsed through it, :91-135), hook 0x40a410,
profiles (:171-174), setDefaultAccessMethod(2) (:176), preload flags (:178-180), D3D8 check 0x40b930, parental lock 0x405610,
cd.ini 0x40ae60 (absent, host cd only), associations 0x40af30. P2 window/subsystems: CreateEx 0xca0000, icon res 0x80,
MediaPlayer 0x409a50, startup dialog 0x405bf0, renderer 0x40d800.
P3 startup dialog 0x40b7a0: loops 0x40b5e0 (0x40b470) + 0x40b670; -nodialog clears 0x867afc, app+0xfe = 0, so the loop breaks
after one pass (0x40b7a0:28-30, host/win32.c:22-23). Host passes it. P4 engine init 0x40b1b0: FPU 53-bit/nearest, renderer
0x40d800 -> D3D8 CreateDevice, ModeSwitch 0x415840 -> 0x415dc0.
P5 modes 0x415dc0 "ModeSwitch" 0x868d5c, one pass: Console/Statistics/HUD/Game (GameMode ctor 0x44f190), Graphic novel
0x49a940, Menus (save 0x40f9e0, menu 0x586cf0, MenuMode 0x429d10 -> MS+0x7c). Parses levels.txt (x_data entry d36, 41,397 B,
28 sections) and menu.txt (entry 349). -skipstartup 0x8a6488 (getter 0x40cdc0) skips the startup modes
0x40b440/0x40e540/0x6dfb30/0x409ac0 (00415dc0.c:559-566); host passes it.
P6 "ModeSwitch flush" 0x868ce0 (00415dc0.c:1083) = P_Map::doneLoadingMaps 0x10120c10 + 0x5a0760 + 0x54b710.
P7 frames: Run 0x40bcd0 -> 0x40c020 (FPU check, R_Timer, modes update 0x601250, render 0x6014d0). First usable menu = first
frame with MenuMode items; the site that executes menu.txt is UNK (0x6313e0 is input only).
## B. One level load (New Game -> Fugitive)
L1 script: gm_setgamelevel("normal") + gm_init(<level>) via GameModeMessage_Init (vt 0x7ccbdc, slots 0x435010/0x4350d0/
0x435160; flow.md 4-5).
L2 tick 0x44c1b0 -> pending switch 0x451960 -> 0x44fda0. Only callers of 0x44fda0 are 0x451960 and 0x454230 (load game).
L3 0x44fda0 "Level init" 0x86d290: R_FileDirectoryChange to the levels.txt Directory (0044fda0.c:176-181; [startup_level]
says front_end), then Level ctor 0x493f80.
L4 0x493f80: "LevelDB" 0x875f2c reads the .ldb, "Room creation" 0x875f1c (0x65a2e0, 0x5265b0, quadtree 0x655f90), "LDB:
lights" 0x875ecc, Triggers 0x875de0, Dynamic objects 0x875dc8, Quadtree DYN 0x875db8, FSMs 0x875dac, Level items 0x875d98,
"MaxPayne_Level flush" 0x875d80 (0x5a6e90+0x5b7740, 00493f80.c:444).
L5 back in 0x44fda0: "Global AI object" 0x86d27c (0x437190), "Enemy creation" 0x86d26c, "Player creation" 0x86d25c
(0x451e70), "Preload geometry" 0x86d200 (C1), "Level init flush" 0x86d1ec (0044fda0.c:784-790).
L6 textures: material import 0x5dc800 -> 0x5dc360 -> P_Map ctor 0x1011f100 -> buildDeviceTexture 0x10120370 -> G_Image
0x10202800 -> loadJPG 0x10204200 -> libjpeg, then driver convert/upload; host rn_ldb_import uploads per material right after
decode (peak ~90 MB vs 1.25 GB, flow.md). L7 after init: 0x60dc00, timer reset 0x40bbf0, 0x60de10(0.1f), normal frames.
## C. Skip / defer, with evidence
C1 -disable3dpreloads: write 0x40cb90:43-45 -> 0x8a6486; only reader 0x40cda0 (0040cda0.c:6). Two instructions use it:
0x40a722 `call 0x5ac860` with cl = !flag sets 0x8b0755, gating the per-mesh preload inside the LDB import (005a70a0.c:857);
0x40a731 P_Map::setAutomaticPreload(!flag) (0040a4c0.c:180). 0x44fda0:774 gates "Preload geometry", which calls renderer vt
slot 13 (0x7cbb94 -> 0x40e150) = e2mfc P_Primitive::preloadAll 0x101424f0. Verdict: DEFER, not a save. The gated work is e2mfc's
own loops (0x101424f0, P_Map::preloadAll 0x101202d0): they run while free texture memory >= 0x100001 (1 MB), and the host
answers VRAM(128 MB) - used (host/d3d8.c:212, 520-523), so they stop at that budget and the work lands at first use.
d3d8 PreLoad is a no-op (host/d3d8.c:1010, 1083, 1084) but that is the driver's inner call, not the guest loop: "PreLoad is a
no-op hence preload is useless" does not hold. Host never passes the flag (host/win32.c:23). Saved: UNMEASURED.
C2 Startup level behind the menu: do not. Boot has no level path (B-L2), so Startup_level.ldb (21,994,472 B, x_data d37) is
paid on New Game; moving it before the menu adds that I/O to boot. Measured: Tutorial.ldb 14,120,563 B; Part0_Level1.ldb
8,864,003 stored / 15,711,160 raw. "47 MB front_end ldb" and "36_levels.txt" not reproduced: levels.txt is entry d36 (41,397 B,
28 sections); no 36_levels.txt in any .ras.
C3 Flushes repeat: boot 00415dc0.c:1083, Level ctor 00493f80.c:444, level init 0044fda0.c:784. Three passes on boot->level 1,
only the last after all imports. Dropping the boot pass is legal only if nothing is pending (INF: no LDB imported yet). Cost: UNMEASURED.
C4 profile/verbose: -profile 0x8a6482 (0x40cd70), -developer 0x8a6483 (0x40cd80), -dumpfactories 0x8a648d (0x40ce10). Host
passes neither, so R_Profile::disableProfiling (0040a4c0.c:171) while sections still construct: 6 in 0x44fda0, 10 in 0x493f80.
0x44fba0:29-42 already skips a 7-call block when developer/dumpfactories are off, and that block calls 0x735570 and 0x65a1c0
twice in a row (0044fba0.c:36-43). Cost: UNMEASURED.
C5 Parse/alloc: levels.txt parsed at boot (0x415dc0) and the Directory resolved again per load (0044fda0.c:176), 41 KB,
UNMEASURED; 0x44fda0 re-creates its 0x2000 R_MemoryFile every level init (0044fda0.c:118-152).
## D. Cost status
Measured: native New Game->level 50 s -> 27 s -> ~10 s; web boot gap 1-9 s, comic 16-23 s, level gap 106-118 s (~12 s) at
738c991; JPEG ~4 s native (733 images, heap 328->1170 MB), 1.7-1.9 s web (flow.md). out/prof_l4.log does not measure these
phases: it is the software-raster bench (prof.py [30,85) s: gpu_submit 41.3%, exec 19.3%, src_expr 14.7%, png 6.6%,
sample_level 4.5%; [1,13) s shows no load symbol). Any "skip X" saving is UNMEASURED.
