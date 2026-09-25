#!/usr/bin/env bash
# Launch the same installation/map in retail Wine or OpenRealm, retaining evidence per run.
set -euo pipefail

usage() {
    echo 'Usage: WC3DATA=/path/to/Warcraft tools/parity/wc3.sh retail|openrealm [roc|tft] [--map=menu|slug|archive/path] [--intro] [--list|--select] [--maps-dir=PATH] [--refresh]'
    echo 'Defaults: TFT menu. Campaign aliases fast-forward native cinematics; --intro keeps normal timing.'
    echo 'Use --list for every installed campaign map; --select opens a searchable picker.'
    echo 'Add optional loose maps with --maps-dir=PATH (repeatable). Qualified slugs: roc-human1, tft-elf1.'
    echo 'Legacy positional archive paths are also accepted. Retail cinematics require manual Escape.'
    echo 'Environment: WINEPREFIX, WINE, WC3_RENDERER=default|opengl, WC3_BINARY, WC3_PARITY_LOGS, WC3_DRY_RUN=1'
}
fail() { echo "wc3: $*" >&2; exit 2; }
if [[ ${1:-} == --help || ${1:-} == -h ]]; then usage; exit 0; fi
[[ $# -gt 0 ]] || { usage >&2; exit 2; }
app=$1 edition=tft map=menu intro=0 edition_set=0 map_set=0 skip=0 action=resolve refresh=0
map_dirs=()
shift
case "$app" in retail|openrealm) ;; *) usage >&2; exit 2 ;; esac
for arg in "$@"; do
    case "$arg" in
        roc|tft)
            [[ $edition_set == 0 ]] || fail 'Edition specified twice'
            edition=$arg edition_set=1 ;;
        --intro) intro=1 ;;
        --list|--select)
            [[ $action == resolve ]] || fail 'Choose either --list or --select'
            action=${arg#--} ;;
        --refresh) refresh=1 ;;
        --maps-dir=*)
            [[ -n ${arg#--maps-dir=} ]] || fail '--maps-dir needs a directory'
            map_dirs+=("--maps-dir=${arg#--maps-dir=}") ;;
        --help|-h) usage; exit 0 ;;
        --map=*)
            [[ $map_set == 0 ]] || fail 'Map specified twice'
            map=${arg#--map=} map_set=1
            [[ -n $map ]] || fail '--map needs a value' ;;
        --*) fail "Unknown option: $arg" ;;
        *)
            [[ $map_set == 0 ]] || fail 'Map specified twice'
            map=$arg map_set=1 ;;
    esac
done
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
data=${WC3DATA:?Set WC3DATA to your Warcraft III installation}
data=$(cd -- "$data" && pwd)
[[ $action == resolve || $map_set == 0 ]] || fail '--map cannot be combined with --list or --select'
catalog=(python3 "$root/tools/parity/wc3_maps.py" --data "$data" --edition "$edition" "${map_dirs[@]+"${map_dirs[@]}"}")
[[ $refresh == 0 ]] || catalog+=(--refresh)
if [[ $action == list ]]; then
    exec "${catalog[@]}" --list
elif [[ $action == select ]]; then
    selection=$(mktemp)
    trap 'rm -f -- "$selection"' EXIT
    "${catalog[@]}" --select --output "$selection"
    IFS=$'\t' read -r edition map < "$selection"
    [[ -z $map || $intro == 1 ]] || skip=1
elif [[ $map == menu ]]; then
    map=
elif [[ $map == *.w3m || $map == *.w3x ]]; then
    : # Explicit paths keep normal cinematic timing.
else
    resolved=$("${catalog[@]}" --resolve "$map")
    IFS=$'\t' read -r edition map <<< "$resolved"
    [[ -z $map || $intro == 1 ]] || skip=1
fi
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
    cmd+=(+set skip_cutscene "$skip")
    if [[ -n $map ]]; then cmd+=(+map "$map"); else cmd+=(+menu_main); fi
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
