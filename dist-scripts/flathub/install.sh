#!/usr/bin/env bash
set -euo pipefail

APP_ID="io.github.corepunch.OpenRealm"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
BUNDLE="${1:-${REPO_ROOT}/build/${APP_ID}.flatpak}"

flatpak install --user -y "$BUNDLE"
printf 'Installed %s\nLaunch with: flatpak run %s\n' "$APP_ID" "$APP_ID"
