// difftest runtime: the lifted images as a library. Imports stop the run so Unicorn and the lifted code
// can be compared at the same point; host/cpu.c answers cpuid for both.
#include "rt.h"

extern const uint32_t rn_fn_count;
extern const uint32_t rn_fn_addr[];
extern void (*const rn_fn_ptr[])(Ctx *);

void rn_call(Ctx *c, uint32_t t) {
    if (t >= 0xe0000000u) { c->halt = 2; c->halt_at = t; c->why = "import"; return; }
    uint32_t lo = 0, hi = rn_fn_count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (rn_fn_addr[mid] < t) lo = mid + 1; else hi = mid;
    }
    if (lo < rn_fn_count && rn_fn_addr[lo] == t) { rn_fn_ptr[lo](c); return; }
    c->halt = 1; c->halt_at = t; c->why = "no lifted function";
}
uint64_t rn_rdtsc(Ctx *c) { (void)c; return 0x123456789abcull; }

// Random test data without signaling-NaN floats/doubles (QEMU 5 keeps an sNaN on fld; the SDM quiets it).
#ifdef RN_REF_MAP
unsigned char rn_ref_unmapped[1u << 20];
#endif

// Half of the dwords become pointers into [lo, lo + span), so a function can follow pointer fields past the first level.
void rn_mix_pointers(uint8_t *p, uint32_t n, uint32_t lo, uint32_t span) {
    for (uint32_t o = 0; o + 4 <= n; o += 4) {
        uint32_t v;
        memcpy(&v, p + o, 4);
        if (v & 1) {
            v = lo + (v >> 2) % span;
            v &= ~3u;
            memcpy(p + o, &v, 4);
        }
    }
}

void rn_quiet_snans(uint8_t *p, uint32_t n) {
    for (uint32_t o = 0; o + 4 <= n; o += 4) {
        uint32_t v;
        memcpy(&v, p + o, 4);
        if ((v & 0x7f800000u) == 0x7f800000u && (v & 0x7fffffu) && !(v & 0x400000u)) p[o + 2] |= 0x40;
        if (o % 8 == 4) {
            uint64_t q;
            memcpy(&q, p + o - 4, 8);
            if ((q & 0x7ff0000000000000ull) == 0x7ff0000000000000ull && (q & 0xfffffffffffffull) && !(q & 0x8000000000000ull))
                p[o + 2] |= 0x08;
        }
    }
}

// patches.HOST_FUNCS answer "not handled" here: the oracle compares the lifted bodies.
int rn_d3dx_convert(Ctx *c) { (void)c; return 0; }
int rn_ftol(Ctx *c) { (void)c; return 0; }
int rn_jpeg_idct(Ctx *c) { (void)c; return 0; }
int rn_jpeg_onepass(Ctx *c) { (void)c; return 0; }
int rn_jpeg_ycc(Ctx *c) { (void)c; return 0; }
int rn_jpeg_row(Ctx *c) { (void)c; return 0; }
int rn_jpeg_huff(Ctx *c) { (void)c; return 0; }
int rn_copy_with_scale(Ctx *c) { (void)c; return 0; }
int rn_grey_alpha(Ctx *c) { (void)c; return 0; }
int rn_bilinear_scale(Ctx *c) { (void)c; return 0; }
int rn_new_pixels(Ctx *c) { (void)c; return 0; }
int rn_memory_copy(Ctx *c) { (void)c; return 0; }
int rn_ldb_import(Ctx *c) { (void)c; return 0; }
int rn_map_readback(Ctx *c) { (void)c; return 0; }   // host/d3d8 stand-in: the lifted body is what difftest compares
int rn_longjmp(Ctx *c) { (void)c; return 0; }
int rn_cheats_key(Ctx *c) { (void)c; return 0; }
void rn_cheats_game_key(Ctx *c) { (void)c; }
void rn_spin(void) {}   // the reference has no other thread
