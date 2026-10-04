#!/bin/bash
# Kernel + vendor_boot per SSH auf Slot b des Redmi A5 schreiben,
# Slot b aktiv schalten (7 Versuche, wie test.sh) und neu starten.
# Faellt der neue Kernel 7x durch, startet der Bootloader wieder Slot a.
# Nutzung: ~/redmi-build/deploy.sh [vendor_boot-image]
set -e
cd ~/redmi-build
VB=${1:-vendor_boot_pmic0_ok.img}   # Standard seit 04.10. (cpufreq an, grosse Kerne Regler 0); Rettung: vendor_boot_usb2.img

for f in boot_mainline.img "$VB" misc_b.img; do
    [ -f "$f" ] || { echo "FEHLER: $f fehlt"; exit 1; }
done

# cpufreq-Modul muss zum Kernel passen (sonst laedt redmi-cpufreq.service es nicht)
KO=~/ums9230-linux/drivers/cpufreq/sprd-cpufreq-v2.ko
kver=$(strings boot_mainline.img | grep -m1 -o "Linux version [^ ]*" | cut -d' ' -f3)
if [ -f "$KO" ]; then
    mver=$(modinfo -F vermagic "$KO" | cut -d' ' -f1)
    if [ "$mver" = "$kver" ]; then
        echo ">>> cpufreq-Modul passt ($mver) - wird mit installiert"
        scp -q "$KO" root@192.168.7.2:/tmp/ && \
        ssh root@192.168.7.2 "install -m644 /tmp/sprd-cpufreq-v2.ko /usr/local/lib/redmi/sprd-cpufreq-v2.ko"
    else
        echo "!! cpufreq-Modul ($mver) passt NICHT zum Kernel ($kver) - erst 'make modules', sonst kein cpufreq"
    fi
fi

echo ">>> kopiere boot_mainline.img + $VB + misc_b.img"
scp -q boot_mainline.img "$VB" misc_b.img root@192.168.7.2:/tmp/
ssh root@192.168.7.2 "dd if=/tmp/boot_mainline.img of=/dev/disk/by-partlabel/boot_b bs=1M status=none && \
dd if=/tmp/$(basename "$VB") of=/dev/disk/by-partlabel/vendor_boot_b bs=1M status=none && \
dd if=/tmp/misc_b.img of=/dev/disk/by-partlabel/misc bs=1M status=none && \
sync && echo '>>> geschrieben, Slot b aktiv' && (sleep 1; reboot) &" || true
echo ">>> Handy startet neu (Slot b) - danach: ~/redmi-build/net.sh"
