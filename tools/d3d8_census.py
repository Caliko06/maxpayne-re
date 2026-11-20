#!/usr/bin/env python3
"""Census of COM vtable calls on interface pointers held in globals of e2_d3d8_driver_mfc.dll (Capstone, linear sweep).
Pattern per window: reg <- [G]; reg2 <- [reg]; call [reg2+off]. Reports (global, offset) -> sites. Static only."""
import collections, json, struct, sys
import pefile
from capstone import CS_ARCH_X86, CS_MODE_32, Cs, x86
pe = pefile.PE(sys.argv[1]); img = pe.get_memory_mapped_image(); base = pe.OPTIONAL_HEADER.ImageBase
DEV_OFF = 0xd0  # P_D3D::m_device (IDirect3DDevice8*), from the CreateDevice call at 0x100293ab
md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True; md.skipdata = True
text = next(s for s in pe.sections if s.Name.startswith(b'.text'))
insns = [i for i in md.disasm(img[text.VirtualAddress:text.VirtualAddress + text.Misc_VirtualSize], base + text.VirtualAddress) if i.id]
def mem(op):
    return op.type == x86.X86_OP_MEM
calls = collections.defaultdict(list)
src = {}  # reg -> ('g', G) or ('vt', G)
for i in insns:
    if i.mnemonic in ('ret', 'jmp') or i.group(1) and i.mnemonic != 'call':
        src = {}
    ops = i.operands
    if i.mnemonic == 'call' and ops and mem(ops[0]) and ops[0].mem.base in src and src[ops[0].mem.base][0] == 'vt' and ops[0].mem.index == 0:
        calls[(src[ops[0].mem.base][1], ops[0].mem.disp)].append(i.address)
        src = {r: v for r, v in src.items() if r not in (x86.X86_REG_EAX, x86.X86_REG_ECX, x86.X86_REG_EDX)}
        continue
    if i.mnemonic == 'mov' and len(ops) == 2 and ops[0].type == x86.X86_OP_REG:
        d, s = ops[0].reg, ops[1]
        if mem(s) and s.mem.base == 0 and s.mem.index == 0 and 0x10060000 <= (s.mem.disp & 0xffffffff) < 0x100a0000:
            src[d] = ('g', s.mem.disp & 0xffffffff); continue
        if mem(s) and s.mem.disp == DEV_OFF and s.mem.index == 0 and s.mem.base not in (0, x86.X86_REG_ESP, x86.X86_REG_EBP):
            src[d] = ('g', 'dev'); continue
        if mem(s) and s.mem.disp == 0 and s.mem.index == 0 and s.mem.base in src and src[s.mem.base][0] == 'g':
            src[d] = ('vt', src[s.mem.base][1]); continue
        if s.type == x86.X86_OP_REG and s.reg in src:
            src[d] = src[s.reg]; continue
        src.pop(d, None); continue
    if ops and ops[0].type == x86.X86_OP_REG and i.mnemonic not in ('push', 'cmp', 'test'):
        src.pop(ops[0].reg, None)
    if i.mnemonic == 'call':
        src = {r: v for r, v in src.items() if r not in (x86.X86_REG_EAX, x86.X86_REG_ECX, x86.X86_REG_EDX)}
byg = collections.defaultdict(dict)
for (g, off), ss in calls.items():
    byg[g][off] = ss
out = {(g if isinstance(g, str) else f'{g:#x}'): {f'{o:#x}': [f'{a:#x}' for a in ss] for o, ss in sorted(d.items())} for g, d in sorted(byg.items(), key=lambda kv: -sum(len(v) for v in kv[1].values()))}
json.dump(out, open(sys.argv[2], 'w'), indent=1)
for g, d in list(out.items())[:12]:
    print(g, sum(len(v) for v in d.values()), 'calls;', ' '.join(f'{o}x{len(v)}' for o, v in d.items()))
