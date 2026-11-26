# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [0.2.0] - 2026-11-26

Adds a 64-bit RISC-V port for the orbit-rv CPU and QEMU, and fixes two latent memory-management bugs found by formal verification.

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
- AArch64 page-table construction moved from `arch/aarch64/mmu.c` into the portable, host-tested `lib/a64pt.c` (`a64pt_map`, `a64pt_walk`; new `tests/test_a64pt.c`), mirroring `lib/sv39.c`

### Fixed
- `buddy_add_range` over pages that were already managed double-managed them when the range started inside an existing block: with a 4-page span, `add(0, 4)` then `add(1, 1)` gave 5 free pages (still passing `buddy_check()`) and page 1 was handed out by two live allocations. Already-managed pages are now skipped page-granularly, as the API comment always promised. `mm_init` only adds disjoint holes, so no shipped boot path hit this. Shared by both ports (`kernel/mm/buddy.c`). Regression tests in `tests/test_buddy.c`
- AArch64 `map_range` computed the end as `ALIGN_DOWN(va) + ALIGN_UP(size)`, so an unaligned range lost its last page (`map(0x1800, 0x1000)` left 0x2000-0x27ff unmapped); `mmu_init` maps the DTB this way, harmless today only because the DTB lies inside mapped RAM on both boards. The end is now rounded up before `va` is rounded down. The rv64 Sv39 code (`lib/sv39.c`) already did this and was not affected. Regression tests in `tests/test_a64pt.c`
- Both bugs were found by the Lean model and differential tests in [proofstack](https://github.com/coopersimpson101-debug/proofstack)

## [0.1.0] - 2026-11-25

First public release of a hobby AArch64 kernel for the Raspberry Pi 5, the base for a future phone and smartwatch OS.

### Added
- boot.S with EL2 to EL1 drop and secondary-core parking; UART chainloader with CRC-32
- FDT parser, GICv2 + generic timer preemption, ESR decoder
- Buddy allocator, kmalloc, 4-level MMU with a higher-half alias
- Priority round-robin scheduler, mutexes and semaphores, EL0 tasks with SVC syscalls, UART shell
- Boots in QEMU virt in CI (direct at EL2, and chainloaded), with both zig and GNU gcc builds

[0.2.0]: https://github.com/coopersimpson101-debug/orbit-os/releases/tag/v0.2.0
[0.1.0]: https://github.com/coopersimpson101-debug/orbit-os/releases/tag/v0.1.0
