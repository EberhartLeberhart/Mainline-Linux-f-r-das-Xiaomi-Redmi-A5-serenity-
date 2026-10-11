# modem_starttest – Testtreiber für das LTE-Modem

Untersucht das Modem (PUBCP) und den Hilfskern pmsys des UMS9230, Hintergrund in `MODEM.md`.
**Stufe 0: lesen** – Steuerbits und ausgewählte Speicherbereiche, es wird nichts geschrieben.
**Stufe 1: Schreibtest** – je 16 Wörter auf einer Seite in WCN (Vergleich), pmsys-DDR, Modem-DDR und sipc
schreiben, zurücklesen, wiederherstellen. Startet nichts.

Bauen (gegen den eigenen Kernel-Baum, wie `kernel/wcn-test`):
```
make KDIR=/pfad/zu/linux ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
scp modem_starttest.ko root@192.168.7.2:/root/
```

Am Handy (vorher `redmi-kmsg.sh` laufen lassen, falls ein Lesezugriff das Handy abstürzen lässt):
```
insmod /root/modem_starttest.ko             # alle Bereiche
insmod /root/modem_starttest.ko bereiche=0x3   # nur pmsys (Bit 0 iRAM, Bit 1 DDR)
dmesg | grep modemtest
rmmod modem_starttest
insmod /root/modem_starttest.ko stufe=1      # Schreibtest
```
Endet das Protokoll nach einer Zeile `lese …`, hat dieser Lesezugriff den Absturz ausgelöst (Firewall ❓);
dann diesen Bereich beim nächsten Versuch weglassen.
