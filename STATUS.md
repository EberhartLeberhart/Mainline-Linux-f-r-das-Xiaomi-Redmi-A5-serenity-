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
| USB-Host-Modus (LAN-Hub, Sticks) | 🟡 | Host-Bus wird angelegt, Host-Betrieb mit Gerät nie getestet |
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
| Echtzeituhr | ❌ | Uhrzeit falsch; die PMIC-Uhr **läuft** (Bootloader übergibt `charge.shutdown_rtc_time`), nur der Treiber fehlt |
| Akku / Laden | ❌ | Akkudaten aus Xiaomis dtbo (5200 mAh, 3 Lieferanten × 4 Alterungsstufen; hier `bat`, Stufe 0). Ladechip an I2C2: SGM41513 (0x1a) / SC89601 (0x6b) / UPM6922 (0x6a), kein Treiber im Kernel. Nächster Schritt: Akkuanzeige |
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
| i2c-sprd | ❓ | alle I2C-Knoten `disabled` → **nicht getestet** |
| sprd-dma | ❌ | 56580000: -110, hängt an der Domain `/audio-dsp` (agdsp_pd) |
| PWM / Backlight | ❓ | hing früher; wahrscheinlich dieselbe Größen-Ursache → neu testen |
| Hänger bei `=y` (Treiber eingebaut) | ✅ gelöst | 02.10.: Treiber-Kernel startet kalt und warm; Ursache war die Kernel-Größe |

## Anzeige und Eingabe

| Bereich | Status | Beleg / Notiz |
|---|---|---|
| Bildschirm (Bootloader-Framebuffer) | ✅ | läuft über **simpledrm** (720×1640) → Grundlage für eine Oberfläche |
| Echter Display-Treiber (DRM) | ❌ | nur simpledrm |
| Helligkeit | ❌ | siehe PWM |
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

1. Akku: PMIC-Grundtreiber → Spannungsmesser/eFuse → Akkuanzeige (Profile aus `dtbo`) → Uhr, Taste → Ladechip (SGM41513/SC89601/UPM6922 an I2C2, zuerst nur lesen)
2. Audio: Domain `/audio-dsp` → DMA → Codec
3. Startstufe mit kexec: Lademodus-Schalter, Startmenü, Rückfall auf funktionierenden Kernel
4. Echtzeituhr, Einschalttaste, Akku/Laden
5. PWM/Helligkeit neu testen
6. USB-Host, dann Marlin3 (WLAN …)
