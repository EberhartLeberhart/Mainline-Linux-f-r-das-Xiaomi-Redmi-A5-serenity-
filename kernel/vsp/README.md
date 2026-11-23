# VSP-Treiber (Video-Decoder)

Erster richtiger Treiber für den Video-Decoder (VSP) des UMS9230. Wächst etappenweise, siehe
[DECODER.md](../../DECODER.md). Out-of-tree-Modul, braucht den Geräteknoten `sprd,ums9230-vsp`
aus Patch 0064 (und 0060–0063 für Stromdomäne und IOMMU).

| Etappe | Was der Treiber kann | Stand |
|---|---|---|
| 3a | bindet sich an den Knoten, Strom über Runtime-PM (`ap_vsp`), Takte, liest die IOMMU-Register, bildet einen 64-KiB-Puffer über die IOMMU ab und prüft die Abbildung | ✅ 23.11. |

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

Erwartet (Handy 2, Kernel #124): `CFG = 0x0000000b`, `FIRST_VPN = 0x00020000`, `VPN_RANGE = 0x0000ffff`,
`Puffer 64 KiB: IOVA 0x2fff0000 -> phys 0x1… (abgebildet)`.

Hinweis: Das Registerfenster beginnt mit den IOMMU-Registern, die `sprd-iommu` schon belegt. Der Treiber bildet
es deshalb mit `devm_ioremap()` ab, ohne den Bereich anzufordern.
