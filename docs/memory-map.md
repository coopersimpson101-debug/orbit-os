# Memory maps

All addresses are physical. The kernel runs identity-mapped, and TTBR1 adds
a linear alias at `0xffff_0000_0000_0000 + PA` (AArch64) or the Sv39 upper
half adds one at `0xffff_ffc0_0000_0000 + PA` (rv64).

## Kernel image layout (from `kernel/arch/aarch64/linker.ld.S`)

Example: the qemu-virt build at the time of writing (`python build.py`; the
exact numbers change as code grows):

| Symbol / section | Address | Notes |
|------------------|---------|-------|
| `_start`, `.text.boot` | `0x4008_0000` | Linux Image header + entry |
| `__user_text_start` .. `__user_text_end` | `0x4008_1000` .. `0x4008_2000` | EL0 code + its strings |
| `.text` (`vectors` first, 2 KiB aligned) | `0x4008_2000` .. `__text_end` | |
| `.rodata` | .. `__rodata_end` (`0x4009_1000`) | |
| `.data`, `.bss` (page tables, thread table) | `0x4009_1000` .. | NOLOAD BSS zeroed by boot.S |
| `.user_stack` | `0x400c_5000` .. `0x400c_9000` | 4 x 4 KiB EL0 stacks |
| boot stack (idle thread) | 16 KiB, top `0x400c_d000` | |
| `_end` | `0x400c_d000` | ~308 KiB including BSS |
| buddy metadata | `_end` .. +1 byte/page | |
| buddy-managed pages | after metadata .. end of RAM (capped at 4 GiB) | minus DTB + /memreserve/ |

The rpi5 build has the same layout shifted to `KERNEL_BASE = 0x8_0000`.

## qemu-virt (`-M virt,gic-version=2`)

| Range | What |
|-------|------|
| `0x0800_0000` | GIC distributor (GICD) |
| `0x0801_0000` | GIC CPU interface (GICC) |
| `0x0900_0000` | PL011 UART (SPI 1 = INTID 33) |
| `0x0800_0000` .. `0x1000_0000` | device window mapped Device-nGnRE |
| `0x4000_0000` | RAM base (QEMU boot stub lives in the first page) |
| `0x4008_0000` | kernel / chainloader load address (RAM + text_offset) |
| `0x4200_0000` | chainloader run address after self-relocation |
| `0x4800_0000` | typical DTB location (QEMU picks it; we read x0) |

## Raspberry Pi 5 (BCM2712), expected, not yet verified on hardware

| Range | What | Confidence |
|-------|------|------------|
| `0x0` .. | RAM (first bank; size from DTB `/memory`) | high |
| `0x8_0000` | kernel load address (`kernel_address=0x80000` in config.txt) | medium: firmware may pick another address for arm64 images. boot.S self-relocates either way |
| `0x200_0000` | chainloader run address after self-relocation | ours |
| `0x10_7c00_0000` .. `0x10_8000_0000` | SoC peripheral window mapped Device-nGnRE | medium |
| `0x10_7d00_1000` | debug UART (PL011), 3-pin JST header | medium-high (matches `earlycon=pl011,0x107d001000` used by Linux on Pi 5) |
| `0x10_7fff_9000` | GIC-400 distributor | medium (from bcm2712 DT) |
| `0x10_7fff_a000` | GIC-400 CPU interface | medium |
| RP1 (GPIO UARTs, Ethernet, USB) | behind PCIe2 | not accessible without a PCIe + RP1 driver |

The firmware's DTB overrides these defaults where it can (see
`kernel/core/platform.c`), but only inside the device window, so a
surprising DT cannot point the console at an unmapped address.

## rv64 (orbit-rv SoC, QEMU riscv64 `virt` subset)

Device addresses are the same on both machines (`kernel/arch/riscv64/include/rv64_map.h`).

| Range | What |
|-------|------|
| `0x0010_0000` | finisher (`sifive,test0`): `0x5555` off, `0x7777` reset, `0x3333 \| code << 16` fail. Used by the M-mode shim only, not mapped for S |
| `0x0200_0000` | CLINT: `msip` +0, `mtimecmp` +`0x4000`, `mtime` +`0xbff8` (10 MHz). M-mode only |
| `0x0c00_0000` | PLIC: priority +4·src, pending +`0x1000`, enable +`0x2000`+`0x80`·ctx, threshold/claim +`0x20_0000`+`0x1000`·ctx; context 1 = hart 0 S-mode |
| `0x1000_0000` | NS16550 UART, byte registers at stride 1, PLIC source 10, `UART_CLK_HZ` 25 MHz on orbit-rv (115200 8N1, divisor 14) |
| `0x8000_0000` | RAM and reset PC. The kernel assumes 256 KiB (`RAM_SIZE`), also under QEMU (`-m 128M` is capped) |
| `0x87e0_0000` (typ.) | DTB under QEMU (2 MiB aligned near the top of its RAM; mapped read-only by megapage) |

Kernel image (`kernel/arch/riscv64/linker.ld.S`, `-Os`, at the time of writing):

| Symbol / section | Address | Notes |
|------------------|---------|-------|
| `_start`, `.text.boot` | `0x8000_0000` | M-mode entry + SBI trap vector `sbi_entry` |
| `.user` | `0x8000_1000` .. `0x8000_2000` | U-mode code + its strings |
| `.text` | `0x8000_2000` .. `0x8000_b000` | `trap_entry` (stvec) inside |
| `.rodata` | .. `0x8000_d000` | |
| `.bss` | `0x8000_d000` .. `0x8001_5050` | page tables (5 used of 6), thread table, RX ring |
| `.user_stack` | `0x8001_6000` .. `0x8001_a000` | 4 x 4 KiB U-mode stacks |
| boot stack (idle thread) | 8 KiB, top `0x8001_c000` | |
| M-mode stack (SBI shim) | 2 KiB, top `0x8001_c800` | |
| `_end` | `0x8001_d000` | 116 KiB including BSS and stacks |
| buddy metadata | `0x8001_d000` (one page) | |
| buddy-managed pages | `0x8001_e000` .. `0x8004_0000` | 34 pages (136 KiB): 8 KiB thread stacks, heap |

`kernel.bin` (the flat image of the loadable sections, ~50 KB) is what an
FPGA bitstream would preload into block RAM; `kernel.elf` is what QEMU
and the ISS load. The link fails (`ASSERT`) if `_end` passes
`0x8000_0000 + RAM_SIZE`, and pytest requires at least 64 KiB of headroom.
