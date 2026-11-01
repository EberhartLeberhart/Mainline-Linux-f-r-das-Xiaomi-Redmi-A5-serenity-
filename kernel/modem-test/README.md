# modem_starttest – Testtreiber für das LTE-Modem

Untersucht das Modem (PUBCP) und den Hilfskern pmsys des UMS9230, Hintergrund in `MODEM.md`.
**Stufe 0: lesen** – Steuerbits und ausgewählte Speicherbereiche, es wird nichts geschrieben.
**Stufe 1: Schreibtest** – je 16 Wörter auf einer Seite in WCN (Vergleich), pmsys-DDR, Modem-DDR und sipc
schreiben, zurücklesen, wiederherstellen. Startet nichts.
**Stufe 2: pmsys starten** (Hilfskern, noch nicht das Modem) – `pm_sys` nach `0x88040000`, Startcode nach
`0x62010000`, corereset AON `0x8c` Bit 0 löschen, 5 s beobachten. **Stufe 3:** pmsys wieder anhalten.

Für Stufe 2 vorher (Dateien bleiben auf dem Handy, nicht ins Repo):
```
# am PC: Startcode aus der vendor-Partition
python3 werkzeuge/analyse/pmsys_bootcode.py /mnt/vendor/etc/modem_sp_info.xml pmsys_bootcode.bin
scp pmsys_bootcode.bin root@192.168.7.2:/lib/firmware/
# am Handy: pm_sys-Partition als Firmware-Datei
dd if=/dev/disk/by-partlabel/pm_sys_a of=/lib/firmware/pm_sys.bin
```

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
insmod /root/modem_starttest.ko stufe=2      # pmsys starten (danach rmmod; Stufe 3 haelt es an)
```
Endet das Protokoll nach einer Zeile `lese …`, hat dieser Lesezugriff den Absturz ausgelöst (Firewall ❓);
dann diesen Bereich beim nächsten Versuch weglassen.
