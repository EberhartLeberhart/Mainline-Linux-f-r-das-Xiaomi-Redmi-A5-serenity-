# VSP-Treiber (Video-Decoder)

Treiber für den Video-Decoder (VSP) des UMS9230. Wächst etappenweise, siehe
[DECODER.md](../../DECODER.md). Out-of-tree-Modul, braucht den Geräteknoten `sprd,ums9230-vsp`
aus Patch 0064 mit den Resets aus 0065 (und 0060–0063 für Stromdomäne und IOMMU). Für Puffer:
DMA-BUF-Heaps (Patch 0066, `/dev/dma_heap/system`).

| Etappe | Was der Treiber kann | Stand |
|---|---|---|
| 3a | bindet sich an den Knoten, Strom über Runtime-PM (`ap_vsp`), Takte, liest die IOMMU-Register, bildet einen 64-KiB-Puffer über die IOMMU ab und prüft die Abbildung | ✅ 23.11. |
| 3b | Reset über Ottos Reset-Steuerung (Patch 0065, Bits 13 und 11 – nicht der IOMMU-Reset Bit 10), Beweis über ein Muster in der Interrupt-Maske; Interrupt SPI 34 anmelden und Ruhe prüfen | ✅ 23.11. |
| 3c | Gerätedatei `/dev/sprd_vsp` mit Unisocs Schnittstelle (ioctl-Magie `'m'`, Nummern 1–18, `mmap` der Register, Puffer als dma-buf über `VSP_GET_IOVA`); Selbsttest aus 3a/3b beim Laden (abschaltbar mit `selbsttest=0`); Testprogramm `redmi_vsp_test` | ✅ 23.11. |

## Bauen und testen

**PC:**
```
cd ~/Mainline-Linux-f-r-das-Xiaomi-Redmi-A5-serenity-/kernel/vsp
make KDIR=~/ums9230-linux KERNELRELEASE=$(ssh redmi2 uname -r)
scp redmi_vsp_drv.ko redmi_vsp_test redmi2:/tmp/ && make clean KDIR=~/ums9230-linux
```

**Handy:**
```
insmod /tmp/redmi_vsp_drv.ko && /tmp/redmi_vsp_test; dmesg | grep redmi-vsp; rmmod redmi_vsp_drv
```

`make` baut das Modul und das Testprogramm (statisch, läuft ohne Bibliotheken).

Erwartet (Handy 2, Kernel #126): Selbsttest wie in 3b (`Reset-Probe … (zurueckgesetzt)`, `Interrupt 47 … 0 mal`,
`… (abgebildet)`), dann `bereit: /dev/sprd_vsp (VSP-Version 20)`. Das Testprogramm meldet bei jedem Befehl `OK`,
`VSP_GET_IOVA … 1 MiB: IOVA 0x2ff00000, 1048576 Bytes`, `VSP_COMPLETE` ohne Auftrag nach 200 ms mit
`ETIMEDOUT` und Status `0x80000000`, am Ende `bestanden (0 Fehler)`.

## Schnittstelle `/dev/sprd_vsp`

Nur root (`crw-------`). Arbeitsweise wie bei Unisoc: Die Bibliothek bildet die Register per `mmap` ab und
programmiert den VSP selbst; der Kernel reserviert, schaltet Takte, setzt zurück, wartet auf den Interrupt und
bildet Puffer über die IOMMU ab.

| ioctl | Nr. | Was unser Treiber tut |
|---|---|---|
| `VSP_CONFIG_FREQ` / `VSP_GET_FREQ` | 1 / 2 | merkt sich die Stufe; Takt bleibt vorerst fest bei 384 MHz ❓ |
| `VSP_ENABLE` / `VSP_DISABLE` | 3 / 4 | Takte an/aus, Interrupt an/aus (Interrupt nur mit Takt) |
| `VSP_ACQUAIRE` / `VSP_RELEASE` | 5 / 6 | Hardware reservieren (500 ms Wartezeit) / freigeben |
| `VSP_COMPLETE` | 7 | auf den Interrupt warten (200 ms); Status zurück, Bit 31 = Zeitüberschreitung, Bit 30 = unterbrochen |
| `VSP_RESET` | 8 | VSP-Reset (Bits 13/11), IOMMU bleibt |
| `VSP_HW_INFO` | 9 | liefert `0x20` (AP_SYST_EB gesetzt), wie Unisoc |
| `VSP_VERSION` | 10 | 20 (SHARKL6) |
| `VSP_GET_IOVA` / `VSP_FREE_IOVA` | 11 / 12 | dma-buf über die IOMMU abbilden / freigeben |
| `VSP_GET_IOMMU_STATUS` | 13 | 0, wenn die IOMMU da ist |
| `VSP_SET_CODEC_ID`, `VSP_GET_CODEC_COUNTER`, `VSP_SET_SCENE`, `VSP_GET_SCENE`, `VSP_SYNC_GSP` | 14–18 | merken bzw. zurückgeben, sonst nichts |

`mmap` bis `0x14000` Bytes (wie Unisocs `SPRD_VSP_MAP_SIZE`), ungecacht. Beim Schließen der Datei räumt der Treiber
alles auf (Takte, Reservierung, abgebildete Puffer) – auch wenn das Programm abstürzt.

**Offen ❓:** Wer die Register abbildet, kann auch die IOMMU-Register am Anfang des Fensters ändern (bei Unisoc
genauso). Deshalb nur für root; später sauberer lösen.

## Hinweis

Das Registerfenster beginnt mit den IOMMU-Registern, die `sprd-iommu` schon belegt. Der Treiber bildet es deshalb
mit `devm_ioremap()` ab, ohne den Bereich anzufordern.
