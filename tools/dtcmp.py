#!/usr/bin/env python3
"""First difference between the lifted trace (A) and the Unicorn trace (B) of host/difftrace.c, then the guest memory
both left at the end of the call (/dev/shm/dt_a.mem, dt_b.mem).
Records: eip, eax ecx edx ebx esp ebp esi edi, flags (CF ZF SF OF bits 0..3), MM0-7: 104 bytes each.
The culprit is the instruction before the first record that differs (its result is wrong).
Usage: python3 tools/dtcmp.py [/tmp/dt_a.bin /tmp/dt_b.bin]"""
import os, struct, sys

A = sys.argv[1] if len(sys.argv) > 2 else "/tmp/dt_a.bin"
B = sys.argv[2] if len(sys.argv) > 2 else "/tmp/dt_b.bin"
R = struct.Struct("<10I8Q")
NAMES = ["eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"]
fa, fb = open(A, "rb"), open(B, "rb")
prev, n = None, 0
while True:
    a, b = fa.read(R.size), fb.read(R.size)
    if len(a) < R.size or len(b) < R.size:
        print(f"no difference in {n} records (A {'ended' if len(a) < R.size else 'longer'}, B {'ended' if len(b) < R.size else 'longer'})")
        break
    if a != b:
        x, y = R.unpack(a), R.unpack(b)
        print(f"record {n}: first difference; previous instruction {prev:08x}" if prev is not None else f"record {n}: first difference")
        print(f"  eip   A {x[0]:08x}  B {y[0]:08x}")
        for k in range(8):
            mark = "  <--" if x[1 + k] != y[1 + k] else ""
            print(f"  {NAMES[k]}   A {x[1 + k]:08x}  B {y[1 + k]:08x}{mark}")
        print(f"  flags A {x[9]:04b}  B {y[9]:04b}  (OF SF ZF CF){'  <--' if x[9] != y[9] else ''}")
        for k in range(8):
            if x[10 + k] != y[10 + k]:
                print(f"  mm{k}   A {x[10 + k]:016x}  B {y[10 + k]:016x}  <--")
        break
    prev = R.unpack(a)[0]
    n += 1

# Memory at the end: the first differing dwords, in 1 MB blocks.
MA, MB = "/dev/shm/dt_a.mem", "/dev/shm/dt_b.mem"
if os.path.exists(MA) and os.path.exists(MB):
    shown, blk = 0, 1 << 20
    with open(MA, "rb") as ma, open(MB, "rb") as mb:
        base = 0
        while shown < 16:
            x, y = ma.read(blk), mb.read(blk)
            if not x or not y:
                break
            if x != y:
                for o in range(0, len(x), 4):
                    if x[o:o + 4] != y[o:o + 4]:
                        print(f"  mem {base + o:08x}: A {x[o:o + 4][::-1].hex()}  B {y[o:o + 4][::-1].hex()}")
                        shown += 1
                        if shown >= 16:
                            break
            base += blk
    print("memory: " + ("same" if not shown else "differs (first 16 dwords above)"))
