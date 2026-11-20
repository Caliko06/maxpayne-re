// Guest CPU state and helpers shared by the lifted code (build/gen) and the host layer.
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>

enum { EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI };

typedef struct Ctx {
    uint32_t r[8];
    uint8_t *mem;               // guest address 0 = mem[0]; flat 32-bit space
    uint32_t memsz;
    uint32_t ret;               // return address popped by the last `ret`
    uint32_t halt, halt_at;     // halt != 0: stop everything, halt_at = guest address
    const char *why;
    uint32_t fs;                // TEB guest address
    int32_t cf, zf, sf, of, pf, df, af;
    uint32_t eflx;              // eflags bits we only store (AC, ID)
    uint32_t fcw, fsw, ftop, ftag;  // fsw keeps C0..C3 only; ftag bit i = physical st i in use
    double st[8];
    uint32_t mb;                // base of the live ABK code module the called lifted function belongs to
    uint64_t mm[8];             // MMX registers. Kept apart from st: the game's MMX code ends with emms before any x87
    uint32_t uw;                // this thread has raised an exception: a call site may see a foreign return address
    uint32_t skipped;           // 1: the last guest call did not return to its host frame (see guest_call)
} Ctx;

// Guest pages below 64 KB are never mapped: a null pointer read is a loud fault.
#define LOW 0x10000u
#ifdef RN_REF_MAP   // difftest: the pages Unicorn has no mapping for, so both CPUs stop at the same access
extern unsigned char rn_ref_unmapped[];
#define REF_GAP(a, n) (rn_ref_unmapped[(uint32_t)(a) >> 12] || rn_ref_unmapped[(uint32_t)((a) + (n) - 1) >> 12])
#define RN_BULK 0   // a bulk copy would run past a gap the reference stops at
#else
#define REF_GAP(a, n) 0
#define RN_BULK 1
#endif
#define CHK(a, n, at) do { if ((uint32_t)((a) - LOW) > c->memsz - LOW - (n) || REF_GAP(a, n)) { c->halt_at = (at); c->why = "memory fault"; goto trap; } } while (0)
#define TRAP(at, msg) do { c->halt_at = (at); c->why = (msg); goto trap; } while (0)

static inline uint32_t ld(Ctx *c, uint32_t a, int n) { uint32_t v = 0; memcpy(&v, c->mem + a, n); return v; }
static inline void st(Ctx *c, uint32_t a, uint32_t v, int n) { memcpy(c->mem + a, &v, n); }
static inline float ldf(Ctx *c, uint32_t a) { float v; memcpy(&v, c->mem + a, 4); return v; }
static inline double ldd(Ctx *c, uint32_t a) { double v; memcpy(&v, c->mem + a, 8); return v; }
static inline void stf(Ctx *c, uint32_t a, float v) { memcpy(c->mem + a, &v, 4); }
static inline void std_(Ctx *c, uint32_t a, double v) { memcpy(c->mem + a, &v, 8); }
static inline uint64_t ld64(Ctx *c, uint32_t a) { uint64_t v; memcpy(&v, c->mem + a, 8); return v; }
static inline void st64(Ctx *c, uint32_t a, uint64_t v) { memcpy(c->mem + a, &v, 8); }
static inline int par(uint32_t v) { return !__builtin_parity(v & 0xffu); }

// Calls any guest or host address. Defined in host/dispatch.c.
void rn_call(Ctx *c, uint32_t target);
void rn_probe(Ctx *c, uint32_t entry);        // entry probe of lifted functions listed in RN_LIFT_PROBES (debug)
void rn_trace_step(Ctx *c, uint32_t addr);    // before every instruction of images lifted with RN_LIFT_TRACE (debug)
// ABK code modules lifted ahead of time (tools/recomp/abk.py -> build/gen/abk.c). Defined in host/abk.c.
typedef struct { uint32_t id, size, nfn; const uint32_t *off; void (*const *fn)(Ctx *); } RnMod;
void (*rn_mod_find(Ctx *c, uint32_t target, uint32_t *mb))(Ctx *);
void rn_spin(void);            // a lifted `while (flag);` gives the other threads their turn (host/threads.c)
uint64_t rn_rdtsc(Ctx *c);
void rn_cpuid(Ctx *c, uint32_t *a, uint32_t *b, uint32_t *cc, uint32_t *d);

// ---- x87 ----------------------------------------------------------------
// Registers are doubles: precision control 24 bits rounds to float, 53 and 64 bits keep double.
#define FC0 0x100u
#define FC2 0x400u
#define FC3 0x4000u
static inline double *fr(Ctx *c, int i) { return &c->st[(c->ftop + i) & 7]; }
static inline double fround(Ctx *c, double v) { return ((c->fcw >> 8) & 3) == 0 ? (double)(float)v : v; }
// NaN results follow the x87 rules (Intel SDM table 4-7), not the host's: an invalid operation on
// non-NaN operands gives the "real indefinite" (-NaN); NaN operands propagate quieted, the larger
// significand wins and a tie takes the positive one.
static inline uint64_t dbits(double v) { uint64_t b; memcpy(&b, &v, 8); return b; }
static inline double bitsd(uint64_t b) { double v; memcpy(&v, &b, 8); return v; }
static inline double fnan2(double a, double b) {
    if (a == a) return bitsd(dbits(b) | 0x8000000000000ull);
    if (b == b) return bitsd(dbits(a) | 0x8000000000000ull);
    uint64_t x = dbits(a) | 0x8000000000000ull, y = dbits(b) | 0x8000000000000ull;
    uint64_t mx = x & 0xfffffffffffffull, my = y & 0xfffffffffffffull;
    if (mx != my) return bitsd(mx > my ? x : y);
    return bitsd(x < y ? x : y);     // same significand: the one without the sign bit
}
#ifdef RN_REF_QEMU5
#define FIE 0x00u     // difftest only: QEMU 5 (Unicorn) never raises IE; the SDM does
#else
#define FIE 0x01u     // invalid operation
#endif
#define FZE 0x04u     // zero divide
#define FSF 0x40u     // stack fault
// Results of x87 arithmetic: NaN rules above; an invalid operation on non-NaN operands also sets IE.
// The x87 helpers run once per guest FPU op: clang -O1 kept them out of line (RN_PROF: fx2 + fist ~14% of a level load).
#define RN_AI __attribute__((always_inline))
static inline RN_AI double fx2(Ctx *c, double a, double b, double r) {
    if (r == r) return r;
    if (a != a || b != b) return fnan2(a, b);
    c->fsw |= FIE;
    return bitsd(0xfff8000000000000ull);
}
static inline RN_AI double fx1(Ctx *c, double a, double r) {
    if (r == r) return r;
    if (a != a) return bitsd(dbits(a) | 0x8000000000000ull);
    c->fsw |= FIE;
    return bitsd(0xfff8000000000000ull);
}
// x87 rounding control other than nearest. D3DX runs its pixel loops in chop mode (FUN_1043ecc6: fcw |= 0xc00 at 0x1043ecd0).
// WebAssembly has no rounding mode, so the nearest result moves by one ulp when its exact error says so.
#define RC_ON __builtin_expect((c->fcw & 0xc00u) != 0, 0)
static double frc_move(Ctx *c, double r, double err) {
    switch ((c->fcw >> 10) & 3) {
    case 1: return err < 0 ? nextafter(r, -INFINITY) : r;
    case 2: return err > 0 ? nextafter(r, INFINITY) : r;
    default: return (r > 0 && err < 0) || (r < 0 && err > 0) ? nextafter(r, 0.0) : r;
    }
}
static __attribute__((noinline)) double frc_add(Ctx *c, double a, double b) {
    double s = a + b, bb = s - a;
    if (!isfinite(s)) return s;
    if (s == 0 && ((c->fcw >> 10) & 3) == 1 && signbit(a) != signbit(b)) return -0.0;   // x + (-x) rounding down
    return frc_move(c, s, (a - (s - bb)) + (b - bb));                                   // TwoSum: the exact error
}
static __attribute__((noinline)) double frc_mul(Ctx *c, double a, double b) {
    double r = a * b;
    return isfinite(r) ? frc_move(c, r, fma(a, b, -r)) : r;
}
static __attribute__((noinline)) double frc_div(Ctx *c, double a, double b) {
    double r = a / b;
    if (!isfinite(r) || b == 0) return r;
    double e = fma(-r, b, a);
    return frc_move(c, r, signbit(b) ? -e : e);
}
static __attribute__((noinline)) double frc_sqrt(Ctx *c, double a) {
    double r = sqrt(a);
    return isfinite(r) ? frc_move(c, r, -fma(r, r, -a)) : r;
}
static __attribute__((noinline)) float frc_float(Ctx *c, double v) {
    float f = (float)v;
    if (!isfinite(v)) return f;
    double err = v - (double)f;
    switch ((c->fcw >> 10) & 3) {
    case 1: return err < 0 ? nextafterf(f, -INFINITY) : f;
    case 2: return err > 0 ? nextafterf(f, INFINITY) : f;
    default: return (f > 0 && err < 0) || (f < 0 && err > 0) ? nextafterf(f, 0.0f) : f;
    }
}
static inline RN_AI float fst32(Ctx *c, double v) { return RC_ON ? frc_float(c, v) : (float)v; }
static inline RN_AI double fdiv_(Ctx *c, double a, double b) {
#ifdef RN_REF_QEMU5
    if (b == 0) c->fsw |= FZE;      // difftest only: the reference CPU flags every x/0, and never IE
#else
    if (b == 0 && a == a && a != 0 && !isinf(a)) c->fsw |= FZE;     // finite non-zero / 0
#endif
    return fx2(c, a, b, RC_ON ? frc_div(c, a, b) : a / b);
}
#define FADD(a, b) fx2(c, (a), (b), RC_ON ? frc_add(c, (a), (b)) : (a) + (b))
#define FSUB(a, b) fx2(c, (a), (b), RC_ON ? frc_add(c, (a), -(b)) : (a) - (b))
#define FMUL(a, b) fx2(c, (a), (b), RC_ON ? frc_mul(c, (a), (b)) : (a) * (b))
#define FDIV(a, b) fdiv_(c, (a), (b))
#define FSQRT(a) (RC_ON ? frc_sqrt(c, (a)) : sqrt(a))

// Stack faults are masked on Windows: an empty register reads as the "real indefinite" (-NaN), and a push
// onto a full register loads it too. Keeping every empty slot at that value gives both for free.
static inline double findef(void) { uint64_t b = 0xfff8000000000000ull; double v; memcpy(&v, &b, 8); return v; }
static inline void fpush(Ctx *c, double v) {
    c->ftop = (c->ftop - 1) & 7;
    if (c->ftag & (1u << c->ftop)) { v = findef(); c->fsw |= FIE | FSF | 0x200u; }
    c->st[c->ftop] = v;
    c->ftag |= 1u << c->ftop;
}
static inline void fpop(Ctx *c) { c->st[c->ftop] = findef(); c->ftag &= ~(1u << c->ftop); c->ftop = (c->ftop + 1) & 7; }
static inline void ffree(Ctx *c, int i) { *fr(c, i) = findef(); c->ftag &= ~(1u << ((c->ftop + i) & 7)); }
static inline void finit(Ctx *c) {
    c->fcw = 0x37f; c->fsw = 0; c->ftop = 0; c->ftag = 0;
    for (int k = 0; k < 8; k++) c->st[k] = findef();
}
// x87 loads of float/double memory turn a signaling NaN into a quiet one (Intel SDM, FLD).
static inline double ldfx(Ctx *c, uint32_t a) {
    uint32_t b = ld(c, a, 4);
    float f;
    if ((b & 0x7f800000u) == 0x7f800000u && (b & 0x7fffffu) && !(b & 0x400000u)) { b |= 0x400000u; c->fsw |= FIE; }
    memcpy(&f, &b, 4);
    return f;
}
static inline double lddx(Ctx *c, uint32_t a) {
    uint64_t b = ld64(c, a);
    if ((b & 0x7ff0000000000000ull) == 0x7ff0000000000000ull && (b & 0xfffffffffffffull) && !(b & 0x8000000000000ull)) {
        b |= 0x8000000000000ull;
        c->fsw |= FIE;
    }
    return bitsd(b);
}
static inline void fset(Ctx *c, int i, double v) { *fr(c, i) = fround(c, v); }
static inline void fucom(Ctx *c, double a, double b) {
    c->fsw &= ~(FC0 | FC2 | FC3);
    if (a != a || b != b) c->fsw |= FC0 | FC2 | FC3;
    else if (a < b) c->fsw |= FC0;
    else if (a == b) c->fsw |= FC3;
}
static inline void fcom(Ctx *c, double a, double b) {
    c->fsw &= ~(FC0 | FC2 | FC3);
    if (a != a || b != b) c->fsw |= FC0 | FC2 | FC3 | FIE;
    else if (a < b) c->fsw |= FC0;
    else if (a == b) c->fsw |= FC3;
}
static inline uint16_t fstsw(Ctx *c) { return (uint16_t)((c->fsw & ~0x3800u) | (c->ftop << 11)); }
static inline RN_AI double frnd(Ctx *c, double v) {
    switch ((c->fcw >> 10) & 3) {
    case 0: return nearbyint(v);
    case 1: return floor(v);
    case 2: return ceil(v);
    default: return trunc(v);
    }
}
// Integer store: out of range gives the "integer indefinite" value, like the FPU with IM masked.
static inline RN_AI int64_t fist(Ctx *c, double v, int bits, int truncate) {
    double r = truncate ? trunc(v) : frnd(c, v);
    double lim = (double)((uint64_t)1 << (bits - 1));     // 2^(bits-1), exact
    if (!(r >= -lim && r < lim)) { c->fsw |= FIE; return (int64_t)((uint64_t)1 << (bits - 1)); }
    return (int64_t)r;
}
// FPREM/FPREM1 (Intel SDM): remainder of st0/st1 with the quotient truncated (FPREM) or rounded to
// nearest (FPREM1); C0, C3, C1 get quotient bits 2, 0, 1; C2 = 0 (the reduction is complete in one step:
// doubles never have the 64-bit exponent difference that makes the x87 stop early).
static inline void fprem(Ctx *c, int nearest) {
    double a = *fr(c, 0), b = *fr(c, 1), r;
    c->fsw &= ~(FC0 | 0x200u | FC2 | FC3);
    if (a != a || b != b || isinf(a) || b == 0) { *fr(c, 0) = fx2(c, a, b, NAN); return; }
    unsigned q;
    if (nearest) { int qi; r = remquo(a, b, &qi); q = (unsigned)(qi < 0 ? -qi : qi); }
    else {
        r = fmod(a, b);
        double r8 = fmod(a, 8 * fabs(b));
        q = (unsigned)nearbyint(fabs((r8 - r) / b));   // quotient mod 8
    }
    *fr(c, 0) = r;
    if (q & 4) c->fsw |= FC0;
    if (q & 1) c->fsw |= FC3;
    if (q & 2) c->fsw |= 0x200u;
}
static inline double ld80(Ctx *c, uint32_t a) {
    uint64_t m; uint16_t se;
    memcpy(&m, c->mem + a, 8); memcpy(&se, c->mem + a + 8, 2);
    int e = se & 0x7fff; double s = (se & 0x8000) ? -1.0 : 1.0;
    // Unsupported encodings (integer bit clear with a non-zero exponent: unnormal, pseudo-NaN/infinity)
    // make any x87 operation invalid (SDM 8.2.2): hold them as the real indefinite.
    if (e != 0 && !(m >> 63)) return findef();
    if (e == 0 && m == 0) return s * 0.0;
    if (e == 0x7fff) {
        if (!(m << 1)) return s * INFINITY;
        return bitsd((uint64_t)(se >> 15) << 63 | 0x7ff0000000000000ull | ((m >> 11) & 0xfffffffffffffull) | 0x8000000000000ull);
    }
    return s * ldexp((double)m, e - 16383 - 63);
}
static inline void st80(Ctx *c, uint32_t a, double v) {
    uint64_t m = 0; uint16_t se = signbit(v) ? 0x8000 : 0;
    if (isnan(v)) { se |= 0x7fff; m = 0xc000000000000000ull; }
    else if (isinf(v)) { se |= 0x7fff; m = 0x8000000000000000ull; }
    else if (v != 0) {
        int e; double f = frexp(fabs(v), &e);           // f in [0.5,1)
        m = (uint64_t)ldexp(f, 64); se |= (uint16_t)(e - 1 + 16383);
    }
    memcpy(c->mem + a, &m, 8); memcpy(c->mem + a + 8, &se, 2);
}
static inline void fxam(Ctx *c) {
    double v = *fr(c, 0);
    uint32_t s = signbit(v) ? 0x200u : 0;      // C1 = sign
    c->fsw &= ~(FC0 | 0x200u | FC2 | FC3);
    if (!(c->ftag & (1u << c->ftop))) c->fsw |= FC3 | FC0 | s;
    else if (isnan(v)) c->fsw |= FC0 | s;
    else if (isinf(v)) c->fsw |= FC2 | FC0 | s;
    else if (v == 0) c->fsw |= FC3 | s;
    else c->fsw |= FC2 | s;
}

// MMX (Intel SDM vol. 2): packed lanes of 8, 16 or 32 bits in a uint64_t. No flags. Every MMX instruction but emms
// sets TOP = 0 and marks all x87 registers in use; emms marks them empty.
static inline void mmx_enter(Ctx *c) { c->ftop = 0; c->ftag = 0xff; }
static inline int64_t sat(int64_t v, int64_t lo, int64_t hi) { return v < lo ? lo : v > hi ? hi : v; }
#define MMX_LANES(name, n, expr) static inline uint64_t name(uint64_t A, uint64_t B) { \
    uint64_t r = 0; \
    for (int k = 0; k < 64 / n; k++) { \
        int64_t a = (int##n##_t)(A >> (k * n)), b = (int##n##_t)(B >> (k * n)); \
        uint64_t ua = (uint##n##_t)(A >> (k * n)), ub = (uint##n##_t)(B >> (k * n)); \
        (void)a; (void)b; (void)ua; (void)ub; \
        r |= (uint64_t)(uint##n##_t)(expr) << (k * n); \
    } \
    return r; }
MMX_LANES(paddb, 8, ua + ub) MMX_LANES(paddw, 16, ua + ub) MMX_LANES(paddd, 32, ua + ub)
MMX_LANES(psubb, 8, ua - ub) MMX_LANES(psubw, 16, ua - ub) MMX_LANES(psubd, 32, ua - ub)
MMX_LANES(paddsb, 8, sat(a + b, -128, 127)) MMX_LANES(paddsw, 16, sat(a + b, -32768, 32767))
MMX_LANES(psubsb, 8, sat(a - b, -128, 127)) MMX_LANES(psubsw, 16, sat(a - b, -32768, 32767))
MMX_LANES(paddusb, 8, sat((int64_t)(ua + ub), 0, 255)) MMX_LANES(paddusw, 16, sat((int64_t)(ua + ub), 0, 65535))
MMX_LANES(psubusb, 8, sat((int64_t)ua - (int64_t)ub, 0, 255)) MMX_LANES(psubusw, 16, sat((int64_t)ua - (int64_t)ub, 0, 65535))
MMX_LANES(pmullw, 16, a * b) MMX_LANES(pmulhw, 16, (a * b) >> 16)
MMX_LANES(pcmpeqb, 8, a == b ? -1 : 0) MMX_LANES(pcmpeqw, 16, a == b ? -1 : 0) MMX_LANES(pcmpeqd, 32, a == b ? -1 : 0)
MMX_LANES(pcmpgtb, 8, a > b ? -1 : 0) MMX_LANES(pcmpgtw, 16, a > b ? -1 : 0) MMX_LANES(pcmpgtd, 32, a > b ? -1 : 0)
static inline uint64_t pand(uint64_t a, uint64_t b) { return a & b; }
static inline uint64_t pandn(uint64_t a, uint64_t b) { return ~a & b; }
static inline uint64_t por(uint64_t a, uint64_t b) { return a | b; }
static inline uint64_t pxor(uint64_t a, uint64_t b) { return a ^ b; }
static inline uint64_t pmaddwd(uint64_t A, uint64_t B) {
    uint64_t r = 0;
    for (int k = 0; k < 2; k++) {
        int32_t p = (int32_t)(int16_t)(A >> 32 * k) * (int16_t)(B >> 32 * k);
        int32_t q = (int32_t)(int16_t)(A >> (32 * k + 16)) * (int16_t)(B >> (32 * k + 16));
        r |= (uint64_t)((uint32_t)p + (uint32_t)q) << 32 * k;
    }
    return r;
}
// Shifts: a count above the lane width gives 0 (logical) or the sign in every bit (arithmetic).
#define MMX_SHIFT(name, n, expr) static inline uint64_t name(uint64_t A, uint64_t cnt) { \
    uint64_t r = 0; \
    for (int k = 0; k < 64 / n; k++) { \
        uint64_t ua = (uint##n##_t)(A >> (k * n)); int64_t a = (int##n##_t)ua; (void)a; (void)ua; \
        r |= (uint64_t)(uint##n##_t)(expr) << (k * n); \
    } \
    return r; }
MMX_SHIFT(psllw, 16, cnt > 15 ? 0 : ua << cnt) MMX_SHIFT(pslld, 32, cnt > 31 ? 0 : ua << cnt)
MMX_SHIFT(psrlw, 16, cnt > 15 ? 0 : ua >> cnt) MMX_SHIFT(psrld, 32, cnt > 31 ? 0 : ua >> cnt)
MMX_SHIFT(psraw, 16, a >> (cnt > 15 ? 15 : cnt)) MMX_SHIFT(psrad, 32, a >> (cnt > 31 ? 31 : cnt))
static inline uint64_t psllq(uint64_t a, uint64_t cnt) { return cnt > 63 ? 0 : a << cnt; }
static inline uint64_t psrlq(uint64_t a, uint64_t cnt) { return cnt > 63 ? 0 : a >> cnt; }
// Unpack: interleave the low (or high) halves of A and B, lanes of n bits, A first.
static inline uint64_t unpack(uint64_t A, uint64_t B, int n, int high) {
    uint64_t r = 0, m = (n == 32 ? 0xffffffffull : (1ull << n) - 1);
    for (int k = 0; k < 32 / n; k++) {
        int s = (high ? 32 : 0) + k * n;
        r |= ((A >> s) & m) << (2 * k * n) | ((B >> s) & m) << ((2 * k + 1) * n);
    }
    return r;
}
static inline uint64_t punpcklbw(uint64_t a, uint64_t b) { return unpack(a, b, 8, 0); }
static inline uint64_t punpckhbw(uint64_t a, uint64_t b) { return unpack(a, b, 8, 1); }
static inline uint64_t punpcklwd(uint64_t a, uint64_t b) { return unpack(a, b, 16, 0); }
static inline uint64_t punpckhwd(uint64_t a, uint64_t b) { return unpack(a, b, 16, 1); }
static inline uint64_t punpckldq(uint64_t a, uint64_t b) { return unpack(a, b, 32, 0); }
static inline uint64_t punpckhdq(uint64_t a, uint64_t b) { return unpack(a, b, 32, 1); }
// Pack with saturation: the lanes of A, then the lanes of B, each narrowed to half width.
static inline uint64_t pack(uint64_t A, uint64_t B, int n, int64_t lo, int64_t hi) {
    uint64_t r = 0, m = (1ull << (n / 2)) - 1;
    int lanes = 64 / n;
    for (int k = 0; k < 2 * lanes; k++) {
        uint64_t src = k < lanes ? A : B;
        int j = k % lanes;
        int64_t v = n == 16 ? (int64_t)(int16_t)(src >> (j * 16)) : (int64_t)(int32_t)(src >> (j * 32));
        r |= ((uint64_t)sat(v, lo, hi) & m) << (k * n / 2);
    }
    return r;
}
static inline uint64_t packsswb(uint64_t a, uint64_t b) { return pack(a, b, 16, -128, 127); }
static inline uint64_t packuswb(uint64_t a, uint64_t b) { return pack(a, b, 16, 0, 255); }
static inline uint64_t packssdw(uint64_t a, uint64_t b) { return pack(a, b, 32, -32768, 32767); }

// Lifted code (build/gen, RN_LIFTED) reads the memory base and size from locals of each function: a store through a
// pointer may alias the Ctx for the compiler, so c->mem and c->memsz were reloaded at every guest access.
#ifdef RN_LIFTED
#undef CHK
#define CHK(a, n, at) do { if ((uint32_t)((a) - LOW) > rn_sz - (n) || REF_GAP(a, n)) { c->halt_at = (at); c->why = "memory fault"; goto trap; } } while (0)
static inline uint32_t ld_m(const uint8_t *m, uint32_t a, int n) { uint32_t v = 0; memcpy(&v, m + a, n); return v; }
static inline void st_m(uint8_t *m, uint32_t a, uint32_t v, int n) { memcpy(m + a, &v, n); }
#define ld(ctx, a, n) ld_m(rn_m, (a), (n))
#define st(ctx, a, v, n) st_m(rn_m, (a), (v), (n))
#endif
