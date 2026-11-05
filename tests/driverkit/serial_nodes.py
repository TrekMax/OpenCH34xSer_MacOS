#!/usr/bin/env python3
"""Resolve this driver's logical ports without assuming macOS's TTY names."""

import argparse
import os
import plistlib
import stat
import subprocess


def serial_nodes():
    raw = subprocess.check_output([
        "ioreg", "-a", "-l", "-r", "-n", "CH9344Driver"
    ])
    entries = plistlib.loads(raw) if raw.strip() else []
    ports = {}
    for entry in entries:
        if entry.get("CFBundleIdentifier") != "com.trekmax.OpenCH34xSer.driver":
            continue
        index = entry.get("CH9344LogicalPort")
        if not isinstance(index, int) or index not in range(4):
            raise ValueError(f"invalid logical port: {index!r}")
        if index in ports:
            raise ValueError("duplicate logical ports: multiple devices or stale terminating services")
        clients = [child for child in entry.get("IORegistryEntryChildren", [])
                   if child.get("IOClass") == "IOSerialBSDClient"]
        if len(clients) != 1:
            raise ValueError(f"port {index + 1}: expected one BSD serial client")
        nodes = (clients[0]["IOCalloutDevice"], clients[0]["IODialinDevice"])
        for node in nodes:
            if not stat.S_ISCHR(os.stat(node).st_mode):
                raise ValueError(f"not a character device: {node}")
        ports[index] = nodes
    return ports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected-count", type=int, choices=range(1, 5), default=4)
    parser.add_argument("--port", type=int, choices=range(1, 5), help="print callout node only")
    args = parser.parse_args()
    ports = serial_nodes()
    if len(ports) != args.expected_count:
        raise ValueError(f"expected {args.expected_count} ports, found {len(ports)}")
    if args.port:
        print(ports[args.port - 1][0])
    else:
        for index, nodes in sorted(ports.items()):
            print(f"port {index + 1}: {nodes[0]}  {nodes[1]}")
        print(f"PASS {len(ports)} driver-owned serial node pairs")


if __name__ == "__main__":
    main()
