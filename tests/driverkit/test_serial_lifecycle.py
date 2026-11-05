#!/usr/bin/env python3
"""Check real DriverKit node names and complete removal after unplugging."""

import argparse
import json
from pathlib import Path
import plistlib
import subprocess

from serial_nodes import serial_nodes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("state", choices=["attached", "detached"])
    parser.add_argument("--snapshot", type=Path)
    parser.add_argument("--previous", type=Path)
    args = parser.parse_args()
    raw = subprocess.check_output([
        "ioreg", "-a", "-l", "-r", "-n", "CH9344Driver"
    ])
    entries = plistlib.loads(raw) if raw.strip() else []
    entries = [e for e in entries
               if e.get("CFBundleIdentifier") == "com.trekmax.OpenCH34xSer.driver"]
    expected = 4 if args.state == "attached" else 0
    assert len(entries) == expected, f"expected {expected} serial services, found {len(entries)}"
    if args.previous:
        stale = [p for p in json.loads(args.previous.read_text()) if Path(p).exists()]
        assert not stale, f"old device nodes still exist: {stale}"
    nodes = serial_nodes() if entries else {}
    for entry in entries:
        assert not (entry["IOServiceState"] & 1), "serial service is inactive/terminating"
        index = entry["CH9344LogicalPort"]
        suffix = f"{entry['IORegistryEntryID']:X}"
        expected_nodes = (f"/dev/cu.serialCH93xxUSB{index}-{suffix}",
                          f"/dev/tty.serialCH93xxUSB{index}-{suffix}")
        assert nodes[index] == expected_nodes, (nodes[index], expected_nodes)
    paths = [path for pair in nodes.values() for path in pair]
    if args.snapshot:
        args.snapshot.write_text(json.dumps(paths, indent=2) + "\n")
    print(f"PASS {args.state}: {len(entries)} serial services, {len(paths)} device nodes")


if __name__ == "__main__":
    main()
