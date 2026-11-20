# Cold load of the local dist at 80 ms RTT (live-web2, 2026-11-19)

## Where the time goes
The guest reads its game files one synchronous XHR at a time: `host/files.c` `web_read` (host/files.c:141)
opens `/game/<path>` with `Range: bytes=a-b` and blocks while it waits. A visitor's round trip is ~75 ms
(RE-NFSU tools/static_server.py header), so a load is request count x RTT, not bytes.

Capture (tools/loadcap.mjs, out/live-web2/before.json, 80 ms RTT, Chrome for Testing 158 headless):
menu at 33.419 s, 339 Range reads on 172 files, 31.5 s of summed wait, 0 from cache. 47.3 MB read of
1150 MB of files: the four .ras archives (x_english 412 MB, x_music 287, x_data 151, x_level1-3 296)
take 171 of the reads, the other 168 are small textures and config (2.6 MB in total).

## Why a prefetch of whole files works
A Range request alone is never cached (probe on this server: 206 twice, 87 ms both times). A plain 200
of the same URL is cached (`Cache-Control: public, max-age=31536000, immutable`, tools/static_server.py:103),
and a later Range request against that URL is answered from the entry: 1.3 ms and 0.5 ms, same bytes
(probe 2026-11-19, /game/data/database/decals/bitmaps/Foam.jpg). So the page can buy the round trips
early and in parallel.

## What the page does (web/index.html, the entry_hint pattern)
`web/prefetch.json` (166 paths, `whole` only) is the list of files <= 8 MB read between navigation and the
first menu. The page fetches them whole with 6 parallel streams (Chrome's connections per host) as soon as
it starts, and ignores failures; the boot never waits. The archives stay on demand: a whole 200 of
x_data.ras is 151 MB for 30 MB of reads.

## Result
menu 33.419 s -> 19.200 s (0.574x). 168 of the 339 guest reads now come from cache. Warm (kept profile, no
cache clear): 18.925 s, so the remaining floor is ~171 archive reads x 93 ms plus the guest's own boot.
Next in this direction, if it is ever in the allowlist: host/files.c read-ahead sizes for the archives
(one Range per open instead of one per 256 KB), which is a C change, not web/.

# Safari over the network (2026-11-19, out/live-safari1119)
Chrome from maxpayne.cc: menu 12.4 s, Range median ttfb 48 ms, `cf-cache-status: DYNAMIC` (no edge cache).
Safari 26.3, same page served locally at 50 ms per request (tools/static_server.py, RN_RTT_MS=50): menu 23 s, part1_level1
at 60.5 s; on loopback with no delay it plays at 60 fps (vsync 60, drop 0). So Safari pays the round trips Chrome's HTTP
cache hides: a Range request after a whole 200 prefetch is not answered from Safari's cache.
Fix (one change, two parts): web/opfs.js fetches web/prefetch.json whole into blob: URLs the guest reads from memory
(replaces the HTTP-cache prefetch in web/index.html); host/files.c block_get doubles a sequential run per request
(1, 2, 4 .. 16 blocks = 4 MB). Same 50 ms test after: menu 4.8 s, part1_level1 at 16.2 s, 60 fps in the level.
Not measured: Safari against the real host (needs an upload), Chrome after the change.
