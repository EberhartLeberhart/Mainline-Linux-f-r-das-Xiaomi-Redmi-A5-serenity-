# Lizenzen in diesem Repository

Dieses Projekt ist Open Source. Weil es aus verschiedenen Arten von Inhalten besteht, gelten je nach Ordner
verschiedene, jeweils übliche Lizenzen. Im Zweifel gilt die Angabe in der einzelnen Datei bzw. im Ordner.

| Bereich | Lizenz | Warum |
|---|---|---|
| `kernel/` – Kernel-Patches, Konfiguration, Kernel-Module (z. B. `wcn-test`) | **GPL-2.0-only** (Text: [`LICENSE`](LICENSE)) | Änderungen am Linux-Kernel stehen zwingend unter dessen Lizenz. Patches anderer Autoren (z. B. Codeberg ums9230-mainline, George Chan) behalten ihre Urheber und ihre GPL-2.0-Lizenz. |
| `werkzeuge/`, `startmenue/` – Skripte, Dienste, Hilfsprogramme | **GPL-2.0-only** (Text: [`LICENSE`](LICENSE)) | Gleiche Lizenz wie der Kernel, damit Werkzeuge und Kernel-Teile frei zwischen beiden wandern können. |
| Dokumentation – alle `*.md`-Dateien, `belege/`, `ideen/` | **CC BY-SA 4.0** ([Lizenztext](https://creativecommons.org/licenses/by-sa/4.0/legalcode.de)) | Üblicher Standard für freie Texte: frei nutzbar und veränderbar mit Namensnennung (EberhartLeberhart), Weitergabe unter gleichen Bedingungen. |
| `bilder/` – Bilder, Logos, Maskottchen | **eigene Angabe je Bild** (README im jeweiligen Unterordner) | Bilder stammen teils von anderen Beitragenden und können fremde Bestandteile enthalten (z. B. Tux von Larry Ewing). |

## Was nicht hier liegt

Firmware, Partitionsabbilder, Sicherungen und Gerätedaten von Xiaomi, Unisoc, Novatek und anderen Herstellern
werden **nicht** veröffentlicht – sie gehören ihren Herstellern. Werkzeuge in diesem Repo lesen sie bei Bedarf
direkt vom eigenen Gerät.

## Beiträge

Wer etwas beiträgt (Pull-Request, Patch, Bild), stimmt zu, dass es unter der Lizenz des jeweiligen Bereichs
veröffentlicht wird – außer es ist ausdrücklich anders vereinbart und im Ordner dokumentiert.

*Kein Rechtsrat; Fragen und Hinweise gern als Issue.*
