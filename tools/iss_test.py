#!/usr/bin/env python3
"""Boot test of the rv64 kernel on the orbit-rv golden ISS (used by pytest).

Loads build/rv64/kernel.elf into the instruction-set simulator of the
orbit-rv SoC with 256 KiB of RAM, waits for the shell, types a scripted
session into the 16550 model (RX interrupts through the PLIC), checks the
UART output for expected lines, and requires `poweroff` (SBI SRST ->
finisher) to halt the hart with exit code 0.

The ISS is not part of this repository. Point ORBIT_RV_PATH at a checkout
of the orbit-rv repository (or put it on PYTHONPATH):

    ORBIT_RV_PATH=../orbit-rv python tools/iss_test.py            # core session
    ORBIT_RV_PATH=../orbit-rv python tools/iss_test.py --full     # + demos, peek/poke
    ORBIT_RV_PATH=../orbit-rv python tools/iss_test.py --transcript uart.txt

Time model: the ISS derives mtime (10 MHz) from the instruction count.
The default of 1 instruction per mtime tick models a 10 MHz, IPC-1 hart,
so one 10 ms scheduler tick is 100k instructions and the whole session
stays within a few million instructions of a pure-Python simulator.
"""
from __future__ import annotations

import argparse
import os
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import sessions  # noqa: E402

RAM_SIZE = 256 * 1024
CHUNK = 20_000


def import_iss():
    """(load_hart, load_elf) from orbit_rv, or raise ImportError."""
    path = os.environ.get("ORBIT_RV_PATH")
    if path and path not in sys.path:
        sys.path.insert(0, path)
    from orbit_rv.elf import load_elf
    from orbit_rv.iss import load_hart
    return load_hart, load_elf


class IssConsole:
    """The hart plus its captured UART output."""

    def __init__(self, elf: Path, ram_size: int = RAM_SIZE, insns_per_tick: int = 1,
                 echo: bool = True):
        load_hart, load_elf = import_iss()
        self.out = bytearray()
        self.echo = echo
        self.hart = load_hart(load_elf(str(elf)), ram_size=ram_size,
                              insns_per_tick=insns_per_tick, on_tx=self._tx)

    def _tx(self, b: int) -> None:
        self.out.append(b)
        if self.echo and b != 13:
            sys.stdout.write(chr(b))
            if b == 10:
                sys.stdout.flush()

    def text(self) -> str:
        return self.out.decode(errors="replace").replace("\r", "")

    def stuck(self) -> bool:
        h = self.hart
        return h.wfi and not (h.mip() & h.mie)

    def wait(self, needles, start: int, budget: int) -> bool:
        """Run until every needle appears after text offset `start`."""
        used = 0
        while True:
            t = self.text()[start:]
            if all(n in t for n in needles):
                return True
            if self.hart.halted or used >= budget or self.stuck():
                return False
            used += self.hart.run(min(CHUNK, budget - used))

    def send(self, s: str) -> None:
        self.hart.uart.feed(s.encode())

    def run_until_halt(self, budget: int) -> bool:
        used = 0
        while not self.hart.halted and used < budget and not self.stuck():
            used += self.hart.run(min(CHUNK, budget - used))
        return self.hart.halted


def run_session(elf: Path, script, budget: int = 4_000_000, insns_per_tick: int = 1,
                echo: bool = True) -> tuple[list[str], IssConsole]:
    """Boot, run `script`, power off. Returns (failures, console)."""
    con = IssConsole(elf, insns_per_tick=insns_per_tick, echo=echo)
    failures = []
    if not con.wait(sessions.RV64_BOOT_EXPECT + [sessions.PROMPT], 0, budget):
        missing = [n for n in sessions.RV64_BOOT_EXPECT if n not in con.text()]
        return [f"boot: missing {missing} (pc={con.hart.pc:#x})"], con
    for cmd, expect in list(script) + [sessions.RV64_POWEROFF]:
        start = len(con.text())
        con.send(cmd + "\r")
        needles = expect if cmd == "poweroff" else expect + [sessions.PROMPT]
        if not con.wait(needles, start, budget):
            out = con.text()[start:]
            failures.append(f"{cmd!r}: missing {[e for e in needles if e not in out]} "
                            f"(pc={con.hart.pc:#x}, mip={con.hart.mip():#x})")
            return failures, con  # the shell is wedged: later steps would only burn budget
    if not con.run_until_halt(budget):
        failures.append("poweroff: the hart did not halt (finisher not reached)")
    elif con.hart.exit_code != 0:
        failures.append(f"poweroff: finisher exit code {con.hart.exit_code}, expected 0")
    return failures, con


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--elf", default=str(ROOT / "build" / "rv64" / "kernel.elf"))
    ap.add_argument("--full", action="store_true", help="also run the extra commands")
    ap.add_argument("--budget", type=int, default=4_000_000,
                    help="instruction budget per step (default 4M)")
    ap.add_argument("--insns-per-tick", type=int, default=1,
                    help="instructions per 10 MHz mtime tick (default 1)")
    ap.add_argument("--transcript", help="write the UART output to this file")
    ap.add_argument("--quiet", action="store_true", help="do not echo the UART")
    a = ap.parse_args()
    try:
        import_iss()
    except ImportError as e:
        sys.exit(f"orbit_rv is not importable ({e}); set ORBIT_RV_PATH to the orbit-rv repo")
    script = sessions.RV64_SCRIPT + (sessions.RV64_EXTRA if a.full else [])
    t0 = time.perf_counter()
    failures, con = run_session(Path(a.elf), script, a.budget, a.insns_per_tick,
                                echo=not a.quiet)
    dt = time.perf_counter() - t0
    if a.transcript:
        Path(a.transcript).write_text(con.text(), encoding="utf-8")
    n = con.hart.instret
    print("\n" + "=" * 60)
    print(f"[iss] {n} instructions in {dt:.1f} s ({n / max(dt, 1e-9) / 1e3:.0f} kIPS)")
    if failures:
        for f in failures:
            print("FAIL:", f)
        return 1
    print(f"PASS: orbit-rv ISS boot, {len(script)} shell commands, clean poweroff")
    return 0


if __name__ == "__main__":
    sys.exit(main())
