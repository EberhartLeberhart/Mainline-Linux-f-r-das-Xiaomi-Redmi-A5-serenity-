# vsp-test – Testmodule für den Video-Decoder (VSP)

Zwei kleine Out-of-tree-Module für den Video-Decoder des UMS9230. Beide bleiben nicht geladen
(`redmi_vsp_an` meldet absichtlich einen Fehler beim Laden), Ausgabe steht im Kernel-Protokoll.

- **`redmi_vsp_frage.c`** liest nur: Zustand der VSP-Stromdomäne (PMU), Systemfreigabe (AON),
  Takt-Tor und Reset (AP-AHB). Der VSP selbst wird nicht angefasst.
- **`redmi_vsp_an.c`** schaltet den VSP kurz ein (Ablauf aus Unisocs `sprd_vsp_pw_domain.c`), holt die
  Takte über Ottos Takt-Treiber, liest 10 Status-Register, schreibt als Lebenszeichen kleine Muster in
  drei Interrupt-Masken (nur wenn kein RAW-Bit steht) und stellt danach alles wieder her.

Hintergrund, Adressen und Ergebnisse: [DECODER.md](../../DECODER.md).

## Bauen und testen

```
make KDIR=~/ums9230-linux KERNELRELEASE=$(ssh redmi uname -r)
scp redmi_vsp_frage.ko redmi_vsp_an.ko redmi:/tmp/
ssh redmi 'insmod /tmp/redmi_vsp_frage.ko && rmmod redmi_vsp_frage; insmod /tmp/redmi_vsp_an.ko; dmesg | grep redmi_vsp'
```

Erwartet (22.11., Handy 2, Kernel #119): vorher `FORCE_SHUTDOWN=1`, Zustand `0x07`, `VSP_EB=0`;
`Strom an nach 1 x 300 us`, `vsp-clk 384000000 Hz`, Schreibprobe in `+0x1004` und `+0x0014` liest
1/5/a zurück, `+0x006c` bleibt 0; nachher wieder Zustand `0x07`.
