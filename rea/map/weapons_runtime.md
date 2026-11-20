# Weapons runtime: shoot, hit, damage, reload (MaxPayne.exe 1.05, rebased VAs)

Read-only pass over `rea/c/MaxPayne.exe/<addr>.c` (corpus filenames are the 8-hex-digit address), `rea/map/names/MaxPayne.exe.tsv`,
`rea/map/weapons.md` §3-4 and `build/data/x_data` (`310_*.txt`, `63_*.txt`, `3_weaponanimid.h`, `179_max_payne.txt`,
`161_default_skeleton.txt`). Nothing was run in the game. FACT = read in a body or in the data; INFERENCE = shape only;
UNKNOWN = not proven here. Corpus gaps matter: 31530 of 46238 entries are not lifted, so a few handlers below have a name
row but no body in this snapshot.

## 1. The chain

1 shoot action down `0x4cbd00`/`0x4cbd30` (action byte `0x8a91ba`) → 2 fire timer `0x7429b0` →
3 interval `state+1 = 1/([C+0x2b8] * wdef+0x5d)` (`0x766950`, `0x7669a0`) → 4 the character plays a `CHARANIM_SHOOT*` clip
(data; the state→clip pick is INFERENCE) → 5 its frame-0 message `RightHandWeapon->WS_AnimateShooting(SHOOT, SHOOTEMPTY)`
→ 6 `0x758400` plays the weapon SHOOT clip and `0x757f50` consumes a round → 7 that clip's frame-0 message
`P_CreateProjectile(bullet_x, n)` → 8 `0x758790` → `0x743fc0` → `0x65ae10` spawns → 9 per-frame sweep `0x70a0d0` →
10 character hit `0x7097d0` → `0x695eb0` → `0x695b10` → health written by `0x6d71a0` at `ST+0x85` = `C+0x2b3`.

## 2. Fire (step 6-8)

- `0x758400` (`X_WeaponSet::onMessage_WS_AnimateShooting`, registered `0x756d2a`): `hasMesh 0x757c30`; `ws+0x1c7 = 0`
  (anim time); `clip = 0x757f60 (ws+0x1cb)`. clip >= 1 → `anim = msg+0x30` (the `WEAPONANIM_*` int), tested with mesh
  `vfn+0x70`, stored in `ws+0x1c3`; `0x757f50` then consumes a round (`ws+0x1cb--`, `ws+0x223++`). clip == 0 → tries
  `msg+0x34` (SHOOTEMPTY); if absent `ws+0x1c3 = 0x7fffffff` and nothing is consumed. Both paths end in `0x7530d0`
  (`X_AnimationPlayer::start`, mesh `[ws+0x1bb]`) plus `0x582ed0` (message lock release).
- `ws+0x1c3` is the `WEAPONANIM_*` index being played (`3_weaponanimid.h`: 0 default, 1 shoot, 2 shoot_nocase,
  3 shoot_noeffects, 4 shootempty, 5 reload, 6 dropondeath, `0x7fffffff` not available). FACT: the same int that is
  passed to `vfn+0x70` is the one stored there. Weapon file per index comes from `310_<weapon>.txt [Animationset]`.
- `0x758790` (`P_CreateProjectile`): `flag = (u8[ws+0x204] && u8[ws+0x205])`, then
  `0x743fc0(msg, [ws+0x17c], flag, ws+0x231, ws+0x23d, ws+0x249, flag)`; `0x743fc0` is
  `interface->vfn[0x2c/4](...)`. `ws+0x231/0x23d/0x249` = the muzzle position/direction vectors (INFERENCE from the call
  shape and `255_default.pay`-style data; not proven field by field).
- `0x65ae10` (`X_ProjectileImplementation::createProjectile`, slot 11 of `0x7d5088`): name → map `0x8b3ee8` (`0x5495e0`),
  unknown name throws `ProjectileNotFound`; `amount = 0x72f530` (def `Amount`); per unit: `0x72e5b0` allocates,
  `0x7065d0` (`X_Projectile::ctor`) initialises, `0x706ff0` starts it. Debris/Blood projectiles are skipped when
  `rand()*0x7cfc98 >= detail 0x65a390`, so casings sometimes do not appear.
- Spread/speed in `0x7065d0`: `m = (rand/32767)*Accuracy[pdef+0x5e]/2500`,
  `v = Speed[pdef+0x38] * (1 + SpeedRandom[pdef+0x62]*(2u-1))`.

## 3. Muzzle flash and shell eject: there is no eject code

Both are data. `310_beretta.txt` `[Animation] Index = WEAPONANIM_SHOOT; Filename = beretta_Shoot.kf2` carries:

```
Frame 0: A_Play3DSound(weapons, shoot_beretta)
Frame 0: P_CreateProjectile(bullet_beretta, 1)        <- the shot
Frame 0: PS_StartEffect(Muzzle_Beretta, "")           <- muzzle flash
Frame 1: P_CreateProjectile(case_9mm, 1)              <- shell eject
```

- Muzzle flash: `X_WeaponSet::onMessage_PS_StartEffect` = `0x758740` (registered `0x756f00`, syntax checker `0x6e3690`).
  The named `.rps` (`Muzzle_Beretta` → `d62 beretta.rps`) is played by the particle receiver that `X_WeaponSet` shares
  (`0x667d00`, vtable `0x7d54cc`). Body of `0x758740` is NOT in the corpus snapshot → internals UNKNOWN.
- Shell eject: the casing is a full `X_Projectile` (`63_case_9mm.txt`: `Debris = TRUE`, `DamagesCharacter = FALSE`,
  `Speed = 1.5`, `GravityMultiplier = 0.8`, `ItemsToCache = 250`, ricochet sounds), spawned by the same `0x758790`
  path on the weapon clip's frame 1. Same for `case_50cal`, `case_12ga`, `case_300cal`, `case_40mm`, `clip_*`.
- Dual/ingram: flash and sound are on the CHARACTER animation, not the weapon script; e.g.
  `161_default_skeleton.txt` `CHARANIM_SHOOTINGRAM`: frame 0 `PS_StartEffect(Muzzle_ingram)` + sound, frames 2/5/8/11
  `WS_AnimateShooting(SHOOT / SHOOT_NOEFFECTS)`.

## 4. Hit test and damage

- `0x70a0d0` (`X_Projectile::sweepAndDispatchHits`, slot 19 of `0x7d8134`) sweeps the motion segment, sorts hits by
  distance, and for characters requires: not the owner (`0x707e90` vs `0x6958b0`), `DamagesCharacter` (`0x574670`,
  `pdef+0x74`) true, `Debris` (`0x541110`, `pdef+0x51`; folded body, same code as `getInvertMouse`) false,
  `proj+0x265 == 0`, then the virtual at `+0x14`.
- `0x7097d0` (`X_Projectile::onCharacterHit`): mode from `DeathCause` (`0x60eea0` counter / data `pdef+0x7d`):
  1→1 (shotgun), 2→2 (burning), 3→5, else 0; damage = `0x40edf0` (`pdef+0x3c`, `Damage`); then
  `0x695eb0`; also fires the `[CharacterHit]` script (e.g. `blood_bullet_small`), and kills the projectile unless `Debris`.
- `0x695eb0` (`X_Character::causeDamageFromHit`): multiplies by the hit-bone group multiplier (`0x60eea0`, `0x5310f0`,
  `_strcmpi` on `[g+0x18]+j*16+4`, multiplier `0x7269f0` = `float[g+0x10]`, default 1.0) and calls `0x695b10`.
  Only two callers in the corpus: `0x7097d0` (bullet) and `0x6976f0` (explosion, empty bone name).
- `0x695b10` (`X_Character::causeDamage`): screen flash + `0x695fb0`, which computes `new = health (C+0x2b3, `0x6d71b0`) - amount`
  clamped to `[0, maxHealth 0x691bd0]`; `C+0x456` invulnerable short-circuits to `0x687f10`; `C+0x454`/`C+0x455` immortal and
  floor a non-lethal hit at `0.1`; `C+0x235` = dying/dead; writes `0x6d71a0` → `ST+0x85`. `ST = C+0x22e`, so health = `C+0x2b3`.
- Difficulty is not in this chain; it scales max health (`0x691bd0`) and ammo found (`0x699570`) only.

## 5. Reload: what it is, and where the duration comes from

- Ammo transfer is instant and has no timer. `C_Reload` handler `0x698fe0` (`[C+0x398]` → `0x729b30`):
  `unloadCurrent 0x729a10` → `pool+4 += ReloadAmount (wdef+0x4d)` → `loadClip 0x729a70`
  (`min(reserve, pool+4, ClipSize)`, x2 for `UseMultipleGunBones`) → `0x748290` subtracts from the reserve.
- The *state* is the animation. `C_Reload` is emitted by the data in the middle of the character reload clip:
  `179_max_payne.txt` `CHARANIM_RELOADBERETTA` = `anim\reloadberetta.kf2`, frame 0 hides the gun and disables bullet time,
  frame 10 `RightHandWeapon->WS_Animate(WEAPONANIM_RELOAD)` (plays `beretta_Default.kf2`, whose frame 0 drops `clip_beretta`),
  **frame 39 `this->C_Reload()`** (the rounds land). Pump shotgun: frame 17. Ingram: frame 59. Franchi/sawed: 37/20.
- Duration source: the clip length of the mesh animation, a virtual call `mesh->vfn[0x78/4](animId)` —
  `X_WeaponSet::tickAnimation 0x757e00` does `ws+0x1c7 += dt`, clamps to that length, calls `vfn+0x84(animId, time, 0)`,
  and on the end clears `ws+0x1c3` to `0x7fffffff`. `0x7530d0` uses the same slot plus `vfn+0xec` and `0x77b180` to bound
  the message-frame window and throws `"Animation message %s sent in frame %d but animation has only %d frames"`
  (`0x8a325c`). So the length is a KF2-side value: **no seconds exist in the exe or in the text data.**
  INFERENCE: the character animator counts frames (`0x753490` builds the window as `fps*time`); measure the real value at
  runtime from `ws+0x1c7` (seconds, same domain as `dt`) up to the clamp.
- Reload is not interruptible by the arsenal, only by animation state; there is no `isReloading` field in the corpus.

## 6. Per frame, a tester must read (player; enemies: same fields on their own `C`)

`C = [GM+0x1080]+8`; `A = [C+0x398]`; `ws = [A+0xc]` (right hand), `[A+0xc]+0x26d` (left hand);
`wdef = DB.array[[A+8]]` (`DB = [A+4]`, array at `DB+8`, getter `0x7299e0`).

| what | read | proof |
|---|---|---|
| weapon id | `i32 [C+0x2a9]` (-1 none) | `0x6d6fb0`, `host/gamestate.c:157` |
| clip, right / left | `i32 [ws+0x1cb]`, `[ws+0x26d+0x1cb]` | `0x757f60`, `0x757ef0` label |
| clip, total (HUD number) | `0x6945a0` → `0x729ce0` | `host/gamestate.c:161` |
| shots fired | `i32 [ws+0x223]` | `0x757f50` (role: counter, no label) |
| reserve | `pool = [A+0x10] + 0x1e*wdef+0x3d`; `inf = u8[pool+0xc]`; value = `inf ? [pool+8] : [pool]` | `0x748250` |
| reserve cap / load cap | `i32 [pool+8]` (PocketSize), `i32 [pool+4]` (running cap) | `0x729850`, `0x729b30` |
| reload state / progress | `i32 [ws+0x1c3]` (`==5` reload, `0x7fffffff` idle) with `f32 [ws+0x1c7]` s vs `mesh->vfn+0x78(animId)` | `0x758400`, `0x757e00` |
| fire interval (s) | `f32 1/([C+0x2b8] * f32[wdef+0x5d])` | `0x766950`, `0x7669a0` |
| health (dealt) | `f32 [C+0x2b3]`; enemies at `[ai+0x533]`, list `[GM+0x24c]..[GM+0x250]` | `0x6d71b0`, `host/gamestate.c:153,177` |
| dead flag | `u8 [C+0x235]` | `0x6963f0` sets it |
| shot damage of a def | `f32 [pdef+0x3c]`, `Debris u8[pdef+0x51]`, `DamagesCharacter u8[pdef+0x74]`, `DeathCause u8[pdef+0x7d]` | `0x40edf0`, `0x541110`, `0x574670` |
| projectiles alive | no plain counter decoded (UNKNOWN). Count hook pairs: `0x65ae10` spawns `amount` units, `0x7048a0` (`X_Projectile::~X_Projectile`) frees one. Hint only: `[[[GM+0x8f]+8]+4]` = entries in the factory pool (`0x420e30` stores the impl at `GM+0x8f`, profile string `GM_Projectile_impl`; `0x65aa60` loops that count) | M / UNKNOWN |

## 7. Hook "a bullet hit X for N"

Three nested points, pick by need:
1. `0x7097d0` `X_Projectile::onCharacterHit` — one event per bullet that reached a character, before the bone multiplier.
   Also the only place that fires the `[CharacterHit]` script. Best for "which projectile, which victim".
2. `0x695eb0` `X_Character::causeDamageFromHit` — N after the bone multiplier (head 5x, arms 0.75, legs 0.5).
   `ecx` = victim character (INFERENCE from `0x695b10` inheriting the same register), stack = damage, mode, attacker, bone name.
   Shared with explosions (`0x6976f0`), so filter by caller or by mode.
3. `0x695b10` `X_Character::causeDamage` — final intended N plus mode (0 normal, 1 shotgun, 2 burning, 4 blast, 5);
   covers bullets, blasts, fire, melee and fall damage (`0x68c960`). `ecx` = victim; arg0 = `float` damage; arg1 = mode.
   The value actually written is one level deeper (`0x695fb0` → `0x6d71a0`), after clamping and the immortal checks.

## 8. KF2 container (needed to bound the reload clips)

- Parser is in the corpus: top level `0x55bc40` (`kf2_read_file_chunks`) over the global `R_MemoryFile 0x8af930`, with a
  dispatch on chunk id and one reader per id: `0x54bbe0` (0x10001), `0x577940` (0x10003), `0x577ab0` (0x10004),
  `0x567800` (0x10005), `0x566fb0` (0x1000e), `0x577d80` (0x10016), `0x577f20` (0x10017), `0x575cd0` (0x10012),
  `0x577160` (0x1000f); script loaders `0x550350` (geometry) and `0x550b80` (animation, resolves `.kf2` paths).
- Header, measured on real files (extracted read-only from `~/Games/maxpayne/x_data.ras` with `tools/ras.py`):
  every chunk is **13 bytes** = `u8 0x0c | u32 id (LE) | u32 flags | u32 size`, `size` includes the 13. Walking on
  `offset += size` lands exactly on the next `0x0c` and ends at EOF for `313_beretta_shoot.kf2` (440 B, 2 blocks),
  `313_beretta_default.kf2` (236 B, 2), `163_ReloadBeretta.kf2` (32618 B, 28), `163_ShootBeretta.kf2` (9464 B, 28).
  In the animation files each `0x10012` block = one animated object: payload starts `0d 14 <len> <name>` (`Pelvis`,
  `clip_beretta`, `Camera01`, ...), keys in the nested `0x10013` stream tagged `0x11/0x1a/0x09/0x0e`.
  FACT: boundaries, sizes, block counts, the name at the head of a block.
- The keyframe tag encoding (`0x11`/`0x1a`/`0x09`/`0x14`/`0x0e`/`0x0d`), and therefore a standalone frame count, is
  UNKNOWN: the corpus reader bodies are thin and no decoded duration field is visible. Read the length at runtime with
  `mesh->vfn+0x78(animId)` (§5), or take the frame numbers out of the animation scripts (`179_max_payne.txt`,
  `161_default_skeleton.txt`), which is what the messages use.

## 9. Not proven here
- Caller and countdown of `0x766950` / `0x7669a0` (`state+5`, `state+9..0xb`): the evaluate slot of vtable `0x7da0a8` is an EH stub here.
- State→`CHARANIM_SHOOT*` selection (INFERENCE via `X_CharacterAnimator::playRequest 0x745130` / `0x7435b0`).
- Bodies of `0x758740` (muzzle flash) and `0x6e3690`: absent from the corpus snapshot.
- Field meaning of `ws+0x231/0x23d/0x249`; whether `ws+0x204/0x205` select the alternate spawn path.
- A live-projectile counter: none decoded; use the hook pair in §6.
