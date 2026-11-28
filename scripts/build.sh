#!/usr/bin/env bash
# Build the arm64-v8a root host locally.
#
#   ANDROID_NDK_HOME  Android NDK r26d or newer
#
# Usage: scripts/build.sh [abi] [api]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ABI="${1:-arm64-v8a}"
API="${2:-28}"
: "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME is required}"

BUILD="$ROOT/build"
OUT="$ROOT/dist"
rm -rf "$BUILD"
mkdir -p "$OUT"

cmake -G Ninja -S "$ROOT" -B "$BUILD" \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI="$ABI" \
  -DANDROID_PLATFORM="android-$API" \
  -DANDROID_STL=c++_static \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" --parallel "$(nproc 2>/dev/null || echo 4)" --target sfimgui

STRIP="$(ls "$ANDROID_NDK_HOME"/toolchains/llvm/prebuilt/*/bin/llvm-strip 2>/dev/null | head -1)"
[ -n "$STRIP" ] || { echo "llvm-strip not found in $ANDROID_NDK_HOME" >&2; exit 1; }
"$STRIP" --strip-unneeded "$BUILD/sf-imgui.sh"
cp "$BUILD/sf-imgui.sh" "$OUT/sf-imgui.sh"
chmod 755 "$OUT/sf-imgui.sh"
ls -l "$OUT/sf-imgui.sh"
