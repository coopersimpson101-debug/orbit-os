#!/usr/bin/env python3
"""OrbitOS build driver.

Uses the Zig toolchain (``pip install ziglang``) as a hermetic cross
compiler: ``zig cc`` (clang) for C/asm, ``zig ld.lld`` to link and
``zig objcopy`` to produce raw images. No system cross-gcc needed.

    python build.py                    # every board (virt, rpi5, rv64)
    python build.py --board rv64       # one board
    python build.py -j 1               # serial build (default: one job per CPU)
    python build.py host-tests         # compile + run host unit tests
    python build.py clean

Outputs (per board) in build/<board>/:
    aarch64 (virt, rpi5): kernel.elf, kernel8.img (rpi5 also: kernel_2712.img),
                          chainloader.elf, chainloader.img
    riscv64 (rv64):       kernel.elf (load at 0x8000_0000), kernel.bin (flat)
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"
K = ROOT / "kernel"

BOARDS = {
    "virt": {
        "arch": "aarch64",
        "kernel_base": "0x40080000",
        "chain_base": "0x42000000",
        "uart_base": "0x09000000",
        "uart_clock": "24000000",
        "mcpu": "cortex_a72",
    },
    "rpi5": {
        "arch": "aarch64",
        "kernel_base": "0x80000",
        "chain_base": "0x2000000",
        "uart_base": "0x107d001000",
        "uart_clock": "0",
        "mcpu": "cortex_a76",
    },
    # orbit-rv SoC + QEMU riscv64 virt subset. RV64IMA_Zicsr_Zifencei only
    # (no C, no F/D); everything must fit in 256 KiB of block RAM.
    "rv64": {
        "arch": "riscv64",
        "kernel_base": "0x80000000",
        "ram_size": "0x40000",
        "mcpu": "generic_rv64+m+a+zicsr+zifencei",
        "defines": {"KSTACK_ORDER": "1", "SPIN_DEMO_MS": "100"},
    },
}

TEXT_OFFSET = "0x80000"

# Architecture-independent kernel sources.
COMMON_SOURCES = [
    "core/main.c",
    "core/console.c",
    "core/clock.c",
    "core/platform.c",
    "core/syscall.c",
    "core/demos.c",
    "drivers/serial.c",
    "fdt/fdt.c",
    "lib/kstring.c",
    "lib/kprintf.c",
    "mm/buddy.c",
    "mm/kheap.c",
    "mm/mm.c",
    "sched/runqueue.c",
    "sched/sched.c",
    "sched/sync.c",
    "shell/shell.c",
    "shell/shell_parse.c",
    "user/user_prog.c",
]

ARCH_SOURCES = {
    "aarch64": [
        "arch/aarch64/boot.S",
        "arch/aarch64/vectors.S",
        "arch/aarch64/switch.S",
        "arch/aarch64/cpu.c",
        "arch/aarch64/trap.c",
        "arch/aarch64/mmu.c",
        "drivers/pl011.c",
        "drivers/gic.c",
        "drivers/timer.c",
        "lib/esr.c",
        "lib/chainproto.c",
        "lib/a64pt.c",
    ],
    "riscv64": [
        "arch/riscv64/boot.S",
        "arch/riscv64/entry.S",
        "arch/riscv64/switch.S",
        "arch/riscv64/cpu.c",
        "arch/riscv64/sbi.c",
        "arch/riscv64/trap.c",
        "arch/riscv64/mmu.c",
        "drivers/ns16550.c",
        "drivers/plic.c",
        "drivers/sbi_timer.c",
        "lib/scause.c",
        "lib/sv39.c",
    ],
}

# Kept for tools that list every kernel source of the AArch64 build.
KERNEL_SOURCES = COMMON_SOURCES + ARCH_SOURCES["aarch64"]

# Portable sources unit-tested on the host, and which test uses them.
HOST_TESTS = {
    "test_printf": ["lib/kprintf.c", "lib/kstring.c"],
    "test_string": ["lib/kstring.c"],
    "test_buddy": ["mm/buddy.c"],
    "test_kheap": ["mm/kheap.c", "mm/buddy.c"],
    "test_runqueue": ["sched/runqueue.c"],
    "test_fdt": ["fdt/fdt.c", "lib/kstring.c"],
    "test_shell_parse": ["shell/shell_parse.c"],
    "test_esr": ["lib/esr.c", "lib/kprintf.c"],
    "test_chainproto": ["lib/chainproto.c"],
    "test_scause": ["lib/scause.c", "lib/kprintf.c"],
    "test_sv39": ["lib/sv39.c"],
    "test_a64pt": ["lib/a64pt.c"],
}

JOBS = os.cpu_count() or 1


def zig() -> list[str]:
    exe = shutil.which("zig")
    if exe and os.environ.get("ORBIT_USE_SYSTEM_ZIG"):
        return [exe]
    return [sys.executable, "-m", "ziglang"]


def run(cmd: list[str], verbose: bool) -> None:
    if verbose:
        print(" ".join(str(c) for c in cmd))
    r = subprocess.run([str(c) for c in cmd])
    if r.returncode != 0:
        raise SystemExit(f"command failed ({r.returncode}): {' '.join(map(str, cmd))}")


COMMON_CFLAGS = [
    "-std=gnu11", "-g",
    "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
    "-fno-sanitize=all", "-fno-pic", "-fno-pie",
    "-fno-omit-frame-pointer", "-fno-unwind-tables",
    "-fno-asynchronous-unwind-tables",
    "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
]


def cross_cflags(board: dict) -> list[str]:
    arch = board["arch"]
    if arch == "aarch64":
        flags = ["-target", "aarch64-freestanding-none", f"-mcpu={board['mcpu']}", "-O2",
                 "-mgeneral-regs-only", "-mstrict-align", f"-DTEXT_OFFSET={TEXT_OFFSET}"]
    else:
        # -Os keeps the image small for 256 KiB of RAM; -mno-relax keeps
        # the code independent of a global pointer.
        flags = ["-target", "riscv64-freestanding-none", f"-mcpu={board['mcpu']}",
                 "-mabi=lp64", "-mcmodel=medany", "-mno-relax", "-msmall-data-limit=0",
                 "-Os", f"-DRAM_SIZE={board['ram_size']}"]
    flags += [f"-D{k}={v}" for k, v in board.get("defines", {}).items()]
    return flags + COMMON_CFLAGS + [f"-I{K / 'arch' / arch / 'include'}", f"-I{K / 'include'}"]


def preprocess_ld(src: Path, out: Path, defines: dict[str, str], verbose: bool) -> None:
    cmd = zig() + ["cc", "-E", "-P", "-undef", "-x", "c"]
    cmd += [f"-D{k}={v}" for k, v in defines.items()]
    cmd += [str(src), "-o", str(out)]
    run(cmd, verbose)


def compile_objs(sources: list[Path], objdir: Path, cflags: list[str], verbose: bool,
                 jobs: int | None = None) -> list[Path]:
    jobs = jobs or JOBS
    objs = []
    cmds = []
    for src in sources:
        rel = src.relative_to(ROOT)
        obj = objdir / rel.with_suffix(".o")
        obj.parent.mkdir(parents=True, exist_ok=True)
        cmds.append(zig() + ["cc"] + cflags + ["-c", str(src), "-o", str(obj)])
        objs.append(obj)
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        list(pool.map(lambda c: run(c, verbose), cmds))
    return objs


def link(objs: list[Path], ld: Path, elf: Path, img: Path, verbose: bool) -> None:
    run(zig() + ["ld.lld", "-T", str(ld), "-nostdlib", "-static",
                 "--build-id=none", "-z", "max-page-size=4096",
                 "-o", str(elf)] + [str(o) for o in objs], verbose)
    run(zig() + ["objcopy", "-O", "binary", str(elf), str(img)], verbose)


def build_board(name: str, verbose: bool, jobs: int | None = None) -> None:
    b = BOARDS[name]
    arch = b["arch"]
    out = BUILD / name
    out.mkdir(parents=True, exist_ok=True)
    cflags = cross_cflags(b)

    # Kernel
    srcs = [K / s for s in COMMON_SOURCES + ARCH_SOURCES[arch]] + \
           [K / "board" / f"board_{name}.c"]
    objs = compile_objs(srcs, out / "obj", cflags, verbose, jobs)
    ld = out / "kernel.ld"
    lddefs = {"KERNEL_BASE": b["kernel_base"]}
    if "ram_size" in b:
        lddefs["RAM_SIZE"] = b["ram_size"]
    preprocess_ld(K / "arch" / arch / "linker.ld.S", ld, lddefs, verbose)

    if arch == "riscv64":
        link(objs, ld, out / "kernel.elf", out / "kernel.bin", verbose)
        ksize = (out / "kernel.bin").stat().st_size
        print(f"[{name}] kernel.bin {ksize} bytes -> {out}")
        return

    link(objs, ld, out / "kernel.elf", out / "kernel8.img", verbose)
    if name == "rpi5":
        shutil.copyfile(out / "kernel8.img", out / "kernel_2712.img")

    # Chainloader (AArch64 boards)
    ccflags = cflags + [f"-DUART_BASE={b['uart_base']}ULL", f"-DUART_CLOCK={b['uart_clock']}"]
    csrcs = [ROOT / "chainloader" / "boot.S", ROOT / "chainloader" / "main.c",
             K / "lib" / "chainproto.c"]
    cobjs = compile_objs(csrcs, out / "chain_obj", ccflags, verbose, jobs)
    cld = out / "chainloader.ld"
    preprocess_ld(ROOT / "chainloader" / "linker.ld.S", cld, {"CHAIN_BASE": b["chain_base"]}, verbose)
    link(cobjs, cld, out / "chainloader.elf", out / "chainloader.img", verbose)

    ksize = (out / "kernel8.img").stat().st_size
    csize = (out / "chainloader.img").stat().st_size
    print(f"[{name}] kernel8.img {ksize} bytes, chainloader.img {csize} bytes -> {out}")


def host_exe(name: str) -> Path:
    return BUILD / "host" / (name + (".exe" if os.name == "nt" else ""))


def build_host_tests(verbose: bool, jobs: int | None = None) -> list[Path]:
    hdir = BUILD / "host"
    hdir.mkdir(parents=True, exist_ok=True)
    cmds, exes = [], []
    for test, srcs in HOST_TESTS.items():
        exe = host_exe(test)
        cmd = zig() + ["cc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                       "-Wno-unused-parameter", "-fno-sanitize=all",
                       f"-I{K / 'include'}", f"-I{ROOT / 'tests'}",
                       str(ROOT / "tests" / f"{test}.c")]
        cmd += [str(K / s) for s in srcs]
        cmd += ["-o", str(exe)]
        cmds.append(cmd)
        exes.append(exe)
    with ThreadPoolExecutor(max_workers=jobs or JOBS) as pool:
        list(pool.map(lambda c: run(c, verbose), cmds))
    return exes


def run_host_tests(verbose: bool, jobs: int | None = None) -> int:
    exes = build_host_tests(verbose, jobs)
    dtb = BUILD / "host" / "test.dtb"
    subprocess.run([sys.executable, str(ROOT / "tools" / "mkdtb.py"), "--preset", "rpi5-like",
                    "-o", str(dtb)], check=True)
    failed = 0
    for exe in exes:
        args = [str(exe)]
        if exe.stem == "test_fdt":
            args.append(str(dtb))
        r = subprocess.run(args)
        status = "PASS" if r.returncode == 0 else "FAIL"
        print(f"{status} {exe.stem}")
        failed += r.returncode != 0
    return failed


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("target", nargs="?", default="all", choices=["all", "host-tests", "clean"])
    ap.add_argument("--board", default="all", choices=["all"] + list(BOARDS))
    ap.add_argument("-j", "--jobs", type=int, default=JOBS, help="parallel compile jobs")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()
    if a.target == "clean":
        shutil.rmtree(BUILD, ignore_errors=True)
        return 0
    if a.target == "host-tests":
        return 1 if run_host_tests(a.verbose, a.jobs) else 0
    for name in (BOARDS if a.board == "all" else [a.board]):
        build_board(name, a.verbose, a.jobs)
    return 0


if __name__ == "__main__":
    sys.exit(main())
