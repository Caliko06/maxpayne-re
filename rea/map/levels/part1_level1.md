# part1_level1 (1_Part1_Level1.ldb, 31,839,914 bytes)

Read with `python3 tools/ldb_dump.py build/data/lvl/1_Part1_Level1.ldb --from 27300000`. Offsets are file byte
offsets. FACT = string read in the file. INFERENCE = my grouping of lines into objects/events. Object-init lists (the
block at 27604000..27632000, one entry per character/item) are FACT per line; they run at load unless noted.

## 1. Entry

| What | Evidence |
|---|---|
| `::startroom::fsm_start` (graphic novel, fade, gamespeed, slow-motion allowed) | `MPGNM_PickupNote(P1L1a_001)` @27603667, `s_modeswitch(graphicnovel)` @27603954, `post_process.DO->do_animate(1)` @27603998 |
| `::startroom::post_process` strips Max: **`player->c_removeallweapons(empty)`** @27604051, **`player->c_sethealth(1)`** @27604089 | FACT |
| Train intro: `::startroom::start_train::cam_rotator` state `start`: fade, camera 100 attached, animate 1, fog, music `subway_station`, `start_train.DO->DO_Animate(move)`, lights | @27605246..@27605726 |
| The three junkies in the train car are **immortal** for the cinematic: `::startroom::start_train::junkie1->C_SetImmortal(true)` @27628003, `junkie2` @27628363 (`junkie3` has no immortal @27628430) | FACT |
| `::start_train` and `::train::1/2/3` kill whoever collides: `activator->c_causedamage(10000)` @27604650, `activator->c_sethealth(0)` @27613964/@27614295/@27614619/@27614943 | FACT |
| Load-time stand-ins: `::teleport::max` gets `C_SetInvulnerable(true)` @27629929; `::maintenance_room::table::dead_cop` gets `C_SetInvulnerable(true)` @27624708/@27624909 | FACT |
| INFERENCE: this whole block runs on load; nothing in the file sends `fsm_start` | no sender found |

Max starts unarmed with health 1; the first weapon comes from the level (leadpipe, see step 2).

## 2. Player path

| # | Step (exact object) | What it fires | Controls / invuln / immortal | Enemies |
|---|---|---|---|---|
| 1 | `::p5::d_door_start::t_door` (door leaf, p5 corridor) | `p5::d_door_start.DO->do_animate(open)` @27555081, self off @27555124 | none | none |
| 2 | `::to_storage01::New mesh 01::t_door` (storage door) | leaf `do_animate(open)` @27616792, self off @27616844, **`::to_storage01::enemy_trigger.TRIGGER->t_enable(true)`** @27616908 | cutscene (st01): `c_removeallweapons(beretta)` @31306219, **40 Beretta rounds** @31306258, **leadpipe + infinite ammo** @31306295/@31306376, painkiller @31306421 | none |
| 3 | `::to_storage01::enemy_trigger` | self off @27616184, **`::p5::script->fsm_send(initialize)`** @27616242 -> `stairs1::d_door01/02 open` @27558752/@27558796, `stairs1::t_gate` off @27558840, `teleport::e1/e2` to `p5::e1w1/e2w1` @27558521/@27558565 | none | e1/e2 idle, dialogue P1L1a_01 @27558695 |
| 4 | `::stairs1::t_flash01` (past the gate) | `bridget01::d_timer.DO->do_animate(1)` @27600044, self off @27600086, `bridget01::gunshoot_dummy` anim @27600135, **`teleport::e1/e2->C_SetStateMachine(mobstercombat)`** @27600185/@27600237, `C_gotoplayer(1)` @27600289/@27600324 | none | the two p5 junkies attack; `p5::script` `start` upgrades e1 to `delay2s` combat @27559110 |
| 5 | `::bridget01::t_enemy_gate01` | `::gate::enemy->A_Play3DSound(P1L1a_03)` @27412501, self off @27412557 | none | gate enemy already `standandshootstatic` @27623430 |
| 6 | `::p4::guard_init` | self off @27550255, **`hallway1::e1_timer->fsm_send(start)`** @27550300, `hallway1::e1->C_SetStateMachine(idle)` @27550341 | none | hallway1::e1 (pump shotgun) |
| 7 | `::s + stairs1::alert_enemies` (name has spaces and a `+`) | self off @27574587, **`::p2::script->fsm_send(initialize)`** @27574644 | none | p2 enemies |
| 8 | `::p2::t1` | `r01::helper` sound stop @27541717, self off @27541759, `r01::t2` off @27541796, **`r01::Door.DO->do_animate(open1)`** @27541834, `r01::script->fsm_send(start)` @27541872, helper speak off @27541905 | none | none |
| 9 | kill `::p2::e1` or `::r01::e1` | `r01::death_counter->FSM_Send(add1)` @27625322 / `add2` @27626536 -> **`::r01::pt1.TRIGGER->T_Enable(true)`** @27566002 and @27566213 (each handler enables it) | none | helper e1 dies -> `r01::game_over_timer` @27626779 |
| 10 | `::r01::pt1` then `::hallway1::helper_to_open_door_trigger` | self off @27473944, **`::r01::helper->c_goto("::hallway1::w2", 1)`** @27474012, `s2::pt1`/`maintenance_room::pt1` off @27474058/@27474096 | none | the cop walks to the door |
| 11 | cop cutscene `::hallway1::cam_rotator::camera1` `start2` | `hallway1::door_dummy::door1->fsm_Send(unlock)` @29883107, anims 1b..6 @29883052..@29926589, `hallway1::timer::trigger1.TRIGGER->T_Activate()` @29883745, door `DO_Animate(delay)` @29933582, **keypads on** @29933640/@29933695, `GM_EnableExit(::hallway1::toggle1, false)` @29933813, `t_doors` on @29933950 | controls handled by the camera FSM | control_room1::e1 goes to `camera2` @29926172/@29926985 |
| 12 | `::hallway1::timer::trigger1` (keypad, **USE**) | both keypads off @27484204/@27484260, **`hallway1::timer.DO->do_animate(enabled)`** @27484620, keyboard sound, dialog | timed: `timer.DO` closes the door and re-locks (`door1->do_animate(close)` @29934293, `GM_EnableExit` false/true dance @29934760..@29935137) | none |
| 13 | `::control_room1::door_dummy::door_trigger` | `door1->fsm_send(open)` @27456115, self off @27456242 (FSM `check_function` -> `open_function` @27455933) | none | none |
| 14 | `::c::t_door` | `c::d_door_3rail01.DO->do_animate(open)` @27445184, self off @27445228, **`GM_EnableExit(::c::exit_to_power, true)`** @29585938 | none | none |
| 15 | `::2nd_rail::control_desk::trigger` (third rail, **USE**) | self off @27379225, button/pole anims @27379287/@27379348, look-at off @27379401, **`::p2::electricity_controller->fsm_Send(start)`** @27379534 | none | p2 enemies `SetInfiniteAmmo` @27521862 |
| 16 | `::p2::electricity_controller` `start` | hides the dead train @27516922.., **`train_start_trigger->FSM_Switch(on)`** @27517472, `door3`/`door1` `unlock` @27517532/@27517580, train parts shown @27517628.. | none | none |
| 17 | `::p2::service_train::trigger1` (side door button, **USE**) | door leaf `do_animate(open)` @27536570, self off @27536454 | none | none |
| 18 | `::p2::service_train::train_start_trigger` (**USE**) | sound `switch` @27535769, dialogue @27535861, self off @27535911, **`::p2::camera1->fsm_Send(start)`** @27535981 | the ride: camera + fade, `player->c_teleport(::p2_tunnel::w1)` @30998367, `GM_EnableExit(::p2_tunnel::exit_out, true)` @30285490, `c_setinvulnerable(false)` @30998200 | none |
| 19 | `::p2::service_train::trigger4` (cab door button, **USE**) then `::p2::service_train::door5::t1` | door5 `t1` disables itself and its twin, leaf `do_animate(open2)` @27532217 | none | none |
| 20 | `::a5::t_door` (a5, after the tunnel) | self off @27403664, **`::a5::timer.DO->DO_Animate(1)`** @27403706 | controls off @29336101, `c_setinvulnerable(true)` @29336151, fade @29336186 | optional a5 fight: e1..e5 @27618707.., `a5::death_counter` @27397302 |

## 3. Exit

`::a5::t_door` T_Activate @27403649 -> `a5::t_door.TRIGGER->t_enable(false)` @27403664 -> `::a5::timer.DO->DO_Animate(1)`
@27403706 -> `:a5::timer.DO` @29334956: `GM_SetPlayerControls(false)` @29336101, `player->c_setinvulnerable(true)`
@29336151, `MPHM_FadeToColor` @29336186, **`gm_init(part1_level1b)`** @29336252 (FACT).

`::a5::t_door` has no `T_Enable(true)` anywhere in the file; it is enabled at load (only `t_enable(false)` @27403664 and
its TRIGGER object @27631615). a5 is reachable only by riding the service train (step 18).

## 4. Bot traps

| Trap | Evidence |
|---|---|
| Train junkies: `junkie1`/`junkie2` are **immortal** for the intro; nothing ends it. `junkie3` is mortal (`C3_In_Junkie`, nonreactive) | @27628003, @27628363, @27628430 |
| `::start_train` / `::train::1/2/3` are lethal collision hulls: `activator->c_causedamage(10000)` and `activator->c_sethealth(0)` | @27604650, @27613964..@27614943 |
| The wounded cop on the maintenance table is `C_SetInvulnerable(true)` and must stay alive-looking; a random shot there wastes ammo but he never dies | @27624708, @27624909 |
| `::r01::helper` death = **game over**: `::r01::game_over->fsm_Send(start)` @27627173, camera `game_over_cam1` @27568486, and his state is `C_SetHealth(0.01)` @27627134. He is no target | @27626863, @27627134, @27627173 |
| `::r01::e1->C_SetTargetCharacter("::r01::helper")` and `::control_room1::e1->C_SetTargetCharacter("::r01::helper")` make the cop their target: he dies (0.01 HP) and the level fails | @27569259, @27448464 (see also @27568016) |
| The security door is timed and **re-locks from both sides**; keypads are the only way (USE, type 0) | @29934293, @29933640, @27484620 |
| Firing `::hallway1::timer::trigger1` remotely with the door already locked/disabled lands in the `disabled` state: sound `security_keyboard` @27484367 and dialog only, door stays shut | @27484172..@27484445 |
| The train ride teleports the player and sets `exit_out` true: `T_Activate` on `train_start_trigger` without Max inside the cab drops him at `::p2_tunnel::w1` anyway, skipping the ride camera | @30998367, @30285490 |
| `::2nd_rail::control_desk::trigger` is **USE**: nothing else powers the train, and `train_start_trigger` stays `off` | @27379210, @27517472 |
| `::p2::service_train::trigger1/3/4` are the train's **USE** door buttons (type 0 per exits.json; the .ldb only shows the leaf anims) | @27536298, @27536570 |

## 5. exits.json check (part1_level1)

| Verdict | Item |
|---|---|
| ok | steps 1..8, 11, 12, 14, 16, 18 names all exist, with the offsets cited above |
| ok | `exit` `::a5::t_door.TRIGGER` @27631615, `next` part1_level1b @29336252 |
| **wrong** | step 9 `kill: ["::p2","::r01"]`: `r01::pt1` is enabled by **either** death handler (`add1` @27566002, `add2` @27566213); the file does not require both rooms cleared |
| **wrong offsets** | step 11 (`::hallway1::timer::trigger1`) cites `@46339206` and `@46339614`; the file is 31,839,914 bytes, so both offsets are outside it. The real lines: door close @29934293, keypad re-enable @29933640 |
| **wrong** | step 2 `why` "st01 cutscene gives the Beretta": the cutscene removes weapons (`c_removeallweapons(beretta)` @31306219), gives 40 Beretta rounds @31306258, the **leadpipe** with infinite ammo @31306295/@31306376 and a painkiller @31306421. The only `C_PickupWeapon(beretta)` is on the stand-in `::teleport::max` @31306673 |
| imprecise | step 19 relies on `door5::t1` `do_animate(open2)` "cited @46399170"; that offset is also outside the file. The real line is @27532217 |
| missing | the a5 fight is optional for the exit; `a5::t_door` is ungated (`FACT`, section 3) |
