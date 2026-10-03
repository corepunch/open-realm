FROM ubuntu:24.04

# Rebuild this image on dependency changes so main can publish the shared CI environment.
LABEL org.opencontainers.image.source="https://github.com/corepunch/open-realm"
LABEL org.opencontainers.image.description="OpenRealm Linux build and test dependencies for GitHub Actions"

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        git \
        libgles2-mesa-dev \
        libgl1-mesa-dri \
        libjpeg-dev \
        libsdl2-dev \
        pkg-config \
        python3 \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*
