# Max Payne whole-game map

One row per function (22,569) of the six images in `subsystems.tsv`:
`image, entry, name, subsystem, confidence, basis`. Addresses are the rebased ones of `rea/map/<image>.tsv`.
Made by `tools/ghidra/classify.py` (rerun it to regenerate; takes 3 s). Inputs: the Ghidra tsv files and `rea/c/`.

## How the labels were made
- FACT: class/namespace of a DLL function (P_Driver is render, S_Sound is sound ...), thunk/unwind/catch names,
  atexit stubs, a string in the function (file name, menu text, error message), or a call to a class method
  (for MaxPayne.exe the class is read from the decompiled C, e.g. `P_Text::print` means ui).
- INFERENCE: spread over the call graph (callers and callees) plus address neighbours (one source file is one
  address range), 5 rounds with falling thresholds; string/call majorities that were not unanimous;
  default image label for the D3D8 driver, the sound DLL and grphmfc when nothing else spoke
  (basis says "image default"). Round-5 inference is weak (about 60% right on a hold-out test of known labels;
  rounds 1-3 about 85%). Treat INFERENCE as a pointer, not as proof.
- Unknown is left unknown when no evidence is strong enough.
- Judgement calls: R_Script/R_File/R_MemoryFile are `files` (parsers), G_Image is `render` (texture decoders),
  R_CPUReference and M_* are `math`, R_Exception/MFC/MSVCRT/thunks/unwind are `crt_runtime`, std::rs is
  `strings_containers`. Template container code of the exe (intrusive lists with the 0x7fffffff count sentinel)
  is `strings_containers`. In the exe, a function that calls P_Camera/P_Material methods is labelled by that
  class even if it is game code that uses the engine.

## Counts per image
| image | functions | FACT | unknown |
|---|---|---|---|
| MaxPayne.exe | 15702 | 14.2% | 2779 (17.7%) |
| e2mfc.dll | 2310 | 84.1% | 6 (0.3%) |
| rlmfc.dll | 1473 | 86.8% | 13 (0.9%) |
| grphmfc.dll | 303 | 51.5% | 0 |
| sndmfc.dll | 466 | 78.1% | 0 |
| e2_d3d8_driver_mfc.dll | 2315 | 53.9% | 0 |

## Counts per subsystem (exe / e2mfc / rlmfc / grphmfc / sndmfc / d3d8)
| subsystem | exe | e2mfc | rlmfc | grphmfc | sndmfc | d3d8 | total |
|---|---|---|---|---|---|---|---|
| render | 918 | 826 | 0 | 208 | 0 | 1343 | 3295 |
| scene | 2159 | 550 | 0 | 0 | 0 | 40 | 2749 |
| physics | 60 | 19 | 0 | 0 | 0 | 0 | 79 |
| sound | 379 | 0 | 0 | 0 | 287 | 0 | 666 |
| input | 190 | 0 | 0 | 0 | 0 | 0 | 190 |
| files | 335 | 0 | 340 | 0 | 0 | 84 | 759 |
| memory | 1 | 0 | 13 | 0 | 0 | 0 | 14 |
| timing | 42 | 0 | 55 | 0 | 0 | 0 | 97 |
| ui | 531 | 51 | 0 | 0 | 0 | 0 | 582 |
| graphic_novel | 23 | 0 | 0 | 0 | 0 | 0 | 23 |
| level | 875 | 0 | 0 | 0 | 0 | 0 | 875 |
| game | 3061 | 0 | 0 | 0 | 0 | 0 | 3061 |
| save | 49 | 0 | 0 | 0 | 0 | 0 | 49 |
| video | 19 | 0 | 0 | 0 | 0 | 0 | 19 |
| math | 3 | 1 | 364 | 1 | 1 | 0 | 370 |
| strings_containers | 3496 | 24 | 189 | 3 | 36 | 7 | 3755 |
| crt_runtime | 782 | 833 | 499 | 91 | 142 | 841 | 3188 |
| unknown | 2779 | 6 | 13 | 0 | 0 | 0 | 2798 |

Gaps worth knowing: `physics` and `memory` are small. Collision lives in P_CollisionBuffer (e2mfc) and in exe
functions with "capsule/collision" strings; no separate physics library was identified. The 2779 unknown exe
functions are mostly small helpers and template code with no string, no class call and no labelled neighbour.

## DLL structure (class -> subsystem)
- e2mfc.dll (engine): render = P_Driver, P_Device, P_Material, P_Map, P_Bitmap, vertex/buffer/shader classes;
  scene = P_Scene, P_BaseObject, P_VirtualObject, P_Camera, lights, meshes, particle systems, P_LODObject;
  ui = P_Text, P_Font; physics = P_CollisionBuffer.
- rlmfc.dll (runtime): files = R_File, R_MemoryFile (chunks), R_FileStream, R_Script; timing = R_Timer, R_Profile;
  math = M_*Array, M_FPUControl, R_CPUReference (CPU vector math); strings_containers = std::rs, R_STR.
- grphmfc.dll: G_Image (TGA, JPG, DDS load and save), G_Color.
- sndmfc.dll: S_Listener, S_Sound, S_SoundOmni/Spot, S_Loader, S_WaveLoader, S_Device.
- e2_d3d8_driver_mfc.dll: P_D3D* classes; DRV_* entries are 5-byte thunks (crt_runtime by name) to the real code.

## Key entry points
Frame and window (MaxPayne.exe)
- 0x0076fb04 `entry` (CRT start) -> MFC app; 0x00409ac0 window creation + movie play (video, "Media player").
- 0x0040bcd0 calls 0x0040c020 = per-frame step: R_Profile::startFrame, FPU checks, PeekMessage, timers,
  sound update ("Modeswitch sound update"). 0x0040c440 = message pump (TranslateMessage/DispatchMessage).
- 0x005828c0 = scene render (P_Driver::beginScene, endScene, swapScreen); 0x0040e0e0 = endScene + swapScreen.
- 0x00415dc0 MaxPayne_ModeSwitch (switches menu / game / graphic novel mode); 0x006014d0 mode-switch render.
- 0x0061ffe0 FPS counter text; 0x00422d60 statistics overlay (player, health, velocity).
Menu, level, game, save
- 0x0042a240 MaxPayne_MenuMode; 0x00634080 DeveloperMenu; 0x00774420 X_ScrollingTextWindow.
- 0x0044fda0 level init ("Level init", 7 KB); 0x0044fba0 level deinit; 0x00493f80 MP_GM_LEVEL_INIT steps;
  0x005a8b90 "Level Database" (X_LevelDB import, 13 KB); 0x0052fc80 X_SharedDB checks.
- 0x0044ba20 and 0x0044f190 MaxPayne_GameMode; 0x00683f50 weapon sets; 0x004db1f0 AI state machines.
- Save: 0x0040fff0 MaxPayne_SaveGameInterface (version check), 0x00410300 save game, 0x00410f20 `savegame%03i.mps`,
  0x00454230 MP_GM_LOAD*, 0x00454fa0 MP_GM_SAVE*, 0x0050cb10 "savegame".
- Graphic novel: 0x0049cab0 MaxPayne_GraphicNovelMode, 0x004a8420 page.
Input and engine start
- 0x0051a6c0 DirectInputCreateA caller (X_InputDeviceException); 0x00517e70 keyboard; 0x004cb3d0 input settings.
- 0x0040b9b0 enumerates 3D drivers ("3D engine drivers not found"); 0x0050e8a0 startup video name.
Engine DLLs
- e2mfc P_Driver: swapScreen 0x10132ac0, clearScreenBuffers 0x10133010, beginScene 0x101326d0, endScene 0x10132760,
  init 0x10131b50, setFullscreenMode 0x101321d0, setWindowMode 0x10132460; P_Scene::render 0x10118ec0.
- D3D8 driver: 0x10425dc0 loads D3D8.DLL and calls Direct3DCreate8 (device creation path, reached through thunk
  0x10401258); DRV_endScene 0x10428560; DRV_swapBuffer 0x1042a420; vertex shader creation 0x10405f60,
  pixel shader 0x10406810.
- rlmfc: R_Timer::getAge 0x10027640; R_System performance counter and CPU info; R_MemoryFile chunk API.
- sndmfc: 0x10301590 sound library init (CoInitialize, DirectSound via Ordinal_2 at 0x1030e4b0, S_Device).

## Files
- `subsystems.tsv` the labels; `tools/ghidra/classify.py` the rules (class table, string rules, propagation).
- Per-image inputs: `<image>.tsv` in this folder; decompiled C in `rea/c/<image>/<addr>.c`.
