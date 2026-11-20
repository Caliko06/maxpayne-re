// kernel32/user32/advapi32/winmm... as documented by Microsoft. We present Windows XP SP2, one CPU,
// code page 1252, the game installed at C:\MaxPayne. Nothing here knows about the game.
#include <ctype.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "host.h"
#include "images.h"

#define ERROR_FILE_NOT_FOUND 2
#define ERROR_PATH_NOT_FOUND 3
#define ERROR_INVALID_HANDLE 6
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_NO_MORE_FILES 18
#define ERROR_ALREADY_EXISTS 183
#define INVALID_HANDLE 0xffffffffu

static const char *MODULE_PATH = "C:\\MaxPayne\\MaxPayne.exe";
static const char *GAME_DIR = "C:\\MaxPayne";
// The startup dialog (CMaxPayneStartupDialog, MFC) is skipped by the game's own switches, parsed at 0x40cb90:
// -nodialog clears 0x867afc; the exe 0x40b470 then skips the dialog when Video Settings are saved (0x407190).
static const char *COMMAND_ARGS = " -skipstartup -nodialog";
void win32_set_root(const char *dir) { files_set_root(dir); }

void set_error(Ctx *c, uint32_t e) { st(c, c->fs + 0x34, e, 4); }
static uint32_t gstrlen(Ctx *c, uint32_t p) { return (uint32_t)strnlen(G(p), c->memsz - p); }
static void put_str(Ctx *c, uint32_t dst, const char *s) { memcpy(G(dst), s, strlen(s) + 1); }

Telem rn_telem;
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EMSCRIPTEN_KEEPALIVE uint32_t rn_telem_addr(void) { return (uint32_t)(uintptr_t)&rn_telem; }
#endif

static struct timespec t0;
uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t ns = (int64_t)(ts.tv_sec - t0.tv_sec) * 1000000000 + (ts.tv_nsec - t0.tv_nsec);   // signed: nsec may be below t0's
    return (uint64_t)ns / 1000;
}

// ---- guest heap: boundary tags, size-class free lists, coalescing ----------------------------
// Block: [size|used] header (8 bytes: size, prev size), payload 16-aligned. Sizes in bytes.
static uint32_t brk_ptr, vtop, heap_top;
#define NBIN 64
static uint32_t bins[NBIN];     // free list heads (guest addresses of blocks)
#define HDR 16u
#define B_SIZE(b) ld(c, (b), 4)
#define B_PREV(b) ld(c, (b) + 4, 4)
#define B_NEXTF(b) ld(c, (b) + 8, 4)
#define B_PREVF(b) ld(c, (b) + 12, 4)
static int bin_of(uint32_t sz) {
    int b = 0;
    sz >>= 5;
    while (sz > 1 && b < NBIN - 1) { sz = sz * 3 / 4 > 1 ? sz * 3 / 4 : 1; b++; }
    return b;
}
static void unlink_free(Ctx *c, uint32_t b) {
    uint32_t n = B_NEXTF(b), p = B_PREVF(b), sz = B_SIZE(b) & ~1u;
    if (p) st(c, p + 8, n, 4); else bins[bin_of(sz)] = n;
    if (n) st(c, n + 12, p, 4);
}
static void push_free(Ctx *c, uint32_t b, uint32_t sz) {
    int k = bin_of(sz);
    st(c, b, sz, 4);
    st(c, b + 8, bins[k], 4);
    st(c, b + 12, 0, 4);
    if (bins[k]) st(c, bins[k] + 12, b, 4);
    bins[k] = b;
    if (b + sz < brk_ptr) st(c, b + sz + 4, sz, 4);
}
// The guest heap in boundary-tag order: totals and blocks in use by size, powers of two (the walk is O(blocks); on a failed
// allocation and with RN_SAMPLE).
void rn_heap_report(Ctx *c) {
    uint32_t used = 0, nused = 0, fr = 0, nfr = 0, big = 0, hn[32] = {0}, hb[32] = {0};
    for (uint32_t b = HEAP_LO, sz; b < brk_ptr && (sz = B_SIZE(b) & ~1u); b += sz)
        if (B_SIZE(b) & 1) { int k = 31 - __builtin_clz(sz); used += sz; nused++; hn[k]++; hb[k] += sz; }
        else { fr += sz; nfr++; if (sz > big) big = sz; }
    rn_log("heap: brk %08x used %u MB in %u blocks, free %u MB in %u (largest %u MB)", brk_ptr, used >> 20, nused, fr >> 20, nfr, big >> 20);
    for (int k = 0; k < 32; k++)
        if (hb[k] > (4u << 20)) rn_log("heap:   %8u..%-9u %6u blocks %5u MB", 1u << k, (2u << k) - 1, hn[k], hb[k] >> 20);
}
// RN_HEAPPEAK: one line each time the guest's span in use (brk + VirtualAlloc regions from the top) grows by 32 MB; it sizes
// GUEST_MEM (host.h). Web also prints the wasm heap top (guest block + host mallocs), which sizes INITIAL_MEMORY (build.py).
static void heap_peak(Ctx *c) {
    static int on = -1;
    static uint32_t mark;
    if (on < 0) on = getenv("RN_HEAPPEAK") != NULL;
    uint32_t need = brk_ptr + (c->memsz - vtop);
    if (!on || need < mark + (32u << 20)) return;
    mark = need;
#ifdef __EMSCRIPTEN__
    unsigned host = (unsigned)((uintptr_t)sbrk(0) >> 20);
#else
    unsigned host = 0;
#endif
    rn_log("heap peak: guest needs %u MB (brk %08x, virtual %u MB), wasm heap %u MB", need >> 20, brk_ptr, (c->memsz - vtop) >> 20, host);
}
static uint32_t heap_alloc(Ctx *c, uint32_t n) {
    uint32_t need = (n + HDR + 15) & ~15u;
    if (need < 32) need = 32;
    for (int k = bin_of(need); k < NBIN; k++) {
        for (uint32_t b = bins[k], tries = 0; b && tries < 64; b = B_NEXTF(b), tries++) {
            uint32_t sz = B_SIZE(b);
            if (sz < need) continue;
            unlink_free(c, b);
            if (sz - need >= 64) {
                push_free(c, b + need, sz - need);
                st(c, b + need + 4, need, 4);
                sz = need;
            }
            st(c, b, sz | 1, 4);
            return b + HDR;
        }
    }
    if (need < n || need >= vtop - brk_ptr) {   // an error like STOP: report the first few, with the heap's state
        static int fails;
        if (fails++ < 5) {
            fprintf(stderr, "heap: alloc of %u bytes failed: vtop %08x\n", n, vtop);
            rn_heap_report(c);
        }
        return 0;
    }
    uint32_t b = brk_ptr;
    st(c, b, need | 1, 4);
    st(c, b + 4, heap_top, 4);           // size of the previous block
    brk_ptr += need;
    heap_top = need;
    heap_peak(c);
    return b + HDR;
}
static uint32_t heap_size(Ctx *c, uint32_t p) { return (B_SIZE(p - HDR) & ~1u) - HDR; }
static void heap_free(Ctx *c, uint32_t p) {
    uint32_t b = p - HDR, sz = B_SIZE(b) & ~1u;
    uint32_t nx = b + sz;
    if (nx < brk_ptr && !(B_SIZE(nx) & 1)) { unlink_free(c, nx); sz += B_SIZE(nx); }
    uint32_t pv = B_PREV(b);
    if (b > HEAP_LO && pv && !(B_SIZE(b - pv) & 1)) { b -= pv; unlink_free(c, b); sz += pv; }
    if (b + sz == brk_ptr) { brk_ptr = b; heap_top = B_PREV(b); return; }
    push_free(c, b, sz);
}
void guest_free(uint32_t p) { if (p) heap_free(g_ctx, p); }           // of guest_alloc: the guest must hold no pointer to it
uint32_t guest_alloc(uint32_t size) {
    Ctx *c = g_ctx;
    uint32_t p = heap_alloc(c, size);
    if (p) memset(G(p), 0, size);
    return p;
}

// ---- VirtualAlloc: 64 KB granularity, top down from the end of guest memory --------------------
typedef struct { uint32_t base, size, commit, protect; } Region;
static Region regions[4096];
static int nregions;
static Region *region_at(uint32_t a) {
    for (int k = 0; k < nregions; k++)
        if (a >= regions[k].base && a < regions[k].base + regions[k].size) return &regions[k];
    return NULL;
}

// ---- handles ---------------------------------------------------------------------------------
static Handle handles[512];
uint32_t new_handle(int kind) {
    for (int k = 1; k < 512; k++)
        if (handles[k].kind == H_FREE) { memset(&handles[k], 0, sizeof handles[k]); handles[k].kind = kind; return 0x100 + 4 * k; }
    return 0;
}
Handle *get_handle(uint32_t h) {
    uint32_t k = (h - 0x100) / 4;
    if (h < 0x100 || (h & 3) || k >= 512 || handles[k].kind == H_FREE) return NULL;
    return &handles[k];
}

// =================================== kernel32 ==================================================
static uint32_t GetVersion_(Ctx *c) { (void)c; return 0x0a280105u; }    // 5.1 build 2600
static uint32_t GetVersionExA_(Ctx *c) {
    uint32_t p = ARG(0), sz = ld(c, p, 4);
    if (sz != 148 && sz != 156) { set_error(c, 122); return 0; }
    memset(G(p + 4), 0, sz - 4);
    st(c, p + 4, 5, 4); st(c, p + 8, 1, 4); st(c, p + 12, 2600, 4); st(c, p + 16, 2, 4);
    put_str(c, p + 20, "Service Pack 2");
    if (sz == 156) st(c, p + 148, 2, 2);
    return 1;
}
static uint16_t to_wide(uint8_t b);
static uint32_t GetCommandLineA_(Ctx *c) { (void)c; return STR_ADDR; }
// GetProcessVersion(pid): the subsystem version in the exe's PE header (major << 16 | minor); 0 = this process.
static uint32_t GetProcessVersion_(Ctx *c) {
    if (ARG(0) && ARG(0) != ld(c, c->fs + 0x20, 4)) { set_error(c, 87); return 0; }
    uint32_t opt = IMAGE_BASE + ld(c, IMAGE_BASE + 0x3c, 4) + 24;
    return ld(c, opt + 48, 2) << 16 | ld(c, opt + 50, 2);
}
static uint32_t GetCommandLineW_(Ctx *c) { (void)c; return STR_ADDR + 0x800; }   // UTF-16 copy (win32_init)
static uint32_t GetEnvironmentStrings_(Ctx *c) { (void)c; return STR_ADDR + 0x400; }
static uint32_t FreeEnvironmentStrings_(Ctx *c) { (void)c; return 1; }
static uint32_t GetEnvironmentStringsW_(Ctx *c) {
    uint32_t src = STR_ADDR + 0x400, n = 0;
    while (G(src)[n] || G(src)[n + 1]) n++;
    n += 2;
    uint32_t w = heap_alloc(c, 2 * n);
    for (uint32_t k = 0; k < n; k++) st(c, w + 2 * k, to_wide((uint8_t)G(src)[k]), 2);
    return w;
}
static uint32_t FreeEnvironmentStringsW_(Ctx *c) { heap_free(c, ARG(0)); return 1; }
static uint32_t GetModuleFileNameA_(Ctx *c) {
    uint32_t h = ARG(0), buf = ARG(1), n = ARG(2);
    char path[300];
    int img = h ? image_of(h) : 0;
    if (img < 0 || (h && rn_images[img].base != h)) { rn_halt(c, "GetModuleFileNameA of module %08x", h); return 0; }
    if (img == 0) snprintf(path, sizeof path, "%s", MODULE_PATH);
    else {
        snprintf(path, sizeof path, "%s\\%s", GAME_DIR, rn_images[img].file + strlen("build/img/"));
        if (rn_images[img].plugin) snprintf(path, sizeof path, "%s\\e2driver\\%s", GAME_DIR, rn_images[img].file + strlen("build/img/"));
    }
    uint32_t len = (uint32_t)strlen(path);
    if (!n) return 0;
    uint32_t w = len < n ? len : n - 1;
    memcpy(G(buf), path, w); G(buf)[w] = 0;
    return w;
}
static uint32_t GetStartupInfoA_(Ctx *c) {
    uint32_t p = ARG(0);
    memset(G(p), 0, 68);
    st(c, p, 68, 4);
    return 0;
}
static uint32_t GetStdHandle_(Ctx *c) { (void)c; return 0; }   // a GUI process has no standard handles
static uint32_t SetStdHandle_(Ctx *c) { (void)c; return 1; }
static uint32_t GetFileType_(Ctx *c) {
    Handle *h = get_handle(ARG(0));
    if (h && h->kind == H_FILE) return 1;     // FILE_TYPE_DISK
    set_error(c, ERROR_INVALID_HANDLE);
    return 0;
}
static uint32_t SetHandleCount_(Ctx *c) { return ARG(0); }
static uint32_t GetACP_(Ctx *c) { (void)c; return 1252; }
static uint32_t GetOEMCP_(Ctx *c) { (void)c; return 437; }
static uint32_t IsValidCodePage_(Ctx *c) { uint32_t cp = ARG(0); return cp == 1252 || cp == 437 || cp == 20127; }
static uint32_t GetCPInfo_(Ctx *c) {
    uint32_t cp = ARG(0), p = ARG(1);
    if (cp != 0 && cp != 1 && cp != 1252 && cp != 437) { set_error(c, 87); return 0; }
    memset(G(p), 0, 20);
    st(c, p, 1, 4);            // MaxCharSize
    st(c, p + 4, '?', 1);      // DefaultChar
    return 1;
}

// Code page 1252: bytes 0x80..0x9f are not Latin-1.
static const uint16_t cp1252_hi[32] = {
    0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
    0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178};
static uint16_t to_wide(uint8_t b) { return b >= 0x80 && b < 0xa0 ? cp1252_hi[b - 0x80] : b; }
static int to_narrow(uint16_t w) {
    if (w < 0x80 || (w >= 0xa0 && w < 0x100)) return w;
    for (int k = 0; k < 32; k++) if (cp1252_hi[k] == w) return 0x80 + k;
    return -1;
}
static uint32_t MultiByteToWideChar_(Ctx *c) {
    uint32_t src = ARG(2), n = ARG(3), dst = ARG(4), dn = ARG(5);
    if ((int32_t)n == -1) n = gstrlen(c, src) + 1;
    if (!dn) return n;
    if (dn < n) { set_error(c, 122); return 0; }
    for (uint32_t k = 0; k < n; k++) st(c, dst + 2 * k, to_wide((uint8_t)G(src)[k]), 2);
    return n;
}
static uint32_t WideCharToMultiByte_(Ctx *c) {
    uint32_t src = ARG(2), n = ARG(3), dst = ARG(4), dn = ARG(5), used = ARG(7);
    if ((int32_t)n == -1) { n = 0; while (ld(c, src + 2 * n, 2)) n++; n++; }
    if (used) st(c, used, 0, 4);
    if (!dn) return n;
    if (dn < n) { set_error(c, 122); return 0; }
    for (uint32_t k = 0; k < n; k++) {
        int b = to_narrow((uint16_t)ld(c, src + 2 * k, 2));
        if (b < 0) { b = '?'; if (used) st(c, used, 1, 4); }
        G(dst)[k] = (char)b;
    }
    return n;
}
// CT_CTYPE1 for UTF-16 code units below 0x100 (Latin-1); others are reported as 0.
static uint16_t ctype1(uint32_t w) {
    uint16_t t = 0;
    if (w >= 0x100) return 0;
    if (w < 0x20 || w == 0x7f || (w >= 0x80 && w < 0xa0)) t |= 0x20;
    if (w == ' ' || w == '\t' || (w >= 0x0a && w <= 0x0d) || w == 0xa0) t |= 0x08;
    if (w == ' ' || w == '\t' || w == 0xa0) t |= 0x40;
    if (w >= '0' && w <= '9') t |= 0x04 | 0x80;
    if ((w >= 'a' && w <= 'f') || (w >= 'A' && w <= 'F')) t |= 0x80;
    if ((w >= 'A' && w <= 'Z') || (w >= 0xc0 && w <= 0xde && w != 0xd7)) t |= 0x01 | 0x100;
    if ((w >= 'a' && w <= 'z') || (w >= 0xdf && w <= 0xff && w != 0xf7) || w == 0xaa || w == 0xb5 || w == 0xba) t |= 0x02 | 0x100;
    if (((w >= 0x21 && w <= 0x2f) || (w >= 0x3a && w <= 0x40) || (w >= 0x5b && w <= 0x60) || (w >= 0x7b && w <= 0x7e) ||
         (w >= 0xa1 && w <= 0xbf && w != 0xaa && w != 0xb5 && w != 0xba) || w == 0xd7 || w == 0xf7)) t |= 0x10;
    return t;
}
static uint32_t GetStringTypeW_(Ctx *c) {
    uint32_t type = ARG(0), src = ARG(1), n = ARG(2), out = ARG(3);
    if (type != 1) { rn_halt(c, "GetStringTypeW type %u", type); return 0; }
    if ((int32_t)n == -1) { n = 0; while (ld(c, src + 2 * n, 2)) n++; n++; }
    for (uint32_t k = 0; k < n; k++) st(c, out + 2 * k, ctype1(ld(c, src + 2 * k, 2)), 2);
    return 1;
}
static uint32_t GetStringTypeA_(Ctx *c) {
    uint32_t type = ARG(1), src = ARG(2), n = ARG(3), out = ARG(4);
    if (type != 1) { rn_halt(c, "GetStringTypeA type %u", type); return 0; }
    if ((int32_t)n == -1) n = gstrlen(c, src) + 1;
    for (uint32_t k = 0; k < n; k++) st(c, out + 2 * k, ctype1(to_wide((uint8_t)G(src)[k])), 2);
    return 1;
}
static uint32_t case_map(uint32_t w, uint32_t flags) {
    if (flags & 0x100) {   // LCMAP_LOWERCASE
        if ((w >= 'A' && w <= 'Z') || (w >= 0xc0 && w <= 0xde && w != 0xd7)) return w + 0x20;
        if (w == 0x160 || w == 0x152 || w == 0x17d) return w + 1;
        if (w == 0x178) return 0xff;
    } else if (flags & 0x200) {
        if ((w >= 'a' && w <= 'z') || (w >= 0xe0 && w <= 0xfe && w != 0xf7)) return w - 0x20;
        if (w == 0x161 || w == 0x153 || w == 0x17e) return w - 1;
        if (w == 0xff) return 0x178;
    }
    return w;
}
static uint32_t LCMapStringW_(Ctx *c) {
    uint32_t flags = ARG(1), src = ARG(2), n = ARG(3), dst = ARG(4), dn = ARG(5);
    if (flags & ~0x300u) { rn_halt(c, "LCMapStringW flags %08x", flags); return 0; }
    if ((int32_t)n == -1) { n = 0; while (ld(c, src + 2 * n, 2)) n++; n++; }
    if (!dn) return n;
    if (dn < n) { set_error(c, 122); return 0; }
    for (uint32_t k = 0; k < n; k++) st(c, dst + 2 * k, case_map(ld(c, src + 2 * k, 2), flags), 2);
    return n;
}
static uint32_t LCMapStringA_(Ctx *c) {
    uint32_t flags = ARG(1), src = ARG(2), n = ARG(3), dst = ARG(4), dn = ARG(5);
    if (flags & ~0x300u) { rn_halt(c, "LCMapStringA flags %08x", flags); return 0; }
    if ((int32_t)n == -1) n = gstrlen(c, src) + 1;
    if (!dn) return n;
    if (dn < n) { set_error(c, 122); return 0; }
    for (uint32_t k = 0; k < n; k++) {
        int b = to_narrow((uint16_t)case_map(to_wide((uint8_t)G(src)[k]), flags));
        G(dst)[k] = (char)(b < 0 ? G(src)[k] : b);
    }
    return n;
}
static int cmp_units(uint32_t a, uint32_t b, uint32_t flags) {
    if (flags & 1) { a = case_map(a, 0x100); b = case_map(b, 0x100); }   // NORM_IGNORECASE
    return a < b ? -1 : a > b;
}
static uint32_t CompareStringA_(Ctx *c) {
    uint32_t flags = ARG(1), s1 = ARG(2), n1 = ARG(3), s2 = ARG(4), n2 = ARG(5);
    if (flags & ~1u) { rn_halt(c, "CompareStringA flags %08x", flags); return 0; }
    if ((int32_t)n1 < 0) n1 = gstrlen(c, s1);
    if ((int32_t)n2 < 0) n2 = gstrlen(c, s2);
    for (uint32_t k = 0; k < n1 && k < n2; k++) {
        int r = cmp_units(to_wide((uint8_t)G(s1)[k]), to_wide((uint8_t)G(s2)[k]), flags);
        if (r) return r < 0 ? 1 : 3;
    }
    return n1 < n2 ? 1 : n1 > n2 ? 3 : 2;
}
static uint32_t CompareStringW_(Ctx *c) {
    uint32_t flags = ARG(1), s1 = ARG(2), n1 = ARG(3), s2 = ARG(4), n2 = ARG(5);
    if (flags & ~1u) { rn_halt(c, "CompareStringW flags %08x", flags); return 0; }
    if ((int32_t)n1 < 0) { n1 = 0; while (ld(c, s1 + 2 * n1, 2)) n1++; }
    if ((int32_t)n2 < 0) { n2 = 0; while (ld(c, s2 + 2 * n2, 2)) n2++; }
    for (uint32_t k = 0; k < n1 && k < n2; k++) {
        int r = cmp_units(ld(c, s1 + 2 * k, 2), ld(c, s2 + 2 * k, 2), flags);
        if (r) return r < 0 ? 1 : 3;
    }
    return n1 < n2 ? 1 : n1 > n2 ? 3 : 2;
}
// lstrcmp(i)A = CompareStringA(LOCALE_USER_DEFAULT, 0 or NORM_IGNORECASE, s1, -1, s2, -1) - 2.
static uint32_t lstrcmp_any(Ctx *c, uint32_t s1, uint32_t s2, uint32_t flags) {
    uint32_t n1 = gstrlen(c, s1), n2 = gstrlen(c, s2);
    for (uint32_t k = 0; k < n1 && k < n2; k++) {
        int r = cmp_units(to_wide((uint8_t)G(s1)[k]), to_wide((uint8_t)G(s2)[k]), flags);
        if (r) return (uint32_t)r;
    }
    return n1 < n2 ? 0xffffffffu : n1 > n2;
}
static uint32_t lstrcmpiA_(Ctx *c) { return lstrcmp_any(c, ARG(0), ARG(1), 1); }
static uint32_t lstrcmpA_(Ctx *c) { return lstrcmp_any(c, ARG(0), ARG(1), 0); }
// Locale 0x0409 (English, United States).
static const char *locale_info(uint32_t type) {
    switch (type & 0xffff) {
    case 0x0001: return "0409";            // LOCALE_ILANGUAGE
    case 0x0003: return "ENU";             // LOCALE_SABBREVLANGNAME
    case 0x0006: return "United States";   // LOCALE_SCOUNTRY
    case 0x0007: return "USA";             // LOCALE_SABBREVCTRYNAME
    case 0x1001: return "English";         // LOCALE_SENGLANGUAGE
    case 0x1002: return "United States";   // LOCALE_SENGCOUNTRY
    case 0x0005: return "1";               // LOCALE_ICOUNTRY
    case 0x000b: return "437";             // LOCALE_IDEFAULTCODEPAGE
    case 0x1004: return "1252";            // LOCALE_IDEFAULTANSICODEPAGE
    case 0x000e: return ".";               // LOCALE_SDECIMAL
    case 0x000f: return ",";               // LOCALE_STHOUSAND
    case 0x0014: return "$";               // LOCALE_SCURRENCY
    case 0x0016: return ".";               // LOCALE_SMONDECIMALSEP
    case 0x0017: return ",";               // LOCALE_SMONTHOUSANDSEP
    case 0x001d: return "/";               // LOCALE_SDATE
    case 0x001e: return ":";               // LOCALE_STIME
    case 0x001f: return "M/d/yyyy";        // LOCALE_SSHORTDATE
    }
    return NULL;
}
static uint32_t GetLocaleInfoA_(Ctx *c) {
    uint32_t type = ARG(1), buf = ARG(2), n = ARG(3);
    const char *s = locale_info(type);
    if (!s) { rn_halt(c, "GetLocaleInfoA type %08x", type); return 0; }
    uint32_t len = (uint32_t)strlen(s) + 1;
    if (!n) return len;
    if (n < len) { set_error(c, 122); return 0; }
    memcpy(G(buf), s, len);
    return len;
}
static uint32_t GetLocaleInfoW_(Ctx *c) {
    uint32_t type = ARG(1), buf = ARG(2), n = ARG(3);
    const char *s = locale_info(type);
    if (!s) { rn_halt(c, "GetLocaleInfoW type %08x", type); return 0; }
    uint32_t len = (uint32_t)strlen(s) + 1;
    if (!n) return len;
    if (n < len) { set_error(c, 122); return 0; }
    for (uint32_t k = 0; k < len; k++) st(c, buf + 2 * k, (uint8_t)s[k], 2);
    return len;
}
static uint32_t GetUserDefaultLCID_(Ctx *c) { (void)c; return 0x0409; }
static uint32_t IsValidLocale_(Ctx *c) { return ARG(0) == 0x0409; }

static int nbig;
static uint32_t big_min(void) { static int v = -1; if (v < 0) v = getenv("RN_BIGALLOC") ? atoi(getenv("RN_BIGALLOC")) : 0; return (uint32_t)v; }
static uint32_t HeapCreate_(Ctx *c) { (void)c; return new_handle(H_HEAP); }
static uint32_t HeapDestroy_(Ctx *c) { (void)c; return 1; }
static uint32_t HeapAlloc_(Ctx *c) {
    uint32_t flags = ARG(1), n = ARG(2);
    uint32_t p = heap_alloc(c, n ? n : 1);
    // RN_BIGALLOC=<bytes>: log the first 4096 allocations and frees of at least that size, with the guest stack (debug).
    if (big_min() && n >= big_min() && nbig++ < 4096) { rn_log("bigalloc %u -> %08x", n, p); rn_backtrace(c); }
    if (!p) { if (flags & 4) rn_halt(c, "HeapAlloc(%u) out of memory", n); return 0; }
    if (flags & 8) memset(G(p), 0, n);
    return p;
}
static uint32_t HeapFree_(Ctx *c) {
    if (ARG(2) && big_min() && heap_size(c, ARG(2)) >= big_min() && nbig++ < 4096) { rn_log("bigfree %08x", ARG(2)); rn_backtrace(c); }
    if (ARG(2)) heap_free(c, ARG(2));
    return 1;
}
static uint32_t HeapSize_(Ctx *c) { return heap_size(c, ARG(2)); }
static uint32_t HeapReAlloc_(Ctx *c) {
    uint32_t flags = ARG(1), p = ARG(2), n = ARG(3);
    uint32_t old = heap_size(c, p);
    if (n <= old) return p;
    if (flags & 0x10) return 0;              // HEAP_REALLOC_IN_PLACE_ONLY
    uint32_t q = heap_alloc(c, n);
    if (!q) return 0;
    memmove(G(q), G(p), old);
    if (flags & 8) memset(G(q + old), 0, n - old);
    heap_free(c, p);
    return q;
}
// Atoms: one table for GlobalAddAtom, RegisterWindowMessage and RegisterClipboardFormat (string atoms 0xc000..0xffff,
// names compared without case). "#123" and integers below 0xc000 are integer atoms and are not stored.
static char atoms[512][256];
static int atom_refs[512];
static uint32_t atom_add(Ctx *c, uint32_t p, int ref) {
    if (p < 0x10000) return p;
    const char *s = G(p);
    if (s[0] == '#') return (uint32_t)atoi(s + 1);
    for (int k = 0; k < 512; k++) if (atom_refs[k] && !strcasecmp(atoms[k], s)) { atom_refs[k] += ref; return 0xc000u + k; }
    if (!ref) return 0;
    for (int k = 0; k < 512; k++)
        if (!atom_refs[k]) { snprintf(atoms[k], sizeof atoms[k], "%s", s); atom_refs[k] = 1; return 0xc000u + k; }
    rn_halt(c, "atom table full"); return 0;
}
static uint32_t GlobalAddAtomA_(Ctx *c) { return atom_add(c, ARG(0), 1); }
static uint32_t GlobalFindAtomA_(Ctx *c) { return atom_add(c, ARG(0), 0); }
static uint32_t GlobalDeleteAtom_(Ctx *c) {
    uint32_t a = ARG(0) - 0xc000u;
    if (a < 512 && atom_refs[a] && !--atom_refs[a]) atoms[a][0] = 0;
    return 0;
}
static uint32_t GlobalGetAtomNameA_(Ctx *c) {
    uint32_t a = ARG(0), buf = ARG(1), n = ARG(2);
    char tmp[256];
    if (a < 0xc000u) snprintf(tmp, sizeof tmp, "#%u", a);
    else if (a - 0xc000u < 512 && atom_refs[a - 0xc000u]) snprintf(tmp, sizeof tmp, "%s", atoms[a - 0xc000u]);
    else return 0;
    if (!n) return 0;
    uint32_t len = (uint32_t)strlen(tmp) < n - 1 ? (uint32_t)strlen(tmp) : n - 1;
    memcpy(G(buf), tmp, len); G(buf)[len] = 0;
    return len;
}

// Global*/Local* memory: every block is fixed, so a handle is its own pointer (GMEM_MOVEABLE included:
// Lock returns the same address, Unlock and Flags report no lock count). GMEM_ZEROINIT = 0x40.
static uint32_t mem_alloc(Ctx *c, uint32_t flags, uint32_t n) {
    uint32_t p = heap_alloc(c, n ? n : 1);
    if (!p) { set_error(c, ERROR_NOT_ENOUGH_MEMORY); return 0; }
    if (flags & 0x40) memset(G(p), 0, n);
    return p;
}
static uint32_t mem_realloc(Ctx *c, uint32_t p, uint32_t n, uint32_t flags) {
    if (!p) return mem_alloc(c, flags, n);
    uint32_t old = heap_size(c, p);
    if (n <= old) return p;
    uint32_t q = heap_alloc(c, n);
    if (!q) { set_error(c, ERROR_NOT_ENOUGH_MEMORY); return 0; }
    memmove(G(q), G(p), old);
    if (flags & 0x40) memset(G(q + old), 0, n - old);
    heap_free(c, p);
    return q;
}
static uint32_t GlobalAlloc_(Ctx *c) { return mem_alloc(c, ARG(0), ARG(1)); }
static uint32_t GlobalReAlloc_(Ctx *c) { return mem_realloc(c, ARG(0), ARG(1), ARG(2)); }
static uint32_t GlobalFree_(Ctx *c) { if (ARG(0)) heap_free(c, ARG(0)); return 0; }
static uint32_t GlobalSize_(Ctx *c) { return ARG(0) ? heap_size(c, ARG(0)) : 0; }
static uint32_t GlobalLock_(Ctx *c) { return ARG(0); }
static uint32_t GlobalUnlock_(Ctx *c) { (void)c; set_error(c, 0); return 0; }     // not locked any more
static uint32_t GlobalHandle_(Ctx *c) { return ARG(0); }
static uint32_t GlobalFlags_(Ctx *c) { (void)c; return 0; }
// GlobalMemoryStatus(MEMORYSTATUS*): 512 MB of RAM, the guest's user address space (host.h GUEST_MEM); nothing is swapped.
static uint32_t GlobalMemoryStatus_(Ctx *c) {
    uint32_t p = ARG(0);
    uint32_t v[8] = {32, 50, 512u << 20, 256u << 20, 1024u << 20, 768u << 20, GUEST_MEM - LOW, vtop - brk_ptr};
    for (int k = 0; k < 8; k++) st(c, p + 4 * k, v[k], 4);
    return 0;
}

static uint32_t VirtualAlloc_(Ctx *c) {
    uint32_t addr = ARG(0), size = ARG(1), type = ARG(2), prot = ARG(3);
    if (!size) { set_error(c, 87); return 0; }
    if (addr) {
        Region *r = region_at(addr);
        if (r && (type & 0x1000) && size <= r->size - (addr - r->base)) {   // commit inside a reservation
            r->commit = 1;
            return addr & ~0xfffu;
        }
        set_error(c, 487);
        return 0;
    }
    uint32_t sz = (size + 0xffffu) & ~0xffffu;
    if (!sz || nregions == 4096 || sz >= vtop - brk_ptr) { set_error(c, ERROR_NOT_ENOUGH_MEMORY); return 0; }
    vtop -= sz;
    regions[nregions++] = (Region){vtop, sz, (type & 0x1000) != 0, prot};
    heap_peak(c);
    memset(G(vtop), 0, sz);
    return vtop;
}
uint32_t virtual_reserve(Ctx *c, uint32_t size) {
    if (!size) return 0;
    uint32_t sz = (size + 0xffffu) & ~0xffffu;
    if (!sz || nregions == 4096 || sz >= vtop - brk_ptr) return 0;
    vtop -= sz;
    regions[nregions++] = (Region){vtop, sz, 1, 4};
    heap_peak(c);
    memset(G(vtop), 0, sz);
    return vtop;
}
static uint32_t VirtualFree_(Ctx *c) {
    uint32_t addr = ARG(0), type = ARG(2);
    Region *r = region_at(addr);
    if (!r) { set_error(c, 487); return 0; }
    if (type & 0x8000) {        // MEM_RELEASE: the address space stays used (no reuse yet)
        r->commit = 0;
    }
    return 1;
}
static uint32_t VirtualQuery_(Ctx *c) {
    uint32_t addr = ARG(0), p = ARG(1), n = ARG(2);
    if (n < 28) return 0;
    Region *r = region_at(addr);
    uint32_t base = addr & ~0xfffu, abase = base, size = 0x1000, state = 0x1000, prot = 4, type = 0x20000;
    if (r) { abase = r->base; size = r->base + r->size - base; prot = r->protect; }
    else if (addr >= IMAGE_BASE && addr < HEAP_LO) { abase = IMAGE_BASE; type = 0x1000000; prot = 0x20; }
    else if (addr >= STACK_LO && addr < STACK_HI) { abase = STACK_LO; size = STACK_HI - base; }
    st(c, p, base, 4); st(c, p + 4, abase, 4); st(c, p + 8, prot, 4); st(c, p + 12, size, 4);
    st(c, p + 16, state, 4); st(c, p + 20, prot, 4); st(c, p + 24, type, 4);
    return 28;
}
static uint32_t VirtualProtect_(Ctx *c) { if (ARG(3)) st(c, ARG(3), 4, 4); return 1; }

static uint32_t TlsAlloc_(Ctx *c) {
    uint32_t bits = ld(c, PEB_ADDR + 0x44, 4);
    for (int k = 0; k < 32; k++)
        if (!(bits & (1u << k))) { st(c, PEB_ADDR + 0x44, bits | (1u << k), 4); st(c, c->fs + 0xe10 + 4 * k, 0, 4); return k; }
    return 0xffffffffu;
}
static uint32_t TlsFree_(Ctx *c) { uint32_t k = ARG(0); if (k >= 32) { set_error(c, 87); return 0; } st(c, PEB_ADDR + 0x44, ld(c, PEB_ADDR + 0x44, 4) & ~(1u << k), 4); return 1; }
static uint32_t TlsGetValue_(Ctx *c) { uint32_t k = ARG(0); set_error(c, 0); return k < 64 ? ld(c, c->fs + 0xe10 + 4 * k, 4) : 0; }
static uint32_t TlsSetValue_(Ctx *c) { uint32_t k = ARG(0); if (k >= 64) return 0; st(c, c->fs + 0xe10 + 4 * k, ARG(1), 4); return 1; }

// One thread runs at a time, so a critical section only counts recursion.
static uint32_t InterlockedExchange_(Ctx *c) { uint32_t p = ARG(0), old = ld(c, p, 4); st(c, p, ARG(1), 4); return old; }
// Guest threads run one at a time under the GIL, so these need no host atomics.
static uint32_t InterlockedIncrement_(Ctx *c) { uint32_t v = ld(c, ARG(0), 4) + 1; st(c, ARG(0), v, 4); return v; }
static uint32_t InterlockedDecrement_(Ctx *c) { uint32_t v = ld(c, ARG(0), 4) - 1; st(c, ARG(0), v, 4); return v; }
// MulDiv(a, b, c): a*b/c with a 64-bit intermediate, rounded half away from zero; -1 when c is 0.
static uint32_t MulDiv_(Ctx *c) {
    int64_t a = (int32_t)ARG(0), b = (int32_t)ARG(1), d = (int32_t)ARG(2);
    if (!d) return 0xffffffffu;
    int64_t p = a * b, q = (p < 0) != (d < 0) ? (p - d / 2) / d : (p + d / 2) / d;
    return q > INT32_MAX || q < INT32_MIN ? 0xffffffffu : (uint32_t)(int32_t)q;
}
// lstr*: the C string functions on guest memory. Code page 1252 has no DBCS lead bytes.
static uint32_t lstrlenA_(Ctx *c) { return ARG(0) ? gstrlen(c, ARG(0)) : 0; }
static uint32_t lstrlenW_(Ctx *c) { uint32_t p = ARG(0), n = 0; if (p) while (ld(c, p + 2 * n, 2)) n++; return n; }
static uint32_t lstrcpyA_(Ctx *c) { memmove(G(ARG(0)), G(ARG(1)), gstrlen(c, ARG(1)) + 1); return ARG(0); }
static uint32_t lstrcpyW_(Ctx *c) {
    uint32_t d = ARG(0), s = ARG(1), n = 0;
    do st(c, d + 2 * n, ld(c, s + 2 * n, 2), 2); while (ld(c, s + 2 * n++, 2));
    return d;
}
static uint32_t lstrcpynA_(Ctx *c) {
    uint32_t d = ARG(0), s = ARG(1), n = ARG(2);
    if (!n) return d;
    uint32_t len = gstrlen(c, s);
    if (len > n - 1) len = n - 1;
    memmove(G(d), G(s), len); G(d)[len] = 0;
    return d;
}
static uint32_t lstrcatA_(Ctx *c) { uint32_t d = ARG(0); memmove(G(d + gstrlen(c, d)), G(ARG(1)), gstrlen(c, ARG(1)) + 1); return d; }
static uint32_t IsDBCSLeadByte_(Ctx *c) { (void)c; return 0; }
static uint32_t CharNextA_(Ctx *c) { return ARG(0) + (G(ARG(0))[0] != 0); }
static uint32_t GetThreadLocale_(Ctx *c) { (void)c; return 0x0409; }   // = GetUserDefaultLCID

static uint32_t GetCurrentProcess_(Ctx *c) { (void)c; return 0xffffffffu; }
static uint32_t GetCurrentThread_(Ctx *c) { (void)c; return 0xfffffffeu; }
static uint32_t GetCurrentProcessId_(Ctx *c) { return ld(c, c->fs + 0x20, 4); }
static uint32_t GetLastError_(Ctx *c) { return ld(c, c->fs + 0x34, 4); }
static uint32_t SetLastError_(Ctx *c) { set_error(c, ARG(0)); return 0; }
static uint32_t SetUnhandledExceptionFilter_(Ctx *c) {
    static uint32_t prev;
    uint32_t old = prev;
    prev = ARG(0);
    (void)c;
    return old;
}
static uint32_t SetErrorMode_(Ctx *c) { static uint32_t mode; uint32_t old = mode; mode = ARG(0); (void)c; return old; }
static int bad_range(Ctx *c, uint32_t p, uint32_t n) { return p < LOW || p > c->memsz || n > c->memsz - p; }
static uint32_t IsBadReadPtr_(Ctx *c) { return ARG(1) && bad_range(c, ARG(0), ARG(1)); }
static uint32_t IsBadCodePtr_(Ctx *c) { uint32_t p = ARG(0); return !(p >= HOST_BASE || (p >= IMAGE_BASE && p < c->memsz)); }
static uint32_t GetSystemInfo_(Ctx *c) {
    uint32_t p = ARG(0);
    memset(G(p), 0, 36);
    st(c, p + 4, 4096, 4); st(c, p + 8, LOW, 4); st(c, p + 12, GUEST_MEM - LOW - 1, 4);
    st(c, p + 16, 1, 4); st(c, p + 20, 1, 4); st(c, p + 24, 586, 4); st(c, p + 28, 0x10000, 4);
    st(c, p + 32, 6, 2); st(c, p + 34, 0x0101, 2);
    return 0;
}
// Same CPU as rn_cpuid: CX8 and RDTSC, no MMX/SSE/3DNow.
static uint32_t IsProcessorFeaturePresent_(Ctx *c) { uint32_t f = ARG(0); return f == 2 || f == 8; }
static uint32_t GetTickCount_(Ctx *c) { (void)c; rn_telem.clock_calls++; return (uint32_t)(now_us() / 1000); }
static void reg_flush_due(uint64_t now);
static uint32_t QueryPerformanceCounter_(Ctx *c) {
    uint64_t now = now_us();
    rn_telem.clock_calls++;
    st64(c, ARG(0), now);
    reg_flush_due(now);
    return 1;
}
static uint32_t QueryPerformanceFrequency_(Ctx *c) { st64(c, ARG(0), 1000000); return 1; }
// The PC's clock runs on UTC: time zone "Greenwich Standard Time" (bias 0, no daylight saving),
// so local time = system time. GetTimeZoneInformation then answers TIME_ZONE_ID_UNKNOWN.
static uint32_t GetTimeZoneInformation_(Ctx *c) {
    uint32_t p = ARG(0);
    memset(G(p), 0, 172);
    const char *name = "Greenwich Standard Time";
    for (int k = 0; name[k]; k++) { st(c, p + 4 + 2 * k, (uint8_t)name[k], 2); st(c, p + 88 + 2 * k, (uint8_t)name[k], 2); }
    return 0;
}
static uint32_t GetSystemTimeAsFileTime_(Ctx *c) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    st64(c, ARG(0), (uint64_t)ts.tv_sec * 10000000ull + ts.tv_nsec / 100 + 116444736000000000ull);
    return 0;
}
// FILETIME (100 ns since 1601-01-01) <-> SYSTEMTIME {year, month, weekday, day, hour, minute, second, ms} (WORDs).
// Local time = UTC on this PC (GetTimeZoneInformation).
static void ft_to_st(Ctx *c, uint64_t ft, uint32_t p) {
    uint64_t ms = ft / 10000, days = ms / 86400000, rem = ms % 86400000;
    int64_t z = (int64_t)days - 134774 + 719468;                 // days since 0000-03-01 (civil_from_days)
    int64_t era = z / 146097, doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
    int64_t y = yoe + era * 400 + (m <= 2);
    uint32_t v[8] = {(uint32_t)y, (uint32_t)m, (uint32_t)((days + 1) % 7), (uint32_t)d, (uint32_t)(rem / 3600000),
                     (uint32_t)(rem / 60000 % 60), (uint32_t)(rem / 1000 % 60), (uint32_t)(rem % 1000)};
    for (int k = 0; k < 8; k++) st(c, p + 2 * k, v[k], 2);
}
static int st_to_ft(Ctx *c, uint32_t p, uint64_t *ft) {
    int64_t y = ld(c, p, 2), m = ld(c, p + 2, 2), d = ld(c, p + 6, 2);
    uint64_t h = ld(c, p + 8, 2), mi = ld(c, p + 10, 2), s = ld(c, p + 12, 2), ms = ld(c, p + 14, 2);
    if (y < 1601 || m < 1 || m > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 59 || ms > 999) return 0;
    y -= m <= 2;                                                 // days_from_civil
    int64_t era = y / 400, yoe = y - era * 400, doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t days = era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468 + 134774;
    *ft = (((uint64_t)days * 86400 + h * 3600 + mi * 60 + s) * 1000 + ms) * 10000;
    return 1;
}
static uint64_t now_ft(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 10000000ull + (uint64_t)ts.tv_nsec / 100 + 116444736000000000ull;
}
static uint32_t FileTimeToLocalFileTime_(Ctx *c) { st64(c, ARG(1), ld64(c, ARG(0))); return 1; }
static uint32_t LocalFileTimeToFileTime_(Ctx *c) { st64(c, ARG(1), ld64(c, ARG(0))); return 1; }
static uint32_t FileTimeToSystemTime_(Ctx *c) {
    uint64_t ft = ld64(c, ARG(0));
    if (ft >> 63) { set_error(c, 87); return 0; }
    ft_to_st(c, ft, ARG(1));
    return 1;
}
static uint32_t SystemTimeToFileTime_(Ctx *c) {
    uint64_t ft;
    if (!st_to_ft(c, ARG(0), &ft)) { set_error(c, 87); return 0; }
    st64(c, ARG(1), ft);
    return 1;
}
static uint32_t GetSystemTime_(Ctx *c) { ft_to_st(c, now_ft(), ARG(0)); return 0; }
static uint32_t GetLocalTime_(Ctx *c) { ft_to_st(c, now_ft(), ARG(0)); return 0; }
static uint32_t ExitProcess_(Ctx *c) { rn_halt(c, "ExitProcess(%u)", ARG(0)); return 0; }
static uint32_t OutputDebugStringA_(Ctx *c) { rn_log("debug: %s", G(ARG(0))); return 0; }

static uint32_t module_handle(const char *name) {
    static const char *known[] = {"kernel32.dll", "user32.dll", "gdi32.dll", "advapi32.dll", "winmm.dll", "d3d8.dll",
                                  "dinput8.dll", "dsound.dll", "ddraw.dll", "shell32.dll", "shfolder.dll", "ntdll.dll", NULL};
    char n[64];
    snprintf(n, sizeof n, "%s", name);
    if (!strchr(n, '.')) strncat(n, ".dll", sizeof n - strlen(n) - 1);
    for (int k = 0; known[k]; k++) if (!strcasecmp(n, known[k])) return 0x70000000u + 0x10000u * k;
    return 0;
}
static const char *module_name(uint32_t h) {
    static const char *known[] = {"kernel32.dll", "user32.dll", "gdi32.dll", "advapi32.dll", "winmm.dll", "d3d8.dll",
                                  "dinput8.dll", "dsound.dll", "ddraw.dll", "shell32.dll", "shfolder.dll", "ntdll.dll"};
    uint32_t k = (h - 0x70000000u) / 0x10000u;
    return h >= 0x70000000u && k < 12 && !(h & 0xffff) ? known[k] : NULL;
}
static uint32_t GetModuleHandleA_(Ctx *c) {
    uint32_t p = ARG(0);
    if (!p) return IMAGE_BASE;
    int img = image_by_name(G(p));
    if (img >= 0 && (!rn_images[img].plugin || image_loaded(img))) return rn_images[img].base;
    uint32_t h = module_handle(G(p));
    if (!h) { rn_log("GetModuleHandleA(%s) -> not loaded", G(p)); set_error(c, 126); }
    return h;
}
static uint32_t LoadLibraryA_(Ctx *c) {
    int img = image_by_name(G(ARG(0)));
    if (img >= 0) return image_load(c, img) ? rn_images[img].base : 0;
    uint32_t h = module_handle(G(ARG(0)));
    // Any other DLL is not on this PC (MFC42 probes system32\MFC42LOC.DLL, a localized resource DLL): NULL, ERROR_MOD_NOT_FOUND.
    if (!h) { rn_log("LoadLibraryA(%s) -> not found", G(ARG(0))); set_error(c, 126); }
    return h;
}
// GetComputerNameA(buffer, size*): the NetBIOS name; *size is in/out (characters without the terminator).
static uint32_t GetComputerNameA_(Ctx *c) {
    static const char name[] = "MAXPAYNE";
    uint32_t buf = ARG(0), np = ARG(1), n = ld(c, np, 4);
    if (n < sizeof name) { st(c, np, sizeof name, 4); set_error(c, 111); return 0; }   // ERROR_BUFFER_OVERFLOW
    memcpy(G(buf), name, sizeof name);
    st(c, np, sizeof name - 1, 4);
    return 1;
}
static uint32_t FreeLibrary_(Ctx *c) { (void)c; return 1; }          // guest images stay mapped
static uint32_t DisableThreadLibraryCalls_(Ctx *c) { (void)c; return 1; }
static uint32_t GetSystemDirectoryA_(Ctx *c) {
    static const char dir[] = "C:\\WINDOWS\\system32";
    uint32_t buf = ARG(0), n = ARG(1);
    if (n < sizeof dir) return sizeof dir;
    memcpy(G(buf), dir, sizeof dir);
    return sizeof dir - 1;
}
static uint32_t GetProcAddress_(Ctx *c) {
    uint32_t h = ARG(0), p = ARG(1);
    int img = image_of(h);
    if (img >= 0 && rn_images[img].base == h) {
        char ord[16];
        const char *name = p < 0x10000 ? (snprintf(ord, sizeof ord, "#%u", p), ord) : G(p);
        uint32_t va = image_proc(img, name);
        if (!va) set_error(c, 127);
        return va;
    }
    const char *dll = module_name(h);
    if (!dll) { rn_halt(c, "GetProcAddress on module %08x", h); return 0; }
    if (p < 0x10000) { rn_halt(c, "GetProcAddress(%s, #%u)", dll, p); return 0; }
    if (find_api(dll, G(p))) return host_addr_of(dll, G(p));
    rn_halt(c, "GetProcAddress(%s, %s): not in the layer", dll, G(p));
    return 0;
}

// ---- resources: the PE resource directory of a mapped image ------------------------------------
// A level entry is {name or id, offset}: bit 31 of the name = a counted UTF-16 string, bit 31 of the
// offset = a subdirectory. An id argument below 0x10000 is MAKEINTRESOURCE; "#123" is the same id.
static uint32_t res_module(Ctx *c, uint32_t h) {
    if (!h) return IMAGE_BASE;
    int img = image_of(h);
    return img >= 0 && rn_images[img].base == h ? h : 0;
}
static int res_match(Ctx *c, uint32_t root, uint32_t e, uint32_t want, int wide) {
    uint32_t name = ld(c, e, 4);
    if (want < 0x10000) return !(name >> 31) && (name & 0xffff) == want;
    char w[256]; uint32_t n = 0;
    if (wide) { for (uint16_t ch; (ch = (uint16_t)ld(c, want + 2 * n, 2)) && n < 255; n++) w[n] = (char)ch; }
    else { for (; G(want)[n] && n < 255; n++) w[n] = G(want)[n]; }
    w[n] = 0;
    if (w[0] == '#') return !(name >> 31) && (name & 0xffff) == (uint32_t)atoi(w + 1);
    if (!(name >> 31)) return 0;
    uint32_t str = root + (name & 0x7fffffff), len = ld(c, str, 2);
    if (len != n) return 0;
    for (uint32_t k = 0; k < len; k++) if (toupper((uint8_t)w[k]) != toupper(ld(c, str + 2 + 2 * k, 2))) return 0;
    return 1;
}
// One directory level: the entry whose name matches (want == ~0u: the first one, used for the language).
static uint32_t res_find(Ctx *c, uint32_t root, uint32_t dir, uint32_t want, int wide) {
    uint32_t n = ld(c, dir + 12, 2) + ld(c, dir + 14, 2);
    for (uint32_t k = 0; k < n; k++) {
        uint32_t e = dir + 16 + 8 * k;
        if (want == ~0u || res_match(c, root, e, want, wide)) return ld(c, e + 4, 4);
    }
    return ~0u;
}
// HRSRC = guest address of the IMAGE_RESOURCE_DATA_ENTRY {rva, size, codepage, 0}.
static uint32_t find_resource(Ctx *c, uint32_t h, uint32_t name, uint32_t type, int wide) {
    uint32_t base = res_module(c, h);
    if (!base) { set_error(c, ERROR_INVALID_HANDLE); return 0; }
    uint32_t opt = base + ld(c, base + 0x3c, 4) + 24, rva = ld(c, opt + 96 + 8 * 2, 4);
    if (!rva) { set_error(c, 1812); return 0; }                       // ERROR_RESOURCE_DATA_NOT_FOUND
    uint32_t root = base + rva, o;
    if ((o = res_find(c, root, root, type, wide)) == ~0u || !(o >> 31)) { set_error(c, 1813); return 0; }
    if ((o = res_find(c, root, root + (o & 0x7fffffff), name, wide)) == ~0u || !(o >> 31)) { set_error(c, 1814); return 0; }
    if ((o = res_find(c, root, root + (o & 0x7fffffff), ~0u, 0)) == ~0u || (o >> 31)) { set_error(c, 1815); return 0; }
    return root + o;
}
static uint32_t FindResourceA_(Ctx *c) { return find_resource(c, ARG(0), ARG(1), ARG(2), 0); }
static uint32_t FindResourceW_(Ctx *c) { return find_resource(c, ARG(0), ARG(1), ARG(2), 1); }
static uint32_t LoadResource_(Ctx *c) {
    uint32_t base = res_module(c, ARG(0)), r = ARG(1);
    return base && r ? base + ld(c, r, 4) : 0;
}
static uint32_t LockResource_(Ctx *c) { return ARG(0); }
static uint32_t SizeofResource_(Ctx *c) { return ARG(1) ? ld(c, ARG(1) + 4, 4) : 0; }
static uint32_t FreeResource_(Ctx *c) { (void)c; return 0; }
// LoadAcceleratorsA(module, name): HACCEL = the guest address of the RT_ACCELERATOR table (user32 TranslateAcceleratorA).
static uint32_t LoadAcceleratorsA_(Ctx *c) {
    uint32_t r = find_resource(c, ARG(0), ARG(1), 9, 0);
    return r ? res_module(c, ARG(0)) + ld(c, r, 4) : 0;
}
// LoadStringA(module, id, buffer, size): string id lives in RT_STRING block id/16+1, entry id%16
// (counted UTF-16 strings). Returns the characters copied, without the terminator.
static uint32_t LoadStringA_(Ctx *c) {
    uint32_t h = ARG(0), id = ARG(1), buf = ARG(2), n = ARG(3);
    uint32_t r = find_resource(c, h, id / 16 + 1, 6, 0);
    if (!r || !n) { if (n) G(buf)[0] = 0; return 0; }
    uint32_t p = res_module(c, h) + ld(c, r, 4);
    for (uint32_t k = 0; k < id % 16; k++) p += 2 + 2 * ld(c, p, 2);
    uint32_t len = ld(c, p, 2);
    if (len > n - 1) len = n - 1;
    for (uint32_t k = 0; k < len; k++) { uint16_t ch = (uint16_t)ld(c, p + 2 + 2 * k, 2); G(buf)[k] = ch < 0x100 ? (char)ch : '?'; }
    G(buf)[len] = 0;
    return len;
}

// ---- files -------------------------------------------------------------------------------------
// CreateFile{A,W}(name, access, share, security, disposition, flags, template).
static uint32_t create_file(Ctx *c, const char *name) {
    uint32_t access = ARG(1), disp = ARG(4);
    int id, existed = 0;
    if ((access & 0x40000000u) || (disp != 3 && gf_stat(name) != -2 && gf_stat(name) != 0)) {   // GENERIC_WRITE or create
        id = gf_create(name, (int)disp, &existed);
        static const uint32_t err[] = {0, ERROR_FILE_NOT_FOUND, 0, 5, 80, 5, ERROR_PATH_NOT_FOUND};
        if (id == -2) { rn_halt(c, "CreateFileA outside the game and save folders: %s", name); return INVALID_HANDLE; }
        if (id < 0) { set_error(c, err[-id]); return INVALID_HANDLE; }
        uint32_t h = new_handle(H_FILE);
        get_handle(h)->gf = id;
        snprintf(get_handle(h)->path, sizeof get_handle(h)->path, "%s", name);
        if (rn_trace()) rn_log("open for writing %s (disp %u) -> %x", name, disp, h);
        set_error(c, existed && (disp == 2 || disp == 4) ? ERROR_ALREADY_EXISTS : 0);
        return h;
    }
    id = gf_open(name);
    if (id == -2) { rn_halt(c, "CreateFileA outside the game folder: %s", name); return INVALID_HANDLE; }
    if (id < 0) {
        if (rn_trace()) rn_log("file missing: %s", name);
        set_error(c, id == -3 ? 5 : ERROR_FILE_NOT_FOUND);   // a directory: ERROR_ACCESS_DENIED
        return INVALID_HANDLE;
    }
    uint32_t h = new_handle(H_FILE);
    get_handle(h)->gf = id;
    snprintf(get_handle(h)->path, sizeof get_handle(h)->path, "%s", name);
    if (rn_trace()) rn_log("open %s -> %x", name, h);
    set_error(c, 0);
    return h;
}
static uint32_t CreateFileA_(Ctx *c) { return create_file(c, G(ARG(0))); }
static uint32_t ReadFile_(Ctx *c) {
    Handle *h = get_handle(ARG(0));
    uint32_t buf = ARG(1), n = ARG(2), got = ARG(3);
    if (!h || h->kind != H_FILE) { set_error(c, ERROR_INVALID_HANDLE); return 0; }
    if (ARG(4)) { rn_halt(c, "ReadFile with OVERLAPPED"); return 0; }
    if (bad_range(c, buf, n)) { rn_halt(c, "ReadFile buffer %08x+%u", buf, n); return 0; }
    int64_t r = gf_read(h->gf, h->pos, (uint8_t *)G(buf), n);
    static int ft = -1;                     // RN_FILETRACE=1: every read, with its thread, handle and position
    if (ft < 0) ft = getenv("RN_FILETRACE") != NULL;
    if (ft) rn_log("read t%x h%x @%u n%u -> %d %s", ld(c, c->fs + 0x24, 4), ARG(0), h->pos, n, (int)r, h->path);
    if (r < 0) { rn_halt(c, "ReadFile I/O error on %s", h->path); return 0; }
    h->pos += (uint32_t)r;
    if (got) st(c, got, (uint32_t)r, 4);
    return 1;
}
static uint32_t WriteFile_(Ctx *c) {
    Handle *h = get_handle(ARG(0));
    uint32_t buf = ARG(1), n = ARG(2), put = ARG(3);
    if (!h || h->kind != H_FILE) { set_error(c, ERROR_INVALID_HANDLE); return 0; }
    if (ARG(4)) { rn_halt(c, "WriteFile with OVERLAPPED"); return 0; }
    if (!gf_is_save(h->gf)) { set_error(c, 5); return 0; }        // opened for reading only
    if (bad_range(c, buf, n)) { rn_halt(c, "WriteFile buffer %08x+%u", buf, n); return 0; }
    if (gf_write(h->gf, h->pos, (const uint8_t *)G(buf), n) < 0) { set_error(c, 5); return 0; }
    h->pos += n;
    if (put) st(c, put, n, 4);
    return 1;
}
static uint32_t SetEndOfFile_(Ctx *c) {
    Handle *h = get_handle(ARG(0));
    if (!h || h->kind != H_FILE) { set_error(c, ERROR_INVALID_HANDLE); return 0; }
    if (gf_truncate(h->gf, h->pos)) { set_error(c, 5); return 0; }
    return 1;
}
static uint32_t FlushFileBuffers_(Ctx *c) {
    Handle *h = get_handle(ARG(0));
    if (!h || h->kind != H_FILE) { set_error(c, ERROR_INVALID_HANDLE); return 0; }
    if (gf_flush(h->gf)) { rn_halt(c, "FlushFileBuffers: cannot write %s", h->path); return 0; }
    return 1;
}
static uint32_t CreateDirectoryA_(Ctx *c) {
    const char *name = G(ARG(0));
    int r = gf_mkdir(name);
    if (r == -2) { rn_log("CreateDirectoryA outside the save folder, refused: %s", name); set_error(c, 5); return 0; }   // screenshots, CombinationMaps
    if (r) { set_error(c, r == 1 ? ERROR_ALREADY_EXISTS : ERROR_PATH_NOT_FOUND); return 0; }
    if (rn_trace()) rn_log("mkdir %s", name);
    return 1;
}
extern int gf_copy(const char *src, const char *dst, int fail_if_exists, int move);   // host/files.c
static uint32_t copy_move(Ctx *c, int fail_if_exists, int move) {
    int r = gf_copy(G(ARG(0)), G(ARG(1)), fail_if_exists, move);
    if (r) set_error(c, r == -1 ? ERROR_FILE_NOT_FOUND : r == -3 ? 80 : 5);
    return !r;
}
static uint32_t CopyFileA_(Ctx *c) { return copy_move(c, ARG(2) != 0, 0); }
static uint32_t MoveFileA_(Ctx *c) { return copy_move(c, 1, 1); }
static uint32_t DeleteFileA_(Ctx *c) {
    const char *name = G(ARG(0));
    int r = gf_delete(name);
    if (r == -2) { set_error(c, 5); return 0; }                     // the game folder is read-only
    if (r) { set_error(c, ERROR_FILE_NOT_FOUND); return 0; }
    return 1;
}
// FindFirstFileA(pattern, WIN32_FIND_DATAA*): 320 bytes; attributes, 3 FILETIMEs, size hi/lo, 2 reserved,
// cFileName[260] at 44, cAlternateFileName[14] at 304.
static uint32_t FindFirstFileA_(Ctx *c) {
    const char *pat = G(ARG(0));
    uint32_t fd = ARG(1);
    char name[260];
    int dir = 0;
    uint64_t size = 0;
    int r = gf_find(pat, 0, name, sizeof name, &dir, &size);
    if (r == -2) { rn_halt(c, "FindFirstFileA outside the game and save folders: %s", pat); return INVALID_HANDLE; }
    if (rn_trace()) rn_log("find %s -> %s", pat, r ? "none" : name);
    if (r) { set_error(c, ERROR_FILE_NOT_FOUND); return INVALID_HANDLE; }
    memset(G(fd), 0, 320);
    st(c, fd, dir ? 0x10 : 0x20, 4);                                 // FILE_ATTRIBUTE_DIRECTORY / ARCHIVE
    st(c, fd + 32, (uint32_t)size, 4);
    memcpy(G(fd + 44), name, strlen(name) + 1);
    uint32_t h = new_handle(H_FIND);
    snprintf(get_handle(h)->pattern, sizeof get_handle(h)->pattern, "%s", pat);
    get_handle(h)->pos = 1;
    return h;
}
// GetShortPathNameA(long, short, size): a component that is already a valid 8.3 name stays as it is; a longer one
// becomes its first six characters (upper case, no spaces) + "~1" + the first three of its extension.
static uint32_t GetShortPathNameA_(Ctx *c) {
    const char *in = G(ARG(0));
    if (gf_stat(in) < 0) { set_error(c, ERROR_FILE_NOT_FOUND); return 0; }
    char out[600] = "", part[300];
    for (const char *p = in; *p;) {
        size_t n = strcspn(p, "\\/");
        snprintf(part, sizeof part, "%.*s", (int)n, p);
        const char *dot = strrchr(part, '.');
        size_t base = dot ? (size_t)(dot - part) : strlen(part), ext = dot ? strlen(dot + 1) : 0;
        int ok = base <= 8 && ext <= 3 && !strchr(part, ' ') && (!dot || strchr(part, '.') == dot);
        if (!ok && !(n == 2 && part[1] == ':')) {
            char sh[16] = "";
            int k = 0;
            for (size_t i = 0; i < base && k < 6; i++) if (part[i] != ' ' && part[i] != '.') sh[k++] = (char)toupper((unsigned char)part[i]);
            sh[k] = 0;
            snprintf(part, sizeof part, "%s~1%s%.3s", sh, dot ? "." : "", dot ? dot + 1 : "");
            for (char *q = part; *q; q++) *q = (char)toupper((unsigned char)*q);
        }
        strncat(out, part, sizeof out - strlen(out) - 1);
        p += n;
        if (*p) { strncat(out, "\\", sizeof out - strlen(out) - 1); p++; }
    }
    uint32_t len = (uint32_t)strlen(out);
    if (ARG(2) <= len) return len + 1;
    memcpy(G(ARG(1)), out, len + 1);
    return len;
}
// GetDiskFreeSpaceA(root, sectorsPerCluster, bytesPerSector, freeClusters, totalClusters): drive C: of this PC,
// 40 GB NTFS with 4 KB clusters, 10 GB free.
static uint32_t GetDiskFreeSpaceA_(Ctx *c) {
    uint32_t v[4] = {8, 512, 10u * 262144, 40u * 262144};                   // 262144 clusters of 4 KB = 1 GB
    for (int k = 0; k < 4; k++) if (ARG(1 + k)) st(c, ARG(1 + k), v[k], 4);
    return 1;
}
// GetFileAttributesA: FILE_ATTRIBUTE_DIRECTORY (0x10) or ARCHIVE (0x20); INVALID_FILE_ATTRIBUTES when missing.
static uint32_t GetFileAttributesA_(Ctx *c) {
    int r = gf_stat(G(ARG(0)));
    if (rn_trace()) rn_log("attributes %s -> %d", G(ARG(0)), r);
    if (r < 0) { set_error(c, r == -2 ? ERROR_PATH_NOT_FOUND : ERROR_FILE_NOT_FOUND); return 0xffffffffu; }
    return r == 1 ? 0x10 : 0x20;
}
// FindNextFileA(handle, WIN32_FIND_DATAA*): the next match of the FindFirstFileA pattern.
static uint32_t FindNextFileA_(Ctx *c) {
    Handle *h = get_handle(ARG(0));
    uint32_t fd = ARG(1);
    if (!h || h->kind != H_FIND) { set_error(c, ERROR_INVALID_HANDLE); return 0; }
    char name[260];
    int dir = 0;
    uint64_t size = 0;
    if (gf_find(h->pattern, (int)h->pos, name, sizeof name, &dir, &size)) { set_error(c, ERROR_NO_MORE_FILES); return 0; }
    h->pos++;
    memset(G(fd), 0, 320);
    st(c, fd, dir ? 0x10 : 0x20, 4);
    st(c, fd + 32, (uint32_t)size, 4);
    memcpy(G(fd + 44), name, strlen(name) + 1);
    return 1;
}
static uint32_t GetCurrentDirectoryA_(Ctx *c) {
    char d[512];
    gf_get_cwd(d, sizeof d);
    uint32_t len = (uint32_t)strlen(d), n = ARG(0), buf = ARG(1);
    if (n <= len) return len + 1;
    memcpy(G(buf), d, len + 1);
    return len;
}
static uint32_t SetCurrentDirectoryA_(Ctx *c) {
    int r = gf_set_cwd(G(ARG(0)));
    if (rn_trace() || r) rn_log("SetCurrentDirectoryA(%s) -> %d", G(ARG(0)), r);
    if (r) { set_error(c, r == -1 ? ERROR_PATH_NOT_FOUND : 5); return 0; }
    return 1;
}
// GetEnvironmentVariableA(name, buffer, size): from the environment block (win32_init); 0 and
// ERROR_ENVVAR_NOT_FOUND when it is not set.
static uint32_t GetEnvironmentVariableA_(Ctx *c) {
    const char *name = G(ARG(0));
    size_t nl = strlen(name);
    for (const char *e = G(STR_ADDR + 0x400); *e; e += strlen(e) + 1)
        if (!strncasecmp(e, name, nl) && e[nl] == '=') {
            uint32_t len = (uint32_t)strlen(e + nl + 1);
            if (ARG(2) <= len) return len + 1;
            memcpy(G(ARG(1)), e + nl + 1, len + 1);
            return len;
        }
    set_error(c, 203);
    return 0;
}
static uint32_t FindClose_(Ctx *c) {
    Handle *h = get_handle(ARG(0));
    if (!h || h->kind != H_FIND) { set_error(c, ERROR_INVALID_HANDLE); return 0; }
    h->kind = H_FREE;
    return 1;
}
// File mappings, read-only: the view is a private copy of the file in fresh guest memory.
static uint32_t CreateFileMappingA_(Ctx *c) {
    Handle *f = get_handle(ARG(0));
    uint32_t prot = ARG(2);
    if (!f || f->kind != H_FILE) { rn_halt(c, "CreateFileMappingA without a file (%08x)", ARG(0)); return 0; }
    if (prot != 2) { rn_halt(c, "CreateFileMappingA protect %#x (only PAGE_READONLY)", prot); return 0; }
    uint32_t h = new_handle(H_MAPPING);
    if (!h) { set_error(c, ERROR_NOT_ENOUGH_MEMORY); return 0; }
    gf_retain(f->gf);
    get_handle(h)->gf = f->gf;
    return h;
}
static uint32_t MapViewOfFile_(Ctx *c) {
    Handle *m = get_handle(ARG(0));
    uint32_t off = ARG(3), n = ARG(4);
    if (!m || m->kind != H_MAPPING) { set_error(c, ERROR_INVALID_HANDLE); return 0; }
    if (ARG(2)) { rn_halt(c, "MapViewOfFile beyond 4 GB"); return 0; }
    uint64_t size = gf_size(m->gf);
    if (!n) n = (uint32_t)(size - off);
    uint32_t p = virtual_reserve(c, n);
    if (!p) { set_error(c, ERROR_NOT_ENOUGH_MEMORY); return 0; }
    if (gf_read(m->gf, off, (uint8_t *)G(p), n) < 0) { rn_halt(c, "MapViewOfFile: I/O error"); return 0; }
    return p;
}
static uint32_t UnmapViewOfFile_(Ctx *c) { (void)c; return 1; }   // address space is not reused (VirtualFree)

static uint32_t SetFilePointer_(Ctx *c) {
    Handle *h = get_handle(ARG(0));
    int32_t dist = (int32_t)ARG(1);
    uint32_t hi = ARG(2), how = ARG(3);
    if (!h || h->kind != H_FILE) { set_error(c, ERROR_INVALID_HANDLE); return INVALID_HANDLE; }
    if (hi && ld(c, hi, 4) != 0 && ld(c, hi, 4) != 0xffffffffu) { rn_halt(c, "SetFilePointer beyond 4 GB"); return 0; }
    int64_t base = how == 0 ? 0 : how == 1 ? (int64_t)h->pos : (int64_t)gf_size(h->gf);
    int64_t to = base + dist;
    if (to < 0) { set_error(c, 131); return INVALID_HANDLE; }    // ERROR_NEGATIVE_SEEK
    h->pos = (uint32_t)to;
    if (hi) st(c, hi, 0, 4);
    return h->pos;
}
static uint32_t GetFileSize_(Ctx *c) {
    Handle *h = get_handle(ARG(0));
    if (!h || h->kind != H_FILE) { set_error(c, ERROR_INVALID_HANDLE); return INVALID_HANDLE; }
    if (ARG(1)) st(c, ARG(1), 0, 4);
    return (uint32_t)gf_size(h->gf);
}
static uint32_t CloseHandle_(Ctx *c) {
    uint32_t hv = ARG(0);
    Handle *h = get_handle(hv);
    if (!h) { set_error(c, ERROR_INVALID_HANDLE); return 0; }
    if (h->kind == H_FILE || h->kind == H_MAPPING) gf_close(h->gf);
    if (h->kind == H_MUTEX) threads_close_handle(hv);
    if (h->dir) closedir(h->dir);
    h->kind = H_FREE;
    return 1;
}
static uint32_t GetFullPathNameA_(Ctx *c) {
    const char *name = G(ARG(0));
    uint32_t n = ARG(1), buf = ARG(2), part = ARG(3);
    char full[600];
    if (name[0] && name[1] == ':') snprintf(full, sizeof full, "%s", name);
    else if (name[0] == '\\') snprintf(full, sizeof full, "C:%s", name);
    else snprintf(full, sizeof full, "%s\\%s", GAME_DIR, name[0] == '.' && name[1] == '\\' ? name + 2 : name);
    uint32_t len = (uint32_t)strlen(full);
    if (n <= len) return len + 1;
    memcpy(G(buf), full, len + 1);
    if (part) { char *s = strrchr(G(buf), '\\'); st(c, part, s ? buf + (uint32_t)(s - G(buf)) + 1 : buf, 4); }
    return len;
}
// Every name on this PC is already long; the path must exist. Same buffer in and out is allowed.
static uint32_t GetLongPathNameA_(Ctx *c) {
    uint32_t src = ARG(0), dst = ARG(1), n = ARG(2);
    char p[600];
    snprintf(p, sizeof p, "%s", G(src));
    int r = gf_stat(p);
    if (r == -2) { rn_halt(c, "GetLongPathNameA outside the game folder: %s", p); return 0; }
    if (r < 0) { set_error(c, ERROR_FILE_NOT_FOUND); return 0; }
    uint32_t len = (uint32_t)strlen(p);
    if (n <= len) return len + 1;
    memcpy(G(dst), p, len + 1);
    return len;
}

// Toolhelp: the process list holds this process only.
static uint32_t CreateToolhelp32Snapshot_(Ctx *c) {
    if (ARG(0) != 2) { rn_halt(c, "CreateToolhelp32Snapshot flags %08x", ARG(0)); return INVALID_HANDLE; }
    return new_handle(H_SNAPSHOT);
}
static uint32_t process_entry(Ctx *c, int first) {
    Handle *h = get_handle(ARG(0));
    uint32_t p = ARG(1);
    if (!h || h->kind != H_SNAPSHOT) { set_error(c, ERROR_INVALID_HANDLE); return 0; }
    if (ld(c, p, 4) < 296) { set_error(c, 24); return 0; }
    if (first) h->signaled = 0;
    if (h->signaled++) { set_error(c, ERROR_NO_MORE_FILES); return 0; }
    memset(G(p + 4), 0, 292);
    st(c, p + 8, ld(c, c->fs + 0x20, 4), 4);      // th32ProcessID
    st(c, p + 20, 1, 4);                          // cntThreads
    st(c, p + 28, 8, 4);                          // pcPriClassBase: normal
    put_str(c, p + 36, "MaxPayne.exe");
    return 1;
}
static uint32_t Process32First_(Ctx *c) { return process_entry(c, 1); }
static uint32_t Process32Next_(Ctx *c) { return process_entry(c, 0); }

static uint32_t GetProcessAffinityMask_(Ctx *c) { st(c, ARG(1), 1, 4); st(c, ARG(2), 1, 4); return 1; }
static uint32_t SetProcessAffinityMask_(Ctx *c) { (void)c; return 1; }
static uint32_t GetPriorityClass_(Ctx *c) { (void)c; return 0x20; }
static uint32_t SetPriorityClass_(Ctx *c) { (void)c; return 1; }
static uint32_t GetThreadPriority_(Ctx *c) { (void)c; return 0; }
static uint32_t SetThreadPriority_(Ctx *c) { (void)c; return 1; }

// =================================== winmm =====================================================
static uint32_t timeGetTime_(Ctx *c) { (void)c; rn_telem.clock_calls++; return (uint32_t)(now_us() / 1000); }
static uint32_t timeBeginPeriod_(Ctx *c) { (void)c; return 0; }
static uint32_t timeEndPeriod_(Ctx *c) { (void)c; return 0; }
static uint32_t timeGetDevCaps_(Ctx *c) { st(c, ARG(0), 1, 4); st(c, ARG(0) + 4, 1000000, 4); return 0; }

// =================================== advapi32 ==================================================
// The registry of this PC: Windows XP with DirectX 8.1, plus every key and value the game writes (MFC42 reaches
// these functions through its delay-load helper: LoadLibraryA("ADVAPI32.dll") + GetProcAddress). It persists in
// save/registry.tsv (layer_store), one value per line: key \t name \t type \t hex data. ~MHz is the CPU we
// present: rn_rdtsc counts at 1 GHz.
#define ERROR_MORE_DATA 234
#define ERROR_NO_MORE_ITEMS 259
typedef struct { char key[256], name[128]; uint32_t type, len; uint8_t *data; } RegValue;
static RegValue *values;
static int nvalues, cap_values;
static char (*keys)[256];
static int nkeys, cap_keys, reg_loaded;
static const char *const hives[] = {"HKCR", "HKCU", "HKLM", "HKU"};

static int key_index(const char *path) {
    for (int k = 0; k < nkeys; k++) if (!strcasecmp(keys[k], path)) return k;
    return -1;
}
static void key_add(const char *path) {          // and its parents
    char p[256];
    snprintf(p, sizeof p, "%s", path);
    for (char *q = p; ; q++) {
        if (*q == '\\' || !*q) {
            char ch = *q; *q = 0;
            if (key_index(p) < 0) {
                if (nkeys == cap_keys) keys = realloc(keys, sizeof *keys * (cap_keys = cap_keys ? 2 * cap_keys : 64));
                snprintf(keys[nkeys++], sizeof *keys, "%s", p);
            }
            *q = ch;
            if (!ch) break;
        }
    }
}
static RegValue *value_find(const char *key, const char *name) {
    for (int k = 0; k < nvalues; k++) if (!strcasecmp(values[k].key, key) && !strcasecmp(values[k].name, name)) return &values[k];
    return NULL;
}
static void reg_save(void);
static void value_set(const char *key, const char *name, uint32_t type, const uint8_t *data, uint32_t len, int save) {
    key_add(key);
    RegValue *v = value_find(key, name);
    if (!v) {
        if (nvalues == cap_values) values = realloc(values, sizeof *values * (cap_values = cap_values ? 2 * cap_values : 64));
        v = &values[nvalues++];
        memset(v, 0, sizeof *v);
        snprintf(v->key, sizeof v->key, "%s", key);
        snprintf(v->name, sizeof v->name, "%s", name);
    }
    else if (v->data && v->type == type && v->len == len && !memcmp(v->data, data, len)) return;   // same value: nothing to save
    free(v->data);
    v->type = type; v->len = len;
    v->data = malloc(len ? len : 1);
    memcpy(v->data, data, len);
    if (save) reg_save();
}
// The game writes each setting on its own (309 writes of the whole 29 KB file on the way from the menu to a level, ~0.7 s of
// stalls on the web, telemetry "saves"). So reg_save only marks it; the clock call flushes it half a second later, and rn_stop.
static int reg_dirty;
static uint64_t reg_dirty_us;
static void reg_save(void) {
    if (!reg_dirty) reg_dirty_us = now_us();
    reg_dirty = 1;
}
void reg_flush(void) {
    if (!reg_dirty) return;
    size_t n = 0, cap = 4096;
    char *t = malloc(cap);
    if (!t) { reg_dirty_us = now_us(); rn_log("registry: out of memory, save/registry.tsv not written"); return; }   // still dirty: tried again in 0.5 s
    for (int k = 0; k < nkeys + nvalues; k++) {
        size_t need = 600 + (k < nkeys ? 0 : 2 * values[k - nkeys].len);
        if (n + need > cap) {
            char *g = realloc(t, cap = 2 * (n + need));
            if (!g) { free(t); reg_dirty_us = now_us(); rn_log("registry: out of memory, save/registry.tsv not written"); return; }
            t = g;
        }
        if (k < nkeys) { n += (size_t)snprintf(t + n, cap - n, "%s\n", keys[k]); continue; }   // a key without values
        RegValue *v = &values[k - nkeys];
        n += (size_t)snprintf(t + n, cap - n, "%s\t%s\t%u\t", v->key, v->name, v->type);
        for (uint32_t i = 0; i < v->len; i++) n += (size_t)snprintf(t + n, cap - n, "%02x", v->data[i]);
        t[n++] = '\n';
    }
    if (layer_store("registry.tsv", (const uint8_t *)t, (int)n)) { reg_dirty_us = now_us(); rn_log("registry: cannot write save/registry.tsv"); }   // 0 = written
    else reg_dirty = 0;
    free(t);
}
static void reg_flush_due(uint64_t now) { if (reg_dirty && now - reg_dirty_us >= 500000) reg_flush(); }

// The game saves its settings (controls, game options, audio, video quality) once, when the menu mode fades out to a game
// (menu update 0x42beb0), and on quit. A page has no quit: a reload in the menus would lose every setting just made. So while a
// menu is up (the main menu, or the pause menu in a level) the same save runs twice a second from Present: the calls of 0x42beb0
// with this = the menu mode; each setter rewrites the registry, which only marks it dirty when a value changed (value_set).
static uint32_t thiscall(Ctx *c, uint32_t fn, uint32_t self) {
    if (c->halt || c->skipped) return 0;      // after a stop or an abandoned call the rest of the sequence must not run (nor its EAX be read)
    uint32_t ecx = c->r[ECX];
    c->r[ECX] = self;
    uint32_t r = call_guest(c, fn, 0, NULL);
    c->r[ECX] = ecx;
    return c->halt || c->skipped ? 0 : r;
}
// Virtual call [[obj] + off] with this = obj; nothing when obj or its vtable is not inside guest memory.
static void vcall(Ctx *c, uint32_t obj, uint32_t off) {
    if (!obj || obj < LOW || obj > c->memsz - 4) return;
    uint32_t vt = ld(c, obj, 4);
    if (vt < LOW || vt > c->memsz - off - 4) return;
    thiscall(c, ld(c, vt + off, 4), obj);
}
void settings_frame(Ctx *c) {
    static uint64_t last;
    uint64_t now = now_us();
    if (now - last < 500000 || c->halt || c->skipped) return;
    last = now;
    uint32_t ms = ld(c, 0x8a6338 + 0xc8, 4);                       // X_ModeSwitch (gamestate.c); +0x19 current mode, +0x7c menu mode
    uint32_t menu = ms ? ld(c, ms + 0x7c, 4) : 0;
    if (!menu || ld(c, ms + 0x19, 4) != menu || (c->mem[ms + 0x0d] & 0xf) != 1) return;
    uint32_t in = thiscall(c, 0x42a4c0, menu);                     // 0x42beb0: input settings (0x4cb7d0), slots 0x80 and 0x64, other settings (0x42c130)
    if (in) thiscall(c, 0x4cb7d0, in);
    uint32_t a = thiscall(c, 0x72f530, menu), b = thiscall(c, 0x60dbe0, menu);
    vcall(c, a, 0x80);
    vcall(c, b, 0x64);
    thiscall(c, 0x42c130, menu);
}
// The page asks for the size of the player's screen (web/index.html RN_SCREEN), over the saved and seeded 1024x768.
static void force_display_mode(void) {
    int w, h;
    const char *s = getenv("RN_SCREEN");
    if (!s || sscanf(s, "%dx%d", &w, &h) != 2) return;
    uint32_t v[2] = {(uint32_t)w & ~1u, (uint32_t)h & ~1u};
    value_set("HKCU\\Software\\Remedy Entertainment\\Max Payne\\Video Settings", "Display Width", 4, (const uint8_t *)&v[0], 4, 0);
    value_set("HKCU\\Software\\Remedy Entertainment\\Max Payne\\Video Settings", "Display Height", 4, (const uint8_t *)&v[1], 4, 0);
}
// RN_TEXDETAIL (phone profile): Level and Other Texture Detail 0..2. 0 halves both sides of a map, scaleMap exe 0x40ef97.
static void force_texture_detail(void) {
    const char *s = getenv("RN_TEXDETAIL");
    if (!s) return;
    uint32_t v = (uint32_t)atoi(s);
    value_set("HKCU\\Software\\Remedy Entertainment\\Max Payne\\Advanced Settings", "Level Texture Detail", 4, (const uint8_t *)&v, 4, 0);
    value_set("HKCU\\Software\\Remedy Entertainment\\Max Payne\\Advanced Settings", "Other Texture Detail", 4, (const uint8_t *)&v, 4, 0);
}

static void reg_load(void) {
    if (reg_loaded) return;
    reg_loaded = 1;
    key_add("HKLM\\Software\\Microsoft\\Direct3D");
    uint32_t mhz = 1000;
    value_set("HKLM\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "~MHz", 4, (const uint8_t *)&mhz, 4, 0);
    // What the game's startup dialog writes once (exe 0x406980, read back at 0x406740): the driver of adapter 0 by its
    // name, which P_D3D::P_D3D (driver 0x10401cad) links as the adapter Description (host/d3d8.c), its CPU driver (id 1),
    // the "D3D HAL HW T&L" level (id 4) and 1024x768x32.
    #define VS_KEY "HKCU\\Software\\Remedy Entertainment\\Max Payne\\Video Settings"
    static const char drv[] = "RADEON 9800 PRO";
    static const uint32_t vs_int[][2] = {{1024, 0}, {768, 1}, {32, 2}, {1, 3}, {4, 4}};
    static const char *const vs_name[] = {"Display Width", "Display Height", "Display Bits Per Pixel", "CPU Driver",
                                          "HW Acceleration Level"};
    value_set(VS_KEY, "Driver", 1, (const uint8_t *)drv, sizeof drv, 0);
    for (int k = 0; k < 5; k++) value_set(VS_KEY, vs_name[vs_int[k][1]], 4, (const uint8_t *)&vs_int[k][0], 4, 0);
    // The startup dialog's Advanced Settings (exe 0x403ce0 writes them), set as a player picks the best quality: Texture
    // Color Depth 1 = 32-bit (0x40c7b0: m_defaultBitsPerTexel = (value != 0) + 1; .bss 0x8a630c default 0 = 16-bit)
    // and the texture detail sliders (range 0..2, 0x402f40; .data defaults 1 at 0x865f6c/0x865f74/0x865f84) at 2.
    // At the defaults the driver uploads 16-bit textures and resamples them down through D3DX's generic blitter.
    #define AS_KEY "HKCU\\Software\\Remedy Entertainment\\Max Payne\\Advanced Settings"
    // Sound Channels 2 = 12 voices (0x40c7b0: 0 -> 8, 1 -> 10, 2 -> 12; read by 0x404190, .data default 1 at 0x865f64):
    // with 10, rain + music + gunfire + voices overflow the pool and the lowest priority sound is stolen (0x1030aad0).
    static const char *const as_name[] = {"Texture Color Depth", "Other Texture Detail", "Level Texture Detail",
                                          "Character Detail", "Sound Channels"};
    static const uint32_t as_val[] = {1, 2, 2, 2, 2};
    key_add(AS_KEY);
    for (int k = 0; k < 5; k++) value_set(AS_KEY, as_name[k], 4, (const uint8_t *)&as_val[k], 4, 0);
    static uint8_t buf[1 << 20];
    int n = layer_load("registry.tsv", buf, sizeof buf - 1);
    if (n <= 0) { force_display_mode(); force_texture_detail(); return; }   // a saved registry overrides the seed values above
    buf[n] = 0;
    for (char *l = (char *)buf, *next; *l; l = next) {
        char *e = strchr(l, '\n');
        if (e) { *e = 0; next = e + 1; } else next = l + strlen(l);     // keep a last line without its '\n'
        char *f[4] = {l, NULL, NULL, NULL};
        for (int i = 1; i < 4 && f[i - 1]; i++) { f[i] = strchr(f[i - 1], '\t'); if (f[i]) *f[i]++ = 0; }
        if (!f[1]) { key_add(l); continue; }
        if (!f[3]) continue;
        uint32_t len = (uint32_t)strlen(f[3]) / 2;
        uint8_t *d = malloc(len + 1);
        for (uint32_t i = 0; i < len; i++) { unsigned x = 0; sscanf(f[3] + 2 * i, "%2x", &x); d[i] = (uint8_t)x; }
        value_set(f[0], f[1], (uint32_t)strtoul(f[2], NULL, 10), d, len, 0);
        free(d);
    }
    force_display_mode();
    force_texture_detail();
}
// The full path ("HKCU\Software\...") of a predefined key or an open key handle, plus sub; NULL for a bad handle.
static const char *key_path(Ctx *c, uint32_t hkey, uint32_t sub, char *out, size_t n) {
    if (hkey >= 0x80000000u && hkey <= 0x80000003u) snprintf(out, n, "%s", hives[hkey - 0x80000000u]);
    else {
        Handle *h = get_handle(hkey);
        if (!h || h->kind != H_KEY) return NULL;
        snprintf(out, n, "%s", h->path);
    }
    const char *s = sub ? G(sub) : "";
    while (*s == '\\') s++;
    if (*s) snprintf(out + strlen(out), n - strlen(out), "\\%s", s);
    size_t len = strlen(out);
    while (len && out[len - 1] == '\\') out[--len] = 0;
    return out;
}
static uint32_t key_handle(Ctx *c, const char *path, uint32_t out) {
    uint32_t h = new_handle(H_KEY);
    if (!h) { rn_halt(c, "out of handles"); return ERROR_NOT_ENOUGH_MEMORY; }
    snprintf(get_handle(h)->path, sizeof get_handle(h)->path, "%s", path);
    st(c, out, h, 4);
    return 0;
}
static uint32_t open_key(Ctx *c, uint32_t parent, uint32_t sub, uint32_t out, int create, uint32_t disp) {
    char path[512];
    reg_load();
    if (!key_path(c, parent, sub, path, sizeof path)) return ERROR_INVALID_HANDLE;
    int exists = !strchr(path, '\\') || key_index(path) >= 0;
    if (rn_trace()) rn_log("registry: %s %s -> %s", create ? "create" : "open", path, exists ? "exists" : "missing");
    if (!exists && !create) return ERROR_FILE_NOT_FOUND;
    if (!exists) { key_add(path); reg_save(); }
    if (disp) st(c, disp, exists ? 2 : 1, 4);       // REG_OPENED_EXISTING_KEY / REG_CREATED_NEW_KEY
    return key_handle(c, path, out);
}
static uint32_t RegOpenKeyExA_(Ctx *c) { return open_key(c, ARG(0), ARG(1), ARG(4), 0, 0); }
static uint32_t RegOpenKeyA_(Ctx *c) { return open_key(c, ARG(0), ARG(1), ARG(2), 0, 0); }
static uint32_t RegCreateKeyExA_(Ctx *c) { return open_key(c, ARG(0), ARG(1), ARG(7), 1, ARG(8)); }
static uint32_t RegCreateKeyA_(Ctx *c) { return open_key(c, ARG(0), ARG(1), ARG(2), 1, 0); }
// Copies a value out: type, size in/out, data when there is room (ERROR_MORE_DATA otherwise).
static uint32_t value_out(Ctx *c, RegValue *v, uint32_t ptype, uint32_t data, uint32_t psize) {
    if (ptype) st(c, ptype, v->type, 4);
    if (!psize) return data ? 87 : 0;
    uint32_t room = ld(c, psize, 4);
    st(c, psize, v->len, 4);
    if (!data) return 0;
    if (room < v->len) return ERROR_MORE_DATA;
    memcpy(G(data), v->data, v->len);
    return 0;
}
static uint32_t RegQueryValueExA_(Ctx *c) {
    char path[512];
    reg_load();
    if (!key_path(c, ARG(0), 0, path, sizeof path)) return ERROR_INVALID_HANDLE;
    RegValue *v = value_find(path, ARG(1) ? G(ARG(1)) : "");
    if (rn_trace()) rn_log("registry: %s \"%s\" -> %s", path, ARG(1) ? G(ARG(1)) : "", v ? "found" : "not found");
    if (!v) return ERROR_FILE_NOT_FOUND;
    return value_out(c, v, ARG(3), ARG(4), ARG(5));
}
// RegQueryValueA(key, sub, data, size*): the default (unnamed) REG_SZ value of key\sub.
static uint32_t RegQueryValueA_(Ctx *c) {
    char path[512];
    reg_load();
    if (!key_path(c, ARG(0), ARG(1), path, sizeof path)) return ERROR_INVALID_HANDLE;
    if (strchr(path, '\\') && key_index(path) < 0) return ERROR_FILE_NOT_FOUND;
    RegValue *v = value_find(path, "");
    if (!v) {                                        // a key without a default value reads as ""
        if (ARG(3)) { uint32_t room = ld(c, ARG(3), 4); st(c, ARG(3), 1, 4); if (ARG(2) && room) G(ARG(2))[0] = 0; }
        return 0;
    }
    return value_out(c, v, 0, ARG(2), ARG(3));
}
static uint32_t RegSetValueExA_(Ctx *c) {
    char path[512];
    reg_load();
    if (!key_path(c, ARG(0), 0, path, sizeof path)) return ERROR_INVALID_HANDLE;
    uint32_t type = ARG(3), len = ARG(5);
    if (rn_trace()) rn_log("registry: set %s \"%s\" type %u len %u", path, ARG(1) ? G(ARG(1)) : "", type, len);
    value_set(path, ARG(1) ? G(ARG(1)) : "", type, (const uint8_t *)G(ARG(4)), len, 1);
    return 0;
}
// RegSetValueA(key, sub, REG_SZ, data, len): the default value of key\sub, created if needed; len is ignored.
static uint32_t RegSetValueA_(Ctx *c) {
    char path[512];
    reg_load();
    if (!key_path(c, ARG(0), ARG(1), path, sizeof path)) return ERROR_INVALID_HANDLE;
    if (ARG(2) != 1) return 87;
    if (rn_trace()) rn_log("registry: set %s default = %s", path, G(ARG(3)));
    value_set(path, "", 1, (const uint8_t *)G(ARG(3)), gstrlen(c, ARG(3)) + 1, 1);
    return 0;
}
static uint32_t RegDeleteValueA_(Ctx *c) {
    char path[512];
    reg_load();
    if (!key_path(c, ARG(0), 0, path, sizeof path)) return ERROR_INVALID_HANDLE;
    RegValue *v = value_find(path, ARG(1) ? G(ARG(1)) : "");
    if (!v) return ERROR_FILE_NOT_FOUND;
    free(v->data);
    *v = values[--nvalues];
    reg_save();
    return 0;
}
// RegDeleteKeyA: a key without subkeys (Windows NT refuses one that has them), with its values.
static uint32_t RegDeleteKeyA_(Ctx *c) {
    char path[512];
    reg_load();
    if (!key_path(c, ARG(0), ARG(1), path, sizeof path)) return ERROR_INVALID_HANDLE;
    int k = key_index(path);
    if (k < 0) return ERROR_FILE_NOT_FOUND;
    size_t n = strlen(path);
    for (int j = 0; j < nkeys; j++) if (!strncasecmp(keys[j], path, n) && keys[j][n] == '\\') return 5;
    for (int j = nvalues - 1; j >= 0; j--) if (!strcasecmp(values[j].key, path)) { free(values[j].data); values[j] = values[--nvalues]; }
    nkeys--;
    if (k < nkeys) memcpy(keys[k], keys[nkeys], sizeof *keys);
    reg_save();
    return 0;
}
// RegEnumKeyA(key, index, name, size): the index-th direct subkey.
static uint32_t RegEnumKeyA_(Ctx *c) {
    char path[512];
    reg_load();
    if (!key_path(c, ARG(0), 0, path, sizeof path)) return ERROR_INVALID_HANDLE;
    size_t n = strlen(path);
    uint32_t want = ARG(1);
    for (int k = 0; k < nkeys; k++)
        if (!strncasecmp(keys[k], path, n) && keys[k][n] == '\\' && !strchr(keys[k] + n + 1, '\\') && !want--) {
            const char *name = keys[k] + n + 1;
            if (ARG(3) <= strlen(name)) return ERROR_MORE_DATA;
            memcpy(G(ARG(2)), name, strlen(name) + 1);
            return 0;
        }
    return ERROR_NO_MORE_ITEMS;
}
static uint32_t RegCloseKey_(Ctx *c) {
    Handle *h = get_handle(ARG(0));
    if (ARG(0) >= 0x80000000u) return 0;
    if (!h || h->kind != H_KEY) return ERROR_INVALID_HANDLE;
    h->kind = H_FREE;
    return 0;
}
// File security (NTFS ACLs): this PC's FAT32 drive has none; MFC treats the failure as "nothing to copy".
static uint32_t GetFileSecurityA_(Ctx *c) { set_error(c, 50); return 0; }   // ERROR_NOT_SUPPORTED
static uint32_t SetFileSecurityA_(Ctx *c) { set_error(c, 50); return 0; }

// SHGetFolderPathA(hwnd, csidl, hToken, dwFlags, pszPath): the folders Windows XP gives a default install.
// Only the CSIDL the game asks for is answered; any other one stops loudly.
static uint32_t SHGetFolderPathA_(Ctx *c) {
    uint32_t csidl = ARG(1) & 0xff, out = ARG(4);     // high bits: CSIDL_FLAG_CREATE and friends
    if (csidl != 0x23) { rn_halt(c, "SHGetFolderPathA(CSIDL %#x)", csidl); return 0x80004005u; }
    put_str(c, out, "C:\\Documents and Settings\\All Users\\Application Data");   // CSIDL_COMMON_APPDATA
    return 0;                                           // S_OK
}

// SHGetSpecialFolderPathA(hwnd, pszPath, csidl, fCreate) of shell32 (the game finds it with GetProcAddress):
// CSIDL_PERSONAL (5) is My Documents of the user Player, the save folder of host/files.c.
static uint32_t SHGetSpecialFolderPathA_(Ctx *c) {
    uint32_t out = ARG(1), csidl = ARG(2);
    if (csidl != 5) { rn_halt(c, "SHGetSpecialFolderPathA(CSIDL %#x)", csidl); return 0; }
    put_str(c, out, "C:\\Documents and Settings\\Player\\My Documents");
    if (ARG(3)) gf_mkdir(G(out));
    return 1;
}

// ---- date and time text: English (United Kingdom), the language the install writes; the time zone is GMT ----
// SYSTEMTIME: wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds (u16 each).
static void now_systemtime(uint16_t t[8]) {
    time_t now = time(NULL);
    struct tm g;
    gmtime_r(&now, &g);
    uint16_t v[8] = {(uint16_t)(g.tm_year + 1900), (uint16_t)(g.tm_mon + 1), (uint16_t)g.tm_wday, (uint16_t)g.tm_mday,
                     (uint16_t)g.tm_hour, (uint16_t)g.tm_min, (uint16_t)g.tm_sec, 0};
    memcpy(t, v, sizeof v);
}
// A date/time picture (d dd ddd dddd M MM MMM MMMM y yy yyyy h hh H HH m mm s ss t tt, 'quoted text').
static int picture(const char *f, const uint16_t t[8], char *out, int n) {
    static const char *mon[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September",
                                "October", "November", "December"};
    static const char *day[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    int o = 0;
    char tmp[32];
    while (*f && o < n - 1) {
        char ch = *f;
        int k = 1;
        if (ch == '\'') {
            for (f++; *f && *f != '\'' && o < n - 1; f++) out[o++] = *f;
            if (*f) f++;
            continue;
        }
        while (f[k] == ch) k++;
        tmp[0] = 0;
        int h12 = t[4] % 12 ? t[4] % 12 : 12;
        switch (ch) {
        case 'd': if (k <= 2) snprintf(tmp, sizeof tmp, k == 1 ? "%u" : "%02u", t[3]);
                  else snprintf(tmp, sizeof tmp, "%.*s", k == 3 ? 3 : 20, day[t[2] % 7]); break;
        case 'M': if (k <= 2) snprintf(tmp, sizeof tmp, k == 1 ? "%u" : "%02u", t[1]);
                  else snprintf(tmp, sizeof tmp, "%.*s", k == 3 ? 3 : 20, mon[(t[1] + 11) % 12]); break;
        case 'y': if (k <= 2) snprintf(tmp, sizeof tmp, k == 1 ? "%u" : "%02u", t[0] % 100);
                  else snprintf(tmp, sizeof tmp, "%u", t[0]); break;
        case 'h': snprintf(tmp, sizeof tmp, k == 1 ? "%u" : "%02u", h12); break;
        case 'H': snprintf(tmp, sizeof tmp, k == 1 ? "%u" : "%02u", t[4]); break;
        case 'm': snprintf(tmp, sizeof tmp, k == 1 ? "%u" : "%02u", t[5]); break;
        case 's': snprintf(tmp, sizeof tmp, k == 1 ? "%u" : "%02u", t[6]); break;
        case 't': snprintf(tmp, sizeof tmp, "%.*s", k == 1 ? 1 : 2, t[4] < 12 ? "AM" : "PM"); break;
        default: k = 1; tmp[0] = ch; tmp[1] = 0;
        }
        f += k;
        for (char *q = tmp; *q && o < n - 1; q++) out[o++] = *q;
    }
    out[o] = 0;
    return o;
}
// Get{Date,Time}FormatA(locale, flags, SYSTEMTIME*, format, out, size): chars written with the NUL, or the
// size needed when size is 0. DATE_LONGDATE = 2; TIME_NOSECONDS = 2, TIME_NOMINUTESORSECONDS = 1.
static uint32_t date_time_format(Ctx *c, int date) {
    uint32_t flags = ARG(1), tp = ARG(2), fmt = ARG(3), out = ARG(4), n = ARG(5);
    uint16_t t[8];
    if (tp) for (int k = 0; k < 8; k++) t[k] = (uint16_t)ld(c, tp + 2 * k, 2); else now_systemtime(t);
    const char *f = fmt ? G(fmt) : date ? (flags & 2 ? "dd MMMM yyyy" : "dd/MM/yyyy")
                                        : (flags & 1 ? "HH" : flags & 2 ? "HH:mm" : "HH:mm:ss");
    char buf[256];
    int len = picture(f, t, buf, sizeof buf) + 1;
    if (!n) return (uint32_t)len;
    if ((uint32_t)len > n) { set_error(c, 122); return 0; }      // ERROR_INSUFFICIENT_BUFFER
    memcpy(G(out), buf, (size_t)len);
    return (uint32_t)len;
}
static uint32_t GetDateFormatA_(Ctx *c) { return date_time_format(c, 1); }
static uint32_t GetTimeFormatA_(Ctx *c) { return date_time_format(c, 0); }

// ---- C runtime support ----------------------------------------------------------------------------
static uint32_t GetProcessHeap_(Ctx *c) {
    static uint32_t heap;
    if (!heap) { heap = new_handle(H_HEAP); st(c, PEB_ADDR + 0x18, heap, 4); }
    return heap;
}
static uint32_t SetEnvironmentVariableA_(Ctx *c) { (void)c; return 1; }      // nothing reads it back
// EnumSystemLocalesA(callback, flags): the one installed locale, English (United Kingdom).
static uint32_t EnumSystemLocalesA_(Ctx *c) {
    static uint32_t str;
    if (!str) { str = virtual_reserve(c, 16); put_str(c, str, "00000809"); }
    uint32_t fn = ARG(0);
    call_guest(c, fn, 1, &str);
    return 1;
}
// No console is attached to a GUI program.
static uint32_t no_console(Ctx *c) { set_error(c, ERROR_INVALID_HANDLE); return 0; }
// Structured exceptions: the walk over fs:[0] that ntdll does (RtlDispatchException, RtlUnwind; same shape as Wine
// signal_i386.c call_seh_handlers, __regs_RtlUnwind). The handlers are the lifted msvcrt ones (__CxxFrameHandler
// 0x1080ace0, _except_handler3 0x1080ef06). rea/map/game.md "C++ exceptions".
#define SEH_END 0xffffffffu
// Handlers of the dispatcher's own frame {next, handler, frame being called}, pushed around each handler call. Only these
// two walks read them. msvcrt _UnwindNestedFrames 0x1080ac90 re-links the top of the chain above the target frame and
// _JumpToContinuation 0x1080ac4f pops it, so this frame has to be there.
#define SEH_RAISE 0xfffffff0u
#define SEH_UNWIND 0xffffffe0u
enum { EH_REC = 0x50, EH_CTX = 0x2cc, EH_MARK = 12, EH_CXX = 0xe06d7363 };
enum { EH_NONCONTINUABLE = 1, EH_UNWINDING = 2, EH_EXIT_UNWIND = 4, EH_NESTED = 0x10 };
enum { CTX_EDI = 0x9c, CTX_EAX = 0xb0, CTX_EBP = 0xb4, CTX_EIP = 0xb8, CTX_EFLAGS = 0xc0, CTX_ESP = 0xc4 };
static const uint8_t ctx_reg[6] = {EDI, ESI, EBX, EDX, ECX, EAX};
static int eh_log(void) { static int on = -1; if (on < 0) on = getenv("RN_EH") != NULL; return on; }
// The thrown C++ type: ThrowInfo.pCatchableTypeArray[0].pType->name, as in ".PAVR_Exception@@".
static const char *eh_name(Ctx *c, uint32_t rec) {
    if (ld(c, rec, 4) != EH_CXX || ld(c, rec + 16, 4) != 3) return "";
    uint32_t info = ld(c, rec + 28, 4);
    if (info < LOW || info >= c->memsz - 16) return "(rethrow)";
    return (const char *)c->mem + ld(c, ld(c, ld(c, info + 12, 4) + 4, 4) + 4, 4) + 8;
}
// One handler under the dispatcher's frame. A skipped call leaves the chain to the continuation that took over.
static uint32_t eh_call(Ctx *c, uint32_t mark, uint32_t kind, uint32_t frame, uint32_t rec, uint32_t ctx, uint32_t disp) {
    uint32_t a[4] = {rec, frame, ctx, disp}, fn = ld(c, frame + 4, 4), fs0 = c->fs;
    st(c, mark, ld(c, fs0, 4), 4);
    st(c, mark + 4, kind, 4);
    st(c, mark + 8, frame, 4);
    st(c, fs0, mark, 4);
    st(c, disp, 0, 4);
    c->df = 0;
    uint32_t r = call_guest_cdecl(c, fn, 4, a);
    if (eh_log()) rn_log("eh frame %08x handler %08x flags %x -> %u%s", frame, fn, ld(c, rec + 4, 4), r, c->skipped ? " skipped" : "");
    if (!c->halt && !c->skipped) st(c, fs0, ld(c, mark, 4), 4);
    return r;
}
// CONTEXT of the raise, for the handlers that read it (offsets of the i386 CONTEXT). sp = esp after the API returns.
static void eh_context(Ctx *c, uint32_t ctx, uint32_t ip, uint32_t sp) {
    memset(c->mem + ctx, 0, EH_CTX);
    st(c, ctx, 0x10007, 4);                 // CONTEXT_FULL
    st(c, ctx + 0x90, 0x3b, 4);             // SegFs, then Es, Ds, Cs, Ss: the values of 32-bit Windows
    st(c, ctx + 0x94, 0x23, 4);
    st(c, ctx + 0x98, 0x23, 4);
    st(c, ctx + 0xbc, 0x1b, 4);
    st(c, ctx + 0xc8, 0x23, 4);
    for (int k = 0; k < 6; k++) st(c, ctx + CTX_EDI + 4 * k, c->r[ctx_reg[k]], 4);
    st(c, ctx + CTX_EBP, c->r[EBP], 4);
    st(c, ctx + CTX_EIP, ip, 4);
    // Same layout as pushfd in tools/recomp/lift.py.
    st(c, ctx + CTX_EFLAGS, (uint32_t)c->cf | 2u | (uint32_t)c->pf << 2 | (uint32_t)c->af << 4 | (uint32_t)c->zf << 6 |
       (uint32_t)c->sf << 7 | 0x200u | (uint32_t)c->df << 10 | (uint32_t)c->of << 11 | c->eflx, 4);
    st(c, ctx + CTX_ESP, sp, 4);
}
// ExceptionContinueExecution: the CONTEXT, with what the handler changed, becomes the registers (Wine NtContinue).
// Returns EAX. A handler that moves Eip or Esp has no user in the game: it stops loud.
static uint32_t eh_continue(Ctx *c, uint32_t ctx, uint32_t ip, uint32_t sp, uint32_t esp0) {
    if (ld(c, ctx + CTX_EIP, 4) != ip || ld(c, ctx + CTX_ESP, 4) != sp) {
        c->halt_at = ip;
        rn_halt(c, "a handler moved the CONTEXT to eip %08x esp %08x", ld(c, ctx + CTX_EIP, 4), ld(c, ctx + CTX_ESP, 4));
        return 0;
    }
    for (int k = 0; k < 6; k++) c->r[ctx_reg[k]] = ld(c, ctx + CTX_EDI + 4 * k, 4);
    c->r[EBP] = ld(c, ctx + CTX_EBP, 4);
    uint32_t f = ld(c, ctx + CTX_EFLAGS, 4);
    c->cf = f & 1; c->pf = f >> 2 & 1; c->af = f >> 4 & 1; c->zf = f >> 6 & 1; c->sf = f >> 7 & 1; c->df = f >> 10 & 1; c->of = f >> 11 & 1;
    c->eflx = f & 0x240000u;
    c->r[ESP] = esp0;
    return c->r[EAX];
}
static uint32_t RaiseException_(Ctx *c) {
    uint32_t code = ARG(0), flags = ARG(1), n = ARG(2) > 15 ? 15 : ARG(2), args = ARG(3), ip = ld(c, c->r[ESP], 4);
    uint32_t esp0 = c->r[ESP], sp = esp0 + 20, fs0 = c->fs;
    uint32_t mark = (esp0 - EH_MARK) & ~3u, disp = mark - 4, rec = disp - EH_REC, ctx = rec - EH_CTX;
    memset(c->mem + rec, 0, EH_REC);
    st(c, rec, code, 4);
    st(c, rec + 4, flags & EH_NONCONTINUABLE, 4);
    st(c, rec + 12, ip, 4);
    st(c, rec + 16, n, 4);
    for (uint32_t k = 0; k < n; k++) st(c, rec + 20 + 4 * k, ld(c, args + 4 * k, 4), 4);
    eh_context(c, ctx, ip, sp);
    if (eh_log()) rn_log("raise %08x %s ip %08x esp %08x", code, eh_name(c, rec), ip, esp0);
    c->r[ESP] = ctx & ~15u;
    uint32_t nested = 0;
    for (uint32_t frame = ld(c, fs0, 4); frame != SEH_END; frame = ld(c, frame, 4)) {
        uint32_t fn = ld(c, frame + 4, 4);
        if (fn == SEH_UNWIND) continue;     // Wine unwind_handler outside an unwind: ExceptionContinueSearch
        if (fn == SEH_RAISE) {              // a handler of an outer dispatch raised: Wine raise_handler, ExceptionNestedException
            uint32_t was = ld(c, frame + 8, 4);
            if (nested < was) nested = was;
            st(c, rec + 4, ld(c, rec + 4, 4) | EH_NESTED, 4);
            continue;
        }
        uint32_t r = eh_call(c, mark, SEH_RAISE, frame, rec, ctx, disp);
        if (c->halt || c->skipped) return 0;
        if (frame == nested) { nested = 0; st(c, rec + 4, ld(c, rec + 4, 4) & ~(uint32_t)EH_NESTED, 4); }
        if (r == 0) {
            if (ld(c, rec + 4, 4) & EH_NONCONTINUABLE) { rn_halt(c, "exception %08x %s: a handler continued a noncontinuable", code, eh_name(c, rec)); return 0; }
            return eh_continue(c, ctx, ip, sp, esp0);
        }
        if (r != 1) { c->halt_at = ip; rn_halt(c, "exception %08x: handler of frame %08x answered %u", code, frame, r); return 0; }
    }
    c->halt_at = ip;
    rn_halt(c, "unhandled exception %08x %s", code, eh_name(c, rec));
    return 0;
}
static uint32_t RtlUnwind_(Ctx *c) {
    uint32_t target = ARG(0), tip = ARG(1), rec = ARG(2), retval = ARG(3), ip = ld(c, c->r[ESP], 4);
    uint32_t esp0 = c->r[ESP], fs0 = c->fs;
    // Every caller resumes right after its call: _UnwindNestedFrames 0x1080acb9, _global_unwind2 0x1080ee18.
    if (tip && tip != ip) { c->halt_at = ip; rn_halt(c, "RtlUnwind resumes at %08x, not at its return %08x", tip, ip); return 0; }
    uint32_t disp = esp0 - 4, own = disp - EH_REC, ctx = own - EH_CTX, mark = (ctx - EH_MARK) & ~3u;
    c->uw = 1;                              // from here on a continuation can return past host frames
    if (!rec) {
        rec = own;
        memset(c->mem + rec, 0, EH_REC);
        st(c, rec, 0xc0000027, 4);          // STATUS_UNWIND
        st(c, rec + 12, ip, 4);
    }
    st(c, rec + 4, ld(c, rec + 4, 4) | EH_UNWINDING | (target ? 0 : EH_EXIT_UNWIND), 4);
    eh_context(c, ctx, ip, esp0 + 20);
    c->r[ESP] = mark & ~15u;
    uint32_t frame = ld(c, fs0, 4);
    while (frame != SEH_END && frame != target) {
        uint32_t fn = ld(c, frame + 4, 4);
        if (fn == SEH_UNWIND) frame = ld(c, frame + 8, 4);      // Wine unwind_handler: ExceptionCollidedUnwind, the frame the outer unwind was calling
        else if (fn != SEH_RAISE) {                             // raise_handler answers ExceptionContinueSearch in an unwind
            uint32_t r = eh_call(c, mark, SEH_UNWIND, frame, rec, ctx, disp);
            if (c->halt || c->skipped) return 0;
            // ExceptionCollidedUnwind: msvcrt _local_unwind2's own handler 0x1080ee38 answers it with its frame in *disp 0x1080ee52.
            if (r == 3) frame = ld(c, disp, 4);
            else if (r != 1) { c->halt_at = ip; rn_halt(c, "RtlUnwind: handler of frame %08x answered %u", frame, r); return 0; }
        }
        frame = ld(c, frame, 4);
        st(c, fs0, frame, 4);
    }
    c->r[ESP] = esp0;
    return retval;
}
static uint32_t UnhandledExceptionFilter_(Ctx *c) {
    uint32_t rec = ld(c, ARG(0), 4);
    c->halt_at = ld(c, rec + 12, 4);
    rn_halt(c, "UnhandledExceptionFilter: exception %08x %s", ld(c, rec, 4), eh_name(c, rec));
    return 0;
}
static uint32_t TerminateProcess_(Ctx *c) { rn_halt(c, "TerminateProcess(%u): the game quit", ARG(1)); return 0; }
// CreateFileW: the UTF-16 name narrowed (game paths are ASCII).
static uint32_t CreateFileW_(Ctx *c) {
    uint32_t w = ARG(0), k = 0;
    char name[512];
    for (uint16_t ch; (ch = (uint16_t)ld(c, w + 2 * k, 2)) && k < sizeof name - 1; k++) {
        if (ch > 0x7f) { rn_halt(c, "CreateFileW with a non-ASCII name"); return INVALID_HANDLE; }
        name[k] = (char)ch;
    }
    name[k] = 0;
    return create_file(c, name);
}
// Serial ports and modems: this PC has none. Handles to them can never be opened.
static uint32_t no_comm(Ctx *c) { set_error(c, ERROR_INVALID_HANDLE); return 0; }

// ole32: COM is initialized; no in-process server is registered on this PC (the game's CoCreateInstance calls test
// optional interfaces: sndmfc 0x10301590 the DirectSound private property set).
static uint32_t CoInitialize_(Ctx *c) { (void)c; return 0; }                   // S_OK
static uint32_t CoUninitialize_(Ctx *c) { (void)c; return 0; }
extern int dsound_cocreate(Ctx *c, uint32_t clsid, uint32_t iid, uint32_t out_p);
static uint32_t CoCreateInstance_(Ctx *c) {
    uint32_t g = ARG(0);
    if (ARG(4) && dsound_cocreate(c, g, ARG(3), ARG(4))) return 0;      // CLSID_DirectSound: host/dsound.c
    rn_log("CoCreateInstance({%08x-%04x-%04x-...}) -> REGDB_E_CLASSNOTREG", ld(c, g, 4), ld(c, g + 4, 2), ld(c, g + 6, 2));
    if (ARG(4)) st(c, ARG(4), 0, 4);
    return 0x80040154u;
}
static uint32_t entry_returned(Ctx *c) { rn_halt(c, "entry point returned eax=%08x", c->r[EAX]); return 0; }

#define K(name, n) {"kernel32.dll", #name, n, name##_}
const Api win32_apis[] = {
    {"rn", "entry_returned", 0, entry_returned},
    {"ole32.dll", "CoInitialize", 1, CoInitialize_}, {"ole32.dll", "CoUninitialize", 0, CoUninitialize_},
    {"ole32.dll", "CoCreateInstance", 5, CoCreateInstance_},
    K(GetVersion, 0), K(GetVersionExA, 1), K(GetCommandLineA, 0), K(GetCommandLineW, 0), K(GetProcessVersion, 1),
    {"kernel32.dll", "GetEnvironmentStrings", 0, GetEnvironmentStrings_},
    {"kernel32.dll", "GetEnvironmentStringsA", 0, GetEnvironmentStrings_},
    {"kernel32.dll", "FreeEnvironmentStringsA", 1, FreeEnvironmentStrings_},
    K(GetEnvironmentStringsW, 0), K(FreeEnvironmentStringsW, 1),
    K(GetModuleFileNameA, 3), K(GetStartupInfoA, 1), K(GetStdHandle, 1), K(SetStdHandle, 2), K(GetFileType, 1),
    K(SetHandleCount, 1), K(GetACP, 0), K(GetOEMCP, 0), K(IsValidCodePage, 1), K(GetCPInfo, 2),
    K(MultiByteToWideChar, 6), K(WideCharToMultiByte, 8), K(GetStringTypeW, 4), K(GetStringTypeA, 5),
    K(LCMapStringW, 6), K(LCMapStringA, 6), K(CompareStringA, 6), K(CompareStringW, 6), K(lstrcmpiA, 2), K(lstrcmpA, 2),
    K(GetLocaleInfoA, 4), K(GetLocaleInfoW, 4), K(GetUserDefaultLCID, 0), K(IsValidLocale, 2),
    K(HeapCreate, 3), K(HeapDestroy, 1), K(HeapAlloc, 3), K(HeapFree, 3), K(HeapSize, 3), K(HeapReAlloc, 4),
    K(VirtualAlloc, 4), K(VirtualFree, 3), K(VirtualQuery, 3), K(VirtualProtect, 4),
    K(GlobalAlloc, 2), K(GlobalReAlloc, 3), K(GlobalFree, 1), K(GlobalSize, 1), K(GlobalLock, 1), K(GlobalUnlock, 1),
    K(GlobalHandle, 1), K(GlobalFlags, 1), K(GlobalMemoryStatus, 1),
    K(GlobalAddAtomA, 1), K(GlobalFindAtomA, 1), K(GlobalDeleteAtom, 1), K(GlobalGetAtomNameA, 3),
    {"user32.dll", "RegisterWindowMessageA", 1, GlobalAddAtomA_}, {"user32.dll", "RegisterClipboardFormatA", 1, GlobalAddAtomA_},
    {"kernel32.dll", "LocalAlloc", 2, GlobalAlloc_}, {"kernel32.dll", "LocalReAlloc", 3, GlobalReAlloc_},
    {"kernel32.dll", "LocalFree", 1, GlobalFree_}, {"kernel32.dll", "LocalLock", 1, GlobalLock_},
    {"kernel32.dll", "LocalUnlock", 1, GlobalUnlock_},
    K(TlsAlloc, 0), K(TlsFree, 1), K(TlsGetValue, 1), K(TlsSetValue, 2),
    K(InterlockedExchange, 2), K(InterlockedIncrement, 1), K(InterlockedDecrement, 1), K(MulDiv, 3),
    K(lstrlenA, 1), K(lstrlenW, 1), K(lstrcpyA, 2), K(lstrcpyW, 2), K(lstrcpynA, 3), K(lstrcatA, 2), K(IsDBCSLeadByte, 1),
    K(GetThreadLocale, 0), K(FindResourceA, 3), K(FindResourceW, 3), K(LoadResource, 2), K(LockResource, 1),
    K(SizeofResource, 2), K(FreeResource, 1), {"user32.dll", "LoadStringA", 4, LoadStringA_},
    {"user32.dll", "LoadAcceleratorsA", 2, LoadAcceleratorsA_}, {"user32.dll", "CharNextA", 1, CharNextA_}, K(GetCurrentProcess, 0), K(GetCurrentThread, 0), K(GetCurrentProcessId, 0),
    K(GetLastError, 0), K(SetLastError, 1), K(SetUnhandledExceptionFilter, 1),
    K(SetErrorMode, 1), K(IsBadReadPtr, 2), {"kernel32.dll", "IsBadWritePtr", 2, IsBadReadPtr_}, K(IsBadCodePtr, 1),
    K(GetSystemInfo, 1), K(IsProcessorFeaturePresent, 1), K(GetTickCount, 0), K(QueryPerformanceCounter, 1), K(QueryPerformanceFrequency, 1),
    K(GetSystemTimeAsFileTime, 1), K(FileTimeToLocalFileTime, 2), K(LocalFileTimeToFileTime, 2), K(FileTimeToSystemTime, 2),
    K(SystemTimeToFileTime, 2), K(GetSystemTime, 1), K(GetLocalTime, 1), K(GetTimeZoneInformation, 1), K(ExitProcess, 1), K(OutputDebugStringA, 1),
    K(GetModuleHandleA, 1), K(LoadLibraryA, 1), K(GetProcAddress, 2), K(FreeLibrary, 1), K(DisableThreadLibraryCalls, 1),
    K(GetSystemDirectoryA, 2), K(GetComputerNameA, 2),
    K(CreateFileA, 7), K(ReadFile, 5), K(WriteFile, 5), K(SetEndOfFile, 1), K(FlushFileBuffers, 1),
    K(CreateDirectoryA, 2), K(DeleteFileA, 1), K(CopyFileA, 3), K(MoveFileA, 2), K(FindFirstFileA, 2), K(FindNextFileA, 2), K(FindClose, 1),
    K(GetCurrentDirectoryA, 2), K(GetFileAttributesA, 1), K(GetShortPathNameA, 3), K(GetDiskFreeSpaceA, 5), K(SetCurrentDirectoryA, 1), K(GetEnvironmentVariableA, 3),
    K(CreateFileMappingA, 6), K(MapViewOfFile, 5), K(UnmapViewOfFile, 1), K(SetFilePointer, 4), K(GetFileSize, 2), K(CloseHandle, 1), K(GetFullPathNameA, 4), K(GetLongPathNameA, 3),
    K(GetProcessAffinityMask, 3), K(SetProcessAffinityMask, 2), K(GetPriorityClass, 1), K(SetPriorityClass, 2),
    K(GetThreadPriority, 1), K(SetThreadPriority, 2),
    K(CreateToolhelp32Snapshot, 2), K(Process32First, 2), K(Process32Next, 2),
    {"winmm.dll", "timeGetTime", 0, timeGetTime_}, {"winmm.dll", "timeBeginPeriod", 1, timeBeginPeriod_},
    {"winmm.dll", "timeEndPeriod", 1, timeEndPeriod_}, {"winmm.dll", "timeGetDevCaps", 2, timeGetDevCaps_},
    {"advapi32.dll", "RegOpenKeyExA", 5, RegOpenKeyExA_}, {"advapi32.dll", "RegOpenKeyA", 3, RegOpenKeyA_},
    {"advapi32.dll", "RegQueryValueExA", 6, RegQueryValueExA_}, {"advapi32.dll", "RegCloseKey", 1, RegCloseKey_},
    {"advapi32.dll", "RegCreateKeyExA", 9, RegCreateKeyExA_}, {"advapi32.dll", "RegCreateKeyA", 3, RegCreateKeyA_},
    {"advapi32.dll", "RegQueryValueA", 4, RegQueryValueA_}, {"advapi32.dll", "RegSetValueExA", 6, RegSetValueExA_},
    {"advapi32.dll", "RegSetValueA", 5, RegSetValueA_}, {"advapi32.dll", "RegDeleteValueA", 2, RegDeleteValueA_},
    {"advapi32.dll", "RegDeleteKeyA", 2, RegDeleteKeyA_}, {"advapi32.dll", "RegEnumKeyA", 4, RegEnumKeyA_},
    {"advapi32.dll", "GetFileSecurityA", 5, GetFileSecurityA_}, {"advapi32.dll", "SetFileSecurityA", 5, SetFileSecurityA_},
    {"shfolder.dll", "SHGetFolderPathA", 5, SHGetFolderPathA_},
    {"shell32.dll", "SHGetSpecialFolderPathA", 4, SHGetSpecialFolderPathA_},
    K(GetDateFormatA, 6), K(GetTimeFormatA, 6), K(GetProcessHeap, 0), K(SetEnvironmentVariableA, 2),
    K(EnumSystemLocalesA, 2), K(RaiseException, 4), K(RtlUnwind, 4), K(UnhandledExceptionFilter, 1),
    K(TerminateProcess, 2), K(CreateFileW, 7),
    {"kernel32.dll", "GetConsoleMode", 2, no_console}, {"kernel32.dll", "GetNumberOfConsoleInputEvents", 2, no_console},
    {"kernel32.dll", "PeekConsoleInputA", 4, no_console},
    {"kernel32.dll", "GetCommConfig", 3, no_comm}, {"kernel32.dll", "GetCommState", 2, no_comm},
    {"kernel32.dll", "SetCommConfig", 3, no_comm}, {"kernel32.dll", "SetCommState", 2, no_comm},
    {"kernel32.dll", "SetCommMask", 2, no_comm}, {"kernel32.dll", "SetCommTimeouts", 2, no_comm},
    {"kernel32.dll", "SetupComm", 3, no_comm}, {"kernel32.dll", "PurgeComm", 2, no_comm},
    {"kernel32.dll", "WaitCommEvent", 3, no_comm}, {"kernel32.dll", "GetOverlappedResult", 4, no_comm},
};
const int win32_api_count = sizeof win32_apis / sizeof win32_apis[0];

int rn_screen_w = 1920, rn_screen_h = 1080;

void win32_init(Ctx *c, uint32_t image_size) {
    int sw, sh;
    const char *scr = getenv("RN_SCREEN");
    if (scr && sscanf(scr, "%dx%d", &sw, &sh) == 2 && sw >= 640 && sh >= 480 && sw <= 7680 && sh <= 4320) {
        rn_screen_w = sw & ~1; rn_screen_h = sh & ~1;
    }
    st(c, SCREEN_ADDR, rn_screen_w, 4); st(c, SCREEN_ADDR + 4, rn_screen_h, 4);
    float s43 = 4.f * (float)rn_screen_h / (3.f * (float)rn_screen_w);
    if (s43 > 1.f) s43 = 1.f;
    float k[2] = {s43 / 320.f, s43};
    memcpy(G(SCREEN_ADDR + 8), k, sizeof k);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    brk_ptr = HEAP_LO;
    vtop = c->memsz;
    (void)image_size;
    st(c, PEB_ADDR + 0x08, IMAGE_BASE, 4);
    st(c, PEB_ADDR + 0x18, 0, 4);             // process heap: set by HeapCreate users
    char cmd[300];
    snprintf(cmd, sizeof cmd, "\"%s\"%s", MODULE_PATH, COMMAND_ARGS);
    put_str(c, STR_ADDR, cmd);
    for (size_t k = 0; k <= strlen(cmd); k++) st(c, STR_ADDR + 0x800 + 2 * (uint32_t)k, to_wide((uint8_t)cmd[k]), 2);
    // Environment block: "name=value\0...\0\0".
    static const char env[] = "PATH=C:\\WINDOWS\\system32;C:\\WINDOWS\0SystemRoot=C:\\WINDOWS\0windir=C:\\WINDOWS\0\0";
    memcpy(G(STR_ADDR + 0x400), env, sizeof env);
}
