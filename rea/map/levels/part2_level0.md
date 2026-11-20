# part2_level0 — Part II nightmare (StartRoom, halls, BabysRoom, Bedroom)

Source: build/data/lvl/1_Part2_Level0.ldb, read with `python3 tools/ldb_dump.py`. FACT = string at that
offset. INFERENCE = my grouping of lines into objects/events. Script section starts ~@14889088.

## 1. Entry

- Start record `::StartRoom::fsm_start` (@14929491). It switches to the comic
  (`x_modeswitch->s_modeswitch(graphicnovel)` @14929630), plays `a_playmusic(nightmare)` @14929674,
  `gm_setplayercontrols(false)` @14929719, `gm_changeglobalgamespeed(0.7, 0)` @14929769, fades to black
  @14929881, `player->c_removeallweapons(empty)` @14929946, `player->c_pickupweapon(painkiller)` @14929983,
  `gm_changefog(0x002020, -20, 20, 2)` @14930021, `mphm_printobjective(OBJ_NIGHTMARE_LOVED_ONES, 0)` @14930078,
  `MPHM_EnableHUD(false)` @14930148, `MPHM_PrintTipNYM(...)` @14930191. FACT.
- Same record's animation 0 hands the opening camera over: `::01_Entry::rotator::Camera.DO->CAM_AttachToDO(100, false)`
  @15103960 and `gm_enablewidescreen(1, 0)` @15104071. The graphic-novel notes P2L0a_001..006 are picked up here
  (@15103493..15103838). FACT.
- Intro camera ends: `::01_Entry::rotator::Camera::timer.DO` @15010659 plays animation 1, 2, 3
  (@15010088, @15011864, @15012272) and gives control back: `gm_setplayercontrols(true)` @15010191,
  `gm_enablewidescreen(0, 0)` @15010240. FACT.
- No weapons beyond the painkiller; no mortal enemy in the level at all (only NPCs, below).

## 2. Player path (ordered)

| # | Step: trigger / object | Fires | Windows | Enemies |
|---|---|---|---|---|
| 1 | `::StartRoom::fsm_start` @14929491 | comic, music, HUD off, weapons → painkiller (@14929630..@14930191) | controls off @14929719, speed 0.7 | none |
| 2 | `::01_Entry::rotator::Camera::timer.DO` @15010659 | camera anim 1→3, widescreen off | controls back on @15010191 | none |
| 3 | `::03_Hallway::Player collide trigger 00/01` @14895241 / @14895478 | `gm_changefov(1.3,1)` @14895256, `(1.7,1)` @14895493 | none | none |
| 4 | `::05_Hallway::T1` (T_Activate @14896004) | `A_Play3DSound(nightmare, P2L0a_06)` @14896019, self-disable @14896085 | none | none |
| 5 | `::09_Hallway::T2` (T_Activate @14896994) | `P2L0a_07` @14897009, self-disable @14897075. T2 is enabled at @14895580 by `::09_Hallway::T2.TRIGGER->T_Enable(true)` | none | none |
| 6 | `::18_Hallway::Player collide trigger 00/01` @14898259 / @14898553 | fov 1.3 + fog `0x401010` @14898274/@14898317; fov 1.7 + fog `0x002020` @14898568/@14898611 | none | none |
| 7 | `::19_LivingRoom::LookAt_01` @14899714, `LookAt_02` @14900048 | `A_PlaySound(p2l0a_Max_02)` @14899739 / `p2l0a_Max_01` @14900073 + `fsm_switch(2)` | none | none |
| 8 | `::19_LivingRoom::note_01_trigger` @14900346, `note_01_trigger01` @14900808 | note p2l0a_008 @14900477 / p2l0a_007 @14900941 + `s_modeswitch(graphicnovel)` @14900535 / @14900999 | none | none |
| 9 | `::19_LivingRoom::Player collide trigger 04` @14901280 | `::19_LivingRoom::Controller.DO->do_animate(animation_0)` @14901295 | none | none |
| 10 | `::20_Stairs::Player collide trigger 01` @14902384 | `A_Play3DSound(dynamic, door_wood_locked)` @14902399 (locked door, INFERENCE) | none | none |
| 11 | `::21_Bathroom::t_door_wc` (T_Activate @14905628) | planks hide+animate @14905696/@14905742, glass shatter @14905797..@14906056 | none | none |
| 12 | `::22_BabyCorridor::door_dummy1::Door::t1/t2` @14906985 / @14907419 | `do_animate(open1)` @14907138 / `open2` @14907572, both triggers disable each other | none | none |
| 13 | `::23_BabyCorridor::Player collide trigger 00/01/02` @14908539 / @14908795 / @14909049 | fog `0x000000` 25/100 @14908554, fog 5/10 @14908810, `A_PlaySound(nightmare, p2l0a_02)` @14909064 | none | none |
| 14 | `::25_BabyCorridor::Player collide trigger 00/02` @14909858 / @14910114 | fog `0x300505` @14909873, fog 5/10 @14910129 | none | none |
| 15 | `::26_BabysRoom::Door::t1/t2` @14914767 / @14915121 | `do_animate(open1/2)` @14914888 / @14915242 | none | none |
| 16 | `::26_BabysRoom::Player collide trigger 00` (T_Activate @14916578) | `player->c_teleport(::26_BabysRoom::W01)` @14916593, self-disable @14916636, `::26_BabysRoom::camera1->FSM_Send(start)` @14916706 | — | none |
| 17 | `::26_BabysRoom::camera1` (@14910418) → camera2 (@14911156) → camera3 (@14911704) → camera4 (@14912558) | camera1: widescreen @14910581, controls off @14910744, invuln on @14910794; camera2 @15040217; camera3: controls off @14912092, invuln on @14912142; camera4: `::26_BabysRoom::Baby->c_teleport("::Teleport::W01")` @14912976 | controls off + invuln ON at camera1/camera3; controls on @15040440 + @15046520, invuln off @15040489 (camera2 end) and @15046569 (camera4 end) | Baby teleports away @14912976 |
| 18 | `::28_WorkRoom::Action button trigger 00` (T_Activate @14918376, use key) | self-disable @14918376, note p2l0a_009 @14918499, `s_modeswitch(graphicnovel)` @14918557, `::28_WorkRoom::L1.TRIGGER->t_enable(false)` @14918601 | none | none |
| 19 | `::28_WorkRoom::Controller::Player collide trigger 00` (T_Activate @14919145) | `Controller.DO->do_animate(animation_0)` @14919160 | none | none |
| 20 | `::28_WorkRoom::Player collide trigger 00` (T_Activate @14919794) | `::29_Bedroom::End_controller.DO->do_animate(animation_0)` @14919809 | none | none |
| 21 | `::29_Bedroom::Player collide trigger 00` (T_Activate @14922289) | `::32_Boiler_Room::Frankie->c_teleport("::32_Boiler_Room::W_Frankie")` @14922304, `standandshoot` @14922376, target `::32_Boiler_Room::max` @14922439, `Camera_Final.DO->CAM_AttachToDO(1.5,false)` @14922515, `do_animate(animation_0)` @14922580, `gm_changeglobalgamespeed(0.15,0)` @14922642 | slow motion only | Frankie appears in 32_Boiler_Room @14922304 |

## 3. Exit

`::29_Bedroom::Player collide trigger 00` (@14922289) starts the ending; the ending itself is driven by
`::29_Bedroom::end_delay_timer.DO` (`delay`, @15068816) and `::29_Bedroom::End_controller.DO`:
`::32_Boiler_Room::Frankie->c_teleport(...)` @15067930, `Camera_Final.do_animate(animation_0)` @15068143,
`FadeToColor(0x000000ff, 0x0, 3)` @15073116, `Camera_Final.do_animate(animation_1/2/3)` @15073235 / @15073654 /
@15074076, `FadeToColor(0x0, 0x000000ff, 3)` @15074018, `::31_Storage::Camera03.DO->cam_attachtodo(1, false)`
@15074440, then `maxpayne_gamemode->gm_init(part2_level1)` @15074495. There is no graphicnovel switch and no
death counter on this path; the level ends on a timer, not on kills. FACT.

## 4. Bot traps

- Player invulnerable windows: `player->c_setinvulnerable(true)` @14910794 (camera1) and @14912142 (camera3);
  only `@15040489` (camera2 end) and `@15046569` (camera4 end) turn it off. Activating a camera remotely and
  skipping its successor leaves the player invulnerable, controls off.
- Player teleport inside a trigger: @14916593. `T_Activate` on `::26_BabysRoom::Player collide trigger 00`
  without the player at the door plays the whole baby-room camera chain while the player stays outside.
- Use key: `::28_WorkRoom::Action button trigger 00` (@14918376) is the only action button; it is only a
  comic note (p2l0a_009), it does not gate the exit.
- No enemy is immortal or invulnerable: no `c_setimmortal` / `c_setinvulnerable` exists in this file (checked,
  0 hits). Nothing to un-set.
- `::29_Bedroom::Player collide trigger 00` remotely activated runs the ending with Frankie in
  `::32_Boiler_Room` and the player wherever he was; the level still loads part2_level1 (@15074495).

## 5. exits.json check

Level entry `part2_level0`: `steps` [], `exit` `::29_Bedroom::Player collide trigger 00.TRIGGER`,
`send` `::32_Boiler_Room::Camera_Final.DO->do_animate(animation_0);`, `next` part2_level1.

1. `exit` and `send` are right: `T_Activate` @14922289 exists and its first camera message is @14922580. FACT.
2. WRONG kill in ff[2] "El dormitorio": `::29_Bedroom`, `::32_Boiler_Room`. No enemy exists in either room
   (`::29_Bedroom::max` @14934543 is the `enemy_max_payne` NPC of the cutscene, `::32_Boiler_Room::Frankie`
   @14934699 is `frankie_the_bat` with no death handler). Both names are unfalsifiable kills.
3. WRONG kill in ff[3] "El taller": `::28_WorkRoom`. `::28_WorkRoom::Action button trigger 00` (@14918376)
   only reads a comic note (@14918499); the room holds no character.
4. MISSING steps: `steps` is empty, so the bot has no order for the 21 rows above; the four `ff` entries are
   teleports, not a route.
5. Trap in ff[1] "La habitación del bebé": `tp` `::26_BabysRoom::W01` is the point the trigger itself teleports
   the player to (@14916593). Teleporting there first does not fire `::26_BabysRoom::Player collide trigger 00`
   (@14916578), so camera1..camera4 never run and the Baby never leaves (@14912976).

Errors counted: 5.
