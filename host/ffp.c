// Fixed-function vertex and pixel processing of Direct3D 8 as shader model 1 tokens (host/ffp.h).
#include <stdlib.h>
#include <string.h>
#include "ffp.h"
#include "sm1.h"

// ---- a tiny assembler: token encodings of the DirectX 9 "Shader Code Format" docs ------------------------------
typedef struct { uint32_t *t; uint32_t n, cap; } Asm;
static void put(Asm *a, uint32_t v) {
    if (a->n == a->cap) a->t = realloc(a->t, 4 * (a->cap = a->cap ? 2 * a->cap : 256));
    a->t[a->n++] = v;
}
static uint32_t regbits(int type) { return (uint32_t)(type & 7) << 28 | (uint32_t)(type >> 3 & 3) << 11; }
#define XYZW 0xe4u
#define SW(x, y, z, w) ((uint32_t)(x) | (uint32_t)(y) << 2 | (uint32_t)(z) << 4 | (uint32_t)(w) << 6)
#define RX SW(0, 0, 0, 0)
#define RY SW(1, 1, 1, 1)
#define RZ SW(2, 2, 2, 2)
#define RW SW(3, 3, 3, 3)
// Source modifiers (D3DSPSM_*) and destination flags.
enum { M_NONE = 0, M_NEG = 1, M_BIAS = 2, M_BX2 = 4, M_COMP = 6 };
typedef struct { int type, num; uint32_t swz; int mod; } S;
typedef struct { int type, num, mask, sat, shift; } D;
static S s_(int type, int num) { return (S){type, num, XYZW, M_NONE}; }
static S sw(S s, uint32_t swz) { s.swz = swz; return s; }
static S md(S s, int mod) { s.mod = mod; return s; }
static D d_(int type, int num, int mask) { return (D){type, num, mask, 0, 0}; }
static void dst(Asm *a, D d) {
    put(a, 0x80000000u | regbits(d.type) | (uint32_t)d.mask << 16 | (d.sat ? 1u << 20 : 0) | (uint32_t)(d.shift & 15) << 24 | (uint32_t)d.num);
}
static void src(Asm *a, S s) { put(a, 0x80000000u | regbits(s.type) | s.swz << 16 | (uint32_t)s.mod << 24 | (uint32_t)s.num); }
static void op1(Asm *a, int op, D d, S x) { put(a, (uint32_t)op); dst(a, d); src(a, x); }
static void op2(Asm *a, int op, D d, S x, S y) { put(a, (uint32_t)op); dst(a, d); src(a, x); src(a, y); }
static void op3(Asm *a, int op, D d, S x, S y, S z) { put(a, (uint32_t)op); dst(a, d); src(a, x); src(a, y); src(a, z); }
static void dcl(Asm *a, int usage, int index, int reg) {
    put(a, OP_DCL); put(a, 0x80000000u | (uint32_t)usage | (uint32_t)index << 16); dst(a, d_(R_INPUT, reg, 15));
}
#define R(n) s_(R_TEMP, n)
#define V(n) s_(R_INPUT, n)
#define C(n) s_(R_CONST, n)
#define T(n) s_(R_TEXTURE, n)
#define DR(n, m) d_(R_TEMP, n, m)
enum { MX = 1, MY = 2, MZ = 4, MW = 8, MXYZ = 7, MALL = 15 };

// ---- vertex ------------------------------------------------------------------------------------------------------
// Temps: r0 position / scratch, r1 view normal, r2 view position, r3 diffuse sum, r4 specular sum, r5 ambient sum,
// r6 vector to the light, r7 lit() input, r8 lit() output, r9 distance terms, r10 attenuation x spot, r11 eye vector.
static S material(int src, int cst) { return src == 1 ? V(2) : src == 2 ? V(3) : C(cst); }
uint32_t *ffp_vs(const FfpVs *k, uint32_t *n, const char **why) {
    Asm a = {0};
    put(&a, 0xfffe0101u);                                   // vs_1_1
    dcl(&a, k->pos_usage, 0, 0);
    if (k->normal) dcl(&a, 3, 0, 1);                        // NORMAL
    if (k->diffuse) dcl(&a, 10, 0, 2);                      // COLOR0
    if (k->specular) dcl(&a, 10, 1, 3);                     // COLOR1
    for (int t = 0; t < k->ntex; t++) dcl(&a, 5, t, 4 + t); // TEXCOORDt
    S k0 = sw(C(FC_K), RX), k1 = sw(C(FC_K), RZ);          // FC_K = (0, 0.5, 1, 2)
    D opos = d_(R_RASTOUT, 0, MALL);
    if (k->pos == 2) {
        // Pretransformed: screen x, y, window z and 1/w back to clip space for the viewport the backend applies.
        op3(&a, OP_MAD, DR(0, MX | MY), V(0), sw(C(FC_PRE), SW(0, 1, 0, 1)), sw(C(FC_PRE), SW(2, 3, 2, 3)));
        op3(&a, OP_MAD, DR(0, MZ), sw(V(0), RZ), sw(C(FC_PREZ), RX), sw(C(FC_PREZ), RY));
        op1(&a, OP_MOV, DR(0, MW), k1);
        op1(&a, OP_RCP, DR(1, MW), sw(V(0), RW));
        op2(&a, OP_MUL, opos, R(0), sw(R(1), RW));
    } else {
        op2(&a, OP_M4x4, opos, V(0), C(FC_WVP));
    }
    int lit = k->lights && k->pos != 2;
    if (lit || k->fog == 1) op2(&a, OP_M4x4, DR(2, MALL), V(0), C(FC_WV));
    if (lit) {
        if (k->normal) {
            op2(&a, OP_M3x3, DR(1, MXYZ), V(1), C(FC_NRM));
            if (k->normalize) {
                op2(&a, OP_DP3, DR(1, MW), R(1), R(1));
                op1(&a, OP_RSQ, DR(1, MW), sw(R(1), RW));
                op2(&a, OP_MUL, DR(1, MXYZ), R(1), sw(R(1), RW));
            }
        } else op1(&a, OP_MOV, DR(1, MALL), k0);            // no normals: N = 0 (only ambient and emissive light)
        for (int r = 3; r <= 5; r++) op1(&a, OP_MOV, DR(r, MALL), k0);
        if (k->specular_on) {                               // eye vector: -P (local viewer) or (0, 0, -1)
            if (k->localviewer) {
                op2(&a, OP_DP3, DR(11, MW), R(2), R(2));
                op1(&a, OP_RSQ, DR(11, MW), sw(R(11), RW));
                op2(&a, OP_MUL, DR(11, MXYZ), md(R(2), M_NEG), sw(R(11), RW));
            } else {
                op1(&a, OP_MOV, DR(11, MALL), k0);
                op1(&a, OP_MOV, DR(11, MZ), md(k1, M_NEG));
            }
        }
        for (int i = 0; i < 8; i++) {
            if (!(k->lights >> i & 1)) continue;
            int b = FC_LIGHT + FC_LSTEP * i, kind = k->kind[i];
            if (kind == 3) {
                op1(&a, OP_MOV, DR(6, MXYZ), C(b + 4));
                op1(&a, OP_MOV, DR(10, MX), k1);
            } else {
                // L = light position - P, d = |L|, attenuation 1 / (a0 + a1 d + a2 d^2), 0 past the range.
                op2(&a, OP_SUB, DR(6, MXYZ), C(b + 3), R(2));
                op2(&a, OP_DP3, DR(9, MW), R(6), R(6));              // d^2
                op1(&a, OP_RSQ, DR(9, MY), sw(R(9), RW));            // 1/d
                op2(&a, OP_MUL, DR(9, MX), sw(R(9), RW), sw(R(9), RY)); // d
                op2(&a, OP_MUL, DR(6, MXYZ), R(6), sw(R(9), RY));    // unit L
                op3(&a, OP_MAD, DR(10, MX), sw(C(b + 5), RY), sw(R(9), RX), sw(C(b + 5), RX));
                op3(&a, OP_MAD, DR(10, MX), sw(C(b + 5), RZ), sw(R(9), RW), sw(R(10), RX));
                op1(&a, OP_RCP, DR(10, MX), sw(R(10), RX));
                op2(&a, OP_SLT, DR(10, MY), sw(R(9), RX), sw(C(b + 5), RW));
                op2(&a, OP_MUL, DR(10, MX), sw(R(10), RX), sw(R(10), RY));
                if (kind == 2) {
                    // rho = -L . axis; spot = clamp((rho - cos phi/2) / (cos theta/2 - cos phi/2), 0, 1) ^ falloff
                    op2(&a, OP_DP3, DR(9, MZ), md(R(6), M_NEG), C(b + 4));
                    op2(&a, OP_ADD, DR(9, MZ), sw(R(9), RZ), md(sw(C(b + 6), RY), M_NEG));
                    op2(&a, OP_MUL, DR(9, MZ), sw(R(9), RZ), sw(C(b + 6), RW));
                    op2(&a, OP_MAX, DR(9, MZ), sw(R(9), RZ), k0);
                    op2(&a, OP_MIN, DR(9, MZ), sw(R(9), RZ), k1);
                    // Outside the cone spot is 0 whatever the falloff: log(0) * falloff 0 would give exp(0) = 1.
                    op2(&a, OP_SLT, DR(10, MY), k0, sw(R(9), RZ));        // r10.y (range mask, used above) = inside
                    op1(&a, OP_LOG, DR(9, MZ), sw(R(9), RZ));
                    op2(&a, OP_MUL, DR(9, MZ), sw(R(9), RZ), sw(C(b + 6), RZ));
                    op1(&a, OP_EXP, DR(9, MZ), sw(R(9), RZ));
                    op2(&a, OP_MUL, DR(10, MX), sw(R(10), RX), sw(R(9), RZ));
                    op2(&a, OP_MUL, DR(10, MX), sw(R(10), RX), sw(R(10), RY));
                }
            }
            // lit((N.L, N.H, 0, power)): y = max(N.L, 0), z = (N.H)^power when N.L > 0.
            op2(&a, OP_DP3, DR(7, MX), R(1), R(6));
            if (k->specular_on) {
                op2(&a, OP_ADD, DR(0, MXYZ), R(6), R(11));
                op2(&a, OP_DP3, DR(0, MW), R(0), R(0));
                op1(&a, OP_RSQ, DR(0, MW), sw(R(0), RW));
                op2(&a, OP_MUL, DR(0, MXYZ), R(0), sw(R(0), RW));
                op2(&a, OP_DP3, DR(7, MY), R(1), R(0));
                op1(&a, OP_MOV, DR(7, MW), sw(C(FC_MSPEC), RW));     // material power rides in the specular w
            } else {
                op1(&a, OP_MOV, DR(7, MY), k0);
                op1(&a, OP_MOV, DR(7, MW), k0);
            }
            op1(&a, OP_LIT, DR(8, MALL), R(7));
            op2(&a, OP_MUL, DR(8, MY | MZ), R(8), sw(R(10), RX));
            op3(&a, OP_MAD, DR(3, MXYZ), C(b), sw(R(8), RY), R(3));
            if (k->specular_on) op3(&a, OP_MAD, DR(4, MXYZ), C(b + 1), sw(R(8), RZ), R(4));
            op3(&a, OP_MAD, DR(5, MXYZ), C(b + 2), sw(R(10), RX), R(5));
        }
        // diffuse = emissive + ambient material (global ambient + lights) + diffuse material x diffuse light
        S mdif = material(k->src_diffuse, FC_MDIF), mamb = material(k->src_ambient, FC_MAMB);
        S mspe = material(k->src_specular, FC_MSPEC), memi = material(k->src_emissive, FC_MEMI);
        op2(&a, OP_ADD, DR(5, MXYZ), R(5), C(FC_GAMB));
        op3(&a, OP_MAD, DR(5, MXYZ), R(5), mamb, memi);
        op3(&a, OP_MAD, d_(R_ATTROUT, 0, MXYZ), R(3), mdif, R(5));
        op1(&a, OP_MOV, d_(R_ATTROUT, 0, MW), sw(mdif, RW));
        if (k->specular_on) {
            op2(&a, OP_MUL, d_(R_ATTROUT, 1, MXYZ), R(4), mspe);
            op1(&a, OP_MOV, d_(R_ATTROUT, 1, MW), k0);
        } else op1(&a, OP_MOV, d_(R_ATTROUT, 1, MALL), k0);
    } else {
        op1(&a, OP_MOV, d_(R_ATTROUT, 0, MALL), k->diffuse ? V(2) : k1);    // no diffuse color: opaque white
        op1(&a, OP_MOV, d_(R_ATTROUT, 1, MALL), k->specular ? V(3) : k0);
    }
    if (k->fog == 1) op3(&a, OP_MAD, d_(R_RASTOUT, 1, MX), sw(R(2), RZ), sw(C(FC_FOG), RX), sw(C(FC_FOG), RY));
    else if (k->fog == 2) op1(&a, OP_MOV, d_(R_RASTOUT, 1, MX), k->specular ? sw(V(3), RW) : k1);
    // Texture coordinates ("Texture Coordinate Processing", DirectX 8.1 docs): the set or the generated vector, then
    // the stage's D3DTS_TEXTUREn when TEXTURETRANSFORMFLAGS has a count. The input of a transform is the set padded
    // with 1 after its last component ((u, v) -> (u, v, 1, 0)); generated vectors are (x, y, z, 1) in view space.
    int slot = 0;
    for (int t = 0; t < k->nstages; t++) {
        uint32_t set = k->tci[t] & 0xffff, gen = k->tci[t] >> 16;
        int count = k->ttff[t] & 7, proj = k->ttff[t] & 0x10;
        D ot = d_(R_TEXCRDOUT, t, MALL);
        S in = k0;
        if (gen == 1 || gen == 2) {
            if (k->pos == 2 || (gen == 1 && !k->normal)) { *why = "texture coordinate generation without a position or normal"; free(a.t); return NULL; }
            if (gen == 2) op2(&a, OP_M4x4, DR(0, MALL), V(0), C(FC_WV));            // CAMERASPACEPOSITION
            else {                                                                  // CAMERASPACENORMAL
                op2(&a, OP_M3x3, DR(0, MXYZ), V(1), C(FC_NRM));
                if (k->normalize) {
                    op2(&a, OP_DP3, DR(0, MW), R(0), R(0));
                    op1(&a, OP_RSQ, DR(0, MW), sw(R(0), RW));
                    op2(&a, OP_MUL, DR(0, MXYZ), R(0), sw(R(0), RW));
                }
                op1(&a, OP_MOV, DR(0, MW), k1);
            }
            in = R(0);
        } else if (gen) { *why = "texture coordinate generation (D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR)"; free(a.t); return NULL; }
        else if (set < k->ntex) {
            int dim = k->tdim[set] ? k->tdim[set] : 2;
            if (!count || dim == 4) in = V(4 + (int)set);
            else {
                op1(&a, OP_MOV, DR(0, MALL), k0);
                op1(&a, OP_MOV, DR(0, (1 << dim) - 1), V(4 + (int)set));
                op1(&a, OP_MOV, DR(0, 1 << dim), k1);
                in = R(0);
            }
        }
        if (!count) { op1(&a, OP_MOV, ot, in); continue; }
        if (slot == FC_TEXMN) { *why = "more than two texture transforms"; free(a.t); return NULL; }
        int m = FC_TEXM + 4 * slot++;
        if (!proj || count == 4) { op2(&a, OP_M4x4, ot, in, C(m)); continue; }
        // PROJECTED: the pixel stage divides by the last of the count components; it reads the divisor from w.
        op2(&a, OP_M4x4, DR(7, MALL), in, C(m));
        op1(&a, OP_MOV, ot, sw(R(7), count == 3 ? SW(0, 1, 2, 2) : count == 2 ? SW(0, 1, 1, 1) : RX));
    }
    put(&a, 0x0000ffffu);
    *n = a.n;
    return a.t;
}

// ---- pixel -------------------------------------------------------------------------------------------------------
// D3DTA_* argument: register, then the COMPLEMENT (0x10) and ALPHAREPLICATE (0x20) modifiers.
static S arg(uint32_t ta, int stage, int alpha) {
    S r;
    switch (ta & 15) {
    case 0: r = V(0); break;                                // DIFFUSE
    case 1: r = stage ? R(0) : V(0); break;                 // CURRENT (stage 0: the diffuse color)
    case 2: r = T(stage); break;                            // TEXTURE
    case 3: r = C(0); break;                                // TFACTOR
    case 4: r = V(1); break;                                // SPECULAR
    case 5: r = R(1); break;                                // TEMP
    default: r = V(0);
    }
    if ((ta & 0x20) || alpha) r = sw(r, RW);
    if (ta & 0x10) r = md(r, M_COMP);
    return r;
}
// One operation of a stage into register rd (mask: rgb or alpha). 0 = done, else unsupported.
static int stage_op(Asm *a, uint32_t op, uint32_t a0, uint32_t a1, uint32_t a2, int stage, int rd, int mask, int alpha) {
    D d = DR(rd, mask);
    d.sat = 1;                                              // the stage result is clamped to [0, 1]
    S x = arg(a1, stage, alpha), y = arg(a2, stage, alpha), z = arg(a0, stage, alpha);
    S half = sw(C(1), RY), one = sw(C(1), RZ);
    switch (op) {
    case 2: op1(a, OP_MOV, d, x); return 0;                 // SELECTARG1
    case 3: op1(a, OP_MOV, d, y); return 0;                 // SELECTARG2
    case 4: op2(a, OP_MUL, d, x, y); return 0;              // MODULATE
    case 5: d.shift = 1; op2(a, OP_MUL, d, x, y); return 0; // MODULATE2X
    case 6: d.shift = 2; op2(a, OP_MUL, d, x, y); return 0; // MODULATE4X
    case 7: op2(a, OP_ADD, d, x, y); return 0;              // ADD
    case 8: op2(a, OP_ADD, d, x, md(y, y.mod ? y.mod : M_BIAS)); return y.mod ? 1 : 0;   // ADDSIGNED: x + y - 0.5
    case 9: d.shift = 1; op2(a, OP_ADD, d, x, md(y, M_BIAS)); return y.mod ? 1 : 0;      // ADDSIGNED2X
    case 10: op2(a, OP_SUB, d, x, y); return 0;             // SUBTRACT
    case 11: op3(a, OP_MAD, d, x, md(y, M_COMP), y); return y.mod ? 1 : 0;               // ADDSMOOTH: x + y - xy
    case 12: case 13: case 14: case 16: {                   // BLEND{DIFFUSE,TEXTURE,FACTOR,CURRENT}ALPHA
        S f = sw(op == 12 ? V(0) : op == 13 ? T(stage) : op == 14 ? C(0) : stage ? R(0) : V(0), RW);
        op3(a, OP_LRP, d, f, x, y);
        return 0;
    }
    case 15: op3(a, OP_MAD, d, y, md(sw(T(stage), RW), M_COMP), x); return 0;           // BLENDTEXTUREALPHAPM
    case 18: op3(a, OP_MAD, d, sw(x, RW), y, x); return 0;                               // MODULATEALPHA_ADDCOLOR
    case 19: op3(a, OP_MAD, d, x, y, sw(x, RW)); return 0;                               // MODULATECOLOR_ADDALPHA
    case 20: op3(a, OP_MAD, d, md(sw(x, RW), M_COMP), y, x); return 0;                   // MODULATEINVALPHA_ADDCOLOR
    case 21: op3(a, OP_MAD, d, md(x, M_COMP), y, sw(x, RW)); return 0;                   // MODULATEINVCOLOR_ADDALPHA
    case 24: op2(a, OP_DP3, DR(rd, 15), md(x, M_BX2), md(y, M_BX2)); return x.mod || y.mod; // DOTPRODUCT3 (all channels)
    case 25: op3(a, OP_MAD, d, x, y, z); return 0;          // MULTIPLYADD: a1 * a2 + a0
    case 26: op3(a, OP_LRP, d, z, x, y); return 0;          // LERP: a0 * a1 + (1 - a0) * a2
    }
    (void)half; (void)one;
    return 1;
}
uint32_t *ffp_ps(const FfpPs *k, uint32_t *n, const char **why) {
    Asm a = {0};
    put(&a, 0xffff0101u);                                   // ps_1_1
    // A PROJECTED stage carries D3DSI_TEXLD_PROJECT (bit 16) on its tex: the sample divides by the coordinate's w.
    for (int s = 0; s < k->n; s++) if (k->st[s].tex) { put(&a, OP_TEX | (k->st[s].proj ? 1u << 16 : 0)); dst(&a, d_(R_TEXTURE, s, MALL)); }
    // c1 = (0, 0.5, 1, 2), defined here.
    put(&a, OP_DEF); dst(&a, d_(R_CONST, 1, MALL));
    float kv[4] = {0, .5f, 1, 2};
    for (int i = 0; i < 4; i++) { uint32_t b; memcpy(&b, &kv[i], 4); put(&a, b); }
    op1(&a, OP_MOV, DR(0, MALL), V(0));                     // CURRENT starts as the diffuse color
    for (int s = 0; s < k->n; s++) {
        const FfpStage *t = &k->st[s];
        if (t->cop == 1) break;                             // DISABLE ends the cascade
        int rd = t->result == 5 ? 1 : 0;                    // RESULTARG: CURRENT or TEMP
        if (stage_op(&a, t->cop, t->ca0, t->ca1, t->ca2, s, rd, MXYZ, 0)) { *why = "a texture stage COLOROP"; free(a.t); return NULL; }
        uint32_t aop = t->aop == 1 ? 3 : t->aop;            // ALPHAOP DISABLE keeps the alpha (SELECTARG2 = CURRENT)
        uint32_t aa2 = t->aop == 1 ? 1 : t->aa2;
        if (stage_op(&a, aop, t->aa0, t->aa1, aa2, s, rd, MW, 1)) { *why = "a texture stage ALPHAOP"; free(a.t); return NULL; }
    }
    if (k->specular_add) { D d = DR(0, MXYZ); d.sat = 1; op2(&a, OP_ADD, d, R(0), V(1)); }
    put(&a, 0x0000ffffu);
    *n = a.n;
    return a.t;
}
