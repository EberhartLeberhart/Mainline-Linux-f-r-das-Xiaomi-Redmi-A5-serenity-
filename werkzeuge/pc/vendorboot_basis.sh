#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# vendorboot_basis.sh - vendor_boot von Grund auf bauen (05.11.2026): nur Geraetebaum, leere Ramdisk, bootconfig.
# Ersetzt das alte, von Hand entstandene vendor_boot_usb2.img (gleicher Inhalt, siehe Gegenprobe) - damit braucht der
# Bau keine Datei mehr, die nur auf unserem PC liegt. Es steckt nichts von Xiaomi darin.
#
#   vendorboot_basis.sh <geraetebaum.dtb> <ausgabe.img>
#   Gegenprobe gegen ein vorhandenes Image:  VERGLEICH=~/redmi-build/vendor_boot_usb2.img vendorboot_basis.sh ...
set -e
DTB=$1; OUT=$2
[ -f "$DTB" ] && [ -n "$OUT" ] || { sed -n 3,9p "$0"; exit 1; }
MKB=${MKB:-$HOME/mkbootimg-aosp}
HIER=$(dirname "$(readlink -f "$0")")
[ -f "$MKB/mkbootimg.py" ] || { echo "FEHLER: $MKB/mkbootimg.py fehlt (AOSP system/tools/mkbootimg)"; exit 1; }
W=$(mktemp -d)

python3 "$HIER/wrap_dtb.py" "$DTB" $W/dt_table.dtb >/dev/null
# leere Ramdisk: cpio (newc) nur mit "." und TRAILER!!!, gzip - wie im alten Image (95 Bytes)
python3 - $W/ramdisk.gz <<'PY'
import gzip, sys
def eintrag(name, mode, nlink):
    n = name.encode() + b"\0"
    kopf = b"070701" + b"".join(b"%08X" % v for v in (0, mode, 0, 0, nlink, 0, 0, 0, 0, 0, 0, len(n), 0))
    d = kopf + n
    return d + b"\0" * (-len(d) % 4)
cpio = eintrag(".", 0o40755, 2) + eintrag("TRAILER!!!", 0, 1)
cpio += b"\0" * (-len(cpio) % 512)
open(sys.argv[1], "wb").write(gzip.compress(cpio, mtime=0))
PY
printf 'androidboot.hardware=serenity\nandroidboot.dtbo_idx=0\n' > $W/bootconfig

# "bootconfig" steht im alten Image doppelt (harmlos, Herkunft unklar) - bewusst gleich gelassen, damit sich nichts aendert
python3 "$MKB/mkbootimg.py" --header_version 4 --pagesize 0x00001000 --base 0x00000000 \
    --kernel_offset 0x00008000 --ramdisk_offset 0x05400000 --tags_offset 0x00000100 --dtb_offset 0x01f00000 \
    --vendor_cmdline 'console=ttyS1,115200n8 bootconfig bootconfig' --board '' \
    --dtb $W/dt_table.dtb --vendor_ramdisk $W/ramdisk.gz --vendor_bootconfig $W/bootconfig \
    --vendor_boot "$OUT"
echo ">>> $OUT: $(stat -c%s "$OUT") Bytes"

if [ -n "${VERGLEICH:-}" ]; then
    python3 "$MKB/unpack_bootimg.py" --boot_img "$VERGLEICH" --out $W/alt > $W/alt.txt
    python3 "$MKB/unpack_bootimg.py" --boot_img "$OUT" --out $W/neu > $W/neu.txt
    echo ">>> Gegenprobe gegen $VERGLEICH:"
    diff $W/alt.txt $W/neu.txt | sed 's/^/    Kopf: /' || true
    cmp -s $W/alt/bootconfig $W/neu/bootconfig && echo "    bootconfig: gleich" || echo "    !! bootconfig: VERSCHIEDEN"
    # Geraetebaum nach Inhalt vergleichen (Tabellenkopf 64 Bytes weg, dann dtc): die Dateigroesse darf abweichen,
    # weil der alte Baum Leerraum fuer fdtput mitschleppt (306038 statt ~44000 Bytes)
    for s in alt neu; do tail -c +65 $W/$s/dtb > $W/$s.dtb; dtc -q -I dtb -O dts -s $W/$s.dtb > $W/$s.dts; done
    cmp -s $W/alt.dts $W/neu.dts && echo "    Geraetebaum-Inhalt: gleich ($(stat -c%s $W/alt.dtb) / $(stat -c%s $W/neu.dtb) Bytes)" \
        || echo "    !! Geraetebaum-Inhalt: VERSCHIEDEN ($(diff $W/alt.dts $W/neu.dts | wc -l) Zeilen)"
    for s in alt neu; do python3 -c "import gzip,sys;d=gzip.decompress(open(sys.argv[1],'rb').read());print(sorted(set(p.split(b'\0')[0] for p in d.split(b'070701')[1:] for p in [p[104:]])))" $W/$s/vendor_ramdisk00 > $W/$s.rd; done
    cmp -s $W/alt.rd $W/neu.rd && echo "    Ramdisk-Inhalt: gleich ($(cat $W/neu.rd))" || echo "    !! Ramdisk-Inhalt: VERSCHIEDEN ($(cat $W/alt.rd) / $(cat $W/neu.rd))"
fi
rm -rf $W
