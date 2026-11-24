#!/usr/bin/env python3
"""OrbitOS build driver.

Uses the Zig toolchain (``pip install ziglang``) as a hermetic cross
compiler: ``zig cc`` (clang) for C/asm, ``zig ld.lld`` to link and
``zig objcopy`` to produce raw images. No system cross-gcc needed.

    python build.py                    # kernel + chainloader for all boards
    python build.py --board virt       # one board
    python build.py host-tests         # compile + run host unit tests
    python build.py clean

Outputs (per board) in build/<board>/:
    kernel.elf, kernel8.img            (rpi5 also: kernel_2712.img)
    chainloader.elf, chainloader.img
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"
K = ROOT / "kernel"

BOARDS = {
    "virt": {
        "kernel_base": "0x40080000",
        "chain_base": "0x42000000",
        "uart_base": "0x09000000",
        "uart_clock": "24000000",
        "mcpu": "cortex_a72",
    },
    "rpi5": {
        "kernel_base": "0x80000",
        "chain_base": "0x2000000",
        "uart_base": "0x107d001000",
        "uart_clock": "0",
        "mcpu": "cortex_a76",
    },
}

TEXT_OFFSET = "0x80000"

KERNEL_SOURCES = [
    "arch/boot.S",
    "arch/vectors.S",
    "arch/switch.S",
    "core/main.c",
    "core/console.c",
    "core/platform.c",
    "core/trap.c",
    "core/syscall.c",
    "core/demos.c",
    "drivers/pl011.c",
    "drivers/gic.c",
    "drivers/timer.c",
    "fdt/fdt.c",
    "lib/kstring.c",
    "lib/kprintf.c",
    "lib/esr.c",
    "lib/chainproto.c",
    "mm/buddy.c",
    "mm/kheap.c",
    "mm/mm.c",
    "mm/mmu.c",
    "sched/runqueue.c",
    "sched/sched.c",
    "sched/sync.c",
    "shell/shell.c",
    "shell/shell_parse.c",
    "user/user_prog.c",
]

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
}


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


def cross_cflags(board: dict) -> list[str]:
    return [
        "-target", "aarch64-freestanding-none",
        f"-mcpu={board['mcpu']}",
        "-std=gnu11", "-O2", "-g",
        "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
        "-fno-sanitize=all", "-fno-pic", "-fno-pie",
        "-fno-omit-frame-pointer", "-fno-unwind-tables",
        "-fno-asynchronous-unwind-tables",
        "-mgeneral-regs-only", "-mstrict-align",
        "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
        f"-DTEXT_OFFSET={TEXT_OFFSET}",
        f"-I{K / 'include'}",
    ]


def preprocess_ld(src: Path, out: Path, defines: dict[str, str], verbose: bool) -> None:
    cmd = zig() + ["cc", "-E", "-P", "-undef", "-x", "c"]
    cmd += [f"-D{k}={v}" for k, v in defines.items()]
    cmd += [str(src), "-o", str(out)]
    run(cmd, verbose)


def compile_objs(sources: list[Path], objdir: Path, cflags: list[str], verbose: bool) -> list[Path]:
    objs = []
    for src in sources:
        rel = src.relative_to(ROOT)
        obj = objdir / rel.with_suffix(".o")
        obj.parent.mkdir(parents=True, exist_ok=True)
        run(zig() + ["cc"] + cflags + ["-c", str(src), "-o", str(obj)], verbose)
        objs.append(obj)
    return objs


def link(objs: list[Path], ld: Path, elf: Path, img: Path, verbose: bool) -> None:
    run(zig() + ["ld.lld", "-T", str(ld), "-nostdlib", "-static",
                 "--build-id=none", "-z", "max-page-size=4096",
                 "-o", str(elf)] + [str(o) for o in objs], verbose)
    run(zig() + ["objcopy", "-O", "binary", str(elf), str(img)], verbose)


def build_board(name: str, verbose: bool) -> None:
    b = BOARDS[name]
    out = BUILD / name
    out.mkdir(parents=True, exist_ok=True)
    cflags = cross_cflags(b)

    # Kernel
    srcs = [K / s for s in KERNEL_SOURCES] + [K / "board" / f"board_{name}.c"]
    objs = compile_objs(srcs, out / "obj", cflags, verbose)
    ld = out / "kernel.ld"
    preprocess_ld(K / "arch" / "linker.ld.S", ld, {"KERNEL_BASE": b["kernel_base"]}, verbose)
    link(objs, ld, out / "kernel.elf", out / "kernel8.img", verbose)
    if name == "rpi5":
        shutil.copyfile(out / "kernel8.img", out / "kernel_2712.img")

    # Chainloader
    ccflags = cflags + [f"-DUART_BASE={b['uart_base']}ULL", f"-DUART_CLOCK={b['uart_clock']}"]
    csrcs = [ROOT / "chainloader" / "boot.S", ROOT / "chainloader" / "main.c",
             K / "lib" / "chainproto.c"]
    cobjs = compile_objs(csrcs, out / "chain_obj", ccflags, verbose)
    cld = out / "chainloader.ld"
    preprocess_ld(ROOT / "chainloader" / "linker.ld.S", cld, {"CHAIN_BASE": b["chain_base"]}, verbose)
    link(cobjs, cld, out / "chainloader.elf", out / "chainloader.img", verbose)

    ksize = (out / "kernel8.img").stat().st_size
    csize = (out / "chainloader.img").stat().st_size
    print(f"[{name}] kernel8.img {ksize} bytes, chainloader.img {csize} bytes -> {out}")


def host_exe(name: str) -> Path:
    return BUILD / "host" / (name + (".exe" if os.name == "nt" else ""))


def build_host_tests(verbose: bool) -> list[Path]:
    hdir = BUILD / "host"
    hdir.mkdir(parents=True, exist_ok=True)
    exes = []
    for test, srcs in HOST_TESTS.items():
        exe = host_exe(test)
        cmd = zig() + ["cc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                       "-Wno-unused-parameter", "-fno-sanitize=all",
                       f"-I{K / 'include'}", f"-I{ROOT / 'tests'}",
                       str(ROOT / "tests" / f"{test}.c")]
        cmd += [str(K / s) for s in srcs]
        cmd += ["-o", str(exe)]
        run(cmd, verbose)
        exes.append(exe)
    return exes


def run_host_tests(verbose: bool) -> int:
    exes = build_host_tests(verbose)
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
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()
    if a.target == "clean":
        shutil.rmtree(BUILD, ignore_errors=True)
        return 0
    if a.target == "host-tests":
        return 1 if run_host_tests(a.verbose) else 0
    for name in (BOARDS if a.board == "all" else [a.board]):
        build_board(name, a.verbose)
    return 0


if __name__ == "__main__":
    sys.exit(main())
