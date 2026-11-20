# part3_level1 (the Valkyr nightmare / "drug trip", no real combat)

Source: `build/data/lvl/1_Part3_Level1.ldb` (26 MB). Message lines are stored as `<target>-><message>`; the sender is
INFERENCE from the object header line above each block. The level has no combat enemies: the only characters are scripted
actors (`::24_Bedroom::Enemy_Max` @25781083, `::25_Bedroom_2::Max`, `::StartRoom::Michelle`).

## 1. Entry
`::StartRoom::Start_FSM` (header @25779472), one block @25779611-25780362, in this order (FACT):
notes `P3L1a_001/002/003` @25779611-25779782, `x_modeswitch->s_modeswitch(graphicnovel)` @25779840, music `drug_trip`
@25779884, `gm_changeglobalgamespeed(0.6,0)` @25779929, `gm_changefov(1.5,0)` @25779984, fade to black @25780027,
`player->c_removeallweapons(empty)` @25780092, `player->c_pickupweapon(painkiller)` @25780129, `player->c_sethealth(0.1)`
@25780168, objective `OBJ_DRUGTRIP` @25780195, `MPHM_EnableHUD(false)` @25780253, tip `STRING_NYM_PART2_PAR` @25780296,
`::StartRoom::Start_FSM.DO->do_animate(animation_0)` @25780362.
Then `::StartRoom::camera2` start (header @25775326): widescreen on @25775583, `GM_SetPlayerControls(false)` @25775634,
`player->c_setinvulnerable(true)` @25775684, `gm_changefov(1.6,3)` @25775719, camera timer @25775762, fade @25775811.
So Max starts with 0.1 health, no weapons, one painkiller, HUD off, 60 % game speed, and is invulnerable.

## 2. Player path (ordered)
| # | step / object | what it fires | controls / invuln | actors |
|---|---|---|---|---|
| 1 | `::StartRoom::Start_FSM` @25779472 | the load block above | controls off @25775634, player invuln true @25775684 | — |
| 2 | `::02_Entrance::Player collide trigger 00` @25716166 | self off @25716355, `::04_Hallway::Sound_FSM->A_play3dsound(drugtrip,P3L1a_01)` @25716424 | — | — |
| 3 | `::05_Livingroom::Player collide trigger 00` @25717988 | `P3L1a_02` @25718179, self off @25718264 | — | — |
| 4 | `::05_Livingroom::Player collide trigger 01` @25718341 | `::05_Livingroom::Michelle_02->A_Play3DSound(drugtrip,michelle_crying)` @25718532 | — | Michelle voice actor |
| 5 | `::06_Corridor::Door::Player collide trigger 00` @25718957 | self off @25719174, `door.DO->do_animate(open2)` @25719249 | — | — |
| 6 | `::06_Corridor::Player collide trigger 03` @25719649 | stops the crying sound @25719838 | — | — |
| 7 | `::06_Stairs::Player collide trigger 00` @25721090 | crying sound again @25721277 | — | — |
| 8 | `::06_Stairs::Player collide trigger 01` @25721361 | `P3L1a_03` @25721548, self off @25721629 | — | — |
| 9 | `::09_Workroom::Player collide trigger 00` @25724323 | `::24_Bedroom::M_Controller.DO->do_animate(animation_0)` @25724512 | — | bedroom scene starts |
| 10 | `::10_Office::Action button trigger 00` @25724576 (use) | notes P3L1a_004 @25724817, graphicnovel @25724875, self off @25724919 | — | — |
| 11 | `::10_Office::Action button trigger 01` @25725036 (use) | note P3L1a_005 @25725277, stops the phone @25725379, self off @25725490 | — | — |
| 12 | `::12_Office::Action button trigger 00/01` @25733000/@25733393 | notes P3L1a_006 @25733174, P3L1a_007 @25733634, self off | — | — |
| 13 | `::12_Office::Player collide trigger 00` @25741937 | phone ringing @25742124, `::12_Office::Cut_Controller.DO->do_animate(animation_0)` @25742420 | cutscene: player teleport @25935640 (see traps) | — |
| 14 | `::14_Entrance::Player collide trigger 00` @25745893 | door01 `close` @25746082, self off @25746129 (2nd half: other doors open by their own triggers @25743581-25745732) | — | — |
| 15 | `::18_Entrance::door01::Player collide trigger 00` @25752174 | door01 `open` @25752395, self off @25752441 | — | — |
| 16 | `::23_Livingroom_corridor::Player collide trigger 00/01/02` @25756740-25757440 | `::RedMaze::Beat_01->FSM_Send(touch)` @25757395, `Beat_01.DO->DO_Animate(stop)` @25757698 | — | maze animation |
| 17 | `::RedMaze::Player collide trigger 00` @25771394 | `::RedMaze::Beat_02.DO->DO_Animate(start)` @25771579, self off @25771623 | — | maze |
| 18 | `::RedMaze::Player collide trigger 02` @25771969 | `Beat_03.DO->DO_Animate(start)` @25772154, self off @25772198 | — | maze |
| 19 | `::RedMaze::Player collide trigger 01/03/04/05/06` @25771694-25773094 | horror_scream sounds only @25771879-25773279 | — | — |
| 20 | `::RedMaze::New mesh 02::Player collide trigger 01` @25770454 | `::RedMaze::Cut_Controller.DO->do_animate(animation_0)` @25770652 + horror_scream @25770969 | cutscene camera | — |
| 21 | `::25_Bedroom_2::Player collide trigger 00` @25764006 | **exit**: `::25_Bedroom_2::Camera_Last.DO->DO_Animate(animation_0)` @25764196, self off @25764255, `gm_setplayercontrols(false)` @25764325 | controls off to the end | — |

## 3. Exit chain (FACT)
`::25_Bedroom_2::Player collide trigger 00` (@25764006, header) T_Activate @25764181 ->
`::25_Bedroom_2::Camera_Last.DO->DO_Animate(animation_0)` @25764196 -> `gm_setplayercontrols(false)` @25764325 ->
`::25_Bedroom_2::Camera_Last.DO` event `Animation_0` (@26057219) which sends, in order: widescreen on @26057338, camera attach
@26057386, `player->c_teleport(::25_Bedroom_2::W02)` @26057446, fade @26057489, gamespeed .3 @26057547, invuln off @26057602,
wounded animations off @26057638, `player->c_sethealth(1)` @26057680, `c_removeallweapons(beretta)` @26057711,
`c_pickupweapon(leadpipe)` + infinite @26057750, `c_pickupweapon(baseballbat)` + infinite @26057831, beretta off @26057918,
`c_pickupammo(beretta,30)` @26057963, painkiller @26058000, fov/gamespeed/widescreen @26058038-26058132,
`MPGNM_MarkAllNotesRead()` @26058180, note `P3L1a_008` @26058235, graphicnovel @26058293, **`gm_init(part3_level2)` @26058337**.
No kill count, no timer: the trigger is always live, one touch ends the level.

## 4. Bot traps
1. Max is invulnerable for the whole level: true @25775684, the first `c_setinvulnerable(false)` is the end block @26057602
   (second: @26092124). A kill/death-driven bot never sees a threat here.
2. Max has 0.1 health and **no weapons** from load (@25780092, @25780168) until the exit block gives beretta + leadpipe +
   baseball bat (@26057711-26057963). Nothing to shoot, and `kill:` lists cannot be satisfied.
3. The exit teleports Max: `player->c_teleport(::25_Bedroom_2::W02)` @26057446, inside the last camera. Activating
   `::25_Bedroom_2::Player collide trigger 00` from anywhere ends the level and yanks Max to that waypoint.
4. Cutscene controllers teleport Max on their own: `::10_Office::Waypoint01` @25930372, `::12_Office::Waypoint01` @25935640,
   `::14_Entrance::Waypoint01` @25958920, `::25_Bedroom_2::W04` @26052077, `::10_Office::Waypoint01` again @26044483
   (inside `::24_Bedroom::M_Controller`). A remote trigger that fires one of these moves Max across the map.
5. `::24_Bedroom::M_Controller` fires `::24_Bedroom::Controller` beretta sounds @26034702-26035989 and moves
   `::StartRoom::Michelle` (@26044158, @26045539): pure theatre, no damage.
6. Use-key objects: `::10_Office::Action button trigger 00/01` (@25724576, @25725036), `::12_Office::Action button trigger 00/01`
   (@25733000, @25733393) — the four notes P3L1a_004..007 are missed if they are only `T_Activate`d remotely (the note is given
   by the same block, so the notes still fire; what is lost is Max standing at the object).
7. Doors in this level are collision triggers, not use keys (`::06_Corridor::Door`, `::09_Workroom::door_dummy2::Door`,
   `::01_Entry::trigger01..08`) so a remote activation skips no camera.

## 5. exits.json check (`part3_level1`)
Entry: `{"steps": [], "exit": ["::25_Bedroom_2::Player collide trigger 00.TRIGGER"], "next": "part3_level2", ...}`.
1. **Step 1 is a dead trigger.** `::01_Entry::trigger01.TRIGGER` @25713547: its only message disables itself; trigger06/07/08
   (@25713773/@25713999/@25714225) do the same. It fires no camera, no sound, no door. Wrong first step.
2. **Missing the first real step.** `::02_Entrance::Player collide trigger 00` @25716166 (P3L1a_01 sound, @25716424).
3. **Kill lists are impossible.** `kill: ["::01_Entry","::02_Entrance"]`, `["::05_Livingroom"]`, `["::06_Stairs","::06_Corridor"]`,
   `["::RedMaze"]` name rooms with no killable enemy in this level; the only actors are cutscene ones (@25781083, @26060512).
4. Correct: the exit trigger @25764196 and the `send` `::25_Bedroom_2::Camera_Last.DO->DO_Animate(animation_0);` @25764196;
   `tp` `::01_Entry::Waypoint04` @25709503 and `::StartRoom::Waypoint01` @25712061 exist.
5. Minor: the `ff` steps 2-4 (`::05_Livingroom::Player collide trigger 00` @25718179, `::06_Stairs::Player collide trigger 01`
   @25721361, `::RedMaze::Player collide trigger 00` @25771394) are real, they just fire sounds / maze animations.

Not verified: whether the 12_Office/24_Bedroom camera blocks require the player's position (their messages are split across the
24.x-26.x MB region), and the exact room order between 12_Office and 14/18_Entrance.
