# part1_level3b (1_Part1_Level3b.ldb)

Source: `build/data/lvl/1_Part1_Level3b.ldb` via `tools/ldb_dump.py`.
Caveat: strings under 5 chars are dropped by the dump, so handler names like `init`, `die`, `end` are
invisible; grouping lines into one handler is INFERENCE. Offsets are file offsets.

Rooms in the file: StartRoom, apartment, wc, outdoor1, before_gognitti, blood02, g2, LaundryStreet,
rooftop1 + rails03, rails, dirtyroom, hallway2, money_laundry.

## 1. Entry (what runs at load)

- FACT @25869377 `mphm_fadetocolor(0x000000ff,0x00000000,0.5)`,
  @25869444 `::StartRoom::camera1->FSM_Send(start)`,
  @25869485 `a_playmusic(city_ambience)`, @25869534 `::StartRoom::post_process.DO->DO_Animate(2)`.
  INFERENCE: these are the load hook (`StartRoom::post_process`), the level's own start.
- FACT `::StartRoom::camera1` handler `start` @24258456: `GM_SetPlayerControls(false)` @24258456,
  **`player->c_setinvulnerable(true)` @24258559**, `GM_EnableWidescreen(true,0)` @24258594,
  `CAM_AttachToDO(100,false)` @24258359, `DO_Animate(1)` @24258414.
- FACT end of that cutscene @25819938..@25820283: speed 1, `GM_SetPlayerControls(true)` @25819991,
  **`player->c_setinvulnerable(false)` @25820081**, widescreen off @25820117, HUD @25820169,
  `CAM_AttachToDO(0,false)` @25820283, then `::StartRoom::d_timer_junkie_speak->FSM_Send(check)`
  @25820404.
- FACT: no `c_setimmortal` in the file. No `GM_TransferPlayerWeapon` at load; the level starts with the
  weapons the previous level left (the apartment scene removes one, see 4).

## 2. Player path, ordered

Order is INFERENCE from unlock messages, not a single linear table in the file.

| # | step (exact name) | what it fires | windows | enemies |
|---|---|---|---|---|
| 1 | load: `::StartRoom::post_process.DO` | fade, music, `StartRoom::camera1` start @25869444 | controls off @24258456, invuln @24258559, released @25819991/@25820081 | - |
| 2 | `::apartment::take_weapon.TRIGGER` @24104575 | `T_Activate` @24104784: disables itself @24104799, `::apartment::Ingram->li_activatelevelitem(true)` @24104852, `::apartment::ct1.TRIGGER->T_Enable(false)` @24104903, `::apartment::script->fsm_send(request_state_switch)` @24104948 | - | `::wc::e1` set to meleecombat @24104134 |
| 3 | `::before_gognitti::door_trigger.TRIGGER` @24305217 | handler `T_Activate` @24116098: disables itself @24116113, `::before_gognitti::mover::camera1->fsm_send(start)` @24116173 | controls off @24116711, invuln @24116823, widescreen @24116980 | Gognitti `::1::gognitti->C_Teleport("::outdoor1::w1")` @24117246; player `c_teleport(::before_gognitti::player_holder)` @24117190 |
| 4 | `::before_gognitti::mover.DO` (handler `start` @24116701) | `GM_TransferPlayerWeaponTo(::teleport::Max)` @24117862, `GM_EnableExit(::blood02::exit_in,true)` @24117927 | closes: controls/invuln restored in the mover anim | - |
| 5 | `::rooftop1 + rails03::t_train.TRIGGER` @24218604 | `::rails::train01.DO->DO_Animate(move)` @24218781, disables itself @24218822 | - | - |
| 6 | `::rooftop1 + rails03::train_move.TRIGGER` @24218886 | `::rails::old.DO->do_animate(move)` @24219135, disables itself @24219074 | - | - |
| 7 | `::rails::ct.TRIGGER` @24210458 | `::rails::camera3->fsm_send(start)` @24210657 | - | - |
| 8 | `::rails::g_trigger.TRIGGER` @24210701 | `::rails::camera2->fsm_send(start)` @24210906, `do_enablecollisions(false)` @24210943 | - | - |
| 9 | `::rails::jump_trigger.TRIGGER` @24314533 | `T_Activate` @24211216: disables itself @24211231, `::rails::camera6->fsm_Send(start)` @24211281 | controls stay on until camera7 ends | - |
| 10 | jump onto the passing train | `::rails::train::end_trigger.DO->do_enablecollisions(false)` + `gm_init(part1_level4)` @24214718 | - | - |

## 3. Exit chain (the only `gm_init` in the file, @24214718)

1. FACT `::rails::jump_trigger` @24211062: handler `T_Activate` @24211216 disables itself @24211231 and
   sends `::rails::camera6->fsm_Send(start)` @24211281.
2. FACT `::rails::camera6.DO` @25537598 ends by sending `::rails::camera5->fsm_send(start)` @25539906;
   `::rails::camera5.DO` @25534813 sends `::rails::camera7->fsm_send(start)` @25537121;
   `::rails::camera7.DO` @25541344 ends with `GM_SetPlayerControls(true)` @25543757,
   `player->c_setinvulnerable(false)` @25543963, `CAM_AttachToDO(0,false)` @25543999,
   `::rails::train->FSM_Send(initial_move_back)` @25544107, `a_playmusic(just_forget)` @25544154,
   `::teleport::Max->C_SetStateMachine(nonreactive)` @25544201.
3. FACT the train is armed inside its own FSM: `::rails::train::end_trigger.DO->do_enablecollisions(true)`
   @25649012 sits next to the `move`/`move_back` states (@25648502, @25648973, @25649250).
4. FACT `::rails::train::end_trigger` @24214363 has collision handlers whose only two messages are
   `::rails::train::end_trigger.DO->do_enablecollisions(false)` @24214524 / @24214656 and
   **`maxpayne_gamemode->gm_init(part1_level4)` @24214718**. INFERENCE: those are
   `DO_CharacterCollides`-style handlers (a DO event table, not a TRIGGER), so nothing but the player's
   own body on the moving train fires them.
5. FACT no other object calls `gm_init`; there is no death counter (no `death_counter` string).

## 4. Bot traps

- FACT: the exit is a **moving-object collision**, not a trigger. `end_trigger` only has collisions
  after @25649012, and the player must be standing where the train arrives. `T_Activate` on it does
  nothing (it has no TRIGGER).
- FACT: scripted instant damage on the roof/rails:
  `:rails::old.DO` @24211854 collision -> `activator->c_sethealth(0)` @24212125 (instant death), and
  `::rails::rails::kill_trigger` @24212421 -> `activator->c_sethealth(0.01)` @24212700 (leaves the player
  at 1 HP). A bot standing on the wrong rail dies with no enemy in sight.
- FACT: `::before_gognitti::mover` is a cutscene that teleports the **player**
  (`c_teleport(::before_gognitti::player_holder)` @24117190) and Gognitti @24117246; activating
  `::before_gognitti::door_trigger` "from outside" drops the player into the mover's holder anyway,
  so the camera is not skipped, but the approach walk is.
- FACT: `::apartment::take_weapon.TRIGGER` @24104575 hands the player the level item Ingram
  (@24104852) and disables itself; it is not a door.
- FACT: `::apartment::take_weapon.TRIGGER->t_enable(false)` @24103514 is inside
  `::apartment::remove_weapon_timer` (`DO->do_animate(0)` @24103332), i.e. the game can close that
  trigger on a timer before the player reaches it.
- FACT: no immortal actor; no invulnerable actor is left armed (the `c_setinvulnerable(true)` lines are
  all the player's, during cutscenes @24116823, @24118595, @24147740, @24181576, @24206929, @24258559).

## 5. exits.json check

Entry `part1_level3b`: `steps: [jump_trigger]`, `exit: []`, `send: [... train::end_trigger.DO->do_enablecollisions(false); ... gm_init(part1_level4)]`.

- CORRECT: step `::rails::jump_trigger.TRIGGER` (@24314533 in the trigger table, object @24211062)
  exists, is enabled at load (its only `T_Enable` call is its own handler @24211231) and does start the
  camera6/5/7 chain (@24211281).
- CORRECT: `exit: []`. There is no trigger that ends the level; `gm_init` @24214718 lives in a
  do_CharacterCollides table (@24214524/@24214656), so no name can be "activated" to end it.
- WRONG: `why` says the DO table runs "exactly two lines ... and maxpayne_gamemode->gm_init(part1_level4)".
  There are **three** strings there: two `do_enablecollisions(false)` (@24214524, @24214656) and the
  `gm_init` (@24214718); the second one is a second handler, not part of the same line list.
- MISSING: the arming step. The train only has collisions from @25649012, so the `send` injection is
  replacing a physical event that the level gates on the train's own `move` animation. Without the
  camera7 block (@25543652..@25544201) the train never starts `initial_move_back`.
- MISSING: nothing in `steps` warns that `::rails::old` @24211854 kills the player on collision
  (@24212125) while the bot walks to `::rails::w3`; the `ff` tp is the only protection.
- MINOR: `next: part1_level4` is right (@24214718).
