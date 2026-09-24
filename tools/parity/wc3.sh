#!/usr/bin/env bash
# Launch the same installation/map in retail Wine or OpenRealm, retaining evidence per run.
set -euo pipefail

usage() {
    echo 'Usage: WC3DATA=/path/to/Warcraft tools/parity/wc3.sh retail|openrealm roc|tft [archive/map/path]'
    echo 'Environment: WINEPREFIX, WINE, WC3_RENDERER=default|opengl, WC3_BINARY, WC3_PARITY_LOGS, WC3_DRY_RUN=1'
}
if [[ ${1:-} == --help ]]; then usage; exit 0; fi
if [[ $# -lt 2 || $# -gt 3 ]]; then usage >&2; exit 2; fi
app=$1 edition=$2 map=${3:-}
case "$app" in retail|openrealm) ;; *) usage >&2; exit 2 ;; esac
case "$edition" in roc|tft) ;; *) usage >&2; exit 2 ;; esac
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
data=${WC3DATA:?Set WC3DATA to your Warcraft III installation}
data=$(cd -- "$data" && pwd)
export WINEPREFIX=${WINEPREFIX:-${XDG_DATA_HOME:-$HOME/.local/share}/open-realm/wine-wc3}
[[ $WINEPREFIX == /* ]] || { echo 'WINEPREFIX must be absolute' >&2; exit 2; }

if [[ $app == retail ]]; then
    wine=${WINE:-wine}
    command -v "$wine" >/dev/null || { echo "Wine not found: $wine" >&2; exit 1; }
    [[ -f $data/war3.exe ]] || { echo "Missing $data/war3.exe" >&2; exit 1; }
    cmd=("$wine" "$data/war3.exe" -window)
    case ${WC3_RENDERER:-default} in
        default) ;;
        opengl) cmd+=(-opengl) ;;
        *) echo 'WC3_RENDERER must be default or opengl' >&2; exit 2 ;;
    esac
    [[ $edition != roc ]] || cmd+=(-classic)
    [[ -z $map ]] || cmd+=(-loadfile "${map//\//\\}")
else
    binary=${WC3_BINARY:-$root/build/bin/openwarcraft3}
    [[ -x $binary ]] || { echo "Build first: make BUILD=release FFMPEG=1 openwarcraft3" >&2; exit 1; }
    cmd=("$binary" -data "$data" +set fs_expansion "$([[ $edition == tft ]] && echo 1 || echo 0)"
         +set vid_native 0 +set vid_fullscreen 0 +set vid_mode 2)
    [[ -z $map ]] || cmd+=(+map "$map")
fi
printf 'Working directory: %s\nWine prefix: %s\nCommand: ' "$data" "$WINEPREFIX"
printf '%q ' "${cmd[@]}"; printf '\n'
[[ ${WC3_DRY_RUN:-0} != 1 ]] || exit 0

logs=${WC3_PARITY_LOGS:-$root/build/parity/logs}
mkdir -p -- "$logs"
log="$logs/$(date -u +%Y%m%dT%H%M%SZ)-$app-$edition-$$.log"
echo "Log: $log"
{
    printf 'UTC: %s\nData: %s\nEdition: %s\nMap: %s\nPrefix: %s\n' "$(date -u +%FT%TZ)" "$data" "$edition" "$map" "$WINEPREFIX"
    git -C "$root" rev-parse HEAD
    git -C "$root" status --short
    [[ $app != retail ]] || "$wine" --version
    printf 'Command: '; printf '%q ' "${cmd[@]}"; printf '\n'
} > "$log"
cd -- "$data"
"${cmd[@]}" 2>&1 | tee -a "$log"
