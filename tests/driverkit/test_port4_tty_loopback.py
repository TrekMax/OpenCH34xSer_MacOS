#!/usr/bin/env python3

import os
import select
import sys
import termios
import time

from serial_nodes import serial_nodes


def configure_port(fd: int, speed: int = termios.B115200) -> None:
    attributes = termios.tcgetattr(fd)
    attributes[0] = 0
    attributes[1] = 0
    attributes[2] = termios.CLOCAL | termios.CREAD | termios.CS8
    attributes[3] = 0
    attributes[4] = speed
    attributes[5] = speed
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


def exchange(fd: int, payload: bytes, timeout: float) -> bytes:
    """Keep TX queued while draining RX, including payloads larger than the TTY ring."""
    deadline = time.monotonic() + timeout
    written = 0
    received = bytearray()
    while written < len(payload) or len(received) < len(payload):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(
                f"loopback timed out: sent {written}/{len(payload)}, received {len(received)}"
            )
        readable, writable, _ = select.select(
            [fd], [fd] if written < len(payload) else [], [], remaining
        )
        if readable:
            chunk = os.read(fd, 65536)
            if not chunk:
                raise EOFError("serial device closed during loopback")
            received.extend(chunk)
        if writable:
            try:
                written += os.write(fd, payload[written:written + 509])
            except BlockingIOError:
                pass
    return bytes(received)


def main() -> int:
    node = sys.argv[1] if len(sys.argv) > 1 else serial_nodes()[3][0]
    for baud, speed in [(9600, termios.B9600), (115200, termios.B115200)]:
        lengths = [1, 16, 30, 31, 32, 64, 255, 509, 510, 1024, 4096]
        if baud == 115200:
            lengths.append(65536)
        fd = os.open(node, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        try:
            configure_port(fd, speed)
            for case, length in enumerate(lengths):
                payload = bytes((index * 37 + case * 13 + 11) & 0xFF for index in range(length))
                received = exchange(fd, payload, max(5.0, length * 10 / baud * 3 + 2))
                if received != payload:
                    differences = [i for i, (a, b) in enumerate(zip(payload, received)) if a != b]
                    raise AssertionError(
                        f"loopback mismatch at {baud} baud for {length} bytes: "
                        f"received {len(received)}, first differing offsets {differences[:16]}"
                    )
                print(f"PASS {baud} baud, {length} bytes", flush=True)
            termios.tcdrain(fd)
            if select.select([fd], [], [], 0.1)[0]:
                raise AssertionError("unexpected extra loopback bytes")
        finally:
            os.close(fd)

    print(f"port 4 tty loopback passed on {node}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
