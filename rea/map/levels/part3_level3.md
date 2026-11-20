# part3_level3 (bunker: Valkyr lab, airlock, vats, main hall, elevator out)

Source: `build/data/lvl/1_Part3_Level3.ldb` (28 MB). Message lines are stored as `<target>-><message>`; the sender object is
INFERENCE from the header line above each block. Room order below is INFERENCE from the airlock/elevator names and from the
two objectives the level prints (`OBJ_BUNKER_MS_VALKYR` then `OBJ_BUNKER_GETOUT` @27768308).

## 1. Entry
`::startroom::startup` (header @25979682), block @25979819-25980401 (FACT): fade to black @25979819,
**`GM_SetPlayerControls(false)` @25979884**, `::startroom::elevator::post_process.DO->do_animate(1)` @25979934,
`::startroom::elevator.DO->DO_Animate(down)` @25979991, gamespeed 1 @25980037, objectives `OBJ_BUNKER_MS_VALKYR0/1`
@25980090/@25980157, notes `P3L3a_001/002` @25980230/@25980343, graphicnovel @25980401.
The elevator trigger in the start room is a use action: `::startroom::elevator::trigger` (`T_Activate` @25979519) plays
`switch` @25979534 and `turn_valve_rusty` @25979604.
Control comes back after the airlock intro camera: `::airlock::camera2.DO->CAM_AttachToDO(0,false)` @27305367,
**`GM_SetPlayerControls(true)` @27305471**, `player->c_setinvulnerable(false)` @27305561, widescreen off @27305597,
HUD on @27305649, flash @27305701, inventory `STRING_CARRYING` @27305763 + `STRING_BUNKER_INV_PINCODE` @27305824,
look-at trigger enabled @27305895. So the player starts inside the airlock, holding the pin code.

## 2. Player path (ordered)
| # | step / object | what it fires | controls / invuln | enemies / actors |
|---|---|---|---|---|
| 1 | load `::startroom::startup` @25979682 | elevator down, notes, objectives, graphic novel | controls off @25979884 | — |
| 2 | `::airlock::camera2` end @27305367 | HUD/inventory/pin code, look-at on | controls on @27305471, invuln off @27305561 | — |
| 3 | `::airlock::player_entered` (header @25712789, `T_Activate` @25713001) | self off @25713016, **`::airlock::counter->FSM_send(touch)` @25713070** | — | `::airlock::pole::alarm` (@25713116), `::airlock::scientist_dummy` (@25714496) |
| 4 | `::airlock::counter` (header @25703081) | `FSM_Send(touch)` @25703332, `fsm_switch(2)` @25703426, `disabled` @25703525; `::airlock::detect_player2` enabled @25714769 | — | airlock doors `door1/door2` (@25703943/@25705039) |
| 5 | keypad `::upper::terminal::terminal_trigger` (header @26030318) | `T_Enable(true)` @26030872, `fsm_switch(open)` @26030935, `security_denied` @26031201, `security_keyboard` @26031428, FSM `code` @25702967, `fsm_send(check_clearance)` @28301043 | — | — |
| 6 | `::storage::terminal_trigger` (header @25985272) | auto-activated by script **`::storage::terminal_trigger.TRIGGER->T_Activate()` @26103709**; `security_keyboard` @25985503, `security_access` @28202303 | — | — |
| 7 | `::storage::kill_character` (header @25980799, `T_Activate` @25980957) | **`::storage::scientist->c_sethealth(0)` @25980972**, `::storage::e2` target scientist + standandshootstatic @25981185/@25981248, e1/e2 GoTo @25981367/@25981412 | — | scientist killed by script |
| 8 | `::storage::merc_timer.DO` (@28204019-28205289) | e1/e2 gotoandshoot `::airlock2::w1/w2` @28204019/@28204073, `airlock2::door2` close @28204911, **`::storage::e1->C_setinvulnerable(false)` @28205289, `e2` @28205332** | — | e1/e2 become killable |
| 9 | `::vats::t1` (header @26096179) | self off @26107458, **`::vats::merc_controller->fsm_send(touch)` @26107500**, `::vats::e1` alert @26107544, `::vats::d_trigger2` off @26107680, `security_access` @26107788, `::hallway1::door_dummy::door1/door2` open @26107855, `door_opened` @26107963 | — | vats e1/e2/e3 |
| 10 | `::main::terminal_trigger` (header @25921469) | `security_keyboard` @25921641 | — | — |
| 11 | `::main::killersuit1/2` + `::main::mercenary` scene | e1/e2 get berettas @27858939/@27859529, target `::main::mercenary` @27859634/@27860092, standandshootstatic; then `::main::mercenary->C_SetInvulnerable(false)` @27871909 + `C_Kill(1)` @27871957; suits turn on the player @27888139-27888302, **immortal off @27888359/@27888404** | — | killersuit1/2, mercenary |
| 12 | `::shaft3::elevator::elevator_door_button` (header @25960400 area) | `security_denied` @25960237 / `security_access` @25960568, `::lower_lab::trigger` off @25960405 + lower doors open @25960454; `::upper_lab::trigger` off @25960734 + `::shaft3::upper_door::door1/door2` open @25960783/@25960835 | — | — |
| 13 | `::end_shaft::elevator::elevator_button` (header @25785449, `T_Activate` block @25785854-25787095) | `switch` @25786068, `fsm_switch(pressed)` @25786223, doors `doors_closed_ok` @25785977 | — | — |
| 14 | elevator ride end `::end_shaft::elevator::end_ride_controller` | arrive: lower doors @27582516-27582571, upper doors @27584848/@27584903, `lift_stop` @27584584, **`::end_upper::cam_dummy::shake::camera1->fsm_send(start)` @27585172** | — | — |
| 15 | `::end_upper::cam_dummy::shake::camera1` start (header @25802572) | `GM_SetPlayerControls(false)` @25803057, gamespeed .8 @25803107, **`player->c_setinvulnerable(true)` @25803161**, `::teleport::Max->c_setstatemachine(nonreactive)` @25803196, **`player->c_teleport(::teleport2::player_waypoint)` @25803247**, `::end_upper::fire_dummy2->fsm_send(start)` @25803299, **`::teleport::Max->c_teleport("::end_upper::w1")` @25803344**, widescreen @25803394, HUD off @25803445, flash @25803497, **`GM_TransferPlayerWeaponTo(::teleport::Max)` @25803559**, fov 0.5/3 s @25803624 | controls off | `::teleport::Max` body double |
| 16 | `::end_upper::camera2` start (header @25803794) | `CAM_AttachToDO(100,false)` @25803954, `DO_Animate(1)` @25804009, fov 0.7 @25804051; `fire_dummy2` flames stopped @25804388-25805418 | — | — |
| 17 | `::end_upper::camera2.DO->do_animate(2)` @27633260 | fade @27633302, gamespeed 1 @27633901, controls on @27633954, invuln off @27634044, widescreen off @27634080, HUD on @27634132, note `P3L3a_007` @27634184, graphicnovel @27634242, **`gm_init(part3_level4)` @27634286** | controls on | — |

## 3. Exit chain (FACT)
There is no exit `.TRIGGER`. The level ends on the elevator: `::end_shaft::elevator::elevator_button` (use, @25786068) ->
`fsm_switch(pressed)` @25786223 -> ride -> `::end_shaft::elevator::end_ride_controller` -> upper doors open @27584848 ->
`::end_upper::cam_dummy::shake::camera1->fsm_send(start)` @27585172 -> cutscene block @25802880-25803624 (player teleported,
weapon handed to `::teleport::Max`) -> `::end_upper::camera2` start @25803794 -> `do_animate(2)` @27633260 ->
**`gm_init(part3_level4)` @27634286**.
The send line in exits.json (`::end_upper::camera2->fsm_send(start);`) exists as a message @27622921, sent by
`::end_upper::cam_dummy.DO`; the gm_init is @27634286.

## 4. Bot traps
1. **`::main::killersuit1` immortal** @26102393, cleared only by script @27888359; **`::main::killersuit2`** @26102821, cleared
   @27888404. Before the mercenary execution scene they are nonreactive and unkillable (@26102288, @26102716).
2. **`::main::mercenary` invulnerable** @26103138, cleared at @27871909 and immediately `C_Kill(1)` @27871957: he dies by script,
   never by the player.
3. **`::storage::e1/e2` invulnerable** @26103616/@26104035, cleared only at @28205289/@28205332 (after their escort to
   `::airlock2`). Shooting them in the storage room does nothing.
4. **`::B::helper` invulnerable** @25762516, cleared @25763082 and @25999303 (cutscene helper).
5. **The ending teleports Max and takes his weapon**: `player->c_teleport(::teleport2::player_waypoint)` @25803247,
   `GM_TransferPlayerWeaponTo(::teleport::Max)` @25803559, and the body double is moved to `::end_upper::w1` @25803344.
   A bot that activates the ending camera remotely leaves Max at the elevator with a camera running.
6. **Use-key keypads/buttons**: `::startroom::elevator::trigger` @25979519, `::upper::terminal::terminal_trigger` @26030318
   (FSM `code` / `check_clearance`, `security_denied` @26031201), `::main::terminal_trigger` @25921469,
   `::shaft3::elevator::elevator_door_button` @25960237/@25960568, `::end_shaft::elevator::elevator_button` @25786068.
7. **`::storage::terminal_trigger` is fired by its own script** (`T_Activate` @26103709), so re-activating it is redundant.
8. Kill-room prefixes: `::storage`, `::vats`, `::airlock` hold both killable mercs and unkillable actors (e1/e2 in storage,
   `::storage::scientist`, `::vats::e1` at @26107257 nonreactive). A kill wait for `::storage` cannot complete before step 8.
9. Player invulnerable windows: @25701754, @25745601, @25803161, @25977242, each cleared by the matching
   `c_setinvulnerable(false)` (@27305561, @27505599, @27634044, @27887989).

## 5. exits.json check (`part3_level3`)
1. **Missing exit.** `"exit": []`. The real end is the `::end_shaft` elevator ride + `end_upper` cutscene -> `gm_init
   (part3_level4)` @27634286; nothing to walk into, so the bot needs the `::end_shaft::elevator::elevator_button` step
   (@25786068) instead of an exit trigger.
2. **First step is inert.** `::upper_lab::trigger.TRIGGER` @25960734 is *disabled* by the shaft3 elevator button; activating it
   remotely does nothing. The step that matters is `::shaft3::elevator::elevator_door_button` (@25960568 access / @25960783
   doors). The ff also orders `upper_lab` first although the level actually starts in the airlock (@27305471).
3. **Missing the pin-code keypad.** `::upper::terminal::terminal_trigger` (@26030318, FSM `code`/`check_clearance`, denied
   @26031201) is what the level expects the player to use with `STRING_BUNKER_INV_PINCODE` (@27305824).
4. **`kill: ["::storage"]` cannot complete early.** storage e1/e2 are invulnerable @26103616/@26104035 until @28205289, and the
   room also holds `::storage::scientist` (killed by script @25980972) - the wait will stall if it is armed too soon.
5. **Missing the main-hall gate.** `::main::killersuit1/2` are immortal (@26102393/@26102821) until the script fight
   (@27888359); a step that waits for their death must come after the mercenary scene (@27871909).
6. Correct: `::airlock::player_entered.TRIGGER` @25713001, `::vats::t1.TRIGGER` @26107458, `::main::terminal_trigger.TRIGGER`
   @26133926, and `send ::end_upper::camera2->fsm_send(start);` @27622921. All `tp` exist: `::storage::w2` @25630346,
   `::airlock::w1` @25626025, `::vats::w1` @25633122, `::main::w5` @25628606.

Not verified: the exact room order (airlock -> storage -> shaft3 upper/lower lab -> vats -> main -> end_shaft) and where
`::main::terminal_trigger` gates the door out; I read the trigger blocks, not the geometry.
