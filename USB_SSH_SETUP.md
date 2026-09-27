# USB-Gadget & SSH — Netzwerkzugang zum Redmi A5 über USB

Dieses Dokument beschreibt, wie das Xiaomi Redmi A5 (serenity) unter dem eigenen Mainline-Kernel als USB-Netzwerkgerät konfiguriert wird, sodass ein PC per SSH darauf zugreifen kann — ganz ohne WLAN, ohne Android, ohne proprietäre Treiber.

---

## Übersicht

| Eigenschaft | Wert |
|-------------|------|
| Gerät | Xiaomi Redmi A5 (serenity), SoC UMS9230E / T7250 |
| Kernel | Mainline (ums9230-mainline-Fork) |
| Rootfs | Ubuntu 24.04 (arm64, debootstrap) auf `cust`-Partition |
| USB-Controller | MUSB @ 0x64900000 (`sprd,qogirl6-musb`) |
| USB-Modus | OTG (dr-mode = "otg") |
| UDC-Name | `musb-hdrc.1.auto` |
| USB-Gadget | Composite: NCM (Netzwerk) + ACM (Serial-Konsole) |
| VID/PID | 0x1d6b / 0x0104 (Linux Foundation / Multifunction Composite Gadget) |
| Host-Erkennung | Automatisch — `/dev/ttyACM0` + `usb0` Netzwerk-Interface |
| SSH | OpenSSH-Server über USB-Netzwerk (NCM) |

---

## 1. Hintergrund — Warum nicht Android-USB?

Der Stock-Android-Kernel benutzt ein proprietäres Unisoc-USB-Protokoll:

| Eigenschaft | Android (Stock) | Mainline (unser Kernel) |
|-------------|----------------|------------------------|
| Serial-Funktion | `vser.gs7` (Sprd Virtual Serial) | `acm.gs0` (Standard CDC-ACM) |
| VID/PID | 0x1782 / 0x4d00 | 0x1d6b / 0x0104 |
| Host-Treiber | `modprobe usbserial vendor=0x1782 product=0x4d00` | Automatisch (CDC-Klasse) |
| Netzwerk | `rndis.gs4` (proprietär) | `ncm.usb0` (Standard CDC-NCM) |
| Module nötig | `usb_f_vser.ko`, `sipc-core.ko`, `sprd_usbm.ko`, ... | Keine — alles built-in |

**Die Umstellung auf Standard-CDC war einer der entscheidenden Durchbrüche.** Der Host-PC erkennt CDC-ACM und CDC-NCM automatisch, ohne spezielle Treiber oder `modprobe`-Befehle.

---

## 2. Kernel-Konfiguration (USB-relevante Optionen)

Alle USB-Gadget-Funktionen sind im Mainline-Kernel **built-in** (=y), keine als Modul (=m):

```
# USB-Gadget-Kern
CONFIG_USB_GADGET=y
CONFIG_USB_CONFIGFS=y
CONFIG_USB_LIBCOMPOSITE=y

# CDC-ACM (Serielle Konsole)
CONFIG_USB_F_ACM=y
CONFIG_USB_U_SERIAL=y

# CDC-NCM (USB-Netzwerk)
CONFIG_USB_F_NCM=y

# USB-Hardware (MUSB Controller, Sprd-spezifisch)
# Ebenfalls built-in — NICHT als Modul laden!
# modules.load listet musb_hdrc NICHT → built-in im Kernel
```

**Wichtig:** Der Stock-Android-Kernel hat diese ebenfalls built-in. Der Fehler in früheren Versuchen war, USB-Hardware-Module (`musb_hdrc.ko`, `musb_sprd.ko`) per `finit_module` nachzuladen — das crashte den bereits laufenden built-in USB-Stack. Die Module existieren nur für Sonderfälle (Recovery-Ramdisk), im normalen Betrieb sind sie überflüssig und schädlich.

---

## 3. USB-Gadget Architektur

Das USB-Gadget ist ein **Composite Device** mit zwei Funktionen:

```
USB-Kabel (Typ-C am Redmi ← → USB-A am PC)
    │
    └─ USB Composite Gadget (VID 0x1d6b, PID 0x0104)
         │
         ├─ Funktion 1: CDC-NCM (ncm.usb0)
         │    → Netzwerk-Interface usb0 am Redmi
         │    → Netzwerk-Interface usb0 am PC
         │    → SSH, HTTP, alles über IP
         │
         └─ Funktion 2: CDC-ACM (acm.gs0)
              → /dev/ttyGS0 am Redmi
              → /dev/ttyACM0 am PC
              → Serielle Konsole (Backup-Zugang)
```

### Warum beide Funktionen?

- **NCM** liefert ein vollwertiges Netzwerk über USB — SSH, `apt`, `scp`, alles was TCP/IP kann
- **ACM** gibt eine serielle Konsole — falls SSH nicht startet, das Netzwerk falsch konfiguriert ist, oder der Login-Prompt hängt, kommt man trotzdem auf das Gerät

---

## 4. Configfs-Setup (Init-Skript)

Die USB-Gadget-Konfiguration erfolgt über das Linux-configfs-Dateisystem. Das folgende Skript wird beim Boot ausgeführt (z.B. als systemd-Service oder direkt im Init):

```bash
#!/bin/bash
# USB-Gadget einrichten: NCM (Netzwerk) + ACM (Serial)

GADGET=/config/usb_gadget/g1
UDC="musb-hdrc.1.auto"

# --- configfs mounten ---
mount -t configfs none /config 2>/dev/null

# --- Gadget-Verzeichnis anlegen ---
mkdir -p $GADGET
cd $GADGET

# --- Geräte-Identifikation ---
echo 0x1d6b > idVendor      # Linux Foundation
echo 0x0104 > idProduct     # Multifunction Composite Gadget
echo 0x0100 > bcdDevice     # Geräteversion
echo 0x0200 > bcdUSB        # USB 2.0

# --- Strings (Englisch) ---
mkdir -p strings/0x409
echo "redmi-a5-serenity"           > strings/0x409/serialnumber
echo "Xiaomi"                      > strings/0x409/manufacturer
echo "Redmi A5 Linux USB Gadget"   > strings/0x409/product

# --- Konfiguration ---
mkdir -p configs/b.1
mkdir -p configs/b.1/strings/0x409
echo "NCM + ACM"  > configs/b.1/strings/0x409/configuration
echo 250           > configs/b.1/MaxPower    # mA

# --- Funktion 1: CDC-NCM (Netzwerk) ---
mkdir -p functions/ncm.usb0
# Optional: feste MAC-Adresse setzen
# echo "aa:bb:cc:dd:ee:f1" > functions/ncm.usb0/host_addr
# echo "aa:bb:cc:dd:ee:f2" > functions/ncm.usb0/dev_addr

# --- Funktion 2: CDC-ACM (Serielle Konsole) ---
mkdir -p functions/acm.gs0

# --- Funktionen an Konfiguration binden ---
ln -sf functions/ncm.usb0  configs/b.1/ncm.usb0
ln -sf functions/acm.gs0   configs/b.1/acm.gs0

# --- Gadget aktivieren ---
echo "$UDC" > UDC

# --- Netzwerk-Interface konfigurieren ---
sleep 1
ip link set usb0 up
ip addr add 10.0.0.1/24 dev usb0

echo "USB-Gadget aktiv: NCM (10.0.0.1) + ACM (/dev/ttyGS0)"
```

### Kritische Details

| Detail | Richtig | Falsch (frühere Versuche) |
|--------|---------|--------------------------|
| UDC-Name | `musb-hdrc.1.auto` | `musb-hdrc.0.auto` (aus Property, nie verifiziert) |
| configfs-Mount | `mount -t configfs none /config` | Argumente vertauscht |
| Config-Name | `configs/b.1` | `configs/c.1` |
| Serial-Funktion | `acm.gs0` (CDC-ACM Standard) | `vser.gs7` (Sprd-proprietär) |

Der **UDC-Name** war einer der hartnäckigsten Fehler: `.0.auto` stammte aus einer Android-Property und wurde nie gegen `/sys/class/udc/` geprüft. Erst per ADB auf dem zweiten Handy (`ls /sys/class/udc/`) wurde `musb-hdrc.1.auto` als der richtige Name bestätigt.

---

## 5. Host-Seite (PC mit Linux)

### Automatische Erkennung

Sobald das USB-Kabel eingesteckt wird, erkennt der Host-PC **automatisch**:

```
$ dmesg | tail
usb 1-2: new high-speed USB device
usb 1-2: New USB device found, idVendor=1d6b, idProduct=0104
usb 1-2: Product: Redmi A5 Linux USB Gadget
usb 1-2: Manufacturer: Xiaomi
usb 1-2: SerialNumber: redmi-a5-serenity
cdc_acm 1-2:1.2: ttyACM0: USB ACM device
cdc_ncm 1-2:1.0: usb0: register 'cdc_ncm'
```

**Kein `modprobe` nötig!** Die CDC-Klassen (ACM und NCM) werden vom Standard-Linux-Kernel automatisch unterstützt.

### Netzwerk konfigurieren

```bash
# USB-Netzwerk-Interface einrichten
sudo ip link set usb0 up
sudo ip addr add 10.0.0.2/24 dev usb0

# Verbindung testen
ping 10.0.0.1
```

### Serielle Konsole (Backup)

```bash
# Falls SSH nicht geht: direkter Konsolenzugang
screen /dev/ttyACM0 115200
# oder:
minicom -D /dev/ttyACM0
```

---

## 6. SSH-Zugang

### Auf dem Redmi (Ubuntu 24.04)

Das Ubuntu-Rootfs wird per `debootstrap` auf dem PC erstellt und enthält bereits OpenSSH:

```bash
# Auf dem PC (als root, für das Redmi-Rootfs)
debootstrap --arch=arm64 noble /mnt/redmi-rootfs http://ports.ubuntu.com/ubuntu-ports

# In das Rootfs chrooten und SSH installieren
chroot /mnt/redmi-rootfs
apt update
apt install -y openssh-server

# Root-Login erlauben (für den Anfang)
echo "PermitRootLogin yes" >> /etc/ssh/sshd_config

# Root-Passwort setzen
passwd root

# Netzwerk-Interface vorkonfigurieren
cat > /etc/systemd/network/10-usb0.network << 'EOF'
[Match]
Name=usb0

[Network]
Address=10.0.0.1/24
EOF

systemctl enable systemd-networkd
systemctl enable ssh
exit
```

### Vom PC aus verbinden

```bash
ssh root@10.0.0.1
```

Das funktioniert sofort, sobald Ubuntu auf dem Redmi hochfährt und das USB-Gadget konfiguriert ist.

### Persistenter SSH-Key (empfohlen)

```bash
# Auf dem PC
ssh-keygen -t ed25519 -f ~/.ssh/redmi_a5 -C "redmi-a5-server"
ssh-copy-id -i ~/.ssh/redmi_a5.pub root@10.0.0.1

# In ~/.ssh/config eintragen
cat >> ~/.ssh/config << 'EOF'

Host redmi
    HostName 10.0.0.1
    User root
    IdentityFile ~/.ssh/redmi_a5
EOF

# Danach einfach:
ssh redmi
```

---

## 7. PMIC-Watchdog

### Das Problem

Der SC2730 PMIC (Power Management IC) hat einen Hardware-Watchdog, der das Gerät automatisch neustartet, wenn er nicht regelmäßig zurückgesetzt wird. Unter Android übernimmt das der `watchdog_feeder`-Service. Unter dem eigenen Linux fehlt dieser Service — das Gerät bootet nach wenigen Sekunden/Minuten unangekündigt neu.

### Die Lösung

Der PMIC-Watchdog muss beim Boot gestoppt werden. Das geschieht über das PMIC-Register:

```bash
# PMIC-Watchdog deaktivieren (im Init oder als systemd-Service)
# Der genaue Weg hängt vom Kernel-DTS ab:
# Option 1: Über /sys/class/watchdog/ (wenn wdt-Treiber geladen)
echo V > /dev/watchdog    # Magic close → Watchdog stoppt

# Option 2: Über den Kernel-DTS
# Im Device-Tree den Watchdog-Knoten auf status = "disabled" setzen
```

### systemd-Service (empfohlen)

```ini
# /etc/systemd/system/pmic-watchdog-stop.service
[Unit]
Description=Stop PMIC Watchdog
DefaultDependencies=no
Before=basic.target

[Service]
Type=oneshot
ExecStart=/usr/local/bin/stop-pmic-watchdog.sh
RemainAfterExit=yes

[Install]
WantedBy=sysinit.target
```

**Ergebnis:** Nach dem Stopp des PMIC-Watchdogs läuft Ubuntu dauerhaft ohne unerwartete Neustarts.

---

## 8. USB-Gadget als systemd-Service

Für den automatischen Start beim Boot:

```ini
# /etc/systemd/system/usb-gadget.service
[Unit]
Description=USB Composite Gadget (NCM + ACM)
DefaultDependencies=no
After=systemd-modules-load.service
Before=network-pre.target
Wants=network-pre.target

[Service]
Type=oneshot
ExecStart=/usr/local/bin/usb-gadget-setup.sh
RemainAfterExit=yes
ExecStop=/bin/bash -c 'echo "" > /config/usb_gadget/g1/UDC'

[Install]
WantedBy=sysinit.target
```

Das Skript `/usr/local/bin/usb-gadget-setup.sh` enthält die configfs-Sequenz aus Abschnitt 4.

### Boot-Reihenfolge

```
Kernel startet
  │
  ├─ PMIC-Watchdog stoppen (sysinit.target)
  ├─ USB-Gadget einrichten (sysinit.target)
  │    ├─ configfs mounten
  │    ├─ NCM + ACM anlegen
  │    ├─ UDC binden (musb-hdrc.1.auto)
  │    └─ usb0 Interface: 10.0.0.1/24
  │
  ├─ systemd-networkd (network.target)
  │    └─ usb0 Adresse zuweisen
  │
  └─ sshd starten (multi-user.target)
       └─ SSH auf 10.0.0.1:22 bereit
```

---

## 9. Gesamtbild — Vom Einschalten bis SSH

```
┌─────────────────────────────────────────────────┐
│  REDMI A5 (serenity)                            │
│                                                 │
│  Power-Taste                                    │
│      │                                          │
│      ▼                                          │
│  BROM → SPL → SML → TrustOS → LK               │
│      │                                          │
│      ▼                                          │
│  Mainline-Kernel (ums9230-mainline)             │
│      │                                          │
│      ├─ Display: simple-framebuffer @ 0x9e000000│
│      │    └─ Kernel-Meldungen auf dem Bildschirm│
│      │                                          │
│      ├─ eMMC: cust-Partition mounten            │
│      │    └─ Ubuntu 24.04 rootfs                │
│      │                                          │
│      ├─ PMIC-Watchdog stoppen                   │
│      │                                          │
│      └─ USB-Gadget (NCM + ACM)                  │
│           │                                     │
│           ▼                                     │
│      systemd → sshd                             │
│                                                 │
└────────────USB-Kabel────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────┐
│  HOST-PC (Linux Mint)                           │
│                                                 │
│  Automatisch erkannt:                           │
│      /dev/ttyACM0  (CDC-ACM Serial)             │
│      usb0          (CDC-NCM Netzwerk)           │
│                                                 │
│  $ sudo ip addr add 10.0.0.2/24 dev usb0       │
│  $ ssh root@10.0.0.1                            │
│                                                 │
│  → Shell auf dem Redmi A5 ✓                     │
│                                                 │
└─────────────────────────────────────────────────┘
```

---

## 10. Fallstricke und Lösungen

### 10.1 UDC-Name falsch

**Symptom:** `echo "$UDC" > UDC` scheitert oder USB-Gadget erscheint nicht am Host.

**Lösung:** UDC-Name verifizieren:
```bash
ls /sys/class/udc/
# Erwartet: musb-hdrc.1.auto
```

Der Name `.0.auto` vs. `.1.auto` war einer der zeitraubendsten Fehler im Projekt.

### 10.2 USB-Module doppelt geladen

**Symptom:** Kein USB nach Modul-Laden, obwohl es vorher (ohne Module) funktionierte.

**Ursache:** Die USB-Hardware (MUSB-Core, PHY) ist im Mainline-Kernel **built-in**. Das Laden der gleichnamigen Module per `insmod`/`modprobe` crasht den USB-Stack.

**Lösung:** Keine USB-Hardware-Module laden. `modules.load` listet sie nicht — das ist kein Fehler, sondern Absicht.

### 10.3 Sprd Virtual Serial (vser.gs7) statt CDC-ACM

**Symptom:** Host erkennt USB-Gerät, aber kein `/dev/ttyACM0` und kein Netzwerk.

**Ursache:** Die Android-configfs-Sequenz benutzt `vser.gs7`, eine proprietäre Unisoc-Funktion. Der Host-PC hat keinen Treiber dafür.

**Lösung:** Standard-CDC-ACM (`acm.gs0`) und CDC-NCM (`ncm.usb0`) verwenden. Diese werden vom Linux-Kernel automatisch erkannt.

### 10.4 configfs-Mount-Syntax

**Symptom:** `mount` schlägt fehl oder `/config` ist leer.

**Richtig:**
```bash
mount -t configfs none /config
```

**Falsch** (Argumente vertauscht):
```bash
mount -t configfs /config none   # FALSCH!
```

### 10.5 PMIC-Watchdog vergessen

**Symptom:** Ubuntu bootet, aber nach 30–60 Sekunden startet das Gerät unerwartet neu.

**Ursache:** Der SC2730 PMIC-Watchdog ist aktiv und wird von niemandem gefüttert.

**Lösung:** PMIC-Watchdog beim Boot stoppen (siehe Abschnitt 7).

### 10.6 vbmeta-Flags falsch

**Symptom:** LK panic bei modifiziertem boot/init_boot/vendor_boot.

**Ursache:** AVB 2.0 Verified Boot prüft die Images und bricht bei Nichtübereinstimmung ab.

**Lösung:** Alle vbmeta-Partitionen für Slot B mit Flags=2 (VERIFICATION_DISABLED) überschreiben:
```bash
# vbmeta mit deaktivierter Verifikation erstellen
python3 avbtool make_vbmeta_image --flags 2 --output vbmeta_disabled.img

# Alle 6 vbmeta-Partitionen für Slot B flashen
for p in vbmeta vbmeta_system vbmeta_vendor vbmeta_system_ext vbmeta_product vbmeta_odm; do
    fastboot flash ${p}_b vbmeta_disabled.img
done
```

**Achtung:** Flags=3 (auch HASHTREE_DISABLED) bricht Android — Flags=2 ist korrekt!

### 10.7 miscdata USB-Flag

**Symptom:** LK schaltet USB komplett ab ("usb is configed as off by miscdata").

**Ursache:** Die miscdata-Partition enthält bei Offset 0x2780 ein `usbmux`-Feld, das auf "off" stehen kann.

**Lösung:** miscdata-Partition per BROM auslesen, das Feld auf Nullen setzen, zurückschreiben:
```bash
# Per spd_dump (BROM-Modus: VOL_UP + VOL_DOWN + USB)
spd_dump read_part miscdata 0 0x8000 miscdata.bin
# Offset 0x2780 auf 4 Null-Bytes setzen
printf '\x00\x00\x00\x00' | dd of=miscdata.bin bs=1 seek=10112 conv=notrunc
spd_dump write_part miscdata 0 miscdata.bin
```

---

## 11. Kernel-Cmdline (Mainline)

```
console=tty0 loglevel=8 ignore_loglevel root=/dev/mmcblk0p65 rootfstype=ext4 rootwait rw
```

| Parameter | Bedeutung |
|-----------|-----------|
| `console=tty0` | Kernel-Meldungen auf dem Display (Framebuffer-Konsole) |
| `loglevel=8` | Alle Kernel-Meldungen anzeigen |
| `root=/dev/mmcblk0p65` | cust-Partition als Root-Dateisystem |
| `rootfstype=ext4` | Dateisystemtyp |
| `rootwait` | Warten bis das Root-Device verfügbar ist |
| `rw` | Root-Partition beschreibbar mounten |

**Hinweis:** LK setzt im NORMAL_MODE `console=null`. Der Mainline-Kernel verwendet `CONFIG_CMDLINE_FORCE`, um seine eigene Cmdline durchzusetzen und die LK-Cmdline zu ignorieren.

---

## 12. Referenzprojekte

Diese Projekte dienten als Vorlage für die USB-Gadget-Konfiguration:

| Projekt | Gerät | SoC | Besonderheit |
|---------|-------|-----|-------------|
| [mu300-linux](https://github.com/nicknisi/mu300-linux) | ZTE F50 (MU300) | Unisoc T760 | USB-ACM + ECM über configfs, Ubuntu + systemd |
| [pixel8-linux](https://github.com/nicknisi/pixel8-linux) | Google Pixel 8 | Tensor G3 | configfs-ACM + mknod ttyGS0 |
| [e5-linux](https://github.com/nicknisi/e5-linux) | Rongyue E5 | Unisoc UMS9621 | Gleicher Ansatz wie mu300 |
| [ums9230-mainline](https://codeberg.org/ums9230-mainline/linux) | Jolla C2, Reeder S19 | UMS9230/UMS512 | Kernel-Fork als Basis |

---

## 13. Nächste Schritte

- **Display:** Echter DRM-Treiber statt simple-framebuffer (für Beschleunigung und Touchscreen)
- **Partition:** Ubuntu von `cust` (2 GB) auf `userdata` (50+ GB) umziehen
- **USB-Host:** USB-OTG umschalten für LAN-Adapter und Hub
- **Autostart:** Kompletter systemd-basierter Boot ohne manuelle Schritte
- **Installer:** Fertiges Flash-Paket auf GitHub (EberhartLeberhart) veröffentlichen

---

## Lizenz

Dieses Dokument ist Teil des [Redmi A5 (serenity) Linux-Projekts](https://github.com/EberhartLeberhart/Mainline-Linux-f-r-das-Xiaomi-Redmi-A5-serenity-) und steht unter der MIT-Lizenz.
