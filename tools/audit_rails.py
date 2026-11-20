# Mechanical half of the NOCHE50 audit (orchestrator 2026-11-19): files touched vs the allowlist in tools/rails.json and the
# referee numbers before/after. The blind bec auditor reads this output, the contract and the diff, and says ACCEPT/REJECT.
# Usage: python3 tools/audit_rails.py <slice>   (worktree ~/maxpayne-re-wt/<slice>, branch slice/<slice>)
import json, os, subprocess, sys
s = sys.argv[1]; R = json.load(open(os.path.join(os.path.dirname(__file__), "rails.json")))
c = R[s]; wt = os.path.expanduser(f"~/maxpayne-re-wt/{s}")
names = subprocess.run(["git", "-C", wt, "diff", "--name-only", "main...HEAD"], capture_output=True, text=True).stdout.split()
dirty = subprocess.run(["git", "-C", wt, "status", "--porcelain"], capture_output=True, text=True).stdout.strip()
ok = R["common"]["always_allowed"] + c["allowed"]
out = [p for p in names if not any(p == a or (a.endswith("/") and p.startswith(a)) for a in ok)]
print(f"AUDIT {s}: {len(names)} files committed; outside allowlist: {out or 'none'}; uncommitted: {'yes' if dirty else 'no'}")
ref = os.path.join(os.path.dirname(__file__), "gate50_referee.py")
for tag in ("before", "after"):
    p = os.path.join(wt, "out", s, tag + ".ndjson")
    if not os.path.exists(p): print(f"{tag}: MISSING {p}"); continue
    line = subprocess.run([sys.executable, ref, p], capture_output=True, text=True).stdout.strip().splitlines()[-1:]
    print(tag, line[0][8:] if line else "referee failed")
print("contract:", c["metric"], "|", c["pass"], "| guard:", c["guard"])
