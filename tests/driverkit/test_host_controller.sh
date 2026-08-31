#!/bin/bash

set -euo pipefail

repo_root=$(cd "$(dirname "$0")/../.." && pwd)
test_root=$(mktemp -d /tmp/ch9344-host-tests.XXXXXX)
trap 'rm -rf "$test_root"' EXIT

DEVELOPER_DIR=/Applications/Xcode-beta.app/Contents/Developer \
    xcrun --sdk macosx swiftc \
    "$repo_root/Driver/Host/SystemExtensionController.swift" \
    "$repo_root/Driver/HostTests/SystemExtensionControllerTests.swift" \
    -o "$test_root/SystemExtensionControllerTests"

"$test_root/SystemExtensionControllerTests"
