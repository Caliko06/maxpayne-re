// The packets of the frame (see gpu.h) and their two backends.
// Web: the game thread fills a packet's arenas in the shared heap and publishes it; web/gpu.js reads them in place on the
// page's thread, so nothing is copied twice and this worker never needs to yield to JavaScript.
// Native: a CPU backend runs the same packet and writes the presented frame to a PNG (--shot).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gpu.h"

enum { A_CMD, A_VTX, A_IDX, A_UNI, A_BLOB, A_N };
typedef struct { uint8_t *p; uint32_t n, cap; } Arena;
static Arena arena[GPU_SLOTS][A_N];
static int lowmem(void) { static int v = -1; if (v < 0) v = getenv("RN_LOWMEM") != NULL; return v; }
static int cur;             // the slot the game fills

// Only the slot being filled moves: the page reads the other one.
static uint8_t *room(int a, uint32_t n, uint32_t align, uint32_t *off) {
    Arena *r = &arena[cur][a];
    uint32_t o = (r->n + align - 1) & ~(align - 1);
    if (o + n > r->cap) {
        // Low memory (phone profile): no doubling, an arena's top is the packet limit in gpu_full, or one texture level.
        r->cap = lowmem() || o + n > r->cap * 2 ? o + n : r->cap * 2;
        if (r->cap < 1u << 20) r->cap = 1u << 20;
        r->p = realloc(r->p, r->cap);
        if (!r->p) { fprintf(stderr, "gpu: out of memory (%u bytes)\n", r->cap); exit(1); }
    }
    r->n = o + n;
    if (off) *off = o;
    return r->p + o;
}
void gpu_cmd(int op, int n, const uint32_t *w) {
    uint32_t *d = (uint32_t *)room(A_CMD, 4u * (1 + (uint32_t)n), 4, NULL);
    d[0] = (uint32_t)op | (uint32_t)n << 8;
    memcpy(d + 1, w, 4u * (uint32_t)n);
}
uint32_t *gpu_draw(void) {
    uint32_t *d = (uint32_t *)room(A_CMD, 4u * (1 + D_N), 4, NULL);
    d[0] = GPU_DRAW | D_N << 8;
    memset(d + 1, 0, 4u * D_N);
    return d + 1;
}
uint8_t *gpu_vtx(uint32_t n, uint32_t *off) { return room(A_VTX, n, 4, off); }
uint32_t *gpu_idx(uint32_t n, uint32_t *off) { return (uint32_t *)room(A_IDX, 4 * n, 4, off); }
uint8_t *gpu_blob(uint32_t n, uint32_t *off) { return room(A_BLOB, n, 4, off); }
GpuU *gpu_uni(uint32_t *off) {
    GpuU *u = (GpuU *)room(A_UNI, GPU_USTEP, GPU_USTEP, off);
    memset(u, 0, GPU_USTEP);
    return u;
}
void gpu_read(uint32_t id, uint32_t level, uint32_t w, uint32_t h, void *dst) {
    uint64_t p = (uint64_t)(uintptr_t)dst;
    uint32_t a[6] = {id, level, w, h, (uint32_t)p, (uint32_t)(p >> 32)};
    gpu_cmd(GPU_READ, 6, a);
}
// Soft limits; a single texture level may pass them. The biggest the driver makes is 4096x1024 (16 MB).
int gpu_full(void) {
    Arena *a = arena[cur];
    int s = lowmem() ? 2 : 0;       // a quarter of each limit
    return a[A_BLOB].n > 32u << 20 >> s || a[A_VTX].n > 16u << 20 >> s || a[A_IDX].n > 8u << 20 >> s || a[A_UNI].n > 8u << 20 >> s || a[A_CMD].n > 4u << 20 >> s;
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/threading.h>

static GpuBox box = {.abi = GPU_ABI};
EMSCRIPTEN_KEEPALIVE uint32_t rn_gpu_mailbox(void) { return (uint32_t)(uintptr_t)&box; }

void gpu_set_shot(const char *path) { (void)path; }

void gpu_submit(int present) {
    GpuPacket *p = &box.slot[box.seq % GPU_SLOTS];
    Arena *a = arena[cur];
    p->cmd = (uint32_t)(uintptr_t)a[A_CMD].p; p->ncmd = a[A_CMD].n / 4;
    p->vtx = (uint32_t)(uintptr_t)a[A_VTX].p; p->nvtx = a[A_VTX].n;
    p->idx = (uint32_t)(uintptr_t)a[A_IDX].p; p->nidx = a[A_IDX].n;
    p->uni = (uint32_t)(uintptr_t)a[A_UNI].p; p->nuni = a[A_UNI].n;
    p->blob = (uint32_t)(uintptr_t)a[A_BLOB].p; p->nblob = a[A_BLOB].n;
    cur = (cur + 1) % GPU_SLOTS;
    for (int k = 0; k < A_N; k++) arena[cur][k].n = 0;
    if (present) __atomic_add_fetch(&box.presents, 1, __ATOMIC_SEQ_CST);
    __atomic_add_fetch(&box.seq, 1, __ATOMIC_SEQ_CST);
    emscripten_futex_wake(&box.seq, 1);      // web/gpu.js waits on seq: without this the page never runs the packet
}
int gpu_screen_free(void) {
    return __atomic_load_n(&box.presents, __ATOMIC_SEQ_CST) + 1 - __atomic_load_n(&box.shown, __ATOMIC_SEQ_CST) < GPU_QUEUED;
}
// The slot the game fills next is free once the page finished the packet before the one it reads; the Present the game
// waits for the screen to take a frame (box.shown): CreateDevice asks for PresentationInterval 1, D3D's own wait.
double gpu_wait(void) {
    uint32_t n = __atomic_load_n(&box.seq, __ATOMIC_SEQ_CST), f = __atomic_load_n(&box.presents, __ATOMIC_SEQ_CST);
    for (;;) {
        uint32_t d = __atomic_load_n(&box.done, __ATOMIC_SEQ_CST);
        if (n - d < GPU_SLOTS) break;
        emscripten_futex_wait(&box.done, d, 100);
    }
    double t0 = emscripten_get_now(), warned = 0;      // from here on the game waits for the screen, not for the page
    for (;;) {
        uint32_t s = __atomic_load_n(&box.shown, __ATOMIC_SEQ_CST);
        if (f - s < GPU_QUEUED) break;
        emscripten_futex_wait(&box.shown, s, 100);
        // No halt: a level load keeps the page busy for 9-42 s. Say it once per 30 s so a stuck page is not a silent mute (#18).
        if (emscripten_get_now() - t0 >= warned + 30000) { warned += 30000; fprintf(stderr, "WARN gpu_wait: the page has not shown a frame for %d s\n", (int)(warned / 1000)); }
    }
    return emscripten_get_now() - t0;
}
static uint32_t reads_sent;
void gpu_wait_read(void) {
    reads_sent++;
    for (;;) {
        uint32_t r = __atomic_load_n(&box.reads, __ATOMIC_SEQ_CST);
        if (r - reads_sent == 0) break;
        emscripten_futex_wait(&box.reads, r, 100);
    }
}
#else

// ---- CPU backend ------------------------------------------------------------------------------
// A reference rasterizer for native shots: D3D9 rules (pixel centers at integer positions, top-left
// fill, clip to 0 <= z <= w), the shaders run in the sm1.c interpreter. Simple, not fast.
#include <math.h>
#include "sm1.h"

enum { IMG_COLOR, IMG_DEPTH, IMG_TEX };
typedef struct {
    uint32_t w, h, kind, levels;
    uint32_t *color;            // color target: ARGB
    float *z; uint8_t *stencil;
    uint32_t *lv[14];           // texture levels: BGRA8 (bytes B G R A), like the color targets' ARGB words
} Img;
static Img imgs[GPU_MAXID + 1];
static Sm1 *shaders[GPU_MAXID + 1];
static uint32_t *decls[GPU_MAXID + 1], ndecl[GPU_MAXID + 1];
static const char *shot;
static uint32_t draws, frames;
static int nodraw = -1, draw_from;      // RN_NODRAW, RN_DRAW_FROM (gpu_run)
uint64_t now_us(void);                  // host/win32.c

void gpu_set_shot(const char *path) { shot = path; }

static void png(const char *path, const uint32_t *argb, uint32_t w, uint32_t h);

static void img_free(Img *t) {
    free(t->color); free(t->z); free(t->stencil);
    for (int k = 0; k < 14; k++) free(t->lv[k]);
    memset(t, 0, sizeof *t);
}

// ---- texture sampling ----
typedef struct { const Img *img; const uint32_t *st; } Stage;
static void texel(const Img *t, int level, int x, int y, float o[4]) {
    uint32_t w = t->w >> level ? t->w >> level : 1;
    const uint32_t *px = t->kind == IMG_COLOR ? t->color : t->lv[level];
    if (!px) { o[0] = o[1] = o[2] = 0; o[3] = 1; return; }
    uint32_t v = px[(uint32_t)y * w + (uint32_t)x];
    o[0] = (v >> 16 & 255) / 255.f; o[1] = (v >> 8 & 255) / 255.f; o[2] = (v & 255) / 255.f; o[3] = (v >> 24) / 255.f;
}
static int address(int i, int n, uint32_t mode) {
    switch (mode) {
    case 2: { int p = 2 * n; i %= p; if (i < 0) i += p; return i < n ? i : p - 1 - i; }    // MIRROR
    case 3: return i < 0 ? 0 : i >= n ? n - 1 : i;                                         // CLAMP
    case 5: if (i < 0) i = -i - 1; return i >= n ? n - 1 : i;                              // MIRRORONCE
    default: i %= n; return i < 0 ? i + n : i;                                             // WRAP
    }
}
static void sample_level(const Img *t, const uint32_t *st, int level, int linear, const float uv[2], float o[4]) {
    int w = (int)(t->w >> level ? t->w >> level : 1), h = (int)(t->h >> level ? t->h >> level : 1);
    float x = uv[0] * (float)w, y = uv[1] * (float)h;
    if (!linear) { texel(t, level, address((int)floorf(x), w, st[T_AU]), address((int)floorf(y), h, st[T_AV]), o); return; }
    x -= 0.5f; y -= 0.5f;
    int x0 = (int)floorf(x), y0 = (int)floorf(y);
    float fx = x - (float)x0, fy = y - (float)y0, a[4], b[4], c[4], d[4];
    int xa = address(x0, w, st[T_AU]), xb = address(x0 + 1, w, st[T_AU]);
    int ya = address(y0, h, st[T_AV]), yb = address(y0 + 1, h, st[T_AV]);
    texel(t, level, xa, ya, a); texel(t, level, xb, ya, b); texel(t, level, xa, yb, c); texel(t, level, xb, yb, d);
    for (int k = 0; k < 4; k++) o[k] = (a[k] * (1 - fx) + b[k] * fx) * (1 - fy) + (c[k] * (1 - fx) + d[k] * fx) * fy;
}
// lod: log2 of the texel footprint of one pixel at level 0.
static void sample(const Img *t, const uint32_t *st, float lod, const float uv[2], float o[4]) {
    if (!t) { o[0] = o[1] = o[2] = 0; o[3] = 1; return; }
    int levels = t->kind == IMG_TEX ? (int)t->levels : 1;
    int mag = lod <= 0.f, filt = mag ? st[T_MAG] : st[T_MIN];
    int linear = filt >= 2;
    if (mag || !st[T_MIP] || levels == 1) { sample_level(t, st, 0, linear, uv, o); return; }
    float l = lod > (float)(levels - 1) ? (float)(levels - 1) : lod;
    if (st[T_MIP] == 1) { sample_level(t, st, (int)floorf(l + 0.5f), linear, uv, o); return; }
    int l0 = (int)floorf(l), l1 = l0 + 1 < levels ? l0 + 1 : l0;
    float f = l - (float)l0, a[4], b[4];
    sample_level(t, st, l0, linear, uv, a);
    sample_level(t, st, l1, linear, uv, b);
    for (int k = 0; k < 4; k++) o[k] = a[k] * (1 - f) + b[k] * f;
}

// ---- draws ----
// Screen position snapped to 1/16 pixel (X, Y: exact edge functions), z, 1/w, outputs.
#define SUB 16
typedef struct { float x, y, z, iw; int64_t X, Y; VsOut o; } Vert;
typedef struct {
    const uint32_t *h;
    const Sm1 *ps;
    const GpuU *u;
    Stage st[GPU_STAGES];
    VsOut at, dx, dy;           // outputs at the pixel and their screen derivatives
    Img *rt, *ds;
} Draw;

static void ps_sample(void *ctx, int k, const float uv[2], float out[4]) {
    Draw *d = ctx;
    const Img *t = d->st[k].img;
    float lod = 0;
    if (t) {
        const float *ddx = d->dx.t[k], *ddy = d->dy.t[k];
        float ux = ddx[0] * (float)t->w, vx = ddx[1] * (float)t->h, uy = ddy[0] * (float)t->w, vy = ddy[1] * (float)t->h;
        float m = fmaxf(ux * ux + vx * vx, uy * uy + vy * vy);
        lod = m > 0 ? 0.5f * log2f(m) : -16.f;
    }
    sample(t, d->st[k].st, lod, uv, out);
}
static int cmp(uint32_t f, float a, float b) {
    switch (f) {
    case 1: return 0; case 2: return a < b; case 3: return a == b; case 4: return a <= b;
    case 5: return a > b; case 6: return a != b; case 7: return a >= b;
    }
    return 1;
}
static void factor(uint32_t f, const float s[4], const float d[4], float o[4]) {
    for (int i = 0; i < 4; i++) {
        float v;
        switch (f) {
        case 1: v = 0; break;
        case 2: v = 1; break;
        case 3: v = s[i]; break;
        case 4: v = 1 - s[i]; break;
        case 5: v = s[3]; break;
        case 6: v = 1 - s[3]; break;
        case 7: v = d[3]; break;
        case 8: v = 1 - d[3]; break;
        case 9: v = d[i]; break;
        case 10: v = 1 - d[i]; break;
        case 11: v = i == 3 ? 1 : fminf(s[3], 1 - d[3]); break;
        default: v = 1;
        }
        o[i] = v;
    }
}
static float blend_op(uint32_t op, float s, float d) {
    switch (op) {
    case 2: return s - d;
    case 3: return d - s;
    case 4: return fminf(s, d);
    case 5: return fmaxf(s, d);
    }
    return s + d;
}
static float sat(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

static void lerp_out(VsOut *o, const Vert *v[3], const float b[3]) {
    // b: perspective-correct weights (already divided by the interpolated 1/w)
    float *dst = (float *)o;
    const float *s0 = (const float *)&v[0]->o, *s1 = (const float *)&v[1]->o, *s2 = (const float *)&v[2]->o;
    for (size_t k = 0; k < sizeof(VsOut) / 4; k++) dst[k] = s0[k] * b[0] + s1[k] * b[1] + s2[k] * b[2];
}
static void persp(const Vert *v[3], float e0, float e1, float e2, float area, VsOut *o) {
    float l[3] = {e0 / area * v[0]->iw, e1 / area * v[1]->iw, e2 / area * v[2]->iw}, s = l[0] + l[1] + l[2];
    float b[3] = {l[0] / s, l[1] / s, l[2] / s};
    lerp_out(o, v, b);
}
static int64_t iedge(const Vert *a, const Vert *b, int64_t px, int64_t py) { return (b->X - a->X) * (py - a->Y) - (b->Y - a->Y) * (px - a->X); }
static float edge(const Vert *a, const Vert *b, float px, float py) { return (float)iedge(a, b, (int64_t)(px * SUB), (int64_t)(py * SUB)); }
static int top_left(const Vert *a, const Vert *b) { int64_t dy = b->Y - a->Y, dx = b->X - a->X; return dy < 0 || (dy == 0 && dx > 0); }

static void raster(Draw *d, const Vert *v0, const Vert *v1, const Vert *v2) {
    const uint32_t *h = d->h;
    float area = (float)iedge(v0, v1, v2->X, v2->Y);
    if (area == 0) return;
    uint32_t cull = h[D_CULL];
    if ((cull == 2 && area > 0) || (cull == 3 && area < 0)) return;
    const Vert *v[3] = {v0, v1, v2};
    if (area < 0) { v[1] = v2; v[2] = v1; area = -area; }
    int x0 = (int)h[D_VP], y0 = (int)h[D_VP + 1], x1 = (int)(h[D_VP] + h[D_VP + 2]), y1 = (int)(h[D_VP + 1] + h[D_VP + 3]);
    if (x1 > (int)d->rt->w) x1 = (int)d->rt->w;
    if (y1 > (int)d->rt->h) y1 = (int)d->rt->h;
    float fx0 = (float)fminf(v[0]->X, fminf(v[1]->X, v[2]->X)) / SUB, fx1 = (float)fmaxf(v[0]->X, fmaxf(v[1]->X, v[2]->X)) / SUB;
    float fy0 = (float)fminf(v[0]->Y, fminf(v[1]->Y, v[2]->Y)) / SUB, fy1 = (float)fmaxf(v[0]->Y, fmaxf(v[1]->Y, v[2]->Y)) / SUB;
    if (fx0 > (float)x0) x0 = (int)ceilf(fx0);
    if (fy0 > (float)y0) y0 = (int)ceilf(fy0);
    if (fx1 + 1 < (float)x1) x1 = (int)floorf(fx1) + 1;
    if (fy1 + 1 < (float)y1) y1 = (int)floorf(fy1) + 1;
    int tl0 = top_left(v[1], v[2]), tl1 = top_left(v[2], v[0]), tl2 = top_left(v[0], v[1]);
    float zs[3];
    for (int k = 0; k < 3; k++) zs[k] = v[k]->z;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float px = (float)x, py = (float)y;     // D3D9: the pixel's sample point is its integer position
            int64_t i0 = iedge(v[1], v[2], (int64_t)x * SUB, (int64_t)y * SUB), i1 = iedge(v[2], v[0], (int64_t)x * SUB, (int64_t)y * SUB);
            int64_t i2 = iedge(v[0], v[1], (int64_t)x * SUB, (int64_t)y * SUB);
            if (i0 < 0 || i1 < 0 || i2 < 0) continue;
            if ((i0 == 0 && !tl0) || (i1 == 0 && !tl1) || (i2 == 0 && !tl2)) continue;
            float e0 = (float)i0, e1 = (float)i1, e2 = (float)i2;
            float z = (e0 * zs[0] + e1 * zs[1] + e2 * zs[2]) / area;
            size_t pix = (size_t)y * d->rt->w + (size_t)x;
            if (d->ds && h[D_ZEN] && !cmp(h[D_ZFUNC], z, d->ds->z[(size_t)y * d->ds->w + (size_t)x])) continue;
            persp(v, e0, e1, e2, area, &d->at);
            VsOut ox, oy;
            persp(v, edge(v[1], v[2], px + 1, py), edge(v[2], v[0], px + 1, py), edge(v[0], v[1], px + 1, py), area, &ox);
            persp(v, edge(v[1], v[2], px, py + 1), edge(v[2], v[0], px, py + 1), edge(v[0], v[1], px, py + 1), area, &oy);
            for (int k = 0; k < 8; k++)
                for (int j = 0; j < 4; j++) { d->dx.t[k][j] = ox.t[k][j] - d->at.t[k][j]; d->dy.t[k][j] = oy.t[k][j] - d->at.t[k][j]; }
            float c[4];
            if (!sm1_run_ps(d->ps, d->u->pc, &d->at, ps_sample, d, c)) continue;
            if (d->u->af[1]) {
                float f = d->at.fog;
                for (int k = 0; k < 3; k++) c[k] = d->u->fogc[k] + (c[k] - d->u->fogc[k]) * f;
            }
            for (int k = 0; k < 4; k++) c[k] = sat(c[k]);
            if (d->u->af[0] && !cmp(d->u->af[0], c[3], d->u->ar[0])) continue;
            uint32_t old = d->rt->color[pix];
            float dc[4] = {(old >> 16 & 255) / 255.f, (old >> 8 & 255) / 255.f, (old & 255) / 255.f, (old >> 24) / 255.f};
            if (h[D_BLEND]) {
                float fs[4], fd[4];
                factor(h[D_SRCB], c, dc, fs);
                factor(h[D_DSTB], c, dc, fd);
                if (h[D_SRCB] == 12) { for (int k = 0; k < 4; k++) { fs[k] = c[3]; fd[k] = 1 - c[3]; } }      // BOTHSRCALPHA
                if (h[D_SRCB] == 13) { for (int k = 0; k < 4; k++) { fs[k] = 1 - c[3]; fd[k] = c[3]; } }      // BOTHINVSRCALPHA
                for (int k = 0; k < 4; k++) c[k] = sat(blend_op(h[D_BOP], c[k] * fs[k], dc[k] * fd[k]));
            }
            uint32_t m = h[D_CWRITE], r = (uint32_t)lrintf(c[0] * 255), g = (uint32_t)lrintf(c[1] * 255);
            uint32_t b = (uint32_t)lrintf(c[2] * 255), a = (uint32_t)lrintf(c[3] * 255);
            uint32_t nv = (m & 1 ? r : old >> 16 & 255) << 16 | (m & 2 ? g : old >> 8 & 255) << 8 | (m & 4 ? b : old & 255) |
                          (m & 8 ? a : old >> 24) << 24;
            d->rt->color[pix] = nv;
            if (d->ds && h[D_ZEN] && h[D_ZWRITE]) d->ds->z[(size_t)y * d->ds->w + (size_t)x] = z;
        }
}

// Clips a clip-space polygon to 0 <= z <= w (D3D9), then fans it to the rasterizer.
static void to_screen(const uint32_t *h, const VsOut *o, Vert *v) {
    float vx = (float)h[D_VP], vy = (float)h[D_VP + 1], vw = (float)h[D_VP + 2], vh = (float)h[D_VP + 3], zmin, zmax;
    memcpy(&zmin, &h[D_VP + 4], 4); memcpy(&zmax, &h[D_VP + 5], 4);
    float iw = 1.f / o->pos[3];
    v->x = vx + (o->pos[0] * iw + 1) * 0.5f * vw;
    v->y = vy + (1 - o->pos[1] * iw) * 0.5f * vh;
    v->z = zmin + o->pos[2] * iw * (zmax - zmin);
    float lim = (float)(1 << 22);
    v->X = (int64_t)lrintf(fmaxf(-lim, fminf(lim, v->x)) * SUB);
    v->Y = (int64_t)lrintf(fmaxf(-lim, fminf(lim, v->y)) * SUB);
    v->iw = iw;
    v->o = *o;
}
static void clip_tri(Draw *d, const VsOut *a, const VsOut *b, const VsOut *c) {
    VsOut in[9], out[9];
    int n = 3;
    in[0] = *a; in[1] = *b; in[2] = *c;
    for (int plane = 0; plane < 2; plane++) {
        int m = 0;
        for (int i = 0; i < n; i++) {
            const VsOut *p = &in[i], *q = &in[(i + 1) % n];
            float dp = plane ? p->pos[3] - p->pos[2] : p->pos[2], dq = plane ? q->pos[3] - q->pos[2] : q->pos[2];
            if (dp >= 0) out[m++] = *p;
            if ((dp >= 0) != (dq >= 0)) {
                float t = dp / (dp - dq);
                float *o = (float *)&out[m++];
                const float *fp = (const float *)p, *fq = (const float *)q;
                for (size_t k = 0; k < sizeof(VsOut) / 4; k++) o[k] = fp[k] + (fq[k] - fp[k]) * t;
            }
        }
        n = m;
        memcpy(in, out, sizeof(VsOut) * (size_t)n);
        if (n < 3) return;
    }
    Vert v[9];
    for (int i = 0; i < n; i++) to_screen(d->h, &in[i], &v[i]);
    for (int i = 1; i + 1 < n; i++) raster(d, &v[0], &v[i], &v[i + 1]);
}

// One vertex element (D3DDECLTYPE) to four floats; missing components are 0, 0, 0, 1.
static void fetch(const uint8_t *p, uint32_t type, float o[4]) {
    o[0] = o[1] = o[2] = 0; o[3] = 1;
    switch (type) {
    case 0: case 1: case 2: case 3: memcpy(o, p, 4u * (type + 1)); return;
    case 4: o[0] = p[2] / 255.f; o[1] = p[1] / 255.f; o[2] = p[0] / 255.f; o[3] = p[3] / 255.f; return;   // D3DCOLOR
    case 5: for (int k = 0; k < 4; k++) o[k] = p[k]; return;
    case 8: for (int k = 0; k < 4; k++) o[k] = p[k] / 255.f; return;
    case 6: case 7: for (int k = 0; k < (type == 6 ? 2 : 4); k++) { int16_t s; memcpy(&s, p + 2 * k, 2); o[k] = s; } return;
    }
    fprintf(stderr, "gpu: vertex element type %u\n", type);
    exit(1);
}

static void run_draw(const uint32_t *h) {
    Sm1 *vs = shaders[h[D_VS]], *ps = shaders[h[D_PS]];
    const uint32_t *el = decls[h[D_DECL]];
    uint32_t nel = ndecl[h[D_DECL]], nv = h[D_NVERT], ni = h[D_NIDX], stride = h[D_STRIDE];
    if (h[D_TOPO] < 4) return;                  // lines: web only
    const Arena *pk = arena[cur];
    const GpuU *u = (const GpuU *)(pk[A_UNI].p + h[D_UOFF]);
    const uint8_t *vb = pk[A_VTX].p + h[D_VOFF];
    const uint32_t *ix = (const uint32_t *)(pk[A_IDX].p + h[D_IOFF]);
    // shader input register -> element offset and type
    int off[16], type[16];
    for (int r = 0; r < 16; r++) {
        off[r] = -1;
        if (!(vs->inputs >> r & 1)) continue;
        for (uint32_t k = 0; k < nel; k++) {
            uint32_t a = el[2 * k], b = el[2 * k + 1];
            if ((a & 0xffff) == 0 && (b >> 16 & 255) == vs->in_usage[r] && (b >> 24) == vs->in_index[r]) { off[r] = (int)(a >> 16); type[r] = b & 255; }
        }
        if (off[r] < 0) { fprintf(stderr, "gpu: vs input v%d (usage %u/%u) not in the declaration\n", r, vs->in_usage[r], vs->in_index[r]); exit(1); }
    }
    VsOut *out = malloc(sizeof(VsOut) * (nv ? nv : 1));
    for (uint32_t i = 0; i < nv; i++) {
        float in[16][4] = {{0}};
        for (int r = 0; r < 16; r++) if (off[r] >= 0) fetch(vb + i * stride + (uint32_t)off[r], (uint32_t)type[r], in[r]);
        sm1_run_vs(vs, u->vc, (const float (*)[4])in, &out[i]);
    }
    Draw d;
    memset(&d, 0, sizeof d);
    d.h = h; d.ps = ps; d.u = u;
    d.rt = &imgs[h[D_RT]];
    d.ds = h[D_DS] ? &imgs[h[D_DS]] : NULL;
    for (int k = 0; k < GPU_STAGES; k++) {
        uint32_t id = h[D_TEX + D_TEXN * k + T_ID];
        d.st[k].img = id ? &imgs[id] : NULL;
        d.st[k].st = &h[D_TEX + D_TEXN * k];
    }
    uint32_t n = ni ? ni : nv, ntri = h[D_TOPO] == 4 ? n / 3 : n >= 2 ? n - 2 : 0;
    for (uint32_t t = 0; t < ntri; t++) {
        uint32_t a, b, c;
        if (h[D_TOPO] == 4) { a = 3 * t; b = a + 1; c = a + 2; }
        else if (t & 1) { a = t + 1; b = t; c = t + 2; }       // strip: odd triangles keep the winding
        else { a = t; b = t + 1; c = t + 2; }
        if (ni) { a = ix[a]; b = ix[b]; c = ix[c]; }
        if (a >= nv || b >= nv || c >= nv) continue;
        clip_tri(&d, &out[a], &out[b], &out[c]);
    }
    free(out);
}

static void gpu_run(const uint32_t *w, uint32_t n) {
    for (uint32_t i = 0; i < n;) {
        uint32_t op = w[i] & 0xff, len = w[i] >> 8;
        const uint32_t *a = w + i + 1;
        i += 1 + len;
        if (op == GPU_TARGET) {
            Img *t = &imgs[a[0]];
            img_free(t);
            *t = (Img){a[1], a[2], a[3] ? IMG_DEPTH : IMG_COLOR, 1};
            if (a[3] == 0) t->color = calloc((size_t)a[1] * a[2], 4);
            else { t->z = calloc((size_t)a[1] * a[2], 4); t->stencil = calloc((size_t)a[1] * a[2], 1); }
        } else if (op == GPU_CLEAR) {
            Img *rt = a[0] ? &imgs[a[0]] : NULL, *ds = a[1] ? &imgs[a[1]] : NULL;
            float z;
            memcpy(&z, &a[4], 4);
            for (uint32_t y = a[7]; y < a[9]; y++)
                for (uint32_t x = a[6]; x < a[8]; x++) {
                    if (rt && (a[2] & 1)) rt->color[y * rt->w + x] = a[3];
                    if (ds && (a[2] & 2)) ds->z[y * ds->w + x] = z;
                    if (ds && (a[2] & 4)) ds->stencil[y * ds->w + x] = (uint8_t)a[5];
                }
        } else if (op == GPU_SHADER) {
            Sm1 *s = calloc(1, sizeof *s);
            char err[128];
            if (sm1_decode(s, a + 21, a[2], err, sizeof err)) { fprintf(stderr, "gpu: %s\n", err); exit(1); }
            if (shaders[a[0]]) { sm1_free(shaders[a[0]]); free(shaders[a[0]]); }
            shaders[a[0]] = s;
        } else if (op == GPU_DECL) {
            free(decls[a[0]]);
            decls[a[0]] = malloc(8u * a[1] + 4);
            memcpy(decls[a[0]], a + 2, 8u * a[1]);
            ndecl[a[0]] = a[1];
        } else if (op == GPU_TEXTURE) {
            Img *t = &imgs[a[0]];
            img_free(t);
            *t = (Img){a[1], a[2], IMG_TEX, a[3]};
        } else if (op == GPU_TEXDATA) {
            Img *t = &imgs[a[0]];
            if (a[1] < 14) {
                free(t->lv[a[1]]);
                t->lv[a[1]] = malloc(4u * a[2] * a[3]);
                memcpy(t->lv[a[1]], arena[cur][A_BLOB].p + a[4], 4u * a[2] * a[3]);
            }
        } else if (op == GPU_FREE) {
            img_free(&imgs[a[0]]);
        } else if (op == GPU_GAMMA) {
            // applied by web/gpu.js when a frame goes to the screen; shots show the frame buffer
        } else if (op == GPU_READ) {
            const Img *t = &imgs[a[0]];
            const uint32_t *src = t->kind == IMG_COLOR ? t->color : t->lv[a[1]];
            if (src) memcpy((void *)(uintptr_t)((uint64_t)a[5] << 32 | a[4]), src, 4u * a[2] * a[3]);
        } else if (op == GPU_DRAW) {
            // RN_NODRAW=1: no rasterizing (fast runs that look for the next missing API, not at pixels).
            // RN_DRAW_FROM=<frame>: rasterize from that frame on (a late shot of a scene redrawn whole each frame).
            // RN_DRAW_AT=<seconds>: from the first frame after that many seconds of run time.
            if (nodraw < 0) { nodraw = getenv("RN_NODRAW") != NULL; draw_from = getenv("RN_DRAW_FROM") ? atoi(getenv("RN_DRAW_FROM")) : 0; }
            static double draw_at = -1;
            if (draw_at < 0) { draw_at = getenv("RN_DRAW_AT") ? atof(getenv("RN_DRAW_AT")) : 0; if (draw_at > 0) draw_from = 1 << 30; }
            if (draw_at > 0 && (double)now_us() / 1e6 >= draw_at) { draw_from = (int)frames; draw_at = 0; }
            if (!nodraw && (int)frames >= draw_from) run_draw(a);
            draws++;
        } else if (op == GPU_PRESENT) {
            if (shot && !nodraw && (int)frames >= draw_from) png(shot, imgs[a[0]].color, a[1], a[2]);
            // RN_SHOT_FRAMES=a-b: also keep frames a..b as <shot>.<frame>.png (debug).
            static int lo = -1, hi = -1;
            if (lo == -1) { const char *e = getenv("RN_SHOT_FRAMES"); lo = -2; if (e && sscanf(e, "%d-%d", &lo, &hi) != 2) lo = -2; }
            if (shot && (int)frames >= lo && (int)frames <= hi) {
                char path[600];
                snprintf(path, sizeof path, "%s.%u.png", shot, frames);
                png(path, imgs[a[0]].color, a[1], a[2]);
            }
            if (frames < 3 || frames % 100 == 0) fprintf(stderr, "gpu: frame %u: %u draws\n", frames, draws);
            frames++;
            draws = 0;
        } else {
            fprintf(stderr, "gpu: unknown command %u\n", op);
            exit(1);
        }
    }
}

void gpu_submit(int present) {
    (void)present;
    gpu_run((const uint32_t *)arena[cur][A_CMD].p, arena[cur][A_CMD].n / 4);
    for (int k = 0; k < A_N; k++) arena[cur][k].n = 0;
}
int gpu_screen_free(void) { return 1; }
double gpu_wait(void) { return 0; }
void gpu_wait_read(void) {}

// PNG with stored (uncompressed) deflate blocks: no zlib needed.
static uint32_t crc_tab[256];
static uint32_t crc(uint32_t c, const uint8_t *p, size_t n) {
    if (!crc_tab[1])
        for (uint32_t k = 0; k < 256; k++) {
            uint32_t v = k;
            for (int j = 0; j < 8; j++) v = v & 1 ? 0xedb88320u ^ (v >> 1) : v >> 1;
            crc_tab[k] = v;
        }
    c = ~c;
    while (n--) c = crc_tab[(c ^ *p++) & 0xff] ^ (c >> 8);
    return ~c;
}
static void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n) {
    uint8_t h[8];
    be32(h, n);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    fwrite(d, 1, n, f);
    uint32_t v = crc(crc(0, h + 4, 4), d, n);
    be32(h, v);
    fwrite(h, 1, 4, f);
}
static void png(const char *path, const uint32_t *argb, uint32_t w, uint32_t h) {
    size_t row = 1 + 3 * (size_t)w, raw_n = row * h;
    uint8_t *raw = malloc(raw_n);
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *r = raw + y * row;
        r[0] = 0;
        for (uint32_t x = 0; x < w; x++) {
            uint32_t v = argb[y * w + x];
            r[1 + 3 * x] = v >> 16; r[2 + 3 * x] = v >> 8; r[3 + 3 * x] = v;
        }
    }
    size_t nblk = (raw_n + 65534) / 65535, zn = 2 + raw_n + 5 * nblk + 4;
    uint8_t *z = malloc(zn), *q = z;
    *q++ = 0x78; *q++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t off = 0; off < raw_n; off += 65535) {
        uint32_t n = raw_n - off < 65535 ? (uint32_t)(raw_n - off) : 65535;
        *q++ = off + n == raw_n;
        *q++ = n; *q++ = n >> 8; *q++ = ~n; *q++ = (~n) >> 8;
        memcpy(q, raw + off, n);
        q += n;
        for (uint32_t k = 0; k < n; k++) { a = (a + raw[off + k]) % 65521; b = (b + a) % 65521; }
    }
    be32(q, b << 16 | a);
    q += 4;
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "gpu: cannot write %s\n", path); exit(1); }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ihdr[13];
    be32(ihdr, w); be32(ihdr + 4, h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)(q - z));
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
}
#endif
