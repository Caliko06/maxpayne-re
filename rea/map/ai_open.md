# Enemy AI: open items of ai_behaviour.md section 5 — answers

Sources: capstone on `build/img/MaxPayne.exe` (all addresses are VAs of that image), Ghidra corpus `rea/c/MaxPayne.exe/`,
game data `build/data/x_data/`. FACT = read in the bytes below; INFERENCE = follows from two facts but not proven line by line;
UNKNOWN = not established. Method note: map VA→file offset per PE section (`.text` raw 0x600 at VA 0x401000). Plain
`VA - 0x400000` reads the wrong bytes, and the `.data` tail past raw 0x4a38f7 is BSS, so globals there look like junk in the file.

## 1. Condition evaluator 0x4ef140: operators 0x26 / 0x27 / 0x28

FACT, `004ef140.c` lines 236-263: a term is 14 bytes (0xe): i32 code at +0, u8 `hasP1` at +4, u8 `hasP2` at +5, i32 p1 at +6,
i32 p2 at +10. For each term: code 1..0x24 -> operand; code 0x25 < c < 0x29 -> operator (tests `004ef7a0.c` / `004ef7c0.c`);
anything else is skipped without touching the stack (the `LAB_004ef727` path). So 0x25 and any code >= 0x29 are no-op terms.

Operand arity comes from the two flags, not from the code: `+4 == 0` -> `[vtbl+0xc](timerA, timerB, code)`;
`+4 != 0, +5 == 0` -> `[vtbl+8](…, code, p1)`; `+5 != 0` -> `[vtbl+4](…, code, p1, p2)`. For AP those slots are
0x4ffc40 (no param), 0x4ffef0 (one param), 0x500060 (two params) — matches `ai_behaviour.md` 1.4.

Values are bits, not ints. Each term pushes exactly one bit on an internal bit-vector stack: a value is the pair
(mask = `1 << bit`, word*) built by 0x4f01f0, and "true" is `(*word & mask) != 0`. Push = 0x4f02d0, end() = 0x4f02a0,
begin() = 0x4f0280. The evaluator returns bit 0 of that stack (`004ef140.c` lines 113-123: begin(), then 0x4f01f0, then
`(word & mask) != 0`), i.e. the term list is a balanced postfix program that must leave one value.

| op | arity | semantics | proof |
|---|---|---|---|
| 0x26 | 1 (unary) | NOT: `result = !((*word & mask) != 0)` | `004ef140.c` 131-151: one operand is converted (0x4f01f0), one test `(uVar2 & uVar7) == 0`, one push |
| 0x27 | 2 | AND: 0 if either side is false, else 1 | 152-190: two operands tested (`(uVar2 & uVar7) == 0` and `local_199`), `\|\|` of the two failures -> 0, else 1 |
| 0x28 | 2 | OR: 0 only if both sides are false | 191-228: same two tests, `&&` of the two failures -> 0, else 1 |

Both binary ops evaluate both operands (no short-circuit; the terms are already on the stack). NOT is worth noting because
the builders show the same: 0x4ee130 (NOT) takes one sub-condition, 0x4ee690 / 0x4eec50 (binary) take two.
Conf: H for the three operators (code + arity + truth table read in the bytes); the "bit 0" return is H by construction.

## 2. Action codes 16 and 30

The applier is `0x4ecec0` (this = the AIAction, arg1 = AIOutputInterface*). FACT: it walks the i32 vector at `[edi+8..0xc]`,
`code = v[ebx]`, `eax = code-1`, `cmp eax,0x1d / ja skip`, `jmp [eax*4 + 0x4ed0c8]` — 30 entries, one per code 1..30.

- **code 16** -> block `0x4ecff0`: `push 1; call [vt+0x1c]` = AIOutputInterface slot 7 (`+0x1c`), vtable 0x7cfcd4 -> **0x4ff690**,
  which is `if (AP+0x60e <= 0) AP[0x5e9] = arg` (reaction-time gate 0x4ff500). So 16 presses the byte at AP+0x5e9.
  What that byte is (FACT on the interface, INFERENCE on the name): AP+0x5e8/0x5e9/0x5ea are one three-button block — 0x5e8 is
  read by AP slot 2 = 0x4ff450 (fire), 0x5ea by slot 8 = 0x4ff3b0 (crouch), and the whole frame is tested as "no buttons"
  at 0x500af9/0x500b07/0x500b15. Input slot 6 (AP 0x4ff460, `MaxPayne_PlayerInput` 0x4ce910 -> 0x4cbe00) queries the action
  whose binding record is 0x8a91e3; the table at 0x4cb156 binds it to DIK 0x13 ('R') and 0x52 (NUMPAD0). So 0x5e9 = **reload**.
  Machines: 16 is used with cond 12 (all clips empty) and keep = NOT cond 13 (clip full) in `CrouchWaitAndShoot*` (0x4d3280,
  0x4d3800), never alone. Conf: H for the slot/field, M for "reload".
- **code 30** -> block `0x4ed0b3`: `push 1; call [vt+0x50]` = slot 20 -> **0x4ff820**:
  `0x4f1eb0(MT, [AP+0x554])` where 0x4f1eb0 writes `[MT+0x3d8] = arg` and a second copy `[MT+0x267] = arg`.
  [AP+0x554] is written by AP vt0 slot 42 **0x4fecd0**, which also raises the one-frame pulse `AP+0x553` (condition 22, "move
  request"). So 30 = **adopt the object of the last move request as the AI target** (MT+0x3d8 = the target character used by
  0x4f17b0/0x4f18a0), and nothing else. Conf: H. Which scripts pass which object into slot 42 is still open (see 5).
  Reader of MT+0x267: UNKNOWN.

## 3. Default machine of a new enemy

FACT, ctor 0x4fe6f0: at 0x4fe7dc it calls **0x7286d0**, which is `mov eax, 0x8b6378; ret` (a global std::string), and copies it
into the name string at AP+0x5a7; at 0x4fe8ed `ecx = AP+0x5a7; call 0x4fa950`, result stored at `AP+0x5a3`; then `AP+0x57c = 1`.
The global 0x8b6378 is built from the literal at **0x87c5e0 = "Idle"** by the static initializer at **0x727cb0** (copy loop
0x727d11..0x727d38). So a new enemy starts on machine **"Idle"** (one state, no action), not on a blank machine.
Same family (0x727d90/0x727e70/0x727f50/0x728110 build 0x8b6398, 0x8b6368, 0x8b6358, 0x8b6338 from "Nonreactive" 0x87c5e8,
"IdleUntilEnemySeen" 0x87c5f4, "IdleUntilHit" 0x87c608).

Correction to the map: the string is **not** at 0x8a6334. In this image 0x8a6330 holds `{0,0,0x4bb0d8,0xea8}` (a descriptor,
+4 = 0 is not a char*) and the code that touches 0x8a6330/0x8a6334 is 0x4055f1..0x407xxx (level/mode code), unrelated to AP.
Conf: H.
UNKNOWN: what 0x4fa950 returns when the name is not registered (its `operator<` fallback walks to the map header node, whose
+0x20 was not verified) — every shipped path uses a registered name.

## 4. Are state machines per-enemy instances?

**Yes, they are clones.** FACT, three links:

1. Registry: 0x4fa640/0x4fa780 returns the std::map at 0x8ab7a8 (ctor 0x4fb2b0, 0x4fc550 = lower_bound over the red-black tree).
   Keys are lowercased names. `FUN_004fab30(name, n)` creates the entry the first time: it builds the machine **once** with
   `operator_new(0x63)` + `0x4f9ef0(n)` and stores it at `[entry+0x20]`; `[entry+0x1c]` counts registrations. That object is the
   prototype (the static machines at 0x8aa360.. are the other kind of input to these ctors).
2. Lookup/clone: 0x4fa950 lowercases the name, finds the entry, then calls
   `[vtable of [entry+0x20]][5](0, &RTTI_AIActionOutputInterface 0x87d078, &RTTI_AIStateMachine 0x87d3b8, 0)` = slot 5 of the
   prototype = 0x4fa270 -> **0x4fc830**, which does `operator_new(0x63)` and copy-constructs `0x4f9ef0(prototype)`, then
   `__RTDynamicCast` to MaxPayne_AIStateMachine.
3. Replacement: `setStateMachine` 0x4fee60 (vt0 slot 38) frees the current instance first (`[AP+0x5a3]`, 0x4fee72 ->
   deleting dtor 0x4fab10), stores 0, then clones again (0x4fee83) and stores at AP+0x5a3. Scripts call it by name
   (handler 0x69a4f0 -> 0x698bb0 -> slot 38).

So each AP owns its SM at AP+0x5a3 (current index SM+0x4f, timers SM+0x14/+0x31); state and timers are never shared. The port
must clone per enemy too. Conf: H.

## 5. Wake path from scripts (AP vt0 slots 44 / 45)

- **slot 44 = 0x4feac0** (args: target). Order, FACT: (a) if arg != 0 it must equal `MT+0x3d8` (0x4f1ed0) else return;
  (b) return if `AP+0x57d` (nonreactive) is set; (c) `0x5312a0(params) > 0.0` else return — that is
  **ActivateOtherCharactersRadius** (params = `[C+0x718]`, field **+0xe1**: getters 0x531260/+0xd1, 0x531270/+0xd5,
  0x531280/+0xd9, 0x531290/+0xdd, 0x5312a0/+0xe1; parse record 0x881880 "ActivateOtherCharactersRadius" -> default 0x8ad6f8
  -> offset +0xe1 in the ctor copy at 0x530154; constant compared 0x7cb8b0 = 0.0);
  (d) `MT->perceive(1, 0)` (0x4f6df0); (e) `ST->setAIActive(1)` (0x4ff8f0 returns `[AP+0x59f]` = ST, 0x6d7330 writes ST+0xae);
  (f) `ST+0xb1 = 1` (0x6d7420, the "update required" byte); (g) sphere query 0x64cc30(radius 0x5312a0) and for every live
  (`ST+0x85 > 0`), non-active character in it: `AP'->vt0 slot 33` (0x4feaa0) = keepOrientatonToPlayer(target).
  Consequence, FACT: with radius 0 the function returns before doing anything. All four shipped classes define
  `AI_*_ACTIVATE_OTHER_CHARACTERS_RADIUS 0` (`3_ai_ranges.h:54, 95, 114, 131`) and 43 character files use the mobster define,
  so **slot 44 is a no-op on every shipped character** unless a level file overrides the [AI] value. This also answers the map's
  open item "ActivateOtherCharactersRadius consumer": 0x4feac0 is its only consumer.
- **slot 45 = 0x4fec90** (no args): `MT->perceive(1, 0)` and nothing else — no radius gate, no nonreactive gate, and it does not
  write ST+0xae. So it forces "seen/perceived + interest timer" on the current target without waking the machine directly.
  Whether the enemy then becomes active depends on AP.update's block at 0x500481-0x500566 re-arming MT+0x3b4 from the perceived
  flag (INFERENCE; that block is not re-read line by line here).
- **Callers, FACT.** Whole-`.text` scan for `call dword ptr [reg+0xb0]` / `[reg+0xb4]`: the only sites that hit AP are
  0x50361c and 0x503647, inside **0x503600** and **0x503630** — methods of `MaxPayne_LocalCharacterInput` (vtable 0x7cff9c,
  RTTI 0x87d738) at slots 33/34 (+0x84/+0x88) that forward to `[C+0x1b5]` = the AP (getter 0x503100). No direct call to
  0x4feac0 / 0x4fec90 exists, and no code calls LocalCharacterInput +0x84/+0x88 either (same scan), so the caller is the
  script/engine layer, not C++ in the image. **UNKNOWN**: the script command that maps to each forwarder (no name string in the
  exe and no call site).
- Grenade noise `P_AlertEnemies` (0x7081f0) does **not** use slot 44: it takes `[msg+0x30]` as a radius, calls 0x64d980, which
  fills a static sphere list (0x64cc30) and calls `[entity+0x78]` on each hit. Whether that reaches forced perceive: UNKNOWN.

## 6. What of section 5 is now closed

| item | state |
|---|---|
| operators 0x26/0x27/0x28 "by use" | CLOSED: NOT / AND / OR, arity 1/2/2, bit-stack result |
| action codes 16 and 30 | CLOSED: 16 = press AP+0x5e9 (reload, R/NUMPAD0); 30 = MT target := last move request object (AP+0x554) |
| default machine before any script | CLOSED: "Idle" (0x8b6378 built from 0x87c5e0) |
| per-enemy SM instances vs 0x8aa360 | CLOSED: prototypes in the map 0x8ab7a8, per-AP clone at 0x4fc830 |
| ActivateOtherCharactersRadius consumer | CLOSED: the early-out of 0x4feac0 (0 on all shipped classes) |
| callers of slots 44/45 | PARTIAL: only the LocalCharacterInput forwarders 0x503600/0x503630; script command name UNKNOWN |
| P_AlertEnemies handler | PARTIAL: sphere + `[entity+0x78]`; link to MT unproven |

## 7. Reproduce

```
# section-aware VA->offset, then capstone:
python3 - <<'E'   # in the port venv (capstone 5)
# off(va) = raw_ptr + (va - section_va); see the note at the top
E
# evaluator: rea/c/MaxPayne.exe/004ef140.c ; operators: 004ef7a0.c / 004ef7c0.c
# action table: dump 30 dwords at 0x4ed0c8 ; blocks 0x4ecff0 (16) and 0x4ed0b3 (30)
# registries: 0x4fab30 (create), 0x4fa950 (find+clone), 0x4fc830 (new+copy), 0x4fee60 (replace)
```
