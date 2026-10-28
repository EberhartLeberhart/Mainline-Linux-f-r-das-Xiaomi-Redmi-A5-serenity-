# Idee: Kernel-Updates wie bei Linux Mint („Aktualisierungen verfügbar“)

Stand: 28.10.2026. Gehört zur Phase „Installer und Abbilder“.

1. **Kernel als `.deb`-Paket** (Kernel, Gerätebaum/vendor_boot, Module) – `apt` behandelt ihn wie jedes andere Paket.
2. **Eigene Paketquelle mit eigenem Signaturschlüssel** (z. B. über GitHub Releases/Pages); Ubuntu-Pakete weiter aus der
   normalen Quelle.
3. **Anzeige:** als Server beim SSH-Anmelden „X Aktualisierungen verfügbar“ (optional automatische Sicherheitsupdates),
   mit Oberfläche eine Benachrichtigung wie bei Mint.
4. **Sicheres Kernel-Update über A/B-Slots:** neuen Kernel in den gerade nicht benutzten Slot schreiben, mit begrenzten
   Versuchen starten, nach erfolgreichem Start übernehmen, sonst startet der Bootloader automatisch den alten
   (wie heute `deploy.sh` + `redmi-boot-ok.service`, nur als Paket).
