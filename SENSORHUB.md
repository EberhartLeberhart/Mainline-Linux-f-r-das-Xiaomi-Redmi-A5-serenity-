# Sensorhub (PMSYS) – Stand 24.11.2026

Lage-, Beschleunigungs-, Licht- und Annäherungssensoren hängen beim UMS9230 nicht am normalen I2C des
Hauptprozessors, sondern an einem eigenen kleinen Prozessor im Always-on-Bereich: dem **PMSYS** bzw.
„SP“ (Sensor-Prozessor), einem **Cortex-M4**. Linux redet mit ihm über SIPC und Mailbox, genau wie mit
dem WCN-Kern (WLAN/Bluetooth). Diese Seite hält fest, was wir darüber wissen und wo es hängt.

**Keine Herstellerdateien im Repo:** Die Firmware (`pm_sys`), der Startcode und die Android-Programme
stammen aus den Partitionen des eigenen Handys und bleiben privat. Hier stehen nur Adressen, Abläufe und
eigene Messungen.

## Kurzfassung

- Laden klappt: Startcode und Firmware liegen nachweislich richtig im Speicher, genau wie unter Android.
- **Starten klappt nicht:** Das Startbit (AON `0x8c` Bit 0) lässt sich vom normalen Kernel nicht löschen.
  Das passt zu MODEM.md, Stufe 4 (11.10.): Die Steuerregister von Modem **und** PMSYS sind gesperrt.
- Android gibt den Start über **trusty** frei: `modem_control` lässt die Firmware vom trusty-Dienst
  `kernelbootcp` prüfen und den Speicher freigeben, erst dann wird gestartet.
- **Entscheidung 24.11.:** trusty bleibt als Sicherheitseinrichtung erhalten. Wir bauen einen eigenen
  trusty-Treiber nach dem Vorbild von Androids GPL-Treiber und benutzen trusty über seine offizielle
  Schnittstelle, mit der unveränderten Original-Firmware. An trusty und an den Prüfungen wird nichts
  verändert oder umgangen. **Das Modem bleibt außen vor** (siehe [MODEM.md](MODEM.md)).

## Hardware und Speicher

| Teil | Wert | Quelle |
|---|---|---|
| Kern | Cortex-M4 („cm4“, „sp“) | u-boot-Meldungen, Android-Konfiguration |
| Startbit | AON-APB `0x64000000` + `0x8c`, Bit 0 (1 = im Reset) | Original-Gerätebaum `modem@0` (`corereset`), Ottos `sprd_pmsys.c` |
| M4-Speicher (iRAM) | `0x62010000`, Größe `0x4e000` (312 KB); aus M4-Sicht ab Adresse 0 | Android-Konfiguration, u-boot-Minidump |
| Firmware im DDR | `0x88040000` (Bereich bis `0x88300000`); aus M4-Sicht `0x10040000` | Android-Konfiguration, Startcode |
| SIPC im DDR | `0x88000000`, `0x40000` groß; aus M4-Sicht `0x18000000` | Original-Gerätebaum `sipc-pmsys` |
| SIPC im SRAM | `0x6500a400`, `0x2000` groß; aus M4-Sicht `0x2000a400` | Original-Gerätebaum `sipc-pmsys` |
| Mailbox | Kanäle 1 und 6 | Original-Gerätebaum |
| Kanäle | 4 `spipe_pm` (Sensoren), 5 `slog_pm` (Log), 6 `sctl_pm`, 7 `data0_gnss`, 41 `stime_pm` | Original-Gerätebaum |

**Hinweis für später:** Ottos Gerätebaum kennt nur den DDR-Teil der SIPC-Verbindung. Ob die Firmware den
SRAM-Teil braucht, zeigt sich erst, wenn der M4 läuft ❓.

## Der Startcode

Der M4 braucht vor seiner Firmware ein kleines Startprogramm (124 Bytes): eine Vektortabelle mit
Stapelzeiger `0x3800`, dann richtet es zwei Adressfenster ein (Bausteine bei `0x61600000` und
`0x61400000`) und springt nach `0x10040000`, also an den Anfang von `pm_sys`. Das passt zu den
`0x100…`-Adressen im Startcode der Firmware, die selbst keine Vektortabelle hat.

Woher er kommt (24.11.):
- Er steht **im u-boot** als festes Feld; u-boot hängt ihn als `sprd,iram-data` an einen Knoten mit
  `sprd,name = "pmic"` im Kernel-Gerätebaum. Den Knoten gibt es aber weder in Xiaomis noch in unserem
  Gerätebaum – dieser Weg läuft also auch unter Android ins Leere.
- Unter Android lädt ihn `modem_control` aus seiner Konfiguration (`/vendor/etc/modem_sp_info.xml`).
  Inhalt dort und im u-boot: **Wort für Wort gleich.**

Ottos Treiber erwartet ihn als `sprd/pm_bootcode.bin`. Für den späteren Installer: aus der eigenen
`uboot`-Partition des Nutzers herausschneiden (Suche nach der Signatur am Anfang), nicht ins Repo legen.

## So startet Android den Sensor-Prozessor

1. `modem_control` liest `modem_sp_info.xml`: zwei Teile, Firmware (`pm_sys`, Ziel `0x88040000`) und
   Startcode (Ziel `0x62010000`). Die Firmware ist mit dem Merkmal **„sec“** gekennzeichnet.
2. Es schreibt beides über `/dev/pmsys` (Kernel-Modul `sprd_modem_loader`, GPL v2) in den Speicher.
3. Es lässt die Abbilder von **trusty** prüfen und den Speicher freigeben (`secure_boot_verify_all`,
   `secure_boot_unlock_ddr` über `libkernelbootcp.trusty.so`, Dienst `com.android.trusty.kernelbootcp`).
4. Es startet über `/dev/pmsys`. Der Kernel-Treiber macht dabei **nur** eines: AON `0x8c` Bit 0 löschen
   (`regmap_update_bits`, dazu 2 × 50 ms Wartezeit). Alle anderen Steuerbits sind bei PMSYS nicht belegt.

Schritt 4 ist also genau das, was Ottos Treiber auch tut. Der Unterschied ist Schritt 3.

## Versuch auf Handy 2 (24.11., Kernel `g3815c63dc8c1`)

Ottos Treiber `SPRD_PMSYS_REMOTEPROC` und `IIO_SPRD_SHUB` (mit allen Unteroptionen) als Module gebaut,
Firmware aus der eigenen Partition `pm_sys_a`, Startcode aus dem eigenen u-boot.

| Schritt | Ergebnis |
|---|---|
| Modul laden | `remoteproc2: pmsys`, „remote processor pmsys is now up“, Handy stabil |
| Startcode im M4-Speicher | richtig (`00003800 00000009 …`) ✅ |
| Firmware im DDR | richtig (`d094f8df …`) ✅ |
| SIPC-Speicher | Kopf von Kanal 4 von Linux angelegt (2 Puffer, `0x180`/`0xb00`), M4-Seite unverändert |
| Mailbox | keine einzige Nachricht vom M4 |
| Startbit AON `0x8c` | **bleibt 1** – weder der Treiber noch ein direkter Schreibzugriff noch die Lösch-Spiegeladresse (`+0x2000`) ändern es |

remoteproc meldet „running“, weil es den Schreibzugriff nicht zurückliest. **Der M4 steht weiter im Reset.**

## Was ausgeschlossen ist

| Verdacht | Prüfung | Ergebnis |
|---|---|---|
| falsches Register auf dem UMS9230E | Original-Gerätebaum `modem@0`: `corereset = <&aon_apb 0x8c 0x1>` | Register stimmt |
| Startbit nur über Spiegeladresse schreibbar | Lösch-Adresse `0x6400208c` | keine Wirkung |
| AON-Sperre mit Magie `0x6896` (`aon-apb-prot`) | Suche in SPL, ATF (`sml`), trusty (`tos`), `teecfg`, u-boot | Wert kommt nirgends vor; Android lädt auch keinen Treiber dafür |
| zusätzliche Busfreigabe | `sprd,sys-bus-ctrl` im Android-Treiber | PMSYS-Knoten hat keine |
| Startcode oder Adressen falsch | Vergleich mit Android-Konfiguration | identisch |
| trusty über SMC unerreichbar | MODEM.md, Stufe 5 (11.10.) | trusty antwortet dem Mainline-Kernel |

## Plan: eigener trusty-Treiber

trusty läuft schon (lk lädt es vor dem Kernel) und antwortet auf Fast-Calls. Es fehlt die Verbindung für
die Kanäle (trusty-IPC). Vorbild ist Androids `drivers/trusty` (GPL) bzw. Unisocs Fassung
(`sprd,trusty-smc-v1`, `-irq-v1`, `-virtio-v1`, `-log-v1`, `sprd,tshm`).

| Etappe | Ziel | Sichtbares Ergebnis |
|---|---|---|
| 0 | Bestandsaufnahme ✅ 24.11. | Androids trusty-Module (`trusty`, `-irq`, `-log`, `-virtio`, `-ipc`, `-pm`, `-tui`, `ion_ipc_trusty`) alle **GPL v2**; Quelltext für genau unseren Chip offen bei mer-hybris/s19mps-kernel `drivers/trusty`; trusty-Speicher `0xb0000000`–`0xb60fffff` ist bei uns reserviert |
| 1 | Grundtreiber ✅ 24.11. | „Unisoc TEE v2.1.1“, API-Version 2 (Testmodul `kernel/trusty-test`) |
| 2 | Interrupts und Log ✅ 24.11. | Knoten `/trusty` (`TRUSTY=1` im Rezept), trusty-irq meldet SGI 13 an, trusty-log teilt 16 KB Log-Speicher; erste Standard-Calls laufen sauber |
| 3 | IPC ✅ 24.11. | Knoten `/trusty/virtio`, Kanal „is online“, `/dev/trusty-ipc-dev0` da; virtio-ID 13 auf private ID umgelegt (13 ist in Mainline der Speicher-Ballon); `trusty-ipc` muss vor `trusty-virtio` geladen werden |
| 4 | `kernelbootcp`: Firmware prüfen lassen, Speicher freigeben (Protokoll siehe MODEM.md) | trusty gibt frei |
| 5 | Sensorhub starten | Sensoren als IIO-Geräte |

Grundsätze: offizielle Schnittstelle, unveränderte Original-Firmware, trusty entscheidet selbst. Kein
Eingriff in trusty, ATF oder die Prüfungen. Das Modem wird dabei nicht gestartet.

**Offene Frage ❓:** Ob auch der Decoder (siehe [DECODER.md](DECODER.md)) an einer trusty-Freigabe hängt,
ist nicht bewiesen. `modem_control` greift zusätzlich auf `/dev/dmc_mpu` zu; das ist eine Spur für später.
