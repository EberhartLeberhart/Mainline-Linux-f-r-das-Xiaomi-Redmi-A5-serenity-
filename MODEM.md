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

Steuerbits von `modem@1` (syscon: `0x11`/`0x12` = Phandles im Xiaomi-DT, Zuordnung zu aon-apb/pmu-apb noch prüfen ❓):

| Name | syscon | Register | Maske |
|---|---|---|---|
| shutdown | 0x12 | 0x330 | 0x2000000 |
| deepsleep | 0x12 | 0x818 | 0x02 |
| corereset | 0x11 | 0x174 | 0x400 |
| sysreset | 0x12 | 0xb98 | 0x800 |
| getstatus | 0x11 | 0xff | 0x00 |
| dspreset | 0x12 | 0xb88 | 0x18 |

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

## Offen / nächste Schritte

1. Wo im `l_modem.img` die eigentlichen `modem`-Daten beginnen (Dateiversatz) und wie der Startcode sie erwartet.
2. Treiber `unisoc,modem` (v2) und `modem_control` in der Realme-Quelle lesen: Ladeweg, Reihenfolge der Steuerbits,
   was über `cpcmdline` übergeben wird.
3. Phandles `0x11`/`0x12` im Xiaomi-DT auflösen (aon-apb / pmu-apb).
4. Danach – wie beim WCN – ein stufenweiser Testtreiber: lesen → Speicher/Firmware → Kern loslassen → sipc core@5.
5. NV-Daten: enthalten u. a. Kalibrierung und Gerätekennungen; sie bleiben auf dem Handy und gehören nicht ins Repo.
