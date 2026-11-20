# FX on screen: particles, decals, muzzle flash, tracers — and what the host drops

Scope: the D3D8 features the FX path uses (blend, alpha test, quads vs point sprites, texture stages,
z-bias, fog, draw order), then `host/d3d8.c`, `host/ffp.c`, `host/gpu.c`: what they honour, ignore or approximate.
Sources: `rea/c/<image>/<addr>.c`, `rea/map/names/MaxPayne.exe.tsv`, `census/d3d8_spec.tsv` (every D3D8 call site of
the driver, 213 rows), `rea/map/render.md` (trace of the states the game really sets), `build/img/MaxPayne.exe` bytes.
FACT unless marked. Rebased addresses: exe 0x400000, e2_d3d8_driver_mfc.dll 0x10400000.

## 1. Where the FX start: no D3D call in any of these

- `PS_StartEffect` = `X_WeaponSet::onMessage_PS_StartEffect` **0x758740**, 0x30 bytes (FACT, bytes at 0x758740):
  `if (this[0x204]) { 0x669000(this+0x150, msg); if (al) 0x668af0(this+0x150, msg); }` — this+0x150 is the particle
  interface, 0x204 the "has a mesh" flag that the projectile handler also checks (`00758790.c`).
  0x669000 walks the attached list (stride 0x364) comparing the message's effect name (`00669000.c:14-26`);
  0x668af0 allocates a 0x364-byte instance and plays the message's animation (`00668af0.c:142-147`). No D3D call in
  either; the draw happens later in the scene.
  Same shape elsewhere: X_Projectile 0x708260 (`add ecx,0x1e`), X_Character 0x69c470 (`add ecx,0x15b`), bytes read.
- Muzzle flash is that message: `310_beretta.txt:19` `this->PS_StartEffect ( Muzzle_Beretta, "" );` frame 0
  (and `161_default_skeleton.txt:1368,1370` RightHandWeapon / LeftHandWeapon at frames 0 and 8).
- `D_CreateDecal` = `X_Projectile::onMessage_D_CreateDecal` **0x708410** (`00708410.c`): only if `proj[0x27c]` (the
  decal-on-hit flag); passes the decal group name, `proj[0x290]`, the world position `proj+0x230` and the surface
  matrix `proj+0x260` to vtable slot 0x34 = `X_DecalImplementation::createDecalForMessage` 0x65c910.
  0x65c910 picks the per-material entry (`[Material] Name=...` blocks in `9_decals.txt:245-498`) or throws
  `X_Decal: [Message] "%s" not material` (`0065c910.c:255`); the geometry is a `P_TriangleMeshBase` filled at
  `00730b20.c:437-459` (conforming patch, not a flat quad). Radius 0.02-0.09 m, plus a separate `AlphaFilename`.
- Tracer: a projectile mesh, 3 LODs of `tracer_9mm_lod0-2.kf2` (`63_bullet_beretta.txt:10-12`), shown only while
  speed > `BULLET_VISIBILITYSPEED` 45 m/s (`3_projectiles.h:9`, getter `X_SharedDBProjectile::getVisibilityMaximumSpeed`
  0x541100). No FX-specific draw code: it is an ordinary triangle mesh.

## 2. What that path really asks of D3D8

| feature | value / form | evidence |
|---|---|---|
| quads, not point sprites | 4 vertices + 2 triangles per particle, built on the CPU | `10419a80.c` (fill loop, 0x24 floats per particle); `10419400.c:66-67` `iVar17*2` polygons, `*4` vertices |
| billboard basis | camera right/up, inverted 3x3, once per primitive | `1041aba0.c` (thunk_d3d_particle_build_billboard_basis), called from `10419400.c:119` |
| particle vertex | 9 floats: pos3 + normal3 + one 3-float texcoord (stride 0x24 = 36 B) | writes at offsets 0-8, 9-0x11, 0x12-0x1a, 0x1b-0x23 in `10419a80.c:222-257` |
| draw order | each particle **triangle** is its own sort entry, depth-keyed by camera distance | `P_SortBuffer::addEntry` twice per particle, `10419a80.c:175` and `:188`, key from `param_8`/`param_9` |
| blend | SRC/DSTBLEND **2/1** (ONE/ONE, additive) and **5/6** (SRCALPHA/INVSRCALPHA) | `rea/map/render.md:30` (per-draw trace); the two values are the only pairs ever set |
| alpha test | ALPHATESTENABLE 15 with ALPHAREF 24 / ALPHAFUNC 25 changed per draw | `rea/map/render.md:30`, `census/d3d8_spec.tsv:208` |
| texture stages | 2 stages used; COLOROP/ALPHAOP 1,2,4,7,13; ADDRESS 1,3; stages 2-3 off | `rea/map/render.md:32` |
| z-bias | **never**: ZBIAS (0x2f) appears nowhere in the driver | `census/d3d8_spec.tsv:208` "NOT seen anywhere ... ZBIAS(0x2f), POINTSIZE/POINTSPRITE" |
| depth for decals | only ZFUNC 4 (LESSEQUAL) and 8 (ALWAYS), ZWRITE toggled per draw | `rea/map/render.md:30`; decal patch lies on the surface (`00730b20.c:437`) |
| fog | FOGENABLE 28, FOGVERTEXMODE 140 = 0 or 3 (LINEAR), FOGCOLOR/START/END/DENSITY | `rea/map/render.md:30`; table fog (FOGTABLEMODE 0x23) never set |
| cull | CULLMODE 22 from `P_Material::getBackfaceCullingMode` (per primitive) | `10419400.c:189`; data `DualSided = FALSE` in `9_decals.txt` |
| absent | no stencil, no multisample, no render-to-texture on this path | `census/d3d8_spec.tsv:208` |

INFERENCE: the additive pair 2/1 is what flash and tracer materials use (it is the only additive pair in the whole
trace, and the rest of the scene is 5/6 or opaque). Which material block asks for ONE/ONE is inside the binary `.rps`/
`.kf2` chunks and was not decoded here — UNKNOWN.

## 3. Per effect, in one line

- **Muzzle flash**: particle system attached to the weapon (bone transform), started by 0x758740, drawn by the
  billboard path above; alpha from the texture, additive or alpha-blended per its material.
- **Impact puffs / sparks / smoke**: same path; the group is chosen by material name (`9_decals.txt:251`
  `PS_StartEffect( bullethit_generic, "")`, `38_particles.txt:3290` bullethit-default.rps).
- **Bullet holes**: conforming triangle patch on the hit surface + separate alpha bitmap; alpha blended, no z-bias,
  depth resolved by ZFUNC/z order. INFERENCE: LESSEQUAL over the coincident surface is the whole trick.
- **Tracer**: ordinary mesh, 3 LODs, no blend state special-cased by the FX code.

## 4. What the host does with exactly those states

Honoured (FACT, code):
- Blend factors ZERO..SRCALPHASAT and BOTH* : `host/gpu.c:218-231`; blend ops add/sub/revsub/min/max `host/gpu.c:238-244`;
  applied at `host/gpu.c:311-318`.
- Alpha test with both func and ref: `host/d3d8.c:1697` (`rs[25]` func, `rs[24]/255` ref), tested at `host/gpu.c:308`
  through all 8 D3DCMP values (`host/gpu.c:212-217`).
- Z test/write/colorwrite/cull: `host/d3d8.c:1706-1708` to `D_ZEN..D_CWRITE`, used at `host/gpu.c:268-270`,
  `host/gpu.c:294`, `host/gpu.c:319-321`.
- Fog, vertex LINEAR only: constants at `host/d3d8.c:1569-1573` (FC_FOG from rs[36]/rs[37]), written to oFog at
  `host/ffp.c:156`, applied post-pixel at `host/gpu.c:303-307`. Fog colour from rs[34] (`host/d3d8.c:1695`).
- Two texture stages, all ops the game uses 1/2/4/7/13: `host/ffp.c:222-253` (op 13 = BLENDTEXTUREALPHA at
  `host/ffp.c:238-243`), 4-stage ABI cap `host/gpu.h:13`.
- 4 verts/2 triangles, 36-byte stride, 3-float texcoord: `fvf_decl` accepts texcoord size 3 (`host/d3d8.c:1198-1200`)
  and stride 36 follows (`host/d3d8.c:1203-1210`).

Ignored or approximated (FACT, code):
- **ZBIAS**: not implemented, a draw with `rs[47]` halts — `host/d3d8.c:1665`. The game never sets it, so decals live
  or die by the float depth compare alone.
- **Depth is unbounded float, not 24-bit**: `host/gpu.c:293` interpolates `z` and `host/gpu.c:294` compares it raw. A
  native D3D8 device quantises to the D24S8 buffer the game created (`rea/map/render.md:34`). INFERENCE: a decal
  patch at exactly surface depth can win or lose a pixel differently from the original; nothing in the host adds the
  sub-pixel/normal offset that a hardware path would not need either.
- **Point sprites / point lists**: never modelled. FVF with point size halts (`host/d3d8.c:1195`), `D3DPT_POINTLIST`
  is skipped with one log line (`host/d3d8.c:1673`, ABI note `host/gpu.h:37`), POINTSIZE/POINTSPRITE states are
  stored and never read (`SetRenderState` keeps any index, `host/d3d8.c:634`). No loss: the game builds quads.
- **Table fog**: halts (`host/d3d8.c:1567`); other vertex modes than LINEAR halt (`host/d3d8.c:1574`). FOGDENSITY
  (rs[38]) is never read, so EXP/EXP2 would need code. Not used by the game.
- **Blend factors BLENDFACTOR/INVBLENDFACTOR (14/15)**: fall into `default: v = 1` = ONE, silently additive —
  `host/gpu.c:230`. Only reachable if the guest starts using them; the game does not.
- **DITHERENABLE (26)**: never read; no dithering. Also stored-only: STENCIL* (halted when enabled,
  `host/d3d8.c:1663`), CLIPPLANEENABLE (halt at `host/d3d8.c:1664`), multisample (CreateDevice halts,
  `host/d3d8.c:469`).
- **ALPHAOP DISABLE** is mapped to SELECTARG2/CURRENT, i.e. "keep the incoming alpha" (`host/ffp.c:270-272`); DX8
  says the stage does not contribute. Same picture as long as the previous alpha was the intended result —
  approximation, not a match.
- **Billboard basis is the guest's**: the host does no camera-facing maths of its own. If the port ever had to
  emulate points, it would have to invent it. It does not.

## 5. Open

- Which material/texture flag asks for ONE/ONE (additive) on flash and tracer: inside `.rps`/`.kf2` chunk data, not
  decoded. UNKNOWN.
- Whether the decal patch carries an epsilon along the normal: not found in `00730b20.c` (the fill writes surface
  vertices and a 0.5/-0.5 extent scale, constants 0x7cbdd4 = 0.5 and 0x7d4aa0 = -0.5); the ZFUNC 4 reading is
  INFERENCE. A native-vs-port screenshot of the same bullet hole would settle it.
- Particle draws are one triangle each after sorting (`10419a80.c:175`, `:188`): nobody measured the draw count this adds
  per frame on the port; `rea/map/render.md:25` counts ~132 draws a frame with no FX-heavy moment.
