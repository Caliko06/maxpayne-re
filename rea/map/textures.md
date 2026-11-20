<!-- Becario report 2026-11-04 (DeepSeek), read by the orchestrator, no second key yet: verify an address before patching on it. -->
# E_textures - level texture census by resolution (s1105)

FACT = read from the bytes; INFERENCE = my reading; UNKNOWN = not measured.
Method: no archive parser; the JPEG marker stream is walked in place (FFD8 SOI, segment
lengths, SOS then entropy data skipping FFD0-FFD7/FF00, stop at FFD9). A candidate that
never reaches EOI with a SOF is dropped. Script: build/ldbjpg.py (census|list).
Cross-check: 29 random images re-parsed with djpeg -verbose, 29/29 same w, h, components.

## Scope
- 26 .ldb under data/database/levels (front_end, part1, part2, part3), 1090.5 MB total.
- Loose .jpg under data/ (597 files), tagged by directory. No .ldb outside levels/.
- FACT: JPEGs are 352.2 MB of the 1090.5 MB of .ldb (32.3%); the rest is geometry/lights/objects.

## Levels (26 files)
- FACT 3703 JPEGs, 352,217,480 B. All 3-component except one 16x16 grey.
- Max side: 214 <=256, 676 <=512, 1622 <=1024, 1068 <=2048, 123 >2048 (largest 4096).
- FACT RGBA8 no mips: 16,360 MB. Capped 2048: 15,398 MB (-5.9%). 1024 + mips x1.33: 12,610 MB (-22.9%).
- FACT over 2048: 24,838,435 B = 7.05% of level JPG bytes, 1,931 MB RGBA = 11.8% of level RGBA.
  Shapes: 4096x1024 x38, 4096x512 x28, 1024x4096 x24, 2048x4096 x11, 4096x2048 x9,
  256x4096 x4, 4096x256 x4, 512x4096 x4, 2400x1200 x1. 22 of 26 levels carry at least one.
  INFERENCE: the >2048 set is where a cap pays: 123 images, 1.9 GB RGBA, in 9 shapes.
- Heaviest RGBA full: Part1_Level1 1322 MB (201 jpgs), Part0_Level1 1248 (132), Part1_Level3 990 (232),
  Part1_Level3b 837, Part2_Level1 788; lightest Part3_Level5b 121. Per level: the TSV.
- >2048 concentration: Part0_Level1 27.5% of its JPG bytes, Tutorial 20.7%, Part3_Level6 14.2%.

## Loose jpgs under data/ (597 files, 27,463,433 B, 1135 MB RGBA)
- skins/sharedtextures: 392 files, 22.4 MB, 976 MB RGBA, the biggest outside the levels.
- Size mix: 120 <=256, 254 <=512, 221 <=1024, 2 <=2048, 0 over 2048. Max 2048x1024.
- The other 36 dirs together: 205 files, 5.0 MB, 160 MB. Each file holds one JPEG (597 = 597).

## Duplicates (md5 of SOI..EOI, all 4300 images)
- FACT 2683 distinct payloads; 795 of them sit in more than one container (2 to 26 copies).
- Redundant 136,725,993 B of 379,680,913 (36.0%): 135.0 MB across containers, 1.8 MB inside one.
- Worst groups: a 2048x2048 x12 (1.77 MB extra), a 1024x1024 x10 (1.52), a 2048x2048 x3 (1.29).
- Deduped levels: 2111 images, RGBA 10,077 MB full / 9311 at 2048 / 7369 at 1024+mips.
  INFERENCE: a shared-texture cache halves the upload; the 4096 class stays the peak.

## Codec facts
- Baseline 4298, progressive 2 (end_combat 128x256 29,166 B; 256x512 183,538 B; Photoshop 5.2).
- Chroma: 4260 at 4:4:4 (all three components 1h2v, no chroma subsampling; djpeg agrees), 39 at 4:2:0, 1 grey.
- COM: "Lavc58.18.100" on 4093 of 4300 (95.2%), empty 189, "Adobe Photoshop 4.0/5.0/5.2" 18.
  164 images carry an APP13 Photoshop thumbnail inside their own bytes (already counted).
- INFERENCE: Lavc58 = ffmpeg 4.x (2018+), so this install's jpgs were re-encoded long after 2001,
  most likely by the HD pack flow.md already blames for the 1.25 GB heap.
  UNKNOWN: retail 2001 dimensions and sizes; no retail copy here to diff against.

## Not measured
- x_level1..3.ras, x_english.ras, x_data.ras (next to data/, not under it) and the 16 .pcx + 9 .tga under
  data/. UNKNOWN whether they add more large textures.
- What a level uploads live: rea/map/render.md:28 counts 343 textures, fmt 21/22, max 4096x1024 in one run.
