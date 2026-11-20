# Loading checkpoint cadence (NOCHE50 L2)

Contract: g50-load2, load_over33 at most half BEFORE, no load_over250,
load_seconds at most 1.15 times BEFORE; only host/d3d8.c changed.

Source: the unmodified main binary's runtime telemetry and worker CPU profile,
out/g50-load2/before.ndjson and before-worker.cpuprofile. Existing mode and
backbuffer evidence is in load_checkpoints.md; no guest semantics changed.
FACT: BEFORE referee load_over33=75, load_over250=0, load_seconds=8.1.
For loading intervals between 95 and 150 ms, median page execution is 3.25 ms,
GPU wait 0 ms, vsync wait 0 ms and GIL wait 0.1 ms. This measures the packet
containing each checkpoint, not an isolated GPU copy timer.

Decision: reduce the existing checkpoint threshold from 100 ms to 16 ms.
The same GPU_PRESENT, mailbox and display pacing remain in use; every extra
frame actually submits the loading backbuffer. Mode predicates, telemetry,
game frame counters and guest calls are unchanged.

CPU profile: tools/perf_join.py aligns worker-7 at +100 ms (correlation 0.80;
end anchor 848 ms). The 148-227 ms no-upload/no-read intervals are under
0x655f90 (room initialization) and 0x6f2370, with hot leaves 0x5ad200,
0x5a8b90 and 0x5acfa0. Ghidra rea/c/MaxPayne.exe/00655f90.c has the room
container loop; 006f2370.c calls 0x6e0750 and 0x6f2c00. No safe checkpoint
inside an allowed host path is demonstrated by those samples. No speculative
sites were added. BEFORE has no interval above 250 ms on this machine.

Measurement command (both runs; BEFORE on 256082f, AFTER on this patch):
RN_SECS=240 RN_CHROME=/Users/angeldamian/.local/bin/chrome-bot node tools/ghost/ghost.mjs --port 5253 --secs 240 --out build/g50-load --plan play --from part1_level1 --rescue 120 --cpuprofile 1
Each run uses an empty save-5253; copy build/webcheck-5253.telemetry.ndjson
to out/g50-load2/{before,after}.ndjson. Score with the main gate50_referee.py.

## Result: FAIL (exactly two completed measurements)

FACT: tested product commit af92d04. Referee load_over33 75 -> 68, above
the allowed 37.5; load_over250 0 -> 4. load_seconds 8.1 -> 12.8 (+58.0%),
above the allowed 9.315 s. Low seconds across all modes 10 -> 11.
Texture-bearing slow load intervals remain 45 -> 45; no-upload/read slow
load intervals increase 9 -> 18. More presents do not split those blocks.
FACT: out/g50-load2/after.png is 1000 px, captured from the same ghost-launched
headless browser at the first controllable scene (frame 4967, control=1).
Character, walls, floor and objects are textured: texture guard PASS.
The original automatic arrival shot caught a black fade; before.png instead
holds the baseline end shot of the same first map, with before-fade.png retained.
FACT: ghost load5 9.0 -> 13.3, both above its quiet-machine threshold of 6.
INFERENCE: contention and cold-code effects may contribute; they do not waive
the failed contract. The two-run rail prevents another patch/measurement here.
UNKNOWN: Safari, other maps, full campaign and quiet-machine performance.
No product changes followed AFTER. Evidence and guard.json stay untracked
under out/g50-load2; this note is the only documentation change.
