// Address -> code: lifted guest functions and host API functions.
#include <stdarg.h>
#include <strings.h>
#include <time.h>
#include "host.h"

extern const uint32_t rn_fn_count;
extern const uint32_t rn_fn_addr[];
extern void (*const rn_fn_ptr[])(Ctx *);

typedef struct { char dll[32], name[64]; int nargs; HostFn fn; uint32_t calls; } HostEntry;
static HostEntry hosts[1024];
const Api *find_api(const char *dll, const char *name);
static int nhosts;

uint32_t host_addr_of(const char *dll, const char *name) {
    for (int k = 0; k < nhosts; k++)
        if (!strcasecmp(hosts[k].dll, dll) && !strcmp(hosts[k].name, name)) return HOST_BASE + 16u * k;
    if (nhosts == 1024) { fprintf(stderr, "host table full\n"); exit(1); }
    HostEntry *h = &hosts[nhosts];
    snprintf(h->dll, sizeof h->dll, "%s", dll);
    snprintf(h->name, sizeof h->name, "%s", name);
    const Api *a = find_api(dll, name);
    if (a) { h->fn = a->fn; h->nargs = a->nargs; }
    return HOST_BASE + 16u * nhosts++;
}

const Api *find_api(const char *dll, const char *name) {
    static const Api *tables[] = {win32_apis, user32_apis, thread_apis, gdi32_apis, d3d8_apis, dinput8_apis, dsound_apis};
    const int *counts[] = {&win32_api_count, &user32_api_count, &thread_api_count, &gdi32_api_count, &d3d8_api_count, &dinput8_api_count, &dsound_api_count};
    for (int t = 0; t < (int)(sizeof tables / sizeof tables[0]); t++)
        for (int k = 0; k < *counts[t]; k++)
            if (!strcasecmp(tables[t][k].dll, dll) && !strcmp(tables[t][k].name, name)) return &tables[t][k];
    return NULL;
}

// Host -> guest call. A C++ catch continuation runs on top of the host stack, inside the handler that caught
// (msvcrt _JumpToContinuation 0x1080ac4f jumps to it). Its function's `ret` then falls through every host and lifted
// frame down to the call site it returns to: lifted call sites return on a foreign address once c->uw is set, and a
// host frame sets c->skipped and leaves registers and c->ret as they are. It answers for this call only: a handler can
// return normally after a skip deeper down (rethrow: msvcrt CallCatchBlock's __except at ret 0x1080a784 goes on searching).
static uint32_t guest_call(Ctx *c, uint32_t fn, int n, const uint32_t *args, int callee_pops) {
    if (c->skipped) return 0;       // this API call was abandoned by a catch: its loop must not run the next callback
    uint32_t saved[8];
    memcpy(saved, c->r, sizeof saved);
    for (int k = n - 1; k >= 0; k--) { c->r[ESP] -= 4; st(c, c->r[ESP], args[k], 4); }
    uint32_t back = host_addr_of("rn", "callback_return");
    c->r[ESP] -= 4;
    st(c, c->r[ESP], back, 4);
    rn_call(c, fn);
    uint32_t v = c->r[EAX];
    if (c->halt) return v;
    c->skipped = c->uw && (c->ret != back || c->r[ESP] != saved[ESP] - (callee_pops ? 0 : 4u * n));
    if (c->skipped) return v;
    if (c->ret != back) { c->halt_at = fn; rn_halt(c, "callback %08x returned to %08x", fn, c->ret); return v; }
    if (callee_pops && c->r[ESP] != saved[ESP]) { c->halt_at = fn; rn_halt(c, "callback %08x is not stdcall/%d", fn, n); return v; }
    memcpy(c->r, saved, sizeof saved);
    return v;
}
// Window procedures, callbacks: stdcall, the callee pops the arguments.
uint32_t call_guest(Ctx *c, uint32_t fn, int n, const uint32_t *args) { return guest_call(c, fn, n, args, 1); }
// SEH handlers: cdecl.
uint32_t call_guest_cdecl(Ctx *c, uint32_t fn, int n, const uint32_t *args) { return guest_call(c, fn, n, args, 0); }

// Last host calls, for the stop report.
#define RING 48
static int ring[RING];
static uint32_t ring_ret[RING];
static unsigned ring_n;

// RN_API=Draw,Present: the trace shows only API names that contain one of these words.
static int traced(const char *name) {
    static const char *f;
    if (!f) f = getenv("RN_API") ? getenv("RN_API") : "";
    if (!*f) return 1;
    for (const char *p = f; *p;) {
        size_t n = strcspn(p, ",");
        for (const char *q = name; *q; q++) if (!strncmp(q, p, n)) return 1;
        p += n + (p[n] == ',');
    }
    return 0;
}
// What each guest thread is doing (telemetry): inside an API, or running guest code after it. No clock here: this runs
// at every API call (thousands per frame in a race).
static struct { uint32_t tid, api, ra, in; } busy[64];
static int nbusy;
static __thread int slot = -1;
void dispatch_threads_report(void) {
    for (int k = 0; k < nbusy && k < 64; k++) {
        int newer = 0;      // a thread id that came back (short-lived threads): only its newest slot
        for (int j = k + 1; j < nbusy && j < 64; j++) newer |= busy[j].tid == busy[k].tid;
        if (!newer) rn_log("  thread %x: %s %s!%s (ret %08x)", busy[k].tid, busy[k].in ? "in" : "guest code after",
               hosts[busy[k].api].dll, hosts[busy[k].api].name, busy[k].ra);
    }
}
static void call_host(Ctx *c, uint32_t target) {
    uint32_t k = (target - HOST_BASE) / 16;
    if (k >= (uint32_t)nhosts || (target & 15)) { rn_halt(c, "jump into host space %08x", target); return; }
    HostEntry *h = &hosts[k];
    uint32_t ra = ld(c, c->r[ESP], 4);
    ring[ring_n % RING] = (int)k; ring_ret[ring_n % RING] = ra; ring_n++;
    h->calls++;
    if (!h->fn) { c->halt_at = ra; rn_halt(c, "missing API %s!%s", h->dll, h->name); return; }
    if (rn_trace() && traced(h->name)) rn_log("api %s (ret %08x)", h->name, ra);
    gil_slice();
    if (slot < 0) slot = __atomic_fetch_add(&nbusy, 1, __ATOMIC_RELAXED);
    if (slot < 64) { busy[slot].tid = ld(c, c->fs + 0x24, 4); busy[slot].api = k; busy[slot].ra = ra; busy[slot].in = 1; }
    c->skipped = 0;
    uint32_t v = h->fn(c);
    if (slot < 64) busy[slot].in = 0;
    if (c->halt) { if (!c->halt_at) c->halt_at = ra; return; }
    if (c->skipped) return;                 // a catch continuation returned through this call
    c->ret = ra;
    c->r[ESP] += 4 + (h->nargs > 0 ? 4u * h->nargs : 0);
    c->r[EAX] = v;
}

// RN_DUMPCALL=<hex addr>:<ptr arg>:<len arg>: on each call through rn_call (calls between images, indirect calls)
// to that address, write the guest bytes [arg ptr, arg ptr + arg len) to /tmp/rn_dump_<n>.bin (stack args from 0).
static int dump_on = -1;    // tested in rn_call before the call: this path runs on every indirect call
static void dump_call(Ctx *c, uint32_t target) {
    static int n;
    static uint32_t at, pa, la;
    if (dump_on < 0) { const char *e = getenv("RN_DUMPCALL"); dump_on = e && sscanf(e, "%x:%u:%u", &at, &pa, &la) == 3; }
    if (!dump_on || target != at) return;
    uint32_t p = ld(c, c->r[ESP] + 4 + 4 * pa, 4), len = ld(c, c->r[ESP] + 4 + 4 * la, 4);
    char path[64];
    snprintf(path, sizeof path, "/tmp/rn_dump_%d.bin", n++);
    FILE *f = fopen(path, "wb");
    if (f && p && len < (64u << 20)) fwrite(G(p), 1, len, f);
    if (f) fclose(f);
    rn_log("dumpcall %08x #%d: %u bytes at %08x -> %s", target, n - 1, len, p, path);
}
// A host API call outside the lifted code (host/difftrace.c: the reference CPU reached a host address).
void rn_host_call(Ctx *c, uint32_t target) { call_host(c, target); }
// RN_PROBE_LOG=1: each probed entry logs its first 10 stack args and 14 words at the first two (debug; first 20000).
static void probe_log(Ctx *c, uint32_t entry) {
    static int on = -1, n;
    if (on < 0) on = getenv("RN_PROBE_LOG") != NULL;
    if (!on || n++ >= 20000) return;
    char line[640];
    int k = snprintf(line, sizeof line, "probe %08x t %llu ecx %08x args", entry, (unsigned long long)now_us(), c->r[ECX]);
    for (int a = 1; a <= 10; a++) k += snprintf(line + k, sizeof line - k, " %x", ld(c, c->r[ESP] + 4 * a, 4));
    for (int a = 1; a <= 2; a++) {
        uint32_t p = ld(c, c->r[ESP] + 4 * a, 4);
        k += snprintf(line + k, sizeof line - k, " |");
        for (int i = 0; i < 14 && p > 0x10000 && p + 64 < c->memsz; i++) k += snprintf(line + k, sizeof line - k, " %x", ld(c, p + 4 * i, 4));
    }
    rn_log("%s", line);
}
#ifdef RN_DIFFTRACE
void rn_difftrace(Ctx *c, uint32_t entry);
void rn_probe(Ctx *c, uint32_t entry) { dump_call(c, entry); probe_log(c, entry); rn_difftrace(c, entry); }
#else
void rn_probe(Ctx *c, uint32_t entry) { dump_call(c, entry); probe_log(c, entry); }
void rn_trace_step(Ctx *c, uint32_t addr) { (void)c; (void)addr; }     // images lifted with RN_LIFT_TRACE
#endif
void rn_call(Ctx *c, uint32_t target) {
    if (target >= HOST_BASE) { call_host(c, target); return; }
    if (dump_on) dump_call(c, target);
    // Calls between images and indirect calls (vtables, libjpeg's method pointers) come here per call: a direct-mapped
    // cache in front of the binary search (RN_PROF: rn_call ~9% of a level load). Guest threads run one at a time (GIL).
    static struct { uint32_t addr; void (*fn)(Ctx *); } cache[1 << 14];
    uint32_t slot = (target ^ (target >> 14)) & ((1u << 14) - 1);
    if (cache[slot].addr == target && cache[slot].fn) { cache[slot].fn(c); return; }
    uint32_t lo = 0, hi = rn_fn_count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (rn_fn_addr[mid] < target) lo = mid + 1; else hi = mid;
    }
    if (lo < rn_fn_count && rn_fn_addr[lo] == target) {
        cache[slot].addr = target; cache[slot].fn = rn_fn_ptr[lo];
        rn_fn_ptr[lo](c);
        return;
    }
    c->halt_at = target;
    // Evidence for the stop: what is at the target and who called it (the return address on the stack).
    char hex[2 * 32 + 1] = "";
    for (uint32_t k = 0; k < 32 && target + k < c->memsz; k++) snprintf(hex + 2 * k, 3, "%02x", c->mem[target + k]);
    rn_halt(c, "no lifted function at %08x (bytes %s, return address %08x)", target, hex, ld(c, c->r[ESP], 4));
}

void dispatch_report(void) {
    unsigned n = ring_n < RING ? ring_n : RING;
    rn_log("last %u API calls (oldest first):", n);
    for (unsigned k = ring_n - n; k < ring_n; k++)
        rn_log("  %s!%s  <- %08x", hosts[ring[k % RING]].dll, hosts[ring[k % RING]].name, ring_ret[k % RING]);
    int used = 0;
    for (int k = 0; k < nhosts; k++) used += hosts[k].calls != 0;
    rn_log("distinct APIs called: %d of %d imported", used, nhosts);
}

uint64_t rn_rdtsc(Ctx *c) {
    (void)c;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec) * 16 / 5;   // 3.2 GHz (host/cpu.c)
}

static char halt_msg[256];
void rn_halt(Ctx *c, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(halt_msg, sizeof halt_msg, fmt, ap);
    va_end(ap);
    c->halt = 1;
    c->why = halt_msg;
}

void rn_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
    fflush(stdout);
}

int rn_trace(void) {
    static int t = -1;
    if (t < 0) t = getenv("RN_TRACE") != NULL;
    return t;
}
