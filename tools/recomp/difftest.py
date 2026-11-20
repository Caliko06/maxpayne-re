#!/usr/bin/env python3
"""Check the lifted images against a reference x86 CPU (Unicorn), bit for bit.

Unicorn is a test oracle only; it never ships in the game.
Every lifted function runs on random inputs in both, with all the functions it calls. All guest images sit at their
bases (census/images.json) with every import resolved as the loader does; host imports point at fake addresses: both
stop at the first host call, the function's return, the first access outside the mapped regions, or the first jump out of
the images, and registers, MMX registers, x87 TOP, tags and st(0), and all memory are compared there. Uses build/gen from `build.py lift`.
Writes census/difftest.json. Usage: [RN_FUNCS=<hex,...>|@file] [RN_JOBS=n] [RN_SECS=limit] .venv/bin/python tools/recomp/difftest.py [cases]
"""
import collections, ctypes, json, math, mmap, os, pickle, random, re, signal, struct, subprocess, sys, time
from unicorn import Uc, UcError, UC_ARCH_X86, UC_MODE_32, UC_HOOK_INSN, UC_HOOK_CODE, UC_HOOK_BLOCK
from unicorn.x86_const import (UC_X86_REG_FPCW, UC_X86_REG_FPSW, UC_X86_REG_FPTAG, UC_X86_REG_FP0, UC_X86_REG_EAX,
                               UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EBX, UC_X86_REG_ESP, UC_X86_REG_EBP,
                               UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EIP, UC_X86_REG_EFLAGS, UC_X86_REG_CS, UC_X86_REG_SS, UC_X86_REG_FS, UC_X86_REG_GDTR,
                               UC_X86_INS_CPUID)
import pefile
sys.path.insert(0, os.path.dirname(__file__))
import patches

CASES = int(sys.argv[1]) if len(sys.argv) > 1 else 4
UREG = [UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EBX,
        UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI]
MAN = json.load(open("census/images.json"))
IMGS = [(n, int(m["base"], 16), -(-int(m["size"], 16) // 0x1000) * 0x1000, m["image"]) for n, m in MAN["images"].items()]
SCRATCH = int(MAN["heap_lo"], 16)        # random data and the stack sit where the guest heap starts
STACK = SCRATCH + 0x100000
STACK_TOP, RET = STACK + 0xf0000, STACK + 0xffff0
MEMSZ = STACK + 0x100000
REGIONS = [(b, sz) for _, b, sz, _ in IMGS] + [(SCRATCH, 0x100000), (STACK, 0x100000)]   # images, scratch, stack
TEB, GDT = SCRATCH + 0xf8000, SCRATCH + 0xff000      # above what val() points at; both CPUs see the same bytes
HOST = 0xe0000000
INDEF_MANT = 0xc000000000000000          # mantissa of the real indefinite: what an MMX register aliases on an empty x87 stack
GEN = "build/gen"
CHUNKS = sorted(f for f in os.listdir(GEN) if re.match(r".+_\d{3}\.c$", f))

# Own build of build/gen with RN_REF_QEMU5: the x87 exception flags as Unicorn (QEMU 5) sets them, not as the SDM.
os.makedirs("build/dobj", exist_ok=True)
jobs, objs = [], []
for f in CHUNKS + ["table.c"]:
    src, o = f"{GEN}/{f}", "build/dobj/" + f[:-2] + ".o"
    objs.append(o)
    if not os.path.exists(o) or os.path.getmtime(o) < max(os.path.getmtime(src), os.path.getmtime("host/rt.h")):
        jobs.append(["clang", "-O1", "-w", "-Ihost", f"-I{GEN}", "-fno-strict-aliasing", "-fPIC", "-DRN_REF_QEMU5", "-DRN_REF_MAP", "-c", src, "-o", o])
running = []
for j in jobs:
    while len(running) >= int(os.environ.get("RN_JOBS", os.cpu_count())):
        running.pop(0).wait()
    running.append(subprocess.Popen(j))
if any(p.wait() for p in running):
    sys.exit("compile failed")
subprocess.run(["clang", "-O1", "-shared", "-fPIC", "-w", "-Ihost", "-DRN_REF_QEMU5", "-DRN_REF_MAP", "-o", "build/difftest.so", *objs,
                "tools/recomp/difftest_rt.c", "host/cpu.c", "host/widescreen.c"], check=True)
lib = ctypes.CDLL(os.path.abspath("build/difftest.so"))


class Ctx(ctypes.Structure):     # host/rt.h
    _fields_ = [("r", ctypes.c_uint32 * 8), ("mem", ctypes.c_void_p), ("memsz", ctypes.c_uint32), ("ret", ctypes.c_uint32),
                ("halt", ctypes.c_uint32), ("halt_at", ctypes.c_uint32), ("why", ctypes.c_char_p), ("fs", ctypes.c_uint32),
                ("flags", ctypes.c_int32 * 7), ("eflx", ctypes.c_uint32), ("fcw", ctypes.c_uint32), ("fsw", ctypes.c_uint32),
                ("ftop", ctypes.c_uint32), ("ftag", ctypes.c_uint32), ("st", ctypes.c_double * 8),
                ("mb", ctypes.c_uint32), ("mm", ctypes.c_uint64 * 8)]


gap = (ctypes.c_ubyte * (1 << 20)).in_dll(lib, "rn_ref_unmapped")
ctypes.memset(gap, 1, 1 << 20)
for b, sz in REGIONS:
    ctypes.memset(ctypes.addressof(gap) + (b >> 12), 0, (sz + 0xfff) >> 12)
lib.rn_call.argtypes = [ctypes.POINTER(Ctx), ctypes.c_uint32]
lib.rn_call.restype = None
lib.rn_cpu_ident.argtypes = [ctypes.c_uint32, ctypes.c_uint32 * 4]
lib.rn_cpu_ident.restype = None
n = ctypes.c_uint32.in_dll(lib, "rn_fn_count").value
entries = list((ctypes.c_uint32 * n).in_dll(lib, "rn_fn_addr"))
seed = {}
for name, *_ in IMGS:
    seed.update({int(l.split()[0], 16): l.split()[1] for l in open(f"census/fn/{name}_functions.tsv")})
only = os.environ.get("RN_FUNCS")
if only:
    only = open(only[1:]).read() if only.startswith("@") else only
    want = {int(x, 16) for x in only.replace(",", " ").split()}
    entries = [e for e in entries if e in want]

# Every image at its base, patched, with its IAT filled as the loader fills it (build/gen/image_tables.c rn_slots):
# guest exports by address, host APIs at a fake host address each, the same in both.
images = []
slots = [(int(a, 16), int(v, 16)) for a, v in re.findall(r"\{0x([0-9a-f]+)u, 0x([0-9a-f]+)u, \"", open(f"{GEN}/image_tables.c").read())]
for name, base, size, path in IMGS:
    img = bytearray(pefile.PE(path).get_memory_mapped_image()[:size].ljust(size, b"\0"))
    patches.apply(img, base, size)
    images.append(img)
for k, (a, va) in enumerate(slots):
    j = next(j for j, (_, b, sz, _) in enumerate(IMGS) if b <= a < b + sz)
    o = a - IMGS[j][1]
    images[j][o:o + 4] = (va or HOST + 16 * k).to_bytes(4, "little")
images = [bytes(i) for i in images]


def in_image(a):
    return any(b <= a < b + sz for _, b, sz, _ in IMGS)


hostmem = ctypes.create_string_buffer(MEMSZ)
TEB_PAGE = bytes(0x18) + TEB.to_bytes(4, "little") + bytes(0x1000 - 0x1c)   # fs:[0] reads 0, fs:[0x18] the TEB itself


def seg_desc(base, access):     # 32-bit descriptor, limit 4 GB (Intel SDM vol. 3, 3.4.5)
    return (0xffff | (base & 0xffff) << 16 | (base >> 16 & 0xff) << 32 | access << 40 | 0xcf << 48 | (base >> 24 & 0xff) << 56).to_bytes(8, "little")


# Selectors 0x08 code, 0x10 stack, 0x18 fs. Loading fs alone makes Unicorn recompute its modes from a 16-bit SS, so all three are set.
GDT_BYTES = bytes(8) + seg_desc(0, 0x9b) + seg_desc(0, 0x93) + seg_desc(TEB, 0x93)
INDEF = struct.unpack("<d", struct.pack("<Q", 0xfff8000000000000))[0]
rng = random.Random(1)


def quiet_snans(b):
    """Random data without signaling-NaN floats/doubles: QEMU 5 keeps an sNaN on fld, the SDM quiets it."""
    buf = ctypes.create_string_buffer(b, len(b))
    lib.rn_quiet_snans(buf, len(b))
    return buf.raw


def val():
    p = rng.random()
    if p < 0.6:
        return SCRATCH + rng.randrange(0, 0xf0000) & ~3
    if p < 0.85:
        return rng.randrange(0, 64)
    return rng.getrandbits(32)


def f80(v):
    """Unicorn's (mantissa, exponent) pair of an 80-bit register as the bits of a double (NaN sign and payload kept)."""
    m, e = v
    if e & 0x7fff == 0x7fff:
        return (e >> 15) << 63 | 0x7ff << 52 | (m >> 11) & ((1 << 52) - 1)
    try:
        x = (-1) ** (e >> 15) * math.ldexp(m, (e & 0x7fff) - 16383 - 63)
    except OverflowError:       # beyond the double range: what a double would hold is infinity
        x = (-1) ** (e >> 15) * float("inf")
    return struct.unpack("<Q", struct.pack("<d", x))[0]


def dbits(x):
    return struct.unpack("<Q", struct.pack("<d", x))[0]


def on_cpuid(uc, _):
    out = (ctypes.c_uint32 * 4)()
    lib.rn_cpu_ident(uc.reg_read(UC_X86_REG_EAX), out)
    for r, v in zip((UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX), out):
        uc.reg_write(r, v)
    return 1


def on_rdtsc(uc, addr, size, _):     # same value as difftest_rt.c; Unicorn has no rdtsc hook, so skip the insn
    uc.reg_write(UC_X86_REG_EAX, 0x56789abc)
    uc.reg_write(UC_X86_REG_EDX, 0x1234)
    uc.reg_write(UC_X86_REG_EIP, addr + 2)


def wide_ret_sites():
    """`ret imm16` with imm >= 0x8000: Unicorn sign-extends the immediate, the SDM zero-extends it."""
    out = set()
    for f in CHUNKS:
        last = None
        for line in open(f"{GEN}/{f}"):
            if line.startswith("L") and line.rstrip().endswith(":;"):
                last = int(line[1:-3], 16)
            elif "c->ret=ld(c,esp,4); esp+=4+" in line and int(line.split("esp+=4+")[1].split(";")[0]) >= 0x8000:
                out.add(last)
    return out


def on_wide_ret(uc, addr, size, _):
    hit[0] = True


def rdtsc_sites():
    """Guest addresses of rdtsc: the label line before each rn_rdtsc call in build/gen."""
    out = set()
    for f in CHUNKS:
        last = None
        for line in open(f"{GEN}/{f}"):
            if line.startswith("L") and line.rstrip().endswith(":;"):
                last = int(line[1:-3], 16)
            elif "rn_rdtsc(" in line:
                out.add(last)
    return out


def on_left_images(u, addr, size, _):      # data run as code: the lifted side stops here with "no lifted function"
    left[0] = addr
    u.emu_stop()


def on_inexact(u, addr, size, _):
    inexact[0] = True


def inexact_sites():
    """x87 work the reference cannot judge. QEMU 5 computes fyl2x, f2xm1, fsin and the like through host doubles, and fprem by a
    long double loop, inexact or with a positive default NaN. fld m80 and the constants (fldpi...) hold 64 mantissa bits that st[] as
    double rounds at load. A mismatch in a case that ran one counts apart, not as a pass and not as a lifter fault."""
    pat = re.compile(r"\bfprem\(c|\bexp2\(|\blog2\(|\btan\(|\batan2\(|\bsin\(|\bcos\(|\bld80\(c|fpush\(c,[0-9]\.[0-9]{20}")
    out = set()
    for f in CHUNKS:
        last = None
        for line in open(f"{GEN}/{f}"):
            if line.startswith("L") and line.rstrip().endswith(":;"):
                last = int(line[1:-3], 16)
            elif pat.search(line):
                out.add(last)
    return out


def make_uc():
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    for b, sz in REGIONS:
        u.mem_map(b, sz)
    u.hook_add(UC_HOOK_BLOCK, on_left_images, None, SCRATCH, STACK + 0xf0000 - 1)   # below RET
    u.hook_add(UC_HOOK_INSN, on_cpuid, None, 1, 0, UC_X86_INS_CPUID)
    for a in RDTSC:
        u.hook_add(UC_HOOK_CODE, on_rdtsc, None, a, a)
    for a in WIDE_RET:
        u.hook_add(UC_HOOK_CODE, on_wide_ret, None, a, a)
    for a in INEXACT:
        u.hook_add(UC_HOOK_CODE, on_inexact, None, a, a)
    return u


RDTSC, WIDE_RET, INEXACT, hit, left, inexact = rdtsc_sites(), wide_ret_sites(), inexact_sites(), [False], [None], [False]
print(len(INEXACT), "x87 sites the reference cannot judge", file=sys.stderr)
uc = None

res, bad, compared, failed, stops = collections.Counter(), [], set(), set(), collections.Counter()
per, inexact_funcs = {}, set()          # entry -> skip kinds of its cases; entries with a case the reference cannot judge
ONLY = os.environ.get("RN_ONLY")      # one function, with its differing dwords printed
blocks, ustep, lstep = [], [], []
if ONLY:
    # Step trace: the entry function lifted again with a callback before each instruction, against Unicorn.
    entries = [int(ONLY, 16)]
    from lift import Lifter
    name = next(n for n, b, sz, _ in IMGS if b <= entries[0] < b + sz)
    LT = Lifter(MAN["images"][name]["image"], {e: k for e, k in seed.items() if in_image(e)})
    LT.trace = True
    code = LT.lift(entries[0], set(entries))
    code = code.replace(f"void f_{entries[0]:x}(Ctx *c)", f"void t_{entries[0]:x}(Ctx *c)", 1)
    open("build/gen/trace.c", "w").write('#include "rt.h"\nvoid (*rn_trace_cb)(Ctx *, uint32_t);\n'
                                         "void rn_trace_step(Ctx *c, uint32_t a) { rn_trace_cb(c, a); }\n" + code)
    subprocess.run(["clang", "-O1", "-shared", "-w", "-Ihost", "-fPIC", "-o", "build/trace.so", "build/gen/trace.c",
                    "build/difftest.so"], check=True)
    tlib = ctypes.CDLL(os.path.abspath("build/trace.so"))
    CB = ctypes.CFUNCTYPE(None, ctypes.POINTER(Ctx), ctypes.c_uint32)

    def l_step(cp, a):
        c = cp.contents
        lstep.append((a, tuple(c.r), c.ftop, f"{dbits(c.st[c.ftop]):016x}" if c.ftag >> c.ftop & 1 else "empty"))
    cb = CB(l_step)
    ctypes.c_void_p.in_dll(tlib, "rn_trace_cb").value = ctypes.cast(cb, ctypes.c_void_p).value
    tfn = getattr(tlib, f"t_{entries[0]:x}")
    tfn.argtypes = [ctypes.POINTER(Ctx)]

    def u_step(u, a, sz, _):
        if not (entries[0] <= a < entries[0] + 0x10000) or a not in LT.labels:
            return
        top = (u.reg_read(UC_X86_REG_FPSW) >> 11) & 7
        tag = (u.reg_read(UC_X86_REG_FPTAG) >> (2 * top)) & 3
        ustep.append((a, tuple(u.reg_read(r) for r in UREG), top, "empty" if tag == 3 else f"{f80(u.reg_read(UC_X86_REG_FP0 + top)):016x}"))

    def hook_only(u):
        u.hook_add(UC_HOOK_BLOCK, lambda u, a, sz, _: blocks.append(a))
        u.hook_add(UC_HOOK_CODE, u_step)
# RN_JOBS processes over slices of the entries. A child saves its results as it goes and publishes the function it is in. When one
# crashes or hangs, the parent notes that function and starts the child again from its last save, without it.
JOBS = 1 if ONLY else int(os.environ.get("RN_JOBS", 1))
PART = "build/dobj/part{}.pkl"
HANG = 120
kids, part, work, start, crashed = {}, None, entries, 0, {}
now_in = mmap.mmap(-1, 4 * max(JOBS, 1))


def save_part(done):
    with open(PART.format(part) + ".tmp", "wb") as f:
        pickle.dump((done, res, bad, compared, failed, stops, per, inexact_funcs), f)
    os.replace(PART.format(part) + ".tmp", PART.format(part))


def spawn(k):
    global part, work, start, res, bad, compared, failed, stops, per, inexact_funcs
    pid = os.fork()
    if pid:
        kids[pid] = [k, 0, time.time()]
        return False
    part, work = k, entries[k::JOBS]
    kids.clear()
    if os.path.exists(PART.format(k)):
        start, res, bad, compared, failed, stops, per, inexact_funcs = pickle.load(open(PART.format(k), "rb"))
    return True


if JOBS > 1:
    for k in range(JOBS):
        if os.path.exists(PART.format(k)):
            os.remove(PART.format(k))
    for k in range(JOBS):
        if spawn(k):
            break
    else:
        work = []
deadline = time.time() + int(os.environ.get("RN_SECS", 86400))
while True:         # the parent waits here; a child leaves it at once, and so does a parent with no children
    if part is None and kids:
        for pid, kid in list(kids.items()):
            k = kid[0]
            cur = struct.unpack_from("<I", now_in, 4 * k)[0]
            if cur != kid[1]:
                kid[1], kid[2] = cur, time.time()
            done, st = os.waitpid(pid, os.WNOHANG)
            why = None
            if done:
                del kids[pid]
                why = f"status {st}" if st else None
            elif time.time() > kid[2] + HANG or time.time() > deadline:
                os.kill(pid, signal.SIGKILL)
                os.waitpid(pid, 0)
                del kids[pid]
                why = "hang" if time.time() <= deadline else "RN_SECS"
            if why == "RN_SECS":
                print(f"job {k} stopped by RN_SECS", file=sys.stderr)
            elif why:
                crashed[cur] = why
                if spawn(k):
                    break
        if part is None:
            time.sleep(1)
            continue
    break
for done, e in enumerate(work[start:], start):
    if part is not None:
        if done % 100 == 0:
            save_part(done)
        struct.pack_into("<I", now_in, 4 * part, e)
    if e in crashed:
        continue
    before = res.copy()
    rng.seed(e)                     # same inputs for a function on every run
    for _ in range(CASES):
        regs = [val() for _ in range(8)]
        regs[4] = STACK_TOP
        scratch = ctypes.create_string_buffer(rng.randbytes(0x100000), 0x100000)
        lib.rn_mix_pointers(scratch, 0x80000, SCRATCH, 0xf0000)     # the upper half stays plain data (floats, strings)
        scratch = bytearray(quiet_snans(scratch.raw))
        scratch[TEB - SCRATCH:TEB - SCRATCH + 0x1000] = TEB_PAGE
        scratch[GDT - SCRATCH:GDT - SCRATCH + len(GDT_BYTES)] = GDT_BYTES
        scratch = bytes(scratch)
        stack = bytearray(0x100000)
        for j in range(16):
            o = STACK_TOP - STACK + 4 + 4 * j
            stack[o:o + 4] = val().to_bytes(4, "little")
        o = STACK_TOP - STACK
        stack[o:o + 4] = RET.to_bytes(4, "little")
        uc = make_uc()          # fresh CPU per case: junk entries run privileged code in Unicorn's ring 0
        if ONLY:
            hook_only(uc)
        for (b, sz), d in zip(REGIONS, images + [scratch, bytes(stack)]):
            uc.mem_write(b, d)
            ctypes.memmove(ctypes.addressof(hostmem) + b, d, sz)
        for r, v in zip(UREG, regs):
            uc.reg_write(r, v)
        uc.reg_write(UC_X86_REG_GDTR, (0, GDT, 0xff, 0))
        uc.reg_write(UC_X86_REG_CS, 0x08)
        uc.reg_write(UC_X86_REG_SS, 0x10)
        uc.reg_write(UC_X86_REG_FS, 0x18)
        uc.reg_write(UC_X86_REG_EFLAGS, 0x202)  # user mode: IF=1; arithmetic flags clear, DF=0, as the lifted code
        uc.reg_write(UC_X86_REG_FPCW, 0x27f)    # Windows thread default: 53-bit precision, exceptions masked
        uc.reg_write(UC_X86_REG_FPSW, 0)        # empty FPU stack every case (TOP 0)
        uc.reg_write(UC_X86_REG_FPTAG, 0xffff)
        for j in range(8):                      # empty registers read as the real indefinite (SDM; QEMU reads raw)
            uc.reg_write(UC_X86_REG_FP0 + j, (0xc000000000000000, 0xffff))
        ustop = "ret"
        hit[0], left[0], inexact[0] = False, None, False
        blocks.clear(); ustep.clear(); lstep.clear()
        try:
            uc.emu_start(e, RET, count=200000)
            if left[0] is not None:
                ustop = f"jump {left[0]:x}"
            elif uc.reg_read(UC_X86_REG_EIP) != RET:
                res["skip_timeout"] += 1
                continue
        except UcError as x:
            eip = uc.reg_read(UC_X86_REG_EIP)
            if eip >= HOST:
                ustop = f"import {eip:x}"
            elif "FETCH" in str(x):
                ustop = f"jump {eip:x}"
            elif "UNMAPPED" in str(x):
                ustop = f"mem {eip:x}"
            elif "INSN_INVALID" in str(x) or "Invalid instruction" in str(x):
                ustop = "invalid"
            else:
                ustop = "fault"
        if hit[0] or (ustop == "invalid" and uc.reg_read(UC_X86_REG_EIP) in WIDE_RET):
            res["skip_ret_imm16_sign"] += 1
            continue
        c = Ctx()
        for j, v in enumerate(regs):
            c.r[j] = v
        c.mem, c.memsz = ctypes.addressof(hostmem), MEMSZ
        c.fcw = 0x27f
        for j in range(8):
            c.st[j] = INDEF                         # empty x87 registers hold the real indefinite
            c.mm[j] = INDEF_MANT                    # and MMj is the mantissa of physical register j
        c.fs = TEB
        if ONLY:
            tfn(ctypes.byref(c))    # same code with the step callback
        else:
            lib.rn_call(ctypes.byref(c), e)
        why = (c.why or b"").decode()
        if not c.halt:
            if c.ret != RET:
                res["skip_not_a_function"] += 1     # ret went elsewhere: entry is mid-function or data
                continue
            lstop = "ret"
        elif why == "import":
            lstop = f"import {c.halt_at:x}"
        elif why.startswith("not lifted"):
            res["skip_pinned_cpu_path"] += 1        # MMX/SSE/3DNow: the CPU we present never runs it
            continue
        elif why == "memory fault" and ustop.startswith("mem"):
            lstop = f"mem {c.halt_at:x}"
        elif why == "no lifted function" and ustop.startswith("jump"):
            lstop = f"jump {c.halt_at:x}"
        elif why.startswith("return address") and ustop.startswith("jump"):
            lstop = f"jump {c.ret:x}"           # the callee's ret: the stack was overwritten, so it went where Unicorn went
        elif why in ("memory fault", "divide error"):
            lstop = "fault"
        elif why == "cannot decode":
            lstop = "invalid"
        else:
            lstop = why
        if ustop == lstop and ustop in ("fault", "invalid"):
            ok = True
        elif ustop != lstop:
            ok = False
        else:
            top = (uc.reg_read(UC_X86_REG_FPSW) >> 11) & 7
            ok = top == c.ftop and (top == 0 or f80(uc.reg_read(UC_X86_REG_FP0 + top)) == dbits(c.st[c.ftop]))
            tag = uc.reg_read(UC_X86_REG_FPTAG)
            # QEMU enters MMX mode before the load of an MMX instruction faults; a real CPU faults first.
            mmx_fault = ustop.startswith("mem") and ustop == lstop and (uc.reg_read(UC_X86_REG_FPTAG) == 0 and c.ftag == 0xff or c.ftag == 0) and \
                uc.mem_read(uc.reg_read(UC_X86_REG_EIP), 1) == b"\x0f"
            ok = ok and (mmx_fault or all((tag >> 2 * j & 3 != 3) == bool(c.ftag >> j & 1) for j in range(8)))
            # MMj aliases the mantissa of physical register j; exponent 0xffff marks one MMX wrote (or the initial indefinite).
            fp = [uc.reg_read(UC_X86_REG_FP0 + j) for j in range(8)]
            # An x87 -inf or negative NaN also has exponent 0xffff: valid it matches the lifted st[j], popped it is not state.
            ok = ok and all(fp[j][0] == c.mm[j] or f80(fp[j]) == dbits(c.st[j]) for j in range(8)
                            if fp[j][1] == 0xffff and tag >> 2 * j & 3 != 3)
            ok = ok and all(uc.reg_read(r) == c.r[j] for j, r in enumerate(UREG)) and all(
                bytes(uc.mem_read(b, sz)) == ctypes.string_at(ctypes.addressof(hostmem) + b, sz) for b, sz in REGIONS)
        if not ok and inexact[0]:
            res["reference_inexact"] += 1
            inexact_funcs.add(e)
            continue
        stops[ustop.split()[0]] += 1
        res["match" if ok else "mismatch"] += 1
        compared.add(e)
        if not ok and ONLY:
            print(f"case: unicorn {ustop}, lifted {lstop} {why} fcw {uc.reg_read(UC_X86_REG_FPCW):x} {c.fcw:x} tag {uc.reg_read(UC_X86_REG_FPTAG):x} {c.ftag:x}")
            print("  unicorn blocks:", " ".join(f"{a:x}" for a in blocks[-40:]))
            ut = (uc.reg_read(UC_X86_REG_FPSW) >> 11) & 7
            print(f"  end unicorn regs {[hex(uc.reg_read(r)) for r in UREG]} top {ut} st0 {f80(uc.reg_read(UC_X86_REG_FP0 + ut)):016x}")
            print(f"  end lifted  regs {[hex(x) for x in c.r]} top {c.ftop} st0 {dbits(c.st[c.ftop]):016x}")
            print(f"  raw st0 {uc.reg_read(UC_X86_REG_FP0 + ut)}")
            for k, (us, ls) in enumerate(zip(ustep, lstep)):
                if us != ls:
                    print(f"  first step that differs (state before it): unicorn {us}")
                    print(f"                                             lifted  {ls}")
                    print(f"  previous instruction: {ustep[k - 1][0]:x}" if k else "")
                    break
            for b, sz in REGIONS:
                u, h = bytes(uc.mem_read(b, sz)), ctypes.string_at(ctypes.addressof(hostmem) + b, sz)
                for a in [i for i in range(0, sz, 4) if u[i:i + 4] != h[i:i + 4]][:8]:
                    print(f"  {b + a:08x}: unicorn {u[a:a + 4][::-1].hex()} lifted {h[a:a + 4][::-1].hex()}")
        if not ok:
            failed.add(e)
            if len(bad) < 1000:
                bad.append({"func": hex(e), "unicorn": ustop, "lifted": lstop, "why": why, "at": hex(c.halt_at),
                            "uc": [hex(uc.reg_read(r)) for r in UREG], "lift": [hex(x) for x in c.r],
                            "mem_diff": [hex(b + next(i for i in range(sz) if u[i] != h[i])) for (b, sz) in REGIONS
                                         for u, h in [(bytes(uc.mem_read(b, sz)), ctypes.string_at(ctypes.addressof(hostmem) + b, sz))] if u != h]})
    per[e] = dict(res - before)
if part is not None:
    save_part(len(work))
    os._exit(0)
finished = len(work)
for k in range(JOBS if JOBS > 1 else 0):
    done, r2, b2, c2, f2, s2, p2, i2 = pickle.load(open(PART.format(k), "rb"))
    finished += done
    res.update(r2); stops.update(s2); bad += b2; compared |= c2; failed |= f2; per.update(p2); inexact_funcs |= i2

# Mismatches traced to a cause that is not a lifter fault. Any other mismatch fails the gate.
KNOWN = {}
# st[] is a double. msvcrt _hypot sets 64-bit precision; the complex power in msvcp60 keeps values below the double range,
# where the x87 still holds them (15-bit exponent) and a double has already gone to a signed zero.
KNOWN |= dict.fromkeys((0x1082a15c,), "x87 model: 64-bit precision")
KNOWN |= dict.fromkeys((0x10920d1f, 0x10922f1d, 0x10922fb4), "x87 model: exponent range")
# Frontend "data" seeds that land in strings or inside an instruction; nothing calls them.
KNOWN |= dict.fromkeys((0x430001, 0x6b0063, 0x740073, 0x7a00bf), "not code: a seed from data")
# A random pointer led a call to a non-entry address (inside an instruction, or .data at 0x8b41e4): Unicorn runs it as code.
KNOWN |= dict.fromkeys((0x1000ad70, 0x10444c86, 0x1061f310, 0x1064ac05), "random input called a non-entry")
never = {e: k for e, k in per.items() if e not in compared}
out = {"functions": len(entries), "functions_finished": finished, "crashed": {hex(e): w for e, w in crashed.items()}, "cases": dict(res),
       "reference_inexact_funcs": sorted(hex(e) for e in inexact_funcs),
       "never_compared": {hex(e): k for e, k in never.items()},
       "never_compared_by_kind": dict(collections.Counter(max(k, key=k.get) if k else "none" for k in never.values())),
       "compared_stop_kinds": dict(stops),
       "functions_compared": len(compared), "functions_all_match": len(compared - failed),
       "all_match_by_seed": dict(collections.Counter(seed.get(e, "?") for e in compared - failed)),
       "functions_with_mismatch": len(failed), "mismatch_funcs": sorted(hex(e) for e in failed), "mismatches": bad}
out["known_reference_deviations"] = {hex(k): v for k, v in KNOWN.items() if k in failed}
out["gate"] = "pass" if failed <= set(KNOWN) else "FAIL"
if not ONLY:
    json.dump(out, open("census/difftest.json", "w"), indent=1)
print(json.dumps({k: out[k] for k in ("gate", "functions", "functions_finished", "crashed", "never_compared_by_kind", "cases", "compared_stop_kinds", "functions_compared",
                                    "functions_all_match", "functions_with_mismatch")}))
print("mismatch funcs:", out["mismatch_funcs"][:30])
