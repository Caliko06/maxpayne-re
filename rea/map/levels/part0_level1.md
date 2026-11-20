# part0_level1 (1_Part0_Level1.ldb)

Read with `python3 tools/ldb_dump.py build/data/lvl/1_Part0_Level1.ldb --from 13800000`. Offsets are file byte
offsets. FACT = string read in the file. INFERENCE = my grouping of lines into objects/events. The script strings of
this level live at 13800000..15711089; the trailing block 15698000+ is the object-name table (no offsets, no code).

## 1. Entry

| What | Evidence |
|---|---|
| FSM `::startroom::startFSM` sends itself `start` at load | `::startroom::startFSM->FSM_Send(start);` @14034403 (INFERENCE: it is the first event of the object and nothing else sends it) |
| `start` state: wounded anims off, `c_setinvulnerable(false)`, all weapons removed, one painkiller, objectives, fade to black, notes P1L0a_002..005, `s_modeswitch(graphicnovel)`, `post_process.DO->DO_Animate(1)` | @14034503, @14034544, @14034603, @14034640, @14034741, @14034858, @14034923..@14035152, @14035210, @14035254 |
| `::startroom::post_process.DO` step 1 then fires `::entrance05::camera1->fsm_send(start)` and changes fog | @15509099, @15509141 |
| Intro camera: widescreen on, flash, camera attached to the player, `GM_SetPlayerControls(false)`, **`player->c_setinvulnerable(true)`**, animate 1, fov 1.1, music none, `player->c_teleport(::startroom::w1)` | @13983185, @13983236, @13983301, @13983357, @13983407, @13983442, @13983485, @13983528, @13983568 |
| Intro tail: "years ago" text, camera anims 2/2b/3/4, then `::startroom::camera2->FSM_Send(start)` | @14616696..@14618567 |
| `startroom::camera2.DO`: camera on player, animate 1, look-at mover, `d_timer.DO->DO_Animate(1)` | @14011607, @14011662, @14011704, @15217898 |
| `d_timer.DO` step 2: fade, camera back to the player, widescreen off, HUD on, **`GM_SetPlayerControls(true)`**, **`c_setinvulnerable(false)`**, fov 1, **`player->c_sethealth(1)`** | @15240738, @15240791, @15240948, @15240997, @15241033, @15241202 |

So: the level opens with a scripted camera while Max is invulnerable and unarmed; he gets control with health 1 and no
weapon. INFERENCE: `startFSM`/`post_process` are load-time entries (nothing in the file sends them).

## 2. Player path

| # | Step (exact object) | What it fires | Controls / invuln windows | Enemies moved |
|---|---|---|---|---|
| 1 | `::startroom::t_hell` `.TRIGGER` (contact, in startroom) | `entrance03::CameraController_01.DO->do_animate(cam_01)` @14035472, phone ring @14035532, `c_removeallweapons(beretta)` @14035612, `c_pickupweapon(beretta)` @14035734, `c_pickupammo(beretta, 99)` @14035769, one painkiller @14035696, self disable @14035806 | controls stay on; cam_01 is a short look-at | none |
| 2 | `::entrance06::cin_trigger2` (player collision DO, entrance hall) | collisions off @14006031, **`::entrance06::Controller.DO->DO_Animate(1)`** @14006092 | the Controller DO sets controls off @14954637, `c_setinvulnerable(true)` @14954687 | `player->c_teleport(::teleport::W01)` @14954722; `::teleport::michelle` to `bedroom::w_michelle` then `c_kill(1)` @14955113/@14955352; **`::babys_room::e1->C_Teleport("::babys_room::e1_waypoint")`** @14955174; `::babys_room::door_dummy1::Door->fsm_send(kick_open)` @14954761; `::WorkRoom::e2->C_gotoplayer(1)` @14955878 |
| 2b | `::entrance06::cin_trigger` (its twin) | collisions off, bookcase smash sound, `speaker->A_Play3DSound(P1L0a_02)` @14005506..@14005691 | none | none |
| 2c | end of the attack cutscene, `::entrance06::Cam_02.DO` | `C_SetStateMachine(mobstercombat)` on e1/e2 @14939441/@14939495, `GM_SetPlayerControls(true)` @14939829, `c_setinvulnerable(false)` @14939878, idle_delay | control returned, Max vulnerable | e1 (`c_goto(::entrance06::w1)` @14955825), e2 goes to player |
| 3 | `::babys_room::New mesh 24::Baby_Cams_trigger` (optional, baby's room) | self disable ×2 @13954052/@13954156, `CameraController.DO->do_animate(cam_01)` @13954229 | 4 cams, controls off in each step @14329993..@14331963, `cinematic_lookat.TRIGGER->T_Enable(true)` @14330090 then off @14332060, **control back + `c_setinvulnerable(false)` @14332355/@14332404** | none |
| 4 | kill `::babys_room::e1` | e1 `C_RemoveAllWeapons(beretta)` @14044427, `death_counter->fsm_send(touch1)` @14044479, **`::bedroom::fsm_count->fsm_send(1)`** @14044573 | none | e1 dies where it stands |
| 5 | kill `::WorkRoom::e2` | @14045439, `death_counter->fsm_send(touch2)` @14045489, **`fsm_count->FSM_Send(2)`** @14045581 | none | none |
| 6 | kill `::storage::e3` | @14044754, **`fsm_count->FSM_Send(3)`** @14044803 | none | none |
| 7 | `::bedroom::end_trigger1` or `::storage::end_trigger1` (player contact) | both disabled @13968437/@14036136, `end_delay_timer.DO->do_animate(delay)` @13968489 | cutscene: teleport + controls off below | player to `::bedroom::W01` |

The doors along the way (`::babys_room::door_dummy1::Door::t1/t2`, `::bedroom::door_dummy2::Door::t1/t2`,
`::WorkRoom::door_dummy1::Door::t1/t2`, `::bedroom::door1::t_door_wc` @13965475) are contact triggers that animate the
leaf and disable themselves (FACT @13951720..@13952782). Drawers, cupboards, the shower, the toilet and the fans only
play sounds/animations (`Bathroom::t_shower` @13962402, `Bathroom::t_toilet` @13962897, `New mesh 24::Action button
trigger 01/02` @13953476/@13953783).

## 3. Exit

1. `::bedroom::fsm_count` has one handler per junkie message (FACT, bytecode at 13970700):
   message `1` -> `FSM_Switch(2)` @13970741; `2` -> `FSM_Switch(3)` @13970787; `3` -> `::storage::e3->CAM_AnimateParented(death_01)`
   @13971245 **and `::bedroom::d_end_timer.DO->DO_Animate(1)`** @13971293. The same DO Animate also appears in the
   e1/e2 handlers @13970884/@13971089, so each message body ends with the timer line; INFERENCE: only the message that
   finds the counter in its last state reaches that line, i.e. the 3rd kill starts the timer (kill order unproven).
2. `::bedroom::d_end_timer.DO` (1 s) -> `::bedroom::end_trigger1.TRIGGER->T_Enable(true)` @14426685 and
   `::storage::end_trigger1.TRIGGER->T_Enable(true)` @14426736 (FACT).
3. `::bedroom::end_trigger1` T_Activate: self off @13968385, storage twin off @13968437, `end_delay_timer.DO->do_animate(delay)`
   @13968489, sound P1L0a_12 @13968541 (FACT). The storage twin does the same @14036084..@14036240.
4. `::bedroom::end_delay_timer.DO` @14435082 -> `::bedroom::End_Controller.DO->do_animate(Exit_sequence)` @14435201.
5. `End_Controller` Exit_sequence: `End_Randomize` animation_0..animation_3 (one camera per animation) @14433548..@14439156,
   then `exit2` @14439852 with fade and music off @14439902/@14439960.
6. `End_Randomize` animation_0: `player->c_teleport(::bedroom::W01)` @14436855, wounded anims on, idle, all weapons
   removed, `gm_changeglobalgamespeed(0.2)`, camera 01, controls off @14437149.
7. State `Exit2` @14440178: idle off, wounded anims off, notes P1L0a_008..010, `s_modeswitch(graphicnovel)` @14440597,
   **`gm_init(part1_level1)`** @14440641.

## 4. Bot traps

| Trap | Evidence |
|---|---|
| Max is unarmed until step 1: the kill list is unreachable if `::startroom::t_hell` is skipped (no other line gives a weapon except the cutscene line `c_removeallweapons(empty)` @14436963 and the intro @14034603) | @14035734/@14035769 |
| `T_Activate` on `::entrance06::Controller.DO` without the player at entrance06 teleports Max to `::teleport::W01`, puts e1 at `::babys_room::e1_waypoint` and sends e2 `gotoplayer` while the camera looks at nobody: a state a player never sees | @14954722, @14955174, @14955878 |
| Same for the final cutscene: it teleports Max to `::bedroom::W01` and strips his weapons @14436855/@14436963, so firing `end_trigger1` early is "safe" but shows the ending with the wrong player state | @13968489 |
| `fsm_count` must see the three death messages; killing e1/e2/e3 out of order is not proven to complete the count (see 3.1) | @13970741..@13971293 |
| No `C_SetImmortal`/`C_SetInvulnerable` on any enemy: the only invulnerable is the player during cutscenes (grep `immortal` = 0 hits in the whole file) | @13983407, @14332404 |
| The exit needs the player to walk into `end_trigger1` (contact, r 3.0/4.0 per rea/map/ai.md:75); nothing else ends the level | @14426685 |

## 5. exits.json check (tools/ghost/exits.json -> part0_level1)

| Verdict | Item |
|---|---|
| ok | `exit` names `::bedroom::end_trigger1.TRIGGER` / `::storage::end_trigger1.TRIGGER` @13968162/@14035861/@14035943 |
| ok | `kill` `::babys_room::e1` @14044287, `::WorkRoom::e2` @14045301, `::storage::e3` @14044617 |
| ok | `why` "End_Randomize.DO step Exit2 sends gm_init(part1_level1)" @14440178/@14440641; timer chain @13971293 |
| ok | ff waypoints `::entrance06::w1` @13948375, `::bedroom::W01` @13947579 exist |
| **missing step** | `::startroom::t_hell` @14035457 (weapon + ammo); without it `kill` cannot start |
| **missing step** | `::entrance06::cin_trigger2` -> `::entrance06::Controller.DO->DO_Animate(1)` @14006092 (the attack cutscene: player teleport, e1 teleport, e2 wake) |
| imprecise | ff "La habitación del bebé" tp `::entrance06::w1`: e1 is at `::babys_room::e1_waypoint` @14955174, not at entrance06 |
