# BROM Reference

## Xiaomi Redmi A5 (serenity) — UMS9230E / Unisoc T7250

> **Alles über den Boot-ROM-Zugang in einem Dokument.** Was BROM ist, wie man reinkommt, was man damit machen kann, welche Fehler auftreten und wie man sie behebt. Wer mit dem BROM arbeiten muss, braucht nur dieses Dokument.

---

## Table of Contents

1. [Was ist BROM?](#1-was-ist-brom)
2. [Voraussetzungen (Host-System)](#2-voraussetzungen-host-system)
3. [BROM-Modus betreten](#3-brom-modus-betreten)
4. [Das Protokoll: Wie BROM kommuniziert](#4-das-protokoll-wie-brom-kommuniziert)
5. [Verbindung aufbauen mit spd_dump](#5-verbindung-aufbauen-mit-spd_dump)
6. [FDL1 und FDL2: Die zwei Stufen](#6-fdl1-und-fdl2-die-zwei-stufen)
7. [FDL2-Kommandos: Vollständige Referenz](#7-fdl2-kommandos-vollständige-referenz)
8. [Partitionen lesen und schreiben](#8-partitionen-lesen-und-schreiben)
9. [Diagnose und Debugging über BROM](#9-diagnose-und-debugging-über-brom)
10. [vbmeta und AVB über BROM steuern](#10-vbmeta-und-avb-über-brom-steuern)
11. [Bootloader-Unlock über BROM](#11-bootloader-unlock-über-brom)
12. [Device-Revival: Geräte wiederbeleben](#12-device-revival-geräte-wiederbeleben)
13. [Stock-Restore über BROM](#13-stock-restore-über-brom)
14. [BROM vs. Fastboot: Wann was?](#14-brom-vs-fastboot-wann-was)
15. [Fehlermeldungen und Lösungen](#15-fehlermeldungen-und-lösungen)
16. [Sicherheitsarchitektur](#16-sicherheitsarchitektur)
17. [Adressen und Werte (Schnellreferenz)](#17-adressen-und-werte-schnellreferenz)

---

## 1. Was ist BROM?

BROM steht für **Boot ROM** — ein winziges Programm, das direkt in den Siliziumchip des Unisoc T7250 (UMS9230E) eingebrannt ist. Es ist der **allererste Code**, der beim Einschalten läuft, noch bevor irgendetwas von der eMMC (dem internen Speicher) geladen wird.

### Warum BROM wichtig ist

```
Strom an
   │
   ▼
 BROM (in Silizium — unveränderlich, nicht überschreibbar)
   │
   ├─ Normal-Boot: Lädt SPL von der eMMC → Rest der Boot-Kette
   │
   └─ Download-Modus: Wartet auf USB-Befehle vom Host-PC
         → Hier setzen wir an
```

BROM hat zwei Eigenschaften, die es zum ultimativen Werkzeug machen:

1. **Kann nicht zerstört werden** — Es liegt im Chip-ROM, nicht auf der eMMC. Egal was du flashst, löschst oder kaputtmachst: BROM ist immer da.
2. **Hat einen Download-Modus** — Über eine Tastenkombination öffnet BROM eine USB-Verbindung und wartet auf Befehle. Damit kann man beliebigen Code laden und jede Partition der eMMC lesen oder schreiben.

### CVE-2022-38694: Der Exploit

Im normalen Download-Modus prüft BROM die Signatur des Codes, der geladen wird. Aber dank der Sicherheitslücke **CVE-2022-38694** (entdeckt von TomKing062) kann man diese Prüfung umgehen. Der Exploit nutzt den `exec_addr`-Parameter, um Code an einer Adresse auszuführen, die die Signaturprüfung überspringt (`custom_exec_no_verify`).

**Ergebnis:** Wir können beliebigen, unsignierten Code auf dem Gerät ausführen — die Grundlage für Bootloader-Unlock, Partition-Dumps, Diagnose und Recovery.

---

## 2. Voraussetzungen (Host-System)

### Software

```bash
# Auf Debian/Ubuntu/Mint:
sudo apt install adb fastboot libusb-1.0-0-dev build-essential
```

### UMS9230E-Paket herunterladen

**Kritisch:** Das Paket muss für **UMS9230E** (mit "E"!) sein, nicht für UMS9230.

| Paket | Für SoC | Funktioniert auf Redmi A5? |
|-------|---------|---------------------------|
| `linux_ums9230e_Tecno_KL4` | T7250 / UMS9230**E** | ✅ **Ja** |
| `ums9230_universal_unlock_EMMC` | T606 / UMS9230 | ❌ **Nein** |

Das UMS9230E-Paket (`linux_ums9230e_Tecno_KL4.zip`) findest du auf XDA. Es enthält:

```
linux_ums9230e_Tecno_KL4/
├── spd_dump                    # BROM-Tool (Linux-Binary)
├── fdl1-dl.bin                 # FDL1 für UMS9230E (DRAM-Init)
├── fdl2-dl.bin                 # FDL2 für UMS9230E (Partitionszugriff)
└── unlock_autopatch_9230.sh    # Automatisches Unlock-Skript
```

```bash
mkdir -p ~/redmi-unlock-work/ums9230e
cd ~/redmi-unlock-work/ums9230e
unzip linux_ums9230e_Tecno_KL4.zip
chmod +x linux_ums9230e_Tecno_KL4/spd_dump
chmod +x linux_ums9230e_Tecno_KL4/unlock_autopatch_9230.sh
```

### Alternativ: spd_dump selbst kompilieren

```bash
git clone https://github.com/nickelback7/CVE-2022-38694_unlock_bootloader.git
cd CVE-2022-38694_unlock_bootloader/spreadtrum_flash
make
# Ergebnis: spd_dump Binary im aktuellen Verzeichnis
```

Dieses selbst-kompilierte `spd_dump` ist das Kommunikationstool — die FDL-Binaries brauchst du trotzdem aus dem UMS9230E-Paket.

### udev-Regel für Linux

Ohne diese Regel braucht man jedes Mal `sudo`:

```bash
# /etc/udev/rules.d/51-sprd.rules
SUBSYSTEM=="usb", ATTR{idVendor}=="1782", MODE="0666", GROUP="plugdev"
```

```bash
sudo udevadm control --reload-rules && sudo udevadm trigger
```

---

## 3. BROM-Modus betreten

### Schritte

1. **Handy komplett ausschalten** (Power lange halten → "Ausschalten" → warten bis Bildschirm schwarz)
2. **5 Sekunden warten** (damit das Gerät wirklich aus ist)
3. **VOL_UP + VOL_DOWN gleichzeitig halten**
4. **USB-Kabel einstecken** (während beide Tasten gedrückt sind)
5. **Nach ~2 Sekunden Tasten loslassen**

### Woran du erkennst, dass es geklappt hat

Der Bildschirm bleibt **schwarz** — es gibt kein sichtbares Zeichen am Handy selbst. Die Bestätigung kommt vom Host-PC:

```bash
lsusb | grep 1782
# Erwartet: Bus XXX Device XXX: ID 1782:4d00 Spreadtrum Communications Inc.
```

Wenn du `1782:4d00` siehst, ist das Handy im BROM Download-Modus.

### Wenn es nicht klappt

| Problem | Lösung |
|---------|--------|
| Kein USB-Gerät erscheint | Timing: Tasten müssen VOR dem USB-Kabel gedrückt sein |
| Falsches USB-Gerät (z.B. ADB) | Handy war nicht ganz aus — nochmal ausschalten und warten |
| `1782:4d00` erscheint und verschwindet sofort | BROM hat gestartet, aber kein `spd_dump` empfängt → schneller reagieren oder `--wait 300` |
| Handy reagiert gar nicht | Batterie-Disconnect nötig (siehe [Kapitel 12](#12-device-revival-geräte-wiederbeleben)) |

---

## 4. Das Protokoll: Wie BROM kommuniziert

### Überblick

Das BROM-Protokoll ist ein serielles USB-Protokoll (CDC-Klasse), das in drei Phasen abläuft:

```
Phase 1: Handshake
   Host sendet BSL_CMD_CONNECT
   BROM antwortet "SPRD3" (= Spreadtrum Gen 3 Protokoll)

Phase 2: FDL1 laden
   Host sendet FDL1-Binary (in Paketen) an Adresse 0x65000800
   Host sendet EXEC-Befehl mit exec_addr 0x65015f08
   → CVE-2022-38694 umgeht Signaturprüfung
   → FDL1 startet, initialisiert DRAM und eMMC
   → FDL1 öffnet USB-Kanal neu

Phase 3: FDL2 laden
   Host sendet FDL2-Binary an Adresse 0x9efffe00
   Host sendet EXEC-Befehl
   → FDL2 startet
   → FDL2> Prompt: interaktiver Zugriff auf alle Partitionen
```

### Warum exec_addr wichtig ist

Der Schlüssel zum Exploit ist `exec_addr 0x65015f08`. Diese Adresse zeigt auf die Funktion `custom_exec_no_verify` im BROM. Normalerweise würde BROM die Signatur des FDL1 prüfen, bevor es ihn ausführt. Durch das Springen zu dieser Adresse wird die Prüfung übersprungen.

Alternative Adressen:
- `0x65015f08` — primär, funktioniert zuverlässig
- `0x65015f48` — alternativ, wurde ebenfalls getestet

### Warum nur ein send_file vor dem ersten exec

Ein häufiger Fehler (den wir mit dem Ilyas-Tool gemacht haben): Das BROM-Protokoll erlaubt nach dem Handshake nur **einen** `send_file`-Befehl vor dem ersten `exec`. Erst nachdem FDL1 die USB-Verbindung neu aufgebaut hat (Phase 3), kann das nächste Binary gesendet werden.

---

## 5. Verbindung aufbauen mit spd_dump

### Grundbefehl

```bash
cd ~/redmi-unlock-work

sudo ./ums9230e/linux_ums9230e_Tecno_KL4/spd_dump --wait 300 \
  exec_addr 0x65015f08 \
  fdl ums9230e/linux_ums9230e_Tecno_KL4/fdl1-dl.bin 0x65000800 \
  fdl ums9230e/linux_ums9230e_Tecno_KL4/fdl2-dl.bin 0x9efffe00 \
  exec
```

### Was jeder Parameter bedeutet

| Parameter | Bedeutung |
|-----------|-----------|
| `--wait 300` | Wartet bis zu 300 Sekunden auf das USB-Gerät (gibt Zeit zum Einstecken) |
| `exec_addr 0x65015f08` | CVE-2022-38694 Exploit-Adresse (umgeht Signaturprüfung) |
| `fdl <file> <addr>` | Sendet die Datei und lädt sie an die angegebene Adresse |
| `0x65000800` | Ladeadresse für FDL1 (im SRAM, vor DRAM-Init) |
| `0x9efffe00` | Ladeadresse für FDL2 (im DRAM, nach DRAM-Init durch FDL1) |
| `exec` | Führt aus und öffnet die interaktive Konsole |

### Erwartete Ausgabe

```
Connecting to device...
Connection established (SPRD3)
Sending FDL1 (fdl1-dl.bin, 123456 bytes)... OK
Executing FDL1...
Spreadtrum Boot Block version 1.1
Sending FDL2 (fdl2-dl.bin, 234567 bytes)... OK
Executing FDL2... OK
FDL2>
```

Wenn du `FDL2>` siehst, hast du vollen Zugriff auf die eMMC.

### Befehlskette ohne interaktive Konsole

Du kannst Befehle auch direkt auf der Kommandozeile anhängen:

```bash
# Partition lesen, ohne in die Konsole zu gehen:
sudo ./spd_dump --wait 300 \
  exec_addr 0x65015f08 \
  fdl fdl1-dl.bin 0x65000800 \
  fdl fdl2-dl.bin 0x9efffe00 \
  exec \
  r miscdata miscdata_backup.bin \
  reset
```

---

## 6. FDL1 und FDL2: Die zwei Stufen

### FDL1 — Hardware-Initialisierung

FDL1 ist der erste Code, der nach dem BROM-Exploit läuft. Seine Aufgabe:

1. **DRAM initialisieren** — Register setzen, Timing kalibrieren, DRAM-Training
2. **eMMC initialisieren** — Controller konfigurieren, HS200-Modus aktivieren
3. **USB-Kanal neu öffnen** — Für die Kommunikation mit dem Host

**Das ist der Grund, warum das richtige FDL-Paket so wichtig ist:** Die DRAM-Initialisierung ist SoC-spezifisch. Ein FDL1 für den T606 (UMS9230) hat andere Register-Adressen und Timing-Parameter als einer für den T7250 (UMS9230E). Falsches FDL1 → DRAM-Init hängt → `CHECK_BAUD FAIL`.

### FDL2 — Partitionszugriff

FDL2 läuft nach FDL1 (im jetzt initialisierten DRAM) und bietet:

- Vollständige Partitionstabelle lesen
- Jede Partition lesen (r) oder schreiben (w)
- vbmeta/dm-verity steuern
- Aktiven Slot wechseln
- Gerät neu starten / ausschalten

### Die Signaturkette

```
BROM ──CVE-2022-38694──▶ FDL1 (unsigniert erlaubt durch Exploit)
                            │
                            └──Signaturprüfung──▶ FDL2
```

Der CVE-Exploit umgeht nur die **erste** Prüfung (BROM → FDL1). FDL1 prüft die Signatur von FDL2 weiterhin. Deshalb müssen **beide** FDLs aus dem **gleichen** Paket kommen — FDL1 akzeptiert nur ein FDL2 mit passender Signatur.

### Warum Xiaomis eigene FDL2 nicht funktioniert

Xiaomis `lk-fdl2-sign.bin` (aus dem Fastboot-ROM) startet zwar, gibt aber auf jeden Befehl `0x00fe` zurück:

```
Flashing is not allowed for Protected Partitions
```

Xiaomi hat ihre FDL2 absichtlich so programmiert, dass sie keine Partitionsoperationen zulässt. Eine Anti-Unlock-Maßnahme. Deshalb brauchen wir die FDL2 aus dem UMS9230E-Tecno-Paket — die ist eine generische Unisoc-FDL2 ohne Xiaomis Sperre.

---

## 7. FDL2-Kommandos: Vollständige Referenz

Am `FDL2>`-Prompt stehen folgende Befehle zur Verfügung:

### Partitionen

| Befehl | Beschreibung | Beispiel |
|--------|-------------|---------|
| `r <part> <datei>` | Partition in Datei lesen | `r boot_b boot_b.bin` |
| `w <part> <datei>` | Datei auf Partition schreiben | `w miscdata miscdata_patched.bin` |
| `rawdata 0` | Partitionstabelle anzeigen | `rawdata 0` |

### AVB / dm-verity

| Befehl | Beschreibung |
|--------|-------------|
| `verity 0` | dm-verity deaktivieren (modifiziert vbmeta auf dem Gerät) |
| `verity 1` | dm-verity aktivieren |

### Slot-Verwaltung (A/B)

| Befehl | Beschreibung |
|--------|-------------|
| `set_active a` | Aktiven Slot auf A setzen |
| `set_active b` | Aktiven Slot auf B setzen |

### Gerätesteuerung

| Befehl | Beschreibung |
|--------|-------------|
| `reset` | Gerät neu starten (Normal-Boot) |
| `poweroff` | Gerät ausschalten |
| `reboot-recovery` | In Recovery neu starten |
| `reboot-fastboot` | In Fastboot-Modus neu starten |

### Tipps

- Partitionsnamen mit Slot-Suffix: `boot_a`, `boot_b`, `vendor_boot_b`, etc.
- Ohne Suffix: `miscdata`, `splloader`, `cache`, `userdata`
- Partitionsnamen sind case-sensitive
- Dateien werden im aktuellen Arbeitsverzeichnis auf dem Host-PC gespeichert/gelesen
- Große Partitionen (z.B. `super` mit 5 GB) brauchen Zeit — USB-Seriell ist langsam

---

## 8. Partitionen lesen und schreiben

### Die wichtigsten Partitionen

| Partition | Größe | Was ist drin | Wann per BROM zugreifen |
|-----------|-------|-------------|------------------------|
| `splloader` | 256 KB | First-Stage Bootloader | Recovery: wenn Bootloop |
| `uboot_a` / `uboot_b` | 8 MB | LK (Little Kernel) Bootloader | Recovery: wenn Fastboot nicht geht |
| `boot_a` / `boot_b` | 64 MB | Linux-Kernel (GKI Image) | Custom Kernel flashen (wenn Fastboot nicht geht) |
| `vendor_boot_a/b` | 100 MB | Vendor-Ramdisk + DTB + Module | Custom Ramdisk/DTB |
| `init_boot_a/b` | 8 MB | Generic-Ramdisk (/init) | Custom Init |
| `miscdata` | 1 MB | Boot-Flags (Unlock-Token!) | Bootloader-Unlock |
| `vbmeta_a/b` | 2 MB | AVB-Metadaten | Verified Boot deaktivieren |
| `uboot_log` | 16 MB | Bootloader-Log | Diagnose! |
| `cache` | 64 MB | Update-Cache (ext4) | Kernel-Log ablegen |
| `userdata` | ~50 GB | Benutzerdaten | Android-Daten sichern |

### Backup erstellen

**Vor** jedem Experiment ein Backup der kritischen Partitionen:

```
FDL2> r splloader splloader_backup.bin
FDL2> r uboot_a uboot_a_backup.bin
FDL2> r uboot_b uboot_b_backup.bin
FDL2> r boot_a boot_a_backup.bin
FDL2> r boot_b boot_b_backup.bin
FDL2> r vendor_boot_a vendor_boot_a_backup.bin
FDL2> r vendor_boot_b vendor_boot_b_backup.bin
FDL2> r init_boot_a init_boot_a_backup.bin
FDL2> r init_boot_b init_boot_b_backup.bin
FDL2> r miscdata miscdata_backup.bin
FDL2> r vbmeta_a vbmeta_a_backup.bin
FDL2> r vbmeta_b vbmeta_b_backup.bin
```

### Partition zurückschreiben

```
FDL2> w boot_b boot_b_backup.bin
FDL2> w vendor_boot_b vendor_boot_b_backup.bin
```

**Wichtig:** `w` überschreibt die gesamte Partition ohne Rückfrage. Die Datei muss **exakt die richtige Größe** haben oder kleiner als die Partition sein.

### Einzelne Partition dumpen (als Kommandozeile)

```bash
sudo ./spd_dump --wait 300 \
  exec_addr 0x65015f08 \
  fdl fdl1-dl.bin 0x65000800 \
  fdl fdl2-dl.bin 0x9efffe00 \
  exec \
  r uboot_log uboot_log.bin \
  reset
```

---

## 9. Diagnose und Debugging über BROM

### uboot_log: Der Gold-Kanal

Die Partition `uboot_log` (16 MB) enthält das **Boot-Log des LK-Bootloaders**. Das ist die wertvollste Diagnosequelle, weil:

- Es zeigt den kompletten Boot-Ablauf mit Timestamps
- Fehler bei der Image-Verifikation werden protokolliert
- Slot-Auswahl und Boot-Modi sind dokumentiert
- USB-Initialisierung und Display-Setup sichtbar

```
FDL2> r uboot_log uboot_log.bin
```

Dann auf dem Host:

```bash
strings uboot_log.bin | less
```

**Beispiel-Auszug:**

```
[00000703] pmic_misc_init
[00000795] eMMC init (HS200 200MHz, 80ms)
[00000820] CHG init OK (BQ2560x, Chip ID: 1)
[00000820] ANDROID: Slot B priority=15, successful_boot=1 → Booting slot_b
[00086592] verify boot check init_boot (secboot!)
[00086655] init_boot verify_ret: 0x0 (OK)
[00088714] [start_linux] at PC:0x80080000, dt_addr:0x9df00000
```

### cache-Partition für Kernel-Logs

Wenn kein UART-Adapter vorhanden ist (wie bei uns), kann man den Linux-Kernel so konfigurieren, dass er sein Log auf die `cache`-Partition schreibt. Nach einem Reboot liest man es per BROM aus:

```
FDL2> r cache cache.bin
```

### miscdata: USB-Mux und Debug-Flags

Die `miscdata`-Partition enthält neben dem Unlock-Token (Offset 0x2000) auch das USB-Mux-Feld (Offset 0x2780):

| Wert bei 0x2780 | Bedeutung |
|-----------------|-----------|
| `off` | USB vom LK deaktiviert ("usb is configed as off by miscdata") |
| `uart` | USB-Port als UART-Konsole (usb2spuart-Modus!) |
| `jtag` | JTAG über USB |
| `jtag_apwdg` | JTAG mit Application-Watchdog |
| (leer / Nullen) | Normaler USB-Betrieb |

**USB-Debug-Konsole aktivieren:**

```bash
# miscdata dumpen:
FDL2> r miscdata miscdata.bin

# Auf dem Host: Offset 0x2780 auf "uart" setzen
# (oder auf Nullen für normalen Betrieb)

# Zurückschreiben:
FDL2> w miscdata miscdata_modified.bin
```

**Achtung:** Wenn `off` bei 0x2780 steht, deaktiviert LK die USB-Hardware komplett. Das betrifft nur die LK-Phase (Fastboot-USB), nicht den Kernel oder BROM.

---

## 10. vbmeta und AVB über BROM steuern

### Was AVB macht

Android Verified Boot (AVB 2.0) prüft bei jedem Start die Integrität von Kernel, Ramdisk und System. Die Prüfwerte stehen in den `vbmeta`-Partitionen.

### vbmeta-Flags

| Flags | Bedeutung | Wann verwenden |
|-------|-----------|---------------|
| `0x00` | Alles aktiv (Verification + dm-verity Hashtree) | Stock-Android, unmodifiziert |
| `0x02` | Verification **aus**, Hashtree **an** | Modifizierte boot/vendor_boot + Stock-super |
| `0x03` | Alles aus | **BRICHT ANDROID** (super kann nicht gemountet werden) |

### Schnellmethode: verity-Befehl

```
FDL2> verity 0    # Deaktiviert dm-verity (setzt Flags)
FDL2> verity 1    # Aktiviert dm-verity
```

### Manuelle Methode: vbmeta-Image mit Flags=2

Erstelle ein 4096-Byte vbmeta-Disable-Image:

```bash
# vbmeta_disabled.bin erstellen (4096 Bytes):
python3 -c "
import struct
# AVB0 Magic + version + flags
data = bytearray(4096)
data[0:4] = b'AVB0'
data[4:8] = struct.pack('>I', 1)      # major version
data[8:12] = struct.pack('>I', 0)     # minor version
# Flags at offset 120
data[120:124] = struct.pack('>I', 2)  # flags=2: disable verification, keep hashtree
with open('vbmeta_disabled.bin', 'wb') as f:
    f.write(data)
"
```

Auf alle relevanten vbmeta-Partitionen schreiben:

```
FDL2> w vbmeta_b vbmeta_disabled.bin
FDL2> w vbmeta_system_b vbmeta_disabled.bin
FDL2> w vbmeta_vendor_b vbmeta_disabled.bin
FDL2> w vbmeta_system_ext_b vbmeta_disabled.bin
FDL2> w vbmeta_product_b vbmeta_disabled.bin
FDL2> w vbmeta_odm_b vbmeta_disabled.bin
FDL2> w avbmeta_rs_b vbmeta_disabled.bin
```

### Wichtig: Slot-spezifische Hashes

vbmeta von Slot A enthält Hashes der Slot-A-Images. vbmeta von Slot B enthält Hashes der Slot-B-Images. Man kann **nicht** Slot-A-vbmeta auf Slot B kopieren — die Hashes stimmen nicht überein.

Wenn du Stock-Boot auf Slot B willst, aber kein Original-vbmeta_b hast, verwende flags=2 (Verification abschalten).

---

## 11. Bootloader-Unlock über BROM

Der Bootloader-Unlock ist die bekannteste BROM-Anwendung. Kurzversion hier — die vollständige Geschichte mit allen Sackgassen steht in [BOOTLOADER_UNLOCK.md](BOOTLOADER_UNLOCK.md).

### Unlock-Befehl

```bash
cd ~/redmi-unlock-work
sudo ./ums9230e/linux_ums9230e_Tecno_KL4/unlock_autopatch_9230.sh
```

Das Skript:
1. Verbindet sich mit BROM
2. Lädt FDL1 + FDL2
3. Liest `miscdata`
4. Schreibt den Unlock-Token an Offset `0x2000`
5. Sichert und stellt SPL/U-Boot wieder her
6. Startet das Gerät neu

### Nach dem Unlock

```bash
# Fastboot-Modus: VOL_DOWN + Power
fastboot getvar unlocked
# unlocked: yes
```

Der Token überlebt Factory-Resets und ROM-Flashes. Einmal unlockt = dauerhaft unlockt (solange niemand miscdata zurückschreibt).

---

## 12. Device-Revival: Geräte wiederbeleben

### "Das Handy ist tot" — Keine Panik

Wenn das Handy nach einem fehlgeschlagenen Flash scheinbar tot ist (kein Bildschirm, kein USB, kein Fastboot), ist es fast immer wiederherstellbar, weil BROM im Silizium liegt und **nicht zerstört werden kann**.

### Schritt 1: Batterie-Disconnect

1. USB-Kabel **abziehen**
2. **Rückseite abnehmen** (Clip-on-Plastik, keine Schrauben nötig)
3. **Batterie-Flexkabel** vorsichtig lösen (kleiner Stecker auf dem Board)
4. **10 Sekunden warten**
5. Flexkabel **wieder einstecken**
6. Rückseite schließen

### Schritt 2: BROM-Modus testen

1. VOL_UP + VOL_DOWN halten
2. USB-Kabel einstecken
3. `lsusb | grep 1782` prüfen

Wenn `1782:4d00` erscheint → BROM ist da → Gerät kann wiederhergestellt werden.

### Schritt 3: Wiederherstellung

Je nach Problem:

| Zustand | Was tun |
|---------|---------|
| Bootloop (Logo-Schleife) | BROM → SPL + uboot von Stock zurückschreiben |
| Fastboot geht, Android nicht | Fastboot → Stock-ROM flashen (`flash_all.sh`) |
| Nichts geht, aber BROM da | BROM → komplette Boot-Kette von Stock zurückschreiben |
| Kein BROM | Batterie-Disconnect wiederholen, evtl. länger warten |

### Warum man sich keine Sorgen machen muss

Beim Arbeiten mit FDL1/FDL2 wird **nichts auf die eMMC geschrieben**, solange du keinen expliziten `w`-Befehl gibst. FDL1 und FDL2 laufen im RAM. Wenn sie abstürzen, bleibt die eMMC unangetastet. Auch `gen_spl-unlock` (das modifizierte SPL als FDL1) läuft nur im RAM und schreibt erst am Ende den Token — wenn es vorher crasht, ist alles unverändert.

---

## 13. Stock-Restore über BROM

### Methode 1: Kompletter Stock-Restore (BROM + Fastboot)

Wenn noch Fastboot erreichbar ist (nach Batterie-Disconnect), ist die sauberste Methode:

1. **Per BROM die Boot-Kette reparieren** (falls nötig):

```
FDL2> w splloader splloader_stock.bin
FDL2> w uboot_a uboot_a_stock.bin
FDL2> w uboot_b uboot_b_stock.bin
FDL2> reset
```

2. **Per Fastboot den Rest flashen:**

```bash
# Fastboot-ROM entpacken:
tar xzf serenity_global_images_A15.0.20.0.VGWMIXM_15.0.tgz
cd serenity_global_images_*/
chmod +x flash_all.sh
./flash_all.sh
```

### Methode 2: Nur über BROM (wenn gar nichts anderes geht)

Alles über BROM schreiben (langsam, aber funktioniert immer):

```
FDL2> w splloader splloader_stock.bin
FDL2> w uboot_a uboot_a_stock.bin
FDL2> w uboot_b uboot_b_stock.bin
FDL2> w boot_a boot_a_stock.bin
FDL2> w boot_b boot_b_stock.bin
FDL2> w vendor_boot_a vendor_boot_a_stock.bin
FDL2> w vendor_boot_b vendor_boot_b_stock.bin
FDL2> w init_boot_a init_boot_a_stock.bin
FDL2> w init_boot_b init_boot_b_stock.bin
FDL2> w vbmeta_a vbmeta_a_stock.bin
FDL2> w vbmeta_b vbmeta_b_stock.bin
FDL2> reset
```

**Tipp:** Die Stock-Images kommen aus dem entpackten Xiaomi-Fastboot-ROM (`images/`-Verzeichnis).

### Stock-ROM-Quellen

| Quelle | URL | Format |
|--------|-----|--------|
| mifirm.net | https://mifirm.net | `.tgz` (Fastboot-ROM) |
| Xiaomi offiziell | Über Mi Flash Tool | `.tgz` |

Such nach "serenity" oder "Redmi A5" + "Global" für das richtige ROM.

---

## 14. BROM vs. Fastboot: Wann was?

Nach dem Bootloader-Unlock stehen **zwei** Flash-Wege zur Verfügung. Die richtige Wahl spart Zeit und Nerven:

### Übersicht

| | BROM / spd_dump | Fastboot |
|---|---|---|
| **Einstieg** | VOL_UP+DOWN+USB, Exploit, FDL1, FDL2 | VOL_DOWN+Power |
| **Geschwindigkeit** | Langsam (serielles USB-Protokoll) | Schnell (USB Bulk Transfer) |
| **Zugriff** | Alle Partitionen, auch splloader | Alle Partitionen (nach Unlock) |
| **Voraussetzung** | UMS9230E-Paket + libusb | `fastboot` Tool + Unlock |
| **Funktioniert wenn** | Immer (solange Chip intakt) | Bootloader muss starten |
| **Befehl** | `r boot_b boot.bin` / `w boot_b boot.bin` | `fastboot flash boot_b boot.img` |

### Wann BROM

- **Bootloader-Unlock** (der einzige Weg)
- **Gerät unbrickbar** (kein Fastboot, kein Recovery)
- **splloader** flashen (Fastboot kann den SPL nicht flashen)
- **uboot_log** lesen (Bootloader-Diagnose)
- **miscdata** direkt bearbeiten (USB-Mux, Unlock-Token)
- **vbmeta** reparieren, wenn Fastboot selbst nicht startet
- **Vollständige eMMC-Dumps** für Analyse

### Wann Fastboot

- **Alles nach dem Unlock** — Kernel, Ramdisks, DTB flashen
- **Tägliche Arbeit** — boot_b, vendor_boot_b, init_boot_b
- **Slot wechseln** — `fastboot set_active a/b`
- **Stock-ROM flashen** — `flash_all.sh`
- **Schnelle Iteration** — Kernel bauen → flashen → testen

### Faustregel

> **Fastboot für den Alltag. BROM für den Notfall und alles, was Fastboot nicht kann.**

---

## 15. Fehlermeldungen und Lösungen

### Beim Verbinden

| Meldung | Ursache | Lösung |
|---------|---------|--------|
| `Connection timeout` | Kein USB-Gerät gefunden | Handy nicht im BROM-Modus → nochmal: VOL_UP+DOWN+USB |
| `Permission denied` | Fehlende USB-Rechte | `sudo` verwenden oder udev-Regel einrichten |
| `device busy` | Anderer Prozess hat USB-Gerät offen | `lsof /dev/bus/usb/...` prüfen, Prozess beenden |

### Bei FDL1

| Meldung | Ursache | Lösung |
|---------|---------|--------|
| `CHECK_BAUD FAIL` | FDL1 DRAM-Init gescheitert → **falsches Paket** | UMS9230**E**-FDL1 verwenden (mit "E"!) |
| `Sending FDL1... timeout` | FDL1-Binary kaputt oder falsches Format | Datei neu herunterladen, Checksumme prüfen |

### Bei FDL2

| Meldung | Ursache | Lösung |
|---------|---------|--------|
| `0x00fe` auf jeden Befehl | Xiaomis FDL2 blockiert Zugriff | UMS9230E-Paket-FDL2 verwenden (nicht Xiaomis lk-fdl2-sign.bin) |
| `0x008b` | Falsches Block-Size / Protokollfehler | FDL2 aus falschem Paket → UMS9230E verwenden |
| `Sending FDL2... timeout` | FDL1 lehnt FDL2 ab (Signaturprüfung) | FDL1 und FDL2 aus dem **gleichen** Paket verwenden |
| `device removed, exiting…` | SoC abgestürzt | Batterie-Disconnect, nochmal versuchen |

### Bei Partitionsoperationen

| Meldung | Ursache | Lösung |
|---------|---------|--------|
| `Partition not found` | Partitionsname falsch | `rawdata 0` für korrekte Namen |
| `Write error` | Datei größer als Partition | Dateigröße prüfen |
| `incompatible partition` | FDL2-Version passt nicht zur Partitionstabelle | Richtiges FDL-Paket verwenden |

### Am Gerät selbst

| Zustand | Ursache | Lösung |
|---------|---------|--------|
| Handy reagiert gar nicht | SoC in Crash-Loop | Batterie-Disconnect ([Kapitel 12](#12-device-revival-geräte-wiederbeleben)) |
| Bootloop (Logo-Schleife) | Modifiziertes Image + falsches vbmeta | BROM → vbmeta mit flags=2 flashen oder Stock wiederherstellen |
| LK Panic: `ESR 0x5e000000` | Modifiziertes init_boot + vbmeta flags=0 | vbmeta-Flags auf 0x02 setzen |
| `LOCK FLAG IS UNLOCKED` | Normal nach Unlock | Erwartetes Verhalten, kein Fehler |
| Fastboot-Modus startet nicht | LK oder uboot beschädigt | BROM → uboot/splloader von Stock zurückschreiben |

---

## 16. Sicherheitsarchitektur

### DHTB-Container-Format

Jedes signierte Binary (SPL, FDL, LK) im Unisoc-Bootpfad ist in einen DHTB-Container verpackt:

```
Offset    Größe      Feld
──────    ──────     ─────────────────────────────────
0x000     4 Bytes    Magic: "DHTB" (0x44 0x48 0x54 0x42)
0x004     4 Bytes    Version / Typ
0x008     32 Bytes   SHA256-Hash des Payloads
0x028     8 Bytes    Padding
0x030     4 Bytes    Payload-Länge
0x034     460 Bytes  Reserviert (Nullen)
0x200     N Bytes    Payload (der eigentliche Code)
0x200+N   ~1.7 KB    SIMGHDR-Block (RSA-2048 Public Key + Signatur)
```

### Zwei Prüfebenen

| Ebene | Was | Wann geprüft |
|-------|-----|-------------|
| SHA256 | Integritäts-Hash des Payloads | **IMMER** — von BROM |
| RSA-2048 | Kryptographische Signatur (SIMGHDR) | **NUR wenn ROTPK-Hash in eFuse gebrannt** |

### eFuses: Nicht gebrannt

Beim Redmi A5 (und den meisten Budget-Unisoc-Geräten) sind die eFuses **nicht gebrannt**:

- **Beweis:** CVE-2022-38694 hat unsignierten Code geladen → RSA-Prüfung wurde übersprungen → eFuse ROTPK-Hash ist Null (ungebrannt)
- **Grund:** eFuse-Brennen kostet extra im Produktionsprozess — wird bei Budget-Geräten gespart
- **Konsequenz:** `secureboot=1` ist nur ein Software-Flag von LK, keine Hardware-Sicherheit

### CVE-2022-38691/38692 (Bonus)

Selbst wenn eFuses gebrannt wären: Type-0-Zertifikate im SIMGHDR überspringen den `memcmp` des Public-Key-Hash gegen den eFuse-Wert. Beliebige RSA-Keys könnten injiziert werden. Eine zweite Verteidigungslinie, die nicht verteidigt.

---

## 17. Adressen und Werte (Schnellreferenz)

### USB

| Item | Wert |
|------|------|
| BROM USB VID | `1782` |
| BROM USB PID | `4d00` |
| BROM Handshake | "SPRD3" |
| LK USB VID:PID | `1782:4d00` (bcdDevice=24.16) |

### Adressen

| Item | Adresse |
|------|---------|
| CVE exec_addr (primär) | `0x65015f08` |
| CVE exec_addr (alternativ) | `0x65015f48` |
| FDL1 Ladeadresse | `0x65000800` (SRAM) |
| FDL2 Ladeadresse | `0x9efffe00` (DRAM) |
| Kernel Startadresse | `0x80080000` |
| DT-Adresse | `0x9df00000` |
| Framebuffer | `0x9e000000` |

### Partitions-Offsets

| Item | Partition | Offset |
|------|-----------|--------|
| Unlock-Token | miscdata | `0x2000` |
| USB-Mux-Feld | miscdata | `0x2780` |
| AB-Boot-Control | misc | `0x0800` (2048) |

### DHTB-Struktur

| Item | Offset / Wert |
|------|---------------|
| Magic | `0x000` → "DHTB" |
| SHA256-Hash | `0x008` (32 Bytes) |
| Payload-Länge | `0x030` (4 Bytes) |
| Payload-Start | `0x200` |
| SIMGHDR | Payload-Ende (~1.7 KB) |

### Tastenkombinationen

| Modus | Kombination |
|-------|-------------|
| BROM Download | VOL_UP + VOL_DOWN + USB einstecken |
| Fastboot | VOL_DOWN + Power |
| Recovery | VOL_UP + Power |

---

## Externe Links

- [CVE-2022-38694 Unlock Tool (TomKing062)](https://github.com/nickelback7/CVE-2022-38694_unlock_bootloader)
- [Unser GitHub Issue #327](https://github.com/nickelback7/CVE-2022-38694_unlock_bootloader/issues/327)
- [mifirm.net — Xiaomi Fastboot-ROMs](https://mifirm.net)
- [Projekt-Repository](https://github.com/EberhartLeberhart/Mainline-Linux-f-r-das-Xiaomi-Redmi-A5-serenity-)

---

*Dieses Dokument ist Teil des [Mainline Linux for Xiaomi Redmi A5](https://github.com/EberhartLeberhart/Mainline-Linux-f-r-das-Xiaomi-Redmi-A5-serenity-) Projekts.*
