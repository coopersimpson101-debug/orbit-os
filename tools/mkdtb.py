#!/usr/bin/env python3
"""Tiny Flattened Device Tree (DTB v17) writer - no dtc required.

Used to generate fixtures for the host FDT parser tests and for local
experiments. Two presets mirror the shapes of real trees:

  virt       QEMU -M virt,gic-version=2 (2/2 cells, flat)
  rpi5-like  BCM2712-style: 2/1 root cells, a /soc bus with 1-cell
             children and a "ranges" window to 0x10_7c00_0000, aliases,
             stdout-path "serial10:115200n8", a disabled PL011, an
             untranslatable PL011 behind a PCIe-like bus, and a
             /memreserve/ entry.

    python tools/mkdtb.py --preset rpi5-like -o build/host/test.dtb
"""
from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass, field

FDT_MAGIC = 0xD00DFEED
FDT_BEGIN_NODE, FDT_END_NODE, FDT_PROP, FDT_END = 1, 2, 3, 9


def u32(*vals: int) -> bytes:
    return b"".join(struct.pack(">I", v & 0xFFFFFFFF) for v in vals)


def strs(*vals: str) -> bytes:
    return b"".join(v.encode() + b"\0" for v in vals)


@dataclass
class Node:
    name: str
    props: list[tuple[str, bytes]] = field(default_factory=list)
    children: list["Node"] = field(default_factory=list)

    def prop(self, name: str, value: bytes) -> "Node":
        self.props.append((name, value))
        return self

    def child(self, node: "Node") -> "Node":
        self.children.append(node)
        return node


def build_dtb(root: Node, memreserve: list[tuple[int, int]] | None = None,
              boot_cpuid: int = 0) -> bytes:
    strings = bytearray()
    str_off: dict[str, int] = {}
    struct_blk = bytearray()

    def pad4(b: bytearray) -> None:
        while len(b) % 4:
            b.append(0)

    def name_off(n: str) -> int:
        if n not in str_off:
            str_off[n] = len(strings)
            strings.extend(n.encode() + b"\0")
        return str_off[n]

    def emit(node: Node) -> None:
        struct_blk.extend(u32(FDT_BEGIN_NODE))
        struct_blk.extend(node.name.encode() + b"\0")
        pad4(struct_blk)
        for pname, val in node.props:
            struct_blk.extend(u32(FDT_PROP, len(val), name_off(pname)))
            struct_blk.extend(val)
            pad4(struct_blk)
        for c in node.children:
            emit(c)
        struct_blk.extend(u32(FDT_END_NODE))

    emit(root)
    struct_blk.extend(u32(FDT_END))

    rsv = bytearray()
    for addr, size in memreserve or []:
        rsv.extend(struct.pack(">QQ", addr, size))
    rsv.extend(struct.pack(">QQ", 0, 0))

    hdr_size = 40
    off_rsv = (hdr_size + 7) & ~7
    off_struct = off_rsv + len(rsv)
    off_strings = off_struct + len(struct_blk)
    total = off_strings + len(strings)
    total = (total + 7) & ~7
    hdr = u32(FDT_MAGIC, total, off_struct, off_strings, off_rsv, 17, 16,
              boot_cpuid, len(strings), len(struct_blk))
    blob = bytearray(hdr)
    blob.extend(b"\0" * (off_rsv - len(blob)))
    blob.extend(rsv)
    blob.extend(struct_blk)
    blob.extend(strings)
    blob.extend(b"\0" * (total - len(blob)))
    return bytes(blob)


def preset_virt() -> tuple[Node, list]:
    root = Node("")
    root.prop("#address-cells", u32(2)).prop("#size-cells", u32(2))
    root.prop("compatible", strs("linux,dummy-virt"))
    root.child(Node("chosen")).prop("stdout-path", strs("/pl011@9000000"))
    root.child(Node("memory@40000000")).prop("device_type", strs("memory")).prop(
        "reg", u32(0, 0x40000000, 0, 0x20000000))
    root.child(Node("psci")).prop("compatible", strs("arm,psci-1.0", "arm,psci-0.2")).prop(
        "method", strs("smc"))
    root.child(Node("intc@8000000")).prop("compatible", strs("arm,cortex-a15-gic")).prop(
        "reg", u32(0, 0x08000000, 0, 0x10000, 0, 0x08010000, 0, 0x10000))
    root.child(Node("pl011@9000000")).prop("compatible", strs("arm,pl011", "arm,primecell")).prop(
        "reg", u32(0, 0x09000000, 0, 0x1000)).prop("interrupts", u32(0, 1, 4))
    root.child(Node("timer")).prop("compatible", strs("arm,armv8-timer")).prop(
        "interrupts", u32(1, 13, 0xF04, 1, 14, 0xF04, 1, 11, 0xF04, 1, 10, 0xF04))
    return root, []


def preset_rpi5_like() -> tuple[Node, list]:
    root = Node("")
    root.prop("#address-cells", u32(2)).prop("#size-cells", u32(1))
    root.prop("compatible", strs("raspberrypi,5-model-b", "brcm,bcm2712"))
    root.child(Node("aliases")).prop("serial10", strs("/soc@107c000000/serial@7d001000")).prop(
        "serial0", strs("/axi/pcie@120000/rp1/serial@30000"))
    root.child(Node("chosen")).prop("stdout-path", strs("serial10:115200n8"))
    root.child(Node("memory@0")).prop("device_type", strs("memory")).prop(
        "reg", u32(0, 0, 0x3F800000))
    root.child(Node("psci")).prop("compatible", strs("arm,psci-1.0")).prop("method", strs("smc"))
    root.child(Node("timer")).prop("compatible", strs("arm,armv8-timer")).prop(
        "interrupts", u32(1, 13, 0xF08, 1, 14, 0xF08, 1, 11, 0xF08, 1, 10, 0xF08))
    soc = root.child(Node("soc@107c000000"))
    soc.prop("compatible", strs("simple-bus")).prop("#address-cells", u32(1)).prop(
        "#size-cells", u32(1)).prop("ranges", u32(0x7C000000, 0x10, 0x7C000000, 0x04000000))
    soc.child(Node("serial@7d001000")).prop("compatible", strs("arm,pl011", "arm,primecell")).prop(
        "reg", u32(0x7D001000, 0x200)).prop("interrupts", u32(0, 121, 4)).prop(
        "status", strs("okay"))
    soc.child(Node("serial@7d002000")).prop("compatible", strs("arm,pl011")).prop(
        "reg", u32(0x7D002000, 0x200)).prop("status", strs("disabled"))
    soc.child(Node("interrupt-controller@7fff9000")).prop(
        "compatible", strs("arm,gic-400")).prop(
        "reg", u32(0x7FFF9000, 0x1000, 0x7FFFA000, 0x2000))
    axi = root.child(Node("axi"))
    axi.prop("#address-cells", u32(2)).prop("#size-cells", u32(2)).prop("ranges", b"")
    pcie = axi.child(Node("pcie@120000"))
    pcie.prop("reg", u32(0x10, 0x00120000, 0, 0x9310)).prop("#address-cells", u32(3)).prop(
        "#size-cells", u32(2))
    rp1 = pcie.child(Node("rp1"))  # no "ranges": not CPU-addressable for our parser
    rp1.prop("#address-cells", u32(2)).prop("#size-cells", u32(2))
    rp1.child(Node("serial@30000")).prop("compatible", strs("arm,pl011")).prop(
        "reg", u32(0xC0, 0x40030000, 0, 0x100))
    return root, [(0x0, 0x1000)]


PRESETS = {"virt": preset_virt, "rpi5-like": preset_rpi5_like}


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--preset", choices=sorted(PRESETS), default="rpi5-like")
    ap.add_argument("-o", "--output", required=True)
    a = ap.parse_args()
    root, rsv = PRESETS[a.preset]()
    blob = build_dtb(root, rsv)
    with open(a.output, "wb") as f:
        f.write(blob)
    print(f"wrote {a.output} ({len(blob)} bytes, preset {a.preset})")


if __name__ == "__main__":
    main()
