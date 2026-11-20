# Weapon reads: why the snapshot says "w 0 clip 1 res 100"

Scope: `host/gamestate.c` lines 157-162 (`player()`). Evidence = corpus `rea/c/MaxPayne.exe/<addr>.c`, the disassembly of
`build/img/MaxPayne.exe` at those addresses (capstone, this pass), `rea/map/weapons.md` §3b, `rea/map/weapons_runtime.md` §6,
`rea/map/gameplay.md` §"Arsenal", and the game data (`build/data/x_data/3_weaponid.h`, `310_empty.txt`).

## The triple is not garbage: it is the EMPTY weapon
`WEAPONID_EMPTY = 0`, `310_empty.txt`: `Clipsize = 1`, `Pocketsize = 100`, `InventoryID = 0` (`3_weaponid.h`).
w 0 / clip 1 / res 100 is exactly id 0, its ClipSize and its PocketSize (the empty pool is infinite, so `0x748250` returns
`[pool+8]` = PocketSize, not `[pool]`). Three independent matches: the snapshot describes a player with **no weapon equipped**,
not a broken pointer. `autoReload 0x729b80` on that empty weapon loads `min(pool 100, ClipSize 1) = 1`, which is the clip 1.
The 40 "shots" consumed nothing because the empty anim (`empty.kf2`) has no `WS_AnimateShooting` message.

## What gamestate.c reads today (all faithful to the game)
- `weapon` = `i32[C+0x2a9]` = `ST+0x7b` (`0x6d6fb0 mov eax,[ecx+0x7b]`, `ST = C+0x22e` via `0x694430`).
- `reserve` = `getter(0x421f00, pw+8)` -> `0x694580`: `[C+0x398]`=`A` -> `0x7299b0` -> `0x748250`.
- `clip` = `getter(0x421f10, pw+8)` -> `0x6945a0` -> `0x729ce0` (sum of both hands).
`0x421f00/0x421f10` are `call 0x421a50 (mov eax,[ecx]) / mov ecx,eax / jmp` thunks, so `ecx` is a *cell holding* `C`
(`pw+8`); passing `pw+8` is right and `player.md` §"Runtime confirmation" already used it (sniper: id 15, clip 5, res 20).
So the offsets are not the reason for the frozen values.

## The correct path (no guest calls needed)
```
A     = i32[C + 0x398]                     ; X_WeaponArsenal, 0x6916e0 mov eax,[ecx+0x398]
hands = i32[A + 0x0c]                      ; 2 x X_WeaponSet, stride 0x26d (0x729ce9..0x729d01)
clip  = i32[hands+0x1cb] + i32[hands+0x26d+0x1cb]      ; 0x757f60; fire decrements at 0x757f50
inHand= i32[hands+0x1bf]                   ; WeaponID attached to hand 0 (0x757b60 writes it)
shots = i32[hands+0x223]                   ; 0x757f50 inc
i     = i32[A + 8]                         ; DB array index, -1 none (NOT a WeaponID)
DB    = i32[ i32[A + 4] ]                  ; two hops: [A+4] is a cell (0x7299b0/0x729900 call 0x421a50)
arr   = i32[DB + 8]
wdef  = i32[arr + 4*i]                     ; X_SharedDBWeapon
id    = i32[wdef + 0x41]                   ; 0x53b870 WeaponID
inv   = i32[wdef + 0x3d]                   ; 0x53b860 InventoryID (ammo pool)
pool  = i32[A + 0x10] + 0x1e*inv           ; 0x7299b0
reserve = u8[pool+0xc] ? i32[pool+8] : i32[pool]        ; 0x748250, +8 = PocketSize, +c = infinite
```
Advance (signed) by `0x26d` from `hands`; `0x1bf` and `0x1cb` are i32, `0xc`/`0x3d`/`0x41` are i32, `+0xc` of a pool is u8.

## What gamestate.c does wrong
1. `weapon` from `C+0x2a9` is a cached property, not the live selection. Only `selectWeapon 0x687310`,
   `onRemoveAllWeapons 0x6992f0`, save/load `0x687180` and reset `0x692d00` push it (always via `setCurrentWeaponID 0x6d6fc0`);
   `X_WeaponArsenal::setCurrentWeapon 0x729940` writes only `[A+8]` and never touches `ST+0x7b`. Read `[A+8] -> wdef+0x41`.
2. The three fields collapse to the empty weapon's numbers (0/1/100) when nothing is equipped, and nothing in the snapshot
   distinguishes that from "beretta in the bag". The current weapon must be exposed as "equipped" (id != 0, `[A+8] != -1`)
   and the pickup state comes from the item table, not from these three.
3. Nothing proves a shot happened. `shots` (`hands+0x223`) and `inHand` (`hands+0x1bf`) are the only fields that show a gun
   in hand and rounds leaving; without them a run cannot tell "picked up and fired" from "sent 40 shoot inputs at the empty anim".
4. `if (rd(c, ch+0x398))` is also the only guard on the HUD thunks: when the arsenal is null the fields are silently 0. Fetch
   `A` once, test it, and publish it (word 24 area) so the page can report "no arsenal" instead of zeros.

## Why the run stayed empty (mechanism, not offset)
`C_PickupWeapon 0x699120` = `0x729f60(name)` -> owned flag `0x748370(id,1)` and, only if global byte `0x6958a0` is set and the
controller `0x6917b0` exists, `controller->vfn+0x4c(id)` (a *request*). It never calls `0x729940`, never writes `ST+0x7b`.
`C_PickupAmmo 0x699570` -> `0x729bb0` only adds to the pool. So `c_pickupweapon(beretta); c_pickupammo(beretta,60)` fills the
bag; `A+8` and `[C+0x2a9]` stay at the empty weapon until the character plays a weapon-switch action. With `A+8 = 0`, the
reads are *correct* and stay 0/1/100 while the beretta pool sits unused at InventoryID 3.

## Cheap check to settle it on the next run
One `rn_log` per level frame: `A`, `[C+0x2a9]`, `[A+8]`, `wdef`, `id`, `[hands+0x1bf]`, `[hands+0x1cb]`, `[hands+0x26d+0x1cb]`,
`[hands+0x223]`, pool `+0/+4/+8/+0xc` for `inv` 0 and 3. If `inHand` is 0 while `[hands+0x223]` grows, the reads are wrong; if
`inHand` is 0 and `shots` is flat, the pickup never equipped and the instrument should say so instead of "w 0".
