# part3_level4 (Part III, chapter 4)

Source: build/data/lvl/1_Part3_Level4.ldb, read with tools/ldb_dump.py. FACT = string read at that offset.
INFERENCE = my grouping of lines into objects/events (no .ldb header parsed).

## 1. Entry (FACT)
- `::startroom::fsm_start` @26333487: fade to black, `mpgnm_markallnotesread`, notes `p3l4a_002`..`p3l4a_006` @26333804-26333978, `x_modeswitch->s_modeswitch(graphicnovel)` @26334036 (comic page), `::startroom::post_process.DO->do_animate(1)` @26334080, objectives `obj_garage_bb0`/`obj_garage_bb1` @26334127/26334187.
- `::startroom::post_process.DO` @29019038 sends `::startroom::camera1->fsm_send(start)`: the whole intro is one camera chain driven by each camera's own .DO animation end, not by a timer.
- `::startroom::camera1` start @26318826: widescreen on, hud off, fade in, `CAM_AttachToDO(100)`, gamespeed .8, `GM_SetPlayerControls(false)` @26319115, `player->c_setinvulnerable(true)` @26319165.
- Chain: camera1.DO @28586898 -> camera2 @28587521 -> camera2.DO @28590673 -> camera3 -> camera3.DO @28613652 -> camera4 -> camera4.DO @28638316 -> camera4b -> camera4b.DO @28642817 -> camera5 -> camera5.DO @28708520 -> camera6.
- `::startroom::camera6.DO` @28711092: widescreen off, hud on, gamespeed 1, `GM_SetPlayerControls(true)` @28711359, `player->c_setinvulnerable(false)` @28711408, `player->c_teleport(::startroom::w6)` @28711543, scripted `::startroom::max` teleported away, fades.
- No weapon is given at load: the scripted Max's weapon is transferred back with `GM_TransferPlayerWeaponTo(::startroom::max)` @26322827 during the cutscene, i.e. the player keeps what they carried in.

## 2. Player path (FACT offsets, INFERENCE order)
| # | step | trigger / object | fires | windows | enemies |
|---|---|---|---|---|---|
| 1 | intro | `::startroom::fsm_start` + camera1..6 | see section 1 | controls OFF 26319115 / ON 28711359; invulnerable ON 26319165 / OFF 28711408 | none |
| 2 | garage fight | `::startroom::trigger02` @26340309 (or trigger03 @26341187, trigger05 @26341789, trigger06 @26342391, whichever fires first) | `::startroom::enemy03->C_SetStateMachine(singledodgeforward)` @26340324, `::startroom::enemy02->C_GoToPlayer(1)` @26340489, `mobstercombat` @26340589; disables the other three triggers | none | startroom::enemy02, enemy03 (already placed) |
| 3 | alley exit | `::startroom::trigger01a` @26339946 | only `Activator->C_SendSpecial();` @26340118 | none | none |
| 4 | booth door | `::booth::Player collide trigger 00` @26257023 | `A_Play3DSound(metal_drawer_locked)` @26257206: a locked box, nothing opens | none | none |
| 5 | booth phone area | `::1::trigger01` @26233216 | teleports `::teleport::bb2` to `::2::Waypointbb1` @26233216 and `::teleport::enemy14` to `::2::Waypoint14` @26233371 | none | bb2, enemy14 |
| 6 | lift shaft | `::lift_shaft::trigger` @26305930, T_Activate @26306159 | note `p3l4a_007` @26306229, `s_modeswitch(graphicnovel)` @26306287, disables trigger + `::lift_shaft::look` @26306331/26306381, radio hum @26306476 | comic page covers the screen; no controls change recorded | none |
| 7 | ramps | `::ramp02::trigger01` @26310752 / `::ramp02::trigger02` (enabled by `::startroom::trigger0` @26339790) | `Activator->C_SendSpecial()` @26310920; ramp02 enemies | none | ramp02 |
| 8 | ramp drive-by | `::ramp03::trigger01` @26313544 (or trigger02 @26313996) | `::level02::enemy01/enemy02->C_SendSpecial()` @26313711/26313750, `::level02::move.DO->do_animate(move1)` @26313852, disables both | none | level02, Level_4, Level_5 |
| 9 | level 5 window | `::Level_5::trigger03` @26303658 | `::05_Level_5::enemy04->C_SendSpecial()` -> that actor's script @26348407 turns him immortal @26348559 | immortal until 27456571 | 05_Level_5::enemy04 (sniper), enemy05 |
| 10 | BB at the booth | `::teleport::bb2` script @26364918 | removes jackhammer, `::booth::wait.DO->do_animate(wait)` @26365016, `::3::teleport_trigger` off @26365054, `::2::bb_timer` stopped @26365104, bb2 to `::3::bb3` @26365319 | none | bb2 (merccombat) |
| 11 | the phone call | `::booth::wait.DO` anim end @27960891 | objective `obj_garage_phone` @27962063, ringing @27962125, `::3::trigger01->fsm_switch(active)` @27962190, `::3::look.TRIGGER->t_enable(true)` @27962228, cutscene `::1::cam01.DO` @27962612 with controls off/on @27962737/27962845, grenade to enemy04 @27962946 | controls off ~100 bytes (short insert) | enemy04 gets a grenade |
| 12 | exit | `::3::trigger01` T_Activate @26246103, state `active` @26246237 | disables `::3::look`, `::3::cam_end.DO->DO_Animate(1)` @26246276, `CAM_AttachToDO(1,false)` @26246309 | see section 3 | none |

## 3. Exit chain (FACT)
1. `::3::trigger01.TRIGGER` touched while the FSM is in `active` -> `::3::cam_end.DO->DO_Animate(1)` @26246276 + `::3::cam_end.DO->CAM_AttachToDO(1, false)` @26246309.
2. `::3::cam_end.DO` animation end (object @27904790, its step strings follow at 27906164..27906594): `GM_EnableWidescreen(true,0)` @27906164, `gm_setplayercontrols(false)` @27906215, fade to black @27906265, widescreen off @27906336, `gm_setplayercontrols(true)` @27906388, note `p3l4a_008` @27906492, `x_modeswitch->s_modeswitch(graphicnovel)` @27906550, `maxpayne_gamemode->gm_init(part3_level5)` @27906594.
3. No death counter takes part: the level ends on this trigger, not on kills. Neither `::startroom::die` nor `::3::death` (a corpse prop, hidden @27722849) gates it.

## 4. Bot traps
- `::3::trigger01` starts in state `inactive`: T_Activate then only plays `phone_busy_tone` @26246118 and stops it @26246196. Nothing else. It becomes `active` only at @27962190, after the phone timer. Touching it earlier does nothing (this is the "trigger01 starts inactive" note in exits.json, confirmed).
- The phone is not triggered by the player: it is `::teleport::bb2`'s own script @26364918 (bb2 = the second Bareccoon/BB that arrives by script) that animates `booth::wait.DO` @26365016. Kill bb2 before that script runs and the phone never rings, so the exit never opens (INFERENCE from the dependency, not observed in game).
- `::05_Level_5::enemy04` is immortal from @26348559 to @27456571. The message that ends it is `::05_Level_5::enemy04->C_setimmortal(false);` inside the step of `::04_Level_5::move::right.DO` (the van drive-by animation end). Killing him earlier is impossible.
- `:Level_5::trigger3` T_Activate @26305610 -> `activator->c_sethealth(0)` @26305626: a kill volume. Teleporting the player onto that trigger kills them.
- Intro camera chain: `fsm_send(start)` on camera2..6 without camera1 running leaves controls off (26319115) and gamespeed .8; `fsm_send(start)` on camera6 alone enables controls @28711359 but the player is teleported to `::startroom::w6` @28711543 anyway, skipping the whole intro and the weapon transfer @26322827.
- `::lift_shaft::trigger` is a plain contact trigger whose step only opens a comic page: a remote `T_Activate` on it skips note `p3l4a_007` and the `graphicnovel` switch but changes no state the player needs.
- Timed/anim doors on the path: `::booth::door01.DO->do_animate(open)` @27723007 and `::ramp07::New mesh 07::door02.DO->do_animate(close)` @26246671 are scripted by other objects, not by the player.

## 5. exits.json check (tools/ghost/exits.json -> part3_level4)
- `"steps": []` while the level does have ordered gates: the four `ff` entries are the same gates, so the route exists only as fast-forward, with no `kill` enforcement. The gates the script itself requires are `::startroom::trigger01a` @26339946, `::lift_shaft::trigger` @26305930 and `::ramp03::trigger01` @26313544.
- `ff[1].t = "::booth::Player collide trigger 00.TRIGGER"` @26375927 is a locked box (`metal_drawer_locked` @26257206), not a gate. Wrong as a step name: it opens nothing.
- `"exit": []`: the exit trigger is `::3::trigger01.TRIGGER` @26245838 / @26246103, enabled only after the phone @27962190.
- `"send": ["::3::cam_end.DO->DO_Animate(1);"]` omits `::3::cam_end.DO->CAM_AttachToDO(1, false);` @26246309, so the ending plays without the camera attached, and it skips the state the player is in when the real trigger fires (controls still on, `::3::look` still enabled). The level still ends because the fade + `gm_init` live in cam_end.DO's own animation end @27906594 (INFERENCE from the string order).
- `ff[0].kill`: `::startroom` exists (enemy02/03 @26340324, @26340489), correct. `ff[2].kill` includes `::Level_5`: only `::Level_5::trigger1/2/3` objects carry that prefix, the sniper is `::05_Level_5::enemy04` and is immortal @26348559, so that kill entry cannot complete until the van pass @27456571.
- Missing from `ff`: the BB2 phone chain (`::teleport::bb2` @26364918 -> `booth::wait.DO` @27962190). Without it the exit trigger stays in `inactive` and the only route left is the `send` line.
