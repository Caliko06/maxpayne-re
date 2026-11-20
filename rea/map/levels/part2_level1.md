# part2_level1 — Part II boiler room, corridors, drug room, lobby, docks, bar

Source: build/data/lvl/1_Part2_Level1.ldb, read with `python3 tools/ldb_dump.py` (script starts ~@17480494).
FACT = string at that offset, INFERENCE = my grouping. Room order below is INFERENCE from the door/exit
chaining of each room; the offsets are FACT.

## 1. Entry

- `:boiler_room::startup_fsm`: comic notes P2L1a_001..006 (@28977430..@28977775) and
  `x_modeswitch->s_modeswitch(graphicnovel)` @28977833. FACT.
- `:boiler_room::post_process.DO` sends `::boiler_room::camera1->fsm_send(start)` @29728775. FACT.
- `::boiler_room::camera1` (@28971843): `CAM_AttachToDO(100,false)` @28972005, `DO_Animate(1)` @28972062,
  `GM_SetPlayerControls(false)` @28972106, `gm_changeglobalgamespeed(.7,0)` @28972156,
  `player->c_setinvulnerable(true)` @28972210, `GM_EnableWidescreen(true,0)` @28972245, HUD hidden @28972296,
  `::boiler_room::max->C_Setstatemachine(idle)` @28972414. camera2 @28972585 (speed .6 @28972848).
- `::boiler_room::camera3.DO` (@29718881) ends the scene: `CAM_AttachToDO(0)` @29721195, speed 1 @29721250,
  `GM_SetPlayerControls(true)` @29721303, fov/fade/widescreen/HUD @29721352..@29721445,
  `player->c_teleport(::boiler_room::w1)` @29721497, `player->c_setinvulnerable(false)` @29721538,
  `::boiler_room::max->C_Teleport("::teleport::w1")` @29721574, bat removed from item and hands
  (@29721742/@29721794), `player->c_sethealth(0.3)` @29721905. FACT.

## 2. Player path (ordered)

| # | Step: trigger / object | Fires | Windows | Enemies |
|---|---|---|---|---|
| 1 | `:boiler_room::startup_fsm` @28977430 | comic P2L1a_001..006, graphicnovel @28977833 | — | none |
| 2 | `::boiler_room::camera1` @28971843 → camera2 → camera3 | camera chain, teleports, health 0.3 (@29721195..@29721905) | controls off @28972106, invuln ON @28972210 → controls on @29721303, invuln OFF @29721538 | none |
| 3 | `::boiler_room::door_dummy2::Door::t1` @28974498 | `do_animate(open1)` @28974628 + `::hallway1::script->fsm_send(start)` @29726244 | none | none |
| 4 | `::boiler_room::pt1` @28976760 | `A_PlaySound(hotel2, p2l1a_Max_03)` @28976775 | none | none |
| 5 | `::corridor1::noise1` @28978039 | `activator->c_sendspecial()` @28978055 + self-disable @28978084 | none | corridor1::e1 / e1b wake via the special |
| 6 | `::hallway1::script` state `max_spotted` @29016929 | `fsm_switch(2)` @29016955, `::corridor4::noise1.TRIGGER->t_enable(false)` @29016992, `::corridor1::e1->c_gotoplayer(1)` @29017040, `::corridor4::e1/e2 -> C_GoToPlayer` @29017076/@29017112, `middle::script->fsm_send(max_spotted)` @29017148, `corridor1::e1b`/`corridor4::e2b` gotoplayer @29017191/@29017228 | none | corridor1::e1, e1b, corridor4::e1, e2, e2b |
| 7 | `::corridor5::trigger_mobster` @28978749 | `::middle::script->fsm_send(start)` @28978764, self-disable @28978801 | none | middle::e1..e4 |
| 8 | `::drug::watercooler01::water_trigger` @29003787 | water FX @29003802..@29003999 (scenery, no gate) | none | none |
| 9 | `::drug_out::enemy_safetrigger` @29007684 | `::drug_out::e2/e3->C_SetStateMachine(mobstercombat)` @29007757/@29007809, `c_gotoplayer(1)` @29007861/@29007896 | none | drug_out::e2, e3 |
| 10 | `::drug_out::script` @29010210 (state `start` @29010393) | `::drug_out::e1->C_gotoplayer(1)` @29010413, `mobstercombat` @29010497, e2/e3 `crouchandshootstatic` @29010622/@29010681, `C_GoToAndShoot` @29010740/@29010791 | none | drug_out::e1 |
| 11 | `::drug_out::note_trigger1` @29008212 (T_Activate @29008638) | notes P2L1a_010/P2L1a_011 @29008903/@29008961 + graphicnovel @29009019 | none | none |
| 12 | `::lobby::pt1` @29024949 | `::lobby::camera1->fsm_send(start)` @29025005 | — | none |
| 13 | `::lobby::camera1` @29021233 / `::lobby::camera1.DO` @30077527 | `DO_Animate(0)` @29021389, `GM_SetPlayerControls(false)` @29021427; then fade @30079829, `DO_Animate(1)` @30079895, `player->c_setinvulnerable(true)` @30080324, widescreen @30080359, `player->c_teleport(::teleport::w1)` @30080522, `::boiler_room::max->c_teleport("::lobby::w1")` @30080560, `GM_TransferPlayerWeaponTo(::boiler_room::max)` @30080609, `::drug_out::camera2->fsm_send(start)` @30080779 | controls off @29021427, invuln ON @30080324 | none |
| 14 | `::drug_out::camera2` @29006177 / `.DO` @29956230 | `do_animate(2)` @29958541, note P2L1a_009 @29958582, graphicnovel @29958640, then controls on @29961543, invuln off @29961633, widescreen off @29961669, `lobby::door_dummy1::Door::t1.TRIGGER->t_enable(true)` @29961773, `::boiler_room::max->C_teleport("::teleport::w1")` @29961912, `player->c_teleport(::lobby::w1)` @29961964 | controls on + invuln OFF @29961543/@29961633 | none |
| 15 | `::exit01::gn_trigger` @29011649 (DO) | `do_enablecollisions(false)` @29011703, `::exit01::gn_timer.DO->do_animate(1)` @29011758 | — | none |
| 16 | `::exit01::gn_timer.DO` @29975923 | fade @29977075, `GM_SetPlayerControls(false)` @29977141, note P2L1a_008 @29977237, graphicnovel @29977295, `::exit01::police_tape_controller->FSM_Send(tear)` @29977697, controls on @29977749 | controls off → on (@29977141 → @29977749) | none |
| 17 | `::exit01::music_outdoor` @29015604 / `::lobby::music_indoor` @29024551 | city ambience @29015723, `lobby::music_indoor.TRIGGER->T_Enable(true)` @29015772; indoor @29024616 + `Street03::police_sounds->FSM_Send(stop)` @29024664 | none | none |
| 18 | `::meat_place1::New mesh 01::t_health` @29028979 | doors open @29029084/@29029145 | none | none |
| 19 | `::outside1::ct1/ct2/ct3` @29094888 / @29095088 / @29095288 | `activator->C_SendSpecial()` @29094904 / @29095104 / @29095304 | none | whoever walks over it |
| 20 | `::outside1::meat_place1_mobsters_trigger` @29095628 (DO) | `do_enablecollisions(false)` @29095856, `::meat_place1::script->fsm_Send(chase_max)` @29095931 | none | meat_place1::e1/e2/e3 → `C_GoToPlayer(1)` @29029450..@29029526 |
| 21 | `::outside1::grenade_trigger` @29095500 | `::meat_place1::grenade_shooter->fsm_Send(shoot)` @29095571 | none | none |
| 22 | `::outside1::objective_change_trigger` @29096590 | `mphm_printobjective(OBJ_HOTEL2_FIND_NIAGARA, 0)` @29096605 | none | none |
| 23 | `::restaurant::door_dummy2::Door::t1/t2` @29100786 / @29101195 | `open1` @29100929 / `open2` @29101338, and the door record disables/enables the room exits @29100251/@30413899 | none | none |
| 24 | `::sewer_room::note_trigger1` @29114343 | note P2L1a_007 @29114487 + graphicnovel @29114545 | none | none |
| 25 | `::sewer_room::script` state `start` @29115464 | `::sewer_room::e1->c_gotoplayer(1)` @29115474, `e2->C_GoToPlayer(1)` @29115511 | none | sewer_room::e1, e2 |
| 26 | `::shaft::lift::lift_trigger` @29117649 | lift animation down/up @29115726/@29115787, doors close/open @30496561..@30511800, `::shaft::t_breath_in` @29120304 stops the breath FX and re-enables `::upper::t_breath_out` | timed doors: lift_trigger disabled until the lift arrives (`FSM_Switch(disabled)` @29118228/@29118573) | none |
| 27 | `::upper::script` @29135308 (state `start`) | `fsm_Switch(2)` @29135509, `::upper::e1->c_gotoandshoot("::outside1::w3",1)` @29135612, `mobstercombatdefensive` @29135664, `::upper::e2` crouchandshootstatic @29135774 | none | upper::e1, e2 |
| 28 | `::to_bar::t_note_bar` @29128480 | `::to_bar::d_door_bar.DO->do_animate(open)` @29128495, self-disable @29128540, `::to_bar::gn_timer.DO->do_animate(1)` @29128589 | — | none |
| 29 | `::to_bar::gn_timer.DO` @30543011 | `do_animate(2)` @30544286, fade, note P2L1a_012 @30544326, graphicnovel @30544384, `::to_bar::camera1->fsm_send(start)` @30544720 | controls off @30544230 → on @30517570 | none |
| 30 | `::to_bar::camera1` @29126455 / `.DO` | controls off @29126888, `player->c_setinvulnerable(true)` @29126938, `player->c_teleport(::teleport::w1)` @29127018, `::boiler_room::max->c_teleport("::to_bar::w1")` @29127056, `::bar::script->FSM_Send(initialize)` @29127209; end: controls on @30517570, invuln off @30517619, `player->c_teleport(::to_bar::w2)` @30517696, max → teleport::w1 @30517732, `::bar::script->fsm_send(start)` @30539849 | controls off + invuln ON → both restored (@30517570/@30517619) | none |
| 31 | `::bar::script` state `initialize` @28968490 / `start` @28968783 | `::bar::e3->C_SetStateMachine(idle)` @28968505, targets → `::boiler_room::max` @28968543..@28968714; `::bar::frankie` crouchandshootstatic @28968793 → delay1s @28968852, `::bar::timer.DO->do_animate(1)` @28968899, `C_GoToAndShoot("::bar::w5",1)` @28968932, targets → player @28968982..@28969111, `A_Play3DSound(p2l1a_04)` @28969165 | none | bar::e1, e2, e3, frankie |
| 32 | `::bar::ct1` @28964243 | `activator->c_sendspecial()` @28964259 (opens fire) | none | whoever walks over it (Frankie) |
| 33 | `::bar::timer.DO` anim 2/3/4 @29689283 / @29689829 / @29690506 | positions e1/e2/e3/frankie (`C_GoToAndShoot` @29690320..@29690455), `::bar::frankie_timer->FSM_Send(start)` @29690831 | none | none |
| 34 | `::bar::fsm_rico` @28965863 | `FSM_Switch(2)` @28966132/@28966244 + `a_playmusic(old_building)` @28966172 | none | none |

## 3. Exit

Not a trigger: four deaths. `::bar::e1`, `::bar::e2`, `::bar::e3` and `::bar::frankie` each send
`::bar::frankie_death_timer->fsm_send(add)` from their death handler (@29137324, @29137606, @29137885,
@29138294). The FSM switches state per add (@28965151, @28965203, @28965255) and wakes Frankie in
`merccombat` (@28965300). The last add runs `::bar::frankie_death_timer.DO->DO_Animate(1)` @28965356 →
animation 2 @29667171 → `FadeToColor(0, 0x000000ff, 0.5)` @29667505 → notes P2L1a_013, P2L2a_002, P2L2a_003
(@29667578, @29667636, @29667694) → `s_modeswitch(graphicnovel)` @29667752 → `gm_init(part2_level2)`
@29667796. FACT for each message; INFERENCE for "4 adds in that order".

## 4. Bot traps

- Player invulnerable: @28972210 (boiler_room::camera1) off at @29721538; @29126938 (to_bar::camera1) off at
  @30517619; @30080324 (lobby::camera1.DO) off at @29961633 (drug_out::camera2 end). A cut camera leaves the
  flag set. `::boiler_room::max` is also made invulnerable (@29138643) — that is the NPC, not the player.
- Player teleports inside cutscenes: @29721497, @30080522, @29961964, @29127018, @30517696. Remote
  `T_Activate` of the camera owners therefore moves the player; remote `T_Activate` of the *triggers* that
  start them (`::lobby::pt1` @29025005, `::to_bar::t_note_bar` @29128589) does not, so the player is left
  where he was while the camera plays elsewhere.
- Use key: no action button in this level; `::restaurant::machine01::t_broken` @29102078 is scenery.
- Timed doors: `::shaft::lift` (two door pairs + `lift_trigger` FSM @29117649). The lift trigger is disabled
  while the doors are moving (@29118228, @29118573) and the player must be inside; a remote activation
  leaves him in the shaft.
- State gates that a remote trigger skips: `::outside1::meat_place1_mobsters_trigger` @29095628 (chase
  `chase_max`), `::bar::script initialize` @28968490 (without it Frankie never gets targets and the bar is
  dead), `::bar::ct1` @28964243 (only a `c_sendspecial` to its activator).

## 5. exits.json check

Entry `part2_level1`: `steps` [], `exit` [], `kill` `::bar::e1`, `::bar::e2`, `::bar::e3`, `::bar::frankie`.

1. `kill` is right and complete: the four `fsm_send(add)` at @29137324, @29137606, @29137885, @29138294, FSM
   at @28965151..@28965300, end at @29667171/@29667796. `exit` [] is right, no trigger ends this level.
2. WRONG step in ff[2] "La fábrica de carne": `::meat_place1::switch.TRIGGER` (@29029728) only plays
   `subway_tunnel_switch` (@29029743). The chase comes from `::outside1::meat_place1_mobsters_trigger`
   (@29095628 → @29095931).
3. WRONG/incomplete step in ff[3] "El bar": `::bar::ct1.TRIGGER` (@28964243) only sends the special to its
   activator (@28964259); the fight is armed by `::bar::script->FSM_Send(initialize)` from the to_bar camera
   (@29127209) and `fsm_send(start)` (@30539849). The step's own `kill` list `::bar` misses `::bar::frankie`,
   which is the 4th add the exit waits for (@29138294).
4. INCOMPLETE ff[1] "El cuarto de droga": `::drug_out::enemy_safetrigger.TRIGGER` (@29007684) wakes only e2
   and e3 (@29007757/@29007809); e1 is woken by `::drug_out::script` (@29010413).
5. MISSING: `steps` is empty, so the 34 rows above (lobby cutscene, police tape note, docks chase, sewer room,
   lift, to_bar comic) have no entry.

Errors counted: 5.
