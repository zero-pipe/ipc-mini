#!/usr/bin/env bash
# Cross-build example programs on the HiSilicon host.
# Does not modify board/Makefile or the main zero_mini build.
set -eo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

# Ensure PATH has the musl cross toolchain even if profile has unbound vars.
export PATH="/opt/linux/x86-arm/arm-v01c02-linux-musleabi-gcc/bin:${PATH:-}"
if [[ -f /etc/profile ]]; then
  # shellcheck disable=SC1091
  set +u
  source /etc/profile
  set -u
fi
set -u

BUILD_DIR="${ROOT}/build"
TOOLCHAIN="${ROOT}/cmake/hisilicon-musl.cmake"

cmake -S "$ROOT" -B "$BUILD_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DCMAKE_BUILD_TYPE=Release

cmake --build "$BUILD_DIR" -j"$(nproc)"

echo
echo "Build OK:"
ls -lh "$BUILD_DIR"/pipeline_direct_mpp "$BUILD_DIR"/pipeline_with_chn
file "$BUILD_DIR"/pipeline_direct_mpp "$BUILD_DIR"/pipeline_with_chn
