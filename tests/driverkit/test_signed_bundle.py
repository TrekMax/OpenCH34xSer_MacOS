#!/usr/bin/env python3
"""Check actual signed entitlements against the embedded provisioning profiles."""

import datetime
import pathlib
import plistlib
import subprocess
import sys


def read_plist(command):
    return plistlib.loads(subprocess.check_output(command, stderr=subprocess.DEVNULL))


def verify_bundle(bundle, profile_path):
    entitlements = read_plist([
        "codesign", "-d", "--entitlements", "-", "--xml", str(bundle)
    ])
    profile = read_plist(["security", "cms", "-D", "-i", str(profile_path)])
    allowed = profile["Entitlements"]
    if profile["ExpirationDate"] <= datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None):
        raise ValueError(f"{bundle.name}: provisioning profile expired")
    team = entitlements.get("com.apple.developer.team-identifier")
    if not team or team not in profile["TeamIdentifier"]:
        raise ValueError(f"{bundle.name}: signing team does not match profile")
    for key, value in entitlements.items():
        if key.startswith("com.apple.developer.") and allowed.get(key) != value:
            raise ValueError(
                f"{bundle.name}: {key} differs from embedded profile: "
                f"signed={value!r}, profile={allowed.get(key)!r}"
            )
    print(f"PASS {bundle.name}: restricted entitlements, team and profile expiry")
    return team


def main():
    app = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/Applications/OpenCH93xxSer.app")
    dext = app / "Contents/Library/SystemExtensions/com.trekmax.OpenCH34xSer.driver.dext"
    subprocess.run([
        "codesign", "--verify", "--deep", "--strict", "--all-architectures", str(app)
    ], check=True)
    host_team = verify_bundle(app, app / "Contents/embedded.provisionprofile")
    driver_team = verify_bundle(dext, dext / "embedded.provisionprofile")
    if host_team != driver_team:
        raise ValueError("Host and DEXT use different signing teams")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
