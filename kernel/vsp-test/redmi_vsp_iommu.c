// SPDX-License-Identifier: GPL-2.0
/*
 * redmi_vsp_iommu: schaltet den Video-Decoder (VSP) des UMS9230 kurz ein und
 * liest die Register seiner IOMMU ("iommuvaul6-vsp", VSP + 0x0..0xa0).
 * NUR LESEN - in die IOMMU wird nichts geschrieben.
 *
 * Frage: Passt der mainline-Treiber drivers/iommu/sprd-iommu.c? Er erkennt
 * die Bauart an VERSION (+0x0) Bits 15:8 (0 = EX, 1 = VAU) und nutzt bei VAU
 * CFG +0x4, UPDATE +0x8, AUTH +0xc, FIRST_PPN +0x10, DEFAULT_PPN_RD/WR
 * +0x14/+0x18, FIRST_VPN +0x1c, VPN_RANGE +0x20. Unisocs VAU-Treiber
 * (sprd_iommuvau_hal_register.h) nutzt dieselben Stellen, dazu MMU_INT_EN
 * +0xa0 und Fehleradressen +0x44..+0x68.
 *
 *
 * Ablauf wie in Unisocs sprd_vsp_pw_domain.c (GPL-2.0, Jolla-C2-Kernel):
 *   PMU 0x64020314: AUTO_SHUTDOWN_EN (Bit 24) und FORCE_SHUTDOWN (Bit 25)
 *   loeschen, dann PMU 0x64020514 Bits 20:16 abfragen, bis 0 (alle 300 us,
 *   hoechstens 100 Versuche).
 * Danach die Takte ueber das Takt-Framework: "vsp-eb" (AP-AHB-Tor,
 * CLK_VSP_EB) und "vsp-clk" (CLK_VSP). Erst wenn Strom UND Takt da sind,
 * werden Register gelesen.
 * Zum Schluss: Takte aus, PMU-Bits wie vorgefunden zuruecksetzen.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>

#define PMU_VSP_CFG	0x64020314
#define PMU_PWR_STS	0x64020514
#define VSP_AUTO_SD	BIT(24)
#define VSP_FORCE_SD	BIT(25)
#define VSP_STATE_MASK	0x1f0000

#define VSP_BASE	0x32000000
#define VSP_SIZE	0xc000

/* Stellen aus mainline sprd-iommu.c und Unisocs sprd_iommuvau_hal_register.h */
static const struct {
	unsigned int off;
	const char *name;
} mmu_regs[] = {
	{ 0x00, "VERSION" },
	{ 0x04, "CFG (EN/GATE/VAOR)" },
	{ 0x08, "UPDATE" },
	{ 0x0c, "AUTH_CFG" },
	{ 0x10, "FIRST_PPN" },
	{ 0x14, "DEFAULT_PPN_RD" },
	{ 0x18, "DEFAULT_PPN_WR" },
	{ 0x1c, "FIRST_VPN" },
	{ 0x20, "VPN_RANGE" },
	{ 0x24, "MINI_PPN1" },
	{ 0x28, "PPN1_RANGE" },
	{ 0x2c, "MINI_PPN2" },
	{ 0x30, "PPN2_RANGE" },
	{ 0x34, "PT_UPDATE_QOS" },
	{ 0x44, "PAOUT_RD_VA" },
	{ 0x50, "PAOUT_WR_PA" },
	{ 0x54, "VARD_OUT_ADDR" },
	{ 0x58, "VAWR_OUT_ADDR" },
	{ 0x5c, "INVALID_RD_ADDR" },
	{ 0x60, "INVALID_WR_ADDR" },
	{ 0x64, "UNSECURE_RD_ADDR" },
	{ 0x68, "UNSECURE_WR_ADDR" },
	{ 0x6c, "(VSP: MMU_INT_MASK)" },
	{ 0xa0, "MMU_INT_EN" },
};

static struct clk *hole_takt(const char *compat, unsigned int idx)
{
	struct of_phandle_args args = { };
	struct clk *c;

	args.np = of_find_compatible_node(NULL, NULL, compat);
	if (!args.np)
		return ERR_PTR(-ENODEV);
	args.args_count = 1;
	args.args[0] = idx;
	c = of_clk_get_from_provider(&args);
	of_node_put(args.np);
	return c;
}

static int __init redmi_vsp_iommu_init(void)
{
	void __iomem *cfg = ioremap(PMU_VSP_CFG, 4);
	void __iomem *sts = ioremap(PMU_PWR_STS, 4);
	struct clk *eb = NULL, *vclk = NULL;
	u32 alt, zustand = 0;
	int i, ret = 0;

	if (!cfg || !sts) {
		ret = -ENOMEM;
		goto raus;
	}

	alt = readl(cfg);
	pr_info("redmi_vsp: vorher CFG 0x%08x, Zustand 0x%02x\n",
		alt, (readl(sts) & VSP_STATE_MASK) >> 16);

	/* einschalten */
	writel(alt & ~(VSP_AUTO_SD | VSP_FORCE_SD), cfg);
	for (i = 0; i < 100; i++) {
		udelay(300);
		zustand = readl(sts) & VSP_STATE_MASK;
		if (!zustand)
			break;
	}
	if (zustand) {
		pr_err("redmi_vsp: Strom kommt nicht (Zustand 0x%02x nach %d Versuchen) - Abbruch\n",
		       zustand >> 16, i);
		ret = -ETIMEDOUT;
		goto strom_aus;
	}
	pr_info("redmi_vsp: Strom an nach %d x 300 us\n", i + 1);

	eb = hole_takt("sprd,ums9230-apahb-gate", 2);	/* CLK_VSP_EB */
	vclk = hole_takt("sprd,ums9230-ap-clk", 28);	/* CLK_VSP */
	if (IS_ERR(eb) || IS_ERR(vclk)) {
		pr_err("redmi_vsp: Takte nicht gefunden (eb %ld, vsp %ld) - Abbruch\n",
		       IS_ERR(eb) ? PTR_ERR(eb) : 0L,
		       IS_ERR(vclk) ? PTR_ERR(vclk) : 0L);
		ret = -ENODEV;
		goto takte_weg;
	}

	ret = clk_prepare_enable(vclk);
	if (ret) {
		pr_err("redmi_vsp: vsp-clk laesst sich nicht einschalten (%d)\n", ret);
		goto takte_weg;
	}
	ret = clk_prepare_enable(eb);
	if (ret) {
		pr_err("redmi_vsp: vsp-eb laesst sich nicht einschalten (%d)\n", ret);
		clk_disable_unprepare(vclk);
		goto takte_weg;
	}
	pr_info("redmi_vsp: Takt an, vsp-clk %lu Hz\n", clk_get_rate(vclk));

	{
		void __iomem *mmu = ioremap(VSP_BASE, 0x100);

		if (mmu) {
			u32 ver = readl(mmu);

			for (i = 0; i < ARRAY_SIZE(mmu_regs); i++)
				pr_info("redmi_vsp: MMU +0x%02x %-20s = 0x%08x\n",
					mmu_regs[i].off, mmu_regs[i].name,
					readl(mmu + mmu_regs[i].off));
			pr_info("redmi_vsp: Bauart (VERSION 15:8) = %u -> %s\n",
				(ver >> 8) & 0xff,
				((ver >> 8) & 0xff) == 1 ? "VAU, passt zu sprd-iommu" :
				((ver >> 8) & 0xff) == 0 ? "EX" : "unbekannt");
			iounmap(mmu);
		}
	}

	clk_disable_unprepare(eb);
	clk_disable_unprepare(vclk);

takte_weg:
	if (!IS_ERR_OR_NULL(eb))
		clk_put(eb);
	if (!IS_ERR_OR_NULL(vclk))
		clk_put(vclk);
strom_aus:
	/* Strom wie vorgefunden */
	writel(alt, cfg);
	udelay(300);
	pr_info("redmi_vsp: nachher CFG 0x%08x, Zustand 0x%02x\n",
		readl(cfg), (readl(sts) & VSP_STATE_MASK) >> 16);
raus:
	if (cfg)
		iounmap(cfg);
	if (sts)
		iounmap(sts);
	/* Modul bleibt nicht geladen, egal wie es ausging */
	return ret ? ret : -EAGAIN;
}

module_init(redmi_vsp_iommu_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Redmi A5: IOMMU des Video-Decoders (VSP) lesen");
