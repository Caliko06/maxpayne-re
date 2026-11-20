# part3_level5b (Part III, chapter 5b: Woden's house, the way out)

Source: build/data/lvl/1_Part3_Level5b.ldb via tools/ldb_dump.py. FACT = string read at that offset;
INFERENCE = my grouping of strings into objects/events, and the room order.

## 1. Entry (FACT)
- `::startroom::fsm_start` @24900882: gamespeed 1, fade from black, notes `P3L5b_001` and `P3L5b_002` @24901139/24901252, `X_Modeswitch->S_Modeswitch(graphicnovel)` @24901310 (comic page), objective `OBJ_ASGARD_ASSASINS` @24901354, `::startroom::post_process.DO->DO_Animate(1)` @24901515.
- `::startroom::camera1` start @24894563: widescreen, `CAM_AttachToDO(100,false)` @24894624, `GM_SetPlayerControls(false)` @24894679, `player->c_setinvulnerable(true)` @24894729, camera animate + fade in @24894764/24894806, camera timer @24894871.
- The intro ends in `::startroom::camera1.DO` @25956601: `CAM_AttachToDO(0,false)` @25958913, widescreen off @25958966, hud on @25959018, white flash @25959070, gamespeed 1 @25959132, `GM_SetPlayerControls(true)` @25959185, `player->c_setinvulnerable(false)` @25959234, fov @25959270, fade from black @25959311. The player is never teleported by the intro: they stand where the level placed them.
- Weapons: nothing is given or transferred at load (FACT: no `GM_TransferPlayerWeaponTo`/`C_PickupWeapon(player)` in this file), the player keeps what they had.

## 2. Player path (FACT offsets, INFERENCE order)
| # | step | trigger / object | fires | windows | enemies |
|---|---|---|---|---|---|
| 1 | intro | `::startroom::fsm_start` + `::startroom::camera1` | section 1 | controls OFF 24894679 / ON 25959185; invuln 24894729 / off 25959234 | none |
| 2 | first door | `::startroom::New mesh 13::t_door` @24901854 | opens the door @24902062, self-disable @24902111 | none | none |
| 3 | note | `::startroom::note_01_trigger` @24903527 | note `P3L5b_003` @24903758, `graphicnovel` @24903816, `t_lookat` + self off @24903860/24903910 | comic page | none |
| 4 | hall | `::hall01::t_killer` @24818129 | `activator->c_causedamage(1000)` @24818208: it kills whoever touches it (the statue trap) | enabled by `::hall01::statue.DO` @25440806, disabled @25441943 | `hall01`, `hall` |
| 5 | trap room | `::trap::t4` @24908806 (in `:trap::t4` block, T_Activate @24908806) | self-disable, `::hall01::t3.TRIGGER` off @24908845, `::hall01::door3.DO->DO_Animate(open1)` @24908886 | none; `door3.DO` re-enables `t4` on its next pass @25429208/25431041/25432825/25434755 | `trap`, `corridor09` |
| 6 | library | `::library::t_act01` @24862393 | `activator->c_setstatemachine(crouchandshootstatic)` @24862393, self-disable @24862447 | see traps: this one acts on the activator, it is an enemy trigger | `library` |
| 7 | stairs / chopper | `::stairs01::t_chopper` @24886424 | `::arch::d_chopper.DO->do_animate(1)` @24886424, self-disable @24886463 | chopper chain @25172617: `helicopter_near` @25173984, animate 2 @25174055, glass shatter @25174094, animate 3 @25174452 | `stairs01`, `stairs03` |
| 8 | last wave | `::Teleport::e01..e05` / `::Teleport_2::enemy01..04` scripts | each death watcher sends `::Teleport_2::timer->FSM_Send(add)` and `::hall01::counter->FSM_Send(die)` @24920844-24923123 | see section 3 | five mercs teleported onto `::Stairs::wp01` / `::stairs01::wp01` @26015102/26015938/26022551 and the balcony @26018421 |
| 9 | exit | `::exit::Exit_Collide_Trigger` @24805113 | fade to black @24805113, `::exit::wait.DO->DO_Animate(1)` @24805179 | enabled only at @26020012 | none |

## 3. Exit chain (FACT)
1. `::exit::Exit_Collide_Trigger` T_Activate @24805113: `mphm_fadetocolor(black, 1)` @24805113 and `::exit::wait.DO->DO_Animate(1)` @24805179.
2. `::exit::wait.DO` animation end @25390994: `maxpayne_gamemode->gm_init(part3_level6)` @25390994, music `byzantine` @25391038.
3. The trigger is not enabled at load: `GM_EnableExit(::exit::Exit_01, false)` @24805427 and `GM_EnableExitAIActivation(::exit::Exit_01, false)` @24805569 close the engine exit, and `::exit::Exit_Collide_Trigger.TRIGGER->T_Enable(true)` @26020012 (inside `::Teleport_2::timer.DO`) is the only line that opens it, together with `a_playmusic(city_ambience)` @26019963.
4. `::Teleport_2::timer` is an FSM advanced by the five death watchers `::Teleport_2::end_e1..end_e5` (`FSM_Send(add)` @24920844, @24921363, @24922054, @24922475, @24922986; the same watchers send `::hall01::counter->FSM_Send(die)` @24920882, @24921497, @24922092, @24922609, @24923123). So: five scripted mercs must die before the exit exists (INFERENCE: the timer's states 2..6 @24906185-24906365 each run one animation; the last end handler is the one that enables the trigger).
5. `::hall01::counter` has its own states 2/3 @24807526/24807569 and also calls `::exit::merc_teleporter->FSM_Send(start)` @24807605; it is the second consumer of the same five deaths.

## 4. Bot traps
- `::library::t_act01` (@24862393) acts on its activator: `activator->c_setstatemachine(crouchandshootstatic)`. If the bot fires it remotely, the activator is the player and the player is put in an NPC combat state machine. `::hall01::t_killer` @24818208 is the same shape: `activator->c_causedamage(1000)`, 1000 damage to whoever activated it.
- `::trap::t4` is not a player gate in the direction the ff list assumes: its own T_Activate closes `::hall01::t3` and opens `hall01::door3` @24908886, and `door3.DO` re-enables `t4` on each of its animation ends @25429208/25431041/25432825/25434755. The player can therefore toggle this door repeatedly; firing `t4` from far away opens a door the player is not at, and the statue (`::hall01::statue.DO`) enables `t_killer` @25440806, so walking the hall afterwards kills the player.
- The exit trigger does not exist until the five watchers have fired @26020012. Sending `::exit::Exit_Collide_Trigger->T_Activate()` early does fade the screen and play `wait.DO`, and `wait.DO`'s animation end calls `gm_init(part3_level6)` regardless (FACT: the gm_init is in wait.DO's own handler @25390994), so a remote activation ends the level while the five mercs are still alive and the chopper never came.
- `::exit::Exit_Collide_Trigger` and `::Teleport_2::Exit_01` are also engine exits, disabled at load @24805427/24805569 with `GM_EnableExit` + `GM_EnableExitAIActivation`; a level that reaches them without the EnableExit(true) closes as if the player died (INFERENCE from the engine, not observed).
- Timed doors on the path: `::hall01::door3` is animation-driven (open1/close), and `::trap::door1.DO->do_animate(open)` @26022513 is part of the `trap::d_timer.DO` chain (@26022036 keyboard, panel shown, door open, `e05` teleported). A bot that reuses the trap door must wait for the chain, not for a timer.

## 5. exits.json check (tools/ghost/exits.json -> part3_level5b)
- `"steps": []` with four `ff` entries; the real gates exist: `::hall01::t_killer` @24818129 (but it is a damage trigger, not a kill counter), `::library::t_act01` @24862393 (an activator/enemy trigger), `::trap::t4` @24908806, `::stairs01::t_chopper` @24886424. Names resolve, roles are wrong.
- `ff[0].kill = [::hall01, ::hall]`: the area clear that opens the next exit is `::hall01::counter` driven by the five `end_e` watchers @24920882.. and `GM_EnableExit(::hall01::exit_to_corridor01, true)` @25423325, so a kill list of area prefixes does not describe the mechanism.
- `"exit": ["::exit::Exit_Collide_Trigger.TRIGGER"]` is right as the sender @24805113, but the record does not say it is gated by `T_Enable(true)` @26020012 (five deaths). As written the bot treats an exit-only route as valid at second 0.
- `"send": ["::exit::wait.DO->DO_Animate(1);"]` is the exact second message of the real chain (@24805179) and does reach `gm_init(part3_level6)` @25390994, so the `send` shortcut is correct; only the fade @24805113 is skipped.
- Missing from the record: `::startroom::New mesh 13::t_door` @24901854 (the first door out of the entry room) and the fact that the entry camera never teleports the player (@25959185).
- Not verified (UNKNOWN): whether the five deaths are also required by `::hall01::counter` for anything the exit needs; I read its states but not its full script.
