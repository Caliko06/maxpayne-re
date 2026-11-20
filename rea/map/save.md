# Save path: the thumbnail is the stall (g50-save, 2026-11-19)

GATE50 cause S. A save costs one guest frame of ~40 ms (quicksave) and ~60 ms (first autosave); the game stops inside
that frame while the front buffer is read back and the thumbnail is built. Target: no frame > 33.4 ms.

## Addresses (capstone on build/img/MaxPayne.exe, .text va == 0x400000 + off - 0x600)
- `0x410f00` MaxPayne_SaveGameInterface::scaleThumbnail. `mov eax,[esp+4]` (src image), `mov ecx,[ecx+0x15]` (dst
  image), `push 0x3f800000` (0x410f07), `push eax`, `call [0x7ca8e0]` (0x410f0d), `ret 4` (0x410f13).
  So the thumbnail scale is the literal 1.0f and the return address at G_Image::copyWithScale entry is `0x410f13`.
- `0x10203be0` grphmfc G_Image::copyWithScale (host/fast.c rn_copy_with_scale, tools/recomp/patches.py:65). One quad per
  destination pixel as two triangles, four bilinear taps per triangle sample, x87 (`0x102032c0` areaSample, `0x10203460`
  setup, `0x10203700` spans).
- Thumbnail image = TGA 256x192 (rea/map/files.md:75), chosen by snapThumbnailSize `0x411c50`.
- `host/d3d8.c:1117` IDirect3DDevice8_GetFrontBuffer = P_D3D::DRV_getScreen `0x10429d20`, A8R8G8B8 at screen size.

## The capture is two guest steps (Ghidra rea/c)
- `0x44c1b0` case 3 (save requested by the mode executor `0x451960`, pending 3/4): `G_Image::G_Image(local)`,
  `FUN_0060dbe0()[vtable+0x3c](local)` = the driver's screen shot, then `0x410f00` scaleThumbnail(local) into the
  thumbnail image at SaveGameInterface+0x15. So the screenshot image is a temporary, and the thumbnail is the only consumer.
- `0x10429d20` (e2_d3d8_driver_mfc, P_D3D::DRV_getScreen, rea/map/render.md:40): GetDisplayMode -> CreateImageSurface
  (A8R8G8B8) -> GetFrontBuffer -> LockRect(D3DLOCK_READONLY) -> `R_Memory::copy` per row (w*4 bytes) into the G_Image ->
  UnlockRect -> Release. The 3 MB row copy after the readback is guest lifted code, not host.
- `0x410300` saveGame only writes: it serializes to R_MemoryFile, encodes the thumbnail with G_Image::saveBuffer (TGA) and
  writes through R_MemoryFile::save (host/files.c, measured 0.1 ms by the `save` telemetry column). So the expensive frame
  is the capture, not the write.

## Where the frame goes (out/g50_save/before.telemetry.ndjson, unpatched, port 5251)
- Quicksave frame t 49.65: dt 40.7 ms = gpuRead 5.0 (full-res rear readback, d3d8.c read_gpu) + vsync 6 + ~30 in game
  code, which is copyWithScale plus the alpha byte pass GetFrontBuffer runs over `w*h` words.
- Autosave frame t 39.09: dt 61.0 ms, gpuRead 0, save 7.0 -> ~54 ms of game code in the thumbnail sampler.
- The readback is 5 ms of that: making it small would save at most that, and GetFrontBuffer keeps its D3D8 contract.

## Decision1. `host/fast.c` save-only branch: when the caller is `0x410f13` and the scale bits are `0x3f800000` and
   `dw <= sw, dh <= sh`, average each source box once in integers instead of rasterizing two x87 triangles per output
   pixel. Same B,G,R,A word layout, alpha 0xff, truncating integer division like the original's `_ftol` of a mean.
   Other copyWithScale callers (comic/loading art paths) keep the faithful sampler and its globals `0x102169b0`.
2. Readback untouched: measured 5 ms, and the image it fills is what the sampler reads.
Sources: capstone of 0x410f00 (scale literal + return address) and `out/g50-save/blk_compare.log`, block average vs the
shipped sampler `cws_copy_d` on the same source at scale 1.0: it is the same box mean without the bilinear edge blend,
so it is NOT bit-identical (random noise 1280x900 -> 256x192: 49147/49152 pixels differ, mean channel delta 11, max 29;
correlated game frames differ far less). Thumbnail only, and the guard's screenshot shows the result.
Native: sampler 18.1 ms -> 0.50 ms for 1280x900 -> 256x192.

## Measured split of the capture (RN_SAVE=1, out/g50-save/after.ndjson + build/webcheck-5251.log)
- `SAVE gf 1280x900: read 1590 us, alpha 621 us, move 0 us` / `read 1156 us, alpha 180 us` and `guest copy 150 / 199 us`:
  on the web port the readback is ~1.2-1.6 ms, the A8R8G8B8 alpha pass 0.2-0.6 ms (now two pixels per store) and
  DRV_getScreen's row copy into the G_Image 0.15-0.2 ms. The rest of the frame is the guest's thumbnail sampler + saveGame.
- Frames around the three quicksaves (game column, ms): before 18.8 / 24.5 / 40.9 (dt 29.0 / 33.1 / 48.4, one over 33.4);
  after 19.3 / 16.4 / 12.8 (dt 30.8 / 24.6 / 20.3, none over). CopyWithScale's sampler was the >33.4 frame, not the readback.
- The write frame (`saves` row, saveGame 0x410300) stays at 12.8-19.3 ms in both runs: R_MemoryFile + TGA thumbnail encode
  in guest code, plus a 682-1023 KB texture upload. Not host, not in this slice's allowlist.
