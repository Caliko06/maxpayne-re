#!/usr/bin/env python3
"""Dump the readable script of a level file: one line per printable string, "offset<TAB>text", in file order.
The .ldb keeps each object's name, then its event names (DO_OnDeath, T_Activate...), then the message lines it sends;
the order of the strings is the order of that structure, so offsets are what rea/map/levels/*.md cite (@offset).
  python3 tools/ldb_dump.py build/data/lvl/1_Part1_Level6.ldb [--from 6600000] [--grep regex]
Paths, texture names and binary noise are dropped: only strings that look like script (::names, ->messages, events)."""
import re, sys

path = sys.argv[1]
args = dict(zip(sys.argv[2::2], sys.argv[3::2]))
start = int(args.get('--from', 0)); pat = re.compile(args['--grep'], re.I) if '--grep' in args else None
SCRIPT = re.compile(r'::|->|^(DO_|T_|C_|FSM_|CAM_)|^[A-Za-z_][A-Za-z0-9_]*$')
b = open(path, 'rb').read()
for m in re.finditer(rb'[\x20-\x7e]{5,}', b):
    if m.start() < start: continue
    s = m.group().decode()
    s = re.sub(r'^[\x20-\x40](?=[A-Za-z:!])', '', s)   # a one-byte length prefix that happens to be printable
    if re.search(r'(?i)\.(jpg|pcx|tga|wav|kf2|skd|skn)\b|x:\\', s) or not SCRIPT.search(s): continue
    if pat and not pat.search(s): continue
    print(f'{m.start()}\t{s}')
