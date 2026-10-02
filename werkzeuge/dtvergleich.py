#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
dtvergleich - Android-Device-Tree gegen Mainline-Kernel vergleichen

Wirft man den Device-Tree des laufenden Android (Dump) und den eigenen
Kernel (Quellcode + Device-Tree) hinein, zeigt das Werkzeug:

  * welche Bausteine Android benutzt, die im eigenen Kernel fehlen,
  * ob es dafür schon einen Mainline-Treiber gibt (und ob er eingebaut ist),
  * wo sich Takte, Spannungen, GPIOs, Interrupts usw. unterscheiden,
  * welche reservierten Speicherbereiche fehlen.

Ergebnis: Kurzfassung im Terminal + HTML-Bericht (Übersicht und
aufklappbares Nachschlagewerk pro Baustein).

Aufruf (Beispiel Redmi A5):
  python3 dtvergleich.py \\
      --android ~/redmi-build/live.dts \\
      --linux   ~/ums9230-linux/arch/arm64/boot/dts/sprd/ums9230-xiaomi-serenity.dts \\
      --kernel  ~/ums9230-linux \\
      -o bericht.html

Eingaben für --android / --linux: .dts (Text), .dtb, Android-DT-Tabelle
(d7b7ab1e) oder ein ganzes Image (z. B. vendor_boot.img, dtb.img), aus dem
der erste Device-Tree herausgesucht wird.
Benötigt: python3, dtc; für Kernel-.dts mit #include ausserdem cpp.
"""

import argparse
import difflib
import html
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from collections import defaultdict

VERSION = "1.0"

FDT_MAGIC = 0xD00DFEED
DTTABLE_MAGIC = 0xD7B7AB1E


# ---------------------------------------------------------------------------
#  Device-Tree laden
# ---------------------------------------------------------------------------

class Node:
    __slots__ = ("name", "parent", "children", "props", "path", "depth")

    def __init__(self, name, parent):
        self.name = name
        self.parent = parent
        self.children = []
        self.props = {}
        if parent is None:
            self.path = "/"
            self.depth = 0
        else:
            self.path = (parent.path.rstrip("/") + "/" + name)
            self.depth = parent.depth + 1

    @property
    def basename(self):
        return self.name.split("@", 1)[0]

    @property
    def unit(self):
        if "@" not in self.name:
            return None
        u = self.name.split("@", 1)[1].lower()
        if u.startswith("0x"):
            u = u[2:]
        return u.lstrip("0") or "0"

    def strs(self, prop):
        v = self.props.get(prop)
        if v is None:
            return []
        return [s.decode("latin-1") for s in v.rstrip(b"\0").split(b"\0")] if v else []

    def str(self, prop):
        s = self.strs(prop)
        return s[0] if s else None

    def cells(self, prop):
        v = self.props.get(prop)
        if v is None or len(v) % 4:
            return None
        return list(struct.unpack(">%dI" % (len(v) // 4), v))

    def u32(self, prop, default=None):
        c = self.cells(prop)
        return c[0] if c else default

    @property
    def compatible(self):
        return self.strs("compatible")


def parse_fdt(blob):
    (magic, totalsize, off_struct, off_strings, _rsv, _ver, _last,
     _cpu, size_strings, size_struct) = struct.unpack(">10I", blob[:40])
    if magic != FDT_MAGIC:
        raise ValueError("kein FDT")
    strings = blob[off_strings:off_strings + size_strings]
    pos = off_struct
    end = off_struct + size_struct
    root = None
    cur = None
    while pos < end:
        tok = struct.unpack(">I", blob[pos:pos + 4])[0]
        pos += 4
        if tok == 1:  # BEGIN_NODE
            e = blob.index(b"\0", pos)
            name = blob[pos:e].decode("latin-1")
            pos = (e + 4) & ~3
            n = Node(name if cur is not None else "", cur)
            if cur is None:
                root = n
            else:
                cur.children.append(n)
            cur = n
        elif tok == 2:  # END_NODE
            cur = cur.parent
        elif tok == 3:  # PROP
            ln, nameoff = struct.unpack(">II", blob[pos:pos + 8])
            pos += 8
            e = strings.index(b"\0", nameoff)
            pname = strings[nameoff:e].decode("latin-1")
            cur.props[pname] = bytes(blob[pos:pos + ln])
            pos = (pos + ln + 3) & ~3
        elif tok == 4:  # NOP
            continue
        elif tok == 9:  # END
            break
        else:
            raise ValueError("kaputter FDT (Token %d)" % tok)
    return root


def extract_fdt(data):
    """FDT aus rohem DTB, Android-DT-Tabelle oder beliebigem Image holen."""
    if len(data) >= 4:
        magic = struct.unpack(">I", data[:4])[0]
        if magic == FDT_MAGIC:
            size = struct.unpack(">I", data[4:8])[0]
            return data[:size]
        if magic == DTTABLE_MAGIC:
            # dt_table_header: magic,total,header_size,entry_size,count,entries_off,...
            _m, _t, _hs, esize, count, eoff = struct.unpack(">6I", data[:24])
            for i in range(count):
                dt_size, dt_off = struct.unpack(">II", data[eoff + i * esize:eoff + i * esize + 8])
                blob = data[dt_off:dt_off + dt_size]
                if blob[:4] == b"\xd0\x0d\xfe\xed":
                    return blob
    pos = 0
    while True:
        pos = data.find(b"\xd0\x0d\xfe\xed", pos)
        if pos < 0:
            return None
        size = struct.unpack(">I", data[pos + 4:pos + 8])[0]
        if 0x100 < size <= len(data) - pos:
            return data[pos:pos + size]
        pos += 4


def run(cmd, **kw):
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kw)
    return r.returncode, r.stdout, r.stderr


def dts_to_dtb(path, kernel, tmp):
    text = open(path, encoding="utf-8", errors="replace").read()
    src = path
    use_symbols = False
    if not re.search(r"^\s*#include", text, re.M) and re.search(r"\\0[0-9]", text):
        # dtc schreibt Zeichenkettenlisten als "a\0b". Beginnt b mit einer Ziffer
        # ("10bit\010bit"), liest dtc beim Zurueckübersetzen ein Oktalzeichen.
        fixed = os.path.join(tmp, os.path.basename(path) + ".fix.dts")
        with open(fixed, "w", encoding="utf-8") as f:
            f.write(re.sub(r"\\0(?=[0-9])", r"\\000", text))
        src = path = fixed
    if re.search(r"^\s*#include", text, re.M):
        if not kernel:
            sys.exit("FEHLER: %s enthält #include - bitte --kernel angeben" % path)
        if not shutil.which("cpp"):
            sys.exit("FEHLER: cpp nicht gefunden (sudo apt install cpp)")
        inc = [os.path.join(kernel, "include"),
               os.path.join(kernel, "scripts/dtc/include-prefixes"),
               os.path.dirname(os.path.abspath(path)),
               os.path.join(kernel, "arch/arm64/boot/dts")]
        pre = os.path.join(tmp, os.path.basename(path) + ".pp")
        cmd = ["cpp", "-nostdinc", "-undef", "-D__DTS__", "-x", "assembler-with-cpp"]
        for i in inc:
            cmd += ["-I", i]
        cmd += [path, "-o", pre]
        rc, _o, e = run(cmd)
        if rc:
            sys.exit("FEHLER beim Vorverarbeiten von %s:\n%s" % (path, e.decode(errors="replace")))
        src = pre
        use_symbols = True
    out = os.path.join(tmp, os.path.basename(path) + ".dtb")
    cmd = ["dtc", "-q", "-f", "-I", "dts", "-O", "dtb", "-o", out,
           "-i", os.path.dirname(os.path.abspath(path))]
    if use_symbols:
        cmd.insert(1, "-@")
    cmd.append(src)
    rc, _o, e = run(cmd)
    if not os.path.exists(out):
        sys.exit("FEHLER: dtc konnte %s nicht übersetzen:\n%s" % (path, e.decode(errors="replace")))
    return open(out, "rb").read()


class Tree:
    def __init__(self, root, source):
        self.root = root
        self.source = source
        self.nodes = []
        self._walk(root)
        self.by_phandle = {}
        for n in self.nodes:
            ph = n.u32("phandle") or n.u32("linux,phandle")
            if ph:
                self.by_phandle[ph] = n
        self.by_path = {n.path: n for n in self.nodes}
        self.labels = {}
        sym = self.by_path.get("/__symbols__")
        if sym:
            for lab in sym.props:
                p = sym.str(lab)
                if p and p in self.by_path:
                    # erster (kuerzester) Label gewinnt
                    old = self.labels.get(p)
                    if old is None or len(lab) < len(old):
                        self.labels[p] = lab
        self._addr = {}
        self._en = {}

    def _walk(self, n):
        self.nodes.append(n)
        for c in n.children:
            self._walk(c)

    # -- Adressen -----------------------------------------------------------
    @staticmethod
    def acells(n):
        return n.u32("#address-cells", 2) if n is not None else 2

    @staticmethod
    def scells(n):
        return n.u32("#size-cells", 1) if n is not None else 1

    @staticmethod
    def _int(cells):
        v = 0
        for c in cells:
            v = (v << 32) | c
        return v

    def addr(self, n):
        """(CPU-Adresse, Größe) des ersten reg-Eintrags oder None."""
        if n in self._addr:
            return self._addr[n]
        res = None
        reg = n.cells("reg")
        p = n.parent
        if reg and p is not None:
            ac, sc = self.acells(p), self.scells(p)
            if ac and sc and len(reg) >= ac + sc:
                a = self._int(reg[:ac])
                size = self._int(reg[ac:ac + sc])
                cur = p
                ok = True
                while cur.parent is not None:
                    if "ranges" not in cur.props:
                        ok = False
                        break
                    r = cur.cells("ranges")
                    if r:
                        cac, csc = self.acells(cur), self.scells(cur)
                        pac = self.acells(cur.parent)
                        ent = cac + pac + csc
                        hit = False
                        for i in range(0, len(r) - ent + 1, ent):
                            ca = self._int(r[i:i + cac])
                            pa = self._int(r[i + cac:i + cac + pac])
                            sz = self._int(r[i + cac + pac:i + ent])
                            if ca <= a < ca + sz:
                                a = pa + (a - ca)
                                hit = True
                                break
                        if not hit:
                            ok = False
                            break
                    cur = cur.parent
                if ok:
                    res = (a, size)
        self._addr[n] = res
        return res

    def reg_list(self, n):
        reg = n.cells("reg")
        if not reg or n.parent is None:
            return []
        ac, sc = self.acells(n.parent), self.scells(n.parent)
        ent = ac + sc
        if not ent:
            return []
        out = []
        for i in range(0, len(reg) - ent + 1, ent):
            out.append((self._int(reg[i:i + ac]), self._int(reg[i + ac:i + ent]) if sc else 0))
        return out

    # -- Status -------------------------------------------------------------
    def enabled(self, n):
        if n in self._en:
            return self._en[n]
        st = n.str("status")
        own = st is None or st in ("okay", "ok")
        res = own and (n.parent is None or self.enabled(n.parent))
        self._en[n] = res
        return res

    # -- Namen --------------------------------------------------------------
    def label(self, n):
        return self.labels.get(n.path)

    def desc(self, n):
        if n is None:
            return "?"
        lab = self.label(n)
        rn = n.str("regulator-name")
        base = ("&" + lab) if lab else n.path
        if rn and rn not in base:
            base += " (" + rn + ")"
        return base

    def compat_owner(self, n):
        """Nächster Knoten (selbst oder Vorfahr) mit compatible."""
        while n is not None and n.parent is not None:
            if n.compatible:
                return n
            n = n.parent
        return None


def load_tree(path, kernel=None, label=""):
    path = os.path.expanduser(path)
    if not os.path.exists(path):
        sys.exit("FEHLER: Datei nicht gefunden: %s" % path)
    tmp = tempfile.mkdtemp(prefix="dtvergleich-")
    try:
        raw = open(path, "rb").read()
        blob = extract_fdt(raw)
        if blob is None:
            if not shutil.which("dtc"):
                sys.exit("FEHLER: dtc nicht gefunden (sudo apt install device-tree-compiler)")
            blob = dts_to_dtb(path, kernel, tmp)
        root = parse_fdt(blob)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return Tree(root, path)


# ---------------------------------------------------------------------------
#  Kernel: welche compatibles haben Treiber, welche CONFIG_ schaltet sie ein
# ---------------------------------------------------------------------------

COMPAT_RE = re.compile(rb'\.compatible\s*=\s*"([^"]+)"')
DECLARE_RE = re.compile(rb'_DECLARE\s*\(\s*\w+\s*,\s*"([^"]+)"')
SCAN_DIRS = ("drivers", "sound", "arch/arm64/kernel", "kernel")


class KernelIndex:
    def __init__(self, kdir, config_path=None):
        self.kdir = os.path.abspath(os.path.expanduser(kdir))
        self.compat = defaultdict(set)
        self._mk = {}
        self._symcache = {}
        t0 = time.time()
        nfiles = 0
        for d in SCAN_DIRS:
            top = os.path.join(self.kdir, d)
            if not os.path.isdir(top):
                continue
            for dp, _dn, fn in os.walk(top):
                for f in fn:
                    if not f.endswith(".c"):
                        continue
                    full = os.path.join(dp, f)
                    try:
                        data = open(full, "rb").read()
                    except OSError:
                        continue
                    nfiles += 1
                    if b"compatible" not in data and b"_DECLARE" not in data:
                        continue
                    rel = os.path.relpath(full, self.kdir)
                    for m in COMPAT_RE.finditer(data):
                        self.compat[m.group(1).decode("latin-1")].add(rel)
                    for m in DECLARE_RE.finditer(data):
                        self.compat[m.group(1).decode("latin-1")].add(rel)
        self.scan_time = time.time() - t0
        self.nfiles = nfiles
        self.config = {}
        self.config_path = None
        cp = config_path or os.path.join(self.kdir, ".config")
        if os.path.exists(cp):
            self.config_path = cp
            for line in open(cp, errors="replace"):
                m = re.match(r"(CONFIG_\w+)=(.*)", line)
                if m:
                    self.config[m.group(1)] = m.group(2).strip()
                m = re.match(r"# (CONFIG_\w+) is not set", line)
                if m:
                    self.config[m.group(1)] = "n"

    # -- Makefiles ----------------------------------------------------------
    def _makefile(self, reldir):
        if reldir in self._mk:
            return self._mk[reldir]
        entries = []
        for name in ("Makefile", "Kbuild"):
            p = os.path.join(self.kdir, reldir, name)
            if not os.path.exists(p):
                continue
            text = open(p, errors="replace").read().replace("\\\n", " ")
            for line in text.splitlines():
                line = line.split("#", 1)[0]
                m = re.match(r"\s*([\w\-\$\(\)]+)\s*(\+=|:=|=)\s*(.*)", line)
                if m:
                    entries.append((m.group(1), m.group(3).split()))
        self._mk[reldir] = entries
        return entries

    def _syms_for(self, reldir, obj, depth=0):
        """Liste der CONFIG_-Symbole, die für obj (z. B. 'foo.o' oder 'musb/') nötig sind.
        None = unbekannt."""
        key = (reldir, obj)
        if key in self._symcache:
            return self._symcache[key]
        if depth > 8:
            return None
        res = None
        for lhs, rhs in self._makefile(reldir):
            if obj not in rhs:
                continue
            m = re.match(r"obj-\$\((CONFIG_\w+)\)$", lhs)
            if m:
                parent = self._dir_syms(reldir, depth)
                res = [m.group(1)] + (parent or [])
                break
            if lhs == "obj-y":
                res = self._dir_syms(reldir, depth) or []
                break
            m = re.match(r"([\w\-]+)-(y|objs|\$\((CONFIG_\w+)\))$", lhs)
            if m and m.group(1) != "obj":
                sub = self._syms_for(reldir, m.group(1) + ".o", depth + 1)
                if sub is not None:
                    res = ([m.group(3)] if m.group(3) else []) + sub
                    break
        self._symcache[key] = res
        return res

    def _dir_syms(self, reldir, depth):
        parent = os.path.dirname(reldir)
        if not parent or reldir in SCAN_DIRS:
            return []
        return self._syms_for(parent, os.path.basename(reldir) + "/", depth + 1) or []

    def syms_for_file(self, rel):
        d, f = os.path.split(rel)
        return self._syms_for(d, f[:-2] + ".o")

    # -- Abfragen -----------------------------------------------------------
    def config_state(self, syms):
        """'y', 'm', 'n' (mit fehlenden Symbolen) oder '?'."""
        if syms is None:
            return "?", []
        if not self.config:
            return "?", []
        missing = [s for s in syms if self.config.get(s, "n") in ("n", "")]
        mod = [s for s in syms if self.config.get(s) == "m"]
        if missing:
            return "n", missing
        if mod:
            return "m", mod
        return "y", []

    def driver(self, compats):
        """Treiber für die erste passende compatible-Zeichenkette."""
        for c in compats:
            files = sorted(self.compat.get(c, ()))
            if files:
                best = None
                for f in files:
                    syms = self.syms_for_file(f)
                    st, lst = self.config_state(syms)
                    cand = {"compat": c, "file": f, "syms": syms, "state": st, "missing": lst}
                    rank = {"y": 0, "m": 1, "?": 2, "n": 3}[st]
                    if best is None or rank < best[0]:
                        best = (rank, cand)
                return best[1]
        return None

    def similar(self, compats, limit=3):
        """Ähnliche Treiber vorschlagen (gleicher Hersteller, gleiches Funktionswort)."""
        out = []
        for c in compats:
            if "," not in c:
                continue
            vendor, model = c.split(",", 1)
            words = {w for w in re.split(r"[-_]", model) if w and not re.search(r"\d", w) and len(w) > 2}
            m = re.match(r"[a-z]{2,}", model.lower())
            prefix = m.group(0) if m and not words else None
            if not words and not prefix:
                continue
            for kc, files in self.compat.items():
                if not kc.lower().startswith(vendor.lower() + ","):
                    continue
                kmodel = kc.split(",", 1)[1]
                kwords = set(re.split(r"[-_]", kmodel))
                if (words and words & kwords) or (prefix and kmodel.lower().startswith(prefix)):
                    out.append((kc, sorted(files)[0]))
        seen = set()
        res = []
        for kc, f in sorted(out, key=lambda x: (len(x[0]), x[0])):
            if kc not in seen:
                seen.add(kc)
                res.append((kc, f))
            if len(res) >= limit:
                break
        return res


# ---------------------------------------------------------------------------
#  Verweise (phandles) aufloesen
# ---------------------------------------------------------------------------

REF_CELLS = {
    "clocks": "#clock-cells", "assigned-clocks": "#clock-cells",
    "assigned-clock-parents": "#clock-cells", "resets": "#reset-cells",
    "power-domains": "#power-domain-cells", "phys": "#phy-cells",
    "pwms": "#pwm-cells", "iommus": "#iommu-cells", "dmas": "#dma-cells",
    "io-channels": "#io-channel-cells", "mboxes": "#mbox-cells",
    "hwlocks": "#hwlock-cells", "interrupts-extended": "#interrupt-cells",
    "thermal-sensors": "#thermal-sensor-cells", "sound-dai": "#sound-dai-cells",
}
REF_PLAIN = {"nvmem-cells", "memory-region", "extcon", "usb-phy", "interrupt-parent",
             "backlight", "monitored-battery", "remote-endpoint", "cpu", "lens-focus"}


def ref_kind(prop):
    if prop in REF_CELLS:
        return "cells", REF_CELLS[prop]
    if prop == "gpios" or prop.endswith("-gpios") or prop.endswith("-gpio"):
        return "cells", "#gpio-cells"
    if prop.endswith("-supply"):
        return "plain", None
    if prop in REF_PLAIN:
        return "plain", None
    if "syscon" in prop and "," in prop:
        return "first", None
    return None, None


def parse_refs(tree, node, prop):
    """Liste von (Zielknoten, Argumente) oder None, wenn prop kein Verweis ist."""
    kind, cname = ref_kind(prop)
    if kind is None:
        return None
    cells = node.cells(prop)
    if not cells:
        return []
    out = []
    if kind == "plain":
        for c in cells:
            out.append((tree.by_phandle.get(c), ()))
        return out
    if kind == "first":
        return [(tree.by_phandle.get(cells[0]), tuple(cells[1:]))]
    i = 0
    while i < len(cells):
        tgt = tree.by_phandle.get(cells[i])
        if tgt is None:
            out.append((None, tuple(cells[i:i + 1])))
            i += 1
            continue
        n = tgt.u32(cname, 0 if cname != "#interrupt-cells" else 3)
        out.append((tgt, tuple(cells[i + 1:i + 1 + n])))
        i += 1 + n
    return out


def fmt_ref(tree, ref):
    tgt, args = ref
    s = tree.desc(tgt)
    if args:
        s += " " + " ".join(str(a) if a < 1000 else hex(a) for a in args)
    return s


# ---------------------------------------------------------------------------
#  Bereiche (grobe Einordnung für die Übersicht)
# ---------------------------------------------------------------------------

AREAS = [
    ("Display", ("dpu", "dsi", "dphy", "panel", "backlight", "lcd", "display", "dispc")),
    ("GPU", ("gpu", "mali")),
    ("Kamera", ("camera", "isp", "dcam", "csi", "sensor", "camsys", "flash", "lens", "vcm")),
    ("Video", ("vsp", "video", "vpu", "jpg", "jpeg")),
    ("Audio", ("audio", "sound", "i2s", "codec", "dai", "vbc", "mcdt", "headset", "sia81", "aw8", "fs16", "amp")),
    ("USB", ("usb", "musb", "otg", "typec", "tcpc", "extcon", "hsphy", "ssphy")),
    ("Speicher", ("sdio", "sdhci", "mmc", "emmc", "ufs")),
    ("Laden/Akku", ("charger", "fgu", "battery", "bq25", "bq2560", "fuel", "cm", "power-supply")),
    ("PMIC/Spannung", ("pmic", "regulator", "ldo", "dcdc", "sc2730", "sc2731", "adi", "efuse", "poweroff")),
    ("Funk", ("wcn", "wifi", "bt", "bluetooth", "gnss", "gps", "marlin", "wlan")),
    ("Modem", ("modem", "sipc", "seth", "cp-", "pm-", "rfspi")),
    ("Eingabe", ("touch", "keys", "key", "eic", "vibrator", "input", "tp")),
    ("Sensoren", ("thermal", "temp", "sensor", "adc", "prox", "als")),
    ("Takte", ("clock", "clk", "pll", "gate")),
    ("Busse", ("i2c", "spi", "serial", "uart", "pwm", "dma", "mailbox", "hwspinlock", "gpio", "pinctrl")),
    ("System", ("timer", "interrupt", "gic", "watchdog", "wdt", "iommu", "syscon", "memory", "dmc", "pmu", "cpu")),
]


def area_of(node):
    hay = (node.name + " " + " ".join(node.compatible)).lower()
    for area, keys in AREAS:
        for k in keys:
            if k in hay:
                return area
    return "Sonstiges"


# ---------------------------------------------------------------------------
#  Zuordnen: welcher Linux-Knoten gehört zu welchem Android-Knoten
# ---------------------------------------------------------------------------

SKIP_TOP = {"__symbols__", "__fixups__", "__local_fixups__", "aliases", "chosen",
            "memory", "reserved-memory", "cpus"}
INFRA = {"simple-bus", "simple-mfd", "syscon", "simple-pm-bus", "simple-audio-card"}


def infra(n):
    """Reine Struktur-/Registerbloecke ohne eigenen Treiber."""
    cs = n.compatible
    return bool(cs) and all(c in INFRA or c.endswith("-glbregs") or c.endswith("-glb-regs")
                            for c in cs)


def skipped(n):
    top = n
    while top.parent is not None and top.parent.parent is not None:
        top = top.parent
    if top.parent is not None and top.basename in SKIP_TOP:
        return True
    if n.basename in ("port", "ports", "endpoint") or n.basename.startswith("port@"):
        return True
    return False


def model_part(c):
    return c.split(",", 1)[1] if "," in c else c


def score(a, l):
    s = 0.0
    ac, lc = set(a.compatible), set(l.compatible)
    if ac & lc:
        s += 10
    if a.basename == l.basename:
        s += 5
    s += 3 * difflib.SequenceMatcher(None, a.basename, l.basename).ratio()
    if a.compatible and l.compatible:
        best = max(difflib.SequenceMatcher(None, model_part(x), model_part(y)).ratio()
                   for x in a.compatible for y in l.compatible)
        s += 3 * best
    return s


def match_trees(A, L):
    l_by_addr = defaultdict(list)
    l_by_compat = defaultdict(list)
    for n in L.nodes:
        if skipped(n):
            continue
        ad = L.addr(n)
        if ad:
            l_by_addr[ad[0]].append(n)
        for c in n.compatible:
            l_by_compat[c].append(n)
    a_per_addr = defaultdict(int)
    for n in A.nodes:
        if not skipped(n) and A.addr(n):
            a_per_addr[A.addr(n)[0]] += 1

    amatch = {}
    taken = {}
    for a in A.nodes:
        if a.parent is None:
            amatch[a] = L.root
            continue
        if skipped(a):
            continue
        cands = []
        how = ""
        ad = A.addr(a)
        if ad and ad[0] in l_by_addr:
            cands = l_by_addr[ad[0]]
            how = "Adresse"
            if a_per_addr[ad[0]] > 1:
                cands = [c for c in cands if score(a, c) >= 4.5]
        if not cands and a.unit is not None and not ad:
            try:
                ua = int(a.unit, 16)
            except ValueError:
                ua = None
            if ua is not None and ua in l_by_addr:
                cands = [c for c in l_by_addr[ua] if score(a, c) >= 3.5]
                how = "Name-Adresse"
        if not cands and a.parent in amatch:
            lp = amatch[a.parent]
            cands = [c for c in lp.children if not skipped(c) and
                     ((a.unit is not None and c.unit == a.unit) or c.basename == a.basename or
                      (set(c.compatible) & set(a.compatible)))]
            how = "Elternknoten"
        if not cands and a.compatible:
            seen = []
            for c in a.compatible:
                for n in l_by_compat.get(c, []):
                    if n not in seen:
                        seen.append(n)
            cands = seen
            how = "compatible"
        if not cands and a.depth == 1 and a.unit is None:
            cands = [c for c in L.root.children if not skipped(c) and c.unit is None and
                     difflib.SequenceMatcher(None, a.basename, c.basename).ratio() >= 0.6]
            how = "Name"
        if not cands:
            continue
        best = max(cands, key=lambda c: score(a, c))
        sc = score(a, best)
        prev = taken.get(best)
        if prev is not None and prev[1] >= sc:
            continue
        if prev is not None:
            amatch.pop(prev[0], None)
        amatch[a] = best
        taken[best] = (a, sc, how)
    return amatch, {v: k for k, v in amatch.items()}


# ---------------------------------------------------------------------------
#  Vergleich eines Knotenpaars
# ---------------------------------------------------------------------------

HIGH, MID, LOW = 3, 2, 1
SEV_NAME = {HIGH: "wichtig", MID: "prüfen", LOW: "info"}

COMMON_IGNORE = {"phandle", "linux,phandle", "status", "compatible", "reg", "reg-names",
                 "#address-cells", "#size-cells", "ranges", "interrupt-parent",
                 "#clock-cells", "#reset-cells", "#gpio-cells", "#interrupt-cells",
                 "#phy-cells", "#pwm-cells", "#power-domain-cells", "#iommu-cells",
                 "#dma-cells", "#io-channel-cells", "#mbox-cells", "#hwlock-cells",
                 "#thermal-sensor-cells", "#sound-dai-cells", "#syscon-cells",
                 "gpio-controller", "interrupt-controller", "wakeup-source",
                 "clock-output-names", "#cooling-cells", "phandle"}


def names_of(n, prop):
    return n.strs(prop)


def compare(A, L, a, l, amatch):
    """Unterschiede zwischen Android-Knoten a und Linux-Knoten l."""
    out = []

    # --- Takte
    ra = parse_refs(A, a, "clocks") or []
    rl = parse_refs(L, l, "clocks") or []
    if ra or rl:
        na, nl = len(ra), len(rl)
        an, ln = names_of(a, "clock-names"), names_of(l, "clock-names")
        only_l = [x for x in ln if x not in an]
        only_a = [x for x in an if x not in ln]
        if nl > na:
            txt = "Dein Kernel verlangt %d Takte, Android benutzt nur %d." % (nl, na)
            if only_l:
                txt += " Nur bei dir: %s - existiert die Hardware dazu überhaupt?" % ", ".join(only_l)
            out.append((MID, "Takte", txt))
        elif nl < na:
            txt = "Android benutzt %d Takte, dein Kernel nur %d." % (na, nl)
            if only_a:
                txt += " Nur bei Android: %s." % ", ".join(only_a)
            out.append((MID, "Takte", txt))
        elif an and ln and set(an) != set(ln):
            out.append((LOW, "Takte", "Gleiche Anzahl, andere Namen: Android %s / Linux %s"
                        % (", ".join(an), ", ".join(ln))))

    # --- Spannungen
    sa = {p for p in a.props if p.endswith("-supply")}
    sl = {p for p in l.props if p.endswith("-supply")}
    l_sup_targets = {}
    for p in sl:
        r = parse_refs(L, l, p)
        if r and r[0][0] is not None:
            l_sup_targets[r[0][0]] = p
    for p in sorted(sa - sl):
        tgt = (parse_refs(A, a, p) or [(None, ())])[0]
        same = amatch.get(tgt[0]) if tgt[0] is not None else None
        if same is not None and same in l_sup_targets:
            out.append((LOW, "Spannung", "Android nennt sie %s, bei dir %s - gleiche Quelle (%s)."
                        % (p, l_sup_targets[same], L.desc(same))))
            continue
        sev = LOW if ("slp" in p or p.startswith("usb-")) else HIGH
        out.append((sev, "Spannung", "%s fehlt bei dir (Android: %s)." % (p, fmt_ref(A, tgt))))

    # --- GPIOs
    ga = {p for p in a.props if ref_kind(p)[1] == "#gpio-cells"}
    gl = {p for p in l.props if ref_kind(p)[1] == "#gpio-cells"}
    for p in sorted(ga - gl):
        refs = parse_refs(A, a, p)
        out.append((HIGH, "GPIO", "%s fehlt bei dir (Android: %s)."
                    % (p, "; ".join(fmt_ref(A, r) for r in refs))))
    for p in sorted(ga & gl):
        x = [r[1][:1] for r in parse_refs(A, a, p)]
        y = [r[1][:1] for r in parse_refs(L, l, p)]
        if x != y:
            out.append((MID, "GPIO", "%s: anderer Pin - Android %s, Linux %s."
                        % (p, "; ".join(fmt_ref(A, r) for r in parse_refs(A, a, p)),
                           "; ".join(fmt_ref(L, r) for r in parse_refs(L, l, p)))))

    # --- Interrupts
    ia, il = a.cells("interrupts"), l.cells("interrupts")
    if ia and il and ia != il:
        out.append((MID, "Interrupt", "Android %s, Linux %s." % (ia, il)))
    elif ia and not il and "interrupts-extended" not in l.props:
        out.append((MID, "Interrupt", "Android nutzt Interrupt %s, dein Knoten hat keinen." % ia))

    # --- sonstige Verweise
    for p in ("phys", "pwms", "resets", "power-domains", "iommus", "dmas", "io-channels", "hwlocks"):
        xa, xl = parse_refs(A, a, p) or [], parse_refs(L, l, p) or []
        if xa and not xl:
            out.append((MID, p, "Nur Android: %s." % "; ".join(fmt_ref(A, r) for r in xa)))
        elif xa and xl and len(xa) != len(xl):
            out.append((MID, p, "Android %d Einträge, Linux %d." % (len(xa), len(xl))))

    # --- reg-Größe
    rga, rgl = A.reg_list(a), L.reg_list(l)
    if rga and rgl and rga[0][1] and rgl[0][1] and rga[0][1] != rgl[0][1]:
        out.append((LOW, "Register", "Bereichsgröße Android 0x%x, Linux 0x%x." % (rga[0][1], rgl[0][1])))
    if len(rga) > len(rgl) and rgl:
        out.append((LOW, "Register", "Android nutzt %d Registerbereiche, Linux %d." % (len(rga), len(rgl))))

    # --- nur bei Android vorhandene Eigenschaften
    handled = set(sa) | set(ga) | {"clocks", "clock-names", "interrupts", "phys", "pwms", "resets",
                                   "power-domains", "iommus", "dmas", "io-channels", "hwlocks"}
    rest = [p for p in a.props if p not in l.props and p not in handled and p not in COMMON_IGNORE
            and not p.endswith("-names")]
    vend = [p for p in rest if "," in p]
    std = [p for p in rest if "," not in p]
    if std:
        out.append((LOW, "Eigenschaften", "Nur bei Android: " + ", ".join(std[:14]) +
                    (" ..." if len(std) > 14 else "")))
    if vend:
        out.append((LOW, "Hersteller", "Android-Sonderwerte: " + ", ".join(vend[:14]) +
                    (" ..." if len(vend) > 14 else "")))
    return out


def provider_checks(L, K, l):
    """Sind die Quellen (Takte, Spannungen, PHYs ...) des Linux-Knotens verfügbar?"""
    out = []
    seen = set()
    for p in list(l.props):
        refs = parse_refs(L, l, p)
        if not refs or p in ("interrupt-parent", "remote-endpoint", "assigned-clock-parents"):
            continue
        for tgt, _args in refs:
            if tgt is None or tgt in seen:
                continue
            seen.add(tgt)
            if not L.enabled(tgt):
                out.append((HIGH, "Quelle", "%s: %s ist bei dir deaktiviert (status)." % (p, L.desc(tgt))))
                continue
            if K is None:
                continue
            own = L.compat_owner(tgt)
            chain = []
            cur = own
            while cur is not None and cur.parent is not None:
                if cur.compatible and "simple-bus" not in cur.compatible and "simple-mfd" not in cur.compatible[:1]:
                    chain.append(cur)
                cur = L.compat_owner(cur.parent) if cur.parent is not None else None
            for c in chain:
                drv = K.driver(c.compatible)
                if drv is None:
                    if "syscon" in c.compatible or "simple-mfd" in c.compatible:
                        continue
                    out.append((HIGH, "Quelle", "%s kommt von %s (%s) - dafür gibt es keinen Treiber im Kernel."
                                % (p, L.desc(c), c.compatible[0])))
                    break
                if drv["state"] == "n":
                    out.append((HIGH, "Quelle", "%s kommt von %s - dessen Treiber ist nicht eingebaut (%s)."
                                % (p, L.desc(c), ", ".join(drv["missing"]))))
                    break
    return out


# ---------------------------------------------------------------------------
#  DTS-Vorlage aus Android-Knoten
# ---------------------------------------------------------------------------

def render_value(A, L, amatch, node, prop):
    v = node.props[prop]
    if not v:
        return None
    refs = parse_refs(A, node, prop)
    if refs:
        parts = []
        for tgt, args in refs:
            lt = amatch.get(tgt) if tgt is not None else None
            if lt is not None and L.label(lt):
                ref = "&" + L.label(lt)
            else:
                ref = "&UNBEKANNT /* Android: %s */" % (tgt.path if tgt else "?")
            parts.append("<%s%s>" % (ref, "".join(" " + (str(x) if x < 256 else hex(x)) for x in args)))
        return ", ".join(parts)
    # Zeichenketten?
    if v[-1:] == b"\0" and all(32 <= b < 127 or b == 0 for b in v) and b"\0\0" not in v:
        return ", ".join('"%s"' % s for s in node.strs(prop))
    if len(v) % 4 == 0:
        return "<%s>" % " ".join(hex(x) for x in node.cells(prop))
    return "[%s]" % " ".join("%02x" % b for b in v)


def dts_template(A, L, amatch, a, l):
    lines = []
    props = [p for p in a.props if p not in ("phandle", "linux,phandle", "status")]
    if l is not None and L.label(l):
        props = [p for p in props if p not in l.props and p != "compatible" and p != "reg"]
        lines.append("&%s {" % L.label(l))
        lines.append("\t/* Vorlage aus Android - jeden Wert prüfen! */")
    else:
        lines.append("%s {" % a.name)
        lines.append("\t/* neuer Knoten nach Android-Vorbild - Adressen/Verweise prüfen! */")
    for p in props:
        val = render_value(A, L, amatch, a, p)
        lines.append("\t%s;" % p if val is None else "\t%s = %s;" % (p, val))
    lines.append('\tstatus = "okay";')
    lines.append("};")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
#  Reservierte Speicherbereiche
# ---------------------------------------------------------------------------

def reserved(tree):
    rm = tree.by_path.get("/reserved-memory")
    out = []
    if not rm:
        return out
    for c in rm.children:
        for a, s in tree.reg_list(c):
            if s:
                out.append({"name": c.name, "addr": a, "size": s,
                            "nomap": "no-map" in c.props, "reusable": "reusable" in c.props,
                            "compat": c.str("compatible") or ""})
    return out


def compare_reserved(A, L):
    ra, rl = reserved(A), reserved(L)
    res = []
    for r in ra:
        a0, a1 = r["addr"], r["addr"] + r["size"]
        cover = 0
        for x in rl:
            b0, b1 = x["addr"], x["addr"] + x["size"]
            cover += max(0, min(a1, b1) - max(a0, b0))
        if cover >= r["size"]:
            st = "ok"
        elif cover:
            st = "teilweise"
        else:
            st = "fehlt"
        res.append(dict(r, state=st))
    extra = []
    for x in rl:
        b0, b1 = x["addr"], x["addr"] + x["size"]
        if not any(max(0, min(b1, r["addr"] + r["size"]) - max(b0, r["addr"])) for r in ra):
            extra.append(x)
    return res, extra


# ---------------------------------------------------------------------------
#  dmesg (optional)
# ---------------------------------------------------------------------------

def parse_dmesg(path):
    probs = defaultdict(list)
    if not path:
        return probs
    for line in open(os.path.expanduser(path), errors="replace"):
        line = re.sub(r"^\[\s*[\d.]+\]\s*", "", line.strip())
        m = re.search(r"\b([0-9a-f]{6,16})\.([\w\-]+)", line)
        if not m:
            continue
        if re.search(r"fail|error|deferred|timeout|not ready|unable|cannot|can't|-\d{1,3}\b", line, re.I):
            probs[int(m.group(1), 16)].append(line[:220])
    return probs


# ---------------------------------------------------------------------------
#  Auswertung
# ---------------------------------------------------------------------------

CAT_EASY = "einfach"      # Treiber da, nur einschalten
CAT_CONFIG = "config"     # im Device-Tree aktiv, Treiber nicht eingebaut
CAT_CHECK = "prüfen"     # aktiv, aber Unterschiede
CAT_OK = "ok"
CAT_MISSING = "treiber"   # kein Treiber
CAT_ONLYLINUX = "nurlinux"

CAT_INFO = {
    CAT_EASY: ("Einfach: nur einschalten", "Mainline-Treiber vorhanden - Knoten im Device-Tree aktivieren/ergänzen."),
    CAT_CONFIG: ("Treiber nicht eingebaut", "Knoten ist aktiv, aber der Treiber fehlt in der Kernel-Konfiguration."),
    CAT_CHECK: ("Aktiv, aber Unterschiede", "Läuft bei dir, weicht aber von Android ab - hier verstecken sich Hänger."),
    CAT_MISSING: ("Treiber fehlt", "Kein passender Treiber im Kernel gefunden - hier wäre Treiberarbeit nötig."),
    CAT_ONLYLINUX: ("Nur bei dir aktiv", "Dein Kernel schaltet etwas ein, das Android nicht benutzt."),
    CAT_OK: ("In Ordnung", "Aktiv und ohne wichtige Unterschiede."),
}
CAT_ORDER = [CAT_CONFIG, CAT_CHECK, CAT_EASY, CAT_ONLYLINUX, CAT_MISSING, CAT_OK]


def relevant(tree, n):
    return n.parent is not None and not skipped(n) and bool(n.compatible) and not infra(n)


def analyse(A, L, K, dmesg=None):
    amatch, lmatch = match_trees(A, L)
    items = []

    def drv_info(compats):
        if K is None:
            return None
        return K.driver(compats)

    for a in A.nodes:
        if not relevant(A, a) or not A.enabled(a):
            continue
        l = amatch.get(a)
        l_en = l is not None and L.enabled(l)
        it = {"a": a, "l": l, "area": area_of(a), "diffs": [], "drv": None,
              "similar": [], "template": None, "dmesg": []}
        compats = (l.compatible if l is not None and l.compatible else []) + a.compatible
        drv = drv_info(compats)
        it["drv"] = drv
        if l_en:
            it["diffs"] = compare(A, L, a, l, amatch)
            it["diffs"] += provider_checks(L, K, l)
            if drv is not None and drv["state"] == "n":
                it["cat"] = CAT_CONFIG
            elif K is not None and drv is None and l.compatible:
                it["cat"] = CAT_MISSING
            elif any(s >= MID for s, _k, _t in it["diffs"]):
                it["cat"] = CAT_CHECK
            else:
                it["cat"] = CAT_OK
        else:
            if drv is not None or K is None:
                it["cat"] = CAT_EASY
            else:
                it["cat"] = CAT_MISSING
                it["similar"] = K.similar(a.compatible) if K else []
            it["template"] = dts_template(A, L, amatch, a, l)
            if l is not None:
                it["diffs"] = compare(A, L, a, l, amatch)
        if dmesg and l is not None and L.addr(l):
            it["dmesg"] = dmesg.get(L.addr(l)[0], [])
            if it["dmesg"] and it["cat"] == CAT_OK:
                it["cat"] = CAT_CHECK
        items.append(it)

    # Nur bei Linux aktiv
    for l in L.nodes:
        if not relevant(L, l) or not L.enabled(l):
            continue
        a = lmatch.get(l)
        if a is not None and A.enabled(a):
            continue
        # Kinder des Wurzelknotens ohne Hardware (Timer, PSCI ...) sind normal
        it = {"a": a, "l": l, "area": area_of(l), "diffs": [], "drv": None,
              "similar": [], "template": None, "dmesg": [], "cat": CAT_ONLYLINUX}
        if K is not None:
            it["drv"] = K.driver(l.compatible)
            if it["drv"] is not None and it["drv"]["state"] == "n":
                it["diffs"].append((LOW, "Treiber", "Treiber ist ohnehin nicht eingebaut (%s)."
                                    % ", ".join(it["drv"]["missing"])))
        if a is not None:
            it["diffs"].append((MID, "Status", "Android hat diesen Baustein ausgeschaltet (status = \"%s\")."
                                % (a.str("status") or "?")))
        if dmesg and L.addr(l):
            it["dmesg"] = dmesg.get(L.addr(l)[0], [])
        items.append(it)

    res, extra = compare_reserved(A, L)
    return items, amatch, res, extra


# ---------------------------------------------------------------------------
#  Ausgabe: Terminal
# ---------------------------------------------------------------------------

def node_title(tree, n):
    if n is None:
        return "-"
    lab = tree.label(n)
    ad = tree.addr(n)
    s = n.name
    if lab:
        s = "%s (&%s)" % (s, lab)
    if ad and ("@" not in n.name or n.unit != ("%x" % ad[0])):
        s += " [0x%x]" % ad[0]
    return s


def drv_short(drv):
    if drv is None:
        return "kein Treiber gefunden"
    st = {"y": "eingebaut", "m": "als Modul", "n": "AUS", "?": "Konfig unbekannt"}[drv["state"]]
    s = "%s -> %s" % (drv["compat"], drv["file"])
    if drv["state"] == "n":
        s += " [%s: %s]" % (st, ", ".join(drv["missing"]))
    else:
        s += " [%s]" % st
    return s


def item_hint(it):
    """Wichtigster Hinweis für die Übersichtszeile - abhaengig von der Kategorie."""
    diffs = sorted(it["diffs"], key=lambda d: -d[0])
    top = diffs[0][2] if diffs and diffs[0][0] >= MID else ""
    cat, drv = it["cat"], it["drv"]
    if cat == CAT_CONFIG and drv:
        return "Treiber %s aus: %s" % (drv["file"], ", ".join(drv["missing"]))
    if cat == CAT_EASY:
        return (drv_short(drv) if drv else "") or top
    if cat == CAT_MISSING:
        if top:
            return top
        if it["similar"]:
            return "ähnlich: " + ", ".join(x[0] for x in it["similar"])
        return ""
    if top:
        return top
    if it["dmesg"]:
        return "dmesg: " + it["dmesg"][0]
    return ""


def print_summary(A, L, K, items, res, extra, color=True):
    def c(code, s):
        return "\033[%sm%s\033[0m" % (code, s) if color else s
    counts = defaultdict(int)
    for it in items:
        counts[it["cat"]] += 1
    na = sum(1 for n in A.nodes if relevant(A, n) and A.enabled(n))
    nl = sum(1 for n in L.nodes if relevant(L, n) and L.enabled(n))
    print()
    print(c("1", "dtvergleich %s" % VERSION))
    print("  Android: %s  (%d aktive Bausteine)" % (A.source, na))
    print("  Linux:   %s  (%d aktive Bausteine)" % (L.source, nl))
    if K:
        print("  Kernel:  %s  (%d Dateien, %d compatibles, %.1fs)%s" % (
            K.kdir, K.nfiles, len(K.compat), K.scan_time,
            "" if K.config_path else "  - KEINE .config gefunden"))
    print()
    colors = {CAT_CONFIG: "31", CAT_CHECK: "33", CAT_EASY: "32", CAT_MISSING: "35",
              CAT_ONLYLINUX: "36", CAT_OK: "90"}
    for cat in CAT_ORDER:
        lst = [it for it in items if it["cat"] == cat]
        if not lst:
            continue
        title, _expl = CAT_INFO[cat]
        print(c(colors[cat] + ";1", "%s (%d)" % (title, len(lst))))
        if cat == CAT_OK:
            print("  " + ", ".join(sorted({it["area"] for it in lst})))
            print()
            continue
        lst.sort(key=lambda it: (it["area"], (it["a"] or it["l"]).path))
        for it in lst[:40]:
            n = it["a"] or it["l"]
            tree = A if it["a"] is not None else L
            hint = item_hint(it)
            hint = (" - " + hint) if hint else ""
            line = "  %-11s %s%s" % ("[" + it["area"] + "]", node_title(tree, n), hint)
            print(line[:220])
        if len(lst) > 40:
            print("  ... und %d weitere (siehe HTML-Bericht)" % (len(lst) - 40))
        print()
    miss = [r for r in res if r["state"] != "ok"]
    if miss:
        print(c("31;1", "Reservierter Speicher: %d von %d Android-Bereichen fehlen/teilweise" % (len(miss), len(res))))
        for r in miss:
            print("  %-10s 0x%09x +0x%-9x %s%s" % (r["state"], r["addr"], r["size"], r["name"],
                                                   " (no-map)" if r["nomap"] else ""))
        print()
    else:
        print(c("32;1", "Reservierter Speicher: alle %d Android-Bereiche abgedeckt" % len(res)))
        print()


# ---------------------------------------------------------------------------
#  Ausgabe: HTML
# ---------------------------------------------------------------------------

CSS = """
:root{--bg:#f7f7f5;--fg:#1d1d1b;--mut:#6b6b66;--card:#fff;--line:#e2e1dc;--code:#f0efea;
--red:#c0392b;--yel:#b7791f;--grn:#2f855a;--vio:#7b3fa0;--cya:#2b7a8a;--gry:#8a8a84}
@media (prefers-color-scheme:dark){:root{--bg:#161615;--fg:#ecebe6;--mut:#9d9c96;--card:#1f1f1d;
--line:#33332f;--code:#262623;--red:#ef6b5b;--yel:#e0b050;--grn:#5cc08a;--vio:#c08ae0;--cya:#5cbccc;--gry:#77776f}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);
font:15px/1.5 system-ui,-apple-system,"Segoe UI",sans-serif}
main{max-width:1100px;margin:0 auto;padding:28px 16px 80px}
h1{font-size:26px;margin:0 0 4px}h2{font-size:19px;margin:36px 0 10px}
.mut{color:var(--mut)}code,pre{font-family:ui-monospace,"DejaVu Sans Mono",monospace;font-size:13px}
pre{background:var(--code);padding:10px 12px;border-radius:8px;overflow-x:auto;white-space:pre}
.meta{font-size:13px;color:var(--mut);margin-bottom:18px}.meta code{word-break:break-all}
.tiles{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:10px;margin:18px 0}
.tile{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px 14px;
border-left:5px solid var(--c)}.tile b{font-size:26px;display:block}.tile span{font-size:13px;color:var(--mut)}
.bar{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:8px 0 14px}
input[type=search]{flex:1;min-width:200px;padding:8px 10px;border-radius:8px;border:1px solid var(--line);
background:var(--card);color:var(--fg);font:inherit}
label.f{font-size:13px;color:var(--mut);display:flex;gap:4px;align-items:center}
details{background:var(--card);border:1px solid var(--line);border-left:5px solid var(--c);
border-radius:10px;margin:8px 0;overflow:hidden}
summary{cursor:pointer;padding:10px 14px;display:flex;gap:10px;align-items:baseline;flex-wrap:wrap}
summary::-webkit-details-marker{display:none}
.area{font-size:12px;padding:1px 8px;border-radius:99px;background:var(--code);color:var(--mut);white-space:nowrap}
.name{font-weight:600;word-break:break-all}.hint{color:var(--mut);font-size:13px;flex-basis:100%}
.body{padding:4px 14px 14px;border-top:1px solid var(--line)}
table{border-collapse:collapse;width:100%;font-size:13px}td,th{text-align:left;padding:5px 8px;
border-bottom:1px solid var(--line);vertical-align:top}th{color:var(--mut);font-weight:500;width:130px}
.sev3{color:var(--red);font-weight:600}.sev2{color:var(--yel);font-weight:600}.sev1{color:var(--mut)}
.twocol{display:grid;grid-template-columns:1fr 1fr;gap:12px}@media(max-width:760px){.twocol{grid-template-columns:1fr}}
.twocol h4{margin:10px 0 4px;font-size:13px;color:var(--mut)}
.expl{font-size:14px;color:var(--mut);margin:-4px 0 8px}
.state-fehlt{color:var(--red);font-weight:600}.state-teilweise{color:var(--yel);font-weight:600}.state-ok{color:var(--grn)}
"""

JS = """
const q=document.getElementById('q');const boxes=[...document.querySelectorAll('input[data-cat]')];
function f(){const t=q.value.toLowerCase();const on=new Set(boxes.filter(b=>b.checked).map(b=>b.dataset.cat));
document.querySelectorAll('details[data-cat]').forEach(d=>{const ok=on.has(d.dataset.cat)&&(!t||d.dataset.s.includes(t));
d.style.display=ok?'':'none';});
document.querySelectorAll('section[data-cat]').forEach(s=>{const vis=[...s.querySelectorAll('details')].some(d=>d.style.display!=='none');
s.style.display=vis?'':'none';});}
q.addEventListener('input',f);boxes.forEach(b=>b.addEventListener('change',f));f();
"""

CAT_COLOR = {CAT_CONFIG: "var(--red)", CAT_CHECK: "var(--yel)", CAT_EASY: "var(--grn)",
             CAT_MISSING: "var(--vio)", CAT_ONLYLINUX: "var(--cya)", CAT_OK: "var(--gry)"}


def e(s):
    return html.escape(str(s))


def props_table(tree, n):
    if n is None:
        return "<p class=mut>- nicht vorhanden -</p>"
    rows = []
    rows.append("<tr><th>Pfad</th><td><code>%s</code></td></tr>" % e(n.path))
    ad = tree.addr(n)
    if ad:
        rows.append("<tr><th>Adresse</th><td><code>0x%x + 0x%x</code></td></tr>" % ad)
    rows.append("<tr><th>Status</th><td>%s</td></tr>" % ("aktiv" if tree.enabled(n) else
                                                          "aus (%s)" % e(n.str("status") or "Elternknoten aus")))
    for p in n.props:
        if p in ("phandle", "linux,phandle"):
            continue
        refs = parse_refs(tree, n, p)
        if refs:
            val = "<br>".join(e(fmt_ref(tree, r)) for r in refs)
        else:
            v = n.props[p]
            if not v:
                val = "<span class=mut>(gesetzt)</span>"
            elif v[-1:] == b"\0" and all(32 <= b < 127 or b == 0 for b in v):
                val = e(", ".join(n.strs(p)))
            elif len(v) % 4 == 0:
                cl = n.cells(p)
                val = e(" ".join(hex(x) for x in cl[:48]) + (" ..." if len(cl) > 48 else ""))
            else:
                val = e(v[:64].hex())
        rows.append("<tr><th>%s</th><td><code>%s</code></td></tr>" % (e(p), val))
    return "<table>%s</table>" % "".join(rows)


def write_html(path, A, L, K, items, res, extra, args):
    counts = defaultdict(int)
    for it in items:
        counts[it["cat"]] += 1
    out = []
    out.append("<!doctype html><html lang=de><head><meta charset=utf-8>"
               "<meta name=viewport content='width=device-width,initial-scale=1'>"
               "<title>DT-Vergleich</title><style>%s</style></head><body><main>" % CSS)
    out.append("<h1>Android-Device-Tree ↔ Mainline-Kernel</h1>")
    out.append("<div class=meta>Android: <code>%s</code><br>Linux: <code>%s</code>%s<br>"
               "Erstellt %s mit dtvergleich %s</div>" % (
                   e(A.source), e(L.source),
                   ("<br>Kernel: <code>%s</code> (%d compatibles%s)" % (
                       e(K.kdir), len(K.compat), "" if K.config_path else ", <b>keine .config</b>")) if K else "",
                   time.strftime("%Y-%m-%d %H:%M"), VERSION))

    out.append("<div class=tiles>")
    for cat in CAT_ORDER:
        out.append("<div class=tile style='--c:%s'><b>%d</b><span>%s</span></div>"
                   % (CAT_COLOR[cat], counts[cat], e(CAT_INFO[cat][0])))
    miss = sum(1 for r in res if r["state"] != "ok")
    out.append("<div class=tile style='--c:%s'><b>%d/%d</b><span>Reservierte Bereiche fehlen</span></div>"
               % ("var(--red)" if miss else "var(--grn)", miss, len(res)))
    out.append("</div>")

    out.append("<div class=bar><input id=q type=search placeholder='Suchen: Name, Adresse, compatible, Bereich …'>")
    for cat in CAT_ORDER:
        out.append("<label class=f><input type=checkbox data-cat=%s %s>%s</label>"
                   % (cat, "" if cat == CAT_OK else "checked", e(CAT_INFO[cat][0])))
    out.append("</div>")

    for cat in CAT_ORDER:
        lst = [it for it in items if it["cat"] == cat]
        if not lst:
            continue
        lst.sort(key=lambda it: (it["area"], (it["a"] or it["l"]).path))
        out.append("<section data-cat=%s><h2>%s (%d)</h2><p class=expl>%s</p>"
                   % (cat, e(CAT_INFO[cat][0]), len(lst), e(CAT_INFO[cat][1])))
        for it in lst:
            a, l = it["a"], it["l"]
            n = a or l
            tree = A if a is not None else L
            diffs = sorted(it["diffs"], key=lambda d: -d[0])
            hint = item_hint(it)
            search = " ".join([n.path, it["area"], " ".join(n.compatible),
                               l.path if l is not None else "", hint,
                               ("0x%x" % tree.addr(n)[0]) if tree.addr(n) else ""]).lower()
            out.append("<details data-cat=%s data-s='%s' style='--c:%s'><summary>"
                       "<span class=area>%s</span><span class=name>%s</span>"
                       "<span class=mut>%s</span><span class=hint>%s</span></summary><div class=body>"
                       % (cat, e(search), CAT_COLOR[cat], e(it["area"]), e(node_title(tree, n)),
                          e(", ".join(n.compatible[:2])), e(hint)))
            rows = []
            if it["drv"]:
                d = it["drv"]
                rows.append("<tr><th>Treiber</th><td><code>%s</code> → <code>%s</code><br>%s</td></tr>" % (
                    e(d["compat"]), e(d["file"]),
                    e("Konfig: " + (", ".join(d["syms"]) if d["syms"] else "immer eingebaut / unbekannt")
                      + {"y": " - eingebaut", "m": " - als Modul", "n": " - FEHLT: " + ", ".join(d["missing"]),
                         "?": ""}[d["state"]])))
            elif K is not None:
                rows.append("<tr><th>Treiber</th><td>keiner gefunden%s</td></tr>" % (
                    ("; ähnlich: " + ", ".join("<code>%s</code> (%s)" % (e(s[0]), e(s[1])) for s in it["similar"]))
                    if it["similar"] else ""))
            for sev, kind, txt in diffs:
                rows.append("<tr><th class=sev%d>%s</th><td><span class=sev%d>%s:</span> %s</td></tr>"
                            % (sev, e(SEV_NAME[sev]), sev, e(kind), e(txt)))
            for line in it["dmesg"]:
                rows.append("<tr><th class=sev2>dmesg</th><td><code>%s</code></td></tr>" % e(line))
            if rows:
                out.append("<table>%s</table>" % "".join(rows))
            if it["template"]:
                out.append("<h4 class=mut>Vorlage für deinen Device-Tree</h4><pre>%s</pre>" % e(it["template"]))
            out.append("<div class=twocol><div><h4>Android</h4>%s</div><div><h4>Dein Kernel</h4>%s</div></div>"
                       % (props_table(A, a), props_table(L, l)))
            out.append("</div></details>")
        out.append("</section>")

    out.append("<h2>Reservierte Speicherbereiche</h2><p class=expl>Bereiche, die Android für Firmware "
               "(Trusted OS, Modem, DSP …) freihält. Fehlen sie bei dir, überschreibt Linux fremden Speicher.</p>")
    out.append("<table><tr><th>Zustand</th><th>Adresse</th><th>Größe</th><th>Name</th><th>Art</th></tr>")
    for r in res:
        out.append("<tr><td class='state-%s'>%s</td><td><code>0x%x</code></td><td><code>0x%x</code> (%s)</td>"
                   "<td>%s</td><td>%s</td></tr>" % (
                       r["state"], e(r["state"]), r["addr"], r["size"], human(r["size"]), e(r["name"]),
                       e(" ".join(x for x in ("no-map" if r["nomap"] else "", "reusable" if r["reusable"] else "",
                                              r["compat"]) if x))))
    out.append("</table>")
    if extra:
        out.append("<p class=mut>Nur bei dir reserviert: %s</p>" % e(", ".join(
            "%s (0x%x +0x%x)" % (x["name"], x["addr"], x["size"]) for x in extra)))

    ch = A.by_path.get("/chosen")
    if ch and ch.str("bootargs"):
        out.append("<h2>Android-Befehlszeile</h2><p class=expl>Was der Bootloader Android mitgegeben hat - "
                   "enthält oft Adressen und Hardware-Kennungen.</p><pre>%s</pre>"
                   % e(ch.str("bootargs").replace(" ", "\n")))
    out.append("<script>%s</script></main></body></html>" % JS)
    with open(path, "w", encoding="utf-8") as f:
        f.write("".join(out))


def human(n):
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024:
            return "%d %s" % (n, unit)
        n //= 1024
    return "%d TB" % n


def write_json(path, A, L, items, res):
    data = {"android": A.source, "linux": L.source, "items": [], "reserved": res}
    for it in items:
        a, l = it["a"], it["l"]
        data["items"].append({
            "category": it["cat"], "area": it["area"],
            "android": a.path if a is not None else None,
            "linux": l.path if l is not None else None,
            "compatible": (a or l).compatible,
            "driver": it["drv"],
            "diffs": [{"severity": SEV_NAME[s], "kind": k, "text": t} for s, k, t in it["diffs"]],
            "dmesg": it["dmesg"],
        })
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1, ensure_ascii=False)


# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(
        description="Android-Device-Tree gegen Mainline-Kernel vergleichen.",
        formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__.split("Aufruf", 1)[1]
        if "Aufruf" in __doc__ else None)
    ap.add_argument("--android", "-a", required=True, help="Android-DT: live.dts, .dtb, vendor_boot.img …")
    ap.add_argument("--linux", "-l", required=True, help="eigener DT: .dts (mit --kernel) oder .dtb")
    ap.add_argument("--kernel", "-k", help="Kernel-Quellbaum (fuer Treiber- und Konfig-Pruefung)")
    ap.add_argument("--config", "-c", help=".config (Standard: <kernel>/.config)")
    ap.add_argument("--dmesg", "-d", help="optional: dmesg-Ausgabe des laufenden eigenen Kernels")
    ap.add_argument("--out", "-o", default="dt-vergleich.html", help="HTML-Bericht (Standard: dt-vergleich.html)")
    ap.add_argument("--json", help="zusätzlich maschinenlesbar speichern")
    ap.add_argument("--no-color", action="store_true")
    args = ap.parse_args()

    kernel = os.path.expanduser(args.kernel) if args.kernel else None
    print("Lade Android-Device-Tree ...", file=sys.stderr)
    A = load_tree(args.android, kernel)
    print("Lade eigenen Device-Tree ...", file=sys.stderr)
    L = load_tree(args.linux, kernel)
    K = None
    if kernel:
        print("Durchsuche Kernel nach Treibern ...", file=sys.stderr)
        K = KernelIndex(kernel, args.config)
    dm = parse_dmesg(args.dmesg) if args.dmesg else None
    items, _amatch, res, extra = analyse(A, L, K, dm)
    print_summary(A, L, K, items, res, extra, color=sys.stdout.isatty() and not args.no_color)
    write_html(args.out, A, L, K, items, res, extra, args)
    print("HTML-Bericht: %s" % os.path.abspath(args.out))
    if args.json:
        write_json(args.json, A, L, items, res)
        print("JSON: %s" % os.path.abspath(args.json))


if __name__ == "__main__":
    main()
