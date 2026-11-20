# HANDOFF maxpayne (2026-11-20, scope: faster downloads, then SEO; main). Replaced at close.
DONE: phone layout, HUD, light version live (af6b28e, 1f14525); public GitHub copy github.com/Caliko06/maxpayne-re (~/maxpayne-re-public). NOT VERIFIED: real phones.
DOWNLOADS (40e671a, live 2026-11-20 with OWNER go: assets+page+engine, edge purged; live desktop level playing at ~18 s): 4 helper threads fetch the chosen level's pieces (web/level_hint.tsv) while the
  engine boots, retries on failed reads; part1_level1 at 80 ms + 50 Mbit: desktop 94 -> 37 s, phone 75 -> 50 s (rea/map/web-devices.md,
  out/load1120). Only part1_level1 has a hint (tools/level_hint.py makes the others); Safari not measured. Upload = engine + page + level_hint.tsv.
Open finding: Cloudflare Browser Cache TTL gave max-age=14400 on no-cache page files (docs/hosting.md); zone setting, OWNER's call.
DROPPED by the OWNER. Nobody builds, polishes or reopens these without the OWNER's go; every worker brief carries this block.
- Launcher 3D scene and generated art: removed in d9dd67b (2026-11-10). Logo in art2/private stays.
- #7-#10, #12-#14 ghost and RE fidelity: parked 2026-11-05. Coverage and naming passes: 2026-11-02. Claude subagents as slice leaders (2026-11-09; cdx and bec lead). Free FOV slider (look_move.md:50).
- Phone HUD as loose discs on an arc (ee94c32, b2f0c5d): OWNER 2026-11-20, too spread out, not a current game.
VERSION FIX (8432e06, live): returning browsers mixed an old maxpayne.js with the new wasm ('import function a:I must be callable');
  the page now asks for both with ?v=<wasm hash> (tools/build_public.py). A browser holding the old index.html heals within 4 h.
OWNER open: takedown address; his phone test of the light version; go for the downloads upload.
Next block (OWNER 2026-11-20): SEO like RE-NFSU (8b41246, 7d7ba10, 4cc2ce0, 26ccabc, 90fd853): search base, player guides, devlog with
  real code and REA output, GitHub SEO; EN + ES; a "How it's made" section inside the launcher with gallery and code.
