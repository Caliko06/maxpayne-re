// Loads the guest images (tools/recomp/images.py) the way the Windows loader does, runs each DLL's DllMain,
// then the entry point of MaxPayne.exe. Usage: build/maxpayne <game dir> [--shot frame.png]
// Native runs end after RN_SECS seconds (default 60, 0 = no limit), so no run is ever unbounded.
#ifdef __APPLE__
#define _XOPEN_SOURCE 700                       // Darwin exposes ucontext (RN_PROF) only with it
#else
#define _GNU_SOURCE                             // REG_RIP for RN_PROF
#endif
#include "host.h"
#include "gpu.h"
#include "images.h"
#include <pthread.h>
#include <unistd.h>

Ctx *g_ctx;
void dispatch_report(void);
void rn_heap_report(Ctx *c);
void win32_set_root(const char *game_dir);
extern const uint32_t rn_patch_count, rn_patch_addr[];
extern const char *const rn_patch_hex[];

static uint8_t *read_file(const char *path, long *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *p = malloc(*size);
    if (fread(p, 1, *size, f) != (size_t)*size) { fclose(f); free(p); return NULL; }
    fclose(f);
    return p;
}

// Return addresses on the guest stack: values in .text that follow a call instruction.
void rn_backtrace(Ctx *c) {
    char line[512] = "";
    int n = 0;
    uint32_t hi = ld(c, c->fs + 4, 4);
    for (uint32_t a = c->r[ESP]; a + 4 <= hi && n < 40; a += 4) {
        uint32_t v = ld(c, a, 4);
        if (image_of(v) < 0 || v < 0x401006) continue;
        const uint8_t *p = c->mem + v;
        if (p[-5] == 0xe8 || (p[-2] == 0xff && (p[-1] & 0x38) == 0x10) || (p[-6] == 0xff && (p[-5] & 0x38) == 0x10) ||
            (p[-3] == 0xff && (p[-2] & 0x38) == 0x10) || (p[-7] == 0xff && (p[-6] & 0x38) == 0x10)) {
            snprintf(line + strlen(line), sizeof line - strlen(line), " %08x", v);
            n++;
        }
    }
    rn_log("stack:%s", line);
}

#ifndef __EMSCRIPTEN__
// RN_PROF=<file>: 1 kHz SIGPROF samples of the host PC (second, rip), written at exit; tools/prof.py names the lifted
// functions (there is no perf in the Pod container).
#include <signal.h>
#include <sys/time.h>
#include <ucontext.h>
#define PROF_MAX (1u << 20)
// The host PC of a sample: Darwin keeps it in mcontext, Linux in gregs. prof.py only needs the address.
#if defined(__APPLE__) && defined(__aarch64__)
#define PROF_PC(uc) ((uint64_t)(uc)->uc_mcontext->__ss.__pc)
#elif defined(__APPLE__)
#define PROF_PC(uc) ((uint64_t)(uc)->uc_mcontext->__ss.__rip)
#elif defined(__x86_64__)
#define PROF_PC(uc) ((uint64_t)(uc)->uc_mcontext.gregs[REG_RIP])
#else
#define PROF_PC(uc) 0
#endif
static struct { uint32_t sec; uint64_t rip; } prof_buf[PROF_MAX];
static volatile uint32_t prof_n;
static struct timeval prof_t0;
static void prof_tick(int sig, siginfo_t *si, void *uc) {
    (void)sig; (void)si;
    struct timeval t;
    gettimeofday(&t, NULL);
    uint32_t k = __atomic_fetch_add(&prof_n, 1, __ATOMIC_RELAXED);
    if (k < PROF_MAX) { prof_buf[k].sec = (uint32_t)(t.tv_sec - prof_t0.tv_sec); prof_buf[k].rip = PROF_PC((ucontext_t *)uc); }
}
static void prof_start(void) {
    if (!getenv("RN_PROF")) return;
    gettimeofday(&prof_t0, NULL);
    struct sigaction sa = {0};
    sa.sa_sigaction = prof_tick;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGPROF, &sa, NULL);
    struct itimerval it = {{0, 1000}, {0, 1000}};
    setitimer(ITIMER_PROF, &it, NULL);
}
void prof_write(void) {
    const char *path = getenv("RN_PROF");
    if (!path) return;
    uint32_t n = prof_n < PROF_MAX ? prof_n : PROF_MAX;
    FILE *f = fopen(path, "wb");
    // First record: sec ~0 and this function's runtime address, so tools/prof.py finds the PIE load base.
    struct { uint32_t sec; uint64_t rip; } mark = {~0u, (uint64_t)(uintptr_t)prof_write};
    if (f) { fwrite(&mark, sizeof mark, 1, f); fwrite(prof_buf, sizeof prof_buf[0], n, f); fclose(f); }
    rn_log("RN_PROF: %u samples -> %s", n, path);
}

static void *watchdog(void *arg) {
    unsigned secs = (unsigned)(uintptr_t)arg;
    sigset_t prof;                                      // RN_PROF ticks must not cut its sleep(1) short
    sigemptyset(&prof);
    sigaddset(&prof, SIGPROF);
    pthread_sigmask(SIG_BLOCK, &prof, NULL);
    // RN_SAMPLE=<s>: every s seconds, the main thread's guest return addresses (debug; where a long stall spends time).
    unsigned every = getenv("RN_SAMPLE") ? (unsigned)atoi(getenv("RN_SAMPLE")) : 0;
    for (unsigned t = 1; t <= secs; t++) {
        sleep(1);
        if (every && t % every == 0) { rn_log("sample at %u s, esp %08x:", t, g_ctx->r[ESP]); rn_backtrace(g_ctx); rn_heap_report(g_ctx); }
    }
    dispatch_report();
    prof_write();
    rn_log("RN_SECS: stopped after %u s (no stop reached)", secs);
    _exit(0);
}
#endif

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }

// Maps one guest image (a rebased PE in build/img) at its base: headers, then each section's raw data.
static int map_image(Ctx *c, const RnImage *m) {
    long fsize;
    uint8_t *pe = read_file(m->file, &fsize);
    if (!pe) { fprintf(stderr, "cannot read %s (run tools/recomp/images.py)\n", m->file); return 0; }
    const uint8_t *nt = pe + rd32(pe + 0x3c);
    const uint8_t *opt = nt + 24;
    uint32_t nsec = rd16(nt + 6), optsz = rd16(nt + 20);
    if (rd32(opt + 28) != m->base) { fprintf(stderr, "%s: image base %08x, table says %08x\n", m->name, rd32(opt + 28), m->base); return 0; }
    memcpy(c->mem + m->base, pe, rd32(opt + 60));
    const uint8_t *sec = opt + optsz;
    uint32_t al = rd32(opt + 32);                       // SectionAlignment
    for (uint32_t k = 0; k < nsec; k++, sec += 40) {
        uint32_t va = rd32(sec + 12), raw = rd32(sec + 16), off = rd32(sec + 20), vs = rd32(sec + 8);
        // Like the Windows loader: min(SizeOfRawData, VirtualSize rounded up to SectionAlignment) bytes from the file
        // (e2mfc.dll has code just past its .text VirtualSize).
        uint32_t span = vs ? (vs + al - 1) / al * al : raw;
        memcpy(c->mem + m->base + va, pe + off, raw < span ? raw : span);
    }
    free(pe);
    return 1;
}

#ifdef __EMSCRIPTEN__
uint32_t rn_guest_mem = 0x50000000u;
#endif
int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: maxpayne <game dir>   (run from the repository root)\n"); return 2; }
    win32_set_root(argv[1]);
    if (argc > 3 && !strcmp(argv[2], "--shot")) gpu_set_shot(argv[3]);
#ifndef __EMSCRIPTEN__
    const char *lim = getenv("RN_SECS");
    unsigned secs = lim ? (unsigned)atoi(lim) : 60;
    pthread_t wd;
    if (secs) { pthread_create(&wd, NULL, watchdog, (void *)(uintptr_t)secs); pthread_detach(wd); }
    prof_start();
#endif

    static Ctx ctx;
    Ctx *c = g_ctx = &ctx;
#ifdef __EMSCRIPTEN__
    { const char *s = getenv("RN_GUEST_MB"); if (s && atoi(s) > 0) rn_guest_mem = (uint32_t)atoi(s) << 20; }
#endif
    c->memsz = GUEST_MEM;
#ifdef __EMSCRIPTEN__
    // Fresh wasm pages are zero; calloc would clear all 3 GB and make them resident (tab 4.7 GB at the menu, 2.1 GB
    // without the clear: out/web_light_report.md). Only the pages the game touches become resident.
    c->mem = malloc(GUEST_MEM);
#else
    c->mem = calloc(1, GUEST_MEM);
#endif
    if (!c->mem) { fprintf(stderr, "cannot allocate guest memory\n"); return 2; }
    for (uint32_t k = 0; k < rn_image_count; k++)
        if (!map_image(c, &rn_images[k])) return 2;
    // PATCH table (tools/recomp/patches.py): the lifted code already has these bytes; the images must match.
    for (uint32_t k = 0; k < rn_patch_count; k++) {
        const char *h = rn_patch_hex[k];
        for (uint32_t n = 0; h[2 * n]; n++) {
            unsigned v;
            sscanf(h + 2 * n, "%2x", &v);
            c->mem[rn_patch_addr[k] + n] = (uint8_t)v;
        }
        rn_log("PATCH %08x active (tools/recomp/patches.py)", rn_patch_addr[k]);
    }
    // Imports: a slot gets the export of another guest image, or the fake address of its host function.
    for (uint32_t k = 0; k < rn_slot_count; k++) {
        const RnSlot *s = &rn_slots[k];
        st(c, s->slot, s->guest ? s->guest : host_addr_of(s->dll, s->name), 4);
    }

    win32_init(c, rn_images[0].size);
    threads_init(c);
    finit(c);
    c->fcw = 0x27f;              // Windows starts threads with 53-bit precision, all exceptions masked
    c->r[ESP] = STACK_HI - 16;
    rn_log("%u images mapped, guest memory %u MB", rn_image_count, GUEST_MEM >> 20);
    // The loader initialises DLLs in dependency order (images.py); plugins wait for their LoadLibraryA.
    for (uint32_t k = 0; k < rn_init_count; k++) {
        int i = (int)rn_init_order[k];
        if (rn_images[i].dll && !image_load(c, i)) { rn_halt(c, "image_load failed for %s", rn_images[i].name); rn_stop(c); return 0; }
    }
    uint32_t entry = rn_images[0].entry;
    c->r[ESP] -= 4;
    st(c, c->r[ESP], host_addr_of("rn", "entry_returned"), 4);
    rn_log("%s entry %08x", rn_images[0].name, entry);
    rn_call(c, entry);
    if (!c->halt && c->ret != host_addr_of("rn", "entry_returned")) rn_halt(c, "a return to %08x reached the program base", c->ret);
    rn_stop(c);
    return 0;
}

// Called with the GIL held by the thread that stopped: report and end every thread.
void rn_stop(Ctx *c) {
    reg_flush();
    dispatch_report();
#ifndef __EMSCRIPTEN__
    prof_write();
#endif
    rn_log("STOP in thread %x at %08x: %s", ld(c, c->fs + 0x24, 4), c->halt_at, c->why ? c->why : "(no reason)");
    rn_log("eax=%08x ecx=%08x edx=%08x ebx=%08x esp=%08x ebp=%08x esi=%08x edi=%08x",
           c->r[0], c->r[1], c->r[2], c->r[3], c->r[4], c->r[5], c->r[6], c->r[7]);
    rn_backtrace(c);
    // A native run that stops on anything but a clean ExitProcess(0) or a plain return must not look like success (#18).
    exit(c->why && strcmp(c->why, "ExitProcess(0)") ? 2 : 0);
}
