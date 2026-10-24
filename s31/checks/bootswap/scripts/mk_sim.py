# Builds simulate-flash-bootswap.py from the v3.7 simulation (sim37.py):
# the same ten paths first, unchanged, then the tiny386-default table.
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
def rep(old, new):
    global t
    assert t.count(old) == 1, old[:70]
    t = t.replace(old, new)

rep('''# Flash simulation for the Korvo-1 dual-boot set, without a board.
''', '''# Flash simulation for the Korvo-1 dual-boot set, without a board.
# This copy (simulate-flash-bootswap.py) is the v3.7 one with the
# tiny386-default table added: the ten v3.7 paths first, unchanged, then
# the table-only writes, the way back from tiny386 the default, and the
# cut-off writes that matter for it.  The boot model now also follows the
# bootloader's fallback when the chosen app is not valid
# (bootloader_utility.c 576 to 627).
''')
rep('''DUAL_PT = "/mnt/d/Repos/esp32-s31-linux-release/tiny386-idf/v3/images/korvo1-dualboot/partition-table.bin"
''', '''DUAL_PT = "/mnt/d/Repos/esp32-s31-linux-release/tiny386-idf/v3/images/korvo1-dualboot/partition-table.bin"
DUAL386_PT = "/mnt/d/Repos/esp32-s31-linux-release/tiny386-idf/v3/images/korvo1-dualboot-tiny386-default/partition-table.bin"
''')
rep('''    "dualboot-app": [(TEST_OFF, TINY)],
''', '''    "dualboot-app": [(TEST_OFF, TINY)],
    "dualboot-all-tiny386": [(0x2000, DUAL_BL), (0x8000, DUAL386_PT), (TEST_OFF, TINY)],
    "table-tiny386": [(0x8000, DUAL386_PT)],
    "table-linux": [(0x8000, DUAL_PT)],
''')
rep('''def describe(flash, tag, rep):''', '''def valid(info):
    return (info.startswith("exit 0;") and "Checksum:" in info and "(invalid)" not in info
            and "Validation hash:" in info)

def boot_line(flash, key, tag, rep):
    """bootloader_utility_load_boot_image(): with the key, the test app or a
    reset (lines 582 to 589); without it, the factory app, and if that is
    not valid, the test app (lines 592 to 622)."""
    what, e = boot_choice(flash, key)
    line = "  boot with GPIO42 %s: %s" % ("held low" if key else "high (no key)", what)
    if not e:
        if what == "none":
            line += (": no app partition in the table, or no valid table (esp_partition_table_verify);"
                     " the bootloader resets, over and over")
        rep.append(line)
        return
    info = image_info(flash, e[3], e[4], tag)
    if valid(info):
        rep.append(line + " '%s' at 0x%x" % (e[0], e[3]))
        rep.append("    image-info: " + info)
        return
    rep.append(line + " '%s' at 0x%x: NOT VALID" % (e[0], e[3]))
    rep.append("    image-info: " + info)
    ents = parse_table(flash)
    test = [x for x in ents if x[1] == 0 and x[2] == 0x20]
    if what == "test" or not test:
        rep.append("    -> no bootable partition of that kind: the bootloader resets, over and over while this holds")
        return
    t0 = test[0]
    info2 = image_info(flash, t0[3], t0[4], tag)
    rep.append("    -> falls back to the test app '%s' at 0x%x (%s)" % (t0[0], t0[3], "valid" if valid(info2) else "NOT VALID either: reset"))
    rep.append("    image-info: " + info2)

def describe(flash, tag, rep):''')
rep('''    for key in (False, True):
        what, e = boot_choice(flash, key)
        line = "  boot with GPIO42 %s: %s" % ("held low" if key else "high (no key)", what)
        if e:
            line += " '%s' at 0x%x" % (e[0], e[3])
            rep.append(line)
            rep.append("    image-info: " + image_info(flash, e[3], e[4], tag))
        else:
            rep.append(line)
''', '''    for key in (False, True):
        boot_line(flash, key, tag, rep)
''')
rep('''    run("10. dual-boot install again after 5 (release Linux)", E, ["dualboot-all"])
''', '''    run("10. dual-boot install again after 5 (release Linux)", E, ["dualboot-all"])

    # ---- tiny386 the default (dual386) ----
    names = {sha(rd(DUAL_PT)): "dual-boot, Linux the default (d41cba3c)",
             sha(rd(DUAL386_PT)): "dual-boot, tiny386 the default (5b90d416)",
             sha(rd(BE + "partition-table.bin")): "Linux's own (520538d3)"}
    def table_name(f):
        return names.get(sha(bytes(f[0x8000:0x8C00])), "none of the three")
    slice_path = os.path.join(OUT, "backup-2000-8fff.bin")
    open(slice_path, "wb").write(bytes(base[0x2000:0x9000]))
    SETS["backup-slice"] = [(0x2000, slice_path)]
    rep.append("")
    rep.append("==== tiny386 the default: images/korvo1-dualboot-tiny386-default/partition-table.bin (5b90d416)")
    T = run("11. flash-dualboot.bat COM10 table tiny386, after 1 (the dual boot, Linux the default)", A, ["table-tiny386"])
    rep.append("  table: " + table_name(T))
    rep.append("  only 0x8000 to 0x8FFF differ from 1: %s" % (changed_ranges(A, T) == [(0x8000, 0x8fff)]))
    rep.append("  0x9000 to 0xE1FFFF equal to the start state (all of Linux): %s" % (T[0x9000:TEST_OFF] == base[0x9000:TEST_OFF]))
    T2 = run("12. flash-dualboot.bat COM10 table linux, after 11", T, ["table-linux"])
    rep.append("  table: " + table_name(T2))
    rep.append("  the whole flash equal to state 1 again: %s" % (T2 == A))
    T3 = run("13. flash-dualboot.bat COM10 all tiny386, over Linux be5b725", base, ["dualboot-all-tiny386"])
    rep.append("  table: " + table_name(T3))
    rep.append("  the whole flash equal to state 11: %s" % (T3 == T))
    run("14. flash-dualboot.bat COM10 app, after 11 (same file again)", T, ["dualboot-app"])
    T5 = run("15. back-to-linux.bat COM10 (backup: 0x2000 to 0x8FFF as they were), after 11", T, ["backup-slice"])
    rep.append("  table: " + table_name(T5))
    rep.append("  0x0 to 0xE1FFFF equal to the start state: %s" % (T5[:TEST_OFF] == base[:TEST_OFF]))
    rep.append("  0xE20000.. still holds tiny386.bin: %s" % (bytes(T5[TEST_OFF:TEST_OFF + len(t)]) == t))
    T6 = run("16. back-to-linux.bat COM10 small (be5b725), after 11", T, ["linux-be5b725-small"])
    rep.append("  table: " + table_name(T6))
    rep.append("  0x0 to 0xC1FFFF equal to the start state (be5b725 exactly): %s" % (T6[:0xC20000] == base[:0xC20000]))
    rep.append("  0x0 to 0xE1FFFF equal to the start state: %s" % (T6[:TEST_OFF] == base[:TEST_OFF]))
    T7 = run("17. back-to-linux.bat COM10 full (be5b725 flash.bin), after 11", T, ["linux-be5b725-full"])
    rep.append("  0x0 to 0xC1FFFF equal to the start state: %s" % (T7[:0xC20000] == base[:0xC20000]))
    run("18. back-to-linux.bat COM10 small release, after 11", T, ["linux-release-small"])
    run("19. flash-standalone.bat COM10, after 11", T, ["standalone"])

    rep.append("")
    rep.append("==== cut-off writes with tiny386 the default")
    def cut(name, start, writes, upto, note):
        # the writes applied in order, the last one stopped after its first
        # `upto` bytes (its sectors erased first, as esptool erases before
        # it writes)
        f = bytearray(start)
        log = []
        for i, (addr, path) in enumerate(writes):
            data = rd(path)
            if i == len(writes) - 1:
                s0 = addr - addr % SEC
                e0 = -(-(addr + len(data)) // SEC) * SEC
                f[s0:e0] = b"\\xff" * (e0 - s0)
                f[addr:addr + upto] = data[:upto]
                log.append("    write 0x%07x %s: cut after %d of %d bytes (sectors 0x%07x to 0x%07x erased)" %
                           (addr, path, upto, len(data), s0, e0 - 1))
            else:
                apply(f, [(addr, path)], log)
        rep.append("")
        rep.append("== %s" % name)
        rep.append("  " + note)
        rep.extend(log)
        rep.append("  table: " + table_name(f))
        describe(f, name, rep)
        return f
    cut("C1. table tiny386 cut inside the table (after 1)", A, [(0x8000, DUAL386_PT)], 0x60,
        "esptool erased 0x8000 to 0x8FFF and wrote 96 of 3072 bytes")
    cut("C2. table tiny386 cut at once (sector erased, nothing written)", A, [(0x8000, DUAL386_PT)], 0,
        "the 0x8000 sector erased, then the cable pulled")
    cut("C3. back-to-linux small after 11, cut after the bootloader, the table's sector erased", T,
        [(0x2000, BE + "bootloader.bin"), (0x8000, BE + "partition-table.bin")], 0,
        "be5b725's bootloader written, the table's sector erased, nothing more")
    cut("C3b. back-to-linux small after 11, stopped between the bootloader and the table", T,
        [(0x2000, BE + "bootloader.bin")], len(rd(BE + "bootloader.bin")),
        "be5b725's bootloader written whole, the table still the tiny386-default one")
    cut("C4. flash-dualboot.bat COM10 app after 11, cut half way through tiny386", T,
        [(TEST_OFF, TINY)], len(rd(TINY)) // 2,
        "tiny386 at 0xE20000 half written")
    cut("C5. flash-dualboot.bat COM10 all tiny386 over Linux be5b725, run 2 stopped between the bootloader and the table", base,
        [(TEST_OFF, TINY), (0x2000, DUAL_BL)], len(rd(DUAL_BL)),
        "run 1 (tiny386) done, run 2 wrote the dual-boot bootloader, not yet the table: Linux's table still in")
''')
open(dst, "w").write(t)
print("written", dst)
