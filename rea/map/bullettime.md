# Bullet time and shoot-dodge: what makes the feel (MaxPayne.exe)

Sources: `rea/c/MaxPayne.exe/*.c` (Ghidra corpus), `rea/map/names/MaxPayne.exe.tsv`, `rea/map/gameplay.md`,
`rea/map/player.md`, `rea/map/sound.md`, `build/data/x_data/1_globalconfig.txt`, `1_difficulty.txt`,
`179_max_payne.txt`, `3_characters.h`. Disassembly ranges and float constants were re-read in this session from
`build/img/MaxPayne.exe` (capstone + pefile). Tags: FACT / INFERENCE / UNKNOWN.

## 0. Correction to the prompt: 0x725be0 is not the slow-motion object

- FACT `0x725be0` = `X_CharacterAim::getPitch`, body `fld [ecx+0x20]; ret` (`rea/c/MaxPayne.exe/00725be0.c`,
  names tsv row 4055). In the bullet-time code it is only reused as the folded config getter `[cfg+0x20]`
  (HeartbeatSoundInterval) at `0x44cf36`.
- FACT The slow-motion state is not a separate object: it lives inside the GameMode object (GM).
  GM = `theApp+0xc8 -> +0x80` (`host/gamestate.c:375-383`). Fields: `+0x12f0` meter f32 0..1, `+0x12f4` active u8,
  `+0x12f8` heartbeat countdown f32, `+0x12fc` block u8, `+0x1258` button tracker, `+0x1300` activations,
  `+0x1304` shoot-dodges, `+0x10cc` config pointer (parser `0x50f8d0`).
- FACT Getters `0x44c090` (meter, `fld [ecx+0x12f0]`) and `0x44c0a0` (active); HUD wrapper `0x421ae0`.
- FACT GameMode ctor zeroes meter/active/block/heartbeat (`0x44f13e-0x44f156`); level start sets meter = 1.0 and
  both counters 0 (asm `0x44ff1a-0x44ff26`). Save/load keeps meter, active, heartbeat, block (`0x454fa0`).

## 1. Two clocks: game dt vs real dt

- FACT `X_TimeUpdate` layout (ctor `0x582df0`, `set` `0x582e60`, `advance` `0x583060`):
  `+4` scaled dt, `+8` scaled total, `+0xc` scale, `+0x10` real dt, `+0x14` real total, `+0x18` frames.
- FACT Folded getters used everywhere: `0x5e53d0` = `[+4]` scaled dt, `0x5830d0` = `[+8]`, `0x5830e0` = `[+0xc]`,
  `0x7269f0` = `[+0x10]` real dt. In the corpus 47 files reference the scaled getter, 16 the real one.
- FACT Movement and animation run on the scaled clock. `X_Character::update 0x687f20` reads `0x5e53d0` (dt) and
  `0x5830d0` (total) from the timer, builds the sub-step timer with `0x582df0(0,total,1.0)` and per step
  `0x582e60(avg,total,scale)`, and stops at 0.11 s accumulated (`0x7d5920` = 0.10999999). So a 0.2 scale really is
  one fifth of a frame of world time per frame.
- FACT Real dt is used only where the UI/audio must ignore the slowdown: meter drain `0x44cec8` and regen
  `0x44d02c`, heartbeat `0x44cf07`, speed interpolation `0x44e858/0x44e87e/0x44e8a4`, music fade `0x44cfc6/0x44d0cd`,
  mouse (`input.md`).
- INFERENCE The camera does not get a third clock: it reads the same timer, so it also slows (not measured).

## 2. Game speed: three slots, one min, one ramp

- FACT Three speed slots, each a message object with `+0x2f` target, `+0x33` duration, `+0x37` remaining, `+0x3b` start:
  Global `GM+0x1168` (handler `0x456150`), Game `GM+0x10e8` (`0x456000`), Controlled `GM+0x1128` (`0x4562b0`).
  Message `begin` `0x431510`: start = current scale (`0x44eba0`), remaining = duration (FLT_MAX = keep).
- FACT Interpolation `0x4312a0(realDt, target)`: `remaining -= realDt`; while `remaining >= 0` and `duration > 0`,
  `speed = (1-t)*target + t*start` with `t = remaining/duration`; else `target`. Linear in REAL time, so a 0.3 s
  ramp at 60 fps takes 18 frames of wall clock, not of game time.
- FACT Combine `0x44e845-0x44e929`: `min(Game, Controlled)`; use Global instead if `Global != 1.0` or any of the three
  mode tests (`0x421390` vslots `+8/+0xc/+0x10`) is true. Consequence: a shoot-dodge inside bullet time does not
  stack below 0.2, and a sniper bullet-cam (`Global 0.01`) wins over everything.
- FACT Apply `0x44ea00`: clamp arg to <= 1.0 (`0x7cbdd0`), `0x60dbf0` -> timer, `0x766c60` stores it at `timer+0xc`,
  then audio pitch (section 3), then the player (`0x691a60`).
- FACT Who sets what (data): bullet time on = `ChangeControlledGameSpeed(0.2, 0.3)`, off = `(1.0, 0.5)`
  (`1_globalconfig.txt:203,210`); shoot-dodge `ChangeGameSpeed(0.2,0.3)` / `(1.0,0.5)`
  (`179_max_payne.txt:385,386`); roll `(0.5,0.0)` / `(1.0,0.5)` (`:209,216`); sniper cam `Global (0.01,0)`.
- FACT There is no save/restore stack. "Restore" is always the ramp to the message's own value (1.0 over 0.5 s).
  UNKNOWN whether a level change hard-resets the three slots.

## 3. Sound pitch: the 3D world detunes, music does not

- FACT `0x44ea00` calls, for the player camera `GM+0x2e9` and the fly camera `GM+0xa1a`, the IAT slots
  `0x7cad48/0x7cad4c/0x7cad50` = `S_Listener::set2DSoundFrequency`, `set3DSoundFrequency`, `setMusicFrequency`
  (import names read from the PE in this session; bodies `0x10303d70/0x10303dc0/0x10303e10` in sndmfc.dll).
- FACT Argument: `pitch = min + (1 - min) * scale` (asm `0x44ea43-0x44ea5f` etc.), `min` from the config object
  `[GM+0x1230] +0/+4/+8` (`0x50c680`, `0x5e53d0`, `0x5830d0`) = `2D/3D/MusicMinimumPitchMultiplier` =
  1.0 / 0.15 / 1.0 (`1_globalconfig.txt:111-113`). Order matches the three calls, so the mapping is now FACT,
  not the INFERENCE in `gameplay.md:326`.
- FACT At scale 0.2: 3D pitch = 0.32, 2D = 1.0, music = 1.0. That 0.32 is the whole "underwater" feel of bullet time.
- FACT Downstream: `S_Sound::setFrequency 0x1030a7c0` (+0x128 factor, dirty flag) and the buffer push
  `0x1030ae20`, which multiplies `[sound+0x1a2]` by the listener factors and clamps to 6000..100000 Hz
  (`sound.md:109-110,178`).
- FACT Music loudness is separate: while active the listener music volume ramps to `MusicVolume` 0.0 at
  `1/MusicVolumeFadeTime` per second (0.5 s), and back to 1.0 when inactive (`0x44cf5d-0x44cff7`, `0x44d068-0x44d0ef`,
  `setMusicVolume` IAT `0x7cad58`).

## 4. Meter economy (all rates in real seconds)

- FACT Drain `0x44cec3-0x44ced6`: `meter -= realDt * (1/SlowMotionTime)`. `SlowMotionTime` = 15.0 s
  (`1_globalconfig.txt:188`), so a full bar = 15 s of wall clock = 3 s of world time at 0.2.
- FACT Regen `0x44cfff-0x44d04d`: `meter += realDt / RegenerationTime`, clamp 1.0. `RegenerationTime` = 1e8 s
  (`:189`), i.e. no regen over time in practice; the bar is fed by kills.
- FACT Kill reward: `0x687b00` (called on death, `0x695b10`) adds the victim's `SlowMotionBonusForKiller`
  (skin def `+0x69`, getter `0x53b8e0`, filled from global `0x8adc68`) to `0x8b4f5c`; `0x6879e0` moves it into the
  killer's `C+0x790`; `0x687b80` returns and clears it; the tick adds `bonus * difficulty` at
  `0x44cbd3-0x44cc0a` (`0x6763f0` = entry`+0x1c` = `BulletTimeGainedPerKillMultiplier`).
- FACT Values: 0.12 = `BONUS_SLOWMOTION` (`3_characters.h:24`) for enemies, 0.10 for the Max skin
  (`179_max_payne.txt:53`); multiplier FUGITIVE 1.3/1.0/0.7 (adaptive min/default/max, `1_difficulty.txt:36,46,56`),
  HARD-BOILED 1.0, DOA 2.0, NYM 1.1. So one kill is +10%..+24% of the bar = 1.5..3.6 s of bullet time.
- FACT Shoot-dodge cost: `meter -= ShootDodgePenalty` (10%, `:192`) only when bullet time is NOT active
  (asm `0x44ce29-0x44ce51`); floor 0. Manual off costs `TurnOffPenalty` (0%, `:191`, asm `0x44ebd5`). Auto-off at
  empty costs nothing.
- FACT Gates: to switch on, `meter > Threshold` (0.1%) else CantSwitchOn sound (`0x44ec01-0x44ec11`, `0x44ec40`);
  to dodge, `meter > ShootDodgeThreshold` (1%) written to `ST+0x6f` (`0x44cdd7-0x44ce0b`) and read by
  `DodgeSpecific::evaluate 0x76438d`.
- FACT Heartbeat: `GM+0x12f8 -= realDt`; below 0 send `HeartbeatSoundMessage` and reload `cfg+0x20` = 1.25 s
  (`0x44cf05-0x44cf3b`, data `:198-199`).
- FACT The whole block is skipped (meter frozen) when `GM+0x12ce != 0` or any mode test is true
  (`0x44cc17-0x44cc61`); block `+0x12fc` set plus the `BulletTimeAid` option on (`0x4554c0`, default 1) refuses the
  toggle (`0x44cd68-0x44cd79`).

## 5. Shoot-dodge, frame by frame

- FACT Input: the bullet-time key doubles as the dodge modifier. `GM+0x1258` = 0 idle, 1 held, 2 held+direction
  (`0x44cc76-0x44cd52`); a tap with no movement toggles bullet time.
- FACT Selection `DodgeSpecific::evaluate 0x764180`: shoot-dodge ids `0x1a..0x21` (8 directions, heading vs pi/4
  steps) when `ST+0x43` allows it and `ST+0x6f` is set, else plain roll `0x12..0x19`.
- FACT Timeline (`179_max_payne.txt:370-395`): f1 sound + `C_SetDodgeImmortal(true)`, f10 `Game=0.2/0.3s`,
  f12 `gm_enablebullettime(false)` + `block(true)`, f17 `C_Jump(4.5)`, f30 `Game=1.0/0.5s`, f49 immortal off.
  Root motion 5.25 m (forward/back/side), diagonals 3.5/3.25 m; get-up f0 `Game=1.0/0.5s` (`:424`), f20
  `block(false)` (`:425`). Roll: 0.5 at f0, 1.0 at f35. `CanShootDodgeFrame 13`, `CanAbortShootDodgeFrame 50`
  (`:47-48`).
- FACT Space probe `0x501810`: cached for 0.35 s of scaled time (`0x7cff00` = 0.34999999), tests the move to
  pos+3 m and to pos-2 m (`0x501b30`), so a dodge into a wall is refused.
- FACT Aim compensation `0x691a60` -> `0x691a80`: stores the scale at `C+0x494`, then
  `mult = base + (base/scale - base) * clamp(w,0,1)`, `base` = skin `ShootingFrequencyMultiplier` (def`+0x72`, 1.0),
  `w` = `wdef+0x65` (`0x53b8d0`). It writes `props+0x8a` (used as `1/(mult*MaximumShootingFrequency)` by
  `0x766950`), and multiplies the character input turn cap (`input+0x5b`, `0x7428a0/0x742890`) by `1/scale * mult`.
- INFERENCE With shipped data `wdef+0x65` is unset, so `mult` stays 1.0: the player's fire interval is NOT
  compensated, only the aim/turn cap is. That is the MP1 feel (you aim at full speed, you shoot at world speed).
  UNKNOWN whether any weapon script sets that field; the character-side `+0x65` is `HealthBonusForKiller`, so the
  name of the weapon-side slot is not settled.

## 6. Visuals: there is no blur and no tint

- FACT No post-process exists in the exe for this: no blur/tint/desaturate function in
  `rea/map/names/MaxPayne.exe.tsv` (0 hits), and `D3DXColorAdjustSaturation 0x10432264` has no callers.
- FACT The only screen effect is a HUD colour flash on entry: StartMessages send
  `mphm_flashtocolor(0x000000a0, 0.01, 0.03, 0.1)` = ARGB alpha 0xA0 black, 0.01 s in, 0.03 s hold, 0.1 s out
  (`1_globalconfig.txt:205`). Message class `0x4a98c0-0x4a9aa0`, handler `0x4b3af0` -> `0x4ac7f0`, which queues an
  effect in the HUD list `HUD+0x89`. The exit flash is commented out in the data (`:212-214`), so leaving bullet
  time is visually silent.
- INFERENCE The flash is a full-screen quad of that colour with an alpha ramp (drawn by the HUD; the draw routine
  was not read).
- FACT Everything else that sells the effect is audio plus time: 0.2 scale, 3D pitch 0.32, music muted over 0.5 s,
  heartbeat every 1.25 s, and 3 s of world time per bar.
- FACT No FOV change and no camera shake are tied to bullet time in `0x44ea00` or in the StartMessages.

## 7. What the port must reproduce (and how to check it)

1. On: Controlled 0.2 over 0.3 s real; Off: 1.0 over 0.5 s real; combine `min(Game,Controlled)` with the Global
   override; store the result at `timer+0xc` and nothing else.
2. Drain `realDt/15`, regen `realDt/1e8`, kill `+0.10..0.12 * difficulty`, dodge `-0.10` only outside bullet time,
   switch-on gate 0.1%, dodge gate 1%.
3. Audio: 3D pitch `0.15 + 0.85*scale`, 2D and music pitch 1.0, music volume -> 0 over 0.5 s, heartbeat 1.25 s.
4. HUD flash ARGB `0x000000A0`, in 0.01 / hold 0.03 / out 0.1, entry only.
5. Shoot-dodge: 5.25 m, jump 4.5 m/s at f17, Game 0.2 at f10 and 1.0 at f30, bullet time off + block at f12,
   block cleared at get-up f20.
- Already measured by the port: `host/gamestate.c:288` reads `gm+0x5b` (scaled dt), `+0x63` (scale), `+0x67`
  (real dt); `:383-384` read meter and active. A native run at one fight row gives those five numbers per frame;
  that is the cheapest proof that the two clocks and the ramp are right.
- Missing in the port: pitch (lives in sndmfc, `host/dsound.c` would need the listener factors), the HUD flash,
  and the aim-cap compensation in `0x691a80`.
- FACT (runtime, 2026-11-05, RN_SOUNDLOG SetFrequency log, web headless): 3D pitch follows the speed scale exactly: 0.32x at scale 0.2 (56 buffers), 0.83x at 0.8, 0.575x at 0.5 (a 0.5 plateau precedes bullet time in the p5 scenario; an earlier log capped at 200 showed only that plateau, which read as a bug). 0.24x = 0.32 x 0.75, a sound's own 0.75 pitch. No host bug.
- UNKNOWN: whether enemies can
  claim the kill bonus before the player (`0x6879e0` returns early for non-null controllers with vslot `+0x8c`
  true, which is INFERENCE for "enemies skip it").

## GATE50 g50-bt (2026-11-19)

- FACT host/dsound.c `mixer` holds `mx` across `mix(n)` for every active voice; the old catch-up cap was 4096 PCM frames. `SetFrequency` takes the same mutex.
- FACT the guest path remains 0x44ea00 -> sndmfc 0x1030ae20 -> DirectSound SetFrequency (section 3). No guest bytes or scale/pitch calculations changed.
- Candidate: cap each locked mix at 256 frames (5.8 ms of PCM at 44100 Hz), retaining the 2048-frame lead, Hermite resampling and cursor publication. PCM duration is NOT measured CPU lock duration.
- Prior art: miniaudio exposes PCM period size separately from total buffering: https://miniaud.io/docs/examples/custom_backend.html . Reuse the existing mixer; no additional audio engine is needed.
- FACT baseline preserved from the stopped task's completed unpatched 240 s run: out/g50-bt/before.ndjson (session 2026-11-19T06:34:02.277Z). Referee: bt_over33=1, low_seconds_pure_game=7, bt_share_pct=61.3.
- UNKNOWN whether baseline's 34 ms toggle frame was mutex contention. RN_SOUNDLOG now reports SetFrequency mutex high-water marks >=1 ms in the AFTER run.
- FACT AFTER on source commit 217698f: bt_over33 1 -> 0; low_seconds_pure_game 7 -> 0; bt_share_pct 61.3 -> 56.5 (92.17% retained, guard >=70%). Both telemetry files, referee output, final screenshots and exact command: out/g50-bt/.
- FACT no RN_SOUNDLOG SetFrequency mutex high-water report >=1000 us during AFTER; frequency logs exercise the pitch ramp. UNKNOWN baseline mutex duration and how much improvement is caused by lock batching rather than nondeterminism/JIT/shared-machine load.
- INFERENCE world slowdown is preserved: guest timer/scale/ramp code is unchanged. UNKNOWN independently timed world slowdown, audible output, Safari/native and other maps. Whole-session GATE50 still fails on load/comic/save frames outside this slice.
