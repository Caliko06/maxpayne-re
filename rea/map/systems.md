# Systems map: Max, enemies, weapons, sound (2026-11-04)
One page per system: where it lives in the binary, what the port does today, where to inject to test it, what is open.
Detail lives in the linked maps. Method (OWNER 11-04): RE first; test only at the injected moment, web vs native.

Injection points for Part I: rea/map/inject_part1.tsv (83 moments: fight / wave / exit / scene, with the RN_JUMP lines and
the .ldb message each comes from). Spawns are `C_Teleport("<waypoint>") + C_SetStateMachine(<combat machine>)`; there is no C_Spawn.
C_Teleport takes a waypoint name, not coordinates. Parts II and III: rea/map/inject_part23.tsv (124 moments, same columns).
Scripted input at a moment: '@' lines in RN_JUMP (host/jump.c, merged 11-04); a level's opening comic is skipped with Esc.
Native oracle line (RN_GHOST=n): pos, vel, lower state id C+0x35b, animation id C+0x363 (move_oracle.md).

Detail maps written 11-04 (becarios, Ghidra corpus, each claim with address; second key still owed where marked):
move_oracle.md (per-frame movement fields), look_move.md (look + movement code), bullettime.md (two clocks, speed slots),
weapons_runtime.md (SHOOT -> projectile -> hit -> health chain), weapon_fx.md/.tsv (sounds and effects per weapon and material),
enemies.md/.tsv (Part I types), ai_open.md (AI open items), char_render.md (LOD, skins, attachments, skinned draw),
sound_virtual.md (when a requested sound never plays).

## 0. Camera / mouse look (first fix, OWNER: "mouse far too fast")
- Chain (FACT, Ghidra corpus + binary bytes + runtime 6.274e-3 rad/count in player.md): input.md "Mouse math", look_move.md section 1.
  rad per DirectInput count = 0.0025 * pi * (slider*1.4 + 0.1) (0x5195c0, 0x519480, 0x7d045c, 0x7cb8b4, 0x7cfb08, 0x7cfb04).
  Default slider 5000 (the OWNER's registry) = 0.36 deg per count, smoothed 0.5 per frame (0x519370).
- No factor for frame time, bullet time, FOV or resolution (look_move.md: real dt 0x7269f0 cancels the mouse dt).
- The port adds no scaling: web/input.js sends movementX 1:1 (pointer lock, unadjustedMovement), host/input.c -> dinput8.c.
  So web and native turn the same per count. The speed difference vs the 2001 PC is the device: a 400 DPI mouse then,
  a 1000-1600 DPI mouse or a trackpad now = 3-4x more counts for the same hand move. If Chrome refuses unadjustedMovement,
  the plain lock adds macOS acceleration on top (not provable headless).
- Fix options (OWNER decides): (a) in-game Options > Controls > Mouse Sensitivity (the original's own slider, down to 1/8);
  (b) a counts divider in web/input.js that emulates a 400 DPI mouse, the slider keeps its meaning.

## 1. Max: movement (WORKLIST #9)
- X_Character::update 0x687f20 (substeps <= 0.11 s, 0x7d5920); lower body + collision 0x68c960; free fall 0x72c540.
- Walk/run are root motion from animations (no speed integrator): run 4.2 m/cycle, walk reverted to run (179_max_payne.txt).
- Shootdodge: DodgeSpecific 0x764180, direction 0x68c850, 5.25 m per dodge (data), space probe 0x501810 (+3 m / -2 m).
- Jump 0x699820 (ground-snap lockout 0.5 s); gravity (0,-20,0) from 36_levels.txt; stairs = capsule sweep + snap 0x647df0 (max drop 0.05).
- Port status: MATCHES native (2026-11-04, part1_level1 p5::w1, '@' run 2 s / strafe 1 s / jump / shootdodge, out/move_web.log
  vs out/move_native2.log RN_NODRAW): same state ids in the same order (12 idle, 2 run, 8 strafe, 38 -> 42 jump, 20 dodge),
  same dodge velocity (2.79, 0, -0.88), jump peak ~1.45 m, run 6.7 m native / 7.1 m web (key hold is real time; jitter).
  Native drawn off-screen ran ~3 fps on a loaded Mac: the 0.11 s substep cap makes it slow-motion; time-based compares need RN_NODRAW.
- Inject: any fight row of inject_part1.tsv + '@' input lines; compare player pos per frame (gamestate.c pos/vel) web vs native.

## 2. Enemies: AI and look (WORKLIST #8)
- AI = ~70 table state machines (0x4d03e0..0x4eba60); AP.update 0x5001f0 -> perception 0x4f62a0 -> machine 0x4fa280.
  Fire interval from Default/MaxFreq * mult; accuracy = projectile spread only (ai_behaviour.md section 0).
- Data: rea/map/enemies.tsv / enemies.md: 8 types in Part I (256 instances), health, weapons, AI ranges, voice set, each with source.
- Port status: MATCHES native (2026-11-04, part1_level1 p2 platform fight, alert_enemies trigger, out/fight_*.log): the same
  three foes (en17-19) wake with the same flags, en17 walks in from x -18 to -8.4 native / -9.3 web in the same time.
- Inject: fight rows (start) and wave rows (C_Kill the room's enemies, watch the next wave or door open: e.g.
  part1_level4 hallway4::death_counter (6) opens end::Door::t1; part1_level1 a5 combat_script2 -> FSM_Switch(secondary)).

## 3. Weapons and bullets (WORKLIST #10)
- weapons.md: data table from x_data, effects per weapon (shoot / reload), runtime layout from the player C.
- Fire chain (weapons_runtime.md): shoot action 0x4cbd00 -> fire interval 0x766950 -> WS_AnimateShooting 0x758400
  (round consumed 0x757f50, ws+0x1cb clip) -> P_CreateProjectile 0x758790 -> 0x65ae10 spawn (spread = rand*Accuracy/2500)
  -> sweep 0x70a0d0 -> character hit 0x7097d0 -> health C+0x2b3 written by 0x6d71a0. Hook for "hit X for N": 0x695b10.
- Port status: MATCHES native (same run, beretta): id 3, reserve 42 -> 24 on both, reloads at the same points, en17 hp
  12 -> 3.2 native / 2.0 web (spread is rand), 2 kills native / 1 web in 600 ticks of '@f' fire. weapon_reads.md: the
  pickup only fills the bag; a weapon is equipped by its slot key ('@k:2:10' for pistols), else reads show EMPTY (0/1/100).
- Earlier note: data side done; runtime side (clip/reserve/damage readings) empty. Open: reload times (kf2), AI fire rate,
  muzzle particle parser, melee, explosion second block (weapons.md section 4).
- Inject: "player->c_pickupweapon(<w>);|player->c_pickupammo(<w>,n);" at a fight row, fire with '@f' lines, read clip/reserve.

## 4. Sound (WORKLIST #11)
- sound.md: sndmfc.dll whole image mapped; host/dsound.c implements every COM method the game calls.
- Measured (web, part1_level1, 420 s): 856 Play, 813 audible, 38 silent (all 8-bit, 0.05-0.57 s).
- Virtual sounds (sound_virtual.md): sndmfc keeps no counter. The tick 0x10302d70 gives voices to the first [0x10315020]
  (12) by priority; the rest stay virtual (pool full, past 3D falloff, audibility off, 3D start pending <= 0.2 s) and burn
  their loops silently. host/dsound.c now counts per name at the tick's commit (ret 0x10303286): RN_SOUNDLOG prints
  "dsound virtual: ... asked and never voiced".
- Web vs native (2026-11-04, part1_level1 p5 ambush, 300 s, out/fight_web.log / fight_native.log): the SAME 16 names are
  asked and never voiced on both (subway control_room_ambience, computer room, coke machine hum, electric hum, water drip,
  camera whirr: far 3D loops; plus some bullet-hit/case sounds), Play 700 web / 711 native, peak 9 voices of 12, pool never
  full. So these are the original's own falloff/priority choices, not a port loss.
- Host approximations (music_voice.md 6, fx_render.md 4): 3D pan is x/d, not the DS3D sine law; no loopStart/loopEnd (no .sfl
  ships); 0.5 s ring hold on a guest stall. Render: no ZBIAS (unused), float depth instead of D24 (decal z-fight risk).
- Inject: the same fight rows; RN_SOUNDLOG accounting per moment.

## Next fixes, in the OWNER's order (Max -> enemies -> weapons -> sound)
1. Mouse: apply the option the OWNER picks (section 0).
2. Max: merge mp-systable ('@' input lines), then one web vs native compare of the same input at one fight row.
3. Enemies: one wave row web vs native (who wakes, who shoots, the next wave arrives).
4. Weapons: runtime readings at one fight row vs the weapons.md table.
5. Sound: read 0x1030b6e0 / 0x1030bc60 at one fight row: requested vs played.
