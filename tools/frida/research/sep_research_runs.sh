#!/bin/bash
# SEP research live runs: each launch takes and releases the shared research live lock separately.
# usage: sep_research_runs.sh JOB...   JOB = mode:variant:mapname:seconds:outdir[:flags]
set -u
REPO=$(cd "$(dirname "$0")/../../.." && pwd)
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research
PY=/home/lofcz/.local/share/uv/tools/frida-tools/bin/python
for job in "$@"; do
  IFS=: read -r mode variant map secs out flags <<<"$job"
  echo "== $(date +%T) $job"
  flock $R/_env/live.lock $PY $REPO/tools/frida/research/sep_research_trace.py $mode \
    --data /run/media/lofcz/ssd_external/Games/w3-research --map "Maps\\$map.w3m" \
    --preload-name sepres-$variant.txt --remote 127.0.0.1:27048 --x11-display :97 \
    --seconds $secs --key-at 10 18 ${flags//,/ } --output $R/$out 2>&1 | tail -3
done
