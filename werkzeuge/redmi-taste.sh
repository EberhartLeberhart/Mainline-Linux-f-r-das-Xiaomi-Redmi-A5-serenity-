#!/bin/bash
# redmi-taste.sh - Einschalttaste: mindestens 3 Sekunden halten = Herunterfahren.
# Kurzer Druck = Display-Licht kurz an (redmi-licht.sh kurz, nur wirksam bei START=aus).
# logind ist per /etc/systemd/logind.conf.d/redmi-taste.conf auf HandlePowerKey=ignore
# gestellt, sonst wuerde schon ein kurzer Druck ausschalten.
# Laeuft als redmi-taste.service. Braucht nur Bordmittel (od), kein Python.
HALTEN=3
k() { echo "REDMI: $*" > /dev/kmsg; }

# Eingabegeraet ueber den Namen finden (die Nummer eventN kann sich spaeter aendern)
dev=""
for e in /sys/class/input/event*; do
    [ "$(cat $e/device/name 2>/dev/null)" = "gpio-keys" ] && dev=/dev/input/$(basename $e)
done
[ -n "$dev" ] || { k "Taste: gpio-keys nicht gefunden"; exit 1; }
k "Taste: ueberwache $dev - Einschalttaste ${HALTEN} s halten = Herunterfahren"

pid=""
# Jedes Ereignis: 24 Bytes = 12 Zahlen; Feld 9 = Typ, 10 = Code, 11 = Wert (1 runter, 0 los, 2 Wiederholung)
stdbuf -oL od -An -v -tu2 -w24 "$dev" | while read -r _ _ _ _ _ _ _ _ typ code wert _; do
    [ "$typ" = 1 ] && [ "$code" = 116 ] || continue
    case "$wert" in
    1)  ( sleep $HALTEN; k "Taste ${HALTEN} s gehalten - fahre herunter"; systemctl poweroff ) &
        pid=$! ;;
    0)  # losgelassen vor Ablauf der 3 s = kurzer Druck -> Licht kurz an (06.10.)
        if [ -n "$pid" ] && kill $pid 2>/dev/null; then
            [ -x /usr/local/sbin/redmi-licht.sh ] && /usr/local/sbin/redmi-licht.sh kurz >/dev/null 2>&1 &
        fi
        pid="" ;;
    esac
done
