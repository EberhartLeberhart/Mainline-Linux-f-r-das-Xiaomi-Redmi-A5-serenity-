#!/usr/bin/env python3
# patch_dvfs_debug.py - Kontrollausgaben in den cpufreq-Treiber einbauen, damit man sieht,
# an welchem Schritt der Kernel haengt, und den Treiber als Modul baubar machen.
# NUR ZUM TESTEN - nicht committen. Rueckgaengig:
#   git -C ~/ums9230-linux checkout drivers/cpufreq/sprd-cpufreq-v2.c drivers/cpufreq/Kconfig.arm
# Nutzung: python3 ~/redmi-tools/patch_dvfs_debug.py
import os, re, sys

K = os.path.expanduser("~/ums9230-linux")
SRC = f"{K}/drivers/cpufreq/sprd-cpufreq-v2.c"
KCONF = f"{K}/drivers/cpufreq/Kconfig.arm"
MARK = "REDMI-DVFS"

def fehler(t):
    print("FEHLER:", t); sys.exit(1)

lines = open(SRC).read().split("\n")
if any(MARK in l for l in lines):
    fehler("Datei ist schon gepatcht - erst mit git checkout zuruecksetzen")

# 1) jeden SMC-Aufruf vorher und nachher melden. Ein do{}while(0) ist EINE Anweisung,
#    funktioniert also auch hinter einem 'if' ohne geschweifte Klammern.
last_inc = max(i for i, l in enumerate(lines) if l.startswith("#include"))
makro = [
    "",
    "/* REDMI-DVFS: Testausgaben - jeder Firmware-Aufruf wird vorher und nachher gemeldet */",
    "#undef arm_smccc_smc",
    "#define arm_smccc_smc(id, ...) do {\t\t\t\t\t\t\\",
    "\tpr_info(\"REDMI-DVFS %s:%d SMC %#x ...\\n\", __func__, __LINE__, (u32)(id));\t\\",
    "\t__arm_smccc_smc(id, __VA_ARGS__, NULL);\t\t\t\t\t\\",
    "\tpr_info(\"REDMI-DVFS %s:%d zurueck\\n\", __func__, __LINE__);\t\t\\",
    "} while (0)",
]
lines[last_inc + 1:last_inc + 1] = makro

# 2) einzelne Schritte markieren (nur vor einfachen Anweisungen, nie hinter 'if' ohne Klammer)
ziele = [
    (r"^\s*cell = of_nvmem_cell_get\(", 'pr_info("REDMI-DVFS %pOF: hole dvfs_bin-Zelle\\n", np);'),
    (r"^\s*buf = nvmem_cell_read\(",    'pr_info("REDMI-DVFS %pOF: lese dvfs_bin aus eFuse\\n", np);'),
    (r"^\s*ret = dev_pm_opp_add\(",     'pr_info("REDMI-DVFS Stufe %d: %lu Hz, %lu uV\\n", i, res.a1, res.a2);'),
    (r"^\s*return cpufreq_register_driver\(", 'pr_info("REDMI-DVFS: melde Treiber an (danach startet der Regler)\\n");'),
]
for muster, text in ziele:
    idx = [i for i, l in enumerate(lines) if re.search(muster, l)]
    if len(idx) != 1:
        fehler(f"'{muster}' {len(idx)}x gefunden statt 1x - Datei anders als erwartet")
    i = idx[0]
    prev = next(l.strip() for l in reversed(lines[:i]) if l.strip())
    if re.match(r"(if|else|for|while)\b", prev) and not prev.endswith("{"):
        fehler(f"Zeile {i+1} steht hinter '{prev}' ohne Klammer - nicht sicher")
    einzug = re.match(r"^\s*", lines[i]).group(0)
    lines.insert(i, einzug + text)

if not any("MODULE_LICENSE" in l for l in lines):
    lines.append('MODULE_LICENSE("GPL");')
open(SRC, "w").write("\n".join(lines))
print(f">>> {SRC}: Testausgaben eingebaut")

# 3) Kconfig: bool -> tristate, damit =m moeglich ist
kc = open(KCONF).read().split("\n")
try:
    s = kc.index("config ARM_SPRD_CPUFREQ_V2")
except ValueError:
    fehler("config ARM_SPRD_CPUFREQ_V2 nicht in Kconfig.arm")
for j in range(s + 1, min(s + 6, len(kc))):
    if kc[j].strip().startswith("bool"):
        kc[j] = kc[j].replace("bool", "tristate", 1)
        open(KCONF, "w").write("\n".join(kc))
        print(">>> Kconfig.arm: bool -> tristate")
        break
    if kc[j].strip().startswith("tristate"):
        print(">>> Kconfig.arm: ist schon tristate"); break
else:
    fehler("Typzeile von ARM_SPRD_CPUFREQ_V2 nicht gefunden")
print(">>> fertig. Kontrolle: git -C ~/ums9230-linux diff --stat")
