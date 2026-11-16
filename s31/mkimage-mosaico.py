#!/usr/bin/env python3
"""
A Windows 95 disk image for the ESP-Mosaico's NAND (s31/MOSAICO.md), from a
folder holding the C: drive's files: Linux or WSL, Python 3 and mtools.

    python3 mkimage-mosaico.py --windows "Win95 Shrunk" --boot win95.img \
        [--attrs attrs.txt] [--size 119279616] --out mosaico-win95.img

The image is the NAND's whole disk (119,279,616 bytes unless --size says
less): tiny386's own master boot record (esp/main/sd_part_mbr.h, the code the
Korvo-1's card partitions boot through), one FAT16 partition from sector 2048
to the last whole cylinder, 2 KiB clusters, and the volume's data area starting on a 2 KiB
boundary, so that each cluster Windows writes is one NAND page.  Its free
space is zeros, which the NAND does not store.

From --boot, an existing Windows 95 disk image (an MBR with a FAT16 first
partition, or a bare FAT16 volume), it takes only what the folder has not
got and nothing else can give: the volume boot sector's code (Microsoft's,
which loads IO.SYS) and IO.SYS.  MSDOS.SYS, COMMAND.COM, CONFIG.SYS and
AUTOEXEC.BAT come from the folder if it has them, else from --boot, else
(MSDOS.SYS) one is written, else (COMMAND.COM) WINDOWS\\COMMAND.COM.

--attrs is a list of DOS attributes for files and folders of the folder, as
PowerShell gives it on Windows, where NTFS keeps them (the WSL view of D:
does not):
    Get-ChildItem -LiteralPath 'D:\\...\\Win95 Shrunk' -Recurse -Force |
      ForEach-Object { "{0}`t{1}" -f [int]$_.Attributes, $_.FullName } |
      Out-File -Encoding utf8 attrs.txt
Without it only IO.SYS, MSDOS.SYS, the registry and the fonts Windows marks
get their attributes, by name.

Writes nothing but --out (and a scratch file beside it).  Never touches the
board.
"""
import argparse, os, shutil, struct, subprocess, sys, tempfile

SECTOR = 512
NAND_DISK = 119279616
PART_START = 2048
HEADS, SPT = 16, 63
HERE = os.path.dirname(os.path.abspath(__file__))


def die(msg):
    sys.exit("mkimage-mosaico: " + msg)


def run(*args, ok=(0,)):
    p = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if p.returncode not in ok:
        die("%s failed:\n%s" % (" ".join(args), p.stdout))
    return p.stdout


def mbr_code():
    """the 356 bytes of esp/main/sd_part_mbr.h"""
    h = open(os.path.join(HERE, "..", "esp", "main", "sd_part_mbr.h")).read()
    body = h[h.index("{", h.index("sd_part_mbr_code")) + 1:h.index("};")]
    code = bytes(int(t, 16) for t in body.replace(",", " ").split())
    if len(code) != 356:
        die("sd_part_mbr.h gave %d bytes, not 356" % len(code))
    return code


def chs(lba):
    c = lba // (HEADS * SPT)
    if c > 1023:
        return bytes([0xFE, 0xFF, 0xFF])
    h = (lba // SPT) % HEADS
    s = lba % SPT + 1
    return bytes([h, ((c >> 2) & 0xC0) | s, c & 0xFF])


def source_volume(path):
    """(offset in bytes, boot sector) of --boot's FAT16 volume"""
    with open(path, "rb") as f:
        s0 = f.read(SECTOR)
        if len(s0) < SECTOR or s0[510:512] != b"\x55\xaa":
            die("%s: no boot signature in its first sector" % path)
        if s0[54:62] == b"FAT16   " or s0[54:59] == b"FAT12":
            return 0, s0
        for i in range(4):
            e = s0[446 + 16 * i:462 + 16 * i]
            if e[4] in (0x04, 0x06, 0x0E):
                start = struct.unpack("<I", e[8:12])[0]
                f.seek(start * SECTOR)
                vbr = f.read(SECTOR)
                if vbr[54:62] != b"FAT16   ":
                    die("%s: partition %d is not FAT16" % (path, i + 1))
                return start * SECTOR, vbr
            if e[4] in (0x0B, 0x0C):
                die("%s: partition %d is FAT32; this builder makes FAT16 and needs a "
                    "FAT16 volume's boot code" % (path, i + 1))
    die("%s: no FAT16 volume found" % path)


def vbr_from(path):
    """a FAT12/16 boot sector: the file itself if it is one, else the first
    512 bytes in it that are one (SYS.COM and FORMAT.COM carry Windows 95's,
    OEM name MSWIN4.1, jump EB 3C 90)"""
    d = open(path, "rb").read()
    if len(d) == SECTOR and d[510:512] == b"\x55\xaa":
        return d
    i = -1
    while True:
        i = d.find(b"MSWIN4.", i + 1)
        if i < 3:
            if i < 0:
                die("%s: no FAT16 boot sector in it" % path)
            continue
        b = d[i - 3:i - 3 + SECTOR]
        if len(b) == SECTOR and b[0] == 0xEB and b[1] == 0x3C and b[510:512] == b"\x55\xaa":
            return b


def mtools_env():
    env = dict(os.environ)
    env["MTOOLS_SKIP_CHECK"] = "1"
    return env


def mt(*args, ok=(0,)):
    p = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                       env=mtools_env())
    if p.returncode not in ok:
        die("%s failed:\n%s" % (" ".join(args), p.stdout))
    return p.stdout


def find_ci(folder, name):
    for n in os.listdir(folder):
        if n.lower() == name.lower():
            return os.path.join(folder, n)
    return None


MSDOS_SYS = """[Paths]
WinDir=C:\\WINDOWS
WinBootDir=C:\\WINDOWS
HostWinBootDrv=C

[Options]
BootMulti=1
BootGUI=1
Network=0
AutoScan=2
;
;The following lines are required for compatibility with other programs.
;Do not remove them (MSDOS.SYS needs to be >1024 bytes).
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--windows", required=True)
    ap.add_argument("--boot", help="an existing Windows 95 image (FAT16)")
    ap.add_argument("--vbr", help="instead of --boot: a 512-byte FAT16 boot sector to take "
                    "the code from, or SYS.COM, whose own template is found in it")
    ap.add_argument("--io-sys", help="instead of --boot: IO.SYS (WINBOOT.SYS from the "
                    "setup files' PRECOPY1.CAB)")
    ap.add_argument("--attrs")
    ap.add_argument("--size", type=int, default=NAND_DISK)
    ap.add_argument("--out", required=True)
    ap.add_argument("--label", default="WIN95")
    a = ap.parse_args()

    if a.size > NAND_DISK or a.size % SECTOR:
        die("--size must be whole sectors and at most %d bytes" % NAND_DISK)
    if os.path.exists(a.out):
        die("%s is there already" % a.out)
    win = a.windows
    if not os.path.isdir(os.path.join(win, "WINDOWS")) and not find_ci(win, "WINDOWS"):
        die("%s has no WINDOWS folder" % win)
    if a.boot:
        off, vbr = source_volume(a.boot)
    elif a.vbr and a.io_sys:
        off, vbr = None, vbr_from(a.vbr)
    else:
        die("give --boot, or --vbr and --io-sys")
    if vbr[3:11] not in (b"MSWIN4.0", b"MSWIN4.1"):
        print("note: the boot sector has OEM name %r, not MSWIN4.x" % vbr[3:11])

    tmp = tempfile.mkdtemp(prefix="mkimage-", dir=os.path.dirname(os.path.abspath(a.out)))
    try:
        # what --boot gives: IO.SYS always; the others if the folder lacks them
        if a.boot:
            src = "%s@@%d" % (a.boot, off)
            listing = mt("mdir", "-i", src, "-b", "::/")
        def from_boot(name):
            if not a.boot:
                return None
            want = "::/" + name.upper()
            if want.lower() not in [l.strip().lower() for l in listing.splitlines()]:
                return None
            dst = os.path.join(tmp, name.upper())
            mt("mcopy", "-i", src, "-n", want, dst)
            return dst
        io_sys = a.io_sys or from_boot("IO.SYS")
        if not io_sys:
            die("%s has no IO.SYS" % a.boot)
        boot_files = [("IO.SYS", io_sys, "hsr")]
        msdos = find_ci(win, "MSDOS.SYS") or from_boot("MSDOS.SYS")
        if not msdos:
            msdos = os.path.join(tmp, "MSDOS.SYS")
            text = MSDOS_SYS + "".join(";" + "x" * 69 + "\n" for _ in range(18))
            open(msdos, "w", newline="\r\n").write(text)
        boot_files.append(("MSDOS.SYS", msdos, "hsr"))
        cmd = find_ci(win, "COMMAND.COM") or from_boot("COMMAND.COM") or \
            find_ci(os.path.join(win, "WINDOWS"), "COMMAND.COM")
        if cmd:
            boot_files.append(("COMMAND.COM", cmd, ""))
        for n in ("CONFIG.SYS", "AUTOEXEC.BAT"):
            p = find_ci(win, n) or from_boot(n)
            if p:
                boot_files.append((n, p, ""))
        tpl = os.path.join(tmp, "vbr.bin")
        open(tpl, "wb").write(vbr)

        # the volume: FAT16, 2 KiB clusters, 512 root entries, its data area
        # on a 2 KiB boundary (reserved sectors chosen for it)
        total = a.size // SECTOR
        # the partition ends on a whole cylinder (16 heads, 63 sectors), so
        # that DOS, reading through the BIOS by cylinder, head and sector,
        # reaches all of it: otherwise ScanDisk at start-up "cannot read
        # from the last cluster" and waits behind the logo for an answer
        cyl = HEADS * SPT
        count = total // cyl * cyl - PART_START
        img = a.out + ".part"
        with open(img, "wb") as f:
            f.truncate(a.size)
        vol = "%s@@%d" % (img, PART_START * SECTOR)
        for reserved in (1, 2, 3, 4):
            mt("mformat", "-i", vol, "-T", str(count), "-h", str(HEADS), "-s", str(SPT),
               "-H", str(PART_START), "-c", "4", "-r", "32", "-R", str(reserved),
               "-B", tpl, "-v", a.label, "::")
            with open(img, "rb") as f:
                f.seek(PART_START * SECTOR)
                bs = f.read(SECTOR)
            res = struct.unpack("<H", bs[14:16])[0]
            nfats = bs[16]
            rootents = struct.unpack("<H", bs[17:19])[0]
            fatsz = struct.unpack("<H", bs[22:24])[0]
            data = PART_START + res + nfats * fatsz + rootents * 32 // SECTOR
            if bs[13] == 4 and bs[54:62] == b"FAT16   " and data % 4 == 0:
                break
        else:
            die("mformat would not give FAT16 with 2 KiB clusters on a 2 KiB boundary")
        # the jump, OEM name and boot code are the template's
        with open(img, "r+b") as f:
            f.seek(PART_START * SECTOR)
            bs = bytearray(f.read(SECTOR))
            bs[0:11] = vbr[0:11]
            bs[62:510] = vbr[62:510]
            f.seek(PART_START * SECTOR)
            f.write(bs)

        # the boot files first (IO.SYS's first sectors in one cluster at the
        # start of the data area), then the folder
        for name, path, attr in boot_files:
            mt("mcopy", "-i", vol, "-m", "-o", path, "::/" + name)
        for n in sorted(os.listdir(win)):
            if n.upper() in [b[0] for b in boot_files]:
                continue
            mt("mcopy", "-i", vol, "-s", "-m", "-o", os.path.join(win, n), "::/" + n)

        # attributes
        def attrib(path, rhs):
            flags = ["+" + c for c in rhs]
            mt("mattrib", "-i", vol, *flags, "::/" + path, ok=(0, 1))
        for name, _, attr in boot_files:
            if attr:
                attrib(name, attr)
        if a.attrs:
            base = None
            for line in open(a.attrs, encoding="utf-8-sig", errors="replace"):
                if "\t" not in line:
                    continue
                v, full = line.rstrip("\r\n").split("\t", 1)
                full = full.replace("\\", "/")
                if base is None:
                    w = os.path.abspath(win).replace("\\", "/").rstrip("/")
                    tail = "/" + os.path.basename(w) + "/"
                    i = full.find(tail)
                    base = full[:i + len(tail)] if i >= 0 else ""
                if not full.startswith(base):
                    continue
                rel = full[len(base):]
                v = int(v)
                rhs = ("r" if v & 1 else "") + ("h" if v & 2 else "") + ("s" if v & 4 else "")
                if rhs and os.path.exists(os.path.join(win, rel)):
                    attrib(rel, rhs)
        else:
            for rel in ("WINDOWS/SYSTEM.DAT", "WINDOWS/USER.DAT"):
                if os.path.exists(os.path.join(win, rel)):
                    attrib(rel, "hsr")
            for rel in ("WINDOWS/FONTS",):
                if os.path.exists(os.path.join(win, rel)):
                    attrib(rel, "s")

        # the master boot record
        with open(img, "r+b") as f:
            mbr = bytearray(SECTOR)
            mbr[0:356] = mbr_code()
            mbr[440:444] = os.urandom(4)
            e = bytearray(16)
            e[0] = 0x80
            e[1:4] = chs(PART_START)
            e[4] = 0x06
            e[5:8] = chs(PART_START + count - 1)
            e[8:12] = struct.pack("<I", PART_START)
            e[12:16] = struct.pack("<I", count)
            mbr[446:462] = e
            mbr[510:512] = b"\x55\xaa"
            f.seek(0)
            f.write(mbr)
        os.rename(img, a.out)
        free = mt("mdir", "-i", "%s@@%d" % (a.out, PART_START * SECTOR), "::/")
        print("%s: %d bytes, FAT16 from sector %d, data area at sector %d (2 KiB aligned)"
              % (a.out, a.size, PART_START, data))
        print(free.strip().splitlines()[-1])
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
        if os.path.exists(a.out + ".part"):
            os.remove(a.out + ".part")


if __name__ == "__main__":
    main()
