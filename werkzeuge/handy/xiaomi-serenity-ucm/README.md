# UCM für den Xiaomi Redmi A5 (serenity) – ASoC-Konfiguration

Diese Konfiguration erlaubt PipeWire/WirePlumber auf dem Redmi A5 (serenity)
die Onboard-Soundkarte automatisch zu erkennen und zu betreiben
(Vorverstärker AW8738, interner Lautsprecherpfad über SC2730 DACS → SPK PA → SPK_OUT,
Kopfhörerpfad über HPL/HPR).

Basis: Vorlage vom Reeder S19 Max Pro S (= Otto Pflügers Referenzgerät) aus
[`ums9230-mainline/alsa-ucm-conf`](https://codeberg.org/ums9230-mainline/alsa-ucm-conf)
(German original), angepasst auf serenity:

- Speaker-Path verwendet `SPK_OUT` + `aw8738` statt `HPL/HPR`+externer Amp gemäß
  Patch 0039/0040/0041 des Hauptrepos (DACS → SPK PA → SPK_OUT,
  External-Amp als `awinic,aw8738` via `ap_gpio 58`).
- Klasse der `conf.d/`-Shims nötig, weil alsa-lib serenity je nach Driver-Name
  `snd-soc-ums9230-card` bzw. `sound` bzw. CardLongName suchen kann
  (siehe `ucm2/ucm.conf`, `UseCasePath`).

## Dateien

```
ucm2/Unisoc/ums9230/xiaomi-serenity/xiaomi-serenity.conf   (Hauptprofil)
ucm2/Unisoc/ums9230/xiaomi-serenity/HiFi.conf              (HiFi-Verb)
ucm2/Unisoc/ums9230/xiaomi-serenity/VoiceCall.conf         (Voice-Call-Verb)
ucm2/conf.d/{sound,xiaomi-serenity,xiaomiserenity,snd-soc-ums9230-card}/{*.conf}
                                                           (Shims für alle möglichen Karten-Namen)
```

## Verwendung

Auf dem Gerät (root):

```bash
# Pfad als root
cp -r ucm2/* /usr/share/alsa/ucm2/
# Service neu laden
alsaucm -c xiaomi-serenity reload
```

Alternativ für einzelne User via `~/.asoundrc` / `.config/alsa/` nicht nötig –
alsa-ucm-conf sucht auf Systempfaden.

## Abhängig von

- Kernel-Patch 0039/0040/0041 (serenity ASoC path)
- Firmware `novatek_ts_csot_fw.bin` nicht benötigt (nur Touchscreen)
- Module `snd_soc_sc2730`, `snd_soc_aw8738`, `snd_soc_ums9230`, `snd_soc_spk_amp ...`
  
## Lizenz

Die Dateien folgen den Lizenzbestimmungen des upstream `alsa-ucm-conf`
Projektes (GPL-2.0). Sind bearbeitete Arbeiten (© Eberhart Leberhart /
dieses Repository), gleiches gilt.
