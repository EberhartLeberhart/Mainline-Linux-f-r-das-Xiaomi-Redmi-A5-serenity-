#!/bin/sh
# redmi-cpufreq.sh - cpufreq SICHER laden (laeuft beim Start ueber redmi-cpufreq.service).
# Umgehung, keine Loesung: Nach einem Taktwechsel haengt das Handy beim Aufwachen der
# GROSSEN Kerne (6,7) aus dem Tiefschlaf "cpu-pd-big" (Tests vom 04.10.). Deshalb wird dieser
# Tiefschlaf zuerst gesperrt - und der Treiber nur geladen, wenn die Sperre nachweislich sitzt.
# Die kleinen Kerne (0-5) duerfen weiter tief schlafen.
# Der Treiber darf NIE automatisch geladen werden: /etc/modprobe.d/redmi-dvfs.conf (blacklist).
KO=/usr/local/lib/redmi/sprd-cpufreq-v2.ko
k() { echo "REDMI: $*" > /dev/kmsg; echo "$*"; }

for c in 6 7; do
    f=/sys/devices/system/cpu/cpu$c/cpuidle/state1
    [ "$(cat $f/name 2>/dev/null)" = "cpu-pd-big" ] || { k "cpufreq NICHT geladen: cpu$c state1 ist nicht cpu-pd-big"; exit 1; }
    echo 1 > $f/disable
    [ "$(cat $f/disable)" = "1" ] || { k "cpufreq NICHT geladen: Sperre fuer cpu$c greift nicht"; exit 1; }
done
lsmod | grep -q '^sprd_cpufreq_v2' && { k "cpufreq war schon geladen"; exit 0; }
insmod $KO || { k "cpufreq NICHT geladen: insmod fehlgeschlagen"; exit 1; }
k "cpufreq geladen, Tiefschlaf cpu6/7 gesperrt ($(ls -d /sys/devices/system/cpu/cpufreq/policy* | xargs -n1 basename | tr '\n' ' '))"
