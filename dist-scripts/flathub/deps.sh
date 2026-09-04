#!/usr/bin/env bash
set -euo pipefail

REMOTE_URL="https://flathub.org/repo/flathub.flatpakrepo"

if ! command -v flatpak-builder >/dev/null 2>&1; then
  echo "flatpak-builder is required on the host." >&2
  exit 1
fi

flatpak remote-add --user --if-not-exists flathub "$REMOTE_URL"
flatpak install --user -y flathub \
  org.freedesktop.Platform//25.08 \
  org.freedesktop.Sdk//25.08
