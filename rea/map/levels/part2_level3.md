# part2_level3 — Part II restaurant fire, offices, sewers

Source: build/data/lvl/1_Part2_Level3.ldb, read with `python3 tools/ldb_dump.py` (script starts ~@23067091).
FACT = string at that offset, INFERENCE = my grouping. Room order is INFERENCE from the fire/door chain and
the object order in the file (StartRoom → storage → A1/R1 → k1/k2 → T1/T2 → CORRIDOR → hallway1 → HALLWAY5 →
hallway → KITCHEN_HALLWAY3 → OFFICE → 1/1-2/2 → ROOM1 → Stairs → shaft → sewer1 → sewer4).

## 1. Entry

- `::StartRoom::FSM_start` @23264622: `GM_ChangeGlobalGameSpeed(1,0)` @23264761, `mphm_fadetocolor(0x000000ff,
  0x000000ff, 0)` @23264814, `::StartRoom::post_process.DO->DO_Animate(1)` @23264879, notes P2L3a_001..004
  (@23264932, @23264990, @23265045, @23265103, @23265161) and `X_Modeswitch->S_Modeswitch(graphicnovel)`
  @23265219. FACT.
- `:street3::camera1` (@23272265): `DO_Animate(1)` @23272476, `fade_dummy.DO->DO_Animate(1)` @23272825,
  `GM_SetPlayerControls(false)` @23272516, `player->c_setinvulnerable(true)` @23272619. FACT.
- `::street3::camera1.DO` (@24641552) ends it: `gm_setplayercontrols(true)` @24643971,
  `player->c_setinvulnerable(false)` @24644061. FACT.
- No weapon is given at load. `::StartRoom::Main door locked` (@23266805) only plays
  `door_wood_locked` @23266820; `::StartRoom::explosions_start_trigger` @23245761 starts the fire FX
  (`::StartRoom::fire_dummy1->fsm_send(start)` @23245842) and `a_playmusic(rumble_flames_explosions)` @23245887.

## 2. Player path (ordered)

| # | Step: trigger / object | Fires | Windows | Enemies |
|---|---|---|---|---|
| 1 | `::StartRoom::FSM_start` @23264622 | comic notes P2L3a_001..004 @23264932..@23265219 | — | none |
| 2 | `:street3::camera1` @23272265 → `.DO` | street cutscene | controls off @23272516 + invuln ON @23272619 → restored (@24643971/@24644061) | none |
| 3 | `::storage::dialog_trigger` @23269424 | `A_PlaySound(restaurant, p2l3a_Max_02)` @23269493 | none | none |
| 4 | `::storage::combat_start_trigger` @23267977 | `do_enablecollisions(false)` @23268195, `::sewer1::script->fsm_Send(start)` @23268261, `::R1::fire_dummy1->fsm_Send(stop)` @23268298, `::storage::rat_controller1->FSM_Send(stop)` @23268335, `::sewer1::rat_controller2->Fsm_Send(start)` @23268381, `gm_changefog(0x15301a, 15, 50, 0)` @23268427 | none | sets up sewer1::e1..e4 |
| 5 | `::storage::combat_script` @23267184 | `:A1::e1->c_gotoplayer(1)` @23267360, `mobstercombat` @23267388, `delay1s` @23267434, `:storage::e1->c_sethealth(0)` @23267508, `:storage::e2->c_sethealth(0)` @23267541, `::storage::service_shaft_door.DO->do_hide(true)` @23267573, `service_shaft_door2.DO->do_hide(false)` @23267680, `::A1::e1->C_GoToAndShoot("::storage::w1",1)` @23267750 | none | A1::e1 attacks; storage::e1/e2 die by script |
| 6 | `::storage::death_counter` @23268549 | `fsm_Send(add1)` @23319774 / `add3` @23314514, `::storage::e1->CAM_AnimateParented(death_03)` @23268874, `e2` @23269045, `A1::e1` @23269216 | none | none |
| 7 | `::A1::pt1` @23129741 | `::A1::dialogue->FSM_Send(start)` @23129756 | none | none |
| 8 | `::A1::t1/t2/t3/t4` @23129939 / @23130213 / @23130487 / @23130724 | each opens the facing door: `::A1::door1.DO->DO_Animate(open2)` @23130028, `door4` @23130302, `door2` @23130539, `door3` @23130776; each trigger disables its opposite (`::R1::t4` @23129991, `::R1::t3` @23130265) | none | none |
| 9 | `::R1::t3/t4` @23233230 / @23233504 | `::A1::door4.DO->DO_Animate(open1)` @23233319, `::A1::door1.DO->DO_Animate(open1)` @23233593 | none | none |
| 10 | `::R1::Gas_Bottle_small4/small5` (ignite) @23226556 / @23228558, `::R1::ignite_trigger(01)` @23229785 / @23230011, `::R1::switch_trigger(01)` @23232768 / @23233005 | bottle explosions, wall switches | none | none |
| 11 | `::k1::t3/t4` @23167312 / @23167631, `::k2::t1/t2` @23173600 / @23173874 | door pairs open/close (`::k1::door2.DO->do_animate(open1)` @23167446, `door1` @23167765, `open2` @23173689, @23173963) | none | none |
| 12 | `::T1::explode_the_doors` @23275353 | `::k2::fire_dummy1->fsm_send(start)` @23275420, `::k2::kill_trigger.TRIGGER->t_enable(true)` @23275458 | fire kill volume ON @23275458 | none |
| 13 | `::T1::t1/t2` @23295566 / @23295840, `::T2::t3/t4` @23313808 / @23314082, `::T2::Door::t1` @23298151 | `::T2::door1.DO->do_animate(open2)` @23295655, `::T2::door2` @23295929, `::T2::door2.DO->DO_Animate(open1)` @23313897, `::T2::door1` @23314171, `::T2::Door.DO->do_animate(open1)` @23298209 | none | none |
| 14 | `::T2::shelf02/03/04` @23311947 / @23312290 / @23312633 | `activator->c_sethealth(0)` (crush) | — | none |
| 15 | `::KITCHEN_HALLWAY3::kill_flames_trigger` @23192747 | `::OFFICE::flame01->fsm_send(start)` @23192830, `::OFFICE::fire_dummy1->fsm_send(stop)` @23192868, `::OFFICE::kill_trigger.TRIGGER->t_enable(true)` @23192909 | fire kill volume ON @23192909 | none |
| 16 | `::OFFICE::door3::t2/t3` @23196179 / @23196508 | `DO_Animate(open2)` @23196290 / `open1` @23196619 | none | none |
| 17 | `::2::flame_stop_trigger` @23115546 | `::1-2::fire_dummy2->fsm_send(start)` @23115613, `::1-2::kill_trigger.TRIGGER->t_enable(true)` @23115652, `::1::fire_dummy1->fsm_send(stop)` @23115699, `::2::fire_dummy1->fsm_send(start)` @23115761 | fire kill volume ON @23115652 | none |
| 18 | `::1::start_explosions_trigger` @23094673 | `::1::fire_dummy1->fsm_send(start)` @23094688 | none | none |
| 19 | `::1-2::trigger_flames` @23078918 | `::CORRIDOR::kill_trigger2.TRIGGER->t_enable(true)` @23078983, flame dummies @23079079..@23079209 | kill volume ON @23078983 | none |
| 20 | `::CORRIDOR::hallway_trigger` @23147736 | `::CORRIDOR::kill_trigger1.TRIGGER->t_enable(true)` @23147807, `::CORRIDOR::fire_dummy2->fsm_send(start)` @23147860, `::T2::fire_dummy->fsm_send(stop)` @23147904 | kill volume ON @23147807 | none |
| 21 | `::hallway1::spread_flames_trigger` @23154066 | `::hallway1::kill_trigger.TRIGGER->t_enable(true)` @23154081, `::hallway1::fire_dummy1->fsm_send(start)` @23154133 | kill volume ON @23154081 | none |
| 22 | `::HALLWAY5::Door::t1` @23157007, `::HALLWAY5::door_locked_trigger` @23157269 | `Door.DO->do_animate(open1)` @23157056, switch sound @23157284 | none | none |
| 23 | `::hallway::hallway_explosion_trigger` @23165664 | `::hallway::fire_dummy1->fsm_send(start)` @23165744 | none | none |
| 24 | `::KITCHEN_HALLWAY3::fire_dummy1...trolley::damage_trigger` @23182067 / @23185772 | `activator->c_causedamage(20)` @23182178 / @23185883 | damage 20 per touch | none |
| 25 | `::T1::fire_dummy1...trolley::damage_trigger` @23286874 / @23289920 | `activator->c_causedamage(10)` @23286971 / @23290017 | damage 10 per touch | none |
| 26 | `::Stairs::ct1` @23245539 | `Activator->C_SendSpecial()` @23245555 | none | whoever walks over it |
| 27 | `::sewer1::ct1/ct2` @23238430 / @23238628 | `activator->c_sendspecial()` @23238446 / @23238644 | none | whoever walks over it |
| 28 | `::sewer1::script` @23242069 (state @23242535) | `::sewer1::e1..e4->C_SetStateMachine(idleuntilenemyseen)` @23242270..@23242435, `::sewer1::e1->c_goto("::sewer1::w1",.5)` @23242490, `e2/e3->C_GoToAndShoot` @23242592/@23242643 + `mobstercombat` @23242694/@23242744 | none | sewer1::e1..e4 |
| 29 | `::sewer1::e5_dodge_trigger` @23239793 | `::sewer1::e5->c_setstatemachine(mobstercombat)` @23239863 + `singledodgeright` @23239913 | none | sewer1::e5 |
| 30 | `::sewer1::e5_in_position` @23240130 | `::sewer1::script2->fsm_send(start)` @23240198 | none | none |
| 31 | `::sewer1::script2` @23242818 | `::sewer1::e5->c_goto("::sewer1::w7",1)` @23243010, `e6->C_GoTo("::sewer1::w10",1)` @23243108, `e6->C_GoToAndShoot("::sewer1::w8",1)` @23243335 + `mobstercombat` @23243386, `::sewer5::script->fsm_Send(start)` @23243501 | none | sewer1::e5, e6; wakes sewer5 |
| 32 | `::sewer5::script` @23244103 | `::sewer5::e1/e2->C_SetStateMachine(idle)` @23244294/@23244335, then `crouchandshootstatic` @23244511, `mobstercombat` @23244568, `c_gotoandshoot("::sewer1::w11")` @23244618, `"::sewer1::w12"` @23244670 | none | sewer5::e1, e2 walk into sewer1 |
| 33 | `::sewer1::counter` @23237974 | `fsm_Switch(2)` @23238191 / `(3)` @23238234 + `fsm_Send(Add)` @23314999.. (death counter) | none | none |
| 34 | `::shaft::grenade_detect1` @23244736 | `::shaft::grenade_detect2.DO->do_hide(false)` @23244908 | none | none |
| 35 | `::sewer4::door1::t_door1` (T_Activate @23243988) | self-disable @23244003, `::sewer4::door1.DO->do_animate(open)` @23244056 | — | none |

## 3. Exit

`::sewer4::door1::t_door1.TRIGGER` (T_Activate @23243988) is enabled at load; it disables itself
(@23244003) and runs `::sewer4::door1.DO->do_animate(open)` @23244056. `:sewer4::door1.DO` @24580526:
`A_Play3DSound(dynamic, door_wood_open)` @24581906, `FadeToColor(0, 0x000000ff, .5)` @24581969,
`GM_SetPlayerControls(false)` @24582035, `GM_SetPlayerControls(true)` @24582091, note P2L3a_005 @24582140,
`x_modeswitch->s_modeswitch(graphicnovel)` @24582198, `gm_init(part2_level4)` @24582242. No death counter and
no enemy gates the exit: the door opens for anyone who reaches it. FACT.

## 4. Bot traps

- The level is a fire corridor, and the fire is lethal by script: every `*::kill_trigger` runs
  `activator->c_sethealth(0)`. They are enabled progressively: `::1-2::kill_trigger` @23078713 (ON @23115652),
  `::2::kill_trigger` @23116017, `::CORRIDOR::kill_trigger1/2` @23148159/@23148422 (ON @23147807/@23078983),
  `::hallway1::kill_trigger` @23153849 (ON @23154081), `::HALLWAY5::kill_trigger` @23159495,
  `::k2::kill_trigger` @23173408 (ON @23275458), `::OFFICE::kill_trigger` @23214980 (ON @23192909),
  `::R1::kill_trigger` @23230282, `::ROOM1::kill_trigger` @23237923, `::T1::kill_trigger` @23295374
  (ON @23313540). Standing in one is instant death, and the volume is enabled a room ahead of the flames.
- Damage volumes: `::KITCHEN_HALLWAY3::...trolley::damage_trigger` @23182178/@23185883 (`c_causedamage(20)`)
  and `::T1::...trolley::damage_trigger` @23286971/@23290017 (`c_causedamage(10)`); `::T2::shelf02/03/04`
  @23311947.. run `c_sethealth(0)`.
- Enemies killed by the script, not by the player: `:storage::e1` @23267508 and `:storage::e2` @23267541 die
  inside `::storage::combat_script`. A remote activation of `::storage::combat_script` removes them and hides
  `::storage::service_shaft_door` (@23267573) with no player present.
- No enemy is immortal or invulnerable: 0 hits for `c_setimmortal`/`c_setinvulnerable` in this file. The
  player is invulnerable only during the opening street camera (@23272619 → @24644061).
- Trap for a walker: the exit needs nothing but the door; a bot that teleports to `::sewer4::door1` skips the
  whole fire maze (and the note P2L3a_005 still plays at @24582140).

## 5. exits.json check

Entry `part2_level3`: `exit` `::sewer4::door1::t_door1.TRIGGER`, `next` part2_level4, no `send`.

1. `exit` is right (@23243813 name, T_Activate @23243988). The `why` text is accurate (@23243988 table,
   @24580526 door, @24582242 gm_init).
2. WRONG ff[0] "El almacén": `::storage::dialog_trigger` (@23269424) only plays `p2l3a_Max_02` (@23269493).
   The room's combat is `::storage::combat_script` (@23267184, kills `storage::e1/e2` itself at @23267508/
   @23267541 and wakes `::A1::e1` at @23267360) plus `::storage::combat_start_trigger` (@23267977). `kill
   ::storage` is therefore a kill the player never performs.
3. WRONG ff[1] "Las cloacas": `::sewer1::ct1` (@23238430) only runs `activator->c_sendspecial()`
   (@23238446). The wave is `::sewer1::script` (@23242069), sent by `::storage::combat_start_trigger`
   (@23268261).
4. WRONG ff[2] "La segunda oleada": `::sewer1::ct2` (@23238628) again only sends the special (@23238644). The
   second wave is `::sewer1::e5_in_position` (@23240130) → `::sewer1::script2` (@23240198).
5. WRONG ff[3] "El sótano A1": `::Stairs::ct1` (@23245539) only `C_SendSpecial()` (@23245555); `::A1::e1` is
   woken by `::storage::combat_script` (@23267360), never by that trigger.
6. MISSING: the third wave `::sewer5::script` (@23244103), sent from `::sewer1::script2` (@23243501), sends
   `::sewer5::e1/e2` into `::sewer1::w11/w12` (@23244618, @23244670).
7. MISSING: the kill volumes listed in section 4 — a `tp`-based route must not teleport into one, and no
   entry records them.

Errors counted: 6.
