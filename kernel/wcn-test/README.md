# wcn_starttest – Testtreiber für den WCN-Kern (BT/WLAN)

Startet den integrierten WCN-Kern des UMS9230 ohne sipc, Schritt für Schritt nach Realme
`wcn_integrate_boot.c` (siehe Kopf von `wcn_starttest.c` und TREIBER_QUELLEN.md).

Bauen (gegen den eigenen Kernel-Baum, nach `modules_prepare` bzw. vollem Bau):
```
make KDIR=/pfad/zu/linux ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
scp wcn_starttest.ko root@10.42.0.27:/root/
```

Am Handy (eine Stufe pro Start, dazwischen neu starten):
```
cp wcnmodem.bin /lib/firmware/          # aus odm_a:/firmware, nur für Stufe 3
redmi-wcntest.sh 0                      # nur lesen
redmi-wcntest.sh 1                      # Strom
redmi-wcntest.sh 2                      # WCN-System an
redmi-wcntest.sh 3                      # Firmware + CPU-Start
cat /sys/devices/platform/wcntest/zustand
```
`rmmod wcn_starttest` dreht nichts zurück; ein Neustart setzt den Zustand zurück (❓ noch nicht belegt).
