#!/bin/bash
# redmi-lantest.sh - Senden ueber den USB-LAN-Adapter (RTL8153) im Hub-Betrieb testen. Laeuft ohne SSH weiter.
#
# Befund 08.10.: Empfang geht (DHCP), Senden haengt nach kurzer Zeit:
#   r8152 ... Tx status -2 / NETDEV WATCHDOG transmit queue timed out / reset high-speed USB device
# Verdacht: musb-DMA bei Bulk-OUT. Varianten (eine pro Lauf):
#   VARIANTE=normal   - nichts aendern (Gegenprobe)
#   VARIANTE=ethtool  - Sendehilfen aus (tso/gso/sg/tx-checksum) am r8152
#   VARIANTE=cdc      - Adapter auf CDC-ECM-Konfiguration umstellen -> Treiber cdc_ether statt r8152
#                       (08.10.: geht nicht - cdc_ether uebernimmt Realtek nicht, wenn r8152 eingebaut ist)
#   VARIANTE=mtu      - MTU 480: jedes Paket passt in EIN USB-Paket (512 Byte) - Gegenprobe zur Groessengrenze
# Befund 08.10. (normal): 56-Byte-Pings 12/12, ab 1000 Byte 0/12, danach Sendeschlange fest (Watchdog/Reset).
#
# Start (Handy am PC-USB):  systemd-run --unit=lantest -E VARIANTE=cdc /usr/local/sbin/redmi-lantest.sh
# dann Handy in den Hub (LAN-Kabel zum PC, PC verteilt 10.42.0.x). Protokoll: /root/lantest.log
set -u
VARIANTE=${VARIANTE:-normal}
ZIEL=${ZIEL:-10.42.0.1}           # PC am LAN-Kabel (NetworkManager "geteilt")
LOG=/root/lantest.log
log(){ echo "$(date +%T) $*" >> $LOG; echo "REDMI: LAN $*" > /dev/kmsg 2>/dev/null; }
fehler(){ dmesg | grep -c -E "Tx status|transmit queue|Tx timeout|reset high-speed" ; }
: > $LOG
log "Test gestartet, Variante=$VARIANTE, Ziel=$ZIEL - warte bis zu 5 min auf den Adapter"

# 1) warten, bis der Adapter als USB-Geraet auftaucht (0bda:8153)
geraet=""; n=0
while [ -z "$geraet" ] && [ $n -lt 300 ]; do
    for g in /sys/bus/usb/devices/1-*; do
        [ "$(cat $g/idVendor 2>/dev/null):$(cat $g/idProduct 2>/dev/null)" = "0bda:8153" ] && geraet=$g
    done
    sleep 1; n=$((n+1))
done
[ -n "$geraet" ] || { log "ABBRUCH: kein RTL8153 gefunden"; exit 1; }
log "Adapter gefunden: $(basename $geraet), Konfiguration $(cat $geraet/bConfigurationValue), Konfigurationen: $(cat $geraet/bNumConfigurations)"

# 2) Variante anwenden
if [ "$VARIANTE" = cdc ]; then
    echo 2 > $geraet/bConfigurationValue 2>>$LOG && log "auf Konfiguration 2 (CDC-ECM) umgestellt" || log "Umstellen auf Konfiguration 2 fehlgeschlagen"
    sleep 3
fi
# Netzwerkkarte des Adapters finden
nic=""; n=0
while [ -z "$nic" ] && [ $n -lt 30 ]; do
    for i in /sys/class/net/*; do
        readlink -f $i/device 2>/dev/null | grep -q "$(basename $geraet)" && nic=$(basename $i)
    done
    sleep 1; n=$((n+1))
done
[ -n "$nic" ] || { log "ABBRUCH: keine Netzwerkkarte zum Adapter"; exit 1; }
log "Netzwerkkarte: $nic, Treiber $(basename $(readlink -f /sys/class/net/$nic/device/driver))"
if [ "$VARIANTE" = ethtool ]; then
    if command -v ethtool >/dev/null; then
        ethtool -K $nic tso off gso off sg off tx off >>$LOG 2>&1; log "Sendehilfen aus: $(ethtool -k $nic | grep -E '^(tcp-segm|generic-segm|scatter|tx-checksumming)' | tr -s ' \n' ' ')"
    else log "ethtool fehlt (apt install ethtool) - Variante wirkungslos"; fi
fi

# 3) auf Adresse warten
n=0; while ! ip -4 addr show $nic | grep -q "inet " && [ $n -lt 40 ]; do sleep 1; n=$((n+1)); done
log "Adresse: $(ip -4 -br addr show $nic)"
if [ "$VARIANTE" = mtu ]; then ip link set $nic mtu 480 && log "MTU auf 480 gesetzt"; fi
log "Firmware-Flicken: $(dmesg | grep -i -E 'rtl8153b|firmware patch|load rtl' | tail -1 | cut -c16-)"
f0=$(fehler)

# 4) eine Minute senden: kleine, mittlere, grosse Pakete
for s in 56 1000 1472 56 1472; do
    erg=$(ping -I $nic -c 12 -i 1 -W 1 -s $s $ZIEL 2>&1 | grep -E "packets transmitted")
    log "Ping $s Bytes: $erg | Sendefehler im Kernel seit Start: $(( $(fehler) - f0 ))"
done
dmesg | grep -E "r8152|cdc_ether|Tx|musb|reset high-speed" | tail -8 | sed 's/^/    k: /' >> $LOG
log "Test fertig - Handy kann zurueck an den PC"
