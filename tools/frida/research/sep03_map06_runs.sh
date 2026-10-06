#!/bin/bash
# Sequential bounded research captures; each run takes and releases the global live lock.
# Usage: sep03_map06_runs.sh <outdir> <name>:<mode>:<map-variant>:<seconds>:<extra-args> ...
set -u
OUT=$1; shift
LOCK=/GitHub/wc3-analysis/reports/pathfinding-1.27/research/_env/live.lock
PY=/home/lofcz/.local/share/uv/tools/frida-tools/bin/python
cd "$(dirname "$0")/../../.."
for spec in "$@"; do
  IFS=: read -r name mode map seconds extra <<< "$spec"
  echo "$(date -Is) start $name"
  flock "$LOCK" "$PY" tools/frida/research/sep03_map06_trace.py --mode "$mode" \
    --data /run/media/lofcz/ssd_external/Games/w3-research --remote 127.0.0.1:27048 --x11-display :97 \
    --map "Maps\\$map.w3m" --seconds "$seconds" --continue-at 80 $extra --output "$OUT/$name.jsonl" > "$OUT/$name.log" 2>&1
  echo "$(date -Is) end $name exit $?"
done
