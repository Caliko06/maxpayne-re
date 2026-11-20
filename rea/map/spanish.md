<!-- Becario report 2026-11-04 (DeepSeek), read by the orchestrator, no second key yet: verify an address before patching on it. -->
# C - Spanish (ES) localization inventory, Max Payne 1 PC v1.05
Read-only pass 2026-11-04. Binaries: MaxPayne.exe md5 cda1650af370e50f12e500831a834783 (build/img).
Sources: tools/ras.py on the OWNER's install, Ghidra corpus rea/c/*, rea/map/*.md, pixel reads of the font atlases.
No game run, no browser. FACT = read in code or data; INFERENCE; UNKNOWN. Rows: C_spanish.tsv.
## Totals by kind (4,150 files, 1,105.0 MB)
| kind          | files |     MB | needs=yes |
|---------------|-------|--------|-----------|
| ui_text       |    15 |    0.1 |        10 |
| hardcoded_exe |    11 |    0.0 |        11 |
| subtitle      |     0 |    0.0 |         0 |
| comic_image   |   236 |   34.3 |       236 |
| comic_text    |     0 |    0.0 |         0 (lettering baked in the jpgs) |
| hud_image     |    53 |    0.6 |         0 |
| menu_image    |    85 |    7.8 | 4 (credits, LOADING, EXITING GAME, Saving) |
| voice         |   790 |  350.9 |       677 |
| video         |     1 |   10.1 |  check 1 |
| other         | 2,959 |  701.1 |         0 |
Tips (66 TIP_ + 90 GCID_*_TIP) count inside ui_text. 938 files (361.1 MB) carry English a player meets, 345 more
are "check"; "other" = 274 MB instrumental music, 541 SFX wav, .ldb/.ai, textures. Text: 565 strings, ~4,570 words.
## Where the UI text lives and how it is referenced (FACT)
- Language pack = x_english.ras (1,323 files): stringdefines.h, stringtable.txt, dialogtexts.txt, hud.txt,
  input.txt, graphicnovelpages.txt.
- stringdefines.h (18,381 B): 240 `#define NAME "text"` = 196 GCID_ (81 labels, 90 tooltips, 25 quit jokes),
  28 LNID_ (chapter titles), 8 SGID_ (save/load HUD text), 8 GNID_ (comic tooltips + GNID_PAUSE). It is an
  #include for the data parser, not a C header: menu.txt:4, hud.txt:1, levels.txt:1, globalconfig.txt:1-2.
- stringtable.txt: 199 `String =` -> 66 TIP_, 53 STRING_, 39 OBJ_, 36 HUDPRINT_, 5 misc. Tips embed key tokens
  (`TIP_SNIPER` = "Press and hold the use key (#USE#)...", stringtable.txt:113-117); the engine substitutes the
  bound key name. dialogtexts.txt: 126 labels of the Win32 startup dialog, Advanced settings, Parental Lock (the
  same 121 labels also sit in .rsrc as UTF-16 dialog templates). input.txt: 23 key names for the Controls menu.
- menu.txt (x_data.ras, 23,098 B) is layout but the biggest reference table: 215 distinct GCID_ ids
  (`Name = GCID_NORMAL;` menu.txt:133; audio ids 771-778) and the 5 font files (17-22).
- Chain: data asks by name -> the exe resolves it (file names in .rdata: `data\dialogtexts.txt` 0x866ad0,
  `hud.txt` 0x878f74, `input.txt` 0x87e708, `GraphicNovelPages.txt` 0x880774, `StringTable.txt` 0x8807bc; parser
  class RTTI `X_SharedDBStringTable` 0x8805b8) -> a missing id throws
  `MaxPayne_DialogTextsException_UndefinedStringIdentifier`; only values may change, never an id.
- Levels add their own ids: each .ldb embeds 1-5 `OBJ_/STRING_` keys (x_level1: 4,2,5,2,4,4,3,1,2,2 per file).
- Data bug for the ES pack: `GNID_PAUSE` ends with `;` -> prints "Paused;" (stringdefines.h:250).
## Text encoding and the fonts (FACT unless marked)
- All 7 text files are 7-bit ASCII, CRLF, zero bytes >= 0x80, no BOM. UTF-8 would break: the only
  MultiByteToWideChar in the exe is 0x409ac0 (movie name for DirectShow, CP_ACP); game text is never converted,
  so a byte >= 0x80 reaches the renderer as-is.
- Glyph table = e2mfc `P_Font::validate` 0x10101e60: 0x500 bytes = 256 records of 5 (byte, width), widths zeroed,
  then the atlas is scanned left to right and each blank-separated run becomes the next character from 0x20
  (qsort base = record 32); glyph height = image height - 2. Code -> glyph is positional: the atlas must stay in
  CP1252 order.
- Atlases (measured): Menu_Font_Univers.tga 2,781x27 24bpp (+_alpha, +_bright), Verysmall 2,512x19,
  Hud_Universal_Medium.tga 2,300x23 (+_alpha), console tahoma.pcx 988x14 8bpp (console.txt:13-14).
- Coverage: the menu atlas carries the CP1252 run in order, `° ± ² ³ ´ µ ¶ · ¸ ¹ º » ¼ ½ ¾ ¿ À Á Â` then
  `... Ð Ñ Ò Ó Ô Õ × Ø Ù Ú Û Ü Ý Þ`; the HUD atlas shows the same tail. Accents, n-tilde and inverted marks exist;
  nothing has to be redrawn for 0x20-0xDE.
- Cell counts by blank-column scan of the alpha masks: menu 222, HUD 192. If the first cell is '!' (space has no
  ink) the HUD font stops near 0xDF, so lowercase á é í ó ú ñ ü (0xE1+) may be MISSING there while the menu font
  reaches them. UNKNOWN until drawn: render "áéíóúñü¿¡" in both fonts before trusting accented HUD text. Safe
  everywhere today: uppercase accents, ¿ and ¡.
## Comic pages: the text is painted into the images (FACT)
- 210 pages = 210 jpg 1024x512 (P1L0a_001.jpg ... P3L*, 31.8 MB) + 210 .kf2 (1.7-3 KB) holding plate and camera.
  Narration, balloons and tails are pixels: verified on P1L0a_001.jpg and P1L1a_002.jpg (3 panels).
- No string exists for any page: graphicnovelpages.txt carries only geometry, NewChapter, AdultContent and
  `A_PlaySound(story, P1L0a_001)`. 26 further jpgs 640x480 are chapter/loading cards, title baked:
  "MAX PAYNE / PART I: THE AMERICAN DREAM / CHAPTER ONE: ROSCOE STREET STATION" (P1L1.jpg), "Loading" (P0L0.jpg).
- So a Spanish comic = 236 re-lettered images, not 236 strings, and the narration exists nowhere as text
  (INFERENCE: the largest art task; the 461 page voice-over wavs are its only other copy).
## In-level dialogue: sound only, no subtitles, no option (FACT)
- 790 wav in x_english.ras (350.9 MB): 461 comic page voice-overs (313 MB), 216 enemy barks (11 MB), 113 scripted
  in-level lines (27 MB) plus misc.
- In-level lines come from x_data.ras per level, e.g. `[P3L6a_004_cam] ID = ID_PLAYER_2D_SPEECH;`
  `Filename = ingame\part3\P3L6a_004_cam.wav` (235_aesir.txt:4-14); English exists only as a comment,
  `// Max: Colder than a walk-in fridge... (continued from GN)` (235_aesir.txt:3).
- No subtitle data: 0 hits for "subtitle"/"caption" in the 28,378 ASCII strings of MaxPayne.exe, no id in the 126
  dialogtexts.txt entries, and the Audio page has only `GCID_MUSICVOLUME` and `GCID_EFFECTSVOLUME`
  (menu.txt:771-778): no dialogue volume, no text toggle.
- The cinematic layout computes a "subtitle offset" (0x4af943/0x4af95b/0x4af9f0/0x4afa4a, rea/ws/hud_exe.md:14)
  so an engine slot exists, but nothing ships feeding it. UNKNOWN whether any code path prints in-level speech;
  no option turns it on: today a Spanish player gets English audio and no text at all.
## Hardcoded English in MaxPayne.exe (.rdata addresses, computed from the PE sections)
- 0x867018 "Max Payne already running"; 0x867034 "You are running the minimum installation ... CD ...";
  0x8670ad "Please insert the Max Payne CD into the CD drive to continue...".
- 0x8675c4 "Insufficient memory."; 0x8675dc "There is insufficient video memory ... display settings."; 0x867637
  "Please set a lower resolution from Max Payne startup dialog ..."; 0x8676e0 "You have to select a platform from
  the Max Payne startup dialog."; 0x867874 "An error has occured in graphics engine ...".
- 0x867e10 "Platform not selected. See settings."; 0x866d54 "Media player could not play video file %s"
  (0x409ac0); 0x867184 "Max Payne Save Game File". Dev console 0x8677d0-0x867ab8 is not player-visible. These 11
  need a binary patch, a string swap in the port, or stay English (they only show on start-up failures).
## Two more things
- No language selector: rlmfc `loadArchives` 0xf1c0 does FindFirstFileA("*.RAS") in the exe folder
  (rea/map/files.md:97), so an ES pack is a second archive beside the exe or a repack of x_english.ras. help/ =
  7 htm files in English (Windows shell); movies/intro.mpg 10.1 MB has burned-in English.
## UNKNOWN
- Exact glyph count per atlas (222 menu / 192 HUD by alpha scan, 229/227 by color) and whether
  Hud_Universal_Medium has 0xE1-0xFF: decides if the HUD can show áéíóúñü. Panels with no lettering and intro.mpg text: not measured.
