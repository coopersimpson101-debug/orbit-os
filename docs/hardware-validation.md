# Hardware validation: TODO for the author

> **Status: OrbitOS has NOT yet been booted on a Raspberry Pi 5.**
> Everything Pi 5-specific comes from public device trees and
> documentation, plus build-time checks. None of it has been tried on
> hardware. The result slots below are intentionally empty. Fill them in
> with what you actually observe.

## What you need

* Raspberry Pi 5 with up-to-date bootloader EEPROM (`sudo rpi-eeprom-update -a`
  from Raspberry Pi OS)
* microSD card with a FAT32 first partition
* 3.3 V USB-serial adapter for the **debug UART**: the 3-pin JST-SH connector
  labelled "UART" between the two micro-HDMI ports. The Raspberry Pi Debug
  Probe plugs in directly. Pins: TX, GND, RX at 3.3 V. Never connect 5 V.
  Use 115200 baud, 8N1, no flow control.
* `python build.py --board rpi5` → `build/rpi5/kernel_2712.img` (same bytes
  as `kernel8.img`) and `build/rpi5/chainloader.img`

## SD card layout

Pi 5 firmware lives in the EEPROM, so the boot partition does **not** need
`start*.elf` / `fixup*.dat`. It does need the device tree:

```
/boot (FAT32)
├── config.txt
├── kernel_2712.img          # OrbitOS kernel OR chainloader.img renamed
├── bcm2712-rpi-5-b.dtb      # from github.com/raspberrypi/firmware, boot/
└── overlays/                # optional, from the same repo
```

`config.txt`:

```ini
arm_64bit=1
kernel=kernel_2712.img
# Ask for our link address. boot.S self-relocates if the firmware ignores it.
kernel_address=0x80000
# Debug UART on the 3-pin header + firmware/bootloader logs on it
enable_uart=1
uart_2ndstage=1
# Firmware OS compatibility check: disable for a non-Linux kernel
os_check=0
```

If the firmware refuses `kernel_2712.img`, try naming the file `kernel8.img`
and set `kernel=kernel8.img`.

## Test procedure and result slots

### 1. Direct boot

Copy `build/rpi5/kernel_2712.img`, connect the serial adapter, open a
terminal at 115200 baud, and power on.

Expected (abridged):

```
OrbitOS v0.1.0 (rpi5) - hobby AArch64 kernel
[boot] entered at EL2, now at EL1, dtb=0x..., image 0x80000-...
[fdt] device tree at 0x..., N bytes
[boot] RAM 0x0 + ... MiB (dtb /memory reg[0])
[mmu] enabled: ...
[mmu] higher-half alias self-test: OK
[gic] GICv2 at D=0x107fff9000 C=0x107fffa000, ... interrupt lines
[timer] 100 Hz tick on INTID 30 (CNTFRQ 54000000 Hz)
OrbitOS shell - type 'help'
orbit>
```

| Check | Result (fill in) |
|-------|------------------|
| Firmware log appears on debug UART (`uart_2ndstage=1`) | |
| OrbitOS banner appears | |
| Reported entry EL | |
| DTB address / size reported | |
| RAM size from DTB | |
| UART source (`info`): stdout-path or scan? address? | |
| GIC addresses (`info`) and line count | |
| CNTFRQ value | |
| `ticks` increases (timer IRQ works) | |
| Typing works; RX mode (`info`): IRQ or polled | |
| `spawn spin` → `preempted: yes` | |
| `spawn user` → `exited with code 42` | |
| `spawn ufault` / `spawn fault` killed, shell survives | |
| `reboot` resets the board (PSCI SYSTEM_RESET) | |
| `poweroff` | |

### 2. Chainloader

Put `build/rpi5/chainloader.img` on the card as `kernel_2712.img`, then:

```sh
python tools/send_kernel.py --port /dev/ttyUSB0 build/rpi5/kernel8.img --monitor
```

| Check | Result (fill in) |
|-------|------------------|
| `[orbit-chainloader] ready. load=... dtb=...` printed | |
| Transfer OK and kernel boots | |
| Transfer time for N KiB | |
| `reboot` then resend works | |

## Known risks / things most likely to need fixing on real hardware

1. **Load address.** The firmware may not honour `kernel_address` for
   images with an arm64 header. `boot.S` copies the image to its link
   address, but if the two regions overlap badly the copy loop could
   overwrite itself. Fix: link at whatever the firmware uses, or make the
   kernel position independent.
2. **Debug UART clock.** The kernel deliberately does not reprogram the
   PL011 baud divisors on the Pi 5 (`uart_clock = 0`) because the
   reference clock is not documented. It relies on `enable_uart=1`. If
   there is no output at all, check this first.
3. **Caches at entry.** Page tables are written with the MMU and D-cache off and
   then the MMU is enabled with caches on. There is no set/way cache
   invalidate. If the firmware left dirty or stale lines, add a D-cache
   invalidate by set/way before `mmu_init()`.
4. **GIC group configuration.** GICv2 in the non-secure state only sees
   Group 1 interrupts. This assumes TF-A configured SPIs/PPIs as Group 1, as
   it does for Linux.
5. **Peripheral addresses.** UART `0x10_7d00_1000` and GIC
   `0x10_7fff_9000/a000` come from the bcm2712 device tree. Use `peek` to
   confirm (GICD_TYPER at `0x107fff9004`, UART FR at `0x107d001018`).
6. **PSCI conduit.** Expected `smc` from `/psci`. `info` shows what the DT said.
7. **Secondary cores.** These are expected to stay off under PSCI. If one ever reaches
   `_start`, it parks in WFE (MPIDR Aff0-2 != 0).
