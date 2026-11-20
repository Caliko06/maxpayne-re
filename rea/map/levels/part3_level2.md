# part3_level2 (Cold Steel, steel mill: linear)

Source: `build/data/lvl/1_Part3_Level2.ldb` (17 MB). Message lines are stored as `<target>-><message>`; the sender object is
INFERENCE from the header line above each block. No enemy is immortal or invulnerable anywhere in this file
(`grep invulnerable|immortal` -> no hits).

## 1. Entry
`::StartRoom::start_fsm` (header @16364315), block @16364454-16365261 (FACT): `gm_setplayercontrolledslowmotion(1)` @16364454,
`gm_resetlevelitems()` @16364512, objective `OBJ_COLDSTEEL_FIND` @16364555, objectives 1/2 cleared @16364619/@16364667,
`mphm_changehudvisibility(0,0)` @16364715, `GM_SetPlayerControls(false)` @16364767, fade @16364817, notes
`p3l2a_002..005` @16364937-16365111, graphicnovel @16365169, `::StartRoom::post_process.DO->do_animate(1)` @16365213,
`player->c_sethealth(1)` @16365261.
Entry cutscene ends in `::StartRoom::wait.DO` (header @17220998) -> `cam04a` attach @17222075, `enemy01` walks @17222129,
`GM_SetPlayerControls(true)` @17222694, HUD on @17222743, widescreen off @17222795, gamespeed 1 @17222848. FACT.

## 2. Player path (ordered)
| # | step / object | what it fires | controls / invuln | enemies |
|---|---|---|---|---|
| 1 | load `::StartRoom::start_fsm` @16364315 | objectives, notes, HUD off | controls off @16364767 | — |
| 2 | `::StartRoom::wait.DO` @17220998 | cam04a, enemy01 walks | controls on @17222694 | StartRoom enemy01 |
| 3 | `::02_Entry::Player collide trigger 00` @16276866 | `A_Play3DSound(dynamic, door_metal_locked)` @16277052 only, self off | — | — |
| 4 | `::03_Radio::trigger01` @16285928 | stops the radio @16286097, self off @16286302, `::03_Radio::look` off @16286352, `trigger1` on @16286397, radio hum @16286451 | — | — |
| 5 | `::05_Hallway::Action button trigger 03` @16287997 (use) | mechanic_lever @16288184 | — | — |
| 6 | `::05_Hallway::d_door01` (header @16288579) | door open/close @16289001/@16289102, `GM_EnableExit(::warehouse::Exit2,false)` @16288747 | — | — |
| 7 | `::05_Hallway::Barrel.DO->DO_Animate(Animation_0)` @16289969 | `::05_Hallway::Explosion->FSM_Switch(Activate)` @16290674, projectile trigger disabled @16290835 | — | barrel blast |
| 8 | `::06_Hallway::Player collide trigger 00` @16294818 | `::07_Hallway::enemy01->C_SendSpecial()` @16295006, `::07_Hallway::grenade->P_CreateProjectile(grenade,1)` @16295048, self off @16295105, `::05_Hallway::enemy01` GoToPlayer @16295173 | — | ambush: grenade + 05_Hallway::enemy01 |
| 9 | `::06_Hallway::Player collide trigger 01` @16295280 | `door_metal_locked` sound @16295468 | — | — |
| 10 | `::06_Hallway::d_mine01` (header @16294275) | `fsm_send(explode)` @16294040/@16294226/@16294518, shake @16293939 | — | mines |
| 11 | `::07_Hallway::Action button trigger 01/02` @16295564/@16295989 (use) | `::08_Ladleroom::d_door02->fsm_send(move)` @16295751, lever @16296176 | — | — |
| 12 | `::08_Ladleroom::Action button trigger 02` @16298227 | d_door02 move @16298416 | — | — |
| 13 | `::08_Ladleroom::ladel` (header @16306143) + `::ladel::ladel01` DO | `do_animate(forward)` @16306281; `activator->c_sethealth(0)` @16306438 and @16306737 on its collide events | kills whoever touches it | — |
| 14 | `::08_Ladleroom::trigger01` @16319095 | `d_door03->fsm_send(move)` @16319299 | — | — |
| 15 | `::09_Sideroom::Action button trigger 00/01` @16319504/@16319742 | d_door03 move @16319692, lever @16319930 | — | — |
| 16 | `::09_Sideroom::Player collide trigger 01/02` @16321587/@16321960 | `lava02`/`lava01` `do_animate(1)` @16321911/@16322149 | — | — |
| 17 | `::13_Corridors::Player collide trigger 00` @16324759 | `::13_Corridors::E02` gotoandshoot W03 @16324949, self off @16325073 | — | E02 |
| 18 | `::13_Corridors::Character collide trigger 00` @16324011 | `Activator->C_GoToAndShoot(::14_Corridors::W04,1)` @16324204 | moves the activator | — |
| 19 | `::14_Corridors::E03` -> `::15_Combat::W01` @16325339 | — | — | E03 pushes to 15_Combat |
| 20 | `::15_Combat::T1/T2` @16329497, @16329717 | `Activator->C_GoToAndShoot(::15_Combat::W02/W03,1)` | moves the activator | — |
| 21 | `::16_Corridor::Exit` (header @16329789, `T_Activate` @16329941) | **exit**: `::16_Corridor::Exit_FSM.DO->DO_Animate(animation_0)` @16329956 | — | — |

## 3. Exit chain (FACT)
`::16_Corridor::Exit` T_Activate @16329941 -> `::16_Corridor::Exit_FSM.DO->DO_Animate(animation_0)` @16329956 ->
`::16_Corridor::Exit_FSM.DO` event `Animation_0` (header @17017524) -> `mphm_fadetocolor(0x0,0x000000ff,0.5)` @17018919 ->
**`maxpayne_gamemode->gm_init(part3_level2b)` @17018985**.
No death counter, no timer, no `GM_EnableExit` toggling of this trigger anywhere in the file: the exit is live from load.

## 4. Bot traps
1. Instant-kill hazards: `activator->c_sethealth(0)` @16288522, @16306438, @16306737, @16397187 (and later @164? blocks) are
   collide events of moving junk (`::08_Ladleroom::ladel`, ladle02, crane loads). The player dies on contact.
2. `::06_Hallway::Player collide trigger 00` spawns a live grenade at `::07_Hallway` (@16295048) and wakes `05_Hallway::enemy01`
   (@16295173). It is the only place the level adds an enemy behind the player.
3. Activator movers: `::02_Hallway`-style `Activator->C_GoToAndShoot(...)` @16324204 and @16329512/@16329732 order Max/enemies to
   move. A remote `T_Activate` gives the movement to the trigger object, not to Max.
4. Use-key objects (must be at them): `::05_Hallway::Action button trigger 03` @16287997, `::07_Hallway::Action button
   trigger 01/02` @16295564/@16295989, `::08_Ladleroom::Action button trigger 02` @16298227, `::09_Sideroom::Action button
   trigger 00/01` @16319504/@16319742 (all play `mechanic_lever`/door moves).
5. Timed doors: `::05_Hallway::d_door01` and `::08_Ladleroom::d_door02/d_door03` expose FSM states `busy`/`closed`
   (@16289048, @16289151, @16303255, @16304354): activating a trigger twice during the animation is ignored.
6. `GM_EnableExit(::warehouse::Exit2,false)` @16288747 and `::05_Hallway::Exit1` @16288812 close backtracking doors while the
   hallway scene runs; a bot that goes back after that step finds the door disabled.
7. No invulnerable/immortal enemy: the mercs are all killable, so `kill:` lists complete.

## 5. exits.json check (`part3_level2`)
1. **"La entrada nevada" is a dead step.** `::02_Entry::Player collide trigger 00` (@16276866) only plays `door_metal_locked`
   @16277052 and disables itself; it opens nothing and gates nothing. Wrong as step 1.
2. **"La radio" is only a radio.** `::03_Radio::trigger01` (@16286302) turns the radio hum on and disables itself; no
   progression. Harmless but not a step.
3. **"El almacén" is a container.** `::warehouse::trigger01` @16417842 opens `::warehouse::container01::d_door01/door02`
   (@16417842, @16417901) - a locker, not the room. The room's real ambush is `::warehouse::t_spawn` @16416994
   (`::Teleport::enemy01->C_Teleport("::warehouse::waypoint01")` @16417168).
4. **Missing the 05/06_Hallway ambush line** (`::06_Hallway::Player collide trigger 00` @16295048 grenade, `::05_Hallway`
   barrel explosion @16290674) and the 13/14/15_Corridor push triggers (@16324759, @16325339, @16329497).
5. Correct: exit `::16_Corridor::Exit.TRIGGER` @16329956, `send` `::16_Corridor::Exit_FSM.DO->DO_Animate(animation_0);` @16329956,
   `next: part3_level2b` (@17018985). All `tp` waypoints exist: `::warehouse::Waypoint13` @16268807,
   `::05_Hallway::Waypoint01` @16264138, `::steelmill::Waypoint02` @16267599, `::warehouse::waypoint08` @16268673.
6. `kill` prefixes are legal (`ghost.mjs` matches `name === room || name.startsWith(room+"::")`), no immortal blocker here.

Not verified: the exact concatenation order of `05_Hallway` -> `06/07_Hallway` -> `08_Ladleroom` -> `09_Sideroom`; I read each
room's triggers, not a walkthrough of the geometry.
