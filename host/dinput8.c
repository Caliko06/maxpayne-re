// dinput8: DirectInput8Create and the keyboard and mouse devices, as documented by Microsoft (DirectX 9).
// The PC has one keyboard and one mouse, no game controllers. host/input.c delivers their events.
#include "host.h"

#define DI_OK 0u
#define DIERR_INVALIDPARAM 0x80070057u
#define DIERR_NOTACQUIRED 0x8007000cu
#define DIERR_DEVICENOTREG 0x80040154u
#define DIERR_NOINTERFACE 0x80004002u

#define TR(...) do { if (rn_trace()) rn_log("dinput8 " __VA_ARGS__); } while (0)

enum { I_DI8, I_DEV, I_N };
static const char *const m_di8[] = {"QueryInterface", "AddRef", "Release", "CreateDevice", "EnumDevices",
    "GetDeviceStatus", "RunControlPanel", "Initialize", "FindDevice", "EnumDevicesBySemantics", "ConfigureDevices", 0};
static const char *const m_dev[] = {"QueryInterface", "AddRef", "Release", "GetCapabilities", "EnumObjects",
    "GetProperty", "SetProperty", "Acquire", "Unacquire", "GetDeviceState", "GetDeviceData", "SetDataFormat",
    "SetEventNotification", "SetCooperativeLevel", "GetObjectInfo", "GetDeviceInfo", "RunControlPanel", "Initialize",
    "CreateEffect", "EnumEffects", "GetEffectInfo", "GetForceFeedbackState", "SendForceFeedbackCommand",
    "EnumCreatedEffectObjects", "Escape", "Poll", "SendDeviceData", "EnumEffectsInFile", "WriteEffectToFile",
    "BuildActionMap", "SetActionMap", "GetImageInfo", 0};
static const char *const iface_name[I_N] = {"IDirectInput8A", "IDirectInputDevice8A"};
static const char *const *const iface_methods[I_N] = {m_di8, m_dev};
static uint32_t vtables[I_N];

static uint32_t vtable(int i) {
    if (vtables[i]) return vtables[i];
    int n = 0;
    while (iface_methods[i][n]) n++;
    uint32_t v = guest_alloc(4u * n);
    for (int k = 0; k < n; k++) {
        char name[96];
        snprintf(name, sizeof name, "%s::%s", iface_name[i], iface_methods[i][k]);
        st(g_ctx, v + 4u * k, host_addr_of("dinput8.dll", name), 4);
    }
    return vtables[i] = v;
}

// The two devices: GUID_SysKeyboard {6F1D2B61-...} and GUID_SysMouse {6F1D2B60-...}.
enum { KEYBOARD, MOUSE };
// What GetDeviceState reports: each object of the data format (or each action of the action map)
// at its offset. what: 0-2 = X/Y/Z axis, 0x100 + n = button n, 0x200 + DIK = key.
typedef struct { uint32_t ofs, what, size, app; } Field;
typedef struct { uint32_t ofs, data, time, seq, app; } Event;
#define MAXF 256
#define MAXEV 256
typedef struct {
    int iface, kind;
    uint32_t refs, guest;
    uint32_t data_size, coop, hwnd, buffer;
    int acquired, format_set, actions, overflow, di7;     // di7: made through DirectInputCreateA
    int nf;
    Field f[MAXF];
    int32_t axis[3];               // relative motion since the last GetDeviceState
    uint8_t buttons[8];
    unsigned ev_head, ev_tail;
    Event ev[MAXEV];
} Obj;
static Obj objs[16];
static int nobjs;
static const uint8_t guid_tail[12] = {0xa0, 0xd5, 0xcf, 0x11, 0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00};

static uint32_t new_obj(Ctx *c, int iface, int kind) {
    if (nobjs == 16) { rn_halt(c, "dinput8: too many objects"); return 0; }
    Obj *o = &objs[nobjs];
    memset(o, 0, sizeof *o);
    o->iface = iface; o->kind = kind; o->refs = 1;
    o->guest = guest_alloc(16);
    st(c, o->guest, vtable(iface), 4);
    st(c, o->guest + 4, (uint32_t)nobjs + 1, 4);
    nobjs++;
    return o->guest;
}
static Obj *obj_of(Ctx *c, uint32_t p, int iface) {
    uint32_t k = p ? ld(c, p + 4, 4) - 1 : ~0u;
    if (k < (uint32_t)nobjs && objs[k].guest == p && objs[k].iface == iface) return &objs[k];
    rn_halt(c, "dinput8: %08x is not a live %s", p, iface_name[iface]);
    return NULL;
}
#define SELF(iface) Obj *o = obj_of(c, ARG(0), iface); if (!o) return DIERR_INVALIDPARAM

static uint32_t QueryInterface_(Ctx *c) {
    uint32_t g = ARG(1);
    rn_halt(c, "dinput8: QueryInterface for {%08x-%04x-%04x-...}", ld(c, g, 4), ld(c, g + 4, 2), ld(c, g + 6, 2));
    return DIERR_NOINTERFACE;
}
static Obj *any_obj(Ctx *c) {
    uint32_t k = ld(c, ARG(0) + 4, 4) - 1;
    if (k < (uint32_t)nobjs && objs[k].guest == ARG(0)) return &objs[k];
    rn_halt(c, "dinput8: %08x is not a live object", ARG(0));
    return NULL;
}
static uint32_t AddRef_(Ctx *c) { Obj *o = any_obj(c); return o ? ++o->refs : 0; }
static uint32_t Release_(Ctx *c) { Obj *o = any_obj(c); return o && o->refs ? --o->refs : 0; }

// DirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter): IID_IDirectInput8A {BF798030-483A-4DA2-...}.
static uint32_t DirectInput8Create_(Ctx *c) {
    uint32_t ver = ARG(1), iid = ARG(2), out = ARG(3);
    TR("DirectInput8Create(version %x)", ver);
    if (!out || ARG(4)) return DIERR_INVALIDPARAM;
    if (ld(c, iid, 4) != 0xbf798030u) { rn_halt(c, "DirectInput8Create for {%08x-...}", ld(c, iid, 4)); return DIERR_NOINTERFACE; }
    if (ver < 0x0800) return 0x8007047eu;                 // DIERR_OLDDIRECTINPUTVERSION
    st(c, out, new_obj(c, I_DI8, 0), 4);
    return DI_OK;
}
// DirectInputCreateA(hinst, dwVersion, ppDI, punkOuter): DirectInput 7 (Max Payne asks for 0x700, exe 0x51a6c0).
// IDirectInputA and IDirectInputDeviceA have the base methods of the DirectInput 8 interfaces in the same vtable order,
// so the same objects serve; GetDeviceData takes the 16-byte DIDEVICEOBJECTDATA of DirectInput 7.
static uint32_t DirectInputCreateA_(Ctx *c) {
    TR("DirectInputCreateA(version %x)", ARG(1));
    if (!ARG(2) || ARG(3)) return DIERR_INVALIDPARAM;
    if (ARG(1) > 0x0700) return 0x80070481u;              // DIERR_BETADIRECTINPUTVERSION
    st(c, ARG(2), new_obj(c, I_DI8, 0), 4);
    objs[nobjs - 1].di7 = 1;
    return DI_OK;
}
static int device_of_guid(Ctx *c, uint32_t g) {
    if (memcmp(G(g + 4), guid_tail, 12)) return -1;
    uint32_t d = ld(c, g, 4);
    return d == 0x6f1d2b61u ? KEYBOARD : d == 0x6f1d2b60u ? MOUSE : -1;
}
static uint32_t IDirectInput8A_CreateDevice(Ctx *c) {
    SELF(I_DI8);
    uint32_t g = ARG(1), out = ARG(2);
    int kind = device_of_guid(c, g);
    TR("CreateDevice({%08x-...}) -> %s", ld(c, g, 4), kind == KEYBOARD ? "keyboard" : kind == MOUSE ? "mouse" : "none");
    if (!out || ARG(3)) return DIERR_INVALIDPARAM;
    if (kind < 0) { st(c, out, 0, 4); return DIERR_DEVICENOTREG; }
    st(c, out, new_obj(c, I_DEV, kind), 4);
    objs[nobjs - 1].di7 = o->di7;
    return DI_OK;
}
// GetProperty(rguidProp, pdiph): DIPROPHEADER {dwSize, dwHeaderSize, dwObj, dwHow} then the value. DIPROP_BUFFERSIZE (1)
// is the device's; DIPROP_GRANULARITY (3) is 1 for x/y and WHEEL_DELTA (120) for the wheel (DIMOFS_Z = offset 8).
static uint32_t IDirectInputDevice8A_GetProperty(Ctx *c) {
    SELF(I_DEV);
    uint32_t prop = ARG(1), p = ARG(2);
    if (!p || ld(c, p + 4, 4) != 16) return DIERR_INVALIDPARAM;
    uint32_t obj = ld(c, p + 8, 4), how = ld(c, p + 12, 4);
    switch (prop) {
    case 1: st(c, p + 16, o->buffer, 4); return DI_OK;
    case 3:
        if (o->kind != MOUSE || how != 1) return 0x80070002u;          // DIERR_NOTFOUND
        st(c, p + 16, obj == 8 ? 120 : 1, 4);
        return DI_OK;
    }
    rn_halt(c, "dinput8: GetProperty(%u)", prop);
    return DIERR_INVALIDPARAM;
}
// GetCapabilities(DIDEVCAPS*): dwSize 24 (DirectX 3) or 44; attached; device type and subtype as the creating
// interface's version defines them (DirectInput 7: DIDEVTYPE_KEYBOARD 3 / PCENH 4, DIDEVTYPE_MOUSE 2 / TRADITIONAL 2;
// DirectInput 8: DI8DEVTYPE_KEYBOARD 0x13 / PCENH 4, DI8DEVTYPE_MOUSE 0x12 / TRADITIONAL 2).
static uint32_t IDirectInputDevice8A_GetCapabilities(Ctx *c) {
    SELF(I_DEV);
    uint32_t p = ARG(1), size = p ? ld(c, p, 4) : 0;
    if (size != 24 && size != 44) return DIERR_INVALIDPARAM;
    memset(G(p + 4), 0, size - 4);
    int kb = o->kind == KEYBOARD;
    uint32_t type = kb ? (o->di7 ? 3u : 0x13u) | 4u << 8 : (o->di7 ? 2u : 0x12u) | 2u << 8;
    st(c, p + 4, 1, 4);                               // DIDC_ATTACHED
    st(c, p + 8, type, 4);
    st(c, p + 12, kb ? 0 : 3, 4);                     // axes: x, y, wheel
    st(c, p + 16, kb ? 101 : 5, 4);                   // buttons: the keys of a 101-key keyboard, 5 mouse buttons (exe 0x518c40 limits its button indexes by this)
    return DI_OK;
}

// DIDATAFORMAT {dwSize, dwObjSize, dwFlags, dwDataSize, dwNumObjs, rgodf}; DIOBJECTDATAFORMAT {pguid, dwOfs,
// dwType, dwFlags}. An object names a guid (GUID_XAxis.. {A36D02E0..E2-...}, GUID_Button {A36D02F0-...},
// GUID_Key {55728220-...}) or none, and a DIDFT_ type with an instance or DIDFT_ANYINSTANCE (the next free one).
static uint32_t IDirectInputDevice8A_SetDataFormat(Ctx *c) {
    SELF(I_DEV);
    uint32_t f = ARG(1);
    if (!f || ld(c, f, 4) != 24 || ld(c, f + 4, 4) != 16) return DIERR_INVALIDPARAM;
    if (o->acquired) return 0x80070005u;                  // DIERR_ACQUIRED
    uint32_t n = ld(c, f + 16, 4), objs_p = ld(c, f + 20, 4);
    if (n > MAXF) { rn_halt(c, "dinput8: data format with %u objects", n); return DIERR_INVALIDPARAM; }
    o->data_size = ld(c, f + 12, 4);
    o->nf = 0;
    o->actions = 0;
    uint8_t used[3 + 8 + 256] = {0};
    for (uint32_t k = 0; k < n; k++) {
        uint32_t od = objs_p + 16 * k, g = ld(c, od, 4), ofs = ld(c, od + 4, 4), type = ld(c, od + 8, 4);
        uint32_t inst = (type >> 8) & 0xffff, any = inst == 0xffff, gd = g ? ld(c, g, 4) : 0;
        int axis = (type & 3) != 0, button = (type & 0xc) != 0;
        uint32_t what = ~0u;
        if (o->kind == MOUSE && axis && (!g || (gd >= 0xa36d02e0u && gd <= 0xa36d02e2u))) {
            uint32_t a = g ? gd - 0xa36d02e0u : inst;
            if (any && !g) for (a = 0; a < 3 && used[a]; a++) {}
            if (a < 3 && !used[a]) { used[a] = 1; what = a; }
        } else if (o->kind == MOUSE && button && (!g || gd == 0xa36d02f0u)) {
            uint32_t b = inst;
            if (any) for (b = 0; b < 8 && used[3 + b]; b++) {}
            if (b < 8 && !used[3 + b]) { used[3 + b] = 1; what = 0x100 + b; }
        } else if (o->kind == KEYBOARD && button && (!g || gd == 0x55728220u)) {
            uint32_t d = inst;
            if (any) for (d = 0; d < 256 && used[11 + d]; d++) {}
            if (d < 256 && !used[11 + d]) { used[11 + d] = 1; what = 0x200 + d; }
        }
        if (what == ~0u) {
            if (ld(c, od + 12, 4) & 0x80000000u || (type & 0x80000000u)) continue;   // DIDOI_ / DIDFT_OPTIONAL
            return DIERR_INVALIDPARAM;                    // the device has no such object
        }
        o->f[o->nf++] = (Field){ofs, what, axis ? 4u : 1u, 0};
    }
    o->format_set = 1;
    TR("SetDataFormat(%s): %u bytes, %u objects", o->kind == KEYBOARD ? "keyboard" : "mouse", o->data_size, ld(c, f + 16, 4));
    return DI_OK;
}
static uint32_t IDirectInputDevice8A_SetCooperativeLevel(Ctx *c) {
    SELF(I_DEV);
    uint32_t flags = ARG(2);
    // Exactly one of EXCLUSIVE (1) / NONEXCLUSIVE (2) and one of FOREGROUND (4) / BACKGROUND (8).
    if (((flags & 3) != 1 && (flags & 3) != 2) || ((flags & 12) != 4 && (flags & 12) != 8)) return DIERR_INVALIDPARAM;
    if (o->kind == KEYBOARD && (flags & 9) == 9) return DIERR_INVALIDPARAM;   // exclusive background keyboard
    o->hwnd = ARG(1);
    o->coop = flags;
    return DI_OK;
}
void input_exclusive(int on);                          // host/input.c
static void mouse_hold(void) {                         // the mouse is held exclusively while an exclusive mouse device is acquired
    int on = 0;
    for (int k = 0; k < nobjs; k++) if (objs[k].iface == I_DEV && objs[k].kind == MOUSE && objs[k].acquired && (objs[k].coop & 1)) on = 1;
    input_exclusive(on);
}
static uint32_t IDirectInputDevice8A_Acquire(Ctx *c) {
    SELF(I_DEV);
    if (!o->format_set) return DIERR_INVALIDPARAM;
    if (o->acquired) return 1;                            // S_FALSE: already acquired
    o->acquired = 1;
    o->ev_head = o->ev_tail = 0;
    o->overflow = 0;
    memset(o->axis, 0, sizeof o->axis);
    mouse_hold();
    return DI_OK;
}
static uint32_t IDirectInputDevice8A_Unacquire(Ctx *c) {
    SELF(I_DEV);
    if (!o->acquired) return 1;
    o->acquired = 0;
    mouse_hold();
    return DI_OK;
}
static uint32_t IDirectInputDevice8A_GetDeviceState(Ctx *c) {
    SELF(I_DEV);
    uint32_t n = ARG(1), p = ARG(2);
    input_poll();
    jump_poll(c);                                         // RN_JUMP (host/jump.c), test only
    if (!o->acquired) return DIERR_NOTACQUIRED;
    if (!p || n != o->data_size) return DIERR_INVALIDPARAM;
    memset(G(p), 0, n);
    for (int k = 0; k < o->nf; k++) {
        Field *f = &o->f[k];
        uint32_t v = f->what < 3 ? (uint32_t)o->axis[f->what] : f->what < 0x200 ? o->buttons[f->what - 0x100] :
                     input_key_down(f->what - 0x200) ? 0x80 : 0;
        if (f->ofs + f->size <= n) st(c, p + f->ofs, v, (int)f->size);
    }
    memset(o->axis, 0, sizeof o->axis);
    return DI_OK;
}
// GetDeviceData(cbObjectData, rgdod, pdwInOut, dwFlags): DIDEVICEOBJECTDATA is 20 bytes (16 for DirectX 3
// applications: no uAppData). rgdod NULL with *pdwInOut = INFINITE flushes. DIGDD_PEEK (1) keeps the events.
static uint32_t IDirectInputDevice8A_GetDeviceData(Ctx *c) {
    SELF(I_DEV);
    uint32_t sz = ARG(1), out = ARG(2), io = ARG(3), flags = ARG(4);
    input_poll();
    if (!o->acquired) return DIERR_NOTACQUIRED;
    if (!io || (sz != 20 && sz != 16)) return DIERR_INVALIDPARAM;
    if (!o->buffer) return 0x80040201u;                   // DIERR_NOTBUFFERED
    uint32_t want = ld(c, io, 4), have = o->ev_tail - o->ev_head, n = want < have ? want : have, start_head = o->ev_head;
    if (have) TR("GetDeviceData(%s, size %u, out %08x, want %u, flags %x): %u queued", o->kind == KEYBOARD ? "keyboard" : "mouse", sz, out, want, flags, have);
    if (out)
        for (uint32_t k = 0; k < n; k++) {
            Event *e = &o->ev[(o->ev_head + k) % MAXEV];
            uint32_t d = out + sz * k;
            st(c, d, e->ofs, 4); st(c, d + 4, e->data, 4); st(c, d + 8, e->time, 4); st(c, d + 12, e->seq, 4);
            if (sz == 20) st(c, d + 16, e->app, 4);
        }
    if (!(flags & 1)) o->ev_head += n;
    st(c, io, n, 4);
    if (n) TR("GetDeviceData(%s): %u events, first ofs %u data %x app %x", o->kind == KEYBOARD ? "keyboard" : "mouse", n,
              o->ev[start_head % MAXEV].ofs, o->ev[start_head % MAXEV].data, o->ev[start_head % MAXEV].app);
    uint32_t r = o->overflow ? 1u : DI_OK;                // DI_BUFFEROVERFLOW
    if (!(flags & 1)) o->overflow = 0;
    return r;
}
// SetProperty(rguidProp, pdiph): rguidProp is MAKEDIPROP(n). DIPROPDWORD {DIPROPHEADER(16), dwData}.
static uint32_t IDirectInputDevice8A_SetProperty(Ctx *c) {
    SELF(I_DEV);
    uint32_t prop = ARG(1), h = ARG(2);
    if (!h) return DIERR_INVALIDPARAM;
    if (prop == 1) {                                      // DIPROP_BUFFERSIZE
        if (o->acquired) return 0x80070005u;
        o->buffer = ld(c, h + 16, 4);
        TR("SetProperty(BUFFERSIZE, %u)", o->buffer);
        return DI_OK;
    }
    rn_halt(c, "dinput8: SetProperty(%u)", prop);
    return DIERR_INVALIDPARAM;
}

// DIDEVICEINSTANCEA (580 bytes): dwSize, guidInstance, guidProduct, dwDevType, tszInstanceName[260],
// tszProductName[260], guidFFDriver, wUsagePage, wUsage. Windows XP names the system devices like this.
static void put_instance(Ctx *c, uint32_t p, int kind) {
    memset(G(p + 4), 0, 576);
    for (int k = 0; k < 2; k++) {
        st(c, p + 4 + 16 * k, kind == KEYBOARD ? 0x6f1d2b61u : 0x6f1d2b60u, 4);
        memcpy(G(p + 8 + 16 * k), guid_tail, 12);
    }
    st(c, p + 36, kind == KEYBOARD ? 0x0413 : 0x0212, 4);  // KEYBOARD/PCENH, MOUSE/TRADITIONAL
    const char *name = kind == KEYBOARD ? "Keyboard" : "Mouse";
    memcpy(G(p + 40), name, strlen(name) + 1);
    memcpy(G(p + 300), name, strlen(name) + 1);
    st(c, p + 576, 1, 2);                                    // HID generic desktop
    st(c, p + 578, kind == KEYBOARD ? 6 : 2, 2);
}
static uint32_t IDirectInputDevice8A_GetDeviceInfo(Ctx *c) {
    SELF(I_DEV);
    uint32_t p = ARG(1);
    if (!p || ld(c, p, 4) != 580) return DIERR_INVALIDPARAM;
    put_instance(c, p, o->kind);
    return DI_OK;
}
// DIDEVICEOBJECTINSTANCEA is 316 bytes (292 for DirectX 3): dwSize, guidType, dwOfs, dwType, dwFlags,
// tszName[260], then the force-feedback and HID fields. A key is GUID_Key {55728220-D33C-11CF-...},
// DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(DIK), named by GetKeyNameText; dwOfs is its offset in the current format.
static int put_key(Ctx *c, Obj *o, uint32_t p, uint32_t sz, int dik) {
    char name[64];
    input_key_name((uint8_t)dik, name, sizeof name);
    if (!name[0]) return 0;
    // GUESS: a key the format does not hold reports its c_dfDIKeyboard offset (the DIK); check against Wine.
    uint32_t ofs = (uint32_t)dik;
    for (int k = 0; k < o->nf; k++) if (o->f[k].what == 0x200u + dik) { ofs = o->f[k].ofs; break; }
    memset(G(p + 4), 0, sz - 4);
    st(c, p + 4, 0x55728220u, 4); st(c, p + 8, 0xd33c, 2); st(c, p + 10, 0x11cf, 2);
    memcpy(G(p + 12), guid_tail + 4, 8);
    st(c, p + 0x14, ofs, 4);
    st(c, p + 0x18, 0x04u | (uint32_t)dik << 8, 4);
    memcpy(G(p + 0x20), name, strlen(name) + 1);
    return 1;
}
// GetObjectInfo(pdidoi, dwObj, dwHow): DIPH_BYOFFSET (1) names an offset of the current format (an action's
// DWORD under an action map), DIPH_BYID (2) a dwType.
static uint32_t IDirectInputDevice8A_GetObjectInfo(Ctx *c) {
    SELF(I_DEV);
    uint32_t p = ARG(1), obj = ARG(2), how = ARG(3), sz = p ? ld(c, p, 4) : 0;
    if (!p || (sz != 316 && sz != 292)) return DIERR_INVALIDPARAM;
    if (o->kind != KEYBOARD) { rn_halt(c, "dinput8: GetObjectInfo on the mouse"); return DIERR_INVALIDPARAM; }
    if (how != 1 && how != 2) { rn_halt(c, "dinput8: GetObjectInfo dwHow %u", how); return DIERR_INVALIDPARAM; }
    int dik = -1;
    for (int k = 0; k < o->nf && how == 1; k++)
        if (o->f[k].what >= 0x200 && o->f[k].ofs == obj) { dik = (int)o->f[k].what - 0x200; break; }
    if (how == 2 && (obj & 0xc) && ((obj >> 8) & 0xffff) < 256) dik = (obj >> 8) & 0xff;
    if (dik < 0 || !put_key(c, o, p, sz, dik)) return 0x80070002u;    // DIERR_OBJECTNOTFOUND
    TR("GetObjectInfo(%s %x) -> key %02x \"%s\"", how == 1 ? "offset" : "id", obj, dik, G(p + 0x20));
    return DI_OK;
}
// EnumObjects(lpCallback, pvRef, dwFlags): each object whose type matches dwFlags (DIDFT_ALL = 0), as
// callback(lpddoi, pvRef) until it returns DIENUM_STOP. The keyboard's objects are its keys, all buttons.
static uint32_t IDirectInputDevice8A_EnumObjects(Ctx *c) {
    SELF(I_DEV);
    uint32_t cb = ARG(1), ref = ARG(2), flags = ARG(3);
    if (!cb) return DIERR_INVALIDPARAM;
    if (o->kind != KEYBOARD) { rn_halt(c, "dinput8: EnumObjects on the mouse"); return DIERR_INVALIDPARAM; }
    TR("EnumObjects(flags %x)", flags);
    if ((flags & 0xff) && !(flags & 0x0c)) return DI_OK;          // asks for no buttons
    uint32_t inst = guest_alloc(316);
    st(c, inst, 316, 4);
    for (int dik = 0; dik < 256; dik++) {
        if (!put_key(c, o, inst, 316, dik)) continue;
        uint32_t args[2] = {inst, ref};
        if (!call_guest(c, cb, 2, args) || c->halt) break;
    }
    guest_free(inst);
    return DI_OK;
}

// EnumDevicesBySemantics(ptszUserName, lpdiActionFormat, lpCallback, pvRef, dwFlags): the attached
// devices are the keyboard and the mouse. Each comes as a new device object that is released after
// the callback (the application AddRefs it to keep it). Flags: DIEDBS_NEWDEVICE (no saved user map),
// plus DIEDBS_MAPPEDPRI1 when the action format names a semantic of that device type.
static uint32_t IDirectInput8A_EnumDevicesBySemantics(Ctx *c) {
    SELF(I_DI8);
    uint32_t af = ARG(2), cb = ARG(3), ref = ARG(4), flags = ARG(5);
    if (!af || !cb) return DIERR_INVALIDPARAM;
    TR("EnumDevicesBySemantics(user %08x, flags %x): %u actions", ARG(1), flags, ld(c, af + 12, 4));
    if (flags & ~0x0000003fu) { rn_halt(c, "dinput8: EnumDevicesBySemantics flags %x", flags); return DIERR_INVALIDPARAM; }
    uint32_t nact = ld(c, af + 12, 4), acts = ld(c, af + 16, 4), asz = ld(c, af + 4, 4);
    uint32_t inst = guest_alloc(580);
    st(c, inst, 580, 4);
    for (int kind = KEYBOARD; kind <= MOUSE; kind++) {
        uint32_t mapped = 0;
        for (uint32_t k = 0; k < nact; k++) {
            uint32_t sem = ld(c, acts + asz * k + 4, 4);
            if ((sem >> 24) == (kind == KEYBOARD ? 0x81u : 0x82u)) mapped = 1;
        }
        put_instance(c, inst, kind);
        uint32_t dev = new_obj(c, I_DEV, kind);
        uint32_t args[5] = {inst, dev, 0x20u | mapped, (uint32_t)(MOUSE - kind), ref};
        uint32_t r = call_guest(c, cb, 5, args);
        objs[ld(c, dev + 4, 4) - 1].refs--;
        if (c->halt || r == 0) break;                                // DIENUM_STOP
    }
    guest_free(inst);
    return DI_OK;
}

// DIACTIONFORMATA: dwSize, dwActionSize, dwDataSize, dwNumActions, rgoAction, guidActionMap, dwGenre,
// dwBufferSize, ... DIACTIONA (40 bytes): uAppData, dwSemantic, dwFlags, name, guidInstance, dwObjID, dwHow.
// BuildActionMap and SetActionMap do what Wine's dinput does (dlls/dinput/device.c). The keyboard maps
// DIKEYBOARD_ semantics (0x810004xx, xx = DIK) to that key, object DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(DIK);
// genre semantics are not mapped to the keyboard.
#define DIA_APPMAPPED 0x2u
#define DIA_APPNOMAP 0x4u
#define DIAH_APPREQUESTED 0x2u
#define DIAH_DEFAULT 0x20u
static int is_this_device(Ctx *c, uint32_t g, int kind) {
    return ld(c, g, 4) == (kind == KEYBOARD ? 0x6f1d2b61u : 0x6f1d2b60u) && !memcmp(G(g + 4), guid_tail, 12);
}
static void set_guid(Ctx *c, uint32_t g, int kind) {
    if (kind < 0) { memset(G(g), 0, 16); return; }                 // GUID_NULL
    st(c, g, kind == KEYBOARD ? 0x6f1d2b61u : 0x6f1d2b60u, 4);
    memcpy(G(g + 4), guid_tail, 12);
}
// The key a keyboard object id names, or -1.
static int key_of_obj(uint32_t obj) {
    uint32_t dik = (obj >> 8) & 0xffff;
    char name[64];
    if ((obj & 0xffu) != 0x04u || dik > 255) return -1;
    input_key_name((uint8_t)dik, name, sizeof name);
    return name[0] ? (int)dik : -1;
}

// The user's saved maps: SetActionMap saves "dwSemantic = dwObjID" for each action on the device, per action
// map guid (Wine: HKCU\Software\Wine\DirectInput\Mappings\<user>\<device>\<guid>); BuildActionMap loads them.
// One user and one keyboard here, kept in save/ as lines "<guid hex> <semantic> <object id>".
#define USER_MAPS "DirectInput user maps.txt"
typedef struct { uint8_t guid[16]; uint32_t sem, obj; } Saved;
static Saved saved[1024];
static int nsaved = -1;
static void load_saved(void) {
    if (nsaved >= 0) return;
    nsaved = 0;
    static char text[64 << 10];
    int n = layer_load(USER_MAPS, (uint8_t *)text, sizeof text - 1);
    if (n <= 0) return;
    text[n] = 0;
    for (char *l = text; *l && nsaved < 1024;) {
        Saved s;
        char hex[33];
        if (sscanf(l, "%32s %x %x", hex, &s.sem, &s.obj) == 3 && strlen(hex) == 32) {
            for (int k = 0; k < 16; k++) { unsigned b; sscanf(hex + 2 * k, "%2x", &b); s.guid[k] = (uint8_t)b; }
            saved[nsaved++] = s;
        }
        char *e = strchr(l, '\n');
        if (!e) break;
        l = e + 1;
    }
}
static void save_map(Ctx *c, uint32_t af, int kind) {
    load_saved();
    const uint8_t *g = (const uint8_t *)G(af + 20);
    int k = 0;
    for (int j = 0; j < nsaved; j++) if (memcmp(saved[j].guid, g, 16)) saved[k++] = saved[j];
    nsaved = k;
    for (uint32_t i = 0, n = ld(c, af + 12, 4), a = ld(c, af + 16, 4); i < n && nsaved < 1024; i++, a += 40)
        if (is_this_device(c, a + 16, kind) && ld(c, a + 36, 4)) {
            memcpy(saved[nsaved].guid, g, 16);
            saved[nsaved].sem = ld(c, a + 4, 4);
            saved[nsaved++].obj = ld(c, a + 32, 4);
        }
    static char text[64 << 10];
    int len = 0;
    for (int j = 0; j < nsaved && len < (int)sizeof text - 80; j++) {
        for (int b = 0; b < 16; b++) len += snprintf(text + len, 3, "%02x", saved[j].guid[b]);
        len += snprintf(text + len, 40, " %x %x\n", saved[j].sem, saved[j].obj);
    }
    if (layer_store(USER_MAPS, (const uint8_t *)text, len)) rn_log("dinput8: cannot write save/" USER_MAPS);
}

static uint32_t IDirectInputDevice8A_BuildActionMap(Ctx *c) {
    SELF(I_DEV);
    uint32_t af = ARG(1), flags = ARG(3);
    if (!af || ld(c, af, 4) != 328 || ld(c, af + 4, 4) != 40) return DIERR_INVALIDPARAM;
    if (flags != 0 && flags != 1 && flags != 2 && flags != 4) return DIERR_INVALIDPARAM;
    uint32_t n = ld(c, af + 12, 4), acts = ld(c, af + 16, 4);
    if (ld(c, af + 8, 4) != 4 * n) return DIERR_INVALIDPARAM;
    if (o->kind != KEYBOARD) { rn_halt(c, "dinput8: BuildActionMap on the mouse"); return DIERR_INVALIDPARAM; }
    // Reset: app-mapped actions keep their object; DIA_APPNOMAP holds only for genre semantics.
    for (uint32_t k = 0; k < n; k++) {
        uint32_t a = acts + 40 * k, sem = ld(c, a + 4, 4), fl = ld(c, a + 8, 4);
        if (!sem) return DIERR_INVALIDPARAM;
        if (flags == 1 && (ld(c, a + 16, 4) | ld(c, a + 20, 4) | ld(c, a + 24, 4) | ld(c, a + 28, 4)) &&
            !is_this_device(c, a + 16, o->kind)) continue;          // DIDBAM_PRESERVE: another device's
        if (fl & DIA_APPMAPPED) { st(c, a + 36, DIAH_APPREQUESTED, 4); continue; }
        if ((sem & 0xf0000000u) == 0x80000000u) st(c, a + 8, fl &= ~DIA_APPNOMAP, 4);
        if (!(fl & DIA_APPNOMAP)) set_guid(c, a + 16, -1);
        st(c, a + 36, 0, 4);
    }
    int from_user = 0;
    if (!(flags & 4)) {                                         // not DIDBAM_HWDEFAULTS: the user's map first
        load_saved();
        for (uint32_t k = 0; k < n; k++) {
            uint32_t a = acts + 40 * k, sem = ld(c, a + 4, 4);
            if (ld(c, a + 36, 4)) continue;
            for (int j = 0; j < nsaved; j++)
                if (saved[j].sem == sem && !memcmp(saved[j].guid, G(af + 20), 16)) {
                    st(c, a + 32, saved[j].obj, 4);
                    set_guid(c, a + 16, o->kind);
                    st(c, a + 36, DIAH_DEFAULT, 4);
                    from_user++;
                    break;
                }
        }
    }
    // Each key serves one action: the ones already mapped, then priority 1 semantics, then priority 2.
    uint8_t used[256] = {0};
    for (uint32_t k = 0; k < n; k++) {
        uint32_t a = acts + 40 * k;
        int dik = key_of_obj(ld(c, a + 32, 4));
        if (ld(c, a + 36, 4) && dik >= 0 && is_this_device(c, a + 16, o->kind)) used[dik] = 1;
    }
    uint32_t mapped = 0;
    for (int pri = 0; pri < 2; pri++)
        for (uint32_t k = 0; k < n; k++) {
            uint32_t a = acts + 40 * k, sem = ld(c, a + 4, 4);
            if (ld(c, a + 36, 4) || (ld(c, a + 8, 4) & DIA_APPNOMAP) || !(sem & 0x4000) != !pri) continue;
            int dik = (sem & 0xff000700u) == 0x81000400u ? key_of_obj(0x04u | (sem & 0xff) << 8) : -1;
            if (dik < 0 || used[dik]) continue;
            st(c, a + 32, 0x04u | (uint32_t)dik << 8, 4);
            set_guid(c, a + 16, o->kind);
            st(c, a + 36, DIAH_DEFAULT, 4);
            used[dik] = 1;
            mapped++;
        }
    int any = 0;
    for (int k = 0; k < 256; k++) any |= used[k];
    TR("BuildActionMap(flags %x): %u of %u actions from the user map, %u by semantic", flags, from_user, n, mapped);
    return any ? DI_OK : 1;                                     // DI_NOEFFECT
}
// SetActionMap(lpdiaf, lptszUserName, dwFlags): the device now reports one DWORD per action, at 4 * its index,
// for each action on this device (not DIA_APPNOMAP), and buffers dwBufferSize events. The map is saved.
static uint32_t IDirectInputDevice8A_SetActionMap(Ctx *c) {
    SELF(I_DEV);
    uint32_t af = ARG(1), flags = ARG(3);
    if (!af || ld(c, af, 4) != 328) return DIERR_INVALIDPARAM;
    if (flags != 0 && flags != 1 && flags != 2) return DIERR_INVALIDPARAM;
    if (o->acquired) return 0x80070005u;                    // DIERR_ACQUIRED
    if (o->kind != KEYBOARD) { rn_halt(c, "dinput8: action map on the mouse"); return DIERR_INVALIDPARAM; }
    o->data_size = ld(c, af + 8, 4);
    o->buffer = ld(c, af + 40, 4);
    o->format_set = 1;
    o->actions = 1;
    o->nf = 0;
    for (uint32_t k = 0, n = ld(c, af + 12, 4), a = ld(c, af + 16, 4); k < n; k++, a += 40) {
        int dik = key_of_obj(ld(c, a + 32, 4));
        if ((ld(c, a + 8, 4) & DIA_APPNOMAP) || !is_this_device(c, a + 16, o->kind) || dik < 0 || o->nf == MAXF) continue;
        o->f[o->nf++] = (Field){4 * k, 0x200u + (uint32_t)dik, 4, ld(c, a, 4)};
    }
    save_map(c, af, o->kind);
    TR("SetActionMap(flags %x): %u bytes of data, %d keys, buffer %u", flags, o->data_size, o->nf, o->buffer);
    for (uint32_t k = 0, n = ld(c, af + 12, 4), a = ld(c, af + 16, 4); k < n && rn_trace(); k++, a += 40)
        TR("  action %u: app %08x sem %08x flags %x obj %08x how %x name %s", k, ld(c, a, 4), ld(c, a + 4, 4), ld(c, a + 8, 4),
           ld(c, a + 32, 4), ld(c, a + 36, 4), ld(c, a + 12, 4) > 0x10000 ? G(ld(c, a + 12, 4)) : "");
    if (flags == 2) return 0x0000000bu;                     // DI_SETTINGSNOTSAVED, as Wine answers DIDSAM_FORCESAVE
    return o->nf ? DI_OK : 1;                               // DI_NOEFFECT
}

// EnumDevices(dwDevType, lpCallback, pvRef, dwFlags): mouse then keyboard, the order Windows lists them.
// dwDevType is a DI8DEVCLASS_ (0 all, 1 device, 2 pointer, 3 keyboard, 4 game controller) or a DI8DEVTYPE_.
static uint32_t IDirectInput8A_EnumDevices(Ctx *c) {
    SELF(I_DI8);
    uint32_t type = ARG(1), cb = ARG(2), ref = ARG(3), flags = ARG(4);
    TR("EnumDevices(type %x, flags %x)", type, flags);
    if (!cb) return DIERR_INVALIDPARAM;
    uint32_t inst = guest_alloc(580);
    st(c, inst, 580, 4);
    for (int kind = MOUSE; kind >= KEYBOARD; kind--) {
        int want = type == 0 || (type == 2 && kind == MOUSE) || (type == 3 && kind == KEYBOARD) ||
                   (type == 0x12 && kind == MOUSE) || (type == 0x13 && kind == KEYBOARD);
        if (!want || (flags & 0x100)) continue;                 // DIEDFL_FORCEFEEDBACK: neither
        put_instance(c, inst, kind);
        uint32_t args[2] = {inst, ref};
        if (!call_guest(c, cb, 2, args) || c->halt) break;
    }
    guest_free(inst);
    return DI_OK;
}

// The keyboard and the mouse are interrupt-driven: Poll has nothing to do (DI_NOEFFECT).
static uint32_t IDirectInputDevice8A_Poll(Ctx *c) {
    SELF(I_DEV);
    return o->acquired ? 1 : DIERR_NOTACQUIRED;
}

// Input from host/input.c goes to every acquired device of its kind (the window is always in the foreground).
static void push(Obj *o, uint32_t ofs, uint32_t data, uint32_t app) {
    static uint32_t seq;
    TR("push %s ofs %u data %x (buffer %u, queued %u)", o->kind == KEYBOARD ? "keyboard" : "mouse", ofs, data, o->buffer, o->ev_tail - o->ev_head);
    if (!o->buffer) return;
    if (o->ev_tail - o->ev_head >= o->buffer || o->ev_tail - o->ev_head >= MAXEV) { o->overflow = 1; return; }
    o->ev[o->ev_tail++ % MAXEV] = (Event){ofs, data, (uint32_t)(now_us() / 1000), ++seq, app};
}
void dinput8_key(uint8_t dik, int down) {
    for (int k = 0; k < nobjs; k++) {
        Obj *o = &objs[k];
        if (o->iface != I_DEV || o->kind != KEYBOARD || !o->acquired) continue;
        for (int j = 0; j < o->nf; j++)
            if (o->f[j].what == 0x200u + dik) push(o, o->f[j].ofs, down ? 0x80 : 0, o->f[j].app);
    }
}
void dinput8_button(int b, int down) {
    for (int k = 0; k < nobjs; k++) {
        Obj *o = &objs[k];
        if (o->iface != I_DEV || o->kind != MOUSE || !o->acquired) continue;
        o->buttons[b] = down ? 0x80 : 0;
        for (int j = 0; j < o->nf; j++) if (o->f[j].what == 0x100u + b) push(o, o->f[j].ofs, o->buttons[b], 0);
    }
}
void dinput8_motion(int32_t dx, int32_t dy, int32_t dz) {
    int32_t d[3] = {dx, dy, dz};
    for (int k = 0; k < nobjs; k++) {
        Obj *o = &objs[k];
        if (o->iface != I_DEV || o->kind != MOUSE || !o->acquired) continue;
        for (int a = 0; a < 3; a++) {
            if (!d[a]) continue;
            o->axis[a] += d[a];
            for (int j = 0; j < o->nf; j++) if (o->f[j].what == (uint32_t)a) push(o, o->f[j].ofs, (uint32_t)d[a], 0);
        }
    }
}

#define DI(name, n) {"dinput8.dll", "IDirectInput8A::" #name, n, IDirectInput8A_##name}
#define DEVM(name, n) {"dinput8.dll", "IDirectInputDevice8A::" #name, n, IDirectInputDevice8A_##name}
const Api dinput8_apis[] = {
    {"dinput8.dll", "DirectInput8Create", 5, DirectInput8Create_}, {"dinput.dll", "DirectInputCreateA", 4, DirectInputCreateA_},
    {"dinput8.dll", "IDirectInput8A::QueryInterface", 3, QueryInterface_},
    {"dinput8.dll", "IDirectInput8A::AddRef", 1, AddRef_}, {"dinput8.dll", "IDirectInput8A::Release", 1, Release_},
    {"dinput8.dll", "IDirectInputDevice8A::QueryInterface", 3, QueryInterface_},
    {"dinput8.dll", "IDirectInputDevice8A::AddRef", 1, AddRef_},
    {"dinput8.dll", "IDirectInputDevice8A::Release", 1, Release_},
    DI(CreateDevice, 4), DEVM(GetCapabilities, 2), DEVM(GetProperty, 3), DI(EnumDevicesBySemantics, 6), DI(EnumDevices, 5), DEVM(GetDeviceInfo, 2), DEVM(BuildActionMap, 4), DEVM(SetActionMap, 4), DEVM(Poll, 1),
    DEVM(SetDataFormat, 2), DEVM(SetCooperativeLevel, 3), DEVM(Acquire, 1), DEVM(Unacquire, 1),
    DEVM(GetDeviceState, 3), DEVM(GetDeviceData, 5), DEVM(SetProperty, 3), DEVM(GetObjectInfo, 4), DEVM(EnumObjects, 4),
};
const int dinput8_api_count = sizeof dinput8_apis / sizeof dinput8_apis[0];
