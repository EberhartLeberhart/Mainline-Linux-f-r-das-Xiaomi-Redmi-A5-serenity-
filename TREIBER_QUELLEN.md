# Treiber-Quellen für Ton, WLAN, Bluetooth und GNSS (Stand 09.10.2026)

Xiaomi veröffentlicht für das Redmi A5 nur den GKI-Kern (`MiCode/Xiaomi_Kernel_OpenSource`, Zweig `arctic-w-oss`).
Die Herstellermodule für Ton und Funk (vendor_ramdisk, vendor_dlkm) fehlen dort. Diese Seite hält fest,
wo es den Quellcode trotzdem gibt und wie gut er zum Redmi A5 passt.

**Keine Firmware, keine Dumps:** Hier stehen nur Namen, Kennungen und Adressen. Firmware (Audio-DSP, VBC,
WCN) ist nicht frei und bleibt auf dem Handy.

## Die Quelle

**Realme C33, Kernel 5.4, gleicher Chip (UMS9230 / qogirl6):**
[github.com/Kiciuk/unisoc-5.4](https://github.com/Kiciuk/unisoc-5.4), Stand „Upload realme_C33 AndroidS kernel source“, 24.10.2022. Lizenz GPL.

| Teil | Pfad in der Realme-Quelle |
|---|---|
| sipc (Kern-zu-Kern-Nachrichten) | `drivers/soc/sprd/modem/sipc/`, `drivers/unisoc_platform/sipc_virt_bus/` |
| Audio-Unterbau | `drivers/unisoc_platform/audio/sprd_audio/` (`audiosipc`, `audiomem`, `audio_pipe`, `mcdt/mcdt_r2p0`, `agdsp_access`, `audiocpboot`, `audiodvfs`) |
| VBC v4 + FE-DAI | `drivers/unisoc_platform/audio/sprd/dai/vbc/v4/` |
| Codec SC2730 (im PMIC) + Kopfhörer | `drivers/unisoc_platform/audio/sprd/codec/sprd/sc2730/` |
| Soundkarte | `drivers/unisoc_platform/audio/sprd/sprd-asoc-card-utils.c`, `vbc-rxpx-codec-sc27xx.c` |
| PCM/DMA-Plattform | `drivers/unisoc_platform/audio/sprd/platform/` |
| WCN-Start (Firmware laden, Strom) | `drivers/unisoc_platform/sprdwcn/` |
| WLAN SC2355 über sipc | `drivers/unisoc_platform/wlan/marlin3_sipc/` |
| Bluetooth, FM | `drivers/kernel_modules/kernel5.4/wcn/bluetooth/`, `.../wcn/fm/` |
| Gerätebaum | `arch/arm64/boot/dts/sprd/qogirl6.dtsi`, `ums9230*.dtsi`, `ums9230-wcn.dtsi` (Overlay), `sc2730.dtsi` |

Xiaomis Module (5.15) wurden aus denselben Unisoc-Quellen gebaut. Das zeigen die Build-Pfade in den
Modulen, zum Beispiel `bsp/modules/kernel5.15/audio/sprd/dai/vbc/v4/vbc-phy-v4.c` und
`bsp/modules/kernel5.15/wcn/wlan/wlan_combo/sc2355/`.

## Abgleich mit dem Redmi A5

Belegt am 09.10. mit `werkzeuge/ko_steckbrief.sh` (Kennungen in Xiaomis `.ko`) und `werkzeuge/dt_vergleich.py`
(Xiaomi-Gerätebaum + dtbo-Overlay gegen die Realme-Dateien).

### Ton

| Baustein | Kennung | Adresse | Redmi = Realme |
|---|---|---|---|
| VBC | `unisoc,qogirl6-vbc` | `0x56480000` | ✅ |
| MCDT | `unisoc,mcdt-r2p0` | `0x56490000`, IRQ 182 | ✅ |
| Digital-Codec | `unisoc,audio-codec-dig-agcp` | `0x56750000` | ✅ |
| Audio-DMA | `sprd,qogirl6-dma` | `0x56580000`, IRQ 180 | ✅ |
| Analog-Codec | `unisoc,sc2730-audio-codec` | PMIC `0x1000` (ADI-Bus `spi@64200000`) | ✅ |
| Mailbox | `unisoc,mailbox` | `0x641c0000/0x641d0000/0x641e0000`, IRQ 82–84 | ✅ |
| audio-sipc, audio-mem, Pipes, pcm-platform, fe-dai, routing, audcp-boot/-dvfs | – | – | ✅ gleiche Knoten |
| Soundkarte | `unisoc,vbc-v4-codec-sc2730` | – | ✅ Kennung im Realme-Code |
| AGDSP-Stromdomäne | Xiaomi `sprd,agdsp-pd`, Realme `unisoc,agdsp-access` | – | 🟡 anders; bei Xiaomi `disabled` |
| **Lautsprecher-Verstärker** | `foursemi,fs1588` + `foursemi,frsm-amp` | I2C `0x20220000`, Adresse `0x34` | ❌ **kein Quellcode** (Realme nutzt sia81xx) |

Zusätzlich lädt Android `sprd_audcp_boot.ko` (startet den Audio-DSP) und `snd-soc-sipa.ko`.

### WLAN, Bluetooth, GNSS

Die Knoten stehen nicht im Haupt-Gerätebaum, sondern im **dtbo-Overlay**, wie bei Realme.

| Baustein | Kennung | Adresse | Redmi = Realme |
|---|---|---|---|
| WCN-Kern BT/WLAN | `unisoc,integrate_marlin` | `0x87000000` (2 MB) | ✅, Xiaomi zusätzlich `0x7c00a000` (0x74) ❓ |
| WLAN | `sprd,sc2355-sipc-wifi` | `0x87380000` (2,5 MB) | ✅ |
| GNSS | `unisoc,integrate_gnss` | `0x87600000` | ✅ |
| Bluetooth | `sprd,wcn_internal_chip` (sipc core@3) | Mailbox-Kanal 8 | ✅ |
| sipc GNSS | core@4 | Mailbox-Kanal 9 | ✅ |

Ladereihenfolge in Android: `unisoc_wcn_bsp.ko` → `sprd_wlan_combo.ko` (direkt danach) → später `sprdbt_tty.ko`.

## Stand 09.10. abends

- **Mailbox läuft bereits in Mainline** ✅: Treiber `sprd-mailbox.c` (`sprd,ums9230-mailbox`, R2) und DT-Knoten sind im Codeberg-Stand,
  `CONFIG_SPRD_MBOX=y`. Am Handy: `641c0000.mailbox` gebunden, 3 Interrupts (GIC 114–116 = SPI 82–84) angemeldet, Zähler 0
  (noch kein Partner-Kern aktiv). Hinweis: Mainline nutzt `#mbox-cells = <1>`, Unisoc `<2>` – beim sipc-Port beachten.
- **WCN-Startdaten aus Xiaomis Overlay** (`cpwcn-btwf`): Firmware kommt aus der **Partition `wcnmodem`** (2 MB, `sprd,file-length`),
  nicht aus einer Datei; Einschaltfolge `sprd,ctrl-reg/-mask/-value/-type/-rw-offset/-us-delay` (10 Schritte) ist **identisch mit Realme**;
  Syscons: aon-apb, pmu-apb, wcn-aon-apb (`0x5180c000`), wcn-aon-ahb (`0x51880000`), pub-apb, wcn-btwf-ahb (`0x51130000`);
  `sprd,apcp-sync-addr = 0x7fdc00`, `sprd,wcn-sipc-ver = 1`. Zusätzlich GPIOs `merlion-chip-en` (118), `merlion-rst` (117),
  `xtal-26m-type-sel` (173) und Regler vddwcn, dcxo1v8, vddwifipa ❓ (Bedeutung beim integrierten WCN noch unklar).
- **Firmware gefunden (09.10. abends):** nicht in einer Partition `wcnmodem` (gibt es auf dem Redmi A5 nicht), sondern in
  **`odm_a`: `/odm/firmware/wcnmodem.bin`** (1 273 416 Bytes), `gnssmodem.bin` (195 412 Bytes) und `frsm-spk1.bin`
  (Einstellungen Lautsprecher-Verstärker). **`wcnmodem.bin` ist nicht signiert**: Sie beginnt nicht mit `DHTB`
  (`SEC_IMAGE_MAGIC`), sondern mit einer Cortex-M-Vektortabelle (Stapel `0x001e1788` < 2 MB, Einsprünge ungerade = Thumb).
  Damit entfällt die trusty-Prüfung; ob eine Speicher-Sperre („UNLOCK_DDR") trotzdem greift: ❓ (zeigt der Startversuch).
  Android lädt die Module in `odm/etc/init/wcn.rc` (`on cali-fs`): wcn_bsp → wlan_combo → gnss → sprdbt_tty → fm.
  Audio-DSP-Firmware liegt in der Partition **`l_agdsp_a/b`**.
- **Vorprüfung WCN-Start (09.10., nur gelesen):**
  - Speicher: `0x87000000–0x8747ffff` und `0x87600000–0x877fffff` reserviert (inkl. `btwf-sync@877fdc00`) ✅;
    **Lücke `0x87480000–0x874fffff` ist normaler RAM** (Teil des WLAN-Puffers `0x87380000–0x875fffff`) → vor dem WLAN-Treiber reservieren.
  - Regler (PMIC SC2730, regmap `spi4.0`, Werte aus Realme `sc2730-regulator.c`, Basis 0x1800): VDDWCN PD `0x191c`=1, VOL `0x1920`=0;
    VDDSIM2 (dcxo1v8) PD `0x1994`=1, VOL `0x1998`=0xb4; VDDWIFIPA PD `0x19d0`=1, VOL `0x19d4`=0xd2 → **alle drei aus** (Bit 0 = 1).
    Mainline hat keinen SC2730-Reglertreiber; der Testtreiber muss sie selbst schalten.
  - `0x640203A8` (PMU WCN) = `0x00209006`: Bits 24/25 (auto/force shutdown) schon 0; `0x64000360` (AON) = `0xC0739C07`:
    Bits 21/22 (unshutdown) schon gesetzt; `0x60008018` (PUB, WCN-Adress-Umlenkung) = 0, Android schreibt 0x70 über den
    SET-Spiegel `0x60009018`. Chip-ID `0x640000E0/E4` = qogirl6, `0x640000FC` = 3 (kein „AA"-Sonderfall).
  - Startsignal des WCN-Kerns: nur Speicherwert `0x877FDC00` = `0xF0F0F0FF` (vorher `0x5A5A5A5A` schreiben), kein Interrupt nötig.
    Kalibrierdaten beim ersten Start alle 0, nur Merker `0x877FEB7C` = `0xEFEFFEFE`.
  - Externer Begleitchip „merlion": GPIO 118 (chip-en) = 1, dann GPIO 117 (reset) 0 → 1.
  - **Vorsicht:** Register der WCN-Seite (`0x51…`) und fremde regmap-Listen nicht blind lesen – Lesen aller debugfs-regmaps
    löste einen „synchronous external abort" aus (Prozess beendet, Kernel lief weiter, Neustart nötig).
- **Nächster Versuch:** WCN-Kern ohne sipc starten (Firmware aus `wcnmodem` nach `0x87000000`, Einschaltfolge nach
  `wcn_integrate_boot.c`) und prüfen, ob er sich über die Mailbox meldet (Interrupt-Zähler). Klärt nebenbei die trusty-Frage.
- **Testtreiber geschrieben (09.10. abends):** `kernel/wcn-test/wcn_starttest.c` (Out-of-tree-Modul, baut ohne Warnung gegen
  Linux 7.1-rc1 arm64 mit `redmi_a5_defconfig`; am Handy **noch nicht getestet** ❓), Aufruf über `werkzeuge/redmi-wcntest.sh <Stufe>`.
  Stufen, eine pro Start: 0 nur lesen · 1 Strom · 2 WCN-System an · 3 Firmware + CPU-Start, warten auf `0xF0F0F0FF`.
  Ablauf 1:1 nach Realme `wcn_proc_native_start()` → `wcn_poweron_device()`, von einem zweiten Durchgang gegen die Quelle geprüft
  (Adressen, Werte, Reihenfolge, Sync-Offsets, Magics). Erkenntnisse beim Lesen der Quelle:
  - Die 10-Schritte-Liste `sprd,ctrl-reg` wird auf qogirl6 **nicht** zum Start benutzt (`wcn_cpu_bootup()` nur bei anderen Chips);
    beim Probe läuft nur Schritt 0 (PUB `0x9018` ← 0x70). Der Start ist fest im Code verdrahtet.
  - **Erklärung für den „synchronous external abort“:** WCN-Register (`0x51…`) sind nur zugänglich, solange das WCN-System an
    (PMU `0x538` Bit 28:24 = 0) und wach (PMU `0x860` Bit 31:28 = 6) ist – sonst Bus-Fehler. Der Treiber prüft das vor jedem Zugriff.
  - Regler: `dcxo1v8` (VDDSIM2) steht auf **3,0 V** (0xb4) und muss vor dem Einschalten auf **1,8 V** (0x3c); `vddwcn` von 0,9 V auf
    **1,2 V** (0x14); `vddwifipa` bleibt (3,0 V nur bei Chip „AA“). Der Regler-Schreibschutz (`0x1bd0` ← `0x6e7f`) muss aufgehoben
    werden, weil Mainline keinen SC2730-Reglertreiber hat. Übernimmt der PMIC die Spannung nicht, bleibt der Regler aus.
  - Nicht übernommen: eFuse-Werte für WLAN (`0x877FEB70`, aus nvmem `wcn_efuse_blk0`) – werden nur angezeigt ❓;
    `0x13579BDF`-Rückmeldung nur mit `fertig_melden=1` (auf dem Redmi stand nach Android `0xF0F0F0FF`, Xiaomi macht es wohl nicht).
  - Offen ❓: ob der Mainline-Power-Domain-Treiber (wcn) sich mit dem direkten Einschalten über PMU `0x3a8` verträgt.
- **Stufe 0 am Handy (09.10., 21:23, Kernel g99c0890089ba, Protokoll `wcntest_2026-10-09_2123_stufe0.log`)** ✅:
  - WCN-Speicher ist `no-map` (Abbildung ungecacht wie bei Android) ✅.
  - Regler alle aus: dcxo1v8 3000 mV, vddwcn 900 mV, vddwifipa 3300 mV. GPIO 117/118 = 0, GPIO 173 (xtal-sel) = 1.
  - **PMU meldet das WCN-System schon nach dem Start als an+wach** (`0x538` = 0, `0x860` = `0x60000006`, AON `0x364` = `0x331`:
    BTWF und GNSS wach), obwohl alle Regler aus sind. Die Statusbits zeigen nur die Zustandsmaschine, nicht ob Strom/Takt anliegt –
    passt zum „synchronous external abort“. Wer die Domäne einschaltet (Bootloader oder Mainline-Power-Domain), ist ❓.
    Folge: Der Treiber prüft „schon gelaufen“ jetzt über vddwcn bzw. `init_status`, nicht mehr über die PMU.
  - Speicher `0x87000000` und Sync-Bereich enthalten nur `0xffff0000` (nichts von Android übrig, auch die eFuse-Felder nicht).
  - Mailbox-Zähler unverändert 0. `gpio-sprd` hat kein `get_direction` (WARN in gpiolib) → Treiber liest nur noch den Wert.
- **Stufe 1 (21:27)** ✅: Regler-Schreibschutz aufgehoben, dcxo1v8 3000 → 1800 mV, vddwcn 900 → 1200 mV, vddwifipa an (3300 mV),
  merlion chip-en = rst = 1, PUB `0x60008018` = 0x70. Hinweis: `gpio-sprd` schaltet eine Leitung erst beim Anfordern frei,
  die GPIO-Werte aus Stufe 0 (vor dem Anfordern) sind nicht verlässlich.
- **Stufe 2 (21:31)** ✅: WCN-Register erstmals gelesen, **ohne Absturz** (mit Strom und Takt). WCN-AON-APB `0x098` = `0x04041000`,
  WCN-AON-AHB `0x00c` = `0x3` (CPU im Reset). Die Quelle schreibt bei AON `0x360` `0x6<<21` = Bits **23:22** (nicht 22:21);
  Bit 23 liest sich danach wieder als 0.
- **Stufe 3 (21:46): WCN-KERN LÄUFT** ✅ (Beleg `wcntest_2026-10-09_2146_stufe3.log`):
  - Firmware 1 273 416 Bytes nach `0x87000000` (Stapel `0x001e1788`, Einsprung `0x00003559`), CPU losgelassen (`0x5188000c` = 0x2).
  - `init_status` zählt hoch: `0x5A5A5A5A` → `0xF0F0F0F1` (160 ms) → `…A2` → `…A3` → `…A6` → **`0xF0F0F0FF` nach ~540 ms**.
  - **Damit belegt: unsignierte Firmware startet ohne trusty, keine Speichersperre.**
  - Der Kern schickt **3 Nachrichten auf Mailbox-Kanal 8** (BT-sipc, core@3), IRQ GIC 115 zählt 2 – Mainline verwirft sie
    („message's been dropped at ch[8]“), weil noch niemand zuhört. Das ist der Einstieg für den sipc-Port.
  - Danach schläft das WCN-System ein (PMU `0x860` = 0x6 statt 0x6…, AON `0x364` = 0x1), `cp2_sleep` = `0x504c5344` („DSLP“).
    Der Schutz im Treiber hat Zugriffe auf `0x51…` danach richtig verweigert.
  - `cali_flag` bleibt `0xEFEFFEFE` und `init_status` `0xF0F0F0FF` – derselbe Zustand, der nach Android im Speicher stand.
    Xiaomi meldet also offenbar kein `0x13579BDF` zurück (`fertig_melden` bleibt aus).
  - Kalibrierung: `dfs` = `0x70`/`0x11000000`, `rfi` = 1 vom Kern gesetzt. eFuse-Felder waren `0x0000ffff`/0 (nicht von uns gesetzt) ❓.
- **Lauscher auf Mailbox-Kanal 8 (21:59, Beleg `wcntest_2026-10-09_2159_stufe3.log`)** ✅: `kernel/wcn-test/wcn_lauscher.c` meldet sich
  über einen zur Laufzeit angelegten DT-Knoten (`mboxes = <&mailbox 8>`) als Empfänger an. Der WCN-Kern schickt kurz vor `0xF0F0F0FF`
  drei sipc-**OPEN**-Nachrichten (Flag `0xBEEE`), Ziel ist sipc-Kern 3 (`SIPC_WCN_DST`):

  | smsg-Kanal | Bedeutung (Realme `sprdwcn/sipc/wcn_sipc.c`) | Art |
  |---|---|---|
  | 4 | AT-Befehle, Bluetooth, FM (gemeinsam; bufid AT 5, BT tx 11/rx 10, FM tx 14/rx 13) | sbuf |
  | 5 | Firmware-Log des WCN-Kerns | sbuf |
  | 7 | WLAN-Befehle | sblock |

  Der Kern wartet auf das OPEN der Linux-Seite und danach auf die Puffer im gemeinsamen Speicher (sbuf/sblock).
- **WCN-Speicher übersteht einen Warmstart** (belegt 21:56): Nach `reboot` standen Firmware, `0xF0F0F0FF` und „DSLP“ noch im RAM.
  Die Werte „nach Android“ in der Vorprüfung waren also Reste. Der Treiber prüft „schon gelaufen“ deshalb nur noch über vddwcn.
- **Lauschangriff auf den Speicher:** Der Lauscher gibt den reservierten WCN-Speicher nur lesend aus
  (`/sys/devices/platform/wcn-lauscher/wcn_ram_btwf` = `0x87000000–0x8747ffff`, `wcn_ram_hoch` = `0x87600000–0x877fffff`; die Lücke
  mit normalem Linux-RAM bleibt draußen). Ziel: Log-Text und Puffer des Kerns finden, bevor sipc steht ❓.
- **Speicherauszug nach dem Start (22:14):** Firmware `0x87000000–0x87136E48`, Arbeitsdaten bis ~`0x87204000`
  (Stapelanfang `0x871E1788`). Ab `0x87204000` und der ganze obere Bereich unberührt (Einschaltmuster `0xffff0000`).
  Kein eigener Log-Ring im RAM; die `[T:…]`-Texte sind Reste von Formatierpuffern. Firmware-Texte bestätigen sipc auf
  der Gegenseite (`sipc.c`, `smsg.c`, `sbuf.c`, `sblock.c`, `VLOG_main: cannot creat sbufl`).
- **sipc-Speicher des WCN (Realme `ums9230-wcn.dtsi`, core@3):** `mboxes = <&mailbox 8 0>`,
  `sprd,smem-info = <0x87240000 0x00240000 0x140000>` – genau im unberührten Bereich. Für Xiaomi noch gegen das dtbo prüfen ❓.
- **sipc-lite, erster Handshake (22:27, Beleg `wcntest_2026-10-09_2227_stufe3.log`)** ✅: Lauscher mit `antworten=1`.
  WCN OPEN 5 → **wir OPEN 5** → WCN CMD SBUF_INIT → **wir DONE (`0x240000`)** → WCN EVENT WRPTR. Die Mailbox
  quittiert unser Senden (Inbox-IRQ GIC 114 zählt 2). Der Kern schreibt in unseren Ring (2880 Bytes in den ersten ~50 ms).
- **Log-Format:** binäre Rahmen `7E7E7E7E | u16 Länge | u16 ? | 5A5A | u16 Typ | u32 Nr | u16 Nutzlänge | u16 ?`.
  Erster Typ `0x0281`: Registerspur der RF-Kalibrierung (Paare `Adresse<<16 | Wert`, z. B. `d19a`/`d0a2` in 4er-Schritten).
  Der Kern meldet WRPTR nur, wenn der Ring vorher leer war → Lauscher sieht zusätzlich alle 500 ms nach.
- **Nächster Schritt:** sipc (smsg/sbuf) aus `drivers/soc/sprd/modem/sipc/` portieren und an Mailbox-Kanal 8 hängen
  (Mainline `#mbox-cells = <1>`), dann die Nachrichten des WCN-Kerns lesen. Danach `sprdbt_tty` bzw. WLAN `sc2355`.

## Offene Hürden

1. **sipc + Mailbox fehlen in Mainline.** Ton, WLAN, Bluetooth, GNSS und Modem bauen alle darauf auf. Das ist der erste Port.
2. **trusty (für WLAN vermutlich erledigt, s. o.):** `unisoc_wcn_bsp` hängt an `trusty-ipc`. Laut Realme-Code wird die WCN-Firmware nur dann von TrustZone
   geprüft, wenn sie einen Signatur-Kopf (`SEC_IMAGE_MAGIC`) hat; sonst wird sie direkt geladen. Ob Xiaomis Firmware
   signiert ist und ob der WCN-Kern ohne Freigabe startet: ❓ (nur durch Versuch zu klären). Mainline hat keinen trusty-Treiber.
3. **Firmware:** Audio-DSP, VBC und WCN brauchen Firmware aus den Android-Partitionen (nicht frei, bleibt auf dem Handy).
4. **Lautsprecher-Verstärker FS1588:** kein Quellcode gefunden. Ohne ihn nur Hörmuschel und Kopfhörer.
5. **Kernel-Version:** 5.4 → 7.1. Der Code ist Vorlage, nicht einbaufertig.

## Weitere Spuren

- postmarketOS-Paket [linux-postmarketos-unisoc-ums9230](https://pkgs.postmarketos.org/package/master/postmarketos/aarch64/linux-postmarketos-unisoc-ums9230)
- [akku1139/ums9230-mainline-linux](https://zff.dev/akku1139/ums9230-mainline-linux)
