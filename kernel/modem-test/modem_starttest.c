// SPDX-License-Identifier: GPL-2.0
/*
 * modem_starttest - Testtreiber: LTE-Modem (PUBCP) und Hilfskern pmsys des UMS9230 untersuchen.
 *
 * Redmi A5 (serenity), Mainline 7.1. Nur zum Ausprobieren, kein fertiger Treiber.
 * Hintergrund und Quellen: MODEM.md im Repo.
 *
 * Stufe 0 (einzige Stufe bisher): NUR LESEN.
 *  1. Steuerbits von modem@1 (Xiaomi-DT, Realme sprd_modem_loader.c soc_modem_start/stop):
 *       shutdown  PMU 0x330 Bit 25   deepsleep PMU 0x818 Bit 1   sysreset PMU 0xb98 Bit 11
 *       dspreset  PMU 0xb88 Bits 3,4 corereset AON 0x174 Bit 10
 *     gesetzt = Modem aus/im Reset, geloescht = Modem laeuft (so setzt es soc_modem_start).
 *  2. Speicher lesen, Bereich fuer Bereich (Parameter "bereiche", Bitmaske):
 *       Bit 0  pmsys-iRAM   0x62010000  (Startcode des Hilfskerns, modem_sp_info.xml)
 *       Bit 1  pmsys-DDR    0x88040000  (pm_sys-Abbild, im DT reserviert: sp-ddr@88000000)
 *       Bit 2  Modem-iRAM   0x65003400  (Startcode des Modems, modem_cp_info.xml)
 *       Bit 3  Modem-DDR    0x89600000, 0x8AF20000 (cpcmdline), 0x8B000000 (Firmware)
 *       Bit 4  sipc-Speicher 0x8e000000
 *     Unter Android gibt trusty den Modem-Speicher erst frei (kernel_bootcp_unlock_ddr,
 *     sprd_set_modem_firewall). Ob der AP ihn ohne trusty lesen darf, ist genau die Frage.
 *     Vor jedem Bereich steht eine Zeile "lese ..." und eine kurze Pause, damit redmi-kmsg.sh
 *     sie noch sichert. Endet das Protokoll dort, hat das Lesen den Absturz ausgeloest -
 *     dann diesen Bereich beim naechsten Versuch per "bereiche" weglassen.
 *
 * Stufe 1: SCHREIBTEST (startet nichts). Je eine Seite am Ende von Modem-DDR, pmsys-DDR und sipc,
 *  zum Vergleich im WCN-Bereich (dort wissen wir, dass Schreiben geht): 16 Woerter merken, Pruefmuster
 *  schreiben, dsb, zuruecklesen, alten Inhalt wiederherstellen. Zeigt, ob die Firewall (trusty
 *  LOCK/UNLOCK_CP_DDR) Schreibzugriffe des AP verwirft. Gleiche Vorsichtsregel wie oben ("teste ...").
 *
 * Stufe 2: pmsys (Hilfskern, Cortex-M) STARTEN - noch nicht das Modem. Wie modem_sp_info.xml:
 *  /lib/firmware/pm_sys.bin (Partition pm_sys_a) nach 0x88040000, /lib/firmware/pmsys_bootcode.bin
 *  (aus dem bootcode-Attribut der XML, werkzeuge/pmsys_bootcode.py) nach 0x62010000, dann corereset
 *  AON 0x8c Bit 0 loeschen (soc_modem_start; andere Bits von modem@0 sind ungueltig). Danach 5 s
 *  beobachten: aendert sich der pmsys-sipc-Bereich 0x6500a400 bzw. das Bit? Bricht ab, wenn pmsys
 *  schon laeuft (Bit geloescht), ausser trotzdem=1. Firmware-Dateien bleiben auf dem Handy.
 * Stufe 3: pmsys wieder anhalten (corereset setzen).
 *
 * Ergebnis: dmesg, Praefix "modemtest:". Stufe 0 schreibt nichts.
 */

#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#define TAG "modemtest: "

static int stufe;
module_param(stufe, int, 0444);
MODULE_PARM_DESC(stufe, "0=nur lesen 1=Schreibtest 2=pmsys starten 3=pmsys anhalten");

static bool trotzdem;
module_param(trotzdem, bool, 0444);
MODULE_PARM_DESC(trotzdem, "Stufe 2 auch, wenn pmsys schon zu laufen scheint");

static uint bereiche = 0x1f;	/* Stufe 1: Bits 0..3 = WCN, pmsys, Modem, sipc */
module_param(bereiche, uint, 0444);
MODULE_PARM_DESC(bereiche, "Bitmaske der zu lesenden Speicher (siehe Kopf), Standard 0x1f = alle");

#define AON_APB_PHYS	0x64000000
#define PMU_APB_PHYS	0x64020000
#define APB_LEN		0x3000

struct steuerbit {
	const char *name;
	bool aon;		/* false = PMU */
	u32 reg;
	u32 maske;
};

static const struct steuerbit bits[] = {
	{ "shutdown",  false, 0x330, 0x2000000 },
	{ "deepsleep", false, 0x818, 0x02 },
	{ "sysreset",  false, 0xb98, 0x800 },
	{ "dspreset",  false, 0xb88, 0x18 },
	{ "corereset", true,  0x174, 0x400 },
	{ "pmsys-corereset", true, 0x08c, 0x01 },	/* modem@0: einziges gueltiges Bit */
};

static void steuerbits_lesen(void)
{
	void __iomem *aon, *pmu;
	int i;

	aon = ioremap(AON_APB_PHYS, APB_LEN);
	pmu = ioremap(PMU_APB_PHYS, APB_LEN);
	if (!aon || !pmu) {
		pr_err(TAG "ioremap AON/PMU fehlgeschlagen\n");
		goto weg;
	}
	for (i = 0; i < ARRAY_SIZE(bits); i++) {
		const struct steuerbit *b = &bits[i];
		u32 v = readl((b->aon ? aon : pmu) + b->reg);

		pr_info(TAG "%-15s %s 0x%03x = 0x%08x, Maske 0x%08x -> %s\n", b->name,
			b->aon ? "AON" : "PMU", b->reg, v, b->maske,
			(v & b->maske) == b->maske ? "GESETZT (aus/Reset)" :
			(v & b->maske) ? "teilweise gesetzt" : "geloescht (laeuft)");
	}
weg:
	if (aon)
		iounmap(aon);
	if (pmu)
		iounmap(pmu);
}

/* 64 Bytes zeigen, dann die ersten 4 KB auswerten */
static void auswerten(const char *name, phys_addr_t phys, const void *va, bool io, size_t len)
{
	u32 w[16], v, erstes = 0;
	unsigned int ungleich0 = 0, gleichff = 0, i;

	for (i = 0; i < 16; i++)
		w[i] = io ? readl((const void __iomem *)va + 4 * i) : READ_ONCE(((const u32 *)va)[i]);
	for (i = 0; i < 16; i += 4)
		pr_info(TAG "  %pa+%02x: %08x %08x %08x %08x\n", &phys, 4 * i, w[i], w[i + 1], w[i + 2], w[i + 3]);
	for (i = 0; i < len / 4; i++) {
		v = io ? readl((const void __iomem *)va + 4 * i) : READ_ONCE(((const u32 *)va)[i]);
		if (v && !ungleich0)
			erstes = i * 4;
		if (v)
			ungleich0++;
		if (v == 0xffffffff)
			gleichff++;
	}
	pr_info(TAG "  %s: %u von %zu Woertern ungleich 0 (erstes bei +0x%x), %u davon 0xffffffff\n",
		name, ungleich0, len / 4, erstes, gleichff);
}

static void text_zeigen(phys_addr_t phys, const u8 *va, size_t len)
{
	char buf[200];
	size_t i, n = 0;

	for (i = 0; i < len && n < sizeof(buf) - 1; i++) {
		u8 c = READ_ONCE(va[i]);

		if (!c)
			break;
		buf[n++] = (c >= 0x20 && c < 0x7f) ? c : '.';
	}
	buf[n] = 0;
	pr_info(TAG "  Text bei %pa: \"%s\"%s\n", &phys, buf, n ? "" : " (leer)");
}

static void lese_io(int bit, const char *name, phys_addr_t phys, size_t len)
{
	void __iomem *va;

	if (!(bereiche & BIT(bit)))
		return;
	pr_info(TAG "lese %s %pa (%zu Bytes, ioremap) ...\n", name, &phys, len);
	msleep(300);
	va = ioremap(phys, len);
	if (!va) {
		pr_err(TAG "  ioremap fehlgeschlagen\n");
		return;
	}
	auswerten(name, phys, (const void __force *)va, true, len);
	iounmap(va);
}

static void lese_ram(int bit, const char *name, phys_addr_t phys, size_t len, bool text)
{
	void *va;

	if (!(bereiche & BIT(bit)))
		return;
	pr_info(TAG "lese %s %pa (%zu Bytes, memremap WC) ...\n", name, &phys, len);
	msleep(300);
	va = memremap(phys, len, MEMREMAP_WC);
	if (!va) {
		pr_err(TAG "  memremap fehlgeschlagen (liegt der Bereich nicht im reservierten Speicher?)\n");
		return;
	}
	if (text)
		text_zeigen(phys, va, len);
	else
		auswerten(name, phys, va, false, len);
	memunmap(va);
}

#define TEST_WORTE	16

/* Muster schreiben, zuruecklesen, alten Inhalt wiederherstellen. 1 = Schreiben wirkt */
static int schreibtest(const char *name, phys_addr_t phys)
{
	u32 alt[TEST_WORTE], v;
	unsigned int i, gut = 0;
	u32 *va;

	pr_info(TAG "teste Schreiben %s %pa ...\n", name, &phys);
	msleep(300);
	va = memremap(phys, PAGE_SIZE, MEMREMAP_WC);
	if (!va) {
		pr_err(TAG "  memremap fehlgeschlagen\n");
		return -1;
	}
	for (i = 0; i < TEST_WORTE; i++)
		alt[i] = READ_ONCE(va[i]);
	for (i = 0; i < TEST_WORTE; i++)
		WRITE_ONCE(va[i], 0x4d440000 | (i << 8) | (~alt[i] & 0xff));	/* sicher != alt */
	dsb(sy);
	for (i = 0; i < TEST_WORTE; i++) {
		v = READ_ONCE(va[i]);
		if (v == (0x4d440000 | (i << 8) | (~alt[i] & 0xff)))
			gut++;
	}
	pr_info(TAG "  vorher %08x %08x, nach Schreiben %08x %08x -> %u von %u Woertern uebernommen\n",
		alt[0], alt[1], READ_ONCE(va[0]), READ_ONCE(va[1]), gut, TEST_WORTE);
	for (i = 0; i < TEST_WORTE; i++)
		WRITE_ONCE(va[i], alt[i]);
	dsb(sy);
	for (i = 0; i < TEST_WORTE; i++)
		if (READ_ONCE(va[i]) != alt[i])
			break;
	pr_info(TAG "  alter Inhalt %s\n", i == TEST_WORTE ? "wiederhergestellt" : "NICHT wiederhergestellt");
	memunmap(va);
	return gut == TEST_WORTE;
}

static void stufe1(void)
{
	static const struct { const char *name; phys_addr_t phys; } seiten[] = {
		{ "WCN-Bereich (Vergleich)",  0x87700000 },
		{ "pmsys-DDR (letzte Seite)", 0x882ff000 },
		{ "Modem-DDR (letzte Seite)", 0x8deff000 },
		{ "sipc (letzte Seite)",      0x8e8ff000 },
	};
	int i, r;

	for (i = 0; i < ARRAY_SIZE(seiten); i++) {
		if (!(bereiche & BIT(i)))
			continue;
		r = schreibtest(seiten[i].name, seiten[i].phys);
		pr_info(TAG "  => %s: %s\n", seiten[i].name,
			r == 1 ? "AP darf schreiben" : r == 0 ? "SCHREIBEN WIRKT NICHT (Firewall?)" : "nicht getestet");
	}
}

/* ---- Stufe 2/3: pmsys ------------------------------------------------- */

#define PMSYS_DDR	0x88040000
#define PMSYS_DDR_LEN	0x200000
#define PMSYS_IRAM	0x62010000
#define PMSYS_IRAM_LEN	0x1000
#define PMSYS_SIPC	0x6500a400	/* sipc-pmsys smem (AP-Sicht) */
#define PMSYS_SIPC_LEN	0x2000
#define AON_PMSYS_RST	0x8c
#define PMSYS_RST_BIT	0x01

static int pmsys_bit(void __iomem *aon)
{
	return readl(aon + AON_PMSYS_RST) & PMSYS_RST_BIT;
}

static void pmsys_bit_setzen(void __iomem *aon, bool setzen)	/* wie regmap_update_bits */
{
	u32 v = readl(aon + AON_PMSYS_RST);

	writel(setzen ? (v | PMSYS_RST_BIT) : (v & ~PMSYS_RST_BIT), aon + AON_PMSYS_RST);
	pr_info(TAG "AON 0x%03x: 0x%08x -> 0x%08x\n", AON_PMSYS_RST, v, readl(aon + AON_PMSYS_RST));
}

static unsigned int unterschiede(const u32 *a, const u32 *b, unsigned int n)
{
	unsigned int i, d = 0;

	for (i = 0; i < n; i++)
		d += a[i] != b[i];
	return d;
}

static int stufe2(struct device *dev)
{
	const struct firmware *fw = NULL, *bc = NULL;
	void __iomem *aon = NULL, *iram = NULL, *sipc = NULL;
	void *ddr = NULL;
	u32 *vorher = NULL, *jetzt = NULL;
	unsigned int n = PMSYS_SIPC_LEN / 4, i, t;
	int ret;

	aon = ioremap(AON_APB_PHYS, APB_LEN);
	iram = ioremap(PMSYS_IRAM, PMSYS_IRAM_LEN);
	sipc = ioremap(PMSYS_SIPC, PMSYS_SIPC_LEN);
	ddr = memremap(PMSYS_DDR, PMSYS_DDR_LEN, MEMREMAP_WC);
	vorher = kmalloc(PMSYS_SIPC_LEN, GFP_KERNEL);
	jetzt = kmalloc(PMSYS_SIPC_LEN, GFP_KERNEL);
	if (!aon || !iram || !sipc || !ddr || !vorher || !jetzt) {
		pr_err(TAG "Abbildung/Speicher fehlgeschlagen\n");
		ret = -ENOMEM;
		goto weg;
	}
	if (!pmsys_bit(aon) && !trotzdem) {
		pr_err(TAG "pmsys-corereset ist schon geloescht - pmsys laeuft evtl. schon. Abbruch (trotzdem=1)\n");
		ret = -EBUSY;
		goto weg;
	}

	ret = request_firmware(&fw, "pm_sys.bin", dev);
	if (ret) {
		pr_err(TAG "/lib/firmware/pm_sys.bin fehlt (%d)\n", ret);
		goto weg;
	}
	ret = request_firmware(&bc, "pmsys_bootcode.bin", dev);
	if (ret) {
		pr_err(TAG "/lib/firmware/pmsys_bootcode.bin fehlt (%d)\n", ret);
		goto weg;
	}
	if (fw->size > PMSYS_DDR_LEN || bc->size > PMSYS_IRAM_LEN || bc->size < 8 || bc->size % 4) {
		pr_err(TAG "Groessen passen nicht: pm_sys %zu (max 0x%x), Startcode %zu (8..0x%x, durch 4)\n",
		       fw->size, PMSYS_DDR_LEN, bc->size, PMSYS_IRAM_LEN);
		ret = -EINVAL;
		goto weg;
	}
	pr_info(TAG "pm_sys.bin %zu Bytes, Startcode %zu Bytes (SP 0x%08x, Reset 0x%08x)\n", fw->size, bc->size,
		((const u32 *)bc->data)[0], ((const u32 *)bc->data)[1]);

	/* sicherheitshalber erst in Reset halten (ist es laut Pruefung schon) */
	pmsys_bit_setzen(aon, true);
	msleep(10);

	memcpy(ddr, fw->data, fw->size);
	dsb(sy);
	ret = memcmp(ddr, fw->data, fw->size);
	pr_info(TAG "pm_sys nach 0x%x geschrieben, Vergleich %s\n", PMSYS_DDR, ret ? "FALSCH" : "ok");
	if (ret) {
		ret = -EIO;
		goto weg;
	}
	for (i = 0; i < bc->size / 4; i++)
		writel(((const u32 *)bc->data)[i], iram + 4 * i);
	for (i = 0; i < bc->size / 4; i++)
		if (readl(iram + 4 * i) != ((const u32 *)bc->data)[i])
			break;
	pr_info(TAG "Startcode nach 0x%x geschrieben, Vergleich %s\n", PMSYS_IRAM,
		i == bc->size / 4 ? "ok" : "FALSCH");
	if (i != bc->size / 4) {
		ret = -EIO;
		goto weg;
	}

	memcpy_fromio(vorher, sipc, PMSYS_SIPC_LEN);
	pr_info(TAG "pmsys loslassen (corereset loeschen) ...\n");
	msleep(300);	/* damit redmi-kmsg.sh die Zeile sichert */
	pmsys_bit_setzen(aon, false);

	for (t = 1; t <= 10; t++) {
		msleep(500);
		memcpy_fromio(jetzt, sipc, PMSYS_SIPC_LEN);
		pr_info(TAG "  nach %u ms: pmsys-sipc 0x%x: %u von %u Woertern veraendert, Bit %d, iRAM[0..1] %08x %08x\n",
			t * 500, PMSYS_SIPC, unterschiede(vorher, jetzt, n), n, pmsys_bit(aon),
			readl(iram), readl(iram + 4));
	}
	pr_info(TAG "  pmsys-sipc jetzt: %08x %08x %08x %08x\n", jetzt[0], jetzt[1], jetzt[2], jetzt[3]);
	pr_info(TAG "  Mailbox-Zaehler: grep -i mailbox /proc/interrupts\n");
	ret = 0;
weg:
	release_firmware(bc);
	release_firmware(fw);
	kfree(jetzt);
	kfree(vorher);
	if (ddr)
		memunmap(ddr);
	if (sipc)
		iounmap(sipc);
	if (iram)
		iounmap(iram);
	if (aon)
		iounmap(aon);
	return ret;
}

static int stufe3(void)
{
	void __iomem *aon = ioremap(AON_APB_PHYS, APB_LEN);

	if (!aon)
		return -ENOMEM;
	pmsys_bit_setzen(aon, true);
	iounmap(aon);
	return 0;
}

static int __init modemtest_init(void)
{
	static const char * const namen[] = { "nur lesen", "Schreibtest", "pmsys starten", "pmsys anhalten" };
	struct platform_device *pd;
	int ret;

	if (stufe < 0 || stufe > 3) {
		pr_err(TAG "stufe muss 0..3 sein\n");
		return -EINVAL;
	}
	pr_info(TAG "Start, Stufe %d (%s), bereiche=0x%x\n", stufe, namen[stufe], bereiche);

	steuerbits_lesen();
	if (stufe == 2) {
		pd = platform_device_register_simple("modemtest", PLATFORM_DEVID_NONE, NULL, 0);
		if (IS_ERR(pd))
			return PTR_ERR(pd);
		ret = stufe2(&pd->dev);
		platform_device_unregister(pd);
		steuerbits_lesen();
		pr_info(TAG "Ende Stufe 2: %d (pmsys bleibt so; anhalten: Stufe 3 oder Neustart)\n", ret);
		return ret;
	}
	if (stufe == 3) {
		ret = stufe3();
		steuerbits_lesen();
		pr_info(TAG "Ende Stufe 3: %d\n", ret);
		return ret;
	}
	if (stufe == 1) {
		stufe1();
		pr_info(TAG "Ende Stufe 1 - nichts gestartet, Testseiten wiederhergestellt\n");
		return 0;
	}

	lese_io(0, "pmsys-iRAM", 0x62010000, 0x1000);
	lese_ram(1, "pmsys-DDR", 0x88040000, 0x1000, false);
	lese_io(2, "Modem-iRAM", 0x65003400, 0x1000);
	lese_ram(3, "Modem-DDR-Anfang", 0x89600000, 0x1000, false);
	lese_ram(3, "cpcmdline", 0x8af20000, 0x800, true);
	lese_ram(3, "Modem-Firmware", 0x8b000000, 0x1000, false);
	lese_ram(4, "sipc-Speicher", 0x8e000000, 0x1000, false);

	pr_info(TAG "Ende Stufe 0 - alles gelesen, nichts geschrieben\n");
	return 0;
}

static void __exit modemtest_exit(void)
{
	pr_info(TAG "entladen\n");
}

module_init(modemtest_init);
module_exit(modemtest_exit);

MODULE_DESCRIPTION("Testtreiber: UMS9230-LTE-Modem untersuchen (Stufe 0: nur lesen) - Redmi A5");
MODULE_AUTHOR("EberhartLeberhart");
MODULE_LICENSE("GPL");
