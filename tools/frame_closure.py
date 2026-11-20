#!/usr/bin/env python3
"""Direct-call closure of the frame tick in MaxPayne.exe (Capstone, recursive descent over direct calls only).
Marks functions that call an allocator or a lock. Virtual calls are not followed: the closure is a lower bound.
usage: frame_closure.py <MaxPayne.exe> <mfc42.exports.ghidra> <out.tsv> [root=0x40c020]"""
import collections, re, struct, sys
import pefile
from capstone import CS_ARCH_X86, CS_MODE_32, Cs, x86
pe = pefile.PE(sys.argv[1]); img = pe.get_memory_mapped_image(); base = 0x400000
mfc = {int(o): n for o, n in re.findall(r'ORDINAL="(\d+)" NAME="([^"]+)"', open(sys.argv[2]).read())}
root = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0x40c020
iat = {}
for e in pe.DIRECTORY_ENTRY_IMPORT:
    for i in e.imports:
        iat[i.address] = e.dll.decode().lower() + '!' + (i.name.decode() if i.name else mfc.get(i.ordinal, f'#{i.ordinal}'))
md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True
TEXT_END = 0x401000 + 0x3c9000
def thunk_of(a):
    i = next(md.disasm(bytes(img[a - base:a - base + 6]), a), None)
    if i and i.mnemonic == 'jmp' and i.operands and i.operands[0].type == x86.X86_OP_MEM and i.operands[0].mem.base == 0:
        return iat.get(i.operands[0].mem.disp & 0xffffffff)
def body(f):
    """Instructions of f by recursive descent inside f (jumps followed, calls not)."""
    seen, todo, out = set(), [f], []
    while todo and len(seen) < 20000:
        a = todo.pop()
        while a not in seen and 0x401000 <= a < TEXT_END:
            seen.add(a)
            i = next(md.disasm(bytes(img[a - base:a - base + 16]), a), None)
            if not i: break
            out.append(i)
            if i.mnemonic == 'ret': break
            if i.group(x86.X86_GRP_JUMP):
                op = i.operands[0]
                if op.type == x86.X86_OP_IMM: todo.append(op.imm & 0xffffffff)
                if i.mnemonic == 'jmp': break
            a = i.address + i.size
    return out
ALLOC = re.compile(r'\?\?2@|!malloc$|!calloc$|!realloc$|_Grow@|GetBufferSetLength|\?\?0CString|\?\?4CString|\?\?YCString|!operator new')
LOCK = re.compile(r'WaitForSingleObject|ReleaseMutex|EnterCriticalSection|R_Mutex::')
funcs, depth, edges = {}, {root: 0}, collections.defaultdict(set)
q = collections.deque([root])
while q:
    f = q.popleft()
    if f in funcs or len(funcs) > 6000: continue
    ins = body(f); info = dict(n=len(ins), alloc=[], lock=[], icall=0, imports=set())
    for i in ins:
        if i.mnemonic != 'call': continue
        op = i.operands[0]
        if op.type == x86.X86_OP_IMM:
            t = op.imm & 0xffffffff; name = thunk_of(t)
            if name:
                info['imports'].add(name)
                if ALLOC.search(name): info['alloc'].append(f'{i.address:#x}:{name.split("!")[1][:40]}')
                if LOCK.search(name): info['lock'].append(f'{i.address:#x}')
            elif 0x401000 <= t < TEXT_END:
                edges[f].add(t)
                if t not in depth: depth[t] = depth[f] + 1; q.append(t)
        elif op.type == x86.X86_OP_MEM and op.mem.base == 0 and (op.mem.disp & 0xffffffff) in iat:
            name = iat[op.mem.disp & 0xffffffff]; info['imports'].add(name)
            if ALLOC.search(name): info['alloc'].append(f'{i.address:#x}:{name.split("!")[1][:40]}')
        else:
            info['icall'] += 1
    funcs[f] = info
with open(sys.argv[3], 'w') as o:
    o.write(f'# direct-call closure of {root:#x}; {len(funcs)} functions; virtual/indirect calls not followed (icall = count)\n')
    o.write('func\tdepth\tinsns\ticalls\tallocs\tlocks\n')
    for f, i in sorted(funcs.items(), key=lambda kv: depth[kv[0]]):
        o.write(f'{f:#x}\t{depth[f]}\t{i["n"]}\t{i["icall"]}\t{" ".join(i["alloc"])}\t{" ".join(i["lock"])}\n')
tot = lambda k: sum(len(i[k]) if isinstance(i[k], list) else i[k] for i in funcs.values())
print(len(funcs), 'functions; insns', tot('n'), 'icalls', tot('icall'), 'alloc sites', tot('alloc'), 'in', sum(1 for i in funcs.values() if i['alloc']), 'functions; max depth', max(depth[f] for f in funcs))
imps = collections.Counter(n for i in funcs.values() for n in i['imports'])
print('imports reached:', ', '.join(f'{n.split("!")[1][:38]}' for n, _ in imps.most_common(60) if not n.startswith(('e2mfc', 'rlmfc', 'grphmfc', 'sndmfc'))))
print('engine dll imports reached:', sum(1 for n in imps if n.startswith(('e2mfc', 'rlmfc', 'grphmfc', 'sndmfc'))))
