# Difficulty and adaptive difficulty (2026-11-06)

All of this is original code and data; the recomp runs it unchanged. Sources: Ghidra corpus rea/c/MaxPayne.exe, x_data.ras 1_difficulty.txt
(1 source). Runtime: NOT observed in any mode.

## Modes
`normal` Fugitive, `nightmare` Hard-Boiled, `hell` Dead on Arrival, `timedmode` New York Minute. Selected by gm_setgamelevel in menu.txt.
Each has properties (SaveGameLimit, IntermediateLevels, DeathFrequency, FrequentDeaths, messages) and [minimum]/[default]/[maximum] tables
(parser 0x6770a0). Fugitive is really adaptive: EnemyHealth 0.25/0.8/1.2, PlayerHealth 2.0/1.0/0.4, AmmoFound 2.0/1.0/1.0, AutoAim radius 8/4/2,
height 3/2/1.5, regen 6/4/2 %/s, BulletTime per kill 1.3/1.0/0.7. Hard-Boiled has EnemyHealth 1.3 and PlayerHealth 0.4 in all three tables.
SaveGameLimit: 7 per level on hell, 0 (unlimited) on the other three.

## Adaptive step (FACT unless marked)
- Level index: difficulty object +0xf5. Up 0x676230 (clamp to count-1), down 0x676260 (clamp to 0), set 0x676280, get 0x676290.
- Update 0x453b40, called from level init 0x44fda0 unless [gm+0x12df] is set. It also stores a per-level row (the table GM_DumpPerformanceMeasurements prints).
- Inputs from the player: deaths C+0x75c (+1 in the death routine 0x6963f0); seconds C+0x764; health-seconds C+0x760. 0x6877b0 adds them from
  dt once per accumulated second, and stops while idle (C+0x778 over a limit). They count seconds, not frames.
- Average health = C+0x760 / C+0x764 / max health (0x691bd0).
- INFERENCE (getter order vs the names at parser 0x525860: MaximumHealth, MinimumHealth, MinimumDeaths, MaximumDeaths, MinimumTime, MaximumTime,
  HardcoreHealth, HardcoreDeaths, HardcoreTime): harder when 2+ of {health over max, deaths under min, time under min}, or a Hardcore test passes;
  easier when 2+ of the opposite. The thresholds are per level, block [Difficulty]; PlayTime and EnemyTimeBonus are block [Timedmode].

## Unlocks
Part3_Level7.ldb holds `GM_SendEndOfGameMessages()`, `GM_EnableGameLevel(normal, timedmode)` and `GM_EnableGameLevel(nightmare, hell)`.
Handler 0x457380 is not in the corpus; cheats_menu.md says a non-empty first argument is only a parse check, which does not fit this use: reread it.

## Our changes that touch this
- tools/ras.py cheats_menu: every menu start unlocks nightmare, hell and timedmode, so the end-of-game unlock has no visible effect.
- tools/ras.py JUMP: a level from the Cheats page always sets gm_setgamelevel("normal") and resets the measurements.
- F3 god keeps average health high, so it pushes the adaptive step toward harder (INFERENCE from the formula).
