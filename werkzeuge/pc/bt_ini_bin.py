#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""bt_ini_bin.py - Unisoc-Bluetooth-Konfiguration (ini) in die Binaerform fuer btsprdsipc umwandeln.

Ottos Treiber btsprdsipc (drivers/bluetooth/btsprdsipc.c) laedt beim Start
  sprd/bt_config_pskey.bin  -> Herstellerbefehl 0xfca0 (pskey, enthaelt ab Byte 20 die BT-Adresse)
  sprd/bt_config_rf.bin     -> Herstellerbefehl 0xfca2 (Funk-Einstellungen)
Die Werte stehen in der eigenen odm-Partition als Text: firmware/bt_configure_pskey.ini und
bt_configure_rf.ini. Diese Dateien gehoeren zur Firmware des Geraets: NICHT ins Repo, nur aufs eigene Handy.

Packregel (wie der In-Kernel-Parser btsprd_ini.c im beebono-Baum, linux-mainline-sprd):
  - Kopf "# ... [Total Length=N]" = Gesamtlaenge, wird am Ende geprueft
  - jedes Feld beginnt mit einem Kommentar "/L=N" (Feldlaenge in Bytes)
  - danach eine oder mehrere Zeilen "name = w0, w1, ..." (Zahlen dezimal oder 0x..)
  - Bytes je Wert = N / Anzahl Werte (1..4), jeder Wert little-endian, Felder in Reihenfolge

Nutzung:  bt_ini_bin.py <eingabe.ini> <ausgabe.bin>
Ausgabe:  Zusammenfassung (Felder, Laengen) - keine Werte.
"""
import re
import sys


def zahl(t):
    m = re.match(r"\s*(0[xX][0-9a-fA-F]+|[0-9]+)", t)
    if not m:
        raise ValueError(f"keine Zahl: {t!r}")
    return int(m.group(1), 0)


def packen(text):
    gesamt = None
    felder = []          # (L, [werte], name)
    cur_l, werte, name = None, [], ""
    aus = bytearray()

    def abschliessen():
        if cur_l is None:
            return
        if not werte or cur_l % len(werte):
            raise ValueError(f"Feld {name!r}: L={cur_l} nicht durch {len(werte)} Werte teilbar")
        bpw = cur_l // len(werte)
        if not 1 <= bpw <= 4:
            raise ValueError(f"Feld {name!r}: {bpw} Bytes je Wert (L={cur_l}, {len(werte)} Werte)")
        for w in werte:
            aus.extend((w & ((1 << (8 * bpw)) - 1)).to_bytes(bpw, "little"))
        felder.append((cur_l, len(werte), name))

    for zeile in text.splitlines():
        s = zeile.strip()
        if not s:
            continue
        if s.startswith("#"):
            if gesamt is None:
                m = re.search(r"Total Length=\s*(0[xX][0-9a-fA-F]+|[0-9]+)", s)
                if m:
                    gesamt = int(m.group(1), 0)
            m = re.search(r"/L=\s*(0[xX][0-9a-fA-F]+|[0-9]+)", s)
            if m:
                abschliessen()
                cur_l, werte, name = int(m.group(1), 0), [], ""
            continue
        if "=" not in s:
            continue
        links, rechts = s.split("=", 1)
        if not name:
            name = links.strip()
        for t in re.split(r"[,\s]+", rechts):
            if t:
                werte.append(zahl(t))
    abschliessen()

    if gesamt is None:
        raise ValueError("kein Kopf [Total Length=N] gefunden")
    if len(aus) != gesamt:
        raise ValueError(f"{len(aus)} Bytes gepackt, Kopf sagt {gesamt}")
    return bytes(aus), felder


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    text = open(sys.argv[1], encoding="latin-1").read()
    daten, felder = packen(text)
    open(sys.argv[2], "wb").write(daten)
    print(f"{sys.argv[1]}: {len(felder)} Felder, {len(daten)} Bytes = Total Length  -> {sys.argv[2]}")
    for l, n, name in felder:
        print(f"   L={l:4d}  {n:3d} Werte x {l // n} B  {name}")


if __name__ == "__main__":
    main()
