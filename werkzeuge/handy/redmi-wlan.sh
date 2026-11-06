#!/bin/sh
# redmi-wlan.sh - WLAN mit Schalter (Redmi A5, 06.11.2026)
#
# Schalter: /etc/redmi/wlan.conf  WLAN=an | WLAN=aus   (wirkt beim Start ueber redmi-wlan.service)
# Zugangsdaten: /etc/redmi/wpa-wlan0.conf (wpa_supplicant-Format, nur root lesbar - nie ins Repo)
#
#   redmi-wlan.sh an       Schalter auf an, WLAN jetzt starten
#   redmi-wlan.sh aus      Schalter auf aus, WLAN jetzt trennen (wlan0 aus); der WCN-Kern bleibt bis zum
#                          naechsten Start an - ihn im Betrieb zu stoppen ist noch nicht erprobt
#   redmi-wlan.sh status   Schalter, Dienst, Adresse
#
# Intern (vom Dienst benutzt): pruefen (Schalter an?), vorbereiten (WCN-Kern laden, auf wlan0 warten).
# Die Ladesperre /etc/modprobe.d/wcn-erst-von-hand.conf bleibt: der Kernel laedt sprd_wcn nie von selbst,
# nur dieses Skript - so entscheidet allein der Schalter.
CONF=/etc/redmi/wlan.conf
log() { echo "redmi-wlan: $*"; logger -t redmi-wlan "$*" 2>/dev/null; }
schalter() { [ -f $CONF ] && . $CONF; echo "${WLAN:-aus}"; }
setzen() {
    mkdir -p /etc/redmi
    if [ -f $CONF ] && grep -q '^WLAN=' $CONF; then sed -i "s/^WLAN=.*/WLAN=$1/" $CONF
    else echo "WLAN=$1" >> $CONF; fi
}

case "$1" in
pruefen)
    [ "$(schalter)" = an ] && exit 0
    log "Schalter ist aus ($CONF) - WLAN bleibt aus"; exit 1 ;;
vorbereiten)
    [ -f /etc/redmi/wpa-wlan0.conf ] || { log "FEHLER: /etc/redmi/wpa-wlan0.conf fehlt"; exit 1; }
    modprobe sprd_wcn || { log "FEHLER: modprobe sprd_wcn"; exit 1; }
    for i in $(seq 1 30); do [ -e /sys/class/net/wlan0 ] && { log "wlan0 nach ${i} s da"; exit 0; }; sleep 1; done
    log "FEHLER: wlan0 kam nicht (30 s)"; exit 1 ;;
an)
    setzen an; systemctl start redmi-wlan.service && log "an" ;;
aus)
    setzen aus; systemctl stop redmi-wlan.service; log "aus (wlan0 getrennt)" ;;
status)
    echo "Schalter: $(schalter)"
    echo "Dienst:   $(systemctl is-active redmi-wlan.service)"
    echo "WCN-Kern: $(lsmod | grep -q '^sprd_wcn' && echo geladen || echo nicht geladen)"
    ip -4 -br addr show wlan0 2>/dev/null || echo "wlan0:    nicht vorhanden" ;;
*)
    sed -n 2,13p "$0"; exit 1 ;;
esac
