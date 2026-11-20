#!/usr/bin/env python3
"""Split REA batch outputs (rea/out/<bin>/*.txt) into readable files: rea/c/<bin>/<addr>.c for pseudocode, rest to .json."""
import json, sys
from pathlib import Path
def find_pc(o, acc):
    if isinstance(o, dict):
        a = o.get('address') or o.get('procedure_address') or o.get('entry')
        code = o.get('pseudo_code') or o.get('pseudocode') or o.get('decompiled') or o.get('c')
        if isinstance(code, str) and a:
            acc.append((a, o.get('name', ''), code))
        for v in o.values():
            find_pc(v, acc)
    elif isinstance(o, list):
        for v in o:
            find_pc(v, acc)
for f in sorted(Path('rea/out').glob('*/*.txt')):
    for block in f.read_text().split('\n=== ')[0:]:
        block = block.lstrip('= ')
        head, _, body = block.partition('\n')
        try:
            d = json.loads(body)
        except Exception:
            continue
        acc = []
        find_pc(d, acc)
        out = Path('rea/c') / f.parent.name
        out.mkdir(parents=True, exist_ok=True)
        for a, n, code in acc:
            ev = head.rsplit(' ', 1)[-1]
            (out / f'{a}.c').write_text(f'// {f.parent.name} {a} {n}\n// {ev}\n{code}\n')
