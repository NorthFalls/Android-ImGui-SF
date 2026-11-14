#!/usr/bin/env bash
# Builds the pure C/C++ host APK: NativeActivity + one native library.
#   ANDROID_SDK_ROOT=... JAVA_HOME=... ./scripts/build_apk.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}"
if [[ -z "${SDK}" || ! -d "${SDK}" ]]; then
    echo "error: set ANDROID_SDK_ROOT" >&2
    exit 1
fi
if [[ -z "${JAVA_HOME:-}" ]]; then
    echo "error: set JAVA_HOME (needed for apksigner)" >&2
    exit 1
fi
if command -v cygpath >/dev/null 2>&1; then
    ROOT="$(cygpath -m "${ROOT}")"
    SDK="$(cygpath -m "${SDK}")"
    JAVA_HOME="$(cygpath -m "${JAVA_HOME}")"
fi

BT="${SDK}/build-tools/34.0.0"
PLATFORM="${SDK}/platforms/android-34/android.jar"
OUT="${ROOT}/build/apk"

LIBRARY=$(ls -t "${ROOT}"/dist/arm64-v8a/lib*_sf.so 2>/dev/null | head -1 || true)
if [[ -z "${LIBRARY}" ]]; then
    echo "error: no dist/arm64-v8a/lib*_sf.so - run scripts/build.sh first" >&2
    exit 1
fi
LIB_FILE=$(basename "${LIBRARY}")
LIB_NAME="${LIB_FILE#lib}"
LIB_NAME="${LIB_NAME%.so}"

rm -rf "${OUT}"
mkdir -p "${OUT}/lib/arm64-v8a" "${ROOT}/dist/apk"
cp "${LIBRARY}" "${OUT}/lib/arm64-v8a/${LIB_FILE}"

sed "s/__SF_LIB_NAME__/${LIB_NAME}/" "${ROOT}/app/AndroidManifest.xml" > "${OUT}/AndroidManifest.xml"

if [[ ! -f "${ROOT}/app/debug.keystore" ]]; then
    "${JAVA_HOME}/bin/keytool" -genkeypair -keystore "${ROOT}/app/debug.keystore" \
        -storepass android -keypass android -alias androiddebugkey \
        -keyalg RSA -keysize 2048 -validity 10000 \
        -dname "CN=SfImGui, O=OpenSource" >/dev/null 2>&1
fi

echo "==> aapt2 link"
"${BT}/aapt2" link -o "${OUT}/unsigned.apk" \
    --manifest "${OUT}/AndroidManifest.xml" \
    -I "${PLATFORM}" \
    --min-sdk-version 28 --target-sdk-version 34

echo "==> package"
cd "${OUT}"
"${JAVA_HOME}/bin/jar" uf unsigned.apk "lib/arm64-v8a/${LIB_FILE}"
"${BT}/zipalign" -f -p 4 unsigned.apk aligned.apk
"${BT}/apksigner" sign --ks "${ROOT}/app/debug.keystore" --ks-pass pass:android \
    --key-pass pass:android --ks-key-alias androiddebugkey \
    --out "${ROOT}/dist/apk/Android-ImGui-SF.apk" aligned.apk

echo "==> ${ROOT}/dist/apk/Android-ImGui-SF.apk"
