# Widescreen and image polish (MaxPayne.exe 1.05)
Prior art: ThirteenAG WidescreenFixesPack, source/MaxPayne.WidescreenFix (MIT). The OWNER's install holds an old build of it
(scripts/, dsound.dll), which the port never loads. All 27 byte patterns of its dllmain.cpp match build/img/MaxPayne.exe (26 once, the cursor one twice).
FACT 0x10114d60 P_Camera::validate: tan(fov/2) to +0x200 (X); that times viewport ratio to +0x204 (Y). Disassembly and the fix agree.
FACT portal culling 0x67a12d, 0x67aeba reads the camera FOV, not the tangents: same scale there, or rooms clip to 4:3.
FACT 2D (menus, HUD, comic) stays centered 4:3 with correct proportions. Fades are 640 wide: host/widescreen.c rn_ws_sprite widens them.
Host: MID_HOOKS in tools/recomp/patches.py (a hook before one instruction; lift.py saves and reloads registers around it).
Render size: web/index.html playSize, RN_SCREEN overrides the saved registry mode. Phones 75%, at most 1920 wide; desktop at most 2560.
Polish: web/gpu.js final pass: FXAA, slight contrast and colour after the game's gamma ramp; ?classic turns it off. Anisotropy 16 (phones 8), mips always linear.
Not ported yet from the fix: HUD anchored to screen edges (e2mfc.ixx WidescreenHud), loading screen side bars (0x582a18), cutscene bars, sniper scope width.
