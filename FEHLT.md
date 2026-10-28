# Was im Kernel noch fehlt (Stand 24.10.2026)

Zum Abhaken. Belege und Einzelheiten stehen in [STATUS.md](STATUS.md), die Arbeitsreihenfolge in
[UEBERGABE.md](UEBERGABE.md). Zeichen: 🟡 angefangen · ❌ fehlt · ❓ noch nicht untersucht.

**Läuft schon:** Start kalt/warm, 8 Kerne mit cpufreq und Tiefschlaf, eMMC, PMIC (Aus, Neustart, Uhr,
Wachhund), Temperaturschutz, Akkuanzeige und Ladegrenze, USB-Gerät und -Host, LAN über USB-Adapter,
Display (DRM) mit Helligkeit, Touch (Zuordnung geprüft, bis 10 Finger), Tasten, WCN-Kern mit allen
sipc-Kanälen, `wlan0` mit Scan und Anmeldung (wpa_supplicant empfängt EAPOL 1/4), Krypto-Befehle der
Kerne (AES/SHA/CRC32/NEON, Kernel nutzt `aes-ce`), GPU Mali-G57 mit Panfrost und Mesa (OpenGL ES 3.1),
Vibrationsmotor, microSD-Kartenleser.

## CPU, GPU und Beschleuniger

| Bereich | Stand | Was fehlt |
|---|---|---|
| CPU-Kapazität (big.LITTLE) | ✅ | 24.10.: selbst gemessen (sysbench, A55 0,498 / A75 1,053 Ereignisse je MHz) → `capacity-dmips-mhz` 484/1024; dazu `cpu-map`-Fehler behoben (cluster1 hieß core6/core7, der Kernel zählt je Cluster ab core0). Ergebnis: Kapazität 428 ×6 / 1024 ×2, Cluster 0-5 und 6-7 erkannt. Meldung an Codeberg zurückgestellt (kein Konto, eigener Weg); Hinweise in UEBERGABE.md |
| Energiemodell / EAS | ❌ | `/sys/kernel/debug/energy_model` leer; braucht zusätzlich `dynamic-power-coefficient` je Kern (aus Xiaomis DT) |
| Leerlauf-Stufen | 🟡 | `WFI` und `cpu-pd-lit` vorhanden; Cluster-Stufen und große Kerne prüfen ❓ |
| devfreq (Speicher/GPU-Takt) | 🟡 | 25.10.: GPU ✅ (`23100000.gpu`, 5 Stufen); Speicher (DDR) ❌ |
| Takt/Spannung, Übertakten | ❓ | Tabellen kommen per SMC aus der SML-Firmware (`DVFS_TABLE_UPDATE` je Temperatur, Güteklasse aus eFuse `dvfs_bin`); `boost` = 0, keine Boost-Stufen. Übertakten nur über geänderte SML – Idee für nach WLAN (zweites Redmi, Kühlung). Vom Kernel aus: Taktgrenzen nach unten (Profile Server/Alltag/Spiele), `sprd,voltage-margin` |
| GPU Mali-G57 | ✅ | 24.10.: **läuft.** Knoten `gpu@23100000` war vollständig, nur `disabled` → im Rezept an (`GPU=1`). Panfrost meldet `mali-g57 id 0x9091 r0p1`, ein Shaderkern (`shader_present=0x1`), 384 MHz; `renderD128`, `gpu_top` an. Mesa 25.2: Renderer „Mali-G57 (Panfrost)“, OpenGL ES 3.1 / OpenGL 3.1; glmark2 (off-screen) Score **228**, dabei 26–28 °C (GPU macht kaum Wärme, Fühler reagiert bei CPU-Last 24 → 36 °C). Temperaturgrenze `gpu-thermal`: passiv 70 °C (drosselt beide CPU-Cluster), kritisch 110 °C. Module `panfrost`/`gpu-sched` liegen in `/lib/modules` und laden von selbst. 25.10.: GPU-Spannung `vddgpu` lesbar (SC2730-Regler): **1,00 V bei 384 MHz** – der Bootloader setzt mehr als Android je nutzt (Realme-Referenz: 384 MHz 0,70 V … 850 MHz 0,80 V). Takt-Eltern 384/512/614,4/768 MHz/gpll vorhanden. 25.10.: **Taktstufen laufen** (Panfrost-devfreq, `simple_ondemand`): 384 / 512 / 614,4 / 768 / 850 MHz, jede Stufe mit eigener Quelle (twpll-384m/512m, lpll-614m4, twpll-768m, gpll); dafür Patch 0026 (Spreadtrum-Takt `gpu-core-clk` darf die Quelle wechseln). glmark2 **460** (vorher 228), keine Fehler, max. 31 °C. 25.10.: Spannung für alle Stufen 0,85 V statt 1,00 V (stabil, glmark2 452); Akkustrom unter GPU-Last 262 → 253 mA, Leerlauf 53 mA (Display aus) – die GPU ist ein kleiner Verbraucher. Zurückgestellt: Einzelwerte je Stufe (Android 0,70–0,80 V, kaum Nutzen). Fehlt: Güteklasse `gpu_bin` (eFuse) lesen, `gpu-mem`-Takt bleibt bei 384 MHz ❓, Kühlgerät für die GPU selbst |
| Video-Decoder/-Encoder (VSP) | ❓ | Android `sprd_vsp`; in Mainline nichts bekannt |
| JPEG-Einheit | ❓ | |
| GSP (2D-Zusammensetzung) | ❓ | Android `sprd-gsp.ko` |
| Hardware-Zufallsgenerator | ❓ | Android `sprd_trng.ko` |

## Angefangen (🟡)

| Bereich | Was fehlt |
|---|---|
| WLAN | Senden über einen Datenkanal, Schlüssel (`WIFI_CMD_KEY`), DHCP → SSH; Lauscher in sauberen Treiber aufteilen (sipc-Kern + WLAN) |
| Display-Schlaf | Ab-/Anschalten über den DRM-Treiber bringt das Panel nicht wieder an; Umweg über direkte DSI-Befehle |
| Touch | Wächter-Signal ein Byte zu früh geprüft (lädt nicht selbst neu); als Panel-Follower ans Display koppeln |
| USB | Rollenwechsel: ein seltener Fehlschlag; Gadget-MAC wechselt bei jedem Start |
| i2c-sprd | meldet kein NACK |
| Ladechip | nur per `i2cset`; drei mögliche Chips (SGM41513 getestet, SC89601, UPM6922 nicht) |
| microSD | 27.10.: läuft (High Speed, 20,8 MB/s lesend). Fehlt: UHS-Modi (SDR50/104, 1,8-V-Signalpegel) für mehr Tempo ❓; Idee für später: System auf SD-Karte (Root auf `mmcblk1`), mit kexec-Startmenü wählbar |
| Start | `redmi-boot-ok.service` markiert fest Slot b; Bootlogo (`logo_b`) wirkt nicht |
| PMIC-Regler (SC2730) | 25.10.: Treiber an (`CONFIG_REGULATOR_SC2730=y`, legt der PMIC-Treiber per Name an), alle 32 Regler mit Spannung lesbar. Zum Schutz alle außer `dcdc-gpu` im DT `disabled` + `regulator_ignore_unused` (sonst schaltet der Kernel 19 Regler ohne `always-on` ab, u. a. eMMC). Später: Verbraucher (eMMC, SD, WCN, USB) richtig an ihre Regler binden, dann Schutz lockern |
| Module | ✅ 25.10.: `deploy.sh` liefert alle Module mit (`modules_install` → `/lib/modules/<Version>` + `depmod`, räumt alte auf). Offen: cpufreq-Sonderweg (`/usr/local/lib/redmi`) und `wcn_lauscher.ko` (außerhalb des Baums, `/root`) vereinheitlichen |

## Fehlt (❌)

| Bereich | Stand / Abhängigkeit |
|---|---|
| Bluetooth | WCN läuft, Ringe 11/10 auf Kanal 4 angelegt; HCI-Treiber und Konfigurationsdaten (pskey/rf) fehlen ❓ |
| FM-Radio | am WCN-Kern, Ringe 13/14 |
| GNSS/GPS | eigener Kern (core@4, Mailbox 9), `gnssmodem.bin`; Speicher `0x8749b000–0x874ccfff` reservieren |
| Ton | Quellcode gefunden (Realme); davor `sprd-dma` (−110, hängt an der Audio-DSP-Domain); Lautsprecher-Verstärker FS1588 ohne Quellcode |
| Kamera | ISP `0x3a000000`; Sensoren laut Analyse OV SP0821, BYD BF30A2, SC080CS ❓ |
| Mobilfunk | bewusst zurückgestellt bis zur Antwort von Bundesnetzagentur/Xiaomi (Anfragen 23.10.); vorbereitbar: sipc-Treiberstapel, `sprd-pmic-wdt` |
| PWM | ungeklärt, fürs Panel nicht nötig |

## Noch nicht untersucht (❓)

- Sensoren (Lage/Beschleunigung, Helligkeit, Annäherung) – Android lädt `sensorhub.ko`
- Fingerabdrucksensor: **verbaut** (seitlich), Overlay kennt Goodix und Silead (Reset ap_gpio 121, IRQ ap_gpio 29) – ganz hinten, siehe `ideen/fingerabdruck-profilometer.md`
- Kopfhörer-Erkennung, USB-C-Kabelerkennung (`sc27xx_typec`)
- Akku-Echtheitsprüfung DS28E30 (1-Wire, vermutlich unwichtig)

## Danach (kein Kernel)

kexec-Startmenü mit Rückfall (optional, getrennt, ohne Funk – `startmenue/`), Updates als Paket mit A/B-Rückfall, Lademodus-Anzeige (Ziege + Akkustand), Bootloader wieder sperren mit
eigenem Schlüssel, dann Oberfläche, Installer und Abbilder.
