# Idee: Fingerabdrucksensor als Mini-3D-Scanner (Profilometer)

Stand: 28.10.2026. Fernziel, ganz hinten auf der Liste.

- Das Redmi A5 hat einen Fingerabdrucksensor an der Seite (in der Einschalttaste). Xiaomis Overlay kennt zwei
  mögliche Chips: `sprd,goodix-fp` und `sprd,silead-fp` (Reset ap_gpio 121, Interrupt ap_gpio 29) – welcher verbaut ist,
  per Kennungsregister abfragen.
- Kapazitiver Sensor: liefert ein Graustufen-Rohbild (~500 dpi). Der Grauwert hängt vom Abstand der leitenden Oberfläche
  ab (etwa C ~ 1/Abstand) → Höhenkarte → 3D-Oberfläche.
- Grenzen: nur wenige Zehntel mm Messtiefe, nicht linear → mit Referenzen eichen (Fühlerlehren, gefrästes Stufenmuster);
  nur Leitfähiges/Feuchtes in direktem Kontakt (sonst z. B. Graphitspray); automatische Verstärkung des Chips muss
  abschaltbar sein; winzige Fläche → mehrere Aufnahmen zusammensetzen.
- Nutzen: Prägungen, Gravuren, Kratzer, Verschleiß an Metallteilen, Leiterbahnen in 3D.
- Vergleich: der Sensor im Lenovo T490 (Synaptics, „Match on Chip“) gibt kein Bild heraus – nur das Redmi taugt dafür.
