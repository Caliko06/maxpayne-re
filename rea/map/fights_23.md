# Fights 23: one real fight per new Part II/III type (WORKLIST #23)

Status 2026-11-05: 13 of 13 types pass, 0 STOP. The three that were open (Joe, Horst, Aesir) are level scripts and the engine's own update rule, not port bugs: no patches.py change.
Method: RN_JUMP injects the moment, no walking, no C_Kill, no god (F3). Max keeps his own 60 hp (a `c_addhealth` before a fight tops him up: painkillers, not invulnerability) and gets pumpshotgun + ingramdual with ammo. `@aim`/`@f` aim at the nearest live foe in the room and fire.
Measured in headless Chrome (build/web RN_BR=0, port 5186, RN_SYS=500 RN_GHOST=60): 0 STOP in every run; fps median 60 in every fight (p5 52-60; the dips are the screenshots every 3 s).
Links open on :5181 (the OWNER's server; needs this build merged). Captures: `rea/fights23/<name>.jpg` (9 frames over the run).

## Findings
- **Jumppoint is not a waypoint.** `C_Teleport("::StartRoom::Jumppoint 00")` ends the game with `Waypoint ... not found` (2 runs, P2L2b); `::StartRoom::W01`, `::table::w1`, `::keke2::Waypoint04` work. 60 of the 124 rows of `inject_part23.tsv` teleport to a `Jumppoint`: they cannot run. Cause named by observation (the waypoint array 0x6d5a50, lookup 0x69a860, holds only w*/Waypoint* records); not RE-checked. Use the lines below.
- **Characters that are not drawn do not think (original engine, found 2026-11-05).** An enemy moved with C_Teleport behind Max's back, or left in a room Max does not see, keeps `lastseen`/interest frozen: its AI is not updated. It wakes the moment Max's camera sees it (horst3: ticks at once) or a script/console line forces it (`C_ForceUpdate(60)`, horst2). That was the "AI flag true, distance constant for 5-10 s" of Aesir, Joe, Horst, Frankie, Jamn. Enemies woken in place (Mercenary, Trenchcoat, ...) were in view, so they engaged at once. For a test line: `@aim` first (camera on the foe) or `::foe->C_ForceUpdate(60);`. Evidence and code: "Why the three were open" below and rea/map/ai_behaviour.md 2.5.
- Mac, Ronald, Pilate, BB, Frankie, Killer Suit shoot hard: Max (60 hp) dies in 1-3 s at 3-8 m without bullet time. That is the data, not a bug (they hit for 5-15 per shot).

## Fights

### 1 Jamn - part2_level2b
In place: the level's own StartRoom, wake naks4. Max starts the fight shooting after 5 s.

Result: PASS: AI active, hit Max 2x (5 hp), naks4 + E01 killed, 0 STOP, 60 fps

[RN_JUMP link](http://127.0.0.1:5181/?env=RN_JUMP%3Dmaxpayne_gamemode-%3Egm_init%28part2_level2b%29%3B%7Cx_modeswitch-%3Es_modeswitch%28game%29%3B%7Cplayer-%3Ec_pickupweapon%28%22pumpshotgun%22%29%3B%7Cplayer-%3Ec_pickupammo%28%22pumpshotgun%22~300%29%3B%7Cplayer-%3Ec_pickupweapon%28%22ingramdual%22%29%3B%7Cplayer-%3Ec_pickupammo%28%22ingramdual%22~600%29%3B%7C%3A%3AStartRoom%3A%3Aenemy_naks4-%3EC_SetStateMachine%28mobstercombat%29%3B%7C%40w%3A300%7C%40aim%3A1.2%7C%40f%3A80%3A10%7C%40w%3A3000)

Capture: `rea/fights23/jamn.jpg`

### 2-3 Mac + Ronald - part2_level4
Ronald and Mac moved to ::table::w5/w4 (6-8 m from Max at w1), woken.

Result: PASS: Max hit 6x (shotguns, 5-15 hp), Mac e3, Mac e2, Ronald e1 killed (Pilate also died: he stands in that room)

[RN_JUMP link](http://127.0.0.1:5181/?env=RN_JUMP%3Dmaxpayne_gamemode-%3Egm_init%28part2_level4%29%3B%7Cx_modeswitch-%3Es_modeswitch%28game%29%3B%7Cplayer-%3Ec_pickupweapon%28%22pumpshotgun%22%29%3B%7Cplayer-%3Ec_pickupammo%28%22pumpshotgun%22~300%29%3B%7Cplayer-%3Ec_pickupweapon%28%22ingramdual%22%29%3B%7Cplayer-%3Ec_pickupammo%28%22ingramdual%22~600%29%3B%7Cplayer-%3Ec_addhealth%28100%29%3B%7Cplayer-%3Ec_teleport%28%22%3A%3Atable%3A%3Aw1%22%29%3B%7C%3A%3Atable%3A%3Ae3-%3EC_Teleport%28%22%3A%3Atable%3A%3Aw5%22%29%3B%7C%3A%3Acellarroom3%3A%3Ae1-%3EC_Teleport%28%22%3A%3Atable%3A%3Aw5%22%29%3B%7C%3A%3Acellarroom3%3A%3Ae2-%3EC_Teleport%28%22%3A%3Atable%3A%3Aw4%22%29%3B%7C%3A%3Atable%3A%3Ae3-%3EC_SetStateMachine%28mobstercombat%29%3B%7C%3A%3Acellarroom3%3A%3Ae1-%3EC_SetStateMachine%28mobstercombat%29%3B%7C%3A%3Acellarroom3%3A%3Ae2-%3EC_SetStateMachine%28mobstercombat%29%3B%7C%40aim%3A1.2%7C%40f%3A100%3A6%7C%40w%3A900)

Capture: `rea/fights23/mac_ronald.jpg`

### 4 Providence Pilate - part2_level4
In place at ::table::, Max at w1, merccombat.

Result: PASS: Pilate hit Max 3x in 0.1 s, then 104 hp -> 0 (dead); Mac e3 died too

[RN_JUMP link](http://127.0.0.1:5181/?env=RN_JUMP%3Dmaxpayne_gamemode-%3Egm_init%28part2_level4%29%3B%7Cx_modeswitch-%3Es_modeswitch%28game%29%3B%7Cplayer-%3Ec_pickupweapon%28%22pumpshotgun%22%29%3B%7Cplayer-%3Ec_pickupammo%28%22pumpshotgun%22~300%29%3B%7Cplayer-%3Ec_pickupweapon%28%22ingramdual%22%29%3B%7Cplayer-%3Ec_pickupammo%28%22ingramdual%22~600%29%3B%7Cplayer-%3Ec_addhealth%28100%29%3B%7Cplayer-%3Ec_teleport%28%22%3A%3Atable%3A%3Aw1%22%29%3B%7C%3A%3Atable%3A%3Apilate-%3EC_SetStateMachine%28merccombat%29%3B%7C%40aim%3A1.2%7C%40f%3A120%3A6%7C%40w%3A900)

Capture: `rea/fights23/pilate.jpg`

### 5 Salem Joe - part2_level4
The level's own gunroom start: Max at ::gunroom::w1, `::gunroom::script->FSM_Send(start)` (what the level sends when Max enters). Joe walks in by himself; nothing is moved or woken by hand.

Result: PASS (closed 2026-11-05, ORIGINAL BEHAVIOUR, see "Why the three were open"): Joe is AI active, seen and perceived at 8.2 m in the gunroom, and is vulnerable there: Max killed e2 (+1.3 s), e1, Joe (104 -> 0, +1.7 s) and died at +3.8 s. 0 STOP, fps 60. That Joe himself attacks Max (about 9 s after the script starts, `joe_timer`) was measured in a separate run with Max immortal (trace_joe.txt, joe7), not in this one

[RN_JUMP link](http://127.0.0.1:5181/?env=RN_JUMP%3Dmaxpayne_gamemode-%3Egm_init%28part2_level4%29%3B%7Cx_modeswitch-%3Es_modeswitch%28game%29%3B%7Cplayer-%3Ec_pickupweapon%28%22pumpshotgun%22%29%3B%7Cplayer-%3Ec_pickupammo%28%22pumpshotgun%22~300%29%3B%7Cplayer-%3Ec_pickupweapon%28%22ingramdual%22%29%3B%7Cplayer-%3Ec_pickupammo%28%22ingramdual%22~600%29%3B%7Cplayer-%3Ec_addhealth%28100%29%3B%7Cplayer-%3Ec_teleport%28%22%3A%3Agunroom%3A%3Aw1%22%29%3B%7C%3A%3Agunroom%3A%3Ascript-%3EFSM_Send%28start%29%3B%7C%40w%3A60%7C%40aim%3A1.2%7C%40f%3A300%3A6%7C%40w%3A900)

Capture: `rea/fights23/joe.jpg`. Trace: `rea/fights23/trace_joe.txt`

### 6 Horst - part2_level1
Horst moved to ::drug::w2 (Max at w1, 6 m) like the level's wake script (`RemoveAllWeapons`, `pickupweapon(sawedshotgun)`, `SetInfiniteAmmo(sawedshotgun, true)`, mobstercombat), Max's camera on him.

Result: PASS (closed 2026-11-05, ORIGINAL BEHAVIOUR): Horst ticks at once (act/seen/perc 1), walks to 4.6 m from Max and hits him 4x (31.6 -> 0 in 4 s) while at 16 hp; with Max firing from +2 s (second link) Horst dies first (16 -> 0). 0 STOP, fps 55-60

[RN_JUMP link, Max only aims](http://127.0.0.1:5181/?env=RN_JUMP%3Dmaxpayne_gamemode-%3Egm_init%28part2_level1%29%3B%7Cx_modeswitch-%3Es_modeswitch%28game%29%3B%7Cplayer-%3Ec_pickupweapon%28%22pumpshotgun%22%29%3B%7Cplayer-%3Ec_pickupammo%28%22pumpshotgun%22~300%29%3B%7Cplayer-%3Ec_pickupweapon%28%22ingramdual%22%29%3B%7Cplayer-%3Ec_pickupammo%28%22ingramdual%22~600%29%3B%7Cplayer-%3Ec_addhealth%28100%29%3B%7Cplayer-%3Ec_teleport%28%22%3A%3Adrug%3A%3Aw1%22%29%3B%7C%3A%3Atoilet_last%3A%3Ae_horst-%3EC_Teleport%28%22%3A%3Adrug%3A%3Aw2%22%29%3B%7C%3A%3Atoilet_last%3A%3Ae_horst-%3EC_RemoveAllWeapons%28empty%29%3B%7C%3A%3Atoilet_last%3A%3Ae_horst-%3EC_pickupweapon%28sawedshotgun%29%3B%7C%3A%3Atoilet_last%3A%3Ae_horst-%3EC_SetInfiniteAmmo%28sawedshotgun~true%29%3B%7C%3A%3Atoilet_last%3A%3Ae_horst-%3EC_SetStateMachine%28mobstercombat%29%3B%7C%40aim%3A1.2%7C%40w%3A500)
[RN_JUMP link, Max fires](http://127.0.0.1:5181/?env=RN_JUMP%3Dmaxpayne_gamemode-%3Egm_init%28part2_level1%29%3B%7Cx_modeswitch-%3Es_modeswitch%28game%29%3B%7Cplayer-%3Ec_pickupweapon%28%22pumpshotgun%22%29%3B%7Cplayer-%3Ec_pickupammo%28%22pumpshotgun%22~300%29%3B%7Cplayer-%3Ec_pickupweapon%28%22ingramdual%22%29%3B%7Cplayer-%3Ec_pickupammo%28%22ingramdual%22~600%29%3B%7Cplayer-%3Ec_addhealth%28100%29%3B%7Cplayer-%3Ec_teleport%28%22%3A%3Adrug%3A%3Aw1%22%29%3B%7C%3A%3Atoilet_last%3A%3Ae_horst-%3EC_Teleport%28%22%3A%3Adrug%3A%3Aw2%22%29%3B%7C%3A%3Atoilet_last%3A%3Ae_horst-%3EC_RemoveAllWeapons%28empty%29%3B%7C%3A%3Atoilet_last%3A%3Ae_horst-%3EC_pickupweapon%28sawedshotgun%29%3B%7C%3A%3Atoilet_last%3A%3Ae_horst-%3EC_SetInfiniteAmmo%28sawedshotgun~true%29%3B%7C%3A%3Atoilet_last%3A%3Ae_horst-%3EC_SetStateMachine%28mobstercombat%29%3B%7C%40aim%3A1.2%7C%40w%3A120%7C%40f%3A100%3A6%7C%40w%3A900)

Capture: `rea/fights23/horst.jpg`. Trace: `rea/fights23/trace_horst.txt`

### 7 Frankie the Bat - part2_level1
Frankie moved to ::outside1::w3 (Max at w1, 8 m) with ingramdual (as the level script gives him), merccombat.

Result: PASS for perceive/chase/shoot: stood 6 s, then came 7.8 -> 5.4 m and hit Max 12x; Frankie 192 -> 112 hp. Max died first (3 goons from the bar joined)

[RN_JUMP link](http://127.0.0.1:5181/?env=RN_JUMP%3Dmaxpayne_gamemode-%3Egm_init%28part2_level1%29%3B%7Cx_modeswitch-%3Es_modeswitch%28game%29%3B%7Cplayer-%3Ec_pickupweapon%28%22pumpshotgun%22%29%3B%7Cplayer-%3Ec_pickupammo%28%22pumpshotgun%22~300%29%3B%7Cplayer-%3Ec_pickupweapon%28%22ingramdual%22%29%3B%7Cplayer-%3Ec_pickupammo%28%22ingramdual%22~600%29%3B%7Cplayer-%3Ec_addhealth%28100%29%3B%7Cplayer-%3Ec_teleport%28%22%3A%3Aoutside1%3A%3Aw1%22%29%3B%7C%3A%3Abar%3A%3Afrankie-%3EC_Teleport%28%22%3A%3Aoutside1%3A%3Aw3%22%29%3B%7C%3A%3Abar%3A%3Afrankie-%3EC_PickupWeapon%28ingramdual%29%3B%7C%3A%3Abar%3A%3Afrankie-%3EC_SetInfiniteAmmo%28ingramdual~true%29%3B%7C%3A%3Abar%3A%3Afrankie-%3EC_SetStateMachine%28merccombat%29%3B%7C%40w%3A300%7C%40aim%3A1.2%7C%40f%3A300%3A6%7C%40w%3A1500)

Capture: `rea/fights23/horst_frankie.jpg`

### 8 BB - part3_level4
::teleport::bb2 moved to ::startroom::w4 (Max at w2), jackhammer, vulnerable, merccombat.

Result: PASS: BB hit Max 7x (5-15 hp) and died (192 -> 0); Max died 8 s later

[RN_JUMP link](http://127.0.0.1:5181/?env=RN_JUMP%3Dmaxpayne_gamemode-%3Egm_init%28part3_level4%29%3B%7Cx_modeswitch-%3Es_modeswitch%28game%29%3B%7Cplayer-%3Ec_pickupweapon%28%22pumpshotgun%22%29%3B%7Cplayer-%3Ec_pickupammo%28%22pumpshotgun%22~300%29%3B%7Cplayer-%3Ec_pickupweapon%28%22ingramdual%22%29%3B%7Cplayer-%3Ec_pickupammo%28%22ingramdual%22~600%29%3B%7Cplayer-%3Ec_addhealth%28100%29%3B%7Cplayer-%3Ec_teleport%28%22%3A%3Astartroom%3A%3Aw2%22%29%3B%7C%3A%3Ateleport%3A%3Abb2-%3EC_Teleport%28%22%3A%3Astartroom%3A%3Aw4%22%29%3B%7C%3A%3Ateleport%3A%3Abb2-%3EC_SetInvulnerable%28false%29%3B%7C%3A%3Ateleport%3A%3Abb2-%3EC_PickupWeapon%28jackhammer%29%3B%7C%3A%3Ateleport%3A%3Abb2-%3EC_SetInfiniteAmmo%28jackhammer~true%29%3B%7C%3A%3Ateleport%3A%3Abb2-%3EC_SetStateMachine%28merccombat%29%3B%7C%40aim%3A1.2%7C%40f%3A100%3A6%7C%40w%3A900)

Capture: `rea/fights23/bb.jpg`

### 9 Mercenary - part3_level2b
In place at ::08_Conveyor::, Max at Waypoint01, two woken.

Result: PASS: 3 mercenaries killed (28 hp each), hit Max once (10 hp), 60 fps

[RN_JUMP link](http://127.0.0.1:5181/?env=RN_JUMP%3Dmaxpayne_gamemode-%3Egm_init%28part3_level2b%29%3B%7Cx_modeswitch-%3Es_modeswitch%28game%29%3B%7Cplayer-%3Ec_pickupweapon%28%22pumpshotgun%22%29%3B%7Cplayer-%3Ec_pickupammo%28%22pumpshotgun%22~300%29%3B%7Cplayer-%3Ec_pickupweapon%28%22ingramdual%22%29%3B%7Cplayer-%3Ec_pickupammo%28%22ingramdual%22~600%29%3B%7Cplayer-%3Ec_addhealth%28100%29%3B%7Cplayer-%3Ec_teleport%28%22%3A%3A08_Conveyor%3A%3AWaypoint01%22%29%3B%7C%3A%3A08_Conveyor%3A%3Aenemy01-%3EC_SetStateMachine%28merccombat%29%3B%7C%3A%3A08_Conveyor%3A%3Aenemy02-%3EC_SetStateMachine%28merccombat%29%3B%7C%40w%3A180%7C%40aim%3A1.2%7C%40f%3A100%3A6%7C%40w%3A900)

Capture: `rea/fights23/mercenary.jpg`

### 10-11 Trenchcoat + FatTrenchcoat - part3_level4
In place at ::keke2::, Max at Waypoint04, both woken.

Result: PASS: both killed (32 hp each), hit Max 4x (5 hp)

[RN_JUMP link](http://127.0.0.1:5181/?env=RN_JUMP%3Dmaxpayne_gamemode-%3Egm_init%28part3_level4%29%3B%7Cx_modeswitch-%3Es_modeswitch%28game%29%3B%7Cplayer-%3Ec_pickupweapon%28%22pumpshotgun%22%29%3B%7Cplayer-%3Ec_pickupammo%28%22pumpshotgun%22~300%29%3B%7Cplayer-%3Ec_pickupweapon%28%22ingramdual%22%29%3B%7Cplayer-%3Ec_pickupammo%28%22ingramdual%22~600%29%3B%7Cplayer-%3Ec_addhealth%28100%29%3B%7Cplayer-%3Ec_teleport%28%22%3A%3Akeke2%3A%3AWaypoint04%22%29%3B%7C%3A%3Akeke2%3A%3Aenemy01-%3EC_SetStateMachine%28mobstercombat%29%3B%7C%3A%3Akeke2%3A%3Aenemy02-%3EC_SetStateMachine%28mobstercombat%29%3B%7C%40w%3A180%7C%40aim%3A1.2%7C%40f%3A100%3A6%7C%40w%3A900)

Capture: `rea/fights23/trenchcoat_fat.jpg`

### 12 Killer Suit - part3_level6
In place at ::sec::, Max at w3, e1/e2 woken (the room also holds e3, e4, enemy03).

Result: PASS: e1, e2, e4 killed (36 hp), e3 to 6; they hit Max 10x in 0.7 s and killed him (death cam)

[RN_JUMP link](http://127.0.0.1:5181/?env=RN_JUMP%3Dmaxpayne_gamemode-%3Egm_init%28part3_level6%29%3B%7Cx_modeswitch-%3Es_modeswitch%28game%29%3B%7Cplayer-%3Ec_pickupweapon%28%22pumpshotgun%22%29%3B%7Cplayer-%3Ec_pickupammo%28%22pumpshotgun%22~300%29%3B%7Cplayer-%3Ec_pickupweapon%28%22ingramdual%22%29%3B%7Cplayer-%3Ec_pickupammo%28%22ingramdual%22~600%29%3B%7Cplayer-%3Ec_addhealth%28100%29%3B%7Cplayer-%3Ec_teleport%28%22%3A%3Asec%3A%3Aw3%22%29%3B%7C%3A%3Asec%3A%3Ae1-%3EC_SetStateMachine%28mobstercombat%29%3B%7C%3A%3Asec%3A%3Ae2-%3EC_SetStateMachine%28mobstercombat%29%3B%7C%40w%3A180%7C%40aim%3A1.2%7C%40f%3A100%3A6%7C%40w%3A900)

Capture: `rea/fights23/killersuit.jpg`

### 13 Aesir Security - part3_level6
The level's own start: `::start::script->FSM_Send(initialize)` (the level sends it; its `::l1::e1-e3` go to `::start::e1w1/e2w1/e3w1`, then fight). Max at his spawn, nobody moved by hand.

Result: PASS (closed 2026-11-05, ORIGINAL BEHAVIOUR): Aesir cross 20 m (and 12 m of height) into the start room, perceive Max and shoot him 10x (5 hp each) over 11 s: Max 60 -> 5, still alive at +14 s; he killed e3 (36 -> 0, +2.6 s) and e1 (+10.8 s), e2 to 31. In the run where Max does not fire (aesir2) one burst took him 60 -> 5 in 0.3 s. 0 STOP. fps median 29 in these runs because 3 other workers were building (load average 36); 60 in the same-day Joe and Horst runs

[RN_JUMP link](http://127.0.0.1:5181/?env=RN_JUMP%3Dmaxpayne_gamemode-%3Egm_init%28part3_level6%29%3B%7Cx_modeswitch-%3Es_modeswitch%28game%29%3B%7Cplayer-%3Ec_pickupweapon%28%22pumpshotgun%22%29%3B%7Cplayer-%3Ec_pickupammo%28%22pumpshotgun%22~300%29%3B%7Cplayer-%3Ec_pickupweapon%28%22ingramdual%22%29%3B%7Cplayer-%3Ec_pickupammo%28%22ingramdual%22~600%29%3B%7Cplayer-%3Ec_addhealth%28100%29%3B%7C%3A%3Astart%3A%3Ascript-%3EFSM_Send%28initialize%29%3B%7C%40w%3A60%7C%40aim%3A1.2%7C%40f%3A200%3A6%7C%40w%3A900)

Capture: `rea/fights23/aesir.jpg`. Trace: `rea/fights23/trace_aesir.txt`

## Why the three were open (WORKLIST #23, 2026-11-05, ia worker)
Method: a local, uncommitted probe (`rea/fights23/ghostall.patch`, env `RN_GHOSTALL=<name part>|*`) prints each foe's perception state once a second: AI active ST+0xae, hit ST+5, seen MT+0x3ad, perceived MT+0x3af, interest MT+0x3b0, activate timeout MT+0x3b4, last-seen AP+0x5fc, AP+0x57d/0x57e (MT = AP+0x157). One run per question; traces in `rea/fights23/trace_*.txt`. The Becario's chain was checked against the corpus (rea/c/MaxPayne.exe 004f62a0 lines 92/109/113/134/154/199, 004f7440, 004f6df0, 004fee60 57-62): every cited line holds, including that `C_SetStateMachine` wakes only an already perceived foe (horst1, joe4: act stays 0 after it). Two of its predictions were wrong: invulnerability was not Joe's problem, and missing ammo alone was not Horst's.

**Salem Joe: the level's own retreat (original).**
- Max at ::FOYER::w2, Joe `merccombat` + `C_SetInvulnerable(false)` (joe2): at 15.9 m Joe sees Max (seen/perc/act 1, interest 360), then within 1 s his state flips to `nonreact 1` and he walks away (x 7.06 -> -6.54, down to y -1.38, toward ::gunroom::). The only script that does that is `1_Part2_Level4.ldb@0x162e17b`: `::FOYER::joe_retreat_safetrigger.TRIGGER->T_Enable(false); ::FOYER::Joe->c_setstatemachine(nonreactive); ::FOYER::Joe->c_goto("::gunroom::joe_w1", 1);`. A nonreactive character clears perception every frame (0x4f7440), so "AI active but quiet" is the script, and the distance that grows is his walk.
- He also spawns invulnerable (`@0x15442db`), until `::gunroom::disable_invulnerability` fires (`@0x1528f12`): in the gunroom he takes damage (joe6: 104 -> 34 without any `C_SetInvulnerable`; joe8: dies to Max).
- In the gunroom the script gives him the fight: `::gunroom::script` FSM_Send(start) -> `::gunroom::joe_timer`; joe7 (Max immortal, e1/e2 busy): Joe nonreactive for 9 s at 2 m, then `merccombatdefensive` (`@0x1529548`): act/seen/perc 1, walks to 0.8 m of Max.
- Port check: the flags follow the machine name exactly (joe4: merccombat, merccombatdefensive 0/0; idleuntilenemyseen -> AP+0x57e; nonreactive -> AP+0x57d), so 0x4fefa0 lifts right. Becario's `ST+5` hit hypothesis: false.

**Horst: not drawn, and the old line had no ammo (original rule + test line).**
- horst1 (Max faces away, Horst at 6.2 m in the same room, mobstercombat, infinite ammo): `lastseen 3.04`, `tmo -0.04` constant for 6 s, act/seen/perc 0. Frozen, not blind.
- horst2 (same + `C_ForceUpdate(60)`): act/seen/perc 1 at once, he walks and shoots (Max 40 -> 15 at f1990, 18 -> 0 at f2057: two bursts, no shot by Max). horst3 (same, no ForceUpdate, `@aim` = camera on him): identical, he ticks the instant he is seen.
- The old test line lacked `C_SetInfiniteAmmo(sawedshotgun, true)`; the level gives it after `mobstercombat`/`delay500ms` (`1_Part2_Level1.ldb@0x1bcc71b..0x1bcc7ee`). His real wake is `::toilet_last::t_horst` -> `C_SendSpecial` + `idle`, with him disarmed and `nonreactive` until then (`@0x1bcc558`).

**Aesir: they are not in the room where the test put them (original).**
- `::l1::e1-e6` stand in room `l1`: 17-22 m from Max's spawn and 12 m below the start-room floor (aesir1: y -13.5; on arrival, aesir2: y -1.5), another room. Max in `::l2::Waypoint05` never shares sight lines with them: with `C_ForceUpdate` they tick (lastseen 6 -> 15 s) but `room=diff`, seen 0, perceived 0 forever. Gate 2 of 0x4f62a0 (`0x4f17b0`, rooms visible to each other) is closed: nothing in 3a/3b/4 can fire.
- The level brings them: `1_Part3_Level6.ldb@0x15f1c00` `::start::script`: `initialize` -> e1-e3 `idleuntilenemyseen` + `c_gotoandshoot("::start::e1w1")`/`e2w1`/`C_GoTo e3w1`; next state: e1/e2 `crouchandshootstatic` (they hold position, so they never "chase"), e3 `merccombat`. aesir2/aesir3: after `FSM_Send(initialize)` e1 is seen/perceived at 4.4 m on arrival (f1800), Max takes 8 hits (60 -> 5 in 0.3 s).
- The Becario's alternative (orientation `C_KeepOrientationToPlayer`) was not needed.

Test-line rule from this: put the foe in view (`@aim`) or `C_ForceUpdate`, give the weapon and ammo the level gives, and prefer the level's own `::room::script->FSM_Send(...)` over hand-moving foes.

## Follow-up (after the merge)
- `inject_part23.tsv` fixed: Jumppoint teleports rewritten to w*/Waypoint* (104 teleports, all checked as records in the .ldb); 24 rows have no waypoint in their rooms and wake the enemies in place (`@m:no waypoint`).
- (superseded 2026-11-05, see "Why the three were open") Joe with the level's own state (`merccombatdefensive`, Max at ::FOYER::w2, 10 s): still no shot at Max, no chase, never damaged (distance 15.9 -> 19.2: he keeps away). Open.
- (superseded 2026-11-05, see "Why the three were open") Horst with the level's own trigger (`::toilet_last::t_horst.TRIGGER->T_Activate()`, Max at ::bar::w6): not tested as a fight (Max in the bar shot Frankie, 192 -> 0, and e1-e3 while they were AI-inactive). Frankie does die to Max's fire. Horst still open.
- (superseded 2026-11-05, see "Why the three were open") Aesir: the Becario sheet gives `idleuntilenemyseen` as the level's state for `::l1::e1`; not run (the Becario's abiertos.md had not arrived).
