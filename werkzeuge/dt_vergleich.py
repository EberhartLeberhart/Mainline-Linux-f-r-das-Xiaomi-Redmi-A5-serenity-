#!/usr/bin/env python3
# dt_vergleich.py - Ton- und WLAN-Knoten aus zwei Geraetebaeumen nebeneinander (nur lesen).
#
#   python3 dt_vergleich.py <Xiaomi.dts|.dtb> <Realme-Datei.dtsi> [weitere .dtsi ...]
#
# .dtb wird mit dtc in Text umgewandelt (apt install device-tree-compiler).
# Ausgabe: je Kennung (compatible) Knotenpfad, reg und status aus jeder Quelle.
import re, subprocess, sys

ZIELE = ["qogirl6-vbc", "mcdt-r2p0", "agdsp", "audio-codec-dig", "sc2730-audio-codec",
         "sipc-virt-bus", "qogirl6-pcm-platform", "fe-dai", "pcm-routing", "audio_sipc",
         "audio-mem", "audio_pipe", "apipe", "audcp", "vbc-v4-codec", "fs15", "fs1599",
         "frsm", "foursemi", "qogirl6-dma", "integrate_marlin", "integrate_gnss",
         "sc2355-sipc-wifi", "wcn_internal_chip", "sprd,sipc\"", "smem", "mailbox"]

def lesen(datei):
    import os
    if os.path.isdir(datei):   # Kopie von /proc/device-tree
        return subprocess.run(["dtc", "-q", "-I", "fs", "-O", "dts", datei],
                              capture_output=True, text=True, check=True).stdout
    if datei.endswith(".dtb"):
        return subprocess.run(["dtc", "-q", "-I", "dtb", "-O", "dts", datei],
                              capture_output=True, text=True, check=True).stdout
    return open(datei, errors="replace").read()

def knoten(text):
    """liefert (pfad, {eigenschaft: wert}) fuer jeden Knoten; nur direkte Eigenschaften"""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    text = re.sub(r"(?m)^\s*(#(include|define|undef|if|ifdef|ifndef|else|endif)\b|/dts-v1/|/plugin/)[^\n]*", "", text)
    stapel, props, aus, puffer = [], [], [], ""
    for teil in re.split(r"([{};])", text):
        if teil == "{":
            name = puffer.strip().split(":")[-1].strip() or "?"
            stapel.append(name); props.append({}); puffer = ""
        elif teil == "}":
            if stapel:
                aus.append((re.sub("/+", "/", "/".join(stapel)), props.pop())); stapel.pop()
            puffer = ""
        elif teil == ";":
            z = " ".join(puffer.split())
            if "=" in z and props:
                k, v = z.split("=", 1); props[-1][k.strip()] = v.strip()
            puffer = ""
        else:
            puffer += teil
    return aus

def main():
    if len(sys.argv) < 3:
        print(__doc__ or "python3 dt_vergleich.py <Xiaomi.dts|.dtb> <Realme.dtsi> [...]"); sys.exit(1)
    quellen = [("XIAOMI", sys.argv[1])] + [("REALME " + d.split("/")[-1], d) for d in sys.argv[2:]]
    alle = []
    for name, d in quellen:
        for pfad, p in knoten(lesen(d)):
            alle.append((name, pfad, p))
    for ziel in ZIELE:
        treffer = [(n, pf, p) for n, pf, p in alle
                   if ziel.strip('"') in p.get("compatible", "") and
                   (not ziel.endswith('"') or p.get("compatible", "").strip().endswith('"sprd,sipc"'))]
        if not treffer:
            continue
        print(f"=== {ziel.strip(chr(34))}")
        for n, pf, p in treffer:
            print(f"  {n:22} {pf}")
            for k in ("compatible", "reg", "status", "memory-region", "sprd,name", "mboxes", "interrupts"):
                if k in p:
                    print(f"  {'':22}   {k} = {p[k][:110]}")
    print("Hinweis: Realme-Werte koennen Makros enthalten (z.B. REG_PMU_...), Xiaomi-Werte sind Zahlen.")

main()
