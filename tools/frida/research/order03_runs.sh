#!/bin/bash
# Sequential bounded ORDER-03 captures through the shared picker (environments B/C only).
# Usage: order03_runs.sh <outdir> <name>:<mode>:<variant> ...
set -u
OUT=$1; shift
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research
PY=/home/lofcz/.local/share/uv/tools/frida-tools/bin/python
cd "$(dirname "$0")/../../.."
for spec in "$@"; do
  IFS=: read -r name mode variant <<< "$spec"
  echo "$(date -Is) start $name"
  $R/_env/live.sh "$PY" tools/frida/research/order_trace.py --mode "$mode" --data {DATA} --remote {REMOTE} --x11-display {DISPLAY} \
    --observer tools/frida/research/order03_observer.js --probe tools/frida/research/order03_probe.j \
    --map "Maps\\RS-ORDER-03.1-$variant.w3m" --seconds 110 --continue-at 70 --continue-every 5 \
    --preload-names "rs-o3-$variant.txt" --output "$OUT/$name.jsonl" > "$OUT/$name.log" 2>&1
  echo "$(date -Is) end $name exit $?"
done
