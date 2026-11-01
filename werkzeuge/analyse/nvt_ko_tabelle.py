#!/usr/bin/env python3
# nvt_ko_tabelle.py - Chip-Tabelle (trim_id_table) und Speicherkarte aus einem Novatek-Touch-Modul (.ko)
# lesen, z.B. Xiaomis novatek_nt36528a_spi_ts.ko aus dem eigenen Android. Nur lesen, nur Zahlen ausgeben.
#
#   python3 nvt_ko_tabelle.py novatek_nt36528a_spi_ts.ko [28 65 03]
#
# Aufbau im Novatek-Treiber (nt36xxx_mem_map.h):
#   struct { u8 id[6]; u8 mask[6]; const struct mem_map *mmap; const struct hw_info *hwinfo; }  (64 Bit: 32 Byte)
# Die Zeiger stehen in der .ko als Relokationen (.rela.rodata o.ae.) - hier werden sie aufgeloest.
import struct, sys

ko = open(sys.argv[1], "rb").read()
such = bytes(int(x, 16) for x in (sys.argv[2:5] if len(sys.argv) >= 5 else ["28", "65", "03"]))
assert ko[:4] == b"\x7fELF" and ko[4] == 2 and ko[5] == 1, "kein ELF64 little-endian"

shoff, = struct.unpack_from("<Q", ko, 0x28)
shentsize, shnum, shstrndx = struct.unpack_from("<HHH", ko, 0x3A)
sek = []
for i in range(shnum):
    name, typ, flags, addr, off, size, link, info, align, entsize = struct.unpack_from("<IIQQQQIIQQ", ko, shoff + i * shentsize)
    sek.append(dict(name=name, typ=typ, off=off, size=size, link=link, info=info, entsize=entsize))
strtab = sek[shstrndx]
def sname(s):
    o = strtab["off"] + s["name"]; return ko[o:ko.index(b"\0", o)].decode()
for s in sek: s["n"] = sname(s)
def daten(i): s = sek[i]; return ko[s["off"]:s["off"] + s["size"]]

# Symboltabelle (fuer Relokationen: Symbol -> Abschnitt + Wert)
symtab = next(i for i, s in enumerate(sek) if s["typ"] == 2)
syms = []
st = sek[symtab]
for j in range(st["size"] // 24):
    nm, info, other, shndx, val, size = struct.unpack_from("<IBBHQQ", ko, st["off"] + j * 24)
    syms.append((shndx, val))

# Relokationen je Zielabschnitt: {zielabschnitt: {offset: (abschnitt, wert)}}
rel = {}
for s in sek:
    if s["typ"] == 4:                                   # SHT_RELA
        ziel = s["info"]; d = rel.setdefault(ziel, {})
        for j in range(s["size"] // 24):
            off, info, add = struct.unpack_from("<QQq", ko, s["off"] + j * 24)
            shndx, val = syms[info >> 32]
            d[off] = (shndx, val + add)

def u32s(abschnitt, off, n):
    b = daten(abschnitt)
    return [struct.unpack_from("<I", b, off + 4 * k)[0] for k in range(n) if off + 4 * k + 4 <= len(b)]

gefunden = 0
for i, s in enumerate(sek):
    if not (s["n"].startswith(".rodata") or s["n"].startswith(".data")) or s["typ"] != 1:
        continue
    b = daten(i); p = 0
    while True:
        p = b.find(such, p)
        if p < 0: break
        start = p - 3
        if start >= 0 and start % 8 == 0 and all(x in (0, 1) for x in b[start + 6:start + 12]):
            gefunden += 1
            idb, mask = b[start:start + 6], b[start + 6:start + 12]
            print("Eintrag in %s @0x%x: id = %s  mask = %s" % (s["n"], start,
                  " ".join("%02x" % x for x in idb), " ".join(str(x) for x in mask)))
            print("  Rohbytes Eintrag +12..+40: " + " ".join("%02x" % x for x in b[start + 12:start + 40]))
            for name, feld, n in (("mmap", 16, 64), ("hwinfo", 24, 4)):
                z = rel.get(i, {}).get(start + feld)
                if not z:
                    print("  %s: keine Relokation gefunden" % name); continue
                zs, zo = z
                print("  %s -> %s+0x%x:" % (name, sek[zs]["n"], zo))
                w = u32s(zs, zo, n) if name == "mmap" else list(daten(zs)[zo:zo + 8])
                for k in range(0, len(w), 8):
                    print("    [%2d] " % k + " ".join(("0x%05X" % x) if name == "mmap" else ("%d" % x) for x in w[k:k + 8]))
        p += 1
if not gefunden:
    print("Kennung %s nicht gefunden (andere Tabellenform?)" % such.hex(" "))
# Zur Orientierung: wie viele Eintraege die ganze Tabelle hat (alle id[3..5] mit 0x03 am Ende)
