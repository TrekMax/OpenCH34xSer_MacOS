#!/usr/bin/env python3

import os
import select
import sys
import termios
import time


def configure_port(fd: int) -> None:
    attributes = termios.tcgetattr(fd)
    attributes[0] = 0
    attributes[1] = 0
    attributes[2] = termios.CLOCAL | termios.CREAD | termios.CS8
    attributes[3] = 0
    attributes[4] = termios.B115200
    attributes[5] = termios.B115200
    attributes[6][termios.VMIN] = 0
    attributes[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attributes)
    termios.tcflush(fd, termios.TCIOFLUSH)


def read_exact(fd: int, length: int, timeout: float) -> bytes:
    deadline = time.monotonic() + timeout
    received = bytearray()
    while len(received) < length:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        readable, _, _ = select.select([fd], [], [], remaining)
        if not readable:
            break
        chunk = os.read(fd, length - len(received))
        if chunk:
            received.extend(chunk)
    return bytes(received)


def main() -> int:
    node = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.OpenCH34x4"
    payloads = [
        b"\x00",
        bytes([0x00, 0x7F, 0xFF]) + bytes(range(13)),
        bytes((index * 37 + 11) & 0xFF for index in range(509)),
    ]

    fd = os.open(node, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        configure_port(fd)
        for payload in payloads:
            written = 0
            while written < len(payload):
                _, writable, _ = select.select([], [fd], [], 2.0)
                if not writable:
                    raise TimeoutError("serial write timed out")
                written += os.write(fd, payload[written:])
            received = read_exact(fd, len(payload), 3.0)
            if received != payload:
                raise AssertionError(
                    f"loopback mismatch for {len(payload)} bytes: "
                    f"received {len(received)} bytes"
                )
    finally:
        os.close(fd)

    print(f"port 4 tty loopback passed on {node}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
