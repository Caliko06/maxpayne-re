// ABK code modules (tools/recomp/abk.py): the sound engine copies the header of a game/Sound/*.abk to the
// heap and relocates the x86 code in it. Each module was lifted ahead of time; this finds the live copy an
// address belongs to: the copy starts with the ABK header, so "ABKC", id (+0x10) and size (+0x14) name it.
#include "rt.h"

extern const uint32_t rn_mod_count;
extern const RnMod rn_mods[];

void (*rn_mod_find(Ctx *c, uint32_t t, uint32_t *mb))(Ctx *) {
    for (uint32_t k = 0; k < rn_mod_count; k++) {
        const RnMod *m = &rn_mods[k];
        for (uint32_t j = 0; j < m->nfn; j++) {
            uint32_t b = t - m->off[j];
            if (b < LOW || b > c->memsz - 0x18 || memcmp(c->mem + b, "ABKC", 4) || ld(c, b + 0x10, 4) != m->id ||
                ld(c, b + 0x14, 4) != m->size)
                continue;
            *mb = b;
            return m->fn[j];
        }
    }
    return 0;
}
