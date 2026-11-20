# Longplay reference (#13): one PC longplay of Max Payne (2001)

Goal: look and timing reference for the reimplementation. Original 2001 PC build, no mods, no commentary, all three parts, with per-map timestamps if possible.

Method (2026-11-03): metadata only, `yt-dlp --skip-download`. No video was downloaded. Durations, resolutions, upload dates and chapter lists come from YouTube's own metadata; the chapter titles and their times are author-provided.

## Candidates

| # | URL | Channel | Length | Uploaded | Max res | Chapters | Mods / editing | Verdict |
|---|---|---|---|---|---|---|---|---|
| 1 | https://www.youtube.com/watch?v=WfIZOWSurJg | Global Gaming | 4:01:25 (14485 s) | 2026-01-31 | 3840x2160 @60 | **25**, in-game chapter names, plus the same list in the description | no mod or edit claimed | **recommended** |
| 2 | https://www.youtube.com/watch?v=C_CtiPxLgNo | SourceSpy91 | 4:13:26 (15206 s) | 2018-09-26 | 2560x1440 @60 | 0 (levels are separate videos in his "Full game Playlist") | no mod claimed, "No Commentary" in title | backup |
| 3 | https://www.youtube.com/watch?v=RDhDpZ5cRE4 | Lacry | 3:58:55 (14335 s) | 2024-02-21 | 3840x2160 @60 | 26, chapter names + times | **"AI Upscaled Textures"** (texture mod) and **edited: "without deaths and long loading times"** | reject as reference; keep its chapter list for naming |
| 4 | https://www.youtube.com/watch?v=t7Lrm746v_s | Sweepers Tony & Nox | 5:10:00 (18600 s) | 2022-04-27 | 3840x2160 @60 | 14, narrative names ("Lupino's hotel", "Mob war") | no mod claimed; the extra hour suggests deaths/retries kept in | second backup (look only, not timing) |
| 5 | https://www.youtube.com/watch?v=GnsLlVbj5bQ | NoCommentaryGameplay | 4:08:18 (14898 s) | 2026-10-30 | 4K | 0 | unknown | not evaluated beyond metadata |
| 6 | https://www.youtube.com/watch?v=BremxB5yQqA | LongplayArchive | 3:14:33 (11673 s) | n/a | n/a | n/a | **"XBSX Enhanced" = remaster, not the original** | reject |
| 7 | https://www.youtube.com/watch?v=pdOwTsXRN3A | Riptide Longplays | 3:32:14 (12734 s) | n/a | n/a | n/a | **PS2 version** | reject |
| 8 | https://www.youtube.com/watch?v=LnlqpATCYR4 | GTA Series Videos | 3:24:28 (12268 s) | n/a | 4K | 0 | "Dead on Arrival" difficulty | reject (hardest difficulty, wrong pacing) |

Lengths: `fact`, read from YouTube metadata. "No mods" for #1, #2, #4 is `not verified`; I only know that their descriptions do not mention any, and that #3 does mention one.

## Recommendation

**#1, Global Gaming (WfIZOWSurJg).** Reasons:

- 25 chapters whose names are the game's own (Prologue, Chapter 1 Roscoe Street Station ... Part III Chapter 8 Pain and Suffering). The repo has exactly 25 maps (`tools/ghost/exits.json`), one label per map.
- The description repeats the same timestamps, so the markers are deliberate, not auto-generated.
- Nothing in the description mentions a texture pack, a widescreen patch or cuts, unlike #3.
- 4K60 upload, PC, all three parts in one video.

Risks to check before trusting it for geometry: `not verified` that the build is unmodded (a widescreen patch changes framing), `not verified` that the voice/audio mix is untouched (relevant to #11), `not verified` which EXE version (1.0 vs 1.05) it was recorded on. The thumbnail is cover art, not gameplay, and no frame was pulled because the task forbids downloading video.

## Map -> chapter and start time (recommended video #1)

One row per map in `tools/ghost/exits.json`, in game order.

- **Start** = the video chapter marker's own time. `fact` (YouTube chapter data, and the same list repeated in the video description).
- **Which map plays under that label** = `inference` (see the next section). Every row carries that inference, so no row is a confirmed map boundary, only a confirmed chapter boundary.
- **Len** = distance to the next marker, so the map's real end may be earlier (a chapter can end on a comic or a cutscene).

| # | Map | Part | Video chapter label | Start | s | Len |
|---|---|---|---|---|---|---|
| 1 | part0_level1 | I | Prologue | 0:00:00 | 0 | 5:49 |
| 2 | part1_level1 | I | Chapter 1 Roscoe Street Station | 0:05:49 | 349 | 7:28 |
| 3 | part1_level1b | I | Chapter 2 Live from the Crime Scene | 0:13:17 | 797 | 8:08 |
| 4 | part1_level2 | I | Chapter 3 Playing It Bogart | 0:21:25 | 1285 | 12:57 |
| 5 | part1_level2b | I | Chapter 4 The Blood Veins of New York | 0:34:22 | 2062 | 9:54 |
| 6 | part1_level3 | I | Chapter 5 Let the Gun Do the Talking | 0:44:16 | 2656 | 9:25 |
| 7 | part1_level3b | I | Chapter 6 Fear That Gives Men Wings | 0:53:41 | 3221 | 8:34 |
| 8 | part1_level4 | I | Chapter 7 Police Brutality | 1:02:15 | 3735 | 10:47 |
| 9 | part1_level5 | I | Chapter 8 Ragna Rock | 1:13:02 | 4382 | 11:08 |
| 10 | part1_level6 | I | Chapter 9 An Empire of Evil | 1:24:10 | 5050 | 6:58 |
| 11 | part2_level0 | II | Prologue | 1:31:08 | 5468 | 7:23 |
| 12 | part2_level1 | II | Chapter 1 The Baseball Bat | 1:38:31 | 5911 | 11:14 |
| 13 | part2_level2 | II | Chapter 2 An Offer You Can't Refuse | 1:49:45 | 6585 | 9:39 |
| 14 | part2_level2b | II | Chapter 3 With Rats and Oily Water | 1:59:24 | 7164 | 11:42 |
| 15 | part2_level3 | II | Chapter 4 Put Out My Flames With Gasoline | 2:11:06 | 7866 | 3:40 |
| 16 | part2_level4 | II | Chapter 5 Angel of Death | 2:14:46 | 8086 | 11:18 |
| 17 | part3_level1 | III | Prologue | 2:26:04 | 8764 | 6:11 |
| 18 | part3_level2 | III | Chapter 1 Take Me to Cold Steel | 2:32:15 | 9135 | 9:14 |
| 19 | part3_level2b | III | Chapter 2 Hidden Truths | 2:41:29 | 9689 | 9:14 |
| 20 | part3_level3 | III | Chapter 3 The Deep Six | 2:50:43 | 10243 | 9:02 |
| 21 | part3_level4 | III | Chapter 4 Backstabbing Bastard | 2:59:45 | 10785 | 13:25 |
| 22 | part3_level5 | III | Chapter 5 In the Land of the Blind | 3:13:10 | 11590 | 12:42 |
| 23 | part3_level5b | III | Chapter 6 Byzantine Power Game | 3:25:52 | 12352 | 7:03 |
| 24 | part3_level6 | III | Chapter 7 Nothing to Lose | 3:32:55 | 12775 | 13:38 |
| 25 | part3_level7 | III | Chapter 8 Pain and Suffering | 3:46:33 | 13593 | 14:52 |

Last row's length is to the end of the video (4:01:25), which includes the ending comic and credits, so `part3_level7` is shorter than 14:52.

## The chapter -> map mapping

`inference`, with one anchor `fact`:

- Fact: the subway level's own speech file (`build/data/x_data/235_subway.txt`) uses ids prefixed `P1L1a_`, and its lines are Roscoe Street lines (`[P1L1a_Max_03] // Max: Death was in the air at roscoe street...`). So `P1L1a` = Chapter 1 Roscoe Street Station, and the `P<part>L<level><a|b>` ids name the level files.
- Fact: Part I speech ids exist for `P1L1a, P1L1b, P1L2a, P1L2b, P1L3a, P1L3b, P1L4a, P1L5a, P1L5b` plus `P1L0a` (prologue). That is 9 playable levels + prologue, which equals the video's 9 chapters + Prologue, and equals the 10 Part I maps in `tools/ghost/exits.json`.
- Inference: both lists are in order, so position i in the video labels = position i in the map chain of `rea/map/ai.md` ("part0_level1 -> part1_level1 -> part1_level1b -> ..."). Hence `part1_level1 = P1L1a`, `part1_level1b = P1L1b`, `part1_level2 = P1L2a`, and so on; `exits.json` `part1_level6` corresponds to `P1L5b`.
- Not verified: that a chapter is a single continuous map load. If a chapter covers two loads, the second map's start is somewhere inside the chapter, not at the label time.

## Not verified / open

- Widescreen or 4:3: no frame inspected, so framing cannot be used yet to check the renderer's aspect handling (`rea/map/widescreen.md`).
- Whether #1 is unedited. Its 4:01:25 against #4's 5:10:00 for the same game suggests #1 cut deaths or loading; that is `inference` from duration alone.
- Chapter starts include the graphic novel page that opens each chapter, so the first playable frame is after the label.
- Audio integrity (needed for #11) cannot be judged from metadata.

## If #1 turns out to be modded or cut

Fallback order: #2 SourceSpy91 (2018, 1440p, unedited-looking, no per-level chapters in the video but a per-level playlist), then #4 Sweepers (2022, uncut-looking, 4K). #3 Lacry is the only one with a clean per-chapter list and a 4K image, but it is a texture mod plus an edited run, so it can be used to name chapters, not to measure timing.
