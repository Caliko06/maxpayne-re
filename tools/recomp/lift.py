#!/usr/bin/env python3
"""Lift x86-32 functions of one guest image (build/img, tools/recomp/images.py) to C.

Each guest function becomes `void f_<addr>(Ctx *c)` (host/rt.h). Registers and flags are C locals,
written back to the Ctx at calls and exits. Direct calls are C calls; indirect calls and jumps go
through rn_call (address -> function). An instruction the lifter does not know becomes a TRAP that
stops the game with its address and name when it runs. Nothing is guessed.
"""
import bisect
import os
import struct
import pefile
import patches
from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_GRP_JUMP, CS_GRP_CALL, CS_GRP_RET
from capstone import x86

REG32 = ["eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"]
LO8 = {"al": "eax", "cl": "ecx", "dl": "edx", "bl": "ebx"}
HI8 = {"ah": "eax", "ch": "ecx", "dh": "edx", "bh": "ebx"}
R16 = {"ax": "eax", "cx": "ecx", "dx": "edx", "bx": "ebx", "sp": "esp", "bp": "ebp", "si": "esi", "di": "edi"}
FLAGS = ["cf", "zf", "sf", "of", "pf", "df", "af"]

COND = {"o": "of", "no": "!of", "b": "cf", "ae": "!cf", "e": "zf", "ne": "!zf", "be": "(cf||zf)",
        "a": "(!cf&&!zf)", "s": "sf", "ns": "!sf", "l": "(sf!=of)", "ge": "(sf==of)",
        "le": "(zf||sf!=of)", "g": "(!zf&&sf==of)", "p": "pf", "np": "!pf"}
PREFIXES = ("lock ", "rep ", "repe ", "repne ", "bnd ", "notrack ")
STRING_OPS = {0xa4: "movs", 0xa5: "movs", 0xa6: "cmps", 0xa7: "cmps", 0xaa: "stos", 0xab: "stos",
              0xac: "lods", 0xad: "lods", 0xae: "scas", 0xaf: "scas"}
FPU_CONST = {0xe8: "1.0", 0xe9: "3.321928094887362347870319429489390175864831393",
             0xea: "1.442695040888963407359924681001892137426645954", 0xeb: "3.141592653589793238462643383279502884197",
             0xec: "0.301029995663981195213738894724493026768189881", 0xed: "0.693147180559945309417232121458176568075500134",
             0xee: "0.0"}

PROLOGUE = '#include "rt.h"\n'
MMX_BIN = {"paddb", "paddw", "paddd", "psubb", "psubw", "psubd", "paddsb", "paddsw", "psubsb", "psubsw", "paddusb",
           "paddusw", "psubusb", "psubusw", "pmullw", "pmulhw", "pmaddwd", "pcmpeqb", "pcmpeqw", "pcmpeqd", "pcmpgtb",
           "pcmpgtw", "pcmpgtd", "pand", "pandn", "por", "pxor", "psllw", "pslld", "psllq", "psrlw", "psrld", "psrlq",
           "psraw", "psrad", "punpcklbw", "punpckhbw", "punpcklwd", "punpckhwd", "punpckldq", "punpckhdq",
           "packsswb", "packuswb", "packssdw"}


class Unsupported(Exception):
    pass


PROBES = {int(x, 16) for x in os.environ.get("RN_LIFT_PROBES", "").split(",") if x}


class Lifter:
    def __init__(self, exe, funcs=None):
        self.pe = pefile.PE(exe)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.img = bytearray(self.pe.get_memory_mapped_image())
        patches.apply(self.img, self.base, self.pe.OPTIONAL_HEADER.SizeOfImage)
        t = next(s for s in self.pe.sections if s.Name.rstrip(b"\0") == b".text")
        al = self.pe.OPTIONAL_HEADER.SectionAlignment      # mapped like the loader does (frontend.py mapped)
        size = max(t.Misc_VirtualSize, min(t.SizeOfRawData, -(-t.Misc_VirtualSize // al) * al))
        self.t0, self.t1 = self.base + t.VirtualAddress, self.base + t.VirtualAddress + size
        self.md = Cs(CS_ARCH_X86, CS_MODE_32)
        self.md.detail = True
        self.funcs = funcs or {}          # entry -> "call" | "data" | "entry"
        self.iat = set()
        for e in self.pe.DIRECTORY_ENTRY_IMPORT:
            for i in e.imports:
                self.iat.add(i.address)
        self._cache = {}
        self.trace = False
        # Address-taken code: dwords in the data sections that point into .text (jump tables, vtables).
        self.addr_taken, self.addr_taken_aligned = set(), set()
        for sec in self.pe.sections:
            if sec.Name.rstrip(b"\0") == b".text":
                continue
            raw = sec.get_data()
            for o in range(len(raw) - 3):       # every offset: packed tables exist; labels filter the noise
                v = struct.unpack_from("<I", raw, o)[0]
                if self.t0 <= v < self.t1:
                    self.addr_taken.add(v)
                    if o % 4 == 0:
                        self.addr_taken_aligned.add(v)

    def u32(self, a):
        return struct.unpack_from("<I", self.img, a - self.base)[0]

    def u8(self, a):
        return self.img[a - self.base]

    def in_image(self, a):
        return self.base <= a < self.base + len(self.img)

    def insn(self, a):
        if a in self._cache:
            return self._cache[a]
        if not (self.t0 <= a < self.t1):
            return None
        o = a - self.base
        i = next(self.md.disasm(bytes(self.img[o:o + 16]), a), None)
        if len(self._cache) > 400000:
            self._cache.clear()
        self._cache[a] = i
        return i

    def entries_after(self, a):
        """Real function entries above a, nearest first."""
        if not hasattr(self, "_real"):
            import bisect
            self._real = sorted(e for e, k in self.funcs.items() if k in ("call", "entry"))
            self._bisect = bisect.bisect_right
        k = self._bisect(self._real, a)
        return iter(self._real[k:k + 1])

    def is_func(self, a):
        """A real function entry: a jump there is a tail call, not a local branch."""
        return self.funcs.get(a) in ("call", "entry")

    # --- body discovery ---------------------------------------------------
    def body(self, entry, limit=60000):
        """Instructions of one function by local recursive descent. Returns (insns, switch_targets)."""
        seen, todo, switches, bad = {}, [entry], {}, set()
        labels_added = False
        while todo:
            a = todo.pop()
            last_cmp = {}
            while a not in seen and a not in bad:
                if len(seen) > limit:
                    raise Unsupported("function too big")
                i = self.insn(a)
                if i is None:
                    bad.add(a)
                    break
                seen[a] = i
                m = self.mnem(i)[1]
                op = i.operands[0] if i.operands else None
                # Jump table bounds: the largest index a register can hold when the jmp uses it.
                imm1 = len(i.operands) > 1 and i.operands[1].type == x86.X86_OP_IMM
                if m == "cmp" and op.type == x86.X86_OP_REG and imm1:
                    last_cmp[i.reg_name(op.reg)] = i.operands[1].imm
                elif m == "movzx" and op.type == x86.X86_OP_REG and i.operands[1].type == x86.X86_OP_MEM:
                    mm = i.operands[1].mem
                    src = i.reg_name(mm.base) if mm.base else None
                    last_cmp.pop(i.reg_name(op.reg), None)
                    if i.operands[1].size == 1 and src in last_cmp and not mm.index:
                        n = last_cmp[src] + 1
                        if self.in_image(mm.disp) and self.in_image(mm.disp + n):
                            last_cmp[i.reg_name(op.reg)] = max(self.u8(mm.disp + k) for k in range(n))
                elif m == "and" and op.type == x86.X86_OP_REG and imm1 and (i.operands[1].imm & (i.operands[1].imm + 1)) == 0:
                    last_cmp[i.reg_name(op.reg)] = i.operands[1].imm
                elif m == "neg" and op.type == x86.X86_OP_REG and i.reg_name(op.reg) in last_cmp:
                    r = i.reg_name(op.reg)       # msvcrt memcpy 0x10805849: neg ecx; jmp [ecx*4+end] reads the table backward
                    last_cmp["-" + r] = last_cmp.pop(r)
                elif op is not None and op.type == x86.X86_OP_REG and m not in ("cmp", "test", "push") and not m.startswith("j"):
                    r = i.reg_name(op.reg)
                    r = {**LO8, **HI8, **R16}.get(r, r)
                    last_cmp.pop(r, None)
                    last_cmp.pop("-" + r, None)
                if m in ("call", "mul", "div", "imul", "idiv", "cdq", "lodsb", "lodsd"):
                    last_cmp.clear()
                if i.group(CS_GRP_RET) or m in ("hlt", "int3", "ud2", "iretd", "retf", "int", "int1", "into"):
                    break
                if i.group(CS_GRP_JUMP) or m in ("loop", "loope", "loopne", "jecxz"):
                    if op.type == x86.X86_OP_IMM:
                        t = op.imm
                        tail = t != entry and self.is_func(t)
                        if not tail:
                            todo.append(t)
                        if m == "jmp":
                            break
                    else:
                        mm = op.mem if op.type == x86.X86_OP_MEM else None
                        if mm and not mm.base and not mm.index and mm.disp in self.iat:
                            break           # jmp [IAT]: an import tail call (thunk), not a switch
                        # Code pointers in data between this entry and the next real function are its labels.
                        if not labels_added:        # the same range for every indirect jmp of this body
                            labels_added = True
                            nxt = next((f for f in self.entries_after(entry)), self.t1)
                            if not hasattr(self, "_ata"):
                                self._ata = sorted(self.addr_taken_aligned)
                            k0, k1 = bisect.bisect_right(self._ata, entry), bisect.bisect_left(self._ata, nxt)
                            todo.extend(t for t in self._ata[k0:k1] if not self.is_func(t))
                        if mm and not mm.base and mm.index and mm.scale == 4:
                            r = i.reg_name(mm.index)
                            n = last_cmp.get(r)
                            if n is None and "-" + r in last_cmp:      # index in [-bound, 0]
                                tg = self.table(mm.disp - 4 * last_cmp["-" + r], last_cmp["-" + r] + 1)
                            else:
                                tg = self.table(mm.disp, None if n is None else n + 1)
                            switches[a] = tg
                            todo.extend(t for t in tg if not (t != entry and self.is_func(t)))
                        break
                a = i.address + i.size
        return seen, switches, bad

    def table(self, at, n):
        """Targets of a jump table. With a known bound, entries that are not code are skipped (a table base
        may overlap code when index 0 is never used); without one, read until the first non-code dword."""
        out = []
        for k in range(n if n is not None else 1024):
            if not self.in_image(at + 4 * k + 3):
                break
            v = self.u32(at + 4 * k)
            if self.t0 <= v < self.t1:
                out.append(v)
            elif n is None:
                break
        return out

    # --- operand helpers --------------------------------------------------
    @staticmethod
    def mnem(i):
        m, pre = i.mnemonic, ""
        for p in PREFIXES:
            if m.startswith(p):
                pre, m = p.strip(), m[len(p):]
        return pre, m

    def ea(self, mem):
        parts = []
        if mem.segment == x86.X86_REG_FS:
            parts.append("c->fs")
        elif mem.segment not in (0, x86.X86_REG_DS, x86.X86_REG_SS, x86.X86_REG_ES, x86.X86_REG_CS):
            raise Unsupported(f"segment {self.i.reg_name(mem.segment)}")
        if mem.base:
            parts.append(self.r32(mem.base))
        if mem.index:
            parts.append(f"{self.r32(mem.index)}*{mem.scale}u")
        parts.append(f"0x{mem.disp & 0xffffffff:x}u")
        return "(uint32_t)(" + "+".join(parts) + ")"

    def r32(self, reg):
        n = self.i.reg_name(reg)
        if n not in REG32:
            raise Unsupported(f"reg {n}")
        return n

    def at(self):
        return f"0x{self.i.address:x}"

    def addr(self, op):
        if op.size not in (1, 2, 4):
            raise Unsupported(f"{op.size}-byte operand")     # far pointers (m16:32), m64...
        self.out.append(f"ea={self.ea(op.mem)}; CHK(ea,{op.size},{self.at()});")

    def read(self, op, size=None):
        if op.type == x86.X86_OP_IMM:
            s = size or op.size
            return f"0x{op.imm & ((1 << (8 * s)) - 1):x}u"
        if op.type == x86.X86_OP_REG:
            n = self.i.reg_name(op.reg)
            if n in REG32:
                return n
            if n in R16:
                return f"({R16[n]}&0xffffu)"
            if n in LO8:
                return f"({LO8[n]}&0xffu)"
            if n in HI8:
                return f"(({HI8[n]}>>8)&0xffu)"
            raise Unsupported(f"reg {n}")
        self.addr(op)
        return f"ld(c,ea,{op.size})"

    def write(self, op, v):
        if op.type == x86.X86_OP_REG:
            n = self.i.reg_name(op.reg)
            if n in REG32:
                return f"{n}=(uint32_t)({v});"
            if n in R16:
                r = R16[n]
                return f"{r}=({r}&0xffff0000u)|(({v})&0xffffu);"
            if n in LO8:
                r = LO8[n]
                return f"{r}=({r}&0xffffff00u)|(({v})&0xffu);"
            if n in HI8:
                r = HI8[n]
                return f"{r}=({r}&0xffff00ffu)|((({v})&0xffu)<<8);"
            raise Unsupported(f"reg {n}")
        return f"st(c,ea,(uint32_t)({v}),{op.size});"      # ea from the matching read() or addr()

    def mem_dst(self, op):
        if op.type == x86.X86_OP_MEM:
            self.addr(op)

    @staticmethod
    def zsp(M, S):
        return f"zf=(res&0x{M:x}u)==0; sf=(res&0x{S:x}u)!=0; pf=par(res);"

    def arith(self, m, M, S):
        if m in ("add", "xadd"):
            return f"res=(t1+t2)&0x{M:x}u; af=((t1^t2^res)>>4)&1; cf=((uint64_t)t1+t2)>0x{M:x}u; of=((~(t1^t2))&(t1^res)&0x{S:x}u)!=0; " + self.zsp(M, S)
        if m == "adc":
            return f"res=(t1+t2+cf)&0x{M:x}u; af=((t1^t2^res)>>4)&1; cf=((uint64_t)t1+t2+cf)>0x{M:x}u; of=((~(t1^t2))&(t1^res)&0x{S:x}u)!=0; " + self.zsp(M, S)
        if m in ("sub", "cmp"):
            return f"res=(t1-t2)&0x{M:x}u; af=((t1^t2^res)>>4)&1; cf=t1<t2; of=((t1^t2)&(t1^res)&0x{S:x}u)!=0; " + self.zsp(M, S)
        if m == "sbb":
            return f"res=(t1-t2-cf)&0x{M:x}u; af=((t1^t2^res)>>4)&1; cf=(uint64_t)t1<(uint64_t)t2+cf; of=((t1^t2)&(t1^res)&0x{S:x}u)!=0; " + self.zsp(M, S)
        opc = {"and": "&", "test": "&", "or": "|", "xor": "^"}[m]
        return f"res=(t1{opc}t2)&0x{M:x}u; cf=0; of=0; af=0; " + self.zsp(M, S)

    # --- control transfer -------------------------------------------------
    def save(self):
        return " ".join(f"c->r[{k}]={r};" for k, r in enumerate(REG32)) + " " + " ".join(f"c->{f}={f};" for f in FLAGS)

    def load(self):
        return " ".join(f"{r}=c->r[{k}];" for k, r in enumerate(REG32)) + " " + " ".join(f"{f}=c->{f};" for f in FLAGS)

    def call_to(self, target_c, direct=None):
        """Call a guest/host address. target_c is a C expression already computed into t1 if indirect."""
        nxt = self.i.address + self.i.size
        o = self.out
        o.append(f"CHK(esp-4,4,{self.at()}); esp-=4; st(c,esp,0x{nxt:x}u,4); {self.save()}")
        o.append(f"f_{direct:x}(c);" if direct is not None else f"rn_call(c,{target_c});")
        o.append(f"{self.load()} if (c->halt) return; if (c->ret!=0x{nxt:x}u) {{ if (c->uw) return; TRAP({self.at()},\"return address changed\"); }}")

    def tail(self, target_c, direct=None):
        call = f"f_{direct:x}(c);" if direct is not None else f"rn_call(c,{target_c});"
        return f"{{ {self.save()} {call} return; }}"

    def jump(self, t, cond=None):
        if t in self.labels:
            s = f"goto L{t:x};"
        elif t in self.lifted:
            s = self.tail(None, t)
        else:
            s = self.tail(f"0x{t:x}u")
        return f"if ({cond}) {s}" if cond else s

    # --- one instruction --------------------------------------------------
    def emit(self, i):
        self.prev, self.i = getattr(self, "i", None), i
        pre, m = self.mnem(i)
        ops = i.operands
        o = self.out
        op0 = i.opcode[0]
        if 0xd8 <= op0 <= 0xdf:
            return self.x87(i)
        if m == "emms" or ((m in MMX_BIN or m in ("movd", "movq")) and
                           any(x.type == x86.X86_OP_REG and i.reg_name(x.reg).startswith("mm") for x in ops)):
            return self.mmx(i, m)
        if m in ("wait", "nop", "fwait") or (m == "pause"):
            return
        sz = ops[0].size if ops else 4
        M = (1 << (8 * sz)) - 1
        S = 1 << (8 * sz - 1)
        ZSP = self.zsp(M, S)

        if op0 in STRING_OPS and len(i.opcode) and i.opcode[1] == 0:
            return self.string(i, pre, STRING_OPS[op0], 1 if op0 & 1 == 0 else (2 if i.prefix[2] == 0x66 else 4))
        if m in ("mov", "movzx"):
            for x in ops:
                if x.type == x86.X86_OP_REG and i.reg_name(x.reg) not in (*REG32, *R16, *LO8, *HI8):
                    raise Unsupported(f"mov {i.reg_name(x.reg)}")
            v = self.read(ops[1], sz)
            self.mem_dst(ops[0])
            o.append(self.write(ops[0], v))
            return
        if m == "movsx":
            s2 = ops[1].size
            v = self.read(ops[1])
            o.append(self.write(ops[0], f"(uint32_t)(int32_t)(int{8*s2}_t)({v})"))
            return
        if m == "lea":
            o.append(self.write(ops[0], self.ea(ops[1].mem)))
            return
        if m in ("add", "sub", "adc", "sbb", "and", "or", "xor", "cmp", "test"):
            b = self.read(ops[1], sz)
            o.append(f"t2={b};")
            a = self.read(ops[0])
            o.append(f"t1={a}; " + self.arith(m, M, S))
            if m not in ("cmp", "test"):
                o.append(self.write(ops[0], "res"))
            return
        if m in ("inc", "dec", "neg", "not"):
            a = self.read(ops[0])
            o.append(f"t1={a};")
            if m == "inc":
                o.append(f"res=(t1+1)&0x{M:x}u; of=res==0x{S:x}u; af=((t1^res)>>4)&1; {ZSP}")
            elif m == "dec":
                o.append(f"res=(t1-1)&0x{M:x}u; of=t1==0x{S:x}u; af=((t1^res)>>4)&1; {ZSP}")
            elif m == "neg":
                o.append(f"res=(0u-t1)&0x{M:x}u; cf=t1!=0; of=t1==0x{S:x}u; af=((t1^res)>>4)&1; {ZSP}")
            else:
                o.append(f"res=(~t1)&0x{M:x}u;")
            o.append(self.write(ops[0], "res"))
            return
        if m in ("shl", "sal", "shr", "sar", "rol", "ror", "rcl", "rcr"):
            n = self.read(ops[1]) if len(ops) > 1 else "1u"
            o.append(f"n=({n})&31u;")
            a = self.read(ops[0])
            o.append(f"t1={a};")
            bits = 8 * sz
            o.append("if (n) {")
            if m in ("shl", "sal"):
                o.append(f"res=(uint32_t)(((uint64_t)t1<<n)&0x{M:x}u); cf=n<={bits}?(int)((((uint64_t)t1)>>({bits}-n))&1):0; of=((res&0x{S:x}u)!=0)^cf; af=0; {ZSP}")
            elif m == "shr":
                o.append(f"res=(uint32_t)((uint64_t)t1>>n); cf=(int)(((uint64_t)t1>>(n-1))&1); of=(t1&0x{S:x}u)!=0; af=0; {ZSP}")
            elif m == "sar":
                o.append(f"res=(uint32_t)(((int64_t)(int{bits}_t)t1)>>n)&0x{M:x}u; cf=(int)((((int64_t)(int{bits}_t)t1)>>(n-1))&1); of=0; af=0; {ZSP}")
            elif m == "rol":
                o.append(f"k=n%{bits}; res=k?((t1<<k)|(t1>>({bits}-k)))&0x{M:x}u:t1; cf=res&1; of=((res&0x{S:x}u)!=0)^cf;")
            elif m == "ror":
                o.append(f"k=n%{bits}; res=k?((t1>>k)|(t1<<({bits}-k)))&0x{M:x}u:t1; cf=(res&0x{S:x}u)!=0; of=((res^(res<<1))&0x{S:x}u)!=0;")
            elif m == "rcl":
                o.append(f"res=t1; for (k=n%{bits + 1}; k; k--) {{ uint32_t hi=(res&0x{S:x}u)!=0; res=((res<<1)|cf)&0x{M:x}u; cf=hi; }} of=((res&0x{S:x}u)!=0)^cf;")
            else:
                o.append(f"of=((t1&0x{S:x}u)!=0)^cf; res=t1; for (k=n%{bits + 1}; k; k--) {{ uint32_t lo=res&1; res=(res>>1)|(cf?0x{S:x}u:0); cf=lo; }}")
            o.append(self.write(ops[0], "res"))
            o.append("}")
            return
        if m in ("shld", "shrd"):
            if sz != 4:
                raise Unsupported(f"{m}16")
            n = self.read(ops[2])
            o.append(f"n=({n})&31u; t2={self.read(ops[1])};")
            a = self.read(ops[0])
            o.append(f"t1={a}; if (n) {{")
            if m == "shld":
                o.append(f"res=(t1<<n)|(t2>>(32-n)); cf=(t1>>(32-n))&1; of=((res^t1)&0x80000000u)!=0; {ZSP}")
            else:
                o.append(f"res=(t1>>n)|(t2<<(32-n)); cf=(t1>>(n-1))&1; of=((res^t1)&0x80000000u)!=0; {ZSP}")
            o.append(self.write(ops[0], "res"))
            o.append("}")
            return
        if m in ("bt", "bts", "btr", "btc"):
            bits = 8 * sz
            if ops[0].type == x86.X86_OP_MEM and ops[1].type == x86.X86_OP_REG:
                sh = 5 if sz == 4 else 4
                o.append(f"t2={self.read(ops[1])}; ea={self.ea(ops[0].mem)}+(uint32_t)(((int{bits}_t)t2>>{sh})*{sz}); CHK(ea,{sz},{self.at()}); t1=ld(c,ea,{sz}); n=t2&{bits - 1}u;")
                dst = f"st(c,ea,res,{sz});"
            else:
                o.append(f"n=({self.read(ops[1])})&{bits - 1}u;")
                o.append(f"t1={self.read(ops[0])};")
                dst = self.write(ops[0], "res")
            o.append("cf=(t1>>n)&1;")
            if m != "bt":
                expr = {"bts": "t1|(1u<<n)", "btr": "t1&~(1u<<n)", "btc": "t1^(1u<<n)"}[m]
                o.append(f"res={expr}; {dst}")
            return
        if m == "imul" and len(ops) >= 2:
            b = self.read(ops[2], sz) if len(ops) == 3 else self.read(ops[1])
            o.append(f"t2={b};")
            a = self.read(ops[1]) if len(ops) == 3 else self.read(ops[0])
            bits = 8 * sz
            o.append(f"w=(int64_t)(int{bits}_t)({a})*(int64_t)(int{bits}_t)t2; cf=of=(w!=(int64_t)(int{bits}_t)w);")
            o.append(self.write(ops[0], "(uint32_t)w"))
            return
        if m in ("mul", "imul", "div", "idiv"):
            v = self.read(ops[0])
            o.append(f"t1={v};")
            at = self.at()
            if sz == 4:
                o.append({
                    "mul": "u=(uint64_t)eax*t1; eax=(uint32_t)u; edx=(uint32_t)(u>>32); cf=of=edx!=0;",
                    "imul": "w=(int64_t)(int32_t)eax*(int32_t)t1; eax=(uint32_t)w; edx=(uint32_t)((uint64_t)w>>32); cf=of=(w!=(int64_t)(int32_t)w);",
                    "div": f"u=((uint64_t)edx<<32)|eax; if (!t1 || u/t1>0xffffffffu) TRAP({at},\"divide error\"); eax=(uint32_t)(u/t1); edx=(uint32_t)(u%t1);",
                    "idiv": f"w=(int64_t)(((uint64_t)edx<<32)|eax); if (!t1 || (w==INT64_MIN && (int32_t)t1==-1)) TRAP({at},\"divide error\"); {{ int64_t q=w/(int32_t)t1; if (q!=(int32_t)q) TRAP({at},\"divide error\"); eax=(uint32_t)q; edx=(uint32_t)(w%(int32_t)t1); }}",
                }[m])
            elif sz == 2:
                o.append({
                    "mul": "u=(eax&0xffffu)*(uint64_t)t1; eax=(eax&0xffff0000u)|(uint32_t)(u&0xffff); edx=(edx&0xffff0000u)|(uint32_t)((u>>16)&0xffff); cf=of=(u>>16)!=0;",
                    "imul": "w=(int64_t)(int16_t)eax*(int16_t)t1; eax=(eax&0xffff0000u)|(uint32_t)(w&0xffff); edx=(edx&0xffff0000u)|(uint32_t)((w>>16)&0xffff); cf=of=(w!=(int16_t)w);",
                    "div": f"u=((edx&0xffffu)<<16)|(eax&0xffffu); if (!t1 || u/t1>0xffffu) TRAP({at},\"divide error\"); eax=(eax&0xffff0000u)|(uint32_t)(u/t1); edx=(edx&0xffff0000u)|(uint32_t)(u%t1);",
                    "idiv": f"w=(int32_t)(((edx&0xffffu)<<16)|(eax&0xffffu)); if (!t1) TRAP({at},\"divide error\"); {{ int64_t q=w/(int16_t)t1; if (q!=(int16_t)q) TRAP({at},\"divide error\"); eax=(eax&0xffff0000u)|(uint32_t)(q&0xffff); edx=(edx&0xffff0000u)|(uint32_t)((w%(int16_t)t1)&0xffff); }}",
                }[m])
            else:
                o.append({
                    "mul": "u=(eax&0xffu)*t1; eax=(eax&0xffff0000u)|(uint32_t)(u&0xffff); cf=of=(u>>8)!=0;",
                    "imul": "w=(int8_t)eax*(int8_t)t1; eax=(eax&0xffff0000u)|(uint32_t)(w&0xffff); cf=of=(w!=(int8_t)w);",
                    "div": f"u=eax&0xffffu; if (!t1 || u/t1>0xffu) TRAP({at},\"divide error\"); eax=(eax&0xffff0000u)|(uint32_t)((u%t1)<<8)|(uint32_t)(u/t1);",
                    "idiv": f"w=(int16_t)eax; if (!t1) TRAP({at},\"divide error\"); {{ int64_t q=w/(int8_t)t1; if (q!=(int8_t)q) TRAP({at},\"divide error\"); eax=(eax&0xffff0000u)|(uint32_t)(((w%(int8_t)t1)&0xff)<<8)|(uint32_t)(q&0xff); }}",
                }[m])
            return
        if m == "push":
            if ops[0].type == x86.X86_OP_IMM:
                n = 2 if i.prefix[2] == 0x66 else 4
                v = f"0x{ops[0].imm & ((1 << (8 * n)) - 1):x}u"
            else:
                n = ops[0].size
                v = self.read(ops[0])
            o.append(f"t1={v}; CHK(esp-{n},{n},{self.at()}); esp-={n}; st(c,esp,t1,{n});")
            return
        if m == "pop":
            n = ops[0].size
            o.append(f"CHK(esp,{n},{self.at()}); t1=ld(c,esp,{n}); esp+={n};")
            self.mem_dst(ops[0])
            o.append(self.write(ops[0], "t1"))
            return
        if m == "pushal":
            o.append(f"t1=esp; esp-=32; CHK(esp,32,{self.at()}); " +
                     " ".join(f"st(c,esp+{28 - 4 * k},{r if r != 'esp' else 't1'},4);" for k, r in enumerate(REG32)))
            return
        if m == "popal":
            o.append(f"CHK(esp,32,{self.at()}); " +
                     " ".join(f"{r}=ld(c,esp+{28 - 4 * k},4);" for k, r in enumerate(REG32) if r != "esp") + " esp+=32;")
            return
        if m == "pushaw":       # 16-bit pusha: AX CX DX BX (SP before) BP SI DI, 2 bytes each
            o.append(f"t1=esp; esp-=16; CHK(esp,16,{self.at()}); " +
                     " ".join(f"st(c,esp+{14 - 2 * k},{r if r != 'esp' else 't1'}&0xffffu,2);" for k, r in enumerate(REG32)))
            return
        if m == "popaw":        # pops the low words back; the saved SP is skipped
            o.append(f"CHK(esp,16,{self.at()}); " +
                     " ".join(f"{r}=({r}&0xffff0000u)|ld(c,esp+{14 - 2 * k},2);" for k, r in enumerate(REG32) if r != "esp") + " esp+=16;")
            return
        if m == "pushfd":
            o.append(f"t1=(uint32_t)cf|2u|((uint32_t)pf<<2)|((uint32_t)af<<4)|((uint32_t)zf<<6)|((uint32_t)sf<<7)|0x200u|((uint32_t)df<<10)|((uint32_t)of<<11)|c->eflx; "
                     f"CHK(esp-4,4,{self.at()}); esp-=4; st(c,esp,t1,4);")
            return
        if m == "popfd":
            o.append(f"CHK(esp,4,{self.at()}); t1=ld(c,esp,4); esp+=4; cf=t1&1; pf=(t1>>2)&1; af=(t1>>4)&1; zf=(t1>>6)&1; sf=(t1>>7)&1; "
                     "df=(t1>>10)&1; of=(t1>>11)&1; c->eflx=t1&0x240000u;")
            return
        if m == "lahf":
            o.append("eax=(eax&0xffff00ffu)|(((uint32_t)sf<<7|(uint32_t)zf<<6|(uint32_t)af<<4|(uint32_t)pf<<2|2u|(uint32_t)cf)<<8);")
            return
        if m == "sahf":
            o.append("t1=(eax>>8)&0xffu; cf=t1&1; pf=(t1>>2)&1; af=(t1>>4)&1; zf=(t1>>6)&1; sf=(t1>>7)&1;")
            return
        if m in ("clc", "stc", "cmc", "cld", "std"):
            o.append({"clc": "cf=0;", "stc": "cf=1;", "cmc": "cf=!cf;", "cld": "df=0;", "std": "df=1;"}[m])
            return
        if m == "leave":
            o.append(f"CHK(ebp,4,{self.at()}); esp=ebp; ebp=ld(c,esp,4); esp+=4;")
            return
        if m == "enter":
            if ops[1].imm != 0:
                raise Unsupported("enter nested")
            o.append(f"esp-=4; CHK(esp,4,{self.at()}); st(c,esp,ebp,4); ebp=esp; esp-=0x{ops[0].imm:x}u;")
            return
        if m == "xchg":
            a = self.read(ops[0])
            o.append(f"t1={a};")
            b = self.read(ops[1])
            o.append(f"t2={b};")
            o.append(self.write(ops[1], "t1"))     # ea from the read: xchg [eax],al must not address through the new al
            o.append(self.write(ops[0], "t2"))
            return
        if m == "xadd":
            b = self.read(ops[1])
            o.append(f"t2={b};")
            a = self.read(ops[0])
            o.append(f"t1={a}; " + self.arith("add", M, S))
            o.append(self.write(ops[1], "t1"))
            o.append(self.write(ops[0], "res"))
            return
        if m == "bswap":
            r = self.r32(ops[0].reg)
            o.append(f"{r}=__builtin_bswap32({r});")
            return
        if m in ("cdq", "cwde", "cbw", "cwd"):
            o.append({"cdq": "edx=(uint32_t)((int32_t)eax>>31);",
                      "cwde": "eax=(uint32_t)(int32_t)(int16_t)(eax&0xffffu);",
                      "cbw": "eax=(eax&0xffff0000u)|((uint32_t)(int16_t)(int8_t)(eax&0xffu)&0xffffu);",
                      "cwd": "edx=(edx&0xffff0000u)|(((int16_t)eax<0)?0xffffu:0u);"}[m])
            return
        if m == "xlatb":
            o.append(f"ea=ebx+(eax&0xffu); CHK(ea,1,{self.at()}); eax=(eax&0xffffff00u)|ld(c,ea,1);")
            return
        if m.startswith("set") and m[3:] in COND:
            self.mem_dst(ops[0])
            o.append(self.write(ops[0], f"({COND[m[3:]]})?1u:0u"))
            return
        if m == "rdtsc":
            o.append("u=rn_rdtsc(c); eax=(uint32_t)u; edx=(uint32_t)(u>>32);")
            return
        if m == "cpuid":
            o.append("rn_cpuid(c,&eax,&ebx,&ecx,&edx);")
            return
        if m == "ret":
            imm = ops[0].imm if ops else 0
            o.append(f"CHK(esp,4,{self.at()}); c->ret=ld(c,esp,4); esp+=4+{imm}; goto done;")
            return
        if m == "call":
            op = ops[0]
            nxt = i.address + i.size
            if op.type == x86.X86_OP_IMM:
                if op.imm == nxt:          # call $+5: pushes its own address
                    o.append(f"CHK(esp-4,4,{self.at()}); esp-=4; st(c,esp,0x{nxt:x}u,4);")
                elif op.imm in self.lifted:
                    self.call_to(None, op.imm)
                else:
                    self.call_to(f"0x{op.imm:x}u")
                return
            o.append(f"t1={self.read(op)};")
            self.call_to("t1")
            return
        if m == "jmp":
            op = ops[0]
            if op.type == x86.X86_OP_IMM:
                o.append(self.jump(op.imm))
                return
            o.append(f"t1={self.read(op)};")
            # Any address-taken label of this function can be the target, not only the table we bounded:
            # the targets of all its tables (they may sit in .text) and code pointers in data.
            cases = self.all_cases
            # One switch per function (dispatch:, emitted by lift), not one per indirect jmp.
            o.append("goto dispatch;" if cases else self.tail("t1"))
            return
        if m in ("loop", "loope", "loopne", "jecxz"):
            t = ops[0].imm
            cond = {"loop": "ecx", "loope": "ecx&&zf", "loopne": "ecx&&!zf", "jecxz": "!ecx"}[m]
            if m != "jecxz":
                o.append("ecx--;")
            o.append(self.jump(t, cond))
            return
        if m.startswith("j") and m[1:] in COND:
            t, p = ops[0].imm, self.prev
            # while (flag); the only thing that can end it is another thread, and that one needs the GIL (sndmfc 0x10303352).
            if p is not None and p.address == t and p.address + p.size == i.address and p.mnemonic in ("cmp", "test") \
                    and any(x.type == x86.X86_OP_MEM for x in p.operands):
                o.append(f"if ({COND[m[1:]]}) {{ rn_spin(); {self.jump(t)} }}")
                return
            o.append(self.jump(t, COND[m[1:]]))
            return
        raise Unsupported(f"{m}")

    def mmx(self, i, m):
        """MMX on c->mm (host/rt.h). A memory operand is m64, or m32 for movd and the low-half unpacks."""
        o, ops = self.out, i.operands
        if m == "emms":
            o.append("c->ftag=0;")
            return

        def mm(x):
            return f"c->mm[{int(i.reg_name(x.reg)[2:])}]"

        def src(x):
            if x.type == x86.X86_OP_IMM:
                return f"0x{x.imm & 0xff:x}ull"
            if x.type == x86.X86_OP_REG:
                n = i.reg_name(x.reg)
                return mm(x) if n.startswith("mm") else f"(uint64_t){self.r32(x.reg)}"
            o.append(f"ea={self.ea(x.mem)}; CHK(ea,{x.size},{self.at()});")
            return "ld64(c,ea)" if x.size == 8 else f"(uint64_t)ld(c,ea,{x.size})"

        d, sv = ops[0], src(ops[1])
        o.append("mmx_enter(c);")
        if m in ("movd", "movq"):
            if d.type == x86.X86_OP_MEM:
                o.append(f"ea={self.ea(d.mem)}; CHK(ea,{d.size},{self.at()});")
                o.append(f"st64(c,ea,{sv});" if d.size == 8 else f"st(c,ea,(uint32_t)({sv}),4);")
            elif i.reg_name(d.reg).startswith("mm"):
                o.append(f"{mm(d)}={sv};" if m == "movq" else f"{mm(d)}=(uint32_t)({sv});")
            else:
                o.append(f"{self.r32(d.reg)}=(uint32_t)({sv});")
            return
        o.append(f"{mm(d)}={m}({mm(d)},{sv});")

    def string(self, i, pre, kind, n):
        o = self.out
        at = self.at()
        d = f"(df?-{n}:{n})"
        M = (1 << (8 * n)) - 1
        S = 1 << (8 * n - 1)
        acc = {1: "(eax&0xffu)", 2: "(eax&0xffffu)", 4: "eax"}[n]
        wacc = {1: "eax=(eax&0xffffff00u)|t1;", 2: "eax=(eax&0xffff0000u)|t1;", 4: "eax=t1;"}[n]
        if kind == "movs":
            step = f"CHK(esi,{n},{at}); CHK(edi,{n},{at}); st(c,edi,ld(c,esi,{n}),{n}); esi+={d}; edi+={d};"
        elif kind == "stos":
            step = f"CHK(edi,{n},{at}); st(c,edi,eax,{n}); edi+={d};"
        elif kind == "lods":
            step = f"CHK(esi,{n},{at}); t1=ld(c,esi,{n}); {wacc} esi+={d};"
        elif kind == "cmps":
            step = f"CHK(esi,{n},{at}); CHK(edi,{n},{at}); t1=ld(c,esi,{n}); t2=ld(c,edi,{n}); esi+={d}; edi+={d}; " + self.arith("cmp", M, S)
        else:
            step = f"CHK(edi,{n},{at}); t1={acc}; t2=ld(c,edi,{n}); edi+={d}; " + self.arith("cmp", M, S)
        if not pre:
            o.append(step)
        elif kind in ("movs", "stos", "lods"):
            if kind == "movs" and n == 4:   # bulk path for the common forward copy that does not overlap
                o.append(f"if (RN_BULK && !df && ecx && ecx<=rn_sz/4 && (uint32_t)(esi-LOW)<=rn_sz-ecx*4 && (uint32_t)(edi-LOW)<=rn_sz-ecx*4 "
                         f"&& (edi+ecx*4<=esi || esi+ecx*4<=edi)) {{ memcpy(rn_m+edi,rn_m+esi,ecx*4); esi+=ecx*4; edi+=ecx*4; ecx=0; }}")
            o.append(f"while (ecx) {{ {step} ecx--; }}")
        else:
            stop = "if (!zf) break;" if pre == "repe" else "if (zf) break;"
            o.append(f"while (ecx) {{ {step} ecx--; {stop} }}")

    # --- x87, decoded from the opcode bytes (capstone's operand order is ambiguous) ---------------
    def x87(self, i):
        o = self.out
        at = self.at()
        b = list(i.opcode)
        op, modrm = b[0], i.modrm
        mod, reg, rm = modrm >> 6, (modrm >> 3) & 7, modrm & 7
        if op < 0xd8 or op > 0xdf:
            raise Unsupported(i.mnemonic)
        mem = next((x for x in i.operands if x.type == x86.X86_OP_MEM), None)
        ar = ["fset(c,0,FADD(*fr(c,0),v));", "fset(c,0,FMUL(*fr(c,0),v));", "fcom(c,*fr(c,0),v);", "fcom(c,*fr(c,0),v); fpop(c);",
              "fset(c,0,FSUB(*fr(c,0),v));", "fset(c,0,FSUB(v,*fr(c,0)));", "fset(c,0,FDIV(*fr(c,0),v));", "fset(c,0,FDIV(v,*fr(c,0)));"]
        if mod != 3:
            o.append(f"ea={self.ea(mem.mem)};")
            ck = lambda n: f"CHK(ea,{n},{at});"
            if op in (0xd8, 0xdc):
                n = 4 if op == 0xd8 else 8
                o.append(f"{ck(n)} {{ double v={'ldfx' if n == 4 else 'lddx'}(c,ea); {ar[reg]} }}")
                return
            if op in (0xda, 0xde):
                n = 4 if op == 0xda else 2
                o.append(f"{ck(n)} {{ double v=(int{8*n}_t)ld(c,ea,{n}); {ar[reg]} }}")
                return
            if op == 0xd9:
                s = {0: f"{ck(4)} fpush(c,ldfx(c,ea));", 2: f"{ck(4)} stf(c,ea,fst32(c,*fr(c,0)));",
                     3: f"{ck(4)} stf(c,ea,fst32(c,*fr(c,0))); fpop(c);", 5: f"{ck(2)} c->fcw=ld(c,ea,2);",
                     7: f"{ck(2)} st(c,ea,c->fcw,2);"}.get(reg)
            elif op == 0xdb:
                s = {0: f"{ck(4)} fpush(c,(int32_t)ld(c,ea,4));", 1: f"{ck(4)} st(c,ea,(uint32_t)fist(c,*fr(c,0),32,1),4); fpop(c);",
                     2: f"{ck(4)} st(c,ea,(uint32_t)fist(c,*fr(c,0),32,0),4);", 3: f"{ck(4)} st(c,ea,(uint32_t)fist(c,*fr(c,0),32,0),4); fpop(c);",
                     5: f"{ck(10)} fpush(c,ld80(c,ea));", 7: f"{ck(10)} st80(c,ea,*fr(c,0)); fpop(c);"}.get(reg)
            elif op == 0xdd:
                s = {0: f"{ck(8)} fpush(c,lddx(c,ea));", 1: f"{ck(8)} st64(c,ea,(uint64_t)fist(c,*fr(c,0),64,1)); fpop(c);",
                     2: f"{ck(8)} std_(c,ea,*fr(c,0));", 3: f"{ck(8)} std_(c,ea,*fr(c,0)); fpop(c);",
                     7: f"{ck(2)} st(c,ea,fstsw(c),2);"}.get(reg)
            else:  # 0xdf
                s = {0: f"{ck(2)} fpush(c,(int16_t)ld(c,ea,2));", 1: f"{ck(2)} st(c,ea,(uint32_t)fist(c,*fr(c,0),16,1),2); fpop(c);",
                     2: f"{ck(2)} st(c,ea,(uint32_t)fist(c,*fr(c,0),16,0),2);", 3: f"{ck(2)} st(c,ea,(uint32_t)fist(c,*fr(c,0),16,0),2); fpop(c);",
                     5: f"{ck(8)} fpush(c,(double)(int64_t)ld64(c,ea));", 7: f"{ck(8)} st64(c,ea,(uint64_t)fist(c,*fr(c,0),64,0)); fpop(c);"}.get(reg)
            if s is None:
                raise Unsupported(i.mnemonic)
            o.append(s)
            return
        k = rm
        if op == 0xd8:
            o.append(f"{{ double v=*fr(c,{k}); {ar[reg]} }}")
            return
        if op in (0xdc, 0xde):
            # dest st(i), src st(0); for 4..7 the direction is swapped relative to d8 (Intel SDM).
            if op == 0xde and reg == 3:
                if k != 1:
                    raise Unsupported(i.mnemonic)
                o.append("fcom(c,*fr(c,0),*fr(c,1)); fpop(c); fpop(c);")
                return
            ex = {0: f"FADD(*fr(c,{k}),*fr(c,0))", 1: f"FMUL(*fr(c,{k}),*fr(c,0))", 4: f"FSUB(*fr(c,0),*fr(c,{k}))",
                  5: f"FSUB(*fr(c,{k}),*fr(c,0))", 6: f"FDIV(*fr(c,0),*fr(c,{k}))", 7: f"FDIV(*fr(c,{k}),*fr(c,0))"}.get(reg)
            if ex is None:
                if reg in (2, 3) and op == 0xdc:
                    o.append(f"fcom(c,*fr(c,0),*fr(c,{k}));" + (" fpop(c);" if reg == 3 else ""))
                    return
                raise Unsupported(i.mnemonic)
            o.append(f"fset(c,{k},{ex});" + (" fpop(c);" if op == 0xde else ""))
            return
        if op == 0xd9:
            if reg == 0:
                o.append(f"{{ double v=*fr(c,{k}); fpush(c,v); }}")
                return
            if reg == 1:
                o.append(f"{{ double v=*fr(c,0); *fr(c,0)=*fr(c,{k}); *fr(c,{k})=v; }}")
                return
            x = modrm
            if x in FPU_CONST:
                o.append(f"fpush(c,{FPU_CONST[x]});")
                return
            s = {0xd0: "", 0xe0: "*fr(c,0)=-*fr(c,0);", 0xe1: "*fr(c,0)=fabs(*fr(c,0));", 0xe4: "fcom(c,*fr(c,0),0.0);",
                 0xe5: "fxam(c);", 0xf0: "fset(c,0,fx1(c,*fr(c,0),exp2(*fr(c,0))-1.0));", 0xf1: "if (*fr(c,0)==0 && *fr(c,1)==*fr(c,1) && *fr(c,1)!=0) c->fsw|=FZE; fset(c,1,fx2(c,*fr(c,0),*fr(c,1),*fr(c,1)*log2(*fr(c,0)))); fpop(c);",
                 0xf2: "if (fabs(*fr(c,0))>=9223372036854775808.0) c->fsw|=FC2; else { fset(c,0,fx1(c,*fr(c,0),tan(*fr(c,0)))); fpush(c,1.0); c->fsw&=~FC2; }",
                 0xf3: "fset(c,1,fx2(c,*fr(c,1),*fr(c,0),atan2(*fr(c,1),*fr(c,0)))); fpop(c);",
                 0xf8: "fprem(c,0);",
                 0xf5: "fprem(c,1);",
                 0xf7: "c->ftop=(c->ftop+1)&7;", 0xf6: "c->ftop=(c->ftop-1)&7;",
                 0xf9: "fset(c,1,fx2(c,*fr(c,0),*fr(c,1),*fr(c,1)*log2(*fr(c,0)+1.0))); fpop(c);",
                 0xfa: "fset(c,0,fx1(c,*fr(c,0),FSQRT(*fr(c,0))));",
                 0xfb: "if (fabs(*fr(c,0))>=9223372036854775808.0) c->fsw|=FC2; else { double v=*fr(c,0); fset(c,0,fx1(c,v,sin(v))); fpush(c,fx1(c,v,cos(v))); c->fsw&=~FC2; }",
                 0xfc: "fset(c,0,fx1(c,*fr(c,0),frnd(c,*fr(c,0))));", 0xfd: "{ double x=*fr(c,0), y=*fr(c,1); fset(c,0,fx2(c,x,y,isnan(y)||(isinf(y)&&(x==0||(isinf(x)&&y<0)))?NAN:ldexp(x,y>=2097152.0?2097152:y<=-2097152.0?-2097152:(int)trunc(y)))); }",
                 0xfe: "if (fabs(*fr(c,0))>=9223372036854775808.0) c->fsw|=FC2; else { fset(c,0,fx1(c,*fr(c,0),sin(*fr(c,0)))); c->fsw&=~FC2; }", 0xff: "if (fabs(*fr(c,0))>=9223372036854775808.0) c->fsw|=FC2; else { fset(c,0,fx1(c,*fr(c,0),cos(*fr(c,0)))); c->fsw&=~FC2; }"}.get(x)
            if s is None:
                raise Unsupported(i.mnemonic)
            o.append(s)
            return
        if op == 0xdd:
            s = {0: f"ffree(c,{k});", 2: f"*fr(c,{k})=*fr(c,0);", 3: f"*fr(c,{k})=*fr(c,0); fpop(c);",
                 4: f"fucom(c,*fr(c,0),*fr(c,{k}));", 5: f"fucom(c,*fr(c,0),*fr(c,{k})); fpop(c);"}.get(reg)
            if s is None:
                raise Unsupported(i.mnemonic)
            o.append(s)
            return
        if op == 0xda and modrm == 0xe9:
            o.append("fucom(c,*fr(c,0),*fr(c,1)); fpop(c); fpop(c);")
            return
        if op == 0xdb and modrm in (0xe2, 0xe3):
            o.append("c->fsw&=~0x80ffu;" if modrm == 0xe2 else "finit(c);")
            return
        if op == 0xdb and reg in (5, 6) or op == 0xdf and reg in (5, 6):    # fucomi/fcomi(p): ZF PF CF
            o.append(f"{{ double a=*fr(c,0), v=*fr(c,{k}); if (a!=a||v!=v) zf=pf=cf=1; else {{ zf=a==v; cf=a<v; pf=0; }} of=sf=0; }}" +
                     (" fpop(c);" if op == 0xdf else ""))
            return
        if op == 0xdf and modrm == 0xe0:
            o.append("eax=(eax&0xffff0000u)|fstsw(c);")
            return
        raise Unsupported(i.mnemonic)

    # --- whole function ---------------------------------------------------
    def lift(self, entry, lifted=None):
        """C source for one function. `lifted` = set of entries that exist as C functions."""
        self.lifted = lifted if lifted is not None else {entry}
        body, self.switches, bad = self.body(entry)
        targets = {entry}
        for a, i in body.items():
            if i.group(CS_GRP_JUMP) and i.operands and i.operands[0].type == x86.X86_OP_IMM:
                targets.add(i.operands[0].imm)
            targets.update(self.switches.get(a, []))
        self.labels = set(body) | bad
        self.all_cases = {t for tg in self.switches.values() for t in tg if t in self.labels} | (self.addr_taken & self.labels)
        self.info = {"calls": 0, "traps": [], "x87": 0}
        self.out = []
        order = sorted(self.labels)
        next_label = dict(zip(order, order[1:]))
        for a in order:
            i = body.get(a)
            self.out.append(f"L{a:x}:;")
            if self.trace:      # difftest RN_ONLY: report the state before every instruction
                self.out.append(f"{{ {self.save()} rn_trace_step(c,0x{a:x}u); }}")
            if i is None:
                self.out.append(f"TRAP(0x{a:x},\"cannot decode\");")
                self.info["traps"].append("decode")
                continue
            if a in patches.MID_HOOKS:
                self.out.append(f"{{ void {patches.MID_HOOKS[a]}(Ctx *); {self.save()} {patches.MID_HOOKS[a]}(c); {self.load()} }}")
            mark = len(self.out)
            try:
                self.emit(i)
            except (Unsupported, KeyError, IndexError) as x:
                del self.out[mark:]
                name = (i.mnemonic + " " + i.op_str).strip().replace('"', "'")
                self.out.append(f"TRAP(0x{a:x},\"not lifted: {name}\");")
                self.info["traps"].append(str(x) if isinstance(x, Unsupported) else i.mnemonic)
            if i.group(CS_GRP_CALL):
                self.info["calls"] += 1
            nxt = a + i.size
            pre, m = self.mnem(i)
            ends = i.group(CS_GRP_RET) or m in ("jmp", "hlt", "int3", "ud2", "iretd", "retf", "int", "int1", "into")
            if not ends and nxt not in self.labels:
                self.out.append(f"TRAP(0x{nxt:x},\"ran off the decoded body\");")
            elif not ends and next_label.get(a) != nxt:
                self.out.append(f"goto L{nxt:x};")     # an overlapping decode stream starts in between
        if "goto dispatch;" in self.out:
            self.out.append("dispatch: switch (t1) { " + " ".join(f"case 0x{t:x}u: {self.jump(t)}" for t in sorted(self.all_cases))
                            + " } " + self.tail("t1"))
        regs = " ".join(f"uint32_t {r}=c->r[{k}];" for k, r in enumerate(REG32))
        flags = " ".join(f"int {f}=c->{f};" for f in FLAGS)
        # RN_LIFT_PROBES=<hex,...>: these entries call rn_probe first (host/dispatch.c: dumps for debugging).
        probe = f"rn_probe(c,0x{entry:x}u);\n" if entry in PROBES else ""
        # patches.HOST_FUNCS: a host function answers this entry when it can (returns 1), else the lifted body runs.
        host = patches.host_func(entry)
        decl, hook = (f"int {host}(Ctx *c);\n", f"if ({host}(c)) return;\n") if host else ("", "")
        # The guest memory base and size never change after boot (host/main.c): locals, not a reload per access (rt.h).
        return (f"{decl}void f_{entry:x}(Ctx *c) {{\n{probe}{hook}{regs} {flags}\n"
                "uint8_t *const rn_m=c->mem; const uint32_t rn_sz=c->memsz-LOW; (void)rn_m; (void)rn_sz;\n"
                "uint32_t ea=0,t1=0,t2=0,res=0,n=0,k=0; uint64_t u=0; int64_t w=0;\n"
                "(void)ea;(void)t1;(void)t2;(void)res;(void)n;(void)k;(void)u;(void)w;\n"
                f"goto L{entry:x};\n" + "\n".join(self.out) +
                f"\ndone: {self.save()} return;\ntrap: c->halt=1; {self.save()} return;\n}}\n")
