# Bullet damage path: weapon fire -> X_Character health

MaxPayne.exe 1.05 (md5 cda1650af370e50f12e500831a834783), VAs as in the Ghidra corpus. Sources per claim: Ghidra
body + capstone on the same bytes (C = the two agree), or one source only. Existing notes reused, not repeated:
`gameplay.md` (weapons), `hits_deaths.md` (death chain), `player.md` (health getters), `physics.md`.

## 0. X_Character offsets this path touches
| What | Offset | Setter / reader | Conf |
|---|---|---|---|
| health (float) | C+0x2b3 (= ST+0x85) | 0x6d71a0 / 0x6d71b0 | C: `0x69625b` `call 0x694430; 0x6d71a0`; player.md |
| max health | computed 0x691bd0 = base int [[C+0x718]+0x5d] * difficulty mult | 0x691bd0 | C: `0x696006 call 0x691bd0` |
| **immortal** (u8) | **C+0x454** | C_SetImmortal 0x69a800: `0x69a80c mov [esi+0x454],al` | C: 0x69a80c asm + Ghidra |
| dodge immortal (u8) | C+0x455 | C_SetDodgeImmortal 0x69a820: `0x69a82c mov [esi+0x455],al` | C |
| **invulnerable** (u8) | **C+0x456** | C_SetInvulnerable 0x69a840: `0x69a84c mov [esi+0x456],al` | C |
| projectile collisions (u8) | C+0x22d | msg 0x69b9b0; getter 0x69cb80 | C: `0x69b9bc`, `0x69cb80 mov al,[ecx+0x22d]` |
| DO collisions (u8) | C+0x22c | msg 0x69b9d0; getter 0x69cb70 | C |
| dead (u8) | C+0x235 (= ST+7) | 0x6d6ec0 / 0x6d6eb0 | C: `0x6960e5 call 0x6d6eb0` |
| wounded (u8) | ST+9 = C+0x237 | 0x7496d0 | C: `0x696136 call 0x7496d0` |
| handle, for the self-hit test | C+0x198 | read via 0x6958b0 (`add ecx,0x198; jmp 0x421a50`) | C: `0x6958b0` asm |
| DamageFlash amount threshold (float) | C+0x46c (+0x474/+0x478/+0x47c/+0x484/+0x488 = colours+max) | 0x695b10 | C: `0x695b10` body |
| "revived by this write" flag (u8) | C+0x70c | 0x695fb0 | C: `0x696248 mov [esi+0x70c],1` |

## 1. From trigger pull to a flying projectile
Unchanged from `gameplay.md`: WS_AnimateShooting 0x758400 -> SHOOT frame 0 message P_CreateProjectile -> 0x758790 ->
X_ProjectileImplementation::createProjectile 0x65ae10 -> X_Projectile::ctor 0x7065d0. Damage never comes from the
weapon: it comes from the projectile definition (pdef) carried by the flying object.

## 2. Sweep and the gates before a character can be damaged
X_Projectile::update 0x70a0d0 sweeps this frame's motion segment, collects hits (BSP 0x5c0ba0 family) and hands each
hit record to a per-class callback: character 0x7097d0, dynamic object 0x709740, trigger 0x709540.
Character branch, in byte order (each `je/jne 0x70aef4` = this hit is discarded; all FACT, asm + Ghidra):

1. `0x70a93d cmp eax,esi` — owner test: projectile owner handle (proj iface +0x1fd, getter 0x707e90) vs
   `*(int*)(C+0x198)` (0x6958b0). Equal -> skip: a character never shoots itself.
2. `0x70a96a call 0x574670` = `[pdef+0x74]` **DamagesCharacter**; 0 must be 1 or no damage at all.
3. `0x70a979 call 0x69cb80` = **C+0x22d projectile collisions**; 0 -> skip. `C_EnableProjectileCollisions(false)`
   (0x69b9b0) makes that character bullet-proof while it stays visible.
4. `0x70a98f call 0x541110` = `[pdef+0x51]` Debris; debris do not damage characters.
5. `0x70a99f` `proj+0x265 != 0` -> skip (projectile-side "already used/canceled" byte).
6. Hit radius `= vfn+0x20 of the body interface at C+0x1e` (`0x70a9b5 call [eax+0x20]`); for a corpse a second,
   larger radius is built through 0x691710 / 0x538b90 / 0x658b50 / 0x697db0 (`0x70a9c4..0x70aa0a`).
7. `0x70ab16 call 0x5ba000` segment-vs-sphere; only a hit is queued.

Note: there is **no** health, immortal, invulnerable, AI-state or "scripted" test in the sweep; those live further
down (§5), and health is not even read here.

## 3. Character callback 0x7097d0: what the damage number is
`0x70a015 mov eax,[ebp+0xc]` ... `0x70a018 mov ecx,[esi+0x1f1]` (= pdef) ... `0x70a025 call 0x40edf0`, whose body is
`0x40edf0 fld dword ptr [ecx+0x3c]; ret` = **pdef.Damage**. Then `0x70a030 call 0x695eb0` with
(damage = pdef.Damage, mode, attacker, bone-string object). FACT (asm + Ghidra body).

- mode = `[pdef+0x7d]` DeathCause read through 0x60eea0 (`return *(u32*)(ecx+0x7d)`), mapped 1->1, 2->2, 3->5,
  anything else 0 (`0x709fb0-0x70a015`). Only affects which death animation plays (hits_deaths.md §4).
- **No difficulty term and no body-part multiplier reach the number.** The "Target group" scan inside 0x695eb0
  compares `_strcmpi(boneName, group bone)` against `param_4`, and the projectile path passes the static **empty**
  string object `0x8b5e30` (`0x70a01e push 0x8b5e30`); the explosion path passes `0x8b4908` likewise
  (`0x697c54`, `0x697c6c`). So the multiplier stays 1.0 and HP_loss = pdef.Damage exactly. FACT for the call sites
  (asm), INFERENCE for "empty string" (0x8b5e30 is a bss rs-string built by the guard `std::rs::_Tidy` in
  0x70a0d0's prologue). *Correction to `hits_deaths.md` §2: head 5 / arms 0.75 / legs 0.5 are in the data and the
  code is there, but v1.05 never feeds a bone name into it from a bullet.*
- Data (x_data.ras, per gameplay.md): Beretta/Ingram/shotgun pellet 5, MP5/Desert Eagle 10, sniper 70.
- After the damage: `0x70a035 call 0x541130` = pdef+0x66 CollisionExplosion, `0x70a04a call 0x5584b0` = pdef+0x67
  DelayedExplosion. Nonzero -> projectile survives the hit; otherwise the projectile is finished
  (`0x70a2a2`-region: `*(u8*)(proj+0x2a2) = 1`). FACT (asm + body).

## 4. causeDamageFromHit 0x695eb0 and causeDamage 0x695b10
- 0x695eb0: compute multiplier (always 1.0 in practice, §3), then `0x695b10(mult * damage, mode)`; then, if an
  attacker pointer was given and a controller exists, `controller->vfn+0x7c(attacker)` so the AI learns who shot it.
- 0x695b10: `newHealth = health(0x6d71b0) - damage`, then setHealth 0x695fb0. The only branch before that is the
  **red flash**, built when: no controller or `vfn+0x8c` (AI-controlled) is false, `health > 0`,
  `C+0x456 == 0` (not invulnerable), and `damage >= C+0x46c` (0x695b10 top). Visual only.
  Returns the "died from this hit" flag of setHealth; the caller 0x695b10 then calls 0x687b00 (kill rewards).
  **No gate here cancels damage** except that the flash needs a live, vulnerable target.

## 5. setHealth 0x695fb0 — every gate, in byte order (asm 0x695fb0-0x69627e, FACT)
```
0x695fc3 health_now                     = ST health
0x695fd5/0x695fef wounded_threshold      = maxHealth(0x691bd0) * (0x5310b0(params)/0x5310a0(params))
0x696015 bl = C+0x455 (dodgeImmortal)   if (arg4 != 0 && newHealth == 0.0) bl = 0     // arg4 = "ignore dodge i-frames"
0x69603d if (!dead) 0x696290(mode)        -> picks ST+0xb7 death type
0x696065 if (newHealth < health_now) ST+5 = 1 (getting damage, 0x6d6ea0)
0x696071 if (C+0x456) goto end           <-- INVULNERABLE: nothing written, no death, no clamp
0x696081 if (newHealth > maxHealth) newHealth = maxHealth
0x6960a0 if (newHealth < 0) newHealth = 0
0x6960b5 if (C+0x454 == 0 && bl == 0) goto 0x696178           // mortal: normal path
0x6960c7 if (newHealth >= 0.1f) goto 0x696178                 // still above the i-frame floor
0x6960e5 if (dead) goto 0x696178                              // a corpse is not clamped
0x6960f2 newHealth = 0.1f                                     <-- IMMORTAL FLOOR (0x3dcccccd)
0x696178 if (newHealth == 0 && !dead) { killed = 1; if AI: controller->vfn+0x84(0); die 0x6963f0 }
0x696166 wounded transition ST+9 via 0x7496d0; if AI and health dropped, ST+6 = 1 (gethit anim, 0x6d6e80)
0x69623d if (dead && newHealth > 0) revive: 0x6972a0(0), C+0x70c = 1, 0x6876d0, 0x687710
0x69625b ST health = newHealth
```
So: **invulnerable = health is never written at all** (stays whatever it was, e.g. 20). **immortal = health can
never go below 0.1f** and the victim never dies; it still takes the flinch, the wounded transition and the damage
animation. dodgeImmortal (C+0x455, set by `C_SetDodgeImmortal` / the shoot-dodge animation) behaves like immortal
for one call, but is dropped when the caller passes arg4 != 0 with newHealth == 0 (that is how C_Kill kills a
shoot-dodging Max).

## 6. Dynamic objects (curtains, furniture)
- Sweep callback 0x709740: `0x657f00` runs the DO's hit reaction (animation / impact), and if `[pdef+0x51]` Debris
  is 0 **and** `[pdef+0x74]` DamagesCharacter is 1 it calls `0x6f4850(pdef.Damage)`.
- 0x6f4850 -> 0x6f4900: **DO hitpoints at DO+0x4a9 (float)**, subtract, clamp at 0, returns "just died". Then the DO
  is sent message `DO_BulletCollides` (static string object 0x8b5c20, built in 0x6f1d30 from "DO_BulletCollides"
  at 0x89c89c) when the hit kills it, or the message at 0x8b5c00 otherwise (name not located, UNKNOWN). That is the
  level-script hook the `.ldb` files attach to (`::Sanctum Corridor::door02.DO` etc.).
- The DO hit does **not** end the projectile by itself (0x709740 writes no projectile state), so a curtain in the
  line of fire is not a shield; it is a separate damageable object. INFERENCE (0x657f00 body not read to the end).
- Character-vs-DO *body* contacts are a different path (0x694fe0, gated by C+0x22c) and never damage health.

## 7. Explosions and falls (for completeness)
- Explosion entry X_Character::hit 0x6976f0 -> 0x695eb0 twice (0x697c5f with mode 4 / blast, 0x697c78 with mode 0),
  bone string 0x8b4908 (empty) -> multiplier 1.0, damage = pdef.Damage of the explosion projectile.
- Fall damage 0x68c960 tail -> causeDamage(..., 3); mode 3 = falling death.

## 8. Reading of the two bot symptoms (INFERENCE, from the addresses above)
- Enemy health stuck at 20: 20 is HEALTH_CLASS_2 base (data), i.e. full health and *nothing was ever written*.
  Immortal would show 0.1, not 20; both immortal and a dead body differ from 20. So the block is upstream of
  setHealth: in order of likelihood — C+0x22d `C_EnableProjectileCollisions(false)` on that character,
  `[pdef+0x74]` DamagesCharacter = 0 for the projectile in use, the projectile never reached the capsule
  (0x5ba000), or the sweep discarded the hit as self/owner. All four are testable at runtime with one dump of the
  target's C+0x22c/C+0x22d/C+0x454/C+0x456 and of pdef+0x74.
- Max stuck at 60: 60 is the player base health (PlayerHealthMultiplier 1.0), so no damage arrived at the player
  either. Level 1-6 sets `player->c_setinvulnerable(true)` around scripted cameras (1_Part1_Level6.ldb @0x65c383,
  @0x65d741, @0x695070) and clears it in the camera-exit blocks (@0x67788d, @0x6781c8, @0x69520f). If the exit
  block does not fire, the player stays invulnerable forever (§5 gate 0x696071). Same for the `c_setimmortal` on
  `::teleport::lupino` (@0x6640f2, cleared @0x67d32b).

## UNKNOWN
- The message name of the static string object 0x8b5c00 (DO hit that does not kill it); 0x6f4850 has no initialiser
  in the dumped corpus.
- Whether 0x8b5e30 / 0x8b4908 are guaranteed empty at the moment of a hit (bss strings; the guard builds them, no
  writer found in the corpus). If something ever writes a bone name there, the Target multipliers activate again.
- Why exactly the sweep never queued ::teleport::End_B01: not traced at runtime (no game run in this slice).
- 0x693860 / 0x6938b0 (the vfn+0x20 slot-8 radius), 0x691710 / 0x538b90 / 0x658b50 (corpse radius), 0x697db0.
- Body of 0x657f00 after its prologue; the exact sort comparator 0x64f410 / 0x680e60 / 0x650140.
- Whether `C+0x22d` defaults to 1 for every spawned character (0x6838f0 sets it, 1): only that reset path read.
