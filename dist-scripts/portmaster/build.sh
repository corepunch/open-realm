#!/usr/bin/env bash
set -euo pipefail

# May need this once on x86:
# docker run --privileged --rm tonistiigi/binfmt --install arm64

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"

IMAGE_NAME="openrealm-portmaster"
CONTAINER_NAME="openrealm-portmaster-extract"
ZIP_NAME="open-realm-wc3.zip"

mkdir -p "${BUILD_DIR}"

# Linux x86_64 hosts need ARM64 emulation.
if [ "$(uname -m)" != "aarch64" ]; then
    if ! docker run --rm --platform linux/arm64 ubuntu:22.04 true >/dev/null 2>&1; then
        echo "ARM64 Docker emulation is not installed."
        echo
        echo "Run once:"
        echo "  docker run --privileged --rm tonistiigi/binfmt --install arm64"
        exit 1
    fi
fi

command -v zip >/dev/null || {
    echo "ERROR: zip is required."
    exit 1
}

command -v unzip >/dev/null || {
    echo "ERROR: unzip is required."
    exit 1
}

echo "Building ARM64 PortMaster image..."

docker buildx build \
    --platform linux/arm64 \
    --load \
    -f "${SCRIPT_DIR}/Dockerfile" \
    -t "${IMAGE_NAME}" \
    "${REPO_ROOT}"

docker rm -f "${CONTAINER_NAME}" >/dev/null 2>&1 || true

docker create \
    --name "${CONTAINER_NAME}" \
    "${IMAGE_NAME}" >/dev/null

cleanup() {
    docker rm -f "${CONTAINER_NAME}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "Copying PortMaster zip..."

rm -f "${BUILD_DIR}/${ZIP_NAME}"

docker cp \
    "${CONTAINER_NAME}:/src/release/${ZIP_NAME}" \
    "${BUILD_DIR}/${ZIP_NAME}"

if [ ! -s "${BUILD_DIR}/${ZIP_NAME}" ]; then
    echo "ERROR: PortMaster zip was not created:"
    echo "  ${BUILD_DIR}/${ZIP_NAME}"
    exit 1
fi

echo
echo "Created:"
echo "  ${BUILD_DIR}/${ZIP_NAME}"