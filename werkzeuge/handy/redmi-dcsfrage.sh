#!/bin/bash
# redmi-dcsfrage.sh - Panel (NT36528) per DCS-Lesebefehl nach seinem Zustand fragen.
# Nutzt dieselben DSI-Register wie redmi-licht.sh und der Kernel-Treiber sprd_dsi.c (sprd_dsi_rd_pkt):
#   GEN_HDR 0x6C (Kopf), GEN_PLD_DATA 0x70 (Antwortdaten), CMD_MODE_STATUS 0x98:
#   Bit 7 = Lesen fertig (GEN_CMD_RDCMD_DONE), Bit 1 = Antwortpuffer leer, Bit 5 = Befehlspuffer leer.
# Paket-Typ 0x06 = "DCS Read, ohne Parameter". Achtung: Lesen braucht eine Bus-Umkehr (BTA) - im Videobetrieb
# kann das einen einzelnen Bildaufbau stoeren, sonst nichts.
#
#   redmi-dcsfrage.sh            -> fragt die Standard-Zustaende ab (0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, IDs)
#   redmi-dcsfrage.sh 0x0a       -> nur dieses Register
set -u
D=0x31100000
rd(){ busybox devmem $((D + $1)); }
wr(){ busybox devmem $((D + $1)) 32 $2; }
command -v busybox >/dev/null || { echo "busybox fehlt"; exit 1; }
# 31.10.: nicht fragen, wenn der Display-Treiber das Panel abgeschaltet hat (fb-blank): dann sind DSI und Panel aus,
# die Antwort waere bedeutungslos (Beleg 31.10.: Werte im Schlaf gelesen) und das Register-Lesen unnoetig riskant.
FB=/sys/class/graphics/fb0/blank
if [ -r $FB ] && [ "$(cat $FB 2>/dev/null)" != 0 ] && [ "${TROTZDEM:-0}" != 1 ]; then
    echo "Display ist ueber den Treiber abgeschaltet (fb0/blank = $(cat $FB)) - erst wecken (redmi-licht.sh an)"; exit 1
fi

frage(){   # $1 = DCS-Befehl -> gibt erstes Antwortbyte aus oder "keine Antwort"
    local c=$(( $1 )) n=0 s
    # Befehlspuffer muss leer sein
    while [ $(( $(rd 0x98) & 0x20 )) -eq 0 ]; do n=$((n+1)); [ $n -gt 50 ] && { echo "Puffer belegt"; return 1; }; sleep 0.01; done
    # alte Antwortreste verwerfen
    n=0; while [ $(( $(rd 0x98) & 0x02 )) -eq 0 ]; do rd 0x70 >/dev/null; n=$((n+1)); [ $n -gt 20 ] && break; done
    wr 0x6c $(( 0x06 | (c << 8) ))
    n=0; while [ $(( $(rd 0x98) & 0x80 )) -eq 0 ]; do n=$((n+1)); [ $n -gt 50 ] && { echo "keine Antwort"; return 1; }; sleep 0.01; done
    s=$(rd 0x98)
    [ $(( s & 0x02 )) -ne 0 ] && { echo "keine Daten"; return 1; }
    printf "0x%02X\n" $(( $(rd 0x70) & 0xff ))
}

erklaere(){   # Bits von 0x0A (Get Power Mode) nach MIPI DCS
    local v=$(( $1 ))
    printf "  Booster %s, Idle %s, Partial %s, Sleep-Out %s, Normal %s, Display %s\n" \
        $([ $((v & 0x80)) -ne 0 ] && echo an || echo aus) $([ $((v & 0x40)) -ne 0 ] && echo an || echo aus) \
        $([ $((v & 0x20)) -ne 0 ] && echo an || echo aus) $([ $((v & 0x10)) -ne 0 ] && echo ja || echo NEIN) \
        $([ $((v & 0x08)) -ne 0 ] && echo ja || echo nein) $([ $((v & 0x04)) -ne 0 ] && echo AN || echo AUS)
}

if [ $# -gt 0 ]; then echo "$1: $(frage $1)"; exit; fi
for c in 0x0A 0x0B 0x0C 0x0D 0x0E 0x0F 0xDA 0xDB 0xDC; do
    case $c in
        0x0A) t="Power Mode (gesund: 0x9C)";; 0x0B) t="Address Mode";; 0x0C) t="Pixel Format";;
        0x0D) t="Display Mode";; 0x0E) t="Signal Mode";; 0x0F) t="Selbstdiagnose";;
        0xDA) t="ID1 (Hersteller)";; 0xDB) t="ID2 (Version)";; 0xDC) t="ID3 (Modul)";;
    esac
    a=$(frage $c); echo "$c $t: $a"
    [ $c = 0x0A ] && case $a in 0x*) erklaere $a;; esac
done
