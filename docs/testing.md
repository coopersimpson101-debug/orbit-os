# Testing

There are three levels, from fastest to most realistic.

## 1. Host unit tests (`python -m pytest tests`)

The portable modules are compiled natively with `zig cc`. `tests/test.h` is a
tiny C harness, and pytest builds and runs each test program:

| Test | Covers |
|------|--------|
| `test_buddy.c` | add/coalesce, split, alignment, double/invalid free, holes (reserved ranges), exhaustion, 20k-op random stress with overlap checks, `buddy_check()` invariants |
| `test_kheap.c` | size classes, alignment, LIFO reuse, large allocations, invalid frees, 600 mixed allocations with content checks, OOM recovery |
| `test_runqueue.c` | priority order, FIFO within a level, round-robin rotation, removal, error cases |
| `test_fdt.c` | DTB from `tools/mkdtb.py --preset rpi5-like`: paths, unit addresses, `ranges` translation to `0x10_7d00_1000`, untranslatable RP1 node, compatible lists, status, aliases + stdout-path, timer PPI, `/memreserve/` |
| `test_shell_parse.c` | line editor (CR/LF/CRLF, backspace/DEL, Ctrl-U, Ctrl-C, ANSI escapes, overflow), tokenizer with quotes, `parse_u64` overflow |
| `test_printf.c` | `k_snprintf` compared against the host libc across a format matrix, truncation semantics |
| `test_esr.c` | ESR decoding text for aborts, SVC, BRK, unknown |
| `test_chainproto.c` | CRC-32 check value, framing, bad CRC/size recovery, and a frame produced by `send_kernel.py` |
| `test_string.c` | mem*/str* helpers, overlapping memmove |

pytest also cross-builds both boards and checks the Linux Image header,
`kernel_2712.img`, and section alignment (with `pyelftools`).

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
