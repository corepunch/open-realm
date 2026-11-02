#!/bin/bash
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research
cd "$(dirname "$0")/../../.."
P=/home/lofcz/.local/share/uv/tools/frida-tools/bin/python
L=$R/_env/live.lock
MAP='Maps\RS-MAP-06.2-ui_route.w3m'
C=(--data /run/media/lofcz/ssd_external/Games/w3-research --remote 127.0.0.1:27048 --x11-display :97 --map "$MAP")
echo "$(date -Is) control"
flock $L $P tools/frida/research/sep03_map06_trace.py --mode observe "${C[@]}" --seconds 160 --continue-at 80 --preload-names rs-ui_route.txt --output $R/MAP-06.2/captures/ui_route-control-observe-1.jsonl > $R/MAP-06.2/captures/ui_route-control-observe-1.log 2>&1
echo "$(date -Is) saveload"
flock $L $P tools/frida/research/sep03_map06_trace.py --mode observe "${C[@]}" --seconds 250 --continue-at 80 --delete-save RSUI --preload-names rs-ui_route.txt \
  --ui-action 85.5:key:F10 --ui-action 86.5:key:s --ui-action 87.5:type:RSUIROUTE --ui-action 89.5:key:Return --ui-action 91:shot \
  --ui-action 120:key:F10 --ui-action 121:key:l --ui-action 122:shot --ui-action 122.5:click:366,204 --ui-action 123.5:click:353,414 --ui-action 125:shot \
  --ui-action 160:shot --ui-action 165:key:space --ui-action 170:key:space --ui-action 175:key:space --ui-action 180:shot --ui-action 230:shot \
  --output $R/MAP-06.2/captures/ui_route-saveload-observe-1.jsonl > $R/MAP-06.2/captures/ui_route-saveload-observe-1.log 2>&1
echo "$(date -Is) done"
