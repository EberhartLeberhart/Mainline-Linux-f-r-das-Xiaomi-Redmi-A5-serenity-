#!/bin/bash
# redmi-usb.sh - USB-Rolle automatisch waehlen (Redmi A5): PC -> Geraet (USB-Netz), Hub mit Netzteil -> Host.
#
#   Am Kabel ...         Erkennung                                         Rolle
#   PC                   Strom da + PC richtet USB-Netz ein (UDC configured)  device
#   Hub mit Netzteil     Strom da, nach 10 s nichts eingerichtet               host
#   nur Ladegeraet       wie Hub, aber im Host-Modus kein Geraet in 16 s       zurueck device (bis Neu-Einstecken)
#   nichts               keine USB-Spannung                                     device
#
# Eigene 5 V (OTG) werden NIE eingeschaltet - der Hub muss Strom liefern (Beleg 05.10.: Hub speist ein, Handy laedt).
# Strom-Erkennung ueber die USB-Spannung (PMIC-ADC Kanal 14), NICHT ueber den Ladechip: der zeigt bei HIZ
# (Ladegrenze "entladen") keinen Eingang mehr an.
#
# Einstellungen /etc/redmi/usb.conf:  USB=auto|geraet|hub   (Standard auto)
#   redmi-usb.sh status   -> zeigt, was erkannt wird (ohne etwas zu schalten)
set -u
CONF=${CONF:-/etc/redmi/usb.conf}
USB=auto
[ -f "$CONF" ] && . "$CONF"
R=${R_ROLE:-/sys/class/usb_role/64900000.usb-role-switch/role}
UDC=$(ls -d ${UDC_DIR:-/sys/class/udc}/* 2>/dev/null | head -1)
GERAETE=${GERAETE:-/sys/bus/usb/devices}
log(){ echo "REDMI: USB $*" > /dev/kmsg 2>/dev/null; echo "$*"; }

# USB-Spannung finden (IIO-Kanal 14 des PMIC-ADC, verarbeitet in mV)
VBUS=""
for d in ${IIO_DIR:-/sys/bus/iio/devices}/iio:device*; do
    [ -r "$d/in_voltage14_input" ] && { VBUS="$d/in_voltage14_input"; break; }
done
vbus(){ [ -n "$VBUS" ] && cat "$VBUS" 2>/dev/null || echo -1; }
rolle(){ cat "$R" 2>/dev/null; }
udc(){ [ -n "$UDC" ] && cat "$UDC/state" 2>/dev/null || echo "?"; }
geraet_da(){ ls -d $GERAETE/1-[1-9]* >/dev/null 2>&1; }   # NICHT 1-0:1.0 (Root-Hub)
# Beim Wechsel auf device den USB-Host abmelden (usb1/authorized=0): sonst bleiben Hub/Adapter als
# Altlasten stehen (Beleg 07.10.: nach Rollenwechsel ohne Abziehen-Ereignis noch in /sys/bus/usb/devices).
host_an(){ [ -w $GERAETE/usb1/authorized ] && echo $1 > $GERAETE/usb1/authorized 2>/dev/null; }
setze(){
    [ "$(rolle)" = "$1" ] && return
    if [ "$1" = host ]; then host_an 1; echo host > "$R"
    else echo device > "$R"; host_an 0; fi
    log "Rolle -> $1 ($2)"
}

# Altlasten beim Start entfernen, falls wir in der Geraete-Rolle sind
[ "$(rolle)" = device ] && [ "${1:-}" != status ] && host_an 0
if [ "${1:-}" = status ]; then
    echo "Einstellung USB=$USB  Rolle=$(rolle)  UDC=$(udc)  USB-Spannung=$(vbus) mV ($VBUS)"
    geraet_da && echo "Geraete am Host:" && for g in $GERAETE/1-[1-9]*; do [ -f $g/idVendor ] && echo "  $(basename $g) $(cat $g/idVendor):$(cat $g/idProduct) $(cat $g/product 2>/dev/null)"; done
    exit 0
fi
[ -n "$VBUS" ] || { log "FEHLER: USB-Spannung (ADC Kanal 14) nicht gefunden - bleibe bei device"; setze device "Fehler"; exit 1; }
case $USB in
    geraet) setze device "Einstellung"; log "fest: Geraet"; exit 0 ;;
    hub)    setze host "Einstellung";   log "fest: Host (Hub muss Strom liefern!)"; exit 0 ;;
esac

trap 'setze device "Dienst beendet"' EXIT
trap 'exit 0' TERM INT HUP
log "automatisch: PC -> Geraet, Hub mit Netzteil -> Host (USB-Spannung $(vbus) mV, Rolle $(rolle))"
warte=0; hostzeit=0; versucht=0
while :; do
    v=$(vbus)
    if [ "$v" -lt 2000 ]; then                      # nichts eingesteckt
        setze device "Kabel ab"; warte=0; hostzeit=0; versucht=0
    elif [ "$(rolle)" = host ]; then
        if geraet_da; then hostzeit=0
        else hostzeit=$((hostzeit+1))
             [ $hostzeit -ge 8 ] && { setze device "kein Geraet am Hub nach 16 s"; hostzeit=0; }
        fi
    else                                             # Rolle device, Strom da
        if [ "$(udc)" = configured ]; then warte=0   # PC hat das USB-Netz eingerichtet
        else warte=$((warte+1))
             if [ $warte -ge 5 ] && [ $versucht = 0 ]; then
                 versucht=1; warte=0; setze host "Strom da, aber kein PC nach 10 s"
             fi
        fi
    fi
    sleep 2 & wait $!
done
