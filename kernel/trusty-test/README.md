# trusty-test – Etappe 1: trusty-Grundtreiber

Portierung von Unisocs trusty-Kerntreiber (`trusty-core.c`, `trusty-mem.c`, Google/Unisoc, GPL v2,
Quelle: mer-hybris/s19mps-kernel `drivers/trusty`) auf Kernel 7.1, zunächst als einzelnes Testmodul.

Änderungen gegenüber dem Original:
- `asm/compiler.h` und die Unisoc-Tracepoints entfernt (gibt es in 7.1 nicht)
- `remove` liefert in 7.1 `void`
- Prüfung auf fehlgeschlagenes `kmalloc` ergänzt
- Testbetrieb ohne Gerätebaum-Knoten: Ist kein `sprd,trusty-smc-v1`-Knoten da, legt das Modul selbst
  ein Gerät an; Unterknoten (irq, log, virtio) gibt es dann noch nicht
- Meldet trusty einen Absturz, gibt es nur eine Warnung statt `panic()` des ganzen Handys

Was das Modul beim Laden tut: nur **Fast-Calls** (Versionstext lesen, API-Version aushandeln).
Keine Standard-Calls, kein gemeinsamer Speicher, keine Dienste.

Bauen (gegen den Kernelbaum, der auf dem Testhandy läuft):

    make KDIR=~/ums9230-linux

Erwartet im `dmesg`:

    sprd-trusty-core: ... trusty version: Unisoc TEE v2.1.1 ...
    sprd-trusty-core: ... selected api version: 3 (requested 3)

## Etappe 2: Interrupts und Log

Neu: `trusty-irq.c` und `trusty-log.c` (gleiche Quelle, GPL v2). Anpassungen an 7.1: `remove` liefert
`void`, `vm_map_ram()` ohne Schutzbits, `panic_notifier_list` aus `linux/panic_notifier.h`.

Braucht den Knoten `/trusty` mit `irq` und `log` im Geraetebaum: `TRUSTY=1` in
`werkzeuge/pc/vendorboot_rezept.sh` (seit 24.11. im Repo).
Mit Knoten probt `trusty.ko` ueber den Geraetebaum und legt die Unterknoten an; `trusty-irq` fragt trusty,
welche Interrupts es braucht, `trusty-log` teilt trusty einen Speicherbereich fuer seine Meldungen zu.
Ab hier gibt es zum ersten Mal Standard-Calls (trusty rechnet auf einem Linux-Kern).

## Ergebnis (24.11., Handy 2, Kernel `g3815c63dc8c1`, vendor_boot mit `PANEL=c3z36 WCN=1 TRUSTY=1`)

| Schritt | Ergebnis |
|---|---|
| `trusty.ko` | „Unisoc TEE v2.1.1, qogirl6, Built: 01:20:24 Nov 28 2024, 921a4247“, API-Version **2** (trusty kann 3 nicht) |
| Bindung | `sprd-trusty-core` → `trusty`, `sprd-trusty-irq` → `trusty:irq`, `sprd-trusty-log` → `trusty:log` |
| `trusty-irq` | trusty fordert **SGI 13** an (per-CPU, `GICv3 13 Edge trusty`), einmal ausgelöst; Linux nutzt nur SGI 0–7 |
| `trusty-log` | Log-Version und Anmeldung des gemeinsamen Speichers (Standard-Calls) ohne Fehler; `trusty_loglevel` 2, `trusty_console` 0, `trusty_logsize` 0x4000 |
| Stabilität | Handy läuft weiter, keine Warnungen |

Das Log bleibt bei Stufe 2 ruhig, solange trusty nichts zu melden hat.

## Etappe 3: virtio und IPC

Neu: `trusty-virtio.c` und `trusty-ipc.c` (gleiche Quelle, GPL v2; `trusty-virtio.c` hat dort keinen
Dateikopf, nur `MODULE_LICENSE("GPL v2")`), dazu `include/linux/trusty/trusty_ipc.h`.
Braucht zusätzlich den Knoten `/trusty/virtio` (`TRUSTY=1` im Rezept, seit Etappe 3).

Anpassungen an 7.1:
- `find_vqs()` bekommt die Namen und Rückrufe jetzt gesammelt als `struct virtqueue_info`
- `remove` liefert `void`, `class_create()` ohne Modulzeiger
- **virtio-ID:** trusty meldet seinen IPC-Kanal mit ID 13. Die ist in Mainline vergeben (Speicher-Ballon,
  bei uns fest eingebaut), der Ballon-Treiber würde sich das Gerät schnappen. `trusty-virtio` meldet das
  Gerät deshalb unter der privaten ID `0x7a5e` an, `trusty-ipc` bindet nur daran
- **Absturz behoben:** trusty kann schon während des Anlegens der Ringe einen Interrupt schicken; dann
  stieß `check_all_vqs()` auf einen noch leeren Ring (`vring_interrupt+0x0`). Jetzt wird der Ring erst
  vollständig gebaut und dann veröffentlicht (`WRITE_ONCE`), leere Ringe werden übersprungen

**Ladereihenfolge ist wichtig:** `trusty-ipc` **vor** `trusty-virtio` laden. trusty liest den Zustand
„Treiber bereit“ nur einmal, beim Start der Kanäle. Kommt `trusty-ipc` erst danach, bleibt der Kanal
offline und es entsteht keine Gerätedatei.

    for m in trusty trusty-irq trusty-log trusty-ipc trusty-virtio; do insmod $m.ko; done

## Ergebnis Etappe 3 (24.11., Handy 2, gleicher Kernel, vendor_boot mit `/trusty/virtio`)

| Schritt | Ergebnis |
|---|---|
| `trusty-virtio` | trusty liefert die Kanalbeschreibung, nimmt unseren gemeinsamen Speicher an (eigene Logzeile `sm_decode_ns_memory_attr ... mair=0xff, sharable=0x3`) |
| Ringe | zwei Ringe mit je 64 Einträgen (`notifyid 1` und `2`) |
| Kanal | `sprd-trusty-ipc virtio0: is online` |
| Gerätedatei | `/dev/trusty-ipc-dev0` (Zeichengerät 235,0) |
| Stabilität | kein Absturz, Handy läuft weiter |

Falsche Reihenfolge (zuerst `trusty-virtio`): Ringe werden angelegt, aber kein „is online“ und keine Gerätedatei.
