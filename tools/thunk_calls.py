#!/usr/bin/env python3
"""Call sites per imported function: direct calls/jumps to its `jmp [IAT]` thunk plus `call [IAT]` (Capstone linear sweep).
usage: thunk_calls.py <game dir> <mfc42.exports.ghidra> <out.tsv>"""
import collections, re, sys
from pathlib import Path
import pefile
from capstone import CS_ARCH_X86, CS_MODE_32, Cs
BINS = ['MaxPayne.exe', 'e2mfc.dll', 'rlmfc.dll', 'grphmfc.dll', 'sndmfc.dll', 'e2driver/e2_d3d8_driver_mfc.dll']
mfc = {int(o): n for o, n in re.findall(r'ORDINAL="(\d+)" NAME="([^"]+)"', open(sys.argv[2]).read())}
md = Cs(CS_ARCH_X86, CS_MODE_32); md.skipdata = True
rows = []
for b in BINS:
    pe = pefile.PE(str(Path(sys.argv[1]) / b)); img = pe.get_memory_mapped_image(); base = pe.OPTIONAL_HEADER.ImageBase
    iat = {i.address: e.dll.decode().lower() + '!' + (i.name.decode() if i.name else mfc.get(i.ordinal, f'#{i.ordinal}'))
           for e in pe.DIRECTORY_ENTRY_IMPORT for i in e.imports}
    s = next(x for x in pe.sections if x.Name.startswith(b'.text'))
    ins = [i for i in md.disasm(img[s.VirtualAddress:s.VirtualAddress + s.Misc_VirtualSize], base + s.VirtualAddress) if i.id]
    thunk = {}
    for i in ins:
        m = re.fullmatch(r'dword ptr \[(0x[0-9a-f]+)\]', i.op_str)
        if m and int(m.group(1), 16) in iat:
            if i.mnemonic == 'jmp': thunk[hex(i.address)] = iat[int(m.group(1), 16)]
    c = collections.Counter()
    for i in ins:
        if i.mnemonic not in ('call', 'jmp'): continue
        m = re.fullmatch(r'dword ptr \[(0x[0-9a-f]+)\]', i.op_str)
        if i.mnemonic == 'call' and m and int(m.group(1), 16) in iat: c[iat[int(m.group(1), 16)]] += 1
        elif i.op_str in thunk: c[thunk[i.op_str]] += 1
    rows += [(b.split('/')[-1], n, k) for n, k in c.items()]
with open(sys.argv[3], 'w') as o:
    o.write('# call/jmp sites per import (to its thunk or through the IAT); linear sweep, so a few may be data\nbinary\timport\tsites\n')
    for r in sorted(rows, key=lambda r: (r[0], -r[2])): o.write(f'{r[0]}\t{r[1]}\t{r[2]}\n')
