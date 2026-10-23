# Redmi A5 (serenity) – Hardware-Status

Stand: 20.10.2026 · Kernel 7.1.0-rc1-g99c0890089ba #78 (Slot b) · Konfiguration `arch/arm64/configs/redmi_a5_defconfig`

## Regeln

1. **Eingebaut heißt nicht, dass es funktioniert.** Ein Punkt gilt erst als ✅, wenn ein Beleg ihn bestätigt: ein Befehl mit Ausgabe, ein Foto oder ein beobachtetes Verhalten.
2. **Jeder Beleg hat ein Datum.** Nach jedem neuen Kernel werden alle ✅ neu geprüft. Was nicht neu bestätigt ist, wird zu ❓.
3. **Vor jedem Kernel-Bau** wird `redmi-check.sh` laufen gelassen und die Ausgabe gesichert (Ausgangslage). **Nach dem Flashen** läuft es wieder, und beide Ausgaben werden verglichen. Was vorher ging und jetzt nicht mehr, ist ein Rückschritt und wird zuerst geklärt.
4. **Auch Vermutungen aus dem Chat sind ❓**, bis sie geprüft sind. Eigene Annahmen werden genauso skeptisch behandelt wie fremde.
5. Ein Test, der nichts findet, beweist nichts. Erst muss sicher sein, dass der Test selbst funktioniert (Beispiel: `find /proc/device-tree` ohne `/` am Ende folgt dem Link nicht und findet deshalb nie etwas).
6. **Eine Änderung pro Test.** Sonst weiß man bei einem Hänger nicht, welche schuld war.
7. **Ein Start ist erst belegt, wenn er protokolliert ist**: `startreihe.sh` prüft `boot_id`, Laufzeit, Kernel und Check-Ergebnis und schreibt alles in `startreihe.csv`.

**Prüfskript selbst:** Bisher 6 falsche OKs gefunden und behoben (Power-Domains, USB-Host 2×, ums9230-clk, DRM; dazu ein Zeitfehler: vor ~15 s Laufzeit fehlen die -110-Fehler noch). Auch das Prüfskript wird geprüft.

Legende: ✅ belegt · 🟡 teilweise / eingeschränkt · ❌ fehlt oder kaputt · ❓ behauptet, aber nicht geprüft

## Durchbruch vom 19./20.10.: WCN-Firmware spricht mit Mainline (AT und erster WLAN-Befehl)

**Befund vorher (19.10.):** Kanal 4 (AT) wurde aufgebaut, der AT-Befehl lag nachweislich im Ring, die Mailbox hat ihn zugestellt (jede gesendete Nachricht = eine Inbox-„Deliver“-Unterbrechung, IRQ 25) – aber `txbuf_rdptr` blieb 0, danach `cp2_sleep = DSLP`.
**Ursache (belegt):** Die Firmware öffnet nach dem Start der Reihe nach die Kanäle **4, 5, 7, 11, 12** und wartet auf unsere Antworten (OPEN, dann DONE SBUF_INIT bzw. SBLOCK_INIT). Erst wenn **4, 5, 11 und 12** fertig sind, bearbeitet sie AT-Befehle. Kanal 7 ist dafür nicht nötig.
**Korrektur einer falschen Spur:** `marlin_set_wakeup()` (Realme) ist nur ein Platzhalter (`return 0;`), Android weckt den WCN-Kern vor dem Senden **nicht** extra. Das „DSLP“ war Folge der fehlenden Kanäle.

| Kanal | Zweck | Art (Realme `wcn_sipc.c`) | Puffer (WCN-Sicht) | Stand |
|---|---|---|---|---|
| 4 | AT/BT/FM | sbuf, 16 Ringe 0x2400/0x2400 (Ring 5 = AT) | 0x280000 | ✅ AT läuft |
| 5 | Firmware-Log | sbuf, 1 Ring 0x8000/0x30000 | 0x240000 | ✅ Klartext im dmesg |
| 7 | WLAN-Befehle | sblock, tx 4×2048, rx 8×2048 | 0x2e0000 | ✅ senden und empfangen |
| 11 | Lebenszeichen | sbuf, 1 Ring 0x400/0x400 | 0x2d0000 | ✅ |
| 12 | Absturzmeldungen | sbuf, 1 Ring 0x400/0x400 | 0x2d1000 | ✅ |

Gemeinsamer Speicher: `sprd,smem-info = <0x87240000 0x00240000 0x140000>` (AP, WCN, Größe).

**Belege:**
- 19.10.: `at+spatgetcp2info` → `Platform Version:WCN_TRUNK_22A_W25.03.4 Project Version:uww2631_qogirL6 … 01-16-2025 19:10:48` ✅
- 19.10.: Firmware-Log bestätigt `sbuf_create: channel b, sbuf_res->smem_addr is 2d0000`
- 20.10.: Firmware-Log bestätigt den sblock von Kanal 7 bis aufs Byte (`raddr:2e0060, rcount:4, rsize:800, rblks:2e6060 …`, Pool `rwrptr:4` / `twrptr:8`)
- 20.10.: **erster WLAN-Befehl** `WIFI_CMD_SYNC_VERSION` (Nr. 9, wie beim Start des Realme-Treibers `marlin3_sipc`) → Firmware: `[RECV_CMD][0][id = 9][WIFI_CMD_SYNC_CMD_VERSION]`, Antwort auf Kanal 7: Befehl 9, unser Zeitstempel, **Status 0**, dazu ihre API-Tabelle ✅. Einzige Warnung: Befehl 36 (`WIFI_CMD_FLUSH_SDIO`) kennt die Firmware nicht – betrifft nur SDIO, harmlos.

- 20.10.: **zweiter WLAN-Befehl** `WIFI_CMD_GET_INFO` (Nr. 1) **ohne INI-Download** → Status 0 ✅. Firmware-Log: `[GET_INFO]: chip_model:0x2355, fw_version:0xf13b`, `feature: 0x00120f7f, IEEE_std: 0x0000007f` – deckt sich mit unserer Entschlüsselung.
  Steckbrief: Chip `0x2355` (Marlin3-WLAN), Firmware `0xf13b`, 802.11d/e/k/r/u/v/w, **5 GHz** (Fähigkeit Bit 0), WPA3/SAE und OWE (`0x543`), AP-Modus bis 10 Clients, HT-Cap `0x16f`, VHT-Cap `0x03b07031`. **MAC-Adresse leer** – kommt unter Android vom Treiber ❓ (INI oder eigene Datei). Antennen 0, RTS `0xffffff`, Frag 0 ❓ (leer ohne INI oder Versatz in `wiphy_sec2_t` falsch). Ob das Gerät 5 GHz auch wirklich nutzt (Antenne, Ländereinstellung), ❓. Hinweis: Realme schaltet 5 GHz im Treiber ab, wenn der Projektname nicht auf seiner Liste steht – das gilt für uns nicht.

- 20.10.: **INI-Download** `WIFI_CMD_DOWNLOAD_INI` (Nr. 76) mit `wifi_board_config.ini` aus `odm:/firmware` (Version 3.0, CRLF, 7415 Bytes; Gerätedatei, **nicht im Repo**). Parser, Strukturen und CRC16 aus Realme `rf_marlin3.c` mit `SIPC_SUPPORT` (`kernel/wcn-test/wcn_ini.h`): 163 Schlüssel übernommen, die übrigen „unbekannten“ Zeilen sind Abschnittsüberschriften. Abschnitte: 1 (356 B) **Status 0**, 2 (1472 B) **Status 0**, 3 (`rf_config`, 70 B) **Status 1, aber `result:0x0`** ✅ (Firmware-Log: `i-m cfg: 28`, `ini_wifidisplay(3-8)`, `[PHY][INI]tlv, passloss enalbe 1 2.4G value:0x79,5G value:0x72` – der TLV-Abschnitt wird gelesen und angewendet; die 1 ist kein Fehlercode, die sind alle negativ), 4 (Roaming, 3 B) **Status 0**, Firmware-Log bestätigt `rf_board_config_section_1_download: version.major: 3, version.minor: 0` und `wifi_parse_config_params trigger=70, delta=5, 5g_prefer=5`. Nach **jedem** Befehl meldet die Firmware `ARB.pm_arb_hw_deinit` / `PLLSetting … state:0` und schläft bis zum nächsten Event (normales Stromsparen, kein Fehler). Firmware-Textmeldungen (Rahmen 0x0920) gibt der Lauscher seit 20.10. abends immer als Text aus. GET_INFO danach unverändert (MAC leer, Antennen 0) → MAC kommt nicht aus der INI.

- 20.10.: **Herkunft der MAC-Adressen geklärt.** Die Firmware kennt die Geräte-MAC nicht (GET_INFO leer, auch nach der INI). Bei Unisoc liest Android sie aus `/mnt/vendor/wifimac.txt`; `/mnt/vendor` ist die Partition **`prodnv`** (`mmcblk0p1`, ext4). Dort belegt (nur lesend eingehängt): `wifimac.txt` (Werks-MAC, Anfang `D0:C1:BF`), `btmac.txt` (Bluetooth-MAC), `wcn/` (leer – keine gesicherte Kalibrierung), `productinfo/sensor_calibration_data` (für die Sensoren später). Der Realme-Treiber setzt die MAC beim Anlegen von `wlan0` (`default_mac`), sonst Zufalls-MAC mit `40:45:da`. Plan: der WLAN-Treiber liest `wifimac.txt` aus `prodnv` wie Android. **Volle MAC nie veröffentlichen; `prodnv` nie schreibend einhängen.**

- 21.10.: **WLAN einschalten und ausschalten** ✅. `WIFI_CMD_OPEN` (Nr. 3, Modus Station, MAC aus `wifimac.txt`) → **Status 0**, ctx_id 0. Firmware-Log: `wifi mode is: STA:2`, Roaming-Grenze −70 dBm, und sie öffnet danach selbst die **Datenkanäle 8 und 9** (sblock, tx 64×1664 / rx 192×1664 bzw. tx 64×1664 / rx 8×1664, WCN `0x2f0000` / `0x360000`; Aufbau im Firmware-Log bis aufs Byte bestätigt: `rcount:40 … rsize:680` = 64 Blöcke zu 1664 Bytes). Beide Kanäle stellt der Lauscher bereit (`datenkanaele=1`), noch ohne Daten. `WIFI_CMD_CLOSE` (Nr. 4) → **Status 0**, Firmware fährt sauber herunter (`host_cmd_close_dev`, `mcc deinit`, `ARB.power down`). Der WLAN-Teil hat **keine** AP+STA-Fähigkeit (Bit nicht gesetzt).
- 21.10.: Testkette als PC-Skript `werkzeuge/redmi-wlantest.sh`: startet den WCN-Kern, schickt `sync_version → download_ini → get_info → open → close` und zeigt eine kurze Übersicht (alle Kanäle 4, 5, 7, 8, 9, 11, 12 bereit, jede Antwort „ok“). Das ganze Log landet in `~/redmi-logs/`, MAC-Adressen sind dort und im Firmware-Log halb ausgeblendet.
- 21.10.: **erster WLAN-Scan unter Mainline** ✅. `WIFI_CMD_SCAN` (Nr. 11, ohne SSID, 2,4 GHz Kanal 1–13 als Bitmaske, optional 5 GHz 36–48) → Status 0. Die Ergebnisse kommen als Ereignis `MGMT_FRAME` (0x83, Typ 4 = Beacon/Probe-Response) auf Kanal 7, am Ende `SCAN_DONE` (0x82). Gefunden: **7 Netze auf 2,4 GHz** (Kanal 1, 6, 11, 13; −42 bis −65 dBm), WPA2, WPA2/WPA3 und offene Netze werden erkannt. Kanal 12/13 werden auch ohne Ländereinstellung abgetastet. Firmware-Log: jeder Kanal aktiv (`passive[0]`, zwei Probe-Anfragen je Kanal).
  **5 GHz ❓:** Die Firmware fragt 36, 40, 44, 48 aktiv ab, empfängt aber nichts, obwohl in der Umgebung 5-GHz-Netze funken. Ursache offen (5-GHz-Empfangsweg/Antenne, fehlendes `SET_REGDOM`, oder Nachbarn nur auf DFS-Kanälen).
  **Ein Rahmen verloren ❓:** einmal `report event fail` / `result:0x2` für `MGMT_FRAME` – vermutlich waren alle 8 Empfangsblöcke von Kanal 7 belegt, weil der Lauscher langsam (viel dmesg) zurückgibt. Im echten Treiber Blöcke sofort freigeben.
  Netznamen und BSSIDs der Nachbarn kommen **nicht** ins Repo; der Lauscher zeigt BSSIDs nur zur Hälfte und schreibt die Beacons nicht roh ins dmesg.
- 22.10.: **5 GHz geht mit Ländereinstellung** ✅. `WIFI_CMD_SET_REGDOM` (Nr. 2) mit den DE-Regeln aus der wireless-regdb (2,4 GHz 20 dBm; 5150–5250 23 dBm innen; 5250–5350 20 dBm innen, DFS; 5470–5725 27 dBm, DFS – gesetzliche Grenzen, nichts erhöht) → Status 0. Danach findet `scan5` ein Netz auf **Kanal 36** (−74 dBm); ohne `SET_REGDOM` blieb 5 GHz stumm (21.10.). Ein Test, Unterschied aber eindeutig.
- 22.10.: **erste Anmeldung bei einem Router** ✅ (eigenes Netz, WPA2-PSK/CCMP, Kanal 1, −53 dBm). Ablauf wie `wpa_supplicant`, aber **ohne Passwort**: `WIFI_CMD_SET_IE` (Nr. 25, RSN-Element für die Assoziierungsanfrage) → Status 0, `WIFI_CMD_CONNECT` (Nr. 10, 82 Bytes) → Status 0, Ereignis **`CONNECT` (0x80) Status 0 = verbunden**. Danach **erste echte Daten über den Datenpfad: viermal EAPOL-Key Nachricht 1 vom Router auf Kanal 9** (Router wiederholt mangels Antwort), EAPOL im Block ab Versatz 66 (davor ein noch unbekannter Deskriptor ❓). Dann `DISCONNECT` (0x81) **Grund 15** (4-Wege-Handschlag abgelaufen) – wie erwartet. `WIFI_CMD_DISCONNECT` (Nr. 13) und `CLOSE` → Status 0. Unbekannte Ereignisse dabei: `0xf5` (Realme `STA_LUT_INDEX`), `0xf9` (`WFD_MIB_CNT`, 168 Bytes), `0xa2` ❓.
  Damit ist klar: **Kanal 9 = Empfangsdaten**, und das WLAN kann sich anmelden. Fehlt nur noch der Schlüsselaustausch (wpa_supplicant) – dafür braucht es ein echtes `wlan0`.
- 22.10.: Kanal 7 gibt empfangene Blöcke jetzt sofort zurück und wertet erst danach aus. `report event fail` trat trotzdem noch einmal auf (beim `scan5`), beim Verbindungstest nicht ❓.
- 22.10. abends: **erstes echtes Linux-WLAN-Gerät `wlan0`** ✅ (Lauscher-Parameter `wlan0=1`, cfg80211 nach dem Muster von Mainline `virt_wifi.c`). `ip link set wlan0 up` schickt selbst `SYNC_VERSION`, INI, `SET_REGDOM` (einmalig) und `OPEN` und wartet jeweils auf die Antwort → „WLAN eingeschaltet (ctx 0)“. **`iw dev wlan0 scan` liefert 9 Netze** (2412/2437/2472 MHz und 5180 MHz), Scan-Ende an cfg80211 gemeldet. Voraussetzung: `cfg80211.ko` und `rfkill.ko` aus dem eigenen Kernelbaum (`CONFIG_CFG80211=m`, `CONFIG_RFKILL=m`) aufs Handy, `apt install iw` (holt `wireless-regdb` mit). Senden verwirft noch alles, Verbinden über cfg80211 fehlt noch.
- Datenformat Kanal 9 (Empfang, aus dem EAPOL-Test entschlüsselt, passt zu Realme `rx_msdu_desc`): Byte 0 = Pakettyp (3) + ctx, **Byte 1 = Versatz des Ethernet-Kopfs (54)**, **Byte 2–3 = Länge des Ethernet-Rahmens** (113 = 14 + 99 EAPOL), ab Byte 28 eine Kopie des 802.11-Kopfs.
- 23.10.: **`wpa_supplicant` verbindet sich über unseren Treiber** ✅. Neu im Lauscher: cfg80211-`connect`/`disconnect` (RSN-Elemente von wpa_supplicant als `SET_IE`, dann `CONNECT` mit WPA2/CCMP/PSK aus der Anfrage; Ereignisse `CONNECT`/`DISCONNECT` → `cfg80211_connect_result`/`cfg80211_disconnected`) und **Empfang**: Rahmen, die direkt im Block auf Kanal 8/9 stehen (Typ 3), gehen per `netif_rx` an `wlan0`. Ergebnis mit dem echten Passwort: Scan → „Trying to associate“ → **„Associated with …“** → **„WPA: RX message 1 of 4-Way Handshake“** → „Sending EAPOL-Key 2/4“ (wird noch verworfen, Senden fehlt) → nach ~4 s Grund 15. `wlan0` empfing 16 Pakete, 0 verworfen. Harmlose Meldungen von wpa_supplicant: „Register frame … not supported“ (Management-Rahmen, fehlt noch), „set_key failed“ (Schlüssel, nächster Schritt).

Testmodul: `kernel/wcn-test/wcn_lauscher.c`, Parameter `kanal=8 antworten=1 at=1 alle_oeffnen=1 kanal11=1 kanal12=1 kanal7=1 datenkanaele=1` (alle neuen Funktionen Standard aus), Ablauf in `kernel/wcn-test/README.md`.

## Durchbruch vom 08.10.: Netzwerk über USB-LAN-Adapter am Hub

**Befund vorher:** Adapter (Hama 00200325 = RTL8153B, Treiber r8152) wird erkannt, DHCP klappt, 56-Byte-Pings 12/12 – **ab 1000 Byte 0/12**, danach `Tx status -2`, `NETDEV WATCHDOG`, Reset. Auch mit MTU 480 kein Erfolg.
**Ursache (belegt 08.10.):** Im Host-Betrieb setzt `musb_tx_dma_program` für die Unisoc-DMA kein einziges Bit im TXCSR, `musb_ep_program` löscht vorher sogar `AUTOSET`/`DMAENAB`. Die DMA füllt den 512-Byte-FIFO, aber ohne **AUTOSET** schickt der Chip volle Pakete nie ab – nur das kurze letzte Stück. Daher: kurze Pakete gehen, alles ab zwei USB-Paketen hängt.
Gefunden durch Vergleich mit Xiaomis Kernel-Quellen: Zweig `arctic-w-oss` von MiCode/Xiaomi_Kernel_OpenSource = **Schwestergerät „UMS9230 ARCTIC Board“** (gleicher Chip, gleiche Touch-Belegung `spi3-gpio144-145`). Dort setzt `musb_host.c` nach dem DMA-Start `MODE | AUTOSET | DMAENAB | DMAMODE`.
**Lösung:** Patch 0017 (`usb: musb: host: AUTOSET/DMAMODE fuer Unisoc-DMA beim Senden setzen`, +18 Zeilen).

| Test (08.10., Handy im Hub, LAN-Kabel direkt am PC) | vorher | mit Patch 0017 |
|---|---|---|
| Ping 56 Byte | 12/12 | 12/12 |
| Ping 1000 Byte | **0/12**, Sendeschlange fest | **12/12** |
| Ping 1472 Byte (2×) | 0/12 | **12/12, 12/12** |
| Sendefehler im Kernel | Tx status −2, Watchdog, Reset | **0** |
| SSH übers LAN | nie geklappt | ✅ `ssh root@10.42.0.27` |
| 50 MB Handy → PC (über SSH) | – | **1,75 s ≈ 30 MB/s** |
| 50 MB PC → Handy (über SSH) | – | **4,72 s ≈ 11 MB/s** |

Gemessen mit Testkernel ohne cpufreq; danach Kernel ga1b678ac5cbb mit cpufreq aufgespielt und gestartet ✅ (Leistung damit noch nicht neu gemessen ❓).
Was **nicht** die Ursache war (widerlegt): MTU/Paketgröße, multipoint, CDC-Modus (cdc_ether übernimmt Realtek nicht, wenn r8152 eingebaut ist), DMA-Knotenformat (unser `sprd_dma.h` hat das V3-Format mit `reserved1` wie Xiaomi), 256-MB-Grenze (wird beachtet).

**Nebenbefunde 08.10.:**
- `CONFIG_CMDLINE_FORCE=y`: Der Kernel nimmt **nur** seine eingebaute Befehlszeile. `--cmdline` im boot.img und `vendor_cmdline` im vendor_boot wirken nicht. Kernel-Parameter gehören in `CONFIG_CMDLINE`.
- `musb_hdrc.use_dma=0` (PIO statt DMA) ❌: Das USB-Netz zum PC geht dann gar nicht. Ankommende Pakete haben die richtige Länge, aber nur Nullen (`Wrong NTH SIGN`, `HEAD: 00 00 …`). Der PIO-Weg ist kaputt, Ursache offen ❓ (Xiaomi hat keine eigenen FIFO-Funktionen).
- Laufzeit-Umschalten von `use_dma` per unbind/bind von musb-hdrc → **Kernel hängt**. Nicht wiederholen.
- Unterschied zu Xiaomi, noch ohne Folgen ❓: Wir schreiben `MUSB_DMA_CHN_ADDR_H` immer 0, Xiaomi trägt die oberen Adressbits der Knotenliste ein (Speicher über 4 GB).

## Der Durchbruch vom 02.10.: Kernel-Größe

**Ursache der rätselhaften Hänger** (Kaltstart, Logo, Treiber als `=y`): Der Kernel war zu groß.
Der Bootloader lädt den Kernel nach `0x80080000`. Der Android-Kernel belegt 48 103 424 Bytes und endet unter `0x83000000`.
Unser alter Kernel (Treiber für rund 40 ARM-Plattformen) belegte 52 887 552 Bytes und ragte darüber hinaus.
Nach einem Neustart lag dort zufällig noch der vorherige Kernel im RAM, deshalb lief es meistens.
Nach echtem Ausschalten oder bei größeren Änderungen hing der Start, noch bevor eine Zeile auf dem Display erschien.

**Lösung:** Nur noch `ARCH_SPRD` (47 Plattformen abgeschaltet, `schlank.sh`) → 27,9 MB, weit unter der Grenze.
**Belege:** Kaltstart 4/4, Warmstart 2/2, Start mit Ladekabel 1/1 (`startreihe.csv`); Logo-Kernel und Treiber-Kernel, die früher hingen, starten jetzt kalt.
**Genauer Mechanismus** (was der Bootloader oberhalb von `0x83000000` ablegt): ❓ nicht beobachtet, für die Lösung nicht nötig.

## cpufreq und Tiefschlaf (03./04.10.) – Ursache gefunden

**Befund:** Taktwechsel + Tiefschlaf der großen Kerne = Hänger (erst extrem langsam, dann tot, keine Fehlermeldung).
**Ursache (belegt 04.10.):** Der Codeberg-Device-Tree meldet der Firmware für die großen Kerne `sprd,pmic-type = 1` (externer Spannungsregler).
Dieses Handy hat keinen: Der Bootloader übergibt `power.from.extern=0`, Xiaomis Original-Device-Tree hat bei `cpufreq-clus1` `sprd,multi-supply` + `pmic-type-v2 = 0`, und Unisocs Android-Treiber wählt damit Regler **0**.
Gefunden durch Vergleich mit dem Android-Kernel (Transsion UMS9230, Linux 5.4) und `serenity_stock.dts`.

| Versuch | Ergebnis |
|---|---|
| Treiber fest eingebaut (`=y`), Start | ❌ Hänger bei ~13 s |
| Modul von Hand, Volllast / Ende der Last | ❌ Hänger (3×) |
| Tiefschlaf **aller** Kerne aus | ✅ |
| Gegenprobe: Tiefschlaf wieder an | ❌ Hänger in Runde 2 |
| Tiefschlaf nur große Kerne aus (Umgehung) | ✅ 35 min, ~26 000× Aufwachen klein |
| **`pmic-type = 0`, KEINE Sperre** | ✅ **10/10 Lastrunden + 51 min Dauertest**, große Kerne ~520× sauber aus dem Tiefschlaf aufgewacht |

**Stand:** `vendor_boot_pmic0_ok.img` (cpufreq `okay` + `cluster@1 sprd,pmic-type = 0`). `redmi-cpufreq.sh` lädt den Treiber ohne Sperre; nur bei altem Device-Tree (Wert 1) sperrt es als Sicherheitsnetz den Tiefschlaf von cpu6/7.
Weitere Unterschiede zu Xiaomi (nicht getestet, bisher nicht nötig): Xiaomi schickt für die Kerne **kein** `dvfs_bin` und keine Chip-Version.
**Offen:** Treiber wieder fest einbauen (`=y`) testen; sauberer Patch für Codeberg: Treiber wertet `power.from.extern` / `pmic-type-v2` aus wie Android.

## Start und Grundsystem

| Bereich | Status | Beleg / Notiz |
|---|---|---|
| Kaltstart (echtes Aus → Taste) | ✅ | 02.10.: mehrfach, `startreihe.csv`; vorher **nie** wirklich getestet (Handy ging nie aus) |
| Warmstart (`reboot`) | ✅ | 02.10.: 2/2 in `startreihe.csv` |
| Start mit Ladekabel | ✅ | 02.10.: kommt nach dem Ausschalten von selbst wieder, Bootloader meldet `sprdboot.mode=charger` |
| Dauerbetrieb | ✅ | 21 h Laufzeit (01.–02.10.) |
| A/B-Rettung, Fastboot | ✅ | 08.10.: `boot_ok.img` (dc21572ec) per `fastboot flash boot_b` zurückgespielt ✅; neuer Stand `boot_lan_ok.img` (ga1b678ac5cbb). Ältere Rettungs-Images: `boot_schlank_ok.img`, `boot_ziege_ok.img`, `boot_treiber_ok.img`, `boot_temp_ok.img`, `boot_pd_ok.img`; vendor_boot: `vendor_boot_usb2.img` (03.10. erneut als Rettung belegt) |
| rst_mode-Fix (keine abnormal-mode-Schleife) | ✅ | 0x40 beim ADI-Probe; nach echtem Aus ist rst_mode 0x0, Start klappt trotzdem |
| PMIC-Wachhund gestoppt | ✅ | kein Neustart alle 5 Minuten mehr |
| 8 CPU-Kerne | ✅ | 01.10.: online 0-7; Kerne lassen sich ab- und anschalten (02.10., PSCI CPU_OFF) |
| CPU-Takt (cpufreq) | ✅ | 04.10.: läuft als Modul über `redmi-cpufreq.service`, 2 Gruppen: klein 614–1612 MHz (8 Stufen), groß 768–1820 MHz (7 Stufen), Spannungen von der Firmware. Tiefschlaf aller Kerne erlaubt seit `pmic-type = 0` (siehe Abschnitt oben). Kühlung: Bremse wirkt (vorgetäuschte 80 °C → 1536 MHz). Hotplug ✅ |
| eMMC, Root auf userdata | ✅ | Ubuntu 24.04 läuft von `PARTLABEL=userdata` |
| Kernel-Größe | ✅ | 27 918 336 → 28 246 016 Bytes Platzbedarf (Grenze ≈ 48 MB) |
| Versionsstring (`uname -r`/`-v`) | ✅ | 02.10.: `g612fb8b51e06 #55` – durch den vollständigen Neubau repariert |
| kexec (Kernel startet Kernel) | ✅ | 02.10.: Testkernel per `kexec -s` + `systemctl kexec` gestartet → Plan B für beliebig große Kernel |
| redmi-boot-ok.service | 🟡 | markiert fest Slot b statt des laufenden Slots |

## Verbindung

| Bereich | Status | Beleg / Notiz |
|---|---|---|
| USB-Gadget NCM + ACM | ✅ | `usb0` 192.168.7.2, Konsole auf ttyGS0 |
| SSH | ✅ | täglich in Gebrauch |
| Internet über PC (NAT) | ✅ | `net.sh` |
| **LAN über USB-Adapter am Hub** | ✅ | 08.10.: RTL8153B (r8152, fest eingebaut, Patch 0016), DHCP über `20-lan.network` (systemd-networkd, `RouteMetric=100`). Mit Patch 0017 alle Paketgrößen, SSH, 30/11 MB/s (siehe Abschnitt oben). Firmware-Flicken `rtl_nic/rtl8153b-2.fw` liegt auf dem Handy, ob er geladen wird ❓ (keine Meldung im dmesg). Offen: DHCPv6-Prefix-Delegation abschalten. **09./10.10. Dauertest am Router (`redmi-lan-dauer.sh`) ❌:** 28 min stabil (Router/Internet je 5/5, auch 1472 Byte), aber Downloads aus dem Internet nur **0,1 MB/s** bzw. **0,0 MB/s**; nach dem 1. Download 7 neue LAN-Fehlermeldungen, ~1 min nach dem 2. Download (23:48) **Hänger** (Tastendruck nötig, kein Panic-Neustart). Verdacht: Dauer-Empfang (RX) über musb/r8152 ❓. Meldungen nicht gesichert → `redmi-kmsg.sh` |
| PC als Router fürs LAN | ✅ | 08.10.: LAN-Kabel des Adapters direkt am PC, NetworkManager-Verbindung `redmi-lan` (geteilt, eno1, 10.42.0.1, dnsmasq) → Handy bekommt 10.42.0.27. Zum Entwickeln einfacher als der Router |
| USB-Rolle automatisch | 🟡 | 07./08.10.: `redmi-usb.sh`/`.service` (Einstellung `/etc/redmi/usb.conf USB=auto|geraet|hub`). PC → Gerät (UDC `configured`), Hub mit Netzteil → nach 10 s ohne PC Host, kein Gerät am Hub nach 16 s → zurück auf Gerät, Kabel ab → Gerät. Eigene 5 V werden nie eingeschaltet. Schaltet mehrfach richtig ✅, einmal nicht umgeschaltet ❓ (Ursache offen) |
| USB-Host-Modus (LAN-Hub, Sticks) | ✅ von Hand | 04.10.: Rolle per `/sys/class/usb_role/64900000.usb-role-switch/role` = host, **5 V aus dem Ladechip** (REG01 Bit 5 OTG, Laden aus; REG08 → 0xe0 = OTG; 5,15 V, Grenze 1,2 A). DT hat **keine vbus-supply** – der Treiber schaltet die 5 V nicht selbst. USB-Stick: High-Speed 480 Mbit, **200 MB lesen mit 10,4–11,2 MB/s, 3× ohne Fehler**, vfat nur lesend eingehängt. Verbrauch mit Stick ~−180 mA, beim Lesen ~−215 mA (aus dem Akku – **Host und Laden gehen nicht gleichzeitig**). ⚠️ Belegt: PC-Kabel einstecken, solange die 5 V an sind → Handy speist ~0,2 A in den PC zurück (−325 mA). Testskript `redmi-otgtest.sh` mit Wächter: Gerät ab → 5 V nach **1,2 s** aus (belegt), dazu Stromgrenze als 2. Sicherung (nicht ausgelöst getestet ❓). Babble/Abbruch nur beim Wackeln am billigen Stick. ❓ einmal `musb-hdrc: unexpected dma_addr` nach der Rückspeisung, ohne Folgen. **05.10. Hub mit Einspeisung ✅:** Rolle host, eigene 5 V AUS, Hub (`1a40:0101`, 4 Ports, am PC als Netzteil) speist ein → REG08=0xb4, **Handy lädt (+384 mA) und ist gleichzeitig Host**; Stick hinter dem Hub (`1-1.3`) gelesen mit 10,5 MB/s, dabei weiter +354 mA. Eingangsstrom blieb 0x04 (500 mA). Umstecken auf PC-Kabel im Host-Betrieb unbedenklich (keine eigenen 5 V). `unexpected dma_addr` (05.10. 5×) kommt nur nach Umstecken im **Geräte**-Modus, nie im Host-Betrieb ❓. Seit 07.10. als Dienst, siehe „USB-Rolle automatisch“ |
| Feste MAC-Adresse fürs Gadget | ❌ | Interface-Name am PC wechselt bei jedem Start |

## Strom und Stabilität

| Bereich | Status | Beleg / Notiz |
|---|---|---|
| Neustart über ADI | ✅ | `reboot` funktioniert |
| Ausschalten | ✅ | 02.10.: eigener Handler (Register aus Unisoc `sc27xx-poweroff.c`: SLP_CTRL 0x1a48, PWR_PD_HW 0x1820), vor PSCI angemeldet; Handy bleibt aus |
| Lademodus-Erkennung | ✅ | `sprdboot.mode=charger` in den bootargs → Schalter für Handy-Modus (aus bleiben) / Server-Modus (hochfahren) |
| AP-Wachhund (sprd-wdt) | ✅ | gebunden (644e0000) |
| Power-Domains | ✅ | 02.10.: Commit 1dcd2ff05 (gpu_top, mm, pubcp, wcn) – alle Taktcontroller starten; `/audio-dsp` (agdsp) fehlt noch |
| Taktcontroller (alle 14) | ✅ | 02.10.: starten nach den Power-Domains |
| Echtzeituhr | ✅ | 04.10.: `RTC_DRV_SC27XX`, Kernel übernimmt die Zeit beim Start (`setting system clock …`). Einmal gestellt mit `timedatectl set-time` (kein `hwclock` auf dem Handy), Zeitzone Europe/Berlin (Datei vom PC kopiert, `tzdata` fehlt). **Kaltstart: Uhr stimmt auf die Sekunde.** Offen: Zeit aus dem Netz (timesyncd fehlt), Paketquellen prüfen (`util-linux-extra` nicht gefunden) |
| Akkuanzeige (FGU) | ✅ | 04.10.: Spannung (4,42 V, deckt sich mit ADC-Kanal 5), Ladestand (97 %), Strom mit richtigem Vorzeichen, **Temperatur 22,4 °C** über Xiaomis `voltage-temp-table` (eigener Patch `patch_fgu_temp.py`, DT-Eigenschaft `sprd,voltage-temp-table`) |
| Kabel-Erkennung | ✅ | 04.10.: USB-Spannung (ADC-Kanal 14) 4,86 V → 0,13 V beim Abziehen → 4,91 V beim Einstecken; Strom springt von +1 mA auf −100 mA |
| Verbrauch | ✅ gemessen | 04.10.: **~102 mA bei 4,40 V ≈ 0,45 W** im Leerlauf ohne Kabel, Display an (Hintergrundlicht vom Bootloader, nicht abschaltbar solange PWM fehlt). Reicht rechnerisch ~2 Tage mit vollem Akku · **08.10.: Licht aus + Panel schläft → ~51 mA** (halbiert) |
| Ladechip | ✅ gesteuert | 04.10.: I2C-Bus 2 läuft (`/soc/i2c@200f0000` + Alias `i2c2` nötig). **SGM41513 an 0x1a** (REG0B PN=1). Drei Schalter belegt, alle ohne Fehler: **HIZ** (REG00 Bit 7) trennt den Eingang → Handy läuft aus dem Akku (−102…−117 mA), USB-Netz bleibt; **Laden aus** (REG01 Bit 4) → 223 mA → 1 mA, Status „lädt nicht“, Handy läuft vom Kabel; **Ladespannung** (REG04) begrenzt das Nachladen. ⚠️ REG04 schlagartig weit unter die Akkuspannung → BAT_FAULT (Überspannung). Nach HIZ aus erkennt der Chip den Eingang neu und setzt den Eingangsstrom selbst auf 2400 mA (zurückstellen!). **Über Neustart:** HIZ und Laden-aus werden zurückgestellt, REG04 bleibt stehen (der Wert 0x99 ≈ 4,45 V stammt also vom alten Android). Wachhund aus. Ausschalten mit Kabel noch nicht geprüft ❓. ITERM-Wert unplausibel (❓). Kein Kernel-Treiber, Steuerung per i2cset |
| Ladegrenze (Server) | ✅ läuft | 04.10.: `redmi-akku.sh`/`.service`, Einstellungen `/etc/redmi/akku.conf` (MODUS=server, 75–80 %, Notfall < 20 %, EINGANG_MAX=500 mA). Über 80 % Strom kappen, 75–80 % ruhen (vom Kabel), darunter laden. Test auf dem Handy: ruhen → `0x01=0x0a`, entladen → `0x00=0x84`, −117 mA, Beenden stellt alles zurück. 05.10.: **Herunterfahren** beendet den Dienst sauber („Laden normal“) ✅; **nach dem Start** läuft er von selbst ✅. **Einstecken des Kabels** setzt im Chip HIZ zurück und den Eingangsstrom auf 0x17 = 2400 mA – die Erkennung dauert einige Sekunden und überschreibt eine zu frühe Korrektur noch einmal (belegt). Deshalb Chip-Prüfung alle 10 s, Eingangsstrom-Grenze, danach 10 s Nachsehen alle 2 s; Umstecken 2× korrekt abgefangen ✅. Umschalten bei 80 % → ruhen und 75 % → laden im echten Betrieb beobachtet ✅ |
| Ladestand speichern + nachkalibrieren | ✅ | 07.10.: Der FGU-Treiber übernimmt nach dem Start den im PMIC gespeicherten Ladestand – gespeichert wird aber nur durch Schreiben nach `capacity` (vorher stand nach jedem Start der Wert vom allerersten Start da). `redmi-akku.sh` speichert jetzt regelmäßig und kalibriert in Ruhe nach (|Strom| ≤ 15 mA für 15 min, Abweichung ≥ 3 Punkte → Ruhespannung über Xiaomis OCV-Tabelle aus dem DT, `calibrate` + `capacity`). Läuft auch im Handy-Modus |
| I2C-Treiber (i2c-sprd) | 🟡 | 04.10.: meldet **kein NACK** – leere Adressen liefern den zuletzt gelesenen Wert (0x08 überall). Bei Bus-Scans nie auf „antwortet“ vertrauen, immer mehrere Register vergleichen |
| Tasten | ✅ | 04.10.: Einschalttaste, Lauter, Leiser melden sich (PMIC-EIC + GPIO 124). **Einschalttaste 3 s halten = Herunterfahren** (`redmi-taste.service`), kurzer Druck tut nichts |
| PMIC SC2730 | ✅ | 04.10.: Grundtreiber, ADC (Akkuspannung ~4,42 V, Akkutemperatur ~22 °C, USB 4,86 V, Akku-ID → `bat.id=0`), eFuse, EIC |

## Temperatur

| Bereich | Status | Beleg / Notiz |
|---|---|---|
| Temperatursensoren | ✅ | 02.10.: 11 Zonen starten von selbst (`NVMEM_RMEM=y`), 25–26 °C |
| Überhitzungsschutz | 🟡 | 10 Zonen: passive 70 °C, critical 110 °C. **critical wirkt** (Abschaltung über den neuen Poweroff), **passive wirkt** (04.10.: 2 Kühlgeräte, 4 Zonen verbunden, Test mit vorgetäuschter Temperatur). `gpu-thermal` hat keine Grenze → vor dem GPU-Schritt ergänzen |

## Treiber

| Treiber | Status | Beleg / Notiz |
|---|---|---|
| gpio-sprd | ✅ | 02.10.: fest eingebaut, bindet beim Start |
| gpio-eic-sprd | ✅ | 02.10.: fest eingebaut, 4 Geräte |
| pinctrl-sprd(-ums9230) | ✅ | 02.10.: fest eingebaut, bindet beim Start |
| sprd-mailbox | ✅ | 02.10.: fest eingebaut, bindet beim Start |
| sprd-thermal | ✅ | 02.10.: fest eingebaut, 2 Geräte |
| i2c-sprd | 🟡 | 04.10.: Bus 2 läuft (mit Alias), aber kein NACK – siehe Ladechip |
| spi-sprd (SPI-Bus 3) | ✅ | 09.10.: fest eingebaut, PIO (keine DMA im DT). Nebenwirkung des Alias `spi3`: der PMIC-Bus heißt jetzt `spi4.0` statt `spi0.0` – Akku, Uhr, Tasten laufen unverändert ✅ |
| sprd-dma | ❌ | 56580000: -110, hängt an der Domain `/audio-dsp` (agdsp_pd) |
| musb-sprd (USB) mit eigener DMA | ✅ | Gerätemodus und Host-Betrieb mit DMA. Host-Senden erst seit Patch 0017 (08.10.). PIO-Weg (`use_dma=0`) kaputt ❌ |
| PWM / Backlight | ❓ | hing früher; wahrscheinlich dieselbe Größen-Ursache → neu testen |
| Hänger bei `=y` (Treiber eingebaut) | ✅ gelöst | 02.10.: Treiber-Kernel startet kalt und warm; Ursache war die Kernel-Größe |

## Anzeige und Eingabe

| Bereich | Status | Beleg / Notiz |
|---|---|---|
| Bildschirm (Bootloader-Framebuffer) | ✅ | läuft über **simpledrm** (720×1640) → Grundlage für eine Oberfläche |
| **Helligkeit** | ✅ | 06.10.: Panel regelt selbst – Xiaomi-DT `oled-backlight`: DCS **0x51**, 12 Bit (0–4095, Standard 500). DSI-Baustein `0x31100000` läuft vom Bootloader weiter (Register = Panelwerte, PHY eingerastet, Anordnung wie Mainline `sprd_dsi.c`) → Befehl per `busybox devmem` (GEN_PLD 0x70, GEN_HDR 0x6C) ✅ sichtbar. **Gemessen ohne Kabel: voll 490 mA, 500 → 119 mA, aus → 72 mA** (−40 %). Werkzeug `redmi-licht.sh` + Dienst (Server: Licht aus, Einschalttaste kurz = 30 s an) ✅ auf dem Handy belegt. Die PWM des Prozessors wirkt bei diesem Panel nicht (erklärt den früheren Fehlschlag) |
| **Panel schlafen legen** | ✅ | 08.10.: DCS Display-Off 0x28 + Sleep-In 0x10 (kurzer Befehl, Datentyp 0x05) über denselben DSI-Weg; Wecken mit Sleep-Out 0x11, 120 ms warten, Display-On 0x29 → **Bild wieder da** (beobachtet). Gemessen ohne Kabel (`redmi-paneltest.sh`, je 20 s Mittel): Licht 500 −118 mA, Licht 0 −74 mA, **Panel schläft −51 mA**, wieder wach −119 mA. In `redmi-licht.sh` eingebaut: `aus` = Licht 0 + Schlaf, `an`/`kurz`/Wert wecken vorher; `kurz 10` → wach, nach 10 s von selbst wieder Schlaf ✅. Kurzer Tastendruck und Start mit `START=aus` mit der neuen Fassung noch nicht belegt ❓. Weiter sparen ginge nur mit DPU/DSI-Takt aus (braucht Display-Treiber) |
| Panel-Daten | ✅ gelesen | 06.10. aus Xiaomis dtbo (`Panel_C3Z_42_02_0a_vid`, 8 Panels im Overlay): 720×1640, 4 Spuren, Video, RGB888, ~1,2 Gbit/s/Spur, Pixeltakt 199,2 MHz, 60/90/120 Hz (nur vfp anders), Reset-Folge, Init-, Sleep-In/Out-, CABC-Befehle. DPU `0x31000000` (qogirl6): Ebene 0 = `0x9CF2A000` ARGB8888 = simpledrm-Bereich (Gegenprobe ✅), IOMMU vermutlich aus |
| **Touch** | ✅ | 09.10.: **funktioniert.** NT36528 (TDDI, Kennung `0a 00 00 28 65 03`, per spidev gelesen) an `spi3` (`spi@20150000`, Mainline `spi-sprd`, PIO, 4 MHz), IRQ ap_gpio 144. Treiber: George Chans Mainline-nahe SPI-Fassung `nt36xxx_spi` (RFC Okt. 2024, github 99degree/linux Zweig nt36xxx, bc0b3286) + eigene NT36528-Ergänzung: Speicherkarte aus Xiaomis `novatek_nt36528a_spi_ts.ko` (Werkzeug `nvt_ko_tabelle.py`; Firmware-Adressen = NT36675), Chip-Eintrag, Kompatibel `novatek,nt36528-spi`. Treiberfehler behoben: Runtime-PM-Reste im Firmware-Download (immer −EACCES). Firmware `novatek_ts_csot_fw.bin` aus der eigenen odm-Partition (nicht frei, nur auf dem Handy in `/lib/firmware`) lädt ~17 s nach dem Start (erster Versuch vor dem Einhängen scheitert, Wiederholung nach 16 s). fwinfo: Version 9, 18×32 Leitungen, **720×1640**. evtest: Wischbahnen mit ~100–120 Hz, Koordinaten plausibel ✅. **Panel-Schlaf bringt die Touch-Firmware zum Absturz** (Puffer nur `fd` = Wächter-Alarm, ~200 Interrupts/s) → `redmi-licht.sh` meldet den Touch-Treiber vor dem Schlaf ab und nach dem Wecken neu an (Firmware frisch geladen) ✅. Kein `reset-gpios` im DT: der Treiber hielte die Leitung sonst dauerhaft LOW. **23.10.: Zuordnung geprüft** ✅ (evtest, Handy hochkant): Bereich 0–719 × 0–1639 = Bildgröße; oben rechts (703, 49), unten rechts (684, 1561), unten links (31, 1613), Mitte (352, 819) – nicht gespiegelt, nicht vertauscht, Ränder werden erkannt → keine `touchscreen-inverted/swapped`-Angaben nötig. **Mehrfinger** ✅ (3 gleichzeitig beobachtet; Treiber meldet bis zu 10, `ABS_MT_SLOT` Max 9). Offen ❓: `hw_crc=2` und `BLD_CRC_EN` vom NT36675 übernommen (läuft); Treiber prüft das Wächter-Signal ein Byte zu früh (`point[0]` statt `[1]`) und lädt daher nicht selbst neu; `truly`-Panelvariante ungetestet |
| **Echter Display-Treiber (DRM)** | ✅ 🟡 | 09.10.: **läuft.** Otto Pflügers UMS9230-DRM (DPU `sprd,ums9230-dpu`, DSI `sprd,ums9230-dsi-host`, IOMMU) war im Codeberg-Stand schon drin, nur aus (`CONFIG_DRM_SPRD`, DT `disabled`). Eigener **Panel-Treiber `panel-novatek-nt36528.c`** (`novatek,nt36528-c3z42`) nach Ottos icnl9916-Vorlage: Xiaomi sendet **keine Init-Befehle** (Panel-OTP), nur Sleep-Out/Display-On bzw. 0x53/0x28/0x10; Modus 720×1640, 199,2 MHz, h 828 / v 4010 = 60,0 Hz, 4 Spuren, 1195,2 Mbit/s; Helligkeit als Backlight-Gerät über DCS 0x51 (12 Bit). Kein Panel-Reset (TDDI: würde den Touch mit zurücksetzen). DT: `display-subsystem`, DPU (+ Bootloader-Speicher per `iommu-addresses`), DSI, Panel-Knoten, `/chosen/framebuffer@0` aus (sonst bleibt die Konsole auf simpledrm). Belege: `card0-DSI-1` connected/enabled 720x1640, Konsole und Ziege über den echten Treiber, Rausch-/Weißtest in fb, Helligkeit 100…4095 per `/sys/class/backlight/31100000.dsi.0` ✅, 2 Neustarts ohne Hänger ✅. Fehler behoben: Helligkeit vor dem Einschalten → NULL-Zeiger in `sprd_dsi_host_transfer` (systemd-backlight). **Offen ❌: Abschalten über den Treiber (fb-blank/DPMS) bekommt das Panel nicht wieder an** (3/3 Zyklen dunkel, `dpms=On`); danach hängt der Neustart (2× heute, Abwürgen nötig). Zwischenlösung: `redmi-licht.sh` schläft weiter per direktem DCS 0x28/0x10, DPU/DSI laufen weiter. Neustart bei schlafendem Panel ❓ |
| Kernel-Logo (Ziege) | ✅ | 02.10.: Ziege mittig statt Pinguine, auch beim Kaltstart; hing früher nur wegen der Größe |
| Bootlogo (Bootloader) | 🟡 | Werkzeug fertig, `logo_b` wirkt nicht → Partition klären (`logo_a`/`fbootlogo`?) – Kosmetik, niedrige Priorität |

## Funk und Multimedia

| Bereich | Status | Notiz |
|---|---|---|
| WLAN / Bluetooth / FM / GPS (Marlin3) | 🟡 | **23.10.: `wpa_supplicant` meldet sich über `wlan0` an und empfängt Nachricht 1 des Schlüsselaustauschs** ✅; 22.10.: `wlan0` mit `iw scan` ✅ (WPA2, `CONNECT` ok, EAPOL Nachricht 1 auf Kanal 9 empfangen; ohne Passwort, daher danach Abmeldung Grund 15). 5 GHz geht mit `SET_REGDOM` ✅. 21.10.: erster WLAN-Scan ✅. WLAN ein- und ausschalten ✅ (`OPEN`/`CLOSE`, Datenkanäle 8/9 aufgebaut). 20.10.: `SYNC_VERSION`, `GET_INFO`, `DOWNLOAD_INI` ✅ (Chip `0x2355`, 5 GHz, WPA3), siehe Abschnitt oben. 19.10.: alle fünf sipc-Kanäle (4, 5, 7, 11, 12) bedient, erste AT-Antwort ✅. 09.10.: WCN-Kern startet ✅ – Testtreiber `kernel/wcn-test` (Stufe 3): Firmware `wcnmodem.bin` (aus `odm_a/firmware`, **nicht signiert**, läuft ohne trusty und ohne Speichersperre) nach `0x87000000`, Einschaltfolge nach Realme `wcn_poweron_device()` → `0xF0F0F0FF` nach ~520–540 ms. Sauber abschalten und ohne Neustart wieder starten ✅ (09.10.). Verbrauch mit schlafendem WCN-Kern nicht messbar höher (< ~2 mA). Offen: WLAN-Treiber `marlin3_sipc` portieren → `wlan0`; Bluetooth (Kanal 4, Ringe 11/10), FM (Ringe 14/13). Siehe [TREIBER_QUELLEN.md](TREIBER_QUELLEN.md) |
| FM-Senden | ❓ | Behauptung aus altem Chat, **nicht belegt** |
| Mobilfunk | ❌ 🟡 | Ziel: Ersatz-Internet. 10.10.: Modem-Firmware (`l_modem` MECP-Container, `l_ldsp`, `l_gdsp`) **ohne `DHTB`-Signatur** ✅, Ladeplan aus dem Container gelesen, Modem-Speicher im Mainline-DT schon reserviert. Siehe [MODEM.md](MODEM.md) |
| GPU (Mali-G57) | ❌ | braucht Power-Domains; vorher Temperaturgrenze für `gpu-thermal` |
| Ton | ❌ 🟡 | 09.10.: **Quellcode gefunden** (Realme C33) für die ganze Kette sipc → audio-sipc/-mem/-pipe → VBC v4 + MCDT → Codec SC2730 → Soundkarte; alle Adressen im Xiaomi-Gerätebaum identisch (VBC `0x56480000`, MCDT `0x56490000`, Codec-Dig `0x56750000`, Audio-DMA `0x56580000`). Lücke: Lautsprecher-Verstärker FourSemi FS1588 (I2C `0x20220000`/`0x34`) ohne Quellcode. Siehe [TREIBER_QUELLEN.md](TREIBER_QUELLEN.md) |
| Kamera | ❌ | später |

## Nächste Schritte

1. LAN: Leistung mit cpufreq neu messen, Dauertest (Stunden), DHCPv6-PD abschalten, Firmware-Flicken klären; Server-Betrieb am Router (statt PC) belegen
2. USB-Automatik: den einen Fehlschlag nachstellen; `deploy.sh` auch übers LAN ermöglichen
3. Patch 0017 an Codeberg (ums9230-mainline) melden
4. Akku: Ausschalten mit Kabel bei gekapptem Strom prüfen; später eigener Ladechip-Treiber statt i2cset
5. Display: Abschalten/Wiedereinschalten im DRM-Treiber reparieren (Diagnose: DCS 0x0A muss 0x9C liefern), dann Schlaf über den Treiber (DPU/DSI aus) und Touch als Panel-Follower; Strom neu messen
6. Touch: Feinschliff (Wächter-Erkennung im Treiber, Ausrichtung/Mehrfinger prüfen), Treiber-Korrekturen an George Chan melden. **23.10.: Absturz beim Firmware-Laden behoben (Patch 0025, Lesen nach `release_firmware()`) – war die Ursache der Hänger beim Herunterfahren**
7. **WLAN:** `GET_INFO`, INI-Download (20.10.), `OPEN`/`CLOSE` mit Datenkanälen 8/9 (21.10.) laufen ✅; MAC-Herkunft geklärt (`prodnv:/wifimac.txt`); Scan (2,4 + 5 GHz mit `SET_REGDOM`) und Anmeldung beim Router mit EAPOL auf Kanal 9 laufen ✅ (22.10.); als Nächstes den Deskriptor der Datenblöcke entschlüsseln und den Realme-Treiber `marlin3_sipc` portieren → `wlan0` → Scan. Den sipc-Teil aus `wcn_lauscher.c` später in einen richtigen sipc-Kern überführen (Plan in `kernel/modem-sipc/`)
8. Danach Bluetooth (Kanal 4, Ringe 11/10) oder Ton (VBC/Codec; Verstärker FS1588 offen); prüfen, ob auch der Audio-Kern über sipc antwortet
9. Startstufe mit kexec: Lademodus-Schalter, Startmenü, Rückfall auf funktionierenden Kernel

## Aufräumen später

- Root-Dateisystem `/dev/mmcblk0p81`: „clean with errors“, betroffen ist **nur** Inode 8101 = `/var/log/wtmp` (Fehler seit dem frühen Aufsetzen, harmlos). Später umbenennen und `e2fsck` offline; `Maximum mount count -1` → es wird nie automatisch geprüft
- Auf dem Handy fehlt `strings` (binutils)
- `wcn_lauscher.c` ist längst mehr als ein Lauscher – später in einen richtigen sipc-Kern überführen

## Ideen für später (niedrige Priorität)

- **Ladegrenze als Schalter im Handy-OS (05.10.):** In den Einstellungen „Akku schonen“ an/aus (setzt `MODUS` in `akku.conf`, Dienst neu starten) – nützlich auch fürs Alltagshandy. Dazu „einmal voll laden“ (z. B. vor einem Ausflug), danach automatisch zurück auf die Grenze. Grenzen einstellbar. Der Mechanismus ist fertig, es fehlt nur die Oberfläche.
- **Ladeanzeige im Handy-Modus:** Startet das Handy durchs Einstecken (`sprdboot.mode=charger`), nicht voll hochfahren, sondern Ziege + Akkustand zeigen; Kabel ab → aus, Einschalttaste 3 s → richtig starten. Bausteine fast alle vorhanden (04.10.).
