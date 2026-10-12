# Idee: Handyhülle mit LTE-Dongle

Stand: 12.10.2026. Idee, noch nicht getestet.

## Grundgedanke
Ein externes LTE-Modul sitzt in einer Hülle und hängt am USB des Handys.
Das Modem im Handy bleibt unangetastet, und der Trusty-Teil wird nicht berührt.
Die Hülle ist also nur eine mechanische und elektrische Verpackung.

## Was dafür nötig ist
- **USB-Host am Handy** (OTG). Das Redmi A5 muss als Host Strom und Daten liefern können.
- **Stromversorgung:** Das Modul zieht im Sendebetrieb deutlich mehr als 500 mA.
  Die Hülle sollte nicht allein am Handy-USB hängen.
- **SIM:** Im Modul-Slot, Zugangsdaten über den Anbieter.
- **Antenne:** Die größte Hürde. Metall und Hülle dämpfen stark, deshalb
  ist eine Antennenführung nach außen nötig.
- **Hitze:** Modul und Akku wärmen sich auf, eine Hülle staut die Wärme.

## Sicherheit und Recht
- Ein fertig zugelassenes Modul verwenden (CE, bei Mobilfunk die passende Zulassung),
  keine Eigenbau-Funkschaltung.
- Akku und Stromversorgung: Eigenbau mit Lithium-Akku birgt Brandrisiko.
  Nur geprüfte Bauteile und eine Schutzschaltung.

## Vorgehen
1. Erst am Laptop mit einem USB-LTE-Modul testen (ohne Hülle).
2. Dann die USB-Verbindung am Handy prüfen (Host-Modus, Strom).
3. Erst danach die Hülle bauen, mit Antennenführung.
