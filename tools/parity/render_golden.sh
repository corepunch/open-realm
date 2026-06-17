#!/bin/bash
#
# render_golden.sh - golden-image regression test for the MDX renderer.
#
# Renders each model in golden_manifest.txt to a deterministic PNG (via
# `mdxtool -o`, fixed frame + seeded RNG) and compares it to a committed
# reference under tools/parity/golden/ using `imgdiff`. Fails (non-zero exit)
# if any render drifts beyond the threshold.
#
# Usage:
#   tools/parity/render_golden.sh [--update] [--threshold <mean>] [--data <dir>]
#     --update        (re)generate the golden references instead of comparing
#     --threshold T   mean abs per-channel diff allowed (default 2.0)
#     --data <dir>    data dir (default: data/Warcraft III)
#
# Requires a display/GL (mdxtool opens a window) -> run locally, not in headless CI.
set -uo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
PARITY="$REPO/tools/parity"
GOLDEN="$PARITY/golden"
MANIFEST="$PARITY/golden_manifest.txt"
MDXTOOL="$REPO/build/bin/mdxtool"
IMGDIFF="$REPO/build/bin/imgdiff"
DATA="$REPO/data/Warcraft III"
THRESHOLD=2.0
UPDATE=0
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

while [ $# -gt 0 ]; do
  case "$1" in
    --update) UPDATE=1; shift;;
    --threshold) THRESHOLD="$2"; shift 2;;
    --data) DATA="$2"; shift 2;;
    -h|--help) sed -n '2,18p' "$0"; exit 0;;
    *) echo "render_golden.sh: unknown arg '$1'" >&2; exit 2;;
  esac
done

[ -x "$MDXTOOL" ] || { echo "missing $MDXTOOL — run 'make mdxtool'" >&2; exit 2; }
[ -x "$IMGDIFF" ] || { echo "missing $IMGDIFF — run 'make imgdiff'" >&2; exit 2; }
mkdir -p "$GOLDEN"

pass=0; fail=0; updated=0
while IFS= read -r line; do
  # strip comments / blank lines
  line="${line%%#*}"
  [ -z "${line// }" ] && continue

  name="$(echo "$line"  | awk -F'|' '{gsub(/^ *| *$/,"",$1); print $1}')"
  mpq="$(echo "$line"   | awk -F'|' '{gsub(/^ *| *$/,"",$2); print $2}')"
  model="$(echo "$line" | awk -F'|' '{gsub(/^ *| *$/,"",$3); print $3}')"
  extra="$(echo "$line" | awk -F'|' '{gsub(/^ *| *$/,"",$4); print $4}')"
  [ -z "$name" ] && continue

  out="$WORK/$name.png"
  ref="$GOLDEN/$name.png"

  if ! "$MDXTOOL" -mpq "$DATA/$mpq" -model "$model" $extra -o "$out" >"$WORK/$name.log" 2>&1; then
    echo "RENDER-FAIL $name"; tail -3 "$WORK/$name.log" | sed 's/^/    /'; fail=$((fail+1)); continue
  fi
  if [ ! -f "$out" ]; then
    echo "RENDER-FAIL $name (no output)"; fail=$((fail+1)); continue
  fi

  if [ "$UPDATE" = 1 ]; then
    cp "$out" "$ref"; echo "UPDATED  $name -> $ref"; updated=$((updated+1)); continue
  fi
  if [ ! -f "$ref" ]; then
    echo "NO-GOLDEN $name (run with --update to create $ref)"; fail=$((fail+1)); continue
  fi
  if "$IMGDIFF" "$ref" "$out" --threshold "$THRESHOLD" --diff "$WORK/$name.diff.png" 2>"$WORK/$name.cmp"; then
    echo "PASS     $name   $(sed -n 's/.*mean=/mean=/p' "$WORK/$name.cmp")"; pass=$((pass+1))
  else
    echo "FAIL     $name   $(sed -n 's/.*mean=/mean=/p' "$WORK/$name.cmp")"
    cp "$out" "$PARITY/_last_fail_$name.png" 2>/dev/null || true
    cp "$WORK/$name.diff.png" "$PARITY/_last_fail_$name.diff.png" 2>/dev/null || true
    fail=$((fail+1))
  fi
done < "$MANIFEST"

echo "----"
if [ "$UPDATE" = 1 ]; then
  echo "render_golden: updated $updated reference(s) in $GOLDEN"
  exit 0
fi
echo "render_golden: $pass passed, $fail failed"
[ "$fail" = 0 ]
