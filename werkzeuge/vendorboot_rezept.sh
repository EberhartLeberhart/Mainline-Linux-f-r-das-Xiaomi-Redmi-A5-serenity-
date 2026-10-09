#!/bin/bash
# vendorboot_rezept.sh - ALLE unsere Device-Tree-Aenderungen an einer Stelle.
# Baut aus dem Codeberg-Stand (serenity_v12.dtb) das aktuelle vendor_boot.
# Neue Aenderungen kommen HIER dazu (mit Datum und Beleg), nie nur auf der Kommandozeile.
#
#   ~/redmi-build/vendorboot_rezept.sh            -> ~/redmi-build/vendor_boot_rezept.img
#   danach: ~/redmi-build/deploy.sh vendor_boot_rezept.img
set -e
GP=/soc/gpio@641b0000      # GPIO-Baustein des Chips, hatte keine phandle
GP_PH=105                  # frei gewaehlt (hoechste vorhandene phandle war 104)
FG=/soc/spi@64200000/pmic@0/fuel-gauge@c00   # Akkuanzeige im PMIC
BAT_PH=106                 # phandle fuer den neuen Akku-Knoten
# Schalter: AKKU=0 baut den Stand ohne Akkuanzeige (vor dem 04.10.-Akku-Schritt)
AKKU=${AKKU:-1}
AKKU_ARGS=()
if [ "$AKKU" = 1 ]; then AKKU_ARGS=(
  `# 04.10.: Akku-Profil "bat", Alterungsstufe 0 aus Xiaomis dtbo (bat.id=0, charge.total_mah=5080000)` \
  /battery compatible simple-battery \
  /battery phandle u:$BAT_PH \
  /battery charge-full-design-microamp-hours u:0x4f5880 \
  /battery charge-full-microamp-hours u:0x4d83c0 \
  /battery precharge-current-microamp u:0x3a980 \
  /battery charge-term-current-microamp u:0x3a980 \
  /battery constant-charge-voltage-max-microvolt u:0x43e6d0 \
  /battery factory-internal-resistance-micro-ohms u:0x1d4c0 \
  /battery voltage-min-design-microvolt u:0x34a490 \
  /battery ocv-capacity-celsius u:0x19 \
  /battery ocv-capacity-table-0 "u:0x42ecd0 0x64 0x41bc20 0x5f 0x40c9f0 0x5a 0x3ff318 0x55 0x3f2028 0x50 0x3e5508 0x4b 0x3d91b8 0x46 0x3cda20 0x41 0x3c39f8 0x3c 0x3b8e18 0x37 0x3ac6e0 0x32 0x3a5d68 0x2d 0x3a1330 0x28 0x39d898 0x23 0x39b570 0x1e 0x398e60 0x19 0x394bf8 0x14 0x38f608 0x0f 0x386968 0x0a 0x3782f0 0x05 0x33e140 0x00" \
  `# 04.10.: Akkuanzeige einschalten, Messwiderstand wie Xiaomi (sprd,calib-resistance-micro-ohms = 0x12f2)` \
  $FG monitored-battery u:$BAT_PH \
  $FG sprd,calib-resistance-micro-ohms u:0x12f2 \
  `# 04.10.: Akkutemperatur-Tabelle von Xiaomi (voltage-temp-table, alle Profile gleich) - braucht patch_fgu_temp.py` \
  $FG sprd,voltage-temp-table "u:0x10e55d 0x320 0xf3999 0x352 0xd93d4 0x384 0xbfeb0 0x3b6 0xa81e2 0x3e8 0x92450 0x41a 0x7e8fb 0x44c 0x6d0fd 0x47e 0x5da8e 0x4b0 0x504f3 0x4e2 0x44ca5 0x514 0x3ae94 0x546 0x327c3 0x578 0x2b4e6 0x5aa 0x2534b 0x5dc 0x20079 0x60e 0x1ba11 0x640 0x17e40 0x672" \
  $FG status okay )
fi

~/redmi-build/vendorboot_dt.sh rezept \
  `# 03./04.10.: cpufreq einschalten` \
  /cpufreq status okay \
  `# 04.10.: grosse Kerne ueber PMIC-Regler 0 (power.from.extern=0, Xiaomi pmic-type-v2=0) - sonst Haenger` \
  /cpufreq/cluster@1 sprd,pmic-type u:0 \
  `# 04.10.: Lautstaerketasten wie im Xiaomi-Overlay (Lauter: PMIC-EIC 4, Leiser: GPIO 124 invertiert)` \
  $GP phandle u:$GP_PH \
  /gpio-keys/key-volumeup label "Volume Up Key" \
  /gpio-keys/key-volumeup linux,code u:115 \
  /gpio-keys/key-volumeup gpios "u:0x38 4 0" \
  /gpio-keys/key-volumeup debounce-interval u:2 \
  /gpio-keys/key-volumedown label "Volume Down Key" \
  /gpio-keys/key-volumedown linux,code u:114 \
  /gpio-keys/key-volumedown gpios "u:$GP_PH 124 1" \
  /gpio-keys/key-volumedown debounce-interval u:2 \
  `# 04.10.: I2C-Bus 2 (Xiaomi-Alias i2c2) fuer den Ladechip - nur der Bus, noch kein Chip-Knoten` \
  /soc/i2c@200f0000 status okay \
  `# 04.10.: i2c-sprd braucht einen Alias als Busnummer, sonst WARNING in i2c_add_numbered_adapter und kein Bus` \
  /aliases i2c2 /soc/i2c@200f0000 \
  `# 09.10.: SPI-Bus 3 fuer den Touch (Xiaomi: spi3 = spi@20150000, Alias spi3). Knoten+Takte waren schon da, nur disabled` \
  /soc/spi@20150000 status okay \
  /soc/spi@20150000 "#address-cells" u:1 \
  /soc/spi@20150000 "#size-cells" u:0 \
  /aliases spi3 /soc/spi@20150000 \
  `# 09.10.: Touch NT36528 (Kennung 0a 00 00 28 65 03 per spidev gelesen) an CS 0. Treiber nt36xxx_spi (George Chan, +NT36528):` \
  `#   max. 5 MHz im Treiber -> 4 MHz; IRQ an ap_gpio 144 wie Xiaomi. KEIN reset-gpios: der Treiber haelt die Leitung sonst dauerhaft LOW` \
  /soc/spi@20150000/touchscreen@0 compatible "novatek,nt36528-spi" \
  /soc/spi@20150000/touchscreen@0 reg u:0 \
  /soc/spi@20150000/touchscreen@0 spi-max-frequency u:4000000 \
  /soc/spi@20150000/touchscreen@0 irq-gpios "u:$GP_PH 144 0" \
  /soc/spi@20150000/touchscreen@0 touchscreen-size-x u:720 \
  /soc/spi@20150000/touchscreen@0 touchscreen-size-y u:1640 \
  "${AKKU_ARGS[@]}"
