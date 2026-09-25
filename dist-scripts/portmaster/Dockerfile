# syntax=docker/dockerfile:1

# PortMaster-supported CFWs use an Ubuntu 22.04-compatible libc baseline.
FROM ubuntu:22.04 AS builder

ARG TARGETARCH
ENV DEBIAN_FRONTEND=noninteractive

# This PortMaster package currently targets aarch64 only.
RUN test "${TARGETARCH}" = "arm64"

# OpenRealm WC3 native GLES3 build dependencies + packaging tools.
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    pkg-config \
    findutils \
    gawk \
    libsdl2-dev \
    libegl1-mesa-dev \
    libgles2-mesa-dev \
    zlib1g-dev \
    libjpeg-dev \
    zip \
    unzip \
 && rm -rf /var/lib/apt/lists/*

# Verify that the container itself is actually ARM64.
RUN test "$(dpkg --print-architecture)" = "arm64"

# build.sh supplies the repository root as the Docker build context.
WORKDIR /src
COPY . .

# ---------------------------------------------------------------------------
# Build OpenWarcraft3
# ---------------------------------------------------------------------------

# Native GLES3 build:
# - no GL4ES
# - no desktop OpenGL wrapper
# - no WoW build
# - no MSAA
RUN make clean && \
    make \
      BUILD=release \
      GL_BACKEND=gles3 \
      MSAA=0 \
      openwarcraft3

# ---------------------------------------------------------------------------
# Stage OpenRealm build products
# ---------------------------------------------------------------------------

RUN mkdir -p /src/release && \
    cp build/bin/openwarcraft3 \
       /src/release/openwarcraft3.aarch64 && \
    cp -a build/lib/. /src/release/

# ---------------------------------------------------------------------------
# Assemble WC3 PortMaster package
# ---------------------------------------------------------------------------

RUN set -eux; \
    \
    PM_SRC="/src/dist-scripts/portmaster"; \
    PKG_ROOT="/src/portmaster.package"; \
    PKG_DIR="${PKG_ROOT}/open-realm-wc3"; \
    \
    rm -rf "${PKG_ROOT}"; \
    mkdir -p \
        "${PKG_DIR}" \
        "${PKG_DIR}/libs.aarch64" \
        "${PKG_DIR}/gamefiles/warcraft-3" \
        "${PKG_DIR}/licenses"; \
    \
    # Verify the WC3 PortMaster files exist.
    test -f "${PM_SRC}/open-realm-wc3.sh"; \
    test -f "${PM_SRC}/openrealm-wc3.gptk2.ini"; \
    test -f "${PM_SRC}/port.json"; \
    test -f "${PM_SRC}/gameinfo.xml"; \
    \
    # Top-level PortMaster launcher.
    cp "${PM_SRC}/open-realm-wc3.sh" \
       "${PKG_ROOT}/open-realm-wc3.sh"; \
    chmod +x "${PKG_ROOT}/open-realm-wc3.sh"; \
    \
    # PortMaster metadata and controller mapping.
    cp "${PM_SRC}/port.json" \
       "${PKG_DIR}/port.json"; \
    cp "${PM_SRC}/gameinfo.xml" \
       "${PKG_DIR}/gameinfo.xml"; \
    cp "${PM_SRC}/openrealm-wc3.gptk2.ini" \
       "${PKG_DIR}/openrealm-wc3.gptk2.ini"; \
    \
    # Main ARM64 executable.
    cp /src/release/openwarcraft3.aarch64 \
       "${PKG_DIR}/openwarcraft3.aarch64"; \
    chmod +x "${PKG_DIR}/openwarcraft3.aarch64"; \
    \
    # Engine/game shared libraries.
    for lib in /src/release/*.so*; do \
        [ -e "${lib}" ] || continue; \
        cp -a "${lib}" "${PKG_DIR}/libs.aarch64/"; \
    done; \
    \
    # OpenRealm's built-in configuration/resources.
    # The executable normally resolves these relative to itself.
    if [ -d /src/build/share ]; then \
        cp -a /src/build/share "${PKG_DIR}/share"; \
    fi; \
    \
    # Optional OpenRealm licence.
    if [ -f /src/LICENSE ]; then \
        cp /src/LICENSE \
           "${PKG_DIR}/licenses/open-realm-LICENSE.txt"; \
    fi; \
    \
    # Preserve the directory where the user supplies Warcraft III data.
    touch "${PKG_DIR}/gamefiles/warcraft-3/.gitkeep"; \
    \
    # Produce final PortMaster archive.
    rm -f /src/release/open-realm-wc3.zip; \
    cd "${PKG_ROOT}"; \
    zip -9 -r /src/release/open-realm-wc3.zip .; \
    \
    # Verify archive integrity.
    unzip -t /src/release/open-realm-wc3.zip