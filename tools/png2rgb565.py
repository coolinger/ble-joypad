#!/usr/bin/env python3
"""Convert a PNG into an RGB565 C header for Arduino_GFX draw16bitRGBBitmap().

Alpha is composited over black; the image is scaled to the target width
keeping aspect ratio. Output uses natural RGB565 word values, i.e. draw with
tft.setSwapBytes(true).

Usage (Pillow required, e.g. d:/pio/penv/Scripts/python.exe -m pip install pillow):
    python tools/png2rgb565.py ed_logo.png src/ed_logo.h ed_logo 320 [whitebg]

The optional "whitebg" flag treats white source pixels as transparent: the
white is un-mixed out of every pixel (alpha = 255 - min(r,g,b)), so
anti-aliased edges stay clean when the image lands on a black screen.
"""
import sys
from PIL import Image


def unmix_white(img: Image.Image) -> Image.Image:
    """Convert 'artwork on white' into 'artwork on black'."""
    out = Image.new("RGB", img.size)
    src_px = img.convert("RGB").load()
    out_px = out.load()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b = src_px[x, y]
            a = 255 - min(r, g, b)          # how "non-white" this pixel is
            if a == 0:
                out_px[x, y] = (0, 0, 0)
                continue
            w = 255 - a                      # white portion to remove
            out_px[x, y] = tuple(max(0, min(255, (c - w) * 255 // a)) for c in (r, g, b))
    return out


def main() -> None:
    in_file, out_file, name, target_w = (
        sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]))
    white_bg = len(sys.argv) > 5 and sys.argv[5] == "whitebg"

    src = Image.open(in_file).convert("RGBA")
    if white_bg:
        src = unmix_white(src).convert("RGBA")
    w = target_w
    h = round(src.height * w / src.width)
    scaled = src.resize((w, h), Image.LANCZOS)
    bg = Image.new("RGBA", (w, h), (0, 0, 0, 255))
    rgb = Image.alpha_composite(bg, scaled).convert("RGB")

    px = rgb.load()
    lines = [
        f"// Auto-generated from {in_file} by tools/png2rgb565.py - do not edit by hand.",
        f"// {w}x{h} RGB565 (natural word order, draw with tft.setSwapBytes(true)),",
        "// alpha composited over black.",
        "#pragma once",
        "#include <stdint.h>",
        "",
        f"#define {name.upper()}_W {w}",
        f"#define {name.upper()}_H {h}",
        "",
        f"const uint16_t {name}_map[{name.upper()}_W * {name.upper()}_H] = {{",
    ]
    for y in range(h):
        row = []
        for x in range(w):
            r, g, b = px[x, y]
            v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            row.append(f"0x{v:04x},")
        lines.append("".join(row))
    lines.append("};")
    lines.append("")

    with open(out_file, "w", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"Wrote {out_file} ({w}x{h}, {w * h} px, {w * h * 2 // 1024} KB flash)")


if __name__ == "__main__":
    main()
