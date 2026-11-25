# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [0.1.0] - 2026-11-25

First public release of a hobby AArch64 kernel for the Raspberry Pi 5, the base for a future phone and smartwatch OS.

### Added
- boot.S with EL2 to EL1 drop and secondary-core parking; UART chainloader with CRC-32
- FDT parser, GICv2 + generic timer preemption, ESR decoder
- Buddy allocator, kmalloc, 4-level MMU with a higher-half alias
- Priority round-robin scheduler, mutexes and semaphores, EL0 tasks with SVC syscalls, UART shell
- Boots in QEMU virt in CI (direct at EL2, and chainloaded), with both zig and GNU gcc builds

[0.1.0]: https://github.com/coopersimpson101-debug/orbit-os/releases/tag/v0.1.0
