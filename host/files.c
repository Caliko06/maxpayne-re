// Read-only access to the game folder (C:\MaxPayne = the game dir given on the command line) and the save folder
// (save/ next to it = My Documents, SAVE_DIR). Names match without case, like NTFS.
// Relative names resolve against the current directory (SetCurrentDirectoryA), which starts at C:\MaxPayne.
// Native: the folder on disk. Web: tools/serve.py; the game thread reads byte ranges with synchronous
// XHR (allowed in its worker), through a read-ahead buffer, so only what the game opens is fetched.
#include <ctype.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include "host.h"
#ifdef __EMSCRIPTEN__
#include <pthread.h>
static void level_prefetch_start(void);
#endif

#ifdef __EMSCRIPTEN__
void d3d_load_checkpoint(void);
#endif

#define MAXF 256
// Web reads come in aligned blocks of BLOCK bytes kept in one cache shared by every handle (the game reopens the same archive
// many times and alternates between its index and its data); reads of BLOCK or more skip the cache.
#define BLOCK (256u << 10)
#define NBLOCKS 640                // 160 MB; RN_FILECACHE_MB lowers it on a phone profile (web/index.html)
// Save files live whole in memory (data): changed there, written back whole by gf_flush and gf_close.
typedef struct {
    int used, refs, save, dirty, snap;      // snap: a read-only handle whose bytes were all read at open (a save)
    uint64_t size, cap;
    char real[512];
    FILE *f;
    uint8_t *data;
} GFile;
static GFile files[MAXF];
static char root[1024], save_root[1024];

void files_set_root(const char *dir) {
    snprintf(root, sizeof root, "%s", dir);
    snprintf(save_root, sizeof save_root, "%s/../save", dir);     // tools/serve.py serves the same save/
#ifdef __EMSCRIPTEN__
    level_prefetch_start();
#endif
#ifndef __EMSCRIPTEN__
    mkdir(save_root, 0755);
#endif
}
// The save folder is the user's My Documents: Max Payne keeps "Max Payne Savegames" there (exe 0x40f9e0,
// SHGetSpecialFolderPathA CSIDL_PERSONAL); host state (registry.tsv) sits next to it.
#define SAVE_DIR "C:/Documents and Settings/Player/My Documents"

static char cwd[512] = "C:/MaxPayne";
// An absolute name with '/' separators: relative names join the current directory, "\x" is on drive C:,
// and "." and ".." components are resolved.
static void absolute(const char *gpath, char *out, size_t n) {
    char p[1100];
    if (gpath[0] && gpath[1] == ':') snprintf(p, sizeof p, "%s", gpath);
    else if (gpath[0] == '\\' || gpath[0] == '/') snprintf(p, sizeof p, "C:%s", gpath);
    else snprintf(p, sizeof p, "%s/%s", cwd, gpath);
    for (char *q = p; *q; q++) if (*q == '\\') *q = '/';
    char *part[64];
    int np = 0;
    char *save;
    for (char *t = strtok_r(p + 2, "/", &save); t && np < 64; t = strtok_r(NULL, "/", &save)) {
        if (!strcmp(t, ".")) continue;
        if (!strcmp(t, "..")) { if (np) np--; continue; }
        part[np++] = t;
    }
    snprintf(out, n, "%c:", p[0]);
    for (int k = 0; k < np; k++) { strncat(out, "/", n - strlen(out) - 1); strncat(out, part[k], n - strlen(out) - 1); }
    if (np == 0) strncat(out, "/", n - strlen(out) - 1);
}
// C:\MaxPayne\a\b, or a\b relative to the current directory -> "a/b": 0 in the game folder, 1 in the save
// folder, -1 outside both. D:\ is the CD-ROM drive with the game disc in it; the OWNER's folder holds those files too.
static int game_rel(const char *gpath, char *rel, size_t n) {
    char p[512];
    absolute(gpath, p, sizeof p);
    const char *r = p;
    int save = 0, k = (int)strlen(SAVE_DIR);
    if (!strncasecmp(p, SAVE_DIR, k) && (p[k] == '/' || !p[k])) r = p + k + (p[k] == '/'), save = 1;
    else if (!strncasecmp(p, "C:/MaxPayne", 11) && (p[11] == '/' || !p[11])) r = p + 11 + (p[11] == '/');
    else if ((p[0] == 'D' || p[0] == 'd') && p[1] == ':' && (p[2] == '/' || !p[2])) r = p + 2 + (p[2] == '/');
    else if (p[0] && p[1] == ':') return -1;
    char *o = rel;
    for (; *r && (size_t)(o - rel) < n - 1; r++)
        if (!(*r == '/' && (o == rel || o[-1] == '/'))) *o++ = *r;     // drop empty components
    *o = 0;
    return save;
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// Where the files come from: RN_FILES (web/opfs.js) is a blob: URL of {game, save}, each [[path, size, blob URL]] (size -1 =
// a directory), for the game folder and saves the page keeps in OPFS; without it, tools/serve.py's /game-files.json and
// /save-files.json, read by path. Module.rnUrls maps "game/<path>" / "save/<path>" to the URL web_read asks.
EM_JS(void, web_files, (const char *opfs), {
    if (Module.rnFiles) return;
    opfs >>>= 0;
    var get = u => { var x = new XMLHttpRequest(); x.open('GET', u, false); x.send(); return JSON.parse(x.responseText); };
    var u = opfs ? UTF8ToString(opfs) : '';
    Module.rnUrls = new Map();
    if (u) {
        Module.rnFiles = get(u);
        for (const [p, n, b] of Module.rnFiles.game) Module.rnUrls.set('game/' + p, b);
        for (const [p, n, b] of Module.rnFiles.save) if (b) Module.rnUrls.set('save/' + p, b);
    } else Module.rnFiles = { game: get('/game-files.json'), save: null };
    // Compressible files also exist as 1 MB gzip pieces (tools/build_public.py gzip_pieces); a host without them answers 404.
    var x = new XMLHttpRequest(); x.open('GET', '/game-z.json', false); x.send();
    Module.rnZ = x.status === 200 ? JSON.parse(x.responseText) : { piece: 1, files: {} };
    Module.rnZc = new Map();
    var files = new Map(), dirs = new Set();
    for (const [p, n] of Module.rnFiles.game) {
        var low = p.toLowerCase();
        files.set(low, [p, n]);
        var parts = low.split('/');
        for (var k = 1; k < parts.length; k++) dirs.add(parts.slice(0, k).join('/'));
    }
    Module.rnIndex = { files: files, dirs: dirs };
});
// Every guest thread runs in its own worker with its own Module: each loads the index on its first use.
static void files_load(void) { static const char *u; if (!u) { const char *e = getenv("RN_FILES"); u = e ? e : ""; } web_files(u); }
// A level hint's bytes of a piece-backed file (web_read): its gzip pieces are fetched and dropped, so they wait in the browser's
// HTTP cache (immutable, on disk) instead of the phone's 32 MB block cache, which held 16 MB of a 117 MB hint and let the guest
// wait ~200 ms per miss for 20 s of part1_level1 (out/loadz1120/phone_log). 1 if it did that, 0 if the file has no pieces.
EM_JS(int, rn_zwarm, (const char *real), {
    var r = UTF8ToString(real >>> 0);
    return !Module.rnUrls.get(r) && Module.rnZ.files[r] ? 1 : 0;
});
EM_JS(int, web_warm, (const char *real, double off, double n), {
    var r = UTF8ToString(real >>> 0), names = Module.rnUrls.get(r) ? null : Module.rnZ.files[r], P = Module.rnZ.piece;
    if (!names) return 0;
    for (var k = Math.floor(off / P); k <= Math.floor((off + n - 1) / P) && k < names.length; k++) {
        try { var x = new XMLHttpRequest(); x.open('GET', '/game-z/' + names[k] + '.gz', false); x.responseType = 'arraybuffer'; x.send(); } catch (e) {}
    }
    return 1;
});
// A small text file of the site into out; its length, or -1 (RE-NFSU host/files.c web_text).
EM_JS(int, web_text, (const char *url, char *out, int cap), {
    try {
        var x = new XMLHttpRequest();
        x.open('GET', UTF8ToString(url), false);
        x.send();
        if (x.status !== 200) { err('files: ' + UTF8ToString(url) + ': HTTP ' + x.status); return -1; }
        return stringToUTF8(x.responseText, out, cap);
    } catch (e) { err('files: ' + UTF8ToString(url) + ': ' + e.message); return -1; }
});
// Index of game/: lower-case path -> [real path, size]; directories too.
EM_JS(double, web_lookup, (const char *rel, char *real, int realsz), {
    rel >>>= 0; real >>>= 0;
    var key = UTF8ToString(rel).toLowerCase();
    var e = Module.rnIndex.files.get(key);
    if (!e) return Module.rnIndex.dirs.has(key) || key === '' ? -2 : -1;
    stringToUTF8(e[0], real, realsz);
    return e[1];
});
// The save folder's index, "path\tsize\n" per entry (size -1 = a directory): its length.
EM_JS(int, web_save_index, (char *out, int outsz), {
    out >>>= 0;
    var list = Module.rnFiles.save;
    if (!list) { var x = new XMLHttpRequest(); x.open('GET', '/save-files.json', false); x.send(); list = JSON.parse(x.responseText); }
    var s = '';
    for (const [p, n] of list) s += p + '\t' + n + '\n';
    if (lengthBytesUTF8(s) >= outsz) return -1;
    return stringToUTF8(s, out, outsz);
});
// Names in a game directory, "name\tsize\n" (size -1 = a directory), into out; the count.
EM_JS(int, web_list, (const char *rel, char *out, int outsz), {
    rel >>>= 0; out >>>= 0;
    var dir = UTF8ToString(rel).toLowerCase(), seen = new Map();
    var add = (p, n) => {
        var low = p.toLowerCase();
        if (dir && !low.startsWith(dir + '/')) return;
        var rest = p.slice(dir ? dir.length + 1 : 0), k = rest.indexOf('/');
        if (k < 0) seen.set(rest, n); else if (!seen.has(rest.slice(0, k))) seen.set(rest.slice(0, k), -1);
    };
    for (const [low, e] of Module.rnIndex.files) add(e[0], e[1]);
    var s = '';
    for (const [name, n] of seen) s += name + '\t' + n + '\n';
    stringToUTF8(s, out, outsz);
    return seen.size;
});
// real is "game/<path>" or "save/<path>". A server that ignores Range sends the whole file (200): the part is cut from it.
EM_JS(int, web_read, (const char *real, double off, uint8_t *dst, int n), {
    real >>>= 0; dst >>>= 0;
    // The game has no answer to a failed read (it hangs on its loading screen), and a play over the internet makes hundreds
    // of requests: a dropped one (a phone changing cell, a 5xx at the edge) is asked again after 0.3, 1, 2, 4 s.
    var r = UTF8ToString(real), waits = [300, 1000, 2000, 4000];
    var get = (url, range, ok) => {
        for (var t = 0; ; t++) {
            try {
                var x = new XMLHttpRequest();
                x.open('GET', url, false);
                x.responseType = 'arraybuffer';
                if (range) x.setRequestHeader('Range', range);
                x.send();
                if (ok(x)) return x;
                err('files: ' + r + ' ' + url + ' ' + range + ': HTTP ' + x.status + ', try ' + (t + 1));
            } catch (e) { err('files: ' + r + ' ' + url + ' ' + range + ': ' + e.message + ', try ' + (t + 1)); }
            if (t === waits.length) return null;
            try { Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, waits[t]); }
            catch (e) { var end = Date.now() + waits[t]; while (Date.now() < end) {} }
        }
    };
    var names = Module.rnUrls.get(r) ? null : Module.rnZ.files[r];     // a blob: of the page (web/opfs.js) wins
    if (names) {
        // Gzip pieces: the browser decodes them (Content-Encoding). This worker keeps its last 4, so the guest's 256 KB
        // blocks of one piece cost one request.
        var P = Module.rnZ.piece, done = 0;
        while (done < n) {
            var at = off + done, k = Math.floor(at / P), name = names[k], piece = Module.rnZc.get(name);
            if (!name) break;
            if (piece) Module.rnZc.delete(name);
            else {
                var x = get('/game-z/' + name + '.gz', null, x => x.status === 200);
                if (!x) return -1;
                piece = new Uint8Array(x.response);
                if (Module.rnZc.size >= 4) Module.rnZc.delete(Module.rnZc.keys().next().value);
            }
            Module.rnZc.set(name, piece);
            var a = piece.subarray(at - k * P, Math.min(piece.length, at - k * P + n - done));
            if (!a.length) break;
            HEAPU8.set(a, dst + done);
            done += a.length;
        }
        return done;
    }
    var url = Module.rnUrls.get(r) || '/' + r.split('/').map(encodeURIComponent).join('/');
    var x = get(url, 'bytes=' + off + '-' + (off + n - 1), x => x.status === 206 || (x.status === 200 && x.response.byteLength >= off + n));
    if (!x) return -1;
    var a = x.status === 206 ? new Uint8Array(x.response) : new Uint8Array(x.response, off, n);
    HEAPU8.set(a, dst);
    return a.length;
});
#endif

// RN_FILELOG: every guest read (file, offset, size) and, on the web, every request to the server; default runs print only errors.
static int filelog(void) { static int v = -1; if (v < 0) v = getenv("RN_FILELOG") != NULL; return v; }
#ifdef __EMSCRIPTEN__
// A synchronous request holds the GIL while the page answers: with RN_FILELOG, the ones that take 5 ms or more are logged.
static void slow_log(const char *what, const char *rel, uint64_t t0) {
    if (filelog() && now_us() - t0 >= 5000) rn_log("slow request %s %s: %.1f ms (GIL held)", what, rel, (double)(now_us() - t0) / 1000);
}
#endif
#ifdef __EMSCRIPTEN__
// The save folder lives in this table after one GET of its index: lookups, listings and reads of what this page wrote
// never wait for the server, and writes go out behind the game, in order, from the page's main thread (save_send). Each
// synchronous request cost 2-3 ms with the GIL held: a save (~20 of them) took 40-70 ms in one frame of play (#5).
// The page is the folder's only writer (each port has its own save folder, tools/serve.py).
typedef struct { char rel[256]; int dir; uint64_t size; uint8_t *data; } SaveEnt;   // data NULL: on the server, not read yet
static SaveEnt *saves;
static int nsaves, capsaves;
static SaveEnt *save_get(const char *rel) {
    for (int k = 0; k < nsaves; k++) if (!strcasecmp(saves[k].rel, rel)) return &saves[k];
    return NULL;
}
static SaveEnt *save_add(const char *rel, int dir) {
    SaveEnt *e = save_get(rel);
    if (e) return e;
    if (nsaves == capsaves) saves = realloc(saves, sizeof *saves * (capsaves = capsaves ? capsaves * 2 : 64));
    e = &saves[nsaves++];
    memset(e, 0, sizeof *e);
    snprintf(e->rel, sizeof e->rel, "%s", rel);
    e->dir = dir;
    return e;
}
static void save_index(void) {
    static int loaded;
    if (loaded) return;
    loaded = 1;
    static char list[1 << 18];
    files_load();
    if (web_save_index(list, sizeof list) < 0) { rn_log("save folder index too big"); return; }
    for (char *l = list, *next; *l; l = next) {
        char *e = strchr(l, '\n');
        if (e) { *e = 0; next = e + 1; } else next = l + strlen(l);     // the last line may lack its '\n'
        char *t = strchr(l, '\t');
        if (!t) continue;                                                // a malformed line: skip it
        *t = 0;
        double v = atof(t + 1);
        for (char *c = strchr(l, '/'); c; c = strchr(c + 1, '/')) { *c = 0; save_add(l, 1); *c = '/'; }   // its parents
        save_add(l, v < 0)->size = v < 0 ? 0 : (uint64_t)v;
    }
}
// The bytes of a save file: read from the server once, then kept. 0 = e->data holds e->size bytes.
static int save_bytes(SaveEnt *e) {
    if (e->data) return 0;
    char real[300];
    snprintf(real, sizeof real, "save/%s", e->rel);
    e->data = malloc(e->size ? e->size : 1);
    files_load();
    if (e->data && (!e->size || web_read(real, 0, e->data, (int)e->size) == (int)e->size)) return 0;
    free(e->data);
    e->data = NULL;
    return -1;
}
// PUT a file (p, n), PUT a directory (dir), or DELETE (del), queued on the page's main thread (FIFO, then one promise
// chain): the game thread does not wait. A failed request is a console error in the page log.
static void save_send(const char *rel, const uint8_t *p, uint64_t n, int dir, int del) {
    char *r = strdup(rel);
    uint8_t *b = p ? malloc(n ? n : 1) : NULL;
    if (!r || (p && !b)) {      // out of memory: an empty PUT would truncate the player's save to 0 bytes, so send nothing
        free(r);
        free(b);
        rn_log("save: out of memory, %s not written", rel);
        MAIN_THREAD_ASYNC_EM_ASM({ if (Module.rnSaveFailed) Module.rnSaveFailed('out of memory'); });
        return;
    }
    if (n) memcpy(b, p, n);
    MAIN_THREAD_ASYNC_EM_ASM({
        var r = $0 >>> 0;      // one var per statement: a top-level comma would split the macro argument
        var b = $1 >>> 0;
        var rel = UTF8ToString(r);
        var url = '/save/' + rel.split('/').map(encodeURIComponent).join('/') + ($3 ? '/' : '');
        var body = b ? HEAPU8.slice(b, b + $2) : null;
        var method = $4 ? 'DELETE' : 'PUT';
        _free(r);
        if (b) _free(b);
        var bad = why => Module.rnSaveFailed(method + ' ' + rel + ': ' + why);    // web/index.html: the player sees it
        if (Module.rnSave) { Module.rnSave(method, rel, $3, body).catch(e => bad(e.message)); return; }   // web/opfs.js: in order, in its worker
        Module.rnSaveQ = (Module.rnSaveQ || Promise.resolve()).then(() => fetch(url, { method: method, body: body }))
            .then(x => { if (!x.ok && !($4 && x.status === 404)) bad('HTTP ' + x.status); }, e => bad(e));   // deleting what is gone: done
    }, r, b, (int)n, dir, del);
}
#endif
// 0 = found (size set), 1 = a directory, -1 = missing, -2 = outside the game and save folders.
static int lookup(const char *gpath, char *real, size_t n, uint64_t *size) {
    char rel[512];
    int save = game_rel(gpath, rel, sizeof rel);
    if (save < 0) return -2;
#ifdef __EMSCRIPTEN__
    files_load();
    if (save) {
        save_index();
        SaveEnt *e = *rel ? save_get(rel) : NULL;
        if (!*rel || (e && e->dir)) return 1;
        if (!e) return -1;
        snprintf(real, n, "save/%s", e->rel);
        *size = e->size;
        return 0;
    }
    char g[512];
    uint64_t t0 = now_us();
    double r = web_lookup(rel, g, sizeof g);
    slow_log("lookup", rel, t0);
    if (r == -2) return 1;
    if (r < 0) return -1;
    snprintf(real, n, "game/%s", g);
    *size = (uint64_t)r;
    return 0;
#else
    // Each component, matched without case against the directory on disk.
    snprintf(real, n, "%s", save ? save_root : root);
    for (char *c = rel; *c;) {
        char *e = strchr(c, '/');
        if (e) *e = 0;
        size_t len = strlen(real);
        snprintf(real + len, n - len, "/%s", c);
        struct stat sb;
        if (stat(real, &sb)) {
            real[len] = 0;
            DIR *d = opendir(real);
            int found = 0;
            struct dirent *de;
            while (d && (de = readdir(d)))
                if (!strcasecmp(de->d_name, c)) { snprintf(real + len, n - len, "/%s", de->d_name); found = 1; break; }
            if (d) closedir(d);
            if (!found) return -1;
        }
        if (!e) break;
        c = e + 1;
    }
    struct stat sb;
    if (stat(real, &sb)) return -1;
    if (S_ISDIR(sb.st_mode)) return 1;
    *size = (uint64_t)sb.st_size;
    return 0;
#endif
}

// 0 = a file, 1 = a directory, -1 = missing, -2 = outside the game folder.
int gf_stat(const char *gpath) {
    char real[512];
    uint64_t size;
    return lookup(gpath, real, sizeof real, &size);
}

static int save_rel(const char *gpath, char *rel, size_t n);
// Opens a game file: id >= 0, or -1 missing, -2 outside the folder, -3 a directory.
int gf_open(const char *gpath) {
    char real[512];
    uint64_t size = 0;
    int r = lookup(gpath, real, sizeof real, &size);
    if (r) return r == 1 ? -3 : r;
    for (int k = 0; k < MAXF; k++) {
        GFile *g = &files[k];
        if (g->used) continue;
        memset(g, 0, sizeof *g);
#ifndef __EMSCRIPTEN__
        g->f = fopen(real, "rb");
        if (!g->f) return -1;
#endif
        g->used = 1;
        g->refs = 1;
        g->size = size;
        snprintf(g->real, sizeof g->real, "%s", real);
#ifdef __EMSCRIPTEN__
        // A save is read whole at open, from the save folder's table (save_bytes): a later write never tears a load.
        char rel[512];
        if (save_rel(gpath, rel, sizeof rel) == 1) {
            SaveEnt *e = save_get(real + 5);
            g->cap = size;
            g->data = malloc(size ? size : 1);
            if (!e || save_bytes(e) || !g->data) { free(g->data); g->data = NULL; g->used = g->refs = 0; return -1; }
            memcpy(g->data, e->data, size);
            g->snap = 1;                                      // gf_read serves it from memory; the handle stays read-only
        }
#endif
        return k;
    }
    return -1;
}
uint64_t gf_size(int id) { return files[id].size; }
void gf_retain(int id) {
    if (id >= 0 && id < MAXF && files[id].used) files[id].refs++;
}
void gf_close(int id) {
    if (id < 0 || id >= MAXF || !files[id].used) return;
    if (--files[id].refs > 0) return;
    gf_flush(id);
    if (files[id].f) { fclose(files[id].f); files[id].f = NULL; }
    free(files[id].data);
    files[id].data = NULL;
    files[id].used = 0;
}
// Reads up to n bytes at off; returns the count (short at the end of the file), -1 on an I/O error.
#ifdef __EMSCRIPTEN__
// One request to the server, counted for the telemetry (the callers hold the GIL).
static int timed_read(const char *real, uint64_t at, uint8_t *dst, uint32_t n) {
    files_load();
    uint64_t t0 = now_us();
    int r = web_read(real, (double)at, dst, (int)n);
    if (filelog()) { static int k; if (k++ < 40000) rn_log("fetch %s off %llu n %u -> %d %.1f ms", real, (unsigned long long)at, n, r, (double)(now_us() - t0) / 1000); }
    rn_telem.read_ms += (double)(now_us() - t0) / 1000;
    rn_telem.reads++;
    rn_telem.read_kb += (r > 0 ? (uint32_t)r : 0) >> 10;
    return r;
}
#endif
#ifdef __EMSCRIPTEN__
typedef struct { uint64_t key, blk, used; uint8_t *p; uint32_t len; int pre; } Block;   // pre: brought by a helper, not read by the guest yet
// Guest and helper threads (level_prefetch below) share blocks[]; a read of the cache, a copy out of a block and an insert run under
// blk_mu, a request to the server never does (pattern of RE-NFSU host/files.c blk_mu, Blk pieces).
static pthread_mutex_t blk_mu = PTHREAD_MUTEX_INITIALIZER;
static int pre_count;                                  // blocks with pre set (under blk_mu)
static struct { uint64_t key, b0, b1; int on; } inflight[5];   // requests on their way (under blk_mu): 0-3 the helpers, 4 the guest
static int in_flight(uint64_t key, uint64_t b) {
    for (int i = 0; i < 5; i++) if (inflight[i].on && inflight[i].key == key && b >= inflight[i].b0 && b <= inflight[i].b1) return 1;
    return 0;
}
static Block blocks[NBLOCKS];
static uint64_t block_clock;
static uint64_t path_key(const char *s) { uint64_t h = 1469598103934665603ull; for (; *s; s++) h = (h ^ (uint8_t)*s) * 1099511628211ull; return h; }
// One free or least recently used slot of the cache (limit: RN_FILECACHE_MB).
static int cache_limit(void) {
    static int limit;
    if (!limit) { const char *s = getenv("RN_FILECACHE_MB"); limit = s && atoi(s) > 0 ? atoi(s) * 4 : NBLOCKS; if (limit > NBLOCKS) limit = NBLOCKS; }
    return limit;
}
// keep_pre: a helper never evicts a block the guest has yet to read (under blk_mu); NULL when every slot is one.
static Block *block_slot(int keep_pre) {
    Block *v = NULL;
    for (int i = 0; i < cache_limit(); i++) {
        Block *c = &blocks[i];
        if (!c->p) return c;
        if (keep_pre && c->pre) continue;
        if (!v || c->used < v->used) v = c;
    }
    if (v && v->pre) { v->pre = 0; pre_count--; }
    return v;
}
static Block *block_find(uint64_t key, uint64_t b) {
    for (int i = 0; i < NBLOCKS; i++) if (blocks[i].p && blocks[i].key == key && blocks[i].blk == b) return &blocks[i];
    return NULL;
}
// The block b of the file (BLOCK bytes, fewer at its end): from the cache, else one request; the least recently used one makes room.
// Read-ahead: a miss on the block after the file's last miss doubles the run fetched in that one request (1, 2, 4 .. RA_MAX
// blocks), as a disk read-ahead does. Over the internet a load is request count x round trip (rea/map/load.md), and a level
// streams its archives mostly in order (2026-11-19: 72 of 175 archive reads to the menu started where the last one ended).
#define RA_MAX 16                  // 4 MB per request at most
static Block *block_get(GFile *g, uint64_t b) {
    uint64_t key = path_key(g->real);
    Block *c = block_find(key, b);
    for (;;) {      // a helper has this block on its way: wait for it, not a second request
        c = block_find(key, b);
        if (c) { c->used = ++block_clock; if (c->pre) { c->pre = 0; pre_count--; } return c; }
        if (!in_flight(key, b)) break;
        pthread_mutex_unlock(&blk_mu); usleep(1000); pthread_mutex_lock(&blk_mu);
    }
    static uint64_t last_key, last_blk, run = 1;
    run = key == last_key && b == last_blk + 1 ? (run * 2 > RA_MAX ? RA_MAX : run * 2) : 1;
    uint64_t at = b * BLOCK, nb = 1;
    while (nb < run && (b + nb) * BLOCK < g->size && !block_find(key, b + nb) && !in_flight(key, b + nb)) nb++;   // a helper's blocks are not asked twice
    uint64_t want = g->size - at < nb * BLOCK ? g->size - at : nb * BLOCK;
    static uint8_t *buf;
    if (!buf && !(buf = malloc(RA_MAX * BLOCK))) return NULL;
    inflight[4].key = key; inflight[4].b0 = b; inflight[4].b1 = b + nb - 1; inflight[4].on = 1;   // the helpers skip it (desk_after: 233 MB vs 137 MB unique by 70 s)
    pthread_mutex_unlock(&blk_mu);
    int r = timed_read(g->real, at, buf, (uint32_t)want);
    pthread_mutex_lock(&blk_mu);
    inflight[4].on = 0;
    if (r <= 0) return NULL;
    last_key = key; last_blk = b + nb - 1;
    Block *first = NULL;
    for (uint64_t k = 0; k < nb && k * BLOCK < (uint64_t)r; k++) {
        Block *v = block_slot(0);
        if (!v->p && !(v->p = malloc(BLOCK))) return NULL;
        uint32_t len = (uint64_t)r - k * BLOCK < BLOCK ? (uint32_t)((uint64_t)r - k * BLOCK) : BLOCK;
        memcpy(v->p, buf + k * BLOCK, len);
        v->key = key; v->blk = b + k; v->len = len; v->used = ++block_clock;
        if (!first) first = v;
    }
    if (first) first->used = ++block_clock;     // the block asked for is the newest
    return first;
}

// ---- web: the chosen level's files come ahead of the game (RN_LEVELHINT=<level id>, web/index.html) ----------------------------
// Port of RE-NFSU host/files.c pre_main/pre_spawn (NPRE helper threads on a request queue). /level_hint.tsv has lines
// "level \t path \t offset \t length [\t phone|desktop]". Helpers fetch them in order into blocks[]; the guest's reads then hit.
// Budget: the helpers stop while unread prefetched blocks fill half the cache, and never evict one (block_slot keep_pre), so
// the guest's use frees room and nothing is evicted before it is read (RE-NFSU 0ab0b2d: the hint must fit the cache).
#define NPRE 4
#define PCHUNK 8                   // blocks per request (2 MB)
#define NPQ 4096
static struct { char real[256]; uint64_t b0, b1; } pq[NPQ];
static unsigned pq_head, pq_tail;  // under blk_mu
static char hint_text[1 << 20];

static void *pre_main(void *first) {
    files_load();                  // each worker has its own Module.rnUrls
    uint8_t *buf = malloc(PCHUNK * BLOCK);
    if (first) {
        int n = web_text("/level_hint.tsv", hint_text, sizeof hint_text - 1);
        const char *lv = getenv("RN_LEVELHINT"), *ph = getenv("RN_LOWMEM") ? "phone" : "desktop";
        pthread_mutex_lock(&blk_mu);
        uint64_t kb = 0;
        for (char *l = n > 0 ? hint_text : 0; l && *l; ) {
            char id[64], path[256], dev[16] = "", real[300]; double off, len;
            int f = sscanf(l, "%63[^\t]\t%255[^\t]\t%lf\t%lf\t%15[^\t\n]", id, path, &off, &len, dev);
            if (f >= 4 && !strcmp(id, lv) && (f < 5 || !strcmp(dev, ph)) && len > 0) {
                const char *pp = path + (path[0] == '/');     // tools/level_hint.py writes URL paths ("/game/x_data.ras"); the cache keys g->real ("game/x_data.ras")
                snprintf(real, sizeof real, "%s%s", strncmp(pp, "game/", 5) ? "game/" : "", pp);
                if (strlen(real) < 256) {
                    uint64_t b0 = (uint64_t)off / BLOCK, b1 = ((uint64_t)off + (uint64_t)len - 1) / BLOCK;
                    for (uint64_t b = b0; b <= b1 && pq_tail - pq_head < NPQ; b += PCHUNK) {
                        unsigned k = pq_tail++ % NPQ;
                        snprintf(pq[k].real, 256, "%s", real);
                        pq[k].b0 = b; pq[k].b1 = b + PCHUNK - 1 < b1 ? b + PCHUNK - 1 : b1;
                        kb += (pq[k].b1 - pq[k].b0 + 1) * (BLOCK >> 10);
                    }
                }
            }
            l = strchr(l, '\n'); if (l) l++;
        }
        rn_log("files: level prefetch %s: %u requests, %u KB, budget %u KB", lv, pq_tail, (unsigned)kb, cache_limit() / 2 * (BLOCK >> 10));
        pthread_mutex_unlock(&blk_mu);
        for (int k = 1; k < NPRE && pq_tail; k++) {
            pthread_t t; pthread_attr_t a;
            pthread_attr_init(&a); pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED); pthread_attr_setstacksize(&a, 256 << 10);   // not the 16 MB default (build.py): 4 of them on a 640 MB phone
            pthread_create(&t, &a, pre_main, 0);
        }
    }
    for (;;) {
        pthread_mutex_lock(&blk_mu);
        if (pq_head == pq_tail) { pthread_mutex_unlock(&blk_mu); break; }
        unsigned q = pq_head % NPQ;
        if (rn_zwarm(pq[q].real)) {     // no budget: the browser's cache holds it
            char real[256]; snprintf(real, sizeof real, "%s", pq[q].real);
            uint64_t b0 = pq[q].b0, b1 = pq[q].b1;
            pq_head++;
            pthread_mutex_unlock(&blk_mu);
            web_warm(real, (double)(b0 * BLOCK), (double)((b1 - b0 + 1) * BLOCK));
            continue;
        }
        uint32_t cap = cache_limit() / 2;
        if (cap < PCHUNK) cap = PCHUNK;     // a tiny cache: still one chunk at a time, never a wait that cannot end
        if ((uint32_t)pre_count + PCHUNK > cap) { pthread_mutex_unlock(&blk_mu); usleep(20000); continue; }
        pq_head++;
        uint64_t key = path_key(pq[q].real), b0 = pq[q].b0, b1 = pq[q].b1;
        while (b0 <= b1 && (block_find(key, b0) || in_flight(key, b0))) b0++;        // the guest or another helper got there first
        while (b1 >= b0 && (block_find(key, b1) || in_flight(key, b1))) b1--;
        int slot = -1;
        for (int i = 0; i < 4 && b0 <= b1; i++) if (!inflight[i].on) { slot = i; break; }
        if (b0 > b1 || slot < 0) {      // nothing to bring, or all four lanes busy (cannot happen with NPRE 4 threads)
            pthread_mutex_unlock(&blk_mu); continue;
        }
        inflight[slot].key = key; inflight[slot].b0 = b0; inflight[slot].b1 = b1; inflight[slot].on = 1;
        char real[256]; snprintf(real, sizeof real, "%s", pq[q].real);
        pthread_mutex_unlock(&blk_mu);
        int r = web_read(real, (double)(b0 * BLOCK), buf, (int)((b1 - b0 + 1) * BLOCK));   // not timed_read: rn_telem is the guest's
        pthread_mutex_lock(&blk_mu);
        for (uint64_t k = 0; r > 0 && k <= b1 - b0 && k * BLOCK < (uint64_t)r; k++) {
            if (block_find(key, b0 + k)) continue;
            Block *v = block_slot(1);
            if (!v) break;
            if (!v->p && !(v->p = malloc(BLOCK))) break;
            uint32_t len = (uint64_t)r - k * BLOCK < BLOCK ? (uint32_t)((uint64_t)r - k * BLOCK) : BLOCK;
            memcpy(v->p, buf + k * BLOCK, len);
            v->key = key; v->blk = b0 + k; v->len = len; v->used = ++block_clock; v->pre = 1; pre_count++;
        }
        inflight[slot].on = 0;
        pthread_mutex_unlock(&blk_mu);
    }
    free(buf);
    return 0;
}
static void level_prefetch_start(void) {
    const char *e = getenv("RN_LEVELHINT");
    if (!e || !*e) return;
    pthread_t t; pthread_attr_t a;
    pthread_attr_init(&a); pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED); pthread_attr_setstacksize(&a, 256 << 10);   // not the 16 MB default (build.py): 4 of them on a 640 MB phone
    pthread_create(&t, &a, pre_main, (void *)e);
}
#endif
int64_t gf_read(int id, uint64_t off, uint8_t *dst, uint32_t n) {
#ifdef __EMSCRIPTEN__
    // Cached archive reads also separate long stretches of level initialization.
    d3d_load_checkpoint();
#endif
    GFile *g = &files[id];
    if (filelog()) { static int k; if (k++ < 40000) rn_log("fread %s off %llu n %u", g->real, (unsigned long long)off, n); }
    if (off >= g->size) return 0;
    if (n > g->size - off) n = (uint32_t)(g->size - off);
    if (g->save || g->snap) { memcpy(dst, g->data + off, n); return n; }
#ifdef __EMSCRIPTEN__
    uint32_t done = 0;
    while (done < n) {
        uint64_t at = off + done;
        uint32_t want = n - done;
        if (want >= BLOCK) {        // large reads go straight to the destination, up to the first block a helper brought or is bringing
            uint64_t key = path_key(g->real), b = at / BLOCK, e = (at + want - 1) / BLOCK, k = b;
            pthread_mutex_lock(&blk_mu);
            while (k <= e && !block_find(key, k) && !in_flight(key, k)) k++;
            pthread_mutex_unlock(&blk_mu);
            if (k == b) goto cached;    // without this the prefetched level came twice (desk_after2: Part1_Level1.ldb 101 MB for 51)
            if (k <= e) want = (uint32_t)(k * BLOCK - at);
            int r = timed_read(g->real, at, dst + done, want);
            if (r <= 0) return -1;
            done += (uint32_t)r;
            continue;
        }
    cached:
        pthread_mutex_lock(&blk_mu);      // the copy out of the block is under the lock too: a helper may evict it
        Block *b = block_get(g, at / BLOCK);
        if (!b) { pthread_mutex_unlock(&blk_mu); return -1; }
        uint32_t o = (uint32_t)(at - b->blk * BLOCK);
        if (o >= b->len) { pthread_mutex_unlock(&blk_mu); return done ? (int64_t)done : -1; }     // the server gave less than the file's size
        uint32_t k = b->len - o;
        if (k > want) k = want;
        memcpy(dst + done, b->p + o, k);
        pthread_mutex_unlock(&blk_mu);
        done += k;
    }
    return done;
#else
    if (fseeko(g->f, (off_t)off, SEEK_SET)) return -1;
    return (int64_t)fread(dst, 1, n, g->f);
#endif
}

// ---- writes: only inside the save folder; the game folder is read-only ------------------------

static int save_rel(const char *gpath, char *rel, size_t n) { return game_rel(gpath, rel, n); }

static int put_whole(const char *rel, const char *real, const uint8_t *p, uint64_t n) {
#ifdef __EMSCRIPTEN__
    (void)real;
    SaveEnt *e = save_add(rel, 0);
    uint8_t *d = malloc(n ? n : 1);
    if (!d || e->dir) { free(d); return -1; }
    if (n) memcpy(d, p, n);
    free(e->data);
    e->data = d;
    e->size = n;
    save_send(e->rel, p, n, 0, 0);
    return 0;
#else
    (void)rel;
    FILE *f = fopen(real, "wb");
    if (!f) return -1;
    size_t w = n ? fwrite(p, 1, n, f) : 0;
    return fclose(f) || w != n ? -1 : 0;
#endif
}

// The real path (native) of a save path that may not exist yet: its parent must be a directory.
static int save_target(const char *gpath, char *rel, char *real, size_t n) {
    if (save_rel(gpath, rel, 512) != 1) return -2;
    char parent[600];
    snprintf(parent, sizeof parent, "%s", gpath);
    char *s = strrchr(parent, '\\');
    if (!s) s = strrchr(parent, '/');
    if (!s) return -2;
    *s = 0;
    char preal[512];
    uint64_t sz;
    if (lookup(parent, preal, sizeof preal, &sz) != 1) return -3;
#ifdef __EMSCRIPTEN__
    snprintf(real, n, "save/%s", rel);
#else
    snprintf(real, n, "%s/%s", preal, s + 1);
#endif
    return 0;
}

int gf_create(const char *gpath, int disp, int *existed) {
    char rel[512], real[512];
    uint64_t size = 0;
    int r = lookup(gpath, real, sizeof real, &size), save = save_rel(gpath, rel, sizeof rel);
    if (r == -2 || save != 1) return r == -2 ? -2 : -5;     // outside, or the read-only game folder
    if (r == 1) return -3;
    *existed = r == 0;
    if (r == 0 && disp == 1) return -4;                       // CREATE_NEW
    if (r != 0 && (disp == 3 || disp == 5)) return -1;        // OPEN_EXISTING, TRUNCATE_EXISTING
    if (r != 0) {
        int t = save_target(gpath, rel, real, sizeof real);
        if (t) return t == -3 ? -6 : t;                       // -6: the folder does not exist
    }
    int k = 0;
    while (k < MAXF && files[k].used) k++;
    if (k == MAXF) return -1;
    GFile *g = &files[k];
    memset(g, 0, sizeof *g);
    g->used = g->refs = g->save = 1;
    snprintf(g->real, sizeof g->real, "%s", real);
    g->cap = size > 4096 ? size : 4096;
    g->data = calloc(1, g->cap);
    if (r == 0 && disp != 2 && disp != 5) {                   // keep the contents
#ifdef __EMSCRIPTEN__
        SaveEnt *e = save_get(real + 5);
        if (!e || save_bytes(e)) { gf_close(k); return -1; }
        memcpy(g->data, e->data, size);
#else
        FILE *f = fopen(real, "rb");
        if (!f || fread(g->data, 1, size, f) != size) { if (f) fclose(f); free(g->data); g->data = NULL; g->used = g->refs = 0; return -1; }
        fclose(f);
#endif
        g->size = size;
    } else g->dirty = 1;                                      // created or truncated: exists from now on
    // A file that existed keeps its old bytes on the server until the game closes the new one: a save is never half-empty.
    if (g->dirty && r != 0) gf_flush(k);
    return k;
}

int gf_is_save(int id) { return files[id].save; }

int64_t gf_write(int id, uint64_t off, const uint8_t *src, uint32_t n) {
    GFile *g = &files[id];
    if (!g->save) return -1;
    if (off + n > g->cap) {
        uint64_t cap = g->cap;
        while (off + n > cap) cap *= 2;
        g->data = realloc(g->data, cap);
        memset(g->data + g->cap, 0, cap - g->cap);
        g->cap = cap;
    }
    memcpy(g->data + off, src, n);
    if (off + n > g->size) g->size = off + n;
    g->dirty = 1;
    return n;
}

int gf_truncate(int id, uint64_t size) {
    GFile *g = &files[id];
    if (!g->save) return -1;
    if (size > g->size) { uint8_t z = 0; if (gf_write(id, size - 1, &z, 1) < 0) return -1; }
    else if (g->data && size < g->size) memset(g->data + size, 0, g->size - size);
    g->size = size;
    g->dirty = 1;
    return 0;
}

int gf_flush(int id) {
    GFile *g = &files[id];
    if (!g->save || !g->dirty) return 0;
    char rel[512];
    snprintf(rel, sizeof rel, "%s", g->real + (strncmp(g->real, "save/", 5) ? 0 : 5));
    uint64_t t0 = now_us();
    if (put_whole(rel, g->real, g->data, g->size)) return -1;
    rn_telem.save_ms += (double)(now_us() - t0) / 1000;     // web/telemetry.js: save windows
    rn_telem.saves++;
    rn_telem.save_kb += (uint32_t)(g->size >> 10);
    g->dirty = 0;
    return 0;
}

// 0 made, 1 already there, -1 the parent is missing, -2 outside the save folder.
// CopyFileA / MoveFileA (the quicksave backup copies slot 1 to slot 2: exe 0x410486). dst must be in the save folder.
// 0 done, -1 source missing or unreadable, -2 dst outside the save folder or an I/O error; fail_if_exists = 1 gives -3.
int gf_copy(const char *src, const char *dst, int fail_if_exists, int move) {
    int in = gf_open(src);
    if (in < 0) return -1;
    uint64_t n = gf_size(in);
    uint8_t *buf = malloc(n ? n : 1);
    int64_t got = n ? gf_read(in, 0, buf, (uint32_t)n) : 0;
    gf_close(in);
    int existed = 0, out = got == (int64_t)n ? gf_create(dst, fail_if_exists ? 1 : 2, &existed) : -1, r = 0;
    if (out < 0) r = out == -4 ? -3 : -2;
    else {
        if (n && gf_write(out, 0, buf, (uint32_t)n) != (int64_t)n) r = -2;
        gf_close(out);
    }
    free(buf);
    if (!r && move) gf_delete(src);
    return r;
}
int gf_mkdir(const char *gpath) {
    char rel[512], real[512];
    int r = gf_stat(gpath);
    if (r == 1) return 1;
    if (r == 0) return 1;
    int t = save_target(gpath, rel, real, sizeof real);
    if (t) return t == -3 ? -1 : -2;
#ifdef __EMSCRIPTEN__
    save_send(save_add(rel, 1)->rel, NULL, 0, 1, 0);
    return 0;
#else
    return mkdir(real, 0755) ? -1 : 0;
#endif
}

// 0 deleted, -1 missing, -2 outside the save folder.
int gf_delete(const char *gpath) {
    char rel[512], real[512];
    uint64_t size;
    if (save_rel(gpath, rel, sizeof rel) != 1) return -2;
    if (lookup(gpath, real, sizeof real, &size) != 0) return -1;
#ifdef __EMSCRIPTEN__
    SaveEnt *e = save_get(real + 5);
    if (!e) return -1;
    save_send(e->rel, NULL, 0, 0, 1);
    free(e->data);
    *e = saves[--nsaves];
    return 0;
#else
    return unlink(real) ? -1 : 0;
#endif
}

static int wild(const char *p, const char *s) {
    for (; *p; p++, s++) {
        if (*p == '*') { for (; ; s++) { if (wild(p + 1, s)) return 1; if (!*s) return 0; } }
        if (!*s || (*p != '?' && tolower((unsigned char)*p) != tolower((unsigned char)*s))) return 0;
    }
    return !*s;
}

// SetCurrentDirectoryA: 0, -1 not a directory, -2 outside both folders.
int gf_set_cwd(const char *gpath) {
    char real[512], abs[512];
    uint64_t sz;
    int r = lookup(gpath, real, sizeof real, &sz);
    if (r == -2) return -2;
    if (r != 1) return -1;
    absolute(gpath, abs, sizeof abs);
    snprintf(cwd, sizeof cwd, "%s", abs);
    return 0;
}
// GetCurrentDirectoryA: the current directory with '\' separators.
void gf_get_cwd(char *out, size_t n) {
    snprintf(out, n, "%s", cwd);
    for (char *q = out; *q; q++) if (*q == '/') *q = '\\';
}

// Entry k (0 = first) of dir\pattern: 0 found (name, is_dir, size), -1 none, -2 outside both folders.
int gf_find(const char *gpath, int k, char *name, size_t n, int *is_dir, uint64_t *size) {
    char dir[600];
    snprintf(dir, sizeof dir, "%s", gpath);
    char *s = strrchr(dir, '\\');
    if (!s) s = strrchr(dir, '/');
    const char *pat = s ? s + 1 : dir;
    char patc[256];
    snprintf(patc, sizeof patc, "%s", pat);
    if (s) *s = 0; else snprintf(dir, sizeof dir, ".");
    if (!strcmp(patc, "*.*")) snprintf(patc, sizeof patc, "*");
    char rel[512], real[512];
    uint64_t sz;
    int save = game_rel(dir, rel, sizeof rel);
    if (save < 0) return -2;
    int r = lookup(dir, real, sizeof real, &sz);
    if (r == -2) return -2;
    if (r != 1 && !(save && !*rel)) return -1;
#ifdef __EMSCRIPTEN__
    static char list[65536];
    if (save) {     // the save folder's table: the names one level under rel
        size_t rl = strlen(rel);
        for (int i = 0; i < nsaves; i++) {
            const char *p = saves[i].rel;
            if (rl && (strncasecmp(p, rel, rl) || p[rl] != '/')) continue;
            if (rl) p += rl + 1;
            if (strchr(p, '/') || !*p) continue;
            if (wild(patc, p) && !k--) { snprintf(name, n, "%s", p); *is_dir = saves[i].dir; *size = saves[i].size; return 0; }
        }
        return -1;
    }
    files_load();
    uint64_t t0 = now_us();
    web_list(rel, list, sizeof list);
    slow_log("list", rel, t0);
    for (char *l = list, *next; *l; l = next) {
        char *e = strchr(l, '\n');
        if (e) { *e = 0; next = e + 1; } else next = l + strlen(l);     // a truncated list: its last line has no '\n'
        char *t = strchr(l, '\t');
        if (!t) continue;
        *t = 0;
        double v = atof(t + 1);
        if (wild(patc, l) && !k--) { snprintf(name, n, "%s", l); *is_dir = v < 0; *size = v < 0 ? 0 : (uint64_t)v; return 0; }
    }
    return -1;
#else
    DIR *d = opendir(real);
    if (!d) return -1;
    struct dirent *de;
    while ((de = readdir(d)))
        if (strcmp(de->d_name, ".") && strcmp(de->d_name, "..") && wild(patc, de->d_name) && !k--) {
            char p[1100];
            struct stat sb;
            snprintf(p, sizeof p, "%s/%s", real, de->d_name);
            stat(p, &sb);
            snprintf(name, n, "%s", de->d_name);
            *is_dir = S_ISDIR(sb.st_mode);
            *size = (uint64_t)sb.st_size;
            closedir(d);
            return 0;
        }
    closedir(d);
    return -1;
#endif
}

// State the layer keeps for the PC, not the game: save/<name> (DirectInput user maps). The whole file in
// dst (at most n bytes): its length, -1 missing. layer_store replaces it.
int layer_load(const char *name, uint8_t *dst, int n) {
    char p[600];
    snprintf(p, sizeof p, SAVE_DIR "/%s", name);
    int id = gf_open(p);
    if (id < 0) return -1;
    int64_t r = gf_read(id, 0, dst, gf_size(id) < (uint64_t)n ? (uint32_t)gf_size(id) : (uint32_t)n);
    gf_close(id);
    return (int)r;
}
int layer_store(const char *name, const uint8_t *src, int n) {
    char p[600];
    int existed;
    snprintf(p, sizeof p, SAVE_DIR "/%s", name);
    int id = gf_create(p, 2, &existed);                          // CREATE_ALWAYS
    if (id < 0) return -1;
    int r = gf_write(id, 0, src, (uint32_t)n) == n ? 0 : -1;
    gf_close(id);
    return r;
}
