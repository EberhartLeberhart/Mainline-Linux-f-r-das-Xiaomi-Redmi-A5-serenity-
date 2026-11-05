# SPDX-License-Identifier: GPL-2.0-only
# misc_slot_b.py - misc-Abbild (1 MiB) erzeugen, das den Bootloader Slot b starten laesst (05.11.2026).
# Nachgebaut nach unserem misc_b.img vom 27.09. (Aufbau nach AOSP bootloader_control / misc_virtual_ab_message):
#   0x0000  Befehl "bootonce-bootloader" (stand so in unserem misc_b.img, der Bootloader startet trotzdem normal)
#   0x0800  bootloader_control: Endung "_b", Magie "BCAB", Version 1, 2 Slots,
#           Slot a: Prioritaet 14, 7 Versuche, erfolgreich; Slot b: Prioritaet 15, 7 Versuche, noch nicht erfolgreich
#           -> Bootloader nimmt b; scheitert b 7x, wieder a. Danach CRC32 ueber die ersten 28 Bytes.
#   0x8000  misc_virtual_ab_message: Version 2, Magie 0x56740AB0, sonst 0
#   python3 misc_slot_b.py misc_b.img
#
# --ok: nur die 32 Bytes bootloader_control mit Slot b "erfolgreich" (Prioritaet 15, 7 Versuche) - die schreibt der Dienst
#   redmi-boot-ok.service nach jedem gelungenen Start an Stelle 0x800 ins misc (sonst zaehlt der Bootloader die Versuche
#   herunter und startet nach 7 Starts wieder Android). Gegenprobe 05.11.: gleich /usr/local/lib/redmi/abc_b_ok.bin.
#   python3 misc_slot_b.py --ok abc_b_ok.bin
import struct, sys, zlib

def slot(prio, versuche, erfolgreich):
    return bytes([(prio & 0xf) | ((versuche & 7) << 4) | (0x80 if erfolgreich else 0), 0])

def bootloader_control(b_erfolgreich):
    bc = b"_b\0\0" + b"BCAB" + bytes([1, 2, 0, 0]) + slot(14, 7, True) + slot(15, 7, b_erfolgreich) + bytes(4) + bytes(8)
    assert len(bc) == 28
    return bc + struct.pack("<I", zlib.crc32(bc))

if sys.argv[1] == "--ok":
    open(sys.argv[2], "wb").write(bootloader_control(True))
    print(f"{sys.argv[2]}: 32 Bytes, Slot b erfolgreich gestartet (fuer misc ab 0x800)")
    sys.exit(0)

img = bytearray(1024 * 1024)
befehl = b"bootonce-bootloader"
img[0:len(befehl)] = befehl
img[0x800:0x800 + 32] = bootloader_control(False)
img[0x8000:0x8005] = bytes([2]) + struct.pack("<I", 0x56740AB0)
open(sys.argv[1], "wb").write(img)
print(f"{sys.argv[1]}: Slot b aktiv (Prioritaet 15, 7 Versuche), Rueckfall Slot a")
