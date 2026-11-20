# Hosting map: Cloudflare R2 + a domain, same shape as nfsu.cc (OWNER 2026-11-19, reopened: 'Va, cambiemos y modifiquemos todo para que SÍ sea publicable')
Live 2026-11-19: https://maxpayne.cc (zone active, bucket `maxpayne`, 833 objects / 2.40 GB), public. The Cloudflare Access app
'Max Payne (private)' (one-time PIN, only the OWNER's email) was deleted 2026-11-19 at the OWNER's ask ("elimina cualquier cosa que
autolimite el acceder al juego"), so the link opens with no email and no account; backup of the app and its policy in
`out/access-backup/`. Anonymous pass 2026-11-19: `/` 200 with the three headers, wasm 200 `br`, Range 206, webgate to the main
menu with no credentials. `RN_HEADERS` in tools/webgate.mjs stays for any future private host.
Credentials: ~/.config/cloudflare/credentials.env. Telemetry is off off-loopback (web/telemetry.js; ?telemetry=1 forces it).
State lives in `web/plan/plan.json` step 4 (Público); next task = its first row not VERIFIED. Nothing below runs without the
OWNER's go for that upload. Reference: RE-NFSU `docs/re/hosting.md`, live since 2026-11-15.
| Step | Who | What |
|---|---|---|
| 1 Account + domain | OWNER | Cloudflare account; buy the domain (Cloudflare Registrar sells at cost, DNS already on the zone) |
| 2 Bucket | OWNER or us with his token | R2 bucket, e.g. `maxpayne`; Settings > Custom Domains > `play.<domain>` first, apex when it works |
| 3 Token | OWNER | R2 > Manage API tokens > Object Read & Write on that bucket; `rclone config` remote `r2` (provider Cloudflare) |
| 4 Build | us, at upload time | `python3 tools/build_public.py --cdn` (the raw `game/` tree: an object store cannot reassemble `data-chunks/`) |
| 5 Blueprint | us | `python3 tools/upload_r2.py maxpayne --dry-run` (below: object counts, bytes and headers; fails if a file has no pass) |
| 6 Upload | us, after his go | `tools/upload_r2.py maxpayne` (~2.5 GB, 833 objects, four rclone passes in this order) |
| 7 Rules on the zone | us or OWNER | below |
| 8 Check | us | list below |
## Dist blueprint (measured 2026-11-19 on `build/public-cdn`: 872 files = 833 uploaded + 39 local, 2,526 MB; rebuilt after
`web/notice.html`, engine recompressed from the 13:11 wasm)
| Pass | What | Objects | Bytes | Header | Sent when |
|---|---|---|---|---|---|
| 1 `game` | the install tree at `/game/<path>`, read with Range by host/files.c | 692 | 2,293 MB | `Cache-Control: public, max-age=86400` | changed (`--checksum`); first, so an upload cut short still boots |
| 2 `assets` | `img/`, `gamefile/` (launcher art), `fonts/`, the rest | 114 | 89 MB | same | changed (`--checksum`) |
| 3 `page` | `*.html`, `*.js`, `*.json`, `*.webmanifest` (notice.html included) | 26 | 0.8 MB | `Cache-Control: no-cache` | every time (`--ignore-times`): the header must reach unchanged bytes |
| 4 `engine` | `out/maxpayne.wasm` ← the `.br` bytes | 1 | 125 MB raw / 14.8 MB sent | `no-cache`, `Content-Encoding: br`, `Content-Type: application/wasm` | every time |
Local, never uploaded: `_headers` (R2 ignores Pages syntax; the headers are zone rules), the 37 `*.gz` twins (`tools/static_server.py` only), `out/maxpayne.wasm.br` (its bytes are pass 4).
`/` is not an object: the URL rewrite rule serves `index.html`. The package has no `data-chunks/`; `--cdn` replaced them with the tree.
Freshness check before an upload: `web/out/maxpayne.wasm` must be newer than every `host/*.c`, `*.h` and `tools/recomp/*` (2026-11-19 13:11 against 0 newer; only `web/gpu.js` and `web/telemetry.js` were, both page JS the wasm does not contain).
Upload 2026-11-19 21:3x (OWNER go: page + engine only): `tools/upload_r2.py maxpayne --dist build/public --only page,engine`,
the Safari load fix (rea/map/load.md, commit 1ae5f20). Checks after: `/` 200 with the three headers, wasm 200 `br` 14.8 MB,
Range 206 `cf-cache-status: HIT` (cache rule `http.host eq "maxpayne.cc"`, respect origin, live since 20:19), cold Chrome menu 12.95 s.
Upload 2026-11-19 (OWNER go: page only, the phone layout, commit 3efd715): `--only page,assets` (launcher.css is in the assets
pass; --checksum sent only it), then a purge of https://maxpayne.cc/launcher.css: assets carry max-age=86400 and the edge kept the old copy.
Found then: browsers get `max-age=14400` on the no-cache page files (Cloudflare Browser Cache TTL 4 h overrides the origin), so a
returning visitor can run old scripts for up to 4 h after an upload; a fresh browser got the new page (MP_DEVICE phone).
## Zone rules (dashboard > Rules)
| Rule | Match | Action | Why |
|---|---|---|---|
| Response header transform | hostname eq play.<domain> | set `Cross-Origin-Opener-Policy: same-origin`, `Cross-Origin-Embedder-Policy: require-corp`, `Cross-Origin-Resource-Policy: same-origin` | SharedArrayBuffer (web/index.html:126 refuses to boot without `crossOriginIsolated`); same three as `dist/_headers`, `tools/serve.py` and `tools/static_server.py` |
| URL rewrite | path eq `/` | rewrite to `/index.html` | R2 serves no index page |
| Cache rule | hostname eq play.<domain> | eligible for cache, edge TTL 1 day | fewer R2 reads: one boot is 339 Range requests (web/index.html:148) plus 166 prefetches |
## No gate (OWNER 2026-11-19: "acceder sin problemas desde cualquier sitio sin bloqueos")
Nothing asks for an account, an email or a challenge. Cloudflare Access app deleted (backup `out/access-backup/`); zone
`security_level` `essentially_off`, `browser_check` off, `challenge_ttl` 300, Bot Fight Mode off; no WAF managed ruleset, no page
rules, no rate-limit rules, no response firewall deployed on this zone. Tested 2026-11-19 with UAs `curl`, `python-requests`,
Googlebot and iPhone: `/` 200 and `Range` 206 in all four. The only 401 left is `POST /log` from `web/index.html:62` (R2 rejects the
write); it is console noise, not a block, and needs a `page`-pass upload to silence.
## Check (once, from the domain)
1. `crossOriginIsolated` true in the console.
2. `/out/maxpayne.wasm` answers 200, `Content-Encoding: br`, `Content-Type: application/wasm`.
3. `Range: bytes=0-1023` on `/game/x_data.ras` answers 206.
4. `node tools/webgate.mjs <url> 240 build/webgate.png` reaches the main menu; then play one level.
## Cost (third-party guides, 2026; confirm on Cloudflare's page)
Storage $0.015/GB-month, 10 GB free: this package and nfsu.cc (1.77 GiB) together fit. Egress free. Class B (reads) 10 M/month
free, then $0.36/M: a boot is ~500 read requests, so ~20,000 boots/month before the first cent. Unknown until live: Range through
the cache, Safari/iOS.
## Legal posture (same as nfsu.cc)
Fan project on the OWNER's own copy, no game files in the repo. The public face is `web/notice.html`: what this is, not
affiliated with or endorsed by Remedy Entertainment, Rockstar Games or Take-Two Interactive, saves stay in the visitor's
browser, and a takedown takes one action (the game data is one package; the notice stays up). It is linked from the
launcher's foot and from the boot screen, and it ships with the `page` pass. Missing before the first upload: the
takedown address, which is the OWNER's to give (marked `data-todo` in the page).
