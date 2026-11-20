# Enemies, level items, triggers, level exit (MaxPayne.exe 1.05, rebased VAs) — for the ghost driver

Runtime status: the enemy list read is wired in host/gamestate.c (en[], enemies_alive); live confirmation in a level with AI
pending (startup_level has EnableAI = FALSE in levels.txt, so 0 enemies there is expected).


Image: build/img/MaxPayne.exe (rebased VAs). All offsets hex. Structs are packed (unaligned dwords are normal).
Proven roots (facts.md): theApp 0x8a6338 -> [theApp+0xc8] ModeSwitch -> [ModeSwitch+0x80] = GM (MaxPayne_GameMode).
Found here: Level L = GM+0x104 (embedded; ctor 0x493bb0; load 0x493f80 called at 0x45008e with ecx=GM+0x104);
LevelDB = [L+0x10] (0x65a2e0 = `mov eax,[ecx+0x10]`).
proof = asm address; 2nd = independent source. Conf: H high, M medium, L low.

## Table (field | path | type | proof | second source | confidence)

| field | path from a proven pointer | type | proof | second source | conf |
|---|---|---|---|---|---|
| AI vector (enemy controllers) | begin=[GM+0x24c], end=[GM+0x250] (vector object at GM+0x248); count=(end-begin)>>2 | MaxPayne_ArtificialPlayerInput*[] (4-byte elements) | 0x4502ae..0x4502c7 count; loop "Enemy creation" 0x450287..0x450a79: one `new(0x612)` + ctor 0x4fe6f0 per 20-byte LevelDB character record (count computed at 0x450298 as bytes/0x14 via imul 0x66666667), stored at 0x4508c7 `mov ecx,[ebx+0x24c]; mov [ecx+edx*4],eax` | 0x451e70 @0x45219e..0x4521d0 loops this vector and calls ai.sm.setTarget(player char); ctor 0x4fe6f0 @0x4fe7a7 stores 0x7cfcd4 (ArtificialPlayerInput vtable) at [obj+8]; string "Max Payne AI" | H |
| enemy X_Character* | `ai=[[GM+0x24c]+4*i]`; `char=u32(ai+0x157+0x3dc)=u32(ai+0x533)` (unaligned) | X_Character* | 0x450a04 `call 0x452cd0` returns wrapper w (`[w+8]`=char); 0x450a16 `call 0x4ff850(ai, [w+8])`; 0x4ff850 @0x4ff85a `lea ecx,[esi+0x157]; call 0x4f1ee0(char)`; 0x4f1ee0 @0x4f1eee `mov [ecx+0x3dc],eax` | 0x4f17b0 / 0x4f18a0 read `[sm+0x3d8]` and `[sm+0x3dc]` and both use `+0x1e` (char entity base) | H (offset); M (unaligned read OK at runtime) |
| AI brain object | `sm=ai+0x157` (getter 0x4ff340 `lea eax,[ecx+0x157]`, ctor call 0x4f0b70 @0x4fe748) | object | 0x4ff340 | AIStateMachine vtable 0x7cfcac is the state-class base; exact class of the object at +0x157 UNPROVEN | M |
| AI target | `u32(sm+0x3d8)=u32(ai+0x52f)` = player X_Character* | X_Character* | 0x4f1eb0 `mov [ecx+0x3d8],eax`, called from 0x4521c4 with the player char | equals [[GM+0x1080]+8] (facts) | H |
| character wrapper list (all chars incl. player) | `n=[GM+0x109c]; while n!=GM+0x1094 {w=n+8; char=[w+8]; ctrl=[w+0x10]; n=[n]}`; count=[GM+0x10a4] | circular doubly-linked list, newest first; w+8=X_Character*, w+0x10=controller (player: GM+0x258; enemy: the AI object) | init 0x44b701..0x44b745 ([0x1098]=0x109c, [0x109c]=0x1094, count 0x10a4=0); insert 0x452d09..0x452d4f (`lea eax,[edi-8]`, relink, `inc [ebx+0x14]`), `[edi+8]=char` at 0x452e3b path; 0x4520fc stores the player wrapper in [GM+0x1080] | alternative route to the same chars; link direction derived from the insert code only | M |
| player wrapper | `w=[GM+0x1080]`, char=[w+8] | | facts.md | | H |
| X_CharacterProperties | `props=char+0x22e` | struct | 0x694430 `lea eax,[ecx+0x22e]`; dump 0x6d74f0 prints "X_CharacterProperties" and every getter with a label | labels (below) | H |
| ALIVE/DEAD | `u8(char+0x235)` (= props+7); nonzero = dying/dead | u8 | getter 0x6d6eb0 `mov al,[ecx+7]`; 0x6d74f0 @0x6d75a8 pairs it with label " Is dying/dead:  %d" (0x89adf4) | 0x4f0fc0 @0x4f0ff7..0x4f101b: the AI ignores either char when `0x6d6eb0(props)` is true | H |
| health | `f32(char+0x2b3)` (= props+0x85) | f32 | 0x694470 -> 0x6d71b0 `fld [ecx+0x85]`; label " Health: %2.2f" | facts.md overlay 0x422d60 prints Health from the same getter | H |
| AI active | `u8(char+0x2dc)` (= props+0xae) | u8 | getter 0x6d7340; label " AI active:" | | H (name), M (meaning: AI switched on) |
| getting damage | `u8(char+0x233)` (= props+5) | u8 | getter 0x6d6e90 | label " Is getting damage" | H |
| current weapon id | `i32(char+0x2a9)` (= props+0x7b) | i32 | getter 0x6d6fb0 | label " Current weapon ID" | H |
| position (world) | `f32x3 at char+0x72` (= entity base char+0x1e, +0x54) | vec3 | 0x642d50 `lea eax,[ecx+0x54]` applied to `char+0x1e` at 0x4f1087..0x4f10ba; squared distance of two chars' vec3 at 0x4f10ca..0x4f110c | entity base is shared (items: base at +0; triggers: base vtable 0x7d8820 at +0x1e, 0x71e6e5). Not checked against a live value | M |
| velocity | char+0x41e (vec3) | | facts.md | | H |
| room (was "team") | `u32(char+0x7e)` (= base+0x60) is the character's room, not a team | u32 | getter 0x642d40 `mov eax,[ecx+0x60]` (items: room `[item+0x60]`, same base); 0x4f17b0 compares two characters' rooms and, when they differ, 0x63bf40 clears `room+0xd9` over every room (0x63a2d0 count, 0x63a2c0 by index) and builds a room list in 0x8ab740 | live: equals the nav room pointers (host/gamestate.c en[].room) | H. Friend vs enemy: no team field found; part1_level1's friendly cop is told apart by its instance name (see Level exit, `::r01::helper`) |
| enemies-killed counter | MaxPayne_StatisticsMode (ctor 0x4232e0) / X_StatisticsMode 0x61f190 | | not traced | difficulty key BulletTimeGainedPerKillMultiplier (0x6770a0) shows kills are tracked | UNPROVEN. Poll dead flags |
| Level-items manager | `im=[GM+0x97]` (getter 0x4213a0) = X_LevelItemImplementation (vtable 0x7d562c, ctor 0x66ebd0, `new 0x21`) | object | created in GM init 0x420e30 near 0x4211e9 (`lea ebx,[eax+0x97]` after 0x66ebd0); passed to Level load as arg 7, stored at [L+0x24] (0x493fe5..0x49400a); Level load @0x494df2..0x494e3d calls `[[L+0x24]]+0x24` = vtable slot 9 = 0x66f650 per LevelDB item record (record list from 0x750490) | GM tick 0x44c1b0 @0x44d7da: profile "Level items" then `[im]+0x14`; slots 0x66f5c0 (update), 0x66f4b0 (clear) | H |
| Level-items container | MSVC6 std::map at im+0xc: head=[im+0x10], size=u32(im+0x18). Walk: `node=[head]` (leftmost), loop as in 0x66f5c0 (successor code 0x66f60f..0x66f645; end when node==head; global nil node is 0x8b4124). `item=[node+0xc]` (X_LevelItem*), `[node+0x10]`=item type id (index into pool 0x8b7060, 0x50 bytes/entry; name->id map at 0x8b4110) | map<X_LevelItem*,int> | 0x66ebd0 builds head (`new 0x18`; [obj+0x10]=head, [obj+0x18]=0); 0x66f650 inserts (0x6702e0) then `[eax+0x10]=ebx` (type id); 0x66f5c0 reads `[esi+0xc]` as the item | X_LevelItem vtable 0x7d8690 set by ctor 0x7195e0; factory 0x738ea0 / 0x738920 | M (key/value roles inferred from code, no live dump) |
| item position | `f32x3 at item+0x54` | vec3 | item is its own entity base (same getter family: 0x658b50 on `item` @0x719edf, on `char+0x1e` @0x4ff873) | | L-M |
| item in world / picked | room `[item+0x60]` != 0 = in the world, 0 = picked (0x642a80; 0x66f5c0). `item+0x132`/`+0x136` are the instance-name handle, both -1 = created at runtime (a dead enemy's gun: projectile `[Decal]` LI_CreateLevelItem -> slot 11 0x66f8c0, same map); those are erased from the map once picked | u32 | 0x642a80, 0x66f5c0, 0x66f8c0 (Ghidra + asm) | live dump (host/gamestate.c Item): see the l1i run | H (in world), M (runtime-created) |
| item type | `T=[item+0x13a]` X_SharedDBLevelItem: name rs chars `[T+4]` len `[T+8]` = the Level_Items file name (beretta, ammo_beretta, medipack_small = painkiller...); TriggerRadius f32 `T+0x34`, MatchingWeaponID i32 `T+0x38` (-1 none), PickupIfNewWeapon u8 `T+0x3c`, PickupIfAmmoNotFull u8 `T+0x3d`; OnPickUp lines count/array `T+0x42/+0x46` (e.g. `C_PickupAmmo("beretta",10)`). Type DB: container `[im+4]`, count +4, array +8, map<rs,int> at +0xc (0x8b4110 is only a scratch key). Pickup decision 0x68c690 (set at `[player+0x804]`), give 0x71a0f0 | | parser 0x528dc0 -> 0x529750 -> 0x544cf0 -> 0x5452b0 -> 0x5443e0 | game data x_data.ras dir Level_Items (23 types) | H |
| item activation | u8 `item+0x142` is stored per item in the level data (load virtual 0x7199c0 @0x719aa6; save 0x719ae0); runtime-created items start at 1 (0x719ec0 -> 0x71a500). The level script turns them on with `<item>->LI_ActivateLevelItem(true)` (handler 0x71a620, only while room != 0): closets, lockers, health boxes send it when their door opens (part1_level1: 15 lines, e.g. `::st01::closet02 000::ammo` @50155309); the door is a USE (type 0) trigger that disables itself | | 0x71a620, 0x7199c0, 0x719ec0 | live: 15/19 items off at load; after USE on st01 closets/lockers the medipack and ammo_beretta were picked (run l1l) | H |
| level REQUEST code | `u32(GM+0xb4)`: 0 none, 1 load level named GM+0xdc, 2 load slot [GM+0xb8], 3 save, 4 save (other) | u32 | consumer 0x451960: `0x40e4e0` (`mov eax,[ecx+0xb4]`), jump table 0x451e3c -> 0x4519c1 (case 1: 0x44fda0 with name GM+0xdc and flag [GM+0xec]), 0x451a8b (2, 0x40ff40), 0x451b17 (3), 0x451b4c (4); cleared 0x451cee, 0x451e20. Setter 0x44be30(code): called with 2/3/4 from ModeSwitch (0x417b1b, 0x417b8b, 0x417c0d) and with 1 from 0x4555fd and 0x45589d | | H |
| NEXT LEVEL NAME | std::string at GM+0xdc: char*=u32(GM+0xe0), len=u32(GM+0xe4), cap=u32(GM+0xe8); u8(GM+0xec)=0 (handler 0x4555f0) or 1 (handler 0x455890) | MSVC6 basic_string | handlers 0x4555f0 / 0x455890: `push 1; call 0x44be30` then copy of the message's name into `[edi+0xdc]` (0x45561b..0x455761, 0x4558bb..0x455a01); reset 0x44ee20 @0x44efce..0x44f013 | menu script `gm_init(startup_level)` -> message GM_Init (string 0x86b89c; flow.md). Which of the two handlers is GM_Init: UNPROVEN | H (fields), M (message) |
| LEVEL FINISHED signal | No engine-side "level finished" flag found. A level ends when level data (trigger/FSM script) sends GM_Init(<next>) (end of game: GM_SendEndOfGameMessages, string 0x86bab0). Poll GM+0xb4: 0 -> 1, name at cstr(u32(GM+0xe0)). After it, 0x451960 clears it and runs 0x44fda0; player wrapper [GM+0x1080] is set to 0 at 0x45246e (in 0x452410) and re-created by 0x451e70; AI vector is resized at 0x450298..0x4502f7 | | 0x451960 / 0x44fda0 chain | flow.md steps 2-3 | M |
| next level = levels.txt order? | UNPROVEN. The name arrives in the script message; no engine counter found; levels.txt parser not located. Driver rule: read the string at u32(GM+0xe0) when GM+0xb4==1 | | | | UNPROVEN |
| level exits | vector of X_LevelRuntimeExit*: begin=u32(L+0x82), end=u32(L+0x86) (GM+0x186 / GM+0x18a), 4-byte elements; exit name via 0x657990 (`mov eax,[ecx+7]`); u8 flags at exit+4,+5,+6 | | lookup 0x496250 (name compare over [L+0x82..L+0x86]); 0x496350 throws "Level doesn't contain exit "%s"" (0x876124); vtable 0x7d4f7c, ctor 0x657810 zeroes +4,+5,+6,+7,+0xb | message classes GM_EnableExit, GM_EnableExitAIActivation, GM_DrawExits (0x86b744, 0x86b7a0, 0x86b11c). What crossing an exit does: UNPROVEN | L |
| runtime-object map (triggers + dynamic objects + FSMs) | std::map at L+0x97: head=u32(L+0x9b), size=u32(L+0x9f); node value `[node+0x10]` = object ptr | | trigger insert 0x494852..0x49485c (`lea ecx,[ebx+0x97]; call 0x496d00; mov eax,[eax]; mov [eax+0x10],edx`); dynamic objects 0x494a3f..; FSMs 0x494c5b; iteration 0x494cff (`mov eax,[edi+0x9b]; mov eax,[eax]` ... `cmp eax,[edi+0x9b]`) | decompile rea/c/MaxPayne.exe/00493f80.c lines 290-420 | M |
| trigger objects (flat list) | std::vector at L+0xb7: first=u32(L+0xbb), last=u32(L+0xbf); 4-byte X_LevelRuntimeTrigger* | | 0x49481d..0x494830: `mov eax,[ebx+0xbf]; lea ecx,[ebx+0xb7]; push 1; call 0x497e20` = insert at end, only in the trigger loop; object `new 0x490` @0x494750, ctor 0x71e680 | vtable 0x7d880c stored at obj+0x150 (0x71e6ec) | M-H |
| trigger vs FSM tag | `u32(obj+0x150)`: 0x7d880c = X_LevelRuntimeTrigger, 0x7d75c0 = X_LevelRuntimeFSM (FSM obj is `new 0x461`) | | 0x71e6ec, 0x6e0512, 0x6e0208 | | H |
| trigger shape | sphere: centre `f32x3(t+0x72)` in the frame of room `u32(t+0x7e)` (same fields as a character), radius `f32(t+0x14a)` (= record f32 +0x94) | f32 | 0x658b80 `[ecx+0x12c]=r` on the entity base t+0x1e; 0x6e0750 passes the record's M43 translation (0x5e8190) to 0x658a90 -> 0x642ed0; test 0x71f120 (vtable 0x7d88a0 slot 14) sums r_char + r_trig (0x64ab30 -> 0x649a10) | .ldb records carry one float (0.2 door .. 4.0 room) | H static |
| trigger type / latches / name | type `u32([t+0x46a]+0x9c)`: 0 action button, 1 player collision, 2 projectile (strings 0x8a0fc4/fd4/fe8 in 0x71ecc0); enabled `u8 t+0x48e` (ctor sets 1, T_Enable 0x71f540 sets it and clears fired), fired `u8 t+0x48f` (0x71f0d0 fires only if enabled && !fired, then sets it); key "::room::name.TRIGGER" chars `u32(t+0x472)`, len `u32(t+0x476)` (0x71e830 strips ".TRIGGER" for the FSM name) | | B: 0x71f070, 0x40e480, 0x71f540, 0x71f0d0 | .ldb FSM init messages t_enable(false) | H static |
| correction (old row: record vec3) | the vec3 passed at 0x4947f1..0x4947fe is `[L+0x18],[L+0x1c],[L+0x20]` (three Level fields), not a trigger record field | | 0x4947eb `mov eax,[ebp-0x14]` = Level | | H |

## Driver recipe (guest addresses; read through the Wasm/native guest memory base)

    GM = u32(u32(u32(0x8a6338+0xc8)+0x80)
    enemies: b=u32(GM+0x24c), e=u32(GM+0x250); for p in b..e step 4: ai=u32(p); ch=u32(ai+0x533)
             dead = u8(ch+0x235)!=0 ; hp = f32(ch+0x2b3) ; pos = f32x3(ch+0x72) ; active = u8(ch+0x2dc) ; room = u32(ch+0x7e)
    player:  pw=u32(GM+0x1080); pc=u32(pw+8); same offsets.
    items:   im=u32(GM+0x97); n=u32(im+0x18); head=u32(im+0x10); walk map as in 0x66f5c0; item=u32(node+0xc); pos=f32x3(item+0x54)
    level change: u32(GM+0xb4)==1 -> new level name = cstr(u32(GM+0xe0)); returns to 0 when the switch starts.
    triggers: L=GM+0x104; for p in u32(L+0xbb)..u32(L+0xbf) step 4: t=u32(p); if u32(t+0x150)==0x7d880c.

Validate once at runtime (cheap): u32(GM+0x250)-u32(GM+0x24c) > 0 in level 1; u8(ch+0x235) flips after a kill; u32(im+0x18) > 0.

## Level exit (how a level ends)
There is no engine exit: a player-collision trigger fires T_Activate on the FSM of the same name; a DO animation step then sends
`maxpayne_gamemode->gm_init(<next>)` (GM_Init handler 0x4555f0/0x455890 sets GM+0xb4=1). The X_LevelRuntimeExit list (L+0x82)
holds room portals (`::room::ExitN`), not the level end. Chains (from the .ldb message strings, owner records by header):
- part0_level1 -> part1_level1: kill junkies `::babys_room::e1`, `::WorkRoom::e2`, `::storage::e3` (each sends
  `::bedroom::fsm_count->FSM_Send(n)`; on death: UNPROVEN) -> fsm_count -> `::bedroom::d_end_timer.DO` step 1 (1 s) ->
  T_Enable(true) on `::bedroom::end_trigger1.TRIGGER` (r 4.0) and `::storage::end_trigger1.TRIGGER` (r 3.0) -> player enters
  -> end_delay_timer -> End_Controller -> End_Randomize steps -> Exit2: `s_modeswitch(graphicnovel)`, `gm_init(part1_level1)`.
- part1_level1 -> part1_level1b: `::a5::t_door.TRIGGER` (type 1, r 0.2, enabled at load) -> FSM `::a5::t_door` ->
  `::a5::timer.DO` step 1 (0.5 s): GM_SetPlayerControls(false), fade, `gm_init(part1_level1b)`.
  a5 is only reachable by the service train, so the level is a scripted chain (offsets into 1_Part1_Level1.ldb; the driver's
  `steps` in tools/ghost/exits.json, one per row, done when its trigger fired or was enabled and went off):
  p5 `d_door_start::t_door` (@27555079) -> to_storage01 door (st01 cutscene gives the Beretta; enables `enemy_trigger`
  @27616906) -> `enemy_trigger` (`::p5::script` initialize @27616240 opens the stairs1 gate d_door01/02 @27558750) ->
  `stairs1::t_flash01` -> `bridget01::t_enemy_gate01` -> `p4::guard_init` -> `s + stairs1::alert_enemies` (`::p2::script`
  @27574642) -> `p2::t1` (opens r01) -> kill `::p2::e1` + `::r01::e1` (`r01::death_counter` enables `r01::pt1` @27566000;
  frees the cop) -> `hallway1::helper_to_open_door_trigger` (cop to `hallway1::w2` @27474010; his special sends
  `cam_rotator::camera1` start, the cutscene unlocks `door_dummy::door1` @29883105) -> keypad `hallway1::timer::trigger1`
  (type 0, USE; needed or not: UNPROVEN) -> `control_room1::door_dummy::door_trigger` (@27456113) -> `c::t_door`
  (GM_EnableExit(`::c::exit_to_power`) @29585936) -> `2nd_rail::control_desk::trigger` (type 0, USE: electricity_controller
  start @27379532, train_start_trigger on @27517470) -> `p2::service_train::train_start_trigger` (type 0, USE: camera1
  ride cutscene @27535978, exit_out on @30285488, player to `p2_tunnel::w1` @30998365) -> `::a5::t_door`.
- Next-level strings (one gm_init per .ldb): part0_level1 -> part1_level1 -> part1_level1b -> part1_level2 -> part1_level2b ->
  part1_level3 -> part1_level3b -> part1_level4 -> part1_level5 -> part1_level6 -> part2_level0 -> part2_level1 -> part2_level2 ->
  part2_level2b -> part2_level3 -> part2_level4 -> part3_level1 -> part3_level2 -> part3_level2b -> part3_level3 -> part3_level4 ->
  part3_level5 -> part3_level5b -> part3_level6 -> part3_level7 (no gm_init: end of game).
The driver's table is tools/ghost/exits.json; host/gamestate.c dumps the triggers (Trig, words 341-343).

## Rooms and exits (one frame for the level)
| field | path | proof | second source | confidence |
|---|---|---|---|---|
| room table | vector<X_LevelRuntimeRoom*> [L+0x41]..[L+0x45] (container L+0x39) | built by 0x655f90 (call at 0x4942f6) | .ldb: 38 rooms in Part1_Level1 | H static |
| room record / index / name | db = [room+8]; index [db+0x184]; name chars [db+0x14c], len [db+0x150]; own axes room+0x80, bbox +0x38/+0x44; no level placement | 0x655f90 | .ldb | M-H |
| exit list | vector<X_LevelRuntimeExit*> [L+0x82]..[L+0x86] (object L+0x7e), names "::room::ExitN" [[X+7]+4] | 0x4943e5 | | H |
| exit record | E = [X+0xb] (ctor 0x657810; [E+0xc8] = X at 0x4943dd); owner/destination room records [E+0x7c]/[E+0x80] (index +0x184); paired exit [E+0x84]; polygon f32x3 [E+0xc]..[E+0x10] in A's frame; normal E+0x18; enabled u8 [X+5], AI activation u8 [X+6] (GM_EnableExit / GM_EnableExitAIActivation) | 0x646020 @0x6460c9..0x64615c picks B via table[[[exit+0x80]+0x184]] | .ldb: all 74 Part1_Level1 exits pair up, t of a pair are exact negatives, cycles close with 0 mismatch | H |
| A -> B transform | p_B = p_A * R + t, R f32[9] at E+0x24, t at E+0x48; identity R in 1684 of 1686 exits of the game (2 rotated in Part1_Level2b) | 0x646020 | .ldb, AI-file matrix of room 11 = exit t | H |
One frame: BFS over exits from any room, off[B] = off[A] - t (identity R); p_level = p_local + off[room]. The AI movement
network splits at doors and the start room (part1_level1: 7 pieces, start room without nodes); the exits join them.
What exit enabled gates beyond the renderer's room flood 0x678050: UNPROVEN.

## Debug dumps already in the binary (oracles for the offsets above)
- 0x6d74f0: X_CharacterProperties (flags, health, AI active, weapon id, all labelled).
- 0x4f74e0, 0x502540: AI dumps ("Enemy seen: %d", "Enemy perceived", "Artificial player input").
- 0x495aa0: "Finite State Machines: ... Triggers:" dump. 0x44c1b0 prints "Level", "Level items", "GlobalAI" profile sections.

## Open (none blocks polling)
1. Enemy-vs-friendly relation table (0x8ab740 / 0x63bf40). 2. Kill counter. 3. Difficulty scaling of the pickup amounts (C_PickupAmmo counts).
4. What a crossed exit does; levels.txt consumer. 5. Trigger bounds. 6. Live confirmation of char+0x72 as world position.
