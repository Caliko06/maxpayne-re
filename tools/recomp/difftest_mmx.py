#!/usr/bin/env python3
"""Every MMX instruction form the game uses, lifted alone and checked against Unicorn on edge-case lanes.

difftest.py runs whole functions on random inputs, which rarely reach deep MMX loops. Here each distinct form
(mnemonic, operand kinds, MMX registers, immediate) found in the lifted functions that use MMX is lifted as a
one-instruction function at its own address (followed by ret) in its own image, and run in both on values that hit
saturation, sign and shift-count edges. Registers, MMX registers (Unicorn: the mantissa of physical FPj), x87 TOP and
tags, and memory are compared. Unicorn is a test oracle only.
Usage: [RN_FUNCS=<hex,...>] .venv/bin/python tools/recomp/difftest_mmx.py [cases]
"""
import ctypes, json, os, random, re, subprocess, sys
from unicorn import Uc, UcError, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import (UC_X86_REG_FPCW, UC_X86_REG_FPSW, UC_X86_REG_FPTAG, UC_X86_REG_FP0, UC_X86_REG_EAX,
                               UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EBX, UC_X86_REG_ESP, UC_X86_REG_EBP,
                               UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EFLAGS, UC_X86_REG_EIP)
from capstone import x86
sys.path.insert(0, os.path.dirname(__file__))
from lift import Lifter, MMX_BIN

CASES = int(sys.argv[1]) if len(sys.argv) > 1 else 300
UREG = [UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EBX,
        UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI]
MAN = json.load(open("census/images.json"))
GEN = "build/gen"
SCRATCH = int(MAN["heap_lo"], 16)
STACK_TOP, RET = SCRATCH + 0x1f0000, SCRATCH + 0x1ffff0
MEMSZ = SCRATCH + 0x200000

if os.environ.get("RN_FUNCS"):
    funcs = {int(x, 16) for x in os.environ["RN_FUNCS"].split(",")}
else:                       # the lifted functions that contain MMX code
    funcs, cur = set(), None
    for f in sorted(os.listdir(GEN)):
        if re.match(r".+_\d{3}\.c$", f):
            for line in open(f"{GEN}/{f}"):
                m = re.match(r"void f_([0-9a-f]+)\(Ctx \*c\)", line)
                cur = int(m.group(1), 16) if m else cur
                if "mmx_enter(c)" in line or "c->ftag=0;" in line:
                    funcs.add(cur)


def is_mmx(i, m):           # lift.py Lifter.emit: what goes to Lifter.mmx
    return m == "emms" or ((m in MMX_BIN or m in ("movd", "movq")) and
                           any(x.type == x86.X86_OP_REG and i.reg_name(x.reg).startswith("mm") for x in i.operands))


def image_of(a):
    return next(n for n, m in MAN["images"].items() if int(m["base"], 16) <= a < int(m["base"], 16) + int(m["size"], 16))


# Distinct forms, the first site of each.
forms, lifters = {}, {}
for e in sorted(funcs):
    name = image_of(e)
    if name not in lifters:
        lifters[name] = Lifter(MAN["images"][name]["image"])
    L = lifters[name]
    for a, i in sorted(L.body(e)[0].items()):
        m = L.mnem(i)[1]
        if not is_mmx(i, m):
            continue
        kinds = tuple(i.reg_name(x.reg) if x.type == x86.X86_OP_REG and i.reg_name(x.reg).startswith("mm") else
                      ("r32" if x.type == x86.X86_OP_REG else "imm%d" % x.imm if x.type == x86.X86_OP_IMM else "m%d" % (8 * x.size))
                      for x in i.operands)
        forms.setdefault((m, kinds), (name, a, bytes(i.bytes)))
print(f"{len(funcs)} functions, {len(forms)} MMX forms")

# Each form as its own function: the instruction at its own address, then ret.
src = ['#include "rt.h"', "void rn_call(Ctx *c, uint32_t t) { c->halt = 1; c->halt_at = t; c->why = \"call\"; }",
       "uint64_t rn_rdtsc(Ctx *c) { (void)c; return 0; }"]
order = sorted(forms)
for k, key in enumerate(order):
    name, a, b = forms[key]
    L = lifters[name]
    o = a - L.base
    old = bytes(L.img[o:o + len(b) + 1])
    L.img[o + len(b)] = 0xc3
    L._cache.clear()
    src.append(L.lift(a, {a}).replace(f"f_{a:x}(Ctx *c)", f"s_{k}(Ctx *c)", 1))
    L.img[o:o + len(old)] = old
    L._cache.clear()
os.makedirs("build", exist_ok=True)
open("build/gen/mmxforms.c", "w").write("\n".join(src) + "\n")
subprocess.run(["clang", "-O1", "-shared", "-fPIC", "-w", "-Ihost", "-DRN_REF_QEMU5", "-o", "build/mmxforms.so",
                "build/gen/mmxforms.c", "host/cpu.c"], check=True)
lib = ctypes.CDLL(os.path.abspath("build/mmxforms.so"))


class Ctx(ctypes.Structure):     # host/rt.h
    _fields_ = [("r", ctypes.c_uint32 * 8), ("mem", ctypes.c_void_p), ("memsz", ctypes.c_uint32), ("ret", ctypes.c_uint32),
                ("halt", ctypes.c_uint32), ("halt_at", ctypes.c_uint32), ("why", ctypes.c_char_p), ("fs", ctypes.c_uint32),
                ("flags", ctypes.c_int32 * 7), ("eflx", ctypes.c_uint32), ("fcw", ctypes.c_uint32), ("fsw", ctypes.c_uint32),
                ("ftop", ctypes.c_uint32), ("ftag", ctypes.c_uint32), ("st", ctypes.c_double * 8),
                ("mb", ctypes.c_uint32), ("mm", ctypes.c_uint64 * 8)]


hostmem = ctypes.create_string_buffer(MEMSZ)
EDGE8 = [0, 1, 0x7f, 0x80, 0x81, 0xfe, 0xff]
EDGE16 = [0, 1, 0x7fff, 0x8000, 0x8001, 0xfffe, 0xffff, 0x00ff, 0xff00, 0x0100]
EDGE32 = [0, 1, 0x7fffffff, 0x80000000, 0x80000001, 0xffffffff, 0x0000ffff, 0xffff0000, 0x00007fff, 0xffff8000]


def lanes(rng):
    """A 64-bit value: random, edge lanes of 8/16/32 bits, or a small shift count."""
    p = rng.random()
    if p < 0.3:
        return rng.getrandbits(64)
    if p < 0.45:
        return rng.choice([rng.randrange(0, 80), 1 << rng.randrange(6, 40)])
    n, edge = rng.choice([(8, EDGE8), (16, EDGE16), (32, EDGE32)])
    return sum((rng.choice(edge) if rng.random() < 0.8 else rng.getrandbits(n)) << (k * n) for k in range(64 // n))


rng = random.Random(1)
bad, total = [], 0
for k, key in enumerate(order):
    name, a, b = forms[key]
    m = MAN["images"][name]
    base, size = int(m["base"], 16), -(-int(m["size"], 16) // 0x1000) * 0x1000
    img = bytearray(bytes(lifters[name].img).ljust(size, b"\0"))
    img[a - base + len(b)] = 0xc3
    regions = [(base, bytes(img)), (SCRATCH, None), (SCRATCH + 0x100000, None)]
    fn = getattr(lib, f"s_{k}")
    fn.argtypes = [ctypes.POINTER(Ctx)]
    fails = 0
    for case in range(CASES):
        scratch = rng.randbytes(0x100000)
        stack = bytearray(0x100000)
        o = STACK_TOP - SCRATCH - 0x100000
        stack[o:o + 4] = RET.to_bytes(4, "little")
        mm = [lanes(rng) for _ in range(8)]
        regs = [SCRATCH + 0x40000 + rng.randrange(0, 0x80000) & ~rng.choice([0, 3, 7]) for _ in range(8)]
        regs[4] = STACK_TOP
        for j in (0, 1, 2, 3):                      # a GPR also feeds movd mm, r32 (pointers stay pointers for m32/m64)
            if not any(x.startswith("m") and not x.startswith("mm") for x in key[1]) and rng.random() < 0.3:
                regs[j] = lanes(rng) & 0xffffffff
        data = [regions[0][1], scratch, bytes(stack)]
        uc = Uc(UC_ARCH_X86, UC_MODE_32)
        for (rb, _), d in zip(regions, data):
            uc.mem_map(rb, len(d))
            uc.mem_write(rb, d)
            ctypes.memmove(ctypes.addressof(hostmem) + rb, d, len(d))
        for r, v in zip(UREG, regs):
            uc.reg_write(r, v)
        uc.reg_write(UC_X86_REG_EFLAGS, 0x202)
        uc.reg_write(UC_X86_REG_FPCW, 0x27f)
        uc.reg_write(UC_X86_REG_FPSW, 0)
        uc.reg_write(UC_X86_REG_FPTAG, 0xffff)
        for j in range(8):                          # MMj = mantissa of physical register j
            uc.reg_write(UC_X86_REG_FP0 + j, (mm[j], 0xffff))
        try:
            uc.emu_start(a, RET, count=10)
            ustop = "ret" if uc.reg_read(UC_X86_REG_EIP) == RET else "timeout"
        except UcError as x:
            ustop = "fault"
        c = Ctx()
        for j in range(8):
            c.r[j], c.mm[j] = regs[j], mm[j]
        c.mem, c.memsz, c.fcw = ctypes.addressof(hostmem), MEMSZ, 0x27f
        fn(ctypes.byref(c))
        lstop = "fault" if c.halt else ("ret" if c.ret == RET else "other")
        total += 1
        ok = ustop == lstop
        if ok and ustop == "ret":
            fp = [uc.reg_read(UC_X86_REG_FP0 + j) for j in range(8)]
            tag = uc.reg_read(UC_X86_REG_FPTAG)
            top = (uc.reg_read(UC_X86_REG_FPSW) >> 11) & 7
            diffs = [f"r{j} {uc.reg_read(r):08x}/{c.r[j]:08x}" for j, r in enumerate(UREG) if uc.reg_read(r) != c.r[j]]
            diffs += [f"mm{j} {fp[j][0]:016x}/{c.mm[j]:016x}" for j in range(8) if fp[j][0] != c.mm[j]]
            diffs += [f"top {top}/{c.ftop}"] if top != c.ftop else []
            diffs += [f"tag {tag:04x}/{c.ftag:02x}"] if any((tag >> 2 * j & 3 != 3) != bool(c.ftag >> j & 1) for j in range(8)) else []
            for rb, d in zip(regions, data):
                u, h = bytes(uc.mem_read(rb[0], len(d))), ctypes.string_at(ctypes.addressof(hostmem) + rb[0], len(d))
                if u != h:
                    o = next(i for i in range(len(u)) if u[i] != h[i])
                    diffs.append(f"mem {rb[0] + o:08x}")
            ok = not diffs
        if not ok:
            fails += 1
            if fails <= 2:
                bad.append({"form": f"{key[0]} {','.join(key[1])}", "site": hex(a), "bytes": b.hex(),
                            "mm_in": [f"{x:016x}" for x in mm], "regs_in": [f"{x:08x}" for x in regs],
                            "stop": f"{ustop}/{lstop}", "diff (unicorn/lifted)": diffs if ustop == lstop else []})
    print(f"{'ok  ' if not fails else 'FAIL'} {key[0]:10} {','.join(key[1]):24} {hex(a)} {fails}/{CASES}")
json.dump({"forms": len(forms), "cases": total, "failing_forms": sorted({x["form"] for x in bad}), "examples": bad},
          open("census/difftest_mmx.json", "w"), indent=1)
print(f"forms {len(forms)}, cases {total}, failing forms {len({x['form'] for x in bad})}")
