# part2_level2 — Part II docks yard, containers, bridge, warehouse

Source: build/data/lvl/1_Part2_Level2.ldb, read with `python3 tools/ldb_dump.py` (script starts ~@21304932).
FACT = string at that offset, INFERENCE = my grouping. Room order is INFERENCE from the `trigger_noise`
music chain (ware1 → yard1/yard2 @21402901/@21402953; ware2 → yard2/yard3 @21413320/@21413372) and from
the door/exits pairs.

## 1. Entry

- `:StartRoom::fsm_start` (@21399885): notes P2L2a_001..006 (@21400089, @21400147, @21400202, @21400260,
  @21400318) and `x_modeswitch->s_modeswitch(graphicnovel)` @21400376. FACT.
- `:StartRoom::camera1` (@21398391): `GM_SetPlayerControls(false)` @21398825, `player->c_setinvulnerable(true)`
  @21398875. `:StartRoom::Floating FSM 00` disables the room exits (`GM_EnableExit(::StartRoom::Exit_01,
  false)` @21399736, `GM_EnableExit(::Teleport::Exit_02, false)` @21399803). FACT.
- `::StartRoom::camera1.DO` (@22439440) ends it: `GM_SetPlayerControls(true)` @22441962,
  `player->c_setinvulnerable(false)` @22442011. FACT.
- No weapon is given to the player at load (no `player->c_pickupweapon` in the file); the first one is the
  sniper rifle on the containers (@21392428).

## 2. Player path (ordered)

| # | Step: trigger / object | Fires | Windows | Enemies |
|---|---|---|---|---|
| 1 | `::StartRoom::camera1` @21398391 → `.DO` @22439440 | intro cutscene | controls off @21398825 + invuln ON @21398875 → both restored (@22441962/@22442011) | none |
| 2 | `::01_Entryway::Action button trigger 00` @21305105 (use key) | lever sound @21305120, `::01_Entryway::gate.DO->DO_Animate(1)` @21305207, self-disable @21305248, `::yard1::enemy02->C_GoToPlayer(1)` @21305316, `enemy03` @21305353, `mobstercombat` @21305390, `crouchandshoot` @21305444 | none | yard1::enemy02, enemy03 |
| 3 | `::01_Entryway::New mesh 08::t_enemy_sound` @21308500 | `::01_Entryway::New mesh 08::enemy01->A_Play3DSound(mobster_noise)` @21308515 | none | 01_Entryway::New mesh 08::enemy01 |
| 4 | `::ware1::Action button trigger 00` @21402021 (use key) | `::yard1::door01->fsm_send(move)` @21402036 | none | none |
| 5 | `::yard1::action_button_trigger` @21413594 (use key) | `::yard1::door01->fsm_send(move)` @21413609 | none | none |
| 6 | `::yard2::Action button trigger 00` @21417151 (use key) | `::ware2::door01->fsm_send(move)` @21417166 | none | none |
| 7 | `::ware2::Action button trigger 00/01/02` @21403178/@21403488/@21403711 (use key) | `::ware2::door01->fsm_send(move)` @21403193; `::ware2::lift01->fsm_send(move)` @21403503/@21403726 | timed lift/wall | none |
| 8 | `::ware2::Character collide trigger 00` @21405573 (fires only when a **character** walks over it) | `activator->c_setstatemachine(standandshootstatic)` @21405588, `::ware2::enemy01->C_PickupWeapon(grenade)` @21405707 | none | ware2::enemy01 |
| 9 | `::yard2::swing_door_dummy1::door1::t1` @21424289 | `open2` @21424429, `::yard2::enemy02->C_GoTo("::yard2::Startpoint01",.5)` @21423924, `::yard2::e3->C_SetStateMachine(idle)` @21424026 | none | yard2::enemy02, e3 |
| 10 | `::yard3::Player collide trigger 03` @21435519 | self-disable @21435534, `::yard3::enemy07->C_GoTo("::yard3::waypoint07a",1)` @21435597, `mobstercombat` @21435652, `::Teleport::enemy09->C_Teleport("::yard3::Startpoint09")` @21435706, `enemy08` @21435766, `enemy09` combat+gotoplayer @21435826/@21435883 | none | yard3::enemy07; ::Teleport::enemy08/09 teleported in |
| 11 | `::yard3::Action button trigger 00` @21425649 (use key) | `::yard3::switch.DO->do_animate(open)` @21425664, self-disable @21425704, `::yard3::T_Crane_Fall.TRIGGER->T_Enable(true)` @21425766 | none | none |
| 12 | `::yard3::T_Crane_Fall` @21439546 (T_Activate @21439814) | `::yard3::container02.DO->DO_Animate(down)` @21439561, self-disable @21439606 | none | none |
| 13 | `::09_YardCorridor::ct4` @21327528 | `activator->c_gotoandshoot(::yard3::wp3, 1)` @21327543, `mobstercombat` @21327589 | none | whoever walks over it |
| 14 | `::containers01::Player collide trigger 00` @21392052 | `A_PlaySound(docks, p2l2a_Max_01)` @21392067 | none | none |
| 15 | `::containers01::sniper` @21392413 | `::containers01::sniper_gun->LI_ActivateLevelItem(true)` @21392428, `mphm_printtip(TIP_SNIPER)` @21392537 | none | none |
| 16 | `::containers01::trigger01` @21393494 | note p2l2a_007 @21393564 + graphicnovel @21393622, `::containers01::trigger04->fsm_switch(active)` @21393666 | none | none |
| 17 | `::containers01::trigger3` @21394361 / `::containers02::trigger1` @21398216 | `::containers02::door01.DO->do_animate(open)` @21394376 / @21398231 | none | none |
| 18 | `::10_Containers1::Player collide trigger 00` @21330535 | `::11_Containers2::enemy02->c_gotoandshoot("::11_Containers2::Waypoint02",1)` @21330622 | none | 11_Containers2::enemy02 |
| 19 | `::10_Containers1::trigger01` @21333098 | `DO_Animate(1)` on `::10_Containers1::truck01.DO` @21333252, `cam_animateinplace(shake_quick)` @21333299 | none | none |
| 20 | `::11_Containers2::Character collide trigger 00` @21341753 (character only) | `::11_Containers2::truck01::enemy01->c_setstatemachine(crouchandshootstatic)` @21341768, truck `DO_Animate(1)` @21341922 | none | 11_Containers2::truck01::enemy01 |
| 21 | `::11_Containers2::Character collide trigger 01` @21342236 (character only) | `::11_Containers2::enemy02->C_PickupWeapon(grenade)` @21342380 + `C_PickupAmmo` @21342434 | none | 11_Containers2::enemy02 |
| 22 | `::11_Containers2::Player collide trigger 00` @21342996 | `::15_Backdrop::enemy06->C_GoToPlayer(1)` @21343083, `::containers02::e5->C_GoToPlayer(1)` @21343126, `crouchandshoot` @21343165, `mobstercombat` @21343226 | none | 15_Backdrop::enemy06, containers02::e5 |
| 23 | `::12_Backdrop::Player collide trigger 00` @21349057 | `::15_Backdrop::enemy03->C_SendSpecial()` @21349072 | none | 15_Backdrop::enemy03 |
| 24 | `::15_Backdrop::exit trigger` @21350079 | `::containers02::enemy01->c_goto("::16_Bridge::Startpoint01",1)` @21350094, exits off @21350161..@21350346, `containers02::enemy04->C_GoToPlayer(1)` @21350479 | none | containers02::enemy01, enemy04 |
| 25 | `::16_Bridge::trigger01` @21357777 | `::Teleport::enemy05->c_teleport("::containers02::waypoint")` @21357792, `enemy06` → `Startpoint06` @21357855, both `C_SendSpecial` @21358026/@21358066 | none | ::Teleport::enemy05/06 into containers02 |
| 26 | `::16_Bridge::Player collide trigger 00` @21354800 | `::18_BridgeOffice::enemy01->c_goto("::5::waypoint03",1)` @21354882, `::20_Warehouse::enemy05->C_GoToPlayer(1)` @21354942 + `crouchandshoot` @21354986 | none | 18_BridgeOffice::enemy01, 20_Warehouse::enemy05 |
| 27 | `::16_Bridge::Player collide trigger 01/02/03` @21355296/@21355594/@21355892 | `mphm_fadetocolor` @21355311, `::16_Bridge::wait.DO->do_animate(1)` @21355377 | none | none |
| 28 | `::18_BridgeOffice::cam_trigger` @21360340 | `::19_BridgeOfficeBackRoom::cam01.DO->CAM_AttachToDO(2,false)` @21360355, `rec.DO->DO_Hide(false)` @21360420, `::18_BridgeOffice::wait.DO->DO_Animate(1)` @21360473 | `::18_BridgeOffice::wait.DO` @22335284: controls off @22335284 + invuln ON @22335334 → controls on @22335479 + invuln OFF @22335528 | none |
| 29 | `::18_BridgeOffice::trigger` @21370800 | `::18_BridgeOffice::door.DO->do_animate(open)` @21370815, `switch.DO->do_animate(1)` @21370918 | none | none |
| 30 | `::19_BridgeOfficeBackRoom::Player collide trigger 00` @21379265 | `::18_BridgeOffice::trigger01.TRIGGER->T_Enable(true)` @21379280 | none | none |
| 31 | `::18_BridgeOffice::trigger01` @21371204 | `::Teleport::enemy12->C_Teleport("::5::waypoint03")` @21371219, `C_SendSpecial` @21371273 | none | ::Teleport::enemy12 |
| 32 | `::5::Action button trigger 00` @21380462 (use key) | lever sound @21380477 only | none | none |
| 33 | `::5::trigger_noise02` @21387005 / `::18_BridgeOffice::trigger_noise01` @21371544 | music `docks` @21387020, re-enable @21387110/@21371676 | none | none |
| 34 | `::20_Warehouse::Player collide trigger 01` @21380216 | `::20_Warehouse::End_Controller.DO->DO_Animate(animation_0)` @21380231 | none | none |

## 3. Exit

`::20_Warehouse::Player collide trigger 01` (T_Activate @21380216) → `::20_Warehouse::End_Controller.DO->
DO_Animate(animation_0)` @21380231 → that animation runs at @22350857: `MPHM_FadeToColor(0x0, 0x00000ff, 1)`
@22352032 → `maxpayne_gamemode->gm_init(part2_level2b)` @22352095. No comic, no note, no death counter: the
exit is a plain "walk into the last room" trigger. FACT.

## 4. Bot traps

- Player invulnerable: @21398875 (intro camera) off at @22442011; @22335334 (BridgeOffice cutscene) off at
  @22335528. Both are restored inside a `.DO` animation, so skipping the animation keeps the flag set.
- Use key: `::01_Entryway::Action button trigger 00` @21305105 (opens the gate **and** wakes yard1),
  `::ware1::Action button trigger 00` @21402021, `::yard1::action_button_trigger` @21413594,
  `::yard2::Action button trigger 00` @21417151, `::ware2::Action button trigger 00/01/02`
  @21403178/@21403488/@21403711 (doors and lift), `::yard3::Action button trigger 00` @21425649,
  `::5::Action button trigger 00` @21380462. Without the use key the doors of yard1/yard2/ware2/yard3 never
  move (`fsm_send(move)`), so the walk is blocked even though nothing looks locked.
- Character-only triggers the player cannot fire: `::ware2::Character collide trigger 00` @21405573,
  `::11_Containers2::Character collide trigger 00` @21341753 and `01` @21342236. Teleporting past them leaves
  `::11_Containers2::truck01::enemy01` and `::ware2::enemy01` in their idle state.
- Teleports that put enemies in rooms a player never sees: @21435706 (`::Teleport::enemy08/09` →
  yard3), @21357792/@21357855 (`enemy05/06` → containers02), @21371219 (`enemy12` → `::5::waypoint03`).
  Activating `::16_Bridge::trigger01` (@21357777) remotely still performs them.
- No enemy is immortal or invulnerable: 0 hits for `c_setimmortal`/`c_setinvulnerable` in this file; the
  player flag is the only one.

## 5. exits.json check

Entry `part2_level2`: `exit` `::20_Warehouse::Player collide trigger 01.TRIGGER`, `send`
`::20_Warehouse::End_Controller.DO->DO_Animate(animation_0);`, `next` part2_level2b.

1. `exit` and `send` are right (@21380041, @21380231). FACT.
2. WRONG ff[4] "El almacén": `::ware2::Character collide trigger 00` (@21405573) is a **character** collide
   trigger — its own message is `activator->c_setstatemachine(standandshootstatic)` @21405588 — so the player
   walking through it fires nothing.
3. WRONG kill in ff[1] "Los contenedores": `::11_Containers2::Player collide trigger 00` (@21342996) wakes
   `::15_Backdrop::enemy06` (@21343083) and `::containers02::e5` (@21343126), not the `::11_Containers2`
   enemies. The listed kill misses those two, and also `::containers02::enemy04` (@21350479) and
   `::containers02::enemy01` (@21350094).
4. MISSING steps: the two `::11_Containers2::Character collide trigger 00/01` (@21341753, @21342236) that
   wake `::11_Containers2::truck01::enemy01` and give `enemy02` a grenade.
5. MISSING kill in ff[2] "El puente": `::16_Bridge::Player collide trigger 00` (@21354800) also wakes
   `::18_BridgeOffice::enemy01` (@21354882); and `::16_Bridge::trigger01` (@21357777) sends `::Teleport::
   enemy05/06` into containers02 — neither is in the entry.
6. MISSING kill in ff[3] "El patio de la grúa": `::Teleport::enemy08` and `::Teleport::enemy09` are
   teleported into yard3 by the same trigger (@21435706, @21435766).
7. MISSING step: `::containers01::sniper` (@21392413) is the only way to get the sniper rifle (and
   `TIP_SNIPER` @21392537) before the bridge; also `::5::Action button trigger 00` (@21380462).

Errors counted: 7.
