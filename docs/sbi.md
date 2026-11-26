# The built-in SBI shim (rv64)

On the `rv64` board OrbitOS runs without OpenSBI. The hart starts in
M-mode at `0x8000_0000` (orbit-rv reset, or QEMU `-bios none`), and
`kernel/arch/riscv64/boot.S` sets up a few hundred bytes of M-mode
firmware before dropping to S-mode with `mret`. The kernel then talks to
it through the standard [RISC-V SBI](https://github.com/riscv-non-isa/riscv-sbi-doc)
calling convention, so the S-mode code would also run on top of OpenSBI.

Files: `kernel/arch/riscv64/boot.S` (`sbi_entry`, the M trap vector),
`kernel/arch/riscv64/sbi.c` (`sbi_trap`, the handler),
`kernel/arch/riscv64/include/sbi.h` (IDs and the S-mode `sbi_call`).

## M-mode setup (boot.S)

| Step | CSR / device | Value |
|------|--------------|-------|
| park other harts | `mhartid != 0` | `wfi` loop |
| M trap vector | `mtvec` | `sbi_entry` (direct mode) |
| M stack | `mscratch` | `__mstack_top` (2 KiB, after the boot stack) |
| no stale tick | CLINT `mtimecmp` | all ones (QEMU resets it to 0) |
| PMP | `pmpaddr0` = `-1 >> 10`, `pmpcfg0` = `0x1f` | one NAPOT entry, RWX for S/U. Required on QEMU; orbit-rv has no PMP and ignores it |
| exception delegation | `medeleg` = `0xb1ff` | misaligned, access faults, illegal instruction, breakpoint, ecall from U, page faults |
| interrupt delegation | `mideleg` = `0x222` | SSI, STI, SEI |
| counters | `mcounteren` = 7 | S/U may read `cycle`, `time`, `instret` |
| interrupts | `mie` = MTIE | M only needs its timer; S enables STIE/SEIE in `sie` |
| translation | `satp` = 0 | S-mode turns Sv39 on itself |
| S trap vector | `stvec`, `sscratch` = 0 | `trap_entry` (kernel/arch/riscv64/entry.S) |
| drop to S | `mstatus.MPP` = S, `mepc` = `s_entry` | `mret`, then `kernel_main(dtb, 3)` |

`misa` is saved in `riscv_boot_misa` for the `info` command, because
S-mode cannot read it.

## Calls

`a7` = extension ID, `a6` = function ID, `a0..a1` = arguments. Returns
`a0` = error, `a1` = value (legacy calls return only `a0`).

| Extension | EID | FID | Behaviour |
|-----------|-----|-----|-----------|
| TIME `set_timer` | `0x54494D45` | 0 | `mtimecmp = a0`, clear `mip.STIP` |
| legacy `set_timer` | `0x00` | - | same |
| legacy `console_putchar` | `0x01` | - | poll LSR.THRE, write THR |
| legacy `console_getchar` | `0x02` | - | RBR or -1 |
| BASE | `0x10` | 0-6 | spec version 2.0, impl ID `0x4f524254` ("ORBT", unregistered), impl version 0.1.0, `probe_extension` (1 for the extensions in this table), `mvendorid`/`marchid`/`mimpid` |
| SRST `system_reset` | `0x53525354` | 0 | type 0 (shutdown) writes `0x5555` to the finisher; types 1/2 (cold/warm reboot) write `0x7777`. Other types return `SBI_ERR_INVALID_PARAM` |
| anything else | | | `SBI_ERR_NOT_SUPPORTED` (-2) |

The finisher is the `sifive,test0`-compatible device at `0x0010_0000`
(QEMU virt and orbit-rv): `0x5555` powers off, `0x7777` resets (the
orbit-rv simulators stop), `0x3333 | code << 16` reports a failure.

## Timer forwarding

S-mode cannot program the CLINT, so every tick goes through M-mode:

```mermaid
sequenceDiagram
    participant S as S-mode kernel (sbi_timer.c)
    participant M as SBI shim (sbi.c)
    participant C as CLINT
    S->>M: ecall TIME set_timer(deadline)
    M->>C: mtimecmp = deadline
    M->>M: clear mip.STIP
    Note over C: mtime reaches mtimecmp
    C->>M: machine timer interrupt (MTIP)
    M->>C: mtimecmp = all ones (one shot)
    M->>M: set mip.STIP
    M-->>S: mret; STIP is delegated, so S takes a supervisor timer interrupt
    S->>M: ecall set_timer(next deadline), which also acknowledges STIP
```

Deadlines are absolute and fixed-rate (`next += timebase / HZ`); if the
kernel falls behind, missed ticks are skipped instead of firing back to
back.

## Unexpected traps

Anything else that reaches M-mode (an ecall from M, a fault inside the
shim, a non-delegated exception) is printed straight to the UART with
`mcause`, `mepc`, `mtval` and `mstatus`, and the finisher is written with
`0x3333 | 1 << 16`, so QEMU and the ISS exit with a failure status instead
of hanging.

## Limits

Single hart: no IPI, RFENCE or HSM extensions. No misaligned-access
emulation (the platform traps misaligned accesses and the kernel never
makes them). The shim trusts S-mode: it is there to replace firmware,
not to isolate the kernel from it.
