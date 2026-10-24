#!/usr/bin/env python3
# Flash simulation for the Korvo-1 dual-boot set, without a board.
#
# Models a 16 MiB flash and applies each write list the way esptool 5.4
# write-flash does it (data padded to 4 bytes with 0xFF; every 4 KiB sector the
# data touches is erased first, including the bytes before an unaligned start;
# image headers left alone because flash mode, frequency and size are "keep").
# The starting state is the be5b725 flash.bin at 0x0 with a marker byte (0x5A)
# from 0xC20000 up, standing for "whatever was there".
#
# Reads only.  Writes only under OUT (first argument).
#
#   python3 -I simulate-flash.py OUT W
#     OUT: output folder;  W: ~/tiny386-idf-scratch/dualboot
import hashlib, os, struct, subprocess, sys

OUT, W = sys.argv[1], sys.argv[2]
os.makedirs(OUT, exist_ok=True)
SIZE, SEC, MARK = 0x1000000, 0x1000, 0x5A
TEST_OFF, TEST_SIZE = 0xE20000, 0x1E0000

def rd(p):
    with open(os.path.join(W, p), "rb") as f:
        return f.read()

def sha(b):
    return hashlib.sha256(b).hexdigest()[:16]

DUAL_BL = "dual/build/bootloader/bootloader/bootloader.bin"
DUAL_PT = "dual/build/bootloader/partition_table/partition-table.bin"
REL = "inputs/release-v2026.09.20/"
BE = "inputs/be5b725/"
V2 = "inputs/v2/"
TINY = V2 + "tiny386.bin"
BLANK = "r2/blank-0x9000-0x1ffff.bin"

# The write lists the .bat files use (same addresses, same files).
SETS = {
    "dualboot-all": [(0x2000, DUAL_BL), (0x8000, DUAL_PT), (TEST_OFF, TINY)],
    "dualboot-app": [(TEST_OFF, TINY)],
    "linux-release-small": [(0x2000, REL + "bootloader/bootloader/bootloader.bin"),
                            (0x8000, REL + "bootloader/partition_table/partition-table.bin")],
    "linux-release-full": [(0x2000, REL + "bootloader/bootloader/bootloader.bin"),
                           (0x8000, REL + "bootloader/partition_table/partition-table.bin"),
                           (0x9000, BLANK),
                           (0x20000, REL + "bootloader/s31-linux-loader.bin"),
                           (0x220000, REL + "opensbi.bin"),
                           (0x2A0000, REL + "Image"),
                           (0xA1FFF4, REL + "linux.size"),
                           (0xA20000, REL + "initramfs.cpio")],
    "linux-be5b725-small": [(0x2000, BE + "bootloader.bin"), (0x8000, BE + "partition-table.bin")],
    "linux-be5b725-full": [(0x0, BE + "flash.bin")],
    "standalone": [(0x2000, V2 + "bootloader/bootloader.bin"),
                   (0x8000, V2 + "partition_table/partition-table.bin"),
                   (0x10000, TINY)],
}

LINUX_PARTS = [("nvs", 0x11000, 0x6000), ("otadata", 0x17000, 0x2000),
               ("phy_init", 0x19000, 0x1000), ("factory", 0x20000, 0x200000),
               ("opensbi", 0x220000, 0x80000), ("linux", 0x2A0000, 0x780000),
               ("initramfs", 0xA20000, 0x200000)]

def apply(flash, writes, log):
    """esptool write-flash semantics; returns the erased/written ranges."""
    touched = []
    for addr, path in writes:
        data = rd(path)
        if len(data) % 4:
            data += b"\xff" * (4 - len(data) % 4)
        start = addr - addr % SEC
        end = -(-(addr + len(data)) // SEC) * SEC
        assert end <= SIZE, path
        flash[start:end] = b"\xff" * (end - start)
        flash[addr:addr + len(data)] = data
        touched.append((start, end))
        log.append("    write 0x%07x %-55s %9d bytes, erased 0x%07x to 0x%07x" %
                   (addr, path, len(rd(path)), start, end - 1))
    # esptool refuses overlapping writes; check that the lists have none
    s = sorted(touched)
    for (a0, a1), (b0, b1) in zip(s, s[1:]):
        assert a1 <= b0, "overlap %x-%x %x-%x" % (a0, a1, b0, b1)
    return touched

def changed_ranges(a, b):
    """Byte ranges where a and b differ, merged at 4 KiB granularity."""
    out, cur = [], None
    for s in range(0, SIZE, SEC):
        if a[s:s + SEC] != b[s:s + SEC]:
            if cur and cur[1] == s:
                cur[1] = s + SEC
            else:
                cur = [s, s + SEC]
                out.append(cur)
    return [(x, y - 1) for x, y in out]

def parse_table(flash):
    ents = []
    for i in range(0, 0xC00, 32):
        e = flash[0x8000 + i:0x8000 + i + 32]
        magic, typ, sub, off, size = struct.unpack("<HBBII", e[:12])
        if magic != 0x50AA:
            break
        name = e[12:28].split(b"\0")[0].decode()
        ents.append((name, typ, sub, off, size))
    return ents

def image_info(flash, off, length, tag):
    p = os.path.join(OUT, "region-%s-0x%x.bin" % (tag, off))
    with open(p, "wb") as f:
        f.write(bytes(flash[off:off + length]))
    r = subprocess.run(["esptool", "--chip", "esp32s31", "image-info", p],
                       capture_output=True, text=True)
    keep = [l for l in r.stdout.splitlines()
            if l.startswith(("Image size", "Chip ID", "Checksum", "Validation hash",
                             "Project name", "App version", "ESP-IDF"))]
    os.remove(p)
    return "exit %d; " % r.returncode + "; ".join(keep)

def boot_choice(flash, key_held):
    """What the ESP-IDF bootloader would pick (bootloader_start.c 84 to 130,
    bootloader_utility.c 376 to 420): no ota_N apps, so factory; the test
    partition only while GPIO42 is held low and only if the build has
    CONFIG_BOOTLOADER_APP_TEST (true for the dual-boot bootloader only)."""
    ents = parse_table(flash)
    factory = [e for e in ents if e[1] == 0 and e[2] == 0x00]
    test = [e for e in ents if e[1] == 0 and e[2] == 0x20]
    ota_apps = [e for e in ents if e[1] == 0 and 0x10 <= e[2] < 0x20]
    has_test_pin = sha(bytes(flash[0x2000:0x2000 + len(rd(DUAL_BL))])) == sha(rd(DUAL_BL))
    if key_held and has_test_pin:
        return ("test", test[0]) if test else ("reset loop: no test partition", None)
    if ota_apps:
        return ("ota (not modelled)", None)
    return ("factory", factory[0]) if factory else ("none", None)

def describe(flash, tag, rep):
    ents = parse_table(flash)
    rep.append("  partition table at 0x8000 (%d entries, sha256 %s):" %
               (len(ents), sha(bytes(flash[0x8000:0x8C00]))))
    for n, t, s, o, z in ents:
        rep.append("    %-10s type %d subtype 0x%02x  0x%07x  0x%06x" % (n, t, s, o, z))
    known = [(DUAL_BL, "dual-boot (test pin GPIO42)"),
             (BE + "bootloader.bin", "be5b725"),
             (REL + "bootloader/bootloader/bootloader.bin", "release v2026.09.20"),
             (V2 + "bootloader/bootloader.bin", "tiny386 v2 own")]
    match = "unknown"
    for path, label in known:
        d = rd(path)
        if (bytes(flash[0x2000:0x2000 + len(d)]) == d and
                all(x == 0xFF for x in flash[0x2000 + len(d):0x8000])):
            match = label + " (" + path + ", rest of the sector 0xFF)"
    rep.append("  bootloader at 0x2000: %s" % match)
    rep.append("    image-info: " + image_info(flash, 0x2000, 0x6000, tag))
    for key in (False, True):
        what, e = boot_choice(flash, key)
        line = "  boot with GPIO42 %s: %s" % ("held low" if key else "high (no key)", what)
        if e:
            line += " '%s' at 0x%x" % (e[0], e[3])
            rep.append(line)
            rep.append("    image-info: " + image_info(flash, e[3], e[4], tag))
        else:
            rep.append(line)

def main():
    rep = []
    base = bytearray(b"\xff" * SIZE)
    fb = rd(BE + "flash.bin")
    base[0:len(fb)] = fb
    base[len(fb):] = bytes([MARK]) * (SIZE - len(fb))
    rep.append("Start: be5b725 flash.bin (0x%x bytes) at 0x0, marker 0x%02X from 0x%x to the end" %
               (len(fb), MARK, len(fb)))
    describe(base, "start", rep)

    def run(name, start, steps):
        rep.append("")
        rep.append("== %s" % name)
        f = bytearray(start)
        log = []
        for s in steps:
            log.append("  %s:" % s)
            apply(f, SETS[s], log)
        rep.extend(log)
        ch = changed_ranges(start, f)
        rep.append("  bytes changed against the state before (4 KiB granules):")
        for a, b in ch:
            rep.append("    0x%07x to 0x%07x" % (a, b))
        hi = max([b for a, b in ch], default=-1)
        rep.append("  highest changed byte 0x%x; at or above 0xE20000: %s" % (hi, hi >= TEST_OFF))
        for n, o, z in LINUX_PARTS:
            same = start[o:o + z] == f[o:o + z]
            rep.append("    Linux %-9s 0x%07x..0x%07x %s" % (n, o, o + z - 1,
                       "unchanged" if same else "CHANGED"))
        same_t = start[TEST_OFF:] == f[TEST_OFF:]
        rep.append("    region 0xE20000..0xFFFFFF %s" % ("unchanged" if same_t else "CHANGED"))
        describe(f, name, rep)
        return f

    A = run("1. dual-boot install over Linux be5b725", base, ["dualboot-all"])
    t = rd(TINY)
    rep.append("  tiny386.bin at 0xE20000 matches the file: %s; %d of %d bytes used, %d spare (%.1f per cent)" %
               (bytes(A[TEST_OFF:TEST_OFF + len(t)]) == t, len(t), TEST_SIZE, TEST_SIZE - len(t),
                100.0 * (TEST_SIZE - len(t)) / TEST_SIZE))
    run("2. tiny386 update only (same file again)", A, ["dualboot-app"])
    C = run("3. back to Linux, small, release files", A, ["linux-release-small"])
    D = run("4. back to Linux, small, be5b725 files", A, ["linux-be5b725-small"])
    rep.append("  0x0..0xC1FFFF equal to the start state: %s" % (D[:0xC20000] == base[:0xC20000]))
    rep.append("  0xE20000.. still holds tiny386.bin: %s" % (bytes(D[TEST_OFF:TEST_OFF + len(t)]) == t))
    E = run("5. back to Linux, full, release (back-to-linux.bat default)", A, ["linux-release-full"])
    rep.append("  0xE20000.. still holds tiny386.bin: %s" % (bytes(E[TEST_OFF:TEST_OFF + len(t)]) == t))
    F = run("6. back to Linux, full, be5b725 flash.bin", A, ["linux-be5b725-full"])
    rep.append("  0x0..0xC1FFFF equal to the start state: %s" % (F[:0xC20000] == base[:0xC20000]))
    G = run("7. standalone tiny386 over Linux be5b725", base, ["standalone"])
    H = run("8. standalone, then back to Linux, full, release", G, ["linux-release-full"])
    # compare with the release written over the start state: what is left of tiny386
    E0 = bytearray(base)
    apply(E0, SETS["linux-release-full"], [])
    left = changed_ranges(E0, H)
    rep.append("  differences from 'release full over plain be5b725' (tiny386 leftovers):")
    for a, b in left:
        rep.append("    0x%07x to 0x%07x" % (a, b))
    I = run("9. standalone, then back to Linux, full, be5b725 flash.bin", G, ["linux-be5b725-full"])
    rep.append("  0x0..0xC1FFFF equal to the start state: %s" % (I[:0xC20000] == base[:0xC20000]))
    run("10. dual-boot install again after 5 (release Linux)", E, ["dualboot-all"])

    # merge-bin cross-check: esptool's own handling of the dual-boot write list
    mb = os.path.join(OUT, "merged-dualboot-all.bin")
    args = ["esptool", "--chip", "esp32s31", "merge-bin", "-o", mb,
            "--flash-mode", "keep", "--flash-freq", "keep", "--flash-size", "keep",
            "--target-offset", "0", "--pad-to-size", "16MB"]
    for a, p in SETS["dualboot-all"]:
        args += ["0x%x" % a, os.path.join(W, p)]
    r = subprocess.run(args, capture_output=True, text=True)
    rep.append("")
    rep.append("== esptool merge-bin of the dual-boot write list (exit %d)" % r.returncode)
    rep.append("  " + " ".join(a.replace(W + "/", "") for a in args[1:]))
    if r.returncode == 0:
        m = open(mb, "rb").read()
        sim = bytearray(b"\xff" * SIZE)
        apply(sim, SETS["dualboot-all"], [])
        rep.append("  merged image %d bytes; equal to this simulation on an erased chip: %s" %
                   (len(m), m == bytes(sim)))
        os.remove(mb)
    else:
        rep.append(r.stdout[-2000:] + r.stderr[-2000:])
    print("\n".join(rep))

main()
