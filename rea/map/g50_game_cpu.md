# G1: where the guest CPU goes in the slow game-mode frames (part1_level1, 2026-11-19)

Question (GATE50 G class): which guest functions take the time in the game-mode frames whose frame time is guest CPU
rather than vsync / save / load. Method: one ghost run (`--plan play --from part1_level1 --rescue 120 --cpuprofile 1`,
240 s, port 5257, empty save-5257), the V8 CPU profile of the guest worker joined to the telemetry by time
(`tools/perf_join.py`; the offset comes from a 100 ms cross-correlation of lifted samples against the frames' own
guest time, checked against the profile-end anchor: 4700 vs 5012 ms). Numbers and the full table: out/g50-game/top.txt.

RN_SAMPLE is native only: host/main.c keeps the sampler in the watchdog inside `#ifndef __EMSCRIPTEN__`
(host/main.c:52-117), so the web build never prints a guest stack. The V8 profile is the web-side substitute, and its
samples are weighted by their real `timeDeltas` (~1.02 ms each, not the 500 us asked for).

## Findings (35 frames with guest CPU >= 15 ms vs 6373 normal play frames, ms per frame)

| addr | name | slow | normal | chain |
|---|---|---|---|---|
| 1041b690 | d3d_skinmesh_fill_vertices (thunk 10401460) | 1.39 | 0.50 | P_D3DSkinMesh::process <- P_Scene::render |
| 004f84f0 | FUN_004f84f0, shadow-volume float body | 0.70 | 0.05 | 64e980 <- 64e690 <- 6dacc0 <- X_Character::updateShadows |
| 005adb50 | FUN_005adb50, vec3 subtract in the crouch sweep | 0.46 | 0.12 | 5ba5b0 <- R_Bsp::capsuleQueryCached <- 63e350 |
| 0063c5a0 | FUN_0063c5a0, same shadow cluster | 0.36 | 0.04 | 63c520 <- 6db2e0 <- 6dacc0 <- X_Character::updateShadows |
| 006db2e0 | FUN_006db2e0 | 0.34 | 0.12 | 6dacc0 <- X_Character::updateShadows |

Character shadows (~1.9 ms) plus D3D8 skinning (~1.4 ms) are most of the +8.3 ms of extra guest CPU per slow frame
(12.57 vs 4.27 ms); nothing else reaches 0.35 ms. All 35 heavy frames are bullet-time frames (bt != 0), so this is the
same path the B cause takes (rea/map/bullettime.md), seen from the guest stack. Rank stability: the offset is good to
about +-300 ms, so the top list was recomputed at 4400 and 5000 ms; skinning, the 5adb50 sweep and the shadow cluster
(4f84f0 / 63c5a0 / 6db2e0) stay in the top five at every lag.

## What it decides

A fix has to cut guest work in `X_Character::updateShadows` (0x68ba90) and in the skinning fill (0x1041b690), not in the
frame loop. Sources: rea/map/names/MaxPayne.exe.tsv (0x68ba90 profile section "Shadows"), names/e2_d3d8_driver_mfc.dll.tsv
(0x1041b690 + thunk 0x10401460), rea/c/MaxPayne.exe/0064e980.c, 0064e690.c, 0063c520.c, 005adb50.c (Ghidra), and the
profile itself (runtime oracle).
