#!/bin/bash
# startreihe.sh - Startversuche systematisch protokollieren (Beleg statt Eindruck).
#
#   ~/redmi-build/startreihe.sh aus kalt      -> faehrt in 30 s herunter (dann Kabel ab, Taste)
#   ~/redmi-build/startreihe.sh aus warm      -> sofortiger Neustart (reboot)
#   ~/redmi-build/startreihe.sh an  kalt      -> nach dem Start: pruefen und eintragen
#   ~/redmi-build/startreihe.sh an  kalt-kabel  (Art frei waehlbar, z.B. kalt-kabel)
#   ~/redmi-build/startreihe.sh an  haenger "Redmi-Logo"  -> gescheiterten Versuch eintragen
#   ~/redmi-build/startreihe.sh liste         -> Tabelle anzeigen
#
# Protokoll: ~/redmi-build/startreihe.csv
LOG=~/redmi-build/startreihe.csv
H=root@192.168.7.2
[ -f "$LOG" ] || echo "zeit;art;ergebnis;boot_id;laufzeit_s;kernel;slot;poweroff_handler;check_ok;check_teil;check_fehlt;notiz" > "$LOG"

case "$1" in
aus)
    art=${2:-kalt}
    # nach einem Neustart fehlt die USB-Netzverbindung - sonst haengt ssh still
    ping -c1 -W2 192.168.7.2 >/dev/null 2>&1 || ~/redmi-build/net.sh || { echo "FEHLER: Handy nicht erreichbar"; exit 1; }
    if [ -f /tmp/startreihe_alt_id ]; then
        echo "HINWEIS: Der letzte Versuch wurde noch nicht mit '$0 an ...' eingetragen. Erst das nachholen!"; exit 1
    fi
    ssh $H "cat /proc/sys/kernel/random/boot_id" > /tmp/startreihe_alt_id 2>/dev/null
    echo ">>> alte boot_id: $(cat /tmp/startreihe_alt_id)"
    if [ "$art" = "warm" ]; then
        ssh $H 'sync; systemd-run --on-active=2 /sbin/reboot' >/dev/null && echo ">>> Neustart ausgeloest"
    else
        ssh $H 'sync; systemd-run --on-active=30 /sbin/poweroff' >/dev/null
        echo ">>> faehrt in 30 s herunter: Kabel ab (ausser bei Test mit Kabel), Display aus abwarten,"
        echo "    dann mit der Taste einschalten. Danach: $0 an $art"
    fi ;;
an)
    art=${2:-kalt}; notiz=$3
    if [ "$art" = "haenger" ]; then
        echo "$(date '+%F %T');haenger;FEHLER;;;;;;;;;${notiz}" >> "$LOG"
        rm -f /tmp/startreihe_alt_id
        echo ">>> Haenger eingetragen."; exit 0
    fi
    ~/redmi-build/net.sh || { echo "$(date '+%F %T');$art;KEIN NETZ;;;;;;;;;${notiz}" >> "$LOG"; exit 1; }
    # derselbe Start darf nur einmal im Protokoll stehen
    neu=$(ssh $H 'cat /proc/sys/kernel/random/boot_id' 2>/dev/null)
    if [ -n "$neu" ] && cut -d';' -f4 "$LOG" | grep -qx "$neu"; then
        echo "HINWEIS: Dieser Start (boot_id $neu) steht schon im Protokoll - nichts eingetragen."; exit 1
    fi
    # erst pruefen, wenn der Kernel fertig ist: Treiber-Timeouts (-110) kommen erst nach ~15 s
    up0=$(ssh $H 'cut -d. -f1 /proc/uptime'); [ "${up0:-0}" -lt 30 ] && { echo ">>> warte $((30-up0)) s, bis der Start abgeschlossen ist ..."; sleep $((30-up0)); }
    r=$(ssh $H 'echo "$(cat /proc/sys/kernel/random/boot_id);$(cut -d. -f1 /proc/uptime);$(uname -r) $(uname -v | cut -d" " -f1);$(grep -ao "slot_suffix=[^ ]*" /proc/device-tree/chosen/bootargs | cut -d= -f2);$(dmesg | grep -c "Poweroff-Handler SC2730 angemeldet")"')
    c=$(ssh $H 'bash -s' < ~/redmi-tools/redmi-check.sh | grep -o "OK=[0-9]*  TEIL=[0-9]*  FEHLT=[0-9]*")
    ok=$(echo "$c" | grep -o "OK=[0-9]*" | cut -d= -f2); te=$(echo "$c" | grep -o "TEIL=[0-9]*" | cut -d= -f2); fe=$(echo "$c" | grep -o "FEHLT=[0-9]*" | cut -d= -f2)
    id=$(echo "$r" | cut -d';' -f1); alt=$(cat /tmp/startreihe_alt_id 2>/dev/null)
    erg=OK
    [ -n "$alt" ] && [ "$id" = "$alt" ] && { erg="KEIN NEUSTART?"; }
    up=$(echo "$r" | cut -d';' -f2); [ "${up:-0}" -gt 600 ] && erg="LAUFZEIT ZU LANG?"
    echo "$(date '+%F %T');$art;$erg;$r;$ok;$te;$fe;$notiz" >> "$LOG"
    echo ">>> eingetragen: $art -> $erg (Laufzeit ${up}s, Check OK=$ok TEIL=$te FEHLT=$fe)"
    rm -f /tmp/startreihe_alt_id ;;
liste|*)
    column -t -s';' "$LOG"
    echo
    echo "Zusammenfassung:"; tail -n +2 "$LOG" | cut -d';' -f2,3 | sort | uniq -c ;;
esac
