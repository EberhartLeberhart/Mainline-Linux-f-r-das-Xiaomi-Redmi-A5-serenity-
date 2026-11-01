// SPDX-License-Identifier: GPL-2.0
/*
 * wcn_ini_bin - wifi_board_config.ini (Geraetedatei aus odm:/firmware) in das Binaerformat umwandeln, das
 * Otto Pfluegers WLAN-Treiber sc23xx_wlan_sipc als sprd/wifi_board_config.bin erwartet.
 *
 * Hintergrund (01.11.2026): sc23xx_wlan_sipc laedt beim Start "sprd/wifi_board_config.bin" und schickt sie
 * abschnittsweise mit WIFI_CMD_DOWNLOAD_INI (sc23xx_load_config()). Format je Abschnitt:
 *   u16 le 0x4353 ("SC"), u16 le Laenge, Nutzdaten   (Laenge 0 = Abschnitt ueberspringen)
 * Die Nutzdaten erzeugt hier derselbe Parser, mit dem wcn_lauscher.c am 20.10. die INI erfolgreich geschickt hat
 * (wcn_ini.h, aus Realme rf_marlin3.c): Abschnitt 1 = wifi_conf_sec1_t, 2 = wifi_conf_sec2_t, 3 = rf_data,
 * 4 = wifi_config_param_t, jeweils mit angehaengtem CRC16 wie beim Lauscher (crc=0 laesst ihn weg).
 *
 * Nutzung am Handy (die INI liegt schon in /lib/firmware, sie und das Ergebnis bleiben auf dem Handy):
 *   insmod wcn_ini_bin.ko && cat /sys/kernel/debug/wcn_ini_bin > /lib/firmware/sprd/wifi_board_config.bin
 *   rmmod wcn_ini_bin
 * Das Modul spricht keine Hardware an, es liest nur die Datei und rechnet.
 */
#include <linux/debugfs.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/unaligned.h>

#include "wcn_ini.h"

#define TAG "wcn_ini_bin: "
#define INI_DATEI "wifi_board_config.ini"
#define SC_MAGIC 0x4353

static bool crc = true;
module_param(crc, bool, 0444);
MODULE_PARM_DESC(crc, "CRC16 an jeden Abschnitt haengen (wie wcn_lauscher, Standard 1)");

static unsigned int abschnitte = 0xf;
module_param(abschnitte, uint, 0444);
MODULE_PARM_DESC(abschnitte, "Bitmaske der Abschnitte 1-4 (Bit 0 = Abschnitt 1, Standard 0xf = alle)");

static struct platform_device *pdev;
static struct debugfs_blob_wrapper blob;
static struct dentry *datei;

static size_t anhaengen(u8 *ziel, size_t pos, unsigned int nr, const void *daten, size_t len)
{
	size_t n = (abschnitte & BIT(nr - 1)) ? len + (crc ? 2 : 0) : 0;

	put_unaligned_le16(SC_MAGIC, ziel + pos);
	put_unaligned_le16(n, ziel + pos + 2);
	pos += 4;
	if (!n) {
		pr_info(TAG "Abschnitt %u: uebersprungen\n", nr);
		return pos;
	}
	memcpy(ziel + pos, daten, len);
	pos += len;
	if (crc) {
		put_unaligned_le16(ini_crc16(daten, len), ziel + pos);
		pos += 2;
	}
	pr_info(TAG "Abschnitt %u: %zu Bytes%s\n", nr, len, crc ? " + CRC16" : "");
	return pos;
}

static int __init wcn_ini_bin_init(void)
{
	const struct firmware *fw;
	struct wifi_conf_t *conf;
	size_t rf_len, max, pos = 0;
	int treffer, unbekannt, ret;
	u8 *buf;

	pdev = platform_device_register_simple("wcn_ini_bin", -1, NULL, 0);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	ret = request_firmware(&fw, INI_DATEI, &pdev->dev);
	if (ret) {
		pr_err(TAG "%s nicht gefunden (%d) - liegt sie in /lib/firmware?\n", INI_DATEI, ret);
		goto weg_pdev;
	}
	conf = kzalloc(sizeof(*conf), GFP_KERNEL);
	if (!conf) {
		ret = -ENOMEM;
		goto weg_fw;
	}
	treffer = ini_parse(fw->data, fw->size, conf, &unbekannt);
	pr_info(TAG "%s: %zu Bytes, %d Schluessel uebernommen, %d unbekannt, Version %u.%u\n", INI_DATEI,
		fw->size, treffer, unbekannt, conf->version.major, conf->version.minor);
	if (treffer <= 0) {
		ret = treffer < 0 ? treffer : -EINVAL;
		goto weg_conf;
	}

	rf_len = min_t(size_t, conf->rf_config.rf_data_len, sizeof(conf->rf_config.rf_data));
	max = 4 * (4 + 2) + sizeof(struct wifi_conf_sec1_t) + sizeof(struct wifi_conf_sec2_t) + rf_len +
	      sizeof(struct wifi_config_param_t);
	buf = kzalloc(max, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto weg_conf;
	}
	pos = anhaengen(buf, pos, 1, conf, sizeof(struct wifi_conf_sec1_t));
	pos = anhaengen(buf, pos, 2, &conf->tx_scale, sizeof(struct wifi_conf_sec2_t));
	pos = anhaengen(buf, pos, 3, conf->rf_config.rf_data, rf_len);
	pos = anhaengen(buf, pos, 4, &conf->wifi_param, sizeof(struct wifi_config_param_t));

	blob.data = buf;
	blob.size = pos;
	datei = debugfs_create_blob("wcn_ini_bin", 0400, NULL, &blob);
	pr_info(TAG "fertig: %zu Bytes unter /sys/kernel/debug/wcn_ini_bin\n", pos);
	ret = 0;
weg_conf:
	kfree(conf);
weg_fw:
	release_firmware(fw);
	if (!ret)
		return 0;
weg_pdev:
	platform_device_unregister(pdev);
	return ret;
}

static void __exit wcn_ini_bin_exit(void)
{
	debugfs_remove(datei);
	kfree(blob.data);
	platform_device_unregister(pdev);
}

module_init(wcn_ini_bin_init);
module_exit(wcn_ini_bin_exit);
MODULE_DESCRIPTION("wifi_board_config.ini in sprd/wifi_board_config.bin (sc23xx_wlan_sipc) umwandeln");
MODULE_LICENSE("GPL");
