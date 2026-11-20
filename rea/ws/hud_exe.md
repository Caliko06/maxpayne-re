# 2D HUD / bars anchors (MaxPayne.exe 1.05). Image: build/img/MaxPayne.exe only.
1. FACT No HUD layout table and no 640x480 constant in the exe: positions are data. hud.txt (str 0x878f74, ptr 0x8786e8) is read by
   FUN_004b0680 (MaxPayne_HUD ctor), which locks each block by name and stores the element at HUD+0x188... Names are literals in .data:
   Health 0x878ee4, SlowMotion 0x878884, PrintTip 0x878eec, Objectives 0x878ec4, Print 0x878ef8, Slot0 0x878870, TimedMode 0x878eb8,
   Painkiller 0x878760, Overlay 0x87877c, ActiveWeapon 0x87876c.
2. FACT The values are 640x480 virtual units (P_Sprite +0x188/+0x18C, P_Text +0x16C); the exe only stores them. The mapping and the
   centered 4:3 area live in e2mfc P_Sprite/P_Text::executeAlways, the site host/widescreen.c rn_ws_sprite already hooks.
3. FACT MaxPayne_HUDMode::render 0x4af900; hide gate 0x4af907..0x4af914 `mov al,[GameMode+0xEE]` (0x44ec50) then `je 0x4afb47` = native.
4. FACT Loading screen 0x5828c0 (X_ProgressBar::updateProgressBar): two P_Sprite quads, filled 0x8afcc4 width k*progress, rest 0x8afcc8
   width k*(1-progress), k = 0x8afccc (0x8afcb8+8 or getWidth 0x5853c0), height 0x8afcd0. 0x582a18 = `mov ecx,[P_Driver]` after the draw
   call 0x5859e0, before endScene [0x7ca518] / swapScreen [0x7ca51c]. INFERENCE nothing there is 4:3: the loading image (level setting
   "loadingscreen" 0x87e2e0) covers only the centered 4:3 area, so the side strips keep the previous frame. The fix fills them black.
5. FACT Bars: 0x4afaa2 `call 0x421ac0` = GameMode+0x1260 (height multiplier), 0x4afaa7 `fsubr 1.0; fmul H; fmul 0.5` = one bar height
   (1-m)*H*0.5; same multiplier at 0x4af943, 0x4af95b, 0x4af9f0, 0x4afa4a (subtitle offset). INFERENCE 4:3 is data: GameMode+0x1230
   X_GlobalCinematicSettings; the exe draws only top/bottom bars, never side ones.
6. UNKNOWN here: sniper scope width (hud.txt "Weapons/Overlay" sprites from 0x4b0680; the pivot=100 / width=100 the fix patches is e2mfc
   executeAlways). FACT the crosshair is not 2D: X_Crosshair::updateTransform 0x6d7e60 writes a world matrix scaled by zoom 0x17d/0x16f.
7. INFERENCE anchor offset = (aspectScaleX-1)*320 virtual units (16:9: 106.67 = 240 px at 1920x1080 = fFullscreenExtension); bar width
   in px = (W-H*4/3)/2; sign: left Health/SlowMotion/PrintTip/Objectives/Painkiller, right the rest.
HOOKS
- 0x582a18 -> black (0,0,barW,H) and (W-barW,0,W,H) before endScene, barW = (W-H*4/3)/2: the loading screen then spans the screen.
- 0x4afaa2 -> multiplier 1.0 (no bars) or 1-letterbox; 0x4af943/0x4af95b/0x4af9f0/0x4afa4a -> same value, moves the subtitles.
- HUD anchor: no draw-time site in the exe; add the offset to +0x188 (sprite) / +0x16C (text) inside e2mfc executeAlways. The sign needs
  the block name, which exists only at creation: a hook at 0x4b0680 can read R_Script's current block (+4, parent +0x14) and tag it.
- Sniper scope: none here (e2mfc worker). Sources: capstone, corpus rea/c, ThirteenAG ix. REA/Hopper timed out; no run.
