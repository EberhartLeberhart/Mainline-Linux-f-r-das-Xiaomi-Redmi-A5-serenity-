#!/bin/bash
# redmi-wcnstrom.sh - Was kostet der WCN-Kern (BT/WLAN) an Strom? Misst den Akkustrom abwechselnd mit
# WCN aus und WCN an (Kern laeuft und schlaeft, wie nach redmi-wcntest.sh 3).
# Laeuft ohne SSH weiter (systemd-run), Protokoll /root/wcnstrom.log.
#
# Start (Handy am PC):  systemd-run --unit=wcnstrom /usr/local/sbin/redmi-wcnstrom.sh
# dann Kabel ABZIEHEN (gemessen wird nur ohne Kabel), nach ~5 min wieder einstecken, net.sh,
# dann: cat /root/wcnstrom.log
#
# Weg wie redmi-paneltest.sh (FGU current_now, Mittel aus 10 Werten in 20 s). Ablauf:
#   aus -> an -> aus -> an -> aus   (je 20 s Messung, vorher 10 s Beruhigung)
# Der Lauscher wird vorher entladen und nicht geladen, damit sein 500-ms-Nachsehen nicht mitmisst.
set -u
LOG=/root/wcnstrom.log
PS=/sys/class/power_supply/sc27xx-fgu
VB=$(ls /sys/bus/iio/devices/iio:device*/in_voltage14_input 2>/dev/null | head -1)
log(){ echo "$(date +%T) $*" >> $LOG; echo "REDMI: wcnstrom $*" > /dev/kmsg 2>/dev/null; }
strom(){ local s=0 i; for i in 1 2 3 4 5 6 7 8 9 10; do s=$(( s + $(cat $PS/current_now) / 1000 )); sleep 2; done; echo $(( s / 10 )); }
messe(){ sleep 10; log "$1: $(strom) mA (Mittel aus 10 Werten, 20 s)"; }
wcn_aus(){
	if [ -w /sys/devices/platform/wcntest/aus ]; then
		echo 1 > /sys/devices/platform/wcntest/aus 2>/dev/null || log "Abschalten meldet Fehler"
	fi
	lsmod | grep -q '^wcn_starttest' && rmmod wcn_starttest
	log "WCN aus"
}
wcn_an(){
	LAUSCHER=/nicht/laden redmi-wcntest.sh 3 > /dev/null 2>&1
	if grep -q 'WCN-Kern laeuft' /sys/devices/platform/wcntest/zustand 2>/dev/null; then
		log "WCN an (0xF0F0F0FF)"
	else
		log "WCN-Start FEHLGESCHLAGEN"; return 1
	fi
}

: > $LOG
[ -n "$VB" ] && [ -r $PS/current_now ] || { log "ABBRUCH: FGU oder USB-Spannung nicht lesbar"; exit 1; }
lsmod | grep -q '^wcn_lauscher' && rmmod wcn_lauscher
if lsmod | grep -q '^wcn_starttest'; then
	wcn_aus
	log "vorher geladenen Treiber abgeschaltet"
fi
log "gestartet - bitte Kabel abziehen (warte bis zu 3 min)"
n=0; while [ "$(cat $VB)" -gt 2000 ] && [ $n -lt 90 ]; do sleep 2; n=$((n+1)); done
[ "$(cat $VB)" -gt 2000 ] && { log "ABBRUCH: Kabel steckt noch"; exit 1; }
log "Kabel ab"

messe "WCN aus (1)"
wcn_an && messe "WCN an, Kern schlaeft (1)"
wcn_aus; messe "WCN aus (2)"
wcn_an && messe "WCN an, Kern schlaeft (2)"
wcn_aus; messe "WCN aus (3)"
log "fertig - Kabel wieder einstecken. Negativ = Entladen; je kleiner der Betrag, desto weniger Verbrauch"
