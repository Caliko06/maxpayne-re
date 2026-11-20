// Race bridge to the NFSU1-RE engine (127.0.0.1:5190). When the game opens TRACKS\TRACKB<n>.LZC (a race loads),
// this reads the race the menu chose from guest memory and hands it to the page (window.rnBridge, web/index.html,
// on only with ?bridge). Nothing in Speed.exe is changed: it is an observer. Every address is Speed.exe (PC).
// V = read from disassembly (objdump) and one race memory dump this session.
//   0x78a2f0  track number the race loads: sprintf("TRACKS\TRACKB%d.LZC") at 0x4497be.
//   0x735dc8  current career EventDesc id (0 = none): written at 0x4c4f0d from the selected menu item's first word.
//   FEPlayerCarDB at 0x758c28 (the `this` of 13 call sites), same layout as the PS2 one (0x1db068):
//     +0 current car handle (+4 when [0x777b4c] == 8), count +0x61d8, cars at +0x2038 stride 0x348, handle at car+0x344,
//     car+8 = FEHashUpper(car name), car+0x100 + 4*i = part hash of slot i (120), car+0x2e0 = PhysicsUpgradeSpecification
//     (UpgradeLevel[11], then JunkmanPart[11] at +0x2c, 0x58 bytes).
//   0x734588  car record table, 0xc90 per car, name at +0.
#include "host.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <ctype.h>
#include <strings.h>

#define A_TRACK   0x78a2f0u
#define A_CAREER  0x735dc8u
#define A_FEMODE  0x777b4cu
#define A_PLAYERS 0x758c28u
#define A_CARTAB  0x734588u

extern Ctx *g_ctx;
static uint32_t gw(uint32_t a) { return ld(g_ctx, a, 4); }

static uint32_t fe_hash_upper(const char *s) {
    uint32_t h = 0xffffffffu;
    for (; *s; s++) h = h * 33 + (uint32_t)toupper((unsigned char)*s);
    return h;
}

static int has_ci(const char *s, const char *sub) {
    for (size_t n = strlen(sub); *s; s++) if (!strncasecmp(s, sub, n)) return 1;
    return 0;
}

// The player's car as a garage.txt (NFSU1-RE src/ug1_garage.h); name gets the car's name. 0 = not found.
static int player_garage(char *name, char *txt, int cap) {
    uint32_t db = A_PLAYERS, tab = gw(A_CARTAB);
    uint32_t cur = gw(db + (gw(A_FEMODE) == 8 ? 4 : 0));
    uint32_t n = gw(db + 0x61d8);
    for (uint32_t i = 0; i < n && i < 20; i++) {
        uint32_t e = db + 0x2038 + i * 0x348;
        if (gw(e + 0x344) != cur) continue;
        uint32_t want = gw(e + 8);
        for (uint32_t k = 0; k < 64; k++) {      // the table holds 35 cars, then other data
            char nm[0x40];
            for (int j = 0; j < 0x3f; j++) nm[j] = (char)ld(g_ctx, tab + k * 0xc90 + j, 1);
            nm[0x3f] = 0;
            if (!nm[0] || (unsigned char)nm[0] > 0x7e) break;
            if (fe_hash_upper(nm) != want) continue;
            snprintf(name, 32, "%s", nm);
            int o = snprintf(txt, cap, "car %s\n", nm);
            for (uint32_t p = 0; p < 11; p++) {
                uint32_t lv = gw(e + 0x2e0 + 4 * p), jk = gw(e + 0x2e0 + 0x2c + 4 * p);
                if (lv && o < cap) o += snprintf(txt + o, cap - o, "perf %u %u\n", p, lv);
                if (jk && o < cap) o += snprintf(txt + o, cap - o, "junk %u\n", p);
            }
            for (uint32_t p = 0; p < 120; p++) {
                uint32_t h = gw(e + 0x100 + 4 * p);
                if (h && o < cap) o += snprintf(txt + o, cap - o, "part %u %x\n", p, h);
            }
            return 1;
        }
    }
    return 0;
}

// Called by gf_open with every game path. Hands the race to window.rnBridge once.
void rn_race_bridge(const char *gpath) {
    static int done;
    if (done || !g_ctx || !has_ci(gpath, "TRACKB") || !has_ci(gpath, ".LZC")) return;
    uint32_t track = gw(A_TRACK), career = gw(A_CAREER);
    char car[32], garage[4096];
    if (track < 1000 || track > 1999 || !player_garage(car, garage, sizeof garage)) return;
    // ';' for newline: the garage travels in a URL
    for (char *p = garage; *p; p++) if (*p == '\n') *p = ';';
    char q[4400];
    int o = snprintf(q, sizeof q, "event=%u&car=%s", track, car);
    if (career) o += snprintf(q + o, sizeof q - o, "&career=%u", career);    // 5190 reads any career= as a career race
    snprintf(q + o, sizeof q - o, "&garage=%s", garage);
    if (MAIN_THREAD_EM_ASM_INT({ return window.rnBridge ? window.rnBridge(UTF8ToString($0)) : 0; }, q)) {
        done = 1;
        rn_log("bridge: race %u career %u car %s handed to the page", track, career, car);
    }
}
#else
void rn_race_bridge(const char *gpath) { (void)gpath; }
#endif
