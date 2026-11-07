# SPDX-License-Identifier: GPL-2.0-only
# leere_images.py - zwei kleine Abbilder, ohne die ein frisches Redmi A5 (Werkszustand) unseren Kernel nicht startet
# (gefunden 07.11.2026 am zweiten Redmi, Issue #26):
#
#   init_boot_leer.img  Android-Boot-Image v4 mit leerer Ramdisk. Auf einem Werksgeraet liegt in init_boot_b Androids
#                       Start-Ramdisk; der Bootloader gibt sie unserem Kernel mit, der startet dann Androids init statt
#                       Ubuntu von userdata -> Neustart-Schleife.
#   dtbo_leer.img       dtbo-Tabelle mit einem leeren Overlay ("/ { };"). Xiaomis dtbo passt nicht zu unserem
#                       Geraetebaum; mit der leeren Folie bleibt unser Baum unveraendert.
#
#   python3 leere_images.py <zielordner>
#   danach: fastboot flash init_boot_b init_boot_leer.img ; fastboot flash dtbo_b dtbo_leer.img
# Beides enthaelt nichts von Xiaomi. Slot a (Android) bleibt unberuehrt.
import gzip, os, struct, sys

if len(sys.argv) != 2:
    print(open(__file__).read().split("\nimport")[0]); sys.exit(1)
out = sys.argv[1]
os.makedirs(out, exist_ok=True)

# --- dtbo: Tabellenkopf + ein Eintrag + leerer Geraetebaum (genau wie auf unserem laufenden Handy, 136 Bytes) ---
fdt = bytes.fromhex("d00dfeed00000048000000380000004800000028000000110000001000000000"
                    "0000000000000010000000000000000000000000000000000000000100000000"
                    "0000000200000009")
assert len(fdt) == 0x48
kopf = struct.pack(">8I", 0xd7b7ab1e, 0x40 + len(fdt), 0x20, 0x20, 1, 0x20, 0x800, 0)
eintrag = struct.pack(">8I", len(fdt), 0x40, 0x002f0000, 0, 0, 0, 0, 0)
open(os.path.join(out, "dtbo_leer.img"), "wb").write(kopf + eintrag + fdt)

# --- init_boot: Boot-Image-Kopf v4 (ohne Kernel) + leere Ramdisk (cpio newc nur "." + TRAILER, gzip) ---
def cpio(name, mode, nlink):
    n = name.encode() + b"\0"
    d = b"070701" + b"".join(b"%08X" % v for v in (0, mode, 0, 0, nlink, 0, 0, 0, 0, 0, 0, len(n), 0)) + n
    return d + b"\0" * (-len(d) % 4)
c = cpio(".", 0o40755, 2) + cpio("TRAILER!!!", 0, 1)
ramdisk = gzip.compress(c + b"\0" * (-len(c) % 512), mtime=0)
SEITE = 4096
hdr = b"ANDROID!" + struct.pack("<4I", 0, len(ramdisk), 0, 1584) + bytes(16) + struct.pack("<I", 4) \
      + bytes(1536) + struct.pack("<I", 0)
assert len(hdr) == 1584
img = hdr + bytes(SEITE - len(hdr)) + ramdisk + bytes(-len(ramdisk) % SEITE)
open(os.path.join(out, "init_boot_leer.img"), "wb").write(img)
print(f"{out}/dtbo_leer.img: {len(kopf + eintrag + fdt)} Bytes, {out}/init_boot_leer.img: {len(img)} Bytes (Ramdisk {len(ramdisk)})")
