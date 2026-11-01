#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
redmi_logo.py - Startlogo des Redmi A5 (Unisoc) aus- und einpacken.

Format von logo.bin (aus dem Stock-ROM entschluesselt):
  0x00  "GZ"                 Kennung
  0x02  u16 (LE)             Anzahl Bilder (Stock: 9)
  0x04  20 Bytes             reserviert (0)
  0x18  Anzahl x u32 (LE)    Groesse jedes Eintrags
  danach                     die Eintraege hintereinander, je ein gzip-gepacktes BMP

Befehle:
  python3 redmi_logo.py info   logo.bin
  python3 redmi_logo.py unpack logo.bin ordner/        -> ordner/1.bmp ... 9.bmp (+ .png, falls Pillow da ist)
  python3 redmi_logo.py pack   ordner/ neu_logo.bin    -> packt ordner/1.bmp ... N.bmp wieder ein
  python3 redmi_logo.py replace logo.bin bild.png neu_logo.bin [--nur 1,4]
        -> ersetzt Bilder durch bild.png (auf 720x1640 skaliert, Format wie das Original)
  python3 redmi_logo.py compose logo.bin neu_logo.bin --nur 8 --mitte ziege.png --unten tux.ppm
        -> tauscht im Original nur das Motiv in der Mitte (Redmi) und unten (Android) aus,
           alles andere (Schloss, Warntext) bleibt. Legt zusaetzlich vorschau_N.png an.
"""

import gzip
import io
import os
import struct
import sys

MAGIC = b"GZ"


def parse(data):
    if data[:2] != MAGIC:
        sys.exit("FEHLER: keine Logo-Datei (Kennung %r statt 'GZ')" % data[:2])
    count = struct.unpack("<H", data[2:4])[0]
    sizes = list(struct.unpack("<%dI" % count, data[0x18:0x18 + 4 * count]))
    pos = 0x18 + 4 * count
    entries = []
    for s in sizes:
        entries.append(data[pos:pos + s])
        pos += s
    if pos != len(data):
        print("Hinweis: %d Bytes am Ende uebrig" % (len(data) - pos), file=sys.stderr)
    return entries


def build(entries):
    head = MAGIC + struct.pack("<H", len(entries)) + b"\0" * 20
    head += struct.pack("<%dI" % len(entries), *[len(e) for e in entries])
    return head + b"".join(entries)


def bmp_info(bmp):
    if bmp[:2] != b"BM":
        return None
    off = struct.unpack("<I", bmp[10:14])[0]
    w, h = struct.unpack("<ii", bmp[18:26])
    bpp = struct.unpack("<H", bmp[28:30])[0]
    comp = struct.unpack("<I", bmp[30:34])[0]
    return {"offset": off, "w": w, "h": h, "bpp": bpp, "comp": comp}


def gz_name(entry):
    # Dateiname aus dem gzip-Kopf (FNAME-Flag)
    if len(entry) > 10 and entry[:2] == b"\x1f\x8b" and entry[3] & 0x08:
        e = entry.index(b"\0", 10)
        return entry[10:e].decode("latin-1")
    return None


def gz_pack(bmp, name):
    buf = io.BytesIO()
    with gzip.GzipFile(filename=name, mode="wb", fileobj=buf, compresslevel=9, mtime=0) as g:
        g.write(bmp)
    return buf.getvalue()


def cmd_info(path):
    entries = parse(open(path, "rb").read())
    print("%s: %d Bilder" % (path, len(entries)))
    for i, e in enumerate(entries, 1):
        bmp = gzip.decompress(e)
        bi = bmp_info(bmp)
        print("  %d: %-8s gepackt %6d, entpackt %8d  %s" % (
            i, gz_name(e) or "?", len(e), len(bmp),
            ("%dx%d, %d bit" % (bi["w"], abs(bi["h"]), bi["bpp"])) if bi else "kein BMP"))


def cmd_unpack(path, out):
    os.makedirs(out, exist_ok=True)
    entries = parse(open(path, "rb").read())
    try:
        from PIL import Image
    except ImportError:
        Image = None
    for i, e in enumerate(entries, 1):
        bmp = gzip.decompress(e)
        p = os.path.join(out, "%d.bmp" % i)
        open(p, "wb").write(bmp)
        if Image is not None:
            try:
                Image.open(p).save(os.path.join(out, "%d.png" % i))
            except Exception as ex:  # noqa: BLE001
                print("  %d.png nicht erzeugt: %s" % (i, ex))
        print("  %s (%s)" % (p, gz_name(e) or "?"))
    if Image is None:
        print("Tipp: 'sudo apt install python3-pil' fuer PNG-Vorschaubilder")


def cmd_pack(folder, out):
    files = sorted((f for f in os.listdir(folder) if f.endswith(".bmp") and f[:-4].isdigit()),
                   key=lambda f: int(f[:-4]))
    if not files:
        sys.exit("FEHLER: keine 1.bmp, 2.bmp ... in %s" % folder)
    entries = [gz_pack(open(os.path.join(folder, f), "rb").read(), f) for f in files]
    open(out, "wb").write(build(entries))
    print("%s: %d Bilder, %d Bytes" % (out, len(entries), os.path.getsize(out)))


def to_bmp_like(template_bmp, img_path):
    """PNG/JPG in ein BMP mit exakt dem Kopf/Format des Originals umwandeln."""
    try:
        from PIL import Image
    except ImportError:
        sys.exit("FEHLER: Pillow fehlt - sudo apt install python3-pil")
    bi = bmp_info(template_bmp)
    if not bi or bi["bpp"] != 32 or bi["comp"] not in (0, 3):
        sys.exit("FEHLER: Original ist kein 32-bit-BMP (%r) - bitte melden" % bi)
    w, h = bi["w"], abs(bi["h"])
    if isinstance(img_path, Image.Image):
        im = img_path.convert("RGB")
    else:
        im = Image.open(img_path).convert("RGB")
    if im.size == (w, h):
        canvas = im
    else:
        # proportional einpassen, Rest schwarz
        im.thumbnail((w, h))
        canvas = Image.new("RGB", (w, h), (0, 0, 0))
        canvas.paste(im, ((w - im.width) // 2, (h - im.height) // 2))
    px = canvas.tobytes("raw", "BGRX")
    stride = w * 4
    rows = [px[y * stride:(y + 1) * stride] for y in range(h)]
    if bi["h"] > 0:  # BMP ist normalerweise von unten nach oben gespeichert
        rows.reverse()
    head = template_bmp[:bi["offset"]]
    data = b"".join(rows)
    # Alphakanal auf 0xff setzen (manche Bootloader werten ihn aus)
    data = bytes(b if (i % 4) != 3 else 0xFF for i, b in enumerate(data))
    return head + data


def cmd_replace(path, img, out, only=None):
    entries = parse(open(path, "rb").read())
    new = []
    for i, e in enumerate(entries, 1):
        if only and i not in only:
            new.append(e)
            continue
        tpl = gzip.decompress(e)
        bmp = to_bmp_like(tpl, img)
        if len(bmp) != len(tpl):
            sys.exit("FEHLER: Groesse passt nicht (%d statt %d)" % (len(bmp), len(tpl)))
        new.append(gz_pack(bmp, gz_name(e) or "%d.bmp" % i))
        print("  Bild %d ersetzt" % i)
    open(out, "wb").write(build(new))
    print("%s: %d Bytes" % (out, os.path.getsize(out)))


def find_blocks(im, bg, gap=24, thr=40):
    """Motive im Bild finden: Zeilenbereiche mit Inhalt, getrennt durch leere Streifen."""
    from PIL import Image, ImageChops
    w, h = im.size
    mask = ImageChops.difference(im, Image.new("RGB", im.size, bg)).convert("L")
    mask = mask.point(lambda v: 255 if v > thr else 0)
    blocks, cur, empty = [], None, 0
    for y in range(h):
        bb = mask.crop((0, y, w, y + 1)).getbbox()
        if bb:
            if cur is None:
                cur = [bb[0], y, bb[2], y + 1]
            else:
                cur = [min(cur[0], bb[0]), cur[1], max(cur[2], bb[2]), y + 1]
            empty = 0
        elif cur is not None:
            empty += 1
            if empty >= gap:
                blocks.append(tuple(cur))
                cur, empty = None, 0
    if cur is not None:
        blocks.append(tuple(cur))
    return blocks


def fit(img, maxw, maxh):
    """Einpassen; kleine Bilder (Pixel-Pinguin) ganzzahlig und scharf vergroessern."""
    from PIL import Image
    f = min(maxw // img.width, maxh // img.height)
    if f >= 2:
        return img.resize((img.width * f, img.height * f), Image.NEAREST)
    img = img.copy()
    img.thumbnail((maxw, maxh), Image.LANCZOS)
    return img


def place(canvas, img, bg, cx, cy):
    from PIL import Image
    w, h = canvas.size
    x = min(max(cx - img.width // 2, 0), w - img.width)
    y = min(max(cy - img.height // 2, 0), h - img.height)
    if img.mode in ("RGBA", "LA", "P"):
        img = img.convert("RGBA")
        base = Image.new("RGBA", img.size, bg + (255,))
        img = Image.alpha_composite(base, img)
    canvas.paste(img.convert("RGB"), (x, y))


def cmd_compose(path, out, only, mitte, unten):
    from PIL import Image
    entries = parse(open(path, "rb").read())
    new = []
    for i, e in enumerate(entries, 1):
        if only and i not in only:
            new.append(e)
            continue
        tpl = gzip.decompress(e)
        im = Image.open(io.BytesIO(tpl)).convert("RGB")
        w, h = im.size
        bg = im.getpixel((2, 2))
        blocks = find_blocks(im, bg)
        print("  Bild %d: Hintergrund %s, Motive (x0,y0,x1,y1): %s" % (i, bg, blocks))
        if not blocks:
            sys.exit("FEHLER: in Bild %d keine Motive gefunden" % i)
        mid = min(blocks, key=lambda b: abs((b[1] + b[3]) / 2 - h / 2))
        low = max(blocks, key=lambda b: b[3])
        canvas = im.copy()
        todo = []
        if mitte:
            todo.append(("mitte", mid, mitte, int(w * 0.62), int(w * 0.62)))
        if unten:
            if low == mid or (low[1] + low[3]) / 2 < h * 0.66:
                print("  Warnung: unten kein eigenes Motiv gefunden - setze es ins untere Achtel")
                low = (0, int(h * 0.82), w, int(h * 0.94))
            cy = (low[1] + low[3]) // 2
            mh = min(int(h * 0.16), max(low[3] - low[1], 2 * (h - cy) - 40))
            todo.append(("unten", low, unten, max(low[2] - low[0], int(w * 0.40)), mh))
        for name, b, src, mw, mh in todo:
            pad = 6
            canvas.paste(bg, (max(b[0] - pad, 0), max(b[1] - pad, 0),
                              min(b[2] + pad, w), min(b[3] + pad, h)))
            img = fit(Image.open(src), mw, mh)
            cx, cy = (b[0] + b[2]) // 2, (b[1] + b[3]) // 2
            place(canvas, img, bg, cx, cy)
            print("    %s: %s -> %dx%d bei Mitte (%d,%d)" % (name, src, img.width, img.height, cx, cy))
        vp = os.path.join(os.path.dirname(out) or ".", "vorschau_%d.png" % i)
        canvas.save(vp)
        print("    Vorschau: %s" % vp)
        bmp = to_bmp_like(tpl, canvas)
        if len(bmp) != len(tpl):
            sys.exit("FEHLER: Groesse passt nicht (%d statt %d)" % (len(bmp), len(tpl)))
        new.append(gz_pack(bmp, gz_name(e) or "%d.bmp" % i))
    open(out, "wb").write(build(new))
    print("%s: %d Bytes" % (out, os.path.getsize(out)))


def main():
    a = sys.argv[1:]
    if len(a) >= 2 and a[0] == "info":
        cmd_info(a[1])
    elif len(a) >= 3 and a[0] == "unpack":
        cmd_unpack(a[1], a[2])
    elif len(a) >= 3 and a[0] == "pack":
        cmd_pack(a[1], a[2])
    elif len(a) >= 4 and a[0] == "replace":
        only = None
        if "--nur" in a:
            only = {int(x) for x in a[a.index("--nur") + 1].split(",")}
        cmd_replace(a[1], a[2], a[3], only)
    elif len(a) >= 3 and a[0] == "compose":
        def opt(n):
            return a[a.index(n) + 1] if n in a else None
        only = {int(x) for x in opt("--nur").split(",")} if opt("--nur") else None
        if not (opt("--mitte") or opt("--unten")):
            sys.exit("FEHLER: --mitte und/oder --unten angeben")
        cmd_compose(a[1], a[2], only, opt("--mitte"), opt("--unten"))
    else:
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    main()
