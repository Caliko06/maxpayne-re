// F1 in the menu opens the Cheats page (tools/ras.py cheats_menu: a nested [Menu] with id = 6), so the page is one key away from
// any menu page, front end and pause menu alike. Replaces X_MenuMode::onKey 0x42c020 for F1 only (tools/recomp/patches.py HOST_FUNCS);
// every other key runs the lifted body, which forwards it to 0x421820. rea/map/cheats_menu.md sections 5 and 7.
#include <stdlib.h>
#include "rt.h"
#include "host.h"

enum { VK_F1 = 0x70 /* free in the menu: 0x635680 maps only vk 8..0x6f, higher keys are ignored */,
       MODE_ROOT = 0x92 /* 0x631390 passes [this+0x92] to 0x6317d0 */, MODE_PAGE = 0x8e /* 0x6317d0 compares it with its argument */,
       PAGE_ID = 0xc /* 0x634080:400 */, PAGE_COUNT = 0x22, PAGE_ITEMS = 0x26 /* 0x635d90 walks count items of this array */,
       CHEATS_ID = 6 };

// A guest range [p, p+n) inside the flat memory; subtracts, so p near 4 GB cannot wrap the way p + n does (ld() does not check).
static int in_mem(Ctx *c, uint32_t p, uint32_t n) { return p >= LOW && n <= c->memsz && p <= c->memsz - n; }

// thiscall(ecx = menu mode, vk, char), ret 8.
int rn_cheats_key(Ctx *c) {
    uint32_t sp = c->r[ESP], mb = c->r[ECX];
    if (ld(c, sp + 4, 4) != VK_F1 || !in_mem(c, mb, 0x100)) return 0;
    uint32_t root = ld(c, mb + MODE_ROOT, 4);
    if (!in_mem(c, root, 0x40)) return 0;
    uint32_t n = ld(c, root + PAGE_COUNT, 4), arr = ld(c, root + PAGE_ITEMS, 4);
    if (n > 64 || !in_mem(c, arr, 4 * n)) return 0;
    static int dbg = -1;
    if (dbg < 0) dbg = getenv("RN_CHEATS") != NULL;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t page = ld(c, arr + 4 * i, 4);
        if (!in_mem(c, page, 0x40)) continue;
        if (dbg) rn_log("cheats: root item %u %08x id %u", i, page, ld(c, page + PAGE_ID, 4));
        if (ld(c, page + PAGE_ID, 4) != CHEATS_ID) continue;
        if (dbg) rn_log("cheats: F1 opens page %08x (was %08x)", page, ld(c, mb + MODE_PAGE, 4));
        uint32_t arg = page;
        call_guest(c, 0x6317d0, 1, &arg);       // 0x6317d0: thiscall(page), ret 4; ecx stays the menu mode
        if (c->halt || c->skipped) return 1;
        c->ret = ld(c, c->r[ESP], 4);           // the original's ret 8
        c->r[ESP] += 4 + 8;
        return 1;
    }
    return 0;
}

// ---- in the running level: F3 god, F4 all weapons + ammo, F6 noclip --------------------------------------------------------------
// The menu page cannot carry `player->...` (the parser resolves the receiver when it builds the page), so the keys send the console's
// own aliases (x_data 2_console.txt: God, GetAllWeapons, NoClip) as lines into the level, the way RN_JUMP does (host/jump.c). F1 and F2
// are the game's own keys (F1 switches to the graphic novel, 0x44c610); F3/F4/F6 map to 'unused' in its table 0x44e984. MID hook at
// 0x44c2a1 (MaxPayne_GameMode tick, after OnKey): the vk a typed key left at [ebp-0x28] (0x44c298..0x44c2a4).
uint32_t jump_send_line(Ctx *c, const char *line);

static const char *const GOD_ON[] = {"player->C_SetImmortal ( false );", "player->C_SetHealth(1);", "player->C_SetImmortal ( true );"};
static const char *const GOD_OFF[] = {"player->C_SetImmortal ( false );"};
static const char *const NOCLIP_ON[] = {"player->C_EnableDOCollisions( false );"};
static const char *const NOCLIP_OFF[] = {"player->C_EnableDOCollisions( true );"};
// GetAllWeapons, statement by statement (a menu/console message is one statement)
static const char *const WEAPONS[] = {
    "player->c_pickupweapon ( \"leadpipe\" );", "player->c_setinfiniteammo( \"leadpipe\", true);", "player->c_pickupweapon ( \"baseballbat\" );",
    "player->c_setinfiniteammo( \"baseballbat\", true);", "player->c_pickupweapon ( \"beretta\" );", "player->c_pickupweapon ( \"berettadual\" );",
    "player->c_pickupammo( \"berettadual\", 1000 );", "player->c_pickupweapon ( \"deserteagle\" );", "player->c_pickupammo( \"deserteagle\", 1000 );",
    "player->c_pickupweapon ( \"sawedshotgun\" );", "player->c_pickupammo( \"sawedshotgun\", 1000 );", "player->c_pickupweapon ( \"pumpshotgun\" );",
    "player->c_pickupammo( \"pumpshotgun\", 1000 );", "player->c_pickupweapon ( \"ingram\" );", "player->c_pickupweapon ( \"ingramdual\" );",
    "player->c_pickupammo( \"ingramdual\", 1000 );", "player->c_pickupweapon ( \"sniper\" );", "player->c_pickupammo(\"sniper\", 1000 );",
    "player->c_pickupweapon ( \"mp5\" );", "player->c_pickupammo( \"mp5\", 1000 );", "player->c_pickupweapon ( \"jackhammer\" );",
    "player->c_pickupammo( \"jackhammer\", 1000 );", "player->c_pickupweapon ( \"m79\" );", "player->c_pickupammo( \"m79\", 20 );",
    "player->c_pickupweapon ( \"grenade\" );", "player->c_pickupammo( \"grenade\", 20 );", "player->c_pickupweapon ( \"molotov\" );",
    "player->c_pickupammo( \"molotov\", 20 );", "player->c_pickupweapon ( \"painkiller\" );", "player->c_pickupammo( \"painkiller\", 20 );",
    "player->c_addhealth( 1000 );"};
#define N(a) (int)(sizeof a / sizeof *a)

// The player (X_Character) of the level running right now, or 0: the chain host/gamestate.c walks, but read live. The tick that
// calls this hook is the one that creates and drops these objects, so a snapshot (gamestate_view, published from Present) lags a
// frame behind, and a `player->` line parsed in the frame a level starts or ends names an object that is not there and closes the game.
static uint32_t live_player(Ctx *c) {
    uint32_t ms = ld(c, 0x8a6338 + 0xc8, 4);                       // theApp + 0xc8: X_ModeSwitch, +0x80 the game mode
    if (!in_mem(c, ms, 0x100)) return 0;
    uint32_t gm = ld(c, ms + 0x80, 4);
    if (!in_mem(c, gm, 0x1100) || !ld(c, gm + 0x1074, 4)) return 0;   // 0x1074: level up (GSF_LEVEL)
    uint32_t pw = ld(c, gm + 0x1080, 4);
    if (!in_mem(c, pw, 12)) return 0;
    uint32_t ch = ld(c, pw + 8, 4);
    return in_mem(c, ch, 0x500) ? ch : 0;
}

// The state the lines left on the player (+0x454 immortal (C_SetImmortal), +0x22c the dynamic-object collisions flag (handler 0x69b9d0 of C_EnableDOCollisions)).
static void cheat_send(Ctx *c, const char *what, const char *const *l, int n) {
    for (int i = 0; i < n && !c->halt; i++) jump_send_line(c, l[i]);
    uint32_t ch = live_player(c);
    if (ch) rn_log("cheats: %s (%d lines): immortal %u, DO collisions %u", what, n, ld(c, ch + 0x454, 1), ld(c, ch + 0x22c, 1));
    else rn_log("cheats: %s (%d lines)", what, n);
}

void rn_cheats_game_key(Ctx *c) {
    uint32_t vk = ld(c, c->r[EBP] - 0x28, 4);
    if (vk != 0x72 && vk != 0x73 && vk != 0x75) return;
    uint32_t ch = live_player(c);
    if (!ch) { rn_log("cheats: key %02x ignored, no player in a running level", vk); return; }   // player-> would stop the game
    if (vk == 0x72) {
        if (!ld(c, ch + 0x454, 1)) cheat_send(c, "god on", GOD_ON, N(GOD_ON));
        else cheat_send(c, "god off", GOD_OFF, N(GOD_OFF));
    } else if (vk == 0x73) {
        cheat_send(c, "all weapons", WEAPONS, N(WEAPONS));
    } else {
        if (ld(c, ch + 0x22c, 1)) cheat_send(c, "noclip on", NOCLIP_ON, N(NOCLIP_ON));
        else cheat_send(c, "noclip off", NOCLIP_OFF, N(NOCLIP_OFF));
    }
}
