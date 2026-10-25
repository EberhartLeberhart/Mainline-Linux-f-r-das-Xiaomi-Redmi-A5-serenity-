#!/usr/bin/env python3
# redmi-vibratest.py - Vibrationsmotor des Redmi A5 kurz anwerfen (Force-Feedback-Schnittstelle, wie bei Gamepads).
# Treiber: sc27xx-vibra (CONFIG_INPUT_SC27XX_VIBRA), Eingabegeraet "sc27xx:vibrator".
# Nutzung (als root auf dem Handy): python3 redmi-vibratest.py [Dauer_ms]   (Standard 1000, hoechstens 5000)
import fcntl, os, re, struct, sys, time

dauer = min(int(sys.argv[1]) if len(sys.argv) > 1 else 1000, 5000)

# Eingabegeraet ueber den Namen finden
geraet = None
with open("/proc/bus/input/devices") as f:
    for block in f.read().split("\n\n"):
        if "vibra" in block.lower():
            # Zeile z. B. "H: Handlers=event2" -> event2
            treffer = re.search(r"\bevent\d+", block)
            if treffer:
                geraet = "/dev/input/" + treffer.group(0)
if not geraet:
    sys.exit("FEHLER: kein Vibrationsgeraet in /proc/bus/input/devices (Treiber geladen?)")
print("Geraet:", geraet)

# struct ff_effect (arm64, 48 Byte): type, id, direction, trigger{button,interval}, replay{length,delay},
# 2 Byte Ausrichtung, union{rumble: strong_magnitude, weak_magnitude}
FF_RUMBLE, EV_FF = 0x50, 0x15
EVIOCSFF = 0x40304580                          # _IOW('E', 0x80, struct ff_effect), Groesse 0x30
effekt = bytearray(struct.pack("<HhHHHHHxxHH", FF_RUMBLE, -1, 0, 0, 0, dauer, 0, 0xC000, 0xC000).ljust(48, b"\0"))

fd = os.open(geraet, os.O_RDWR)
try:
    fcntl.ioctl(fd, EVIOCSFF, effekt)
    nr = struct.unpack_from("<h", effekt, 2)[0]
    # struct input_event (arm64, 24 Byte): timeval (16), type, code, value -> Effekt nr einmal abspielen
    os.write(fd, struct.pack("<qqHHi", 0, 0, EV_FF, nr, 1))
    print("Motor an fuer %d ms (Effekt %d)" % (dauer, nr))
    time.sleep(dauer / 1000 + 0.3)
finally:
    os.close(fd)
print("fertig")
