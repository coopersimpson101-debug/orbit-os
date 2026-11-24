#!/usr/bin/env python3
"""Headless QEMU boot test for the qemu-virt build (used by CI).

Boots build/virt/kernel8.img on
    qemu-system-aarch64 -M virt,gic-version=2,virtualization=on -cpu cortex-a72
(entered at EL2, like the Pi 5), waits for the shell, runs a script of
commands, checks the UART output for expected lines, then powers the VM
off through PSCI SYSTEM_OFF and requires QEMU to exit.

    python tools/qemu_test.py                      # direct -kernel boot
    python tools/qemu_test.py --mode chainload     # chainloader + send_kernel.py over TCP
    python tools/qemu_test.py --interactive        # just run QEMU on this terminal
"""
from __future__ import annotations

import argparse
import shutil
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import send_kernel  # noqa: E402

QEMU = "qemu-system-aarch64"
MACHINE = ["-M", "virt,gic-version=2,virtualization=on", "-cpu", "cortex-a72",
           "-smp", "4", "-m", "512M", "-display", "none", "-monitor", "none"]

# (command, substrings that must appear before the next prompt)
SCRIPT = [
    ("help", ["poweroff", "demos:"]),
    ("info", ["board    : qemu-virt", "PL011 @ 0x9000000", "psci     : smc"]),
    ("mem", ["mmu      : on", "pages    :"]),
    ("echo \"hello   orbit\" 42", ["hello   orbit 42"]),
    ("uptime", ["up "]),
    ("ticks", []),
    ("peek 0x40080000 2", ["0x40080000:"]),
    ("poke 0x40080000 1", ["refusing to poke the kernel image"]),
    ("spawn counter", ["[counter] done"]),
    ("spawn mutex", ["shared counter = 2000 (expected 2000) OK"]),
    ("spawn pc", ["consumer sum = 204 (expected 204) OK"]),
    ("spawn spin", ["[spin-A] done", "preempted: yes", "[spin-B] done"]),
    ("spawn user", ["hello from EL0", "bad pointer correctly rejected", "exited with code 42"]),
    ("spawn ufault", ["data abort (lower EL): permission fault", "killing user task"]),
    ("spawn fault", ["translation fault", "killing kernel thread"]),
    ("ps", ["shell", "idle"]),
]
BOOT_EXPECT = ["OrbitOS v", "entered at EL2, now at EL1", "[mmu] enabled",
               "higher-half alias self-test: OK", "[gic] GICv2", "OrbitOS shell"]


class Console:
    """Collects output from a byte stream in a background thread."""

    def __init__(self, reader, writer):
        self.buf = bytearray()
        self.lock = threading.Lock()
        self.writer = writer
        self.closed = False
        threading.Thread(target=self._pump, args=(reader,), daemon=True).start()

    def _pump(self, reader):
        while True:
            try:
                data = reader()
            except (OSError, ConnectionError):
                data = b""
            if not data:
                if self.closed:
                    break
                time.sleep(0.01)
                continue
            with self.lock:
                self.buf.extend(data)
            sys.stdout.write(data.decode(errors="replace").replace("\r", ""))
            sys.stdout.flush()

    def text(self) -> str:
        with self.lock:
            return self.buf.decode(errors="replace").replace("\r", "")

    def wait(self, needles, start: int, timeout: float) -> bool:
        deadline = time.time() + timeout
        while time.time() < deadline:
            t = self.text()[start:]
            if all(n in t for n in needles):
                return True
            time.sleep(0.05)
        return False

    def send(self, s: str) -> None:
        self.writer(s.encode())


def run_script(con: Console, timeout: float) -> list[str]:
    failures = []
    if not con.wait(BOOT_EXPECT + ["orbit> "], 0, timeout):
        missing = [n for n in BOOT_EXPECT if n not in con.text()]
        return [f"boot: missing {missing}"]
    for cmd, expect in SCRIPT:
        start = len(con.text())
        # type slowly-ish: the PL011 FIFO is 16-32 bytes deep
        for ch in cmd + "\r":
            con.send(ch)
            time.sleep(0.002)
        if not con.wait(expect + ["orbit> "], start, timeout):
            out = con.text()[start:]
            failures.append(f"{cmd!r}: missing {[e for e in expect if e not in out]}")
    start = len(con.text())
    con.send("poweroff\r")
    if not con.wait(["powering off via PSCI SYSTEM_OFF"], start, timeout):
        failures.append("poweroff: no PSCI message")
    return failures


def qemu_exe() -> str:
    exe = shutil.which(QEMU)
    if not exe:
        sys.exit(f"{QEMU} not found. Install QEMU (e.g. apt install qemu-system-arm) "
                 "or use tools/uniboot.py for an approximate local test.")
    return exe


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mode", choices=["direct", "chainload"], default="direct")
    ap.add_argument("--build-dir", default=str(ROOT / "build" / "virt"))
    ap.add_argument("--timeout", type=float, default=30.0, help="per-step timeout")
    ap.add_argument("--interactive", action="store_true")
    a = ap.parse_args()
    bd = Path(a.build_dir)
    exe = qemu_exe()

    if a.interactive:
        return subprocess.call([exe] + MACHINE[:-4] + ["-nographic", "-kernel",
                                                       str(bd / "kernel8.img")])

    if a.mode == "direct":
        p = subprocess.Popen([exe] + MACHINE + ["-serial", "stdio", "-kernel",
                                                str(bd / "kernel8.img")],
                             stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        import os

        def reader():
            return os.read(p.stdout.fileno(), 4096)

        def writer(b):
            p.stdin.write(b)
            p.stdin.flush()
        con = Console(reader, writer)
        failures = run_script(con, a.timeout)
    else:
        port = free_port()
        p = subprocess.Popen([exe] + MACHINE + [
            "-serial", f"tcp:127.0.0.1:{port},server=on,wait=on",
            "-kernel", str(bd / "chainloader.img")])
        link = send_kernel.Link(tcp=f"127.0.0.1:{port}")
        rest = send_kernel.send(link, (bd / "kernel8.img").read_bytes(), timeout=a.timeout,
                                echo=lambda b: sys.stdout.write(b.decode(errors="replace")))
        con = Console(link.read, link.write)
        with con.lock:
            con.buf.extend(rest)
        failures = run_script(con, a.timeout)

    try:
        p.wait(timeout=a.timeout)
        exited = True
    except subprocess.TimeoutExpired:
        exited = False
        p.kill()
    con.closed = True
    if not exited:
        failures.append("QEMU did not exit after PSCI SYSTEM_OFF")
    print("\n" + "=" * 60)
    if failures:
        for f in failures:
            print("FAIL:", f)
        return 1
    print(f"PASS: qemu-virt {a.mode} boot, {len(SCRIPT)} shell commands, clean poweroff")
    return 0


if __name__ == "__main__":
    sys.exit(main())
