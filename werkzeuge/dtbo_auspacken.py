#!/usr/bin/env python3
# dtbo_auspacken.py - Android-dtbo-Partition (Tabelle mit mehreren Device-Tree-Overlays)
# in einzelne .dtb/.dts zerlegen. Liest nur, aendert nichts.
#
#   python3 ~/redmi-tools/dtbo_auspacken.py ~/redmi-unlock-work/dtbo_b.bin ~/redmi-build/dtbo
# Ergebnis: ~/redmi-build/dtbo/overlay_<n>.dtb und .dts (dts nur, wenn dtc installiert ist)
import os, struct, subprocess, sys

if len(sys.argv) != 3:
    print(__doc__ or "Nutzung: dtbo_auspacken.py <dtbo.bin> <zielordner>"); sys.exit(1)
src, out = sys.argv[1], os.path.expanduser(sys.argv[2])
data = open(src, "rb").read()
MAGIC = 0xd7b7ab1e
magic, total, hsize, esize, count, eoff, page, ver = struct.unpack(">8I", data[:32])
if magic != MAGIC:
    print(f"FEHLER: keine dtbo-Tabelle (Kennung {magic:#x}, erwartet {MAGIC:#x})"); sys.exit(1)
print(f">>> dtbo-Tabelle: {count} Overlays, Gesamtgroesse {total} Bytes, Version {ver}")
os.makedirs(out, exist_ok=True)
for i in range(count):
    o = eoff + i * esize
    size, off, ident, rev = struct.unpack(">4I", data[o:o + 16])
    blob = data[off:off + size]
    ok = blob[:4] == b"\xd0\x0d\xfe\xed"
    p = os.path.join(out, f"overlay_{i}.dtb")
    open(p, "wb").write(blob)
    line = f"    overlay_{i}: {size} Bytes, id={ident:#x} rev={rev:#x}" + ("" if ok else "  !! keine DTB-Kennung")
    try:
        dts = subprocess.run(["dtc", "-I", "dtb", "-O", "dts", "-q", p], capture_output=True, text=True).stdout
        open(p[:-4] + ".dts", "w").write(dts)
        # Hinweise, wofuer das Overlay zustaendig ist
        keys = [k for k in ("charger", "bq25", "sgm4", "battery", "fgu", "panel", "touch") if k in dts.lower()]
        line += "  [" + ", ".join(keys) + "]" if keys else ""
    except FileNotFoundError:
        pass
    print(line)
print(f">>> fertig: {out}")
