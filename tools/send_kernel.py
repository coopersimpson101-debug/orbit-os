#!/usr/bin/env python3
"""Host side of the orbit-chainloader serial protocol.

Waits for the chainloader's break signal (0x03 0x03 0x03), sends the
header ("OBK1" + u32 length + u32 CRC-32, little endian), the payload, and
checks the 2-byte replies. Optionally stays attached as a dumb terminal.

Real hardware (USB-serial adapter on the Pi 5 debug UART header):
    pip install pyserial
    python tools/send_kernel.py --port /dev/ttyUSB0 build/rpi5/kernel8.img --monitor
    python tools/send_kernel.py --port COM5 build/rpi5/kernel8.img --monitor

QEMU (chardev over TCP, as the CI does):
    qemu-system-aarch64 ... -kernel build/virt/chainloader.img \\
        -serial tcp:127.0.0.1:4555,server=on,wait=on
    python tools/send_kernel.py --tcp 127.0.0.1:4555 build/virt/kernel8.img --monitor

See docs/chainloader-protocol.md for the wire format.
"""
from __future__ import annotations

import argparse
import socket
import struct
import sys
import threading
import time
import zlib

MAGIC = b"OBK1"
BREAK = b"\x03\x03\x03"


def build_header(payload: bytes) -> bytes:
    return MAGIC + struct.pack("<II", len(payload), zlib.crc32(payload) & 0xFFFFFFFF)


class Link:
    """Byte transport: a pyserial port or a TCP socket."""

    def __init__(self, port: str | None = None, baud: int = 115200, tcp: str | None = None):
        self.sock = self.ser = None
        if tcp:
            host, p = tcp.rsplit(":", 1)
            deadline = time.time() + 30
            while True:
                try:
                    self.sock = socket.create_connection((host, int(p)), timeout=5)
                    break
                except OSError:
                    if time.time() > deadline:
                        raise
                    time.sleep(0.2)
            self.sock.settimeout(0.2)
        else:
            try:
                import serial  # type: ignore
            except ImportError:
                sys.exit("serial ports need pyserial: pip install pyserial")
            self.ser = serial.Serial(port, baud, timeout=0.2)

    def read(self, n: int = 4096) -> bytes:
        if self.sock:
            try:
                data = self.sock.recv(n)
                if data == b"":
                    raise ConnectionError("peer closed the connection")
                return data
            except socket.timeout:
                return b""
        return self.ser.read(n)

    def write(self, data: bytes) -> None:
        if self.sock:
            self.sock.sendall(data)
        else:
            self.ser.write(data)
            self.ser.flush()

    def close(self) -> None:
        (self.sock or self.ser).close()


def wait_for(link: Link, token: bytes, timeout: float, echo=None) -> bytes:
    """Read until `token` appears; returns everything after it."""
    buf = b""
    deadline = time.time() + timeout
    while time.time() < deadline:
        chunk = link.read()
        if chunk:
            if echo:
                echo(chunk)
            buf += chunk
            i = buf.find(token)
            if i >= 0:
                return buf[i + len(token):]
    raise TimeoutError(f"timed out waiting for {token!r}")


def read_exact(link: Link, n: int, timeout: float, pre: bytes = b"") -> tuple[bytes, bytes]:
    buf = pre
    deadline = time.time() + timeout
    while len(buf) < n and time.time() < deadline:
        buf += link.read(n - len(buf))
    if len(buf) < n:
        raise TimeoutError(f"expected {n} reply bytes, got {buf!r}")
    return buf[:n], buf[n:]


def send(link: Link, payload: bytes, timeout: float = 120.0, chunk: int = 1024,
         echo=None, log=print) -> bytes:
    """Run the protocol. Returns any bytes received after the final reply."""
    log("[send_kernel] waiting for chainloader break signal (reset the board now)...")
    rest = wait_for(link, BREAK, timeout, echo)
    hdr = build_header(payload)
    log(f"[send_kernel] sending {len(payload)} bytes, crc32=0x{zlib.crc32(payload) & 0xFFFFFFFF:08x}")
    link.write(hdr)
    reply, rest = read_exact(link, 2, 10, rest)
    if reply != b"OK":
        raise RuntimeError(f"header rejected: {reply!r} (SZ = bad size)")
    t0 = time.time()
    for i in range(0, len(payload), chunk):
        link.write(payload[i:i + chunk])
    reply, rest = read_exact(link, 2, max(30.0, len(payload) / 1000), rest)
    if reply != b"OK":
        raise RuntimeError(f"transfer failed: {reply!r} (CK = CRC mismatch)")
    dt = time.time() - t0
    log(f"[send_kernel] transfer OK in {dt:.1f}s; kernel is booting")
    return rest


def monitor(link: Link, initial: bytes = b"") -> None:
    """Minimal terminal: device output to stdout, stdin lines to device."""
    out = sys.stdout.buffer
    out.write(initial)
    out.flush()
    stop = threading.Event()

    def pump():
        while not stop.is_set():
            try:
                data = link.read()
            except ConnectionError:
                stop.set()
                break
            if data:
                out.write(data)
                out.flush()

    t = threading.Thread(target=pump, daemon=True)
    t.start()
    try:
        for line in sys.stdin:
            link.write(line.rstrip("\n").encode() + b"\r")
    except KeyboardInterrupt:
        pass
    stop.set()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", help="kernel8.img to send")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--port", help="serial device, e.g. /dev/ttyUSB0 or COM5")
    g.add_argument("--tcp", help="host:port of a TCP serial bridge (QEMU)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=120.0)
    ap.add_argument("--monitor", action="store_true", help="stay attached as a terminal")
    a = ap.parse_args()

    payload = open(a.image, "rb").read()
    link = Link(a.port, a.baud, a.tcp)
    try:
        rest = send(link, payload, a.timeout,
                    echo=lambda b: (sys.stdout.buffer.write(b), sys.stdout.flush()))
        if a.monitor:
            monitor(link, rest)
    finally:
        link.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
