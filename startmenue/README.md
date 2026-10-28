# Startmenü („BIOS“) – Konzept, optional

Stand: 28.10.2026. **Nur Konzept, noch kein Code.**

## Abgrenzung (wichtig)
- **Optional, nicht Standard.** Standard bleibt der bisherige Weg: der Bootloader startet direkt den Kernel aus
  `kernel/` (wie im Haupt-Repo beschrieben). Das Startmenü wird später nur auf Wunsch im Installer ausgewählt.
- **Getrennt vom Hauptkernel:** eigener Ordner, eigene schlanke Kernel-Konfiguration, eigene Doku.
- **Keine Funkfunktionen.** Mobilfunk (LTE), WLAN, Bluetooth, GPS und FM sind im Startmenü **nicht verfügbar** und
  werden nicht angesteuert; die Funk-Firmware des Geräts wird weder geladen noch verändert. Die Funktreiber werden in
  der Startmenü-Konfiguration gar nicht erst eingebaut (nachprüfbar). Damit berührt das Startmenü die offene Frage an
  die Bundesnetzagentur (Betriebssystemwechsel bei unveränderter Funk-Firmware) nicht.

## Idee
Ein kleiner „Menü-Kernel“ mit winziger Startumgebung (initramfs) in `boot_b`, ähnlich LinuxBoot/Petitboot:
1. bringt nur Display, Tasten (Lauter/Leiser/Ein), eMMC, microSD und USB-Host mit;
2. sucht auf allen Speichern nach startbaren Systemen und zeigt ein Menü
   (z. B. „eMMC: Ubuntu“, „SD-Karte: Testsystem“, „USB-Stick: Rettungssystem“);
3. startet das gewählte System per `kexec`, ohne Neustart über den Bootloader;
4. ohne Auswahl nach einigen Sekunden das Standardsystem; nach einem Absturz über den Wachhund zurück ins Menü;
5. Lademodus-Anzeige (Ziege + Akkustand), wenn das Handy ausgeschaltet am Kabel hängt.

Damit wäre auch „von SD-Karte / USB-Stick starten“ gelöst, ohne den Bootloader anzufassen.

## Offene Punkte ❓
- **kexec auf Handy-Chips:** vor dem Sprung müssen Display, IOMMU, DMA usw. sauber stillstehen. Das saubere
  Aus-/Einschalten des Displays über den Treiber (Panel-Fix vom 27.10.) ist ein Baustein dafür.
- **Sicherheit:** wenn der Bootloader später mit eigenem Schlüssel gesperrt ist, startet er nur noch den signierten
  Menü-Kernel – dann darf das Menü per `kexec` auch **nur signierte Kernel** laden (Kernel-Signaturprüfung für kexec),
  sonst könnte jeder per USB-Stick ein beliebiges System starten.
