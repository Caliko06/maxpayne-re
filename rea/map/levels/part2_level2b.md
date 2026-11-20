# part2_level2b — Part II docks: warehouses, containers, ship, stairs

Source: build/data/lvl/1_Part2_Level2b.ldb, read with `python3 tools/ldb_dump.py` (script starts ~@22920842).
FACT = string at that offset, INFERENCE = my grouping. Room order is INFERENCE from the door/exit pairs.

## 1. Entry

- `::StartRoom::FSM_Start` @23042619 → `::StartRoom::FSM_Start.DO->do_animate(animation_0)` @23042758,
  notes P2L2b_001 @23042932 and `MPGNM_MarkAllNotesRead()` @23042990. FACT.
- `::StartRoom::camera1` (@23040515, event `start` @23040665): `GM_EnableWidescreen(true,0)` @23040675,
  HUD hidden @23040726, `CAM_AttachToDO(100,false)` @23040840, speed .8 @23040895,
  `GM_SetPlayerControls(false)` @23040949, `player->c_setinvulnerable(true)` @23040999. FACT.
- `::StartRoom::camera1.DO` ends it: `GM_SetPlayerControls(true)` @24624369,
  `player->c_setinvulnerable(false)` @24624418. FACT.
- No weapon is handed to the player at load in this file (no `player->c_pickupweapon(player`).

## 2. Player path (ordered)

| # | Step: trigger / object | Fires | Windows | Enemies |
|---|---|---|---|---|
| 1 | `::StartRoom::FSM_Start` @23042619 | comic note P2L2b_001 @23042932 | — | none |
| 2 | `::StartRoom::camera1` @23040515 → `.DO` | intro cutscene | controls off @23040949 + invuln ON @23040999 → restored (@24624369/@24624418) | none |
| 3 | `::StartRoom::P01` @23053425 | `::StartRoom::enemy_naks4->C_goto("::StartRoom::Waypoint04",1)` @23053440 + `mobstercombat` @23053506, `::StartRoom::E01->c_goto("::StartRoom::W01",1)` @23053568, `::StartRoom::StartCombat.DO->DO_Hide(true)` @23053664 | none | StartRoom::enemy_naks4, E01 |
| 4 | `::StartRoom::Player collide trigger 00` @23053888 | `::StartRoom::enemy01->C_GoToAndShoot("::StartRoom::Startpoint01",1)` @23053970 + `crouchandshootstatic` @23054042; `enemy02` same on `Startpoint02` @23054107/@23054179 | none | StartRoom::enemy01, enemy02 |
| 5 | `::StartRoom::Player collide trigger 03` @23054704 | `::StartRoom::e3->C_GoToAndShoot("::StartRoom::Startpoint03",1)` @23054786 + `mobstercombat` @23054853, `::StartRoom::grenade->p_createprojectile(grenade,1)` @23054906, `enemy02->C_GoToPlayer(1)` @23054962 | none | StartRoom::e3 |
| 6 | `::StartRoom::trigger04` @23058413 | `::Teleport::enemy05->c_teleport("::StartRoom::Waypoint05")` @23058479, `enemy06` → `Waypoint06` @23058541 | none | ::Teleport::enemy05/06 |
| 7 | `::StartRoom::ct1` @23041980 | `Activator->C_SendSpecial()` @23041996 (only a special to whoever walks over it) | none | whoever walks over it |
| 8 | `::02_WareHouse::Player collide trigger 00` @22942531 | `::02_WareHouse::enemy03->C_GoTo("::02_WareHouse::waypoint03",.5)` @22942546, `enemy04->C_GoTo("waypoint01")` @22942685 | none | 02_WareHouse::enemy03, enemy04 |
| 9 | `::02_WareHouse::Player collide trigger 01` @22942935 | `::03_SmallWareHouse::enem01->C_GoTo("::02_WareHouse::waypoint01",1)` @22942950 | none | 03_SmallWareHouse::enem01 walks into 02_WareHouse |
| 10 | `::03_SmallWareHouse::Action button trigger 00` @22947955 (use key) | `::02_WareHouse::gate.DO->DO_Animate(1)` @22947970, `::02_WareHouse::cam02.DO->CAM_AttachToDO(4,false)` @22948012, `::03_SmallWareHouse::wait.DO->DO_Animate(1)` @22948140, lever sound @22948187, `::Teleport::enemy02/03->C_GoToPlayer(1)` @22948280/@22948320 + `C_Teleport` to `02_WareHouse::Waypoint02/waypoint03` @22948475/@22948540 | `::03_SmallWareHouse::wait.DO` @24235420: controls off @24235420 + invuln ON @24235470 → controls on @24235563 + invuln OFF @24235612 | ::Teleport::enemy02, enemy03 |
| 11 | `::02_WareHouse::Player collide trigger 02` @22943273 | `::02_WareHouse::wait.DO->DO_Animate(1)` @22943358, `::02_WareHouse::cam01.DO->CAM_AttachToDO(2,false)` @22943400, `GM_TransferPlayerWeaponTo(::Teleport::max)` @22943454, `mphm_printobjective(OBJ_DOCKS_WIN,0)` @22943519 | — | none |
| 12 | `::02_WareHouse::wait.DO` @23819353 | `::Teleport::max->C_Teleport("::02_WareHouse::Waypoint02")` @23819353, `player->c_teleport(::Teleport::Waypoint01)` @23819414, `gm_setplayercontrols(false)` @23819502, `x_modeswitch->s_modeswitch(graphicnovel)` @23820627, `player->c_teleport(::02_WareHouse::Waypoint02)` @23820671, `max → ::Teleport::Waypoint01` @23820721, `gm_setplayercontrols(true)` @23820778 | controls off @23819502 → on @23820778 | none |
| 13 | `::cr::Player collide trigger 00` @22988653 | `::cr::ship01.DO->do_animate(move)` @22988668 | none | none |
| 14 | `::cr::Player collide trigger 02/04/05` @22988936 / @22989343 / @22989697 | `::Teleport::enemy17->c_teleport("::cr::teleport2")` @22989011, `enemy18` @22989065, `enemy16` @22989119; `enemy12/13` @22989358/@22989412; `enemy15/14` @22989712/@22989826 | none | ::Teleport::enemy12..18 teleported into cr |
| 15 | `::cr::trigger3` @22999025 | `::cr::ride.DO->do_animate(3)` @22999101, `::cr::sniper01->C_GoTo("::cr::snipe01",.5)` @22999171 + `standandshootstatic` @22999218 | none | cr::sniper01 |
| 16 | `::cr::trigger4/5/7` @22999820 / @23000557 / @23002037 | `::cr::sniper02->C_GoTo("::cr::snipe02")` @22999966, `sniper03` @23000703, `sniper04` @23002183 | none | cr::sniper02, sniper03, sniper04 |
| 17 | `::ship::Player collide trigger 00` @23025528 | `::ship::note_trigger->A_Play3DSound(dynamic, phone_ringing_old)` @23025605 | none | none |
| 18 | `::ship::note_trigger` @23025046 | note P2L2b_003 @23025116 + graphicnovel @23025174, `::ship::look.TRIGGER->t_Enable(false)` @23025218 | none | none |
| 19 | `::ship::Character collide trigger 01` @23020526 (character only) | `::Teleport::enemy09->c_gotoplayer(1)` @23020606 + `mobstercombat` @23020646 | none | ::Teleport::enemy09 |
| 20 | `::ship::Character collide trigger 02` @23020926 (character only) | `::ship::enemy01->c_setstatemachine(crouchandshootstatic)` @23020994 | none | ship::enemy01 |
| 21 | `::ship::trigger` @23029519 | `::Teleport::enemy08->C_SetStateMachine(standandshoot)` @23029534, `activator->c_gotoplayer(1)` @23029592, `::Teleport::enemy11->C_SetStateMachine(standandshoot)` @23029681 | none | Teleport::enemy08, enemy11 |
| 22 | `::ship::Action button trigger 00` @23015441 (use key) | `::06_ShipCorridor::door01.DO->do_animate(open)` @23015456 | none | none |
| 23 | `::06_ShipCorridor::Player collide trigger 00/01` @22983298 / @22983769 | `::staircase01::enemy02->C_goto("::staircase01::waypoint01",1)` @22983313, `enemy04` @22983452, `enemy05` @22983519, `enemy03` @22983784 | none | staircase01::enemy02..05 |
| 24 | `::09_ShipTreasure01::Player collide trigger 00` @22984073 | `::staircase01::end_trigger.TRIGGER->t_enable(true)` @22984088 | — | none |
| 25 | `::staircase01::Action button trigger 00` @23033556 (use key) | `::staircase01::door02.DO->do_animate(open)` @23033571, `::ship::note_trigger` sounds off @23033685 | none | none |
| 26 | `::staircase01::Action button trigger 01` @23034001 | `door_metal_locked` @23034016 (dead end) | none | none |
| 27 | `::end_combat::trigger` @23013826 | `::end_combat::door01.DO->do_animate(open)` @23013841, `A_PlaySound(docks, p2l2a_07)` @23013936, `mphm_printobjective(OBJ_DOCKS_MEETING,0)` @23013987 | none | none |
| 28 | `::end_combat::wait2.DO` @24514597 | `::end_combat::boris->C_SetStateMachine(nonreactive)` @24514597, `C_GoTo("::end_combat::Waypoint01",1)` @24514652, `::end_combat::boris->C_SetInvulnerable(true)` @24514714, `::end_combat::enemy02->C_SendSpecial()` @24514762, `enemy03->C_goto("Waypoint03")` @24514804, `enemy01/02/03` combat @24514868..@24514989 | Boris invulnerable ON @24514714 | end_combat::enemy01, enemy02, enemy03 (last wave) |
| 29 | `::end_combat::trigger02` @23014556 | `::end_combat::boris->C_SetInvulnerable(false)` @23014677 | Boris invulnerable OFF @23014677 | none |
| 30 | `::staircase01::end_trigger` (T_Activate @23035972) | self-disable @23035901, `::staircase01::wait.DO->do_animate(1)` @23035987 | — | none |

## 3. Exit

`::staircase01::end_trigger.TRIGGER` (T_Activate @23035972) → `::staircase01::wait.DO->do_animate(1)`
@23035987 → `::staircase01::wait.DO` @24613267: `gm_setplayercontrols(false)` @24613267,
`GM_EnableExit(::staircase01::exit_to_cargo, true)` @24613368, then the next animation block
`x_modeswitch->s_modeswitch(graphicnovel)` @24614946 (note P2L2b_004 @24614888) and
`maxpayne_gamemode->gm_init(part2_level3)` @24614990. No death counter: plain trigger chain. FACT.

## 4. Bot traps

- The exit trigger is **off** at load: `::staircase01::end_trigger.TRIGGER->t_Enable(False)` in its own record
  @23035901; it is only enabled by `::09_ShipTreasure01::Player collide trigger 00` @22984088. Walking into
  `::staircase01::end_trigger` before the ship room does nothing.
- Boris is invulnerable during his exit walk: `::end_combat::boris->C_SetInvulnerable(true)` @24514714 (in
  `::end_combat::wait2.DO`); the message that ends it is `::end_combat::trigger02` →
  `::end_combat::boris->C_SetInvulnerable(false)` @23014677. His record also starts with
  `C_SetInvulnerable(false)` @23066969. He is `nonreactive` @24514597 and cannot be killed anyway.
- Invulnerable player windows: @23040999 (StartRoom camera) off at @24624418; @24235470
  (03_SmallWareHouse::wait) off at @24235612. Both end inside a `.DO` animation.
- Use key: `::03_SmallWareHouse::Action button trigger 00` @22947955 (opens `::02_WareHouse::gate`),
  `::ship::Action button trigger 00` @23015441 (opens `::06_ShipCorridor::door01`),
  `::staircase01::Action button trigger 00` @23033556 (opens `::staircase01::door02`).
- Character-only triggers: `::StartRoom::Character collide trigger 01` @23041510,
  `::ship::Character collide trigger 01/02` @23020526/@23020926, `::cr::Character collide trigger 00..09`
  (@22984391..@22986169). A player walk fires none of them, so those enemies stay idle.
- Player teleports inside cutscenes: @23819414 and @23820671 (`::02_WareHouse::wait.DO`). A remote
  `T_Activate` of `::02_WareHouse::Player collide trigger 02` (@22943273) moves the player and plays the
  comic with him already at `Waypoint02`.

## 5. exits.json check

Entry `part2_level2b`: `exit` `::staircase01::end_trigger.TRIGGER`, `send`
`::staircase01::wait.DO->do_animate(1);`, `next` part2_level3.

1. `exit` and `send` are right (@23035972, @23035987). FACT.
2. MISSING precondition of the exit: `::09_ShipTreasure01::Player collide trigger 00` (@22984073) is what
   enables `::staircase01::end_trigger` (@22984088). Without that step the listed exit can never fire.
3. WRONG ff[0] "El muelle": `::StartRoom::ct1.TRIGGER` (@23041980) only runs `Activator->C_SendSpecial()`
   (@23041996). The dock fight is armed by `::StartRoom::P01` (@23053425) and
   `::StartRoom::Player collide trigger 00` / `03` (@23053888, @23054704).
4. WRONG ff[3] "El barco": `::ship::Player collide trigger 00` (@23025528) only makes the phone ring
   (@23025605). The ship fight comes from `::ship::Character collide trigger 01/02` (@23020526, @23020926) and
   `::ship::trigger` (@23029519), so `kill ::ship` on that step never wakes anybody.
5. WRONG kill in ff[4] "La escalera": `::end_combat` includes `::end_combat::boris`, who is put
   `C_SetInvulnerable(true)` @24514714 and only released by `::end_combat::trigger02` @23014677.
6. MISSING kill in ff[1] "Los almacenes": `::02_WareHouse::Player collide trigger 01` (@22942935) moves
   `::03_SmallWareHouse::enem01` into `::02_WareHouse` (@22942950); it is not in any kill list.
7. MISSING steps: `::cr::trigger4/5/7` (@22999820, @23000557, @23002037) wake `::cr::sniper02/03/04`; only
   `trigger3` is listed. Also missing `::03_SmallWareHouse::Action button trigger 00` (@22947955),
   `::ship::Action button trigger 00` (@23015441) and `::staircase01::Action button trigger 00` (@23033556),
   the three use-key doors.

Errors counted: 7.
