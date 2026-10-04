#!/bin/bash
# stufentest.sh - cpufreq Stufe fuer Stufe unter Volllast pruefen.
# Laedt den Treiber (falls noch nicht geladen), begrenzt SOFORT beide Gruppen auf die
# niedrigste Stufe und hebt dann je Gruppe eine Stufe nach der anderen an, jeweils mit
# Last auf allen 8 Kernen. Jeder Schritt wird auch ins Kernel-Log geschrieben (/dev/kmsg),
# damit er bei einem Haenger auf dem Display stehen bleibt.
#
#   ~/redmi-build/stufentest.sh [sekunden_pro_stufe]     (Standard 30)
# Protokoll: ~/redmi-build/stufentest_<datum>.txt
S=${1:-30}
OUT=~/redmi-build/stufentest_$(date +%F_%H%M).txt
ping -c1 -W1 192.168.7.2 >/dev/null 2>&1 || ~/redmi-build/net.sh || exit 1
echo ">>> Protokoll: $OUT"
ssh root@192.168.7.2 "S=$S bash -s" <<'PHONE' | tee "$OUT"
log() { echo "$(date +%T) $*"; echo "REDMI-STUFE $*" > /dev/kmsg; }
C=/sys/devices/system/cpu/cpufreq
lsmod | grep -q sprd_cpufreq_v2 || insmod /root/sprd-cpufreq-v2.ko || { log "insmod fehlgeschlagen"; exit 1; }
sleep 0.2
P=$(ls -d $C/policy* 2>/dev/null)
[ -n "$P" ] || { log "keine policy - Treiber nicht gebunden"; exit 1; }
# sofort alles auf die niedrigste Stufe
for p in $P; do
    [ -f $p/scaling_available_frequencies ] || { log "$(basename $p): keine Stufenliste"; exit 1; }
    min=$(tr ' ' '\n' < $p/scaling_available_frequencies | grep . | sort -n | head -1)
    echo $min > $p/scaling_max_freq
    log "$(basename $p) CPUs $(cat $p/related_cpus): begrenzt auf $((min/1000)) MHz"
done
temp() { echo "$(( $(cat /sys/class/thermal/thermal_zone0/temp)/1000 ))C"; }
for p in $P; do
    n=$(basename $p)
    for f in $(tr ' ' '\n' < $p/scaling_available_frequencies | grep . | sort -n); do
        echo $f > $p/scaling_max_freq
        log "$n: teste $((f/1000)) MHz unter Last fuer ${S}s (Temp $(temp))"
        for i in 1 2 3 4 5 6 7 8; do timeout $S sh -c 'while :; do :; done' & done
        t=0
        while [ $t -lt $S ]; do
            sleep 5; t=$((t+5))
            line=""; for q in $P; do line="$line $(basename $q)=$(( $(cat $q/scaling_cur_freq)/1000 ))"; done
            echo "   ${t}s:$line MHz  Temp $(temp)"
        done
        wait
        log "$n: $((f/1000)) MHz OK"
        sleep 2
    done
    # diese Gruppe wieder runter, bevor die naechste dran ist
    echo $(tr ' ' '\n' < $p/scaling_available_frequencies | grep . | sort -n | head -1) > $p/scaling_max_freq
done
log "FERTIG: alle Stufen unter Last bestanden"
PHONE
