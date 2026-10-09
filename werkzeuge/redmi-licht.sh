#!/bin/bash
# redmi-licht.sh - Display-Helligkeit des Redmi A5 ueber den DSI-Befehl 0x51 (ohne Display-Treiber).
#
#   redmi-licht.sh aus | an | <0-4095> | kurz [Sekunden] | schlaf | wach | start | status
#
#   aus    = Licht 0 UND Panel schlafen legen (DCS 0x28 + 0x10); an/Wert/kurz wecken es vorher (0x11 + 0x29)
#
# Belege 06.10.2026:
#   Panel C3Z_42 (NT36528-TDDI) regelt das Licht selbst: Xiaomi-DT oled-backlight
#   brightness-levels = [39 00 00 03 51 00 00], max-level 0xfff -> DCS 0x51 mit 12 Bit.
#   DSI-Baustein 0x31100000 laeuft noch vom Bootloader (Register = Panelwerte, PHY eingerastet),
#   Registeranordnung wie Mainline sprd_dsi.c: GEN_PLD_DATA 0x70, GEN_HDR 0x6C, CMD_MODE_STATUS 0x98.
#   Gemessen (Akku, ohne Kabel): voll 490 mA, 500 -> 119 mA, aus -> 72 mA.
# Beleg 08.10.2026 (redmi-paneltest.sh): Licht 500 -118 mA, Licht 0 -74 mA, Panel schlaeft -51 mA,
#   nach Sleep-Out + Display-On Bild wieder da, -119 mA. Sleep-Out braucht 120 ms vor Display-On.
#
# Einstellungen /etc/redmi/licht.conf:  START=aus|an  HELL=500  KURZ=30
set -u
CONF=${CONF:-/etc/redmi/licht.conf}
START=an; HELL=500; KURZ=30
[ -f "$CONF" ] && . "$CONF"
D=0x31100000
STAND=/run/redmi-licht.stand
SCHLAF=/run/redmi-licht.schlaeft
# Touch (NT36528, TDDI): Display und Touch sind EIN Chip. Sleep-In bringt die Touch-Firmware zum Absturz
# (Beleg 09.10.: Puffer nur noch "fd" = Waechter-Alarm, ~200 Interrupts/s, keine Beruehrungen mehr).
# Deshalb: vor dem Schlafen Treiber abmelden, nach dem Wecken neu anmelden -> Firmware wird frisch geladen.
TS_DEV=spi3.0
TS_DRV=/sys/bus/spi/drivers/nt36675-spi
touch_ab(){ [ -e $TS_DRV/$TS_DEV ] && echo $TS_DEV > $TS_DRV/unbind 2>/dev/null; return 0; }
touch_an(){ [ -d $TS_DRV ] && [ ! -e $TS_DRV/$TS_DEV ] && echo $TS_DEV > $TS_DRV/bind 2>/dev/null; return 0; }
KPID=/run/redmi-licht.kurz
k(){ echo "REDMI: Licht $*" > /dev/kmsg 2>/dev/null; }
rd(){ busybox devmem $((D + $1)); }
wr(){ busybox devmem $((D + $1)) 32 $2; }

dsi_ok(){   # DSI laeuft? PHY_STATUS Bit 1 = PHY eingerastet
    [ $(( $(rd 0x9c) & 0x2 )) -ne 0 ]
}
fifo_leer(){  # CMD_MODE_STATUS: Bit 3 Nutzdaten-FIFO leer, Bit 5 Befehls-FIFO leer
    local n=0
    while [ $(( $(rd 0x98) & 0x28 )) -ne $((0x28)) ]; do
        n=$((n+1)); [ $n -gt 50 ] && return 1; sleep 0.02
    done
}
setze(){
    local w=$1
    [ "$w" -ge 0 ] 2>/dev/null && [ "$w" -le 4095 ] || { echo "Wert 0-4095"; return 1; }
    command -v busybox >/dev/null || { echo "busybox fehlt (apt install busybox)"; return 1; }
    dsi_ok || { k "DSI nicht bereit - nichts gesendet"; echo "DSI nicht bereit"; return 1; }
    fifo_leer || { k "DSI-Puffer voll - nichts gesendet"; echo "DSI-Puffer voll"; return 1; }
    # DCS Long Write (0x39), 3 Bytes: 51 <hoch> <tief>
    wr 0x70 $(( 0x51 | ((w >> 8) << 8) | ((w & 0xff) << 16) ))
    wr 0x6c 0x339
    echo $w > $STAND
}
dcs(){ fifo_leer || return 1; wr 0x6c $(( 0x05 | ($1 << 8) )); }   # kurzer DCS-Befehl ohne Parameter
schlaf(){
    [ -f $SCHLAF ] && return 0
    dsi_ok || return 1
    setze 0 || return 1
    touch_ab
    dcs 0x28 && sleep 0.05 && dcs 0x10 && touch $SCHLAF
}
wach(){
    [ -f $SCHLAF ] || return 0
    dsi_ok || return 1
    dcs 0x11 && sleep 0.15 && dcs 0x29 && rm -f $SCHLAF && touch_an
}
kurz_stop(){ [ -f $KPID ] && kill "$(cat $KPID)" 2>/dev/null; rm -f $KPID; }

case "${1:-status}" in
    aus|schlaf) kurz_stop; schlaf && k "aus (Panel schlaeft)" ;;
    wach)   kurz_stop; wach && k "Panel wach (Licht unveraendert)" ;;
    an)     kurz_stop; wach; setze $HELL && k "an ($HELL)" ;;
    kurz)   # fuer N Sekunden an, danach wieder aus (nur wenn START=aus, sonst bleibt es an)
            s=${2:-$KURZ}; kurz_stop; wach; setze $HELL || exit 1
            if [ "$START" = aus ]; then
                ( sleep $s; schlaf; rm -f $KPID ) & echo $! > $KPID
                k "kurz an fuer $s s"
            fi ;;
    start)  # beim Hochfahren (redmi-licht.service)
            if [ "$START" = aus ]; then sleep 10; rm -f $SCHLAF; schlaf && k "Start: Licht aus, Panel schlaeft (START=aus, Einschalttaste kurz = ${KURZ} s an)"
            else k "Start: Licht bleibt an"; fi ;;
    status) echo "Panel: $([ -f $SCHLAF ] && echo schlaeft || echo wach)  Touch: $([ -e $TS_DRV/$TS_DEV ] && echo an || echo ab)  Stand: $(cat $STAND 2>/dev/null || echo unbekannt)  START=$START HELL=$HELL KURZ=$KURZ  DSI: $(dsi_ok && echo bereit || echo NICHT bereit)" ;;
    *)      if [ "$1" -ge 0 ] 2>/dev/null; then kurz_stop; wach; setze "$1" && k "auf $1"
            else sed -n 2,4p "$0"; exit 1; fi ;;
esac
