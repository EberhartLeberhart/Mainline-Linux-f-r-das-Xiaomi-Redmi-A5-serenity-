// SPDX-License-Identifier: GPL-2.0
/*
 * redmi_vsp_auto: Frage an die PMU - haelt AUTO_SHUTDOWN_EN (Bit 24) den VSP
 * wach, obwohl FORCE_SHUTDOWN (Bit 25) gesetzt ist?
 *
 * Loescht NUR Bit 24 von PD_AP_VSP_CFG_0 (0x64020314) ueber die Loesch-Adresse
 * (+0x2000, aendert keine anderen Bits) und fragt danach den Zustand ab.
 * Nur sinnvoll, wenn die Domaene laut Kernel aus ist (pm_genpd_summary:
 * ap_vsp off) - dann ist "wirklich aus" genau der Zustand, den der Kernel
 * ohnehin annimmt. Schaltet sie der Kernel spaeter ein, loescht er beide Bits.
 */
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>

#define PMU_VSP_CFG	0x64020314
#define PMU_VSP_CLR	(PMU_VSP_CFG + 0x2000)
#define PMU_PWR_STS	0x64020514
#define VSP_AUTO_SD	BIT(24)
#define VSP_FORCE_SD	BIT(25)
#define VSP_STATE_MASK	0x1f0000

static int __init redmi_vsp_auto_init(void)
{
	void __iomem *cfg = ioremap(PMU_VSP_CFG, 4);
	void __iomem *clr = ioremap(PMU_VSP_CLR, 4);
	void __iomem *sts = ioremap(PMU_PWR_STS, 4);
	u32 v, zustand = 0;
	int i;

	if (!cfg || !clr || !sts)
		goto raus;

	v = readl(cfg);
	pr_info("redmi_vsp: vorher CFG 0x%08x (FORCE=%d AUTO=%d), Zustand 0x%02x\n",
		v, !!(v & VSP_FORCE_SD), !!(v & VSP_AUTO_SD),
		(readl(sts) & VSP_STATE_MASK) >> 16);

	if (!(v & VSP_FORCE_SD) || !(v & VSP_AUTO_SD)) {
		pr_info("redmi_vsp: nicht FORCE=1 und AUTO=1 - nichts geaendert\n");
		goto raus;
	}

	writel(VSP_AUTO_SD, clr);
	for (i = 0; i < 100; i++) {
		udelay(300);
		zustand = (readl(sts) & VSP_STATE_MASK) >> 16;
		if (zustand == 0x07)
			break;
	}
	v = readl(cfg);
	pr_info("redmi_vsp: nach AUTO=0: CFG 0x%08x (FORCE=%d AUTO=%d), Zustand 0x%02x nach %d x 300 us\n",
		v, !!(v & VSP_FORCE_SD), !!(v & VSP_AUTO_SD), zustand, i + 1);
raus:
	if (cfg)
		iounmap(cfg);
	if (clr)
		iounmap(clr);
	if (sts)
		iounmap(sts);
	return -EAGAIN;
}

module_init(redmi_vsp_auto_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Redmi A5: haelt AUTO_SHUTDOWN_EN den VSP wach?");
