# Mechanics: health, damage, painkillers, bullet time, enemy damage, sniper zoom (MaxPayne.exe 1.05)

For WORKLIST #7/#9/#10. Table to fill web vs native: `census/systems_table.tsv` (50 rows; columns web/native empty).
Sources: Ghidra corpus rea/c/MaxPayne.exe (read here), capstone over build/img/MaxPayne.exe (independent), the OWNER's game data
(build/data/x_data), host/gamestate.c, names TSV (earlier Ghidra+Hopper+disasm notes). Nothing was run in a game.
Hopper: opened the exe (search_strings works) but `procedure_pseudo_code/procedure_info` answer "No procedure exists" at every exe
address tried (0x695a80, 0x695a90, sub_695a80): no Hopper body was read this session. Second key = capstone. Rows marked 2src
say which two. 1src = one source. data = game data only. UNPROVEN = no code read.

Notation: `C` = [[GM+0x1080]+8] player X_Character; `GM` = [[[0x8a6338]+0xc8]+0x80]; `ST` = C+0x22e; `WS` = [C+0x398];
`D` = [C+0x490] difficulty object; `E` = [D+0x20] + 32*u32[D+0xf5] (difficulty entry); `P` = D+{5,0x41,0x7d,0xb9} chosen by
u32[D+0xf9] = 1..4 (normal, nightmare, hell, timed) (0x6760a0/0x6760e0, capstone+Ghidra); `CFG` = u32[GM+0x10cc] (slow-motion
config, parser 0x50f8d0); timer `TS` = GM+0x57 (0x60dbf0).

## 1. Health and damage to the player
- hp = f32[C+0x2b3]. Max is not stored: `base x mult`, base = i32[[C+0x718]+0x5d] (60 for Max), mult = f32[E+8]
  PlayerHealthMultiplier, or f32[E+0] EnemyHealthMultiplier when the controller [C+0x2fe] vslot +0x8c is true (0x691bd0).
  Native New Game reads hp 60 = 60 x 1.0, so the player takes the E+8 branch and a New Game uses the "default" entry
  (PlayerHP 1.0, EnemyHP 0.8, Regen 4%, Immediate 5%, BulletTimeKill 1.0). Which index it picks is inferred from that, not read.
- Damage chain: projectile hit 0x7097d0 -> 0x695eb0 (zone multiplier by bone) -> 0x695b10 -> setHealth 0x695fb0. No difficulty term
  anywhere in it (0x695eb0 and 0x695b10 read in full). Difficulty changes max hp only (and enemy max hp). Max's skin
  (179_max_payne.txt) has no [Target] zone, so every hit on Max is x1.0: enemy Beretta/Ingram 5, Deagle 10, sniper 70.
- No natural regeneration. The only writers: heal 0x695a80 and the pending pool. heal(x): immediate = min(x, max x E+0x18 (5%));
  the rest goes to the pool f32[C+0x72c]. 0x68f510 (called from X_Character::update at 0x688a69; `0x6763c0` HealthRegenerate has exactly
  this one caller) drains the pool at max x E+0x14 (4%) per SCALED second (0x5e53d0), so 2.4 hp/s at max 60, only while the pool is > 0;
  the pool is zeroed when hp >= max/1.0001.
- Wounded animations (data 179_max_payne.txt): `C_AddHealth(0.6)` every 10 frames in the W_* animations -> heal(0.6), below the immediate
  cap so it lands at once. What switches Max into the wounded set (WoundedHealth 12 / UpperLimit 60) is not located.
- Flags: C+0x454 Immortal, C+0x455 DodgeImmortal, C+0x456 Invulnerable (setters 0x69a800/0x69a820/0x69a840, names TSV 3 sources;
  consumers 0x696015/0x6960b5/0x696071 capstone). Invulnerable: setHealth returns at once, no damage. Immortal and DodgeImmortal:
  hp is clamped to 0.1 instead of dying (new hp < 0.1), damage still lands. A dodge-immortal shoot-dodge (frames 1..49) is NOT
  invulnerability, only "cannot die" (INFERENCE: setHealth's arg4 with hp 0 clears the 0x455 test, so a forced kill ignores it).
- Death: u8[C+0x235] (= ST+7) set by 0x6963f0 when hp <= 0 (gamestate GSF_DEAD).
- Fall damage (0x68c960 end, asm 0x68dd05-0x68dd52 + Ghidra): dmg = max x f32[C+0x448] x pow(|v| - f32[C+0x44c], f32[C+0x450]),
  cause 3, when |v| > f32[C+0x444]. Fields are set at spawn (0x684085-0x6840a3 from the four CharacterConfig values in data order):
  15 m/s, 1%, 10, 2.1. Gravity (0,-20,0): 5 m fall v=14.1 safe, 7 m v=16.7 -> 32.6 hp, 10 m v=20 -> 75.5 hp (lethal). |v| = length of the
  previous-frame velocity (C+0x436, copy of C+0x41e).

## 2. Painkillers
- Count: call 0x694630 (HUD wrapper 0x421f20; gamestate word 23). Memory: pool = [WS+0x10] + 30*[wdef(id 16)+0x3d]; i32[pool+0].
  Max 8 (Pocketsize), start 0, +1 per medipack_small, picked up only if not full (PickupIfAmmoNotFull). Key Tab.
- Use 0x6959d0: refused unless count > 0 and (hp + C+0x72c) x 1.0001 (const 0x7d5d04 read from the exe) < max. Then count-1, heal(max x
  P+0xc HealthGainedFromPainkiller = 20%) = 12 hp at max 60: 3.0 immediate, 9.0 into the pool, drained at 2.4 hp/s (3.75 s).
  Effects from data: sound use_painkillers, white flash.
- Ghost today: uses a threshold on health_base (55% with foes near, 40% otherwise). The game also refuses at full health, and counts the
  pool: reading C+0x72c would let the ghost avoid wasting a pill while a previous one is still draining.

## 3. Bullet time and shoot-dodge
- Meter f32[GM+0x12f0] (0..1; 1.0 at level start), active u8[GM+0x12f4]. Config (data 1_globalconfig.txt): SlowMotionTime 15 s,
  RegenerationTime 1e8 s, Threshold 0.001, TurnOffPenalty 0, ShootDodgePenalty 0.10, ShootDodgeThreshold 0.01.
- Drain: meter -= REAL dt / 15 (6.67%/s of real time; bar lasts 15 s even though the world is at 0.2). At <0: meter 0, off.
- Refill: ONLY kills. meter += C+0x790 (taken by 0x687b80) x E+0x1c (BulletTimeGainedPerKill, default 1.0); each kill adds the victim's
  SlowMotionBonusForKiller: 0.12 for enemies using BONUS_SLOWMOTION, 0.10 for the Max skin. Time regen is 1e-8 per s = none.
- Speed: f32[GM+0x63] (= timer+0xc; getter 0x44eba0, setter 0x766c60). 1.0 normal; start message gm_changecontrolledgamespeed(0.2, 0.3),
  end (1.0, 0.5); linear in real time; min-combined with GM_ChangeGameSpeed; a Global speed != 1.0 (sniper cam 0.01) wins.
  Scaled dt (timer+4, 0x5e53d0) feeds characters, free fall, animation; real dt (timer+0x10, 0x7269f0) feeds meters, fades, blends.
- Shoot-dodge: allowed if meter > 0.01 (ST+0x6f); costs 0.10 only when bullet time is NOT already active (ST+0x70 started flag,
  counter u32[GM+0x1304]); anim data: scale 0.2 at frame 10, bullet time forced off at frame 12, jump 4.5, scale back at frame 30.
- Input tracker u32[GM+0x1258]: key down + any move/dodge direction = modifier (shoot-dodge), tap without movement = toggle.
- Ghost consequence: a bar of 100% is 15 s; each shoot-dodge outside bullet time = 10%, each kill = +12%. Without kills the bar is finite for
  the whole game. Spend it on shoot-dodges inside bullet time (free) rather than outside.

## 4. Damage to enemies
- Enemy max hp = HEALTH_CLASS_n (15,20,25,30,35,40,45,70,130,240) x f32[E+0] (0.8 default): class 1 = 12.0.
- Zones (3_characters.h, applied by 0x695eb0 on the hit bone name): Head+Neck x5, arms x0.75, legs x0.5, anything else x1.0 (no group matched).
- Per hit (pdef+0x3c, getter 0x40edf0): beretta/ingram 5, deagle/mp5 10, shotgun/jackhammer 5 per pellet x5, sniper 70, bat 15, pipe 1 + blast.
  Explosions: value(d) lerps r_in..r_out (0x64d230), grenade 50 -> 10 (r2..5), m79 50 (r1..5), molotov 50 direct.
- Read a hit as the hp delta of gamestate en[].health (f32[Ce+0x2b3]); zone and weapon are not logged.

## 5. Sniper zoom
- Weapon: wdef FOVMultiplier at +0x6e (0x691a40 -> 0x541170 `fld [ecx+0x6e]`, capstone; +0x6d is UseModeSwitchAnimation): sniper 0.10, all other 1.0.
- State machine in the game tick 0x44c1b0 (offsets confirmed by capstone): u32[GM+0x124c] 0 idle, 1 key held (zooming), 2 key released
  (zoom kept), 3 key pressed again (zoom reset to 1.0) until release; f32[GM+0x1250] = zoom factor z (1.0 -> FOVMult); runs only when
  f32[GM+0x123c] == 1.0 (no GM_ChangeFOV active) and z target < 1. Step: z = tan(atan(z) - realdt x 0.7), 0.7 = CameraConfig+0x4c
  SniperZoomInSpeed (getter 0x50b9d0). 1.0 -> 0.10 in about 0.98 s. 0x452480 passes z to the camera and sets u8[C+0x758].
- With C+0x758 set, 0x68ebd0 adds a scope shake (spline, 0x68e820): new random point every 0.8 s, strength 7.5 / 15 (crouched 1.5 / 3).
- Bullet cam: pdef [Sniper] block (63_bullet_sniper.txt): GM_ChangeGlobalGameSpeed(0.01, 0) + cam_animateparented(bullet_behind).
  Who runs the block and when (every sniper shot, or only a zoomed one) is NOT located.
- Sniper never allows shoot-dodge or auto-aim (ShootDodgeAllowed NO, AutoAimingAllowed NO, 310_sniper.txt).

## 6. Surprises for the ghost and the host
1. No health regeneration other than painkiller/pickup pools and the wounded animation: a hit costs hp for good. The ghost cannot "wait it out".
2. Bullet time never refills by itself (RegenerationTime 1e8). It is spent by use (15 s real) and shoot-dodges (10%); only kills give it back.
3. DodgeImmortal does not stop damage. The ghost replacing C_SetInvulnerable with real dodges gets real damage during shoot-dodges.
4. (INFERENCE for the host) Game speed 0.2 slows characters, physics and animation but NOT the meter drain, music fades or the zoom (real dt). Web must feed the guest
   the real frame dt and let the guest scale: a host clock that is already slowed would double the effect.
5. Sniper zoom runs on real dt and stops while GM+0x123c != 1.0 (a GM_ChangeFOV in progress).
6. Fall damage is steep (2.1 exponent): an unscripted drop of 7 m costs half the bar.
7. Existing gamestate reads cover hp, health_base, painkillers, weapon, clip, reserve, meter, active. Not read yet: C+0x72c pool,
   GM+0x63 speed, GM+0x124c/0x1250, C+0x758, C+0x454-0x456, difficulty entry, CFG values.

## 7. Not proven
- Runtime values of everything here: NO native or web read was done (columns empty). Chain of expected values is from code + data.
- Which difficulty index a New Game selects (inferred = default). Wounded-state trigger. Bullet-cam trigger. z -> FOV mapping (8.0 deg is arithmetic).
- Shoot-dodge cost and button tracker: Ghidra / gameplay.md only, not re-derived with capstone. Spawn-argument pairing of C+0x444..0x450 with the config
  names is by order.
- Reload duration (kf2 length not extracted), fire interval multiplier formula (0x691a80), per-class SlowMotionBonusForKiller of every 179_* skin.
