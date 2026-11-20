# part3_level2b (Cold Steel, mine / "the bomb room" second half)

Source: `build/data/lvl/1_Part3_Level2b.ldb` (24 MB). Message lines are stored as `<target>-><message>`; the sender object is
INFERENCE from the header line above each block. No enemy is immortal or invulnerable in this file
(`grep invulnerable|immortal` -> only the player, @22123121, @23370013, cleared @23370584 and @23875370).

## 1. Entry
Load-time DO (header block @23899717, message block @23899836-23900207): music `steelmill_ambience` @23899836,
**`::StartRoom::camera1->FSM_Send(start)` @23899890**, fade @23899931, objective `OBJ_COLDSTEEL_FIND` @23899989,
notes `p3l2a_007` @23900149 + `MarkAllNotesRead` @23900207.
`::StartRoom::camera1` start (header @22122689): widescreen @22122849, flash @22122900, camera attach @22122962,
gamespeed .8 @22123017, **`GM_SetPlayerControls(false)` @22123071**, **`player->c_setinvulnerable(true)` @22123121**,
`DO_Animate(1)` @22123156.
End of that camera (block @23874992-23875406): fade @23874992, detach @23875051, widescreen off @23875104, HUD on @23875156,
fade in @23875208, gamespeed 1 @23875268, **controls on @23875321**, **invuln off @23875370**, fov 1 @23875406.

## 2. Player path (ordered)
| # | step / object | what it fires | controls / invuln | enemies |
|---|---|---|---|---|
| 1 | load block @23899717 | camera1 start, objective, note | controls off @22123071, invuln @22123121 until @23875321/@23875370 | — |
| 2 | `::02_Hallway::trigger04` (header @21931152, `T_Activate` @21931360) | self off @21931292/@21931425, **`activator->c_goto(::bomb_room::waypoint04a,1)` @21931375** | moves the activator | — |
| 3 | `::02_Hallway::t1` @21930803 | `bomb_room::swing_door_dummy4::door1` `open2` @21931082 | — | — |
| 4 | `::02_Hallway::damage` @21930364 | `P_CreateProjectile(damage_fire_4x4, 8)` @21930581 | — | fire hazard |
| 5 | `::04_MineRoom::barrel08..10` (headers @21931484, @21932178, @21932769) | hide, `explosion_gasoline_huge`, `damage_fire_8x8` @21931713-21932663 | — | barrels |
| 6 | `::06_SteelMill::trigger01` (header @21967151, `T_Activate` @21967309) | `ramp01/ramp02 do_animate(close)` @21967324, `door02`/`door01` open @21967420/@21967467, self off @21967514 | — | — |
| 7 | `::06_SteelMill::trigger02` @21967629 | `mine_armed` sound @21967856, `::06_SteelMill::wait.DO->DO_Animate(1)` @21967925 | — | mine |
| 8 | `::06_SteelMill::trigger02a` @21967974 | `::06_SteelMill::enemy01->C_SendSpecial()` @21968247, `::11_Controlroom::merc_sounds->FSM_Send(start)` @21968346 | — | enemy01 |
| 9 | `::11_Controlroom::dialogue` (header @21997844, `T_Activate` @21998003) | `A_PlaySound(cold_steel, P3L2a_Max_02)` @21998018, self off @21998078 (voice only) | — | — |
| 10 | `::11_Controlroom::t_door01` (header @22006526, `T_Activate` @22006691) | `::d6::cam.DO->cam_attachtodo(8,false)` @22006706, switch `do_animate(move1)` @22006748, `::d6::grate01/grate02` open @22006855/@22006893, `wait.DO->do_animate(1)` @22006931, **HUD off @22006975**, sound P3L2a_009_cam @22007027, **objective `OBJ_COLDSTEEL_ELEVATOR` @22007088**, lookat off @22007252 | HUD off, camera | — |
| 11 | `::11_Controlroom::wait.DO` (header @23368887) | controls off @23369963, **invuln true @23370013**, widescreen @23370048, `::10_Corridor::door01 do_animate(1)` @23370102, cam01 attach @23370145; then controls on @23370535, invuln off @23370584, HUD on @23370620 | brief cutscene | — |
| 12 | `::12_Craneroom::trigger01` (header @22029693, `T_Activate` @22029906) | self off @22029921, `debris_crash_small` @22029975, `debris088` hidden @22030044, `::12_Craneroom::enemy03` crouchandshootstatic @22030091, timer `break` @22030159, `ceiling_crash_large` @22030201 | — | enemy03 |
| 13 | `::12_Craneroom::Action button trigger 09/10` @22007598/@22007881 | `mechanic_lever` @22007787 | — | — |
| 14 | `::madovens::trigger01` (header @22118789, messages @22044009-22044848) | `d_oven01..04` `DO_Animate(closed)` @22044200-22044388, `cam01 attach` @22044534 + `do_animate(1)` @22044584, `oven_doors_slam` @22044623, self off @22044794, `::16_Trap::trigger02` off @22044151 | camera (short shot, @23764977-@23765138 turns controls off then on) | — |
| 15 | `::16_Trap::trolley` (header @22044863) | `activator->c_sethealth(0)` @22045106, `activator->c_causedamage(50)` @22045652, sparks @22045921 | kills on contact | — |
| 16 | `::d6::Action button trigger 00` (header @22085784, `T_Activate` @22085953) | self off @22085968, `::d6::wait.DO->do_animate(1)` @22086028, `::d6::wait2.DO->do_animate(1)` @22086060, alarm loop @22086092, **`a_playmusic(killer_suits)` @22086154**, **`::d6::trigger.TRIGGER->T_Enable(true)` @22086202** | — | killer suits |
| 17 | `::d6::death_counter` (header @22088041) | FSM 2 @22088243 -> **`::shaft::trigger.TRIGGER->T_Enable(true)` @22088288** | — | opens the way out |
| 18 | `::shaft::trigger` (header @22121680, `T_Activate` @22121874) | self off @22121889, `door01/door02 DO_Animate(close)` @22121934/@22121975, **`GM_SetPlayerControls(false)` @22122016**, `::d6::wait2.DO->do_animate(2)` @22122067 | controls off to the end | — |
| 19 | `::d6::wait2.DO` animation 3 @23855432 | **`::shaft::lift01.DO->do_animate(down)` @23855464**, `lift_start`/`oven_doors_slam` @23855299/@23855364, elevator stop @23856130, `::Teleport::enemy08/09` to `::shaft::way2/way1` @23856192/@23856244 | — | — |
| 20 | `::shaft::wire.DO` (block @23867690-23867912) | `lift_hum_loop` @23867690, wire visible @23867752, **fade 5 s @23867788**, merc_noise @23867853, **`gm_init(part3_level3)` @23867912** | — | — |

## 3. Exit chain (FACT)
`::d6::Action button trigger 00` @22085953 -> `::d6::trigger` enabled @22086202, `killer_suits` music @22086154 ->
killing the d6 killer suits -> `::d6::death_counter` @22088041 FSM 2 @22088243 -> `::shaft::trigger.TRIGGER->T_Enable(true)`
@22088288 -> `::shaft::trigger` T_Activate @22121874 -> controls off @22122016 + `::d6::wait2.DO->do_animate(2)` @22122067 ->
`::d6::wait2.DO` animation 3 @23855432 -> `::shaft::lift01.DO->do_animate(down)` @23855464 -> `::shaft::wire` fade
@23867788 -> **`gm_init(part3_level3)` @23867912**.
The lift (`::shaft::lift01`) is the actual level transition; there is no `.TRIGGER` in the exit path.

## 4. Bot traps
1. **The exit is kill-gated.** `::shaft::trigger` starts disabled and is only enabled by the d6 death counter @22088288.
   `::shaft::trigger.TRIGGER` (@22121680) exists and is the object to activate, but only after step 16-17.
2. **`::d6::Action button trigger 00` is a use action** (@22085784/@22085953). Without it the killer suits are never spawned
   into the d6 fight and `::d6::death_counter` never reaches 2, so the exit never opens.
3. Player is teleported/moved: `activator->c_goto(::bomb_room::waypoint04a,1)` @21931375 (step 2) - with `T_Activate` from
   elsewhere the activator is the trigger object, so Max keeps his position.
4. Contact killers: `::16_Trap::trolley` sets the activator's health to 0 @22045106 and does 50 damage @22045652;
   the mad ovens close on the player (`d_oven01..04` @22044200).
5. Cameras that move the view but not Max: `::madovens::cam01` @22044534, `::11_Controlroom::wait.DO` @23369963-23370720
   (controls off ~1 s), `::d6::cam` @22006706. Remote activation plays them while Max is elsewhere.
6. No invulnerable enemy: all mercs are killable, `kill:` prefixes work.
7. Use-key `Action button` objects elsewhere in the level: `::12_Craneroom::Action button trigger 09/10` @22007598/@22007881,
   `::11_Controlroom` switch `::11_Controlroom::d_switch` @21997565.

## 5. exits.json check (`part3_level2b`)
1. **Missing exit.** `"exit": []`. Real exit path: `::shaft::trigger.TRIGGER` @22121680 (enabled @22088288) ->
   `::shaft::lift01.DO->do_animate(down)` @23855464 -> `gm_init(part3_level3)` @23867912. As written the bot cannot end it.
2. **Missing the kill gate.** No step names `::d6::Action button trigger 00` (@22085953) or the d6 killer-suit fight that
   enables `::shaft::trigger` (@22088288). Without it every later step is inert.
3. **"La sala de control" step is a voice line.** `::11_Controlroom::dialogue` (@21998003) only plays `P3L2a_Max_02`
   @21998018. The room's real beat is `::11_Controlroom::t_door01` @22006691 (camera + gates + objective
   `OBJ_COLDSTEEL_ELEVATOR` @22007088).
4. **"Los hornos" fires a camera.** `::madovens::trigger01` @22118789 is real, but its block also attaches `::madovens::cam01`
   and slams the ovens (@22044534-@22044623); activated remotely Max never sees it and the ovens shut without him.
5. Correct: `::02_Hallway::trigger04` @21931375 (`c_goto` of the activator, weak), `::06_SteelMill::trigger01` @21967309
   (ramps/doors), `::12_Craneroom::trigger01` @22029906 (debris + enemy03). `send`
   `::shaft::lift01.DO->do_animate(down);` is a real line @23855464, but it is fired by `::d6::wait2.DO`, not by a trigger.
6. All `tp` waypoints exist: `::bomb_room::waypoint06` @21921778, `::06_SteelMill::Waypoint01` @21918636,
   `::11_Controlroom::Waypoint01` @21920005, `::12_Craneroom::Waypoint04` @21920421, `::corridor::Waypoint01` @21922453.

Not verified: the room order `02_Hallway -> 04_MineRoom -> 06_SteelMill -> 10_Corridor -> 11_Controlroom -> 12_Craneroom ->
madovens -> 16_Trap/17_Corridor -> d6 -> shaft` is inferred from door/elevator names, not walked in the geometry; and which
actor count the d6 death counter needs (FSM states 2 and 3 exist, @22088243).
