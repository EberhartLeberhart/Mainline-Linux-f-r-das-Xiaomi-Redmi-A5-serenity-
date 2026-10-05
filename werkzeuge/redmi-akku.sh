#!/bin/bash
# redmi-akku.sh - Ladegrenze fuer den Server-Betrieb (Ladechip SGM41513, i2c-2 Adresse 0x1a)
#
# Zustaende:  laden    = Strom an, Laden an          (Akku unter UNTEN)
#             ruhen    = Strom an, Laden aus         (Handy laeuft vom Kabel, Akku ruht)
#             entladen = Strom gekappt (HIZ)         (Akku ueber OBEN, Handy laeuft aus dem Akku)
#
# Belege 04.10.2026:
#   HIZ (REG00 Bit 7) an      -> Eingang getrennt, -102 mA aus dem Akku, USB-Netz bleibt
#   Laden aus (REG01 Bit 4)   -> 223 mA -> 1 mA, Status "laedt nicht", haelt, kein Fehler
#   Beides steht nach einem Neustart wieder auf normal (Bootloader). REG04 (Ladespannung)
#   bleibt dagegen ueber Neustarts stehen -> wird hier NICHT angefasst.
#   Nach HIZ aus erkennt der Chip den Eingang neu und stellt den Eingangsstrom selbst
#   hoch (0x17 = 2400 mA, am PC-Anschluss zu viel) -> wird danach zurueckgestellt.
#
# Einstellungen: /etc/redmi/akku.conf (MODUS=server, OBEN, UNTEN, NOTFALL, INTERVALL)
# Ohne MODUS=server tut der Dienst nichts (Handy-Modus: voll laden).
set -u
CONF=${CONF:-/etc/redmi/akku.conf}
MODUS=handy; OBEN=80; UNTEN=75; NOTFALL=20; INTERVALL=60; PRUEF=10; EINGANG_MAX=500
[ -f "$CONF" ] && . "$CONF"
BUS=2; ADR=0x1a
CAP=/sys/class/power_supply/sc27xx-fgu/capacity

log(){ echo "REDMI: Akku $*" > /dev/kmsg 2>/dev/null; echo "$*"; }
rd(){ i2cget -y -f $BUS $ADR $1; }
wr(){ i2cset -y -f $BUS $ADR $1 $2; }

if [ "$MODUS" != server ]; then
    log "Modus '$MODUS' - keine Ladegrenze, Laden normal"; exit 0
fi
[ "$UNTEN" -lt "$OBEN" ] && [ "$NOTFALL" -lt "$UNTEN" ] || { log "FEHLER: NOTFALL < UNTEN < OBEN noetig"; exit 1; }

R00=$(rd 0x00) && R01=$(rd 0x01) || { log "FEHLER: Ladechip nicht lesbar"; exit 1; }
# Eingangsstrom (REG00 Bits 4:0, 100 mA + n*100 mA) auf hoechstens EINGANG_MAX begrenzen.
# Grund 05.10.: nach dem Einstecken stellt der Chip selbst 0x17 = 2400 mA ein (auch am PC-Anschluss).
IMAX=$(( (EINGANG_MAX - 100) / 100 )); [ $IMAX -lt 0 ] && IMAX=0; [ $IMAX -gt 31 ] && IMAX=31
IIN=$(( R00 & 0x7f ))            # Eingangsstrom wie beim Start, ohne HIZ-Bit
[ $(( IIN & 0x1f )) -gt $IMAX ] && IIN=$(( (IIN & 0x60) | IMAX ))
AN=$(( R01 | 0x10 ))             # REG01 mit Laden an
AUS=$(( R01 & ~0x10 & 0xff ))    # REG01 mit Laden aus

hiz_aus(){
    # HIZ nur loesen, wenn es gesetzt ist - danach Eingangsstrom zurueck (Neuerkennung)
    if [ $(( $(rd 0x00) & 0x80 )) -ne 0 ]; then
        wr 0x00 $IIN; sleep 3; wr 0x00 $IIN
    fi
}
setze(){
    case $1 in
        laden)    hiz_aus; wr 0x00 $IIN; wr 0x01 $AN ;;
        ruhen)    hiz_aus; wr 0x00 $IIN; wr 0x01 $AUS ;;
        entladen) wr 0x01 $AUS; wr 0x00 $(( IIN | 0x80 )) ;;
    esac
}
passt(){   # stimmen die Schalter im Chip noch mit dem Zustand ueberein?
    local r0=$(rd 0x00) h l
    h=$(( r0 & 0x80 )); l=$(( $(rd 0x01) & 0x10 ))
    [ $(( r0 & 0x1f )) -gt $IMAX ] && return 1      # Eingangsstrom zu hoch (Neuerkennung)
    case $1 in
        laden)    [ $h -eq 0 ] && [ $l -ne 0 ] ;;
        ruhen)    [ $h -eq 0 ] && [ $l -eq 0 ] ;;
        entladen) [ $h -ne 0 ] ;;
    esac
}

# Nach einer Korrektur 10 s lang alle 2 s nachsehen: Beleg 05.10. - die Eingangserkennung nach dem
# Einstecken dauert einige Sekunden und ueberschreibt eine zu fruehe Korrektur noch einmal (0x17).
nachsehen(){
    local k alt
    for k in 1 2 3 4 5; do
        sleep 2
        if ! passt $zust; then
            alt="0x00=$(rd 0x00) 0x01=$(rd 0x01)"
            setze $zust; log "$(cat $CAP 2>/dev/null) % - Chip hat nachtraeglich ueberschrieben ($alt), erneut gesetzt"
        fi
    done
}

# Beim Beenden - egal wie - immer zurueck auf normal (Strom an, Laden an)
trap 'setze laden; log "Dienst beendet - Laden normal"' EXIT
trap 'exit 0' TERM INT HUP

log "Ladegrenze aktiv: ${UNTEN}-${OBEN} %, Notfall unter ${NOTFALL} %, Eingang 0x$(printf %02x $IIN) (max. ${EINGANG_MAX} mA), Pruefung alle ${PRUEF} s"
zust=""; seit=$INTERVALL
while :; do
  if [ -n "$zust" ] && [ $seit -lt $INTERVALL ]; then
    # nur der schnelle Chip-Check (Einstecken setzt HIZ zurueck und den Eingangsstrom hoch)
    if ! passt $zust; then
        alt="0x00=$(rd 0x00) 0x01=$(rd 0x01)"
        setze $zust; log "$(cat $CAP 2>/dev/null) % - Chip stand nicht auf '$zust' ($alt), neu gesetzt"
        nachsehen
    fi
  else
    seit=0
    c=$(cat $CAP 2>/dev/null)
    if ! [ "$c" -ge 0 ] 2>/dev/null; then
        soll=laden; c="?"                       # Anzeige nicht lesbar -> lieber laden
    elif [ "$c" -le "$NOTFALL" ]; then
        soll=laden
    else
        case $zust in
            laden)    [ "$c" -ge "$OBEN" ] && soll=ruhen || soll=laden ;;
            ruhen)    [ "$c" -lt "$UNTEN" ] && soll=laden || soll=ruhen ;;
            entladen) [ "$c" -le "$OBEN" ] && soll=ruhen || soll=entladen ;;
            *)        if   [ "$c" -gt "$OBEN" ];  then soll=entladen
                      elif [ "$c" -ge "$UNTEN" ]; then soll=ruhen
                      else soll=laden; fi ;;
        esac
    fi
    if [ "$soll" != "$zust" ]; then
        setze $soll; log "$c % -> $soll"; zust=$soll
    elif ! passt $zust; then
        alt="0x00=$(rd 0x00) 0x01=$(rd 0x01)"
        setze $zust; log "$c % - Chip stand nicht auf '$zust' ($alt), neu gesetzt"
        nachsehen
    fi
    f=$(rd 0x09); [ "$f" != 0x00 ] && log "Ladechip meldet Fehler $f (REG09) bei $c %"
  fi
  sleep "$PRUEF" & wait $!; seit=$(( seit + PRUEF ))
done
