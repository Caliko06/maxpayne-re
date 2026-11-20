# Script messages and trigger activation (MaxPayne.exe 1.05, rebased VAs)

Sources: Ghidra corpus `rea/c/MaxPayne.exe/*.c` + capstone 5 over `build/img/MaxPayne.exe`
(base 0x400000, PE section table for offsets); prior notes `rea/map/{gameplay,hits_deaths,ai,player}.md`
(not repeated here except where the new read corrects them). FACT = two sources agree or the body was
read twice by different means; INFERENCE = one source or by use.

## 0. How a message carries its arguments
- Arguments sit in fixed slots of the message object: float at `msg+0x30` (getter 0x74d340),
  int at `msg+0x40` (getter 0x74bf40), bytes via 0x74e5b0, string object via 0x74e8e0.
  FACT (each getter is a 3-instruction body; the `%f`/`%d`/`%s` formats are the table in gameplay.md:140-230).
- No argument validation: a missing argument reads whatever the slot holds (0 for a fresh message).
  INFERENCE (no test in any handler).

## 1. C_SetHealth (0x699ad0) - the argument is a FRACTION, not health points
FACT, whole body read (asm):
```
max = getMaxHealth(C)                       /* 0x691bd0 */
hp  = msg+0x30 (%f) * max
setHealth(C, hp, /*mode*/0, /*flag*/1)      /* 0x695fb0 */
```
- So `c_sethealth(1)` sets hp = maxHealth exactly, `c_sethealth(0.5)` = half, `>1` is clamped to max,
  `<0` gives 0 (death). FACT.
- `getMaxHealth 0x691bd0` = int `props+0x5d` (class base health) `* EnemyHealthMultiplier` (0x676370)
  when the character is AI-controlled, else `* PlayerHealthMultiplier` (0x676390). FACT.
  For Max that is 60 x PlayerHealthMultiplier, enemies `HEALTH_CLASS_n * EnemyHealthMultiplier`
  (gameplay.md:266; Fugitive EnemyHealth 0.25..1.2 / PlayerHealth 0.4..2.0, difficulty.md).
  **Consequence: the hp a script sees is not a constant 60; it is the difficulty-scaled max.**
- `setHealth 0x695fb0(C, float v, mode, char flag)`:
  - **If `C+0x456 != 0` the health write is skipped entirely** (the whole clamp block is bypassed and the
    function returns before 0x6d71a0 writes `props+0x85`). FACT (asm: `cmp byte [C+0x456]; jne end`).
    `C+0x456` is written only by **C_SetInvulnerable 0x69a840**. FACT (only writer in the corpus).
    Same gate blocks `C_CauseDamage`, explosions and bullets, because all of them end in this function.
  - `v > max` -> max; `v < 0` -> 0. FACT.
  - if the target is alive (`0x6d6eb0` reads `props+7` = dying/dead is 0) and `v < 0.1`
    (`_DAT_007cb8b4` = 0.1) -> `v = 0.1`: a living character is never left below 0.1 hp. FACT.
  - C_SetHealth passes flag=1, whose only effect is clearing `C+0x455` (dodge-immortal) when v == 0. FACT.
  - mode 0 -> `pickDeathType(0)` (0x696290) is called whenever the target is not yet dying; it only picks a
    random death animation id, harmless. FACT (body) / INFERENCE (harmless).
  - Writing 0 does call `die` (0x6963f0) inside setHealth. FACT.
- **Why `c_sethealth(1)` can leave hp at 20/60**: either the target had `C+0x456` set
  (any earlier `C_SetInvulnerable(true)`, which several injection recipes in
  `injection_part1.tsv` do to the player and one to enemies), or the message never reached the object
  (name not resolved). INFERENCE, both fit the code; the check is to dump `C+0x456` before the call.
- `C_SetImmortal 0x69a800` writes `C+0x454`, `C_SetDodgeImmortal 0x69a820` writes `C+0x455`; neither
  blocks damage in this function. FACT (asm); their flags only feed the "living hp floor" branch above
  and the red damage flash (hits_deaths.md:19). INFERENCE for the flash link.

## 2. C_AddHealth (0x699c10) - absolute points, not a fraction
FACT (body: `0x74d340` -> `heal 0x695a80`):
- `heal(C, n)`: `add = min(n, maxHealth * m)` where `m = [difficulty+0xc]` via 0x6763e0 (multiplier,
  value 1.0 on the default tables); the rest, `n - add`, is added to `C+0x72c` (carry). FACT.
- It only applies **if current hp > 0** (`cmp 0, hp; jle skip`) - a heal cannot revive a corpse. FACT.
- If hp stays > 0 it calls `setHealth(hp + add, 0, 0)` (flag 0, so no dodge-immortal clear). FACT.
- No clamp against `n < 0`: a negative argument damages through the same path. INFERENCE (no sign test).
- Uses the same `C+0x456` gate as section 1 (it ends in setHealth). FACT.

## 3. C_PickupWeapon (0x699120)
FACT (body read):
- `arsenal = 0x6916e0(C)`, name = `%s` arg (0x74e8e0), weapon = arsenal lookup 0x729f60(name).
- `owned = 0x748310(weapon)` is queried *before* the write, then `0x748370(weapon, 1)` marks it owned.
- If the character has that weapon (`0x6958a0`) and has a controller and it was not owned before,
  `controller->vfn+0x4c(weapon)` makes the AI equip it. INFERENCE for the slot name (by use).
- **It adds no ammo**: there is no write to a clip or reserve in the body. FACT (matches gameplay.md:236).
  A pickup that "works" for the bot needs C_PickupAmmo after it, or the clip stays empty.

## 4. C_PickupAmmo (0x699570)
FACT (asm, matches gameplay.md:292):
- name = `%s` arg (0x74e8e0; empty -> the default at 0x7caa2c, resolved at runtime), `n = %d` arg (0x74bf40).
- `count = round_to_int(n * AmmoFoundMultiplier)` (`[difficulty+4]` via 0x676380), and if `n > 0` and
  `count < 1` then `count = 1`: a positive pickup is never rounded to zero.
- `0x729bb0(arsenal, weapon, count)` adds it; that callee clamps to `PocketSize - loaded` (gameplay.md:292,
  not re-read here).
- Then, if the character has the weapon, has a controller and the weapon was not already owned,
  `controller->vfn+0x4c(weapon)` equips it. INFERENCE (same pattern as section 3).

## 5. Triggers: types, latches, and the two activation paths
- Type id = `[[t+0x46a]+0x9c]` (getter 0x71f070). 0x71ecc0 builds the type label from that id:
  0 -> `"(Action Button)"` 0x8a0fc4, 1 -> `"(Player Collision)"` 0x8a0fd4,
  2 -> `"(Projectile Collision)"` 0x8a0fe8. FACT (strings selected by the id in that body).
  Ids 3 and 4 exist (0x71f080, 0x694a40 table) but no label was found: UNKNOWN.
- Latches: enabled `t+0x48e` (1 at construction, 0x71ecc0:14), fired `t+0x48f` (0 at construction).
  `T_Enable 0x71f540` sets `t+0x48e` from the arg and clears `t+0x48f`. FACT (asm).
  Reset/load hook 0x71f5b0 clears `t+0x48f` again: a saved game can fire the same trigger again.
  INFERENCE (vtable 0x7d88a0 slot 6).
- **Script `trigger.TRIGGER->T_Activate()` -> 0x71f4f0** (registered by 0x71f640):
  if `t+0x48f != 0` -> return immediately; else build the `T_Activate` message (name 0x8a0fb8, static
  object 0x8b61d0) with the call's `%f` argument as the activator, dispatch it with 0x6e2cd0 on the
  trigger, and set `t+0x48f = 1`. FACT (asm).
  **It never reads the player, the position, the distance or `t+0x48e`**: a disabled trigger still fires
  from script, and one T_Activate per enable is the ceiling. FACT (asm of the whole body).
- **Real activation -> 0x71f0d0** (used by both the Use key and a player-collision touch):
  `if (t+0x48e == 0) return; if (t+0x48f != 0) return;` then the same message object 0x8b61d0 is built
  with the collider/activator as argument, dispatched with 0x6e2cd0, and `t+0x48f = 1`. FACT (asm).
  **So T_Activate and a real touch run the same receiver-side event**; the differences are the activator
  argument and the guards (enabled + volume vs none). FACT.
- Volume test 0x71f120 (vtable 0x7d88a0 slot 14) per character: filter 0x694a40, then a box test
  (0x64ab30) against the trigger matrix; inside -> 0x6946e0 (add to the character's tracked set),
  outside -> 0x694890 (remove). FACT (bodies).
- Filter 0x694a40(char, t): disabled trigger -> reject; type 0 and 3 -> accept any character;
  type 1 and 4 -> only if `char+0x7c0` is set; type 2 -> reject (projectiles have their own path).
  FACT (jump table at 0x694a80). `char+0x7c0` is written by 0x694ab0 only from the player attach/detach
  paths 0x451e70 and 0x452410: it is the "this character is the player" flag. INFERENCE (strong).
  **A Player Collision trigger therefore only fires for Max, not for a teleported bot enemy.** FACT
  (filter) + INFERENCE (flag meaning).
- After inserting, 0x6946e0 calls 0x71f0d0 only when 0x71f080 is true, i.e. type 1, 3 or 4: walking
  into an Action Button does nothing. FACT (asm). A Player Collision trigger fires once on entry and
  not again until T_Enable / load resets `t+0x48f`. FACT.

## 6. The Use key and Action Button triggers
`C_Use 0x699d60` -> `findUseTrigger 0x6966a0` -> `0x71f0d0(trigger)`. FACT (bodies: `if (find != 0) fire`).
`findUseTrigger` (0x6966a0) is the whole condition set, FACT unless marked:
- Preconditions: `0x642a80(C)` true (character active/controlled) and `C+0x758 == 0`. INFERENCE for the
  meaning of both flags.
- It scans a per-character container at `C+0x7d0` (nodes carry the trigger at node+0xc). The container is
  filled by the volume test above, **so Max must already be inside the trigger box**: there is no
  search radius of its own. INFERENCE (strong: same node layout, filled by 0x6946e0).
- Keeps only type **0** (`0x71f070() == 0`) that are **enabled** (`0x71f0b0` = `t+0x48e`). FACT.
- Facing: angle between the character's forward (0x63e070 through the world matrix 0x74e8e0) and the
  direction to the trigger must be `< t_use_angle`, where the per-character angle `C+0x7bc` is in degrees
  (multiplied by 0.0174533 /* 0x7cfb04 */) and the code warns outside [4,180] (0x7cce6c / 0x7cfb08).
  The smallest angle wins. FACT.
- A segment test 0x5ba000 (segment-sphere, gameplay.md:390) also runs along that direction with a reach of
  two floats read by 0x658b50; no hit -> the trigger is skipped. FACT (call); the reach values are UNKNOWN.
- Same floor is never compared; height enters only through the box test and that segment. FACT (no
  y-test in the body) / INFERENCE (consequence).
- Result: `C_Use` fires at most the single best trigger, once, then `t+0x48f` blocks it.
**For a bot**: `c_use` is fragile (needs to be inside the box, facing it within `C+0x7bc`, and unarmed of
the latch); `trigger.TRIGGER->T_Activate()` skips all of it and runs the same event. Prefer T_Activate,
and call `T_Enable(true)` again if the same trigger must fire twice.

## 7. Reading the two reported symptoms
- Enemy hp 20/60 after a full clip at 1 m: a bullet only reaches health through
  0x695eb0 -> 0x695b10 -> 0x695fb0, so `C+0x456` (SetInvulnerable) makes every hit a no-op, and so does a
  projectile name/aim miss. The static code cannot tell which; dump `C+0x456` on the enemy.
  INFERENCE (two candidate causes, one check).
- Max pinned at 60 for 600 s: 60 is the full bar, so nothing ever damaged him; the only passive heal is
  the game-mode global 0x8b4f54 consumed in 0x6879e0 (gameplay.md:201) - the character itself never
  regenerates. INFERENCE.

## UNKNOWN
- Whether a message whose recipient name does not resolve fails silently (not read).
- Meaning of trigger type ids 3 and 4; no label string was located.
- The reach floats read by 0x658b50 inside findUseTrigger, and what object ecx is there.
- What receives the T_Activate message: 0x6e2cd0 builds/dispatches through the trigger's argument store
  `t+0x365`, but the FSM-side handler body was not read in this pass.
- Whether `C+0x72c` (heal carry) is spent anywhere.
- Whether `t+0x48f` is cleared by anything other than T_Enable and the reset hook 0x71f5b0.
