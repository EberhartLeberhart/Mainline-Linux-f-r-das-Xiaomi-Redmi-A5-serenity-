#!/bin/bash
# redmi-paneltest.sh - Panel schlafen legen und wecken (DCS 0x28/0x10 bzw. 0x11/0x29) und dabei
# den Akkustrom messen. Laeuft ohne SSH weiter (systemd-run), Protokoll /root/paneltest.log.
#
# Start (Handy am PC):  systemd-run --unit=paneltest /usr/local/sbin/redmi-paneltest.sh
# dann Kabel ABZIEHEN (gemessen wird nur ohne Kabel), nach ~3 min wieder einstecken.
#
# Weg wie redmi-licht.sh (belegt 06.10.): DSI 0x31100000 laeuft vom Bootloader, Befehle ueber
# GEN_HDR 0x6C / GEN_PLD 0x70. Kurzer DCS-Befehl ohne Parameter = Datentyp 0x05, Befehl im 2. Byte.
# Sleep-Out braucht laut MIPI-DCS 120 ms, bevor Display-On kommt.
# Wacht das Panel nicht auf: Neustart stellt alles wieder her (Bootloader initialisiert neu).
set -u
D=0x31100000
LOG=/root/paneltest.log
PS=/sys/class/power_supply/sc27xx-fgu
VB=$(ls /sys/bus/iio/devices/iio:device*/in_voltage14_input 2>/dev/null | head -1)
log(){ echo "$(date +%T) $*" >> $LOG; echo "REDMI: Panel $*" > /dev/kmsg 2>/dev/null; }
rd(){ busybox devmem $((D + $1)); }
wr(){ busybox devmem $((D + $1)) 32 $2; }
fifo_leer(){ local n=0; while [ $(( $(rd 0x98) & 0x28 )) -ne $((0x28)) ]; do n=$((n+1)); [ $n -gt 50 ] && return 1; sleep 0.02; done; }
dcs(){ fifo_leer || { log "DSI-Puffer voll - $1 nicht gesendet"; return 1; }; wr 0x6c $(( 0x05 | ($1 << 8) )); }
licht(){ fifo_leer && wr 0x70 $(( 0x51 | (($1 >> 8) << 8) | (($1 & 0xff) << 16) )) && wr 0x6c 0x339; }
strom(){ local s=0 i; for i in 1 2 3 4 5 6 7 8 9 10; do s=$(( s + $(cat $PS/current_now) / 1000 )); sleep 2; done; echo $(( s / 10 )); }
messe(){ log "$1: $(strom) mA (Mittel aus 10 Werten, 20 s)"; }

: > $LOG
[ $(( $(rd 0x9c) & 0x2 )) -ne 0 ] || { log "ABBRUCH: DSI nicht bereit"; exit 1; }
log "gestartet - bitte Kabel abziehen (warte bis zu 3 min)"
n=0; while [ "$(cat $VB)" -gt 2000 ] && [ $n -lt 90 ]; do sleep 2; n=$((n+1)); done
[ "$(cat $VB)" -gt 2000 ] && { log "ABBRUCH: Kabel steckt noch"; exit 1; }
log "Kabel ab, warte 10 s bis der Strom sich beruhigt"; sleep 10

licht 500; sleep 3;              messe "Licht 500"
licht 0;   sleep 3;              messe "Licht 0"
dcs 0x28; sleep 0.05; dcs 0x10;  sleep 3
log "Display-Off (0x28) + Sleep-In (0x10) gesendet"
                                 messe "Panel schlaeft"
dcs 0x11; sleep 0.15; dcs 0x29;  sleep 1
log "Sleep-Out (0x11) + Display-On (0x29) gesendet"
licht 500; sleep 3;              messe "Panel wach, Licht 500"
licht 0
log "fertig - Licht wieder 0. Bitte melden: war das Bild nach dem Wecken wieder da (Ziege/Text)?"
