# Enemy / Max look: the character render path

Scope: X_Character + skin data -> LOD, texture set, weapon in hand, upper/lower blend, skinned draw.
Addresses are file VAs; `FACT` = read in the cited Ghidra body / bytes / data. Corpus: `rea/c/<image>/<addr>.c`.

## 1. Data the runtime reads (build/data/x_data)

- `3_object_lods.h`: `SKIN_LOD_NEAR 4.0`, `SKIN_LOD_MEDIUM 50.0`, `SKIN_LOD_FAR 100.0`; `WEAPON_LOD_NEAR 2.0`, `WEAPON_LOD_FAR 200.0`. FACT.
- `179_<skin>.txt`: `[Properties] Skeleton = default_skeleton`, `RandomizeTextureSet`, `CanPlayerSwitchToThisSkin`;
  `[LOD]{[Geometry] ExportData=*.kfs; SkinData=*.skd; Distance=SKIN_LOD_FAR; EnableBoundingSphere; BoundingSphereRadius=1.90}`;
  `[TextureSet]{ExportData=*.kfs}` x1..5 (`179_C1_All_Johnny.txt` lines 163-180: Doe_Johnny_Set01..05);
  `[TextureAnimation]{Texturename="FaceTexture"; GroupIndex=0}`; `[Target]{[General] name="UpperBody"|"Legs"; [Bone]...}` (damage groups, not draw).
- All 50 `179_*.txt` have exactly **one** `[LOD]` block, all at `SKIN_LOD_FAR`, `179_lupino_jack.txt` even carries
  `//NOTE!LODS MISSING`. So in Part I a character has no LOD to switch to. FACT (grep over the 50 files).
- Weapons do have 2: `310_beretta.txt:8-9` `beretta_Lod0.kf2 / WEAPON_LOD_NEAR`, `beretta_Lod1.kf2 / WEAPON_LOD_FAR`. FACT.
- `161_default_skeleton.txt`: `[Skeleton]{[Geometry] ExportData=maxpayne-collision.kfs; CrossMode=Orthonormalized;`
  `[Animation] Index=CHARANIM_* Filename=anim\*.kf2}` - one animation id space shared with `characteranimid.h`. FACT.

## 2. Parsers (what turns that text into fields)

- `0x52fc80` character-definition parse (~4 KB): `[Modifiers]` -> count +0x79; `[Target]` -> +0x7d/+0x81 (0x1d-byte records,
  ctor 0x5312b0); `[LOD]` -> count +0x85, array +0x89 (0x3d-byte records, ctor 0x5316b0); `[TextureSet]` -> count +0x8d,
  array +0x91 (0x10-byte `rs` names, filled by `R_Script::parse(..., "TextureSet", i)`); `[TextureAnimation]` -> +0x95/+0x99/+0x9d.
  FACT (`rea/c/MaxPayne.exe/0052fc80.c:407-520`); getters 0x531100 (LOD count), 0x531110 (LOD rec), 0x531130 (TextureSet name),
  0x531150/160/180 (TextureAnimation name/group).
- `0x531730` one `[LOD]` record: string +0x00 (ExportData), string +0x10 (SkinData), float distance, byte +0x3c =
  EnableBoundingSphere (FACT; string table 0x881650). `0x537680` `[Skeleton]` parse: bone-name groups One/TwoHanded
  UpperBody + LowerBody, Aiming, shadows, `RightHandGunBone` at skel+0x58, `LeftHandGunBone` at +0x68 (FACT,
  `00537680.c:322-350`; TwoHandedLowerBodyBone list +0x50/+0x54).

## 3. Which LOD is used: P_LODObject::calculateLOD 0x1011a960 (e2mfc.dll)

- Fields (FACT, body): used LOD index **+0x110**, forced LOD **+0x114** (-1 = free), level count **+0x118**, array **+0x11c**
  of 8-byte entries `{float range; P_BaseObject*}`.
- Math (FACT): `d = |obj[+0x80..0x88] - target[+0x80..0x88]|` (LUT inverse sqrt, `M_LUTInverseSqrt`), then a linear scan
  `if (d < m_rangeMultiplier * range[i]) break;` and `used = i`; if `+0x114 >= 0` the forced index wins. It vcalls +0xc on the
  current child before the scan and +8 on the new one after (INFERENCE: hide / show).
- `m_rangeMultiplier` is a **file-static global** set only through `P_LODObject::setRangeMultiplier 0x1011ac00`
  (get 0x1011ac10), reached from the game by `X_RendererImplementation::setLODRangeMultiplier 0x40ee80` (vtable 0x7cbb60 slot 20).
- Called once per object per frame from `P_LODObject::executeAlways 0x1011ab80` (its only caller) -> **no dt, no FOV, no
  resolution term**; granularity of a LOD change is one frame. FACT for the code, INFERENCE for the visible consequence (pop).
- `+0x80` is the cached world position; `P_BaseObject::calculateObjectToWorldMatrix` re-runs only when dirty bit 5 of `+0xe0`
  is clear. The target is `[vfn+4][0x38]`, the same expression `P_Scene::render` uses for its camera: INFERENCE, the distance
  is camera-to-object.
- Other API: `getUsedLOD 0x1011ab90`, `getUsedLODObject 0x1011aba0`, `setRange 0x1011a870`, `getRange 0x1011a890`,
  `setLODObject 0x1011a8a0`, `forceOn 0x1011abc0` / `forceOff 0x1011abd0` / `getForcedLOD 0x1011abf0`.
- Character LOD list is built in `CharacterFactory::getMesh 0x66ca30`: it counts one block list (`R_Script::count`, name from
  0x532160), and **if `CharacterDetail != 0` and count > 1 it skips block 0 and loads count-1**, then
  `0x561350(KF_VertexAnimationLODs, count, script, blockIndex[])`. FACT for the code; INFERENCE that the block is `[LOD]` and
  that "nonzero = lower detail" (registry default `DAT_00865f84` is outside the raw .data image, so it reads 0). Getter
  `config_getAdvanced_CharacterDetail 0x404310` -> 0x8b3eb8 (`set 0x65a360`, `get 0x65a350`); 0x669680 has the same skip.
- Consequence: with the shipped data the character picks index 0 always; the mechanism matters for weapons/props and for a
  modded 179 file. The port forwards the multiplier 1:1 - any port-side LOD bias must be applied here, not per draw.

## 4. Skin id and texture sets (the model variant)

- Skin id lives at `X_Character+0x714` (getter 0x694530). `X_Character::spawnInit 0x683f50` stores it and with it indexes
  the level's skin array: `C+0x718 = *(param_4[8] + skinId*4)` = the character definition record. FACT (`00683f50.c:178-179`).
  So "skin" = one `179_*.txt`: mesh + skeleton + all texture variations of that character.
- Variation count = `0x66d810` = `1 + textureSetCount(def+0x8d)`. In `0x683f50` (loop at `00683f50.c:441-459`) it builds every
  index (`0x66c960`, then `0x66ca30` to load), registers the `[TextureAnimation]` entries (`0x740d80`), releases it (`0x66d450`);
  after the loop it builds the kept one and stores it with `0x691720` (setBaseObject). FACT for the calls; INFERENCE that the
  loop preloads all variants and that `0x66c960` is (skin, variant) (Ghidra lost its arguments, `00694020.c:99-118` shows the
  same pair driven by `C+0x714`).
- `CharacterFactory::getMesh 0x66ca30` per variant: loads the KF script of the def, builds the LOD list (`0x561350`), loads the
  skeleton animations (`0x54c1c0`, `0x54c8e0`), then creates a texture-set object (`0x56b620` default / `0x56b3a0` per variation),
  appends it (`0x66dc80`) and applies the `.kfs` to it (`0x54d740`, reads the file, `KF_Memory_Container`). FACT for the calls.
- Face: `[TextureAnimation] FaceTexture / GroupIndex 0` is registered at spawn; `C_AnimateTexture` (handler 0x699940) swaps the
  face texture for grin / shoot-grin / death. FACT for the handler registration, INFERENCE for the group semantics.
- `RandomizeTextureSet` is a `[Properties]` flag parsed into the def; the spawn-time pick was **not** located (no RNG call site
  in 0x683f50 / 0x694020). UNKNOWN.

## 5. Weapon in the hand

- Arsenal at `C+0x398` (getter 0x6916e0); two hand sets, stride 0x26d (`X_WeaponArsenal::ctor 0x729040`, `clearClips 0x729820`).
  Hand 0 = right. FACT (names TSV + `00729820.c`).
- `X_WeaponArsenal::setCurrentWeapon 0x729940`: unload 0x729a10, map the weapon id through the DB (0x53c380), then
  `X_WeaponSet::attachWeapon 0x757b60` and, **only if `UseMultipleGunBones` (0x53b850, wdef+0x36)**, attach in the second set too.
  FACT (`00729940.c`).
- `attachWeapon 0x757b60`: id -> `+0x1bf`, mesh = `0x66b1e0(id)` -> `+0x1bb`, `+0x1ff = 0.5`, then `0x7530a0`.
- Per frame `X_WeaponSet::composeBoneTransform 0x758020` (vtable 0x7d9ad4 slot 3) runs when a mesh exists, `+0x203 != 0` and
  `+0x204 == 0`: it composes the 4x3 at `+0x1cf` (getter 0x757ff0) with the world 4x3 from `0x74e8e0` (base-object matrix),
  writes the 12 floats to mesh+0x2c..0x58, `P_BaseObject::invalidateMatrices`. FACT for the body; INFERENCE that the `0x74e8e0`
  object is the hand bone (the bone names skel+0x58/+0x68 come from the skeleton parse, but the resolve call site - vtable slot 37,
  `KF_ObjectAnimation::findBoneOrThrow 0x54e870` - is not greppable statically). UNKNOWN: exact store of the bone pointer.
- Weapon animation (reload/shoot/hide) is separate: `X_WeaponSet::tickAnimation 0x757e00` (time += dt, clamped),
  `WS_Animate` handler 0x758360, `+0x1c3` id, `+0x1c7` time, `+0x1cb` clip. FACT (names TSV).
- Weapon LOD: the mesh is its own P_LODObject built from the weapon `[LOD]` blocks (2 for the beretta), so the weapon drops to
  Lod1 at 2 m * multiplier while the body stays on its single mesh. FACT (data) + INFERENCE (same LOD class).

## 6. Upper / lower body blend

- `X_Character::update 0x687f20`, per substep (`00687f20.c:296-300`), order is FACT:
  `0x68f480 applyWeaponChange` -> `0x68c960 updateLowerBody` -> `0x68ebd0 buildAimRay` -> `0x68df90 updateUpperBody` ->
  aim read -> `animator->vfn+0xc(dt)`.
- `0x68c960` (profile `"Char.LowerB"` 0x892c4c): if not shoot-dodging it calls `0x744d80(animator, dt, 1.0, 0, 0)`;
  otherwise `0x68c850` blends the two shoot-dodge sectors (sector = heading*4/pi, weight 1-frac).
- `0x68df90` (profile `"Char.UpperB"` 0x892c58): if not shoot-dodging and not aiming it calls `0x744dc0(animator, dt, 1.0, 0, 0)`;
  aiming/shoot-dodging take the aim-matrix branch (0x725be0 / 0x74e8e0; INFERENCE: that rotation lands on the aim bone).
- `0x744d80`/`0x744dc0` differ only in the first argument of `0x744ca0`: **1 = lower, 0 = upper**. FACT (`00744d80.c`,
  `00744dc0.c`, `00744ca0.c`). Each ends with `0x753490(animator, dt)` = the animation tick.
- The channel machinery (gameplay.md §3, FACT): animator+0x25[ch] channel, weapon mode table `0x7453e0` / `0x745400`,
  `X_CharacterAnimator::playRequest 0x745130` with jump table 0x74528c (play once / crossfade 0x74a180 / loop / continue),
  `X_CharacterAnimator::getKFObject 0x7453e0`. Two KF objects per weapon mode = the two "bodies": each one's tracks cover only
  its bone group. INFERENCE: 0x68c960/0x68df90 do not build a per-bone weight mask; the split is by which bones each .kf2
  animates, and the aim rotation is applied separately in the upper path.
- Blend is a weighted sum of 3x4 matrices (`0x54e470`, `0x572530`, `X_ObjectAnimationLODs` snapshots for crossfade), then
  CrossMode (Orthonormalized here) re-normalizes. FACT (gameplay.md §2).

## 7. Drawing the skinned mesh into D3D8

- Render pass: `P_Scene::render 0x10118ec0` (e2mfc) walks the visible virtual objects (+0x13c list), skips primitives with no
  material, and pushes `P_SortBuffer::addEntry(buffer, depthKey, obj, primIdx, -1)`; sort `0x1012ce70`, then per entry the
  primitive's `process(obj, primIdx, sortBuffer)` virtual. FACT (`10118ec0.c:180-260`). Depth key is a squared camera distance
  divided by `obj[0x38][0x1d8]*2` and scaled by constants at 0x10149768 / 0x10149e38 -> projection-scale dependent, no resolution
  term (INFERENCE on the constants).
- Skin mesh: `P_D3DSkinMesh::process 0x1041b290` (driver): `P_VirtualObject::getPrimitiveInstance`, RTDynamicCast to
  P_D3DOutputPrimitive / P_D3DVertexBuffer / P_D3DTriangleBuffer, poly/vertex counters, `P_VirtualObject::getBoneMatrices`
  (0x1010de90), then either the software skin fill (`0x1041b690`, when out-flag `local_1c` is 0, INFERENCE: the buffer cannot
  use hardware blending) or the plain buffer path, `setVertices`, `setIndices`, and `P_D3DOutputPrimitive::render 0x104170e0`
  -> `SetStreamSource` / `SetIndices` / `DrawIndexedPrimitive` (render.md "Geometry").
- Software skinning `0x1041b690` (FACT, body): per vertex, bones = `(fmt>>8)&7`, per bone a float weight and a **bone index
  stored as the low byte of a float** in the VB (`+0x5c` array); `p' = SUM w*(M_b*p + t_b)`, normals transformed the same way
  and normalized with the LUT inverse sqrt; results are written back into a plain XYZ/NORMAL VB. Up to 4 bones per vertex
  (`KF_SkinMeshCallback::createSkinMesh 0x57c010` throws above 4; `KF_VertexAnimation::checkSkinGeometry 0x57cf20` throws
  `KF_Exception_InvalidGeometry` when the .skd and the .kfs disagree).
- The 2026-11-01 trace saw only FVFs 0x12 / 0x112 / 0x142 / 0x152 / 0x212 (no XYZB1-4, no INDEXEDVERTEXBLEND), so the
  characters ran the software skin path. INFERENCE from the FVF list in render.md. Port: `host/d3d8.c:1193` `fvf_decl` halts
  on `XYZB*` and `host/d3d8.c:1231-1239` `vsd_decl` on BLENDWEIGHT / BLENDINDICES registers - correct only while the guest
  keeps choosing software skinning; if a cap report ever makes it take the hardware path, the port stops loudly. FACT (code).

## 8. Frame-rate / resolution dependence

- LOD: distance compare, no dt, once per frame -> a hitch can flip a LOD for one frame; same distance = same index at any fps.
  No resolution term; only the global range multiplier (LOD bias). Texture sharpness comes from the video settings mipmap/LOD
  bias (`config_writeVideoQualitySettings 0x40eaf0`), not the draw path. Nothing in sections 3-7 reads the backbuffer size.

## 9. Open

- Exact block name in `0x66ca30` (`DAT_008addc0` is runtime-initialized) and the meaning of `CharacterDetail` values. UNKNOWN.
- Where the variation index for `RandomizeTextureSet` is drawn, and where `C+0x714`-driven model choice repeats per instance
  (0x694020 touches 0x66c960/0x66ca30 again). UNKNOWN.
- Bone resolve for the gun (skeleton +0x58 / +0x68 -> P_BaseObject) and the store into X_WeaponSet. UNKNOWN.
- `+0x110` read by `P_Scene::render` as part of the sort key is a field of P_VirtualObject, not P_LODObject's used-LOD;
  its meaning is UNKNOWN; do not assume it is the LOD.
- Not measured: any frame where a character actually used LOD 1 (data says none), and the cost of the software skin fill
  per frame on the port (0x1041b690 is host-lifted, no timing taken).
