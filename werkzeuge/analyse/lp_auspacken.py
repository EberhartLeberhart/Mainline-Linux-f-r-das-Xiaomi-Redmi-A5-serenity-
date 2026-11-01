#!/usr/bin/env python3
# lp_auspacken.py - Inhaltsverzeichnis einer Android-"super"-Partition (liblp, dynamische Partitionen)
# lesen und Befehle erzeugen, die EINE logische Partition (z.B. vendor_a) einzeln vom Handy holen.
# Nur lesen - am Handy wird nichts veraendert.
#
#   1) Kopf holen:   ssh root@192.168.7.2 "dd if=/dev/disk/by-partlabel/super bs=1M count=1" > super_kopf.bin
#   2) Liste:        python3 lp_auspacken.py super_kopf.bin
#   3) Befehle:      python3 lp_auspacken.py super_kopf.bin vendor_a > hole_vendor.sh ; sh hole_vendor.sh
import struct, sys

SEKTOR = 512
GEO_OFF = 4096                      # LP_PARTITION_RESERVED_BYTES
GEO_MAGIC, HDR_MAGIC = 0x616C4467, 0x414C5030

if len(sys.argv) not in (2, 3):
    print("Nutzung: lp_auspacken.py super_kopf.bin [partition]"); sys.exit(1)
d = open(sys.argv[1], "rb").read()

g_magic, g_size = struct.unpack_from("<II", d, GEO_OFF)
if g_magic != GEO_MAGIC:
    print(f"FEHLER: keine super-Partition (Geometrie-Magic 0x{g_magic:08x})"); sys.exit(1)
meta_max, slots, blk = struct.unpack_from("<III", d, GEO_OFF + 40)
meta_off = GEO_OFF + 2 * 4096       # nach zwei Geometrie-Kopien: Metadaten Slot 0

def tabelle(h, pos):
    off, n, size = struct.unpack_from("<III", d, h + pos)
    return off, n, size

h = meta_off
magic, major, minor, hsize = struct.unpack_from("<IHHI", d, h)
if magic != HDR_MAGIC:
    print(f"FEHLER: Metadaten-Kopf nicht gefunden (0x{magic:08x})"); sys.exit(1)
tab = h + hsize
p_off, p_n, p_sz = tabelle(h, 80)
e_off, e_n, e_sz = tabelle(h, 92)

extents = []
for i in range(e_n):
    nsec, ttype, tdata, tsrc = struct.unpack_from("<QIQI", d, tab + e_off + i * e_sz)
    extents.append((nsec, ttype, tdata, tsrc))

parts = []
for i in range(p_n):
    o = tab + p_off + i * p_sz
    name = d[o:o + 36].split(b"\0")[0].decode()
    attr, first, num, grp = struct.unpack_from("<IIII", d, o + 36)
    ex = extents[first:first + num]
    parts.append((name, attr, ex))

if len(sys.argv) == 2:
    print(f"super: Metadaten v{major}.{minor}, {slots} Slots, Blockgroesse {blk}, {p_n} Partitionen")
    for name, attr, ex in parts:
        mb = sum(e[0] for e in ex) * SEKTOR / 1048576
        print(f"  {name:20s} {mb:9.1f} MB  {len(ex)} Stueck(e){'  (leer)' if not ex else ''}")
    sys.exit(0)

ziel = sys.argv[2]
treffer = [p for p in parts if p[0] == ziel]
if not treffer:
    print(f"# FEHLER: Partition {ziel} nicht gefunden", file=sys.stderr); sys.exit(1)
name, attr, ex = treffer[0]
if not ex:
    print(f"# FEHLER: {ziel} ist leer", file=sys.stderr); sys.exit(1)
if any(e[1] != 0 or e[3] != 0 for e in ex):
    print("# FEHLER: unerwartete Stueckart (nicht linear / anderes Geraet)", file=sys.stderr); sys.exit(1)
print(f"# holt {ziel} ({sum(e[0] for e in ex) * SEKTOR} Bytes, {len(ex)} Stueck(e)) nur lesend vom Handy")
print(f": > {ziel}.img")
for nsec, _, start, _ in ex:
    print(f'ssh root@192.168.7.2 "dd if=/dev/disk/by-partlabel/super bs=4M iflag=skip_bytes,count_bytes '
          f'skip={start * SEKTOR} count={nsec * SEKTOR} status=none" >> {ziel}.img')
print(f'echo ">>> fertig: $(stat -c %s {ziel}.img) Bytes (erwartet {sum(e[0] for e in ex) * SEKTOR})"')
