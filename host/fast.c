// Host functions that answer hot guest entries (tools/recomp/patches.py HOST_FUNCS). Each returns 1 when it did the work
// of the whole function (result, return address and stack as the original's ret leaves them), 0 to run the lifted body.
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include "rt.h"
#include "host.h"

void f_1043a907(Ctx *c);
void f_10210920(Ctx *c);
void f_1020a080(Ctx *c);
void f_1020d570(Ctx *c);
void f_102045e0(Ctx *c);
void f_1020b370(Ctx *c);
void f_10203be0(Ctx *c);
void f_10120e20(Ctx *c);
void f_10120ee0(Ctx *c);
void f_10203d20(Ctx *c);
void f_10016730(Ctx *c);
void f_5dc360(Ctx *c);

static inline void guest_ret(Ctx *c, uint32_t pop) { c->ret = ld(c, c->r[ESP], 4); c->r[ESP] += 4 + pop; }
// Sizes in 64 bits: a guest width or row count may be anything. Each stand-in returns 0 on a bad range (the original reports the
// fault) and on overlapping ranges (the original reads a whole pixel or row before it writes, a host loop would not).
static inline int guest_ok(Ctx *c, uint64_t p, uint64_t n) { return p >= LOW && p + n <= c->memsz; }
static inline int overlap(uint64_t a, uint64_t an, uint64_t b, uint64_t bn) { return a < b + bn && b < a + an; }

// D3DX surface descriptor, driver FUN_1043ed08 (fields) and FUN_10441ff8 (one class per format).
enum { D_FORMAT = 0x4 /* 0x1043ed2b */, D_COLORKEY = 0x10, D_DATA = 0x18, D_DITHER = 0x2c, D_ROW = 0x1048, D_SLICE = 0x104c,
       D_W = 0x1050, D_H = 0x1054, D_D = 0x1058 };
enum { FMT_A8R8G8B8 = 0x15, FMT_X8R8G8B8 = 0x16 };  // D3DFMT, case labels of FUN_10441ff8
#define NO_DITHER_TABLE 0x10476f78u                 // FUN_1043ed08: table when the filter has no D3DX_FILTER_DITHER

// FUN_1043a907(ecx = {src desc, dst desc}): same-size convert. Original per pixel, A8R8G8B8 to X8R8G8B8:
// load 0x10440268 byte*(1/255.f), store 0x1043f137 round(f*255 + table), r<<16|g<<8|b with the top byte 0.
int rn_d3dx_convert(Ctx *c) {
    gil_slice();      // no API call inside the decode loops: the sound thread's turn (RN_HOLD showed 100-165 ms holds here)
    static int check = -1, inside;
    if (check < 0) check = getenv("RN_FASTCHECK") != NULL;
    if (inside) return 0;
    uint32_t ctx = c->r[ECX], src = ld(c, ctx, 4), dst = ld(c, ctx + 4, 4);
    uint32_t w = ld(c, dst + D_W, 4), h = ld(c, dst + D_H, 4), d = ld(c, dst + D_D, 4);
    if (ld(c, src + D_FORMAT, 4) != FMT_A8R8G8B8 || ld(c, dst + D_FORMAT, 4) != FMT_X8R8G8B8) return 0;
    if (ld(c, src + D_COLORKEY, 4) || ld(c, dst + D_DITHER, 4) != NO_DITHER_TABLE) return 0;
    if (ld(c, src + D_W, 4) != w || ld(c, src + D_H, 4) != h || ld(c, src + D_D, 4) != d) return 0;
    uint32_t sp = ld(c, src + D_DATA, 4), sr = ld(c, src + D_ROW, 4), ss = ld(c, src + D_SLICE, 4);
    uint32_t dp = ld(c, dst + D_DATA, 4), dr = ld(c, dst + D_ROW, 4), ds = ld(c, dst + D_SLICE, 4);
    uint64_t need = (uint64_t)w * 4;
    if (h && d) {   // every byte in guest memory, or the original reports the fault
        uint64_t send = sp + (h - 1) * (uint64_t)sr + (d - 1) * (uint64_t)ss + need;
        uint64_t dend = dp + (h - 1) * (uint64_t)dr + (d - 1) * (uint64_t)ds + need;
        if (sp < LOW || dp < LOW || send > c->memsz || dend > c->memsz || overlap(sp, send - sp, dp, dend - dp)) return 0;
    }
    // The store row 0x1043f137 saves the control word through FUN_1043ecc6: dword at 0x10488374 = ecx (dither table) high half + FCW.
    uint32_t saved = (ld(c, dst + D_DITHER, 4) & 0xffff0000u) | (c->fcw & 0xffffu);
    uint32_t *want = check ? malloc((size_t)need * h * d + 4) : NULL;
    uint32_t *out = want;
    for (uint32_t z = 0; z < d; z++)
        for (uint32_t y = 0; y < h; y++) {
            const uint8_t *s = c->mem + sp + y * sr + z * ss;
            uint8_t *t = c->mem + dp + y * dr + z * ds;
            if (want) { memcpy(out, s, need); for (uint32_t x = 0; x < w; x++) out[x] &= 0xffffffu; out += w; continue; }
            for (uint32_t x = 0; x < w; x++) { uint32_t v; memcpy(&v, s + 4 * x, 4); v &= 0xffffffu; memcpy(t + 4 * x, &v, 4); }
        }
    if (want) {     // RN_FASTCHECK: the lifted original writes the texture; this compares it to the host result.
        static unsigned calls, bad;
        inside = 1; f_1043a907(c); inside = 0;
        out = want;
        int diff = 0;
        for (uint32_t z = 0; z < d; z++)
            for (uint32_t y = 0; y < h; y++, out += w)
                diff |= memcmp(out, c->mem + dp + y * dr + z * ds, need) != 0;
        if (w && h && d) diff |= ld(c, 0x10488374u, 4) != saved;
        calls++; bad += diff;
        if (diff || calls % 64 == 1) rn_log("fastcheck d3dx convert: %u calls, %u differ (last %ux%u)", calls, bad, w, h);
        free(want);
        return 1;
    }
    if (w && h && d) st(c, 0x10488374u, saved, 4);
    c->r[EAX] = 0;  // 0x1043a99c
    guest_ret(c, 0);
    return 1;
}

// msvcrt _ftol 0x10801f14: truncate ST0, pop, 64-bit result in edx:eax.
int rn_ftol(Ctx *c) {
    int64_t v = fist(c, *fr(c, 0), 64, 1);
    fpop(c);
    uint32_t ah = ((c->fcw >> 8) & 0xffu) | 0xc;    // flags of `or ah, 0xc` 0x10801f23, the last flag writer
    c->cf = c->of = c->af = 0; c->zf = 0; c->sf = (ah >> 7) & 1; c->pf = par(ah);
    c->r[EAX] = (uint32_t)v;
    c->r[EDX] = (uint32_t)((uint64_t)v >> 32);
    guest_ret(c, 0);
    return 1;
}

static inline float qf(uint32_t bits) { float f; memcpy(&f, &bits, 4); return f; }

#define K184_BITS 0x3fec835e
#define K261_BITS 0x40273d75
#define K108_BITS 0x3f8a8bd4
// libjpeg 6a jpeg_idct_float as compiled in grphmfc 0x10210920, in the same order: T is what an x87 register holds (double at
// 53-bit precision, float at 24-bit) and F() is an fstp to a 4-byte stack slot or workspace, so the rounding is the original's.
#define IDCT_BODY(NAME, T)                                                                                                  \
static void NAME(const int16_t *in, const uint8_t *qb, uint8_t *const out[8], const uint8_t *range) {                       \
    const float K2 = qf(0x3fb504f3) /* 0x10212898 */, K184 = qf(K184_BITS) /* 0x10212894 */,                                \
                K261 = qf(K261_BITS) /* 0x10212890 */, K108 = qf(K108_BITS) /* 0x1021288c */;                               \
    float q[64], ws[64];                                                                                                    \
    memcpy(q, qb, sizeof q);                                                                                                \
    for (int i = 0; i < 8; i++, in++) {                                                                                     \
        float *w = ws + i;                                                                                                  \
        const float *qq = q + i;                                                                                            \
        if (!(in[8] | in[16] | in[24] | in[32] | in[40] | in[48] | in[56])) {   /* 0x10210985 */                            \
            float dc = (float)((T)in[0] * (T)qq[0]);                                                                        \
            w[0] = w[8] = w[16] = w[24] = w[32] = w[40] = w[48] = w[56] = dc;                                               \
            continue;                                                                                                       \
        }                                                                                                                   \
        T a = (T)in[0] * (T)qq[0];                                                                                          \
        float f28 = (float)((T)qq[16] * (T)in[16]);                                                                         \
        T c2 = (T)qq[32] * (T)in[32];                                                                                       \
        float f20 = (float)((T)qq[48] * (T)in[48]);                                                                         \
        float f18 = (float)(c2 + a);                                                                                        \
        T t11 = a - c2;                                                                                                     \
        T t13 = (T)f20 + (T)f28;                                                                                            \
        float f14 = (float)(((T)f28 - (T)f20) * (T)K2 - t13);                                                               \
        float f30 = (float)(t13 + (T)f18);                                                                                  \
        f20 = (float)((T)f18 - t13);                                                                                        \
        f28 = (float)((T)f14 + t11);                                                                                        \
        T t2 = t11 - (T)f14;                                                                                                \
        T v1 = (T)qq[8] * (T)in[8], v3 = (T)qq[24] * (T)in[24], v5 = (T)qq[40] * (T)in[40];                                 \
        float f1c = (float)((T)qq[56] * (T)in[56]);                                                                         \
        float f10 = (float)(v5 + v3);                                                                                       \
        f18 = (float)(v5 - v3);                                                                                             \
        f14 = (float)((T)f1c + v1);                                                                                         \
        T z12 = v1 - (T)f1c;                                                                                                \
        f1c = (float)((T)f14 + (T)f10);                                                                                     \
        T z5 = (z12 + (T)f18) * (T)K184;                                                                                    \
        float f2c = (float)(z5 - (T)f18 * (T)K261 - (T)f1c);                                                                \
        f14 = (float)(((T)f14 - (T)f10) * (T)K2 - (T)f2c);                                                                  \
        f18 = (float)(z12 * (T)K108 - z5 + (T)f14);                                                                         \
        w[0] = (float)((T)f1c + (T)f30);                                                                                    \
        w[56] = (float)((T)f30 - (T)f1c);                                                                                   \
        w[8] = (float)((T)f2c + (T)f28);                                                                                    \
        w[48] = (float)((T)f28 - (T)f2c);                                                                                   \
        w[16] = (float)((T)f14 + t2);                                                                                       \
        w[40] = (float)(t2 - (T)f14);                                                                                       \
        w[32] = (float)((T)f18 + (T)f20);                                                                                   \
        w[24] = (float)((T)f20 - (T)f18);                                                                                   \
    }                                                                                                                       \
    for (int i = 0; i < 8; i++) {                                                                                           \
        const float *w = ws + 8 * i;                                                                                        \
        uint8_t *o = out[i];                                                                                                \
        if (w[1] == 0 && w[2] == 0 && w[3] == 0 && w[4] == 0 && w[5] == 0 && w[6] == 0 && w[7] == 0) {                      \
            /* A row with no AC terms (74% of the rows of part1_level1): every step below is then exact and gives w[0]. */      \
            uint8_t v = OUT((T)w[0]);                                                                                       \
            o[0] = o[1] = o[2] = o[3] = o[4] = o[5] = o[6] = o[7] = v;                                                      \
            continue;                                                                                                       \
        }                                                                                                                   \
        T A = (T)w[4] + (T)w[0];                                                                                            \
        float f24 = (float)((T)w[0] - (T)w[4]);                                                                             \
        T B = (T)w[6] + (T)w[2];                                                                                            \
        float f14 = (float)(((T)w[2] - (T)w[6]) * (T)K2 - B);                                                               \
        float f30 = (float)(A + B);                                                                                         \
        T D = A - B;                                                                                                        \
        T E = (T)f14 + (T)f24;                                                                                              \
        float f20 = (float)((T)f24 - (T)f14);                                                                               \
        T G = (T)w[5] + (T)w[3], H = (T)w[5] - (T)w[3];                                                                     \
        f14 = (float)((T)w[7] + (T)w[1]);                                                                                   \
        f24 = (float)((T)w[1] - (T)w[7]);                                                                                   \
        float f1c = (float)((T)f14 + G);                                                                                    \
        float f10 = (float)(((T)f24 + H) * (T)K184);                                                                        \
        float f2c = (float)((T)f10 - H * (T)K261 - (T)f1c);                                                                 \
        f14 = (float)(((T)f14 - G) * (T)K2 - (T)f2c);                                                                       \
        float f18 = (float)((T)f24 * (T)K108 - (T)f10 + (T)f14);                                                            \
        o[0] = OUT((T)f1c + (T)f30);                                                                                        \
        o[7] = OUT((T)f30 - (T)f1c);                                                                                        \
        o[1] = OUT((T)f2c + E);                                                                                             \
        o[6] = OUT(E - (T)f2c);                                                                                             \
        o[2] = OUT((T)f14 + (T)f20);                                                                                        \
        o[5] = OUT((T)f20 - (T)f14);                                                                                        \
        o[4] = OUT((T)f18 + D);                                                                                             \
        o[3] = OUT(D - (T)f18);                                                                                             \
    }                                                                                                                       \
}
// _ftol: fistp qword with truncation, NaN and out of range give the integer indefinite (low dword 0); eax is the low dword.
// Then eax + 4 (modular), sar 3, and 0x3ff index the range limit table (0x10210c83..0x10210c8e).
static inline uint8_t idct_out(const uint8_t *range, double v) {
    int64_t i = v >= -9223372036854775808.0 && v < 9223372036854775808.0 ? (int64_t)v : INT64_MIN;
    return range[((int32_t)((uint32_t)i + 4u) >> 3) & 0x3ff];
}
#define OUT(v) idct_out(range, (double)(v))
IDCT_BODY(idct_53, double)
IDCT_BODY(idct_24, float)
#undef OUT


// ---- libjpeg decompress_onepass 0x1020a080: the IDCTs of one iMCU row run on a helper thread -------------------------------
// The IDCT is 13% of the level load and nothing in decompress_onepass reads its output rows before it returns (the next
// reader is the upsampler / color convert, called after). rn_jpeg_idct queues each block (coefficients copied, tables and output
// rows as host pointers into guest memory) and rn_jpeg_onepass waits for the helper before the lifted function returns to the guest.
// Bit-identical to the synchronous path (same idct_24 / idct_53); RN_FASTCHECK and RN_NOASYNC keep it synchronous.
#define IJ_BATCH 32
#define IJ_RING 256
typedef struct { int16_t coef[64]; const uint8_t *qb, *range; uint8_t *out[8]; int pc; } IJob;
typedef struct { IJob j[IJ_BATCH]; int n; atomic_uint busy; } IBatch;      // busy: published and not finished
static IBatch ij_ring[IJ_RING];
static atomic_uint ij_pub, ij_claim, ij_fin, ij_sleep;                     // batches published / claimed / finished
static pthread_mutex_t ij_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ij_cv = PTHREAD_COND_INITIALIZER;
static int ij_state;                    // 0 untried, 1 helper running, -1 off
static Ctx *ij_owner;                   // the guest thread inside rn_jpeg_onepass, NULL outside
static int ij_skip;

// Take the next published batch (the helper, or the game thread when it would otherwise wait) and run it.
static int ij_work(void) {
    unsigned cl = atomic_load_explicit(&ij_claim, memory_order_relaxed);
    do {
        if (cl == atomic_load_explicit(&ij_pub, memory_order_acquire)) return 0;
    } while (!atomic_compare_exchange_weak_explicit(&ij_claim, &cl, cl + 1, memory_order_acq_rel, memory_order_relaxed));
    IBatch *b = &ij_ring[cl % IJ_RING];
    for (int i = 0; i < b->n; i++) {
        IJob *j = &b->j[i];
        if (j->pc == 0) idct_24(j->coef, j->qb, j->out, j->range);
        else idct_53(j->coef, j->qb, j->out, j->range);
    }
    b->n = 0;
    atomic_store_explicit(&b->busy, 0, memory_order_release);
    atomic_fetch_add_explicit(&ij_fin, 1, memory_order_release);
    return 1;
}
static void *ij_helper(void *arg) {
    (void)arg;
    for (;;) {
        if (ij_work()) continue;
        int spins = 0;
        while (atomic_load_explicit(&ij_pub, memory_order_acquire) == atomic_load_explicit(&ij_claim, memory_order_relaxed) && ++spins < 200000) ;
        if (atomic_load(&ij_pub) != atomic_load(&ij_claim)) continue;
        pthread_mutex_lock(&ij_mu);
        atomic_store(&ij_sleep, 1);
        while (atomic_load(&ij_pub) == atomic_load(&ij_claim)) pthread_cond_wait(&ij_cv, &ij_mu);
        atomic_store(&ij_sleep, 0);
        pthread_mutex_unlock(&ij_mu);
    }
    return NULL;
}
static int ij_ready(void) {
    if (ij_state == 0) {
        pthread_t t;
        ij_state = getenv("RN_NOASYNC") || getenv("RN_FASTCHECK") || pthread_create(&t, NULL, ij_helper, NULL) ? -1 : 1;
        if (ij_state > 0) pthread_detach(t);
    }
    return ij_state > 0;
}
static void ij_publish(void) {
    unsigned p = atomic_load_explicit(&ij_pub, memory_order_relaxed);
    IBatch *b = &ij_ring[p % IJ_RING];
    if (!b->n) return;
    atomic_store_explicit(&b->busy, 1, memory_order_relaxed);
    atomic_store(&ij_pub, p + 1);                                  // seq_cst: pairs with the helper's sleep check
    if (atomic_load(&ij_sleep)) { pthread_mutex_lock(&ij_mu); pthread_cond_signal(&ij_cv); pthread_mutex_unlock(&ij_mu); }
}
// The game thread waits for the helper only while it is busy with a batch; every batch it has not started, this thread runs.
static void ij_flush(void) {
    ij_publish();
    while (ij_work()) ;
    for (unsigned spins = 0; atomic_load_explicit(&ij_fin, memory_order_acquire) != atomic_load_explicit(&ij_pub, memory_order_relaxed); )
        if (++spins % 4096 == 0) sched_yield();
}
// A job from rn_jpeg_idct (the caller checked every range): 1 = queued.
static int ij_queue(Ctx *c, const int16_t *in, const uint8_t *qb, uint8_t *const out[8], const uint8_t *range, int pc) {
    if (ij_owner != c) return 0;
    IBatch *b = &ij_ring[atomic_load_explicit(&ij_pub, memory_order_relaxed) % IJ_RING];
    while (atomic_load_explicit(&b->busy, memory_order_acquire)) if (!ij_work()) sched_yield();   // ring full
    IJob *j = &b->j[b->n++];
    memcpy(j->coef, in, sizeof j->coef);
    memcpy(j->out, out, sizeof j->out);
    j->qb = qb; j->range = range; j->pc = pc;
    if (b->n == IJ_BATCH) ij_publish();
    return 1;
}
// Anything that reads the rows after a non-local exit of the wrapper finds the helper idle.
static void ij_stray(void) { if (ij_owner) { ij_flush(); ij_owner = NULL; } }
// grphmfc decompress_onepass: the lifted body runs with the helper doing its IDCTs; ret as the original's.
int rn_jpeg_onepass(Ctx *c) {
    if (ij_skip) { ij_skip = 0; return 0; }
    if (!ij_ready()) return 0;
    ij_stray();
    ij_owner = c; ij_skip = 1;
    f_1020a080(c);
    ij_flush();
    ij_owner = NULL;
    return 1;
}

// jpeg_idct_float, grphmfc 0x10210920: fastcall ecx = cinfo, edx = compptr; coef_block, output_buf, output_col on the stack.
// 40% of the level load with the _ftol it calls 64 times per block.
int rn_jpeg_idct(Ctx *c) {
    gil_slice();      // no API call inside the decode loops: the sound thread's turn (RN_HOLD showed 100-165 ms holds here)
    static int check = -1, inside;
    if (check < 0) check = getenv("RN_FASTCHECK") != NULL;
    if (inside) return 0;
    uint32_t sp = c->r[ESP], cinfo = c->r[ECX], comp = c->r[EDX];
    if (!guest_ok(c, cinfo + 0x136, 4) || !guest_ok(c, comp + 0x50, 4)) return 0;
    uint32_t coef = ld(c, sp + 4, 4), obuf = ld(c, sp + 8, 4), col = ld(c, sp + 12, 4);
    uint32_t range = ld(c, cinfo + 0x136, 4) + 0x80 /* 0x1021093a */, table = ld(c, comp + 0x50, 4);
    int pc = (c->fcw >> 8) & 3, rc = (c->fcw >> 10) & 3;
    if (rc || pc == 1) return 0;    // the port assumes round to nearest and a known precision
    // pc 3 (64-bit) runs as double, like the lifted x87 (host/rt.h keeps ST in doubles); pc 0 is the float variant.
    if (!guest_ok(c, coef, 128) || !guest_ok(c, table, 256) || !guest_ok(c, range, 0x400) || !guest_ok(c, obuf, 32)) return 0;
    uint8_t *out[8], tmp[8][8];
    uint32_t rows[8];
    for (int i = 0; i < 8; i++) {
        rows[i] = ld(c, obuf + 4 * i, 4) + col;
        if (!guest_ok(c, rows[i], 8) || overlap(rows[i], 8, range, 0x400)) return 0;
        out[i] = check ? tmp[i] : c->mem + rows[i];
    }
    int16_t in[64];
    memcpy(in, c->mem + coef, sizeof in);
    if (!check && ij_queue(c, in, c->mem + table, out, c->mem + range, pc)) { guest_ret(c, 12); return 1; }
    if (pc == 0) idct_24(in, c->mem + table, out, c->mem + range);
    else idct_53(in, c->mem + table, out, c->mem + range);
    if (check) {
        static unsigned calls, bad;
        inside = 1; f_10210920(c); inside = 0;
        int diff = 0;
        for (int i = 0; i < 8; i++) diff |= memcmp(tmp[i], c->mem + rows[i], 8) != 0;
        calls++; bad += diff;
        if ((diff && bad < 20) || calls % 400000 == 1) rn_log("fastcheck idct: %u calls, %u differ, pc %d", calls, bad, pc);
        return 1;
    }
    guest_ret(c, 12);
    return 1;
}

// libjpeg ycc_rgb_convert, grphmfc 0x1020d570: fastcall ecx = cinfo, edx = input_buf; input_row, output_buf, num_rows on the stack.
int rn_jpeg_ycc(Ctx *c) {
    ij_stray();
    gil_slice();      // no API call inside the decode loops: the sound thread's turn (RN_HOLD showed 100-165 ms holds here)
    static int check = -1, inside;
    if (check < 0) check = getenv("RN_FASTCHECK") != NULL;
    if (inside) return 0;
    uint32_t sp = c->r[ESP], cinfo = c->r[ECX], ibuf = c->r[EDX];
    if (!guest_ok(c, cinfo + 0x1ba, 4) || !guest_ok(c, ibuf, 12)) return 0;
    uint32_t row = ld(c, sp + 4, 4), obuf = ld(c, sp + 8, 4), nrows = ld(c, sp + 12, 4);
    uint32_t cc = ld(c, cinfo + 0x1ba, 4) /* 0x1020d573 */, range = ld(c, cinfo + 0x136, 4), cols = ld(c, cinfo + 0x6c, 4);
    if (!guest_ok(c, cc + 8, 16) || !guest_ok(c, range, 0x400)) return 0;
    uint32_t tcrr = ld(c, cc + 8, 4), tcbb = ld(c, cc + 12, 4), tcrg = ld(c, cc + 16, 4), tcbg = ld(c, cc + 20, 4);
    if (!guest_ok(c, tcrr, 1024) || !guest_ok(c, tcbb, 1024) || !guest_ok(c, tcrg, 1024) || !guest_ok(c, tcbg, 1024)) return 0;
    if ((int32_t)nrows < 0 || nrows > 64 || !guest_ok(c, obuf, nrows * 4ull)) return 0;
    uint32_t p0 = ld(c, ibuf, 4), p1 = ld(c, ibuf + 4, 4), p2 = ld(c, ibuf + 8, 4);
    uint64_t r4 = row * 4ull, n4 = nrows * 4ull;
    if (!guest_ok(c, p0 + r4, n4) || !guest_ok(c, p1 + r4, n4) || !guest_ok(c, p2 + r4, n4)) return 0;
    for (uint32_t r = 0; r < nrows; r++) {  // all rows in range first, or the original reports the fault
        uint32_t y = ld(c, p0 + (row + r) * 4, 4), cb = ld(c, p1 + (row + r) * 4, 4), cr = ld(c, p2 + (row + r) * 4, 4), o = ld(c, obuf + r * 4, 4);
        if (!guest_ok(c, y, cols) || !guest_ok(c, cb, cols) || !guest_ok(c, cr, cols) || !guest_ok(c, o, cols * 3ull)) return 0;
        if (overlap(o, cols * 3ull, range, 0x400)) return 0;
        for (uint32_t i = 0; i < nrows; i++)    // an output row against every input row
            if (overlap(o, cols * 3ull, ld(c, p0 + (row + i) * 4ull, 4), cols) || overlap(o, cols * 3ull, ld(c, p1 + (row + i) * 4ull, 4), cols)
                || overlap(o, cols * 3ull, ld(c, p2 + (row + i) * 4ull, 4), cols)) return 0;
    }
    const int32_t *crr = (const int32_t *)(c->mem + tcrr), *cbb = (const int32_t *)(c->mem + tcbb),
                  *crg = (const int32_t *)(c->mem + tcrg), *cbg = (const int32_t *)(c->mem + tcbg);
    const uint8_t *rl = c->mem + range;     // libjpeg builds these tables (build_ycc_rgb_table): indexes stay inside the range table
    size_t sz = (size_t)cols * 3 * nrows;   // checks ran first: cols fits guest memory
    uint8_t *want = check ? malloc(sz + 1) : NULL;
    for (uint32_t r = 0; r < nrows; r++) {
        const uint8_t *py = c->mem + ld(c, p0 + (row + r) * 4, 4), *pb = c->mem + ld(c, p1 + (row + r) * 4, 4), *pr = c->mem + ld(c, p2 + (row + r) * 4, 4);
        uint8_t *o = want ? want + (size_t)r * cols * 3 : c->mem + ld(c, obuf + r * 4, 4);
        for (uint32_t x = 0; x < cols; x++, o += 3) {
            int32_t Y = py[x], B = pb[x], R = pr[x];
            o[0] = rl[Y + crr[R]];
            o[1] = rl[Y + ((cbg[B] + crg[R]) >> 16)];   // sar 0x10, 0x1020d65b
            o[2] = rl[Y + cbb[B]];
        }
    }
    if (want) {
        static unsigned calls, bad;
        inside = 1; f_1020d570(c); inside = 0;
        int diff = 0;
        for (uint32_t r = 0; r < nrows; r++)
            diff |= memcmp(want + (size_t)r * cols * 3, c->mem + ld(c, obuf + r * 4, 4), (size_t)cols * 3) != 0;
        calls++; bad += diff;
        if ((diff && bad < 20) || calls % 20000 == 1) rn_log("fastcheck ycc: %u calls, %u differ", calls, bad);
        free(want);
        return 1;
    }
    guest_ret(c, 12);
    return 1;
}

// libjpeg 6a decode_mcu (sequential Huffman), grphmfc 0x1020b370: fastcall ecx = cinfo, edx = MCU_data; returns TRUE in eax.
// Offsets from the disassembly (capstone + Hopper), the cinfo is packed:
//   cinfo: +0x14 src, +0x110 restart_interval, +0x156 blocks_in_MCU, +0x15a MCU_membership[], +0x13e cur_comp_info[], +0x192 unread_marker,
//          +0x1ae entropy.  entropy: +8 get_buffer, +0xc bits_left, +0x14 last_dc_val[4], +0x24 restarts_to_go, +0x28 dc_derived_tbls[4],
//          +0x38 ac_derived_tbls[4].  comp: +0x14 dc_tbl_no, +0x18 ac_tbl_no, +0x24 DCT_scaled_size, +0x30 component_needed.
//   derived table: +0 mincode[], +0x44 maxcode[] (17 = 0xfffff), +0x8c valptr[], +0xd0 &htbl->bits (huffval at +0x11), +0xd4 look_nbits[256]
//   (int), +0x4d4 look_sym[256] (byte).  Extend tables 0x102125bc/0x102125fc, jpeg_natural_order 0x1021247c.
// The whole MCU is decoded into locals first; any case needing the guest (restart due, marker, source buffer empty, bad code or
// table) returns 0 before anything is written and the lifted body runs.
enum { HD_TEST = 0x102125bc, HD_OFF = 0x102125fc, HD_NAT = 0x1021247c };
typedef struct { const uint8_t *m; uint32_t next, left; int32_t buf; int bits; } HBits;
// fill_bit_buffer 0x1020b010 with the marker and refill branches left to the lifted code: reads bytes until 25 bits are held.
static inline int hb_fill(HBits *b) {
    while (b->bits < 25) {
        if (!b->left) return 0;
        uint32_t ch = b->m[b->next++]; b->left--;
        if (ch == 0xff) {
            do { if (!b->left) return 0; ch = b->m[b->next++]; b->left--; } while (ch == 0xff);
            if (ch) return 0;                       // marker (0x1020b099)
            ch = 0xff;                              // stuffed byte (0x1020b092)
        }
        b->buf = (int32_t)(((uint32_t)b->buf << 8) | ch); b->bits += 8;
    }
    return 1;
}
static inline int32_t hb_sar(int32_t v, int n) { return v >> (n & 31); }
// HUFF_DECODE (inline lookahead 0x1020b41a..0x1020b4be, jpeg_huff_decode 0x1020b110); -1 = give up.
static int hb_decode(Ctx *c, HBits *b, uint32_t t) {
    if (b->bits < 8 && !hb_fill(b)) return -1;
    if (b->bits >= 8) {
        uint32_t look = hb_sar(b->buf, b->bits - 8) & 0xff;
        uint32_t nb = ld(c, t + 0xd4 + look * 4, 4);
        if (nb) { b->bits -= nb; return c->mem[t + 0x4d4 + look]; }
    }
    int l = 9;
    if (b->bits < l && !hb_fill(b)) return -1;
    b->bits -= l;
    int32_t code = hb_sar(b->buf, b->bits) & ((1 << l) - 1);
    while (code > (int32_t)ld(c, t + 0x44 + l * 4, 4)) {
        if (b->bits < 1 && !hb_fill(b)) return -1;
        b->bits--;
        code = (code << 1) | (hb_sar(b->buf, b->bits) & 1);
        if (++l > 16) return -1;                    // bad code: the original warns (0x72)
    }
    uint32_t hp = ld(c, t + 0xd0, 4);
    uint32_t i = ld(c, t + 0x8c + l * 4, 4) - ld(c, t + l * 4, 4) + (uint32_t)code;
    if (i > 255 || !guest_ok(c, hp + 0x11 + i, 1)) return -1;
    return c->mem[hp + 0x11 + i];
}
// receive + HUFF_EXTEND (0x1020b4c2..0x1020b50d); the tables are read from the guest.
static inline int hb_extend(Ctx *c, HBits *b, int s, int32_t *out) {
    if (s > 15) return 0;
    if (b->bits < s && !hb_fill(b)) return 0;
    b->bits -= s;
    int32_t r = hb_sar(b->buf, b->bits) & ((1 << s) - 1);
    *out = r < (int32_t)ld(c, HD_TEST + s * 4, 4) ? r + (int32_t)ld(c, HD_OFF + s * 4, 4) : r;
    return 1;
}
int rn_jpeg_huff(Ctx *c) {
    static int check = -1, inside;
    static unsigned calls, fast, bad;
    if (check < 0) check = getenv("RN_FASTCHECK") != NULL;
    if (inside) return 0;
    uint32_t cinfo = c->r[ECX], mcu = c->r[EDX];
    calls++;
    if (!guest_ok(c, cinfo, 0x1b2) || !guest_ok(c, ld(c, cinfo + 0x1ae, 4), 0x48)) return 0;
    uint32_t ent = ld(c, cinfo + 0x1ae, 4), src = ld(c, cinfo + 0x14, 4);
    uint32_t rtg = ld(c, ent + 0x24, 4), nblk = ld(c, cinfo + 0x156, 4);
    if (ld(c, cinfo + 0x110, 4) && !rtg) return 0;      // process_restart 0x1020b790
    if (ld(c, cinfo + 0x192, 4) || !guest_ok(c, src, 8)) return 0;      // a marker is pending
    if (nblk == 0 || nblk > 10 || !guest_ok(c, mcu, nblk * 4)) return 0;
    HBits b = { c->mem, ld(c, src, 4), ld(c, src + 4, 4), (int32_t)ld(c, ent + 8, 4), (int)ld(c, ent + 0xc, 4) };
    if (b.bits < 0 || b.bits > 32 || !guest_ok(c, b.next, b.left)) return 0;
    uint32_t start = b.next, last[4], blk[10];
    int32_t dcv[4];
    for (int i = 0; i < 4; i++) dcv[i] = (int32_t)(last[i] = ld(c, ent + 0x14 + i * 4, 4));
    struct { uint32_t a; int16_t v; } wr[10 * 64];
    int nw = 0;
    for (uint32_t i = 0; i < nblk; i++) {
        uint32_t ci = ld(c, cinfo + 0x15a + i * 4, 4);
        if (ci > 3) return 0;
        uint32_t comp = ld(c, cinfo + 0x13e + ci * 4, 4);
        if (!guest_ok(c, comp, 0x34)) return 0;
        uint32_t dn = ld(c, comp + 0x14, 4), an = ld(c, comp + 0x18, 4);
        if (dn > 3 || an > 3 || !ld(c, comp + 0x30, 4) || (int32_t)ld(c, comp + 0x24, 4) <= 1) return 0;
        uint32_t dt = ld(c, ent + 0x28 + dn * 4, 4), at = ld(c, ent + 0x38 + an * 4, 4);
        blk[i] = ld(c, mcu + i * 4, 4);
        if (!guest_ok(c, dt, 0x5d4) || !guest_ok(c, at, 0x5d4) || !guest_ok(c, blk[i], 128)) return 0;
        if (overlap(blk[i], 128, ent, 0x48) || overlap(blk[i], 128, src, 8) || overlap(blk[i], 128, cinfo, 0x1b2) || overlap(blk[i], 128, dt, 0x5d4)
            || overlap(blk[i], 128, at, 0x5d4) || overlap(blk[i], 128, comp, 0x34) || overlap(blk[i], 128, mcu, nblk * 4)) return 0;
        int s = hb_decode(c, &b, dt);
        int32_t v;
        if (s < 0) return 0;
        if (s) { if (!hb_extend(c, &b, s, &v)) return 0; } else v = 0;
        dcv[ci] = (int32_t)((uint32_t)dcv[ci] + (uint32_t)v);
        wr[nw].a = blk[i]; wr[nw++].v = (int16_t)dcv[ci];
        for (int k = 1; k < 64; k++) {
            int rs = hb_decode(c, &b, at);
            if (rs < 0) return 0;
            int r = rs >> 4;
            s = rs & 15;
            if (s) {
                k += r;
                if (!hb_extend(c, &b, s, &v)) return 0;
                uint32_t z = ld(c, HD_NAT + k * 4, 4);
                if (z > 63) return 0;
                wr[nw].a = blk[i] + z * 2; wr[nw++].v = (int16_t)v;
            } else {
                if (r != 15) break;
                k += 15;
            }
        }
    }
    for (uint32_t i = 0; i < nblk; i++) if (overlap(blk[i], 128, start, b.next - start)) return 0;
    fast++;
    uint8_t before[10 * 128 + 0x48 + 16], after[sizeof before];
    size_t len = 0;
    if (check) {    // the regions the original writes: blocks, entropy state, source fields, unread_marker
        for (uint32_t i = 0; i < nblk; i++, len += 128) memcpy(before + len, c->mem + blk[i], 128);
        memcpy(before + len, c->mem + ent, 0x48); len += 0x48;
        memcpy(before + len, c->mem + src, 8); len += 8;
        memcpy(before + len, c->mem + cinfo + 0x192, 4); len += 4;
    }
    for (int i = 0; i < nw; i++) { uint16_t v = (uint16_t)wr[i].v; memcpy(c->mem + wr[i].a, &v, 2); }
    st(c, src, b.next, 4); st(c, src + 4, b.left, 4);
    st(c, ent + 0xc, (uint32_t)b.bits, 4); st(c, ent + 8, (uint32_t)b.buf, 4);
    for (int i = 0; i < 4; i++) st(c, ent + 0x14 + i * 4, (uint32_t)dcv[i], 4);
    st(c, ent + 0x24, rtg - 1, 4);      // 0x1020b778
    if (check) {
        size_t n = 0;
        for (uint32_t i = 0; i < nblk; i++, n += 128) memcpy(after + n, c->mem + blk[i], 128);
        memcpy(after + n, c->mem + ent, 0x48); n += 0x48;
        memcpy(after + n, c->mem + src, 8); n += 8;
        memcpy(after + n, c->mem + cinfo + 0x192, 4);
        n = 0;
        for (uint32_t i = 0; i < nblk; i++, n += 128) memcpy(c->mem + blk[i], before + n, 128);
        memcpy(c->mem + ent, before + n, 0x48); n += 0x48;
        memcpy(c->mem + src, before + n, 8); n += 8;
        memcpy(c->mem + cinfo + 0x192, before + n, 4);
        inside = 1; f_1020b370(c); inside = 0;
        int diff = c->r[EAX] != 1;
        n = 0;
        for (uint32_t i = 0; i < nblk; i++, n += 128) diff |= memcmp(after + n, c->mem + blk[i], 128) != 0;
        diff |= memcmp(after + n, c->mem + ent, 0x48) != 0; n += 0x48;
        diff |= memcmp(after + n, c->mem + src, 8) != 0; n += 8;
        diff |= memcmp(after + n, c->mem + cinfo + 0x192, 4) != 0;
        bad += diff;
        if ((diff && bad < 20) || calls % 25000 == 1) rn_log("fastcheck huff: %u calls, %u on the host path, %u differ", calls, fast, bad);
        return 1;
    }
    c->r[EAX] = 1;
    guest_ret(c, 0);
    return 1;
}

// grphmfc FUN_102045e0: RGB scanline to the image's 32-bit row (b, g, r, 0). fastcall ecx = rgb, edx = width; image, y on the stack.
int rn_jpeg_row(Ctx *c) {
    ij_stray();
    gil_slice();      // no API call inside the decode loops: the sound thread's turn (RN_HOLD showed 100-165 ms holds here)
    static int check = -1, inside;
    if (check < 0) check = getenv("RN_FASTCHECK") != NULL;
    if (inside) return 0;
    uint32_t sp = c->r[ESP], rgb = c->r[ECX], w = c->r[EDX], img = ld(c, sp + 4, 4), y = ld(c, sp + 8, 4);
    if ((int32_t)w <= 0 || (int32_t)y < 0) return 0;
    uint64_t d = (uint64_t)w * y * 4 + img;     // 0x102045e0: param_2 * param_4 * 4 + param_3
    if (!guest_ok(c, rgb, w * 3ull) || !guest_ok(c, d, w * 4ull) || overlap(rgb, w * 3ull, d, w * 4ull)) return 0;
    const uint8_t *s = c->mem + rgb;
    uint8_t *want = check ? malloc((size_t)w * 4) : NULL, *o = want ? want : c->mem + (uint32_t)d;
    for (uint32_t x = 0; x < w; x++, s += 3, o += 4) { o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = 0; }
    if (want) {
        static unsigned calls, bad;
        inside = 1; f_102045e0(c); inside = 0;
        int diff = memcmp(want, c->mem + (uint32_t)d, (size_t)w * 4) != 0;
        calls++; bad += diff;
        if ((diff && bad < 20) || calls % 20000 == 1) rn_log("fastcheck row: %u calls, %u differ", calls, bad);
        free(want);
        return 1;
    }
    guest_ret(c, 8);
    return 1;
}

// ---- grphmfc G_Image::copyWithScale 0x10203be0 (scales the screen into the quicksave thumbnail) ----------------------------------
// Four lifted x87 functions in host C, same order of operations and same rounding:
//   0x10203be0 copyWithScale(this = dst ecx; src, float scale on the stack; ret 8): one quad per dst pixel, 1/w x 1/h big, as a
//     fan of two triangles (A,B,C) and (A,C,D).
//   0x102032c0 areaSample(this = src ecx; out, scale, 4, quad; ret 0x10): vertices (x,y) scaled to (x*w*scale, y*h*scale, u = x*w,
//     v = y*h), one FUN_10203460 per triangle, then colour = trunc(acc * 1/count) low byte per channel, alpha 0xff.
//   0x10203460 triangle setup (sorts by y, writes the globals 0x102169b0..0x102169e0), 0x10203700 spans (bilinear, wraps, 4 taps).
// Disassembly (capstone, build/img/grphmfc.dll) and the Ghidra decompile (rea/c/grphmfc.dll/*.c) agree. What the decompile hides:
//  - fld/fmul/fadd chains stay in an x87 register (double at 53-bit precision, float at 24), only fstp/fst dword rounds to float;
//    the stand-in is instantiated for both (CWS(double) / CWS(float)), the precision control picks one, like IDCT_BODY above.
//  - fcom + `test ah,1` is "less than or unordered" (C0 is set for NaN): LTU(). Compares of fcomp/fcom run on the register value.
//  - _ftol (0x10211410 jmp to msvcrt) truncates; only eax is used (idiv/low byte), so out of range and NaN give 0.
//  - FUN_10203700 keeps U (ebp-4) and V (ebp+8) in float memory AND in registers: ftol(U) and the wrap loops use the register
//    (unrounded), the bilinear fractions use the stored float. V is only in memory once the pixel starts.
//  - The globals 0x102169b0..0x102169e0 (b0 b4 b8 bc c0 c4 c8 cc d0 d4 d8 dc e0) are read only by FUN_10203460/FUN_10203700
//    (xrefs in rea/c); their last values are written back as the original leaves them.
// Whole result goes to a temp buffer first: any odd input (non-finite, budget, index outside the source) returns 0 before a write.
typedef struct { float x, y, u, v; } CwsV;
typedef struct { float b0, b4, b8, bc, c0, c4, c8, cc, d0, d4, d8; int32_t dc, e0; } CwsG;   // 0x102169b0 .. 0x102169e0
typedef struct { const uint32_t *pix; int32_t w, h; } CwsS;
#define CWS_GLOBALS 0x102169b0u
#define LTU(a, b) (!((a) >= (b)))   // fcom/fcomp + test ah,1
static inline int32_t cws_ftol(double v) {    // _ftol, eax only
    double r = trunc(v);
    return r >= -9223372036854775808.0 && r < 9223372036854775808.0 ? (int32_t)(uint32_t)(uint64_t)(int64_t)r : 0;
}
#define CWS_CH(p, sh) ((T)(int32_t)(((p) >> (sh)) & 0xffu))

#define CWS(S)                                                                                                            \
/* FUN_10203700 0x10203700: scanlines dc..e0 of one triangle, bilinear taps summed into acc[3]. */                      \
static int cws_span_##S(const CwsS *s, int32_t *cnt, float acc[3], CwsG *g, long *bud) {                                \
    _Pragma("clang fp contract(off)")                                                                                    \
    const int32_t w = s->w, h = s->h;                                                                                    \
    if (g->dc >= g->e0) return 0;                                                                                        \
    for (int32_t y = g->dc; y < g->e0; y++) {                                                                            \
        int32_t xl = cws_ftol(g->b0), xr = cws_ftol(g->b4);                      /* 0x10203773, 0x10203781 */            \
        T tt = ((T)1 - ((T)g->b0 - (T)xl)) - ((T)g->d8 - (T)g->b0);              /* 0x10203794 qword 1.0 */              \
        float mt = (float)tt;                                                                                            \
        T Ur = ((T)g->d0 * (T)mt + (T)g->c0) - (T)0.5f;                          /* 0x10212158 dword 0.5 */              \
        float mu = (float)Ur;                                                                                            \
        T Vr = ((T)g->d4 * (T)mt + (T)g->c4) - (T)0.5f;                                                                  \
        float mv = (float)Vr;                                                                                            \
        if (xl < xr) {                                                                                                   \
            int64_t n = (int64_t)xr - xl;                                                                                \
            if (n > (1 << 20)) return -1;                                                                                \
            for (; n > 0; n--) {                                                                                         \
                if (LTU(Ur, (T)0)) { do { if (--*bud < 0) return -1; Ur += (T)w; } while (LTU(Ur, (T)0)); mu = (float)Ur; }  \
                if (LTU(Vr, (T)0)) { do { if (--*bud < 0) return -1; Vr += (T)h; } while (LTU(Vr, (T)0)); mv = (float)Vr; }  \
                float fw = (float)w, fh = (float)h;                                                                      \
                if (!LTU(Ur, (T)fw)) { do { if (--*bud < 0) return -1; Ur -= (T)fw; } while (!LTU(Ur, (T)fw)); mu = (float)Ur; } \
                if (!LTU(Vr, (T)fh)) { do { if (--*bud < 0) return -1; Vr -= (T)fh; } while (!LTU(Vr, (T)fh)); mv = (float)Vr; } \
                int32_t x0 = cws_ftol(Ur), x1 = cws_ftol(Ur + (T)1.0f);                                                  \
                int32_t y0 = cws_ftol((T)mv), y1 = cws_ftol((T)mv + (T)1.0f);                                            \
                int32_t a0 = x0 % w, a1 = x1 % w, r0 = y0 % h, r1 = y1 % h;                                              \
                int64_t lim = (int64_t)w * h;                                                                            \
                int64_t iA = (int64_t)r0 * w + a0, iB = (int64_t)r0 * w + a1, iC = (int64_t)r1 * w + a0, iD = (int64_t)r1 * w + a1; \
                if (iA < 0 || iB < 0 || iC < 0 || iD < 0 || iA >= lim || iB >= lim || iC >= lim || iD >= lim) return -1; \
                uint32_t pA = s->pix[iA], pB = s->pix[iB], pC = s->pix[iC], pD = s->pix[iD];                             \
                T fx = (T)mu - (T)x0, fy = (T)mv - (T)y0;                                                                \
                T ifx = (T)1.0f - fx, ify = (T)1.0f - fy;                                                                \
                const int sh[3] = {16, 8, 0};                                                                            \
                for (int k = 0; k < 3; k++) {   /* 0x10203997 red, 0x102039f0 green, 0x10203a43 blue */                  \
                    T r1_ = CWS_CH(pA, sh[k]) * ifx + CWS_CH(pB, sh[k]) * fx;                                            \
                    T r2_ = CWS_CH(pC, sh[k]) * ifx + CWS_CH(pD, sh[k]) * fx;                                            \
                    acc[k] = (float)((r1_ * ify + r2_ * fy) + (T)acc[k]);                                                \
                }                                                                                                        \
                Ur = (T)g->d0 + (T)mu; mu = (float)Ur;                                   /* 0x10203a78 */                \
                Vr = (T)g->d4 + (T)mv; mv = (float)Vr;                                                                   \
                (*cnt)++;                                                                                                \
            }                                                                                                            \
        }                                                                                                                \
        g->b0 = (float)((T)g->b8 + (T)g->b0); g->b4 = (float)((T)g->bc + (T)g->b4);   /* 0x10203ab5 */                  \
        g->c0 = (float)((T)g->c8 + (T)g->c0); g->c4 = (float)((T)g->cc + (T)g->c4);                                      \
    }                                                                                                                    \
    return 0;                                                                                                            \
}                                                                                                                        \
/* FUN_10203460 0x10203460: one triangle (a = esi, b = ebx, c = edi at entry). */                                       \
static int cws_tri_##S(const CwsS *s, int32_t *cnt, float acc[3], CwsG *g, const CwsV *a, const CwsV *b, const CwsV *c, long *bud) { \
    _Pragma("clang fp contract(off)")                                                                                    \
    const CwsV *esi = a, *ebx = b, *edi = c, *t;                                                                         \
    float s38, s34, s3c, s14, s18, s10;                                                                                  \
    T T0;                                                                                                                \
    for (int it = 0;; it++) {                                                                                            \
        if (it > 8) return -1;                                                                                           \
        if (LTU(ebx->y, esi->y)) {                                                                                       \
            t = edi;                                                                                                     \
            if (edi->y > ebx->y) { edi = esi; esi = ebx; ebx = t; }                  /* 0x1020349f (test ah,0x41) */        \
            else { edi = ebx; ebx = esi; esi = t; }                                  /* 0x10203497 */                       \
        }                                                                                                                \
        if (LTU(edi->y, esi->y)) { t = edi; edi = ebx; ebx = esi; esi = t; }         /* 0x102034b2 */                       \
        T0 = (T)ebx->x - (T)esi->x;                                                  /* stays in st0 until 0x10203663 */    \
        s38 = (float)((T)ebx->y - (T)esi->y); s34 = (float)((T)edi->x - (T)esi->x); s3c = (float)((T)edi->y - (T)esi->y); \
        s14 = (float)((T)edi->x - (T)ebx->x); s18 = (float)((T)edi->y - (T)ebx->y);                                      \
        T den = (T)s3c * T0 - (T)s34 * (T)s38;                                                                           \
        T inv = (T)1.0f / den;                                                       /* 0x102034fc fdivr 1.0f */            \
        s10 = (float)inv;                                                                                                \
        if (!LTU(inv, (T)0)) break;                                                                                      \
        t = edi; edi = ebx; ebx = t;                                                 /* 0x10203513, retry from the top */   \
    }                                                                                                                    \
    int32_t y1 = cws_ftol(ebx->y), y2 = cws_ftol(edi->y), y0 = cws_ftol(esi->y);                                         \
    g->dc = y0; g->e0 = y2 > y1 ? y1 : y2;                                                                               \
    g->d8 = esi->x;                                                                                                      \
    T fv7 = (T)1.0f - ((T)esi->y - (T)y0);                                                                               \
    g->b8 = (float)((T)s34 / (T)s3c);                                                                                    \
    g->bc = (float)(T0 / (T)s38);                                                                                        \
    g->b0 = (float)((T)g->b8 * fv7 + (T)esi->x);                                                                         \
    g->b4 = (float)((T)g->bc * fv7 + (T)esi->x);                                                                         \
    g->d0 = (float)((((T)s3c * (T)ebx->u - (T)s18 * (T)esi->u) - (T)s38 * (T)edi->u) * (T)s10);                         \
    g->d4 = (float)((((T)s3c * (T)ebx->v - (T)s18 * (T)esi->v) - (T)s38 * (T)edi->v) * (T)s10);                         \
    g->c8 = (float)((((T)s14 * (T)esi->u - (T)s34 * (T)ebx->u) + T0 * (T)edi->u) * (T)s10);                             \
    g->cc = (float)((((T)s14 * (T)esi->v - (T)s34 * (T)ebx->v) + T0 * (T)edi->v) * (T)s10);                             \
    g->c0 = (float)((T)g->c8 * fv7 + (T)esi->u);                                                                         \
    g->c4 = (float)((T)g->cc * fv7 + (T)esi->v);                                                                         \
    if (cws_span_##S(s, cnt, acc, g, bud)) return -1;                                                                    \
    if (y2 == y1) return 0;                                                                                              \
    if (y2 > y1) {                                                                                                       \
        g->dc = y1; g->bc = (float)((T)s14 / (T)s18); g->e0 = y2;                    /* 0x10203674 */                       \
        g->b4 = (float)(((T)1.0f - ((T)ebx->y - (T)y1)) * (T)g->bc + (T)ebx->x);                                         \
    } else {                                                                                                             \
        g->dc = y2; g->b8 = (float)((T)s14 / (T)s18); g->e0 = y1;                    /* 0x102036aa */                       \
        g->b0 = (float)(((T)1.0f - ((T)edi->y - (T)y2)) * (T)g->b8 + (T)edi->x);                                         \
    }                                                                                                                    \
    return cws_span_##S(s, cnt, acc, g, bud) ? -1 : 0;                                                                   \
}                                                                                                                        \
/* areaSample 0x102032c0 for the 4-vertex quad q[4] (x, y), result colour word in *out. */                              \
static int cws_area_##S(const CwsS *s, float scale, const float q[4][2], CwsG *g, uint32_t *out, long *bud) {            \
    _Pragma("clang fp contract(off)")                                                                                    \
    const T W = (T)s->w, H = (T)s->h;                                                                                    \
    float acc[3] = {0, 0, 0};                                                                                            \
    int32_t cnt = 0;                                                                                                     \
    CwsV v[4];                                                                                                           \
    for (int i = 0; i < 4; i++) {                                                                                        \
        v[i].x = (float)((W * (T)q[i][0]) * (T)scale); v[i].y = (float)((H * (T)q[i][1]) * (T)scale);                    \
        v[i].u = (float)(W * (T)q[i][0]); v[i].v = (float)(H * (T)q[i][1]);                                              \
    }                                                                                                                    \
    for (int i = 1; i < 3; i++)                                                                                          \
        if (cws_tri_##S(s, &cnt, acc, g, &v[0], &v[i], &v[i + 1], bud)) return -1;                                       \
    float k = cnt == 0 ? 0.0f : (float)((T)1.0f / (T)cnt);                       /* 0x102033cb: fcom 0.0f, test ah,0x40 */ \
    for (int i = 0; i < 3; i++) acc[i] = (float)((T)k * (T)acc[i]);                                                      \
    *out = (cws_ftol(acc[2]) & 0xffu) | (cws_ftol(acc[1]) & 0xffu) << 8 | (cws_ftol(acc[0]) & 0xffu) << 16 | 0xff000000u; \
    return 0;                                                                                                            \
}                                                                                                                        \
/* copyWithScale 0x10203be0: dst dw x dh from src, pixels written to out[dw*dh]. */                                     \
static int cws_copy_##S(const CwsS *s, int32_t dw, int32_t dh, float scale, uint32_t *out, CwsG *g) {                    \
    _Pragma("clang fp contract(off)")                                                                                    \
    long bud = 1l << 26;                                                                                                 \
    float dx = (float)((T)1.0f / (T)dw), dy = (float)((T)1.0f / (T)dh);           /* 0x10203bf4 */                       \
    float q[4][2] = {{0, 0}, {0, 0}, {0, dy}, {0, dy}};                           /* x of v0..v3 reset per row; y accumulates */ \
    for (int32_t row = 0, i = 0; row < dh; row++) {                                                                      \
        q[0][0] = 0; q[1][0] = dx; q[2][0] = dx; q[3][0] = 0;                                                            \
        for (int32_t col = 0; col < dw; col++, i++) {                                                                    \
            if (cws_area_##S(s, scale, q, g, &out[i], &bud)) return -1;                                                  \
            for (int k = 0; k < 4; k++) q[k][0] = (float)((T)q[k][0] + (T)dx);                                           \
        }                                                                                                                \
        for (int k = 0; k < 4; k++) q[k][1] = (float)((T)q[k][1] + (T)dy);                                               \
    }                                                                                                                    \
    return 0;                                                                                                            \
}
#define T double
CWS(d)
#undef T
#define T float
CWS(f)
#undef T

// G_Image::copyWithScale 0x10203be0, thiscall: ecx = dst image, src image and float scale on the stack, ret 8.
// G_Image: pixels this+5 (u32 B,G,R,A per pixel), width this+0x19, height this+0x1d.
int rn_copy_with_scale(Ctx *c) {
    static int check = -1, inside;
    if (check < 0) check = getenv("RN_FASTCHECK") != NULL;
    if (inside) return 0;
    uint32_t sp = c->r[ESP], dst = c->r[ECX];
    if (!guest_ok(c, dst, 0x21) || !guest_ok(c, sp, 12)) return 0;
    uint32_t src = ld(c, sp + 4, 4), fbits = ld(c, sp + 8, 4);
    if (!guest_ok(c, src, 0x21)) return 0;
    int32_t dw = (int32_t)ld(c, dst + 0x19, 4), dh = (int32_t)ld(c, dst + 0x1d, 4);
    int32_t sw = (int32_t)ld(c, src + 0x19, 4), sh = (int32_t)ld(c, src + 0x1d, 4);
    uint32_t dp = ld(c, dst + 5, 4), spx = ld(c, src + 5, 4);
    int pc = (c->fcw >> 8) & 3, rc = (c->fcw >> 10) & 3;
    if (rc || pc == 1) return 0;    // the port assumes round to nearest and a known precision
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0 || (int64_t)dw * dh > (1 << 24) || (int64_t)sw * sh > (1 << 26)) return 0;
    uint64_t dn = (uint64_t)dw * dh * 4, sn = (uint64_t)sw * sh * 4;
    if (!guest_ok(c, dp, dn) || !guest_ok(c, spx, sn) || overlap(dp, dn, spx, sn)) return 0;
    if (overlap(dp, dn, dst, 0x21) || overlap(dp, dn, src, 0x21) || !guest_ok(c, CWS_GLOBALS, sizeof(CwsG))) return 0;
    CwsS s = {(const uint32_t *)(c->mem + spx), sw, sh};    // aligned: guest images are dword aligned
    if (spx & 3) return 0;
    // The save thumbnail (MaxPayne_SaveGameInterface::scaleThumbnail 0x410f00) is the one caller that asks for scale
    // exactly 1.0f (push 0x3f800000 at 0x410f07, call at 0x410f0d, its ret 4 lands at 0x410f13) into an image no larger
    // than the source, so there every destination pixel covers a whole source box and the two-triangles-per-pixel
    // sampler 0x102032c0 is only averaging that box: 4 bilinear taps per source pixel, per destination pixel. Averaging
    // the box once in integers gives a near-identical thumbnail (not bit-exact: rea/map/save.md) for a fraction of the frame.
    // Every other caller keeps the sampler and its scratch globals 0x102169b0.
    if (ld(c, sp, 4) == 0x410f13u && fbits == 0x3f800000u && dw <= sw && dh <= sh && !(dp & 3)) {
        uint32_t *dpx = (uint32_t *)(c->mem + dp);
        for (int32_t y = 0; y < dh; y++) {
            int32_t y0 = (int32_t)((int64_t)y * sh / dh), y1 = (int32_t)((int64_t)(y + 1) * sh / dh);
            for (int32_t x = 0; x < dw; x++) {
                int32_t x0 = (int32_t)((int64_t)x * sw / dw), x1 = (int32_t)((int64_t)(x + 1) * sw / dw);
                uint32_t b = 0, g = 0, r = 0, n = (uint32_t)(x1 - x0) * (uint32_t)(y1 - y0);
                for (int32_t sy = y0; sy < y1; sy++) {
                    const uint32_t *row = s.pix + (int64_t)sy * sw;
                    for (int32_t sx = x0; sx < x1; sx++) {
                        uint32_t p = row[sx];
                        b += p & 255u; g += (p >> 8) & 255u; r += (p >> 16) & 255u;
                    }
                }
                dpx[(int64_t)y * dw + x] = 0xff000000u | (r / n) << 16 | (g / n) << 8 | b / n;
            }
        }
        guest_ret(c, 8);
        return 1;
    }
    uint32_t *out = malloc((size_t)dn);
    if (!out) return 0;
    CwsG g;
    float scale;
    memcpy(&scale, &fbits, 4);
    int bad = pc == 0 ? cws_copy_f(&s, dw, dh, scale, out, &g) : cws_copy_d(&s, dw, dh, scale, out, &g);
    if (bad) { free(out); return 0; }
    if (check) {    // RN_FASTCHECK: the lifted original writes the image; this compares it and the globals to the host result.
        static unsigned calls, nbad;
        inside = 1; f_10203be0(c); inside = 0;
        int diff = memcmp(out, c->mem + dp, (size_t)dn) != 0;
        diff |= memcmp(&g, c->mem + CWS_GLOBALS, sizeof g) != 0;
        calls++; nbad += diff;
        rn_log("fastcheck copyWithScale: %u calls, %u differ (last %dx%d from %dx%d, pc %d)", calls, nbad, dw, dh, sw, sh, pc);
        free(out);
        return 1;
    }
    memcpy(c->mem + dp, out, (size_t)dn);
    memcpy(c->mem + CWS_GLOBALS, &g, sizeof g);
    free(out);
    guest_ret(c, 8);
    return 1;
}

// ---- e2mfc 0x10120e20 and 0x10120ee0 (the comic/panel image path), grphmfc 0x10203d20 (G_Image pixel buffer) -------------------------
// RN_FASTCHECK summary at exit (the per-call lines only print on a difference or every few hundred calls).
static struct { const char *name; unsigned calls, bad; } fc3[4];
static void fc3_report(void) { for (int k = 0; k < 4; k++) if (fc3[k].calls) fprintf(stderr, "FASTCHECK %s: %u calls, %u differ\n", fc3[k].name, fc3[k].calls, fc3[k].bad); }
static void fc3_note(int k, const char *name, int diff, const char *extra) {
    if (!fc3[0].name && !fc3[1].name && !fc3[2].name && !fc3[3].name) atexit(fc3_report);
    fc3[k].name = name; fc3[k].calls++; fc3[k].bad += diff;
    if ((diff && fc3[k].bad < 20) || fc3[k].calls % 256 == 1) rn_log("fastcheck %s: %u calls, %u differ %s", name, fc3[k].calls, fc3[k].bad, extra);
}
// The x87 of the lifted code: PC 24 bits rounds every arithmetic result to float, 53/64 keep a double (rt.h fround).
static inline double xr(int pc0, double v) { return pc0 ? (double)(float)v : v; }
// fistp dword: out of range or NaN gives 0x80000000; the stand-ins return 0 instead (the original also raises IE then).
static inline int fist32(double v, int nearest, int32_t *out) {
    double r = nearest ? nearbyint(v) : trunc(v);
    if (!(r >= -2147483648.0 && r < 2147483648.0)) return 0;
    *out = (int32_t)r;
    return 1;
}
static inline int guest_x87_ok(Ctx *c) {   // PC 24/53/64 and RC nearest only; room on the x87 stack for the 2 registers used
    int pc = (c->fcw >> 8) & 3, rc = (c->fcw >> 10) & 3;
    return !rc && pc != 1 && __builtin_popcount(c->ftag & 0xffu) <= 4;
}
// A range the stand-in must not touch or read: the callee's own frame and argument slots (the original scribbles on them).
static inline int on_frame(uint64_t p, uint64_t n, uint32_t sp) { return overlap(p, n, sp - 0x100ull, 0x140); }

// e2mfc FUN_10120e20(dst, src, w, h), stdcall ret 0x10 (ecx = this is unused): grey -> alpha. Per pixel i of w*h (32-bit imul
// 0x10120e27): dst[4i+3] = round-to-nearest(float(b2*K54 + b1*K50 + b0*K4c)) low byte, where b0..b2 = src[4i..4i+2].
// The fistp at 0x10120e9b forces RC = 0 (ebx = 0, 0x10120e7f) whatever the control word holds, so the stand-in needs RC = 0 only
// for the multiplies and adds. The args are scratch slots of the original (ebp+8, +0x10, +0x14), dead after the ret.
// Left as the original leaves them: edx = src + 4n, ecx = w high half | FCW, eax = last byte, flags of `dec edi` (0x10120ec7).
int rn_grey_alpha(Ctx *c) {
    static int check = -1, inside;
    if (check < 0) check = getenv("RN_FASTCHECK") != NULL;
    if (inside) return 0;
    uint32_t sp = c->r[ESP];
    if (!guest_ok(c, sp, 20)) return 0;
    uint32_t dst = ld(c, sp + 4, 4), src = ld(c, sp + 8, 4), w = ld(c, sp + 12, 4), h = ld(c, sp + 16, 4);
    uint32_t n = w * h;         // 0x10120e27 imul edi, [ebp+0x14]: low 32 bits, `jbe` leaves for 0
    if (!n || !guest_x87_ok(c)) return 0;
    uint64_t bytes = 4ull * n;
    if (!guest_ok(c, dst, bytes) || !guest_ok(c, src, bytes) || overlap(dst, bytes, src, bytes)) return 0;
    if (on_frame(dst, bytes, sp) || on_frame(src, bytes, sp) || !guest_ok(c, 0x10149f4cu, 12)) return 0;
    const double k0 = qf(ld(c, 0x10149f4cu, 4)), k1 = qf(ld(c, 0x10149f50u, 4)), k2 = qf(ld(c, 0x10149f54u, 4));
    const int pc0 = ((c->fcw >> 8) & 3) == 0;
    uint8_t *a = malloc(n);     // the alpha bytes first: a bad value returns 0 with the image untouched
    if (!a) return 0;
    const uint8_t *s = c->mem + src;
    for (uint32_t i = 0; i < n; i++, s += 4) {
        double v = xr(pc0, xr(pc0, (double)s[2] * k2) + xr(pc0, (double)s[1] * k1));    // 0x10120e52, 0x10120e5e, faddp 0x10120e67
        v = xr(pc0, v + xr(pc0, (double)s[0] * k0));                                    // 0x10120e6c, faddp 0x10120e72
        int32_t r;
        if (!fist32((double)(float)v, 1, &r)) { free(a); return 0; }                   // fstp dword 0x10120e74, fistp 0x10120ebc
        a[i] = (uint8_t)r;
    }
    uint32_t ecx = (w & 0xffff0000u) | (c->fcw & 0xffffu), edx = src + 4 * n, eax = a[n - 1];
    if (check) {
        inside = 1; f_10120e20(c); inside = 0;
        int diff = 0;
        for (uint32_t i = 0; i < n; i++) diff |= c->mem[dst + 4 * i + 3] != a[i];
        diff |= c->r[EAX] != eax || c->r[ECX] != ecx || c->r[EDX] != edx || c->r[ESP] != sp + 4 + 0x10;
        diff |= c->cf || !c->zf || c->sf || c->of || !c->pf || c->af;
        free(a);
        fc3_note(0, "grey_alpha", diff, "");
        return 1;
    }
    for (uint32_t i = 0; i < n; i++) c->mem[dst + 4 * i + 3] = a[i];
    free(a);
    c->r[EAX] = eax; c->r[ECX] = ecx; c->r[EDX] = edx;
    c->cf = c->sf = c->of = c->af = 0; c->zf = c->pf = 1;
    guest_ret(c, 0x10);
    return 1;
}

// e2mfc FUN_10120ee0(dst, src, dw, dh, sw, sh, unused), stdcall ret 0x1c: 16x16 fixed-point bilinear scale, 4 bytes per pixel.
// Steps (8.8 fixed point): ystep = nearest(float(sh*K5c / dh)) 0x10120f65, xstep from sw, dw 0x10120fb9 (RC = 0 forced). Pixel (x, y):
// taps at ((y>>8), (x>>8)) and the next row/column (unless on the last, 0x10121028, 0x10120079), weights fy, fx = 4 bits of y, x;
// per byte k the sum B*fy + D*(16-fy) times (16-fx), plus A*fy + C*(16-fy) times fx, then trunc(float(sum * K58)) (RC = 0xc00
// forced at 0x10121179); the pixel goes through DAT_101657a8 (bytes written in the order 2, 1, 0, 3) to dst.
// Only once both guards are set (table byte 0x10165c20, bit 0 of 0x101657ac): the lifted original does those first-time effects.
// Left as the original leaves it: 0x101657a8 = the last pixel. eax, ecx, edx are scratch (callers reload them); ebx, esi, edi, ebp kept.
int rn_bilinear_scale(Ctx *c) {
    static int check = -1, inside;
    if (check < 0) check = getenv("RN_FASTCHECK") != NULL;
    if (inside) return 0;
    uint32_t sp = c->r[ESP];
    if (!guest_ok(c, sp, 28)) return 0;
    if (!ld(c, 0x10165c20u, 1) || !(ld(c, 0x101657acu, 1) & 1)) return 0;
    uint32_t dst = ld(c, sp + 4, 4), src = ld(c, sp + 8, 4);
    int32_t dw = (int32_t)ld(c, sp + 12, 4), dh = (int32_t)ld(c, sp + 16, 4), sw = (int32_t)ld(c, sp + 20, 4), sh = (int32_t)ld(c, sp + 24, 4);
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0 || (int64_t)dw * dh > (1 << 24) || (int64_t)sw * sh > (1 << 26)) return 0;
    if (!guest_x87_ok(c) || !guest_ok(c, 0x10149f58u, 8)) return 0;
    uint64_t dn = 4ull * dw * dh, sn = 4ull * sw * sh;
    if (!guest_ok(c, dst, dn) || !guest_ok(c, src, sn) || overlap(dst, dn, src, sn)) return 0;
    if (on_frame(dst, dn, sp) || on_frame(src, sn, sp) || overlap(dst, dn, 0x101657a8u, 8) || overlap(src, sn, 0x101657a8u, 8)) return 0;
    const double k58 = qf(ld(c, 0x10149f58u, 4)), k5c = qf(ld(c, 0x10149f5cu, 4));
    const int pc0 = ((c->fcw >> 8) & 3) == 0;
    int32_t ystep, xstep;
    if (!fist32((double)(float)xr(pc0, xr(pc0, (double)sh * k5c) / (double)dh), 1, &ystep)) return 0;   // fild, fmul, fidiv, fstp, fistp
    if (!fist32((double)(float)xr(pc0, xr(pc0, (double)sw * k5c) / (double)dw), 1, &xstep)) return 0;
    uint32_t *out = malloc((size_t)dn);
    if (!out) return 0;
    const uint32_t ylim = ((uint32_t)sh << 8) - 0x100u, xlim = ((uint32_t)sw << 8) - 0x100u;    // 0x10120f40, 0x10120f49
    const uint8_t *base = c->mem + src;
    uint32_t *o = out, last = 0;
    uint32_t y = 0;
    for (int32_t row = 0; row < dh; row++, y += (uint32_t)ystep) {
        uint32_t r0 = (uint32_t)((int32_t)(y + (((int32_t)y >> 31) & 0xff)) >> 8) * (uint32_t)sw;   // 0x10121017..0x10121025
        uint32_t r1 = r0 + ((int32_t)y < (int32_t)ylim ? (uint32_t)sw : 0);
        uint32_t fy = (uint32_t)((int32_t)(y + (((int32_t)y >> 31) & 0xf)) >> 4) & 0xf, ify = 16 - fy;
        uint32_t x = 0;
        for (int32_t col = 0; col < dw; col++, x += (uint32_t)xstep) {
            uint32_t x1 = (int32_t)x < (int32_t)xlim ? x + 0x100 : x;
            uint32_t xf = (uint32_t)((int32_t)(x + (((int32_t)x >> 31) & 0xf)) >> 4) & 0xf, ifx = 16 - xf;
            uint32_t i1 = (uint32_t)((int32_t)(x1 + (((int32_t)x1 >> 31) & 0xff)) >> 8), i0 = (uint32_t)((int32_t)(x + (((int32_t)x >> 31) & 0xff)) >> 8);
            uint32_t oa = (r1 + i1) * 4, ob = (r1 + i0) * 4, oc = (i1 + r0) * 4, od = (i0 + r0) * 4;    // A, B, C, D of the asm
            if (oa > sn - 4 || ob > sn - 4 || oc > sn - 4 || od > sn - 4) { free(out); return 0; }       // a tap outside the source
            const uint8_t *A = base + oa, *B = base + ob, *C = base + oc, *D = base + od;
            uint32_t px = 0;
            for (int k = 0; k < 4; k++) {   // the byte order of the stores does not matter: only the last pixel's bytes stay
                uint32_t sum = (B[k] * fy + D[k] * ify) * ifx + (A[k] * fy + C[k] * ify) * xf;
                int32_t r;
                if (!fist32((double)(float)xr(pc0, (double)(int32_t)sum * k58), 0, &r)) { free(out); return 0; }
                px |= (uint32_t)(uint8_t)r << (8 * k);
            }
            *o++ = last = px;
        }
    }
    if (check) {
        inside = 1; f_10120ee0(c); inside = 0;
        int diff = memcmp(out, c->mem + dst, (size_t)dn) != 0 || ld(c, 0x101657a8u, 4) != last || c->r[ESP] != sp + 4 + 0x1c;
        free(out);
        char extra[64];
        snprintf(extra, sizeof extra, "(last %dx%d from %dx%d)", dw, dh, sw, sh);
        fc3_note(1, "bilinear_scale", diff, extra);
        return 1;
    }
    memcpy(c->mem + dst, out, (size_t)dn);
    free(out);
    st(c, 0x101657a8u, last, 4);
    guest_ret(c, 0x1c);
    return 1;
}

// grphmfc FUN_10203d20 (fastcall ecx = count, plain ret): operator new(count * 4) through the same thunk 0x10211404, every dword
// 00 00 00 ff (0xff000000), ++*sm_iNewCount (pointer at 0x10212088); returns the buffer in eax. Null: the original counts and
// throws R_Exception (SEH frame 0x1021180a, which has nothing to unwind at state -1). The stand-in does nothing then and returns 0,
// so the lifted original calls new again, counts once and throws: no double count, nothing leaked (a failed new allocated nothing).
// Left as the original leaves them: edx = the new count, ecx = fs:[0] (saved ExceptionList, restored at 0x10203e36). Flags not set.
int rn_new_pixels(Ctx *c) {
    static int check = -1, inside;
    if (check < 0) check = getenv("RN_FASTCHECK") != NULL;
    if (inside) return 0;
    int32_t n = (int32_t)c->r[ECX];
    if (n <= 0 || n > (1 << 28) || !guest_ok(c, c->r[ESP], 4) || !guest_ok(c, c->fs, 4)) return 0;
    uint32_t cp = ld(c, 0x10212088u, 4);
    if (!guest_ok(c, cp, 4)) return 0;
    uint32_t size = (uint32_t)n * 4, fs0 = ld(c, c->fs, 4), cnt0 = ld(c, cp, 4), sp = c->r[ESP];
    (void)cnt0; (void)sp;
    if (check) {    // the lifted original allocates; compare what it leaves with what this would
        inside = 1; f_10203d20(c); inside = 0;
        uint32_t p = c->r[EAX];
        if (c->halt || !p) return 1;    // it threw: nothing to compare
        int diff = !guest_ok(c, p, size);
        for (uint32_t i = 0; !diff && i < (uint32_t)n; i++) diff |= ld(c, p + 4 * i, 4) != 0xff000000u;
        diff |= ld(c, cp, 4) != cnt0 + 1 || c->r[EDX] != cnt0 + 1 || c->r[ECX] != fs0 || ld(c, c->fs, 4) != fs0 || c->r[ESP] != sp + 4;
        fc3_note(2, "new_pixels", diff, "");
        return 1;
    }
    uint32_t arg = size;
    uint32_t p = call_guest_cdecl(c, 0x10211404u, 1, &arg);     // the call at 0x10203d45
    if (c->halt || c->skipped) return 1;
    if (!p || !guest_ok(c, p, size)) return 0;
    uint32_t *d = (uint32_t *)(c->mem + p);
    for (int32_t i = 0; i < n; i++) d[i] = 0xff000000u;
    uint32_t cnt = ld(c, cp, 4) + 1;                            // 0x10203d79..0x10203d84
    st(c, cp, cnt, 4);
    c->r[EAX] = p; c->r[ECX] = fs0; c->r[EDX] = cnt;
    guest_ret(c, 0);
    return 1;
}

// rlmfc R_Memory::copy 0x10016730, fastcall (dst ecx, src edx, n on the stack, ret 4 at 0x10016839). Picks MMX, x87 or dword
// moves by alignment and caps (0x1001675f..0x1001676f), then 0x1c and 3 tail loops: always a forward byte copy.
// Leaves eax = dst (0x10016830), ecx = 0 (0x1001680b..0x1001681d), edx = 0 (0x1001681f..0x1001682e). Overlap and a negative
// n (sar at 0x10016771) stay with the original. Reads each JPEG source buffer during the level load.
int rn_memory_copy(Ctx *c) {
    static int check = -1, inside;
    if (check < 0) check = getenv("RN_FASTCHECK") != NULL;
    if (inside) return 0;
    uint32_t dst = c->r[ECX], src = c->r[EDX];
    if (!guest_ok(c, c->r[ESP], 8)) return 0;
    int32_t n = (int32_t)ld(c, c->r[ESP] + 4, 4);
    if (n < 0 || !guest_ok(c, dst, (uint32_t)n) || !guest_ok(c, src, (uint32_t)n) || overlap(dst, (uint32_t)n, src, (uint32_t)n)) return 0;
    if (check) {
        uint32_t sp = c->r[ESP];
        inside = 1; f_10016730(c); inside = 0;
        int diff = memcmp(c->mem + dst, c->mem + src, (size_t)n) != 0 || c->r[EAX] != dst || c->r[ECX] != 0 || c->r[EDX] != 0 || c->r[ESP] != sp + 8;
        fc3_note(3, "memory_copy", diff, "");
        return 1;
    }
    memcpy(c->mem + dst, c->mem + src, (size_t)n);
    c->r[EAX] = dst; c->r[ECX] = 0; c->r[EDX] = 0;
    guest_ret(c, 4);
    return 1;
}

// Level load peak (rea/map/flow.md). X_LevelDBImportMaterial import 0x5dc360 (thiscall, ret 0xc at 0x5dc6b9) decodes the JPEG into a
// new P_Map (+0x2d, owned flag +0x2c, both set there). The original uploads it only when getMap 0x5dc6f0 first runs, after every
// material of the level is decoded, and P_Map's FUN_10120370 frees the pixels right after the upload: 1.25 GB of held images with this
// install's 4096x2048 maps. Calling getMap as soon as the import returns uploads the same map, with the same fields, one at a time.
int rn_ldb_import(Ctx *c) {
    static int inside;
    if (inside) return 0;
    uint32_t self = c->r[ECX], sp = c->r[ESP];
    if (!guest_ok(c, sp, 16) || !guest_ok(c, self, 0x31)) return 0;
    uint32_t ra = ld(c, sp, 4);
    inside = 1; f_5dc360(c); inside = 0;
    if (c->halt || c->ret != ra || c->r[ESP] != sp + 16) return 1;      // it threw: the unwind goes on
    uint32_t map = ld(c, self + 0x2d, 4);
    if (ld(c, self + 0x2c, 1) != 1 || !guest_ok(c, map + 8, 4) || ld(c, map + 8, 4) != 1) return 1;     // 0x5dc6f6: state 1, not validated
    uint32_t ecx = c->r[ECX];
    c->r[ECX] = self;
    call_guest_cdecl(c, 0x5dc6f0u, 0, NULL);
    if (c->halt || c->skipped) return 1;
    c->r[ECX] = ecx;
    c->ret = ra;
    return 1;
}

// msvcrt longjmp 0x1081e40e: restores esp and jumps to the setjmp return (0x1081e476..0x1081e47c). With no SEH frame to unwind it never
// reaches RtlUnwind, so c->uw stays 0 and the first stale lifted call site would trap. It leaves the lifted frames like a catch
// continuation does (rea/map/game.md), so it sets the same state. The setjmp returns are lifted entries (patches.py LIFT_SEEDS).
int rn_longjmp(Ctx *c) {
    c->uw = 1;
    return 0;
}

// Driver texture readback FUN_1042db00 (P_D3D vtable +0x90: LockRect, P_Driver::convertTexture 0x10133210, UnlockRect;
// thiscall, 7 stack args, ret 0x1c at 0x1042dcaa; reached through the thunk 0x10401b59). P_Map::releaseDeviceTexture 0x101209a0 calls it (0x10120a69) to copy a
// MANAGED level into the map's G_Image +0x2c before it releases the texture. From P_Map::destruct (call 0x1011f827) that copy
// is dead: destruct frees +0x2c at 0x1011f938 and touches only its name strings in between (Ghidra 1011f7d0 + capstone).
// On the port each such lock is a GPU_READ of an evicted level (host/d3d8.c lock_mem) plus the lifted pixel loop:
// 41-59 ms on comic -> game for the 2 MB page maps (#5 sweep). P_Driver::destroyDriverObjects 0x10133194 keeps the image,
// so only the destruct chain is skipped: return address 0x10120a6f, and in 0x101209a0's ebp frame (push ebp at 0x101209a0)
// its own return address 0x1011f82c. The caller ignores eax (0x10120a6f stores 3, then reloads ecx/eax).
int rn_map_readback(Ctx *c) {
    uint32_t sp = c->r[ESP], bp = c->r[EBP];
    if (!guest_ok(c, sp, 4) || ld(c, sp, 4) != 0x10120a6fu || !guest_ok(c, bp + 4, 4) || ld(c, bp + 4, 4) != 0x1011f82cu) return 0;
    c->r[EAX] = 0;
    guest_ret(c, 0x1c);
    return 1;
}
