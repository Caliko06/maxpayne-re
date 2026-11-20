# 2D layer: HUD, loading bars, cutscene bars (MaxPayne.exe 1.05 + e2mfc.dll)

Why: 3D is Hor+ correct (see `widescreen.md`). All 2D still lives in a 640x480 unit space mapped into the centred
4:3 column, so the HUD sits bunched in the middle and the side strips keep stale pixels.
Canonical doc. Raw evidence: `rea/ws/2d_e2mfc.md` (engine side) and `rea/ws/hud_exe.md` (game side).

## FACT: the 4:3 root is one place, in e2mfc

- Only three renderers read the unit scale: `P_Sprite::executeAlways 0x10108040`, `P_Text::executeAlways 0x10104280`,
  `P_2DLineObject::executeAlways 0x10105eb0`. No 3D code reads those constants.
- Unit -> world: raw camera fov at `0x1010805e` (`fptan`) then X `fmul [0x101495d0]` = 1/320, Y `fmul [0x101495cc]` = -1/240.
  Text repeats it at `0x1010458b/0x10104599/0x10104616/0x1010464c`.
- Unit -> pixel funnel, one for all drawing: `P_Camera::getScreenRect 0x101145c0`, W `fmul [0x10149de8]` = 1/640 at
  `0x10114602`, H `fmul [0x10149de4]` = 1/480; sub-viewport branch `0x101147d5` (cam+0x1dc..+0x1e8).
  Then `P_Driver::setViewport 0x10132870` -> driver vtable +0x5c. Width/height `0x10132c90/0x10132ca0`.
- Aspect: `P_Camera::validate 0x10114d60` writes +0x200 = tan(fov/2)*0.5 and +0x204 = viewport dy/dx (0.75).
  2D quads use the RAW fov, the projection divides by +0x200, which `host/widescreen.c rn_ws_camera` multiplies by
  scale_x at `0x10114da5`. Net: 640 units cover exactly 0.75*W. **2D is under-filled, not stretched.**
  That is why a 640-wide fade stops at the 4:3 edge and `rn_ws_sprite` widens it in units.
- Camera rect is written only by `0x10113ab0` (clamped 0..640 / 0..480, DAT_10165604 / DAT_101655fc) and `0x101144a0`
  (from P_Map +0x74/+0x78, pixels). e2mfc never draws 2D into a render target.

## FACT: HUD positions are data, not a layout table

- `hud.txt` (string `0x878f74`, ptr `0x8786e8`) is read by `FUN_004b0680` (MaxPayne_HUD ctor); it locks each block by
  name and stores the element at `HUD+0x188`. Names are literals in `.data`: Health `0x878ee4`, SlowMotion `0x878884`,
  PrintTip `0x878eec`, Objectives `0x878ec4`, Print `0x878ef8`, Slot0 `0x878870`, TimedMode `0x878eb8`,
  Painkiller `0x878760`, Overlay `0x87877c`, ActiveWeapon `0x87876c`.
- Values are 640x480 virtual units (`P_Sprite +0x188/+0x18C`, `P_Text +0x16C`). The exe only stores them; the mapping
  to screen is the e2mfc site above. So editing `hud.txt` reshuffles the HUD inside the 4:3 column only, never to the
  real edges. Edge anchoring needs the draw-time offset.
- `MaxPayne_HUDMode::render 0x4af900`; HUD hide gate `0x4af907..0x4af914` reads `[GameMode+0xEE]` then `je 0x4afb47`.

## FACT: bars

- Loading screen: `0x5828c0` (`X_ProgressBar::updateProgressBar`), two P_Sprite quads, filled `0x8afcc4` (width
  k*progress) and rest `0x8afcc8` (k*(1-progress)), k = `0x8afccc`, height `0x8afcd0`. The image (level setting
  "loadingscreen" `0x87e2e0`) covers only the 4:3 area, so the strips keep the previous frame.
  **Correction to `widescreen.md:10`: `0x582a18` is NOT the bars**, it is `mov ecx,[P_Driver]` after the draw call
  `0x5859e0` and before endScene `[0x7ca518]` / swapScreen `[0x7ca51c]`. It is still a usable hook point, but what it
  does is clear/paint the strips, not draw them.
- Cutscene bars: `0x4afaa2` (`call 0x421ac0` = GameMode+0x1260, height multiplier), `0x4afaa7` `fsubr 1.0; fmul H;
  fmul 0.5` gives one bar height (1-m)*H*0.5; same multiplier at `0x4af943/0x4af95b/0x4af9f0/0x4afa4a` (subtitles).
  The 4:3 comes from data: `GameMode+0x1230` (X_GlobalCinematicSettings). The exe draws top/bottom only, never sides.
- Crosshair is NOT 2D: `X_Crosshair::updateTransform 0x6d7e60` writes a world matrix scaled by zoom (+0x17d/+0x16f).

## Options (none applied)

| option | site | effect | cost |
|---|---|---|---|
| A, one hook | `0x10114da5` on the 2D camera's +0x200 | 2D fills the width, everything stretched by scale_x | needs to know which P_Camera renders 2D |
| B, data | float `0x101495d0` = scale_x/320 | each element grows in place in the 4:3 column | one `patches.py` entry; glyphs get scale_x wider; layout does not spread |
| C, HUD anchor | offset added to `+0x188` (sprite) / `+0x16C` (text) in e2mfc `executeAlways` | HUD reaches the real edges, proportions kept | needs the block name as a sign: hook `0x4b0680` and tag via R_Script current block (+4, parent +0x14) |
| D, bars | `0x582a18`: paint (0,0,barW,H) and (W-barW,0,W,H) black before endScene, barW=(W-H*4/3)/2; `0x4afaa2` -> multiplier 1.0 | loading and cutscenes span the screen, no stale strips | two hooks |
| E, skip | leave 2D 4:3 | original look | zero |

INFERENCE (C): anchor offset = (aspectScaleX-1)*320 virtual units; 16:9 gives 106.67 units = 240 px at 1920x1080,
which matches the fix's `fFullscreenExtension`. Sign: left = Health, SlowMotion, PrintTip, Objectives, Painkiller;
right = the rest.

## UNKNOWN, and what is NOT verified

- Which `P_Camera` renders the 2D scene (created in MaxPayne.exe). Option A needs that id or a pass flag; C and D don't.
- Whether menu and comic states reach `0x10108040/0x10104280` at all. Only HUD sprites and text are confirmed.
- Sniper scope width: `hud.txt` "Weapons/Overlay" sprites from `0x4b0680`; the pivot=100 / width=100 the ThirteenAG fix
  patches lives in e2mfc `executeAlways`. Not localised.
- Nothing was verified at runtime. Both workers had the REA/**Hopper bridge fail with a startup timeout**, so the second
  key was capstone/objdump against the Ghidra decompile: same bytes, not independent. Per AGENTS this does not yet count
  as confirmed. The cheap way to settle it is one capture, not more static work.

## How this was produced (so it can be repeated)

Two parallel REA workers, one binary each, disjoint output files. `resolve_containing_procedure` was 566 of the 598
recorded batch calls; the value came from the bulk dump (`rea/c/`, 22 569 functions) and 10 257 durable names in
`rea/map/names/`. Everything above was found by grepping `rea/c/` first and spending REA calls only on the sites that
mattered.
