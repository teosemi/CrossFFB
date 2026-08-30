#!/bin/bash
#
# Builds, signs and packages CrossFFB for distribution.
#
# Usage:
#   scripts/make_release.sh [--output-dir <dir>] [--notarize]
#
# Without --notarize the script stops after producing a signed DMG, which is
# enough to check the build locally. With --notarize it also submits the DMG to
# Apple, staples the ticket and verifies it with Gatekeeper.
#
# No credentials live in this repository. Notarization uses a keychain profile
# created once with:
#
#   xcrun notarytool store-credentials "CrossFFB-Notary" \
#       --apple-id <apple id> --team-id <team id> --password <app-specific password>
#
# Override the defaults with the environment variables CROSSFFB_SIGN_IDENTITY
# and CROSSFFB_NOTARY_PROFILE.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT_DIR="${REPO_ROOT}/build/release"
NOTARIZE=0
SIGN_IDENTITY="${CROSSFFB_SIGN_IDENTITY:-Developer ID Application: Maurizio Seminara (84TJ2LG6PJ)}"
NOTARY_PROFILE="${CROSSFFB_NOTARY_PROFILE:-CrossFFB-Notary}"

while [ $# -gt 0 ]; do
    case "$1" in
        --output-dir)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --notarize)
            NOTARIZE=1
            shift
            ;;
        -h|--help)
            sed -n '2,21p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "error: unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

if ! command -v create-dmg >/dev/null 2>&1; then
    echo "error: create-dmg not found. Install it with: brew install create-dmg" >&2
    exit 1
fi

VERSION="$(xcodebuild -project "${REPO_ROOT}/CrossFFB.xcodeproj" -scheme CrossFFB \
    -configuration Release -showBuildSettings 2>/dev/null |
    awk '$1 == "MARKETING_VERSION" {print $3; exit}')"

if [ -z "${VERSION}" ]; then
    echo "error: could not read MARKETING_VERSION from the project" >&2
    exit 1
fi

echo "make_release: building CrossFFB ${VERSION}"

ARCHIVE_PATH="${OUTPUT_DIR}/CrossFFB.xcarchive"
EXPORT_PATH="${OUTPUT_DIR}/export"
DMG_PATH="${OUTPUT_DIR}/CrossFFB-${VERSION}.dmg"

rm -rf "${ARCHIVE_PATH}" "${EXPORT_PATH}" "${DMG_PATH}"
mkdir -p "${OUTPUT_DIR}"

# The proxy must be present in a release build, unlike a development one.
"${REPO_ROOT}/scripts/prepare_resources.sh" --require-proxy --arch "arm64 x86_64"

xcodebuild \
    -project "${REPO_ROOT}/CrossFFB.xcodeproj" \
    -scheme CrossFFB \
    -configuration Release \
    -archivePath "${ARCHIVE_PATH}" \
    archive

EXPORT_OPTIONS="${OUTPUT_DIR}/ExportOptions.plist"
cat > "${EXPORT_OPTIONS}" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>method</key>
    <string>developer-id</string>
    <key>teamID</key>
    <string>84TJ2LG6PJ</string>
    <key>signingStyle</key>
    <string>automatic</string>
</dict>
</plist>
PLIST

xcodebuild \
    -exportArchive \
    -archivePath "${ARCHIVE_PATH}" \
    -exportPath "${EXPORT_PATH}" \
    -exportOptionsPlist "${EXPORT_OPTIONS}"

APP_PATH="${EXPORT_PATH}/CrossFFB.app"
STAGING_PATH="${OUTPUT_DIR}/dmg-root"
BACKGROUND_PATH="${REPO_ROOT}/packaging/dmg-background.png"
DMG_README_PATH="${REPO_ROOT}/packaging/DMG-README.txt"

echo "make_release: verifying the exported app"
codesign --verify --deep --strict --verbose=2 "${APP_PATH}"
lipo -archs "${APP_PATH}/Contents/MacOS/CrossFFB"
lipo -archs "${APP_PATH}/Contents/Resources/g29_ffb_bridge"

# The disk image carries a short read-me beside the app, so its contents live
# in a folder of their own rather than being just the bundle.
rm -rf "${STAGING_PATH}"
mkdir -p "${STAGING_PATH}"
cp -R "${APP_PATH}" "${STAGING_PATH}/CrossFFB.app"
cp "${DMG_README_PATH}" "${STAGING_PATH}/README.txt"

# create-dmg arranges the window through Finder, over Apple Events. The first
# run from a given terminal raises a permission prompt; without it the layout
# step fails and leaves a large read-write image behind.
cleanup_scratch_images() {
    rm -f "${OUTPUT_DIR}"/rw.*.dmg
}

trap cleanup_scratch_images EXIT

# The layout below is the one that was accepted for 1.0.0: app on the left,
# Applications on the right, the read-me underneath the arrow.
if ! create-dmg \
    --volname "CrossFFB" \
    --background "${BACKGROUND_PATH}" \
    --window-pos 200 120 \
    --window-size 600 520 \
    --icon-size 100 \
    --icon "CrossFFB.app" 170 130 \
    --app-drop-link 430 130 \
    --icon "README.txt" 300 305 \
    --no-internet-enable \
    "${DMG_PATH}" \
    "${STAGING_PATH}"
then
    echo >&2
    echo "error: create-dmg could not lay the window out." >&2
    echo "       It drives Finder over Apple Events, which needs permission." >&2
    echo "       Run this script from a terminal and allow the prompt, or grant" >&2
    echo "       it under System Settings > Privacy & Security > Automation." >&2
    exit 1
fi

echo "make_release: signing the disk image"
codesign --force --timestamp --sign "${SIGN_IDENTITY}" "${DMG_PATH}"

if [ "${NOTARIZE}" -eq 0 ]; then
    echo
    echo "make_release: signed disk image ready at ${DMG_PATH}"
    echo "make_release: it is NOT notarized. Re-run with --notarize to submit it."
    exit 0
fi

echo "make_release: submitting to Apple, this takes a few minutes"
xcrun notarytool submit "${DMG_PATH}" --keychain-profile "${NOTARY_PROFILE}" --wait

xcrun stapler staple "${DMG_PATH}"
xcrun stapler validate "${DMG_PATH}"

# The plain spctl invocation reports "Insufficient Context" for a disk image.
spctl -a -vvv -t open --context context:primary-signature "${DMG_PATH}"

echo
echo "make_release: notarized disk image ready at ${DMG_PATH}"
