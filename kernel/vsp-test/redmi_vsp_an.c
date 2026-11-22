// SPDX-License-Identifier: GPL-2.0
/*
 * redmi_vsp_an: schaltet den Video-Decoder (VSP) des UMS9230 kurz ein,
 * liest ein paar Status-Register und schaltet ihn wieder aus.
 *
 * Ablauf wie in Unisocs sprd_vsp_pw_domain.c (GPL-2.0, Jolla-C2-Kernel):
 *   PMU 0x64020314: AUTO_SHUTDOWN_EN (Bit 24) und FORCE_SHUTDOWN (Bit 25)
 *   loeschen, dann PMU 0x64020514 Bits 20:16 abfragen, bis 0 (alle 300 us,
 *   hoechstens 100 Versuche).
 * Danach die Takte ueber das Takt-Framework: "vsp-eb" (AP-AHB-Tor,
 * CLK_VSP_EB) und "vsp-clk" (CLK_VSP). Erst wenn Strom UND Takt da sind,
 * werden VSP-Register gelesen. Als Lebenszeichen werden kleine Muster in die
 * drei Interrupt-Masken geschrieben und zurueckgelesen (nur wenn kein RAW-Bit
 * steht), danach wieder auf den alten Wert gesetzt.
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

/* aus Unisocs vsp_common.h */
static const struct {
	unsigned int off;
	const char *name;
} vsp_regs[] = {
	{ 0x0010, "ARM_INT_STS" },
	{ 0x0014, "ARM_INT_MASK" },
	{ 0x001c, "ARM_INT_RAW" },
	{ 0x0060, "MMU_INT_STS" },
	{ 0x0068, "MMU_INT_RAW" },
	{ 0x006c, "MMU_INT_MASK" },
	{ 0x1000, "GLB VSP_INT_STS" },
	{ 0x1004, "GLB VSP_INT_MASK" },
	{ 0x100c, "GLB VSP_INT_RAW" },
	{ 0x101c, "GLB VSP_AXI_STS" },
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

static int __init redmi_vsp_an_init(void)
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
		void __iomem *vsp = ioremap(VSP_BASE, VSP_SIZE);

		if (vsp) {
			static const unsigned int masken[] = { 0x1004, 0x0014, 0x006c };
			static const u32 muster[] = { 0x1, 0x5, 0xa };

			for (i = 0; i < ARRAY_SIZE(vsp_regs); i++)
				pr_info("redmi_vsp: VSP +0x%04x %-17s = 0x%08x\n",
					vsp_regs[i].off, vsp_regs[i].name,
					readl(vsp + vsp_regs[i].off));

			/*
			 * Lebenszeichen: Muster in die Interrupt-Masken schreiben
			 * und zuruecklesen. Ungefaehrlich, solange RAW 0 ist und
			 * niemand den Interrupt angemeldet hat; danach wieder 0.
			 */
			if (readl(vsp + 0x100c) || readl(vsp + 0x001c) ||
			    readl(vsp + 0x0068)) {
				pr_info("redmi_vsp: RAW nicht 0 - Schreibprobe ausgelassen\n");
			} else {
				int m, k;

				for (m = 0; m < ARRAY_SIZE(masken); m++) {
					u32 alt_m = readl(vsp + masken[m]);

					for (k = 0; k < ARRAY_SIZE(muster); k++) {
						writel(muster[k], vsp + masken[m]);
						pr_info("redmi_vsp: Probe +0x%04x: geschrieben 0x%x, gelesen 0x%08x\n",
							masken[m], muster[k],
							readl(vsp + masken[m]));
					}
					writel(alt_m, vsp + masken[m]);
				}
			}
			iounmap(vsp);
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

module_init(redmi_vsp_an_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Redmi A5: Video-Decoder (VSP) kurz einschalten und Status lesen");
