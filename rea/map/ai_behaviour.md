# Enemy AI behaviour (MaxPayne.exe 1.05, rebased VAs) — WORKLIST #8

Built on rea/map/ai.md (fields, X_CharacterProperties) and gameplay.md. Sources: Ghidra corpus rea/c/MaxPayne.exe/, capstone on
build/img/MaxPayne.exe, game data in build/data/x_data/ (from x_data.ras). Hopper was NOT run (see Open).
Conf: H = two independent sources agree. M = one source or inferred from code shape. L = guess.
All offsets hex. AP = MaxPayne_ArtificialPlayerInput* (the enemy controller, `new 0x612`, ctor 0x4fe6f0).
MT = AP+0x157 = MaxPayne_AIMovementTarget (perception + path object). C = X_Character*. ST = C+0x22e.

Correction to ai.md: the "AI brain object" at AP+0x157 is MaxPayne_AIMovementTarget (ctor 0x4f0b70 stores vtable 0x7cfb64 at +0;
RTTI vtable 0x7cfb64). It is the perception object, not the state machine. The state machine is a separate object (section 1).

## 0. The whole thing in 8 lines
1. The AI does not run code per behaviour. It runs small tables: ~70 hard-coded state machines (static initialisers 0x4d03e0..0x4eba60).
2. Each frame AP.update (0x5001f0) updates perception (MT), then runs the active machine (0x4fa280): pick a state, apply its action codes.
3. Action codes only press virtual buttons on AP (run/strafe/turn axes, shoot, crouch, jump, dodge). The normal character code does the rest.
4. Conditions are small expressions over 0x28 condition codes (seen, perceived, distance, timers, random pick, clip empty, can-dodge...).
5. Scripts choose the machine by name (`c_setstatemachine(mobstercombat)`); the machine switches itself between sub-machines by distance.
6. Perceive = (room visible) AND (player noise group radius OR general radius OR hit OR line of sight in front within visual radius).
7. "Awake" (ST+0xae AI active) = MT+0x3b4 timeout > 0; the timeout is refreshed while the enemy is perceived.
8. Fire = random interval between 1/(MaxFreq*mult) and 1/(DefaultFreq*mult); accuracy is plain projectile spread, no AI term found.

## 1. Classes, state machine, transitions

### 1.1 Classes (vtables in MaxPayne.exe.vtables.tsv)
| class | vtable | role |
|---|---|---|
| MaxPayne_ArtificialPlayerInput (AP) | 0x7cfd40 @+0 (49 slots = player input interface), 0x7cfd2c @+4 (AIConditionEvaluationInterface), 0x7cfcd4 @+8 (AIOutputInterface) | controller of an enemy; ctor 0x4fe6f0; update = slot 32 0x5001f0; dump = slot 31 0x502540 |
| MaxPayne_AIMovementTarget (MT) | 0x7cfb64 (+0x131 and +0x4 hold sub-vtables 0x7cfb70, 0x7cfbf4) | perception, path, target; ctor 0x4f0b70; dump 0x4f74e0 |
| MaxPayne_AIStateMachine (SM) | 0x7cfcac (9 slots: 1 reset-all 0x4fa180, 2 0x4fa1d0, 3 reset 0x4fa3c0, 4 update 0x4fa280, 6 dump 0x4fa4f0) | one machine = ordered list of states; 0x68 bytes each, static objects at 0x8aa360.. |
| MaxPayne_AIAction | 0x7cfb14 | list of action codes (vector<int> at +4); slot 4 apply 0x4ecec0, slot 6 dump 0x4ed150 ("%sAction codes:") |
| (condition) | no RTTI | vector of 14-byte terms at +4 (code i32, 2 bytes, 2 floats); evaluator 0x4ef140; builders 0x4ed6a0 (1 code), 0x4ed940 (code,float), 0x4edbe0 (code,f1,f2), 0x4ee130 (NOT), 0x4ee690/0x4eec50/0x4ee970 (binary) |
| DodgeTestSphere | 0x7cff0c | collision probe used by the can-dodge conditions |

Evidence for the class roles: RTTI names + vtable slot functions; dump strings 0x87d3e0.. ("State machine: %s", "Current state: %d"), 0x87d0cc.

### 1.2 State machine object (SM) and state (0x5c bytes) — H (Ghidra + capstone agree)
| item | offset | source |
|---|---|---|
| name (std::string; char* at +8) | SM+4 | dump 0x4fa4f0 reads [SM+8] |
| timer A / timer B | SM+0x14 / SM+0x31 | passed to the evaluator 0x4ef140 as the two time sources; 0x4fa300 resets B on a state change |
| current state index (i32) | SM+0x4f | 0x4fa280 `[esi+0x4f]`; reset 0x4fa3c0 sets it to count+1 = "not selected" |
| state vector begin / end | SM+0x57 / SM+0x5b; count = (end-begin)/0x5c | 0x4fa240; state i = 0x4fa220 |
| state: enter condition | state+0 | 0x421a50 returns [state+0] |
| state: action (AIAction*) | state+4 | enter/leave/tick through action vfns 0x4f8c40/0x4f8cd0/0x4f8ce0 |
| state: keep/free condition | state+8 | 0x748300 returns [state+8] |
| state: two script sub-objects (0x28 each) | state+0xc, state+0x34 | built by 0x4fcba0; text like `this->C_RequestStateMachineChange( MercCombat_CloseCombat );` (strings 0x87c23c..0x87cf20) |

Selection rule, from 0x4fa280 (H):
1. If a state is current, evaluate its keep condition (state+8). True: every state is a candidate. False: only indexes 0..cur-2 (earlier = higher priority) are candidates. (The `cur-1` bound is as compiled; not understood why not `cur`.)
2. Walk the candidates in order; the first whose enter condition (state+0) is true is chosen.
3. If it differs from the current one: leave old (0x4f8ce0, 0x4f8c40), enter new (0x4f8cd0), reset both timers (0x582e50), set the index. Then tick the chosen state's action.
So the last state of every machine is the default (enter = always). Priority = list order.

Machine registration: `0x4f8e30 / 4f9000 / 4f91e0 / 4f93d0 / 4f95d0 / 4f97e0 / 4f9a00 / 4f9bf0 / 4f9ef0` = constructor for 1..9 states into a global SM (name string first).
Source order of the states is the argument order (MSVC pushes right-to-left, so states are built last-first). Lookup by name + per-enemy instance: 0x4fa950; AP.setStateMachine = vt0 slot 38 0x4fee60 (stores the SM at AP+0x5a3 and the name string at AP+0x5a7).

### 1.3 Machines (decoded with a symbolic tracer over the static initialisers; M for the exact numbers, H for the structure)
Notation: state = (enter | action codes | keep). NOT/AND/OR on condition codes. d = distance to the enemy (condition 0x18/0x19 thresholds).

| machine (init fn) | states in priority order |
|---|---|
| Idle 0x4d8c30, Nonreactive 0x4d8df0, IdleUntilEnemySeen 0x4d8fb0 | 1 state: (always, no action). The difference is a flag (see 2.4), not the table |
| IdleUntilHit 0x4d9170 | (cond 22 "move request" -> action 0x1e + script) , default idle |
| StandAndIdle 0x4d94b0 | (cond 7 -> acts 0xb), (cond 8 -> 0xc), default none: turn toward the side the enemy is on |
| StandAndShootStatic 0x4ea500 | (NOT perceived: idle), (cond 7: turn left+shoot), (cond 8: turn right+shoot), default: shoot (13) |
| StandAndShoot 0x4ea8d0 | as above plus (perceived AND NOT seen: act 0x1c = go to player) |
| CrouchAndShoot(Static), CrouchWaitAndShoot(Static) 0x4d3280, 0x4d3800 | same with crouch (10); reload branch when clips empty (cond 12) |
| WalkAndShoot, WalkStopAndShoot, RunWalkAndShoot 0x4ead90, 0x4eba60, 0x4e8fc0 | shoot + walk (acts 2/3) toward / sideways, 3 variants each (turn left, turn right, straight) |
| SingleShootAccurately 0x4ea080 | (cond 9 -> turn left), (cond 10 -> turn right), (cond 0x21 counter>=1 -> script), default shoot |
| MobsterCombat 0x4e33a0 (top) | 0: NOT perceived -> idle (keep = perceived). 1..4: seen AND d<1.0 / d<4.0 / d<9.6 / d>=100 -> script switch to BackAway / CloseCombat / MidCombat / MaximumCombatDistance (order of strings; M). 5 default -> ApproachEnemy (+TurnToEnemy) |
| MercCombat 0x4da080 (top) | same shape, thresholds d<6.4 / 9.6 / 19.2 / d>=100 |
| MercCombatDefensive, MobsterCombatDefensive 0x4deca0, 0x4e6860 | same top shape with the Defensive sub-machines |
| *_CloseCombat / MidCombat / FarCombat / MaximumCombatDistance / BackAway / TurnToEnemy / ApproachEnemy | each = 1-3 sub-machines: strafe-and-shoot (acts 0xb/0xc + 6/9 + 13), dodge/jump (acts 0x14,0x15 under random cond 0x24 and can-dodge conds 0x11/0x12), a movement state with a script |
| DodgeLeft/Right/Forward/Backward 0x4d3e80.., ShootDodgeLeft.. 0x4d78f0.., Single*Dodge 0x4d4580.. | 1-3 states pressing dodge (acts 24-27) or jump combos (20-23) |
| Goto 0x4d03e0, GotoPlayer 0x4d08a0, Retreat 0x4d0be0 | move states (acts 0x1d = go to waypoint, 0x1c = go to player); Goto states have cond 0x17 / 5 |
| CombatTest* 0x4d0ff0..0x4d2ad0, MeleeCombat 0x4d97c0 | test machines / melee; MeleeCombat not decoded |
| Delay500ms, Delay1s, Delay2s, Crouch, Test, SingleJump | tiny timed machines (names at 0x87c1fc..0x87c5d8) |

Machines that scripts use in the shipped level files (grep in build/data/lvl/*.ldb): nonreactive 25, mobstercombat 15, idle 12, standandshootstatic 9, crouchandshootstatic 7, crouchandshoot 7, delay1s 3, merccombat 2, standandshoot 1, idleuntilenemyseen 1, delay2s 1.
Character files only set `singledodgeright` (enemy_max_payne, gognitti_vinnie) in an animation frame message.
Script movement commands: c_goto 15, c_gotoandshoot 19, c_gotoplayer 16, C_Patrol, C_SetIdle(n,true). Handlers: GoTo/GoToPlayer/GoToAndShoot/Patrol/SetIdle in gameplay.md.

### 1.4 Condition codes (code = operand; evaluator 0x4ef140; operand test 0x4ef7a0 = 1..0x24; operator test 0x4ef7c0 = 0x26..0x28)
Operators (M, from use): 0x26 NOT (one operand), 0x27 AND, 0x28 OR. Code 0x25 unused.
Simple codes 1..0x17 = AP vt+4 slot 3 0x4ffc40 (jump table 0x4ffe90). `MT` calls act on AP+0x157.

| code | meaning | proof |
|---|---|---|
| 1 / 2 | always true / always false | table entries 0x4ffe70 / 0x4ffe79 |
| 3 | enemy seen now (MT+0x3ad) | 0x4f6260; dump label "Enemy seen" 0x87d380 |
| 4 | enemy perceived (MT+0x3af) | 0x4f6290; dump label "Enemy perceived" |
| 5 | perceived AND some entry in the character's C+0x710 list is not type 0/1 and strcmp differs | 0x4ffc7d (M, not understood) |
| 6 | enemy distance test (0x4f2230, 1/sqrt trick) | M |
| 7 / 8 | enemy is to the left / right of the aim (rotated vector component, 0x4f2320 / 0x4f2420, arg 0) | M; used with turn actions 0xb / 0xc |
| 9 / 10 | enemy behind-left / behind-right style tests (0x4f2520, 0x4f25c0); SingleShootAccurately turns with them | M |
| 11 | 0x4f3240(0) another side test | L |
| 12 | all weapon clips empty (X_WeaponArsenal::clipsEmpty 0x729c20) | names TSV |
| 13 | current clip full (isClipFull 0x729c50) | names TSV |
| 14 | enemy is in the same room (room ids equal, 0x4f2200 via 0x642d40) | H (ai.md room getter) |
| 15 / 16 | ST+0xb0 "Enemy aiming" / ST+0xaf "Ally aiming" (0x6d7350 / 0x6d7370); meaning "someone is aiming at me" is M | names TSV |
| 17..20 | can dodge left/right/forward/back (cached DodgeTestSphere probes 0x501810, 0x5018d0, 0x501990, 0x501a60; TSV names dodge_probe_cached*) | M |
| 21 | reaction time elapsed (AP+0x60e <= 0, 0x4ff500) = the AI is allowed to act | H |
| 22 | one-frame "move request" pulse: byte AP+0x553, set by vt0 slot 42 0x4fecd0, cleared each frame at 0x5004e8 | H |
| 23 | byte AP+0x5fa (cleared by action 29) | M |
Parameter codes (AP vt+4 slot 2 0x4ffef0; 4 args: timerA, timerB, code, float f):
0x18 d < f; 0x19 d >= f (d = 0x4f3320, length of the vector to the enemy); 0x1a rand()/32767 <= f (probability f); 0x1b timerB >= f; 0x1c timerA >= f;
0x1d enemy inside cone f (0x4f2fc0(f,0)); 0x1e NOT that; 0x1f 0x4f3400() >= f (path length based); 0x20 0x4f3400() <= f; 0x21 counter AP+0x601 >= f; 0x22 counter <= f. (H structure, M names)
Random-pick codes (vt+4 slot 1 0x500060): 0x24 = ((AP+0x574 mod a)+1) == b where AP+0x574 is a per-enemy random seed (picks "slot b of a" so each enemy keeps a different style; used as 1-in-5, 1-in-10); 0x23 = float form of the same. (M)

### 1.5 Action codes (AIAction slot 4 0x4ecec0; jump table 0x4ed0c8; each calls AP output vfn, writes input fields read by input getters = vt0 slots) — H for fields, M for button names
| code | effect | field (AP+) |
|---|---|---|
| 1 | run forward (run axis +1.0) | 0x5dc f32 (input slot 0) |
| 2 | walk forward (+0.5) | same |
| 3 | backwards (-1.0) | same |
| 4 / 5 | forward / back with strafe +1 | 0x5dc, 0x5eb (slot 1) |
| 6 | strafe +1 | 0x5eb |
| 7 / 8 | forward / back with strafe -1 | |
| 9 | strafe -1 | |
| 10 | crouch | 0x5ea (slot 8) |
| 11 / 12 | turn / aim left (-1) / right (+1) | 0x5e0 f32 (slot 19); aim up/down is 0x5e4 (slot 18), not driven by a code |
| 13 | shoot (limit 3.0 s since last seen) | 0x5e8 (slots 2, 3) |
| 14 | shoot, tight (limit 0.5 s since last seen) | 0x5e8 |
| 15 | no-op (vfn 0x18 = nop 0x723bd0) | |
| 16 | button (probably reload; used with "clips empty") | 0x5e9 (slot 6) |
| 17 | jump | 0x5f7 (slot 7) |
| 20..23 | jump + strafe left / right / forward / back | 0x5ef..0x5f2 (slots 9..12) |
| 24..27 | dodge left / right / forward / back | 0x5f3..0x5f6 (slots 14..17) |
| 28 | "Go to player" flag = 1 (movement code walks to the player) | 0x5f8; dump label "Go to player: %d" 0x87d5f0 reads AP+0x5f8 (H) |
| 29 | "Go to waypoint" flag = 1, and 0x5fa = 0 (cond 23) | 0x5f9; dump label "Go to waypoint: %d" 0x87d5dc reads AP+0x5f9 (H) |
| 30 | writes the target pointer MT+0x3d8 (0x4f1eb0) | purpose open |
| 18, 19 | nothing | |
All output setters are gated by `AP+0x60e <= 0` (0x4ff500): while the reaction time runs, no button is pressed.

## 2. Perception

### 2.1 Where values come from (H)
Per-character file build/data/x_data/179_<name>.txt, block `[AI]` (parse table at 0x8817d0..0x881880, names at 0x881ac0..0x881bc4):
AimingSpeed, AimingSpeedCone, AimUpDownSpeed, TurnLeftRightSpeed, VisualPerceivingRadius, GeneralPerceivingRadius, ActivationReactionTime, ShootingCone,
PerceivingGroupOne/Two/ThreeRadius, EnemyInterestTime, ActivateOtherCharactersRadius. Values are `#define`s in build/data/x_data/3_ai_ranges.h:

| class | aim speed | speed cone | turn | visual | general | reaction | shoot cone | group1 | group2 | group3 | interest s | shoot freq |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Junkie | 8 | 25 | 80 | 20 | 2 | 0.0 | 170 | 15 | 4 | 0 | 10 | 0.8 |
| Mobster | 10 | 30 | 520 | 50 | 2 | 0.0 | 120 | 15 | 4 | 0 | 30 | 0.8 |
| Mercenary soldier | 10 | 30 | 720 | 60 | 3 | 0.0 | 100 | 40 | 5 | 2 | 30 | 0.4 |
| Killer suit | 16 | 15 | 720 | 70 | 5 | 0.0 | 10 | 50 | 8 | 3 | 30 | 0.7 |
| Boss | | | | | | | | | | | 360 | |
ActivateOtherCharactersRadius is 0 for all four classes. Header comments (designer): group1 = player shooting, group2 = running/strafing, group3 = peeking;
general = "if the player sneaks behind the AI, how near he can get"; visual = "how far the AI can see"; reaction = "how long the AI waits idle after activate";
shoot cone = "when aimed this close to the exact hit, he can start shooting".
Runtime copies: ST+0x8e aiming speed, +0x92 turn, +0x96 aim up/down, +0x9a speed cone, +0x9e visual, +0xa2 general, +0xa6 reaction, +0xaa shoot cone (ai.md; getters 0x6d7230..0x6d7310).
Group radii and interest time live in the character params object [C+0x718] (+0xd1 / +0xd5 / +0xd9 group 1/2/3, +0xdd interest; getters 0x531260/70/80/90).
Per-character shooting-frequency multiplier ("ShootingFrequencyMultiplier" in [Properties], e.g. 0.05 for henchman_01) -> ST+0x8a.

### 2.2 Sight / noise / hit test — MT update 0x4f62a0 (called by 0x4f6ea0 each frame; dt = 0x5e53d0)
1. d = distance to the target character (vector 0x4f18a0, then the 1/sqrt trick). vis = 0x4f17b0 (the rooms of the two characters are linked/visible; see ai.md room list).
2. MT+0x3ae = own ST+5 "getting damage" (0x6d6e90 at 0x4f63cf).
3. If vis, the enemy becomes "triggered" when any of:
   - target ST group1 flag (0x6d73e0) and d < group1 radius; or group2 flag (0x6d7400) and d < group2 radius; or group3 flag (0x421800) and d < group3 radius;
   - own ST+5 getting damage (any hit triggers);
   - d < GeneralPerceivingRadius (0x6d72b0), not checked while damaged flag logic above says otherwise (0x4f647c..0x4f64a0).
   The target's group flags are written by the player input update (0x742cf1.. setActivationGroup1/2/3): group1 = shooting, group2 = running/strafing, group3 = peeking (names from the header comment; M for the exact writer condition).
4. Visual scan: MT+0x3b8 (Visual scan time) counts down by dt; when it reaches 0, seen is recomputed (the scan is throttled, not every frame):
   in front of the AI (dot(facing, to-enemy) > 0, 0x4f64f7..0x4f6518), d <= VisualPerceivingRadius (0x6d7290 at 0x4f652b), then eye-to-eye ray tests through the level (0x43e060 builds the segment, 0x63bfb0 room test, 0x6542e0 / 0x64c910 line test) and a random factor using the target's crouching ratio (ST+0x74, 0x6d7200, `1 - ratio`, constants 0.9 / 0.6 at 0x7cfca0 / 0x7cfca4). Result writes MT+0x3ad (seen) and resets the scan timer (0x4f6c69).
   Exact cone angle: there is no angle in the sight test beyond "front half-space"; NOT proven that no other test exists in 0x4f6520..0x4f6c60 (about 3 KB read only for calls).
5. Hearing: there is no sound-event radius for guns. Gunshots are heard through the player's group1 radius (step 3). Grenade and molotov data send `this->P_AlertEnemies( 10 )` / `( 7.5 )` / `( 15.0 )` in frame 0 (63_grenade.txt, 63_molotov.txt) -> message X_ProjectileMessage_AlertEnemies (RTTI 0x89f268). The handler body and what it writes into MT was not read.

### 2.3 Perceived flag, interest time, active flag (H for fields, M for the exact sequence)
Per frame in AP.update (0x500481..0x500566):
- last-seen time AP+0x5fc: 0 while seen, else += dt.
- if perceived (MT+0x3af): and (AP+0x57d "nonreactive" set, or AP+0x57e "idle until enemy seen" set and NOT seen and NOT hit) -> ST+0x89 = 0 and clear perception (0x4f7440: seen, perceived, interest = 0). Otherwise if perceived: re-arm the activate timeout MT+0x3b4 = params+0xdd (0x4f7470).
- ST+0xae "AI active" = (MT+0x3b4 > 0) (0x4f74b0 then setter 0x6d7330 at 0x500566).
- Perceive entry 0x4f6df0 (flag, dt): decrements MT+0x3b4; when seen: if not yet perceived and timeout <= 0 -> ST+0x89 "send OnActivate" = 1; perceived = 1; interest MT+0x3b0 = params+0xdd. When not seen: interest -= dt; at <= 0 perceived = 0 (0x4f7470(0)). Callers of the forced form: AP vt0 slot 45 0x4fec90 (`push 0; push 1`); who calls slot 45 was not found.
- AP+0x57d / 0x57e are set by vt0 slot 39 0x4fefa0, by comparing the entries of the character list [C+0x710] with three global strings: "Nonreactive" (0x8b6368 -> 0x57d), "IdleUntilEnemySeen" (0x8b6358 -> 0x57e), "IdleUntilHit" (0x8b6338 -> third flag, target not confirmed). So those machine names are special.
- Reaction time: AP+0x60e is set to the ActivationReactionTime (6d72d0) at activation and counts down (0x500479..); no input is produced until it reaches 0. All shipped classes use 0.0.

### 2.4 How an asleep enemy wakes
Asleep = ST+0xae 0 (MT+0x3b4 <= 0, perceived 0). Wake paths:
1. Perception trigger in 2.2 (player in a group radius while doing that action, within general radius, line of sight in the visual radius, or the enemy was hit).
2. Forced perceive 0x4f6df0 via vt0 slot 45 (callers open) and `P_AlertEnemies` grenades (handler open).
3. Script: `c_setstatemachine(...)` (0x69a4f0 -> 0x698bb0 -> vt0 slot 38) and vt0 slot 44 0x4feac0 which also calls the AI-active setter (0x4feb4c).
Machines Nonreactive / IdleUntilEnemySeen / IdleUntilHit suppress path 1 as described (2.3).

### 2.5 Which characters think at all (character update gate; H for the runs, M for the code)
Runs 2026-11-05 (rea/map/fights_23.md "Why the three were open", traces in rea/fights23/trace_*.txt): an enemy that is not drawn and not forced keeps `AP+0x5fc` (last seen) and `MT+0x3b4` constant: no perception, no scan, no wake from `C_SetStateMachine`. H: Horst 6.2 m away in Max's room but behind him, frozen for 6 s; the same Horst ticks the frame Max's camera turns to him (act/seen/perc 1) or after `C_ForceUpdate(60)`. Also H: a perceived foe in another room with rooms not visible stays unseen (Aesir in room l1, gate 2 of 0x4f62a0).
Code (M, read not run): `0x687ba0` (per character, per frame, "Active characters at frame %i" debug legend `r render, a ai, p particle, t timer, n sniper, f force`) sets `char+0x201` (active this frame) when: the player (`ST+0xb1`, getter 0x6d7430; `0x4fee60` sets it, 0x4ff8f0/0x6d7420, when the machine is changed while already perceived), or the character was drawn last frame (`char+0x12a == DAT_008b3d70-1`, plus the render stamps of its two part lists, `0x6687d0`, `0x5911c0`), or the AI object's vt+0x18 says so, or `char+0x218 > 0` (ForceUpdate, handler 0x699d90, `C_ForceUpdate(seconds)`; reset 0x696680). `char+0x200` ("asleep") is raised after being inactive and cleared by a timer. NOT verified: who reads `+0x200/+0x201` to skip `AP.update` (0x5001f0, called virtually, the corpus has no direct caller) - the runs show the effect, not the branch.
Consequences: (1) scripted spawns use `c_forceupdate(10)` (P2L4 Joe `@0x15440b7`) so they walk to their post before Max sees them; (2) a test that moves a foe out of view tests nothing: use `@aim` or `C_ForceUpdate`; (3) (inference) the port reproduces this because the game's own lifted render code stamps `+0x12a`; no host code is involved, and enemies in view tick at once in every run.
Script facts the three open cases needed (levels are the source): Joe retreat block `1_Part2_Level4.ldb@0x162e17b` (nonreactive + goto gunroom joe_w1, fired by `::FOYER::joe_retreat_safetrigger`), `::gunroom::joe_timer` -> `merccombatdefensive` `@0x1529548`, invulnerable on at `@0x15442db`, off by `::gunroom::disable_invulnerability` `@0x1528f12`; Horst wake order `1_Part2_Level1.ldb@0x1bcc677..0x1bcc7ee` (mobstercombat, delay500ms, pickupweapon, SetInfiniteAmmo); Aesir `1_Part3_Level6.ldb@0x15f1c00` `::start::script` (initialize/start: goto e1w1/e2w1/e3w1, then crouchandshootstatic x2 + merccombat). Machine names set the AP flags exactly as 2.3 says (run joe4: merccombat and merccombatdefensive 0/0, idleuntilenemyseen -> +0x57e, nonreactive -> +0x57d; `AP+0x57d` also suppresses perception: a nonreactive foe stays at act 0).

## 3. Combat

### 3.1 Aim and shoot decision (H for the code facts)
- Turn/aim: the machine presses turn left/right (acts 11/12) from conditions 7/8; the character code rotates at ST aim/turn speeds (+0x8e..+0x96, 0x68ebd0 aim ray, 0x68df90 upper body). Aim speed applies inside AimingSpeedCone, TurnLeftRightSpeed outside (header comments).
- Shoot button (acts 13/14, 0x4ff5b0): set only if all hold: reaction elapsed (AP+0x60e <= 0); enemy inside the cone `0x4f2fc0(ShootingCone * 0x7cbdd4 * 0.01745)` (ST+0xaa 0x6d7310; 0x7cbdd4 not read, probably 0.5); seen now OR last seen <= 3.0 s (act 13) / 0.5 s (act 14); own health > 0; the flag at char+0x273 (0x4f5cb0) is 0.
- Fire gate (vt0 slot 3 0x4ff670): shoot button AND AP+0x596 ("randomized shooting time elapsed", 0x4ff840).
- Burst timing (0x500353..0x50042b; Ghidra 005001f0.c lines 103-131 agree):
  `AP+0x596 = 0` every frame; `AP+0x597 -= dt`; at <= 0: Tmin = 1/(MaximumShootingFrequency * mult), Tmax = 1/(DefaultShootingFrequency * mult) (0 if the product is 0), AP+0x597 = Tmin + rand/32767 * (Tmax - Tmin), AP+0x596 = 1 (this frame may fire).
  mult = ST+0x8a (0x6d72f0); weapon = AP+0x59b arsenal current weapon (0x7299e0). The weapon file gives Maximum = Default + 0.001 (weapons.md), so Tmin ~ Tmax and the randomness is small; the shot spacing is about 1/(freq*mult). **This closes the weapons.md open item "AI firing uses default frequency":** the AI uses both, interval drawn between the two.
- Accuracy: no AI-specific accuracy and no difficulty term in AP/MT code. A bullet gets the normal projectile spread (gameplay.md "Flow of a shot" step 5). Difficulty reaches enemies through EnemyHealthMultiplier only (0x676370 in getMaxHealth). NOT proven absent elsewhere.

### 3.2 Distance behaviour (MercCombat / MobsterCombat tops, 1.3)
Seen and closer than 1.0 (Mobster) -> BackAway; < 4.0 -> CloseCombat; < 9.6 -> MidCombat; >= 100 -> MaximumCombatDistance; otherwise ApproachEnemy.
Merc: 6.4 / 9.6 / 19.2 / 100. Not perceived -> idle state. The sub-machines then pick strafe-and-shoot, or the dodge branch.

### 3.3 Dodge / strafe / cover
- No cover system. No cover points, no "take cover" code; AIAnimation names "Crouch*" only crouch in place (act 10).
- Dodge = presses of the jump-combo / dodge buttons (acts 20..27) chosen in the CloseCombat/MidCombat sub-machines under: someone aiming at me (cond 15 or 16) AND random per-enemy pick (cond 0x24, e.g. 1 of 5) AND can-dodge probe true (cond 17..20), e.g. CombatTest_sub1 0x4d1900. CloseCombat Mobster: pick 1..10 / 6..10 and probes 0x11 / 0x12.
- Strafe while shooting: acts 6 / 9 with 0xb / 0xc / 0xd in the *_CloseCombat sub-machines.
- Reload: cond 12 "all clips empty" -> crouch (10) and act 16 until cond 13 "clip full" (CrouchWaitAndShoot* 0x4d3280).

### 3.4 Reaction to being hit
- Hit sets ST+5 (getting damage; set by the damage code, ai.md). MT+0x3ae mirrors it and forces the perceive trigger (2.2). IdleUntilEnemySeen and IdleUntilHit still react to a hit.
- The character itself plays its get-damage state (X_PlayerInputEvaluator GetDamage 0x765020: ids 0x2f / 0x30, gameplay.md). No AI table reacts to health thresholds.
- WoundedHealth / WoundedHealthUpperLimit in `[Properties]` (e.g. 25 / 15 for class 9) drive the wounded animation (ST+9), not read in AP/MT.
- Death: causeDamage 0x695b10 -> setHealth 0x695fb0 -> die 0x6963f0; AP is detached by the character code; the AI ignores dead characters (0x4f0fc0 via ST+7).

### 3.5 Grenades and molotovs
The AI has no throw action: action codes 1..30 contain no throw, and shipped enemy files do not give grenades to normal enemies (`PickupWeapon("grenade"|"molotov")` count 0 in 1_Part1_Level1.ldb; grenade/molotov mentions appear only in the Max Payne-clone files 179_enemy_max_payne*.txt etc.).
Level scripts can still make an enemy throw by script (C_PickupWeapon + shoot). NOT checked for other levels.

## 4. Runtime readout from the X_Character pointer C (two sources each)
| what | read | sources |
|---|---|---|
| **Awake / AI active** | `u8(C+0x2dc)` (= ST+0xae) | (1) getter 0x6d7340 + dump label " AI active" (ai.md); (2) setter 0x6d7330 `mov [ecx+0xae],al` called every frame from AP.update 0x500566. **H** |
| **AI state id** | there is no single id. Read machine name + state index: `AP = u32(C+0x2fe)` (check `u32(AP)==0x7cfd40`); `SM = u32(AP+0x5a3)`; `idx = i32(SM+0x4f)`; valid when `0 <= idx < (u32(SM+0x5b)-u32(SM+0x57))/0x5c`; name = `cstr(u32(AP+0x5ab))` (std::string at AP+0x5a7) or `cstr(u32(SM+8))`. Map (name, idx) with the tables in 1.3 (state order = priority order) | (1) dump 0x4fa4f0 prints "State machine: %s" from [SM+8] and "Current state: %d" from [SM+0x4f]; (2) evaluator 0x4fa280 compares/updates [SM+0x4f]; AP+0x5a3 written at 0x4fee8c, read by dump 0x502540 and update 0x50056b. **H** for idx, M for name offset |
| enemy controller from a character | `AP = u32(C+0x2fe)` | gameplay.md (X_CharacterInput at C+0x2f9, +5 = controller; getter 0x6917b0); reverse: `u32(AP+0x533)==C` (ai.md, 0x4f1ee0). Both sides agree on the pair. M (the pointer stored by setController is AP+0 in code shape only; verify with the vtable check above) |
| controller list (all enemies) | `GM+0x24c` vector of AP*; `C = u32(AP+0x533)` | ai.md (creation loop 0x450287..0x450a79) |
| target character | `u32(AP+0x52f)` (= MT+0x3d8) | 0x4f1eb0 setter, 0x4f2200/0x4f18a0 readers; ai.md says = player |
| enemy seen / hit / perceived | `u8(AP+0x504)` / `u8(AP+0x505)` / `u8(AP+0x506)` | dump 0x4f74e0 labels + getters 0x4f6260 / 0x4f6280 / 0x4f6290. **H** |
| interest time / activate timeout / scan time | `f32(AP+0x507)` / `f32(AP+0x50b)` / `f32(AP+0x50f)` | dump labels + 0x4f74b0, 0x4f6df0. H |
| reaction time left | `f32(AP+0x60e)` | dump "Current reaction time" 0x502657 + gate 0x4ff500. H |
| last seen time | `f32(AP+0x5fc)` | dump "Enemy last seen time" + shoot gate 0x4ff5f1. H |
| fire timer / may-fire this frame | `f32(AP+0x597)` / `u8(AP+0x596)` | dump labels "Randomized shooting time" + update 0x500353. H |
| buttons now | run `f32(AP+0x5dc)`, strafe `f32(AP+0x5eb)`, aim LR `f32(AP+0x5e0)`, shoot `u8(AP+0x5e8)`, crouch `u8(AP+0x5ea)`, jump `u8(AP+0x5f7)`, dodges `u8(AP+0x5f3..0x5f6)`, jump combos `u8(AP+0x5ef..0x5f2)` | input getters vt0 slots 0,1,19,2,8,7,14-17,9-12 (0x4ff370..0x4ff440) + output setters. H |
| path | `u8(AP+0x600)` path to enemy calculated; `u32(AP+0x157+0x3e8)` high-level length; `u32(AP+0x157+0x3f4)` low-level; `f32(AP+0x14e)` path blocked time; `f32(AP+0x5bb)` re-evaluate path timer | dump labels. H |
| flags | `u8(AP+0x57d)` nonreactive, `u8(AP+0x57e)` idle-until-seen, `u8(AP+0x595)` keep orientation to player, `u8(AP+0x5f8)` go to player, `u8(AP+0x5f9)` go to waypoint, `u8(AP+0x60d)` idling | 0x4fefa0, 0x4feaa0; dump labels 0x87d64c / 0x87d5f0 / 0x87d5dc / 0x87d66c (H); AP+0x57c, 0x57f open |
| Enemy aiming / ally aiming | `u8(C+0x2de)` / `u8(C+0x2dd)` | getters 0x6d7350 / 0x6d7370 + dump labels |
A ghost can also print the machine by name using the dump function 0x502540 (vt0 slot 31), which writes the same fields to the log.

## 5. Open (not proven)
- Hopper was not run for this map. Second keys were: Ghidra for the state machine, perception fields, shoot timing (agree); capstone for everything else. Hopper pass still wanted for 0x4f62a0 (sight) and 0x4ef140 (evaluator).
- Meaning of operators 0x26/0x27/0x28 by use, not by reading 0x4ef140 (it mixes 64-bit masks I did not decode). Condition codes 5, 6, 9, 10, 11, 17..20, 23 and parameter codes 0x1d..0x20 names are M or L.
- Which literal feeds which top-level state (the BackAway/CloseCombat/MidCombat order) — by string order only.
- Exact meaning of the compiled `cur-1` bound in the selection rule (1.2).
- Action codes 16 and 30; whether 16 is reload.
- Sight cone: only front half-space seen; eye ray details 0x4f6520..0x4f6c60 not read line by line; the crouching factor use is partial.
- Handler of X_ProjectileMessage_AlertEnemies (grenade noise) and the callers of AP vt0 slots 44 / 45 (script wake).
- ActivateOtherCharactersRadius consumer (0 in all shipped classes).
- Default machine of an enemy before any script (ctor 0x4fe6f0 calls 0x4fa950 with a string at 0x8a6334).
- Per-enemy state machine instances vs the static objects at 0x8aa360: 0x4fa950 appears to return a per-enemy object; allocation not read.
- Dodge probe semantics (DodgeTestSphere 0x7cff0c) and the third flag of 0x4fefa0.
- Whether x_data per-level files override [AI] values (only 179_*.txt and 3_ai_ranges.h read).
- Death chain after die (message lists at C+0x1b4 / C+0x1c4) is in the session notes, not re-read here.

### Reproduce
- Machines dump: symbolic tracer over the static initialisers (scratch, not committed). Command pattern: capstone over 0x4d03e0..0x4eba60, calls 0x4ed6a0 (condition), 0x4ec6f0 (action), 0x4f8a70 (state), 0x4f8e30..0x4f9ef0 (machine).
- Data: `.venv/bin/python tools/ras.py <game>/x_data.ras extract build/data/x_data` (already extracted: build/data/x_data/179_*.txt, 3_ai_ranges.h).
