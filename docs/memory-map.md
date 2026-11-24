# Memory maps

All addresses are physical. The kernel runs identity-mapped, and TTBR1 adds
a linear alias at `0xffff_0000_0000_0000 + PA`.

## Kernel image layout (from `kernel/arch/linker.ld.S`)

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
