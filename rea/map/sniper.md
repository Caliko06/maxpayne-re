# Sniper bullet camera, 2026-11-09

Scope: WORKLIST #6, bullet camera only. Runtime evidence: `out/sniper_1109/`.

## Original contract

- FACT: `x_data.ras`, `63_bullet_sniper.txt`, `[Sniper]` sends
  `GM_ChangeGlobalGameSpeed(0.01,0)` and `this->CAM_AnimateParented(bullet_behind)`.
  `4_camerapaths.txt` selects `cinematic/bullet_behind_static_loop.kf2`.
- FACT: path frame 0 hides HUD in 0.1 s, disables player controls, sets FOV multiplier
  1 and global speed 0.01, and plays `camera/shot_01`. Frame 20 blends speed to
  0.4 over 1 s; frame 23 blends FOV to 1.4 over 0.4 s. Exit restores controls,
  HUD (0.1 s), global speed 1 and FOV multiplier 1 immediately.
- FACT: this path has no `GM_EnableWidescreen` message. Black letterbox bars are
  not part of the vanilla sniper path. Other cinematic paths request them explicitly.
- FACT: `0x69c6c0` checks character `+0x758` (sniper zoom mode) and the shot
  solver's `+0x17c` (`0x6d8810`) before forwarding the sniper flag through
  `0x743fc0`. `0x6d8be0` sets `+0x17c` after its character ray query
  (`0x698130`) succeeds. Equip and fire without zoom is insufficient evidence.
- FACT: global speed handler `0x456150` writes GM+0x1168; tick `0x44c1b0`
  combines it and `0x44ea00` writes the timer scale and listener pitches.
  Its 3D pitch formula is `0.15 + 0.85 * scale`, hence 0.1585 at scale 0.01,
  subject to sound-specific pitch and sndmfc's 6000 Hz lower clamp (`0x1030ae20`).
- FACT: projectile camera handler `0x7082c0` forwards to camera implementation
  `0x672840`; player camera `0x504130` reads the animated path. These anchors
  are retained in `gameplay.md`; the shot activation and speed code above were
  read directly in `rea/c/MaxPayne.exe/` for this task.

## Prior art and implementation choice

Read ThirteenAG's existing Max Payne fix before proposing a replacement:
https://github.com/ThirteenAG/WidescreenFixesPack/tree/master/source/MaxPayne.FusionFix
(`Camera.ixx`, `Cinematic.ixx`; copies in the evidence directory).
It derives borders from the game's cinematic height state and preserves the
camera's own FOV. Keep the original scripts and camera implementation; a second
host camera would duplicate game logic and override its exit behavior. This is
an implementation choice, not a claim that the existing camera has passed parity.

`tools/ghost/ghost.mjs --camera-frames N` records speed, FOV multiplier, control
bits and clip with bounded screenshots in watch mode (at most 180). Values come
from the existing seqlocked gamestate, not a second simulation.

## Step 1 runtime result: incomplete, no game patch justified

- FACT: web build succeeded with `RN_BR=0 RN_JOBS=2`; `out/sniper_1109/build.log`.
  Two 90-second runs used port 5193, headless and muted. No third run was made.
- FACT: `before/` never fired: the first click confirmed weapon selection after Q
  had already been pressed with fists equipped. `after/` confirmed selection first,
  held Q for 90 script ticks, then fired (clip 5 -> 4). Its 180 captures show speed 1,
  scripted FOV multiplier 1, control bits 1, and the ordinary player camera.
  `after/page.log` shows zoom state 0 and zoom factor 1 throughout this attempt.
- FACT: Q is the original default zoom binding (`0x4cb2c9` writes action
  `0x8a960d` via `0x4cdc00`, confirmed by disassembly). The zoom state machine
  calls the embedded GM input interface at GM+0x258 slot +0x50 (`0x44d375`),
  not necessarily the separate MaxPayne_PlayerInput vtable. Do not substitute
  the latter's use-key wrapper based on its matching slot number.
- UNKNOWN: why this held Q did not produce zoom. Resolve the embedded input
  dispatch and gating before changing camera behavior. A successful character
  ray query (`0x6d8be0`) is also required; these runs did not establish one.
- FACT: SetFrequency was logged (171 / 200 lines), but no sniper-speed interval
  occurred. Neither sniper pitch-down nor exit/control return was verified.
  Movement after the mark named `control_return` is only ordinary movement.
- Evidence: `out/sniper_1109/evidence.json`, `before/page.log`, `after/page.log`,
  `after/07_camera_f3039.jpg` and subsequent captures. The two exact launch
  scripts are `run.sh` and `run-after.sh`. Raw evidence remains local and ignored.

## Step 2: input resolved, qualifying camera captured

- Correction: the preceding Q-blocker premise was false. `0x44b6db` constructs
  MaxPayne_PlayerInput at GM+0x258. Its vtable `0x7cf974` slot +0x50 is
  `0x4ceaf0 -> 0x4cc600 -> 0x4ce0a0`, reading held Use action `0x8a937d`.
  `0x519a10 -> 0x5184c0` reads the DIK state byte. Default E or Enter works;
  the Q defaults for action `0x8a960d` have no reader in this zoom path.
  Full chain, byte matches and prior art: input.md, out/sniper_1109/input-re/dispatch.asm.
- FACT: the first additional 90 s run (`run3/`) held E for 30 ticks. Zoom entered
  state 1, then 2 after release; factor 0.465 and C+0x758=1. Aim offset 1.0
  pointed above the foe (visible ceiling in `run3/07_camera_f3125.jpg`), so no cam.
- FACT: the second and final 90 s run (`run3/hit/`) used E and aim offset 0.0,
  leveling the shot between equal character anchors. Clip 5 -> 4 at frame 3107.
  Speed 0.01 at frames 3107..3142; control 0 from frame 3111. Bullet-follow
  images show the original projectile flying into `::teleport::e1`.
  Frame 3143: target hp 6.50 -> 0.00, distance 3.6 m, alive count 25 -> 24.
  Frame 3145: speed 1, control 1; scope and HUD return.
- INFERENCE: the qualifying `0x698130` ray query succeeded, because the original
  `0x6d8be0`/`0x6d8810`/`0x69c6c0` gate produced the sniper bullet camera.
  That return byte was not separately instrumented.
- FACT: scripted FOV multiplier stayed 1.0. Speed 0.4 and FOV 1.4 were not
  observed. INFERENCE: the close target ended the projectile path before those
  later keyframes. No change to game scripts or host camera is justified.
- Patch: tools/ghost/sniper.sh preserves the corrected E + level-aim scenario;
  run `tools/ghost/sniper.sh <output-dir>` on test port 5193. No host input
  semantics or game bytes changed: Wine's held-state and buffered-event delivery
  already matches host/dinput8.c, and retail zoom demonstrably works.
- Evidence: out/sniper_1109/step2-evidence.json, run3/hit/page.log, report.json,
  state.tsv; six preserved keepers in out/keep/1109/sniper/step2_*.jpg.
  No rebuild needed for this script/docs correction; used the step-1 web build.
  Not verified: later 0.4/1.4 keyframes, Safari, physical browser keyboard input,
  subjective pitch/audio parity. Two game runs total in step 2; no more launched.
