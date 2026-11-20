# part3_level7 boss: the cin1 "freeze" (#24) — CLOSED 2026-11-07: an injection artifact, not a game or host bug

Cause: `::Outside::camera_trigger1` (entity @22881704, volume `.TRIGGER` @23045209, at 5.00 141.50 12.50) is what moves cin1 from cam1 to
cam2. cin1_cam1 enables it (@22883844) and sends `::startroom::Horne->c_goto("::Outside::w2", 1)` (@22883582; w2 = 8.00 141.50 14.75).
Horne walks through the volume on his way to w2 (native4: Horne at 5.57 142.49 11.94). Injected straight from gm_init, Horne is still in the
lobby (y -32.5), never reaches the roof, the trigger never fires, and cin1 waits on cam1 forever (speed 0.7, no control). In natural play
Horne is on the roof by then (the ramp script sends him to `::Outside::w5`), so the OWNER's run does not hit this.
Refuted on the way: Max dying before the trigger (hp 60 for the whole run, out/re1106/boss_sample/native.log); the dummy `::teleport::max`
(it is teleported onto move_max_trigger and reaches w3, 9 m from camera_trigger1).

Route that plays the whole end (native, out/goal/p0/jump7.txt; RN_SYS_EN=<name> logs an actor's position per S line):
`gm_init(part3_level7)`, `s_modeswitch(game)`, `player->c_setinvulnerable(true)`, `::startroom::Horne->c_teleport("::Outside::w5")`,
door `::Outside::KATTOSYS::open_door_trigger.TRIGGER->T_Activate()`, wait out cin1 and cin2 (cin2_cam2 turns invulnerability off @24673319:
set it again), then `::teleport::ek1..ek4->C_Kill(1)` (the killer-suit counter's transition sends `make_breakable` to horn1/2; a bare
`killersuit_counter->fsm_switch(4)` sets the state without those commands), `horn1/horn2::piece1->FSM_Send(break)`,
`explosion_detector1->FSM_Send(make_breakable)` + `break` -> cable_counter 3 -> `all_cables_broken` -> e_cam1..15 -> `End_Credits` (@24611637)
-> `s_modeswitch(menu)` (@24014943). Chain details: out/goal/bec/f13_final.md, out/bec/jefe/cadena.md.
Tools: RN_JUMP `!line` sends or counts a line under a cutscene; `C_Kill()` without its argument is a parse error that closes the game.
