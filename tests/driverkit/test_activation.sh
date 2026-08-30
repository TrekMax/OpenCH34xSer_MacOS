#!/usr/bin/env bash

set -euo pipefail

bundle_id="com.trekmax.OpenCH34xSer.driver"
team_id="M6D66QWKAN"

extension_line="$(systemextensionsctl list | grep "$bundle_id" || true)"
if [[ -z "$extension_line" ]]; then
    echo "$bundle_id is not registered" >&2
    exit 1
fi
if [[ "$extension_line" != *"$team_id"* ||
      ! "$extension_line" =~ ^[[:space:]]*\*[[:space:]]+\* ]]; then
    echo "$bundle_id is not active and enabled for team $team_id" >&2
    echo "$extension_line" >&2
    exit 1
fi

if ! ioreg -r -c CH9344Driver -l | grep -q 'CH9344Driver'; then
    echo "CH9344Driver is not bound in IORegistry" >&2
    exit 1
fi

echo "$bundle_id is active, enabled, and bound"
