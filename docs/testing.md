# Testing

There are three levels, from fastest to most realistic.

## 1. Host unit tests (`python -m pytest tests`)

The portable modules are compiled natively with `zig cc`. `tests/test.h` is a
tiny C harness, and pytest builds and runs each test program:

| Test | Covers |
|------|--------|
| `test_buddy.c` | add/coalesce, split, alignment, double/invalid free, holes (reserved ranges), exhaustion, 20k-op random stress with overlap checks, overlapping `buddy_add_range` calls (regression) and 200 random overlapping-add rounds against a page bitmap, `buddy_check()` invariants |
| `test_a64pt.c` | AArch64 stage-1 builder: unaligned ranges cover their last page (regression), 2 MiB blocks vs 4 KiB pages, first mapping wins, ranges under an existing block, higher-half alias, table exhaustion, all via the software walk |
| `test_kheap.c` | size classes, alignment, LIFO reuse, large allocations, invalid frees, 600 mixed allocations with content checks, OOM recovery |
| `test_runqueue.c` | priority order, FIFO within a level, round-robin rotation, removal, error cases |
| `test_fdt.c` | DTB from `tools/mkdtb.py --preset rpi5-like`: paths, unit addresses, `ranges` translation to `0x10_7d00_1000`, untranslatable RP1 node, compatible lists, status, aliases + stdout-path, timer PPI, `/memreserve/` |
| `test_shell_parse.c` | line editor (CR/LF/CRLF, backspace/DEL, Ctrl-U, Ctrl-C, ANSI escapes, overflow), tokenizer with quotes, `parse_u64` overflow |
| `test_printf.c` | `k_snprintf` compared against the host libc across a format matrix, truncation semantics |
| `test_esr.c` | ESR decoding text for aborts, SVC, BRK, unknown |
| `test_scause.c` | scause decoding text for page/access faults (U and S), misaligned, illegal instruction (insn word), breakpoint, ecall, interrupts, reserved codes, truncation |
| `test_sv39.c` | Sv39 PTE bits (A/D pre-set, no reserved bits), VPN/canonical checks, `satp`, mapping at 4 KiB/2 MiB/1 GiB, no overwrite of finer mappings, permissions via the software walk, table exhaustion, misaligned superpages, reserved W-without-R encoding |
| `test_chainproto.c` | CRC-32 check value, framing, bad CRC/size recovery, and a frame produced by `send_kernel.py` |
| `test_string.c` | mem*/str* helpers, overlapping memmove |

pytest also cross-builds every board and checks the Linux Image header,
`kernel_2712.img`, and section alignment (with `pyelftools`). For rv64 it
checks that `kernel.elf` is a little-endian ELF64 RISC-V file with entry
`0x8000_0000`, no RVC flag in `e_flags` and no 16-bit encodings anywhere
in code, an lp64 soft-float ABI, an ISA attribute of only I/M/A + Zicsr/
Zifencei, and that the image including BSS and stacks leaves at least
64 KiB of the 256 KiB RAM for the page allocator.

`python build.py host-tests` runs the same C tests without pytest.

## 2. Unicorn smoke boot (`tools/uniboot.py`, optional)

If `pip install unicorn` is available, pytest boots the real
`build/virt/kernel8.img` on Unicorn's AArch64 core, drives the shell over a
fake PL011, and checks the results of `spawn user/ufault/fault/mutex/pc/spin`.
It also tests the full chainloader path, sending the kernel to
`chainloader.img` with the same Python code as `send_kernel.py`. The harness emulates
the GIC, exception entry and stage-1 translation (a Python walker of the
kernel's own page tables with AP/UXN/PXN checks), because Unicorn does not
provide them for bare-metal code. Limitations: it starts at EL1, so the EL2 drop
is not exercised. FAR is reported at page granularity. Timing is approximate.

## 2b. orbit-rv golden ISS (`tools/iss_test.py`, optional)

The rv64 kernel is booted on the instruction-set simulator of the orbit-rv
SoC (a separate repository) through its Python API, with 256 KiB of RAM.
The test types `help`, `info`, `ps`, `spawn spin`, `spawn user`,
`spawn ufault`, `uptime` and `poweroff` into the 16550 model (RX
interrupts through the PLIC model), checks the output, and requires the
SBI SRST shutdown to stop the hart through the finisher with exit code 0.
`--full` adds `mem`, `echo`, `peek`/`poke`, `counter`, `mutex`, `pc` and
`fault`.

```sh
ORBIT_RV_PATH=../orbit-rv python tools/iss_test.py [--full] [--transcript uart.txt]
ORBIT_RV_PATH=../orbit-rv python -m pytest -q tests -k iss   # skipped without orbit_rv
```

The ISS derives `mtime` from the instruction count; the test uses 1
instruction per 10 MHz tick (a 10 MHz IPC-1 hart) so a 10 ms tick is
100k instructions, and the rv64 build shortens the `spin` demo to 100 ms
(`SPIN_DEMO_MS`), still two 50 ms slices. The full session is about 2.5M
instructions (about 35 s at 70k instructions per second).

## 3. QEMU (`tools/qemu_test.py`, CI)

```sh
sudo apt install qemu-system-arm
python build.py --board virt
python tools/qemu_test.py --mode direct      # -kernel kernel8.img, enters at EL2
python tools/qemu_test.py --mode chainload   # chainloader.img + TCP serial
python tools/qemu_test.py --interactive      # play with the shell
```

The machine is `-M virt,gic-version=2,virtualization=on -cpu cortex-a72 -smp 4`.
`virtualization=on` makes QEMU enter the kernel at EL2, as the Pi 5 firmware
is expected to do, and makes QEMU's built-in PSCI use SMC. The script runs 16
shell commands, checks their output, and requires `poweroff` (PSCI
SYSTEM_OFF) to make QEMU exit.

For rv64 (CI job `rv64-qemu`):

```sh
sudo apt install qemu-system-misc
python build.py --board rv64
python tools/qemu_test.py --board rv64                 # scripted session, 18 commands
python tools/qemu_test.py --board rv64 --interactive   # Ctrl-A X to quit
# or by hand:
qemu-system-riscv64 -M virt -bios none -m 128M -nographic -kernel build/rv64/kernel.elf
```

`-bios none` makes QEMU start the kernel in M-mode at `0x8000_0000` with
the DTB in `a1`, exactly like the orbit-rv reset (which passes no DTB), so
the built-in SBI shim is exercised. The session additionally checks that
the DTB was used (`board    : rv64 (qemu-virt, from DTB)`, RAM capped to
256 KiB) and that QEMU exits with status 0 after the finisher write.
