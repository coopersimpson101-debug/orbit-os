"""Scripted shell sessions shared by the boot tests.

Each script is a list of (command, substrings that must appear before the
next prompt). ``BOOT_EXPECT`` lines must appear before the first prompt.
Used by tools/qemu_test.py (QEMU, CI) and tools/iss_test.py (orbit-rv
golden ISS, pytest).
"""
from __future__ import annotations

PROMPT = "orbit> "

# --- AArch64, qemu-virt (-M virt,gic-version=2,virtualization=on) ----------
AARCH64_BOOT_EXPECT = ["OrbitOS v", "entered at EL2, now at EL1", "[mmu] enabled",
                       "higher-half alias self-test: OK", "[gic] GICv2", "OrbitOS shell"]
AARCH64_SCRIPT = [
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
AARCH64_POWEROFF = ("poweroff", ["powering off via PSCI SYSTEM_OFF"])

# --- RV64 (orbit-rv ISS and qemu-system-riscv64 -M virt -bios none) ---------
RV64_BOOT_EXPECT = ["OrbitOS v", "hobby RISC-V kernel", "entered in M-mode",
                    "now in S-mode", "[mmu] enabled: Sv39", "higher-half alias self-test: OK",
                    "[plic] PLIC at 0xc000000", "[timer] 100 Hz tick via SBI set_timer",
                    "[uart] RX interrupt driven", "OrbitOS shell"]
# The core session: everything the port must prove on every target.
RV64_SCRIPT = [
    ("help", ["poweroff", "SBI SRST shutdown", "demos:"]),
    ("info", ["board    : rv64", "arch     : RISC-V", "NS16550 @ 0x10000000, RX IRQ",
              "OrbitOS built-in M-mode shim", "rv64ima"]),
    ("ps", ["TID  NAME", "idle", "shell", "<- you"]),
    ("spawn spin", ["[spin-A] done", "preempted: yes", "[spin-B] done"]),
    ("spawn user", ["hello from U-mode", "bad pointer correctly rejected with -EFAULT",
                    "exited with code 42"]),
    ("spawn ufault", ["load page fault from U-mode", "killing user task"]),
    ("uptime", ["up ", "ticks at 100 Hz"]),
]
# Extra commands for the full session (QEMU in CI, `iss_test.py --full`).
RV64_EXTRA = [
    ("mem", ["mmu      : on (Sv39", "pages    :", "256 KiB"]),
    ("echo \"hello   orbit\" 42", ["hello   orbit 42"]),
    ("ticks", []),
    ("peek 0x80000000 2", ["0x80000000:"]),
    ("poke 0x80000000 1", ["refusing to poke the kernel image"]),
    ("spawn counter", ["[counter] done"]),
    ("spawn mutex", ["shared counter = 2000 (expected 2000) OK"]),
    ("spawn pc", ["consumer sum = 204 (expected 204) OK"]),
    ("spawn fault", ["load page fault from S-mode", "killing kernel thread"]),
    ("ps", ["shell", "idle"]),
]
RV64_POWEROFF = ("poweroff", ["powering off via SBI SRST shutdown"])
