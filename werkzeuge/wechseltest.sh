#!/bin/bash
# wechseltest.sh - trennt "Takt-Stufe zu hoch" von "Takt-WECHSEL ist gefaehrlich".
# Nutzt den userspace-Regler: der Takt aendert sich NUR, wenn das Skript es sagt.
#
#   ~/redmi-build/wechseltest.sh fest   <MHz_klein> <MHz_gross> <sek>   feste Stufe, Volllast, kein Wechsel
#   ~/redmi-build/wechseltest.sh wechsel <policy> <MHz_a> <MHz_b> <anzahl>  im Leerlauf hin und her (eine Gruppe)
#   ~/redmi-build/wechseltest.sh beide  <anzahl>                         beide Gruppen GLEICHZEITIG hin und her
# Jeder Schritt geht auch ins Kernel-Log (bleibt bei Haenger auf dem Display).
# Protokoll: ~/redmi-build/wechseltest_<datum>.txt
OUT=~/redmi-build/wechseltest_$(date +%F_%H%M).txt
ping -c1 -W1 192.168.7.2 >/dev/null 2>&1 || ~/redmi-build/net.sh || exit 1
echo ">>> Protokoll: $OUT"
ssh root@192.168.7.2 "set -- $*; bash -s \"\$@\"" <<'PHONE' | tee "$OUT"
log() { echo "$(date +%T) $*"; echo "REDMI-WT $*" > /dev/kmsg; }
C=/sys/devices/system/cpu/cpufreq
lsmod | grep -q sprd_cpufreq_v2 || insmod /root/sprd-cpufreq-v2.ko || { log "insmod fehlgeschlagen"; exit 1; }
sleep 0.2
P0=$C/policy0; P1=$C/policy6
[ -d $P0 ] && [ -d $P1 ] || { log "policy0/policy6 fehlen: $(ls $C)"; exit 1; }
grep -qw userspace $P0/scaling_available_governors || { log "userspace-Regler nicht im Kernel: $(cat $P0/scaling_available_governors)"; exit 1; }
# sofort auf feste, niedrigste Stufe - ab hier wechselt nichts mehr von selbst
for p in $P0 $P1; do
    echo userspace > $p/scaling_governor
    min=$(tr ' ' '\n' < $p/scaling_available_frequencies | grep . | sort -n | head -1)
    echo $min > $p/scaling_setspeed
done
log "Start: $(basename $P0)=$(( $(cat $P0/scaling_cur_freq)/1000 )) $(basename $P1)=$(( $(cat $P1/scaling_cur_freq)/1000 )) MHz, Regler userspace"
temp() { echo "$(( $(cat /sys/class/thermal/thermal_zone0/temp)/1000 ))C"; }
case "$1" in
fest)
    echo $(( $2*1000 )) > $P0/scaling_setspeed; echo $(( $3*1000 )) > $P1/scaling_setspeed
    log "fest: klein=$(( $(cat $P0/scaling_cur_freq)/1000 )) gross=$(( $(cat $P1/scaling_cur_freq)/1000 )) MHz, Volllast $4 s"
    for i in 1 2 3 4 5 6 7 8; do timeout $4 sh -c 'while :; do :; done' & done
    t=0; while [ $t -lt $4 ]; do sleep 10; t=$((t+10)); log "  ${t}s: klein=$(( $(cat $P0/scaling_cur_freq)/1000 )) gross=$(( $(cat $P1/scaling_cur_freq)/1000 )) Temp $(temp)"; done
    wait; log "fest: Last vorbei, noch 20 s Leerlauf"; sleep 20; log "fest: BESTANDEN" ;;
wechsel)
    p=$C/$2
    for n in $(seq 1 $5); do
        echo $(( $3*1000 )) > $p/scaling_setspeed; sleep 0.2
        echo $(( $4*1000 )) > $p/scaling_setspeed; sleep 0.2
        [ $((n % 10)) = 0 ] && log "  $2: $n Wechselpaare $3<->$4 MHz ok"
    done
    log "wechsel: BESTANDEN ($5 Paare)" ;;
beide)
    f0=$(tr ' ' '\n' < $P0/scaling_available_frequencies | grep . | sort -n | head -2 | tr '\n' ' ')
    f1=$(tr ' ' '\n' < $P1/scaling_available_frequencies | grep . | sort -n | head -2 | tr '\n' ' ')
    log "beide: klein $f0 / gross $f1 (kHz) gleichzeitig"
    set -- $2 $f0 $f1   # $1=anzahl $2/$3 klein $4/$5 gross
    for n in $(seq 1 $1); do
        echo $2 > $P0/scaling_setspeed & echo $4 > $P1/scaling_setspeed & wait; sleep 0.2
        echo $3 > $P0/scaling_setspeed & echo $5 > $P1/scaling_setspeed & wait; sleep 0.2
        [ $((n % 10)) = 0 ] && log "  beide: $n Paare ok"
    done
    log "beide: BESTANDEN ($1 Paare)" ;;
*) log "unbekannter Modus: $1" ;;
esac
PHONE
