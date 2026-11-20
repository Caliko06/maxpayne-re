# MaxPayne.exe gameplay core

Names: rea/map/names/MaxPayne.exe.tsv (area = the subsystem below). Base 0x400000. FACT = read in a body or from RTTI or strings. INFERENCE = shape only.
Sources: Ghidra corpus rea/c/MaxPayne.exe, Hopper, capstone, RTTI (rea/map/MaxPayne.exe.vtables.tsv). The corpus holds 15702 of the 46238 lifted entries; 31530 of the missing are data-seeded (24215 of them EH funclets at 0x784000+), so a slot without a corpus file is checked with Hopper + capstone.
Hopper's own analysis of the exe stops at 7 procedures. A script must call seg.markAsProcedure(addr) before getProcedureAtAddress (rea sessions report "No procedure exists" for the same reason).

## Coverage
Rows added by subsystem. "Left" = addresses of the scope list (RTTI, strings, callers, blocks) with no name in the table: container and string instantiations, code blocks with no entry, and functions not read.

| Subsystem | understood strong | understood weak | named strong | named weak | scope list | left |
|---|---|---|---|---|---|---|
| Script and message system | 0 | 0 | 1578 | 103 | 344 | 88 |
| X_Character and the state machine | 265 | 14 | 80 | 113 | 1862 | 836 |
| Weapons and damage | 90 | 19 | 0 | 31 | 468 | 248 |
| Bullet time and shootdodge | 31 | 12 | 1 | 12 | 224 | 83 |
| Physics and collision | 5 | 19 | 13 | 22 | 101 | 68 |
| Animation | 10 | 19 | 12 | 63 | 404 | 180 |

Strong = two sources agree. Weak = one source or shape only: treat those names as a lead, not as proof. Evidence holding "two sources:" means a second pass read Ghidra and Hopper for that row. That pass corrected a large share of what it read (a name, or a claim beyond the code), so a row without it is less sure, even if strong. Where this text and the table differ, the table wins.

## Script and message system

Sources: Ghidra corpus rea/c/MaxPayne.exe, capstone disassembly of build/img/MaxPayne.exe, RTTI in rea/map/MaxPayne.exe.vtables.tsv.
The command table (91 rows) is after the prose, at the end of this file. X_Character and X_Projectile share some handler addresses (3D sound stubs 0x69c3f0..0x69c440).

### FACT: the text command pipeline
- A command is `receiver->Command(args);`. The separator "->" is the string at 0x87f8d0 /* 0x87f8d0 */. A trailing ';' is required.
- X_Message::parse = 0x51e9e0 /* 0x51e9e0 */ (fastcall: ecx=text, edx=scope table). 0x51e9c0 is parse(text) with no scope; 0x51e7c0 and 0x51e8c0 are SEH wrappers.
- Callers: GN loader 0x49cab0, animation messages 0x753860, GameMode/ModeSwitch code (0x40fff0, 0x44fda0, 0x453f50), level runtime (0x65db00, 0x683f50, 0x687390, 0x7086e0, 0x709030, 0x71ac80, 0x73aa70).
- No "->" means a broadcast: 0x51f9f0 creates the message, 0x51af50 checks that broadcast is allowed.
- Receiver text is resolved in this order:
  1. scope table (0x522500): the whole string, lowercased, looked up in a std::map<string,handle>. This is the `::room::object` form.
  2. global named receivers (0x512a00), for example `maxpayne_gamemode`.
  3. `ClassName:<number>`: 0x5127c0 maps the class name to an id, then "%d" reads the object id.
  4. else error UnknownReceiverName ("Receiver name '%s' couldn't be found").
- A handle is {objId, classId}; 0x511c40 is valid when both are not -1.
- Command lookup is 0x51f9f0: it walks the global prototype list (head DAT_008ac878) and compares `_strnicmp(proto->getCommandName(), text, strlen(name))`. So command names are case-insensitive (`gm_init` = `GM_Init`).
- It then needs '(' and a final ')'. The text between goes to the prototype clone's parseArgs (slot 5).
- Clone = 0x51e790: allocator slot 1 (allocate), then message slot 1 (assign from the prototype).
- Syntax check on a receiver-bound message = 0x51b120: slot 7 must allow direct send; receiver must be valid; then the checker (D+8, the "syntax checker" of the registration) is called.
- Exceptions (vtables): SemicolonMissing 0x7d0630, UnknownReceiverClass 0x7d05c4, UnknownReceiverObject 0x7d05a0, UnknownReceiverName 0x7d060c, UncompatibleReceiverClass 0x7d05e8, UnknownCommand 0x7d0678.
- Argument parser 0x51ff20 is a private sscanf: %c %d %i %u %f %s %x. %d/%i accept true/yes/on = 1 and false/no/off = 0, so `GM_SetPlayerControls(false)` stores 0.
- %f accepts a trailing '%' and multiplies by 0.01 /* 0x7cc260 */. %s accepts "quoted" text and stops at the next literal or comma.

### FACT: the developer console runs a line (2026-11-02, Ghidra corpus + capstone + web runtime; host/jump.c)
- `-developer` (0x40cb90 -> 0x8a6483) arms F12: 0x421820 sends MaxPayne_SystemMessage_ConsoleModeToggle (vt 0x7cbe0c) ->
  cycleConsoleOverlay 0x417c90 (states 0 -> 1 open -> 2 -> 3). On web the first F12 showed nothing, the second opened it.
- Console = [ModeSwitch+0x70], MaxPayne_ConsoleMode (ctor 0x429590, vt 0x7cc404; slot 14 0x611ac0 reads the typed-key ring
  0x517300/0x517320, Enter (vk 0xd) -> 0x611cb0). Per line: parse 0x51e9d0 -> 0x51e9e0 (ecx text, edx console+0x8a),
  send 0x51b6a0 (ecx message, edx console+0x9c, push 0, push 0; ret 8) at 0x611e1f, free 0x51fdf0.
- SEH wrapper 0x51e8c0: same ecx/edx plus one stack argument (the name its catch path reports, [ebp+8]); ret 4 (0x51e900).
  0x51e7c0 is the same with edx = that name and parse run with no scope (callers 0x40fff0, 0x453f50).
- From the menu, `maxpayne_gamemode->gm_init(X);` only queues the level; `x_modeswitch->s_modeswitch(game);` switches
  (the level scripts' own form is `x_modeswitch->s_modeswitch(graphicnovel)`). Proven on web: menu -> part1_level1 in 4 s.
- `player->c_teleport(::teleport::w1);` in play moved the player (-3.62, 2.80) -> (1.50, 0.15) (web, 2026-11-02).

### FACT: send and dispatch
- X_MessageQueue::send = 0x51b6a0 (named in rea/map). It calls enqueue 0x51b4c0. If the flag byte at 0x87f004 is 1 (initial value, "immediate"), it loops dispatch 0x51b730 until the queue is empty.
- 0x51b4c0 direct send: clones the message, stores descriptor D at entry+8 and objId at entry+0xc, links the entry (queue globals 0x8ac830 / 0x8ac858), logs "posted %s".
- Broadcast (message+8 == 0): iterates the message-type list at MT+0x24; each element is {D, objId}. With no receiver it reports an error.
- 0x51b730 pops an entry and calls the receiver object (D+0x18) slot 1 with (objId, handler member pointer D+4, message).
- X_MessageLock (ctor 0x771800, dtor body 0x771850): the first lock clears the flag at 0x87f004, so messages only queue. The last unlock drains the queue with 0x51b730 and restores the flag.
- Receiver slot 1 = deliver: RTDynamicCast the message to T, find R* in the object table (stride 13 bytes, valid byte at +0xc, base [0x8b84cd]) and call `(R*->*handler)(T*)`. Handlers are thiscall, one argument, `ret 4`.

### FACT: registration (206 R,T pairs, all checked)
- One function F<R,T> per pair registers R and T names (0x512080), builds the X_MessageReceiverAllocator<R,T> (0x54 bytes) and fills the entry. D = entry+0x10: D+0 message type id, D+4 handler, D+8 checker, D+0xc command name, D+0x14 receiver allocator, D+0x18 first receiver.
- Each F has one tiny static-init caller that pushes handler and checker. Handler is the last push, checker the third from last (4-arg form, ret 0x10) or the 2-arg form (ret 8). Checker may be 0.
- 0x723bd0 is the no-op placeholder (ret 4). Anchor: GM_Init handler 0x4555f0 matches the existing name row MaxPayne_GameMode::GM_Init.
- All 206 messages have default slot 6 (0x422170, broadcast not allowed) and slot 7 (0x422180, direct send allowed). So none of them can be sent without a receiver.

### FACT: message object layout
- X_Message vtable 0x7d0558: +0 vptr, +4 message-type info, +8 receiver descriptor (0 = broadcast), +0xc receiver objId, +0x10 bound flag.
- X_LaggedMessage (vtable 0x7d14bc, ctor 0x5846a0) adds an X_TimeUpdate (vtable 0x7d142c) at +0x11 and a flag at +0x2e; size 0x2f. Game messages derive from it; their own fields start at +0x30.
- Fields: %d byte at +0x30; %f float at +0x30; %s std::string with _Ptr at +0x34. PickupAmmo has the string at +0x30 and the int at +0x40.
- Message vtable slots: 0 sdd, 1 assign (RTDynamicCast + copy), 2 getCommandName (stub returns the string, for example "C_Reload"), 3 getArgFormat, 4 toString (_snprintf), 5 parseArgs (0x51ff20), 6 isBroadcastAllowed, 7 isDirectSendAllowed. GameMode messages add slot 8 assignTyped.

### FACT: template families (shape-hash checked on all instances)
- X_MessageAllocator<T> (3 slots): 0 sdd, 1 allocate (pop a free node, grow if empty, construct T), 2 release (RTDynamicCast, destroy, push to free list). Two R_DLList sub-objects: used at +4, free at +0x2c.
- X_MessageReceiverAllocator<R,T>: same shape for receivers. Reload example 0x6b8be0 / 0x6b8900 / 0x6b89b0.
- X_MessageReceiver<R,T> (6 slots): 0 sdd; 1, 2, 3 are the same body (deliver); 4 getReceiverClassId (shared by all T of one R); 5 isObjectValid (shared by R).
- R_DLList<T> / R_DLListBase<T,R_DLListBaseNode>: 0 sdd, 1 allocNode (operator new(8+payload) via 0x41e020, zero links, return node+8), 2 freeNode (delete p-8, ++sm_iDeleteCount; shared 0x6ed9d0).
- Folded stubs (one address, many classes) are listed once in messages.tsv with generic names: allocNode_nodesize_<hex>, getArgFormat_shared_<fmt>, toString/parseArgs_shared_fmt_<fmt>, stub_return_true/false.

### INFERENCE
- FACT (two sources): 0x753490 consumes the lag. For each message whose frame falls in the window, 0x582df0 at 0x7535f8 builds a temporary X_TimeUpdate, 0x5848c0 at 0x753608 copies it into the message at +0x11, 0x51b6a0 at 0x75362b sends it. The animation loader 0x753860 stores (time, message) pairs sorted with qsort and rejects non-lagged messages.
- Level-script dotted names (`::a5::t_door.TRIGGER`, `::a5::timer.DO`) are FSM or step names of the level data, not X_Message syntax. Only the commands inside the steps go through X_Message::parse. UNPROVEN link.
- Keys of the scope table are the full `::room::object` names (the lookup is a whole-string, case-insensitive map match).

### Still open
- MT+0x74/0x78 hook called as "slot 3" by 0x51b120 after the checker: purpose unproven.
- Where broadcast receivers (MT+0x24 list) and named receivers (`maxpayne_gamemode`) are registered; how the scope table is filled.
- The console dumps "Registered Messages / Broadcasts / Receivers" were not read.
- Handler bodies were read for C_Reload, C_SetHealth, C_PickupWeapon and GM_Init only; other handler names are from the registration pairing (weak rows).
- The table includes foreign messages that these receivers also handle (A_ audio, PS_ particles, CAM_, D_ decal, LI_Create). Their message classes belong to other slices.

### Command table
Columns: receiver class R, command (slot 2 string), argument format (slot 3), message class T, handler, checker ("-" = none).

| R | command | format | T | handler | checker |
|---|---|---|---|---|---|
| MaxPayne_GameMode | A_PlayMusic | `%s` | X_AudioMessage_PlayMusic | 42c210 | 42c200 |
| MaxPayne_GameMode | A_PlaySound | `%s, %s` | X_AudioMessage_PlaySound | 42c1f0 | 42c1e0 |
| MaxPayne_GameMode | GM_ChangeControlledGameSpeed | `%f, %f` | MaxPayne_GameModeMessage_ChangeControlledGameSpeed | 4562b0 | 456190 |
| MaxPayne_GameMode | GM_ChangeFog | `0x%x, %f, %f, %f` | MaxPayne_GameModeMessage_ChangeFog | 457790 | 4575f0 |
| MaxPayne_GameMode | GM_ChangeFOV | `%f, %f` | MaxPayne_GameModeMessage_ChangeFOV | 4564d0 | 4562e0 |
| MaxPayne_GameMode | GM_ChangeGameSpeed | `%f, %f` | MaxPayne_GameModeMessage_ChangeGameSpeed | 456000 | 455ee0 |
| MaxPayne_GameMode | GM_ChangeGlobalGameSpeed | `%f, %f` | MaxPayne_GameModeMessage_ChangeGlobalGameSpeed | 456150 | 456030 |
| MaxPayne_GameMode | GM_EnableBulletTime | `%i` | MaxPayne_GameModeMessage_EnableBulletTime | 4577c0 | - |
| MaxPayne_GameMode | GM_EnableBulletTimeBlock | `%i` | MaxPayne_GameModeMessage_EnableBulletTimeBlock | 457800 | - |
| MaxPayne_GameMode | GM_EnableExit | `%s, %i` | MaxPayne_GameModeMessage_EnableExit | 456990 | 456970 |
| MaxPayne_GameMode | GM_EnableExitAIActivation | `%s, %i` | MaxPayne_GameModeMessage_EnableExitAIActivation | 4569d0 | 4569b0 |
| MaxPayne_GameMode | GM_EnableGameLevel | `%s, %s` | MaxPayne_GameModeMessage_EnableGameLevel | 457380 | 457180 |
| MaxPayne_GameMode | GM_EnableWideScreen | `%d, %f` | MaxPayne_GameModeMessage_EnableWideScreen | 456580 | - |
| MaxPayne_GameMode | GM_Init | `%s` | MaxPayne_GameModeMessage_Init | 4555f0 | 4554d0 |
| MaxPayne_GameMode | GM_InitAndCalcAI | `%s` | MaxPayne_GameModeMessage_InitAndCalcAI | 455890 | 455770 |
| MaxPayne_GameMode | GM_ResetLevelItems | `` | MaxPayne_GameModeMessage_ResetLevelItems | 456860 | - |
| MaxPayne_GameMode | GM_ResetPerformanceMeasurements | `` | MaxPayne_GameModeMessage_ResetPerformanceMeasurements | 4570e0 | - |
| MaxPayne_GameMode | GM_ResetShootDodgeBulletTimeCounters | `` | MaxPayne_GameModeMessage_ResetShootDodgeBulletTimeCounters | 456870 | - |
| MaxPayne_GameMode | GM_SendEndOfGameMessages | `` | MaxPayne_GameModeMessage_SendEndOfGameMessages | 4577b0 | - |
| MaxPayne_GameMode | GM_SetCameraCollisions | `%i` | MaxPayne_GameModeMessage_SetCameraCollisions | 455d00 | - |
| MaxPayne_GameMode | GM_SetGameLevel | `%s` | MaxPayne_GameModeMessage_SetGameLevel | 456cd0 | 456bc0 |
| MaxPayne_GameMode | GM_SetPlayerControlledSlowMotion | `%f` | MaxPayne_GameModeMessage_SetPlayerControlledSlowMotion | 4575d0 | 4574c0 |
| MaxPayne_GameMode | GM_SetPlayerControls | `%d` | MaxPayne_GameModeMessage_SetPlayerControls | 456650 | - |
| MaxPayne_GameMode | GM_TransferPlayerWeaponTo | `%s` | MaxPayne_GameModeMessage_TransferPlayerWeaponTo | 456b30 | 4569f0 |
| X_Character | A_Play3DSound | `%s, %s, %s` | X_AudioMessage_Play3DSound | 69c400 | 69c3f0 |
| X_Character | A_PlayFloating3DSound | `%s, %s, %s` | X_AudioMessage_PlayFloating3DSound | 69c420 | 69c410 |
| X_Character | A_StopAll3DSounds | `%s` | X_AudioMessage_StopAll3DSounds | 69c440 | 69c430 |
| X_Character | C_AddHealth | `%f` | X_CharacterMessage_AddHealth | 699c10 | 699b00 |
| X_Character | C_AnimateTexture | `%d,%d,%f` | X_CharacterMessage_AnimateTexture | 699940 | 699920 |
| X_Character | C_CauseDamage | `%f` | X_CharacterMessage_CauseDamage | 699d40 | 699c30 |
| X_Character | C_DisplayCrosshair | `%d` | X_CharacterMessage_DisplayCrosshair | 699980 | - |
| X_Character | C_Dump | `` | X_CharacterMessage_Dump | 698ec0 | - |
| X_Character | C_EnableDOCollisions | `%i` | X_CharacterMessage_EnableDOCollisions | 69b9d0 | - |
| X_Character | C_EnableProjectileCollisions | `%i` | X_CharacterMessage_EnableProjectileCollisions | 69b9b0 | - |
| X_Character | C_ForceUpdate | `%f` | X_CharacterMessage_ForceUpdate | 699d90 | - |
| X_Character | C_GoTo | `%s, %f` | X_CharacterMessage_GoTo | 69b270 | 69afa0 |
| X_Character | C_GoToAndShoot | `%s, %f` | X_CharacterMessage_GoToAndShoot | 69b750 | 69b480 |
| X_Character | C_GoToPlayer | `%f` | X_CharacterMessage_GoToPlayer | 69b960 | - |
| X_Character | C_Jump | `%f` | X_CharacterMessage_Jump | 699820 | - |
| X_Character | C_KeepOrientationToPlayer | `%d` | X_CharacterMessage_KeepOrientationToPlayer | 69c100 | - |
| X_Character | C_Kill | `%d` | X_CharacterMessage_Kill | 69c390 | 69c280 |
| X_Character | C_Patrol | `%s, %s, %s, %f` | X_CharacterMessage_Patrol | 69bce0 | 69b9f0 |
| X_Character | C_PickupAmmo | `%s, %d` | X_CharacterMessage_PickupAmmo | 699570 | 699440 |
| X_Character | C_PickupWeapon | `%s` | X_CharacterMessage_PickupWeapon | 699120 | 698ff0 |
| X_Character | C_Reload | `` | X_CharacterMessage_Reload | 698fe0 | - |
| X_Character | C_RemoveAllWeapons | `%s` | X_CharacterMessage_RemoveAllWeapons | 6992f0 | 6991c0 |
| X_Character | C_RequestStateMachineChange | `%s` | X_CharacterMessage_RequestStateMachineChange | 69a7a0 | 69a510 |
| X_Character | C_SendSpecial | `` | X_CharacterMessage_SendSpecial | 699d80 | - |
| X_Character | C_SetAnimationDumping | `%d` | X_CharacterMessage_SetAnimationDumping | 699960 | - |
| X_Character | C_SetDodgeImmortal | `%i` | X_CharacterMessage_SetDodgeImmortal | 69a820 | - |
| X_Character | C_SetHealth | `%f` | X_CharacterMessage_SetHealth | 699ad0 | 6999a0 |
| X_Character | C_SetIdle | `%d, %d` | X_CharacterMessage_SetIdle | 69c240 | 69c130 |
| X_Character | C_SetImmortal | `%i` | X_CharacterMessage_SetImmortal | 69a800 | - |
| X_Character | C_SetInfiniteAmmo | `%s, %d` | X_CharacterMessage_SetInfiniteAmmo | 699790 | 699660 |
| X_Character | C_SetInputVisualization | `%d` | X_CharacterMessage_SetInputVisualization | 699800 | - |
| X_Character | C_SetInvulnerable | `%i` | X_CharacterMessage_SetInvulnerable | 69a840 | - |
| X_Character | C_SetStateMachine | `%s` | X_CharacterMessage_SetStateMachine | 69a4f0 | 69a140 |
| X_Character | C_SetTargetCharacter | `%s` | X_CharacterMessage_SetTargetCharacter | 69a030 | 699db0 |
| X_Character | C_SetWoundedAnimations | `%d` | X_CharacterMessage_SetWoundedAnimations | 6997e0 | - |
| X_Character | C_Teleport | `%s` | X_CharacterMessage_Teleport | 69ad60 | 69a860 |
| X_Character | C_Use | `` | X_CharacterMessage_Use | 699d60 | - |
| X_Character | CAM_AnimateAbsolute | `%s, %s` | X_CameraMessage_AnimateAbsolute | 69c9c0 | 69c9a0 |
| X_Character | CAM_AnimateInPlace | `%s` | X_CameraMessage_AnimateInPlace | 69cb50 | 69cb30 |
| X_Character | CAM_AnimateParented | `%s` | X_CameraMessage_AnimateParented | 69ca90 | 69ca70 |
| X_Character | CAM_AnimatePlayerParented | `%s` | X_CameraMessage_AnimatePlayerParented | 69caf0 | 69cad0 |
| X_Character | P_CreateProjectileToBone | `%s, %d, %s` | X_ProjectileMessage_CreateProjectileToBone | 69c6c0 | 69c4d0 |
| X_Character | PS_StartEffect | `%s, %s` | X_ParticleSystemMessage_PlayAnimation | 69c470 | 69c450 |
| X_Character | PS_StopAllEmissions | `%s` | X_ParticleSystemMessage_StopEmission | 69c4b0 | 69c490 |
| X_LevelItem | LI_ActivateLevelItem | `%d` | X_LevelItemMessage_ActivateLevelItem | 71a620 | - |
| X_LevelItem | LI_RemoveLevelItem | `` | X_LevelItemMessage_RemoveLevelItem | 71a680 | - |
| X_Projectile | A_Play3DSound | `%s, %s, %s` | X_AudioMessage_Play3DSound | 69c400 | 69c3f0 |
| X_Projectile | A_PlayFloating3DSound | `%s, %s, %s` | X_AudioMessage_PlayFloating3DSound | 69c420 | 69c410 |
| X_Projectile | A_StopAll3DSounds | `%s` | X_AudioMessage_StopAll3DSounds | 69c440 | 69c430 |
| X_Projectile | CAM_AnimateInPlace | `%s` | X_CameraMessage_AnimateInPlace | 708320 | 708300 |
| X_Projectile | CAM_AnimateParented | `%s` | X_CameraMessage_AnimateParented | 7082c0 | 7082a0 |
| X_Projectile | D_CreateDecal | `%s` | X_DecalMessage_CreateDecal | 708410 | 7083f0 |
| X_Projectile | LI_CreateLevelItem | `%s` | X_LevelItemMessage_CreateLevelItem | 7083b0 | 708390 |
| X_Projectile | P_AlertEnemies | `%f` | X_ProjectileMessage_AlertEnemies | 7081f0 | - |
| X_Projectile | P_CreateProjectile | `%s, %d` | X_ProjectileMessage_CreateProjectile | 708340 | 6e3710 |
| X_Projectile | P_Explosion | `%f, %f, %f, %f, %d, %d, %d, %d` | X_ProjectileMessage_Explosion | 708040 | 723bd0 |
| X_Projectile | PS_StartEffect | `%s, %s` | X_ParticleSystemMessage_PlayAnimation | 708270 | 708260 |
| X_Projectile | PS_StopAllEmissions | `%s` | X_ParticleSystemMessage_StopEmission | 708290 | 708280 |
| X_WeaponSet | A_Play3DSound | `%s, %s, %s` | X_AudioMessage_Play3DSound | 7586a0 | 758680 |
| X_WeaponSet | A_PlayFloating3DSound | `%s, %s, %s` | X_AudioMessage_PlayFloating3DSound | 7586e0 | 7586c0 |
| X_WeaponSet | A_StopAll3DSounds | `%s` | X_AudioMessage_StopAll3DSounds | 758720 | 758700 |
| X_WeaponSet | P_CreateProjectile | `%s, %d` | X_ProjectileMessage_CreateProjectile | 758790 | 6e3710 |
| X_WeaponSet | PS_StartEffect | `%s, %s` | X_ParticleSystemMessage_PlayAnimation | 758740 | 6e3690 |
| X_WeaponSet | PS_StopAllEmissions | `%s` | X_ParticleSystemMessage_StopEmission | 758770 | 6e36d0 |
| X_WeaponSet | WS_Animate | `%d` | X_WeaponSetMessage_Animate | 758360 | - |
| X_WeaponSet | WS_AnimateShooting | `%d,%d` | X_WeaponSetMessage_AnimateShooting | 758400 | - |
| X_WeaponSet | WS_Hide | `%d` | X_WeaponSetMessage_Hide | 758510 | - |

## X_Character and the state machine

FACT = read in a body or taken from RTTI/strings. INFERENCE = shape only.

### Entry points (FACT)
- C = X_Character*. Four vtables: 0x7d59d0 at C+0 (slot0 deleting dtor 0x682fd0, slots 1..9 = 0x694300 0x6942c0 0x6942e0 0x692e40 0x692f90 0x694ef0 0x694d30 0x694fe0 0x694ac0), 0x7d5950 at C+0x1e (main object, 31 slots), 0x7d59fc at C+0x15b (thunks 0x694350/0x694310/0x694330), 0x7d5930 at C+0x17c (CameraTarget, 7 slots 0x693070..0x6930d0).
- Ctor 0x682ab0, dtor 0x6831e0, release 0x683640, reset 0x6838f0. vt1e slot1 load 0x691d00 and slot2 save 0x692890 use this = C+0x1e (Ghidra offsets there = C offset - 0x1e). Runtime state save/load: 0x692c10 / 0x692c60 (CharacterState, weapon set, C+0x454, C+0x456, C+0x770).
- vt1e slot3 0x6911e0 builds the world matrix of the P_BaseObject [C+0x53c]. Slot 19 0x695070 contact dispatch, 25 0x6954f0 contact collect, 21 0x696f60 line test (physics), 29 0x6976f0 hit entry (weapons).
- Per-frame update 0x687f20 (profile "Character"): teleport C+0x6d5 (0x6973a0), 0x6879e0 (consume globals 0x8b4f54 heal, 0x8b4f58 timed bonus, 0x8b4f5c bullet-time bonus), 0x6877b0 updateTimers (no health write: no regeneration here; timed-mode clock C+0x76c behind gates, dies at 0), 0x687ba0 updateActivity (recomputes C+0x201 and the other activity flags). If C+0x200 or C+0x21c: 0x68f480, 0x68c960 updateLowerBody (Char.LowerB; see Physics: not a controller read), 0x68ebd0 (aim ray), 0x68df90 ("Char.UpperB"), 0x68f600 (dead body), 0x6966a0 findUseTrigger (a search that returns a pointer, not an update), 0x68ba90 ("Shadows"), 0x68c690, 0x68c550, then 0x6680c0 (animation). Other sections: 0x688c70 "2DAiming", 0x689550 "LookAt".
- Health: max 0x691bd0, fraction 0x694480, heal 0x695a80 (clamped, excess to C+0x72c), applyDamage 0x695eb0 (per-body-part multiplier from params, then 0x695b10), setHealth 0x695fb0, die 0x6963f0, death type pick 0x696290. A kill calls 0x687b00, which adds rewards to the three globals above. 0x687b80 takes and clears C+0x790.

### X_CharacterProperties ST = C+0x22e (string at the head of dump 0x6d74f0) (FACT, names from the dump 0x6d74f0 labels)
Offsets are ST-relative (C offset = +0x22e). +2/+3/+4 activation groups 1/2/3; +5 getting damage; +6 triggers damage animation; +7 dying/dead (byte); +9 wounded; +0xa aiming; +0xb aim pitch; +0x3f airborne; +0x40 falling; +0x41 play landing anim; +0x43 shoot-dodge allowed; +0x44 can crossfade to shoot-dodge; +0x45 can end shoot-dodge; +0x46 is shootdodging; +0x47..+0x66 array of 8 dwords (0x6d70e0); +0x67 float shoot-dodge heading; +0x72 can use; +0x73 can lookAt; +0x74 crouching ratio; +0x78/+0x79 two-handed anims / bone groups; +0x7a weapon changed; +0x7b current weapon id; +0x7f next weapon id; +0x85 health; +0x89 send OnActivate; +0x8a shooting frequency multiplier; +0x8e..+0xaa AI floats (aiming speed, turn, aim up/down, aiming cone, visual radius, general radius, reaction time, shooting cone); +0xae AI active; +0xaf ally aiming; +0xb0 enemy aiming; +0xb1 update required; +0xb2 idle variant 1..4; +0xb7 death type; +0xc7 selected weapon id. Unlabelled but present: +0x6b float, +0x71, +0x83, +0x84, +0xb6 bytes.
- Constants: 0x7cbdd0 = 1.0, 0x7cb8b0 = 0.0, 0x7d5cf0 = 4/pi (heading * it = 8 sectors, 0x68c850), 0x7d9f08..0x7d9f14 = pi/4, 3pi/4, 5pi/4, 7pi/4 (DodgeSpecific compares ST+0x67).
- 0x68c850 (FACT): sector = round(heading*4/pi); blends array entries sector and sector+1 (mod 8) with weight 1 - frac. INFERENCE: the array holds per-direction shoot-dodge animation ids.

### Controller chain (FACT unless marked)
- C+0x2f9 is an X_CharacterInput (vtable 0x7d9268, 4 slots: dtor 0x742330, dump 0x743d60, reset 0x7425d0, update 0x7429b0). Its +5 holds the controller pointer (0x751da0 reads it; 0x6917b0 getController), its +9 the CharacterState, +0x62/+0x66/+0x6a/+0x6e four state ids (0x7fffffff = none; +0x6a lower body, +0x6e upper body).
- setController 0x691750: null detaches (controller vfn +0x9c); a controller gets vfn +0x98(ST).
- Controller interface = X_CharacterInputBase vtable 0x7d8af0 (42 slots, 1/2/20..41 pure). Input slots used by the states: 0 run axis, 1 strafe axis, 7 jump, 8 crouch, 9/10/11/12 jump+strafe-left/right/run-fwd/run-back, 14/15/16/17 dodge left/right/fwd/back, 19 aim left-right, 21 use pressed, 46 (+0xb8) false for the player, gate of the jump combos (meaning open). Controller +0x90/+0x94 = upper/lower body state id, +0xa0 reset, +0xa4 dump.
- X_CharacterInput::update (FACT, first 90 lines): forwards use/jump edges, calls controller +0x18(dt), reads the two ids; lower id 0xd (crouch) raises ST crouch ratio, 0xc (idle) lowers it, rate = dt / table duration (0x6de6b0, table stride 24 per state id). INFERENCE: the remaining 16K selects animations from the two ids.

### State machine (FACT, bodies read)
- X_PlayerInputEvaluator vtable 0x7d8c18, ctor 0x72a2d0 builds 12 states. Each state = X_PlayerInputStateBase (0x7da0d4) over StateBase (0x7da4c0; +4,+5 active bytes, +6 changed, +7 dword state id, +0xc input, +0x10 ST). Vtable slots: 0 load, 1 save, 2 evaluate(int* id)->bool, 3 canInterrupt, 4 onEnter(id) (+5=0, +7=id, +6=1), 5 onLeave, 6 clear active, 7 set active, 8 reset, 9 false.
- Ids: Movement 0x765eb0: 1..9 run/strafe (thresholds 0.5, 0), 0xa/0xb turn (aim axis vs float at state+0x15), 0xd crouch. StandIdle 0x766400: 0xc, 0xe 0xf 0x10 0x11 by ST+0xb2. FallLand 0x7661a0: 0x2a fall (leaves on +0x40 falling), 0x2b land (needs 0x2a active). Jump 0x7651c0: 0x26 jump, 0x27 +run, 0x28 +strafe-left, 0x29 +strafe-right. Use 0x764e70: 0x2c. GetDamage 0x765020: 0x2f/0x30 (crouch). WeaponSpecific 0x765530: 0x2d, 0x2e, more dynamic. DodgeSpecific 0x764180: 0x12..0x19 dodge, 0x1a..0x21 shoot-dodge, 0x22..0x25 landing by heading quadrant. Death 0x764b40 by ST+0xb7: 0->0x31 1->0x32 2->0x33 3->0x34 4->0x35 5->0x37 6->0x39 7->0x3b 8->0x3c 9->0x36 10->0x38 11->0x3a. Peek, Turn180: no evaluate (slot 2 = false).
- INFERENCE: the id to animation-name map (DodgeRight, ShootDodgeForward at 0x87c4xx) is a table keyed by id; not located. AI scripts (0x4d3e80, 0x4d4580, 0x4d78f0) use the same names (ghost).

### Messages (FACT)
- 44 handlers registered by static-init stubs 0x69cba0..0x69dbc0 (stride 0x60) through registrars 0x6a0680..0x6b63e0. 33 X_CharacterMessage_* + 11 foreign (Audio 3D sound x3, ParticleSystem x2, Projectile CreateProjectileToBone, Camera Animate x4). Handler bodies: SetHealth 0x699ad0 (range 0..1, times max health), AddHealth 0x699c10, CauseDamage 0x699d40, Kill 0x69c390, Teleport 0x69ad60, PickupWeapon 0x699120, Use 0x699d60, SetStateMachine 0x69a4f0; all in the tsv.
- Message class vtable: 0 deleting dtor, 1 assign, 2 name ("C_SetHealth" 0x8a2e00), 3 arg format ("%f" 0x86bbdc), 4 format args, 5 parse args (calls 0x51ff20), 6/7 false/true.

### Open
- Not located: controller +0xb8 meaning, 0x72ace0 (16K bitset of per-state masks), ShootingBase, bodies of GoTo/Patrol/GoToAndShoot, 0x74xxxx factory/skin builders (0x744230, 0x746f10), vt0 slots 4..9 (collision side), 0x7429b0 tail.
- Corrections to rea/map: (1) player.md "X_CharacterInput" interface used by the states is the controller at C+0x2fe (MaxPayne_PlayerInput for the player), not X_CharacterInput itself (0x7d9268, a bridge object). (2) vtable 0x7d5930 is the CameraTarget interface at C+0x17c, not the main class; the main one is 0x7d5950 at C+0x1e. (3) 0x765c00 (state canInterrupt) is a jmp to 0x6d6e90, which on a state object reads the StateBase +5 byte, not ST health damage.

## Weapons and damage
FACT = read in code or data. INFERENCE = marked. Rows: weapons.tsv. Open items: weapons.unknown.tsv.

### Entry points
- Shot: script frame message -> X_WeaponSet::onMessage_WS_AnimateShooting 0x758400 -> weapon SHOOT animation frame 0
  message P_CreateProjectile -> 0x758790 -> thunk 0x743fc0 -> interface vcall +0x2c (X_ProjectileImplementation 0x7d5088 slot 11).
- Spawn: X_ProjectileImplementation::createProjectile 0x65ae10 -> X_Projectile::ctor 0x7065d0.
- Flight and hit: X_Projectile vtable 0x7d8134 slot 19 = 0x70a0d0, callbacks 0x7097d0 (character), 0x709740 (dynamic object), 0x709540 (trigger).
- Damage: X_Character::causeDamageFromHit 0x695eb0 -> causeDamage 0x695b10 -> setHealth 0x695fb0 -> die 0x6963f0.
- Reload: message C_Reload -> 0x698fe0 -> X_WeaponArsenal::reload 0x729b30. Pickup: 0x699120 (weapon: sets the owned flag only, adds no ammo), 0x699570 (ammo). C_RemoveAllWeapons 0x6992f0 leaves infinite ammo on the weapon it keeps.
- Explosion: message P_Explosion -> 0x708040 -> 0x64d230 (radius query) -> collider vslot 29 (X_Character: 0x6976f0) -> 0x695eb0.

### Flow of a shot (FACT unless marked)
1. Character animation frame message RightHandWeapon->WS_AnimateShooting(SHOOT,SHOOTEMPTY) reaches 0x758400.
2. 0x758400: clip [ws+0x1cb] > 0 and mesh has the animation named by message+0x30 (SHOOT, from data) -> play it, consume a round (0x757f50: clip--, shots[+0x223]++).
   clip == 0 and SHOOTEMPTY [tbl+0x34] exists -> play that instead. No round is consumed on empty.
3. SHOOT animation frame 0 carries this->P_CreateProjectile(bullet_x,1) (from 310_<weapon>.txt) -> 0x758790 builds position/dir
   from [ws+0x231/0x23d/0x249] and calls the interface held at [ws+0x17c].
4. 0x65ae10: looks name up in map 0x8b3ee8 (request name copied by validateCreate 0x65aaf0, no case folding in that body; unknown name throws ProjectileNotFound, amount<1 InvalidAmount).
   For Debris or BloodProjectile defs it skips the spawn when rand()/32767 /* 0x7cfc98 */ >= detail level (0x65a390).
5. 0x7065d0, spread (FACT, from asm): m = rand() * Accuracy[pdef+0x5e] * 1.2207e-8 /* 0x7d81b8 */ = (rand/32767) * Accuracy/2500;
   dir = normalize(dir + randUnit * m) with randUnit from 0x712870 (rejection sampling, components rand()*6.1039e-5-1); then rotated
   by the shooter matrix (param_7) and normalized.
6. Speed: v = dir * Speed[pdef+0x38] * (1 + SpeedRandom[pdef+0x62] * (2u-1)), u = rand/32767 (const 6.1037e-5 /* 0x7d5d00 */ = 2/32767).
7. Gravity vector = GravityMultiplier[pdef+0x6e] * g (set by 0x72c290). Fire rate is not here (see Open).
8. Per frame 0x70a0d0 sweeps the motion segment, collects hits for X_Character /* 0x8684c8 */, X_LevelRuntimeDynamicObject
   /* 0x8683c0 */, X_LevelRuntimeTrigger /* 0x892ed0 */, sorts by distance, calls the callbacks. Characters need DamagesCharacter
   [pdef+0x74], not Debris [pdef+0x51], owner != target, and proj+0x265 == 0.
9. 0x7097d0 calls 0x695eb0(pdef.Damage [+0x3c], mode, attacker, hitBoneName) at 0x70a030. mode from DeathCause [pdef+0x7d]:
   1 -> 1 (shotgun), 2 -> 2 (burning), 3 -> 5, else 0. Then it runs the CharacterHit script and ends the projectile unless Debris.

### Damage formula
- Bullet: HP_loss = pdef.Damage * Multiplier(group of hit bone). FACT: 0x695eb0 loops the character params Target groups
  (count [params+0x7d] via 0x60eea0, group i = [params+0x81][i], bones = [g+0x18] + j*16 name at +4), matches with _strcmpi,
  Multiplier = float [g+0x10] (0x7269f0); no match -> 1.0. Then 0x695b10(mult*dmg): health_new = health - amount (0x695fb0).
- No difficulty term in this chain (FACT: 0x695eb0, 0x695b10 read). Difficulty also scales max health (0x691bd0), ammo found (0x699570), auto-aim, regen and bullet time, and it adapts per level: difficulty.md.
- Data (FACT, x_data.ras): Damage per hit: Beretta 5, Desert Eagle 10, shotgun pellet 5, Ingram 5, MP5 10, Sniper 70 (shooting.h);
  zone multipliers head 5, arms 0.75, legs 0.5 (characters.h MULTIPLIER_*). Max health: Max 60 (179_max_payne.txt),
  enemies HEALTH_CLASS_1..10 = 15,20,25,30,35,40,45,70,130,240 times EnemyHealthMultiplier.
- Explosion: value(d) = inner value if d <= r_in; lerp (t=(d-r_in)/(r_out-r_in); t*v_out + (1-t)*v_in, 0x64d640) to r_out; 0 beyond r_out
  (0x64d230). The collider receives value as damage in vslot 29. Script args: P_Explosion(r_in, r_out, v_in, v_out, fire, blast, melee, x).
  0x6976f0 (FACT): fire -> mode 2; blast -> mode 4 and an impulse; neither -> mode 0; melee flag -> skips the owner.
  Examples: grenade (2,5,50,10 blast), M79 (1,5,50,5 blast), bat (1.2,1.201,15.01,15 melee), pipe (0.8,0.801,9,8.99 melee).
  INFERENCE: flag order fire/blast/melee/x matches the data (molotov files set only the first, grenade the second, bat/pipe the third).
- Bone name for explosions is the static string 0x8b4908 (empty by inference), so the Target multiplier is 1.0.

### Data layout
- Weapon def (X_SharedDBWeapon, parser 0x53b0e0, Variables table 0x882df8, filled by static init 0x53ab80): +0x34 TwoHandedBones,
  +0x36 UseMultipleGunBones, +0x39 SlotIndex, +0x3d InventoryID (ammo pool), +0x41 WeaponID, +0x45 ClipSize, +0x49 PocketSize,
  +0x4d ReloadAmount (-1 = ClipSize), +0x51 ProjectileOffset vec3, +0x5d MaximumShootingFrequency, +0x61 DefaultShootingFrequency,
  +0x65 ShootingFrequencyMultiplier, +0x69 SidewaysAimingMultiplier, +0x72.. ShakeLength/Strength (x0.0004 /* 0x7d0bfc */), +0x9c animation set.
- Projectile def (parser 0x540490, table 0x883930, init 0x53ff90): +0x38 Speed, +0x3c Damage, +0x40..0x48 Ricochet*, +0x51 Debris,
  +0x52 Direction vec3, +0x5e Accuracy, +0x62 SpeedRandom, +0x66 CollisionExplosion, +0x67 DelayedExplosion, +0x68 ExplosionDelay,
  +0x6c RandomizeDirection, +0x6d RandomizeRotation, +0x6e GravityMultiplier, +0x74 DamagesCharacter, +0x7d DeathCause,
  +0x81 AdultContent, +0x82 BloodProjectile. Sub-blocks CharacterHit/Sniper/Decal at +0x87..+0xaf (16-byte elements).
- Arsenal (X_WeaponArsenal, at X_Character+0x398): +4 DB, +8 current weapon id (-1 none), +0xc two X_WeaponSet (0x26d bytes each, one per hand),
  +0x10 ammo pools (30 bytes: +0 cur, +4 clip max, +8 pocket max, +0xc infinite, +0xd map<int,bool> owned).
- X_WeaponSet: +0x17c projectile interface, +0x180 animation player, +0x1bb mesh, +0x1bf weapon id, +0x1cb clip rounds, +0x223 shots fired.
- Difficulty (parser 0x6770a0): +0 EnemyHealthMultiplier, +4 AmmoFoundMultiplier, +8 PlayerHealthMultiplier, +0xc/+0x10 AutoAim capsule radius/height,
  +0x14 HealthRegenerate, +0x18 HealthGainedImmediately, +0x1c BulletTimeGainedPerKill. Object at [Character+0x490].

### Reload, switch, pickup, ammo (FACT)
- Reload 0x729b30: unloadCurrent 0x729a10, then pool[+4] += ReloadAmount (wdef+0x4d), where +4 is the limit loadClip reads (getLoadLimit 0x7482e0; meaning of that limit not settled), then loadClip 0x729a70 (min(pool, ClipSize); dual weapons 2x clip split on hands).
- Switch 0x729940: unload old (0x729a10), set id, attach mesh to hand 0 (and hand 1 if +0x36), autoReload 0x729b80.
- Pickup ammo 0x699570: n * AmmoFoundMultiplier (0x676380), at least 1 if n > 0, then 0x729bb0 clamps to PocketSize minus loaded rounds.
- Infinite ammo: pool flag +0xc; getAmount returns +8 instead of +0 (0x748250).

### Open
- Fire-rate timer lives in X_CharacterInput 0x7429b0 (asm only, character slice): 1/(MaximumShootingFrequency*f), f = 0x6d72f0. Not verified.
- Melee, bullet-time effect on projectile speed, who calls 0x6917c0 (weapon change), X_WeaponFactory, X_ProjectileMeshFactory, ToBone path
  (0x65aff0, 0x705980), projectile load/save: not read.
- Does PlayerHealthMultiplier scale incoming damage? Only max health scaling is proven.
- Dynamic-object damage: 0x6f4900 hitpoints at +0x4a9 (FACT); message names after it unknown.
- Contradictions with rea/map: 0x541110 is named X_InputDeviceMouse::getInvertMouse in the table. Its body is one line (return byte [this+0x51]), the X_SharedDBProjectile Debris getter reads the same offset, so it may be one folded function: which callers use it was not checked;
  player.md "[wdef+0x41] slot group" is WeaponID, the slot field is SlotIndex +0x39; player.md pool "+4" is max clip over the pool.

## Bullet time and shootdodge

Sources: Ghidra corpus + capstone disasm of the exe, owner data (x_data.ras 1_globalconfig.txt, 179_max_payne.txt, 1_difficulty.txt).
GM = MaxPayne_GameMode, ST = X_CharacterProperties (C+0x22e), dt = real frame time.

### FACT: time model
- Mode timer = X_TimeUpdate at GM+0x57 (0x60dbf0). Advance 0x583060(dt): +4 = dt*scale, +8 += previous +4 (game time), +0x10 = dt (real), +0x14 += previous real, +0x18 = frame count. Scale = timer+0xc (0x766c60, getter 0x44eba0).
- Anything that reads timer+4 (0x5e53d0) gets scaled dt; reading +0x10 (0x7269f0) gets real dt. Characters sub-step with scaled dt (0x687f20 loop).
- Scale is set once per GM tick by 0x44ea00 at the end of 0x44c1b0 (0x44e929), clamped to <= 1.0 /* 0x7cbdd0 */.

### FACT: three game speeds (GM state copies of the three messages)
- GM+0x10e8 GameSpeed (GM_ChangeGameSpeed 0x456000), GM+0x1128 ControlledGameSpeed (0x4562b0), GM+0x1168 GlobalGameSpeed (0x456150). Args (%f, %f) = (target speed, duration s).
- Message fields: +0x2f target, +0x33 duration, +0x37 remaining, +0x3b start. Handler: copy message, start = current scale (0x44eba0), remaining = duration.
- Each tick 0x4312a0(real dt, target): remaining -= dt; t = remaining/duration; speed = (1-t)*target + t*start while remaining >= 0 and duration > 0; else target. Linear, real-time.
- Combine (0x44e845-0x44e929): g = Global, m = min(Game, Controlled). scale = (g != 1.0 or any of the three tests on [GM+0x93] vslots +8,+0xc,+0x10 is true) ? g : m. Then setGameTimeScale.
- Global speed is set by data: sniper bullet cam GM_ChangeGlobalGameSpeed(0.01, 0) (63_bullet_sniper.txt); DOExitMessages ChangeGameSpeed(1,0).
- setGameTimeScale also sets sound pitch: pitch = min + (1-min)*scale for music, 3D, 2D, with min = Music/3D/2D MinimumPitchMultiplier from [cinematic] (1.0, 0.15, 1.0) via cfg [GM+0x1230] +0,+4,+8 (INFERENCE for which field is which: string order 0x87de80,0x87de9c,0x87debc).

### FACT: bullet-time state (GM fields)
- +0x12f0 float meter 0..1; +0x12f4 byte active; +0x12f8 float heartbeat countdown; +0x12fc byte block (EnableBulletTimeBlock); +0x1258 button tracker; +0x1300 activations; +0x1304 shoot-dodges; config object [GM+0x10cc] (parser 0x50f8d0).
- Config offsets: +0 Threshold, +4 TurnOffPenalty, +8 SlowMotionTime, +0xc RegenerationTime, +0x10 ShootDodgePenalty, +0x14 ShootDodgeThreshold, +0x18 MusicVolume, +0x1c MusicVolumeFadeTime, +0x20 HeartbeatSoundInterval, +0x24 HeartbeatSoundMessage, message lists Start (count +0x38), End (+0x44), CantSwitchOn (+0x50).
- DATA (1_globalconfig.txt [slowmotion], percent = x0.01): SlowMotionTime 15.0 s; RegenerationTime 1e8 s (no time regen); Threshold 0.1%; TurnOffPenalty 0%; ShootDodgePenalty 10%; ShootDodgeThreshold 1%; MusicVolume 0.0; MusicVolumeFadeTime 0.5 s; HeartbeatSoundInterval 1.25 s.
- DATA Start: GM_ChangeControlledGameSpeed(0.2, 0.3), sound slow_motion, HUD flash. End: GM_ChangeControlledGameSpeed(1.0, 0.5), sound slow_motion_end. CantSwitchOn: sound Shoot_NoBulletsLeft.
- New level / reset: meter = 1.0 (0x44ff26), counters 0, active 0, block 0 (0x44f13e). Save/load keeps 0x12f0,0x12f4,0x12f8,0x12fc (0x454fa0, player.md).

### FACT: per tick (0x44cbbf-0x44d11f), only when [GM+0x93] tests are all false and GM+0x12ce == 0 (else the whole block is skipped: the meter is frozen)
1. With a player: meter += takeBulletTimeBonus(C) * Difficulty.BulletTimeGainedPerKill; clamp 1.0. takeBulletTimeBonus 0x687b80 returns C+0x790 and clears it. Kill reward = SlowMotionBonusForKiller of the victim (10% for most, data) added by the character slice (0x687b00).
2. Button tracker GM+0x1258 (PlayerInput GM+0x258, isBulletTimeDown 0x4cea50): 0 idle -> 1 when the key goes down. In 1: if any dodge direction (PlayerInput vslots +0x38..+0x44) or run/strafe axis != 0 -> 2 (key is a shoot-dodge modifier, no toggle). In 1 with no movement: key released -> 0 and a TAP is recorded. In 2: key released -> 0.
3. If isSlowMotionPressed (0x4cea60) or TAP: if block (+0x12fc) is set and BulletTimeAid option (0x4554c0, default 1) is on, nothing; else toggle 0x44ebb0. Toggle fails (meter <= Threshold) -> sendCantSwitchOn.
4. ST+0x6f = (meter > ShootDodgeThreshold). If ST+0x70 (shoot-dodge started) is set: ++GM+0x1304; if bullet time is NOT active: meter -= ShootDodgePenalty, floor 0. Clear ST+0x70.
5. Active: meter -= dt / SlowMotionTime (0 if SlowMotionTime == 0). meter < 0 -> meter = 0, sendEndMessages, active = 0 (no TurnOffPenalty here). Heartbeat: GM+0x12f8 -= dt; < 0 -> send HeartbeatSoundMessage, = HeartbeatSoundInterval. Music volume moves toward MusicVolume at dt / MusicVolumeFadeTime per s (listener 0x677830).
6. Inactive: meter += dt / RegenerationTime (0 if 0), clamp 1.0; music volume fades back to 1.0 at the same rate.
- Full bar lasts 15 s of real time (drain at 0x44cec8-0x44ced6 reads the real dt, timer +0x10; two sources); a shoot-dodge costs 10% (1.5 s); needs > 1%.

### FACT: shoot-dodge (data + code)
- Input: dodge direction with the bullet-time key held (modifier mode 2) or the plain dodge key. DodgeSpecific::evaluate 0x764180 (character slice): any dodge press sets ST+0x71 (0x6d7190). If ST+0x43 (shootdodge allowed) and ST+0x6f are true it picks a shoot-dodge id 0x1a..0x21 (8 directions, heading ST+0x67 vs pi/4..7pi/4); else a plain roll 0x12..0x19. onEnter 0x764840 of a shoot-dodge id sets ST+0x70 = 1.
- Data 179_max_payne.txt: CanShootDodgeFrame 13 (blend dodge -> shootdodge until frame 13), CanAbortShootDodgeFrame 50. Shoot-dodge forward anim: frame 1 sound, frame 10 GM_ChangeGameSpeed(0.2, 0.3), frame 12 GM_EnableBulletTime(false) + GM_EnableBulletTimeBlock(true), frame 17 C_Jump(PLAYER_SHOOTDODGEJUMP 4.5), frame 30 GM_ChangeGameSpeed(1.0, 0.5), frame 1 C_SetDodgeImmortal(true), 49 false. Movement EndPosition 5.25 m (forward/back/side), diagonal 3.5/3.25.
- Get-up anims: frame 0 GM_ChangeGameSpeed(1.0, 0.5), frame 20 GM_EnableBulletTimeBlock(false). Roll dodge: frame 0 ChangeGameSpeed(0.5, 0.0), frame 35 (1.0, 0.5). Death anims: GM_EnableBulletTime(false) at frame 0.
- GM_EnableBulletTime(b) 0x4577c0: only if BulletTimeAid option on: b false and active -> toggle off; b true and inactive -> toggle on. GM_EnableBulletTimeBlock(b) 0x457800: block = b. GM_ResetShootDodgeBulletTimeCounters 0x456870: counters 0x1300/0x1304 = 0 (menu start, tutorial). GM_SetPlayerControlledSlowMotion(f) 0x4575d0: meter = f (0..1, checker 0x4574c0); console GetBulletTime = (1).
- Because ChangeGameSpeed (0.2) and ChangeControlledGameSpeed (0.2) are min-combined, shoot-dodge inside bullet time does not stack below 0.2.
- DodgeTestSphere / CharacterTestSphere (vtable 0x7cff0c / 0x7d5d10, derive from X_LevelRuntimeSphereObjectBase): temporary sphere objects. DodgeTestSphere is built in 0x501b30 for a 0.35 s cached probe (0x501810 and 3 siblings): move to pos + 3 m along a matrix row, then 2 m down. INFERENCE: it tells the player input whether a dodge/shoot-dodge direction has room. 0x501d50 is its vslot 31 (forwards to SphereObject move 0x6590d0).

### Port note (INFERENCE)
- To keep the feel: scale applies to ALL game dt (timer+4); drain/regen/fades use real dt (timer+0x10); speed blends are linear in real time.

### Open
- Who reads timer scale in X_Projectile speed, character animation rate and camera: not traced. Player at normal speed in bullet time: UNPROVEN.
- Meaning of the three tests on [GM+0x93] (+8,+0xc,+0x10) and of GM+0x12ce: UNPROVEN (menu/cutscene/pause by position in canSave).
- No contradiction with rea/map/player.md. It lists only part of the config (+0,+8,+0xc,+0x14,+0x1c) and says GM+0x12f8 / GM+0x12fc roles are not decoded: here 0x12f8 = heartbeat countdown, 0x12fc = EnableBulletTimeBlock flag; +0x10 ShootDodgePenalty, +4 TurnOffPenalty and +0x18 MusicVolume are also used. Its toggle-off note (subtract 0x5e53d0 from the meter) is TurnOffPenalty (cfg+4, folded getter).

## Physics and collision

Sources: Ghidra corpus + capstone disasm + image constants + owner data. Hopper was NOT cross-checked (dump only covers the scope list).
C = X_Character, ST = X_CharacterProperties (C+0x22e). Rows: physics.tsv (75). Leftovers: physics.unknown.tsv.

### FACT: who moves the player
- The player moves by animation root motion plus a small free-fall body. There is no separate "run speed" integrator.
- One character substep is X_Character::updateLowerBody 0x68c960 (profile sections "Char.LowerB" /* 0x892c4c */ and "Char_Collisions").
- Order inside 0x68c960 (all dt = scaled dt, 0x5e53d0): (1) animation delta from the motion object C+0x3a0 (0x744e20) added to the animation velocity C+0x42a; (2) displacement = animDelta + dt * motionVec[+0x86] * [+0x9a] * scale; (3) plus last velocity C+0x41e * dt (rotated room <-> local by 0x642d70/0x642d80); (4) plus free fall displacement (0x72c540) unless the body is "free" flag cleared; (5) collision section; (6) velocity rebuilt as displacement / dt into C+0x41e and C+0x7f4.
- Step (2) and the motion vector at +0x86 are UNPROVEN in meaning.

### FACT: free fall body X_FreeFall (C+0x404; also projectiles)
- Layout (byte offsets, unaligned): +0 enabled, +1 acceleration vec3, +0xd velocity vec3, +0x19 free flag. Saved/loaded as a chunk (0x72c360 / 0x72c2c0).
- step(out, dt) 0x72c540: out += v*dt + 0.5*a*dt^2 /* 0.5 = 0x7cbdd4 */; then v += a*dt. Order: displacement first, velocity second.
- setFree(f) 0x72c5c0: stores +0x19. If not free and v . a > 0 then v = 0 (landing: the fall velocity dies on contact).
- Jump 0x699820 (C_Jump handler): v = v - speed * normalize(a), then C+0x228 = 0.5 s (0x695840). Data speeds: PLAYER_SHOOTDODGEJUMP 4.5 /* 179_max_payne.txt */, PLAYER_DODGE 3.5, PLAYER_JUMPSIDE 3.
- Gravity value: level table 36_levels.txt "Gravity = (0, -20, 0)" for every level, parsed in 0x525490/0x525860 into level entry +0x40 (negation at +0x4c). Link from that entry to C+0x404.acceleration is INFERENCE (spawn vector passed 0x452cd0 -> 0x6834c0 -> spawnInit 0x683f50 -> 0x72c290); projectiles scale it by GravityMultiplier (gameplay.md).

### FACT: collision section of 0x68c960
1. target = position (+0x54, 0x642d50) + displacement; sweepAndRefresh 0x647da0(target, contacts 0x8b4c10, 1).
2. Contact buffer entries are 0x28 bytes (normal at +4, object at +0x24). isBlockedAfterMove 0x6480a0 decides if the move is blocked; else a retry through 0x64af10, then testMove.
3. If not blocked, not airborne (ST test 0x6d6e20) and C+0x228 <= 0: snapToGround 0x647df0. Drop = min(0.05 /* 0x7d5924 */, |disp| * tan(MaximumDragDownAngle 60 deg)); the surface is accepted when cos(MaximumSteepnessAngle 46 deg) < dot(up normal, hit normal). Both angles are copied from CharacterConfig into statics 0x891b80 / 0x891b7c by the GM ctor 0x44f190.
4. C+0x228 counts down by dt (0x695840): after a jump, ground snapping is off for 0.5 s.
5. C+0x469 = result flag (1 = no contact, "free"); 0 = in contact. Feeds setFree(). Velocity is zeroed when not free.
6. Falling damage (end of 0x68c960): previous velocity C+0x436 vs C+0x444 threshold, then causeDamage(factor * C+0x448 * pow(...), 3). Exponent source UNPROVEN (pow args on FPU stack). CharacterConfig data: DamageFallingThreshold 15, RelativeFallingDamage 1%, DamageFallingSpeedDeduct 10, FallingDamageExponent 2.1.
7. At the end of 0x687f20 the room box test 0x63a330 catches a character that left the room.

### FACT: collider classes (vtable 32+ slots, 0x7d4e20 family)
- ObjectBase 0x7d4e20, PointObjectBase 0x7d4f88, SphereObjectBase 0x7d5008, CapsuleObjectBase 0x7d4d64, TestSphere 0x7d4edc, CharacterTestSphere 0x7d5d10, DodgeTestSphere 0x7cff0c, StartPointTestCapsule 0x7cd010.
- Slots 22/23/24 are the world queries (capsule 0x63e280/0x63e350/0x63e430; sphere 0x658bf0/0x658c30/0x658c80; point 0x657d80/0x657dc0/0x657e10). Slot 31 is an extra "run the test" virtual only the Test* classes define (0x501d50, 0x64d1d0, 0x452c50).
- Fields: +0x54 position, +0x60 room, +0x30 matrix, +0x64 margin, +0x88/+0xb0 dirty, +0x120 collision enabled, +0x12c radius, +0x130 capsule half axis (vec3), +0x184 passed to slot 24.
- moveTo 0x647660(newPos, result, bool): returns 1 when result+0x98 != FLT_MAX /* 0x7d4f6c */ (hit record, normal at +0x24, object at +0x34). testMove 0x64c910 = moveTo with +0x120 off.
- Users: DodgeTestSphere 0x501b30 (3 m side test then 2 m down, cached 0.35 s), CharacterTestSphere 0x68f600 (dead body), TestSphere 0x64d230 (explosion), StartPointTestCapsule 0x4524d0.

### FACT: BSP ray/sweep primitives
- 0x5c0ba0 (entry) -> 0x5c0c10 (recursive: segment p3->p4 with radius, plane distance 0x5ca490, leaf 0x5bdf50, returns 0 none / 1 hit / 2 stop). 0x5bd240 -> 0x5bd0a0 and 0x5bf8e0 / 0x5bf2f0 are the other entries.
- 0x5ba000 segmentSphereIntersect (returns 0, 1, 2 or 3; two output vec3 in ecx, edx); 20 callers incl. projectile sweep 0x70a0d0, use-trigger 0x6966a0, line of sight 0x696f60.

### Camera collision (config only)
- [camera] (parser 0x50a2b0): SphereRadius 0.1, CollisionPredictionSphereRadius 0.6, MaximumPredictionIterations 7, InitialBackwardsSpeed 5.0, BackwardsAcceleration 20.0, FrontPlaneDistance 0.05. Collision code not traced (0x672150, 0x672ce0, 0x506700).
- Character capsule (parser 0x50aec0): CapsuleTopLimit 2.0, BottomLimit 0, RadiusLimit 0.5, AICapsuleRadius 0.31; player 179_max_payne.txt CapsuleTop 1.95, Radius 0.48. Camera sphere must fit in the capsule else exception CameraSphereOutsideCharacterCapsule.

### Port note (INFERENCE)
- Keep dt scaled for free fall and collision (bullet time slows jumps), real dt only for meters. Fixed substep cap 0.11 s /* 0x7d5920 */.

### Open
- Bodies of 0x643eb0, 0x6590d0, 0x63fc40, 0x5bdf50, 0x5bf8e0 (BSP internals); contact record layout beyond +0x24/+0x34/+0x98.
- Meaning of motion vector C+0x3a0+0x86; the fall-damage pow arguments; camera collision.

## Animation

### 1. Classes (KF2, RTTI)
- FACT: KF_ObjectAnimation (primary vtable 0x7d0e94, 64 slots; P_UserData vtable 0x7d0e84 at this+16), KF_VertexAnimation 0x7d11b8,
  KF_ObjectAnimationLODs 0x7d1334, KF_VertexAnimationLODs 0x7d1074. Base KF_VirtualObject 0x7d0d90 (slots 2..22 = thunks into
  e2mfc P_BaseObject, slots 23..59 pure /* 0x76f964 */).
- FACT: slots 23..59 have the same meaning in all four classes. The three other classes are pure forwarders: slot i calls slot i of
  the inner animation (VertexAnimation: [[this+0x111]] via 0x57baa0; ObjectAnimationLODs: selected LOD via 0x57fd40/0x57fd70;
  VertexAnimationLODs: LOD 0 via 0x562440). Checked by script on all slots 23..59; slot 44 shares the body of slot 38 in these three.
- Slot table (KF_ObjectAnimation): 24 getObjectCount, 25 getObject(i), 28 hasAnimation(id), 29 getFrameCount(id), 30 getDurationSeconds(id),
  31 getFps, 32 setFps, 33 setTime(id,sec,flag), 34 setFrame(id,frame), 35 capturePose, 36 restorePose, 37 findBone(name),
  39 setPoseBlended(weight), 38,40..47 frame-set variants (unread), 48..57 forward to the named-value core [this+0x111],
  58 record field +8, 59 frame duration = 1/fps. Slots 9..11 bounding radius/box min/box max, 18 visibility update.
- FACT: animation record (found by 0x54feb0(id)): +0 fps (int), +4 frame count, +8 dword, +0x10/+0x14 per-child track vector,
  +0x1c track count, +0x24 per-track cursor vector. Animations live in a std::map at this+0x169 keyed by int id.
- FACT: children are P_BaseObjects (bones/nodes); local matrix = 12 floats at child+0x2c..0x58. CrossMode int at this+0x179 /* 0x550350 */: 0 linear, 1 normalized, 2 orthonormalized.

### 2. Posing one animation (per frame)
- setTime(id, sec) = setFrame(id, sec*fps) -> 0x54e470(id, frame, weight 1.0, addBase 0, flag): for each child, 0x55e300 evaluates the
  track (3x4 matrix, cursor kept in record+0x24), scales by weight if weight != 1.0, adds the child base matrix if addBase,
  writes the 12 floats, invalidateMatrices. Then 0x54ea80 applies CrossMode (re-normalizes the rotation rows when mode != 0).
- INFERENCE: interpolation between keys is inside 0x55e300 (not read). Blending is a linear weighted sum of 3x4 matrices, no slerp,
  then CrossMode cleans the rotation. Basis: 0x572530 additive branch multiplies by weight and adds component-wise.
- Pose snapshot: slot 35 copies all child matrices into a pose (stride 0x30, bit mask) /* 0x5723d0 */; 0x572530(pose, onlyAnimated, weight, additive) puts it back (copy at weight 1.0, additive = weight*pose added to current). INFERENCE: crossfade = snapshot old pose (0x74a180 calls slot 35), then blend the new animation by weight (slot 39).
### 3. Character side (no RTTI; names are descriptive)
- FACT: X_Character::update 0x687f20 -> C->[0x3a0] (animator) -> 0x744e60 update(lowerReq, upperReq, isAiming, isShootDodging);
  requests are X_CharacterInput+0xd (lower) and +0x32 (upper).
- FACT: 0x744e60 sets weapon mode (0x745400 -> mode index [this+0x31]; KF object = table[mode][channel] 0x7453e0), then 0x745130
  per channel ([this+0x25][0] lower, [1] upper).
- FACT request layout: +1 type 0..5, +5 float, +9 byte active, +0xa animation id, +0x16 float, +0x1a..+0x1c bytes, +0x1d dword, +0x21 float.
- FACT 0x745130 jump table /* 0x74528c */: type 0,1 play once (0x74a310, or 0x74a090 flagged); 2,3 crossfade 0x74a180; 4 loop
  0x74a100 (or 0x74a050 flagged); 5/default continue from elapsed time 0x74b180.
- FACT channel: +0x1e mode (0 loop, 1/2 once, 3/4 crossfade), +0x22 KF object, +0x2e timer, +0x40 second timer, +0x52 float 1.0,
  +0x5a current id, +0x5e previous id. Loop/once set timer step = KF slot 59, length = KF slot 30.
- FACT AnimationTimer (0x12 bytes): +1 time, +5 length, +9 step, +0xd loop offset, +0x11 loop byte. advance 0x77b1a0: time += dt;
  at length it wraps (fmod by length-step, adds to +0xd) when looping, else clamps.
- FACT: request builder animRequest_initFromState 0x7435b0 copies the character state id as the animation id, it does not map it; state table pointer /* 0x8b72cc */, entry stride 0x18
  (+9 type, +0xd duration, +0x11/+0x12 flags). INFERENCE: state ids, KF animation ids and SharedDB Index (CHARANIM_*) are one number space.
### 4. Game time and bullet time
- FACT: X_TimeUpdate (vtable 0x7d142c): +4 dt (already x speed), +8 total, +0xc speed (default 1.0). Names by bullettime agent.
- FACT: AnimationTimer::advance reads dt with 0x5e53d0 = X_TimeUpdate+4, so a scaled TimeUpdate slows animation. NOT VERIFIED: which TimeUpdate reaches the channel timers (bullettime owns the scale).
- FACT: weapon tick (X_WeaponSet::tickAnimation 0x757e00): anim time at this+0x1c7 += dt, clamped to KF slot 30, then KF slot 33
  setTime(id, time, 0) poses the weapon, then 0x753490. Anim id at +0x1c3 (0x7fffffff = none).

### 5. Animation messages and the lag
- FACT: script text [Properties]{[Message] Frame=N; String="this->Cmd(...)";} -> 0x53efc0 (16-byte records) -> X_AnimationMessages
  loader 0x753860 (method of the per-animation element, 0x22 bytes, built by 0x753770): pairs {float frame, X_LaggedMessage*}, X_Message::parse, RTDynamicCast to X_LaggedMessage else
  X_AnimationMessagesException_NotLaggedMessageBased, qsort by frame. Per-id vectors at +4, current id +8, cursor +0x1c, window end +0x20.
- FACT: X_AnimationPlayer: +1 sink, +5 timer, +0x17 fps, +0x1b frame count, +0x1f id, +0x23/+0x2b receiver, +0x2f KF object.
  start 0x7530d0 reads fps/count from the KF object; 0x7532d0 takes them as arguments; throws
  ..._AnimationFrameOutOfLimits (vtable 0x7d9950) if last message frame >= frame count.
- FACT lag consumer = X_AnimationPlayer::update 0x753490(X_TimeUpdate t, float dtArg): window [fps*time, (time+t.dt)*fps);
  per message in window (0x754360 first, 0x754480 next): frac = (msgFrame-start)/(end-start); temp X_TimeUpdate(avg = dtArg - t.dt*frac,
  total = t.total + t.dt*frac, scale 1.0 /* 0x7cbdd0 */) copied into the message at +0x11 (0x5848c0); X_MessageQueue::send to the
  receiver; then timer advance. With dtArg = dt (tickAnimation), lag = dt*(1-frac): time left in the frame after the event.
- FACT: handlers get the lag by message+0x11 (0x753ca0): P_CreateProjectile (createProjectile 0x65ae10) and WS_Animate 0x758360
  (stores it with 0x582ed0 assign). INFERENCE: spawned projectiles are advanced by the lag. NOT VERIFIED (0x706ff0/0x707070 unread).

### 6. Data on disk (OWNER copy, read with tools/ras.py)
- FACT x_data.ras: 608 .kf2 (anim\*.kf2, death_*, enemy_shootdodge*, camera_shake_*), 169 .kfs, 25 .SKD, scripts .txt, headers
  characteranimid.h (CHARANIM_*), weaponanimid.h (WEAPONANIM_*). Other archives: x_level1..3.ras, x_english.ras, x_music.ras.
- FACT character script (161_default_skeleton.txt): [Skeleton]{[Geometry] ExportData=...kfs; CrossMode=Orthonormalized;}
  [Animation] Index=CHARANIM_X; Filename=anim\x.kf2; [Properties]{[Movement] EndPosition=(x,y,z); Rotation=...; [Message] Frame=N;
  String="..."} ; [Revert] From=CHARANIM_W_X; To=CHARANIM_X (wounded falls back; "Infinite Loop in revert" check).
- FACT weapon script (310_beretta.txt): [LOD]{[Geometry] ExportData=...kf2; Distance}, [Animationset]{[Animation] Index=WEAPONANIM_*;
  Filename=beretta_Default.kf2; [Properties]{[Message] Frame=0; String="this->A_Play3DSound/P_CreateProjectile/PS_StartEffect/WS_Hide"}},
  [Attributes] TwoHandedBones, ShootDodgeAllowed, ProjectileOffset, SlotIndex...
- Parsers: 0x53dba0 animation set (block table 0x883530), 0x53e980 movement (record 0x32), 0x53efc0 message, 0x537680 skeleton, 0x550350 geometry. KF2 chunks: files.md.

### 7. Level dynamic objects
- FACT: X_LevelRuntimeDynamicObjectAnimation (vtable 0x7d9800): slot 0 0x751a80, slot 1 0x751d10 (adds three entries from this+5 to a list). Messages DO_Animate, DO_Pause/Invert/Stop/ResumeAnimation; error strings 'does not have animation', 'is not continuous path!' (0x6f3d20, body unread).

### 8. Open / contradicts rea/map
- messages.md "lag consumer UNPROVEN": answered in 5. character.md "animation id table not located": see 3.
- Folded getters: 0x5e53d0/0x5830d0/0x5830e0/0x5830f0 are named SlowMotionConfig::* (bullettime) and also serve X_TimeUpdate; 0x742860 serves many classes.
- Open: channel +0x22 vs +0x26 KF pointer; who calls channel timer advance each frame; track interpolation 0x55e300; slots 38..47,
  49..57, 60..63; root motion (movement records at channel +0x62/+0x175/+0x288/+0x3a7); skinning (render slice).

## Addresses another area named first
Their row stays in the table. The right column is what the gameplay read called it; a folded function can carry both.

| addr | table (area) | gameplay read (area) |
|---|---|---|
| 0054bb80 | KF2::KF_VirtualObject::getField_e8 (render) | KF_VirtualObject::getScene (animation) |
| 0054da60 | KF2::KF_ObjectAnimation::fwdToSubObject_slot48 (render) | KF_ObjectAnimation::hasNamedValue (animation) |
| 0054dbe0 | KF_ObjectAnimation::getClassName (render) | KF_ObjectAnimation::getObjectType (animation) |
| 0054dc00 | KF2::KF_ObjectAnimation::getChildCount (render) | KF_ObjectAnimation::getObjectCount (animation) |
| 0054dc20 | KF2::KF_ObjectAnimation::getChild (render) | KF_ObjectAnimation::getObject (animation) |
| 0054e210 | KF2::KF_ObjectAnimation::getTrackCount (render) | KF_ObjectAnimation::getEntryCount52 (animation) |
| 0054e240 | KF2::KF_ObjectAnimation::sampleAccessor_slot29 (render) | KF_ObjectAnimation::getFrameCount (animation) |
| 0054e270 | KF2::KF_ObjectAnimation::sampleAccessor_slot31 (render) | KF_ObjectAnimation::getFps (animation) |
| 0054e2a0 | KF2::KF_ObjectAnimation::sampleAccessor_slot30 (render) | KF_ObjectAnimation::getDurationSeconds (animation) |
| 0054e300 | KF2::KF_ObjectAnimation::sampleAccessor_slot32 (render) | KF_ObjectAnimation::setFps (animation) |
| 0054e420 | KF2::KF_ObjectAnimation::sampleAccessor_slot33 (render) | KF_ObjectAnimation::setTime (animation) |
| 0054e450 | KF2::KF_ObjectAnimation::sampleAccessor_slot34 (render) | KF_ObjectAnimation::setFrame (animation) |
| 0054e790 | KF2::KF_ObjectAnimation::allocMemoryContainer_slot35 (render) | KF_ObjectAnimation::capturePose (animation) |
| 0054e800 | KF2::KF_ObjectAnimation::allocMemoryContainer_slot36 (render) | KF_ObjectAnimation::restorePose (animation) |
| 0054e870 | KF_ObjectAnimation::findBoneOrThrow (render) | KF_ObjectAnimation::findBone (animation) |
| 0054fdf0 | KF2::KF_ObjectAnimation::findTrackIndex (render) | KF_ObjectAnimation::hasAnimation (animation) |
| 0054fe40 | KF2::KF_ObjectAnimation::getSampleField8_slot58 (render) | KF_ObjectAnimation::getRecordField8 (animation) |
| 0054fe50 | KF2::KF_ObjectAnimation::getSample_slot59 (render) | KF_ObjectAnimation::getFrameDuration (animation) |
| 00551020 | KF2::KF_ObjectAnimation::updateVisibilityFlag_slot18 (render) | KF_ObjectAnimation::executeAlways (animation) |
| 00561d80 | KF_VertexAnimationLODs::getClassName (render) | KF_VertexAnimationLODs::getObjectType (animation) |
| 00561dc0 | KF2::KF_VertexAnimationLODs::fwdToCurrentLOD_slot24 (render) | KF_VertexAnimationLODs::getObjectCount (animation) |
| 00561dd0 | KF_VertexAnimationLODs::forward_slot25 (render) | KF_VertexAnimationLODs::getObject (animation) |
| 00561e10 | KF_VertexAnimationLODs::forward_slot29 (render) | KF_VertexAnimationLODs::getFrameCount (animation) |
| 00561e30 | KF_VertexAnimationLODs::forward_slot30 (render) | KF_VertexAnimationLODs::getDurationSeconds (animation) |
| 00561e50 | KF_VertexAnimationLODs::forward_slot31 (render) | KF_VertexAnimationLODs::getFps (animation) |
| 00561e70 | KF_VertexAnimationLODs::forward_slot32 (render) | KF_VertexAnimationLODs::setFps (animation) |
| 00561e90 | KF_VertexAnimationLODs::forward_slot33 (render) | KF_VertexAnimationLODs::setTime (animation) |
| 00561ec0 | KF_VertexAnimationLODs::forward_slot34 (render) | KF_VertexAnimationLODs::setFrame (animation) |
| 00561ef0 | KF_VertexAnimationLODs::forward_slot35 (render) | KF_VertexAnimationLODs::capturePose (animation) |
| 00561f10 | KF_VertexAnimationLODs::forward_slot36 (render) | KF_VertexAnimationLODs::restorePose (animation) |
| 00561f30 | KF_VertexAnimationLODs::forward_slot37 (render) | KF_VertexAnimationLODs::findBone (animation) |
| 005624d0 | KF_VertexAnimationLODs::forward_slot58 (render) | KF_VertexAnimationLODs::getRecordField8 (animation) |
| 005624f0 | KF_VertexAnimationLODs::forward_slot59 (render) | KF_VertexAnimationLODs::getFrameDuration (animation) |
| 00562510 | KF_VertexAnimationLODs::forward_slot28 (render) | KF_VertexAnimationLODs::hasAnimation (animation) |
| 00573f40 | KF_ObjectAnimationMeshCallback::createTriangleMesh (render) | KF_ObjectAnimationMeshCallback::slot1 (animation) |
| 00573fc0 | KF_ObjectAnimationKeyframeCallback::onTargetNotFound (render) | KF_ObjectAnimationKeyframeCallback::onKeyframes (animation) |
| 0057ba40 | KF2::KF_VertexAnimation::fwdToSkin_slot24 (render) | KF_VertexAnimation::getObjectCount (animation) |
| 0057ba50 | KF_VertexAnimation::forward_slot25 (render) | KF_VertexAnimation::getObject (animation) |
| 0057ba70 | KF_VertexAnimation::getClassName (render) | KF_VertexAnimation::getObjectType (animation) |
| 0057bad0 | KF_VertexAnimation::forward_slot29 (render) | KF_VertexAnimation::getFrameCount (animation) |
| 0057baf0 | KF_VertexAnimation::forward_slot30 (render) | KF_VertexAnimation::getDurationSeconds (animation) |
| 0057bb10 | KF_VertexAnimation::forward_slot31 (render) | KF_VertexAnimation::getFps (animation) |
| 0057bb30 | KF_VertexAnimation::forward_slot32 (render) | KF_VertexAnimation::setFps (animation) |
| 0057bb50 | KF_VertexAnimation::forward_slot33 (render) | KF_VertexAnimation::setTime (animation) |
| 0057bb80 | KF_VertexAnimation::forward_slot34 (render) | KF_VertexAnimation::setFrame (animation) |
| 0057bbb0 | KF_VertexAnimation::forward_slot35 (render) | KF_VertexAnimation::capturePose (animation) |
| 0057bbd0 | KF_VertexAnimation::forward_slot36 (render) | KF_VertexAnimation::restorePose (animation) |
| 0057bbf0 | KF_VertexAnimation::forward_slot37 (render) | KF_VertexAnimation::findBone (animation) |
| 0057c010 | KF_SkinMeshCallback::createSkinMesh (render) | KF_SkinMeshCallback::onSkinMesh (animation) |
| 0057d490 | KF_VertexAnimation::forward_slot58 (render) | KF_VertexAnimation::getRecordField8 (animation) |
| 0057d4b0 | KF_VertexAnimation::forward_slot59 (render) | KF_VertexAnimation::getFrameDuration (animation) |
| 0057d4d0 | KF_VertexAnimation::forward_slot28 (render) | KF_VertexAnimation::hasAnimation (animation) |
| 0057f300 | KF_ObjectAnimationLODs::getClassName (render) | KF_ObjectAnimationLODs::getObjectType (animation) |
| 0057f350 | KF2::KF_ObjectAnimationLODs::fwdToCurrentLOD_slot24 (render) | KF_ObjectAnimationLODs::getObjectCount (animation) |
| 0057f380 | KF2::KF_ObjectAnimationLODs::fwdToCurrentLOD_slot25 (render) | KF_ObjectAnimationLODs::getObject (animation) |
| 0057f420 | KF2::KF_ObjectAnimationLODs::fwdToCurrentLOD_slot29 (render) | KF_ObjectAnimationLODs::getFrameCount (animation) |
| 0057f460 | KF2::KF_ObjectAnimationLODs::fwdToCurrentLOD_slot30 (render) | KF_ObjectAnimationLODs::getDurationSeconds (animation) |
| 0057f4a0 | KF2::KF_ObjectAnimationLODs::fwdToCurrentLOD_slot31 (render) | KF_ObjectAnimationLODs::getFps (animation) |
| 0057f4e0 | KF_ObjectAnimationLODs::forward_slot32 (render) | KF_ObjectAnimationLODs::setFps (animation) |
| 0057f520 | KF_ObjectAnimationLODs::forward_slot33 (render) | KF_ObjectAnimationLODs::setTime (animation) |
| 0057f580 | KF_ObjectAnimationLODs::forward_slot34 (render) | KF_ObjectAnimationLODs::setFrame (animation) |
| 0057f5e0 | KF_ObjectAnimationLODs::forward_slot35 (render) | KF_ObjectAnimationLODs::capturePose (animation) |
| 0057f610 | KF_ObjectAnimationLODs::forward_slot36 (render) | KF_ObjectAnimationLODs::restorePose (animation) |
| 0057f640 | KF_ObjectAnimationLODs::forward_slot37 (render) | KF_ObjectAnimationLODs::findBone (animation) |
| 0057fda0 | KF_ObjectAnimationLODs::forward_slot58 (render) | KF_ObjectAnimationLODs::getRecordField8 (animation) |
| 0057fdc0 | KF_ObjectAnimationLODs::forward_slot59 (render) | KF_ObjectAnimationLODs::getFrameDuration (animation) |
| 0057fde0 | KF_ObjectAnimationLODs::forward_slot28 (render) | KF_ObjectAnimationLODs::hasAnimation (animation) |
| 00642d40 | X_LevelRuntimeObjectBase::getId (render) | X_LevelRuntimeObjectBase::getRoom (physics) |
| 00648a90 | X_LevelRuntimeObjectBase::getCollisionGroupRef (render) | X_LevelRuntimeObjectBase::getMoveMargin (physics) |
| 0064c2b0 | X_LevelRuntimeObjectBase::getFlag120 (render) | X_LevelRuntimeObjectBase::getCollisionEnabled (physics) |
| 00657d80 | X_LevelRuntimePointObjectBase::debugDrawPoint_slot22 (render) | X_LevelRuntimePointObjectBase::slot22_bspSweep (physics) |
| 00657dc0 | X_LevelRuntimePointObjectBase::debugDraw_slot23 (render) | X_LevelRuntimePointObjectBase::slot23_bspSweepCached (physics) |
| 00657e10 | X_LevelRuntimePointObjectBase::debugDraw_slot24 (render) | X_LevelRuntimePointObjectBase::slot24_bspQuery (physics) |
| 00658bf0 | X_LevelRuntimeSphereObjectBase::debugDrawSphere_slot22 (render) | X_LevelRuntimeSphereObjectBase::slot22_bspSweep (physics) |
| 00658c30 | X_LevelRuntimeSphereObjectBase::debugDraw_slot23 (render) | X_LevelRuntimeSphereObjectBase::slot23_bspSweepCached (physics) |
| 00658c80 | X_LevelRuntimeSphereObjectBase::debugDraw_slot24 (render) | X_LevelRuntimeSphereObjectBase::slot24_bspQuery (physics) |
| 00658dd0 | X_LevelRuntimeSphereObjectBase::setRadius_slot20 (render) | X_LevelRuntimeSphereObjectBase::slot20_broadphaseBounds (physics) |

## FACT: cinematic cameras are data + script (2026-11-02, x_data.ras / x_level*.ras, tools/ras.py)
- Paths live in database camerapaths.txt (x_data.ras d4): [Death_01] (3 s circle), [Death_02], [Death_03], [bullet_behind]
  (+2x, 4x), [special_killed], [player_death_01], [circle], [half_circle], [jump], [pause], shakes. Each: a cinematic\*.kf2 camera
  animation + frame-keyed messages (GM_ChangeGlobalGameSpeed(speed, secs), GM_ChangeFOV(mul, secs), gm_enablewidescreen(on, secs),
  mphm_changehudvisibility, gm_setplayercontrols, A_PlaySound(camera, shot_01)) and an [exit] block that restores them.
- Kill cam (last enemy): level scripts, not exe code. Each fight has a ::room::death_counter FSM; the state for the last kill sends
  "::room::eN->CAM_AnimateParented(Death_0x);" (Part1_Level1 10 sites, 1b 16, 2 6, 2b 6, 4 3, 6 1, Part0 3).
- Sniper bullet cam: database bullet_sniper*.txt [Sniper] strings "GM_ChangeGlobalGameSpeed(0.01, 0.0)" and
  "this->cam_animateparented(bullet_behind)" on the projectile (X_Projectile receives CAM_AnimateParented / AnimateInPlace).
- Exe anchors: "CAM_AnimateParented" 0x8a2568 (ref 0x73db01), "GM_ChangeGlobalGameSpeed" 0x86aff0 (ref 0x4314a1); sounds use
  FrequencyScaling = TRUE, so slow motion pitches audio down through IDirectSoundBuffer::SetFrequency (host/dsound.c 560).
- Chain (static, Ghidra + capstone, 2026-11-02; detail in the session's cams_re.md, not kept): GM_ChangeGlobalGameSpeed handler
  0x456150 -> GM+0x1168; the tick 0x44e845..0x44ea00 blends it into timer+0xc (cap 1.0; only the real dt comes from the host).
  0x44ea00 sets 2D/3D/music frequency = min + (1-min)*speed (data: 2D 1.0, 3D 0.15, music 1.0: only 3D sounds pitch down)
  -> S_Sound::applyVoiceParameters 0x1030ae20 -> SetFrequency (clamped 6000..100000; host/dsound.c mixes at that rate).
  GM_ChangeFOV -> [camera+0x5d0] -> P_Camera::setFOV -> projection. Widescreen bars = two P_Driver::clearScreen(rect) from
  HUDMode::render 0x4af900 (SetViewport + Clear with a rect; host/d3d8.c Clear, web/gpu.js). CAM_AnimateParented handlers
  0x69ca90 (X_Character), 0x7082c0 (X_Projectile) -> X_CameraImplementation slot 24 0x672840; PlayerCamera::update 0x504130
  writes the path pose each frame (GM+0x1074 stays the player camera). Unproven: the .kf2 open site, kf2 FOV track.
- Host risk: SetViewport (host/d3d8.c ~658) rejects a rect outside the target; at widescreen, a driver size != back buffer
  would make the bars throw.
