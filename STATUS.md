# Redmi A5 (serenity) – Hardware-Status

Stand: 02.10.2026 · Kernel 7.1.0-rc1-g612fb8b51e06 #55 (Slot b) · Konfiguration `arch/arm64/configs/redmi_a5_defconfig`

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

## Start und Grundsystem

| Bereich | Status | Beleg / Notiz |
|---|---|---|
| Kaltstart (echtes Aus → Taste) | ✅ | 02.10.: mehrfach, `startreihe.csv`; vorher **nie** wirklich getestet (Handy ging nie aus) |
| Warmstart (`reboot`) | ✅ | 02.10.: 2/2 in `startreihe.csv` |
| Start mit Ladekabel | ✅ | 02.10.: kommt nach dem Ausschalten von selbst wieder, Bootloader meldet `sprdboot.mode=charger` |
| Dauerbetrieb | ✅ | 21 h Laufzeit (01.–02.10.) |
| A/B-Rettung, Fastboot | ✅ | Rettungs-Images: `boot_schlank_ok.img`, `boot_ziege_ok.img`, `boot_treiber_ok.img`, `boot_temp_ok.img` |
| rst_mode-Fix (keine abnormal-mode-Schleife) | ✅ | 0x40 beim ADI-Probe; nach echtem Aus ist rst_mode 0x0, Start klappt trotzdem |
| PMIC-Wachhund gestoppt | ✅ | kein Neustart alle 5 Minuten mehr |
| 8 CPU-Kerne | ✅ | 01.10.: online 0-7; Kerne lassen sich ab- und anschalten (02.10., PSCI CPU_OFF) |
| CPU-Takt (cpufreq) | ❌ | kein cpufreq, CPUs laufen mit Bootloader-Takt |
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
| Power-Domains | ❌ | `SPRD_PM_DOMAINS` aus; Treiber gelesen, schaltet nur EIN → nächster Schritt |
| MM-Taktcontroller (30000000) | ❌ | -110, wartet auf Power-Domain |
| Taktcontroller 23000000 | ❌ | ebenfalls -110, Ursache offen |
| Echtzeituhr | ❌ | Uhrzeit falsch; die PMIC-Uhr **läuft** (Bootloader übergibt `charge.shutdown_rtc_time`), nur der Treiber fehlt |
| Akku / Laden | ❌ | Bootloader nennt Lade-Chip `bq2560x` (kompatibel SGM41513); FGU fehlt |
| Einschalttaste | ❌ | gpio-keys wartet, PMIC-EIC nicht gebaut |

## Temperatur

| Bereich | Status | Beleg / Notiz |
|---|---|---|
| Temperatursensoren | ✅ | 02.10.: 11 Zonen starten von selbst (`NVMEM_RMEM=y`), 25–26 °C |
| Überhitzungsschutz | 🟡 | 10 Zonen: passive 70 °C, critical 110 °C. **critical wirkt** (Abschaltung über den neuen Poweroff), **passive wirkt noch nicht** (braucht cpufreq). `gpu-thermal` hat keine Grenze → vor dem GPU-Schritt ergänzen |

## Treiber

| Treiber | Status | Beleg / Notiz |
|---|---|---|
| gpio-sprd | ✅ | 02.10.: fest eingebaut, bindet beim Start |
| gpio-eic-sprd | ✅ | 02.10.: fest eingebaut, 4 Geräte |
| pinctrl-sprd(-ums9230) | ✅ | 02.10.: fest eingebaut, bindet beim Start |
| sprd-mailbox | ✅ | 02.10.: fest eingebaut, bindet beim Start |
| sprd-thermal | ✅ | 02.10.: fest eingebaut, 2 Geräte |
| i2c-sprd | ❓ | alle I2C-Knoten `disabled` → **nicht getestet** |
| sprd-dma | ❌ | 56580000: -110 (wartet auf Takt) |
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

1. Power-Domains → Taktcontroller, DMA
2. cpufreq (Strom sparen, aktiviert die 70-°C-Bremse)
3. Startstufe mit kexec: Lademodus-Schalter, Startmenü, Rückfall auf funktionierenden Kernel
4. Echtzeituhr, Einschalttaste, Akku/Laden
5. PWM/Helligkeit neu testen
6. USB-Host, dann Marlin3 (WLAN …)
