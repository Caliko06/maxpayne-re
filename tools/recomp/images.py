#!/usr/bin/env python3
"""The guest images: MaxPayne.exe and every DLL it loads that ships in the game folder.

Each DLL is rebased (its own relocations) to a fixed slot inside the 1 GB guest space and written as a valid PE
to build/img/, so frontend.py and lift.py read it like any image. Imports between these images are resolved here
(by name or ordinal, forwarders followed); the rest go to the host layer. Writes:
  build/img/<file>        rebased PE
  census/images.json      bases, entries, init order, import counts
  build/gen/image_tables.c      tables for host/main.c (images, IAT slots, exports for GetProcAddress)
Usage: .venv/bin/python tools/recomp/images.py <game dir>
"""
import json, os, sys
import pefile

# file in the game folder, guest base. The exe has no relocations and keeps 0x400000. The five engine DLLs all
# prefer 0x10000000 and the runtime DLLs prefer bases above 1 GB, so every DLL gets its own slot below HEAP_LO.
# The driver is a plugin: e2mfc.dll LoadLibraryA's it from e2driver\ (P_Device::P_Device 0x1002dd70), so it is
# mapped but not initialised until then.
IMAGES = [
    ("MaxPayne.exe", 0x00400000),
    ("rlmfc.dll", 0x10000000),
    ("e2mfc.dll", 0x10100000),
    ("grphmfc.dll", 0x10200000),
    ("sndmfc.dll", 0x10300000),
    ("e2driver/e2_d3d8_driver_mfc.dll", 0x10400000),
    ("MFC42.DLL", 0x10600000),
    ("MSVCRT.DLL", 0x10800000),
    ("MSVCP60.DLL", 0x10900000),
    ("MSVCIRT.DLL", 0x10a00000),
]
PLUGINS = {"e2_d3d8_driver_mfc.dll"}
HEAP_LO = 0x11000000      # host/host.h: the guest heap starts above the last image


def rebase(path, base):
    """The file's bytes with its base relocations applied for `base` (pefile.relocate_image also rewrites the
    import name tables on write, so the relocations are applied here, on the raw file)."""
    raw = bytearray(open(path, "rb").read())
    pe = pefile.PE(data=bytes(raw))
    delta = (base - pe.OPTIONAL_HEADER.ImageBase) & 0xffffffff
    if not delta:
        return raw
    for blk in getattr(pe, "DIRECTORY_ENTRY_BASERELOC", []):
        for e in blk.entries:
            if e.type == 3:                     # IMAGE_REL_BASED_HIGHLOW
                o = pe.get_offset_from_rva(e.rva)
                v = int.from_bytes(raw[o:o + 4], "little")
                raw[o:o + 4] = ((v + delta) & 0xffffffff).to_bytes(4, "little")
            elif e.type != 0:                   # ABSOLUTE is padding
                sys.exit(f"{path}: relocation type {e.type}")
    o = pe.OPTIONAL_HEADER.get_file_offset() + 28    # ImageBase
    raw[o:o + 4] = base.to_bytes(4, "little")
    return raw


def load(game):
    out = {}
    for rel, base in IMAGES:
        raw = rebase(os.path.join(game, rel), base)
        pe = pefile.PE(data=bytes(raw))
        pe.__raw = raw
        name = os.path.basename(rel).lower()
        assert base + pe.OPTIONAL_HEADER.SizeOfImage <= (HEAP_LO if base >= 0x10000000 else 0x10000000), name
        out[name] = (rel, base, pe)
    return out


def exports(pe, base):
    by_name, by_ord, fwd = {}, {}, {}
    if not hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
        return by_name, by_ord, fwd
    for s in pe.DIRECTORY_ENTRY_EXPORT.symbols:
        if s.forwarder:
            key = s.name.decode() if s.name else s.ordinal
            fwd[key] = s.forwarder.decode()
            continue
        va = base + s.address
        by_ord[s.ordinal] = va
        if s.name:
            by_name[s.name.decode()] = va
    return by_name, by_ord, fwd


def main():
    game = sys.argv[1]
    imgs = load(game)
    exp = {n: exports(pe, base) for n, (_, base, pe) in imgs.items()}

    def resolve(dll, name, ordinal, depth=0):
        """Guest address of dll!name (or #ordinal) when dll is a guest image, else None."""
        dll = dll.lower()
        if dll not in exp or depth > 4:
            return None, dll, name if name is not None else f"#{ordinal}"
        by_name, by_ord, fwd = exp[dll]
        key = name if name is not None else ordinal
        if key in fwd:
            fdll, fname = fwd[key].split(".", 1)
            if fname.startswith("#"):
                return resolve(fdll + ".dll", None, int(fname[1:]), depth + 1)
            return resolve(fdll + ".dll", fname, None, depth + 1)
        va = by_name.get(name) if name is not None else by_ord.get(ordinal)
        if va is None:
            sys.exit(f"{dll}: no export {name or '#%d' % ordinal}")
        return va, dll, name

    os.makedirs("build/img", exist_ok=True)
    os.makedirs("build/gen", exist_ok=True)
    slots, deps, man = [], {}, {}
    for n, (rel, base, pe) in imgs.items():
        deps[n] = set()
        host_n = guest_n = 0
        for e in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
            dll = e.dll.decode().lower()
            for i in e.imports:
                name = i.name.decode() if i.name else None
                va, rdll, rname = resolve(dll, name, i.ordinal)
                if va is None:
                    slots.append((i.address, 0, rdll, rname if rname is not None else f"#{i.ordinal}"))
                    host_n += 1
                else:
                    slots.append((i.address, va, "", ""))
                    deps[n].add(dll)
                    guest_n += 1
        path = f"build/img/{os.path.basename(rel)}"
        open(path, "wb").write(pe.__raw)
        man[n] = {"file": rel, "image": path, "base": hex(base), "size": hex(pe.OPTIONAL_HEADER.SizeOfImage),
                  "entry": hex(base + pe.OPTIONAL_HEADER.AddressOfEntryPoint), "dll": bool(pe.FILE_HEADER.Characteristics & 0x2000),
                  "imports_guest": guest_n, "imports_host": host_n, "relocated": base != pefile.PE(os.path.join(game, rel), fast_load=True).OPTIONAL_HEADER.ImageBase}
    # DllMain order: dependencies first (the exe last, plugins only when LoadLibraryA asks).
    order, seen = [], set()

    def visit(n):
        if n in seen:
            return
        seen.add(n)
        for d in sorted(deps[n]):
            visit(d)
        order.append(n)
    for n in imgs:
        if n not in PLUGINS:
            visit(n)
    json.dump({"images": man, "init_order": order, "heap_lo": hex(HEAP_LO)}, open("census/images.json", "w"), indent=1)

    names = list(imgs)
    c = ['#include "host.h"', '#include "images.h"']
    c.append("const RnImage rn_images[] = {")
    for n in names:
        m = man[n]
        c.append(f'  {{"{n}", "{m["image"]}", {m["base"]}u, {m["size"]}u, {m["entry"]}u, {int(m["dll"])}, {int(n in PLUGINS)}}},')
    c.append("};")
    c.append(f"const uint32_t rn_image_count = {len(names)};")
    c.append("const uint32_t rn_init_order[] = {" + ",".join(str(names.index(n)) for n in order) + "};")
    c.append(f"const uint32_t rn_init_count = {len(order)};")
    c.append("const RnSlot rn_slots[] = {")
    for a, va, dll, name in slots:
        c.append(f'  {{0x{a:x}u, 0x{va:x}u, "{dll}", "{name}"}},')
    c.append("};")
    c.append(f"const uint32_t rn_slot_count = {len(slots)};")
    c.append("const RnExport rn_exports[] = {")
    nexp = 0
    for k, n in enumerate(names):
        by_name, by_ord, _ = exp[n]
        named = {}
        for nm, va in by_name.items():
            named.setdefault(va, []).append(nm)
        for o, va in sorted(by_ord.items()):
            for nm in (named.pop(va, None) or [None]):
                c.append(f'  {{{k}, {o}, 0x{va:x}u, {json.dumps(nm) if nm else "0"}}},')
                nexp += 1
        for va, nms in named.items():
            for nm in nms:
                c.append(f'  {{{k}, 0, 0x{va:x}u, {json.dumps(nm)}}},')
                nexp += 1
    c.append("};")
    c.append(f"const uint32_t rn_export_count = {nexp};")
    open("build/gen/image_tables.c", "w").write("\n".join(c) + "\n")
    for n in names:
        m = man[n]
        print(f"{n:24} base {m['base']:>10} size {m['size']:>9} entry {m['entry']} guest imports {m['imports_guest']:4} host imports {m['imports_host']:4}")
    print("DllMain order:", " -> ".join(order))


if __name__ == "__main__":
    main()
