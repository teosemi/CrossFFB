#!/bin/bash
#
# Cross-compiles the Windows DirectInput 8 proxy (dinput8.dll) from source.
#
# Usage:
#   scripts/build_dinput8_proxy.sh [--output-dir <dir>] [--optional]
#
# Requires the mingw-w64 cross toolchain:
#   brew install mingw-w64
#
# With --optional the script warns and exits successfully when the toolchain is
# missing, so the macOS app can still be built without it.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE_FILE="${REPO_ROOT}/dinput8_proxy/dinput8.cpp"
DEF_FILE="${REPO_ROOT}/dinput8_proxy/dinput8.def"
OUTPUT_DIR="${CROSSFFB_RESOURCES_DIR:-${REPO_ROOT}/build/resources}"
OPTIONAL=0
CXX_TOOL="x86_64-w64-mingw32-g++"

while [ $# -gt 0 ]; do
    case "$1" in
        --output-dir)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --optional)
            OPTIONAL=1
            shift
            ;;
        -h|--help)
            sed -n '2,13p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "error: unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

if ! command -v "${CXX_TOOL}" >/dev/null 2>&1; then
    MESSAGE="mingw-w64 not found (${CXX_TOOL}). Install it with: brew install mingw-w64"
    if [ "${OPTIONAL}" -eq 1 ]; then
        echo "warning: ${MESSAGE}" >&2
        echo "warning: skipping dinput8.dll; CrossFFB will build but cannot install the proxy." >&2
        exit 0
    fi
    echo "error: ${MESSAGE}" >&2
    exit 1
fi

for required in "${SOURCE_FILE}" "${DEF_FILE}"; do
    if [ ! -f "${required}" ]; then
        echo "error: proxy source not found: ${required}" >&2
        exit 1
    fi
done

OUTPUT_FILE="${OUTPUT_DIR}/dinput8.dll"
STAMP_FILE="${OUTPUT_DIR}/.dinput8.dll.stamp"
SIGNATURE="$(cat "${SOURCE_FILE}" "${DEF_FILE}" | shasum -a 256 | cut -d' ' -f1) tool=$(${CXX_TOOL} -dumpversion)"

if [ -f "${OUTPUT_FILE}" ] && [ -f "${STAMP_FILE}" ] && [ "$(cat "${STAMP_FILE}")" = "${SIGNATURE}" ]; then
    echo "build_dinput8_proxy: up to date (${OUTPUT_FILE})"
    exit 0
fi

mkdir -p "${OUTPUT_DIR}"

echo "build_dinput8_proxy: cross-compiling Win64 proxy"
"${CXX_TOOL}" \
    -shared \
    -O2 \
    -static \
    -static-libgcc \
    -static-libstdc++ \
    -o "${OUTPUT_FILE}" \
    "${SOURCE_FILE}" \
    "${DEF_FILE}" \
    -lws2_32 \
    -lole32 \
    -luuid \
    -ldinput8

printf '%s' "${SIGNATURE}" > "${STAMP_FILE}"

echo "build_dinput8_proxy: wrote ${OUTPUT_FILE}"
file "${OUTPUT_FILE}"
