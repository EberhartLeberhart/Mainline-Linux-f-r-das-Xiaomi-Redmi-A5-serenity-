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
| `datenkanaele=1` | Kanäle 8 und 9 (WLAN-Daten) als sblock bereitstellen, wenn die Firmware sie nach `open` öffnet; nur Empfang | 21.10. |

Puffer im gemeinsamen Speicher (`0x87240000`, WCN-Sicht `0x240000`):
Kanal 5 `0x240000`, Kanal 4 `0x280000`, Kanal 11 `0x2d0000`, Kanal 12 `0x2d1000`, Kanal 7 `0x2e0000`,
Kanal 8 `0x2f0000` (tx 64×1664, rx 192×1664), Kanal 9 `0x360000` (tx 64×1664, rx 8×1664).

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

`get_info` schickt `WIFI_CMD_GET_INFO` (Nr. 1, liest nur). Die Antwort wird entschlüsselt ausgegeben
(`dmesg | grep GET_INFO`), die MAC-Adresse nur zur Hälfte, damit Logs teilbar bleiben.
Belegt 20.10. (ohne INI): Status 0, Chip `0x2355`, Firmware `0xf13b`, 5 GHz, WPA3; MAC noch leer.

`download_ini` liest `/lib/firmware/wifi_board_config.ini` und schickt sie wie Realme in 3–4 Abschnitten
(`WIFI_CMD_DOWNLOAD_INI`, Nr. 76, je mit CRC16). Parser und Strukturen stehen in `wcn_ini.h`
(aus Realme `rf_marlin3.c`, GPL, mit `SIPC_SUPPORT`). Die INI ist eine Gerätedatei aus `odm:/firmware`
und kommt **nicht ins Repo**:
```
sudo mount -o ro,loop odm_a.img /mnt/odm && cp /mnt/odm/firmware/wifi_board_config.ini ~/redmi-android/firmware/ && sudo umount /mnt/odm
scp ~/redmi-android/firmware/wifi_board_config.ini root@192.168.7.2:/lib/firmware/
```
Reihenfolge wie Realme: `sync_version`, `download_ini`, `get_info`.
Belegt 20.10.: Abschnitte 1, 2, 4 Status 0, Abschnitt 3 (`rf_config`, TLV) Status 1 mit `result:0x0` –
die Firmware liest und wendet ihn an (Pfadverlust 2,4/5 GHz), die 1 ist kein Fehler.

`open` schickt `WIFI_CMD_OPEN` (Nr. 3, Modus Station) mit der Geräte-MAC aus `/lib/firmware/wifimac.txt`
(von `prodnv`, **nicht ins Repo**; fehlt sie, nimmt der Lauscher eine Zufalls-MAC). `close` schickt
`WIFI_CMD_CLOSE` (Nr. 4) mit der ctx_id aus der OPEN-Antwort.
Belegt 21.10.: beide Status 0; nach `open` öffnet die Firmware die Kanäle 8 und 9, nach `close` schaltet sie ab.

Zu jeder Antwort schreibt der Lauscher eine Klartextzeile, z. B. `WLAN-Antwort: OPEN -> ok`
(Fehlercodes als Text). Die ganze Kette mit Übersicht macht das PC-Skript `werkzeuge/redmi-wlantest.sh`
(Handy vorher neu starten):
```
./werkzeuge/redmi-wlantest.sh                       # sync_version download_ini get_info open scan close
./werkzeuge/redmi-wlantest.sh sync_version get_info # nur diese
```
Belegt 21.10.: Kanäle 4, 5, 7, 8, 9, 11, 12 bereit, alle acht Antworten „ok“.

`scan` schickt `WIFI_CMD_SCAN` (Nr. 11) ohne SSID über 2,4 GHz Kanal 1–13, `scan5` zusätzlich über
5 GHz Kanal 36–48 (ohne DFS). Vorher muss `open` gelaufen sein. Jedes Netz erscheint als eine Zeile
`SCAN: Kanal 6, -42 dBm, WPA2/WPA3 BSSID aa:bb:cc:xx:xx:xx, "Name"`, am Ende
`WLAN-Ereignis: SCAN_DONE -> fertig, N Netze`. Beacons werden nicht roh ausgegeben (volle BSSIDs).
Belegt 21.10.: 7 Netze auf 2,4 GHz; 5 GHz wird abgefragt, liefert aber noch nichts ❓.
**Logs mit Scan-Ergebnissen enthalten die Netznamen der Nachbarn – nicht ins Repo.**
```
./werkzeuge/redmi-wlantest.sh                                                   # ... open scan close
./werkzeuge/redmi-wlantest.sh sync_version download_ini get_info open scan5 close
```

Hinweis: Im Firmware-Log blendet der Lauscher MAC-Adressen zur Hälfte aus (`d0:c1:bf:xx:xx:xx`).
Das Firmware-Log (`wcn_log`, `*_wcnlog.bin`) enthält Spuren der RF-Kalibrierung – vor dem
Veröffentlichen ansehen.
