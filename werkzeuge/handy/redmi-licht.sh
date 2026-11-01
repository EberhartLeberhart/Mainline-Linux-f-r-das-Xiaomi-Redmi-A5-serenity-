#!/bin/bash
# redmi-licht.sh - Display-Helligkeit des Redmi A5 ueber den DSI-Befehl 0x51 (ohne Display-Treiber).
#
#   redmi-licht.sh aus | an | <0-4095> | kurz [Sekunden] | schlaf | wach | start | status
#
#   aus    = Licht 0 UND Panel schlafen legen; an/Wert/kurz wecken es vorher
#            Weg ab 29.10.: ueber den Display-Treiber (fb-blank: Panel aus, DSI+DPU aus, Touch schlaeft als
#            Panel-Follower mit). Ersatzweg (kein DRM oder WEG=dcs in licht.conf): DCS 0x28 + 0x10 / 0x11 + 0x29
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
# Beleg 29.10.2026: fb-blank/unblank mit Touch als Panel-Follower (Patch 0029) -> Panel 0x9C/0x2C, Touch-Firmware
#   genau einmal neu geladen, evtest 827 Positionen.
#
# Einstellungen /etc/redmi/licht.conf:  START=aus|an  HELL=500  KURZ=30  WEG=treiber|dcs
set -u
CONF=${CONF:-/etc/redmi/licht.conf}
START=an; HELL=500; KURZ=30; WEG=treiber
[ -f "$CONF" ] && . "$CONF"
D=0x31100000
STAND=/run/redmi-licht.stand
SCHLAF=/run/redmi-licht.schlaeft   # Inhalt = Weg, auf dem das Panel eingeschlafen ist (treiber|dcs; leer = dcs, alte Fassung)
# Touch (NT36528, TDDI): Display und Touch sind EIN Chip. Sleep-In bringt die Touch-Firmware zum Absturz
# (Beleg 09.10.: Puffer nur noch "fd" = Waechter-Alarm, ~200 Interrupts/s, keine Beruehrungen mehr).
# Deshalb: vor dem Schlafen Treiber abmelden, nach dem Wecken neu anmelden -> Firmware wird frisch geladen.
TS_DEV=spi3.0
TS_DRV=/sys/bus/spi/drivers/nt36675-spi
# Echter Display-Treiber (DRM, ab 09.10.): Helligkeit ueber /sys/class/backlight.
# Schlaf ueber den Treiber (fb-blank) bekam das Panel bis 28.10. nicht wieder an (0x53 fehlte, Patch 0027).
# Seit 29.10. ist das der Standard-Weg; der Touch folgt dem Panel (DT-Eigenschaft "panel", Patch 0029) und
# muss dann NICHT mehr ab-/angemeldet werden.
BL=$(ls -d /sys/class/backlight/*dsi* 2>/dev/null | head -1)
FB=/sys/class/graphics/fb0
drm(){ [ -n "$BL" ] && [ -w $FB/blank ]; }
folger(){ [ -e /proc/device-tree/soc/spi@20150000/touchscreen@0/panel ]; }
treiberweg(){ [ "$WEG" != dcs ] && drm && folger; }
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
    if drm; then echo $w > $BL/brightness && echo $w > $STAND; return; fi
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
    if treiberweg; then
        setze 0 || return 1
        echo 1 > $FB/blank && echo treiber > $SCHLAF
        return
    fi
    dsi_ok || return 1
    setze 0 || return 1
    touch_ab
    dcs 0x28 && sleep 0.05 && dcs 0x10 && echo dcs > $SCHLAF
}
wach(){
    [ -f $SCHLAF ] || return 0
    # Geweckt wird auf dem Weg, auf dem das Panel eingeschlafen ist: Ein per DCS schlafendes Panel weckt
    # fb-unblank nicht, weil der Treiber es fuer wach haelt (Beleg 29.10.: danach 0x0A = 0x08, Touch tot).
    if [ "$(cat $SCHLAF 2>/dev/null)" = treiber ]; then
        echo 0 > $FB/blank && rm -f $SCHLAF
        touch_an   # nur falls der Touch noch vom alten Weg abgemeldet ist
        return 0
    fi
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
    status) echo "Panel: $([ -f $SCHLAF ] && echo "schlaeft ($(cat $SCHLAF 2>/dev/null || true))" || echo wach)  Touch: $([ -e $TS_DRV/$TS_DEV ] && echo an || echo ab)  Stand: $(cat $STAND 2>/dev/null || echo unbekannt)  START=$START HELL=$HELL KURZ=$KURZ  Licht: $(drm && echo "DRM ($BL)" || echo "Register")  Schlaf: $(treiberweg && echo "Treiber (fb-blank, Touch folgt)" || echo "DCS (DSI $(dsi_ok && echo bereit || echo NICHT bereit))")" ;;
    *)      if [ "$1" -ge 0 ] 2>/dev/null; then kurz_stop; wach; setze "$1" && k "auf $1"
            else sed -n 2,4p "$0"; exit 1; fi ;;
esac
