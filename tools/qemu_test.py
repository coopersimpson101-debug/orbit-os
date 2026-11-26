#!/usr/bin/env python3
"""Headless QEMU boot tests (used by CI).

virt (default): boots build/virt/kernel8.img on
    qemu-system-aarch64 -M virt,gic-version=2,virtualization=on -cpu cortex-a72
(entered at EL2, like the Pi 5), and powers off through PSCI SYSTEM_OFF.

rv64: boots build/rv64/kernel.elf on
    qemu-system-riscv64 -M virt -bios none -m 128M
(entered in M-mode at 0x8000_0000, no OpenSBI: the kernel's own SBI shim),
and powers off through SBI SRST -> the sifive_test finisher.

Both wait for the shell, run a script of commands (tools/sessions.py) over
the serial console on stdio, check the UART output for expected lines, and
require QEMU to exit after poweroff.

    python tools/qemu_test.py                          # virt, direct -kernel boot
    python tools/qemu_test.py --mode chainload         # virt, chainloader + send_kernel.py over TCP
    python tools/qemu_test.py --board rv64             # rv64
    python tools/qemu_test.py --board rv64 --interactive   # use the shell; Ctrl-A X to quit
"""
from __future__ import annotations

import argparse
import os
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
import sessions  # noqa: E402

TARGETS = {
    "virt": {
        "qemu": "qemu-system-aarch64",
        "package": "qemu-system-arm",
        "machine": ["-M", "virt,gic-version=2,virtualization=on", "-cpu", "cortex-a72",
                    "-smp", "4", "-m", "512M"],
        "kernel": "kernel8.img",
        "boot_expect": sessions.AARCH64_BOOT_EXPECT,
        "script": sessions.AARCH64_SCRIPT,
        "poweroff": sessions.AARCH64_POWEROFF,
    },
    "rv64": {
        "qemu": "qemu-system-riscv64",
        "package": "qemu-system-misc",
        "machine": ["-M", "virt", "-bios", "none", "-m", "128M", "-smp", "1"],
        "kernel": "kernel.elf",
        "boot_expect": sessions.RV64_BOOT_EXPECT + ["[fdt] device tree at",
                                                    "capped by the board"],
        "script": [("info", ["board    : rv64 (qemu-virt, from DTB)",
                             "dtb stdout-path", "timebase 10000000 Hz (dtb"])]
                  + sessions.RV64_SCRIPT + sessions.RV64_EXTRA,
        "poweroff": sessions.RV64_POWEROFF,
    },
}
HEADLESS = ["-display", "none", "-monitor", "none"]


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


def run_script(con: Console, target: dict, timeout: float) -> list[str]:
    failures = []
    boot = target["boot_expect"]
    if not con.wait(boot + [sessions.PROMPT], 0, timeout):
        missing = [n for n in boot if n not in con.text()]
        return [f"boot: missing {missing}"]
    for cmd, expect in target["script"]:
        start = len(con.text())
        # type slowly-ish: the PL011 / 16550 FIFOs are 16-32 bytes deep
        for ch in cmd + "\r":
            con.send(ch)
            time.sleep(0.002)
        if not con.wait(expect + [sessions.PROMPT], start, timeout):
            out = con.text()[start:]
            failures.append(f"{cmd!r}: missing {[e for e in expect if e not in out]}")
    cmd, expect = target["poweroff"]
    start = len(con.text())
    con.send(cmd + "\r")
    if not con.wait(expect, start, timeout):
        failures.append(f"poweroff: missing {expect}")
    return failures


def qemu_exe(target: dict) -> str:
    exe = shutil.which(target["qemu"])
    if not exe:
        sys.exit(f"{target['qemu']} not found. Install QEMU (e.g. apt install "
                 f"{target['package']}) or use the emulated tests (tools/uniboot.py, "
                 "tools/iss_test.py) for an approximate local test.")
    return exe


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--board", choices=list(TARGETS), default="virt")
    ap.add_argument("--mode", choices=["direct", "chainload"], default="direct")
    ap.add_argument("--build-dir", help="default: build/<board>")
    ap.add_argument("--timeout", type=float, default=30.0, help="per-step timeout")
    ap.add_argument("--interactive", action="store_true")
    a = ap.parse_args()
    target = TARGETS[a.board]
    bd = Path(a.build_dir) if a.build_dir else ROOT / "build" / a.board
    kernel = bd / target["kernel"]
    exe = qemu_exe(target)
    if a.mode == "chainload" and a.board != "virt":
        sys.exit("--mode chainload is only available for the virt board")

    if a.interactive:
        return subprocess.call([exe] + target["machine"] + ["-nographic", "-kernel", str(kernel)])

    if a.mode == "direct":
        p = subprocess.Popen([exe] + target["machine"] + HEADLESS +
                             ["-serial", "stdio", "-kernel", str(kernel)],
                             stdin=subprocess.PIPE, stdout=subprocess.PIPE)

        def reader():
            return os.read(p.stdout.fileno(), 4096)

        def writer(b):
            p.stdin.write(b)
            p.stdin.flush()
        con = Console(reader, writer)
        failures = run_script(con, target, a.timeout)
    else:
        port = free_port()
        p = subprocess.Popen([exe] + target["machine"] + HEADLESS + [
            "-serial", f"tcp:127.0.0.1:{port},server=on,wait=on",
            "-kernel", str(bd / "chainloader.img")])
        link = send_kernel.Link(tcp=f"127.0.0.1:{port}")
        rest = send_kernel.send(link, kernel.read_bytes(), timeout=a.timeout,
                                echo=lambda b: sys.stdout.write(b.decode(errors="replace")))
        con = Console(link.read, link.write)
        with con.lock:
            con.buf.extend(rest)
        failures = run_script(con, target, a.timeout)

    try:
        p.wait(timeout=a.timeout)
        exited = True
    except subprocess.TimeoutExpired:
        exited = False
        p.kill()
    con.closed = True
    if not exited:
        failures.append("QEMU did not exit after poweroff")
    elif a.board == "rv64" and p.returncode != 0:
        # finisher 0x5555 exits 0; 0x3333 (the shim's failure path) does not
        failures.append(f"QEMU exited with status {p.returncode}")
    print("\n" + "=" * 60)
    if failures:
        for f in failures:
            print("FAIL:", f)
        return 1
    print(f"PASS: {a.board} {a.mode} boot, {len(target['script'])} shell commands, "
          "clean poweroff")
    return 0


if __name__ == "__main__":
    sys.exit(main())
