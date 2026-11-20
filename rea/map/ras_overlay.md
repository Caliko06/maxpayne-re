<!-- Becario report 2026-11-04 (DeepSeek), read by the orchestrator, no second key yet: verify an address before patching on it. -->
# B. RAS archives: order, overrides, and how to serve an extra archive

Keys: Ghidra rea/c/rlmfc.dll, Hopper (REA provider hopper, same sha256 7dc5b6dbf7e8...) and objdump -D on build/img/rlmfc.dll. Facts are marked.

## 1. Load order (FACT unless noted)
- R_File ctor 0x1000ef00: cwd = exe dir, setOverride(1), loadArchives (rea/map/files.md §3).
- loadArchives 0x1000f1c0: setArchiveHandling(1) at 0x1000f1ce; FindFirstFileA("*.RAS") at 0x1000f235 (string 0x1003d638); per name: alloc node (vtable slot 1), link it (0x1000f24c..0x1000f262), loadArchiveHeader 0x1000f284; FindNextFileA 0x1000f292. A second glob runs only if sm_strSecondaryArchiveSearchPath is set ("\%s%s", 0x1003d630), i.e. only from cd.ini (exe 0x40ae60; absent in this install).
- Lookup: R_File::searchFile 0x1000fe60. Head = [RASArchiveList+8] (Hopper 0x100100e1/0x100100f0), test [head+4] == 0 (empty), walk with next = [node+4] (Hopper 0x1001012e), first archive that has the path wins (bsearch, comparator 0x10010210). So the FIRST name returned by FindFirstFileA is searched first; loadArchives links each new node behind the previous (0x1000f25d, 0x1000f262) = enumeration order (INFERENCE, both disassemblers agree on the instructions).
- R_File::enablePriorityArchive 0x10012ee0 overwrites the head slot itself (Hopper 0x10012f50 `mov [esi+8], eax`) -> that archive is searched before every globbed one, i.e. a .mpm wins.

## 2. FindFirstFileA in the port (FACT)
- host/files.c gf_find (files.c:613) is the only answer: native = readdir order; web = web_list (files.c:660) over /game-files.json, whose order is Python `sorted(files)` (tools/serve.py:62-70), i.e. plain byte order of the path. Wildcard match is case-insensitive (files.c:587). host/ has no archive code at all (no setArchiveHandling outside the guest).
- So the order the game sees in the port is deterministic (alphabetical) while on NTFS it is the B-tree collation order; for the lowercase ASCII names in this install those agree (INFERENCE).
- Caveat (FACT): with an OPFS copy present the page passes RN_FILES and /game-files.json is never read (web/index.html:123-129, files.c web_files). Overlay only works with a full server listing (?files=server or no OPFS copy).

## 3. Duplicates and the loose data folder (FACT)
- R_File::load 0x10011e20 does fopen(absolute path) FIRST, and only then searchFile: a loose file always beats every archive. where() 0x100126f0 returns 2 for loose, 1 for archive. ~/Games/maxpayne/data/* is loose, so it beats x_data.ras.
- No duplicate *path* exists between x_data.ras and x_english.ras in this install: the same base names live in different dirs, e.g. hud.txt = \data\database\sounds\ (dir 235, 1,069 B) in x_data vs \data\hud\ (dir 37, 26,644 B) in x_english. The key is "%05i%s" = dir index + file name (0x1001088b.c:151), so only an identical dir+name collides. x_data itself holds menu.txt twice: \data\menu\ (23,098 B) and \data\database\sounds\ (1,762 B).

## 4. .mpm mods (FACT)
- CMaxPayneStartupDialog::OnInitDialog 0x405f00 scans "*.MPM" (0x4061f6 pushes 0x866b88, FindFirstFileA 0x406200, LB_ADDSTRING 0x40622c) and lists them. The Play button 0x4073d0 compares the picked name with ".MPM" (0x407730, string 0x866c2c) and calls R_File::enablePriorityArchive (0x4077b9, census/import_sites.tsv:639) -> head of the list.
- -nodialog: 0x40cb90:15 clears DAT_00867afc, getter 0x40cd50, read at startup 0x40a6a2. No dialog = no .mpm can be picked. One .mpm per run.

## 5. Language (FACT + INFERENCE)
- MaxPayne.exe contains no "english", "spanish" or "language" string (strings -a | grep -ci = 0). Nothing in the exe names a language: loadArchives takes any *.RAS (0x1000f1c0) and every resource is asked for by path (0x10011e20).
- So the language is whatever x_<lang>.ras (+ loose data/) is installed, and for a colliding path the earliest archive in the enumeration wins. This install ships x_english.ras only.

## 6. Smallest exact way to serve an extra archive (plan, nothing changed yet)
1. serve.py: add an overlay root (3rd argv or env). Merge its files into listing() for /game-files.json and fall back to it in translate_path(). No host/*.c change: files.c only trusts that listing (files.c web_lookup + web_read).
2. Name the archive so it sorts before x_data.ras, e.g. a_spanish.ras ('0'/'a' < 'x'): the first enumerated archive wins a collision.
3. For text/menu only, skip the archive: a loose file at the exact requested path in the overlay wins with no format work (0x10011e20 fopen first). menu.txt lives at \data\menu\ in x_data, so the loose path is data/menu/menu.txt.
4. Native build has no overlay: it reads argv dir. Use a symlink dir of the install plus the extra file (no code change) if the bench needs it.
Ruled out: copying into the install (read-only); the .mpm/priority route (needs the dialog, one per run, and -nodialog is what automation uses); cd.ini secondary path (needs a file in the install).

## 7. What a writer must emit (all FACT; tools/ras.py reads it but cannot write it)
- Header 0x2c: "RAS\0" (xxd of x_data.ras), seed i32 at +4, then 0x24 ENCRYPTED bytes: nfiles, ndirs, ftsize, dtsize, version f32 (must equal 1.2; 0x100103a0 rejects anything else), headerCRC, ftCRC, dtCRC, spare.
- CRCs = zlib.crc32: the table at rlmfc 0x1003d220 starts 0, 0x77073096, 0xEE0E612C (standard reflected); R_File::CRC32 0x10013a40 is init 0xffffffff, xorout ~. headerCRC is over the 0x2c bytes with its own field zeroed (0x100103a0:76-80); table CRCs are over the decrypted table bytes (0x1001088b.c:63, 0x1001088b.c:232).
- Cipher 0x100134a0 (per byte i): out = rotl1(in, i%5) then ^= ((i+3)*6) then += (s&0xff); s = s*0xab + trunc(s/0xb1)*(-0x763d), s0 = seed or 1. The game only decrypts, so the writer needs the inverse of those three steps.
- File table entry: name\0 + 6 u32 {size, stored, 0, dirIndex, seed, method} + 16 B time; stride 0x29 (0x1001088b.c:126-155). Dir table entry: path\0 + 0x10 bytes, stride len+0x11 (0x1001088b.c:281-318); dirIndex is the ordinal, and the path strings must match what the game asks for (copy them from x_data, e.g. b'\\data\\menu\\').
- Data: concatenated in table order starting at 0x2c+ftsize+dtsize (0x1001088b.c:327-337). method 3 = stored, payload copied verbatim (0x10011e20.c:114-124); method 1/2 LZSS. No per-file CRC is checked in this load path (no CRC32 call in 0x10011e20).
- Write order is free: both tables are qsort'ed at load with 0x10010210 (0x1001088b.c:343-346).
