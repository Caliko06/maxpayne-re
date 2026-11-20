# part3_level6 (Part III, chapter 6: the Aesir towers)

Source: build/data/lvl/1_Part3_Level6.ldb via tools/ldb_dump.py. FACT = string read at that offset;
INFERENCE = my grouping of strings into objects/events, and the area order.

## 1. Entry (FACT)
- `::StartRoom::FSM_Startup` @23018811: gives the player `jackhammer` @23018012 and `sniper` @23018051 plus ammo (`pumpshotgun` 50, `beretta` 100, `deserteagle` 50, `ingram` 200, `grenade` 5, `mp5` 100, `jackhammer` 40, `sniper` 20) @23018084-23018351, `player->c_sethealth(1)` @23018388, fade from black @23018413, notes `P3L6a_001`..`P3L6a_004` @23018480-23018709, `graphicnovel` @23018767, `::StartRoom::post_process.DO->do_animate(1)` @23018811, objectives `OBJ_AESIR_FIND0` @23018858 and `OBJ_AESIR_FIND1` @23018919, 8 painkillers @23018980.
- Separate intro camera object: `::Outside::mover::altitude.DO` start @22953725: widescreen, fade in, `CAM_AttachToDO(100,false)` @22953844, `GM_SetPlayerControls(false)` @22953914, `player->c_setinvulnerable(true)` @22953964, gamespeed .9 @22953999, camera animate, mover animate, snowstorm on @22954267.
- The player is placed in the level only at the end of that camera: `player->c_teleport(::StartRoom::w1)` @24001058 (`::Outside::mover::altitude::camera1.DO` handler), with the camera detached @23958085, widescreen off @23958153, hud on @23958205, gamespeed 1 @23958317, `GM_SetPlayerControls(true)` @23958370, `player->c_setinvulnerable(false)` @23958419, music `ms_valkyr` @23958496.
- A third cutscene mid-level turns controls off at @24146712 and back on @24147763 (the lift/elevator ride into the towers).

## 2. Player path (FACT offsets, INFERENCE order)
| # | step | trigger / object | fires | windows | enemies |
|---|---|---|---|---|---|
| 1 | intro | `::Outside::mover::altitude` + `::StartRoom::FSM_Startup` | section 1 | controls OFF 22953914 / ON 23958370; invuln 22953964 / off 23958419; player teleported to `::StartRoom::w1` @24001058 | none |
| 2 | hatch out of the start room | `::StartRoom::sec_hatch_top` @23019313 | `sec_hatch_top.DO->DO_Animate(open)` @23019456 | none | none |
| 3 | scenic lift (use key) | `::horne03::SCENIC_LIFT_01::Action button trigger 00` @22883281 | `::horne03::SCENIC_LIFT_01.DO->DO_Animate(go_up)` @22883509, button off @22883560, dialogue timer @22883901 | the ride is an animation; controls stay on | none |
| 4 | lift exit | `::horne03::exit_lift_horne` (engine gate) | `GM_EnableExit(..., false)` @22893883, `(..., true)` @23597810: opened by the horne03 clear script | none | `horne03` |
| 5 | mona corridor | `::mona::combat_trigger` @22939087 | `::teleport::merc07->C_Teleport("::mona::Waypoint07")` @22939257, `C_GoToPlayer(1)` @22939313, `merccombat` @22939352 | none | merc07, merc06 (`::mona::Character collide trigger 00` @22939023) |
| 6 | mainframe lockdown | `::mainframe::t_enemies` @22929362 | closes `Outside::SCENIC_LIFT_03` doors @22929465/22929540 and `mainframe::door01a/02a` @22929615/22929661; `t_enemies` and `t_enemies2` off; teleports `enemy01/enemy02` to `::liftbottom02::Waypoint01a/02a` (see counter state 2 @22913558) | arena closed behind the player | mercs in mainframe |
| 7 | towers | `::mainframe::tower01::trigger01` @22931026, and tower02 @22932995, tower03 @22934925, tower04 @22936895 | each: hides beam + window, `glass_shatter`, `decal_explosion`, alarm, debris, `PS_StartEffect(explosion_small)`, `::mainframe::counter->FSM_Send(die)` (@22931778, @22933807, @22935737, @22937707), self off, `::mainframe::wait.DO->DO_Animate(1)` @22931877 (1a @22935776) | none | none |
| 8 | mainframe counter | `::mainframe::counter` states 2/3/4 @22913235, @22913281, @22914085 | state 2/3: lift doors + `door01a/02a` open @22913320-22913513, `enemy01/02` teleported and alerted @22913558-22914006; state 4: `light_control` @22914131, `panel01` hidden / `panel02` shown @22914186/22914228, `delay.DO->DO_Animate(1)` @22914316 | none | second merc wave |
| 9 | Horne's office door | `::horne2::T1` @22899221 | `::horne2::sliderA.DO->DO_Animate(open)` @22899221, `T1` and `T2` off @22899263/22899304 | none | `horne2` guards (`t_enemies` @22902101 sets e01/e02) |
| 10 | the computer | `::horne2::t_computer` @22900817, handler @22901340 | note `P3L6a_010` @22901040, `graphicnovel` @22901098, `t_lookat` off @22901142, `t_computer` off @22901189, light on @22901238, traffic lights green @22901275, `::horne2::t_end.TRIGGER->T_Enable(true)` @22901340, `::horne2::wait.DO->DO_Animate(screen)` @22901383 | none | none |
| 11 | exit | `::horne2::t_end` @22901431, T_Activate @22901623 | see section 3 | fade | none |

## 3. Exit chain (FACT)
1. The computer: `::horne2::t_computer` T_Activate @22901340 enables the exit trigger and starts `::horne2::wait.DO->DO_Animate(screen)` @22901383. Without this, `t_end` is off (`T_Enable(false)` at its own object line @22901563 and again @22901739).
2. `::horne2::t_end` T_Activate @22901623: fade to black @22901638, `::horne2::wait.DO->DO_Animate(1)` @22901703, self off @22901739, traffic light `yellow_coming` @22901783, `lift_hum_loop` @22901856.
3. `::horne2::wait.DO` @23621922 animation end: `maxpayne_gamemode->gm_init(part3_level7)` @23622996, then `::horne2::screen01.DO->DO_Hide(true)` @23623337 and `screen02.DO->DO_Hide(false)` @23623377.
4. No death counter gates the door: the mainframe counter (@22913235 etc.) and the tower triggers only open doors, spawn mercs and animate the machine room (FACT: none of their messages touch `::horne2::`).
5. The engine gate `::horne03::exit_lift_horne` @22893883/23597810 belongs to the lift between areas, not to the level end.

## 4. Bot traps
- `::horne03::SCENIC_LIFT_01::Action button trigger 00` @22883281 is an action button (use key), and its parent animates `go_up` @22883509. Sending `T_Activate` remotely rides the lift with nobody on it, and the button disables itself @22883560: the lift is then up and the button is gone.
- `::mainframe::t_enemies` and `t_enemies2` (@22929362, @22929885) are contact triggers that LOCK DOWN the room (doors close @22929465-22929661) and both disable each other. Firing one remotely closes the doors on the player's side and teleports the mercs (@22913558) into a room the player is not in.
- `::mainframe::tower01..04::trigger01` each self-disable and send `::mainframe::counter->FSM_Send(die)`; the counter is what opens the next doors. Firing them out of order still counts (one `die` each) but their explosions (`decal_explosion`, `explosion_small`) and the window destruction happen at fixed points; a remote activation destroys windows the player never sees and cannot undo.
- `::horne2::t_end` is disabled at load and only `::horne2::t_computer` @22901340 enables it. Sending `T_Activate` on `t_end` early does nothing (its own `T_Enable(false)` @22901563 is in the same object). But sending `::horne2::wait.DO->DO_Animate(1);` directly (what exits.json does) fades and calls `gm_init(part3_level7)` @23622996 with the computer never used and the screen animation skipped.
- No enemy in this level is immortal or invulnerable (FACT: the only `C_SetInvulnerable`/`C_SetImmortal` hits are the player's @22953964 and @23958419).
- `::mona::combat_trigger` @22939087 has no kill and no state change for the player: it only teleports merc07 and sets his state. A bot that treats it as "the Mona step" and skips it leaves one merc unspawned, which cannot block the exit.
- Timed/lift doors: `::mona::door_down_01`/`door_down_02` and the `::mainframe::wait.DO` chain (anim 2 @23749064, 2a @23749630, 3 @23750196) are animation-driven, not timers.

## 5. exits.json check (tools/ghost/exits.json -> part3_level6)
- `"steps": []` with five `ff` entries; all five names resolve in the file: `::horne03::SCENIC_LIFT_01::Action button trigger 00` @22883281, `::horne2::T1` @22899221, `::mainframe::t_enemies` @22929362, `::mainframe::tower01::trigger01` @22931026, `::mona::combat_trigger` @22939087.
- `ff[0]` cites `::horne03::SCENIC_LIFT_01` as `kill`: it is the lift's `.DO` object, not an enemy group; killing it is meaningless (@22883004).
- `ff[3]` kills `::mainframe::tower02..tower04`: the ff record names `::mainframe::tower01` only in `t`, while the four towers are four separate triggers @22931026/@22932995/@22934925/@22936895 and each must fire for the counter. Missing in the record: the other three triggers.
- `ff[4]` ("Mona") has no kill and no distinct gate: the script's own Mona trigger is `::mona::combat_trigger` @22939087, which only teleports `merc07`. No character object named Mona or Horne appears in this dump (searched `mona` and `horne`); the actors are `::teleport::merc06/merc07`.
- `"exit": ["::horne2::t_end.TRIGGER"]` is right as the sender @22901623 but the record omits that `t_end` is enabled only by `::horne2::t_computer` @22901340. As an exit-only route it walks past the computer, which the script requires.
- `"send": ["::horne2::wait.DO->DO_Animate(1);"]` is the exact message at @22901703 and does reach `gm_init(part3_level7)` @23622996, so the send shortcut works (INFERENCE: fade to black @22901638 skipped, so the player sees a hard cut).
- Not verified (UNKNOWN): the room order between horne03, mona, shaft01, liftbottom02 and mainframe; I read the gates but not the geometry.
