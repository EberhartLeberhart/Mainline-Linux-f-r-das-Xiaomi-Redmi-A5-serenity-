# wcn-test – Testtreiber für den WCN-Kern (WLAN/BT/FM/GNSS)

Zwei Out-of-tree-Module für den integrierten WCN-Kern des UMS9230:

- **`wcn_starttest.c`** startet den WCN-Kern ohne sipc, Schritt für Schritt nach Realme
  `wcn_integrate_boot.c` (siehe Kopf der Datei und [TREIBER_QUELLEN.md](../../TREIBER_QUELLEN.md)).
- **`wcn_lauscher.c`** meldet sich an der Mailbox (Kanal 8) an, zeigt jede sipc-Nachricht an und
  beantwortet – je nach Parameter – die Kanäle, die die Firmware öffnet („sipc-lite“).

## Bauen

Gegen den eigenen Kernel-Baum (nach `modules_prepare` bzw. vollem Bau):
```
make KDIR=~/ums9230-linux
scp wcn_lauscher.ko wcn_starttest.ko root@192.168.7.2:/root/
```
`KDIR` ist standardmäßig `~/ums9230-linux`; `ARCH=arm64` und `CROSS_COMPILE=aarch64-linux-gnu-` sind voreingestellt.

## Start in Stufen (wcn_starttest)

Am Handy (eine Stufe pro Start, dazwischen neu starten):
```
cp wcnmodem.bin /lib/firmware/          # aus odm_a:/firmware, nur für Stufe 3 – NICHT veröffentlichen
redmi-wcntest.sh 0                      # nur lesen
redmi-wcntest.sh 1                      # Strom
redmi-wcntest.sh 2                      # WCN-System an
redmi-wcntest.sh 3                      # Firmware + CPU-Start
cat /sys/devices/platform/wcntest/zustand
```
`rmmod wcn_starttest` dreht nichts zurück; ein Neustart setzt den Zustand zurück (❓ noch nicht belegt).

`redmi-wcntest.sh` liegt auf dem Handy (`/usr/local/sbin`, Vorlage in `werkzeuge/`) und lädt bei
Stufe 3 vorher `/root/wcn_lauscher.ko` mit den Parametern aus `LAUSCHER_PARAM`.

## Lauscher: Parameter

Alle neuen Funktionen sind ein eigener Parameter und standardmäßig **aus**, damit jeder ältere Test
unverändert wiederholbar bleibt.

| Parameter | Wirkung | seit |
|---|---|---|
| `kanal=8` | Mailbox-Kanal des WCN-Kerns | 09.10. |
| `antworten=1` | sipc-lite: Kanal 5 (Firmware-Log) als sbuf bereitstellen | 09.10. |
| `at=1` | Kanal 4 (AT/BT/FM, 16 Ringe) als sbuf; Dateien `at_befehl`, `at_antworten` | 19.10. |
| `alle_oeffnen=1` | jedes OPEN der Firmware quittieren (nur OPEN, kein Puffer) | 19.10. |
| `kanal11=1` | Kanal 11 (Lebenszeichen) als sbuf, 1 Ring 0x400/0x400 | 19.10. |
| `kanal12=1` | Kanal 12 (Absturzmeldungen) als sbuf, 1 Ring 0x400/0x400 | 19.10. |
| `kanal7=1` | Kanal 7 (WLAN-Befehle) als sblock, tx 4×2048, rx 8×2048; Datei `wlan_befehl` | 20.10. |

Puffer im gemeinsamen Speicher (`0x87240000`, WCN-Sicht `0x240000`):
Kanal 5 `0x240000`, Kanal 4 `0x280000`, Kanal 11 `0x2d0000`, Kanal 12 `0x2d1000`, Kanal 7 `0x2e0000`.

## AT-Kanal (Kanal 4) – erstes Gespräch mit dem WCN-Kern ✅ (19.10.)

Kanal 4 ist ein sbuf mit 16 Ringen wie bei Realme `sprdwcn/sipc/wcn_sipc.c`:
Ring 5 = AT-Befehle, 11/10 = Bluetooth, 14/13 = FM.

**Wichtig:** Die Firmware bearbeitet AT-Befehle erst, wenn die Kanäle **4, 5, 11 und 12** bereit sind.
Mit nur `at=1` kommt keine Antwort (belegt 19.10.).

Am Handy (Lauscher neu kopiert, Handy frisch gestartet):
```
LAUSCHER_PARAM="kanal=8 antworten=1 at=1 alle_oeffnen=1 kanal11=1 kanal12=1" redmi-wcntest.sh 3
echo "at+spatgetcp2info" > /sys/devices/platform/wcn-lauscher/at_befehl
sleep 2; cat /sys/devices/platform/wcn-lauscher/at_antworten
```
Belegt 19.10.: `[Ring 5, 105 Bytes] Platform Version:WCN_TRUNK_22A_W25.03.4 Project Version:uww2631_qogirL6 …`

## WLAN-Befehle (Kanal 7) – erster Befehl ✅ (20.10.)

Kanal 7 ist ein sblock (Aufbau wie Realme `sblock_host_init()`). Die Firmware bestätigt den Aufbau
in ihrem Log (`sblock_create … raddr:2e0060, rcount:4, rsize:800 …`).
```
LAUSCHER_PARAM="kanal=8 antworten=1 at=1 alle_oeffnen=1 kanal11=1 kanal12=1 kanal7=1" redmi-wcntest.sh 3
echo sync_version > /sys/devices/platform/wcn-lauscher/wlan_befehl
sleep 2; dmesg | grep -E "Kanal 7|K7 |wcnlog"
```
`sync_version` schickt `WIFI_CMD_SYNC_VERSION` (Nr. 9), den ersten Befehl des Realme-WLAN-Treibers
`marlin3_sipc`. Er ändert nichts am Funk. Belegt 20.10.: Antwort mit Befehl 9, Status 0 und der
API-Tabelle der Firmware.

Hinweis: Das Firmware-Log (`wcn_log`, `*_wcnlog.bin`) enthält Spuren der RF-Kalibrierung – vor dem
Veröffentlichen ansehen.
