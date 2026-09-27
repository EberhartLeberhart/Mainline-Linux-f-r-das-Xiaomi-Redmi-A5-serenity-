# Xiaomi Redmi A5 — Complete Reverse Engineering Documentation

**Codename**: `serenity`  
**Model**: 25028RN03A (Global)  
**SoC**: Unisoc T7250 / UMS9230E (`sharkl5pro` family)  
**Stock OS**: Android 15 Go — Build `A15.0.20.0.VGWMIXM`  
**Status**: ✅ Bootloader unlocked, custom Mainline kernel boots, Ubuntu 24.04 runs

> This is the most complete public documentation of the Xiaomi Redmi A5's internals.
> Built from hands-on reverse engineering — no leaked schematics, no vendor documentation.

---

## Table of Contents

- [1. Hardware Overview](#1-hardware-overview)
- [2. Boot Chain](#2-boot-chain)
- [3. BROM & Download Mode](#3-brom--download-mode)
- [4. Secure Boot Analysis](#4-secure-boot-analysis)
- [5. Bootloader Unlock](#5-bootloader-unlock)
- [6. Fastboot Mode](#6-fastboot-mode)
- [7. Partition Layout](#7-partition-layout)
- [8. Boot Images (GKI v4)](#8-boot-images-gki-v4)
- [9. vbmeta & AVB 2.0](#9-vbmeta--avb-20)
- [10. Vendor Ramdisk](#10-vendor-ramdisk)
- [11. USB Stack](#11-usb-stack)
- [12. Kernel Configuration](#12-kernel-configuration)
- [13. miscdata Partition](#13-miscdata-partition)
- [14. Display & Framebuffer](#14-display--framebuffer)
- [15. Booting Linux](#15-booting-linux)
- [16. Lessons Learned](#16-lessons-learned)
- [17. Tools & References](#17-tools--references)

---

## 1. Hardware Overview

| Component       | Details |
|-----------------|---------|
| SoC             | Unisoc T7250 (UMS9230E), `sharkl5pro` platform |
| CPU             | 8 cores (ARM Cortex-A75 + Cortex-A55, big.LITTLE) |
| RAM             | 3 GB LPDDR4X |
| Storage         | eMMC 5.1 — 64 GB or 128 GB variants |
| Display         | IPS LCD, ~6.7" |
| Modem           | Integrated LTE (T615-class baseband) |
| USB             | Micro-USB, OTG capable (`dr-mode = "otg"` in DTS) |
| USB Controller  | `sprd,qogirl6-musb` @ `0x64900000` (Mentor Graphics MUSB) |
| FM Radio        | Present in SoC (TX capability unclear — PCB routing unknown) |

**Storage variants observed:**
- Phone 1: `userdata` = 50,841 MB (~64 GB total flash)
- Phone 2: `userdata` = 110,473 MB (~128 GB total flash)

### SoC Family

The T7250 (UMS9230E) belongs to Unisoc's `sharkl5pro` family, which also includes:
- T618 / UMS512 (earlier generation, well-documented via rg-rotate-linux)
- T615 (very close relative — same DRAM init, same FDL packages)
- T760 (newer, used in ZTE F50 / mu300-linux project)

This family relationship means that many drivers, DTS structures, and boot procedures are shared across these SoCs.

---

## 2. Boot Chain

```
BROM (silicon ROM)
  │
  ├── Checks DHTB SHA256 (always)
  ├── Checks RSA signature (ONLY if eFuse ROTPK burned — NOT burned on this device)
  │
  ▼
SPL (splloader / u-boot-spl-16k-emmc-sign.bin)
  │
  ├── DRAM initialization (T7250-specific — different from T606/UMS9230!)
  ├── eMMC initialization
  │
  ▼
SML (Secure Monitor Layer)
  │
  ▼
TrustOS (TEE)
  │
  ▼
LK (Little Kernel / U-Boot hybrid)
  │
  ├── Sets secureboot=1 (software flag)
  ├── Performs AVB 2.0 verification
  ├── Replaces console=ttyS1,921600n8 with console=null in NORMAL_MODE
  ├── Reads miscdata for USB mode (off/uart/jtag/normal)
  ├── Displays boot logo + padlock status
  │
  ▼
Linux Kernel (from boot_b) + Ramdisks (vendor_boot_b + init_boot_b)
  │
  ├── First-stage init (from init_boot_b) mounts super partition via fstab
  ├── Second-stage init starts Android services
  │
  ▼
Android / Linux userspace
```

### Key Insight: LK Console Suppression

LK actively replaces the kernel command line parameter `console=ttyS1,921600n8` with `console=null` in normal boot mode. This means:
- **No kernel log output via UART** in normal boot
- The actual serial console is `ttyS1` at `921600` baud (not the common 115200)
- To get serial output, either patch LK or use the `usb2spuart` miscdata mode

### Boot Log Access

LK writes its own boot log to the `uboot_log` partition, which survives reboots and is readable via BROM/spd_dump:

```bash
# Read boot log via BROM
r uboot_log uboot_log.bin
strings uboot_log.bin | less
```

This was instrumental in diagnosing boot failures — LK logs AVB verification results, Secure Boot panics, and partition mount attempts.

---

## 3. BROM & Download Mode

### Entering Download Mode (BROM)

1. Power off the phone completely (long-press Power)
2. Hold **VOL_UP + VOL_DOWN** simultaneously
3. While holding both buttons, insert USB cable
4. Release buttons after ~2 seconds

The phone will appear as USB device **VID `1782`** (Spreadtrum).

### udev Rule (Linux Host)

```bash
# /etc/udev/rules.d/51-sprd.rules
SUBSYSTEM=="usb", ATTR{idVendor}=="1782", MODE="0666", GROUP="plugdev"
```

```bash
sudo udevadm control --reload-rules && sudo udevadm trigger
```

### Connecting with spd_dump

**Critical**: You must use the **UMS9230E-specific** package (`linux_ums9230e_Tecno_KL4`), NOT the generic `ums9230_universal_unlock_EMMC` package. The DRAM initialization code is different!

```bash
cd ~/redmi-unlock-work

sudo ./ums9230e/linux_ums9230e_Tecno_KL4/spd_dump --wait 300 \
  exec_addr 0x65015f08 \
  fdl ums9230e/linux_ums9230e_Tecno_KL4/fdl1-dl.bin 0x65000800 \
  fdl ums9230e/linux_ums9230e_Tecno_KL4/fdl2-dl.bin 0x9efffe00 \
  exec
```

**What happens:**
1. `spd_dump` connects to BROM (responds with "SPRD3")
2. CVE-2022-38694 exploit executes at `exec_addr 0x65015f08`
3. FDL1 loads to `0x65000800` — initializes DRAM + eMMC
4. FDL2 loads to `0x9efffe00` — provides interactive partition access
5. You get an `FDL2>` prompt

### spd_dump Interactive Commands

| Command | Description |
|---------|-------------|
| `r <part> <file>` | Read partition to file |
| `w <part> <file>` | Write file to partition |
| `verity 0` | Disable dm-verity (modifies vbmeta on-device) |
| `verity 1` | Enable dm-verity |
| `set_active a` | Set active slot to A |
| `set_active b` | Set active slot to B |
| `reset` | Reboot device |
| `poweroff` | Power off device |
| `reboot-recovery` | Reboot to recovery |
| `reboot-fastboot` | Reboot to fastbootd |
| `rawdata 0` | Print partition table |

### Why Generic FDLs Don't Work

The T7250 requires device-specific FDL binaries because:
- **FDL1**: DRAM initialization is SoC-specific. The universal FDL1 initializes DRAM for T606, which hangs on T7250 ("CHECK_BAUD FAIL")
- **FDL2**: Xiaomi's own `lk-fdl2-sign.bin` starts but returns `0x00fe` on every command ("Partition table not available" / "Flashing not allowed for Protected Partitions")
- **fdl2-cboot.bin** (unsigned): Rejected by FDL1's signature check (FDL1→FDL2 signature verification remains active even after BROM exploit)
- **Universal fdl2-dl.bin**: Rejected by FDL1 entirely (timeout during send)

The `linux_ums9230e_Tecno_KL4` package from XDA contains compatible FDLs built for the UMS9230E platform.

---

## 4. Secure Boot Analysis

### TL;DR: Paper Tiger 🐯

Secure Boot on this device is **software-only**. The hardware security mechanisms exist in silicon but are **not activated** because eFuses were never burned during manufacturing.

### DHTB Header Format

Every signed binary (SPL, FDL, LK) is wrapped in a DHTB container:

```
Offset  Size    Field
──────  ────    ─────
0x000   4       Magic: "DHTB"
0x008   32      SHA256 hash of payload
0x030   4       Payload length
0x034   ...     (reserved)
0x200   N       Payload (SPL/FDL/LK code)
0x200+N ~1.7K   SIMGHDR block (RSA-2048 public key + signature)
```

### Two-Layer Verification

| Layer | What | When Checked |
|-------|------|--------------|
| DHTB SHA256 | Integrity hash of payload | **ALWAYS** — by BROM |
| RSA-2048 | Cryptographic signature (SIMGHDR) | **ONLY if eFuse ROTPK hash is burned** |

### Proof: eFuses Are Not Burned

1. **CVE-2022-38694** successfully loaded unsigned FDL code → BROM did not check RSA signature → eFuse ROTPK hash is all-zeros (unburned)
2. Budget Xiaomi/Unisoc devices (Redmi A-Serie) do not burn eFuse keys in manufacturing — it costs extra
3. NCC Group confirmed: *"eFuse-based root key storage depends on proper burning; unburned devices skip validation entirely."*

### What "secureboot=1" Actually Means

```
secureboot=1        ← Software flag set by LK (Little Kernel)
flash.locked=1      ← Software lock (cleared by unlock procedure)
verifiedbootstate=green  ← AVB result (not BROM Secure Boot)
```

These are software-layer checks, not hardware-backed. LK sets `secureboot=1` regardless of actual eFuse state.

### Implications

- **Custom SPL** can be loaded by fixing only the DHTB SHA256 hash — no RSA signature needed
- **Arbitrary BROM-level code execution** via `exec_addr`
- **All partitions readable/writable** via BROM exploit
- A persistent bypass (permanently patched SPL) is theoretically possible

### Additional Vulnerability: CVE-2022-38691/38692

Type-0 certificates skip the `memcmp` of the public key hash against eFuse. This means even IF eFuse keys were burned, arbitrary RSA keys could be injected. A second layer of defense that doesn't defend.

---

## 5. Bootloader Unlock

### Prerequisites

- OEM unlock enabled in Developer Options (Settings → About → tap Build 7x → Developer Options → OEM Unlock)
- spd_dump with UMS9230E-specific FDLs
- Linux host with USB access

### Standard Methods That DON'T Work

| Method | Result |
|--------|--------|
| `fastboot oem unlock` | `Err:0xffffffff` |
| Mi Unlock Tool (Windows) | "not supported" |
| ADB/Fastboot standard | No Spreadtrum driver on Linux |

The only working method is the BROM CVE exploit.

### Unlock Procedure

```bash
cd ~/redmi-unlock-work

# 1. Enter BROM mode (VOL_UP + VOL_DOWN + USB)
# 2. Run the unlock script:
sudo ./ums9230e/linux_ums9230e_Tecno_KL4/spd_dump --wait 300 \
  exec_addr 0x65015f08 \
  fdl ums9230e/linux_ums9230e_Tecno_KL4/fdl1-dl.bin 0x65000800 \
  fdl ums9230e/linux_ums9230e_Tecno_KL4/fdl2-dl.bin 0x9efffe00 \
  exec

# 3. At FDL2> prompt, run the unlock autopatch:
# (This writes the unlock token to miscdata at offset 0x2000)
```

Alternatively, use the included `unlock_autopatch_9230.sh` script.

### Verification

After unlock and reboot:

```bash
fastboot getvar unlocked
# Expected: unlocked: yes
```

The boot screen will show an open padlock icon (with stock vbmeta). With vbmeta flags=2, the padlock disappears entirely.

### Unlock Token Location

The unlock token is stored in the `miscdata` partition at offset `0x2000`. It survives factory resets and ROM flashes. Once written, the device stays unlocked even after re-flashing stock firmware.

---

## 6. Fastboot Mode

### Entering Fastboot

1. Power off the phone
2. Hold **VOL_DOWN + Power**
3. Release when "FASTBOOT" appears on screen
4. Connect USB

### Why Fastboot Matters

Fastboot is dramatically simpler than the BROM path:

| | BROM/spd_dump | Fastboot |
|---|---|---|
| Entry | VOL_UP+DOWN+USB, exploit, FDL1, FDL2 | VOL_DOWN+Power |
| Command | `w vendor_boot_b file.bin` | `fastboot flash vendor_boot_b file.bin` |
| Speed | Slow (serial protocol) | Fast (USB bulk transfer) |
| Risk | Low (robust) | Low |
| Requires | spd_dump + specific FDLs | Standard fastboot binary |

**We discovered Fastboot works only late in the project** — all initial work used the BROM path because `fastboot oem unlock` had failed (giving `Err:0xffffffff`), which led us to assume Fastboot was completely non-functional. In reality, Fastboot for *flashing* worked perfectly after the BROM-based unlock.

### Useful Fastboot Commands

```bash
# Flash individual partitions
fastboot flash vendor_boot_b vendor_boot_modified.bin
fastboot flash vbmeta_b vbmeta_modified.bin
fastboot flash boot_b boot_custom.bin

# Flash full stock ROM (both slots)
cd serenity_global_images_A15.0.20.0.VGWMIXM_15.0
./flash_all.sh

# Check unlock status
fastboot getvar unlocked

# Reboot
fastboot reboot

# Boot slot selection
fastboot set_active a
fastboot set_active b
```

### Fastboot vs. `fastboot boot`

`fastboot boot <image>` (temporary boot without flashing) is **prohibited** on this device — it returns an error. Images must be flashed to a partition.

---

## 7. Partition Layout

The device has **81 partitions** with a **Virtual A/B (VAB)** slot scheme.

### Slot Structure

- **Active slot**: B (default on stock)
- **Slot A**: Kept as stock Android safety net
- **Slot B**: Used for experiments

### Key Partitions

| Partition | Size | Slots | Description |
|-----------|------|-------|-------------|
| `splloader` | ~256K | No | SPL (first-stage bootloader) |
| `uboot_a/b` | 8 MB | A/B | LK / U-Boot (second-stage bootloader) |
| `boot_a/b` | 64 MB | A/B | Linux kernel (GKI) |
| `vendor_boot_a/b` | 100 MB | A/B | Vendor ramdisk + DTB + bootconfig |
| `init_boot_a/b` | 8 MB | A/B | Generic ramdisk with `/init` |
| `dtb_a/b` | — | A/B | Empty on this device! DTB is in vendor_boot |
| `dtbo_a/b` | — | A/B | Device Tree Blob Overlays |
| `vbmeta_a/b` | 2 MB | A/B | Main AVB metadata |
| `vbmeta_system_a/b` | — | A/B | AVB for system partition |
| `vbmeta_vendor_a/b` | — | A/B | AVB for vendor partition |
| `vbmeta_system_ext_a/b` | — | A/B | AVB for system_ext |
| `vbmeta_product_a/b` | — | A/B | AVB for product |
| `vbmeta_odm_a/b` | — | A/B | AVB for ODM |
| `super` | ~5 GB | **No** | Dynamic partition (dm-linear) — single, NOT A/B |
| `userdata` | ~50-110 GB | No | User data (ext4/f2fs) |
| `cache` | 64 MB | No | Cache (useful for diagnostics) |
| `miscdata` | — | No | Unlock token, USB mode config |
| `uboot_log` | — | No | Bootloader log (readable via BROM) |
| `cust` | — | No | Customization partition — repurposed for Ubuntu rootfs |

### super Partition

The `super` partition uses Android's **dm-linear** (dynamic partitions) and contains:

| Sub-partition | Filesystem | Contents |
|---------------|-----------|----------|
| `system` | erofs | Android framework, /system |
| `vendor` | erofs | HAL libraries, vendor binaries |
| `product` | erofs | Product-specific overlays |
| `odm` | erofs | ODM customizations |

**Critical**: `super` is NOT A/B slotted. Both slot A and slot B boot from the same `super` partition. This means:
- dm-verity must remain enabled (vbmeta flags bit 0 must be 0)
- Modifying system/vendor requires disabling hashtree verification AND re-signing, or using dm-verity flags carefully

---

## 8. Boot Images (GKI v4)

This device uses **Generic Kernel Image (GKI)** with boot header version 4. The kernel, ramdisks, and DTB are split across three images:

### boot_b (64 MB) — Kernel

Contains only the Linux kernel (~47 MB compressed). No ramdisk, no DTB.

### vendor_boot_b (100 MB) — Vendor Ramdisk + DTB

**vendor_boot v4 header layout** (Little-Endian):

```
Offset   Size    Field
──────   ────    ─────
0        8       Magic: "VNDRBOOT"
8        4       header_version: 4
12       4       page_size: 4096
16       4       kernel_addr
20       4       ramdisk_addr
24       4       vendor_ramdisk_size (e.g. 34,296,002 = ~32 MB)
28       2048    cmdline (includes console=ttyS1,921600n8)
2076     4       tags_addr
2080     16      name
2096     4       header_size
2100     4       dtb_size (e.g. 141,378)
2104     8       dtb_addr
2112     4       vendor_ramdisk_table_size (108)
2116     4       vendor_ramdisk_table_entry_num (1)
2120     4       vendor_ramdisk_table_entry_size
2124     4       bootconfig_size (53)
```

**On-disk layout** (all page-aligned to 4096 bytes):

```
Offset          Content
──────          ───────
0               Header (1 page = 4096 bytes)
4096            Vendor ramdisk (LZ4 compressed, ~32 MB)
~34,304,000     DTB
~34,447,360     Ramdisk table
~34,451,456     Bootconfig
```

**Bootconfig content**: `androidboot.hardware=serenity\nandroidboot.dtbo_idx=0`

### init_boot_b (8 MB) — Generic Ramdisk

Contains Android's `/init` binary (3.7 MB, statically linked aarch64) and `snapuserd_ramdisk`.

**Critical GKI insight**: In GKI v4, the ramdisks are overlaid in this order:
1. vendor_boot ramdisk (bottom layer)
2. init_boot ramdisk (top layer — **wins for duplicate files**)

This means `/init` from init_boot always overwrites any `/init` in vendor_boot. Early attempts to place a custom init in vendor_boot failed because Android's init from init_boot was always laid on top.

### DTB Location

The DTB is inside `vendor_boot`, NOT in the `dtb` partition (which is empty on this device). The DTB declares `compatible = "sprd,ums9230"`.

---

## 9. vbmeta & AVB 2.0

### vbmeta Flag Bits

Flags are at offset 120 in the vbmeta image, stored as **big-endian uint32**:

| Value | Bit 0 (HASHTREE) | Bit 1 (VERIFY) | Effect |
|-------|------------------|-----------------|--------|
| 0     | enabled          | enabled         | Stock: everything verified |
| 1     | **DISABLED**     | enabled         | dm-verity OFF, signature checks ON |
| 2     | enabled          | **DISABLED**    | dm-verity ON, signatures NOT checked ← **correct for modified images** |
| 3     | **DISABLED**     | **DISABLED**    | Everything OFF ← **BREAKS ANDROID BOOT** |

### Why flags=3 Breaks Android

With `HASHTREE_DISABLED` (bit 0), dm-verity is not set up for the `super` partition. Since `system`, `vendor`, `product`, and `odm` are erofs filesystems that require dm-verity for mounting, Android cannot access any of its system files and fails to boot.

### Why flags=2 Is Correct

With only `VERIFICATION_DISABLED` (bit 1):
- Modified boot images (vendor_boot, init_boot) are not hash-checked → custom images boot
- dm-verity/hashtree remains active → super partition mounts correctly → Android services work
- This is the correct setting for ANY modified image scenario

### Creating a Modified vbmeta

```bash
# Copy stock vbmeta
cp vbmeta_stock.bin vbmeta_flags2.bin

# Set flags=2 at offset 120 (big-endian)
printf '\x00\x00\x00\x02' | dd of=vbmeta_flags2.bin bs=1 seek=120 conv=notrunc

# Verify
python3 -c "
import struct
data = open('vbmeta_flags2.bin','rb').read()
flags = struct.unpack_from('>I', data, 120)[0]
print(f'Flags: {flags} (expected: 2)')
"
```

### Sub-vbmetas

The main `vbmeta` delegates to sub-vbmetas for individual partitions:

| Sub-vbmeta | Covers |
|------------|--------|
| `vbmeta_system` | system partition hash tree |
| `vbmeta_vendor` | vendor partition hash tree |
| `vbmeta_system_ext` | system_ext hash tree |
| `vbmeta_product` | product hash tree |
| `vbmeta_odm` | ODM hash tree |

Sub-vbmetas should retain their **original flags** (flags=0) with intact hashtree descriptors. Only the main `vbmeta` needs flags=2.

### Padlock Behavior

| vbmeta state | Boot screen |
|--------------|-------------|
| flags=0 (stock) | Open padlock (if unlocked) |
| flags=2 (verify off) | No padlock shown |
| flags=3 (all off) | No padlock shown, Android won't boot |

### Full Disable (for custom kernels without super)

When booting a custom kernel that doesn't need the super partition at all, ALL vbmeta partitions can be overwritten with a minimal disabled header:

```bash
# Create minimal AVB0 header with flags=3
python3 -c "
header = b'AVB0' + b'\x00' * 116 + b'\x00\x00\x00\x03' + b'\x00' * (4096 - 124)
open('vbmeta_disabled.bin', 'wb').write(header[:4096])
"

# Flash to ALL vbmeta partitions on slot B:
fastboot flash vbmeta_b vbmeta_disabled.bin
fastboot flash vbmeta_system_b vbmeta_disabled.bin
fastboot flash vbmeta_vendor_b vbmeta_disabled.bin
fastboot flash vbmeta_system_ext_b vbmeta_disabled.bin
fastboot flash vbmeta_product_b vbmeta_disabled.bin
fastboot flash vbmeta_odm_b vbmeta_disabled.bin
# Also: avbmeta_rs_b if present
```

---

## 10. Vendor Ramdisk

### Compression

The vendor ramdisk uses **LZ4 compression** (NOT gzip). This is important for unpacking and repacking:

```bash
# Unpack
lz4 -d vendor_ramdisk.lz4 vendor_ramdisk.cpio

# Extract cpio
mkdir vendor_rd && cd vendor_rd
cpio -idm < ../vendor_ramdisk.cpio

# Repack (gzip also works — kernel supports both)
find . | sort | cpio --quiet -o -H newc | lz4 > ../vendor_ramdisk_new.lz4
# OR
find . | sort | cpio --quiet -o -H newc | gzip > ../vendor_ramdisk_new.gz
```

### Contents (829 files)

| Path | Contents |
|------|----------|
| `lib/modules/` | 168 kernel modules (.ko files) |
| `first_stage_ramdisk/fstab.serenity` | First-stage mount table (mounts super sub-partitions) |
| `system/bin/` | Recovery binaries |
| `*.rc` files | init.recovery.common.rc, init.recovery.serenity.rc, etc. |

### Module Loading

Two module lists exist:

| File | Modules | When Used |
|------|---------|-----------|
| `modules.load` | 67 | Normal Android boot |
| `modules.load.recovery` | 168 | Recovery mode (**complete list**) |

**Critical discovery**: USB hardware modules (`musb_hdrc`, `musb_sprd`, `phy-sprd-*`, `extcon-usb-gpio`, `sprd_usbpinmux`, `sc27xx_typec`) are in `modules.load.recovery` but NOT in `modules.load`. However, they are **built-in to the kernel** — loading them as modules crashes the USB stack (double registration).

The `modules.load.recovery` list is the correct basis for a custom Linux init that needs all hardware initialized.

### fstab.serenity Location

```
first_stage_ramdisk/fstab.serenity
```

NOT the typical `etc/fstab.*` location. Contains 11 mount entries for the super sub-partitions. Emptying this file prevents Android first-stage init from mounting system/vendor/product/odm.

### No Busybox

The stock vendor ramdisk does **not** contain busybox. A statically compiled aarch64 busybox must be added for shell access in custom Linux boots.

---

## 11. USB Stack

### Architecture

```
Hardware (built-in to kernel):
  USB PHY (phy-sprd-*) → MUSB controller (musb-hdrc) → USB Gadget framework

Software (configfs):
  /config/usb_gadget/g1/ → functions/ → configs/b.1/ → UDC binding
```

### Kernel Config (all built-in, no modules needed)

```
CONFIG_USB_GADGET=y
CONFIG_USB_CONFIGFS=y
CONFIG_USB_F_ACM=y
CONFIG_USB_U_SERIAL=y
CONFIG_USB_LIBCOMPOSITE=y
```

### UDC Name

```
musb-hdrc.1.auto
```

**Warning**: Some documentation and properties suggest `.0.auto` — this is WRONG. Verified via ADB on running Android: `ls /sys/class/udc/` → `musb-hdrc.1.auto`.

### USB Gadget Functions

| Function | VID:PID | Host Driver | Notes |
|----------|---------|-------------|-------|
| `vser.gs7` (Sprd Virtual Serial) | `1782:4d00` | Requires `modprobe usbserial vendor=0x1782 product=0x4d00` | Proprietary Spreadtrum protocol |
| `acm.gs0` (CDC ACM) | `1d6b:0104` | Standard `/dev/ttyACM0` — auto-detected | **Recommended for Linux** |
| Sprd Gadget Serial (general) | `1782:4023` | Various | Appears during configfs setup |

### Configfs CDC-ACM Setup (Recommended)

```c
// In custom init:
mount("none", "/config", "configfs", 0, 0);
mkdir("/config/usb_gadget/g1");
write("/config/usb_gadget/g1/idVendor", "0x1d6b");  // Linux Foundation
write("/config/usb_gadget/g1/idProduct", "0x0104");  // CDC ACM
mkdir("/config/usb_gadget/g1/strings/0x409");
write("/config/usb_gadget/g1/strings/0x409/manufacturer", "Serenity");
write("/config/usb_gadget/g1/strings/0x409/product", "Linux Console");
mkdir("/config/usb_gadget/g1/functions/acm.gs0");
mkdir("/config/usb_gadget/g1/configs/b.1");
symlink("/config/usb_gadget/g1/functions/acm.gs0",
        "/config/usb_gadget/g1/configs/b.1/f1");
write("/config/usb_gadget/g1/UDC", "musb-hdrc.1.auto");
```

### Android's Original configfs Sequence (vser.gs7)

From `init.recovery.common.rc`:
```
mkdir /config/usb_gadget/g1/functions/vser.gs7
symlink /config/usb_gadget/g1/functions/vser.gs7 /config/usb_gadget/g1/configs/b.1/f1
write /config/usb_gadget/g1/UDC ${sys.usb.controller}  # = musb-hdrc.1.auto
```

Module dependencies for vser: `usb_f_vser.ko` → `sipc-core.ko`; `sprd_usb_f_serial.ko` → both.

### Host udev Rule (for Sprd Virtual Serial)

```bash
# /etc/udev/rules.d/99-sprd-serial.rules
ACTION=="add", SUBSYSTEM=="usb", ATTR{idVendor}=="1782", ATTR{idProduct}=="4d00", \
  RUN+="/sbin/modprobe usbserial vendor=0x1782 product=0x4d00"
```

Not needed for CDC-ACM — the host kernel detects it automatically.

---

## 12. Kernel Configuration

### Stock Kernel

- **GKI-based**: Header version 4, shipped in `boot_b` (64 MB, kernel ~47 MB)
- **IKCONFIG**: Present and extractable (188 KB)
- **Console**: `ttyS1,921600n8` (suppressed by LK in normal mode)
- **Architecture**: aarch64

### Key Config Entries

```
# USB (all built-in)
CONFIG_USB_GADGET=y
CONFIG_USB_CONFIGFS=y
CONFIG_USB_F_ACM=y
CONFIG_USB_U_SERIAL=y
CONFIG_USB_LIBCOMPOSITE=y

# Storage
# eMMC drivers in modules: sdhci-sprd.ko, mmc_hsq.ko, mmc_swcq.ko
```

### Mainline Kernel

Successfully boots based on the `ums9230-mainline` fork (Otto Pflüger's line):

- All 8 CPU cores active
- eMMC functional
- Display works via `simple-framebuffer` (logobuffer at `0x9e000000`)
- CMDLINE_FORCE used to override LK's console suppression

The stock DTB declares `compatible = "sprd,ums9230"` and is extracted from vendor_boot (the `dtb` partition is empty).

---

## 13. miscdata Partition

### USB Mode Control

At offset `0x2780` in miscdata, a text field controls LK's USB behavior:

| Value | Effect |
|-------|--------|
| `off` | LK disables USB entirely ("usb is configed as off by miscdata") |
| `uart` | USB cable becomes UART console (usb2spuart mode) |
| `jtag` | JTAG mode |
| `jtag_apwdg` | JTAG with AP watchdog |
| (empty/zeros) | Normal USB mode ← **default for Linux work** |

If USB isn't working from LK, check this field:

```bash
# Read miscdata via BROM
r miscdata miscdata.bin

# Check USB mode field
dd if=miscdata.bin bs=1 skip=$((0x2780)) count=16 | xxd
# "off" = USB disabled; all zeros = normal
```

To clear:

```bash
# Zero out the USB mode field
python3 -c "
data = bytearray(open('miscdata.bin','rb').read())
data[0x2780:0x2780+16] = b'\x00' * 16
open('miscdata_cleared.bin','wb').write(data)
"
# Flash back
w miscdata miscdata_cleared.bin   # via spd_dump
# OR
fastboot flash miscdata miscdata_cleared.bin
```

### Unlock Token

At offset `0x2000` in miscdata. Written by the unlock procedure. Survives factory resets and ROM flashes. Once set, `fastboot getvar unlocked` returns `yes`.

---

## 14. Display & Framebuffer

### Stock Android

Display is driven by `sprd-drm.ko`, which lives on the `super` partition (in the vendor filesystem). Without mounting super, there is no `/dev/fb0` or DRM device.

### Mainline Linux

Display works via **simple-framebuffer** using the logobuffer left by LK:

- LK initializes the display hardware and writes the boot logo
- The framebuffer memory is at `0x9e000000`
- A `simple-framebuffer` DTS node (based on the Reeder DTS template) allows the Mainline kernel to reuse this initialized framebuffer
- `CONFIG_SIMPLEDRM=y` + `CONFIG_FRAMEBUFFER_CONSOLE=y` enables kernel text on screen

This approach works because LK always initializes the display, regardless of what kernel is booted.

---

## 15. Booting Linux

### What Works (as of September 2026)

- ✅ Custom Mainline kernel boots (all 8 CPUs, eMMC)
- ✅ Ubuntu 24.04 Server (aarch64, debootstrap) boots to login prompt
- ✅ Ubuntu rootfs runs from the `cust` partition
- ✅ Display shows kernel text via simple-framebuffer
- ✅ Both warm and cold boot are reliable

### Strategy: Slot B Experiments, Slot A Safety

```
Slot A: Stock Android (untouched safety net)
Slot B: Custom kernel + Ubuntu
```

Switch between them:
```bash
# Boot Linux (slot B)
fastboot set_active b
fastboot reboot

# Boot Android (slot A)
fastboot set_active a
fastboot reboot
```

### Key Lessons for Custom Init

1. **Use init_boot, not vendor_boot** for your custom `/init` — GKI overlays init_boot on top
2. **Don't load USB hardware modules** — they're built-in; loading them crashes USB
3. **Use CDC-ACM** (`acm.gs0`) instead of Sprd's proprietary `vser.gs7` — auto-detected by hosts
4. **UDC name is `musb-hdrc.1.auto`** (not `.0.auto`)
5. **Mount configfs correctly**: `mount("none", "/config", "configfs", 0, 0)` — not `mount("configfs", ...)`
6. **eMMC requires module loading**: `sdhci-sprd.ko`, `mmc_hsq.ko`, `mmc_swcq.ko` must be loaded for block device access
7. **devtmpfs doesn't auto-populate** without ueventd/mdev — block device nodes for eMMC won't appear without a device manager

### Diagnostic Approach

Without UART and with unreliable USB-serial, diagnostics relied on:

1. **uboot_log partition**: Contains LK boot log, readable via BROM after any boot attempt
2. **cache partition** (64 MB): Writable from init for log dumping, readable via BROM
3. **USB timing analysis**: Different USB connect/disconnect patterns indicated different init stages
4. **Display output**: With simple-framebuffer, kernel panics and boot messages are visible on screen

---

## 16. Lessons Learned

### Critical Mistakes & Fixes

| Mistake | Impact | Fix |
|---------|--------|-----|
| Used generic `ums9230_universal_unlock_EMMC` FDLs | Weeks of failed attempts | Switch to `linux_ums9230e_Tecno_KL4` (T7250-specific) |
| Put custom init in vendor_boot | Init never ran (overwritten by init_boot) | Put custom init in init_boot |
| Set vbmeta flags=3 | Android couldn't boot (dm-verity broken) | Use flags=2 (verify off, hashtree on) |
| Loaded USB hardware modules | USB stack crashed | Don't load built-in modules |
| Used UDC name `.0.auto` | Gadget never bound | Correct name is `.1.auto` |
| Used BROM for everything | Slow, complex workflow | Fastboot works after unlock! |
| Assumed vendor ramdisk was gzip | Extraction failed | It's LZ4 |
| Assumed DTB is in dtb partition | Empty file | DTB is in vendor_boot |

### Architecture Insights

1. **GKI changes everything**: The kernel, vendor ramdisk, generic ramdisk, and DTB are in separate images. Understanding the overlay order is essential.
2. **LK is actively hostile**: It suppresses console output, may disable USB, and enforces AVB even without real secure boot.
3. **Budget Unisoc devices have paper-thin security**: eFuses not burned, software-only checks, multiple CVEs in BROM.
4. **The super partition is sacred**: It's shared across both A/B slots. dm-verity must stay enabled or nothing on system/vendor works.
5. **Fastboot is the fast path**: After BROM-based unlock, all subsequent flashing should use fastboot.

---

## 17. Tools & References

### Tools Used

| Tool | Purpose | Source |
|------|---------|--------|
| `spd_dump` (TomKing062) | BROM exploit, partition read/write | [GitHub](https://github.com/TomKing062/CVE-2022-38694_unlock_bootloader) |
| `linux_ums9230e_Tecno_KL4` | UMS9230E-specific FDL binaries | XDA Forums |
| `fastboot` | Partition flashing (after unlock) | Android SDK Platform Tools |
| `adb` | Android debugging, file transfer | Android SDK Platform Tools |
| `lz4` | Ramdisk decompression | System package |
| `cpio` | Ramdisk archive manipulation | System package |
| `mkbootimg` / `unpack_bootimg` | Boot image creation/extraction | Android source |
| Ghidra | Binary analysis (SPL, FDL, LK) | [ghidra-sre.org](https://ghidra-sre.org) |

### Reference Projects

| Project | Relevance |
|---------|-----------|
| [rg-rotate-linux](https://github.com/nicman23/rg-rotate-linux) | Mainline Linux on UMS512/T618 (same sharkl5pro family) |
| [mu300-linux](https://github.com/nicman23/mu300-linux) | ZTE F50 (T760), USB-ACM+ECM configfs, Ubuntu+systemd |
| [pixel8-linux](https://github.com/nicman23/pixel8-linux) | configfs-ACM + mknod ttyGS0 approach |
| [e5-linux](https://github.com/nicman23/e5-linux) | Rongyue E5 (UMS9621), same approach as mu300 |
| [ums9230-mainline](https://github.com/nicman23/ums9230-mainline) | Mainline kernel fork for UMS9230 — basis for our kernel |
| [Otto Pflüger's work](https://github.com/nicman23/ums9230-mainline) | Original ums9230 mainline effort |

### Vulnerability References

| CVE | Description | Paper |
|-----|-------------|-------|
| CVE-2022-38694 | BROM code execution via unsigned FDL loading | [NCC Group](https://www.nccgroup.com/research/there-s-another-hole-in-your-soc-unisoc-rom-vulnerabilities/) |
| CVE-2022-38691 | Type-0 certificate bypasses eFuse key check | [TomKing062](https://github.com/TomKing062/CVE-2022-38691_38692) |
| CVE-2022-38692 | Related BROM vulnerability | [TomKing062](https://github.com/TomKing062/CVE-2022-38691_38692) |

### Additional Analysis

| Resource | Description |
|----------|-------------|
| [BinaryChunk CVE Analysis](https://mutur4.github.io/2026/03/19/cve-2022-38694.html) | Detailed CVE-2022-38694 technical analysis |
| [TheGammaSqueeze/UnisocBypass](https://github.com/TheGammaSqueeze/UnisocBypass) | Alternative Unisoc exploit approach |
| [crackerjacques/ums512_spl](https://github.com/crackerjacques/ums512_spl) | SPL analysis for UMS512 |

---

## Appendix A: Quick Reference Card

### Enter BROM Mode
```
Power off → Hold VOL_UP + VOL_DOWN → Insert USB
```

### Enter Fastboot Mode
```
Power off → Hold VOL_DOWN + Power → Release at FASTBOOT screen
```

### Flash Custom Image (Fastboot)
```bash
fastboot flash vendor_boot_b custom_vendor_boot.bin
fastboot flash vbmeta_b vbmeta_flags2.bin
fastboot reboot
```

### Flash Custom Image (BROM/spd_dump)
```bash
# At FDL2> prompt:
w vendor_boot_b custom_vendor_boot.bin
w vbmeta_b vbmeta_flags2.bin
reset
```

### Restore Stock Android
```bash
# Via Fastboot:
cd serenity_global_images_A15.0.20.0.VGWMIXM_15.0
./flash_all.sh
fastboot reboot
```

### Read Boot Log After Failed Boot
```bash
# Enter BROM mode, connect spd_dump, then:
r uboot_log uboot_log.bin
strings uboot_log.bin | less
```

---

*Documentation by EberhartLeberhart — built from hands-on reverse engineering of two Xiaomi Redmi A5 devices.*  
*No vendor documentation was used. No animals were harmed. Several USB cables were.*
