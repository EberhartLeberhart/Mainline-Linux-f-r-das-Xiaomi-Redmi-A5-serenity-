# VSP-Treiber (Video-Decoder)

Erster richtiger Treiber für den Video-Decoder (VSP) des UMS9230. Wächst etappenweise, siehe
[DECODER.md](../../DECODER.md). Out-of-tree-Modul, braucht den Geräteknoten `sprd,ums9230-vsp`
aus Patch 0064 mit den Resets aus 0065 (und 0060–0063 für Stromdomäne und IOMMU).

| Etappe | Was der Treiber kann | Stand |
|---|---|---|
| 3a | bindet sich an den Knoten, Strom über Runtime-PM (`ap_vsp`), Takte, liest die IOMMU-Register, bildet einen 64-KiB-Puffer über die IOMMU ab und prüft die Abbildung | ✅ 23.11. |
| 3b | Reset über Ottos Reset-Steuerung (Patch 0065, Bits 13 und 11 – nicht der IOMMU-Reset Bit 10), Beweis über ein Muster in der Interrupt-Maske; Interrupt SPI 34 anmelden und Ruhe prüfen | ✅ 23.11. |

## Bauen und testen

**PC:**
```
cd ~/Mainline-Linux-f-r-das-Xiaomi-Redmi-A5-serenity-/kernel/vsp
make KDIR=~/ums9230-linux KERNELRELEASE=$(ssh redmi2 uname -r)
scp redmi_vsp_drv.ko redmi2:/tmp/ && make clean KDIR=~/ums9230-linux
```

**Handy:**
```
insmod /tmp/redmi_vsp_drv.ko; dmesg | grep redmi-vsp; rmmod redmi_vsp_drv
```

Erwartet (Handy 2, Kernel #125): `CFG = 0x0000000b`, `FIRST_VPN = 0x00020000`, `VPN_RANGE = 0x0000ffff`;
`vor Reset … MASK 0x00000031`, `nach Reset … MASK 0x00000000`, `Reset-Probe: … (zurueckgesetzt)`, MMU-Werte vor und
nach dem Reset gleich; `Interrupt 47 … 0 mal ausgeloest`; `Puffer 64 KiB: IOVA 0x2fff0000 -> phys 0x1… (abgebildet)`.

Hinweis: Das Registerfenster beginnt mit den IOMMU-Registern, die `sprd-iommu` schon belegt. Der Treiber bildet
es deshalb mit `devm_ioremap()` ab, ohne den Bereich anzufordern.
