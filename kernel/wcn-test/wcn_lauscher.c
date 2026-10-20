// SPDX-License-Identifier: GPL-2.0
/*
 * wcn_lauscher - hoert auf einem Mailbox-Kanal des UMS9230 mit und zeigt jede Nachricht als
 * sipc-"smsg" an. Erster Schritt zum sipc-Port: Was sagt der WCN-Kern nach dem Start?
 *
 * Redmi A5 (serenity), Mainline 7.1. Ohne Parameter nur Zuhoeren; mit antworten=1 und den
 * weiteren Parametern (siehe unten und README.md) beantwortet es die sipc-Kanaele 4, 5, 7, 11, 12.
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
 * Andere Kanaele (4 = AT/BT/FM, 7 = WLAN) bleiben unbeantwortet - ausser mit alle_oeffnen=1:
 * dann wird jedes OPEN des WCN-Kerns quittiert (nur OPEN, kein sbuf/sblock).
 * kanal11=1: Kanal 11 (Lebenszeichen) bekommt wie Kanal 5 einen sbuf (1 Ring, 0x400/0x400)
 * bei SMEM + 0x90000; empfangene Bytes stehen im dmesg ("Kanal 11:").
 *   Beleg 19.10.: Firmware-Log "sbuf_create: channel b, sbuf_res->smem_addr is 2d0000".
 * kanal12=1: dasselbe fuer Kanal 12 (Absturzmeldungen) bei SMEM + 0x91000 ("Kanal 12:").
 *   Beleg 19.10.: mit 4, 5, 11, 12 beantwortet kommt die erste AT-Antwort (at+spatgetcp2info).
 * kanal7=1: Kanal 7 (WLAN-Befehle) als sblock bei SMEM + 0xa0000 (WCN 0x2e0000), tx 4 x 2048,
 * rx 8 x 2048. Nur Empfang: angekommene Bloecke als Hex-Dump ins dmesg ("K7 #n"), dann zurueck
 * in den Pool (EVENT RELEASE).
 *   Beleg 20.10.: Firmware-Log "sblock_create ... raddr:2e0060, rcount:4, rsize:800 ..." passt genau.
 * Senden (nur mit kanal7=1): echo sync_version > /sys/devices/platform/wcn-lauscher/wlan_befehl
 *   schickt WIFI_CMD_SYNC_VERSION (erster Befehl des Realme-WLAN-Treibers, aendert nichts am Funk).
 *   Beleg 20.10.: Antwort Befehl 9, Status 0, API-Tabelle der Firmware.
 * echo get_info > .../wlan_befehl schickt WIFI_CMD_GET_INFO (liest nur: Chip, Firmware, Faehigkeiten, MAC);
 *   die Antwort wird entschluesselt ausgegeben, die MAC nur zur Haelfte.
 *
 * AT-Kanal (Parameter at=1, setzt antworten=1 voraus), Stand 19.10.:
 * Kanal 4 ist bei Realme (sprdwcn/sipc/wcn_sipc.c) ein sbuf mit 16 Ringen, je tx 0x2400 /
 * rx 0x2400; Ring 5 = AT-Befehle, Ring 11/10 = Bluetooth tx/rx, Ring 14/13 = FM tx/rx.
 * Ablauf wie Kanal 5: WCN OPEN -> wir OPEN; WCN CMD SBUF_INIT -> wir DONE SBUF_INIT mit der
 * Adresse des sbuf-Kopfs (liegt im gemeinsamen Speicher ab SMEM + 0x40000, hinter Kanal 5).
 * Senden (Realme sbuf_write): Text in den tx-Ring 5, txbuf_wrptr erhoehen; war der Ring leer,
 * EVENT WRPTR mit Wert = Ringnummer. Antworten kommen als EVENT WRPTR (Wert = Ringnummer)
 * und werden aus dem rx-Ring gelesen.
 *   echo "at+spatgetcp2info" > /sys/devices/platform/wcn-lauscher/at_befehl   (\r\n wird angehaengt)
 *   cat /sys/devices/platform/wcn-lauscher/at_antworten
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

static bool at;
module_param(at, bool, 0444);
MODULE_PARM_DESC(at, "zusaetzlich Kanal 4 (AT/BT/FM, 16 Ringe) oeffnen; braucht antworten=1");

static bool kanal11;
module_param(kanal11, bool, 0444);
MODULE_PARM_DESC(kanal11, "Kanal 11 (Lebenszeichen) als sbuf bereitstellen (1 Ring, 0x400/0x400); braucht antworten=1");

static bool kanal12;
module_param(kanal12, bool, 0444);
MODULE_PARM_DESC(kanal12, "Kanal 12 (Absturzmeldungen) als sbuf bereitstellen (1 Ring, 0x400/0x400); braucht antworten=1");

static bool kanal7;
module_param(kanal7, bool, 0444);
MODULE_PARM_DESC(kanal7, "Kanal 7 (WLAN-Befehle) als sblock bereitstellen und nur empfangen (tx 4x2048, rx 8x2048); braucht antworten=1");

static bool alle_oeffnen;
module_param(alle_oeffnen, bool, 0444);
MODULE_PARM_DESC(alle_oeffnen, "jedes OPEN des WCN-Kerns (auch Kanal 7, 11, 12 ...) mit OPEN beantworten, sonst nichts; braucht antworten=1");

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

/* Kanal 4: sbuf mit 16 Ringen (Realme wcn_sipc.c: SIPC_ATCMD_TX/RX, bufid 5, 0x2400/0x2400) */
#define AT_KANAL	4
#define AT_OFF		0x40000		/* Abstand vom Anfang des gemeinsamen Speichers (Kanal 5 endet bei 0x38024) */
#define AT_RINGE	16
#define AT_TX		0x2400
#define AT_RX		0x2400
#define AT_HDR		(4 + AT_RINGE * 32)	/* ringnr + 16 x sbuf_ring_header, wie Realme hsize */
#define AT_RING		5		/* AT-Befehle */
#define AT_ANTW_MAX	(64 * 1024)

/*
 * Kleine sbufs mit 1 Ring, 0x400/0x400 (Realme wcn_sipc.c):
 *   Kanal 11 = Lebenszeichen (SIPC_LOOPCHECK_RX), bei SMEM + 0x90000
 *   Kanal 12 = Absturzmeldungen (SIPC_ASSERT_RX), bei SMEM + 0x91000
 * Beide liegen hinter Kanal 4 (endet bei 0x40000 + 0x48204 = 0x88204).
 */
#define LC_TX		0x400
#define LC_RX		0x400
#define LC_HDR		36

/*
 * Kanal 7: WLAN-Befehle, sblock (Realme wcn_sipc.c SIPC_WIFI_CMD: tx 4 x 2048, rx 8 x 2048),
 * Aufbau wie Realme sblock_host_init(): sblock_header (Ring + Pool, je 12 x u32), dann
 * tx-Bloecke, rx-Bloecke, Ring-Listen (tx, rx), Pool-Listen (tx, rx); Listeneintrag = {addr, length}.
 * Liegt bei SMEM + 0xa0000 (hinter Kanal 12 bei 0x91000 + 0x824).
 */
#define SB7_KANAL	7
#define SB7_OFF		0xa0000
#define SB7_TXN		4
#define SB7_RXN		8
#define SB7_BLK		2048
#define SB7_HDR		96
#define SB7_TXBLK	(SB7_OFF + SB7_HDR)
#define SB7_RXBLK	(SB7_TXBLK + SB7_TXN * SB7_BLK)
#define SB7_R_TXL	(SB7_RXBLK + SB7_RXN * SB7_BLK)	/* Ring: Liste tx */
#define SB7_R_RXL	(SB7_R_TXL + SB7_TXN * 8)		/* Ring: Liste rx */
#define SB7_P_TXL	(SB7_R_RXL + SB7_RXN * 8)		/* Pool: Liste tx */
#define SB7_P_RXL	(SB7_P_TXL + SB7_TXN * 8)		/* Pool: Liste rx */
#define SB7_ENDE	(SB7_P_RXL + SB7_RXN * 8)
/* Felder von sblock_ring_header (Ring bei +0, Pool bei +48) */
#define SH_TX_ADDR	0
#define SH_TX_CNT	4
#define SH_TX_SIZE	8
#define SH_TX_BLKS	12
#define SH_TX_RD	16
#define SH_TX_WR	20
#define SH_RX_ADDR	24
#define SH_RX_CNT	28
#define SH_RX_SIZE	32
#define SH_RX_BLKS	36
#define SH_RX_RD	40
#define SH_RX_WR	44
#define SH_RING		0
#define SH_POOL		48
#define EV_SBLOCK_SEND		0x0001
#define EV_SBLOCK_RELEASE	0x0002

/*
 * Erster WLAN-Befehl (Realme wlan/marlin3_sipc): WIFI_CMD_SYNC_VERSION = 9.
 * sprdwl_cmd_hdr (gepackt, 12 Bytes): common (Bit 0-2 Typ 0 = CMD, Bit 4 rsp = 1 Antwort
 * erwuenscht, Bit 5-7 ctx_id 0), cmd_id, le16 plen (inkl. Kopf), le32 mstime, s8 status,
 * u8 rsp_cnt, u8 reserv[2]. Danach sprdwl_cmd_api_t: u32 main_ver (1) + u8 api_map[256]
 * (Werte aus Realme api_version.c g_api_array, Index = Position in der Tabelle).
 */
#define WIFI_CMD_GET_INFO	1
#define WIFI_CMD_SYNC_VERSION	9
#define WL_HDR			12
/*
 * WIFI_CMD_GET_INFO: Nutzdaten = ein TLV {le16 Typ 2 NOTIFY_AP_VERSION, le16 Laenge 1, u8 0 = User-Build}.
 * Antwort struct sprdwl_cmd_fw_info (Realme cmdevt.h, NICHT gepackt; Versatz ab Nutzdaten):
 *   0 chip_model, 4 chip_version, 8 fw_version, 12 fw_std, 16 fw_capa (Bit 0 = 5 GHz),
 *   20 max_ap_assoc_sta, 21 max_acl_mac_addrs, 22 max_mc_mac_addrs, 23 wnm_ft_support,
 *   24 wiphy_sec2_t (52 Bytes: ht_cap_info, ampdu_para, ht_mcs 16, vht_cap 4, vht_mcs 8, ant tx/rx, ...),
 *   76 mac_addr[6], 82 credit_capa, 84 extend_feature (Bit 0 SAE, 1 OWE, 2 DPP, ...)
 */
#define FWI_MAC		76
#define FWI_CREDIT	82
#define FWI_EXTEND	84
static const u8 wl_api_map[256] = {
	1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0,
};

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

static DEFINE_MUTEX(sende_lock);	/* Arbeits-Thread und at_befehl senden beide */

static void senden(u8 ch, u8 typ, u16 flag, u32 wert, const char *was)
{
	u32 m[2] = { ch | typ << 8 | (u32)flag << 16, wert };
	int ret;

	merken(m[0], m[1], true);
	mutex_lock(&sende_lock);
	ret = mbox_send_message(chan, m);	/* blockierend, tx_tout */
	mutex_unlock(&sende_lock);
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

/* ---- Kanal 4 (AT/BT/FM) ---- */
static bool at_bereit;
static char *at_antw;		/* gesammelte Antworten aller Ringe, als Text */
static size_t at_antw_len;
static DEFINE_MUTEX(at_lock);	/* Ringe von Kanal 4 */

static u32 at_kopf(u32 ring_nr, u32 feld)	/* Offset im gemeinsamen Speicher */
{
	return AT_OFF + 4 + ring_nr * 32 + feld - 4;	/* feld wie H_TX_ADDR.. (ab 4) */
}

static u32 at_tx_off(u32 i) { return AT_OFF + AT_HDR + (AT_TX + AT_RX) * i; }
static u32 at_rx_off(u32 i) { return at_tx_off(i) + AT_TX; }

/* sbuf_host_init() fuer 16 Ringe */
static void at_vorbereiten(void)
{
	u32 i;

	memset(smem + AT_OFF, 0, AT_HDR + (AT_TX + AT_RX) * AT_RINGE);
	sm_wr(AT_OFF, AT_RINGE);
	for (i = 0; i < AT_RINGE; i++) {
		sm_wr(at_kopf(i, H_TX_ADDR), SMEM_CP + at_tx_off(i));
		sm_wr(at_kopf(i, H_TX_SIZE), AT_TX);
		sm_wr(at_kopf(i, H_RX_ADDR), SMEM_CP + at_rx_off(i));
		sm_wr(at_kopf(i, H_RX_SIZE), AT_RX);
	}
	wmb();
	pr_info(TAG "sbuf fuer Kanal 4 bei 0x%x (WCN-Sicht 0x%x): %u Ringe, Ring %u tx @0x%x rx @0x%x\n",
		SMEM_AP + AT_OFF, SMEM_CP + AT_OFF, AT_RINGE, AT_RING,
		sm_rd(at_kopf(AT_RING, H_TX_ADDR)), sm_rd(at_kopf(AT_RING, H_RX_ADDR)));
}

static void at_merken(u32 ring_nr, const u8 *p, u32 n)
{
	char kopf[32];
	int k;

	if (!at_antw)
		return;
	k = scnprintf(kopf, sizeof(kopf), "[Ring %u, %u Bytes] ", ring_nr, n);
	if (at_antw_len + k + n + 1 >= AT_ANTW_MAX)
		return;
	memcpy(at_antw + at_antw_len, kopf, k);
	at_antw_len += k;
	memcpy(at_antw + at_antw_len, p, n);
	at_antw_len += n;
	at_antw[at_antw_len++] = '\n';
}

static void senden(u8 ch, u8 typ, u16 flag, u32 wert, const char *was);

/* sbuf_read() fuer einen rx-Ring von Kanal 4 */
static void at_ring_lesen(u32 i)
{
	u32 rd, wr, pos, n;
	bool war_voll;
	u8 *tmp;

	rmb();
	rd = sm_rd(at_kopf(i, H_RX_RD));
	wr = sm_rd(at_kopf(i, H_RX_WR));
	if (rd == wr)
		return;
	if (wr - rd > AT_RX) {
		pr_warn(TAG "Kanal 4 Ring %u: Zeiger unplausibel (rd %u, wr %u)\n", i, rd, wr);
		rd = wr - AT_RX;
	}
	war_voll = (wr - rd) >= AT_RX;
	tmp = kmalloc(AT_RX, GFP_KERNEL);
	if (!tmp)
		return;
	n = 0;
	while (rd != wr) {
		u32 k;

		pos = rd % AT_RX;
		k = min(wr - rd, AT_RX - pos);
		memcpy(tmp + n, smem + at_rx_off(i) + pos, k);
		n += k;
		rd += k;
	}
	sm_wr(at_kopf(i, H_RX_RD), rd);
	wmb();
	pr_info(TAG "Kanal 4 Ring %u: %u Bytes empfangen: %*pE\n", i, n, (int)min(n, 200u), tmp);
	at_merken(i, tmp, n);
	kfree(tmp);
	if (war_voll)
		senden(AT_KANAL, T_EVENT, EV_RDPTR, i, "EVENT RDPTR Kanal 4 (Ring war voll)");
}

static void at_alles_lesen(void)
{
	u32 i;

	if (!at_bereit)
		return;
	mutex_lock(&at_lock);
	for (i = 0; i < AT_RINGE; i++)
		at_ring_lesen(i);
	mutex_unlock(&at_lock);
}

/* sbuf_write() in den tx-Ring AT_RING */
static int at_schreiben(const u8 *p, u32 n)
{
	u32 rd, wr, pos, frei, k;
	bool war_leer;

	if (!at_bereit)
		return -ENOTCONN;
	mutex_lock(&at_lock);
	rmb();
	rd = sm_rd(at_kopf(AT_RING, H_TX_RD));
	wr = sm_rd(at_kopf(AT_RING, H_TX_WR));
	frei = AT_TX - (wr - rd);
	if (wr - rd > AT_TX || n > frei) {
		mutex_unlock(&at_lock);
		pr_warn(TAG "AT: kein Platz (rd %u, wr %u, %u Bytes)\n", rd, wr, n);
		return -EBUSY;
	}
	war_leer = (wr == rd);
	while (n) {
		pos = wr % AT_TX;
		k = min(n, AT_TX - pos);
		memcpy(smem + at_tx_off(AT_RING) + pos, p, k);
		p += k;
		n -= k;
		wr += k;
	}
	wmb();
	sm_wr(at_kopf(AT_RING, H_TX_WR), wr);
	wmb();
	mutex_unlock(&at_lock);
	if (war_leer)
		senden(AT_KANAL, T_EVENT, EV_WRPTR, AT_RING, "EVENT WRPTR Kanal 4 Ring 5 (AT-Befehl)");
	return 0;
}

/* ---- Kleine sbufs: Kanal 11 (Lebenszeichen) und 12 (Absturzmeldungen) ---- */
struct klein {
	u8 kanal;
	u32 off;
	const char *name;
	bool bereit;
	unsigned int bytes;
	struct mutex lock;
};

static struct klein kleine[] = {
	{ .kanal = 11, .off = 0x90000, .name = "Lebenszeichen" },
	{ .kanal = 12, .off = 0x91000, .name = "Absturzmeldungen" },
};

static bool klein_aktiv(const struct klein *k)
{
	return (k->kanal == 11 && kanal11) || (k->kanal == 12 && kanal12);
}

static struct klein *klein_fuer(u8 ch)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(kleine); i++)
		if (kleine[i].kanal == ch && klein_aktiv(&kleine[i]))
			return &kleine[i];
	return NULL;
}

static void klein_vorbereiten(struct klein *k)
{
	u32 o = k->off;

	memset(smem + o, 0, LC_HDR + LC_TX + LC_RX);
	sm_wr(o + H_RINGNR, 1);
	sm_wr(o + H_TX_ADDR, SMEM_CP + o + LC_HDR);
	sm_wr(o + H_TX_SIZE, LC_TX);
	sm_wr(o + H_RX_ADDR, SMEM_CP + o + LC_HDR + LC_TX);
	sm_wr(o + H_RX_SIZE, LC_RX);
	wmb();
	pr_info(TAG "sbuf fuer Kanal %u bei 0x%x (WCN-Sicht 0x%x): tx 0x%x @0x%x, rx 0x%x @0x%x\n",
		k->kanal, SMEM_AP + o, SMEM_CP + o, LC_TX, sm_rd(o + H_TX_ADDR),
		LC_RX, sm_rd(o + H_RX_ADDR));
}

/* sbuf_read() fuer den rx-Ring eines kleinen Kanals, Inhalt nur ins dmesg */
static void klein_lesen(struct klein *k)
{
	u8 tmp[LC_RX];
	u32 rd, wr, n = 0, l, pos, o = k->off;
	bool war_voll;

	if (!k->bereit)
		return;
	mutex_lock(&k->lock);
	rmb();
	rd = sm_rd(o + H_RX_RD);
	wr = sm_rd(o + H_RX_WR);
	if (rd == wr) {
		mutex_unlock(&k->lock);
		return;
	}
	if (wr - rd > LC_RX) {
		pr_warn(TAG "Kanal %u: Zeiger unplausibel (rd %u, wr %u)\n", k->kanal, rd, wr);
		rd = wr - LC_RX;
	}
	war_voll = (wr - rd) >= LC_RX;
	while (rd != wr) {
		pos = rd % LC_RX;
		l = min(wr - rd, LC_RX - pos);
		memcpy(tmp + n, smem + o + LC_HDR + LC_TX + pos, l);
		n += l;
		rd += l;
	}
	sm_wr(o + H_RX_RD, rd);
	wmb();
	k->bytes += n;
	mutex_unlock(&k->lock);
	pr_info(TAG "Kanal %u: %u Bytes empfangen: %*pE\n", k->kanal, n, (int)min(n, 200u), tmp);
	pr_info(TAG "Kanal %u roh: %*ph\n", k->kanal, (int)min(n, 64u), tmp);
	if (war_voll)
		senden(k->kanal, T_EVENT, EV_RDPTR, 0, "EVENT RDPTR (Ring war voll)");
}

static void klein_alle_lesen(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(kleine); i++)
		if (klein_aktiv(&kleine[i]))
			klein_lesen(&kleine[i]);
}

static void klein_bearbeiten(struct klein *k, u8 typ, u16 flag)
{
	char was[48];

	switch (typ) {
	case T_OPEN:	/* auch mit alle_oeffnen=1: kleine Kanaele werden hier quittiert */
		if (flag == SMSG_OPEN_MAGIC) {
			k->bereit = false;
			snprintf(was, sizeof(was), "OPEN Kanal %u", k->kanal);
			senden(k->kanal, T_OPEN, SMSG_OPEN_MAGIC, 0, was);
		}
		break;
	case T_CLOSE:
		k->bereit = false;
		snprintf(was, sizeof(was), "CLOSE-Antwort Kanal %u", k->kanal);
		senden(k->kanal, T_CLOSE, SMSG_CLOSE_MAGIC, 0, was);
		break;
	case T_CMD:
		if (flag == CMD_SBUF_INIT && !k->bereit) {
			mutex_lock(&k->lock);
			klein_vorbereiten(k);
			mutex_unlock(&k->lock);
			k->bereit = true;
			snprintf(was, sizeof(was), "DONE SBUF_INIT Kanal %u (WCN 0x%x)",
				 k->kanal, SMEM_CP + k->off);
			senden(k->kanal, T_DONE, DONE_SBUF_INIT, SMEM_CP + k->off, was);
			klein_lesen(k);
		}
		break;
	case T_EVENT:
		if (flag == EV_WRPTR)
			klein_lesen(k);
		break;
	}
}

/* ---- Kanal 7 (WLAN-Befehle, sblock) ---- */
static bool sb7_bereit;
static unsigned int sb7_bloecke;
static DEFINE_MUTEX(sb7_lock);

static u32 cp7(u32 off)	/* Offset im gemeinsamen Speicher -> Adresse aus WCN-Sicht */
{
	return SMEM_CP + off;
}

static void sb7_vorbereiten(void)
{
	u32 r = SB7_OFF + SH_RING, p = SB7_OFF + SH_POOL, i;

	memset(smem + SB7_OFF, 0, SB7_ENDE - SB7_OFF);
	sm_wr(r + SH_TX_ADDR, cp7(SB7_TXBLK));
	sm_wr(r + SH_TX_CNT, SB7_TXN);
	sm_wr(r + SH_TX_SIZE, SB7_BLK);
	sm_wr(r + SH_TX_BLKS, cp7(SB7_R_TXL));
	sm_wr(r + SH_RX_ADDR, cp7(SB7_RXBLK));
	sm_wr(r + SH_RX_CNT, SB7_RXN);
	sm_wr(r + SH_RX_SIZE, SB7_BLK);
	sm_wr(r + SH_RX_BLKS, cp7(SB7_R_RXL));

	sm_wr(p + SH_TX_ADDR, cp7(SB7_TXBLK));
	sm_wr(p + SH_TX_CNT, SB7_TXN);
	sm_wr(p + SH_TX_SIZE, SB7_BLK);
	sm_wr(p + SH_TX_BLKS, cp7(SB7_P_TXL));
	sm_wr(p + SH_RX_ADDR, cp7(SB7_RXBLK));
	sm_wr(p + SH_RX_CNT, SB7_RXN);
	sm_wr(p + SH_RX_SIZE, SB7_BLK);
	sm_wr(p + SH_RX_BLKS, cp7(SB7_P_RXL));

	/* alle Bloecke liegen anfangs frei im Pool */
	for (i = 0; i < SB7_TXN; i++) {
		sm_wr(SB7_P_TXL + i * 8, cp7(SB7_TXBLK + i * SB7_BLK));
		sm_wr(SB7_P_TXL + i * 8 + 4, SB7_BLK);
	}
	sm_wr(p + SH_TX_WR, SB7_TXN);
	for (i = 0; i < SB7_RXN; i++) {
		sm_wr(SB7_P_RXL + i * 8, cp7(SB7_RXBLK + i * SB7_BLK));
		sm_wr(SB7_P_RXL + i * 8 + 4, SB7_BLK);
	}
	sm_wr(p + SH_RX_WR, SB7_RXN);
	wmb();
	pr_info(TAG "sblock fuer Kanal 7 bei 0x%x (WCN-Sicht 0x%x): tx %ux%u @0x%x, rx %ux%u @0x%x, Ende 0x%x\n",
		SMEM_AP + SB7_OFF, cp7(SB7_OFF), SB7_TXN, SB7_BLK, cp7(SB7_TXBLK),
		SB7_RXN, SB7_BLK, cp7(SB7_RXBLK), cp7(SB7_ENDE));
}

static void wl_info_zeigen(const u8 *h, u32 len);

/* sblock_receive() + sblock_release() fuer alle angekommenen Bloecke, Inhalt ins dmesg */
static void sb7_empfangen(void)
{
	u32 r = SB7_OFF + SH_RING, p = SB7_OFF + SH_POOL;
	u32 rd, wr, pos, addr, len, off, pwr;
	bool melden = false;
	char kopf[48];

	if (!sb7_bereit)
		return;
	mutex_lock(&sb7_lock);
	for (;;) {
		rmb();
		rd = sm_rd(r + SH_RX_RD);
		wr = sm_rd(r + SH_RX_WR);
		if (rd == wr)
			break;
		if (wr - rd > SB7_RXN) {
			pr_warn(TAG "Kanal 7: Ring-Zeiger unplausibel (rd %u, wr %u) - gebe auf\n", rd, wr);
			break;
		}
		pos = rd % SB7_RXN;
		addr = sm_rd(SB7_R_RXL + pos * 8);
		len = sm_rd(SB7_R_RXL + pos * 8 + 4);
		off = addr - SMEM_CP;
		sb7_bloecke++;
		if (off < SB7_RXBLK || off + SB7_BLK > SB7_RXBLK + SB7_RXN * SB7_BLK ||
		    (off - SB7_RXBLK) % SB7_BLK || len > SB7_BLK) {
			pr_warn(TAG "Kanal 7: Block %u unplausibel (Adresse 0x%x, Laenge %u)\n",
				sb7_bloecke, addr, len);
		} else {
			pr_info(TAG "Kanal 7: Block %u empfangen, %u Bytes (WCN 0x%x)\n",
				sb7_bloecke, len, addr);
			snprintf(kopf, sizeof(kopf), "wcnlausch: K7 #%u ", sb7_bloecke);
			if (len >= WL_HDR) {
				const u8 *h = smem + off;

				pr_info(TAG "Kanal 7: Kopf: Typ %u, rsp %u, ctx %u, Befehl %u, plen %u, Zeit %u, Status %d, rsp_cnt %u\n",
					h[0] & 7, (h[0] >> 4) & 1, h[0] >> 5, h[1],
					get_unaligned_le16(h + 2), get_unaligned_le32(h + 4),
					(s8)h[8], h[9]);
			}
			if (len >= WL_HDR && ((const u8 *)smem + off)[1] == WIFI_CMD_GET_INFO) {
				/* kein Roh-Dump: enthaelt die volle MAC-Adresse */
				wl_info_zeigen(smem + off, len);
			} else {
				print_hex_dump(KERN_INFO, kopf, DUMP_PREFIX_OFFSET, 16, 1,
					       smem + off, min(len, 320u), true);
			}
		}
		sm_wr(r + SH_RX_RD, rd + 1);
		/* Block zurueck in den Pool (sblock_release) */
		pwr = sm_rd(p + SH_RX_WR);
		sm_wr(SB7_P_RXL + (pwr % SB7_RXN) * 8, addr);
		sm_wr(SB7_P_RXL + (pwr % SB7_RXN) * 8 + 4, SB7_BLK);
		wmb();
		sm_wr(p + SH_RX_WR, pwr + 1);
		wmb();
		if (pwr + 1 - sm_rd(p + SH_RX_RD) == 1)
			melden = true;
	}
	mutex_unlock(&sb7_lock);
	if (melden)
		senden(SB7_KANAL, T_EVENT, EV_SBLOCK_RELEASE, 0, "EVENT RELEASE Kanal 7 (Block zurueck)");
}

/* sblock_get() + sblock_send(): einen Block aus dem Sende-Pool fuellen und abschicken */
static int sb7_senden(const u8 *daten, u32 len)
{
	u32 r = SB7_OFF + SH_RING, p = SB7_OFF + SH_POOL;
	u32 prd, pwr, addr, off, rwr;
	bool melden;

	if (!sb7_bereit)
		return -ENOTCONN;
	if (len > SB7_BLK)
		return -EMSGSIZE;
	mutex_lock(&sb7_lock);
	rmb();
	prd = sm_rd(p + SH_TX_RD);
	pwr = sm_rd(p + SH_TX_WR);
	if (prd == pwr) {
		mutex_unlock(&sb7_lock);
		pr_warn(TAG "Kanal 7: kein freier Sendeblock (Pool rd %u wr %u)\n", prd, pwr);
		return -EBUSY;
	}
	addr = sm_rd(SB7_P_TXL + (prd % SB7_TXN) * 8);
	off = addr - SMEM_CP;
	if (off < SB7_TXBLK || off + SB7_BLK > SB7_RXBLK || (off - SB7_TXBLK) % SB7_BLK) {
		mutex_unlock(&sb7_lock);
		pr_warn(TAG "Kanal 7: Sendeblock unplausibel (Adresse 0x%x)\n", addr);
		return -EIO;
	}
	sm_wr(p + SH_TX_RD, prd + 1);
	memcpy(smem + off, daten, len);
	rwr = sm_rd(r + SH_TX_WR);
	sm_wr(SB7_R_TXL + (rwr % SB7_TXN) * 8, addr);
	sm_wr(SB7_R_TXL + (rwr % SB7_TXN) * 8 + 4, len);
	wmb();
	sm_wr(r + SH_TX_WR, rwr + 1);
	wmb();
	melden = (rwr + 1 - sm_rd(r + SH_TX_RD)) == 1;
	mutex_unlock(&sb7_lock);
	pr_info(TAG "Kanal 7: %u Bytes gesendet (Block WCN 0x%x)\n", len, addr);
	print_hex_dump(KERN_INFO, "wcnlausch: K7 raus ", DUMP_PREFIX_OFFSET, 16, 1,
		       daten, min(len, 32u), false);
	if (melden)
		senden(SB7_KANAL, T_EVENT, EV_SBLOCK_SEND, 0, "EVENT SEND Kanal 7 (WLAN-Befehl)");
	return 0;
}

static int wl_sync_version(void)
{
	u8 buf[WL_HDR + 4 + 256];
	u32 ms = (u32)div_u64(ktime_get_ns(), NSEC_PER_MSEC);

	memset(buf, 0, sizeof(buf));
	buf[0] = 0x10;			/* Typ CMD, Antwort erwuenscht, ctx 0 */
	buf[1] = WIFI_CMD_SYNC_VERSION;
	put_unaligned_le16(sizeof(buf), buf + 2);
	put_unaligned_le32(ms, buf + 4);
	put_unaligned_le32(1, buf + WL_HDR);		/* main_ver = MAIN_DRV_VERSION */
	memcpy(buf + WL_HDR + 4, wl_api_map, sizeof(wl_api_map));
	pr_info(TAG "WLAN: sende WIFI_CMD_SYNC_VERSION (%zu Bytes)\n", sizeof(buf));
	return sb7_senden(buf, sizeof(buf));
}

static int wl_get_info(void)
{
	u8 buf[WL_HDR + 5];
	u32 ms = (u32)div_u64(ktime_get_ns(), NSEC_PER_MSEC);

	memset(buf, 0, sizeof(buf));
	buf[0] = 0x10;			/* Typ CMD, Antwort erwuenscht, ctx 0 */
	buf[1] = WIFI_CMD_GET_INFO;
	put_unaligned_le16(sizeof(buf), buf + 2);
	put_unaligned_le32(ms, buf + 4);
	put_unaligned_le16(2, buf + WL_HDR);		/* NOTIFY_AP_VERSION */
	put_unaligned_le16(1, buf + WL_HDR + 2);
	buf[WL_HDR + 4] = 0;				/* NOTIFY_AP_VERSION_USER */
	pr_info(TAG "WLAN: sende WIFI_CMD_GET_INFO (%zu Bytes)\n", sizeof(buf));
	return sb7_senden(buf, sizeof(buf));
}

/* Antwort auf GET_INFO lesbar ausgeben (MAC nur halb, damit Logs teilbar bleiben) */
static void wl_info_zeigen(const u8 *h, u32 len)
{
	const u8 *d = h + WL_HDR;
	u32 n = len - WL_HDR, capa;

	if (n < 24) {
		pr_info(TAG "GET_INFO: nur %u Bytes Nutzdaten\n", n);
		return;
	}
	capa = get_unaligned_le32(d + 16);
	pr_info(TAG "GET_INFO: Chip 0x%08x, Chipversion 0x%08x, Firmware 0x%08x\n",
		get_unaligned_le32(d), get_unaligned_le32(d + 4), get_unaligned_le32(d + 8));
	pr_info(TAG "GET_INFO: Standards 0x%08x, Faehigkeiten 0x%08x (5 GHz: %s)\n",
		get_unaligned_le32(d + 12), capa, (capa & 1) ? "ja" : "nein");
	pr_info(TAG "GET_INFO: max. AP-Clients %u, ACL %u, Multicast %u, WNM/FT %u\n",
		d[20], d[21], d[22], d[23]);
	if (n >= 76)
		pr_info(TAG "GET_INFO: HT-Cap 0x%04x, VHT-Cap 0x%08x, Antennen tx %u rx %u, RTS %u, Frag %u\n",
			get_unaligned_le16(d + 24), get_unaligned_le32(d + 24 + 20),
			get_unaligned_le32(d + 24 + 32), get_unaligned_le32(d + 24 + 36),
			get_unaligned_le32(d + 24 + 48), get_unaligned_le32(d + 24 + 44));
	if (n >= FWI_MAC + 6)
		pr_info(TAG "GET_INFO: MAC %02x:%02x:%02x:xx:xx:xx (%s)\n", d[FWI_MAC], d[FWI_MAC + 1],
			d[FWI_MAC + 2], memchr_inv(d + FWI_MAC, 0, 6) ? "gesetzt" : "leer");
	if (n >= FWI_CREDIT + 1)
		pr_info(TAG "GET_INFO: Credit-Modus %u (0 = mit Credit, 1 = ohne)\n", d[FWI_CREDIT]);
	if (n >= FWI_EXTEND + 4)
		pr_info(TAG "GET_INFO: erweiterte Funktionen 0x%08x (SAE %u, OWE %u)\n",
			get_unaligned_le32(d + FWI_EXTEND), get_unaligned_le32(d + FWI_EXTEND) & 1,
			(get_unaligned_le32(d + FWI_EXTEND) >> 1) & 1);
	pr_info(TAG "GET_INFO: %u Bytes Nutzdaten (Realme erwartet ab 88)\n", n);
}

static void sb7_bearbeiten(u8 typ, u16 flag, u32 wert)
{
	switch (typ) {
	case T_OPEN:	/* auch mit alle_oeffnen=1: Kanal 7 wird hier quittiert */
		if (flag == SMSG_OPEN_MAGIC) {
			sb7_bereit = false;
			senden(SB7_KANAL, T_OPEN, SMSG_OPEN_MAGIC, 0, "OPEN Kanal 7");
		}
		break;
	case T_CLOSE:
		sb7_bereit = false;
		senden(SB7_KANAL, T_CLOSE, SMSG_CLOSE_MAGIC, 0, "CLOSE-Antwort Kanal 7");
		break;
	case T_CMD:
		if (flag == CMD_SBUF_INIT && !sb7_bereit) {	/* SMSG_CMD_SBLOCK_INIT = 1 */
			mutex_lock(&sb7_lock);
			sb7_vorbereiten();
			mutex_unlock(&sb7_lock);
			sb7_bereit = true;
			senden(SB7_KANAL, T_DONE, DONE_SBUF_INIT, cp7(SB7_OFF),
			       "DONE SBLOCK_INIT Kanal 7 (WCN 0x2e0000)");
			sb7_empfangen();
		}
		break;
	case T_EVENT:
		if (flag == EV_SBLOCK_SEND)
			sb7_empfangen();
		else if (flag == EV_SBLOCK_RELEASE)
			pr_info(TAG "Kanal 7: EVENT RELEASE (WCN gibt Sendeblock zurueck), Wert 0x%x\n", wert);
		break;
	}
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
	at_alles_lesen();
	klein_alle_lesen();
	if (kanal7)
		sb7_empfangen();
	schedule_delayed_work(&nachsehen, msecs_to_jiffies(500));
}

static void bearbeiten(u32 lo, u32 hi)
{
	u8 ch = lo & 0xff, typ = (lo >> 8) & 0xff;
	u16 flag = lo >> 16;

	/*
	 * Test 19.10. (alle_oeffnen=1): Der WCN-Kern oeffnet auch Kanal 7 (WLAN-Befehle) und 11
	 * (Lebenszeichen) und wartet vermutlich auf unser OPEN (Realme smsg_ch_open() wartet auf
	 * die Gegenseite). Nur das OPEN quittieren - keine Puffer, kein DONE. Was danach kommt,
	 * steht in "nachrichten".
	 */
	if (alle_oeffnen && typ == T_OPEN && flag == SMSG_OPEN_MAGIC &&
	    ch != LOG_KANAL && !(ch == AT_KANAL && at) && !klein_fuer(ch) &&
	    !(ch == SB7_KANAL && kanal7)) {
		char was[32];

		snprintf(was, sizeof(was), "OPEN Kanal %u (nur Quittung)", ch);
		senden(ch, T_OPEN, SMSG_OPEN_MAGIC, 0, was);
		return;
	}

	if (ch == SB7_KANAL && kanal7) {
		sb7_bearbeiten(typ, flag, hi);
		return;
	}

	if (klein_fuer(ch)) {
		klein_bearbeiten(klein_fuer(ch), typ, flag);
		return;
	}

	if (ch == AT_KANAL && at) {
		switch (typ) {
		case T_OPEN:
			if (flag == SMSG_OPEN_MAGIC) {
				at_bereit = false;	/* Neustart des Kerns: Ringe neu aufsetzen */
				mutex_lock(&at_lock);
				at_vorbereiten();
				mutex_unlock(&at_lock);
				senden(AT_KANAL, T_OPEN, SMSG_OPEN_MAGIC, 0, "OPEN Kanal 4");
			}
			break;
		case T_CLOSE:
			at_bereit = false;
			senden(AT_KANAL, T_CLOSE, SMSG_CLOSE_MAGIC, 0, "CLOSE-Antwort Kanal 4");
			break;
		case T_CMD:
			if (flag == CMD_SBUF_INIT && !at_bereit) {
				at_bereit = true;
				senden(AT_KANAL, T_DONE, DONE_SBUF_INIT, SMEM_CP + AT_OFF,
				       "DONE SBUF_INIT Kanal 4 (sbuf bei WCN 0x280000)");
			}
			break;
		case T_EVENT:
			if (flag == EV_WRPTR)
				at_alles_lesen();
			/* EV_RDPTR: WCN hat gelesen - nichts zu tun */
			break;
		}
		return;
	}
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

static ssize_t at_befehl_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	char zeile[256];
	size_t n = min(count, sizeof(zeile) - 3);
	int ret;

	memcpy(zeile, buf, n);
	while (n && (zeile[n - 1] == '\n' || zeile[n - 1] == '\r'))
		n--;
	if (!n)
		return -EINVAL;
	zeile[n++] = '\r';
	zeile[n++] = '\n';
	pr_info(TAG "AT senden: %*pE\n", (int)n, zeile);
	ret = at_schreiben(zeile, n);
	return ret ? ret : count;
}
static DEVICE_ATTR_WO(at_befehl);

static ssize_t at_antworten_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	size_t n;

	if (!at_antw)
		return sysfs_emit(buf, "(at=0)\n");
	mutex_lock(&at_lock);
	n = min_t(size_t, at_antw_len, PAGE_SIZE - 1);
	memcpy(buf, at_antw + (at_antw_len - n), n);	/* das Neueste, falls zu lang */
	mutex_unlock(&at_lock);
	return n;
}
static DEVICE_ATTR_RO(at_antworten);

/* Testbefehle fuer Kanal 7: "sync_version", "get_info" */
static ssize_t wlan_befehl_store(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t count)
{
	int ret;

	if (sysfs_streq(buf, "sync_version"))
		ret = wl_sync_version();
	else if (sysfs_streq(buf, "get_info"))
		ret = wl_get_info();
	else
		ret = -EINVAL;
	return ret ? ret : count;
}
static DEVICE_ATTR_WO(wlan_befehl);
static bool wlan_datei;
static bool at_dateien;

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
	unsigned int i;

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
	if (pdev && at_dateien) {
		device_remove_file(&pdev->dev, &dev_attr_at_befehl);
		device_remove_file(&pdev->dev, &dev_attr_at_antworten);
	}
	at_dateien = false;
	at_bereit = false;
	if (pdev && wlan_datei)
		device_remove_file(&pdev->dev, &dev_attr_wlan_befehl);
	wlan_datei = false;
	for (i = 0; i < ARRAY_SIZE(kleine); i++)
		kleine[i].bereit = false;
	sb7_bereit = false;
	kfree(at_antw);
	at_antw = NULL;
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
	unsigned int i;
	u32 ph;
	int ret;

	INIT_WORK(&arbeit, arbeit_fn);
	INIT_DELAYED_WORK(&nachsehen, nachsehen_fn);
	for (i = 0; i < ARRAY_SIZE(kleine); i++)
		mutex_init(&kleine[i].lock);
	if (kanal < 0 || kanal > 15) {
		pr_err(TAG "kanal muss 0..15 sein\n");
		return -EINVAL;
	}
	if (at && !antworten) {
		pr_err(TAG "at=1 braucht antworten=1\n");
		return -EINVAL;
	}
	if ((kanal7 || kanal11 || kanal12) && !antworten) {
		pr_err(TAG "kanal7=1 / kanal11=1 / kanal12=1 brauchen antworten=1\n");
		return -EINVAL;
	}
	if (alle_oeffnen && !antworten) {
		pr_err(TAG "alle_oeffnen=1 braucht antworten=1\n");
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
		if (at) {
			at_antw = kzalloc(AT_ANTW_MAX, GFP_KERNEL);
			if (!at_antw) {
				ret = -ENOMEM;
				goto fehler;
			}
			at_vorbereiten();
		}
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
	if (at) {
		if (!device_create_file(&pdev->dev, &dev_attr_at_befehl) &&
		    !device_create_file(&pdev->dev, &dev_attr_at_antworten))
			at_dateien = true;
		else
			pr_warn(TAG "Dateien at_befehl/at_antworten fehlen\n");
	}

	if (kanal7) {
		if (!device_create_file(&pdev->dev, &dev_attr_wlan_befehl))
			wlan_datei = true;
		else
			pr_warn(TAG "Datei wlan_befehl fehlt\n");
	}

	if (wlog && !sysfs_create_bin_file(&pdev->dev.kobj, &bin_attr_wcn_log))
		bin_log = true;
	if (ram_btwf.va && !sysfs_create_bin_file(&pdev->dev.kobj, &bin_attr_wcn_ram_btwf))
		bin_btwf = true;
	if (ram_hoch.va && !sysfs_create_bin_file(&pdev->dev.kobj, &bin_attr_wcn_ram_hoch))
		bin_hoch = true;
	pr_info(TAG "WCN-Speicher lesbar: wcn_ram_btwf %s, wcn_ram_hoch %s\n",
		bin_btwf ? "ja" : "NEIN", bin_hoch ? "ja" : "NEIN");
	pr_info(TAG "hoere auf Mailbox-Kanal %d%s%s - jetzt wcn_starttest stufe=3 laden\n", kanal,
		antworten ? (at ? ", sipc-lite fuer Kanal 5 (Log) und 4 (AT) aktiv" :
			     ", sipc-lite fuer Kanal 5 (Log) aktiv") : "",
		alle_oeffnen ? ", jedes OPEN wird quittiert" : "");
	for (i = 0; i < ARRAY_SIZE(kleine); i++)
		if (klein_aktiv(&kleine[i]))
			pr_info(TAG "Kanal %u (%s) wird als sbuf bereitgestellt\n",
				kleine[i].kanal, kleine[i].name);
	if (kanal7)
		pr_info(TAG "Kanal 7 (WLAN-Befehle) wird als sblock bereitgestellt (nur Empfang)\n");
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
