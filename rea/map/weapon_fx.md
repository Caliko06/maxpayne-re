# Weapon and impact sounds + effects map

Sources: `build/data/x_data` (extracted `x_data.ras`), `rea/map/weapons.md`. Every row of the TSV
(`out/bec/weapon_fx.tsv`, 156 filas) carries `file:line`, and the generator asserted the expected
token on each cited line, so no line number is a guess. Handlers in the exe: `A_Play3DSound`
0x7586a0, `PS_StartEffect` 0x758740, `P_CreateProjectile` 0x758790 (weapons.md §3a),
`D_CreateDecal` 0x708410 (gameplay.md:176). FACT unless marked.

## 1. Per weapon (shoot / reload / empty / shell / muzzle / tracer)

| weapon | shoot sound | reload sound | empty click | shell drop | muzzle effect | tracer |
|---|---|---|---|---|---|---|
| leadpipe | shoot_leadpipe (161:1295,1296) = Shoot_baseballbat.wav (235w:78) | - | - | - | - | none (63_bullet_leadpipe:44 speed 0) |
| baseballbat | shoot_baseballbat (161:1319,1321) = Shoot_baseballbat.wav (235w:99) | - | - | - | - | none (63_bullet_baseballbat:44) |
| beretta | shoot_beretta (310_beretta:17) = shoot_beretta.wav (235w:120) | reload_beretta (310_beretta:26) = Reload_Beretta.wav (235w:398) | none | case_9mm f1 (310_beretta:20) -> Casehit_generic (63_case_9mm:19) | Muzzle_Beretta -> beretta.rps (310_beretta:19, 38p:3823/3834) | tracer_9mm_lod0-2 (63_bullet_beretta:10), visible >45 m/s (3_projectiles.h:9) |
| berettadual | shoot_beretta x2 (161:1367,1369) | reload_beretta f5/15/17 (310_berettadual:24-26) | none | case_9mm f1 (310_berettadual:18) | Muzzle_Beretta R f0 / L f8 (161:1368,1370) | tracer_9mm (63_bullet_berettadual:10) |
| deserteagle | shoot_deserteagle (310_deserteagle:21) = Shoot_Deserteagle.wav (235w:141) | reload_deserteagle (310_deserteagle:30) | none | case_50cal f1 (310_deserteagle:24) -> Casehit_50cal (235p:2102) | Muzzle_Deserteagle -> deserteagle.rps (38p:3849) | tracer_50cal (63_bullet_deserteagle:10) |
| sawedshotgun | shoot_sawedshotgun (310_sawedshotgun:22) = Shoot_SawedShotgun.wav (235w:204) | reload_sawedshotgun f0 (310_sawedshotgun:32) = Reload_SawedShotgun.wav (235w:461) | none | Case_12ga_sawed x2 **on reload** (310_sawedshotgun:33) -> case_12ga_ricochet (63_case_12ga_sawed:19) | muzzle_sawedshotgun -> sawedshotgun.rps (38p:3864) | tracer_12ga (63_bullet_sawedshotgun:11) |
| pumpshotgun | shoot_pumpshotgun (310_pumpshotgun:20) = Shoot_PumpShotgun.wav (235w:225); + Shoot_PumpShotgun_Cock f14 (179:1232) = _Cock.wav (235w:246) | Reload_PumpShotgun f17 char anim (179:766), 3 variantes (235w:482,496,510) | none | Case_12ga f16 (310_pumpshotgun:25) -> case_12ga_ricochet (63_case_12ga:19) | Muzzle_pumpshotgun -> pumpshotgun.rps (38p:3879) | tracer_12ga (63_bullet_pumpshotgun:10) |
| ingram | shoot_ingram (161:1452) = Shoot_Ingram.wav (235w:162) | reload_ingram (310_ingram:35) = Reload_Ingram.wav (235w:440) | none | case_9mm cada disparo (310_ingram:22,29) | Muzzle_ingram -> ingram.rps (161:1451, 38p:3894) | tracer_9mm (63_bullet_ingram:10) |
| ingramdual | shoot_ingramdual (161:1476) = Shoot_IngramDual.wav (235w:183) | reload_ingram (310_ingramdual:35) | none | case_9mm (310_ingramdual:22) | Muzzle_ingramdual -> ingram_dual_cloned.rps (161:1475, 38p:3909) | tracer_9mm (63_bullet_ingram:10) |
| mp5 | shoot_ColtCommando (310_mp5:24) = Shoot_Colt.wav (235w:288) | reload_mp5 (310_mp5:29) = reload_Colt.wav (235w:531) | none | case_300cal (310_mp5:23) -> Casehit_generic (63_case_300cal:19) | muzzle_coltCommando -> coltcommando.rps (38p:3954); muzzle_mp5/mp5.rps sin uso | tracer_9mm (63_bullet_mp5:10) |
| jackhammer | shoot_jackhammer (310_jackhammer:21) = Shoot_Jackhammer.wav (235w:351) | reload_jackhammer (310_jackhammer:31) = Reload_Jackhammer.wav (235w:573) | none | none (no crea case_*) | Muzzle_Jackhammer -> jackhammer.rps (38p:3969) | tracer_12ga (63_bullet_jackhammer:10) |
| sniper | shoot_sniper (310_sniper:21) = Shoot_Sniper.wav (235w:309) + chamber_Sniper f17 (310_sniper:24) = chamber_Sniper.wav (235w:330) | reload_sniper (310_sniper:31) = Reload_Sniper.wav (235w:552) | none | Case_300cal f18 (310_sniper:25) -> Casehit_generic | Muzzle_sniper -> sniper.rps (38p:3924) | bullet_7_62mm_l2.kf2, siempre visible (63_bullet_sniper:10,41) |
| m79 | shoot_m79 (310_m79:21) = Shoot_m79.wav (235w:373) | reload_m79_1 f3 / _2 f24 / _3 f30 (310_m79:28-30; 235w:615,636,657) | none | Case_40mm f0 **on reload** (310_m79:31) -> case_40mm_ricochet (63_case_40mm:19) | Muzzle_m79 -> m79.rps (38p:3984) | bullet_40mm_lod0/1.kfs, visible (63_bullet_m79:9,45) |
| molotov | shoot_throwable (310_molotov:22) = shoot_throwable.wav (235w:57) | - (solo WS_Hide, 310_molotov:29) | none | none | molotov_smoke en impacto (63_molotov:16) | n/a |
| grenade | shoot_throwable (310_grenade:21) | none | none | none | ninguno; item_drop + Bullethit_Grenade al botar (63_grenade:20,21) | n/a |
| painkiller | none (310_painkiller:16) | none | none | none | none | n/a |
| empty | none (310_empty:12) | - | - | - | - | - |

Empty click: every `WEAPONANIM_SHOOTEMPTY` points at the DEFAULT kf2 and carries no `[Message]`
(e.g. 310_beretta:29, 310_ingram:39). The only "no bullets" sound in the whole game is
`Shoot_NoBulletsLeft` -> `weapons\shoot_empty.wav` (235_weapons.txt:15), played from
`[CantSwitchOnMessages]` when the game cannot switch weapons (1_globalconfig.txt:218). FACT.

## 2. Impact per material (one shared branch, resolved by material name)

`[Decal]` in the bullet file names a decal group (e.g. 63_bullet_beretta:20 `bullethit_generic`).
That group plays ONE floating 3D sound + ONE particle system (9_decals.txt:248,251) and picks the
bitmap by material. Both the particle group (38_particles.txt:3279+) and the sound group
(235_projectiles.txt:7+) carry `[Material] Name="..."` blocks with the same names as the decal.

| surface | decal bitmap (radius m) | particle .rps | sound wavs |
|---|---|---|---|
| concrete | **no existe**: cae a Default `Hole_Default.jpg` 0.052-0.055 (9_decals:294-298) | bullethit-default.rps (38p:3290) | default_hit_01..08.wav (235p:13..111) |
| wood | `Hole_Wood.jpg` 0.040-0.060 (9_decals:484-488) | bullethit-wood.rps (38p:3454) | wood_hit_01..04.wav (235p:1259,1273,1287,1301) |
| metal | `Hole_Metal.jpg` 0.040-0.045 (9_decals:374-378) | bullethit-metal.rps, chispas (38p:3359) | metal_hit_01..04 (235p:585,599,613,627); hollow 4 propios (235p:647), solid 4 propios (235p:743) |
| flesh | **sin decal**: `Character` esta en NoDecalMaterial (9_decals:253); sangre `blood_small` 0.25-0.30 (9_decals:1689) via CharacterHit | bullethit-character.rps para `flesh` y `character` (38p:3324) | character_hit_01..03.wav (235p:229,243,257) |
| glass | `Hole_Glass.jpg` 0.25-0.30 (9_decals:324-328) | bullethit-glass.rps (38p:3329) | glass_hit_01..03.wav (235p:373,387,401); bulletproof = mismos (235p:421) |

- Structures check out: `3_materials.txt` has `[Flesh]` (285), `[Glass]` (306), `[Metal]` (474),
  `[Wood]` (957), `[Stucco]` (810), `[Rock]` (705) and **no `[Concrete]`**. INFERENCE: in-game
  "concrete" walls ship as Default or Stucco, so they sound like `default_hit_*`.
- FACT: glass sound is shared with bulletproof glass; only the decal bitmap differs
  (`melee_glass.jpg`, 9_decals:334-336).
- INFERENCE (not proven in code): the engine hands the hit material to the sound group so
  `[Material] Name="wood"` is selected, because `D_CreateDecal` 0x708410 only receives the group
  name and the group carries per-material blocks. Where the material index is read is UNKNOWN here.
- UNKNOWN: `flesh` has a particle and a decal (blood) but the sound group has only `character`.
  Whether a `flesh` surface falls back to character_hit or to default_hit was not traced.
- Muzzle .rps contain no wav references (strings on 43_bullethit-metal.rps / 62_beretta.rps):
  flashes are visual only, the shot sound comes from the weapon sound group. FACT for those files.

## 3. Decal groups used by bullets (which weapon hits which)

`bullethit_generic` beretta/berettadual/ingram/ingramdual (63_bullet_beretta:20, 63_bullet_ingram:20);
`bullethit_noeffects` ingram NOEFFECTS shots (63_bullet_ingram_noeffects:20);
`bullethit_large` deserteagle/mp5/sniper (63_bullet_deserteagle:19, 63_bullet_mp5:20,
63_bullet_sniper:25); `bullethit_shotgun` + `_group` sawed/pump/jackhammer
(63_bullet_pumpshotgun:19,63_bullet_pumpshotgun_group:19); `bullethit_leadpipe` /
`bullethit_baseballbat` melee (63_bullet_leadpipe:23, 63_bullet_baseballbat:24);
`grenade_m79` (63_bullet_m79:25), `grenade` (63_grenade:33), `molotov` (63_molotov:23).
The `_group` pellet set differs from the single one only by decal + a large-impact particle
(9_decals:952, 38_particles is shared). Reacting to the size of the hole: FACT in data.

## 4. Unverified / open

- No per-material impact sound for melee beyond bat/pipe groups (235p:1515,1784) — checked, they do
  have their own per-material wavs (wood/metal/glass/character), good.
- Reload durations in seconds not extracted (kf2 lengths): UNKNOWN.
- `weapons.md` already lists most of the muzzle/shell mapping; this file adds the line numbers, the
  per-material sound branch and the empty-click negative result.
