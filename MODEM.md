# Mobilfunk-Modem (LTE) des Redmi A5 – Stand der Untersuchung

Ziel: Das eingebaute LTE-Modem unter Mainline-Linux selbst starten und steuern (Ersatz-Internet).
Wie beim WCN: erst lesen und belegen, dann Schritt für Schritt starten. Regeln wie in STATUS.md.

**Keine Firmware, keine Dumps im Repo:** Hier stehen nur Namen, Adressen und Kennungen.

## Was im Chip steckt (aus FIRMWARE_ANALYSIS.md und Xiaomis Gerätebaum)

| Teil | Bedeutung | Gerätebaum |
|---|---|---|
| PUBCP / WTLCP | Modem-Kern(e): LTE/4G sowie 2G/3G (Sprache, SMS) | `modem@1` (`compatible = "unisoc,modem"`, `sprd,version = 2`, Alias `modem`) |
| pmsys | Stromspar-Hilfskern (`pm_sys.img`) | `modem@0` (Alias `pmsys`); läuft vermutlich schon ab Bootloader ❓ |
| sipc-lte | Nachrichtenweg AP ↔ Modem | `core@5`, Mailbox-Kanal 2, `sprd,smem-info = <0x8e000000 0x8e000000 0x900000>` |
| seth | Netzwerkschnittstellen des Modems | `seth0`–`seth13` = `/sipc-virt/core@5/channel@7…31` |

Steuerbits von `modem@1` (syscon-Phandles im Xiaomi-DT, aufgelöst 11.10.: `0x11` = **AON-APB** `0x64000000`,
`0x12` = **PMU-APB** `0x64020000` – dieselben Blöcke, die der WCN-Testtreiber schon nutzt):

| Name | syscon | Register | Maske | Adresse |
|---|---|---|---|---|
| shutdown | 0x12 | 0x330 | 0x2000000 | 0x64020330 |
| deepsleep | 0x12 | 0x818 | 0x02 | 0x64020818 |
| corereset | 0x11 | 0x174 | 0x400 | 0x64000174 |
| sysreset | 0x12 | 0xb98 | 0x800 | 0x64020b98 |
| getstatus | 0x11 | 0xff | 0x00 | – (ungültig) |
| dspreset | 0x12 | 0xb88 | 0x18 | 0x64020b88 |

Zusätzlich `sprd,decoup = "cproc-use-decoup"`. Bei `sprd,version = 2` stehen keine Ladeadressen im Gerätebaum;
unter Android lädt das Programm `modem_control` die Abbilder über den Treiber (Modul `sprd_modem_loader.ko`).

## Firmware-Abbilder (Stock-ROM A15.0.20.0.VGWMIXM, Ordner `images/`)

Geprüft 10.10. (am PC, nur gelesen):

| Datei | Größe | Anfang | `DHTB` irgendwo in der Datei |
|---|---|---|---|
| `l_modem.img` | 25 MB | `MECPV1.0` (Container, s. u.) | **0** |
| `l_ldsp.img` | 20 MB | `00 5A 5A 5A …` | **0** |
| `l_gdsp.img` | 10 MB | `.PSD88CS_S00.MSG` | **0** |
| `l_agdsp.img` | 6 MB | `SharkL5_AUDCP_2024Y_VER_5002 … AUDCP.SharkL6` (Audio-DSP) | **0** |
| `pm_sys.img` | 1 MB | – (enthält `GNSS_NAVCORE_VERSION`) | **0** |
| `qogirl6_pubcp_MHM_customer_nvitem.bin` | 875 KB | NV-Vorlage | – |
| `qogirl6_pubcp_MHM_customer_deltanv.bin` | 50 KB | Delta-NV | – |

**Keine Unisoc-Signatur (`DHTB`) gefunden** – wie bei `wcnmodem.bin`, die ohne trusty startet. Ob das Modem ohne
trusty-Freigabe läuft, zeigt erst ein Startversuch ❓.

Korrektur zu FIRMWARE_ANALYSIS.md: `l_gdsp` gehört nach dieser Tabelle zum **Modem** (lädt nach `0x89620000` im
Modem-Bereich), nicht zu GNSS. GNSS läuft auf dem WCN (`gnssmodem.bin`).

## Der MECP-Container (`l_modem.img`) = Ladeplan

Kopf `MECPV1.0`, danach Einträge zu je 0x30 Bytes: Name (16), u64 (immer 0), u64 Adresse, u64 Größe, u64 Art.

| Eintrag | Adresse | Größe | Art | Bedeutung |
|---|---|---|---|---|
| `sipc-mem` | `0x87800000` | 8 MB | – | sipc-Speicher (weicht vom Gerätebaum ab: dort `0x8e000000`, 9 MB ❓) |
| `cp-modem` | `0x89600000` | 73 MB | – | gesamter Modem-Speicher |
| *(Startcode bei Dateiversatz `0xD0`)* | – | – | – | ARM32-Code (Cache/MMU-Register), springt nach `0x8B001ACC` |
| `modem` | `0x8B000000` | 19,5 MB | 1 | Modem-Firmware |
| `cpcmdline` | `0x8AF20000` | 2 KB | 2 | Befehlszeile für das Modem |
| `deltanv` | `0x8AF30000` | 128 KB | 1 | NV-Daten (Partition `l_deltanv`) |
| `fixnv` | `0x8CEA0000` | 1 MB | 1 | NV-Daten (Partition `l_fixnv1/2`) |
| `runnv` | `0x8CFA0000` | 1,1 MB | 1 | NV-Daten (Partition `l_runtimenv1/2`) |
| `gdsp` | `0x89620000` | 4,25 MB | 1 | DSP (Partition `l_gdsp`) |
| `ldsp` | `0x89AA8000` | 11 MB | 1 | LTE-DSP (Partition `l_ldsp`) |
| `cp-mem` | `0x89600000` | 73 MB | – | wie `cp-modem` |
| `mini-dump` | `0x8D163B84` | 1 KB | – | Absturzinfo |

Vermutung: Art 1 = aus Partition laden, Art 2 = Text (Befehlszeile) ❓. Alle Ladeziele liegen im Bereich
`0x89600000–0x8DEFFFFF`.

**Speicher unter Mainline:** Der Gerätebaum (Patch 0001) reserviert schon `cp@89600000` (`0x4900000` = genau `cp-mem`)
und `sipc@8e000000` (`0x900000`). `0x87800000–0x87ffffff` (`sipc-mem` laut Container) ist **nicht** reserviert –
erst klären, ob das Modem diesen Bereich wirklich nutzt ❓.

## Der Kernel-Treiber (Realme `drivers/unisoc_platform/modem_loader/sprd_modem_loader.c`, 1 366 Zeilen)

Der Kernel-Teil ist **dünn**. Er legt je Modem ein Gerät an (`modem@0` → `pmsys`, sipc-Ziel PM_SYS;
`modem@1` → `modem`, sipc-Ziel PSCP) und bietet über ioctl nur an:
Ladeplan setzen/lesen (`MODEM_SET/GET_LOAD_INFO`), Schreib-/Lesebereich wählen, Speicher beschreiben (`write`),
Start/Stopp (`MODEM_START_CMD`/`MODEM_STOP_CMD`), Assert, Schlaf. **Das MECP-Format liest der Kernel nicht** – das macht
unter Android das (nicht freie) Programm `modem_control`: Es zerlegt `l_modem`, schreibt Firmware, DSPs, NV-Daten und
Befehlszeile an die Adressen aus der Tabelle und ruft dann START.

Startfolge (`soc_modem_start`), die Bits kommen aus `syscon0…5` (Format `<regmap register maske>`, „löschen“ = Bits auf 0):

1. shutdown löschen (`0x12`:`0x330` Maske `0x2000000`)
2. deepsleep löschen (`0x12`:`0x818` Maske `0x02`)
3. 50 ms warten
4. sysreset löschen (`0x12`:`0xb98` Maske `0x800`)
5. corereset löschen (`0x11`:`0x174` Maske `0x400`)
6. 50 ms warten

Stopp (`soc_modem_stop`) genau umgekehrt (setzen statt löschen). `getstatus` hat Register `0xff` = „ungültig“ und wird
übersprungen; `dspreset` (`0x12`:`0xb88` Maske `0x18`) nutzt der Kernel-Treiber nicht selbst.

Was `modem_control` außer dem Laden noch tut (Reihenfolge, Wartezeiten, ob `dspreset` geschaltet wird, was in
`cpcmdline` steht, Umgang mit `sipc-mem`), ist **nicht** in der Quelle. Spur: das Programm und seine `.rc`-Datei aus der
`vendor`-Partition lesen (nur Texte/Aufrufe, ohne es auszuführen) ❓.

## Android-Seite: `modem_control` (aus `vendor_a`, gelesen 11.10., nur Texte/Konfiguration)

Dienst: `/vendor/etc/init/modem_control.rc` startet `/vendor/bin/modem_control` als root ohne Parameter (`class core`).
Ladeplan steht in **`/vendor/etc/modem_cp_info.xml`** (Modem) und **`/vendor/etc/modem_sp_info.xml`** (pmsys).

**Modem (`cp`)**, Gerät `/dev/modem`, Quelle `/dev/block/by-name/`:
`modem_range` = `0x89600000`/`0x4900000` (= `cp@89600000` im DT), `all_range` = `0x87800000`/`0x7100000`
(bis `0x8E8FFFFF`, schließt `sipc@8e000000` ein – damit erklärt sich `sipc-mem 0x87800000` im MECP-Kopf als Anfang
dieses Gesamtbereichs, nicht als eigener sipc-Speicher ❓).

| Teil | Partition | Flag | Bedeutung der Flag-Bits (laut XML-Kommentar) |
|---|---|---|---|
| modem | `l_modem` | `0x201` | sec + modem |
| deltanv | `l_deltanv` | `0x202` | nv + modem |
| gdsp, ldsp | `l_gdsp`, `l_ldsp` | `0x401` | sec + modem-dsp |
| cdsp, warm | `l_cdsp`, `l_warm` | `0x401` | wie oben (gibt es auf dem A5 vermutlich nicht ❓) |
| cpcmdline | (erzeugt) | `0x808` | cmdline + other modem |
| fixnv, runnv | `l_fixnv`, `l_runtimenv` | `0x802` | nv + other modem |
| modem_bootcode | – | `0x104` | boot iram + modem head: **`0x65003400`, 0x1000 Bytes** |

Der Startcode aus dem MECP-Kopf (Dateiversatz `0xD0`, springt nach `0x8B001ACC`) gehört also nach **`0x65003400`**
(iRAM); der Modem-Kern beginnt dort.

**pmsys (`sp`)**, Gerät `/dev/pmsys`: `pm_sys` nach `0x88040000` (0x200000), Startcode **direkt im XML** (32 Wörter
Thumb-Code) nach `0x62010000` (pmsys-iRAM, `modem_range` `0x62010000`/`0x4e000`). Android startet pmsys **vor** dem
Modem und setzt danach `vendor.modem.sp-boot=1`. Unter Mainline liegt `0x88040000` im schon reservierten
`sp-ddr@88000000` (0x300000).

**Sicherheit:** `modem_control` ruft über `libkernelbootcp.trusty.so` (trusty-Dienst `com.android.trusty.kernelbootcp`,
`/dev/trusty-ipc-dev0`) zwei Dinge auf: `kernel_bootcp_verify_all` (Abbilder prüfen; es gibt die Meldung
„skip cp image verify!“) und **`kernel_bootcp_unlock_ddr`** (vermutlich Freigabe der DDR-Schutzwand `dmc_mpu` für den
Modem-Bereich). Mainline hat keinen trusty-IPC-Treiber. Ob das Modem ohne `unlock_ddr` an seinen Speicher kommt, ist
**die entscheidende offene Frage** ❓ (beim WCN war keine Freigabe nötig).

**`cpcmdline`** baut `modem_control` aus `/proc/cmdline` bzw. `/proc/bootconfig` zusammen: `modem=`, `calibration=`,
`lcd_id=`, `rfboard.id=`, `hw_ver…`, Bootmodus, `modemboot=reset` (nach Neustart des Modems),
`log_chan=sblock,…` (Adresse aus `sipc-virt/core@5/channel@5/base_addr`), Abschluss `cpcmdline=end`.
Unter Mainline reicht der Bootloader davon **nichts** an `/proc/cmdline` weiter (geprüft 11.10.: nur `kvm-arm.mode=none`
passt), `/proc/bootconfig` gibt es nicht (Kernel ohne `CONFIG_BOOT_CONFIG`) ❓.

**Nach dem Start:** `modem_control` wartet auf „Modem Alive“, setzt `sipa_dele_reset` (IPA-Einheit für den
Datenweg) und bedient Assert/Neustart. Laufzeit-Kanäle (`vendor/build.prop`):

| Eigenschaft | Gerät | Zweck |
|---|---|---|
| `ro.vendor.modem.tty` | `/dev/stty_lte*` | AT-Befehle (`stty_lte2` u. a.) |
| `ro.vendor.modem.eth` | `seth_lte*` | Datenschnittstellen |
| `ro.vendor.modem.nv` | `/dev/spipe_lte1` | NV-Dienst: Modem liest/schreibt NV zur Laufzeit ❓ |
| `ro.vendor.modem.assert` | `/dev/spipe_lte2` | Absturzmeldungen |
| `ro.vendor.modem.loop` | `/dev/spipe_lte0` | Prüfschleife |
| `ro.vendor.modem.log`/`diag` | `/dev/slog_lte`, `/dev/sdiag_lte` | Log/Diagnose |
| `ro.vendor.modem.tmctl` | `/dev/sprd_time_sync` | Zeitabgleich (`refnotify`) |

Nebenprogramme: `refnotify` (Uhrzeit AP ↔ Modem), `modem_ctrl_dbg` (Start/Stopp über `modem_control`),
`nvmerge` (NV-Zusammenführung bei Updates, für uns unwichtig).

## Messung am Handy: Stufe 0 (11.10., Mainline, `modem_starttest.ko`, nur lesen)

Kein Absturz – **der AP darf Modem-DDR, pmsys-DDR, sipc und beide iRAMs ohne trusty lesen.**

| Was | gelesen | Deutung |
|---|---|---|
| shutdown PMU `0x330` | `0x00010101` → Bit 25 = 0 | Modem nicht abgeschaltet ❓ |
| deepsleep PMU `0x818` | `0x00000004` → Bit 1 = 0 | |
| sysreset PMU `0xb98` | `0x00004000` → Bit 11 = 0 | |
| dspreset PMU `0xb88` | `0x00000018` → gesetzt | DSPs im Reset |
| corereset AON `0x174` | `0x00000000` → Bit 10 = 0 | Kern nicht im Reset ❓ |
| pmsys-iRAM `0x62010000` | 1024/1024 Wörter ≠ 0, ungeordnet | kein Startcode aus der XML → pmsys vermutlich **nicht** gestartet ❓ |
| Modem-iRAM `0x65003400` | AArch64-Befehle (`mov`, `ldr`, `bl`) | Code des AP (Bootloader/ATF?), **nicht** der ARM32-Startcode des Modems – vor dem Beschreiben klären, ob der AP diesen Bereich noch braucht ❓ |
| pmsys-DDR `0x88040000`, Modem-DDR `0x89600000` | gleiches Streifenmuster (`0000ffff`), 467/1024 ≠ 0 | gleicher Inhalt an verschiedenen Adressen → Rest eines Speichertests, **nichts geladen** |
| Firmware `0x8B000000`, sipc `0x8E000000` | gleiches (anderes) Streifenmuster, 472/1024 ≠ 0 | ebenso |
| `cpcmdline` `0x8AF20000` | leer | |

Die Steuerbits stehen also auf „laufen“, obwohl nichts geladen ist. Ob der Kern dabei wirklich Strom hat und
was er ausführt, ist offen ❓.

## Messung am Handy: Stufe 1 (11.10., Schreibtest)

Je 16 Wörter geschrieben, zurückgelesen, alter Inhalt wiederhergestellt – kein Absturz:

| Seite | Ergebnis |
|---|---|
| WCN `0x87700000` (Vergleich) | 16/16 übernommen |
| pmsys-DDR `0x882FF000` | 16/16 übernommen |
| Modem-DDR `0x8DEFF000` | 16/16 übernommen |
| sipc `0x8E8FF000` | 16/16 übernommen |

**Die Firewall ist unter Mainline offen: Der AP darf den Modem-Speicher ohne trusty beschreiben.**
`kernel_bootcp_unlock_ddr` ist also (in diesem Zustand nach dem Bootloader) nicht nötig. Ob der *Modem-Kern* selbst
ohne trusty an seinen Speicher kommt, zeigt erst der Startversuch ❓.

## Aufbau von `l_modem.img` und pmsys-Steuerung (11.10., am PC, nur Struktur gelesen)

- Datei 25 MB (`0x1900000`), Daten bis `0xD57600`, danach leer (Ende ebenfalls leer).
- Kopf: `MECPV1.0`, Einträge ab `0x10` und ab `0x1A0` (je 0x30 Bytes); erstes Feld überall 0 – **kein Dateiversatz**.
  Am Kopfende zusätzlich `cp-mem`, `mini-dump` (`0x8D163AFC`, 0x400), `smem` (`0x87800000`, 8 MB) und `ring`.
- Bei `0xD0`: Anzahl `0x31` (49 Wörter), danach der ARM32-Startcode für den Modem-iRAM `0x65003400`.
- **Ladeweg:** Die Datei wird ab Versatz 0 (mit Kopf) nach `0x8B000000` geladen – bei Dateiversatz `0x1ACC`
  (= Sprungziel `0x8B001ACC` des Startcodes) stehen ARM32-Befehle.
- pmsys (`modem@0`) hat nur **ein** gültiges Steuerbit: corereset = AON `0x8c` Bit 0 (alle anderen `0xff`).
- iRAM `0x6500xxxx` wird geteilt: `0x65003400` Modem-Startcode, `0x65009400` Audio-DSP, `0x6500a400` sipc von
  pmsys (pmsys-Sicht `0x2000a400`, `sipc-pmsys`, Mailbox-Kanal 6).

## Offen / nächste Schritte

1. ~~Dateiversatz der `modem`-Daten~~ ✅ 11.10.: ganze Datei ab 0 nach `0x8B000000` (s. o.).
2. ~~Treiber `unisoc,modem` (v2) und `modem_control` lesen~~ ✅ (s. o.). Offen: Werte für `cpcmdline` (was der Bootloader
   unter Android übergibt), DDR-Freigabe ohne trusty, NV-Dienst auf `spipe_lte1`.
3. ~~Phandles `0x11`/`0x12` auflösen~~ ✅ 11.10.: AON-APB / PMU-APB (s. o.).
4. Stufenweiser Testtreiber `kernel/modem-test/modem_starttest.c` (wie beim WCN): Stufe 0 = lesen ✅ 11.10.
   (s. o.), Stufe 1 = Schreibtest ✅ 11.10. Stufe 2 = pmsys starten (Startcode aus `modem_sp_info.xml` per
   `werkzeuge/pmsys_bootcode.py`, erste zwei Wörter = SP und Reset-Vektor eines Cortex-M) ❓. Danach Speicher/Firmware → Kern loslassen → sipc core@5.
5. NV-Daten: enthalten u. a. Kalibrierung und Gerätekennungen; sie bleiben auf dem Handy und gehören nicht ins Repo.
