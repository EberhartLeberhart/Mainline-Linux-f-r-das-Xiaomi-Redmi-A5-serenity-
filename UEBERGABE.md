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
  Die Firmware-Logs `wcntest_*_wcnlog.bin` (Binärspuren mit RF-Kalibrierung) gehören nicht ins Repo: 01.11. entfernt
  (lagen seit 09.10. drin; Prüfung: keine MAC, Seriennummer, SSID), `.gitignore` verhindert neue.
- **Erst den Chip fragen (seit 27.10.):** bevor wir an einem Baustein herumprobieren, seinen Zustand abfragen –
  genormte Status-/ID-Register (z. B. MIPI-DCS 0x0A/0x54/0xDA–0xDC, siehe `werkzeuge/handy/redmi-dcsfrage.sh`) oder was der
  Treiber selbst liest –, dann ändern, dann wieder abfragen. So wurde der Display-Fehler gefunden (Panel wach, Licht aus).
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
   `close` → Status 0, Firmware fährt sauber herunter. Ganze Kette mit Übersicht: `werkzeuge/tests/redmi-wlantest.sh`
   (Handy vorher neu starten; Log in `~/redmi-logs/`, MAC halb ausgeblendet).
4. ✅ **Erster Scan** (21.10., PR #5): `scan` (2,4 GHz 1–13) / `scan5` (+ 5 GHz 36–48). Ergebnisse als Ereignis
   `MGMT_FRAME` 0x83 (Typ 4) auf Kanal 7, Ende `SCAN_DONE` 0x82. 7 Netze auf 2,4 GHz, Sicherheit richtig erkannt.
   Offen ❓: **5 GHz** – Firmware fragt 36–48 aktiv ab (`scan_channel [36]`…), empfängt aber nichts, obwohl es in der
   Umgebung 5-GHz-Netze gibt. Ideen: `WIFI_CMD_SET_REGDOM` (Nr. 2) vor dem Scan, 5-GHz-Teil der INI/`rf_config`
   prüfen, DFS-Kanäle passiv. Außerdem einmal `report event fail` (Kanal-7-Empfangsblöcke voll ❓).
   Netznamen/BSSIDs der Nachbarn nie ins Repo.
5. ✅ **5 GHz** (22.10., PR #6): Ursache war die fehlende Ländereinstellung. Befehl `regdom` (DE-Regeln aus der
   wireless-regdb, gesetzliche Grenzen) vor `open` → `scan5` findet Kanal 36.
6. ✅ **Erste Anmeldung beim Router** (22.10., PR #6): `echo -n MeinNetz > wlan_ziel` (SSID des eigenen Netzes), dann `scan`, `connect`.
   `SET_IE` (RSN) + `CONNECT` → Ereignis CONNECT verbunden, **EAPOL Nachricht 1 auf Kanal 9** (Versatz 66 im Block),
   nach ~5 s Abmeldung Grund 15 (kein Passwort – erwartet). Testaufruf:
   `ZIEL=MeinNetz ./werkzeuge/tests/redmi-wlantest.sh sync_version download_ini get_info regdom open scan connect disconnect close`
   Rohzeilen von Kanal 8/9 (`K9 #…`) enthalten volle MACs → Log nur lokal.
7. ✅ **`wlan0` über cfg80211** (22.10., PR #7): Lauscher `wlan0=1` (braucht `kanal7=1 datenkanaele=1`).
   Vorher `insmod /root/rfkill.ko; insmod /root/cfg80211.ko` (aus `~/ums9230-linux/net/…`, gleicher Bau wie der
   Kernel), `iw` per apt. Test:
   `redmi 'insmod /root/rfkill.ko; insmod /root/cfg80211.ko; LAUSCHER_PARAM="kanal=8 antworten=1 at=1 alle_oeffnen=1 kanal11=1 kanal12=1 kanal7=1 datenkanaele=1 wlan0=1" redmi-wcntest.sh 3 > /dev/null'`
   dann `ip link set wlan0 up` und `iw dev wlan0 scan` → 9 Netze inkl. 5 GHz.
   Empfangsformat Kanal 9: Byte 1 = Versatz Ethernet-Kopf, Byte 2–3 = Länge (siehe STATUS).
   **Achtung:** `ip link`/`iw`-Ausgaben enthalten volle MACs/BSSIDs → nur gefiltert teilen.
8. ✅ **Empfang + Verbinden über cfg80211** (23.10., PR #8): `wpa_supplicant` scannt, verbindet und empfängt
   „RX message 1 of 4-Way Handshake“ über `wlan0`. Passwortdatei ohne Klartext:
   `ssh -t root@192.168.7.2 'wpa_passphrase MeinNetz | sed "/#psk=/d" > /root/wpa.conf; chmod 600 /root/wpa.conf'`
   Test: `timeout 30 wpa_supplicant -i wlan0 -D nl80211 -c /root/wpa.conf -d` (Ausgabe filtern: MACs!).
9. Als Nächstes: **Senden** über einen Datenkanal (Realme `tx_msdu_dscr`, 11 Bytes + Adresse; die Datenpakete liegen bei
   Realme in einem eigenen Paketspeicher – erst gründlich nachlesen ❓), dann **Schlüssel** (`add_key`/`set_default_key`
   → `WIFI_CMD_KEY`), dann DHCP → SSH über WLAN. Außerdem: `mgmt_frame_register` (stilllegen der Meldungen),
   Namen für Ereignisse 0xf5 (`STA_LUT_INDEX`, wird fürs Senden gebraucht ❓)/0xf9/0xa2.
10. Danach den Lauscher in einen sauberen Treiber aufteilen (sipc-Kern + WLAN) `drivers/unisoc_platform/wlan/marlin3_sipc` portieren → `wlan0` → Scan.
   Der sipc-Teil aus `wcn_lauscher.c` wird dafür zu einem richtigen sipc-Kern (Plan `kernel/modem-sipc/`).

## Nebenbefunde
- ✅ **Gelöst 23.10. (Patch 0025):** Ursache der Hänger war ein Fehler im Touch-Treiber: `_nt36xxx_boot_prepare_firmware()`
  las nach `release_firmware()` noch `fw_entry->size`/`fw_entry->data[0]` → gelegentlich Oops in
  `nt36xxx_download_firmware+0x460` (Beleg 22.10. zweimal), dabei bleibt `ts->lock` gesperrt → alles, was den Touch
  anfasst (unbind in `redmi-licht.sh`, `systemd-logind`), hängt im Kernel. Korrektur: Größe vor der Freigabe kopieren,
  WARN raus, Zeiger in Fehlerpfaden zurücksetzen. Danach Licht-/Tastendienst wieder an, 2 Neustarts zügig, kein Oops,
  `redmi-licht.sh aus/an` in < 1 s. **Gemeldet 23.10.:** Pull Request https://github.com/99degree/linux/pull/13
  (Zweig `nt36xxx`, +11/−6, Co-Autor Claude; Georges letzter Commit war vom 18.02.2025 – Antwort abwarten).
  Achtung `deploy.sh`: Standard-vendor_boot ist jetzt `vendor_boot_drm_ok.img` (der alte `vendor_boot_ok.img` vom 04.10. hat
  kein Display/Touch im DT; nach jedem neuen Kernel-Commit auch `cfg80211.ko`/`rfkill.ko`/Lauscher neu aufs Handy).
- Verlauf: `redmi-licht.service` hing am 18./19.10. ~24 h im Start und blockierte das Herunterfahren. **22.10. wieder:**
  Herunterfahren hing ~13 min; Journal: `redmi-licht.sh` reagiert nicht auf SIGTERM, **bleibt sogar nach SIGKILL**
  (also im Kernel festgehangen – vermutlich DSI/devmem- oder Touch-Treiber-Zugriff ❓). Nicht das WLAN.
  Dienst am 22.10. **abgeschaltet** (`systemctl disable`); Ursache später klären. Am selben Abend hing es noch einmal
  über **`redmi-taste.service`** (ruft beim Tastendruck `redmi-licht.sh`; auch `systemd-logind` lief in den Zeitablauf,
  Display blieb dunkel) → ebenfalls abgeschaltet. Bis zur Klärung: keine Licht-/Tastendienste.
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
- **Bootloader am Ende wieder sperren** (Beschluss 23.10.): Die fertige Installation soll sich wieder sperren
  lassen, sodass nur noch unser signiertes System startet – mindestens als Option im Installer. Zu prüfen:
  (1) eigener Schlüssel im vorhandenen Bootloader (wie `avb_custom_key`) ❓, sonst (2) eigene Bootloader-Stufe, die
  den Kernel gegen den Schlüssel des Nutzers prüft; dazu (3) Verschlüsselung der Datenpartition. **Keine** eigenen
  Schlüssel in die eFuses (endgültig, würde vermutlich die herstellersignierten Teile brechen).

## Entscheidungen (21.10.)
- **Funk-Firmware (WCN, Modem) und Sendeleistung werden nicht verändert.** Wer mehr Leistung will, kauft ein Gerät,
  das das kann und darf.
- **LTE** wartet auf die Antwort von Bundesnetzagentur/Hersteller (Anfrage noch zu stellen).
- **Übertakten** (CPU/RAM über SPL/SML; Secure-Boot-eFuses nicht gebrannt, BROM-Rettung möglich) ist eine Idee für
  **nach** WLAN – mit dem zweiten Redmi als Testgerät und besserer Kühlung.
- **Sicherung** aller Partitionen außer `userdata` auf den PC: Skript `redmi-sicherung.sh` (nur lesend, SHA256 auf
  beiden Seiten). Gerätedaten, **nie ins Repo**.

## Anfragen (am 23.10. abgeschickt, Antworten abwarten)
- **Bundesnetzagentur** (info@bnetza.de) und **Xiaomi** (service.de@support.mi.com), beide zunächst unter dem
  Pseudonym EberhartLeberhart als formlose Erkundung; für eine verbindliche Auskunft später mit vollem Namen.
- Kernfrage an die Bundesnetzagentur: Ist das Ersetzen des Betriebssystems bei unveränderter Funk-Firmware eine
  „wesentliche Veränderung“ (dann wäre man selbst Hersteller)? Dazu: Art. 3 Abs. 3 i RED, eigener Gebrauch im
  Netz, Weitergabe der Software, Bootloader wieder gesperrt.
- An Xiaomi: Nutzung der Geräte-Firmware/`modem_control`/TEE-Dienst unter Linux, offizieller Weg zum Sperren mit
  eigenem Schlüssel, GPL-Kernelquellen für serenity, Dokumentation der Schnittstellen.
- Vor dem Abschicken entscheiden: Repo-Link in der Xiaomi-Mail ja/nein (`BOOTLOADER_UNLOCK.md` ist öffentlich).

## CPU (24.10.)
- Kernkapazität und `cpu-map` korrigiert (Rezept, PR #10): 428 ×6 / 1024 ×2, Cluster richtig erkannt.
- Meldung an Codeberg **zurückgestellt** (kein Konto; wir gehen bewusst einen eigenen Weg, Codeberg/Xiaomi nur als
  Referenz). Was man melden könnte (`codeberg.org/ums9230-mainline/linux`): cpu-map-Benennung (betrifft vermutlich alle UMS9230-Geräte),
  gemessene Kapazitätswerte, dazu Patch 0017 (musb) und `sprd,pmic-type` der großen Kerne (Redmi: 0).
- Neuer belegter vendor_boot: `vendor_boot_rezept.img` vom 24.10. → nach dem Test als `vendor_boot_cpu_ok.img` sichern und
  in `deploy.sh` als Standard eintragen.
- Takt/Spannung kommen aus der SML-Firmware (SMC, je Temperatur, Güteklasse `dvfs_bin`); Übertakten bleibt Thema für nach WLAN.
- Profile als `leistung.conf`, nach Ziel benannt (`sparsam` / `ausgewogen` / `leistung`) plus Kühlung (`normal` /
  `kühlkörper`) – auch der Serverbetrieb kann Leistung brauchen. Grundsatz: Automatik (leise im Leerlauf, schnell unter
  Last), das Profil verschiebt nur ihre Neigung (Taktgrenzen, uclamp), keine festen Takte.

## GPU (24.10.)
- Mali-G57 MP1 läuft mit Panfrost + Mesa (OpenGL ES 3.1), glmark2 Score 228; Rezept-Schalter `GPU=1` ist Standard,
  belegter Stand `vendor_boot_gpu_ok.img` (Standard in `deploy.sh`). `gpu-thermal`: passiv 70 °C, kritisch 110 °C.
- Module `panfrost.ko`/`gpu-sched.ko` wurden von Hand nach `/lib/modules/$(uname -r)/kernel/drivers/gpu/drm/` gelegt
  + `depmod -a`. **Bei jedem neuen Kernel neu nötig** → nächster Schritt: `deploy.sh` mit `make modules_install`.
- 25.10.: PMIC-Regler-Treiber an (`CONFIG_REGULATOR_SC2730=y`; Codeberg hatte ihn, aber aus). Rezept `REGLER=1`
  (Standard): alle Regler außer `dcdc-gpu` im DT `disabled` + `regulator_ignore_unused` – ohne das schaltet der Kernel
  nach ~30 s alle Regler ohne `always-on` ab (eMMC!). Belegt: `vendor_boot_regler_ok.img`, `vddgpu` = 1,00 V bei 384 MHz.
- Referenz Realme (`qogirl6.dtsi`): 384/512/614,4/768/850 MHz bei 0,70/0,75/0,75/0,80/0,80 V, Güteklasse `gpu_bin`.
  `topdvfs` (Hardware-DVFS) regelt nur CPU/MM/Modem, nicht die GPU → `vddgpu` ist ein normaler Software-Regler.
- 25.10.: Taktstufen laufen. Hindernis war der Spreadtrum-Takttyp `composite`: er teilt nur den aktuellen Elterntakt
  (twpll-384m → 384/192/… MHz) und wechselt nie die Quelle. Patch 0026 fügt `sprd_comp_reparent_ops` hinzu (probiert
  alle Eltern, nimmt den höchsten Takt ≤ Wunsch) und nutzt ihn **nur** für `gpu-core-clk`. Rezept `OPP=1` (Standard):
  `/gpu-opp-table` mit 5 Stufen, alle 1,00 V, `mali-supply` = `dcdc-gpu` (phandle 112, Tabelle 113).
  Messung: 384+512 → glmark2 307; alle 5 → 460 (vorher 228), jede Stufe mit eigener Quelle, keine Panfrost-Fehler,
  max. 31 °C. Belegt: `vendor_boot_opp_ok.img`.
- `vddgpu` versorgt laut Realme-DT nur die GPU. 25.10.: alle Stufen auf **0,85 V** (Rezept `OPP_UV`, Standard 850000),
  belegt als `vendor_boot_opp850mv_ok.img`: glmark2 452, keine GPU-Fehler. Strommessung auf Akku (Kabel ab, Display aus,
  Skript `/root/strommessung.sh`): Leerlauf 54/53 mA, GPU-Last 850 MHz 262 → 253 mA. Die GPU (1 Shaderkern) ist ein
  kleiner Verbraucher, den Großteil unter glmark machen CPU und Speicher. Einzelwerte je Stufe (Android 0,70–0,80 V)
  bewusst zurückgestellt – erst den Kernel fertig machen.
- Strom messen geht nur ohne Kabel: am Kabel hält die Ladegrenze den Akku an, der Messchip zeigt ~0 mA.
- Kernelversion: jeder Kernel-Commit ändert `uname -r` → die Module müssen in den passenden Ordner. Seit 25.10. macht
  das `deploy.sh` selbst: `make modules_install` (gestrippt) → Prüfung gegen `boot_mainline.img` (sonst Abbruch VOR dem
  Schreiben) → `/lib/modules/<Version>` + `depmod`; alte eigene Ordner (`-g<Hash>`) bis auf neu, laufend und einen
  weiteren werden gelöscht. Abschalten mit `MODULE=0`. Beleg: 849 Module, 48 MB; nach dem Start laden von selbst nur
  panfrost/gpu_sched, cfg80211/rfkill, fuse, dm_mod, sha256 (+ cpufreq über den Dienst), keine Fehler.
  Vorher immer `make Image modules` bauen. Das cpufreq-Modul liegt zusätzlich in `/usr/local/lib/redmi` (Dienst, blacklist).

## Andere Stände (aus dem Repo)
- Modem: Firmware nicht signiert, trusty antwortet; **trusty-Teil bewusst zurückgestellt** bis zur rechtlichen
  Klärung. Nicht umgehen, auch nicht anbieten. Alles andere (sipc, Gerätebaum, Laden bis vor trusty) darf vorbereitet
  werden. Anfrage an Bundesnetzagentur/Xiaomi ist **geplant, noch nicht gestellt** – an einem eigenen Tag gemeinsam
  formulieren.
- `kernel/modem-sipc/`: Plan für den sipc-Port (smem → sipc → smsg → sbuf → sblock), Vorlage mu300-linux.
- Ziel/Motivation: Redmi A5 als günstiges, offenes Linux-Multitool (Anregung: Flipper One), plus Server.

## microSD (27.10.)
- Kartenleser `mmc@201a0000` läuft (Rezept `SD=1`, Standard): Werte aus Xiaomis Original-DT (`serenity_stock.dts`) und
  Overlay (`dtbo/overlay_0.dts`, fragment@36). Die beiden SD-Regler sind die einzige Ausnahme von der Regler-Sperre und
  fest auf 3,0 V. Beleg `vendor_boot_sd_ok.img`: 128-GB-SDXC, 20,8 MB/s lesend, Ein-/Ausstecken erkannt.
- ❓ Kurz nach dem Start zeigte die cd-Leitung einmal `hi` ohne Karte; im Betrieb stimmt sie (raus `lo`, rein `hi`).
  Beobachten.
- Später: UHS (SDR50/104, braucht Umschalten von `vddsdio` auf 1,8 V → dann min/max 1,8–3,0 V); Root-Dateisystem auf
  SD-Karte und Auswahl über das kexec-Startmenü (Idee des Nutzers, „von SD-Karte booten“).

## Ideen und Pläne (28.10.)
- Startmenü („BIOS“) mit Booten von eMMC/microSD/USB-Stick per kexec: **optional, nicht Standard**, getrennt vom
  Hauptkernel und **ohne Funkfunktionen** → [startmenue/README.md](startmenue/README.md).
- Kernel-Updates als signiertes Paket mit A/B-Rückfall → [ideen/updates-als-paket.md](ideen/updates-als-paket.md).
- Fingerabdrucksensor (verbaut, seitlich) als Mini-3D-Scanner → [ideen/fingerabdruck-profilometer.md](ideen/fingerabdruck-profilometer.md).
- Lizenzen: [LIZENZEN.md](LIZENZEN.md) (Kernel/Werkzeuge GPL-2.0-only, Doku CC BY-SA 4.0, Bilder je Ordner).

