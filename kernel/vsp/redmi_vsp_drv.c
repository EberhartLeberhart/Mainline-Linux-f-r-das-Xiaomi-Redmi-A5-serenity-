// SPDX-License-Identifier: GPL-2.0
/*
 * redmi_vsp_drv: Treiber fuer den Video-Decoder (VSP) des UMS9230 (Redmi A5).
 *
 * Etappe 3a: Strom ueber Runtime-PM (Stromdomaene ap_vsp), Takte, IOMMU.
 * Etappe 3b: Reset (VSP_SOFT_RST + VSP_GLOBAL_SOFT_RST, nicht der IOMMU-Reset)
 *            und Interrupt SPI 34. Selbsttest beim Laden (Parameter selbsttest).
 * Etappe 3c: Geraetedatei /dev/sprd_vsp mit der Schnittstelle, die Unisocs
 *            Bibliotheken erwarten (ioctl-Magie 'm', Nummern 1-18, mmap der
 *            Register). Neu geschrieben fuer mainline; Nummern und Strukturen
 *            entsprechen Unisocs GPL-Treiber sprd_vsp (sprd_vsp.h).
 *
 * Arbeitsweise wie bei Unisoc: Die Bibliothek bildet die Register per mmap ab
 * und programmiert den VSP selbst. Der Kernel reserviert die Hardware
 * (ACQUAIRE/RELEASE), schaltet Takte (ENABLE/DISABLE), setzt zurueck (RESET),
 * wartet auf den Fertig-Interrupt (COMPLETE) und bildet dma-buf-Puffer ueber
 * die IOMMU ab (GET_IOVA/FREE_IOVA). Der VSP sieht nur Speicher, der ueber
 * GET_IOVA abgebildet wurde.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

static bool selbsttest = true;
module_param(selbsttest, bool, 0444);
MODULE_PARM_DESC(selbsttest, "Beim Laden Reset, Interrupt und IOMMU pruefen (Etappe 3a/3b)");

/* --- Schnittstelle wie Unisocs sprd_vsp.h --- */
#define SPRD_VSP_IOCTL_MAGIC	'm'
#define VSP_CONFIG_FREQ		_IOW(SPRD_VSP_IOCTL_MAGIC, 1, unsigned int)
#define VSP_GET_FREQ		_IOR(SPRD_VSP_IOCTL_MAGIC, 2, unsigned int)
#define VSP_ENABLE		_IO(SPRD_VSP_IOCTL_MAGIC, 3)
#define VSP_DISABLE		_IO(SPRD_VSP_IOCTL_MAGIC, 4)
#define VSP_ACQUAIRE		_IO(SPRD_VSP_IOCTL_MAGIC, 5)
#define VSP_RELEASE		_IO(SPRD_VSP_IOCTL_MAGIC, 6)
#define VSP_COMPLETE		_IO(SPRD_VSP_IOCTL_MAGIC, 7)
#define VSP_RESET		_IO(SPRD_VSP_IOCTL_MAGIC, 8)
#define VSP_HW_INFO		_IO(SPRD_VSP_IOCTL_MAGIC, 9)
#define VSP_VERSION		_IO(SPRD_VSP_IOCTL_MAGIC, 10)
#define VSP_GET_IOVA		_IOWR(SPRD_VSP_IOCTL_MAGIC, 11, struct vsp_iommu_map_data)
#define VSP_FREE_IOVA		_IOW(SPRD_VSP_IOCTL_MAGIC, 12, struct vsp_iommu_map_data)
#define VSP_GET_IOMMU_STATUS	_IO(SPRD_VSP_IOCTL_MAGIC, 13)
#define VSP_SET_CODEC_ID	_IOW(SPRD_VSP_IOCTL_MAGIC, 14, unsigned int)
#define VSP_GET_CODEC_COUNTER	_IOWR(SPRD_VSP_IOCTL_MAGIC, 15, unsigned int)
#define VSP_SET_SCENE		_IO(SPRD_VSP_IOCTL_MAGIC, 16)
#define VSP_GET_SCENE		_IO(SPRD_VSP_IOCTL_MAGIC, 17)
#define VSP_SYNC_GSP		_IO(SPRD_VSP_IOCTL_MAGIC, 18)

struct vsp_iommu_map_data {
	int fd;
	uint64_t size;
	uint64_t iova_addr;
	uint8_t need_cache_sync;
};

#define SPRD_VSP_MAP_SIZE	0x14000	/* so gross darf die Bibliothek abbilden */
#define VSP_VERSION_SHARKL6	20	/* video_ip_version unseres Chips */
#define VSP_CODEC_MAX		8	/* VSP_H264_DEC (0) ... VSP_ENC (8) */
#define VSP_ACQUIRE_TIMEOUT_MS	500
#define VSP_COMPLETE_TIMEOUT_MS	200
#define VSP_STATUS_INTERRUPTED	BIT(30)
#define VSP_STATUS_TIMEOUT	BIT(31)
#define AON_AP_SYST_EB		BIT(5)	/* VSP_HW_INFO: Wert wie bei Unisoc (Bit gesetzt) */

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

struct redmi_vsp_fh;

struct redmi_vsp {
	struct device *dev;
	void __iomem *base;
	phys_addr_t phys;
	struct clk *clk_eb;
	struct clk *clk_core;
	struct reset_control *rst_core;
	struct reset_control *rst_global;
	int irq;
	atomic_t irq_count;

	struct miscdevice misc;
	struct semaphore hw_sem;	/* ACQUAIRE/RELEASE */
	spinlock_t owner_lock;
	struct redmi_vsp_fh *owner;	/* wer die Hardware gerade hat */
	atomic_t instances;
	u32 freq_level;
	u32 scene;
};

struct redmi_vsp_map {
	struct list_head list;
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *att;
	struct sg_table *sgt;
	u64 iova;
	u64 size;
};

struct redmi_vsp_fh {
	struct redmi_vsp *vsp;
	bool acquired;
	bool enabled;
	int codec_id;
	wait_queue_head_t wq;
	bool done;
	u32 int_status;
	struct mutex maps_lock;
	struct list_head maps;
};

/* ------------------------------------------------------------------ */
/* Takte, Interrupt, Reset                                            */

static int redmi_vsp_takt_an(struct redmi_vsp *vsp)
{
	int ret;

	ret = clk_prepare_enable(vsp->clk_core);
	if (ret)
		return ret;
	ret = clk_prepare_enable(vsp->clk_eb);
	if (ret)
		clk_disable_unprepare(vsp->clk_core);
	return ret;
}

static void redmi_vsp_takt_aus(struct redmi_vsp *vsp)
{
	clk_disable_unprepare(vsp->clk_eb);
	clk_disable_unprepare(vsp->clk_core);
}

/* alle Quellen maskieren und Anstehendes loeschen (nur mit Takt!) */
static void redmi_vsp_int_ruhe(struct redmi_vsp *vsp)
{
	writel(0, vsp->base + GLB_BASE + VSP_INT_MASK);
	writel(VSP_INT_ALL, vsp->base + GLB_BASE + VSP_INT_CLR);
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

/*
 * Der Interrupt ist nur zwischen ENABLE und DISABLE (bzw. im Selbsttest)
 * eingeschaltet - also nur, wenn die Takte laufen.
 */
static irqreturn_t redmi_vsp_irq(int irq, void *data)
{
	struct redmi_vsp *vsp = data;
	void __iomem *g = vsp->base + GLB_BASE;
	struct redmi_vsp_fh *fh;
	u32 raw = readl(g + VSP_INT_RAW);

	if (!(raw & VSP_INT_ALL))
		return IRQ_NONE;
	atomic_inc(&vsp->irq_count);

	/* wie Unisoc: Masken auf 0, Anstehendes loeschen */
	redmi_vsp_int_ruhe(vsp);

	spin_lock(&vsp->owner_lock);
	fh = vsp->owner;
	if (fh) {
		fh->int_status |= raw & VSP_INT_ALL;
		fh->done = true;
		wake_up_interruptible(&fh->wq);
	}
	spin_unlock(&vsp->owner_lock);

	if (raw & (BIT(0) | BIT(4) | BIT(5)))
		dev_warn_ratelimited(vsp->dev, "Fehler-Interrupt: RAW 0x%08x\n", raw);
	return IRQ_HANDLED;
}

/* ------------------------------------------------------------------ */
/* Puffer ueber die IOMMU abbilden (dma-buf)                          */

static void redmi_vsp_map_frei(struct redmi_vsp_map *m)
{
	dma_buf_unmap_attachment_unlocked(m->att, m->sgt, DMA_BIDIRECTIONAL);
	dma_buf_detach(m->dmabuf, m->att);
	dma_buf_put(m->dmabuf);
	kfree(m);
}

static int redmi_vsp_get_iova(struct redmi_vsp_fh *fh, void __user *arg)
{
	struct redmi_vsp *vsp = fh->vsp;
	struct vsp_iommu_map_data d;
	struct redmi_vsp_map *m;
	int ret;

	if (copy_from_user(&d, arg, sizeof(d)))
		return -EFAULT;
	if (d.fd < 0)
		return -EINVAL;

	m = kzalloc(sizeof(*m), GFP_KERNEL);
	if (!m)
		return -ENOMEM;

	m->dmabuf = dma_buf_get(d.fd);
	if (IS_ERR(m->dmabuf)) {
		ret = PTR_ERR(m->dmabuf);
		goto err_free;
	}
	m->att = dma_buf_attach(m->dmabuf, vsp->dev);
	if (IS_ERR(m->att)) {
		ret = PTR_ERR(m->att);
		goto err_put;
	}
	m->sgt = dma_buf_map_attachment_unlocked(m->att, DMA_BIDIRECTIONAL);
	if (IS_ERR(m->sgt)) {
		ret = PTR_ERR(m->sgt);
		goto err_detach;
	}
	/*
	 * Mit IOMMU legt der DMA-Kern den ganzen Puffer an einem Stueck in den
	 * IOVA-Bereich - der VSP braucht genau eine Adresse.
	 */
	if (sg_dma_len(m->sgt->sgl) < m->dmabuf->size) {
		dev_err(vsp->dev, "Puffer nicht zusammenhaengend abgebildet (%u von %zu Bytes)\n",
			sg_dma_len(m->sgt->sgl), m->dmabuf->size);
		ret = -EINVAL;
		goto err_unmap;
	}
	m->iova = sg_dma_address(m->sgt->sgl);
	m->size = m->dmabuf->size;

	d.iova_addr = m->iova;
	d.size = m->size;
	if (copy_to_user(arg, &d, sizeof(d))) {
		ret = -EFAULT;
		goto err_unmap;
	}

	mutex_lock(&fh->maps_lock);
	list_add(&m->list, &fh->maps);
	mutex_unlock(&fh->maps_lock);
	dev_dbg(vsp->dev, "GET_IOVA fd %d: IOVA 0x%llx, %llu Bytes\n", d.fd, m->iova, m->size);
	return 0;

err_unmap:
	dma_buf_unmap_attachment_unlocked(m->att, m->sgt, DMA_BIDIRECTIONAL);
err_detach:
	dma_buf_detach(m->dmabuf, m->att);
err_put:
	dma_buf_put(m->dmabuf);
err_free:
	kfree(m);
	return ret;
}

static int redmi_vsp_free_iova(struct redmi_vsp_fh *fh, void __user *arg)
{
	struct vsp_iommu_map_data d;
	struct redmi_vsp_map *m, *gefunden = NULL;

	if (copy_from_user(&d, arg, sizeof(d)))
		return -EFAULT;

	mutex_lock(&fh->maps_lock);
	list_for_each_entry(m, &fh->maps, list) {
		if (m->iova == d.iova_addr && m->size == d.size) {
			list_del(&m->list);
			gefunden = m;
			break;
		}
	}
	mutex_unlock(&fh->maps_lock);

	if (!gefunden)
		return -EINVAL;
	redmi_vsp_map_frei(gefunden);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Geraetedatei /dev/sprd_vsp                                         */

static int redmi_vsp_enable(struct redmi_vsp_fh *fh)
{
	struct redmi_vsp *vsp = fh->vsp;
	int ret;

	if (fh->enabled)
		return 0;
	ret = redmi_vsp_takt_an(vsp);
	if (ret)
		return ret;
	redmi_vsp_int_ruhe(vsp);
	fh->enabled = true;
	enable_irq(vsp->irq);
	return 0;
}

static void redmi_vsp_disable(struct redmi_vsp_fh *fh)
{
	struct redmi_vsp *vsp = fh->vsp;

	if (!fh->enabled)
		return;
	/* erst den Interrupt aus, dann die Takte: der Handler liest Register */
	disable_irq(vsp->irq);
	redmi_vsp_int_ruhe(vsp);
	fh->enabled = false;
	redmi_vsp_takt_aus(vsp);
}

static void redmi_vsp_release_hw(struct redmi_vsp_fh *fh)
{
	struct redmi_vsp *vsp = fh->vsp;

	if (!fh->acquired)
		return;
	spin_lock_irq(&vsp->owner_lock);
	vsp->owner = NULL;
	spin_unlock_irq(&vsp->owner_lock);
	fh->acquired = false;
	up(&vsp->hw_sem);
}

static long redmi_vsp_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct redmi_vsp_fh *fh = filp->private_data;
	struct redmi_vsp *vsp = fh->vsp;
	void __user *uarg = (void __user *)arg;
	u32 wert;
	long ret = 0;

	switch (cmd) {
	case VSP_CONFIG_FREQ:
		/* Takt bleibt vorerst fest bei 384 MHz; Stufe wird nur gemerkt */
		if (get_user(wert, (u32 __user *)uarg))
			return -EFAULT;
		vsp->freq_level = wert;
		break;
	case VSP_GET_FREQ:
		return put_user(vsp->freq_level, (u32 __user *)uarg);
	case VSP_ENABLE:
		return redmi_vsp_enable(fh);
	case VSP_DISABLE:
		redmi_vsp_disable(fh);
		break;
	case VSP_ACQUAIRE:
		if (fh->acquired)
			return 0;
		ret = down_timeout(&vsp->hw_sem, msecs_to_jiffies(VSP_ACQUIRE_TIMEOUT_MS));
		if (ret)
			return ret;
		fh->acquired = true;
		fh->done = false;
		fh->int_status = 0;
		spin_lock_irq(&vsp->owner_lock);
		vsp->owner = fh;
		spin_unlock_irq(&vsp->owner_lock);
		break;
	case VSP_RELEASE:
		redmi_vsp_release_hw(fh);
		break;
	case VSP_COMPLETE:
		if (!fh->acquired || !fh->enabled)
			return -EPERM;
		ret = wait_event_interruptible_timeout(fh->wq, fh->done,
				msecs_to_jiffies(VSP_COMPLETE_TIMEOUT_MS));
		spin_lock_irq(&vsp->owner_lock);
		wert = fh->int_status;
		if (ret == -ERESTARTSYS) {
			wert |= VSP_STATUS_INTERRUPTED;
			ret = -EINVAL;
		} else if (ret == 0) {
			wert |= VSP_STATUS_TIMEOUT;
			ret = -ETIMEDOUT;
		} else {
			ret = 0;
		}
		fh->int_status = 0;
		fh->done = false;
		spin_unlock_irq(&vsp->owner_lock);
		if (ret == -ETIMEDOUT) {
			dev_dbg(vsp->dev, "COMPLETE: Zeitueberschreitung, RAW 0x%08x\n",
				readl(vsp->base + GLB_BASE + VSP_INT_RAW));
			redmi_vsp_int_ruhe(vsp);
		}
		if (put_user(wert, (u32 __user *)uarg))
			return -EFAULT;
		break;
	case VSP_RESET:
		if (!fh->acquired)
			return -EPERM;
		return redmi_vsp_reset(vsp);
	case VSP_HW_INFO:
		/* Unisoc liefert hier das gesetzte AP_SYST_EB-Bit (AON APB_EB1) */
		return put_user(AON_AP_SYST_EB, (u32 __user *)uarg);
	case VSP_VERSION:
		return put_user(VSP_VERSION_SHARKL6, (u32 __user *)uarg);
	case VSP_GET_IOMMU_STATUS:
		return iommu_get_domain_for_dev(vsp->dev) ? 0 : -ENODEV;
	case VSP_GET_IOVA:
		return redmi_vsp_get_iova(fh, uarg);
	case VSP_FREE_IOVA:
		return redmi_vsp_free_iova(fh, uarg);
	case VSP_SET_CODEC_ID:
		if (get_user(wert, (u32 __user *)uarg))
			return -EFAULT;
		if (wert > VSP_CODEC_MAX)
			return -EINVAL;
		fh->codec_id = wert;
		break;
	case VSP_GET_CODEC_COUNTER:
		return put_user(atomic_read(&vsp->instances), (u32 __user *)uarg);
	case VSP_SET_SCENE:
		if (get_user(wert, (u32 __user *)uarg))
			return -EFAULT;
		vsp->scene = wert;
		break;
	case VSP_GET_SCENE:
		return put_user(vsp->scene, (u32 __user *)uarg);
	case VSP_SYNC_GSP:
		break;
	default:
		dev_dbg(vsp->dev, "unbekanntes ioctl 0x%x\n", cmd);
		return -ENOTTY;
	}
	return ret;
}

static int redmi_vsp_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct redmi_vsp_fh *fh = filp->private_data;
	unsigned long groesse = vma->vm_end - vma->vm_start;

	if (vma->vm_pgoff || groesse > SPRD_VSP_MAP_SIZE)
		return -EINVAL;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vm_flags_set(vma, VM_IO | VM_DONTEXPAND | VM_DONTDUMP);
	return remap_pfn_range(vma, vma->vm_start, fh->vsp->phys >> PAGE_SHIFT,
			       groesse, vma->vm_page_prot);
}

static int redmi_vsp_open(struct inode *inode, struct file *filp)
{
	struct redmi_vsp *vsp = container_of(filp->private_data, struct redmi_vsp, misc);
	struct redmi_vsp_fh *fh;
	int ret;

	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (!fh)
		return -ENOMEM;
	fh->vsp = vsp;
	init_waitqueue_head(&fh->wq);
	mutex_init(&fh->maps_lock);
	INIT_LIST_HEAD(&fh->maps);

	ret = pm_runtime_resume_and_get(vsp->dev);	/* Strom an */
	if (ret) {
		kfree(fh);
		return ret;
	}
	atomic_inc(&vsp->instances);
	filp->private_data = fh;
	return 0;
}

static int redmi_vsp_release(struct inode *inode, struct file *filp)
{
	struct redmi_vsp_fh *fh = filp->private_data;
	struct redmi_vsp *vsp = fh->vsp;
	struct redmi_vsp_map *m, *tmp;

	/* aufraeumen, falls die Bibliothek abgestuerzt ist */
	redmi_vsp_disable(fh);
	redmi_vsp_release_hw(fh);
	list_for_each_entry_safe(m, tmp, &fh->maps, list) {
		list_del(&m->list);
		redmi_vsp_map_frei(m);
	}
	atomic_dec(&vsp->instances);
	pm_runtime_put(vsp->dev);
	kfree(fh);
	return 0;
}

static const struct file_operations redmi_vsp_fops = {
	.owner = THIS_MODULE,
	.open = redmi_vsp_open,
	.release = redmi_vsp_release,
	.unlocked_ioctl = redmi_vsp_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.mmap = redmi_vsp_mmap,
};

/* ------------------------------------------------------------------ */
/* Selbsttest beim Laden (Etappe 3a/3b)                               */

static void redmi_vsp_regs_zeigen(struct redmi_vsp *vsp, const char *wann)
{
	void __iomem *g = vsp->base + GLB_BASE;

	dev_info(vsp->dev, "%s: GLB INT_STS 0x%08x MASK 0x%08x RAW 0x%08x AXI_STS 0x%08x | MMU CFG 0x%08x FIRST_PPN 0x%08x FIRST_VPN 0x%08x\n",
		 wann, readl(g + VSP_INT_STS), readl(g + VSP_INT_MASK),
		 readl(g + VSP_INT_RAW), readl(g + VSP_AXI_STS),
		 readl(vsp->base + MMU_CFG), readl(vsp->base + MMU_FIRST_PPN),
		 readl(vsp->base + MMU_FIRST_VPN));
}

static void redmi_vsp_maptest(struct redmi_vsp *vsp)
{
	struct iommu_domain *dom = iommu_get_domain_for_dev(vsp->dev);
	dma_addr_t iova;
	phys_addr_t phys;
	void *buf;

	if (!dom) {
		dev_err(vsp->dev, "keine IOMMU-Domaene - iommus im Geraetebaum?\n");
		return;
	}
	buf = dma_alloc_coherent(vsp->dev, TEST_SIZE, &iova, GFP_KERNEL);
	if (!buf) {
		dev_err(vsp->dev, "dma_alloc_coherent(%u) fehlgeschlagen\n", TEST_SIZE);
		return;
	}
	phys = iommu_iova_to_phys(dom, iova);
	dev_info(vsp->dev, "Puffer %u KiB: IOVA 0x%llx -> phys 0x%llx (%s)\n",
		 TEST_SIZE / SZ_1K, (u64)iova, (u64)phys,
		 phys && iova <= dom->geometry.aperture_end ?
		 "abgebildet" : "NICHT abgebildet");
	dma_free_coherent(vsp->dev, TEST_SIZE, buf, iova);
}

static int redmi_vsp_selbsttest(struct redmi_vsp *vsp)
{
	struct device *dev = vsp->dev;
	u32 mask;
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;
	ret = redmi_vsp_takt_an(vsp);
	if (ret)
		goto out_pm;
	dev_info(dev, "Selbsttest: vsp-clk %lu Hz, MMU VERSION 0x%08x\n",
		 clk_get_rate(vsp->clk_core), readl(vsp->base + MMU_VERSION));

	/* Reset-Beweis: Muster in die Maske, ein echter Reset loescht es */
	writel(BIT(0) | BIT(4) | BIT(5), vsp->base + GLB_BASE + VSP_INT_MASK);
	redmi_vsp_regs_zeigen(vsp, "vor Reset ");
	ret = redmi_vsp_reset(vsp);
	if (ret) {
		dev_err(dev, "Reset fehlgeschlagen: %d\n", ret);
		goto out_clk;
	}
	redmi_vsp_regs_zeigen(vsp, "nach Reset");
	mask = readl(vsp->base + GLB_BASE + VSP_INT_MASK);
	dev_info(dev, "Reset-Probe: MASK nach Reset 0x%08x (%s)\n", mask,
		 mask ? "NICHT zurueckgesetzt" : "zurueckgesetzt");

	redmi_vsp_int_ruhe(vsp);
	atomic_set(&vsp->irq_count, 0);
	enable_irq(vsp->irq);
	msleep(100);
	disable_irq(vsp->irq);
	dev_info(dev, "Interrupt %d: in 100 ms %d mal ausgeloest (erwartet: 0)\n",
		 vsp->irq, atomic_read(&vsp->irq_count));

	redmi_vsp_maptest(vsp);
out_clk:
	redmi_vsp_takt_aus(vsp);
out_pm:
	pm_runtime_put(dev);
	return ret;
}

/* ------------------------------------------------------------------ */

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
	sema_init(&vsp->hw_sem, 1);
	spin_lock_init(&vsp->owner_lock);
	atomic_set(&vsp->instances, 0);

	/*
	 * Das Fenster beginnt mit den IOMMU-Registern, die sprd-iommu schon
	 * belegt hat: abbilden, ohne den Bereich anzufordern.
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;
	vsp->phys = res->start;
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
	/* erst einschalten, wenn die Takte laufen (ENABLE bzw. Selbsttest) */
	ret = devm_request_irq(dev, vsp->irq, redmi_vsp_irq, IRQF_NO_AUTOEN,
			       dev_name(dev), vsp);
	if (ret)
		return dev_err_probe(dev, ret, "Interrupt %d\n", vsp->irq);

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return dev_err_probe(dev, ret, "DMA-Maske\n");

	platform_set_drvdata(pdev, vsp);
	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	if (selbsttest) {
		ret = redmi_vsp_selbsttest(vsp);
		if (ret)
			return ret;
	}

	vsp->misc.minor = MISC_DYNAMIC_MINOR;
	vsp->misc.name = "sprd_vsp";
	vsp->misc.fops = &redmi_vsp_fops;
	vsp->misc.parent = dev;
	ret = misc_register(&vsp->misc);
	if (ret)
		return dev_err_probe(dev, ret, "/dev/sprd_vsp\n");

	dev_info(dev, "bereit: /dev/sprd_vsp (VSP-Version %d)\n", VSP_VERSION_SHARKL6);
	return 0;
}

static void redmi_vsp_remove(struct platform_device *pdev)
{
	struct redmi_vsp *vsp = platform_get_drvdata(pdev);

	misc_deregister(&vsp->misc);
}

static const struct of_device_id redmi_vsp_of_match[] = {
	{ .compatible = "sprd,ums9230-vsp" },
	{ }
};
MODULE_DEVICE_TABLE(of, redmi_vsp_of_match);

static struct platform_driver redmi_vsp_driver = {
	.probe = redmi_vsp_probe,
	.remove = redmi_vsp_remove,
	.driver = {
		.name = "redmi-vsp",
		.of_match_table = redmi_vsp_of_match,
	},
};
module_platform_driver(redmi_vsp_driver);

MODULE_IMPORT_NS("DMA_BUF");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Redmi A5: Video-Decoder (VSP) - Strom, IOMMU, Reset, Interrupt, /dev/sprd_vsp");
