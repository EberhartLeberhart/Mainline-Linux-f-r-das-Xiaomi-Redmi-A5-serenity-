# SPDX-License-Identifier: GPL-2.0-only
# wrap_dtb.py - einen Geraetebaum (.dtb) in die DT-Tabelle verpacken, die der Bootloader im vendor_boot erwartet
# (Magie 0xd7b7ab1e, ein Eintrag). Seit 26.09.2026 im Einsatz, 05.11. ins Repo.
#   python3 wrap_dtb.py serenity.dtb serenity_table.dtb
import struct, sys
dtb = open(sys.argv[1], "rb").read()
hdr = struct.pack(">8I", 0xd7b7ab1e, 0x40 + len(dtb), 0x20, 0x20, 1, 0x20, 0x800, 0)
ent = struct.pack(">8I", len(dtb), 0x40, 0, 0, 0, 0, 0, 0)
open(sys.argv[2], "wb").write(hdr + ent + dtb)
print(f"{sys.argv[2]}: {0x40 + len(dtb)} Bytes")
