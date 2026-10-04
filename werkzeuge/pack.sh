#!/bin/bash
# pack.sh - boot_mainline.img aus dem frisch gebauten Kernel packen und SELBST pruefen,
# ob wirklich der aktuelle Kernel drinsteckt. Nutzung: ~/redmi-build/pack.sh
set -e
K=~/ums9230-linux
IMG=$K/arch/arm64/boot/Image
cd ~/redmi-build

[ -f "$IMG" ] || { echo "FEHLER: $IMG fehlt - Kernel bauen!"; exit 1; }
# Image muss NACH der letzten Konfigurationsaenderung entstanden sein (sonst ist der Bau gescheitert)
[ "$IMG" -nt "$K/.config" ] || { echo "FEHLER: Image ($(date -r "$IMG" +%H:%M)) ist aelter als .config ($(date -r "$K/.config" +%H:%M)) - Bau gescheitert oder nicht gelaufen!"; exit 1; }
head=$(git -C $K rev-parse --short=12 HEAD)
want=$(strings "$IMG" | grep -m1 -o "Linux version [^ ]*")
echo ">>> Image:  $(date -r "$IMG" '+%F %H:%M')  $want"
echo ">>> Git:    $head  $(git -C $K log -1 --format=%s)"
case "$want" in *g${head}*) ;; *) echo "FEHLER: Image passt nicht zum Git-Stand - neu bauen!"; exit 1;; esac

python3 $HOME/mkbootimg-aosp/mkbootimg.py --header_version 4 --kernel "$IMG" --cmdline '' -o boot_mainline.img

got=$(strings boot_mainline.img | grep -m1 -o "Linux version [^ ]*")
[ "$got" = "$want" ] || { echo "FEHLER: im boot_mainline.img steckt '$got'"; exit 1; }
[ "$(stat -c%s boot_mainline.img)" -gt "$(stat -c%s "$IMG")" ] || { echo "FEHLER: boot_mainline.img kleiner als Image?"; exit 1; }
echo ">>> boot_mainline.img: $(date -r boot_mainline.img '+%F %H:%M'), $(stat -c%s boot_mainline.img) Bytes, md5 $(md5sum boot_mainline.img | cut -c1-12)"
echo ">>> OK - bereit fuer ~/redmi-build/deploy.sh"
