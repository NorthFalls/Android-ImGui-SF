#!/usr/bin/env bash
# Build the arm64-v8a root host executable on a Linux CI host.
#
#   ANDROID_NDK_HOME  Android NDK r26d (26.2.13676358)
#
# Usage: scripts/ci_build.sh [output-dir]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/dist}"
: "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME is required}"

BUILD="$ROOT/build-ci"
rm -rf "$BUILD"
mkdir -p "$OUT"

cmake -G Ninja -S "$ROOT" -B "$BUILD" \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-28 \
  -DANDROID_STL=c++_static \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" --parallel "$(nproc)" --target sfimgui

STRIP="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip"
[ -x "$STRIP" ] || { echo "missing llvm-strip: $STRIP" >&2; exit 1; }
"$STRIP" --strip-unneeded "$BUILD/sf-imgui.sh"
cp "$BUILD/sf-imgui.sh" "$OUT/sf-imgui.sh"
chmod 755 "$OUT/sf-imgui.sh"
( cd "$OUT" && sha256sum sf-imgui.sh > SHA256SUMS )
ls -l "$OUT/sf-imgui.sh" "$OUT/SHA256SUMS"
file "$OUT/sf-imgui.sh" || true
