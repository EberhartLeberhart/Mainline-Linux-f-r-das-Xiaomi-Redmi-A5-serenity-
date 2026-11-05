#!/bin/sh
# USB-Gadget: Netzwerk (NCM) + serielle Konsole (ACM), mit Diagnose auf dem Bildschirm
log() { echo "<0>REDMI-USB: $*" > /dev/kmsg; }

G=/sys/kernel/config/usb_gadget/redmi
if [ ! -d $G ]; then
    mkdir -p $G && cd $G || exit 1
    echo 0x1d6b > idVendor
    echo 0x0104 > idProduct
    mkdir -p strings/0x409
    echo "redmi-a5"          > strings/0x409/serialnumber
    echo "EberhartLeberhart" > strings/0x409/manufacturer
    echo "Redmi A5 Linux"    > strings/0x409/product
    mkdir -p configs/c.1/strings/0x409
    echo "NCM+ACM" > configs/c.1/strings/0x409/configuration
    mkdir -p functions/ncm.usb0 functions/acm.GS0
    ln -s functions/ncm.usb0 configs/c.1/
    ln -s functions/acm.GS0 configs/c.1/
fi
cd $G

log "Rollen-Schalter: $(ls /sys/class/usb_role 2>/dev/null | tr '\n' ' ')"
for r in /sys/class/usb_role/*/role; do
    [ -e "$r" ] || continue
    log "  $r vorher: $(cat $r)"
    echo device > "$r"
    sleep 1
    log "  $r nachher: $(cat $r)"
done

for i in $(seq 1 20); do
    UDC=$(ls /sys/class/udc 2>/dev/null | head -1)
    [ -n "$UDC" ] && break
    sleep 1
done
log "UDC: '$UDC'  Zustand: $(cat /sys/class/udc/$UDC/state 2>/dev/null)"
echo "$UDC" > UDC && log "an UDC gebunden" || log "FEHLER beim Binden"
sleep 3
log "Zustand nach 3s: $(cat /sys/class/udc/$UDC/state 2>/dev/null)  Speed: $(cat /sys/class/udc/$UDC/current_speed 2>/dev/null)"
dmesg | grep -i -E "musb|phy|role|udc|sprd-usb|64900000" | tail -12 | while read -r l; do log "k: $l"; done
log "fertig"
