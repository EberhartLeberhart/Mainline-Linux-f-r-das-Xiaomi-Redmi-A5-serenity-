# Das Redmi A5 als normales Handy – was noch fehlt

Ziel: Das Redmi A5 mit Linux-Desktop soll sich im Alltag bedienen lassen wie unter Android – alles per Touch
und aus dem Desktop heraus, ohne Terminal, ohne Maus und Tastatur.

Diese Liste ist für die **Bedienung**. Was im **Kernel** fehlt, steht in [FEHLT.md](FEHLT.md), der geprüfte Stand in
[STATUS.md](STATUS.md). Mithelfen ist willkommen – am besten ein Punkt pro Pull Request.

Zeichen: ✅ erledigt · 🟡 teilweise · ❌ offen · ⏸️ bewusst angehalten · ❓ Vermutung, noch nicht geprüft

Stand: 16.11.2026 (abends) – Ubuntu 24.04 + Cinnamon 6.0 (lightdm, slick-greeter) auf dem Arbeitshandy;
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
| Bildschirmtastatur am Anmeldebildschirm | ❌ | 20.11.: Ursache gefunden – Onboard stürzt ab, sobald es eingebettet wird (`onboard --xid` in einem `Gtk.Socket` nachgestellt: Ende-Code −11), und genau so startet slick-greeter es. Plan: eigener Handy-Anmeldebildschirm (lightdm-Greeter) mit der `redmi-tastatur`; bis dahin Autologin. Notlösung „Autologin + sofort sperren“ besprochen, nicht umgesetzt |
| Bildschirmtastatur auf dem Sperrbildschirm | ✅ | 20.11.: cinnamon-screensaver hat eine eigene Tastatur (`osk.py`) und zeigt sie von selbst, sobald `screen-keyboard-enabled true` und `activation-mode accessible` gesetzt sind |
| Bildschirmsperre mit PIN per Touch | 🟡 | 20.11.: Passwort per Bildschirmtastatur auf dem Sperrbildschirm geht; eigene PIN statt Passwort offen |
| Desktop stabil und flüssig | ❌ | 20.11.: einmal ganz abgestürzt beim Tastatur-Test, Desktop reagiert zeitweise zäh. Vermutungen ❓: Speicher (3,6 GB, ZRAM), Mehrlast durch die Barrierefreiheit, oder Kernel. Nach dem Fundament untersuchen (`journalctl -b -1`, OOM, pstore) |
| Herunterfahren ohne Hänger | 🟡 | 16.11. einmal ~90 s beim Licht-Dienst gehangen; Dienste brauchen kurze Abbruchzeiten |

## Bildschirm und Einschalttaste

| Punkt | Stand | Notiz |
|---|---|---|
| Display bleibt im Desktop an | ✅ | 16.11.: `redmi-licht.sh` behandelt `DESKTOP=an` wie `START=an` |
| Einschalttaste kurz = Display an/aus | ❌ | heute: Licht an (ohne Abschaltung); Ziel wie Android: an/aus umschalten, Touch schläft mit |
| Helligkeit im Desktop regeln | ✅ | 18.11.: Cinnamon-Regler wirkt (`/sys/class/backlight/31100000.dsi.0`) |
| Display nach Zeit aus (Energieverwaltung) | ❓ | Cinnamon-Einstellung prüfen, Zusammenspiel mit `redmi-licht.sh` |
| 60/90/120 Hz | ✅ | Patches 0050–0052; in den Anzeige-Einstellungen wählbar ❓ |
| Hoch- und Querformat | 🟡 | von Hand drehbar (beim Tester quer); automatisch braucht den Lagesensor (hängt am pmsys-Kern ⏸️) |
| Leiste: Abstand für runde Ecken und Kamera-Aussparung | ❌ | Hostname links oben wird angeschnitten |

## Bedienung per Touch

| Punkt | Stand | Notiz |
|---|---|---|
| Touch als Maus | ✅ | 16.11.: trifft genau (Zuordnung Display/Touch seit 26.10.) |
| Bildschirmtastatur im Desktop bei Textfeldern | ✅ | 20.11.: eigene `redmi-tastatur` (werkzeuge/handy) – Aussehen und Aufteilung wie auf dem Sperrbildschirm, passt in die Breite, blendet sich in Firefox und im Terminal selbst ein und aus („lief einwandfrei“). Grundlage: Barrierefreiheit an (`toolkit-accessibility true`, `screen-keyboard-enabled true`, `activation-mode accessible`). Cinnamons eigene Desktop-Tastatur bleibt unsichtbar (wartet auf Show vom nicht installierten `caribou-gtk-module`); Onboard nicht genutzt. **Leitlinie: diese Tastatur überall auf dem Handy** |
| Größere Bedienelemente | ❌ | Skalierung, Leistenhöhe, Schriftgröße für Finger |
| Gesten (Wischen) | ❌ | Cinnamon-Touch-Gesten prüfen ❓ |
| Mint-Optik | 🟡 | Mint-Y-Symbole installiert; Mint-Themen nicht bei Ubuntu → von Mint (GitHub) holen |

## Netz

| Punkt | Stand | Notiz |
|---|---|---|
| WLAN (unser Weg) | ✅ | `redmi-wlan.service` + wpa_supplicant + systemd-networkd; 16.11.: hält auch mit Desktop-Autologin (Patch 0053, vorher Anmelde-Timeouts) |
| WLAN im Desktop wählen | ❌ | 16.11.: NetworkManager stritt mit unserem Weg um `wlan0` (vermutlich auch ein Absturz, Kernel-Meldung `ieee80211 phy0: mismatch`) → vorerst `wlan0`/`usb0` für NetworkManager **unmanaged**. Ziel: NetworkManager übernimmt `wlan0` ganz, unser Dienst lädt nur den Treiber |
| Bluetooth im Desktop | ❌ | Kernel ✅; 20.11.: Desktop meldet „Gerät antwortet nicht“ – Kette Kernel → rfkill → bluetoothd → Adapter prüfen; Absturzbericht von `blueman-applet` vom 16.11. |
| Mobilfunk | ⏸️ | wartet auf die Antwort der Bundesnetzagentur |
| Uhrzeit aus dem Netz | ✅ | 18.11.: Uhr ging 1 h 23 min vor, kein Zeitdienst installiert → `apt install systemd-timesyncd`, `timedatectl set-ntp true`, `hwclock --systohc`; Zeitzone Europe/Berlin stimmte. Gehört später ins Abbild ❓ RTC-Drift beobachten |

## Ton

| Punkt | Stand | Notiz |
|---|---|---|
| Lautsprecher | ✅ | UCM-Profil + `redmi-ton.service` (15.11.) |
| Lautstärke im Desktop | ✅ | 16.11.: PipeWire + WirePlumber laufen in der Sitzung, Cinnamon-Regler wirkt |
| Laut-/Leise-Tasten | ✅ | 16.11.: Cinnamon nimmt sie selbst, Regler bewegt sich |
| Medien abspielen | ✅ | 16.11.: Firefox spielt YouTube über den Lautsprecher; Medienanzeige mit Pause/Vor/Zurück im Ton-Menü |
| Kopfhörer | 🟡 | Kernel ✅; über das Profil noch nicht getestet |
| Mikrofon | ✅ | 19.11.: erscheint im Desktop als Eingang „Mikrofon“, Pegelanzeige schlägt aus (UCM-Gerät „Mic“) |
| Hörmuschel | ✅ | 19.11.: gibt es nicht extra – derselbe Lautsprecher oben, beim Telefonieren nur leiser |
| Klinkenerkennung | ✅ | 20.11.: Kopfhörer rein → Ton springt von selbst um, raus → zurück auf den Lautsprecher (Patch 0056 + UCM `JackControl`) |

## Akku und Laden

| Punkt | Stand | Notiz |
|---|---|---|
| Akkustand im Desktop | ✅ | 16.11.: Leiste zeigt Prozent (Akkuanzeige + Ladetreiber, Patch 0045/0048) |
| Ladegrenze (Server) im Desktop schaltbar | ❌ | heute nur `redmi-akku.sh` / `akku.conf` |
| Eingangsstrom im Ladetreiber | ✅ | 18.11.: Patches 0054/0055 – PC 500 mA, Netzteil 2000 mA, Treiber prüft Fehl-Erkennung nach |

## Mehr Hardware

| Punkt | Stand | Notiz |
|---|---|---|
| USB-Geräte (Stick, Tastatur, Maus) | ✅ | OTG mit 5 V und Rollenwechsel durch den Kernel (Patches 0045–0047) |
| Kamera | ❌ | Sensoren bekannt (OV32D40, OV08F10, OVSP0821), Sensortreiber fehlen |
| GPS, Lage-, Licht- und Näherungssensor | ❌ / ⏸️ | Sensoren hängen am pmsys-Kern |

## Werkzeuge

| Punkt | Stand | Notiz |
|---|---|---|
| Webbrowser | ✅ | 16.11.: Firefox 160 als arm64-.deb aus Mozillas Paketquelle (kein Snap), dazu `pavucontrol`; Skript `werkzeuge/handy/redmi-browser.sh` |
| Handy und PC verbinden (wie „Smartphone-Link“) | ❌ | Idee 16.11.: KDE Connect auf Handy und PC (Paket `kdeconnect`, läuft auch unter Cinnamon): Zwischenablage, Dateien, Benachrichtigungen, Medien steuern, Handy als Touchpad, Akkustand am PC; später fest ins Abbild |
| Am PC wie ein Android-Handy anmelden (MTP) | ❌ | Idee 20.11.: MTP-Funktion im USB-Gadget (FunctionFS) neben dem USB-Netz, z. B. mit uMTP-Responder; freigegebene Ordner von `tunnel-handy`; Mint erkennt MTP von selbst |
| `net.sh` / `deploy.sh` verlässlicher | ❌ | 20.11.: `net.sh` wartet nur auf die Netzwerkkarte, nicht auf Ping → `deploy.sh` läuft ins Leere; `net.sh` setzt eine Standardroute über USB, die dem Handy das Internet nimmt; Gadget-MAC wechselt (Kartenname am PC wechselt mit) |
| Desktop auf den PC spiegeln | ✅ | `x11vnc` auf dem Handy, durch SSH zum PC getunnelt, Remmina am PC; per USB-Kabel flüssiger als per WLAN |
