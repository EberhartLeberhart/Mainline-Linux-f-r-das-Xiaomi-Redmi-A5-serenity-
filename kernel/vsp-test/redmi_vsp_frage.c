// SPDX-License-Identifier: GPL-2.0
/*
 * redmi_vsp_frage: fragt den Zustand des Video-Decoders (VSP) ab, ohne ihn
 * anzufassen. Gelesen werden nur Register in immer eingeschalteten Bereichen
 * (PMU, AON) und im laufenden AP-AHB - NICHT der VSP selbst (0x32000000),
 * denn ein Zugriff auf einen abgeschalteten Block kann den Bus aufhaengen.
 *
 * Adressen und Bits aus Unisocs qogirl6-Gerätebaum (UMS9230):
 *   PMU APB   0x64020000: PD_AP_VSP_CFG_0 0x314 (Bit 25 FORCE_SHUTDOWN,
 *                         Bit 24 AUTO_SHUTDOWN_EN), PWR_STATUS_DBG_9 0x514
 *                         (Bits 20:16 Zustand der VSP-Stromdomaene)
 *   AON APB   0x64000000: APB_EB1 0x004 (Bit 5 AP_SYST_EB)
 *   AP AHB    0x20400000: AHB_EB 0x000 (Bit 2 VSP_EB), AHB_RST 0x004
 *                         (Bit 13 VSP_SOFT_RST, Bit 11 VSP_GLOBAL_SOFT_RST)
 * Nur lesen, nichts schreiben.
 */
#include <linux/io.h>
#include <linux/module.h>

static u32 lies(phys_addr_t adr)
{
	void __iomem *p = ioremap(adr, 4);
	u32 v;

	if (!p)
		return 0xdeadbeef;
	v = readl(p);
	iounmap(p);
	return v;
}

static int __init redmi_vsp_frage_init(void)
{
	u32 cfg = lies(0x64020314);
	u32 sts = lies(0x64020514);
	u32 eb1 = lies(0x64000004);
	u32 ahb_eb = lies(0x20400000);
	u32 ahb_rst = lies(0x20400004);

	pr_info("redmi_vsp: PMU PD_AP_VSP_CFG_0 = 0x%08x (FORCE_SHUTDOWN=%u AUTO_SHUTDOWN_EN=%u)\n",
		cfg, !!(cfg & BIT(25)), !!(cfg & BIT(24)));
	pr_info("redmi_vsp: PMU PWR_STATUS_DBG_9 = 0x%08x (VSP-Zustand=0x%02x)\n",
		sts, (sts >> 16) & 0x1f);
	pr_info("redmi_vsp: AON APB_EB1 = 0x%08x (AP_SYST_EB=%u)\n",
		eb1, !!(eb1 & BIT(5)));
	pr_info("redmi_vsp: AP AHB_EB = 0x%08x (VSP_EB=%u)\n",
		ahb_eb, !!(ahb_eb & BIT(2)));
	pr_info("redmi_vsp: AP AHB_RST = 0x%08x (VSP_SOFT_RST=%u VSP_GLOBAL_SOFT_RST=%u)\n",
		ahb_rst, !!(ahb_rst & BIT(13)), !!(ahb_rst & BIT(11)));
	return 0;
}

static void __exit redmi_vsp_frage_exit(void)
{
}

module_init(redmi_vsp_frage_init);
module_exit(redmi_vsp_frage_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Redmi A5: Zustand des Video-Decoders (VSP) nur lesend abfragen");
