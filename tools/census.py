#!/usr/bin/env python3
"""Static platform census of the Max Payne PC binaries (pefile + Capstone, linear sweep of executable sections).

usage: census.py <game dir> <out dir>
Writes per binary: imports with every referencing site, and platform-sensitive instructions.
Static coverage only: a linear sweep can mis-decode data inside .text; Ghidra (REA) is the authority for
function boundaries. Import sites are memory operands equal to an IAT slot, so they are exact when decoded.
"""
import collections
import hashlib
import json
import sys
from pathlib import Path

import pefile
from capstone import CS_ARCH_X86, CS_MODE_32, Cs, x86

BINS = ['MaxPayne.exe', 'e2mfc.dll', 'rlmfc.dll', 'grphmfc.dll', 'sndmfc.dll', 'e2driver/e2_d3d8_driver_mfc.dll']
# Instructions a WASM recompiler cannot treat as plain arithmetic.
SPECIAL = {'cpuid', 'rdtsc', 'int', 'int3', 'into', 'in', 'out', 'insb', 'insd', 'outsb', 'outsd', 'cli', 'sti',
           'hlt', 'iretd', 'lgdt', 'sgdt', 'sidt', 'lidt', 'sldt', 'str', 'smsw', 'lmsw', 'wbinvd', 'invd',
           'femms', 'emms', 'prefetch', 'prefetchw', 'prefetchnta', 'prefetcht0', 'sfence', 'lfence', 'mfence',
           'fxsave', 'fxrstor', 'ldmxcsr', 'stmxcsr', 'fldcw', 'fnstcw', 'fstcw', 'fnclex', 'fninit', 'pushfd',
           'popfd', 'lock', 'xchg', 'cmpxchg', 'cmpxchg8b', 'bound', 'arpl', 'les', 'lds', 'lfs', 'lgs', 'lss'}


def iset(ins):
    """Coarse ISA family of one instruction."""
    ops = ins.op_str
    m = ins.mnemonic
    if ins.bytes[:2] == b'\x0f\x0f' or m in ('femms', 'prefetch', 'prefetchw') or m.startswith('pf') or m in ('pi2fd', 'pi2fw', 'pmulhrw', 'pavgusb', 'pswapd'):
        return '3dnow'
    if 'xmm' in ops or m in ('ldmxcsr', 'stmxcsr', 'sfence', 'lfence', 'mfence') or m.startswith('prefetcht') or m == 'prefetchnta':
        return 'sse'
    if 'mm' in ops.replace('xmm', '') and any(r in ops for r in ('mm0', 'mm1', 'mm2', 'mm3', 'mm4', 'mm5', 'mm6', 'mm7')):
        return 'mmx'
    if m.startswith('f') and m not in ('femms',):
        return 'x87'
    return 'int'


def census(path):
    pe = pefile.PE(str(path))
    base = pe.OPTIONAL_HEADER.ImageBase
    img = pe.get_memory_mapped_image()
    iat = {}
    for e in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', []):
        for imp in e.imports:
            name = imp.name.decode() if imp.name else f'#{imp.ordinal}'
            iat[imp.address] = f'{e.dll.decode().lower()}!{name}'
    # Leftover SafeDisc stub sections: any code reference into them is a DRM call that must be removed.
    stubs = {}
    for s in pe.sections:
        if s.Name.startswith((b'stxt', b'.MGD')):
            n = s.Name.rstrip(b'\0').decode()
            for a in range(base + s.VirtualAddress, base + s.VirtualAddress + s.Misc_VirtualSize):
                stubs[a] = n
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    md.skipdata = True
    sites = collections.defaultdict(list)
    special = collections.defaultdict(list)
    isa = collections.Counter()
    fs_refs = []
    ninsn = 0
    for s in pe.sections:
        name = s.Name.rstrip(b'\0').decode(errors='replace')
        # The shipped MaxPayne.exe is a SafeDisc-unwrapped dump: every section is RW data (0xc0000040), code included.
        if not (s.Characteristics & 0x20000000 or name == '.text'):
            continue
        va = base + s.VirtualAddress
        code = img[s.VirtualAddress:s.VirtualAddress + s.Misc_VirtualSize]
        for ins in md.disasm(code, va):
            if ins.id == 0:  # skipdata
                continue
            ninsn += 1
            isa[(name, iset(ins))] += 1
            m = ins.mnemonic
            if m in SPECIAL or ins.prefix[0] == 0xf0:
                special[m if ins.prefix[0] != 0xf0 else 'lock ' + m].append(ins.address)
            for op in ins.operands:
                if op.type == x86.X86_OP_MEM:
                    if op.mem.segment == x86.X86_REG_FS:
                        fs_refs.append((ins.address, f'{m} {ins.op_str}'))
                    if op.mem.base == 0 and op.mem.index == 0:
                        d = op.mem.disp & 0xffffffff
                        if d in iat:
                            sites[iat[d]].append((ins.address, m, name))
                        elif d in stubs:
                            sites['<' + stubs[d] + '>'].append((ins.address, m, name))
                if op.type == x86.X86_OP_IMM and ins.group(1) | ins.group(2) and stubs.get(op.imm & 0xffffffff):
                    sites['<' + stubs[op.imm & 0xffffffff] + '>'].append((ins.address, m, name))
                if op.type == x86.X86_OP_IMM and (op.imm & 0xffffffff) in iat:
                    sites[iat[op.imm & 0xffffffff]].append((ins.address, m + ' imm', name))
    strings = []
    for s in pe.sections:
        if s.Characteristics & 0x20000000 or s.Name.startswith(b'.text'):
            continue
        data = s.get_data()
        cur, start = bytearray(), 0
        for i, b in enumerate(data):
            if 0x20 <= b < 0x7f:
                if not cur:
                    start = i
                cur.append(b)
            else:
                if len(cur) >= 4:
                    strings.append((base + s.VirtualAddress + start, cur.decode()))
                cur = bytearray()
    exports = []
    if hasattr(pe, 'DIRECTORY_ENTRY_EXPORT'):
        for sym in pe.DIRECTORY_ENTRY_EXPORT.symbols:
            exports.append((sym.ordinal, base + sym.address, sym.name.decode() if sym.name else ''))
    secs = [(s.Name.rstrip(b'\0').decode(errors='replace'), base + s.VirtualAddress, s.Misc_VirtualSize, hex(s.Characteristics)) for s in pe.sections]
    return dict(sha256=hashlib.sha256(path.read_bytes()).hexdigest(), base=base,
                entry=base + pe.OPTIONAL_HEADER.AddressOfEntryPoint, sections=secs, instructions=ninsn,
                iat=iat, sites=sites, special=special, isa=isa, fs=fs_refs, strings=strings, exports=exports)


def main():
    game, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    summary = {}
    with open(out / 'import_sites.tsv', 'w') as f_imp, open(out / 'special_insns.tsv', 'w') as f_sp, \
            open(out / 'strings.tsv', 'w') as f_str, open(out / 'exports.tsv', 'w') as f_exp:
        f_imp.write('binary\timport\tnsites\tsites (addr:mnemonic)\n')
        f_sp.write('binary\tmnemonic\tcount\tfirst sites\n')
        f_str.write('binary\taddr\tstring\n')
        f_exp.write('binary\tordinal\taddr\tname\n')
        for b in BINS:
            r = census(game / b)
            short = Path(b).name
            for imp in sorted(set(r['iat'].values()) | {k for k in r['sites'] if k.startswith('<')}):
                ss = r['sites'].get(imp, [])
                f_imp.write(f'{short}\t{imp}\t{len(ss)}\t' + ' '.join(f'{a:#x}:{m}' for a, m, _ in ss[:40]) + '\n')
            for m, ss in sorted(r['special'].items(), key=lambda kv: -len(kv[1])):
                f_sp.write(f'{short}\t{m}\t{len(ss)}\t' + ' '.join(f'{a:#x}' for a in ss[:30]) + '\n')
            for a, s in r['strings']:
                f_str.write(f'{short}\t{a:#x}\t{s}\n')
            for o, a, n in r['exports']:
                f_exp.write(f'{short}\t{o}\t{a:#x}\t{n}\n')
            isa = collections.defaultdict(dict)
            for (sec, fam), n in r['isa'].items():
                isa[sec][fam] = n
            summary[short] = dict(sha256=r['sha256'], base=hex(r['base']), entry=hex(r['entry']), sections=r['sections'],
                                  instructions=r['instructions'], imports=len(set(r['iat'].values())),
                                  imports_without_site=sorted(i for i in set(r['iat'].values()) if not r['sites'].get(i)),
                                  isa=isa, fs_refs=len(r['fs']), fs_examples=[(hex(a), t) for a, t in r['fs'][:12]],
                                  exports=len(r['exports']))
    (out / 'binaries.json').write_text(json.dumps(summary, indent=1))


main()
