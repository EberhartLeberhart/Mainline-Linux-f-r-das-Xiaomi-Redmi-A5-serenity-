# Übergabe – Redmi A5 Mainline (Stand 20.10.2026, abends)

Diese Datei ist für den nächsten Chat. Erst lesen, dann STATUS.md und TREIBER_QUELLEN.md im Repo.

## Wie wir zusammenarbeiten (wichtig)
- **Immer auf Deutsch antworten**, auch in Zwischenzeilen zwischen Arbeitsschritten. Nie ins Englische wechseln.
- Locker, freundlich, ehrlich, mit Freude an der Sache – wir sind ein Team. Erfolge kurz feiern, Fehler offen zugeben.
- **Skeptisch bleiben:** Nichts gilt als „funktioniert" ohne Beleg. Vermutungen mit ❓ kennzeichnen.
- **Eine Änderung pro Test.** Neue Funktionen im Lauscher als **Parameter (Standard aus)**, damit der vorige Test
  unverändert wiederholbar bleibt. STATUS.md aktuell halten.
- Befehle immer vollständig und direkt kopierbar: keine Platzhalter wie `<Adresse>`, keine „…", keine Beispielausgabe in Befehlsblöcken.
- Code am liebsten als **Zip**, das wir dann gemeinsam Schritt für Schritt auf dem PC einrichten und testen.
- Vor dem Ausliefern selbst bauen, wenn möglich (Linux v7.1-rc1 + aarch64-Cross-Compiler, `W=1`).
- Prüfzahlen (z. B. `grep -c`) vorher **selbst nachzählen**, nicht schätzen.
- **Nie veröffentlichen:** NV-Backups, Firmware (Xiaomi/Novatek/WCN), uboot_log-Dumps, SSH-Schlüssel, Seriennummer, `~/redmi-android` (privat). GitHub-Token nur im Cache. `github_update.sh` committet nicht selbst und hat eine Sicherheitsprüfung.
  ❓ Die Firmware-Logs `wcntest_*_wcnlog.bin` vor einem Commit erst ansehen (enthalten u. a. RF-Kalibrierspuren).
- Register nie blind lesen (fremde regmap-Listen/WCN-Seite `0x51…` können einen „synchronous external abort" auslösen).
  Gemeinsamen Speicher lesen über `wcn_ram_btwf` ist ungefährlich.

## Werkzeuge und Wege
- Handy per USB-Netz: `192.168.7.2` (PC `192.168.7.1`), Helfer `redmi '<befehl>'` und `~/redmi-build/net.sh`.
- Kopieren: `scp <datei> root@192.168.7.2:/root/`
- Kernel-Baum: `~/ums9230-linux` (7.1.0-rc1, Codeberg-Stand + eigene Patches), Repo: `~/Mainline-Linux-f-r-das-Xiaomi-Redmi-A5-serenity-`
- **Testskript liegt auf dem Handy:** `/usr/local/sbin/redmi-wcntest.sh`, also immer über `redmi '…'` aufrufen.
  Es lädt `/root/wcn_lauscher.ko` (Variable `LAUSCHER`) und `/root/wcn_starttest.ko`.
- **Bauen:** `cd ~/Mainline-Linux-f-r-das-Xiaomi-Redmi-A5-serenity-/kernel/wcn-test && make` (KDIR ist seit 20.10.
  standardmäßig `~/ums9230-linux`; für Warnungen `make W=1`)
- Nach einem Neustart des Handys fehlt oft die PC-Seite des USB-Netzes (`ping` geht nicht, `enx…` ohne IP):
  erst `~/redmi-build/net.sh`, dann `scp`. Das Testskript wartet selbst darauf.
- Auf dem Handy fehlt `strings` (Paket binutils). Firmware-Log am PC auswerten.
- Serielle USB-Konsole `/dev/ttyACM0` existiert, **dort läuft aber keine Anmeldung**.
- Realme-C33-Quelle (gleicher Chip, GPL): github.com/Kiciuk/unisoc-5.4, auf dem PC als `~/realme-c33-kernel`.

## Stand WLAN/BT (WCN)
### 20.10.: erster WLAN-Befehl beantwortet ✅
Test (nach Neustart des Handys):
```
redmi 'LAUSCHER_PARAM="kanal=8 antworten=1 at=1 alle_oeffnen=1 kanal11=1 kanal12=1 kanal7=1" redmi-wcntest.sh 3'
redmi 'echo sync_version > /sys/devices/platform/wcn-lauscher/wlan_befehl; sleep 2; dmesg | grep -E "Kanal 7|K7 |wcnlog" | tail -40'
```
- Kanal 7 als sblock (tx 4×2048, rx 8×2048, WCN 0x2e0000); die Firmware bestätigt den Aufbau im Log bis aufs Byte.
- `WIFI_CMD_SYNC_VERSION` (Nr. 9): Kopf 12 Bytes (`0x10` = CMD + Antwort erwünscht, cmd_id, le16 plen, le32 mstime,
  status, rsp_cnt, 2 reserviert), danach `main_ver = 1` + 256 Bytes API-Tabelle aus Realme `api_version.c`.
- Firmware-Log: `[RECV_CMD][0][id = 9][WIFI_CMD_SYNC_CMD_VERSION]`, Antwort auf Kanal 7: Befehl 9, unser Zeitstempel,
  Status 0, ihre API-Tabelle. Warnung zu Befehl 36 (`WIFI_CMD_FLUSH_SDIO`) ist harmlos (nur SDIO).
- Antwort-Kopf hat `rsp 0`; Zuordnung zum Befehl über cmd_id + mstime (wie Realme).

### Durchbruch 19.10.: erste AT-Antwort vom WCN-Kern ✅
Test (nach Neustart des Handys):
```
redmi 'LAUSCHER_PARAM="kanal=8 antworten=1 at=1 alle_oeffnen=1 kanal11=1 kanal12=1" redmi-wcntest.sh 3'
redmi 'echo "at+spatgetcp2info" > /sys/devices/platform/wcn-lauscher/at_befehl; sleep 2; cat /sys/devices/platform/wcn-lauscher/at_antworten'
```
Antwort (Ring 5, 105 Bytes):
`Platform Version:WCN_TRUNK_22A_W25.03.4 Project Version:uww2631_qogirL6 HW Version:.... 01-16-2025 19:10:48`
Protokoll auf dem Handy: `/root/wcntest_2026-10-19_2210_stufe3.log` (Handy-Uhr geht ~1 h 17 min vor).

### Was die Firmware nach dem Start will (belegt)
Sie öffnet der Reihe nach die Kanäle **4, 5, 7, 11, 12** und wartet jeweils auf unser OPEN, bei sbuf-Kanälen danach
auf DONE SBUF_INIT. **AT antwortet erst, wenn 4, 5, 11 und 12 fertig sind.** Kanal 7 (sblock) ist für AT nicht nötig.

| Kanal | Zweck | Art (Realme wcn_sipc.c) | Unser Puffer (WCN-Sicht) | Stand |
|---|---|---|---|---|
| 4 | AT/BT/FM | sbuf, 16 Ringe 0x2400/0x2400 | 0x280000 | ✅ AT läuft (Ring 5) |
| 5 | Firmware-Log | sbuf, 1 Ring 0x8000/0x30000 | 0x240000 | ✅ Log mit Klartext |
| 7 | WLAN-Befehle | **sblock**, tx 4×2048, rx 8×2048 | 0x2e0000 | ✅ seit 20.10., erster Befehl beantwortet |
| 11 | Lebenszeichen | sbuf, 1 Ring 0x400/0x400 | 0x2d0000 | ✅ Firmware-Log bestätigt `sbuf_create: channel b … 2d0000` |
| 12 | Absturzmeldungen | sbuf, 1 Ring 0x400/0x400 | 0x2d1000 | ✅ |

Gemeinsamer Speicher: `sprd,smem-info = <0x87240000 0x00240000 0x140000>` (AP, WCN, Größe).

### Weitere Belege 19.10.
- Mailbox in Ordnung: Ziel-Kern 8; jede gesendete Nachricht erzeugt eine Inbox-„Deliver"-Unterbrechung (IRQ 25),
  Anzahl gesendet = Anzahl IRQ 25.
- Ring-Köpfe im Speicher entsprechen genau Realme `sbuf_host_init()` (per `dd`/`od` aus `wcn_ram_btwf` geprüft).
- **Korrektur zur alten Übergabe:** `marlin_set_wakeup()` ist bei Realme nur ein Platzhalter (`return 0;` am Anfang),
  `btwf_sys_force_exit_deep_sleep()` wird nur beim Ein-/Ausschalten benutzt. Android weckt den WCN-Kern vor dem
  Senden **nicht** extra. Das „DSLP" (`cp2_sleep = 0x504c5344`) war Folge der fehlenden Kanäle, kein Weckproblem.
- Firmware-Log-Rahmen Typ `0x0920` enthalten Klartext ab Nutzdaten +6 (z. B. `[T:490]smsg_ch.c:sbuf_wait ...`).
  Der Lauscher zeigt sie noch als Hex, weil `druckbar()` die ersten Binärbytes mitzählt.

### Nächste Schritte
0. ✅ 20.10.: `GET_INFO` (PR #1) und `DOWNLOAD_INI` (PR #2): INI-Abschnitte 1, 2, 4 Status 0, Abschnitt 3 Status 1.
   Arbeitsweise: jede Änderung als **Branch + Pull Request**, Commit erst nach erfolgreichem Test.
1. ✅ Status 1 bei INI-Abschnitt 3 geklärt (PR #3): TLV wird angewendet, `result:0x0`. Firmware-Text (Rahmen 0x0920)
   ist jetzt immer lesbar.
2. ✅ **MAC-Adresse** (20.10.): steht in `prodnv:/wifimac.txt` (Partition `prodnv` = Androids `/mnt/vendor`,
   `mmcblk0p1`); Bluetooth-MAC in `btmac.txt`. Nicht über INI/Firmware – der Treiber setzt sie für `wlan0`
   (Realme `sprdwl_set_mac_addr`, `default_mac`) und schickt sie mit `WIFI_CMD_OPEN`. `prodnv` nur lesend einhängen,
   volle MAC nie veröffentlichen.
3. ✅ **WLAN ein/aus** (21.10., PR #4): `open` → Status 0, ctx_id 0 (Antwort ohne Nutzdaten, ctx steht im Kopf),
   Firmware stellt auf Station und öffnet die **Datenkanäle 8 und 9** (Lauscher `datenkanaele=1`, nur Empfang).
   `close` → Status 0, Firmware fährt sauber herunter. Ganze Kette mit Übersicht: `werkzeuge/redmi-wlantest.sh`
   (Handy vorher neu starten; Log in `~/redmi-logs/`, MAC halb ausgeblendet).
4. Als Nächstes `WIFI_CMD_SCAN` (Nr. 11) im Lauscher: Ergebnisse kommen als Ereignis auf Kanal 7 ❓.
5. Danach den Realme-WLAN-Treiber `drivers/unisoc_platform/wlan/marlin3_sipc` portieren → `wlan0` → Scan.
   Der sipc-Teil aus `wcn_lauscher.c` wird dafür zu einem richtigen sipc-Kern (Plan `kernel/modem-sipc/`).

## Nebenbefunde
- `redmi-licht.service` hing am 18./19.10. ~24 h im Start und blockierte das Herunterfahren. Ursache unbekannt ❓.
  `/etc/redmi/licht.conf` steht auf `START=aus`.
- Jede Minute `sc27xx-fgu sc2730-fgu.4.auto: get charger status error` ❓.
- **Dateisystem:** `/dev/mmcblk0p81` (Root) „clean with errors", **nur Inode 8101 = `/var/log/wtmp`** betroffen,
  erster Fehler schon beim frühen Aufsetzen (Zeitstempel 19.04.2024 = Abbild-Datum). Harmlos; aufräumen später
  (umbenennen, dann `e2fsck` offline). `Maximum mount count -1` → es prüft nie automatisch.

## Aufräumen später (gesammelt)
- `/var/log/wtmp` (siehe oben).
- `wcn_lauscher.c` später in einen richtigen sipc-Kern überführen (Plan in `kernel/modem-sipc/`).
- Erledigt 20.10.: Makefile-`KDIR`, veralteter Kopfkommentar im Lauscher, README/STATUS aktualisiert.

## Zielbild (vom 19.10.)
- Erst der Kernel mit **kompletter Hardware** (WLAN, BT, Modem ohne Trusty-Teil, Kameras, Ton, Sensoren, UKW-Radio –
  FM hängt am selben WCN-Kern, Kanal 4 Ringe 13/14), **dann** die Oberfläche.
- Am Ende **ein Kernel, zwei Abbilder:** Server-Variante (schlank, ohne Oberfläche) und Benutzer-Variante.
- **Installer, Releases, fertige Abbilder erst, wenn der Kernel die Hardware ordentlich kann** (sonst nur
  Beschwerden). Bis dahin heißt „professionell“: saubere Treiber, belegte Tests, Doku, Pull Requests.
- Benutzer-Variante: eigene „Mint-Variante“ auf Ubuntu-arm64-Basis (Mint selbst gibt es nur für x86_64; Cinnamon
  gibt es für arm64, die Mint-eigenen Pakete selbst bauen, PC-spezifische Werkzeuge weglassen). Auf dem Handy eine
  Touch-Oberfläche (z. B. Phosh), am großen Bildschirm Cinnamon – vermutlich über das USB-Netz (VNC/RDP) oder einen
  DisplayLink-Adapter, weil die USB-Buchse wohl kein Bild ausgeben kann ❓.

## Entscheidungen (21.10.)
- **Funk-Firmware (WCN, Modem) und Sendeleistung werden nicht verändert.** Wer mehr Leistung will, kauft ein Gerät,
  das das kann und darf.
- **LTE** wartet auf die Antwort von Bundesnetzagentur/Hersteller (Anfrage noch zu stellen).
- **Übertakten** (CPU/RAM über SPL/SML; Secure-Boot-eFuses nicht gebrannt, BROM-Rettung möglich) ist eine Idee für
  **nach** WLAN – mit dem zweiten Redmi als Testgerät und besserer Kühlung.
- **Sicherung** aller Partitionen außer `userdata` auf den PC: Skript `redmi-sicherung.sh` (nur lesend, SHA256 auf
  beiden Seiten). Gerätedaten, **nie ins Repo**.

## Andere Stände (aus dem Repo)
- Modem: Firmware nicht signiert, trusty antwortet; **trusty-Teil bewusst zurückgestellt** bis zur rechtlichen
  Klärung. Nicht umgehen, auch nicht anbieten. Alles andere (sipc, Gerätebaum, Laden bis vor trusty) darf vorbereitet
  werden. Anfrage an Bundesnetzagentur/Xiaomi ist **geplant, noch nicht gestellt** – an einem eigenen Tag gemeinsam
  formulieren.
- `kernel/modem-sipc/`: Plan für den sipc-Port (smem → sipc → smsg → sbuf → sblock), Vorlage mu300-linux.
- Ziel/Motivation: Redmi A5 als günstiges, offenes Linux-Multitool (Anregung: Flipper One), plus Server.
