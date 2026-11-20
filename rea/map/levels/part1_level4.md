# part1_level4 (1_Part1_Level4.ldb)

Source: `build/data/lvl/1_Part1_Level4.ldb` via `tools/ldb_dump.py`.
Caveat: the dump drops strings of fewer than 5 chars, so handler names like `init`/`die`/`end` are
invisible; grouping lines into a handler is INFERENCE. Offsets are file offsets.

## 1. Entry (what runs at load)

- FACT `::rails::FSM_Start` @26925211: `GM_ChangeGlobalGameSpeed(1,0)` @26925346,
  `MPHM_FadeToColor(black,black,0)` @26925399, `::rails::post_process.DO->DO_Animate(1)` @26925464,
  `MPHM_PrintObjective(OBJ_ROOFTOPS_GOGNITTI,0)` @26925507, notes `P1L4a_001..003`
  (@26925580, @26925693, @26925751), `X_Modeswitch->S_Modeswitch(graphicnovel)` @26925809.
  INFERENCE: `FSM_Start` is the load hook (it is the only object that fades in and prints the first
  objective), and it fires the first graphic novel of the level.
- FACT `::rails::post_process.DO` @28574996 then plays animation 1: fade from black @28576385,
  `a_playmusic(elevated_city_ambience)` @28576452, `::rails::train1::camera5->fsm_send(start)` @28576510.
  So the level opens on the elevated tracks with a camera, not with player control.
- FACT: no `c_setimmortal` in the file. No weapon is given at load (no `C_GiveWeapon` /
  `c_pickupweapon` on the player); `GM_TransferPlayerWeaponTo` is only used during cutscenes.
- FACT: `::startroom::gognitti` (model `gognitti_vinnie` @26988394) carries
  `c_setinvulnerable(true)` @26988414 and the release is `C_SetInvulnerable(false)` @28446593.

## 2. Player path, ordered

Order is INFERENCE (unlock messages, not a linear table).

| # | step (exact name) | what it fires | windows | enemies |
|---|---|---|---|---|
| 1 | load: `::rails::FSM_Start` | graphic novel P1L4a_001..003, `train1::camera5` start @28576510 | intro cutscene; the file's own `c_setinvulnerable(true)` are @26818196, @26876836, @26904181, @26929176 | - |
| 2 | `::BasketballCourt03::gognitti_activate_trigger.TRIGGER` @26996580 | `T_Activate` @26818848: disables itself @26818863, `::BasketballCourt03::camera1->fsm_send(start)` @26818938 | camera @26817814/@26817864, player invuln @26818196, `::BasketballCourt03::e2->C_Setinvulnerable(false)` @26820009 | court fight, `e2` becomes damageable here |
| 3 | `::BasketballCourt03::gognitti_close_door.TRIGGER` @26818993 | `T_Activate` @26819167: `::BasketballCourt04::door1.DO->do_animate(close)` @26819251 | - | - |
| 4 | `::4::close_door01.TRIGGER` @26992043 | `T_Activate` @26783570: disables itself, `::startroom::gognitti->c_teleport("::end::w2")` @26783616 | - | moves Gognitti ahead |
| 5 | `::7::gognitti_teleport.TRIGGER` @26992937 | disables itself @26792496, `::startroom::gognitti->c_teleport("::stairs::w3")` @26792547 | - | - |
| 6 | `::construction_site::gognitti_teleport.TRIGGER` @26999208 | disables itself @26830091, `c_teleport("::construction_site::w1")` @26830158, `c_removeallweapons(empty)` @26830286 | `::construction_site::camera1` @28063095 | - |
| 7 | `::end::Door::t1.TRIGGER` @27002137 | `T_Activate` @26881893: disables itself @26881908, `::end::Door.DO->do_animate(open1)` @26881952 -> `::end::camera1->FSM_Send(start)` @28448875 | cameras 1..5 (@28432579, @28436632, @28439918, @28442459, @28443895); at the end: controls on @28446467, player invuln off @28446516, Gognitti vulnerable @28446593 | `::end::script->fsm_Send(initialize)` @28446755 arms the arena; `::end::pt1/pt2`, `second_wave_trigger1/2` enabled @28446870/@28446925 |
| 8 | `::end::second_wave_trigger1.TRIGGER` @27003582 (also `trigger_me` message) | `T_Activate` @26890413 -> `second_wave_trigger1->FSM_Send(trigger_me)` @26890438 -> `::end::script3->fsm_Send(initialize)` @26890208: teleports `::teleport::e1..e3` to `::end::t1..t3` @26888889/@26888932/@26888975 | - | second wave in |
| 9 | kill the waves and Gognitti | `::end::end_timer->fsm_Send(add)` @26880480, @26881175, @26988695 -> `::end::end_timer.DO->DO_Animate(timer)` @26884131 | - | see 3 |

## 3. Exit chain

1. FACT `::end::end_timer` @26883824 is an FSM: `FSM_Switch(2)` @26884047, `FSM_Switch(3)` @26884089,
   `::end::end_timer.DO->DO_Animate(timer)` @26884131. Messages and offsets are identical for the three
   `add` senders, i.e. the third add falls through to the DO animation (INFERENCE: the switch calls are
   the state advance; the DO call is the last state).
2. FACT the three `add` senders:
   - `::end::death_counter` @26879975, handler list `Switch(2)`,`Switch(3)`,`Switch(4)`
     (@26880212, @26880258, @26880304) then `::end::second_wave_trigger1->FSM_Send(trigger_me)`
     @26880420, `::end::end_timer->fsm_Send(add)` @26880480,
     `::end::Gognitti_reaction->fsm_Send(add)` @26880515. INFERENCE: this is the 4th first-wave death
     (`::end::e1..e4`, the four actors whose combat states start @26887755..@26887887).
   - `::end::death_counter3` @26880860: `Switch(2)` @26881081, `Switch(3)` @26881128 ->
     `::end::end_timer->fsm_Send(add)` @26881175, `Gognitti_reaction add` @26881210. INFERENCE: 3rd
     second-wave death.
   - `::end::end_timer->FSM_Send(add)` @26988695, immediately followed by
     `::end::Gognitti_reaction->FSM_Switch(disabled)` @26988730 and
     `::startroom::gognitti_dummy->fsm_send(touch)` @26988783, inside the `::startroom::gognitti` record
     block (@26988264, model `gognitti_vinnie` @26988394).
3. FACT `::end::end_timer.DO` @28455186 plays `timer` @28456455: notes `P1L4a_005..008`
   (@28456574..@28456748), `S_Modeswitch(graphicnovel)` @28456806,
   **`maxpayne_gamemode->gm_init(part1_level5)` @28456850**.
4. FACT: no TRIGGER leads to `gm_init`; the only `GM_EnableExit` calls in the file are room doors
   (@26881388 `::hallway4::Exit2`, @26881452 `::end::Exit0`, both set false at load).

## 4. Bot traps

- FACT: `::startroom::gognitti->c_setinvulnerable(true)` @26988414 (his level record, model
  `gognitti_vinnie` @26988394) and `::startroom::gognitti->C_SetInvulnerable(false)` @28446593 inside
  the `::end::camera5` animation. INFERENCE: the first line runs at load (same record shape as every
  other actor in the file), so he is unkillable until the door + 5 cameras finish. Killing him earlier
  does nothing.
- FACT: `::lower_room::e1..e3` are `C_SetInvulnerable(true)` @26982332 / @26982792 / @26983132 and only
  released @28505028 / @28505074 / @28505120, at the end of the `::inside_room::camera1` animation
  (player invuln off @28504683, controls on @28504772). Killing them before that camera is wasted.
- FACT: `::BasketballCourt03::e2` is `c_setinvulnerable(true)` @26974961 and released at @26820009
  (inside the court camera). Same pattern.
- FACT: the exit needs **three adds**, and the third one is not a plain kill of `::end::e2`; it sits in
  Gognitti's record next to `Gognitti_reaction->FSM_Switch(disabled)` @26988730 (the "he is down"
  state, see `::end::Gognitti_reaction` handler `disabled` @26885317 -> switch(2) @26885411 +
  `::end::G_timer.DO->DO_Animate(1)` @26885461 + breathing sound @26885497). INFERENCE: the player must
  finish Gognitti in the arena, not only the two waves.
- FACT: `::end::second_wave_trigger1` can be armed two ways: by `death_counter` (4th kill, @26880420)
  and by its own TRIGGER (@26890413 -> @26890438). A bot that walks into the armed trigger brings the
  second wave in while the first is alive; a player only reaches it at the same moment death_counter
  fires, so that state is not one a player sees.
- FACT: `::end::Door::t1.TRIGGER` is disabled at load (@26881833) and re-enabled only by the door's own
  `open2` handlers (@26900406, @28449034, @28449831); a bot that activates the trigger before the door
  animation is queued finds it disabled.

## 5. exits.json check

Entry `part1_level4`.

- CORRECT: `steps[0].t` `::end::Door::t1.TRIGGER` @27002137, `tp ::end::w2`, and the camera chain
  offsets (@28432579..@28443895) and @28446870 for the trigger enable.
- WRONG: step 2 `"::end::second_wave_trigger1.TRIGGER"` with `kill: [::end::e1..e4]`. The trigger does
  not wait for kills and its activation does not require them: the wave gate is
  `::end::death_counter` @26879975, whose `Switch(4)` handler sends `trigger_me` @26880420 and the
  `end_timer` add @26880480. The `kill e1..e4` belongs to the death counter, not to that step.
- WRONG: `kill` lists `::startroom::gognitti` without noting he is invulnerable until @28446593;
  as written the bot may shoot an invulnerable actor and never see the third add.
- WRONG wording: `exit: ["::end::end_timer.DO"]` names a DO object, not a trigger. The exact call is
  `::end::end_timer.DO->DO_Animate(timer)` @26884131, and the animation name is `timer` @28456455.
- MISSING: the first wave is `::end::e1..e4` and it is already placed in the map (combat states
  @26887755..@26887887, nonreactive @26977996..); no spawn point, no teleport for them.
- MISSING: the `why` says the FSM "needs 3 'add' and then runs end_timer.DO->DO_Animate(timer)
  @26884131". True, but the third `add` @26988695 is Gognitti's own event, not a wave death; the note
  that "its record also sets invulnerable" is correct and is the reason the kill order matters.
