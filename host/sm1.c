// Shader model 1 bytecode (see sm1.h): decoder, disassembler, WGSL translation, interpreter.
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gpu.h"
#include "sm1.h"

// ---- decoder --------------------------------------------------------------------------------------
// Shader model 1 leaves the instruction length field at 0, so the operand count comes from the opcode.
static int nsrc_of(int op, int ps, int major_minor) {
    switch (op) {
    case OP_NOP: case OP_PHASE: return -1;      // no operands
    case OP_MOV: case OP_RCP: case OP_RSQ: case OP_EXP: case OP_LOG: case OP_LIT: case OP_FRC: case OP_EXPP:
    case OP_LOGP: return 1;
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DP3: case OP_DP4: case OP_MIN: case OP_MAX: case OP_SLT:
    case OP_SGE: case OP_DST: case OP_M4x4: case OP_M4x3: case OP_M3x4: case OP_M3x3: case OP_M3x2: return 2;
    case OP_MAD: case OP_LRP: case OP_CND: case OP_CMP: return 3;
    case OP_TEXCOORD: case OP_TEX: return ps && major_minor == 0x104 ? 1 : 0;
    case OP_TEXKILL: return 0;
    }
    return -2;
}

static int sm1_check(const Sm1 *s, char *err, int errn);
static Dst dst_of(uint32_t t) {
    return (Dst){(uint8_t)(((t >> 28) & 7) | ((t >> 8) & 0x18)), (uint8_t)((t >> 16) & 0xf), (uint8_t)((t >> 20) & 1),
                 (uint8_t)((t >> 24) & 0xf), (uint16_t)(t & 0x7ff)};
}
static Src src_of(uint32_t t) {
    Src s = {(uint8_t)(((t >> 28) & 7) | ((t >> 8) & 0x18)), (uint8_t)((t >> 24) & 0xf), {0}, (uint16_t)(t & 0x7ff),
             (uint8_t)((t >> 13) & 1)};
    for (int k = 0; k < 4; k++) s.swz[k] = (t >> (16 + 2 * k)) & 3;
    return s;
}

static int fail(char *err, int errn, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, (size_t)errn, fmt, ap);
    va_end(ap);
    return -1;
}

int sm1_decode(Sm1 *s, const uint32_t *tok, uint32_t n, char *err, int errn) {
    memset(s, 0, sizeof *s);
    s->ps = tok[0] >> 16 == 0xffff;
    s->major = (tok[0] >> 8) & 0xff;
    s->minor = tok[0] & 0xff;
    s->max_const = -1;
    int mm = s->major << 8 | s->minor;
    if (s->major != 1 || (s->ps ? s->minor < 1 || s->minor > 4 : s->minor != 1))
        return fail(err, errn, "%s_%d_%d is not shader model 1", s->ps ? "ps" : "vs", s->major, s->minor);
    s->ins = calloc(n, sizeof(Ins));
    for (uint32_t i = 1; i < n;) {
        uint32_t t = tok[i++];
        int op = t & 0xffff;
        if (op == 0xffff) break;
        if (op == 0xfffe) { i += (t >> 16) & 0x7fff; continue; }     // comment
        Ins *in = &s->ins[s->n];
        memset(in, 0, sizeof *in);
        in->op = (uint16_t)op;
        in->coissue = (t >> 30) & 1;
        in->ctl = (uint8_t)(t >> 16);
        if (op == OP_DCL) {
            if (i + 2 > n) return fail(err, errn, "dcl past the end");
            uint32_t u = tok[i++];
            in->usage = u & 0x1f;
            in->uidx = (u >> 16) & 0xf;
            in->d = dst_of(tok[i++]);
            if (in->d.type == R_INPUT && in->d.num < 16) {
                s->in_usage[in->d.num] = in->usage;
                s->in_index[in->d.num] = in->uidx;
                s->inputs |= 1u << in->d.num;
            }
            s->n++;
            continue;
        }
        if (op == OP_DEF) {
            if (i + 5 > n) return fail(err, errn, "def past the end");
            in->d = dst_of(tok[i++]);
            memcpy(in->def, &tok[i], 16);
            i += 4;
            if (in->d.num < 256) { s->defd[in->d.num] = 1; memcpy(s->defs[in->d.num], in->def, 16); }
            s->n++;
            continue;
        }
        int ns = nsrc_of(op, s->ps, mm);
        if (ns == -2) return fail(err, errn, "%s_%d_%d: opcode %d not known", s->ps ? "ps" : "vs", s->major, s->minor, op);
        if (ns >= 0) {
            if (i + 1 + (uint32_t)ns > n) return fail(err, errn, "instruction past the end");
            in->d = dst_of(tok[i++]);
            in->nsrc = (uint8_t)ns;
            for (int k = 0; k < ns; k++) {
                in->s[k] = src_of(tok[i++]);
                Src *r = &in->s[k];
                if (r->type == R_CONST) {
                    int top = r->rel ? 255 : r->num;
                    if (top > s->max_const) s->max_const = top;
                }
            }
            if (op == OP_TEX || op == OP_TEXCOORD) s->tex_used |= 1u << in->d.num;
            if (!s->ps) {
                if (in->d.type == R_RASTOUT) s->outputs |= 1u << in->d.num;
                if (in->d.type == R_ATTROUT) s->outputs |= 8u << in->d.num;
                if (in->d.type == R_TEXCRDOUT) s->outputs |= 32u << in->d.num;
            }
        }
        s->n++;
    }
    return sm1_check(s, err, errn);
}

void sm1_free(Sm1 *s) { free(s->ins); s->ins = NULL; }

// ---- disassembler ---------------------------------------------------------------------------------
static const char *opname(int op) {
    static const char *const names[] = {"nop", "mov", "add", "sub", "mad", "mul", "rcp", "rsq", "dp3", "dp4", "min",
        "max", "slt", "sge", "exp", "log", "lit", "dst", "lrp", "frc", "m4x4", "m4x3", "m3x4", "m3x3", "m3x2"};
    if (op <= OP_M3x2) return names[op];
    switch (op) {
    case OP_DCL: return "dcl"; case OP_TEXCOORD: return "texcoord"; case OP_TEXKILL: return "texkill";
    case OP_TEX: return "tex"; case OP_EXPP: return "expp"; case OP_LOGP: return "logp"; case OP_CND: return "cnd";
    case OP_DEF: return "def"; case OP_CMP: return "cmp"; case OP_PHASE: return "phase";
    }
    return "?";
}
static const char *regname(int ps, int type, int num, char *b) {
    switch (type) {
    case R_TEMP: sprintf(b, "r%d", num); break;
    case R_INPUT: sprintf(b, "v%d", num); break;
    case R_CONST: sprintf(b, "c%d", num); break;
    case 3: sprintf(b, ps ? "t%d" : "a%d", num); break;
    case R_RASTOUT: sprintf(b, "%s", num == 0 ? "oPos" : num == 1 ? "oFog" : "oPts"); break;
    case R_ATTROUT: sprintf(b, "oD%d", num); break;
    case R_TEXCRDOUT: sprintf(b, "oT%d", num); break;
    default: sprintf(b, "reg%d_%d", type, num);
    }
    return b;
}
void sm1_disasm(const Sm1 *s, void (*out)(const char *line)) {
    char line[256], r[32];
    snprintf(line, sizeof line, "%s_%d_%d", s->ps ? "ps" : "vs", s->major, s->minor);
    out(line);
    for (int i = 0; i < s->n; i++) {
        const Ins *in = &s->ins[i];
        int n = 0;
        n += snprintf(line + n, sizeof line - n, "%s%s", in->coissue ? "+" : "", opname(in->op));
        if (in->op == OP_DCL) n += snprintf(line + n, sizeof line - n, "_usage%d_%d", in->usage, in->uidx);
        if (in->d.sat) n += snprintf(line + n, sizeof line - n, "_sat");
        static const char *const sh[16] = {"", "_x2", "_x4", "_x8", "", "", "", "", "", "", "", "", "", "_d8", "_d4", "_d2"};
        n += snprintf(line + n, sizeof line - n, "%s", sh[in->d.shift]);
        if (in->op == OP_NOP || in->op == OP_PHASE) { out(line); continue; }
        n += snprintf(line + n, sizeof line - n, " %s", regname(s->ps, in->d.type, in->d.num, r));
        if (in->d.mask != 0xf) {
            n += snprintf(line + n, sizeof line - n, ".");
            for (int k = 0; k < 4; k++) if (in->d.mask >> k & 1) n += snprintf(line + n, sizeof line - n, "%c", "xyzw"[k]);
        }
        if (in->op == OP_DEF) {
            snprintf(line + n, sizeof line - n, ", %g, %g, %g, %g", in->def[0], in->def[1], in->def[2], in->def[3]);
            out(line);
            continue;
        }
        for (int k = 0; k < in->nsrc; k++) {
            const Src *a = &in->s[k];
            static const char *const pre[14] = {"", "-", "", "-", "", "-", "1-", "", "-", "", "", "", "-", "!"};
            static const char *const post[14] = {"", "", "_bias", "_bias", "_bx2", "_bx2", "", "_x2", "_x2", "_dz", "_dw", "_abs", "_abs", ""};
            n += snprintf(line + n, sizeof line - n, ", %s%s", a->mod < 14 ? pre[a->mod] : "?", regname(s->ps, a->type, a->num, r));
            if (a->rel) n += snprintf(line + n, sizeof line - n, "[a0.x]");
            n += snprintf(line + n, sizeof line - n, "%s", a->mod < 14 ? post[a->mod] : "?");
            if (a->swz[0] != 0 || a->swz[1] != 1 || a->swz[2] != 2 || a->swz[3] != 3) {
                n += snprintf(line + n, sizeof line - n, ".");
                for (int k2 = 0; k2 < 4; k2++) n += snprintf(line + n, sizeof line - n, "%c", "xyzw"[a->swz[k2]]);
            }
        }
        out(line);
    }
}

// ---- shared semantics -----------------------------------------------------------------------------
// ps_1_x arithmetic keeps values in [-PixelShader1xMaxValue, +PixelShader1xMaxValue]; our caps say 8.
#define PS1X_MAX 8.0f
static int mat_rows(int op, int *dot) {
    *dot = op == OP_M4x4 || op == OP_M4x3 ? 4 : 3;
    return op == OP_M4x4 || op == OP_M3x4 ? 4 : op == OP_M4x3 || op == OP_M3x3 ? 3 : op == OP_M3x2 ? 2 : 0;
}
static int implemented(int op) {
    switch (op) {
    case OP_NOP: case OP_MOV: case OP_ADD: case OP_SUB: case OP_MAD: case OP_MUL: case OP_RCP: case OP_RSQ:
    case OP_DP3: case OP_DP4: case OP_MIN: case OP_MAX: case OP_SLT: case OP_SGE: case OP_EXP: case OP_LOG:
    case OP_LIT: case OP_DST: case OP_LRP: case OP_FRC: case OP_M4x4: case OP_M4x3: case OP_M3x4: case OP_M3x3:
    case OP_M3x2: case OP_DCL: case OP_DEF: case OP_EXPP: case OP_LOGP: case OP_CND: case OP_CMP: return 1;
    }
    return 0;
}

// ---- WGSL -----------------------------------------------------------------------------------------
typedef struct { char *p; size_t n, cap; } Buf;
static void emit(Buf *b, const char *fmt, ...) {
    va_list ap;
    for (;;) {
        va_start(ap, fmt);
        int k = vsnprintf(b->p + b->n, b->cap - b->n, fmt, ap);
        va_end(ap);
        if (b->n + (size_t)k < b->cap) { b->n += (size_t)k; return; }
        b->cap = b->cap * 2 + (size_t)k + 256;
        b->p = realloc(b->p, b->cap);
    }
}
static const char *SW = "xyzw";
static void src_base(const Sm1 *s, const Src *a, int row, char *o, size_t on) {
    int n = a->num + row;
    switch (a->type) {
    case R_TEMP: snprintf(o, on, "r%d", n); break;
    case R_INPUT: snprintf(o, on, s->ps ? "v%d" : "i.v%d", n); break;
    case R_CONST:
        if (a->rel) snprintf(o, on, "%s[clamp(a0 + %d, 0, %d)]", s->ps ? "u.pc" : "u.c", n, s->ps ? 7 : GPU_NVC - 1);
        else if (n < 256 && s->defd[n]) {
            const float *d = s->defs[n];
            snprintf(o, on, "vec4f(%.9g, %.9g, %.9g, %.9g)", d[0], d[1], d[2], d[3]);
        } else snprintf(o, on, "%s[%d]", s->ps ? "u.pc" : "u.c", n);
        break;
    case 3: snprintf(o, on, s->ps ? "t%d" : "vec4f(f32(a0))", n); break;
    default: snprintf(o, on, "BAD");
    }
}
static void src_expr(const Sm1 *s, const Src *a, int row, char *o, size_t on) {
    char b[160], w[180];
    src_base(s, a, row, b, sizeof b);
    if (a->swz[0] != 0 || a->swz[1] != 1 || a->swz[2] != 2 || a->swz[3] != 3)
        snprintf(w, sizeof w, "(%s).%c%c%c%c", b, SW[a->swz[0]], SW[a->swz[1]], SW[a->swz[2]], SW[a->swz[3]]);
    else snprintf(w, sizeof w, "%s", b);
    switch (a->mod) {
    case 0: snprintf(o, on, "%s", w); break;
    case 1: snprintf(o, on, "(-%s)", w); break;
    case 2: snprintf(o, on, "(%s - 0.5)", w); break;
    case 3: snprintf(o, on, "(0.5 - %s)", w); break;
    case 4: snprintf(o, on, "(2.0 * %s - 1.0)", w); break;
    case 5: snprintf(o, on, "(1.0 - 2.0 * %s)", w); break;
    case 6: snprintf(o, on, "(1.0 - %s)", w); break;
    case 7: snprintf(o, on, "(2.0 * %s)", w); break;
    case 8: snprintf(o, on, "(-2.0 * %s)", w); break;
    case 11: snprintf(o, on, "abs(%s)", w); break;
    case 12: snprintf(o, on, "(-abs(%s))", w); break;
    default: snprintf(o, on, "BADMOD");
    }
}
static void dst_name(const Sm1 *s, const Dst *d, char *o, size_t on) {
    switch (d->type) {
    case R_TEMP: snprintf(o, on, "r%d", d->num); break;
    case 3: snprintf(o, on, s->ps ? "t%d" : "a0", d->num); break;
    case R_RASTOUT: snprintf(o, on, d->num == 0 ? "o.pos" : d->num == 1 ? "fogv" : "ptsv"); break;
    case R_ATTROUT: snprintf(o, on, "o.d%d", d->num); break;
    case R_TEXCRDOUT: snprintf(o, on, "o.t%d", d->num); break;
    default: snprintf(o, on, "BAD");
    }
}
// One instruction's result as a vec4f expression, before result modifiers.
static void op_expr(const Sm1 *s, const Ins *in, Buf *b) {
    char x[3][400];
    for (int k = 0; k < in->nsrc; k++) src_expr(s, &in->s[k], 0, x[k], sizeof x[k]);
    int dot, rows = mat_rows(in->op, &dot);
    if (rows) {
        emit(b, "vec4f(");
        for (int r = 0; r < 4; r++) {
            if (r >= rows) { emit(b, ", 0.0"); continue; }
            char row[400];
            src_expr(s, &in->s[1], r, row, sizeof row);
            if (dot == 4) emit(b, "%sdot(%s, %s)", r ? ", " : "", x[0], row);
            else emit(b, "%sdot((%s).xyz, (%s).xyz)", r ? ", " : "", x[0], row);
        }
        emit(b, ")");
        return;
    }
    switch (in->op) {
    case OP_MOV: emit(b, "%s", x[0]); break;
    case OP_ADD: emit(b, "(%s + %s)", x[0], x[1]); break;
    case OP_SUB: emit(b, "(%s - %s)", x[0], x[1]); break;
    case OP_MUL: emit(b, "(%s * %s)", x[0], x[1]); break;
    case OP_MAD: emit(b, "(%s * %s + %s)", x[0], x[1], x[2]); break;
    case OP_DP3: emit(b, "vec4f(dot((%s).xyz, (%s).xyz))", x[0], x[1]); break;
    case OP_DP4: emit(b, "vec4f(dot(%s, %s))", x[0], x[1]); break;
    case OP_MIN: emit(b, "min(%s, %s)", x[0], x[1]); break;
    case OP_MAX: emit(b, "max(%s, %s)", x[0], x[1]); break;
    case OP_SLT: emit(b, "select(vec4f(0.0), vec4f(1.0), %s < %s)", x[0], x[1]); break;
    case OP_SGE: emit(b, "select(vec4f(0.0), vec4f(1.0), %s >= %s)", x[0], x[1]); break;
    case OP_RCP: emit(b, "vec4f(d3d_rcp((%s).w))", x[0]); break;
    case OP_RSQ: emit(b, "vec4f(d3d_rsq((%s).w))", x[0]); break;
    case OP_EXP: emit(b, "vec4f(exp2((%s).w))", x[0]); break;
    case OP_LOG: emit(b, "vec4f(d3d_log((%s).w))", x[0]); break;
    case OP_EXPP: emit(b, "d3d_expp((%s).w)", x[0]); break;
    case OP_LOGP: emit(b, "d3d_logp((%s).w)", x[0]); break;
    case OP_LIT: emit(b, "d3d_lit(%s)", x[0]); break;
    case OP_DST: emit(b, "vec4f(1.0, (%s).y * (%s).y, (%s).z, (%s).w)", x[0], x[1], x[0], x[1]); break;
    case OP_LRP: emit(b, "mix(%s, %s, %s)", x[2], x[1], x[0]); break;
    case OP_FRC: emit(b, "fract(%s)", x[0]); break;
    case OP_CND: emit(b, "select(%s, %s, %s > vec4f(0.5))", x[2], x[1], x[0]); break;
    case OP_CMP: emit(b, "select(%s, %s, %s >= vec4f(0.0))", x[2], x[1], x[0]); break;
    }
}
static const char *const helpers =
    // A constant expression may not be infinite in WGSL: u.af.z is always 0, so this one is not constant.
    "fn d3d_inf() -> f32 { return bitcast<f32>(0x7f800000u | u.af.z); }\n"
    "fn d3d_rcp(x: f32) -> f32 { if (x == 1.0) { return 1.0; } if (x == 0.0) { return d3d_inf(); } return 1.0 / x; }\n"
    "fn d3d_rsq(x0: f32) -> f32 { let x = abs(x0); if (x == 1.0) { return 1.0; } if (x == 0.0) { return d3d_inf(); }"
    " return inverseSqrt(x); }\n"
    "fn d3d_log(x: f32) -> f32 { if (x == 0.0) { return -3.4028234e38; } return log2(abs(x)); }\n"
    "fn d3d_expp(w: f32) -> vec4f { let v = floor(w);"
    " return vec4f(exp2(v), w - v, bitcast<f32>(bitcast<u32>(exp2(w)) & 0xffffff00u), 1.0); }\n"
    "fn d3d_logp(w: f32) -> vec4f { let v = abs(w); if (v == 0.0) { return vec4f(-3.4028234e38, 1.0, -3.4028234e38, 1.0); }"
    " let b = bitcast<u32>(v); return vec4f(f32(i32(b >> 23u) - 127), bitcast<f32>((b & 0x7fffffu) | 0x3f800000u),"
    " bitcast<f32>(bitcast<u32>(log2(v)) & 0xffffff00u), 1.0); }\n"
    "fn d3d_lit(s: vec4f) -> vec4f { var d = vec4f(1.0, 0.0, 0.0, 1.0); let p = clamp(s.w, -127.9961, 127.9961);"
    " if (s.x > 0.0) { d.y = s.x; if (s.y > 0.0) { d.z = pow(s.y, p); } } return d; }\n"
    "fn d3d_cmp(f: u32, a: f32, r: f32) -> bool { switch f { case 1u: { return false; } case 2u: { return a < r; }"
    " case 3u: { return a == r; } case 4u: { return a <= r; } case 5u: { return a > r; } case 6u: { return a != r; }"
    " case 7u: { return a >= r; } default: { return true; } } }\n";

static void write_result(const Sm1 *s, const Ins *in, const char *val, Buf *b) {
    char d[32];
    dst_name(s, &in->d, d, sizeof d);
    if (in->d.type == 3 && !s->ps) { emit(b, "  a0 = i32(floor(%s.x + 0.5));\n", val); return; }   // round to nearest
    if (in->d.type == R_RASTOUT && in->d.num != 0) { emit(b, "  %s = %s.x;\n", d, val); return; }
    for (int k = 0; k < 4; k++) if (in->d.mask >> k & 1) emit(b, "  %s.%c = %s.%c;\n", d, SW[k], val, SW[k]);
}


char *sm1_wgsl(const Sm1 *s) {
    Buf b = {malloc(4096), 0, 4096};
    b.p[0] = 0;
    emit(&b, "struct U { c: array<vec4f, %d>, pc: array<vec4f, 8>, hp: vec4f, fogc: vec4f, af: vec4u, ar: vec4f };\n"
             "@group(0) @binding(0) var<uniform> u: U;\n%s", GPU_NVC, helpers);
    emit(&b, "struct VO { @builtin(position) pos: vec4f, @location(0) d0: vec4f, @location(1) d1: vec4f");
    for (int k = 0; k < 8; k++) emit(&b, ", @location(%d) t%d: vec4f", 2 + k, k);
    emit(&b, ", @location(10) fog: f32 };\n");
    if (!s->ps) {
        emit(&b, "struct VI { @builtin(vertex_index) vid: u32");
        for (int k = 0; k < 16; k++) if (s->inputs >> k & 1) emit(&b, ", @location(%d) v%d: vec4f", k, k);
        emit(&b, " };\n@vertex fn main(i: VI) -> VO {\n  var o: VO;\n  var a0: i32 = 0;\n  var fogv: f32 = 0.0;\n"
                 "  var ptsv: f32 = 0.0;\n");
        for (int k = 0; k < 12; k++) emit(&b, "  var r%d = vec4f(0.0);\n", k);
    } else {
        for (int k = 0; k < GPU_STAGES; k++)
            if (s->tex_used >> k & 1) emit(&b, "@group(1) @binding(%d) var s%d: sampler;\n@group(1) @binding(%d) var x%d: texture_2d<f32>;\n",
                                            2 * k, k, 2 * k + 1, k);
        emit(&b, "@fragment fn main(i: VO) -> @location(0) vec4f {\n  let v0 = i.d0;\n  let v1 = i.d1;\n");
        for (int k = 0; k < 2; k++) emit(&b, "  var r%d = vec4f(0.0);\n", k);
        for (int k = 0; k < 4; k++) emit(&b, "  var t%d = vec4f(0.0);\n", k);
    }
    // Co-issue: the pair reads all its sources before either one writes.
    const Ins *pend = NULL;
    char pend_val[64] = "";
    for (int i = 0; i < s->n; i++) {
        const Ins *in = &s->ins[i];
        if (in->op == OP_DCL || in->op == OP_DEF || in->op == OP_NOP) continue;
        if (in->op == OP_TEX) {
            int k = in->d.num;
            // ctl bit 0 = D3DSI_TEXLD_PROJECT (host/ffp.c sets it for a TTFF PROJECTED stage): divide by w.
            if (in->ctl & 1) emit(&b, "  t%d = textureSample(x%d, s%d, i.t%d.xy / i.t%d.w);\n", k, k, k, k, k);
            else emit(&b, "  t%d = textureSample(x%d, s%d, i.t%d.xy);\n", k, k, k, k);
            continue;
        }
        if (in->op == OP_TEXCOORD) { emit(&b, "  t%d = vec4f(clamp(i.t%d.xyz, vec3f(0.0), vec3f(1.0)), 1.0);\n", in->d.num, in->d.num); continue; }
        if (in->op == OP_TEXKILL) { emit(&b, "  if (any(t%d.xyz < vec3f(0.0))) { discard; }\n", in->d.num); continue; }
        emit(&b, "  let q%d = ", i);
        op_expr(s, in, &b);
        emit(&b, ";\n");
        char val[64];
        snprintf(val, sizeof val, "q%d", i);
        if (in->d.shift) {
            static const char *const f[16] = {"1.0", "2.0", "4.0", "8.0", "", "", "", "", "", "", "", "", "", "0.125", "0.25", "0.5"};
            emit(&b, "  let m%d = %s * %s;\n", i, val, f[in->d.shift]);
            snprintf(val, sizeof val, "m%d", i);
        }
        if (in->d.sat) { emit(&b, "  let n%d = clamp(%s, vec4f(0.0), vec4f(1.0));\n", i, val); snprintf(val, sizeof val, "n%d", i); }
        if (s->ps) { emit(&b, "  let p%d = clamp(%s, vec4f(-8.0), vec4f(8.0));\n", i, val); snprintf(val, sizeof val, "p%d", i); }
        if (i + 1 < s->n && s->ins[i + 1].coissue) { pend = in; snprintf(pend_val, sizeof pend_val, "%s", val); continue; }
        if (in->coissue && pend) { write_result(s, pend, pend_val, &b); pend = NULL; }
        write_result(s, in, val, &b);
    }
    if (!s->ps) {
        emit(&b, "  o.d0 = clamp(o.d0, vec4f(0.0), vec4f(1.0));\n  o.d1 = clamp(o.d1, vec4f(0.0), vec4f(1.0));\n"
                 "  o.fog = clamp(fogv, 0.0, 1.0);\n"
                 // D3D9 samples pixels at integer positions, WebGPU at half: move by half a pixel.
                 "  o.pos.x = o.pos.x + u.hp.x * o.pos.w;\n  o.pos.y = o.pos.y - u.hp.y * o.pos.w;\n  return o;\n}\n");
    } else {
        emit(&b, "  var c = r0;\n  if (u.af.y != 0u) { c = vec4f(mix(u.fogc.xyz, c.xyz, i.fog), c.w); }\n"
                 "  if (u.af.x != 0u && !d3d_cmp(u.af.x, clamp(c.w, 0.0, 1.0), u.ar.x)) { discard; }\n  return c;\n}\n");
    }
    return b.p;
}

// ---- interpreter ----------------------------------------------------------------------------------
typedef struct {
    const Sm1 *s;
    const float (*c)[4];
    float r[12][4], v[16][4], t[8][4];
    int a0;
    VsOut *o;
} St;

static void read_src(St *e, const Src *a, int row, float out[4]) {
    static const float zero[4] = {0, 0, 0, 0};
    const float *p = zero;
    float tmp[4];
    int n = a->num + row, nconst = e->s->ps ? 8 : 256;
    switch (a->type) {
    case R_TEMP: p = e->r[n]; break;
    case R_INPUT: p = e->v[n]; break;
    case R_CONST:
        if (a->rel) { int k = e->a0 + n; k = k < 0 ? 0 : k >= nconst ? nconst - 1 : k; p = e->c[k]; }
        else if (n < 256 && e->s->defd[n]) p = e->s->defs[n];
        else p = e->c[n];
        break;
    case 3:
        if (e->s->ps) p = e->t[n];
        else { tmp[0] = tmp[1] = tmp[2] = tmp[3] = (float)e->a0; p = tmp; }
        break;
    }
    float w[4];
    for (int k = 0; k < 4; k++) w[k] = p[a->swz[k]];
    for (int k = 0; k < 4; k++) {
        float x = w[k];
        switch (a->mod) {
        case 1: x = -x; break;
        case 2: x = x - 0.5f; break;
        case 3: x = 0.5f - x; break;
        case 4: x = 2.f * x - 1.f; break;
        case 5: x = 1.f - 2.f * x; break;
        case 6: x = 1.f - x; break;
        case 7: x = 2.f * x; break;
        case 8: x = -2.f * x; break;
        case 11: x = fabsf(x); break;
        case 12: x = -fabsf(x); break;
        }
        out[k] = x;
    }
}
static float bits_and(float f, uint32_t m) { uint32_t u; memcpy(&u, &f, 4); u &= m; memcpy(&f, &u, 4); return f; }
static float d3d_log(float x) { return x == 0.f ? -3.4028234e38f : log2f(fabsf(x)); }

static void exec_op(St *e, const Ins *in, float r[4]) {
    float x[3][4];
    for (int k = 0; k < in->nsrc; k++) read_src(e, &in->s[k], 0, x[k]);
    int dot, rows = mat_rows(in->op, &dot);
    if (rows) {
        for (int i = 0; i < 4; i++) {
            r[i] = 0;
            if (i >= rows) continue;
            float m[4];
            read_src(e, &in->s[1], i, m);
            for (int k = 0; k < dot; k++) r[i] += x[0][k] * m[k];
        }
        return;
    }
    float w = x[0][3], d;
    switch (in->op) {
    case OP_MOV: memcpy(r, x[0], 16); return;
    case OP_ADD: for (int k = 0; k < 4; k++) r[k] = x[0][k] + x[1][k]; return;
    case OP_SUB: for (int k = 0; k < 4; k++) r[k] = x[0][k] - x[1][k]; return;
    case OP_MUL: for (int k = 0; k < 4; k++) r[k] = x[0][k] * x[1][k]; return;
    case OP_MAD: for (int k = 0; k < 4; k++) r[k] = x[0][k] * x[1][k] + x[2][k]; return;
    case OP_DP3: d = x[0][0] * x[1][0] + x[0][1] * x[1][1] + x[0][2] * x[1][2]; break;
    case OP_DP4: d = x[0][0] * x[1][0] + x[0][1] * x[1][1] + x[0][2] * x[1][2] + x[0][3] * x[1][3]; break;
    case OP_MIN: for (int k = 0; k < 4; k++) r[k] = x[0][k] < x[1][k] ? x[0][k] : x[1][k]; return;
    case OP_MAX: for (int k = 0; k < 4; k++) r[k] = x[0][k] > x[1][k] ? x[0][k] : x[1][k]; return;
    case OP_SLT: for (int k = 0; k < 4; k++) r[k] = x[0][k] < x[1][k] ? 1.f : 0.f; return;
    case OP_SGE: for (int k = 0; k < 4; k++) r[k] = x[0][k] >= x[1][k] ? 1.f : 0.f; return;
    case OP_RCP: d = w == 1.f ? 1.f : w == 0.f ? INFINITY : 1.f / w; break;
    case OP_RSQ: w = fabsf(w); d = w == 1.f ? 1.f : w == 0.f ? INFINITY : 1.f / sqrtf(w); break;
    case OP_EXP: d = exp2f(w); break;
    case OP_LOG: d = d3d_log(w); break;
    case OP_EXPP: { float v = floorf(w); r[0] = exp2f(v); r[1] = w - v; r[2] = bits_and(exp2f(w), 0xffffff00u); r[3] = 1; return; }
    case OP_LOGP: {
        float v = fabsf(w);
        if (v == 0.f) { r[0] = r[2] = -3.4028234e38f; r[1] = r[3] = 1; return; }
        uint32_t u; memcpy(&u, &v, 4);
        r[0] = (float)((int)(u >> 23) - 127);
        u = (u & 0x7fffffu) | 0x3f800000u; memcpy(&r[1], &u, 4);
        r[2] = bits_and(log2f(v), 0xffffff00u); r[3] = 1;
        return;
    }
    case OP_LIT: {
        float p = x[0][3] < -127.9961f ? -127.9961f : x[0][3] > 127.9961f ? 127.9961f : x[0][3];
        r[0] = 1; r[1] = 0; r[2] = 0; r[3] = 1;
        if (x[0][0] > 0) { r[1] = x[0][0]; if (x[0][1] > 0) r[2] = powf(x[0][1], p); }
        return;
    }
    case OP_DST: r[0] = 1; r[1] = x[0][1] * x[1][1]; r[2] = x[0][2]; r[3] = x[1][3]; return;
    case OP_LRP: for (int k = 0; k < 4; k++) r[k] = x[2][k] + x[0][k] * (x[1][k] - x[2][k]); return;
    case OP_FRC: for (int k = 0; k < 4; k++) r[k] = x[0][k] - floorf(x[0][k]); return;
    case OP_CND: for (int k = 0; k < 4; k++) r[k] = x[0][k] > 0.5f ? x[1][k] : x[2][k]; return;
    case OP_CMP: for (int k = 0; k < 4; k++) r[k] = x[0][k] >= 0.f ? x[1][k] : x[2][k]; return;
    default: d = 0;
    }
    r[0] = r[1] = r[2] = r[3] = d;
}
static void modify(const Sm1 *s, const Ins *in, float r[4]) {
    static const float f[16] = {1, 2, 4, 8, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0.125f, 0.25f, 0.5f};
    for (int k = 0; k < 4; k++) {
        float x = r[k] * f[in->d.shift];
        if (in->d.sat) x = x < 0 ? 0 : x > 1 ? 1 : x;
        if (s->ps) x = x < -PS1X_MAX ? -PS1X_MAX : x > PS1X_MAX ? PS1X_MAX : x;
        r[k] = x;
    }
}
static void write_dst(St *e, const Ins *in, const float r[4]) {
    float *d = NULL;
    const Dst *t = &in->d;
    switch (t->type) {
    case R_TEMP: d = e->r[t->num]; break;
    case 3:
        if (!e->s->ps) { e->a0 = (int)floorf(r[0] + 0.5f); return; }
        d = e->t[t->num];
        break;
    case R_RASTOUT:
        if (t->num == 0) d = e->o->pos;
        else if (t->num == 1) { e->o->fog = r[0]; return; }
        else return;
        break;
    case R_ATTROUT: d = e->o->d[t->num & 1]; break;
    case R_TEXCRDOUT: d = e->o->t[t->num & 7]; break;
    default: return;
    }
    for (int k = 0; k < 4; k++) if (t->mask >> k & 1) d[k] = r[k];
}
// Runs the arithmetic; for ps, tex/texcoord/texkill go through the callbacks. Returns 0 on texkill.
static int exec(St *e, const VsOut *pin, Sm1Sample sample, void *ctx) {
    float pr[4];
    const Ins *pin_ins = NULL;
    for (int i = 0; i < e->s->n; i++) {
        const Ins *in = &e->s->ins[i];
        if (in->op == OP_DCL || in->op == OP_DEF || in->op == OP_NOP) continue;
        if (in->op == OP_TEX) {
            int k = in->d.num;
            const float *t = pin->t[k];
            float q = (in->ctl & 1) && t[3] != 0 ? 1 / t[3] : 1;    // D3DSI_TEXLD_PROJECT
            float uv[2] = {t[0] * q, t[1] * q};
            sample(ctx, k, uv, e->t[k]);
            continue;
        }
        if (in->op == OP_TEXCOORD) {
            for (int k = 0; k < 3; k++) { float x = pin->t[in->d.num][k]; e->t[in->d.num][k] = x < 0 ? 0 : x > 1 ? 1 : x; }
            e->t[in->d.num][3] = 1;
            continue;
        }
        if (in->op == OP_TEXKILL) {
            const float *t = e->t[in->d.num];
            if (t[0] < 0 || t[1] < 0 || t[2] < 0) return 0;
            continue;
        }
        float r[4];
        exec_op(e, in, r);
        modify(e->s, in, r);
        if (i + 1 < e->s->n && e->s->ins[i + 1].coissue) { memcpy(pr, r, 16); pin_ins = in; continue; }
        if (in->coissue && pin_ins) { write_dst(e, pin_ins, pr); pin_ins = NULL; }
        write_dst(e, in, r);
    }
    return 1;
}

void sm1_run_vs(const Sm1 *s, const float (*c)[4], const float in[16][4], VsOut *o) {
    St e;
    memset(&e, 0, sizeof e);
    memset(o, 0, sizeof *o);
    e.s = s; e.c = c; e.o = o;
    memcpy(e.v, in, sizeof e.v);
    exec(&e, NULL, NULL, NULL);
    for (int j = 0; j < 2; j++) for (int k = 0; k < 4; k++) { float x = o->d[j][k]; o->d[j][k] = x < 0 ? 0 : x > 1 ? 1 : x; }
    o->fog = o->fog < 0 ? 0 : o->fog > 1 ? 1 : o->fog;
}

int sm1_run_ps(const Sm1 *s, const float (*c)[4], const VsOut *in, Sm1Sample sample, void *ctx,
               float out[4]) {
    St e;
    memset(&e, 0, sizeof e);
    e.s = s; e.c = c;
    memcpy(e.v[0], in->d[0], 16);
    memcpy(e.v[1], in->d[1], 16);
    if (!exec(&e, in, sample, ctx)) return 0;
    memcpy(out, e.r[0], 16);
    return 1;
}

// Every instruction the translator and the interpreter know; anything else stops at CreateShader.
static int sm1_check(const Sm1 *s, char *err, int errn) {
    for (int i = 0; i < s->n; i++) {
        const Ins *in = &s->ins[i];
        if (in->op == OP_TEX || in->op == OP_TEXCOORD || in->op == OP_TEXKILL) {
            if (!s->ps || s->minor == 4) return fail(err, errn, "%s in %s_1_%d", opname(in->op), s->ps ? "ps" : "vs", s->minor);
            continue;
        }
        if (!implemented(in->op)) return fail(err, errn, "%s_1_%d: %s not implemented", s->ps ? "ps" : "vs", s->minor, opname(in->op));
        for (int k = 0; k < in->nsrc; k++)
            if (in->s[k].mod > 8 && in->s[k].mod != 11 && in->s[k].mod != 12) return fail(err, errn, "source modifier %d", in->s[k].mod);
    }
    return 0;
}
