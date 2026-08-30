#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
derived_data="$(mktemp -d /tmp/opench34xser-unsigned.XXXXXX)"
trap 'rm -rf "$derived_data"' EXIT

DEVELOPER_DIR=/Applications/Xcode-beta.app/Contents/Developer \
    xcodebuild -quiet \
    -project "$repo_root/Driver/OpenCH34xSer.xcodeproj" \
    -scheme OpenCH34xSerHost \
    -configuration Debug \
    -destination 'platform=macOS,arch=arm64' \
    -derivedDataPath "$derived_data" \
    CODE_SIGNING_ALLOWED=NO \
    clean build

app="$derived_data/Build/Products/Debug/OpenCH34xSerHost.app"
dext="$app/Contents/Library/SystemExtensions/com.trekmax.OpenCH34xSer.driver.dext"
host_executable="$app/Contents/MacOS/OpenCH34xSerHost"
dext_executable="$dext/com.trekmax.OpenCH34xSer.driver"

test -x "$host_executable"
test -x "$dext_executable"
test "$(plutil -extract CFBundleIdentifier raw "$app/Contents/Info.plist")" = \
    "com.trekmax.OpenCH34xSer"
test "$(plutil -extract CFBundleIdentifier raw "$dext/Info.plist")" = \
    "com.trekmax.OpenCH34xSer.driver"
test "$(plutil -extract IOKitPersonalities.CH9344Transport.idVendor raw "$dext/Info.plist")" = \
    "6790"
test "$(plutil -extract IOKitPersonalities.CH9344Transport.idProduct raw "$dext/Info.plist")" = \
    "57368"

dependencies="$(otool -L "$dext_executable")"
printf '%s\n' "$dependencies" | grep -q 'DriverKit.framework'
printf '%s\n' "$dependencies" | grep -q 'USBDriverKit.framework'
printf '%s\n' "$dependencies" | grep -q 'SerialDriverKit.framework'
if printf '%s\n' "$dependencies" | grep -Eiq 'libusb|libusb-1\.0'; then
    echo "DEXT must not link libusb" >&2
    exit 1
fi

test "$(plutil -extract 'com\.apple\.developer\.driverkit' raw \
    "$repo_root/Driver/Extension/CH9344Driver.entitlements")" = "true"
test "$(plutil -extract 'com\.apple\.developer\.driverkit\.family\.serial' raw \
    "$repo_root/Driver/Extension/CH9344Driver.entitlements")" = "true"
test "$(plutil -extract 'com\.apple\.developer\.system-extension\.install' raw \
    "$repo_root/Driver/Host/OpenCH34xSerHost.entitlements")" = "true"

echo "unsigned DriverKit bundle checks passed"
