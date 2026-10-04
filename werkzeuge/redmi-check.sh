#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# redmi-check.sh - prueft am laufenden Redmi A5, was WIRKLICH funktioniert.
# Grundsatz: Nichts gilt als "geht", nur weil es eingebaut ist. Jede Zeile braucht einen Beleg.
#
# Nutzung am PC:   ssh root@192.168.7.2 'bash -s' < ~/redmi-tools/redmi-check.sh | tee ~/redmi-build/check_$(date +%F_%H%M).txt
# Ergebnis:  OK = belegt funktionsfaehig, TEIL = laeuft eingeschraenkt, FEHLT = nicht da/kaputt, INFO = nur Angabe

ok=0; teil=0; fehlt=0
r() {  # r STATUS "Bereich" "Beleg"
    case $1 in OK) ok=$((ok+1));; TEIL) teil=$((teil+1));; FEHLT) fehlt=$((fehlt+1));; esac
    printf "%-6s %-24s %s\n" "$1" "$2" "$3"
}
bound() {  # bound treibername -> Anzahl gebundener Geraete
    ls /sys/bus/platform/drivers/"$1" 2>/dev/null | grep -c '^[0-9a-f]*\.'
}
mountpoint -q /sys/kernel/debug || mount -t debugfs none /sys/kernel/debug 2>/dev/null

echo "=== Redmi A5 Pruefung $(date '+%F %T') ==="
echo "Kernel:  $(uname -r)  $(uname -v)"
echo "Slot:    $(grep -ao 'slot_suffix=[^ ]*' /proc/device-tree/chosen/bootargs 2>/dev/null)"
echo "Laufzeit: $(uptime -p)   (Datum oben ist falsch, solange die Echtzeituhr fehlt)"
echo

echo "--- Grundsystem ---"
n=$(nproc --all); on=$(cat /sys/devices/system/cpu/online)
[ "$n" -eq 8 ] && r OK "CPU-Kerne" "$n Kerne, online: $on" || r TEIL "CPU-Kerne" "nur $n Kerne (erwartet 8), online: $on"
mem=$(awk '/MemTotal/{printf "%d", $2/1024}' /proc/meminfo)
r INFO "Arbeitsspeicher" "${mem} MB"
root=$(findmnt -no SOURCE,FSTYPE /)
case "$root" in *mmcblk*) r OK "Root-Dateisystem" "$root (eMMC)";; *) r TEIL "Root-Dateisystem" "$root";; esac
[ "$(bound sdhci_sprd_r11)" -ge 1 ] && r OK "eMMC-Treiber" "sdhci gebunden" || r FEHLT "eMMC-Treiber" "sdhci nicht gebunden"
if [ -d /sys/devices/system/cpu/cpufreq/policy0 ]; then
    # cpufreq ist nur mit gesperrtem Tiefschlaf der grossen Kerne stabil (Tests 04.10.)
    sp=$(cat /sys/devices/system/cpu/cpu6/cpuidle/state1/disable /sys/devices/system/cpu/cpu7/cpuidle/state1/disable 2>/dev/null | tr -d '\n')
    pol=$(ls -d /sys/devices/system/cpu/cpufreq/policy* | wc -l)
    frq="$(for p in /sys/devices/system/cpu/cpufreq/policy*; do printf '%s ' $(( $(cat $p/scaling_cur_freq)/1000 )); done)MHz"
    if [ "$sp" != "11" ]; then
        r FEHLT "CPU-Takt (cpufreq)" "!! laeuft OHNE Tiefschlaf-Sperre cpu6/7 ($sp) - Haenger droht"
    elif [ "$pol" -eq 2 ]; then
        r OK "CPU-Takt (cpufreq)" "2 Gruppen, jetzt $frq, Tiefschlaf cpu6/7 gesperrt"
    else
        r TEIL "CPU-Takt (cpufreq)" "$pol Gruppen statt 2, jetzt $frq"
    fi
else
    r FEHLT "CPU-Takt (cpufreq)" "kein cpufreq - CPUs laufen mit Bootloader-Takt"
fi

echo; echo "--- Verbindung ---"
udc=$(ls /sys/class/udc 2>/dev/null | head -1)
[ -n "$udc" ] && r OK "USB-Gadget" "UDC $udc, Rolle: $(cat /sys/class/usb_role/*/role 2>/dev/null | head -1)" || r FEHLT "USB-Gadget" "kein UDC"
ip -br addr show usb0 2>/dev/null | grep -q 192.168.7.2 && r OK "USB-Netz" "usb0 192.168.7.2" || r FEHLT "USB-Netz" "usb0 ohne 192.168.7.2"
systemctl is-active -q ssh 2>/dev/null && r OK "SSH" "aktiv (sonst liefe dieses Skript nicht)" || r TEIL "SSH" "Dienst nicht aktiv?"
ping -c1 -W2 1.1.1.1 >/dev/null 2>&1 && r OK "Internet ueber PC" "1.1.1.1 erreichbar" || r TEIL "Internet ueber PC" "kein Ping (net.sh/NAT am PC?)"
[ -e /dev/ttyGS0 ] && r OK "Serielle Konsole USB" "/dev/ttyGS0" || r FEHLT "Serielle Konsole USB" "kein ttyGS0"
# echte Geraete heissen 1-1, 1-1.2 ... ; 1-0:1.0 ist nur die Root-Hub-Schnittstelle
dev=$(ls /sys/bus/usb/devices/ 2>/dev/null | grep -E '^[0-9]+-[1-9][0-9.]*$')
if [ -n "$dev" ]; then
    r OK "USB-Host-Modus" "angeschlossenes Geraet erkannt: $(echo $dev)"
elif ls /sys/bus/usb/devices/ 2>/dev/null | grep -q '^usb[0-9]'; then
    r TEIL "USB-Host-Modus" "Host-Bus angelegt, aber nie mit Geraet getestet (Rolle jetzt: device)"
else r FEHLT "USB-Host-Modus" "kein USB-Host-Bus"; fi

echo; echo "--- Stabilitaet / Strom ---"
[ "$(bound sprd-wdt)" -ge 1 ] && r OK "Watchdog (AP)" "sprd-wdt gebunden" || r FEHLT "Watchdog (AP)" "nicht gebunden"
dmesg | grep -q "REDMI: rst_mode beim Start" && r OK "rst_mode-Fix" "Code laeuft ($(dmesg | grep -o 'rst_mode beim Start: 0x[0-9a-f]*' | head -1), setzt 0x40)" \
    || r FEHLT "rst_mode-Fix" "Meldung fehlt im dmesg - Fix nicht im Kernel?"
if dmesg | grep -q "REDMI: Poweroff-Handler SC2730 angemeldet"; then
    r TEIL "Ausschalten" "SC2730-Handler angemeldet - echtes Aus nur von Hand belegbar (poweroff, 3 Min warten)"
else
    r FEHLT "Ausschalten" "kein Poweroff-Handler: poweroff haelt nur an, Watchdog startet neu"
fi
if ls /sys/class/rtc/rtc0 >/dev/null 2>&1; then r OK "Echtzeituhr" "rtc0: $(cat /sys/class/rtc/rtc0/date) $(cat /sys/class/rtc/rtc0/time)"
else r FEHLT "Echtzeituhr" "kein rtc0 - Uhrzeit nach Neustart falsch"; fi
ps=$(ls /sys/class/power_supply 2>/dev/null | tr '\n' ' ')
[ -n "$ps" ] && r TEIL "Akku/Laden" "power_supply: $ps" || r FEHLT "Akku/Laden" "kein power_supply - Akkustand unbekannt"
pdl=$(ls /sys/kernel/debug/pm_genpd 2>/dev/null | grep -v '^pm_genpd_summary$')
pd=$(echo -n "$pdl" | grep -c .)
[ "$pd" -gt 0 ] && r OK "Power-Domains" "$pd Domains: $(echo $pdl)" || r FEHLT "Power-Domains" "keine genpd-Domains (Uebersichtsdatei allein zaehlt nicht)"

echo; echo "--- Temperatur ---"
nz=$(ls -d /sys/class/thermal/thermal_zone* 2>/dev/null | wc -l)
if [ "$nz" -gt 0 ]; then
    t=$(for z in /sys/class/thermal/thermal_zone*; do echo $(( $(cat $z/temp) / 1000 )); done | sort -n)
    lo=$(echo "$t" | head -1); hi=$(echo "$t" | tail -1)
    if [ "$lo" -ge 5 ] && [ "$hi" -le 90 ]; then r OK "Temperatursensoren" "$nz Zonen, $lo..$hi C"
    else r TEIL "Temperatursensoren" "$nz Zonen, $lo..$hi C - Werte unplausibel?"; fi
    trips=$(ls /sys/class/thermal/thermal_zone*/trip_point_*_type 2>/dev/null | wc -l)
    crit=$(cat /sys/class/thermal/thermal_zone*/trip_point_*_type 2>/dev/null | grep -c critical)
    [ "$crit" -gt 0 ] && r OK "Ueberhitzungsschutz" "$crit critical-Grenzen" || r FEHLT "Ueberhitzungsschutz" "$trips Grenzen, keine critical"
else
    r FEHLT "Temperatursensoren" "keine thermal_zone (nvmem-rmem + sprd_thermal geladen?)"
    r FEHLT "Ueberhitzungsschutz" "ohne Sensoren nicht moeglich"
fi

echo; echo "--- Treiber gebunden (nicht nur eingebaut!) ---"
for d in sprd-adi sprd-gpio sprd-eic sprd-pinctrl sprd-mailbox sprd_hwspinlock sprd-usb2-phy musb-sprd \
         sprd-i2c sprd-dma sprd-thermal sprd-power-controller ums9230-clk sprd-pwm; do
    n=$(bound $d)
    nf=$(dmesg | grep -E "with driver $d failed" | sed -E 's/.*\] ([^ ]+ )?([0-9a-f]+\.[^:]+):.*/\2/' | sort -u)
    if [ "$n" -gt 0 ] && [ -n "$nf" ]; then r TEIL "$d" "$n gebunden, gescheitert: $(echo $nf)"
    elif [ "$n" -gt 0 ]; then r OK "$d" "$n Geraet(e)"
    elif [ -d /sys/bus/platform/drivers/$d ]; then r FEHLT "$d" "Treiber da, aber nichts gebunden"
    else r FEHLT "$d" "Treiber nicht geladen/eingebaut"; fi
done

echo; echo "--- Eingabe / Anzeige ---"
grep -qi "power\|eic\|gpio-keys" /proc/bus/input/devices 2>/dev/null && r OK "Tasten" "$(grep -i '^N:' /proc/bus/input/devices | tr '\n' ' ')" \
    || r FEHLT "Tasten" "keine Tasten-Eingabegeraete"
[ -e /dev/fb0 ] && r OK "Bildschirm (Framebuffer)" "$(cat /sys/class/graphics/fb0/name) $(cat /sys/class/graphics/fb0/virtual_size)" || r FEHLT "Bildschirm (Framebuffer)" "kein fb0"
ls /sys/class/backlight/* >/dev/null 2>&1 && r OK "Helligkeit" "$(ls /sys/class/backlight)" || r FEHLT "Helligkeit" "kein Backlight-Geraet"
drv=$(basename "$(readlink /sys/class/drm/card0/device/driver 2>/dev/null)")
if [ -z "$drv" ]; then r FEHLT "DRM-Grafik" "kein DRM-Geraet"
elif [ "$drv" = "simple-framebuffer" ] || grep -q simpledrm /sys/class/graphics/fb0/name 2>/dev/null; then
    r TEIL "DRM-Grafik" "nur simpledrm (Bild vom Bootloader), kein echter Display-Treiber"
else r OK "DRM-Grafik" "Treiber: $drv"; fi

echo; echo "--- Funk ---"
ls /sys/class/net | grep -q '^wl' && r OK "WLAN" "$(ls /sys/class/net | grep '^wl')" || r FEHLT "WLAN" "kein wlan-Interface"
ls /sys/class/bluetooth/hci* >/dev/null 2>&1 && r OK "Bluetooth" "hci da" || r FEHLT "Bluetooth" "kein hci"
ls /dev/radio* >/dev/null 2>&1 && r OK "FM-Radio" "$(ls /dev/radio*)" || r FEHLT "FM-Radio" "kein /dev/radio"
ls /dev/gnss* >/dev/null 2>&1 && r OK "GPS" "gnss da" || r FEHLT "GPS" "kein /dev/gnss"
ls /dev/wwan* /dev/cdc-wdm* >/dev/null 2>&1 && r OK "Mobilfunk" "Modem-Geraet da" || r FEHLT "Mobilfunk" "kein Modem-Geraet"

echo; echo "--- Auffaelligkeiten im Kernel-Log ---"
dd=$(cat /sys/kernel/debug/devices_deferred 2>/dev/null)
if [ -n "$dd" ]; then echo "Wartende Geraete (deferred):"; echo "$dd" | sed 's/^/   /'; fi
echo "Fehlgeschlagene Probes:"
dmesg | grep -E "probe.*failed|failed with error" | sed 's/^\[ *[0-9.]*\] /   /' | sort -u
echo "Anzahl Fehler/Warnungen im dmesg: $(dmesg --level=err,warn 2>/dev/null | wc -l)"

echo
echo "=== Ergebnis: OK=$ok  TEIL=$teil  FEHLT=$fehlt ==="
