#!/usr/bin/env python3
"""Join REA containing-procedure results with the batch tags -> census/os_sites.tsv (binary, site, api, procedure, evidence)."""
import json, re
from pathlib import Path
rows = []
for bdir in sorted(Path('rea/batches').iterdir()):
    out = Path('rea/out') / bdir.name / '02_sites.txt'
    if not out.exists():
        continue
    tags = {c['args']['address']: c['tag'] for c in json.loads((bdir / '02_sites.json').read_text())}
    for block in out.read_text().split('=== resolve_containing_procedure ')[1:]:
        head, body = block.split('\n', 1)
        addr = json.loads(head.rsplit(' ', 1)[0])['address']
        ev = head.rsplit(' ', 1)[1]
        try:
            r = json.loads(body.strip().splitlines()[0])
        except Exception:
            r = {}
        p = r.get('procedure') or {}
        rows.append((bdir.name, addr, tags.get(addr, '?'), p.get('address', '-'), p.get('name', 'NOT IN A FUNCTION'), ev[:15]))
with open('census/os_sites.tsv', 'w') as f:
    f.write('# REA 6.7.0 + Ghidra 12.1.4 resolve_containing_procedure on census/import_sites.tsv and special_insns.tsv sites.\n')
    f.write('# "NOT IN A FUNCTION" = Ghidra found no function body there: linear-sweep noise (data or padding), not code.\n')
    f.write('binary\tsite\tapi_or_insn\tprocedure\tname\tevidence\n')
    for r in rows:
        f.write('\t'.join(r) + '\n')
print(len(rows), sum(1 for r in rows if r[4] == 'NOT IN A FUNCTION'))
