#!/usr/bin/env python3
"""MSVC RTTI of an image: every vtable with its class, and every slot's function. Writes TSV to stdout:
vtable, class (undecorated tail of .?AV...), this_offset, slot, function. Class names of the unnamed exe come from here.
Usage: .venv/bin/python tools/recomp/rtti.py build/img/MaxPayne.exe > rea/map/MaxPayne.exe.vtables.tsv"""
import re, struct, sys
import pefile

pe = pefile.PE(sys.argv[1])
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image(ImageBase=base)
u32 = lambda a: struct.unpack_from("<I", img, a - base)[0] if 0 <= a - base <= len(img) - 4 else 0
text = next(s for s in pe.sections if s.Name.startswith(b".text"))
tlo, thi = base + text.VirtualAddress, base + text.VirtualAddress + text.Misc_VirtualSize
tds = {base + m.start() - 8: m.group()[4:-1].decode("latin1").removesuffix("@@") for m in re.finditer(rb"\.\?A[VU][^\0]{1,300}\0", img)}
cols = {a: (tds[u32(a + 12)], u32(a + 4)) for a in range(base, base + len(img) - 20, 4) if u32(a) == 0 and u32(a + 12) in tds}   # complete object locators
print("vtable\tclass\tthis_offset\tslot\tfunction")
for a in range(base, base + len(img) - 8, 4):           # a vtable is the word after a pointer to its locator
    if u32(a) in cols:
        name, off = cols[u32(a)]
        k = 0
        while tlo <= u32(a + 4 + 4 * k) < thi:
            print(f"{a + 4:08x}\t{name}\t{off}\t{k}\t{u32(a + 4 + 4 * k):08x}")
            k += 1
