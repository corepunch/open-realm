#!/usr/bin/env bash
set -euo pipefail

APP_ID="io.github.corepunch.OpenRealm"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
MANIFEST="${SCRIPT_DIR}/${APP_ID}.yml"
WORK_ROOT="${TMPDIR:-/tmp}/openrealm-flatpak"
BUILD_DIR="${WORK_ROOT}/build-dir"
STATE_DIR="${WORK_ROOT}/state"
REPO="${WORK_ROOT}/repo"
BUNDLE="${REPO_ROOT}/build/${APP_ID}.flatpak"

rm -rf "$WORK_ROOT" "$BUNDLE"
mkdir -p "$WORK_ROOT" "${REPO_ROOT}/build"

ostree --repo="$REPO" init --mode=archive-z2
ostree --repo="$REPO" config set core.min-free-space-percent 0

flatpak-builder \
  --user \
  --force-clean \
  --state-dir="$STATE_DIR" \
  --repo="$REPO" \
  "$BUILD_DIR" \
  "$MANIFEST"

flatpak build-update-repo "$REPO"
flatpak build-bundle \
  --runtime-repo="https://flathub.org/repo/flathub.flatpakrepo" \
  "$REPO" \
  "$BUNDLE" \
  "$APP_ID"

echo "Built: $BUNDLE"
