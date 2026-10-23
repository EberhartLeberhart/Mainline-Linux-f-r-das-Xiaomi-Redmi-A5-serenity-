# Was im Kernel noch fehlt (Stand 23.10.2026)

Zum Abhaken. Belege und Einzelheiten stehen in [STATUS.md](STATUS.md), die Arbeitsreihenfolge in
[UEBERGABE.md](UEBERGABE.md). Zeichen: 🟡 angefangen · ❌ fehlt · ❓ noch nicht untersucht.

**Läuft schon:** Start kalt/warm, 8 Kerne mit cpufreq und Tiefschlaf, eMMC, PMIC (Aus, Neustart, Uhr,
Wachhund), Temperaturschutz, Akkuanzeige und Ladegrenze, USB-Gerät und -Host, LAN über USB-Adapter,
Display (DRM) mit Helligkeit, Touch (Zuordnung geprüft, bis 10 Finger), Tasten, WCN-Kern mit allen
sipc-Kanälen, `wlan0` mit Scan und Anmeldung (wpa_supplicant empfängt EAPOL 1/4), Krypto-Befehle der
Kerne (AES/SHA/CRC32/NEON, Kernel nutzt `aes-ce`).

## CPU, GPU und Beschleuniger

| Bereich | Stand | Was fehlt |
|---|---|---|
| CPU-Kapazität (big.LITTLE) | ❌ | Planer hält alle 8 Kerne für gleich stark (`cpu_capacity` 1024 bei cpu0 und cpu7). T7250 = 6× A55 + 2× A75 (cpu6/7). `capacity-dmips-mhz` fehlt im Gerätebaum (auch bei Codeberg) → Werte aus Xiaomis `dtb_a` holen |
| Energiemodell / EAS | ❌ | `/sys/kernel/debug/energy_model` leer; braucht zusätzlich `dynamic-power-coefficient` je Kern (aus Xiaomis DT) |
| Leerlauf-Stufen | 🟡 | `WFI` und `cpu-pd-lit` vorhanden; Cluster-Stufen und große Kerne prüfen ❓ |
| devfreq (Speicher/GPU-Takt) | ❌ | `/sys/class/devfreq` leer |
| GPU Mali-G57 | 🟡 | Knoten `gpu@23100000` (`sprd,ums9230-mali`, `arm,mali-valhall-jm`) mit Takten, Reset, Interrupts, Power-Domain `gpu_top` ist im DT, aber `disabled`. Panfrost (=m) kennt `arm,mali-valhall-jm`. Fehlt: Temperaturgrenze `gpu-thermal`, `status = "okay"`, `panfrost.ko` aufs Handy, OPP-Tabelle für Taktstufen ❓, Mesa-Test |
| Video-Decoder/-Encoder (VSP) | ❓ | Android `sprd_vsp`; in Mainline nichts bekannt |
| JPEG-Einheit | ❓ | |
| GSP (2D-Zusammensetzung) | ❓ | Android `sprd-gsp.ko` |
| Hardware-Zufallsgenerator | ❓ | Android `sprd_trng.ko` |

## Angefangen (🟡)

| Bereich | Was fehlt |
|---|---|
| WLAN | Senden über einen Datenkanal, Schlüssel (`WIFI_CMD_KEY`), DHCP → SSH; Lauscher in sauberen Treiber aufteilen (sipc-Kern + WLAN) |
| Display-Schlaf | Ab-/Anschalten über den DRM-Treiber bringt das Panel nicht wieder an; Umweg über direkte DSI-Befehle |
| Touch | Wächter-Signal ein Byte zu früh geprüft (lädt nicht selbst neu); als Panel-Follower ans Display koppeln |
| USB | Rollenwechsel: ein seltener Fehlschlag; Gadget-MAC wechselt bei jedem Start |
| Temperatur | `gpu-thermal` ohne Grenze (vor der GPU nötig) |
| i2c-sprd | meldet kein NACK |
| Ladechip | nur per `i2cset`; drei mögliche Chips (SGM41513 getestet, SC89601, UPM6922 nicht) |
| Start | `redmi-boot-ok.service` markiert fest Slot b; Bootlogo (`logo_b`) wirkt nicht |

## Fehlt (❌)

| Bereich | Stand / Abhängigkeit |
|---|---|
| Bluetooth | WCN läuft, Ringe 11/10 auf Kanal 4 angelegt; HCI-Treiber und Konfigurationsdaten (pskey/rf) fehlen ❓ |
| FM-Radio | am WCN-Kern, Ringe 13/14 |
| GNSS/GPS | eigener Kern (core@4, Mailbox 9), `gnssmodem.bin`; Speicher `0x8749b000–0x874ccfff` reservieren |
| Ton | Quellcode gefunden (Realme); davor `sprd-dma` (−110, hängt an der Audio-DSP-Domain); Lautsprecher-Verstärker FS1588 ohne Quellcode |
| Kamera | ISP `0x3a000000`; Sensoren laut Analyse OV SP0821, BYD BF30A2, SC080CS ❓ |
| Mobilfunk | bewusst zurückgestellt bis zur Antwort von Bundesnetzagentur/Xiaomi (Anfragen 23.10.); vorbereitbar: sipc-Treiberstapel, `sprd-pmic-wdt` |
| PWM | ungeklärt, fürs Panel nicht nötig |

## Noch nicht untersucht (❓)

- Sensoren (Lage/Beschleunigung, Helligkeit, Annäherung) – Android lädt `sensorhub.ko`
- microSD-Kartenleser (zweiter `sdhci`?)
- Vibrationsmotor (SC2730, Mainline `sc27xx-vibra`)
- Fingerabdrucksensor (falls verbaut)
- Kopfhörer-Erkennung, USB-C-Kabelerkennung (`sc27xx_typec`)
- Akku-Echtheitsprüfung DS28E30 (1-Wire, vermutlich unwichtig)

## Danach (kein Kernel)

kexec-Startmenü mit Rückfall, Lademodus-Anzeige (Ziege + Akkustand), Bootloader wieder sperren mit
eigenem Schlüssel, dann Oberfläche, Installer und Abbilder.
