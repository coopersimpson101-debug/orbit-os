#!/usr/bin/env python3
"""Approximate local boot test of the qemu-virt kernel using Unicorn.

QEMU (see tools/qemu_test.py, used in CI) is the reference environment.
This script exists for machines where QEMU is not available: it runs the
real kernel8.img on Unicorn's AArch64 CPU core and fakes just enough of
the virt board to get through boot and drive the shell:

  * PL011 UART (TX capture, RX injection + RX interrupt)
  * a GICv2 stub (IAR returns the pending INTID)
  * timer interrupts injected when CNTPCT passes CNTP_CVAL (checked every
    --chunk instructions) while PSTATE.I is clear
  * exception entry (SVC, aborts, IRQ) performed in Python, because Unicorn
    reports exceptions to a hook instead of vectoring through VBAR_EL1
  * stage-1 translation done by a Python walker of the kernel's own page
    tables (Unicorn's built-in MMU path does not work for bare-metal code),
    including EL0/EL1 AP and UXN/PXN permission checks

It is NOT a faithful machine model (no caches, no real GIC, approximate
timing, and Unicorn starts at EL1 so the EL2->EL1 drop in boot.S is NOT
exercised), so a pass here is a smoke test, not proof of hardware behaviour.

    pip install unicorn
    python tools/uniboot.py build/virt/kernel8.img --cmd help --cmd "spawn user"
"""
from __future__ import annotations

import argparse
import collections
import struct
import sys
import time
from pathlib import Path

try:
    from unicorn import (UC_ARCH_ARM64, UC_HOOK_INSN, UC_HOOK_INTR, UC_HOOK_MEM_UNMAPPED,
                         UC_HOOK_TLB_FILL, UC_MODE_ARM, UC_PROT_EXEC, UC_PROT_READ,
                         UC_PROT_WRITE, UC_TLB_VIRTUAL, Uc, UcError)
    from unicorn import arm64_const as A
    from unicorn.unicorn_const import UC_MEM_FETCH, UC_MEM_WRITE
except ImportError:  # pragma: no cover
    sys.exit("uniboot.py needs Unicorn: pip install unicorn")

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mkdtb  # noqa: E402
import send_kernel  # noqa: E402

RAM_BASE = 0x40000000
RAM_SIZE = 512 << 20
DTB_ADDR = 0x48000000
UART = 0x09000000
GICD = 0x08000000
TIMER_INTID = 30
UART_INTID = 33


class Machine:
    def __init__(self, image: bytes, chunk: int):
        self.uc = uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        uc.ctl_set_tlb_mode(UC_TLB_VIRTUAL)
        self.page_size = uc.ctl_get_page_size()
        uc.mem_map(RAM_BASE, RAM_SIZE)
        uc.mem_write(RAM_BASE + 0x80000, image)
        root, rsv = mkdtb.preset_virt()
        uc.mem_write(DTB_ADDR, mkdtb.build_dtb(root, rsv))
        self.out = bytearray()
        self.rx: collections.deque[int] = collections.deque()
        self.rx_irq_enabled = False
        self.pending: list[int] = []
        self.fault = None
        self.chunk = chunk
        self.powered_off = False
        self.debug = False
        self.exceptions = 0
        uc.mmio_map(UART, 0x1000, self._uart_r, None, self._uart_w, None)
        uc.mmio_map(GICD, 0x20000, self._gic_r, None, self._gic_w, None)
        uc.hook_add(UC_HOOK_TLB_FILL, self._tlb_fill)
        uc.hook_add(UC_HOOK_INSN, self._sys_insn, None, 1, 0, A.UC_ARM64_INS_SYS)
        uc.hook_add(UC_HOOK_INTR, self._intr)
        uc.hook_add(UC_HOOK_MEM_UNMAPPED, self._unmapped)
        uc.reg_write(A.UC_ARM64_REG_X0, DTB_ADDR)
        uc.reg_write(A.UC_ARM64_REG_PC, RAM_BASE + 0x80000)

    # ---- devices -------------------------------------------------------
    def _uart_r(self, uc, off, size, _):
        if off == 0x00:
            return self.rx.popleft() if self.rx else 0
        if off == 0x18:  # FR: TXFE always, RXFE when no input
            return 0x80 | (0 if self.rx else 0x10)
        return 0

    def _uart_w(self, uc, off, size, val, _):
        if off == 0x00:
            self.out.append(val & 0xFF)
        elif off == 0x38:
            self.rx_irq_enabled = bool(val & 0x50)

    def _gic_r(self, uc, off, size, _):
        if off == 0x004:
            return 7  # GICD_TYPER: 256 lines
        if off == 0x1000C:  # GICC_IAR
            return self.pending.pop(0) if self.pending else 1023
        return 0

    def _gic_w(self, uc, off, size, val, _):
        pass

    # ---- MMU -----------------------------------------------------------
    def _cp(self, crn, crm, op0, op1, op2):
        return self.uc.reg_read(A.UC_ARM64_REG_CP_REG, (crn, crm, op0, op1, op2))

    def _rd64(self, addr):
        return struct.unpack("<Q", self.uc.mem_read(addr, 8))[0]

    def walk(self, vaddr: int, access: int, el: int):
        """ARMv8 stage-1 walk (4K granule, 48-bit). Returns (pa, perms) or
        (None, (level, kind))."""
        hi = vaddr >> 48
        if hi not in (0, 0xFFFF):
            return None, (0, "translation")
        table = self._cp(2, 0, 3, 0, 1 if hi else 0) & 0x0000FFFFFFFFF000
        for level in range(4):
            idx = (vaddr >> (39 - 9 * level)) & 511
            desc = self._rd64(table + idx * 8)
            if not desc & 1:
                return None, (level, "translation")
            if level < 3 and desc & 2:
                table = desc & 0x0000FFFFFFFFF000
                continue
            if level != 2 and not desc & 2:  # only 2M blocks and 4K pages are used
                return None, (level, "translation")
            shift = 39 - 9 * level
            pa = (desc & 0x0000FFFFFFFFF000 & ~((1 << shift) - 1)) | (vaddr & ((1 << shift) - 1))
            if not desc & (1 << 10):
                return None, (level, "access")
            ap = (desc >> 6) & 3
            el0_ok, ro = bool(ap & 1), bool(ap & 2)
            uxn, pxn = bool(desc >> 54 & 1), bool(desc >> 53 & 1)
            perms = 0
            if el == 0:
                if el0_ok:
                    perms |= UC_PROT_READ | (0 if ro else UC_PROT_WRITE) | (0 if uxn else UC_PROT_EXEC)
            else:
                perms |= UC_PROT_READ | (0 if ro else UC_PROT_WRITE)
                if not pxn and not (el0_ok and not ro):  # EL0-writable implies PXN
                    perms |= UC_PROT_EXEC
            need = UC_PROT_EXEC if access == UC_MEM_FETCH else (
                UC_PROT_WRITE if access == UC_MEM_WRITE else UC_PROT_READ)
            if not perms & need:
                return None, (level, "permission")
            return pa, perms
        return None, (3, "translation")

    def _tlb_fill(self, uc, vaddr, access, entry, _):
        sctlr = self._cp(1, 0, 3, 0, 0)
        el = (uc.reg_read(A.UC_ARM64_REG_PSTATE) >> 2) & 3
        if not sctlr & 1:
            entry.paddr = vaddr
            entry.perms = UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC
            return True
        pa, info = self.walk(vaddr, access, el)
        if pa is None:
            return self._fault(vaddr, access, el, *info)
        entry.paddr = pa & ~(self.page_size - 1)
        entry.perms = info
        return True

    def _sys_insn(self, uc, reg, cp, _):
        """Emulate AT S1E1R (sys #0, c7, c8, #0, Xt) with our walker."""
        if (cp.op0, cp.op1, cp.crn, cp.crm, cp.op2) == (1, 0, 7, 8, 0):
            va = uc.reg_read(reg)
            pa, info = self.walk(va, 16, 1)
            par = 1 if pa is None else (pa & 0x0000FFFFFFFFF000)
            uc.reg_write(A.UC_ARM64_REG_PAR_EL1, par)
            return True
        return False

    def _fault(self, va, access, el, level, kind):
        fsc = {"translation": 0x04, "access": 0x08, "permission": 0x0C}[kind] | level
        fetch = access == UC_MEM_FETCH
        ec = (0x20 if fetch else 0x24) + (0 if el == 0 else 1)
        iss = fsc | (0 if fetch or access != UC_MEM_WRITE else 1 << 6)
        self.fault = ((ec << 26) | (1 << 25) | iss, va)
        return False

    # ---- exceptions ----------------------------------------------------
    def take_exception(self, kind: str, esr: int = 0, far: int | None = None) -> None:
        uc = self.uc
        pst = uc.reg_read(A.UC_ARM64_REG_PSTATE)
        el, spsel = (pst >> 2) & 3, pst & 1
        pc = uc.reg_read(A.UC_ARM64_REG_PC)
        base = 0x400 if el == 0 else (0x200 if spsel else 0x0)
        off = base + (0x80 if kind == "irq" else 0x0)
        cur_sp = uc.reg_read(A.UC_ARM64_REG_SP)
        if el == 0:
            uc.reg_write(A.UC_ARM64_REG_SP_EL0, cur_sp)
            new_sp = uc.reg_read(A.UC_ARM64_REG_SP_EL1)
        else:
            new_sp = cur_sp
        uc.cpr_write(3, 0, 4, 0, 0, pst)  # SPSR_EL1
        uc.reg_write(A.UC_ARM64_REG_ELR_EL1, pc)
        if kind == "sync":
            uc.reg_write(A.UC_ARM64_REG_ESR_EL1, esr)
            if far is not None:
                uc.reg_write(A.UC_ARM64_REG_FAR_EL1, far)
        uc.reg_write(A.UC_ARM64_REG_PSTATE, 0x3C5)
        # Unicorn does not rebuild its cached translation flags (current EL)
        # on a PSTATE write; rewriting SCTLR_EL1 forces that rebuild.
        uc.cpr_write(3, 0, 1, 0, 0, uc.cpr_read(3, 0, 1, 0, 0))
        uc.reg_write(A.UC_ARM64_REG_SP, new_sp)
        uc.reg_write(A.UC_ARM64_REG_PC, self._cp(12, 0, 3, 0, 0) + off)
        self.exceptions += 1
        if self.debug and kind != "irq":
            print(f"[uniboot] exc {kind} esr={esr:#x} far={far} pc={pc:#x} el={el}")

    def _intr(self, uc, intno, _):
        if self.debug:
            print(f"[intr] {intno} pc={uc.reg_read(A.UC_ARM64_REG_PC):#x} pstate={uc.reg_read(A.UC_ARM64_REG_PSTATE):#x}")
        self.pending_sync = None
        if intno == 2:  # SVC: PC already points past the instruction
            insn = struct.unpack("<I", uc.mem_read(uc.reg_read(A.UC_ARM64_REG_PC) - 4, 4))[0]
            self.pending_sync = ((0x15 << 26) | (1 << 25) | ((insn >> 5) & 0xFFFF), None)
        elif intno in (3, 4) and self.fault:
            self.pending_sync = self.fault
            self.fault = None
        elif intno == 1:  # UDEF
            self.pending_sync = ((1 << 25), None)
        elif intno in (11, 13):  # HVC/SMC -> PSCI (QEMU-style built-in firmware)
            fn = uc.reg_read(A.UC_ARM64_REG_X0)
            if fn in (0x84000008, 0x84000009):
                self.powered_off = True
                uc.emu_stop()
                return
            uc.reg_write(A.UC_ARM64_REG_X0, 0x10000 if fn == 0x84000000 else 0xFFFFFFFF)
            return
        else:
            self.pending_sync = ("stop", intno)
        uc.emu_stop()

    def _unmapped(self, uc, access, addr, size, value, _):
        print(f"\n[uniboot] unmapped physical access {addr:#x} at pc={uc.reg_read(A.UC_ARM64_REG_PC):#x}")
        return False

    # ---- driver loop ---------------------------------------------------
    def irqs_masked(self) -> bool:
        return bool(self.uc.reg_read(A.UC_ARM64_REG_PSTATE) & 0x80)

    def run(self, seconds: float) -> None:
        deadline = time.time() + seconds
        while time.time() < deadline and not self.powered_off:
            self.pending_sync = None
            try:
                self.uc.emu_start(self.uc.reg_read(A.UC_ARM64_REG_PC), 0, count=self.chunk)
            except UcError as e:
                if self.fault:
                    self.pending_sync, self.fault = self.fault, None
                else:
                    raise SystemExit(f"[uniboot] CPU error {e} at pc="
                                     f"{self.uc.reg_read(A.UC_ARM64_REG_PC):#x}")
            ps = self.pending_sync
            if ps and ps[0] == "stop":
                pc = self.uc.reg_read(A.UC_ARM64_REG_PC)
                if ps[1] in (0, 5, 0x10001):  # HLT/WFI style exits: just continue
                    continue
                raise SystemExit(f"[uniboot] unhandled exception {ps[1]} at pc={pc:#x}")
            if ps:
                self.take_exception("sync", ps[0], ps[1])
                continue
            if not self.irqs_masked():
                if self.rx and self.rx_irq_enabled and UART_INTID not in self.pending:
                    self.pending.append(UART_INTID)
                elif self.timer_due():
                    self.pending.append(TIMER_INTID)
                else:
                    continue
                self.take_exception("irq")

    def timer_due(self) -> bool:
        # Unicorn reads CNTP_CTL_EL0 back as 0, so key off CNTP_CVAL alone
        # (it is only non-zero once the kernel has programmed the timer).
        cval = self._cp(14, 2, 3, 3, 2)
        return cval != 0 and self._cp(14, 0, 3, 3, 1) >= cval  # CNTPCT >= CVAL

    def text(self) -> str:
        return self.out.decode(errors="replace").replace("\r", "")


class MachineLink:
    """send_kernel.Link look-alike that talks to the emulated UART."""

    def __init__(self, m: Machine):
        self.m, self.pos = m, 0

    def read(self, n: int = 4096) -> bytes:
        self.m.run(0.05)
        data = bytes(self.m.out[self.pos:])
        self.pos = len(self.m.out)
        return data

    def write(self, data: bytes) -> None:
        self.m.rx.extend(data)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image")
    ap.add_argument("--cmd", action="append", default=[], help="shell command to type")
    ap.add_argument("--expect", action="append", default=[], help="substring that must appear")
    ap.add_argument("--seconds", type=float, default=4.0, help="wall time per command")
    ap.add_argument("--chunk", type=int, default=20000, help="instructions between interrupt checks")
    ap.add_argument("--debug", action="store_true")
    ap.add_argument("--chainload", metavar="KERNEL",
                    help="IMAGE is chainloader.img; send KERNEL to it over the fake UART")
    a = ap.parse_args()

    m = Machine(Path(a.image).read_bytes(), a.chunk)
    m.debug = a.debug
    if a.chainload:
        send_kernel.send(MachineLink(m), Path(a.chainload).read_bytes(), timeout=60,
                         log=lambda s: print(s, file=sys.stderr))
    m.run(a.seconds)
    for c in a.cmd:
        if m.powered_off:
            break
        m.rx.extend((c + "\r").encode())
        m.run(a.seconds)
    out = m.text()
    print(out)
    print(f"[uniboot] {m.exceptions} exceptions delivered, powered_off={m.powered_off}")
    missing = [e for e in a.expect if e not in out]
    for e in missing:
        print(f"[uniboot] MISSING: {e!r}")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
