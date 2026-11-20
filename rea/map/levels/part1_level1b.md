# part1_level1b (1_Part1_Level1b.ldb, Roscoe St. station vault)

Read with `python3 tools/ldb_dump.py build/data/lvl/1_Part1_Level1b.ldb --from 22600000`. Offsets are file byte
offsets. FACT = string read in the file. INFERENCE = my grouping of lines into objects/events.

## 1. Entry

| What | Evidence |
|---|---|
| `::startroom::camera1` state `start`: widescreen on, HUD hidden, flash, camera 100 attached, gamespeed .8, **`GM_SetPlayerControls(false)`**, **`player->c_setinvulnerable(true)`**, animate 1, `camera1::timer.DO->DO_Animate(1)` | @22756606, @22756616, @22756667, @22756719, @22756781, @22756836, @22756890, @22756940, @22756975, @22757017 |
| `::startroom::Fsm_Start`: notes `P1L1b_001` @22757638, `P1L1b_002` @22757871, `s_modeswitch(graphicnovel)` @22757929, `Fsm_Start.DO->do_animate(1)` @22757973, objective `OBJ_SUBWAY_FIND_ALEX` @22758017 | FACT (INFERENCE: load-time, no sender in the file) |
| Intro ends: camera back to the player, widescreen off, HUD on, flash off, **`GM_SetPlayerControls(true)`** @23982210, **`c_setinvulnerable(false)`** @23982259, fov 1, fade | @23981938..@23982404 |
| NPCs made invulnerable: `::g2::alex` @22872424, `::stairs::bb` @22879246 | FACT |

Max keeps his weapons from part1_level1 (nothing removes them at load here; the only `c_removeallweapons` lines are on
NPCs, e.g. @22883304).

## 2. Player path

| # | Step (exact object) | What it fires | Controls / invuln windows | Enemies |
|---|---|---|---|---|
| 1 | corridor `::3` (ticket hall) entry: `::3::player_detect` | self off @22680211, `::3::player_detect_timer->FSM_Send(player_detected)` @22680336 -> `::tr::door_explosion->fsm_Send(start2)` @22680623, **`player->c_sethealth(0)`** @22680666 | instant death if spotted (state `player_dead`) | `::3::e1/e2` idle -> combat via `::3::script` `start` @22681404.. |
| 2 | `::v-a::New mesh 11::t_detonators` (USE, v-a) | detonator prop hidden @22803029, self off @22803083, **`::3::explode.TRIGGER->t_enable(true)`** @22803144, `::3::explode->fsm_switch(active)` @22803184, inventory print @22803220/@22803281, sound P1L1a_Max_12 @22803412, `::3::teleport_script->FSM_Send(initialize)` @22803468, **`::old_corridor::t_script.TRIGGER->T_Enable(true)`** @22803562 | none | none |
| 3 | `::v-b01::t_doors` (vault B door) | self off @22811712, `v-b01::d_door01/02->DO_Animate(open)` @22811757/@22811799, **`::vc::script->fsm_Send(initialize)`** @22811841 | none | `::vc::e1` idle, `e2..e4` `idleuntilenemyseen` @22859509..@22859648 |
| 4 | `::vc::script` `start` (timer) | `FSM_Switch(2)` @22859778, `vc::script_timer.DO->DO_Animate(combat)` @22859808, e1..e4 go to `vc::w1/w2/w3` and to the player @22860196..@22860326, state machines `crouchandshootstatic`/`standandshootstatic`/`merccombat` @22860354..@22860512 | none | 4 mercs, one with `berettadual` @22883799, one `deserteagle` @22884216 |
| 5 | kill `::vc::e1..e4` | per-enemy `::vc::death_counter->fsm_Send(add1..add4)` @22883542, @22883958, @22884264, @22884653 -> **`::vc::vault_control_lookat.TRIGGER->T_Enable(true)`** @22841848/@22842138/@22842428/@22842753 plus `::vc::d_phone.DO->DO_Animate(1)` and each own death cam | none | none |
| 6 | `::vc::vault_control_lookat` then `::vc::t_doors` (**USE**, vault switch) | self off @22861060, look-at off @22861102, switch sound @22861167, **`::v-a::d_door01/02 close`** @22861220/@22861261, **`::v-b01::d_door01/02 open`** @22861302/@22861344, `FSM_Switch(b)` @22861387; the same handler cycles `c` @22861943 and back to `a` @22862495 | none | none |
| 7 | the switch again (twice more) | each cycle re-enables the switch: `::vc::t_doors.TRIGGER->t_enable(true)` @24339204, @24364785, @24386501 (with `GM_EnableExit(::v-a::exit_from_vault_a, true)` @24338960 etc.) | none | none |
| 8 | `::exit::end_trigger` (player collision DO, outside in the snow) | `DO_EnableCollisions(false)` @22710113, **`::exit::d_endcamera.DO->do_animate(1)`** @22710167, mailbox hatch opens @22710542 | see 3 | none |

`::tr::door_explosion` `start`: `GM_EnableExit(::tr::toggle1, true)` @22772609 and `GM_EnableExit(::3::toggle1, true)`
@22772668 plus prop animations: the explosion opens the two portals (FACT).

## 3. Exit

1. The end object is **not a trigger**: `:exit::end_trigger` @22709907 is a DAO whose `DO_CharacterCollides` @22710088
   runs `::exit::end_trigger.DO->DO_EnableCollisions(false)` @22710113 and `::exit::d_endcamera.DO->do_animate(1)` @22710167.
   Nothing enables or gates it (`FACT`: no `T_Enable` line names it in the file).
2. `::exit::d_endcamera.DO` @23746378: flash, `do_animate(2)` @23746450 -> **`gm_setplayercontrols(false)`**
   @23746777, camera 10 attached @23746827, notes read @23746886, **`s_modeswitch(graphicnovel)`** @23746941,
   `MPGNM_PickupNote(P1L1b_005)` @23746985, **`GM_Init(part1_level2)`** @23747043.
3. There is no death counter or timer between the collision and `gm_init`: only the d_endcamera animation (FACT).

## 4. Bot traps

| Trap | Evidence |
|---|---|
| **Stealth kill**: `::3::player_detect` -> `::tr::door_explosion->fsm_Send(start2)` -> `player->c_sethealth(0)` (the `player_dead` state) | @22680211, @22680623, @22680666 |
| The vault enemies are NOT immortal and NOT invulnerable (no `C_SetImmortal`/`C_SetInvulnerable` on `::vc::e*` in the file); the only invulnerable characters are the NPCs `::g2::alex` and `::stairs::bb` | @22872424, @22879246 |
| The vault switch is a **USE** action and must be pressed **three times**: each press runs one state of the cycle (`b` @22861387, `c` @22861943, `a` @22862495) and re-enables the trigger @24339204/@24364785/@24386501 | FACT |
| `T_Activate` on `::vc::t_doors` from far away still flips the doors and re-enables itself: the player can be locked on the wrong side of a closing vault door (`d_door01/02 close` @22861220) | @22861220 |
| The exit is a collision DAO with no cutscene teleport: firing `d_endcamera` remotely ends the level with Max wherever he stands (controls off @23746777 then `gm_init`) | @23746941, @23747043 |
| `::v-a::New mesh 11::t_detonators` is a USE trigger that also disables the look-at @22803355 and prints inventory; if it is skipped, `::3::explode` and `::old_corridor::t_script` stay disabled | @22803144, @22803562 |

## 5. exits.json check (part1_level1b)

| Verdict | Item |
|---|---|
| ok | next = part1_level2 @23747043; the `send` `::exit::d_endcamera.DO->DO_Animate(1)` matches @22710167 |
| **wrong name** | `exit: ["::exit::end_trigger.DO"]`: the object in the .ldb is `::exit::end_trigger` (@22709907), a **collision DAO**; `.DO` is its animation child, not a trigger. Nothing to `T_Activate` |
| **wrong** | steps[1] `kill: ["::vc"]`: no kill gate exists for the vault switch. `::vc::death_counter` only enables `::vc::vault_control_lookat` @22841848 (a look-at trigger), and `::vc::t_doors` re-enables itself @24339204, @24364785, @24386501 |
| **missing** | the switch has to be pressed 3 times (cycle `b`/`c`/`a` @22861387/@22861943/@22862495); one step is not enough |
| **missing** | `::3::player_detect` @22680211 is a scripted-death step the bot must avoid (or it dies at @22680666) |
| ok | steps[0] `::v-b01::t_doors` @22811712 and steps[2] `::v-a::New mesh 11::t_detonators` @22803014, with the enable lines @22803144/@22803562 |
