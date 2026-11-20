# part1_level6 (1_Part1_Level6.ldb)

Source: `build/data/lvl/1_Part1_Level6.ldb` via `tools/ldb_dump.py`.
Caveat: strings under 5 chars are dropped by the dump (this is why the Exit FSM's handler names
`add1`/`add2` are invisible while `add_lupino` survives). Grouping lines into a handler is INFERENCE.

## 1. Entry (what runs at load)

- FACT `::innersanctum::trigger` @6683070 (a DO object with collisions): its collision handler sends
  `::innersanctum::cam01->fsm_send(start)` @6683280 and `::innersanctum::trigger.DO->do_enablecollisions(false)`
  @6683322. The player's own body starts the opening cutscene.
- FACT `::innersanctum::cam01` @6668647, handler `start`: widescreen @6668851, HUD off @6668902,
  fade @6668954, `CAM_AttachToDO(100,false)` @6669016, speed .8 @6669072,
  **`GM_SetPlayerControls(false)` @6669126**, **`player->c_setinvulnerable(true)` @6669176**,
  `::Sanctum Corridor::door02.DO->do_animate(2)` @6669217.
- FACT end of cam01 @6779347: detach @6780902, `GM_SetPlayerControls(true)` @6781009,
  `player->c_setinvulnerable(false)` @6781058, fov 1 @6781094.
- FACT `::innersanctum::cam04` @6669389, handler `start` @6669540: attach @6669550, animate(1)
  @6669606, speed .7 @6669649, fade @6669703, `::teleport::lupino->C_SetIdle(4,true)` @6669769,
  `A_Play3DSound(ragnarock, P1L5a_06)` @6669811; ends @6783056 with controls on @6783372 and
  invuln off @6783421.
- FACT three dummy actors are killed at load: `::StartRoom::Character 00/01/02->c_kill(1)`
  @6696108, @6696366, @6696624 (INFERENCE: the altar corpses of the graphic novel).
- FACT: the only `gm_init` in the file is @6853452.

## 2. Player path, ordered

Order is INFERENCE (unlock messages).

| # | step (exact name) | what it fires | windows | enemies |
|---|---|---|---|---|
| 1 | walk-in: `::innersanctum::trigger` (collision, @6683070) | `cam01->fsm_send(start)` @6683280, own collisions off @6683322 | controls off @6669126, invuln @6669176, released @6781009/@6781058 | - |
| 2 | `::innersanctum::cam04` (auto after cam01) | Lupino idle 4 + dialogue `P1L5a_06` @6669769/@6669811 | released @6783372/@6783421 | - |
| 3 | `::innersanctum::Combat_Trigger_01.TRIGGER` @6703066 or `..._02.TRIGGER` @6703223 | `T_Activate` @6671895 / @6672353: `::innersanctum::Combat_Controller.DO->dO_animate(Start_Combat)` @6671972 / @6672492, `church_bell` @6672038, both triggers disabled @6672118 / @6672368 | - | - |
| 4 | `Start_Combat` @6826489 | `::teleport::End_A01->c_teleport("::innersanctum::A01")` @6826609, `End_B01` @6826667, `End_C01` @6826725 | - | three columns start, one enemy each |
| 5 | curtain reveal (part of 3/4) | speed .8 @6674126, controls off @6674180, invuln @6674230, curtains `DO_Animate(1)` @6674271/@6674318, `::innersanctum::Curtain_Cam::timer` @6674365, `::teleport::lupino->C_Teleport("::innersanctum::w1")` @6674421, `e1->nw1` @6674527, `e2->nw2` @6674580 | controls off / invuln during the reveal | Lupino + 2 guards appear |
| 6 | kill column A | `End_A01` death -> `End_A02->C_Teleport("::innersanctum::A01")` @6697509, then A03 @6697819, A04 @6698140, A05 @6698460 | - | 5 in the column |
| 7 | kill column B / C | B02 @6699092, B03 @6699411, B04 @6699826, B05 @6700145; C02 @6700767 | - | 5 + 2 |
| 8 | last of each column dies | `End_A05` record `::innersanctum::Lupino_FSM->FSM_Send(touch)` @6698782, `End_B05` @6700467, `End_C02` @6701078 -> `Lupino_FSM` `touch` @6678225: `fsm_switch(2)` @6678245, `FSM_Switch(3)` @6678297, `lupino_delay_timer.DO->do_animate(1)` @6678349 | - | - |
| 9 | Lupino joins for real | `::teleport::lupino->C_SetStateMachine(merccombat)` @6804191, **`::teleport::lupino->C_SetImmortal(false)` @6804244**, `Lupino_Controller.DO->DO_Animate(1)` @6804288, `e1/e2->C_GoToAndShoot` @6804343/@6804403 | - | Lupino becomes killable here |
| 10 | kill `::teleport::e1`, `e2`, `lupino` | `::innersanctum::Exit->FSM_Send(add1)` @6696914, `add2` @6697207, `add_lupino` @6701377 -> `Exit.DO->DO_Animate(animation_0)` @6675727 | death cam `::teleport::lupino->CAM_AnimateParented(death_02)` @6675779 | exit |

## 3. Exit chain (verified)

1. FACT `::innersanctum::Exit` @6675074 is an FSM with three identical bodies
   (`FSM_Switch(2)`, `FSM_Switch(3)`, `::innersanctum::Exit.DO->DO_Animate(animation_0)`
   at @6675295/@6675341/@6675387, @6675462/@6675508/@6675554, @6675635/@6675681/@6675727).
   The third body is the one that also runs `::teleport::lupino->CAM_AnimateParented(death_02)`
   @6675779, i.e. the state reached by the last add.
2. FACT the three add senders (`add1` @6696914 in the `::teleport::e1` record, `add2` @6697207 in
   `::teleport::e2`, `add_lupino` @6701377 in `::teleport::lupino`) are **not** load messages:
   `::teleport::lupino` cannot be "added" before the fight because the same record sets
   `c_setimmortal(true)` @6701275 and the level does not end at load.
3. FACT `::innersanctum::Exit.DO` @6852125, animation `Animation_0` @6853170:
   `MPGNM_MarkAllNotesRead()` @6853295, `MPGNM_PickupNote(P1L5b_008)` @6853350,
   `X_Modeswitch->S_Modeswitch(graphicnovel)` @6853408,
   **`maxpayne_gamemode->gm_init(part2_level0)` @6853452**, speed 1 @6853496.
4. FACT: no TRIGGER leads to `gm_init`; `Distinct gm_init` count in the file = 1 (@6853452).

## 4. Bot traps

- **FACT: Lupino IS immortal for most of the fight.** `::teleport::lupino->c_setimmortal(true)`
  @6701275 (his spawn record) and `::teleport::lupino->C_SetImmortal(false)` @6804244, immediately
  before `Lupino_Controller.DO->DO_Animate(1)` @6804288. exits.json's claim that "Lupino is not set
  invulnerable in the .ldb" is wrong for `immortal` (which is the stronger flag).
- FACT: the two guards `::teleport::e1`/`e2` are only teleported in during the curtain reveal
  (@6674527, @6674580), at the same moment the player loses controls (@6674180) and goes invulnerable
  (@6674230). A bot that activates `Combat_Trigger_01` remotely runs the reveal with the player
  elsewhere in the room.
- FACT: `::innersanctum::Combat_Trigger_01.TRIGGER` and `_02.TRIGGER` are **enabled at load**: the
  only lines that touch them are `t_enable(false)` @6671910, @6672118, @6672368, @6672430. Either one
  starts the fight; both are disabled after the first.
- FACT: the wave columns are chained one-death-to-one-teleport; killing `End_A01` is what brings
  `End_A02` (@6697509). A bot that only kills the names it can see will stall the column.
- FACT: the three `touch` messages come from the *last* member of each column (`End_A05` @6698782,
  `End_B05` @6700467, `End_C02` @6701078), so the fight's second phase (immortal off) needs all three
  columns finished, not just one.
- FACT: `::Sanctum Corridor::Action button trigger 00` @6683445 is a use-key switch
  (`::Sanctum Corridor::switch.DO->do_animate(1)` @6683638 + `door01.DO->do_animate(1)` @6683686).
- FACT: `::innersanctum::Player collide trigger 03` @6682962 plays `mobster_alert` @6682977 and nothing
  else; `::innersanctum::Character collide trigger 00/01/02/04/05` (@6669998, @6670250, @6670555,
  @6670850, @6671145) are enemy-side triggers that set `activator` combat states, so they only fire if
  an enemy walks in.

## 5. exits.json check

Entry `part1_level6`.

- **WRONG: `why` says "Lupino is not set invulnerable in the .ldb".** He is immortal:
  `c_setimmortal(true)` @6701275, cleared only by `C_SetImmortal(false)` @6804244. Any bot that shoots
  him before the `Lupino_Controller` animation runs loses that time.
- CORRECT: `steps[0]` `::innersanctum::Combat_Trigger_01.TRIGGER` and its effect
  (`Combat_Controller.DO->dO_animate(Start_Combat)` @6671972).
- WRONG: `exit: ["::innersanctum::Exit.DO"]` names a DO object. The call is
  `::innersanctum::Exit.DO->DO_Animate(animation_0)` @6675727; the animation is `Animation_0`
  @6853170. No trigger reaches it.
- MISSING: `kill` lists the waves and the final three but nothing states that `::teleport::e1`/`e2`
  only exist from the curtain reveal (@6674527/@6674580) and that `::teleport::lupino` is teleported
  twice (`::innersanctum::w1` @6674421 in the reveal, then combat states at @6804191).
- MISSING: the `why` says the Exit FSM "counts 3 deaths ... then Exit.DO->DO_Animate(animation_0) ->
  graphicnovel + gm_init(part2_level0) @6853452". The add offsets are right (@6696674 / @6696961 /
  @6701132 are the record *names*; the sends are @6696914 / @6697207 / @6701377), but the FSM body that
  runs at the third add is @6675727, and `graphicnovel` is fired inside `Animation_0` @6853408, after
  `MPGNM_PickupNote(P1L5b_008)` @6853350.
- CORRECT: there is no TRIGGER to `gm_init` (@6853452 is the only one), so no `send` is needed; the kill
  list is the lever.
