// SPDX-License-Identifier: GPL-2.0
/*
 * wcn_starttest - Testtreiber: WCN-Kern (BT/WLAN, "marlin") des UMS9230 ohne sipc starten.
 *
 * Redmi A5 (serenity), Mainline 7.1. Nur zum Ausprobieren, kein fertiger Treiber.
 *
 * Vorlage: Realme C33 (Kernel 5.4, gleicher Chip, GPL), github.com/Kiciuk/unisoc-5.4
 *   drivers/unisoc_platform/sprdwcn/boot/wcn_integrate_boot.c
 *     wcn_proc_native_start() -> wcn_poweron_device() -> wcn_wait_marlin_boot()
 *   drivers/unisoc_platform/sprdwcn/boot/wcn_integrate.c (Regler, merlion-GPIOs, DFS/RFI)
 *   drivers/unisoc_platform/sprdwcn/boot/wcn_integrate_dev.c (Probe: nur Schritt 0 der ctrl-reg-Liste)
 *   arch/arm64/boot/dts/sprd/ums9230-wcn.dtsi, drivers/regulator/sc2730-regulator.c
 *
 * Wichtig aus der Quelle:
 *  - Auf qogirl6 wird die 10-Schritte-Liste "sprd,ctrl-reg" NICHT zum Start benutzt
 *    (wcn_cpu_bootup() laeuft nur auf anderen Chips). Beim Probe wird nur Schritt 0
 *    geschrieben (PUB 0x9018 = 0x70, WCN-Adress-Umlenkung). Der Start selbst ist fest
 *    verdrahtet in wcn_poweron_device(); genau dieser Ablauf steht hier.
 *  - WCN-Register (0x51...) darf der AP nur lesen/schreiben, solange das WCN-System
 *    eingeschaltet UND wach ist, sonst Bus-Fehler ("AP SYS kernel crash"). Das erklaert
 *    den "synchronous external abort" vom 09.10. Deshalb prueft dieser Treiber vor jedem
 *    Zugriff auf 0x51... die PMU-Statusregister und bricht sonst ab.
 *
 * Stufen (Modulparameter "stufe"), eine Stufe pro Start (Regel 6):
 *   0  nur lesen: AON/PMU/PUB, PMIC-Regler, GPIOs, Sync-Speicher. Fasst 0x51... nie an.
 *   1  Strom: PUB-Umlenkung, Regler dcxo1v8 (1,8 V), vddwcn (1,2 V), vddwifipa, merlion-GPIOs.
 *   2  wie 1, dazu WCN-System einschalten und aufwecken (wcn_sys_power_up), dann stehen lassen.
 *   3  kompletter Start wie Android: Speicher vorbereiten, Firmware nach 0x87000000,
 *      Strom, WCN-System, PLL, BTWF einschalten, CPU loslassen, auf 0xF0F0F0FF warten.
 *
 * Ergebnis: dmesg (Praefix "wcntest:") und /sys/devices/platform/wcntest/zustand.
 * rmmod laesst den Zustand der Hardware stehen (schaltet nichts ab).
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/unaligned.h>
#include <asm/barrier.h>
#include <asm/sysreg.h>

#define TAG "wcntest: "

/* ---- Modulparameter ---------------------------------------------------- */

static int stufe;
module_param(stufe, int, 0444);
MODULE_PARM_DESC(stufe, "0=lesen 1=Strom 2=+WCN-System an 3=Firmware+CPU-Start");

static bool trotzdem;
module_param(trotzdem, bool, 0444);
MODULE_PARM_DESC(trotzdem, "auch starten, wenn das WCN-System schon an ist");

static char *fw_name = "wcnmodem.bin";
module_param(fw_name, charp, 0444);
MODULE_PARM_DESC(fw_name, "Firmware in /lib/firmware (aus odm_a:/firmware/wcnmodem.bin)");

static char *pmic = "spi4.0";
module_param(pmic, charp, 0444);
MODULE_PARM_DESC(pmic, "SPI-Geraet des PMIC SC2730 (regmap)");

static char *gpiochip = "641b0000.gpio";
module_param(gpiochip, charp, 0444);
MODULE_PARM_DESC(gpiochip, "Label des ap_gpio-Controllers");

/*
 * Realme schreibt nach 0xF0F0F0FF 0x13579BDF zurueck (wcn_marlin_boot_finish). Auf dem Redmi stand
 * nach Android aber 0xF0F0F0FF / 0xEFEFFEFE im Speicher - Xiaomi macht das offenbar nicht (oder der
 * Kern ueberschreibt es). Deshalb standardmaessig aus.
 */
static bool fertig_melden;
module_param(fertig_melden, bool, 0444);
MODULE_PARM_DESC(fertig_melden, "nach 0xF0F0F0FF wie Realme 0x13579BDF zurueckschreiben (Standard aus)");

/* ---- Adressen (Xiaomi-dtbo = Realme ums9230-wcn.dtsi / qogirl6.dtsi) ----- */

#define AON_APB_PHYS		0x64000000	/* aon_apb_regs, 0x3000 */
#define PMU_APB_PHYS		0x64020000	/* pmu_apb_regs, 0x3000 */
#define PUB_APB_PHYS		0x60008000	/* pub_apb_regs + 0x8000 (0x8018 / SET 0x9018) */
#define WCN_AON_APB_PHYS	0x5180c000	/* wcncp_aon_apb_regs */
#define WCN_AON_AHB_PHYS	0x51880000	/* wcncp_aon_ahb_regs */
#define WCN_BTWF_AHB_PHYS	0x51130000	/* wcncp_btwf_ahb_regs */

#define REG_SET			0x1000		/* SET-Spiegel */
#define REG_CLR			0x2000		/* CLEAR-Spiegel */

#define BTWF_BASE		0x87000000	/* cpwcn-btwf reg, 2 MB, sprd,file-length */
#define BTWF_LEN		0x200000
#define SYNC_PHYS		0x877fd000	/* Seite mit Sync-Bereich, bis 0x87800000 */
#define SYNC_LEN		0x3000
#define APCP_SYNC_ADDR		0x007fdc00	/* sprd,apcp-sync-addr, Sicht des WCN-Kerns */

/* struct qogirl6_wcn_special_share_mem ab BTWF_BASE + 0x7fdc00 (Offsets in der SYNC-Seite) */
#define S_INIT_STATUS		0xc00	/* marlin.init_status          0x877fdc00 */
#define S_TEMPER_MAGIC		0xc18	/* efuse_temper_magic          0x877fdc18 */
#define S_TEMPER_VAL		0xc1c	/* efuse_temper_val            0x877fdc1c */
#define S_CP2_SLEEP		0xc10	/* cp2_sleep_status            0x877fdc10 */
#define S_SLEEP_FLAG		0xc14	/* sleep_flag_addr             0x877fdc14 */
#define S_CALI_DATA		0xc2c	/* wifi.calibration_data       0x877fdc2c */
#define S_CALI_LEN		0xf44	/* sizeof(struct wifi_calibration) = 616 + 3292 */
#define S_WIFI_EFUSE		0x1b70	/* wifi.efuse[0..2]            0x877feb70 */
#define S_CALI_FLAG		0x1b7c	/* wifi.calibration_flag       0x877feb7c */
#define S_DFS			0x2b00	/* WCN_SYS_DFS_SYNC_ADDR_OFFSET 0x877ffb00 */
#define S_DFS_LEN		16	/* struct wcn_dfs_sync_info */
#define S_RFI			0x2b10	/* WCN_SYS_RFI_SYNC_ADDR_OFFSET 0x877ffb10 */

#define MAGIC_START		0x5a5a5a5a	/* MARLIN_CP_INIT_START_MAGIC */
#define MAGIC_READY		0xf0f0f0ff	/* UMW2631_MARLIN_CP_INIT_READY_MAGIC */
#define MAGIC_SUCCESS		0x13579bdf	/* MARLIN_CP_INIT_SUCCESS_MAGIC */
#define MAGIC_FAILED		0x88888888	/* MARLIN_CP_INIT_FAILED_MAGIC */
#define CALI_FLAG_SET		0xefeffefe	/* WIFI_CALIBRATION_FLAG_VALUE */
#define CALI_FLAG_CLEAR		0x12345678	/* WIFI_CALIBRATION_FLAG_CLEAR_VALUE */
#define SEC_IMAGE_MAGIC		0x42544844	/* "DHTB": signiert -> braeuchte trusty */

/* PMIC SC2730, Basis 0x1800 (sc2730-regulator.c) */
#define PMIC_PWR_WR_PROT	0x1bd0	/* Schreibschutz der Regler */
#define PMIC_WR_UNLOCK		0x6e7f	/* sc2730_regulator_unlock() */
#define LDO_VDDWCN_PD		0x191c
#define LDO_VDDWCN_VOL		0x1920	/* 6 Bit, 900 mV + n * 15 mV */
#define LDO_VDDSIM2_PD		0x1994	/* = dcxo1v8 */
#define LDO_VDDSIM2_VOL		0x1998	/* 8 Bit, 1200 mV + n * 10 mV */
#define LDO_VDDWIFIPA_PD	0x19d0
#define LDO_VDDWIFIPA_VOL	0x19d4	/* 8 Bit, 1200 mV + n * 10 mV */

#define GPIO_MERLION_RST	117
#define GPIO_MERLION_EN		118
#define GPIO_XTAL_SEL		173

#define POLL_N			256	/* *_POLLING_COUNT */
#define POLL_STABIL		3	/* WCN_REG_POLL_STABLE_COUNT */

/* ---- Zustand ---------------------------------------------------------- */

struct block {
	const char *name;
	phys_addr_t phys;
	size_t len;
	bool wcn_seite;		/* nur zugreifen, wenn WCN-System an und wach */
	void __iomem *va;
};

enum { B_AON, B_PMU, B_PUB, B_WAON_APB, B_WAON_AHB, B_BTWF_AHB, B_ANZ };

static struct block bl[B_ANZ] = {
	[B_AON]      = { "AON",      AON_APB_PHYS,      0x3000, false },
	[B_PMU]      = { "PMU",      PMU_APB_PHYS,      0x3000, false },
	[B_PUB]      = { "PUB",      PUB_APB_PHYS,      0x2000, false },
	[B_WAON_APB] = { "WCN-AON-APB", WCN_AON_APB_PHYS, 0x1000, true },
	[B_WAON_AHB] = { "WCN-AON-AHB", WCN_AON_AHB_PHYS, 0x1000, true },
	[B_BTWF_AHB] = { "WCN-BTWF-AHB", WCN_BTWF_AHB_PHYS, 0x1000, true },
};

static struct platform_device *pdev;
static struct device *pmic_dev;
static struct regmap *pmic_map;
static struct gpio_device *gdev;
static int gpio_basis = -1;
static bool gpio_en_req, gpio_rst_req;
static void *mem_fw;		/* 0x87000000, 2 MB */
static void *mem_sync;		/* 0x877fd000, 12 KB */
static bool wcn_wach;		/* WCN-Seite freigegeben (nach erfolgreichem Aufwecken) */
static bool mem_ungecacht;	/* true: memremap WC hat geklappt (wie Android: nocache) */
static const char *ergebnis = "noch nicht gelaufen";

static bool wcn_sys_an(void);

/* ---- Cache: der WCN-Kern liest/schreibt DDR direkt am Cache vorbei --------- */

/*
 * Android greift ungecacht zu (wcn_mem_ram_vmap_nocache). Geht das hier nicht (Speicher liegt
 * in der Linear-Abbildung, dann verweigert arm64 eine WC-Abbildung), nehmen wir WB und spuelen
 * jede Cachezeile selbst. Nachteil WB: ein Schreiben raeumt die ganze Zeile zurueck und koennte
 * ein gleichzeitiges Schreiben des WCN-Kerns in derselben Zeile ueberdecken (kleines Fenster).
 */
static void cache_spuelen(void *p, size_t n)
{
	u64 ctr;
	unsigned long line, a, e;

	if (mem_ungecacht) {
		dsb(sy);
		return;
	}
	ctr = read_sysreg(ctr_el0);
	line = 4UL << ((ctr >> 16) & 0xf);	/* DminLine */
	a = (unsigned long)p & ~(line - 1);
	e = (unsigned long)p + n;
	for (; a < e; a += line)
		asm volatile("dc civac, %0" : : "r"(a) : "memory");
	dsb(sy);
}

static u32 mem_rd(void *base, u32 off)
{
	cache_spuelen(base + off, 4);
	return READ_ONCE(*(u32 *)(base + off));
}

static void mem_wr(void *base, u32 off, u32 val)
{
	WRITE_ONCE(*(u32 *)(base + off), val);
	cache_spuelen(base + off, 4);
}

/* ---- Registerzugriff mit Schutz --------------------------------------- */

/*
 * WCN-Seite nur, wenn freigegeben UND das WCN-System gerade an+wach ist (jedes Mal neu
 * geprueft: nach "Tiefschlaf erlauben" kann es jederzeit einschlafen -> Bus-Fehler).
 */
static bool zugriff_ok(int b)
{
	if (!bl[b].wcn_seite)
		return true;
	if (wcn_wach && wcn_sys_an())
		return true;
	pr_err(TAG "ABBRUCH: Zugriff auf %s verweigert - WCN-System nicht (mehr) an+wach\n",
	       bl[b].name);
	wcn_wach = false;
	return false;
}

static u32 rd(int b, u32 off)
{
	if (!zugriff_ok(b))
		return 0xdeadbeef;
	return readl(bl[b].va + off);
}

static void wr(int b, u32 off, u32 val, const char *was)
{
	if (!zugriff_ok(b))
		return;
	writel(val, bl[b].va + off);
	pr_info(TAG "  %s+0x%04x <- 0x%08x  (%s)\n", bl[b].name, off, val, was);
}

/* Liest off POLL_STABIL-mal hintereinander, alle muessen passen (wie *_is_*_status) */
static bool stabil(int b, u32 off, u32 mask, u32 soll)
{
	int i;

	for (i = 0; i < POLL_STABIL; i++)
		if ((rd(b, off) & mask) != soll)
			return false;
	return true;
}

/* wie *_polling_*: bis zu 256 Versuche im Abstand 64..128 us */
static bool warte(int b, u32 off, u32 mask, u32 soll, const char *was)
{
	int i;

	for (i = 0; i < POLL_N; i++) {
		if (stabil(b, off, mask, soll)) {
			pr_info(TAG "  %s: ja (Versuch %d, %s+0x%04x=0x%08x)\n",
				was, i, bl[b].name, off, rd(b, off));
			return true;
		}
		usleep_range(64, 128);
	}
	pr_err(TAG "  %s: NEIN nach %d Versuchen (%s+0x%04x=0x%08x)\n",
	       was, POLL_N, bl[b].name, off, rd(b, off));
	return false;
}

/* WCN-System an (PMU 0x538 Bit 28:24 = 0) und wach (PMU 0x860 Bit 31:28 = 6) */
static bool wcn_sys_an(void)
{
	return stabil(B_PMU, 0x538, 0x1f000000, 0) &&
	       stabil(B_PMU, 0x860, 0xf0000000, 0x60000000);
}

/* ---- Lesen (Stufe 0 und Zustandsdatei) -------------------------------- */

static int pmic_rd(u32 reg, u32 *v)
{
	int ret = regmap_read(pmic_map, reg, v);

	if (ret)
		pr_err(TAG "PMIC 0x%04x lesen: Fehler %d\n", reg, ret);
	return ret;
}

static int gpio_zustand(int nr, char *buf, size_t n)
{
	struct gpio_desc *d;
	int dir, val;

	if (!gdev)
		return scnprintf(buf, n, "GPIO %d: kein Controller\n", nr);
	d = gpio_device_get_desc(gdev, nr);
	if (IS_ERR(d))
		return scnprintf(buf, n, "GPIO %d: Fehler %ld\n", nr, PTR_ERR(d));
	dir = gpiod_get_direction(d);
	val = gpiod_get_raw_value(d);
	return scnprintf(buf, n, "GPIO %d: %s, Wert %d\n", nr,
			 dir == GPIO_LINE_DIRECTION_OUT ? "Ausgang" :
			 dir == GPIO_LINE_DIRECTION_IN ? "Eingang" : "?", val);
}

static ssize_t zustand_schreiben(char *buf, size_t n)
{
	ssize_t p = 0;
	u32 pd, vol;
	bool an;

	p += scnprintf(buf + p, n - p, "Stufe %d, Ergebnis: %s\n", stufe, ergebnis);
	p += scnprintf(buf + p, n - p,
		"AON 0x354=0x%08x 0x360=0x%08x 0x364=0x%08x 0x34c=0x%08x\n",
		rd(B_AON, 0x354), rd(B_AON, 0x360), rd(B_AON, 0x364), rd(B_AON, 0x34c));
	p += scnprintf(buf + p, n - p,
		"PMU 0x3a8=0x%08x 0x538=0x%08x 0x818=0x%08x 0x860=0x%08x\n",
		rd(B_PMU, 0x3a8), rd(B_PMU, 0x538), rd(B_PMU, 0x818), rd(B_PMU, 0x860));
	p += scnprintf(buf + p, n - p, "PUB 0x8018=0x%08x (WCN-Umlenkung, Android 0x70)\n",
		rd(B_PUB, 0x18));

	an = wcn_sys_an();
	p += scnprintf(buf + p, n - p, "WCN-System an+wach: %s\n", an ? "ja" : "nein");
	if (an && wcn_wach) {
		p += scnprintf(buf + p, n - p,
			"WCN-AON-APB 0x098=0x%08x  WCN-AON-AHB 0x00c=0x%08x 0x04c=0x%08x 0x0c4=0x%08x\n",
			rd(B_WAON_APB, 0x98), rd(B_WAON_AHB, 0x0c),
			rd(B_WAON_AHB, 0x4c), rd(B_WAON_AHB, 0xc4));
	}

	if (pmic_map) {
		if (!pmic_rd(LDO_VDDSIM2_PD, &pd) && !pmic_rd(LDO_VDDSIM2_VOL, &vol))
			p += scnprintf(buf + p, n - p, "dcxo1v8 (VDDSIM2): %s, %u mV\n",
				       pd & 1 ? "aus" : "an", 1200 + (vol & 0xff) * 10);
		if (!pmic_rd(LDO_VDDWCN_PD, &pd) && !pmic_rd(LDO_VDDWCN_VOL, &vol))
			p += scnprintf(buf + p, n - p, "vddwcn: %s, %u mV\n",
				       pd & 1 ? "aus" : "an", 900 + (vol & 0x3f) * 15);
		if (!pmic_rd(LDO_VDDWIFIPA_PD, &pd) && !pmic_rd(LDO_VDDWIFIPA_VOL, &vol))
			p += scnprintf(buf + p, n - p, "vddwifipa: %s, %u mV\n",
				       pd & 1 ? "aus" : "an", 1200 + (vol & 0xff) * 10);
	}

	p += gpio_zustand(GPIO_MERLION_EN, buf + p, n - p);
	p += gpio_zustand(GPIO_MERLION_RST, buf + p, n - p);
	p += gpio_zustand(GPIO_XTAL_SEL, buf + p, n - p);

	if (mem_sync) {
		p += scnprintf(buf + p, n - p,
			"Sync: init_status 0x877fdc00=0x%08x  cali_flag 0x877feb7c=0x%08x\n",
			mem_rd(mem_sync, S_INIT_STATUS), mem_rd(mem_sync, S_CALI_FLAG));
		p += scnprintf(buf + p, n - p,
			"      cp2_sleep=0x%08x sleep_flag=0x%08x dfs=0x%08x/0x%08x rfi=0x%08x\n",
			mem_rd(mem_sync, S_CP2_SLEEP), mem_rd(mem_sync, S_SLEEP_FLAG),
			mem_rd(mem_sync, S_DFS), mem_rd(mem_sync, S_DFS + 4),
			mem_rd(mem_sync, S_RFI));
	}
	if (mem_fw)
		p += scnprintf(buf + p, n - p, "0x87000000: 0x%08x 0x%08x (Stapel, Einsprung)\n",
			       mem_rd(mem_fw, 0), mem_rd(mem_fw, 4));
	return p;
}

static ssize_t zustand_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return zustand_schreiben(buf, PAGE_SIZE);
}
static DEVICE_ATTR_RO(zustand);

static void zustand_ins_log(const char *wann)
{
	char *buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	char *z, *s;

	if (!buf)
		return;
	zustand_schreiben(buf, PAGE_SIZE);
	pr_info(TAG "---- Zustand %s ----\n", wann);
	for (s = buf; (z = strsep(&s, "\n")) != NULL; )
		if (*z)
			pr_info(TAG "%s\n", z);
	kfree(buf);
}

/* ---- Stufe 1: Strom --------------------------------------------------- */

/* Regler einschalten; sel < 0: Spannung nicht anfassen (wie Android) */
static int ldo_an(const char *name, u32 pd_reg, u32 vol_reg, u32 vol_mask,
		  int sel, unsigned int mv_basis, unsigned int mv_schritt)
{
	u32 pd, vol;
	int ret;

	ret = pmic_rd(pd_reg, &pd) ?: pmic_rd(vol_reg, &vol);
	if (ret)
		return ret;
	pr_info(TAG "  %s vorher: PD 0x%04x=0x%x (%s), VOL 0x%04x=0x%x (%u mV)\n", name,
		pd_reg, pd, pd & 1 ? "aus" : "an", vol_reg, vol,
		mv_basis + (vol & vol_mask) * mv_schritt);

	if (sel >= 0 && (vol & vol_mask) != sel) {
		ret = regmap_update_bits(pmic_map, vol_reg, vol_mask, sel);
		if (ret)
			return ret;
		udelay(100);	/* Rampe 25 mV/us, groesster Sprung hier 1,2 V */
		ret = pmic_rd(vol_reg, &vol);
		if (ret)
			return ret;
		if ((vol & vol_mask) != sel) {
			/* z. B. Schreibschutz: NICHT mit falscher Spannung einschalten (dcxo1v8 sonst 3 V) */
			pr_err(TAG "  %s: Spannung nicht uebernommen (VOL=0x%x statt 0x%x) - bleibt aus\n",
			       name, vol, sel);
			return -EIO;
		}
	}
	ret = regmap_update_bits(pmic_map, pd_reg, BIT(0), 0);	/* PD-Bit 0 = 0 -> an */
	if (ret)
		return ret;

	ret = pmic_rd(pd_reg, &pd) ?: pmic_rd(vol_reg, &vol);
	if (ret)
		return ret;
	pr_info(TAG "  %s nachher: %s, %u mV\n", name, pd & 1 ? "AUS (?)" : "an",
		mv_basis + (vol & vol_mask) * mv_schritt);
	return (pd & 1) ? -EIO : 0;
}

static int gpio_holen(void)
{
	int ret;

	gdev = gpio_device_find_by_label(gpiochip);
	if (!gdev) {
		pr_err(TAG "GPIO-Controller \"%s\" nicht gefunden\n", gpiochip);
		return -ENODEV;
	}
	gpio_basis = gpio_device_get_base(gdev);
	pr_info(TAG "GPIO-Controller %s, Basis %d\n", gpiochip, gpio_basis);
	if (stufe < 1)
		return 0;

	ret = gpio_request(gpio_basis + GPIO_MERLION_EN, "wcntest merlion-chip-en");
	if (ret) {
		pr_err(TAG "GPIO %d (chip-en) belegt: %d\n", GPIO_MERLION_EN, ret);
		return ret;
	}
	gpio_en_req = true;
	ret = gpio_request(gpio_basis + GPIO_MERLION_RST, "wcntest merlion-rst");
	if (ret) {
		pr_err(TAG "GPIO %d (rst) belegt: %d\n", GPIO_MERLION_RST, ret);
		return ret;
	}
	gpio_rst_req = true;
	return 0;
}

/* wcn_merlion_power_on(): chip-en = 1, 500 us, rst 0, 500 us, rst 1 */
static int merlion_an(void)
{
	int en = gpio_basis + GPIO_MERLION_EN, rst = gpio_basis + GPIO_MERLION_RST;
	int ret;

	/* Android-Probe legt beide Leitungen lange vorher auf 0 (GPIOD_OUT_LOW) */
	ret = gpio_direction_output(rst, 0) ?: gpio_direction_output(en, 0);
	if (ret)
		return ret;
	usleep_range(1000, 1100);
	gpio_set_value(en, 1);
	udelay(500);
	gpio_set_value(rst, 0);
	udelay(500);
	gpio_set_value(rst, 1);
	pr_info(TAG "  merlion: chip-en=%d rst=%d\n", gpio_get_value(en), gpio_get_value(rst));
	return 0;
}

/* Probe-Schritt (ctrl-reg[0]): PUB 0x9018 (SET) <- 0x70, Maske 0x3fff */
static void pub_umlenkung(void)
{
	pr_info(TAG "PUB-Umlenkung: 0x60008018 vorher 0x%08x\n", rd(B_PUB, 0x18));
	wr(B_PUB, 0x18 + REG_SET, 0x70, "WCN-Adress-Umlenkung, wie wcn_probe");
	usleep_range(1000, 1100);	/* ctrl-us-delay[0] = 1000 */
	pr_info(TAG "PUB-Umlenkung: 0x60008018 nachher 0x%08x\n", rd(B_PUB, 0x18));
}

/* wcn_power_clock_support(true) */
static int strom_an(void)
{
	int ret;

	pr_info(TAG "Strom (wcn_power_clock_support):\n");
	/* Ohne Mainline-SC2730-Reglertreiber hat niemand den Schreibschutz aufgehoben */
	ret = regmap_write(pmic_map, PMIC_PWR_WR_PROT, PMIC_WR_UNLOCK);
	if (ret)
		return ret;
	pr_info(TAG "  PMIC 0x%04x <- 0x%04x (Regler-Schreibschutz aufheben, wie sc2730_regulator_unlock)\n",
		PMIC_PWR_WR_PROT, PMIC_WR_UNLOCK);
	/* Probe setzt dcxo1v8 auf 1,8 V (Grundwert 3 V!), vddwcn auf 1,2 V; vddwifipa nur bei Chip "AA" */
	ret = ldo_an("dcxo1v8", LDO_VDDSIM2_PD, LDO_VDDSIM2_VOL, 0xff, 60, 1200, 10);
	if (ret)
		return ret;
	usleep_range(10, 15);
	ret = ldo_an("vddwcn", LDO_VDDWCN_PD, LDO_VDDWCN_VOL, 0x3f, 20, 900, 15);
	if (ret)
		return ret;
	usleep_range(10, 15);
	ret = merlion_an();
	if (ret)
		return ret;
	usleep_range(10, 15);
	usleep_range(10000, 30000);	/* VDDWIFIPA_VDDCON_MIN/MAX_INTERVAL_TIME */
	return ldo_an("vddwifipa", LDO_VDDWIFIPA_PD, LDO_VDDWIFIPA_VOL, 0xff, -1, 1200, 10);
}

/* ---- Stufe 2: WCN-System einschalten ---------------------------------- */

/* btwf_gnss_force_unshutdown() */
static void unshutdown(void)
{
	u32 v = rd(B_AON, 0x360);

	pr_info(TAG "btwf_gnss_force_unshutdown: AON 0x360=0x%08x\n", v);
	wr(B_AON, 0x360, v | (0x6 << 21), "Bit 22:21 setzen");
	pr_info(TAG "  AON 0x360 jetzt 0x%08x\n", rd(B_AON, 0x360));
}

/* wcn_sys_power_up() */
static int wcn_sys_hoch(void)
{
	pr_info(TAG "wcn_sys_power_up: PMU 0x3a8=0x%08x\n", rd(B_PMU, 0x3a8));
	wr(B_PMU, 0x3a8 + REG_CLR, 0xffff << 8, "Verzoegerungszaehler loeschen");
	wr(B_PMU, 0x3a8 + REG_SET, 0x0204 << 8, "Verzoegerungszaehler 0x0204");
	pr_info(TAG "  PMU 0x3a8=0x%08x (Delay cnt)\n", rd(B_PMU, 0x3a8));
	wr(B_PMU, 0x3a8 + REG_CLR, BIT(24), "auto shutdown aus");
	wr(B_PMU, 0x3a8 + REG_CLR, BIT(25), "force shutdown aus");
	pr_info(TAG "  PMU 0x3a8=0x%08x\n", rd(B_PMU, 0x3a8));
	if (!warte(B_PMU, 0x538, 0x1f000000, 0, "WCN-System an (PMU 0x538)"))
		return -ETIMEDOUT;

	pr_info(TAG "  PMU 0x818=0x%08x\n", rd(B_PMU, 0x818));
	wr(B_PMU, 0x818 + REG_CLR, BIT(7), "force deep sleep aus");
	if (!warte(B_PMU, 0x860, 0xf0000000, 0x60000000, "WCN-System wach (PMU 0x860)"))
		return -ETIMEDOUT;

	msleep(8);	/* WCN_SYS_POWER_ON_WAKEUP_TIME: XTL, PLL1/2 */

	/* Ab hier darf der AP die WCN-Seite anfassen */
	if (!wcn_sys_an()) {
		pr_err(TAG "  WCN-System nach 8 ms nicht mehr an/wach\n");
		return -EIO;
	}
	wcn_wach = true;

	/* Android prueft nur zur Sicherheit nach und macht immer weiter */
	warte(B_AON, 0x364, 0xf << 7, 0x6 << 7, "BTWF wach (AON 0x364 Bit 10:7)");
	warte(B_AON, 0x360, 0x1f << 25, 0, "BTWF an (AON 0x360 Bit 29:25)");
	warte(B_AON, 0x364, 0xf << 3, 0x6 << 3, "GNSS wach (AON 0x364 Bit 6:3)");
	warte(B_AON, 0x360, 0x1f << 5, 0, "GNSS an (AON 0x360 Bit 9:5)");
	return 0;
}

/* ---- Stufe 3: Firmware und CPU-Start ---------------------------------- */

static void rmw(int b, u32 off, u32 setzen, u32 loeschen, const char *was)
{
	u32 v = rd(b, off);

	pr_info(TAG "  %s+0x%04x war 0x%08x\n", bl[b].name, off, v);
	wr(b, off, (v & ~loeschen) | setzen, was);
	pr_info(TAG "  %s+0x%04x ist 0x%08x\n", bl[b].name, off, rd(b, off));
}

/* pll1_pll2_stable_time(): nur ODER, wie im Original */
static void pll_zeiten(void)
{
	pr_info(TAG "pll1_pll2_stable_time:\n");
	rmw(B_WAON_APB, 0x3c8, 0x1458 << 12, 0, "btwf pll2");
	rmw(B_WAON_APB, 0x3c4, 0x1458 << 12, 0, "btwf pll1");
	rmw(B_WAON_APB, 0x3cc, 0x1458 << 12, 0, "gnss pll");
	rmw(B_WAON_APB, 0x19c, 0x37 << 16, 0, "btwf pll1");
	rmw(B_WAON_APB, 0x168, 0x7 << 16, 0, "btwf pll2");
}

/* wcn_sys_allow_deep_sleep() + wcn_ip_allow_sleep(true) */
static void tiefschlaf_erlauben(void)
{
	pr_info(TAG "wcn_sys_allow_deep_sleep:\n");
	rmw(B_WAON_APB, 0x90, BIT(30), 0, "sfware_core_lv_en");
	pr_info(TAG "  WCN-AON-AHB 0x0c4 war 0x%08x\n", rd(B_WAON_AHB, 0xc4));
	wr(B_WAON_AHB, 0xc4, 0xffffffff, "WCN_AON_IP_STOP");
}

/* gnss_sys_force_deep_to_shutdown(): nur BTWF soll laufen */
static void gnss_aus(void)
{
	pr_info(TAG "gnss_sys_force_deep_to_shutdown:\n");
	rmw(B_WAON_APB, 0xc8, BIT(3), 0, "gnss force deep");
	rmw(B_WAON_APB, 0xc8, BIT(12), 0, "gnss auto shutdown");
}

/* btwf_clear_force_shutdown() */
static void btwf_shutdown_weg(void)
{
	pr_info(TAG "btwf_clear_force_shutdown:\n");
	rmw(B_AON, 0x360, 0, 0x6 << 21, "Bit 22:21 loeschen");
}

/* btwf_sys_poweron() */
static int btwf_an(void)
{
	pr_info(TAG "btwf_sys_poweron:\n");
	rmw(B_WAON_AHB, 0x0c, 0x55, 0, "CPU/SYS/Cache/Busmonitor in Reset");
	rmw(B_WAON_APB, 0x98, 0, BIT(3), "btwf force deep aus");
	rmw(B_WAON_APB, 0x98, 0, BIT(12), "btwf auto shutdown aus");
	rmw(B_WAON_APB, 0x98, 0, BIT(2), "btwf_ss_arm_sys_power_down aus");
	pr_info(TAG "  AON 0x354 war 0x%08x\n", rd(B_AON, 0x354));
	wr(B_AON, 0x354, 0, "wie Android");

	if (!warte(B_AON, 0x364, 0xf << 7, 0x6 << 7, "BTWF wach (AON 0x364 Bit 10:7)") ||
	    !warte(B_AON, 0x360, 0x1f << 25, 0, "BTWF an (AON 0x360 Bit 29:25)")) {
		pr_err(TAG "  BTWF kommt nicht hoch - CPU bleibt im Reset\n");
		return -ETIMEDOUT;
	}

	rmw(B_BTWF_AHB, 0x410, 0x3 << 24, 0, "Boot aus DDR");
	pr_info(TAG "  WCN-AON-AHB 0x04c war 0x%08x\n", rd(B_WAON_AHB, 0x4c));
	wr(B_WAON_AHB, 0x4c, APCP_SYNC_ADDR, "Sync-Adresse fuer den WCN-Kern");
	pr_info(TAG "  WCN-AON-AHB 0x04c ist 0x%08x\n", rd(B_WAON_AHB, 0x4c));
	rmw(B_WAON_AHB, 0x0c, 0, 0x55, "CPU LOSLASSEN");
	return 0;
}

static int firmware_laden(void)
{
	const struct firmware *fw;
	u32 sp, pc;
	int ret;

	ret = request_firmware(&fw, fw_name, &pdev->dev);
	if (ret) {
		pr_err(TAG "Firmware %s nicht gefunden (%d) - nach /lib/firmware kopieren\n",
		       fw_name, ret);
		return ret;
	}
	ret = -EINVAL;
	if (fw->size < 8 || fw->size > BTWF_LEN) {
		pr_err(TAG "Firmware-Groesse %zu unplausibel (max %u)\n", fw->size, BTWF_LEN);
		goto raus;
	}
	sp = get_unaligned_le32(fw->data);
	pc = get_unaligned_le32(fw->data + 4);
	if (sp == SEC_IMAGE_MAGIC) {
		pr_err(TAG "Firmware ist signiert (DHTB) - braeuchte trusty, Abbruch\n");
		goto raus;
	}
	if (sp >= 0x800000 || !(pc & 1)) {	/* WCN-Kern sieht 8 MB ab 0x87000000 */
		pr_err(TAG "keine Cortex-M-Vektortabelle (Stapel 0x%08x, Einsprung 0x%08x)\n", sp, pc);
		goto raus;
	}
	memcpy(mem_fw, fw->data, fw->size);
	cache_spuelen(mem_fw, fw->size);
	if (memcmp(mem_fw, fw->data, fw->size)) {
		pr_err(TAG "Firmware nach dem Kopieren anders - Speicher 0x87000000 nicht beschreibbar?\n");
		goto raus;
	}
	pr_info(TAG "Firmware %s: %zu Bytes nach 0x87000000, Stapel 0x%08x, Einsprung 0x%08x\n",
		fw_name, fw->size, sp, pc);
	ret = 0;
raus:
	release_firmware(fw);
	return ret;
}

/* wcn_proc_native_start() fuer marlin, Vorbereitung vor dem Einschalten */
static int speicher_vorbereiten(void)
{
	u32 v;
	int ret;

	/* wcn_clean_marlin_ddr_flag() */
	pr_info(TAG "Sync vorher: init_status=0x%08x cali_flag=0x%08x\n",
		mem_rd(mem_sync, S_INIT_STATUS), mem_rd(mem_sync, S_CALI_FLAG));
	mem_wr(mem_sync, S_INIT_STATUS, MAGIC_START);
	mem_wr(mem_sync, S_CP2_SLEEP, 0);
	mem_wr(mem_sync, S_SLEEP_FLAG, 0);

	ret = firmware_laden();
	if (ret)
		return ret;

	/* wcn_marlin_pre_boot() -> marlin_write_cali_data(): auf qogirl6 Nullen + Merker */
	memset(mem_sync + S_CALI_DATA, 0, S_CALI_LEN);
	cache_spuelen(mem_sync + S_CALI_DATA, S_CALI_LEN);
	mem_wr(mem_sync, S_CALI_FLAG, CALI_FLAG_SET);

	/*
	 * Probe: wcn_dfs_status_clear() setzt alle 16 Bytes auf 0 (BTWF, GNSS, debug);
	 * danach wcn_dfs_poweron_status_clear() (BTWF Bit 7:0) - nach dem Nullen nichts mehr zu tun.
	 */
	memset(mem_sync + S_DFS, 0, S_DFS_LEN);
	cache_spuelen(mem_sync + S_DFS, S_DFS_LEN);

	/*
	 * Probe: wcn_marlin_write_efuse() schreibt wifi.efuse[0..2] (aus nvmem "wcn_efuse_blk0")
	 * und efuse_temper_magic. Die eFuse-Werte haben wir (noch) nicht -> nur anzeigen, was dasteht.
	 */
	pr_info(TAG "eFuse im Sync (nicht veraendert): wifi 0x%08x 0x%08x 0x%08x, temper magic 0x%08x val 0x%08x\n",
		mem_rd(mem_sync, S_WIFI_EFUSE), mem_rd(mem_sync, S_WIFI_EFUSE + 4),
		mem_rd(mem_sync, S_WIFI_EFUSE + 8), mem_rd(mem_sync, S_TEMPER_MAGIC),
		mem_rd(mem_sync, S_TEMPER_VAL));
	/* wcn_rfi_status_clear() (am Anfang von wcn_poweron_device) */
	v = mem_rd(mem_sync, S_RFI);
	mem_wr(mem_sync, S_RFI, 0);
	pr_info(TAG "Sync: init_status=0x%08x cali_flag=0x%08x dfs=0x%08x rfi war 0x%08x\n",
		mem_rd(mem_sync, S_INIT_STATUS), mem_rd(mem_sync, S_CALI_FLAG),
		mem_rd(mem_sync, S_DFS), v);
	return 0;
}

/* wcn_wait_marlin_boot() + wcn_marlin_boot_finish() */
static int auf_kern_warten(void)
{
	u32 v, alt = 0;
	int i;

	for (i = 0; i < POLL_N; i++) {
		v = mem_rd(mem_sync, S_INIT_STATUS);
		if (v != alt || i % 50 == 0)
			pr_info(TAG "  init_status=0x%08x (%d ms)\n", v, i * 20);
		alt = v;
		if (v == MAGIC_READY)
			break;
		msleep(20);
	}
	if (v != MAGIC_READY) {
		pr_err(TAG "WCN-Kern meldet sich nicht (init_status=0x%08x nach %d ms)\n",
		       v, POLL_N * 20);
		mem_wr(mem_sync, S_INIT_STATUS, MAGIC_FAILED);
		return -ETIMEDOUT;
	}
	pr_info(TAG "WCN-KERN LAEUFT: init_status=0xF0F0F0FF nach ca. %d ms\n", i * 20);

	if (fertig_melden) {
		u8 *c = mem_sync + S_CALI_DATA;
		int k, nicht_null = 0;

		cache_spuelen(c, S_CALI_LEN);
		for (k = 0; k < S_CALI_LEN; k++)
			nicht_null += c[k] != 0;
		pr_info(TAG "Kalibrierdaten vom Kern: %d von %d Bytes ungleich 0\n",
			nicht_null, S_CALI_LEN);
		mem_wr(mem_sync, S_CALI_FLAG, CALI_FLAG_CLEAR);
		mem_wr(mem_sync, S_INIT_STATUS, MAGIC_SUCCESS);
		pr_info(TAG "0x13579BDF zurueckgemeldet (wie wcn_marlin_boot_finish)\n");
	}
	return 0;
}

/* ---- Ablauf ----------------------------------------------------------- */

static int ablauf(void)
{
	int ret;

	if (stufe >= 1 && wcn_sys_an() && !trotzdem) {
		pr_err(TAG "WCN-System ist schon an - erst neu starten (oder trotzdem=1)\n");
		return -EBUSY;
	}

	if (stufe == 3) {
		ret = speicher_vorbereiten();
		if (ret)
			return ret;
	}
	if (stufe >= 1) {
		pub_umlenkung();
		ret = strom_an();
		if (ret) {
			pr_err(TAG "Strom fehlgeschlagen: %d\n", ret);
			return ret;
		}
	}
	if (stufe >= 2) {
		unshutdown();
		ret = wcn_sys_hoch();
		if (ret)
			return ret;
	}
	if (stufe == 2) {
		zustand_ins_log("nach Stufe 2");
		return 0;
	}
	if (stufe == 3) {
		pll_zeiten();
		tiefschlaf_erlauben();
		gnss_aus();
		btwf_shutdown_weg();
		if (!wcn_wach)	/* Schutz hat zwischendurch zugeschlagen */
			return -EIO;
		ret = btwf_an();
		if (ret)
			return ret;
		if (!wcn_wach)
			return -EIO;
		ret = auf_kern_warten();
		if (ret)
			return ret;
	}
	return 0;
}

static int karten_holen(void)
{
	int b;

	for (b = 0; b < B_ANZ; b++) {
		bl[b].va = ioremap(bl[b].phys, bl[b].len);
		if (!bl[b].va) {
			pr_err(TAG "ioremap %s fehlgeschlagen\n", bl[b].name);
			return -ENOMEM;
		}
	}

	/*
	 * Reservierter Speicher: zuerst ungecacht (WC, wie Android). Klappt nur bei no-map;
	 * liegt er in der Linear-Abbildung, WB und Cache selbst spuelen.
	 */
	mem_sync = memremap(SYNC_PHYS, SYNC_LEN, MEMREMAP_WC);
	mem_fw = memremap(BTWF_BASE, BTWF_LEN, MEMREMAP_WC);
	mem_ungecacht = mem_sync && mem_fw;
	if (!mem_ungecacht) {
		if (mem_sync)
			memunmap(mem_sync);
		if (mem_fw)
			memunmap(mem_fw);
		mem_sync = memremap(SYNC_PHYS, SYNC_LEN, MEMREMAP_WB);
		mem_fw = memremap(BTWF_BASE, BTWF_LEN, MEMREMAP_WB);
		if (!mem_sync || !mem_fw) {
			pr_err(TAG "memremap 0x%x / 0x%x fehlgeschlagen\n", SYNC_PHYS, BTWF_BASE);
			return -ENOMEM;
		}
	}
	pr_info(TAG "WCN-Speicher %s abgebildet\n",
		mem_ungecacht ? "ungecacht (WC, no-map)" : "gecacht (WB + Spuelen; liegt in der Linear-Abbildung)");

	pmic_dev = bus_find_device_by_name(&spi_bus_type, NULL, pmic);
	if (!pmic_dev) {
		pr_err(TAG "PMIC-Geraet %s nicht gefunden\n", pmic);
		return -ENODEV;
	}
	pmic_map = dev_get_regmap(pmic_dev, NULL);
	if (!pmic_map) {
		pr_err(TAG "PMIC %s hat keine regmap\n", pmic);
		return -ENODEV;
	}
	return 0;
}

static void aufraeumen(void)
{
	int b;

	if (pdev) {
		device_remove_file(&pdev->dev, &dev_attr_zustand);
		platform_device_unregister(pdev);
		pdev = NULL;
	}
	if (gpio_rst_req)
		gpio_free(gpio_basis + GPIO_MERLION_RST);
	if (gpio_en_req)
		gpio_free(gpio_basis + GPIO_MERLION_EN);
	gpio_rst_req = gpio_en_req = false;
	if (gdev)
		gpio_device_put(gdev);
	gdev = NULL;
	if (pmic_dev)
		put_device(pmic_dev);
	pmic_dev = NULL;
	pmic_map = NULL;
	if (mem_fw)
		memunmap(mem_fw);
	if (mem_sync)
		memunmap(mem_sync);
	mem_fw = mem_sync = NULL;
	for (b = 0; b < B_ANZ; b++) {
		if (bl[b].va)
			iounmap(bl[b].va);
		bl[b].va = NULL;
	}
}

static int __init wcntest_init(void)
{
	int ret;

	if (stufe < 0 || stufe > 3) {
		pr_err(TAG "stufe muss 0..3 sein\n");
		return -EINVAL;
	}
	pr_info(TAG "Start, Stufe %d (0=lesen 1=Strom 2=WCN-System 3=Firmware+CPU)\n", stufe);

	pdev = platform_device_register_simple("wcntest", PLATFORM_DEVID_NONE, NULL, 0);
	if (IS_ERR(pdev)) {
		ret = PTR_ERR(pdev);
		pdev = NULL;
		return ret;
	}

	ret = karten_holen();
	if (ret)
		goto fehler;
	ret = gpio_holen();
	if (ret)
		goto fehler;

	/* Wenn das WCN-System schon laeuft (zweiter Versuch), darf die Zustandsanzeige es lesen */
	wcn_wach = false;

	zustand_ins_log("vorher");
	ret = ablauf();
	ergebnis = ret ? "FEHLER (siehe dmesg)" :
		   stufe == 3 ? "WCN-Kern laeuft (0xF0F0F0FF)" : "Stufe ohne Fehler durchlaufen";
	if (stufe >= 1)
		zustand_ins_log("nachher");
	pr_info(TAG "Ende Stufe %d: %s (%d). Mailbox-Zaehler: grep mailbox /proc/interrupts\n",
		stufe, ergebnis, ret);

	/* Modul bleibt geladen, auch bei Fehler: Zustandsdatei lesbar, nichts wird zurueckgedreht */
	ret = device_create_file(&pdev->dev, &dev_attr_zustand);
	if (ret)
		pr_warn(TAG "Zustandsdatei: %d\n", ret);
	return 0;

fehler:
	aufraeumen();
	return ret;
}

static void __exit wcntest_exit(void)
{
	aufraeumen();
	pr_info(TAG "entladen - Hardware unveraendert (WCN laeuft ggf. weiter, Neustart setzt zurueck)\n");
}

module_init(wcntest_init);
module_exit(wcntest_exit);

MODULE_DESCRIPTION("Testtreiber: UMS9230-WCN-Kern (marlin) ohne sipc starten - Redmi A5");
MODULE_AUTHOR("EberhartLeberhart");
MODULE_LICENSE("GPL");
