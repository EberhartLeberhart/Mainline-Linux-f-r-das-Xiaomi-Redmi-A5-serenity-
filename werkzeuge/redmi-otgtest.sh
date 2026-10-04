#!/bin/bash
# redmi-otgtest.sh - EINMALIGER Test USB-Host (OTG) am Redmi A5, laeuft ohne SSH weiter.
#
# Ablauf (Stick abziehen beendet den Test sofort und sicher):
#         Start per  systemd-run --unit=otgtest /usr/local/sbin/redmi-otgtest.sh
#   1. Ladegrenze anhalten (stellt Strom/Laden normal)
#   2. warten (max. 120 s), bis das PC-Kabel abgezogen ist (Ladechip: kein Eingang)
#   3. 20 s Zeit, OTG-Adapter mit USB-Geraet einzustecken
#   4. nochmal pruefen: KEIN fremder Strom an der Buchse - sonst Abbruch
#   5. Rolle host + 5 V aus dem Ladechip (OTG an, Laden aus), 45 s beobachten
#   6. IMMER: 5 V aus, Rolle device, Ladegrenze wieder an  (auch bei Fehler/Abbruch)
# Protokoll: /root/otgtest.log  - nach dem Wiedereinstecken des PC-Kabels lesen.
#
# Ladechip SGM41513 (i2c-2, 0x1a): REG01 Bit 5 OTG, Bit 4 Laden; REG08 Bit 2 = Eingang ok (PG),
# Bits 7:5 = Eingangsart (111 = OTG); REG09 Bit 6 = Boost-Fehler (Ueberlast/Kurzschluss).
set -u
LOG=/root/otgtest.log
R=/sys/class/usb_role/64900000.usb-role-switch/role
BUS=2; ADR=0x1a
rd(){ i2cget -y -f $BUS $ADR $1; }
wr(){ i2cset -y -f $BUS $ADR $1 $2; }
log(){ echo "$(date +%T) $*" >> $LOG; echo "REDMI: OTG $*" > /dev/kmsg 2>/dev/null; }
strom(){ echo $(( $(cat /sys/class/power_supply/sc27xx-fgu/current_now)/1000 )); }
geraete(){ for d in /sys/bus/usb/devices/*; do [ -f $d/idVendor ] && echo "    $(basename $d) $(cat $d/idVendor):$(cat $d/idProduct) $(cat $d/product 2>/dev/null) [$(cat $d/speed 2>/dev/null) Mbit]"; done; }
kein_eingang(){ [ $(( $(rd 0x08) & 0x04 )) -eq 0 ]; }
# NUR echte Geraete an Port 1-1, 1-2 ... - NICHT 1-0:1.0 (der eingebaute Root-Hub, immer da!)
geraet_da(){ ls -d /sys/bus/usb/devices/1-[1-9]* >/dev/null 2>&1; }

# Waechter (laeuft parallel, solange 5 V an sind): jede Sekunde pruefen.
# Geraet 2 s weg (abgezogen) oder nach 15 s nie erkannt -> 5 V SOFORT aus, dann Test beenden.
# Grund 04.10.: Stick ab und PC-Kabel gleich wieder rein -> Handy speiste ~0,2 A in den PC zurueck.
waechter(){
    local gesehen=0 weg=0 n=0
    while :; do
        sleep 1; n=$((n+1))
        if geraet_da; then gesehen=1; weg=0; else weg=$((weg+1)); fi
        if [ $gesehen = 0 ] && [ $n -ge 15 ]; then
            wr 0x01 $NORMAL; log "Waechter: nach 15 s kein Geraet erkannt - 5 V aus"; kill -TERM $$; return
        fi
        if [ $gesehen = 1 ] && [ $weg -ge 2 ]; then
            wr 0x01 $NORMAL; log "Waechter: Geraet abgezogen - 5 V SOFORT aus"; kill -TERM $$; return
        fi
        # 2. Sicherung: kein Geraet, aber hoher Strom -> etwas Fremdes haengt an den 5 V (z.B. PC-Kabel)
        if [ $weg -ge 1 ] && [ $(strom) -lt -250 ]; then
            wr 0x01 $NORMAL; log "Waechter: kein Geraet, aber $(strom) mA - Fremdlast/PC? 5 V SOFORT aus"; kill -TERM $$; return
        fi
    done
}

: > $LOG
log "Test gestartet"
systemctl stop redmi-akku 2>/dev/null; sleep 2
R01=$(rd 0x01) || { log "FEHLER: Ladechip nicht lesbar"; exit 1; }
NORMAL=$(( (R01 | 0x10) & ~0x20 & 0xff ))   # Laden an, OTG aus
OTG=$(( (R01 | 0x20) & ~0x10 & 0xff ))      # OTG an, Laden aus
log "REG00=$(rd 0x00) REG01=$R01 REG02=$(rd 0x02) REG06=$(rd 0x06) REG08=$(rd 0x08) Rolle=$(cat $R)"

zurueck(){
    [ -n "${W:-}" ] && kill $W 2>/dev/null
    wr 0x01 $NORMAL
    echo device > $R 2>/dev/null
    log "ZURUECK: REG01=$(rd 0x01) REG08=$(rd 0x08) REG09=$(rd 0x09) Rolle=$(cat $R) Strom $(strom) mA"
    dmesg | grep -i -E "usb [0-9]|musb|hub|sd[a-z]|storage|otg" | tail -25 | sed 's/^/    k: /' >> $LOG
    systemctl start redmi-akku 2>/dev/null
    log "Test beendet - Kabel wieder einstecken ist jetzt sicher"
}
trap zurueck EXIT
trap 'exit 1' TERM INT HUP

log "warte auf Abziehen des PC-Kabels (max. 120 s) ..."
n=0; ok=0
while [ $n -lt 120 ]; do
    if kein_eingang; then ok=$((ok+1)); else ok=0; fi
    [ $ok -ge 3 ] && break
    sleep 1; n=$((n+1))
done
[ $ok -ge 3 ] || { log "ABBRUCH: Kabel nicht abgezogen"; exit 1; }
log "Kabel ab (REG08=$(rd 0x08), Strom $(strom) mA) - jetzt 20 s fuer OTG-Adapter + Geraet"
sleep 20
kein_eingang || { log "ABBRUCH: fremder Strom an der Buchse (REG08=$(rd 0x08)) - keine 5 V eingeschaltet"; exit 1; }

echo host > $R; sleep 1
log "Rolle jetzt: $(cat $R), Modus: $(cat /sys/devices/platform/soc/64900000.usb/musb-hdrc.9.auto/mode)"
f=$(rd 0x09)   # alten Fehler loeschen (wird beim Lesen geloescht)
wr 0x01 $OTG
waechter & W=$!
log "5 V an: REG01=$(rd 0x01) REG08=$(rd 0x08) (Waechter laeuft)"

# Lesetest (nur lesen - auf dem Stick wird nichts veraendert)
n=0; while [ ! -b /dev/sda ] && [ $n -lt 15 ]; do sleep 1; n=$((n+1)); done
if [ -b /dev/sda ]; then
    log "Lesetest: 200 MB direkt von /dev/sda ..."
    erg=$(timeout 60 dd if=/dev/sda of=/dev/null bs=1M count=200 iflag=direct 2>&1 | tail -1)
    log "  dd: $erg"
    log "  danach: REG08=$(rd 0x08) REG09=$(rd 0x09) Strom $(strom) mA"
    if [ -b /dev/sda1 ]; then
        mkdir -p /mnt/otgtest
        if mount -o ro /dev/sda1 /mnt/otgtest 2>>$LOG; then
            log "  sda1 nur lesend eingehaengt ($(findmnt -no FSTYPE /mnt/otgtest)), Inhalt:"
            ls /mnt/otgtest | head -10 | sed 's/^/      /' >> $LOG
            df -h /mnt/otgtest | tail -1 | sed 's/^/      /' >> $LOG
            umount /mnt/otgtest
        else
            log "  sda1 nicht einhaengbar (Dateisystem unbekannt?)"
        fi
    fi
else
    log "Lesetest: kein /dev/sda nach 15 s - Stick nicht erkannt"
fi

for t in 5 10 15 20 25 30; do
    sleep 5
    f=$(rd 0x09)
    log "${t}s: REG08=$(rd 0x08) REG09=$f Strom $(strom) mA"
    geraete >> $LOG
    [ $(( f & 0x40 )) -ne 0 ] && { log "BOOST-FEHLER (Ueberlast/Kurzschluss) - sofort aus"; exit 1; }
done
exit 0
