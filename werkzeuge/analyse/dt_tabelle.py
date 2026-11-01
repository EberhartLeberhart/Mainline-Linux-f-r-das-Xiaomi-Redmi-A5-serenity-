#!/usr/bin/env python3
# dt_tabelle.py - Android-DT-Tabelle (dtb/dtbo-Partition, Magic 0xd7b7ab1e) aufschluesseln
# und jeden Eintrag als eigene Datei herausschreiben. Nur lesen - die Eingabe wird nicht veraendert.
#
#   python3 dt_tabelle.py <partition.bin> <ausgabeordner>
#   -> <ausgabeordner>/eintrag_<n>.dtb  (+ Liste mit id/rev/custom und Wurzel-Infos)
import struct, sys, os, subprocess

if len(sys.argv) != 3:
    print(__doc__ if __doc__ else "Nutzung: dt_tabelle.py <partition.bin> <ausgabeordner>"); sys.exit(1)
quelle, ziel = sys.argv[1], sys.argv[2]
d = open(quelle, "rb").read()
magic, total, hsize, esize, count, eoff, page, ver = struct.unpack(">8I", d[:32])
if magic != 0xd7b7ab1e:
    # keine Tabelle - vielleicht ein einzelner Device-Tree?
    if d[:4] == b"\xd0\x0d\xfe\xed":
        print(f"{quelle}: KEINE Tabelle, sondern ein einzelner Device-Tree ({struct.unpack('>I', d[4:8])[0]} Bytes)")
    else:
        print(f"{quelle}: unbekanntes Format (Magic 0x{magic:08x})")
    sys.exit(1)
os.makedirs(ziel, exist_ok=True)
print(f"{quelle}: DT-Tabelle Version {ver}, {count} Eintraege, Gesamtgroesse {total} Bytes")

def wurzel(datei):
    """model / compatible der Wurzel und Zahl der Fragmente (bei Overlays) lesen"""
    def get(p, e):
        r = subprocess.run(["fdtget", datei, p, e], capture_output=True, text=True)
        return r.stdout.strip() if r.returncode == 0 else ""
    r = subprocess.run(["fdtget", "-l", datei, "/"], capture_output=True, text=True)
    frag = [x for x in r.stdout.split() if x.startswith("fragment@")]
    return get("/", "model"), get("/", "compatible"), len(frag)

for i in range(count):
    o = eoff + i * esize
    dsize, doff, eid, rev, c0, c1, c2, c3 = struct.unpack(">8I", d[o:o + 32])
    blob = d[doff:doff + dsize]
    ok = blob[:4] == b"\xd0\x0d\xfe\xed"
    f = os.path.join(ziel, f"eintrag_{i}.dtb")
    open(f, "wb").write(blob)
    model, compat, nfrag = wurzel(f) if ok else ("", "", 0)
    print(f"  [{i}] {dsize:7d} Bytes  id=0x{eid:x} rev=0x{rev:x} custom={c0:x},{c1:x},{c2:x},{c3:x}"
          f"  {'DTB' if ok else '?? kein DTB'}"
          f"{'  Overlay mit %d Fragmenten' % nfrag if nfrag else ''}"
          f"{'  model=' + model if model else ''}{'  compatible=' + compat if compat else ''}")
print(f">>> Eintraege liegen in {ziel}/")
