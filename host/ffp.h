// The Direct3D 8 fixed-function pipeline as shader model 1 bytecode: a vs_1_1 for the transform, lighting, fog and
// texture coordinates of the current state, and a ps_1_1 for the texture stage cascade. Both backends (host/gpu.c,
// web/gpu.js) then draw fixed-function and shader draws the same way. Formulas: the DirectX 8.1 docs
// ("Mathematics of Lighting", "Texture Blending", "Vertex Fog").
#pragma once
#include <stdint.h>

// Vertex inputs, fixed registers: v0 position, v1 normal, v2 diffuse, v3 specular, v4+k texture coordinate set k.
// Usage and usage index per register must match the vertex declaration of the draw (host/d3d8.c builds both).
typedef struct {
    uint8_t pos;                // 1 XYZ (FLOAT3), 2 XYZRHW (pretransformed, FLOAT4), 3 XYZW
    uint8_t normal, diffuse, specular, ntex;
    uint8_t pos_usage;          // D3DDECLUSAGE of v0 (POSITION 0 or POSITIONT 9)
    // Lighting: 0 off; else bit k = light slot k is on (8 at most); kinds 1 point, 2 spot, 3 directional.
    uint8_t lights, kind[8];
    uint8_t localviewer, normalize, specular_on;
    uint8_t src_diffuse, src_ambient, src_specular, src_emissive;  // D3DMCS_*, already resolved against the vertex
    uint8_t fog;                // 0 none, 1 vertex fog LINEAR from view z, 2 from the specular alpha (pretransformed)
    uint32_t tci[8];            // D3DTSS_TEXCOORDINDEX of the stages a ps samples (low word set, high word texgen)
    uint8_t nstages;            // texture coordinates to write (oT0..oT{n-1})
    uint8_t tdim[8];            // components of texture coordinate set k in the vertex (1..4)
    // D3DTSS_TEXTURETRANSFORMFLAGS per stage: low bits COUNT (0 = no transform), 0x10 PROJECTED. The stages with a
    // count take the FC_TEXM matrices in stage order.
    uint8_t ttff[8];
} FfpVs;

// Constant registers the vs reads (host/d3d8.c fills them per draw).
enum { FC_WVP = 0, FC_WV = 4, FC_NRM = 8, FC_K = 11, FC_MDIF = 12, FC_MAMB, FC_MSPEC, FC_MEMI, FC_GAMB, FC_FOG,
       FC_PRE = 18, FC_PREZ = 19, FC_LIGHT = 20, FC_LIGHTN = 8, FC_LSTEP = 7,
       FC_TEXM = FC_LIGHT + FC_LSTEP * FC_LIGHTN, FC_TEXMN = 2, FC_N = FC_TEXM + 4 * FC_TEXMN };
// Texture transforms (FC_TEXM + 4 j): the columns of D3DTS_TEXTUREn of the j-th stage with a count (out = in x M).
// Per light (FC_LIGHT + FC_LSTEP i): +0 diffuse, +1 specular, +2 ambient, +3 position (view), +4 direction to the light
// (directional; view, unit) or the spot axis (view, unit), +5 attenuation (a0, a1, a2, range),
// +6 spot (cos theta/2, cos phi/2, falloff, 1 / (cos theta/2 - cos phi/2)).

// Texture stage state of one stage (D3DTSS_* values, D3D8).
typedef struct { uint32_t cop, ca1, ca2, ca0, aop, aa1, aa2, aa0, result; uint8_t tex, proj; } FfpStage;  // proj: TTFF PROJECTED
typedef struct { FfpStage st[8]; uint8_t n; uint8_t specular_add; } FfpPs;
// Pixel constant: c0 = D3DRS_TEXTUREFACTOR.

// Token streams (malloc'd, ending with 0x0000ffff); *n = token count. NULL with *why set: a state not drawn yet.
uint32_t *ffp_vs(const FfpVs *k, uint32_t *n, const char **why);
uint32_t *ffp_ps(const FfpPs *k, uint32_t *n, const char **why);
