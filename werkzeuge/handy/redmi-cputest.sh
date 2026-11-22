#!/bin/sh
# redmi-cputest.sh: Rechenfehler-Zaehler fuer einzelne CPU-Kerne.
#
# Jeder gewaehlte Kern berechnet immer wieder die SHA-256-Pruefsumme derselben
# Zufallsdatei (im RAM) und vergleicht sie mit dem Sollwert. Ein gekipptes Bit
# im Kern oder in seinem Cache fuehrt zu einer falschen Summe - oft lange bevor
# der Kernel abstuerzt. Jeder Fehler landet sofort im Journal (logger -t REDMI-CPUTEST),
# damit er auch nach einem Absturz noch nachlesbar ist.
#
# Aufruf als root:  redmi-cputest.sh "6 7" 30      (Kerne 6 und 7, 30 Minuten)
#                   redmi-cputest.sh "0 1 2 3 4 5 6 7" 60
set -u

KERNE=${1:-"6 7"}
MINUTEN=${2:-30}
DIR=/dev/shm/redmi-cputest
mkdir -p "$DIR"

# 32 MiB Zufallsdaten: groesser als alle Caches, damit L1/L2/L3 und RAM arbeiten
head -c 33554432 /dev/urandom > "$DIR/daten"
SOLL=$(sha256sum "$DIR/daten" | cut -d' ' -f1)
ENDE=$(( $(date +%s) + MINUTEN * 60 ))

logger -t REDMI-CPUTEST "Start: Kerne $KERNE, $MINUTEN min"
echo "Start: Kerne $KERNE, $MINUTEN min (Fehler stehen sofort im Journal: journalctl -t REDMI-CPUTEST)"

for k in $KERNE; do
	(
		n=0; f=0
		while [ "$(date +%s)" -lt "$ENDE" ]; do
			IST=$(taskset -c "$k" sha256sum "$DIR/daten" | cut -d' ' -f1)
			n=$((n + 1))
			if [ "$IST" != "$SOLL" ]; then
				f=$((f + 1))
				logger -t REDMI-CPUTEST "FEHLER CPU$k Durchlauf $n Takt $(cat /sys/devices/system/cpu/cpu$k/cpufreq/scaling_cur_freq 2>/dev/null)"
			fi
		done
		echo "CPU$k: $n Durchlaeufe, $f Fehler" > "$DIR/ergebnis.$k"
		logger -t REDMI-CPUTEST "Ende CPU$k: $n Durchlaeufe, $f Fehler"
	) &
done
wait

echo "=== Ergebnis ==="
cat "$DIR"/ergebnis.*
rm -rf "$DIR"
