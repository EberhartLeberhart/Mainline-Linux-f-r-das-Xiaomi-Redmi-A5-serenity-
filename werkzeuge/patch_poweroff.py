#!/usr/bin/env python3
"""
patch_poweroff.py - baut echtes Ausschalten fuer den PMIC SC2730 in spi-sprd-adi.c ein.

Register aus dem Unisoc-Treiber sc27xx-poweroff.c (Redmi A7 Pro, arctic-w-oss):
  SC2730_SLP_CTRL  0x1a48: Bit 2 (LDO_XTL_EN) und Bit 0 (SLP_LDO_PD_EN) loeschen
  SC2730_PWR_PD_HW 0x1820: Bit 0 (PWR_OFF_EN) schreiben -> Strom aus

Nutzung:  python3 patch_poweroff.py ~/ums9230-linux/drivers/spi/spi-sprd-adi.c
Bricht ab, ohne etwas zu aendern, wenn eine Ankerstelle nicht eindeutig gefunden wird.
"""
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "drivers/spi/spi-sprd-adi.c"
src = open(path).read()

if "sprd_adi_poweroff_sc2730" in src:
    sys.exit("Schon eingebaut - nichts zu tun.")

FUNC = r'''
/* REDMI: echtes Ausschalten ueber den PMIC SC2730 (Register aus Unisoc sc27xx-poweroff.c) */
#define REDMI_SC2730_PWR_PD_HW		0x1820
#define REDMI_SC2730_SLP_CTRL		0x1a48
#define REDMI_SC2730_LDO_XTL_EN		BIT(2)
#define REDMI_SC2730_SLP_LDO_PD_EN	BIT(0)
#define REDMI_SC27XX_PWR_OFF_EN		BIT(0)

static int sprd_adi_poweroff_sc2730(struct sys_off_data *data)
{
	struct sprd_adi *sadi = data->cb_data;
	u32 val = 0;

	dev_emerg(sadi->dev, "REDMI: Ausschalten ueber PMIC SC2730\n");
	sprd_adi_read(sadi, REDMI_SC2730_SLP_CTRL, &val);
	val &= ~(REDMI_SC2730_LDO_XTL_EN | REDMI_SC2730_SLP_LDO_PD_EN);
	sprd_adi_write(sadi, REDMI_SC2730_SLP_CTRL, val);
	sprd_adi_write(sadi, REDMI_SC2730_PWR_PD_HW, REDMI_SC27XX_PWR_OFF_EN);

	mdelay(1000);
	dev_emerg(sadi->dev, "REDMI: Ausschalten hat nicht gewirkt\n");
	return NOTIFY_DONE;
}

'''

REG = r'''
	/* REDMI: vor PSCI (Firmware) anmelden - PSCI SYSTEM_OFF bleibt auf diesem Geraet nur stehen */
	if (sadi->data->restart == sprd_adi_restart_ums512) {
		ret = devm_register_sys_off_handler(&pdev->dev, SYS_OFF_MODE_POWER_OFF,
						    SYS_OFF_PRIO_FIRMWARE + 1,
						    sprd_adi_poweroff_sc2730, sadi);
		if (ret)
			return dev_err_probe(&pdev->dev, ret, "can not register poweroff handler\n");
		dev_info(&pdev->dev, "REDMI: Poweroff-Handler SC2730 angemeldet\n");
	}
'''

# 1) Funktion vor sprd_adi_probe einsetzen
anchor1 = "static int sprd_adi_probe(struct platform_device *pdev)"
if src.count(anchor1) != 1:
    sys.exit("FEHLER: Anker 1 (sprd_adi_probe) nicht eindeutig gefunden - nichts geaendert.")
src = src.replace(anchor1, FUNC.lstrip("\n") + anchor1)

# 2) Anmeldung direkt nach dem Block der Restart-Anmeldung einsetzen
anchor2 = '"can not register restart handler\\n");'
if src.count(anchor2) != 1:
    sys.exit("FEHLER: Anker 2 (restart handler) nicht eindeutig gefunden - nichts geaendert.")
pos = src.index(anchor2) + len(anchor2)
close = src.find("\n\t}\n", pos)          # Ende des if (sadi->data->restart) { ... }
if close == -1 or close - pos > 200:
    sys.exit("FEHLER: Ende des Restart-Blocks nicht gefunden - nichts geaendert.")
close += len("\n\t}\n")
src = src[:close] + REG + src[close:]

# 3) mdelay braucht linux/delay.h
if "#include <linux/delay.h>" not in src:
    src = src.replace("#include <linux/", "#include <linux/delay.h>\n#include <linux/", 1)

open(path, "w").write(src)
print("Eingebaut in", path)
print("Pruefen mit:  cd ~/ums9230-linux && git diff drivers/spi/spi-sprd-adi.c")
