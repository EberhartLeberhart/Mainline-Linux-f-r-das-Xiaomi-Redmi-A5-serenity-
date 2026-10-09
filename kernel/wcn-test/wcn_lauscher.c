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
 *
 * sipc-lite (Parameter antworten=1): Das Modul antwortet auf Kanal 5 (Firmware-Log) wie der
 * Realme-sbuf-Wirt (drivers/soc/sprd/modem/sipc/sbuf.c, AP = Wirt, WCN-Kern = Gast):
 *   1. WCN: OPEN (Kanal 5, 0xBEEE)   -> wir: OPEN (Kanal 5, 0xBEEE)        smsg_open_ack()
 *   2. WCN: CMD SBUF_INIT (Flag 1)   -> wir: DONE SBUF_INIT (Flag 2), Wert = Adresse des
 *      sbuf-Kopfs aus Sicht des WCN-Kerns
 *   3. WCN: EVENT WRPTR (Flag 1)     -> wir lesen den Empfangsring (rxbuf_rdptr .. rxbuf_wrptr);
 *      war er voll, melden wir EVENT RDPTR (Flag 2)
 * Gemeinsamer Speicher (Realme ums9230-wcn.dtsi, core@3): sprd,smem-info =
 *   <0x87240000 0x00240000 0x140000>  (AP-Adresse, Adresse fuer den WCN-Kern, Groesse)
 * Puffer fuer Kanal 5 wie Realme wcn_sipc.c (SIPC_LOG_RX): 1 Ring, tx 0x8000, rx 0x30000.
 * Das Log steht danach in /sys/devices/platform/wcn-lauscher/wcn_log (und gekuerzt im dmesg).
 *
 * Log-Format (belegt 09.10., erster Lauf): binaere Rahmen
 *   +0  7E 7E 7E 7E   Sync
 *   +4  u16 Laenge    Rest des Rahmens ab +8 ... (0x59c bei einem 1440-Byte-Rahmen = 1440 - 4)
 *   +6  u16 ?         (Pruefsumme/Folge?)
 *   +8  5A 5A         Magic
 *   +10 u16 Typ       0x0281 = Registerspur der RF-Kalibrierung (Paare Adresse<<16 | Wert)
 *   +12 u32 Nummer
 *   +16 u16 Nutzlaenge, +18 u16 ?, ab +20 Nutzdaten
 * Der Kern meldet WRPTR offenbar nur, wenn der Ring vorher leer war - deshalb wird nach
 * dem Lesen und alle 500 ms nachgesehen.
 * Andere Kanaele (4 = AT/BT/FM, 7 = WLAN) bleiben unbeantwortet.
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
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/kfifo.h>
#include <linux/unaligned.h>

#define TAG "wcnlausch: "

static int kanal = 8;
module_param(kanal, int, 0444);
MODULE_PARM_DESC(kanal, "Mailbox-Kanal (Kern-ID), WCN = 8");

static bool antworten;
module_param(antworten, bool, 0444);
MODULE_PARM_DESC(antworten, "sipc-lite: Kanal 5 (Firmware-Log) oeffnen und den sbuf bereitstellen");

/* smsg-Typen und Flags (Realme include/linux/sipc.h, sipc/sbuf.h) */
#define T_OPEN		1
#define T_CLOSE		2
#define T_EVENT		4
#define T_CMD		5
#define T_DONE		6
#define CMD_SBUF_INIT	0x0001
#define DONE_SBUF_INIT	0x0002
#define EV_WRPTR	0x0001
#define EV_RDPTR	0x0002

#define LOG_KANAL	5
#define SMEM_AP		0x87240000	/* sprd,smem-info core@3 */
#define SMEM_CP		0x00240000
#define SMEM_LEN	0x140000
#define SB_TX		0x8000		/* Linux -> WCN */
#define SB_RX		0x30000		/* WCN -> Linux (das Log) */
#define SB_HDR		36		/* sbuf_smem_header: ringnr + 1 * sbuf_ring_header (8 x u32) */
/* Offsets im sbuf-Kopf */
#define H_RINGNR	0
#define H_TX_ADDR	4
#define H_TX_SIZE	8
#define H_TX_RD		12
#define H_TX_WR		16
#define H_RX_ADDR	20
#define H_RX_SIZE	24
#define H_RX_RD		28
#define H_RX_WR		32

#define WLOG_MAX	(1024 * 1024)
#define DMESG_ZEILEN	400

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
	bool raus;	/* von uns gesendet */
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
		"#%u %llu.%06llu s %s: roh %08x %08x -> smsg Kanal %u, Typ %u %s, Flag 0x%04x%s, Wert 0x%08x\n",
		nr, e->zeit_ns / NSEC_PER_SEC, (e->zeit_ns % NSEC_PER_SEC) / 1000,
		e->raus ? "GESENDET " : "empfangen",
		e->lo, e->hi, ch, typ, typ_text(typ), flag, zusatz, e->hi);
}

static void merken(u32 lo, u32 hi, bool raus)
{
	struct eintrag e = { ktime_get_boottime_ns(), lo, hi, raus };
	unsigned long flags;
	unsigned int nr;
	char text[200];

	spin_lock_irqsave(&ring_lock, flags);
	nr = anzahl++;
	ring[nr % RING] = e;
	spin_unlock_irqrestore(&ring_lock, flags);

	zeile(text, sizeof(text), &e, nr);
	pr_info(TAG "%s", text);
}

/* ---- sipc-lite ------------------------------------------------------- */

struct smsg_roh {
	u32 lo, hi;
};

static DEFINE_KFIFO(eingang, struct smsg_roh, 64);
static DEFINE_SPINLOCK(eingang_lock);
static struct work_struct arbeit;
static bool arbeit_bereit;
static void *smem;		/* SMEM_AP, ungecacht */
static bool sb_bereit;
static char *wlog;		/* gesammeltes Firmware-Log */
static size_t wlog_len;
static unsigned int dmesg_zeilen;
static unsigned int ereignisse;

static void senden(u8 ch, u8 typ, u16 flag, u32 wert, const char *was)
{
	u32 m[2] = { ch | typ << 8 | (u32)flag << 16, wert };
	int ret;

	merken(m[0], m[1], true);
	ret = mbox_send_message(chan, m);	/* blockierend, tx_tout */
	if (ret < 0)
		pr_warn(TAG "  %s: Senden meldet %d (Quittung der Mailbox fehlt?)\n", was, ret);
	else
		pr_info(TAG "  %s gesendet\n", was);
}

static u32 sm_rd(u32 off)
{
	return READ_ONCE(*(u32 *)(smem + off));
}

static void sm_wr(u32 off, u32 v)
{
	WRITE_ONCE(*(u32 *)(smem + off), v);
}

/* sbuf_host_init() fuer 1 Ring */
static void sbuf_vorbereiten(void)
{
	memset(smem, 0, SB_HDR + SB_TX + SB_RX);
	sm_wr(H_RINGNR, 1);
	sm_wr(H_TX_ADDR, SMEM_CP + SB_HDR);
	sm_wr(H_TX_SIZE, SB_TX);
	sm_wr(H_TX_RD, 0);
	sm_wr(H_TX_WR, 0);
	sm_wr(H_RX_ADDR, SMEM_CP + SB_HDR + SB_TX);
	sm_wr(H_RX_SIZE, SB_RX);
	sm_wr(H_RX_RD, 0);
	sm_wr(H_RX_WR, 0);
	wmb();
	pr_info(TAG "sbuf fuer Kanal 5 bei 0x%x (WCN-Sicht 0x%x): tx 0x%x @0x%x, rx 0x%x @0x%x\n",
		SMEM_AP, SMEM_CP, SB_TX, sm_rd(H_TX_ADDR), SB_RX, sm_rd(H_RX_ADDR));
}

static size_t rahmen_pos;	/* bis hier sind die Rahmen in wlog ausgewertet */
static unsigned int rahmen_anzahl;

static bool druckbar(const u8 *p, size_t n)
{
	size_t i, gut = 0;

	for (i = 0; i < n; i++)
		gut += (p[i] >= 0x20 && p[i] < 0x7f) || p[i] == '\n' || p[i] == '\r' || p[i] == '\t';
	return n && gut * 10 >= n * 9;
}

static void text_ausgeben(const u8 *p, size_t n)
{
	char zeile[161];
	size_t i, z = 0;

	for (i = 0; i <= n && dmesg_zeilen < DMESG_ZEILEN; i++) {
		char c = i < n ? p[i] : '\n';

		if (c == '\n' || c == '\r' || c == 0 || z == sizeof(zeile) - 1) {
			if (z) {
				zeile[z] = 0;
				pr_info("wcnlog:   %s\n", zeile);
				dmesg_zeilen++;
			}
			z = 0;
			if (c == '\n' || c == '\r' || c == 0)
				continue;
		}
		zeile[z++] = (c >= 0x20 && c < 0x7f) ? c : '.';
	}
}

/* vollstaendige Rahmen ab rahmen_pos in wlog zerlegen und kurz im dmesg zeigen */
static void rahmen_auswerten(void)
{
	static const u8 sync[4] = { 0x7e, 0x7e, 0x7e, 0x7e };

	while (wlog && rahmen_pos + 20 <= wlog_len) {
		const u8 *basis = (const u8 *)wlog, *r = basis + rahmen_pos;
		u16 laenge, typ, nutz;
		u32 nr;

		if (memcmp(r, sync, 4)) {
			/* nicht im Takt: bis zum naechsten Sync weitersuchen */
			const u8 *t = memchr(r + 1, 0x7e, wlog_len - rahmen_pos - 1);

			if (dmesg_zeilen < DMESG_ZEILEN) {
				pr_info("wcnlog: %zu Bytes ohne Sync bei %zu\n",
					(size_t)((t ? t : basis + wlog_len) - r), rahmen_pos);
				dmesg_zeilen++;
			}
			if (!t) {
				rahmen_pos = wlog_len;
				return;
			}
			rahmen_pos = t - basis;
			continue;
		}
		laenge = get_unaligned_le16(r + 4);
		if (rahmen_pos + 4 + laenge > wlog_len)
			return;		/* Rahmen noch nicht vollstaendig */
		typ = get_unaligned_le16(r + 10);
		nr = get_unaligned_le32(r + 12);
		nutz = get_unaligned_le16(r + 16);
		rahmen_anzahl++;
		if (dmesg_zeilen < DMESG_ZEILEN) {
			const u8 *d = r + 20;
			size_t dn = min_t(size_t, nutz, laenge >= 16 ? laenge - 16 : 0);

			pr_info("wcnlog: Rahmen %u: Typ 0x%04x, Nr %u, %u Bytes, Kopf %*ph\n",
				rahmen_anzahl, typ, nr, nutz, 12, r + 4);
			dmesg_zeilen++;
			if (druckbar(d, dn)) {
				text_ausgeben(d, dn);
			} else if (dn >= 4) {
				pr_info("wcnlog:   %*ph ...\n", (int)min_t(size_t, dn, 32), d);
				dmesg_zeilen++;
			}
		}
		rahmen_pos += 4 + laenge;
	}
	if (dmesg_zeilen == DMESG_ZEILEN) {
		pr_info(TAG "weitere Rahmen nur noch in wcn_log\n");
		dmesg_zeilen++;
	}
}

static DEFINE_MUTEX(lese_lock);	/* Ereignis-Arbeit und Nachsehen koennen gleichzeitig laufen */

/* sbuf_read() fuer den Empfangsring */
static void log_lesen_ungeschuetzt(void);

static void log_lesen(void)
{
	mutex_lock(&lese_lock);
	log_lesen_ungeschuetzt();
	mutex_unlock(&lese_lock);
}

static void log_lesen_ungeschuetzt(void)
{
	u32 rd, wr, alt, pos, n;
	bool war_voll;

	if (!sb_bereit)
		return;
	rmb();
	rd = sm_rd(H_RX_RD);
	wr = sm_rd(H_RX_WR);
	alt = rd;
	if (rd == wr)
		return;
	war_voll = (wr - rd) >= SB_RX;
	if (wr - rd > SB_RX) {
		pr_warn(TAG "Ring uebergelaufen (rd %u, wr %u)\n", rd, wr);
		rd = wr - SB_RX;
	}
	while (rd != wr) {
		pos = rd % SB_RX;
		n = min(wr - rd, SB_RX - pos);
		if (wlog && wlog_len < WLOG_MAX) {
			u32 k = min_t(size_t, n, WLOG_MAX - wlog_len);

			memcpy(wlog + wlog_len, smem + SB_HDR + SB_TX + pos, k);
			wlog_len += k;
		}
		rd += n;
	}
	sm_wr(H_RX_RD, rd);
	wmb();
	pr_info(TAG "Log: %u Bytes gelesen (gesamt %zu)\n", rd - alt, wlog_len);
	rahmen_auswerten();
	if (war_voll)
		senden(LOG_KANAL, T_EVENT, EV_RDPTR, 0, "EVENT RDPTR (Ring war voll)");
}

/* Nachsehen ohne Ereignis: der Kern meldet nur, wenn der Ring vorher leer war */
static struct delayed_work nachsehen;

static void nachsehen_fn(struct work_struct *w)
{
	if (!arbeit_bereit)
		return;
	if (sb_bereit)
		log_lesen();
	schedule_delayed_work(&nachsehen, msecs_to_jiffies(500));
}

static void bearbeiten(u32 lo, u32 hi)
{
	u8 ch = lo & 0xff, typ = (lo >> 8) & 0xff;
	u16 flag = lo >> 16;

	if (ch != LOG_KANAL)
		return;		/* andere Kanaele: nur mitlesen */

	switch (typ) {
	case T_OPEN:
		if (flag == SMSG_OPEN_MAGIC) {
			if (sb_bereit) {	/* Neustart des Kerns: alte Daten verwerfen */
				sm_wr(H_RX_RD, sm_rd(H_RX_WR));
				sb_bereit = false;
			}
			senden(LOG_KANAL, T_OPEN, SMSG_OPEN_MAGIC, 0, "OPEN Kanal 5");
		}
		break;
	case T_CLOSE:
		sb_bereit = false;
		senden(LOG_KANAL, T_CLOSE, SMSG_CLOSE_MAGIC, 0, "CLOSE-Antwort Kanal 5");
		break;
	case T_CMD:
		if (flag == CMD_SBUF_INIT && !sb_bereit) {
			sb_bereit = true;
			senden(LOG_KANAL, T_DONE, DONE_SBUF_INIT, SMEM_CP, "DONE SBUF_INIT (sbuf bei WCN 0x240000)");
			log_lesen();
		}
		break;
	case T_EVENT:
		if (flag == EV_WRPTR) {
			ereignisse++;
			log_lesen();
			log_lesen();	/* waehrend des Lesens Nachgeschobenes gleich mitnehmen */
		}
		break;
	}
}

static void arbeit_fn(struct work_struct *w)
{
	struct smsg_roh m;
	unsigned long flags;
	unsigned int n;

	for (;;) {
		spin_lock_irqsave(&eingang_lock, flags);
		n = kfifo_get(&eingang, &m);
		spin_unlock_irqrestore(&eingang_lock, flags);
		if (!n)
			break;
		bearbeiten(m.lo, m.hi);
	}
}

static void empfangen(struct mbox_client *c, void *msg)
{
	u32 *m = msg;
	struct smsg_roh r = { m[0], m[1] };
	unsigned long flags;

	merken(m[0], m[1], false);
	if (!antworten || !arbeit_bereit)
		return;
	spin_lock_irqsave(&eingang_lock, flags);
	if (!kfifo_put(&eingang, r))
		pr_warn_ratelimited(TAG "Eingang voll, Nachricht verloren\n");
	spin_unlock_irqrestore(&eingang_lock, flags);
	schedule_work(&arbeit);
}

static ssize_t wcn_log_read(struct file *f, struct kobject *k, const struct bin_attribute *a,
			    char *buf, loff_t pos, size_t n)
{
	if (!wlog || pos >= wlog_len)
		return 0;
	n = min_t(size_t, n, wlog_len - pos);
	memcpy(buf, wlog + pos, n);
	return n;
}
static const BIN_ATTR_RO(wcn_log, WLOG_MAX);
static bool bin_log;

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

	p = scnprintf(buf, PAGE_SIZE, "Kanal %d, %u Nachrichten%s\n", kanal, n,
		      n > RING ? " (nur die letzten 64 gezeigt)" : "");
	if (antworten && smem)
		p += scnprintf(buf + p, PAGE_SIZE - p,
			       "sipc-lite Kanal 5: sbuf %s, rx rd %u wr %u, tx rd %u wr %u, %u Ereignisse, %zu Bytes Log, %u Rahmen\n",
			       sb_bereit ? "BEREIT" : "nicht bereit", sm_rd(H_RX_RD), sm_rd(H_RX_WR),
			       sm_rd(H_TX_RD), sm_rd(H_TX_WR), ereignisse, wlog_len, rahmen_anzahl);
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
	arbeit_bereit = false;
	if (chan)
		mbox_free_channel(chan);
	chan = NULL;
	cancel_delayed_work_sync(&nachsehen);
	cancel_work_sync(&arbeit);
	if (pdev && bin_log)
		sysfs_remove_bin_file(&pdev->dev.kobj, &bin_attr_wcn_log);
	bin_log = false;
	vfree(wlog);
	wlog = NULL;
	smem = NULL;
	sb_bereit = false;
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

	INIT_WORK(&arbeit, arbeit_fn);
	INIT_DELAYED_WORK(&nachsehen, nachsehen_fn);
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

	/* WC wie Android (nocache); klappt nur bei no-map - sonst lieber gar nicht */
	ram_btwf.va = memremap(ram_btwf.phys, ram_btwf.len, MEMREMAP_WC);
	ram_hoch.va = memremap(ram_hoch.phys, ram_hoch.len, MEMREMAP_WC);

	if (antworten) {
		if (!ram_btwf.va) {
			pr_err(TAG "antworten=1 braucht den ungecachten WCN-Speicher - Abbruch\n");
			ret = -ENOMEM;
			goto fehler;
		}
		smem = ram_btwf.va + (SMEM_AP - ram_btwf.phys);
		wlog = vzalloc(WLOG_MAX);
		if (!wlog) {
			ret = -ENOMEM;
			goto fehler;
		}
		sbuf_vorbereiten();
		arbeit_bereit = true;
		schedule_delayed_work(&nachsehen, msecs_to_jiffies(500));
	}

	cl.dev = &pdev->dev;
	cl.rx_callback = empfangen;
	cl.tx_block = true;		/* nur aus dem Arbeits-Thread gesendet */
	cl.tx_tout = 100;		/* ms; kommt keine Quittung, geht es trotzdem weiter */
	cl.knows_txdone = false;
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

	if (wlog && !sysfs_create_bin_file(&pdev->dev.kobj, &bin_attr_wcn_log))
		bin_log = true;
	if (ram_btwf.va && !sysfs_create_bin_file(&pdev->dev.kobj, &bin_attr_wcn_ram_btwf))
		bin_btwf = true;
	if (ram_hoch.va && !sysfs_create_bin_file(&pdev->dev.kobj, &bin_attr_wcn_ram_hoch))
		bin_hoch = true;
	pr_info(TAG "WCN-Speicher lesbar: wcn_ram_btwf %s, wcn_ram_hoch %s\n",
		bin_btwf ? "ja" : "NEIN", bin_hoch ? "ja" : "NEIN");
	pr_info(TAG "hoere auf Mailbox-Kanal %d%s - jetzt wcn_starttest stufe=3 laden\n", kanal,
		antworten ? ", sipc-lite fuer Kanal 5 (Log) aktiv" : "");
	return 0;

fehler:
	aufraeumen();
	return ret;
}

static void __exit lauscher_exit(void)
{
	pr_info(TAG "%u Nachrichten, %u Log-Ereignisse, %zu Bytes Log, %u Rahmen, Kanal %d wieder frei\n",
		anzahl, ereignisse, wlog_len, rahmen_anzahl, kanal);
	aufraeumen();
}

module_init(lauscher_init);
module_exit(lauscher_exit);

MODULE_DESCRIPTION("Mailbox-Lauscher: sipc-Nachrichten des UMS9230-WCN-Kerns anzeigen - Redmi A5");
MODULE_AUTHOR("EberhartLeberhart");
MODULE_LICENSE("GPL");
