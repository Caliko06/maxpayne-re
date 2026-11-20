# Ending credits to main menu (#24, 2026-11-09)

## Static evidence (FACT)
Original `build/data/lvl/1_Part3_Level7.ldb`, 25,749,889 bytes. Offsets below are file bytes, not CPU addresses.
The original data already implements the complete exit; no game/host patch is justified by the old 900-second native observation alone.

| Camera_01 animation | Length field byte | Float seconds |
|---|---:|---:|
| 0 | 24013789 | 1 |
| 1 | 24014383 | 4 |
| 2 | 24014815 | 218 |

Each field follows `0d 14 01 <ASCII animation ID> 09 <little-endian float>`.
Animation 0 resets global speed to 1 (command begins byte 24014058).
Animation 1 sends `GM_SendEndOfGameMessages` (24014491) and starts music; neither is a completion condition.
Animation 2's ending list contains `x_modeswitch->s_modeswitch(menu);` starting byte 24014929, followed by restarting animation 2.
The 223 seconds are simulation time for the three animations, not a wall-clock deadline.
Prior `out/goal/bec/f13_final.md` gave byte 24014943 inside the menu command, not its start.

CPU evidence: `rea/c/MaxPayne.exe/006f34c0.c` increments DO+0x49b by getter 0x5e53d0 (time object +4), compares elapsed time with animation length getter 0x5ff0f0, and dispatches the ending list through 0x751d90 -> 0x6e2c00 -> 0x51b6a0.
`006020c0.c` resolves the requested mode and calls 0x601b40. `00601250.c` advances its fade states and activates the selected mode. No keyboard, movie, or music completion event gates this path.

## Previous observation (INFERENCE)
`out/goal/p0/native9.log` near its end advances about 2.5-3 frames per wall second, with sdt/rdt 0.1 per frame.
At that rate 218 animation seconds alone need roughly 727-872 wall seconds after the camera starts. A run capped at 900 seconds from boot cannot establish a freeze.
A community ending walkthrough also describes the credits completing, but supplies no precise timer: https://gamefaqs.gamespot.com/pc/913964-max-payne/faqs/13621 . Original data is the timing authority.

## Reproduction
`node tools/ghost/ghost.mjs --port 5191 --secs 780 --plan final --out out/final_1109`
The final plan reuses `MPFF.run('part3_level7', 10)`, then keeps observing after the autopilot promise resolves; only actual steady mode 1 after entering game mode, reaching route step 10, and resolving the autopilot promise passes. No direct credits/menu message is sent by this plan.
The existing route is assisted (teleports, invulnerability, scripted enemy/cable kills), so this is ending-chain evidence, not an unassisted boss fight.

## Web result (FACT)
`out/final_1109/report.json`: `result=reached`, `stop=""`, end mode `menu`, 610.0 wall seconds from browser boot.
`run.log` / `state.tsv`: final graphic novel entered at 309.2 s, exited naturally at 386.7 s; steady main menu observed at 609.9 s. Difference **223.2 wall seconds**, matching the original 223 simulation seconds including fade/sampling rounding.
`page.log`: **0 STOP**, 0 RuntimeError/wasm abort/pageerror. Captures inspected: `boss.png`, `credits.png`, `main_menu.png` (aliases of numbered original shots). No credits/menu trigger or keyboard input was sent after the route ended.
Build: `RN_BR=0 RN_JOBS=2 .venv/bin/python tools/recomp/build.py web`, successful (`build.log`). Fresh checkout mtime on unchanged `host/rt.h` initially invalidated cloned objects; restoring that file's mtime to its last Git revision reused the unchanged caches.
The earlier browser attempt had no built `web/out` and failed before entering the game; retained as `missing_build.log`, separate from the successful zero-STOP ending run.
No host/game patch: the missing menu was an observation cutoff, not a demonstrated host blocker.
Not verified: Safari, unassisted boss combat, audible sound quality, full start-to-end gameplay by the OWNER. WORKLIST #24 as a whole remains open.
