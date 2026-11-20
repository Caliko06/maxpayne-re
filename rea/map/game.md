# MaxPayne.exe by subsystem

Sources: Ghidra corpus rea/c/MaxPayne.exe, MSVC RTTI (rea/map/MaxPayne.exe.vtables.tsv: every vtable and slot with its
class, made by tools/recomp/rtti.py), rea/map/subsystems.tsv (one label per function). `#n` = vtable slot.
Function counts by label: strings_containers 3496, game 3061, unknown 2779, scene 2159, render 918, level 875, crt 782,
ui 531, sound 379, files 335, input 190, physics 60, save 49, timing 42, graphic_novel 23, video 19.

## Boot
- Entry 0x76fb04 -> AfxWinMain thunk 0x7838b8 -> CMaxPayneApp (vtable 0x7cb77c).
- #22 InitInstance 0x40b7a0: S_Listener setup, FUN_0040b500 startup init -> 0x40a4c0 (setAutomaticPreload unless
  -disable3dpreloads), loop FUN_0040b5e0 startup dialog + FUN_0040b670 -> 0x40b1b0.
- #23 Run 0x40bcd0: loops FUN_0040c020 until false. #29 ProcessWndProcException 0x40c590.
- Command line 0x40cb90: window, nodialog, screenshot, profile, developer, enableloadingstats, debugmem,
  disable3dpreloads, skipstartup. The host passes " -skipstartup -nodialog".
- CMaxPayneGameWindow vtable 0x7cba94, WM_ACTIVATEAPP 0x40d270.

## Main loop and frame timing
- FUN_0040c020, one frame: checks FPU 53-bit/round-near (M_FPUControl) else throws; R_Timer::sample via 0x40bbd0, clamp 0.1 s
  (0x7cb8b4), average of the last 4 frames (+0xf1 = 4 set in 0x40a1d0, ring +0xe5/+0xe9/+0xed), accumulator +0xf5;
  FUN_00582df0(avg, total, 1.0) -> 0x582e60 game timer scale; input FUN_00417c70; FUN_00601250 modes update
  ("Modes", "Overlaid modes", X_ModeSwitch); FUN_006014d0 render ("Modeswitch render", "Rendering"); FUN_00582e20.
- Windows messages: FUN_0040c440 (PeekMessage ranges 0x100-0x108, 0x20, 0x1c). Timer reset after a load: FUN_0040bbf0.

## Modes (the state machine above everything)
- MaxPayne_ModeSwitch 0x7cbf0c, ctor FUN_00415dc0 (builds all modes and the loading screen data 0x50ec10); X_ModeSwitch
  0x7d3418, X_ModeBase 0x7d3934, X_GameModeBase 0x7d3a2c. Labels MP_MS_* 0x868a3c..0x868bf8, factories MP_GMB_* 0x868b30.
- MaxPayne_GameMode 0x7ccec4: #14 tick 0x44c1b0, #15 render 0x44ec60, #21 cleanup 0x44fa10, #22 level deinit 0x44fba0,
  #25 load game 0x454230, #29 save game 0x454fa0. Ctor 0x44f190 via 0x44ba20.
- MaxPayne_MenuMode 0x7cc500, X_MenuModeBase 0x7d4670 (#14 0x6313e0 update, cursor).
- MaxPayne_GraphicNovelMode 0x7ce920 (#14 0x49b6d0, #21 0x49b480), MaxPayne_GraphicNovelPage 0x7ced14.
- MaxPayne_HUDMode 0x7cefc8, MaxPayne_ConsoleMode 0x7cc3e8 (X_ConsoleMode 0x7d3c7c), MaxPayne_StatisticsMode.

## Menus / front end
Script-driven: x_data.ras menu.txt. See rea/map/flow.md. Message classes: MaxPayne_GameModeMessage_Init 0x7ccbdc,
SetGameLevel 0x7ccd10, EnableGameLevel 0x7ccb8c. Input: Keyboard ctor 0x517e70, Mouse ctor 0x518c40, Mouse_Update 0x519070
(rea/map/input.md). Menu cursor: 640x480 virtual, +1.25 units per DirectInput count.

## Level load
Full path in rea/map/flow.md. Level init 0x44fda0 ("Level init"), MaxPayne_Level ctor 0x493f80 ("LevelDB", "Room creation",
"LDB: lights/Triggers/Dynamic objects/FSMs/Level items"), player 0x451e70. LDB: X_LevelDBLevel 0x7d2b7c (#33 0x5ebd50, #2
0x5ec550 version), X_LevelDBImportLevel 0x7d1ff8, materials 0x7d2738/0x7d21b4, importer FUN_005a8b90, X_LEVELDB_*_LOAD labels
0x86ce24... Loading screen data: parser 0x50ee70 (loadingscreen, MenuLoadingScreen, AINetworkCalculationScreen,
GameExitScreen, ProgressBar*).

## Game objects, AI, weapons, bullet time
- Characters: X_Character 0x7d5930 (#1 0x691d00, #29 0x6976f0), X_SkinItem 0x7da190, KF_ObjectAnimation 0x7d0e84 (#39 0x54f370).
- Player/AI input: MaxPayne_PlayerInput 0x7cf974 (#32 0x4cecd0), MaxPayne_ArtificialPlayerInput 0x7cfcd4 (#32 0x5001f0),
  MaxPayne_AIAction 0x7cfb14, MaxPayne_AIStateMachine 0x7cfcac, X_GlobalAIObject.
- Weapons/projectiles: X_WeaponSet 0x7d9ac0, X_Projectile 0x7d80fc (#19 0x70a0d0), X_ProjectileImplementation 0x7d5088.
- Level runtime: X_LevelRuntimeTrigger 0x7d880c, X_LevelRuntimeFSM 0x7d75c0, X_LevelRuntimeDynamicObject 0x7d7ac0
  (#6 0x6e1a00), X_LevelItem 0x7d8690.
- Bullet time: shoot-dodge counters reset by the message gm_ResetShootDodgeBulletTimeCounters; game timer scale 0x582e60.
- Effects: X_ParticleSystem 0x7d8de4, PS_ParticleSystem 0x7da71c, X_DecalImplementation 0x7d5164 (#13 0x65c910).
- Camera: MaxPayne_PlayerCamera 0x7d00dc (#34 0x504130), MaxPayne_FlyCamera 0x7d0194, X_CameraImplementation 0x7d56cc, X_Crosshair 0x7d7404.

## Renderer side of the exe
X_RendererImplementation: #1 0x40df40 begin, #3 0x40e0e0 present, #14 0x40e810 screenshot. MapScalingCallback 0x40ef70: halves
textures by detail category (registry Level/Other Texture Detail, Character Detail; the host seeds 2). Driver and e2mfc: rea/map/ of render.

## Sound and video
X_AudioImplementation 0x7d1638 (#20 0x587980), X_Sound 0x7d1d44, X_SoundOmni 0x7d1c40, MaxPayne_MediaPlayer 0x7cb6e0,
X_VideoInterface 0x7cbc70. Host side: platform slice.

## Graphic novel
MaxPayne_GraphicNovelMode tick 0x49b6d0, pages + callbacks 0x7ced14/0x7ced1c/0x7ced2c/0x7ced34. See rea/map/graphicnovel.md.

## Save / load
MaxPayne_SaveGameInterface 0x7cbe78 (labels MP_SGI_LOAD1-2, MP_SGI_SAVE1-3 at 0x8681f8..), X_MenuSave 0x7d4c8c. Game mode #25 load
0x454230 (MP_GM_LOAD1-3, calls level init 0x44fda0), #29 save 0x454fa0 (MP_GM_SAVE1-4).

## Memory
R_MemoryScope in rlmfc ("E2 Library" scope), R_Memory, -debugmem flag, sm_iNewCount/sm_iDeleteCount counters. Guest heap: host/win32.c.
Level textures: P_Map holds a source G_Image until upload (FUN_10120370). rn_ldb_import uploads each LDB material right after decoding,
so the level peak is ~90 MB sampled on native (was ~1.3 GB, level 1).

## C++ exceptions (SEH + MSVC EH)
Decision: the port needs real unwinding. rlmfc catches R_MemoryFileException_DataError as normal flow (funclets 0x1001894f, 0x10018b3e, 0x10018d5e: old-format string read in operator>> 0x100189b0, 0x10018bd0), and the exe has catches that resume.
Census (disassembly of the FuncInfo tables, magic 0x19930520): exe 657 catch funclets (35 resume at a continuation, the others rethrow), 20459 unwind funclets. DLL catch funclets: mfc42 262, msvcp60 94, e2mfc 51, rlmfc 30, driver 18, sndmfc 11, grphmfc 7.
All catch funclets, unwind funclets, continuations and SEH filters/handlers are already lifted: frontend.py seeds them from data and from `mov eax, imm`. No new seeds.
Load path: 0x42a240 and 0x44c1b0 have only catch(...) that rethrow. The first catch that resumes is the top level 0x40bcd0 (P_DriverException_Failed cont 0x40be43, R_Exception 0x40bf23, std exception 0x40c008). So a bad save ends the game with its error path; the game has no recovery there.
msvcrt: __CxxFrameHandler 0x1080ace0, _CxxThrowException 0x1080ac15, _except_handler3 0x1080ef06, _global_unwind2 0x1080ee18. _UnwindNestedFrames 0x1080ac90 keeps fs:[0] on entry, calls RtlUnwind with TargetIp = its return 0x1080acb9, then re-links that kept frame above the target. _JumpToContinuation 0x1080ac4f pops that frame, sets esp = [pRN-4], ebp = pRN+0xc, jmp eax. So ntdll's own frame must be on the chain: host SEH_RAISE / SEH_UNWIND, pushed around each handler call as Wine does (signal_i386.c raise_handler, unwind_handler). Nested: walks on, sets EH_NESTED (0x10) until the frame is passed; msvcrt _except_handler3 tests only flags & 6 at 0x1080ef17. Collided: goes on from the frame the outer unwind was calling; msvcrt _local_unwind2's handler 0x1080ee38 also answers 3 with its frame in *disp.
Host: win32.c RaiseException_ and RtlUnwind_ walk fs:[0]; handlers are called cdecl through call_guest_cdecl.
Non-local return: the continuation runs on top of the host stack. Its function's `ret` sets c->ret; lifted call sites with another address return (once c->uw is set), the call site that matches resumes. C locals hold no state across a call, so a match in a deeper recursive frame is still correct. A host frame the return passes sets c->skipped and leaves registers alone; a frame whose own call returned clears it (a rethrow returns normally to the first handler after skipping deeper frames). A host loop that goes on after an abandoned callback gets 0 from any further call_guest. c->uw is set at the first RtlUnwind of the thread and stays: a matched deeper recursive frame leaves stale host frames below it that must still pass foreign returns, so a bad return after a throw stops at the thread or program base with the address, not at its call site. The host stack comes back when the catching function returns.
Debug: RN_EH=1 logs each raise with the C++ type.
longjmp 0x1081e40e (driver only): rn_longjmp sets c->uw and patches.py LIFT_SEEDS lifts the setjmp returns 0x1043c6d2, 0x1043d0a3, 0x10444939. loadJPG's setjmp is never jumped to: grphmfc's error exit 0x102044f0 throws.
