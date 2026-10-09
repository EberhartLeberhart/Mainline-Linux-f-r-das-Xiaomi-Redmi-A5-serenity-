#!/bin/bash
# lan-dauertest.sh - LAN-Dauertest fuer das Redmi A5 am USB-LAN-Adapter (RTL8153, Patch 0017). Laeuft auf dem PC.
#
# Aufbau wie 08.10.: Handy im Hub mit Einspeisung, LAN-Kabel des Adapters direkt am PC,
# NetworkManager-Verbindung "redmi-lan" (geteilt, 10.42.0.1) -> Handy 10.42.0.27.
#
#   Start:   systemd-inhibit --what=sleep:idle bash werkzeuge/lan-dauertest.sh
#            (oder mit nohup ... & im Hintergrund; systemd-inhibit verhindert, dass der PC einschlaeft)
#   Ende:    Strg+C - die Zusammenfassung steht am Ende des Protokolls
#
# Jede Runde (RUNDE s): Pings 56/1000/1472 Byte (je 5), Zustand vom Handy (Laufzeit, boot_id, LAN-Fehler
# im dmesg, Akku, Strom, Temperatur, CPU-Takt). Alle UEBERTRAGUNG s: 50 MB hin und zurueck ueber SSH.
# Protokoll: ~/redmi-lan-dauertest_<Datum>.log und .csv
set -u
HANDY=${HANDY:-10.42.0.27}
RUNDE=${RUNDE:-60}
UEBERTRAGUNG=${UEBERTRAGUNG:-900}
MB=${MB:-50}
NAME=~/redmi-lan-dauertest_$(date +%Y-%m-%d_%H%M)
LOG=$NAME.log
CSV=$NAME.csv
SSH="ssh -o ConnectTimeout=5 -o BatchMode=yes -o ServerAliveInterval=5 -o ServerAliveCountMax=3 root@$HANDY"

log(){ echo "$(date '+%F %T') $*" | tee -a "$LOG"; }

ping_quote(){	# Groesse -> "erhalten/5"
	local n
	n=$(ping -c 5 -i 0.3 -W 2 -s "$1" "$HANDY" 2>/dev/null | sed -n 's/.* \([0-9]*\) received.*/\1/p')
	echo "${n:-0}/5"
}

zustand(){	# eine Zeile vom Handy: laufzeit;boot_id;lanfehler;akku;strom_mA;temp_mC;takt_klein;takt_gross
	$SSH 'echo "$(cut -d" " -f1 /proc/uptime);$(cat /proc/sys/kernel/random/boot_id);$(dmesg | grep -c -E "Tx status|transmit queue|Tx timeout|reset high-speed|NETDEV WATCHDOG");$(cat /sys/class/power_supply/sc27xx-fgu/capacity);$(( $(cat /sys/class/power_supply/sc27xx-fgu/current_now) / 1000 ));$(cat /sys/class/thermal/thermal_zone0/temp);$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq 2>/dev/null || echo -);$(cat /sys/devices/system/cpu/cpu6/cpufreq/scaling_cur_freq 2>/dev/null || echo -)"' 2>/dev/null
}

uebertragen(){	# gibt "hin_MBs;zurueck_MBs" aus, "-" bei Fehler
	local t0 t1 hin zur
	t0=$(date +%s.%N)
	if dd if=/dev/zero bs=1M count="$MB" status=none | $SSH 'cat > /dev/null' 2>/dev/null; then
		t1=$(date +%s.%N); hin=$(echo "$MB / ($t1 - $t0)" | bc -l | xargs printf '%.1f')
	else hin=-; fi
	t0=$(date +%s.%N)
	if $SSH "dd if=/dev/zero bs=1M count=$MB status=none" > /dev/null 2>&1; then
		t1=$(date +%s.%N); zur=$(echo "$MB / ($t1 - $t0)" | bc -l | xargs printf '%.1f')
	else zur=-; fi
	echo "$hin;$zur"
}

runden=0; ausfaelle=0; neustarts=0; boot_alt=""; fehler_start=""; letzte_uebertragung=0
ende(){
	log "==== Zusammenfassung: $runden Runden, $ausfaelle ohne Antwort, $neustarts Handy-Neustart(e) erkannt, LAN-Fehlermeldungen seit Beginn: ${fehler_jetzt:-?} (Start: ${fehler_start:-?}) ===="
	exit 0
}
trap ende INT TERM

command -v bc >/dev/null || { echo "bc fehlt: sudo apt install bc"; exit 1; }
echo "zeit;p56;p1000;p1472;laufzeit_s;boot_id;lanfehler;akku_pct;strom_mA;temp_mC;takt_klein;takt_gross;hin_MBs;zurueck_MBs" > "$CSV"
log "==== LAN-Dauertest gestartet: Handy $HANDY, Runde ${RUNDE}s, alle ${UEBERTRAGUNG}s ${MB} MB hin/zurueck ===="
log "Protokoll: $LOG  Tabelle: $CSV"

while true; do
	runden=$((runden + 1))
	p56=$(ping_quote 56); p1000=$(ping_quote 1000); p1472=$(ping_quote 1472)
	z=$(zustand)
	ueb=";"
	if [ -n "$z" ] && [ $(( $(date +%s) - letzte_uebertragung )) -ge "$UEBERTRAGUNG" ]; then
		ueb=$(uebertragen); letzte_uebertragung=$(date +%s)
	fi
	if [ -z "$z" ]; then
		ausfaelle=$((ausfaelle + 1))
		log "Runde $runden: KEINE ANTWORT per SSH (Pings 56:$p56 1000:$p1000 1472:$p1472)"
		echo "$(date '+%F %T');$p56;$p1000;$p1472;;;;;;;;;;" >> "$CSV"
	else
		IFS=';' read -r lauf boot fehler akku strom temp tk tg <<< "$z"
		fehler_jetzt=$fehler
		[ -z "$fehler_start" ] && fehler_start=$fehler
		if [ -n "$boot_alt" ] && [ "$boot" != "$boot_alt" ]; then
			neustarts=$((neustarts + 1)); log "!!! Handy wurde neu gestartet (neue boot_id, Laufzeit ${lauf}s)"
			fehler_start=$fehler
		fi
		boot_alt=$boot
		echo "$(date '+%F %T');$p56;$p1000;$p1472;$z;$ueb" >> "$CSV"
		zeile="Runde $runden: Pings 56:$p56 1000:$p1000 1472:$p1472 | Laufzeit ${lauf%.*}s, LAN-Fehler $fehler, Akku $akku %, ${strom} mA, $((temp / 1000)) C"
		[ "$ueb" != ";" ] && zeile="$zeile | ${MB} MB hin ${ueb%;*} MB/s, zurueck ${ueb#*;} MB/s"
		log "$zeile"
	fi
	sleep "$RUNDE"
done
