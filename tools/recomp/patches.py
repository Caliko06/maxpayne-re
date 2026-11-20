"""PATCH table (AGENTS.md contract 3): every change to game code or data, in one place.

The lifter, the difftest and the loader (build/gen/table.c -> host/main.c) all see the patched images.
Each entry checks the original bytes first; a mismatch stops the build. Addresses are guest addresses
(census/images.json: the rebased DLL bases).
"""

PATCHES = [
    # BRIDGE: the startup dialog's capture of the selected mode's 32-bit texture support, exe 0x408020 -> 0x404590
    # (byte 0x8a6307 = mode item +0x10 = P_DisplayMode::supports32BitTextures, e2mfc 0x1012f8b0). host/win32.c skips the
    # dialog (-skipstartup), so the byte stays 0 and 0x4043c0 turns "Texture Color Depth" 32-bit into 16-bit. The driver
    # builds every mode of our adapter with that flag set (P_DisplayMode ctor e2mfc 0x1012f100, param 8 = 1 at runtime).
    {"addr": 0x8a6307, "old": "00", "new": "01", "when": lambda: True},
]

# Menu keyboard on the left hand (rea/map/input.md 4). Every menu already has a keyboard: the menu container FUN_006354e0
# (called per typed key by the menu mode update 0x6313e0 and the item OnKey slot 0x20 first) switches on the VK through the byte
# table 0x635680 (index vk-8) into 0x63565c: 0 Esc/Backspace = back, 1 Enter = select, 2/4 Left/Right = item adjust (sliders,
# options), 3/5 Up/Down = focus move, 6 Delete, 7 F9, 8 = ignore. Only the arrow keys were wired, so the table gets the letters:
# W/S = Up/Down, A/D = Left/Right, Space = Enter, Q = Esc. An item that is capturing a key (X_MenuInputConfig 0x6382f0) or editing
# text (X_MenuInputField 0x637820) consumes the key first, so the controls screen can still bind W/A/S/D/Space/Q.
# The key rebind capture 0x4cd5c0 waits until Enter, NumEnter and mouse button 0 are released before it takes the next key
# (else the key that selected the item would bind itself). Space is a select key now, so the NumEnter check (DIK 0x9c, two
# sites) watches Space (DIK 0x39) instead; E is not a select key for that reason.
PATCHES += [
    {"addr": 0x635680 + vk - 8, "old": "08", "new": f"{idx:02x}", "when": lambda: True}
    for vk, idx in ((0x20, 1), (0x41, 2), (0x44, 4), (0x51, 0), (0x53, 5), (0x57, 3))
] + [
    {"addr": a, "old": "9c", "new": "39", "when": lambda: True} for a in (0x4cd61e, 0x4cd66d)
]
# Speed: loadJPG 0x10204200 clears each new image to (0, 0, 0, 0xff) with four byte stores per pixel (loop 0x1020437f, Ghidra and
# capstone agree), ~5% of the JPEG load phase. The same bytes as one dword store per pixel: mov dword [ecx-1], 0xff000000;
# add ecx, 4; dec edx; jne; then nops up to the original jmp 0x10204391. Registers and flags at the exit match (ecx past the end,
# edx 0, ZF 1). The function has setjmp/SEH, so a HOST_FUNCS entry cannot take it.
PATCHES += [
    {"addr": 0x1020437f, "old": "88410188018841ffc64102ff83c1044a75ee",
     "new": "c741ff000000ff" "83c104" "4a" "75f3" "9090909090", "when": lambda: True},
]


# Host functions that answer a guest entry (tools/recomp/lift.py emits `if (host(c)) return;` at the entry; host/fast.c).
# Each returns 0 for any case it does not cover, so the lifted original still runs there.
HOST_FUNCS = {
    # D3DX same-size format convert, driver FUN_1043a907 (under FUN_1043be51): loads each row to floats (0x10440268) and
    # stores it (0x1043f137) per pixel. Every level 0 upload of a 32-bit image to an X8R8G8B8 texture comes here (35% of
    # the level load, RN_PROF). The float round trip gives back the same bytes, so a mask is exact (RN_FASTCHECK).
    0x1043a907: "rn_d3dx_convert",
    # msvcrt _ftol: truncates ST0 to edx:eax, saving and restoring the FPU control word in lifted memory operations.
    0x10801f14: "rn_ftol",
    # libjpeg 6a inside grphmfc (every level texture is a JPG). Float IDCT on software x87 with a _ftol call per sample: 40% of
    # the load. Same arithmetic and rounding in host C (host/fast.c), the color convert and the row copy too. RN_FASTCHECK compares.
    # libjpeg decompress_onepass 0x1020a080 (jdcoefct): the host runs the lifted body and lets a helper thread do its IDCTs; nothing in the
    # function reads their output rows, and it waits for the helper before it returns (13% of the load off the game thread).
    0x1020a080: "rn_jpeg_onepass",
    0x10210920: "rn_jpeg_idct",
    0x1020d570: "rn_jpeg_ycc",
    0x102045e0: "rn_jpeg_row",
    # libjpeg decode_mcu (sequential Huffman, jdhuff) 0x1020b370: the next biggest sampled body of the JPEG phase after the IDCT. The
    # host decodes a whole MCU from bytes already in the source buffer and falls back to the lifted body for restarts, markers,
    # an empty buffer or a bad code. RN_FASTCHECK compares blocks, entropy state and source fields with the lifted run.
    0x1020b370: "rn_jpeg_huff",
    # G_Image::copyWithScale: the quicksave (F5) thumbnail, screen image -> small image, one areaSample (bilinear taps, x87 and
    # _ftol) per pixel. The lifted body stalls the game ~210 ms (RN_SAMPLE, rea/map/render.md). Same arithmetic in host C; the
    # globals 0x102169b0..0x102169e0 are written too. RN_FASTCHECK compares the image and the globals with the lifted body.
    0x10203be0: "rn_copy_with_scale",
    # e2mfc P_Map alpha from a grey map, caller FUN_10120370 (one x87 mul-add chain and a control-word switch per pixel, 10% of the load).
    0x10120e20: "rn_grey_alpha",
    # e2mfc 16x16 fixed-point bilinear scale (4 x87 fmul + fistp per pixel, 12% of the load). Only after its first-time guards.
    0x10120ee0: "rn_bilinear_scale",
    # grphmfc G_Image pixel buffer: operator new(count*4) + a per-pixel 00 00 00 ff fill loop (9% of the load).
    0x10203d20: "rn_new_pixels",
    # rlmfc R_Memory::copy: a forward byte copy by 32-byte MMX/x87/dword blocks, called for each JPEG source buffer
    0x10016730: "rn_memory_copy",
    # exe X_LevelDBImportMaterial import: upload and free each map right after it is decoded, not after all of them
    # (peak guest heap 1.25 GB in part0_level1 with the install's 4096x2048 maps). Changes the order of the uploads only.
    0x5dc360: "rn_ldb_import",
    # e2mfc P_Map::destruct -> releaseDeviceTexture 0x101209a0 -> driver readback 0x1042db00: a GPU_READ + pixel convert into an
    # image destruct frees next (41-59 ms on comic -> game). Skipped for that caller only (host/fast.c rn_map_readback).
    # The vtable slot holds the incremental-link thunk 0x10401b59 (jmp 0x1042db00), the lifted entry that holds the body.
    0x10401b59: "rn_map_readback",
    # msvcrt longjmp: a non-local return with no RtlUnwind, so it must set the state a catch continuation runs under.
    0x1081e40e: "rn_longjmp",
    # exe X_MenuMode::onKey: F1 opens the Cheats page (menu.txt id 6, tools/ras.py), every other key runs the original (host/cheats.c).
    0x42c020: "rn_cheats_key",
}


# Lifted entries the frontend cannot see, walked from here as roots (frontend.py).
# Driver: the returns of _setjmp3 that a longjmp comes back to. The driver alone imports longjmp
# (D3DX JPEG 0x1043c6cd, PNG 0x1043d09e and 0x10444934 in its image decoders, tried in turn by FUN_1043d885). grphmfc's error
# exit 0x102044f0 throws a C++ exception, so loadJPG's setjmp return 0x10204263 is never jumped to.
# msvcrt: the first and last handler of the math records sinh 0x1083aeaa, cosh 0x1083aeca, tanh 0x1083aeea, exp 0x1083af2e.
# The records are unaligned and these values appear once each, so the frontend's packed-table rule misses them; every regular
# argument went to "no lifted function". Only MSVCP60 imports one of them (exp); the exe imports none.
LIFT_SEEDS = {"e2_d3d8_driver_mfc.dll": (0x1043c6d2, 0x1043d0a3, 0x10444939),
              "msvcrt.dll": (0x1082c5b2, 0x1082c62a, 0x1082c60f, 0x1082c637, 0x1082c5d6, 0x1082c651, 0x1082c6b3, 0x1082c73b)}


# Host hooks that run before the instruction at an address, registers saved and reloaded around them. host/widescreen.c.
MID_HOOKS = {
    0x10114da5: "rn_ws_camera",         # e2mfc P_Camera::validate: wider tangents for the screen
    0x67a12d: "rn_ws_portal_first",     # getFirstSceneToRender: portal frustum as wide as the view
    0x67aeba: "rn_ws_portal_next",      # getSceneToRender: the same
    0x10108594: "rn_ws_sprite",         # e2mfc P_Sprite::executeAlways: fades cover the wide screen
    # exe MaxPayne_GameMode tick 0x44c1b0, right after OnKey returns for a typed key (vk at [ebp-0x28]; unused F-keys fall to its
    # default at 0x44c26b): F3 god, F4 all weapons, F6 noclip as console lines (host/cheats.c; menu.txt cannot carry player->).
    0x44c2a1: "rn_cheats_game_key",
}


def host_func(entry):
    return HOST_FUNCS.get(entry)


def active():
    return [p for p in PATCHES if p["when"]()]


def apply(img, base, size=None):
    """Patch a bytearray image mapped at base; patches of other images (outside base..base+size) are skipped."""
    for p in active():
        if size is not None and not base <= p["addr"] < base + size:
            continue
        o, old, new = p["addr"] - base, bytes.fromhex(p["old"]), bytes.fromhex(p["new"])
        if o >= len(img):   # .bss past the raw data: zero by the PE format; the loader writes it in guest memory
            if old.strip(b"\0"): raise SystemExit(f"PATCH {p['addr']:#x}: .bss starts as zero")
            continue
        if len(old) != len(new) or bytes(img[o:o + len(old)]) != old:
            raise SystemExit(f"PATCH {p['addr']:#x}: original bytes do not match")
        img[o:o + len(new)] = new


def c_table():
    """C arrays the loader applies to guest memory after it maps the sections."""
    ps = active()   # a trailing 0 keeps the arrays non-empty
    return (f"const uint32_t rn_patch_count = {len(ps)};\n"
            "const uint32_t rn_patch_addr[] = {" + "".join(f"0x{p['addr']:x}," for p in ps) + "0};\n"
            "const char *const rn_patch_hex[] = {" + "".join(f'"{p["new"]}",' for p in ps) + "0};\n")
