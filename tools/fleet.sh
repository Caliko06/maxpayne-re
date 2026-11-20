#!/bin/bash
# Fleet (OWNER 2026-11-02, disposable Pod): N ghost players in parallel, one map per run, from a queue.
#   tools/fleet.sh <batch> <route|play> [N|0] [map ...]
# N 0 = as many as the machine holds: min(cores / 2.5, available RAM / 4.5 GB). No maps = the whole game (25).
# A map may repeat ("part1_level1 part1_level1"): runs are not deterministic, repeats catch intermittent faults.
# Each player owns a port (5200 + lane), an empty save folder (save-<port>, wiped per run) and a Chrome profile.
# Out: build/fleet/<batch>/<map>-<n>/ (report.json, state.tsv, page.tail, jpg shots) and build/fleet/<batch>/summary.tsv,
# rewritten after every run so a pull is always current. page.log is cut to its tail: the Pod is lost at the end.
set -u
cd "$(dirname "$0")/.."
B=${1:?batch}; PLAN=${2:?route|play}; N=${3:-0}; shift; shift; [ $# -gt 0 ] && shift
MAPS=("$@")
[ ${#MAPS[@]} -eq 0 ] && MAPS=(part0_level1 part1_level1 part1_level1b part1_level2 part1_level2b part1_level3 part1_level3b
  part1_level4 part1_level5 part1_level6 part2_level0 part2_level1 part2_level2 part2_level2b part2_level3 part2_level4
  part3_level1 part3_level2 part3_level2b part3_level3 part3_level4 part3_level5 part3_level5b part3_level6 part3_level7)
GAME=${RN_GAME:-$HOME/Games/maxpayne}
SECS=${RN_SECS:-900}
if [ "$N" = 0 ]; then
  CORES=$(nproc 2>/dev/null || sysctl -n hw.ncpu)
  MEMK=$(awk '/MemAvailable/ {print $2}' /proc/meminfo 2>/dev/null || echo 16000000)
  # A container sees the host's cores and RAM: its cgroup quota is the real limit (RunPod: cpu.max, memory.max).
  read Q PER < /sys/fs/cgroup/cpu.max 2>/dev/null && [ "$Q" != max ] && CORES=$(( Q / PER ))
  ML=$(cat /sys/fs/cgroup/memory.max 2>/dev/null) && [ "$ML" != max ] && MEMK=$(( (ML - $(cat /sys/fs/cgroup/memory.current)) / 1024 ))
  N=$(( CORES * 2 / 5 )); M=$(( MEMK / 4700000 )); [ $M -lt $N ] && N=$M; [ $N -lt 1 ] && N=1
fi
OUT=build/fleet/$B; mkdir -p "$OUT"
echo "fleet $B: plan $PLAN, ${#MAPS[@]} runs, $N players" | tee "$OUT/fleet.log"

summary() {
  python3 - "$OUT" <<'PY'
import json, os, sys
o = sys.argv[1]; rows = ['run\tresult\tloaded\tsave_ok\tsteps\tfails\texit\tfps\tp99ms\tover33\tlost\tgreen\tsteady\tdeaths\tsecs\tstop']
for d in sorted(os.listdir(o)):
    p = os.path.join(o, d, 'report.json')
    if not os.path.exists(p): continue
    r = json.load(open(p)); m = (r.get('maps') or [{}])[0]; runs = r.get('runs') or [{}]
    rows.append('\t'.join(str(x) for x in [d, r.get('result'), m.get('loaded', ''), m.get('save_ok', ''), m.get('steps', ''),
        ';'.join(m.get('fails', [])), m.get('exit', runs[-1].get('reached', '')), m.get('fps', ''), m.get('p99', ''),
        m.get('over33', ''), m.get('lost', ''), m.get('green', ''), m.get('steady', ''), r.get('deaths', ''), r.get('secs', ''),
        (r.get('stop') or '')[:80]]))
open(os.path.join(o, 'summary.tsv'), 'w').write('\n'.join(rows) + '\n')
PY
}

lane() {
  local k=$1 port=$((5200 + $1)) i=$1
  while [ $i -lt ${#MAPS[@]} ]; do
    local m=${MAPS[$i]} dir="$OUT/${MAPS[$i]}-$i"
    rm -rf "$(dirname "$GAME")/save-$port" "$dir"
    node tools/ghost/ghost.mjs --port $port --plan $PLAN --from $m --to $m --secs $SECS --shots jpg --out "$dir" > "$dir.out" 2>&1
    { tail -n 300 "$dir/page.log"; } > "$dir/page.tail" 2>/dev/null; rm -f "$dir/page.log"
    tail -n 5 "$dir.out" > "$dir/driver.tail"; rm -f "$dir.out"
    python3 tools/telemetry.py build/webcheck-$port.telemetry.ndjson build/webcheck-$port.log > "$dir/telemetry.txt" 2>&1
    gzip -c build/webcheck-$port.telemetry.ndjson > "$dir/telemetry.ndjson.gz" 2>/dev/null   # the rows the steady columns came from
    tail -n 200 build/webcheck-$port.log > "$dir/web.tail" 2>/dev/null; rm -f build/webcheck-$port.*
    echo "$(date +%T) lane $k $m: $(grep -o '"result": "[a-z]*"' "$dir/report.json" 2>/dev/null)" >> "$OUT/fleet.log"
    summary
    i=$((i + N))
  done
}
for k in $(seq 0 $((N - 1))); do lane $k & sleep 20; done   # staggered: N Chromes compiling the 144 MB wasm at once spike RAM
wait
summary
echo "fleet $B done $(date +%T)" >> "$OUT/fleet.log"
