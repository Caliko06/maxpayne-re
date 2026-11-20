"""The game's bitmap fonts -> an RGBA atlas PNG + glyph metrics JSON, for the launcher's title screen.

There is no .fnt/.dat/.txt with glyph metrics in the install (checked 2026-11-07 across every *.ras: data/menu/bitmaps holds
only the five TGA strips). The metrics are inside the strip, and P_Font::validate (e2mfc.dll 0x10001e60, 2467 bytes) reads
them back out: it walks the image, cuts every glyph out and repacks all of them into its own atlas, then stores per-glyph uv and
width. The cut is driven by the FIRST ROW of the strip: the exporter drew one dash above every glyph, and the dash runs from the
glyph's left edge to its right edge. Row 0 of menu_font_univers is exactly that: 224 dashes, one per code point 0x20..0xFF, with
a 1-px dashed margin at x<63 that is not part of any glyph. Cutting on ink columns instead (the obvious way) is wrong: '@' and
'A' touch in this strip and '…' is three separate dots, so the count stops matching the code points.

Findings re-measured on every run:
  * one row per strip: menu_font_univers 2781x27, verysmall 2512x19, 24 bpp TGA type 2, uncompressed, no TTF anywhere
  * 224 code points in CP1252 order, 0x20..0xFF; NBSP 0xA0 has an empty cell; space 0x20's cell holds the missing-glyph square
  * the glyph advance is the distance between consecutive row-0 dashes: the exporter packed the cells with the font's own
    advances, which is why the ink gaps are not constant
  * Spanish accents are all real glyphs, see the report it prints

Usage:  python3 tools/font_atlas.py [game dir]      ->  build/launcher/font/ (outside git)
"""
import json, os, struct, sys, zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import launcher_assets as la

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "build", "launcher", "font")
FONTS = [                                                        # menu.txt [General], in the game's own order
    ("menu_font_univers", "data/menu/bitmaps/menu_font_univers.tga",
     "data/menu/bitmaps/menu_font_univers_alpha.tga", "NormalFont: an unselected menu line"),
    ("menu_font_univers_bright", "data/menu/bitmaps/menu_font_univers_bright.tga",
     "data/menu/bitmaps/menu_font_univers_alpha.tga", "SelectedFont: the line under the cursor"),
    ("menu_font_univers_verysmall", "data/menu/bitmaps/menu_font_univers_verysmall.tga",
     "data/menu/bitmaps/menu_font_univers_verysmall_alpha.tga", "ToolTipFont: the bottom-left tooltip"),
]
KEY = 60            # row-0 dash: the exporter drew it at full brightness, the margin dithering stays dark


# ---- TGA / PNG -----------------------------------------------------------------------------------------------------------
def tga_read(b):
    """(w, h, channels, rows) of an uncompressed or RLE 24/32-bpp TGA, top row first."""
    idlen, cmap, kind = b[0], b[1], b[2]
    if cmap or kind not in (2, 3, 10, 11):
        raise ValueError("unsupported TGA type %d" % kind)
    w, h, bpp, desc = struct.unpack_from("<HHBB", b, 12)
    n, p = bpp // 8, 18 + idlen
    if kind >= 10:
        out = bytearray()
        while len(out) < w * h * n:
            c = b[p]; p += 1
            if c & 0x80:
                out += b[p:p + n] * ((c & 0x7f) + 1); p += n
            else:
                out += b[p:p + (c + 1) * n]; p += (c + 1) * n
        px = bytes(out[:w * h * n])
    else:
        px = b[p:p + w * h * n]
    if n >= 3:                                   # BGR(A) -> RGB(A)
        a = bytearray(px)
        a[0::n], a[2::n] = px[2::n], px[0::n]
        px = bytes(a)
    rows = [px[y * w * n:(y + 1) * w * n] for y in range(h)]
    if not desc & 0x20:                          # origin bottom-left (the usual TGA): flip
        rows.reverse()
    return w, h, n, rows


def gray(w, h, n, rows):
    """one 0..255 luminance byte per pixel, row-major"""
    out = bytearray(w * h)
    for y in range(h):
        r = rows[y]
        for x in range(w):
            i, v = y * w + x, r[x * n:x * n + 3]
            out[i] = (v[0] * 299 + v[1] * 587 + v[2] * 114) // 1000 if n >= 3 else v[0]
    return out


def png_rgba(w, h, rgba):
    raw = b"".join(b"\0" + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


# ---- cells ---------------------------------------------------------------------------------------------------------------
def runs_of(seq, on):
    out, s = [], None
    for i, v in enumerate(seq):
        if on(v) and s is None:
            s = i
        elif not on(v) and s is not None:
            out.append((s, i - 1)); s = None
    if s is not None:
        out.append((s, len(seq) - 1))
    return out


def cells_of(key_row):
    """the per-glyph cells of a strip, from its key row: (x, w) per code point, plus the advance deltas.

    The 1-px dither in the left margin also lights pixels of row 0; it is single-pixel runs one pixel apart, so a run of
    width 1 followed by a 1-px gap is dropped.
    """
    rr = runs_of(key_row, lambda v: v > KEY)
    start = 0                                           # both strips open with one-pixel margin dithers, 2 px apart
    while start + 1 < len(rr) and rr[start][1] == rr[start][0] and rr[start + 1][0] - rr[start][0] <= 2:
        start += 1
    if start and rr[start][1] == rr[start][0] and rr[start][0] - rr[start - 1][0] <= 2:
        start += 1                                      # the small strip's dither chain ends on one more pixel
    rr = rr[start:]
    return [(a, b - a + 1) for a, b in rr], rr


def build(game, name, base_path, alpha_path, note):
    gf = la.GameFiles(game)
    sb, _ = gf.find(base_path)
    sa, _ = gf.find(alpha_path)
    if sb is None or sa is None:
        raise SystemExit("missing %s / %s" % (base_path, alpha_path))
    w, h, n, brows = tga_read(gf.raw(sb))
    wa, ha, na, arows = tga_read(gf.raw(sa))
    if (w, h) != (wa, ha):
        raise SystemExit("%s: base %dx%d but alpha %dx%d" % (name, w, h, wa, ha))
    base, alpha = gray(w, h, n, brows), gray(wa, ha, na, arows)

    key = [base[x] for x in range(w)]                    # row 0
    cells, allruns = cells_of(key)
    codes = list(range(0x20, 0x100))
    if len(cells) != len(codes):
        raise SystemExit("%s: %d cells in row 0 (%d runs) for %d code points"
                         % (name, len(cells), len(allruns), len(codes)))

    # The engine copies each glyph as a rectangle: x = the left end of its row-0 dash, width = the dash width + 1
    # (P_Font::validate stores `dash_columns + 1`). Rows 0-1 carry that dash (faint in the alpha twin) and are not glyph,
    # so a cell is rows 2..h-1 of its dash column plus one pixel. We keep that rectangle, which is what the game draws,
    # and only measure the ink inside it for the vertical frame.
    ink = []
    for cx, cw in cells:
        hi = min(w - 1, cx + cw)
        xs = [x for x in range(cx, hi + 1) if any(alpha[y * w + x] > 24 for y in range(1, h))]
        ys = [y for y in range(1, h) if any(alpha[y * w + x] > 24 for x in range(cx, hi + 1))]
        ink.append((min(xs), min(ys), max(xs) - min(xs) + 1, max(ys) - min(ys) + 1) if xs else (0, 0, 0, 0))

    # vertical frame: the bottom of 'H' is the baseline and its height the cap height (no overshoot, no descender)
    H = ink[codes.index(0x48)]
    baseline = H[1] + H[3] - 1
    cap = H[3]
    xh = ink[codes.index(0x78)][3]                       # 'x': no ascender, no descender, no overshoot

    glyphs = {}
    for i, c in enumerate(codes):
        cx, cw = cells[i]
        adv = cells[i + 1][0] - cx if i + 1 < len(cells) else cw + 2
        glyphs[c] = {"x": cx, "y": 2, "w": cw + 1, "h": h - 2, "advance": adv, "ox": 0, "oy": 2 - baseline}
    # space (0x20) and NBSP (0xA0) are never drawn: their cell holds the missing-glyph square, and the game skips them.
    # The advance of 0x20 is ALSO not the strip's cell delta: measured on rea/m3_menu_native.png, "New Game" puts
    # 16.9 game px between the ink of 'w' and 'G', and the atlas runs ~1.12x the game's size, so ~19 atlas px between the
    # pen positions, minus the 'w' advance from the dashes -> about one 'n' wide. See the report's calibration note.
    sp = glyphs[0x6E]["advance"]
    glyphs[0x20] = {"x": 0, "y": 0, "w": 0, "h": 0, "advance": sp, "ox": 0, "oy": 0}
    glyphs[0xA0] = dict(glyphs[0x20])

    os.makedirs(OUT, exist_ok=True)
    rgba = bytearray(w * h * 4)
    for i in range(w * h):
        rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = base[i]
        rgba[i * 4 + 3] = alpha[i]
    png = name + ".png"
    open(os.path.join(OUT, png), "wb").write(png_rgba(w, h, rgba))

    meta = {
        "font": name,
        "note": note,
        "source": {"color": base_path, "alpha": alpha_path},
        "atlas": png, "atlas_w": w, "atlas_h": h, "bpp": n * 8,
        "encoding": "cp1252",
        "em": h, "baseline": baseline, "cap_height": cap, "x_height": xh,
        "space_advance": sp,
        "condense": 0.72,
        "condense_note": "the game draws its menu lines ~28% narrower than the atlas ink: on rea/m3_menu_native.png at "
                         "1.6x, New Game inks 100 px (62.5 game px) with a 33 px cap (20.6 game px), ratio 3.03; the "
                         "atlas inks it 97 px at a 23 px cap, ratio 4.22. Scale w and advance by this to match",
        "glyphs": {str(c): g for c, g in sorted(glyphs.items())},
    }
    open(os.path.join(OUT, name + ".json"), "w").write(json.dumps(meta, indent=1))
    return {"name": name, "w": w, "h": h, "bpp": n * 8, "glyphs": len(glyphs), "cap": cap, "baseline": baseline,
            "x_height": xh, "note": note, "png": png, "ink": ink, "codes": codes}


def main(game):
    print("%-30s %-10s %-4s %-7s %-4s %-5s %s" % ("font", "atlas", "bpp", "glyphs", "cap", "base", "role"))
    built = []
    for name, b, a, note in FONTS:
        r = build(game, name, b, a, note)
        built.append(r)
        print("%-30s %dx%-5d %-4d %-7d %-4d %-5d %s" % (r["name"], r["w"], r["h"], r["bpp"], r["glyphs"], r["cap"],
                                                       r["baseline"], note))
    big = [r for r in built if r["name"] == "menu_font_univers"][0]
    missing = [c for c, g in json.load(open(os.path.join(OUT, "menu_font_univers.json")))["glyphs"].items()
               if g["w"] and g["x"] == json.load(open(os.path.join(OUT, "menu_font_univers.json")))["glyphs"][str(0x7F)]["x"]]
    print("\nSpanish set on menu_font_univers: present (no shared missing-glyph cell)")
    for ch in "áéíóúñÁÉÍÓÚÑüÜ¿¡·" + chr(0x96) + chr(0x97) + chr(0x85):
        print("  U+%04X %s" % (ord(ch), ch))
    json.dump([{k: v for k, v in r.items() if k not in ("boxes", "codes")} for r in built],
              open(os.path.join(OUT, "fonts.json"), "w"), indent=1)
    print("\nwrote %s" % OUT)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/Games/maxpayne"))
