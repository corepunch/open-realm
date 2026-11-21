#!/usr/bin/env bash
# Drive OpenRealm (Warcraft III) on a private headless display: real X11 input, screenshots, UI steps by frame name.
# Nothing touches the desktop session. Linux only: needs gamescope (headless backend), gamescopectl and xdotool.
usage() {
    cat <<'EOF'
Usage: WC3DATA=/path/to/Warcraft tools/parity/wc3_drive.sh VERB [ARGS]
  start [engine args]       launch (e.g. start -tft); exports OPENREALM_UI_TEST=1 so `click` exists
  stop | status | log [N]
  shot [NAME]               screenshot, prints the PNG path
  move X Y | click X Y [BUTTON] | rclick X Y | dclick X Y (real double click)
  key KEY... | type TEXT | chat TEXT          xdotool key names: Return, Escape, F9, ctrl+a
  cmd TEXT                  run a console command, print the log lines it produced
  ui [FILTER]               uidump, prints the UIDUMP/UIFRAME lines
  uiclick NAME[#N] [BUTTON] click a frame by name; fails unless the game answers UICLICK ok
  uiwait NAME [SECONDS]     wait until NAME is visible, enabled and clickable (default 20 s)
  waitlog SECONDS REGEX...  wait until a game log line matches the extended REGEX (the rest of the line)
  as NAME VERB [ARGS]       run VERB on a second instance whose state is "$WC3_DRIVE_STATE-NAME"
  run SCENARIO              replay a file of verbs, one per line, plus "wait SECONDS"; stops at the first failure
Environment: WC3DATA (start), WC3_BINARY, WC3_DRIVE_STATE (one per instance), WC3_DRIVE_WIDTH/HEIGHT (1280x800)
EOF
}
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
state=${WC3_DRIVE_STATE:-$root/build/parity/drive}
log=$state/game.log
W=${WC3_DRIVE_WIDTH:-1280} H=${WC3_DRIVE_HEIGHT:-800}
fail() { echo "wc3_drive: $*" >&2; exit 1; }
attach() {
    [[ -s $state/env ]] || fail 'not running (use start)'
    set -a; . "$state/env"; set +a
    unset WAYLAND_DISPLAY
}
logsize() { stat -c %s "$log" 2>/dev/null || echo 0; }
since() { tail -c +"$(($1 + 1))" "$log"; }
# console TEXT [PATTERN]: type TEXT into the console, wait up to 10 s for a log line matching PATTERN
# (or 0.5 s without one) and print what the game logged meanwhile.
console() {
    attach
    local off end
    off=$(logsize) end=$((SECONDS + 10))
    xdotool key --clearmodifiers grave; sleep 0.3
    xdotool type --delay 20 -- "$1"; sleep 0.1
    xdotool key --clearmodifiers Return; sleep 0.2
    xdotool key --clearmodifiers grave
    [[ -n ${2:-} ]] || sleep 0.5
    until [[ -z ${2:-} ]] || since "$off" | grep -aq -- "$2"; do
        [[ $SECONDS -lt $end ]] || break
        sleep 0.1
    done
    since "$off"
}
mkdir -p "$state/shots" "$state/data"
verb=${1:-}; shift || true
case "$verb" in
_inner) # runs inside gamescope: record its display for the other verbs, then become the game
    env | grep -E '^(DISPLAY|GAMESCOPE_WAYLAND_DISPLAY)=' > "$state/env"
    unset WAYLAND_DISPLAY
    exec env SDL_VIDEODRIVER=x11 XDG_DATA_HOME="$state/data" "${WC3_BINARY:-$root/build/bin/openwarcraft3}" -data "$WC3DATA" "$@" ;;
start)
    [[ -n ${WC3DATA:-} ]] || fail 'Set WC3DATA to your Warcraft III installation'
    [[ -x ${WC3_BINARY:-$root/build/bin/openwarcraft3} ]] || fail 'Build first: make BUILD=release openwarcraft3'
    for tool in gamescope gamescopectl xdotool; do command -v "$tool" >/dev/null || fail "$tool not found"; done
    "$0" stop >/dev/null
    export OPENREALM_UI_TEST=${OPENREALM_UI_TEST:-1} WC3DATA
    # The console opens with the key that types ` on a US keyboard; pin that layout so a desktop's
    # XKB_DEFAULT_* settings (e.g. de, fr) cannot remap it on the private display.
    XKB_DEFAULT_LAYOUT=us XKB_DEFAULT_VARIANT= XKB_DEFAULT_OPTIONS= setsid gamescope --backend headless -W "$W" -H "$H" -r 30 -- "$0" _inner "$@" +set vid_fullscreen 0 \
        > "$log" 2>&1 < /dev/null &
    echo $! > "$state/pid"
    end=$((SECONDS + ${WC3_DRIVE_TIMEOUT:-60}))
    until grep -aq 'OpenWarcraft3 initialized' "$log"; do
        [[ $SECONDS -lt $end ]] || { tail -5 "$log" >&2; fail 'start failed'; }
        sleep 0.5
    done
    attach
    xdotool mousemove $((W / 2)) $((H / 2))  # a pointer left in a corner edge-scrolls the camera
    echo "started: $log" ;;
stop)
    if [[ -s $state/pid ]]; then
        pid=$(cat "$state/pid")
        kill -- -"$pid" 2>/dev/null || kill "$pid" 2>/dev/null
        for _ in 1 2 3 4 5 6 7 8 9 10; do kill -0 "$pid" 2>/dev/null || break; sleep 0.2; done
        kill -9 -- -"$pid" 2>/dev/null
    fi
    rm -f "$state/pid" "$state/env"; echo stopped ;;
status) [[ -s $state/pid ]] && kill -0 "$(cat "$state/pid")" 2>/dev/null && echo running || { echo 'not running'; exit 1; } ;;
log) tail -n "${1:-20}" "$log" ;;
shot)
    attach; f=$state/shots/${1:-$(date +%H%M%S)}.png; rm -f "$f"
    gamescopectl screenshot "$f" >/dev/null 2>&1
    for _ in $(seq 20); do [[ -s $f ]] && break; sleep 0.25; done
    [[ -s $f ]] || fail "no screenshot written to $f"
    echo "$f" ;;
move) attach; xdotool mousemove "$1" "$2" ;;
click) # approach from the side so the game sees a motion event before the press
    attach; xdotool mousemove "$(($1 - 2))" "$2"; sleep 0.2; xdotool mousemove "$1" "$2"; sleep 0.2
    xdotool mousedown "${3:-1}"; sleep 0.08; xdotool mouseup "${3:-1}" ;;
rclick) "$0" click "$1" "$2" 3 ;;
dclick) attach; xdotool mousemove "$1" "$2"; sleep 0.3; xdotool click --repeat 2 --delay 120 1 ;;
key) attach; for k in "$@"; do xdotool key --clearmodifiers "$k"; sleep 0.1; done ;;
type) attach; xdotool type --delay 30 -- "$*" ;;
chat) "$0" key Return; sleep 0.2; "$0" type "$@"; sleep 0.1; "$0" key Return ;;
cmd) console "$*" ;;
ui) console "uidump $*" '^UIDUMP end' | grep -a '^UI\(DUMP\|FRAME\)' ;;
uiclick)
    out=$(console "click $*" '^UICLICK\|Unknown command "click')
    [[ $out == *'Unknown command "click'* ]] && fail 'the game has no click command: start it with OPENREALM_UI_TEST=1'
    out=$(grep -a '^UICLICK' <<< "$out")
    echo "$out"; [[ $out == *'UICLICK ok'* ]] ;;
uiwait)
    name=${1:?uiwait needs a frame name}; end=$((SECONDS + ${2:-20}))
    while [[ $SECONDS -lt $end ]]; do
        console "uidump $name" '^UIDUMP end' | grep -a "^UIFRAME .* name=$name " |
            grep -aqE ' visible=1 enabled=1 .*(interactive=0| hit=1)' && { echo "uiwait: $name ready"; exit 0; }
    done
    fail "uiwait: $name not ready after ${2:-20}s" ;;
waitlog)
    [[ ${1:-} =~ ^[0-9]+$ && $# -ge 2 ]] || fail 'usage: waitlog SECONDS REGEX...'
    limit=$1; shift; re=$*; end=$((SECONDS + limit))
    until grep -aqE -- "$re" "$log" 2>/dev/null; do
        [[ $SECONDS -lt $end ]] || fail "waitlog: no log line matching '$re' after ${limit}s"
        sleep 0.25
    done
    grep -aE -- "$re" "$log" | tail -1 ;;
as)
    [[ ${1:-} =~ ^[A-Za-z0-9_-]+$ && -n ${2:-} ]] || fail 'usage: as NAME VERB [ARGS]'
    name=$1; shift
    WC3_DRIVE_STATE=$state-$name exec "$0" "$@" ;;
run)
    [[ -f ${1:-} ]] || fail "no scenario file: ${1:-}"
    n=0
    while read -r -a words || [[ ${#words[@]} -gt 0 ]]; do
        n=$((n + 1))
        [[ ${#words[@]} -eq 0 || ${words[0]} == \#* ]] && continue
        if [[ ${words[0]} == wait ]]; then sleep "${words[1]}"; continue; fi
        "$0" "${words[@]}" < /dev/null || fail "scenario stopped at line $n: ${words[*]}"
    done < "$1" ;;
-h|--help|help) usage ;;
*) usage >&2; exit 2 ;;
esac
