#!/bin/bash
#
# Builds the native macOS bridge helper (g29_ffb_bridge) from source.
#
# Usage:
#   scripts/build_native_bridge.sh [--output-dir <dir>] [--arch "<arch> [<arch> ...]"]
#
# Defaults to a universal binary (arm64 + x86_64) so the helper runs on every
# Mac the app itself supports.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE_FILE="${REPO_ROOT}/native_bridge/g29_ffb_bridge.c"
OUTPUT_DIR="${CROSSFFB_RESOURCES_DIR:-${REPO_ROOT}/build/resources}"
ARCHS="arm64 x86_64"

while [ $# -gt 0 ]; do
    case "$1" in
        --output-dir)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --arch)
            ARCHS="$2"
            shift 2
            ;;
        -h|--help)
            sed -n '2,12p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "error: unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

if [ ! -f "${SOURCE_FILE}" ]; then
    echo "error: bridge source not found: ${SOURCE_FILE}" >&2
    exit 1
fi

OUTPUT_FILE="${OUTPUT_DIR}/g29_ffb_bridge"
STAMP_FILE="${OUTPUT_DIR}/.g29_ffb_bridge.stamp"
SIGNATURE="$(shasum -a 256 "${SOURCE_FILE}" | cut -d' ' -f1) archs=${ARCHS}"

if [ -f "${OUTPUT_FILE}" ] && [ -f "${STAMP_FILE}" ] && [ "$(cat "${STAMP_FILE}")" = "${SIGNATURE}" ]; then
    echo "build_native_bridge: up to date (${OUTPUT_FILE})"
    exit 0
fi

SDK_FLAGS=()
if command -v xcrun >/dev/null 2>&1; then
    CC="$(xcrun --find clang)"
    SDK_FLAGS+=("-isysroot" "$(xcrun --show-sdk-path --sdk macosx)")
else
    CC="clang"
fi

ARCH_FLAGS=()
for arch in ${ARCHS}; do
    ARCH_FLAGS+=("-arch" "${arch}")
done

mkdir -p "${OUTPUT_DIR}"

echo "build_native_bridge: compiling for [${ARCHS}]"
"${CC}" \
    "${ARCH_FLAGS[@]}" \
    ${SDK_FLAGS[@]+"${SDK_FLAGS[@]}"} \
    -O2 \
    -Wall \
    -mmacosx-version-min=14.0 \
    "${SOURCE_FILE}" \
    -framework IOKit \
    -framework CoreFoundation \
    -o "${OUTPUT_FILE}"

printf '%s' "${SIGNATURE}" > "${STAMP_FILE}"

echo "build_native_bridge: wrote ${OUTPUT_FILE}"
file "${OUTPUT_FILE}"
