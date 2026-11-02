#!/bin/bash
# Sequential bounded ORDER-05 captures through the shared picker (environments B/C only).
# Usage: order05_runs.sh <outdir> <scenario> <seconds> <name>:<mode> ... ; extra order_trace.py args via O5_EXTRA (array-safe: one per line)
set -u
OUT=$1; SC=$2; SECS=$3; shift 3
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research
PY=/home/lofcz/.local/share/uv/tools/frida-tools/bin/python
cd "$(dirname "$0")/../../.."
EXTRA=()
if [ -n "${O5_EXTRA:-}" ]; then while IFS= read -r line; do [ -n "$line" ] && EXTRA+=("$line"); done <<< "$O5_EXTRA"; fi
for spec in "$@"; do
  IFS=: read -r name mode <<< "$spec"
  echo "$(date -Is) start $name"
  $R/_env/live.sh "$PY" tools/frida/research/order_trace.py --mode "$mode" --data {DATA} --remote {REMOTE} --x11-display {DISPLAY} \
    --observer tools/frida/research/order05_observer.js --probe tools/frida/research/order05_probe.j \
    --map "Maps\\RS-ORDER-05-$SC.w3m" --seconds "$SECS" --preload-names "rs-o5-$SC.txt" --output "$OUT/$name.jsonl" "${EXTRA[@]}" > "$OUT/$name.log" 2>&1
  echo "$(date -Is) end $name exit $?"
done
