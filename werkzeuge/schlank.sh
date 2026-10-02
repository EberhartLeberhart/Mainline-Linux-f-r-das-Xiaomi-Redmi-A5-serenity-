#!/bin/bash
# schlank.sh - alle ARM64-Plattformen ausser Unisoc (ARCH_SPRD) abschalten, damit der Kernel
# kleiner wird als der Android-Kernel (Platzbedarf 48103424 Bytes, endet unter 0x83000000).
# Nutzung: ~/redmi-build/schlank.sh   (im Kernel-Baum ~/ums9230-linux)
set -e
cd ~/ums9230-linux
cp .config ~/redmi-build/config_vor_schlank
echo ">>> Sicherung: ~/redmi-build/config_vor_schlank"

plat=$(grep -oP '^config \KARCH_\w+' arch/arm64/Kconfig.platforms | grep -vx ARCH_SPRD)
n=0
for p in $plat; do
    if grep -q "^CONFIG_$p=y" .config; then
        scripts/config --disable "$p"; n=$((n+1))
    fi
done
echo ">>> $n Plattformen abgeschaltet, ARCH_SPRD bleibt"
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig >/dev/null

grep -q '^CONFIG_ARCH_SPRD=y' .config || { echo "FEHLER: ARCH_SPRD weg!"; exit 1; }
echo ">>> eingebaute Optionen (=y) vorher: $(grep -c '=y$' ~/redmi-build/config_vor_schlank)  nachher: $(grep -c '=y$' .config)"
echo ">>> Wichtige Redmi-Optionen (muessen noch da sein):"
for o in ARCH_SPRD SPRD_ADI SPI_SPRD_ADI MMC_SDHCI_SPRD USB_MUSB_SPRD PHY_SPRD_USB2 SPRD_WATCHDOG \
         SPRD_TIMER COMMON_CLK_SPRD SPRD_UMS9230_CLK SERIAL_SPRD DRM_SIMPLEDRM SYSFB_SIMPLEFB \
         USB_CONFIGFS USB_CONFIGFS_NCM USB_CONFIGFS_ACM EXT4_FS IKCONFIG_PROC CMDLINE_FORCE; do
    v=$(grep -E "^CONFIG_$o=|^# CONFIG_$o is not set" .config | head -1)
    v0=$(grep -E "^CONFIG_$o=|^# CONFIG_$o is not set" ~/redmi-build/config_vor_schlank | head -1)
    [ "$v" = "$v0" ] && m="  " || m="!!"
    printf "  %s %-28s %s\n" "$m" "$o" "${v:-(unbekannt)}"
done
echo ">>> Zeilen mit !! haben sich geaendert - bitte pruefen, bevor gebaut wird."
