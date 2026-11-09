#!/usr/bin/env bash
# One-shot INDI build + unit tests for Bohr's WSL Ubuntu distro.
#
#   bash scripts/bohr-wsl-build.sh            # deps + configure + build + ctest
#   SKIP_DEPS=1 bash scripts/bohr-wsl-build.sh
#
# Env knobs: BUILD_DIR (default build/bohr), JOBS (default nproc), SKIP_DEPS=1.
# Build in the WSL filesystem (e.g. ~/src/indi), not /mnt/c, for speed and
# correct file permissions. Tests run offline with ASTROSPHERIC_API_KEY unset.
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$SRC_DIR/build/bohr}"
JOBS="${JOBS:-$(nproc)}"

if [[ "${SKIP_DEPS:-0}" != "1" ]]; then
    echo "==> Installing Ubuntu build dependencies"
    sudo apt-get update
    sudo apt-get install -y \
        git cdbs dkms cmake ninja-build fxload pkg-config \
        libev-dev libgps-dev libgsl-dev libraw-dev libusb-dev zlib1g-dev \
        libftdi-dev libjpeg-dev libkrb5-dev libnova-dev libtiff-dev \
        libfftw3-dev librtlsdr-dev libcfitsio-dev libgphoto2-dev \
        build-essential libusb-1.0-0-dev libdc1394-dev libboost-regex-dev \
        libcurl4-gnutls-dev libtheora-dev liberfa-dev
    # Not packaged on every Ubuntu release; INDI builds without it.
    sudo apt-get install -y libxisf-dev || echo "    (libxisf-dev not available; continuing without XISF)"
fi

echo "==> Configuring in $BUILD_DIR"
cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DINDI_BUILD_UNITTESTS=ON

echo "==> Building with $JOBS jobs"
cmake --build "$BUILD_DIR" -j "$JOBS"

echo "==> Running unit tests (offline, no API key)"
cd "$BUILD_DIR/test"
env -u ASTROSPHERIC_API_KEY ctest --output-on-failure -j "$JOBS"

echo "==> Done. Astrospheric tests only: ctest -R astrospheric -V (in $BUILD_DIR/test)"
