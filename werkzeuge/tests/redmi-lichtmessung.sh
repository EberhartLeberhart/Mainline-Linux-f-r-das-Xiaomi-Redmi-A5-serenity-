#!/bin/bash
# redmi-lichtmessung.sh - Akkustrom: Panel-Schlaf alter Weg (DCS) gegen neuer Weg (Display-Treiber).
# Start (Handy am PC):  systemd-run --unit=lichtmessung /root/lichtmessung.sh
# dann Kabel ABZIEHEN (gemessen wird nur ohne Kabel), nach ~3 min wieder einstecken; Protokoll /root/lichtmessung.log
# Beleg 29.10.2026: an 130 mA, Schlaf DCS 60 mA, Schlaf Treiber 43 mA.
set -u
L=/usr/local/sbin/redmi-licht.sh
LOG=/root/lichtmessung.log
PS=/sys/class/power_supply/sc27xx-fgu
VB=$(ls /sys/bus/iio/devices/iio:device*/in_voltage14_input 2>/dev/null | head -1)
log(){ echo "$(date +%T) $*" >> $LOG; echo "REDMI: Messung $*" > /dev/kmsg; }
strom(){ local s=0 i; for i in 1 2 3 4 5 6 7 8 9 10; do s=$(( s + $(cat $PS/current_now) / 1000 )); sleep 2; done; echo $(( s / 10 )); }
messe(){ log "$1: $(strom) mA (Mittel aus 10 Werten, 20 s)"; }
{ cat /etc/redmi/licht.conf; echo WEG=dcs; } > /run/licht-dcs.conf
: > $LOG
log "gestartet - bitte Kabel abziehen (warte bis zu 3 min)"
n=0; while [ "$(cat $VB)" -gt 2000 ] && [ $n -lt 90 ]; do sleep 2; n=$((n+1)); done
[ "$(cat $VB)" -gt 2000 ] && { log "ABBRUCH: Kabel steckt noch"; exit 1; }
log "Kabel ab, warte 10 s"; sleep 10
$L an; sleep 5;                              messe "an, Licht 500"
CONF=/run/licht-dcs.conf $L aus; sleep 5;    messe "Schlaf ALT (DCS, DSI+DPU laufen)"
CONF=/run/licht-dcs.conf $L an; sleep 5;     messe "wieder an"
$L aus; sleep 5;                             messe "Schlaf NEU (Treiber, DSI+DPU aus)"
$L an; sleep 5;                              messe "wieder an"
log "fertig - Kabel wieder einstecken"
