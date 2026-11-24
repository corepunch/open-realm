# syntax=docker/dockerfile:1

# PortMaster-supported CFWs use an Ubuntu 22.04-compatible libc baseline.
FROM ubuntu:22.04 AS builder

ARG TARGETARCH
# Set to 0 to omit the FFmpeg runtime libraries from the PortMaster package.
ARG BUNDLE_FFMPEG_LIBS=1
ARG FFMPEG_PREFIX=/opt/ffmpeg-classic
ARG FFMPEG_BUILD_JOBS=2
ENV DEBIAN_FRONTEND=noninteractive

# This PortMaster package currently targets aarch64 only.
RUN test "${TARGETARCH}" = "arm64"
RUN case "${BUNDLE_FFMPEG_LIBS}" in 0|1) ;; *) echo "BUNDLE_FFMPEG_LIBS must be 0 or 1" >&2; exit 1 ;; esac

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
    dpkg-dev \
    nasm \
    yasm \
    zip \
    unzip \
 && rm -rf /var/lib/apt/lists/*

# Verify that the container itself is actually ARM64.
RUN test "$(dpkg --print-architecture)" = "arm64"

# Build Ubuntu's FFmpeg source package with only the software codecs and
# containers required for classic Warcraft III campaign movies and music.
# BLZ0/DivX movies decode through FFmpeg's MPEG-4 Part 2 decoder; no Nox VQA3,
# VAAPI, or other hardware-video support is needed.
RUN set -eux; \
    printf '%s\n' \
      'Types: deb-src' \
      'URIs: http://ports.ubuntu.com/ubuntu-ports' \
      'Suites: jammy jammy-updates jammy-security' \
      'Components: main universe restricted multiverse' \
      'Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg' \
      > /etc/apt/sources.list.d/ubuntu-src.sources; \
    apt-get update; \
    mkdir -p /tmp/ffmpeg-source; \
    cd /tmp/ffmpeg-source; \
    apt-get source ffmpeg; \
    FFMPEG_SOURCE="$(find . -mindepth 1 -maxdepth 1 -type d -name 'ffmpeg-*' -print -quit)"; \
    test -n "${FFMPEG_SOURCE}"; \
    cd "${FFMPEG_SOURCE}"; \
    ./configure \
      --prefix="${FFMPEG_PREFIX}" \
      --arch=aarch64 \
      --target-os=linux \
      --enable-shared \
      --disable-static \
      --disable-programs \
      --disable-doc \
      --disable-debug \
      --enable-pic \
      --disable-autodetect \
      --disable-everything \
      --enable-avformat \
      --enable-avcodec \
      --enable-avutil \
      --enable-swscale \
      --enable-swresample \
      --enable-demuxer=avi,mp3 \
      --enable-decoder=mpeg4,mp3,mp3float \
      --enable-parser=mpeg4video,mpegaudio \
      --enable-protocol=file; \
    make -j"${FFMPEG_BUILD_JOBS}"; \
    make install; \
    mkdir -p /opt/ffmpeg-classic-licenses; \
    cp COPYING.LGPLv2.1 /opt/ffmpeg-classic-licenses/; \
    rm -rf /tmp/ffmpeg-source /var/lib/apt/lists/*

ENV PKG_CONFIG_PATH=${FFMPEG_PREFIX}/lib/pkgconfig
ENV LD_LIBRARY_PATH=${FFMPEG_PREFIX}/lib

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
      FFMPEG=1 \
      openwarcraft3 \
      build/bin/isoextract \
      build/bin/wc3setup

# ---------------------------------------------------------------------------
# Stage OpenRealm build products
# ---------------------------------------------------------------------------

RUN mkdir -p /src/release && \
    cp build/bin/openwarcraft3 \
       /src/release/openwarcraft3.aarch64 && \
    cp -a build/lib/. /src/release/ && \
    cp build/bin/isoextract /src/release/isoextract.aarch64 && \
    cp build/bin/wc3setup /src/release/wc3setup.aarch64

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
        "${PKG_DIR}/ffmpeg.aarch64" \
        "${PKG_DIR}/gamefiles/warcraft-3" \
        "${PKG_DIR}/licenses"; \
    \
    # Verify the WC3 PortMaster files exist.
    test -f "${PM_SRC}/open-realm-wc3.sh"; \
    test -f "${PM_SRC}/openrealm-wc3.gptk2.ini"; \
    test -f "${PM_SRC}/port.json"; \
    test -f "${PM_SRC}/gameinfo.xml"; \
    test -f "${PM_SRC}/README.md"; \
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
    cp "${PM_SRC}/README.md" "${PKG_DIR}/README.md"; \
    cp "${PM_SRC}/openrealm-wc3.gptk2.ini" \
       "${PKG_DIR}/openrealm-wc3.gptk2.ini"; \
    cp "${PM_SRC}/WC3-ISO-IMPORT.md" "${PKG_DIR}/WC3-ISO-IMPORT.md"; \
    \
    # Main ARM64 executable.
    cp /src/release/openwarcraft3.aarch64 \
       "${PKG_DIR}/openwarcraft3.aarch64"; \
    chmod +x "${PKG_DIR}/openwarcraft3.aarch64"; \
    cp /src/release/isoextract.aarch64 /src/release/wc3setup.aarch64 "${PKG_DIR}/"; \
    chmod +x "${PKG_DIR}/isoextract.aarch64" "${PKG_DIR}/wc3setup.aarch64"; \
    \
    # Engine/game shared libraries.
    for lib in /src/release/*.so*; do \
        [ -e "${lib}" ] || continue; \
        cp -a "${lib}" "${PKG_DIR}/libs.aarch64/"; \
    done; \
    \
    # Keep FFmpeg separate from the engine's own libraries. The launcher adds
    # ffmpeg.${DEVICE_ARCH} to LD_LIBRARY_PATH for the executable and its
    # transitive FFmpeg dependencies.
    if [ "${BUNDLE_FFMPEG_LIBS}" = "1" ]; then \
        cp -a /opt/ffmpeg-classic/lib/libav*.so* \
            /opt/ffmpeg-classic/lib/libsw*.so* "${PKG_DIR}/ffmpeg.aarch64/"; \
        mkdir -p "${PKG_DIR}/licenses/ffmpeg"; \
        cp /opt/ffmpeg-classic-licenses/COPYING.LGPLv2.1 \
            "${PKG_DIR}/licenses/ffmpeg/"; \
    fi; \
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
