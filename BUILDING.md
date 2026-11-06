# Building and installing — Mainline Linux on the Xiaomi Redmi A5 (serenity)

This guide takes you from a clean Linux PC to a Redmi A5 that boots our mainline kernel (Linux 7.1-rc1) with Ubuntu 24.04.
It describes the way the project author's own phone is built today (November 2026).

> **Read this first**
> - This is an experimental hobby project. You can lose all data on the phone. **Back up every partition first.**
> - The bootloader must already be **unlocked** (`fastboot getvar unlocked` → `yes`). How that was done is documented in
>   [BOOTLOADER_UNLOCK.md](BOOTLOADER_UNLOCK.md); this guide does not repeat it.
> - Android stays on **slot a** as a rescue system. Linux goes to **slot b**. Never touch slot a.
> - Steps marked **(not re-verified)** are known to have worked during the project, but have not been repeated from
>   scratch with this exact guide yet. Please report how it went (see the end).
> - What works and what doesn't: [README.md, section 15](README.md#15-booting-linux) and [STATUS.md](STATUS.md) (German).
>   Most other project documents are in German.

## Overview

| Partition (slot b) | Content | Built by |
|---|---|---|
| `boot_b` | kernel `Image` (Android boot image v4, no ramdisk) | `werkzeuge/pc/pack.sh` |
| `vendor_boot_b` | device tree (+ empty ramdisk, bootconfig) — **nothing from Xiaomi** | `werkzeuge/pc/vendorboot_basis.sh` + `vendorboot_rezept.sh` |
| `userdata` | Ubuntu 24.04 root filesystem (ext4) | `debootstrap` |
| `misc` | "boot slot b, fall back to a after 7 failed tries" | `werkzeuge/pc/misc_slot_b.py` (only for updates over SSH) |

The kernel command line is built into the kernel (`CONFIG_CMDLINE_FORCE`, see `kernel/redmi_a5_defconfig`):
it mounts `root=PARTLABEL=userdata`. The vbmeta partitions are **left unchanged**: on the author's unlocked phone all
`vbmeta*_a/_b` still have flags = 0 (checked 05.11.2026), and slot b boots anyway.

## 1. Host PC

Tested on Linux Mint (any Debian/Ubuntu-based distribution should work).

```bash
sudo apt install git gcc-aarch64-linux-gnu build-essential bc flex bison libssl-dev libelf-dev \
  device-tree-compiler python3 fastboot debootstrap qemu-user-static
```

Google's `mkbootimg` (Python scripts, used for the Android boot images):

```bash
git clone https://android.googlesource.com/platform/system/tools/mkbootimg ~/mkbootimg-aosp
```

The project scripts expect this layout (same as on the author's PC):

| Path | Content |
|---|---|
| `~/ums9230-linux` | kernel source tree |
| `~/redmi-build` | build output (images), copies of `werkzeuge/pc/*` |
| `~/mkbootimg-aosp` | Google mkbootimg |

```bash
git clone https://github.com/EberhartLeberhart/Mainline-Linux-f-r-das-Xiaomi-Redmi-A5-serenity- ~/redmi-a5
mkdir -p ~/redmi-build && cp ~/redmi-a5/werkzeuge/pc/* ~/redmi-build/
```

## 2. Kernel

The base is Otto Pflüger's UMS9230 mainline tree on Codeberg, plus all patches from [`kernel/patches/`](kernel/patches/).

```bash
git clone https://codeberg.org/ums9230-mainline/linux.git ~/ums9230-linux
cd ~/ums9230-linux
git checkout -b redmi-a5 4a5b97b821b846f9a25a99a4fdcb3040fd910965
git am ~/redmi-a5/kernel/patches/*.patch          # 33 patches, all apply cleanly (checked 05.11.2026)
cp ~/redmi-a5/kernel/redmi_a5_defconfig arch/arm64/configs/
make ARCH=arm64 redmi_a5_defconfig
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc) Image dtbs modules
```

**Size limit:** the bootloader only loads a kernel image well below 48,103,424 bytes. With `redmi_a5_defconfig`
(only the Unisoc platform enabled) the image is about 28 MB. If you enable more, check `ls -l arch/arm64/boot/Image`
— a too large kernel boots after a warm reboot but **not** after a real power-off.

Result: `arch/arm64/boot/Image`, `arch/arm64/boot/dts/sprd/ums9230-xiaomi-serenity.dtb` and the modules.

## 3. Boot images

```bash
cd ~/redmi-build
DTB=~/ums9230-linux/arch/arm64/boot/dts/sprd/ums9230-xiaomi-serenity.dtb

# boot.img (kernel only); pack.sh also checks that the image really is the one just built
./pack.sh                                                    # -> boot_mainline.img

# empty vendor_boot shell, then all our device-tree changes on top of it
./vendorboot_basis.sh $DTB vendor_boot_basis.img
VB=vendor_boot_basis.img BASIS_DTB=$DTB ./vendorboot_rezept.sh   # -> vendor_boot_rezept.img
```

`vendorboot_rezept.sh` collects every device-tree change of the project in one place, with switches such as `WCN=1`
(Wi-Fi/Bluetooth core, needs the firmware from step 6). Without switches you get the default configuration.

> **(not re-verified)** The `vendor_boot` built this way contains exactly the same device tree and bootconfig as the
> author's working image (compared 05.11.2026), but has not yet been booted on the phone itself.

## 4. Root filesystem (Ubuntu 24.04) — (not re-verified)

During the project the root filesystem was built with `debootstrap` on the PC, first written to the small `cust`
partition, and later copied by the running system to `userdata`. Building it directly for `userdata`:

```bash
R=/mnt/redmi-rootfs
sudo debootstrap --arch=arm64 noble $R http://ports.ubuntu.com/ubuntu-ports
# qemu-user-static lets an x86 PC run the arm64 programs inside the chroot
sudo chroot $R apt-get install -y openssh-server systemd-sysv udev kmod sudo nano
sudo chroot $R passwd root
echo redmi-a5 | sudo tee $R/etc/hostname
# SSH login with your key (Ubuntu does not allow root login with a password over SSH)
sudo mkdir -m700 -p $R/root/.ssh && sudo cp ~/.ssh/id_ed25519.pub $R/root/.ssh/authorized_keys
```

Kernel modules (they must match the kernel version exactly):

```bash
cd ~/ums9230-linux
sudo make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- INSTALL_MOD_PATH=$R INSTALL_MOD_STRIP=1 modules_install
```

The project's files for the phone ([`werkzeuge/handy/`](werkzeuge/handy/)) — the minimum is the USB network
(phone `192.168.7.2`, PC `192.168.7.1`) and the "slot b booted fine" marker:

```bash
H=~/redmi-a5/werkzeuge/handy
sudo install -m755 $H/redmi-usb-gadget.sh $R/usr/local/sbin/
sudo install -m644 $H/redmi-usb-gadget.service $H/redmi-boot-ok.service $R/etc/systemd/system/
sudo install -m644 $H/10-usb0.network $R/etc/systemd/network/
python3 ~/redmi-a5/werkzeuge/pc/misc_slot_b.py --ok /tmp/abc_b_ok.bin
sudo install -D -m644 /tmp/abc_b_ok.bin $R/usr/local/lib/redmi/abc_b_ok.bin
sudo chroot $R systemctl enable redmi-usb-gadget.service redmi-boot-ok.service systemd-networkd ssh
```

`redmi-boot-ok.service` matters: without it the bootloader counts down the boot tries of slot b and returns to
Android after 7 boots. The other scripts (backlight/sleep, charge limit, power button, cpufreq, USB role switching)
are optional; each one explains itself in its first lines.

Pack the tree into an ext4 image and write it to `userdata` (this **erases** the Android user data):

```bash
truncate -s 4G rootfs.img && sudo mkfs.ext4 -L ubuntu -d $R rootfs.img
fastboot flash userdata rootfs.img
```

On the PC, `werkzeuge/pc/net.sh` sets up the USB network after each boot of the phone; then `ssh root@192.168.7.2`.
([USB_SSH_SETUP.md](USB_SSH_SETUP.md) still shows the older `10.0.0.x` addresses.)

## 5. First installation (fastboot) — (not re-verified)

Phone in fastboot mode (Volume down + Power):

```bash
cd ~/redmi-build
fastboot flash boot_b boot_mainline.img
fastboot flash vendor_boot_b vendor_boot_rezept.img
fastboot set_active b
fastboot reboot
```

The bootloader takes a while; then kernel messages and the boot logo appear on the screen.
**Back to Android at any time:** `fastboot set_active a && fastboot reboot`.

## Updates from the running system

Once Linux runs and SSH over USB works, the author updates over SSH instead of fastboot:
`~/redmi-build/deploy.sh vendor_boot_rezept.img` writes `boot_b`, `vendor_boot_b`, the modules and `misc`
(from `misc_slot_b.py`: slot b, back to slot a after 7 failed boots) and reboots. See the comments in the script.

## 6. Firmware you must take from your own phone

We do **not** distribute Xiaomi/Unisoc firmware. Wi-Fi needs two files from your own phone's firmware
(`odm` partition, folder `firmware/`), copied to the phone's Linux system:

| File on the phone | Source |
|---|---|
| `/lib/firmware/sprd/wcnmodem.bin` | Wi-Fi/Bluetooth core firmware from `odm/firmware` |
| `/lib/firmware/sprd/wifi_board_config.bin` | converted from `wifi_board_config.ini` with [`kernel/wcn-test/wcn_ini_bin.c`](kernel/wcn-test/wcn_ini_bin.c) (instructions at the top of the file) |

Wi-Fi autostart with an on/off switch (all in [`werkzeuge/handy/`](werkzeuge/handy/)): `redmi-wlan.sh` to
`/usr/local/sbin/`, `redmi-wlan.service` to `/etc/systemd/system/` (enable it), `30-wlan0.network` to
`/etc/systemd/network/`, `wlan.conf` (`WLAN=an` = on, `WLAN=aus` = off) to `/etc/redmi/`, and your Wi-Fi credentials in
`wpa_supplicant` format to `/etc/redmi/wpa-wlan0.conf` (mode 600). Build the `vendor_boot` with `WCN=1`.
Then `redmi-wlan.sh an|aus|status` switches it. Details: [STATUS.md](STATUS.md) (German, section "WLAN").

## Reporting back

Please open an issue with what worked and what didn't. A `dmesg` log helps a lot — but **remove MAC addresses,
serial numbers and IMEIs** before posting. Reports from the Poco C71 (very similar hardware?) are very welcome.

License: kernel patches and tools GPL-2.0-only, documentation CC BY-SA 4.0 — see [LIZENZEN.md](LIZENZEN.md).
