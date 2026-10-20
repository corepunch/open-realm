#!/bin/bash
# MAP-06.2 exact post-load commits (--all-movers), MAP-06.2 observer-free UI save/load, MAP-06.1 observer-free controls.
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research
cd "$(dirname "$0")/../../.."
P=/home/lofcz/.local/share/uv/tools/frida-tools/bin/python
L=$R/_env/live.lock
D=(--data /run/media/lofcz/ssd_external/Games/w3-research --remote 127.0.0.1:27048 --x11-display :97)
UI=(--ui-action 85.5:key:F10 --ui-action 86.5:key:s --ui-action 87.5:type:RSUIROUTE --ui-action 89.5:key:Return --ui-action 91:shot
    --ui-action 120:key:F10 --ui-action 121:key:l --ui-action 122:shot --ui-action 122.5:click:366,204 --ui-action 123.5:click:353,414 --ui-action 125:shot
    --ui-action 160:shot --ui-action 165:key:space --ui-action 170:key:space --ui-action 175:key:space --ui-action 180:shot --ui-action 230:shot)
O=$R/MAP-06.2/captures
echo "$(date -Is) saveload-observe-2"
flock $L $P tools/frida/research/sep03_map06_trace.py --mode observe "${D[@]}" --map 'Maps\RS-MAP-06.2-ui_route.w3m' --seconds 250 --continue-at 80 \
  --all-movers --delete-save RSUI --preload-names rs-ui_route.txt "${UI[@]}" --output $O/ui_route-saveload-observe-2.jsonl > $O/ui_route-saveload-observe-2.log 2>&1
cp -n /run/media/lofcz/ssd_external/Games/w3-research/save/Profile1/RSUIROUTE.w3z $R/MAP-06.2/saves/RSUIROUTE-saveload-observe-2.w3z
echo "$(date -Is) saveload-control-1"
flock $L $P tools/frida/research/sep03_map06_trace.py --mode control "${D[@]}" --map 'Maps\RS-MAP-06.2-ui_route.w3m' --seconds 250 --continue-at 80 \
  --delete-save RSUI --preload-names rs-ui_route.txt "${UI[@]}" --output $O/ui_route-saveload-control-1.jsonl > $O/ui_route-saveload-control-1.log 2>&1
cp -n /run/media/lofcz/ssd_external/Games/w3-research/save/Profile1/RSUIROUTE.w3z $R/MAP-06.2/saves/RSUIROUTE-saveload-control-1.w3z
echo "$(date -Is) ui_route-control-control-1"
flock $L $P tools/frida/research/sep03_map06_trace.py --mode control "${D[@]}" --map 'Maps\RS-MAP-06.2-ui_route.w3m' --seconds 160 --continue-at 80 \
  --preload-names rs-ui_route.txt --output $O/ui_route-control-control-1.jsonl > $O/ui_route-control-control-1.log 2>&1
O=$R/MAP-06.1/captures
echo "$(date -Is) changelevel-control-1"
flock $L $P tools/frida/research/sep03_map06_trace.py --mode control "${D[@]}" --map 'Maps\RS-MAP-06.1-changelevel_a.w3m' --seconds 200 --continue-at 80 \
  --continue-every 15 --preload-names rs-changelevel_a-prechange.txt,rs-changelevel_b.txt --output $O/changelevel-control-1.jsonl > $O/changelevel-control-1.log 2>&1
echo "$(date -Is) restart-control-1"
flock $L $P tools/frida/research/sep03_map06_trace.py --mode control "${D[@]}" --map 'Maps\RS-MAP-06.1-restart.w3m' --seconds 200 --continue-at 80 \
  --continue-every 15 --preload-names rs-restart-prerestart.txt --output $O/restart-control-1.jsonl > $O/restart-control-1.log 2>&1
echo "$(date -Is) done"
