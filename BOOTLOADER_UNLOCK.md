# Bootloader Unlock: The Complete Journey

## Xiaomi Redmi A5 (serenity) — UMS9230E / Unisoc T7250

> **This document tells the full story** of unlocking the bootloader on the Xiaomi Redmi A5, including every dead end, every cryptic error message, and the hours of reverse engineering that led to the solution. It exists so that nobody else has to repeat our mistakes.
>
> If you just want the working procedure, skip to [Chapter 8: The Working Procedure](#8-the-working-procedure-step-by-step).

---

## Table of Contents

1. [The Device](#1-the-device)
2. [Why This Is Hard](#2-why-this-is-hard)
3. [Attempt 1: The Wrong Tool (Ilyas spreadtrum_flash)](#3-attempt-1-the-wrong-tool-ilyas-spreadtrum_flash)
4. [Attempt 2: The Wrong Package (ums9230 universal)](#4-attempt-2-the-wrong-package-ums9230-universal)
5. [Attempt 3: Xiaomi's Own FDLs](#5-attempt-3-xiaomis-own-fdls)
6. [Attempt 4: gen_spl-unlock (The SPL Workaround)](#6-attempt-4-gen_spl-unlock-the-spl-workaround)
7. [The Breakthrough: Understanding What Went Wrong](#7-the-breakthrough-understanding-what-went-wrong)
8. [The Working Procedure (Step-by-Step)](#8-the-working-procedure-step-by-step)
9. [Verification & Post-Unlock](#9-verification--post-unlock)
10. [Device Revival: When Things Go Wrong](#10-device-revival-when-things-go-wrong)
11. [Stock Restore](#11-stock-restore)
12. [Security Analysis: Why This Works](#12-security-analysis-why-this-works)
13. [Lessons Learned](#13-lessons-learned)

---

## 1. The Device

| Property | Value |
|----------|-------|
| **Model** | Xiaomi Redmi A5 (25028RN03A) |
| **Codename** | `serenity` |
| **Variant** | Global (VGWMIXM) |
| **SoC** | Unisoc T7250 / UMS9230**E** |
| **CPU** | 2x Cortex-A75 + 6x Cortex-A55 |
| **RAM** | 3 GB |
| **Storage** | eMMC (64 GB or 128 GB variants) |
| **Android** | 15 Go Edition, Build `A15.0.20.0.VGWMIXM` |
| **Boot scheme** | A/B slots, GKI v4, AVB 2.0 |
| **BROM exploit** | CVE-2022-38694, exec_addr `0x65015f08` |

### The "E" in UMS9230E

This is the single most important detail in this entire document:

**T7250 = T615 = UMS9230E**.  
**T606 = UMS9230** (no "E").

They are **different SoCs** with **different DRAM initialization sequences**. Every tool, FDL binary, and unlock package that says "ums9230" without the "E" is built for the T606 and **will not work** on our device. This distinction cost us days of debugging before we understood it.

---

## 2. Why This Is Hard

On most Android phones, you unlock the bootloader with one of:

```bash
fastboot oem unlock          # Standard Android
fastboot flashing unlock     # Pixel-style
```

Or you use the manufacturer's official tool (Mi Unlock for Xiaomi).

**None of these work on the Redmi A5:**

| Method | What Happens |
|--------|-------------|
| `fastboot oem unlock` | Returns `Err:0xffffffff` |
| `fastboot flashing unlock` | Not supported |
| Mi Unlock Tool (Windows) | "This device is not supported" |
| ADB/Fastboot unlock | No Unisoc/Spreadtrum support |

Xiaomi and Unisoc did not implement any standard unlock mechanism for this device. The **only** way in is through a BROM (Boot ROM) vulnerability — CVE-2022-38694, discovered by TomKing062 — that lets you execute arbitrary code at the earliest stage of the boot chain, before any software locks take effect.

### What BROM Mode Is

Every Unisoc SoC has a Boot ROM burned into silicon. It's the very first code that runs when the chip powers on. In normal boot, BROM loads the SPL (Secondary Program Loader), which loads the rest of the boot chain. But BROM also has a **Download Mode** — a USB protocol intended for factory provisioning — that lets a host computer send code to execute. CVE-2022-38694 exploits a flaw in this protocol to bypass signature checks and run unsigned code.

### The Boot Chain

```
BROM (silicon) → SPL → SML → TrustOS → LK (Little Kernel) → Linux Kernel
                  ↑
            CVE-2022-38694 enters here
            (loads FDL1 instead of SPL)
```

The exploit loads an **FDL1** (Flash Download Layer 1) binary that initializes DRAM and eMMC hardware, then loads **FDL2** which provides full partition access. FDL2 can read/write any partition, including `miscdata` where the unlock token lives.

---

## 3. Attempt 1: The Wrong Tool (Ilyas spreadtrum_flash)

### What We Tried

Our first attempt used [Ilyas's spreadtrum_flash](https://github.com/nickelback7/spreadtrum_flash) tool, which implements the Spreadtrum/Unisoc download protocol. We even patched `spd_dump.c` to fix compilation issues.

### What Went Wrong

The BROM protocol requires a very specific sequence:

1. Send handshake (`BSL_CMD_CONNECT`)
2. Send the FDL1 binary in one transfer
3. Execute FDL1
4. FDL1 initializes hardware, then accepts FDL2
5. Execute FDL2

Ilyas's tool attempted to send **two** binaries after the initial handshake, but the BootROM protocol only allows **one** `send_file` before the first `exec`. The second send was silently rejected.

### The Fix

Switch to TomKing062's `spd_dump` from the [CVE-2022-38694 repository](https://github.com/nickelback7/CVE-2022-38694_unlock_bootloader), which correctly implements the exploit sequence. It handles the send→exec→send→exec chain properly because FDL1 re-opens the USB channel after taking over from BROM.

```bash
# Build spd_dump (requires libusb-1.0-dev)
git clone https://github.com/nickelback7/CVE-2022-38694_unlock_bootloader.git
cd CVE-2022-38694_unlock_bootloader/spreadtrum_flash
make
```

> **Note:** On a Windows-only system, TomKing062's Windows tool with Spreadtrum USB drivers works. But on Linux, building `spd_dump` from source is the path.

---

## 4. Attempt 2: The Wrong Package (ums9230 universal)

### What We Tried

With the correct tool (`spd_dump`) in hand, we downloaded `ums9230_universal_unlock_EMMC` package (v1.72) — the most commonly referenced package on XDA for Unisoc UMS9230 devices.

```bash
sudo ./spd_dump --wait 300 \
  exec_addr 0x65015f08 \
  fdl fdl1-dl.bin 0x65000800 \
  fdl fdl2-dl.bin 0x9efffe00 \
  exec
```

### What Went Wrong

#### Stage 1 — FDL1: CHECK_BAUD FAIL

The universal `fdl1-dl.bin` loaded and BROM executed it, but then:

```
CHECK_BAUD FAIL
(timeout waiting for FDL1 response)
```

FDL1's first job is to initialize DRAM. The universal FDL1 contains DRAM init code for the **T606 (UMS9230)**, which has different DRAM controller registers and timing parameters than our **T7250 (UMS9230E)**. FDL1 hung during DRAM initialization and never responded to the baud-rate negotiation.

We also tried:
- `rawdata 1` mode → same result
- `exec_addr 0x65015f48` (alternate entry point) → same result

**The universal FDL1 simply cannot initialize our DRAM.**

### What We Learned

At this point, we didn't yet understand the UMS9230 vs. UMS9230E distinction. We just knew the universal FDLs didn't work and we needed device-specific ones.

---

## 5. Attempt 3: Xiaomi's Own FDLs

### The Idea

If the universal FDLs don't work, maybe Xiaomi's own FDL binaries — extracted from the official Fastboot ROM — would. After all, they were built specifically for this hardware.

### Getting the Firmware

Xiaomi distributes firmware as Fastboot ROMs (`.tgz` archives), not Unisoc `.pac` packages. We downloaded two ROMs from [mifirm.net](https://mifirm.net):

1. **India ROM**: `A15.0.2.0.VGWINXM` (earlier build, India variant)
2. **Global ROM**: `A15.0.20.0.VGWMIXM` (our device's exact firmware)

Both contain FDL binaries under `images/`:
- `fdl1-sign.bin` — FDL1 (signed, DHTB-wrapped)
- `lk-fdl2-sign.bin` — FDL2 (signed, DHTB-wrapped, based on LK)

### Stage 1 — FDL1: SUCCESS!

Using Xiaomi's device-specific FDL1 with `spd_dump`:

```bash
sudo ./spd_dump --wait 300 \
  exec_addr 0x65015f08 \
  fdl images/fdl1-sign.bin 0x65000800 \
  fdl images/lk-fdl2-sign.bin 0x9efffe00 \
  exec
```

FDL1 loaded, initialized DRAM correctly, and responded:

```
Spreadtrum Boot Block version 1.1
```

**The CVE exploit worked. FDL1 was running.** This was the first real breakthrough.

### Stage 2 — FDL2: 0x00fe

FDL1 accepted FDL2 and executed it (`EXEC FDL2` succeeded). But then:

```
FDL2> r miscdata miscdata.bin
Response: 0x00fe
```

**Every single command returned `0x00fe`**. Verbose output showed:

```
FDL2 > (prompt ready)
Partition table not available
Flashing is not allowed for Protected Partitions
```

### The FDL2 Dead-End Matrix

We tried every FDL2 we could find:

| FDL2 Binary | Source | Result |
|-------------|--------|--------|
| `lk-fdl2-sign.bin` | India ROM | EXEC OK → `0x00fe` on all commands |
| `lk-fdl2-sign.bin` | Global ROM | EXEC OK → `0x00fe` on all commands |
| `fdl2-dl.bin` | Universal (ums9230) | Timeout during send (FDL1 rejects it) |
| `fdl2-cboot.bin` | Universal (unsigned) | Timeout during send (FDL1 rejects it) |
| `fdl2-dl.bin` (blk_size 528) | Universal | Response `0x008b` instead of `0x00fe` |

### Why Xiaomi's FDL2 Returns 0x00fe

We later found (through Ghidra analysis) that Xiaomi's `lk-fdl2-sign.bin` contains a deliberate check. At offset `0x01516c` in the raw binary:

```arm
mov w0, #0xfe
```

This is near the string `"Flashing is not allowed for Protected Partitions"`. Xiaomi's FDL2 is specifically programmed to **refuse partition operations** when loaded through the download protocol on this device. It's an anti-unlock measure.

### Why Unsigned FDL2s Are Rejected

When we tried loading unsigned FDL2 binaries (like `fdl2-cboot.bin` from the universal package):

- CVE-2022-38694 bypasses BROM → FDL1 signature check ✅
- But FDL1 → FDL2 signature verification **remains active** ❌

The CVE exploit only defeats the first link in the chain (BROM loading FDL1). FDL1 still verifies the DHTB signature on FDL2 before executing it. Without a valid signature matching FDL1's embedded key, FDL2 is silently rejected (timeout during send, as FDL1 drops the data).

### What We Learned

We were stuck: Xiaomi's signed FDL2 runs but refuses to do anything useful. Unsigned FDL2s are rejected by FDL1's signature check. The universal package's FDL2 doesn't even initialize on our hardware.

---

## 6. Attempt 4: gen_spl-unlock (The SPL Workaround)

### The Idea

TomKing062's repository includes a tool called `gen_spl-unlock` that takes a different approach entirely. Instead of using FDL1+FDL2, it:

1. Takes the device's own SPL (`u-boot-spl-16k-emmc-sign.bin`)
2. Patches out the signature-verification calls (replaces `BL verify` with NOPs)
3. Adds code to write the unlock token directly to `miscdata`
4. The patched SPL is loaded as "FDL1" via the BROM exploit

This bypasses the FDL2 problem entirely — the patched SPL writes the unlock token itself.

### Building spl-unlock.bin

```bash
# Extract SPL from Xiaomi ROM
cd ~/redmi-unlock-work

# Generate patched SPL
python3 gen_spl-unlock.py images/u-boot-spl-16k-emmc-sign.bin
# Output: spl-unlock.bin
```

The script finds the `BL verify` calls (5 of them) and replaces each with 14 NOP instructions. Then it injects the token-write payload.

### What Went Wrong

We tried three SPL variants:

| SPL Source | Result |
|------------|--------|
| India eMMC SPL | "device removed, exiting…" |
| India generic SPL | "device removed, exiting…" |
| Global eMMC SPL | "device removed, exiting…" |

In all cases, `spd_dump` reported `"device removed, exiting…"` — the USB connection dropped. After rebooting into fastboot:

```bash
fastboot getvar unlocked
# unlocked: no
```

**The token was never written.** The patched SPL started executing but crashed before reaching the miscdata write.

### Why It Crashed

The SPL's startup sequence is:

1. CPU initialization
2. DRAM initialization (SoC-specific registers, timing, training)
3. eMMC initialization
4. Load and verify the next boot stage
5. (Our injected code: write unlock token to miscdata)

Step 2 was the problem. The SPL contains hardcoded register addresses and timing values for DRAM initialization that are specific to the SoC variant. Our India/Global SPL was compiled for the **exact same hardware** (T7250), but the `gen_spl-unlock` patch modifies the binary in ways that may shift the DRAM init sequence or corrupt it, OR (more likely) the SPL crashes on an unrelated hardware-init step (clock tree, PMU setup) before ever reaching the miscdata code.

The USB disconnect was actually the crash itself — the SoC reset when the SPL hit an unhandled exception during early hardware initialization, which tore down the USB connection.

### Reviving the Device

After several failed spl-unlock attempts, the phone appeared dead — no BROM mode, no fastboot, no screen. **Don't panic.** The SPL-unlock only runs from RAM and **never writes to eMMC** until (if) it reaches the token-write code. Our eMMC was untouched.

**Recovery procedure:**
1. Disconnect USB
2. Open the back cover (it's a clip-on plastic back, no screws)
3. Disconnect the battery flex cable for 10 seconds
4. Reconnect battery
5. Try BROM mode again (VOL_UP + VOL_DOWN + USB)

The phone came back every time. The "brick" was just the SoC stuck in a crash loop that required a full power cycle to clear.

### GitHub Issue #327

At this point, we opened [Issue #327](https://github.com/nickelback7/CVE-2022-38694_unlock_bootloader/issues/327) on TomKing062's repository, documenting all our attempts with full verbose logs. This turned out to be productive — the community discussion helped confirm that the ums9230 vs. ums9230E distinction was the key.

---

## 7. The Breakthrough: Understanding What Went Wrong

### Ghidra Reverse Engineering

With every standard approach exhausted, we turned to reverse engineering. We installed [Ghidra](https://ghidra-sre.org/) and loaded the SPL, FDL1, and Xiaomi's FDL2 as AArch64 binaries.

**Key finding in SPL:**

The SPL binary contains **no reference to "miscdata"**. It doesn't know about the partition where the unlock token is stored. The `gen_spl-unlock` approach assumed the SPL would write the token itself, but the SPL has never done that — it only boots the next stage.

**Key finding in fdl2-cboot.bin (universal):**

The **unsigned** `fdl2-cboot.bin` from the universal package is the component that actually writes the unlock token. It's a stripped-down version of U-Boot that knows the `miscdata` partition layout and writes the token at offset `0x2000`.

**So the actual unlock chain should be:**

```
BROM → FDL1 (init DRAM/eMMC) → fdl2-cboot.bin (write token)
```

But we can't load `fdl2-cboot.bin` because FDL1 checks its signature, and it's unsigned.

### DHTB Header Deep Dive

We mapped out the DHTB container format used for all signed binaries:

```
Offset  Size       Field
──────  ─────      ──────────────────────────────────────────
0x000   4 bytes    Magic: "DHTB" (0x44 0x48 0x54 0x42)
0x004   4 bytes    (unknown / version)
0x008   32 bytes   SHA256 hash of payload
0x028   8 bytes    (padding)
0x030   4 bytes    Payload length
0x034   460 bytes  (reserved, zeros)
0x200   N bytes    Payload (the actual SPL/FDL/LK code)
0x200+N ~1.7 KB    SIMGHDR block (RSA-2048 public key + signature)
```

The verification is two-layered:

1. **SHA256 integrity hash** (at offset 0x08): BROM always checks this. If you modify the payload, you must recalculate this hash.
2. **RSA-2048 signature** (SIMGHDR block at end): Checked **only if the ROTPK (Root of Trust Public Key) hash is burned into eFuse**. If eFuses are unburned (all zeros), this check is skipped.

### The Key Insight: UMS9230 ≠ UMS9230E

While researching our GitHub issue, examining XDA threads, and comparing tool packages, the critical realization finally hit:

The package `ums9230_universal_unlock_EMMC` is built for **Unisoc T606 (UMS9230)**.  
Our device has **Unisoc T7250 (UMS9230E)**.

Despite the similar names, these SoCs have **different DRAM controllers**, different timing parameters, and different initialization sequences. Every FDL1 and SPL we'd been trying was compiled for the wrong DRAM hardware.

**This explains every single failure:**

| Failure | Explanation |
|---------|-------------|
| Universal FDL1: `CHECK_BAUD FAIL` | DRAM init for T606, hangs on T7250 |
| gen_spl-unlock crash | SPL compiled for T7250 but NOP patch corrupts init sequence (or: universal SPL for T606 DRAM) |
| 0x00fe from Xiaomi FDL2 | This was a *different* problem (Xiaomi's anti-unlock), but we conflated it with the FDL mismatch |

### Finding the Right Package

Searching XDA for "ums9230e" (with the E!) led to:

**`linux_ums9230e_Tecno_KL4.zip`** — a BROM unlock package built from a Tecno KL4 device, which uses the same UMS9230E SoC. It contains:

- `fdl1-dl.bin` — UMS9230E-specific FDL1 (correct DRAM init!)
- `fdl2-dl.bin` — UMS9230E-specific FDL2 (no Xiaomi anti-unlock code!)
- `spd_dump` — pre-built Linux binary
- `unlock_autopatch_9230.sh` — automated unlock script

The FDL binaries in this package initialize DRAM correctly for the T7250/UMS9230E platform, and the FDL2 is a generic Unisoc FDL2 (not Xiaomi's locked-down version), so it provides full partition access.

### Proof from the Commercial World

Additional validation: professional tools like **EFT Pro** and **E-GSM Tool** had already successfully unlocked the Redmi A5 (serenity) using their proprietary implementations. They use device-specific FDLs in their databases — confirming that the unlock works with the right binaries.

---

## 8. The Working Procedure (Step-by-Step)

> **Tested on:** Two Xiaomi Redmi A5 devices (25028RN03A, Global variant, 64 GB and 128 GB).
> **Host system:** Linux Mint (amd64). Should work on any Linux with libusb.

### 8.1 Prerequisites

1. **OEM Unlock enabled** in Developer Options:
   - Settings → About phone → Tap "Build number" 7 times → Back
   - Developer Options → Enable "OEM unlocking"
   - (This sets a flag that LK checks. Without it, LK may re-lock on reboot.)

2. **udev rule** for Spreadtrum USB devices:

```bash
# /etc/udev/rules.d/51-sprd.rules
SUBSYSTEM=="usb", ATTR{idVendor}=="1782", MODE="0666", GROUP="plugdev"
```

```bash
sudo udevadm control --reload-rules && sudo udevadm trigger
```

3. **Download the UMS9230E package:**

Download `linux_ums9230e_Tecno_KL4.zip` (search XDA for it) and extract:

```bash
mkdir -p ~/redmi-unlock-work/ums9230e
cd ~/redmi-unlock-work/ums9230e
unzip linux_ums9230e_Tecno_KL4.zip
chmod +x linux_ums9230e_Tecno_KL4/spd_dump
chmod +x linux_ums9230e_Tecno_KL4/unlock_autopatch_9230.sh
```

4. **ADB and Fastboot:**

```bash
sudo apt install adb fastboot
```

### 8.2 Enter BROM Download Mode

1. Power off the phone completely (hold Power for 10s, tap "Power off")
2. Wait 5 seconds after the screen goes black
3. Hold **VOL_UP + VOL_DOWN** simultaneously
4. While holding both buttons, plug in USB cable
5. Release buttons after ~2 seconds

The phone screen stays black (no visible indicator). On the host:

```bash
lsusb | grep 1782
# Expected: Bus XXX Device XXX: ID 1782:4d00 Spreadtrum Communications Inc.
```

If you see `1782:4d00`, the phone is in BROM mode.

### 8.3 Connect and Load FDLs

```bash
cd ~/redmi-unlock-work

sudo ./ums9230e/linux_ums9230e_Tecno_KL4/spd_dump --wait 300 \
  exec_addr 0x65015f08 \
  fdl ums9230e/linux_ums9230e_Tecno_KL4/fdl1-dl.bin 0x65000800 \
  fdl ums9230e/linux_ums9230e_Tecno_KL4/fdl2-dl.bin 0x9efffe00 \
  exec
```

**Expected output:**

```
Connection established (SPRD3)
Sending FDL1... OK
Executing FDL1...
Spreadtrum Boot Block version 1.1
Sending FDL2... OK
Executing FDL2... OK
FDL2>
```

**If you get `CHECK_BAUD FAIL`:** You're using the wrong FDL1 (probably from the ums9230 package without the "E"). Use the UMS9230E-specific fdl1-dl.bin.

**If FDL2 send times out:** FDL1 rejected the FDL2 (wrong package or wrong signature). Use the matching FDL2 from the same UMS9230E package.

### 8.4 Verify Partition Access

At the `FDL2>` prompt, verify you have full access:

```
FDL2> rawdata 0
```

You should see 81 partitions with A/B slots, including `miscdata`. If you get `0x00fe`, you're running Xiaomi's FDL2 instead of the UMS9230E package's FDL2.

### 8.5 Run the Unlock

**Option A — Automated (recommended):**

```bash
sudo ./ums9230e/linux_ums9230e_Tecno_KL4/unlock_autopatch_9230.sh
```

The script:
1. Connects to BROM
2. Loads FDL1 and FDL2
3. Reads current `miscdata` partition
4. Patches the unlock token at offset `0x2000`
5. Writes patched `miscdata` back
6. Backs up and restores SPL and U-Boot (safety measure)
7. Reboots the device

**Option B — Manual (if the script doesn't work):**

At the `FDL2>` prompt:

```
FDL2> r miscdata miscdata_backup.bin
FDL2> r splloader splloader_backup.bin
FDL2> r uboot_a uboot_a_backup.bin
FDL2> r uboot_b uboot_b_backup.bin
```

Then apply the unlock patch manually (the token is a specific byte pattern at offset `0x2000` in miscdata — the `unlock_autopatch_9230.sh` script source shows the exact bytes).

### 8.6 What You'll See

After the unlock completes and the device reboots:

1. **First boot takes longer** (30-60 seconds) — the bootloader performs a factory reset
2. **The screen shows an open padlock icon** with the text `LOCK FLAG IS UNLOCKED`
3. The device boots into Android setup wizard

> **Note:** All user data is wiped during the first boot after unlock (factory reset is mandatory, enforced by LK). Back up anything important before starting.

---

## 9. Verification & Post-Unlock

### Verify the Unlock

After the device boots, enter Fastboot mode:

1. Power off
2. Hold **VOL_DOWN + Power**
3. Release when "FASTBOOT" appears

Then on the host:

```bash
fastboot getvar unlocked
# unlocked: yes

fastboot getvar is-userspace
# is-userspace: no    (you're in bootloader fastboot, not fastbootd)
```

### The Unlock Token

The unlock token lives in the `miscdata` partition at offset `0x2000`.

**It survives:**
- Factory resets
- Full ROM re-flashes (via fastboot `flash_all.sh`)
- Slot switches (active slot A ↔ B)

**It does NOT survive:**
- Explicitly re-writing `miscdata` with a locked token (via BROM)
- A re-lock command (if one were supported — it isn't on this device)

Once unlocked, you can re-flash stock firmware any number of times and the device stays unlocked. This is important for experimentation — you can always restore stock.

### What Unlock Gives You

| Before Unlock | After Unlock |
|--------------|--------------|
| `fastboot flash` rejected for critical partitions | `fastboot flash` works for **all** partitions |
| `fastboot oem unlock` → `Err:0xffffffff` | `fastboot getvar unlocked` → `yes` |
| Modified images trigger boot abort | Modified images boot (with correct vbmeta flags) |
| BROM is the only way to flash | Both BROM and Fastboot work |

### Fastboot Mode (Your New Best Friend)

With the bootloader unlocked, Fastboot (VOL_DOWN + Power) becomes the primary flashing interface. It's dramatically faster and simpler than BROM:

```bash
# Flash a custom boot image:
fastboot flash boot_b custom_boot.img

# Flash vendor_boot:
fastboot flash vendor_boot_b custom_vendor_boot.img

# Set active slot:
fastboot set_active b

# Reboot:
fastboot reboot
```

BROM is still available as a last resort for unbricking, but for day-to-day work, Fastboot is the way.

### FRP Lock (Factory Reset Protection)

If you forget the Google account after a factory reset, FRP will prevent you from completing Android setup. **This is irrelevant for BROM unlock** — FRP only affects the Android userspace, not the bootloader or BROM. You can still enter BROM mode, Fastboot, and flash anything.

---

## 10. Device Revival: When Things Go Wrong

### The Phone Won't Respond (Appears Dead)

After a failed flash or a crashed FDL/SPL, the phone may appear completely dead: no screen, no USB device, no button response.

**This is almost certainly recoverable.** The BROM is in silicon — it cannot be overwritten. As long as the SoC isn't physically damaged, BROM mode is always available.

**Recovery steps:**

1. Disconnect USB cable
2. **Battery disconnect** (critical for UMS9230E — it needs a full power cycle):
   - Remove the back cover (clip-on, no tools needed)
   - Carefully disconnect the battery flex cable
   - Wait 10 seconds
   - Reconnect the battery flex cable
3. Try BROM mode: Hold VOL_UP + VOL_DOWN, plug in USB
4. Check `lsusb` for `1782:4d00`

If BROM mode works, you can restore the device completely (see [Stock Restore](#11-stock-restore)).

### eMMC Was Never Written

Important reassurance: When using `spd_dump` with FDL1 and FDL2, **nothing is written to eMMC until you explicitly send a write command** at the `FDL2>` prompt (or the unlock script does it). If FDL1 crashes during DRAM init, or FDL2 crashes, or you pull the cable — eMMC is untouched. Your data and Android installation are still intact.

The `gen_spl-unlock` approach is the same: the patched SPL runs entirely from RAM. If it crashes before writing the token (as it did in our case), eMMC is untouched.

### Phone Stuck in Bootloop

If you've modified boot images and the device bootloops:

1. Try Fastboot: Hold VOL_DOWN + Power during the bootloop
2. If Fastboot works: flash known-good images back
3. If Fastboot doesn't work: use BROM mode (VOL_UP + VOL_DOWN + USB, may need battery disconnect first)
4. Via BROM: restore SPL, UBoot, and vbmeta from stock ROM

### The A/B Slot Safety Net

The Redmi A5 has A/B partition slots. Keep one slot (e.g., slot A) with stock Android as a fallback:

```bash
# Check active slot:
fastboot getvar current-slot

# Switch to safe slot:
fastboot set_active a
fastboot reboot
```

Experiment only on slot B. If anything goes wrong, switch back to slot A for a working system.

---

## 11. Stock Restore

### Full Stock Restore via Fastboot

If you have an unlocked bootloader, the simplest restore is Xiaomi's official flash script:

1. Download the Global Fastboot ROM from [mifirm.net](https://mifirm.net):
   - Search for "serenity" or "Redmi A5"
   - Download `A15.0.20.0.VGWMIXM` (or latest version)

2. Extract the `.tgz`:

```bash
tar xzf serenity_global_images_*.tgz
cd serenity_global_images_*/
```

3. Run the flash script:

```bash
chmod +x flash_all.sh
./flash_all.sh
```

This flashes both slots (A and B) with stock images. The bootloader **stays unlocked** (the token in miscdata is preserved).

### Stock Restore via BROM (No Fastboot)

If the device can't reach Fastboot (e.g., corrupted bootloader), use BROM:

1. Enter BROM mode (VOL_UP + VOL_DOWN + USB)
2. Connect with spd_dump using the UMS9230E package
3. At the `FDL2>` prompt, write back stock images:

```
FDL2> w splloader splloader_stock.bin
FDL2> w uboot_a uboot_a_stock.bin
FDL2> w uboot_b uboot_b_stock.bin
FDL2> w boot_a boot_a_stock.bin
FDL2> w boot_b boot_b_stock.bin
FDL2> w vendor_boot_a vendor_boot_a_stock.bin
FDL2> w vendor_boot_b vendor_boot_b_stock.bin
FDL2> reset
```

Then use Fastboot to flash the rest.

### AVB / vbmeta Notes

If you've disabled AVB verification (vbmeta flags=2 or flags=3), you need to restore vbmeta images for stock boot:

- **flags=0**: Stock behavior (full verification). Required for unmodified Android.
- **flags=2**: Verification disabled, dm-verity hashtree enabled. Use this for modified boot/vendor_boot images.
- **flags=3**: Both verification AND hashtree disabled. **BREAKS ANDROID** — dm-verity is needed to mount the super partition (system/vendor/product/odm as erofs). Don't use flags=3 unless you know the images don't need dm-verity.

**Critical: Slot-specific vbmeta hashes**

Slot A's vbmeta contains hashes of slot A's images. Slot B's vbmeta contains hashes of slot B's images. You **cannot** copy slot A's vbmeta to slot B and expect verification to pass — the hashes won't match. If you need to restore verification on slot B, either:
- Flash slot B's original vbmeta (from a backup)
- Use flags=2 (disable verification)
- Re-flash the entire slot from the stock ROM

---

## 12. Security Analysis: Why This Works

### DHTB: The Container Format

Every signed binary in the Unisoc boot chain is wrapped in a DHTB container. The name likely stands for "Download Header / Trust Block."

```
+─────────────────────────────────────────────────+
│ 0x000  Magic: "DHTB" (4 bytes)                  │
│ 0x004  Version/Type (4 bytes)                    │
│ 0x008  SHA256 of payload (32 bytes)              │
│ 0x028  Padding (8 bytes)                         │
│ 0x030  Payload length (4 bytes)                  │
│ 0x034  Reserved (460 bytes, zeros)               │
├─────────────────────────────────────────────────┤
│ 0x200  Payload (SPL/FDL/LK binary)              │
│        ... N bytes ...                           │
├─────────────────────────────────────────────────┤
│ 0x200+N  SIMGHDR block (~1.7 KB)                │
│          - RSA-2048 public key                   │
│          - RSA-2048 signature                    │
│          - Key metadata                          │
+─────────────────────────────────────────────────+
```

### Two-Layer Verification

| Layer | What | Who Checks | When |
|-------|------|-----------|------|
| SHA256 | Integrity hash of payload bytes | BROM | **Always** — even with CVE exploit |
| RSA-2048 | Cryptographic signature (SIMGHDR) | BROM | **Only if ROTPK hash is burned in eFuse** |

### Why eFuses Aren't Burned

1. **Direct proof:** CVE-2022-38694 successfully loaded unsigned FDL code. If the eFuse ROTPK hash were burned, BROM would have checked the RSA signature and rejected our unsigned FDL. It didn't.

2. **Economic argument:** Burning eFuses during manufacturing is an extra production step that costs time and money. Budget devices (Redmi A-series, Tecno, Infinix) in the Unisoc ecosystem typically skip it.

3. **NCC Group research confirms:** "eFuse-based root key storage depends on proper burning; unburned devices skip validation entirely."

### What `secureboot=1` Actually Means

```
secureboot=1        ← Software flag set by LK bootloader
flash.locked=1      ← Software lock (cleared by unlock token)
verifiedbootstate=green  ← AVB verification result
```

These are **software-layer flags**, not hardware-backed security. LK sets `secureboot=1` unconditionally — it doesn't check the actual eFuse state. It's security theater on this class of device.

### CVE-2022-38691/38692: The Second Bypass

Even IF eFuses were burned (they aren't, but hypothetically), there's a second vulnerability: Type-0 certificates in the SIMGHDR skip the `memcmp` of the embedded public key hash against the eFuse value. This means arbitrary RSA keys can be injected even on devices with burned eFuses. A backup bypass for the backup bypass.

### Implications

- Custom SPL/FDL binaries need only valid DHTB SHA256 hashes — no RSA signature required
- Arbitrary code execution at BROM level via `exec_addr`
- All partitions readable and writable through BROM exploit
- Persistent modifications (custom SPL with patched DHTB hash) are theoretically possible

---

## 13. Lessons Learned

### 1. Read the SoC Designation Carefully

**T7250 = T615 = UMS9230E** (with "E")  
**T606 = UMS9230** (without "E")

This single letter cost us days. Check your SoC designation before downloading any tools or packages. On the Redmi A5:

```bash
# Via ADB (if Android is running):
adb shell getprop ro.board.platform
# Expected: ums9230e
```

### 2. Error Messages Are Not Always What They Seem

| What We Saw | What It Actually Meant |
|-------------|----------------------|
| `CHECK_BAUD FAIL` | DRAM init failed (wrong FDL1 for our SoC) |
| `0x00fe` | Xiaomi deliberately blocked partition access |
| `device removed, exiting…` | SPL crashed during hardware init |
| Phone appears dead | SoC stuck in crash loop, needs battery disconnect |

### 3. The Boot Chain Is a Series of Trust Handoffs

```
BROM → (CVE-2022-38694 breaks this link) → FDL1 → (signature check!) → FDL2
```

Breaking one link doesn't break the next. The BROM exploit lets us load custom FDL1, but FDL1 still verifies FDL2. You need FDLs from a **matching trust chain** — either both from the same package (like `linux_ums9230e_Tecno_KL4`), or the device's own signed binaries.

### 4. Always Have a Backup Path

- **BROM mode** is the ultimate safety net (hardwired in silicon, cannot be bricked)
- **A/B slots** let you experiment on one slot while keeping the other stock
- **Battery disconnect** recovers from most crash states
- **Back up critical partitions** before any modification (especially `miscdata`, `splloader`, `uboot`)

### 5. Ghidra Is Your Friend

When documentation doesn't exist (and for budget Unisoc devices, it rarely does), reverse engineering the binaries directly is the only way to understand what's actually happening. Key discoveries from our Ghidra sessions:

- SPL doesn't know about `miscdata` → explains why gen_spl-unlock failed
- Xiaomi's FDL2 has a hardcoded block at offset `0x01516c` → explains `0x00fe`
- DHTB header format → explains two-layer verification

### 6. Community Matters

Opening GitHub Issue #327 with full documentation of our attempts led to the breakthrough. Other users confirmed the UMS9230 vs. UMS9230E distinction. Open-source tools and community packages (especially from XDA) made the unlock possible without expensive commercial tools.

---

## Appendix A: File Locations

```
~/redmi-unlock-work/
├── ums9230e/
│   └── linux_ums9230e_Tecno_KL4/
│       ├── spd_dump              # BROM communication tool
│       ├── fdl1-dl.bin           # UMS9230E-specific FDL1
│       ├── fdl2-dl.bin           # UMS9230E-specific FDL2
│       └── unlock_autopatch_9230.sh  # Automated unlock script
├── serenity_global_images_A15.0.20.0.VGWMIXM_15.0/
│   └── images/
│       ├── fdl1-sign.bin         # Xiaomi's FDL1 (works but paired with blocked FDL2)
│       ├── lk-fdl2-sign.bin      # Xiaomi's FDL2 (blocks all operations)
│       └── ...                   # All stock images for flash_all.sh
├── miscdata_backup.bin           # Backup of miscdata before unlock
├── splloader_backup.bin          # Backup of splloader before unlock
└── ...
```

## Appendix B: Key Values & Addresses

| Item | Value |
|------|-------|
| BROM USB VID:PID | `1782:4d00` |
| BROM exec_addr | `0x65015f08` (alternate: `0x65015f48`) |
| FDL1 load address | `0x65000800` |
| FDL2 load address | `0x9efffe00` |
| Unlock token offset in miscdata | `0x2000` |
| DHTB magic | `0x44485442` ("DHTB") |
| DHTB SHA256 offset | `0x08` (32 bytes) |
| DHTB payload offset | `0x200` |
| BROM handshake | "SPRD3" |
| Xiaomi FDL2 block instruction | `mov w0, #0xfe` at offset `0x01516c` |
| Fastboot mode entry | VOL_DOWN + Power |
| BROM mode entry | VOL_UP + VOL_DOWN + USB |

## Appendix C: External Links

- [CVE-2022-38694 Unlock Tool (TomKing062)](https://github.com/nickelback7/CVE-2022-38694_unlock_bootloader) — BROM exploit tool and FDL packages
- [Our GitHub Issue #327](https://github.com/nickelback7/CVE-2022-38694_unlock_bootloader/issues/327) — Full documentation of the unlock journey
- [mifirm.net](https://mifirm.net) — Xiaomi Fastboot ROMs
- [Ghidra](https://ghidra-sre.org/) — NSA's reverse engineering framework (free, open-source)
- [Our project repository](https://github.com/EberhartLeberhart/Mainline-Linux-f-r-das-Xiaomi-Redmi-A5-serenity-) — Full documentation for running Mainline Linux on the Redmi A5

---

*This document is part of the [Mainline Linux for Xiaomi Redmi A5](https://github.com/EberhartLeberhart/Mainline-Linux-f-r-das-Xiaomi-Redmi-A5-serenity-) project.*
