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
 * Schreibt nichts. Ergebnis: dmesg, Praefix "modemtest:".
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>

#define TAG "modemtest: "

static int stufe;
module_param(stufe, int, 0444);
MODULE_PARM_DESC(stufe, "0=nur lesen (andere Stufen gibt es noch nicht)");

static uint bereiche = 0x1f;
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

		pr_info(TAG "%-9s %s 0x%03x = 0x%08x, Maske 0x%08x -> %s\n", b->name,
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

static int __init modemtest_init(void)
{
	if (stufe != 0) {
		pr_err(TAG "nur Stufe 0 (lesen) gibt es bisher\n");
		return -EINVAL;
	}
	pr_info(TAG "Start, Stufe 0 (nur lesen), bereiche=0x%x\n", bereiche);

	steuerbits_lesen();

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
