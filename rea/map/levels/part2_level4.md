# part2_level4 (Punchinello manor, free roam)

Source: `build/data/lvl/1_Part2_Level4.ldb` (25 MB). Facts read with `tools/ldb_dump.py`; offsets are file offsets.
Message lines in the .ldb are stored as `<target>-><message>(args)`, so the offset alone does not name the sender;
"the object that sends this" is INFERENCE from the object header line just above the block.

## 1. Entry
- `::StartRoom::start_fsm` (header @22260846): gamespeed 1, fade to black, `::StartRoom::post_process.DO->DO_Animate(1)`,
  objective `OBJ_MANOR_TRIO` @22261150, notes `P2L4a_001..005` @22261216-22261503, `x_modeswitch->s_modeswitch(graphicnovel)`
  @22261561. FACT.
- Intro flythrough cameras: `::FOYER::camera3.DO` sends `::StartRoom::camera1->fsm_send(start)` @23233119; `::StartRoom::camera1`
  start (header @22259877) sets `GM_SetPlayerControls(false)` @22260134, gamespeed .8 @22260184, `player->c_setinvulnerable(true)`
  @22260238, widescreen on, HUD off, flash @22260376. FACT.
- Control returns once: `GM_SetPlayerControls(true)` @24624027, `player->c_setinvulnerable(false)` @24624117, widescreen off,
  HUD on, fade in (@24623921-24624257). INFERENCE: this is `::StartRoom::camera1.DO`'s animation end.
- No weapon is given by the level script; the only player weapon source is the gunroom action trigger (step 3).

## 2. Player path (ordered; the manor itself is free roam)
| # | step / object | what it fires | controls / invuln windows | enemies |
|---|---|---|---|---|
| 1 | load `::StartRoom::start_fsm` @22260846 | graphic novel P2L4a_001-005, objective OBJ_MANOR_TRIO @22261150 | — | — |
| 2 | intro cameras `::FOYER::camera3.DO` -> `::StartRoom::camera1` @23233119 / @22259877 | camera attach, fade | controls off @22260134, player invuln @22260238, both end @24624027 / @24624117 | — |
| 3 | `::gunroom::give_weapons` (use key, `T_Activate` @22189046) | `activator->c_sendspecial()` @22189062 (gives the gunroom weapon set) | — | — |
| 4 | `::gunroom::script` start (header @22193341) | `::gunroom::e2` goto+shoot `::gunroom::w3` @22193586, e1/e2 crouchandshootstatic @22193697 | — | gunroom e1/e2 |
| 5 | `::gunroom::death_counter` (header @22186287) | FSM 2 @22186542, 3 @22186592, delay @22186642, `disabled` @22186879, `::FOYER::Joe` death anim @22186832, `add_joe` @22299623 | — | — |
| 6 | `::gunroom::disable_invulnerability` (`T_Activate` @22187716) | `::FOYER::Joe->c_setinvulnerable(false)` @22187794 | ends Joe's invulnerability | Joe (FOYER) becomes killable |
| 7 | `::gallery::t6` (`T_Activate` @22185583) | self off @22185598, `::piano::door01::t4` off @22185640, piano door `open2` @22185688 | — | gallery e1/e2/e3, piano timer @22251993 |
| 8 | `::table::ct1` (`T_Activate` @22262939) | self off, `activator->c_sendspecial()` @22262996, `::table::player_trigger.DO->do_enablecollisions(true)` @22263025 | seals the dining room colliders | table pilate/e3 |
| 9 | `::table::death_counter` (header @22263620) | 2 deaths @22263851, 3 @22263899, `::gallery::enemy_controller->fsm_send(rush_in)` @22263947, deathcam @22263997 | — | gallery wave rushes in |
| 10 | `::office_entrance::door09::t1` (`T_Activate` @22237107, header @22236778) | `::office_entrance::door_timer.DO->do_animate(1)` @22237317 -> timer block @24337797: fade, controls off @24339028, `::u::t6->fsm_switch(killersuits_coming)` @24339078, **`player->c_teleport(::office::player_teleport)` @24339172** | controls off until @24250738 | killersuit1-3 teleported to `::u::wk1..3` @24339634-24339736, nonreactive, GoTo `::office::w1..3` @24340244 |
| 11 | `::office::camera2.start` @22230193 | Punchinello scene ends: `C_SetImmortal(false)` @24250828, `C_sethealth(0)` @24250875, controls on @24250738, player invuln off @24251136, objective OBJ_MANOR_PUNCHINELLO0/1 @24251172 | controls on @24250738 | Punchinello dies |
| 12 | `::office_entrance::door01::t4` (`T_Activate` @22235220) | door2 `DO_Animate(open_end)` @22235415; only reachable after the FSM is switched to `the_end` @24251082 | `the_end` on @22235653 | killersuit4-12 teleported to `::u::endw1..9` @22235755-22236181, horne to `::u::horne_teleport` @22236235 |
| 13 | `::office_entrance::door01::door2` FSM state `open_end` | `GM_EnableExit(::u::toggle3,true)` @24323460, `::u::camera1->fsm_send(start)` @24323525 | — | — |
| 14 | `::u::camera1.DO` (end block @24810507) | widescreen off, HUD on, gamespeed 1, controls on @24812968, `player->c_setinvulnerable(false)` @24813058, notes `P3L0a_003/004` @24813094, graphicnovel @24813210, **`gm_init(part3_level1)` @24813254** | — | — |

## 3. Exit chain (FACT unless marked)
`::office_entrance::door01::t4.TRIGGER` T_Activate @22235220 -> `door2.DO->DO_Animate(open_end)` @22235415 ->
state `open_end` of `::office_entrance::door01::door2` @24323185 -> `::u::camera1->fsm_send(start)` @24323525 ->
`::u::camera1.DO` messages @24812811-24813254 -> `maxpayne_gamemode->gm_init(part3_level1)` @24813254.
There is no death counter in the exit: the FSM gate is Punchinello's scripted death setting `t4` to `the_end` @24251082.
`t4` itself is enabled/disabled with `::u::toggle3` and `::office_entrance::toggle3` (@24315633, @24319526, @22234728).

## 4. Bot traps
1. `::FOYER::Joe` invulnerable @22299355 (his initial state, `idleuntilenemyseen` + `c_setinvulnerable(true)`) and the only
   clear in the file is `::FOYER::Joe->c_setinvulnerable(false)` @22187794, fired by `::gunroom::disable_invulnerability`
   (`T_Activate` @22187716). Until then Joe cannot be damaged. FACT.
2. `::office::punchinello` immortal @22302890; cleared by script `C_SetImmortal(false)` @24250828 + `C_sethealth(0)` @24250875:
   the player never has to shoot him; killing him before that scene is impossible (immortal).
3. `::teleport::killersuit4..12` nonreactive + `c_setidle(2,true)` + `c_setinvulnerable(true)` @22308118, @22308497, @22308876,
   @22309255, @22309634, @22309956, @22306642, @22306968, @22307294. No clear anywhere in the file: they stay unkillable corpses
   and are teleported to `::u::endw1..9` @22235755. A kill list that names them can never complete.
4. `::teleport::killersuit1` invulnerable set true @24340186 (escort into the office), cleared only once at @24251308
   (different scene); `::teleport::invisible` invulnerable @22306097; `::u::vince` invulnerable @22310570, cleared @22094006.
5. Player teleport: `player->c_teleport(::office::player_teleport)` @24339172 (step 10). Activating `door09::t1` remotely moves Max
   into the office and skips the walk; everything after depends on being there.
6. `::table::ct1` remotely activated also runs `do_enablecollisions(true)` @22263025 on `::table::player_trigger`, i.e. it seals
   the room colliders without the player inside.
7. `::gunroom::give_weapons` is a use-key action on the activator (`activator->c_sendspecial()` @22189062): a remote `T_Activate`
   does not hand the weapons to Max.
8. Use-key / action buttons that only play a locked sound: `::gunroom::Action button trigger 12/13` @22185742/@22186014,
   `::office_entrance::Action button trigger 35/36/39` @22232882-22233650, `::kitchen::Action button trigger 03..13`
   @22193850-22196570, `::bedroom4::drawer_locked` @22091251.
9. Timed doors / timers: `::gunroom::joe_timer` @22189098 (`start` @22189283), `::gunroom::dialog_timer`,
   `::FOYER::camera1::timer` @22151881, `::office_entrance::door_timer` @22238209, `::office_entrance::e1_timer` @22238491.

## 5. exits.json check (`tools/ghost/exits.json` -> `part2_level4`)
The bot reads `ff` as the step list when `steps` is empty (`ghost.mjs:356`), so the five `ff` entries below are the route.
1. **Missing exit.** `"exit": []`. Real exit: `::office_entrance::door01::t4.TRIGGER` (@22235220 -> @22235415 -> @24323525 ->
   gm_init(part3_level1) @24813254). As written the bot can never end this level.
2. **Missing steps.** Nothing in `ff` reaches the office: `::office_entrance::door09::t1` (@22237107) and
   `::office_entrance::door01::t4` are absent, and `t4` is inert until Punchinello's scene sets it to `the_end` @24251082.
3. **"La vestíbulo" wrong trigger.** `::FOYER::t1.TRIGGER` @22166559 is a locked door: it plays `door_wood_locked` @22166589,
   disables itself @22166660 and `::table::t4` @22166700. It opens nothing, spawns nothing, kills nothing.
4. **"El sótano" wrong trigger.** `::CellarRoom4::t1.TRIGGER` @22131471 is the dodge tutorial (@22131592-22131760 sets
   `::cellarroom3::e2` and `::CellarRoom4::E1` dodge states). The mobster fight starts at `::CellarRoom4::trigger_mobsters`
   @22131834 -> `::CellarRoom6::e1` gotoandshoot `::CellarRoom8::w1` @22132073.
5. **"La armería" needs the player.** `::gunroom::give_weapons.TRIGGER` (@22189046) only works as a use action on Max
   (`activator->c_sendspecial()` @22189062). The ambush that follows is `::gunroom::script` @22193341. `kill: ["::gunroom"]`
   is fine as a room prefix, but no kill is needed to get the weapons.
6. Correct: `::gallery::t6.TRIGGER` @22249756 (opens the piano door, @22185640), `::table::ct1.TRIGGER` @22262954 (dining ambush).
   `send` `::u::camera1->fsm_send(start);` is a real line @24323525 but it is the door01 `open_end` message, not a gm_init sender.
7. All `tp` waypoints exist: `::CellarRoom4::w1` @22048327, `::FOYER::w1` @22049247, `::gunroom::w1` @22050003,
   `::office_entrance::w5` @22051271, `::table::w1` @22052199.

Not verified: which of `::FOYER::Joe` / gunroom e1-e2 / gallery / table count as the "trio" of `OBJ_MANOR_TRIO`; the exact
execution order of the two 24.x MB message blocks (the file stores each object's messages far from its header).
