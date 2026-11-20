// dsound: DirectSoundCreate and the sound buffers, as documented by Microsoft (DirectX 9.0b).
// The PC has one stereo output at 44.1 kHz with a software mixer (no hardware buffers). Buffers live in
// guest memory, so Lock hands out pointers into them, and the mixer reads them the way a sound card
// reads its DMA buffers: from its own thread, while the game runs. The mixer keeps LEAD frames ahead
// of the wall clock; the play cursor is what sounds now, the write cursor is what is already mixed.
// Output: web = a ring in the shared heap that web/audio.js plays; native = RN_WAV=out.wav records it.
#include <math.h>
#include <pthread.h>
#include <time.h>
#include "host.h"

#define DS_OK 0u
#define DSERR_INVALIDPARAM 0x80070057u
#define DSERR_INVALIDCALL 0x88780032u
#define DSERR_BADFORMAT 0x88780064u
#define DSERR_NOINTERFACE 0x80004002u
#define DSERR_PRIOLEVELNEEDED 0x88780046u
#define RATE 44100
#define LEAD 2048                       // frames mixed ahead of the clock (46 ms)

#define TR(...) do { if (rn_trace()) rn_log("dsound " __VA_ARGS__); } while (0)
// RN_SOUNDLOG: the first buffers, Plays and Stops (what the game feeds and for how long) and, every 3 s while the game
// plays, the accounting of "Sound accounting" below (Plays that reached the speakers or not and why, how Plays ended, COM
// errors and no-op calls, the web ring); default runs print only errors.
static int soundlog(void) { static int v = -1; if (v < 0) v = getenv("RN_SOUNDLOG") != NULL; return v; }
// RN_SOUNDLOG=2: every Play/Stop line, not just the first 60 (a scene after the first minute: death music, ending)
static int soundlog_cap(void) { static int v = -1; if (v < 0) { const char *e = getenv("RN_SOUNDLOG"); v = e && atoi(e) >= 2 ? 1 << 30 : 60; } return v; }

enum { I_DS, I_BUF, I_L3D, I_B3D, I_N };
#define UNK "QueryInterface", "AddRef", "Release"
static const char *const m_ds[] = {UNK, "CreateSoundBuffer", "GetCaps", "DuplicateSoundBuffer", "SetCooperativeLevel",
    "Compact", "GetSpeakerConfig", "SetSpeakerConfig", "Initialize", 0};
static const char *const m_buf[] = {UNK, "GetCaps", "GetCurrentPosition", "GetFormat", "GetVolume", "GetPan",
    "GetFrequency", "GetStatus", "Initialize", "Lock", "Play", "SetCurrentPosition", "SetFormat", "SetVolume",
    "SetPan", "SetFrequency", "Stop", "Unlock", "Restore", 0};
// IDirectSound3DListener / IDirectSound3DBuffer: sndmfc sets only the methods that are not "Get" (rea/map/sound.md 3).
static const char *const m_l3d[] = {UNK, "GetAllParameters", "GetDistanceFactor", "GetDopplerFactor", "GetOrientation",
    "GetPosition", "GetRolloffFactor", "GetVelocity", "SetAllParameters", "SetDistanceFactor", "SetDopplerFactor",
    "SetOrientation", "SetPosition", "SetRolloffFactor", "SetVelocity", "CommitDeferredSettings", 0};
static const char *const m_b3d[] = {UNK, "GetAllParameters", "GetConeAngles", "GetConeOrientation", "GetConeOutsideVolume",
    "GetMaxDistance", "GetMinDistance", "GetMode", "GetPosition", "GetVelocity", "SetAllParameters", "SetConeAngles",
    "SetConeOrientation", "SetConeOutsideVolume", "SetMaxDistance", "SetMinDistance", "SetMode", "SetPosition",
    "SetVelocity", 0};
static const char *const iface_name[I_N] = {"IDirectSound", "IDirectSoundBuffer", "IDirectSound3DListener", "IDirectSound3DBuffer"};
static const char *const *const iface_methods[I_N] = {m_ds, m_buf, m_l3d, m_b3d};
static uint32_t vtables[I_N];

static uint32_t vtable(int i) {
    if (vtables[i]) return vtables[i];
    int n = 0;
    while (iface_methods[i][n]) n++;
    uint32_t v = guest_alloc(4u * n);
    for (int k = 0; k < n; k++) {
        char name[96];
        snprintf(name, sizeof name, "%s::%s", iface_name[i], iface_methods[i][k]);
        st(g_ctx, v + 4u * k, host_addr_of("dsound.dll", name), 4);
    }
    return vtables[i] = v;
}

// A buffer: PCM data in guest memory. pos = ring position in source frames (fractional: resampling).
typedef struct {
    int iface, primary, playing, looping;
    uint32_t refs, guest, flags, data, size;
    uint16_t channels, bits, align;
    uint32_t rate, freq;                // format rate; SetFrequency (0 = the format rate)
    int32_t volume, pan;                // hundredths of a dB
    double pos, mixed_pos, play_pos;   // ring position, unwrapped mix and last reported play positions
    double since_unlock;                // telemetry: frames played since the last Unlock (a stream refills by Unlock)
    uint32_t unlocks;
    int written;                        // the guest has written the data since the host filled it (Unlock): never written = the host's silence
    uint32_t wrote_lo, wrote_hi;        // byte range the guest wrote since the last Play (0/0 = nothing)
    // A stream is refilled by the game behind the play cursor (sndmfc 0x1030a140). avail = frames of valid data ahead of pos
    // (the first Unlock fills the buffer, every later one adds what the game wrote). The mixer holds a stream whose valid
    // data ran out instead of looping the stale lap, as a long frame without the game's update would: see mix().
    double avail;
    int held;
    int fade_in;                        // frames left of the resume ramp after a hold
    uint64_t streamed, play_us;         // telemetry: bytes unlocked since the last Stop, and when Play ran
    int parent;                         // 3D interfaces: index of the buffer (or the primary) they came from, else -1
    int has3d;                          // a buffer with an IDirectSound3DBuffer: position in pos[], distances min/max
    float p3[3], min_d, max_d;
    // Sound accounting of the current Play (see below): frames mixed, frames held back, and the largest source sample, volume*pan
    // gain, DS3D gain and output sample seen while it played.
    int in_play;
    uint32_t pframes, pheld;
    float pin, pgain, p3max, pout;
    uint16_t nm;                        // index in vt[]: the name of the S_Sound that holds this voice (#11)
} Obj;
#define MAXO 512
static Obj objs[MAXO];
static int nobjs;
static pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;     // buffer play state, shared with the mixer
static int32_t master = 0;              // primary buffer volume

// IDirectSound3DListener state (one listener: the primary buffer's). The game keeps it at the origin facing +Z.
static float lis_pos[3], lis_front[3] = {0, 0, 1}, lis_top[3] = {0, 1, 0}, lis_rolloff = 1.0f;
// ---- Sound accounting (the counting is always on and cheap; RN_SOUNDLOG prints it, see report()) ---------------------------
// Every Play on a secondary buffer is followed until it ends, and then judged: did one sample louder than 1 LSB of 16 bits
// (3e-5) leave the mixer for it? If not, the cause is named. The game's own losses are told apart from the host's:
//  - game: volume -10000 (sndmfc 0x1030ae20 sends it for v < 0.01 or a muted listener); steals (the lowest priority sound loses its
//    voice: acquireVoice 0x1030aad0 -> releaseVoice 0x1030bb60), voice release by the update tick 0x10302d70, pause, audibility.
//  - host: Stop before the mixer ran (a Play the host ended, E_DATA_END/E_REPLAYED), a stream held because the game did not
//    refill (mix), DS3D max distance, silent data, and a buffer the guest never wrote at all. A Play the *game* withdrew
//    before the first mixer pass is its own counter (P_CANCELLED), out of the host group: real DirectSound's period was
//    10-20 ms, so Windows would not have played it either.
// The way a Play ended comes from the return address of IDirectSoundBuffer::Stop and the caller found on the stack above it.
enum { P_NOMIX, P_HELD, P_MUTE_3D, P_ZERO_DATA, P_QUIET, P_MUTE_VOL, P_CANCELLED, P_NEVER_FILLED, P_N };
static const char *const lose_name[P_N] = {"stopped before the mixer ran", "held, game did not refill", "DS3D max distance",
    "mixed samples all zero", "under 1 LSB", "game volume off",
    "the game released the voice before the first mixer pass", "mixed samples all zero, the guest never wrote the buffer"};
enum { E_STEAL, E_REL_TICK, E_REL_PAUSE, E_REL_AUDIB, E_REL_OTHER, E_STOP_TICK, E_STOP_LISTENER, E_STOP_DESTRUCT, E_STOP_SAVE,
       E_STOP_RESTART, E_STOP_VIRTUAL, E_STOP_VPOS, E_STOP_OTHER, E_DATA_END, E_RELEASED, E_REPLAYED, E_N };
static const char *const end_name[E_N] = {"steal (acquireVoice)", "voice released by update tick", "voice released by pause",
    "voice released by audibility off", "voice released, other caller", "stop by update tick", "stop by S_Listener::stop",
    "stop by S_Sound::destruct", "stop by saveRaw", "stop by startPlaying", "stop by startPlayingVirtual",
    "stop by updateVirtualPosition", "stop, other caller", "non-looping data ran out (host)", "buffer Released while playing",
    "Play again after Play (host)"};
// Return addresses of the calls to releaseVoice 0x1030bb60 (release = 1) and S_Sound::stop 0x1030b9a0 (release = 0): call site + 5.
static const struct { uint32_t ret; int release, kind; } stop_sites[] = {
    {0x1030ac20, 1, E_STEAL},          /* 0x1030aad0 acquireVoice, call at 0x1030ac1b */
    {0x10302ffe, 1, E_REL_TICK},       /* 0x10302d70 update tick, 0x10302ff9 */
    {0x1030b975, 1, E_REL_PAUSE},      /* 0x1030b930 pause, 0x1030b970 */
    {0x1030bdda, 1, E_REL_AUDIB},      /* 0x1030bdc0 setAudibility, 0x1030bdd5 */
    {0x1030306a, 0, E_STOP_TICK},      /* 0x10302d70 update tick, 0x10303065 */
    {0x103041e7, 0, E_STOP_LISTENER},  /* 0x103041b0 S_Listener::stop, 0x103041e2 */
    {0x10309c73, 0, E_STOP_DESTRUCT},  /* 0x10309c20 S_Sound::destruct, 0x10309c6e */
    {0x10309f3e, 0, E_STOP_SAVE},      /* 0x10309f30 saveRaw, 0x10309f39 */
    {0x1030b58a, 0, E_STOP_RESTART},   /* 0x1030b380 startPlaying, 0x1030b585 */
    {0x1030b7bd, 0, E_STOP_VIRTUAL},   /* 0x1030b6e0 startPlayingVirtual, 0x1030b7b8 */
    {0x1030bd96, 0, E_STOP_VPOS},      /* 0x1030bc60 updateVirtualPosition, 0x1030bd91 */
};
static struct {
    uint64_t plays, started, audible, lost[P_N], ends[E_N], ends_silent[E_N];
    uint32_t concurrent_peak;
} acct;
// Per-name accounting of the sounds the game asks for (#11): sound_virtual names them every tick, play_begin/play_end
// count the Plays that got a voice and one that left the mixer. Declared here because the mixer counts too.
#define VN 384
static struct { char name[48]; uint32_t virt, voiced, plays, audible; } vt[VN] = {{"unknown"}};
static int nvt = 1;                     // slot 0 is "unknown": nm starts at 0 from the memset of a new slot
// Index in vt[] of the file name of the S_Sound at guest s (data +0x114, length +0x118); a new name is added. -1 if full.
static int vt_index(Ctx *c, uint32_t s) {
    char nm[48]; uint32_t np = 0, nl = 0, k = 0;
    if (s >= LOW && s <= c->memsz - 0x120) { np = ld(c, s + 0x114, 4); nl = ld(c, s + 0x118, 4); }
    if (np < LOW || np > c->memsz - sizeof nm) np = 0;      // ld() does not check: a half-built S_Sound must not read outside guest memory
    for (; k < nl && k < sizeof nm - 1 && np; k++) nm[k] = (char)ld(c, np + k, 1);
    nm[k] = 0;
    int j = 0;
    while (j < nvt && strcmp(vt[j].name, nm)) j++;
    if (j == nvt) { if (nvt == VN) return -1; snprintf(vt[nvt++].name, sizeof vt[0].name, "%s", nm); }
    return j;
}
// The cue a Play starts: the S_Sound whose channel +0x143 holds this buffer (S_CHANNEL {owner, buffer, 3D buffer}, +4).
// The sndmfc tick gives the channel before startVoicePlayback calls Play, so the name is known here and a reused buffer never keeps
// the previous cue's name.
static int cue_of_buffer(Ctx *c, uint32_t gbuf) {
    uint32_t b = ld(c, 0x10315974, 4), e = ld(c, 0x10315978, 4);
    if (b < LOW || e < b || e - b > 4096 || e > c->memsz) return 0;
    for (uint32_t p = b; p < e; p += 4) {
        uint32_t s = ld(c, p, 4);
        if (s < LOW || s > c->memsz - 0x194) continue;
        uint32_t ch = ld(c, s + 0x143, 4);
        if (ch >= LOW && ch <= c->memsz - 8 && ld(c, ch + 4, 4) == gbuf) { int j = vt_index(c, s); return j > 0 ? j : 0; }
    }
    return 0;
}
static int stop_kind(Ctx *c) {                                // the return address of Stop: inside releaseVoice or S_Sound::stop?
    uint32_t sp = c->r[ESP], ret = ld(c, sp, 4);
    int release = ret == 0x1030bc0b;                          /* call [eax+0x48] at 0x1030bc08 in 0x1030bb60 */
    if (!release && ret != 0x1030b9e0)                        /* call [eax+0x48] at 0x1030b9dd in 0x1030b9a0 */
        return E_STOP_OTHER;
    for (int k = 1; k <= 48; k++) {                           // the nearest caller frame above wins
        uint32_t w = ld(c, sp + 4u * k, 4);
        for (unsigned j = 0; j < sizeof stop_sites / sizeof stop_sites[0]; j++)
            if (stop_sites[j].ret == w && stop_sites[j].release == release) return stop_sites[j].kind;
    }
    return release ? E_REL_OTHER : E_STOP_OTHER;
}
static int trace_cue(const Obj *o) {
    const char *n = vt[o->nm < VN ? o->nm : 0].name;
    return soundlog() && (strstr(n, "default_hit_01") || strstr(n, "Default_hit_01") ||
           strstr(n, "metal_hit_01") || strstr(n, "shell_ricochet_01"));
}
static void play_end(Obj *o, int kind);
static void play_begin(Obj *o) {                              // mx held
    if (o->in_play) play_end(o, E_REPLAYED);
    o->in_play = 1; o->pframes = o->pheld = 0; o->pin = o->pgain = o->p3max = o->pout = 0;
    o->wrote_lo = o->wrote_hi = 0;
    acct.started++;
    uint32_t live = 1;
    for (int k = 0; k < nobjs; k++) if (&objs[k] != o && objs[k].iface == I_BUF && !objs[k].primary && objs[k].playing) live++;
    if (live > acct.concurrent_peak) acct.concurrent_peak = live;
}
static void play_end(Obj *o, int kind) {                      // mx held
    if (!o->in_play) return;
    o->in_play = 0;
    static unsigned results;
    if (trace_cue(o) && results++ < 200) rn_log("dsound cue result: %s frames %u input %.6f output %.6f end %s", vt[o->nm].name,
                               o->pframes, o->pin, o->pout, end_name[kind]);
    acct.ends[kind]++;
    if (o->nm < VN) vt[o->nm].plays++;      // counted at the end: a Play whose sound the tick names later is still told apart
    if (o->pout >= 3e-5f) { acct.audible++; if (o->nm < VN) vt[o->nm].audible++; return; }
    int why = !o->pframes ? (o->pheld ? P_HELD : kind == E_DATA_END || kind == E_REPLAYED ? P_NOMIX : P_CANCELLED)
            : o->pgain <= 0 ? P_MUTE_VOL : o->p3max <= 0 ? P_MUTE_3D
            : o->pin <= 0 ? (o->written ? P_ZERO_DATA : P_NEVER_FILLED) : P_QUIET;
    acct.lost[why]++; acct.ends_silent[kind]++;
    static int shown;
    if (soundlog() && shown++ < 30)
        rn_log("dsound: silent Play, buffer %d (%u Hz %u-bit %u ch, %s): %s; ended by %s after %.2f s, %u frames mixed, %u held, "
               "wrote %u..%u of %u, pos %.0f",
               (int)(o - objs), o->rate, o->bits, o->channels, o->has3d ? "3D" : "2D", lose_name[why], end_name[kind],
               (double)(now_us() - o->play_us) / 1e6, o->pframes, o->pheld, o->wrote_lo, o->wrote_hi, o->size, o->pos);
}

// COM methods: calls, errors and no-ops, per interface method (one X-macro list feeds the wrappers and the API table below).
// k: 0 = a result other than DS_OK is an error, 1 = implemented as "accept and ignore" (the game calls it), 2 = returns a count.
#define METHODS(X) \
    X(IDirectSound, QueryInterface, 3, 0) X(IDirectSound, AddRef, 1, 2) X(IDirectSound, Release, 1, 2) \
    X(IDirectSound, CreateSoundBuffer, 4, 0) X(IDirectSound, GetCaps, 2, 0) X(IDirectSound, DuplicateSoundBuffer, 3, 0) \
    X(IDirectSound, SetCooperativeLevel, 3, 1) X(IDirectSound, Compact, 1, 1) X(IDirectSound, Initialize, 2, 1) \
    X(IDirectSound, GetSpeakerConfig, 2, 0) X(IDirectSound, SetSpeakerConfig, 2, 1) \
    X(IDirectSoundBuffer, QueryInterface, 3, 0) X(IDirectSoundBuffer, AddRef, 1, 2) X(IDirectSoundBuffer, Release, 1, 2) \
    X(IDirectSoundBuffer, GetCaps, 2, 0) X(IDirectSoundBuffer, GetCurrentPosition, 3, 0) X(IDirectSoundBuffer, GetFormat, 4, 0) \
    X(IDirectSoundBuffer, GetVolume, 2, 0) X(IDirectSoundBuffer, GetPan, 2, 0) X(IDirectSoundBuffer, GetFrequency, 2, 0) \
    X(IDirectSoundBuffer, GetStatus, 2, 0) X(IDirectSoundBuffer, Initialize, 3, 0) X(IDirectSoundBuffer, Lock, 8, 0) \
    X(IDirectSoundBuffer, Play, 4, 0) X(IDirectSoundBuffer, SetCurrentPosition, 2, 0) X(IDirectSoundBuffer, SetFormat, 2, 0) \
    X(IDirectSoundBuffer, SetVolume, 2, 0) X(IDirectSoundBuffer, SetPan, 2, 0) X(IDirectSoundBuffer, SetFrequency, 2, 0) \
    X(IDirectSoundBuffer, Stop, 1, 0) X(IDirectSoundBuffer, Unlock, 5, 0) X(IDirectSoundBuffer, Restore, 1, 1) \
    X(IDirectSound3DListener, QueryInterface, 3, 0) X(IDirectSound3DListener, AddRef, 1, 2) \
    X(IDirectSound3DListener, Release, 1, 2) X(IDirectSound3DListener, SetOrientation, 8, 0) \
    X(IDirectSound3DListener, SetPosition, 5, 0) X(IDirectSound3DListener, SetRolloffFactor, 3, 0) \
    X(IDirectSound3DListener, SetVelocity, 5, 1) X(IDirectSound3DListener, CommitDeferredSettings, 1, 1) \
    X(IDirectSound3DBuffer, QueryInterface, 3, 0) X(IDirectSound3DBuffer, AddRef, 1, 2) X(IDirectSound3DBuffer, Release, 1, 2) \
    X(IDirectSound3DBuffer, SetConeAngles, 4, 1) X(IDirectSound3DBuffer, SetConeOrientation, 5, 1) \
    X(IDirectSound3DBuffer, SetConeOutsideVolume, 3, 1) X(IDirectSound3DBuffer, SetMaxDistance, 3, 0) \
    X(IDirectSound3DBuffer, SetMinDistance, 3, 0) X(IDirectSound3DBuffer, SetPosition, 5, 0) X(IDirectSound3DBuffer, SetVelocity, 5, 1)
enum {
#define X(i, m, n, k) M_##i##_##m,
    METHODS(X)
#undef X
    M_COUNT
};
static struct { const char *name; uint32_t calls, errs, last; int kind; } ms[M_COUNT] = {
#define X(i, m, n, k) {#i "::" #m, 0, 0, 0, k},
    METHODS(X)
#undef X
};
static uint32_t note(Ctx *c, int m, uint32_t r) {
    __atomic_fetch_add(&ms[m].calls, 1, __ATOMIC_RELAXED);
    if (r && ms[m].kind != 2) {
        uint32_t n = __atomic_add_fetch(&ms[m].errs, 1, __ATOMIC_RELAXED);
        ms[m].last = r;
        if (soundlog() && n <= 3) rn_log("dsound: %s returned %08x (args %08x %08x %08x)", ms[m].name, r, ARG(1), ARG(2), ARG(3));
    }
    return r;
}

// Slots of released objects are reused (the game recreates buffers whenever a sound's format differs: 0x1030aad0),
// and so are their data blocks, by exact size (guest memory has no free here).
typedef struct { uint32_t data, size; } Block;
static Block pool[64];
static int npool;
static Obj *slot_new(Ctx *c, int iface, uint32_t *guest) {
    int k = -1;
    for (int j = 0; j < nobjs && k < 0; j++) if (objs[j].iface == -1) k = j;
    if (k < 0) {
        if (nobjs == MAXO) { rn_halt(c, "dsound: too many live objects"); return NULL; }
        k = nobjs++;
        objs[k].guest = guest_alloc(16);
    }
    uint32_t g = objs[k].guest;
    memset(&objs[k], 0, sizeof objs[k]);
    objs[k].guest = g; objs[k].iface = iface; objs[k].refs = 1; objs[k].parent = -1;
    st(c, g, vtable(iface), 4);
    st(c, g + 4, (uint32_t)k + 1, 4);
    *guest = g;
    return &objs[k];
}
static uint32_t new_obj(Ctx *c, int iface) {
    uint32_t g = 0;
    pthread_mutex_lock(&mx);
    slot_new(c, iface, &g);
    pthread_mutex_unlock(&mx);
    return g;
}
static Obj *obj_of(Ctx *c, uint32_t p, int iface) {
    uint32_t k = p ? ld(c, p + 4, 4) - 1 : ~0u;
    if (k < (uint32_t)nobjs && objs[k].guest == p && objs[k].iface == iface) return &objs[k];
    rn_halt(c, "dsound: %08x is not a live %s", p, iface_name[iface]);
    return NULL;
}
#define SELF(iface) Obj *o = obj_of(c, ARG(0), iface); if (!o) return DSERR_INVALIDPARAM

static Obj *any_obj(Ctx *c);
// QueryInterface: an object answers for IUnknown and its own interface. IKsPropertySet (EAX and other
// driver property sets) exists only on hardware buffers, and every buffer here is a software one.
static const struct { uint32_t d1, d2, d3; int iface; } iids[] = {
    {0x00000000u, 0x0000, 0x0000, -1},             // IUnknown {00000000-0000-0000-C000-000000000046}
    {0x279afa83u, 0x4981, 0x11ce, I_DS},           // IDirectSound
    {0x279afa85u, 0x4981, 0x11ce, I_BUF},          // IDirectSoundBuffer
    {0x279afa84u, 0x4981, 0x11ce, I_L3D},          // IDirectSound3DListener: only the primary buffer has it
    {0x279afa86u, 0x4981, 0x11ce, I_B3D},          // IDirectSound3DBuffer: only a CTRL3D secondary buffer has it
    {0x31efac30u, 0x515c, 0x11d0, -2},             // IKsPropertySet
};
static uint32_t QueryInterface_(Ctx *c) {
    uint32_t g = ARG(1), out_p = ARG(2), d1 = ld(c, g, 4), d2 = ld(c, g + 4, 2), d3 = ld(c, g + 6, 2);
    Obj *o = any_obj(c);
    if (!o || !out_p) return DSERR_INVALIDPARAM;
    st(c, out_p, 0, 4);
    for (unsigned k = 0; k < sizeof iids / sizeof iids[0]; k++) {
        if (iids[k].d1 != d1 || iids[k].d2 != d2 || iids[k].d3 != d3) continue;
        int want = iids[k].iface;
        int ok = want == -1 || want == o->iface ||
                 (want == I_L3D && o->iface == I_BUF && o->primary && (o->flags & 0x10)) ||
                 (want == I_B3D && o->iface == I_BUF && !o->primary && (o->flags & 0x10));
        if (!ok) {
            TR("QueryInterface({%08x-...}) on %s -> E_NOINTERFACE", d1, iface_name[o->iface]);
            return DSERR_NOINTERFACE;
        }
        if (want == I_L3D || want == I_B3D) {              // a new object that holds a reference on its buffer
            int pk = (int)(o - objs);
            uint32_t ng;
            pthread_mutex_lock(&mx);
            Obj *n = slot_new(c, want, &ng);
            // The 3D state lives on the buffer the mixer plays (objs[pk]), with the DirectSound defaults min 1, max 1e9.
            if (n) { n->parent = pk; objs[pk].refs++; if (want == I_B3D && !objs[pk].has3d) { objs[pk].has3d = 1; objs[pk].min_d = 1.0f; objs[pk].max_d = 1e9f; } }
            pthread_mutex_unlock(&mx);
            if (!n) return DSERR_INVALIDPARAM;
            st(c, out_p, ng, 4);
            return DS_OK;
        }
        o->refs++;
        st(c, out_p, ARG(0), 4);
        return DS_OK;
    }
    rn_halt(c, "dsound: QueryInterface for {%08x-%04x-%04x-...} on %s", d1, d2, d3, iface_name[o->iface]);
    return DSERR_NOINTERFACE;
}
static Obj *any_obj(Ctx *c) {
    uint32_t k = ld(c, ARG(0) + 4, 4) - 1;
    if (k < (uint32_t)nobjs && objs[k].guest == ARG(0) && objs[k].iface >= 0) return &objs[k];
    rn_halt(c, "dsound: %08x is not a live object", ARG(0));
    return NULL;
}
static uint32_t AddRef_(Ctx *c) { Obj *o = any_obj(c); return o ? ++o->refs : 0; }
// The last Release of a buffer stops it and frees its slot and data block (see Block pool); a 3D interface
// releases its buffer with itself.
static void release_slot(Obj *o) {                       // mx held
    if (--o->refs) return;
    int parent = o->parent;
    if (o->playing) play_end(o, E_RELEASED);
    o->playing = 0;
    if (o->iface == I_BUF && !o->primary && o->data && !(o->flags & 0x80000000u) && npool < 64) pool[npool++] = (Block){o->data, o->size};
    o->iface = -1;
    if (parent >= 0 && objs[parent].iface >= 0) release_slot(&objs[parent]);
}
static uint32_t Release_(Ctx *c) {
    Obj *o = any_obj(c);
    if (!o || !o->refs) return 0;
    pthread_mutex_lock(&mx);
    uint32_t r = o->refs - 1;
    release_slot(o);
    pthread_mutex_unlock(&mx);
    return r;
}

// ---- the mixer -------------------------------------------------------------------------------
static float out[2 * 4096];
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// Header: frames written (mixer), frames taken (web/audio.js), ring address, ring size in stereo float frames.
#define RING 16384
static float ring[2 * RING];
static uint32_t ring_hdr[12];           // [4] frames of silence the page played (underruns), [5] resyncs, [6] peak, [8] frames a resync skipped: web/audio.js;
                                        // [7] saturated samples, [9] frames overwritten before the page took them: emit()
EMSCRIPTEN_KEEPALIVE uint32_t rn_audio_ring(void) {
    ring_hdr[2] = (uint32_t)(uintptr_t)ring;
    ring_hdr[3] = RING;
    return (uint32_t)(uintptr_t)ring_hdr;
}
// Saturates like the 16-bit software mixer of real DirectSound (and the native WAV path below): the sum of several full-scale
// voices passes 1.0 (render telemetry saw 1.085) and the browser DAC would clip it the same way, but then the peak and the
// clip count would be read past full scale. ring_hdr[7] counts the saturated samples.
static void emit(const float *f, uint32_t n) {
    uint32_t w = ring_hdr[0], clipped = 0;
    // The oldest n frames of the ring are about to be overwritten: the ones the page has not taken yet are lost (the page is not
    // running yet, its AudioContext is suspended, or it is too slow).
    int64_t old = (int64_t)w - RING, taken = __atomic_load_n(&ring_hdr[1], __ATOMIC_RELAXED), first = old > taken ? old : taken;
    if (first < 0) first = 0;
    if (old + (int64_t)n > first) __atomic_fetch_add(&ring_hdr[9], (uint32_t)(old + n - first), __ATOMIC_RELAXED);
    for (uint32_t k = 0; k < n; k++, w++)
        for (int c = 0; c < 2; c++) {
            float v = f[2 * k + c];
            if (v < -1 || v > 1) { clipped++; v = v < 0 ? -1 : 1; }
            ring[2 * (w % RING) + c] = v;
        }
    if (clipped) __atomic_fetch_add(&ring_hdr[7], clipped, __ATOMIC_RELAXED);
    __atomic_store_n(&ring_hdr[0], w, __ATOMIC_RELEASE);
}
#else
static FILE *wav;
static uint32_t wav_frames;
static void emit(const float *f, uint32_t n) {
    if (!wav) return;
    int16_t s[2 * 4096];
    for (uint32_t k = 0; k < 2 * n; k++) {
        float v = f[k] < -1 ? -1 : f[k] > 1 ? 1 : f[k];
        s[k] = (int16_t)lrintf(v * 32767);
    }
    fwrite(s, 4, n, wav);
    wav_frames += n;
    uint32_t data = wav_frames * 4, riff = 36 + data;
    fseek(wav, 4, SEEK_SET); fwrite(&riff, 4, 1, wav);
    fseek(wav, 40, SEEK_SET); fwrite(&data, 4, 1, wav);
    fseek(wav, 0, SEEK_END);
    fflush(wav);
}
static void open_wav(void) {
    const char *p = getenv("RN_WAV");
    if (!p || !(wav = fopen(p, "wb"))) return;
    // RIFF/WAVE, PCM, 2 channels, 44100 Hz, 16 bits; sizes are rewritten after each write.
    uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
                     0x44, 0xac, 0, 0, 0x10, 0xb1, 2, 0, 4, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0};
    fwrite(h, 1, 44, wav);
}
#endif

static float gain(int32_t v) { return v <= -10000 ? 0.0f : powf(10.0f, (float)v / 2000.0f); }
static float sample(const Obj *o, uint32_t frame, int ch) {
    const uint8_t *p = g_ctx->mem + o->data + (size_t)frame * o->align + (o->channels == 2 ? ch : 0) * (o->bits / 8);
    if (o->bits == 8) return ((int)p[0] - 128) / 128.0f;
    int16_t s;
    memcpy(&s, p, 2);
    return s / 32768.0f;
}
// Frame a+d of a buffer: wraps when it loops, else holds the first or last frame.
static uint32_t at(const Obj *o, uint32_t frames, uint32_t a, int d) {
    int64_t i = (int64_t)a + d;
    if (o->looping) return (uint32_t)((i % frames + frames) % frames);
    return i < 0 ? 0 : i >= frames ? frames - 1 : (uint32_t)i;
}
// 4-point, 3rd-order Hermite between frames a and a+1 (t in [0,1)); t = 0 gives frame a exactly.
static float hermite(const Obj *o, uint32_t frames, uint32_t a, float t, int ch) {
    float ym = sample(o, at(o, frames, a, -1), ch), y0 = sample(o, a, ch);
    float y1 = sample(o, at(o, frames, a, 1), ch), y2 = sample(o, at(o, frames, a, 2), ch);
    float c1 = 0.5f * (y1 - ym), c2 = ym - 2.5f * y0 + 2 * y1 - 0.5f * y2, c3 = 0.5f * (y2 - ym) + 1.5f * (y0 - y1);
    return ((c3 * t + c2) * t + c1) * t + y0;
}
// Distance gain and pan (-1 left .. 1 right) of a 3D buffer: gain = min / (min + rolloff * (d - min)) past the
// minimum distance (the DirectSound default model), silence at the maximum distance (DSBCAPS_MUTE3DATMAXDISTANCE 0x20000).
static void gain3d(const Obj *o, float *a, float *pan) {
    float rx = o->p3[0] - lis_pos[0], ry = o->p3[1] - lis_pos[1], rz = o->p3[2] - lis_pos[2];
    float *f = lis_front, *u = lis_top;
    float right[3] = {u[1] * f[2] - u[2] * f[1], u[2] * f[0] - u[0] * f[2], u[0] * f[1] - u[1] * f[0]};  // up x front
    float d = sqrtf(rx * rx + ry * ry + rz * rz), mn = o->min_d > 0 ? o->min_d : 1.0f;
    *a = d <= mn ? 1.0f : mn / (mn + lis_rolloff * (d - mn));
    if ((o->flags & 0x20000) && d >= o->max_d) *a = 0;
    *pan = d > 1e-6f ? (rx * right[0] + ry * right[1] + rz * right[2]) / d : 0;
    if (*pan > 1) *pan = 1;
    if (*pan < -1) *pan = -1;
}
// Mixes n frames of every playing buffer into out[] and moves the buffers on. Called with mx held.
static void mix(uint32_t n) {
    memset(out, 0, 8u * n);
    float m = gain(master);
    for (int k = 0; k < nobjs; k++) {
        Obj *o = &objs[k];
        if (o->iface != I_BUF || o->primary || !o->playing || !o->size) continue;
        uint32_t frames = o->size / o->align;
        double step = (double)(o->freq ? o->freq : o->rate) / RATE;
        float g = gain(o->volume) * m;
        float gl = g * (o->pan > 0 ? gain(-o->pan) : 1), gr = g * (o->pan < 0 ? gain(o->pan) : 1);
        float a3 = 1;
        o->pgain = fmaxf(o->pgain, fmaxf(gl, gr));
        if (o->has3d) {                                    // DS3D without HRTF: the game puts the source in listener space
            float p3;                                      // (0x1030ae20) and the listener at the origin, so this is cheap
            gain3d(o, &a3, &p3);
            gl *= a3 * (p3 > 0 ? 1 - p3 : 1); gr *= a3 * (p3 < 0 ? 1 + p3 : 1);
        }
        o->p3max = fmaxf(o->p3max, a3);
        float pin = 0, pout = 0;
        uint32_t done = 0;
        int ended = 0;
        for (uint32_t j = 0; j < n; j++) {
            if (o->looping && o->unlocks >= 2 && o->avail < 2) {   // the game did not refill: wait for it, no stale lap, no skip
                if (!o->held) {
                    o->held = 1;
                    rn_telem.starves++; rn_telem.starve_buf = (uint32_t)k;
                    if (rn_telem.starves <= 20)
                        rn_log("dsound stream %d: held, the game did not refill (%u frames, %u Hz, %u ch, %u Unlocks)", k, frames,
                               o->freq ? o->freq : o->rate, o->channels, o->unlocks);
                    if (rn_telem.starves <= 5) dispatch_threads_report();
                }
                o->pheld += n - j;
                break;
            }
            uint32_t a = (uint32_t)o->pos;
            float t = (float)(o->pos - a);
            float h0 = hermite(o, frames, a, t, 0), h1 = (o->channels == 1) ? h0 : hermite(o, frames, a, t, 1);
            float dither = 1.0f;                        // 1 except in the 128 frames on each side of a hold
            if (o->fade_in > 0) dither = 1.0f - (float)--o->fade_in / 128.0f;
            if (o->looping && o->unlocks >= 2) {         // and let the last frames before the hold die out
                float tail = (float)o->avail / (128.0f * (float)step);
                if (tail < 1.0f && tail < dither) dither = tail < 0 ? 0 : tail;
            }
            float l = gl * dither * h0, r = gr * dither * h1;
            out[2 * j] += l;
            out[2 * j + 1] += r;
            pin = fmaxf(pin, fmaxf(fabsf(h0), fabsf(h1))); pout = fmaxf(pout, fmaxf(fabsf(l), fabsf(r)));
            done++;
            o->pos += step;
            o->avail -= step;
            o->since_unlock += step;
            if (o->pos >= frames) {
                if (o->looping) o->pos -= frames * floor(o->pos / frames);
                else { o->playing = 0; o->pos = 0; ended = 1; break; }   // Wine resets the cursor at the end: check
            }
        }
        o->mixed_pos += done * step;
        o->pframes += done; o->pin = fmaxf(o->pin, pin); o->pout = fmaxf(o->pout, pout);
        if (ended) play_end(o, E_DATA_END);
        if (o->looping && o->unlocks >= 2) {                // a stream: the game refills it as it plays
            uint32_t pm = (uint32_t)(1000 * o->since_unlock / frames);
            if (pm > rn_telem.stream_pm) rn_telem.stream_pm = pm;
            if (o->since_unlock >= frames && o->since_unlock - n * step < frames) {
                rn_telem.starves++; rn_telem.starve_buf = (uint32_t)k;
                if (rn_telem.starves <= 20)
                    rn_log("dsound stream %d: a whole lap with no Unlock (%u frames, %u Hz, %u ch, %u Unlocks)", k, frames,
                           o->freq ? o->freq : o->rate, o->channels, o->unlocks);
                if (rn_telem.starves <= 5) dispatch_threads_report();
            }
        }
    }
}
static uint64_t mixed;                  // frames mixed since the device was created
static uint64_t clock_frames(void) { return now_us() * RATE / 1000000; }
// RN_SOUNDLOG: the accounting above, every 3 s, only when something happened since the last report.
static void virtual_report(void);
static void report(void) {
    static uint64_t last_sum;
    pthread_mutex_lock(&mx);
    typeof(acct) a = acct;
    int live = 0;
    for (int k = 0; k < nobjs; k++) if (objs[k].iface == I_BUF && !objs[k].primary && objs[k].playing) live++;
    pthread_mutex_unlock(&mx);
    uint64_t sum = a.plays, calls = 0;
    for (int m = 0; m < M_COUNT; m++) calls += ms[m].calls;
    if (sum + calls == last_sum) return;
    last_sum = sum + calls;
    uint64_t silent = 0;
    for (int k = 0; k < P_N; k++) if (k != P_CANCELLED) silent += a.lost[k];   // a Play the game withdrew is not a lost sound
    rn_log("dsound report %llu s: Play calls %llu (started %llu, audible %llu, silent %llu; game: volume off %llu; host: not mixed %llu, "
           "held %llu, DS3D max distance %llu, mixed zero %llu, under 1 LSB %llu, never filled %llu); playing now %d, peak %u",
           (unsigned long long)(now_us() / 1000000), (unsigned long long)a.plays, (unsigned long long)a.started,
           (unsigned long long)a.audible, (unsigned long long)silent, (unsigned long long)a.lost[P_MUTE_VOL],
           (unsigned long long)a.lost[P_NOMIX], (unsigned long long)a.lost[P_HELD], (unsigned long long)a.lost[P_MUTE_3D],
           (unsigned long long)a.lost[P_ZERO_DATA], (unsigned long long)a.lost[P_QUIET], (unsigned long long)a.lost[P_NEVER_FILLED],
           live, a.concurrent_peak);
    rn_log("dsound cancelled: %llu (the game released the voice before the first mixer pass)",
           (unsigned long long)a.lost[P_CANCELLED]);
    char b[1024]; int n = 0;
    for (int k = 0; k < E_N && n < (int)sizeof b - 96; k++)
        if (a.ends[k]) n += snprintf(b + n, sizeof b - n, "%s%s %llu (silent %llu)", n ? "; " : "", end_name[k],
                                     (unsigned long long)a.ends[k], (unsigned long long)a.ends_silent[k]);
    rn_log("dsound ends: %s", n ? b : "none yet");
    virtual_report();
    n = 0;
    for (int m = 0; m < M_COUNT && n < (int)sizeof b - 96; m++)
        if (ms[m].errs) n += snprintf(b + n, sizeof b - n, "%s%s %u of %u (last %08x)", n ? "; " : "", ms[m].name, ms[m].errs, ms[m].calls, ms[m].last);
    rn_log("dsound COM errors: %s", n ? b : "none");
    n = 0;
    for (int m = 0; m < M_COUNT && n < (int)sizeof b - 96; m++)
        if (ms[m].kind == 1 && ms[m].calls) n += snprintf(b + n, sizeof b - n, "%s%s %u", n ? "; " : "", ms[m].name, ms[m].calls);
    rn_log("dsound COM calls accepted and ignored: %s", n ? b : "none");
#ifdef __EMSCRIPTEN__
    rn_log("dsound ring: mixed %u taken %u, silence %u frames, resyncs %u (skipped %u frames), overwritten before taken %u, clipped %u; "
           "streams held/stale %u", __atomic_load_n(&ring_hdr[0], __ATOMIC_RELAXED), __atomic_load_n(&ring_hdr[1], __ATOMIC_RELAXED),
           ring_hdr[4], ring_hdr[5], ring_hdr[8], __atomic_load_n(&ring_hdr[9], __ATOMIC_RELAXED), ring_hdr[7], rn_telem.starves);
#else
    rn_log("dsound streams held/stale %u", rn_telem.starves);
#endif
}
// The ghost's sndSnap calls this before reading the counters: the report prints on a change and every 3 s, so the last
// plays of a map could end after its last line (I_result §6 patch 3).
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE void rn_sound_report(void) { report(); }
#endif
static void *mixer(void *arg) {
    (void)arg;
    mixed = clock_frames();
    for (;;) {
        uint64_t want = clock_frames() + LEAD;
        while (mixed < want) {
            // Release mx between short quanta: SetFrequency's bullet-time ramp must not wait for a 4096-frame catch-up.
            uint32_t n = want - mixed > 256 ? 256 : (uint32_t)(want - mixed);
            pthread_mutex_lock(&mx);
            mix(n);
            mixed += n;                // publish positions and their clock together to GetCurrentPosition
            pthread_mutex_unlock(&mx);
            emit(out, n);
        }
        static uint64_t next_report;
        if (soundlog() && now_us() >= next_report) { next_report = now_us() + 3000000; report(); }
        struct timespec ts = {0, 5000000};
        nanosleep(&ts, NULL);
    }
    return NULL;
}

// ---- IDirectSound ------------------------------------------------------------------------------
static void dsound_start(void) {
    static int started;
    if (started) return;
    started = 1;
#ifndef __EMSCRIPTEN__
    open_wav();
#endif
    pthread_t t;
    if (pthread_create(&t, NULL, mixer, NULL)) rn_log("dsound: pthread_create for the mixer failed");
}
static uint32_t DirectSoundCreate_(Ctx *c) {
    uint32_t guid = ARG(0), out_p = ARG(1);
    if (!out_p || ARG(2)) return DSERR_INVALIDPARAM;
    if (guid) { rn_halt(c, "DirectSoundCreate for device {%08x-...}", ld(c, guid, 4)); return DSERR_INVALIDPARAM; }
    dsound_start();
    st(c, out_p, new_obj(c, I_DS), 4);
    TR("DirectSoundCreate -> %08x", ld(c, out_p, 4));
    return DS_OK;
}
// DirectSoundEnumerateA(callback, context): this PC lists one device, the primary sound driver (GUID NULL), which
// DirectSoundCreate takes. BOOL CALLBACK (LPGUID, LPCSTR description, LPCSTR module, LPVOID context).
// sndmfc 0x10301590 only picks a device whose GUID pointer is non-NULL (the NULL one is "Primary Sound Driver"),
// else it would take its own Null Audio Device and run silent (rea/map/sound.md 1.1), so the PC lists both.
static uint32_t DirectSoundEnumerateA_(Ctx *c) {
    static uint32_t desc, desc2, mod, guid;
    if (!desc) {
        desc = guest_alloc(32); desc2 = guest_alloc(32); mod = guest_alloc(16); guid = guest_alloc(16);
        memcpy(G(desc), "Primary Sound Driver", 21);
        memcpy(G(desc2), "Built-in Output", 16);
        memcpy(G(mod), "dsound.dll", 11);
        st(c, guid, 0x6cb2a8d1u, 4); st(c, guid + 4, 0x4c1d, 2); st(c, guid + 6, 0x4f7e, 2);
        st(c, guid + 8, 0x4d41584du, 4); st(c, guid + 12, 0x454e5950u, 4);
    }
    uint32_t a[4] = {0, desc, mod, ARG(1)};
    if (!call_guest(c, ARG(0), 4, a)) return DS_OK;
    a[0] = guid; a[1] = desc2;
    call_guest(c, ARG(0), 4, a);
    return DS_OK;
}
// IDirectSound::Initialize(pcGuidDevice): the object CoCreateInstance made binds to a device; this PC has one.
static uint32_t IDirectSound_Initialize(Ctx *c) { SELF(I_DS); TR("Initialize(%08x)", ARG(1)); return DS_OK; }
// CoCreateInstance(CLSID_DirectSound, IID_IDirectSound): sndmfc 0x10301590 / 0x10301950 create the device this way
// (not DirectSoundCreate) and call Initialize + SetCooperativeLevel(DSSCL_EXCLUSIVE... = 3) on it.
int dsound_cocreate(Ctx *c, uint32_t clsid, uint32_t iid, uint32_t out_p) {
    if (ld(c, clsid, 4) != 0x47d4d946u || ld(c, clsid + 4, 2) != 0x62e8 || ld(c, clsid + 6, 2) != 0x11cf) return 0;
    if (ld(c, iid, 4) != 0x279afa83u && ld(c, iid, 4) != 0) { st(c, out_p, 0, 4); return 0; }
    dsound_start();
    st(c, out_p, new_obj(c, I_DS), 4);
    TR("CoCreateInstance(CLSID_DirectSound) -> %08x", ld(c, out_p, 4));
    return 1;
}
static uint32_t IDirectSound_SetCooperativeLevel(Ctx *c) { SELF(I_DS); TR("SetCooperativeLevel(%u)", ARG(2)); return DS_OK; }
static uint32_t IDirectSound_Compact(Ctx *c) { SELF(I_DS); return DS_OK; }
// DSSPEAKER_COMBINED(DSSPEAKER_STEREO, DSSPEAKER_GEOMETRY_WIDE): the Windows XP default for stereo.
static uint32_t IDirectSound_GetSpeakerConfig(Ctx *c) { SELF(I_DS); if (!ARG(1)) return DSERR_INVALIDPARAM; st(c, ARG(1), 4u | 20u << 16, 4); return DS_OK; }
static uint32_t IDirectSound_SetSpeakerConfig(Ctx *c) { SELF(I_DS); return DS_OK; }
// DSCAPS (96 bytes) of a software device: every format, any rate in 100..200000 Hz, no hardware buffers.
static uint32_t IDirectSound_GetCaps(Ctx *c) {
    SELF(I_DS);
    uint32_t p = ARG(1);
    if (!p || ld(c, p, 4) != 96) return DSERR_INVALIDPARAM;
    memset(G(p + 4), 0, 92);
    st(c, p + 4, 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x100 | 0x200 | 0x400 | 0x800, 4);
    st(c, p + 8, 100, 4);               // dwMinSecondarySampleRate
    st(c, p + 12, 200000, 4);
    st(c, p + 16, 1, 4);                // dwPrimaryBuffers
    return DS_OK;
}

// WAVEFORMATEX: wFormatTag, nChannels, nSamplesPerSec, nAvgBytesPerSec, nBlockAlign, wBitsPerSample, cbSize.
static int set_format(Ctx *c, Obj *o, uint32_t f) {
    uint32_t tag = ld(c, f, 2), ch = ld(c, f + 2, 2), rate = ld(c, f + 4, 4), align = ld(c, f + 12, 2), bits = ld(c, f + 14, 2);
    if (tag != 1 || (ch != 1 && ch != 2) || (bits != 8 && bits != 16) || align != ch * bits / 8 || rate < 100 || rate > 200000)
        return 0;
    o->channels = (uint16_t)ch; o->bits = (uint16_t)bits; o->align = (uint16_t)align; o->rate = rate;
    return 1;
}
// DSBUFFERDESC: dwSize (20, or 36 with guid3DAlgorithm), dwFlags, dwBufferBytes, dwReserved, lpwfxFormat.
static uint32_t IDirectSound_CreateSoundBuffer(Ctx *c) {
    SELF(I_DS);
    uint32_t d = ARG(1), out_p = ARG(2), sz = d ? ld(c, d, 4) : 0;
    if (!d || !out_p || ARG(3) || (sz != 20 && sz != 36)) return DSERR_INVALIDPARAM;
    uint32_t flags = ld(c, d + 4, 4), bytes = ld(c, d + 8, 4), fmt = ld(c, d + 16, 4);
    uint32_t g = new_obj(c, I_BUF);
    if (!g) return DSERR_INVALIDPARAM;
    Obj *b = &objs[ld(c, g + 4, 4) - 1];
    b->flags = flags;
    if (flags & 1) {                    // DSBCAPS_PRIMARYBUFFER: the output, 22 kHz 8-bit stereo until SetFormat
        if (bytes || fmt) { b->iface = -1; return DSERR_INVALIDPARAM; }
        b->primary = 1; b->channels = 2; b->bits = 8; b->align = 2; b->rate = 22050;
        b->size = 4 * LEAD;
    } else {
        if (!fmt || bytes < 4 || bytes > 0x0fffffff || !set_format(c, b, fmt)) {
            TR("CreateSoundBuffer: format refused");
            b->iface = -1;
            return fmt ? DSERR_BADFORMAT : DSERR_INVALIDPARAM;
        }
        b->size = bytes - bytes % b->align;
        for (int k = 0; k < npool && !b->data; k++)
            if (pool[k].size == b->size) { b->data = pool[k].data; pool[k] = pool[--npool]; }
        if (!b->data) b->data = guest_alloc(b->size);
        if (!b->data) { b->iface = -1; rn_halt(c, "dsound: no guest memory for a %u-byte buffer", bytes); return DSERR_INVALIDPARAM; }
        memset(G(b->data), b->bits == 8 ? 0x80 : 0, b->size);
        b->written = 0;                 // the host's fill: a Play that mixes only this is P_NEVER_FILLED, not the game's silence
    }
    TR("CreateSoundBuffer(flags %x, %u bytes, %u Hz %u-bit %u ch) -> %08x", flags, b->size, b->rate, b->bits, b->channels, g);
    static int made;                    // the first few, always: whether the game makes sound at all is the first question
    if (soundlog() && made++ < 8) rn_log("dsound: CreateSoundBuffer #%d flags %x %u bytes %u Hz %u-bit %u ch", made, flags, b->size, b->rate, b->bits, b->channels);
    st(c, out_p, g, 4);
    return DS_OK;
}
static uint32_t IDirectSound_DuplicateSoundBuffer(Ctx *c) {
    SELF(I_DS);
    Obj *src = obj_of(c, ARG(1), I_BUF);
    uint32_t out_p = ARG(2);
    if (!src || !out_p || src->primary) return DSERR_INVALIDCALL;
    uint32_t g = new_obj(c, I_BUF);
    if (!g) return DSERR_INVALIDPARAM;
    Obj *b = &objs[ld(c, g + 4, 4) - 1];
    pthread_mutex_lock(&mx);
    *b = *src;                          // the data block is shared, and so it is never given back to the pool
    b->guest = g; b->refs = 1; b->playing = 0; b->looping = 0; b->pos = 0; b->avail = 0; b->held = 0; b->has3d = 0; b->parent = -1; b->in_play = 0; b->flags |= 0x80000000u; src->flags |= 0x80000000u;
    pthread_mutex_unlock(&mx);
    st(c, out_p, g, 4);
    return DS_OK;
}

// ---- IDirectSoundBuffer -------------------------------------------------------------------------
static uint32_t IDirectSoundBuffer_GetCaps(Ctx *c) {
    SELF(I_BUF);
    uint32_t p = ARG(1);
    if (!p || ld(c, p, 4) != 20) return DSERR_INVALIDPARAM;
    st(c, p + 4, o->flags | (o->primary ? 0 : 0x8u), 4);      // DSBCAPS_LOCSOFTWARE
    st(c, p + 8, o->size, 4); st(c, p + 12, 0, 4); st(c, p + 16, 0, 4);
    return DS_OK;
}
// Convert an unwrapped source position only at the API boundary.
/* 0x10302e78: the tick refills behind the play cursor. Negative rounding must not look like a whole lap. */
static uint32_t cursor_bytes(const Obj *o, double pos) {
    return (uint32_t)fmod(fmax(pos, 0), o->size / o->align) * o->align;
}
static uint32_t IDirectSoundBuffer_GetCurrentPosition(Ctx *c) {
    SELF(I_BUF);
    uint32_t pp = ARG(1), wp = ARG(2);
    if (o->primary) {                   // the primary buffer cycles at the output rate
        uint32_t at = (uint32_t)(clock_frames() % LEAD) * 4u;
        if (pp) st(c, pp, at, 4);
        if (wp) st(c, wp, (at + 4u * 441) % o->size, 4);
        return DS_OK;
    }
    pthread_mutex_lock(&mx);
    double step = (double)(o->freq ? o->freq : o->rate) / RATE;
    uint64_t now = clock_frames(), ahead = mixed > now ? mixed - now : 0;
    // SetFrequency changes step while older samples are still queued. Keep the exposed cursor monotonic.
    /* 0x10302e78: a backward move is interpreted as a lap and overwrites unread short cues. */
    double back = fmin((double)ahead, (double)o->pframes) * step;
    if (o->playing) o->play_pos = fmax(o->play_pos, o->mixed_pos - back);
    uint32_t play = cursor_bytes(o, o->playing ? o->play_pos : o->pos), write = cursor_bytes(o, o->pos);
    static unsigned cursors;
    if (trace_cue(o) && now_us() - o->play_us < 150000 && cursors++ < 240)
        rn_log("dsound cue cursor: %s pos %.12f back %.12f play %u mixed %u ahead %llu", vt[o->nm].name,
               o->pos, back, play, o->pframes, (unsigned long long)ahead);
    pthread_mutex_unlock(&mx);
    if (pp) st(c, pp, play, 4);
    if (wp) st(c, wp, write, 4);
    return DS_OK;
}
static uint32_t IDirectSoundBuffer_GetFormat(Ctx *c) {
    SELF(I_BUF);
    uint32_t f = ARG(1), n = ARG(2), got = ARG(3);
    if (got) st(c, got, 18, 4);
    if (!f) return got ? DS_OK : DSERR_INVALIDPARAM;
    if (n < 16) return DSERR_INVALIDPARAM;
    st(c, f, 1, 2); st(c, f + 2, o->channels, 2); st(c, f + 4, o->rate, 4); st(c, f + 8, o->rate * o->align, 4);
    st(c, f + 12, o->align, 2); st(c, f + 14, o->bits, 2);
    if (n >= 18) st(c, f + 16, 0, 2);
    return DS_OK;
}
static uint32_t IDirectSoundBuffer_SetFormat(Ctx *c) {
    SELF(I_BUF);
    if (!o->primary) return DSERR_INVALIDCALL;
    if (!ARG(1) || !set_format(c, o, ARG(1))) return DSERR_BADFORMAT;
    TR("primary SetFormat: %u Hz %u-bit %u ch (the mixer stays at %u Hz stereo)", o->rate, o->bits, o->channels, RATE);
    return DS_OK;
}
static uint32_t IDirectSoundBuffer_GetVolume(Ctx *c) { SELF(I_BUF); if (!ARG(1)) return DSERR_INVALIDPARAM; st(c, ARG(1), (uint32_t)(o->primary ? master : o->volume), 4); return DS_OK; }
static uint32_t IDirectSoundBuffer_GetPan(Ctx *c) { SELF(I_BUF); if (!ARG(1)) return DSERR_INVALIDPARAM; st(c, ARG(1), (uint32_t)o->pan, 4); return DS_OK; }
static uint32_t IDirectSoundBuffer_GetFrequency(Ctx *c) { SELF(I_BUF); if (!ARG(1)) return DSERR_INVALIDPARAM; st(c, ARG(1), o->freq ? o->freq : o->rate, 4); return DS_OK; }
// The CTRL flags say which controls a buffer has: DSBCAPS_CTRLFREQUENCY 0x20, CTRLPAN 0x40, CTRLVOLUME 0x80.
static uint32_t IDirectSoundBuffer_SetVolume(Ctx *c) {
    SELF(I_BUF);
    int32_t v = (int32_t)ARG(1);
    if (!(o->flags & 0x80)) return 0x8878001eu;                  // DSERR_CONTROLUNAVAIL
    if (v > 0 || v < -10000) return DSERR_INVALIDPARAM;
    pthread_mutex_lock(&mx);
    if (o->primary) master = v; else o->volume = v;
    pthread_mutex_unlock(&mx);
    return DS_OK;
}
static uint32_t IDirectSoundBuffer_SetPan(Ctx *c) {
    SELF(I_BUF);
    int32_t v = (int32_t)ARG(1);
    if (!(o->flags & 0x40)) return 0x8878001eu;
    if (v > 10000 || v < -10000) return DSERR_INVALIDPARAM;
    pthread_mutex_lock(&mx); o->pan = v; pthread_mutex_unlock(&mx);
    return DS_OK;
}
static uint32_t IDirectSoundBuffer_SetFrequency(Ctx *c) {
    SELF(I_BUF);
    uint32_t f = ARG(1);
    if (!(o->flags & 0x20) || o->primary) return 0x8878001eu;
    if (f && (f < 100 || f > 200000)) return DSERR_INVALIDPARAM;
    uint64_t start = soundlog() ? now_us() : 0;
    pthread_mutex_lock(&mx); o->freq = f; pthread_mutex_unlock(&mx);
    if (start) {
        uint64_t us = now_us() - start;
        static uint64_t worst;
        if (us > worst && us >= 1000) { worst = us; rn_log("dsound: SetFrequency mutex high-water %llu us", (unsigned long long)us); }
    }
    static int nlog; static double last;   // bullet time detunes the 3D world through SetFrequency (rea/map/bullettime.md 3): the first 200 changes of more than 2%
    double ratio = f ? (double)f / o->rate : 0;       // logged when it differs from the last logged ratio too, else a long plateau fills the cap
    if (soundlog() && f && (f > o->rate * 1.02 || f < o->rate * 0.98) && nlog < 200 && (ratio > last * 1.02 || ratio < last * 0.98)) { nlog++; last = ratio; rn_log("dsound: SetFrequency %u Hz on a %u Hz buffer (%.2fx)", f, o->rate, (double)f / o->rate); }
    return DS_OK;
}
// DSBSTATUS_PLAYING 1, DSBSTATUS_LOOPING 4, DSBSTATUS_LOCSOFTWARE 8 (LOCDEFER buffers only).
static uint32_t IDirectSoundBuffer_GetStatus(Ctx *c) {
    SELF(I_BUF);
    if (!ARG(1)) return DSERR_INVALIDPARAM;
    pthread_mutex_lock(&mx);
    uint32_t s = (o->playing ? 1u : 0) | (o->playing && o->looping ? 4u : 0) | (o->flags & 0x40000 ? 8u : 0);
    pthread_mutex_unlock(&mx);
    st(c, ARG(1), s, 4);
    return DS_OK;
}
static uint32_t IDirectSoundBuffer_Initialize(Ctx *c) { SELF(I_BUF); return 0x88780082u; }   // DSERR_ALREADYINITIALIZED
static uint32_t IDirectSoundBuffer_Restore(Ctx *c) { SELF(I_BUF); return DS_OK; }
// Lock(dwOffset, dwBytes, ppvAudioPtr1, pdwAudioBytes1, ppvAudioPtr2, pdwAudioBytes2, dwFlags):
// DSBLOCK_FROMWRITECURSOR 1, DSBLOCK_ENTIREBUFFER 2. A range past the end wraps into the second part.
static uint32_t IDirectSoundBuffer_Lock(Ctx *c) {
    SELF(I_BUF);
    uint32_t off = ARG(1), n = ARG(2), p1 = ARG(3), n1 = ARG(4), p2 = ARG(5), n2 = ARG(6), flags = ARG(7);
    if (o->primary) return DSERR_PRIOLEVELNEEDED;               // no DSSCL_WRITEPRIMARY here
    if (flags & 2) { off = 0; n = o->size; }
    if (flags & 1) {
        pthread_mutex_lock(&mx);
        off = cursor_bytes(o, o->pos);
        pthread_mutex_unlock(&mx);
    }
    if (!p1 || !n1 || off >= o->size || !n || n > o->size) return DSERR_INVALIDPARAM;
    uint32_t a = n < o->size - off ? n : o->size - off;
    st(c, p1, o->data + off, 4); st(c, n1, a, 4);
    if (p2) st(c, p2, n > a ? o->data : 0, 4);
    if (n2) st(c, n2, n - a, 4);
    if (n > a && !p2) return DSERR_INVALIDPARAM;
    return DS_OK;
}
static uint32_t IDirectSoundBuffer_Unlock(Ctx *c) {
    SELF(I_BUF);
    uint32_t frames = o->size / o->align, nf = (ARG(2) + ARG(4)) / o->align;
    uint32_t off = ARG(1) - o->data, hi = off + ARG(2) + ARG(4);   // ARG(1) is the pointer Lock handed out
    pthread_mutex_lock(&mx);
    if (ARG(1) >= o->data && ARG(1) < o->data + o->size) {   // a pointer outside the buffer proves nothing
        if (off < o->wrote_lo || (!o->wrote_lo && !o->wrote_hi)) o->wrote_lo = off;
        if (hi > o->wrote_hi) o->wrote_hi = hi;
    }
    o->since_unlock = 0; o->unlocks++; o->streamed += ARG(2) + ARG(4); o->written = 1;
    o->avail = nf >= frames ? frames : fmin(frames, fmax(o->avail, 0) + nf);
    if (o->held) o->fade_in = 128;      // 3 ms: takes the click off the resume, loses no data
    o->held = 0;
    pthread_mutex_unlock(&mx);
    return DS_OK;
}
// Play(dwReserved1, dwPriority, dwFlags): DSBPLAY_LOOPING 1. The primary buffer must loop.
static uint32_t IDirectSoundBuffer_Play(Ctx *c) {
    SELF(I_BUF);
    uint32_t flags = ARG(3);
    if (o->primary && !(flags & 1)) return DSERR_INVALIDPARAM;
    int cue = o->primary ? 0 : cue_of_buffer(c, ARG(0));
    pthread_mutex_lock(&mx);
    if (!o->primary) {
        acct.plays++;
        if (!o->playing) { o->nm = (uint16_t)cue; o->mixed_pos = o->play_pos = o->pos; play_begin(o); o->play_us = now_us(); }     // Play on a playing buffer only updates its looping flag
    }
    o->playing = 1;
    o->looping = flags & 1;
    static unsigned starts;
    if (trace_cue(o) && starts++ < 60) {
        unsigned nonzero = 0;
        for (uint32_t i = 0; i < o->size / o->align; i++) if (sample(o, i, 0) != 0) nonzero++;
        rn_log("dsound cue start: %s size %u nonzero %u pos %.12f", vt[o->nm].name, o->size, nonzero, o->pos);
    }
    pthread_mutex_unlock(&mx);
    static int played;
    if (soundlog() && !o->primary && played++ < soundlog_cap())
        rn_log("dsound: Play #%d t=%llu ms %s buffer %08x %u Hz %u-bit %u ch, volume %d", played,
               (unsigned long long)(now_us() / 1000), vt[o->nm < VN ? o->nm : 0].name, ARG(0), o->rate, o->bits, o->channels,
               o->volume);
    return DS_OK;
}
static uint32_t IDirectSoundBuffer_Stop(Ctx *c) {
    SELF(I_BUF);
    static int stops;
    if (soundlog() && !o->primary && o->playing && stops++ < soundlog_cap())       // how long it ran against how much data the game fed it
        rn_log("dsound: Stop #%d t=%llu ms buffer %08x ran %.2f s, streamed %.2f s (%u Hz %u-bit %u ch)", stops,
               (unsigned long long)(now_us() / 1000), ARG(0), (double)(now_us() - o->play_us) / 1e6,
               (double)o->streamed / ((double)o->rate * o->align), o->rate, o->bits, o->channels);
    int kind = stop_kind(c);
    pthread_mutex_lock(&mx);
    if (o->playing) play_end(o, kind);
    o->playing = 0; o->streamed = 0;
    pthread_mutex_unlock(&mx);
    return DS_OK;
}
static uint32_t IDirectSoundBuffer_SetCurrentPosition(Ctx *c) {
    SELF(I_BUF);
    uint32_t p = ARG(1);
    if (o->primary) return DSERR_INVALIDCALL;
    if (p >= o->size) return DSERR_INVALIDPARAM;
    pthread_mutex_lock(&mx); o->pos = o->mixed_pos = o->play_pos = p / o->align; o->avail = o->size / o->align; o->held = 0; pthread_mutex_unlock(&mx);   // unknown fill: assume full (never hold early)
    return DS_OK;
}

// ---- IDirectSound3DListener and IDirectSound3DBuffer ------------------------------------------------
// Arguments are 32-bit floats on the stack; "apply" (DS3D_IMMEDIATE / DEFERRED) is ignored: values count at once.
static float fl(Ctx *c, int i) { uint32_t v = ARG(i); float f; memcpy(&f, &v, 4); return f; }
static uint32_t IDirectSound3DListener_SetOrientation(Ctx *c) {
    SELF(I_L3D);
    pthread_mutex_lock(&mx);
    for (int k = 0; k < 3; k++) { lis_front[k] = fl(c, 1 + k); lis_top[k] = fl(c, 4 + k); }
    pthread_mutex_unlock(&mx);
    return DS_OK;
}
static uint32_t IDirectSound3DListener_SetPosition(Ctx *c) {
    SELF(I_L3D);
    pthread_mutex_lock(&mx);
    for (int k = 0; k < 3; k++) lis_pos[k] = fl(c, 1 + k);
    pthread_mutex_unlock(&mx);
    return DS_OK;
}
static uint32_t IDirectSound3DListener_SetRolloffFactor(Ctx *c) {
    SELF(I_L3D);
    float f = fl(c, 1);
    if (!(f >= 0 && f <= 10)) return DSERR_INVALIDPARAM;
    pthread_mutex_lock(&mx); lis_rolloff = f; pthread_mutex_unlock(&mx);
    return DS_OK;
}
static uint32_t IDirectSound3DListener_SetVelocity(Ctx *c) { SELF(I_L3D); return DS_OK; }   // no Doppler
// Requested but never played (WORKLIST #11, rea/map/sound_virtual.md): the sndmfc tick 0x10302d70 sorts the playing
// sounds by priority, gives voices to the first [0x10315020], then commits the listener (call at 0x10303283). At that
// point a sound with playing +0x18d == 1 and no channel +0x143 is virtual: pool full, past its 3D falloff +0x14d,
// audibility +0x13e off, or a 3D start still pending. Counted per file name (+0x114 data, +0x118 length).
static uint32_t vt_ticks, vt_full;
static float gfl(Ctx *c, uint32_t a) { float f; memcpy(&f, G(a), 4); return f; }
static uint8_t vwhy[VN];                // one line per name, the first time it is seen without a voice
static void sound_virtual(Ctx *c) {
    uint32_t b = ld(c, 0x10315974, 4), e = ld(c, 0x10315978, 4), voices = ld(c, 0x10315020, 4), used = 0;
    if (b < LOW || e < b || e - b > 4096 || e > c->memsz) return;
    vt_ticks++;
    for (uint32_t p = b; p < e; p += 4) {
        uint32_t s = ld(c, p, 4);
        if (s < LOW || s > c->memsz - 0x194) continue;
        uint32_t ch = ld(c, s + 0x143, 4);
        if (ch && (ch < LOW || ch > c->memsz - 8)) ch = 0;
        if (!ch && ld(c, s + 0x18d, 1) != 1) continue;
        used += ch != 0;
        int j = vt_index(c, s);
        if (j < 0) continue;
        const char *nm = vt[j].name;
        if (ch) {
            vt[j].voiced++;
            uint32_t buf = ld(c, ch + 4, 4);     // a Play that ran before the voice was named (cue_of_buffer found nothing) gets it now
            pthread_mutex_lock(&mx);                // the mixer reads in_play/nm of these under mx (play_end)
            for (int k = 0; k < nobjs; k++)
                if (objs[k].iface == I_BUF && objs[k].guest == buf && objs[k].in_play && !objs[k].nm) objs[k].nm = (uint16_t)j;
            pthread_mutex_unlock(&mx);
        } else {
            vt[j].virt++;
            if (!vwhy[j]++) {                     // one line per name: audibility, 3D and distance
                uint32_t aud = ld(c, s + 0x13e, 1), is3d = ld(c, s + 0x147, 1);
                float d = 0, hot = 0, fall = 0;
                if (is3d) {                             // S_SoundOmni: hotspot +0x149, falloff +0x14d, position +0x151
                    float x = gfl(c, s + 0x151), y = gfl(c, s + 0x155), z = gfl(c, s + 0x159);
                    d = sqrtf(x * x + y * y + z * z);   // in listener space: the distance the tick compares
                    hot = gfl(c, s + 0x149); fall = gfl(c, s + 0x14d);
                }
                rn_log("dsound virtual first: %s aud %u 3d %u prio %.1f dist %.1f hot %.1f fall %.1f", nm, aud, is3d,
                       gfl(c, s + 0x193), d, hot, fall);
            }
        }
    }
    if (voices && used >= voices) vt_full++;
}
static uint8_t lost_shown[VN];
static void virtual_report(void) {
    char b[1536]; int n = 0, never = 0;
    for (int j = 0; j < nvt; j++)                       // the column #11 asks for: per cue, not per run
        if (vt[j].plays && !vt[j].audible && !lost_shown[j] && !strstr(vt[j].name, "placeholder.wav") && !strstr(vt[j].name, "empty.wav")) {
            lost_shown[j] = 1;                          // once per cue; the two files that are all zeros in the data (census/sounds.tsv) are not losses
            rn_log("dsound cue lost: %s plays %u audible 0 virtual %u", vt[j].name, vt[j].plays, vt[j].virt);
        }
    for (int j = 0; j < nvt; j++) if (vt[j].virt && !vt[j].voiced) {
        never++;
        if (n < (int)sizeof b - 64) n += snprintf(b + n, sizeof b - n, "%s%s %u", n ? ", " : "", vt[j].name, vt[j].virt);
    }
    rn_log("dsound virtual: %d names seen, %d asked and never voiced (ticks virtual): %s; pool full %u of %u ticks",
           nvt, never, n ? b : "none", vt_full, vt_ticks);
}
static uint32_t IDirectSound3DListener_CommitDeferredSettings(Ctx *c) {
    SELF(I_L3D);
    if (ld(c, c->r[ESP], 4) == 0x10303286) sound_virtual(c);   /* the tick's own commit, after every voice is assigned */
    return DS_OK;
}
// The setters write the buffer the interface came from: the mixer reads p3/min_d/max_d there. Writing them on the
// interface object left every 3D buffer at max distance 0, and DSBCAPS_MUTE3DATMAXDISTANCE (all the game's buffers,
// flags 0x300f0) muted it: no gunshots.
#define B3D_BUF Obj *b = &objs[o->parent]
static uint32_t IDirectSound3DBuffer_SetPosition(Ctx *c) {
    SELF(I_B3D); B3D_BUF;
    pthread_mutex_lock(&mx);
    for (int k = 0; k < 3; k++) b->p3[k] = fl(c, 1 + k);
    pthread_mutex_unlock(&mx);
    return DS_OK;
}
static uint32_t IDirectSound3DBuffer_SetMinDistance(Ctx *c) {
    SELF(I_B3D); B3D_BUF;
    float f = fl(c, 1);
    if (!(f > 0)) return DSERR_INVALIDPARAM;
    pthread_mutex_lock(&mx); b->min_d = f; pthread_mutex_unlock(&mx);
    return DS_OK;
}
static uint32_t IDirectSound3DBuffer_SetMaxDistance(Ctx *c) {
    SELF(I_B3D); B3D_BUF;
    float f = fl(c, 1);
    if (!(f > 0)) return DSERR_INVALIDPARAM;
    pthread_mutex_lock(&mx); b->max_d = f; pthread_mutex_unlock(&mx);
    return DS_OK;
}
// Cones and velocity: the game sets them (omnidirectional sound, no Doppler), nothing here uses them.
static uint32_t IDirectSound3DBuffer_SetConeAngles(Ctx *c) { SELF(I_B3D); return DS_OK; }
static uint32_t IDirectSound3DBuffer_SetConeOrientation(Ctx *c) { SELF(I_B3D); return DS_OK; }
static uint32_t IDirectSound3DBuffer_SetConeOutsideVolume(Ctx *c) { SELF(I_B3D); return DS_OK; }
static uint32_t IDirectSound3DBuffer_SetVelocity(Ctx *c) { SELF(I_B3D); return DS_OK; }

// The methods the game calls go through note() (calls and errors per method); QueryInterface, AddRef and Release are shared.
#define UNKNOWN_METHODS(i) \
    static uint32_t i##_QueryInterface(Ctx *c) { return QueryInterface_(c); } \
    static uint32_t i##_AddRef(Ctx *c) { return AddRef_(c); } \
    static uint32_t i##_Release(Ctx *c) { return Release_(c); }
UNKNOWN_METHODS(IDirectSound) UNKNOWN_METHODS(IDirectSoundBuffer) UNKNOWN_METHODS(IDirectSound3DListener)
UNKNOWN_METHODS(IDirectSound3DBuffer)
#define X(i, m, n, k) static uint32_t i##_##m##_w(Ctx *c) { return note(c, M_##i##_##m, i##_##m(c)); }
METHODS(X)
#undef X
const Api dsound_apis[] = {
    {"dsound.dll", "#1", 3, DirectSoundCreate_}, {"dsound.dll", "#2", 2, DirectSoundEnumerateA_},
#define X(i, m, n, k) {"dsound.dll", #i "::" #m, n, i##_##m##_w},
    METHODS(X)
#undef X
};
const int dsound_api_count = sizeof dsound_apis / sizeof dsound_apis[0];
