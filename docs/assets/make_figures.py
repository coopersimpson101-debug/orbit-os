#!/usr/bin/env python3
"""Regenerate the README images in docs/assets/ from a real (emulated) boot.

  pip install ziglang unicorn
  python build.py
  python docs/assets/make_figures.py      # ~30 s; writes banner.svg and terminal.svg

Both images come from booting build/virt/kernel8.img on the Unicorn-based
emulator in tools/uniboot.py (see docs/testing.md for what it does and does
not model):

* terminal.svg - the UART transcript of that boot plus a scripted shell
                 session, exactly as the kernel printed it.
* banner.svg   - the right-hand motif is the kernel's own page-table tree, read
                 out of emulated RAM after boot by walking TTBR0_EL1/TTBR1_EL1:
                 one box per table, with a 512-slot strip showing which
                 descriptors are valid.

banner.png is rasterised from banner.svg with docs/assets/render_png.mjs
(`npm i @resvg/resvg-js`, then `node render_png.mjs banner.svg banner.png`).
"""
import sys
from pathlib import Path
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "tools"))
import uniboot  # noqa: E402  (needs `pip install unicorn`)

IMAGE = ROOT / "build" / "virt" / "kernel8.img"
ACCENT, ACCENT2 = "#22D3EE", "#A5F3FC"
BG, FG, MUTED, GRID = "#0d1117", "#e6edf3", "#8b949e", "#161b22"
MONO = "ui-monospace, SFMono-Regular, Menlo, Consolas, monospace"
ADDR = 0x0000FFFFFFFFF000
CMDS = ["spawn user", "ps", "spawn ufault"]


# --------------------------------------------------------------------------- boot
def boot():
    m = uniboot.Machine(IMAGE.read_bytes(), 20000)
    m.run(4.0)
    tables = {r: walk(m, m._cp(2, 0, 3, 0, r) & ADDR, 0) for r in (0, 1)}  # TTBR0_EL1, TTBR1_EL1
    for c in CMDS:
        m.rx.extend((c + "\r").encode())
        m.run(4.0)
    return m.text(), tables


def walk(m, addr, level):
    """-> dict(level, valid[512 bools], leaves, attr, children[list of subtrees])"""
    node = {"level": level, "valid": [], "leaves": 0, "attr": set(), "children": []}
    for i in range(512):
        d = m._rd64(addr + i * 8)
        node["valid"].append(bool(d & 1))
        if not d & 1:
            continue
        if level < 3 and d & 2:
            node["children"].append(walk(m, d & ADDR, level + 1))
        else:
            node["leaves"] += 1
            node["attr"].add((d >> 2) & 7)  # MAIR index: 1 = device nGnRE, 2 = normal WB
    return node


# --------------------------------------------------------------------------- banner
def label(n):
    if not n["leaves"]:
        return f"L{n['level']}  {sum(n['valid'])} table" + ("s" if sum(n["valid"]) > 1 else "")
    kind = "dev" if n["attr"] == {1} else "RAM"
    size = {1: "1G", 2: "2M", 3: "4K"}[n["level"]]
    return f"L{n['level']} {kind} {n['leaves']}×{size}"


def banner(tables):
    s = [f'<svg xmlns="http://www.w3.org/2000/svg" width="1280" height="400" viewBox="0 0 1280 400">',
         '<defs><pattern id="g" width="32" height="32" patternUnits="userSpaceOnUse">'
         f'<path d="M32 0H0V32" fill="none" stroke="{GRID}" stroke-width="1"/></pattern>'
         f'<linearGradient id="fade" x1="0" x2="1"><stop offset="0" stop-color="{BG}"/>'
         f'<stop offset="1" stop-color="{BG}" stop-opacity="0"/></linearGradient></defs>',
         f'<rect width="1280" height="400" fill="{BG}"/><rect width="1280" height="400" fill="url(#g)"/>',
         '<rect width="640" height="400" fill="url(#fade)"/>',
         f'<rect x="72" y="118" width="56" height="6" rx="3" fill="{ACCENT}"/>',
         f'<text x="68" y="210" font-family="{MONO}" font-size="92" font-weight="700" fill="{FG}">OrbitOS</text>',
         f'<text x="72" y="258" font-family="{MONO}" font-size="22" fill="{MUTED}">'
         'An AArch64 kernel for the Raspberry Pi 5</text>',
         f'<text x="72" y="306" font-family="{MONO}" font-size="17" fill="{MUTED}">'
         f'boot <tspan fill="{ACCENT}">·</tspan> MMU <tspan fill="{ACCENT}">·</tspan> GIC '
         f'<tspan fill="{ACCENT}">·</tspan> scheduler <tspan fill="{ACCENT}">·</tspan> EL0 '
         f'<tspan fill="{ACCENT}">·</tspan> shell</text>']
    bw, bh, col0, dx, rowh = 132, 40, 664, 148, 54
    rows, edges, nodes = [0], [], []

    def place(n, depth):
        kids = [place(c, depth + 1) for c in n["children"]]
        if kids:
            y = (kids[0][1] + kids[-1][1]) / 2 if n["leaves"] == 0 else kids[0][1]
        else:
            y = rows[0]
            rows[0] += 1
        if n["leaves"] and kids:  # a table with both leaves and a child: put the child on the same row
            y = kids[0][1]
        x = col0 + depth * dx
        nodes.append((x, y, n))
        for kx, ky in kids:
            edges.append((x + bw, y, kx, ky))
        return x, y

    group_rows = []
    for r in (0, 1):
        start = rows[0]
        place(tables[r], 0)
        group_rows.append((r, start, rows[0] - 1))
        rows[0] += 0.45
    y0 = 200 - (rows[0] - 0.45 - 1) * rowh / 2  # centre the tree vertically

    def Y(row):
        return y0 + row * rowh

    for lv in range(4):
        s.append(f'<text x="{col0 + lv * dx + bw / 2}" y="{Y(0) - 34}" text-anchor="middle" font-family="{MONO}" '
                 f'font-size="13" fill="{MUTED}">L{lv}</text>')
    for r, a, b in group_rows:
        s.append(f'<text x="{col0 - 12}" y="{(Y(a) + Y(b)) / 2 + 4}" text-anchor="end" font-family="{MONO}" '
                 f'font-size="13" fill="{ACCENT}">TTBR{r}</text>')
    for x1, r1, x2, r2 in edges:
        y1, y2 = Y(r1), Y(r2)
        xm = (x1 + x2) / 2
        s.append(f'<path d="M{x1} {y1}C{xm} {y1} {xm} {y2} {x2} {y2}" fill="none" stroke="{ACCENT}" '
                 'stroke-width="1.5" opacity="0.6"/>')
    for x, r, n in nodes:
        y = Y(r) - bh / 2
        s.append(f'<rect x="{x}" y="{y}" width="{bw}" height="{bh}" rx="6" fill="{GRID}" stroke="{ACCENT}" '
                 'stroke-opacity="0.7"/>')
        s.append(f'<text x="{x + 8}" y="{y + 16}" font-family="{MONO}" font-size="12" fill="{FG}">'
                 f'{escape(label(n))}</text>')
        # 512-slot occupancy strip: 128 cells x 4 slots each, lit if any of the 4 is valid
        sx, sy, cw = x + 8, y + 24, (bw - 16) / 128
        s.append(f'<rect x="{sx}" y="{sy}" width="{bw - 16}" height="8" fill="#0b2530"/>')
        v = n["valid"]
        i = 0
        while i < 128:
            if any(v[4 * i:4 * i + 4]):
                j = i
                while j < 128 and any(v[4 * j:4 * j + 4]):
                    j += 1
                s.append(f'<rect x="{sx + i * cw:.2f}" y="{sy}" width="{max((j - i) * cw, 1.2):.2f}" height="8" '
                         f'fill="{ACCENT}"/>')
                i = j
            else:
                i += 1
    n_tables = len(nodes)
    s.append(f'<text x="{col0}" y="{Y(rows[0] - 0.45 - 1) + 52}" font-family="{MONO}" font-size="14" fill="{MUTED}">'
             f'the {n_tables} live page tables after boot (emulated), 512 slots each</text>')
    s.append("</svg>")
    (OUT / "banner.svg").write_text("\n".join(s) + "\n", encoding="utf-8")
    return n_tables


# --------------------------------------------------------------------------- terminal
def terminal_svg(lines, title, caption, path, cols=96):
    wrapped = []
    for text, col in lines:
        text = text.rstrip()
        while len(text) > cols:
            wrapped.append((text[:cols], col))
            text = "    " + text[cols:]
        wrapped.append((text, col))
    lh, pad, top = 19, 22, 44
    w = int(cols * 8.45) + 2 * pad
    h = top + len(wrapped) * lh + 40
    s = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">',
         f'<rect width="{w}" height="{h}" rx="10" fill="{BG}" stroke="#30363d"/>',
         f'<path d="M0 10a10 10 0 0 1 10-10h{w - 20}a10 10 0 0 1 10 10v20H0z" fill="{GRID}"/>',
         '<circle cx="20" cy="15" r="6" fill="#ff5f57"/><circle cx="40" cy="15" r="6" fill="#febc2e"/>'
         '<circle cx="60" cy="15" r="6" fill="#28c840"/>',
         f'<text x="{w / 2}" y="20" text-anchor="middle" font-family="{MONO}" font-size="13" fill="{MUTED}">{escape(title)}</text>',
         f'<g font-family="{MONO}" font-size="14" xml:space="preserve">']
    for i, (text, col) in enumerate(wrapped):
        y = top + 14 + i * lh
        if text.startswith("orbit> "):
            s.append(f'<text x="{pad}" y="{y}" fill="{FG}"><tspan fill="{ACCENT}">orbit&gt;</tspan>'
                     f' {escape(text[7:])}</text>')
        elif text.startswith("$ "):
            s.append(f'<text x="{pad}" y="{y}" fill="{FG}"><tspan fill="{ACCENT}">$</tspan> {escape(text[2:])}</text>')
        else:
            s.append(f'<text x="{pad}" y="{y}" fill="{col}">{escape(text)}</text>')
    s.append("</g>")
    s.append(f'<text x="{pad}" y="{h - 15}" font-family="{MONO}" font-size="12" fill="{MUTED}">{escape(caption)}</text>')
    s.append("</svg>")
    path.write_text("\n".join(s) + "\n", encoding="utf-8")


def colour(line):
    if line.startswith("***") or "-> killing" in line or line.startswith("    ESR") or line.startswith("    ELR"):
        return "#ff7b72"
    if line.startswith("[user]"):
        return ACCENT2
    if line.startswith("[") or line.startswith("OrbitOS"):
        return MUTED
    return FG


def terminal(text):
    cmd = "$ python tools/uniboot.py build/virt/kernel8.img " + " ".join(f'--cmd "{c}"' for c in CMDS)
    lines = [(cmd, FG)]
    for ln in text.splitlines():
        if ln.strip() or (lines and lines[-1][0].strip()):
            lines.append((ln, colour(ln)))
    terminal_svg(lines, "OrbitOS - UART console, qemu-virt image on the Unicorn emulator",
                 "Real UART output of build/virt/kernel8.img, emulated (tools/uniboot.py), not hardware. "
                 "Regenerate: docs/assets/make_figures.py", OUT / "terminal.svg", cols=100)


def main():
    if not IMAGE.exists():
        sys.exit("build first: python build.py")
    text, tables = boot()
    n = banner(tables)
    terminal(text)
    print(f"wrote banner.svg ({n} page tables), terminal.svg")


if __name__ == "__main__":
    main()
