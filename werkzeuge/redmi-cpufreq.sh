#!/bin/sh
# redmi-cpufreq.sh - cpufreq laden (laeuft beim Start ueber redmi-cpufreq.service).
#
# Ursache der Haenger (04.10., belegt): Der Device-Tree nannte der Firmware fuer die GROSSEN
# Kerne den externen Spannungsregler (sprd,pmic-type = 1). Dieses Handy hat keinen
# (Bootloader: power.from.extern=0, Xiaomi-DT: pmic-type-v2 = 0). Folge: Aufwachen der grossen
# Kerne aus dem Tiefschlaf nach einem Taktwechsel -> Haenger.
# Richtig ist pmic-type = 0 (vendor_boot_pmic0). Dann laeuft cpufreq OHNE Einschraenkung.
#
# Sicherheitsnetz: Steht im laufenden Device-Tree noch der alte Wert 1 (z.B. Rettungs-vendor_boot),
# wird wie frueher der Tiefschlaf der grossen Kerne gesperrt, bevor der Treiber laedt.
# Der Treiber darf NIE automatisch geladen werden: /etc/modprobe.d/redmi-dvfs.conf (blacklist).
KO=/usr/local/lib/redmi/sprd-cpufreq-v2.ko
DT=/proc/device-tree/cpufreq/cluster@1/sprd,pmic-type
k() { echo "REDMI: $*" > /dev/kmsg; echo "$*"; }

lsmod | grep -q '^sprd_cpufreq_v2' && { k "cpufreq war schon geladen"; exit 0; }
typ=$(od -An -tu4 --endian=big $DT 2>/dev/null | tr -d ' ')
case "$typ" in
0)  modus="Regler 0 (richtig), keine Tiefschlaf-Sperre" ;;
1)  for c in 6 7; do
        f=/sys/devices/system/cpu/cpu$c/cpuidle/state1
        echo 1 > $f/disable
        [ "$(cat $f/disable)" = "1" ] || { k "cpufreq NICHT geladen: alter Device-Tree und Sperre fuer cpu$c greift nicht"; exit 1; }
    done
    modus="ALTER Device-Tree (Regler 1) - Tiefschlaf cpu6/7 gesperrt" ;;
*)  k "cpufreq NICHT geladen: pmic-type der grossen Kerne unbekannt ('$typ')"; exit 1 ;;
esac
insmod $KO || { k "cpufreq NICHT geladen: insmod fehlgeschlagen"; exit 1; }
k "cpufreq geladen, $modus ($(ls -d /sys/devices/system/cpu/cpufreq/policy* | xargs -n1 basename | tr '\n' ' '))"
