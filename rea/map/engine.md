# e2mfc.dll: the engine core (RE coverage)
e2mfc.dll is mapped at 0x10100000 in the game (file base 0x10000000: Hopper addresses = game - 0x100000). Names are in
rea/map/names/e2mfc.dll.tsv (columns in rea/map/names/README.md). census/hopper_names_e2mfc.json applies them to a Hopper
session in one batch (`node tools/rea/q.mjs census/hopper_names_e2mfc.json`; 1666 applied, 0 failed, 2026-11-02).

## Coverage (2026-11-02, no game runs)
| Range | Functions | named strong | understood strong | understood weak | Unknown |
|---|---|---|---|---|---|
| 0x10100000-0x1011ffff | 859 (731 Ghidra + 128 Ghidra missed) | 583 | 61 | 215 | 0 |
| 0x10120000-0x10148000 | 807 (770 Ghidra + 37 Ghidra missed) | 448 | 26 | 333 | 0 |
| whole image | 1666 | 1031 | 87 | 548 | 0 |
- Excluded: 1075 EH funclets (761 Unwind@, 46 Catch@, 268 `mov eax; jmp __CxxFrameHandler` stubs).
- The levels mean: named strong = the name comes from an export, RTTI or a thunk target; understood strong = identity
  from code plus a second source; understood weak = a descriptive name from the code's shape (STL roles, the ref T&L stages,
  vector element types named by size).
- Sources: Ghidra corpus rea/c/e2mfc.dll, a capstone sweep of the original DLL (call targets and vtable slots Ghidra missed:
  atexit stubs, `mov ecx; jmp dtor` stubs, 30 vtable slots, the initterm initializers), Hopper callers and pseudo code as the
  second key (191 callers checked, 166 agree; the rest are destructors Hopper sees one caller for).

## Classes and vtables (RTTI; game-mapped address, slots)
- Scene: P_BaseObject 0x10149ec4 (23), P_ObjectContainer 0x10149f28 (2), P_UserData 0x10149f34 (3), P_Scene 0x1014952c (4),
  P_LODObject 0x10149e50 (24), P_VirtualObject 0x10149ae0 (25), P_Text 0x10149560 (26), P_Bitmap 0x10149790 (26),
  P_Sprite 0x1014981c (26), P_2DLineObject 0x101495ec (25), P_3DLineObject 0x10149664 (25).
- Camera: P_Camera 0x10149d78 (24).
- Lights: P_DirectionalLight 0x10149b58 (27), P_PointLight 0x10149bd8 (27), P_SpotLight 0x10149c5c (28), P_VirtualLight 0x10149cf0 (26).
- Particles: P_ParticleSystemBase 0x1014997c (26), P_ParticleSystemTemplate<P_TestParticle> 0x101498fc (27),
  P_TestParticleSystem 0x101499f8 (27), P_ParticleBuffer 0x10149888, P_ParticlePrimitive 0x101498b8 (12).
- Textures: P_DriverMap 0x10149f44 (1), P_TextureFormat, P_BitmapInterface 0x10149a88 (3); P_Map ctor 0x1011f100,
  construct 0x1011f650, teardown 0x101209a0 (rea/map/flow.md, render.md).
- Material: P_Material 0x10149f74 (2), P_MaterialDefinitions 0x10149f80 (1), P_MaterialInfo 0x10149f88 (1),
  P_DeviceDependentVertexShader / PixelShader 0x10149f64 / 0x10149f6c (1).
- Geometry: P_Primitive 0x10149248 (11), P_Buffer 0x10149278 (10), P_VertexBuffer 0x101492a4 (46), P_DDVertexBuffer 0x10149360 (56),
  P_TriangleBuffer 0x10149444 (13), P_DDIndexBuffer 0x1014947c (13), P_TriangleMeshBase 0x101494b4 (12), P_TriangleMesh 0x101494e8 (12),
  P_SkinMesh 0x10149a98 (13), P_LineBuffer 0x101496cc (26), P_LinePrimitive 0x10149738 (12), P_OutputPrimitive 0x1014a388 (23),
  P_PrimitiveInstance 0x1014a3e8 (2).
- Driver: P_Driver 0x1014a040 (43; the d3d8 driver's P_D3D overrides it), P_CPUDriver 0x10149f90 (15), P_DriverCaps,
  P_DisplayMode (ctor 0x1012f100), P_ZBufferFormat, R_DLList x6 (3 slots each).
- Reference T&L (software path): P_RefVertexBuffer 0x1014a154 (46), P_RefTriangleBuffer 0x1014a218 (13), P_RefTriangleMesh
  0x1014a258 (12), P_RefSkinMesh 0x1014a28c (13), P_RefCPUDriver 0x1014a2c4 (15), P_RefVirtualObject 0x1014a318 (7).
- Math: M_Plane3Template<float> 0x10149e28 (4: normalized setNormal, setNormal, transformNormal, ray vs plane; read, not run).
- Exceptions (8 slots): R_MemoryException 0x10149224, R_MemoryException_Allocation 0x10149200,
  P_DriverException_TooManyBonesPerVertex 0x101491c4, P_DriverException_Failed 0x1014a0f0, and 0x1014a11c, 0x1014a364.
- 94 vtables in all. R_ streams and containers are imported from rlmfc.dll; e2mfc holds their template instances.

## Uncertain
- STL roles (set/map/vector) come from template shape; only set<uint> and map<rs, ptr> were read in detail.
- The ref T&L stages (transform, lighting, texcoord, projection, clipping) are named from callers and callees, not read through.
- P_Buffer slots 2-5 (allocate, reallocate, deallocate, copyContents) come from the vcalls of P_Buffer::construct/reallocate/destruct.
- 0x10118460 rayCrossesPlane is inferred from the x87 math. 0x10102810 is P_Font::validate's qsort comparator over 5-byte records
  whose content is unknown.
- Ghidra aliases several exports at one address (identical bodies folded): those rows keep one name and list the others.
- Two catch continuations the sweep placed past .text (0x10148b54, 0x10148b8c) were dropped.

# rlmfc.dll: the R_ runtime (RE coverage, 2026-11-02, no game runs)
rlmfc.dll is mapped at 0x10000000, its file base (Hopper addresses = game addresses). Names: rea/map/names/rlmfc.dll.tsv;
census/hopper_names_rlmfc.json applies them to Hopper (1134 applied, 0 failed).
| Functions | named strong | understood strong | understood weak | Unknown |
|---|---|---|---|---|
| 1134 (1057 Ghidra + 77 Ghidra missed: import thunks, atexit stubs, vtable slots, CRT) | 875 | 84 | 175 | 0 |
- Excluded: 416 EH funclets in the corpus (385 Unwind@, 30 Catch@, 1 __ehvec_dtor funclet) and 103 `mov eax; jmp __CxxFrameHandler` stubs.
- By area: math 375, strings 148, files 128, containers 120, other R_ 94, streams 90, exceptions 83, time 41, memory 27, crt 21, threads 7.
- Second key: PE exports, IAT and RTTI plus the Ghidra corpus; Hopper only accepted the names (addresses are procedure starts),
  no per-function Hopper read.
- Classes (81 RTTI vtables): R_CPUDriver 0x1002a24c (136 slots, all _purecall 0x100281f6) and R_CPUReference 0x1002a470 (136:
  the software math kernels); R_FileStream 0x1002a84c (6), R_UnbufferedFileStream 0x1002a8d4 (6); CObject 0x1002a6e0 (5);
  R_Exception 0x1002a6bc and 52 R_*Exception subclasses (Memory, File, MemoryFile, Script, ScriptLoader; 8 slots, one shared
  scalar deleting dtor 0x100215d0); 22 R_DLList/R_DLListBase/R_DLListInit vtables 0x1002a914..0x1002b03c (3: dtor, allocNode,
  freeNode; R_DLListBase::freeNode 0x100271b0 shared by 21); type_info 0x1002b060.
- Uncertain: identical bodies folded under one address carry many exports (0x10027790 has 16, 0x10009980 is
  R_Exception::getErrorMessage and four M_*Array getMemory); the row keeps one name and lists the rest in evidence.
  R_ScriptLoader_* helpers, rbtree_A/B_* (MSVC6 std::_Tree shape) and vector/basic_string helpers are weak, shape-only names.
  0x100062d0 CObject::Serialize_Dump_nop comes from MFC's vtable layout.

# MaxPayne.exe: visual classes (RE coverage, 2026-11-02, no game runs)
589 rows appended to rea/map/names/MaxPayne.exe.tsv (shared with platform and ghost; their 123 rows at the same addresses were
kept: 70 name the same function in other words). census/hopper_names_exe_visual.json applies these 589 to Hopper (0 failed).
| Area | particles | decals | camera | sky/fog | bullet time | effects | render | all |
|---|---|---|---|---|---|---|---|---|
| Functions | 91 | 48 | 113 | 25 | 8 | 30 | 274 | 589 |
- Levels: named strong 256, named weak 71, understood strong 262. Sources: RTTI, vtable stores, capstone, strings, calls into e2mfc/rlmfc.
  No live Ghidra/Hopper read per function (the corpus lacks many small functions).
- Classes (vtable, slots): X_ParticleSystem 0x7d8e74 (27), X_ParticleSystemImplementation 0x7d57e8 (12), PS_ParticleSystem
  0x7da72c (27); X_DecalImplementation 0x7d5178 (15), X_DecalMeshCombination 0x7d8c74 (31); MaxPayne_CameraBase 0x7d0048 (36),
  MaxPayne_PlayerCamera 0x7d00dc (37), X_CameraImplementation 0x7d56cc (30), X_Camera 0x7d3bf4 (24); X_RendererImplementation
  0x7cbb60 (67), X_Crosshair 0x7d7404 (33), X_StaticLight 0x7d8a64 (31); messages ChangeFog 0x7cc700 (9), X_* messages (8).
- Pass 2 (214 rows: render 129, effects 38, camera 32, particles 9, decals 6; named 211, understood 3; strong 63, weak 151):
  the shared base slots (named in the class that introduces them: X_LevelRuntimeObjectBase slots 1-30, the Point/Sphere
  variants), FlyCamera, PlayerCamera, X_CameraImplementation, the KF family slots, the 5 X_ParticleSystemImplementation slots,
  X_Projectile / X_ProjectileImplementation / X_ProjectileInterface, 12 onMessage_GM_* debug-draw handlers.
  Hopper second key (callees a subset of capstone's direct targets): 214 of 214 agree. That checks structure, not the name.
- Still unknown: KF slot 39 0x54f370 (file parsing), FlyCamera math 0x5097b0 / 0x509840 / 0x509a70, debug drawers 0x5bb9e0 /
  0x5bf2f0 / 0x5c05f0, GM Draw* without an identified message (0x455a10.., 0x455d40).
- Weak: fog start/end 0x677750 / 0x677760, X_CameraImplementation animation slots 22-29, parts of Crosshair, StaticLight, Decal.
  The GameModeMessage handlers were not found (X_MessageReceiver is a generic template).

# MaxPayne.exe: level and resource load path (RE coverage, 2026-11-02, no game runs)
484 rows appended to rea/map/names/MaxPayne.exe.tsv; census/hopper_names_exe_load.json applies them (0 failed).
| Area | leveldb | geometry | textures | animation | materials | resources | all |
|---|---|---|---|---|---|---|---|
| Functions | 240 | 102 | 53 | 44 | 43 | 2 | 484 |
- Levels: named 474, understood 10; strong 220 (RTTI/vtable fixes the name: ctors, dtors, deleting dtors), weak 264.
  Hopper structure check: 467 of 484 agree; the 17 others are 14 X_LevelDB* dtors where Hopper adds callee 0x5f13e0 (not in
  the bytes), a tail jmp 0x5ebb90, ~X_LevelDBImage 0x6007f0 (capstone overruns into the next function), tessellate 0x5a8b90
  (no callees from Hopper). No name looked wrong.
- Pipeline: X_Level::load 0x493f80 reads the .ldb into an R_MemoryFile -> X_LevelDBImportLevel ctor 0x5b7370, load 0x5b75a0
  (pre-pass readers 0x5b8640, 0x5b86c0, 0x5b8760, 0x5b8810) -> X_LevelDBLevel::readVersioned 0x5ec550 (version 0x20, 13
  containers, factories in slots 7-19: Level thunks 0x5ebc80-0x5ebd40, Import thunks 0x5b7470-0x5b7530) -> resolveLinks 0x5ebd50.
  Materials/textures: Material::createMap 0x5dc360 (host rn_ldb_import), Lightmap::build 0x5db1b0, Image::createGImage
  0x600980, default P_Maps 0x5dd6c0. Geometry: ImportStaticMesh::buildVirtualObject 0x5a70a0 (slot 6), tessellate 0x5a8b90,
  statistics 0x5a6e90, dynamic meshes 0x5c17b0. Progress 0x582750 / 0x582770. Animation: KF2 script loaders 0x54c9c0,
  0x54ca90, 0x54d9d0, 0x550b80, reader reset 0x55bb30, chunk dispatch 0x55bc40.
- Classes (vtable, slots): X_LevelDBLevel 0x7d2b7c (34), X_LevelDBImportLevel 0x7d1ff8 (34), StaticMesh 0x7d2f70 (13),
  ImportStaticMesh 0x7d1ee8 (13), MaterialContainer 0x7d2738 (14), Material 0x7d26ac (6), ObjectBase 0x7d2690 (5), Image
  0x7d33fc (6), DynamicMeshAnimation 0x7d3360 (6), one 13-slot class per container; ~70 LDB vtables in all.
- Pass 2 (47 rows: textures 15, geometry 15, materials 8, leveldb 5, animation 4; strong 23, weak 24; Hopper 47/47 agree):
  ImportLevel::load 0x5b75a0 runs two loops. Rooms: 0x5b7730 returns the room's BSP (ImportRoom+0x188), 0x5c1220 gives each
  BSP element its material pointer (+0x40) and a 6-flag mask (+0x3c) through the mesh polygon (finds 0x5ca570 / 0x5ca540).
  Dynamic meshes: the same with 0x5cc0c0 (+0xa6) and 0x5c1400. 0x5cee80 / 0x5cdb40 are container cursor `next` (int keys,
  8-byte entries / string keys, 0x14-byte entries); 0x5cc300 allocates ImportBSP. 0x5a5690 is the ImportMeshPolygon ctor; its
  first call builds the 13 shared level P_Materials. Also named: P_Material/P_Map/G_Image new wrappers, X_SpriteDivider tile
  loader, KF2 mesh sub-chunks 0x10007, 0x1000b, 0x1000c, 0x1001a.
- Unknown: the meaning of the 6 flag bits; folded boolean material getters (0x73f430, 0x77e0f0, 0x422340, 0x633e60, 0x5748f0,
  0x636bb0, 0x5da210, 0x5da230) left unnamed; KF2 chunks 0x10005/0x10006/0x1000f internals.
- Seen, not changed: some COMDAT-folded getters in the shared TSV carry one user's name (0x7269e0
  X_InputStateEvaluator::StateBase::getInput is also X_LevelDBImage's getter).

# MaxPayne.exe: framework (RE coverage, 2026-11-02, no game runs)
226 rows appended to rea/map/names/MaxPayne.exe.tsv (9 more addresses already had rows and kept them);
census/hopper_names_exe_framework.json applies them (0 failed).
| Area | app | modes | config | frame | gamemode | savegame | all |
|---|---|---|---|---|---|---|---|
| Functions | 66 | 67 | 51 | 18 | 20 | 4 | 226 |
- Levels: named strong 112, named weak 49, understood strong 50, understood weak 15. Hopper structure check 232/235 agree
  (0x409a90, 0x40a170, 0x451e70 differ: weak).
- Boot: entry 0x76fb04 -> AfxWinMain 0x7838b8 -> theApp 0x8a6338 (CMaxPayneApp vt 0x7cb77c, ctor 0x40a1d0). InitInstance
  0x40b7a0: S_Listener thread, startupInit 0x40b500 (window class, already-running check 0x40c6d0, registry "Remedy
  Entertainment", MaxPayneCommandLine vt 0x7cb94c ParseParam 0x40cb90 flags 0x8a6480..0x8a648d, D3D8.DLL 0x40b930, parental
  lock 0x405610, cd.ini 0x40ae60, game window 0x40cec0, MediaPlayer 0x409a50, startup dialog 0x405bf0, renderer 0x40d800,
  driver check 0x40b9b0), then a loop of startupDialog 0x40b5e0 / engineInit 0x40b670 (FPU 53-bit nearest; MaxPayne_ModeSwitch
  new 0xee, ctor 0x415840, into app+0xc8 = u32[0x8a6400]; R_Timer reset).
- Run 0x40bcd0: runFrame 0x40c020 until WM_QUIT. Shutdown: ExitInstance 0x40b900 -> engineShutdown 0x40b810 (0x40b3e0 deletes
  the ModeSwitch). Fatal: 0x40c740 message box + PostQuitMessage(-1); FrameworkQuitHandler vt 0x7cb774 (obj 0x8a6458).
- Frame: runFrame samples the app R_Timer (+0xd0, QueryPerformanceCounter), clamps to 0.1 s, averages a 4-float ring (+0xe5),
  fills an X_TimeUpdate (vt 0x7d142c: +4 dt, +8 total, +0xc scale, +0x10 real dt, +0x14 real total, +0x18 frames), then
  X_ModeSwitch::update 0x601250, X_ModeSwitch::render 0x6014d0 (renderer active, app active +0xff, not iconic),
  S_Listener::update, the PeekMessage pump. Global R_Timer 0x8afc90. Loads: pump 0x40c440 and resetFrameTimers 0x40bbf0
  around GameMode::executePendingRequest 0x451960. Focus: WM_ACTIVATEAPP 0x40d270 -> 0x40baf0 / 0x40bb50 -> resume 0x6010d0 / pause 0x601150.
- Modes (MaxPayne_ModeSwitch vt 0x7cbf0c, base X_ModeSwitch vt 0x7d3418, init 0x415dc0): switchable game +0x80
  MaxPayne_GameMode 0x7ccec4, menu +0x7c MaxPayne_MenuMode 0x7cc500, graphicnovel +0x84 MaxPayne_GraphicNovelMode 0x7ce920;
  overlays HUD +0x78 0x7cefc8, console +0x70 0x7cc404, statistics +0x74 0x7cc1fc. Switch: X_SystemMessage_ModeSwitch
  (vt 0x7d39a8) -> 0x6020c0 -> switchModeByName 0x601b40 (fade out, fade in, applied in render). X_ModeBase vt 0x7d3934
  (1 resume, 2 pause, 8/9 fade durations, 10/11 fadeIn/Out, 14 update, 15 render, 18 onKey); X_GameModeBase vt 0x7d3a2c.
- GameMode: ctor 0x44b660, init 0x44ba20, update 0x44c1b0, render 0x44ec60, onKey 0x44c170; setPendingRequest 0x44be30
  (GM+0xb4: 1 load level, 2 load slot, 3 save, 4 autosave) -> executePendingRequest 0x451960 -> levelInit 0x44fda0 /
  loadGame 0x454230 / saveGame 0x454fa0; cleanup 0x44fa10, levelDeinit 0x44fba0, dtor 0x44b810.
- Save file (writer SaveGameInterface::saveGame 0x410300, reader loadFile 0x40fff0): head `0c | id 1 | type 1 | size | 14 30`
  (version 0x30); chunk 1 (v1): int 0x30, description, thumbnail size, TGA thumbnail. GameMode::saveGame: chunk 0x10 (v0xd)
  level name + bool, chunk 0x11 (v1) with per-character chunks 0x12; level FSM states 0x495630 (sub-chunks 0x13..0x1a);
  GraphicNovelMode save 0x49b620 / load 0x49b480; HUD save 0x4aeb60 (v4) / load 0x4ae890. Version error vt 0x7cbe88/0x7cbeac.
- Correction for files.md (one source, not changed here): SaveGameInterface::init 0x40f9e0 takes SGI+9 = GameMode,
  SGI+0xd = GraphicNovelMode (ModeSwitch+0x84), SGI+0x11 = HUDMode; "+0xd level" is wrong.
- Unknown: ModeSwitch config-block classes (ctors 0x50a160..0x511430), GameMode::update internals (45 KB), GM slots 0x4540c0,
  0x4540e0, 0x454f90, the startup dialog internals (0x401000-0x409000), the profile report 0x417c70. Weak: the meaning of
  the six getStartupSetting_0x10N getters, cycleConsoleOverlay / cycleStatisticsOverlay.
- Seen, not changed: 0x5830d0 / 0x5830e0 / 0x5830f0 are named SlowMotionConfig getters in the shared TSV; this pass reads them as
  X_TimeUpdate getters (total time, time scale, unscaled total): folded bodies.
