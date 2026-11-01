#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
kernel_logo.py - beliebiges Bild in ein Kernel-Bootlogo (logo_linux_clut224.ppm) umwandeln.

Der Kernel verlangt: PPM im Textformat (P3), hoechstens 224 Farben.
Transparenz wird auf Schwarz gelegt (der Konsolenhintergrund ist schwarz).

  python3 kernel_logo.py ziege.png ziege_clut224.ppm [--groesse 300]

Danach im Kernel-Baum (siehe Anleitung im Chat):
  cp ziege_clut224.ppm ~/ums9230-linux/drivers/video/logo/logo_linux_clut224.ppm
"""
import sys

from PIL import Image


def main():
    a = sys.argv[1:]
    if len(a) < 2:
        print(__doc__)
        sys.exit(1)
    src, out = a[0], a[1]
    size = int(a[a.index("--groesse") + 1]) if "--groesse" in a else 300

    im = Image.open(src).convert("RGBA")
    im.thumbnail((size, size), Image.LANCZOS)
    bg = Image.new("RGBA", im.size, (0, 0, 0, 255))
    im = Image.alpha_composite(bg, im).convert("RGB")

    # auf 224 Farben reduzieren (Kernel-Grenze), dann als RGB-Werte ausschreiben
    im = im.quantize(colors=224, method=Image.MEDIANCUT, dither=Image.FLOYDSTEINBERG).convert("RGB")
    raw = im.tobytes()
    px = [tuple(raw[i:i + 3]) for i in range(0, len(raw), 3)]
    ncol = len(set(px))
    if ncol > 224:
        sys.exit("FEHLER: %d Farben, erlaubt sind 224" % ncol)

    w, h = im.size
    with open(out, "w") as f:
        f.write("P3\n# Kernel-Bootlogo aus %s\n%d %d\n255\n" % (src, w, h))
        line = []
        for r, g, b in px:
            line.append("%d %d %d" % (r, g, b))
            if len(line) == 4:
                f.write("  ".join(line) + "\n")
                line = []
        if line:
            f.write("  ".join(line) + "\n")
    print("%s: %dx%d, %d Farben" % (out, w, h, ncol))


if __name__ == "__main__":
    main()
