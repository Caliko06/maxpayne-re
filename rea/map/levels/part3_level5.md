# part3_level5 (Part III, chapter 5a: the Woden building)

Source: build/data/lvl/1_Part3_Level5.ldb via tools/ldb_dump.py. FACT = string read at that offset;
INFERENCE = my grouping of strings into objects/events. Area order across the building is INFERENCE
from the script's own teleports and from the `ff` list, not from geometry.

## 1. Entry (FACT)
- `::conf::fsm_start` @27777683: gamespeed 1, fade, notes `P3L5a_001`..`P3L5a_005` @27777937-27778224, `x_modeswitch->s_modeswitch(graphicnovel)` @27778282, `::conf::post_process.DO->do_animate(1)` @27778326, objective `OBJ_ASGARD_ASSASINS` @27778368, two empty goals @27778433/27778481.
- `::conf::post_process.DO` @28635753 sends `::conf::camera1->FSM_Send(start)` @28637189: the intro is a camera chain on animation end.
- `::conf::camera1` start @27775362: widescreen, `CAM_AttachToDO(100,false)`, fade in, `GM_SetPlayerControls(false)` @27775685, `player->c_setinvulnerable(true)` @27775735, `GM_TransferPlayerWeaponTo(::conf::max)` @27775856 (weapons held by the scripted Max person).
- camera1.DO @28580573 -> animate 2 @28582916 + camera2 start @28583485; camera2 start @27776196 attaches, animates, `::conf::invis_pad.DO->DO_Animate(1)` @27776324, `::s::ks2->c_goto("::s::w1")` @27776364, timer @27776397; camera2.DO @28586028, timer.DO @28586833 animates 2/3 @28588082/28588449.
- The intro's last step (`::yard::camera3.DO` region @28965394, strings 28967701..28971105): `::conf::max->C_SetStateMachine(singledodgeforward)` @28967701, max to `::conf::window_waypoint` @28967755, `::s::ks2` teleported and set to shoot @28967809/27867896, `::yard::window->FSM_Send(break)` @28968313, slow motion @28969003, grenade @28969118, then `CAM_AttachToDO(0,false)` @28970161, widescreen off, hud on, `GM_SetPlayerControls(true)` @28970430, `player->c_setinvulnerable(false)` @28970479, `player->c_teleport(::yard::wp01)` @28970632, music `byzantine` @28970716, and it kills the scripted actors `::conf::ic1..ic3`, `::conf::woden`, `::s::ks1/ks2`, `invis_pad::invisible` @28970762-28971003.
- So the player starts in the yard outside the Woden building, in control, with their own weapons back.

## 2. Player path (FACT offsets, INFERENCE order)
| # | step | trigger / object | fires | windows | enemies |
|---|---|---|---|---|---|
| 1 | intro | `::conf::fsm_start`, camera1 -> camera2 -> invis_pad -> yard::camera3 | section 1 | controls OFF 27775685 / ON 28970430; invuln 27775735 / off 28970479 | scripted `::s::ks1/ks2` |
| 2 | 1st floor lift | `::1st_floor::t_lift` @27735509 | `::1st_floor::d_lift.DO->do_animate(1)` @27735509, self-disable @27735550 | none | `1st_floor` guards |
| 3 | office | `::office::t_enemy` @27832951 | self-disable @27832951, `::office::t01` plays `merc_noise` @27832997 | none | `office`, `office06` |
| 4 | security room | `::securityroom::t_enemy` @27854897 | `::securityroom::d_timer.DO->do_animate(1)` @27854897, self-disable @27854942 | the timer is a small cutscene: keyboard sound, `t_doorcode` off @28859260, panel hidden, `sec_door01.DO->do_animate(open)` @28860106 | `securityroom`, `sec` |
| 5 | basement | `::basement05::t_ai` @27764938 | `activator->c_setstatemachine(crouchandshootstatic)` @27764938, self-disable @27764992 | see traps: this trigger belongs to an enemy, not to the player | `basement04`, `basement05`, `cellar` |
| 6 | mine | `::basement04::t_mine` @27760692 | arms `d_mine03` @27760860, plays `mine_armed` @27760962, `Activator->C_SendSpecial()` @27761027, `::basement04::e01->C_SetInvulnerable(false)` @27761056 | ends e01's invulnerability | e01 (killer-suit boss) |
| 7 | the dome | `::Backdome13::t_tv` @27748686 | TV switch, `::Backdome13::t_lookat` off @27748853, note `P3L5a_009` @27748959, `graphicnovel` @27749017, TVs on @27749108-27749202 (second half switches them off @27749795-27750247) | a comic page over the TV | `Backdome13`, `top`, `top2` |
| 8 | notes on the way | `::00_Staircase09 + 00_BackOffice + corr09::note_01_trigger` @27718767 | note `P3L5a_008` @27719116, `graphicnovel` @27719174, `d_tape` hidden @27719218, `t_lookat` off @27719289 | comic page | none |
| 9 | exit corridor | `::00_XFilesCorridor02::d_door04::t_door` @27723737 | see section 3 | fade + `gm_init` | none |

Other note beats on the way (each one opens a comic page and nothing else): `P3L5a_007` @27834560,
`P3L5a_010` @27744276, `P3L5a_006` @27853783. `::corr04::d_door03.DO` @27780508 only plays `radio_tv_static_loop`.

## 3. Exit chain (FACT)
1. `::00_XFilesCorridor02::d_door04::t_door` T_Activate @27723944 -> `::00_XFilesCorridor02::d_door04.DO->do_animate(open)` @27723959 and `...::t_door.TRIGGER->t_enable(false)` @27724015.
2. `::00_XFilesCorridor02::d_door04.DO` animation end (@28316618) -> `a_play3dsound(door_wood_medium_open)` @28318015, `mphm_fadetocolor(black, 0.9)` @28318101, `maxpayne_gamemode->gm_init(part3_level5b)` @28318174.
3. Nothing else gates this door: there is no `GM_EnableExit` call for `::00_XFilesCorridor02::d_door04::t_door` anywhere in the file, and the only other line touching it is its own self-disable. No death counter participates (FACT: no `death_counter` object in this level).
4. The `GM_EnableExit(..., true/false)` calls in this level (@28348585, @28559542, @28640856, @28642676, @28659234, @28810756, @28850102, @28874346, @28893785 ... @28904187) belong to the engine's own per-area exits (`::top::exip`, `::hall::exit_hall02`...), all enabled by door/clear scripts, not by the gm_init chain.

## 4. Bot traps
- `::basement04::e01` (killer-suit, mp5 + 99999 ammo) is invulnerable at spawn: `C_SetInvulnerable(true)` @27892684 in his own script, `c_setstatemachine(nonreactive)` @27892542, and he disables `::basement04::t_mine` @27892733 and `::basement05::t_ai` @27892782. The message that ends it is `::basement04::e01->C_SetInvulnerable(false);` @27761056, and it is inside the `::basement04::t_mine` T_Activate handler. No mine, no kill: the boss stays unkillable.
- `::basement05::t_ai` T_Activate acts on the ACTIVATOR: `activator->c_setstatemachine(crouchandshootstatic)` @27764938. It is meant for an enemy patrolling into it (it is disabled once e01 spawns @27892782). If the bot sends `T_Activate` on it, the activator is the player: the player gets an NPC combat state machine. Never fire it remotely.
- `::conf::invis_pad::invisible` is the cutscene's bullet sponge: `C_SetStateMachine(nonreactive)` @27895195 and `C_SetInvulnerable(true)` @27895259, and `::s::ks1/ks2` target it @27899418/27899766. Killing it is pointless (script only).
- `::securityroom::t_doorcode.TRIGGER` is disabled by the security-room cutscene @28859260; opening the code door out of order leaves `sec_door01` closed with no way back to the timer.
- Intro: `fsm_send(start)` on camera2 / invis_pad / yard::camera3 without camera1 leaves controls off (@27775685) and the player invulnerable; jumping to the last step's strings teleports the player to `::yard::wp01` @28970632 while the scripted actors were never killed, so the cutscene actors stay in the world.
- Action buttons (use key) on the path exist only for optional content in this level: e.g. `::office06::New mesh 04::cupboard-door` @27806764 and the drawer triggers that play `drawer_locked` @27803775/27804049/27804323/27804603. The only door the exit needs is a contact trigger, `t_door`, not an action button.
- Timed door: `::securityroom::sec_door01.DO->do_animate(open)` @28860106 comes out of an animation chain (`d_timer.DO` steps @28858789-28859769), roughly 4 animations, not a wall clock; a bot that re-enters before the chain ends finds the door closed.

## 5. exits.json check (tools/ghost/exits.json -> part3_level5)
- `"steps": []` with five `ff` entries: the real gates are `1st_floor::t_lift` @27735509, `office::t_enemy` @27832951, `securityroom::t_enemy` @27854897, `basement05::t_ai` @27764938, `Backdome13::t_tv` @27748686 (all five exist, names correct).
- `ff[3]` uses `::basement05::t_ai.TRIGGER` as a player gate, but that trigger acts on its activator @27764938 and is meant for an enemy; as a player step it is wrong (it must be the mine, `::basement04::t_mine` @27760692, that frees e01). Missing `kill`: `::basement04::e01` is invulnerable until @27761056.
- `ff[4].kill = ::Backdome13 / ::top / ::top2`: `t_tv` only switches TVs and opens note `P3L5a_009` @27748959; killing those areas is not what the trigger does. Wrong grouping, not wrong names.
- `"exit": ["::00_XFilesCorridor02::d_door04::t_door.TRIGGER"]` is correct as the sender @27723944; the record's `"why"` cites @27723761 (the trigger object) and @28316642/@28318194 for the open state and `gm_init`. Offsets are 300-600 bytes early but name the right strings.
- Missing from the level record: the invulnerable boss `::basement04::e01` (@27892684 -> @27761056) and the note/comic pages that take control for a moment (`P3L5a_006` @27853783, `P3L5a_008` @27719116, `P3L5a_009` @27748959, `P3L5a_010` @27744276). A bot that never triggers them misses no gate but does miss the story beats.
- Not verified (UNKNOWN): the exact walk order between yard, 1st/2nd floor, corridor01/corr04, top and top2; this file is 15.5k lines and the area exits are `GM_EnableExit` triggers whose enabling scripts I only sampled.
