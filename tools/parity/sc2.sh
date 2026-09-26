#!/bin/bash
# Launch an installed StarCraft II map in OpenRealm, retaining evidence per run.
set -euo pipefail

usage() {
    echo 'Usage: SC2DATA=/path/to/StarCraft2 tools/parity/sc2.sh [--map=slug|archive/path] [--list|--select] [--refresh]'
    echo 'Defaults to TRaynor01. --list prints installed maps; --select opens the searchable picker.'
    echo 'Environment: SC2_BINARY, SC2_PARITY_LOGS, SC2_DRY_RUN=1'
}
fail() { echo "sc2: $*" >&2; exit 2; }
if [[ ${1:-} == --help || ${1:-} == -h ]]; then usage; exit 0; fi
map=traynor01 action=resolve map_set=0 refresh=0
for arg in "$@"; do
    case "$arg" in
        --list|--select)
            [[ $action == resolve ]] || fail 'Choose either --list or --select'
            action=${arg#--} ;;
        --refresh) refresh=1 ;;
        --map=*)
            [[ $map_set == 0 ]] || fail 'Map specified twice'
            map=${arg#--map=} map_set=1
            [[ -n $map ]] || fail '--map needs a value' ;;
        --help|-h) usage; exit 0 ;;
        --*) fail "Unknown option: $arg" ;;
        *)
            [[ $map_set == 0 ]] || fail 'Map specified twice'
            map=$arg map_set=1 ;;
    esac
done
[[ $action == resolve || $map_set == 0 ]] || fail '--map cannot be combined with --list or --select'
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
data=${SC2DATA:?Set SC2DATA to your StarCraft II installation}
data=$(cd -- "$data" && pwd)
catalog=(python3 "$root/tools/parity/sc2_maps.py" --data "$data")
[[ $refresh == 0 ]] || catalog+=(--refresh)
if [[ $action == list ]]; then
    exec "${catalog[@]}" --list
elif [[ $action == select ]]; then
    selection=$(mktemp)
    trap 'rm -f -- "$selection"' EXIT
    "${catalog[@]}" --select --output "$selection"
    IFS= read -r map < "$selection"
else
    case $map in
        *.SC2Map|*.sc2map|*.SC2Components|*.sc2components|*.s2ma|*.S2MA) ;;
        *) map=$("${catalog[@]}" --resolve "$map") ;;
    esac
fi
binary=${SC2_BINARY:-$root/build/bin/opensc2}
[[ -x $binary ]] || { echo 'Build first: make opensc2' >&2; exit 1; }
cmd=("$binary" -data "$data" +map "$map")
printf 'Working directory: %s\nCommand: ' "$data"
printf '%q ' "${cmd[@]}"; printf '\n'
[[ ${SC2_DRY_RUN:-0} != 1 ]] || exit 0

logs=${SC2_PARITY_LOGS:-$root/build/parity/logs}
mkdir -p -- "$logs"
log="$logs/$(date -u +%Y%m%dT%H%M%SZ)-sc2-$$.log"
echo "Log: $log"
{
    printf 'UTC: %s\nData: %s\nMap: %s\n' "$(date -u +%FT%TZ)" "$data" "$map"
    git -C "$root" rev-parse HEAD
    git -C "$root" status --short
    printf 'Command: '; printf '%q ' "${cmd[@]}"; printf '\n'
} > "$log"
cd -- "$data"
"${cmd[@]}" 2>&1 | tee -a "$log"
