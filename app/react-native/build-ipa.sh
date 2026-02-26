#!/bin/bash
set -e

cd "$(dirname "$0")"

SCHEME="GeorecordBLE"
WORKSPACE="ios/GeorecordBLE.xcworkspace"
ARCHIVE_PATH="build/GeorecordBLE.xcarchive"
EXPORT_PATH="build/ipa"
EXPORT_OPTIONS="ios/ExportOptions.plist"

echo "=== Cleaning previous build ==="
rm -rf build

echo "=== Archiving (Release) ==="
xcodebuild archive \
  -workspace "$WORKSPACE" \
  -scheme "$SCHEME" \
  -sdk iphoneos \
  -configuration Release \
  -archivePath "$ARCHIVE_PATH" \
  CODE_SIGN_STYLE=Automatic \
  -allowProvisioningUpdates \
  | tail -5

echo ""
echo "=== Exporting IPA ==="
xcodebuild -exportArchive \
  -archivePath "$ARCHIVE_PATH" \
  -exportPath "$EXPORT_PATH" \
  -exportOptionsPlist "$EXPORT_OPTIONS" \
  -allowProvisioningUpdates \
  | tail -5

echo ""
echo "=== Done ==="
IPA_FILE=$(find "$EXPORT_PATH" -name "*.ipa" | head -1)
echo "IPA: $(pwd)/$IPA_FILE"
echo "Size: $(du -h "$IPA_FILE" | cut -f1)"
