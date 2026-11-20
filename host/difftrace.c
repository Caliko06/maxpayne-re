// difftrace: the lifted code against a reference x86 CPU (Unicorn) on one real call, to find the first instruction
// whose translation is wrong. Native debug builds only (-DRN_DIFFTRACE, -lunicorn); Unicorn never ships.
// RN_DT=<hex entry>:<n>[:<lo>-<hi>] — on the n-th entry (rn_probe; lift that entry with RN_LIFT_PROBES) the process
// forks twice over the same guest memory (copy on write):
//   A runs on in the lifted code; rn_trace_step (images lifted with RN_LIFT_TRACE) records eip + registers + flags
//     before each instruction in [lo, hi) to /tmp/dt_a.bin; it sees the return only in a traced image, so trace the
//     caller's image too;
//   B runs the same call in Unicorn and records the same at its instructions in [lo, hi) to /tmp/dt_b.bin; a call to a
//     host API (HOST_BASE) runs the host function itself (host/dispatch.c rn_host_call) and resumes at its return.
// Records also hold MM0-7 (B: the mantissas of physical x87 registers 0-7, seeded with A's MMX registers).
// Both stop when the call returns (eip = the return address with esp above the entry esp) or after 50M records, and
// write their guest memory to /dev/shm/dt_<side>.mem. tools/dtcmp.py prints the first difference.
#ifdef RN_DIFFTRACE
#include <sys/wait.h>
#include <unistd.h>
#include <unicorn/unicorn.h>
#include "host.h"

typedef struct { uint32_t eip, r[8], fl; uint64_t mm[8]; } Rec;    // fl: CF ZF SF OF in bits 0..3
static FILE *out;
static uint32_t lo, hi, ret_at, esp0;
static uint64_t nrec;
static int active;
static uint32_t watch, wval;                       // RN_DT_WATCH=<hex addr>: log each change of that dword, both sides
static char side;
#define MAXREC 50000000ull
static void watch_check(Ctx *c, uint32_t eip) {
    if (!watch) return;
    uint32_t v = ld(c, watch, 4);
    if (v != wval) { fprintf(stderr, "difftrace %c: [%08x] %08x -> %08x before %08x\n", side, watch, wval, v, eip); wval = v; }
}

static Ctx *mc;                                    // the guest memory to dump at the end
static void finish(void) {
    fclose(out);
    char path[64];
    snprintf(path, sizeof path, "/dev/shm/dt_%c.mem", side == 'A' ? 'a' : 'b');
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(mc->mem, 1, mc->memsz, f); fclose(f); }
    _exit(0);
}
static void put(uint32_t eip, const uint32_t *r, uint32_t fl, const uint64_t *mm) {
    Rec x = {eip, {r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]}, fl, {0}};
    memcpy(x.mm, mm, sizeof x.mm);
    fwrite(&x, sizeof x, 1, out);
    if (++nrec >= MAXREC) finish();
}
static int done(uint32_t eip, uint32_t esp) { return eip == ret_at && esp > esp0; }

// A: the lifted side. The lifted code saved its registers into c before calling this.
void rn_trace_step(Ctx *c, uint32_t addr) {
    if (!active) return;
    watch_check(c, addr);
    if (done(addr, c->r[ESP])) finish();
    if (addr < lo || addr >= hi) return;
    put(addr, c->r, (uint32_t)(c->cf | c->zf << 1 | c->sf << 2 | c->of << 3), c->mm);
}

// B: the reference side.
static uc_engine *uc;
static Ctx *bc;
static const int ureg[8] = {UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EBX,
                            UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI};
static void get_regs(uint32_t *r) { for (int k = 0; k < 8; k++) uc_reg_read(uc, ureg[k], &r[k]); }
static void hook_code(uc_engine *u, uint64_t addr, uint32_t size, void *ud) {
    (void)size; (void)ud;
    uint32_t r[8], ef;
    get_regs(r);
    watch_check(bc, (uint32_t)addr);
    if (done((uint32_t)addr, r[ESP])) { uc_emu_stop(u); return; }
    if (addr < lo || addr >= hi) return;
    static uint32_t last = ~0u;
    if ((uint32_t)addr == last) return;              // a rep instruction: one record, like the lifted side
    last = (uint32_t)addr;
    uc_reg_read(u, UC_X86_REG_EFLAGS, &ef);
    uint64_t mm[8];
    for (int k = 0; k < 8; k++) { uint8_t fp[10]; uc_reg_read(u, UC_X86_REG_FP0 + k, fp); memcpy(&mm[k], fp, 8); }
    put((uint32_t)addr, r, (ef & 1) | (ef >> 6 & 1) << 1 | (ef >> 7 & 1) << 2 | (ef >> 11 & 1) << 3, mm);
}
static void run_reference(Ctx *c, uint32_t entry) {
    bc = c;
    if (uc_open(UC_ARCH_X86, UC_MODE_32, &uc)) { fprintf(stderr, "difftrace: uc_open\n"); _exit(1); }
    if (uc_mem_map_ptr(uc, 0, c->memsz, UC_PROT_ALL, c->mem)) { fprintf(stderr, "difftrace: map\n"); _exit(1); }
    for (int k = 0; k < 8; k++) uc_reg_write(uc, ureg[k], &c->r[k]);
    uint32_t ef = 2u | (uint32_t)c->cf | (uint32_t)c->pf << 2 | (uint32_t)c->af << 4 | (uint32_t)c->zf << 6 |
                  (uint32_t)c->sf << 7 | (uint32_t)c->df << 10 | (uint32_t)c->of << 11;
    uc_reg_write(uc, UC_X86_REG_EFLAGS, &ef);
    // In 32-bit mode Unicorn ignores FS_BASE: fs:[0] (the SEH chain) needs a real descriptor, in a GDT page of its
    // own above guest memory. Once a GDT is loaded Unicorn 2.0.1 no longer treats SS/DS as flat, so all segments get
    // ring-0 descriptors: 1 flat code, 2 flat data (SS DS ES), 3 data based at the thread's TEB (FS).
    enum { GDT = 0xd0000000u };
    uint64_t gdt[4] = {0, 0x00cf9a000000ffffull, 0x00cf92000000ffffull,
                       0x00cf92000000ffffull | (uint64_t)(c->fs & 0xffffff) << 16 | (uint64_t)(c->fs >> 24) << 56};
    uc_mem_map(uc, GDT, 0x1000, UC_PROT_READ | UC_PROT_WRITE);
    uc_mem_write(uc, GDT, gdt, sizeof gdt);
    uc_x86_mmr gdtr = {0, GDT, sizeof gdt - 1, 0};
    uc_reg_write(uc, UC_X86_REG_GDTR, &gdtr);
    uint32_t sel[] = {8, 16, 16, 16, 24};
    const int sreg[] = {UC_X86_REG_CS, UC_X86_REG_SS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_FS};
    for (int k = 0; k < 5; k++) uc_reg_write(uc, sreg[k], &sel[k]);
    uint16_t fcw = (uint16_t)c->fcw;
    uc_reg_write(uc, UC_X86_REG_FPCW, &fcw);
    for (int k = 0; k < 8; k++) {                    // MMk = mantissa of physical register k, as A holds it
        uint8_t fp[10];
        memcpy(fp, &c->mm[k], 8); fp[8] = fp[9] = 0xff;
        uc_reg_write(uc, UC_X86_REG_FP0 + k, fp);
    }
    uc_hook h;
    uc_hook_add(uc, &h, UC_HOOK_CODE, hook_code, NULL, 1, 0);
    uint32_t eip = entry;
    for (;;) {
        uc_err e = uc_emu_start(uc, eip, 0xffffffffu, 0, 0);
        uint32_t r[8];
        get_regs(r);
        uc_reg_read(uc, UC_X86_REG_EIP, &eip);
        if (done(eip, r[ESP])) break;
        if (eip >= HOST_BASE) {                        // a host API: run it on the same memory, resume at its return
            memcpy(c->r, r, sizeof r);
            rn_host_call(c, eip);
            if (c->halt) { fprintf(stderr, "difftrace: B halted in a host call: %s\n", c->why); break; }
            for (int k = 0; k < 8; k++) uc_reg_write(uc, ureg[k], &c->r[k]);
            eip = c->ret;
            continue;
        }
        fprintf(stderr, "difftrace: B stopped at %08x (%s)\n", eip, uc_strerror(e));
        break;
    }
    finish();
}

void rn_difftrace(Ctx *c, uint32_t entry) {
    static int on = -1, n;
    static uint32_t at, want;
    if (on < 0) {
        const char *e = getenv("RN_DT");
        on = e && sscanf(e, "%x:%u:%x-%x", &at, &want, &lo, &hi) >= 2;
        if (on && !hi) { lo = 0; hi = 0xffffffffu; }
        if ((e = getenv("RN_DT_WATCH"))) watch = (uint32_t)strtoul(e, 0, 16);
    }
    if (!on || entry != at || ++n != (int)want) return;
    mc = c;
    ret_at = ld(c, c->r[ESP], 4);
    esp0 = c->r[ESP];
    if (watch) wval = ld(c, watch, 4);
    fflush(stdout);
    rn_log("difftrace: call %d of %08x, return %08x, esp %08x", n, entry, ret_at, esp0);
    pid_t b = fork();
    if (b == 0) { rn_gil_off = 1; side = 'B'; out = fopen("/tmp/dt_b.bin", "wb"); run_reference(c, entry); }
    pid_t a = fork();
    if (a == 0) { rn_gil_off = 1; side = 'A'; out = fopen("/tmp/dt_a.bin", "wb"); active = 1; return; }   // A: back into the lifted function
    int st;
    waitpid(a, &st, 0);
    waitpid(b, &st, 0);
    rn_log("difftrace: done, compare with python3 tools/dtcmp.py");
    _exit(0);
}
#endif
