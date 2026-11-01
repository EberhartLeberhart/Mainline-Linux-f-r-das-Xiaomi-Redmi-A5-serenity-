#!/bin/bash
# redmi-kmsg.sh - Kernel-Meldungen (dmesg) laufend auf die eMMC schreiben, jede Zeile sofort gesichert.
# Damit bleiben die letzten Meldungen vor einem Haenger erhalten (dmesg selbst liegt nur im RAM).
#
#   Start:  systemd-run --unit=kmsg /usr/local/sbin/redmi-kmsg.sh
#   Ende:   systemctl stop kmsg
#   Datei:  /root/kmsg_<boot_id-Anfang>.log  (je Start eine eigene Datei; alte bleiben liegen)
#
# Liest /dev/kmsg ab dem aktuellen Stand (alte Meldungen werden einmal vorab mit dmesg gesichert).
# Jede Zeile: Zeit (Wanduhr) | Kernel-Zeit | Meldung. "sync" pro Zeile kostet etwas, ist aber bei
# normalem Betrieb (wenige Zeilen pro Minute) egal; bei einer Meldungsflut nur die ersten 20 je Sekunde.
set -u
BOOT=$(cut -c1-8 /proc/sys/kernel/random/boot_id)
LOG=/root/kmsg_${BOOT}.log
{
	echo "==== $(date '+%F %T') Mitschreiber gestartet, Kernel $(uname -r), boot_id $(cat /proc/sys/kernel/random/boot_id)"
	echo "---- bisheriger dmesg:"
	dmesg
	echo "---- ab hier laufend:"
} >> "$LOG"
sync "$LOG"

letzt=0; n=0
# /dev/kmsg liefert je read einen Eintrag "prio,seq,usec,-;Text"
exec 3< /dev/kmsg
# vorhandene Eintraege ueberspringen (die stehen schon oben)
while read -r -t 0.2 -u 3 _; do :; done
while read -r -u 3 zeile; do
	kopf=${zeile%%;*}; text=${zeile#*;}
	usec=$(echo "$kopf" | cut -d, -f3)
	printf '%s [%5d.%06d] %s\n' "$(date '+%F %T')" $((usec / 1000000)) $((usec % 1000000)) "$text" >> "$LOG"
	# jede Zeile sichern; nur bei einer Flut (> 20 Zeilen in derselben Sekunde) seltener
	jetzt=$(date +%s)
	if [ "$jetzt" != "$letzt" ]; then n=0; letzt=$jetzt; fi
	n=$((n + 1))
	[ $n -le 20 ] && sync "$LOG"
done
