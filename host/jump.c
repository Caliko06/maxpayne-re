#define _GNU_SOURCE   // strcasestr (emscripten libc)
// RN_JUMP (test only, WORKLIST #4; the RE-NFSU RN_JUMP pattern): run game script messages the way the developer
// console runs a typed line, so a run starts at an exact point without walking there.
//   RN_JUMP="maxpayne_gamemode->gm_init(part1_level1);|player->c_setinvulnerable(true);|player->c_teleport(::p5::w4);"
// Lines are split on '|' (',' splits the web ?env= pairs). The syntax is the level scripts' own (Part1_Level1.ldb holds
// "player->c_teleport(::p5::w4);", "player->c_setinvulnerable(true);", "::hallway1::timer::trigger1.TRIGGER->T_Activate();").
// Gates: a gm_init or s_modeswitch line waits for a steady menu or game (60 frames); every other line waits until the
// level asked for is up with player control (30 frames). From the menu, gm_init only queues the level: the switch to
// the game is "x_modeswitch->s_modeswitch(game);" (the level scripts' own form: "x_modeswitch->s_modeswitch(graphicnovel)"). The console path (rea/map/gameplay.md "text command pipeline"):
//   0x611ac0 X_ConsoleMode slot 14, Enter -> 0x611cb0 -> X_Message::parse 0x51e9e0 (ecx = text, edx = console + 0x8a)
//   -> X_MessageQueue::send 0x51b6a0 (ecx = message, edx = console + 0x9c, push 0, push 0; ret 8) -> free 0x51fdf0 (0x611e1f).
// Console = [ModeSwitch + 0x70] (MaxPayne_ConsoleMode, ctor 0x429590, vtable 0x7cc404 slot 14 = 0x611ac0).
// Here parse goes through its SEH wrapper 0x51e8c0 (same ecx/edx plus one stack argument, the name its catch path
// reports; ret 4 at 0x51e900) instead of the console's own frame.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "host.h"

void gamestate_view(uint32_t *mode, uint32_t *fade, int *level_up, int *control, const char **level, uint32_t *frame);

static uint32_t rd(Ctx *c, uint32_t a) { return ld(c, a, 4); }

// '@' lines (census/systems_table.tsv scenario): frame-gated input, the same on web and native, in the same list:
// Times count in 1/60 s of real time, not in frames: native without drawing runs at 500 fps, Chrome at 60.
//   @w:<ticks>  wait              @k:<KEY>:<ticks>  press, hold, release      @d:<KEY> @u:<KEY>  key down / up
//   @f:<shots>:<ticks>  clicks    @b:<0|1>  left button                       @m:<text>  a mark in the log
//   !<line>  sent or counted under a cutscene too (control off)
//   @aim:<metres above the foe's body point>  keep aiming at the nearest live foe in the room     @aimoff
int input_script(const char *name, int d);
int gamestate_aim(float up, int *dx, int *dy);
#define TICK_US 16667
static int64_t script_wait;
static int script_aim, script_shots, script_gap, script_phase;
static float script_up;
static char script_key[32];
static void script_tick(void) {
    if (script_aim) { int dx, dy; if (gamestate_aim(script_up, &dx, &dy) && (dx || dy)) dinput8_motion(dx, dy, 0); }
    static uint64_t last; uint64_t now = now_us(); int64_t dt = last ? (int64_t)(now - last) : 0; last = now;
    if (script_wait > 0 && (script_wait -= dt) <= 0) { script_wait = 0; if (script_key[0]) { input_script(script_key, 0); script_key[0] = 0; } }
    if (script_shots && !script_wait) {
        if (!script_phase) { input_script(NULL, 1); script_phase = 1; script_wait = 3 * TICK_US; }
        else { input_script(NULL, 0); script_phase = 0; script_shots--; script_wait = (int64_t)script_gap * TICK_US; }
    }
}
static void script_line(const char *l) {
    char a[32] = ""; int n = 0; float f = 0;
    if (sscanf(l, "@w:%d", &n) == 1) script_wait = (int64_t)n * TICK_US;
    else if (sscanf(l, "@k:%31[A-Z0-9_]:%d", a, &n) == 2) { if (input_script(a, 1)) { snprintf(script_key, sizeof script_key, "%s", a); script_wait = (int64_t)n * TICK_US; } }
    else if (sscanf(l, "@d:%31[A-Z0-9_]", a) == 1) input_script(a, 1);
    else if (sscanf(l, "@u:%31[A-Z0-9_]", a) == 1) input_script(a, 0);
    else if (sscanf(l, "@f:%d:%d", &script_shots, &script_gap) == 2) script_phase = 0;
    else if (sscanf(l, "@b:%d", &n) == 1) input_script(NULL, n);
    else if (sscanf(l, "@aim:%f", &f) == 1) { script_aim = 1; script_up = f; }
    else if (!strcmp(l, "@aimoff")) script_aim = 0;
    else if (!strncmp(l, "@m:", 3)) rn_log("MARK %s", l + 3);
    else rn_log("RN_JUMP: bad line '%s'", l);
}

static uint32_t send_line(Ctx *c, const char *line) {
    uint32_t ms = rd(c, 0x8a6338 + 0xc8), con = ms ? rd(c, ms + 0x70) : 0;   // theApp 0x8a6338 + 0xc8: ModeSwitch
    if (!con) { rn_log("RN_JUMP: no console object yet"); return 0; }
    size_t n = strlen(line);
    if (n > 0xe00) n = 0xe00;
    memcpy(G(JUMP_ADDR), line, n); c->mem[JUMP_ADDR + n] = 0;
    uint32_t ecx = c->r[ECX], edx = c->r[EDX];
    c->r[ECX] = JUMP_ADDR; c->r[EDX] = con + 0x8a;
    memcpy(G(JUMP_ADDR + 0xf00), "RN_JUMP", 8);
    const uint32_t who = JUMP_ADDR + 0xf00;                        // [ebp+8]: the name its catch path reports (ret 4)
    uint32_t msg = call_guest(c, 0x51e8c0u, 1, &who);
    if (msg && !c->halt && !c->skipped) {
        const uint32_t a[2] = {0, 0};
        c->r[ECX] = msg; c->r[EDX] = con + 0x9c;
        call_guest(c, 0x51b6a0u, 2, a);
        c->r[ECX] = msg;
        call_guest(c, 0x51fdf0u, 0, NULL);
    }
    c->r[ECX] = ecx; c->r[EDX] = edx;
    return msg;
}

// host/cheats.c: one console line into the running game, as above.
uint32_t jump_send_line(Ctx *c, const char *line) { return send_line(c, line); }

// Runtime mailbox (WORKLIST #3, tools/ghost/ghost.mjs plan route): the driver writes '|'-separated lines to line[],
// then bumps seq; jump_poll runs them with the same gates (1 steady frame instead of 30/60: the driver gates first) and
// sets done = seq. sent/failed count the lines of all batches. A batch the driver gave up on is cancelled: a line meant for
// one level, parsed in the next, names an object that is not there, and the parse wrapper's catch closes the game.
typedef struct { uint32_t seq, done, sent, failed, cancel; char line[4096]; } JumpBox;   // cancel = seq: drop that batch
static JumpBox box;
uint32_t rn_jump_box(void) { return (uint32_t)(uintptr_t)&box; }

// Called at the game's input reads (host/dinput8.c), the phase where the console runs its lines.
void jump_poll(Ctx *c) {
    static char *list, *next, want[64];
    static uint32_t last_frame, since, steady, cur;                 // cur: mailbox batch being run (0 = RN_JUMP env)
    if (!list) { const char *e = getenv("RN_JUMP"); list = strdup(e ? e : ""); next = list; }
    { static uint32_t tick_frame; uint32_t m, fd, fr; int u, ct; const char *lv;
      gamestate_view(&m, &fd, &u, &ct, &lv, &fr);
      if (fr != tick_frame) { tick_frame = fr; script_tick(); } }
    if (!*next) {
        uint32_t q = __atomic_load_n(&box.seq, __ATOMIC_ACQUIRE);
        if (q == box.done) return;
        free(list); box.line[sizeof box.line - 1] = 0; list = strdup(box.line); next = list;
        cur = q; want[0] = 0; steady = 0;
        if (!*next) { __atomic_store_n(&box.done, cur, __ATOMIC_RELEASE); return; }
    }
    if (cur && __atomic_load_n(&box.cancel, __ATOMIC_ACQUIRE) == cur) {
        rn_log("RN_JUMP: batch %u cancelled", cur); next += strlen(next); __atomic_store_n(&box.done, cur, __ATOMIC_RELEASE); return;
    }
    uint32_t mode, fade, frame; int up, ctl; const char *level;
    gamestate_view(&mode, &fade, &up, &ctl, &level, &frame);
    if (frame == last_frame) return;                                // once per presented frame
    last_frame = frame;
    char *end = strchr(next, '|');
    size_t len = end ? (size_t)(end - next) : strlen(next);
    char line[1024];
    snprintf(line, sizeof line, "%.*s", (int)len, next);
    for (char *q = line; *q; q++) if (*q == '~') *q = ',';          // the web ?env= list splits on ','
    int force = line[0] == '!';                                     // '!line': also under a cutscene (no control), to
    if (force) memmove(line, line + 1, strlen(line));               // drive a cinematic chain (#24 part3_level7 cin1)
    // S_LoadGame(slot) / S_LoadLastSavedGame() (ModeSwitch message slot #12, rea/map/files.md:216; the last one reads the
    // registry's "Last Saved Game Filename") are system lines too: from the menu they load a save, the launcher's Continue.
    int init = strcasestr(line, "gm_init(") != NULL;
    int sys = init || strcasestr(line, "s_modeswitch(") || strcasestr(line, "s_loadgame(") || strcasestr(line, "s_loadlastsavedgame(");
    int ready = sys ? fade == 1 && (mode == 1 || (mode == 3 && up))
                     : fade == 1 && mode == 3 && up && (ctl || force) && (!*want || !strcasecmp(level, want));   // not under the comic:
                                                       // the level's intro script places the player after it (web 2026-11-02)
    // A level's opening comic (mode 2) holds every non-system line: Esc stops a page, Esc again exits to the game
    // (rea/map/graphicnovel.md 3, the ghost's own skip). One tap every 4 s of comic while a line waits.
    static uint32_t novel_since;
    if (!sys && mode == 2 && fade == 1 && !script_wait) {
        if (!novel_since) novel_since = frame;
        else if (frame - novel_since > 240 && input_script("ESCAPE", 1)) {
            snprintf(script_key, sizeof script_key, "ESCAPE"); script_wait = 6 * TICK_US; novel_since = frame;
            rn_log("RN_JUMP: Esc to skip the comic at frame %u", frame);
        }
    } else if (mode != 2) novel_since = 0;
    if (!ready) { steady = 0; return; }
    if (script_wait || script_shots) return;                        // the last '@' line is still running
    if (line[0] == '@') {                                           // no steady gate
        script_line(line); next += len + (end != NULL); steady = 0;
        // Done = every line started (a last @w is still counting); unpublished, the mailbox would rerun the batch each frame.
        if (!*next) { rn_log("RN_JUMP: done at frame %u", frame); if (cur) __atomic_store_n(&box.done, cur, __ATOMIC_RELEASE); }
        return;
    }
    if (!steady++) since = frame;
    if (frame - since < (cur ? 1u : sys ? 60u : 30u)) return;
    uint32_t msg = send_line(c, line);
    if (cur) { if (msg) box.sent++; else box.failed++; }
    rn_log("RN_JUMP: '%s' at frame %u (mode %u level '%s'): %s", line, frame, mode, level, msg ? "sent" : "parse failed");
    if (init) { const char *a = strchr(line, '(') + 1; a += *a == '"'; snprintf(want, sizeof want, "%.*s", (int)strcspn(a, ")\" "), a); }
    next += len + (end != NULL);
    steady = 0;
    if (!*next) { rn_log("RN_JUMP: done at frame %u", frame); if (cur) __atomic_store_n(&box.done, cur, __ATOMIC_RELEASE); }
}
