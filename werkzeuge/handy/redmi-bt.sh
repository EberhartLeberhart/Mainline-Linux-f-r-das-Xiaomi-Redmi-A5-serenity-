#!/bin/sh
# redmi-bt.sh - Bluetooth mit Schalter (Redmi A5, 07.11.2026)
#
# Schalter: /etc/redmi/bt.conf  BT=an | BT=aus   (wirkt beim Start ueber redmi-bt.service)
# Braucht: Treiber btsprdsipc (CONFIG_BT_SPRDSIPC=m, Patch fuer das Park-Bit), /lib/firmware/sprd/bt_config_pskey.bin
#   und bt_config_rf.bin (aus der eigenen odm-Partition, werkzeuge/pc/bt_ini_bin.py - nie ins Repo), bluez.
#   Der WCN-Kern muss laufen - den startet bisher redmi-wlan.sh (WLAN=an).
#
# Warum ein Skript: Im Geraetebaum steht keine BT-Adresse (nur Nullen), deshalb meldet sich hci0 als
# "nicht konfiguriert". Erst "btmgmt public-addr" schickt dem Chip pskey/RF/Einschalten (0xfca0/0xfca2/0xfca1).
# Adresse = WLAN-MAC mit letztem Byte +1 (eindeutig, bei jedem Start gleich).
#
#   redmi-bt.sh an       Schalter auf an, Bluetooth jetzt einschalten
#   redmi-bt.sh aus      Schalter auf aus, Controller jetzt ausschalten (bis zum naechsten Start)
#   redmi-bt.sh status   Schalter, Zustand (Adresse maskiert)
#   redmi-bt.sh start    (vom Dienst) bei BT=an auf hci0 warten und Adresse setzen
CONF=/etc/redmi/bt.conf
log() { echo "redmi-bt: $*"; logger -t redmi-bt "$*" 2>/dev/null; }
maske() { sed -E 's/(([0-9A-Fa-f]{2}:){3})([0-9A-Fa-f]{2}:){2}[0-9A-Fa-f]{2}/\1xx:xx:xx/g'; }
schalter() { [ -f $CONF ] && . $CONF; echo "${BT:-aus}"; }
setzen() {
    mkdir -p /etc/redmi
    if [ -f $CONF ] && grep -q '^BT=' $CONF; then sed -i "s/^BT=.*/BT=$1/" $CONF
    else echo "BT=$1" >> $CONF; fi
}
# btmgmt haengt ohne Eingabe (unter systemd ist stdin /dev/null - Beleg 07.11.: rc 124), mit Pipe nicht
bm() { echo | timeout 10 btmgmt "$@"; }
konfiguriert() { bm --index 0 info 2>/dev/null | grep -q "^hci0:"; }

einschalten() {
    n=0
    while [ ! -e /sys/class/bluetooth/hci0 ]; do
        n=$((n + 1)); [ $n -gt 120 ] && { log "FEHLER: kein hci0 nach 120 s (WCN-Kern an? WLAN=an?)"; return 1; }
        sleep 1
    done
    if ! konfiguriert; then
        m=$(cat /sys/class/net/wlan0/address 2>/dev/null) || { log "FEHLER: keine WLAN-MAC fuer die BT-Adresse"; return 1; }
        l=$(printf "%02x" $(( (0x${m##*:} + 1) & 0xff )))
        a=$(echo "${m%:*}:$l" | tr a-f A-F)
        bm --index 0 public-addr "$a" > /dev/null 2>&1
        sleep 2
        konfiguriert || { log "FEHLER: hci0 nach public-addr nicht konfiguriert (dmesg: btsprd/hci0)"; return 1; }
        log "Adresse gesetzt: $(echo "$a" | maske)"
    fi
    bm --index 0 power on > /dev/null 2>&1
    log "Bluetooth an ($(bm --index 0 info 2>/dev/null | grep -o 'current settings:.*'))"
}

case "$1" in
start)
    [ "$(schalter)" = an ] || { log "Schalter ist aus ($CONF) - Bluetooth bleibt aus"; exit 0; }
    einschalten ;;
an)
    setzen an; einschalten ;;
aus)
    setzen aus
    bm --index 0 power off > /dev/null 2>&1; log "Bluetooth aus (Schalter aus)" ;;
status)
    echo "Schalter: $(schalter)  ($CONF)"
    if [ -e /sys/class/bluetooth/hci0 ]; then
        konfiguriert && bm --index 0 info 2>/dev/null | grep -E "addr|current settings" | maske \
                     || echo "hci0 da, aber nicht konfiguriert (keine Adresse)"
    else echo "kein hci0 (Treiber/WCN-Kern?)"; fi ;;
*)
    sed -n 2,17p "$0"; exit 1 ;;
esac
