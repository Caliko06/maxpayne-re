# Launcher input profile (MaxPayne.exe v1.05)

Problem: expose settings the original actually consumes, without a second input transform.
Reuses `web/input.js` DIK codes, the existing launcher rows, and `host/win32.c` registry.tsv.
Prior art: the existing `out/research_launcher_1109.md` compares FusionFix/RetroArch grouping;
[FusionFix settings](https://github.com/ThirteenAG/WidescreenFixesPack/blob/master/source/MaxPayne.FusionFix/settings.json)
separates controls from video. Its added camera options are not evidence for this original executable.

## FACT: original profile
- `0x40a4c0` SetRegistryKey("Remedy Entertainment"); `0x4cb3d0` loads and `0x4cb7d0` saves
  `HKCU\Software\Remedy Entertainment\Max Payne\Input Settings` (section string `0x7cf830`).
- Version `Input Settings Version` must be >=9 (`0x87bf64`); older/missing versions call
  `0x4cb090`, ignoring the whole saved profile. The launcher seeds the complete original
  defaults before writing the first changed profile. It preserves other registry sections.
- `0x8a9168`: 32 actions, stride `0x29`, four 9-byte bindings, slider at `+0x24`;
  loop ends `0x8a9688`. Name format `%2d %s` at `0x87bf68` is space-padded, not zero-padded.
  Per action: `Slider type`, `Button0 type/value` through `Button3 type/value`.
- `0x519a00`: type 1 keyboard DIK, 2 mouse button, 3 joystick. `0x4cdc70`/`0x4ce0a0`:
  slots 0/2 positive and 1/3 negative. All four slots remain independently editable.
  `0x4cb090` defaults verified against Capstone, including indices 27 pause and 28 painkiller.
  Sliders for actions 11/12/26 stay mouse Y/mouse X/wheel (2/1/3).

| Registry name / string | Loaded state | Consumer |
|---|---|---|
| Mouse Sensitivity X / `0x7cf908` | `0x8a9690` | `0x519850` |
| Mouse Sensitivity Y / `0x7cf91c` | `0x8a9694` | `0x742870` |
| Invert Mouse / `0x7cf8e4` | `0x8a968c` | `0x519960`, mouse object `+0x51` |
| Invert Mouse Wheel / `0x7cf8f4` | `0x8a968d` | `0x576430`, mouse object `+0x52` |
| Double-Click Speed / `0x7cf958` | `0x8a96a0`, then `0x87f000` | `0x51a840` -> `0x51ad60`; `0x51ad80` |

Sensitivity values are DWORD 0..10000 times 0.0001 (`0x7cbdc4`); missing X/Y default
5000, invalid range resets to 0.5. Existing profiles missing double-click use 500; the
original reset routine `0x4cb33e` uses 0.5. `0x51ad30` converts its normalized slider to
seconds: `0.15` (qword `0x7d04b8`) + slider * `0.55` (float `0x7d04c0`), so the launcher
shows 150..700 ms, not a direct registry-value-to-milliseconds conversion.

## FACT: other controls
`0x42c040` reads `Other Settings` (`0x7cc458`); `0x42c130` saves it. The getters supply
current game defaults, so the launcher offers "Del juego" rather than inventing defaults.

| Registry name / string | Consumer / state |
|---|---|
| Crosshair Toggle / `0x7cc480` | `0x6d8920` / `0x89aecc` |
| AutoAim Toggle / `0x7cc494` | `0x6d8940` / `0x89aecd` |
| AutoWeaponSelect Toggle / `0x7cc4a4` | `0x695890` / `0x891b8c` |
| BulletTimeAid Toggle / `0x7cc4d0` | `0x4554b0` / `0x86c484` |

## Decisions
- Await registry PUT/OPFS close before `MPFiles.files()` enumerates saves or wasm boots.
  Serialization matches host `reg_init`: path, name, type 4, four little-endian bytes.
  On launcher reload the game registry supplies current controls; edits save on Play.
- Keep existing action sliders, joystick values and unrelated settings. No joystick UI:
  `0x516d90` returns null in the current host (see input.md); these controls would be inert.
- No FOV: look_move.md establishes no original user FOV factor. A new patch needs OWNER approval.
- Do not offer fixed/browser/cheat keys or F/N: `web/ffwd.js:295-305` consumes F/N
  during gameplay, so a binding there would not reach the original game.
- No game-code/data patch or new host bridge: this writes the original user profile only.
- Visual references at `~/.codex/skills/frontend-design/references/` were absent. Reused
  existing options layout and motion; no new animation. Safari is outside this front.

Evidence: `rea/c/MaxPayne.exe/004cb090.c`, `004cb3d0.c`, `004cb7d0.c`, `0051ad30.c`,
`0051ad80.c`, `0042c040.c`, `0042c130.c` and the four setters above; `out/launcher_1109/original-input.asm`; runtime and screenshots in that folder.

## Runtime evidence (FACT, 2026-11-09, isolated port 5194)
`RN_GATE_SCRIPT=out/launcher_1109/scenario.mjs node tools/webgate.mjs
'http://127.0.0.1:5194/?launcher=1&state=options&seed=9&fastplay' 120 out/launcher_1109/game.png`
changed settings through keyboard navigation and capture, then launched the original menu.
`read-proof.json`: X/Y = 0.5500000119, both invert flags = 1; crosshair, auto-aim,
auto-weapon and bullet-time aid = 1 at the addresses above. Forward binding = (1,20)
(T); reverse/alternates remain (1,31)/(1,200)/(1,208), and sliders remain 2/1/3.
`before.png`, `after.png`, `bindings.png`, `actions-end.png`: real headless renders,
inspected at <=1000 px, including scroll to the last action and canceled key capture.
Reduced-motion keyboard flow passed. The initial empty profile was used; unrelated
registry preservation also passed a serialization check (`profile-check.json`).
UNKNOWN: Safari, OPFS in-game path, gameplay feel, and injected storage failure states.
