"""pytest entry point.

* compiles the portable kernel modules natively with zig cc and runs the
  C unit tests (buddy, kheap, run-queue, FDT, shell parser, printf, ...)
* cross-builds kernel + chainloader for both boards and checks the images
* optionally boots the qemu-virt image under Unicorn (tools/uniboot.py)
  when the `unicorn` package is installed; QEMU itself runs in CI via
  tools/qemu_test.py.
"""
from __future__ import annotations

import importlib.util
import os
import struct
import subprocess
import sys
import zlib
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tools"))

import build  # noqa: E402
import mkdtb  # noqa: E402
import send_kernel  # noqa: E402


@pytest.fixture(scope="session")
def host_bins():
    build.build_host_tests(verbose=False)
    return {name: build.host_exe(name) for name in build.HOST_TESTS}


@pytest.fixture(scope="session")
def rpi5_dtb(tmp_path_factory):
    root, rsv = mkdtb.preset_rpi5_like()
    p = tmp_path_factory.mktemp("dtb") / "rpi5-like.dtb"
    p.write_bytes(mkdtb.build_dtb(root, rsv))
    return p


@pytest.fixture(scope="session")
def images():
    for board in build.BOARDS:
        build.build_board(board, verbose=False)
    return build.BUILD


def run_bin(path: Path, *args: str) -> subprocess.CompletedProcess:
    r = subprocess.run([str(path), *args], capture_output=True, text=True, timeout=120)
    print(r.stdout)
    print(r.stderr, file=sys.stderr)
    return r


@pytest.mark.parametrize("name", [n for n in build.HOST_TESTS
                                  if n not in ("test_fdt", "test_chainproto")])
def test_c_unit(host_bins, name):
    r = run_bin(host_bins[name])
    assert r.returncode == 0, r.stderr
    assert "0 failures" in r.stdout


def test_c_fdt(host_bins, rpi5_dtb):
    r = run_bin(host_bins["test_fdt"], str(rpi5_dtb))
    assert r.returncode == 0, r.stderr


def test_c_chainproto_against_python_sender(host_bins, tmp_path):
    payload = os.urandom(40000)
    frame = tmp_path / "frame.bin"
    frame.write_bytes(send_kernel.build_header(payload) + payload)
    r = run_bin(host_bins["test_chainproto"], str(frame))
    assert r.returncode == 0, r.stderr


def test_send_kernel_header():
    hdr = send_kernel.build_header(b"123456789")
    assert hdr[:4] == b"OBK1"
    assert struct.unpack("<II", hdr[4:]) == (9, 0xCBF43926)


def test_mkdtb_header_is_valid():
    root, rsv = mkdtb.preset_virt()
    blob = mkdtb.build_dtb(root, rsv)
    magic, total, off_struct, off_strings, off_rsv, version = struct.unpack(">6I", blob[:24])
    assert magic == 0xD00DFEED and total == len(blob) and version == 17
    assert off_rsv < off_struct < off_strings <= total
    assert blob[off_struct:off_struct + 4] == b"\0\0\0\1"  # FDT_BEGIN_NODE


@pytest.mark.parametrize("board", list(build.BOARDS))
def test_kernel_image(images, board):
    out = images / board
    img = (out / "kernel8.img").read_bytes()
    # Linux arm64 Image header: magic "ARM\x64" at 56, text_offset at 8
    assert img[56:60] == b"ARM\x64"
    text_offset, image_size = struct.unpack("<QQ", img[8:24])
    assert text_offset == 0x80000
    assert image_size >= len(img)  # includes BSS
    assert (out / "kernel.elf").stat().st_size > len(img) // 2
    chain = (out / "chainloader.img").read_bytes()
    assert chain[56:60] == b"ARM\x64" and len(chain) < 16 * 1024
    if board == "rpi5":
        assert (out / "kernel_2712.img").read_bytes() == img


@pytest.mark.parametrize("board", list(build.BOARDS))
def test_kernel_sections_page_aligned(images, board):
    elftools = pytest.importorskip("elftools.elf.elffile")
    with open(images / board / "kernel.elf", "rb") as f:
        elf = elftools.ELFFile(f)
        syms = elf.get_section_by_name(".symtab")

        def addr(name):
            return syms.get_symbol_by_name(name)[0]["st_value"]

        base = int(build.BOARDS[board]["kernel_base"], 16)
        assert addr("_start") == base
        for s in ("__user_text_start", "__user_text_end", "__text_end", "__rodata_end",
                  "__user_stack_start", "__user_stack_end", "_end"):
            assert addr(s) % 4096 == 0, s
        assert addr("__user_text_start") <= addr("user_entry") < addr("__user_text_end")
        assert addr("vectors") % 2048 == 0


HAVE_UNICORN = importlib.util.find_spec("unicorn") is not None


@pytest.mark.skipif(not HAVE_UNICORN, reason="pip install unicorn to run the emulated boot")
@pytest.mark.parametrize("mode", ["direct", "chainload"])
def test_uniboot_virt(images, mode):
    virt = images / "virt"
    cmd = [sys.executable, str(ROOT / "tools" / "uniboot.py")]
    if mode == "direct":
        cmd += [str(virt / "kernel8.img")]
        cmds = ["spawn user", "spawn ufault", "spawn fault", "spawn mutex", "spawn pc",
                "spawn spin", "ps", "mem", "poweroff"]
        expect = ["higher-half alias self-test: OK", "hello from EL0",
                  "exited with code 42", "killing user task", "killing kernel thread",
                  "shared counter = 2000 (expected 2000) OK",
                  "consumer sum = 204 (expected 204) OK", "preempted: yes",
                  "powering off via PSCI SYSTEM_OFF"]
    else:
        cmd += [str(virt / "chainloader.img"), "--chainload", str(virt / "kernel8.img")]
        cmds = ["spawn user", "poweroff"]
        expect = ["CRC OK, jumping", "OrbitOS shell", "hello from EL0",
                  "powering off via PSCI SYSTEM_OFF"]
    for c in cmds:
        cmd += ["--cmd", c]
    for e in expect:
        cmd += ["--expect", e]
    r = subprocess.run(cmd + ["--seconds", "2.5"], capture_output=True, text=True, timeout=600)
    print(r.stdout)
    assert r.returncode == 0, r.stdout[-3000:] + r.stderr[-2000:]
    assert "powered_off=True" in r.stdout
