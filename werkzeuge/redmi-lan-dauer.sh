#!/bin/bash
# redmi-lan-dauer.sh - LAN-Dauertest am Router, das Handy schreibt selbst mit. Laeuft ohne SSH weiter.
#
# Aufbau: Handy im Hub mit Einspeisung, USB-LAN-Adapter (RTL8153, Patch 0017) per Kabel am Router.
# Adresse per DHCP vom Router (20-lan.network). Der PC wird nicht gebraucht.
#
#   Start:   systemd-run --unit=landauer /usr/local/sbin/redmi-lan-dauer.sh
#   Stand:   tail /root/landauer.log          (ueber LAN-SSH oder spaeter per USB)
#   Ende:    systemctl stop landauer           -> Zusammenfassung am Ende des Protokolls
#
# Jede Runde (RUNDE s, Standard 60): Pings zum Router mit 56/1000/1472 Byte (je 5), Ping ins Internet
# (INTERNET, Standard 1.1.1.1), Namensaufloesung, Zustand (Adresse, Link, LAN-Fehler im dmesg, Akku,
# Strom, Temperatur). Alle LADEN s (Standard 1800) ein Download zum Messen der Geschwindigkeit, falls
# curl oder wget da ist. Jede Zeile wird sofort auf die eMMC geschrieben (sync), damit sie einen Haenger
# uebersteht. Ein Neustart des Handys zeigt sich als Luecke und neuer Kopf mit anderer boot_id.
set -u
RUNDE=${RUNDE:-60}
LADEN=${LADEN:-1800}
INTERNET=${INTERNET:-1.1.1.1}
NAME=${NAME:-example.com}
URL=${URL:-https://speed.cloudflare.com/__down?bytes=20000000}
LOG=/root/landauer.log
CSV=/root/landauer.csv
PS=/sys/class/power_supply/sc27xx-fgu

log(){ echo "$(date '+%F %T') $*" >> $LOG; sync $LOG 2>/dev/null; }

nic(){	# Netzwerkkarte des r8152
	local i
	for i in /sys/class/net/*; do
		[ "$(basename "$(readlink -f "$i/device/driver" 2>/dev/null)")" = r8152 ] && { basename "$i"; return; }
	done
}

quote(){	# Ziel Groesse [Schnittstelle] -> erhalten/5
	local n
	n=$(ping -c 5 -i 0.3 -W 2 -s "$2" ${3:+-I "$3"} "$1" 2>/dev/null | sed -n 's/.* \([0-9]*\) received.*/\1/p')
	echo "${n:-0}/5"
}

laden(){	# MB/s oder -
	local t0 t1 bytes
	t0=$(date +%s.%N)
	if command -v curl >/dev/null; then
		bytes=$(curl -s -o /dev/null -m 120 -w '%{size_download}' "$URL" 2>/dev/null)
	elif command -v wget >/dev/null; then
		bytes=$(wget -q -O - -T 120 "$URL" 2>/dev/null | wc -c)
	else
		echo "kein-curl"; return
	fi
	t1=$(date +%s.%N)
	[ "${bytes:-0}" -gt 0 ] || { echo "-"; return; }
	awk -v b="$bytes" -v t="$(echo "$t1 $t0" | awk '{print $1 - $2}')" 'BEGIN { printf "%.1f", b / 1048576 / t }'
}

runden=0; router_weg=0; internet_weg=0; fehler_start=""; fehler_jetzt=""; letztes_laden=0
ende(){
	log "==== Zusammenfassung: $runden Runden, Router nicht erreichbar in $router_weg, Internet nicht erreichbar in $internet_weg, LAN-Fehlermeldungen ${fehler_start:-?} -> ${fehler_jetzt:-?} ===="
	exit 0
}
trap ende INT TERM

[ -f $CSV ] || echo "zeit;boot_id;laufzeit_s;nic;adresse;link;router;r56;r1000;r1472;internet;dns;lanfehler;akku_pct;strom_mA;temp_mC;laden_MBs" > $CSV
log "==== LAN-Dauertest am Router gestartet, Kernel $(uname -r), boot_id $(cat /proc/sys/kernel/random/boot_id), Runde ${RUNDE}s ===="

while true; do
	runden=$((runden + 1))
	n=$(nic)
	if [ -z "$n" ]; then
		router_weg=$((router_weg + 1)); internet_weg=$((internet_weg + 1))
		log "Runde $runden: KEIN LAN-ADAPTER (r8152) gefunden"
		sleep "$RUNDE"; continue
	fi
	adr=$(ip -4 -br addr show "$n" | awk '{print $3}')
	link=$(cat /sys/class/net/$n/operstate 2>/dev/null)
	gw=$(ip -4 route show default dev "$n" 2>/dev/null | awk '{print $3; exit}')
	if [ -n "$gw" ]; then
		r56=$(quote "$gw" 56 "$n"); r1000=$(quote "$gw" 1000 "$n"); r1472=$(quote "$gw" 1472 "$n")
	else
		r56=-; r1000=-; r1472=-
	fi
	inet=$(quote "$INTERNET" 56 "$n")
	if getent hosts "$NAME" >/dev/null 2>&1; then dns=ok; else dns=FEHLT; fi
	fehler_jetzt=$(dmesg | grep -c -E "Tx status|transmit queue|Tx timeout|reset high-speed|NETDEV WATCHDOG")
	[ -z "$fehler_start" ] && fehler_start=$fehler_jetzt
	akku=$(cat $PS/capacity 2>/dev/null); strom=$(( $(cat $PS/current_now 2>/dev/null || echo 0) / 1000 ))
	temp=$(cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null)
	ld=""
	if [ $(( $(date +%s) - letztes_laden )) -ge "$LADEN" ] && [ "$inet" != "0/5" ]; then
		ld=$(laden); letztes_laden=$(date +%s)
	fi
	[ "$r56" = "0/5" ] || [ "$r56" = "-" ] && router_weg=$((router_weg + 1))
	[ "$inet" = "0/5" ] && internet_weg=$((internet_weg + 1))

	echo "$(date '+%F %T');$(cat /proc/sys/kernel/random/boot_id);$(cut -d' ' -f1 /proc/uptime);$n;$adr;$link;$gw;$r56;$r1000;$r1472;$inet;$dns;$fehler_jetzt;$akku;$strom;$temp;$ld" >> $CSV
	zeile="Runde $runden: $n $adr ($link) | Router ${gw:-?} 56:$r56 1000:$r1000 1472:$r1472 | Internet $inet, DNS $dns | LAN-Fehler $fehler_jetzt | Akku $akku %, $strom mA, $(( ${temp:-0} / 1000 )) C"
	[ -n "$ld" ] && zeile="$zeile | Download $ld MB/s"
	log "$zeile"
	sleep "$RUNDE"
done
