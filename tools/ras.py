#!/usr/bin/env python3
"""Max Payne RAS archives (Remedy RL): list and extract, as rlmfc.dll reads them. A test oracle for the port.

Format (REA on rlmfc.dll, rea/c/rlmfc.dll): R_File::loadArchiveHeader 0x100103a0 / FUN_1001088b, R_File::load 0x10011e20,
R_File::decryptWithSeed 0x100134a0, R_File::decompress 0x100138e0, R_File::CRC32 0x10013a40.
- Header 0x2c bytes: magic, seed, then 0x24 bytes encrypted with that seed: file count, dir count, file table size,
  dir table size, version (float 1.2), header CRC (computed with itself zeroed), file table CRC, dir table CRC, (unused).
- File table (encrypted, same seed): per file "name\\0" + 0x28 bytes: original size, stored size, ?, dir index, seed,
  method (1 LZSS, 2 encrypted + LZSS, 3 stored), 16 bytes (time). Data follow the tables in table order.
- A stored LZSS blob: 12-byte header (+8 = length of the code that follows), then Okumura LZSS (N 4096, F 18, fill 0x20).
write() builds a stored-only archive (cipher inverse, CRCs as read above); cheats_archive() makes a_cheats.ras for tools/serve.py.
Usage: python3 tools/ras.py <archive.ras> [list | extract <out dir> [suffix]]   |   python3 tools/ras.py <game dir> cheats <out.ras>
"""
import os, struct, sys


def _key(n, seed):
    """Per byte i of the cipher (R_File::decryptWithSeed 0x100134a0): (i % 5, (i + 3) * 6 & 0xff, s & 0xff)."""
    s = seed or 1
    for i in range(n):
        # s = s * 0xab + (s / 0xb1) * -0x763d, 32-bit signed, division truncating toward zero
        q = int(s / 0xb1) if s >= 0 else -int(-s / 0xb1)
        s = (s * 0xab + q * -0x763d) & 0xffffffff
        if s >= 1 << 31:
            s -= 1 << 32
        yield i % 5, ((i + 3) * 6) & 0xff, s & 0xff


def decrypt(buf, seed):
    out = bytearray(len(buf))
    for i, (b, (r, k, a)) in enumerate(zip(buf, _key(len(buf), seed))):
        b = ((b << r) | (b >> (8 - r))) & 0xff
        out[i] = ((k ^ b) + a) & 0xff
    return bytes(out)


def encrypt(buf, seed):
    """The inverse of decrypt: un-add, un-xor, rotate right."""
    out = bytearray(len(buf))
    for i, (o, (r, k, a)) in enumerate(zip(buf, _key(len(buf), seed))):
        b = ((o - a) & 0xff) ^ k
        out[i] = ((b >> r) | (b << (8 - r))) & 0xff
    return bytes(out)


def lzss(blob, size):
    n = struct.unpack_from("<I", blob, 8)[0]
    src, end = 12, 12 + n
    text = bytearray(b" " * 4096)
    r, out, flags = 0xfee, bytearray(), 0
    while True:
        flags >>= 1
        if not flags & 0x100:
            if src >= end:
                break
            flags = blob[src] | 0xff00
            src += 1
        if flags & 1:
            if src >= end:
                break
            c = blob[src]; src += 1
            out.append(c); text[r] = c; r = (r + 1) & 0xfff
        else:
            if src + 1 >= end:
                break
            i, j = blob[src], blob[src + 1]
            src += 2
            i |= (j & 0xf0) << 4
            for k in range((j & 0xf) + 3):
                c = text[(i + k) & 0xfff]
                out.append(c); text[r] = c; r = (r + 1) & 0xfff
    return bytes(out[:size]) if size else bytes(out)


def crc32(data):
    import zlib
    return zlib.crc32(data) & 0xffffffff


def read(path):
    f = open(path, "rb")
    hdr = f.read(0x2c)
    seed = struct.unpack_from("<i", hdr, 4)[0]
    h = decrypt(hdr[8:0x2c], seed)
    nfiles, ndirs, ftsize, dtsize = struct.unpack_from("<4I", h, 0)
    ver, = struct.unpack_from("<f", h, 16)
    ft = decrypt(f.read(ftsize), seed)
    files, p, off = [], 0, 0
    for _ in range(nfiles):
        e = ft.index(b"\0", p)
        name = ft[p:e].decode("latin-1")
        d = struct.unpack_from("<6I", ft, e + 1)
        files.append({"name": name, "size": d[0], "stored": d[1], "dir": d[3], "seed": d[4], "method": d[5], "rel": off})
        off += d[1]
        p = e + 0x29
    dt = decrypt(f.read(dtsize), seed)
    base = 0x2c + ftsize + dtsize
    for x in files:
        x["offset"] = base + x["rel"]
    return f, seed, ver, files, dt


def extract(f, x):
    f.seek(x["offset"])
    blob = f.read(x["stored"])
    if x["method"] == 2:
        blob = decrypt(blob, x["seed"])
    if x["method"] in (1, 2):
        return lzss(blob, x["size"])
    return blob[:x["size"]]


def write(entries, dirs, seed=0x1badf00d, time=bytes(16), flags=3):
    """A RAS archive, every file stored (method 3). entries: [(dir index, name, bytes)]; dirs: [b"\\data\\menu\\", ...].
    Layout read by R_File::loadArchiveHeader 0x100103a0 / FUN_1001088b; header CRC over the plain 0x2c bytes, its field zeroed."""
    ft, data, off = bytearray(), bytearray(), 0
    for d, name, blob in entries:
        ft += name.encode("latin-1") + b"\0" + struct.pack("<6I", len(blob), len(blob), 0, d, seed & 0xffffffff, 3) + time
        data += blob
    dt = b"".join(p + b"\0" + bytes(16) for p in dirs)
    head = struct.pack("<4If4I", len(entries), len(dirs), len(ft), len(dt), 1.2, 0, crc32(bytes(ft)), crc32(dt), flags)
    plain = b"RAS\0" + struct.pack("<i", seed) + head
    head = head[:20] + struct.pack("<I", crc32(plain)) + head[24:]
    return b"RAS\0" + struct.pack("<i", seed) + encrypt(head, seed) + encrypt(bytes(ft), seed) + encrypt(dt, seed) + bytes(data)


# ---- the Cheats page (rea/map/cheats_menu.md sections 6-7) ----------------------------------------------------------------------
# Levels from x_data's 36_levels.txt (LevelName -> section); startup_level and end_combat are not playable on their own.
LEVELS = [("Tutorial", "tutorial", "LNID_TUTORIAL"),
          ("Part I", [("part0_level1", "LNID_PART1_PROLOGUE")] + [(n, f"LNID_PART1_CHAPTER{i}") for i, n in enumerate(
              ("part1_level1", "part1_level1b", "part1_level2", "part1_level2b", "part1_level3", "part1_level3b", "part1_level4", "part1_level5", "part1_level6"), 1)]),
          ("Part II", [("part2_level0", "LNID_PART2_PROLOGUE")] + [(n, f"LNID_PART2_CHAPTER{i}") for i, n in enumerate(
              ("part2_level1", "part2_level2", "part2_level2b", "part2_level3", "part2_level4"), 1)]),
          ("Part III", [("part3_level1", "LNID_PART3_PROLOGUE")] + [(n, f"LNID_PART3_CHAPTER{i}") for i, n in enumerate(
              ("part3_level2", "part3_level2b", "part3_level3", "part3_level4", "part3_level5", "part3_level5b", "part3_level6", "part3_level7"), 1)])]
# 2_console.txt aliases whose receiver exists while the menu page is built. "player->..." (God, GetAllWeapons, NoClip) does not: the
# menu parser resolves the receiver at build time and the engine stops with "Receiver name 'player' couldn't be found" (2026-11-05).
CONSOLE = [("Bullet time", "maxpayne_gamemode->GM_SetPlayerControlledSlowMotion(1);")]
JUMP = ["maxpayne_graphicnovelmode->mpgnm_removeallnotes();", "maxpayne_gamemode->gm_ResetShootDodgeBulletTimeCounters();",
        "maxpayne_gamemode->gm_resetlevelitems();", 'maxpayne_gamemode->gm_setgamelevel( "normal" );',
        "maxpayne_gamemode->gm_resetperformancemeasurements();", "maxpayne_gamemode->gm_init( %s );", "x_modeswitch->s_modeswitch( game );"]


def _item(name, msgs, tab):
    t = "\t" * tab
    return (f"{t}[Item]\n{t}{{\n{t}\t[General]\n{t}\tName = {name};\n{t}\tToolTip = \"\";\n{t}\tID = MENUID_ITEM;\n" +
            "".join(f'{t}\t[Message] String = "{m}";\n' for m in msgs) + f"{t}}}\n")


def _page(name, head, body, tab, pid=None):
    t = "\t" * tab
    return (f'{t}[Menu]\n{t}{{\n{t}\t[Properties]\n{t}\tName = "{name}";\n{t}\tToolTip = "";\n{t}\tHeadline = "{head}";\n' +
            (f"{t}\tid = {pid};\n" if pid else "") + body +
            f'{t}\t[Back]\n{t}\tName = GCID_BACK;\n{t}\tToolTip = GCID_BACK_TIP;\n{t}\tID = 0;\n{t}}}\n')


def cheats_menu(text):
    """x_data's menu.txt (\\data\\menu\\, CRLF) with a Cheats page before Credits (id 6: host/cheats.c finds it by that id), and the
    difficulty levels unlocked at every menu start (gm_enablegamelevel with an empty first argument writes the registry key, 0x457380)."""
    t = text.replace("\r\n", "\n")
    anchor = "\t[Menu]\n\t{\n\t\t[Properties]\n\t\tName = GCID_CREDITS;"
    on_init = '\t[Message] String = "maxpayne_gamemode->gm_enablegamelevel( \\"\\", "normal" );";\n'
    assert t.count(anchor) == 1 and t.count(on_init) == 1
    body = _item("LNID_TUTORIAL", [m % "tutorial" if "%s" in m else m for m in JUMP], 2)
    for part, lv in LEVELS[1:]:
        body += _page(part, part, "".join(_item(n, [m % lvl if "%s" in m else m for m in JUMP], 3) for lvl, n in lv), 2)
    body += "".join(_item(f'"{n}"', [m], 2) for n, m in CONSOLE)
    t = t.replace(on_init, on_init + "".join(on_init.replace("normal", m) for m in ("nightmare", "hell", "timedmode")))
    return t.replace(anchor, _page("Cheats", "Cheats", body, 1, 6) + "\n" + anchor).replace("\n", "\r\n").encode("latin-1")


def cheats_archive(game):
    """a_cheats.ras: just the patched menu.txt. It sorts before x_data.ras, and a loose or earlier archive wins a path (rea/map/ras_overlay.md)."""
    f, seed, ver, files, dt = read(os.path.join(game, "x_data.ras"))
    menu = next(x for x in files if x["name"].lower() == "menu.txt" and x["dir"] == 349)
    return write([(2, "menu.txt", cheats_menu(extract(f, menu).decode("latin-1")))], [b"\\", b"\\data\\", b"\\data\\menu\\"])


if __name__ == "__main__":
    if len(sys.argv) > 3 and sys.argv[2] == "cheats":      # ras.py <game dir> cheats <out.ras>
        open(sys.argv[3], "wb").write(cheats_archive(sys.argv[1]))
        sys.exit(0)
    f, seed, ver, files, dt = read(sys.argv[1])
    cmd = sys.argv[2] if len(sys.argv) > 2 else "list"
    print(f"{sys.argv[1]}: version {ver:.1f}, {len(files)} files", file=sys.stderr)
    if cmd == "list":
        for x in files:
            print(f"{x['offset']}\t{x['stored']}\t{x['size']}\tm{x['method']}\td{x['dir']}\t{x['name']}")
    elif cmd == "extract":
        out, suffix = sys.argv[3], (sys.argv[4] if len(sys.argv) > 4 else "").lower()
        os.makedirs(out, exist_ok=True)
        for x in files:
            if suffix and not x["name"].lower().endswith(suffix):
                continue
            open(os.path.join(out, f"{x['dir']}_{x['name']}"), "wb").write(extract(f, x))
