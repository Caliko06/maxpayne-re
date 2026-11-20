# WORKLIST (the OWNER writes the items; agents only tick boxes)

## H1: Prologue + Part I playable on :5181 (OWNER 2026-11-02)
- [x] #1 Part I sweep
- [x] #2 part1_level1 finished (DONE 2026-11-02)
- [x] #3 Part I end to end by the automatic player (DONE 2026-11-02)
- [x] #4 Exact-point injection with RN_JUMP (DONE 2026-11-02)
- [x] #5 Smoothness: 60.0 fps in steady play; autosave and comic exit keep 1-2 frames of 36-54 ms (2026-11-02)
- [ ] #6 Cinematic cameras (OWNER 2026-11-02): the kill cam (level death_counter scripts -> "::room::eN->CAM_AnimateParented(Death_01..03)"), the sniper bullet cam (bullet_sniper*.txt [Sniper] -> cam_animateparented(bullet_behind), game speed 0.01) and the player death cam (player_death_01 / special_killed: widescreen bars, reload) look and sound as in the original (camerapaths.txt keys: camera .kf2 path, game speed, FOV, widescreen bars, HUD hidden, audio pitch through SetFrequency). Facts: rea/map/gameplay.md "cinematic cameras". Driven by RN_JUMP on :5186 (part1_level1 + "player->cam_animateparented(Death_01);"; sniper via "player->c_pickupweapon(sniper);|player->c_pickupammo(sniper,10);" and a ghost shot at an enemy). Done when shots every few frames show each camera's orbit / bullet follow, FOV change and bars, the log shows the game speed changes and SetFrequency pitch-down, and control comes back after the [exit] block. SEEN 2026-11-02 (route runs): kill cam runs (HUD off, camera at the dead enemy: part0, part1_level1, 1b shots); r01 helper death plays the game over cam with bars. Open: sniper bullet cam (no sniper rifle in Part I).

## H2: whole game, really played (OWNER 2026-11-03)
Order: systems first, proven in Part I; then Parts II and III. Truth: the binaries + native build; a PC longplay (#13) for look and timing. Sound note (OWNER): weapons and ambience/music feel incomplete.
- [ ] #11 Sound: weapons, Max's voice (narration and in-game lines), enemy voices, steps, ambience, music. Done when every map logs 0 sound events the game asked for and the host did not play. PARTIAL 2026-11-05 (mp-safari f9bad7b): cue names, anti-click ramp, cue-lost log; 3 short 8-bit hit cues never mixed (default_hit_01, metal_hit_01, shell_ricochet_01); 10 cues to hear in rea/map/sound_cues.md; OWNER listens; bullet time pitch 0.57x vs 0.32x open.
- Parked by the OWNER 2026-11-05 (ghost and RE fidelity, no product gain now): #7 Ghost plays for real; #8 Enemy AI as in the binary; #9 Movement of Max and the enemies (run, shootdodge, jump, stairs, collision) from the X_Character states; #10 Weapons; #12 Canon; #13 Longplay reference; #14 Parts II and III by the real ghost, map by map.

## H3: load, cheats, Safari (OWNER 2026-11-05; browsers = Chrome and Safari 26.3; sources: HANDOFF.md, rea/map/cheats_menu.md, ras_overlay.md, boot_phases.md, textures.md)
Spanish waits for the OWNER's files: not an item.
- [x] #15 Cheats in the game's own menu (DONE 2026-11-05 ea21ad4)
- [x] #16 Faster level load, same quality (4096 kept) (DONE 2026-11-05 23014e3)
- [ ] #17 Safari 26.3: AudioWorklet blob: and the doubled synthetic Esc (input.js:72). Done when the OWNER's Safari session plays 5 minutes with sound and one Esc opens the menu once (the OWNER judges; agents do not open Safari).
- [x] #18 No silent mute: native exit(2) on STOP, warning when gpu_wait waits 30 s
- [x] #19 Dev build loop
- [x] #20 One chain of 25 maps in one session (part0_level1 to the credits) on the current build with RN_HEAPPEAK (DONE 2026-11-05 461ef00)
- [x] #21 Load Game after a page reload
- [x] #22 Weapons and bullet time felt right (DONE 2026-11-05 dea7483)
- [x] #23 Enemies of Parts II/III (PARTIAL 2026-11-05)
- [x] #24 Boss and ending (by route, 2026-11-09; see HANDOFF)
- [x] #25 Comics and voice order (DONE 2026-11-05)
- [x] #26 Menus (DONE 2026-11-05 a512315)
- [x] #27 Lifter check (DONE 2026-11-05)
- [x] #28 God mode, all weapons and noclip from the Cheats key without -developer (DONE 2026-11-05 8005395)
- [x] #29 Milestones that land (OWNER 2026-11-07)
