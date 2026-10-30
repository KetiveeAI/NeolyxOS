#!/usr/bin/env python3
"""
NeolyxOS - system icon pack builder

Reads resources/icons/pack/iconpack.manifest, renders every icon from its
NeolyxOS source art into an NXIB bitmap (System/Desktop/Icons/<name>.nxi)
and regenerates the two files that link the pack into the desktop image:

    desktop/shell/nxicon_pack.S             .incbin of every NXIB file
    desktop/lib/nxicons/nxicon_pack_index.h  name -> symbol table

The desktop build itself never runs Python; the generated files are
committed. Re-run this after editing the manifest or the source art.

Usage:  python3 tools/build_icon_pack.py [--check]
        --check  rebuild in memory and fail if any committed output differs

Requires: Pillow, cairosvg (pip install pillow cairosvg). Icons marked
render=browser (Figma glass art with SVG filters, which cairosvg drops)
also need Node.js with Playwright and a Chromium build.

Copyright (c) 2025-2026 KetiveeAI
"""

import io
import os
import re
import struct
import sys

try:
    import cairosvg
    from PIL import Image, ImageDraw
except ImportError as exc:  # pragma: no cover - environment problem
    sys.exit(f"build_icon_pack: missing dependency ({exc}); pip install pillow cairosvg")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(ROOT, "resources/icons/pack/iconpack.manifest")
OUT_DIR = os.path.join(ROOT, "System/Desktop/Icons")
ASM_OUT = os.path.join(ROOT, "desktop/shell/nxicon_pack.S")
INDEX_OUT = os.path.join(ROOT, "desktop/lib/nxicons/nxicon_pack_index.h")

NXIB_MAGIC = b"NXIB"
NXIB_VERSION = 1
SUPERSAMPLE = 4
TILE_RADIUS = 0.225          # matches the dock shadow radius (sz * 0.225)
NAME_RE = re.compile(r"^[a-z0-9][a-z0-9-]{0,47}$")
MODES = ("art", "tile", "tile-w")


class PackError(Exception):
    pass


# ============ Manifest ============

def parse_manifest(path):
    entries, seen = [], set()
    with open(path, encoding="utf-8") as f:
        for lineno, raw in enumerate(f, 1):
            # '#' starts a comment only at line start or after whitespace,
            # so tile colors (tile=#RRGGBB) survive.
            line = re.sub(r"(^|\s)#.*$", "", raw).strip()
            if not line:
                continue
            # Sources may contain spaces ("Dir with item.svg"): the source
            # runs from the 4th field up to the first key=value field.
            fields = line.split()
            if len(fields) < 4:
                raise PackError(f"{path}:{lineno}: expected 'name size mode source'")
            name, size_s, mode = fields[0], fields[1], fields[2]
            rest = fields[3:]
            src_parts, opts = [], {}
            for tok in rest:
                if src_parts and re.match(r"^[a-z]+=", tok):
                    k, v = tok.split("=", 1)
                    opts[k] = v
                elif opts:
                    raise PackError(f"{path}:{lineno}: text after options: {tok}")
                else:
                    src_parts.append(tok)
            source = " ".join(src_parts)
            if not NAME_RE.match(name):
                raise PackError(f"{path}:{lineno}: bad icon name '{name}'")
            if name in seen:
                raise PackError(f"{path}:{lineno}: duplicate icon '{name}'")
            seen.add(name)
            if not size_s.isdigit() or not 16 <= int(size_s) <= 256:
                raise PackError(f"{path}:{lineno}: size must be 16..256")
            if mode not in MODES:
                raise PackError(f"{path}:{lineno}: mode must be one of {MODES}")
            for k in opts:
                if k not in ("tile", "scale", "color", "render"):
                    raise PackError(f"{path}:{lineno}: unknown option '{k}'")
            if opts.get("render", "svg") not in ("svg", "browser"):
                raise PackError(f"{path}:{lineno}: render must be svg or browser")
            src_abs = os.path.normpath(os.path.join(ROOT, source))
            if not src_abs.startswith(ROOT + os.sep) or not os.path.isfile(src_abs):
                raise PackError(f"{path}:{lineno}: source not found in repo: {source}")
            entries.append(dict(name=name, size=int(size_s), mode=mode,
                                source=source, src_abs=src_abs, opts=opts, line=lineno))
    if not entries:
        raise PackError(f"{path}: no icons")
    return entries


def parse_color(text):
    m = re.match(r"^#([0-9A-Fa-f]{6})$", text)
    if not m:
        raise PackError(f"bad color '{text}' (want #RRGGBB)")
    v = int(m.group(1), 16)
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255)


# ============ Source loading ============

def vector_nxi_to_svg(path):
    """NeolyxOS text vector .nxi (INI-like, SVG path data) -> SVG string."""
    sections, cur = {}, None
    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("[") and line.endswith("]"):
                cur = sections.setdefault(line[1:-1], {})
                continue
            if cur is not None and "=" in line:
                k, v = line.split("=", 1)
                cur[k.strip()] = v.strip()
    vb = sections.get("nxi", {}).get("viewbox", "0 0 24 24")
    colors = sections.get("colors", {})
    style = sections.get("style", {})
    paths = sections.get("paths", {})
    if not paths:
        raise PackError(f"{path}: vector NXI has no [paths]")

    def resolve(ref):
        seen = 0
        while ref in colors and seen < 4:
            ref, seen = colors[ref], seen + 1
        return ref

    try:
        vbx, vby, vbw, vbh = (float(v) for v in vb.split())
    except ValueError:
        raise PackError(f"{path}: bad viewbox '{vb}'")

    def is_frame_rect(d):
        # Design-tool exports sometimes keep the artboard background as a
        # rect covering the whole viewbox; it would paint the glyph solid.
        nums = [float(n) for n in re.findall(r"-?[0-9.]+", d)]
        return (len(nums) == 8 and min(nums[0::2]) <= vbx and min(nums[1::2]) <= vby and
                max(nums[0::2]) >= vbx + vbw and max(nums[1::2]) >= vby + vbh)

    def order(k):
        tail = k.rsplit("_", 1)[-1]
        return int(tail) if tail.isdigit() else 0

    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{vb}">']
    for key in sorted(paths, key=order):
        d = paths[key]
        if key.startswith("rect_") and is_frame_rect(d):
            print(f"build_icon_pack: note: {os.path.relpath(path, ROOT)}: skipping "
                  f"artboard background {key}", file=sys.stderr)
            continue
        if not re.match(r"^[MmLlHhVvCcSsQqTtAaZz0-9eE.,\s+-]+$", d):
            raise PackError(f"{path}: {key} has invalid path data")
        fill = resolve(style.get(f"{key}.fill", "primary"))
        if not re.match(r"^(#[0-9A-Fa-f]{3,8}|[a-z]+)$", fill):
            fill = "#FFFFFF"
        out.append(f'<path d="{d}" fill="{fill}"/>')
    out.append("</svg>")
    return "\n".join(out)


BROWSER_JS = r"""
const fs = require('fs');
const { chromium } = require('playwright');
(async () => {
  const [svgPath, outPath, px] = [process.argv[2], process.argv[3], parseInt(process.argv[4], 10)];
  const svg = fs.readFileSync(svgPath, 'utf8');
  const b64 = Buffer.from(svg).toString('base64');
  const browser = await chromium.launch();
  try {
    const page = await browser.newPage({ viewport: { width: px, height: px } });
    await page.setContent('<html><body style="margin:0;background:transparent">' +
      '<img style="display:block;width:' + px + 'px;height:' + px + 'px;object-fit:contain" ' +
      'src="data:image/svg+xml;base64,' + b64 + '"></body></html>');
    await page.waitForFunction(() => document.images[0] && document.images[0].complete);
    await page.screenshot({ path: outPath, omitBackground: true });
  } finally {
    await browser.close();
  }
})().catch(e => { console.error(e && e.message ? e.message : e); process.exit(1); });
"""


def render_svg_browser(entry, px):
    """Rasterize an SVG in headless Chromium (full filter support).
    The SVG is loaded as an <img>, so its scripts never run."""
    import subprocess
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        js = os.path.join(tmp, "render.js")
        out = os.path.join(tmp, "out.png")
        with open(js, "w", encoding="utf-8") as f:
            f.write(BROWSER_JS)
        try:
            res = subprocess.run(["node", js, entry["src_abs"], out, str(px)],
                                 capture_output=True, text=True, timeout=120)
        except (OSError, subprocess.TimeoutExpired) as exc:
            raise PackError(f"{entry['source']}: browser render failed: {exc}")
        if res.returncode != 0 or not os.path.isfile(out):
            raise PackError(f"{entry['source']}: browser render failed "
                            f"(needs node + playwright): {res.stderr.strip()[:300]}")
        with Image.open(out) as im:
            return im.convert("RGBA")


def render_source(entry, px):
    """Render the source to a straight-alpha RGBA image fitted in px x px."""
    path = entry["src_abs"]
    with open(path, "rb") as f:
        head = f.read(4)
    if head == NXIB_MAGIC:
        img = load_nxib(path)
    elif entry["opts"].get("render") == "browser":
        if not path.endswith(".svg"):
            raise PackError(f"{entry['source']}: render=browser needs an .svg source")
        img = render_svg_browser(entry, px * 2)
    else:
        if path.endswith(".nxi"):
            svg = vector_nxi_to_svg(path).encode()
        elif path.endswith(".svg"):
            with open(path, "rb") as f:
                svg = f.read()
        else:
            raise PackError(f"{entry['source']}: unsupported source type")
        try:
            png = cairosvg.svg2png(bytestring=svg, output_width=px * 2, unsafe=False)
        except Exception as exc:
            raise PackError(f"{entry['source']}: SVG render failed: {exc}")
        img = Image.open(io.BytesIO(png)).convert("RGBA")
    bbox = img.getbbox()
    if not bbox:
        raise PackError(f"{entry['source']}: renders empty")
    img = img.crop(bbox)
    w, h = img.size
    k = px / max(w, h)
    img = img.resize((max(1, round(w * k)), max(1, round(h * k))), Image.LANCZOS)
    canvas = Image.new("RGBA", (px, px), (0, 0, 0, 0))
    canvas.alpha_composite(img, ((px - img.size[0]) // 2, (px - img.size[1]) // 2))
    return canvas


def load_nxib(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 10:
        raise PackError(f"{path}: truncated NXIB")
    magic, ver, w, h, flags = struct.unpack("<4sBHHB", data[:10])
    if magic != NXIB_MAGIC or ver != NXIB_VERSION or flags != 0 or not w or not h:
        raise PackError(f"{path}: unsupported NXIB header")
    if len(data) != 10 + w * h * 4:
        raise PackError(f"{path}: NXIB size mismatch")
    return Image.frombytes("RGBA", (w, h), data[10:])


# ============ Composition ============

def make_tile(px, top, bottom):
    """NeolyxOS app tile: full-bleed rounded square, vertical gradient,
    soft top highlight and a hairline edge (rendered supersampled)."""
    grad = Image.new("RGBA", (1, px))
    for y in range(px):
        t = y / max(1, px - 1)
        grad.putpixel((0, y), tuple(round(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + (255,))
    grad = grad.resize((px, px))
    hi = Image.new("RGBA", (px, px), (255, 255, 255, 0))
    hd = ImageDraw.Draw(hi)
    for y in range(int(px * 0.5)):
        a = round(38 * (1 - y / (px * 0.5)))
        hd.line([(0, y), (px, y)], fill=(255, 255, 255, a))
    grad.alpha_composite(hi)
    mask = Image.new("L", (px, px), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, px - 1, px - 1], radius=px * TILE_RADIUS, fill=255)
    edge = Image.new("RGBA", (px, px), (0, 0, 0, 0))
    ImageDraw.Draw(edge).rounded_rectangle([0, 0, px - 1, px - 1], radius=px * TILE_RADIUS,
                                           outline=(0, 0, 0, 46), width=max(1, px // 128))
    grad.alpha_composite(edge)
    tile = Image.new("RGBA", (px, px), (0, 0, 0, 0))
    tile.paste(grad, (0, 0), mask)
    return tile


def build_icon(entry):
    size = entry["size"]
    px = size * SUPERSAMPLE
    mode, opts = entry["mode"], entry["opts"]
    if mode == "art":
        img = render_source(entry, px)
    else:
        if "tile" not in opts:
            raise PackError(f"line {entry['line']}: mode {mode} needs tile=#TOP:#BOTTOM")
        parts = opts["tile"].split(":")
        if len(parts) != 2:
            raise PackError(f"line {entry['line']}: tile wants #TOP:#BOTTOM")
        top, bottom = parse_color(parts[0]), parse_color(parts[1])
        try:
            scale = float(opts.get("scale", "0.62"))
        except ValueError:
            raise PackError(f"line {entry['line']}: bad scale")
        if not 0.2 <= scale <= 1.0:
            raise PackError(f"line {entry['line']}: scale must be 0.2..1.0")
        img = make_tile(px, top, bottom)
        gpx = max(8, round(px * scale))
        glyph = render_source(entry, gpx)
        if mode == "tile-w":
            rgb = parse_color(opts.get("color", "#FFFFFF"))
            alpha = glyph.getchannel("A")
            glyph = Image.new("RGBA", glyph.size, rgb + (0,))
            glyph.putalpha(alpha)
        # Soft drop under the glyph so light glyphs read on light tiles.
        shadow = Image.new("RGBA", glyph.size, (0, 0, 0, 0))
        shadow.putalpha(glyph.getchannel("A").point(lambda a: a * 40 // 255))
        off = (px - gpx) // 2
        img.alpha_composite(shadow, (off, off + max(1, px // 96)))
        img.alpha_composite(glyph, (off, off))
    return img.resize((size, size), Image.LANCZOS)


def encode_nxib(img):
    w, h = img.size
    return struct.pack("<4sBHHB", NXIB_MAGIC, NXIB_VERSION, w, h, 0) + img.tobytes()


# ============ Generated sources ============

def sym(name):
    return "nxpack_" + name.replace("-", "_")


def gen_asm(entries):
    out = [
        "/*",
        " * NeolyxOS - system icon pack (GENERATED by tools/build_icon_pack.py)",
        " *",
        " * NXIB icons linked into the desktop image as read-only data. Do not",
        " * edit; change resources/icons/pack/iconpack.manifest and re-run the",
        " * generator. Paths are relative to desktop/, where the Makefile runs.",
        " *",
        " * Copyright (c) 2025-2026 KetiveeAI",
        " */",
        "",
        '    .section .rodata.nxicon_pack, "a"',
    ]
    for e in entries:
        s = sym(e["name"])
        out += ["", "    .balign 16", f"    .global {s}, {s}_end", f"{s}:",
                f'    .incbin "../System/Desktop/Icons/{e["name"]}.nxi"', f"{s}_end:"]
    out += ["", '    .section .note.GNU-stack, "", @progbits', ""]
    return "\n".join(out)


def gen_index(entries):
    out = [
        "/*",
        " * NeolyxOS - system icon pack index (GENERATED by tools/build_icon_pack.py)",
        " *",
        " * X-macro list of the icons in desktop/shell/nxicon_pack.S:",
        " *   NXPACK(symbol, \"name\")",
        " * Do not edit; change resources/icons/pack/iconpack.manifest instead.",
        " *",
        " * Copyright (c) 2025-2026 KetiveeAI",
        " */",
        "",
        "/* No include guard: included once per expansion of NXPACK. */",
        "",
    ]
    for e in entries:
        out.append(f'NXPACK({sym(e["name"])}, "{e["name"]}")')
    out.append("")
    return "\n".join(out)


def main(argv):
    check = "--check" in argv[1:]
    try:
        entries = parse_manifest(MANIFEST)
        outputs = {}
        for e in entries:
            outputs[os.path.join(OUT_DIR, e["name"] + ".nxi")] = encode_nxib(build_icon(e))
        outputs[ASM_OUT] = gen_asm(entries).encode()
        outputs[INDEX_OUT] = gen_index(entries).encode()
    except PackError as exc:
        print(f"build_icon_pack: error: {exc}", file=sys.stderr)
        return 1
    total = sum(len(v) for k, v in outputs.items() if k.endswith(".nxi"))
    if check:
        stale = []
        for path, data in outputs.items():
            try:
                with open(path, "rb") as f:
                    if f.read() != data:
                        stale.append(path)
            except OSError:
                stale.append(path)
        for p in stale:
            print(f"build_icon_pack: stale: {os.path.relpath(p, ROOT)}", file=sys.stderr)
        return 1 if stale else 0
    os.makedirs(OUT_DIR, exist_ok=True)
    for path, data in outputs.items():
        with open(path, "wb") as f:
            f.write(data)
    print(f"build_icon_pack: {len(entries)} icons, {total // 1024} KiB of NXIB data")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
