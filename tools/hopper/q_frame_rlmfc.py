# Question (2026-11-01): Max Payne frame chain inside the runtime library (rlmfc.dll): 0x10026b90 and 0x10027640 sit between the engine's frame function and the driver's swapBuffer. What they do, what they call, and whether the update of the world happens above or below them.
# Image: build/img/rlmfc.dll. Out: build/hopper/frame_rlmfc.txt, ends in DONE.
OUT = '/Users/angeldamian/maxpayne-re-b/build/hopper/frame_rlmfc.txt'
Q = "Max Payne frame chain inside the runtime library (rlmfc.dll): 0x10026b90 and 0x10027640 sit between the engine's frame function and the driver's swapBuffer. What they do, what they call, and whether the update of the world happens above or below them."
ADDRS = [0x10026b90, 0x10027640]
DATA = []

import time
t0 = time.time(); doc = Document.getCurrentDocument()
f = open(OUT, 'w'); f.write('# %s\nanalysis %.0f s\n' % (Q, time.time() - t0))
def seg(a): return doc.getSegmentAtAddress(a)
def proc(a):
    s = seg(a); return s.getProcedureAtAddress(a) if s else None
def where(a):
    p = proc(a); e = p.getEntryPoint() if p else None
    return '0x%x in %s' % (a, ('0x%x %s' % (e, doc.getNameAtAddress(e) or '')) if e is not None else '?')
def refs(a):
    for get in (lambda: seg(a).getReferencesOfAddress(a), lambda: doc.getReferencesOfAddress(a)):
        try:
            r = get()
            if r is not None: return sorted(set(r))
        except Exception: pass
    return []
def insn(a):
    try:
        i = seg(a).getInstructionAtAddress(a)
        return '%s %s' % (i.getInstructionString(), ', '.join(i.getFormattedArgument(k) for k in range(i.getArgumentCount())))
    except Exception as x: return '(no instruction: %s)' % x
# Hopper analyzes a fresh copy in the background: wait for the first address before asking.
for _ in range(300):
    doc.waitForBackgroundProcessToEnd()
    if proc(ADDRS[0]) is not None: break
    time.sleep(1)
n = 0
for a in ADDRS:
    p = proc(a)
    if p is None: f.write('\n==== 0x%x: no procedure\n' % a); continue
    e = p.getEntryPoint(); n += 1
    f.write('\n==== 0x%x %s (asked 0x%x)\n' % (e, doc.getNameAtAddress(e) or '', a))
    try: f.write((p.decompile() or '(no pseudocode)') + '\n')
    except Exception as x: f.write('(decompile failed: %s)\n' % x)
    f.write('callers: %s\n' % '; '.join(where(r) for r in refs(e)))
    try: f.write('callees: %s\n' % ' '.join('0x%x' % c for c in sorted(set(c.toAddress() for c in p.getAllCallees()))))
    except Exception: pass
    f.flush()
for a in DATA:
    r = refs(a)
    f.write('\n==== data 0x%x: %d refs\n' % (a, len(r)))
    for x in r: f.write('  %s: %s\n' % (where(x), insn(x)))
    f.flush()
seen = set(ADDRS)
for a in DATA:
    for x in refs(a):
        p = proc(x)
        if p is None or len(seen) >= len(ADDRS) + 16: continue
        e = p.getEntryPoint()
        if e in seen: continue
        seen.add(e); n += 1
        f.write('\n==== 0x%x %s (refs data 0x%x)\n' % (e, doc.getNameAtAddress(e) or '', a))
        try: f.write((p.decompile() or '(no pseudocode)') + '\n')
        except Exception as x: f.write('(decompile failed: %s)\n' % x)
        f.write('callers: %s\n' % '; '.join(where(r) for r in refs(e)))
        f.flush()
f.write('\nDONE %.0f s, %d procedures, %d data\n' % (time.time() - t0, n, len(DATA))); f.close()
doc.closeDocument()
