#!/bin/bash

set -euo pipefail

repo_root=$(cd "$(dirname "$0")/../.." && pwd)
project="$repo_root/Driver/OpenCH34xSer.xcodeproj/project.pbxproj"
host_info="$repo_root/Driver/Host/Info.plist"
host_entitlements="$repo_root/Driver/Host/OpenCH34xSerHost.entitlements"
driver_info="$repo_root/Driver/Extension/Info.plist"
driver_entitlements="$repo_root/Driver/Extension/CH9344Driver.entitlements"

fail()
{
    echo "FAIL: $*" >&2
    exit 1
}

for required in \
    "$project" \
    "$host_info" \
    "$host_entitlements" \
    "$driver_info" \
    "$driver_entitlements" \
    "$repo_root/Driver/Host/OpenCH34xSerApp.swift" \
    "$repo_root/Driver/Extension/CH9344Transport.iig" \
    "$repo_root/Driver/Extension/CH9344Transport.cpp" \
    "$repo_root/Driver/Extension/CH9344Driver.iig" \
    "$repo_root/Driver/Extension/CH9344Driver.cpp"
do
    [[ -f "$required" ]] || fail "missing $required"
done

plist_value()
{
    local plist=$1
    local key=$2
    plutil -extract "$key" raw -o - "$plist"
}

[[ "$(plist_value "$host_entitlements" 'com\.apple\.developer\.system-extension\.install')" == "true" ]] || \
    fail "host system-extension entitlement is not true"
[[ "$(plist_value "$driver_entitlements" 'com\.apple\.developer\.driverkit')" == "true" ]] || \
    fail "DriverKit entitlement is not true"
[[ "$(plist_value "$driver_entitlements" 'com\.apple\.developer\.driverkit\.family\.serial')" == "true" ]] || \
    fail "serial family entitlement is not true"
[[ "$(plist_value "$driver_entitlements" 'com\.apple\.developer\.driverkit\.transport\.usb.0.idVendor')" == "6790" ]] || \
    fail "USB entitlement vendor mismatch"
[[ "$(plist_value "$driver_entitlements" 'com\.apple\.developer\.driverkit\.transport\.usb.0.idProduct')" == "57368" ]] || \
    fail "USB entitlement product mismatch"

personality='IOKitPersonalities.CH9344Transport'
[[ "$(plist_value "$driver_info" "$personality.idVendor")" == "6790" ]] || \
    fail "personality vendor mismatch"
[[ "$(plist_value "$driver_info" "$personality.idProduct")" == "57368" ]] || \
    fail "personality product mismatch"
[[ "$(plist_value "$driver_info" "$personality.bConfigurationValue")" == "1" ]] || \
    fail "configuration mismatch"
[[ "$(plist_value "$driver_info" "$personality.bInterfaceNumber")" == "0" ]] || \
    fail "interface mismatch"
[[ "$(plist_value "$driver_info" "$personality.IOProviderClass")" == "IOUSBHostInterface" ]] || \
    fail "provider class mismatch"
[[ "$(plist_value "$driver_info" "$personality.IOClass")" == "IOUserService" ]] || \
    fail "kernel proxy class mismatch"
[[ "$(plist_value "$driver_info" "$personality.IOUserClass")" == "CH9344Transport" ]] || \
    fail "DriverKit class mismatch"

for logical_port in 0 1 2 3; do
    child="$personality.CH9344SerialPort$((logical_port + 1))"
    [[ "$(plist_value "$driver_info" "$child.IOClass")" == "IOUserSerial" ]] || \
        fail "port $logical_port kernel proxy class mismatch"
    [[ "$(plist_value "$driver_info" "$child.IOUserClass")" == "CH9344Driver" ]] || \
        fail "port $logical_port DriverKit class mismatch"
    [[ "$(plist_value "$driver_info" "$child.CH9344LogicalPort")" == "$logical_port" ]] || \
        fail "port $logical_port logical index mismatch"
    [[ "$(plist_value "$driver_info" "$child.IOTTYSuffix")" == "$((logical_port + 1))" ]] || \
        fail "port $logical_port tty suffix mismatch"
done

grep -Fq 'productType = "com.apple.product-type.application";' "$project" || \
    fail "host application target missing"
grep -Fq 'productType = "com.apple.product-type.driver-extension";' "$project" || \
    fail "driver extension target missing"
grep -Fq 'dstPath = "$(SYSTEM_EXTENSIONS_FOLDER_PATH)";' "$project" || \
    fail "system extension embed destination missing"
grep -Fq 'lastKnownFileType = sourcecode.iig;' "$project" || \
    fail "IIG source is not part of the project"

echo "PASS DriverKit project metadata"
