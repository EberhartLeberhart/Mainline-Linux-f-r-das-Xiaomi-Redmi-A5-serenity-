#!/bin/bash
# Kernel + vendor_boot per SSH auf Slot b des Redmi A5 schreiben,
# Slot b aktiv schalten (7 Versuche, wie test.sh) und neu starten.
# Faellt der neue Kernel 7x durch, startet der Bootloader wieder Slot a.
# Nutzung: ~/redmi-build/deploy.sh [vendor_boot-image]
set -e
cd ~/redmi-build
VB=${1:-vendor_boot_panelk_ok.img}   # zuletzt belegter Stand (31.10., wie folger_ok + Panel in Kernel-Hand: Reset/avdd/avee
                                  # an den Panel-Treiber, braucht Patch 0030;
                                  # davor folger_ok (29.10.): wie sd_ok + Touch als Panel-Follower, Patch 0029;
                                  # davor sd_ok (27.10.): wie opp850mv_ok + microSD-Kartenleser;
                                  # davor opp850mv_ok: wie opp_ok, GPU-Spannung 0,85 V statt 1,00 V; davor opp_ok:
                                  # Display + Touch + CPU + GPU mit Taktstufen 384-850 MHz +
                                  # PMIC-Regler; braucht Patch 0026 + CONFIG_REGULATOR_SC2730=y; davor vendor_boot_regler_ok.img); vendor_boot_ok.img (04.10.) hat
                                  # noch kein Display/Touch im Geraetebaum (23.10. versehentlich benutzt). Rettung: vendor_boot_usb2.img

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

# 25.10.: ALLE Kernel-Module (=m) automatisch mitliefern - vorher einzeln von Hand (/root, /lib/modules), nach jedem
# Kernel-Commit fehlten sie (neue Version = neuer Modulordner). make modules_install legt sie genau so ab, wie der Kernel
# sie sucht (/lib/modules/<Version>/...); danach depmod auf dem Handy. Bricht VOR dem Schreiben ab, wenn etwas nicht passt.
# Ausschalten: MODULE=0 ~/redmi-build/deploy.sh
K=~/ums9230-linux
MODDIR=~/redmi-build/module
if [ "${MODULE:-1}" = 1 ]; then
    rm -rf "$MODDIR"
    make -s -C "$K" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- INSTALL_MOD_PATH="$MODDIR" INSTALL_MOD_STRIP=1 \
        modules_install > /dev/null || { echo "FEHLER: modules_install - erst 'make modules' im Kernelbaum"; exit 1; }
    [ -d "$MODDIR/lib/modules/$kver" ] || { echo "FEHLER: Module sind fuer $(ls "$MODDIR/lib/modules"), Kernel ist $kver - neu bauen"; exit 1; }
    rm -f "$MODDIR/lib/modules/$kver/build" "$MODDIR/lib/modules/$kver/source"   # Verweise auf den PC-Quellbaum
    echo ">>> Module fuer $kver: $(find "$MODDIR/lib/modules/$kver" -name '*.ko*' | wc -l) Stueck, $(du -sh "$MODDIR/lib/modules/$kver" | cut -f1)"
    tar -C "$MODDIR/lib/modules" -czf - "$kver" | ssh root@192.168.7.2 "set -e
        rm -rf /lib/modules/$kver.neu && mkdir /lib/modules/$kver.neu
        tar -xzf - -C /lib/modules/$kver.neu
        rm -rf /lib/modules/$kver && mv /lib/modules/$kver.neu/$kver /lib/modules/$kver && rmdir /lib/modules/$kver.neu
        depmod -a $kver
        # Aufraeumen: NUR eigene Kernel-Ordner (Name endet auf -g<12 Hex>[-dirty], nicht z. B. -generic) bis auf
        # neuen, laufenden und einen weiteren loeschen
        laeuft=\$(uname -r); weitere=0
        for v in \$(ls -t /lib/modules/ | grep -E -- '-g[0-9a-f]{12}(-dirty)?\$'); do
            d=/lib/modules/\$v
            [ \"\$v\" = \"$kver\" ] || [ \"\$v\" = \"\$laeuft\" ] && continue
            weitere=\$((weitere+1)); [ \$weitere -le 1 ] && continue
            echo \">>> alter Modulordner geloescht: \$v\"; rm -rf \"\$d\"
        done
        echo \">>> Module installiert: /lib/modules/$kver\""
fi

echo ">>> kopiere boot_mainline.img + $VB + misc_b.img"
scp -q boot_mainline.img "$VB" misc_b.img root@192.168.7.2:/tmp/
ssh root@192.168.7.2 "dd if=/tmp/boot_mainline.img of=/dev/disk/by-partlabel/boot_b bs=1M status=none && \
dd if=/tmp/$(basename "$VB") of=/dev/disk/by-partlabel/vendor_boot_b bs=1M status=none && \
dd if=/tmp/misc_b.img of=/dev/disk/by-partlabel/misc bs=1M status=none && \
sync && echo '>>> geschrieben, Slot b aktiv' && (sleep 1; reboot) &" || true
echo ">>> Handy startet neu (Slot b) - danach: ~/redmi-build/net.sh"
