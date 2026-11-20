# Play hitches by cause, from CPU profiles (2026-11-19)

Method: ghost `--cpuprofile 1` now also profiles the page thread; `tools/slowframes.py` joins the guest worker's and the
page's V8 profiles to the telemetry frame by frame. Offset from worker Present starts vs telemetry presents: 103658 of
103899 land within 2 ms (out/a_prof, 1800 s, part0 -> part1_level2). Evidence: out/a_prof/slowframes.txt.

Normal play frame: guest busy ~5 ms (p99 ~8 ms) of 16.7. Slow frames are events, not a steady overload.

| Bucket (chain n50, 139) | Cause | Tag |
|---|---|---|
| Death -> load last save (83: "bt toggle" 24, "game code" 23, "mixed" 17, "vsync" 19) | the referee's classes were wrong: these frames are within 8 s after a death. In out/a_prof the 196 "game code" frames are 1 per death of the ghost's death loop (187 deaths), worker busy ~35 ms: X_Character::update 0x687f20 19 ms, resolveMove 0x646f30 / sweepAndRefresh 0x647da0 (collision), loadGame 0x454230 -> X_Character::load 0x691d00 | FACT (profile); why update costs 19 ms in that frame UNKNOWN |
| Save (22) | autosave frame 35-45 ms: MaxPayne_SaveGameInterface::saveGame 0x410300 ~9 ms of many R_MemoryFile::write 0x10017650 calls (rlmfc, doubling buffer, no single hot spot) + executePendingRequest 0x451960; page posts the file (fetch 3-6 ms, page thread only). Thumbnail no longer shows (S fix) | FACT |
| Vsync, not near a death (20) | in out/a_prof 2 of 6 are the test harness: Playwright serializing an evaluate result on the page thread 40-60 ms (serializeAsCallArgument); 2 have both threads idle (compositor/GPU, not visible in JS); 1 is the end of the run | FACT for the split; the idle ones UNKNOWN without a real window |
| Comic (8) | page turn: G_Image::loadJPG 0x10204200 (jdhuff_decode_mcu 0x1020b370 4-8 ms) + web_read 6-10 ms, both on the game thread inside GraphicNovelMode::update 0x49b6d0 | FACT |
| Other (load tail 5, mixed 5, game code 2, bt 2) | scattered: AI ArtificialPlayerInput::update 0x5001f0, skinning 0x10401460, one 15 ms bigintToI53Checked JS glue call, page clear 10 ms | FACT, no single cause |

## Correction: most "death" hitches are the first scene frame after a loading screen (2026-11-19)
- FACT (out/a_prof): every one of the 196 "game code" frames has 7 frames of 8 draws (the loading screen) before it and
  ~103 draws in it: the first scene frame after load-last-save, ~35 ms of guest work (X_Character::update 0x687f20 runs
  its sub-step loop, bound 0.11 s at 0x7d5920, for every character; dynamic objects 0x495930 -> 0x6e1a00 FSM/DO; AI
  0x5001f0 50x a normal frame). Nothing moves on screen before it, so it shows as the loading screen held ~35 ms longer.
- Chain n50 (139 slow play frames) by draws (prev 3 frames < 20 draws, this one > 40): 47 are this first scene frame
  (bt toggle 22, mixed 16, game code 7, load tail 2); 92 happen in motion: vsync 39, save 22, game code 18, mixed 6,
  bt 4 (39 draws after a loading screen, same family), load tail 3.
- So a patch to the death/reload frame removes no visible stutter. Visible ones: vsync 39 and saves 22 lead.

## Page thread, run 2 (out/a_gc vs out/a_prof, 1800 s each, 2026-11-19)
- web/telemetry.js keeps the session rows in Float64Array blocks: page heap at end 96 -> 75 MB, page stalls > 20 ms
  23 -> 15. Slow play frames by class unchanged (vsync 6 -> 6): FACT, no visible gain, kept because it is correct.
- FACT: the vsync-class hitches at 269/578/713 s are the ghost's GHOST.nav() dump on each new level (run.log "nav:" at
  271/580/715 s): Playwright serializes ~4000-6800 nodes on the page thread, 50-80 ms. Test harness only.
- Other page stalls: 965 s is a save (out of scope); ones with < 1 sample/ms are the profiler starved by machine load.
- Most page stalls > 20 ms do not show: frames around them stay <= 28 ms (the guest keeps running in the worker).
