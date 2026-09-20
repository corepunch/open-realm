#!/usr/bin/env bash
# bundle_mingw_dlls.sh — copy the MSYS2 runtime DLL closure next to the game.
#
# Usage: bundle_mingw_dlls.sh <dest_dir> <binary> [<binary> ...]
#
# Runs `ldd` over every exe/dll, copies each dependency that lives under an
# MSYS2 environment prefix (/mingw64, /ucrt64, /clang64, /mingw32) into
# <dest_dir>, and fails loudly when a dependency is unresolved ("not found")
# or when a required runtime DLL (SDL2, zlib, libepoxy) is absent from the
# closure. System DLLs (C:/Windows/...) are never copied: they belong to the OS.
#
# The third-party DLLs must sit beside the exe because Windows resolves DLLs
# from the executable directory, and a clean Windows install has no MSYS2.
set -euo pipefail

# Required at runtime: windowing/input, compression (MPQ), GL dispatch.
# Checked case-insensitively; a missing entry fails the release job instead
# of shipping a zip that cannot start (issue #462).
REQUIRED_DLLS="sdl2.dll zlib1.dll libepoxy-0.dll"

dest="${1:?usage: bundle_mingw_dlls.sh <dest_dir> <binary> ...}"
shift
[ "$#" -gt 0 ] || { echo "bundle_mingw_dlls.sh: no binaries given" >&2; exit 1; }
mkdir -p "$dest"

# Our own engine DLLs are bundled separately; put $dest on PATH so ldd
# resolves them there instead of reporting "not found" for them.
export PATH="$dest:$PATH"

tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT

# Collect every `ldd` "name => path" mapping over all binaries.
for bin in "$@"; do
    ldd "$bin" >> "$tmp"
done

if grep -qi "not found" "$tmp"; then
    echo "bundle_mingw_dlls.sh: unresolved dependencies:" >&2
    grep -i "not found" "$tmp" | sort -u >&2
    exit 1
fi

# Keep only MSYS2 environment DLLs; system and already-bundled paths drop out.
# Typical lines: "  SDL2.dll => /mingw64/bin/SDL2.dll (0x...)".
# BZ_MINGW_PREFIX_RE overrides the prefix match in tests (real MSYS2 paths
# like /mingw64 cannot be fabricated on Linux/macOS).
PREFIX_RE="${BZ_MINGW_PREFIX_RE:-^/(mingw64|mingw32|ucrt64|clang64)/bin/}"
grep "=>" "$tmp" | awk '{print $3}' | sort -u | grep -E "$PREFIX_RE" | \
while IFS= read -r dll; do
    cp -u "$dll" "$dest/" 2>/dev/null || cp "$dll" "$dest/"
    echo "bundled: $(basename "$dll")"
done

# Regression guard: the archive must be runnable out of the box.
missing=0
for want in $REQUIRED_DLLS; do
    found=0
    for have in "$dest"/*.dll; do
        [ -e "$have" ] || continue
        if [ "$(echo "$(basename "$have")" | tr 'A-Z' 'a-z')" = "$want" ]; then found=1; break; fi
    done
    if [ "$found" -eq 0 ]; then echo "bundle_mingw_dlls.sh: required $want not bundled" >&2; missing=1; fi
done
[ "$missing" -eq 0 ] || exit 1
echo "bundle_mingw_dlls.sh: OK ($(ls "$dest"/*.dll | wc -l) DLLs in $dest)"
