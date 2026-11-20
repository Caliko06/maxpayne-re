# part1_level5 (1_Part1_Level5.ldb)

Source: `build/data/lvl/1_Part1_Level5.ldb` via `tools/ldb_dump.py`.
Caveat: strings under 5 chars are dropped by the dump, so handler names like `init`/`die`/`end` are
invisible; grouping lines into a handler is INFERENCE. Offsets are file offsets.
Rooms: outside, startroom, room1, disco, disco2, bar, bar2, Attic_stairup, Attic_barrels, Towerstairs,
backstage1, backstage2, roof_back, gate, DISCO_way.

## 1. Entry (what runs at load)

- FACT `::startroom::start_fsm.DO` @29683416: `GM_EnableExit(::startroom::e_back,true)` @29682959,
  `GM_EnableExit(::room1::e_ticket,true)` @29683024, secret-door sound @29683087,
  `::room1::script->fsm_send(initialize)` @29683168,
  `::outside::cam_dummy::camera1->fsm_send(start)` @29684806. INFERENCE: `start_fsm` is the load hook.
- FACT the outside cutscene (`::outside::cam_dummy`, handler `start` @28467622): widescreen @28467632,
  HUD off @28467683, fade @28467735, `CAM_AttachToDO(100,false)` @28467795, speed 1 @28467859,
  **`GM_SetPlayerControls(false)` @28467912**, **`player->c_setinvulnerable(true)` @28467962**,
  `camera1.DO->DO_Animate(1)` @28467997, `::outside::car02.DO->do_animate(1)` @28468154,
  snow `fsm_send(start)` @28468192, `a_playmusic(skyscraper_rooftop_stormy)` @28468248,
  `gm_changefov(1.2,0)` @28468309.
- FACT end of the intro @29504791..@29505322: camera detach, widescreen off, HUD on, fade, speed 1,
  **`GM_SetPlayerControls(true)` @29505070**, **`player->c_setinvulnerable(false)` @29505119**,
  fov 1, `a_playmusic(eerie_junkie_atmosphere)` @29505263, snow stop @29505322.
- FACT: no `c_setimmortal` and no `GiveWeapon` in the file; no enemy is made invulnerable. The only
  `c_setinvulnerable` calls are the player's, inside cutscenes (@28327773, @28467962, @28993815,
  @29029981).

## 2. Player path, ordered

Order is INFERENCE (unlock messages). The level is a chain of proximity rooms ("Player collide
trigger 00" is the same object name in six rooms, each with its own namespace).

| # | step (exact name) | what it fires | windows | enemies |
|---|---|---|---|---|
| 1 | load: `::startroom::start_fsm.DO` | exits enabled @29682959/@29683024, `room1::script initialize` @29683168, outside camera @29684806 | controls off @28467912, invuln @28467962, released @29505070/@29505119 | - |
| 2 | `::Attic_stairup::speaker_trigger.TRIGGER` @28527690 | `T_Activate` @28344164: disables itself @28344179, `::Attic_barrels::script->fsm_send(initialize)` @28344240, `::outside::door1->fsm_send(start)` @28344289, `::outside::door2->fsm_send(start)` @28344326 | - | Attic barrels fight (`e1..e3` armed with sawn-off/ingram @28512406..) |
| 3 | `::Attic_stairup::t4.TRIGGER` @28344370 | `T_Activate` @28344588: locked-door sound @28344618, disables itself @28344697, opens `::disco2::door06::door2` @28344801 | - | - |
| 4 | `::backstage1::Action button trigger 00.TRIGGER` @28344853 | `T_Activate` @28345025: `drum_solo` sound @28345040 only (use key) | - | - |
| 5 | `::backstage1::Player collide trigger 00.TRIGGER` @28528933 | `T_Activate` @28364167: `::backstage1::E03` and `e4` to mobstercombat + `C_GoToPlayer(1)` @28364182..@28364329, `e5` standandshootstatic @28364366 | - | spawns the backstage fight |
| 6 | `::backstage1::Character collide trigger 00` @28354073 | `T_Activate` @28354249: `e5` combat @28354335, `::backstage1::Controller->FSM_Switch(disabled)` @28354426, `Camera_01.DO->DO_Animate(animation_0)` @28354726 | `Camera_01` @28993542..@28994235: widescreen, controls off @28993765, invuln @28993815, released @28994186/@28994235 | - |
| 7 | `::DISCO_way::Player collide trigger 00.TRIGGER` @28537344 | `T_Activate` @28463574: `door_metal_locked` sound @28463574 only | - | - |
| 8 | `::gate::Player collide trigger 00.TRIGGER` @28537830 | `T_Activate` @28464631: `door_metal_locked` sound only | - | - |
| 9 | `::roof_back::Player collide trigger 00.TRIGGER` @28540316 | disables itself (same handler shape) | - | - |
| 10 | `::backstage2::Player collide trigger 00.TRIGGER` @28530315 | `T_Activate` @28373474: disables itself @28373489, `::backstage2::Camera_01.DO->do_animate(animation_0)` @28373557 | see 3 (exit) | - |

## 3. Exit chain (verified)

1. FACT `::backstage2::Player collide trigger 00` @28373301, handler `T_Activate` @28373474:
   `::backstage2::Player collide trigger 00.TRIGGER->t_enable(false)` @28373489 and
   `::backstage2::Camera_01.DO->do_animate(animation_0)` @28373557.
2. FACT `::backstage2::Camera_01.DO` @29027829, animation `Animation_0` @29029535: widescreen @29029654,
   HUD off @29029705, fade @29029757, attach @29029819, speed .8 @29029877,
   `GM_SetPlayerControls(false)` @29029931, `player->c_setinvulnerable(true)` @29029981,
   then `DO->do_animate(animation_1)` @29030022.
3. FACT `Animation_1` @29030254: `MPHM_FadeToColor(0x0,0x000000ff,1)` @29030373, camera detach
   @29030437, widescreen off @29030493, HUD on @29030545, fade @29030597, speed 1 @29030659,
   controls on @29030712, invuln off @29030761, fov 1 @29030797,
   **`maxpayne_gamemode->gm_init(part1_level6)` @29030838**.
4. FACT: no death counter, no `add`, no `gm_init` elsewhere. The trigger is only disabled by itself.

## 4. Bot traps

- FACT: the exit is a single walk-in trigger with no condition. A remote `T_Activate` on it ends the
  level immediately and still runs the real 2-animation fade out (@29029535 -> @29030838); what it
  skips is only the ~1 s black fade and the camera, not any check.
- FACT: `::backstage1::Player collide trigger 00` @28364167 sends three enemies to the **player**
  (`C_GoToPlayer(1)` @28364237, @28364329) wherever he is, so activating it remotely pulls the fight to
  the bot instead of holding it in the room.
- FACT: `::backstage1::Character collide trigger 00` @28354073 is an **enemy**-collision trigger: it
  runs only when one of them walks into it. It is what fires the `Camera_01` cutscene @28354726, so a
  bot that kills `e5` from distance never sees that camera (and never gets the `Controller` disabled).
- FACT: `::Attic_stairup::t4.TRIGGER`, `::DISCO_way::Player collide trigger 00`,
  `::gate::Player collide trigger 00`, `::backstage1::/backstage2::Action button trigger 00` are
  action/lock triggers that play a sound and disable themselves; none opens a door except `t4`
  (which opens `::disco2::door06::door2` @28344801). Waiting at DISCO_way or gate is a dead end.
- FACT: `::outside::door04::door2::t7.TRIGGER` is disabled (@28470495) and the door is driven by a
  script (`::outside::door04::door2.DO->DO_Animate(open1)` @28510081 and @28510376), plus
  `::outside::door1/2.DO->DO_Animate(stop)` @28510131/@28510172.
- FACT: no immortal and no invulnerable enemy in this level, so no "kill him until a message" trap.

## 5. exits.json check

Entry `part1_level5`: `steps: []`, `exit: ["::backstage2::Player collide trigger 00.TRIGGER"]`,
`next: part1_level6`, two `ff` entries (speaker_trigger, backstage2 trigger with `tp ::backstage2::w1`).

- CORRECT: the exit name, the trigger offset and the `why` chain in substance:
  @28373490 (trigger handler) -> `Camera_01.DO->do_animate(animation_0)` @28373557 ->
  `gm_init(part1_level6)` @29030838.
- WRONG wording: `why` calls @28373301 "FSM". It is the TRIGGER object; the message is in `T_Activate`
  @28373474/@28373557.
- WRONG: `why` says "Camera_01.DO (widescreen, player controls off, invulnerable) ends with
  gm_init". The DO plays **two** animations: `Animation_0` puts the camera on and cuts controls
  (@29029535..@29030022), `Animation_1` takes them back and only then calls `gm_init` (@29030254 ->
  @29030838). The distinction matters: a `T_Activate` fired during `Animation_0` is ignored by the
  engine's camera state, not by the level.
- MISSING: nothing about `::Attic_stairup::speaker_trigger` being the trigger that opens
  `::outside::door1/2` (@28344289/@28344326). The `ff` entry cites "@28344180" for
  "Attic_barrels::script initialize + outside::door1/2"; the real offsets are @28344240, @28344289,
  @28344326.
- CORRECT: `steps: []` - the level has no kill requirement and no other gate before the exit.
- CORRECT: `::backstage2::w1` exists (@28310340), as does `::disco2::W02` (@28312619).
