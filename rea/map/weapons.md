# Weapons (MaxPayne.exe 1.05, rebased VAs): data table, sounds, parsers, runtime offsets

Goal: check "game data table == runtime readings" (WORKLIST #10). Feeds #11 (sounds).
Sources: the OWNER's x_data.ras (read-only, extracted to build/data/x_data, gitignored), Ghidra corpus rea/c/MaxPayne.exe,
capstone disasm of build/img/MaxPayne.exe (independent of Ghidra), debug-dump label strings, host/gamestate.c runtime reads.
Hopper: NOT re-run here (tools/rea/node_modules is missing in this checkout). "Hopper agrees" below is only what earlier
rows in rea/map/names/MaxPayne.exe.tsv recorded. Scripts and CSVs: build/data/weapons/ (310_summary.txt, sounds.tsv).
FACT = read in data or code. INFERENCE = marked. Conf: H = two independent sources, M = one source plus consistent context, L/UNPROVEN = one source.

## 0. Where the data lives (correction to the brief)
- d11 (Level_Items) = pickups only: `<weapon>.txt` and `ammo_*.txt` (TriggerRadius, MatchingWeaponID, OnPickUp `C_PickupWeapon/C_PickupAmmo`).
- The weapon definition is `310_<weapon>.txt` (dir d310, "weapons" scripts, 17 files incl. `empty` and `painkiller`). Numbers are macros in
  `3_shooting.h` (clip, pocket, frequency, damage, spread), `3_weaponid.h` (ids), `3_object_lods.h` (LOD distances), `3_level_items.h` (pickup amounts).
- Projectile defs = d63 `bullet_*`, `case_*`, `clip_*`, `grenade`, `molotov`, `frag_molotov`, `damage_*`, `enemy_*` (drop on death).
- Sounds = `235_weapons.txt` (group `weapons`, wavs in d309) and `235_projectiles.txt` (group `projectiles`, wavs d297-d302). Muzzle flashes = `38_particles.txt` -> d62 `*.rps`.
- Meshes: `.kf2` in x_data (d313 beretta, d324 ingram, d327 jackhammer, d331 m79, d335 mp5, d343 sniper ...). Textures are loose files in
  `data/database/weapons/<name>/textures/` (e.g. Beretta.jpg, Ingram02.jpg, Colt_Commando.jpg, d_eagle01.jpg, M79_2.jpg, pumpsg.JPG+pumpbarl.JPG, sawnoff.jpg+barrel.tga, Jackhammer02_256x128.JPG, LeadPipe.JPG, BaseballBat.JPG, Molotov.JPG+Rag.JPG, FlackGrenade.JPG, mp5.jpg; sniper dir has only McMillanCasing.JPG, main texture not found there).
- All 63 `.kf2` names the 16 weapon scripts reference exist in x_data (checked by basename). The 11 muzzle `.rps` exist in d62.

## 1. Weapon table
Ids: `3_weaponid.h`. Slot = SlotIndex (HUD slot). Pool = InventoryID (ammo pool shared by weapons with the same number).
Frequency = shots per second; the engine uses MaximumShootingFrequency (= Default + 0.001) so interval = 1/(mult * Max) (section 3).
Mult = ShootingFrequencyMultiplier (bullet-time: 0 = shoots slower with slow motion, 1 = constant real rate). ReloadAmount: -1 = ClipSize.
Source of every number: `310_<w>.txt` Attributes (macro in `3_shooting.h`); damage/spread/speed from the bullet file.

| id | weapon | slot/pool | clip | max ammo (Pocketsize) | freq Def/Max | mult | ReloadAmount | bullet file(s) | dmg/bullet | spread (Accuracy) | speed |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | leadpipe | 0/1 | 1 | 1 | 1.46/1.47 | 0 | -1 | bullet_leadpipe | 1 + melee blast 9->8.99 r0.8 | 10 | 20 |
| 2 | baseballbat | 0/2 | 1 | 1 | 1.26/1.27 | 0 | -1 | bullet_baseballbat | 15 + melee blast 15.01->15 r1.2 | 10 | 30 |
| 3 | beretta | 1/3 | 18 | 162 | 4/4.001 | 0.25 | -1 | bullet_beretta | 5 | 100 | 50 |
| 4 | berettadual | 1/3 | 18 per hand (36 loaded) | 162 | 4/4.001 | 0.25 | -1 | bullet_berettadual | 5 | 200 | 50 |
| 5 | deserteagle | 1/4 | 12 | 48 | 3/3.001 | 0.25 | -1 | bullet_deserteagle | 10 | 50 | 50 |
| 6 | sawedshotgun | 2/5 | 2 | 80 | 2/2.001 | 0.25 | 2 (=clip) | straight x1, bullet_sawedshotgun x2, _group x2 | 5 per pellet, 5 pellets | 20 (straight), 400 | 50 |
| 7 | pumpshotgun | 2/5 | 7 | 80 | 1/1.001 | 0.2 | 1 (one shell per reload) | straight x1, bullet_pumpshotgun x2, _group x2 | 5 per pellet, 5 pellets | 20, 150 | 50 |
| 8 | ingram | 3/6 | 50 | 300 | 8/8.001 | 0 | -1 | bullet_ingram, bullet_ingram_noeffects | 5 | 300 (noeffects shots: 400) | 50 |
| 9 | ingramdual | 3/6 | 50 (shared dual rule) | 300 | 8/8.001 | 0 | -1 | same two | 5 | 300 / 400 | 50 |
| 10 | mp5 (uses the Colt Commando model) | 3/8 | 30 | 150 | 6/6.001 | 0.8 | -1 | bullet_mp5 | 10 | 25 | 50 |
| 11 | jackhammer | 2/9 | 12 | 168 | 3/3.001 | 0.8 | -1 | straight x1, bullet_jackhammer x2, _group x2 | 5 per pellet, 5 pellets | 20, 300 | 50 |
| 12 | molotov | 4/11 | 1 | 10 | 1/1.001 | 0.1 | 1 | molotov (+ frag_molotov x5) | 50 direct; fire blasts 20..8 | 125 | 10 |
| 13 | grenade | 4/10 | 1 | 10 | 1/1.001 | 0 | -1 | grenade (fuse 2.0 s) | 1 direct; blast 50 (r2..5), 20 (r2..10) | 20 | 20 |
| 14 | m79 | 4/12 | 1 | 10 | 1/1.001 | 0.8 | -1 | bullet_m79 (gravity 0.2) | 10 direct; blast 50 (r1..5) x2 | 50 | 30 |
| 15 | sniper | 5/7 | 5 | 20 | 1/1.001 | 0.8 | -1 | bullet_sniper (+2x, 4x camera variants) | 70 | 10 | 400 |
| 16 | painkiller | 10/13 | 1 | 8 | 0.5/0.51 | 0.8 | -1 | none | - | - | - |

Notes (FACT unless marked):
- Death cause: shotgun class (1) for deserteagle, baseballbat, all pellets and m79; molotov/fire = burning (2); others random (0). 3_projectiles.h PROJECTILEDEATH_*.
- Beretta and berettadual share pool 3 (both Pocketsize 162); pump + sawed share pool 5 (80); ingram + ingramdual share pool 6 (300). The exe refuses a DB where
  weapons of one pool differ in PocketSize (exception InconsistentPocketSize, sortAndValidate 0x53bfc0).
- Shotgun total per trigger pull = 5 pellets x 5 = 25 if all hit (1 straight + 2 + 2). Group projectiles differ from single ones only in the hit decal (bullethit_shotgun_group).
- Ingram: char anim alternates `WS_AnimateShooting(SHOOT)` and `SHOOT_NOEFFECTS` at frames 2,5,8,11 (161_default_skeleton.txt:1485-1488); the noeffects bullet file uses the INGRAMDUAL spread macro (400). Data quirk, keep as is.
- Spread: m = rand01 * Accuracy / 2500 added to a unit direction (gameplay.md "Flow of a shot" step 5, from asm). INFERENCE: Beretta 100 -> up to 0.04 per axis (about 2.3 deg); not re-derived here.
- Projectile speed also has SpeedRandom: 5% (beretta, dual, deagle, ingram, noeffects) , 0% (mp5, sniper), 1% (m79, grenade), 5% molotov.
- Dual weapons: loadClip gives up to 2x ClipSize split over two hands (0x729a70); the clip reading is the sum of both hands (0x729ce0).
- Pickup amounts (3_level_items.h): box = AMMOBOX_*: beretta 30, deagle 20, shotgun 10, ingram 100, sniper 5, mp5 30 (file `ammo_coltcommando`), jackhammer 20, m79 3.
  Weapon pickup = PICKUPAMMO_*: beretta 10, dual 10, deagle 5, sawed 4, pump 5, ingram 25, ingramdual 25, sniper 2, mp5 10, jackhammer 10, m79/grenade/molotov 1. `ammo_shotgun` gives `sawedshotgun` ammo (pool 5). Difficulty scales these (AmmoFoundMultiplier, 1_difficulty.txt).
- Selection lists (1_weaponpriority.txt): best = jackhammer, mp5, ingramdual, ingram, pump, sawed, deagle, beretta dual, beretta, bat, pipe (no sniper, no throwables). Cycle = bat, pipe, deagle, beretta dual, beretta, jackhammer, pump, sawed, mp5, ingram dual, ingram, m79, grenade, molotov, sniper.

### 1b. Effects per weapon (what fires on SHOOT / RELOAD)
Messages are in the weapon script animation (`310_<w>.txt`, frame number after the colon) unless marked "char anim" (the character animation in `161_default_skeleton.txt` / `179_max_payne.txt`).

| weapon | mesh LOD0 (x_data) | muzzle flash (38_particles -> d62 rps) | shell projectile | clip drop on reload | shoot sound | reload sound |
|---|---|---|---|---|---|---|
| leadpipe | leadpipe_lod0.kf2 | - | - | - | shoot_leadpipe (char anim 179:704 frame 9, 29) | - |
| baseballbat | baseballbat_lod0.kf2 | - | - | - | shoot_baseballbat (char anim 179:717 frames 7, 30) | - |
| beretta | beretta_Lod0/Lod1.kf2 | Muzzle_Beretta -> beretta.rps | case_9mm f1 | clip_beretta f0 | shoot_beretta f0 | reload_beretta f0 |
| berettadual | beretta_lod0/1.kf2 | Muzzle_Beretta (char anim, right f0, left f8) | case_9mm f1 | clip_beretta f5 | shoot_beretta (char anim 161:1367, 1369) | reload_beretta f5, f15, f17 |
| deserteagle | deserteagle_Lod0.kf2 | Muzzle_Deserteagle -> deserteagle.rps | case_50cal f1 | clip_beretta f6 | shoot_deserteagle f0 | reload_deserteagle f0 |
| sawedshotgun | sawedshotgun_Lod0.kf2 | muzzle_sawedshotgun -> sawedshotgun.rps | Case_12ga_sawed x2 (on reload f0) | - | shoot_sawedshotgun f0 | reload_sawedshotgun f0 |
| pumpshotgun | pumpshotgun_Lod0.kf2 | Muzzle_pumpshotgun -> pumpshotgun.rps | Case_12ga f16 | - | shoot_pumpshotgun f0; cock shoot_pumpshotgun_cock (char anim 179:1232 f14) | reload_pumpshotgun x3 variants (char anim 179:766 f17) |
| ingram | ingram_Lod0.kf2 | Muzzle_ingram -> ingram.rps (char anim 161:1451) | case_9mm | clip_ingram f0 | shoot_ingram (char anim 161:1452) | reload_ingram f0 |
| ingramdual | ..\ingram\ingram_lod0.kf2 | Muzzle_ingramdual -> ingram_dual_cloned.rps (char anim 161:1475) | case_9mm | clip_ingram f0 | shoot_ingramdual (char anim 161:1476) | reload_ingram f0 |
| mp5 | coltCommando_Lod0.kf2 | muzzle_coltCommando -> coltcommando.rps (muzzle_mp5 -> mp5.rps is defined, no weapon uses it) | case_300cal | clip_coltcommando f0 | shoot_coltCommando f0 | reload_mp5 f0 (also char anim 179:373, 478) |
| jackhammer | jackhammer_Lod0.kf2 | Muzzle_Jackhammer -> jackhammer.rps | - | clip_jackhammer f0 | shoot_jackhammer f0 | reload_jackhammer f0 (char anim 161:830) |
| molotov | molotov_lod0.kf2 | - (molotov_smoke on impact) | - | - | shoot_throwable f0 | - |
| grenade | grenade_Lod0.kf2 | - | - | - | shoot_throwable f0 | - |
| m79 | m79_Lod0.kf2 | Muzzle_m79 -> m79.rps | Case_40mm (reload f0) | - | shoot_m79 f0 | reload_m79_1 f3, _2 f24, _3 f30 |
| sniper | sniper_Lod0.kf2 | Muzzle_sniper -> sniper.rps | Case_300cal f18 | clip_coltcommando f0 | shoot_sniper f0; chamber_Sniper f17 | reload_sniper f0 |

- Empty gun: `WEAPONANIM_SHOOTEMPTY` points to the same kf2 as DEFAULT in every script and carries no message: no click sound when firing empty (data).
  `Shoot_NoBulletsLeft` (weapons\shoot_empty.wav) is played only when the game cannot switch to a weapon (1_globalconfig.txt:218 `[CantSwitchOnMessages]`).
- Muzzle flash and sound for beretta dual, ingram and ingram dual are in the character animation, not in the weapon script. Reload time = length of the
  character reload animation (`ReloadBeretta.kf2` ... in d163) and of the weapon RELOAD kf2: seconds not extracted (see Open).

## 2. Sound check (feeds #11)
Method: every `A_Play3DSound/A_PlayFloating3DSound ( group, name )` in the 16 weapon scripts, the weapon/ammo/projectile files of d63, the character
animations that name `weapons`, and the decals the bullets create (9_decals.txt) -> name looked up in 235_weapons.txt / 235_projectiles.txt -> each `Filename`
-> looked up by basename in x_data / x_music / x_english / x_level1-3 listings. Result file: build/data/weapons/sounds.tsv (66 rows, 45 references).
- **Missing: none.** Every referenced wav exists in x_data.ras (weapons wavs in d309; cases d299; clips d300; discard d301; bullet hits d298; melee hits d302; explosions d272/d297).
- Every referenced sound name has a definition (no `NO_DEFINITION`). Nothing comes from x_music or x_english.
- Limit: the check is by file name, not by directory path. The dirs agree with the group prefixes (weapons\ = d309 only).
- d309 holds 33 wavs; 28 are reached from the scripts above (shoot_*, reload_*, chamber_Sniper; `Shoot_ColtCommando` -> shoot_colt.wav, `Reload_Mp5` -> reload_colt.wav,
  `Shoot_NoBulletsLeft` -> shoot_empty.wav, `shoot_leadpipe` -> shoot_baseballbat.wav, Reload_PumpShotgun01-03).
- In the archive but reached by no script I found (x_data text only): `reload_m79.wav` (def `Reload_m79`, no caller), `reload_mp5.wav` and `shoot_mp5.wav` (no def points to them),
  `shoot_generic.wav` (def `Shoot_Generic`, no caller), `Shoot_Mp5_1shot_adpcm.wav` (def `Shoot_Mp5`: the player MP5 uses `Shoot_ColtCommando`). `Reload_grenade` is an alias of reload_m79_3, no caller.
- Variants: Reload_PumpShotgun has 3 files (random pick); bullethit_generic has 8; case/clip sounds 2-3 each. Frequency is randomised (`Random = 5%`, `Pitch = 22050`) in 235_weapons.txt.
- Projectile-side sounds: case_* (Casehit_generic, case_12ga_ricochet/freeze, case_40mm_*), clip_* (clip_ricochet/freeze, clip_jackhammer_*), enemy_* drops (handgun_ricochet, Bullethit_Grenade_Harmless), grenade (Bullethit_Grenade + bullethit_40mm = explosion_grenade.wav), molotov (explosion_molotov via decal, fire_projectile for level_molotov), bullets: only through decals (bullethit_*, melee hit wavs for bat and pipe).
- Pickup sounds (level_items group, `pickup_handgun`, `pickup_ammo`, `pickup_ammo_full`) are not checked here.

## 3. In MaxPayne.exe: parsers and runtime layout
### 3a. Parsers and accessors
| what | address | evidence A | evidence B | conf |
|---|---|---|---|---|
| weapon def parse (one `310_*.txt`) | 0x53b0e0 `X_SharedDBWeapon::parse`; Variables table 0x882df8 (init 0x53ab80); anim set 0x53da50 -> wdef+0x9c | Ghidra (names TSV) | field-name strings in the exe: ClipSize, PocketSize, ReloadAmount, WeaponID, InventoryID, SlotIndex, MaximumShootingFrequency, DefaultShootingFrequency, ProjectileOffset, SidewaysAimingMultiplier, UseMultipleGunBones, TwoHandedBones (strings at file 0x480a04-0x480bd8). Name-to-offset pairing is by the parse copy only | M |
| wdef offsets | +0x36 UseMultipleGunBones (u8), +0x3d InventoryID, +0x41 WeaponID, +0x45 ClipSize, +0x49 PocketSize, +0x4d ReloadAmount, +0x5d MaximumShootingFrequency, +0x61 DefaultShootingFrequency, +0x65 ShootingFrequencyMultiplier, +0x69 SidewaysAimingMultiplier | getters 0x53b850..0x53b8e0 `mov [ecx+off]` (capstone, this run) | Ghidra getters 0x53b860..0x53b8d0 (names TSV) | H for offsets, M for the name pairing of +0x49 |
| weapon DB container | load 0x53b970, sort ascending by WeaponID 0x53bfc0, `isValidWeaponID` 0x53c380, max id [DB+0x32] (0x729ec0) | Ghidra, TSV | exception strings `Weapons %s and %s had same ID`, InconsistentPocketSize | H |
| projectile def parse | 0x540490; table 0x883930; init 0x53ff90. Getters: Speed +0x38 (0x5f1b20), Damage +0x3c (0x40edf0), Accuracy +0x5e (0x574920), SpeedRandom +0x62 (0x5749b0), ExplosionDelay +0x68, Gravity +0x6e (0x541170), DamagesCharacter +0x74 (0x574670) | capstone this run (getters) | Ghidra + gameplay.md | H (offsets) |
| [Message] strings in weapon anims | WS_AnimateShooting 0x758400 (consumes a round), P_CreateProjectile 0x758790, PS_StartEffect 0x758740, A_Play3DSound 0x7586a0, WS_Animate 0x758360, WS_Hide 0x758510 | gameplay.md handler table | names TSV (Ghidra+Hopper recorded) | M |
| level item (pickup) parse | chain 0x528dc0 -> 0x529750 -> 0x544cf0 -> 0x5452b0 -> 0x5443e0 | ai.md | - | M |
| weapon priority lists | 0x723d10 `X_WeaponPriorityList::load` (BestWeapons, CycleWeapons), select 0x724590 | names TSV | strings "BestWeapons", "CycleWeapons" in the exe | H (load), L (select) |
| reload / pickup | C_Reload 0x698fe0 -> arsenal reload 0x729b30; C_PickupWeapon 0x699120 (flag only, no ammo); C_PickupAmmo 0x699570 (n x AmmoFoundMultiplier 0x676380, clamp 0x729bb0) | gameplay.md | names TSV | M |
| particle (muzzle) defs, sound-group scripts | parser addresses NOT looked up | - | - | UNPROVEN |

### 3b. Runtime layout (reach everything from the player X_Character `C`)
Chain (native, runtime-confirmed in player.md): theApp 0x8a6338 -> [+0xc8] ModeSwitch -> [+0x80] GameMode -> [+0x1080] player wrapper -> [+8] = `C`.
Enemies use the same fields on their own X_Character (their wrappers: GM+0x109c list, ai.md).

| field | location | type | source A | source B | conf |
|---|---|---|---|---|---|
| X_CharacterProperties `ST` | `C+0x22e` | struct | capstone 0x694430 `lea eax,[ecx+0x22e]` | dump 0x6d74f0 prints "X_CharacterProperties" for ecx=C+0x22e (ai.md) | H |
| **current weapon id** (= wdef WeaponID, `3_weaponid.h`) | `i32 [C+0x2a9]` (= ST+0x7b), -1 none | i32 | capstone 0x6d6fb0 `mov eax,[ecx+0x7b]` and 0x694570 -> 0x694430 -> 0x6d6fb0 | debug-dump label " Current weapon ID:" in the dump 0x6d74f0 (string present in the exe), and host/gamestate.c:142 reads it (native run: 15 = sniper at start) | H |
| X_WeaponArsenal `A` | `ptr [C+0x398]` | struct (vt 0x7d8c10) | capstone 0x6916e0 `mov eax,[ecx+0x398]` | HUD wrappers 0x694580/0x6945a0 start with call 0x6916e0; host/gamestate.c:143 | H |
| arsenal current weapon | `i32 [A+8]` = index into the DB array, -1 none. Equals WeaponID for the shipped data (ids 0..16 dense, array sorted by id) | i32 | capstone 0x7299b0 / 0x729900 index `DB.array[[A+8]]` | dump label " Active Weapon: %s" (0x729610) prints the name from the same def | H (index); equality with the id: M |
| hand weapon sets | `[A+0xc]` -> array of 2 x 0x26d bytes (right, left) | struct | capstone 0x729ce0 loop `add esi,0x26d; cmp esi,0x4da`; alloc 0x4de in 0x729040 | dump 0x729610 prints "Right Hand Weapon Set" and "Left Hand Weapon Set" | H |
| **clip (rounds loaded, one hand)** | `i32 [[A+0xc] + 0x1cb]`; left hand `+0x26d+0x1cb`; HUD number = sum of both (0x6945a0 -> 0x729ce0) | i32 | capstone 0x757f60 `mov eax,[ecx+0x1cb]`; fire 0x757f50 `dec [+0x1cb]` | dump label " Bullets left: %d" (0x757ef0 prints [this+0x1cb]); host/gamestate.c:146; native read clip 5 for the sniper (ClipSize 5) | H |
| shots fired counter | `i32 [set+0x223]` | i32 | 0x757f50 `inc [+0x223]` (Ghidra) | no label | L (role: counter only) |
| ammo pools | `ptr [A+0x10]` -> array of 0x1e-byte records, index = InventoryID of the weapon | struct | capstone 0x7299b0 `[A+0x10] + inv*0x1e` (lea x3, x5, x2) | alloc `n*0x1e+4` with n = max InventoryID+1 in 0x729040 | H |
| **reserve ammo** | `i32 pool+0` (current); if `u8 pool+0xc` (infinite) the getter returns `pool+8` | i32 | capstone 0x748250 (`test [ecx+0xc]`, `[ecx+8]` else `[ecx]`); HUD path 0x694580 -> 0x7299b0 -> 0x748250 | dump label " Ammo: %d" (0x729610 prints 0x748250(pool)); host/gamestate.c:34 (reserve) | H |
| **max ammo carried** | `i32 pool+8` = PocketSize | i32 | initPools 0x729850 -> 0x7482f0 `mov [ecx+8],arg` with arg from 0x53b890 (+0x49); 0x748260 clamps `pool+0 <= pool+8 - loaded` | Ghidra 0x748300 getMax / 0x748260; names TSV | M (pairing of +0x49 with PocketSize rests on the parse copy) |
| pool load cap | `i32 pool+4` = max ClipSize over the pool at init (0x72989b..0x7298b9, `cmp/jg` then 0x421a70); reload adds ReloadAmount (0x729b30); unload overwrites it with the hand-0 rounds (0x729a10) | i32 | capstone 0x729850 | names TSV (reload row says the same) | M. gameplay.md said "clip max", player.md "unknown": it is a running cap, not a constant |
| infinite flag | `u8 pool+0xc` | u8 | capstone 0x748250 | 0x6945b0 -> 0x7482d0 `mov al,[ecx+0xc]`; message SetInfiniteAmmo 0x699790 | H |
| **fire interval** (seconds per shot, player) | `float [state+1]` of `X_PlayerInputEvaluator::State_ShootingBase` (ctor 0x7667b0); `state+0xf` = arsenal, `state+0x13` = ST | float | capstone 0x766950 / 0x7669a0: `1.0 / (ST.mult * wdef.MaximumShootingFrequency)` | Ghidra body of the same two functions; state save 0x766810 stores +1, +5, +9..+0xc | M. Countdown field (likely +5): UNPROVEN |
| shooting frequency multiplier | `float [C+0x2b8]` (= ST+0x8a) | float | getter 0x6d72f0 (names TSV) | dump label " Shooting frequency multiplier:"; set by 0x691a80 from wdef.mult and the bullet-time scale `[C+0x494]` | H (field), M (0x691a80 formula) |
| AI interval | uses DefaultShootingFrequency (wdef+0x61, 0x53b8c0) in 0x5001f0 (large AI function, not read in full) | float | Ghidra grep only | - | L / UNPROVEN |
| weapon sets after save/load | arsenal load 0x729370, save 0x729470; pool save 0x748050 writes +0, +4, +0xc, owned map | - | Ghidra | - | L |

How to check "table == runtime" (no game run done here): for the player or any enemy, read weapon id, then
`pool = [A+0x10] + 0x1e * InventoryID` and compare `pool+8` with Pocketsize, loaded clip with ClipSize after a full reload (dual: 2 x ClipSize),
`state+1` with 1/(mult * MaxFreq), and the wdef fields via `DB.array[id]` (DB from `[A+4]`, array at DB+8) against the table in section 1.
Observed so far (player.md, native): weapon 15 (sniper), clip 5 = ClipSize, reserve 20 = Pocketsize. INFERENCE: reserve + loaded would be 25 > 20,
so that reading is probably the infinite-ammo case (getter returns pool+8); not proven. Enemy reads in out/ghost_snd*.log show clips such as 18 and 12 with res 0 (weapon id not logged), consistent with ClipSize 18 / 12.

## 4. Open (not proven)
1. Reload time per weapon (seconds): needs the kf2 length of `Reload*.kf2` (d163) and each weapon RELOAD kf2; not extracted. kf2 format not read.
2. Hopper second key not run in this session (tools/rea/node_modules missing; install is outside this task). Rows marked H use Ghidra + capstone/labels/runtime; the names TSV rows keep their earlier "Ghidra+Hopper" notes.
3. Fire countdown field in State_ShootingBase (+5?) and who calls 0x766950 / 0x7669a0 (state evaluate not read). The value `X` (+0x72 of the object from 0x691700) in 0x691a80 is unproven (it is not the wdef: 0x53b8f0 is an aliased `fld [ecx+0x72]`).
4. AI fire rate (0x5001f0 uses Default frequency) and enemy weapon choice.
5. Parsers for particle files (muzzle rps) and sound-group scripts (235_*.txt), and the PS_StartEffect target.
6. Melee hit sequence for bat and pipe (delayed explosion 0.04-0.05 s, melee flag), `P_Explosion` second block of M79/grenade (r up to 10, probably alert noise): meaning not proven.
7. The reading reserve 20 for the sniper (infinite flag or script grant?), dual-weapon clip totals (36 beretta, 100 ingram) at runtime.
8. `pool+4` semantics under shotgun partial reloads (ReloadAmount 1) at runtime.
9. Sound name `Shoot_Mp5` / `Shoot_Generic` have no caller in the data I searched (x_data text only; level scripts in x_level*.ras not searched).
10. Pickup sounds, bullet-hit materials per surface, and Sniper zoom (ShakeLength etc.) not covered.
