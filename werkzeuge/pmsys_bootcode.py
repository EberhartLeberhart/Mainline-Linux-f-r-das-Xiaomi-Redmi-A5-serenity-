#!/usr/bin/env python3
# pmsys_bootcode.py - Startcode des Hilfskerns pmsys aus Android-Konfiguration ziehen (fuer modem_starttest Stufe 2).
#
#   python3 werkzeuge/pmsys_bootcode.py /mnt/vendor/etc/modem_sp_info.xml pmsys_bootcode.bin
#   scp pmsys_bootcode.bin root@192.168.7.2:/lib/firmware/
#
# Liest das Attribut bootcode="..." des Eintrags pm_sys_bootcode (Liste von 32-Bit-Woertern) und schreibt sie
# little-endian hintereinander. Die Datei stammt aus der vendor-Partition und kommt NICHT ins Repo.
import struct, sys, xml.etree.ElementTree as ET

quelle, ziel = sys.argv[1], sys.argv[2]
for e in ET.parse(quelle).getroot().iter():
    if e.get('name') == 'pm_sys_bootcode' and e.get('bootcode'):
        w = [int(x, 16) for x in e.get('bootcode').split()]
        with open(ziel, 'wb') as f:
            f.write(b''.join(struct.pack('<I', x) for x in w))
        print(f"{len(w)} Woerter ({4 * len(w)} Bytes) nach {ziel}, Ziel laut XML {e.get('base')}, "
              f"erste zwei Woerter (SP, Reset): 0x{w[0]:08x} 0x{w[1]:08x}")
        break
else:
    sys.exit("kein pm_sys_bootcode mit bootcode-Attribut gefunden")
