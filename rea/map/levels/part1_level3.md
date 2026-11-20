# part1_level3 (1_Part1_Level3.ldb)

Source: `build/data/lvl/1_Part1_Level3.ldb`, read with
`python3 tools/ldb_dump.py build/data/lvl/1_Part1_Level3.ldb`.
Caveat: the dump prints only strings of 5+ chars, so a handler name like `init`, `die`, `end`, `add1`
is invisible. Lines in the file run: object name, its handler names, the messages that handler sends.
Where I group lines into one handler without seeing its name, I mark it INFERENCE.

## 1. Entry (what runs at load)

- FACT @26337020 `maxpayne_hudmode->mphm_fadetocolor(0x000000ff, 0x00000000, 0.5)` (fade from black),
  @26337087 `maxpayne_gamemode->a_playmusic(city_ambience)`,
  @26337136 `::startroom::camera1->fsm_send(start)`.
  INFERENCE: these three are the messages of `::street2::post_process.DO` (name @26335629), the level's
  load hook; no other object starts a camera without a player/message cause.
- FACT @24645023..@24645382 (`::startroom::camera1` handler `start` @24645013):
  `GM_EnableWidescreen(true,0)` @24645023, HUD off @24645074, fade @24645126, `CAM_AttachToDO(100,false)`
  @24645188, `gm_changeglobalgamespeed(.8,0)` @24645243, **`GM_SetPlayerControls(false)` @24645297**,
  **`player->c_setinvulnerable(true)` @24645347**, `DO->DO_Animate(1)` @24645382,
  `GM_TransferPlayerWeaponTo(::street2::max)` @24645430.
- FACT: cutscene chain `startroom::camera1` -> `startroom::camera2` (start sent @26109493) ->
  `street2::camera4` (start sent @26306185).
- FACT end of intro @26260962..@26261245: widescreen off, HUD on, speed 1,
  **`GM_SetPlayerControls(true)` @26261119**, **`player->c_setinvulnerable(false)` @26261168**,
  fov 1 @26261204, camera detach @26261245.
- FACT: no `c_setimmortal` and no `C_GiveWeapon`/`c_pickupweapon(player)` anywhere in the file. The
  intro moves the existing gun to the cutscene dummy (@24645430); the player keeps the weapons he had.
- FACT: no enemy is set immortal in this level; the only invulnerable actors are the four cops
  `::teleport::p1..p4` (model `nypd_cop` @24707862), invulnerable @24707875.. and never released.

## 2. Player path, ordered

Order is INFERENCE from the messages that unlock each gate (a level has no single linear list).

| # | step (exact name) | what it fires | windows | enemies |
|---|---|---|---|---|
| 1 | load: `::street2::post_process.DO` | fade, `city_ambience`, `startroom::camera1` start @26337136 | controls off @24645297, invuln @24645347 | - |
| 2 | `::startroom::camera1` -> `camera2` -> `::street2::camera4` | camera chain, then control back @26261119, invuln off @26261168 | closes the intro window | - |
| 3 | `::4::t1.TRIGGER` @24711347 | `T_Activate` @24521952: `T_Enable(false)` on itself, `::CellarRoom1::t2.TRIGGER->T_Enable(true)` @24522003, winter breath @24522048 | - | - |
| 4 | `::1stFloor01::Door::t1` / `t2` @24710368 / @24710535 | `Door.DO->do_animate(open1)`; opening sends `GM_EnableExit(::1stFloor01::exit_out,true)` @25375117 and re-enables `t1`/`t2` @25375263/@25375313 | - | - |
| 5 | `::pawnshop::trigger_bravura.TRIGGER` @24606290 | disables itself @24606434 / @24606521 | - | pawnshop fight: `::pawnshop::e1`/`e2` teleported to `::teleport::dump` @24603635/@24603685 |
| 6 | `::l1::door_dummy1::Door` | `open` @25855258 -> `GM_EnableExit(::l1::exit_to_staircase,true)` @25855329 | - | - |
| 7 | `::room3::d_door01` | @26028655 -> `GM_EnableExit(::room3::exit_to_shop,true)` @26028727 and `GM_EnableExit(::pawnshop_back01::exit_from_shop,true)` @26028794 | - | - |
| 8 | `::End_street::Action button trigger 00.TRIGGER` @24546309 | `T_Activate` @24546588, disables itself @24546615 | - | - |
| 9 | `::Alley::end_trigger.TRIGGER` @24711478 | `T_Activate` @24522825 -> `::Alley::end_timer.DO->do_animate(1)` @24522840 | - | none (see 3) |

Also FACT, same level: four invulnerable cops are dropped at the start room windows
(`::teleport::p1..p4->c_teleport("::startroom::pwN")` @24603879..@24604029, `nypd_cop` @24707862,
`c_setinvulnerable(true)` @24707875..). They are scenery for the pawnshop shootout, not targets.

## 3. Exit chain (verified, end to end)

1. FACT `::Alley::end_trigger` @24522672 has handler `T_Activate` @24522825 whose only message is
   `::Alley::end_timer.DO->do_animate(1)` @24522840.
2. FACT `::Alley::end_timer.DO` @25548426 plays animation 1, whose messages are
   `MPHM_FadeToColor(0x00000000,0x000000ff,1)` @25549806 and
   **`maxpayne_gamemode->gm_init(part1_level3b)` @25549877**.
3. FACT no other `gm_init` exists in the file, and the trigger is never disabled or enabled by any
   message (`grep end_trigger` returns only @24522672, @24711478, @24522840, the DO @25548426 and the
   object copy @26378243): it is enabled from load.
4. No death counter, no timer FSM, no kill requirement. The Alley contains only
   `end_trigger`, `end_timer`, `flame01`, `New mesh 00`, `window1/2`, `Snow02` (INFERENCE: no actor).

## 4. Bot traps

- FACT: the exit needs no kill. Teleporting the player to `::Alley::end_trigger.TRIGGER` and letting
  the collision fire skips the whole level and still produces the real fade + `gm_init` @25549877.
  Nothing in the chain checks progress.
- FACT: remotely sending `T_Activate` to the trigger does not skip a camera or a player teleport; the
  only thing skipped is the 1 s fade @25549806. This is the safest exit in the group.
- FACT: `::teleport::p1..p4` (`nypd_cop`) are `c_setinvulnerable(true)` @24707875.. and no line ever
  sets them false. A bot must not wait for their death.
- FACT: the intro window (controls off @24645297, invuln @24645347) stays open for the whole three
  camera chain; a bot that activates `::4::t1` or the Alley trigger during it is still frozen.
- FACT: `::End_street::Action button trigger 00` @24546309 is an action-button (use key) trigger that
  only plays a sound and disables itself @24546615; nothing depends on it.
- FACT: doors `::1stFloor01::Door::t1/t2` and `::l1::door_dummy1::Door::t1/t2` disable their own
  triggers when closed (@24503463/@24503514, @24571806/@24571862) and re-enable them when opened; a bot
  that walks through a closing door gets a disabled trigger, not a blocked exit (the exit is enabled by
  the DO animation, once).

## 5. exits.json check

Entry `part1_level3`: `steps: []`, `exit: ["::Alley::end_trigger.TRIGGER"]`, `next: part1_level3b`.

- CORRECT: the exit object name and its effect. `end_timer.DO->do_animate(1)` @24522840 and
  `gm_init(part1_level3b)` @25549877 confirmed.
- WRONG wording: `why` calls @24522672 an "FSM". It is the TRIGGER object; the message lives in its
  `T_Activate` handler @24522840.
- MISSING: the level opens with a three-camera cutscene (controls off @24645297, invuln @24645347)
  that is still running when the level starts; a bot's first move must wait for @26261119.
- MISSING: no `kill` field is needed, and none is present - correct, but for the record the only armed
  actors in the level are the four invulnerable cops @24707875 (never a kill target).
- UNPROVEN: the `ff` tp `::kitchen::w2` exists as a waypoint (@24498554) but nothing in the file ties
  it to `::Alley::end_trigger`; in this level it is used by enemies
  (`::room01::e2->c_gotoandshoot("::kitchen::w2",1)` @26020021). The only player teleport the file does
  is `player->c_teleport(::street2::w3)` @24678247, inside the intro camera4 handler.
