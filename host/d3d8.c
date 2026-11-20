// d3d8: Direct3DCreate8 and the COM objects behind it, as documented by Microsoft (DirectX 8.1). Ported from the
// RE-NFSU d3d9 layer: the same command list (host/gpu.h) and backends. One adapter, an ATI RADEON 9800 PRO HAL device
// under the DirectX 8.1 runtime (vs_1_1, ps_1_4). Objects live in guest memory: [vtable][host index]; every vtable
// slot is a host function "d3d8.dll!Interface::Method", so a method we lack stops loudly with its name.
// Fixed-function draws become shader model 1 programs (host/ffp.c); Direct3D 8 vertex shaders get the dcl
// instructions their declaration implies, so both draw like d3d9 shader draws.
#include <math.h>
#include <strings.h>
#include "host.h"
#include "gpu.h"
#include "sm1.h"
#include "ffp.h"

#define D3D_OK 0u
#define D3DERR_NOTFOUND 0x88760866u
#define D3DERR_NOTAVAILABLE 0x8876086au
#define D3DERR_INVALIDCALL 0x8876086cu
#define D3DERR_OUTOFVIDEOMEMORY 0x8876017cu

#define TR(...) do { if (rn_trace()) rn_log("d3d8 " __VA_ARGS__); } while (0)

static __thread int render_thread;
static uint64_t last_present_us;

// ---- interfaces: vtable order of d3d8.h -----------------------------------------------------
enum { I_D3D8, I_DEVICE, I_SURFACE, I_TEXTURE, I_CUBE, I_VB, I_IB, I_SWAPCHAIN, I_VS, I_PS, I_SB, I_N };
#define UNK "QueryInterface", "AddRef", "Release"
#define RES UNK, "GetDevice", "SetPrivateData", "GetPrivateData", "FreePrivateData", "SetPriority", "GetPriority", \
            "PreLoad", "GetType"
#define BASETEX RES, "SetLOD", "GetLOD", "GetLevelCount"
static const char *const m_d3d8[] = {UNK, "RegisterSoftwareDevice", "GetAdapterCount", "GetAdapterIdentifier",
    "GetAdapterModeCount", "EnumAdapterModes", "GetAdapterDisplayMode", "CheckDeviceType", "CheckDeviceFormat",
    "CheckDeviceMultiSampleType", "CheckDepthStencilMatch", "GetDeviceCaps", "GetAdapterMonitor", "CreateDevice", 0};
static const char *const m_device[] = {UNK, "TestCooperativeLevel", "GetAvailableTextureMem",
    "ResourceManagerDiscardBytes", "GetDirect3D", "GetDeviceCaps", "GetDisplayMode", "GetCreationParameters",
    "SetCursorProperties", "SetCursorPosition", "ShowCursor", "CreateAdditionalSwapChain", "Reset", "Present",
    "GetBackBuffer", "GetRasterStatus", "SetGammaRamp", "GetGammaRamp", "CreateTexture", "CreateVolumeTexture",
    "CreateCubeTexture", "CreateVertexBuffer", "CreateIndexBuffer", "CreateRenderTarget", "CreateDepthStencilSurface",
    "CreateImageSurface", "CopyRects", "UpdateTexture", "GetFrontBuffer", "SetRenderTarget", "GetRenderTarget",
    "GetDepthStencilSurface", "BeginScene", "EndScene", "Clear", "SetTransform", "GetTransform", "MultiplyTransform",
    "SetViewport", "GetViewport", "SetMaterial", "GetMaterial", "SetLight", "GetLight", "LightEnable",
    "GetLightEnable", "SetClipPlane", "GetClipPlane", "SetRenderState", "GetRenderState", "BeginStateBlock",
    "EndStateBlock", "ApplyStateBlock", "CaptureStateBlock", "DeleteStateBlock", "CreateStateBlock", "SetClipStatus",
    "GetClipStatus", "GetTexture", "SetTexture", "GetTextureStageState", "SetTextureStageState", "ValidateDevice",
    "GetInfo", "SetPaletteEntries", "GetPaletteEntries", "SetCurrentTexturePalette", "GetCurrentTexturePalette",
    "DrawPrimitive", "DrawIndexedPrimitive", "DrawPrimitiveUP", "DrawIndexedPrimitiveUP", "ProcessVertices",
    "CreateVertexShader", "SetVertexShader", "GetVertexShader", "DeleteVertexShader", "SetVertexShaderConstant",
    "GetVertexShaderConstant", "GetVertexShaderDeclaration", "GetVertexShaderFunction", "SetStreamSource",
    "GetStreamSource", "SetIndices", "GetIndices", "CreatePixelShader", "SetPixelShader", "GetPixelShader",
    "DeletePixelShader", "SetPixelShaderConstant", "GetPixelShaderConstant", "GetPixelShaderFunction",
    "DrawRectPatch", "DrawTriPatch", "DeletePatch", 0};
static const char *const m_surface[] = {UNK, "GetDevice", "SetPrivateData", "GetPrivateData", "FreePrivateData",
    "GetContainer", "GetDesc", "LockRect", "UnlockRect", 0};
static const char *const m_texture[] = {BASETEX, "GetLevelDesc", "GetSurfaceLevel", "LockRect", "UnlockRect",
    "AddDirtyRect", 0};
static const char *const m_cube[] = {BASETEX, "GetLevelDesc", "GetCubeMapSurface", "LockRect", "UnlockRect",
    "AddDirtyRect", 0};
static const char *const m_vb[] = {RES, "Lock", "Unlock", "GetDesc", 0};
static const char *const m_swapchain[] = {UNK, "Present", "GetBackBuffer", 0};
static const char *const m_none[] = {0};
static const char *const iface_name[I_N] = {"IDirect3D8", "IDirect3DDevice8", "IDirect3DSurface8",
    "IDirect3DTexture8", "IDirect3DCubeTexture8", "IDirect3DVertexBuffer8", "IDirect3DIndexBuffer8",
    "IDirect3DSwapChain8", "VertexShader", "PixelShader", "StateBlock"};
// Shaders and state blocks are DWORD handles in Direct3D 8, not COM objects: they have no vtable.
static const char *const *const iface_methods[I_N] = {m_d3d8, m_device, m_surface, m_texture, m_cube, m_vb, m_vb,
    m_swapchain, m_none, m_none, m_none};
static uint32_t vtables[I_N];

static uint32_t vtable(int i) {
    if (vtables[i]) return vtables[i];
    int n = 0;
    while (iface_methods[i][n]) n++;
    uint32_t v = guest_alloc(4u * (n ? n : 1));
    for (int k = 0; k < n; k++) {
        char name[96];
        snprintf(name, sizeof name, "%s::%s", iface_name[i], iface_methods[i][k]);
        st(g_ctx, v + 4u * k, host_addr_of("d3d8.dll", name), 4);
    }
    return vtables[i] = v;
}

// ---- objects --------------------------------------------------------------------------------
enum { POOL_DEFAULT, POOL_MANAGED, POOL_SYSTEMMEM, POOL_SCRATCH };
typedef struct {
    int iface;
    uint32_t refs, guest;
    uint32_t w, h, fmt, usage, pool, type;  // type: D3DRESOURCETYPE
    uint32_t mem, pitch, size;              // guest memory behind Lock
    uint32_t container;                     // owning object index + 1 (surfaces of textures, back buffers)
    uint32_t gpu;                           // render target / depth id in the command list, 0 = none
    int locked, lockable, dead;
    int on_gpu, evicted;                    // level: mem released, the GPU holds its texels; texture: that was done
    uint32_t levels, level0, fvf;           // textures: mip count, object index of level 0 (levels follow)
    uint32_t *blob, nblob;                  // shader tokens as drawn (d3d8 vs: with the dcl added)
    uint32_t *orig, norig;                  // the tokens the game gave (GetVertexShaderFunction)
    uint32_t *vsd, nvsd;                    // vertex shaders: the D3DVSD declaration tokens
    uint32_t decl;                          // vertex shaders: their declaration object index + 1
    float (*cmem)[4]; uint32_t cbase, ncmem;    // vertex shaders: D3DVSD_CONST constants, loaded at SetVertexShader
    Sm1 *sm;
    struct Rec *rec;                        // state blocks: the recorded Set calls
    uint32_t nrec;
} Obj;
#define MAXOBJ GPU_MAXID
static Obj objs[MAXOBJ];
static int nobjs;                       // slots ever used; objs[0..nobjs) hold live and released objects
static Obj *last;                       // the object new_obj just made
// Released slots are reused once the table is full: a whole-game session creates far more than MAXOBJ textures and
// mip levels (the full-game run halted 'too many objects' entering part3_level2). The device's bindings hold a
// reference (bind), so a slot at refs 0 is unbound; the GPU backends free an id again on GPU_TEXTURE/GPU_TARGET.
static uint8_t freed[MAXOBJ];
static uint32_t dev_rt(void), dev_ds(void), live_objs(void);
static uint32_t run_at, run_left, rover;    // a reserved run of contiguous slots: a texture and its levels follow it

static int reserve(Ctx *c, uint32_t n) {
    if (nobjs + n <= MAXOBJ) { run_at = (uint32_t)nobjs; run_left = n; return 1; }
    for (uint32_t tried = 0, k = rover, len = 0; tried < 2 * MAXOBJ; tried++, k++) {
        if (k >= MAXOBJ) { k = 0; len = 0; }
        uint32_t k1 = k + 1;
        len = freed[k] && k1 != dev_rt() && k1 != dev_ds() ? len + 1 : 0;
        if (len == n) { run_at = k1 - n; run_left = n; rover = k1 % MAXOBJ; return 1; }
    }
    rn_halt(c, "d3d8: too many objects (%u live)", (unsigned)live_objs());
    return 0;
}
static uint32_t new_obj(Ctx *c, int iface) {
    if (!run_left && !reserve(c, 1)) return 0;
    uint32_t k = run_at++; run_left--;
    Obj *o = &objs[k];
    uint32_t old = k < (uint32_t)nobjs ? o->guest : 0;
    memset(o, 0, sizeof *o);
    o->iface = iface;
    o->refs = 1;
    o->guest = guest_alloc(16);         // a fresh block before the old one is freed: a stale pointer stays stale
    if (old) guest_free(old);
    st(c, o->guest, vtable(iface), 4);
    st(c, o->guest + 4, k + 1, 4);
    freed[k] = 0;
    if (k >= (uint32_t)nobjs) nobjs = (int)k + 1;
    last = o;
    return o->guest;
}
static Obj *obj_of(Ctx *c, uint32_t p, int iface) {
    uint32_t k = p ? ld(c, p + 4, 4) - 1 : ~0u;
    if (k < (uint32_t)nobjs && objs[k].guest == p && (iface < 0 || objs[k].iface == iface)) return &objs[k];
    rn_halt(c, "d3d8: %08x is not a live %s", p, iface < 0 ? "object" : iface_name[iface]);
    return NULL;
}
#define SELF(iface) Obj *o = obj_of(c, ARG(0), iface); if (!o) return D3DERR_INVALIDCALL
static uint32_t idx(const Obj *o) { return (uint32_t)(o - objs); }
static Obj *obj_k(uint32_t k1) { return k1 ? &objs[k1 - 1] : NULL; }

static uint32_t unk_QueryInterface(Ctx *c) {
    uint32_t g = ARG(1);
    rn_halt(c, "d3d8: QueryInterface for {%08x-%04x-%04x-...}", ld(c, g, 4), ld(c, g + 4, 2), ld(c, g + 6, 2));
    return 0x80004002u;     // E_NOINTERFACE
}
static uint32_t unk_AddRef(Ctx *c) { SELF(-1); if (o->container) return ++objs[o->container - 1].refs; return ++o->refs; }
// A surface that belongs to a texture or a swap chain shares its container's count (d3d8 docs).
static uint32_t release_obj(Ctx *c, Obj *o) {
    if (o->container) o = &objs[o->container - 1];
    if (!o->refs) { rn_halt(c, "d3d8: Release of a dead %s", iface_name[o->iface]); return 0; }
    if (!--o->refs) {
        TR("%s %08x freed", iface_name[o->iface], o->guest);
        guest_free(o->mem); o->mem = 0;
        if (o->iface == I_TEXTURE) for (uint32_t l = 0; l < o->levels; l++) {
            Obj *s = &objs[o->level0 + l];
            guest_free(s->mem); s->mem = 0; s->on_gpu = 0;
        }
        if (o->iface == I_TEXTURE && !(o->usage & 1)) { uint32_t id = idx(o) + 1; gpu_cmd(GPU_FREE, 1, &id); }  // not D3DUSAGE_RENDERTARGET
        if (o->iface == I_TEXTURE) for (uint32_t l = 0; l < o->levels; l++) freed[o->level0 + l] = 1;
        freed[idx(o)] = 1;
    }
    return o->refs;
}
static uint32_t unk_Release(Ctx *c) { SELF(-1); return release_obj(c, o); }
static uint32_t res_GetDevice(Ctx *c);

// ---- formats --------------------------------------------------------------------------------
enum {
    F_UNKNOWN = 0, F_R8G8B8 = 20, F_A8R8G8B8, F_X8R8G8B8, F_R5G6B5, F_X1R5G5B5, F_A1R5G5B5, F_A4R4G4B4, F_R3G3B2,
    F_A8, F_A8R3G3B2, F_X4R4G4B4, F_A8P8 = 40, F_P8, F_L8 = 50, F_A8L8, F_A4L4, F_V8U8 = 60, F_L6V5U5, F_X8L8V8U8,
    F_Q8W8V8U8, F_V16U16, F_W11V11U10, F_D16_LOCKABLE = 70, F_D32, F_D15S1 = 73, F_D24S8 = 75, F_D16 = 80,
    F_D24X8 = 77, F_D24X4S4 = 79, F_VERTEXDATA = 100, F_INDEX16, F_INDEX32,
};
#define FOURCC(a, b, c_, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c_) << 16 | (uint32_t)(d) << 24)
#define F_DXT1 FOURCC('D', 'X', 'T', '1')
#define F_DXT2 FOURCC('D', 'X', 'T', '2')
#define F_DXT3 FOURCC('D', 'X', 'T', '3')
#define F_DXT4 FOURCC('D', 'X', 'T', '4')
#define F_DXT5 FOURCC('D', 'X', 'T', '5')
static int is_dxt(uint32_t f) { return f == F_DXT1 || f == F_DXT2 || f == F_DXT3 || f == F_DXT4 || f == F_DXT5; }
static int fmt_bits(uint32_t f) {
    switch (f) {
    case F_A8R8G8B8: case F_X8R8G8B8: case F_X8L8V8U8: case F_Q8W8V8U8: case F_V16U16: case F_W11V11U10:
    case F_D32: case F_D24S8: case F_D24X8: case F_D24X4S4: case F_INDEX32: return 32;
    case F_R8G8B8: return 24;
    case F_R5G6B5: case F_X1R5G5B5: case F_A1R5G5B5: case F_A4R4G4B4: case F_A8R3G3B2: case F_X4R4G4B4:
    case F_A8P8: case F_A8L8: case F_V8U8: case F_L6V5U5: case F_D16_LOCKABLE: case F_D15S1: case F_D16:
    case F_INDEX16: return 16;
    case F_R3G3B2: case F_A8: case F_P8: case F_L8: case F_A4L4: return 8;
    case F_DXT1: return 4;
    case F_DXT2: case F_DXT3: case F_DXT4: case F_DXT5: return 8;
    }
    return 0;
}
static uint32_t pitch_of(uint32_t f, uint32_t w) {
    if (is_dxt(f)) return ((w + 3) / 4) * (f == F_DXT1 ? 8 : 16);
    return (w * fmt_bits(f) / 8 + 3) & ~3u;
}
static uint32_t size_of(uint32_t f, uint32_t w, uint32_t h) { return pitch_of(f, w) * (is_dxt(f) ? (h + 3) / 4 : h); }

// ---- the adapter ----------------------------------------------------------------------------
#define DESK_W rn_screen_w
#define DESK_H rn_screen_h
#define VRAM (128u << 20)
// MaxTextureWidth/Height. The R350 had 2048; the HD-modded install ships 4096x2048 maps, and P_Map::getClosestTextureSize
// (e2mfc 0x10120cd0, sizes up to 8192) clamps to this, so 2048 made the driver resample them down through D3DX (filter 4).
// RN_MAXTEX (phone profile): the game fits its textures itself to this cap, P_Map::getClosestTextureSize 0x10120cd0.
static uint32_t maxtex(void) { static uint32_t v; if (!v) { const char *s = getenv("RN_MAXTEX"); v = s && atoi(s) > 0 ? (uint32_t)atoi(s) : 4096u; } return v; }
#define MAXTEX maxtex()
static const uint16_t modes[][2] = {{640, 480}, {800, 600}, {1024, 768}, {1152, 864}, {1280, 960}, {1280, 1024},
    {1600, 1200}};
#define NSTATIC (int)(sizeof modes / sizeof modes[0])
static int has_desk(void) { for (int k = 0; k < NSTATIC; k++) if (modes[k][0] == DESK_W && modes[k][1] == DESK_H) return 1; return 0; }
// Modes per display format (X8R8G8B8 and R5G6B5), the desktop size last.
#define NMODES (NSTATIC + !has_desk())
static void mode_at(int k, uint32_t *w, uint32_t *h) { if (k < NSTATIC) { *w = modes[k][0]; *h = modes[k][1]; } else { *w = DESK_W; *h = DESK_H; } }
static int display_fmt(uint32_t f) { return f == F_X8R8G8B8 || f == F_R5G6B5; }
static uint32_t vram_used;

static uint32_t d3d8_obj;
static uint32_t Direct3DCreate8_(Ctx *c) {
    rn_log("d3d8 Direct3DCreate8(%u)", ARG(0));
    if (ARG(0) != 120) return 0;            // D3D_SDK_VERSION of DirectX 8.1
    if (!d3d8_obj) d3d8_obj = new_obj(c, I_D3D8);
    else objs[ld(c, d3d8_obj + 4, 4) - 1].refs++;
    return d3d8_obj;
}
static uint32_t IDirect3D8_GetAdapterCount(Ctx *c) { (void)c; TR("GetAdapterCount -> 1"); return 1; }
static void put_s(Ctx *c, uint32_t p, const char *s) { memcpy(G(p), s, strlen(s) + 1); }
// D3DADAPTER_IDENTIFIER8: Driver[512], Description[512], DriverVersion (LARGE_INTEGER), VendorId, DeviceId, SubSysId,
// Revision, DeviceIdentifier, WHQLLevel.
static uint32_t IDirect3D8_GetAdapterIdentifier(Ctx *c) {
    uint32_t ad = ARG(1), flags = ARG(2), p = ARG(3);
    TR("GetAdapterIdentifier(%u, flags %x)", ad, flags);
    if (ad != 0 || !p) return D3DERR_INVALIDCALL;
    memset(G(p), 0, 1068);
    put_s(c, p, "ati2dvag.dll");
    put_s(c, p + 512, "RADEON 9800 PRO");
    st(c, p + 1024, (10u << 16) | 6396, 4);   // 6.14.10.6396
    st(c, p + 1028, (6u << 16) | 14, 4);
    st(c, p + 1032, 0x1002, 4);               // ATI
    st(c, p + 1036, 0x4e48, 4);               // R350
    st(c, p + 1040, 0x00021002, 4);
    static const uint8_t guid[16] = {0xd7, 0xb7, 0x1e, 0xe2, 0x0d, 0x08, 0xcf, 0x11, 0x8e, 0x46, 0x00, 0x52, 0x4e, 0x48, 0x10, 0x02};
    memcpy(G(p + 1048), guid, 16);
    st(c, p + 1064, (flags & 2) ? 1 : 0, 4);  // D3DENUM_NO_WHQL_LEVEL clear: a signed driver
    return D3D_OK;
}
static void put_mode(Ctx *c, uint32_t p, uint32_t w, uint32_t h, uint32_t f) {
    st(c, p, w, 4); st(c, p + 4, h, 4); st(c, p + 8, 60, 4); st(c, p + 12, f, 4);
}
// Direct3D 8 enumerates every format's modes in one list: X8R8G8B8 then R5G6B5.
static uint32_t IDirect3D8_GetAdapterModeCount(Ctx *c) { TR("GetAdapterModeCount(%u)", ARG(1)); return ARG(1) == 0 ? 2 * NMODES : 0; }
static uint32_t IDirect3D8_EnumAdapterModes(Ctx *c) {
    uint32_t ad = ARG(1), k = ARG(2), p = ARG(3);
    if (ad != 0 || k >= 2u * NMODES || !p) return D3DERR_INVALIDCALL;
    uint32_t mw, mh;
    mode_at((int)(k % NMODES), &mw, &mh);
    put_mode(c, p, mw, mh, k < (uint32_t)NMODES ? F_X8R8G8B8 : F_R5G6B5);
    return D3D_OK;
}
static uint32_t IDirect3D8_GetAdapterDisplayMode(Ctx *c) {
    if (ARG(1) != 0 || !ARG(2)) return D3DERR_INVALIDCALL;
    put_mode(c, ARG(2), DESK_W, DESK_H, F_X8R8G8B8);
    return D3D_OK;
}
static int backbuffer_ok(uint32_t disp, uint32_t bb, int windowed) {
    if (bb == F_UNKNOWN) return windowed;
    if (disp == F_X8R8G8B8) return bb == F_X8R8G8B8 || bb == F_A8R8G8B8;
    if (disp == F_R5G6B5) return bb == F_R5G6B5;
    return 0;
}
static uint32_t IDirect3D8_CheckDeviceType(Ctx *c) {
    uint32_t ad = ARG(1), type = ARG(2), disp = ARG(3), bb = ARG(4), win = ARG(5);
    uint32_t r = ad != 0 ? D3DERR_INVALIDCALL : type == 1 && display_fmt(disp) && backbuffer_ok(disp, bb, win) ? D3D_OK : D3DERR_NOTAVAILABLE;
    TR("CheckDeviceType(%u, type %u, disp %u, bb %u, windowed %u) -> %08x", ad, type, disp, bb, win, r);
    return r;
}
static int tex_fmt(uint32_t f) {
    switch (f) {
    case F_A8R8G8B8: case F_X8R8G8B8: case F_R5G6B5: case F_X1R5G5B5: case F_A1R5G5B5: case F_A4R4G4B4:
    case F_A8: case F_L8: case F_A8L8: case F_V8U8: case F_L6V5U5: case F_X8L8V8U8: case F_Q8W8V8U8: case F_V16U16:
        return 1;
    }
    return is_dxt(f);
}
static int rt_fmt(uint32_t f) { return f == F_A8R8G8B8 || f == F_X8R8G8B8 || f == F_R5G6B5 || f == F_X1R5G5B5 || f == F_A1R5G5B5 || f == F_A4R4G4B4; }
static int ds_fmt(uint32_t f) { return f == F_D16 || f == F_D16_LOCKABLE || f == F_D24S8 || f == F_D24X8; }
#define U_RENDERTARGET 0x1u
#define U_DEPTHSTENCIL 0x2u
#define U_DYNAMIC 0x200u
static uint32_t check_format(uint32_t disp, uint32_t usage, uint32_t rtype, uint32_t f) {
    if (!display_fmt(disp)) return D3DERR_NOTAVAILABLE;
    if (rtype == 6) return f == F_VERTEXDATA ? D3D_OK : D3DERR_NOTAVAILABLE;
    if (rtype == 7) return f == F_INDEX16 || f == F_INDEX32 ? D3D_OK : D3DERR_NOTAVAILABLE;
    int ok;
    if (usage & U_DEPTHSTENCIL) ok = rtype == 1 && ds_fmt(f);
    else if (usage & U_RENDERTARGET) ok = (rtype == 1 || rtype == 3 || rtype == 5) && rt_fmt(f);
    else ok = rtype == 1 || rtype == 3 || rtype == 5 ? tex_fmt(f) : rtype == 4 ? tex_fmt(f) && !is_dxt(f) : 0;
    return ok ? D3D_OK : D3DERR_NOTAVAILABLE;
}
static uint32_t IDirect3D8_CheckDeviceFormat(Ctx *c) {
    uint32_t ad = ARG(1), type = ARG(2), disp = ARG(3), usage = ARG(4), rtype = ARG(5), f = ARG(6);
    uint32_t r = ad != 0 ? D3DERR_INVALIDCALL : type != 1 ? D3DERR_NOTAVAILABLE : check_format(disp, usage, rtype, f);
    TR("CheckDeviceFormat(%u, type %u, disp %u, usage %x, rtype %u, fmt %u) -> %08x", ad, type, disp, usage, rtype, f, r);
    return r;
}
static uint32_t IDirect3D8_CheckDeviceMultiSampleType(Ctx *c) {
    uint32_t ad = ARG(1), type = ARG(2), f = ARG(3), ms = ARG(5);
    int fmt_ok = rt_fmt(f) || ds_fmt(f);
    uint32_t r = ad != 0 ? D3DERR_INVALIDCALL : type == 1 && fmt_ok && (ms == 0 || ms == 2 || ms == 4 || ms == 6) ? D3D_OK : D3DERR_NOTAVAILABLE;
    TR("CheckDeviceMultiSampleType(%u, type %u, fmt %u, ms %u) -> %08x", ad, type, f, ms, r);
    return r;
}
static uint32_t IDirect3D8_CheckDepthStencilMatch(Ctx *c) {
    uint32_t ad = ARG(1), disp = ARG(3), rt = ARG(4), ds = ARG(5);
    return ad != 0 ? D3DERR_INVALIDCALL : display_fmt(disp) && rt_fmt(rt) && ds_fmt(ds) ? D3D_OK : D3DERR_NOTAVAILABLE;
}
// D3DCAPS8 (212 bytes; the same layout as the first 212 bytes of D3DCAPS9): an R350 under the DirectX 8.1 runtime.
static void put_caps(Ctx *c, uint32_t p) {
    static const struct { uint16_t off; uint32_t v; } caps[] = {
        {0, 1}, {4, 0},                        // DeviceType HAL, AdapterOrdinal
        {8, 0x00020000}, {12, 0x60020000}, {16, 0x000000a0}, {20, 0x8000000f}, {24, 0x3},
        {28, 0x001bbef0},                      // DevCaps: HW T&L, pure device, video memory textures
        {32, 0x00000ef2}, {36, 0x0f732191}, {40, 0xff}, {44, 0x1fff}, {48, 0x1fff}, {52, 0xff},
        {56, 0x00084208}, {60, 0x0000ec47}, {64, 0x07030700}, {68, 0x07030700}, {72, 0x03030300},
        {76, 0x3f}, {80, 0x3f}, {84, 0x1f},
        {88, 0}, {92, 0}, {96, 2048}, {100, 2048}, {104, 2048}, {108, 16},
        {136, 0x1ff}, {140, 0x00080008}, {144, 0x03feffff}, {148, 8}, {152, 8}, {156, 0x3b},
        {160, 8}, {164, 6}, {168, 4}, {172, 37}, {180, 0xfffff}, {184, 0xffffff}, {188, 16}, {192, 508},
        {196, 0xfffe0101}, {200, 256}, {204, 0xffff0104},
    };
    static const struct { uint16_t off; float v; } fcaps[] = {
        {112, 1e10f}, {116, -65536.f}, {120, -65536.f}, {124, 65536.f}, {128, 65536.f}, {132, 0.f},
        {176, 256.f}, {208, 8.f},
    };
    memset(G(p), 0, 212);
    for (unsigned k = 0; k < sizeof caps / sizeof caps[0]; k++) st(c, p + caps[k].off, caps[k].v, 4);
    st(c, p + 88, MAXTEX, 4); st(c, p + 92, MAXTEX, 4);
    for (unsigned k = 0; k < sizeof fcaps / sizeof fcaps[0]; k++) stf(c, p + fcaps[k].off, fcaps[k].v);
}
// HAL only: the reference rasterizer and software devices are not installed on a retail PC.
static uint32_t IDirect3D8_GetDeviceCaps(Ctx *c) {
    TR("GetDeviceCaps(%u, type %u)", ARG(1), ARG(2));
    if (ARG(1) != 0 || !ARG(3)) return D3DERR_INVALIDCALL;
    if (ARG(2) != 1) return D3DERR_NOTAVAILABLE;
    put_caps(c, ARG(3));
    return D3D_OK;
}
static uint32_t IDirect3D8_GetAdapterMonitor(Ctx *c) { return ARG(1) == 0 ? 0x10005 : 0; }

// ---- the device -----------------------------------------------------------------------------
// Render and texture stage state defaults of the d3d8 documentation.
#define F1 0x3f800000u
static const uint32_t rs_default[][2] = {
    {8, 3}, {9, 2}, {10, 0}, {14, 1}, {15, 0}, {16, 1}, {19, 2}, {20, 1}, {22, 3}, {23, 4}, {24, 0}, {25, 8},
    {26, 0}, {27, 0}, {28, 0}, {29, 0}, {34, 0}, {35, 0}, {36, 0}, {37, F1}, {38, F1}, {47, 0}, {48, 0}, {52, 0},
    {53, 1}, {54, 1}, {55, 1}, {56, 8}, {57, 0}, {58, 0xffffffff}, {59, 0xffffffff}, {60, 0xffffffff},
    {128, 0}, {136, 1}, {137, 1}, {139, 0}, {140, 0}, {141, 1}, {142, 1}, {143, 0}, {145, 1}, {146, 2}, {147, 0},
    {148, 0}, {151, 0}, {152, 0}, {154, F1}, {155, F1}, {156, 0}, {157, 0}, {158, F1}, {159, 0}, {160, 0},
    {161, 1}, {162, 0xffffffff}, {163, 0}, {165, 0}, {166, 0x42800000}, {167, 0}, {168, 0xf}, {170, 0}, {171, 1},
};
typedef struct { uint32_t x, y, w, h; float zmin, zmax; } Viewport;
static struct {
    uint32_t guest, flags, window, devtype;
    uint32_t pp[13];                    // D3DPRESENT_PARAMETERS (d3d8) as the game gave them, completed
    uint32_t backbuf, depth;            // object indexes + 1
    uint32_t rt, ds;
    uint32_t rs[256], tss[8][32];
    float xf[512][16];
    Viewport vp;
    int in_scene;
    uint32_t frames;
    uint32_t vs, ps, ib, base_vertex, tex[8];   // vs: an FVF (even) or a shader handle (odd); others object index + 1
    struct { uint32_t vb, stride; } stream[16];
    float vsf[96][4], psf[8][4];
    uint32_t material[17];
    // D3D8 lights are keyed by any DWORD: the E2 driver passes each light's own id (light + 0x110, read in
    // P_D3DVirtualObject::preparePointLight 0x10423ec0, disabled per object in 0x10416a60), so ids grow with the level.
    struct { uint32_t idx, set, on, v[26]; } light[1024];     // open-addressed by id
    uint16_t lon[1024]; uint32_t nlon;                         // enabled slots, in enable order
    float clip[6][4];
    int cursor_shown;
    uint16_t gamma[3][256];
    int recording;
    struct Rec *rec;
    uint32_t nrec, caprec;
} dev;
static uint32_t dev_rt(void) { return dev.rt; }
static uint32_t dev_ds(void) { return dev.ds; }
static uint32_t live_objs(void) { uint32_t n = 0; for (int k = 0; k < nobjs; k++) n += !freed[k]; return n; }

// A recorded Set call (BeginStateBlock): handler, arguments, a host copy of what its pointer argument pointed to.
typedef struct Rec { HostFn fn; uint32_t n, a[6]; int pidx; uint32_t bytes; } Rec;
static uint32_t record(Ctx *c, HostFn fn, uint32_t n, int pidx, uint32_t bytes) {
    if (dev.nrec == dev.caprec) dev.rec = realloc(dev.rec, sizeof(Rec) * (dev.caprec = dev.caprec ? 2 * dev.caprec : 64));
    Rec *r = &dev.rec[dev.nrec++];
    r->fn = fn; r->n = n; r->pidx = pidx; r->bytes = bytes;
    for (uint32_t k = 0; k < n; k++) r->a[k] = ARG(k);
    if (pidx >= 0) {
        if (!r->a[pidx]) return D3DERR_INVALIDCALL;
        uint32_t d = guest_alloc(bytes ? bytes : 4);
        memcpy(G(d), G(r->a[pidx]), bytes);
        r->a[pidx] = d;
    }
    return D3D_OK;
}
#define REC(fn, n, pidx, bytes) do { if (dev.recording) return record(c, fn, n, pidx, bytes); } while (0)

static uint32_t new_surface(Ctx *c, uint32_t w, uint32_t h, uint32_t f, uint32_t usage, uint32_t pool, int lockable) {
    uint32_t g = new_obj(c, I_SURFACE);
    if (!g) return 0;
    Obj *o = last;
    o->w = w; o->h = h; o->fmt = f; o->usage = usage; o->pool = pool; o->type = 1; o->lockable = lockable;
    o->pitch = pitch_of(f, w);
    o->size = size_of(f, w, h);
    if (usage & (U_RENDERTARGET | U_DEPTHSTENCIL)) {
        o->gpu = idx(o) + 1;
        uint32_t t[4] = {o->gpu, w, h, (usage & U_DEPTHSTENCIL) ? 1u : 0u};
        gpu_cmd(GPU_TARGET, 4, t);
        if (pool == POOL_DEFAULT) vram_used += o->size;
    }
    return g;
}
static void reset_viewport(Obj *rt) { dev.vp = (Viewport){0, 0, rt->w, rt->h, 0.f, 1.f}; }
static void default_state(void) {
    memset(dev.rs, 0, sizeof dev.rs);
    for (unsigned k = 0; k < sizeof rs_default / sizeof rs_default[0]; k++) dev.rs[rs_default[k][0]] = rs_default[k][1];
    dev.rs[7] = dev.pp[8] ? 1 : 0;                  // ZENABLE: on with an automatic depth buffer
    for (int s = 0; s < 8; s++) {
        uint32_t *t = dev.tss[s];
        memset(t, 0, sizeof dev.tss[s]);
        t[1] = s ? 1 : 4; t[2] = 2; t[3] = 1; t[4] = s ? 1 : 2; t[5] = 2; t[6] = 1; t[11] = (uint32_t)s;
        t[13] = t[14] = t[25] = 1;                  // ADDRESSU/V/W: WRAP
        t[16] = t[17] = 1; t[18] = 0; t[21] = 1;    // MAG/MIN POINT, MIP NONE, MAXANISOTROPY 1
        t[26] = 1; t[27] = 1; t[28] = 1;            // COLORARG0, ALPHAARG0, RESULTARG: CURRENT
    }
    for (int m = 0; m < 512; m++) {
        memset(dev.xf[m], 0, sizeof dev.xf[m]);
        dev.xf[m][0] = dev.xf[m][5] = dev.xf[m][10] = dev.xf[m][15] = 1.f;
    }
}
// D3DPRESENT_PARAMETERS (d3d8): BackBufferWidth, Height, Format, Count, MultiSampleType, SwapEffect, hDeviceWindow,
// Windowed, EnableAutoDepthStencil, AutoDepthStencilFormat, Flags, FullScreen_RefreshRateInHz,
// FullScreen_PresentationInterval. Checked, completed and written back like the runtime does.
static uint32_t take_pp(Ctx *c, uint32_t pp, uint32_t window) {
    uint32_t *p = dev.pp;
    int windowed = p[7] != 0;
    if (!p[0] || !p[1]) {
        if (!windowed) return D3DERR_INVALIDCALL;
        // Windowed with 0 x 0: the desktop-sized client area of the device window.
        p[0] = (uint32_t)DESK_W; p[1] = (uint32_t)DESK_H;
        rn_log("d3d8: windowed device without a size: %ux%u from window %08x", p[0], p[1], window);
    }
    if (!windowed) {
        int found = 0;
        for (int k = 0; k < NMODES; k++) { uint32_t mw, mh; mode_at(k, &mw, &mh); found |= mw == p[0] && mh == p[1]; }
        if (!found || (!display_fmt(p[2]) && p[2] != F_A8R8G8B8)) return D3DERR_INVALIDCALL;
    }
    if (p[2] == F_UNKNOWN) p[2] = F_X8R8G8B8;
    if (!backbuffer_ok(p[2] == F_A8R8G8B8 || p[2] == F_X8R8G8B8 ? F_X8R8G8B8 : p[2], p[2], windowed)) return D3DERR_INVALIDCALL;
    if (p[3] == 0) p[3] = 1;
    if (p[3] > 3 || p[4] || (p[8] && !ds_fmt(p[9]))) {
        rn_halt(c, "d3d8: CreateDevice with %u back buffers, multisample %u, depth fmt %u", p[3], p[4], p[9]);
        return D3DERR_INVALIDCALL;
    }
    for (int k = 0; k < 13; k++) st(c, pp + 4 * k, p[k], 4);
    return D3D_OK;
}
void rn_ws_size(uint32_t w, uint32_t h);     // host/widescreen.c
static void swap_chain(Ctx *c) {
    uint32_t *p = dev.pp;
    rn_ws_size(p[0], p[1]);
    uint32_t bb = new_surface(c, p[0], p[1], p[2], U_RENDERTARGET, POOL_DEFAULT, (p[10] & 1) != 0);  // LOCKABLE_BACKBUFFER
    Obj *b = obj_of(c, bb, I_SURFACE);
    dev.backbuf = dev.rt = idx(b) + 1;
    dev.depth = dev.ds = 0;
    if (p[8]) {
        Obj *d = obj_of(c, new_surface(c, p[0], p[1], p[9], U_DEPTHSTENCIL, POOL_DEFAULT, p[9] == F_D16_LOCKABLE), I_SURFACE);
        dev.depth = dev.ds = idx(d) + 1;
    }
    default_state();
    reset_viewport(b);
}
static uint32_t IDirect3D8_CreateDevice(Ctx *c) {
    uint32_t ad = ARG(1), type = ARG(2), focus = ARG(3), flags = ARG(4), pp = ARG(5), out = ARG(6);
    if (ad != 0 || !out || !pp) return D3DERR_INVALIDCALL;
    for (int k = 0; k < 13; k++) dev.pp[k] = ld(c, pp + 4 * k, 4);
    uint32_t *p = dev.pp;
    rn_log("d3d8 CreateDevice(%u, type %u, focus %08x, flags %x): %ux%u fmt %u x%u ms %u swap %u hwnd %08x windowed %u "
           "autodepth %u fmt %u flags %x %u Hz interval %x",
           ad, type, focus, flags, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12]);
    if (type != 1) return D3DERR_NOTAVAILABLE;
    if (dev.guest) { rn_halt(c, "d3d8: a second device"); return D3DERR_INVALIDCALL; }
    uint32_t window = p[6] ? p[6] : focus;
    uint32_t r = take_pp(c, pp, window);
    if (r != D3D_OK) return r;
    // Without D3DCREATE_FPU_PRESERVE the runtime sets the calling thread's FPU to single precision, round to nearest.
    if (!(flags & 2)) c->fcw &= ~0xf00u;
    dev.flags = flags; dev.window = window; dev.devtype = type;
    rn_telem.guest_mem = (uint32_t)(uintptr_t)c->mem;     // web/telemetry.js reads game state from here
    dev.guest = new_obj(c, I_DEVICE);
    objs[ld(c, d3d8_obj + 4, 4) - 1].refs++;
    swap_chain(c);
    render_thread = 1;
    st(c, out, dev.guest, 4);
    return D3D_OK;
}

#define DEV() do { if (ARG(0) != dev.guest) { rn_halt(c, "d3d8: %08x is not the device", ARG(0)); return D3DERR_INVALIDCALL; } } while (0)

static uint32_t IDirect3DDevice8_TestCooperativeLevel(Ctx *c) { DEV(); return D3D_OK; }
static uint32_t IDirect3DDevice8_GetAvailableTextureMem(Ctx *c) {
    DEV();
    return (VRAM - vram_used + (1u << 19)) & ~((1u << 20) - 1);     // rounded to the nearest MB
}
static uint32_t IDirect3DDevice8_ResourceManagerDiscardBytes(Ctx *c) { DEV(); return D3D_OK; }
static uint32_t IDirect3DDevice8_GetDirect3D(Ctx *c) {
    DEV();
    objs[ld(c, d3d8_obj + 4, 4) - 1].refs++;
    st(c, ARG(1), d3d8_obj, 4);
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_GetDeviceCaps(Ctx *c) { DEV(); put_caps(c, ARG(1)); return D3D_OK; }
static uint32_t IDirect3DDevice8_GetDisplayMode(Ctx *c) {
    DEV();
    if (dev.pp[7]) put_mode(c, ARG(1), DESK_W, DESK_H, F_X8R8G8B8);
    else put_mode(c, ARG(1), dev.pp[0], dev.pp[1], dev.pp[2] == F_A8R8G8B8 ? F_X8R8G8B8 : dev.pp[2]);
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_GetCreationParameters(Ctx *c) {
    DEV();
    uint32_t p = ARG(1);
    st(c, p, 0, 4); st(c, p + 4, dev.devtype, 4); st(c, p + 8, dev.window, 4); st(c, p + 12, dev.flags, 4);
    return D3D_OK;
}
static uint32_t ref_out(Ctx *c, uint32_t out, uint32_t k1) {
    Obj *o = obj_k(k1);
    if (!o) { st(c, out, 0, 4); return D3DERR_NOTFOUND; }
    if (o->container) objs[o->container - 1].refs++; else o->refs++;
    st(c, out, o->guest, 4);
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_GetBackBuffer(Ctx *c) {
    DEV();
    if (ARG(1) >= dev.pp[3] || !ARG(3)) return D3DERR_INVALIDCALL;
    if (ARG(1) != 0) { rn_halt(c, "d3d8: GetBackBuffer(%u)", ARG(1)); return D3DERR_INVALIDCALL; }
    return ref_out(c, ARG(3), dev.backbuf);
}
static uint32_t IDirect3DDevice8_GetRenderTarget(Ctx *c) { DEV(); return ref_out(c, ARG(1), dev.rt); }
static uint32_t IDirect3DDevice8_GetDepthStencilSurface(Ctx *c) { DEV(); return ref_out(c, ARG(1), dev.ds); }
static uint32_t IDirect3DDevice8_BeginScene(Ctx *c) { DEV(); if (dev.in_scene) return D3DERR_INVALIDCALL; dev.in_scene = 1; return D3D_OK; }
static uint32_t IDirect3DDevice8_EndScene(Ctx *c) { DEV(); if (!dev.in_scene) return D3DERR_INVALIDCALL; dev.in_scene = 0; return D3D_OK; }

// Clear(Count, pRects, Flags, Color, Z, Stencil): each rectangle, clipped to the viewport.
static uint32_t IDirect3DDevice8_Clear(Ctx *c) {
    DEV();
    uint32_t n = ARG(1), rects = ARG(2), flags = ARG(3), color = ARG(4), z = ARG(5), stencil = ARG(6);
    Obj *rt = obj_k(dev.rt), *ds = obj_k(dev.ds);
    TR("Clear(%u rects, flags %x, color %08x, stencil %u)", n, flags, color, stencil);
    if ((n && !rects) || (flags & ~7u) || !flags) return D3DERR_INVALIDCALL;
    if ((flags & 6) && !ds) return D3DERR_INVALIDCALL;
    int x0 = (int)dev.vp.x, y0 = (int)dev.vp.y, x1 = x0 + (int)dev.vp.w, y1 = y0 + (int)dev.vp.h;
    for (uint32_t k = 0; k < (n ? n : 1); k++) {
        int a0 = x0, b0 = y0, a1 = x1, b1 = y1;
        if (n) {
            int32_t r[4];
            for (int i = 0; i < 4; i++) r[i] = (int32_t)ld(c, rects + 16 * k + 4 * i, 4);
            if (r[0] > a0) a0 = r[0];
            if (r[1] > b0) b0 = r[1];
            if (r[2] < a1) a1 = r[2];
            if (r[3] < b1) b1 = r[3];
        }
        if (a0 >= a1 || b0 >= b1) continue;
        uint32_t w[10] = {rt->gpu, ds ? ds->gpu : 0, flags, color, z, stencil,
                          (uint32_t)a0, (uint32_t)b0, (uint32_t)a1, (uint32_t)b1};
        gpu_cmd(GPU_CLEAR, 10, w);
    }
    return D3D_OK;
}
// The packet goes to the page; the wait for it is without the GIL, so the game's other threads run.
static void hand_off(int present) {
    gpu_submit(present); gil_release();
    uint64_t t0 = now_us();
    double vsync = gpu_wait(), all = (double)(now_us() - t0) / 1000;
    gil_acquire();
    rn_telem.gpu_wait_ms += all - vsync;
    rn_telem.vsync_ms += vsync;
}
static void flush_big(void) { if (gpu_full()) hand_off(0); }
// BRIDGE: level init (0x44fda0) uploads whole texture batches between guest Presents.
// Present the loading backbuffer between chunks, without reentering guest rendering or settings.
void d3d_load_checkpoint(void) {
#ifdef __EMSCRIPTEN__
    if (!render_thread || !dev.backbuf || !last_present_us) return;
    uint64_t now = now_us();
    if (now - last_present_us < 100000) return;
    Ctx *c = g_ctx;
    uint32_t ms = ld(c, 0x8a6400, 4);                 /* 0x40b3a2: theApp's mode switch */
    if (ms) {
        uint32_t cur = ld(c, ms + 0x19, 4);           /* 0x724b90: current mode */
        uint32_t gm = ld(c, ms + 0x80, 4);            /* 0x415dc0: game slot */
        if (cur && cur != ld(c, ms + 0x7c, 4)) {     /* 0x415dc0: menu slot */
            if (!gm || cur != gm) return;
            if (ld(c, gm + 0xd4, 4) &&               /* 0x44fda0: level name length */
                ld(c, gm + 0x1074, 4) &&             /* 0x44fda0: level ready */
                ld(c, gm + 0x1080, 4) &&             /* 0x451e50: player wrapper */
                ld(c, ms + 0x0d, 1) != 3) return;    /* 0x601250: loading fade */
        }
    }
    // A loading frame is only worth showing when the screen has room for it: waiting a vsync for one cost 18.9 of
    // 96.6 s of load in the 21-map chain (out/n50_chain). Skipped here, the next checkpoint tries again.
    if (!gpu_screen_free()) return;
    Obj *b = obj_k(dev.backbuf);
    uint32_t w[3] = {b->gpu, b->w, b->h};
    gpu_cmd(GPU_PRESENT, 3, w);
    rn_telem.presents++;
    last_present_us = now;
    hand_off(1);
#endif
}
// Reads a color target or a texture level through the page; everything recorded so far is in it.
static void read_gpu(uint32_t id, uint32_t level, uint32_t w, uint32_t h, void *dst) {
    gpu_read(id, level, w, h, dst);
    hand_off(0);
    gil_release();
    uint64_t t0 = now_us();                 // after hand_off (gpu_wait_ms), before the GIL (gil_main_ms): no time counted twice
    gpu_wait_read();
    uint64_t dt = now_us() - t0;
    gil_acquire();
    rn_telem.gpu_read_ms += (double)dt / 1000;
    rn_telem.gpu_reads++;
}
// Weak: d3d8.c builds with or without host/gamestate.c.
__attribute__((weak)) void gamestate_frame(Ctx *c);
void settings_frame(Ctx *c);
static uint32_t IDirect3DDevice8_Present(Ctx *c) {
    DEV();
    if (ARG(1) || ARG(2) || ARG(3) || ARG(4)) {
        rn_halt(c, "d3d8: Present with rects/window (%08x %08x %08x %08x)", ARG(1), ARG(2), ARG(3), ARG(4));
        return D3DERR_INVALIDCALL;
    }
    Obj *b = obj_k(dev.backbuf);
    uint32_t w[3] = {b->gpu, b->w, b->h};
    gpu_cmd(GPU_PRESENT, 3, w);
    dev.frames++;
    rn_telem.presents++;
    settings_frame(c);                         // host/win32.c: the game's settings save while a menu is up (a page never quits)
    if (gamestate_frame) gamestate_frame(c);   // host/gamestate.c (ghost slice): player/enemy/mode snapshot, once per frame
    if (dev.frames == 1 || dev.frames % 100 == 0) rn_log("d3d8 Present %u: %ux%u at %.1f s", dev.frames, b->w, b->h, (double)now_us() / 1e6);
    static int stack_at = -2;               // RN_STACK_AT=<frame>: where the game presents from at that frame
    if (stack_at == -2) { const char *e = getenv("RN_STACK_AT"); stack_at = e ? atoi(e) : -1; }
    if ((int)dev.frames == stack_at) rn_backtrace(c);
    last_present_us = now_us();
    hand_off(1);
    return D3D_OK;
}

// ---- state ----------------------------------------------------------------------------------
static uint32_t IDirect3DDevice8_SetRenderState(Ctx *c) {
    DEV(); REC(IDirect3DDevice8_SetRenderState, 3, -1, 0);
    if (ARG(1) >= 256) return D3DERR_INVALIDCALL;
    TR("SetRenderState(%u, %08x)", ARG(1), ARG(2));
    dev.rs[ARG(1)] = ARG(2);
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_GetRenderState(Ctx *c) {
    DEV();
    if (ARG(1) >= 256 || !ARG(2)) return D3DERR_INVALIDCALL;
    st(c, ARG(2), dev.rs[ARG(1)], 4);
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_SetTextureStageState(Ctx *c) {
    DEV(); REC(IDirect3DDevice8_SetTextureStageState, 4, -1, 0);
    if (ARG(1) >= 8 || ARG(2) >= 32) return D3DERR_INVALIDCALL;
    TR("SetTextureStageState(%u, %u, %08x)", ARG(1), ARG(2), ARG(3));
    dev.tss[ARG(1)][ARG(2)] = ARG(3);
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_GetTextureStageState(Ctx *c) {
    DEV();
    if (ARG(1) >= 8 || ARG(2) >= 32 || !ARG(3)) return D3DERR_INVALIDCALL;
    st(c, ARG(3), dev.tss[ARG(1)][ARG(2)], 4);
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_SetTransform(Ctx *c) {
    DEV(); REC(IDirect3DDevice8_SetTransform, 3, 2, 64);
    if (ARG(1) >= 512 || !ARG(2)) return D3DERR_INVALIDCALL;
    for (int k = 0; k < 16; k++) dev.xf[ARG(1)][k] = ldf(c, ARG(2) + 4 * k);
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_GetTransform(Ctx *c) {
    DEV();
    if (ARG(1) >= 512 || !ARG(2)) return D3DERR_INVALIDCALL;
    for (int k = 0; k < 16; k++) stf(c, ARG(2) + 4 * k, dev.xf[ARG(1)][k]);
    return D3D_OK;
}
static void mat_mul(const float *a, const float *b, float *r) {   // row vectors: r = a * b
    float t[16];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) {
        float s = 0;
        for (int k = 0; k < 4; k++) s += a[4 * i + k] * b[4 * k + j];
        t[4 * i + j] = s;
    }
    memcpy(r, t, sizeof t);
}
static uint32_t IDirect3DDevice8_MultiplyTransform(Ctx *c) {
    DEV();
    if (ARG(1) >= 512 || !ARG(2)) return D3DERR_INVALIDCALL;
    float m[16];
    for (int k = 0; k < 16; k++) m[k] = ldf(c, ARG(2) + 4 * k);
    mat_mul(m, dev.xf[ARG(1)], dev.xf[ARG(1)]);
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_SetViewport(Ctx *c) {
    DEV(); REC(IDirect3DDevice8_SetViewport, 2, 1, 24);
    uint32_t p = ARG(1);
    Obj *rt = obj_k(dev.rt);
    Viewport v = {ld(c, p, 4), ld(c, p + 4, 4), ld(c, p + 8, 4), ld(c, p + 12, 4), ldf(c, p + 16), ldf(c, p + 20)};
    TR("SetViewport(%u, %u, %ux%u, %g..%g)", v.x, v.y, v.w, v.h, (double)v.zmin, (double)v.zmax);
    if (!v.w || !v.h || v.x + v.w > rt->w || v.y + v.h > rt->h) return D3DERR_INVALIDCALL;
    dev.vp = v;
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_GetViewport(Ctx *c) {
    DEV();
    uint32_t p = ARG(1);
    st(c, p, dev.vp.x, 4); st(c, p + 4, dev.vp.y, 4); st(c, p + 8, dev.vp.w, 4); st(c, p + 12, dev.vp.h, 4);
    stf(c, p + 16, dev.vp.zmin); stf(c, p + 20, dev.vp.zmax);
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_SetMaterial(Ctx *c) { DEV(); REC(IDirect3DDevice8_SetMaterial, 2, 1, 68); if (!ARG(1)) return D3DERR_INVALIDCALL; memcpy(dev.material, G(ARG(1)), 68); return D3D_OK; }
static uint32_t IDirect3DDevice8_GetMaterial(Ctx *c) { DEV(); if (!ARG(1)) return D3DERR_INVALIDCALL; memcpy(G(ARG(1)), dev.material, 68); return D3D_OK; }
// D3DLIGHT8 is 104 bytes: Type, Diffuse, Specular, Ambient (4 floats each), Position, Direction (3 each), Range,
// Falloff, Attenuation0..2, Theta, Phi. Lights are indexed by any DWORD (see dev.light).
static int light_slot(Ctx *c, uint32_t i, int make) {
    for (uint32_t k = 0, h = i * 2654435761u >> 22; k < 1024; k++, h = (h + 1) & 1023) {
        if (dev.light[h].set && dev.light[h].idx == i) return (int)h;
        if (!dev.light[h].set) {
            if (!make) return -1;
            dev.light[h].idx = i; dev.light[h].set = 1; dev.light[h].on = 0;
            return (int)h;
        }
    }
    if (make) rn_halt(c, "d3d8: more than 1024 lights");
    return -1;
}
static uint32_t IDirect3DDevice8_SetLight(Ctx *c) {
    DEV(); REC(IDirect3DDevice8_SetLight, 3, 2, 104);
    if (!ARG(2)) return D3DERR_INVALIDCALL;
    int s = light_slot(c, ARG(1), 1);
    if (s < 0) return D3DERR_INVALIDCALL;
    memcpy(dev.light[s].v, G(ARG(2)), 104);
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_GetLight(Ctx *c) {
    DEV();
    int s = light_slot(c, ARG(1), 0);
    if (s < 0 || !ARG(2)) return D3DERR_INVALIDCALL;
    memcpy(G(ARG(2)), dev.light[s].v, 104);
    return D3D_OK;
}
// LightEnable on a light never set defines it with the default light: white directional along +z.
static uint32_t IDirect3DDevice8_LightEnable(Ctx *c) {
    DEV(); REC(IDirect3DDevice8_LightEnable, 3, -1, 0);
    int s = light_slot(c, ARG(1), 0);
    if (s < 0) {
        if ((s = light_slot(c, ARG(1), 1)) < 0) return D3DERR_INVALIDCALL;
        uint32_t *L = dev.light[s].v;
        memset(L, 0, 104);
        L[0] = 3; L[1] = L[2] = L[3] = F1; L[18] = F1;
    }
    uint32_t on = ARG(2) != 0;
    if (on == dev.light[s].on) return D3D_OK;
    dev.light[s].on = on;
    if (on) dev.lon[dev.nlon++] = (uint16_t)s;
    else for (uint32_t j = 0; j < dev.nlon; j++)
        if (dev.lon[j] == s) { memmove(&dev.lon[j], &dev.lon[j + 1], (--dev.nlon - j) * sizeof dev.lon[0]); break; }
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_GetLightEnable(Ctx *c) {
    DEV();
    int s = light_slot(c, ARG(1), 0);
    if (s < 0 || !ARG(2)) return D3DERR_INVALIDCALL;
    st(c, ARG(2), dev.light[s].on, 4);
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_SetClipPlane(Ctx *c) {
    DEV(); REC(IDirect3DDevice8_SetClipPlane, 3, 2, 16);
    if (ARG(1) >= 6 || !ARG(2)) return D3DERR_INVALIDCALL;
    memcpy(dev.clip[ARG(1)], G(ARG(2)), 16);
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_GetClipPlane(Ctx *c) {
    DEV();
    if (ARG(1) >= 6 || !ARG(2)) return D3DERR_INVALIDCALL;
    memcpy(G(ARG(2)), dev.clip[ARG(1)], 16);
    return D3D_OK;
}
// ValidateDevice: one pass for whatever is bound; the renderer takes any state this part takes.
static uint32_t IDirect3DDevice8_ValidateDevice(Ctx *c) { DEV(); if (ARG(1)) st(c, ARG(1), 1, 4); return D3D_OK; }
static uint32_t IDirect3DDevice8_ShowCursor(Ctx *c) { DEV(); int was = dev.cursor_shown; dev.cursor_shown = ARG(1) != 0; return (uint32_t)was; }
static uint32_t IDirect3DDevice8_SetCursorPosition(Ctx *c) { DEV(); return 0; }
// D3DGAMMARAMP: 3 x 256 WORDs. Kept; the renderer does not apply it yet.
static uint32_t IDirect3DDevice8_SetGammaRamp(Ctx *c) {
    DEV();
    if (ARG(2)) {
        memcpy(dev.gamma, G(ARG(2)), sizeof dev.gamma);
        uint32_t off;
        memcpy(gpu_blob(sizeof dev.gamma, &off), dev.gamma, sizeof dev.gamma);
        gpu_cmd(GPU_GAMMA, 1, &off);
    }
    return 0;
}
static uint32_t IDirect3DDevice8_GetGammaRamp(Ctx *c) { DEV(); if (ARG(1)) memcpy(G(ARG(1)), dev.gamma, sizeof dev.gamma); return 0; }

// ---- resources ------------------------------------------------------------------------------
static int pow2(uint32_t v) { return v && !(v & (v - 1)); }
static uint32_t mip_count(uint32_t w, uint32_t h) {
    uint32_t n = 1;
    while (w > 1 || h > 1) { w = w > 1 ? w / 2 : 1; h = h > 1 ? h / 2 : 1; n++; }
    return n;
}
static int bad_pool(uint32_t usage, uint32_t pool) {
    if (pool > POOL_SYSTEMMEM) return 1;
    if ((usage & (U_RENDERTARGET | U_DEPTHSTENCIL)) && pool != POOL_DEFAULT) return 1;
    if ((usage & U_DYNAMIC) && pool == POOL_MANAGED) return 1;
    return 0;
}
#define D3DLOCK_READONLY 0x10u
static uint32_t lock_mem(Obj *o) {
    if (!o->mem) {
        o->mem = guest_alloc(o->size ? o->size : 16);
        if (!o->mem) return 0;      // guest heap exhausted: the caller must fail the lock, never read into G(0) = guest 0
        if (o->on_gpu) {            // see evict_levels
            Ctx *c = g_ctx;
            read_gpu(o->container, idx(o) - objs[o->container - 1].level0, o->w, o->h, G(o->mem));
            o->on_gpu = 0;
        }
    }
    return o->mem;
}
// CreateTexture(Width, Height, Levels, Usage, Format, Pool, ppTexture)
static uint32_t IDirect3DDevice8_CreateTexture(Ctx *c) {
    DEV();
    uint32_t w = ARG(1), h = ARG(2), levels = ARG(3), usage = ARG(4), f = ARG(5), pool = ARG(6), out = ARG(7);
    TR("CreateTexture(%ux%u, %u levels, usage %x, fmt %u, pool %u)", w, h, levels, usage, f, pool);
    if (!w || !h || !out || bad_pool(usage, pool)) return D3DERR_INVALIDCALL;
    if (w > MAXTEX || h > MAXTEX || check_format(F_X8R8G8B8, usage, 3, f)) return D3DERR_INVALIDCALL;
    uint32_t full = mip_count(w, h);
    if (!levels) levels = full;
    if (levels > full) return D3DERR_INVALIDCALL;
    if ((!pow2(w) || !pow2(h)) && levels != 1) return D3DERR_INVALIDCALL;
    if (!reserve(c, levels + 1)) return D3DERR_OUTOFVIDEOMEMORY;  // levels sit right after their texture (level_of)
    uint32_t g = new_obj(c, I_TEXTURE);
    if (!g) return D3DERR_OUTOFVIDEOMEMORY;
    Obj *t = last;
    t->w = w; t->h = h; t->fmt = f; t->usage = usage; t->pool = pool; t->type = 3; t->levels = levels;
    uint32_t tk = idx(t) + 1;
    t->level0 = tk;
    for (uint32_t l = 0; l < levels; l++) {
        uint32_t lw = w >> l ? w >> l : 1, lh = h >> l ? h >> l : 1;
        new_surface(c, lw, lh, f, usage, pool, !(usage & (U_RENDERTARGET | U_DEPTHSTENCIL)));
        last->container = tk;
        if (pool != POOL_SYSTEMMEM) vram_used += last->size;
    }
    if (!(usage & U_RENDERTARGET)) { uint32_t w4[4] = {tk, w, h, levels}; gpu_cmd(GPU_TEXTURE, 4, w4); }
    st(c, out, g, 4);
    return D3D_OK;
}
static Obj *level_of(Obj *t, uint32_t l) { return l < t->levels ? &objs[t->level0 + l] : NULL; }
static uint32_t IDirect3DTexture8_GetLevelCount(Ctx *c) { SELF(I_TEXTURE); return o->levels; }
// D3DSURFACE_DESC (d3d8): Format, Type, Usage, Pool, Size, MultiSampleType, Width, Height.
static void surface_desc(Ctx *c, Obj *o, uint32_t p) {
    uint32_t d[8] = {o->fmt, 1, o->usage, o->pool, o->size, 0, o->w, o->h};
    for (int k = 0; k < 8; k++) st(c, p + 4 * k, d[k], 4);
}
static uint32_t IDirect3DTexture8_GetLevelDesc(Ctx *c) {
    SELF(I_TEXTURE);
    Obj *l = level_of(o, ARG(1));
    if (!l || !ARG(2)) return D3DERR_INVALIDCALL;
    surface_desc(c, l, ARG(2));
    return D3D_OK;
}
static uint32_t IDirect3DTexture8_GetSurfaceLevel(Ctx *c) {
    SELF(I_TEXTURE);
    Obj *l = level_of(o, ARG(1));
    if (!l || !ARG(2)) return D3DERR_INVALIDCALL;
    o->refs++;
    st(c, ARG(2), l->guest, 4);
    return D3D_OK;
}
// D3DLOCKED_RECT {Pitch, pBits}. Default-pool resources lock only when dynamic (or a lockable target).
static uint32_t lock_rect(Ctx *c, Obj *s, uint32_t out, uint32_t rect, uint32_t flags) {
    if (!out || s->locked) return D3DERR_INVALIDCALL;
    // P_D3D::DRV_getBackBuffer 0x1042a100 locks the back buffer READONLY; the flags of CreateDevice vary by mode
    // (0 in the census run), and a failed lock throws in the driver, so a color target always locks.
    if (s->gpu && ((s->usage & U_DEPTHSTENCIL) || fmt_bits(s->fmt) != 32)) { rn_halt(c, "d3d8: LockRect of a depth or 16-bit target"); return D3DERR_INVALIDCALL; }
    if (!s->gpu && s->pool == POOL_DEFAULT && !(s->usage & U_DYNAMIC) && !s->lockable) return D3DERR_INVALIDCALL;
    uint32_t bits = lock_mem(s);
    if (!bits) return D3DERR_OUTOFVIDEOMEMORY;   // the driver throws "texture:LockRect"; G(0) would wipe the TEB and the stack
    if (s->gpu) read_gpu(s->gpu, 0, s->w, s->h, G(bits));
    uint32_t off = 0;
    if (rect) {
        uint32_t x = ld(c, rect, 4), y = ld(c, rect + 4, 4);
        off = is_dxt(s->fmt) ? (y / 4) * s->pitch + (x / 4) * (s->fmt == F_DXT1 ? 8 : 16) : y * s->pitch + x * (fmt_bits(s->fmt) / 8);
    }
    s->locked = flags & D3DLOCK_READONLY ? 2 : 1;
    st(c, out, s->pitch, 4);
    st(c, out + 4, bits + off, 4);
    return D3D_OK;
}
// ---- texel decoding: every format the game creates becomes BGRA8 (bytes B G R A) for both backends. A8R8G8B8 and
// X8R8G8B8, all the driver makes (census/d3d8_spec.tsv), are that already in memory.
static uint32_t rgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a) { return b | g << 8 | r << 16 | a << 24; }
static uint32_t x5(uint32_t v) { return (v << 3) | (v >> 2); }
static uint32_t x6(uint32_t v) { return (v << 2) | (v >> 4); }
static uint32_t x4(uint32_t v) { return v << 4 | v; }
static void dxt_color(const uint8_t *b, int dxt1, uint32_t out[16]) {
    uint32_t c0 = b[0] | b[1] << 8, c1 = b[2] | b[3] << 8, bits = b[4] | b[5] << 8 | b[6] << 16 | (uint32_t)b[7] << 24;
    uint32_t r[4], g[4], bl[4], a[4] = {255, 255, 255, 255};
    r[0] = x5(c0 >> 11); g[0] = x6((c0 >> 5) & 63); bl[0] = x5(c0 & 31);
    r[1] = x5(c1 >> 11); g[1] = x6((c1 >> 5) & 63); bl[1] = x5(c1 & 31);
    if (c0 > c1 || !dxt1) {
        r[2] = (2 * r[0] + r[1] + 1) / 3; g[2] = (2 * g[0] + g[1] + 1) / 3; bl[2] = (2 * bl[0] + bl[1] + 1) / 3;
        r[3] = (r[0] + 2 * r[1] + 1) / 3; g[3] = (g[0] + 2 * g[1] + 1) / 3; bl[3] = (bl[0] + 2 * bl[1] + 1) / 3;
    } else {
        r[2] = (r[0] + r[1]) / 2; g[2] = (g[0] + g[1]) / 2; bl[2] = (bl[0] + bl[1]) / 2;
        r[3] = g[3] = bl[3] = a[3] = 0;
    }
    for (int k = 0; k < 16; k++) { uint32_t i = bits >> (2 * k) & 3; out[k] = rgba(r[i], g[i], bl[i], a[i]); }
}
static void dxt_alpha5(const uint8_t *b, uint32_t out[16]) {
    uint32_t a[8] = {b[0], b[1]};
    if (a[0] > a[1]) for (int k = 1; k < 7; k++) a[k + 1] = ((7 - k) * a[0] + k * a[1] + 3) / 7;
    else { for (int k = 1; k < 5; k++) a[k + 1] = ((5 - k) * a[0] + k * a[1] + 2) / 5; a[6] = 0; a[7] = 255; }
    uint64_t bits = 0;
    for (int k = 0; k < 6; k++) bits |= (uint64_t)b[2 + k] << (8 * k);
    for (int k = 0; k < 16; k++) out[k] = (out[k] & 0x00ffffffu) | a[bits >> (3 * k) & 7] << 24;
}
static int decode_texels(uint32_t f, const uint8_t *src, uint32_t pitch, uint32_t w, uint32_t h, uint32_t *dst) {
    if (is_dxt(f)) {
        int bs = f == F_DXT1 ? 8 : 16;
        for (uint32_t by = 0; by < (h + 3) / 4; by++)
            for (uint32_t bx = 0; bx < (w + 3) / 4; bx++) {
                const uint8_t *b = src + by * pitch + bx * (uint32_t)bs;
                uint32_t px[16];
                dxt_color(b + bs - 8, f == F_DXT1, px);
                if (f == F_DXT2 || f == F_DXT3)
                    for (int k = 0; k < 16; k++) px[k] = (px[k] & 0x00ffffffu) | x4(b[k / 2] >> (4 * (k & 1)) & 15) << 24;
                if (f == F_DXT4 || f == F_DXT5) dxt_alpha5(b, px);
                for (int k = 0; k < 16; k++) {
                    uint32_t x = bx * 4 + (k & 3), y = by * 4 + (k >> 2);
                    if (x < w && y < h) dst[y * w + x] = px[k];
                }
            }
        return 1;
    }
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *r = src + y * pitch;
        if (f == F_A8R8G8B8 || f == F_X8R8G8B8) {
            memcpy(dst + y * w, r, 4u * w);
            if (f == F_X8R8G8B8) for (uint32_t x = 0; x < w; x++) dst[y * w + x] |= 0xff000000u;
            continue;
        }
        for (uint32_t x = 0; x < w; x++) {
            uint32_t v, o;
            switch (f) {
            case F_R5G6B5: v = r[2 * x] | r[2 * x + 1] << 8; o = rgba(x5(v >> 11), x6(v >> 5 & 63), x5(v & 31), 255); break;
            case F_X1R5G5B5: case F_A1R5G5B5:
                v = r[2 * x] | r[2 * x + 1] << 8;
                o = rgba(x5(v >> 10 & 31), x5(v >> 5 & 31), x5(v & 31), f == F_X1R5G5B5 || (v >> 15) ? 255 : 0);
                break;
            case F_A4R4G4B4: v = r[2 * x] | r[2 * x + 1] << 8; o = rgba(x4(v >> 8 & 15), x4(v >> 4 & 15), x4(v & 15), x4(v >> 12)); break;
            case F_A8: o = rgba(0, 0, 0, r[x]); break;
            case F_L8: o = rgba(r[x], r[x], r[x], 255); break;
            case F_A8L8: o = rgba(r[2 * x], r[2 * x], r[2 * x], r[2 * x + 1]); break;
            default: return 0;
            }
            dst[y * w + x] = o;
        }
    }
    return 1;
}
// A texture level's contents count from its unlock (or a copy into it): send the whole level to the renderer.
static void upload_level(Ctx *c, Obj *s) {
    Obj *t = obj_k(s->container);
    if (!t || t->iface != I_TEXTURE || (t->usage & U_RENDERTARGET) || t->pool == POOL_SYSTEMMEM) return;
    uint32_t mem = lock_mem(s);
    if (!mem) return;                                       // guest heap exhausted: nothing to read, the GPU copy stays
    const uint8_t *src = (const uint8_t *)G(mem);           // first: it can hand the packet off
    uint32_t off;
    uint32_t *px = (uint32_t *)gpu_blob(4u * s->w * s->h, &off);
    if (!decode_texels(s->fmt, src, s->pitch, s->w, s->h, px)) {
        rn_halt(c, "d3d8: texture format %u has no decoder", s->fmt);
        return;
    }
    uint32_t w[5] = {s->container, idx(s) - t->level0, s->w, s->h, off};
    gpu_cmd(GPU_TEXDATA, 5, w);
    flush_big();
    d3d_load_checkpoint();
}
// Once a MANAGED 32-bit texture's chain is complete its texels live on the GPU, and the level memory goes back to the
// heap (1.8 GB of level 1 otherwise). Not earlier: D3DX filtering (0x10433c73) locks level n READONLY to build n + 1.
// These two formats reach the GPU unchanged, so a later lock reads them back (lock_mem), as P_Map's teardown does through
// 0x1042db00, and that level then keeps its memory: a game that relocks may do it every frame.
static void evict_levels(Obj *s) {
    Obj *t = &objs[s->container - 1];
    if (t->iface != I_TEXTURE || t->pool != POOL_MANAGED || t->evicted || (t->fmt != F_A8R8G8B8 && t->fmt != F_X8R8G8B8)) return;
    if (idx(s) - t->level0 != t->levels - 1) return;
    t->evicted = 1;
    for (uint32_t l = 0; l < t->levels; l++) {
        Obj *o = level_of(t, l);
        if (o->locked || !o->mem) continue;
        guest_free(o->mem); o->mem = 0; o->on_gpu = 1;
    }
}
// RN_SAVE=1: the save capture's split, one line per GetFrontBuffer and one per UnlockRect (DRV_getScreen 0x10429d20 copies
// the locked surface row by row into the G_Image between the two).
static int save_dbg(void) { static int v = -1; if (v < 0) v = getenv("RN_SAVE") != NULL; return v; }
static uint32_t sv_gf_guest, sv_gf_w, sv_gf_h; static uint64_t sv_gf_t;
static uint32_t unlock_rect(Ctx *c, Obj *s) {
    if (!s->locked) return D3DERR_INVALIDCALL;
    int wrote = s->locked == 1;
    s->locked = 0;
    if (wrote) upload_level(c, s);
    if (s->container) evict_levels(s);
    if (save_dbg() && sv_gf_guest == s->guest) {   // the guest's row copy of the screenshot, between GetFrontBuffer and this
        fprintf(stderr, "SAVE gf %ux%u: guest copy %llu us\n", sv_gf_w, sv_gf_h, (unsigned long long)(now_us() - sv_gf_t));
        sv_gf_guest = 0;
    }
    return D3D_OK;
}
static uint32_t IDirect3DTexture8_LockRect(Ctx *c) {
    SELF(I_TEXTURE);
    Obj *l = level_of(o, ARG(1));
    return l ? lock_rect(c, l, ARG(2), ARG(3), ARG(4)) : D3DERR_INVALIDCALL;
}
static uint32_t IDirect3DTexture8_UnlockRect(Ctx *c) {
    SELF(I_TEXTURE);
    Obj *l = level_of(o, ARG(1));
    return l ? unlock_rect(c, l) : D3DERR_INVALIDCALL;
}
static uint32_t IDirect3DTexture8_AddDirtyRect(Ctx *c) { SELF(I_TEXTURE); return D3D_OK; }
static uint32_t IDirect3DTexture8_GetType(Ctx *c) { SELF(I_TEXTURE); return 3; }
static uint32_t IDirect3DTexture8_PreLoad(Ctx *c) { SELF(I_TEXTURE); return 0; }
static uint32_t IDirect3DTexture8_SetPriority(Ctx *c) { SELF(I_TEXTURE); return 0; }
static uint32_t IDirect3DTexture8_SetLOD(Ctx *c) { SELF(I_TEXTURE); return 0; }
static uint32_t IDirect3DSurface8_GetDesc(Ctx *c) { SELF(I_SURFACE); surface_desc(c, o, ARG(1)); return D3D_OK; }
static uint32_t IDirect3DSurface8_LockRect(Ctx *c) { SELF(I_SURFACE); return lock_rect(c, o, ARG(1), ARG(2), ARG(3)); }
static uint32_t IDirect3DSurface8_UnlockRect(Ctx *c) { SELF(I_SURFACE); return unlock_rect(c, o); }
// GetContainer(riid, ppContainer): the texture of a level, else the device.
static uint32_t IDirect3DSurface8_GetContainer(Ctx *c) {
    SELF(I_SURFACE);
    if (!ARG(2)) return D3DERR_INVALIDCALL;
    if (o->container) return ref_out(c, ARG(2), o->container);
    objs[ld(c, dev.guest + 4, 4) - 1].refs++;
    st(c, ARG(2), dev.guest, 4);
    return D3D_OK;
}
static uint32_t res_GetDevice(Ctx *c) {
    SELF(-1);
    (void)o;
    objs[ld(c, dev.guest + 4, 4) - 1].refs++;
    st(c, ARG(1), dev.guest, 4);
    return D3D_OK;
}

static uint32_t new_buffer(Ctx *c, int iface, uint32_t len, uint32_t usage, uint32_t f, uint32_t pool, uint32_t out) {
    if (!len || !out || bad_pool(usage, pool) || (usage & (U_RENDERTARGET | U_DEPTHSTENCIL))) return D3DERR_INVALIDCALL;
    uint32_t g = new_obj(c, iface);
    if (!g) return D3DERR_OUTOFVIDEOMEMORY;
    Obj *o = last;
    o->size = len; o->usage = usage; o->pool = pool;
    if (iface == I_VB) { o->fvf = f; o->fmt = F_VERTEXDATA; o->type = 6; } else { o->fmt = f; o->type = 7; }
    if (pool == POOL_DEFAULT) vram_used += len;
    st(c, out, g, 4);
    return D3D_OK;
}
// CreateVertexBuffer(Length, Usage, FVF, Pool, ppVB), CreateIndexBuffer(Length, Usage, Format, Pool, ppIB)
static uint32_t IDirect3DDevice8_CreateVertexBuffer(Ctx *c) {
    DEV();
    TR("CreateVertexBuffer(%u bytes, usage %x, fvf %x, pool %u)", ARG(1), ARG(2), ARG(3), ARG(4));
    return new_buffer(c, I_VB, ARG(1), ARG(2), ARG(3), ARG(4), ARG(5));
}
static uint32_t IDirect3DDevice8_CreateIndexBuffer(Ctx *c) {
    DEV();
    TR("CreateIndexBuffer(%u bytes, usage %x, fmt %u, pool %u)", ARG(1), ARG(2), ARG(3), ARG(4));
    if (ARG(3) != F_INDEX16 && ARG(3) != F_INDEX32) return D3DERR_INVALIDCALL;
    return new_buffer(c, I_IB, ARG(1), ARG(2), ARG(3), ARG(4), ARG(5));
}
// Lock(OffsetToLock, SizeToLock, ppbData, Flags): size 0 = the whole buffer. Nested locks are allowed.
static uint32_t buffer_lock(Ctx *c, Obj *o) {
    uint32_t off = ARG(1), size = ARG(2), out = ARG(3);
    if (!out || off > o->size || (size && off + size > o->size)) return D3DERR_INVALIDCALL;
    uint32_t mem = lock_mem(o);
    if (!mem) return D3DERR_OUTOFVIDEOMEMORY;   // guest heap exhausted: never hand the game G(0)
    o->locked++;
    st(c, out, mem + off, 4);
    return D3D_OK;
}
static uint32_t buffer_unlock(Obj *o) { if (!o->locked) return D3DERR_INVALIDCALL; o->locked--; return D3D_OK; }
static uint32_t IDirect3DVertexBuffer8_Lock(Ctx *c) { SELF(I_VB); return buffer_lock(c, o); }
static uint32_t IDirect3DVertexBuffer8_Unlock(Ctx *c) { SELF(I_VB); return buffer_unlock(o); }
static uint32_t IDirect3DIndexBuffer8_Lock(Ctx *c) { SELF(I_IB); return buffer_lock(c, o); }
static uint32_t IDirect3DIndexBuffer8_Unlock(Ctx *c) { SELF(I_IB); return buffer_unlock(o); }
static uint32_t IDirect3DVertexBuffer8_GetDesc(Ctx *c) {
    SELF(I_VB);
    uint32_t d[6] = {F_VERTEXDATA, 6, o->usage, o->pool, o->size, o->fvf};
    for (int k = 0; k < 6; k++) st(c, ARG(1) + 4 * k, d[k], 4);
    return D3D_OK;
}
static uint32_t IDirect3DIndexBuffer8_GetDesc(Ctx *c) {
    SELF(I_IB);
    uint32_t d[5] = {o->fmt, 7, o->usage, o->pool, o->size};
    for (int k = 0; k < 5; k++) st(c, ARG(1) + 4 * k, d[k], 4);
    return D3D_OK;
}
static uint32_t IDirect3DVertexBuffer8_PreLoad(Ctx *c) { SELF(I_VB); return 0; }
static uint32_t IDirect3DIndexBuffer8_PreLoad(Ctx *c) { SELF(I_IB); return 0; }

// CreateRenderTarget(Width, Height, Format, MultiSample, Lockable, ppSurface)
static uint32_t IDirect3DDevice8_CreateRenderTarget(Ctx *c) {
    DEV();
    uint32_t w = ARG(1), h = ARG(2), f = ARG(3), ms = ARG(4), out = ARG(6);
    if (!w || !h || !out || !rt_fmt(f)) return D3DERR_INVALIDCALL;
    if (ms) { rn_halt(c, "d3d8: multisampled render target"); return D3DERR_INVALIDCALL; }
    st(c, out, new_surface(c, w, h, f, U_RENDERTARGET, POOL_DEFAULT, ARG(5) != 0), 4);
    return D3D_OK;
}
// CreateDepthStencilSurface(Width, Height, Format, MultiSample, ppSurface)
static uint32_t IDirect3DDevice8_CreateDepthStencilSurface(Ctx *c) {
    DEV();
    uint32_t w = ARG(1), h = ARG(2), f = ARG(3), ms = ARG(4), out = ARG(5);
    if (!w || !h || !out || !ds_fmt(f)) return D3DERR_INVALIDCALL;
    if (ms) { rn_halt(c, "d3d8: multisampled depth buffer"); return D3DERR_INVALIDCALL; }
    st(c, out, new_surface(c, w, h, f, U_DEPTHSTENCIL, POOL_DEFAULT, f == F_D16_LOCKABLE), 4);
    return D3D_OK;
}
// CreateImageSurface(Width, Height, Format, ppSurface): a lockable system memory surface, any texture format.
static uint32_t IDirect3DDevice8_CreateImageSurface(Ctx *c) {
    DEV();
    uint32_t w = ARG(1), h = ARG(2), f = ARG(3), out = ARG(4);
    TR("CreateImageSurface(%ux%u, fmt %u)", w, h, f);
    if (!w || !h || !out || !fmt_bits(f)) return D3DERR_INVALIDCALL;
    st(c, out, new_surface(c, w, h, f, 0, POOL_SYSTEMMEM, 1), 4);
    return D3D_OK;
}
// GetFrontBuffer(pDestSurface): A8R8G8B8 of the screen's size (P_D3D::DRV_getScreen 0x10429d20). Present keeps the back
// buffer, and this game asks outside its frame, so the back buffer is what the screen shows.
static uint32_t IDirect3DDevice8_GetFrontBuffer(Ctx *c) {
    DEV();
    Obj *d = obj_of(c, ARG(1), I_SURFACE), *b = obj_k(dev.backbuf);
    if (!d || d->fmt != F_A8R8G8B8 || d->w < b->w || d->h < b->h) return D3DERR_INVALIDCALL;
    uint32_t mem = lock_mem(d);
    if (!mem) return D3DERR_OUTOFVIDEOMEMORY;   // guest heap exhausted: read_gpu into G(0) would wipe the TEB and the stack
    uint32_t *px = (uint32_t *)G(mem), n = b->w * b->h;
    uint64_t t0 = now_us();
    read_gpu(b->gpu, 0, b->w, b->h, px);
    uint64_t t1 = now_us();
    // A8R8G8B8: the screen has no alpha channel, D3D8 returns 255 there. Two pixels per store (the copy leaves A empty and
    // GetFrontBuffer's only caller in this game, DRV_getScreen, copies the surface row by row and never writes it).
    if (!((uintptr_t)px & 7u)) {
        uint32_t k = 0;
        for (; k + 1 < n; k += 2) *(uint64_t *)(px + k) |= 0xff000000ff000000ull;
        if (k < n) px[k] |= 0xff000000u;
    } else for (uint32_t k = 0; k < n; k++) px[k] |= 0xff000000u;
    uint64_t t2 = now_us();
    if (d->w != b->w) for (uint32_t y = b->h; y--;) memmove((uint8_t *)px + y * d->pitch, px + y * b->w, 4u * b->w);
    if (save_dbg()) {
        fprintf(stderr, "SAVE gf %ux%u: read %llu us, alpha %llu us, move %llu us\n", b->w, b->h,
                (unsigned long long)(t1 - t0), (unsigned long long)(t2 - t1), (unsigned long long)(now_us() - t2));
        sv_gf_guest = ARG(1); sv_gf_w = d->w; sv_gf_h = d->h; sv_gf_t = now_us();
    }
    return D3D_OK;
}
// CopyRects(pSource, pSourceRects, cRects, pDestination, pDestPoints): same format, no stretching. Into a texture
// level the result is uploaded; render target sources would need a readback.
static uint32_t IDirect3DDevice8_CopyRects(Ctx *c) {
    DEV();
    Obj *s = obj_of(c, ARG(1), I_SURFACE), *d = obj_of(c, ARG(4), I_SURFACE);
    if (!s || !d || s->fmt != d->fmt) return D3DERR_INVALIDCALL;
    if (s->gpu || d->gpu) { rn_halt(c, "d3d8: CopyRects with a render target (%ux%u -> %ux%u)", s->w, s->h, d->w, d->h); return D3DERR_INVALIDCALL; }
    uint32_t n = ARG(3) ? ARG(3) : 1, bpp = fmt_bits(s->fmt);
    for (uint32_t k = 0; k < n; k++) {
        uint32_t x0 = 0, y0 = 0, x1 = s->w, y1 = s->h, dx = 0, dy = 0;
        if (ARG(2)) { uint32_t r = ARG(2) + 16 * k; x0 = ld(c, r, 4); y0 = ld(c, r + 4, 4); x1 = ld(c, r + 8, 4); y1 = ld(c, r + 12, 4); }
        if (ARG(5)) { dx = ld(c, ARG(5) + 8 * k, 4); dy = ld(c, ARG(5) + 8 * k + 4, 4); } else { dx = x0; dy = y0; }
        if (x1 > s->w || y1 > s->h || dx + (x1 - x0) > d->w || dy + (y1 - y0) > d->h) return D3DERR_INVALIDCALL;
        if (is_dxt(s->fmt)) { x0 /= 4; x1 = (x1 + 3) / 4; y0 /= 4; y1 = (y1 + 3) / 4; dx /= 4; dy /= 4; bpp = s->fmt == F_DXT1 ? 64 : 128; }
        uint32_t spm = lock_mem(s), dpm = lock_mem(d);
        if (!spm || !dpm) return D3DERR_OUTOFVIDEOMEMORY;   // guest heap exhausted: never copy through G(0)
        uint8_t *sp = (uint8_t *)G(spm), *dp = (uint8_t *)G(dpm);
        for (uint32_t y = y0; y < y1; y++)
            memmove(dp + (dy + y - y0) * d->pitch + dx * bpp / 8, sp + y * s->pitch + x0 * bpp / 8, (x1 - x0) * bpp / 8);
    }
    upload_level(c, d);
    return D3D_OK;
}
// UpdateTexture(pSource, pDestination): system memory texture -> default pool texture, every level.
static uint32_t IDirect3DDevice8_UpdateTexture(Ctx *c) {
    DEV();
    Obj *s = obj_of(c, ARG(1), I_TEXTURE), *d = obj_of(c, ARG(2), I_TEXTURE);
    if (!s || !d || s->fmt != d->fmt || s->w != d->w || s->h != d->h) return D3DERR_INVALIDCALL;
    for (uint32_t l = 0; l < d->levels && l < s->levels; l++) {
        Obj *a = level_of(s, l), *b = level_of(d, l);
        uint32_t am = lock_mem(a), bm = lock_mem(b);
        if (!am || !bm) return D3DERR_OUTOFVIDEOMEMORY;   // guest heap exhausted: never copy through G(0)
        memcpy(G(bm), G(am), b->size);
        upload_level(c, b);
    }
    return D3D_OK;
}
// SetRenderTarget(pRenderTarget, pNewZStencil): NULL keeps the current one (d3d8 docs).
static uint32_t IDirect3DDevice8_SetRenderTarget(Ctx *c) {
    DEV();
    uint32_t p = ARG(1), z = ARG(2);
    if (p) {
        Obj *s = obj_of(c, p, I_SURFACE);
        if (!s || !(s->usage & U_RENDERTARGET)) return D3DERR_INVALIDCALL;
        dev.rt = idx(s) + 1;
        reset_viewport(s);
    }
    Obj *d = z ? obj_of(c, z, I_SURFACE) : NULL;
    if (z && (!d || !(d->usage & U_DEPTHSTENCIL))) return D3DERR_INVALIDCALL;
    dev.ds = d ? idx(d) + 1 : 0;
    TR("SetRenderTarget(%08x, %08x)", p, z);
    return D3D_OK;
}

// ---- vertex shaders, declarations ---------------------------------------------------------------
// D3DVERTEXELEMENT9 lists for the backends: {WORD Stream, Offset; BYTE Type, Method, Usage, UsageIndex}.
static uint32_t make_decl(Ctx *c, const uint8_t *el, uint32_t n) {
    static struct { uint8_t el[8 * 17]; uint32_t n, k1; } cache[256];
    static int ncache;
    for (int k = 0; k < ncache; k++) if (cache[k].n == n && !memcmp(cache[k].el, el, 8 * n)) return cache[k].k1;
    if (!new_obj(c, I_SB)) return 0;        // a declaration is an object id the backends know (GPU_DECL), no COM object
    uint32_t k1 = idx(last) + 1;
    objs[k1 - 1].dead = 1;                  // not a state block token the game may use
    uint32_t *w = calloc(2 + 2 * n, 4);
    w[0] = k1; w[1] = n;
    memcpy(w + 2, el, 8u * n);
    gpu_cmd(GPU_DECL, (int)(2 + 2 * n), w);
    free(w);
    if (ncache < 256) { memcpy(cache[ncache].el, el, 8 * n); cache[ncache].n = n; cache[ncache++].k1 = k1; }
    return k1;
}
#define EL(type, usage, ui, size) do { uint8_t *e = el + 8 * ne++; e[0] = (uint8_t)stream; e[1] = 0; e[2] = (uint8_t)off; \
    e[3] = (uint8_t)(off >> 8); e[4] = (type); e[5] = 0; e[6] = (usage); e[7] = (ui); off += (size); } while (0)
// The declaration an FVF describes ("Mapping FVF Codes"), with the fixed registers of host/ffp.h.
static uint32_t fvf_decl(Ctx *c, uint32_t fvf, FfpVs *k) {
    uint8_t el[8 * 17];
    uint32_t ne = 0, off = 0, stream = 0;
    uint32_t pos = fvf & 0xe;
    memset(k, 0, sizeof *k);
    if (pos == 0x2) { EL(2, 0, 0, 12); k->pos = 1; k->pos_usage = 0; }
    else if (pos == 0x4) { EL(3, 9, 0, 16); k->pos = 2; k->pos_usage = 9; }
    else { rn_halt(c, "d3d8: FVF %x (blend weights)", fvf); return 0; }
    if (fvf & 0x10) { EL(2, 3, 0, 12); k->normal = 1; }
    if (fvf & 0x20) { rn_halt(c, "d3d8: FVF %x with point size", fvf); return 0; }
    if (fvf & 0x40) { EL(4, 10, 0, 4); k->diffuse = 1; }
    if (fvf & 0x80) { EL(4, 10, 1, 4); k->specular = 1; }
    static const uint8_t tc_type[4] = {1, 2, 3, 0}, tc_size[4] = {8, 12, 16, 4};
    k->ntex = (uint8_t)((fvf >> 8) & 0xf);
    for (uint32_t t = 0; t < k->ntex; t++) { uint32_t f = (fvf >> (16 + 2 * t)) & 3; EL(tc_type[f], 5, t, tc_size[f]); k->tdim[t] = (uint8_t)(tc_size[f] / 4); }
    return make_decl(c, el, ne);
}
static uint32_t fvf_stride(uint32_t fvf) {
    static const uint32_t tc_size[4] = {8, 12, 16, 4};
    uint32_t s = (fvf & 0xe) == 0x4 ? 16 : 12;
    if (fvf & 0x10) s += 12;
    if (fvf & 0x20) s += 4;
    if (fvf & 0x40) s += 4;
    if (fvf & 0x80) s += 4;
    for (uint32_t t = 0; t < ((fvf >> 8) & 0xf); t++) s += tc_size[(fvf >> (16 + 2 * t)) & 3];
    return s;
}
// D3DVSD tokens: STREAM (1 << 29 | n), REG (2 << 29 | type << 16 | reg), SKIP (2 << 29 | 1 << 28 | dwords << 16),
// CONST (4 << 29 | count << 25 | address, then 4 x count floats), END (0xffffffff). D3DVSDT types = D3DDECLTYPE 0..7.
// Programmable shaders read v<reg> as TEXCOORD<reg> (the dcl we add); fixed-function declarations name D3DVSDE_*.
static uint32_t vsd_decl(Ctx *c, Obj *o, const uint32_t *t, int fixed, FfpVs *k) {
    static const uint8_t size[8] = {4, 8, 12, 16, 4, 4, 4, 8};
    uint8_t el[8 * 17];
    uint32_t ne = 0, off = 0, stream = 0;
    if (k) memset(k, 0, sizeof *k);
    for (uint32_t i = 0;; i++) {
        uint32_t x = t[i], kind = x >> 29;
        if (x == 0xffffffffu) break;
        if (kind == 0) continue;
        if (kind == 1) { stream = x & 0xf; off = 0; continue; }
        if (kind == 2 && (x >> 28 & 1)) { off += 4 * (x >> 16 & 0xf); continue; }
        if (kind == 2) {
            uint32_t type = x >> 16 & 0xf, reg = x & 0x1f;
            if (type > 7 || ne == 16) { rn_halt(c, "d3d8: vertex shader declaration type %u", type); return 0; }
            if (!fixed) { EL((uint8_t)type, 5, (uint8_t)reg, size[type]); continue; }
            // D3DVSDE_POSITION 0, BLENDWEIGHT 1, BLENDINDICES 2, NORMAL 3, PSIZE 4, DIFFUSE 5, SPECULAR 6, TEXCOORD0..7 7..14
            if (reg == 0) { EL((uint8_t)type, 0, 0, size[type]); k->pos = 1; }
            else if (reg == 3) { EL((uint8_t)type, 3, 0, size[type]); k->normal = 1; }
            else if (reg == 5) { EL((uint8_t)type, 10, 0, size[type]); k->diffuse = 1; }
            else if (reg == 6) { EL((uint8_t)type, 10, 1, size[type]); k->specular = 1; }
            else if (reg >= 7 && reg <= 14) {
                EL((uint8_t)type, 5, (uint8_t)(reg - 7), size[type]);
                static const uint8_t dim[8] = {1, 2, 3, 4, 4, 4, 2, 4};
                k->tdim[reg - 7] = dim[type];
                if (reg - 7 + 1u > k->ntex) k->ntex = (uint8_t)(reg - 7 + 1);
            } else { rn_halt(c, "d3d8: fixed-function declaration register %u", reg); return 0; }
            continue;
        }
        if (kind == 4) {
            uint32_t cnt = x >> 25 & 0xf, addr = x & 0x7f;
            if (o) {
                o->cmem = realloc(o->cmem, 16u * (o->ncmem + cnt));
                if (!o->ncmem) o->cbase = addr;
                if (addr != o->cbase + o->ncmem) { rn_halt(c, "d3d8: D3DVSD_CONST blocks not contiguous"); return 0; }
                memcpy(o->cmem + o->ncmem, t + i + 1, 16u * cnt);
                o->ncmem += cnt;
            }
            i += 4 * cnt;
            continue;
        }
        rn_halt(c, "d3d8: vertex shader declaration token %08x", x);
        return 0;
    }
    if (fixed && k->pos) k->pos_usage = 0;
    if (stream && !fixed) {}
    return make_decl(c, el, ne);
}
// A shader's tokens through the decoder, then GPU_SHADER (tokens and WGSL) for both backends. Returns its index + 1.
static uint32_t shader_obj(Ctx *c, int iface, const uint32_t *tok, uint32_t n) {
    Sm1 *sm = calloc(1, sizeof *sm);
    char err[128];
    if (sm1_decode(sm, tok, n, err, sizeof err)) { rn_halt(c, "d3d8: shader: %s", err); return 0; }
    if (!new_obj(c, iface)) return 0;
    Obj *o = last;
    o->nblob = n;
    o->blob = malloc(4u * n);
    memcpy(o->blob, tok, 4u * n);
    o->sm = sm;
    char *wgsl = sm1_wgsl(sm);
    uint32_t m = (uint32_t)strlen(wgsl), nw = 21 + n + (m + 4) / 4;
    uint32_t *w = calloc(nw, 4);
    w[0] = idx(o) + 1; w[1] = iface == I_PS; w[2] = n; w[3] = m;
    for (int k = 0; k < 16; k++) if (sm->inputs >> k & 1) w[4 + k] = 1u << 31 | (uint32_t)sm->in_usage[k] << 8 | sm->in_index[k];
    w[20] = sm->tex_used;
    memcpy(w + 21, o->blob, 4u * n);
    memcpy(w + 21 + n, wgsl, m);
    gpu_cmd(GPU_SHADER, (int)nw, w);
    free(w);
    free(wgsl);
    return idx(o) + 1;
}
static void disasm_line(const char *l) { rn_log("d3d8   %s", l); }
static uint32_t ntok(const uint32_t *t) { uint32_t n = 1; while (t[n - 1] != 0x0000ffffu && n < 65536) n++; return n; }
// Handles: a vertex shader is (object index + 1) << 1 | 1 (FVF codes are even); a pixel shader its object index + 1.
static Obj *vs_of(uint32_t h) { uint32_t k1 = h >> 1; return (h & 1) && k1 && k1 <= (uint32_t)nobjs && objs[k1 - 1].iface == I_VS && !objs[k1 - 1].dead ? &objs[k1 - 1] : NULL; }
static Obj *ps_of(uint32_t h) { return h && h <= (uint32_t)nobjs && objs[h - 1].iface == I_PS && !objs[h - 1].dead ? &objs[h - 1] : NULL; }
// CreateVertexShader(pDeclaration, pFunction, pHandle, Usage)
static uint32_t IDirect3DDevice8_CreateVertexShader(Ctx *c) {
    DEV();
    uint32_t pd = ARG(1), pf = ARG(2), out = ARG(3);
    if (!pd || !out) return D3DERR_INVALIDCALL;
    uint32_t nd = 0;
    while (ld(c, pd + 4 * nd, 4) != 0xffffffffu) if (++nd > 4096) return D3DERR_INVALIDCALL;
    nd++;
    uint32_t *vsd = malloc(4u * nd);
    memcpy(vsd, G(pd), 4u * nd);
    uint32_t k1 = 0;
    if (pf) {
        // Direct3D 8 vs_1_1 has no dcl: add one per declared register (TEXCOORD<reg>, matching vsd_decl).
        const uint32_t *f = (const uint32_t *)G(pf);
        uint32_t n = ntok(f);
        if (f[0] >> 16 != 0xfffe || (f[0] & 0xffff) > 0x101) { free(vsd); return D3DERR_INVALIDCALL; }
        uint32_t *t = malloc(4u * (n + 3 * 17)), m = 0;
        t[m++] = f[0];
        uint16_t seen = 0;
        for (uint32_t i = 0; vsd[i] != 0xffffffffu; i++)
            if (vsd[i] >> 29 == 2 && !(vsd[i] >> 28 & 1)) {
                uint32_t r = vsd[i] & 0xf;
                if (seen >> r & 1) continue;
                seen |= (uint16_t)(1u << r);
                t[m++] = OP_DCL; t[m++] = 0x80000000u | 5 | r << 16; t[m++] = 0x80000000u | 1u << 28 | 0xfu << 16 | r;
            }
        memcpy(t + m, f + 1, 4u * (n - 1));
        m += n - 1;
        k1 = shader_obj(c, I_VS, t, m);
        free(t);
        if (!k1) { free(vsd); return D3DERR_INVALIDCALL; }
        Obj *o = &objs[k1 - 1];
        o->orig = malloc(4u * n); memcpy(o->orig, f, 4u * n); o->norig = n;
        o->decl = vsd_decl(c, o, vsd, 0, NULL);
        if (rn_trace() || getenv("RN_VS")) { rn_log("d3d8 vertex shader %x:", k1); sm1_disasm(o->sm, disasm_line); }
    } else {
        // A fixed-function vertex shader with a declaration (no function).
        if (!new_obj(c, I_VS)) { free(vsd); return D3DERR_INVALIDCALL; }
        k1 = idx(last) + 1;
        FfpVs fk;
        objs[k1 - 1].decl = vsd_decl(c, &objs[k1 - 1], vsd, 1, &fk);
    }
    Obj *o = &objs[k1 - 1];
    o->vsd = vsd; o->nvsd = nd;
    TR("CreateVertexShader(function %s) -> %x", pf ? "yes" : "fixed", k1 << 1 | 1);
    st(c, out, k1 << 1 | 1, 4);
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_SetVertexShader(Ctx *c) {
    DEV(); REC(IDirect3DDevice8_SetVertexShader, 2, -1, 0);
    uint32_t h = ARG(1);
    TR("SetVertexShader(%x)", h);
    if (h & 1) {
        Obj *o = vs_of(h);
        if (!o) return D3DERR_INVALIDCALL;
        for (uint32_t k = 0; k < o->ncmem && o->cbase + k < 96; k++) memcpy(dev.vsf[o->cbase + k], o->cmem[k], 16);
    }
    dev.vs = h;
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_GetVertexShader(Ctx *c) { DEV(); st(c, ARG(1), dev.vs, 4); return D3D_OK; }
static uint32_t IDirect3DDevice8_DeleteVertexShader(Ctx *c) {
    DEV();
    Obj *o = vs_of(ARG(1));
    if (!o) return D3DERR_INVALIDCALL;
    o->dead = 1;
    if (dev.vs == ARG(1)) dev.vs = 0;
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_SetVertexShaderConstant(Ctx *c) {
    DEV(); REC(IDirect3DDevice8_SetVertexShaderConstant, 4, 2, 16 * ARG(3));
    uint32_t r = ARG(1), p = ARG(2), n = ARG(3);
    if (!p || r + n > 96) return D3DERR_INVALIDCALL;
    memcpy(dev.vsf[r], G(p), 16u * n);
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_GetVertexShaderConstant(Ctx *c) {
    DEV();
    if (!ARG(2) || ARG(1) + ARG(3) > 96) return D3DERR_INVALIDCALL;
    memcpy(G(ARG(2)), dev.vsf[ARG(1)], 16u * ARG(3));
    return D3D_OK;
}
// Get...Declaration / Function(Handle, pData, pSizeOfData): size query when pData is NULL.
static uint32_t blob_out(Ctx *c, const uint32_t *b, uint32_t n) {
    uint32_t p = ARG(2), ps = ARG(3);
    if (!ps) return D3DERR_INVALIDCALL;
    if (!p) { st(c, ps, 4 * n, 4); return D3D_OK; }
    if (ld(c, ps, 4) < 4 * n) { st(c, ps, 4 * n, 4); return 0x88760868u; }   // D3DERR_MOREDATA
    memcpy(G(p), b, 4u * n);
    st(c, ps, 4 * n, 4);
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_GetVertexShaderDeclaration(Ctx *c) { DEV(); Obj *o = vs_of(ARG(1)); return o ? blob_out(c, o->vsd, o->nvsd) : D3DERR_INVALIDCALL; }
static uint32_t IDirect3DDevice8_GetVertexShaderFunction(Ctx *c) { DEV(); Obj *o = vs_of(ARG(1)); return o && o->orig ? blob_out(c, o->orig, o->norig) : D3DERR_INVALIDCALL; }
// CreatePixelShader(pFunction, pHandle): ps_1_1 .. ps_1_4 tokens, as in Direct3D 9.
static uint32_t IDirect3DDevice8_CreatePixelShader(Ctx *c) {
    DEV();
    if (!ARG(1) || !ARG(2)) return D3DERR_INVALIDCALL;
    const uint32_t *f = (const uint32_t *)G(ARG(1));
    if (f[0] >> 16 != 0xffff || (f[0] & 0xffff) > 0x104) return D3DERR_INVALIDCALL;
    uint32_t k1 = shader_obj(c, I_PS, f, ntok(f));
    if (!k1) return D3DERR_INVALIDCALL;
    if (rn_trace() || getenv("RN_VS")) { rn_log("d3d8 pixel shader %x:", k1); sm1_disasm(objs[k1 - 1].sm, disasm_line); }
    st(c, ARG(2), k1, 4);
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_SetPixelShader(Ctx *c) {
    DEV(); REC(IDirect3DDevice8_SetPixelShader, 2, -1, 0);
    if (ARG(1) && !ps_of(ARG(1))) return D3DERR_INVALIDCALL;
    dev.ps = ARG(1);
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_GetPixelShader(Ctx *c) { DEV(); st(c, ARG(1), dev.ps, 4); return D3D_OK; }
static uint32_t IDirect3DDevice8_DeletePixelShader(Ctx *c) {
    DEV();
    Obj *o = ps_of(ARG(1));
    if (!o) return D3DERR_INVALIDCALL;
    o->dead = 1;
    if (dev.ps == ARG(1)) dev.ps = 0;
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_GetPixelShaderFunction(Ctx *c) { DEV(); Obj *o = ps_of(ARG(1)); return o ? blob_out(c, o->blob, o->nblob) : D3DERR_INVALIDCALL; }
static uint32_t IDirect3DDevice8_SetPixelShaderConstant(Ctx *c) {
    DEV(); REC(IDirect3DDevice8_SetPixelShaderConstant, 4, 2, 16 * ARG(3));
    if (!ARG(2) || ARG(1) + ARG(3) > 8) return D3DERR_INVALIDCALL;
    memcpy(dev.psf[ARG(1)], G(ARG(2)), 16u * ARG(3));
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_GetPixelShaderConstant(Ctx *c) {
    DEV();
    if (!ARG(2) || ARG(1) + ARG(3) > 8) return D3DERR_INVALIDCALL;
    memcpy(G(ARG(2)), dev.psf[ARG(1)], 16u * ARG(3));
    return D3D_OK;
}
// SetStreamSource(StreamNumber, pStreamData, Stride), SetIndices(pIndexData, BaseVertexIndex)
// The device holds a reference to what is bound (d3d8 docs): the game may Release a bound buffer or texture.
static uint32_t bind(Ctx *c, uint32_t *slot, uint32_t p, int iface) {
    Obj *o = p ? obj_of(c, p, iface) : NULL;
    if (p && !o) return D3DERR_INVALIDCALL;
    if (o) o->refs++;
    if (*slot) release_obj(c, &objs[*slot - 1]);
    *slot = o ? idx(o) + 1 : 0;
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_SetStreamSource(Ctx *c) {
    DEV(); REC(IDirect3DDevice8_SetStreamSource, 4, -1, 0);
    if (ARG(1) >= 16) return D3DERR_INVALIDCALL;
    dev.stream[ARG(1)].stride = ARG(3);
    return bind(c, &dev.stream[ARG(1)].vb, ARG(2), I_VB);
}
static uint32_t IDirect3DDevice8_GetStreamSource(Ctx *c) {
    DEV();
    if (ARG(1) >= 16) return D3DERR_INVALIDCALL;
    if (ARG(3)) st(c, ARG(3), dev.stream[ARG(1)].stride, 4);
    if (!dev.stream[ARG(1)].vb) { st(c, ARG(2), 0, 4); return D3D_OK; }
    return ref_out(c, ARG(2), dev.stream[ARG(1)].vb);
}
static uint32_t IDirect3DDevice8_SetIndices(Ctx *c) {
    DEV(); REC(IDirect3DDevice8_SetIndices, 3, -1, 0);
    dev.base_vertex = ARG(2);
    return bind(c, &dev.ib, ARG(1), I_IB);
}
static uint32_t IDirect3DDevice8_GetIndices(Ctx *c) {
    DEV();
    if (ARG(2)) st(c, ARG(2), dev.base_vertex, 4);
    if (!dev.ib) { st(c, ARG(1), 0, 4); return D3D_OK; }
    return ref_out(c, ARG(1), dev.ib);
}
static uint32_t IDirect3DDevice8_SetTexture(Ctx *c) {
    DEV(); REC(IDirect3DDevice8_SetTexture, 3, -1, 0);
    uint32_t s = ARG(1), p = ARG(2);
    if (s >= 8) return D3DERR_INVALIDCALL;
    Obj *o = p ? obj_of(c, p, -1) : NULL;
    if (p && (!o || (o->iface != I_TEXTURE && o->iface != I_CUBE))) return D3DERR_INVALIDCALL;
    return bind(c, &dev.tex[s], p, -1);
}
static uint32_t IDirect3DDevice8_GetTexture(Ctx *c) {
    DEV();
    if (ARG(1) >= 8 || !ARG(2)) return D3DERR_INVALIDCALL;
    if (!dev.tex[ARG(1)]) { st(c, ARG(2), 0, 4); return D3D_OK; }
    return ref_out(c, ARG(2), dev.tex[ARG(1)]);
}

// ---- fixed function: the vs/ps for the current state, cached by key ------------------------------
static uint32_t ffp_cache(Ctx *c, int iface, const void *key, size_t n, uint32_t *(*gen)(const void *, uint32_t *, const char **)) {
    static struct { int iface; uint8_t key[sizeof(FfpVs) > sizeof(FfpPs) ? sizeof(FfpVs) : sizeof(FfpPs)]; uint32_t k1; } cache[1024];
    static int nc;
    for (int k = 0; k < nc; k++) if (cache[k].iface == iface && !memcmp(cache[k].key, key, n)) return cache[k].k1;
    uint32_t nt;
    const char *why = NULL;
    uint32_t *t = gen(key, &nt, &why);
    if (!t) { rn_halt(c, "d3d8: fixed-function draw needs %s", why); return 0; }
    uint32_t k1 = shader_obj(c, iface, t, nt);
    if (k1 && getenv("RN_FFP")) { rn_log("d3d8 fixed-function %s %x:", iface == I_VS ? "vs" : "ps", k1); sm1_disasm(objs[k1 - 1].sm, disasm_line); }
    free(t);
    if (k1 && nc < 1024) { cache[nc].iface = iface; memcpy(cache[nc].key, key, n); cache[nc++].k1 = k1; }
    return k1;
}
static uint32_t *gen_vs(const void *k, uint32_t *n, const char **why) { return ffp_vs(k, n, why); }
static uint32_t *gen_ps(const void *k, uint32_t *n, const char **why) { return ffp_ps(k, n, why); }
static void transpose_rows(const float *m, float (*out)[4], int rows) {     // rows of the transpose: columns of m
    for (int r = 0; r < rows; r++) for (int k = 0; k < 4; k++) out[r][k] = m[4 * k + r];
}
static void color4(float *d, const uint32_t *w) { memcpy(d, w, 16); }
static void xform_dir(const float *m, const float *v, float *o) {   // v (row) * upper 3x3 of m
    for (int j = 0; j < 3; j++) o[j] = v[0] * m[j] + v[1] * m[4 + j] + v[2] * m[8 + j];
}
static void xform_pt(const float *m, const float *v, float *o) {
    for (int j = 0; j < 3; j++) o[j] = v[0] * m[j] + v[1] * m[4 + j] + v[2] * m[8 + j] + m[12 + j];
}
static void unit(float *v) { float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l > 0) { v[0] /= l; v[1] /= l; v[2] /= l; } }
// Builds the fixed-function vs key and its constants for the vertex layout k (from fvf_decl / vsd_decl).
static int ffp_vs_state(Ctx *c, FfpVs *k, float (*cst)[4], int nstages) {
    float *W = dev.xf[256], *V = dev.xf[2], *P = dev.xf[3], wv[16], wvp[16];
    mat_mul(W, V, wv);
    mat_mul(wv, P, wvp);
    memset(cst, 0, 16u * FC_N);
    transpose_rows(wvp, cst + FC_WVP, 4);
    transpose_rows(wv, cst + FC_WV, 4);
    // Normals: the inverse transpose of the world-view 3x3; m3x3 with rows = its columns = the inverse's rows.
    float a = wv[0], b = wv[1], cc = wv[2], d = wv[4], e = wv[5], f = wv[6], g = wv[8], h = wv[9], i = wv[10];
    float det = a * (e * i - f * h) - b * (d * i - f * g) + cc * (d * h - e * g);
    float inv[9] = {(e * i - f * h), -(b * i - cc * h), (b * f - cc * e), -(d * i - f * g), (a * i - cc * g), -(a * f - cc * d),
                    (d * h - e * g), -(a * h - b * g), (a * e - b * d)};
    for (int r = 0; r < 3; r++) for (int q = 0; q < 3; q++) cst[FC_NRM + r][q] = det != 0 ? inv[3 * r + q] / det : 0;
    // n_view = n * (WV^-1)^T: component j = sum_q n_q * inv[j][q] -> rows of inv as the m3x3 rows.
    cst[FC_K][0] = 0; cst[FC_K][1] = .5f; cst[FC_K][2] = 1; cst[FC_K][3] = 2;
    color4(cst[FC_MDIF], dev.material); color4(cst[FC_MAMB], dev.material + 4);
    color4(cst[FC_MSPEC], dev.material + 8); color4(cst[FC_MEMI], dev.material + 12);
    memcpy(&cst[FC_MSPEC][3], &dev.material[16], 4);                     // Power rides in the specular w
    uint32_t amb = dev.rs[139];
    cst[FC_GAMB][0] = (amb >> 16 & 255) / 255.f; cst[FC_GAMB][1] = (amb >> 8 & 255) / 255.f; cst[FC_GAMB][2] = (amb & 255) / 255.f;
    // Pretransformed vertices: screen to clip space for the current viewport.
    float vw = (float)dev.vp.w, vh = (float)dev.vp.h, zr = dev.vp.zmax - dev.vp.zmin;
    cst[FC_PRE][0] = 2 / vw; cst[FC_PRE][1] = -2 / vh;
    cst[FC_PRE][2] = -1 - 2 * (float)dev.vp.x / vw; cst[FC_PRE][3] = 1 + 2 * (float)dev.vp.y / vh;
    cst[FC_PREZ][0] = zr != 0 ? 1 / zr : 1; cst[FC_PREZ][1] = zr != 0 ? -dev.vp.zmin / zr : 0; cst[FC_PREZ][2] = 1;
    k->lights = 0;
    if (k->pos != 2 && dev.rs[137]) {                                   // LIGHTING
        int n = 0;
        for (uint32_t l = 0; l < dev.nlon && n < 8; l++) {             // D3D8 caps at 8 active lights
            const uint32_t *L = dev.light[dev.lon[l]].v;
            float *q = cst[FC_LIGHT + FC_LSTEP * n], fv[3];
            color4(q, L + 1); color4(q + 4, L + 5); color4(q + 8, L + 9);
            memcpy(fv, L + 13, 12); xform_pt(V, fv, q + 12);
            memcpy(fv, L + 16, 12); xform_dir(V, fv, q + 16); unit(q + 16);
            if (L[0] == 3) { q[16] = -q[16]; q[17] = -q[17]; q[18] = -q[18]; }  // directional: toward the light
            memcpy(q + 20, L + 21, 12); memcpy(q + 23, L + 19, 4);            // a0 a1 a2, range
            float th, ph, fo;
            memcpy(&fo, L + 20, 4); memcpy(&th, L + 24, 4); memcpy(&ph, L + 25, 4);
            q[24] = cosf(th / 2); q[25] = cosf(ph / 2); q[26] = fo;
            q[27] = q[24] - q[25] != 0 ? 1 / (q[24] - q[25]) : 1e9f;
            k->kind[n] = L[0] == 1 ? 1 : L[0] == 2 ? 2 : 3;
            k->lights |= (uint8_t)(1u << n);
            n++;
        }
        k->localviewer = dev.rs[142] != 0; k->normalize = dev.rs[143] != 0; k->specular_on = dev.rs[29] != 0;
        int cv = dev.rs[141] != 0;                                      // COLORVERTEX
        uint8_t *src[4] = {&k->src_diffuse, &k->src_specular, &k->src_ambient, &k->src_emissive};
        static const int rsn[4] = {145, 146, 147, 148};
        for (int s = 0; s < 4; s++) {
            uint32_t v = cv ? dev.rs[rsn[s]] : 0;
            if ((v == 1 && !k->diffuse) || (v == 2 && !k->specular)) v = 0;
            *src[s] = (uint8_t)v;
        }
    }
    int lit_any = k->pos != 2 && dev.rs[137];
    if (lit_any && !k->lights) {
        // LIGHTING on with no light enabled: still lit (ambient + emissive); use one light of black.
        k->lights = 1; k->kind[0] = 3;
        memset(cst[FC_LIGHT], 0, 16 * FC_LSTEP);
        k->localviewer = dev.rs[142] != 0; k->normalize = dev.rs[143] != 0; k->specular_on = dev.rs[29] != 0;
    }
    // Fog: vertex fog from the view depth (LINEAR), or the specular alpha of pretransformed vertices.
    if (dev.rs[28]) {
        if (dev.rs[35]) { rn_halt(c, "d3d8: table (pixel) fog mode %u", dev.rs[35]); return 0; }
        if (k->pos == 2) k->fog = 2;
        else if (dev.rs[140] == 3) {
            float s, e;
            memcpy(&s, &dev.rs[36], 4); memcpy(&e, &dev.rs[37], 4);
            k->fog = 1;
            cst[FC_FOG][0] = e != s ? -1 / (e - s) : 0; cst[FC_FOG][1] = e != s ? e / (e - s) : 1;
        } else if (dev.rs[140]) { rn_halt(c, "d3d8: vertex fog mode %u", dev.rs[140]); return 0; }
    }
    k->nstages = (uint8_t)nstages;
    // D3DTSS_TEXTURETRANSFORMFLAGS (24): COUNT1..4, PROJECTED 0x100; D3DTS_TEXTUREn = transform state 16 + n.
    int slot = 0;
    for (int s = 0; s < nstages; s++) {
        uint32_t f = dev.tss[s][24];
        k->tci[s] = dev.tss[s][11];
        if (!f) continue;
        if ((f & ~0x107u) || (f & 7) > 4 || (dev.ps && (f & 0x100))) {
            rn_halt(c, "d3d8: texture transform flags %x on stage %d%s", f, s, dev.ps ? " with a pixel shader" : ""); return 0;
        }
        k->ttff[s] = (uint8_t)((f & 7) | (f & 0x100 ? 0x10 : 0));
        if (slot < FC_TEXMN && (f & 7)) transpose_rows(dev.xf[16 + s], cst + FC_TEXM + 4 * slot++, 4);
    }
    return 1;
}
static void ffp_ps_state(FfpPs *k) {
    memset(k, 0, sizeof *k);
    int n = 0;
    for (; n < 8; n++) {
        const uint32_t *t = dev.tss[n];
        if (t[1] == 1) break;
        FfpStage *s = &k->st[n];
        s->cop = t[1]; s->ca1 = t[2]; s->ca2 = t[3]; s->ca0 = t[26]; s->aop = t[4]; s->aa1 = t[5]; s->aa2 = t[6]; s->aa0 = t[27];
        s->result = t[28];
        s->proj = !dev.ps && (dev.tss[n][24] & 0x100) && (dev.tss[n][24] & 7);    // TTFF PROJECTED (host/ffp.c)
        uint32_t args[6] = {s->ca1, s->ca2, s->ca0, s->aa1, s->aa2, s->aa0};
        for (int a = 0; a < 6; a++) if ((args[a] & 15) == 2) s->tex = 1;
        if (s->cop == 13 || s->aop == 13 || s->cop == 15 || s->aop == 15) s->tex = 1;
    }
    k->n = (uint8_t)n;
    k->specular_add = dev.rs[29] != 0;
}

// ---- draws --------------------------------------------------------------------------------------
static int prim_ok(uint32_t type, uint32_t count) { return type >= 1 && type <= 6 && count && count <= 0xfffff && dev.in_scene; }
static uint32_t verts_of(uint32_t type, uint32_t n) {
    switch (type) { case 1: return n; case 2: return 2 * n; case 3: return n + 1; case 4: return 3 * n; }
    return n + 2;
}
// The shaders, declaration and constants of the draw: the game's (a vs handle / ps handle) or fixed-function ones.
_Static_assert(FC_N == GPU_NVC, "fixed-function constants");
typedef struct { uint32_t vs, ps, decl; uint16_t tex_used; } Bound;
static int bind_draw(Ctx *c, Bound *b, GpuU *u) {
    FfpPs pk;
    ffp_ps_state(&pk);
    Obj *vso = (dev.vs & 1) ? vs_of(dev.vs) : NULL;
    if ((dev.vs & 1) && !vso) { rn_halt(c, "d3d8: draw with deleted vertex shader %x", dev.vs); return 0; }
    if (vso && vso->sm) {                                           // a programmable vertex shader
        b->vs = (uint32_t)(vso - objs) + 1; b->decl = vso->decl;
        if (vso->sm->max_const >= GPU_NVC) { rn_halt(c, "d3d8: vertex shader reads c%d", vso->sm->max_const); return 0; }
        memcpy(u->vc, dev.vsf, 16u * GPU_NVC);
    } else {
        FfpVs vk;
        uint32_t decl;
        if (vso) {                                                  // fixed function with a declaration
            decl = vsd_decl(c, NULL, vso->vsd, 1, &vk);
        } else {
            if (!dev.vs) { rn_halt(c, "d3d8: draw without a vertex shader or FVF"); return 0; }
            decl = fvf_decl(c, dev.vs, &vk);
        }
        if (!decl) return 0;
        // Texture coordinates for the stages the fixed-function ps samples, or all eight for a game pixel shader.
        if (!ffp_vs_state(c, &vk, u->vc, dev.ps ? 8 : pk.n)) return 0;
        b->vs = ffp_cache(c, I_VS, &vk, sizeof vk, gen_vs);
        b->decl = decl;
        if (!b->vs) return 0;
    }
    if (dev.ps) {
        Obj *p = ps_of(dev.ps);
        if (!p) return 0;
        b->ps = dev.ps;
        memcpy(u->pc, dev.psf, sizeof u->pc);
        b->tex_used = p->sm->tex_used;
    } else {
        b->ps = ffp_cache(c, I_PS, &pk, sizeof pk, gen_ps);
        if (!b->ps) return 0;
        uint32_t f = dev.rs[60];
        u->pc[0][0] = (f >> 16 & 255) / 255.f; u->pc[0][1] = (f >> 8 & 255) / 255.f; u->pc[0][2] = (f & 255) / 255.f; u->pc[0][3] = (f >> 24) / 255.f;
        b->tex_used = objs[b->ps - 1].sm->tex_used;
    }
    return 1;
}
// States this renderer does not draw yet stop loudly (each one is a feature to add, not to guess).
static int draw_supported(Ctx *c) {
    const char *why = NULL;
    if (dev.rs[8] != 3) why = "FILLMODE not solid";
    else if (dev.rs[9] != 2 && dev.rs[9] != 1) why = "SHADEMODE";
    else if (dev.rs[52]) why = "stencil test";
    else if (dev.rs[152]) why = "user clip planes";
    else if (dev.rs[47]) why = "ZBIAS";
    if (why) rn_halt(c, "d3d8: draw needs %s", why);
    return !why;
}
// vbase: guest address of vertex 0; ib: guest indices (0 = none) rebased by -vmin; vertices [vmin, vmin + nvert).
static void draw_cmd(Ctx *c, uint32_t type, uint32_t count, uint32_t vbase, uint32_t stride, uint32_t vmin, uint32_t nvert,
                     uint32_t ib, int i32) {
    // The first scene builds geometry and shaders after the last archive/texture checkpoint.
    d3d_load_checkpoint();
    if (!draw_supported(c)) return;
    if (type == 1) { static int once; if (!once++) rn_log("d3d8: point draws are skipped (not drawn yet)"); return; }
    uint32_t uoff;
    GpuU *u = gpu_uni(&uoff);
    Bound b = {0};
    if (!bind_draw(c, &b, u)) return;
    if (b.tex_used >> GPU_STAGES) { rn_halt(c, "d3d8: draw samples texture stages %x", b.tex_used); return; }
    TR("draw %u x%u vs %x ps %x decl %u stride %u", type, count, b.vs, b.ps, b.decl, stride);
    uint32_t nidx = 0, ioff = 0, voff, n = verts_of(type, count);
    if (ib || type == 6) {
        nidx = type == 6 ? 3 * count : n;
        uint32_t *ix = gpu_idx(nidx, &ioff);
        const uint8_t *src = ib ? (const uint8_t *)G(ib) : NULL;
        for (uint32_t k = 0; k < nidx; k++) {
            uint32_t s = type == 6 ? (k % 3 == 0 ? 0 : k / 3 + k % 3) : k, v = s;
            if (src) { if (i32) memcpy(&v, src + 4 * s, 4); else { uint16_t h; memcpy(&h, src + 2 * s, 2); v = h; } }
            ix[k] = v - vmin;
        }
        if (type == 6) type = 4;
    }
    if (!ib) nvert = n;
    memcpy(gpu_vtx(nvert * stride, &voff), G(vbase + vmin * stride), nvert * stride);
    u->hp[0] = 1.f / (float)dev.vp.w; u->hp[1] = 1.f / (float)dev.vp.h;
    uint32_t fc = dev.rs[34];
    u->fogc[0] = (fc >> 16 & 255) / 255.f; u->fogc[1] = (fc >> 8 & 255) / 255.f; u->fogc[2] = (fc & 255) / 255.f;
    u->af[0] = dev.rs[15] ? dev.rs[25] : 0; u->af[1] = dev.rs[28];
    u->ar[0] = (float)(dev.rs[24] & 0xff) / 255.f;
    Obj *rt = obj_k(dev.rt), *ds = obj_k(dev.ds);
    uint32_t *w = gpu_draw();
    w[D_TOPO] = type; w[D_NVERT] = nvert; w[D_NIDX] = nidx; w[D_VOFF] = voff; w[D_IOFF] = ioff; w[D_UOFF] = uoff;
    w[D_STRIDE] = stride; w[D_VS] = b.vs; w[D_PS] = b.ps; w[D_DECL] = b.decl;
    w[D_RT] = rt->gpu; w[D_DS] = ds ? ds->gpu : 0;
    w[D_VP] = dev.vp.x; w[D_VP + 1] = dev.vp.y; w[D_VP + 2] = dev.vp.w; w[D_VP + 3] = dev.vp.h;
    memcpy(&w[D_VP + 4], &dev.vp.zmin, 4); memcpy(&w[D_VP + 5], &dev.vp.zmax, 4);
    w[D_ZEN] = dev.rs[7]; w[D_ZWRITE] = dev.rs[14]; w[D_ZFUNC] = dev.rs[23]; w[D_CULL] = dev.rs[22];
    w[D_BLEND] = dev.rs[27]; w[D_SRCB] = dev.rs[19]; w[D_DSTB] = dev.rs[20]; w[D_BOP] = dev.rs[171] ? dev.rs[171] : 1;
    w[D_CWRITE] = dev.rs[168] ? dev.rs[168] : 0xf;
    for (int k = 0; k < GPU_STAGES; k++) {
        if (!(b.tex_used >> k & 1)) continue;
        uint32_t *t = &w[D_TEX + D_TEXN * k];
        Obj *tx = obj_k(dev.tex[k]);
        const uint32_t *ts = dev.tss[k];
        if (tx && tx->iface == I_CUBE) { rn_halt(c, "d3d8: draw with a cube texture"); return; }
        if (ts[24] && dev.ps) { rn_halt(c, "d3d8: texture transform flags %x with a pixel shader", ts[24]); return; }
        t[T_ID] = !tx ? 0 : (tx->usage & U_RENDERTARGET) ? objs[tx->level0].gpu : dev.tex[k];
        t[T_AU] = ts[13]; t[T_AV] = ts[14]; t[T_MAG] = ts[16]; t[T_MIN] = ts[17];
        t[T_MIP] = tx && tx->levels > 1 ? ts[18] : 0; t[T_ANISO] = ts[21];
    }
    flush_big();
}
static uint32_t stream0(Ctx *c) {
    Obj *vb = obj_k(dev.stream[0].vb);
    for (uint32_t k = 1; k < 16; k++)
        if (dev.stream[k].vb && (dev.vs & 1)) { rn_halt(c, "d3d8: draw with stream %u", k); return 0; }
    return vb ? lock_mem(vb) : 0;
}
// DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount)
static uint32_t IDirect3DDevice8_DrawPrimitive(Ctx *c) {
    DEV();
    if (!prim_ok(ARG(1), ARG(3)) || !dev.stream[0].vb) return D3DERR_INVALIDCALL;
    uint32_t base = stream0(c), stride = dev.stream[0].stride;
    if (base) draw_cmd(c, ARG(1), ARG(3), base + ARG(2) * stride, stride, 0, 0, 0, 0);
    return D3D_OK;
}
// DrawIndexedPrimitive(Type, MinIndex, NumVertices, StartIndex, PrimitiveCount); the base vertex comes from SetIndices.
static uint32_t IDirect3DDevice8_DrawIndexedPrimitive(Ctx *c) {
    DEV();
    if (!prim_ok(ARG(1), ARG(5)) || !dev.stream[0].vb || !dev.ib) return D3DERR_INVALIDCALL;
    Obj *ib = obj_k(dev.ib);
    int i32 = ib->fmt == F_INDEX32;
    uint32_t base = stream0(c), stride = dev.stream[0].stride;
    if (base) {
        uint32_t imem = lock_mem(ib);
        if (!imem) return D3DERR_OUTOFVIDEOMEMORY;   // guest heap exhausted: no index data, never G(0) + offset
        draw_cmd(c, ARG(1), ARG(5), base + dev.base_vertex * stride, stride, ARG(2), ARG(3), imem + ARG(4) * (i32 ? 4 : 2), i32);
    }
    return D3D_OK;
}
// DrawPrimitiveUP(Type, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride): stream 0 is unset after.
static uint32_t IDirect3DDevice8_DrawPrimitiveUP(Ctx *c) {
    DEV();
    if (!prim_ok(ARG(1), ARG(2)) || !ARG(3) || !ARG(4)) return D3DERR_INVALIDCALL;
    draw_cmd(c, ARG(1), ARG(2), ARG(3), ARG(4), 0, 0, 0, 0);
    dev.stream[0].vb = 0; dev.stream[0].stride = 0;
    return D3D_OK;
}
// DrawIndexedPrimitiveUP(Type, MinVertexIndex, NumVertexIndices, PrimitiveCount, pIndexData, IndexDataFormat,
// pVertexStreamZeroData, VertexStreamZeroStride)
static uint32_t IDirect3DDevice8_DrawIndexedPrimitiveUP(Ctx *c) {
    DEV();
    if (!prim_ok(ARG(1), ARG(4)) || !ARG(5) || !ARG(7) || !ARG(8) || (ARG(6) != F_INDEX16 && ARG(6) != F_INDEX32))
        return D3DERR_INVALIDCALL;
    draw_cmd(c, ARG(1), ARG(4), ARG(7), ARG(8), ARG(2), ARG(3), ARG(5), ARG(6) == F_INDEX32);
    dev.stream[0].vb = 0; dev.stream[0].stride = 0; dev.ib = 0;
    return D3D_OK;
}

// ---- state blocks: DWORD tokens ---------------------------------------------------------------------
static uint32_t IDirect3DDevice8_BeginStateBlock(Ctx *c) {
    DEV();
    if (dev.recording) return D3DERR_INVALIDCALL;
    dev.recording = 1;
    dev.nrec = 0;
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_EndStateBlock(Ctx *c) {
    DEV();
    if (!dev.recording || !ARG(1)) return D3DERR_INVALIDCALL;
    dev.recording = 0;
    new_obj(c, I_SB);
    Obj *o = last;
    o->rec = malloc(sizeof(Rec) * (dev.nrec ? dev.nrec : 1));
    memcpy(o->rec, dev.rec, sizeof(Rec) * dev.nrec);
    o->nrec = dev.nrec;
    st(c, ARG(1), idx(o) + 1, 4);
    return D3D_OK;
}
static Obj *sb_of(Ctx *c, uint32_t t) {
    if (t && t <= (uint32_t)nobjs && objs[t - 1].iface == I_SB && !objs[t - 1].dead) return &objs[t - 1];
    rn_halt(c, "d3d8: state block token %u", t);
    return NULL;
}
static uint32_t IDirect3DDevice8_ApplyStateBlock(Ctx *c) {
    DEV();
    Obj *o = sb_of(c, ARG(1));
    if (!o || dev.recording) return D3DERR_INVALIDCALL;
    uint32_t sp = c->r[ESP];
    for (uint32_t k = 0; k < o->nrec && !c->halt; k++) {
        Rec *r = &o->rec[k];
        c->r[ESP] = sp - 64;
        for (uint32_t i = 0; i < r->n; i++) st(c, c->r[ESP] + 4 + 4 * i, r->a[i], 4);
        r->fn(c);
    }
    c->r[ESP] = sp;
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_CaptureStateBlock(Ctx *c) {
    DEV();
    Obj *o = sb_of(c, ARG(1));
    if (!o) return D3DERR_INVALIDCALL;
    for (uint32_t k = 0; k < o->nrec; k++) {
        Rec *r = &o->rec[k];
        uint32_t *a = r->a, d = r->pidx >= 0 ? a[r->pidx] : 0;
        if (r->fn == IDirect3DDevice8_SetRenderState) a[2] = dev.rs[a[1]];
        else if (r->fn == IDirect3DDevice8_SetTextureStageState) a[3] = dev.tss[a[1]][a[2]];
        else if (r->fn == IDirect3DDevice8_SetTransform) memcpy(G(d), dev.xf[a[1]], 64);
        else if (r->fn == IDirect3DDevice8_SetViewport) memcpy(G(d), &dev.vp, 24);
        else if (r->fn == IDirect3DDevice8_SetVertexShader) a[1] = dev.vs;
        else if (r->fn == IDirect3DDevice8_SetPixelShader) a[1] = dev.ps;
        else if (r->fn == IDirect3DDevice8_SetMaterial) memcpy(G(d), dev.material, 68);
        else if (r->fn == IDirect3DDevice8_SetLight || r->fn == IDirect3DDevice8_LightEnable) {
            int s = light_slot(c, a[1], 0);
            if (s < 0) continue;                                        // never defined: nothing to capture
            if (r->fn == IDirect3DDevice8_SetLight) memcpy(G(d), dev.light[s].v, 104); else a[2] = dev.light[s].on;
        }
        else if (r->fn == IDirect3DDevice8_SetClipPlane) memcpy(G(d), dev.clip[a[1]], 16);
        else if (r->fn == IDirect3DDevice8_SetVertexShaderConstant) memcpy(G(d), dev.vsf[a[1]], r->bytes);
        else if (r->fn == IDirect3DDevice8_SetPixelShaderConstant) memcpy(G(d), dev.psf[a[1]], r->bytes);
        else { rn_halt(c, "d3d8: CaptureStateBlock of a call it cannot read back"); return D3DERR_INVALIDCALL; }
    }
    return D3D_OK;
}
static uint32_t IDirect3DDevice8_DeleteStateBlock(Ctx *c) { DEV(); Obj *o = sb_of(c, ARG(1)); if (!o) return D3DERR_INVALIDCALL; o->dead = 1; return D3D_OK; }

#define DEV_ENTRY(name, n) {"d3d8.dll", "IDirect3DDevice8::" #name, n, IDirect3DDevice8_##name}
#define D3D_ENTRY(name, n) {"d3d8.dll", "IDirect3D8::" #name, n, IDirect3D8_##name}
#define UNK_ENTRIES(iface) {"d3d8.dll", iface "::QueryInterface", 3, unk_QueryInterface}, \
    {"d3d8.dll", iface "::AddRef", 1, unk_AddRef}, {"d3d8.dll", iface "::Release", 1, unk_Release}, \
    {"d3d8.dll", iface "::GetDevice", 2, res_GetDevice}
const Api d3d8_apis[] = {
    {"d3d8.dll", "Direct3DCreate8", 1, Direct3DCreate8_},
    UNK_ENTRIES("IDirect3D8"), UNK_ENTRIES("IDirect3DDevice8"), UNK_ENTRIES("IDirect3DSurface8"),
    UNK_ENTRIES("IDirect3DTexture8"), UNK_ENTRIES("IDirect3DCubeTexture8"), UNK_ENTRIES("IDirect3DVertexBuffer8"),
    UNK_ENTRIES("IDirect3DIndexBuffer8"), UNK_ENTRIES("IDirect3DSwapChain8"),
    D3D_ENTRY(GetAdapterCount, 1), D3D_ENTRY(GetAdapterIdentifier, 4), D3D_ENTRY(GetAdapterModeCount, 2),
    D3D_ENTRY(EnumAdapterModes, 4), D3D_ENTRY(GetAdapterDisplayMode, 3), D3D_ENTRY(CheckDeviceType, 6),
    D3D_ENTRY(CheckDeviceFormat, 7), D3D_ENTRY(CheckDeviceMultiSampleType, 6), D3D_ENTRY(CheckDepthStencilMatch, 6),
    D3D_ENTRY(GetDeviceCaps, 4), D3D_ENTRY(GetAdapterMonitor, 2), D3D_ENTRY(CreateDevice, 7),
    DEV_ENTRY(TestCooperativeLevel, 1), DEV_ENTRY(GetAvailableTextureMem, 1), DEV_ENTRY(ResourceManagerDiscardBytes, 2),
    DEV_ENTRY(GetDirect3D, 2), DEV_ENTRY(GetDeviceCaps, 2), DEV_ENTRY(GetDisplayMode, 2), DEV_ENTRY(GetCreationParameters, 2),
    DEV_ENTRY(ShowCursor, 2), DEV_ENTRY(SetCursorPosition, 4), DEV_ENTRY(Present, 5), DEV_ENTRY(GetBackBuffer, 4),
    DEV_ENTRY(SetGammaRamp, 3), DEV_ENTRY(GetGammaRamp, 2), DEV_ENTRY(CreateTexture, 8), DEV_ENTRY(CreateVertexBuffer, 6),
    DEV_ENTRY(CreateIndexBuffer, 6), DEV_ENTRY(CreateRenderTarget, 7), DEV_ENTRY(CreateDepthStencilSurface, 6),
    DEV_ENTRY(CreateImageSurface, 5), DEV_ENTRY(CopyRects, 6), DEV_ENTRY(UpdateTexture, 3), DEV_ENTRY(GetFrontBuffer, 2), DEV_ENTRY(SetRenderTarget, 3),
    DEV_ENTRY(GetRenderTarget, 2), DEV_ENTRY(GetDepthStencilSurface, 2), DEV_ENTRY(BeginScene, 1), DEV_ENTRY(EndScene, 1),
    DEV_ENTRY(Clear, 7), DEV_ENTRY(SetTransform, 3), DEV_ENTRY(GetTransform, 3), DEV_ENTRY(MultiplyTransform, 3),
    DEV_ENTRY(SetViewport, 2), DEV_ENTRY(GetViewport, 2), DEV_ENTRY(SetMaterial, 2), DEV_ENTRY(GetMaterial, 2),
    DEV_ENTRY(SetLight, 3), DEV_ENTRY(GetLight, 3), DEV_ENTRY(LightEnable, 3), DEV_ENTRY(GetLightEnable, 3),
    DEV_ENTRY(SetClipPlane, 3), DEV_ENTRY(GetClipPlane, 3), DEV_ENTRY(SetRenderState, 3), DEV_ENTRY(GetRenderState, 3),
    DEV_ENTRY(BeginStateBlock, 1), DEV_ENTRY(EndStateBlock, 2), DEV_ENTRY(ApplyStateBlock, 2),
    DEV_ENTRY(CaptureStateBlock, 2), DEV_ENTRY(DeleteStateBlock, 2), DEV_ENTRY(GetTexture, 3), DEV_ENTRY(SetTexture, 3),
    DEV_ENTRY(GetTextureStageState, 4), DEV_ENTRY(SetTextureStageState, 4), DEV_ENTRY(ValidateDevice, 2),
    DEV_ENTRY(DrawPrimitive, 4), DEV_ENTRY(DrawIndexedPrimitive, 6), DEV_ENTRY(DrawPrimitiveUP, 5),
    DEV_ENTRY(DrawIndexedPrimitiveUP, 9), DEV_ENTRY(CreateVertexShader, 5), DEV_ENTRY(SetVertexShader, 2),
    DEV_ENTRY(GetVertexShader, 2), DEV_ENTRY(DeleteVertexShader, 2), DEV_ENTRY(SetVertexShaderConstant, 4),
    DEV_ENTRY(GetVertexShaderConstant, 4), DEV_ENTRY(GetVertexShaderDeclaration, 4), DEV_ENTRY(GetVertexShaderFunction, 4),
    DEV_ENTRY(SetStreamSource, 4), DEV_ENTRY(GetStreamSource, 4), DEV_ENTRY(SetIndices, 3), DEV_ENTRY(GetIndices, 3),
    DEV_ENTRY(CreatePixelShader, 3), DEV_ENTRY(SetPixelShader, 2), DEV_ENTRY(GetPixelShader, 2),
    DEV_ENTRY(DeletePixelShader, 2), DEV_ENTRY(SetPixelShaderConstant, 4), DEV_ENTRY(GetPixelShaderConstant, 4),
    DEV_ENTRY(GetPixelShaderFunction, 4),
    {"d3d8.dll", "IDirect3DSurface8::GetDesc", 2, IDirect3DSurface8_GetDesc},
    {"d3d8.dll", "IDirect3DSurface8::LockRect", 4, IDirect3DSurface8_LockRect},
    {"d3d8.dll", "IDirect3DSurface8::UnlockRect", 1, IDirect3DSurface8_UnlockRect},
    {"d3d8.dll", "IDirect3DSurface8::GetContainer", 3, IDirect3DSurface8_GetContainer},
    {"d3d8.dll", "IDirect3DTexture8::GetLevelCount", 1, IDirect3DTexture8_GetLevelCount},
    {"d3d8.dll", "IDirect3DTexture8::GetLevelDesc", 3, IDirect3DTexture8_GetLevelDesc},
    {"d3d8.dll", "IDirect3DTexture8::GetSurfaceLevel", 3, IDirect3DTexture8_GetSurfaceLevel},
    {"d3d8.dll", "IDirect3DTexture8::LockRect", 5, IDirect3DTexture8_LockRect},
    {"d3d8.dll", "IDirect3DTexture8::UnlockRect", 2, IDirect3DTexture8_UnlockRect},
    {"d3d8.dll", "IDirect3DTexture8::AddDirtyRect", 2, IDirect3DTexture8_AddDirtyRect},
    {"d3d8.dll", "IDirect3DTexture8::GetType", 1, IDirect3DTexture8_GetType},
    {"d3d8.dll", "IDirect3DTexture8::PreLoad", 1, IDirect3DTexture8_PreLoad},
    {"d3d8.dll", "IDirect3DTexture8::SetPriority", 2, IDirect3DTexture8_SetPriority},
    {"d3d8.dll", "IDirect3DTexture8::SetLOD", 2, IDirect3DTexture8_SetLOD},
    {"d3d8.dll", "IDirect3DVertexBuffer8::Lock", 5, IDirect3DVertexBuffer8_Lock},
    {"d3d8.dll", "IDirect3DVertexBuffer8::Unlock", 1, IDirect3DVertexBuffer8_Unlock},
    {"d3d8.dll", "IDirect3DVertexBuffer8::GetDesc", 2, IDirect3DVertexBuffer8_GetDesc},
    {"d3d8.dll", "IDirect3DVertexBuffer8::PreLoad", 1, IDirect3DVertexBuffer8_PreLoad},
    {"d3d8.dll", "IDirect3DIndexBuffer8::Lock", 5, IDirect3DIndexBuffer8_Lock},
    {"d3d8.dll", "IDirect3DIndexBuffer8::Unlock", 1, IDirect3DIndexBuffer8_Unlock},
    {"d3d8.dll", "IDirect3DIndexBuffer8::GetDesc", 2, IDirect3DIndexBuffer8_GetDesc},
    {"d3d8.dll", "IDirect3DIndexBuffer8::PreLoad", 1, IDirect3DIndexBuffer8_PreLoad},
};
const int d3d8_api_count = sizeof d3d8_apis / sizeof d3d8_apis[0];
