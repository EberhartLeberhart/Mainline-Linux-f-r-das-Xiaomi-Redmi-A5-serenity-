#!/usr/bin/env python3
# patch_fgu_temp.py - Akkuanzeige SC27xx: Akkutemperatur ueber eine Spannungs-Temperatur-Tabelle
# aus dem Device-Tree berechnen (wie Xiaomis "voltage-temp-table").
#
# Ohne Patch meldet der Treiber den Rohwert des Temperaturfuehlers (Millivolt) als Temperatur:
# 357 mV -> "35,7 °C", obwohl es ~22 °C sind.
#
# Neue, optionale DT-Eigenschaft der Akkuanzeige:
#   sprd,voltage-temp-table = <uV code  uV code ...>;   code - 1000 = Temperatur in 0,1 °C
#   (genau Xiaomis Format: 1250 = 25,0 °C, 800 = -20,0 °C; Spannung faellt mit steigender Temperatur)
# Fehlt die Eigenschaft, bleibt alles wie bisher.
#
# Nutzung: python3 ~/redmi-tools/patch_fgu_temp.py      (im Baum ~/ums9230-linux, danach committen)
import os, re, sys
F = os.path.expanduser("~/ums9230-linux/drivers/power/supply/sc27xx_fuel_gauge.c")
s = open(F).read()
if "voltage-temp-table" in s:
    print("FEHLER: schon gepatcht"); sys.exit(1)

def ersetze(alt, neu, was):
    global s
    if s.count(alt) != 1:
        print(f"FEHLER: Stelle '{was}' {s.count(alt)}x gefunden statt 1x - Datei anders als erwartet"); sys.exit(1)
    s = s.replace(alt, neu)

# 1) Tabelle in der Treiber-Struktur (direkt nach der oeffnenden Klammer - unabhaengig vom Rest)
ersetze("struct sc27xx_fgu_data {\n",
        "struct sc27xx_fgu_data {\n"
        "\tu32 *vt_table;\t\t/* Paare: Mikrovolt, Temperatur-Code (0,1 Grad + 1000) */\n"
        "\tint vt_len;\t\t/* Anzahl der Paare */\n",
        "struct sc27xx_fgu_data {")

# 2) Umrechnung Spannung -> Temperatur (lineare Interpolation, Spannung faellt mit der Temperatur)
m = re.search(r"static int sc27xx_fgu_get_temp\(struct sc27xx_fgu_data \*data, int \*temp\)\s*\{[^{}]*?\}", s)
if not m or "iio_read_channel_processed" not in m.group(0):
    print("FEHLER: Funktion sc27xx_fgu_get_temp nicht in erwarteter Form gefunden:")
    print(m.group(0) if m else "(gar nicht gefunden)"); sys.exit(1)
ersetze(m.group(0),
"""static int sc27xx_fgu_get_temp(struct sc27xx_fgu_data *data, int *temp)
{
	const u32 *t = data->vt_table;
	int ret, mv, uv, i, n = data->vt_len;

	ret = iio_read_channel_processed(data->channel, &mv);
	if (ret < 0)
		return ret;
	if (!n) {
		*temp = mv;
		return ret;
	}

	uv = mv * 1000;
	if (uv >= (int)t[0]) {
		*temp = (int)t[1] - 1000;
		return 0;
	}
	if (uv <= (int)t[2 * (n - 1)]) {
		*temp = (int)t[2 * (n - 1) + 1] - 1000;
		return 0;
	}
	for (i = 1; i < n; i++) {
		int v0 = t[2 * (i - 1)], c0 = t[2 * (i - 1) + 1];
		int v1 = t[2 * i], c1 = t[2 * i + 1];

		if (uv >= v1) {
			*temp = c1 + (int)((s64)(uv - v1) * (c0 - c1) / (v0 - v1)) - 1000;
			return 0;
		}
	}
	return -EINVAL;
}""", "sc27xx_fgu_get_temp")

# 3) Tabelle beim Start einlesen und pruefen
ersetze("""	data->charge_chan = devm_iio_channel_get(dev, "charge-vol");""",
"""	ret = device_property_count_u32(dev, "sprd,voltage-temp-table");
	if (ret > 0) {
		int i;

		if (ret % 2 || ret < 4) {
			dev_err(dev, "sprd,voltage-temp-table: %d Werte, erwartet gerade Anzahl >= 4\\n", ret);
			return -EINVAL;
		}
		data->vt_len = ret / 2;
		data->vt_table = devm_kcalloc(dev, ret, sizeof(u32), GFP_KERNEL);
		if (!data->vt_table)
			return -ENOMEM;
		ret = device_property_read_u32_array(dev, "sprd,voltage-temp-table",
						     data->vt_table, ret);
		if (ret)
			return ret;
		for (i = 1; i < data->vt_len; i++) {
			if (data->vt_table[2 * i] >= data->vt_table[2 * (i - 1)]) {
				dev_err(dev, "sprd,voltage-temp-table: Spannung muss fallen (Punkt %d)\\n", i);
				return -EINVAL;
			}
		}
		dev_info(dev, "Akkutemperatur ueber Tabelle mit %d Punkten\\n", data->vt_len);
	}

	data->charge_chan = devm_iio_channel_get(dev, "charge-vol");""", "probe: charge-vol")

open(F, "w").write(s)
print(f">>> {F}: Temperatur-Tabelle eingebaut. Kontrolle: git -C ~/ums9230-linux diff --stat")
