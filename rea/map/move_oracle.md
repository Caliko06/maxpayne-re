# Per-frame movement oracle (web vs native) — Max Payne 1.05, X_Character

Sources: `rea/c/MaxPayne.exe/*.c` (Ghidra corpus), `rea/map/names/MaxPayne.exe.tsv` (names + evidence text),
`rea/map/{player,look_move,gameplay,ai}.md`, `git show mp-systable:host/gamestate.c`. Nothing was run: every row is static
unless it says "runtime". `C` = X_Character* = `[[GM+0x1080]+8]`; `GM` = `[[[0x8a6338]+0xc8]+0x80]`.
FACT = read in a body/table. INFERENCE = derived shape. UNKNOWN = not found.

## 1. The one thing that was wrong in the maps
`C+0x2f9` is the **embedded** `X_CharacterInput` (vtable 0x7d9268), and its controller pointer is `C+0x2fe` (bridge+5,
`0x751da0` = `mov eax,[ecx+5]`, names.tsv:4508). `look_move.md` calls it "Bridge" but does not give the id fields; the
dump `0x743d60` names them (names.tsv:4240): **+0x6e = "Upper body animation", +0x6a = "Lower body animation"**. In
`X_CharacterInput::update` `0x7429b0` the raw ids come from controller vfn +0x90 (upper) and +0x94 (lower)
(names.tsv:4238) and are stored next to them (FACT, `007429b0.c:161-162,178-179`):
`+0x66 = upper state id`, `+0x6e = upper anim id`, `+0x62 = lower state id`, `+0x6a = lower anim id`.
`0x743ce0(id)` adds the wounded (0x8c) / two-handed (0x46) / weapon-id group offset and runs the `[Revert]` resolver
`0x53e770` (walk -> run, `00743ce0.c`), so **+0x6a/+0x6e are the ids that reach the animation**, +0x62/+0x66 are the
evaluator ids (same number space: INFERENCE, names.tsv:4239).

## 2. Player: fields that define the movement state each frame
| field | offset from C | type | writer (function) | read from C (guest ptr) | evidence | conf |
|---|---|---|---|---|---|---|
| world position | `+0x72` | 3 x f32 | `X_Character::updateLowerBody` 0x68c960 collision move (getter 0x642d50, moveTo 0x647660); teleport 0x6973a0 | `memcpy(p, C+0x72, 12)` | gameplay.md:362-377; player.md "World position"; runtime (player.md 12-15) | HIGH |
| velocity (disp/dt) | `+0x41e` | 3 x f32 | 0x68c960 step 6 (velocity := displacement/dt, `0068c960.c:364`; getter 0x694540); zeroed when not free at `0068c960.c:343` | `memcpy(v, C+0x41e, 12)` | player.md "Velocity"; gameplay.md:362 | HIGH |
| gravity/free-fall velocity | `+0x411` | 3 x f32 | `X_FreeFall::step` 0x72c540 (called from 0x68c960 step 4) | `memcpy(C+0x411,12)`; getter 0x694550 | gameplay.md:365; player.md "Velocity" | HIGH |
| animation velocity | `+0x42a` | 3 x f32 | 0x68c960:91 (`+= animDelta` 0x744e20) | getter 0x694560 | `0068c960.c:91` | MEDIUM |
| **lower/body state id** (movement, jump, fall, dodge, damage) | **`+0x35b`** (= 0x2f9+0x62) | i32 | `X_CharacterInput::update` 0x7429b0 from controller vfn +0x94 | `(int32_t)rd(C+0x35b)` | `007429b0.c:178`, dump `0x743d60` (names.tsv:4240), names.tsv:4238 | HIGH (store). id values: see §4 |
| **lower body animation id** (post-Revert) | **`+0x363`** (= 0x2f9+0x6a) | i32 | 0x7429b0 via 0x743ce0 + 0x53e770 | `(int32_t)rd(C+0x363)` | `007429b0.c:179`, dump labels "Lower body animation" | HIGH |
| upper/aim state id, upper anim id | `+0x35f`, `+0x367` | i32 | 0x7429b0 from controller vfn +0x90 | `rd()` | `007429b0.c:161-162` | HIGH (store) |
| current anim id at the channel (2nd source for `+0x363`) | `A=u32[C+0x3a0]`; chan0=`u32[A+0x25]`; `chan0+0x5a` | i32 | `X_AnimChannel::playOnce` 0x74a310 / `playLoop` 0x74a100 / `playCrossfade` 0x74a180 | 2 hops, see §3 | gameplay.md:431 ("[this+0x25][0] lower"); names.tsv:4245; `0074a310.c:13-27` | MEDIUM (chain not run) |
| **current animation time** (seconds) | `chan0+0x2f` (timer at `chan0+0x2e`) | f32 | `AnimationTimer::advance` 0x77b1a0 (scaled dt 0x5e53d0), `setStartTime` 0x74b150 | `ldf(chan0+0x2f)`; length `f32[chan0+0x33]`, step `+0x37` | `0074b150.c:8`, `0077b1a0.c:12-20`, names.tsv:4295 | MEDIUM |
| **airborne** | `+0x26d` (ST+0x3f) | u8 | 0x68c960:450 `setIsAirborne(= C+0x469)`; cleared 0x687f20:528 | `c->mem[C+0x26d]` | `0068c960.c:448-450`, names.tsv:3688-3689 | HIGH |
| falling (accelerating down, not free) | `+0x26e` (ST+0x40) | u8 | 0x68c960:453 `setIsFalling` | byte | `0068c960.c:431-453`, names.tsv:3690 | HIGH |
| **contact / ground** | `+0x469` | u8: **1 = no blocking contact (free)**, 0 = in contact | 0x68c960:370 (`cVar3 = 1 - blocked`) | `c->mem[C+0x469]`; ground = `!free && !airborne` | `0068c960.c:273,370,448`; gameplay.md:377 | HIGH (flag), INFERENCE (ground = !free) |
| ground-snap lockout | `+0x228` | f32 s | 0x695840 (jump 0x699820 sets 0.5) | `ldf(C+0x228)`; snap only when `<= 0` | `0068c960.c:278,316`; gameplay.md:376 | HIGH |
| **body yaw (rendered)** | `atan2(f32[C+0x66], f32[C+0x6e])` (row 2 of the 3x4 at `+0x4e`) | f32 rad | matrix rebuilt by 0x6911e0 (vfn slot 3 of `C+0x1e`) | `atan2f(ldf(C+0x66), ldf(C+0x6e))` | player.md "World transform" + runtime dot 1.000; `006911e0.c:49-50` (yaw blend `C+0x7a4*w + (1-w)*C+0x7b4`, w `C+0x7c4`, x 0.017453292) | HIGH (runtime), MEDIUM (which fields feed it) |
| heading (logical aim/shoot-dodge yaw, 0..2pi) | `+0x295` (ST+0x67) | f32 rad | 0x687f20: `+= bridge yaw delta` then wrap 6.2831855 (0x7d5ce0) via setter 0x6d7110 | `ldf(C+0x295)` | `00687f20.c:281-292`, look_move step 9a, names.tsv:3718 | HIGH |
| aim pitch | `u32[C+0x498]+0x20` | f32 rad | accumulator 0x725b30, clamp +-1.5533431 | already gamestate word 344 | player.md "Aim pitch"; host/gamestate.c:344 | HIGH |
| **dodge: isShootDodging** | `+0x274` (ST+0x46) | u8 | **writer NOT in corpus (UNKNOWN)**; getter 0x6d70c0 | `c->mem[C+0x274]` | names.tsv:3716; used by 0x68c960 / 0x68df90 | HIGH (read) / UNKNOWN (writer) |
| dodge: shoot-dodge started | `+0x29e` (ST+0x70) | u8 | `DodgeSpecific::onEnter` 0x764840 (0x764895); cleared by GameMode 0x44ce20 | byte | names.tsv:3723 | HIGH |
| dodge: dodge requested | `+0x29f` (ST+0x71) | u8 | 0x764180 sets 1 (0x7644bb), 0x44cdc0 clears | byte | names.tsv:3725 | HIGH |
| dodge: allowed / canCrossfade / canEnd | `+0x271` / `+0x272` / `+0x273` (ST+0x43/44/45) | u8 | setters 0x6d7070 (and siblings) | byte | names.tsv:3712-3715 | MEDIUM |
| dodge bullet-time ok (meter > threshold) | `+0x29d` (ST+0x6f) | u8 | GameMode 0x44cde2-0x44ce0b every frame | byte | names.tsv:3721 | HIGH |
| dodge direction sector | `floor(f32[C+0x295] * 4/pi)`, 8 sectors (0x7d5cf0) | i32 | `blendShootDodgeDirection` 0x68c850 | computed | names.tsv:2838; gameplay.md:207 | HIGH |
| room (positions are room-local) | `+0x7e` | u32 | moveTo/parent; gamestate word 336 | `rd(C+0x7e)` | host/gamestate.c:336; player.md | HIGH |

## 3. Exact read chain for the animation clock (only 2-hop part)
```
A     = u32[C+0x3a0]        // X_CharacterAnimator*, getter 0x691740 = mov eax,[ecx+0x3a0]
chan0 = u32[A+0x25]         // channel array base; index 0 = lower body, 1 = upper (gameplay.md:431)
id    = i32[chan0+0x5a]     // KF animation id being posed (0074a310.c:13)
t     = f32[chan0+0x2f]     // AnimationTimer time, seconds (timer begins at chan0+0x2e; +1 = time, 0077b1a0.c:12)
len   = f32[chan0+0x33]     // length;  step = f32[chan0+0x37];  loop byte = u8[chan0+0x3f]
fps   = (**(int**)(chan0+0x22))->vfn[0x7c]   // KF slot 31 getFps; frame = t*fps  (slot 0xec/4 = 59 = frameDuration)
```
Timer advances with the **scaled** dt (`0x5e53d0`), so web and native must be compared on game time, not wall clock.
INFERENCE: `chan0+0x5a` equals `C+0x363` in steady state (both come from `0x743ce0`; a crossfade can leave the channel one
frame behind). Not verified at runtime.

## 4. State ids (lower body channel) — `State_Movement::evaluate` 0x765eb0, vtable 0x7da024
Order matters: later writes win (FACT, names.tsv:4728 = Ghidra + Hopper).
`0xa/0xb` aim axis vs `state+0x15` | `0xd` crouch (input slot 8) | run axis (slot 0): `>0.5 -> 2`, `(0,0.5] -> 1`, `<0 -> 6`;
strafe (slot 1): `>0` remaps `2->9, 6->7, else 8`; `<0` remaps `2->3, 6->5, else 4`.
Other producers on the same bus: `0xc` StandIdle 0x766400; `0x26..0x29` Jump 0x7651c0; `0x2a` fall / `0x2b` land FallLand
0x7661a0; `0x2c` Use; `0x2d/0x2e` WeaponSpecific; `0x2f/0x30` GetDamage; `0x12..0x19` dodge, `0x1a..0x21` shoot-dodge,
`0x22..0x25` get-up (DodgeSpecific 0x764180); `0x31..0x3c` death by ST+0xb7 (0x764b40). gameplay.md:217 lists 1..9 differently
from names.tsv:4728: **trust names.tsv:4728** (two sources), diff = the strafe remap.

## 5. Enemy (same struct, different controller)
| what | path | writer / evidence | conf |
|---|---|---|---|
| enemy list | `GM+0x24c` begin, `+0x250` end, 4-byte AP* (vector at GM+0x248) | ai.md:17; host/gamestate.c `enemies()` | HIGH |
| AP -> X_Character | `C = u32(AP+0x533)`; inverse `AP = u32(C+0x2fe)` | ai.md:18; ai_behaviour.md:221 | HIGH |
| all §2 rows above | identical offsets from the enemy `C` | same code path (X_Character::update, same bridge at C+0x2f9) | HIGH for pos/vel/health/flags |
| enemy lower/upper ids at `+0x35b/+0x363` | same | bridge is per-character, filled by 0x7429b0 from *its* controller | INFERENCE: AI handle vtable 0x7cfd40 slots 36/37 = 0x4ff280 / 0x4ff130 (unnamed) — verify at runtime against the channel id |
| AI active | `+0x2dc` (ST+0xae) | host/gamestate.c `enemies()` (0x5001f0) | HIGH |
| dead / health | `+0x235`, `+0x2b3` | 0x6963f0; host/gamestate.c | HIGH |
| instance name | `id=u32(C+0x198)`, table `u32[0x8b84cd]`, entry 13 B: `+0xc` valid, `+4` char* | 0x683f79, 0x459590; host/gamestate.c | HIGH |
| collision radius | `+0x14a` | 0x658b80; host/gamestate.c word 343 | HIGH |

## 6. host/gamestate.c: the words to add (continue after word 366)
```c
int32_t st_lower;      // 367 lower body state id  (C+0x35b)  0x7429b0
int32_t an_lower;      // 368 lower body anim id   (C+0x363)  0x7429b0 / 0x743ce0
float   an_time;       // 369 anim time s  f32[[u32[A+0x25]]+0x2f]  (0 if any hop is 0)
float   yaw;           // 370 atan2f(rot[6], rot[8]) = atan2f(f32[C+0x66], f32[C+0x6e]); no new read (rot is already words 9..17)
uint32_t mv_flags;     // 371 bits: +1 airborne (C+0x26d), +2 falling (C+0x26e), +4 free (C+0x469),
                       //      +8 shootdodge (C+0x274), +16 dodge started (C+0x29e), +32 dodge requested (C+0x29f)
float   snap_lock;     // 372 f32[C+0x228]
```
Add `if (ok(c,ch+0x3a0,4) && rd(c,ch+0x3a0))` guard around the animator hops; guard `C+0x2f9+0x62` with `ok(c,ch+0x35b,4)`.
`free` (word 371 bit 4) is the same value as airborne; keep both so a mismatch shows up in the diff.

## 7. Compare protocol (what "same movement" means here)
Per frame, same input script on both sides (`@d/@u/@k/@m`, mp-systable `host/jump.c`), log in this order and stop at the
first divergence: `pos` -> `vel` -> `st_lower` -> `an_lower` -> `an_time` -> `mv_flags` -> `yaw`. One frame of slack is
expected on `st_lower/an_lower` (they update inside the substep loop, 0x687f20 caps substeps at 0.11 s, 0x7d5920).

## 8. Not verified / open
- No runtime read of `+0x35b`, `+0x363`, `mv_flags` or the animator chain in this pass (static only). UNKNOWN values.
- Writer of `isShootDodging` (`C+0x274`) is not in the corpus; the getter is 0x6d70c0. Set it from the state id instead
  (`0x12..0x25`) if the runtime shows it stale.
- `look_move.md` files the yaw blend at `C+0x776/0x786/0x796`; `006911e0.c` uses `param_1 = C+0x1e`, so the real offsets are
  `C+0x7a4 / C+0x7b4 / C+0x7c4` (INFERENCE, off-by-0x1e in that map). Nothing was changed there.
- Enemy controller slots 0x4ff280/0x4ff130 are unnamed; the "same ids as the player" row is INFERENCE.
