# part1_level2b (1_Part1_Level2b.ldb, the hotel bar and the roof)

Read with `python3 tools/ldb_dump.py build/data/lvl/1_Part1_Level2b.ldb --from 27070000`. Offsets are file byte
offsets. FACT = string read in the file. INFERENCE = my grouping of lines into objects/events.

## 1. Entry

| What | Evidence |
|---|---|
| `::StartRoom::fsm_start`: **`gm_setplayercontrols(false)`** @27318330, widescreen on @27318380, HUD hidden @27318428, `StartRoom::d_timer.DO->do_animate(0)` @27318480, fov 1.2 @27318522 | @27318191..@27318565 |
| Same state: notes `P1L2b_001` @27318565, mark all read @27318623, `P1L2b_002` @27318678, **`S_Modeswitch(graphicnovel)`** @27318736, fade @27318787, objectives `OBJ_HOTEL_COVER_BLOWN0/1` @27318845/@27318913 | FACT |
| Control returned by `StartRoom::d_timer.DO` step 3: **`gm_setplayercontrols(true)`** @30610374, widescreen off @30610423, HUD on @30610471, fov 1 @30610590 | @30610040..@30610590 |
| The lift from part1_level2 arrives: `::StartRoom::d_lift::t_end` (sound `switch` @27317845, TRIGGER @27366268) | FACT |
| Max keeps the weapons he carries in; nothing strips them at load. No enemy is immortal or invulnerable (FACT: 0 hits for `SetImmortal`; the only `c_setinvulnerable` lines are @27315266, @27327805, @30572162 for the player) | grep |

## 2. Player path

| # | Step (exact object) | What it fires | Controls / invuln windows | Enemies |
|---|---|---|---|---|
| 1 | `::to_bar::t_note_bar` (bar door) | `::to_bar::d_door_bar.DO->do_animate(open)` @27327529, `::b02::d_candy.DO->do_animate(1)` @27327574, **`gm_setplayercontrols(false)`** @27327610, inventory cleared @27327660, self off @27327756, **`player->c_setinvulnerable(true)`** @27327805, sound `door_locked` @27327853, `FSM_Switch(1)` @27327912, `::to_bar::d_timer.DO->DO_Animate(1)` @27327951 | cutscene: controls off + invulnerable | none moved |
| 2 | bar cutscene tail | fade @28087011, notes `P1L2b_006/007` @28087132/@28087190, `s_modeswitch(graphicnovel)` @28087248, `b02::d_candy` anim @28087292, **`gm_setplayercontrols(true)`** @28087620 | control returned | `::b02` enemies |
| 3 | the bar fight (`::b02`) | level enemies, `d_candy` animations @28087669; nothing gates the roof on them | none | none |
| 4 | `::respa::t_exit` (buzzer, respa) | switch sound @27293940, **`::roof04::door3->DO_Animate(open1)`** @27294005 and **`::roof04::door4->DO_Animate(open1)`** @27294046, buzzer @27294087, `FSM_Switch(2)` @27294151 | none | none |
| 5 | `::roof03::t_exit_doors01` (door that closes behind) | sound `door_wood_locked` @27312286; enabled/disabled by `roof04::door4` states @30579143 / @30580756 | none | none |
| 6 | `::roof04::t_Exit` (**exit**, roof) | `roof04::d_timer.DO->DO_Animate(1)` @27314675, `roof04::door3/door4->FSM_Switch(2)` @27314715/@27314749, `roof03::fsm_buzzer` stop @27315174, self off @27315221, **`player->c_setinvulnerable(true)`** @27315266 | invulnerable until the end of the cutscene | `::Teleport::e02/e03` nonreactive, `c_goto(::respa::wp04)` @27314884/@27314933; `::roof03::e01/e02` nonreactive, to `respa::wp04` @27315031/@27315127 |

## 3. Exit

1. `::roof04::t_Exit` T_Activate @27314660 -> d_timer animate 1 @27314675 (door + enemy shuffle + invulnerable, above).
2. `::roof04::d_timer.DO` @30569574: music `just_forget` @30570953, fade @30571000, **`do_animate(2)`** @30571073 ->
   camera 6 attached @30571398, **`gm_setplayercontrols(false)`** @30571449, HUD hidden @30571499, widescreen on
   @30571551, fade @30571599, camera animate 1 @30571666, `winter_breath_loop` @30571706, **`do_animate(3)`** @30571764.
3. `d_timer` step 3: fade @30572089, **`c_setinvulnerable(false)`** @30572162, **`gm_setplayercontrols(true)`**
   @30572198, HUD on @30572247, widescreen off @30572299, **`gm_init(part1_level3)`** @30572347, breath effect off
   @30572391 (FACT).
4. No FSM counts deaths for this exit: `::roof04::t_Exit` is only ever disabled by itself (@27315221) and is enabled at
   load (no `T_Enable(true)` in the file).

## 4. Bot traps

| Trap | Evidence |
|---|---|
| `T_Activate` on `::roof04::t_Exit` with Max elsewhere runs the whole ending camera (a fixed DO) and leaves him invulnerable for its duration, then hands control back where he stands: the cutscene plays over an empty roof | @27315266, @30571398, @30572198 |
| The four roof enemies are teleported to `::respa::wp04` and set `nonreactive` by the exit; if the exit is fired before they exist/are placed, the end state differs from a played run | @27314884..@27315127 |
| `::to_bar::t_note_bar` sets controls off + invulnerable and only the `d_timer.DO` tail (`@28087620`) gives control back: interrupting the bar camera strands Max without control | @27327610, @27327805, @28087620 |
| The roof doors need `::respa::t_exit` (the buzzer): without it `roof04::door3/door4` stay closed and `::roof03::t_exit_doors01` may close behind the player (@30580756 disables it in the `open1` state) | @27294005, @27294046, @30580756 |
| No `C_SetImmortal`/`C_SetInvulnerable` on any enemy in this level: no death message ends an immortal window here | grep |

## 5. exits.json check (part1_level2b)

| Verdict | Item |
|---|---|
| ok | `exit` `::roof04::t_Exit.TRIGGER` @27365987; "only ever disabled by itself, enabled at load" (FACT) |
| ok | `next` part1_level3 @30572347; the `d_timer.DO` chain cited @27314511/@30572347 matches |
| ok | ff waypoint `::Lobby::wp01` exists |
| **missing (whole list)** | `steps: []`: at minimum `::to_bar::t_note_bar` @27327069 (the bar cutscene, controls off/on) and **`::respa::t_exit` @27293735** (the buzzer that opens `roof04::door3/door4`, without it the exit is unreachable) |
| imprecise | the `why` mentions "nonreactive state for the Teleport/roof03 enemies" without the fact that they are **moved** to `::respa::wp04` @27314884..@27315127 |
