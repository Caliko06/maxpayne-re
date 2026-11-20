// The guest images (tools/recomp/images.py): lookups for the loader and for GetModuleHandle/LoadLibrary/GetProcAddress.
#include <strings.h>
#include "host.h"
#include "images.h"

static int inited[64];

int image_by_name(const char *path) {
    const char *b = path;
    for (const char *p = path; *p; p++) if (*p == '\\' || *p == '/') b = p + 1;
    char n[64];
    snprintf(n, sizeof n, "%s", b);
    if (!strchr(n, '.')) strncat(n, ".dll", sizeof n - strlen(n) - 1);
    for (uint32_t k = 0; k < rn_image_count; k++) if (!strcasecmp(n, rn_images[k].name)) return (int)k;
    return -1;
}

int image_of(uint32_t addr) {
    for (uint32_t k = 0; k < rn_image_count; k++)
        if (addr >= rn_images[k].base && addr < rn_images[k].base + rn_images[k].size) return (int)k;
    return -1;
}

uint32_t image_proc(int img, const char *name) {
    uint32_t ord = 0;
    if (name[0] == '#') ord = (uint32_t)atoi(name + 1);
    for (uint32_t k = 0; k < rn_export_count; k++) {
        const RnExport *e = &rn_exports[k];
        if (e->image != (uint32_t)img) continue;
        if (ord ? e->ordinal == ord : (e->name && !strcmp(e->name, name))) return e->va;
    }
    return 0;
}

int image_loaded(int img) { return img >= 0 && img < 64 && inited[img]; }

// DllMain(hinst, DLL_PROCESS_ATTACH, 0), once per image.
int image_load(Ctx *c, int img) {
    if (img < 0 || img >= 64) return 0;
    const RnImage *m = &rn_images[img];
    if (!m->dll || inited[img]) return 1;
    inited[img] = 1;
    uint32_t args[3] = {m->base, 1, 0};
    rn_log("DllMain %s (%08x)", m->name, m->entry);
    uint32_t ok = call_guest(c, m->entry, 3, args);
    if (c->halt) return 0;
    if (!ok) { rn_halt(c, "DllMain of %s returned FALSE", m->name); return 0; }
    return 1;
}
