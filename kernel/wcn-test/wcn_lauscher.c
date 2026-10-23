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
 *   Beleg 20.10. (ohne INI): Status 0, Chip 0x2355, Firmware 0xf13b, 5 GHz, WPA3, MAC leer.
 * echo download_ini > .../wlan_befehl liest /lib/firmware/wifi_board_config.ini (Geraetedatei aus
 *   odm:/firmware, NICHT veroeffentlichen), wandelt sie wie Realme rf_marlin3.c (wcn_ini.h) und
 *   schickt WIFI_CMD_DOWNLOAD_INI in 3-4 Abschnitten mit CRC16.
 *   Beleg 20.10.: Abschnitte 1, 2, 4 Status 0; 3 (rf_config, TLV) Status 1 mit result 0 (angewendet).
 * echo open > .../wlan_befehl schickt WIFI_CMD_OPEN (Modus Station) mit der MAC aus
 *   /lib/firmware/wifimac.txt (Kopie von prodnv:/wifimac.txt, NICHT veroeffentlichen), sonst einer
 *   zufaelligen lokal verwalteten MAC; die Antwort enthaelt die ctx_id. "close" schaltet wieder ab.
 *   Beleg 21.10.: Firmware faehrt den WLAN-Teil hoch (PLL an, eFuse-Kalibrierung, "create sta ctxt"),
 *   oeffnet dann Kanal 8 und wartet - deshalb datenkanaele=1 (Kanal 8/9).
 * Das Firmware-Log enthaelt die volle MAC; im gesammelten Log (wcn_log, dmesg) wird sie halb ausgeblendet.
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
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/hex.h>
#include <linux/ctype.h>
#include <linux/completion.h>
#include <linux/netdevice.h>
#if IS_ENABLED(CONFIG_CFG80211)
#include <net/cfg80211.h>
#define WL_CFG80211 1
#else
#define WL_CFG80211 0
#endif

#include "wcn_ini.h"

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

static bool wlan0;
module_param(wlan0, bool, 0444);
MODULE_PARM_DESC(wlan0, "echtes WLAN-Geraet wlan0 ueber cfg80211 anmelden (ip link set wlan0 up = Firmware vorbereiten + OPEN, iw scan); braucht kanal7=1 und datenkanaele=1");

static bool kanal7;
module_param(kanal7, bool, 0444);
MODULE_PARM_DESC(kanal7, "Kanal 7 (WLAN-Befehle) als sblock bereitstellen und nur empfangen (tx 4x2048, rx 8x2048); braucht antworten=1");

static bool datenkanaele;
module_param(datenkanaele, bool, 0444);
MODULE_PARM_DESC(datenkanaele, "Kanal 8 und 9 (WLAN-Daten) als sblock bereitstellen, nur Empfang; braucht antworten=1");

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
#define WIFI_CMD_DOWNLOAD_INI	76
#define WIFI_CMD_OPEN		3	/* struct sprdwl_cmd_open { u8 mode; u8 reserved; u8 mac[6]; } __packed */
#define WIFI_CMD_CLOSE		4	/* struct sprdwl_cmd_close { u8 mode; } __packed */
#define WIFI_CMD_SET_REGDOM	2	/* struct sprdwl_ieee80211_regdomain { u32 n_reg_rules; char alpha2[2]; (2 Fuellbytes)
					 * struct sprdwl_reg_rule { freq_range {start,end,max_bw kHz}, power_rule
					 * {max_antenna_gain mBi, max_eirp mBm}, u32 flags, u32 dfs_cac_ms } [] } */
#define WIFI_CMD_SCAN		11	/* struct sprdwl_cmd_scan { le32 channels; le32 reserved; le16 ssid_len; ssid[] }
					 * danach le16 Anzahl 5-GHz-Kanaele + le16 Kanalnummern (Realme sprdwl_scan) */
#define WIFI_CMD_CONNECT	10	/* struct sprdwl_cmd_connect (82 Bytes, gepackt): le32 wpa_versions, bssid[6],
					 * u8 channel, auth_type, pairwise_cipher, group_cipher, key_mgmt, mfp_enable,
					 * psk_len, ssid_len, psk[32], ssid[32]; Cipher/AKM mit Bit 0x80 = gueltig */
#define WIFI_CMD_DISCONNECT	13	/* le16 reason_code */
#define WIFI_CMD_SET_IE		25	/* u8 type (3 = ASSOC_REQ), le16 len, data[] */
#define WIFI_EVENT_CONNECT	0x80	/* u8 status (0 ok, 1 Fehler; bei Fehler Statuscode des Routers in Byte 2),
					 * bssid[6], u8 channel, s8 signal, le16 req_ie_len, ... */
#define WIFI_EVENT_DISCONNECT	0x81	/* le16 reason_code */
#define WIFI_EVENT_SCAN_DONE	0x82	/* u8 type: 1 fertig, 2 sched, 3 Fehler, 4 gscan, 5 abgebrochen */
#define WIFI_EVENT_MGMT_FRAME	0x83	/* u8 type (4 = Scan), u8 channel, s8 signal, u8 res, bssid[6], le16 len, Rahmen */
#define WL_FRAME_SCAN		4
#define WL_SCAN_MAX		64
#define WL_MODE_STATION		1	/* enum sprdwl_mode: NONE 0, STATION 1, AP 2, ... */
#define WL_MAC_DATEI		"wifimac.txt"	/* aus prodnv:/wifimac.txt, auf dem Handy in /lib/firmware */
#define WL_INI_DATEI		"wifi_board_config.ini"	/* aus odm:/firmware, auf dem Handy in /lib/firmware */
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
#define DMESG_ZEILEN	3000	/* Firmware-Text ist jetzt immer lesbar, das braucht mehr Zeilen */

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

/*
 * EAPOL im Datenblock suchen (Ethertype 88 8e, Version 1-3, Typ 3 = EAPOL-Key). Nur ein Hinweis fuer den
 * Test: das genaue Format der Empfangsbloecke (Deskriptor vor dem Rahmen) kennen wir noch nicht.
 */
static void sbd_eapol_suchen(u8 kanal, const u8 *b, u32 len)
{
	u32 i;

	for (i = 0; i + 8 <= len; i++) {
		if (b[i] == 0x88 && b[i + 1] == 0x8e && b[i + 2] >= 1 && b[i + 2] <= 3 && b[i + 3] == 3) {
			u16 info = i + 9 <= len ? get_unaligned_be16(b + i + 7) : 0;

			pr_info(TAG "Kanal %u: EAPOL-Key bei Versatz %u (Key-Info 0x%04x%s)\n", kanal, i, info,
				(info & 0x01c8) == 0x0088 ? " = Nachricht 1 vom Router" : "");	/* paarweise, ACK, ohne MIC/Install */
			return;
		}
	}
}

/* ---- Kanal 7 (WLAN-Befehle, sblock) ---- */
static bool sb7_bereit;
static int wl_ctx = -1;		/* ctx_id aus der OPEN-Antwort */
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

/* Namen der Befehle (Realme cmdevt.h) und Fehlercodes (msg.h), fuer eine lesbare Ergebniszeile */
static const char *wl_befehl_name(u8 id)
{
	switch (id) {
	case 0: return "ERR";
	case 1: return "GET_INFO";
	case 2: return "SET_REGDOM";
	case 3: return "OPEN";
	case 4: return "CLOSE";
	case 5: return "POWER_SAVE";
	case 6: return "SET_PARAM";
	case 9: return "SYNC_VERSION";
	case 10: return "CONNECT";
	case 11: return "SCAN";
	case 13: return "DISCONNECT";
	case 25: return "SET_IE";
	case 76: return "DOWNLOAD_INI";
	default: return "?";
	}
}

static const char *wl_status_text(u8 id, s8 st)
{
	switch (st) {
	case 0: return "ok";
	case -1: return "FEHLER: Argument";
	case -2: return "FEHLER: Ergebnis";
	case -3: return "FEHLER: Ausfuehrung";
	case -4: return "FEHLER: kein Speicher";
	case -5: return "FEHLER: falscher WLAN-Modus";
	case -6: return "FEHLER";
	case -7: return "FEHLER: nicht ausfuehrbar";
	case -8: return "FEHLER: nicht unterstuetzt";
	case -9: return "FEHLER: Pruefsumme";
	case -10: return "FEHLER: INI-Abschnitt";
	}
	if (id == 76 && st == 1)
		return "ok (TLV-Abschnitt angewendet)";
	return st > 0 ? "unbekannter Hinweis" : "unbekannter Fehler";
}

static const char *wl_ereignis_name(u8 id)
{
	switch (id) {
	case 0x80: return "CONNECT";
	case 0x81: return "DISCONNECT";
	case 0x82: return "SCAN_DONE";
	case 0x83: return "MGMT_FRAME";
	case 0x84: return "MGMT_TX_STATUS";
	case 0x89: return "RSSI_MONITOR";
	case 0x90: return "COEX_BT_ON_OFF";
	case 0xb0: return "CQM";
	case 0xf6: return "HANG_RECOVERY";
	case 0xf7: return "THERMAL_WARN";
	case 0xfa: return "FW_PWR_DOWN";
	default: return "?";
	}
}

/* Haken fuer wlan0 (cfg80211), weiter unten */
static void wl0_antwort(u8 id, s8 st);
static void wl0_bss(u8 kanal, s8 signal, const u8 *f, u16 flen);
static void wl0_scan_ende(bool abgebrochen);
static void wl0_verbunden_ereignis(const u8 *d, u32 n);
static void wl0_getrennt_ereignis(u16 grund);
static void wl0_rx(u8 kanal, const u8 *b, u32 len);

/* ---- Scan-Ergebnisse (WIFI_EVENT_MGMT_FRAME Typ 4 = Beacon/Probe-Response) ---- */
static u8 scan_bssid[WL_SCAN_MAX][ETH_ALEN];
static unsigned int scan_anzahl, scan_rahmen;

/* Ziel fuer "connect": SSID ueber die Datei wlan_ziel, Rest aus dem Scan (volle BSSID nur im Speicher) */
static char ziel_ssid[33];
static u8 ziel_ssid_len;
static bool ziel_da;
static u8 ziel_bssid[ETH_ALEN], ziel_kanal, ziel_gruppe;	/* Gruppen-Cipher als RSN-Suite-Typ (2 TKIP, 4 CCMP) */
static s8 ziel_signal;

/* RSN-Element des Routers pruefen: Gruppen-Cipher merken, CCMP paarweise und PSK muessen dabei sein */
static bool ziel_rsn_passt(const u8 *rsn, u8 rsn_len, u8 *gruppe)
{
	unsigned int i, n, pos;
	bool ccmp = false, psk = false;

	if (!rsn || rsn_len < 8 || rsn[2] || rsn[3] != 0x0f || rsn[4] != 0xac)
		return false;
	*gruppe = rsn[5];
	n = get_unaligned_le16(rsn + 6);
	for (i = 0, pos = 8; i < n && pos + 4 <= rsn_len; i++, pos += 4)
		if (!rsn[pos] && rsn[pos + 1] == 0x0f && rsn[pos + 2] == 0xac && rsn[pos + 3] == 4)
			ccmp = true;
	if (pos + 2 > rsn_len)
		return false;
	n = get_unaligned_le16(rsn + pos);
	for (i = 0, pos += 2; i < n && pos + 4 <= rsn_len; i++, pos += 4)
		if (!rsn[pos] && rsn[pos + 1] == 0x0f && rsn[pos + 2] == 0xac && rsn[pos + 3] == 2)
			psk = true;
	return ccmp && psk && (*gruppe == 2 || *gruppe == 4);
}

/* Sicherheit grob aus Capability (Privacy-Bit) und RSN-Element (AKM 8 = SAE) */
static const char *scan_sicherheit(u16 capab, const u8 *rsn, u8 rsn_len)
{
	unsigned int i, n, akm_pos;
	bool psk = false, sae = false;

	if (!(capab & 0x10))
		return "offen";
	if (!rsn)
		return "WEP/WPA1";
	/* RSN: ver 2, Gruppe 4, Anzahl paarweise 2 + 4*n, Anzahl AKM 2 + 4*m */
	if (rsn_len < 8)
		return "WPA2?";
	n = get_unaligned_le16(rsn + 6);
	akm_pos = 8 + 4 * n;
	if (akm_pos + 2 > rsn_len)
		return "WPA2?";
	n = get_unaligned_le16(rsn + akm_pos);
	for (i = 0; i < n && akm_pos + 2 + 4 * i + 4 <= rsn_len; i++) {
		const u8 *a = rsn + akm_pos + 2 + 4 * i;

		if (a[0] == 0x00 && a[1] == 0x0f && a[2] == 0xac) {
			if (a[3] == 2 || a[3] == 6)
				psk = true;
			if (a[3] == 8 || a[3] == 24)
				sae = true;
		}
	}
	if (sae && psk)
		return "WPA2/WPA3";
	if (sae)
		return "WPA3";
	return psk ? "WPA2" : "WPA2-Enterprise?";
}

static void scan_rahmen_zeigen(const u8 *d, u32 n)
{
	const u8 *f, *ie, *ssid = NULL, *rsn = NULL;
	u8 kanal, ssid_len = 0, rsn_len = 0, ds_kanal = 0;
	s8 signal;
	u16 flen, capab;
	u32 pos, i;
	bool versteckt = true;

	if (n < 12 || d[0] != WL_FRAME_SCAN)
		return;
	kanal = d[1];
	signal = (s8)d[2];
	flen = get_unaligned_le16(d + 10);
	f = d + 12;
	if (flen > n - 12 || flen < 36)	/* 24 Kopf + 8 Zeitstempel + 2 Intervall + 2 Capability */
		return;
	scan_rahmen++;
	wl0_bss(kanal, signal, f, flen);	/* jeder Rahmen an cfg80211 (aktualisiert Signal/IEs) */
	/* BSSID = addr3 im 802.11-Kopf; doppelte (2,4 und mehrfach empfangen) nur einmal zeigen */
	for (i = 0; i < scan_anzahl; i++)
		if (ether_addr_equal(scan_bssid[i], f + 16))
			return;
	if (scan_anzahl < WL_SCAN_MAX)
		ether_addr_copy(scan_bssid[scan_anzahl], f + 16);
	scan_anzahl++;
	capab = get_unaligned_le16(f + 34);
	for (pos = 36; pos + 2 <= flen; pos += 2 + ie[1]) {
		ie = f + pos;
		if (pos + 2 + ie[1] > flen)
			break;
		if (ie[0] == 0 && !ssid) {
			ssid = ie + 2;
			ssid_len = min_t(u8, ie[1], 32);
		} else if (ie[0] == 3 && ie[1] >= 1) {
			ds_kanal = ie[2];
		} else if (ie[0] == 48 && !rsn) {
			rsn = ie + 2;
			rsn_len = ie[1];
		}
	}
	for (i = 0; i < ssid_len; i++)
		if (ssid[i])
			versteckt = false;
	if (ziel_ssid_len && ssid_len == ziel_ssid_len && !memcmp(ssid, ziel_ssid, ssid_len)) {
		u8 gruppe = 0;

		if (ziel_rsn_passt(rsn, rsn_len, &gruppe) && (!ziel_da || signal > ziel_signal)) {
			ether_addr_copy(ziel_bssid, f + 16);
			ziel_kanal = ds_kanal ? ds_kanal : kanal;
			ziel_gruppe = gruppe;
			ziel_signal = signal;
			ziel_da = true;
			pr_info(TAG "ZIEL: \"%*pE\" gefunden, Kanal %u, %d dBm, WPA2-PSK/CCMP, Gruppe %s\n",
				ssid_len, ssid, ziel_kanal, signal, gruppe == 4 ? "CCMP" : "TKIP");
		} else if (!ziel_rsn_passt(rsn, rsn_len, &gruppe)) {
			pr_info(TAG "ZIEL: \"%*pE\" gefunden, aber nicht WPA2-PSK mit CCMP - connect geht dafuer nicht\n",
				ssid_len, ssid);
		}
	}
	if (versteckt)
		pr_info(TAG "SCAN: Kanal %3u, %4d dBm, %-16s BSSID %02x:%02x:%02x:xx:xx:xx, (versteckt)\n",
			ds_kanal ? ds_kanal : kanal, signal, scan_sicherheit(capab, rsn, rsn_len),
			f[16], f[17], f[18]);
	else
		pr_info(TAG "SCAN: Kanal %3u, %4d dBm, %-16s BSSID %02x:%02x:%02x:xx:xx:xx, \"%*pE\"\n",
			ds_kanal ? ds_kanal : kanal, signal, scan_sicherheit(capab, rsn, rsn_len),
			f[16], f[17], f[18], ssid_len, ssid);
}

/* eine Zeile pro Antwort bzw. Ereignis der Firmware */
static void wl_ergebnis(const u8 *h, u32 len)
{
	u8 typ = h[0] & 7, id = h[1];
	s8 st = (s8)h[8];
	u16 plen = get_unaligned_le16(h + 2);
	u32 n = min_t(u32, plen, len);

	if (typ == 0) {
		pr_info(TAG "WLAN-Antwort: %s -> %s\n", wl_befehl_name(id), wl_status_text(id, st));
		wl0_antwort(id, st);
	} else if (typ == 1 && id == WIFI_EVENT_MGMT_FRAME) {
		if (n > WL_HDR)
			scan_rahmen_zeigen(h + WL_HDR, n - WL_HDR);
	} else if (typ == 1 && id == WIFI_EVENT_CONNECT) {
		const u8 *d = h + WL_HDR;

		if (n < WL_HDR + 1)
			pr_info(TAG "WLAN-Ereignis: CONNECT ohne Inhalt\n");
		else if (d[0] == 0 && n >= WL_HDR + 9)
			pr_info(TAG "WLAN-Ereignis: CONNECT -> VERBUNDEN mit %02x:%02x:%02x:xx:xx:xx, Kanal %u, %d dBm\n",
				d[1], d[2], d[3], d[7], (s8)d[8]);
		else
			pr_info(TAG "WLAN-Ereignis: CONNECT -> fehlgeschlagen (Ergebnis %u, Statuscode des Routers %u)\n",
				d[0], n >= WL_HDR + 3 ? d[2] : 0);
		if (n > WL_HDR)
			wl0_verbunden_ereignis(d, n - WL_HDR);
	} else if (typ == 1 && id == WIFI_EVENT_DISCONNECT) {
		u16 grund = n >= WL_HDR + 2 ? get_unaligned_le16(h + WL_HDR) : 0;

		pr_info(TAG "WLAN-Ereignis: DISCONNECT -> Grund %u%s\n", grund,
			grund == 15 ? " (4-Wege-Handschlag abgelaufen - ohne Passwort erwartet)" :
			grund == 2 ? " (Anmeldung ungueltig)" :
			grund == 3 ? " (abgemeldet)" :
			grund == 6 || grund == 7 ? " (Rahmen ohne gueltige Anmeldung)" : "");
		wl0_getrennt_ereignis(grund);
	} else if (typ == 1 && id == WIFI_EVENT_SCAN_DONE) {
		u8 t = n > WL_HDR ? h[WL_HDR] : 0;

		pr_info(TAG "WLAN-Ereignis: SCAN_DONE -> %s, %u Netze (%u Rahmen)\n",
			t == 1 ? "fertig" : t == 3 ? "FEHLER" : t == 5 ? "abgebrochen" : "?",
			scan_anzahl, scan_rahmen);
		wl0_scan_ende(t != 1);
	} else if (typ == 1) {
		pr_info(TAG "WLAN-Ereignis: %s (0x%02x), %u Bytes\n", wl_ereignis_name(id), id, plen);
	} else {
		pr_info(TAG "WLAN: Nachricht Typ %u, Nr %u\n", typ, id);
	}
}

/* einen empfangenen Block von Kanal 7 auswerten (Kopie, der Block ist schon zurueck im Pool) */
static void sb7_block_zeigen(const u8 *h, u32 len, u32 nr, u32 addr)
{
	char kopf[48];

	if (len >= WL_HDR && (h[0] & 7) == 1 && h[1] == WIFI_EVENT_MGMT_FRAME) {
		/* Scan-Rahmen: nur die Auswertung, der Rohinhalt enthaelt volle BSSIDs */
		wl_ergebnis(h, len);
		return;
	}
	snprintf(kopf, sizeof(kopf), "wcnlausch: K7 #%u ", nr);
	pr_info(TAG "Kanal 7: Block %u empfangen, %u Bytes (WCN 0x%x)\n",
		nr, len, addr);
	if (len >= WL_HDR) {
		pr_info(TAG "Kanal 7: Kopf: Typ %u, rsp %u, ctx %u, Befehl %u, plen %u, Zeit %u, Status %d, rsp_cnt %u\n",
			h[0] & 7, (h[0] >> 4) & 1, h[0] >> 5, h[1],
			get_unaligned_le16(h + 2), get_unaligned_le32(h + 4),
			(s8)h[8], h[9]);
		wl_ergebnis(h, len);
	}
	if (len >= WL_HDR && h[1] == WIFI_CMD_OPEN) {
		u16 plen = get_unaligned_le16(h + 2);

		/*
		 * Realme sprdwl_open_fw(): ctx_id = erstes Nutzdatenbyte, falls vorhanden,
		 * sonst bleibt der Vorgabewert 0. Beleg 21.10.: plen 12 (nur Kopf), Firmware-Log
		 * "ctxt_id:0" - das Byte hinter dem Kopf ist dann nur ein Speicherrest.
		 */
		wl_ctx = (plen > WL_HDR && len > WL_HDR) ? h[WL_HDR] : h[0] >> 5;
		pr_info(TAG "OPEN: ctx_id %d (%s)\n", wl_ctx,
			plen > WL_HDR ? "aus den Nutzdaten" : "keine Nutzdaten, Kopf");
	}
	if (len >= WL_HDR && h[1] == WIFI_CMD_GET_INFO) {
		/* kein Roh-Dump: enthaelt die volle MAC-Adresse */
		wl_info_zeigen(h, len);
	} else {
		print_hex_dump(KERN_INFO, kopf, DUMP_PREFIX_OFFSET, 16, 1,
			       h, min(len, 320u), true);
	}
}

/*
 * sblock_receive() + sblock_release(): erst alle angekommenen Bloecke kopieren und sofort
 * zurueckgeben, dann in Ruhe auswerten. Vorher (bis 21.10.) wurde jeder Block erst nach der
 * dmesg-Ausgabe frei; beim Scan liefen die 8 Empfangsbloecke voll ("report event fail").
 */
static u8 sb7_kopie[SB7_RXN][SB7_BLK];
static u32 sb7_kopie_len[SB7_RXN], sb7_kopie_nr[SB7_RXN], sb7_kopie_addr[SB7_RXN];
static DEFINE_MUTEX(sb7_zeig_lock);	/* schuetzt sb7_kopie; immer vor sb7_lock nehmen */

static void sb7_empfangen(void)
{
	u32 r = SB7_OFF + SH_RING, p = SB7_OFF + SH_POOL;
	u32 rd, wr, pos, addr, len, off, pwr, n, i;
	bool melden;

	if (!sb7_bereit)
		return;
	mutex_lock(&sb7_zeig_lock);
	do {
		n = 0;
		melden = false;
		mutex_lock(&sb7_lock);
		while (n < SB7_RXN) {
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
				memcpy(sb7_kopie[n], smem + off, len);
				sb7_kopie_len[n] = len;
				sb7_kopie_nr[n] = sb7_bloecke;
				sb7_kopie_addr[n] = addr;
				n++;
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
		for (i = 0; i < n; i++)
			sb7_block_zeigen(sb7_kopie[i], sb7_kopie_len[i], sb7_kopie_nr[i], sb7_kopie_addr[i]);
	} while (n == SB7_RXN);	/* es koennten waehrenddessen weitere gekommen sein */
	mutex_unlock(&sb7_zeig_lock);
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

/*
 * sprdwl_down_ini_cmd(): Nutzdaten = u8 Abschnitt, 3 Bytes frei, Daten, u16 CRC16 der Daten.
 * Realme wartet nach jedem Abschnitt auf die Antwort; wir schicken mit kurzer Pause und lesen
 * die Antworten (Befehl 76, Status) im dmesg.
 */
static int wl_ini_abschnitt(u8 nr, const void *daten, u32 len)
{
	u32 plen = WL_HDR + 4 + len + 2;
	u32 ms = (u32)div_u64(ktime_get_ns(), NSEC_PER_MSEC);
	u8 *buf;
	u16 crc;
	int ret;

	if (plen > SB7_BLK) {
		pr_warn(TAG "INI: Abschnitt %u zu gross (%u Bytes, Block %u)\n", nr, plen, SB7_BLK);
		return -EMSGSIZE;
	}
	buf = kzalloc(plen, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	buf[0] = 0x10;			/* Typ CMD, Antwort erwuenscht, ctx 0 */
	buf[1] = WIFI_CMD_DOWNLOAD_INI;
	put_unaligned_le16(plen, buf + 2);
	put_unaligned_le32(ms, buf + 4);
	buf[WL_HDR] = nr;
	memcpy(buf + WL_HDR + 4, daten, len);
	crc = ini_crc16(daten, len);
	put_unaligned_le16(crc, buf + WL_HDR + 4 + len);
	pr_info(TAG "INI: sende Abschnitt %u, %u Bytes Daten, CRC 0x%04x\n", nr, len, crc);
	ret = sb7_senden(buf, plen);
	kfree(buf);
	return ret;
}

/* sprdwl_download_ini(): Datei einlesen, in wifi_conf_t uebertragen, 3-4 Abschnitte senden */
static int wl_download_ini(void)
{
	const struct firmware *fw;
	struct wifi_conf_t *conf;
	int treffer, unbekannt, ret;

	ret = request_firmware(&fw, WL_INI_DATEI, &pdev->dev);
	if (ret) {
		pr_warn(TAG "INI: %s nicht gefunden (%d) - nach /lib/firmware kopieren\n", WL_INI_DATEI, ret);
		return ret;
	}
	conf = kzalloc(sizeof(*conf), GFP_KERNEL);
	if (!conf) {
		release_firmware(fw);
		return -ENOMEM;
	}
	treffer = ini_parse(fw->data, fw->size, conf, &unbekannt);
	pr_info(TAG "INI: %s, %zu Bytes, %d Schluessel uebernommen, %d unbekannt\n",
		WL_INI_DATEI, fw->size, treffer, unbekannt);
	release_firmware(fw);
	if (treffer <= 0) {
		kfree(conf);
		return treffer < 0 ? treffer : -EINVAL;
	}
	pr_info(TAG "INI: Version %u.%u, Ketten 2G 0x%x 5G 0x%x, Kalibrierung aus %u, Regdomain 0x%x/0x%x, rf_data %d Bytes\n",
		conf->version.major, conf->version.minor, conf->board_config.g2_chain_mask,
		conf->board_config.g5_chain_mask, conf->board_config.calibration_bypass,
		conf->reg_domain.reg_domain1, conf->reg_domain.reg_domain2, conf->rf_config.rf_data_len);
	pr_info(TAG "INI: Groessen sec1 %zu, sec2 %zu, sec4 %zu, gesamt %zu\n",
		sizeof(struct wifi_conf_sec1_t), sizeof(struct wifi_conf_sec2_t),
		sizeof(struct wifi_config_param_t), sizeof(struct wifi_conf_t));

	ret = wl_ini_abschnitt(1, conf, sizeof(struct wifi_conf_sec1_t));
	if (!ret) {
		msleep(300);
		ret = wl_ini_abschnitt(2, &conf->tx_scale, sizeof(struct wifi_conf_sec2_t));
	}
	if (!ret && conf->rf_config.rf_data_len > 0) {
		msleep(300);
		ret = wl_ini_abschnitt(3, conf->rf_config.rf_data,
				       min_t(u32, conf->rf_config.rf_data_len, sizeof(conf->rf_config.rf_data)));
	}
	if (!ret) {
		msleep(300);
		ret = wl_ini_abschnitt(4, &conf->wifi_param, sizeof(struct wifi_config_param_t));
	}
	kfree(conf);
	return ret;
}

/* MAC wie Android aus wifimac.txt ("aa:bb:cc:dd:ee:ff"), sonst zufaellige lokal verwaltete MAC */
static void wl_mac_holen(u8 *mac)
{
	const struct firmware *fw;
	char text[32];
	size_t n;

	if (!request_firmware(&fw, WL_MAC_DATEI, &pdev->dev)) {
		n = min_t(size_t, fw->size, sizeof(text) - 1);
		memcpy(text, fw->data, n);
		text[n] = 0;
		release_firmware(fw);
		if (mac_pton(strim(text), mac) && is_valid_ether_addr(mac)) {
			pr_info(TAG "WLAN: MAC aus %s: %02x:%02x:%02x:xx:xx:xx\n", WL_MAC_DATEI,
				mac[0], mac[1], mac[2]);
			return;
		}
		pr_warn(TAG "WLAN: %s unlesbar - nehme zufaellige MAC\n", WL_MAC_DATEI);
	} else {
		pr_warn(TAG "WLAN: %s nicht in /lib/firmware - nehme zufaellige MAC\n", WL_MAC_DATEI);
	}
	eth_random_addr(mac);	/* lokal verwaltet (Bit 1 gesetzt), kollidiert mit keiner Hersteller-MAC */
	pr_info(TAG "WLAN: zufaellige MAC %02x:%02x:%02x:xx:xx:xx\n", mac[0], mac[1], mac[2]);
}

/* sprdwl_open_fw(): WLAN-Teil im Client-Modus einschalten */
static int wl_open(void)
{
	u8 buf[WL_HDR + 8];
	u32 ms = (u32)div_u64(ktime_get_ns(), NSEC_PER_MSEC);

	memset(buf, 0, sizeof(buf));
	buf[0] = 0x10;			/* Typ CMD, Antwort erwuenscht, ctx 0 */
	buf[1] = WIFI_CMD_OPEN;
	put_unaligned_le16(sizeof(buf), buf + 2);
	put_unaligned_le32(ms, buf + 4);
	buf[WL_HDR] = WL_MODE_STATION;
	buf[WL_HDR + 1] = 0;
	wl_mac_holen(buf + WL_HDR + 2);
	pr_info(TAG "WLAN: sende WIFI_CMD_OPEN (Modus Station)\n");
	return sb7_senden(buf, sizeof(buf));
}

/* sprdwl_close_fw(): wieder ausschalten (ctx_id aus der OPEN-Antwort im Kopf) */
static int wl_close(void)
{
	u8 buf[WL_HDR + 1];
	u32 ms = (u32)div_u64(ktime_get_ns(), NSEC_PER_MSEC);

	if (wl_ctx < 0) {
		pr_warn(TAG "WLAN: CLOSE ohne vorheriges OPEN\n");
		return -EINVAL;
	}
	memset(buf, 0, sizeof(buf));
	buf[0] = 0x10 | ((wl_ctx & 7) << 5);
	buf[1] = WIFI_CMD_CLOSE;
	put_unaligned_le16(sizeof(buf), buf + 2);
	put_unaligned_le32(ms, buf + 4);
	buf[WL_HDR] = WL_MODE_STATION;
	pr_info(TAG "WLAN: sende WIFI_CMD_CLOSE (ctx %d)\n", wl_ctx);
	return sb7_senden(buf, sizeof(buf));
}

/*
 * sprdwl_reg_notify() -> sprdwl_set_regdom(): Laenderregeln an die Firmware, wie Android sie beim
 * Anlegen von wlan0 schickt. Werte = Eintrag DE aus der wireless-regdb (gesetzliche Grenzen, keine
 * Erhoehung): 2,4 GHz 20 dBm; 5150-5250 23 dBm nur innen; 5250-5350 20 dBm nur innen, DFS;
 * 5470-5725 27 dBm, DFS. Die Firmware begrenzt die Sendeleistung zusaetzlich selbst.
 */
#define RRF_NO_OUTDOOR	(1 << 3)
#define RRF_DFS		(1 << 4)
#define RRF_AUTO_BW	(1 << 11)
static const struct {
	u32 start, ende, bw, gewinn, eirp, flags, cac;
} wl_regeln_de[] = {
	{ 2400000, 2483500,  40000, 0, 2000, 0, 0 },
	{ 5150000, 5250000,  80000, 0, 2300, RRF_NO_OUTDOOR | RRF_AUTO_BW, 0 },
	{ 5250000, 5350000,  80000, 0, 2000, RRF_NO_OUTDOOR | RRF_DFS | RRF_AUTO_BW, 60000 },
	{ 5470000, 5725000, 160000, 0, 2700, RRF_DFS, 60000 },
};

static int wl_regdom(void)
{
	u8 buf[WL_HDR + 8 + ARRAY_SIZE(wl_regeln_de) * 28];
	u32 ms = (u32)div_u64(ktime_get_ns(), NSEC_PER_MSEC), i;
	u8 *r;

	memset(buf, 0, sizeof(buf));
	buf[0] = 0x10;			/* CMD, Antwort erwuenscht, ctx 0 (SPRDWL_MODE_NONE) */
	buf[1] = WIFI_CMD_SET_REGDOM;
	put_unaligned_le16(sizeof(buf), buf + 2);
	put_unaligned_le32(ms, buf + 4);
	put_unaligned_le32(ARRAY_SIZE(wl_regeln_de), buf + WL_HDR);
	buf[WL_HDR + 4] = 'D';
	buf[WL_HDR + 5] = 'E';
	for (i = 0; i < ARRAY_SIZE(wl_regeln_de); i++) {
		r = buf + WL_HDR + 8 + i * 28;
		put_unaligned_le32(wl_regeln_de[i].start, r);
		put_unaligned_le32(wl_regeln_de[i].ende, r + 4);
		put_unaligned_le32(wl_regeln_de[i].bw, r + 8);
		put_unaligned_le32(wl_regeln_de[i].gewinn, r + 12);
		put_unaligned_le32(wl_regeln_de[i].eirp, r + 16);
		put_unaligned_le32(wl_regeln_de[i].flags, r + 20);
		put_unaligned_le32(wl_regeln_de[i].cac, r + 24);
	}
	pr_info(TAG "WLAN: sende WIFI_CMD_SET_REGDOM (DE, %zu Regeln, %zu Bytes)\n",
		ARRAY_SIZE(wl_regeln_de), sizeof(buf));
	return sb7_senden(buf, sizeof(buf));
}

/*
 * sprdwl_scan(): Suchlauf ohne SSID (wie cfg80211 mit Platzhalter-SSID). 2,4 GHz Kanaele 1-13 als Bitmaske;
 * mit fuenf=true zusaetzlich nur die 5-GHz-Kanaele 36-48 (in Deutschland ohne DFS/Radarpruefung erlaubt).
 */
static int wl_scan_kanaele(u32 maske24, const u16 *k5, u32 n5)
{
	u8 buf[WL_HDR + 10 + 2 + 2 * 32];
	u32 ms = (u32)div_u64(ktime_get_ns(), NSEC_PER_MSEC);
	u32 plen, i;

	if (n5 > 32)
		n5 = 32;
	plen = WL_HDR + 10 + 2 + 2 * n5;

	if (wl_ctx < 0) {
		pr_warn(TAG "WLAN: SCAN ohne vorheriges OPEN\n");
		return -EINVAL;
	}
	memset(buf, 0, sizeof(buf));
	buf[0] = 0x10 | ((wl_ctx & 7) << 5);
	buf[1] = WIFI_CMD_SCAN;
	put_unaligned_le16(plen, buf + 2);
	put_unaligned_le32(ms, buf + 4);
	put_unaligned_le32(maske24, buf + WL_HDR);	/* Bit n-1 = Kanal n */
	/* reserved 0 (ABORT_SCAN_MODE waere 1), ssid_len 0 */
	put_unaligned_le16(n5, buf + WL_HDR + 10);
	for (i = 0; i < n5; i++)
		put_unaligned_le16(k5[i], buf + WL_HDR + 12 + 2 * i);
	scan_anzahl = 0;
	scan_rahmen = 0;
	pr_info(TAG "WLAN: sende WIFI_CMD_SCAN (2,4 GHz Maske 0x%04x, %u Kanaele 5 GHz)\n", maske24, n5);
	return sb7_senden(buf, plen);
}

static int wl_scan(bool fuenf)
{
	static const u16 k5[] = { 36, 40, 44, 48 };

	return wl_scan_kanaele(0x1fff, k5, fuenf ? ARRAY_SIZE(k5) : 0);
}

/*
 * sprdwl_cfg80211_connect() ohne wpa_supplicant: erst das RSN-Element fuer die Assoziierungsanfrage
 * (WIFI_CMD_SET_IE, Typ ASSOC_REQ), dann WIFI_CMD_CONNECT. Ohne Passwort - den 4-Wege-Handschlag
 * (EAPOL) macht unter Linux spaeter wpa_supplicant ueber die Datenkanaele. Erwartet: Anmeldung beim
 * Router, erste EAPOL-Nachricht auf Kanal 8/9, nach einigen Sekunden wirft der Router uns wieder raus.
 */
static int wl_connect(void)
{
	u8 ie[WL_HDR + 3 + 22], buf[WL_HDR + 82];
	u32 ms = (u32)div_u64(ktime_get_ns(), NSEC_PER_MSEC);
	u8 *r = ie + WL_HDR + 3, *c = buf + WL_HDR;
	static const u8 rsn_rest[] = { 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,	/* 1 paarweise: CCMP */
				       0x01, 0x00, 0x00, 0x0f, 0xac, 0x02,	/* 1 AKM: PSK */
				       0x00, 0x00 };				/* Faehigkeiten: kein MFP */
	int ret;

	if (wl_ctx < 0) {
		pr_warn(TAG "WLAN: CONNECT ohne vorheriges OPEN\n");
		return -EINVAL;
	}
	if (!ziel_da) {
		pr_warn(TAG "WLAN: CONNECT ohne Ziel - erst SSID in wlan_ziel schreiben, dann scan\n");
		return -ENOENT;
	}
	memset(ie, 0, sizeof(ie));
	ie[0] = 0x10 | ((wl_ctx & 7) << 5);
	ie[1] = WIFI_CMD_SET_IE;
	put_unaligned_le16(sizeof(ie), ie + 2);
	put_unaligned_le32(ms, ie + 4);
	ie[WL_HDR] = 3;					/* SPRDWL_IE_ASSOC_REQ */
	put_unaligned_le16(22, ie + WL_HDR + 1);
	r[0] = 48;					/* RSN-Element, 20 Bytes */
	r[1] = 20;
	r[2] = 1;					/* Version 1 */
	r[3] = 0;
	r[4] = 0x00;					/* Gruppe wie beim Router */
	r[5] = 0x0f;
	r[6] = 0xac;
	r[7] = ziel_gruppe;
	memcpy(r + 8, rsn_rest, sizeof(rsn_rest));
	pr_info(TAG "WLAN: sende WIFI_CMD_SET_IE (RSN fuer die Anmeldung)\n");
	ret = sb7_senden(ie, sizeof(ie));
	if (ret)
		return ret;
	msleep(300);

	memset(buf, 0, sizeof(buf));
	buf[0] = 0x10 | ((wl_ctx & 7) << 5);
	buf[1] = WIFI_CMD_CONNECT;
	put_unaligned_le16(sizeof(buf), buf + 2);
	put_unaligned_le32((u32)div_u64(ktime_get_ns(), NSEC_PER_MSEC), buf + 4);
	put_unaligned_le32(1 << 1, c);			/* SPRDWL_WPA_VERSION_2 */
	ether_addr_copy(c + 4, ziel_bssid);
	c[10] = ziel_kanal;
	c[11] = 0;					/* SPRDWL_AUTH_OPEN */
	c[12] = 0x80 | 4;				/* paarweise CCMP */
	c[13] = 0x80 | (ziel_gruppe == 4 ? 4 : 3);	/* Gruppe CCMP bzw. TKIP */
	c[14] = 0x80 | 2;				/* SPRDWL_AKM_SUITE_PSK */
	c[15] = 0;					/* kein MFP */
	c[16] = 0;					/* kein PSK an die Firmware */
	c[17] = ziel_ssid_len;
	memcpy(c + 18 + 32, ziel_ssid, ziel_ssid_len);
	pr_info(TAG "WLAN: sende WIFI_CMD_CONNECT (\"%*pE\", Kanal %u, ohne Passwort)\n",
		ziel_ssid_len, ziel_ssid, ziel_kanal);
	return sb7_senden(buf, sizeof(buf));
}

static int wl_disconnect(void)
{
	u8 buf[WL_HDR + 2];

	if (wl_ctx < 0)
		return -EINVAL;
	memset(buf, 0, sizeof(buf));
	buf[0] = 0x10 | ((wl_ctx & 7) << 5);
	buf[1] = WIFI_CMD_DISCONNECT;
	put_unaligned_le16(sizeof(buf), buf + 2);
	put_unaligned_le32((u32)div_u64(ktime_get_ns(), NSEC_PER_MSEC), buf + 4);
	put_unaligned_le16(3, buf + WL_HDR);		/* WLAN_REASON_DEAUTH_LEAVING */
	pr_info(TAG "WLAN: sende WIFI_CMD_DISCONNECT\n");
	return sb7_senden(buf, sizeof(buf));
}

/*
 * ---- wlan0: echtes WLAN-Geraet ueber cfg80211 (Aufbau nach Mainline virt_wifi.c) ----
 * Stand 22.10.: Einschalten (ip link set wlan0 up -> SYNC_VERSION, INI, SET_REGDOM einmalig, dann OPEN),
 * Ausschalten (CLOSE) und Scan (iw dev wlan0 scan). Senden verwirft noch alles, Verbinden fehlt noch.
 */
static DECLARE_COMPLETION(wl_antwort_da);
static u8 wl_antwort_id;
static s8 wl_antwort_st;
static bool __maybe_unused wl0_vorbereitet;	/* SYNC_VERSION/INI/REGDOM nach dem Firmware-Start schon geschickt */

#if WL_CFG80211
static struct wiphy *wl0_wiphy;
static struct net_device *wl0_ndev;
static struct wireless_dev wl0_wdev;
static struct cfg80211_scan_request *wl0_scan_anf;
static DEFINE_MUTEX(wl0_scan_lock);
static struct delayed_work wl0_scan_wache;

#define WL0_K24(n) { .band = NL80211_BAND_2GHZ, .center_freq = 2407 + 5 * (n), .hw_value = (n), .max_power = 20 }
#define WL0_K5(n)  { .band = NL80211_BAND_5GHZ, .center_freq = 5000 + 5 * (n), .hw_value = (n), .max_power = 20 }
static struct ieee80211_channel wl0_kanaele24[] = {
	WL0_K24(1), WL0_K24(2), WL0_K24(3), WL0_K24(4), WL0_K24(5), WL0_K24(6), WL0_K24(7),
	WL0_K24(8), WL0_K24(9), WL0_K24(10), WL0_K24(11), WL0_K24(12), WL0_K24(13),
};
/* vorerst nur 36-48: ohne DFS/Radarpruefung erlaubt; mehr erst mit echter Regulierung im Treiber */
static struct ieee80211_channel wl0_kanaele5[] = { WL0_K5(36), WL0_K5(40), WL0_K5(44), WL0_K5(48) };
/* Raten in 100 kbit/s (nur fuer cfg80211; die Firmware waehlt selbst) */
static struct ieee80211_rate wl0_raten24[] = {
	{ .bitrate = 10 }, { .bitrate = 20 }, { .bitrate = 55 }, { .bitrate = 110 },
	{ .bitrate = 60 }, { .bitrate = 90 }, { .bitrate = 120 }, { .bitrate = 180 },
	{ .bitrate = 240 }, { .bitrate = 360 }, { .bitrate = 480 }, { .bitrate = 540 },
};
static struct ieee80211_supported_band wl0_band24 = {
	.band = NL80211_BAND_2GHZ, .channels = wl0_kanaele24, .n_channels = ARRAY_SIZE(wl0_kanaele24),
	.bitrates = wl0_raten24, .n_bitrates = ARRAY_SIZE(wl0_raten24),
};
static struct ieee80211_supported_band wl0_band5 = {
	.band = NL80211_BAND_5GHZ, .channels = wl0_kanaele5, .n_channels = ARRAY_SIZE(wl0_kanaele5),
	.bitrates = wl0_raten24 + 4, .n_bitrates = ARRAY_SIZE(wl0_raten24) - 4,
};
#endif

static void wl0_antwort(u8 id, s8 st)
{
	wl_antwort_id = id;
	wl_antwort_st = st;
	complete(&wl_antwort_da);
}

/* Befehl schicken und auf seine Antwort warten (Realme sprdwl_cmd_send_recv, vereinfacht) */
static int __maybe_unused wl_befehl_warten(int (*befehl)(void), u8 id, unsigned int ms)
{
	unsigned long ende = jiffies + msecs_to_jiffies(ms);
	int ret;

	reinit_completion(&wl_antwort_da);
	ret = befehl();
	if (ret)
		return ret;
	for (;;) {
		long rest = (long)(ende - jiffies);

		if (rest <= 0 || !wait_for_completion_timeout(&wl_antwort_da, rest)) {
			pr_warn(TAG "wlan0: keine Antwort auf %s\n", wl_befehl_name(id));
			return -ETIMEDOUT;
		}
		if (wl_antwort_id == id)
			break;
		reinit_completion(&wl_antwort_da);	/* Antwort auf etwas anderes - weiter warten */
	}
	if (wl_antwort_st < 0) {
		pr_warn(TAG "wlan0: %s -> %s\n", wl_befehl_name(id), wl_status_text(id, wl_antwort_st));
		return -EIO;
	}
	return 0;
}

static const u8 *wl_puffer;
static u32 wl_puffer_len;
static int wl_puffer_senden(void)
{
	return sb7_senden(wl_puffer, wl_puffer_len);
}

static int __maybe_unused wl_puffer_warten(const u8 *buf, u32 len, unsigned int ms)
{
	wl_puffer = buf;
	wl_puffer_len = len;
	return wl_befehl_warten(wl_puffer_senden, buf[1], ms);
}

#if WL_CFG80211
enum { WL0_FREI, WL0_VERBINDET, WL0_VERBUNDEN, WL0_TRENNT };
static int wl0_zustand = WL0_FREI;
static u8 wl0_bssid[ETH_ALEN];
static DEFINE_MUTEX(wl0_zustand_lock);

static void wl0_bss_melden(u8 kanal, s8 signal, const u8 *f, u16 flen)
{
	struct ieee80211_channel *ch;
	struct cfg80211_bss *bss;

	if (!wl0_wiphy)
		return;
	ch = ieee80211_get_channel(wl0_wiphy, ieee80211_channel_to_frequency(kanal,
				   kanal <= 14 ? NL80211_BAND_2GHZ : NL80211_BAND_5GHZ));
	if (!ch)
		return;
	bss = cfg80211_inform_bss_frame(wl0_wiphy, ch, (struct ieee80211_mgmt *)f, flen,
					(s32)signal * 100, GFP_KERNEL);
	cfg80211_put_bss(wl0_wiphy, bss);
}

/* Scan-Rahmen nur waehrend eines cfg80211-Scans weitergeben */
static void wl0_bss(u8 kanal, s8 signal, const u8 *f, u16 flen)
{
	if (wl0_scan_anf)
		wl0_bss_melden(kanal, signal, f, flen);
}

static void wl0_scan_ende(bool abgebrochen)
{
	struct cfg80211_scan_info info = { .aborted = abgebrochen };

	mutex_lock(&wl0_scan_lock);
	if (wl0_scan_anf) {
		cancel_delayed_work(&wl0_scan_wache);
		cfg80211_scan_done(wl0_scan_anf, &info);
		wl0_scan_anf = NULL;
		pr_info(TAG "wlan0: Scan an cfg80211 gemeldet (%s)\n", abgebrochen ? "abgebrochen" : "fertig");
	}
	mutex_unlock(&wl0_scan_lock);
}

/* kommt kein SCAN_DONE (Realme: 10 s), Scan als abgebrochen melden, sonst bleibt cfg80211 "busy" */
static void wl0_scan_wache_fn(struct work_struct *w)
{
	pr_warn(TAG "wlan0: kein SCAN_DONE nach 12 s\n");
	wl0_scan_ende(true);
}

static int wl0_scan(struct wiphy *wiphy, struct cfg80211_scan_request *anf)
{
	u16 k5[32];
	u32 maske = 0, n5 = 0, i;
	int ret;

	if (wl_ctx < 0)
		return -ENETDOWN;
	for (i = 0; i < anf->n_channels; i++) {
		u16 k = anf->channels[i]->hw_value;

		if (k >= 1 && k <= 14)
			maske |= BIT(k - 1);
		else if (n5 < ARRAY_SIZE(k5))
			k5[n5++] = k;
	}
	mutex_lock(&wl0_scan_lock);
	if (wl0_scan_anf) {
		mutex_unlock(&wl0_scan_lock);
		return -EBUSY;
	}
	wl0_scan_anf = anf;
	mutex_unlock(&wl0_scan_lock);
	ret = wl_scan_kanaele(maske, k5, n5);
	if (ret) {
		mutex_lock(&wl0_scan_lock);
		wl0_scan_anf = NULL;
		mutex_unlock(&wl0_scan_lock);
		return ret;
	}
	schedule_delayed_work(&wl0_scan_wache, 12 * HZ);
	return 0;
}

static u8 wl0_cipher(u32 c)
{
	switch (c) {
	case WLAN_CIPHER_SUITE_WEP40: return 1;
	case WLAN_CIPHER_SUITE_WEP104: return 2;
	case WLAN_CIPHER_SUITE_TKIP: return 3;
	case WLAN_CIPHER_SUITE_CCMP: return 4;
	case WLAN_CIPHER_SUITE_AES_CMAC: return 8;
	case WLAN_CIPHER_SUITE_GCMP_256: return 9;
	default: return 0;
	}
}

static u8 wl0_akm(u32 a)
{
	switch (a) {
	case WLAN_AKM_SUITE_8021X: return 1;
	case WLAN_AKM_SUITE_PSK: return 2;
	case WLAN_AKM_SUITE_FT_8021X: return 3;
	case WLAN_AKM_SUITE_FT_PSK: return 4;
	case WLAN_AKM_SUITE_8021X_SHA256: return 5;
	case WLAN_AKM_SUITE_PSK_SHA256: return 6;
	case WLAN_AKM_SUITE_SAE: return 8;
	default: return 0;
	}
}

/*
 * sprdwl_cfg80211_connect(): RSN/WPA-Elemente von wpa_supplicant (sme->ie) als SET_IE ASSOC_REQ,
 * dann WIFI_CMD_CONNECT. Ergebnis kommt spaeter als Ereignis CONNECT/DISCONNECT.
 */
static int wl0_connect(struct wiphy *wiphy, struct net_device *nd, struct cfg80211_connect_params *sme)
{
	u8 *buf, *c;
	u32 wpa = 0;
	int ret;

	if (wl_ctx < 0)
		return -ENETDOWN;
	if (!sme->ssid || !sme->ssid_len || sme->ssid_len > 32 || sme->ie_len > 1024)
		return -EINVAL;
	if (sme->key_len)
		return -EOPNOTSUPP;	/* WEP/statische Schluessel noch nicht */
	buf = kzalloc(WL_HDR + 3 + 1024, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (sme->ie_len) {
		buf[0] = 0x10 | ((wl_ctx & 7) << 5);
		buf[1] = WIFI_CMD_SET_IE;
		put_unaligned_le16(WL_HDR + 3 + sme->ie_len, buf + 2);
		put_unaligned_le32((u32)div_u64(ktime_get_ns(), NSEC_PER_MSEC), buf + 4);
		buf[WL_HDR] = 3;			/* SPRDWL_IE_ASSOC_REQ */
		put_unaligned_le16(sme->ie_len, buf + WL_HDR + 1);
		memcpy(buf + WL_HDR + 3, sme->ie, sme->ie_len);
		ret = wl_puffer_warten(buf, WL_HDR + 3 + sme->ie_len, 1000);
		if (ret)
			goto raus;
	}

	memset(buf, 0, WL_HDR + 82);
	c = buf + WL_HDR;
	buf[0] = 0x10 | ((wl_ctx & 7) << 5);
	buf[1] = WIFI_CMD_CONNECT;
	put_unaligned_le16(WL_HDR + 82, buf + 2);
	put_unaligned_le32((u32)div_u64(ktime_get_ns(), NSEC_PER_MSEC), buf + 4);
	if (sme->crypto.wpa_versions & NL80211_WPA_VERSION_1)
		wpa = 1 << 0;
	if (sme->crypto.wpa_versions & NL80211_WPA_VERSION_2)
		wpa = 1 << 1;
	if (sme->crypto.wpa_versions & NL80211_WPA_VERSION_3)
		wpa = 1 << 3;
	put_unaligned_le32(wpa, c);
	if (sme->bssid)
		ether_addr_copy(c + 4, sme->bssid);
	else if (sme->bssid_hint)
		ether_addr_copy(c + 4, sme->bssid_hint);
	if (sme->channel)
		c[10] = ieee80211_frequency_to_channel(sme->channel->center_freq);
	else if (sme->channel_hint)
		c[10] = ieee80211_frequency_to_channel(sme->channel_hint->center_freq);
	c[11] = sme->auth_type == NL80211_AUTHTYPE_SAE ? 4 :
		sme->auth_type == NL80211_AUTHTYPE_SHARED_KEY ? 1 : 0;
	if (sme->crypto.n_ciphers_pairwise && wl0_cipher(sme->crypto.ciphers_pairwise[0]))
		c[12] = 0x80 | wl0_cipher(sme->crypto.ciphers_pairwise[0]);
	if (wl0_cipher(sme->crypto.cipher_group))
		c[13] = 0x80 | wl0_cipher(sme->crypto.cipher_group);
	if (sme->crypto.n_akm_suites && wl0_akm(sme->crypto.akm_suites[0]))
		c[14] = 0x80 | wl0_akm(sme->crypto.akm_suites[0]);
	c[15] = sme->mfp == NL80211_MFP_REQUIRED;
	c[17] = sme->ssid_len;
	memcpy(c + 18 + 32, sme->ssid, sme->ssid_len);
	memcpy(wl0_bssid, c + 4, ETH_ALEN);

	mutex_lock(&wl0_zustand_lock);
	wl0_zustand = WL0_VERBINDET;
	mutex_unlock(&wl0_zustand_lock);
	netdev_info(nd, "verbinde mit \"%*pE\", Kanal %u (WPA 0x%x, paarweise 0x%02x, Gruppe 0x%02x, AKM 0x%02x)\n",
		    (int)sme->ssid_len, sme->ssid, c[10], wpa, c[12], c[13], c[14]);
	ret = wl_puffer_warten(buf, WL_HDR + 82, 2000);
	if (ret) {
		mutex_lock(&wl0_zustand_lock);
		wl0_zustand = WL0_FREI;
		mutex_unlock(&wl0_zustand_lock);
	}
raus:
	kfree(buf);
	return ret;
}

static int wl0_disconnect(struct wiphy *wiphy, struct net_device *nd, u16 grund)
{
	u8 buf[WL_HDR + 2];
	bool selbst_melden;

	mutex_lock(&wl0_zustand_lock);
	if (wl0_zustand == WL0_FREI) {
		mutex_unlock(&wl0_zustand_lock);
		return 0;
	}
	wl0_zustand = WL0_TRENNT;
	mutex_unlock(&wl0_zustand_lock);
	memset(buf, 0, sizeof(buf));
	buf[0] = 0x10 | ((wl_ctx & 7) << 5);
	buf[1] = WIFI_CMD_DISCONNECT;
	put_unaligned_le16(sizeof(buf), buf + 2);
	put_unaligned_le32((u32)div_u64(ktime_get_ns(), NSEC_PER_MSEC), buf + 4);
	put_unaligned_le16(grund, buf + WL_HDR);
	if (wl_ctx >= 0 && sb7_bereit)
		wl_puffer_warten(buf, sizeof(buf), 2000);
	msleep(300);			/* Firmware meldet meist noch ein DISCONNECT-Ereignis */
	mutex_lock(&wl0_zustand_lock);
	selbst_melden = wl0_zustand == WL0_TRENNT;
	wl0_zustand = WL0_FREI;
	mutex_unlock(&wl0_zustand_lock);
	if (selbst_melden)
		cfg80211_disconnected(nd, grund, NULL, 0, true, GFP_KERNEL);
	netif_carrier_off(nd);
	return 0;
}

/* Ereignis CONNECT: u8 Status, bssid[6], Kanal, Signal, le16+req_ie, le16+resp_ie, le16+Beacon */
static void wl0_verbunden_ereignis(const u8 *d, u32 n)
{
	const u8 *req = NULL, *resp = NULL, *bea = NULL;
	u16 req_n = 0, resp_n = 0, bea_n = 0;
	u32 pos;
	int zustand;

	if (!wl0_ndev)
		return;
	mutex_lock(&wl0_zustand_lock);
	zustand = wl0_zustand;
	mutex_unlock(&wl0_zustand_lock);
	if (zustand != WL0_VERBINDET)
		return;
	if (d[0] != 0 || n < 9) {
		cfg80211_connect_result(wl0_ndev, wl0_bssid, NULL, 0, NULL, 0,
					n >= 3 && d[2] ? d[2] : WLAN_STATUS_UNSPECIFIED_FAILURE, GFP_KERNEL);
		mutex_lock(&wl0_zustand_lock);
		wl0_zustand = WL0_FREI;
		mutex_unlock(&wl0_zustand_lock);
		return;
	}
	ether_addr_copy(wl0_bssid, d + 1);
	pos = 9;
	if (pos + 2 <= n) {
		req_n = get_unaligned_le16(d + pos);
		req = d + pos + 2;
		pos += 2 + req_n;
	}
	if (pos + 2 <= n) {
		resp_n = get_unaligned_le16(d + pos);
		resp = d + pos + 2;
		pos += 2 + resp_n;
	}
	if (pos + 2 <= n) {
		bea_n = get_unaligned_le16(d + pos);
		bea = d + pos + 2;
		pos += 2 + bea_n;
	}
	if (pos > n) {		/* Laengen passen nicht - ohne IEs melden */
		netdev_warn(wl0_ndev, "CONNECT-Ereignis: IE-Laengen unplausibel (%u > %u)\n", pos, n);
		req = resp = bea = NULL;
		req_n = resp_n = bea_n = 0;
	}
	if (bea && bea_n >= 36)	/* Beacon des Routers, damit cfg80211 den BSS-Eintrag hat */
		wl0_bss_melden(d[7], (s8)d[8], bea, bea_n);
	cfg80211_connect_result(wl0_ndev, wl0_bssid, req, req_n, resp, resp_n,
				WLAN_STATUS_SUCCESS, GFP_KERNEL);
	mutex_lock(&wl0_zustand_lock);
	wl0_zustand = WL0_VERBUNDEN;
	mutex_unlock(&wl0_zustand_lock);
	netif_carrier_on(wl0_ndev);
	netdev_info(wl0_ndev, "verbunden (Kanal %u, %d dBm, IEs %u/%u/%u)\n", d[7], (s8)d[8],
		    req_n, resp_n, bea_n);
}

static void wl0_getrennt_ereignis(u16 grund)
{
	int zustand;

	if (!wl0_ndev)
		return;
	mutex_lock(&wl0_zustand_lock);
	zustand = wl0_zustand;
	wl0_zustand = WL0_FREI;
	mutex_unlock(&wl0_zustand_lock);
	netif_carrier_off(wl0_ndev);
	if (zustand == WL0_VERBINDET)
		cfg80211_connect_result(wl0_ndev, wl0_bssid, NULL, 0, NULL, 0,
					WLAN_STATUS_UNSPECIFIED_FAILURE, GFP_KERNEL);
	else if (zustand == WL0_VERBUNDEN || zustand == WL0_TRENNT)
		cfg80211_disconnected(wl0_ndev, grund, NULL, 0, zustand == WL0_TRENNT, GFP_KERNEL);
}

/*
 * Empfangsblock eines Datenkanals: Byte 0 Typ (unten 4 Bit), Byte 1 Versatz des Ethernet-Kopfs,
 * Byte 2-3 Laenge des Ethernet-Rahmens (belegt 22.10. mit EAPOL: Typ 3, Versatz 54, 113 Bytes).
 * Vorerst nur Rahmen, die direkt im Block stehen.
 */
static void wl0_rx(u8 kanal, const u8 *b, u32 len)
{
	struct sk_buff *skb;
	u32 eoff, elen;

	if (!wl0_ndev || !netif_running(wl0_ndev) || len < 4)
		return;
	eoff = b[1];
	elen = get_unaligned_le16(b + 2);
	if ((b[0] & 0x0f) != 3 || eoff < 4 || elen < ETH_HLEN || eoff + elen > len) {
		wl0_ndev->stats.rx_frame_errors++;
		return;
	}
	skb = netdev_alloc_skb_ip_align(wl0_ndev, elen);
	if (!skb) {
		wl0_ndev->stats.rx_dropped++;
		return;
	}
	skb_put_data(skb, b + eoff, elen);
	skb->protocol = eth_type_trans(skb, wl0_ndev);
	wl0_ndev->stats.rx_packets++;
	wl0_ndev->stats.rx_bytes += elen;
	netif_rx(skb);
}

static const struct cfg80211_ops wl0_ops = {
	.scan = wl0_scan,
	.connect = wl0_connect,
	.disconnect = wl0_disconnect,
};

static const u32 wl0_ciphers[] = {
	WLAN_CIPHER_SUITE_CCMP, WLAN_CIPHER_SUITE_TKIP, WLAN_CIPHER_SUITE_AES_CMAC,
};

static int wl0_ndo_open(struct net_device *nd)
{
	int ret;

	if (!sb7_bereit) {
		netdev_warn(nd, "WCN-Firmware laeuft noch nicht (erst redmi-wcntest.sh 3)\n");
		return -EAGAIN;
	}
	if (!wl0_vorbereitet) {
		ret = wl_befehl_warten(wl_sync_version, WIFI_CMD_SYNC_VERSION, 1000);
		if (ret)
			return ret;
		ret = wl_download_ini();	/* 4 Abschnitte mit Pausen, Antworten laufen nebenher ein */
		if (ret)
			return ret;
		msleep(500);
		ret = wl_befehl_warten(wl_regdom, WIFI_CMD_SET_REGDOM, 1000);
		if (ret)
			return ret;
		wl0_vorbereitet = true;
	}
	ret = wl_befehl_warten(wl_open, WIFI_CMD_OPEN, 2000);
	if (ret)
		return ret;
	if (wl_ctx < 0)
		return -EIO;
	netif_carrier_off(nd);		/* noch nicht verbunden */
	netif_start_queue(nd);
	netdev_info(nd, "WLAN eingeschaltet (ctx %d)\n", wl_ctx);
	return 0;
}

static int wl0_ndo_stop(struct net_device *nd)
{
	netif_stop_queue(nd);
	cancel_delayed_work_sync(&wl0_scan_wache);
	wl0_scan_ende(true);
	if (wl_ctx >= 0 && sb7_bereit)
		wl_befehl_warten(wl_close, WIFI_CMD_CLOSE, 1000);
	wl_ctx = -1;
	netdev_info(nd, "WLAN ausgeschaltet\n");
	return 0;
}

static netdev_tx_t wl0_ndo_xmit(struct sk_buff *skb, struct net_device *nd)
{
	nd->stats.tx_dropped++;		/* Senden ueber Kanal 8 kommt im naechsten Schritt */
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static const struct net_device_ops wl0_ndo = {
	.ndo_open = wl0_ndo_open,
	.ndo_stop = wl0_ndo_stop,
	.ndo_start_xmit = wl0_ndo_xmit,
};

static int wl0_anmelden(void)
{
	u8 mac[ETH_ALEN];
	int ret;

	INIT_DELAYED_WORK(&wl0_scan_wache, wl0_scan_wache_fn);
	wl0_wiphy = wiphy_new(&wl0_ops, 0);
	if (!wl0_wiphy)
		return -ENOMEM;
	set_wiphy_dev(wl0_wiphy, &pdev->dev);
	wl0_wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);
	wl0_wiphy->bands[NL80211_BAND_2GHZ] = &wl0_band24;
	wl0_wiphy->bands[NL80211_BAND_5GHZ] = &wl0_band5;
	wl0_wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;
	wl0_wiphy->max_scan_ssids = 1;	/* Firmware-Scan ohne SSID; Platzhalter-SSID von iw ist ok */
	wl0_wiphy->max_scan_ie_len = 255;	/* wpa_supplicant haengt IEs an; die Firmware sucht ohne sie */
	wl0_wiphy->cipher_suites = wl0_ciphers;
	wl0_wiphy->n_cipher_suites = ARRAY_SIZE(wl0_ciphers);
	wl_mac_holen(mac);
	memcpy(wl0_wiphy->perm_addr, mac, ETH_ALEN);
	ret = wiphy_register(wl0_wiphy);
	if (ret) {
		wiphy_free(wl0_wiphy);
		wl0_wiphy = NULL;
		return ret;
	}
	wl0_ndev = alloc_netdev(0, "wlan%d", NET_NAME_ENUM, ether_setup);
	if (!wl0_ndev) {
		ret = -ENOMEM;
		goto weg_wiphy;
	}
	wl0_wdev.wiphy = wl0_wiphy;
	wl0_wdev.netdev = wl0_ndev;
	wl0_wdev.iftype = NL80211_IFTYPE_STATION;
	wl0_ndev->ieee80211_ptr = &wl0_wdev;
	wl0_ndev->netdev_ops = &wl0_ndo;
	eth_hw_addr_set(wl0_ndev, mac);
	SET_NETDEV_DEV(wl0_ndev, wiphy_dev(wl0_wiphy));
	ret = register_netdev(wl0_ndev);
	if (ret) {
		free_netdev(wl0_ndev);
		wl0_ndev = NULL;
		goto weg_wiphy;
	}
	pr_info(TAG "wlan0: angemeldet als %s (cfg80211)\n", wl0_ndev->name);
	return 0;
weg_wiphy:
	wiphy_unregister(wl0_wiphy);
	wiphy_free(wl0_wiphy);
	wl0_wiphy = NULL;
	return ret;
}

static void wl0_abmelden(void)
{
	if (wl0_ndev) {
		unregister_netdev(wl0_ndev);	/* ruft vorher ndo_stop */
		free_netdev(wl0_ndev);
		wl0_ndev = NULL;
	}
	if (wl0_wiphy) {
		cancel_delayed_work_sync(&wl0_scan_wache);
		wiphy_unregister(wl0_wiphy);
		wiphy_free(wl0_wiphy);
		wl0_wiphy = NULL;
	}
}
#else
static void wl0_bss(u8 kanal, s8 signal, const u8 *f, u16 flen) { }
static void wl0_scan_ende(bool abgebrochen) { }
static void wl0_verbunden_ereignis(const u8 *d, u32 n) { }
static void wl0_getrennt_ereignis(u16 grund) { }
static void wl0_rx(u8 kanal, const u8 *b, u32 len) { }
static int wl0_anmelden(void) { return -EOPNOTSUPP; }
static void wl0_abmelden(void) { }
#endif

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

/*
 * ---- Kanal 8 und 9 (WLAN-Daten, sblock) ----
 * Realme wcn_sipc.c: SIPC_WIFI_DATA0 tx 64 x 1664, rx 192 x 1664; SIPC_WIFI_DATA1 tx 64 x 1664, rx 8 x 1664.
 * Aufbau wie Kanal 7 (sblock_host_init), Lage im gemeinsamen Speicher:
 *   Kanal 8 bei SMEM + 0xb0000 (bis 0x119060), Kanal 9 bei SMEM + 0x120000 (bis 0x13d460), Ende 0x140000.
 * Beleg 21.10.: nach WIFI_CMD_OPEN meldet die Firmware "sipc devices open : 8" und wartet (sbuf_wait).
 * Vorerst nur Empfang: Bloecke gekuerzt ins dmesg, dann zurueck in den Pool.
 */
struct sbd {
	u8 kanal;
	u32 off, txn, rxn, blk;
	bool bereit;
	unsigned int bloecke;
	struct mutex lock;
};

static struct sbd sbds[] = {
	{ .kanal = 8, .off = 0xb0000, .txn = 64, .rxn = 192, .blk = 1664 },
	{ .kanal = 9, .off = 0x120000, .txn = 64, .rxn = 8, .blk = 1664 },
};

static u32 sbd_txblk(const struct sbd *d) { return d->off + 96; }
static u32 sbd_rxblk(const struct sbd *d) { return sbd_txblk(d) + d->txn * d->blk; }
static u32 sbd_r_txl(const struct sbd *d) { return sbd_rxblk(d) + d->rxn * d->blk; }
static u32 sbd_r_rxl(const struct sbd *d) { return sbd_r_txl(d) + d->txn * 8; }
static u32 sbd_p_txl(const struct sbd *d) { return sbd_r_rxl(d) + d->rxn * 8; }
static u32 sbd_p_rxl(const struct sbd *d) { return sbd_p_txl(d) + d->txn * 8; }
static u32 sbd_ende(const struct sbd *d) { return sbd_p_rxl(d) + d->rxn * 8; }

static struct sbd *sbd_fuer(u8 ch)
{
	unsigned int i;

	if (!datenkanaele)
		return NULL;
	for (i = 0; i < ARRAY_SIZE(sbds); i++)
		if (sbds[i].kanal == ch)
			return &sbds[i];
	return NULL;
}

static void sbd_vorbereiten(struct sbd *d)
{
	u32 r = d->off + SH_RING, p = d->off + SH_POOL, i;

	memset(smem + d->off, 0, sbd_ende(d) - d->off);
	sm_wr(r + SH_TX_ADDR, SMEM_CP + sbd_txblk(d));
	sm_wr(r + SH_TX_CNT, d->txn);
	sm_wr(r + SH_TX_SIZE, d->blk);
	sm_wr(r + SH_TX_BLKS, SMEM_CP + sbd_r_txl(d));
	sm_wr(r + SH_RX_ADDR, SMEM_CP + sbd_rxblk(d));
	sm_wr(r + SH_RX_CNT, d->rxn);
	sm_wr(r + SH_RX_SIZE, d->blk);
	sm_wr(r + SH_RX_BLKS, SMEM_CP + sbd_r_rxl(d));
	sm_wr(p + SH_TX_ADDR, SMEM_CP + sbd_txblk(d));
	sm_wr(p + SH_TX_CNT, d->txn);
	sm_wr(p + SH_TX_SIZE, d->blk);
	sm_wr(p + SH_TX_BLKS, SMEM_CP + sbd_p_txl(d));
	sm_wr(p + SH_RX_ADDR, SMEM_CP + sbd_rxblk(d));
	sm_wr(p + SH_RX_CNT, d->rxn);
	sm_wr(p + SH_RX_SIZE, d->blk);
	sm_wr(p + SH_RX_BLKS, SMEM_CP + sbd_p_rxl(d));
	for (i = 0; i < d->txn; i++) {
		sm_wr(sbd_p_txl(d) + i * 8, SMEM_CP + sbd_txblk(d) + i * d->blk);
		sm_wr(sbd_p_txl(d) + i * 8 + 4, d->blk);
	}
	sm_wr(p + SH_TX_WR, d->txn);
	for (i = 0; i < d->rxn; i++) {
		sm_wr(sbd_p_rxl(d) + i * 8, SMEM_CP + sbd_rxblk(d) + i * d->blk);
		sm_wr(sbd_p_rxl(d) + i * 8 + 4, d->blk);
	}
	sm_wr(p + SH_RX_WR, d->rxn);
	wmb();
	pr_info(TAG "sblock fuer Kanal %u bei 0x%x (WCN-Sicht 0x%x): tx %ux%u @0x%x, rx %ux%u @0x%x, Ende 0x%x\n",
		d->kanal, SMEM_AP + d->off, SMEM_CP + d->off, d->txn, d->blk, SMEM_CP + sbd_txblk(d),
		d->rxn, d->blk, SMEM_CP + sbd_rxblk(d), SMEM_CP + sbd_ende(d));
}

/* sblock_receive() + sblock_release(): Bloecke gekuerzt anzeigen und zurueckgeben */
static void sbd_empfangen(struct sbd *d)
{
	u32 r = d->off + SH_RING, p = d->off + SH_POOL;
	u32 rd, wr, addr, len, off, pwr;
	bool melden = false;
	char kopf[40];

	if (!d->bereit)
		return;
	mutex_lock(&d->lock);
	for (;;) {
		rmb();
		rd = sm_rd(r + SH_RX_RD);
		wr = sm_rd(r + SH_RX_WR);
		if (rd == wr)
			break;
		if (wr - rd > d->rxn) {
			pr_warn(TAG "Kanal %u: Ring-Zeiger unplausibel (rd %u, wr %u)\n", d->kanal, rd, wr);
			break;
		}
		addr = sm_rd(sbd_r_rxl(d) + (rd % d->rxn) * 8);
		len = sm_rd(sbd_r_rxl(d) + (rd % d->rxn) * 8 + 4);
		off = addr - SMEM_CP;
		d->bloecke++;
		if (off < sbd_rxblk(d) || off + d->blk > sbd_r_txl(d) || (off - sbd_rxblk(d)) % d->blk ||
		    len > d->blk) {
			pr_warn(TAG "Kanal %u: Block %u unplausibel (0x%x, %u Bytes)\n", d->kanal, d->bloecke, addr, len);
		} else {
			wl0_rx(d->kanal, smem + off, len);	/* an wlan0, falls angemeldet */
			if (d->bloecke <= 20) {
				pr_info(TAG "Kanal %u: Block %u, %u Bytes\n", d->kanal, d->bloecke, len);
				sbd_eapol_suchen(d->kanal, smem + off, len);
				snprintf(kopf, sizeof(kopf), "wcnlausch: K%u #%u ", d->kanal, d->bloecke);
				print_hex_dump(KERN_INFO, kopf, DUMP_PREFIX_OFFSET, 16, 1, smem + off,
					       min(len, 48u), false);
			}
		}
		sm_wr(r + SH_RX_RD, rd + 1);
		pwr = sm_rd(p + SH_RX_WR);
		sm_wr(sbd_p_rxl(d) + (pwr % d->rxn) * 8, addr);
		sm_wr(sbd_p_rxl(d) + (pwr % d->rxn) * 8 + 4, d->blk);
		wmb();
		sm_wr(p + SH_RX_WR, pwr + 1);
		wmb();
		if (pwr + 1 - sm_rd(p + SH_RX_RD) == 1)
			melden = true;
	}
	mutex_unlock(&d->lock);
	if (melden)
		senden(d->kanal, T_EVENT, EV_SBLOCK_RELEASE, 0, "EVENT RELEASE (Datenblock zurueck)");
}

static void sbd_bearbeiten(struct sbd *d, u8 typ, u16 flag)
{
	char was[48];

	switch (typ) {
	case T_OPEN:	/* auch mit alle_oeffnen=1: Datenkanaele werden hier quittiert */
		if (flag == SMSG_OPEN_MAGIC) {
			d->bereit = false;
			snprintf(was, sizeof(was), "OPEN Kanal %u", d->kanal);
			senden(d->kanal, T_OPEN, SMSG_OPEN_MAGIC, 0, was);
		}
		break;
	case T_CLOSE:
		d->bereit = false;
		snprintf(was, sizeof(was), "CLOSE-Antwort Kanal %u", d->kanal);
		senden(d->kanal, T_CLOSE, SMSG_CLOSE_MAGIC, 0, was);
		break;
	case T_CMD:
		if (flag == CMD_SBUF_INIT && !d->bereit) {	/* SMSG_CMD_SBLOCK_INIT = 1 */
			mutex_lock(&d->lock);
			sbd_vorbereiten(d);
			mutex_unlock(&d->lock);
			d->bereit = true;
			snprintf(was, sizeof(was), "DONE SBLOCK_INIT Kanal %u (WCN 0x%x)", d->kanal,
				 SMEM_CP + d->off);
			senden(d->kanal, T_DONE, DONE_SBUF_INIT, SMEM_CP + d->off, was);
			sbd_empfangen(d);
		}
		break;
	case T_EVENT:
		if (flag == EV_SBLOCK_SEND)
			sbd_empfangen(d);
		break;
	}
}

static void sbd_alle_empfangen(void)
{
	unsigned int i;

	if (!datenkanaele)
		return;
	for (i = 0; i < ARRAY_SIZE(sbds); i++)
		sbd_empfangen(&sbds[i]);
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
			/*
			 * Typ 0x0920 = Textmeldungen der Firmware: jeder Eintrag beginnt mit ein paar
			 * Binaerbytes (Laenge/Kennung), danach "[T:...]text". druckbar() lehnt solche
			 * Rahmen ab, wenn sie mit einem Kopf beginnen - deshalb immer als Text ausgeben
			 * (nicht druckbare Zeichen werden zu '.').
			 */
			if (typ == 0x0920 || druckbar(d, dn)) {
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

/*
 * Die Firmware schreibt die volle Geraete-MAC in ihr Log ("mac addr AA:BB:CC:DD:EE:FF").
 * Damit Logs teilbar bleiben, wird jede MAC im gesammelten Log zur Haelfte ausgeblendet.
 */
static void mac_ausblenden(char *t, size_t n)
{
	size_t i, k;

	for (i = 0; i + 17 <= n; i++) {
		for (k = 0; k < 17; k++)
			if ((k % 3 == 2) ? t[i + k] != ':' : !isxdigit(t[i + k]))
				break;
		if (k == 17) {
			memcpy(t + i + 9, "xx:xx:xx", 8);
			i += 16;
		}
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
	if (wlog) {	/* auch ueber die Grenze zum vorigen Stueck hinweg */
		size_t ab = (wlog_len > (rd - alt) + 16) ? wlog_len - (rd - alt) - 16 : 0;

		mac_ausblenden(wlog + ab, wlog_len - ab);
	}
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
	sbd_alle_empfangen();
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
	    !(ch == SB7_KANAL && kanal7) && !sbd_fuer(ch)) {
		char was[32];

		snprintf(was, sizeof(was), "OPEN Kanal %u (nur Quittung)", ch);
		senden(ch, T_OPEN, SMSG_OPEN_MAGIC, 0, was);
		return;
	}

	if (ch == SB7_KANAL && kanal7) {
		sb7_bearbeiten(typ, flag, hi);
		return;
	}

	if (sbd_fuer(ch)) {
		sbd_bearbeiten(sbd_fuer(ch), typ, flag);
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

/* Testbefehle fuer Kanal 7: "sync_version", "get_info", "download_ini", "regdom", "open", "scan", "scan5", "connect", "disconnect", "close" */
static ssize_t wlan_befehl_store(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t count)
{
	int ret;

	if (sysfs_streq(buf, "sync_version"))
		ret = wl_sync_version();
	else if (sysfs_streq(buf, "get_info"))
		ret = wl_get_info();
	else if (sysfs_streq(buf, "download_ini"))
		ret = wl_download_ini();
	else if (sysfs_streq(buf, "open"))
		ret = wl_open();
	else if (sysfs_streq(buf, "close"))
		ret = wl_close();
	else if (sysfs_streq(buf, "connect"))
		ret = wl_connect();
	else if (sysfs_streq(buf, "disconnect"))
		ret = wl_disconnect();
	else if (sysfs_streq(buf, "regdom"))
		ret = wl_regdom();
	else if (sysfs_streq(buf, "scan"))
		ret = wl_scan(false);
	else if (sysfs_streq(buf, "scan5"))
		ret = wl_scan(true);
	else
		ret = -EINVAL;
	return ret ? ret : count;
}
static DEVICE_ATTR_WO(wlan_befehl);

/* SSID fuer "connect" (wird beim naechsten Scan gesucht); Lesen zeigt nur, ob es gefunden wurde */
static ssize_t wlan_ziel_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	size_t n = count;

	if (n && buf[n - 1] == '\n')
		n--;
	if (!n || n > 32)
		return -EINVAL;
	memcpy(ziel_ssid, buf, n);
	ziel_ssid[n] = 0;
	ziel_ssid_len = n;
	ziel_da = false;
	pr_info(TAG "ZIEL: \"%*pE\" gemerkt, wird beim naechsten Scan gesucht\n", (int)n, ziel_ssid);
	return count;
}

static ssize_t wlan_ziel_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (!ziel_ssid_len)
		return sysfs_emit(buf, "kein Ziel\n");
	return sysfs_emit(buf, "%s (Kanal %u, %d dBm)\n", ziel_da ? "gefunden" : "noch nicht gefunden",
			  ziel_kanal, ziel_signal);
}
static DEVICE_ATTR_RW(wlan_ziel);
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

	wl0_abmelden();		/* zuerst: ndo_stop schickt noch CLOSE */
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
	if (pdev && wlan_datei) {
		device_remove_file(&pdev->dev, &dev_attr_wlan_befehl);
		device_remove_file(&pdev->dev, &dev_attr_wlan_ziel);
	}
	wlan_datei = false;
	for (i = 0; i < ARRAY_SIZE(kleine); i++)
		kleine[i].bereit = false;
	sb7_bereit = false;
	for (i = 0; i < ARRAY_SIZE(sbds); i++)
		sbds[i].bereit = false;
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
	for (i = 0; i < ARRAY_SIZE(sbds); i++)
		mutex_init(&sbds[i].lock);
	if (kanal < 0 || kanal > 15) {
		pr_err(TAG "kanal muss 0..15 sein\n");
		return -EINVAL;
	}
	if (at && !antworten) {
		pr_err(TAG "at=1 braucht antworten=1\n");
		return -EINVAL;
	}
	if ((kanal7 || kanal11 || kanal12 || datenkanaele) && !antworten) {
		pr_err(TAG "kanal7/kanal11/kanal12/datenkanaele brauchen antworten=1\n");
		return -EINVAL;
	}
	if (wlan0 && !(kanal7 && datenkanaele)) {
		pr_err(TAG "wlan0=1 braucht kanal7=1 und datenkanaele=1\n");
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
		if (wlan_datei && device_create_file(&pdev->dev, &dev_attr_wlan_ziel))
			pr_warn(TAG "Datei wlan_ziel fehlt\n");
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
		pr_info(TAG "Kanal 7 (WLAN-Befehle) wird als sblock bereitgestellt\n");
	if (datenkanaele)
		pr_info(TAG "Kanal 8 und 9 (WLAN-Daten) werden als sblock bereitgestellt (nur Empfang)\n");
	if (wlan0) {
		ret = wl0_anmelden();
		if (ret)
			pr_warn(TAG "wlan0 nicht angemeldet: %d%s\n", ret,
				WL_CFG80211 ? "" : " (Kernel ohne cfg80211)");
	}
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
