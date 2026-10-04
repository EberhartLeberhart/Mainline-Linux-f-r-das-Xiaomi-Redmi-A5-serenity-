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
  /gpio-keys/key-volumedown debounce-interval u:2
