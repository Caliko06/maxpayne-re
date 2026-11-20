# Question (2026-11-01): Max Payne frame chain, from the guest stack at the first Present: 0x415840 -> 0x421e50 -> 0x600d20 -> 0x606cd0 -> 0x416874 -> 0x5824f0 -> 0x5828c0 -> e2mfc 0x10132ac0/0x10132b30 -> rlmfc -> driver swapBuffer. Name each one and show its body so the one that runs the per-frame update is identified by evidence.
# Image: build/img/MaxPayne.exe. Out: build/hopper/framechain.txt, ends in DONE.
OUT = '/Users/angeldamian/maxpayne-re-b/build/hopper/framechain.txt'
Q = 'Max Payne frame chain, from the guest stack at the first Present: 0x415840 -> 0x421e50 -> 0x600d20 -> 0x606cd0 -> 0x416874 -> 0x5824f0 -> 0x5828c0 -> e2mfc 0x10132ac0/0x10132b30 -> rlmfc -> driver swapBuffer. Name each one and show its body so the one that runs the per-frame update is identified by evidence.'
ADDRS = [0x415840, 0x421e50, 0x600d20, 0x606cd0, 0x416874, 0x5824f0, 0x5828c0]
DATA = []

import time
t0 = time.time(); doc = Document.getCurrentDocument()
def seg(a): return doc.getSegmentAtAddress(a)
def proc(a):
    s = seg(a); return s.getProcedureAtAddress(a) if s else None
# (the wait above needs proc before it is defined; reorder below)
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
# Hopper analyze a fresh copy in the background; the script must wait for it (a 4.9 MB exe takes minutes).
for _ in range(600):
    doc.waitForBackgroundProcessToEnd()
    if proc(ADDRS[0]) is not None: break
    time.sleep(1)
f = open(OUT, 'w'); f.write('# %s\nanalysis %.0f s\n' % (Q, time.time() - t0))

if proc(ADDRS[0]) is None:
    f.write("analysis did not reach 0x%x; segments: %s\n" % (ADDRS[0], ", ".join("%s@0x%x" % (doc.getSegmentName(s), s.getStartingAddress()) for s in [seg(x) for x in ADDRS] if s)))
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
