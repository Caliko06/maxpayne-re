// user32: window classes, windows, the message queue, wsprintf. One desktop of DESK_W x DESK_H.
// Messages that Windows sends synchronously (create, show, activate) call the window procedure here.
#include <ctype.h>
#include <strings.h>
#include "host.h"

#define DESK_W rn_screen_w
#define DESK_H rn_screen_h
#define WM_CREATE 0x0001
#define WM_MOVE 0x0003
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_SETFOCUS 0x0007
#define WM_SHOWWINDOW 0x0018
#define WM_ACTIVATEAPP 0x001c
#define WM_NCCREATE 0x0081
#define WM_PAINT 0x000f
#define WM_QUIT 0x0012
#define WM_ERASEBKGND 0x0014

typedef struct { char name[64]; uint32_t proc, style, extra, instance, atom, brush, clsextra, icon, cursor, menu; } WClass;
static WClass classes[32];
static int nclasses;
typedef struct { uint32_t proc, style, exstyle, x, y, w, h, visible, userdata, id, parent; WClass *cls;
                 int32_t upd[4]; int dirty, erase;               // update region (its bounding rectangle)
                 int dead, disabled; char title[128]; } Window;
static Window windows[64];
static int nwindows;
static uint32_t focus, active, gui_tid;

typedef struct { uint32_t hwnd, msg, wp, lp; } Msg;
static Msg queue[256];
static unsigned qhead, qtail;
static int quit_posted;
static uint32_t quit_code;

static uint32_t hwnd_of(int k) { return 0x10010 + 4u * k; }
static Window *win(uint32_t h) {
    uint32_t k = (h - 0x10010) / 4;
    return h >= 0x10010 && !(h & 3) && k < (uint32_t)nwindows && !windows[k].dead ? &windows[k] : NULL;
}
static uint32_t send(Ctx *c, uint32_t h, uint32_t msg, uint32_t wp, uint32_t lp) {
    Window *w = win(h);
    if (!w || c->halt) return 0;
    uint32_t args[4] = {h, msg, wp, lp};
    if (rn_trace()) rn_log("send %04x to %08x", msg, h);
    return call_guest(c, w->proc, 4, args);
}

static WClass *find_class(Ctx *c, uint32_t name) {
    for (int k = 0; k < nclasses; k++)
        if (name < 0x10000 ? classes[k].atom == name : !strcasecmp(classes[k].name, G(name))) return &classes[k];
    return NULL;
}
// From WNDCLASSA at p (RegisterClassExA passes its WNDCLASSEXA + 4: the same ten fields follow cbSize).
static uint32_t register_class(Ctx *c, uint32_t p) {
    if (nclasses == 32) { rn_halt(c, "too many window classes"); return 0; }
    uint32_t name = ld(c, p + 36, 4);
    if (find_class(c, name)) { set_error(c, 1410); return 0; }      // ERROR_CLASS_ALREADY_EXISTS
    WClass *k = &classes[nclasses];
    snprintf(k->name, sizeof k->name, "%s", name < 0x10000 ? "#atom" : G(name));
    k->style = ld(c, p, 4); k->proc = ld(c, p + 4, 4); k->clsextra = ld(c, p + 8, 4); k->extra = ld(c, p + 12, 4);
    k->instance = ld(c, p + 16, 4); k->icon = ld(c, p + 20, 4); k->cursor = ld(c, p + 24, 4); k->brush = ld(c, p + 28, 4);
    k->menu = ld(c, p + 32, 4);
    k->atom = 0xc000 + nclasses++;
    return k->atom;
}
static uint32_t RegisterClassA_(Ctx *c) { return register_class(c, ARG(0)); }
static uint32_t RegisterClassExA_(Ctx *c) {
    if (ld(c, ARG(0), 4) != 48) { set_error(c, 87); return 0; }
    return register_class(c, ARG(0) + 4);
}
// GetClassInfoA(instance, name, WNDCLASSA*): the registered class, or 0 and ERROR_CLASS_DOES_NOT_EXIST.
static uint32_t GetClassInfoA_(Ctx *c) {
    WClass *k = find_class(c, ARG(1));
    uint32_t p = ARG(2);
    if (!k) { set_error(c, 1411); return 0; }
    uint32_t f[10] = {k->style, k->proc, k->clsextra, k->extra, k->instance, k->icon, k->cursor, k->brush, k->menu, ARG(1)};
    for (int i = 0; i < 10; i++) st(c, p + 4 * i, f[i], 4);
    return k->atom;
}
static uint32_t UnregisterClassA_(Ctx *c) { (void)c; return 1; }
// Icons and cursors are only handed back to RegisterClass/SetCursor: a nonzero handle per resource id.
static uint32_t LoadIconA_(Ctx *c) { return 0x20000 + (ARG(1) & 0xffff); }
static uint32_t LoadCursorA_(Ctx *c) { return 0x30000 + (ARG(1) & 0xffff); }
static uint32_t SetCursor_(Ctx *c) { static uint32_t cur; uint32_t old = cur; cur = ARG(0); (void)c; return old; }
static uint32_t ShowCursor_(Ctx *c) { static int count; count += ARG(0) ? 1 : -1; (void)c; return (uint32_t)count; }
// The cursor position: the centre of the desktop until SetCursorPos moves it (the game reads the mouse through DirectInput).
static int32_t cursor_x = -1, cursor_y;
static uint32_t SetCursorPos_(Ctx *c) { cursor_x = (int32_t)ARG(0); cursor_y = (int32_t)ARG(1); return 1; }
static uint32_t GetCursorPos_(Ctx *c) {
    if (cursor_x < 0) { cursor_x = DESK_W / 2; cursor_y = DESK_H / 2; }
    st(c, ARG(0), (uint32_t)cursor_x, 4); st(c, ARG(0) + 4, (uint32_t)cursor_y, 4);
    return 1;
}

// Frame sizes of XP classic style: caption 19, sizing border 4, fixed border 3, thin border 1.
static void frame(uint32_t style, uint32_t ex, int *b, int *cap) {
    *b = 0;
    if (style & 0x00040000) *b = 4;                       // WS_THICKFRAME
    else if ((style & 0x00c00000) || (ex & 1)) *b = 3;    // WS_CAPTION / WS_DLGFRAME / WS_EX_DLGMODALFRAME
    else if (style & 0x00800000) *b = 1;                  // WS_BORDER
    *cap = (style & 0x00c00000) == 0x00c00000 ? 19 : 0;
}
static void adjust(Ctx *c, uint32_t r, uint32_t style, uint32_t ex) {
    int b, cap;
    frame(style, ex, &b, &cap);
    st(c, r, ld(c, r, 4) - b, 4); st(c, r + 4, ld(c, r + 4, 4) - b - cap, 4);
    st(c, r + 8, ld(c, r + 8, 4) + b, 4); st(c, r + 12, ld(c, r + 12, 4) + b, 4);
}
static uint32_t AdjustWindowRect_(Ctx *c) { adjust(c, ARG(0), ARG(1), 0); return 1; }
static uint32_t AdjustWindowRectEx_(Ctx *c) { adjust(c, ARG(0), ARG(1), ARG(3)); return 1; }

// Client = window minus the frame AdjustWindowRect adds.
static void client_size(const Window *w, uint32_t *cw, uint32_t *ch) {
    int b, cap;
    frame(w->style, w->exstyle, &b, &cap);
    *cw = w->w - 2 * b;
    *ch = w->h - 2 * b - cap;
}
// The update region is kept as its bounding rectangle in client coordinates.
static void invalidate(Window *w, const int32_t *r, int erase) {
    uint32_t cw, ch;
    client_size(w, &cw, &ch);
    int32_t a[4] = {0, 0, (int32_t)cw, (int32_t)ch};
    if (r) {
        for (int k = 0; k < 2; k++) if (r[k] > a[k]) a[k] = r[k];
        for (int k = 2; k < 4; k++) if (r[k] < a[k]) a[k] = r[k];
    }
    if (a[0] >= a[2] || a[1] >= a[3]) return;
    if (!w->dirty) memcpy(w->upd, a, sizeof a);
    else {
        for (int k = 0; k < 2; k++) if (a[k] < w->upd[k]) w->upd[k] = a[k];
        for (int k = 2; k < 4; k++) if (a[k] > w->upd[k]) w->upd[k] = a[k];
    }
    w->dirty = 1;
    w->erase |= erase;
}
// Hooks (SetWindowsHookExA): one chain per hook type, newest first. A hook procedure passes the event on with
// CallNextHookEx. Called here: WH_CBT (5) HCBT_CREATEWND, which MFC uses to subclass every window it creates
// (AfxHookWindowCreate). WH_KEYBOARD_LL (13) is kept; this PC delivers keys through DirectInput and the queue.
typedef struct { uint32_t type, proc, live; } Hook;
static Hook hooks[32];
static unsigned hook_seq[32], seq;      // install order: the chain runs newest first
static uint32_t hook_handle(int k) { return 0x40000u + (uint32_t)k; }
static uint32_t SetWindowsHookExA_(Ctx *c) {
    for (int k = 0; k < 32; k++)
        if (!hooks[k].live) { hooks[k] = (Hook){ARG(0), ARG(1), 1}; hook_seq[k] = ++seq; return hook_handle(k); }
    rn_halt(c, "SetWindowsHookExA: too many hooks");
    return 0;
}
static uint32_t UnhookWindowsHookEx_(Ctx *c) {
    uint32_t k = ARG(0) - 0x40000u;
    if (k >= 32 || !hooks[k].live) return 0;
    hooks[k].live = 0;
    return 1;
}
// The next hook of the chain after `from` (-1: the newest): the newest one installed before it.
static int next_hook(uint32_t type, int from) {
    int best = -1;
    unsigned lim = from >= 0 ? hook_seq[from] : ~0u;
    for (int k = 0; k < 32; k++)
        if (hooks[k].live && hooks[k].type == type && hook_seq[k] < lim && (best < 0 || hook_seq[k] > hook_seq[best])) best = k;
    return best;
}
static uint32_t call_hook(Ctx *c, int k, uint32_t code, uint32_t wp, uint32_t lp) {
    if (k < 0) return 0;
    uint32_t a[3] = {code, wp, lp};
    return call_guest(c, hooks[k].proc, 3, a);
}
static uint32_t CallNextHookEx_(Ctx *c) {
    uint32_t k = ARG(0) - 0x40000u;
    if (k >= 32) return 0;
    return call_hook(c, next_hook(hooks[k].type, (int)k), ARG(1), ARG(2), ARG(3));
}
// FindWindowA(class, title): a top-level window with that class (name or atom); the title is not kept, so a
// title alone matches nothing.
// TranslateAcceleratorA(hwnd, haccel, MSG*): ACCEL entries {fVirt, key, cmd} of 8 bytes, 0x80 on the last.
// A WM_KEYDOWN/WM_SYSKEYDOWN matches an FVIRTKEY entry with the same Shift/Ctrl/Alt state; a WM_CHAR/WM_SYSCHAR
// matches a character entry. A match sends WM_COMMAND (id, 1) and returns 1.
static uint32_t TranslateAcceleratorA_(Ctx *c) {
    uint32_t h = ARG(0), t = ARG(1), m = ARG(2);
    if (!t || !win(h)) return 0;
    uint32_t msg = ld(c, m + 4, 4), key = ld(c, m + 8, 4);
    int virt = msg == 0x100 || msg == 0x104, chr = msg == 0x102 || msg == 0x106;
    if (!virt && !chr) return 0;
    uint32_t mods = (input_shift() ? 4 : 0) | ((input_vk_state(0x11) & 0x8000) ? 8 : 0) | (input_alt() ? 0x10 : 0);
    for (uint32_t e = t;; e += 8) {
        uint32_t f = ld(c, e, 2), k = ld(c, e + 2, 2), id = ld(c, e + 4, 2);
        int match = (f & 1) ? virt && k == key && (f & 0x1c) == mods : chr && k == key;
        if (match) { send(c, h, 0x0111, 0x10000u | id, 0); return 1; }
        if (f & 0x80) return 0;
    }
}
// MessageBoxA(hwnd, text, caption, type): nobody sits at this PC; the box is logged and answered with its default
// button (MB_OK/MB_OKCANCEL: IDOK 1, MB_YESNO/YESNOCANCEL: IDYES 6, MB_RETRYCANCEL: IDRETRY 4, ABORTRETRYIGNORE: IDABORT 3).
static uint32_t MessageBoxA_(Ctx *c) {
    static const uint32_t answer[6] = {1, 1, 3, 6, 6, 4};
    uint32_t type = ARG(3) & 0xf;
    rn_log("MessageBoxA \"%s\": %s", ARG(2) ? G(ARG(2)) : "Error", ARG(1) ? G(ARG(1)) : "");
    return type < 6 ? answer[type] : 1;
}
// SystemParametersInfoA(action, uiParam, pvParam, winIni).
static uint32_t SystemParametersInfoA_(Ctx *c) {
    uint32_t act = ARG(0), ui = ARG(1), pv = ARG(2);
    static uint32_t saver_running;
    switch (act) {
    case 0x61:                                  // SPI_SETSCREENSAVERRUNNING: pvParam gets the previous state
        if (pv) st(c, pv, saver_running, 4);
        saver_running = ui != 0;
        return 1;
    case 0x30:                                  // SPI_GETWORKAREA: the desktop above a 30-pixel taskbar
        st(c, pv, 0, 4); st(c, pv + 4, 0, 4); st(c, pv + 8, DESK_W, 4); st(c, pv + 12, DESK_H - 30, 4);
        return 1;
    }
    rn_halt(c, "SystemParametersInfoA(%#x)", act);
    return 0;
}
static uint32_t FindWindowA_(Ctx *c) {
    uint32_t cname = ARG(0);
    if (!cname) return 0;
    for (int k = 0; k < nwindows; k++)
        if (windows[k].cls && !windows[k].parent &&
            (cname < 0x10000 ? windows[k].cls->atom == cname : !strcasecmp(windows[k].cls->name, G(cname)))) return hwnd_of(k);
    return 0;
}
static uint32_t CreateWindowExA_(Ctx *c) {
    uint32_t ex = ARG(0), cname = ARG(1), title = ARG(2), style = ARG(3);
    int32_t x = ARG(4), y = ARG(5), w = ARG(6), h = ARG(7);
    uint32_t parent = ARG(8), menu = ARG(9), inst = ARG(10), param = ARG(11);
    WClass *k = find_class(c, cname);
    if (!k) { rn_halt(c, "CreateWindowExA: class %s not registered", cname < 0x10000 ? "#atom" : G(cname)); return 0; }
    if (nwindows == 64) { rn_halt(c, "too many windows"); return 0; }
    if (x == (int32_t)0x80000000) { x = 0; y = 0; }
    if (w == (int32_t)0x80000000) { w = DESK_W; h = DESK_H; }
    Window *wd = &windows[nwindows];
    uint32_t hw = hwnd_of(nwindows++);
    *wd = (Window){k->proc, style, ex, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, 0, 0, menu, parent, k};
    snprintf(wd->title, sizeof wd->title, "%s", title ? G(title) : "");
    wd->disabled = (style & 0x08000000) != 0;                   // WS_DISABLED
    if (!gui_tid) gui_tid = ld(c, c->fs + 0x24, 4);             // the thread that creates the first window
    rn_log("CreateWindowExA \"%s\" class \"%s\" %dx%d at %d,%d style %08x ex %08x -> %08x",
           title ? G(title) : "", k->name, w, h, x, y, style, ex, hw);
    // CREATESTRUCTA for WM_NCCREATE / WM_CREATE.
    uint32_t cs = guest_alloc(48);
    uint32_t f[12] = {param, inst, menu, parent, (uint32_t)h, (uint32_t)w, (uint32_t)y, (uint32_t)x, style, title, cname, ex};
    for (int i = 0; i < 12; i++) st(c, cs + 4 * i, f[i], 4);
    // WH_CBT HCBT_CREATEWND (3): CBT_CREATEWND {lpcs, hwndInsertAfter}; a nonzero answer cancels the window.
    if (next_hook(5, -1) >= 0) {
        uint32_t cbt = guest_alloc(8);
        st(c, cbt, cs, 4);
        if (call_hook(c, next_hook(5, -1), 3, hw, cbt) && !c->halt) { rn_log("WH_CBT refused the window"); return 0; }
        if (c->halt) return 0;
    }
    if (!send(c, hw, WM_NCCREATE, 0, cs) && !c->halt) { rn_log("WM_NCCREATE refused"); return 0; }
    if ((int32_t)send(c, hw, WM_CREATE, 0, cs) == -1 && !c->halt) { rn_log("WM_CREATE refused"); return 0; }
    if (c->halt) return 0;
    if (style & 0x10000000) {   // WS_VISIBLE
        wd->visible = 1;
        invalidate(wd, NULL, 1);
        send(c, hw, WM_SHOWWINDOW, 1, 0);
    }
    send(c, hw, WM_SIZE, 0, ((uint32_t)h << 16) | ((uint32_t)w & 0xffff));
    send(c, hw, WM_MOVE, 0, ((uint32_t)y << 16) | ((uint32_t)x & 0xffff));
    return hw;
}
static void activate(Ctx *c, uint32_t h) {
    if (active == h) return;
    active = h;
    send(c, h, WM_ACTIVATEAPP, 1, 0);
    send(c, h, WM_ACTIVATE, 1, 0);
    if (focus != h) { focus = h; send(c, h, WM_SETFOCUS, 0, 0); }
}
static uint32_t ShowWindow_(Ctx *c) {
    uint32_t h = ARG(0), cmd = ARG(1);
    Window *w = win(h);
    if (!w) return 0;
    uint32_t was = w->visible;
    if (cmd == 0) { w->visible = 0; if (was) send(c, h, WM_SHOWWINDOW, 0, 0); return was; }
    w->visible = 1;
    if (!was) { invalidate(w, NULL, 1); send(c, h, WM_SHOWWINDOW, 1, 0); }
    if (cmd != 4 && cmd != 8 && cmd != 7) activate(c, h);    // SW_SHOWNOACTIVATE, SW_SHOWNA, SW_SHOWMINNOACTIVE
    return was;
}
// UpdateWindow sends WM_PAINT at once when the window has an update region.
static uint32_t UpdateWindow_(Ctx *c) {
    Window *w = win(ARG(0));
    if (!w) return 0;
    if (w->visible && w->dirty) send(c, ARG(0), WM_PAINT, 0, 0);
    return 1;
}
static uint32_t SetFocus_(Ctx *c) {
    uint32_t h = ARG(0), old = focus;
    if (h && h != focus) { focus = h; send(c, h, WM_SETFOCUS, old, 0); }
    return old;
}
static uint32_t SetForegroundWindow_(Ctx *c) { activate(c, ARG(0)); return 1; }
static uint32_t SetActiveWindow_(Ctx *c) { uint32_t old = active; activate(c, ARG(0)); return old; }
static uint32_t GetForegroundWindow_(Ctx *c) { (void)c; return active; }
static uint32_t GetDesktopWindow_(Ctx *c) { (void)c; return 0x10004; }
static uint32_t GetClientRect_(Ctx *c) {
    uint32_t h = ARG(0), r = ARG(1);
    Window *w = win(h);
    uint32_t cw = DESK_W, ch = DESK_H;
    if (w) client_size(w, &cw, &ch);
    else if (h != 0x10004) { set_error(c, 1400); return 0; }
    st(c, r, 0, 4); st(c, r + 4, 0, 4); st(c, r + 8, cw, 4); st(c, r + 12, ch, 4);
    return 1;
}
static uint32_t GetWindowRect_(Ctx *c) {
    uint32_t h = ARG(0), r = ARG(1);
    Window *w = win(h);
    if (!w && h != 0x10004) { set_error(c, 1400); return 0; }
    uint32_t x = w ? w->x : 0, y = w ? w->y : 0, ww = w ? w->w : DESK_W, hh = w ? w->h : DESK_H;
    st(c, r, x, 4); st(c, r + 4, y, 4); st(c, r + 8, x + ww, 4); st(c, r + 12, y + hh, 4);
    return 1;
}
static uint32_t SetWindowPos_(Ctx *c) {
    Window *w = win(ARG(0));
    uint32_t flags = ARG(6);
    if (!w) return 0;
    if (!(flags & 2)) { w->x = ARG(2); w->y = ARG(3); }       // SWP_NOMOVE
    if (!(flags & 1)) { w->w = ARG(4); w->h = ARG(5); }       // SWP_NOSIZE
    return 1;
}
static uint32_t SetRect_(Ctx *c) { uint32_t r = ARG(0); for (int k = 0; k < 4; k++) st(c, r + 4 * k, ARG(1 + k), 4); return 1; }
static uint32_t SetWindowLongA_(Ctx *c) {
    Window *w = win(ARG(0));
    int32_t idx = ARG(1);
    uint32_t v = ARG(2), old;
    if (!w) return 0;
    switch (idx) {
    case -4: old = w->proc; w->proc = v; return old;          // GWL_WNDPROC
    case -16: old = w->style; w->style = v; return old;
    case -20: old = w->exstyle; w->exstyle = v; return old;
    case -21: old = w->userdata; w->userdata = v; return old;
    }
    rn_halt(c, "SetWindowLongA index %d", idx);
    return 0;
}
static uint32_t GetWindowLongA_(Ctx *c) {
    Window *w = win(ARG(0));
    int32_t idx = ARG(1);
    if (!w) return 0;
    switch (idx) {
    case -4: return w->proc;
    case -16: return w->style;
    case -20: return w->exstyle;
    case -21: return w->userdata;
    }
    rn_halt(c, "GetWindowLongA index %d", idx);
    return 0;
}
static uint32_t IsIconic_(Ctx *c) { (void)c; return 0; }
// Screen position of a window's client origin: its frame offset, plus the parent's client origin for a child.
static void client_origin(Window *w, int32_t *x, int32_t *y) {
    int b, cap;
    frame(w->style, w->exstyle, &b, &cap);
    *x = (int32_t)w->x + b; *y = (int32_t)w->y + b + cap;
    Window *p = (w->style & 0x40000000) ? win(w->parent) : NULL;    // WS_CHILD
    if (p) { int32_t px, py; client_origin(p, &px, &py); *x += px; *y += py; }
}
static uint32_t ClientToScreen_(Ctx *c) {
    Window *w = win(ARG(0));
    if (!w) return 0;
    int32_t x, y;
    client_origin(w, &x, &y);
    st(c, ARG(1), ld(c, ARG(1), 4) + x, 4); st(c, ARG(1) + 4, ld(c, ARG(1) + 4, 4) + y, 4);
    return 1;
}
static uint32_t ScreenToClient_(Ctx *c) {
    Window *w = win(ARG(0));
    if (!w) return 0;
    int32_t x, y;
    client_origin(w, &x, &y);
    st(c, ARG(1), ld(c, ARG(1), 4) - x, 4); st(c, ARG(1) + 4, ld(c, ARG(1) + 4, 4) - y, 4);
    return 1;
}
// MapWindowPoints(from, to, points, n): from/to 0 = the screen. Returns the offset (x low word, y high word).
static uint32_t MapWindowPoints_(Ctx *c) {
    int32_t fx = 0, fy = 0, tx = 0, ty = 0;
    if (ARG(0)) { Window *w = win(ARG(0)); if (!w) return 0; client_origin(w, &fx, &fy); }
    if (ARG(1)) { Window *w = win(ARG(1)); if (!w) return 0; client_origin(w, &tx, &ty); }
    int32_t dx = fx - tx, dy = fy - ty;
    for (uint32_t k = 0; k < 2 * ARG(3); k++) st(c, ARG(2) + 4 * k, ld(c, ARG(2) + 4 * k, 4) + (k & 1 ? dy : dx), 4);
    return ((uint32_t)dy << 16) | ((uint32_t)dx & 0xffff);
}
// DestroyWindow: WM_DESTROY, then the children, then WM_NCDESTROY; the handle is invalid afterwards.
static uint32_t DestroyWindow_(Ctx *c) {
    uint32_t h = ARG(0);
    Window *w = win(h);
    if (!w) { set_error(c, 1400); return 0; }
    w->visible = 0;
    send(c, h, 0x0002, 0, 0);                                         // WM_DESTROY
    for (int k = 0; k < nwindows; k++)
        if (!windows[k].dead && windows[k].parent == h && (windows[k].style & 0x40000000)) {
            send(c, hwnd_of(k), 0x0002, 0, 0);
            send(c, hwnd_of(k), 0x0082, 0, 0);
            windows[k].dead = 1;
        }
    send(c, h, 0x0082, 0, 0);                                         // WM_NCDESTROY
    w->dead = 1;
    if (focus == h) focus = 0;
    if (active == h) active = 0;
    return 1;
}
static uint32_t IsWindow_(Ctx *c) { return win(ARG(0)) != NULL; }
static uint32_t IsWindowVisible_(Ctx *c) { Window *w = win(ARG(0)); return w && w->visible; }
static uint32_t IsWindowEnabled_(Ctx *c) { Window *w = win(ARG(0)); return w && !w->disabled; }
static uint32_t EnableWindow_(Ctx *c) {
    Window *w = win(ARG(0));
    if (!w) return 0;
    uint32_t was = (uint32_t)w->disabled;
    w->disabled = !ARG(1);
    if (was != (uint32_t)w->disabled) send(c, ARG(0), 0x000a, ARG(1) != 0, 0);    // WM_ENABLE
    return was;
}
static uint32_t IsZoomed_(Ctx *c) { (void)c; return 0; }
static uint32_t GetParent_(Ctx *c) { Window *w = win(ARG(0)); return w ? w->parent : 0; }
static uint32_t IsChild_(Ctx *c) {
    for (Window *w = win(ARG(1)); w && (w->style & 0x40000000); w = win(w->parent)) if (w->parent == ARG(0)) return 1;
    return 0;
}
// GetWindow(h, cmd): siblings share a parent and keep creation order; GW_HWNDFIRST 0 .. GW_CHILD 5.
static uint32_t sibling(uint32_t parent, int from, int dir) {
    for (int k = from; k >= 0 && k < nwindows; k += dir)
        if (!windows[k].dead && windows[k].parent == parent) return hwnd_of(k);
    return 0;
}
static uint32_t GetWindow_(Ctx *c) {
    Window *w = win(ARG(0));
    if (!w) { set_error(c, 1400); return 0; }
    int k = (int)((ARG(0) - 0x10010) / 4);
    uint32_t par = (w->style & 0x40000000) ? w->parent : 0;
    switch (ARG(1)) {
    case 0: return sibling(par, 0, 1);
    case 1: return sibling(par, nwindows - 1, -1);
    case 2: return sibling(par, k + 1, 1);
    case 3: return sibling(par, k - 1, -1);
    case 4: return (w->style & 0x40000000) ? 0 : w->parent;        // GW_OWNER
    case 5: return sibling(ARG(0), 0, 1);                           // GW_CHILD
    }
    return 0;
}
static uint32_t GetTopWindow_(Ctx *c) { return sibling(ARG(0), 0, 1); }
static uint32_t GetLastActivePopup_(Ctx *c) { return ARG(0); }
static uint32_t GetFocus_(Ctx *c) { (void)c; return focus; }
static uint32_t GetActiveWindow_(Ctx *c) { (void)c; return active; }
static uint32_t GetDlgCtrlID_(Ctx *c) { Window *w = win(ARG(0)); return w && (w->style & 0x40000000) ? w->id : 0; }
// Every window belongs to the GUI thread (the one that runs WinMain) of this process.
static uint32_t GetWindowThreadProcessId_(Ctx *c) {
    if (!win(ARG(0))) return 0;
    if (ARG(1)) st(c, ARG(1), ld(c, c->fs + 0x20, 4), 4);
    return gui_tid ? gui_tid : ld(c, c->fs + 0x24, 4);
}
static uint32_t SendMessageA_(Ctx *c) { return send(c, ARG(0), ARG(1), ARG(2), ARG(3)); }
// CallWindowProcA(proc, hwnd, msg, wp, lp): the previous procedure of a subclassed window (guest code or a host
// function such as DefWindowProcA: both are call targets).
static uint32_t CallWindowProcA_(Ctx *c) {
    uint32_t a[4] = {ARG(1), ARG(2), ARG(3), ARG(4)};
    return ARG(0) ? call_guest(c, ARG(0), 4, a) : 0;
}
static uint32_t GetWindowTextA_(Ctx *c) {
    Window *w = win(ARG(0));
    uint32_t n = ARG(2);
    if (!w || !n) return 0;
    uint32_t len = (uint32_t)strlen(w->title) < n - 1 ? (uint32_t)strlen(w->title) : n - 1;
    memcpy(G(ARG(1)), w->title, len); G(ARG(1))[len] = 0;
    return len;
}
static uint32_t GetWindowTextLengthA_(Ctx *c) { Window *w = win(ARG(0)); return w ? (uint32_t)strlen(w->title) : 0; }
static uint32_t SetWindowTextA_(Ctx *c) {
    Window *w = win(ARG(0));
    if (!w) return 0;
    snprintf(w->title, sizeof w->title, "%s", ARG(1) ? G(ARG(1)) : "");
    return 1;
}
// Window properties (SetPropA): name strings or atoms, per window.
static struct { uint32_t hwnd, atom, value; char name[64]; } props[64];
static int prop_slot(Ctx *c, uint32_t h, uint32_t name, int make) {
    for (int k = 0; k < 64; k++)
        if (props[k].hwnd == h && (name < 0x10000 ? props[k].atom == name : !strcasecmp(props[k].name, G(name)))) return k;
    if (!make) return -1;
    for (int k = 0; k < 64; k++)
        if (!props[k].hwnd) {
            props[k].hwnd = h; props[k].atom = name < 0x10000 ? name : 0;
            snprintf(props[k].name, sizeof props[k].name, "%s", name < 0x10000 ? "" : G(name));
            return k;
        }
    return -1;
}
static uint32_t SetPropA_(Ctx *c) { int k = prop_slot(c, ARG(0), ARG(1), 1); if (k < 0) return 0; props[k].value = ARG(2); return 1; }
static uint32_t GetPropA_(Ctx *c) { int k = prop_slot(c, ARG(0), ARG(1), 0); return k < 0 ? 0 : props[k].value; }
static uint32_t RemovePropA_(Ctx *c) {
    int k = prop_slot(c, ARG(0), ARG(1), 0);
    if (k < 0) return 0;
    uint32_t v = props[k].value;
    props[k].hwnd = 0;
    return v;
}
// RECT helpers on {left, top, right, bottom}.
#define RL(r, i) ((int32_t)ld(c, (r) + 4 * (i), 4))
static void rset(Ctx *c, uint32_t r, int32_t l, int32_t t, int32_t rr, int32_t b) {
    st(c, r, (uint32_t)l, 4); st(c, r + 4, (uint32_t)t, 4); st(c, r + 8, (uint32_t)rr, 4); st(c, r + 12, (uint32_t)b, 4);
}
static int rempty(Ctx *c, uint32_t r) { return RL(r, 0) >= RL(r, 2) || RL(r, 1) >= RL(r, 3); }
static uint32_t CopyRect_(Ctx *c) { rset(c, ARG(0), RL(ARG(1), 0), RL(ARG(1), 1), RL(ARG(1), 2), RL(ARG(1), 3)); return 1; }
static uint32_t SetRectEmpty_(Ctx *c) { rset(c, ARG(0), 0, 0, 0, 0); return 1; }
static uint32_t IsRectEmpty_(Ctx *c) { return rempty(c, ARG(0)); }
static uint32_t EqualRect_(Ctx *c) { for (int i = 0; i < 4; i++) if (RL(ARG(0), i) != RL(ARG(1), i)) return 0; return 1; }
static uint32_t OffsetRect_(Ctx *c) {
    int32_t dx = (int32_t)ARG(1), dy = (int32_t)ARG(2);
    rset(c, ARG(0), RL(ARG(0), 0) + dx, RL(ARG(0), 1) + dy, RL(ARG(0), 2) + dx, RL(ARG(0), 3) + dy);
    return 1;
}
static uint32_t InflateRect_(Ctx *c) {
    int32_t dx = (int32_t)ARG(1), dy = (int32_t)ARG(2);
    rset(c, ARG(0), RL(ARG(0), 0) - dx, RL(ARG(0), 1) - dy, RL(ARG(0), 2) + dx, RL(ARG(0), 3) + dy);
    return 1;
}
static uint32_t PtInRect_(Ctx *c) {
    int32_t x = (int32_t)ARG(1), y = (int32_t)ARG(2);       // POINT passed by value
    return x >= RL(ARG(0), 0) && x < RL(ARG(0), 2) && y >= RL(ARG(0), 1) && y < RL(ARG(0), 3);
}
static uint32_t IntersectRect_(Ctx *c) {
    int32_t l = RL(ARG(1), 0) > RL(ARG(2), 0) ? RL(ARG(1), 0) : RL(ARG(2), 0);
    int32_t t = RL(ARG(1), 1) > RL(ARG(2), 1) ? RL(ARG(1), 1) : RL(ARG(2), 1);
    int32_t r = RL(ARG(1), 2) < RL(ARG(2), 2) ? RL(ARG(1), 2) : RL(ARG(2), 2);
    int32_t b = RL(ARG(1), 3) < RL(ARG(2), 3) ? RL(ARG(1), 3) : RL(ARG(2), 3);
    if (l >= r || t >= b) { rset(c, ARG(0), 0, 0, 0, 0); return 0; }
    rset(c, ARG(0), l, t, r, b);
    return 1;
}
static uint32_t UnionRect_(Ctx *c) {
    uint32_t a = ARG(1), b2 = ARG(2);
    if (rempty(c, a) && rempty(c, b2)) { rset(c, ARG(0), 0, 0, 0, 0); return 0; }
    if (rempty(c, a)) a = b2; else if (rempty(c, b2)) b2 = a;
    rset(c, ARG(0), RL(a, 0) < RL(b2, 0) ? RL(a, 0) : RL(b2, 0), RL(a, 1) < RL(b2, 1) ? RL(a, 1) : RL(b2, 1),
         RL(a, 2) > RL(b2, 2) ? RL(a, 2) : RL(b2, 2), RL(a, 3) > RL(b2, 3) ? RL(a, 3) : RL(b2, 3));
    return 1;
}

// WM_CLOSE goes to DestroyWindow; everything else needs no default action in this layer yet.
static uint32_t DefWindowProcA_(Ctx *c) {
    uint32_t msg = ARG(1);
    if (msg == WM_NCCREATE) return 1;
    if (msg == 0x0010) { DestroyWindow_(c); return 0; }
    if (msg == WM_ERASEBKGND) { Window *w = win(ARG(0)); return w && w->cls->brush; }
    if (msg == WM_PAINT) {    // validates the update region
        Window *w = win(ARG(0));
        if (w) w->dirty = w->erase = 0;
        return 0;
    }
    return 0;
}

static void post(uint32_t h, uint32_t msg, uint32_t wp, uint32_t lp) {
    if (qtail - qhead < 256) queue[qtail++ % 256] = (Msg){h, msg, wp, lp};
}
static void fill_msg(Ctx *c, uint32_t p, Msg m) {
    st(c, p, m.hwnd, 4); st(c, p + 4, m.msg, 4); st(c, p + 8, m.wp, 4); st(c, p + 12, m.lp, 4);
    st(c, p + 16, (uint32_t)(now_us() / 1000), 4); st(c, p + 20, 0, 4); st(c, p + 24, 0, 4);
}
// Windows makes WM_PAINT when no other message is waiting and a window has an update region;
// it stays until the region is validated (BeginPaint, ValidateRect), so PM_REMOVE does not drop it.
static int paint_msg(Msg *m) {
    for (int k = 0; k < nwindows; k++)
        if (windows[k].visible && windows[k].dirty) { *m = (Msg){hwnd_of(k), WM_PAINT, 0, 0}; return 1; }
    return 0;
}
// A key goes to the focus window as WM_KEYDOWN/UP, or WM_SYSKEYDOWN/UP for F10 and while ALT is down.
// lParam: repeat count 1, scan code, extended key (bit 24), context = ALT (29), previous state (30), transition (31).
// down 2 = auto-repeat of a held key: WM_KEYDOWN with the previous-state bit set (the game's hook 0x518360 takes repeats).
void user32_key(uint8_t dik, uint8_t vk, int down) {
    if (!focus || !vk) return;
    int sys = input_alt() || vk == 0x79 || vk == 0x12;
    uint32_t msg = (sys ? 0x104u : 0x100u) + (down ? 0 : 1);
    uint32_t lp = 1u | (uint32_t)(dik & 0x7f) << 16 | (dik & 0x80 ? 1u << 24 : 0) | (input_alt() ? 1u << 29 : 0) |
                  (down == 2 ? 1u << 30 : down ? 0 : 3u << 30);
    post(focus, msg, vk, lp);
}
// WH_KEYBOARD (2) hooks see a WM_KEYDOWN/UP the moment GetMessage/PeekMessage takes it from the queue (HC_ACTION 0,
// HC_NOREMOVE 3 for PM_NOREMOVE); a nonzero result drops the message. The game's hook (exe 0x518360, installed by
// Keyboard_Ctor 0x517e70) fills the VK/char ring its play mode reads for Esc, F1, F2, F5 quicksave and F9 quickload.
static int key_hook(Ctx *c, Msg m, int remove) {
    if (m.msg != 0x100 && m.msg != 0x101 && m.msg != 0x104 && m.msg != 0x105) return 0;
    return call_hook(c, next_hook(2, -1), remove ? 0 : 3, m.wp, m.lp) != 0;
}
static uint32_t PeekMessageA_(Ctx *c) {
    uint32_t p = ARG(0), remove = ARG(4) & 1;
    Msg m;
    input_poll();
    while (qhead != qtail) {
        m = queue[qhead % 256];
        int drop = key_hook(c, m, remove);
        if (remove) qhead++;
        if (drop && remove) continue;
        fill_msg(c, p, m);
        return 1;
    }
    if (paint_msg(&m)) { fill_msg(c, p, m); return 1; }
    if (quit_posted) {
        fill_msg(c, p, (Msg){0, WM_QUIT, quit_code, 0});
        if (remove) quit_posted = 0;
        return 1;
    }
    return 0;
}
static uint32_t GetMessageA_(Ctx *c) {
    uint32_t p = ARG(0);
    Msg m;
    input_poll();
    while (qhead != qtail) {
        m = queue[qhead++ % 256];
        if (key_hook(c, m, 1)) continue;
        fill_msg(c, p, m);
        return 1;
    }
    if (paint_msg(&m)) { fill_msg(c, p, m); return 1; }
    if (quit_posted) { fill_msg(c, p, (Msg){0, WM_QUIT, quit_code, 0}); return 0; }
    rn_halt(c, "GetMessageA would wait for input");
    return 0;
}
static uint32_t PostMessageA_(Ctx *c) { post(ARG(0), ARG(1), ARG(2), ARG(3)); return 1; }
static uint32_t capture;
static uint32_t SetCapture_(Ctx *c) { uint32_t old = capture; capture = ARG(0); return old; }
static uint32_t ReleaseCapture_(Ctx *c) { (void)c; capture = 0; return 1; }
// One GUI thread reads messages here: a thread message joins the same queue with no window.
static uint32_t PostThreadMessageA_(Ctx *c) { post(0, ARG(1), ARG(2), ARG(3)); return 1; }
static uint32_t DeleteObject_(Ctx *c) { (void)c; return 1; }                    // GDI objects are not tracked
// ShellExecuteA (the EA web site and such): nothing is launched outside the game; > 32 means success.
static uint32_t ShellExecuteA_(Ctx *c) { rn_log("ShellExecuteA(%s) not launched", G(ARG(2))); return 31; }  // SE_ERR_NOASSOC
// DirectDrawCreate: the game draws with D3D9; DirectDraw reports no hardware.
static uint32_t DirectDrawCreate_(Ctx *c) { (void)c; return 0x887601ffu; }      // DDERR_NODIRECTDRAWHW
static uint32_t PostQuitMessage_(Ctx *c) { quit_posted = 1; quit_code = ARG(0); return 0; }
// WM_KEYDOWN / WM_SYSKEYDOWN of a key that types a character posts WM_CHAR / WM_SYSCHAR (US layout).
static uint32_t TranslateMessage_(Ctx *c) {
    uint32_t p = ARG(0), msg = ld(c, p + 4, 4);
    if (msg != 0x100 && msg != 0x104) return 0;
    char ch = input_char_of((uint8_t)ld(c, p + 8, 4), input_shift());
    if (!ch) return 0;
    post(ld(c, p, 4), msg == 0x100 ? 0x102u : 0x106u, (uint8_t)ch, ld(c, p + 12, 4));
    return 1;
}
static uint32_t DispatchMessageA_(Ctx *c) {
    uint32_t p = ARG(0);
    return send(c, ld(c, p, 4), ld(c, p + 4, 4), ld(c, p + 8, 4), ld(c, p + 12, 4));
}

// Painting. The window's device context is a fixed handle per window: nothing draws with GDI yet.
static uint32_t hdc_of(uint32_t h) { return 0x50000 + (h & 0xffff); }
static uint32_t BeginPaint_(Ctx *c) {
    uint32_t h = ARG(0), ps = ARG(1);
    Window *w = win(h);
    if (!w || !ps) { set_error(c, 1400); return 0; }
    int erase = w->erase;
    w->erase = 0;
    if (erase) erase = !send(c, h, WM_ERASEBKGND, hdc_of(h), 0);    // fErase: the background still needs erasing
    // PAINTSTRUCT: hdc, fErase, rcPaint, fRestore, fIncUpdate, rgbReserved[32]
    memset(G(ps), 0, 64);
    st(c, ps, hdc_of(h), 4);
    st(c, ps + 4, (uint32_t)erase, 4);
    if (w->dirty) for (int k = 0; k < 4; k++) st(c, ps + 8 + 4 * k, (uint32_t)w->upd[k], 4);
    w->dirty = 0;
    return hdc_of(h);
}
static uint32_t EndPaint_(Ctx *c) { return win(ARG(0)) != NULL; }
// RedrawWindow(NULL, NULL, NULL, 0x85): the exe repaints the desktop on exit (0x40b900); this PC has no desktop.
static uint32_t RedrawWindow_(Ctx *c) { (void)c; return 1; }
static uint32_t InvalidateRect_(Ctx *c) {
    uint32_t h = ARG(0), r = ARG(1);
    if (!h) { for (int k = 0; k < nwindows; k++) if (windows[k].visible) invalidate(&windows[k], NULL, ARG(2) != 0); return 1; }
    Window *w = win(h);
    if (!w) { set_error(c, 1400); return 0; }
    int32_t rr[4];
    if (r) for (int k = 0; k < 4; k++) rr[k] = (int32_t)ld(c, r + 4 * k, 4);
    invalidate(w, r ? rr : NULL, ARG(2) != 0);
    return 1;
}
// ValidateRect with a rectangle removes it from the region; the region is one rectangle here, so
// only a rectangle that covers it validates it all.
static uint32_t ValidateRect_(Ctx *c) {
    uint32_t h = ARG(0), r = ARG(1);
    Window *w = win(h);
    if (!w) { set_error(c, 1400); return 0; }
    if (!r) { w->dirty = w->erase = 0; return 1; }
    int32_t a[4];
    for (int k = 0; k < 4; k++) a[k] = (int32_t)ld(c, r + 4 * k, 4);
    if (w->dirty && a[0] <= w->upd[0] && a[1] <= w->upd[1] && a[2] >= w->upd[2] && a[3] >= w->upd[3]) w->dirty = w->erase = 0;
    else if (w->dirty) rn_halt(c, "ValidateRect of part of the update region");
    return 1;
}

static uint32_t GetSystemMetrics_(Ctx *c) {
    switch (ARG(0)) {
    case 0: case 16: return DESK_W;     // SM_CXSCREEN, SM_CXFULLSCREEN
    case 1: return DESK_H;
    case 17: return DESK_H - 19;
    case 4: return 19;                  // SM_CYCAPTION
    case 5: case 6: return 1;           // SM_CXBORDER
    case 7: case 8: return 3;           // SM_CXDLGFRAME
    case 32: case 33: return 4;         // SM_CXFRAME
    case 43: return 3;                  // SM_CMOUSEBUTTONS
    // Windows XP classic metrics at 96 DPI (one monitor, a wheel mouse, no DBCS, no pen).
    case 2: case 3: case 9: case 10: case 20: case 21: return 17;   // scroll bars and thumbs
    case 11: case 12: case 13: case 14: return 32;                  // SM_CXICON, SM_CXCURSOR
    case 15: return 19;                                             // SM_CYMENU
    case 19: case 75: case 81: case 80: return 1;                   // mouse, wheel, same display format, monitors
    case 18: case 22: case 23: case 40: case 41: case 42: case 44: case 67: case 70: case 73: case 74: return 0;
    case 28: case 34: return 112;                                   // SM_CXMIN, SM_CXMINTRACK
    case 29: case 35: return 27;
    case 30: case 31: case 54: case 55: return 18;                  // SM_CXSIZE, SM_CXMENUSIZE
    case 36: case 37: case 68: case 69: return 4;                   // double click, drag
    case 38: case 39: return 75;                                    // SM_CXICONSPACING
    case 45: case 46: return 2;                                     // SM_CXEDGE
    case 47: case 57: return 160;                                   // SM_CXMINSPACING, SM_CXMINIMIZED
    case 48: case 58: return 24;
    case 49: case 50: return 16;                                    // SM_CXSMICON
    case 51: return 15;                                             // SM_CYSMCAPTION
    case 52: case 53: return 12;                                    // SM_CXSMSIZE
    case 56: return 8;                                              // SM_ARRANGE: ARW_HIDE
    case 59: return DESK_W + 12;                                    // SM_CXMAXTRACK
    case 60: return DESK_H + 12;
    case 61: return DESK_W + 8;                                     // SM_CXMAXIMIZED
    case 62: return DESK_H + 8;
    case 63: return 3;                                              // SM_NETWORK
    case 71: case 72: return 13;                                    // SM_CXMENUCHECK
    case 76: case 77: return 0;                                     // virtual screen origin
    case 78: return DESK_W;
    case 79: return DESK_H;
    }
    rn_halt(c, "GetSystemMetrics(%u)", ARG(0));
    return 0;
}
static uint32_t CharUpperA_(Ctx *c) {
    uint32_t p = ARG(0);
    if (p < 0x10000) return (uint32_t)toupper((int)p);
    for (char *s = G(p); *s; s++) if (*s >= 'a' && *s <= 'z') *s -= 32;
    return p;
}
// GetAsyncKeyState: bit 15 = down now. GetKeyState: bit 15 = down, bit 0 = toggled (CAPS LOCK etc.).
// Both return a SHORT (sign-extended here). The thread's key state is the live state: the game reads
// it from the thread that pumps messages.
// GetKeyboardState(BYTE[256]): bit 7 = down, bit 0 = toggled, per VK. ToAscii(vk, scan, state, WORD *out, flags): US layout.
static uint32_t GetKeyboardState_(Ctx *c) {
    if (!ARG(0)) return 0;
    for (uint32_t vk = 0; vk < 256; vk++) {
        uint32_t s = input_vk_state(vk);
        st(c, ARG(0) + vk, (s & 0x8000u ? 0x80u : 0) | (s & 1), 1);
    }
    return 1;
}
static uint32_t ToAscii_(Ctx *c) {
    if (ARG(1) & 0x8000u) return 0;                                  // a key-up scan code types nothing
    char ch = input_char_of((uint8_t)ARG(0), ARG(2) ? (ld(c, ARG(2) + 0x10, 1) & 0x80) != 0 : 0);
    if (!ch || !ARG(3)) return 0;
    st(c, ARG(3), (uint8_t)ch, 2);
    return 1;
}
static uint32_t GetAsyncKeyState_(Ctx *c) { return input_vk_state(ARG(0)) & 0x8000u ? 0xffff8000u : 0; }
static uint32_t GetKeyState_(Ctx *c) { uint32_t s = input_vk_state(ARG(0)); return (s & 0x8000u ? 0xffffff80u : 0) | (s & 1); }
// GetKeyNameTextA: lParam bits 16-23 are the scan code and bit 24 says extended (KEY_NAME, exe 0x519bf1, called when
// the game names the keys of a binding). The names come from the same US table the DirectInput objects use.
static uint32_t GetKeyNameTextA_(Ctx *c) {
    uint32_t lp = ARG(0), buf = ARG(1), n = ARG(2);
    uint8_t dik = (uint8_t)((lp >> 16) & 0xff);
    if (lp & 0x01000000u) dik |= 0x80;
    char name[64];
    input_key_name(dik, name, sizeof name);
    uint32_t len = 0;
    while (name[len]) len++;
    if (n && len >= n) len = n - 1;
    if (buf) { memcpy(G(buf), name, len); G(buf)[len] = 0; }
    return len;
}

// wsprintf: %[-][#][0][width][.prec][l|h]{c,d,i,s,u,x,X,%}; arguments are guest dwords.
static uint32_t format(Ctx *c, uint32_t out, uint32_t fmt, uint32_t ap) {
    char buf[1025];
    size_t n = 0;
    for (const char *f = G(fmt); *f && n < 1024; f++) {
        if (*f != '%') { buf[n++] = *f; continue; }
        char spec[32] = "%";
        size_t sl = 1;
        f++;
        while (*f && strchr("-#0", *f) && sl < 8) spec[sl++] = *f++;
        while (*f >= '0' && *f <= '9' && sl < 16) spec[sl++] = *f++;
        if (*f == '.') { spec[sl++] = *f++; while (*f >= '0' && *f <= '9' && sl < 24) spec[sl++] = *f++; }
        if (*f == 'l' || *f == 'h') f++;
        char conv = *f, tmp[1025];
        if (!conv) break;
        spec[sl++] = conv == 'i' ? 'd' : conv; spec[sl] = 0;
        if (conv == '%') { buf[n++] = '%'; continue; }
        uint32_t v = ld(c, ap, 4);
        ap += 4;
        if (conv == 's') snprintf(tmp, sizeof tmp, spec, v ? G(v) : "(null)");
        else if (conv == 'c') snprintf(tmp, sizeof tmp, spec, (int)(v & 0xff));
        else if (strchr("diuxX", conv)) snprintf(tmp, sizeof tmp, spec, conv == 'd' || conv == 'i' ? (int)v : v);
        else { rn_halt(c, "wsprintf conversion %%%c", conv); return 0; }
        for (char *t = tmp; *t && n < 1024; t++) buf[n++] = *t;
    }
    buf[n] = 0;
    memcpy(G(out), buf, n + 1);
    return (uint32_t)n;
}
static uint32_t wvsprintfA_(Ctx *c) { return format(c, ARG(0), ARG(1), ARG(2)); }
static uint32_t wsprintfA_(Ctx *c) { return format(c, ARG(0), ARG(1), c->r[ESP] + 12); }

#define U(name, n) {"user32.dll", #name, n, name##_}
const Api user32_apis[] = {
    U(SetWindowsHookExA, 4), U(UnhookWindowsHookEx, 1), U(CallNextHookEx, 4),
    U(SetCapture, 1), U(ReleaseCapture, 0), U(PostThreadMessageA, 4),
    {"gdi32.dll", "DeleteObject", 1, DeleteObject_}, {"shell32.dll", "ShellExecuteA", 6, ShellExecuteA_},
    {"ddraw.dll", "DirectDrawCreate", 3, DirectDrawCreate_},
    U(RegisterClassA, 1), U(RegisterClassExA, 1), U(GetClassInfoA, 3), U(FindWindowA, 2), U(SystemParametersInfoA, 4), U(MessageBoxA, 4), U(TranslateAcceleratorA, 3), U(ScreenToClient, 2), U(MapWindowPoints, 4),
    U(IsWindow, 1), U(IsWindowVisible, 1), U(IsWindowEnabled, 1), U(EnableWindow, 2), U(IsZoomed, 1), U(GetParent, 1),
    U(IsChild, 2), U(GetWindow, 2), U(GetTopWindow, 1), U(GetLastActivePopup, 1), U(GetFocus, 0), U(GetActiveWindow, 0),
    U(GetDlgCtrlID, 1), U(GetWindowThreadProcessId, 2), U(SendMessageA, 4), U(CallWindowProcA, 5), U(GetWindowTextA, 3),
    U(GetWindowTextLengthA, 1), U(SetWindowTextA, 2), U(SetPropA, 3), U(GetPropA, 2), U(RemovePropA, 2),
    U(CopyRect, 2), U(SetRectEmpty, 1), U(IsRectEmpty, 1), U(EqualRect, 2), U(OffsetRect, 3), U(InflateRect, 3),
    U(PtInRect, 3), U(IntersectRect, 3), U(UnionRect, 3), U(UnregisterClassA, 2), U(LoadIconA, 2), U(LoadCursorA, 2),
    U(SetCursor, 1), U(ShowCursor, 1), U(SetCursorPos, 2), U(GetCursorPos, 1), U(AdjustWindowRect, 3), U(AdjustWindowRectEx, 4),
    U(CreateWindowExA, 12), U(ShowWindow, 2), U(UpdateWindow, 1), U(SetFocus, 1), U(SetForegroundWindow, 1),
    U(SetActiveWindow, 1), U(GetForegroundWindow, 0), U(GetDesktopWindow, 0), U(GetClientRect, 2),
    U(GetWindowRect, 2), U(SetWindowPos, 7), U(SetRect, 5), U(SetWindowLongA, 3), U(GetWindowLongA, 2),
    U(IsIconic, 1), U(ClientToScreen, 2), U(DestroyWindow, 1), U(DefWindowProcA, 4), U(PeekMessageA, 5),
    U(GetMessageA, 4), U(PostMessageA, 4), U(PostQuitMessage, 1), U(TranslateMessage, 1), U(DispatchMessageA, 1),
    U(GetSystemMetrics, 1), U(CharUpperA, 1), U(GetAsyncKeyState, 1), U(GetKeyboardState, 1), U(ToAscii, 5), U(GetKeyState, 1), U(GetKeyNameTextA, 3),
    U(BeginPaint, 2), U(EndPaint, 2), U(InvalidateRect, 3), U(ValidateRect, 2), U(RedrawWindow, 4),
    U(wvsprintfA, 3), {"user32.dll", "wsprintfA", -1, wsprintfA_},
};
const int user32_api_count = sizeof user32_apis / sizeof user32_apis[0];
