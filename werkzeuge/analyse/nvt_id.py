#!/usr/bin/env python3
# nvt_id.py - Kennung des Novatek-Touch-Controllers (TDDI) ueber spidev lesen. Nur lesen:
# geschrieben wird ausschliesslich das Seitenregister (0xFF), das bei Novatek die Leseadresse waehlt.
# Kein Reset, keine Firmware, nichts was den Chip veraendert.
#
#   python3 nvt_id.py [/dev/spidev3.0] [hz]
#
# Protokoll wie im GPL-Treiber nt36xxx (nt36xxx_spi):
#   Schreiben: erstes Byte | 0x80        Lesen: erstes Byte & 0x7F, dann 1 Blindbyte, Daten ab Byte 2
#   Seite waehlen: [0xFF, (addr>>15)&0xFF, (addr>>7)&0xFF], danach Lesen ab (addr & 0x7F)
#   Kennung ("chip_ver_trim") liegt bei 0x3F004 (neuere Chips) oder 0x1F64E (aeltere) - 6 Bytes
import array, fcntl, os, struct, sys   # kein ctypes: fehlt in python3-minimal

DEV = sys.argv[1] if len(sys.argv) > 1 else "/dev/spidev3.0"
HZ = int(sys.argv[2]) if len(sys.argv) > 2 else 1000000
SPI_IOC_MESSAGE_1 = 0x40206B00          # _IOW('k', 0, struct spi_ioc_transfer[1]), 32 Bytes

fd = os.open(DEV, os.O_RDWR)

def xfer(data):
    n = len(data)
    tx = array.array("B", bytes(data))
    rx = array.array("B", bytes(n))
    # tx_buf, rx_buf (u64), len, speed_hz (u32), delay_usecs (u16), bits, cs_change, tx_nbits, rx_nbits, word_delay, pad
    msg = struct.pack("QQIIHBBBBBB", tx.buffer_info()[0], rx.buffer_info()[0], n, HZ, 0, 8, 0, 0, 0, 0, 0)
    fcntl.ioctl(fd, SPI_IOC_MESSAGE_1, msg)
    return rx.tobytes()

def set_page(addr):
    xfer([0xFF, (addr >> 15) & 0xFF, (addr >> 7) & 0xFF])      # 0xFF | 0x80 bleibt 0xFF

def read(addr, n):
    set_page(addr)
    r = xfer([addr & 0x7F] + [0] * (n + 1))                    # Befehl + Blindbyte + n Daten
    return r[2:2 + n]

def hx(b):
    return " ".join("%02x" % x for x in b)

print("Geraet %s, %d Hz" % (DEV, HZ))
for name, addr in (("chip_ver_trim (neu) 0x3F004", 0x3F004), ("chip_ver_trim (alt) 0x1F64E", 0x1F64E)):
    a = read(addr, 6); b = read(addr, 6)
    gleich = "gleich" if a == b else "UNGLEICH (Leitung/Takt?)"
    print("%-28s %s   (2. Lesung %s)" % (name, hx(a), gleich))
r = read(0x3F004, 6)
if all(x == 0xFF for x in r) or all(x == 0x00 for x in r):
    print("=> nur %02x: keine Antwort. Moeglich: Chip im Reset/aus, falscher Bus/CS, Takt zu hoch" % r[0])
else:
    print("=> Antwort erhalten. die Bytes vergleichen wir mit der Kennungstabelle des Novatek-Treibers")
os.close(fd)
