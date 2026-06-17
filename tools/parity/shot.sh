#!/bin/bash
#
# shot.sh - launch a build and capture its window to a PNG (macOS).
#
# Captures the real GL window reliably by window-id (works even when occluded),
# so you don't have to fight window focus/overlap. Can capture our build or the
# original Legacy client, and can put them side by side for parity comparison.
#
# Usage:
#   tools/parity/shot.sh [options]
#     --app ours|legacy|both   which client (default: ours)
#     -o, --out <file.png>     output path (default: /tmp/owc3-shot.png)
#     --data <dir>             data dir for our build (default: data/Warcraft III)
#     --screen <cmd>           UI command to launch into, e.g. menu_main, menu_options
#     --delay <sec>            seconds to wait after launch before capture (default: 6)
#     --keep                   leave the app running after capture
#
# Requires: python3 (Quartz, ships with macOS), screencapture.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
APP="ours"
OUT="/tmp/owc3-shot.png"
DATA="$REPO/data/Warcraft III"
SCREEN=""
DELAY=6
KEEP=0
LEGACY="/Applications/Warcraft III (Legacy)/Warcraft III.app/Contents/MacOS/Warcraft III"

while [ $# -gt 0 ]; do
  case "$1" in
    --app) APP="$2"; shift 2;;
    -o|--out) OUT="$2"; shift 2;;
    --data) DATA="$2"; shift 2;;
    --screen) SCREEN="$2"; shift 2;;
    --delay) DELAY="$2"; shift 2;;
    --keep) KEEP=1; shift;;
    -h|--help) sed -n '2,22p' "$0"; exit 0;;
    *) echo "shot.sh: unknown arg '$1'" >&2; exit 2;;
  esac
done

# find_window <owner-name> -> prints the largest on-screen window id for that process
find_window() {
  python3 - "$1" <<'PY'
import sys, Quartz
owner = sys.argv[1]
best, area = None, -1
for w in Quartz.CGWindowListCopyWindowInfo(Quartz.kCGWindowListOptionOnScreenOnly, Quartz.kCGNullWindowID):
    if w.get('kCGWindowOwnerName','') == owner:
        b = w['kCGWindowBounds']
        a = b['Width'] * b['Height']
        if a > area:
            area, best = a, w.get('kCGWindowNumber')
print(best if best is not None else '')
PY
}

capture_one() {
  local owner="$1" launch_pid="$2" out="$3"
  local wid=""
  for _ in $(seq 1 20); do
    wid="$(find_window "$owner" || true)"
    [ -n "$wid" ] && break
    sleep 0.5
  done
  if [ -z "$wid" ]; then
    echo "shot.sh: no window found for '$owner'" >&2
    return 1
  fi
  screencapture -x -l "$wid" "$out"
  echo "shot.sh: wrote $out (window $wid of '$owner')"
}

launch_ours() {
  local out="$1"
  local args=(-data "$DATA" -tft)
  [ -n "$SCREEN" ] && args+=("+$SCREEN")
  "$REPO/build/bin/openwarcraft3" "${args[@]}" >/tmp/shot-ours.log 2>&1 &
  local pid=$!
  sleep "$DELAY"
  capture_one "openwarcraft3" "$pid" "$out" || true
  [ "$KEEP" = 1 ] || kill "$pid" 2>/dev/null || true
}

launch_legacy() {
  local out="$1"
  if [ ! -x "$LEGACY" ]; then echo "shot.sh: Legacy client not found at $LEGACY" >&2; return 1; fi
  "$LEGACY" >/tmp/shot-legacy.log 2>&1 &
  local pid=$!
  sleep "$DELAY"
  # The Legacy bundle's window owner is "Warcraft III".
  capture_one "Warcraft III" "$pid" "$out" || true
  [ "$KEEP" = 1 ] || kill "$pid" 2>/dev/null || true
}

case "$APP" in
  ours)   launch_ours "$OUT";;
  legacy) launch_legacy "$OUT";;
  both)
    base="${OUT%.png}"
    launch_ours "${base}-ours.png"
    launch_legacy "${base}-legacy.png"
    if command -v montage >/dev/null 2>&1; then
      montage "${base}-ours.png" "${base}-legacy.png" -tile 2x1 -geometry +4+4 "${base}-sidebyside.png" \
        && echo "shot.sh: wrote ${base}-sidebyside.png"
    else
      echo "shot.sh: install imagemagick (brew install imagemagick) for a combined montage."
      echo "shot.sh: wrote ${base}-ours.png and ${base}-legacy.png"
    fi
    ;;
  *) echo "shot.sh: --app must be ours|legacy|both" >&2; exit 2;;
esac
