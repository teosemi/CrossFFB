#!/bin/bash
#
# Builds every native resource the CrossFFB app bundle embeds:
#   - g29_ffb_bridge  (native macOS helper)
#   - dinput8.dll     (Windows DirectInput 8 proxy, needs mingw-w64)
#
# Usage:
#   scripts/prepare_resources.sh [--output-dir <dir>] [--arch "<arch> ..."] [--require-proxy]
#
# The Xcode build calls this script automatically. Run it by hand to refresh the
# resources without opening Xcode.
#
# By default a missing mingw-w64 toolchain is only a warning; pass --require-proxy
# to make it a hard error (use this when preparing a release).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT_DIR="${CROSSFFB_RESOURCES_DIR:-${REPO_ROOT}/build/resources}"
REQUIRE_PROXY=0
ARCH_ARGS=()

while [ $# -gt 0 ]; do
    case "$1" in
        --output-dir)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --arch)
            ARCH_ARGS=(--arch "$2")
            shift 2
            ;;
        --require-proxy)
            REQUIRE_PROXY=1
            shift
            ;;
        -h|--help)
            sed -n '2,16p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "error: unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

mkdir -p "${OUTPUT_DIR}"

"${REPO_ROOT}/scripts/build_native_bridge.sh" --output-dir "${OUTPUT_DIR}" ${ARCH_ARGS[@]+"${ARCH_ARGS[@]}"}

PROXY_ARGS=(--output-dir "${OUTPUT_DIR}")
if [ "${REQUIRE_PROXY}" -eq 0 ]; then
    PROXY_ARGS+=(--optional)
fi

"${REPO_ROOT}/scripts/build_dinput8_proxy.sh" "${PROXY_ARGS[@]}"

echo "prepare_resources: resources ready in ${OUTPUT_DIR}"
