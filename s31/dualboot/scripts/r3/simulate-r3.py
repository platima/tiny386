#!/usr/bin/env python3
# Round 3 flash simulation for the Korvo-1 dual-boot set (2026-10-21).
#
# The round 2 model (scripts\simulate-flash.py), driven this time by the write
# lists taken out of the .bat files themselves (check-r3.py writes them to
# write-lists.json), with the v3.1 images, and with flash-dualboot.bat's two
# esptool runs as they now are: tiny386 at 0xE20000 first, then the bootloader
# and table.  Adds the states a cut-short install leaves.
#
# esptool 5.4 write-flash, as modelled: data padded to 4 bytes with 0xFF;
# every 4 KiB sector the data touches is erased first, including the bytes
# before an unaligned start; headers left alone (-fm keep -ff keep -fs keep).
# Start state: be5b725's flash.bin at 0x0, a marker byte (0x5A) from its end
# (0xC20000) up, standing for "whatever was there".
#
# Reads only.  Writes only under OUT.
#
#   python3 -I simulate-r3.py OUT write-lists.json
import hashlib, json, os, struct, subprocess, sys

OUT, LISTS = sys.argv[1], sys.argv[2]
os.makedirs(OUT, exist_ok=True)
SIZE, SEC, MARK = 0x1000000, 0x1000, 0x5A
TEST_OFF, TEST_SIZE = 0xE20000, 0x1E0000
SETS = {k: [(a, f) for a, f in v] for k, v in json.load(open(LISTS)).items()}


def rd(p):
    with open(p, "rb") as f:
        return f.read()


def sha(b):
    return hashlib.sha256(b).hexdigest()[:16]


DUAL_BL = [f for a, f in SETS["dualboot-run2"] if a == 0x2000][0]
TINY = SETS["dualboot-run1"][0][1]
BE_FLASHBIN = SETS["linux-be5b725-full"][0][1]
KNOWN_BL = [(DUAL_BL, "dual-boot (test pin GPIO42)"),
            ([f for a, f in SETS["linux-be5b725-small"] if a == 0x2000][0], "be5b725"),
            ([f for a, f in SETS["linux-release-small"] if a == 0x2000][0], "release v2026.09.20"),
            ([f for a, f in SETS["standalone"] if a == 0x2000][0], "tiny386 own")]
LINUX_PARTS = [("nvs", 0x11000, 0x6000), ("otadata", 0x17000, 0x2000),
               ("phy_init", 0x19000, 0x1000), ("factory", 0x20000, 0x200000),
               ("opensbi", 0x220000, 0x80000), ("linux", 0x2A0000, 0x780000),
               ("initramfs", 0xA20000, 0x200000)]


def short(p):
    for pre in ("/mnt/d/Repos/esp32-s31-linux-release/tiny386-idf/v3/", "/mnt/d/Repos/esp32-s31-linux-release/"):
        if p.startswith(pre):
            return p[len(pre):]
    return p


def apply(flash, writes, log, cut=None):
    """esptool write-flash; cut=(index, nbytes) stops inside write number index
    after its sectors are erased and nbytes of it are written."""
    touched = []
    for i, (addr, path) in enumerate(writes):
        data = rd(path)
        if len(data) % 4:
            data += b"\xff" * (4 - len(data) % 4)
        start = addr - addr % SEC
        end = -(-(addr + len(data)) // SEC) * SEC
        assert end <= SIZE, path
        if cut and cut[0] == i:
            # esptool erases as it goes; model the worst case for what is left:
            # every sector of this file erased, only the first nbytes written
            flash[start:end] = b"\xff" * (end - start)
            flash[addr:addr + cut[1]] = data[:cut[1]]
            log.append("    write 0x%07x %-55s CUT SHORT after %d of %d bytes" % (addr, short(path), cut[1], len(data)))
            return touched
        flash[start:end] = b"\xff" * (end - start)
        flash[addr:addr + len(data)] = data
        touched.append((start, end))
        log.append("    write 0x%07x %-55s %9d bytes, erased 0x%07x to 0x%07x" %
                   (addr, short(path), len(rd(path)), start, end - 1))
    s = sorted(touched)
    for (a0, a1), (b0, b1) in zip(s, s[1:]):
        assert a1 <= b0, "overlap %x-%x %x-%x" % (a0, a1, b0, b1)
    return touched


def changed_ranges(a, b):
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
        ents.append((e[12:28].split(b"\0")[0].decode(), typ, sub, off, size))
    return ents


def image_info(flash, off, length, tag):
    p = os.path.join(OUT, "region-%s-0x%x.bin" % (tag.split(".")[0], off))
    with open(p, "wb") as f:
        f.write(bytes(flash[off:off + length]))
    r = subprocess.run(["esptool", "-c", "esp32s31", "image-info", p], capture_output=True, text=True)
    keep = [l for l in r.stdout.splitlines()
            if l.startswith(("Chip ID", "Checksum", "Validation hash", "Project name", "App version"))]
    os.remove(p)
    good = r.returncode == 0 and "(invalid)" not in r.stdout and "Chip ID: 32" in r.stdout
    return ("valid" if good else "NOT VALID") + " (exit %d); " % r.returncode + "; ".join(keep)


def bl_name(flash):
    for path, label in KNOWN_BL:
        d = rd(path)
        if bytes(flash[0x2000:0x2000 + len(d)]) == d and all(x == 0xFF for x in flash[0x2000 + len(d):0x8000]):
            return label
    return "none of the known ones"


def boot_choice(flash, key_held):
    """ESP-IDF 188e3e55 bootloader_start.c 84 to 130 and bootloader_utility.c
    376 to 420 and 582 to 621: no ota_N apps, so factory; the test partition
    only while GPIO42 is held low, and only with the dual-boot bootloader
    (CONFIG_BOOTLOADER_APP_TEST); a held key and no test partition resets."""
    name = bl_name(flash)
    if name == "none of the known ones":
        return ("no valid bootloader at 0x2000: ROM download mode needed", None)
    ents = parse_table(flash)
    if not ents:
        return ("no partition table: the bootloader stops", None)
    factory = [e for e in ents if e[1] == 0 and e[2] == 0x00]
    test = [e for e in ents if e[1] == 0 and e[2] == 0x20]
    if key_held and name.startswith("dual-boot"):
        return ("test", test[0]) if test else ("resets while the key is held (no test partition)", None)
    if factory:
        return ("factory", factory[0])
    if test:
        return ("test (fallback, the only app)", test[0])
    return ("none", None)


def describe(flash, tag, rep):
    ents = parse_table(flash)
    rep.append("  table at 0x8000: %d entries, sha256 %s: %s" % (
        len(ents), sha(bytes(flash[0x8000:0x8C00])),
        ", ".join("%s@0x%x" % (n, o) for n, t, s, o, z in ents)))
    rep.append("  bootloader at 0x2000: %s" % bl_name(flash))
    for key in (False, True):
        what, e = boot_choice(flash, key)
        line = "  GPIO42 %-13s -> %s" % ("held low:" if key else "high (no key):", what)
        if e:
            line += " '%s' at 0x%x: %s" % (e[0], e[3], image_info(flash, e[3], e[4], tag))
        rep.append(line)


def main():
    rep = []
    base = bytearray(b"\xff" * SIZE)
    fb = rd(BE_FLASHBIN)
    base[0:len(fb)] = fb
    base[len(fb):] = bytes([MARK]) * (SIZE - len(fb))
    rep.append("Write lists: from the .bat files (check-r3.py, write-lists.json).")
    rep.append("tiny386 image: %s, %d bytes, sha %s" % (short(TINY), len(rd(TINY)), sha(rd(TINY))))
    rep.append("Start: be5b725 flash.bin (0x%x bytes) at 0x0, marker 0x%02X from 0x%x to the end" % (len(fb), MARK, len(fb)))
    describe(base, "start", rep)
    results = {}

    def run(name, start, steps, cut=None):
        rep.append("")
        rep.append("== %s" % name)
        f = bytearray(start)
        log = []
        for i, s in enumerate(steps):
            log.append("  esptool run %d, %s:" % (i + 1, s))
            apply(f, SETS[s], log, cut if (cut and i == len(steps) - 1) else None)
        rep.extend(log)
        ch = changed_ranges(start, f)
        rep.append("  changed against the state before (4 KiB granules): " +
                   (", ".join("0x%07x to 0x%07x" % (a, b) for a, b in ch) or "nothing"))
        linux_changed = [n for n, o, z in LINUX_PARTS if start[o:o + z] != f[o:o + z]]
        rep.append("  Linux partitions changed: %s; 0xE20000 to the end: %s" % (
            ", ".join(linux_changed) or "none", "changed" if start[TEST_OFF:] != f[TEST_OFF:] else "unchanged"))
        describe(f, name, rep)
        results[name] = (f, ch, linux_changed)
        return f

    t = rd(TINY)
    A = run("1. flash-dualboot.bat (all) over Linux be5b725", base, ["dualboot-run1", "dualboot-run2"])
    rep.append("  check: tiny386 at 0xE20000 = the file: %s; %d of %d bytes, %d spare (%.1f per cent)" % (
        bytes(A[TEST_OFF:TEST_OFF + len(t)]) == t, len(t), TEST_SIZE, TEST_SIZE - len(t), 100.0 * (TEST_SIZE - len(t)) / TEST_SIZE))
    rep.append("  check: nothing from 0x9000 to 0xE1FFFF changed: %s" % (A[0x9000:TEST_OFF] == base[0x9000:TEST_OFF]))
    run("1a. run 1 only (run 2 not started or failed at once)", base, ["dualboot-run1"])
    run("1b. run 1 cut short half way through tiny386", base, ["dualboot-run1"], cut=(0, len(t) // 2 & ~3))
    S1 = bytearray(base)
    apply(S1, SETS["dualboot-run1"], [])
    bl = rd(DUAL_BL)
    run("1c. run 2 cut short inside the bootloader (4 KiB of it written)", S1, ["dualboot-run2"], cut=(0, 0x1000))
    SETS["dualboot-run2-bootloader-only"] = SETS["dualboot-run2"][:1]
    run("1d. run 2 stopped between its two files (bootloader written, Linux table still there)", S1,
        ["dualboot-run2-bootloader-only"])
    run("1e. run 2 cut short at the start of the table (its sector erased, nothing written)", S1,
        ["dualboot-run2"], cut=(1, 0))
    run("2. flash-dualboot.bat app (the same file again)", A, ["dualboot-app"])
    run("2a. flash-dualboot.bat all again over the dual boot", A, ["dualboot-run1", "dualboot-run2"])
    D = run("3. back-to-linux.bat (small be5b725, the default) after 1", A, ["linux-be5b725-small"])
    rep.append("  check: 0x0 to 0xC1FFFF = the start state: %s; tiny386 still at 0xE20000: %s" % (
        D[:0xC20000] == base[:0xC20000], bytes(D[TEST_OFF:TEST_OFF + len(t)]) == t))
    rep.append("  check: the whole flash = the start state but 0xE20000 up: %s" % (D[:TEST_OFF] == base[:TEST_OFF]))
    run("4. back-to-linux.bat small release after 1", A, ["linux-release-small"])
    E = run("5. back-to-linux.bat full release after 1", A, ["linux-release-full"])
    rep.append("  check: tiny386 still at 0xE20000: %s" % (bytes(E[TEST_OFF:TEST_OFF + len(t)]) == t))
    F = run("6. back-to-linux.bat full (be5b725 flash.bin) after 1", A, ["linux-be5b725-full"])
    rep.append("  check: 0x0 to 0xC1FFFF = the start state: %s" % (F[:0xC20000] == base[:0xC20000]))
    G = run("7. flash-standalone.bat over Linux be5b725", base, ["standalone"])
    run("8. standalone, then back-to-linux.bat full (be5b725)", G, ["linux-be5b725-full"])
    H = results["8. standalone, then back-to-linux.bat full (be5b725)"][0]
    rep.append("  check: 0x0 to 0xC1FFFF = the start state: %s" % (H[:0xC20000] == base[:0xC20000]))
    run("9. standalone, then back-to-linux.bat full release", G, ["linux-release-full"])
    G2 = run("10. flash-standalone.bat over the dual boot (1)", A, ["standalone"])
    run("11. standalone over the dual boot, then back-to-linux.bat full (be5b725)", G2, ["linux-be5b725-full"])
    run("12. flash-dualboot.bat after 5 (release Linux)", E, ["dualboot-run1", "dualboot-run2"])

    # merge-bin cross-check of the two runs as one list
    mb = os.path.join(OUT, "merged-dualboot.bin")
    lst = SETS["dualboot-run2"] + SETS["dualboot-run1"]
    args = ["esptool", "-c", "esp32s31", "merge-bin", "-o", mb, "-fm", "keep", "-ff", "keep", "-fs", "keep",
            "--target-offset", "0", "--pad-to-size", "16MB"]
    for a, p in lst:
        args += ["0x%x" % a, p]
    r = subprocess.run(args, capture_output=True, text=True)
    rep.append("")
    rep.append("== esptool merge-bin of the dual-boot writes (exit %d)" % r.returncode)
    if r.returncode == 0:
        m = rd(mb)
        sim = bytearray(b"\xff" * SIZE)
        apply(sim, lst, [])
        rep.append("  merged image %d bytes; equal to this model on an erased chip: %s" % (len(m), m == bytes(sim)))
        os.remove(mb)
    else:
        rep.append(r.stdout[-2000:] + r.stderr[-2000:])
    print("\n".join(rep))


main()
