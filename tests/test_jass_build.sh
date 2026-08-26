#!/bin/sh
# A header-only ABI change must rebuild the VM that dereferences game edicts and clients.
set -eu
lib=$1
# Otherwise an unrelated pending rebuild could make every pretend-new header appear to work.
make --no-print-directory -q "$lib"
for header in common/shared.h server/game.h games/warcraft-3/game/g_local.h games/warcraft-3/game/g_shared.h; do
    output=$(make --no-print-directory -n -W "$header" "$lib")
    case "$output" in
        *'echo "[jass]"'*) ;;
        *) echo "FAIL: $header does not rebuild $lib" >&2; exit 1 ;;
    esac
done
echo "JASS header rebuild dependencies: PASS"
