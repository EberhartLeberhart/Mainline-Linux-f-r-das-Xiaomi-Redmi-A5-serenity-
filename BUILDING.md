# Building and installing — Mainline Linux on the Xiaomi Redmi A5 (serenity)

This guide takes you from a clean Linux PC to a Redmi A5 that boots our mainline kernel (Linux 7.1-rc1) with Ubuntu 24.04.
It describes the way the project author's own phone is built today (November 2026).

> **Read this first**
> - This is an experimental hobby project. You can lose all data on the phone. **Back up every partition first.**
> - The bootloader must already be **unlocked** (`fastboot getvar unlocked` → `yes`). How that was done is documented in
>   [BOOTLOADER_UNLOCK.md](BOOTLOADER_UNLOCK.md); this guide does not repeat it.
> - Linux goes to **slot b**; never touch slot a. Note: once `userdata` holds Linux, Android on slot a no longer
>   starts properly (its recovery says "Can't load Android system" — **do not** choose "Factory data reset", that
>   would erase Linux). Your rescue is **fastboot** (Volume down + Power), which always works.
> - **Panel variants:** the Redmi A5 ships with at least four panels (C3Z_35, C3Z_36, C3Z_42, C3Z_43) with different
>   display/touch chips. Check yours in Android: `tr " \0" "\n\n" < /proc/device-tree/chosen/bootargs | grep lcd_name`
>   (needs root). **`Panel_C3Z_42_02_0a_vid`: display and touch work** (default). **`Panel_C3Z_36_10_0d_vid`: display
>   works, touch not yet** — build the `vendor_boot` with `PANEL=c3z36` (step 3). Other variants boot and run fine over
>   USB/SSH, but the screen stays black or distorted after boot (work in progress).
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
| `init_boot_b` | Android boot image **with an empty ramdisk** | `werkzeuge/pc/leere_images.py` |
| `dtbo_b` | **empty** device-tree overlay | `werkzeuge/pc/leere_images.py` |

The kernel command line is built into the kernel (`CONFIG_CMDLINE_FORCE`, see `kernel/redmi_a5_defconfig`):
it mounts `root=PARTLABEL=userdata` **read-only** with `fsck.repair=yes`, so systemd checks (and if needed repairs)
the root file system on every boot before the `/etc/fstab` line (step 4) switches it to read-write. Until 07.11.2026
the command line said `rw`, which silently skipped that check (see STATUS.md). The vbmeta partitions are **left unchanged**: on the author's unlocked phone all
`vbmeta*_a/_b` still have flags = 0 (checked 05.11.2026), and slot b boots anyway.

**Why `init_boot_b` and `dtbo_b` must be emptied** (found 07.11.2026 on a phone in factory state, which hung at the
logo and rebooted in a loop): on a stock phone `init_boot_b` holds Android's first-stage ramdisk — the bootloader
hands it to our kernel, which then starts Android's init instead of Ubuntu. And `dtbo_b` holds Xiaomi's overlay for the
Android device tree, which the bootloader lays over ours. **Both are needed:** with only `init_boot_b` emptied
and Xiaomi's original `dtbo_b` the phone hangs at the logo again (counter-test 07.11.2026). The author's own phone had both emptied long ago, which is
why this was only noticed with a second phone.

## 1. Host PC

Tested on Linux Mint (any Debian/Ubuntu-based distribution should work).

```bash
sudo apt install git gcc-aarch64-linux-gnu build-essential bc flex bison libssl-dev libelf-dev \
  device-tree-compiler python3 fastboot debootstrap qemu-user-static android-sdk-libsparse-utils
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
git am ~/redmi-a5/kernel/patches/*.patch          # all patches apply cleanly (checked 05.11.2026); they also
                                                  # bring arch/arm64/configs/redmi_a5_defconfig (= kernel/redmi_a5_defconfig)
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- redmi_a5_defconfig   # CROSS_COMPILE here too, see note below
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc) Image dtbs modules sprd/ums9230-xiaomi-serenity.dtb
```

The last target builds our device tree explicitly — its line in `arch/arm64/boot/dts/sprd/Makefile` is still missing
in the patches (found by the first outside tester, fix coming).

**Use the same `CROSS_COMPILE` for `defconfig` and for the build.** Several options depend on what the compiler
supports; configuring with the host compiler and building with the cross compiler makes `make` restart the
configuration and ask questions (`… (NEW)`). If that happened: run the `defconfig` line again with `CROSS_COMPILE`,
then build. (Found 06.11.2026 by the first outside tester — thank you!)

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

# empty init_boot and dtbo for slot b (see "Why" above)
python3 ./leere_images.py ~/redmi-build                      # -> init_boot_leer.img, dtbo_leer.img
```

For the panel variant C3Z_36 (see the warning at the top) add `PANEL=c3z36` to the last line
(`VB=… BASIS_DTB=$DTB PANEL=c3z36 ./vendorboot_rezept.sh`).

`vendorboot_rezept.sh` collects every device-tree change of the project in one place, with switches such as `WCN=1`
(Wi-Fi/Bluetooth core, needs the firmware from step 6). Without switches you get the default configuration.

> Verified 06.11.2026: a `vendor_boot` built this way (with `WCN=1`) boots the author's phone; display, Wi-Fi
> autostart and everything else work as with the old hand-made image (same device tree and bootconfig).

## 4. Root filesystem (Ubuntu 24.04)

During the project the root filesystem was built with `debootstrap` on the PC, first written to the small `cust`
partition, and later copied by the running system to `userdata`. Building it directly for `userdata` (verified
07.11.2026 on a second phone):

```bash
R=/mnt/redmi-rootfs
sudo debootstrap --arch=arm64 noble $R http://ports.ubuntu.com/ubuntu-ports
# qemu-user-static lets an x86 PC run the arm64 programs inside the chroot
sudo chroot $R apt-get install -y openssh-server systemd-sysv udev kmod sudo nano
sudo chroot $R passwd root
echo redmi-a5 | sudo tee $R/etc/hostname
# SSH login with your key (Ubuntu does not allow root login with a password over SSH)
sudo mkdir -m700 -p $R/root/.ssh && sudo cp ~/.ssh/id_ed25519.pub $R/root/.ssh/authorized_keys
# the kernel mounts / read-only first (and systemd runs fsck); without this line it stays read-only
echo "PARTLABEL=userdata / ext4 defaults,noatime 0 1" | sudo tee $R/etc/fstab
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

Recommended: the power button handler. Without it Ubuntu **powers off on a short press** of the power button.
With it: hold 3 s = shut down, short press = nothing (or screen light briefly on, with `redmi-licht.sh`):

```bash
sudo install -m755 $H/redmi-taste.sh $R/usr/local/sbin/
sudo install -m644 $H/redmi-taste.service $R/etc/systemd/system/
sudo install -D -m644 $H/redmi-taste-logind.conf $R/etc/systemd/logind.conf.d/redmi-taste.conf
sudo chroot $R systemctl enable redmi-taste.service
```

Pack the tree into an ext4 image and write it to `userdata` (this **erases** the Android user data):

```bash
truncate -s 4G rootfs.img && sudo mkfs.ext4 -L ubuntu -d $R rootfs.img
img2simg rootfs.img rootfs_sparse.img          # sparse format (package android-sdk-libsparse-utils)
fastboot flash userdata rootfs_sparse.img
```

The 4 GB file system can be grown to the whole partition later on the phone: `resize2fs /dev/disk/by-partlabel/userdata`.

On the PC, `werkzeuge/pc/net.sh` sets up the USB network after each boot of the phone; then `ssh root@192.168.7.2`.
([USB_SSH_SETUP.md](USB_SSH_SETUP.md) still shows the older `10.0.0.x` addresses.)

## 5. First installation (fastboot)

Phone in fastboot mode (Volume down + Power):

```bash
cd ~/redmi-build
fastboot flash boot_b boot_mainline.img
fastboot flash vendor_boot_b vendor_boot_rezept.img
fastboot flash init_boot_b init_boot_leer.img
fastboot flash dtbo_b dtbo_leer.img
fastboot set_active b
fastboot reboot
```

Verified 07.11.2026 on a second phone in factory state (with `userdata` from step 4). The bootloader takes about
1.5 minutes; then kernel messages and the boot logo appear. `fastboot reboot` may report `FAILED (No such device)` —
the phone was just faster than the answer; it does reboot.

After boot: `werkzeuge/pc/net.sh`, then `ssh root@192.168.7.2`. **Only connect one phone by USB at a time** — every
phone uses 192.168.7.2. With several phones, give each its own SSH host key name, e.g.
`ssh -o HostKeyAlias=redmi-a5-zwei root@192.168.7.2`, otherwise SSH warns about a changed host key.

**Back to slot a:** `fastboot set_active a && fastboot reboot` (Android itself won't start with Linux on `userdata`, see the top).

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

**Bluetooth** (verified 07.11.2026: Bluetooth 5.0, Classic and LE, scan finds devices) runs on the same Wi-Fi/BT core,
so it needs Wi-Fi set up as above (`WLAN=an` starts the core). Two more files from `odm/firmware`, converted with
[`werkzeuge/pc/bt_ini_bin.py`](werkzeuge/pc/bt_ini_bin.py) — the transmit power values in them are used unchanged:

```bash
O=~/odm/firmware          # folder firmware/ of your own odm partition copy
python3 ~/redmi-a5/werkzeuge/pc/bt_ini_bin.py $O/bt_configure_pskey.ini bt_config_pskey.bin   # 160 bytes
python3 ~/redmi-a5/werkzeuge/pc/bt_ini_bin.py $O/bt_configure_rf.ini bt_config_rf.bin         # 252 bytes
scp bt_config_pskey.bin bt_config_rf.bin root@192.168.7.2:/lib/firmware/sprd/
```

On the phone: `apt install bluez`, then `redmi-bt.sh` to `/usr/local/sbin/`, `redmi-bt.service` to
`/etc/systemd/system/` (enable it) and `bt.conf` (`BT=an`/`BT=aus`) to `/etc/redmi/`. The device tree has no
Bluetooth address, so the script sets one at boot (Wi-Fi MAC + 1); only then the driver sends the configuration to
the chip. `redmi-bt.sh an|aus|status` switches it.

## Reporting back

Please open an issue with what worked and what didn't. A `dmesg` log helps a lot — but **remove MAC addresses,
serial numbers and IMEIs** before posting. Reports from the Poco C71 (very similar hardware?) are very welcome.

License: kernel patches and tools GPL-2.0-only, documentation CC BY-SA 4.0 — see [LIZENZEN.md](LIZENZEN.md).
