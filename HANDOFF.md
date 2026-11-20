# HANDOFF maxpayne (2026-11-20, scope: phone light version, "si o si, default principle"; main). Replaced at close.
DONE: AGENTS.md Goal now has phone (iPhone Safari, Android Chrome) and the device principle (f41babd). web/index.html sets
  html[data-device] phone|tablet|desktop (?device= forces); phone: rotate screen in portrait, landscape launcher block at the end of
  web/launcher.css, TAP TO START + Back button, HUD in web/touch.js (3efd715). Shots: out/phone1119/. Live on maxpayne.cc (OWNER go, page only; launcher.css purged at the edge); Safari load fix before (1ae5f20, ab50895).
HUD (OWNER chose compact cluster 2026-11-20): FIRE + 4 discs 6 px apart, weapon card, pause+tray; live 2026-11-20 (page pass, touch.js purged).
LIGHT (2026-11-20): phones skip the 1 GB HD pack (data/database) and read the retail x_*.ras textures, profile phone, 30 fps;
  headless part1_level1 load 38 s -> 5 s (out/phone1120, rea/map/web-devices.md). Live 2026-11-20 (OWNER go, page pass; index/opfs/gpu purged). NOT VERIFIED: any real phone.
Open finding: Cloudflare Browser Cache TTL gives browsers max-age=14400 on no-cache page files (docs/hosting.md); returning visitors
  may run old scripts up to 4 h after an upload. Changing it is a zone setting: OWNER's call.
DROPPED by the OWNER. Nobody builds, polishes or reopens these without the OWNER's go; every worker brief carries this block.
- Launcher 3D scene and generated art: removed in d9dd67b (2026-11-10). Logo in art2/private stays.
- #7-#10, #12-#14 ghost and RE fidelity: parked 2026-11-05. Coverage and naming passes: 2026-11-02. Claude subagents as slice leaders (2026-11-09; cdx and bec lead). Free FOV slider (look_move.md:50).
- Phone HUD as loose discs on an arc (ee94c32, b2f0c5d): OWNER 2026-11-20, too spread out, not a current game.
OWNER open: takedown address; his phone test of the light version (iPhone 15 Pro, Moto G6 Plus: load, fps, sound).
Next block (OWNER 2026-11-20): faster downloads on every device and browser, citing the AGENTS.md Goal default principle; prior art first (RE-NFSU load lanes 0ab0b2d, ef0244e), measure before/after.
