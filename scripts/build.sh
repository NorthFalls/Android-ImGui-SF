#!/usr/bin/env bash
# Builds the overlay library and names it lib<YYYYMMDD_HHMMSS>_sf.so.
#
#   ANDROID_NDK_HOME=/path/to/ndk ./scripts/build.sh [abi] [api]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ABI="${1:-arm64-v8a}"
API="${2:-28}"

NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
if [[ -z "${NDK}" || ! -d "${NDK}" ]]; then
    echo "error: set ANDROID_NDK_HOME to an extracted Android NDK (r26 or newer)" >&2
    exit 1
fi
if command -v cygpath >/dev/null 2>&1; then
    NDK="$(cygpath -m "${NDK}")"
    ROOT="$(cygpath -m "${ROOT}")"
fi

STAMP="$(date +%Y%m%d_%H%M%S)"
ARTIFACT="lib${STAMP}_sf.so"
BUILD_DIR="${ROOT}/build/${ABI}"

GENERATOR=()
if command -v ninja >/dev/null 2>&1; then
    GENERATOR=(-G Ninja)
fi

echo "==> configuring ${ABI} (API ${API}) as ${ARTIFACT}"
cmake -S "${ROOT}" -B "${BUILD_DIR}" "${GENERATOR[@]}" \
    -DCMAKE_TOOLCHAIN_FILE="${NDK}/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="${ABI}" \
    -DANDROID_PLATFORM="android-${API}" \
    -DANDROID_STL=c++_static \
    -DCMAKE_BUILD_TYPE=Release \
    -DSF_ARTIFACT_NAME="${ARTIFACT}" >/dev/null

echo "==> building"
cmake --build "${BUILD_DIR}" --parallel

mkdir -p "${ROOT}/dist/${ABI}"
cp "${BUILD_DIR}/${ARTIFACT}" "${ROOT}/dist/${ABI}/${ARTIFACT}"

# Strip debug info: 9 MB -> ~3.7 MB, which maps noticeably faster on device.
STRIP_BIN="$(ls "${NDK}"/toolchains/llvm/prebuilt/*/bin/llvm-strip* 2>/dev/null | head -1)"
if [[ -n "${STRIP_BIN}" ]]; then
    "${STRIP_BIN}" --strip-unneeded "${ROOT}/dist/${ABI}/${ARTIFACT}" || true
fi
echo "==> ${ROOT}/dist/${ABI}/${ARTIFACT}"
