// The keyboard and mouse of this PC. One source feeds them: the browser (web/input.js writes key and
// mouse events into a ring in the shared heap) or, native, a script in RN_KEYS for off-screen runs:
// RN_KEYS="3000:RETURN,4500:DOWN" taps each key (100 ms down) at that many ms after start;
// RN_MOUSE="3000:-200:-40:0,3500:0:0:1" moves the mouse by dx, dy (DirectInput counts) and, with 1, clicks the left
// button (100 ms down) at that many ms.
// input_poll() hands new events to the key state, the DirectInput devices and the window messages.
#include <strings.h>
#include "host.h"

// DirectInput key codes (scan codes; 0x80 = the 0xE0-prefixed extended keys), US layout.
typedef struct { uint8_t dik, vk; char ch, shift_ch; const char *name; } Key;
static const Key keys[] = {
    {0x01, 0x1b, 27, 27, "ESCAPE"}, {0x02, '1', '1', '!', "1"}, {0x03, '2', '2', '@', "2"}, {0x04, '3', '3', '#', "3"},
    {0x05, '4', '4', '$', "4"}, {0x06, '5', '5', '%', "5"}, {0x07, '6', '6', '^', "6"}, {0x08, '7', '7', '&', "7"},
    {0x09, '8', '8', '*', "8"}, {0x0a, '9', '9', '(', "9"}, {0x0b, '0', '0', ')', "0"}, {0x0c, 0xbd, '-', '_', "MINUS"},
    {0x0d, 0xbb, '=', '+', "EQUALS"}, {0x0e, 0x08, 8, 8, "BACK"}, {0x0f, 0x09, 9, 9, "TAB"},
    {0x10, 'Q', 'q', 'Q', "Q"}, {0x11, 'W', 'w', 'W', "W"}, {0x12, 'E', 'e', 'E', "E"}, {0x13, 'R', 'r', 'R', "R"},
    {0x14, 'T', 't', 'T', "T"}, {0x15, 'Y', 'y', 'Y', "Y"}, {0x16, 'U', 'u', 'U', "U"}, {0x17, 'I', 'i', 'I', "I"},
    {0x18, 'O', 'o', 'O', "O"}, {0x19, 'P', 'p', 'P', "P"}, {0x1a, 0xdb, '[', '{', "LBRACKET"}, {0x1b, 0xdd, ']', '}', "RBRACKET"},
    {0x1c, 0x0d, 13, 13, "RETURN"}, {0x1d, 0x11, 0, 0, "LCONTROL"},
    {0x1e, 'A', 'a', 'A', "A"}, {0x1f, 'S', 's', 'S', "S"}, {0x20, 'D', 'd', 'D', "D"}, {0x21, 'F', 'f', 'F', "F"},
    {0x22, 'G', 'g', 'G', "G"}, {0x23, 'H', 'h', 'H', "H"}, {0x24, 'J', 'j', 'J', "J"}, {0x25, 'K', 'k', 'K', "K"},
    {0x26, 'L', 'l', 'L', "L"}, {0x27, 0xba, ';', ':', "SEMICOLON"}, {0x28, 0xde, '\'', '"', "APOSTROPHE"},
    {0x29, 0xc0, '`', '~', "GRAVE"}, {0x2a, 0x10, 0, 0, "LSHIFT"}, {0x2b, 0xdc, '\\', '|', "BACKSLASH"},
    {0x2c, 'Z', 'z', 'Z', "Z"}, {0x2d, 'X', 'x', 'X', "X"}, {0x2e, 'C', 'c', 'C', "C"}, {0x2f, 'V', 'v', 'V', "V"},
    {0x30, 'B', 'b', 'B', "B"}, {0x31, 'N', 'n', 'N', "N"}, {0x32, 'M', 'm', 'M', "M"}, {0x33, 0xbc, ',', '<', "COMMA"},
    {0x34, 0xbe, '.', '>', "PERIOD"}, {0x35, 0xbf, '/', '?', "SLASH"}, {0x36, 0x10, 0, 0, "RSHIFT"},
    {0x37, 0x6a, '*', '*', "MULTIPLY"}, {0x38, 0x12, 0, 0, "LMENU"}, {0x39, 0x20, ' ', ' ', "SPACE"},
    {0x3a, 0x14, 0, 0, "CAPITAL"}, {0x3b, 0x70, 0, 0, "F1"}, {0x3c, 0x71, 0, 0, "F2"}, {0x3d, 0x72, 0, 0, "F3"},
    {0x3e, 0x73, 0, 0, "F4"}, {0x3f, 0x74, 0, 0, "F5"}, {0x40, 0x75, 0, 0, "F6"}, {0x41, 0x76, 0, 0, "F7"},
    {0x42, 0x77, 0, 0, "F8"}, {0x43, 0x78, 0, 0, "F9"}, {0x44, 0x79, 0, 0, "F10"}, {0x45, 0x90, 0, 0, "NUMLOCK"},
    {0x46, 0x91, 0, 0, "SCROLL"}, {0x47, 0x67, 0, 0, "NUMPAD7"}, {0x48, 0x68, 0, 0, "NUMPAD8"}, {0x49, 0x69, 0, 0, "NUMPAD9"},
    {0x4a, 0x6d, '-', '-', "SUBTRACT"}, {0x4b, 0x64, 0, 0, "NUMPAD4"}, {0x4c, 0x65, 0, 0, "NUMPAD5"},
    {0x4d, 0x66, 0, 0, "NUMPAD6"}, {0x4e, 0x6b, '+', '+', "ADD"}, {0x4f, 0x61, 0, 0, "NUMPAD1"}, {0x50, 0x62, 0, 0, "NUMPAD2"},
    {0x51, 0x63, 0, 0, "NUMPAD3"}, {0x52, 0x60, 0, 0, "NUMPAD0"}, {0x53, 0x6e, 0, 0, "DECIMAL"},
    {0x57, 0x7a, 0, 0, "F11"}, {0x58, 0x7b, 0, 0, "F12"},
    {0x9c, 0x0d, 13, 13, "NUMPADENTER"}, {0x9d, 0x11, 0, 0, "RCONTROL"}, {0xb5, 0x6f, '/', '/', "DIVIDE"},
    {0xb7, 0x2c, 0, 0, "SYSRQ"}, {0xb8, 0x12, 0, 0, "RMENU"}, {0xc5, 0x13, 0, 0, "PAUSE"}, {0xc7, 0x24, 0, 0, "HOME"},
    {0xc8, 0x26, 0, 0, "UP"}, {0xc9, 0x21, 0, 0, "PRIOR"}, {0xcb, 0x25, 0, 0, "LEFT"}, {0xcd, 0x27, 0, 0, "RIGHT"},
    {0xcf, 0x23, 0, 0, "END"}, {0xd0, 0x28, 0, 0, "DOWN"}, {0xd1, 0x22, 0, 0, "NEXT"}, {0xd2, 0x2d, 0, 0, "INSERT"},
    {0xd3, 0x2e, 0, 0, "DELETE"}, {0xdb, 0x5b, 0, 0, "LWIN"}, {0xdc, 0x5c, 0, 0, "RWIN"}, {0xdd, 0x5d, 0, 0, "APPS"},
};
#define NKEYS (int)(sizeof keys / sizeof keys[0])
static const Key *key_of(uint8_t dik) {
    for (int k = 0; k < NKEYS; k++) if (keys[k].dik == dik) return &keys[k];
    return NULL;
}

// The key's name as GetKeyNameText gives it for the US layout: the aKeyNames / aKeyNamesExt tables of
// kbdus.c (Windows-driver-samples, input/layout/kbdus), else the key's unshifted character, letters in capitals.
typedef struct { uint8_t sc; const char *name; } Name;
static const Name names[] = {
    {0x01, "Esc"}, {0x0e, "Backspace"}, {0x0f, "Tab"}, {0x1c, "Enter"}, {0x1d, "Ctrl"}, {0x2a, "Shift"},
    {0x36, "Right Shift"}, {0x37, "Num *"}, {0x38, "Alt"}, {0x39, "Space"}, {0x3a, "Caps Lock"}, {0x3b, "F1"},
    {0x3c, "F2"}, {0x3d, "F3"}, {0x3e, "F4"}, {0x3f, "F5"}, {0x40, "F6"}, {0x41, "F7"}, {0x42, "F8"}, {0x43, "F9"},
    {0x44, "F10"}, {0x45, "Pause"}, {0x46, "Scroll Lock"}, {0x47, "Num 7"}, {0x48, "Num 8"}, {0x49, "Num 9"},
    {0x4a, "Num -"}, {0x4b, "Num 4"}, {0x4c, "Num 5"}, {0x4d, "Num 6"}, {0x4e, "Num +"}, {0x4f, "Num 1"},
    {0x50, "Num 2"}, {0x51, "Num 3"}, {0x52, "Num 0"}, {0x53, "Num Del"}, {0x54, "Sys Req"}, {0x57, "F11"},
    {0x58, "F12"},
}, names_ext[] = {
    {0x1c, "Num Enter"}, {0x1d, "Right Ctrl"}, {0x35, "Num /"}, {0x37, "Prnt Scrn"}, {0x38, "Right Alt"},
    {0x45, "Num Lock"}, {0x46, "Break"}, {0x47, "Home"}, {0x48, "Up"}, {0x49, "Page Up"}, {0x4b, "Left"},
    {0x4d, "Right"}, {0x4f, "End"}, {0x50, "Down"}, {0x51, "Page Down"}, {0x52, "Insert"}, {0x53, "Delete"},
    {0x56, "Help"}, {0x5b, "Left Windows"}, {0x5c, "Right Windows"}, {0x5d, "Application"},
};

static uint8_t down[256];      // by DIK
static uint8_t vk_down[256], vk_toggle[256];

// VK state for GetAsyncKeyState/GetKeyState: the generic VK and the left/right ones.
static void set_vk(uint8_t vk, int d) {
    if (d && !vk_down[vk]) vk_toggle[vk] ^= 1;
    vk_down[vk] = (uint8_t)d;
}
uint32_t input_vk_state(uint32_t vk) {
    input_poll();
    if (vk > 255) return 0;
    return (vk_down[vk] ? 0x8000u : 0) | vk_toggle[vk];
}
int input_shift(void) { return vk_down[0x10]; }
int input_caps(void) { return vk_toggle[0x14]; }
int input_alt(void) { return vk_down[0x12]; }
int input_key_down(uint8_t dik) { return down[dik]; }
uint8_t input_vk_of(uint8_t dik) { const Key *k = key_of(dik); return k ? k->vk : 0; }
char input_char_of(uint8_t vk, int shift) {
    for (int k = 0; k < NKEYS; k++)
        if (keys[k].vk == vk && keys[k].ch) {
            char ch = shift ? keys[k].shift_ch : keys[k].ch;
            if (vk >= 'A' && vk <= 'Z' && input_caps()) ch ^= 0x20;
            return ch;
        }
    return 0;
}

// The kbd tables put Pause at 0x45 and Num Lock at E0 45; the DIK codes are the other way round
// (DIK_NUMLOCK 0x45, DIK_PAUSE 0xC5), so those two swap tables.
void input_key_name(uint8_t dik, char *out, int n) {
    int ext = (dik & 0x80) != 0;
    if ((dik & 0x7f) == 0x45) ext = !ext;
    const Name *t = ext ? names_ext : names;
    int nt = ext ? (int)(sizeof names_ext / sizeof *names_ext) : (int)(sizeof names / sizeof *names);
    for (int k = 0; k < nt; k++)
        if (t[k].sc == (dik & 0x7f)) { snprintf(out, n, "%s", t[k].name); return; }
    const Key *k = key_of(dik);
    char ch = k && k->ch > ' ' ? (k->vk >= 'A' && k->vk <= 'Z' ? k->shift_ch : k->ch) : 0;
    snprintf(out, n, "%.*s", ch ? 1 : 0, &ch);
}

static void key_event(uint8_t dik, int d) {
    if (down[dik] == d) return;
    const Key *k = key_of(dik);
    down[dik] = (uint8_t)d;
    if (k) {
        set_vk(k->vk, d);
        static const uint8_t side[][2] = {{0x2a, 0xa0}, {0x36, 0xa1}, {0x1d, 0xa2}, {0x9d, 0xa3}, {0x38, 0xa4}, {0xb8, 0xa5}};
        for (int s = 0; s < 6; s++) if (side[s][0] == dik) set_vk(side[s][1], d);
        if (k->vk == 0x10 || k->vk == 0x11 || k->vk == 0x12)      // the generic VK is down while either side is
            vk_down[k->vk] = (uint8_t)(vk_down[k->vk == 0x10 ? 0xa0 : k->vk == 0x11 ? 0xa2 : 0xa4] |
                                       vk_down[k->vk == 0x10 ? 0xa1 : k->vk == 0x11 ? 0xa3 : 0xa5]);
    }
    if (rn_trace()) rn_log("input: key %02x %s %s", dik, k ? k->name : "?", d ? "down" : "up");
    dinput8_key(dik, d);
    user32_key(dik, k ? k->vk : 0, d);
}
static void button_event(int b, int d) {
    if (b < 0 || b > 7) return;
    static const uint8_t vk[] = {0x01, 0x02, 0x04, 0x05, 0x06, 0, 0, 0};    // VK_LBUTTON, RBUTTON, MBUTTON, X1, X2
    if (vk[b]) set_vk(vk[b], d);
    dinput8_button(b, d);
}
// RN_JUMP '@' lines (host/jump.c): the same events a real key or button sends. name NULL = left mouse button.
int input_script(const char *name, int d) {
    if (!name) { button_event(0, d); return 1; }
    for (int j = 0; j < NKEYS; j++) if (!strcasecmp(keys[j].name, name)) { key_event(keys[j].dik, d); return 1; }
    return 0;
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// u32 words: [0] head (page), [1] tail (game), [2..4] mouse dx, dy, wheel (page adds, game takes),
// [8..8+RING) events: bit 31 = mouse button (else key), bit 9 = auto-repeat of a held key, bit 8 = down, low byte = DIK or button.
#define RING 1024
static uint32_t ring[8 + RING];
EMSCRIPTEN_KEEPALIVE uint32_t rn_input_ring(void) { return (uint32_t)(uintptr_t)ring; }
// [5] = 1 while the game holds the mouse exclusively (DISCL_EXCLUSIVE, acquired): web/input.js hides the page cursor and locks the pointer.
void input_exclusive(int on) { __atomic_store_n(&ring[5], (uint32_t)on, __ATOMIC_RELEASE); }
void input_poll(void) {
    uint32_t head = __atomic_load_n(&ring[0], __ATOMIC_ACQUIRE), tail = ring[1];
    for (; tail != head; tail++) {
        uint32_t e = ring[8 + tail % RING];
        if (e & 0x80000000u) button_event(e & 0xff, (e >> 8) & 1);
        else if (e & 0x200) { if (down[e & 0xff]) user32_key((uint8_t)(e & 0xff), input_vk_of((uint8_t)(e & 0xff)), 2); }   // repeat
        else key_event(e & 0xff, (e >> 8) & 1);
    }
    __atomic_store_n(&ring[1], tail, __ATOMIC_RELEASE);
    int32_t dx = (int32_t)__atomic_exchange_n(&ring[2], 0, __ATOMIC_ACQ_REL);
    int32_t dy = (int32_t)__atomic_exchange_n(&ring[3], 0, __ATOMIC_ACQ_REL);
    int32_t dz = (int32_t)__atomic_exchange_n(&ring[4], 0, __ATOMIC_ACQ_REL);
    if (dx || dy || dz) dinput8_motion(dx, dy, dz);
}
#else
void input_exclusive(int on) { (void)on; }
typedef struct { uint64_t at_us; uint8_t dik; } Tap;
static Tap taps[256];
static int ntaps, script_read;
static unsigned next_tap;
typedef struct { uint64_t at_us; int32_t dx, dy; int click; } Move;
static Move moves[256];
static int nmoves;
static unsigned next_move;
static void read_script(void) {
    script_read = 1;
    const char *s = getenv("RN_KEYS");
    while (s && *s && ntaps < 256) {
        char name[32];
        unsigned ms;
        int used;
        if (sscanf(s, "%u:%31[A-Z0-9_]%n", &ms, name, &used) != 2) { rn_log("RN_KEYS: cannot read \"%s\"", s); break; }
        const Key *k = NULL;
        for (int j = 0; j < NKEYS && !k; j++) if (!strcasecmp(keys[j].name, name)) k = &keys[j];
        if (!k) { rn_log("RN_KEYS: no key %s", name); break; }
        taps[ntaps++] = (Tap){(uint64_t)ms * 1000, k->dik};
        s += used;
        while (*s == ',' || *s == ' ') s++;
    }
    const char *m = getenv("RN_MOUSE");
    while (m && *m && nmoves < 256) {
        unsigned ms;
        int dx, dy, click, used;
        if (sscanf(m, "%u:%d:%d:%d%n", &ms, &dx, &dy, &click, &used) != 4) { rn_log("RN_MOUSE: cannot read \"%s\"", m); break; }
        moves[nmoves++] = (Move){(uint64_t)ms * 1000, dx, dy, click};
        m += used;
        while (*m == ',' || *m == ' ') m++;
    }
}
// Each tap is a down event at its time and an up event 100 ms later, in time order.
void input_poll(void) {
    if (!script_read) read_script();
    uint64_t now = now_us();
    while (next_tap < 2u * (unsigned)ntaps) {
        unsigned k = next_tap / 2;
        uint64_t at = taps[k].at_us + (next_tap & 1 ? 100000 : 0);
        if (at > now) break;
        key_event(taps[k].dik, !(next_tap & 1));
        next_tap++;
    }
    // Each mouse step: the move at its time, then (click) the left button down and, 100 ms later, up.
    while (next_move < 3u * (unsigned)nmoves) {
        unsigned k = next_move / 3, part = next_move % 3;
        uint64_t at = moves[k].at_us + (part == 2 ? 100000 : 0);
        if (at > now) break;
        if (part == 0 && (moves[k].dx || moves[k].dy)) dinput8_motion(moves[k].dx, moves[k].dy, 0);
        if (part == 1 && moves[k].click) button_event(0, 1);
        if (part == 2 && moves[k].click) button_event(0, 0);
        next_move++;
    }
}
#endif
