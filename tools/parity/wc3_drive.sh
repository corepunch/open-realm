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
  uiwait NAME [SECONDS]     wait until a frame named NAME is visible, enabled, interactive and hit (default 20 s)
  mark                      remember the current end of the game log; waitlog only looks at lines after it
  waitlog SECONDS REGEX...  wait until a log line after the last mark matches the extended REGEX (rest of the line)
  as NAME VERB [ARGS]       run VERB on a second instance whose state is "$WC3_DRIVE_STATE-NAME"
  run SCENARIO              replay a file of verbs, one per line, plus "wait SECONDS"; stops at the first failure
                            and stops the instances the scenario started (their logs are kept)
Environment: WC3DATA (start), WC3_BINARY, WC3_DRIVE_STATE (one per instance), WC3_DRIVE_WIDTH/HEIGHT (1280x800)
EOF
}
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
state=${WC3_DRIVE_STATE:-$root/build/parity/drive}
log=$state/game.log
W=${WC3_DRIVE_WIDTH:-1280} H=${WC3_DRIVE_HEIGHT:-800}
fail() { echo "wc3_drive: $*" >&2; exit 1; }
# A killed game whose parent is gone stays a zombie when PID 1 never reaps orphans (a container started with
# `tail -f /dev/null`, as CI does); kill -0 still succeeds for it, so a zombie counts as stopped.
alive() {
    local stat
    kill -0 "$1" 2>/dev/null || return 1
    stat=$(cat "/proc/$1/stat" 2>/dev/null) || return 0
    stat=${stat##*) }
    [[ ${stat%% *} != Z ]]
}
attach() {
    [[ -s $state/env ]] || fail 'not running (use start)'
    set -a; . "$state/env"; set +a
    unset WAYLAND_DISPLAY
}
# Milliseconds since the epoch; EPOCHREALTIME's separator follows the locale.
now_ms() { local t=${EPOCHREALTIME//[.,]/}; echo $((t / 1000)); }
xdo() { xdotool "$@" || fail "xdotool $1 failed"; }
logsize() { stat -c %s "$log" 2>/dev/null || echo 0; }
since() { tail -c +"$(($1 + 1))" "$log" 2>/dev/null; }
# console TEXT [PATTERN [MILLISECONDS]]: type TEXT into the console, wait up to MILLISECONDS (default 10000) for a
# log line matching PATTERN (or 0.5 s without one) and print what the game logged meanwhile.
console() {
    attach
    local off end
    off=$(logsize) end=$(($(now_ms) + ${3:-10000}))
    xdo key --clearmodifiers grave; sleep 0.3
    xdo type --delay 20 -- "$1"; sleep 0.1
    xdo key --clearmodifiers Return; sleep 0.2
    xdo key --clearmodifiers grave
    [[ -n ${2:-} ]] || sleep 0.5
    until [[ -z ${2:-} ]] || since "$off" | grep -aq -- "$2"; do
        [[ $(now_ms) -lt $end ]] || break
        sleep 0.1
    done
    since "$off"
}
# ready NAME < uidump lines: true when a frame named exactly NAME is visible, enabled, interactive and hit at its
# centre. Only the structured fields before text="..." are read, so a label cannot fake them.
ready() {
    awk -v name="$1" '/^UIFRAME / {
        head = $0; sub(/ text=".*/, "", head); n = split(head, f, " "); split("", kv)
        for (i = 2; i <= n; i++) { eq = index(f[i], "="); if (eq) kv[substr(f[i], 1, eq - 1)] = substr(f[i], eq + 1) }
        if (kv["name"] == name && kv["visible"] == "1" && kv["enabled"] == "1" && kv["interactive"] == "1" &&
            kv["hit"] == "1") found = 1
    } END { exit !found }'
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
    rm -f "$state/mark"
    export OPENREALM_UI_TEST=${OPENREALM_UI_TEST:-1} WC3DATA
    # The console opens with the key that types ` on a US keyboard; pin that layout so a desktop's
    # XKB_DEFAULT_* settings (e.g. de, fr) cannot remap it on the private display.
    XKB_DEFAULT_LAYOUT=us XKB_DEFAULT_VARIANT= XKB_DEFAULT_OPTIONS= setsid gamescope --backend headless -W "$W" -H "$H" -r 30 -- "$0" _inner "$@" +set vid_fullscreen 0 \
        > "$log" 2>&1 < /dev/null &
    pid=$!
    echo "$pid" > "$state/pid"
    end=$((SECONDS + ${WC3_DRIVE_TIMEOUT:-60}))
    until grep -aq 'OpenWarcraft3 initialized' "$log"; do
        if [[ $SECONDS -ge $end ]] || ! kill -0 "$pid" 2>/dev/null; then
            tail -5 "$log" >&2
            "$0" stop >/dev/null  # kill the whole process group; the log stays for diagnosis
            fail "start failed, log: $log"
        fi
        sleep 0.5
    done
    attach
    xdo mousemove $((W / 2)) $((H / 2))  # a pointer left in a corner edge-scrolls the camera
    echo "started: $log" ;;
stop)
    if [[ -s $state/pid ]]; then
        pid=$(cat "$state/pid")
        kill -- -"$pid" 2>/dev/null || kill "$pid" 2>/dev/null
        for _ in 1 2 3 4 5 6 7 8 9 10; do alive "$pid" || break; sleep 0.2; done
        kill -9 -- -"$pid" 2>/dev/null
    fi
    rm -f "$state/pid" "$state/env"; echo stopped ;;
status) [[ -s $state/pid ]] && alive "$(cat "$state/pid")" && echo running || { echo 'not running'; exit 1; } ;;
log) tail -n "${1:-20}" "$log" ;;
shot)
    attach; f=$state/shots/${1:-$(date +%H%M%S)}.png; rm -f "$f"
    gamescopectl screenshot "$f" >/dev/null 2>&1
    for _ in $(seq 20); do [[ -s $f ]] && break; sleep 0.25; done
    [[ -s $f ]] || fail "no screenshot written to $f"
    echo "$f" ;;
move) attach; xdo mousemove "$1" "$2" ;;
click) # approach from the side so the game sees a motion event before the press
    attach; xdo mousemove "$(($1 - 2))" "$2"; sleep 0.2; xdo mousemove "$1" "$2"; sleep 0.2
    xdo mousedown "${3:-1}"; sleep 0.08; xdo mouseup "${3:-1}" ;;
rclick) exec "$0" click "$1" "$2" 3 ;;
dclick) attach; xdo mousemove "$1" "$2"; sleep 0.3; xdo click --repeat 2 --delay 120 1 ;;
key) attach; for k in "$@"; do xdo key --clearmodifiers "$k"; sleep 0.1; done ;;
type) attach; xdo type --delay 30 -- "$*" ;;
chat) "$0" key Return && sleep 0.2 && "$0" type "$@" && sleep 0.1 && "$0" key Return ;;
cmd) console "$*" ;;
ui) out=$(console "uidump $*" '^UIDUMP end') || exit 1; grep -a '^UI\(DUMP\|FRAME\)' <<< "$out" ;;
uiclick)
    out=$(console "click $*" '^UICLICK\|Unknown command "click') || exit 1
    [[ $out == *'Unknown command "click'* ]] && fail 'the game has no click command: start it with OPENREALM_UI_TEST=1'
    out=$(grep -a '^UICLICK' <<< "$out")
    echo "$out"; [[ $out == *'UICLICK ok'* ]] ;;
uiwait)
    name=${1:?uiwait needs a frame name}; limit=${2:-20}
    [[ $limit =~ ^[0-9]+$ ]] || fail 'usage: uiwait NAME [SECONDS]'
    end=$(($(now_ms) + limit * 1000))
    while left=$((end - $(now_ms))); [[ $left -gt 0 ]]; do
        out=$(console "uidump $name" '^UIDUMP end' "$left") || exit 1
        ready "$name" <<< "$out" && { echo "uiwait: $name ready"; exit 0; }
    done
    fail "uiwait: $name not visible, enabled, interactive and hit after ${limit}s" ;;
mark) logsize > "$state/mark" ;;
waitlog)
    [[ ${1:-} =~ ^[0-9]+$ && $# -ge 2 ]] || fail 'usage: waitlog SECONDS REGEX...'
    limit=$1; shift; re=$*; end=$((SECONDS + limit))
    from=$(cat "$state/mark" 2>/dev/null || echo 0)
    until since "$from" | grep -aqE -- "$re"; do
        [[ $SECONDS -lt $end ]] || fail "waitlog: no log line matching '$re' after ${limit}s"
        sleep 0.25
    done
    since "$from" | grep -aE -- "$re" | tail -1 ;;
as)
    [[ ${1:-} =~ ^[A-Za-z0-9_-]+$ && -n ${2:-} ]] || fail 'usage: as NAME VERB [ARGS]'
    name=$1; shift
    WC3_DRIVE_STATE=$state-$name exec "$0" "$@" ;;
run)
    [[ -f ${1:-} ]] || fail "no scenario file: ${1:-}"
    n=0 child= owned=()
    # Stop every instance this scenario started; logs and screenshots stay in their state directories.
    cleanup() {
        local who
        [[ -n $child ]] && kill "$child" 2>/dev/null
        for who in "${owned[@]}"; do
            if [[ $who == . ]]; then "$0" stop; else "$0" as "$who" stop; fi >/dev/null
            echo "wc3_drive: stopped the $([[ $who == . ]] && echo main || echo "$who") instance started by the scenario," \
                 "log: $state$([[ $who == . ]] || echo "-$who")/game.log" >&2
        done
    }
    abort() { echo "wc3_drive: scenario stopped at line $n: $*" >&2; cleanup; exit 1; }
    trap 'abort interrupted' INT TERM
    # Children run in the background so a signal interrupts `wait` at once.
    while read -r -a words || [[ ${#words[@]} -gt 0 ]]; do
        n=$((n + 1))
        [[ ${#words[@]} -eq 0 || ${words[0]} == \#* ]] && continue
        if [[ ${words[0]} == wait ]]; then
            [[ ${#words[@]} -eq 2 && ${words[1]} =~ ^[0-9]+([.][0-9]+)?$ ]] || abort "${words[*]} (usage: wait SECONDS)"
            sleep "${words[1]}" & child=$!; wait "$child"; child=
            continue
        fi
        [[ ${words[0]} == start ]] && owned+=(.)
        [[ ${words[0]} == as && ${words[2]:-} == start ]] && owned+=("${words[1]}")
        "$0" "${words[@]}" < /dev/null & child=$!
        wait "$child" || abort "${words[*]}"
        child=
    done < "$1" ;;
-h|--help|help) usage ;;
*) usage >&2; exit 2 ;;
esac
