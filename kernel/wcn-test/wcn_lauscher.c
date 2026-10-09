// SPDX-License-Identifier: GPL-2.0
/*
 * wcn_lauscher - hoert auf einem Mailbox-Kanal des UMS9230 mit und zeigt jede Nachricht als
 * sipc-"smsg" an. Erster Schritt zum sipc-Port: Was sagt der WCN-Kern nach dem Start?
 *
 * Redmi A5 (serenity), Mainline 7.1. Sendet NICHTS, antwortet nicht.
 *
 * Hintergrund (Stufe 3 von wcn_starttest, 09.10.): Der WCN-Kern schickt nach dem Start
 * 3 Nachrichten auf Kanal 8; Mainline verwirft sie ("message's been dropped at ch[8]"),
 * weil kein Empfaenger angemeldet ist. Dieses Modul meldet sich als Empfaenger an.
 *
 * Ohne Aenderung am Geraetebaum: Das Modul legt zur Laufzeit einen Knoten
 * "/wcn-lauscher" (status = "disabled", damit kein Treiber anbeisst) mit
 * mboxes = <&mailbox KANAL> an und haengt ein eigenes Geraet daran.
 *
 * Nachrichtenformat (Realme include/linux/sipc.h): 8 Bytes =
 *   struct smsg { u8 channel; u8 type; u16 flag; u32 value; }
 * Die Mailbox liefert msg[0] = channel | type << 8 | flag << 16, msg[1] = value.
 *
 * Reihenfolge: ZUERST dieses Modul laden, DANN wcn_starttest stufe=3.
 * Ausgabe: dmesg (Praefix "wcnlausch:") und /sys/devices/platform/wcn-lauscher/nachrichten
 *
 * "Lauschangriff" auf den Speicher (nur lesen): Zwei Dateien geben den reservierten
 * WCN-Speicher so aus, wie der WCN-Kern ihn sieht:
 *   wcn_ram_btwf  0x87000000-0x8747ffff (4,5 MB, Firmware, Daten, Puffer des BT/WLAN-Kerns)
 *   wcn_ram_hoch  0x87600000-0x877fffff (2 MB, GNSS-Bereich und Sync bei 0x877fdc00)
 * Die Luecke 0x87480000-0x874fffff ist normaler Linux-RAM und wird bewusst NICHT ausgegeben.
 * Auswerten am PC z. B.: strings -n 6 wcn_ram_btwf | less
 */

#include <linux/device.h>
#include <linux/ktime.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/sysfs.h>

#define TAG "wcnlausch: "

static int kanal = 8;
module_param(kanal, int, 0444);
MODULE_PARM_DESC(kanal, "Mailbox-Kanal (Kern-ID), WCN = 8");

static const char * const typ_name[] = {
	"NONE", "OPEN", "CLOSE", "DATA", "EVENT", "CMD", "DONE",
	"SMEM_ALLOC", "SMEM_FREE", "SMEM_DONE", "FUNC_CALL", "FUNC_RETURN",
	"DIE", "DFS", "DFS_RSP", "ASS_TRG", "HIGH_OFFSET", "ASSERT",
};

#define SMSG_OPEN_MAGIC		0xBEEE
#define SMSG_CLOSE_MAGIC	0xEDDD

#define RING 64

struct eintrag {
	u64 zeit_ns;
	u32 lo, hi;
};

static struct eintrag ring[RING];
static unsigned int anzahl;	/* insgesamt empfangen */
static DEFINE_SPINLOCK(ring_lock);

static struct of_changeset cs;
static bool cs_aktiv;
static struct device_node *knoten;
static struct platform_device *pdev;
static struct mbox_client cl;
static struct mbox_chan *chan;
static bool phandle_gesetzt;
static struct device_node *mbox_np;

/* reservierter WCN-Speicher (no-map, belegt 09.10.), nur lesend */
struct bereich {
	phys_addr_t phys;
	size_t len;
	void *va;
};
static struct bereich ram_btwf = { 0x87000000, 0x480000 };
static struct bereich ram_hoch = { 0x87600000, 0x200000 };

static ssize_t ram_lesen(struct bereich *b, char *buf, loff_t pos, size_t n)
{
	if (!b->va || pos >= b->len)
		return 0;
	n = min_t(size_t, n, b->len - pos);
	memcpy_fromio(buf, (void __iomem *)(b->va + pos), n);
	return n;
}

static ssize_t wcn_ram_btwf_read(struct file *f, struct kobject *k, const struct bin_attribute *a,
				 char *buf, loff_t pos, size_t n)
{
	return ram_lesen(&ram_btwf, buf, pos, n);
}

static ssize_t wcn_ram_hoch_read(struct file *f, struct kobject *k, const struct bin_attribute *a,
				 char *buf, loff_t pos, size_t n)
{
	return ram_lesen(&ram_hoch, buf, pos, n);
}

static const BIN_ATTR_RO(wcn_ram_btwf, 0x480000);
static const BIN_ATTR_RO(wcn_ram_hoch, 0x200000);
static bool bin_btwf, bin_hoch;

static const char *typ_text(u8 t)
{
	return t < ARRAY_SIZE(typ_name) ? typ_name[t] : "?";
}

static int zeile(char *buf, size_t n, const struct eintrag *e, unsigned int nr)
{
	u8 ch = e->lo & 0xff, typ = (e->lo >> 8) & 0xff;
	u16 flag = e->lo >> 16;
	const char *zusatz = "";

	if (typ == 1 && flag == SMSG_OPEN_MAGIC)
		zusatz = " (OPEN-Magic)";
	else if (typ == 2 && flag == SMSG_CLOSE_MAGIC)
		zusatz = " (CLOSE-Magic)";

	return scnprintf(buf, n,
		"#%u %llu.%06llu s: roh %08x %08x -> smsg Kanal %u, Typ %u %s, Flag 0x%04x%s, Wert 0x%08x\n",
		nr, e->zeit_ns / NSEC_PER_SEC, (e->zeit_ns % NSEC_PER_SEC) / 1000,
		e->lo, e->hi, ch, typ, typ_text(typ), flag, zusatz, e->hi);
}

static void empfangen(struct mbox_client *c, void *msg)
{
	u32 *m = msg;
	struct eintrag e = { ktime_get_boottime_ns(), m[0], m[1] };
	unsigned long flags;
	unsigned int nr;
	char text[192];

	spin_lock_irqsave(&ring_lock, flags);
	nr = anzahl++;
	ring[nr % RING] = e;
	spin_unlock_irqrestore(&ring_lock, flags);

	zeile(text, sizeof(text), &e, nr);
	pr_info(TAG "%s", text);
}

static ssize_t nachrichten_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	unsigned int n, start, i;
	unsigned long flags;
	struct eintrag *kopie;
	ssize_t p;

	kopie = kmalloc_array(RING, sizeof(*kopie), GFP_KERNEL);
	if (!kopie)
		return -ENOMEM;
	spin_lock_irqsave(&ring_lock, flags);
	n = anzahl;
	memcpy(kopie, ring, sizeof(ring));
	spin_unlock_irqrestore(&ring_lock, flags);

	p = scnprintf(buf, PAGE_SIZE, "Kanal %d, %u Nachrichten empfangen%s\n", kanal, n,
		      n > RING ? " (nur die letzten 64 gezeigt)" : "");
	start = n > RING ? n - RING : 0;
	for (i = start; i < n && p < PAGE_SIZE - 200; i++)
		p += zeile(buf + p, PAGE_SIZE - p, &kopie[i % RING], i);
	kfree(kopie);
	return p;
}
static DEVICE_ATTR_RO(nachrichten);

/* Mailbox-Knoten finden und sicherstellen, dass er eine phandle hat */
static int mailbox_finden(u32 *ph)
{
	static const struct of_device_id ids[] = {
		{ .compatible = "sprd,ums9230-mailbox" },
		{ .compatible = "sprd,sc9863a-mailbox" },
		{ .compatible = "sprd,sc9860-mailbox" },
		{ }
	};
	struct device_node *np;
	u32 groesste = 0;

	mbox_np = of_find_matching_node(NULL, ids);
	if (!mbox_np) {
		pr_err(TAG "kein Mailbox-Knoten im Geraetebaum\n");
		return -ENODEV;
	}
	if (!mbox_np->phandle) {
		/*
		 * Niemand verweist im DT auf die Mailbox, dtc hat ihr deshalb keine phandle gegeben.
		 * Fuer diesen Test eine freie vergeben (groesste vorhandene + 1).
		 */
		for (np = of_find_all_nodes(NULL); np; np = of_find_all_nodes(np))
			groesste = max(groesste, np->phandle);
		mbox_np->phandle = groesste + 1;
		phandle_gesetzt = true;
		pr_info(TAG "Mailbox %pOF hatte keine phandle - vergebe 0x%x\n", mbox_np, mbox_np->phandle);
	}
	*ph = mbox_np->phandle;
	pr_info(TAG "Mailbox %pOF, phandle 0x%x, #mbox-cells %s\n", mbox_np, *ph,
		of_property_present(mbox_np, "#mbox-cells") ? "vorhanden" : "FEHLT");
	return 0;
}

static int knoten_anlegen(u32 ph)
{
	u32 mboxes[2] = { ph, kanal };
	struct device_node *wurzel = of_find_node_by_path("/");
	int ret;

	if (!wurzel)
		return -ENODEV;
	of_changeset_init(&cs);
	knoten = of_changeset_create_node(&cs, wurzel, "wcn-lauscher");
	of_node_put(wurzel);
	if (!knoten) {
		ret = -ENOMEM;
		goto fehler;
	}
	ret = of_changeset_add_prop_string(&cs, knoten, "status", "disabled") ?:
	      of_changeset_add_prop_u32_array(&cs, knoten, "mboxes", mboxes, 2) ?:
	      of_changeset_apply(&cs);
	if (ret)
		goto fehler;
	cs_aktiv = true;
	return 0;
fehler:
	of_changeset_destroy(&cs);
	knoten = NULL;
	return ret ?: -ENOMEM;
}

static void aufraeumen(void)
{
	if (chan)
		mbox_free_channel(chan);
	chan = NULL;
	if (pdev && bin_btwf)
		sysfs_remove_bin_file(&pdev->dev.kobj, &bin_attr_wcn_ram_btwf);
	if (pdev && bin_hoch)
		sysfs_remove_bin_file(&pdev->dev.kobj, &bin_attr_wcn_ram_hoch);
	bin_btwf = bin_hoch = false;
	if (ram_btwf.va)
		memunmap(ram_btwf.va);
	if (ram_hoch.va)
		memunmap(ram_hoch.va);
	ram_btwf.va = ram_hoch.va = NULL;
	if (pdev) {
		device_remove_file(&pdev->dev, &dev_attr_nachrichten);
		platform_device_unregister(pdev);
		pdev = NULL;
	}
	if (cs_aktiv) {
		of_changeset_revert(&cs);
		of_changeset_destroy(&cs);
		cs_aktiv = false;
	}
	if (mbox_np) {
		if (phandle_gesetzt)
			mbox_np->phandle = 0;
		of_node_put(mbox_np);
		mbox_np = NULL;
	}
}

static int __init lauscher_init(void)
{
	u32 ph;
	int ret;

	if (kanal < 0 || kanal > 15) {
		pr_err(TAG "kanal muss 0..15 sein\n");
		return -EINVAL;
	}
	ret = mailbox_finden(&ph);
	if (ret)
		return ret;
	ret = knoten_anlegen(ph);
	if (ret) {
		pr_err(TAG "DT-Knoten anlegen: %d\n", ret);
		goto fehler;
	}

	pdev = platform_device_register_simple("wcn-lauscher", PLATFORM_DEVID_NONE, NULL, 0);
	if (IS_ERR(pdev)) {
		ret = PTR_ERR(pdev);
		pdev = NULL;
		goto fehler;
	}
	device_set_node(&pdev->dev, of_fwnode_handle(knoten));

	cl.dev = &pdev->dev;
	cl.rx_callback = empfangen;
	cl.tx_block = false;
	cl.knows_txdone = true;
	chan = mbox_request_channel(&cl, 0);
	if (IS_ERR(chan)) {
		ret = PTR_ERR(chan);
		chan = NULL;
		pr_err(TAG "Kanal %d anfordern: %d\n", kanal, ret);
		goto fehler;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_nachrichten);
	if (ret)
		pr_warn(TAG "Datei nachrichten: %d\n", ret);

	/* WC wie Android (nocache); klappt nur bei no-map - sonst lieber gar nicht */
	ram_btwf.va = memremap(ram_btwf.phys, ram_btwf.len, MEMREMAP_WC);
	ram_hoch.va = memremap(ram_hoch.phys, ram_hoch.len, MEMREMAP_WC);
	if (ram_btwf.va && !sysfs_create_bin_file(&pdev->dev.kobj, &bin_attr_wcn_ram_btwf))
		bin_btwf = true;
	if (ram_hoch.va && !sysfs_create_bin_file(&pdev->dev.kobj, &bin_attr_wcn_ram_hoch))
		bin_hoch = true;
	pr_info(TAG "WCN-Speicher lesbar: wcn_ram_btwf %s, wcn_ram_hoch %s\n",
		bin_btwf ? "ja" : "NEIN", bin_hoch ? "ja" : "NEIN");
	pr_info(TAG "hoere auf Mailbox-Kanal %d - jetzt wcn_starttest stufe=3 laden\n", kanal);
	return 0;

fehler:
	aufraeumen();
	return ret;
}

static void __exit lauscher_exit(void)
{
	pr_info(TAG "%u Nachrichten empfangen, Kanal %d wieder frei\n", anzahl, kanal);
	aufraeumen();
}

module_init(lauscher_init);
module_exit(lauscher_exit);

MODULE_DESCRIPTION("Mailbox-Lauscher: sipc-Nachrichten des UMS9230-WCN-Kerns anzeigen - Redmi A5");
MODULE_AUTHOR("EberhartLeberhart");
MODULE_LICENSE("GPL");
