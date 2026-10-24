#!/usr/bin/env python3
"""Faults put into copies of the status LED's sources (v3.7) for host test 15
(test_status.c) to catch, each one change to one file.
argv: esp/main outdir.  Writes outdir/NAME/ (status_code.c, status_code.h,
status_bb.h and status_led.c, one of them changed) for each fault, and
outdir/list.txt with one "NAME<TAB>file<TAB>what" line each."""
import os, shutil, sys

main = sys.argv[1]
out = sys.argv[2]
FILES = ["status_code.c", "status_code.h", "status_bb.h", "status_led.c"]
src = {f: open(os.path.join(main, f)).read() for f in FILES}

M = [
 ("no-floor", "status_code.c", "a lit channel may come to 0 (no floor of 1)",
  "\t\tif (!v && c && level)\n\t\t\tv = 1;\n", ""),
 ("floor-dark", "status_code.c", "the floor of 1 lights dark channels too",
  "\t\tif (!v && c && level)\n", "\t\tif (!v && level)\n"),
 ("round-down", "status_code.c", "rounded down, not to the nearest step",
  "(c * level + 127) / 255", "(c * level) / 255"),
 ("div-256", "status_code.c", "divided by 256 (a shift), so 255 is not the table",
  "(c * level + 127) / 255", "(c * level + 128) >> 8"),
 ("no-clamp-scale", "status_code.c", "a level above 255 not held to 255 in the scale",
  "\tif (level > STATUS_BRIGHTNESS_MAX)\n\t\tlevel = STATUS_BRIGHTNESS_MAX;\n", ""),
 ("no-clamp-set", "status_code.c", "the level set not held to 0 to 255",
  "\ts_level = level < 0 ? 0 : level > STATUS_BRIGHTNESS_MAX ? STATUS_BRIGHTNESS_MAX :\n\t\t  (uint32_t) level;",
  "\ts_level = (uint32_t) level;"),
 ("frame-unscaled", "status_code.c", "status_frame_grb() without the level (both paths at full scale)",
  "\treturn status_grb(status_scale(rgb, s_level));", "\treturn status_grb(rgb);"),
 ("frame-default", "status_code.c", "status_frame_grb() at the default whatever the level set",
  "\treturn status_grb(status_scale(rgb, s_level));",
  "\treturn status_grb(status_scale(rgb, STATUS_BRIGHTNESS_DEFAULT));"),
 ("scale-64bit", "status_code.c", "the scale in 64 bits (a 64-bit division the panic path would reach in flash)",
  "\t\tuint32_t v = (c * level + 127) / 255;",
  "\t\tuint32_t v = (uint32_t) (((uint64_t) c * level + 127) / 255);"),
 ("level-not-dram", "status_code.c", "the level not marked for internal RAM",
  "static STATUS_DRAM volatile uint32_t s_level", "static volatile uint32_t s_level"),
 ("scale-not-iram", "status_code.c", "the scale not marked for internal RAM",
  "STATUS_IRAM uint32_t status_scale(", "uint32_t status_scale("),
 ("table-v36", "status_code.c", "the table left at v3.6's quarter (red 0x46)",
  "#define RED     0xff0000u", "#define RED     0x460000u"),
 ("yellow-hue", "status_code.c", "yellow's green changed (a different hue)",
  "#define YELLOW  0xdb8000u", "#define YELLOW  0xdb6000u"),
 ("white-share", "status_code.c", "white at full scale on its own (its share against red changed)",
  "#define WHITE   0x929292u", "#define WHITE   0xffffffu"),
 ("default-70", "status_code.h", "the default left at v3.6's light (70)",
  "#define STATUS_BRIGHTNESS_DEFAULT 9 ", "#define STATUS_BRIGHTNESS_DEFAULT 70 "),
 ("default-20", "status_code.h", "a default of 20 (red 20 of 255)",
  "#define STATUS_BRIGHTNESS_DEFAULT 9 ", "#define STATUS_BRIGHTNESS_DEFAULT 20 "),
 ("default-4", "status_code.h", "a default of 4 (red 4 of 255)",
  "#define STATUS_BRIGHTNESS_DEFAULT 9 ", "#define STATUS_BRIGHTNESS_DEFAULT 4 "),
 ("rmt-unscaled", "status_led.c", "the RMT path's frames without the brightness (status_grb())",
  "\tstatus_rmt_frame(status_frame_grb(rgb), s_syms);", "\tstatus_rmt_frame(status_grb(rgb), s_syms);"),
 ("panic-unscaled", "status_led.c", "the panic path's first frame without the brightness",
  "\tstatus_bb_frame(status_frame_grb(status_panic_pattern(early, 0)), &s_bb);",
  "\tstatus_bb_frame(status_grb(status_panic_pattern(early, 0)), &s_bb);"),
 ("halt-unscaled", "status_led.c", "the halt loop's frames without the brightness",
  "\t\t\tstatus_bb_frame(status_frame_grb(rgb), &s_bb);", "\t\t\tstatus_bb_frame(status_grb(rgb), &s_bb);"),
 ("halt-own-scale", "status_led.c", "the halt loop scaling at a fixed level of its own (not the level set)",
  "\t\t\tstatus_bb_frame(status_frame_grb(rgb), &s_bb);",
  "\t\t\tstatus_bb_frame(status_grb(status_scale(rgb, 9)), &s_bb);"),
]

os.makedirs(out, exist_ok=True)
with open(os.path.join(out, "list.txt"), "w") as lst:
    for name, f, what, old, new in M:
        n = src[f].count(old)
        if n != 1:
            print("pattern for %s found %d times in %s" % (name, n, f))
            sys.exit(1)
        d = os.path.join(out, name)
        os.makedirs(d, exist_ok=True)
        for g in FILES:
            text = src[g].replace(old, new) if g == f else src[g]
            open(os.path.join(d, g), "w").write(text)
        lst.write("%s\t%s\t%s\n" % (name, f, what))
print(len(M), "faults written")
