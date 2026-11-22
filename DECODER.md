# Video-Decoder (VSP) – Stand 22.11.2026

Der UMS9230 hat einen eigenen Video-Block („VSP“, Video Signal Processor) für H.264, HEVC und weitere
Formate. Unter Linux gibt es dafür bisher keinen Treiber: Firefox dekodiert jedes Video mit der CPU,
was viel Last und Wärme macht. Diese Seite hält fest, was wir über den Block wissen, welche Quellen es
gibt und wie wir vorgehen.

**Keine Herstellerdateien im Repo:** Die Android-Bibliotheken und Kernel-Module stammen aus der
`super`-Partition des eigenen Handys und bleiben privat. Hier stehen nur Namen, Adressen und unsere
eigenen Messungen.

## Hardware

Aus Unisocs Gerätebaum für den UMS9230 (`qogirl6.dtsi`, Jolla-C2-Kernel) und Xiaomis `ums9230.dtsi`:

| Teil | Wert |
|---|---|
| VSP | `0x32000000`, Größe `0xc000`, Interrupt **SPI 34**, `sprd,video_ip_version = <20>`, kompatibel `"sprd,qogirl6-vsp", "sprd,sharkl5pro-vsp"` |
| Globale VSP-Register | VSP + `0x1000` (`VSP_INT_STS` 0x0, `_MASK` 0x4, `_CLR` 0x8, `_RAW` 0xc, `AXI_STS` 0x1c) |
| ARM-Interrupt | VSP + `0x10`–`0x1c` – nutzt Unisocs Treiber nur bei Chips **vor** SharkL3; bei uns (`video_ip_version 20` = `SHARKL6`) liegen dort IOMMU-Register |
| IOMMU | im selben Fenster `0x32000000`–`0x3200007f` (+ `0xa0`), kompatibel `sprd,iommuvaul6-vsp`, IOVA `0x20000000` + 256 MiB; Bauart **VAU** (`VERSION` = `0x100`), Register wie in mainline `sprd-iommu.c` (siehe unten) |
| Stromdomäne | PMU `0x64020000` + `0x314` (`PD_AP_VSP_CFG_0`): Bit 25 `FORCE_SHUTDOWN`, Bit 24 `AUTO_SHUTDOWN_EN`; Zustand in `+0x514` (`PWR_STATUS_DBG_9`) Bits 20:16, 0 = an |
| Freigaben | AON `0x64000000` + `0x4` Bit 5 `AP_SYST_EB`; AP-AHB `0x20400000` + `0x0` Bit 2 `VSP_EB`, Reset `+0x4` Bit 13 `VSP_SOFT_RST`, Bit 11 `VSP_GLOBAL_SOFT_RST` |
| Takte | AP-AHB-Tor `CLK_VSP_EB` (2), VSP-Takt `CLK_VSP` (28) mit Quellen 256 / 307,2 / 384 MHz – beide schon in Ottos `ums9230-clk.c` |
| Daneben | JPEG-Block `0x36000000` (SPI 42, eigene IOMMU) |

Einschalten (aus Unisocs `sprd_vsp_pw_domain.c`): `AUTO_SHUTDOWN_EN` und `FORCE_SHUTDOWN` löschen,
dann alle 300 µs den Zustand lesen, bis er 0 ist (höchstens 100 Versuche). Ausschalten:
`FORCE_SHUTDOWN` setzen.

## Erster Kontakt (22.11., Handy 2, Kernel #119)

Testmodule unter [`kernel/vsp-test/`](kernel/vsp-test/).

1. **Nur lesen:** VSP-Domäne zwangsweise aus (`FORCE_SHUTDOWN=1`, Zustand `0x07`), `AP_SYST_EB=1`,
   `VSP_EB=0`, kein Reset. Der Block ruht also sauber.
2. **Einschalten:** Strom nach **300 µs** da, Takt **384 MHz** über Ottos Takt-Treiber, kein Hänger beim
   Lesen, danach wieder Zustand `0x07`.
3. **Lebenszeichen:** Muster 1/5/a in `GLB VSP_INT_MASK` (+0x1004) und `ARM_INT_MASK` (+0x14)
   geschrieben → genau so zurückgelesen ✅. `MMU_INT_MASK` (+0x6c) bleibt 0 ❓ – die IOMMU braucht
   vermutlich noch einen eigenen Takt (Android nennt `clk_vsp_ahb_mmu_eb`, `clk_vsp_mq_ahb_eb`) oder
   muss erst selbst eingeschaltet sein.

**Strom, Takt und Registerzugriff funktionieren also unter Linux.**

Nachtrag: Die Stelle `+0x14` ist bei uns nicht `ARM_INT_MASK`, sondern `DEFAULT_PPN` der IOMMU (siehe
nächster Abschnitt) – die Schreibprobe hat also schon die IOMMU erreicht. `+0x6c` gibt es in Unisocs
Registerliste für die VAU-Bauart nicht; die Angabe im VSP-Treiber stammt vermutlich von der älteren
EX-Bauart ❓. Die zusätzlichen Takte `clk_vsp_ahb_mmu_eb`/`clk_vsp_mq_ahb_eb` holt Unisocs Treiber nur
für SharkL3 und Pike2 – für uns reichen `CLK_VSP_EB` und `CLK_VSP`.

## IOMMU (22.11., Handy 2, Kernel #119)

Testmodul `redmi_vsp_iommu.c`: VSP einschalten wie oben, dann die IOMMU-Register **nur lesen**.

| Register | Stelle | gelesen (Grundzustand) |
|---|---|---|
| `VERSION` | +0x00 | `0x00000100` → Bits 15:8 = **1 = VAU** |
| `CFG` | +0x04 | `0x00000008` (aus; Bit 3 unbekannt ❓, mainline lässt es unberührt) |
| `UPDATE`, `AUTH_CFG`, `FIRST_PPN`, `DEFAULT_PPN_RD/WR`, `FIRST_VPN` | +0x08–0x1c | 0 |
| `VPN_RANGE` | +0x20 | `0x000fffff` |
| `PPN1_RANGE`, `PPN2_RANGE` | +0x28, +0x30 | `0x0000ffff` |
| Fehleradressen | +0x44–0x68 | 0 |
| `MMU_INT_EN` | +0xa0 | 0 |

Damit passt der **mainline-Treiber `drivers/iommu/sprd-iommu.c`** (`sprd,iommu-v1`, läuft bei Otto
schon fürs Display): Er erkennt die Bauart an genau diesem Register und nutzt für VAU dieselben Stellen
wie Unisocs `sprd_iommuvau_hal_register.h`. Kleine Unterschiede ❓: mainline schreibt zusätzlich
`DEFAULT_PPN_WR` (+0x18), Unisoc setzt beim Einschalten noch Bit 4 (`VAOR_BYPASS`, `0x13` statt `0x3`).

**Hürde:** `sprd-iommu` liest `VERSION` einmal beim Start und kennt keine Stromdomäne. Die VSP-Domäne
muss also vorher als genpd-Treiber existieren und im Gerätebaum per `power-domains` an IOMMU und VSP
hängen – sonst liest der Treiber einen ausgeschalteten Block.

## Android-Seite (eigene `super`-Partition, privat)

- `vendor/lib64/libomx_avcdec_hw_sprd.so` (H.264), `libomx_hevcdec_hw_sprd.so` (HEVC), dazu Codec2-Fassungen
  `libcodec2_unisoc_*`. Die Bibliothek braucht nur Grundbibliotheken (`libc`, `libm`, `libdl`, `libc++`,
  `liblog`, `libutils`, `libcutils`) – kein Grafiktreiber, kein Mediaserver.
- Schnittstelle: `H264DecInit` → `H264DecDecode` → `H264DecRelease` (siehe `SPRDAVCDecoder.cpp`,
  Apache 2.0, in `codeworkx/android_hardware_sprd`). Den H.264-Strom zerlegt die Bibliothek selbst in
  Software (Slice-Header, CABAC), die Hardware rechnet – das entspricht den „stateless“-Decodern in
  mainline (Cedrus, Hantro, RKVDEC).
- Kernel-Seite: Gerätedatei **`/dev/sprd_vsp`**, ioctl-Magie `'m'`, Nummern 1–18 (`VSP_CONFIG_FREQ`,
  `VSP_ENABLE`, `VSP_ACQUAIRE`, `VSP_COMPLETE`, `VSP_RESET`, `VSP_GET_IOVA`, `VSP_FREE_IOVA`, …). Die
  Bibliothek blendet sich den Registerblock per `mmap` ein und schreibt ihn selbst.
- Xiaomis Module: `vpu.ko` („UNISOC VPU Driver“, GPL, Quellpfad `bsp/modules/kernel5.15/video/sprd-vpu/`),
  `sprd-vsp-pw-domain.ko` (GPL v2), `jpg.ko` (GPL v2).

## Quellen (GPL bzw. Apache 2.0)

| Was | Wo |
|---|---|
| VSP-Treiber (`sprd_vsp.c`, `vsp_common.c`, `vsp_isr_func.c`), VPU-Treiber, Stromdomäne, JPEG | [mer-hybris/s19mps-kernel-modules](https://github.com/mer-hybris/s19mps-kernel-modules) `video/` – Jolla C2 = Reeder S19 Max Pro S, gleicher Chip, Kernel 5.4 |
| Gerätebaum und Register-Header für den UMS9230 | [mer-hybris/s19mps-kernel](https://github.com/mer-hybris/s19mps-kernel) `arch/arm64/boot/dts/sprd/qogirl6.dtsi`, `include/dt-bindings/soc/sprd,qogirl6-*.h` |
| OpenMAX-Hülle der Android-Bibliothek | [codeworkx/android_hardware_sprd](https://github.com/codeworkx/android_hardware_sprd) `omx_components/video/avc_sprd/` |
| Ältere VSP-Generation (SC8830, Registernamen) | ebenda `libcamera/sc8830/vsp/` – nur lesen, „All Rights Reserved“ |

## Vorgehen

**Weg B zuerst, als Werkzeug für Weg A:** Die Android-Bibliothek unter Linux zum Laufen bringen
(eigener Kernel-Treiber mit derselben Schnittstelle), dabei die Registerzugriffe mitschreiben, und
daraus später einen eigenen, offenen V4L2-Treiber bauen.

| Etappe | Stand | Was |
|---|---|---|
| 0. Bestandsaufnahme | ✅ | Hardware, Android-Bibliothek, Kernel-Module, GPL-Quellen (21./22.11.) |
| 1. Den Chip fragen | ✅ | Stromdomäne, Takte, Registerzugriff (22.11., siehe oben) |
| 2. IOMMU | 🟡 | Bauart VAU bestätigt, mainline `sprd-iommu` passt (22.11.); fehlt: Stromdomäne als genpd, Gerätebaum-Eintrag, erster Map-Test |
| 3. Treiber | ❌ | Stromdomäne als genpd, `/dev/sprd_vsp` mit den ioctls aus dem Jolla-Code auf 7.1 portieren; Puffer per dma-buf |
| 4. Bibliothek laden | ❌ | Chroot mit `system/` und `vendor/` aus der eigenen `super`-Partition, kleines Testprogramm (NDK), ein H.264-Bild dekodieren |
| 5. Mitschreiben | ❌ | Registerstand bei jedem `VSP_COMPLETE` festhalten → Ablauf je Bild verstehen |
| 6. Eigener Treiber | ❌ | V4L2-stateless-Treiber, erst H.264; Anbindung an GStreamer/FFmpeg ❓ |

**Nicht vergessen:** Kein Treiber auf instabilem Fundament – die Abstürze der Taktsteuerung sind seit
Patch 0059 behoben (siehe [FEHLT.md](FEHLT.md)).
