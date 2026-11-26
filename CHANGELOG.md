# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
- `rv64` board: 64-bit RISC-V port (RV64IMA_Zicsr_Zifencei, no C/F/D) for the orbit-rv SoC and QEMU riscv64 `virt` (`-bios none`), fitting in 256 KiB of RAM; `python build.py` builds `build/rv64/kernel.elf` and `kernel.bin`
- M-mode entry with PMP, exception/interrupt delegation and a built-in SBI shim (TIME, SRST, BASE, legacy console) instead of OpenSBI (docs/sbi.md)
- S-mode trap vector with a full trap frame, Sv39 page tables with W^X kernel sections, A/D pre-set and a higher-half alias, SBI timer preemption at 100 Hz, PLIC + NS16550 driver with RX interrupts, U-mode tasks with `ecall` syscalls and SUM-guarded user copies
- Portable scause decoder and Sv39 PTE builder/walker with host unit tests; rv64 image checks (no RVC, entry, ISA attribute, RAM budget)
- `tools/iss_test.py`: scripted boot on the orbit-rv golden ISS (pytest, skipped without `ORBIT_RV_PATH`); `tools/qemu_test.py --board rv64` and an `rv64-qemu` CI job; rv64 images in release artefacts

### Changed
- Arch code moved to `kernel/arch/aarch64/` (including `trap.c`, `mmu.c` and `arch.h`) behind `arch.h`/`context.h`/`hal.h`; UART RX ring and tick bookkeeping shared (`drivers/serial.c`, `core/clock.c`)
- `build.py` compiles in parallel (`-j`)
- `info` prints an `arch` line; RAM and buddy sizes below 1 MiB are shown in KiB

## [0.1.0] - 2026-11-25

First public release of a hobby AArch64 kernel for the Raspberry Pi 5, the base for a future phone and smartwatch OS.

### Added
- boot.S with EL2 to EL1 drop and secondary-core parking; UART chainloader with CRC-32
- FDT parser, GICv2 + generic timer preemption, ESR decoder
- Buddy allocator, kmalloc, 4-level MMU with a higher-half alias
- Priority round-robin scheduler, mutexes and semaphores, EL0 tasks with SVC syscalls, UART shell
- Boots in QEMU virt in CI (direct at EL2, and chainloaded), with both zig and GNU gcc builds

[0.1.0]: https://github.com/coopersimpson101-debug/orbit-os/releases/tag/v0.1.0
