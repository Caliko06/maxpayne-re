# Enemy wake-up: from idle/asleep to awake and shooting (MaxPayne.exe 1.05, md5 cda1650af370e50f12e500831a834783)

Sources: Ghidra corpus `rea/c/MaxPayne.exe/` and capstone on `build/img/MaxPayne.exe` (VAs of that image; `.text` VA
0x401000 = raw 0x600). FACT = both sources agree on that line. INFERENCE = one source or code shape. Structs already mapped:
`ai.md` (fields), `ai_behaviour.md` (perception, machines, actions), `ai_open.md` (operators, action 16/30, slots 44/45).
This file only answers: what makes an idle enemy become an awake shooter, and which bytes say which. AP = the enemy's
MaxPayne_ArtificialPlayerInput, MT = AP+0x157 (perception object), ST = C+0x22e (X_CharacterProperties), C = X_Character*.

## 0. The whole answer in five lines
1. There is no "wake event". Awake is a float timer: `AP+0x50b` (= MT+0x3b4) > 0. FACT.
2. It is refreshed only while the enemy perceives the player, and it drains every frame. FACT.
3. Perception = rooms visible AND (a player action radius, or being hit, or general radius), plus a 0.3 s visual scan
   (front half-space, visual radius, clear eye ray). FACT.
4. Every frame `AP.update` (0x5001f0) copies that state into `u8(C+0x2dc)` ("AI active") and into the state machine; the
   machine named by the script (`merccombat`, `nonreactive`...) only decides what to do **if** it sees something. FACT.
5. So an enemy that never shoots is an enemy whose perception never fired, or one that is not ticked at all
   (`u8(C+0x201)` stays 0). Both are readable from the bot. FACT + INFERENCE (§7).

## 1. Awake = f32(AP+0x50b) > 0, written to u8(C+0x2dc) every frame
- `0x4f74b0`: `fld [ecx+0x3b4]` / `fcomp 0.0` / returns 1 when the timer is <= 0, else 0. FACT (asm 0x4f74b0 and
  `004f74b0.c`), i.e. the function returns *sleeping*.
- In `AP.update` 0x5001f0 at **0x50054f..0x500566**: `call 0x4f74b0`, `neg al; sbb al,al; inc al` (= NOT), then
  `0x6d7330(ST)` with that byte; 0x6d7330 is `mov [ecx+0xae],al`, ST+0xae = C+0x2dc. FACT (asm + `005001f0.c`).
  So `C+0x2dc == 1`  <=>  `AP+0x50b > 0`.
- Writers of AP+0x50b (MT+0x3b4):
  - `0x4f7470(arg)`: arg != 0 -> `[MT+0x3b4] = params([MT+0x3dc]+0x718)+0xdd` (EnemyInterestTime); arg == 0 -> 0. FACT
    (asm 0x4f747b..0x4f74a2, `004f7470.c`). Full call scan of 0x4f7470: 0x50054a (AP.update), 0x4fecbe (slot 45 forced
    perceive), 0x4ff113, 0x4f6e3d (perceive, with 0).
  - `0x4f6df0(MT, flag, dt)` decrements it first thing: `fld [esi+0x3b4]; fsub dt; fst [esi+0x3b4]` (asm 0x4f6df9). FACT.
- Consequence: awake lasts EnemyInterestTime after the last perceived frame: 10 s (Junkie), 30 s (Mobster/Merc/Killer),
  360 s (Boss) — values in `ai_behaviour.md` 2.1 from `x_data/3_ai_ranges.h`. INFERENCE on the arithmetic, FACT on the fields.

## 2. Flags a reader can use (all byte reads, no pointer walking needed beyond C and AP)
| what | read | evidence |
|---|---|---|
| **awake / AI active** | `u8(C+0x2dc)` != 0 | setter 0x6d7330 `mov [ecx+0xae],al`, getter 0x6d7340, dump label " AI active" (`ai.md`); writer AP.update 0x500566. FACT |
| awake timer, seconds left | `f32(AP+0x50b)` = `MT+0x3b4` | §1. FACT |
| **perceived now** | `u8(AP+0x506)` = `MT+0x3af` | getter 0x4f6290 used at 0x5004ef, and `004f6df0.c` writes +0x3af. FACT |
| seen now (this scan) | `u8(AP+0x504)` = `MT+0x3ad` | getter 0x4f6260 used at 0x5004be; cleared/set in 0x4f62a0. FACT |
| was hit (damage mirror) | `u8(AP+0x505)` = `MT+0x3ae` = ST+5 | store 0x4f63d6 from `0x6d6e90`; getter 0x4f6280 used at 0x500517. FACT |
| interest (perception memory) | `f32(AP+0x507)` = `MT+0x3b0` | `004f6df0.c` lines 20-40. FACT |
| visual scan timer | `f32(AP+0x50f)` = `MT+0x3b8`, reloaded with 0.3 | asm 0x4f6c69 `mov [esi+0x3b8],0x3e4ccccd`. FACT |
| **ticked this frame** | `u8(C+0x201)` | 0x687ba8 reads/0x687baf clears, 0x687c4a sets (`0x687ba0` updateActivity). FACT for the writes |
| inactive/asleep marker | `u8(C+0x200)` | set 0x687c9b, cleared 0x687cdc in the same function. FACT for writes |
Practice for the bot: `awake = u8(C+0x2dc) && u8(C+0x201)`; if C+0x201 is 0 the AI never even ran that frame.

## 3. What feeds perception — MT update 0x4f62a0 (the only organic wake input)
Everything below sits inside `if (rooms_linked)` from `0x4f17b0` (`004f62a0.c` line 109 `cVar4`, block lines 113-145).
- mirror damage: `MT+0x3ae = ST+5` (asm 0x4f63d6 from 0x6d6e90). FACT.
- "triggered" bit (local flag, OR-ed into the perceive call at 0x4f6c7a) when, with rooms linked:
  - target (player) group flag 1 and d < Group1Radius, same for group 2 and 3 (`0x6d73e0`/`0x6d7400`/`0x421800`,
    radii `0x531260/70/80`): this is the only "noise" sense the AI has (player shooting / running / peeking).
    FACT (lines 113-142, asm at 0x4f647c block).
  - d < GeneralPerceivingRadius (`0x6d72b0`) **and** not damaged (line 132 guarded by `MT+0x3ae == 0`). FACT.
  - own damage: any hit jumps straight to triggered (LAB_004f64a2, line 144). FACT.
- "seen" `MT+0x3ad`, recomputed once per 0.3 s scan (timer line 148-152, reload asm 0x4f6c69):
  in front — `dot(facing, to_player) > 0` only, no angle (asm 0x4f64f7..0x4f6518) — and d < own VisualPerceivingRadius
  (asm 0x4f651e..0x4f6538 via 0x6d7290), then an eye-height ray through the level (`0x64c910`, line 321) sets seen=1.
  FACT (Ghidra lines 153-335 + the asm cross-checks above). The "ShootingCone" of the x_data is used only by the shoot
  button, never here.
- 0x4f6c7a: `flag |= MT+0x3ad`, then `0x4f6df0(flag, dt)` (asm 0x4f6c8b..0x4f6c9a). Inside 0x4f6df0, flag != 0 and not
  yet perceived and timer <= 0: `ST+0x89 = 1` ("send OnActivate", 0x6d73b0 at asm 0x4f6e6c), `MT+0x3af = 1`, and
  `MT+0x3b0 = EnemyInterestTime` (line 334-340). FACT.
- The player side of the trigger is script/engine data, not AI code: group 1 = shooting, 2 = running/strafing, 3 = peeking
  (`ai_behaviour.md` 2.2, header comments in `3_ai_ranges.h`). Radii: 15/4/0 m (Mobster), 40/5/2 (Merc), 50/8/3 (Killer).
- Net effect at 1 m: a Merc's GeneralPerceivingRadius is 3 m and his VisualPerceivingRadius 60 m, so a player standing 1 m
  away in the same visible room is perceived within one frame — *provided the enemy is ticked and the rooms are linked*.

## 4. What does NOT wake an enemy
- Gunfire as sound: no sound-event radius for gunshots exists in AP/MT. A shot is heard only through the shooter's
  group-1 radius (40 m merc / 15 m mobster) and only with rooms linked. FACT for the code (§3), INFERENCE that no other
  audio hook exists (a whole-`.text` scan for readers of the sound system in AP found none, not re-run here).
- `P_AlertEnemies` (grenades/molotov, 0x7081f0 -> 0x64d980 -> `[entity+0x78]`): reaches some per-entity slot, but no path
  into MT was shown (`ai_open.md` §5). UNKNOWN.
- `C_SetIdle(n, flag)` 0x69c240: writes `ST+0xb2` (idle variant, 0x6d7440) and `ST+0xb6` (0x6d7460) and nothing else —
  no machine, no update flag (asm 0x69c240..0x69c276). FACT.
- Slot 44 "wake with radius" 0x4feac0: early-outs because ActivateOtherCharactersRadius is 0 for all shipped classes
  (`ai_open.md` §5). FACT.
- `nonreactive` in effect (`AP+0x57d`): AP.update sleeps the enemy every frame even while perceived —
  at **0x5004f8** `jne 0x500522` -> `0x6d73b0(ST,0)` then `0x4f7440(MT)` (clears seen/perceived/interest). FACT.

## 5. The three real handles a script or bot has
1. Being perceived (§3) — the only organic path.
2. `C_ForceUpdate(seconds)` 0x699d90 -> `f32(C+0x218)`; while > 0 the character ticks off-screen, so AP.update (and
   therefore perception) runs. FACT for the field and the handler; INFERENCE for "ticks off-screen" — it is the measured
   behaviour in `ai_behaviour.md` 2.5 and the only reason the field is read at 0x687c37/0x687d4b.
3. `ST+0xb1` "update required" (`0x6d7420`), a one-frame flag: 0x687ba0 reads it at 0x687bbd and forces `C+0x201 = 1`
   at 0x687c4a, so the character (and its AI) ticks once. FACT (asm 0x687ba0..0x687c4a).
   Full caller list of 0x6d7420: 0x4feb63 (slot 44 wake), 0x4fef89 (**inside AP::setStateMachine 0x4fee60**), 0x5007cc,
   0x687efb (the per-frame clear), 0x69b45c (C_GoTo), 0x69b93c (C_GoToAndShoot), 0x69b9a3 (C_GoToPlayer),
   0x69c0e3 (C_Patrol). FACT by scanning every `call rel32` in .text.

## 6. Script entry points (which machine, and whether they poke the update flag)
| script command | handler | what it does |
|---|---|---|
| `C_SetStateMachine(name)` | 0x69a4f0 | reads the name (msg+0x30 via 0x74e8e0) and calls 0x698bb0(C,name). 0x698bb0 returns at once when the character has no controller (`0x6917b0` at 0x698bd2, FACT), puts the name in the character's AI name list at `C+0x710` (0x7288d0/0x728e00, asm 0x698dd4..0x698de8) and calls one controller vtable method (asm 0x698e62 `call [ebp+0x6c]`). AP::setStateMachine 0x4fee60 frees the old SM, clones the named prototype (0x4fa950 -> 0x4fc830) and sets update-required (0x6d7420 at 0x4fef89). FACT. It never touches perception. |
| `C_RequestStateMachineChange(name)` | 0x69a7a0 | empty name -> 0x728cf0 (clear list), else 0x7288d0(C+0x710,name); then controller vfn +0x74 with the stored name. This is the machines' own sub-machine switch (`this->C_RequestStateMachineChange( MercCombat_CloseCombat )` in `ai_behaviour.md` 1.2). FACT (asm 0x69a7a0). |
| `C_GoTo(name,f)` | 0x69b270 | sets machine **"GoTo"** (global 0x8b6398, built from literal 0x8a1968 at init 0x727b1c), controller vfn +0x60(room,pos,matrix,f), then ST+0xb1=1 (0x69b45c). FACT |
| `C_GoToAndShoot(name,f)` | 0x69b750 | sets machine **"Retreat"** (global 0x8b63b0, built from 0x87c274 "Retreat" at init 0x727a3c) at 0x69b8fc..0x69b904, controller vfn +0x60 with the waypoint, then ST+0xb1=1 (0x69b93c). i.e. walk to the point while shooting. FACT |
| `C_GoToPlayer(f)` | 0x69b960 | machine **"GoToPlayer"** (0x8b6388 from 0x8a1970, init 0x727bfc), vfn +0x68(f), ST+0xb1=1 (0x69b9a3). FACT |
| `C_Patrol(a,b,c,f)` | 0x69bce0 | machine **"GoTo"**, three waypoints, ST+0xb1=1 (0x69c0e3). FACT |
| `C_SetIdle(n,flag)` | 0x69c240 | idle animation variant only (§4). FACT |
Machine names are also read by AP vt slot 39 `0x4fefa0`, which turns three literals into flags (`ai_behaviour.md` 2.3):
`nonreactive` -> AP+0x57d, `idleuntilenemyseen` -> AP+0x57e, `idleuntilhit` -> a third flag. Literal pool 0x87c5e0..0x87c640
("Idle", "Nonreactive", "IdleUntilEnemySeen", "IdleUntilHit", "StandAndIdle", "MeleeCombat", "MercCombat").
Correction to `ai_open.md` §5: the 0x69a4f0 -> 0x698bb0 -> "slot 38" line is not what the bytes show; the call at 0x698e62
is a +0x6c slot on the controller. Whether that lands in 0x4fee60 is UNKNOWN (§9).

## 7. merccombat, and why selecting it does not wake anybody
- Name literal "MercCombat" 0x87c634; top machine init 0x4da080 (asm: seven name strings then state builders 0x4fcba0 /
  0x4fcda0 and condition builder 0x4ed6a0). FACT for the shape; the decoded states are in `ai_behaviour.md` 1.3: state 0 =
  "NOT perceived -> idle" (keep = perceived), then seen && d < 6.4 / 9.6 / 19.2 / >= 100 -> script switch to
  MercCombat_CloseCombat / MidCombat / FarCombat / MaximumCombatDistance, default -> ApproachEnemy.
- So `merccombat` is a table that *consumes* perception (condition 4, `0x4f6290`). Assigning it to a sleeping enemy changes
  which state it sits in, but its seen/perceived flags stay 0 until §3 fires, and no action is ever pressed.
- Shooting itself (act 13, 0x4ff5b0) needs, besides an awake+perceived enemy: reaction elapsed (`AP+0x60e <= 0`), target
  inside `ShootingCone * 0.5 * pi/180` (`0x4f2fc0`), `seen now || AP+0x5fc <= 3.0 s`, own health > 0, and the burst timer
  `AP+0x597` expired (`ai_behaviour.md` 3.1). FACT (that note's asm + Ghidra).

## 8. Reading the part1_level6 report through this
- "enemy health stayed 20 while a clip was emptied at 1 m": HP is a different chain (`causeDamageFromHit` 0x695eb0 ->
  `causeDamage` 0x695b10); but a hit also sets ST+5, which is the damage trigger of §3, so an enemy that lost no health
  *and* did not react points at "the enemy is not being ticked" or "the shots never hit", not at the wake code.
- "Max health stayed 60 for 600 s with enemies near": candidates in order, all readable:
  1. `u8(C+0x201) == 0` for those enemies (not drawn, no ForceUpdate): AP.update never ran, so they cannot perceive. (§2)
  2. rooms not linked: after a script `C_Teleport` the enemy's room handle `u32(C+0x7e)` can still name the old room, and
     0x4f17b0 gates *both* the radius triggers and the damage trigger. INFERENCE (the field relationship is FACT in `ai.md`;
     the teleport case was not measured).
  3. `AP+0x57d` set (the enemy was handed `nonreactive`; part1_level6's templates do that to e1/e2 in the level file,
     `1_Part1_Level6.ldb`: `::teleport::e1->c_setstatemachine(nonreactive)`), which suppresses perception. FACT for the
     suppression, level data for the usage. `End_B01`'s own template (`::teleport::End_B01->c_pickupweapon(sawedshotgun)`
     + 8 shells) sets no nonreactive and no immortality, so on that one it is not the script.
- Practical wake for a bot that must not rely on being watched: `C_ForceUpdate(10)` on the enemy (handle 0x699d90) plus
  keeping Max inside the same room; then read `C+0x2dc` / `AP+0x506` to confirm.

## 9. UNKNOWN
- The controller object behind `C+0x2fe`: `getController` 0x6917b0 -> 0x751da0(C+0x2f9) returns whatever was stored; the
  vtable read as 0x7cfd40 with +0x6c = 0x401b50 (`ret 4`) cannot be the target of the 3-argument call at 0x698e62, so the
  pointer is probably a secondary base (0x7cfd2c's +0x6c = 0x4ff4c0 is a weapon/clip helper, also not a match). Reading of
  `setController` 0x691750 -> 0x7496b0 not finished. Until this is closed, "C_SetStateMachine -> AP::setStateMachine
  0x4fee60" is inference from intent, not from a call edge.
- Who reads `C+0x200` / `C+0x201` to actually skip `AP.update` (0x5001f0 is only called virtually); the effect is measured,
  the branch is not.
- `AP+0x57f` (a byte read at 0x500592) and `AP+0x595`-family flags: not read.
- Whether `P_AlertEnemies` (0x7081f0) can force perception; and what `[entity+0x78]` is.
- The exact meaning of condition code 5 (`0x4ffc7d`) and the third literal flag of 0x4fefa0 (`idleuntilhit` target byte).
- Not verified on a live run: every claim above is static. What a ghost should print first is `C+0x201`, `C+0x2dc`,
  `AP+0x506`, `AP+0x50b`, `u32(AP+0x5a3)` + `i32(SM+0x4f)` + the SM name.
