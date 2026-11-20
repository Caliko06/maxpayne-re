// The contract between host/d3d8.c (game thread, records) and the renderers: web/gpu.js (WebGPU, page thread) and the
// CPU backend in gpu.c (native off-screen shots). Sized for what Max Payne's driver really does (census/d3d8_spec.tsv,
// rea/map/render.md): ~130 fixed-function draws and ~8,400 triangles a frame, so a packet keeps its arenas simple and
// copies what each draw uses; nothing stays resident on the GPU but textures and targets.
// A packet is one frame or a part of one. web/gpu.js mirrors every number here (GPU_ABI catches a mismatch at start).
#pragma once
#include <stddef.h>
#include <stdint.h>

#define GPU_ABI 3u
#define GPU_MAXID 65536     // ids are d3d8 object indexes + 1; render targets and textures share the id space
#define GPU_SLOTS 2         // packets alive at once: the one the game fills and the one the page reads
#define GPU_STAGES 4        // texture stages a draw can sample (the driver's fixed-function draws use 2)
#define GPU_QUEUED 2        // Presents the game may be ahead of the screen: the 2 back buffers of its CreateDevice. Its Present
                            // waits there, at interval 1, as D3D does when no back buffer is free.

// Command: [op | nwords << 8], then nwords words. The streams of a packet run in this order, one after the other.
enum {
    GPU_TARGET = 1,     // id, width, height, kind (0 color, 1 depth/stencil)
    GPU_CLEAR = 2,      // bound color id, bound depth id (0 none), D3DCLEAR flags, ARGB, z (float bits), stencil, x0, y0, x1, y1
    GPU_PRESENT = 3,    // color id, width, height
    GPU_DRAW = 4,       // D_* words
    GPU_SHADER = 5,     // id, 0 vs / 1 ps, token count n, WGSL byte count m, 16 inputs, tex mask, n tokens, WGSL text
                        // (m bytes, zero padded). Input k: 0 = v<k> not read, else 1 << 31 | usage << 8 | usage index.
    GPU_DECL = 6,       // id, element count n, n D3DVERTEXELEMENT9 (2 words each)
    GPU_TEXTURE = 7,    // id, width, height, levels
    GPU_TEXDATA = 8,    // id, level, width, height, byte offset in the blob arena. Texels: BGRA8, tightly packed.
    GPU_FREE = 9,       // id: a texture or target the game released
    GPU_GAMMA = 10,     // blob offset of 3 x 256 u16 (D3DGAMMARAMP); applied when a frame goes to the screen
    GPU_READ = 11,      // id, level, w, h, pointer low, pointer high: a whole target or texture level goes there as tight
                        // BGRA8 rows; then box.reads++
};

// GPU_DRAW words. Render states keep their D3D8 values.
enum {
    D_TOPO = 0,         // D3DPRIMITIVETYPE: 2, 3 lines, 4, 5 triangles; fans arrive as lists, points are not drawn
    D_NVERT, D_NIDX,    // vertices; indices (0 = draw the vertices in order)
    D_VOFF, D_IOFF, D_UOFF, // byte offsets in the vtx (one run per draw), idx (u32) and uni (GPU_USTEP aligned) arenas
    D_STRIDE, D_VS, D_PS, D_DECL, D_RT, D_DS,
    D_VP = 12,          // x, y, w, h, zmin, zmax (floats for z)
    D_ZEN = 18, D_ZWRITE, D_ZFUNC, D_CULL,
    D_BLEND = 22, D_SRCB, D_DSTB, D_BOP,
    D_CWRITE = 26,
    D_TEX = 27,         // GPU_STAGES x D_TEXN words
    D_N = D_TEX + 4 * 7,
};
// Per stage: texture id (0 none), ADDRESSU, ADDRESSV, MAGFILTER, MINFILTER, MIPFILTER, MAXANISOTROPY.
enum { T_ID = 0, T_AU, T_AV, T_MAG, T_MIN, T_MIP, T_ANISO, D_TEXN };
_Static_assert(D_TEX + GPU_STAGES * D_TEXN == D_N, "stage block");

// Uniform block of a draw, GPU_USTEP aligned in the uni arena (WebGPU's dynamic offset alignment). sm1.c writes the
// same layout into the WGSL. vc is the vertex shader's c registers (fixed function: host/ffp.h FC_*).
#define GPU_NVC 84
typedef struct {
    float vc[GPU_NVC][4];
    float pc[8][4];         // ps c0..c7 (fixed function: c0 = TEXTUREFACTOR)
    float hp[4];            // 1 / viewport width, 1 / viewport height: D3D pixels sit on integers, WebGPU's on half
    float fogc[4];          // RGB of FOGCOLOR
    uint32_t af[4];         // x alpha func (0 = test off), y fog on, z always 0 (see d3d_inf in sm1.c)
    float ar[4];            // x alpha reference
} GpuU;
#define GPU_USTEP 1536u
_Static_assert(sizeof(GpuU) == GPU_USTEP && GPU_USTEP % 256 == 0, "uniform block");

// The arenas of one packet. All four live in the shared heap; the page uploads vtx, idx and uni with one write each.
typedef struct {
    uint32_t cmd, ncmd;     // pointer, u32 words
    uint32_t vtx, nvtx;     // pointer, bytes (same for the rest)
    uint32_t idx, nidx;
    uint32_t uni, nuni;
    uint32_t blob, nblob;   // texels, gamma ramps
} GpuPacket;
_Static_assert(sizeof(GpuPacket) == 40, "packet");

// Shared with the page. seq and presents are written by the game, the rest by the page.
typedef struct {
    uint32_t abi;
    uint32_t seq, done;         // packets published; packets the page has encoded. Packet k lives in slot[k % GPU_SLOTS]
    uint32_t presents, shown;   // GPU_PRESENTs published; frames handed to the screen (or dropped)
    uint32_t reads;             // GPU_READs finished
    GpuPacket slot[GPU_SLOTS];
} GpuBox;
_Static_assert(offsetof(GpuBox, slot) == 24 && sizeof(GpuBox) == 24 + 40 * GPU_SLOTS, "mailbox");

// Game thread.
void gpu_cmd(int op, int n, const uint32_t *w);
uint32_t *gpu_draw(void);                       // room for D_N words, zeroed; the draw is in the list when it returns
uint8_t *gpu_vtx(uint32_t n, uint32_t *off);    // room for n vertex bytes in this packet, 4 aligned
uint32_t *gpu_idx(uint32_t n, uint32_t *off);   // room for n u32 indices
GpuU *gpu_uni(uint32_t *off);                   // one zeroed uniform block
uint8_t *gpu_blob(uint32_t n, uint32_t *off);   // room for n blob bytes, 4 aligned
int gpu_full(void);                             // an arena is past its soft limit: hand the packet off now
void gpu_submit(int present);                   // publish the packet; present: it holds a GPU_PRESENT
int gpu_screen_free(void);                      // the screen can take one more Present without gpu_wait blocking on vsync
double gpu_wait(void);                          // until the next packet can be written (call without the GIL); returns
                                                // the ms of it spent waiting for the screen to take a frame (vsync)
void gpu_read(uint32_t id, uint32_t level, uint32_t w, uint32_t h, void *dst);  // records a GPU_READ
void gpu_wait_read(void);                       // until every GPU_READ published so far is done (without the GIL)
void gpu_set_shot(const char *path);            // native: write every presented frame to this PNG
