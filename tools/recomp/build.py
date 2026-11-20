#!/usr/bin/env python3
"""Lift every function of every guest image (census/images.json) to C in build/gen, then compile.

build/gen/<image>_NNN.c  lifted functions, in chunks
build/gen/funcs.h        prototypes
build/gen/table.c        sorted guest address -> function table for rn_call (all images in one table)
build/gen/images.c       loader tables (tools/recomp/images.py)
census/lift_report.json  counts per image: functions, traps by reason
Usage: [RN_JOBS=n] [RN_RELIFT=1] [RN_BR=11] .venv/bin/python tools/recomp/build.py [native|web|lift]   (default: native, 4 jobs; web needs emsdk). Run images.py and frontend.py first.
"""
import collections, glob, hashlib, inspect, json, multiprocessing as mp, os, re, shutil, subprocess, sys
sys.path.insert(0, os.path.dirname(__file__))
from lift import Lifter, Unsupported
import patches

CHUNK_INSNS = 60000         # lifted instructions per C file (compile parallelism)
GEN = "build/gen"
JOBS = int(os.environ.get("RN_JOBS", 4))   # 10 lift workers panicked a 16 GB Mac; 4 peaked at 964 MB of python RSS in all (2026-11-05, 60 s lift)
# Lift stamp: a build whose lift inputs are unchanged since the last lift reuses build/gen (RN_RELIFT=1 or `build.py lift` forces it).
STAMP = f"{GEN}/lift.stamp"
STAMP_IN = ["tools/recomp/lift.py", "tools/recomp/patches.py", "tools/recomp/frontend.py", "tools/recomp/images.py", "census/images.json"]
STAMP_ENV = ("RN_LIFT_PROBES", "RN_LIFT_TRACE")     # both change the lifted C
MAN = json.load(open("census/images.json"))
L = FUNCS = None


def functions(name):
    return {int(l.split()[0], 16): l.split()[1] for l in open(f"census/fn/{name}_functions.tsv")}


def init(name):
    global L, FUNCS
    FUNCS = functions(name)
    L = Lifter(MAN["images"][name]["image"], FUNCS)
    # RN_LIFT_TRACE=<image,...>: rn_trace_step before every instruction of these images (host/difftrace.c).
    L.trace = name in os.environ.get("RN_LIFT_TRACE", "").split(",")


def work(e):
    try:
        src = L.lift(e, set(FUNCS))
        cover = [(a, i.size) for a, i in L.body(e)[0].items()] if FUNCS[e] != "data" else []
        return e, src, L.info, cover, None
    except Unsupported as x:
        return e, None, None, [], str(x)


def write_if_changed(path, text):
    if os.path.exists(path) and open(path).read() == text:
        return
    open(path, "w").write(text)


def lift_image(name):
    funcs = functions(name)
    with mp.Pool(JOBS, initializer=init, initargs=(name,)) as pool:
        res = pool.map(work, sorted(funcs), chunksize=64)
    # A data pointer that lands inside an instruction of real code is not a code start.
    inside = set()
    for e, src, info, cover, err in res:
        for a, n in cover:
            inside.update(range(a + 1, a + n))
    traps, failed, dropped, out = collections.Counter(), {}, 0, []
    for e, src, info, cover, err in res:
        if err:
            failed[hex(e)] = err
            continue
        if funcs[e] == "data" and e in inside:
            dropped += 1
            continue
        out.append((e, src))
        for t in info["traps"]:
            traps[t.split(" 0x")[0]] += 1
    return out, dict(functions=len(out), data_seeds_inside_instructions=dropped, failed=failed,
                     trap_sites_by_reason=dict(traps.most_common()))


def lift_stamp():
    h = hashlib.sha256(repr((CHUNK_INSNS, [os.environ.get(k, "") for k in STAMP_ENV])).encode())
    for fn in (functions, init, lift_image, lift_all):      # the emitter (funcs.h, table.c, the split): changing it must reach build/gen; the rest of build.py need not
        h.update(inspect.getsource(fn).encode())
    for f in STAMP_IN + sorted(glob.glob("census/fn/*")) + sorted(glob.glob("build/img/*")):
        h.update(f.encode())
        h.update(open(f, "rb").read())
    return h.hexdigest()


def gen_sources():
    return sorted(f"{GEN}/{f}" for f in os.listdir(GEN) if re.search(r"_\d{3}\.c$", f))


def lift_all(force=False):
    os.makedirs(GEN, exist_ok=True)
    stamp = lift_stamp()
    if not force and os.environ.get("RN_RELIFT") in (None, "", "0") and os.path.exists(STAMP) and open(STAMP).read() == stamp \
            and os.path.exists(f"{GEN}/table.c") and gen_sources():
        print(f"lift inputs unchanged ({STAMP}): reusing {GEN}")
        return gen_sources()
    if os.path.exists(STAMP):
        os.remove(STAMP)
    allout, rep, files = [], {"patches": [f"{p['addr']:#x}" for p in patches.active()]}, []
    for name in MAN["images"]:
        out, r = lift_image(name)
        rep[name] = r
        allout += out
        stem = name.replace(".", "_")
        k, part, n = 0, [], 0
        for e, src in out:
            part.append(src)
            n += src.count(";")
            if n >= CHUNK_INSNS:
                files.append((f"{GEN}/{stem}_{k:03d}.c", part)); k, part, n = k + 1, [], 0
        if part:
            files.append((f"{GEN}/{stem}_{k:03d}.c", part))
        print(f"{name}: lifted {r['functions']}, dropped {r['data_seeds_inside_instructions']} false data seeds, "
              f"failed {len(r['failed'])}, trap sites {sum(r['trap_sites_by_reason'].values())}")
    allout.sort()
    write_if_changed(f"{GEN}/funcs.h", '#pragma once\n#define RN_LIFTED\n#include "rt.h"\n' + "".join(f"void f_{e:x}(Ctx *c);\n" for e, _ in allout))
    for path, part in files:
        write_if_changed(path, '#include "funcs.h"\n' + "\n".join(part))
    keep = {p for p, _ in files}
    for f in os.listdir(GEN):           # chunks of an older split
        if f[-6:-2].startswith("_") and f.endswith(".c") and f"{GEN}/{f}" not in keep and f not in ("table.c", "image_tables.c"):
            os.remove(f"{GEN}/{f}")
    write_if_changed(f"{GEN}/table.c", '#include "funcs.h"\n'
                     f"const uint32_t rn_fn_count = {len(allout)};\n"
                     "const uint32_t rn_fn_addr[] = {" + ",".join(f"0x{e:x}" for e, _ in allout) + "};\n"
                     "void (*const rn_fn_ptr[])(Ctx *) = {" + ",".join(f"f_{e:x}" for e, _ in allout) + "};\n"
                     + patches.c_table())
    json.dump(rep, open("census/lift_report.json", "w"), indent=1)
    open(STAMP, "w").write(stamp)
    print(f"total lifted functions: {len(allout)} in {len(files)} files")
    return [p for p, _ in files]


# M0 host: loader, dispatch, kernel32/user32/threads, CPU identity, files, input. The D3D8, DirectSound and
# DirectInput layers come when a run reaches them.
HOST = ["host/main.c", "host/dispatch.c", "host/win32.c", "host/user32.c", "host/threads.c", "host/cpu.c",
        "host/files.c", "host/input.c", "host/gpu.c", "host/images.c", "host/sm1.c", "host/dinput8.c", "host/dsound.c", "host/gdi32.c", "host/d3d8.c", "host/widescreen.c", "host/ffp.c", "host/fast.c", "host/gamestate.c", "host/jump.c", "host/cheats.c"]


# host/fast.c stands in for the hot guest loops: it alone gets the optimizer, and the C it holds must keep x87-order
# float results, so no fused multiply-add on arm64.
FAST = "host/fast.c"
WEB_OPT = "-O2"
# Final link only (binaryen over the whole module): -Oz measured 145 -> 110 MB raw, 14.7 -> 14.0 MB br (out/research_wasm_1109.md,
# as Godot #97407). RN_WEBLINK=-O2 gives back the fast dev link.
WEB_LINK_OPT = os.environ.get("RN_WEBLINK", "-Oz")


def compile_fast(cc, out, *extra):
    subprocess.run([cc, "-O3", "-w", "-ffp-contract=off", "-fno-strict-aliasing", "-Ihost", f"-I{GEN}", *extra, "-c", FAST, "-o", out], check=True)
    return [h for h in HOST if h != FAST] + [out]


def compile_native(srcs):
    srcs = srcs + [f"{GEN}/table.c", f"{GEN}/image_tables.c"]
    os.makedirs("build/obj", exist_ok=True)
    flags = ["-O1", "-w", "-Ihost", f"-I{GEN}", "-fno-strict-aliasing"]
    jobs = []
    for s in srcs:
        o = "build/obj/" + os.path.basename(s)[:-2] + ".o"
        if not os.path.exists(o) or os.path.getmtime(o) < max(os.path.getmtime(s), os.path.getmtime("host/rt.h")):
            jobs.append(["clang", *flags, "-c", s, "-o", o])
    run_jobs(jobs)
    objs = ["build/obj/" + os.path.basename(s)[:-2] + ".o" for s in srcs]
    subprocess.run(["clang", "-O1", "-g", "-Wall", "-Wno-unused-function", "-Ihost", f"-I{GEN}", *compile_fast("clang", "build/obj/host_fast.o", "-g"), *objs,
                    "-lm", "-lpthread", "-o", "build/maxpayne"], check=True)
    print("built build/maxpayne")


def precompress(path):
    """<path>.br next to the wasm (tools/serve.py sends it to browsers that accept br): 146 MB -> 15 MB at quality 11
    (q9 18 MB in 8 s, q10 15.7 MB in 90 s, q11 14.9 MB in 200 s). RN_BR=<q> picks the quality; dev builds skip it
    (default 0, q11 is 196 s of every build): the build the OWNER plays and Safari/download numbers come from runs with RN_BR=11."""
    q = os.environ.get("RN_BR", "0")
    if q == "0":
        print("brotli skipped (dev build; RN_BR=11 for the build the OWNER plays)")
        return
    if shutil.which("brotli"):
        subprocess.run(["brotli", "-q", q, "-w", "24", "-f", "-k", path], check=True)       # writes path.br
    else:
        try:
            import brotli
        except ImportError:
            print("no brotli (CLI or python module): tools/serve.py will send gzip (28 MB)")
            return
        open(path + ".br", "wb").write(brotli.compress(open(path, "rb").read(), quality=int(q), lgwin=24))
    os.utime(path + ".br")      # brotli copies the input's mtime truncated to seconds: older than the wasm, serve.py skips it
    print(f"{path}.br: {os.path.getsize(path + '.br') >> 10} KB from {os.path.getsize(path) >> 10} KB")


def compile_web(srcs):
    """emcc build of the same sources for Chrome: web/out/maxpayne.js + .wasm (RE-NFSU compile_web)."""
    srcs = srcs + [f"{GEN}/table.c", f"{GEN}/image_tables.c"]
    os.makedirs("build/wobj", exist_ok=True)
    # -O2 as RE-NFSU builds its lifted code. Sweep 2026-11-02: same frame times as -O1 (V8 optimizes the wasm again).
    flags = [WEB_OPT, "-w", "-Ihost", f"-I{GEN}", "-fno-strict-aliasing", "-pthread"]
    stamp = "build/wobj/flags.txt"           # objects built with other flags are rebuilt
    same = os.path.exists(stamp) and open(stamp).read() == " ".join(flags)
    jobs = []
    for s in srcs:
        o = "build/wobj/" + os.path.basename(s)[:-2] + ".o"
        if not same or not os.path.exists(o) or os.path.getmtime(o) < max(os.path.getmtime(s), os.path.getmtime("host/rt.h")):
            jobs.append(["emcc", *flags, "-c", s, "-o", o])
    run_jobs(jobs)
    open(stamp, "w").write(" ".join(flags))
    objs = ["build/wobj/" + os.path.basename(s)[:-2] + ".o" for s in srcs]
    os.makedirs("web/out", exist_ok=True)
    # main() runs in a worker (PROXY_TO_PTHREAD) so guest threads and waits may block; game files are read with
    # synchronous XHR from that worker (host/files.c): OPFS blob: URLs (web/opfs.js) or tools/serve.py.
    subprocess.run(["emcc", WEB_LINK_OPT, "-pthread", "-Ihost", f"-I{GEN}", *compile_fast("emcc", "build/wobj/host_fast.o", "-pthread", "-msimd128"), *objs, "-o", "web/out/maxpayne.js",
                    "--profiling-funcs",   # wasm name section: Chrome CPU profiles show guest function names (#5)
                    "-sINITIAL_MEMORY=512MB", "-sALLOW_MEMORY_GROWTH=1", "-sMAXIMUM_MEMORY=1920MB",
                    # The page builds the memory (web/index.html profile: 1920 MB, or 640 MB on a phone) with maximum = initial, so it
                    # never grows; these two only widen the import's limits. Desktop: guest 1.25 GB + host 415 MB, under 2 GB for Firefox (host.h).
                    "-sINCOMING_MODULE_JS_API=ENVIRONMENT,arguments,canvas,dynamicLibraries,elementPointerLock,instantiateWasm,locateFile,monitorRunDependencies,noExitRuntime,noInitialRun,onAbort,onExit,onRuntimeInitialized,postRun,preInit,preRun,print,printErr,setStatus,statusMessage,stderr,stdin,stdout,thisProgram,wasm,websocket,wasmMemory",
                    "-sSTACK_SIZE=16MB", "-sDEFAULT_PTHREAD_STACK_SIZE=16MB",
                    "-sPROXY_TO_PTHREAD=1", "-sPTHREAD_POOL_SIZE=8", "-sEXIT_RUNTIME=1",
                    "-sMODULARIZE=1", "-sEXPORT_NAME=MaxPayne", "-sEXPORTED_RUNTIME_METHODS=FS,ENV,HEAPU8,HEAP32,HEAPU32,HEAPF64",
                    "-sEXPORTED_FUNCTIONS=_main,_free,_rn_gpu_mailbox,_rn_input_ring,_rn_audio_ring,_rn_telem_addr,_rn_gamestate_addr,_rn_jump_box,_rn_sound_report",
                    "-sFORCE_FILESYSTEM=1", "-sENVIRONMENT=web,worker"], check=True)
    print("built web/out/maxpayne.js + maxpayne.wasm")
    precompress("web/out/maxpayne.wasm")


def run_jobs(jobs):
    running, bad = [], 0
    for j in jobs:
        while len(running) >= JOBS:
            bad += running.pop(0).wait() != 0
        running.append(subprocess.Popen(j))
    bad += sum(p.wait() != 0 for p in running)
    if bad:
        sys.exit("compile failed")
    if jobs:
        print(f"compiled {len(jobs)} files")


if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else "native"
    srcs = lift_all(force=what == "lift")
    if what == "native":
        compile_native(srcs)
    elif what == "web":
        compile_web(srcs)
