# Mouse look consumer + X_Character movement (beto, read-only)

Sources: `rea/c/MaxPayne.exe/*.c` (Ghidra corpus), `rea/map/names/MaxPayne.exe.tsv`, `rea/map/MaxPayne.exe.vtables.tsv`,
`rea/map/input.md`, `rea/map/player.md`, `rea/map/gameplay.md`, `build/data/x_data/179_max_payne.txt`,
`build/data/x_data/161_default_skeleton.txt`, `build/data/x_data/36_levels.txt`. Constants in `0x7cxxxx`/`0x7dxxxx`
were re-read from `build/img/MaxPayne.exe` in this session (180.0, 0.017453292, +-1.5533431, 6.2831855, 9.424778,
0.11, 0.05, 4/pi, pi/4, 3pi/4, 0.35).

## 0. Two corrections to the prompt

1. Offsets are swapped: **+0x19 = yaw, +0x1d = pitch.** In `004cecd0.c` the store to +0x19 comes from vslot +0x4c
   (slot 19, `getAimLeftRightAxis` 0x4cead0 = mouse X) and +0x1d from vslot +0x48 (slot 18, `getAimUpDownAxis`
   0x4ceab0 = mouse Y). Confirmed by vtable 0x7cf974 slot 23 = 0x4cf2e0 `getYawDelta` (`fld [ecx+0x19]`) and
   slot 24 = 0x4cf2f0 `getPitchDelta` (`fld [ecx+0x1d]`), `names/MaxPayne.exe.tsv` lines 1240-1241.
2. There is no `004cefae.c`: Ghidra emits one file per function entry. 0x4cefae..0x4cefdd is inside
   **`MaxPayne_PlayerInput::update` 0x4cecd0**, file `004cecd0.c`.

## 1. Mouse look: full chain and every factor on it

FACT unless marked. "I" = `MaxPayne_PlayerInput` (GM+0x258), "Bridge" = `X_CharacterInput` at C+0x2f9.

| # | where | address / file | math, constant (value, address) |
|---|---|---|---|
| 1 | mouse poll | `00519070.c` `X_InputDeviceMouse::update` | stores the frame dt into obj+0x4d, GetDeviceState lX/lY |
| 2 | slider -> scale | `00519480.c` | `s = slider*1.4 + 0.1` (0x7d045c = 1.4, 0x7cb8b4 = 0.1); cite input.md:52 |
| 3 | smoothing | `00519370.c` | `out = acc*0.5` (0x7cbdd4 = 0.5), `acc -= out`; two-frame average |
| 4 | delta -> rate | `005195c0.c` | `(1/dt) * delta * 0.0025` (0x3b23d70a = 0.0025); Y negated if "Invert Mouse" obj+0x51 |
| 5 | PlayerInput::update | `004cecd0.c` (writes 0x4cefae..0x4cefdd) | `I+0x19 = aimX * 180.0(0x7cfb08) * dt * 0.017453292(0x7cfb04)`; `I+0x1d = aimY * 180.0 * dt * 0.017453292`; dt = `FUN_007269f0` = getter of +0x10 = **real** dt (0x7269f0, names.tsv 4057) |
| 5b | same, other branch | `004cecd0.c` lines 43-102 | runs when `I+0x2d != 0` (input locked): yaw = acos of the XZ dot of the stored target dir (0x6d6f10 = ST+0xf) and the body dir (ST+0x2e..0x36), pitch = asin(...) - ST+0xb (0x6d6ef0); both clamped to **+-3pi = 9.424778 (0x7cfb0c)** |
| 6 | getters (slots 23/24) | 0x4cf2e0 / 0x4cf2f0 (vtable 0x7cf974, offsets +0x5c/+0x60) | return `[I+0x19]` / `[I+0x1d]`; no scale |
| 7 | local wrapper slots 3/4 | `00503280.c` / `005032b0.c` (vtable 0x7cff9c, +0xc/+0x10) | forward to inner (I) +0x5c / +0x60; return 0.0 (0x7cb8b0) if inner == 0 (controls off) |
| 8 | Bridge update | `007429b0.c` (`X_CharacterInput::update`) | calls controller vslot +0x10 -> `setFloat_7e` `00742960.c` (pitch into Bridge+0x7e); vslot +0xc -> `setFloat_7a` `00742920.c` (yaw into Bridge+0x7a). No scale |
| 9a | yaw applied | `00687f20.c` lines 281-292 (`X_Character::update`) | `heading = ST+0x67 (0x6d7100) + Bridge+0x7a (0x742910)`; wrapped into [0,2pi) with **6.2831855 (0x7d5ce0)**; stored back 0x6d7110 (ST+0x67 = "Shoot dodge heading") |
| 9b | pitch applied | `00687f20.c` line ~167 + `00725b30.c` | `A.pitch += Bridge+0x7e` (0x742950); clamp to **+-1.5533431 rad = +-89.0 deg (0x7d8c08 / 0x7d8c04)**; aim vector `A+0x14 = (0, -sin p, cos p)`. Absolute copy into ST+0xb via 0x6d6f00 (line ~300), matrix into ST+0xf via 0x6d6f20 |
| 9c | yaw rate limit | `00743b10.c` (called at end of 0x7429b0) | body-turn value Bridge+0x82 approaches Bridge+0x7a by `scaled_dt(0x5e53d0) * accel(+0x96)`, clamped to +-max(+0x92), decays to 0 otherwise; +0x92/+0x96 set by 0x743e30 (caller spawnInit `00683f50.c` line 388) from the character params (TurnLeftRightSpeed = 90.0, 179_max_payne.txt:30) - INFERENCE on which two params |
| 10 | body matrix | `006911e0.c` `X_Character::updateBaseObjectTransform` (vtable 0x7d5950 sub-object C+0x1e slot 3) | rebuilds the P_BaseObject matrix from the animation matrix (0x74e8e0) plus a yaw angle = `getFloat_82 (00742930.c) * 0.017453292 (0x7cfb04)`, blended between C+0x776 and C+0x786 by weight C+0x796; result is `C+0x4e` (12 floats) |
| 11 | camera | `00504130.c` `MaxPayne_PlayerCamera` slot 34 | reads the CameraTarget slots +8/+0xc/+0x10/+0x14 = `C+0x49c`, `C+0x4a8`, `C+0x4c0` (0x693090/0x6930a0) and sets camera dir = `aimEnd - aimStart`; pose from the target matrix (slot 0 -> 0x74e8e0 = C+0x4e). **The camera never reads the mouse**; its yaw/pitch are the player's aim ray |

Net per DirectInput count (FACT, algebra of steps 2/3/4/5 with dt cancelling):
`rad/count = 0.0025 * pi * (slider*1.4 + 0.1)` => 6.283e-3 rad (0.36 deg) per count at slider 0.5, smoothed 0.5/frame.
Agrees with the live measurement 6.274e-3 rad/count and +200 counts = +1.2548 rad (`rea/map/player.md` lines 14-15).

Scaling answers:
- **Frame time:** no net term. The mouse stores its own dt (step 1) and step 4 divides by it; step 5 multiplies by the
  real frame dt. Both are the same per-frame interval, so the ratio is 1 (FACT for the algebra, INFERENCE that the input
  manager is fed the real dt; if it were fed the scaled dt, bullet time would multiply sensitivity by 1/speed).
- **Game speed / bullet time:** no factor in steps 2-9. 0x7269f0 is the real-dt getter (names.tsv 4057: "real dt on
  X_TimeUpdate"), not the scaled 0x5e53d0. Mouse look is therefore meant to stay at normal speed in bullet time.
  Not runtime-tested in bullet time (UNKNOWN).
- **FOV:** no factor. Sniper zoom changes the camera FOV through the zoom state machine (0x44c1b0 -> 0x452480) and the
  character flag C+0x758; the look path never reads FOV. Camera slot 6 (0x6930d0, isFOVMultiplierBelowOne) is only a
  branch in the camera update, not a look multiplier.
- **Resolution:** none. DirectInput counts are device counts; no screen size appears in steps 2-9.

## 2. X_Character movement

FACT unless marked. dt for the body is always the **scaled** dt 0x5e53d0 (bullet time slows movement/jumps);
real dt 0x7269f0 is only for meters, fades and mouse look.

| area | function | constants / data |
|---|---|---|
| per-frame root | `00687f20.c` `X_Character::update` (profile "Character") | substep cap 0.11 s (0x7d5920 = 0.11) |
| lower body + collisions | `0068c960.c` `X_Character::updateLowerBody` ("Char.LowerB") | (1) animation delta 0x744e20 added to anim velocity C+0x42a; (2) + `dt * motionVec(controller+0x86, 0x742980) * scale(controller+0x9a, 0x742970) * 0x531070`; (3) + last velocity C+0x41e * dt; (4) + free-fall 0x72c540; (5) collision; (6) velocity = displacement/dt |
| walk / run | no speed integrator: root motion only (`rea/map/gameplay.md` 359-362) | Max: run `PLAYER_MOVEMENT 4.2`, diagonal `PLAYER_DIAGONAL 3.0`, wounded `3.0 / 2.12`; **walk is reverted to run** `[Revert] From = CHARANIM_WALK; To = CHARANIM_RUN` (179_max_payne.txt 10-15, 875-884). Enemies: walk 1.5, run 3.0, strafe 2.5 (161_default_skeleton.txt 6-10). EndPosition is metres per animation cycle |
| state ids | State_Movement 0x765eb0 (ctor `00765c50.c`, vtable 0x7da024) | ids 1..9 run/strafe (thresholds 0.5 and 0), 0xa/0xb turn, 0xd crouch (`rea/map/gameplay.md` 217). No Ghidra file for the 0x765eb0 body: cite = gameplay.md |
| shoot dodge | `DodgeSpecific::evaluate` 0x764180 (vtable block 0x7d9f..) | ids 0x12..0x19 dodge, 0x1a..0x21 shoot-dodge, 0x22..0x25 get-up by heading quadrant; quadrant constants pi/4 = 0.7853982 and 3pi/4 = 2.3561945 (0x7d9f08 / 0x7d9f0c, gameplay.md 206) |
| dodge direction | `0068c850.c` `blendShootDodgeDirection` | sector = heading * 4/pi (0x7d5cf0 = 1.2732395), weight = 1 - frac, entries from ST+0x47 array (0x6d70e0) |
| dodge distance | data, not code | 5.25 m per shoot-dodge (179_max_payne.txt 359-396), 4.59451 m two-handed (lines 447-470); side/back 3.5, diag 3.25/3.37 |
| dodge space test | `00501810.c`, `00501b30.c` | `dodge_probe_space` tests +3.0 m (0x40400000) then -2.0 m (0x7cff04), result cached 0.35 s (0x7cff00) |
| jump | `00699820.c` `C_Jump` handler; state 0x7651c0 | `v -= speed * normalize(gravity)` (speed from 0x74cab0), then `setGroundSnapLockout(0.5)` (0x3f000000, `00695840.c`); speeds from data: PLAYER_SHOOTDODGEJUMP 4.5, PLAYER_JUMPSIDE/BACKWARD 3.0, ENEMY_JUMP 7.5 |
| free fall / gravity | `0072c540.c` `X_FreeFall::step` | `out += v*dt + 0.5*a*dt^2` (0x7cbdd4 = 0.5), then `v += a*dt`; gravity (0,-20,0) from 36_levels.txt parsed by 0x525490/0x525860 (link to C+0x404 = INFERENCE); AirborneSpeed 2.5 m/s (data) |
| stairs / step-up | **no dedicated function** | 0x68c960 only calls `snapToGround` 0x647df0 when not airborne and C+0x228 <= 0; max drop 0.05 (0x7d5924), drop = min(0.05, |disp| * tan(60 deg)), surface accepted when cos(46 deg) < dot(up, normal), statics 0x891b7c/0x891b80 set by GM ctor 0x44f190 (gameplay.md 375). INFERENCE: stair climbing relies on the capsule sweep + this snap; the corpus has no step-height constant |
| collision push | `0068c960.c` section 5, `006954f0.c` collectContacts, `00695070.c` dispatchContacts | sweep 0x647da0 into contacts 0x8b4c10 (records 0x28 B, normal +4, object +0x24); blocked test 0x6480a0, retry 0x64af10, moveTo 0x647660, testMove 0x64c910; character-vs-character 0x695490 / 0x695390; room box 0x63a330 |
| character capsule | data | CapsuleTop 1.95, Bottom 0.0, Radius 0.48 (179_max_payne.txt 45-47) |

## 3. Open / not proven here

- Which value becomes the rendered body yaw: 0x6911e0 blends C+0x776 and C+0x786 (weight C+0x796), but no writer of
  those two fields exists in the corpus (only save 0x692890 and the load); the yaw -> matrix step is INFERENCE.
  Runtime evidence that the body does turn with the mouse: player.md 12-15.
- 0x765eb0 (Movement evaluate) is not in `rea/c` (Ghidra gap 0x765e70..0x766000): its ids/thresholds come from
  gameplay.md, one source only.
- No bullet-time mouse-look measurement. Step 6 assumption (input manager dt = real dt) is not proven in code.
