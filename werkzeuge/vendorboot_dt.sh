#!/bin/bash
# vendorboot_dt.sh - vendor_boot mit EINER Device-Tree-Aenderung neu bauen.
# Basis ist der bewaehrte serenity_v12.dtb; geaendert wird nur, was als Argumente kommt.
#
#   ~/redmi-build/vendorboot_dt.sh <name> <knoten> <eigenschaft> <wert> [...weitere Dreiergruppen]
#   Beispiel: ~/redmi-build/vendorboot_dt.sh cpufreq /cpufreq status okay
#   Zahl:     ~/redmi-build/vendorboot_dt.sh pmic0 /cpufreq status okay /cpufreq/cluster@1 sprd,pmic-type u:0
#   Ergebnis: ~/redmi-build/vendor_boot_<name>.img  ->  ~/redmi-build/deploy.sh vendor_boot_<name>.img
set -e
cd ~/redmi-build
NAME=$1; shift
[ -n "$NAME" ] && [ $(( $# % 3 )) -eq 0 ] && [ $# -gt 0 ] || { sed -n 2,7p "$0"; exit 1; }
BASE=~/redmi-unlock-work/serenity_v12.dtb
VB=vendor_boot_usb2.img
MKB=~/mkbootimg-aosp
WRAP=$(ls ~/redmi-build/wrap_dtb.py ~/redmi-unlock-work/wrap_dtb.py ~/wrap_dtb.py 2>/dev/null | head -1)
command -v fdtput >/dev/null || { echo "FEHLER: fdtput fehlt -> sudo apt install device-tree-compiler"; exit 1; }
[ -f "$BASE" ] && [ -f "$VB" ] && [ -n "$WRAP" ] || { echo "FEHLER: $BASE, $VB oder wrap_dtb.py fehlt"; exit 1; }
W=$(mktemp -d)

# 1) Kette pruefen: ergibt v12 verpackt genau den Device-Tree im heutigen vendor_boot?
python3 $MKB/unpack_bootimg.py --boot_img $VB --out $W/alt --format=mkbootimg > $W/args.txt
python3 "$WRAP" "$BASE" $W/v12_table.dtb >/dev/null
if cmp -s $W/v12_table.dtb $W/alt/dtb; then
    echo ">>> Kette bestaetigt: serenity_v12.dtb + wrap_dtb.py = Device-Tree in $VB"
else
    echo "FEHLER: serenity_v12.dtb verpackt ist NICHT der Device-Tree in $VB - Basis unklar, Abbruch."
    exit 1
fi

# 2) genau die gewuenschten Aenderungen setzen
cp "$BASE" $W/neu.dtb
while [ $# -gt 0 ]; do
    knoten=$1; eig=$2; wert=$3; shift 3
    # Zahlen (u32) mit "u:" davor, z.B. u:0 - sonst wird der Wert als Text geschrieben
    case "$wert" in u:*) typ=u; wert=${wert#u:} ;; *) typ=s ;; esac
    # fdtput liest Zahlen nur dezimal - 0x38 wuerde still zu 0! Deshalb hier umrechnen.
    [ $typ = u ] && wert=$(for n in $wert; do printf "%d " "$n"; done)
    # fehlender Knoten wird angelegt - steht dann deutlich in der Ausgabe und im Vergleich
    if ! fdtget -p $W/neu.dtb "$knoten" >/dev/null 2>&1; then
        fdtput -p -c $W/neu.dtb "$knoten" && echo ">>> NEUER Knoten angelegt: $knoten"
    fi
    vorher=$(fdtget -t $typ $W/neu.dtb "$knoten" "$eig" 2>/dev/null || echo "(nicht gesetzt)")
    if [ $typ = u ]; then fdtput -t u $W/neu.dtb "$knoten" "$eig" $wert   # mehrere Zahlen erlaubt
    else fdtput -t s $W/neu.dtb "$knoten" "$eig" "$wert"; fi              # Text mit Leerzeichen bleibt ein Text
    echo ">>> $knoten $eig: $vorher -> $(fdtget -t $typ $W/neu.dtb "$knoten" "$eig")"
done

# 3) Gegenprobe: ausser den Aenderungen ist alles gleich
dtc -I dtb -O dts -q "$BASE" > $W/a.dts; dtc -I dtb -O dts -q $W/neu.dtb > $W/b.dts
echo ">>> Unterschiede im Device-Tree:"; diff $W/a.dts $W/b.dts | sed 's/^/    /' || true

# 4) verpacken und vendor_boot mit denselben Parametern neu bauen
python3 "$WRAP" $W/neu.dtb $W/neu_table.dtb >/dev/null
ARGS=$(sed "s|--dtb [^ ]*|--dtb $W/neu_table.dtb|" $W/args.txt)
eval python3 $MKB/mkbootimg.py $ARGS --vendor_boot vendor_boot_$NAME.img
cp $W/neu.dtb serenity_$NAME.dtb

# 5) Gegenprobe am fertigen Image: nur der Device-Tree darf sich unterscheiden
python3 $MKB/unpack_bootimg.py --boot_img vendor_boot_$NAME.img --out $W/neu >/dev/null
for f in $(cd $W/alt && ls); do
    [ "$f" = dtb ] && continue; [ -d "$W/alt/$f" ] && continue
    cmp -s $W/alt/$f $W/neu/$f || echo "!! WARNUNG: $f unterscheidet sich ebenfalls"
done
cmp -s $W/neu/dtb $W/neu_table.dtb && echo ">>> Device-Tree im neuen Image ist der geaenderte" || echo "!! Device-Tree im Image stimmt nicht"
echo ">>> fertig: ~/redmi-build/vendor_boot_$NAME.img  (Rettung: $VB)"
rm -rf $W
