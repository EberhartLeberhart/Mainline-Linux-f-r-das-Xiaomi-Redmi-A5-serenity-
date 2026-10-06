# Redmi A5 (serenity) – Hardware-Status

Stand: 04.10.2026 · Kernel 7.1.0-rc1-g1dcd2ff05af2-dirty #57 (Slot b; dirty = Kconfig-Zeile für cpufreq als Modul) · Konfiguration `arch/arm64/configs/redmi_a5_defconfig`

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
| A/B-Rettung, Fastboot | ✅ | Rettungs-Images: `boot_schlank_ok.img`, `boot_ziege_ok.img`, `boot_treiber_ok.img`, `boot_temp_ok.img`, `boot_pd_ok.img`; vendor_boot: `vendor_boot_usb2.img` (03.10. erneut als Rettung belegt) |
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
| USB-Host-Modus (LAN-Hub, Sticks) | ✅ von Hand | 04.10.: Rolle per `/sys/class/usb_role/64900000.usb-role-switch/role` = host, **5 V aus dem Ladechip** (REG01 Bit 5 OTG, Laden aus; REG08 → 0xe0 = OTG; 5,15 V, Grenze 1,2 A). DT hat **keine vbus-supply** – der Treiber schaltet die 5 V nicht selbst. USB-Stick: High-Speed 480 Mbit, **200 MB lesen mit 10,4–11,2 MB/s, 3× ohne Fehler**, vfat nur lesend eingehängt. Verbrauch mit Stick ~−180 mA, beim Lesen ~−215 mA (aus dem Akku – **Host und Laden gehen nicht gleichzeitig**). ⚠️ Belegt: PC-Kabel einstecken, solange die 5 V an sind → Handy speist ~0,2 A in den PC zurück (−325 mA). Testskript `redmi-otgtest.sh` mit Wächter: Gerät ab → 5 V nach **1,2 s** aus (belegt), dazu Stromgrenze als 2. Sicherung (nicht ausgelöst getestet ❓). Babble/Abbruch nur beim Wackeln am billigen Stick. ❓ einmal `musb-hdrc: unexpected dma_addr` nach der Rückspeisung, ohne Folgen. **05.10. Hub mit Einspeisung ✅:** Rolle host, eigene 5 V AUS, Hub (`1a40:0101`, 4 Ports, am PC als Netzteil) speist ein → REG08=0xb4, **Handy lädt (+384 mA) und ist gleichzeitig Host**; Stick hinter dem Hub (`1-1.3`) gelesen mit 10,5 MB/s, dabei weiter +354 mA. Eingangsstrom blieb 0x04 (500 mA). Umstecken auf PC-Kabel im Host-Betrieb unbedenklich (keine eigenen 5 V). `unexpected dma_addr` (05.10. 5×) kommt nur nach Umstecken im **Geräte**-Modus, nie im Host-Betrieb ❓. Kein Dauerbetrieb/Dienst – Schalter `USB=geraet|hub` geplant |
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
| Verbrauch | ✅ gemessen | 04.10.: **~102 mA bei 4,40 V ≈ 0,45 W** im Leerlauf ohne Kabel, Display an (Hintergrundlicht vom Bootloader, nicht abschaltbar solange PWM fehlt). Reicht rechnerisch ~2 Tage mit vollem Akku |
| Ladechip | ✅ gesteuert | 04.10.: I2C-Bus 2 läuft (`/soc/i2c@200f0000` + Alias `i2c2` nötig). **SGM41513 an 0x1a** (REG0B PN=1). Drei Schalter belegt, alle ohne Fehler: **HIZ** (REG00 Bit 7) trennt den Eingang → Handy läuft aus dem Akku (−102…−117 mA), USB-Netz bleibt; **Laden aus** (REG01 Bit 4) → 223 mA → 1 mA, Status „lädt nicht“, Handy läuft vom Kabel; **Ladespannung** (REG04) begrenzt das Nachladen. ⚠️ REG04 schlagartig weit unter die Akkuspannung → BAT_FAULT (Überspannung). Nach HIZ aus erkennt der Chip den Eingang neu und setzt den Eingangsstrom selbst auf 2400 mA (zurückstellen!). **Über Neustart:** HIZ und Laden-aus werden zurückgestellt, REG04 bleibt stehen (der Wert 0x99 ≈ 4,45 V stammt also vom alten Android). Wachhund aus. Ausschalten mit Kabel noch nicht geprüft ❓. ITERM-Wert unplausibel (❓). Kein Kernel-Treiber, Steuerung per i2cset |
| Ladegrenze (Server) | ✅ läuft | 04.10.: `redmi-akku.sh`/`.service`, Einstellungen `/etc/redmi/akku.conf` (MODUS=server, 75–80 %, Notfall < 20 %, EINGANG_MAX=500 mA). Über 80 % Strom kappen, 75–80 % ruhen (vom Kabel), darunter laden. Test auf dem Handy: ruhen → `0x01=0x0a`, entladen → `0x00=0x84`, −117 mA, Beenden stellt alles zurück. 05.10.: **Herunterfahren** beendet den Dienst sauber („Laden normal“) ✅; **nach dem Start** läuft er von selbst ✅. **Einstecken des Kabels** setzt im Chip HIZ zurück und den Eingangsstrom auf 0x17 = 2400 mA – die Erkennung dauert einige Sekunden und überschreibt eine zu frühe Korrektur noch einmal (belegt). Deshalb Chip-Prüfung alle 10 s, Eingangsstrom-Grenze, danach 10 s Nachsehen alle 2 s; Umstecken 2× korrekt abgefangen ✅. Umschalten bei 80 % / 75 % im echten Betrieb noch nicht beobachtet ❓ |
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
| sprd-dma | ❌ | 56580000: -110, hängt an der Domain `/audio-dsp` (agdsp_pd) |
| PWM / Backlight | ❓ | hing früher; wahrscheinlich dieselbe Größen-Ursache → neu testen |
| Hänger bei `=y` (Treiber eingebaut) | ✅ gelöst | 02.10.: Treiber-Kernel startet kalt und warm; Ursache war die Kernel-Größe |

## Anzeige und Eingabe

| Bereich | Status | Beleg / Notiz |
|---|---|---|
| Bildschirm (Bootloader-Framebuffer) | ✅ | läuft über **simpledrm** (720×1640) → Grundlage für eine Oberfläche |
| **Helligkeit** | ✅ | 06.10.: Panel regelt selbst – Xiaomi-DT `oled-backlight`: DCS **0x51**, 12 Bit (0–4095, Standard 500). DSI-Baustein `0x31100000` läuft vom Bootloader weiter (Register = Panelwerte, PHY eingerastet, Anordnung wie Mainline `sprd_dsi.c`) → Befehl per `busybox devmem` (GEN_PLD 0x70, GEN_HDR 0x6C) ✅ sichtbar. **Gemessen ohne Kabel: voll 490 mA, 500 → 119 mA, aus → 72 mA** (−40 %). Werkzeug `redmi-licht.sh` + Dienst (Server: Licht aus, Einschalttaste kurz = 30 s an) ✅ auf dem Handy belegt. Die PWM des Prozessors wirkt bei diesem Panel nicht (erklärt den früheren Fehlschlag) |
| Panel-Daten | ✅ gelesen | 06.10. aus Xiaomis dtbo (`Panel_C3Z_42_02_0a_vid`, 8 Panels im Overlay): 720×1640, 4 Spuren, Video, RGB888, ~1,2 Gbit/s/Spur, Pixeltakt 199,2 MHz, 60/90/120 Hz (nur vfp anders), Reset-Folge, Init-, Sleep-In/Out-, CABC-Befehle. DPU `0x31000000` (qogirl6): Ebene 0 = `0x9CF2A000` ARGB8888 = simpledrm-Bereich (Gegenprobe ✅), IOMMU vermutlich aus |
| Touch | ❌ | 06.10.: Novatek TDDI über **SPI** (`c3z,NVT-ts-spi`, 9,6 MHz), Reset GPIO 145, IRQ GPIO 144; Display-Spannung SM5109 an I2C 0x3e. Xiaomi-Treiber `novatek_nt36528a_spi_ts.ko` (vendor_ramdisk / vendor_dlkm) basiert auf Novateks GPL-Treiber `nt36xxx` → **portieren**. Chip ohne Flash: Firmware wird bei jedem Start geladen (`nvt_download_firmware_hw_crc`) → **Firmware nötig**: liegt in der eigenen Android-Partition `odm_a:/firmware/novatek_ts_{csot,truly}_fw.bin` (je 128 KB; nicht frei, wird **nicht** veröffentlicht – Werkzeug `lp_auspacken.py` holt sie aus der eigenen super-Partition). Zuordnung vermutlich C3Z_42 → CSOT (Treiber-Standard) ❓. Kernel braucht noch `CONFIG_SPI_SPRD` (Bus `spi3` = `spi@20150000`) |
| Echter Display-Treiber (DRM) | ❌ | nur simpledrm; Mainline sprd_dpu/sprd_dsi (sharkl3) als Ausgangspunkt – DSI-Register passen |
| Helligkeit | 🟡 | 04.10.: PWM-Treiber läuft, Kanal 2 erzeugt nachweislich 25 kHz (Takte an, `actual: enabled`), **Licht reagiert aber nicht** – weder 0 % noch 100 %. Xiaomi/Android nutzen genau diesen Kanal. Vermutung ❓: Display-Chip NT36528 muss erst selbst initialisiert werden (DSI) → mit dem Display-Treiber |
| Touchscreen | ❌ | Novatek NT36528A über **SPI** (laut Android-Modulen) |
| Kernel-Logo (Ziege) | ✅ | 02.10.: Ziege mittig statt Pinguine, auch beim Kaltstart; hing früher nur wegen der Größe |
| Bootlogo (Bootloader) | 🟡 | Werkzeug fertig, `logo_b` wirkt nicht → Partition klären (`logo_a`/`fbootlogo`?) – Kosmetik, niedrige Priorität |

## Funk und Multimedia

| Bereich | Status | Notiz |
|---|---|---|
| WLAN / Bluetooth / FM / GPS (Marlin3) | ❌ | Quellen: A7-Pro-Kernel, Firmware aus Dump |
| FM-Senden | ❓ | Behauptung aus altem Chat, **nicht belegt** |
| Mobilfunk | ❌ | Ziel: Ersatz-Internet |
| GPU (Mali-G57) | ❌ | braucht Power-Domains; vorher Temperaturgrenze für `gpu-thermal` |
| Ton, Kamera | ❌ | für den Server nicht geplant |

## Nächste Schritte

1. Akku: Umschalten bei 80 % im Dauerbetrieb belegen; Ausschalten mit Kabel bei gekapptem Strom prüfen; später eigener Ladechip-Treiber statt i2cset
2. Hintergrundlicht (PWM): vermutlich ❓ größter Verbraucher im Leerlauf – für den Server abschaltbar machen, dann Verbrauch neu messen
2. Audio: Domain `/audio-dsp` → DMA → Codec
3. Startstufe mit kexec: Lademodus-Schalter, Startmenü, Rückfall auf funktionierenden Kernel
4. USB-Schalter `geraet|hub` als Dienst, mit Rückfall auf `geraet`, wenn im Hub-Betrieb kein Netz kommt (Zugang dann über USB-LAN-Adapter am Hub)
5. Display: Panel schlafen legen (Sleep-In aus dem DT), dann Touch (SPI, Novatek)
6. Marlin3 (WLAN …) – Voraussetzung für USB-Host im Server-Betrieb

## Ideen für später (niedrige Priorität)

- **Ladegrenze als Schalter im Handy-OS (05.10.):** In den Einstellungen „Akku schonen“ an/aus (setzt `MODUS` in `akku.conf`, Dienst neu starten) – nützlich auch fürs Alltagshandy. Dazu „einmal voll laden“ (z. B. vor einem Ausflug), danach automatisch zurück auf die Grenze. Grenzen einstellbar. Der Mechanismus ist fertig, es fehlt nur die Oberfläche.
- **Ladeanzeige im Handy-Modus:** Startet das Handy durchs Einstecken (`sprdboot.mode=charger`), nicht voll hochfahren, sondern Ziege + Akkustand zeigen; Kabel ab → aus, Einschalttaste 3 s → richtig starten. Bausteine fast alle vorhanden (04.10.).
