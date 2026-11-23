// SPDX-License-Identifier: GPL-2.0
/*
 * redmi_vsp_drv: erster Treiber fuer den Video-Decoder (VSP) des UMS9230
 * (Redmi A5), Etappe 3a + 3b.
 *
 * Bindet sich an den Geraetebaum-Knoten "sprd,ums9230-vsp" (Patch 0064),
 * schaltet Strom (Stromdomaene ap_vsp ueber Runtime-PM) und Takte ein und
 * prueft dann die IOMMU:
 *
 *  1. Hat sprd-iommu sie beim Anbinden eingerichtet? Erwartet: CFG = 0x0b
 *     (an), FIRST_PPN != 0 (Seitentabelle), VPN_RANGE = 0xffff (256 MiB).
 *     Die Register werden NUR GELESEN.
 *  2. Erster Abbildungstest: 64 KiB per dma_alloc_coherent() anlegen. Die
 *     DMA-Adresse muss eine IOMMU-Adresse unter 256 MiB sein, und die IOMMU
 *     muss sie auf echten Speicher abbilden (iommu_iova_to_phys).
 *
 * Etappe 3b (Patch 0065):
 *  3. Globale VSP-Register lesen (Interrupt, AXI-Status), dann den VSP
 *     zuruecksetzen (VSP_SOFT_RST + VSP_GLOBAL_SOFT_RST, NICHT den IOMMU-Reset
 *     VSP_VAU_SOFT_RST) und alles noch einmal lesen. Frage an den Chip:
 *     ueberlebt die IOMMU-Einstellung (CFG, FIRST_PPN) den VSP-Reset?
 *  4. Interrupt SPI 34 anmelden, Anstehendes loeschen (alle Quellen
 *     maskiert) und 100 ms beobachten - im Ruhezustand darf nichts kommen.
 *  Danach laeuft der Abbildungstest (2) - nach dem Reset.
 *
 * Der VSP selbst rechnet noch nichts. Beim Entladen wird alles freigegeben.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

/* IOMMU-Register am Anfang des VSP-Fensters (Bauart VAU, wie sprd-iommu.c) */
#define MMU_VERSION		0x00
#define MMU_CFG			0x04
#define MMU_FIRST_PPN		0x10
#define MMU_DEFAULT_PPN_RD	0x14
#define MMU_DEFAULT_PPN_WR	0x18
#define MMU_FIRST_VPN		0x1c
#define MMU_VPN_RANGE		0x20

/* Globale VSP-Register ab +0x1000 (Unisocs vsp_common.h) */
#define GLB_BASE		0x1000
#define VSP_INT_STS		0x00
#define VSP_INT_MASK		0x04
#define VSP_INT_CLR		0x08
#define VSP_INT_RAW		0x0c
#define VSP_AXI_STS		0x1c
#define VSP_INT_ALL		0x1fff	/* bekannt: BIT(0) BSM_BUF_OVF, BIT(4) VLD_ERR, BIT(5) TIMEOUT */

#define TEST_SIZE		SZ_64K

struct redmi_vsp {
	struct device *dev;
	void __iomem *base;
	struct clk *clk_eb;
	struct clk *clk_core;
	struct reset_control *rst_core;
	struct reset_control *rst_global;
	int irq;
	atomic_t irq_count;
};

static void redmi_vsp_glb_zeigen(struct redmi_vsp *vsp, const char *wann)
{
	void __iomem *g = vsp->base + GLB_BASE;

	dev_info(vsp->dev, "%s: GLB INT_STS 0x%08x MASK 0x%08x RAW 0x%08x AXI_STS 0x%08x | MMU CFG 0x%08x FIRST_PPN 0x%08x FIRST_VPN 0x%08x\n",
		 wann, readl(g + VSP_INT_STS), readl(g + VSP_INT_MASK),
		 readl(g + VSP_INT_RAW), readl(g + VSP_AXI_STS),
		 readl(vsp->base + MMU_CFG), readl(vsp->base + MMU_FIRST_PPN),
		 readl(vsp->base + MMU_FIRST_VPN));
}

static int redmi_vsp_reset(struct redmi_vsp *vsp)
{
	int ret;

	/* wie Unisocs VSP_RESET: beide Bits setzen, dann beide loeschen */
	ret = reset_control_assert(vsp->rst_core);
	if (ret)
		return ret;
	ret = reset_control_assert(vsp->rst_global);
	if (ret)
		goto out_core;
	udelay(10);
	ret = reset_control_deassert(vsp->rst_global);
out_core:
	reset_control_deassert(vsp->rst_core);
	return ret;
}

static irqreturn_t redmi_vsp_irq(int irq, void *data)
{
	struct redmi_vsp *vsp = data;
	void __iomem *g = vsp->base + GLB_BASE;
	u32 raw = readl(g + VSP_INT_RAW);

	if (!(raw & VSP_INT_ALL))
		return IRQ_NONE;
	atomic_inc(&vsp->irq_count);
	writel(raw & VSP_INT_ALL, g + VSP_INT_CLR);
	dev_info_ratelimited(vsp->dev, "Interrupt: RAW 0x%08x\n", raw);
	return IRQ_HANDLED;
}

static void redmi_vsp_mmu_zeigen(struct redmi_vsp *vsp)
{
	static const struct {
		unsigned int off;
		const char *name;
	} regs[] = {
		{ MMU_VERSION, "VERSION" },
		{ MMU_CFG, "CFG" },
		{ MMU_FIRST_PPN, "FIRST_PPN" },
		{ MMU_DEFAULT_PPN_RD, "DEFAULT_PPN_RD" },
		{ MMU_DEFAULT_PPN_WR, "DEFAULT_PPN_WR" },
		{ MMU_FIRST_VPN, "FIRST_VPN" },
		{ MMU_VPN_RANGE, "VPN_RANGE" },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(regs); i++)
		dev_info(vsp->dev, "MMU +0x%02x %-15s = 0x%08x\n", regs[i].off,
			 regs[i].name, readl(vsp->base + regs[i].off));
}

static void redmi_vsp_maptest(struct redmi_vsp *vsp)
{
	struct iommu_domain *dom = iommu_get_domain_for_dev(vsp->dev);
	dma_addr_t iova;
	phys_addr_t phys;
	u32 *buf;

	if (!dom) {
		dev_err(vsp->dev, "keine IOMMU-Domaene - iommus im Geraetebaum?\n");
		return;
	}
	dev_info(vsp->dev, "IOMMU-Domaene: Typ 0x%x, Bereich 0x%llx-0x%llx\n",
		 dom->type, (u64)dom->geometry.aperture_start,
		 (u64)dom->geometry.aperture_end);

	buf = dma_alloc_coherent(vsp->dev, TEST_SIZE, &iova, GFP_KERNEL);
	if (!buf) {
		dev_err(vsp->dev, "dma_alloc_coherent(%u) fehlgeschlagen\n", TEST_SIZE);
		return;
	}
	buf[0] = 0x52454431;	/* "RED1" - Muster, falls der VSP es spaeter liest */

	phys = iommu_iova_to_phys(dom, iova);
	dev_info(vsp->dev, "Puffer %u KiB: IOVA 0x%llx -> phys 0x%llx (%s)\n",
		 TEST_SIZE / SZ_1K, (u64)iova, (u64)phys,
		 phys && iova <= dom->geometry.aperture_end ?
		 "abgebildet" : "NICHT abgebildet");
	phys = iommu_iova_to_phys(dom, iova + TEST_SIZE - SZ_4K);
	dev_info(vsp->dev, "letzte Seite: IOVA 0x%llx -> phys 0x%llx\n",
		 (u64)(iova + TEST_SIZE - SZ_4K), (u64)phys);

	dma_free_coherent(vsp->dev, TEST_SIZE, buf, iova);
	dev_info(vsp->dev, "Puffer wieder freigegeben\n");
}

static int redmi_vsp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct redmi_vsp *vsp;
	struct resource *res;
	int ret;

	vsp = devm_kzalloc(dev, sizeof(*vsp), GFP_KERNEL);
	if (!vsp)
		return -ENOMEM;
	vsp->dev = dev;

	/*
	 * Das Fenster beginnt mit den IOMMU-Registern, die sprd-iommu schon
	 * belegt hat: abbilden, ohne den Bereich anzufordern.
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;
	vsp->base = devm_ioremap(dev, res->start, resource_size(res));
	if (!vsp->base)
		return -ENOMEM;

	vsp->clk_eb = devm_clk_get(dev, "enable");
	if (IS_ERR(vsp->clk_eb))
		return dev_err_probe(dev, PTR_ERR(vsp->clk_eb), "Takt enable\n");
	vsp->clk_core = devm_clk_get(dev, "core");
	if (IS_ERR(vsp->clk_core))
		return dev_err_probe(dev, PTR_ERR(vsp->clk_core), "Takt core\n");

	vsp->rst_core = devm_reset_control_get_exclusive(dev, "core");
	if (IS_ERR(vsp->rst_core))
		return dev_err_probe(dev, PTR_ERR(vsp->rst_core), "Reset core\n");
	vsp->rst_global = devm_reset_control_get_exclusive(dev, "global");
	if (IS_ERR(vsp->rst_global))
		return dev_err_probe(dev, PTR_ERR(vsp->rst_global), "Reset global\n");
	vsp->irq = platform_get_irq(pdev, 0);
	if (vsp->irq < 0)
		return vsp->irq;
	atomic_set(&vsp->irq_count, 0);

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return dev_err_probe(dev, ret, "DMA-Maske\n");

	platform_set_drvdata(pdev, vsp);
	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;
	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return dev_err_probe(dev, ret, "Strom (ap_vsp)\n");

	ret = clk_prepare_enable(vsp->clk_core);
	if (ret)
		goto out_pm;
	ret = clk_prepare_enable(vsp->clk_eb);
	if (ret)
		goto out_core;

	dev_info(dev, "Strom und Takt an, vsp-clk %lu Hz\n", clk_get_rate(vsp->clk_core));
	redmi_vsp_mmu_zeigen(vsp);

	/* Etappe 3b: Reset und Interrupt */
	/*
	 * Beweis fuer den Reset: ein Muster in die Interrupt-Maske schreiben
	 * (nur bekannte Bits, alle Quellen bleiben ohne Takt-/Arbeitsauftrag
	 * still) - ein echter Reset muss es wieder auf 0 setzen.
	 */
	writel(BIT(0) | BIT(4) | BIT(5), vsp->base + GLB_BASE + VSP_INT_MASK);
	redmi_vsp_glb_zeigen(vsp, "vor Reset ");
	ret = redmi_vsp_reset(vsp);
	if (ret) {
		dev_err(dev, "Reset fehlgeschlagen: %d\n", ret);
		goto out_clk;
	}
	redmi_vsp_glb_zeigen(vsp, "nach Reset");
	dev_info(dev, "Reset-Probe: MASK nach Reset 0x%08x (%s)\n",
		 readl(vsp->base + GLB_BASE + VSP_INT_MASK),
		 readl(vsp->base + GLB_BASE + VSP_INT_MASK) ? "NICHT zurueckgesetzt" : "zurueckgesetzt");

	writel(0, vsp->base + GLB_BASE + VSP_INT_MASK);
	writel(VSP_INT_ALL, vsp->base + GLB_BASE + VSP_INT_CLR);
	ret = devm_request_irq(dev, vsp->irq, redmi_vsp_irq, 0, dev_name(dev), vsp);
	if (ret) {
		dev_err(dev, "Interrupt %d nicht anmeldbar: %d\n", vsp->irq, ret);
		goto out_clk;
	}
	msleep(100);
	dev_info(dev, "Interrupt %d angemeldet, in 100 ms %d mal ausgeloest (erwartet: 0)\n",
		 vsp->irq, atomic_read(&vsp->irq_count));
	/*
	 * Ohne Takt darf der Handler die Register nicht lesen (Bus haengt):
	 * Interrupt aus, bevor die Takte unten abgeschaltet werden.
	 */
	disable_irq(vsp->irq);

	redmi_vsp_maptest(vsp);

	clk_disable_unprepare(vsp->clk_eb);
	clk_disable_unprepare(vsp->clk_core);
	pm_runtime_put(dev);
	dev_info(dev, "Etappe 3b fertig - Treiber bleibt gebunden (rmmod zum Entfernen)\n");
	return 0;

out_clk:
	clk_disable_unprepare(vsp->clk_eb);

out_core:
	clk_disable_unprepare(vsp->clk_core);
out_pm:
	pm_runtime_put(dev);
	return ret;
}

static const struct of_device_id redmi_vsp_of_match[] = {
	{ .compatible = "sprd,ums9230-vsp" },
	{ }
};
MODULE_DEVICE_TABLE(of, redmi_vsp_of_match);

static struct platform_driver redmi_vsp_driver = {
	.probe = redmi_vsp_probe,
	.driver = {
		.name = "redmi-vsp",
		.of_match_table = redmi_vsp_of_match,
	},
};
module_platform_driver(redmi_vsp_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Redmi A5: Video-Decoder (VSP), Etappe 3a/3b - Strom, Takt, IOMMU, Reset, Interrupt");
