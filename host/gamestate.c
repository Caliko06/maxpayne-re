// Game state for the ghost driver and the play telemetry: once per frame (gamestate_frame, from Present) the player,
// the enemies and the mode are copied out of the guest into one struct of 32-bit words that the page reads
// (rn_gamestate_addr). Offsets and proofs: rea/map/player.md and rea/map/ai.md. Read-only: nothing here changes the game.
// RN_GHOST=<n>: native prints the struct every n frames (the runtime oracle for the offsets).
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "host.h"

#define GS_VERSION 2
#define GS_NODES 16384
#define GS_ENEMIES 48
#define GS_TRIGGERS 512
#define GS_ROOMS 256
#define GS_PORTALS 1024
#define GS_ITEMS 512

// Word layout (u32 or f32, little endian). The page reads it while the game thread writes: seq is odd during a write
// (seqlock), so a reader retries when seq is odd or changed across its read.
typedef struct {
    uint32_t version;           // 0  GS_VERSION
    uint32_t seq;               // 1  seqlock
    uint32_t frame;             // 2  presents seen
    uint32_t mode;              // 3  active mode: 0 none, 1 menu, 2 graphic novel, 3 game, 4 statistics, 5 HUD, 6 console, 7 other
    uint32_t flags;             // 4  GSF_*; bits 8-11: mode switch transition [ms+0x0d] (1 steady, 2 fade in, 3 fade out)
    uint32_t pending;           // 5  game mode pending action [gm+0xb4]: 1 new level, 2 load, 3/4 save
    float pos[3];               // 6  player position (X_Character +0x72, Y up)
    float rot[9];               // 9  player rotation rows (X_Character +0x4e, +0x5a, +0x66)
    float vel[3];               // 18 player velocity (+0x41e)
    float health;               // 21 player health (+0x2b3)
    int32_t health_base;        // 22 skin's base maximum health [[C+0x718]+0x5d] (before the difficulty multiplier)
    int32_t painkillers;        // 23 0x694630
    int32_t weapon;             // 24 current weapon id (+0x2a9), -1 none
    int32_t clip;               // 25 rounds loaded, both hands (0x6945a0)
    int32_t reserve;            // 26 ammo pool of the current weapon (0x694580)
    float bt_meter;             // 27 bullet-time meter [gm+0x12f0], 0..1
    uint32_t bt_active;         // 28 [gm+0x12f4] (byte)
    uint32_t enemies;           // 29 entries in en[] (the game mode's enemy list, at most GS_ENEMIES)
    uint32_t enemies_alive;     // 30
    uint32_t items;             // 31 level items in the item map
    char level[32];             // 32 current level name [gm+0xcc] (std::string)
    char next_level[32];        // 40 level asked by GM_Init [gm+0xe0] while pending == 1
    struct { float pos[3]; float health; uint32_t flags; uint32_t room; } en[GS_ENEMIES];   // 48: 6 words each, GSE_*
    uint32_t room;              // 336 player's room object [char+0x7e] (positions are in that room's frame)
    uint32_t nav_req;           // 337 the page writes n != nav_done to ask for a node dump on the next frame
    uint32_t nav_done;          // 338 = nav_req once nav[] holds the dump
    uint32_t nav_count;         // 339 nodes in nav[]
    uint32_t nav_addr;          // 340 address of nav[] (NavNode, 10 words each)
    uint32_t trig_count;        // 341 triggers in trig[] (refreshed every frame while a level is up)
    uint32_t trig_addr;         // 342 address of trig[] (Trig, 23 words each)
    float radius;               // 343 player's collision radius (X_Character +0x14a; a trigger fires at r_player + r_trigger)
    float pitch;                // 344 player's aim pitch, radians, + looks down, clamp +-1.5533 (0x7d8c04/08): f32 [[C+0x498]+0x20]
    uint32_t room_count;        // 345 rooms in rooms[] (filled with the nav dump)
    uint32_t room_addr;         // 346 address of rooms[] (Room, 10 words each)
    uint32_t portal_count;      // 347 exits in portals[] (filled with the nav dump)
    uint32_t portal_addr;       // 348 address of portals[] (Portal, 19 words each)
    uint32_t item_count;        // 349 level items in item[] (refreshed every frame while a level is up)
    uint32_t item_addr;         // 350 address of item[] (Item, 15 words each)
    uint32_t control;           // 351 GSC_*: player input linked (GM_SetPlayerControls), script camera busy, inventory overlay
    uint32_t en_name_addr;      // 352 address of en_name[] (64 chars per en[] entry: the level instance name, "::r01::helper")
    // Systems table (census/systems_table.tsv, rea/map/mechanics.md for each address). Printed by RN_SYS=<n>.
    float pool;                 // 353 pending heal [C+0x72c]
    float speed;                // 354 game speed factor [GM+0x63]
    float sdt, rdt;             // 355 scaled dt [GM+0x5b], 356 real dt [GM+0x67]
    uint32_t bt_input;          // 357 button tracker [GM+0x1258]
    uint32_t sd_count;          // 358 shoot-dodge counter [GM+0x1304]
    uint32_t cflags;            // 359 bytes: C+0x454 immortal, +0x455 dodge immortal, +0x456 invulnerable, C+0x758 sniper mode
    uint32_t stflags;           // 360 bytes: ST+0x6f shoot-dodge allowed, ST+0x70 started, GM+0x12fc block, GM+0x1254 zoom allowed
    uint32_t zoom;              // 361 zoom state [GM+0x124c]
    float z;                    // 362 zoom factor [GM+0x1250]
    float fovmsg;               // 363 [GM+0x123c]
    float shake_t, shake_p;     // 364 [C+0x750], 365 [C+0x754]
    float fire_mult;            // 366 [C+0x2b8]
} GameState;
// One node of the AI movement network (rea/map/ai.md, navigation): what the driver path-finds over, as 0x438820 does.
typedef struct { int32_t id; int32_t kind; uint32_t room; float pos[3]; int32_t nb[4]; } NavNode;
static NavNode nav[GS_NODES];
// One X_LevelRuntimeTrigger (rea/map/ai.md, triggers): a sphere in its room's frame; a player-collision one (type 1) fires
// T_Activate on the FSM of the same name when the player's sphere touches it (0x71f120 -> 0x71f0d0), if enabled and not fired.
typedef struct { uint32_t room; float pos[3]; float r; int32_t type; uint8_t enabled, fired, pad[2]; char name[64]; } Trig;
static Trig trig[GS_TRIGGERS];
// Rooms and the exits (portals) between them (rea/map/ai.md, rooms). A room has no placement of its own: each exit maps its
// owner room A's frame to its destination B's (p_B = p_A*R + t, 0x646020), so a BFS over exits puts every room in one frame.
typedef struct { uint32_t ptr; int32_t idx; char name[32]; } Room;
typedef struct { int32_t a, b; uint32_t ok; uint32_t enabled; float c[3]; float t[3]; float R[9]; } Portal;
// One X_LevelItem (rea/map/ai.md, level items): a pickup in its room's frame; picked (or not placed yet) when room == 0.
typedef struct { uint32_t room; float pos[3]; float r; int32_t weapon; uint8_t active, pad[3]; char name[32]; } Item;
static Item item[GS_ITEMS];
// Instance names of en[]: the object id at C+0x198 (copied by the X_Character init 0x683f50 @0x683f79) indexes the object
// table [0x8b84cd] (13-byte entries, valid byte +0xc); the name the script uses ("::room::name") is the char* at entry+4
// (set by 0x459570 -> 0x471830, read by 0x459590; name -> character lookup 0x4569f0).
static char en_name[GS_ENEMIES][64];   // 64: a nested instance name passes 31 chars, and a truncated one closes the game when a script line names it
static Room rooms[GS_ROOMS];
static Portal portals[GS_PORTALS];

enum { GSF_LEVEL = 1, GSF_PLAYER = 2, GSF_DEAD = 4, GSF_BT = 8 };
// Damage gates of rea/map/damage_path.md: projectile collisions off C+0x22d (0x69cb80), immortal C+0x454 (0x69a800),
// invulnerable C+0x456 (0x69a840).
enum { GSE_DEAD = 1, GSE_AI_ACTIVE = 2, GSE_NOHIT = 4, GSE_IMMORTAL = 8, GSE_INVULN = 16 };
// Player control (rea/map/player.md): GM_SetPlayerControls 0x456650 links the player's input [W+0x1c9] (W = [GM+0x1080]) to
// GM+0x258 or 0; MaxPayne_LocalCharacterInput (0x5030e0) reads nothing while it is 0. Camera X_CameraImplementation
// CI = [GM+0x93]: busy when [CI+0x13f] && !u8 CI+0x14b && !u8 CI+0x14d (slot +0xc), or u8 CI+0x14e (slot +8). u8 GM+0x12ce:
// inventory overlay open (unlinks the input).
enum { GSC_INPUT = 1, GSC_CAMERA = 2, GSC_OVERLAY = 4 };

static GameState pub, gs;   // pub: what the page reads; gs: built here, then published under the seqlock

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EMSCRIPTEN_KEEPALIVE uint32_t rn_gamestate_addr(void) { return (uint32_t)(uintptr_t)&pub; }
#endif
// For host/jump.c: the last published snapshot, read on the game thread (no seqlock needed there).
void gamestate_view(uint32_t *mode, uint32_t *fade, int *level_up, int *control, const char **level, uint32_t *frame) {
    *mode = pub.mode; *fade = (pub.flags >> 8) & 15; *level_up = !!(pub.flags & GSF_LEVEL);
    *control = !!(pub.control & GSC_INPUT); *level = pub.level; *frame = pub.frame;
}

// A guest pointer that can be read n bytes from (the game's objects live in the image or the heap).
static int ok(Ctx *c, uint32_t p, uint32_t n) { return p >= IMAGE_BASE && p < c->memsz - n; }
static uint32_t rd(Ctx *c, uint32_t p) { return ok(c, p, 4) ? ld(c, p, 4) : 0; }

// MSVC 6 std::string: chars at +4, length at +8 (0x44fda0 sets [gm+0xcc]).
static void copy_str_n(Ctx *c, char *dst, uint32_t chars, uint32_t len, uint32_t max) {
    if (len > max) len = max;
    if (!ok(c, chars, len + 1)) len = 0;
    if (len) memcpy(dst, c->mem + chars, len);
    dst[len] = 0;
}
static void copy_str(Ctx *c, char *dst, uint32_t chars, uint32_t len) { copy_str_n(c, dst, chars, len, 31); }

// Integer getter of the original (thiscall, no arguments, result in EAX).
static int32_t getter(Ctx *c, uint32_t fn, uint32_t self) {
    uint32_t ecx = c->r[ECX];
    c->r[ECX] = self;
    int32_t v = (int32_t)call_guest(c, fn, 0, NULL);
    c->r[ECX] = ecx;
    return v;
}

static int mode_of(Ctx *c, uint32_t ms) {
    uint32_t cur = rd(c, ms + 0x19);                                     // current mode (X_ModeSwitch)
    static const uint32_t slot[] = {0x7c, 0x84, 0x80, 0x74, 0x78, 0x70}; // menu, GN, game, statistics, HUD, console (0x415dc0)
    if (!cur) return 0;
    for (int k = 0; k < 6; k++) if (rd(c, ms + slot[k]) == cur) return k + 1;
    return 7;
}

// cell: a guest dword holding the X_Character* (the player wrapper +8), the "this" of the HUD's getters 0x421f00..
static void player(Ctx *c, uint32_t cell, uint32_t ch) {
    gs.flags |= GSF_PLAYER;
    memcpy(gs.pos, c->mem + ch + 0x72, 12);         // 0x642d50, crosshair 0x68ebd0
    memcpy(gs.rot, c->mem + ch + 0x4e, 36);         // 3x4 matrix at +0x4e, translation last
    memcpy(gs.vel, c->mem + ch + 0x41e, 12);        // 0x694540
    gs.health = ldf(c, ch + 0x2b3);                 // 0x694470 -> 0x694430 (+0x22e) -> 0x6d71b0 (+0x85)
    if (c->mem[ch + 0x235]) gs.flags |= GSF_DEAD;   // set by the death routine 0x6963f0, read by 0x6d6eb0
    uint32_t prm = rd(c, ch + 0x718);               // 0x691700
    gs.health_base = prm ? (int32_t)rd(c, prm + 0x5d) : 0;   // 0x691bd0
    gs.weapon = (int32_t)rd(c, ch + 0x2a9);         // 0x6d6fb0 via 0x694570
    if (rd(c, ch + 0x398)) {                        // weapon set (0x6916e0): the HUD ammo routine 0x4afcf0 calls these wrappers
        gs.painkillers = getter(c, 0x421f20, cell); // -> 0x694630
        gs.reserve = getter(c, 0x421f00, cell);     // -> 0x694580
        gs.clip = getter(c, 0x421f10, cell);        // -> 0x6945a0 -> 0x729ce0
    }
}

static int32_t en_base[GS_ENEMIES], en_weap[GS_ENEMIES];
static void enemies(Ctx *c, uint32_t gm) {
    uint32_t b = rd(c, gm + 0x24c), e = rd(c, gm + 0x250);              // vector of MaxPayne_ArtificialPlayerInput* (0x450287)
    if (!b || e < b || e - b > 4096 * 4) return;
    for (uint32_t p = b; p < e; p += 4) {
        uint32_t ai = rd(c, p), ch = ai ? rd(c, ai + 0x533) : 0;      // its X_Character
        if (!ok(c, ch, 0x800)) continue;
        int dead = c->mem[ch + 0x235] != 0;
        if (!dead) gs.enemies_alive++;
        if (gs.enemies >= GS_ENEMIES) continue;
        typeof(gs.en[0]) *x = &gs.en[gs.enemies++];
        memcpy(x->pos, c->mem + ch + 0x72, 12);
        x->health = ldf(c, ch + 0x2b3);
        x->flags = (dead ? GSE_DEAD : 0) | (c->mem[ch + 0x2dc] ? GSE_AI_ACTIVE : 0)
                 | (c->mem[ch + 0x22d] ? 0 : GSE_NOHIT) | (c->mem[ch + 0x454] ? GSE_IMMORTAL : 0) | (c->mem[ch + 0x456] ? GSE_INVULN : 0);
        x->room = rd(c, ch + 0x7e);
        uint32_t prm = rd(c, ch + 0x718);
        en_base[gs.enemies - 1] = prm ? (int32_t)rd(c, prm + 0x5d) : 0; en_weap[gs.enemies - 1] = (int32_t)rd(c, ch + 0x2a9);
        uint32_t id = rd(c, ch + 0x198), tab = rd(c, 0x8b84cd), nm = 0;   /* 0x683f79, 0x459590 */
        char *dst = en_name[gs.enemies - 1]; dst[0] = 0;
        if (id != 0xffffffffu && tab && ok(c, tab + 13 * id, 13) && c->mem[tab + 13 * id + 0xc]) nm = rd(c, tab + 13 * id + 4);
        if (nm && ok(c, nm, 64)) copy_str_n(c, dst, nm, (uint32_t)strnlen((char *)c->mem + nm, 63), 63);
    }
}

// Every X_MovementNode of the level: std::map<int, X_MovementNode*> at 0x8a6a08 (head [0x8a6a0c], nil [0x8a6a30], size
// [0x8a6a14]; tree node: left +0, parent +4, right +8, key +0xc, value +0x10). Node (0x6c bytes, loader 0x43e690): kind +8
// (4 room grid node, 2 portal node), pos +0xc (room frame; nearest-node 0x444f50 reads it), neighbour ids +0x18 (4, BFS 0x438820
// walks the pointers at +0x28), room = [[node+0x4c]+0x1c] (0x43f170 -> 0x63a320).
static uint32_t nav_walk(Ctx *c, uint32_t t, uint32_t nil, uint32_t n, int depth) {
    if (!t || t == nil || depth > 64 || n >= GS_NODES) return n;
    n = nav_walk(c, rd(c, t), nil, n, depth + 1);
    uint32_t nd = rd(c, t + 0x10);
    if (nd && ok(c, nd, 0x6c) && n < GS_NODES) {
        NavNode *v = &nav[n++];
        v->id = (int32_t)rd(c, t + 0xc);         // the map key = node id (the ids at +0x18 refer to it)
        v->kind = (int32_t)rd(c, nd + 8);
        memcpy(v->pos, c->mem + nd + 0xc, 12);
        for (int k = 0; k < 4; k++) v->nb[k] = (int32_t)rd(c, nd + 0x18 + 4 * k);
        uint32_t arr = rd(c, nd + 0x4c);
        v->room = arr ? rd(c, arr + 0x1c) : 0;
    }
    return nav_walk(c, rd(c, t + 8), nil, n, depth + 1);
}

// Level items: std::map<X_LevelItem*, int> at im+0xc, im = [GM+0x97] (head [im+0x10], root [head+4], nil [0x8b4124];
// node: left +0, parent +4, right +8, item +0xc, type id +0x10; walked in order as 0x66f5c0 does). Item: pos f32x3 +0x54,
// room [+0x60] (0 = picked, 0x642a80), contact radius f32 +0x12c, active u8 +0x142 (picked by contact only while set:
// 3D sphere test r_char + r_item in 0x694d30, set at [C+0x804], given by 0x68c690), type X_SharedDBLevelItem T = [+0x13a]: name rs chars [T+4] len [T+8] (the
// Level_Items file: beretta, ammo_beretta, medipack_small...), TriggerRadius f32 T+0x34, MatchingWeaponID i32 T+0x38 (parser 0x5443e0).
static uint32_t items_walk(Ctx *c, uint32_t t, uint32_t nil, uint32_t n, int depth) {
    if (!t || t == nil || depth > 64 || n >= GS_ITEMS) return n;
    n = items_walk(c, rd(c, t), nil, n, depth + 1);
    uint32_t it = rd(c, t + 0xc), T = rd(c, it + 0x13a);
    if (ok(c, it, 0x148) && ok(c, T, 0x5a) && n < GS_ITEMS) {
        Item *v = &item[n++];
        v->room = rd(c, it + 0x60);
        memcpy(v->pos, c->mem + it + 0x54, 12);
        v->r = ldf(c, it + 0x12c);               // TriggerRadius T+0x34 copied by 0x658b80
        v->active = c->mem[it + 0x142];          // pickup gate in 0x694d30 (with the player's u8 C+0x7c0)
        v->weapon = (int32_t)rd(c, T + 0x38);
        copy_str(c, v->name, rd(c, T + 4), rd(c, T + 8));
    }
    return items_walk(c, rd(c, t + 8), nil, n, depth + 1);
}

// Level L is embedded at GM+0x104 (load 0x493f80 called at 0x45008e with ecx=GM+0x104); its trigger vector is at L+0xb7
// (begin [L+0xbb], end [L+0xbf]; filled at 0x49481d). Object fields: tag [t+0x150]=0x7d880c (ctor 0x71e680), centre +0x72 in
// room [t+0x7e], radius +0x14a (0x658b80), type [[t+0x46a]+0x9c] (0x71f070 -> 0x40e480), enabled +0x48e / fired +0x48f
// (T_Enable 0x71f540, fire 0x71f0d0), key string "::room::name.TRIGGER" at chars [t+0x472], length [t+0x476] (0x71e830).
static uint32_t triggers(Ctx *c, uint32_t L) {
    uint32_t b = rd(c, L + 0xbb), e = rd(c, L + 0xbf), n = 0;
    if (e < b || e - b > 4 * 8192) return 0;
    for (uint32_t p = b; p < e && n < GS_TRIGGERS; p += 4) {
        uint32_t t = rd(c, p);
        if (!ok(c, t, 0x490) || rd(c, t + 0x150) != 0x7d880c) continue;
        Trig *g = &trig[n++];
        g->room = rd(c, t + 0x7e);
        memcpy(g->pos, c->mem + t + 0x72, 12);
        g->r = ldf(c, t + 0x14a);
        uint32_t rec = rd(c, t + 0x46a);
        g->type = ok(c, rec, 0xa0) ? (int32_t)rd(c, rec + 0x9c) : -1;
        g->enabled = c->mem[t + 0x48e]; g->fired = c->mem[t + 0x48f];
        copy_str_n(c, g->name, rd(c, t + 0x472), rd(c, t + 0x476), 63);
    }
    return n;
}

// Rooms: vector<X_LevelRuntimeRoom*> begin [L+0x41], end [L+0x45] (built by 0x655f90, called at 0x4942f6); LevelDB record
// [room+8], index [db+0x184], name chars [db+0x14c] len [db+0x150]. Exits: vector<X_LevelRuntimeExit*> [L+0x82]..[L+0x86]
// (0x4943e5); record E = [X+0xb] (ctor 0x657810, back pointer [E+0xc8] = X at 0x4943dd); owner/destination room records
// [E+0x7c]/[E+0x80] (index at +0x184), paired exit [E+0x84], polygon f32x3 from [E+0xc] to [E+0x10] in A's frame,
// R f32[9] at E+0x24 and t at E+0x48 (0x646020 @0x6460c9..0x64615c), enabled u8 [X+5] (GM_EnableExit).
static void rooms_portals(Ctx *c, uint32_t L, uint32_t *nr, uint32_t *np) {
    uint32_t b = rd(c, L + 0x41), e = rd(c, L + 0x45), n = 0;
    for (uint32_t p = b; e >= b && p < e && n < GS_ROOMS; p += 4) {
        uint32_t r = rd(c, p), db = r ? rd(c, r + 8) : 0;
        Room *o = &rooms[n++];
        o->ptr = r; o->idx = ok(c, db, 0x188) ? (int32_t)rd(c, db + 0x184) : -1;
        if (ok(c, db, 0x154)) copy_str(c, o->name, rd(c, db + 0x14c), rd(c, db + 0x150)); else o->name[0] = 0;
    }
    *nr = n;
    b = rd(c, L + 0x82); e = rd(c, L + 0x86); n = 0;
    for (uint32_t p = b; e >= b && p < e && n < GS_PORTALS; p += 4) {
        uint32_t x = rd(c, p), E = x ? rd(c, x + 0xb) : 0;
        if (!ok(c, E, 0xcc)) continue;
        Portal *o = &portals[n++];
        uint32_t ra = rd(c, E + 0x7c), rb = rd(c, E + 0x80), pr = rd(c, E + 0x84);
        o->a = ok(c, ra, 0x188) ? (int32_t)rd(c, ra + 0x184) : -1;
        o->b = ok(c, rb, 0x188) ? (int32_t)rd(c, rb + 0x184) : -1;
        o->ok = (rd(c, E + 0xc8) == x) | (ok(c, pr, 0x88) && rd(c, pr + 0x84) == E) << 1;   // the RE's two asserts
        o->enabled = c->mem[x + 5];
        memcpy(o->R, c->mem + E + 0x24, 36); memcpy(o->t, c->mem + E + 0x48, 12);
        uint32_t vb = rd(c, E + 0xc), ve = rd(c, E + 0x10), k = 0;
        o->c[0] = o->c[1] = o->c[2] = 0;
        for (uint32_t v = vb; ve >= vb && v < ve && k < 64 && ok(c, v, 12); v += 12, k++)
            for (int i = 0; i < 3; i++) o->c[i] += ldf(c, v + 4 * i);
        if (k) for (int i = 0; i < 3; i++) o->c[i] /= (float)k;
    }
    *np = n;
}

// Fields of the systems table that change per frame.
static void sys_read(Ctx *c, uint32_t gm, uint32_t ch) {
    gs.pool = ldf(c, ch + 0x72c); gs.speed = ldf(c, gm + 0x63); gs.sdt = ldf(c, gm + 0x5b); gs.rdt = ldf(c, gm + 0x67);
    gs.bt_input = rd(c, gm + 0x1258); gs.sd_count = rd(c, gm + 0x1304);
    gs.cflags = c->mem[ch + 0x454] | c->mem[ch + 0x455] << 8 | c->mem[ch + 0x456] << 16 | (uint32_t)c->mem[ch + 0x758] << 24;
    gs.stflags = c->mem[ch + 0x22e + 0x6f] | c->mem[ch + 0x22e + 0x70] << 8 | c->mem[gm + 0x12fc] << 16 | (uint32_t)c->mem[gm + 0x1254] << 24;
    gs.zoom = rd(c, gm + 0x124c); gs.z = ldf(c, gm + 0x1250); gs.fovmsg = ldf(c, gm + 0x123c);
    gs.shake_t = ldf(c, ch + 0x750); gs.shake_p = ldf(c, ch + 0x754); gs.fire_mult = ldf(c, ch + 0x2b8);
}

// RN_SYS=<ms>: "S" every ms milliseconds, "H"/"E" at once when the player's or a foe's hp moves, "D" once a level, "W" per weapon.
static void sys_log(Ctx *c, uint32_t gm, uint32_t ch, int every) {
    static float last_hp, en_hp[GS_ENEMIES];
    static uint32_t last_en;
    static int32_t last_w = -2;
    static char last_level[32];
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    double t = (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
    uint32_t f = gs.frame, A = rd(c, ch + 0x398);
    uint32_t D = rd(c, ch + 0x490), mode = D ? rd(c, D + 0xf9) : 0, idx = D ? rd(c, D + 0xf5) : 0;
    static uint32_t last_idx = 0xffffffffu;
    if (strcmp(last_level, gs.level)) { snprintf(last_level, sizeof last_level, "%s", gs.level); last_w = -2; last_en = 0; last_idx = 0xffffffffu; }
    if (idx != last_idx) {
        last_idx = idx;
        static const uint32_t pk[] = {5, 0x41, 0x7d, 0xb9};                 /* 0x6760a0 */
        // 0x6760e0: entry = [P+0x20] + 32 * [D+0xf5], P = the mode's props (0x6760a0)
        uint32_t E = mode >= 1 && mode <= 4 ? rd(c, D + pk[mode - 1] + 0x20) + 32 * idx : 0;
        uint32_t cfg = rd(c, gm + 0x10cc);
        rn_log("D f%u level %s mode %u idx %u E %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f painkiller %.3f cfg %g %g %g %g %g %g fall %g %g %g %g", f, gs.level, mode, idx,
               ldf(c, E), ldf(c, E + 4), ldf(c, E + 8), ldf(c, E + 12), ldf(c, E + 16), ldf(c, E + 20), ldf(c, E + 24), ldf(c, E + 28),
               mode >= 1 && mode <= 4 ? ldf(c, D + pk[mode - 1] + 0xc) : 0.0f,
               ldf(c, cfg), ldf(c, cfg + 4), ldf(c, cfg + 8), ldf(c, cfg + 12), ldf(c, cfg + 16), ldf(c, cfg + 20),
               ldf(c, ch + 0x444), ldf(c, ch + 0x448), ldf(c, ch + 0x44c), ldf(c, ch + 0x450));
    }
    if (gs.weapon != last_w && A) {
        last_w = gs.weapon;
        uint32_t wd = rd(c, A + 8) != 0xffffffffu ? (uint32_t)getter(c, 0x7299e0, A) : 0;   /* 0x691a40 */
        uint32_t pool = wd ? rd(c, A + 0x10) + 0x1e * rd(c, wd + 0x3d) : 0;                 /* 0x7299b0 */
        rn_log("W f%u weapon %d clip %d res %d fov %.3f freq %.3f shake %.3f %.3f %.3f inv %d pocket %d pool %d %d %d inf %d", f, gs.weapon, gs.clip, gs.reserve,
               wd ? ldf(c, wd + 0x6e) : 0.0f, wd ? ldf(c, wd + 0x5d) : 0.0f, wd ? ldf(c, wd + 0x72) : 0.0f, wd ? ldf(c, wd + 0x76) : 0.0f, wd ? ldf(c, wd + 0x86) : 0.0f,
               wd ? (int32_t)rd(c, wd + 0x3d) : -1, wd ? (int32_t)rd(c, wd + 0x49) : -1,
               pool ? (int32_t)rd(c, pool) : -1, pool ? (int32_t)rd(c, pool + 4) : -1, pool ? (int32_t)rd(c, pool + 8) : -1, pool ? c->mem[pool + 0xc] : -1);
    }
    if (gs.health != last_hp) { rn_log("H f%u t %.3f hp %.3f -> %.3f pool %.3f fl %x w %d", f, t, last_hp, gs.health, gs.pool, gs.cflags, gs.weapon); last_hp = gs.health; }
    {   // 0x68c960 end: C+0x436 = velocity along the contact normal, the fall damage input
        float a = ldf(c, ch + 0x436), b = ldf(c, ch + 0x43a), d = ldf(c, ch + 0x43e), v = sqrtf(a * a + b * b + d * d);
        if (v > 3) rn_log("V f%u t %.3f v %.2f y %.2f hp %.2f", f, t, v, gs.pos[1], gs.health);
    }
    for (uint32_t k = 0; k < gs.enemies; k++) {
        typeof(gs.en[0]) *x = &gs.en[k];
        if (k >= last_en) rn_log("EN f%u %u %s hp %.2f base %d w %d", f, k, en_name[k], x->health, en_base[k], en_weap[k]);
        else if (x->health != en_hp[k]) {
            float dx = x->pos[0] - gs.pos[0], dy = x->pos[1] - gs.pos[1], dz = x->pos[2] - gs.pos[2];
            rn_log("E f%u t %.3f %u %s hp %.2f -> %.2f base %d my_w %d d %.1f bt %.3f", f, t, k, en_name[k], en_hp[k], x->health, en_base[k], gs.weapon, sqrt(dx*dx+dy*dy+dz*dz), gs.bt_meter);
        }
        en_hp[k] = x->health;
    }
    last_en = gs.enemies;
    static double next_s;
    if (t >= next_s) { next_s = t + every * 1e-3;
        static const char *en_watch; if (!en_watch) en_watch = getenv("RN_SYS_EN") ? getenv("RN_SYS_EN") : "";
        for (uint32_t k = 0; *en_watch && k < gs.enemies; k++)    // RN_SYS_EN=<name part>: where a scripted actor walks (#24 dummy Max)
            if (strstr(en_name[k], en_watch)) rn_log("EP f%u %s p %.2f %.2f %.2f hp %.2f room %x", f, en_name[k], gs.en[k].pos[0], gs.en[k].pos[1], gs.en[k].pos[2], gs.en[k].health, gs.en[k].room);
        rn_log("S f%u t %.3f hp %.2f pool %.3f pk %d w %d clip %d res %d bt %.4f %u spd %.3f sdt %.4f rdt %.4f in %u sd %u cf %x st %x zs %u z %.3f fm %.2f sh %.3f %.3f fr %.3f v %.2f %.2f %.2f p %.2f %.2f %.2f rm %x al %u ctl %u",
               f, t, gs.health, gs.pool, gs.painkillers, gs.weapon, gs.clip, gs.reserve, gs.bt_meter, gs.bt_active, gs.speed, gs.sdt, gs.rdt, gs.bt_input, gs.sd_count, gs.cflags, gs.stflags,
               gs.zoom, gs.z, gs.fovmsg, gs.shake_t, gs.shake_p, gs.fire_mult, gs.vel[0], gs.vel[1], gs.vel[2], gs.pos[0], gs.pos[1], gs.pos[2], gs.room, gs.enemies_alive, gs.control); }
}

// RN_JUMP "@aim" (host/jump.c): mouse counts toward the nearest live foe in the player's room, from the published snapshot.
// Forward is rotation row 2; 6.288e-3 rad per count at the default slider, + is right / down (measured by ghost calibrate).
int gamestate_aim(float up, int *dx, int *dy) {
    const typeof(pub.en[0]) *b = NULL; float best = 1e9f;
    for (uint32_t k = 0; k < pub.enemies && k < GS_ENEMIES; k++) {
        const typeof(pub.en[0]) *x = &pub.en[k];
        if ((x->flags & GSE_DEAD) || x->room != pub.room) continue;
        float ex = x->pos[0] - pub.pos[0], ez = x->pos[2] - pub.pos[2], d = ex * ex + ez * ez;
        if (d < best) { best = d; b = x; }
    }
    if (!b) return 0;
    float ex = b->pos[0] - pub.pos[0], ez = b->pos[2] - pub.pos[2];
    float err = atan2f(ex, ez) - atan2f(pub.rot[6], pub.rot[8]);
    err = atan2f(sinf(err), cosf(err));
    float want = atan2f(pub.pos[1] - (b->pos[1] + up), fmaxf(sqrtf(ex * ex + ez * ez), 0.3f));
    float cx = err / 6.288e-3f * 0.4f, cy = (want - pub.pitch) / 6.284e-3f * 0.4f;
    *dx = (int)fminf(fmaxf(cx, -150), 150); *dy = (int)fminf(fmaxf(cy, -150), 150);
    return 1;
}

void gamestate_frame(Ctx *c) {
    uint32_t frame = gs.frame + 1;
    memset(&gs, 0, sizeof gs);
    gs.version = GS_VERSION; gs.frame = frame;
    uint32_t ms = rd(c, 0x8a6338 + 0xc8);           // theApp 0x8a6338 (0x40a180) + 0xc8: MaxPayne_ModeSwitch (0x40b3a2)
    uint32_t gm = ms ? rd(c, ms + 0x80) : 0;        // MaxPayne_GameMode (0x4170f9)
    if (ms) { gs.mode = (uint32_t)mode_of(c, ms); gs.flags |= (uint32_t)(c->mem[ms + 0x0d] & 0xf) << 8; }   // 0x601250
    // RN_NOCURSOR (a touch screen, web/index.html): no finger needs the menu's arrow. Its sprite is mode+0x9e, placed by the menu update
    // 0x6313e0 (+0x188, +0x18c); zero size (+0x144, +0x148) draws nothing and leaves the position the buttons test.
    static int nocursor = -1;
    if (nocursor < 0) nocursor = getenv("RN_NOCURSOR") != NULL;
    if (nocursor && ms) {
        uint32_t cur = rd(c, ms + 0x19), sprite = cur && rd(c, cur) == 0x7d4670 ? rd(c, cur + 0x9e) : 0;
        if (sprite) { st(c, sprite + 0x144, 0, 4); st(c, sprite + 0x148, 0, 4); }
    }
    if (ok(c, gm, 0x1300)) {
        gs.pending = rd(c, gm + 0xb4);
        uint32_t nx = rd(c, gm + 0xe0);             // GM_Init handler 0x4555f0/0x455890: name of the level asked for
        if (gs.pending == 1 && ok(c, nx, 32)) copy_str(c, gs.next_level, nx, (uint32_t)strnlen((char *)c->mem + nx, 31));
        copy_str(c, gs.level, rd(c, gm + 0xd0), rd(c, gm + 0xd4));
        gs.bt_meter = ldf(c, gm + 0x12f0);          // 0x44c1b0, HUD 0x4aee20, save 0x454fa0
        gs.bt_active = c->mem[gm + 0x12f4];         // toggle 0x44ebb0
        if (gs.bt_active) gs.flags |= GSF_BT;
        uint32_t pw = rd(c, gm + 0x1080);           // 0x451e50
        if (rd(c, gm + 0x1074) && pw) {
            gs.flags |= GSF_LEVEL;
            uint32_t ch = rd(c, pw + 8);            // 0x451e60
            if (ok(c, ch, 0x800)) { player(c, pw + 8, ch); gs.room = rd(c, ch + 0x7e); gs.radius = ldf(c, ch + 0x14a);
                uint32_t aim = rd(c, ch + 0x498);   // getter 0x6916d0; pitch accumulated by 0x725b30, read by 0x725be0
                if (ok(c, aim, 0x24)) gs.pitch = ldf(c, aim + 0x20); }
            enemies(c, gm);
            if (ok(c, ch, 0x800)) sys_read(c, gm, ch);
            if (rd(c, pw + 0x1c9) == gm + 0x258) gs.control |= GSC_INPUT;
            uint32_t ci = rd(c, gm + 0x93);
            if (ok(c, ci, 0x150) && ((rd(c, ci + 0x13f) && !c->mem[ci + 0x14b] && !c->mem[ci + 0x14d]) || c->mem[ci + 0x14e])) gs.control |= GSC_CAMERA;
            if (c->mem[gm + 0x12ce]) gs.control |= GSC_OVERLAY;
            gs.trig_count = triggers(c, gm + 0x104);
            uint32_t im = rd(c, gm + 0x97);         // X_LevelItemImplementation (vtable 0x7d562c), map size at +0x18
            if (im) { gs.items = rd(c, im + 0x18); uint32_t head = rd(c, im + 0x10);
                gs.item_count = head ? items_walk(c, rd(c, head + 4), rd(c, 0x8b4124), 0, 0) : 0; }
        }
    }
    uint32_t req = __atomic_load_n(&pub.nav_req, __ATOMIC_ACQUIRE), done = pub.nav_done, count = pub.nav_count;
    uint32_t nr = pub.room_count, np = pub.portal_count;
    if (req != done && (gs.flags & GSF_LEVEL)) {
        uint32_t head = rd(c, 0x8a6a0c), nil = rd(c, 0x8a6a30);
        count = head ? nav_walk(c, rd(c, head + 4), nil, 0, 0) : 0;
        rooms_portals(c, gm + 0x104, &nr, &np);
        done = req;
    }
    gs.room_count = nr; gs.room_addr = (uint32_t)(uintptr_t)rooms; gs.portal_count = np; gs.portal_addr = (uint32_t)(uintptr_t)portals;
    gs.nav_req = req; gs.nav_done = done; gs.nav_count = count; gs.nav_addr = (uint32_t)(uintptr_t)nav;
    gs.trig_addr = (uint32_t)(uintptr_t)trig; gs.item_addr = (uint32_t)(uintptr_t)item; gs.en_name_addr = (uint32_t)(uintptr_t)en_name;
    uint32_t seq = pub.seq;
    __atomic_store_n(&pub.seq, seq + 1, __ATOMIC_RELEASE);
    gs.seq = seq + 1;
    memcpy(&pub, &gs, sizeof gs);
    __atomic_store_n(&pub.seq, seq + 2, __ATOMIC_RELEASE);

    static int sys_every = -1;
    if (sys_every < 0) sys_every = getenv("RN_SYS") ? atoi(getenv("RN_SYS")) : 0;
    if (sys_every > 0 && (gs.flags & GSF_PLAYER)) sys_log(c, gm, rd(c, rd(c, gm + 0x1080) + 8), sys_every);

    static int every = -1;
    if (every < 0) every = getenv("RN_GHOST") ? atoi(getenv("RN_GHOST")) : 0;
    if (every > 0 && frame % (unsigned)every == 0) {
        fprintf(stderr, "ghost f%u mode %u fl %x pend %u lvl '%s' next '%s' pos %.2f %.2f %.2f fwd %.2f %.2f %.2f hp %.1f/%d pk %d w %d clip %d res %d bt %.2f/%u en %u/%u items %u\n",
                frame, gs.mode, gs.flags, gs.pending, gs.level, gs.next_level, gs.pos[0], gs.pos[1], gs.pos[2],
                gs.rot[6], gs.rot[7], gs.rot[8], gs.health, gs.health_base, gs.painkillers, gs.weapon, gs.clip,
                gs.reserve, gs.bt_meter, gs.bt_active, gs.enemies_alive, gs.enemies, gs.items);
        uint32_t gm = rd(c, rd(c, 0x8a6338 + 0xc8) + 0x80), lv = gm ? rd(c, gm + 0xf8) : 0;
        char alt[32] = "";
        if (lv) copy_str(c, alt, rd(c, lv + 0x14), rd(c, lv + 0x18));
        uint32_t ch = gm && rd(c, gm + 0x1080) ? rd(c, rd(c, gm + 0x1080) + 8) : 0;
        // Movement state (rea/map/move_oracle.md): X_CharacterInput at C+0x2f9 stores lower state id +0x62 and the
        // animation id after [Revert] +0x6a (0x7429b0); velocity C+0x41e.
        fprintf(stderr, "ghost   alt level '%s' ms %08x cur %08x gm %08x cam %08x pw %08x ch %08x st %d an %d vel %.2f %.2f %.2f pitch %.2f\n", alt,
                rd(c, 0x8a6338 + 0xc8), rd(c, rd(c, 0x8a6338 + 0xc8) + 0x19), gm, gm ? rd(c, gm + 0x1074) : 0,
                gm ? rd(c, gm + 0x1080) : 0, ch, ch ? (int32_t)rd(c, ch + 0x35b) : 0, ch ? (int32_t)rd(c, ch + 0x363) : 0,
                gs.vel[0], gs.vel[1], gs.vel[2], gs.pitch * 57.29578f);   // pitch in degrees, for input parity
        for (uint32_t k = 0, shown = 0; k < gs.enemies && k < GS_ENEMIES && shown < 6; k++) {   // the live ones near the player
            float dx = gs.en[k].pos[0] - gs.pos[0], dy = gs.en[k].pos[1] - gs.pos[1], dz = gs.en[k].pos[2] - gs.pos[2];
            if (gs.en[k].health <= 0 || gs.en[k].room != gs.room || dx * dx + dy * dy + dz * dz > 30.f * 30.f) continue;
            shown++;
            fprintf(stderr, "ghost   en%u %.2f %.2f %.2f hp %.1f fl %x\n", k, gs.en[k].pos[0], gs.en[k].pos[1], gs.en[k].pos[2],
                    gs.en[k].health, gs.en[k].flags);
        }
    }
}
