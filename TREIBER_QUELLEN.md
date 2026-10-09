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

## Offene Hürden

1. **sipc + Mailbox fehlen in Mainline.** Ton, WLAN, Bluetooth, GNSS und Modem bauen alle darauf auf. Das ist der erste Port.
2. **trusty:** `unisoc_wcn_bsp` hängt an `trusty-ipc`. Laut Realme-Code wird die WCN-Firmware nur dann von TrustZone
   geprüft, wenn sie einen Signatur-Kopf (`SEC_IMAGE_MAGIC`) hat; sonst wird sie direkt geladen. Ob Xiaomis Firmware
   signiert ist und ob der WCN-Kern ohne Freigabe startet: ❓ (nur durch Versuch zu klären). Mainline hat keinen trusty-Treiber.
3. **Firmware:** Audio-DSP, VBC und WCN brauchen Firmware aus den Android-Partitionen (nicht frei, bleibt auf dem Handy).
4. **Lautsprecher-Verstärker FS1588:** kein Quellcode gefunden. Ohne ihn nur Hörmuschel und Kopfhörer.
5. **Kernel-Version:** 5.4 → 7.1. Der Code ist Vorlage, nicht einbaufertig.

## Weitere Spuren

- postmarketOS-Paket [linux-postmarketos-unisoc-ums9230](https://pkgs.postmarketos.org/package/master/postmarketos/aarch64/linux-postmarketos-unisoc-ums9230)
- [akku1139/ums9230-mainline-linux](https://zff.dev/akku1139/ums9230-mainline-linux)
