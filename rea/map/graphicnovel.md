# Graphic novel (comic panels + voice-over), MaxPayne.exe 1.05

Sources: Ghidra rea/c/MaxPayne.exe, rea/c/rlmfc.dll, the OWNER's x_data.ras / x_english.ras / x_music.ras (tools/ras.py; files
graphicnovel.txt, graphicnovelpages.txt, story.txt, globalconfig.txt, music.txt). FACT = read in code or data. All addresses are VAs.
Names are mine. Input DIKs: 01 Esc, 39 Space, 3b F1. Do not confuse: the "mode" = MaxPayne_GraphicNovelMode, a "page" = MaxPayne_GraphicNovelPage.

## 1. Short answer for the host
1. A page advances by ITS OWN TIMER (while "Play" is on) or by a click on a button / a key. Not by the sound end. The mode never
   asks S_Sound::isSoundPlaying about its voice-over (the only caller is the cleanup 0x58bf30, see 3).
2. Input = DirectInput state only: mouse position + button 0, and keyboard bytes via GetDeviceState (FUN_005184c0 = state[DIK] != 0).
   The WH_KEYBOARD ring is drained every frame but its OnKey 0x49c110 only forwards to the dev-key handler: no effect in retail.
3. Voice-over = the page's OnInitMessage `this->A_PlaySound( story, <page> )`, a normal streamed 2D S_Sound. Needs host sound to work.
4. Panels = 1024x512 baseline jpg (3 comps) inside x_english.ras, drawn as a textured plate in a tiny .kf2 scene. Buttons = loose jpgs.
## 2. Mode objects (FACT)
- Class MaxPayne_GraphicNovelMode, size 0x14e, ctor 0x49a940 (vtable 0x7ce920), body 0x49cab0 (reads GraphicNovel.txt block by block).
  Created by mode manager 0x415dc0 (line 999); strings MP_MS_GRAPHICNOVELMODE 0x868b18, MP_MS_GRAPHICNOVEL_DEINIT 0x868a64.
  Vtable slots used: 3 = 0x49b350 (enter: kills all sounds 0x58bf70, 0x4a8f00), 10 = 0x49b0b0 (fade-in step), 11 = 0x49b210 (fade-out
  step), 14 = 0x49b6d0 (UPDATE every frame), 18 = 0x49c110 (OnKey), 20 = 0x49af10 (select first usable page, resets +0x110), 21 = 0x49b480 (load state), 22 = 0x49b620 (save state) [corrected 2026-11-02: 20 was listed as save/load].
- Fields: +0x8b = array of 7 buttons, +0xf4 current page index, +0xf8 page shown, +0x104 page vector, +0x97 button held by mouse,
  +0x9b button held by keyboard (8 = none), +0x110 fade-done flag, +0x44*4 = "playing" flag, +0x123/0x11f pause-text timer.
- Page = 0x4a8420 (ctor, builds from graphicnovelpages.txt), list in x_english.ras: 210 blocks `[P1L1a_002] { ExportData=part1\P1L1a_002.kf2;
  NewChapter=yes|no; AdultContent=yes|no; [OnInitMessage] String="this->A_PlaySound( story, P1L1a_002 );" ... }`. OnExitMessage exists on a few
  (note pickup, mpgnm_pickupnote / removenote). Pages with AdultContent and the filter flag are skipped (0x49c170 uses 0x4a8fc0).
- Buttons (index in +0x8b): 0 NextNote(page+1) 1 PreviousNote 2 NextChapter 3 PreviousChapter 4 Play 5 Stop 6 ExitToGame (the SKIP art).
  Positions in graphicnovel.txt are 640x480 virtual units (same cursor space as the menu: 1.25 units per mouse count, clamp 640x480).
## 3. How a page advances and how the mode ends (FACT, from 0x49b6d0)
- Mouse: cursor moved by FUN_00519710 delta (x400 x dt, clamp 0..640 / 0..480, lines at 0x49b6d0). Button 0 DOWN over a button
  (hit test 0x49a1e0) = press (0x49c680, +0x97). Button 0 UP while still over the same button = ACTION 0x49c760(index):
  0/1 page +/-1 (0x49c1f0 -> index +0xf4, page shown in its END state 0x4a8f00, no OnInit, so NO voice-over), 2/3 chapter +/-1 (next page with
  NewChapter=yes), 4 Play (0x4a8f40 = reset time to 0 + fire OnInit messages 0x4a87e0), 5 Stop (0x49ca60), 6 Exit (0x49c910).
  Moving off the button cancels. Only mouse button 0; button 1 and the wheel are unused here.
- Keyboard (edge-armed: a key held on entry does nothing until it was released once, flags +0x111..+0x116):
  A / Left (action STRAFELEFT value +1, 0x4cbea0) = previous page, D / Right = next page; W / Up (RUN, 0x4cbc20) = previous chapter,
  S / Down = next chapter; Space (DIK 39) or USE action (E / Enter, 0x4cc600) = Play when stopped, Stop when playing;
  Esc (DIK 01) or F1 (DIK 3b) = Stop when playing, Exit to game when stopped; PAUSE action (P / Pause, 0x4cc930, edge) toggles pause
  (vtable +0x5c / +0x64; while paused the page time does not run; text GNID_PAUSE appears after a delay). Keys act on RELEASE like the mouse.
  Binding is through the action table (rea/map/input.md), so user rebinding changes them.
- Auto advance (PLAY on): each frame 0x4a8b50(dt) adds dt to page time +0x6f. 0x4a8f60 = "page still running" = time < animation length
  (vtbl +0x78 of the page scene +0x2b) + PageFadeOutTime 0.5 (global 0x8a8078). When it turns false: if a next page exists (0x49c220) then
  0x49c1f0 + 0x4a8f40 (next page, its OnInit voice starts, time = 0); else 0x49c910 = leave the mode.
  The page length is the kf2 animation length: NOT VERIFIED whether the authors made it >= the voice length (wavs are 6-60 s, e.g.
  P1L0a_001.wav = 586412 bytes = 6.6 s 8-bit stereo 44100). A voice longer than its page would overlap the next one (no stop on advance).
- Exit 0x49c910: 0x4a8820 fires OnExit messages, mode switch to "game" (string 0x868c08) via 0x60e610. Fades: 0.25 s in/out
  (graphicnovel.txt ModeFadeInTime / ModeFadeOutTime and globalconfig.txt FadeIn/OutTime...GraphicNovelMode 0.25).
- Entering from a level: by script / game mode message (class X_GraphicNovelMode), first page DefaultPageName "p1l0a_002" (Properties) or the
  saved page (0x49b480); entry point of the script call NOT VERIFIED.
## 4. Voice-over and music (FACT unless marked)
- Chain: page start 0x4a8f40 -> 0x4a87e0 sends each OnInitMessage (0x51b6a0) -> mode is an X_MessageReceiver<GN, X_AudioMessage_PlaySound> (RTTI
  strings 0x876d00..0x877000) -> sound factory (rea/map/sound.md 4) -> S_Sound in the mode's sound list (this+0x57, list at +9).
  No sound call exists in 0x49cab0 / 0x4a8420 / 0x49b6d0 themselves.
- Database: x_data.ras story.txt: `[P1L0a_001] ID = ID_PLAYER_2D_SPEECH; Filename = story\part1\P1L0a_001.wav; Volume = ID_GN_SPEECH_VOLUME;
  Pitch 44100; 3Dsound NO; Looping NO; Streamed YES; Priority 100; FrequencyScaling FALSE`. `[Empty]` = placeholder.wav (pages A_PlaySound(story, empty)).
  So STREAM flag set (sound.md 2.6), plain 2D, no Falloff/Hotspot. Files live in x_english.ras (wav 8 bit stereo 44100 PCM for P1L0a_001; others
  not all checked: NOT VERIFIED), the sound code loads RIFF PCM/ADPCM only.
- Timing vs sound: nothing reads getPlayingPosition or the sound length for panels. Cleanup 0x58bf30 (called by base update 0x60de10) deletes
  every S_Sound of the mode whose isSoundPlaying() is false; 0x58bf70 deletes all of them (mode enter 0x49b350, every manual page / Stop click 0x49c760).
  If isSoundPlaying wrongly returned false early, the voice would be cut at once; if it never returns false, sounds pile up (memory).
- Music: music.txt has [graphic_novel] = wavs\graphic_novel.wav (x_music.ras, 18173380 bytes, looping) and [graphicnovel] = wavs\empty.wav;
  who starts it (PlayMusic receiver exists in the mode, same RTTI list): NOT VERIFIED.
## 5. Time dependence (FACT)
- dt chain: main loop 0x40c020 -> R_Timer::sample (rlmfc 0x276f0, QueryPerformanceCounter, fallback timeGetTime when R_System+0x61b = 0, 0x26b90)
  -> frame sample clamped to 0.1 s (const 0x7cb8b4), averaged over a ring (+0xe5..) -> time object 0x582df0 -> mode update gets that SMOOTHED
  dt (0x601250 reads it with 0x5e53d0, passes it to slot 14). So a slow frame advances a page by at most ~0.1 s / ring size; 60 fps just gives dt ~ 0.0167.
- Risk for the port: the page animation runs on dt, the voice on the DirectSound thread (real time, sndmfc update 1/30 s). If the host frame loop stalls
  (tab hidden, dt capped at 0.1 s per frame), the picture falls behind the voice. Only the audio clock is real time: NOT VERIFIED in the web build.
## 6. Assets and loading path (FACT unless marked)
- Pages: scene `part1\P1L1a_002.kf2` (1.7-3 KB, in x_english.ras dirs d4 / d6 / d8) = P_Scene data: texture path "textures;..\sharedtextures",
  mesh "Plate" with map "platemap" = `P1L1a_002.jpg`, a "Background" mesh (black.tga), Camera01 + keyframes. Loaded by 0x4a8420 via the scene
  loader (FUN_0054c1c0 object 0x19b bytes; e2mfc P_Scene). Textures decoded by grphmfc/e2mfc jpeg code (host must give the jpg bytes via the .ras
  reader like any other file).
- Panel jpg: 1024x512, baseline, 3 components, JFIF (checked P1L0a_001, P2L1a_001, P3L1a_001; 80-180 KB; 210 pages in dirs d5 / d7 / d9 =
  86 + 54 + 70 jpgs). Alpha is not used for panels. Color jpg only: djpeg is a valid oracle.
- Buttons: loose files data\graphicnovel\bitmaps (57 jpg, 7 buttons x 4 states + `_a` alpha twin): 512x512 (PLAY/STOP), 512x256 (page, chapter),
  256x256 (SKIP), gn_background.jpg 2048x512 (shown 640x100); font menu_font_univers_Verysmall.tga, cursor ..\menu\bitmaps\cursor.pcx.
  The block also names Hud_Universal_Medium.tga for the pause text. Loaded in 0x49cab0 (R_FileDirectoryChange "GraphicNovel" 0x87681c = data\graphicnovel).
## NOT VERIFIED (summary)
Kf2 animation length vs voice length per page; script call that enters the mode and starts the graphic_novel music; whether the web build keeps the
sound thread in real time while frames stall; sample format of every story wav; exact meaning of flags +0x110..+0x116 beyond "armed after release".

## Runtime proof (web build :5183, headless, New Game > Fugitive, dsound Play/Stop log, 2026-11-02)
Every game sound is an 8-bit PCM stream buffer (44100 or 22050 Hz, mono or stereo), 0.5 s ring refilled by the game.
"ran" = Play to Stop on the wall clock; "streamed" = bytes the game fed (Unlock) since the last Stop. A stream that ended
naturally has streamed - ran = 0.96 s (0.5 s first fill + about 0.46 s kept ahead), so voice length = ran (+/- 0.1 s).
- t=30.8 s music, stereo 44.1 kHz, ran 74.8 s (load + prologue start), Stop at 105.6 s.
- Prologue voice-overs (stereo 44.1 kHz, SetVolume -416 = -4.16 dB): 6.26 s, 30.10 s, 23.83 s, then a mono 8.43 s one.
  The next voice's Play comes in the same millisecond as the previous voice's Stop (191059 ms: Stop 30.10 s / Play), and
  the background loop (stereo, ran 63.33 s) stops together with the last voice (224103/224104 ms), then the level sounds start
  (22 kHz mono, 224.1 s). So the page timer and the voice end together: no drift, no long desync.
- Audio side in the same run: silence 0 frames, resyncs 0, peak 7000-9000/32767.

## Keys on the web build (2026-11-02, port 5183)
Keys are DirectInput state (section 3), the same path as in-game keys, so the left hand covers the comic with the default table.
New Game by keys is NOT skipping the comic. Order on web (RN_SOUNDLOG, keys S S Space Space, Resume Game enabled): opening rooftop cinematic
(menu music stops 27 s, cinematic music Play at 31.8 s, 74.8 s long), then the comic: Stop 105.0 s, voice-over Play 105.3 s (stereo, -416, ran 6.23 s),
next voice Play 124.5 s; screenshot at 135 s shows the first panel ("I was still in the force back then...") with the button bar. Pages run on
their own timers; the Space that started the level (released long before) neither skips nor stops anything (edge-armed keys, section 3).
Test trap: with a save present, Resume Game is enabled and takes the initial focus, so one S lands on Tutorial (the rooftop-less alley level, "Welcome
to a short tutorial"); New Game is S S. Panel layout looks narrow (panel drawn tall at the left, 1/3 of the width): render's item to check.

## Coverage (2026-11-02, names in rea/map/names/): exe graphic novel (GraphicNovelMode, GraphicNovelPage)
Names come from Ghidra-corpus + disassembly reading by a Sonnet worker, spot-checked by the platform slice; one source = weak, two = strong.
'understood' = body read and role stated; 'named' = name justified by class/shape only. Full table: rea/map/names/<image>.tsv (addr, name, level, confidence, summary, evidence).

| set | functions in table | named before | named now | unknown | understood (strong) | named outside the table |
|---|---|---|---|---|---|---|
| graphic_novel (subsystem) | 23 | 0 | 23 | 0 | 17 (12) | 32 |

Source: `names_gn.json` (55 entries: all 23 table functions + 32 outside the table, marked `outside_table`).

#### MaxPayne_GraphicNovelMode (vtable `0x7ce920`, size 0x14e, ctor `0x49a940`, 26 slots)
- Slots: 0 `0x49a920` scalar deleting dtor (body `0x49aa70`), 3 `0x49b350` enter, 8 `0x49b3c0` and 9 `0x49b3d0` float getters (+0xbf, +0xc3),
  10 `0x49b0b0` fadeIn, 11 `0x49b210` fadeOut, 14 `0x49b6d0` update, 15 `0x49c130` (unknown), 18 `0x49c110` onKey,
  20 `0x49af10` select first page, 21 `0x49b480` load, 22 `0x49b620` save, 23 `0x49b3e0` show hint, 24 `0x49b430` isHintShown, 25 `0x49b440` hide hint.
  Other slots are shared base code (`0x60dc..`, `0x60dd..`, `0x723bd0`).
- Loader `0x49cab0` (4343 bytes): reads GraphicNovel.txt, creates pages with `0x4a8190`, OnSelectActive/OnSelectInactive messages. Read through callees and strings only.
- `0x49dbe0` resetState (called by ctor and `0x49ac10`); `0x49dbc0` ctor of MaxPayne_GraphicNovelModeException (vtable `0x7ce9bc`).
- Helpers: `0x49c170` find page by step, `0x49c1f0` go to page, `0x49c220` can go, `0x49c550` show current page, `0x49c680` select-changed messages,
  `0x49c760` button action (0 prev, 1 next, 2 chapter +, 3 chapter -, 4 play, 5 stop, 6 exit), `0x49c910` exit, `0x49ca60` show all unlocked pages, `0x49a1e0` button hit test (button object, not the mode).
- Message handlers (RTTI MaxPayne_GraphicNovelModeMessage_*): PickUpNote `0x49def0` (static init `0x49e3b0`), RemoveNote `0x49e090` (`0x49e410`),
  MarkAllNotesRead `0x49e100` (`0x49e470`), RemoveAllNotes `0x49e150` (`0x49e4d0`). Audio receivers: PlaySound `0x49e2f0`, PlayMusic `0x49e350` (not named, outside table).
- Key fields: page db +0xab, button vector +0x8b, page vector +0x104 (end +0x108), current page +0xf4, shown page +0xf8, default page +0xfc,
  play flag +0x110, key-armed flags +0x111..+0x116, hint flag +0x12b, fade progress +0xbb.

#### MaxPayne_GraphicNovelPage (vtable `0x7ced14`)
- Slot 0 `0x4a8170` scalar deleting dtor. ctor `0x4a8190`, dtor body `0x4a8380`, init `0x4a8420`, cleanup `0x4a8710`, stop `0x4a8ae0`, tick `0x4a8b50`.
- `0x4a87e0` fires OnInit messages (vector +0xc..+0x10), `0x4a8820` OnExit (+0x1c..+0x20), `0x4a8f00` end state, `0x4a8f40` play, `0x4a8f60` isPlaying, `0x4a8fc0` isSelectable.
- Flags: +0x28 unlocked (`0x575750` set, `0x40e800` get), +0x29 read (`0x575760`), +0x2a shown (`0x4a8fb0` set, `0x4a8fa0` get). Meaning inferred from the handlers (weak). Time +0x6f, scene +0x2b.
- `0x4a9100` resets runtime state.

#### Globals
- `0x8a66c0` std::string "graphicnovel" (static init `0x4154d0`); `0x8a8030` and `0x8a8078` constants used by handlers and page time; string `0x8a6334` is the empty string.

#### Unknown or left unnamed
- `0x49c130` slot 15 (calls `0x60de40` with 5 args; role unclear).
- `0x4a8b50`, `0x4a8ae0`, `0x4a8420` read by role only, not line by line.
- `0x49e350` PlayMusic receiver registration, `0x49e3b0`/`0x49e410`/`0x49e470`/`0x49e4d0` static inits not named (outside table).
- `0x435280`, `0x539c90`, `0x575750`, `0x575760`, `0x40e800` are COMDAT-folded shared stubs; names are by shape.
