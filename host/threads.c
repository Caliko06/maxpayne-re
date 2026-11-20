// Threads and synchronization. Every guest thread is a host thread, but one global lock (the GIL,
// handed out in ticket order) lets only one of them run guest code at a time. A thread gives the
// lock up only inside APIs that block on Windows: Sleep, waits, a critical section owned by another
// thread. So the guest sees real threads; the lifted code needs no atomics.
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include "host.h"

#define PROCESS_ID 0x8
#define WAIT_TIMEOUT 0x102u
#define INFINITE 0xffffffffu

// A thread is created (suspended or not), runs, and ends; a timer thread parks instead and a new timeSetEvent re-arms it.
enum { T_NEW, T_RUNNING, T_PARKED, T_DONE };
typedef struct Thread {
    Ctx ctx;
    uint32_t id, handle, start, param, exit_code;
    int state, suspended;               // state: T_*; suspended: the SuspendThread count
    pthread_t pt;
    uint32_t timer_ms, timer_flags, timer_user, timer_id, killed;   // winmm timers
    uint32_t stack_top;
} Thread;
static Thread *threads[64];
static int nthreads;

static pthread_mutex_t gm = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gcv = PTHREAD_COND_INITIALIZER;   // GIL turn changed
static pthread_cond_t scv = PTHREAD_COND_INITIALIZER;   // some object changed state
static unsigned long next_ticket, serving, gen;

static uint64_t slice_start, hold_start;
static __thread int is_main;
int rn_gil_off;
static __thread Thread *self;           // this guest thread (NULL: the main thread)
// A suspended thread (SuspendThread) runs no guest code: when its GIL turn comes it hands the turn on and waits
// until ResumeThread brings its count to 0. Only the GIL holder runs guest code, so it stops between APIs.
void gil_acquire(void) {
    if (rn_gil_off) return;
    uint64_t t0 = now_us();
    pthread_mutex_lock(&gm);
    for (;;) {
        unsigned long t = next_ticket++;
        while (t != serving) pthread_cond_wait(&gcv, &gm);
        if (!self || !self->suspended) break;
        serving++;
        pthread_cond_broadcast(&gcv);
        while (self->suspended) pthread_cond_wait(&scv, &gm);
    }
    pthread_mutex_unlock(&gm);
    slice_start = hold_start = now_us();
    *(is_main ? &rn_telem.gil_main_ms : &rn_telem.gil_other_ms) += (double)(slice_start - t0) / 1000;
}
// Windows preempts a thread when its quantum (about 20 ms on XP) ends. A guest that polls for work
// done by another thread relies on it. The GIL holder gives its turn away at the next API call
// once it has run a quantum (since it got the GIL) while another thread waits for the GIL.
// This runs at every API call (thousands per frame): with no waiter it reads no clock (a clock read
// is a call out to JS on the web), and with one it reads the clock every 16th call.
#define QUANTUM_US 20000
static unsigned slice_calls;
void gil_slice(void) {
    if (rn_gil_off) return;
    if (__atomic_load_n(&next_ticket, __ATOMIC_RELAXED) == __atomic_load_n(&serving, __ATOMIC_RELAXED) + 1) return;
    if (++slice_calls & 15 || now_us() - slice_start < QUANTUM_US) return;
    gil_release();
    gil_acquire();
}
void rn_spin(void) { gil_release(); sched_yield(); gil_acquire(); }
static void count_hold(void) {
    double held = (double)(now_us() - hold_start) / 1000, *m = is_main ? &rn_telem.hold_main_max_ms : &rn_telem.hold_other_max_ms;
    if (held > *m) *m = held;
    // RN_HOLD=<ms>: who ended a long main-thread hold, and how many API calls it spanned with a waiter queued (debug)
    static int hold_log = -1;
    if (hold_log < 0) hold_log = getenv("RN_HOLD") ? atoi(getenv("RN_HOLD")) : 0;
    if (hold_log && is_main && held >= hold_log) {
        static unsigned logged, last_calls;
        if (logged++ < 40) { rn_log("hold %.0f ms, %u API calls with a waiter", held, slice_calls - last_calls); rn_backtrace(g_ctx); }
        last_calls = slice_calls;
    }
}
void gil_release(void) {
    if (rn_gil_off) return;
    count_hold();
    pthread_mutex_lock(&gm);
    serving++;
    pthread_cond_broadcast(&gcv);
    pthread_mutex_unlock(&gm);
}
static void notify(void) {
    pthread_mutex_lock(&gm);
    gen++;
    pthread_cond_broadcast(&scv);
    pthread_mutex_unlock(&gm);
}
// Give up the GIL until some object changes state or the deadline (µs, 0 = none) passes.
static void wait_change(uint64_t deadline) {
    uint64_t t0 = now_us();
    count_hold();
    pthread_mutex_lock(&gm);
    unsigned long g = gen;
    serving++;
    pthread_cond_broadcast(&gcv);
    while (gen == g) {
        uint64_t now = now_us();
        if (deadline && now >= deadline) break;
        uint64_t step = deadline ? deadline - now : 100000;
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        uint64_t ns = (uint64_t)ts.tv_nsec + step * 1000;
        ts.tv_sec += ns / 1000000000ull;
        ts.tv_nsec = ns % 1000000000ull;
        pthread_cond_timedwait(&scv, &gm, &ts);
    }
    pthread_mutex_unlock(&gm);
    if (is_main) rn_telem.idle_main_ms += (double)(now_us() - t0) / 1000;   // before the GIL is back: this thread's field
    gil_acquire();
}

static uint32_t tid(Ctx *c) { return ld(c, c->fs + 0x24, 4); }

static void teb_init(Ctx *c, uint32_t teb, uint32_t lo, uint32_t hi, uint32_t id) {
    c->fs = teb;
    st(c, teb + 0x00, 0xffffffffu, 4);     // SEH chain end
    st(c, teb + 0x04, hi, 4);
    st(c, teb + 0x08, lo, 4);
    st(c, teb + 0x18, teb, 4);
    st(c, teb + 0x20, PROCESS_ID, 4);
    st(c, teb + 0x24, id, 4);
    st(c, teb + 0x2c, teb + 0xe10, 4);     // TLS slots
    st(c, teb + 0x30, PEB_ADDR, 4);
}

void threads_init(Ctx *c) {
    teb_init(c, TEB_ADDR, STACK_LO, STACK_HI, 0xc);
    is_main = 1;
    gil_acquire();
}

// ---- threads -------------------------------------------------------------------------------
static void *run(void *arg) {
    Thread *t = arg;
    self = t;
    gil_acquire();
    Ctx *c = &t->ctx;
    uint32_t args[1] = {t->param};
    if (rn_trace()) rn_log("thread %x starts at %08x", t->id, t->start);
    uint32_t r = call_guest(c, t->start, 1, args);
    if (c->skipped) rn_halt(c, "thread %x: a return to %08x reached the thread base", t->id, c->ret);
    if (c->halt) rn_stop(c);
    t->exit_code = r;
    t->state = T_DONE;
    notify();
    gil_release();
    return NULL;
}
// winmm timer: TIME_PERIODIC (1) or one shot; TIME_CALLBACK_EVENT_SET (0x10) or a TIMECALLBACK.
// A one-shot or killed timer's thread does not end: it parks until a new timeSetEvent re-arms it
// (reuse_timer). Games re-arm one-shot timers from inside the callback, every few milliseconds, and a
// thread start on the web waits for the page's thread, with the GIL held (measured: 16% of the main
// thread's race time waiting for that GIL).
static void *run_timer(void *arg) {
    Thread *t = arg;
    self = t;
    gil_acquire();
    Ctx *c = &t->ctx;
    for (;;) {
        uint64_t next = now_us();
        do {
            next += (uint64_t)t->timer_ms * 1000;
            while (!t->killed && now_us() < next) wait_change(next);
            if (t->killed) break;
            if (t->timer_flags & 0x10) {
                Handle *h = get_handle(t->start);
                if (h && h->kind == H_EVENT) { h->signaled = 1; notify(); }
            } else {
                uint32_t args[5] = {t->timer_id, 0, t->timer_user, 0, 0};
                call_guest(c, t->start, 5, args);
                if (c->halt) rn_stop(c);
            }
        } while (t->timer_flags & 1);
        t->state = T_PARKED;
        while (t->state == T_PARKED) wait_change(0);
    }
    return NULL;
}
static void start_fn(Thread *t, void *(*fn)(void *)) {
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setstacksize(&a, 16u << 20);   // lifted guest calls are host calls
    t->state = T_RUNNING;
    if (pthread_create(&t->pt, &a, fn, t)) { fprintf(stderr, "pthread_create failed\n"); exit(1); }
}
static void start(Thread *t) { start_fn(t, run); }
static Thread *thread_of(uint32_t h) {
    Handle *x = get_handle(h);
    return x && x->kind == H_THREAD ? x->thread : NULL;
}

static Thread *new_thread(Ctx *c, uint32_t size) {
    if (nthreads == 64) { rn_halt(c, "too many threads"); return NULL; }
    Thread *t = calloc(1, sizeof *t);
    uint32_t reserve = size > 0x100000 ? size : 0x100000;
    uint32_t stack = virtual_reserve(c, reserve), teb = guest_alloc(0x1000);
    if (!stack || !teb) { set_error(c, 8); free(t); return NULL; }
    t->ctx = *c;                      // same memory; fresh registers below
    memset(t->ctx.r, 0, sizeof t->ctx.r);
    t->ctx.halt = t->ctx.uw = t->ctx.skipped = 0; t->ctx.cf = t->ctx.zf = t->ctx.sf = t->ctx.of = t->ctx.pf = t->ctx.df = t->ctx.af = 0;
    finit(&t->ctx);
    t->ctx.fcw = 0x27f;
    t->id = 0x10 + 4u * nthreads;
    teb_init(&t->ctx, teb, stack, stack + reserve, t->id);
    t->stack_top = stack + reserve - 16;
    t->ctx.r[ESP] = t->stack_top;
    threads[nthreads++] = t;
    return t;
}
// A new timer takes a parked timer thread (its stack and TEB too); timeSetEvent_ wakes it.
static Thread *reuse_timer(void) {
    for (int k = 0; k < nthreads; k++) {
        Thread *t = threads[k];
        if (!t->timer_id || t->state != T_PARKED) continue;
        memset(t->ctx.r, 0, sizeof t->ctx.r);
        t->ctx.halt = t->ctx.uw = t->ctx.skipped = 0; t->ctx.cf = t->ctx.zf = t->ctx.sf = t->ctx.of = t->ctx.pf = t->ctx.df = t->ctx.af = 0;
        finit(&t->ctx);
        t->ctx.fcw = 0x27f;
        t->ctx.r[ESP] = t->stack_top;
        t->state = T_RUNNING; t->killed = 0;
        return t;
    }
    return NULL;
}
static uint32_t CreateThread_(Ctx *c) {
    uint32_t size = ARG(1), entry = ARG(2), param = ARG(3), flags = ARG(4), out = ARG(5);
    Thread *t = new_thread(c, size);
    if (!t) return 0;
    t->start = entry; t->param = param;
    t->handle = new_handle(H_THREAD);
    get_handle(t->handle)->thread = t;
    if (out) st(c, out, t->id, 4);
    rn_log("CreateThread %08x(%08x) -> thread %x%s", entry, param, t->id, (flags & 4) ? " suspended" : "");
    if (flags & 4) t->suspended = 1;
    else start(t);
    return t->handle;
}
static uint32_t ResumeThread_(Ctx *c) {
    Thread *t = thread_of(ARG(0));
    if (!t) { rn_halt(c, "ResumeThread on handle %08x", ARG(0)); return 0xffffffffu; }
    pthread_mutex_lock(&gm);
    uint32_t prev = (uint32_t)t->suspended;
    if (t->suspended) t->suspended--;
    pthread_mutex_unlock(&gm);
    if (prev == 1 && t->state == T_NEW) start(t);
    else if (prev == 1) notify();
    return prev;
}
// The caller holds the GIL, so the target is not running guest code; it parks at its next GIL turn.
static uint32_t SuspendThread_(Ctx *c) {
    Thread *t = thread_of(ARG(0));
    if (!t) { set_error(c, 6); return 0xffffffffu; }
    pthread_mutex_lock(&gm);
    uint32_t prev = (uint32_t)t->suspended++;
    pthread_mutex_unlock(&gm);
    return prev;
}
static uint32_t GetExitCodeThread_(Ctx *c) {
    Thread *t = thread_of(ARG(0));
    if (!t) { set_error(c, 6); return 0; }
    st(c, ARG(1), t->state == T_DONE ? t->exit_code : 259, 4);    // STILL_ACTIVE
    return 1;
}
// msvcrt _endthreadex 0x10803852 (AfxEndThread, the end of the sound thread proc 0x10303360). Lifted frames hold no state.
static uint32_t ExitThread_(Ctx *c) {
    if (!self || self->timer_id) { rn_halt(c, "ExitThread(%u) on the main or a timer thread", ARG(0)); return 0; }
    self->exit_code = ARG(0);
    self->state = T_DONE;
    notify();
    gil_release();
    pthread_exit(NULL);
}
static uint32_t TerminateThread_(Ctx *c) { rn_halt(c, "TerminateThread(%08x)", ARG(0)); return 0; }
static uint32_t GetCurrentThreadId_(Ctx *c) { return tid(c); }

static uint32_t timeSetEvent_(Ctx *c) {
    uint32_t ms = ARG(0), cb = ARG(2), user = ARG(3), flags = ARG(4);
    if (flags & ~0x111u) { rn_halt(c, "timeSetEvent flags %08x", flags); return 0; }
    Thread *t = reuse_timer(), *parked = t;
    if (!t) t = new_thread(c, 0);
    if (!t) return 0;
    t->timer_ms = ms ? ms : 1; t->timer_flags = flags; t->timer_user = user; t->start = cb;
    t->timer_id = t->id;
    if (rn_trace()) rn_log("timeSetEvent %u ms -> %s %08x", ms, (flags & 0x10) ? "event" : "callback", cb);
    if (parked) notify();
    else start_fn(t, run_timer);
    return t->timer_id;
}
static uint32_t timeKillEvent_(Ctx *c) {
    for (int k = 0; k < nthreads; k++)
        if (threads[k]->timer_id && threads[k]->timer_id == ARG(0)) { threads[k]->killed = 1; notify(); return 0; }
    return 97;   // TIMERR_NOCANDO
}

// Pseudo handles of this process/thread duplicate into a real handle.
static uint32_t DuplicateHandle_(Ctx *c) {
    uint32_t src = ARG(1), out = ARG(3);
    if (ARG(0) != 0xffffffffu || ARG(2) != 0xffffffffu || (src != 0xfffffffeu && src != 0xffffffffu)) {
        rn_halt(c, "DuplicateHandle of handle %08x", src);
        return 0;
    }
    uint32_t h = new_handle(H_THREAD);
    if (src == 0xfffffffeu)
        for (int k = 0; k < nthreads; k++)
            if (threads[k]->id == tid(c)) get_handle(h)->thread = threads[k];
    if (out) st(c, out, h, 4);
    return 1;
}

// ---- waits ---------------------------------------------------------------------------------
// 1 = signaled (and consumed for auto-reset events and mutexes), 0 = not yet, -1 = not waitable.
static int try_take(Ctx *c, uint32_t hv) {
    Handle *h = get_handle(hv);
    if (!h) return -1;
    switch (h->kind) {
    case H_EVENT:
        if (!h->signaled) return 0;
        if (!h->manual) h->signaled = 0;
        return 1;
    case H_MUTEX:
        if (h->count && h->owner != tid(c)) return 0;
        h->owner = tid(c);
        h->count++;
        return 1;
    case H_THREAD:
        return h->thread ? h->thread->state == T_DONE : -1;
    }
    return -1;
}
static uint64_t deadline_of(uint32_t ms) { return ms == INFINITE ? 0 : now_us() + (uint64_t)ms * 1000 + (ms == 0); }

static uint32_t WaitForSingleObject_(Ctx *c) {
    uint32_t h = ARG(0), ms = ARG(1);
    uint64_t dl = deadline_of(ms);
    for (;;) {
        int r = try_take(c, h);
        if (r < 0) { rn_halt(c, "WaitForSingleObject on handle %08x", h); return 0; }
        if (r) return 0;
        if (ms == 0 || (dl && now_us() >= dl)) return WAIT_TIMEOUT;
        if (ms == INFINITE && nthreads == 0) { rn_halt(c, "WaitForSingleObject(INFINITE) with no other thread"); return 0; }
        wait_change(dl);
    }
}
static uint32_t WaitForMultipleObjects_(Ctx *c) {
    uint32_t n = ARG(0), arr = ARG(1), all = ARG(2), ms = ARG(3);
    uint64_t dl = deadline_of(ms);
    if (all) { rn_halt(c, "WaitForMultipleObjects(wait all)"); return 0; }
    for (;;) {
        for (uint32_t k = 0; k < n; k++) {
            int r = try_take(c, ld(c, arr + 4 * k, 4));
            if (r < 0) { rn_halt(c, "WaitForMultipleObjects on handle %08x", ld(c, arr + 4 * k, 4)); return 0; }
            if (r) return k;
        }
        if (ms == 0 || (dl && now_us() >= dl)) return WAIT_TIMEOUT;
        wait_change(dl);
    }
}
static uint32_t Sleep_(Ctx *c) {
    uint32_t ms = ARG(0);
    gil_release();
    if (ms) { struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L}; nanosleep(&ts, NULL); }
    else sched_yield();
    gil_acquire();
    (void)c;
    return 0;
}
static uint32_t SleepEx_(Ctx *c) { Sleep_(c); return 0; }

static uint32_t CreateEventA_(Ctx *c) {
    if (ARG(3)) { rn_halt(c, "CreateEventA named %s", G(ARG(3))); return 0; }
    uint32_t h = new_handle(H_EVENT);
    get_handle(h)->manual = ARG(1) != 0;
    get_handle(h)->signaled = ARG(2) != 0;
    set_error(c, 0);
    return h;
}
static uint32_t SetEvent_(Ctx *c) {
    Handle *h = get_handle(ARG(0));
    if (!h || h->kind != H_EVENT) { set_error(c, 6); return 0; }
    h->signaled = 1;
    notify();
    return 1;
}
static uint32_t ResetEvent_(Ctx *c) {
    Handle *h = get_handle(ARG(0));
    if (!h || h->kind != H_EVENT) { set_error(c, 6); return 0; }
    h->signaled = 0;
    return 1;
}
// Named mutexes live in this process only: the first CreateMutexA of a name creates it.
static struct { char name[128]; uint32_t h; } named[16];
static uint32_t CreateMutexA_(Ctx *c) {
    uint32_t own = ARG(1), name = ARG(2);
    if (name)
        for (int k = 0; k < 16; k++)
            if (named[k].h && !strcmp(named[k].name, G(name))) { set_error(c, 183); return named[k].h; }
    uint32_t h = new_handle(H_MUTEX);
    if (own) { get_handle(h)->owner = tid(c); get_handle(h)->count = 1; }
    if (name)
        for (int k = 0; k < 16; k++)
            if (!named[k].h) { snprintf(named[k].name, sizeof named[k].name, "%s", G(name)); named[k].h = h; break; }
    set_error(c, 0);
    return h;
}
static uint32_t ReleaseMutex_(Ctx *c) {
    Handle *h = get_handle(ARG(0));
    if (!h || h->kind != H_MUTEX || !h->count || h->owner != tid(c)) { set_error(c, 288); return 0; }
    if (!--h->count) { h->owner = 0; notify(); }
    return 1;
}

// CRITICAL_SECTION: +4 LockCount, +8 RecursionCount, +12 OwningThread.
static int cs_waiters;
static uint32_t InitializeCriticalSection_(Ctx *c) {
    uint32_t p = ARG(0);
    memset(G(p), 0, 24);
    st(c, p + 4, 0xffffffffu, 4);
    return 0;
}
static uint32_t InitializeCriticalSectionAndSpinCount_(Ctx *c) { InitializeCriticalSection_(c); return 1; }
static uint32_t EnterCriticalSection_(Ctx *c) {
    uint32_t p = ARG(0), me = tid(c);
    while (ld(c, p + 8, 4) && ld(c, p + 12, 4) != me) {
        cs_waiters++;
        wait_change(0);
        cs_waiters--;
    }
    st(c, p + 4, ld(c, p + 4, 4) + 1, 4);
    st(c, p + 8, ld(c, p + 8, 4) + 1, 4);
    st(c, p + 12, me, 4);
    return 0;
}
static uint32_t LeaveCriticalSection_(Ctx *c) {
    uint32_t p = ARG(0), rc = ld(c, p + 8, 4) - 1;
    st(c, p + 4, ld(c, p + 4, 4) - 1, 4);
    st(c, p + 8, rc, 4);
    if (!rc) { st(c, p + 12, 0, 4); if (cs_waiters) notify(); }
    return 0;
}
static uint32_t DeleteCriticalSection_(Ctx *c) { (void)c; return 0; }

#define T(name, n) {"kernel32.dll", #name, n, name##_}
const Api thread_apis[] = {
    T(CreateThread, 6), T(ResumeThread, 1), T(SuspendThread, 1), T(GetExitCodeThread, 2), T(ExitThread, 1),
    T(TerminateThread, 2), T(GetCurrentThreadId, 0), T(DuplicateHandle, 7),
    T(WaitForSingleObject, 2), T(WaitForMultipleObjects, 4), T(Sleep, 1), T(SleepEx, 2),
    T(CreateEventA, 4), T(SetEvent, 1), T(ResetEvent, 1), T(CreateMutexA, 3), T(ReleaseMutex, 1),
    T(InitializeCriticalSection, 1), T(InitializeCriticalSectionAndSpinCount, 2), T(EnterCriticalSection, 1),
    T(LeaveCriticalSection, 1), T(DeleteCriticalSection, 1),
    {"winmm.dll", "timeSetEvent", 5, timeSetEvent_}, {"winmm.dll", "timeKillEvent", 1, timeKillEvent_},
};
const int thread_api_count = sizeof thread_apis / sizeof thread_apis[0];
