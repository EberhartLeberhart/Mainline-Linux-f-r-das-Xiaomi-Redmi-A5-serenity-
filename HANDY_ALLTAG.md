# Das Redmi A5 als normales Handy – was noch fehlt

Ziel: Das Redmi A5 mit Linux-Desktop soll sich im Alltag bedienen lassen wie unter Android – alles per Touch
und aus dem Desktop heraus, ohne Terminal, ohne Maus und Tastatur.

Diese Liste ist für die **Bedienung**. Was im **Kernel** fehlt, steht in [FEHLT.md](FEHLT.md), der geprüfte Stand in
[STATUS.md](STATUS.md). Mithelfen ist willkommen – am besten ein Punkt pro Pull Request.

Zeichen: ✅ erledigt · 🟡 teilweise · ❌ offen · ⏸️ bewusst angehalten · ❓ Vermutung, noch nicht geprüft

Stand: 16.11.2026 – Ubuntu 24.04 + Cinnamon 6.0 (lightdm, slick-greeter) auf dem Arbeitshandy;
beim Tester Arch Linux ARM + KDE Plasma 6.8 (Wayland).

## Desktop als Standard und Gestaltung

| Punkt | Stand | Notiz |
|---|---|---|
| Desktop läuft von Haus aus | ❌ | Ziel: Wer den Kernel bzw. später das fertige Abbild installiert, bekommt Cinnamon samt Anpassungen gleich mit (Installer/Abbild). Wer einen anderen Desktop möchte (KDE Plasma, Phosh, …), tauscht ihn selbst; der Kernel bleibt derselbe |
| Oberfläche fürs Handy gestalten | ❌ | eigenes Erscheinungsbild statt PC-Standard: Leistenaufbau und -lage, Startmenü, Symbole und Abstände für Finger, Farben und Hintergrund; als Vorgabe ins Abbild |

## Starten und Anmelden

| Punkt | Stand | Notiz |
|---|---|---|
| Desktop an/aus mit Schalter | ✅ | 16.11.: `redmi-desktop.sh an|aus|status`, Schalter `/etc/redmi/desktop.conf`; aus = Server-Betrieb |
| Automatische Anmeldung | ✅ | 16.11.: lightdm `autologin-user=tunnel-handy` (wie bei Android ohne Passwortfeld) |
| Bildschirmtastatur am Anmeldebildschirm | ❌ | slick-greeter blendet `onboard` nach einem Neustart nicht ein (nur beim Start von Hand) ❓; im Protokoll keine Meldung dazu |
| Bildschirmsperre mit PIN per Touch | ❌ | braucht eine Bildschirmtastatur in der Sperre |
| Herunterfahren ohne Hänger | 🟡 | 16.11. einmal ~90 s beim Licht-Dienst gehangen; Dienste brauchen kurze Abbruchzeiten |

## Bildschirm und Einschalttaste

| Punkt | Stand | Notiz |
|---|---|---|
| Display bleibt im Desktop an | ✅ | 16.11.: `redmi-licht.sh` behandelt `DESKTOP=an` wie `START=an` |
| Einschalttaste kurz = Display an/aus | ❌ | heute: Licht an (ohne Abschaltung); Ziel wie Android: an/aus umschalten, Touch schläft mit |
| Helligkeit im Desktop regeln | ❓ | `/sys/class/backlight/31100000.dsi.0` ist da; prüfen, ob der Cinnamon-Regler sie nimmt |
| Display nach Zeit aus (Energieverwaltung) | ❓ | Cinnamon-Einstellung prüfen, Zusammenspiel mit `redmi-licht.sh` |
| 60/90/120 Hz | ✅ | Patches 0050–0052; in den Anzeige-Einstellungen wählbar ❓ |
| Hoch- und Querformat | 🟡 | von Hand drehbar (beim Tester quer); automatisch braucht den Lagesensor (hängt am pmsys-Kern ⏸️) |
| Leiste: Abstand für runde Ecken und Kamera-Aussparung | ❌ | Hostname links oben wird angeschnitten |

## Bedienung per Touch

| Punkt | Stand | Notiz |
|---|---|---|
| Touch als Maus | ✅ | 16.11.: trifft genau (Zuordnung Display/Touch seit 26.10.) |
| Bildschirmtastatur im Desktop bei Textfeldern | ❌ | `onboard` ist installiert; automatisch einblenden einrichten |
| Größere Bedienelemente | ❌ | Skalierung, Leistenhöhe, Schriftgröße für Finger |
| Gesten (Wischen) | ❌ | Cinnamon-Touch-Gesten prüfen ❓ |
| Mint-Optik | 🟡 | Mint-Y-Symbole installiert; Mint-Themen nicht bei Ubuntu → von Mint (GitHub) holen |

## Netz

| Punkt | Stand | Notiz |
|---|---|---|
| WLAN (unser Weg) | ✅ | `redmi-wlan.service` + wpa_supplicant + systemd-networkd |
| WLAN im Desktop wählen | ❌ | 16.11.: NetworkManager stritt mit unserem Weg um `wlan0` (vermutlich auch ein Absturz, Kernel-Meldung `ieee80211 phy0: mismatch`) → vorerst `wlan0`/`usb0` für NetworkManager **unmanaged**. Ziel: NetworkManager übernimmt `wlan0` ganz, unser Dienst lädt nur den Treiber |
| Bluetooth im Desktop | ❓ | Kernel ✅; Desktop-Applet + `redmi-bt.sh` (feste Adresse) zusammen prüfen |
| Mobilfunk | ⏸️ | wartet auf die Antwort der Bundesnetzagentur |
| Uhrzeit aus dem Netz | ❌ | Uhr geht ~1 h vor; Zeitabgleich (NTP) prüfen |

## Ton

| Punkt | Stand | Notiz |
|---|---|---|
| Lautsprecher | ✅ | UCM-Profil + `redmi-ton.service` (15.11.) |
| Lautstärke im Desktop | ❓ | PipeWire mit dem UCM-Profil prüfen (Software-Lautstärke) |
| Laut-/Leise-Tasten | ❓ | senden `KEY_VOLUMEUP/DOWN` ❓; der Desktop sollte sie selbst nutzen |
| Kopfhörer | 🟡 | Kernel ✅; über das Profil noch nicht getestet |
| Mikrofon, Hörmuschel, Klinkenerkennung | ❌ | Kernel-Arbeit, siehe FEHLT.md |

## Akku und Laden

| Punkt | Stand | Notiz |
|---|---|---|
| Akkustand im Desktop | ✅ | 16.11.: Leiste zeigt Prozent (Akkuanzeige + Ladetreiber, Patch 0045/0048) |
| Ladegrenze (Server) im Desktop schaltbar | ❌ | heute nur `redmi-akku.sh` / `akku.conf` |
| Eingangsstrom im Ladetreiber | ❌ | Chip stellt am PC-Kabel oft 2400 mA ein; heute korrigiert das Skript |

## Mehr Hardware

| Punkt | Stand | Notiz |
|---|---|---|
| USB-Geräte (Stick, Tastatur, Maus) | ✅ | OTG mit 5 V und Rollenwechsel durch den Kernel (Patches 0045–0047) |
| Kamera | ❌ | Sensoren bekannt (OV32D40, OV08F10, OVSP0821), Sensortreiber fehlen |
| GPS, Lage-, Licht- und Näherungssensor | ❌ / ⏸️ | Sensoren hängen am pmsys-Kern |

## Werkzeuge

| Punkt | Stand | Notiz |
|---|---|---|
| Webbrowser | ❌ | Ubuntus `firefox` ist nur ein Snap-Umweg (snapd auf unserem Kernel ungeprüft ❓); Plan: Firefox als .deb aus Mozillas Paketquelle, sonst GNOME Web; auch für den Tontest aus dem Desktop |
| Desktop auf den PC spiegeln | ✅ | `x11vnc` auf dem Handy, durch SSH zum PC getunnelt, Remmina am PC; per USB-Kabel flüssiger als per WLAN |
