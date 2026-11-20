# part1_level2 (1_Part1_Level2.ldb, the hotel / parking-house block)

Read with `python3 tools/ldb_dump.py build/data/lvl/1_Part1_Level2.ldb --from 25518000`. Offsets are file byte
offsets. FACT = string read in the file. INFERENCE = my grouping of lines into objects/events.

This level is a long walk through a building: `GM_EnableExit` toggles on every door portal, no death counter anywhere
(FACT: `grep death_counter` on the script region = 0 hits). The only fixed gates are the level entry and the lift that
ends it.

## 1. Entry

| What | Evidence |
|---|---|
| `::finito::fsm_start`: gamespeed 1, fade from black, `::finito::post_process.DO->DO_Animate(1)` @25653948, notes `P1L2a_001` @25653998, note bookmarked read @25654056; music `old_building` @25653600 | @25653694..@25654056 (INFERENCE: load-time, no sender in the file) |
| `::finito::camera2` (intro camera): camera 100 attached @25638019, animate 1 @25638071, `camera2::timer.DO->DO_Animate(1)` @25638110 | FACT |
| Intro tail (`camera2.DO` step 3 @26978159): fade, camera back to the player @26978589, widescreen off @26978639, HUD on @26978691, **`GM_SetPlayerControls(true)`** @26978796, **`c_setinvulnerable(false)`** @26978845, fov 1 @26978881, **`player->c_teleport(::finito::w1)`** @26978922, then `finito::e02` to `merccombat`/`GoToAndShoot(::finito::wp06)` @26979090/@26979138 and, later, both enemies target the player @26979645/@26979692 | FACT |
| Enemies at the entrance: `::finito::e01/e02` pick up `sawedshotgun`/`berettadual` in the init block @25636341..@25636437 | FACT |

## 2. Player path

| # | Step (exact object) | What it fires | Controls / invuln windows | Enemies |
|---|---|---|---|---|
| 1 | `::startroom::door_trigger` (hotel door) | self off @25806126, **`::finito::d_door01/02->DO_Animate(open)`** @25806180/@25806223 | none | none |
| 2 | `::startroom::t_switch` (light switch, in the entry hall) | dialogue `P1L2a_Max_02` @25806772, switch sound @25806827, `fsm_Switch(2)` @25806888 | none | none |
| 3 | `::center04::t_firedoors01` | **sound only**: `door_metal_locked` @25556241, state `default` | none | none |
| 4 | `::center04::t_lift` | **sound only**: `switch` @25556503 (no FSM, no door: the lift is moved by its own DO) | none | none |
| 5 | `::center04::t_music` | music `old_building` @25556735, self off @25556783 | none | none |
| 6 | `::room_firedoors01b::t_lift` | sound `switch` @25770986 | none | none |
| 7 | `::room_firedoors01b::t_firedoors01` | sound `door_metal_locked` @25770706 | none | none |
| 8 | `::room_to_ledge::d_window_out` (breakable window to the ledge) | `DO_SetHitpoints(25)` @25771206, break sounds/effects/debris @25771333..@25771889 | none | none |
| 9 | `::lift04::t_door` (lift call, **USE**) | switch sound @25690872, **`::lift04::d_liftdoor.DO->do_animate(open)`** @25690928, self off @25690973 | none | `lift04::e01..e03` in the lift car (`standandshootstatic` @25867765) |
| 10 | `::lift2::t_service_elevator01` (service elevator) | both of its door FSMs switch to `inactive` @25692455/@25692524 | none | none |
| 11 | `::lift2::t_end` (inside the lift, **the exit**) | self off @25691978, **`::lift04::d_liftdoor.DO->do_animate(close)`** @25692021, **`::lift2::d_timer.DO->do_animate(1)`** @25692067 | see 3 | none |

## 3. Exit

`::lift2::t_end` T_Activate @25691963 -> self off @25691978 -> lift door closes @25692021 -> `::lift2::d_timer.DO`
@27424990: fade to black @27426178 -> **`gm_init(part1_level2b)`** @27426251 (FACT). No kill, timer or enable gates it:
the only lines that name it are `t_enable(false)` @25691978 and its TRIGGER object @25890299 (FACT: no `T_Enable(true)`
in the file), so it is enabled at load, as exits.json says.

## 4. Bot traps

| Trap | Evidence |
|---|---|
| `::lift2::t_end` closes the lift door and cuts to black at once: `T_Activate` from anywhere ends the level with Max left where he stands (the `d_timer.DO` has no camera and no player teleport) | @25692021, @27424990, @27426251 |
| The two "lift" triggers the bot used as anchors are sounds only: calling `::center04::t_lift` or `::room_firedoors01b::t_lift` does not move anything | @25556503, @25770986 |
| The ledge route needs the window broken: `::room_to_ledge::d_window_out` has 25 HP and only then sends `DO_Hide(true)` @25771417 | @25771206 |
| `::lift04::t_door` is a USE trigger that disables itself: if the bot walks into the closed door without pressing it, it never opens | @25690872, @25690973 |
| No enemy is immortal or invulnerable in this level (FACT: 0 hits for `SetImmortal`, the only `c_setinvulnerable` lines are the player @27462305, @27469103, @27512899, @27519429, @27568972) | grep |
| The entry fight (`finito::e01/e02`) starts inside the intro DO: skipping the intro leaves both mercs with `merccombat` and no player target | @26979090, @26979645 |

## 5. exits.json check (part1_level2)

| Verdict | Item |
|---|---|
| ok | `exit` `::lift2::t_end.TRIGGER` @25890299, `next` part1_level2b @27426251, "enabled at load" (no `T_Enable(true)` anywhere) |
| ok | ff waypoints `::finito::w1` @26978922, `::center01::wp01`, `::centerl_01::wp02`, `::lift04::wp01` all exist as objects |
| **missing (whole list)** | `steps: []`: the level has real ordered progression and the bot has none. Minimum missing: `::startroom::door_trigger` @25806111 (opens the two hotel doors), `::lift04::t_door` @25690708 (USE, opens the lift door) and `::lift2::t_end` @25691963 (exit) |
| imprecise | ff "El ascensor" uses `::center04::t_lift` as if it moved the lift; the object only plays `switch` @25556503 |
| imprecise | ff "La ventana al vacío" cites `::room_firedoors01b::t_lift` "beside `::room_to_ledge::d_window_out` @25896761": that offset is the trigger object `::room_firedoors01b::t_lift.TRIGGER` @25896760 (a `switch` sound @25770986). The breakable window is `::room_to_ledge::d_window_out` @25771060, 25 HP @25771206 |
