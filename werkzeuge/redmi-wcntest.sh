#!/bin/bash
# redmi-wcntest.sh - WCN-Kern (BT/WLAN) stufenweise starten mit kernel/wcn-test/wcn_starttest.ko
# und alles protokollieren: /root/wcntest_<Datum>_stufe<N>.log
#
#   redmi-wcntest.sh 0     nur lesen (fasst WCN-Register 0x51... nie an)
#   redmi-wcntest.sh 1     + Strom (Regler, merlion-GPIOs, PUB-Umlenkung)
#   redmi-wcntest.sh 2     + WCN-System einschalten und aufwecken
#   redmi-wcntest.sh 3     + Firmware nach 0x87000000, BTWF-CPU loslassen, auf 0xF0F0F0FF warten
#
# Regel 6: eine Stufe pro Start. Nach Stufe 1..3 vor dem naechsten Versuch neu starten
# (der Treiber verweigert sonst mit "WCN-System ist schon an").
# Vorher: wcnmodem.bin aus odm_a:/firmware nach /lib/firmware kopieren (nicht frei, bleibt auf dem Handy).
set -u
STUFE=${1:-0}
KO=${KO:-/root/wcn_starttest.ko}
LAUSCHER=${LAUSCHER:-/root/wcn_lauscher.ko}   # hoert auf Mailbox-Kanal 8 mit (nur Stufe 3, wenn vorhanden)
LOG=/root/wcntest_$(date +%Y-%m-%d_%H%M)_stufe${STUFE}.log

log(){ echo "$*" | tee -a "$LOG"; }
mbox(){ grep -i mailbox /proc/interrupts | tr -s ' '; }

case "$STUFE" in 0|1|2|3) ;; *) echo "Stufe 0..3"; exit 1;; esac
[ -f "$KO" ] || { echo "Modul $KO fehlt"; exit 1; }
if [ "$STUFE" = 3 ] && [ ! -f /lib/firmware/wcnmodem.bin ]; then
	echo "ABBRUCH: /lib/firmware/wcnmodem.bin fehlt (aus odm_a:/firmware/wcnmodem.bin, 1 273 416 Bytes)"
	exit 1
fi
lsmod | grep -q '^wcn_starttest' && { echo "Modul ist schon geladen: erst rmmod wcn_starttest (Hardware bleibt wie sie ist)"; exit 1; }

: > "$LOG"
log "== wcntest Stufe $STUFE, $(date), Kernel $(uname -r -v)"
log "boot_id $(cat /proc/sys/kernel/random/boot_id), Laufzeit $(cut -d' ' -f1 /proc/uptime) s"
[ -f /lib/firmware/wcnmodem.bin ] && log "Firmware: $(stat -c %s /lib/firmware/wcnmodem.bin) Bytes, sha256 $(sha256sum /lib/firmware/wcnmodem.bin | cut -c1-16)"
log "-- Mailbox-Interrupts vorher:"; mbox | tee -a "$LOG"
echo "REDMI: wcntest Stufe $STUFE startet" > /dev/kmsg

N0=$(dmesg | wc -l)
if [ "$STUFE" = 3 ] && [ -f "$LAUSCHER" ] && ! lsmod | grep -q "^wcn_lauscher"; then
	insmod "$LAUSCHER" kanal=8 && log "-- Lauscher auf Mailbox-Kanal 8 geladen" || log "-- Lauscher laedt NICHT (weiter ohne)"
fi
insmod "$KO" stufe="$STUFE"
RC=$?
log "-- insmod Rueckgabe $RC"

# Nach dem Start Zeit lassen, damit ein laufender Kern sich ueber die Mailbox melden kann
[ "$STUFE" = 3 ] && sleep 5
log "-- dmesg:"; dmesg | tail -n +$((N0 + 1)) | tee -a "$LOG"
log "-- Mailbox-Interrupts nachher:"; mbox | tee -a "$LOG"
if [ -r /sys/devices/platform/wcn-lauscher/nachrichten ]; then
	log "-- Lauscher:"; tee -a "$LOG" < /sys/devices/platform/wcn-lauscher/nachrichten
fi
if [ -r /sys/devices/platform/wcntest/zustand ]; then
	log "-- Zustand:"; tee -a "$LOG" < /sys/devices/platform/wcntest/zustand
fi
log "== Protokoll: $LOG"
