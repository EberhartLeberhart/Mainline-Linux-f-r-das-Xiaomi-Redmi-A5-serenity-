# Werkzeuge

Alle Skripte des Projekts, nach Einsatzort sortiert (seit 01.11.2026; vorher lagen alle flach in `werkzeuge/`).
Jedes Skript erklärt in seinen ersten Zeilen selbst, wie es aufgerufen wird. Lizenz: GPL-2.0-only (siehe `LIZENZEN.md`).

**Wichtig:** Die Bau-Werkzeuge laufen aus `~/redmi-build/` (Kopien). Nach einer Änderung im Repo also kopieren, z. B.
`cp werkzeuge/pc/vendorboot_rezept.sh werkzeuge/pc/deploy.sh ~/redmi-build/`.
Seit 05.11. geht der vendor_boot-Bau auch ganz aus dem Repo, ohne Dateien, die nur auf unserem PC liegen:
`BASIS_DTB` = Gerätebaum aus dem Kernel-Bau, `VB` = Hülle aus `vendorboot_basis.sh`, `AUSGABE` = Zielordner (siehe Kopf von `vendorboot_dt.sh`).
Ohne diese Angaben arbeiten die Skripte wie bisher (Gegenprobe 05.11.: Byte für Byte gleiches Image). Die Handy-Skripte liegen auf dem Handy
unter `/usr/local/sbin/`, die Dienste unter `/etc/systemd/system/`, die Einstellungen unter `/etc/redmi/`.

## `pc/` – bauen, packen, aufspielen (läuft am PC)

| Werkzeug | Zweck |
|---|---|
| `vendorboot_rezept.sh` | **Alle** unsere Gerätebaum-Änderungen an einer Stelle (Schalter wie `GPU`, `SD`, `PANELKERNEL`, `GPUKUEHL`) → `vendor_boot_rezept.img` |
| `vendorboot_dt.sh` | vendor_boot mit Gerätebaum-Änderungen neu bauen (wird vom Rezept benutzt) |
| `vendorboot_basis.sh` | leere vendor_boot-Hülle von Grund auf bauen (Gerätebaum + leere Ramdisk + bootconfig, nichts von Xiaomi); ersetzt das alte `vendor_boot_usb2.img` |
| `wrap_dtb.py` | Gerätebaum in die DT-Tabelle des Bootloaders verpacken (wird von den vendor_boot-Skripten benutzt) |
| `misc_slot_b.py` | `misc_b.img` erzeugen: Bootloader startet Slot b, nach 7 Fehlversuchen wieder Slot a; mit `--ok` die 32 Bytes für `redmi-boot-ok.service` |
| `pack.sh` | `boot_mainline.img` aus dem frisch gebauten Kernel packen und prüfen, ob wirklich der aktuelle Kernel drinsteckt |
| `deploy.sh` | Kernel, vendor_boot und alle Module per SSH auf Slot b schreiben, Slot b aktiv schalten, neu starten |
| `net.sh` | USB-Netz zum Handy einrichten (nach jedem Handy-Neustart) |
| `schlank.sh` | alle ARM64-Plattformen außer Unisoc abschalten (kleiner Kernel, passt in den Ladebereich des Bootloaders) |
| `kernel_logo.py` | beliebiges Bild in ein Kernel-Startlogo umwandeln (die Ziege) |
| `redmi_logo.py` | Startlogo des Bootloaders (`logo.bin`) aus- und einpacken |

## `handy/` – Dienste und Werkzeuge auf dem Handy

| Werkzeug | Zweck |
|---|---|
| `redmi-licht.sh` + `.service` + `licht.conf` | Display-Licht und -Schlaf über den Treiber (Ersatzweg per DCS); Server: Licht aus, Einschalttaste kurz = 30 s an |
| `redmi-akku.sh` + `.service` + `akku.conf` | Ladegrenze für den Server-Betrieb (Ladechip SGM41513) |
| `redmi-cpufreq.sh` + `.service` | cpufreq-Modul beim Start laden |
| `redmi-taste.sh` + `.service` | Einschalttaste: kurz = Licht, mindestens 3 s halten = Herunterfahren |
| `redmi-usb.sh` + `.service` | USB-Rolle automatisch wählen: PC → Gerät (USB-Netz), Hub mit Netzteil → Host |
| `redmi-wlan.sh` + `.service` + `wlan.conf` | WLAN mit Schalter: beim Start an/aus nach `/etc/redmi/wlan.conf`, `redmi-wlan.sh an\|aus\|status`; Zugangsdaten in `/etc/redmi/wpa-wlan0.conf` (nie ins Repo) |
| `30-wlan0.network` | WLAN holt per DHCP eine Adresse (Metrik 600, USB-Netz bleibt bevorzugt) |
| `redmi-usb-gadget.sh` + `.service` | USB-Gerät: Netz (NCM) + serielle Konsole (ACM), meldet jeden Schritt im Kernel-Log (05.11. ins Repo, läuft seit 27.09.) |
| `10-usb0.network` | USB-Netz: Handy 192.168.7.2, PC 192.168.7.1 als Gateway (systemd-networkd, `/etc/systemd/network/`) |
| `redmi-boot-ok.service` | nach gelungenem Start Slot b im `misc` als „erfolgreich“ markieren (32 Bytes `abc_b_ok.bin` aus `misc_slot_b.py --ok`, liegt unter `/usr/local/lib/redmi/`) – sonst startet nach 7 Starts wieder Android |
| `20-lan.network` | USB-LAN-Adapter holt sich per DHCP eine Adresse (systemd-networkd) |
| `redmi-kmsg.sh` | Kernel-Meldungen laufend auf die eMMC schreiben (für Abstürze) |
| `redmi-dcsfrage.sh` | Panel per DCS-Lesebefehl nach seinem Zustand fragen (`0x0A` = 0x9C heißt gesund); fragt nicht, wenn das Display aus ist |
| `redmi-check.sh` | prüft am laufenden Handy, was wirklich funktioniert (vor und nach jedem neuen Kernel) |

## `tests/` – messen und prüfen

| Werkzeug | Zweck |
|---|---|
| `redmi-lichtmessung.sh` | Akkustrom ohne Kabel: Display an, Schlaf alter Weg, Schlaf über den Treiber |
| `redmi-paneltest.sh` | Panel-Schlaf per DCS mit Strommessung (Stand 08.10., alter Weg) |
| `redmi-vibratest.py` | Vibrationsmotor kurz anwerfen |
| `redmi-otgtest.sh` | einmaliger Test USB-Host (OTG) |
| `redmi-lantest.sh`, `redmi-lan-dauer.sh`, `lan-dauertest.sh` | USB-LAN-Adapter (RTL8153): Senden, Dauertest am Router |
| `redmi-wcntest.sh` | WCN-Kern (BT/WLAN) stufenweise starten (`kernel/wcn-test/`) |
| `redmi-wlantest.sh` | WLAN-Testkette starten und zusammenfassen (läuft am PC) |
| `redmi-wcnstrom.sh` | Stromverbrauch des WCN-Kerns messen |
| `startreihe.sh` | Startversuche protokollieren (`belege/startreihe.csv`) |
| `stufentest.sh`, `wechseltest.sh` | cpufreq: jede Stufe unter Volllast bzw. Takt-Wechsel prüfen |

## `analyse/` – Android- und Firmware-Dateien lesen (ändert nichts)

| Werkzeug | Zweck |
|---|---|
| `dtbo_auspacken.py` | dtbo-Partition in einzelne Overlays zerlegen (Panel-Daten, Einschaltfolgen) |
| `dt_tabelle.py` | Android-DT-Tabelle (dtb/dtbo) aufschlüsseln |
| `dtvergleich.py` | Android-Gerätebaum gegen den eigenen Kernel vergleichen (was fehlt, welcher Treiber) |
| `dt_vergleich.py` | Ton- und WLAN-Knoten aus zwei Gerätebäumen nebeneinander |
| `lp_auspacken.py` | Inhaltsverzeichnis der `super`-Partition |
| `ko_steckbrief.sh` | Steckbrief für Android-Kernelmodule (`.ko`) |
| `nvt_id.py`, `nvt_ko_tabelle.py` | Novatek-Touch: Kennung per spidev, Chip-Tabelle aus dem Xiaomi-Modul |
| `pmsys_bootcode.py` | Startcode des Hilfskerns pmsys (Modem-Untersuchung, ruht) |

## `alt/` – überholt

Diese Skripte haben früher Änderungen in den Kernel-Quelltext geschrieben. Die Änderungen stecken inzwischen als
Patches in `kernel/patches/`; die Skripte bleiben nur zum Nachvollziehen.

| Werkzeug | Zweck damals |
|---|---|
| `patch_poweroff.py` | echtes Ausschalten über den PMIC SC2730 |
| `patch_fgu_temp.py` | Akkutemperatur über eine Spannungs-Temperatur-Tabelle |
| `patch_dvfs_debug.py` | Kontrollausgaben im cpufreq-Treiber (Fehlersuche) |
