#!/usr/bin/env python3
"""profile_report.py: tiny386's PROFILE.TXT and the tiny386.elf that wrote it,
into a report of where core 1's time went (s31/PROFILE.md).

    python3 profile_report.py PROFILE.TXT tiny386.elf [options]

  --rom-elf FILE     the chip's ROM ELF, to name code in ROM (memcpy and the
                     like); looked for under $IDF_TOOLS_PATH, ~/.espressif-upstream
                     and ~/.espressif (esp-rom-elfs) when not given
  --addr2line FILE   addr2line for the ELF's machine; looked for on PATH and
                     under the same tool folders (riscv32-esp-elf-addr2line for
                     the boards); without one, no source lines
  --src TREE         the tiny386 tree the ELF was built from, for the text of
                     the lines and fast_exec()'s handler labels (default: the
                     tree this script is in, two folders up)
  --since EARLIER    the stretch between two files of the same run: EARLIER's
                     counts taken from PROFILE.TXT's (Ctrl+Alt+F8 writes
                     PROF01.TXT, PROF02.TXT, ...: PROF02.TXT --since
                     PROF01.TXT is what ran between the two presses)
  --top N            rows of each table (default 25)
  --force            go on when the ELF's SHA-256 is not the one the file names
  -o FILE            the report to FILE (default: standard output)

Python 3.6 or later, its standard library alone.  It reads the ELF itself
(symbols, sections and the fast interpreter's jump tables); only the source
lines need addr2line.  Every share has its 95 per cent margin from the
number of samples beside it, as +-.
"""

import argparse
import bisect
import collections
import glob
import hashlib
import math
import os
import re
import shutil
import struct
import subprocess
import sys

AREAS = ["fast", "exec1", "alone", "halt"]
AREA_WORDS = {
    "fast": "the fast interpreter (fast_exec) or the loop around it",
    "exec1": "an instruction handed to tiny386's cpu_exec1()",
    "alone": "tiny386's own interpreter alone (fast_cpu = 0)",
    "halt": "the guest halted (HLT), waiting for an interrupt",
}
MODES = ["real", "v86", "pm16", "pm32"]
REGIONS = ["iram", "flash", "rom", "other"]
REGION_WORDS = {
    "iram": "internal RAM",
    "flash": "flash, through core 1's instruction cache",
    "rom": "the chip's ROM",
    "other": "none of these",
}


# ==== PROFILE.TXT ====

class Profile:
    def __init__(self):
        self.meta = {}
        self.kv = {}            # "area" -> {"fast": n, ...} and the like
        self.ranges = {}        # "iram" -> (lo, hi)
        self.tables = {}        # "host" -> (slots, used, lost)
        self.cache = []         # (name, core, kind, {counter: value or None})
        self.host = []          # (pc, area, count)
        self.coarse = []        # (line, area, count)
        self.guest = []         # (area, mode, cpl, cs, page, guess, count)
        self.ops = []           # (key, count)
        self.complete = False
        self.cut = None         # where a file cut short stops making sense
        self.samples = 0


def parse_profile(path):
    """The file's lines.  A file cut short (a power cut or a full card in
    the write) is read up to its last whole line that makes sense: the line
    the cut fell in, and anything after a line that does not parse, are
    left out, and the report says so."""
    p = Profile()
    section = None
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()
    lines = text.splitlines()
    if not lines or not lines[0].startswith("tiny386 profile "):
        raise SystemExit("%s is not a tiny386 PROFILE.TXT (its first line is %r)" %
                         (path, lines[0] if lines else ""))
    p.meta["format"] = lines[0].split()[2] if len(lines[0].split()) > 2 else "?"
    if not text.endswith("\n"):         # the last line is cut: its figures would be wrong
        p.cut = "line %d, which has no end" % len(lines)
        lines = lines[:-1]
    for n, line in enumerate(lines[1:], 2):
        if not line.strip():
            continue
        if line == "end":
            p.complete = True
            break
        try:
            section = parse_line(p, line, section)
        except (ValueError, IndexError, KeyError):
            p.cut = "line %d, which does not parse: %r" % (n, line[:60])
            break
    p.samples = p.meta.get("samples", 0)
    return p


def fields(w, n):
    if len(w) != n:
        raise ValueError("%d fields, not %d" % (len(w), n))
    return w


def parse_line(p, line, section):
    """One line of the file into p; returns the table section it is in.
    ValueError, IndexError or KeyError for a line that does not parse."""
    if line.startswith("["):
        return line[1:line.index("]")]
    w = line.split()
    if section == "host":
        w = fields(w, 3)
        p.host.append((int(w[0], 16), int(w[1]), int(w[2])))
    elif section == "coarse":
        w = fields(w, 3)
        p.coarse.append((int(w[0], 16), int(w[1]), int(w[2])))
    elif section == "guest":
        w = fields(w, 7)
        p.guest.append((int(w[0]), int(w[1]), int(w[2]), int(w[3], 16), int(w[4], 16),
                        int(w[5]), int(w[6])))
    elif section == "ops":
        w = fields(w, 2)
        p.ops.append((int(w[0], 16), int(w[1])))
    else:
        key, rest = w[0], w[1:]
        if key in ("firmware", "board", "idf", "elf_sha256", "ini"):
            p.meta[key] = " ".join(rest)
        elif key == "sampler":
            p.meta["sampler"] = rest[0]
            p.meta["rate_hz"] = int(rest[1])
        elif key == "file":
            p.meta["file_no"] = int(rest[0])
            p.meta["why"] = rest[1] if len(rest) > 1 else "-"
        elif key == "run":
            int(rest[0], 16)
            p.meta["run"] = rest[0]
        elif key in ("fast_cpu", "cpu_mhz", "last_write_us", "samples", "op_unread",
                     "page_guess", "cache_reads"):
            p.meta[key] = int(rest[0])
        elif key == "range":
            p.ranges[rest[0]] = (int(rest[1], 16), int(rest[2], 16))
        elif key == "table":
            d = dict(zip(rest[1::2], rest[2::2]))
            p.tables[rest[0]] = (int(d["slots"]), int(d["used"]), int(d["lost"]))
        elif key == "cache":
            name, core, kind = rest[0], rest[1], rest[2]
            c = {}
            for k, v in zip(rest[3::2], rest[4::2]):
                c[k] = None if v == "-" else int(v)
            p.cache.append((name, core, kind, c))
        elif key in ("area_region", "mode_area"):
            d = {}
            i = 0
            while i < len(rest):
                d[rest[i]] = [int(x) for x in fields(rest[i + 1:i + 5], 4)]
                i += 5
            p.kv[key] = d
        else:
            # name value name value ...
            d = {}
            for k, v in zip(rest[0::2], rest[1::2]):
                try:
                    d[k] = int(v)
                except ValueError:
                    d[k] = v
            p.kv[key] = d
    return section


def subtract(later, earlier, names):
    """The counts of the stretch between two files of the same run: every
    count of the earlier file taken from the later's (a key never leaves a
    table, so this is exact)."""
    for k in ("elf_sha256", "sampler", "ini"):
        if later.meta.get(k) != earlier.meta.get(k):
            raise SystemExit("--since: %s and %s differ in %s: not two files of one run" %
                             (names[1], names[0], k))
    # each boot draws its own run word (v5.1): files numbered from 1 again by
    # a later boot of the same app are refused here
    if "run" in later.meta and "run" in earlier.meta and later.meta["run"] != earlier.meta["run"]:
        raise SystemExit("--since: %s is from run %s and %s from run %s: two boots, not one run" %
                         (names[1], earlier.meta["run"], names[0], later.meta["run"]))
    if not earlier.complete:
        later.meta["since_cut"] = "%s was cut short%s" % (
            names[1], (", read to before its " + earlier.cut) if earlier.cut else "")
    if earlier.samples > later.samples or earlier.meta.get("file_no", 0) >= later.meta.get("file_no", 0):
        raise SystemExit("--since: %s (file %s, %d samples) is not earlier than %s (file %s, %d "
                         "samples) in the same run" % (names[1], earlier.meta.get("file_no"),
                                                      earlier.samples, names[0],
                                                      later.meta.get("file_no"), later.samples))
    out = later
    for key, d in later.kv.items():
        e = earlier.kv.get(key, {})
        for k, v in d.items():
            if isinstance(v, list):
                d[k] = [a - b for a, b in zip(v, e.get(k, [0] * len(v)))]
            elif key == "sampler_cost" and k == "max":
                continue        # the most a sample took, over the whole run
            elif isinstance(v, int) and isinstance(e.get(k, 0), int):
                d[k] = v - e.get(k, 0)
    for k in ("samples", "op_unread", "page_guess", "cache_reads"):
        if k in later.meta:
            later.meta[k] -= earlier.meta.get(k, 0)
    later.samples = later.meta.get("samples", 0)
    for name, (slots, used, lost) in list(later.tables.items()):
        later.tables[name] = (slots, used, lost - earlier.tables.get(name, (0, 0, 0))[2])
    ecache = {c[0]: c[3] for c in earlier.cache}
    for name, core, kind, c in later.cache:
        for k in c:
            if c[k] is not None and ecache.get(name, {}).get(k) is not None:
                c[k] -= ecache[name][k]

    def minus(rows, erows, nkey):
        e = collections.Counter()
        for r in erows:
            e[r[:nkey]] += r[-1]
        res = []
        for r in rows:
            n = r[-1] - e.get(r[:nkey], 0)
            if n > 0:
                res.append(r[:nkey] + (n,))
        return res
    out.host = minus(later.host, earlier.host, 2)
    out.coarse = minus(later.coarse, earlier.coarse, 2)
    out.guest = minus(later.guest, earlier.guest, 6)
    out.ops = minus(later.ops, earlier.ops, 1)
    out.meta["stretch"] = "from file %s (%s) to file %s (%s)" % (
        earlier.meta.get("file_no"), earlier.meta.get("why"), later.meta.get("file_no"),
        later.meta.get("why"))
    return out


# ==== the ELF ====

class Elf:
    """Sections, symbols and bytes of a little-endian ELF32 or ELF64."""

    def __init__(self, path):
        with open(path, "rb") as f:
            self.data = f.read()
        d = self.data
        if d[:4] != b"\x7fELF":
            raise SystemExit("%s is not an ELF file" % path)
        self.bits = 64 if d[4] == 2 else 32
        if d[5] != 1:
            raise SystemExit("%s is big-endian: not one of tiny386's" % path)
        if self.bits == 32:
            (self.machine,) = struct.unpack_from("<H", d, 18)
            shoff, = struct.unpack_from("<I", d, 32)
            shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 46)
        else:
            (self.machine,) = struct.unpack_from("<H", d, 18)
            shoff, = struct.unpack_from("<Q", d, 40)
            shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 58)
        self.sections = []
        for i in range(shnum):
            o = shoff + i * shentsize
            if self.bits == 32:
                name, typ, flags, addr, off, size, link, info, align, entsize = \
                    struct.unpack_from("<IIIIIIIIII", d, o)
            else:
                name, typ, flags, addr, off, size, link, info, align, entsize = \
                    struct.unpack_from("<IIQQQQIIQQ", d, o)
            self.sections.append({"name": name, "type": typ, "flags": flags, "addr": addr,
                                  "off": off, "size": size, "link": link, "entsize": entsize})
        strs = self.sections[shstrndx]
        for s in self.sections:
            s["name"] = self._str(strs["off"], s["name"])
        self.symbols = []
        for s in self.sections:
            if s["type"] != 2:          # SHT_SYMTAB
                continue
            strtab = self.sections[s["link"]]
            n = s["size"] // s["entsize"]
            for i in range(n):
                o = s["off"] + i * s["entsize"]
                if self.bits == 32:
                    name, value, size, info, other, shndx = struct.unpack_from("<IIIBBH", d, o)
                else:
                    name, info, other, shndx, value, size = struct.unpack_from("<IBBHQQ", d, o)
                typ = info & 15
                if typ not in (0, 1, 2) or not name:
                    continue
                self.symbols.append((value, size, typ, shndx, self._str(strtab["off"], name)))

    def _str(self, base, off):
        end = self.data.index(b"\0", base + off)
        return self.data[base + off:end].decode("utf-8", "replace")

    def section_at(self, addr):
        for s in self.sections:
            if s["flags"] & 2 and s["addr"] <= addr < s["addr"] + s["size"]:
                return s
        return None

    def read(self, addr, n):
        s = self.section_at(addr)
        if not s or s["type"] == 8 or addr + n > s["addr"] + s["size"]:     # NOBITS
            return None
        o = s["off"] + addr - s["addr"]
        return self.data[o:o + n]


class Symbols:
    """Host PCs to function names: the ELF's functions, then the ROM's."""

    def __init__(self, elf, rom=None):
        funcs = {}
        points = []
        for value, size, typ, shndx, name in elf.symbols:
            if typ == 2 and size > 0:
                funcs[value] = (size, name)
            elif typ in (0, 2) and shndx != 0:
                points.append((value, name))
        if rom:
            for value, size, typ, shndx, name in rom.symbols:
                if typ == 2 and size > 0 and value not in funcs:
                    funcs[value] = (size, name)
        self.starts = sorted(funcs)
        self.funcs = [(a, a + funcs[a][0], funcs[a][1]) for a in self.starts]
        points.sort()
        self.pstarts = [a for a, _ in points]
        self.points = points

    def func(self, pc):
        """(name, start) of the function holding pc, else None."""
        i = bisect.bisect_right(self.starts, pc) - 1
        if i >= 0:
            a, e, n = self.funcs[i]
            if a <= pc < e:
                return n, a
        return None

    def name(self, pc):
        f = self.func(pc)
        if f:
            return f[0]
        i = bisect.bisect_right(self.pstarts, pc) - 1
        if i >= 0 and pc - self.points[i][0] < 0x1000:
            return "%s+0x%x" % (self.points[i][1], pc - self.points[i][0])
        return "?"


def fast_handlers(elf, syms):
    """The fast interpreter's handlers: its jump tables (ops.N and ops0f.N,
    static in fast_exec()) give the label each opcode starts at.  Returns
    (start, end, [(label address, description)]) or None."""
    fe = None
    for a, e, n in syms.funcs:
        if n == "fast_exec":
            fe = (a, e)
    if not fe:
        return None
    ptr = 8 if elf.bits == 64 else 4
    fmt = "<%d%s" % (256, "Q" if ptr == 8 else "I")
    tables = {}
    for value, size, typ, shndx, name in elf.symbols:
        base = name.split(".")[0]
        if typ == 1 and base in ("ops", "ops0f") and size == 256 * ptr:
            raw = elf.read(value, size)
            if not raw:
                continue
            t = struct.unpack(fmt, raw)
            if all(fe[0] <= x < fe[1] for x in t):
                tables[base] = t
    if "ops" not in tables:
        return None
    labels = collections.defaultdict(list)
    slow = collections.Counter(tables["ops"]).most_common(1)[0][0]
    for op, a in enumerate(tables["ops"]):
        if a != slow:
            labels[a].append("%02X" % op)
    for op, a in enumerate(tables.get("ops0f", ())):
        if a != slow:
            labels[a].append("0F %02X" % op)
    out = [(slow, "slow: an instruction handed to cpu_exec1()")]
    for a, ops in labels.items():
        out.append((a, "op " + compress_ops(ops)))
    out.sort()
    return fe[0], fe[1], out


def compress_ops(ops):
    """["40", "41", ... "47"] -> "40-47"; a few opcodes named with their
    mnemonic."""
    if len(ops) > 2:
        return "%s-%s (%d opcodes)" % (ops[0], ops[-1], len(ops))
    return ", ".join("%s %s" % (o, op_mnemonic(o)) for o in ops)


# ==== x86 opcodes (for the handlers and the handed-over instructions) ====

ONE = {}
for _b, _n in enumerate(["ADD", "OR", "ADC", "SBB", "AND", "SUB", "XOR", "CMP"]):
    for _j, _f in enumerate(["r/m8,r8", "r/m,r", "r8,r/m8", "r,r/m", "AL,imm8", "eAX,imm"]):
        ONE[_b * 8 + _j] = "%s %s" % (_n, _f)
ONE.update({0x06: "PUSH ES", 0x07: "POP ES", 0x0E: "PUSH CS", 0x16: "PUSH SS", 0x17: "POP SS",
            0x1E: "PUSH DS", 0x1F: "POP DS", 0x27: "DAA", 0x2F: "DAS", 0x37: "AAA", 0x3F: "AAS",
            0x60: "PUSHA", 0x61: "POPA", 0x62: "BOUND", 0x63: "ARPL", 0x68: "PUSH imm",
            0x69: "IMUL r,r/m,imm", 0x6A: "PUSH imm8", 0x6B: "IMUL r,r/m,imm8", 0x6C: "INSB",
            0x6D: "INSW/D", 0x6E: "OUTSB", 0x6F: "OUTSW/D", 0x80: "grp1 r/m8,imm8",
            0x81: "grp1 r/m,imm", 0x82: "grp1 r/m8,imm8", 0x83: "grp1 r/m,imm8",
            0x84: "TEST r/m8,r8", 0x85: "TEST r/m,r", 0x86: "XCHG r/m8,r8", 0x87: "XCHG r/m,r",
            0x88: "MOV r/m8,r8", 0x89: "MOV r/m,r", 0x8A: "MOV r8,r/m8", 0x8B: "MOV r,r/m",
            0x8C: "MOV r/m,Sreg", 0x8D: "LEA", 0x8E: "MOV Sreg,r/m", 0x8F: "POP r/m",
            0x90: "NOP", 0x98: "CBW/CWDE", 0x99: "CWD/CDQ", 0x9A: "CALL far", 0x9B: "WAIT",
            0x9C: "PUSHF", 0x9D: "POPF", 0x9E: "SAHF", 0x9F: "LAHF", 0xA0: "MOV AL,moffs",
            0xA1: "MOV eAX,moffs", 0xA2: "MOV moffs,AL", 0xA3: "MOV moffs,eAX", 0xA4: "MOVSB",
            0xA5: "MOVSW/D", 0xA6: "CMPSB", 0xA7: "CMPSW/D", 0xA8: "TEST AL,imm8",
            0xA9: "TEST eAX,imm", 0xAA: "STOSB", 0xAB: "STOSW/D", 0xAC: "LODSB", 0xAD: "LODSW/D",
            0xAE: "SCASB", 0xAF: "SCASW/D", 0xC0: "grp2 r/m8,imm8", 0xC1: "grp2 r/m,imm8",
            0xC2: "RET imm16", 0xC3: "RET", 0xC4: "LES", 0xC5: "LDS", 0xC6: "MOV r/m8,imm8",
            0xC7: "MOV r/m,imm", 0xC8: "ENTER", 0xC9: "LEAVE", 0xCA: "RETF imm16", 0xCB: "RETF",
            0xCC: "INT3", 0xCD: "INT imm8", 0xCE: "INTO", 0xCF: "IRET", 0xD0: "grp2 r/m8,1",
            0xD1: "grp2 r/m,1", 0xD2: "grp2 r/m8,CL", 0xD3: "grp2 r/m,CL", 0xD4: "AAM",
            0xD5: "AAD", 0xD6: "SALC", 0xD7: "XLAT", 0xE0: "LOOPNE", 0xE1: "LOOPE", 0xE2: "LOOP",
            0xE3: "JCXZ", 0xE4: "IN AL,imm8", 0xE5: "IN eAX,imm8", 0xE6: "OUT imm8,AL",
            0xE7: "OUT imm8,eAX", 0xE8: "CALL", 0xE9: "JMP", 0xEA: "JMP far", 0xEB: "JMP short",
            0xEC: "IN AL,DX", 0xED: "IN eAX,DX", 0xEE: "OUT DX,AL", 0xEF: "OUT DX,eAX",
            0xF0: "LOCK", 0xF1: "INT1", 0xF4: "HLT", 0xF5: "CMC", 0xF6: "grp3 r/m8",
            0xF7: "grp3 r/m", 0xF8: "CLC", 0xF9: "STC", 0xFA: "CLI", 0xFB: "STI", 0xFC: "CLD",
            0xFD: "STD", 0xFE: "grp4 r/m8", 0xFF: "grp5 r/m"})
for _i, _r in enumerate(["eAX", "eCX", "eDX", "eBX", "eSP", "eBP", "eSI", "eDI"]):
    ONE[0x40 + _i] = "INC " + _r
    ONE[0x48 + _i] = "DEC " + _r
    ONE[0x50 + _i] = "PUSH " + _r
    ONE[0x58 + _i] = "POP " + _r
    ONE[0xB8 + _i] = "MOV %s,imm" % _r
    if _i:
        ONE[0x90 + _i] = "XCHG eAX," + _r
for _i, _r in enumerate(["AL", "CL", "DL", "BL", "AH", "CH", "DH", "BH"]):
    ONE[0xB0 + _i] = "MOV %s,imm8" % _r
for _i, _c in enumerate(["O", "NO", "B", "AE", "E", "NE", "BE", "A", "S", "NS", "P", "NP", "L",
                         "GE", "LE", "G"]):
    ONE[0x70 + _i] = "J%s short" % _c
for _i in range(8):
    ONE[0xD8 + _i] = "FPU (%02X)" % (0xD8 + _i)

TWO = {0x00: "grp6 (SLDT, STR, LLDT, LTR, VERR, VERW)", 0x01: "grp7 (SGDT, SIDT, LGDT, LIDT, SMSW, LMSW, INVLPG)",
       0x02: "LAR", 0x03: "LSL", 0x06: "CLTS", 0x08: "INVD", 0x09: "WBINVD", 0x0B: "UD2",
       0x20: "MOV r,CRn", 0x21: "MOV r,DRn", 0x22: "MOV CRn,r", 0x23: "MOV DRn,r",
       0x30: "WRMSR", 0x31: "RDTSC", 0x32: "RDMSR", 0xA0: "PUSH FS", 0xA1: "POP FS",
       0xA2: "CPUID", 0xA3: "BT", 0xA4: "SHLD imm8", 0xA5: "SHLD CL", 0xA8: "PUSH GS",
       0xA9: "POP GS", 0xAB: "BTS", 0xAC: "SHRD imm8", 0xAD: "SHRD CL", 0xAF: "IMUL r,r/m",
       0xB0: "CMPXCHG r/m8", 0xB1: "CMPXCHG r/m", 0xB2: "LSS", 0xB3: "BTR", 0xB4: "LFS",
       0xB5: "LGS", 0xB6: "MOVZX r,r/m8", 0xB7: "MOVZX r,r/m16", 0xBA: "grp8 (BT, BTS, BTR, BTC imm8)",
       0xBB: "BTC", 0xBC: "BSF", 0xBD: "BSR", 0xBE: "MOVSX r,r/m8", 0xBF: "MOVSX r,r/m16",
       0xC0: "XADD r/m8", 0xC1: "XADD r/m"}
for _i, _c in enumerate(["O", "NO", "B", "AE", "E", "NE", "BE", "A", "S", "NS", "P", "NP", "L",
                         "GE", "LE", "G"]):
    TWO[0x80 + _i] = "J%s near" % _c
    TWO[0x90 + _i] = "SET%s" % _c
for _i in range(8):
    TWO[0xC8 + _i] = "BSWAP"

GROUPS = {
    0x80: ["ADD", "OR", "ADC", "SBB", "AND", "SUB", "XOR", "CMP"],
    0xC0: ["ROL", "ROR", "RCL", "RCR", "SHL", "SHR", "SAL", "SAR"],
    0xF6: ["TEST", "TEST", "NOT", "NEG", "MUL", "IMUL", "DIV", "IDIV"],
    0xFE: ["INC", "DEC", "CALL", "CALL far", "JMP", "JMP far", "PUSH", "?"],
}
for _b in (0x81, 0x82, 0x83):
    GROUPS[_b] = GROUPS[0x80]
for _b in (0xC1, 0xD0, 0xD1, 0xD2, 0xD3):
    GROUPS[_b] = GROUPS[0xC0]
GROUPS[0xF7] = GROUPS[0xF6]
GROUPS[0xFF] = GROUPS[0xFE]
GROUPS2 = {
    0x00: ["SLDT", "STR", "LLDT", "LTR", "VERR", "VERW", "?", "?"],
    0x01: ["SGDT", "SIDT", "LGDT", "LIDT", "SMSW", "?", "LMSW", "INVLPG"],
    0xBA: ["?", "?", "?", "?", "BT", "BTS", "BTR", "BTC"],
}


def op_mnemonic(text):
    """ "8B" or "0F B6" -> a mnemonic """
    w = text.split()
    if len(w) == 2:
        return TWO.get(int(w[1], 16), "")
    return ONE.get(int(w[0], 16), "")


def op_key_words(key):
    """prof_op_key()'s key (prof_hist.h) -> (instruction words, mode)"""
    modrm = key & 0xff
    op = (key >> 8) & 0xff
    two = key >> 16 & 1
    has = key >> 17 & 1
    pfx = []
    for bit, name in ((18, "66"), (19, "67"), (20, "REP"), (21, "REPNE"), (22, "LOCK"),
                      (23, "seg")):
        if key >> bit & 1:
            pfx.append(name)
    mode = MODES[key >> 24 & 3]
    partial = key >> 27 & 1
    if two:
        text = "0F %02X" % op
        name = TWO.get(op, "")
        grp = GROUPS2.get(op)
    else:
        text = "%02X" % op
        name = ONE.get(op, "")
        grp = GROUPS.get(op)
    if has:
        reg = modrm >> 3 & 7
        mem = (modrm >> 6) != 3
        text += " /%d" % reg
        if grp:
            name = "%s %s" % (grp[reg], "mem" if mem else "reg")
        else:
            name += " (memory)" if mem else " (registers)"
    if partial:
        name += " [bytes ran out]"
    words = " ".join(pfx + [text])
    return words, name, mode


# ==== tools ====

def tool_dirs():
    roots = [os.environ.get("IDF_TOOLS_PATH", ""), os.path.expanduser("~/.espressif-upstream"),
             os.path.expanduser("~/.espressif")]
    return [r for r in roots if r and os.path.isdir(os.path.join(r, "tools"))]


def find_addr2line(elf, given):
    if given:
        return given
    if elf.machine == 243:      # RISC-V: ESP-IDF's own first, then any other
        p = shutil.which("riscv32-esp-elf-addr2line")
        if p:
            return p
        for r in tool_dirs():
            hits = sorted(glob.glob(os.path.join(r, "tools", "riscv32-esp-elf", "*",
                                                 "riscv32-esp-elf", "bin",
                                                 "riscv32-esp-elf-addr2line*")))
            if hits:
                return hits[-1]
        for n in ("riscv32-unknown-elf-addr2line", "riscv64-unknown-elf-addr2line",
                  "riscv32-linux-addr2line"):
            p = shutil.which(n)
            if p:
                return p
        return None
    return shutil.which("addr2line")


def find_rom_elf(prof, given):
    if given:
        return given
    board = prof.meta.get("board", "")
    if "S31" not in board and "DOS board" not in board:
        return None
    for r in tool_dirs():
        hits = sorted(glob.glob(os.path.join(r, "tools", "esp-rom-elfs", "*",
                                             "esp32s31_rev0_rom.elf")))
        if hits:
            return hits[-1]
    return None


def addr2line(tool, elf_path, pcs):
    """pc -> [(function, file:line), ...] innermost first (inlined frames)."""
    out = {}
    if not tool or not pcs:
        return out
    pcs = sorted(pcs)
    for i in range(0, len(pcs), 2000):
        chunk = pcs[i:i + 2000]
        try:
            r = subprocess.run([tool, "-a", "-f", "-i", "-C", "-e", elf_path],
                               input="\n".join("0x%x" % pc for pc in chunk) + "\n",
                               stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                               universal_newlines=True, check=False)
        except OSError:
            return {}
        cur = None
        lines = r.stdout.splitlines()
        j = 0
        while j < len(lines):
            line = lines[j]
            if line.startswith("0x"):
                cur = int(line, 16)
                out[cur] = []
                j += 1
                continue
            fn = line
            loc = lines[j + 1] if j + 1 < len(lines) else "??:0"
            loc = loc.split(" (discriminator")[0]
            loc = loc.replace("/TINY386/", "")
            if cur is not None:
                out[cur].append((fn, loc))
            j += 2
    return out


# ==== the report ====

def pct(n, total):
    return 100.0 * n / total if total else 0.0


def margin(n, total):
    """the 95 per cent margin of n/total, in per cent"""
    if not total:
        return 0.0
    p = n / total
    return 196.0 * math.sqrt(p * (1 - p) / total)


def share(n, total):
    return "%5.1f%% +-%.1f" % (pct(n, total), margin(n, total))


class Out:
    def __init__(self, f):
        self.f = f

    def __call__(self, s=""):
        self.f.write(s + "\n")

    def table(self, head, rows):
        w = [len(h) for h in head]
        for r in rows:
            for i, c in enumerate(r):
                w[i] = max(w[i], len(str(c)))
        fmt = "  ".join("%%-%ds" % x for x in w)
        self(fmt % tuple(head))
        self("  ".join("-" * x for x in w))
        for r in rows:
            self(fmt % tuple(str(c) for c in r))
        self()


def guest_region(mode, cpl, page, cs):
    lin = page << 12
    if mode in (0, 1):
        if lin >= 0xF0000:
            return "BIOS (F0000h up)"
        if lin >= 0xC0000:
            return "option ROMs, the VGA BIOS (C0000h-EFFFFh)"
        if lin >= 0xA0000:
            return "video memory (A0000h-BFFFFh)"
        return "DOS memory (below A0000h)" + (" in a v86 box" if mode == 1 else "")
    if mode == 2:
        return "16-bit protected mode (Win16 and VxD 16-bit code, CS %04X)" % cs
    if lin >= 0xC0000000:
        return "C0000000h up: VMM and VxDs" if cpl == 0 else "C0000000h up at level %d" % cpl
    if lin >= 0x80000000:
        return "80000000h-BFFFFFFFh: the shared arena (KERNEL32, USER32, GDI32)"
    if lin >= 0x400000:
        return "400000h-7FFFFFFFh: an application's own"
    return "below 400000h (32-bit)"


def split_loc(loc):
    """ "/TINY386/i386_fast.h:810" -> ("/TINY386/i386_fast.h", 810); line 0 if none """
    path, _, line = loc.rpartition(":")
    try:
        return path, int(line)
    except ValueError:
        return loc, 0


class Source:
    """The tree's sources, for the text of a line and the handler a line of
    i386_fast.h is under (the last label at the start of a line above it, or
    the line itself when it is a macro that makes handlers, ALU6(...))."""

    LABEL = re.compile(r"^([a-z][a-z0-9_]*):")

    def __init__(self, root):
        self.root = root
        self.files = {}

    def _lines(self, path):
        name = os.path.basename(path)
        if name not in self.files:
            lines = None
            if self.root:
                for cand in (os.path.join(self.root, name), os.path.join(self.root, "esp", "main", name)):
                    if os.path.exists(cand):
                        with open(cand, "r", encoding="utf-8", errors="replace") as f:
                            lines = f.read().splitlines()
                        break
            self.files[name] = lines
        return self.files[name]

    def describe(self, name):
        lines = self._lines(name)
        if lines is None:
            return "not found: --src TREE"
        return os.path.join(self.root, name)

    def text(self, path, line):
        lines = self._lines(path)
        if not lines or not 0 < line <= len(lines):
            return ""
        t = " ".join(lines[line - 1].split())
        return t if len(t) <= 70 else t[:67] + "..."

    def handler(self, path, line):
        lines = self._lines(path)
        if not lines or not 0 < line <= len(lines):
            return "%s:%d" % (os.path.basename(path), line)
        here = lines[line - 1].strip()
        if here.startswith("ALU6(") or here.startswith("ALU_"):
            return here.split(",")[0] + ", ...) " + here.split(",")[6].strip() \
                if here.count(",") > 6 else here[:40]
        for i in range(line - 1, -1, -1):
            m = self.LABEL.match(lines[i])
            if m:
                return m.group(1)
            if lines[i].startswith("static bool") and "fast_exec(" in lines[i]:
                break
        return "fast_exec's entry, before the first label"


def report(prof, elf, elf_path, rom, a2l, top, out, src):
    o = Out(out)
    syms = Symbols(elf, rom)
    total = prof.samples
    m = prof.meta
    area = prof.kv.get("area", {})
    busy = total - area.get("halt", 0)
    o("tiny386 profile report")
    o("======================")
    o()
    o("Firmware %s on %s (ESP-IDF %s); ELF %s." % (m.get("firmware"), m.get("board"), m.get("idf"),
                                                  m.get("elf_sha256", "")[:16]))
    if m.get("elf_check"):
        o(m["elf_check"])
    o("Sampler: %s at about %d a second; file %d (%s)%s; ini %s; fast_cpu %d." %
      (m.get("sampler"), m.get("rate_hz", 0), m.get("file_no", 0), m.get("why", "-"),
       (" of run " + m["run"]) if m.get("run") else "", m.get("ini"), m.get("fast_cpu", -1)))
    t = prof.kv.get("time_us", {})
    samp_s = t.get("sampling", 0) / 1e6
    paused_s = t.get("paused", 0) / 1e6
    o("Sampling for %.1f s (%.2f s of it paused for the writes); %d samples, %d with the guest "
      "not halted." % (samp_s, paused_s, total, busy))
    if m.get("stretch"):
        o("A stretch of the run (--since): %s; the counts below are that stretch's alone." %
          m["stretch"])
    if not prof.complete:
        o("WARNING: the file has no \"end\" line: it was cut short (power off or a full card during "
          "a write?). It is read as far as it goes%s." %
          ((", to before its " + prof.cut) if prof.cut else ""))
    if m.get("since_cut"):
        o("WARNING: --since: %s; the stretch's counts are too high where it stops." % m["since_cut"])
    o("The write before this file: %d ms, the guest waiting." % (m.get("last_write_us", 0) // 1000))
    cost = prof.kv.get("sampler_cost", {})
    if cost.get("samples"):
        mhz = m.get("cpu_mhz", 0) or 1
        avg = cost["cycles"] / cost["samples"]
        o("The sampler's cost on core 0: %.0f cycles a sample on average (%.1f us), %d at most; "
          "%.2f%% of core 0 at this rate." % (avg, avg / mhz, cost.get("max", 0),
                                               100.0 * avg * m.get("rate_hz", 0) / (mhz * 1e6)))
    g = prof.kv.get("guest", {})
    if g.get("insns") and g.get("run_us"):
        mhz = m.get("cpu_mhz", 0)
        o("Guest: %.1f M instructions in %.1f s of running (%.2f M a second), %.1f s halted; "
          "about %.0f of core 1's cycles a guest instruction while running." %
          (g["insns"] / 1e6, g["run_us"] / 1e6, g["insns"] / g["run_us"], g.get("halt_us", 0) / 1e6,
           mhz * g["run_us"] / g["insns"] if mhz else 0))
    o()

    # == where core 1 was
    o("1. Core 1 by what the guest's CPU was doing")
    o("--------------------------------------------")
    o()
    rows = []
    for a in AREAS:
        n = area.get(a, 0)
        rows.append((a, share(n, total), n, AREA_WORDS[a]))
    o.table(["area", "share", "samples", "what"], rows)

    o("2. Core 1 by where its code is")
    o("------------------------------")
    o()
    reg = prof.kv.get("region", {})
    ar = prof.kv.get("area_region", {})
    if not any(hi > lo for lo, hi in prof.ranges.values()):
        o("No code regions in this file (a stand-in on the PC): section 4 names each function's")
        o("ELF section instead.")
        o()
    rows = []
    for i, r in enumerate(REGIONS):
        n = reg.get(r, 0)
        nb = n - (ar.get("halt", [0, 0, 0, 0])[i])
        rows.append((r, share(n, total), share(nb, busy), REGION_WORDS[r]))
    o.table(["region", "of all samples", "of samples not halted", "what"], rows)
    rows = []
    for a in AREAS:
        v = ar.get(a, [0, 0, 0, 0])
        rows.append([a] + ["%d" % x for x in v])
    o("Area against region (samples):")
    o.table(["area"] + REGIONS, rows)
    for r in ("iram", "flash", "rom"):
        lo, hi = prof.ranges.get(r, (0, 0))
        if hi > lo:
            o("  %s: %08x to %08x" % (r, lo, hi))
    o()

    # == the cache counters
    if prof.cache:
        o("3. The cache counters (ESP-IDF's esp_cache_cnt: core 0's and core 1's buses into the")
        o("   flash and PSRAM cache)")
        o("-------------------------------------------------------------------------------------")
        o()
        o("hit: completed accesses; nxtlvl_rd: lines fetched from flash or PSRAM (the true misses);")
        o("miss: stall events, which grow with the time waited on misses (a relative measure);")
        o("nxtlvl_wr: lines written back to PSRAM (esp_cache_cnt.h).")
        o()
        rows = []
        secs = max(samp_s - paused_s, 1e-9)
        gi = g.get("insns", 0)
        for name, core, kind, c in prof.cache:
            def per_s(k):
                return "-" if c.get(k) is None else "%.2f M" % (c[k] / secs / 1e6)
            fills = c.get("nxtlvl_rd")
            hit = c.get("hit")
            ratio = "-" if not hit or fills is None else "%.2f%%" % (100.0 * fills / hit)
            per_k = "-" if not gi or fills is None else "%.1f" % (1000.0 * fills / gi)
            rows.append((name, core, kind, per_s("hit"), per_s("nxtlvl_rd"), ratio, per_s("miss"),
                         per_s("nxtlvl_wr"), per_k))
        o.table(["unit", "core", "kind", "hits/s", "fills/s", "fills/hits", "stall events/s",
                 "writebacks/s", "fills per 1000 guest insns"], rows)

    # == host functions
    o("4. Host functions (core 1's PC, all samples, stalls included)")
    o("-------------------------------------------------------------")
    o()
    byf = collections.Counter()
    byf_area = collections.defaultdict(collections.Counter)
    regions_of = {}
    for pc, a, n in prof.host:
        name = syms.name(pc)
        byf[name] += n
        byf_area[name][AREAS[a]] += n
        regions_of[name] = region_of(prof, elf, pc)
    coarse_n = 0
    for line, a, n in prof.coarse:
        name = syms.name(line + 32)
        byf[name] += n
        byf_area[name][AREAS[a]] += n
        regions_of[name] = region_of(prof, elf, line)
        coarse_n += n
    tables = prof.tables
    lost = tables.get("coarse", (0, 0, 0))[2]
    rows = []
    for name, n in byf.most_common(top):
        ar = byf_area[name]
        split = ", ".join("%s %d" % (a, ar[a]) for a in AREAS if ar[a])
        rows.append((name, share(n, total), n, regions_of.get(name, "?"), split))
    o.table(["function", "share", "samples", "region", "by area"], rows)
    if coarse_n or lost:
        o("(%d samples counted by 64-byte line only, named by the line's middle here and in 5 and "
          "6; %d in the totals only.)" % (coarse_n, lost))
        o()

    # == fast_exec() by handler and by line, from the source; the coarse
    # table's samples at their line's middle (a full host table puts the
    # PCs first seen late there)
    every = list(prof.host) + [(line + 32, a, n) for line, a, n in prof.coarse]
    pcs = sorted(set(pc for pc, a, n in every))
    lines = addr2line(a2l, elf_path, pcs)
    infast = sum(n for pc, a, n in every if (syms.func(pc) or ("",))[0] == "fast_exec")
    if lines and infast:
        byh = collections.Counter()
        own = collections.Counter()
        for pc, a, n in every:
            f = syms.func(pc)
            fr = lines.get(pc)
            if not f or f[0] != "fast_exec" or not fr:
                continue
            path, line = split_loc(fr[-1][1])
            own[(path, line)] += n
            byh[src.handler(path, line)] += n
        o("5. fast_exec() by handler (each PC's line in fast_exec(), under the label above it in")
        o("   %s)" % src.describe("i386_fast.h"))
        o("-" * 88)
        o()
        rows = [(h, share(n, infast), share(n, total), n) for h, n in byh.most_common(top)]
        o.table(["handler", "of fast_exec", "of all", "samples"], rows)
        o("fast_exec()'s own lines (macros and inlined helpers count at the line that uses them):")
        rows = [("%s:%s" % (os.path.basename(p), l), share(n, total), n, src.text(p, l))
                for (p, l), n in own.most_common(top)]
        o.table(["line", "share", "samples", "source"], rows)
    else:
        fh = fast_handlers(elf, syms)
        if fh and infast:
            start, end, labels = fh
            starts = [a for a, _ in labels]
            byh = collections.Counter()
            for pc, a, n in every:
                if start <= pc < end:
                    i = bisect.bisect_right(starts, pc) - 1
                    byh[labels[i][1] if i >= 0 else "fast_exec's entry, before the first handler"] += n
            o("5. fast_exec() by handler, roughly (no addr2line: the jump tables' label at or before")
            o("   each PC; the compiler moves code between handlers, so take it as a rough guide)")
            o("-" * 88)
            o()
            rows = [(h, share(n, infast), share(n, total), n) for h, n in byh.most_common(top)]
            o.table(["handler", "of fast_exec", "of all", "samples"], rows)

    # == source lines
    if lines:
        o("6. Source lines (addr2line: the innermost line, inlined code at its own line)")
        o("-----------------------------------------------------------------------------")
        o()
        inner = collections.Counter()
        fn_of = {}
        for pc, a, n in every:
            fr = lines.get(pc)
            if not fr:
                continue
            path, line = split_loc(fr[0][1])
            inner[(path, line)] += n
            fn_of[(path, line)] = fr[0][0]
        rows = [("%s:%s" % (os.path.basename(p), l), fn_of.get((p, l), "?"), share(n, total), n,
                 src.text(p, l)) for (p, l), n in inner.most_common(top)]
        o.table(["line", "function", "share", "samples", "source"], rows)
    elif a2l is None:
        o("6. Source lines: no addr2line for this ELF's machine (--addr2line).")
        o()

    # == the guest
    o("7. The guest")
    o("------------")
    o()
    md = prof.kv.get("mode", {})
    ma = prof.kv.get("mode_area", {})
    rows = []
    for x in MODES:
        v = ma.get(x, [0, 0, 0, 0])
        rows.append([x, share(md.get(x, 0), total)] + ["%d" % n for n in v])
    o.table(["mode", "share", "fast", "exec1", "alone", "halt"], rows)
    byreg = collections.Counter()
    byreg_area = collections.defaultdict(collections.Counter)
    bypage = collections.Counter()
    page_area = collections.defaultdict(collections.Counter)
    for a, mo, cpl, cs, page, guess, n in prof.guest:
        r = guest_region(mo, cpl, page, cs)
        byreg[(MODES[mo], cpl, r)] += n
        byreg_area[(MODES[mo], cpl, r)][AREAS[a]] += n
        bypage[(MODES[mo], cpl, cs, page)] += n
        page_area[(MODES[mo], cpl, cs, page)][AREAS[a]] += n
    rows = []
    for k, n in byreg.most_common(top):
        ar = byreg_area[k]
        rows.append((k[0], k[1], k[2], share(n, total), ", ".join("%s %d" % (a, ar[a]) for a in AREAS if ar[a])))
    o("By region:")
    o.table(["mode", "cpl", "region", "share", "by area"], rows)
    rows = []
    for k, n in bypage.most_common(top):
        ar = page_area[k]
        rows.append((k[0], k[1], "%04X" % k[2], "%05X000" % k[3], share(n, total),
                     ", ".join("%s %d" % (a, ar[a]) for a in AREAS if ar[a])))
    o("By code page (linear; in the fast interpreter the page tiny386's fetch cache holds):")
    o.table(["mode", "cpl", "CS", "page", "share", "by area"], rows)
    if prof.meta.get("page_guess"):
        o("(%d fast-interpreter samples had no cached page and were counted at the step's start.)"
          % prof.meta["page_guess"])
        o()

    # == handed-over instructions
    if prof.ops:
        tot_ops = sum(n for k, n in prof.ops)
        o("8. The instructions tiny386's cpu_exec1() was doing (time-weighted)")
        o("--------------------------------------------------------------------")
        o()
        rows = []
        for k, n in sorted(prof.ops, key=lambda x: -x[1])[:top]:
            words, name, mode = op_key_words(k)
            who = "alone" if k >> 26 & 1 else "exec1"
            rows.append((words, name, mode, who, share(n, tot_ops), n))
        o.table(["bytes", "instruction", "mode", "area", "of these", "samples"], rows)
        if prof.meta.get("op_unread"):
            o("(%d samples in cpu_exec1() whose bytes could not be read: the instruction was not in "
              "the page tiny386's fetch cache held.)" % prof.meta["op_unread"])
            o()

    # == the tables
    o("9. The tables")
    o("-------------")
    o()
    rows = [(k, v[0], v[1], v[2]) for k, v in sorted(prof.tables.items())]
    o.table(["table", "slots", "used", "samples lost"], rows)
    o("Margins (+-) are 95 per cent from the number of samples alone. A guest EIP read while the")
    o("fast interpreter runs is where its step began, up to 512 instructions back; the code page is")
    o("the one it was running in. Fields are read without stopping core 1: now and then a sample")
    o("mixes two moments (s31/PROFILE.md 3).")


def region_of(prof, elf, pc):
    for r in ("iram", "flash", "rom"):
        lo, hi = prof.ranges.get(r, (0, 0))
        if lo <= pc < hi:
            return r
    s = elf.section_at(pc)
    return s["name"] if s else "other"


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("profile")
    ap.add_argument("elf")
    ap.add_argument("--rom-elf")
    ap.add_argument("--addr2line")
    ap.add_argument("--top", type=int, default=25)
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--src")
    ap.add_argument("--since")
    ap.add_argument("-o", dest="out")
    a = ap.parse_args(argv)
    src_root = a.src or os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                      "..", ".."))
    prof = parse_profile(a.profile)
    if a.since:
        prof = subtract(prof, parse_profile(a.since), (a.profile, a.since))
    with open(a.elf, "rb") as f:
        sha = hashlib.sha256(f.read()).hexdigest()
    # The file names the ELF by its SHA-256: all 64 hex digits since v5.1's
    # reviews; ESP-IDF's esp_app_get_elf_sha256() gives only the first
    # CONFIG_APP_RETRIEVE_LEN_ELF_SHA of them (9 by default, at least 8),
    # which v5.1's first images wrote.  A file's digits are taken as a
    # prefix, from 8 up.
    want = prof.meta.get("elf_sha256", "").strip().lower()
    if re.match(r"^[0-9a-f]{8,64}$", want) and sha.startswith(want):
        prof.meta["elf_check"] = ("The ELF's SHA-256 matches the file's %d hex digits of it%s." %
                                  (len(want), "" if len(want) == 64 else " (the file has no more)"))
    else:
        msg = ("%s has SHA-256 %s, but %s was written by the ELF %s: the names would be wrong" %
               (a.elf, sha[:16], a.profile, want[:16] or "(none named)"))
        if want and not re.match(r"^[0-9a-f]{8,64}$", want):
            msg = ("%s names its ELF by %r, not by 8 to 64 hex digits of its SHA-256: it cannot be "
                   "checked against %s" % (a.profile, want[:70], a.elf))
        if not a.force:
            raise SystemExit(msg + ". The release's images\\*\\tiny386.elf of the same version "
                             "and board has it; --force to go on anyway.")
        sys.stderr.write("warning: " + msg + "\n")
        prof.meta["elf_check"] = "WARNING (--force): " + msg + "."
    elf = Elf(a.elf)
    rom_path = find_rom_elf(prof, a.rom_elf)
    rom = Elf(rom_path) if rom_path and os.path.exists(rom_path) else None
    a2l = find_addr2line(elf, a.addr2line)
    out = open(a.out, "w") if a.out else sys.stdout
    try:
        report(prof, elf, a.elf, rom, a2l, a.top, out, Source(src_root))
        if rom_path:
            out.write("\nROM symbols from %s.\n" % rom_path)
        if a2l:
            out.write("Source lines by %s.\n" % a2l)
    finally:
        if a.out:
            out.close()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
