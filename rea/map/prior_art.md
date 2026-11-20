# Prior art: original Max Payne engine behaviour and bugs that matter to a 60 fps port

Sources: PCGamingWiki (archived snapshot 2026-10-27, the live wiki sits behind Cloudflare), ThirteenAG WidescreenFixesPack `mp1`, the MOGGA patch page, three public recompilation/port projects, two Windows-fix repos, and this repo's own RE notes (`rea/map/gameplay.md`, `ai_behaviour.md`, `render.md`, `input.md`). Checked 2026-11-03. Marking: **FACT** = read in the source named; **INFERENCE** = my reading; **NOT VERIFIED** = could not confirm.

---

## 1. No frame limiter, and what high fps actually breaks

- **FACT** (PCGW, Video, frame rate row): "No in-game FPS limiter. High frame rate breaks physics" (their ref 11).
- **FACT** (PCGW, Issues §9.16 "High frame rate issues"): "Shootdodge and jump distance are shortened, which makes certain areas incompletable." Their fixes: turn Vsync on and set the refresh rate to 144 Hz or lower, or cap the frame rate at 144 FPS or lower. They also note driver-level caps do **not** work.
- **FACT** (MOGGA patch feature list): "An optional frame limiter provides an adjustable cap from 60 to 240 FPS" and "Fixed frame-rate-dependent bugs".
- **FACT** (PCGW entry for the ThirteenAG fix): "An optional frame limiter provides an adjustable cap from 60 to 240 FPS."
- **FACT** (this repo, `gameplay.md` time model): the engine keeps one timer (`X_TimeUpdate` 0x60dbf0) with `+4 = dt*scale`, `+0x10 = real dt`, `+0x18 = frame count`; characters sub-step using the scaled dt (0x687f20); there is a fixed substep cap of 0.11 s (0x7d5920).
- **INFERENCE**: so the simulation is per-render-frame with a dt, and the substep cap only protects the slow end. At 120 fps dt is halved, and anything the engine quantises per frame (animation-frame events, a jump impulse applied on one frame, a dodge whose distance comes from the animation's EndPosition) lands differently.
- **FACT, and the fixes disagree on the number**: PCGW says cap at 144 fps or lower, the ThirteenAG and MOGGA limiters offer 60 to 240, and MOGGA additionally claims the frame-rate bugs themselves are fixed. The only number that every source treats as safe is 60, which is also what the two public recompilations target (§7).
- **Expect in our port**: the shoot-dodge should still end at 5.25 m forward / 3.5 m diagonal (data EndPosition, `gameplay.md`), the shoot-dodge jump is `C_Jump(4.5)` at animation frame 17, and a full bullet-time bar lasts 15 s of real time. If those three numbers move with fps, we inherited the bug.
- **How to recognise it**: same scripted inputs at 30, 60 and 120 fps; measure dodge end position, peak jump height, and a bar drain to zero. A 5 % drift at 120 fps is the bug; a 40 % shortening is the original behaviour at uncapped fps.

## 2. Bullet time, game speed and the sound pitch

- **FACT** (`gameplay.md`): three speeds, `GameSpeed`, `ControlledGameSpeed`, `GlobalGameSpeed`, combined as `scale = (global != 1.0 ? global : min(game, controlled))`, clamped `<= 1.0`, and each one ramps linearly in **real** time over its duration.
- **FACT**: bullet time = `ChangeControlledGameSpeed(0.2, 0.3)`; the sniper bullet cam = `ChangeGlobalGameSpeed(0.01, 0)`; a plain roll dodge = `ChangeGameSpeed(0.5, 0)`; shoot-dodge inside bullet time does not stack below 0.2 because of the `min`.
- **FACT**: the same call sets audio pitch per channel: `pitch = min + (1 - min) * scale`, with `Music/3D/2D MinimumPitchMultiplier` = 1.0 / 0.15 / 1.0.
- **FACT**: the meter drains with the **real** dt (0x44cec8 reads timer +0x10), full bar = 15 s, a shoot-dodge costs 10 %.
- **INFERENCE**: bullet time is fps-independent by design (real-time ramp and drain), so a port that computes the ramp per frame without the real dt will drain faster at 60 than at 30. The two things easy to break and easy to hear: the 0.3 s ramp and the music pitch (music stays at pitch 1.0 during slow motion, only the 3D channel drops to 0.15).
- **NOT VERIFIED**: whether the original also clamps the scaled dt per frame outside characters (the 0.11 s substep is character-side).

## 3. Sound on modern Windows

- **FACT** (PCGW §9.3 "Missing audio"): the fix is "DirectSound3D restoration software" (DSOAL), plus an "unofficial patch" run through `MaxBatch.bat` that "relies on automatic recompression of audio files without quality loss to another format". Notes: with dgVoodoo use version 2.72 or older; on newer versions run as administrator.
- **FACT** (PCGW, ThirteenAG fix entry): "Comes with DSOAL to fix sound issues."
- **FACT** (silentgameplays Max-Payne-Fix-Windows-11 README): the sequence is legacy DirectPlay + .NET 3.5, install the DirectX 9 runtime, copy the sound patch, run `MaxBatch.bat` and press C then E to convert the files, launch the game once, then drop the wrapper files (`d3d8.dll`, `d3d9.dll`, `d3d9.ini`, `global.ini`, `MaxPayne.WidescreenFix.asi`, `.ini`). It also offers swapping `rlmfc.dll` "for AMD CPUs".
- **FACT** (PCGW §9.5 "JPEG errors on modern CPUs"): the fix is a fixed `rlmfc.dll`; `luigoalma/maxpayne-grphmfc-jpg-patcher` is a DLL patch for an alternative JPEG loader.
- **INFERENCE**: the missing-audio family is a Windows audio-stack problem (DS3D/EAX-era calls, and in some builds the audio files themselves), not a game-logic problem. A recompilation that calls its own mixer should never see it; what it can still get wrong is per-channel initialisation (voices vs music vs 3D), the pitch link from §2, and the sample format of the converted files.
- **How to recognise it**: music but no voices = per-channel or per-bank init; nothing at all = mixer never started; voices too fast/slow during bullet time = pitch formula missing; crackle at 44.1 vs 48 kHz = resample.
- **NOT VERIFIED**: PCGW's middleware section is empty ("No middleware information; you can edit this page"), so I did not confirm the audio middleware from a public source.

## 4. Rendering and modern Windows

- **FACT** (PCGW): Direct3D 8, DirectDraw only for the intro video; 32-bit executable, "may need the LAA flag applied"; D3D8 games on Windows 10+ need a wrapper for Vsync.
- **FACT** (PCGW issues): "Error: setFullscreen - creating the device failed: E_FAIL", "Game won't launch, shows up in task manager, white screen", "Random freezes", "Visible polygon seams", "Permanent exclamation mark in cutscenes", "Black screen on intro video (Proton/Linux)".
- **FACT** (MOGGA): D3D8→Vulkan translation, high-resolution UI and fonts, "fixes the stretched main menu and rearranged novel layout", raw input with DPI scaling, borderless window mode.
- **FACT** (aldotoa recomp README): native resolution with Hor+ widescreen and a HUD anchored to the real screen edges; `--view stretch` restores the original 4:3 stretched.
- **INFERENCE**: aspect ratio and HUD anchoring are a design decision that three separate projects each solved their own way; the safe default for comparison with the original is 4:3 stretch or pillarbox, and Hor+ only as an option.
- **How to recognise it**: HUD drifting off the edges at 16:9 is the "HUD is 640x480 virtual" fact in `rea/map/hud_2d.md` being ignored; menus stretched is the same thing in the menu layer.

## 5. AI and dead bodies

- **FACT** (PCGW, ThirteenAG feature list): "Fixes Adaptive difficulty".
- **FACT** (`rea/map/ai_behaviour.md`): "no AI-specific accuracy and no difficulty term in AP/MT code"; difficulty reaches enemies only through `EnemyHealthMultiplier` (0x676370). `gameplay.md` agrees: difficulty scales max health and ammo found only.
- **INFERENCE**: whatever the community calls "adaptive difficulty", our RE says it is not enemy aim. If we see enemies shooting better after a death, it is not this.
- **NOT VERIFIED**: ragdoll behaviour as a function of frame rate. I found no public report and did not test it. Watch the dead-body sphere (`CharacterTestSphere` 0x68f600) and the explosion test sphere at large dt.

## 6. Community patches, and what each one fixes

| Patch | What it fixes (as its own source says) | Notes for a recomp |
|---|---|---|
| Official 1.05 | last official patch; Steam ships 1.05 | the base every tool assumes; aldotoa hash-checks every module and refuses anything else |
| ThirteenAG WidescreenFixesPack `mp1` | Hor+ aspect, HUD scaling, FOV, FMV scaling, startup hang, JPEG errors; bundles DxWrapper (D3D8→D3D9) and DSOAL; controller support; ini instead of registry; optional frame limiter 60-240 FPS; "fixes Adaptive difficulty" | the de-facto standard reference for expected behaviour at 16:9 |
| MOGGA patch | D3D8→Vulkan, hi-res UI/fonts, unstretched menus and novel layout, raw input with DPI, borderless, frame limiter 60-240, "fixed frame-rate-dependent bugs", crash and sound fixes, disables adaptive difficulty | claims the frame-rate bug is fixed; useful as a behavioural oracle at 144 fps |
| Darkje sound patch (+ `MaxBatch.bat`) | recompresses the audio files so the game has sound on modern Windows | changes the audio data, so a port comparing to it compares different files |
| `Corrected Max Payne` (modDB) | "adds new fixes" | NOT VERIFIED which bugs; no changelog read |
| `luigoalma/maxpayne-grphmfc-jpg-patcher` | alternative JPEG loader code (the JPEG errors on modern CPUs) | irrelevant if we decode textures ourselves; relevant if we keep grphmfc |
| silentgameplays Win10/Win11 packs | orchestration of the above plus a 1080p/60 fps lock | shows the common working combination, not a single fix |

## 7. Public Max Payne recompilation and tooling repos

| Repo | What it is | Useful facts |
|---|---|---|
| `aldotoa/max-payne-recomp` | static recompilation of the 32-bit Windows build to arm64 macOS: exe, engine DLLs, D3D8 driver and MSVC runtime translated to C; host implements Win32, D3D8, DirectSound, DirectInput, DirectShow; Metal, SDL3 mixer, AVFoundation | "Variable frame rate up to 60 fps (the simulation is variable-dt)"; Hor+ with anchored HUD; `--view stretch`, `--audio off`, `--intro skip`; needs exact 1.05 hashes; `docs/` has census, renderer, resolution and 60 fps milestone notes |
| `phobos665/payne-recomp` | Xbox PAL build, translated to C with DirectXbox-Recomp, runs on Windows and Apple Silicon | all 67 levels load; 60 fps; sound "no dropouts measured (muted test runs)"; fixed a crash in *Rooftops and Alleys 3*; widescreen optional |
| `orktes/max_arm64` | Linux handheld port of the Android build (Max Payne Mobile 1.7) | the mobile build is a different binary; its `conf/config.txt` keys (bloom, trilinear, mipmaps) do not map to the PC build |
| `m0nstr0/max_payne_ldb_importer` | Blender importer for LDB (levels) and KF2 (models) | level/model inspection without the game |
| `ernestask/libras` | RAS archive implementation | same container family as our `x_*.ras` |
| `ermaccer/rockstar.wadtool` | Rockstar WAD archives for the console ports | not the PC build |
| `luigoalma/maxpayne-grphmfc-jpg-patcher` | DLL patch for the JPEG loader path | see §3 |

**Expectation**: two independent recompilations both target 60 fps and describe the sim as variable-dt. Neither claims 144. That is the strongest available signal that 60 is the safe target and that the bugs above appear above it.

## 8. Checklist for our build (how to recognise each one)

1. Dodge ends at 5.25 m, jump clears the 4.5 impulse, bar lasts 15 s: measure at 30/60/120, drift = bug (§1, §2).
2. Slow motion ramps in 0.3 s of real time and the 3D pitch drops to 0.15 while music stays at 1.0 (§2).
3. Voices present, music present, 3D present: three separate inits; missing one is a bank problem, not a mixer problem (§3).
4. Menu and HUD anchored at 16:9; original is 4:3 and 640x480 virtual (§4).
5. Dead bodies and explosion spheres behave the same at 30 and 60; if not, it is untested territory (§5).
6. Sniper cam runs at game speed 0.01, and sound pitch must survive a 0.01 scale (§2).

## Not verified / open

- Ragdoll and dead-body behaviour vs frame rate: no source, no test.
- Whether the original clamps the render dt at all outside characters.
- What "Corrected Max Payne" changes.
- Audio middleware identity (PCGW has no middleware data; the repo has `sndmfc.dll` but I did not read its imports for this note).
- Whether any public project runs the original at 144 fps without the MOGGA patch's fixes.

## Sources

- PCGamingWiki, Max Payne (archived 2026-10-27): https://web.archive.org/web/20261027015816/https://www.pcgamingwiki.com/wiki/Max_Payne
- ThirteenAG WidescreenFixesPack, release `mp1`: https://github.com/ThirteenAG/WidescreenFixesPack/releases/tag/mp1
- MOGGA patch page: https://mogga.space/en/games/max-payne
- aldotoa/max-payne-recomp: https://github.com/aldotoa/max-payne-recomp
- phobos665/payne-recomp: https://github.com/phobos665/payne-recomp
- orktes/max_arm64: https://github.com/orktes/max_arm64
- silentgameplays/Max-Payne-Fix-Windows-11: https://github.com/silentgameplays/Max-Payne-Fix-Windows-11
- luigoalma/maxpayne-grphmfc-jpg-patcher: https://github.com/luigoalma/maxpayne-grphmfc-jpg-patcher
- m0nstr0/max_payne_ldb_importer: https://github.com/m0nstr0/max_payne_ldb_importer
- ernestask/libras: https://github.com/ernestask/libras
- This repo: `rea/map/gameplay.md` (time model, bullet time, shoot-dodge, sound pitch), `rea/map/ai_behaviour.md` (no accuracy scaling), `rea/map/hud_2d.md`, `rea/map/render.md`.
