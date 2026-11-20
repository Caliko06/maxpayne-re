// gdi32 and the user32 DC/colour functions, as documented by Microsoft. One display: the desktop of
// rn_screen_w x rn_screen_h, 32 bits per pixel, 96 DPI, Windows XP classic colours.
// Handles: DCs 0x50000 + slot, system colour brushes 0x60000 + index.
#include "host.h"

#define DC_BASE 0x50000u
#define NDC 64
static struct { int used; uint32_t hwnd; } dcs[NDC];

static uint32_t GetDC_(Ctx *c) {
    for (uint32_t k = 0; k < NDC; k++)
        if (!dcs[k].used) { dcs[k].used = 1; dcs[k].hwnd = ARG(0); return DC_BASE + k; }
    rn_halt(c, "GetDC: no free DC");
    return 0;
}
static uint32_t ReleaseDC_(Ctx *c) {
    uint32_t k = ARG(1) - DC_BASE;
    if (k >= NDC || !dcs[k].used) return 0;
    dcs[k].used = 0;
    return 1;
}
// GetDeviceCaps(hdc, index) of the display.
static uint32_t GetDeviceCaps_(Ctx *c) {
    switch (ARG(1)) {
    case 0: return 0x400;                       // DRIVERVERSION
    case 2: return 1;                           // TECHNOLOGY: DT_RASDISPLAY
    case 4: return rn_screen_w * 254 / 960;     // HORZSIZE (mm at 96 DPI)
    case 6: return rn_screen_h * 254 / 960;     // VERTSIZE
    case 8: return rn_screen_w;                 // HORZRES
    case 10: return rn_screen_h;                // VERTRES
    case 12: return 32;                         // BITSPIXEL
    case 14: return 1;                          // PLANES
    case 24: return 0xffffffffu;                // NUMCOLORS: more than 256 colours
    case 38: return 0x7e99;                     // RASTERCAPS: bitblt, bitmap64, dibs, stretch, no palette
    case 88: case 90: return 96;                // LOGPIXELSX, LOGPIXELSY
    case 104: case 106: return 0;               // SIZEPALETTE, NUMRESERVED
    case 108: return 24;                        // COLORRES
    case 116: return 60;                        // VREFRESH
    case 117: return rn_screen_h;               // DESKTOPVERTRES
    case 118: return rn_screen_w;               // DESKTOPHORZRES
    }
    rn_halt(c, "GetDeviceCaps(%u)", ARG(1));
    return 0;
}

// Windows XP "Windows Classic" scheme, COLOR_SCROLLBAR (0) .. COLOR_MENUBAR (30), as 0x00bbggrr.
static const uint32_t sys_colors[31] = {
    0xc8d0d4, 0xa56e3a, 0x6a240a, 0x808080, 0xc8d0d4, 0xffffff, 0x000000, 0x000000, 0x000000, 0xffffff,
    0xc8d0d4, 0xc8d0d4, 0x808080, 0x6a240a, 0xffffff, 0xc8d0d4, 0x808080, 0x808080, 0x000000, 0xc8d0d4,
    0xffffff, 0x404040, 0xc8d0d4, 0x000000, 0xe1ffff, 0xb5b5b5, 0x800000, 0xf0caa6, 0xc0c0c0, 0x6a240a,
    0xc8d0d4,
};
static uint32_t GetSysColor_(Ctx *c) { return ARG(0) < 31 ? sys_colors[ARG(0)] : 0; }
static uint32_t GetSysColorBrush_(Ctx *c) { return ARG(0) < 31 ? 0x60000u + ARG(0) : 0; }

#define U(name, n) {"user32.dll", #name, n, name##_}
#define GD(name, n) {"gdi32.dll", #name, n, name##_}
const Api gdi32_apis[] = {
    U(GetDC, 1), {"user32.dll", "GetWindowDC", 1, GetDC_}, U(ReleaseDC, 2), U(GetSysColor, 1), U(GetSysColorBrush, 1),
    GD(GetDeviceCaps, 2),
};
const int gdi32_api_count = sizeof gdi32_apis / sizeof gdi32_apis[0];
