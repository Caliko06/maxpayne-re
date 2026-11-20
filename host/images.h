// Guest image tables made by tools/recomp/images.py (build/gen/image_tables.c).
#pragma once
#include "rt.h"

typedef struct { const char *name, *file; uint32_t base, size, entry; int dll, plugin; } RnImage;
typedef struct { uint32_t slot, guest; const char *dll, *name; } RnSlot;     // guest == 0: a host API
typedef struct { uint32_t image, ordinal, va; const char *name; } RnExport;

extern const RnImage rn_images[];
extern const uint32_t rn_image_count, rn_init_order[], rn_init_count;
extern const RnSlot rn_slots[];
extern const uint32_t rn_slot_count;
extern const RnExport rn_exports[];
extern const uint32_t rn_export_count;

int image_by_name(const char *path);              // index of a guest image by file name (any directory), or -1
int image_of(uint32_t addr);                       // index of the image holding a guest address, or -1
uint32_t image_proc(int img, const char *name);    // GetProcAddress: name or "#ordinal"; 0 if missing
int image_loaded(int img);                         // DllMain already ran
int image_load(Ctx *c, int img);                   // LoadLibrary of a plugin: run its DllMain once; 0 on failure
