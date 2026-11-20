#!/usr/bin/env python3
"""Recompiler front end: find every function and basic block in one guest image (build/img, tools/recomp/images.py).

Recursive descent from the entry point, the exports and from code pointers found in data.
Writes census/fn/<image>.json: imports, functions, indirect branches, jump tables, instruction mix,
and census/fn/<image>_functions.tsv. Usage: .venv/bin/python tools/recomp/frontend.py <image> <out.json>
"""
import collections, json, os, struct, sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_GRP_JUMP, CS_GRP_CALL, CS_GRP_RET, CS_GRP_INT
from capstone import x86

EXE, OUT = sys.argv[1], sys.argv[2]

pe = pefile.PE(EXE)
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
text = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
t0 = base + text.VirtualAddress
# The loader maps min(SizeOfRawData, VirtualSize rounded up to SectionAlignment) bytes of a section: code may sit
# past VirtualSize (e2mfc.dll 0x10148b54, right after .text's VirtualSize, runs after a plugin is loaded).
def mapped(s): al = pe.OPTIONAL_HEADER.SectionAlignment; return min(s.SizeOfRawData, -(-s.Misc_VirtualSize // al) * al) or s.Misc_VirtualSize
t1 = t0 + max(text.Misc_VirtualSize, mapped(text))
data_secs = [s for s in pe.sections if s.Name.rstrip(b"\0") != b".text"]

def in_text(a): return t0 <= a < t1
def u32(a): return struct.unpack_from("<I", img, a - base)[0]

# Imports: IAT slot address -> "dll!name"
iat = {}
for e in pe.DIRECTORY_ENTRY_IMPORT:
    for i in e.imports:
        iat[i.address] = f"{e.dll.decode().lower()}!{i.name.decode() if i.name else '#%d' % i.ordinal}"

md = Cs(CS_ARCH_X86, CS_MODE_32)
md.detail = True

insn_at = {}          # addr -> (size, mnemonic)
block_starts = set()
funcs = {}            # entry -> how it was found
jump_tables = {}      # jmp addr -> [targets]
indirect_jmp = []     # unresolved jmp reg / [mem]
indirect_call = collections.Counter()   # kind -> count
import_calls = collections.Counter()
groups = collections.Counter()
bad = []              # decode failures inside reached code

def mem_operand(ins):
    for op in ins.operands:
        if op.type == x86.X86_OP_MEM:
            return op.mem
    return None

def read_table(table, idx_scale):
    out = []
    a = table
    while len(out) < 4096 and any(s.VirtualAddress + base <= a < s.VirtualAddress + base + s.Misc_VirtualSize
                                  for s in pe.sections):
        v = u32(a)
        if not in_text(v):
            break
        out.append(v)
        a += 4
    return out

work = collections.deque()

def add_func(a, why):
    if in_text(a) and a not in funcs:
        funcs[a] = why
        work.append(a)

def add_block(a):
    if in_text(a) and a not in block_starts:
        block_starts.add(a)
        work.append(a)

def walk(a):
    while in_text(a):
        if a in insn_at:
            return
        off = a - base
        ins = next(md.disasm(bytes(img[off:off + 16]), a), None)
        if ins is None:
            bad.append(a)
            return
        insn_at[a] = (ins.size, ins.mnemonic)
        for g in ins.groups:
            groups[ins.group_name(g)] += 1
        nxt = a + ins.size
        m = ins.mnemonic
        op = ins.operands[0] if ins.operands else None
        if ins.group(CS_GRP_RET) or m in ("hlt", "ud2"):
            return
        if m == "int3":
            return
        if ins.group(CS_GRP_CALL):
            if op.type == x86.X86_OP_IMM:
                add_func(op.imm, "call")
            elif op.type == x86.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0 and op.mem.disp in iat:
                name = iat[op.mem.disp]
                import_calls[name] += 1
                if name.endswith(("!ExitProcess", "!_exit", "!exit")):
                    return
            else:
                indirect_call["reg" if op.type == x86.X86_OP_REG else "mem"] += 1
            a = nxt
            continue
        if ins.group(CS_GRP_JUMP):
            if op.type == x86.X86_OP_IMM:
                if m == "jmp":
                    if op.imm in funcs:      # tail call
                        return
                    add_block(op.imm)
                    return
                add_block(op.imm)
                add_block(nxt)
                return
            mm = mem_operand(ins)
            if mm is not None and mm.base == 0 and mm.index == 0 and mm.disp in iat:
                import_calls[iat[mm.disp]] += 1     # import thunk
                return
            if mm is not None and mm.index != 0 and mm.scale == 4 and mm.base == 0:
                tgts = read_table(mm.disp, 4)
                jump_tables[a] = tgts
                for t in tgts:
                    add_block(t)
                return
            indirect_jmp.append(a)
            return
        a = nxt

add_func(base + pe.OPTIONAL_HEADER.AddressOfEntryPoint, "entry")
sys.path.insert(0, os.path.dirname(__file__))
import patches
for a in patches.LIFT_SEEDS.get(os.path.basename(OUT).removesuffix(".json"), ()):
    add_func(a, "seed")
# A DLL's exports are entries too (callers live in other images). Exported data lies outside .text.
for s in getattr(getattr(pe, "DIRECTORY_ENTRY_EXPORT", None), "symbols", []):
    if not s.forwarder:
        add_func(base + s.address, "call")

# Code pointers in data (vtables, callbacks): dwords that land in .text.
data_ptrs = set()
for s in data_secs:
    raw = s.get_data()
    for o in range(0, len(raw) - 3, 4):
        v = struct.unpack_from("<I", raw, o)[0]
        if in_text(v):
            data_ptrs.add(v)

def drain():
    while work:
        walk(work.popleft())

drain()
reached_before_data = sum(sz for sz, _ in insn_at.values())
# A pointer to an instruction another flow already decoded is still an entry (MSVCP60 0x10901146: a C++ initializer
# in the __xc_a table that a tail jump reached first). build.py drops pointers into the middle of an instruction.
for v in sorted(data_ptrs):
    if v not in funcs:
        add_func(v, "data")
drain()

# Packed tables (e.g. the CRT math dispatch) hold code pointers at unaligned offsets. One counts if it hits
# an instruction start already decoded, or if the same value appears at least twice (random bytes rarely do).
unaligned = 0
seen_unaligned = collections.Counter()
for sec in data_secs:
    raw = sec.get_data()
    for o in range(len(raw) - 3):
        if o % 4:
            v = struct.unpack_from("<I", raw, o)[0]
            if in_text(v):
                seen_unaligned[v] += 1
for v, n in sorted(seen_unaligned.items()):
    if v not in funcs and (v in insn_at or n >= 2):
        add_func(v, "data")
        unaligned += 1
drain()

# Code pointers as instruction immediates (push offset f, mov reg/mem, offset f): callbacks, atexit, vtables.
imm_ptrs, scanned = set(), set()
while True:
    new = []
    for a in sorted(set(insn_at) - scanned):
        scanned.add(a)
        ins = next(md.disasm(bytes(img[a - base:a - base + 16]), a), None)
        if ins is None or ins.mnemonic not in ("push", "mov"):
            continue
        for op in ins.operands:
            if op.type == x86.X86_OP_IMM and in_text(op.imm & 0xffffffff) and op.size == 4:
                v = op.imm & 0xffffffff
                imm_ptrs.add(v)
                # An entry already decoded inside another flow still needs its own function (0x504010: a
                # destructor pushed for the CRT array iterator 0x672188).
                if v not in funcs:
                    new.append(v)
    if not new:
        break
    for v in new:
        add_func(v, "data")
    drain()

# Pointers that land inside an already-decoded instruction are not code starts.
mid = 0
starts = set(insn_at)
for v in data_ptrs:
    if v not in starts:
        mid += 1

covered = sum(sz for sz, _ in insn_at.values())
mn = collections.Counter(m for _, m in insn_at.values())
fpu = sum(c for m, c in mn.items() if m.startswith("f"))
by_dll = collections.Counter(n.split("!")[0] for n in iat.values())

res = {
    "exe": EXE, "image_base": hex(base), "text": [hex(t0), hex(t1)], "text_bytes": t1 - t0,
    "imports": {"total": len(iat), "by_dll": dict(by_dll), "names": sorted(iat.values())},
    "functions": {"total": len(funcs), "from_entry_and_calls": sum(1 for w in funcs.values() if w != "data"),
                  "from_data_pointers": sum(1 for w in funcs.values() if w == "data")},
    "instructions": len(insn_at), "distinct_mnemonics": len(mn),
    "bytes_decoded": covered, "text_coverage": round(covered / (t1 - t0), 4),
    "bytes_decoded_before_data_seeds": reached_before_data,
    "jump_tables": {"count": len(jump_tables), "targets": sum(len(t) for t in jump_tables.values())},
    "indirect_jmp_unresolved": len(indirect_jmp),
    "indirect_call": dict(indirect_call),
    "import_call_sites": sum(import_calls.values()),
    "imports_called": len(import_calls),
    "imports_never_called": sorted(set(iat.values()) - set(import_calls)),
    "x87_instructions": fpu,
    "groups": dict(groups.most_common()),
    "decode_failures": len(bad),
    "data_pointers_into_text": len(data_ptrs), "data_pointers_mid_instruction": mid,
    "immediate_pointers_into_text": len(imm_ptrs), "unaligned_pointers_to_code": unaligned,
    "top_mnemonics": dict(mn.most_common(40)),
    "all_mnemonics": sorted(mn),
    "unresolved_jmp_sites": [hex(a) for a in indirect_jmp],
}
with open(OUT, "w") as f:
    json.dump(res, f, indent=1)
with open(OUT.rsplit(".", 1)[0] + "_functions.tsv", "w") as f:
    for a in sorted(funcs):
        f.write(f"{a:08x}\t{funcs[a]}\n")
for k in ("text_bytes", "functions", "instructions", "distinct_mnemonics", "text_coverage", "jump_tables",
          "indirect_jmp_unresolved", "indirect_call", "import_call_sites", "imports_called", "x87_instructions",
          "decode_failures", "data_pointers_into_text", "data_pointers_mid_instruction", "immediate_pointers_into_text",
          "unaligned_pointers_to_code"):
    print(f"{k}: {res[k]}")
print("imports by dll:", res["imports"]["by_dll"])
print("groups:", {g: c for g, c in groups.most_common() if g not in ("jump", "call", "ret", "branch_relative", "int")})
