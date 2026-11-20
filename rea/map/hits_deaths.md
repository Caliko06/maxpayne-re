# Enemy hit reactions and deaths (MaxPayne.exe 1.05, rebased VAs)

Sources: Ghidra corpus `rea/c/MaxPayne.exe/*.c`, `rea/map/names/MaxPayne.exe.tsv`, `rea/map/ai_behaviour.md` 3.4,
game data `build/data/x_data/*` (x_data.ras), `build/data/lvl/*.ldb`.
Conf: FACT = code + data or two sources agree; INFERENCE = one source / by use; UNKNOWN = not read.

## 1. The chain
FACT (`rea/map/gameplay.md:236` + bodies read):
`causeDamageFromHit 0x695eb0` (bone -> multiplier) -> `causeDamage 0x695b10` (flash, apply) -> `setHealth 0x695fb0`
(clamp, wounded, flinch flag) -> `pickDeathType 0x696290` -> `die 0x6963f0` -> `State_Death::evaluate 0x764b40` (type -> anim id).
Other entries: `onCauseDamage` 0x699d40 (script `C_CauseDamage`, mode 0), `onKill` 0x69c390 (`C_Kill` 1->type 7, 2->8, then die),
`onExplosionHit 0x6976f0` (modes 2/4/5), fall damage `0x68c960:540` (`causeDamage(maxHealth*relDmg*pow(v,2.1), 3)`),
projectile hit `0x7097d0:288`.

## 2. Being hit: bone -> damage multiplier
FACT `0x695eb0`: multiplier 1.0; for each `[Target]` group (count 0x60eea0, group 0x5310f0, bones 0x735570/0x548bd0+4) it
`_strcmpi`s the hit bone name; on a match it takes the group multiplier (0x7269f0) and stops. Then
`causeDamage(mult*damage, mode)`, then `controller->vfn +0x7c(attacker)` so the AI learns who shot it.
Data `179_henchman_01.txt:47-77` (same groups in all 179_*): Head = Head/Neck, Arms = UpArm/LoArm/Hand L+R,
Legs = UpLeg/LoLeg/Foot/Toe L+R. Multipliers `3_characters.h:20-22`: **Head 5, Arms 0.75, Legs 0.5**, default 1.0.

FACT `0x695b10`: damage applies to everyone; the red flash (0x6554e0, 0x4a9ad0/0x4a9ae0/0x4a9b00) is built only when there is no
controller or `vfn+0x8c` is false, health > 0, `C+0x456 == 0`, amount >= `C+0x46c` (values in `1_globalconfig.txt` DamageFlash*).
`vfn+0x8c` = "AI-controlled" (INFERENCE-strong, by use): `getMaxHealth 0x691bd0` uses EnemyHealthMultiplier 0x676370 only when
true, PlayerHealthMultiplier 0x676390 otherwise.

## 3. Flinch
FACT `0x695fb0`: if the new health is below the current one it sets `props+5` getting-damage (0x6d6ea0) and, when AI-controlled,
`props+6` = 1 (0x6d6e80). `0x687f20:345` clears `props+6` every frame.
FACT `0x765020` (State_GetDamage): if the state is already running (own byte +6, set on enter 0x76f410) nothing happens;
else if `props+6` is set it emits anim **0x2f**, or **0x30** when the input's `vfn+0x20` says crouched. One flinch per hit,
never restarted mid-animation, same state machine as the player.
Data `161_default_skeleton.txt:377` CHARANIM_GETDAMAGE = `gethit_1000ms.kf2`, frame 0 `A_Play3DSound(enemy, mobster_gethit, head)`;
`:384` GETDAMAGECROUCHED = `gethit_crouched_0750ms.kf2`, same sound. Overrides (FACT, grep 179_*): 1250 ms Johnny/Mickey/
helper/tutorial, 1000 Buck/Horst/Michelle, 800 lab_junkie/Martin/Luciano, 650 Ray/Jamn, 500 mercenary/Mac/Candy,
350 trenchcoats, 250 bb, 200 AesirSecurity/killer suit/Woden.
Wounded (health under WoundedHealth, ST+9 written by 0x7496d0 in setHealth) switches to the limping set
`161_default_skeleton.txt:616-767`; the ST+9 consumer was not read (UNKNOWN beyond 0x7429b0:443).
AI (`ai_behaviour.md` 3.4): `props+5` is mirrored to `MT+0x3ae` and forces the perception trigger in `0x4f62a0`, so a hit wakes
a sleeping enemy. No AI machine reads health.

## 4. Death: type, animation, messages
FACT `0x696290` (argument = mode/DeathCause): 0 -> rand%4 (types 0..3), 1 -> 4 or 9 (rand%2), 2 -> 5 if 0x65a430() or rand even
else 10, 3 -> 6, 4 -> 11, 5 -> 10 if 0x65a430(). Mode from data (`3_projectileanimid.h:6-8`): RANDOM 0, SHOTGUN 1, BURNING 2 -
pistols/sniper/grenade/blood use RANDOM, all shotguns SHOTGUN, molotov and fire damage BURNING; explosion modes 2/4/5 and fall
mode 3 come from code. FACT `0x764b40` maps type -> anim id:

| type | anim | CHARANIM | file (161 line) | cause |
|---|---|---|---|---|
| 0..3 | 0x31..0x34 | RANDOMDEATH1..4 | death_minimum / death_stomach / death_staggerbackwards / death_headshot (392-444) | random bullet |
| 4 / 9 | 0x35 / 0x36 | SHOTGUNDEATH / 2 | death_shotgun_a / _b (445, 461) | shotgun |
| 5 / 10 | 0x37 / 0x38 | BURNINGDEATH / 2 | death_fire / death_fire_b (478, 500) | molotov, fire |
| 6 | 0x39 | FALLINGDEATH | death_falling (559) | fall damage (mode 3) |
| 11 | 0x3a | GRENADEDEATH | death_explosion_b (541; the `_a` at 523 is commented out) | explosion (mode 4) |
| 7 / 8 | 0x3b / 0x3c | CUSTOMDEATH1 / 2 | dead_body / dead_body2 (572, 582) | script C_Kill 1/2 |

Every death block sends, in this order: frame 0 `C_AnimateTexture(FACETEXTURE_ID, FACETEXTURE_DEATH_ID=2, 0)` (dead face
texture); frame 0 `A_Play3DSound(enemy, mobster_death, head)` (or merc/junkie/killersuit_death per class file); frame 0
(RANDOMDEATH1-4) or 38 (shotgun) `P_CreateProjectileToBone(blood_dummy, N, torso)` with N = 2 for bullet/shotgun deaths,
3 for custom deaths, 4 for explosion; frames 0 and 1 `Left/RightHandWeapon->WS_Animate(WEAPONANIM_DROPONDEATH)` (gun drops).
Then `A_Play3DSound(characters, death_medium, pelvis)` **at the frame the body lands** plus `dummy_character_land` one or two
frames later: death_minimum 40, stomach 34, stagger 34 (+51), headshot 40, shotgun 22 and 36, fire 38, explosion 38, falling 38.
Motion baked in (FACT `[Movement]`): stomach `(0,0,-1.05)`, stagger `(0.38,0,-2.26)`, headshot `(-1.43,0,0)`,
shotgun `(0,0,-3.63855)` + `c_jump(5)`, explosion `(0,0,-2.48856)` + `c_jump(6)` then `c_jump(2)` at 6/12/17, fire `(0,0,1.199)`.
Fire deaths also `PS_StartEffect(fire_death, torso)`, re-apply `damage_molotovfire` every 20 frames and spawn
`dummy_smokingbody` at 90/91. CUSTOMDEATH1/2 (`dead_body.kf2`) have no cry, no thud, no face change: pre-placed corpses.
The 2-handed deaths (`CHARANIM_2_*`, line 1005-1011) are all `[Revert]` to the 1-handed ones.

## 5. What die() does (0x6963f0)
FACT, in order: bail if `props+7` set (0x6d6eb0). If AI-controlled, scan the machine-name list `C+0x710` (0x698eb0) for
"Nonreactive" (string 0x8b6368, iterator 0x728740/0x728710); if `AI active` (0x6d7340) is **false** and there is no
"Nonreactive", send every message of the vector `C+0x1c4..+0x1c8` (0x51b6a0). Then `setIsDyingDead(1)`, `setField_83(0)`,
`C+0x220 = 0`, `C+0x75c++`. Non-AI only: push the death time into `C+0x780..+0x784`, drop entries older than the difficulty
DeathFrequency (0x676340), and if the rest >= FrequentDeaths (0x676350) set `C+0x78c = 1` (the "dying too often" path;
`1_difficulty.txt` normal 180 s / 5). Then `C+0x218 = DAT_008b4f50`, send every message of **`C+0x1b4..+0x1b8`** (always),
and clear the dead-body block `C+0x794..+0x7b4`.
Both lists are built at spawn by `buildMessageLists 0x687390` (call site 0x45141e) from the level import character's four
string vectors `+0xa4/+0xb4/+0xc4/+0xd4` -> `C+0x1f8 / 0x1c4 / 0x1b4 / 0x1d4` (FACT, 0x687390 body).
INFERENCE: the always-sent `0x1b4` list is `[OnDeath]` (every 179_* has one; all enemies contain
`this->A_StopAll3DSounds(head);`, the player `179_max_payne.txt` adds `maxpayne_gamemode->A_Playsound(story,empty);`).
The `0x1c4` list is a second per-instance list used only for an unaware enemy; its data block is UNKNOWN.

## 6. The "missing" death_medium
Not missing: it is a sound group, and groups live in the sounds database, not in the character files.
`build/data/x_data/235_characters.txt` is `database\sounds\characters.txt` (235 = archive dir index, `tools/ras.py:111`
writes `{dir}_{name}`) and defines `[Death_Medium]` at line 598: two wavs `characters\death\bodyfall_15.wav` and
`bodyfall_09.wav`, Volume 1.0, Random 5%, 3DSound yes, Hotspot `CHARACTERHOTSPOT_DEFAULT` 2.0, FallOff
`CHARACTERFALLOFF_DEFAULT` 8.0, Priority 0. FACT `0x528dc0:160` loads the DB named `"Sounds"` via 0x534d90, and every
`A_Play3DSound(<db>, <block>, <bone>)` first argument matches a `235_<db>.txt` (characters, enemy, camera, story, weapons,
dynamic) - that is why grepping 179_*/161 for the definition fails.
Enemy cries use the `SOUNDID_DEATH_*` defaults (`3_soundid.h:21-24`: 0.75 / 3 / 40 / 30) and are a different, louder group:
mobster_death 8 wavs, merc_death 6, killersuit_death 6, junkie_death 2, generic_fire_death 3; mobster_gethit 11 wavs at
volume 0.5, hotspot 1, falloff 15, random 4%.

## 7. Blood
FACT data: a bullet hitting a character fires its `[CharacterHit]` message - `P_CreateProjectile(blood_bullet_small, 1)` for
beretta/berettadual/ingram/jackhammer/pumpshotgun/sawedshotgun, `blood_bullet` for deagle/mp5/chaingun/baseballbat,
`blood_bullet_large` for sniper (all 2x/4x) and m79 (`63_bullet_*.txt`). `blood_dummy` is the death spray
(`63_blood_dummy.txt`): Speed 3, SpeedRandom 30%, Gravity 0.8, Damage 0, DamagesCharacter FALSE, trail effect
`blood_projectile_trail` at frame 0, `D_CreateDecal(blood_large)` on impact, ItemsToCache 11. `Debris = FALSE` and
`BloodProjectile = FALSE`, so the detail cull in `createProjectile 0x65ae10` (gated on getDebris 0x541110 / getBloodProjectile
0x541190) never skips death blood; only the bullet sprays are culled (they set BloodProjectile TRUE).

## 8. Kill cam
FACT (`gameplay.md:569`, re-verified): level script, not exe code. Each fight has a `::room::death_counter` FSM
(50 hits of `death_counter` in `1_Part1_Level1.ldb`) whose last state runs `<enemy>->CAM_AnimateParented(Death_0x)`
(5 sites with that exact spelling in Part1_Level1: Death_01 x2, Death_02 x1, Death_03 x2). Paths are data,
`4_camerapaths.txt:476` `[Death_01]` (3 s circle): `cinematic\Death_01.kf2`, frame 0 `GM_ChangeGlobalGameSpeed(0.2, 0.25)`,
`A_PlaySound(camera, shot_01)`, `GM_ChangeFOV(1.3, 0.8)`, frame 30 speed 0.1, frame 80 speed 0.8, exit restores HUD, speed and
FOV. `[Death_02]`/`[Death_03]` same shape (0.3 at frame 0). Handler chain `CAM_AnimateParented` on X_Character 0x69ca90 ->
X_CameraImplementation slot 24 0x672840.

## 9. What the player sees and hears
Henchman: HEALTH_CLASS_9 = 130 (`3_ai_ranges.h:9`) x EnemyHealthMultiplier 0.8 (normal default, `1_difficulty.txt:39`) = 104 hp.
- **Body hit** (beretta 5 dmg): one flinch `gethit_1000ms`, `mobster_gethit` voice at the head bone, small blood spray, 21 shots
  to kill.
- **Head hit** (25 dmg): same flinch and voice, blood spray, 5 shots to kill. There is no separate head explosion: the 5x is
  damage only, and the headshot animation (RANDOMDEATH4) comes 1 in 4 on a random bullet, never selected by the hit bone.
- **Leg hit** (2.5 dmg): same flinch and voice, blood, 42 shots to kill; no limp unless health also drops under WoundedHealth.
- **Death**: gun drops, face texture goes dead, death cry, one of 4 random deaths (or shotgun/fire/explosion/fall/custom by
  cause), a burst of 2-4 `blood_dummy`, and the body thud `death_medium` (bodyfall_15/09, 3D, falloff 8) 22 to 40 frames
  later when the body lands. Then OnDeath stops its voices, the AI detaches, and if it was the last enemy of the fight the
  level script starts the Death_0x camera with time at 0.2 and a camera shot sound.

## 10. Open
- `controller->vfn +0x7c(attacker)` target: with AP's vtable that offset is the dump 0x502540 (UNKNOWN).
- Data block behind the `C+0x1c4` list; reader of ST+9 beyond 0x7429b0:443.
- CLOSED 2026-11-05 (carga, #24): the player's death music is the `[Player_Death_Music]` sound (235_characters.txt:534, streamed, 13.527 s), sent at frame 0 of
  each Max death animation (179_max_payne.txt:565, with `a_playmusic("")` and `CAM_AnimatePlayerParented(player_death_01)`). The host plays it:
  RN_SOUNDLOG=2 on part2_level3 and part3_level7 (`player->C_SetHealth(0);`) shows at the death a new 44100 Hz 8-bit 2 ch Play and its Stop 13.13 s later
  (streamed 14.09 s), the prompt `Press the use key (E or Enter) to load the last saved game.` follows at ~10 s. Streamed buffers have no cue name in the log.
- Part III kill cam seen: part3_level7 `::startroom::e1` + `::plants::e2..e4` with `->C_Kill(1);` fire `death_counter` (RN_JUMP, out/scene_kill3). Part II: `::storage::e1/e2->C_Kill(1)` from the spawn
  shows no kill cam (those two enemies are not alive before the room's combat trigger): needs the real fight.
