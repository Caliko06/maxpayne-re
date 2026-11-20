#!/usr/bin/env python3
"""Compact view of q.mjs output: '=== call' headers, and for Evidence envelopes only normalized_result + limitations."""
import json, sys
for line in open(sys.argv[1]):
    line = line.rstrip("\n")
    if line.startswith("{\"evidence_id\""):
        try:
            d = json.loads(line)
            print(json.dumps(d.get("normalized_result"))[:int(sys.argv[2]) if len(sys.argv) > 2 else 6000])
            continue
        except ValueError:
            pass
    print(line[:int(sys.argv[2]) if len(sys.argv) > 2 else 6000])
