# Was im Kernel noch fehlt (Stand 22.11.2026)

Zum Abhaken. Belege und Einzelheiten stehen in [STATUS.md](STATUS.md), die Arbeitsreihenfolge in
[UEBERGABE.md](UEBERGABE.md). Zeichen: 🟡 angefangen · ❌ fehlt · ❓ noch nicht untersucht.

**Läuft schon:** (neu bis 14.11.: Bluetooth, Ton über Kopfhörer und Lautsprecher, USB-OTG mit 5 V und Rollenwechsel durch den Kernel, Ladechip-Treiber, Landlock) Start kalt/warm, 8 Kerne mit cpufreq und Tiefschlaf, eMMC, PMIC (Aus, Neustart, Uhr,
Wachhund), Temperaturschutz, Akkuanzeige und Ladegrenze, USB-Gerät und -Host, LAN über USB-Adapter,
Display (DRM) mit Helligkeit, Touch (Zuordnung geprüft, bis 10 Finger), Tasten, WCN-Kern mit allen
sipc-Kanälen, `wlan0` mit Scan und Anmeldung (wpa_supplicant empfängt EAPOL 1/4), Krypto-Befehle der
Kerne (AES/SHA/CRC32/NEON, Kernel nutzt `aes-ce`), GPU Mali-G57 mit Panfrost und Mesa (OpenGL ES 3.1),
Vibrationsmotor, microSD-Kartenleser.

## Zuerst: Stabilität (20.–22.11., vor den Sensoren)

**Abstürze:** Mit Taktsteuerung (`sprd-cpufreq-v2`) stürzte das Handy unter Last (Firefox mit Video) alle 10–40 Minuten ab, manchmal sogar im Leerlauf: Oops und Panics an immer anderen Stellen (`kfree`, Planer, Timer, `cpuidle`), einzelne gekippte Bits im Kernel-Code – nur aus Sicht **eines** Kerns –, Register als 0 gelesen. Die Berichte kamen über **pstore/ramoops** (`/var/lib/systemd/pstore`), das schon im Kernel und Gerätebaum steckte.

**Ursache (22.11., auf zwei Handys belegt):** Beide Kern-Gruppen hängen an **einer** Leitung `DCDC_CPU` (Bootloader `power.from.extern=0`, bestätigt unter Android). Die Firmware stellt aber nur die Spannung der Gruppe ein, die gerade umschaltet, und übergeht den Bedarf der anderen. Messung Handy 2, kleine Kerne fest auf 1378 MHz (Tabelle: 846 mV), große Stufe für Stufe runter: 1820 → 928 mV, 1536 → 862, 1404 → **822**, 1228 → **762**, 768 → **750** mV. Kleine Kerne auf 1612 MHz (928 mV), große auf 768 MHz → **sofortiger Neustart**. Die Firmware programmiert den Regler außerdem nur, wenn sich die Spannung der umschaltenden Gruppe ändert.

**Gegenproben:** ohne Taktsteuerung (feste 1,0 V vom Bootloader) 60 min Rechenfehler-Zähler auf allen 8 Kernen, 263.694 Prüfsummen, 0 Fehler (Handy 1) und 30 min große Kerne (Handy 2); jede feste Stufe (1820, 1228) und der Wechsel 1228 ↔ 1820 (10.735×) stabil; Tiefschlaf sperren hilft nicht; Bin-Übergabe an die Firmware ändert die Tabelle nicht (Android übergibt keinen).

**Lösung: Kopplung im Treiber (Patch 0059):** Der Treiber merkt sich die gewünschte Stufe beider Gruppen, hält jede Gruppe mindestens auf der Stufe, deren Spannung den höchsten Bedarf deckt, schickt die Gruppe mit der höchsten Spannung zuletzt und frischt deren Spannung auf, wenn nötig (Schalter `couple_clusters`, Zähler `couple_raises`/`couple_refreshes`). Beleg: Handy 2, freie Taktsteuerung, Rechenfehler-Zähler 30 min auf den großen Kernen → 0 Fehler (ohne Kopplung Absturz nach ~1 min); Handy 1 im Alltag mit Firefox/Video 67 min ohne Oops (vorher spätestens nach 40 min), 2,1 Mio. Eingriffe. Fühlt sich flüssiger an, weil die kleinen Kerne nicht mehr auf 614 MHz fallen, solange die großen laufen (Leerlaufstrom noch messen).

| Schritt | Stand | Notiz |
|---|---|---|
| WCN-Reservierung klären | ✅ | Board-DT `wcn@87000000` überlappte Ottos Aufteilung → Lücke 0x87480000–0x874FFFFF war normaler RAM; reserviert (Patch 0057). Richtig, aber nicht die Ursache |
| Audio-DMA nach Stopp | ✅ | `sprd-pcm-dma` schickte nach `TRIGGER_STOP` einen freigegebenen Deskriptor erneut ab (Oops `vchan_tx_submit`, `dead000000000108`) → neuer Deskriptor in `prepare()` (Patch 0058). Steckt auch in mainline; Kandidat für eine Einreichung |
| Spannung beider Kern-Gruppen | ✅ | Kopplung (Patch 0059), siehe oben. Kandidat für eine Einreichung |
| Selbsterkennung: pstore/ramoops | ✅ | läuft (`ramoops@fff80000`, 256 KiB); systemd legt die Berichte in `/var/lib/systemd/pstore` ab |
| Werkzeug: Rechenfehler-Zähler | ✅ | `werkzeuge/handy/redmi-cputest.sh "6 7" 30` – SHA-256 je Kern immer wieder, jeder Fehler sofort im Journal |
| Selbsterkennung: Wächter-Dienst | ❌ | Kernel-Protokoll und pstore nach dem Start auswerten, Desktop-Benachrichtigung |
| Leerlaufstrom mit Kopplung | ❓ | Strom mit/ohne Kopplung messen; ggf. nur koppeln, wenn die großen Kerne wirklich laufen |
| Spannungsschwankung im Leerlauf | ❓ | `DCDC_CPU` wechselt 700/725 mV ohne Zutun des Treibers (Hardware senkt bei schlafenden Kernen?) |
| Firmware-Wartezeit/Spannungsschritt | ❓ | `sprd,voltage-margin` ist bei Unisoc eine Wartezeit in Takten, `sprd,voltage-step` der größte Schritt; bei uns nicht gesetzt (bei Xiaomi auch nicht) |
| Speicher im Alltag | ❓ | Idee 20.11.: Desktop-Sitzung per cgroup Mindestspeicher (`MemoryLow`), Firefox begrenzen (`MemoryMax`), `systemd-oomd` greift früh ein |

**Android 17 (21.11.):** Ein Tester hat nach dem Android-17-Update unsere Abbilder auf Slot b geflasht: nur Vibration, schwarzer Bildschirm ohne Logo. Gerettet über BROM (Slot umschalten) und fastboot (Original-Partitionen zurück). Vermutung ❓: Der neue Bootloader nimmt unsere Abbilder nicht an (`vendor_boot`-Format, Gerätebaum, AVB/Rollback?), unser Kernel startet gar nicht erst. Warnung in README und BUILDING.

| Schritt | Stand | Notiz |
|---|---|---|
| Android-17-Bootloader verstehen | ❌ | vom Tester: was genau geflasht, `fastboot getvar version-bootloader`/`all` (ohne Seriennummer/IMEI); Vergleich Android-15- und Android-17-Bootloader: `vendor_boot`-Kopfversion, erwarteter Gerätebaum, Prüfungen. 22.11. (lunadevph): neuer AVB-Bildschirm, längeres Vibrieren, Kernel 5.15 → 6.18; alte GSIs und fremde GKI-Kernel booten nicht mehr. Vergleich am besten mit der offiziellen Fastboot-ROM (privat, nicht ins Repo) |
| Weg für Android-17-Geräte | ❌ | je nach Ursache: Abbilder anpassen oder klar dokumentieren, dass nur der Android-15-Bootloader geht; eigene Handys bleiben auf Android 15 |

## CPU, GPU und Beschleuniger

| Bereich | Stand | Was fehlt |
|---|---|---|
| CPU-Kapazität (big.LITTLE) | ✅ | 24.10.: selbst gemessen (sysbench, A55 0,498 / A75 1,053 Ereignisse je MHz) → `capacity-dmips-mhz` 484/1024; dazu `cpu-map`-Fehler behoben (cluster1 hieß core6/core7, der Kernel zählt je Cluster ab core0). Ergebnis: Kapazität 428 ×6 / 1024 ×2, Cluster 0-5 und 6-7 erkannt. Meldung an Codeberg zurückgestellt (kein Konto, eigener Weg); Hinweise in UEBERGABE.md |
| Energiemodell / EAS | ❌ | `/sys/kernel/debug/energy_model` leer; braucht zusätzlich `dynamic-power-coefficient` je Kern (aus Xiaomis DT) |
| Leerlauf-Stufen | 🟡 | `WFI` und `cpu-pd-lit` vorhanden; Cluster-Stufen und große Kerne prüfen ❓ |
| devfreq (Speicher/GPU-Takt) | 🟡 | 25.10.: GPU ✅ (`23100000.gpu`, 5 Stufen); Speicher (DDR) ❌ |
| Takt/Spannung, Übertakten | ❓ | Tabellen kommen per SMC aus der SML-Firmware (`DVFS_TABLE_UPDATE` je Temperatur, Güteklasse aus eFuse `dvfs_bin`); `boost` = 0, keine Boost-Stufen. Übertakten nur über geänderte SML – Idee für nach WLAN (zweites Redmi, Kühlung). Vom Kernel aus: Taktgrenzen nach unten (Profile Server/Alltag/Spiele), `sprd,voltage-margin` |
| GPU Mali-G57 | ✅ | 24.10.: **läuft.** Knoten `gpu@23100000` war vollständig, nur `disabled` → im Rezept an (`GPU=1`). Panfrost meldet `mali-g57 id 0x9091 r0p1`, ein Shaderkern (`shader_present=0x1`), 384 MHz; `renderD128`, `gpu_top` an. Mesa 25.2: Renderer „Mali-G57 (Panfrost)“, OpenGL ES 3.1 / OpenGL 3.1; glmark2 (off-screen) Score **228**, dabei 26–28 °C (GPU macht kaum Wärme, Fühler reagiert bei CPU-Last 24 → 36 °C). Temperaturgrenze `gpu-thermal`: passiv 70 °C (drosselt beide CPU-Cluster), kritisch 110 °C. Module `panfrost`/`gpu-sched` liegen in `/lib/modules` und laden von selbst. 25.10.: GPU-Spannung `vddgpu` lesbar (SC2730-Regler): **1,00 V bei 384 MHz** – der Bootloader setzt mehr als Android je nutzt (Realme-Referenz: 384 MHz 0,70 V … 850 MHz 0,80 V). Takt-Eltern 384/512/614,4/768 MHz/gpll vorhanden. 25.10.: **Taktstufen laufen** (Panfrost-devfreq, `simple_ondemand`): 384 / 512 / 614,4 / 768 / 850 MHz, jede Stufe mit eigener Quelle (twpll-384m/512m, lpll-614m4, twpll-768m, gpll); dafür Patch 0026 (Spreadtrum-Takt `gpu-core-clk` darf die Quelle wechseln). glmark2 **460** (vorher 228), keine Fehler, max. 31 °C. 25.10.: Spannung für alle Stufen 0,85 V statt 1,00 V (stabil, glmark2 452); Akkustrom unter GPU-Last 262 → 253 mA, Leerlauf 53 mA (Display aus) – die GPU ist ein kleiner Verbraucher. Zurückgestellt: Einzelwerte je Stufe (Android 0,70–0,80 V, kaum Nutzen). 31.10.: **Beschleunigung aufs Display** ✅ ohne Umweg (Mesa 25.2 findet zu `sprd` selbst panfrost): `glmark2-es2-drm` zeichnet auf dem Display, `Mali-G57 (Panfrost)`, Score 723 (2 Szenen, ohne Warten); mit `--swap-mode fifo` genau 60 FPS, DPU-Interrupt ~60/s → Bildwechsel-Meldung (VBlank) stimmt. **GPU als Kühlgerät** ✅ (Rezept `GPUKUEHL`): `gpu-thermal` drosselt bei 70 °C jetzt auch die GPU (Test mit `emul_temp` 75 °C: 850 → 768 MHz, danach zurück). Nicht möglich: Vulkan (panvk unterstützt die Mali-G57 nicht, nur `llvmpipe`), Video-Dekodierung (kein Knoten, kein Treiber). Fehlt: Güteklasse `gpu_bin` (eFuse) lesen, `gpu-mem`-Takt bleibt bei 384 MHz ❓ |
| Video-Decoder/-Encoder (VSP) | ❓ | Android `sprd_vsp`; in Mainline nichts bekannt |
| JPEG-Einheit | ❓ | |
| GSP (2D-Zusammensetzung) | ❓ | Android `sprd-gsp.ko` |
| Hardware-Zufallsgenerator | ❓ | Android `sprd_trng.ko` |

## Angefangen (🟡)

| Bereich | Was fehlt |
|---|---|
| WLAN | 16.11.: Anmelde-Timeouts nach Umschalten von `wlan0` behoben ✅ (Patch 0053 von beebono). 01.11.: **läuft** mit Ottos Treibern (Scan, WPA2, DHCP, SSH über WLAN). 04.11.: MAC ✅ (Patch 0032: feste MAC aus der Chip-Kennung, nie mehr 00:00). 05.11.: Rundrufe ✅ (Patch 0033: „Bildschirm an“ + eigene IP an die Firmware – von außen per ARP auffindbar, erstes Paket ~240 ms). 06.11.: Autostart ✅ mit Schalter (`redmi-wlan.service`, `/etc/redmi/wlan.conf` an/aus, `redmi-wlan.sh an|aus|status`; Lade-Sperre bleibt, nur der Dienst lädt `sprd_wcn`). Fehlt: erster Schlüsselaustausch nach dem Start bricht manchmal ab (Grund 15), `wpa_supplicant` verbindet sich nach ~4 s von selbst ❓; WCN-Kern im Betrieb stoppen (bisher wirkt „aus“ erst ab dem nächsten Start); Schalter „MAC fest/zufällig“ (später Oberfläche, `wpa_supplicant mac_addr`); `WCN=1` als Standard im Rezept; Bluetooth (`BT_SPRDSIPC`) |
| Display-Schlaf | 29.10.: `redmi-licht.sh` schläft über den Treiber (fb-blank, DSI+DPU aus, Touch folgt) ✅ – Akku: Schlaf 43 mA statt 60 mA (DCS-Weg). 31.10.: **Panel vollständig in Kernel-Hand** ✅ (Patch 0030: Reset/avdd/avee, Xiaomis Init-Befehle; Schlaf mit Versorgung aus 41 mA). ESD-Prüfung wie Xiaomi ✅ (Patch 0031, alle 3 s `0x0A = 0x9C`, sonst neu aufsetzen). Ungeklärt: `fb0/blank = 4` nach dem Start mit alter licht-Fassung ❓ |
| Touch | 28.10.: Wächter-Erkennung + Selbst-Neuladen ✅, Runtime-PM beim Abmelden ✅ (Patch 0028). 29.10.: als Panel-Follower ans Display gekoppelt ✅ (Patch 0029, DT `panel`). `redmi-licht.sh` meldet ihn nicht mehr ab ✅ |
| USB | Rollenwechsel macht seit 14.11. der Kernel (Patch 0047), das Rate-Skript `redmi-usb.sh` ist überflüssig; Gadget-Skript setzt die Rolle beim Start noch zusätzlich auf `device`; Gadget-MAC wechselt bei jedem Start; `musb … unexpected dma_addr` beim Ab-/Anstecken (18.11. gesehen, seit wann ❓) |
| Ton | 08.11.: Kopfhörer und Lautsprecher spielen (Patches 0039–0041). 15.11.: UCM-Profil (vom Tester, aufgeräumt) + `redmi-ton.service` ✅. 19.11.: Mikrofon ✅ (nur Mischer, UCM-Gerät „Mic“); Hörmuschel = derselbe Lautsprecher oben, nichts zu tun. Fehlt: Klinkenerkennung (Headset-Mikrofon HPMIC dann mit), Telefonie-Weg (mit Modem). 19.11. Klinke untersucht: Unisoc (`fe-be.dtsi`, `sprd-headset`, von Xiaomis Overlay nur eingeschaltet) erkennt über `pmic_eic 6` ("aud_int_all") plus ADC-Kanal 20 (3-/4-polig, Tasten 0–480/481–990/991–2500). EIC 6 bleibt beim Einstecken auf 0 → Sammel-Unterbrechung des Codecs, keine Schaltleitung; die Erkennungsschaltung im SC2730 muss erst eingeschaltet werden (Register aus Unisocs Headset-Treiber, Quelle noch suchen ❓). Ottos Kartentreiber (`ums9230-card.c`) hat noch keine Buchsen-Erkennung. **20.11.: Klinkenerkennung ✅** (Patch 0056, Abfrage von `ANA_STS0` LDETL im Codec-Treiber, Ton schaltet automatisch um). Fehlt noch: Interrupt (EIC 6) statt Abfrage, Headset-Mikrofon (HPMIC) und Tasten über ADC 20 |
| i2c-sprd | meldet kein NACK |
| Ladechip | Treiber `sgm41513_charger` (Patches 0045/0048): Ladezustand für die Akkuanzeige, OTG-5-V als Regler, Wachhund aus; 18.11.: Eingangsstrom nach erkanntem Eingang ✅ (Patches 0054/0055, PC 500 / Netzteil 2000 mA, Fehl-Erkennung „PC als Netzteil“ wird nachgeprüft). Fehlt: Ladegrenze (Server) im Treiber statt `redmi-akku.sh` (Stufe 4), Ladestrom (ICHG) prüfen ❓; die beiden anderen möglichen Chips (SC89601, UPM6922) kennt der Treiber nicht |
| microSD | 27.10.: läuft (High Speed, 20,8 MB/s lesend). Fehlt: UHS-Modi (SDR50/104, 1,8-V-Signalpegel) für mehr Tempo ❓; Idee für später: System auf SD-Karte (Root auf `mmcblk1`), mit kexec-Startmenü wählbar |
| Start | `redmi-boot-ok.service` markiert fest Slot b; Bootlogo (`logo_b`) wirkt nicht |
| PMIC-Regler (SC2730) | 25.10.: Treiber an (`CONFIG_REGULATOR_SC2730=y`, legt der PMIC-Treiber per Name an), alle 32 Regler mit Spannung lesbar. Zum Schutz alle außer `dcdc-gpu` im DT `disabled` + `regulator_ignore_unused` (sonst schaltet der Kernel 19 Regler ohne `always-on` ab, u. a. eMMC). Später: Verbraucher (eMMC, SD, WCN, USB) richtig an ihre Regler binden, dann Schutz lockern |
| Module | ✅ 25.10.: `deploy.sh` liefert alle Module mit (`modules_install` → `/lib/modules/<Version>` + `depmod`, räumt alte auf). Offen: cpufreq-Sonderweg (`/usr/local/lib/redmi`) und `wcn_lauscher.ko` (außerhalb des Baums, `/root`) vereinheitlichen |

## Fehlt (❌)

| Bereich | Stand / Abhängigkeit |
|---|---|
| FM-Radio | am WCN-Kern, Ringe 13/14 |
| GNSS/GPS | eigener Kern (core@4, Mailbox 9), `gnssmodem.bin`; Speicher `0x8749b000–0x874ccfff` reservieren |
| Kamera | ISP `0x3a000000`; Sensoren (Tester-Handy, 10.11.): hinten OV32D40, vorne OV08F10, Tiefe OVSP0821; kein Mainline-Treiber für diese Sensoren ❓ |
| Mobilfunk | bewusst zurückgestellt bis zur Antwort von Bundesnetzagentur/Xiaomi (Anfragen 23.10.); vorbereitbar: sipc-Treiberstapel, `sprd-pmic-wdt` |
| PWM | ungeklärt, fürs Panel nicht nötig |

## Noch nicht untersucht (❓)

- Sensoren (Lage/Beschleunigung, Helligkeit, Annäherung) – Android lädt `sensorhub.ko`. 19.11.: Sensoren hängen am **Sensorhub im PMSYS-Kern** (Grundbaum: `sipc` „pmsys“, Kanal 4 „pm_shub“, `rpmsg` „sprd,shub“); in Ottos Baum gibt es schon `drivers/remoteproc/sprd_pmsys.c` und `drivers/iio/common/sprd_shub/sprd_shub.c` (Beschleunigung, Magnetfeld, Gyro, Licht, Annäherung), beide bei uns aus (`SPRD_PMSYS_REMOTEPROC`, `IIO_SPRD_SHUB`). Firmware: `pm_sys.bin` aus Partition `pm_sys_a/b` (privat), Herkunft von `pm_bootcode.bin` offen ❓. Im Overlay keine I2C-Sensoren außer `vl53l0@52` (ST-Laserabstand, vermutlich Kamera-Fokus ❓); Annäherung vermutlich virtuell über den Touch (`sprd,tp-proximity-command`, `PS_Config_…`)
- Fingerabdrucksensor: **verbaut** (seitlich), Overlay kennt Goodix und Silead (Reset ap_gpio 121, IRQ ap_gpio 29) – ganz hinten, siehe `ideen/fingerabdruck-profilometer.md`
- Kopfhörer-Erkennung, USB-C-Kabelerkennung (`sc27xx_typec`)
- Akku-Echtheitsprüfung DS28E30 (1-Wire, vermutlich unwichtig)

## Spätere Verbesserungen (bewusst zurückgestellt, 01.11.)

Läuft schon, könnte aber besser werden – kommt in ein späteres Update, nicht jetzt:

- **microSD schneller (UHS-I, SDR50/104):** braucht umschaltbare Signalspannung 3,0 ↔ 1,8 V (`ldo-vddsdio`, bisher bewusst
  fest 3,0 V). Vorbild: RG Rotate (UMS512) läuft mit SDR104.
- **Fingerabdrucksensor:** siehe oben und `ideen/fingerabdruck-profilometer.md`.
- **GPU-Feinschliff:** `gpu-mem`-Takt (fest 384 MHz) und Güteklasse `gpu_bin` (eFuse) für Spannungen je Stufe.
- **WLAN „Immer erreichbar / Sparsam“ (05.11.):** die Firmware bekommt fest „Bildschirm an“ (wacht zu jedem DTIM auf,
  Handy im Netz auffindbar). Schalter in der Oberfläche: „Sparsam“ = Meldung folgt dem echten Display (weniger Strom ❓,
  bei dunklem Display schlechter erreichbar). Mehrverbrauch von „immer an“ noch nicht gemessen (`redmi-wcnstrom.sh`).
- **WLAN-Senderate:** der Devolo empfängt vom Handy nur mit 6 Mbit/s (alte Betriebsart), zum Handy hin 52 Mbit/s (MCS 5);
  passt zu ~19 Mbit/s bei scp. Ursache offen ❓ (Ratenauswahl der Firmware, beebonos Korrekturen?).
- **Suspend (echter Tiefschlaf):** für einen Server nicht nötig, für einen Handy-Modus schon. Bekannte Stolpersteine aus dem
  RG-Rotate-Projekt (gleicher PMIC SC2730): Akkuanzeige braucht `power-supplies` zum Ladechip, `sc27xx_wdt` ohne
  Schlaf-Routinen, musb-Weck-Interrupt. Quelle: [rg-rotate-linux, DEVICE-BRINGUP.md](https://github.com/beebono/rg-rotate-linux/blob/main/DEVICE-BRINGUP.md)

## Danach (kein Kernel)

kexec-Startmenü mit Rückfall (optional, getrennt, ohne Funk – `startmenue/`), Updates als Paket mit A/B-Rückfall, Lademodus-Anzeige (Ziege + Akkustand), Bootloader wieder sperren mit
eigenem Schlüssel, dann Oberfläche, Installer und Abbilder.
