# Contributing to orbit-os

Thanks for your interest! Bug reports, fixes and hardware test results are all welcome.

## Setup

```bash
pip install ziglang pytest pyelftools unicorn
python build.py
python -m pytest -q tests
python tools/qemu_test.py   # if QEMU is installed
```

## Ground rules

- Keep CI green: host tests plus the QEMU `virt` boot (direct and chainload).
- New portable logic (parsers, allocators, queues) gets a host C unit test in `tests/`.
- Board-specific values go in `kernel/board/` or come from the device tree, never hard-coded in drivers.
- Hardware claims need hardware: say what board and firmware you tested on, or mark it as QEMU/emulated only.
- One logical change per PR; describe what you tested and where.
- Follow the style of the surrounding code (`.editorconfig` covers whitespace).

## Reporting results

Numbers and boot logs from real hardware are especially valuable. Open an issue
with the exact command, the full output, and the machine details.
