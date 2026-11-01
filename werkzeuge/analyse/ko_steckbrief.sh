#!/bin/bash
# ko_steckbrief.sh - Steckbrief fuer Android-Kernelmodule (.ko): was steckt drin, woran haengt es sich,
# welche Quelldateien/Firmware nennt es. Nur lesen. Ausgabe enthaelt KEINEN Code, nur Namen/Texte.
#
#   bash ko_steckbrief.sh modul1.ko [modul2.ko ...] > steckbrief.txt
set -u
NM=$(command -v aarch64-linux-gnu-nm || command -v nm)
for ko in "$@"; do
    [ -f "$ko" ] || { echo "### fehlt: $ko"; continue; }
    echo "################################################################"
    echo "### $(basename "$ko")   ($(stat -c %s "$ko") Bytes)"
    for f in description author license version depends; do
        v=$(modinfo -F $f "$ko" 2>/dev/null | tr '\n' ' ')
        [ -n "$v" ] && printf "%-12s %s\n" "$f:" "$v"
    done
    # DT-Kennungen (alias of:N*T*C<compatible>) und Plattform-/Bus-Namen
    modinfo -F alias "$ko" 2>/dev/null | sort -u | head -20 | sed 's/^/alias:       /'
    strings -n 6 "$ko" | grep -E '^[a-z0-9-]+,[a-z0-9,._-]+$' | sort -u | head -25 | sed 's/^/kennung?:    /'
    # Quelldateien und Firmware-Namen, wie sie im Modul als Text stehen
    strings -n 6 "$ko" | grep -E '\.(c|h):?[0-9]*$|^[a-zA-Z0-9_./-]+\.c$' | sort -u | head -20 | sed 's/^/quelle:      /'
    strings -n 6 "$ko" | grep -E -i '\.(bin|fw|img|dat)$|firmware|_fw' | sort -u | head -15 | sed 's/^/firmware?:   /'
    # eigene Funktionen (Anzahl + Auswahl) und benoetigte fremde Funktionen
    if [ -n "$NM" ]; then
        d=$($NM --defined-only "$ko" 2>/dev/null | awk '$2 ~ /[tT]/ {print $3}' | grep -v '^\$\|^__' | sort -u)
        u=$($NM --undefined-only "$ko" 2>/dev/null | awk '{print $2}' | sort -u)
        echo "funktionen:  $(echo "$d" | grep -c .) eigene, $(echo "$u" | grep -c .) fremde"
        echo "$d" | head -60 | tr '\n' ' ' | fold -w 110 | sed 's/^/  eigen:     /'
        echo "$u" | grep -v -E '^(_|mem|str|kmalloc|kfree|devm_|dev_|printk|_printk|mutex|spin|__)' | head -40 | tr '\n' ' ' | fold -w 110 | sed 's/^/  braucht:   /'
    fi
done
