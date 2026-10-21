#!/bin/bash
# redmi-wlantest.sh - WLAN-Testkette auf dem Redmi A5 starten und kurz zusammenfassen (laeuft auf dem PC)
#
# Startet den WCN-Kern mit dem Lauscher (alle Kanaele), schickt die WLAN-Befehle der Reihe nach und
# zeigt am Ende nur eine Uebersicht. Das komplette dmesg landet in ~/redmi-logs/ (MAC halb ausgeblendet).
# Voraussetzung: Handy frisch gestartet, wcn_lauscher.ko und wcn_starttest.ko in /root, Firmware,
# wifi_board_config.ini und wifimac.txt in /lib/firmware.
#
# Aufruf:  redmi-wlantest.sh                         -> sync_version download_ini get_info open scan close
#          redmi-wlantest.sh sync_version get_info   -> nur diese Befehle
#          scan = 2,4 GHz Kanal 1-13, scan5 = zusaetzlich 5 GHz Kanal 36-48
# Das Log enthaelt nach einem Scan die Netznamen (SSID) der Nachbarn: nicht ins Repo.

set -u
BEFEHLE=${*:-sync_version download_ini get_info open scan close}
PARAM="kanal=8 antworten=1 at=1 alle_oeffnen=1 kanal11=1 kanal12=1 kanal7=1 datenkanaele=1"
ORDNER=$HOME/redmi-logs
mkdir -p "$ORDNER"
LOG=$ORDNER/wlantest_$(date +%Y-%m-%d_%H%M%S).log

if command -v redmi >/dev/null; then
	handy() { redmi "$1"; }
else
	handy() { ssh -o ConnectTimeout=5 root@192.168.7.2 "$1"; }
fi
mac_weg() { sed -E 's/(([0-9A-Fa-f]{2}:){3})([0-9A-Fa-f]{2}:){2}[0-9A-Fa-f]{2}/\1xx:xx:xx/g'; }

echo "== WLAN-Test $(date '+%d.%m.%Y %H:%M'), Befehle: $BEFEHLE"
handy "LAUSCHER_PARAM=\"$PARAM\" redmi-wcntest.sh 3" 2>&1 | mac_weg > "$LOG.start"
if grep -q "WCN-KERN LAEUFT" "$LOG.start"; then
	echo "   WCN-Kern: laeuft ($(grep -o 'nach ca\. [0-9]* ms' "$LOG.start" | head -1))"
elif grep -q "schon geladen" "$LOG.start"; then
	echo "   WCN-Kern: laeuft schon von einem frueheren Test - bitte Handy neu starten:"
	echo "   redmi reboot   (danach ~/redmi-build/net.sh und das Skript nochmal)"
	exit 1
else
	echo "   WCN-Kern: STARTET NICHT. Letzte Zeilen aus $LOG.start:"
	tail -8 "$LOG.start" | sed 's/^/   | /'
	exit 1
fi

for b in $BEFEHLE; do
	case $b in
	download_ini) warte=3 ;;
	open) warte=5 ;;
	scan | scan5) warte=12 ;;
	*) warte=2 ;;
	esac
	handy "echo $b > /sys/devices/platform/wcn-lauscher/wlan_befehl" 2>&1 | sed "s/^/   $b: /"
	sleep $warte
	# nach jedem Befehl abholen: das Firmware-Log ist so gespraechig, dass der Kernel-Logpuffer
	# sonst bis zum Ende die fruehen Zeilen verliert
	handy 'dmesg' | mac_weg >> "$LOG.teile"
done

# Startprotokoll + alle Teile zusammensetzen; Zeilen beginnen mit "[ Sekunden]" fester Breite,
# daher sortiert sort (mit LC_ALL=C, sonst ignoriert es Leerzeichen) sie zeitlich; -u wirft doppelte weg
grep -h '^\[ *[0-9]*\.[0-9]*\]' "$LOG.start" "$LOG.teile" | LC_ALL=C sort -u > "$LOG"
rm -f "$LOG.teile"

echo "-- Kanaele bereit:"
k=$(grep -oE "DONE SB(UF|LOCK)_INIT Kanal [0-9]+" "$LOG" | grep -oE "[0-9]+$" | sort -n | uniq | tr '\n' ' ')
grep -q "DONE SBUF_INIT (sbuf bei WCN 0x240000)" "$LOG" && k="5 $k"
echo "   $k"
echo "-- Antworten der Firmware:"
grep -oE "WLAN-(Antwort|Ereignis): .*" "$LOG" | sed 's/^/   /'
if grep -q "SCAN: " "$LOG"; then
	echo "-- Gefundene Netze (BSSID halb ausgeblendet):"
	grep -oE "SCAN: .*" "$LOG" | sed 's/^SCAN: /   /'
elif echo "$BEFEHLE" | grep -q scan; then
	echo "-- Gefundene Netze: keine"
fi
if grep -q "GET_INFO: Chip" "$LOG"; then
	echo "-- Steckbrief:"
	grep -oE "GET_INFO: (Chip|Standards|MAC).*" "$LOG" | tail -3 | sed 's/^/   /'
fi
grep -q "INI: wifi_board_config" "$LOG" && grep -oE "INI: .*Schluessel.*" "$LOG" | tail -1 | sed 's/^/-- /'
n8=$(grep -cE "Kanal 8: Block [0-9]+, " "$LOG"); n9=$(grep -cE "Kanal 9: Block [0-9]+, " "$LOG")
echo "-- Datenkanaele: Kanal 8 $n8 Bloecke, Kanal 9 $n9 Bloecke empfangen (Anzeige max. 20)"
auff=$(grep "wcnlog:" "$LOG" | grep -i -E "assert|panic|error|fail|timeout" | grep -v -i -E "flush_rx_list|MSG_WIN_TIMEOUT" | head -5)
if [ -n "$auff" ]; then
	echo "-- Auffaellig im Firmware-Log:"
	echo "$auff" | sed -E 's/^\[ *[0-9.]+\] wcnlog: +/   /'
else
	echo "-- Firmware-Log: nichts Auffaelliges (assert/error/fail/timeout)"
fi
echo "== ganzes Log: $LOG"
