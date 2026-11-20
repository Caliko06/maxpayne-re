// Host layer: the Windows APIs the Max Payne images import, written against the documented API.
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include "rt.h"

// Guest memory layout: MaxPayne.exe at 0x400000 (no relocations), the DLLs rebased to 0x10000000..0x10a12000.
#define TEB_ADDR    0x00010000u
#define PEB_ADDR    0x00011000u
#define STR_ADDR    0x00012000u     // command line, environment, module name
#define STACK_LO    0x00300000u     // 1 MB main stack (SizeOfStackReserve)
#define STACK_HI    0x00400000u
#define IMAGE_BASE  0x00400000u
#define SCREEN_ADDR 0x00013000u     // PATCH 0x40877e reads the screen width and height here (win32_init writes them)
#define JUMP_ADDR   0x00013100u     // host/jump.c: one RN_JUMP script line for X_Message::parse (test only)
// PATCH 0x40bf09..: the 2D quad code reads S/320 (+8) and S (+12) as floats; S = 4:3 over the screen aspect, at most 1
#define HEAP_LO     0x11000000u     // above the last guest image (tools/recomp/images.py)
#define HOST_BASE   0xe0000000u     // fake code addresses of host functions: HOST_BASE + 16*index
// Flat guest space. MaxPayne.exe is LARGEADDRESSAWARE (PE characteristics 0x12f), so 64-bit Windows gives it 4 GB of user
// space. Native: everything below the fake host addresses.
#ifdef __EMSCRIPTEN__
// Web: 1.25 GB (heap ceiling 0x4ff00000 - HEAP_LO = 1007 MB). A late level of the OWNER's HD install (4096x2048 maps,
// host/d3d8.c MAXTEX) holds 741-748 MB of decoded textures at 1280x900 (fleet s1104: part1_level6-9 741 MB, part1_level4-7
// 748, part3_level4-20 745), and at 751 MB of heap an 8 MB readback and a 16 MB copy failed inside the level teardown.
// The 1 GB was sized on the 554 MB peak of the 25-map route at 1024x768 (2026-11-02, Pod mem1g, wasm heap 1439 MB), a
// measurement the product resolution never reproduced. INITIAL_MEMORY=512MB, MAXIMUM_MEMORY=1920MB (build.py) leaves the host heap measured at
// 415 MB (wasm heap 1439 - guest block 1024) and stays under 2 GB for Firefox (bugzilla 2022805: WebGPU
// writeBuffer/writeTexture reject views of a larger buffer); 2048 MB would sit exactly on that limit. The 3 GB of
// 2026-10-31 (brk 0x3fde54b0 in the tutorial) came from P_Map holding every source image until its first use, which
// host/fast.c rn_ldb_import now uploads at import.
// One wasm, two sizes: the page picks the wasm memory (web/index.html profile) and RN_GUEST_MB sizes the block inside it.
extern uint32_t rn_guest_mem;
#define GUEST_MEM   rn_guest_mem
#else
#define GUEST_MEM   HOST_BASE
#endif

#include <dirent.h>

typedef uint32_t (*HostFn)(Ctx *c);
typedef struct { const char *dll, *name; int nargs; HostFn fn; } Api;   // nargs < 0: cdecl

extern Ctx *g_ctx;
extern const Api win32_apis[], user32_apis[], thread_apis[], gdi32_apis[], d3d8_apis[], dinput8_apis[], dsound_apis[];
extern const int win32_api_count, user32_api_count, thread_api_count, gdi32_api_count, d3d8_api_count, dinput8_api_count, dsound_api_count;

enum { H_FREE, H_FILE, H_EVENT, H_MUTEX, H_FIND, H_HEAP, H_THREAD, H_SNAPSHOT, H_KEY, H_MAPPING };
struct Thread;
typedef struct {
    int kind;
    int gf;                         // files: host/files.c id and the file pointer
    uint32_t pos;
    int signaled, manual;           // events; snapshot position
    uint32_t owner, count;          // mutexes
    struct Thread *thread;
    char path[512];
    DIR *dir;
    char pattern[256];
} Handle;
uint32_t new_handle(int kind);
Handle *get_handle(uint32_t h);

// Arguments of a stdcall/cdecl host function: [esp] = return address, [esp+4] = first argument.
#define ARG(n) ld(c, c->r[ESP] + 4 + 4 * (n), 4)
#define G(p) ((char *)c->mem + (uint32_t)(p))

const Api *find_api(const char *dll, const char *name);
uint32_t call_guest(Ctx *c, uint32_t fn, int n, const uint32_t *args);
uint32_t call_guest_cdecl(Ctx *c, uint32_t fn, int n, const uint32_t *args);
void reg_flush(void);                                         // host/win32.c: the registry file, if a value changed
uint32_t host_addr_of(const char *dll, const char *name);     // HOST_BASE address, registering if needed
void rn_halt(Ctx *c, const char *fmt, ...);
extern int rn_screen_w, rn_screen_h;   // the desktop the game sees: RN_SCREEN=WxH (the page passes its window size)
void rn_log(const char *fmt, ...);
void jump_poll(Ctx *c);
int rn_trace(void);
void win32_init(Ctx *c, uint32_t image_size);
uint32_t guest_alloc(uint32_t size);                           // zeroed, for host-owned guest data
void guest_free(uint32_t p);
uint64_t now_us(void);
uint32_t virtual_reserve(Ctx *c, uint32_t size);
void gil_acquire(void);
void gil_release(void);
void gil_slice(void);                                          // preempt at an API call after a quantum
// Telemetry: running totals that web/telemetry.js reads at every frame (layout mirrored there). Each field has
// one writer: the game threads under the GIL, or the mixer under its lock. The maxima are cleared by the page.
typedef struct {
    double gpu_wait_ms;         // Present waiting for the page (d3d9 hand_off)
    double gil_main_ms;         // the main thread waiting for its GIL turn (another guest thread runs)
    double gil_other_ms;        // every other guest thread waiting for its turn
    double read_ms;             // game file reads from the server (sync XHR, GIL held)
    double idle_main_ms;        // the main thread in Sleep or a wait (GIL given up on purpose)
    double hold_main_max_ms;    // longest time the main thread kept the GIL in one go (a maximum the page clears)
    double hold_other_max_ms;   // the same for every other guest thread
    double vsync_ms;            // Present waiting for the screen to take a frame (host/gpu.c; game ahead of 60 Hz)
    double gpu_read_ms;         // color/texture reads through the page (host/d3d8.c read_gpu)
    double save_ms;             // save files written to the server (host/files.c, sync XHR, GIL held)
    uint32_t presents, reads, read_kb;
    uint32_t stream_pm;         // worst looping buffer: frames mixed since its last Unlock, per mille of its length
    uint32_t starves;           // times a looping buffer played a whole lap with no Unlock (stale audio repeats)
    uint32_t starve_buf;        // dsound object index of the last one
    uint32_t clock_calls;       // GetTickCount, timeGetTime, QueryPerformanceCounter: many per frame = a busy wait
    uint32_t gpu_reads;         // host/d3d8.c read_gpu calls
    uint32_t saves, save_kb;    // host/files.c: files written to the server, KB written
    uint32_t guest_mem;         // wasm address of guest 0: the page reads game state with it
} Telem;
extern Telem rn_telem;
void dispatch_threads_report(void);                             // log what every guest thread is doing now
void threads_init(Ctx *main_ctx);
void threads_close_handle(uint32_t h);
void rn_stop(Ctx *c);
void rn_backtrace(Ctx *c);
void rn_host_call(Ctx *c, uint32_t target);                    // run one host API for code outside the lifted code
extern int rn_gil_off;                                          // forked debug children: one thread, no GIL                                     // log the return addresses on the guest stack                                          // report the halt and end the process
void files_set_root(const char *dir);
int gf_stat(const char *gpath);                                // 0 file, 1 directory, -1 missing, -2 outside
int gf_open(const char *gpath);                                // id, or -1 missing, -2 outside, -3 directory
uint64_t gf_size(int id);
int64_t gf_read(int id, uint64_t off, uint8_t *dst, uint32_t n);
void gf_retain(int id);
void gf_close(int id);
// Save folder only: create/open for writing (disp = CreateFileA dwCreationDisposition). id, or -1 missing,
// -2 outside, -3 a directory, -4 exists (CREATE_NEW), -5 read-only (game folder), -6 the folder is missing.
int gf_create(const char *gpath, int disp, int *existed);
int gf_is_save(int id);
int64_t gf_write(int id, uint64_t off, const uint8_t *src, uint32_t n);
int gf_truncate(int id, uint64_t size);
int gf_flush(int id);
int gf_mkdir(const char *gpath);                                // 0 made, 1 exists, -1 no parent, -2 outside
int gf_delete(const char *gpath);                               // 0, -1 missing, -2 outside the save folder
int gf_find(const char *gpath, int k, char *name, size_t n, int *is_dir, uint64_t *size);   // entry k of dir\\pattern
int gf_set_cwd(const char *gpath);                              // 0, -1 not a directory, -2 outside
void gf_get_cwd(char *out, size_t n);
int layer_load(const char *name, uint8_t *dst, int n);         // save/<name>: length, -1 missing
int layer_store(const char *name, const uint8_t *src, int n);
void set_error(Ctx *c, uint32_t e);
// Keyboard and mouse (host/input.c): input_poll hands new events to dinput8_* and user32_key.
void input_poll(void);
uint32_t input_vk_state(uint32_t vk);                          // GetAsyncKeyState/GetKeyState bits
int input_key_down(uint8_t dik);
int input_shift(void);
int input_alt(void);
char input_char_of(uint8_t vk, int shift);
void input_key_name(uint8_t dik, char *out, int n);                 // GetKeyNameText, US layout
void dinput8_key(uint8_t dik, int down);
void dinput8_button(int b, int down);
void dinput8_motion(int32_t dx, int32_t dy, int32_t dz);
void user32_key(uint8_t dik, uint8_t vk, int down);
