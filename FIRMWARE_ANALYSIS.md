# Redmi A5 (serenity) — Komplette Firmware-Architektur

## Übersicht

| Eigenschaft | Wert |
|-------------|------|
| Gerät | Xiaomi Redmi A5 |
| Codename | serenity |
| SoC | Unisoc UMS9230E (Marketing: T7250 = T615) |
| Plattform | sharkl5pro / qogirl6 |
| CPU | 2× Cortex-A75 + 6× Cortex-A55 (big.LITTLE) |
| GPU | Mali "natt" |
| RAM | 3072 MB (464 MB reserviert für Modem-Firmware) |
| Storage | eMMC, 59.5 GB (0xe90000000 Bytes), 81 Partitionen |
| Kernel | 5.15.178-android13-8-00006-g0c6055fd2d8b |
| Android | 13, GKI (Generic Kernel Image), Header v4 |
| Slots | A/B (VAB), aktiv: Slot B |
| Secure Boot | AVB 2.0, deaktivierbar über vbmeta-Flags |
| BROM-Zugang | VOL_UP + VOL_DOWN + USB |
| BROM-Exploit | CVE-2022-38694, exec_addr 0x65015f08 |
| BROM-Tool | spd_dump (Paket: linux_ums9230e_Tecno_KL4) |
| SKU | serenity_GLA |

---

## 1. Boot-Kette

```
BROM (Chip-ROM, unveränderlich)
  │
  ├─ Normal: lädt SPL aus splloader-Partition
  └─ Download: VOL_UP+VOL_DOWN+USB → DL-Modus (spd_dump)
        │
        ├─ FDL1 (fdl1-dl.bin → 0x65000800)
        │    CVE-2022-38694: custom_exec_no_verify → umgeht Signaturprüfung
        │
        └─ FDL2 (fdl2-dl.bin → 0x9efffe00)
             Voller eMMC-Zugriff: lesen, schreiben, Partitionsliste

SPL (Spreadtrum Boot Block v1.1)
  │
  └─ Lädt SML (Secure Monitor Layer)
       │
       └─ Lädt TrustOS (TEE) + U-Boot/LK (Little Kernel)

LK (Little Kernel) — der eigentliche Bootloader
  │
  ├─ Initialisiert: eMMC, PMIC (SC2730), Charger (BQ2560x), Display, USB
  ├─ A/B Slot-Auswahl: priority + tries_remaining + successful_boot
  ├─ Verified Boot: prüft boot, vendor_boot, init_boot, dtbo gegen vbmeta
  ├─ Kernel-Cmdline: zusammengebaut aus DTS + bootconfig + LK-fixups
  ├─ USB Gadget Serial: VID=1782 PID=4d00, bcdDevice=24.16 (LK-Phase)
  │
  └─ Lädt Kernel + Ramdisks:
       ├─ boot_b     → Kernel (Image) @ PC:0x80080000
       ├─ vendor_boot_b → Vendor-Ramdisk (Module, DTB) @ 0xa0000000
       ├─ init_boot_b   → Generic-Ramdisk (/init) @ 0xa20b60d4
       ├─ dtb_b         → Device Tree (fix) @ 0x9df00000
       └─ dtbo_b        → Device Tree Overlays
```

### 1.1 LK Boot-Ablauf (aus uboot_log)

```
[00000703] pmic_misc_init
[00000795] eMMC init (HS200 200MHz, 80ms)
[00000820] CHG init OK (BQ2560x, Chip ID: 1)
[00000820] ANDROID: Slot B priority=15, successful_boot=1 → Booting slot_b
[00000888] vddusb33 = 3300mV
[00000992] sysdump/minidump checks
[00000995] Boot mode: NORMAL_MODE
[00086592] verify boot check init_boot (secboot!)
[00086655] init_boot verify_ret: 0x0 (OK bei Original, CRASH bei modifiziert)
[00087489] Ramdisk-Layout:
           vendor_boot_b ramdisk: 34,300,116 bytes
           init_boot_b ramdisk:   2,793,420 bytes
[00088530] Kernel cmdline zusammengebaut
[00088666] usb_driver_exit (USB wird für Kernel freigegeben)
[00088714] [start_linux] at PC:0x80080000, dt_addr:0x9df00000
           LK Zeit: ~88 Sekunden
```

### 1.2 Verified Boot — Kritische Erkenntnis

LK prüft `init_boot_b` mit Secure Boot (AVB 2.0). Ein modifiziertes Image verursacht:

```
unhandled synchronous exception
ESR 0x5e000000: ec 0x17, il 0x2000000, iss 0x0, EL3
panic (caller 0x9de8c9f2): die
```

Lösung: Alle vbmeta-Partitionen für Slot B mit Flags=0x03 (disable verification + disable hashtree) überschreiben:

- vbmeta_b
- vbmeta_system_b
- vbmeta_vendor_b
- vbmeta_system_ext_b
- vbmeta_product_b
- vbmeta_odm_b
- avbmeta_rs_b

vbmeta-Disable-Image: 4096 Bytes, `AVB0` Magic, Flags=3 @ Offset 120.

---

## 2. Partitionslayout

| Nr | Name | Größe | Beschreibung |
|----|------|-------|--------------|
| 0 | splloader | 256 KB | First-stage bootloader |
| 1 | prodnv | 64 MB | Produktions-NV-Daten |
| 2 | miscdata | 1 MB | Verschiedene Boot-Flags |
| 3 | countrycode | 2 MB | Ländercode |
| 4 | misc | 1 MB | Android Boot Control Block (32 Bytes AB-Metadaten @ Offset 2048) |
| 5-6 | trustos_a/b | 6 MB | TrustZone OS |
| 7-8 | sml_a/b | 1 MB | Secure Monitor Layer |
| 9-10 | uboot_a/b | 8 MB | LK (Little Kernel) Bootloader |
| 11 | uboot_log | 16 MB | **Boot-Log! Per BROM lesbar für Diagnostik** |
| 12-13 | logo_a/b | 8 MB | Boot-Logo |
| 14 | fbootlogo | 8 MB | Fastboot-Logo |
| 15-18 | l_fixnv1/2_a/b | 2 MB | Modem NV-Daten (fest) |
| 19-20 | l_runtimenv1/2 | 2 MB | Modem NV-Daten (Runtime) |
| 21 | persist | 2 MB | Persistente Einstellungen |
| 22-23 | l_modem_a/b | 25 MB | LTE/4G Modem-Firmware |
| 24-25 | l_deltanv_a/b | 1 MB | Modem Delta-NV |
| 26-27 | l_gdsp_a/b | 10 MB | GNSS DSP Firmware |
| 28-29 | l_ldsp_a/b | 20 MB | LTE DSP Firmware |
| 30-31 | l_agdsp_a/b | 6 MB | Audio DSP Firmware |
| 32-33 | pm_sys_a/b | 1 MB | Power Management |
| 34-35 | teecfg_a/b | 1 MB | TEE Konfiguration |
| 36-37 | hypervsior_a/b | 10 MB | Hypervisor |
| 38-39 | **boot_a/b** | **64 MB** | **Kernel (GKI Image)** |
| 40-41 | **vendor_boot_a/b** | **100 MB** | **Vendor-Ramdisk (DTB, 168 Module)** |
| 42-43 | **init_boot_a/b** | **8 MB** | **Generic-Ramdisk (/init)** |
| 44-45 | dtb_a/b | 8 MB | Device Tree Blob |
| 46-47 | dtbo_a/b | 8 MB | Device Tree Overlays |
| 48 | **super** | **5120 MB** | **Dynamic Partitions (system, vendor, product, odm)** |
| 49 | cache | 64 MB | Android Update-Cache (ext4) |
| 50 | blackbox | 500 MB | Crash/Dump-Daten |
| 51-52 | vbmeta_a/b | 2 MB | Haupt-Verified-Boot-Metadaten |
| 53 | metadata | 64 MB | FDE/Fscrypt Metadaten |
| 54 | sysdumpdb | 10 MB | System-Dump-Datenbank |
| 55-66 | vbmeta_*_a/b | 2 MB | Sub-vbmeta (system, vendor, system_ext, product, odm, rs) |
| 67-72 | common_rs1-3_a/b | 8-32 MB | Common Resource |
| 73 | reserve1 | 8 MB | Reserviert |
| 74 | reserve2 | 16 MB | Reserviert |
| 75 | calinv | 2 MB | Kalibrierung |
| 76 | gsort | 16 MB | ? |
| 77 | mem | 4 MB | Memory-Info |
| 78 | ffu | 8 MB | Field Firmware Update |
| 79 | cust | 2048 MB | Xiaomi Customization |
| 80 | rescue | 128 MB | Recovery/Rescue |
| 81 | **userdata** | **50841 MB** | **Benutzerdaten (~50 GB)** |

---

## 3. GKI Boot-Image-Architektur (Header v4)

```
boot_b (64 MB):
  Header v4 (4096 Bytes)
  └─ Kernel Image (GKI, ~47 MB)
     Keine Ramdisk, kein DTB — die kommen aus vendor_boot und init_boot

vendor_boot_b (100 MB):
  Header v4
  ├─ Vendor-Ramdisk (~34 MB, LZ4-komprimiert → ~67 MB entpackt)
  │   ├─ /lib/modules/ → 168 Kernel-Module (.ko)
  │   ├─ /system/bin/busybox (aarch64, statisch gelinkt)
  │   ├─ /system/bin/toybox
  │   ├─ /system/etc/init/hw/init.rc
  │   ├─ /init.recovery.common.rc → USB-Gadget-Konfiguration!
  │   └─ /ueventd.serenity.rc
  ├─ DTB (Device Tree)
  ├─ Vendor-Ramdisk-Table (108 Bytes)
  └─ Bootconfig (53 Bytes)

init_boot_b (8 MB):
  Header v4 (Page Size: 4096)
  └─ Generic-Ramdisk (~2.8 MB LZ4 → ~5.3 MB entpackt)
      ├─ /init → Android Init (3.7 MB, aarch64, statisch)
      ├─ /system/bin/snapuserd_ramdisk
      ├─ /dev/, /proc/, /sys/, /mnt/, /metadata/
      ├─ /debug_ramdisk/
      └─ /second_stage_resources/
```

### 3.1 Ramdisk-Overlay-Reihenfolge

Kernel überlagert die Ramdisks. init_boot liegt OBEN:

```
Unterste Schicht: vendor_boot ramdisk (Module, .rc-Dateien, busybox)
Oberste Schicht:  init_boot ramdisk   (/init überschreibt alles darunter)
```

Konsequenz: `/init` muss in init_boot_b ersetzt werden, nicht in vendor_boot_b!

### 3.2 Image-Packing

init_boot_b neu bauen:

```python
import struct
d = open('init_boot_b.bin','rb').read()
hdr = bytearray(d[:4096])
nr = open('init_ramdisk_mod.lz4','rb').read()
struct.pack_into('<I', hdr, 12, len(nr))  # ramdisk_size @ offset 12
out = bytes(hdr) + nr + padding
```

vendor_boot_b neu bauen:

```python
# page_size @ offset 12, ramdisk_size @ offset 24
ps = struct.unpack_from('<I', data, 12)[0]
rs = struct.unpack_from('<I', data, 24)[0]
# Neue Ramdisk einfügen, Größe im Header updaten
```

---

## 4. Kernel-Cmdline (komplett)

```
console=ttyS1,921600n8
loop.max_part=7
loglevel=1
log_buf_len=2M
kpti=0
firmware_class.path=/odm/firmware,/vendor/firmware
init=/init
root=/dev/ram0 rw
printk.devkmsg=on
ftrace_dump_on_oops
swiotlb=1
dummy_hcd.num=0
rcupdate.rcu_expedited=1
rcu_nocbs=0-7
kvm-arm.mode=none
lcd_id=ID4160
lcd_name=Panel_C3Z_36_10_0d_vid
lcd_base=9caa8000
lcd_size=1640x720
logo_bpix=32
sprdboot.mode=normal
sprdboot.usbmux=0x0
modem=shutdown
sku.name=serenity_GLA
```

Wichtig: LK ändert `console=ttyS1,921600n8` zu `console=null` im NORMAL_MODE!
UART-Output ist damit im normalen Boot deaktiviert.

---

## 5. USB-Stack

### 5.1 Hardware

```
USB-Controller: MUSB @ 0x64900000 (sprd,qogirl6-musb)
USB-PHY:        sprd,qogirl6 (phy-sprd-qogirl6)
Mode:           OTG (dr-mode = "otg")
Interrupts:     IRQ 69 ("mc")
```

### 5.2 USB am Host (drei Phasen)

| Phase | bcdDevice | Strings | Dauer | Bedeutung |
|-------|-----------|---------|-------|-----------|
| BROM | 2.02 | keine | kurz | Download-Modus |
| LK/Bootloader | 24.16 | "Sprd Gadget Serial" / "spreadtrum with musb-hdrc" | ~2s | Bootloader USB |
| Kernel (Android) | 2.02 | keine | 0.5s-33s | Kernel läuft, Init konfiguriert USB |

### 5.3 USB VID/PID

| VID | PID | Konfiguration |
|-----|-----|---------------|
| 0x1782 | 0x4d00 | vser (Debug Serial) — Standard |
| 0x1782 | 0x5d06 | adb + vser + gser |

### 5.4 USB-Treiber-Stack (Kernel)

Built-in (kein Modul nötig):
- MUSB Core (musb_hdrc) — **ACHTUNG: modules.load listet es NICHT → built-in**
- USB PHY

Module (aus vendor_boot Ramdisk):

```
musb_sprd.ko          → Sprd MUSB Glue-Layer
                         depends: musb_hdrc, sc27xx_typec, phy-sprd-commonphy,
                                  sprd_usbm, sprd_usbpinmux
                         NICHT in modules.load → wird von ueventd per Hardware-Match geladen
                         alias: sprd,qogirl6-musb

sprd_usbm.ko          → USB Manager (IN modules.load)
usb_f_vser.ko          → Virtual Serial Funktion (/dev/vser) (IN modules.load)
sprd_u_serial.ko       → Serial Core (IN modules.load)
sprd_usb_f_serial.ko   → Gadget Serial (/dev/ttyGS0) (IN modules.load)
                         depends: sprd_u_serial
sprd_u_ether.ko        → Ethernet Core (IN modules.load)
sprd_usb_f_rndis.ko    → RNDIS Funktion (IN modules.load)
```

### 5.5 USB Gadget Configfs (aus Android .rc)

```bash
mount configfs none /config

# Gadget erstellen
mkdir /config/usb_gadget/g1
write idVendor  0x1782
write idProduct 0x4d00

# Strings
mkdir /config/usb_gadget/g1/strings/0x409
write manufacturer "Unisoc"
write product      "Unisoc Phone"

# Config
mkdir /config/usb_gadget/g1/configs/b.1       # NICHT c.1!
write MaxPower 120

# Funktionen erstellen
mkdir functions/vser.gs7          → /dev/vser
mkdir functions/sprdgser.gs0-gs7  → /dev/ttyGS0-7
mkdir functions/ffs.adb           → ADB
mkdir functions/mtp.gs0           → MTP
mkdir functions/rndis.gs4         → USB-Netzwerk

# vser-only Modus (PID 0x4d00):
symlink functions/vser.gs7 → configs/b.1/f1
write UDC <controller>     # z.B. "musb-hdrc.0.auto"

# adb+vser+gser Modus (PID 0x5d06):
symlink functions/ffs.adb      → configs/b.1/f1
symlink functions/vser.gs7     → configs/b.1/f2
symlink functions/sprdgser.gs0 → configs/b.1/f3
write UDC <controller>
```

### 5.6 Host-Seite (Linux Mint)

Der Sprd Gadget Serial hat keine Standard-USB-Klasse. Der Host braucht:

```bash
sudo modprobe usbserial vendor=0x1782 product=0x4d00
```

Dann erscheint `/dev/ttyUSB0`. Ohne diesen Befehl: USB-Device wird erkannt, aber kein serielles Interface erstellt.

---

## 6. Kernel-Module — Vollständige Ladereihenfolge

Aus `vendor_rd/lib/modules/modules.load` (Android-Reihenfolge):

```
# Phase 1: Kernel-Infrastruktur
printk_cpuid.ko
native_hang_monitor.ko
timer-sprd.ko
regmap-hook.ko
sprd_wdt_fiq.ko
sprd_sip_svc.ko
sprd_systimer.ko
sprd_time_sync.ko
sprd_time_sync_cp.ko

# Phase 2: Clocks + Power
clk-sprd.ko
ums9230-clk.ko
sysdump.ko
iolimit.ko
unisoc-sched.ko

# Phase 3: PMIC + RTC
spi-sprd-adi.ko
sprd-pmic-spi.ko
rtc-sc27xx.ko
sc2730-regulator.ko

# Phase 4: System
sprd_soc_id.ko
sprd_hwspinlock.ko
nvmem-sc27xx-efuse.ko
nvmem_sprd_cache_efuse.ko
nvmem_sprd_efuse.ko

# Phase 5: eMMC Storage
rpmb.ko
ufs_sprd.ko
i2c-sprd.ko
i2c-sprd-hw-v2.ko

# Phase 6: Sicherheit + Thermal
sprd_7sreset.ko
trusty-tui.ko
sprd_thermal.ko
sprd_soc_thm.ko
sprd_thermal_ctl.ko
sprd-cpufreq-v2.ko

# Phase 7: GPIO + MMC
gpio-eic-sprd.ko
gpio-sprd.ko
gpio-pmic-eic-sprd.ko
sdhci-sprd.ko         ← eMMC-Controller!
mmc_hsq.ko            ← eMMC Command Queue
mmc_swcq.ko           ← eMMC Software CQ

# Phase 8: System-Services
shutdown_detect.ko
sprd_pmic_syscon.ko
sensorhub.ko

# Phase 9: USB
sprd_usbm.ko          ← USB Manager
sprd_power_manager.ko
sprd_pdbg.ko

# Phase 10: IPC (Inter-Processor Communication)
unisoc-mailbox.ko
sprd-sipc-virt-bus.ko
sipc-core.ko           ← SIPC Core (Dependency für vser + Modem)
spipe.ko
spool.ko
sipx.ko
seth.ko                ← Modem-Netzwerk-Interface

# Phase 11: Modem + Debug
sprd_modem_loader.ko
sprd_cp_dump.ko
sprd_iq.ko
slog_bridge.ko
sbuf_bridge.ko
sblock_bridge.ko

# Phase 12: USB Gadget Functions
usb_f_vser.ko          ← Virtual Serial (/dev/vser)
sprd_u_serial.ko       ← Serial Core
sprd_usb_f_serial.ko   ← Gadget Serial (/dev/ttyGS0)
sprd_u_ether.ko        ← Ethernet Core
sprd_usb_f_rndis.ko    ← RNDIS

# Phase 13: Misc
sprd_cache_print.ko
sprd_trng.ko           ← Hardware-RNG
```

### 6.1 Module die NICHT in modules.load stehen (per ueventd/hotplug geladen)

```
musb_sprd.ko                → USB MUSB Glue (per Device Tree Match)
phy-sprd-commonphy.ko       → USB PHY
phy-sprd-qogirl6.ko         → USB PHY (qogirl6-spezifisch)
extcon-usb-gpio.ko          → USB-Kabel-Erkennung
sprd_usbpinmux.ko           → USB Pin-Muxing
sc27xx_typec.ko              → USB Type-C
sprd-drm.ko                 → Display (DRM)
sprd-gsp.ko                 → Graphics Signal Processor
focaltech_ft8057_spi_ts.ko  → Touchscreen
sprd_camera.ko              → Kamera-Subsystem
sprd-charger-manager.ko     → Ladegerät-Manager
und ~30 weitere...
```

### 6.2 Modul-Abhängigkeiten (aus modules.dep)

```
usb_f_vser.ko:              (keine Kernel-Modul-Deps, aber braucht configfs)
sprd_usb_f_serial.ko:       sprd_u_serial.ko
slog_bridge.ko:             usb_f_vser.ko, sipc-core.ko, ...
musb_sprd.ko:               musb_hdrc.ko, sc27xx_typec.ko,
                             phy-sprd-commonphy.ko, sprd_usbm.ko,
                             sprd_usbpinmux.ko
sipc-core.ko:               sprd_pdbg.ko, sprd_systimer.ko,
                             sprd_sip_svc.ko, unisoc-mailbox.ko,
                             sprd_power_manager.ko, sysdump.ko
```

---

## 7. Device Tree (serenity.dts) — USB-relevanter Auszug

```dts
usb@64900000 {
    compatible = "sprd,qogirl6-musb";
    reg = <0x00 0x64900000 0x00 0x2000>;
    interrupts = <0x00 0x45 0x04>;      // IRQ 69
    interrupt-names = "mc";
    clocks = <0x37 0x18 0x5f 0x29 0x2a 0x03 0x91 0x08>;
    clock-names = "core_clk", "hclk_source_sel",
                  "hclk_default_source", "hclk_suspend_source";
    usb-phy = <0x92>;
    phy-names = "usb";
    dr-mode = "otg";
    multipoint = "true";
    core_select = <0x01 0x06>;
    wakeup-source;
};
```

---

## 8. Serielle Konsole

| Interface | Baudrate | Ort | Status |
|-----------|----------|-----|--------|
| ttyS1 (UART) | 921600 | Testpunkte auf PCB | LK setzt `console=null` im Normal-Mode! |
| ttyGS0 (USB Gadget) | n/a | USB-C | Braucht sprdgser.gs0 configfs-Funktion |
| /dev/vser (USB Gadget) | n/a | USB-C | Braucht vser.gs7 configfs-Funktion |

UART wird von LK im Normal-Mode **deaktiviert** (`console=ttyS1,921600n8` → `console=null`).
Um UART-Output zu bekommen, müsste die Kernel-Cmdline gepatcht werden.

---

## 9. Android Init-System

### 9.1 Init-Ablauf

```
Kernel startet /init (aus init_boot_b)
  │
  ├─ First Stage Init
  │   ├─ Mountet /dev, /proc, /sys
  │   ├─ Startet ueventd → erstellt /dev/-Nodes per Kernel-Events
  │   ├─ Mountet /system, /vendor, /product aus super-Partition
  │   └─ SELinux-Policy laden
  │
  └─ Second Stage Init
      ├─ Liest .rc-Dateien
      ├─ Trigger: on early-init → on init → on fs → on boot
      ├─ Lädt Module (modules.load Reihenfolge)
      ├─ Configfs USB-Gadget aufsetzen
      ├─ Services starten (adbd, logd, servicemanager, ...)
      └─ Property-System (sys.usb.config, sys.usb.controller, ...)
```

### 9.2 ueventd

Android erstellt Device-Nodes NICHT mit devtmpfs allein.
`ueventd` lauscht auf Kernel-Events und erstellt `/dev/`-Nodes mit korrekten
Permissions und Ownership.

Ohne ueventd fehlen:
- `/dev/mmcblk0*` (eMMC Block-Devices)
- `/dev/vser`, `/dev/ttyGS*` (USB Serial)
- `/dev/fb0`, `/dev/graphics/*` (Display)
- viele weitere

Ersatz in unserem Init: explizites `mknod()` oder devtmpfs (erstellt nur Basis-Nodes).

### 9.3 Relevante .rc-Dateien

```
vendor_rd/init.recovery.common.rc  → USB-Gadget-Setup, Module
vendor_rd/system/etc/init/hw/init.rc → Haupt-Init
vendor_rd/ueventd.serenity.rc       → Device-spezifische ueventd-Regeln
```

---

## 10. SoC-Peripherie (aus Device Tree)

### 10.1 Display

- DPU @ 0x31000000 (sprd,qogirl6-dpu)
- DSI Host @ 0x31100000
- Panel: MIPI-DSI (Panel_C3Z_36_10_0d_vid), 1640x720
- Backlight: PWM
- Kernel-Module: sprd-drm.ko, sprd-gsp.ko (NICHT in modules.load)

### 10.2 Touchscreen

- Focaltech FT8057 (SPI) oder Novatek NT36528A (SPI)
- Framework: hq_touch.ko

### 10.3 Audio

- Digital-Codec: AGCP @ 0x56750000
- Analog-Codec: SC2730
- Speaker-Amp: Foursemi FS1588
- Audio-DSP: dediziert (AGCP)

### 10.4 Kamera

- ISP @ 0x3a000000
- 3x QVGA Sub-Kameras: OV SP0821, BYD BF30A2, SC080CS

### 10.5 Mobilfunk

- Modem 0 (WTLCP): LTE/4G
- Modem 1 (PUBCP): 2G/3G Sprache/SMS
- Kommunikation: SIPC über sipc-core.ko + seth.ko

### 10.6 WiFi/BT/GNSS/FM

- WCN-Subsystem (separater Co-Prozessor)
- Firmware unter /odm/firmware, /vendor/firmware
- Keine .ko-Module — Firmware per SIPC geladen

### 10.7 Akku/Laden

- PMIC: SC2730
- Charger-ICs: SGM41513, SC89601, UPM6922 (drei Optionen!)
- Fuel Gauge: SC27xx FGU
- Batterie-Auth: DS28E30 (1-Wire)

---

## 11. Referenzprojekte

| Projekt | SoC | Ansatz | Link |
|---------|-----|--------|------|
| mu300-linux | Unisoc T760 (UMS9620) | USB-ACM+ECM configfs, Ubuntu+systemd, 86 Module | github.com/dikeckaan/mu300-linux |
| pixel8-linux | Google Tensor G3 | configfs ACM, mknod ttyGS0, multi-pass Module | github.com/Lassulus/pixel8-linux |
| e5-linux | Unisoc UMS9621 | Gleicher Ansatz wie mu300-linux | github.com/Enceka/e5-linux |
| rg-rotate-linux | Unisoc UMS512/T618 | Mainline 7.1 auf sharkl5pro | (Referenz für Mainline) |

---

## 12. Bisherige Erkenntnisse (Linux-Port)

### Was funktioniert

- BROM-Zugang: jederzeit über VOL_UP+VOL_DOWN+USB
- CVE-2022-38694 Exploit: exec_addr 0x65015f08
- Bootloader unlock: beide Handys
- Partitionszugriff: alle 81 Partitionen per BROM
- Kernel bootet: bestätigt durch USB-Gadget am Host + uboot_log
- Verified Boot disabled: vbmeta-Flags=0x03 für alle Slot-B-Partitionen
- Custom init_boot_b flashbar: nach vbmeta-Disable kein LK-Panic mehr
- aarch64-static busybox: vorhanden und verifiziert
- Init kompiliert und läuft: Watchdog wird gefüttert (7-33s USB-Verbindungen)

### Was NICHT funktioniert (Stock-Kernel-Ansatz)

- USB-Serial zum Host: Device erscheint am Host, aber Host-Treiber (`usbserial`) bindet nicht automatisch → `modprobe usbserial vendor=0x1782 product=0x4d00` nötig
- eMMC-Zugriff aus Init: ohne sdhci-sprd.ko (und Abhängigkeiten) keine Block-Devices
- Display-Output: ohne sprd-drm.ko kein Framebuffer
- Cache-Partition beschreiben: ext4-Dateisystem blockiert Raw-Writes
- Kernel-Log lesen: `console=null` (von LK gesetzt) unterdrückt UART-Output

### Gelöst (Mainline-Kernel)

- **Mainline Linux 6.x** erfolgreich gebootet (basierend auf ums9230-mainline Fork)
- **Alle 8 CPUs aktiv** (2x A75 + 6x A55)
- **eMMC funktioniert** — Partitionszugriff vollständig
- **Display funktioniert** — simple-framebuffer @ 0x9e000000, 720x1640
- **Ubuntu 24.04** rootfs auf cust-Partition, bootet bis Login-Prompt
- **USB-Konsole** über CDC-ACM (Standard-Treiber, kein Spezial-Modul nötig)

---

## 13. Offene Fragen (Stock-Kernel)

1. Welche Module crashen den USB-Stack beim Laden?
2. Kann die Kernel-Cmdline gepatcht werden um `console=ttyS1,921600n8` zu behalten?
3. Gibt es einen PM-Co-Prozessor-Watchdog (wie beim MU300, ~290s)?
4. Können wir /dev/vser oder /dev/ttyGS0 per mknod erstellen ohne ueventd?
