#!/bin/bash
#
# Cross-compiles wheel_probe.exe, a console tool that reads the G29 through
# DirectInput 8 the way a game does. It is a development aid and is not
# bundled in the app.
#
# Usage:
#   scripts/build_wheel_probe.sh [--output-dir <dir>]
#
# Copy the result, together with dinput8.dll, into a folder inside a bottle's
# drive_c (never system32) and run it with CrossOver's wine.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE_FILE="${REPO_ROOT}/dinput8_proxy/tools/wheel_probe.cpp"
OUTPUT_DIR="${REPO_ROOT}/build/tools"
CXX_TOOL="x86_64-w64-mingw32-g++"

while [ $# -gt 0 ]; do
    case "$1" in
        --output-dir)
            OUTPUT_DIR="$2"
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

if ! command -v "${CXX_TOOL}" >/dev/null 2>&1; then
    echo "error: mingw-w64 not found (${CXX_TOOL}). Install it with: brew install mingw-w64" >&2
    exit 1
fi

mkdir -p "${OUTPUT_DIR}"

OUTPUT_FILE="${OUTPUT_DIR}/wheel_probe.exe"

echo "build_wheel_probe: cross-compiling Win64 probe"
"${CXX_TOOL}" \
    -O2 \
    -Wall \
    -static \
    -static-libgcc \
    -static-libstdc++ \
    -o "${OUTPUT_FILE}" \
    "${SOURCE_FILE}" \
    -ldinput8 \
    -ldxguid \
    -luuid

echo "build_wheel_probe: wrote ${OUTPUT_FILE}"
file "${OUTPUT_FILE}"
