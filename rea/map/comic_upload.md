# Comic page upload: where it is and what one of its frames really costs (GATE50 "comic: texture upload")

GATE50 (OWNER 2026-11-18) counts every presented comic frame: 24 frames of the 25-map baseline are 33-37 ms with
`texKb > 2000` (out/g60_1118/gate50_causes.txt, e.g. 7.7 s, 229.3 s, 868.3 s).

## Path
- host/d3d8.c `unlock_rect` -> `upload_level` (d3d8.c:961): decodes the locked level into the packet blob (A_BLOB,
  host/gpu.c:44), emits `GPU_TEXDATA`, `flush_big()` only submits when an arena is over its soft limit.
- web/gpu.js `case GPU_TEXDATA` (gpu.js:462): `device.queue.writeTexture` of the whole level, `pk.texKb += 4*w*h>>10`.

## What the level is
`texKb` 2048/2052 in comic rows = 4*w*h of 2 MB -> a 1024x512 (1024x513) bgra8 page level, uploaded once per page, in
pairs one frame apart (build/webcheck-5243.telemetry.ndjson: t 7.96/8.20, 209.89/210.12, 1657.36/1661.77, ...).

## What the frame costs (measured with ?texlog, gpu.js TEXLOG, out/g50-comic/before.ndjson + build/g50-comic/page.log)
Run: `node tools/ghost/ghost.mjs --port 5256 --secs 240 --out build/g50-comic --plan play --from part0_level1
--rescue 120` (RN_PAGE_QUERY=texlog=1), referee `comic: texture upload` = 3 frames.
- The 2 MB `writeTexture` itself is 0.55-0.60 ms (`up` in the frame line of the three frames, i125 t=16.82,
  i1934 t=46.95, i12507 t=223.21), against 33.5-37.3 ms of frame time.
- The same frames hold the game thread 15-20 ms in one go (`holdMain`, host/threads.c:70) and read 2.5-7.6 ms
  (`read`): the comic page decode (e2mfc 0x10120ee0 path, host/fast.c stand-ins) plus the page file read.
- `vsync` (host gpu_wait, host/gpu.c:87) 7-22 ms: the game is 2 presents ahead (`q 2 pend 2` in every frame line,
  GPU_QUEUED = 2, host/gpu.h:14), so any frame over one 16.67 ms period costs a whole second period.

## Why the upload cannot fix those frames
Taking the 0.6 ms upload away leaves 15-20 ms of guest work plus the present: the frame still lands in the second
period. The beats are not lost in the display pacing either: `showQueued` (gpu.js:179) refused a queued frame 0 times in
the run (`skip 0`), and the queue is full only because the game is already blocked in Present.
A change that split the 2 MB into two frames would only move `texKb` under 2000 and relabel the frame; the slow part
(guest page decode + file read, host/fast.c and host/files.c, other slices) stays.

Measured conclusion: the comic page upload is 1.5-2 % of these frames. Removing GATE50's `comic: texture upload` needs
the page's guest-side work (decode + read) out of the presenting frame, which is outside this slice's allowlist
(host/d3d8.c, web/gpu.js).
