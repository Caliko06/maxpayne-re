# files: save/load, config, .ras archives, file APIs (Max Payne 1.05)

Sources: Ghidra C in rea/c/<image>/<addr>.c, rea/map/*.tsv, census/import_sites.tsv, capstone on the OWNER's exe, and the files
the port wrote to ~/Games/save/ on 2026-11-01. FACT = read in code or data. INFERENCE / NOT VERIFIED are marked.
Addresses: exe = MaxPayne.exe 0x4xxxxx..., rlmfc = rlmfc.dll 0x1000xxxx. Names with "FUN_" are Ghidra's; the others are mine.

## 1. Save and load

### Path and names (FACT)
- SaveGameManager constructor = exe 0x40f9e0 (my name). It builds the folder in the 260-byte buffer at this+0x3d:
  1. first `<R_File cwd>\savegames` (string 0x8680dc), then
  2. LoadLibraryA("shell32.dll") + GetProcAddress("SHGetSpecialFolderPathA") (0x8680d0, 0x8680b8) and
     `SHGetSpecialFolderPathA(0, buf, CSIDL_PERSONAL=5, fCreate=1)`; if it returns 1 the buffer is replaced by
     `<My Documents>\Max Payne Savegames` (string 0x8680a4; the demo build uses "Max Payne Demo Savegames" 0x868088, chosen by
     exe 0x422180, which returns 1 in this build). So the folder is NOT relative to cwd and not from the registry.
  - Host answer: win32.c SHGetSpecialFolderPathA_ -> `C:\Documents and Settings\Player\My Documents`, maps to save/ (files.c SAVE_DIR).
    Result on disk: ~/Games/save/Max Payne Savegames/savegameNNN.mps.
- File name = exe 0x410f20 (SlotPath): `"%s\savegame%03i.mps"` (0x868318; demo `.mds` 0x868304). Slot numbers (FACT):
  0 = autosave (exe 0x410c20 returns 0), 1 = quicksave (0x410c30 returns 1), 2 = quicksave backup (0x410c40 returns 2),
  3..12 = manual slots. The Load/Save menus list slot ids 0..12 (x_data.ras `menu.txt`, MENUID_SAVEGAME_SLOT0..12, lines ~195-335).
- The menus do NOT enumerate the folder: each slot is probed by index (info cache exe 0x411270 -> validity check 0x4109c0 ->
  info reader 0x411490/0x411710). No FindFirstFileA on the save folder anywhere in the save code.
- Last save: MFC `WriteProfileStringA("Save Game", "Last Saved Game Filename", <full path>)` at the end of save (0x410300) and of
  load (0x40fff0); read back by the manager constructor (0x40f9e0) and by script S_LoadLastSavedGame (string 0x88a698).
  It is the full path (`C:\Documents and Settings\Player\My Documents\Max Payne Savegames\savegame000.mps` in registry.tsv).
- Limit: difficulty files `SaveGameLimit` (x_data.ras 1_difficulty.txt: 0 = unlimited on normal, 7 on the hardest); counter at
  game object +0x22c (0x44c020 get, 0x44c030 decrement, 0x7fffffff = unlimited). Only manual/quick saves decrement it (0x410300).

### Code path (FACT unless marked)
- Request layer: object field +0xb4 = pending request (0 none, 2 load, 3 save; 4 = autosave in the executor), +0xb8 = slot,
  +0xbc = path string (CS_LoadGameFromFile). Setter/validator = exe 0x44be30(state): state 2 checks the slot with 0x410d60/0x410ce0
  (HUD text SaveGameNotFound when missing), state 3 checks with 0x410e20 (save limit, HUD texts). Entry for "load this file" = 0x417b30.
- Executor = exe 0x451960, a per-frame step run from 0x44c1b0 (switch on +0xb4):
  case 2 -> `0x40fff0` (load by path) when slot == -1 and a path is set, else `0x40ff40` (load by slot: 0x410f20 -> 0x40fff0);
  case 3 -> `0x410300(slot, description, 0)` (manual/quick); case 4 -> `0x410300(slot, description, 1)` (silent autosave);
  case 1 (after a level change) saves again when the request is 3 or 4 after the level started. Game-mode names MP_GM_SAVE1..4
  (exe 0x454fa0) and MP_GM_LOAD1..3 (0x454230) are the in-mode state machines around these.
- Autosave triggers: script command S_AutoSaveGame / message X_SystemMessage_AutoSaveGame (strings 0x88a708, 0x889268); the Part I
  autosave seen in the port is written at level start ("Auto: ... - Part I, Prologue"). Which exact script call fires it: NOT VERIFIED.
- Quicksave / quickload: controls menu items GCID_QUICKSAVE / GCID_QUICKLOAD (menu.txt lines ~688-697) and message
  X_SystemMessage_SaveGame (0x889210); quickload asks "QuickLoadQueryText" (0x87e410). Default keys F5 / F9 are the game's known
  defaults (scancodes 0x3f/0x43; web/input.js maps them): the binding table itself was NOT VERIFIED (it lives in the Input Settings
  registry values, which the port has not written yet).
- Save writer = exe 0x410300(this, slot, flagAuto, ...) (my name SaveGame). Order:
  1. R_File cwd saved, `setArchiveHandling(0)` (disk only), description text: quick = prefix `QuickSaveGameDescriptionPrefix` (0x8682e4,
     localized), auto = `AutoSaveGameDescriptionPrefix` (0x8682c4, seen "Auto: ") + `strftime("%a %b %d %H:%M")` (0x868330)
     + " - " + level title (0x411070);
  2. quicksave only: if slot 1 is already a valid save (0x410920) then `CopyFileA(path(slot 1), path(slot 2), FALSE)` at 0x410486
     (confirmed by disassembly: pushes 0, path2 = slot from 0x410c40, path1 = slot from 0x410c30), then refreshes slot 2's info cache;
  3. R_MemoryFile (0x2000 initial) chunk 1 (see format), then two virtual serializers (this+0x24 vtable +0x74, this+0x34 vtable +0x58)
     and 0x4aeb60 write the game and level state;
  4. `R_File::isDirectory(folder)` else `CreateDirectoryA(folder, NULL)` (exe 0x41069e);
  5. `R_MemoryFile::save(path)` = rlmfc 0x174c0 -> `R_File::save(path, buf, len)` (rlmfc 0x12290): relativeToAbsolutePath +
     absolutePathToDirStructure, `checkPathExistence` (rlmfc 0xee80: CreateDirectoryA of the parent), `del` first because R_File sets
     override=1 in its constructor (rlmfc 0xef00 `setOverride(1)`; del = CRT `remove` -> DeleteFileA), refuses when `where()==2` after that,
     then CRT `fopen(path, "wb")`, ONE `fwrite` of the whole buffer, `fclose`. No compression, no temp file, no rename.
  6. on success `WriteProfileStringA` (above), refreshes the slot cache (0x411270 / 0x411710). On failure HUD text UnableToSave/SavingFailed.
- Load = exe 0x40fff0(this, path): `R_MemoryFile::construct(path, 0)` reads the whole file (R_File::load, rlmfc 0x11e20 loose-file branch:
  fopen "rb", fseek end, ftell, fseek 0, fread all, fclose), `openChunk(1)`, int version must be 0x30 else exception
  "Save game "%s" version is %i, version %i expected" (0x868260), then the same two serializers read, then 0x4ae890.
- Validity check = exe 0x4109c0: file name must end in `.mps` (or `.mds`), whole file is read and chunk 1 must exist. So every
  slot probe in a menu reads the whole file (about 0.3 MB); 13 slots -> 13 HTTP reads per menu open.
- Decision (web, #5, 2026-11-02): the save folder is a table in host memory (host/files.c SaveEnt), filled by one GET of
  /save-files.json (directories included, size -1). Lookups, listings and reads of a file already read or written answer from it;
  PUT/DELETE go out behind the game from the page's main thread, in order. Why: one save (0x410300 -> R_File::save, CreateDirectoryA,
  DeleteFileA, the registry write) was ~20 synchronous requests of 2-3 ms each: 40-71 ms in one frame of play, twice per map
  (autosave at level start + after the comic; Chrome profile of part1_level4: web_save_put 32%, web_save_lookup 18% of the 91 ms frame).

### File format (FACT from rlmfc + the files in ~/Games/save/Max Payne Savegames/)
- R_MemoryFile tagged stream (rlmfc writeTagged 0x17870, createChunk 0x18f70, closeChunk 0x19130). A chunk = tag byte 0x0c + 12 bytes
  {id u32, type u32, size u32} + payload (size patched at closeChunk).
- Observed file head (savegame000.mps, 295,894 bytes): `0c | id=1 | type=1 | size=0x24051 | 14 30 (int 0x30 = version) |
  0d 14 29 "Auto: Mon Nov 02 05:25 - Part I, Prologue" (string: tag 0x0d + length) | 12 12 40 02 (int 0x24012 = thumbnail size) |
  TGA image`. Ints are variable length (tag 0x14 + 1 byte, 0x12 + 3 bytes observed; operator<< family, rlmfc 0x10007xxx: not read).
- Thumbnail = TGA 256x192, 24 bit, 18-byte header + 147,456 bytes (G_Image::saveBuffer, format 0 = TGA, grphmfc 0x10202fb0 -> 0x10205710).
  Next chunks follow with ids 0x10, 0x11, 0x12... (level name string "part0_level1" first, then actor/object state).
- Size 287,542..295,894 bytes for a tutorial / prologue autosave.

## 2. Config the game writes
- Everything goes to the REGISTRY through MFC `CWinApp::GetProfile*/WriteProfile*` (exe 0x40a4c0 calls `SetRegistryKey("Remedy Entertainment")`,
  string 0x867000; app name "Max Payne"). No .ini/.cfg/.txt is written by the game. dsound.ini and scripts/*.ini belong to wrapper DLLs in the
  OWNER's install (read-only, not the game's).
- Keys under `HKCU\Software\Remedy Entertainment\Max Payne\` (from the port's save/registry.tsv): `Video Settings` (Driver, Display Width/Height/
  Bits Per Pixel, CPU Driver, HW Acceleration Level, Video Settings Version, Framerate Smoothness, Mipmap Bias, LOD Bias, Gamma),
  `Advanced Settings` (Texture Color Depth, Other/Level Texture Detail, Character Detail), `Audio Settings` (Effects Volume, Music Volume),
  `Game Level` (normal), `Other Settings` (Crosshair/AutoAim/AutoWeaponSelect/EnableTips/BulletTimeAid Toggle + version), `Input Settings`
  (per control N: "N Slider type", "N Button0..3 type/value"; Mouse Sensitivity X/Y, Invert Joystick, Joystick Sensitivity, Double-Click Speed; version),
  `Save Game` (Last Saved Game Filename), `Parental Lock`, `Customized Game`.
- File association written at start: exe 0x40af30 (called from 0x40a4c0): `RegSetValueA` x18 under HKCR `.mps` / `MaxPayne.SaveGame\...` using
  GetModuleFileNameA + GetShortPathNameA (exe 0x40af94, 0x40afb3).
- Host: win32.c 1142-1390 keeps the registry in memory and rewrites save/registry.tsv on every set (`reg_save` -> `layer_store`).
- Other files the game creates under the cwd (C:\MaxPayne, read-only in the host): `\screenshots\shot%04i.tga` (exe 0x40e810, CreateDirectoryA at 0x40e869),
  `\CombinationMaps\...` (exe 0x674300 CreateDirectoryA, exe 0x7350c0 `DecalCombination%02i.tga`). Whether level 1 reaches them: NOT VERIFIED.
  win32.c CreateDirectoryA_ calls `rn_halt` for any directory outside the save folder, so these stop the run if reached.

## 3. .ras archives
- Names: R_File constructor (rlmfc 0xef00) does GetCurrentDirectoryA, parses GetCommandLineA, `SetCurrentDirectoryA(dir of the exe)`, `setOverride(1)`,
  `loadArchives` (rlmfc 0xf1c0): `FindFirstFileA("*.RAS")` + FindNextFileA in cwd (string 0x1003d638), then each name -> `loadArchiveHeader` (rlmfc 0x103a0).
  Optional secondary path from `cd.ini` (exe 0x40ae60, absent in the OWNER's install, so skipped).
- Header read: CRT `fopen(GetFullPathNameA(name), "rb")`, `fread` 0x2c bytes, then file table and dir table (rlmfc 0x1001088b), `fclose`. Format in tools/ras.py
  (seed-encrypted tables, per-file method 1/2 LZSS, 3 stored). Archives are NOT memory-mapped: no CreateFileMapping for them.
- Loading one resource, R_File::load (rlmfc 0x11e20): `fopen(archive, "rb")` + `fseek(offset)` + `operator_new(stored)` + ONE `fread(stored size)` + `fclose`,
  then decrypt (method 2) and LZSS decompress (0x138e0). So every resource = open, seek, one contiguous read of its stored bytes, close. No overlapped IO.
  The CRT turns these into CreateFileA(GENERIC_READ, OPEN_EXISTING) / SetFilePointer / ReadFile / CloseHandle (MSVCRT.DLL lifted).
- Sizes (from tools/ras.py list): x_data 151.4 MB, 2664 files (2253 under 64 KB, max 22 MB); x_english 412.7 MB, 1323 files (max 6.8 MB);
  x_level1 123.2 MB, 20 files (.ldb levels 8.9-14.3 MB stored, 15-32 MB raw); x_level2 71.0 MB, 11 files; x_level3 102.5 MB, 17 files;
  x_music 286.8 MB, 30 .wav files of 4-20 MB (all method 3 = stored).
- Streaming (music and long sounds): sndmfc 0x1030e330 does `new R_UnbufferedFileStream(path, 0)` (default buffer 0x2000 = 8 KB, rounded to the
  sector size). rlmfc 0x148f0: `CreateFileA(GENERIC_READ, share 1, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING = 0x20000080)`,
  `GetDiskFreeSpaceA` for bytes per sector (host answers 512), `GetFileSize`; only method-3 archive entries stream. rlmfc 0x15360 `SetFilePointer`,
  0x153f0 `ReadFile` non-overlapped, sector-multiple sizes, sequential. Pattern for HTTP: many 8 KB reads in order per stream, several streams at once.
- Other readers: e2mfc P_Device (0x1012dd70) `SetCurrentDirectoryA(e2driver)` + `FindFirstFileA("*_cpu_mfc.dll"/driver pattern)` + LoadLibraryA;
  e2_d3d8 driver 0x10436446 uses CreateFileA/W + CreateFileMappingA + MapViewOfFile + GetFileSize for shader files (read-only mapping).
- Host (files.c + win32.c) already serves all of this: whole-resource reads of 256 KB or more go straight to the destination (one Range),
  smaller ones use a 256 KB read-ahead per handle. Nothing here needs a change for save/load.

## 4. File APIs the game imports that win32.c does not implement (compared on 2026-11-01 with the K()/Api tables of host/*.c)
Implemented: CreateFileA/W, ReadFile, WriteFile, SetEndOfFile, FlushFileBuffers, SetFilePointer, GetFileSize, GetFileType, CloseHandle, CreateDirectoryA,
DeleteFileA, FindFirst/Next/CloseFile A, GetFileAttributesA, GetShortPathNameA, GetFullPathNameA, GetLongPathNameA, Get/SetCurrentDirectoryA, GetDiskFreeSpaceA,
CreateFileMappingA (PAGE_READONLY only), MapViewOfFile, GetModuleFileNameA, SHGetSpecialFolderPathA, SHGetFolderPathA, Reg* (A).
Missing (the call halts with "missing API", dispatch.c:91):
- **kernel32!CopyFileA**: exe 0x410486 = the quicksave backup (slot 1 -> slot 2). Hit on the second quicksave. MFC42 imports it too. MUST ADD.
- kernel32!MoveFileA: MFC42, MSVCRT (`rename`). Game code (rlmfc) does not rename; low risk. Add with CopyFileA (same folder rules).
- kernel32!SetFileAttributesA: exe 0x43a4a4 (AI network .ai writer, 0x43a380, a level-editor path) and 0x40e3c4, MFC42, MSVCRT (`_chmod`). Probably unreached; a stub returning 1 is enough.
- kernel32!GetFileTime / SetFileTime: rlmfc R_File::timeStamp (0x100125c0, no caller found), MFC42 CFile. LockFile / UnlockFile: MFC42 CFile, MSVCRT. Unreached by the game; stub if hit.
- kernel32!GetFileInformationByHandle: MSVCRT (`_fstat`/`_stat`). RemoveDirectoryA: MSVCRT (`_rmdir`). GetDriveTypeA, GetVolumeInformationA, GetLogicalDrives: MFC42/MSVCRT.
- kernel32!GetPrivateProfileStringA/IntA, WritePrivateProfileStringA, GetProfileIntA: MFC42 only; the game calls SetRegistryKey so the registry path is used. GetTempPathA, GetTempFileNameA, SearchPathA: MFC42 only.
- Wide variants (CreateDirectoryW, DeleteFileW, FindFirstFileW, FindNextFileW, GetFileAttributesW, MoveFileW, RemoveDirectoryW, SetFileAttributesW,
  GetCurrentDirectoryW, GetFullPathNameW, GetModuleFileNameW, SetCurrentDirectoryW): MSVCRT only (NT code paths); the game uses the A calls.
- Behaviour gaps in implemented calls: CreateFileA ignores flags (fine for NO_BUFFERING); ReadFile/WriteFile halt on OVERLAPPED (the game never passes one);
  CreateDirectoryA outside the save folder halts (screenshots, CombinationMaps above); CreateFileMappingA only PAGE_READONLY.

## 5. State seen in ~/Games/save on 2026-11-01 (facts to test against)
- `Max Payne Savegames/savegame000.mps`: valid autosaves written by the web run through serve.py PUT (23:14 "Tutorial", 23:25 "Part I, Prologue").
  Thumbnail + chunk 1 intact: writes of game data work end to end.
- `registry.tsv`: 29,780 bytes, ALL NUL, at 23:26 and 23:27 (it was valid text 23:25: HKCU Save Game "Last Saved Game Filename" present).
  reg_save (win32.c 1198) -> layer_store -> gf_create(CREATE_ALWAYS) / gf_write / gf_close -> files.c put_whole -> web_save_put. A NUL file of the exact
  length means the PUT body came from the wrong memory. HYPOTHESIS (NOT VERIFIED): build.py:148 uses INITIAL_MEMORY=3584MB and host.h gives the guest a 3 GB
  calloc, so host malloc pointers (GFile.data, the registry text) can be above 2^31; EM_JS receives them as signed int32 and `HEAPU8.slice(src, src + n)` /
  `HEAPU8.set(a, dst)` read from a negative index. Fix candidate: `src >>> 0` / `dst >>> 0` in every EM_JS of files.c (web_save_put, web_read).
  A NUL registry loads as "empty" (reg_load stops at the first NUL), so after a reload the last-save path and the Input Settings are lost.
- gf_create with CREATE_ALWAYS flushes a 0-byte file first (files.c, `if (g->dirty) gf_flush(k)`), so the old content is gone until close.
  A page reload between the two PUTs leaves an empty save or registry.

## Open
- Prologue autosave load crash (reported by render, web: menu + F9 F9 on savegame000.mps 295,895 B -> RaiseException e06d7363, R_MemoryFile::readTag
  (rlmfc 10017700) <- operator>>(bool) 10017de0 <- exe 006e1210 <- 006f2840 <- 00495180 <- 0042a240 <- 00454230 (MP_GM_LOAD); bool tag 0x0e did not match).
  2026-11-02 on build 1e6d098 (keys only, port 5183): New Game > Fugitive wrote savegame000.mps = 295,896 B (the Prologue autosave, one byte more
  than the failing file); Options-free path menu > Load > slot 0 loaded it with no exception ("New Jersey. Three Years Ago." scene, load
  stalled ~100 s on web, flow's stall). Copy kept at the scratchpad (saves/prologue_autosave.mps). So the crash does NOT reproduce after daf1722
  (whole-file read at open): the failing file was most likely a torn read of a file another page was rewriting. F9 from the menu (key 19 s, 2nd F9 in game at 60 s) on the
  same file: the level loaded, "Press F9 again to Quickload, Esc to Cancel" shown, no exception in 200 s.
- Fixed on the way (daf1722): web saves are read whole at open (no torn read when another page rewrites the file mid-load).
  Not proven to be the cause. flow-61 adds C++ exception unwinding in win32.c, so a bad save may then be caught by the game.

## Save/load inside the level, web (2026-11-02, port 5183, build main eda1e9d+, keys only, RN_SOUNDLOG)
New Game > Fugitive; level playable by 330 s (hallway, Max at the spawn). F5 at 330 s, W held 2.5 s at 336 s (footstep Plays #28-#35, 336-339 s),
Esc at 344 s (level sounds stop, menu music Play 343.0 s), in-game menu = Resume, Load, Save, Options (no Tutorial/New Game: S S S lands on Options),
S + Space = Load page, S + Space = slot 1: menu music Stop 353.5 s and the level sounds Play again at 353.7 s (level state restored), final shot at 500 s =
Max at the spawn pose in the hallway, not where W had taken him. Files after: savegame000 (autosave 295,897 B) savegame001 (quick, 295,945 B)
savegame002 (the previous quicksave copied by 0x410486, 295,946 B): the slot 1 -> 2 rotation works through serve.py PUT. No exception. The page log has
no save/load line of its own (files.c prints nothing by default); serve.py logs nothing either.

## Read pattern at boot and level load, web (2026-11-02, RN_FILELOG=1, keys to level 1, 330 s, port 5183)
Guest reads (R_File fread through gf_read): 8,843 reads, 142 MB. By file: x_data.ras 3,498 reads / 69 MB (median read 8 KB, 2,919 reads <= 4 KB),
x_english.ras 2,018 / 21 MB, x_music.ras 1,872 / 14 MB, Part0_Level1.ldb 126 / 31 MB (one sequential 256 KB+ read each), the level .ras files
only a few reads (header). x_data.ras pattern: 1,227 sequential, 801 forward jumps, 1,469 backward jumps. Before each resource the game
re-reads the same two index pages (offsets 102,533,583 and 102,537,679, 4 KB each) and then jumps to the data (131-138 MB region), so every
resource costs index, data, index, data.
Old host (one 256 KB read-ahead buffer per handle, dropped on every jump, new handle on every reopen): 3,037 requests, 710 MB, 6.1 s of reads;
x_data.ras alone 2,346 requests / 585 MB for 69 MB asked (1,384 re-fetches of an offset already fetched).
New host (files.c, one cache of 640 aligned 256 KB blocks, LRU, keyed by file name so reopens hit): 820 requests, 163 MB, 1.7 s, same game
state (hallway reached). Remaining requests = distinct blocks touched (x_data.ras 297). Reads of 256 KB or more still go straight to the
destination. Native build reads the disk and is unchanged.

## Coverage (2026-11-02, names in rea/map/names/): exe files and save (SaveGameInterface, ModeSwitch, SharedDB, LevelDB, KF2)
Names come from Ghidra-corpus + disassembly reading by a Sonnet worker, spot-checked by the platform slice; one source = weak, two = strong.
'understood' = body read and role stated; 'named' = name justified by class/shape only. Full table: rea/map/names/<image>.tsv (addr, name, level, confidence, summary, evidence).

| set | functions in table | named before | named now | unknown | understood (strong) | named outside the table |
|---|---|---|---|---|---|---|
| files + save (subsystem) | 384 | 1 | 384 | 0 | 156 (111); 221 more are named by shape only (weak) | 15 |

Table: 384 functions (335 files + 49 save). names_files.json has 399 entries: all 384 plus 15 helper functions
outside the table that the save manager needs (0x40f650, 0x40fda0, 0x411070, 0x411270, 0x411490, 0x411620,
0x411710, 0x411c50, 0x411e60, 0x417b00, 0x417b70, 0x417ba0, 0x417c30, 0x417c40, 0x528dc0).
Level: 168 understood (body read, one-line role), 231 named (name from shape / vtable / caller, body only skimmed).
Most "named weak" entries are template internals or one-line getters: the name says the shape, the real role is NOT proven.

Important: the "files" label is wide. Only about 60 functions are real save/file code. The rest call
R_File / R_MemoryFile / R_Script, so they belong to: LevelDB (level database) serialization, KF2 keyframe
loader, X_SharedDB parsers, input evaluator save/load, STL map/vector instantiations.

#### 1. Save system

##### MaxPayne_SaveGameInterface (vtable 0x7cbe78, ctor 0x40f650, init 0x40f9e0, dtor 0x40f890, deleting dtor 0x40f780)
Role: save manager owned by MaxPayne_ModeSwitch (+0xd4). Object layout:
- +4 byte flag, +9 gamemode, +0xd level, +0x15 G_Image thumbnail (vtable 0x7cbe80)
- +0x1d std::map<int,SlotInfo*> head (sentinel DAT_008a662c, shared refcount DAT_008a6628), count +0x25
- +0x29 last-saved path (rs), +0x3d 260-byte folder `<My Documents>\Max Payne Savegames` (SHGetSpecialFolderPathA, CSIDL_PERSONAL=5)
Methods: init 0x40f9e0 (registry "Save Game" / "Last Saved Game Filename"), loadSlot 0x40ff40, loadFile 0x40fff0,
saveGame 0x410300, isSlotValid 0x410920, isPathValid 0x4109c0, slot constants 0x410c20/30/40 (0/1/2),
checkLoadPath 0x410ce0, checkLoadSlot 0x410d60, checkSaveAllowed 0x410e20, slotPath 0x410f20,
makeDateDescription 0x411070, getSlotInfo 0x411270, SlotInfo ctor 0x411490, ~SlotInfo 0x411620, SlotInfo::readFile 0x411710,
snapThumbnailSize 0x411c50, flushCache 0x40fda0, file association 0x40af30 (HKCR MaxPayne.SaveGame).
Exceptions: MaxPayne_SaveGameInterfaceException 0x7cbeac (ctor 0x4102d0), _OldVersion 0x7cbe88 (static object 0x8b8570).
Slots: 0 = autosave, 1 = quicksave, 2 = quicksave backup (CopyFileA slot1 -> slot2 in saveGame), 3..12 manual.
File name `%s\savegame%03i.mps` (`.mds` demo). File = R_MemoryFile tagged stream:
chunk 1 = header (version 0x30, description, thumbnail size, TGA), chunk 0x10 = level name + bool, chunk 0x11 = game state
(sub-chunks 0x12 and 0, versioned counter up to 0xd).

##### SlotInfo (0x34 bytes, created by 0x411490)
+4 path rs, +0x14 description rs, +0x24/+0x28 uv scale floats, +0x2c P_Map thumbnail, +0x30 P_Material.

##### MaxPayne_ModeSwitch message slots (vtable 0x7cbf0c; +0x80 GameMode, +0xd4 SaveGameInterface)
#12 0x417b00 LoadGame(slot) -> request 2; #13 0x417b30 LoadGameFromFile(path) (slot -1, path, request 2);
#14 0x417b70 SaveGame(slot) -> request 3; #15 0x417ba0 AutoSaveGame (desc 0x410f00, slot 0, request 4);
#16 0x417c30 call vtable +0x30 with -1 (weak); #17 0x417c40 isPathValid thunk.

##### MaxPayne_GameMode (vtable 0x7ccec4) save-related
Request state machine: +0xb4 pending (2 load, 3 save, 4 autosave), +0xb8 slot, +0xbc path rs, +0x22c save limit
(0x7fffffff = unlimited). setPendingRequest 0x44be30, setPendingSlot 0x44bec0, set path 0x44bed0 (not in table), getSaveLimit 0x44c020.
#25 loadGame 0x454230, #29 saveGame 0x454fa0; level FSM state 0x495630 (chunks 0, 0x13, 0x14), two bools 0x657950.
Executor 0x451960 (not in table) runs the request.

##### Savegame config (script block "savegame"), parser 0x50cb10, ctor 0x50c890
8 rs fields, 0x10 apart: +0 SaveScreen, +0x10 LoadHUDText, +0x20 SaveHUDText, +0x30 SaveLimitedHUDText,
+0x40 UnableToSaveHUDText, +0x50 SaveGameNotFoundHUDText, +0x60 CannotSaveHUDText, +0x70 SavingFailedHUDText;
plus ScreenShotWidth/Height. Tiny getters 0x6dfb40, 0x74e8e0, 0x74fe00, 0x74fe10, 0x50f5c0, 0x406a60 (outside table).

##### Options
0x4554c0 getBulletTimeAid / 0x4554b0 setBulletTimeAid, global DAT_0086c484 (registry "Other Settings" / "BulletTimeAid Toggle").

#### 2. Startup / data loading
- X_SharedDB (ctor 0x528d90, zero-init 0x529590, load 0x528dc0, MS+0x88): loads Materials.txt (0x52e3d0), Particles.txt
  (0x52bec0), Sounds (0x534d90), Music.txt (0x5422b0), Projectiles (0x5412a0), then 0x5295d0, 0x529750, 0x5298d0, 0x529a50,
  ObligatoryIndices.txt (0x529bd0), 0x529e20, 0x52a060, 0x52a2b0, 0x52a4f0, 0x52a730, 0x52a970. R_Profile name "X_SharedDB".
- String table: 0x528b40 getText(id) (lowercase, map find, throws "Undefined string identifier"); dialog variants 0x400bc0 / 0x4015c0.
- Animation parser 0x53dba0 (not in table) uses 0x53e940..0x53fc20 (movement + message blocks).
- Static rs initializers (10 bytes, mov ecx,<global>; jmp 0x524da0): 0x40f620 -> 0x8a6608, 0x528d60 -> 0x8ad108,
  0x5cf880 -> 0x8b0f58, 0x5e8440 -> 0x8b10d0, 0x5f62c0 -> 0x8b1308, 0x65a450 -> 0x8b3ee8, 0x66ea60 -> 0x8b4110.
  0x415760 = global "globalconfig.txt" name (0x8a6690). 0x5824b0 = R_Timer at 0x8afc90.
- X_DifficultyLevelContainer (vt 0x7d5880): 0x676570 parses "properties" (SaveGameLimit, IntermediateLevels,
  HealthGainedFromPainkiller, DeathFrequency, FrequentDeaths, frequentdeathmessages, endofgamemessages).

#### 3. LevelDB serialization (level database, R_MemoryFile tags: 0x1c list, 0x1f container, 0x25 named entry)
Object classes: slot 0 = read, 1 = write, 5 = scalar deleting dtor. Container classes: 13-14 slots, #5 deleting dtor.
- X_LevelDBObjectBase vt 0x7d2690 (base ctors 0x5e5400 int-keyed, 0x5e8520 rs-keyed Startpoint, 0x5fb890 rs-keyed FSM).
- X_LevelDBRoom vt 0x7d2808: #2 read 0x5e4550, #3 write 0x5e4c30. X_LevelDBRoomProperties vt 0x7d282c (+4 float, 1.0 on ctor).
- X_LevelDBRoomContainer vt 0x7d2848, X_LevelDBImportRoomContainer vt 0x7d2288 (ctor 0x5cfa00).
- X_LevelDBLevelObject vt 0x7d28f0: #0 read 0x5e81c0, #1 write 0x5e8280; +8 name, +0x18/+0x48 matrices, +0x7c id, +0x78 resolved pointer
  (sorted vector (id,ptr) lower_bound 0x5f6270, resolver 0x5e82d0 called from X_LevelDBLevel #33 0x5ebd50).
- X_LevelDBStartpoint vt 0x7d290c (ctor 0x5e8470, 0x98 bytes), ImportStartpoint 0x5d2ce0, ImportStartpointContainer vt 0x7d2394.
- X_LevelDBImportCharacter vt 0x7d2420: #1 write 0x5ea480. X_LevelDBImportDynamicMesh vt 0x7d20fc and X_LevelDBDynamicMesh vt 0x7d2d14:
  read/write with six bool flags at +0xa0..+0xa5.
- FSM: X_LevelDBImportFSMContainer vt 0x7d23e8, X_LevelDBFSMContainer vt 0x7d32b8 (dtor 0x5fba00); maps sentinel DAT_008b1378 / 008b1374.
- DynamicMeshAnimation: Import vt 0x7d251c (#1 write 0x5ff7f0), container vt 0x7d2538 / 0x7d3384; quaternion math 0x5ffcb0 / 0x5ffe00 / 0x5ffe70.
- X_LevelDBMaterialContainer vt 0x7d2738: #3 write 0x5ddca0.
- T_Graph (curve data): read 0x7830c0, write 0x7833c0 (version 3, tag 0x71), evaluate 0x783510, free 0x783080.

#### 4. Other save/load participants
- MaxPayne_LocalCharacterInput vt 0x7cff9c (inner X_CharacterInput at +0x1b5): #1 load 0x502fc0, #2 save 0x503070;
  slots 3,4,19,22,24-35 are pure forwarders to the inner vtable; 38/39/40 call X_PlayerInputEvaluator init/release/reset first.
  Slots 3/4 (0x503280/0x5032b0) -> inner +0x5c/+0x60: matches rea/map/player.md.
- X_PlayerInputEvaluator vt 0x7d8c18: load 0x72aae0 (chunk 0), save 0x72ac00 (createChunk 0,1); 11 sub-evaluators at +0x20..+0x10e.
- X_ProjectileImplementation vt 0x7d5088, X_LevelItemImplementation vt 0x7d562c, X_ParticleSystemReceiver vt 0x7d54cc
  (load 0x667d00 shared with X_WeaponSet, X_DecalImplementation, X_Character, X_LevelRuntimeFSM, X_Projectile), X_WeaponSet vt 0x7d9ad4 (#1 0x7578e0, #2 0x757a60).
- AI network file writer 0x43a380 (version 0x7b, SetFileAttributesA + R_MemoryFile::save), called by 0x437510.
- KF2 keyframe loader (0x54b..0x58x): top reader 0x55bc40, chunk readers 0x54bec0 (0x10000), 0x54bbe0 (0x10001), 0x577940 (0x10003),
  0x577ab0 (0x10004), 0x567800 (0x10005), 0x565740 (0x10006), 0x566fb0 (0x1000e), 0x580eb0 (0x10014), 0x577d80 (0x10016);
  geometry script parser 0x550350 (CrossMode linear / normalized / orthonormalized).

#### 5. Weakest names / unknown role (named only, role NOT proven)
- Tiny getters/setters: 0x44bcd0 (+0xac), 0x44bce0 (+0xf4), 0x44bcf0 (+0x10d0), 0x5e3b20, 0x5ea640, 0x5ea650, 0x5faf70, 0x5faf80,
  0x546d20, 0x7482d0, 0x7578b0, 0x757b30, 0x756130, 0x72e5a0, 0x72e890, 0x55b910-0x55b9c0 (KF2 globals): class and meaning unproven.
- Template internals (map/vector/sort/allocator for 8/16/20/0x24-byte records): 0x411ed0, 0x412190, 0x412570, 0x412630, 0x5d4cc0, 0x5d4df0, 0x5d93d0, 0x5d9450, 0x5d94d0,
  0x5fbc20-0x5fcff0, 0x5e0330, 0x5e0490, 0x5e7260, 0x5e7370, 0x66eae0-0x670550, 0x55c220-0x55cf40, 0x55d170-0x55d5b0: only the container type is known.
- KF2 mesh accessors 0x565450-0x56aa30, 0x580d10-0x580da0: element layout inferred from index arithmetic, no RTTI.
- Callback stubs 0x455d00, 0x457bf0, 0x457c50, 0x457cb0, motion helpers 0x74a3c0-0x74a660, 0x76dc20-0x76e200 (a movement record with flag +0x112),
  0x762a10-0x762a50, 0x7667d0-0x7667f0, 0x72e170, 0x72e330, 0x738470, 0x7384d0: owner class not proven.
- 0x40f5e0 (receiver unlink) and 0x410ef0 (state thunk) are weak. 0x4541c0 internals not decoded.
- NOT VERIFIED: 0x40fda0 registry flush, 0x4ae890 / 0x4aeb60 (load/save tails) not read.

#### 6. Contradictions / additions versus rea/map/*.md
- files.md says 0x4109c0 validates a slot; it takes a path rs (slot -> path is 0x410f20) and checks the extension, existence and chunk 1.
- 0x411490 only allocates the SlotInfo; the real reader is 0x411710 (builds the P_Map/P_Material thumbnail too).
- Load reads chunk 0x10 (level name) then 0x11; chunk 1 is only the header (version 0x30).
- ModeSwitch slots #12..#15 = LoadGame, LoadGameFromFile, SaveGame, AutoSaveGame (files.md named only 0x417b30).
- 0x4554b0 / 0x4554c0 are the BulletTimeAid setter/getter; 0x528dc0 is X_SharedDB::load.
