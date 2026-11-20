# Player, game mode and camera (MaxPayne.exe 1.05, rebased VAs) — for the ghost driver and host/gamestate.c

Sources: asm (capstone over build/img/MaxPayne.exe), Ghidra rea/c/MaxPayne.exe, the render slice's RE (mode switch, level
name, pending action: agrees), and a native runtime read (host/gamestate.c, RN_GHOST=60). Packed structs: unaligned is normal.

## Runtime confirmation (native, 2026-11-02, New Game)
- Chain live: theApp 0x8a6338 -> [+0xc8] ModeSwitch (110f6a50) -> [+0x80] GameMode (119e7c40) -> [+0x1080] (11d88328) -> [+8] X_Character (11dc9150).
- Menu: [ms+0x19] = another mode (menu); after the load [ms+0x19] == [ms+0x80] (game) -> gamestate mode 3.
- Level key [gm+0xcc] std::string = "startup_level" (the levels.txt section); [gm+0xf8]+0x10 = "Startup" (its LevelName).
- Player: hp 60.0, base 60, weapon id 15, clip 5, reserve 20, painkillers 0, bullet-time meter 1.00, pos (28.50, 219.49, 1.50),
  rotation row 2 = (1, 0, 0).
- Web, part0_level1 (ghost calibrate): holding W 0.9 s moved the player 3.97 units along rotation row 2 (char+0x66..+0x6e),
  dot 1.000: forward = local +Z = row 2; yaw = atan2(row2.x, row2.z). Row 1 = up (0,1,0).
- Mouse look: +200 counts (X) turned yaw by +1.2548 rad = 6.274e-3 rad/count. Predicted from the RE: slider 0.5 (registry
  "Mouse Sensitivity X" 5000) -> s = 0.5*1.4+0.1 = 0.8; 0.8 x 0.0025 (0x5195c0) x 180 x pi/180 (0x4cecd0) = 6.283e-3. Agrees.
- The getters 0x694580 and 0x6945a0 are not lifted entries (reached only by jmp from the HUD wrappers): call 0x421f00/0x421f10/
  0x421f20 with ECX = a dword holding X_Character* (the player wrapper +8), as the HUD 0x4afcf0 does.


All offsets are bytes, packed structs (unaligned is normal), floats are IEEE32, ints are 32-bit.
`C` = X_Character* = `[[GM+0x1080]+8]`, `GM` = `[[[0x8a6338]+0xc8]+0x80]` (theApp -> ModeSwitch -> GameMode, see facts.md).
`ST` = character state object = `C+0x22e` (0x694430 `lea eax,[ecx+0x22e]`; the class with health at +0x85).
`WS` = weapon set = `[C+0x398]` (0x6916e0).
Disassembler used: x86d.py (capstone over build/img/MaxPayne.exe). Ghidra decompiles: rea/c/MaxPayne.exe/<addr>.c.

## Field table

| name | how to reach (from C or global) | type | proof (addresses) | second source | confidence |
|---|---|---|---|---|---|
| Player X_Character* | `[[GM+0x1080]+8]`; has-player test 0x451e50 | ptr | facts.md (0x451e50/0x451e60) | overlay 0x422d60 -> 0x421ca0 uses 0x451e50/0x451e60 | high |
| World position | `C+0x72` (x), `+0x76` (y), `+0x7a` (z) = translation of the 3x4 transform at `C+0x4e` | 3 x float | 0x642d50 = `lea eax,[ecx+0x54]` (getPosition on sub-object `C+0x1e`, 85 callers; 0x54 = 0x30 + 9*4). 0x6973a0: `lea edi,[esi+0x1e]; mov ecx,edi; call 0x642d50` then `pos - halfextent` (box min = AABB test with params +0xc..+0x14) | crosshair 0x68ebd0: `lea esi,[edi+0x1e]; call 0x74e8e0` (returns `ecx+0x30`) then `out = v*M[0..8] + M[9..11]` (asm 0x68f112-0x68f11c, C lines "local_44 = ... + local_20[9]"); camera 0x504130 slot 0 of the camera-target iface copies 12 floats of the same matrix (`C+0x1e+0x30`) | high |
| World transform (3x4) | `C+0x4e` .. `C+0x7d`: 12 floats; rows = 3 basis vectors `[0..2] [3..5] [6..8]` (row-vector convention, `world = local*R + t`), `[9..11]` = position | 12 x float | 0x74e8e0 `lea eax,[ecx+0x30]` on `C+0x1e`; camera target iface vtable at `C+0x17c` slot 0 = thunk 0x693070 (`add ecx,-0x15e; jmp 0x74e8e0`) | crosshair math 0x68f112.. uses `p[0]*x+p[3]*y+p[6]*z`, `+p[9]` | high |
| Orientation / facing (yaw) | derive from the rotation rows at `C+0x4e`: row0 = local X (`+0x4e,+0x52,+0x56`), row1 = local Y (`+0x5a..`), row2 = local Z (`+0x66,+0x6a,+0x6e`). Yaw = atan2(row2.x, row2.z) if forward = local +Z. | 9 x float | Y is the vertical axis: PlayerInput 0x4cecd0 builds yaw as acos of the XZ dot product and pitch as asin of the middle component (`[param+0x32]`), movement vec (0x4cecd0 end) = (-strafe, 0, forward) with y = 0 | none for "forward = +Z" (consumer of PlayerInput+0x21 not located) | rotation rows: high. Which axis is forward: MEDIUM (+Z). Confirm at run time by moving forward and comparing the position delta with the rows (one sample) |
| Aim pitch (candidate) | `ST+0xb` = `C+0x239` (float, radians) | float | 0x6d6ef0 `fld [ecx+0xb]`; PlayerInput 0x4cecd0: `pitch_delta = asin(y_component) - 0x6d6ef0()` (lines "fVar9 = FUN_006d6ef0(); fVar9 = asin - fVar9") | none | MEDIUM (name inferred from the subtraction) |
| Aim/facing direction vec3 (candidate) | `ST+0x27` = `C+0x255` (3 floats) | vec3 | 0x4cecd0 asm 0x4ced10-0x4ced15: `call 0x6d6f10 (ST+0xf); add eax,0x18` -> vec compared (XZ dot) with PlayerInput+0x2e target dir to produce the yaw delta | none | LOW-MEDIUM (role inferred) |
| Velocity | `C+0x41e` (vec3); gravity vel `C+0x411` (vec3, = `C+0x404+0xd`); anim vel `C+0x42a` (vec3) | 3 x float | getters 0x694540 `lea eax,[ecx+0x41e]`; 0x694550 -> 0x742840 `lea eax,[ecx+0xd]` on `C+0x404`; 0x694560 `lea eax,[ecx+0x42a]` | overlay 0x422d60 prints them (`vel %4.2f %4.2f %4.2f` + length) | high |
| Health (raw) | `C+0x2b3` (float) = `ST+0x85` | float | 0x694470: `call 0x694430; mov ecx,eax; jmp 0x6d71b0` (`fld [ecx+0x85]`); setter 0x6d71a0 | overlay 0x422d60 `Health %2.2f` via wrapper 0x421e80; setHealth 0x695fb0 ends with `call 0x6d71a0`; heal 0x695a80 reads/writes it | high |
| Health fraction | call `0x694480` (thiscall C) = health / maxHealth, clamped to <= 1.0 (const 0x7cbdd0 = 1.0f) | float | asm 0x694480-0x6944bc (`fdivr` by 0x691bd0, `fcomp [0x7cbdd0]`) | HUD update 0x4aee20 uses wrapper 0x421e90 for the bar; 0x453b40 calls 0x694480 | high |
| Max health | call `0x691bd0` (thiscall C): `(float)(int)[[C+0x718]+0x5d] * mult`; `mult` = `diff[0]` (EnemyHealthMultiplier, 0x676370) if the controller `[C+0x2fe]` (0x6917b0) is non-null and its vtable+0x8c returns true, else `diff[8]` (PlayerHealthMultiplier, 0x676390). `diff` = entry `[difficulty_obj+0x20] + 32*[difficulty_obj+0xf5]` of `[C+0x490]` (0x6760e0) | float | asm 0x691bd0-0x691c2d; difficulty field names/order from parser 0x6770a0 (EnemyHealth +0, AmmoFound +4, PlayerHealth +8, AutoAimRadius +0xc, AutoAimHeight +0x10, HealthRegenerate +0x14, HealthGainedImmediately +0x18, BulletTimeGainedPerKill +0x1c) | base int `[[C+0x718]+0x5d]` is also the divisor in 0x695fb0 (`[+0x61]/[+0x5d] * maxHealth`) | formula: high. Which multiplier branch the player takes: MEDIUM (player controller's 0x8c is not proven). Numeric values are data (character params + difficulty config): NOT READ. Driver: simplest is to call 0x694480 / 0x691bd0 or compute `health / (float)(int)[[C+0x718]+0x5d]` and treat the multiplier as unknown |
| Pending heal | `C+0x72c` (float) | float | 0x695a80 (heal): `[C+0x72c] += amount - min(amount, maxHealth*diff[0x18])`; fraction 0x6944e0 = `[C+0x72c]/maxHealth` | HUD 0x4aee20 draws it as the second bar segment (wrapper 0x421ea0) | high (role), low importance |
| Dead flag | `*(u8*)(C+0x235)` (= `ST+7`), nonzero = dead; read via 0x6d6eb0 | u8 | die routine 0x6963f0: early-out if `ST+7` already set; at the end `ST+0x85 := 0` (call 0x6d71a0), `ST+7 := 1` (asm 0x6964ec-0x6964f7: `push 1; call 0x6d6ec0`), `ST+0x83 := 0`. Revive path 0x6972a0 sets `ST+7 := arg` | (a) game-mode key handler 0x44c1b0 asm 0x44c84b-0x44c85b: player exists and `0x6d6eb0` true -> posts the "dead" message instead of quicksaving; (b) collision test 0x694ef0 skips pairs where either char has `0x6d6eb0` true | high |
| Alive test | `health > 0 && ST+7 == 0` | - | setHealth 0x695fb0 triggers 0x6963f0 when new health <= 0 and `ST+7 == 0` (asm 0x696178-0x6961f4) | same as dead flag | high |
| Painkiller weapon id | `*(i32*)(C+0x728)` (-1 = none) | i32 | set in the character init 0x683f50 (C line 1728: `[C+0x728] = FUN_00729f60()` name lookup, exception text 0x8924ac `Character "%s" does not have weapon "%s" (weapon names are used, not indices)`); name from config `PainkillerWeaponName` (0x50e070, default "painkiller") | 0x6959d0 and 0x694630 both pass `[C+0x728]` | high |
| Painkiller count | call `0x694630` (thiscall C) -> int; memory: `pool = [WS+0x10] + 30*[wdef+0x3d]` where `wdef = DB.arr[id]`, `DB = **(WS+4)` ... then `count = *(i32*)pool` (or `*(i32*)(pool+8)` if `*(u8*)(pool+0xc)` infinite flag) | i32 | use routine 0x6959d0 (called by key handler 0x44c1b0 on PAINKILLER): `E = 0x729ed0(WS,[C+0x728])`; requires `0x748250(E) > 0`; then `0x748290(E,1)` (decrement) and heal 0x695a80. 0x694630 is the same lookup returning `0x748250(E)` | HUD ammo routine 0x4afcf0: `0x421f20` (= 0x694630) printed with "%d" in its own text slot, hidden when < 1 (asm 0x4afda8-0x4afe0d) | count: high. Memory path: MEDIUM (std::map/array internals decoded from 0x729ed0, 0x748250, 0x53b860; prefer calling 0x694630 in the bench) |
| Ammo pool record (30 bytes) | `[WS+0x10] + 30*idx`: `+0` i32 current, `+4` i32 (unknown), `+8` i32 max, `+0xc` u8 infinite flag, `+0xd..` std::map<id,bool> of weapons that use it | struct | 0x748250 (`if [+0xc] return [+8] else return [+0]`), 0x748290 (subtract, clamp 0..[+8], no-op if infinite), 0x748310 (map lookup at +0xd), pool index from 0x53b860 = `[wdef+0x3d]` | pool decrement in painkiller use and reset 0x692d00 | MEDIUM |
| Current weapon id | `*(i32*)(C+0x2a9)` (= `ST+0x7b`, getter 0x6d6fb0 via wrapper 0x421ef0/0x694570); same value as `[WS+8]` (-1 none) | i32 | HUD 0x4afcf0 asm 0x4afd11-0x4afd1c: `call 0x421ef0` gives the weapon id it shows; character reset 0x692d00 writes `uVar1 = 0x729900(WS)` (= `[wdef+0x41]` of the current weapon) into `ST+0x7b`, `ST+0x7f`, `ST+0xc7` via setters 0x6d6fc0/0x6d6fd0/0x6d74d0; setCurrentWeapon 0x729940 stores id in `[WS+8]` | 0x7299e0 (current wdef = `DB.arr[[WS+8]]`) used by 17 callers | id: high. Slot group 0..5 (melee/handgun/...): UNPROVEN (field of wdef not identified; `[wdef+0x41]` = id, `[wdef+0x36]` = dual-wield flag (0x53b850), `[wdef+0x3d]` = ammo pool index) |
| Clip (rounds loaded) | `*(i32*)([WS+0xc] + 0x1cb)`; second hand (dual wield) at `[WS+0xc] + 0x26d + 0x1cb`. Total loaded = call `0x694580`-sibling `0x6945a0` (= 0x729ce0, sums both) | i32 | 0x757f60 `mov eax,[ecx+0x1cb]`; fire 0x757f50 `dec [ecx+0x1cb]; inc [ecx+0x223]`; sum loop 0x729ce0 (`add esi,0x26d; cmp esi,0x4da`); setCurrentWeapon 0x729940 sets object 0 and, if `[wdef+0x36]`, object `+0x26d` | HUD 0x4afcf0 prints `0x421f10` (= 0x6945a0 -> 0x729ce0) | high (single weapon), MEDIUM (which number the HUD places where) |
| Reserve ammo of current weapon | call `0x694580` -> `0x748250(pool of current weapon)` (pool = `0x7299b0(WS)`) | i32 | wrapper 0x421f00 -> 0x694580 -> 0x6916e0, 0x7299b0, 0x748250 | total = reserve + loaded in 0x729d10 (`pool count + sum of clips`, same-pool check via `0x53b860`); HUD 0x4afcf0 prints 0x421f00 and 0x421f10 as the two ammo numbers | high (semantics), MEDIUM (HUD layout) |
| Infinite-ammo flag of weapon id w | call `0x6945b0(w)` (wrapper 0x421f70) -> `*(u8*)(pool+0xc)` | u8 | 0x6945b0: `0x729ed0(WS,w)` then `0x7482d0` (`mov al,[ecx+0xc]`) | HUD branches on it (shows weapon text instead of numbers) | MEDIUM |
| Bullet-time meter | `*(float*)(GM+0x12f0)`, range 0..1 | float | per-frame GM update 0x44c1b0: regen `+= 0x687b80(player)*diff...` then clamp to 1.0 (asm 0x44cbd3-0x44cc0a: `fcomp [0x7cbdd0]; mov [ebx+0x12f0],0x3f800000`); drain while active `-= dt/SlowMotionTime`, at <= 0 force off (asm 0x44ce85-0x44cefe: clears `+0x12f4`, calls 0x510730) | HUD 0x4aee20: `FUN_00421ae0` (0x44c090 `fld [ecx+0x12f0]`) multiplies the bar height; save 0x454fa0 writes `+0x12f0` then `+0x12f4` (and `+0x12f8`, `+0x12fc`); activation test 0x44ebb0: allowed only when `Threshold(0x50c680 on [GM+0x10cc]) < meter` | high |
| Bullet time active | `*(u8*)(GM+0x12f4)` nonzero = active (getter 0x44c0a0) | u8 | toggle 0x44ebb0: sets 1 (calls 0x5106f0 = start slow-mo, `++[GM+0x1300]`) or 0 (calls 0x510730 = end, subtracts 0x5e53d0 from the meter) | HUD 0x4aee20 uses 0x421af0 (= 0x44c0a0) to pick the bar style; save 0x454fa0; drain code tests it (0x44ce85) | high |
| Bullet-time config (SlowMotion) | object at `[GM+0x10cc]`: `+0` Threshold, `+8` SlowMotionTime, `+0xc` RegenerationTime, `+0x14` ShootDodgeThreshold, `+0x1c` MusicVolumeFadeTime | floats | parser 0x50f8d0 (names 0x87e320-0x87e398 paired with destination offsets); getters 0x50c680 (`[ecx]`), 0x5830e0 (`[+0xc]`), 0x5830f0 (`[+0x14]`) | used by 0x44ebb0 and 0x44c1b0 | MEDIUM (name-to-offset pairing from the parser table; values are data: NOT READ) |
| Timed-mode clock (not bullet time) | `*(float*)(C+0x76c)` (getter 0x697380, wrapper 0x421f30) | float | HUD 0x4aee20 prints it as m:s.cs only when 0x421c70 (TimedMode) is true | - | MEDIUM |
| Skin / name | skin id `*(i32*)(C+0x714)` (0x694530); character params ptr `[C+0x718]`, name string pointer at `[[C+0x718]+4]` | i32 / ptr | 0x694530; 0x691700 `mov eax,[ecx+0x718]`, 0x694460 returns it, overlay prints `[eax+4]` as the name | overlay 0x422d60 | high |

## Overlay wrappers 0x421e70..0x421fd0 (each: `call 0x421a50` = `mov eax,[ecx]` unwraps the handle to X_Character*, then jumps to the getter)

| wrapper | target | returns |
|---|---|---|
| 0x421e70 | 0x694460 | params pointer `[C+0x718]` (name at +4) |
| 0x421e80 | 0x694470 | health float `[C+0x2b3]` |
| 0x421e90 | 0x694480 | health / maxHealth (<= 1.0) |
| 0x421ea0 | 0x6944e0 | `[C+0x72c] / maxHealth` (pending heal fraction) |
| 0x421eb0 | 0x694530 | skin int `[C+0x714]` |
| 0x421ec0 | 0x694540 | `&C[0x41e]` velocity vec3 |
| 0x421ed0 | 0x694550 | `&C[0x411]` gravity velocity vec3 |
| 0x421ee0 | 0x694560 | `&C[0x42a]` animation velocity vec3 |
| 0x421ef0 | 0x694570 | `[ST+0x7b]` current weapon id (`[C+0x2a9]`) |
| 0x421f00 | 0x694580 | ammo pool count of the current weapon (reserve) |
| 0x421f10 | 0x6945a0 | sum of rounds loaded in both weapon objects (clip) |
| 0x421f20 | 0x694630 | painkiller count (pool of `[C+0x728]`), 0 if the pool does not list that weapon |
| 0x421f30 | 0x697380 | float `[C+0x76c]` (timed-mode clock) |
| 0x421f40 | 0x694670 | bool: scan of list `[C+0x7d0]` for an entry of type 4 (UNPROVEN meaning; HUD uses it to show/hide) |
| 0x421f50(w) | 0x6945d0 | `0x748310(pool(w), w)` pool lists weapon w |
| 0x421f70(w) | 0x6945b0 | `[pool(w)+0xc]` infinite-ammo flag |
| 0x421f90(w) | 0x694610 | `0x729d80(w)` = total (pool + loaded) > 0 |
| 0x421fb0 | 0x6d74e0 on ST | `[ST+0xc7]` (second weapon id, used when HUD mode 0x421c10/0x421c40 flags are set) |
| 0x421fd0 | 0x6945f0(0x421fb0) | `0x729d10([ST+0xc7])` total ammo for that id |

## Camera-target interface of X_Character (vtable at `C+0x17c`, class sub-vtable "380" in rea/map/MaxPayne.exe.vtables.tsv)

Used by MaxPayne_PlayerCamera #34 0x504130 (`param_2` = the target): slot0 0x693070 -> 0x74e8e0 = `&C[0x4e]` 3x4 matrix; slot1 0x693080 -> 0x642d40 = `[C+0x7e]` (parent/room object ptr); slot2 0x693090 -> `&C[0x49c]` vec3; slot3 0x6930a0 -> `&C[0x4a8]` vec3; slot4 0x6930b0 -> `&C[0x4c0]` vec3; slot5 0x6930c0 -> `[C+0x7e]+0x50`. `C+0x49c/0x4a8/0x4c0` are written by the crosshair routine 0x68ebd0 (setters 0x691c70/0x691c90/0x691cd0): aim-ray start and end points in world space (so the camera target interface is the aim ray, not only the body).

## Not proven / open

- Forward axis of the body matrix (+Z is the best reading; confirm with one forward-walk sample).
- Weapon slot group (0..5) of a weapon def.
- Player-branch of the max-health multiplier (value depends on the config files anyway).
- Numeric values of difficulty table, SlowMotion config and base max health: data, not read here.
- `GM+0x12f8` (float, counts down at 0x44cf0c, then reset from `0x725be0` of the slow-mo object) and `GM+0x12fc` (u8): saved with the bullet-time state, role not decoded.

# Game mode, level, loading, camera (second report)

All offsets decimal-free hex. Structs are packed (pack 1), unaligned offsets are normal.
rs = Remedy std::string: +0 allocator (unused), +4 char* (NULL = empty), +8 length, +0xC capacity
(proof: 0x451c50..0x451cea copies into [gm+0xbc] with ptr=[+0xc0], len=[+0xc4], cap compare [+0xc] vs 0x1f).
Roots (proven chain, facts.md): theApp = 0x8a6338; MS = [theApp+0xc8] (MaxPayne_ModeSwitch, vtable 0x7cbf0c);
GM = [MS+0x80] (MaxPayne_GameMode, vtable 0x7ccec4).
MS also is an X_ModeSwitch (vtable 0x7d3418) at this-offset 0 (same pointer: 0x601250 `mov esi,ecx; [esi+0xd]` and its
virtual calls [*this+0x10],[*this+0x14] resolve to MaxPayne_ModeSwitch slots 4,5 = 0x417950,0x417970).

## Table 1. ModeSwitch fields (MS = [theApp+0xc8])

| field | path | type | proof (asm) | second source | conf |
|---|---|---|---|---|---|
| MS+0x70 ConsoleMode | MS | ptr MaxPayne_ConsoleMode (vt 0x7cc3e8) | ctor 0x415dc0: 0x416c5c `mov [esi+0x70],edi`, edi=result of 0x429590 (0x4295e6 stores vt 0x7cc3e8) | overlay getter 0x417970 idx1 -> [ecx+0x70] | high |
| MS+0x74 StatisticsMode | MS | ptr MaxPayne_StatisticsMode (vt 0x7cc1fc) | 0x416df3 `mov [edx+0x74],esi`, esi from 0x4229d0 (0x422a07 stores vt 0x7cc1fc) | 0x417970 idx2 -> [ecx+0x74]; toggle 0x417d20 uses [esi+0x74] | high |
| MS+0x78 HUDMode | MS | ptr MaxPayne_HUDMode (vt 0x7cefc8) | 0x416f72 `mov [ecx+0x78],esi`, esi from 0x4ae010 (0x4ae1dd stores vt 0x7cefc8) | 0x417970 idx0 -> [ecx+0x78]; name "HUD" lookup 0x417800 | high |
| MS+0x7c MenuMode | MS | ptr MaxPayne_MenuMode (vt 0x7cc500) | 0x4174bc `mov [ecx+0x7c],esi`, esi from 0x429d10 (0x429dfa stores vt 0x7cc500) | 0x417790: name "menu" -> [ebx+0x7c] | high |
| MS+0x80 GameMode | MS | ptr MaxPayne_GameMode (vt 0x7ccec4) | 0x4170f9 `mov [eax+0x80],esi`, esi from 0x44b660 (0x44b7c0 stores vt 0x7ccec4) | 0x4178d0 getMode(0) -> [ecx+0x80]; name "game" 0x417790 | high |
| MS+0x84 GraphicNovelMode | MS | ptr MaxPayne_GraphicNovelMode (vt 0x7ce920) | 0x417324 `mov [eax+0x84],esi`, esi from 0x49a940 (0x49aa11 stores vt 0x7ce920) | 0x417790: name "graphicnovel" (0x8a66c0) -> [ebx+0x84] | high |
| MS+0x88 X_SharedDB | MS | ptr (class X_SharedDB, "Materials.txt/Particles.txt/Music.txt") | 0x416a9a `mov [ebx+0x88],edi`, ctor 0x528d90 -> 0x528dc0 | getter 0x417780 `mov eax,[ecx+0x88]` | high (not a mode) |
| MS+0x8c..0xc8 (16 dwords) | MS | ptrs to config data blocks, NOT modes | 0x41605e,0x4160e4,... 0x416778: one `mov [ebx+off],edi` after each ctor 0x50a160, 0x50ace0, 0x50c510, 0x50c6c0, 0x50bcb0, 0x50c390, 0x50c890, 0x50d510, 0x50d660, 0x50da70, 0x50df90, 0x50f7f0, 0x50e7c0, 0x50ec10 (+0xc0), 0x5108e0 (+0xc4), 0x511430 (+0xc8) | game.md: +0xc0 = loading screen data 0x50ec10 | ctor addresses high; class names UNPROVEN |
| MS+0xcc | MS | ptr, result of 0x419830 (arg 0x868d48) | 0x415ebd | - | UNPROVEN purpose |
| MS+0xd0 | MS | ptr copy of GM | 0x4174f9/0x4174ff `mov ecx,[eax+0x80]; mov [eax+0xd0],ecx` | - | copy proven, consumers UNPROVEN |
| MS+0xd4 | MS | ptr SaveGameInterface (ctor 0x40f650, vt 0x7cbe78; strings "MaxPayne_SaveGameInterface") | 0x416b25 `mov [ebx+0xd4],edi` | 0x417ba0 uses [esi+0xd4]->0x410f00/0x410c20 (name -> save index); GM+0xb0 holds the same kind | high class, medium name |

Mode name strings (rs globals built by static init, content read from the exe):
"game" 0x868c08 (rs 0x8a6708), "menu" 0x868c10 (rs 0x8a66f0), "console" 0x868c18 (0x8a66e0), "statistics" 0x868c20,
"graphicnovel" 0x868c2c (rs 0x8a66c0), "HUD" 0x868c3c (rs 0x8a66b0). Static init funcs 0x415150, 0x415230, 0x415310,
0x4153f0, 0x4154d0, 0x4155b0 (Ghidra rea/c).

## Table 2. Which mode is active now (X_ModeSwitch fields, MS = [theApp+0xc8])

Two lists exist, do not confuse them:
- "modes" list (slots 1/2 = 0x4178b0 count, 0x4178d0 get): only [MS+0x80] GameMode (count = 1 if non-null). 0x601250 updates it
  when it is the current mode or when its vslot 0x6c (+27) is true. GameMode's slot 27 is 0x422170 `xor al,al; ret` = false.
- "overlaid modes" (slots 4/5 = 0x417950 count, 0x417970 get): idx0 HUD [+0x78], idx1 Console [+0x70], idx2 Statistics [+0x74].
- "switchable modes" (slots 7/9 = 0x417a20 count 3, 0x417a50 get): idx0 Game [+0x80], idx1 Menu [+0x7c], idx2 GraphicNovel [+0x84].

| field | path | type | proof | second source | conf |
|---|---|---|---|---|---|
| CUR = MS+0x19 | MS | ptr to the active mode (== MS+0x7c Menu / +0x80 Game / +0x84 GraphicNovel) | getter 0x724b90 `mov eax,[ecx+0x19]`; setter vslot 18 = 0x601710 `mov [ecx+0x19],eax` (ret 4). Called by 0x6014d0 at 0x6016d0 `call [edx+0x48]` (vslot 18) with ESI = pending mode; used as "current" in 0x601250 (`piVar3 == FUN_00724b90()`) | 0x601b40 switch: if CUR==0 the first mode is set directly (calls vslot 8 by name, vslot 18); 0x6020c0 message handler -> 0x601b40 | high |
| NEXT = MS+0x1d | MS | ptr mode being switched to (nonzero only during fade-out) | getter 0x749780 `[ecx+0x1d]`; setter 0x749790 (called 0x601b40 and 0x6016d7 with 0 = clear) | 0x601250 state==3 branch calls NEXT vslot 0x4c each frame | high |
| STATE = MS+0x0d | MS | int: 0 initial, 1 steady, 2 fade-in of CUR, 3 fade-out of CUR toward NEXT | setter 0x601af0 `[ecx+0xd]=a; [ecx+0x11]=0; [ecx+0x15]=b`; 0x601250: `cmp [esi+0xd],3` and `==2`; ends 2 -> 1 at 0x60135c; 0x601b40 sets 3 (`push 3`) when CUR!=target and 2 for first mode | progress = [MS+0x11] / [MS+0x15] (floats, 0x601b10) | high for 1/2/3, medium for 0 |
| PENDING = MS+0x51 | MS | ptr, mode to install at next render | 0x601305 `mov [esi+0x51],eax` when fade-out reaches 1.0; consumed in 0x6014d0 0x6015b5..0x6015c6 (`[ebp+0x51]=0`) and 0x6016d0 setCurrent | 0x6011d0 clears CUR/NEXT/PENDING when a mode is destroyed | high |
| MS+0x55 | MS | byte, "some mode other than current ran this frame" | 0x601250 end: `[param_1+0x55] = cVar8` | used 0x6016ef | medium |
| overlay visible | each of MS+0x70/+0x74/+0x78 | int [overlay+0x7d] != 0 | 0x60ee90 `mov edx,[ecx+0x7d]; setne al` (used in 0x601250 overlay loop `FUN_0060ee90`); setter 0x60eeb0; toggles 0x417c90 (Console) and 0x417d20 (Statistics) set 1/2/3 | HUD has no toggle here | medium (state codes 1/2/3 meaning per overlay UNPROVEN) |
| mode stack | MS+0x21: vector (begin +0x25, end +0x29, cap +0x2d) of overlay ptrs | 0x601720 (top overlay with state==1), 0x601760 | 0x6017d0 push (called by 0x417c90/0x417d20/ctor), 0x6019d0 pop | - | medium |

How to say menu / graphic novel / game (decision table):
- CUR == [MS+0x7c] -> menu (main menu OR in-game menu, see Table 3 "paused").
- CUR == [MS+0x84] -> graphic novel.
- CUR == [MS+0x80] -> game (play, or loading, see Table 3).
- NEXT != 0 or STATE in {2,3} -> a mode fade is running (NEXT is the target).
- Statistics = [[MS+0x74]+0x7d] != 0 (overlay), console = [[MS+0x70]+0x7d] != 0.
Mode switch is requested by a message built with the target name ("menu" 0x44c50b = Esc in game tick 0x44c1b0,
"graphicnovel" 0x44c640, "game" in 0x49c910/0x42a4e0/0x42ba00/0x42bc40, and "menu" as the first mode at the end of the
ctor 0x4176c1); handler 0x6020c0 -> lookup vslot 8 (0x417790 name -> [+0x7c]/[+0x80]/[+0x84]) -> 0x601b40.
Initial mode after boot: the ctor ends by posting "menu" (0x41769e..0x4176c1). Statistics UNPROVEN as a *current* mode
(it is only an overlay in this build).

## Table 3. Level name, pending level / loading, paused (GM = [MS+0x80])

| field | path | type | proof | second source | conf |
|---|---|---|---|---|---|
| PENDING_CMD = GM+0xb4 | GM | int: 0 none, 1 init level (gm_init), 2 load game, 3 save game, 4 save game (flag 1: quick/auto) | getter 0x40e4e0 `mov eax,[ecx+0xb4]`. 0x451960 (called from the game tick): `dec eax; cmp eax,3; jmp [eax*4+0x451e3c]` table = {0x4519c1,0x451a8b,0x451b17,0x451b4c}; state 1 -> 0x44fda0 (level init) with name rs GM+0xdc and byte GM+0xec; state 2 -> 0x40ff40/0x40fff0 (load savegame by idx GM+0xb8 or name GM+0xbc); 3/4 -> 0x410300 (SaveGame). End 0x451cee `[ebx+0xb4]=0`. | setters: 0x44be30(state) used by ModeSwitch slots 12..15 (0x417b00 `push 2`, 0x417b30 `push 2`, 0x417b70 `push 3`, 0x417ba0 `push 4`); state 1 set by the GM_Init message handlers (flow.md). 0x44be30 validates via SaveGame interface (0x410ce0/0x410d60/0x410e20) and resets to 0 if the save does not exist | high |
| PENDING_IDX = GM+0xb8 | GM | int (-1 = use the name) | 0x451ab3 `cmp eax,-1`; 0x44bec0 setter; reset to -1 at 0x451c3c | - | high |
| PENDING_SAVENAME = GM+0xbc | GM | rs (ptr GM+0xc0, len GM+0xc4) | 0x451ac2 `lea eax,[ebx+0xbc]` -> 0x40fff0; assigned by 0x44bed0; cleared 0x451c50.. | - | high |
| PENDING_LEVEL = GM+0xdc | GM | rs (ptr GM+0xe0, len GM+0xe4): level name given to gm_init(<name>) | 0x4519f0 `lea ecx,[ebx+0xdc]` passed to 0x44fda0 | cleared to "" at the end of 0x451960 (0x451b8f..0x451c35 copies 0x8a6334 empty string into +0xdc) so it is non-empty only until the tick consumes it | high |
| NEWGAME flag = GM+0xec | GM | byte | passed to 0x44fda0 (`mov al,[ebx+0xec]; push eax`), cleared 0x451a37 | - | medium |
| LEVEL LOADED = GM+0x1080 | GM | ptr player wrapper, NULL when no level | set 0x4520fc (player creation 0x451e70), cleared 0x45246e, init 0x44ee6a; 0x451e50 hasPlayer `setne al` | facts.md (X_Character = [[gm+0x1080]+8]) | high |
| LEVEL ENTRY = GM+0xf8 | GM | ptr to the level descriptor chosen by name | 0x44ff7f..0x44ffc8: `mov ecx,0x8a6b50; call 0x5250a0(name rs GM+0xdc)` (rs::assign into global key 0x8a6b50), then map find `0x5495e0(&0x8a6b50)` over the container [[GM+0xf4]->0x5ebc70], index -> `mov [esi+0xf8],ecx` | cleared 0x44ee39 (ctor) | high |
| CURRENT LEVEL NAME = [GM+0xf8]+0x10 | GM -> entry | rs (char* at entry+0x14, len at entry+0x18) | 0x65a1c0 `lea eax,[ecx+0x10]`; used as the "level" argument of SaveGame 0x410300 (0x451a54 `mov ecx,[ebx+0xf8]; call 0x65a1c0; push eax`) and as the "Level Name" column of the performance table (0x453b40/0x456d10) | global key rs 0x8a6b50 holds the last name looked up (assigned from GM+0xdc in 0x44fda0 and 0x4539f0) | medium (that +0x10 is the plain level name, not a path, is inferred from its use in SaveGame/perf table; read it once to confirm) |
| global level key = 0x8a6b50 | absolute | rs (ptr at 0x8a6b54, len 0x8a6b58) | see LEVEL ENTRY | only two functions touch it (0x44fda0, 0x4539f0) | high as "last looked-up name"; medium as "current level" |
| LEVEL OBJECT = GM+0x104 | GM | MaxPayne_Level embedded (not a pointer), ctor 0x493f80 called with ecx=`lea ebx,[esi+0x104]` | 0x450054 `lea ebx,[esi+0x104]` ... 0x45008e `call 0x493f80` | GM ctor 0x44b6a0 `lea ecx,[esi+0x104]; call 0x493bb0` | high |
| LEVEL DB = GM+0xf4 | GM | ptr (levels container); getter 0x44bce0 | set 0x44f34a | used as argument 0x44bce0 -> Level ctor | medium |
| SaveGameInterface = GM+0xb0 | GM | ptr | used 0x451a5f, 0x44be4c (`mov ecx,[esi+0xb0]`) | MS+0xd4 same type | medium |
| LOADING | (derived, no single flag) | - | The whole level load runs synchronously inside the game tick: 0x44c1b0 -> 0x451960 -> 0x44fda0 (flow.md; no frames between). Observable markers: (a) at tick entry GM+0xb4 != 0 and GM+0xdc non-empty (pending level); (b) during load GM+0x1080 == 0 (player not yet created; player creation is inside 0x44fda0 "Player creation") or GM+0xf8 changes; (c) the comic/loading screen shows through GraphicNovel (CUR == MS+0x84) before the level starts. Frame counter global 0x8b3d70 is reset to 1 at 0x4519db / 0x451aa3 (load start) and set each tick at 0x44c223 (so it is a frame stamp, NOT a loading flag) | - | UNPROVEN as a standalone "loading" boolean; combination above is proven |
| PAUSED / in-game menu | CUR == MS+0x7c (Menu) while GM+0x1080 != 0 | - | Esc in the game tick (0x44c431..0x44c4fc) sets `[ebx+0xef]=1` and posts the "menu" mode message (0x44c50b, 0x60e610); 0x44bd20 (GM slot 9) returns `[ecx+0xef]`; 0x44bdb2/0x44bdbe clear it when game mode is re-entered | `[GM+0xef]` byte | medium (flag semantics inferred; CUR test is high) |
| GM+0x12ce / GM+0x12cf | GM | bytes toggled by HUD inventory functions 0x453f50, 0x4540e0 (strings "MPHM_PrintInventory") | writes 0x45400e/0x4540b1 | 0x451960 calls vslot 0x60 if [+0x12ce] | NOT a pause flag (inventory/HUD state) |

## Table 4. Player camera and aim

GM+0x2e9 is the embedded MaxPayne_PlayerCamera: GM ctor 0x44b660 `lea ecx,[esi+0x2e9]; call 0x503c20` (0x503e19 stores vptr 0x7d00dc at +0).
Embedded siblings in the same ctor: GM+0x258 MaxPayne_PlayerInput (0x4ce4f0, vt 0x7cf974), GM+0x2e1 an 8-byte object
(0x508520, camera-target holder, set by 0x503b50(GM+0x2e1)), GM+0xa1a MaxPayne_FlyCamera (0x5089d0, vt 0x7d0194).

| field | path | type | proof | second source | conf |
|---|---|---|---|---|---|
| ACTIVE CAMERA = GM+0x1074 | GM | ptr, == GM+0x2e9 (player) or GM+0xa1a (fly camera) | 0x453360: `if (GM+0x2e9 == [gm+0x1074]) { [gm+0x1074]=GM+0xa1a; ... } else { [gm+0x1074]=GM+0x2e9 ... }` (debug fly-cam toggle); getter 0x453430; set at level init 0x4513b2 `mov [ebx+0x1074],edi`; cleared 0x44ee2d, 0x44fa47 | game tick calls `[ [gm+0x1074] ]->vslot 0x88 (#34)` with target `PlayerChar+0x17c` and dt at 0x44d91e/0x44d985 (profile "Camera" 0x86cff4) | high |
| PlayerCamera P | = GM+0x2e9 | struct | see above | 0x44d11f..0x44d1e2, 0x44cf41 use `lea edi,[ebx+0x2e9]` | high |
| camera POSITION | P+0x54 | float x,y,z (world) | getter 0x642d50 `lea eax,[ecx+0x54]`; written by 0x642ed0 (arg2 -> [esi+0x54..0x5c]); delta vs [esi+0x54] in the collision move 0x647660 (0x647691 `fsub [esi+0x54]`, +0x58, +0x5c); read by the game tick at 0x44da3d (`mov ecx,[ebx+0x1074]; call 0x642d50`) | 0x504130 final position via 0x64c910 (0x505512) | high |
| camera ROTATION | P+0x30 | 9 floats (3x3, row-major as stored: 3 consecutive rows of 3) | 0x642ed0 and 0x642c00 copy `[esi+0..0x20]` -> `[ecx+0x30..0x50]` (0x642c0b `add ecx,0x30`); PlayerCamera #34 0x504130 ends with `call 0x642c00` (0x506174) on this matrix | identity default global 0x43dfc0 -> 0x8b82e0 (1,0,0,0,1,0,0,0,1) | high for location; axis convention (which row is forward/up, Y-up) UNPROVEN |
| camera scalar | P+0x60 | float (arg1 of 0x642ed0; likely FOV or similar) | 0x642ed0 `[esi+0x60]=edx`; getter 0x642d40 | char CameraTarget slot 1 returns it | meaning UNPROVEN |
| camera follow scratch pose | P+0x700 | 12 floats: 3x3 + vec3 at P+0x724 | 0x504130: `lea eax,[ecx+0x700]`; ctor 0x503c20 fills it from identity (0x43dfc0) + zeros | - | internal temp of #34, do not read as final pose |
| camera implementation | P+0x64e | ptr X_CameraImplementation (vt 0x7d56cc; vslots +0x28/+0x2c/+0x30/+0x34/+0x3c/+0x44/+0x48/+0x4c used by #34) | 0x504130 `mov ecx,[edi+0x64e]` | - | medium (class name from vtable list) |
| camera dirty/cache | P+0x78..0x80 (last position), P+0x84, P+0x88/+0xb0 dirty flags | - | 0x6461c0 compares [esi+0x78..0x80] against 0x642d50; 0x642ed0 sets [+0xb0]=1,[+0x88]=1 | - | low |
| player world pose (character) | CH+0x4e (3x3), CH+0x72 (vec3), CH+0x7e (float); CH = [[GM+0x1080]+8] | - | X_Character CameraTarget thunks (vt 0x7d5930 this=CH+0x17c): slot0 `add ecx,-0x15e; jmp 0x74e8e0 (lea +0x30)` -> CH+0x1e+0x30 = CH+0x4e; same base as camera (+0x30 matrix, +0x54 pos, +0x60 scalar) -> CH+0x72 pos | camera follows via 0x504130 reading these through the target interface | high that the base (CH+0x1e) has the same layout as the camera base; "+0x72 = position" inferred by identical class accessors (0x642d50) |
| camera target vectors | CH+0x49c, CH+0x4a8, CH+0x4c0 | vec3 each | CameraTarget slots 2,3,4 (+8,+0xc,+0x10): 0x691c40/0x691c50/0x691c60 `lea eax,[ecx+0x49c/0x4a8/0x4c0]`; 0x504130 uses (+0xc)-(+8) as a direction | setter 0x691c70 writes CH+0x49c | meaning (eye / look-at / up) UNPROVEN |
| mouse look -> input | PlayerInput I = GM+0x258 | - | actions: AIMUP/AIMDOWN = action index 11 (value ptr 0x8a932b), AIMLEFT/AIMRIGHT = index 12 (0x8a9354), action table 0x8a9168 stride 0x29 (input.md). Getters: 0x4cc4c0 (idx 11) and 0x4cc520 (idx 12) each `0x4cdc70(&action)`, reached through PlayerInput vslot 18 (0x4ceab0: returns 0 if [I+0x2d]!=0 else 0x4cc4c0) and vslot 19 (0x4cead0: 0x4cc520) | CORRECTION: 0x4cc560/0x4cc5c0 (input.md "aim getters") are NOT look consumers: they test the controls object and call 0x4cd5c0 / 0x4ce060 = the key-REBIND capture. The only call sites 0x42aa7c/0x42b22c are in the controls menu dispatcher 0x42a500 (switch on menu message). | high |
| yaw delta | I+0x19 | float, radians, PER FRAME DELTA | PlayerInput #32 0x4cecd0 (per tick, dt = `call 0x7269f0`): `I+0x19 = vslot19() * 180.0 * dt * 0.017453292` (0x4cefb5..0x4cefc8 `fmul [0x7cfb08]=180.0; fmul [0x7cfb04]=0.01745329`); right sign UNPROVEN | read back by PlayerInput slot 23 0x4cf2e0 `fld [ecx+0x19]` | high for formula (180 * pi/180 = pi, so rad = action_value * dt * pi); sign/positive direction UNPROVEN |
| pitch delta | I+0x1d | float, radians, PER FRAME DELTA | same: `I+0x1d = vslot18() * 180 * dt * 0.01745` (0x4cefce..0x4cefdd) | slot 24 0x4cf2f0 | high for formula |
| look chain unit | counts -> action value | - | input.md: value = (1/dt)*(counts*(slider*1.4+0.1) smoothed)*0.0025 => per frame angle(rad) = counts_smoothed * (slider*1.4+0.1) * 0.0025 * pi | - | medium (chain of two documented steps) |
| absolute aim yaw/pitch of the player | - | - | The accumulation of I+0x19/I+0x1d into a character angle was NOT found (consumer = X_CharacterInput interface; MaxPayne_LocalCharacterInput vt 0x7cff9c slots 3/4 = 0x503280/0x5032b0 return I.vslot 23/24 through `[this+0x1b5]`; the caller in X_Character was not located by byte search of `call [reg+0xc]/[reg+0x10]` near 0x421a50). | - | UNPROVEN |

### Practical reading recipe (all float32 little-endian guest memory)
- MS = u32[0x8a6338+0xc8]; GM = u32[MS+0x80]; CUR = u32[MS+0x19]; mode = (CUR==u32[MS+0x7c]) menu, (==u32[MS+0x84]) graphic novel, (==GM) game.
- Level loaded: u32[GM+0x1080] != 0. Level name: entry = u32[GM+0xf8]; s = u32[entry+0x14] (len u32[entry+0x18]).
- Pending: u32[GM+0xb4] (1 = level init queued, name rs at GM+0xdc: ptr u32[GM+0xe0], len u32[GM+0xe4]).
- Camera: active = u32[GM+0x1074]; position = f32x3[active+0x54]; rotation = f32x9[active+0x30]; player camera is the active one iff active == GM+0x2e9.
- Note: GM+0x2e9 is an address inside GameMode (embedded), not a pointer to load.

### Method notes
Disassembly: capstone over build/img/MaxPayne.exe (rebased); Ghidra corpus rea/c/MaxPayne.exe/*.c and rea/map/MaxPayne.exe.tsv /
.vtables.tsv for the second source. Nothing was run in a game; all "type" entries are from code, not memory dumps.

## Aim pitch (vertical look)
| field | path | units | proof | second source | confidence |
|---|---|---|---|---|---|
| aim object | A = u32[C+0x498] | ptr | getter 0x6916d0 | capstone disasm | H |
| pitch | f32 [A+0x20], + looks down; local aim vector A+0x14 = (0, -sin p, cos p) | rad | accumulator 0x725b30, reader 0x725be0 | runtime: ghost calibrate (word 344 moves with lY) | H |
| clamp | +-1.5533431 (89 deg) | rad | 0x7d8c08 / 0x7d8c04 (imm 0x3fc6d3f2 / 0xbfc6d3f2) | | H |
| mouse lY | delta = smooth0.5(lY x (sliderY*1.4+0.1) x GM+0x1260) x 0.0025 x pi; lY > 0 (down) looks down; Invert Mouse (obj+0x51) negates | rad/count | 0x519070 -> 0x519480 -> 0x519370 -> 0x5195c0 (0.0025 imm 0x3b23d70a at 0x51962b) -> 0x4cecd0 -> I+0x1d -> 0x725b30; I = GM+0x258 | | M-H |
| copies | C+0x239 pitch copy (per tick), C+0x377/+0x373 pitch/yaw deltas, yaw f32 C+0x295 (0..2pi) | rad | | | M |
| aim ray | built in 0x68ebd0 from A+0x14 and the body matrix C+0x4e; stored C+0x49c, +0x4a8, +0x4c0. Aim = character pitch, not the camera | | | | M (bullet use UNPROVEN) |

## Player control (cutscenes, scripted cameras)
- GM_SetPlayerControls handler 0x456650 (registration 0x458190 -> 0x465f80, message 0x436aa0): bool `msg+0x2f`; writes the
  input link `u32[W+0x1c9]` (W = [GM+0x1080]) = GM+0x258 (PlayerInput) when on, 0 when off. MaxPayne_LocalCharacterInput
  (0x5030e0 / 0x503100, ~25 slots) returns 0.0 and forwards nothing while it is 0. Also u8 C+0x6aa (2D aiming crosshair) off.
- Camera X_CameraImplementation CI = [GM+0x93] (vtable 0x7d56cc): busy when `[CI+0x13f] && !u8 CI+0x14b && !u8 CI+0x14d`
  (slot +0xc) or `u8 CI+0x14e` (slot +8). Inventory overlay u8 GM+0x12ce unlinks the input. GM+0x1074 stays the player camera.
- After level create (0x452126) and loadGame (0x454496) the link is on at once; a script's SetPlayerControls(false) comes later.
- Proof: static (Ghidra + capstone); live (host/gamestate.c word 351): input 1 -> off with camera busy during the st01 cutscene
  (9 s) and the hallway1 keypad cutscene (7 s) in part1_level1, back on after. Confidence: input link high, camera bits medium.
