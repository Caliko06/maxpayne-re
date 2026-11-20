# Load presentation checkpoints (NOCHE50 L1)

Scope: `load_over250 == 0`, with load time at most 1.15 times baseline.

Sources: `rea/c/MaxPayne.exe/00724b90.c:7` reads current mode at +0x19;
`00415dc0.c:942,1076` assigns game/menu slots +0x80/+0x7c;
`0044fda0.c:705,838` establishes level state +0x1074 and name length +0xd4;
`00451e50.c:6` tests player wrapper +0x1080; `00601250.c:39` tests fade +0x0d.
`rea/map/render.md` (Telemetry) identifies the mode-switch pointer at 0x8a6400.

Runtime oracle: `out/g50-load/before.ndjson`, unpatched contract command on port 5253.
FACT: 16 load/boot/menu intervals exceed 250 ms; load_seconds is 11.0.
Texture-heavy intervals include 708 ms/299 MB and 1133 ms/258 MB. Some slow intervals
have no uploads, so cached archive reads also need a checkpoint.

Decision: every 100 ms, at texture uploads or archive reads, submit the current loading
backbuffer through the existing GPU_PRESENT/hand_off path. Only the device-creating
thread can do this. Mode checks match telemetry's boot/menu/load predicate; comic and
ready gameplay are excluded. No guest calls, settings, game frame counter, or telemetry
classification are changed. Host present totals include the real additional presents.
Archive reads include cache hits; other guest threads cannot submit GPU commands.

BEFORE: ghost load5=12.1, above its quiet-machine recommendation; shared-machine timing
is a confound. The rail's referee remains the scoring source. Safari, other maps, and
long chains are outside this two-run check.

## Result (exactly two contract runs)

FACT: referee load_over250 16 -> 1 (FAIL), load_seconds 11.0 -> 8.9 (time guard PASS).
The remaining load interval ends at 12684.90 ms and lasts about 268 ms: no texels or
file reads, 1.3 ms page execution, 10.3 ms vsync and 0.6 ms GIL wait in its row.
INFERENCE: work between available upload/read checkpoints still exceeds the bound;
the cause inside that guest work was not traced. No product edits followed AFTER.
FACT: load_over33 51 -> 84; this patch targets L1's 250 ms bound only.
AFTER ghost load5=6.5; neither run satisfies ghost's quiet-machine recommendation.
FACT: later gameplay shots before-end.png/after-end.png have complete scene and
character textures. UNKNOWN: initial texture guard, because after.png caught the fade.
Both telemetry files, referee outputs, 1000-pixel shots, and exact repro are under
out/g50-load/ (guard.json). Measurement code commit: 853f693; evidence is amended
onto the same product patch. The zero-stall contract remains unmet.

## L1 retry (one additional AFTER run)

Runtime source: the prior AFTER telemetry around 12416.62 -> 12684.90 ms.
FACT: the 268.28 ms interval has 1122 KB geometry, 10 new shaders and 10
pipelines; zero texture uploads/reads, 1.3 ms page execution, 10.3 ms vsync,
0.6 ms main GIL wait, 56.5 ms maximum main GIL hold and 165 clock calls.
The prior row still draws the eight loading-screen primitives. The next row
starts drawing the scene (96 draws once its async pipelines are ready).
INFERENCE: initial scene geometry/shader setup is the uncovered batch, rather
than archive I/O or texture transfer. These counters do not name a guest callee.
Decision: call the existing time- and mode-gated checkpoint at draw_cmd entry,
before gpu_uni/bind_draw allocate packet storage. This splits initial scene
setup between draws, including fixed-function shader creation, without keeping
a pointer into a submitted packet. Ready gameplay still fails the mode gate.
The original unpatched before.ndjson is retained; retry replaces after.ndjson.
Evidence stays on disk under out/g50-load; it is removed from the git index.

Retry result (FACT): load_over250 16 -> 1 against the unpatched baseline,
and 1 -> 1 against the first attempt: FAIL. load_seconds 11.0 -> 9.6,
within 12.65 s: PASS. AFTER is the single retry run on code commit 36153c3.
The remaining interval is 14055.69 -> 14337.32 ms (281.63 ms): one new
pipeline, zero draws/shaders/geometry/textures/reads/clock calls in its row,
25.9 ms maximum main GIL hold, 0.2 ms page execution. The next interval
is 18.38 ms with 1122 KB geometry, 10 shaders and nine pipelines. The new
checkpoint separates the draw batch but does not bound the work before it.
UNKNOWN: precise guest callee or scheduling/driver contribution to that gap.
FACT: after.png is 1000 px, post-fade player-controlled game (control=1),
with character, walls, floor and objects textured; texture guard PASS.
FACT: ghost load5=10.1 exceeds its quiet-machine threshold; no quiet-machine
performance claim. Safari, other maps and the full campaign were not checked.
No further product changes or measuring runs followed the failed retry.

## B1: checkpoint presents skip instead of waiting on vsync (2026-11-19)

Source: out/n50_chain telemetry, 27 loads = 96.6 s, of it 18.9 s main-thread vsync wait inside hand_off(1)
from d3d_load_checkpoint (gpu_wait blocks while presents - shown >= GPU_QUEUED).
Decision: host/gpu.c gpu_screen_free(); the checkpoint returns when the screen has no room, so it never
waits a beat; last_present_us is kept, the next checkpoint retries. Mode gates unchanged.
FACT (out/ab1, one 240 s ghost run, part1_level1): load vsync 0.22 of 8.62 s (2.5 %, chain 19.6 %),
124 load presents still shown, load_over250 3, play slow frames 5 in 175 s.
UNKNOWN: wall-clock load gain; this load (10.6 s referee) is not comparable to the chain's warm average.

Dropped A (same run): web/gpu.js showQueued's 2 ms gate `t < nextShow - 2` was suspected of skipping
ready frames. A counter for frames the old gate would have held read early=0 for the whole run, so the
gate is not the cause of "vsync wait (missed beat)" frames; the change was reverted.
