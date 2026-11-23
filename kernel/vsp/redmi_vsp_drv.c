// SPDX-License-Identifier: GPL-2.0
/*
 * redmi_vsp_drv: erster Treiber fuer den Video-Decoder (VSP) des UMS9230
 * (Redmi A5), Etappe 3a.
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
 * Der VSP selbst rechnet noch nichts. Beim Entladen wird alles freigegeben.
 */
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

/* IOMMU-Register am Anfang des VSP-Fensters (Bauart VAU, wie sprd-iommu.c) */
#define MMU_VERSION		0x00
#define MMU_CFG			0x04
#define MMU_FIRST_PPN		0x10
#define MMU_DEFAULT_PPN_RD	0x14
#define MMU_DEFAULT_PPN_WR	0x18
#define MMU_FIRST_VPN		0x1c
#define MMU_VPN_RANGE		0x20

#define TEST_SIZE		SZ_64K

struct redmi_vsp {
	struct device *dev;
	void __iomem *base;
	struct clk *clk_eb;
	struct clk *clk_core;
};

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
	redmi_vsp_maptest(vsp);

	clk_disable_unprepare(vsp->clk_eb);
	clk_disable_unprepare(vsp->clk_core);
	pm_runtime_put(dev);
	dev_info(dev, "Etappe 3a fertig - Treiber bleibt gebunden (rmmod zum Entfernen)\n");
	return 0;

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
MODULE_DESCRIPTION("Redmi A5: Video-Decoder (VSP), Etappe 3a - Strom, Takt, IOMMU-Abbildung");
