# Web surface look, 2026-11-09

Scope: presentation and navigation in this worktree, port 5194 only. No original game code,
data, lifter or host bridge is changed. Existing c27d2f2 controls remain authoritative.
Original controls: loader `0x4cb3d0`, defaults `0x4cb090`, 32 actions at `0x8a9168`,
other controls `0x42c040`; see launcher.md and the cited Ghidra corpus. No patches.py entry applies.

## Problems and the prior art used before designing

| Surface | Real problem | Existing solution and adaptation |
| --- | --- | --- |
| Main / cold | Cropped wordmark, tiny prompt, long repeated reveal | Max Payne's own menu grammar and Mobile port: retain runtime artwork and condensed labels; clamp the independent wordmark inside a safe margin; 240 ms reveal, 250 ms handoff. |
| Options / controls / all 32 action panels | Conflicting CSS and narrow columns make values and explanations compete | Original setup dialog uses fixed choices; ThirteenAG WidescreenFixesPack explains label + consequence. One ruled list and one paper caption, stacked at narrow widths. Keep every existing option, all four bindings and defaults. |
| Chapters | Artwork, narration and navigation need a reading order at 390 px | Graphic-novel gutters and the existing chapter sheet: cover, caption, narration, milestones, then the filmstrip. Runtime game art only. |
| Saves | Zero saves throws while looking up the first row | js-dos uses frame/date rows. Keep those rows; explicit empty copy explains where saves come from and Esc returns. |
| Cheats | Toggle choices compete with the background | Original Max Payne setup list: same full-width selection plate and the same paper consequence caption as Options. |
| Loading / folder picker / errors | Small text, weak progress track, default button and unrelated red | js-dos fullscreen player and comic caption grammar: larger wordmark, visible track, explicit folder action, shared paper/red errors. No invented percentage. |
| Avanzar bar / panel / running caption | Equal-weight cards, glyph-only menu, swallowed Tab, header CSS leak | js-dos floating player UI + Max Payne comic gutters: compact action strip, numbered contact sheet, paper current selection, named Hitos/Cerrar, dialog focus cycle. Hide the bar while mode 3 still has an empty level. |
| Notices / touch / debug | Four palettes and unrelated rounded web UI | ScummVM's one-theme rule: shared tokens and type. Diagnostic graph keeps distinct grayscale series and red faults; touch directions retain their circular stick affordance. |

Prior-art inputs: the OWNER-provided look-inventario.md, look-auditoria.md and look-prior-art.md
in `/Users/angeldamian/maxpayne-re/out/bec/1109/` were read only.
Primary examples: https://github.com/ThirteenAG/WidescreenFixesPack,
https://js-dos.com/overview.html, https://github.com/scummvm/scummvm/tree/master/gui/themes.
No component framework or downloaded art was needed: the existing DOM, runtime gamefile loader,
scene renderer and registry bridge already solve the functional work.

Design references read under `~/.hermes/skills/.archive/frontend-design/references/`:
`taste-principles.md` (hierarchy, states, collapse duplicates), `anti-slop.md` (no CSS accretion),
`pattern-galleries.md` (choose composition before components), `motion.md` (140 ms / .97 press,
under-300 ms interface motion, reduced motion). The global historical reference path is absent.

## Evidence and limits

`out/keep/1109/look/` retains the before/after PNG pairs at 1280x900 and 390x844,
layout and controls JSON, game-state evidence, scripts and build/browser logs.
The baseline is c27d2f2, served via browser routes for the additional action-panel captures.
Loading/error images use the actual DOM with deterministic messages, not injected IO failures.
The first game capture opened the baseline panel before the level string was ready; its next
button was correctly disabled and its attempted click timed out. This prompted the empty-level
visibility fix; it is not evidence of an operational next-milestone action.
Safari, native gameplay, real folder copy, injected disk/write failures and manual game feel
are outside the observed evidence. No private game artwork is added to git.

The second and final real game run reached `part0_level1` and captured the live Avanzar bar,
then entered a narrative sequence before the panel click. The browser click timed out;
`final.log` records the failure. Exactly two engine runs were used, both headless and muted.
Final panel/advancing pairs are explicitly layout fixtures: production MPFF DOM and mailbox
code with a fixed SharedArrayBuffer state, without loading the engine. The fixture proves
focus cycling and close behavior, not game progression (`*-fixture-proof.json`).
The final fixture also exposed and fixed a `.ff-next` selector shared by the bar and first
panel card; bar styles are now scoped beneath `.ff-bar`.
All 32 binding screens and the basic screens were inspected at both widths. Controls proof
records sensitivity change/restore, capture/cancel and native Tab access to utility controls.
Build passed with existing compiler warnings; static JS syntax and diff whitespace passed.
Safari, actual next-milestone advancement and real save failure recovery remain UNKNOWN.


## Pass 2: composition and player-control visibility

Problem: the first pass kept a floating chapter card, unused lower screen, tiny control
values and a scene wordmark behind settings. Mode 3 alone also exposed the action bar
during scripted cameras and credits.

- Reuse the game's existing logo/cover assets and scene background. DOM owns the logo;
  the scene's existing setLayout contract suppresses its separate logo layer. Main uses
  one left margin and a shared 41vw card/detail axis, with a selected-row connector and
  a lower chapter/save caption. Narrow layout uses a full-width cover and caption.
- Reuse the existing original setting values, registry serialization and keyboard actions.
  Sensitivity/double-click now use range inputs and large outputs. Boolean choices are
  explicit No/Yes segments; Other Settings retain the third inherited state. Each
  original setting shows its effect, range, default and exact registry name.
  Source: launcher.md and rea/c/MaxPayne.exe/0051ad30.c, 00519960.c. Defaults/ranges
  remain 0x4cb090/0x4cb3d0, sensitivity 0x519850/0x742870, other controls 0x42c040.
- The action bar requires game mode plus player input, no script camera/inventory,
  and a living player. It appears on hover, keyboard focus or F/N/Tab only. This reads
  existing host/gamestate.c GSC_* fields: input link 0x456650 and consumer 0x5030e0.
  No original code/data or host bridge changed; patches.py does not apply.
- Layout keeps the selected row visible after viewport changes. Narrow hints use two
  rows and keep mute visible; original action screens preserve all four bindings.
- Prior art requested at out/bec/1109/look-prior-art.md is absent in this worktree.
  Reused the existing table above and rechecked js-dos's player/sidebar approach:
  https://js-dos.com/v7/build/docs/jsdos-overview/ and https://js-dos.com/player-api.html .
  Adaptation is our design decision, not a claim that js-dos implements these widgets.
  The four global frontend reference files are absent at the prescribed path and
  were also not found in installed skill roots. No new animation exceeds 240 ms;
  existing reduced-motion rules cover the changed presentation.

FACT evidence: out/keep/1109/look/pass2-proof.json records keyboard, slider (6500),
boolean and binding cancel checks. A real part3_level7 run sent the original
Credits_Start->FSM_Send(start_credits), which disables controls at LDB @22799742
and starts Camera_01.DO at @22799792 (out/bec/final/informe.md original-script analysis).
The snapshot changed mode 3/control 1 to mode 3/control 2, with bar visibility hidden;
after-credits-{1280,390}.png show the actual opening credits/completion message.
After-* keepers and pass2-keys-sheet-*.png cover 1280/390 layouts and all 32 actions.
resize-proof.json records the selected row after resize. Logs retain the first game
run's hover timeout and the successful second run. Exactly two game starts.
UNKNOWN: Safari, full credits duration/return to menu, every cinematic, manual gameplay
feel and folder-copy failures. No third game run is authorized or performed.

FACT: RN_BR=0 RN_JOBS=2 web build succeeded (pass2-build.log), with existing
host/files.c inline-JS and emcc pthread/memory warnings; JS syntax and diff whitespace checks passed.
