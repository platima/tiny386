#!/usr/bin/env python3
# Mutation run for v4.0's VBE drawing and v4.1's changes (the last six): each fault put alone into a copy of
# the tree, then the test that should see it.  vga.c faults: test_vbe
# (tests/host/test_vbe.c, Korvo-1 header) and, for the ones the guest can
# show, BVBE480 in the PC harness; pc.c faults: BVBE480 in the harness
# (Korvo-1, 1M), and the LFB read fault Windows 95 at 800x480 against the
# unchanged tree's END line.
#   mutants.py TREE WORKDIR
import os, shutil, subprocess, sys

TREE, WORK = sys.argv[1], sys.argv[2]
os.makedirs(WORK, exist_ok=True)

M = [
 # name, file, old, new, how it is caught
 ("write16-unmarked", "vga.c", "        stu16(&(s->vga_ram[addr]), val);\n        VRAM_WRITTEN(s, addr, 2);",
  "        stu16(&(s->vga_ram[addr]), val);", "vbe"),
 ("write32-unmarked", "vga.c", "        stu32(&(s->vga_ram[addr]), val);\n        VRAM_WRITTEN(s, addr, 4);",
  "        stu32(&(s->vga_ram[addr]), val);", "vbe"),
 ("string-unmarked", "vga.c", "        if (len > 0)\n            VRAM_WRITTEN(s, addr, len);\n        return true;",
  "        return true;", "vbe"),
 ("write8-unmarked", "vga.c", "            s->vga_ram[addr] = val;\n            VRAM_WRITTEN(s, addr, 1);",
  "            s->vga_ram[addr] = val;", "vbe"),
 ("block-first-row-only", "vga.c", "            uint32_t yb = (hi - addr1) / line_offset;",
  "            uint32_t yb = (lo - addr1) / line_offset;", "vbe"),
 ("block-hi-not-clipped", "vga.c", "            if (hi >= end)\n                hi = end - 1;",
  "            if (hi >= end + 1024)\n                hi = end - 1;", "vbe"),
 ("palette-change-missed", "vga.c",
  "            if (s->vbe_pal[i] != (uint16_t) palette[i]) {\n                s->vbe_pal[i] = palette[i];\n                all = true;\n            }\n        }\n    }\n    s->vbe_kind = 1;",
  "            if (s->vbe_pal[i] != (uint16_t) palette[i]) {\n                s->vbe_pal[i] = palette[i];\n            }\n        }\n    }\n    s->vbe_kind = 1;", "vbe"),
 ("start-change-missed", "vga.c",
  "        s->vbe_i0 != i0 || s->vbe_addr != addr1 || s->vbe_line != line_offset;\n    if (bpp == 8) {\n        for (int i = 0; i < 256; i++) {\n            if (s->vbe_pal[i] != (uint16_t) palette[i]) {\n                s->vbe_pal[i] = palette[i];\n                all = true;\n            }\n        }\n    }\n    s->vbe_kind = 1;",
  "        s->vbe_i0 != i0 || s->vbe_line != line_offset;\n    if (bpp == 8) {\n        for (int i = 0; i < 256; i++) {\n            if (s->vbe_pal[i] != (uint16_t) palette[i]) {\n                s->vbe_pal[i] = palette[i];\n                all = true;\n            }\n        }\n    }\n    s->vbe_kind = 1;", "vbe"),
 ("rgb24-pixels-swapped", "vga.c",
  "            VBE_PUT(d[1], vbe_rgb565((b >> 16) | (c << 16)) | (vbe_rgb565(c >> 8) << 16),",
  "            VBE_PUT(d[1], vbe_rgb565(c >> 8) | (vbe_rgb565((b >> 16) | (c << 16)) << 16),", "vbe"),
 ("rgb15-bit15-kept", "vga.c", "(v & 0x001f001f) | ((v & 0x7fe07fe0) << 1)",
  "(v & 0x001f001f) | ((v & 0xffe0ffe0) << 1)", "vbe"),
 ("rgb32-green-low-bit", "vga.c", "((p >> 5) & 0x07e0)", "((p >> 5) & 0x07c0)", "vbe"),
 ("band-dropped", "vga.c",
  "                if (!all_dirty)\n                    vga_redraw_rows(s, redraw_func, opaque, b0, b1 + 1);\n                b0 = -1;",
  "                b0 = -1;", "vbe"),
 ("shrunk-rows-unmapped", "vga.c", "            if (sh != h) {\n                /* the rows dy",
  "            if (0) {\n                /* the rows dy", "vbe"),
 ("bank-mask-again", "vga.c", "            val = banks ? val % banks : 0;",
  "            val &= banks - 1;", "vbe"),
 ("lfb-write8-unmarked", "pc.c", "\t\t\tpc->vga_mem[addr] = val;\n\t\t\tLFB_WRITTEN(pc, addr, 1);",
  "\t\t\tpc->vga_mem[addr] = val;", "guest"),
 ("lfb-write16-unmarked", "pc.c", "\t\t\tstu16((uint8_t *) &(pc->vga_mem[addr]), val);\n\t\t\tLFB_WRITTEN(pc, addr, 2);",
  "\t\t\tstu16((uint8_t *) &(pc->vga_mem[addr]), val);", "guest"),
 ("lfb-write32-unmarked", "pc.c", "\t\t\tstu32((uint8_t *) &(pc->vga_mem[addr]), val);\n\t\t\tLFB_WRITTEN(pc, addr, 4);",
  "\t\t\tstu32((uint8_t *) &(pc->vga_mem[addr]), val);", "guest"),
 ("lfb-string-unmarked", "pc.c", "\t\t\tif (len > 0)\n\t\t\t\tLFB_WRITTEN(pc, addr, len);\n",
  "", "guest"),
 ("lfb-read16-wrong", "pc.c", "\t\treturn ldu16((uint8_t *) &(pc->vga_mem[addr - vga_addr2]));",
  "\t\treturn ldu16((uint8_t *) &(pc->vga_mem[(addr - vga_addr2) ^ 2]));", "windows"),
 ("mark-without-vbe", "vga.c", "        if (vbe_enabled(s) && vga_dirty_blocks)", "        if (vga_dirty_blocks)", "vbe"),
 ("mark-with-blocks-0", "vga.c", "        if (vbe_enabled(s) && vga_dirty_blocks)", "        if (vbe_enabled(s))", "vbe"),
 ("mark-only-without-vbe", "vga.c", "        if (vbe_enabled(s) && vga_dirty_blocks)", "        if (!vbe_enabled(s) && vga_dirty_blocks)", "vbe"),
 ("lfb-mark-only-blocks-0", "pc.c", "\t\tif (vga_dirty_blocks)", "\t\tif (!vga_dirty_blocks)", "guest"),
 ("chain4-switch-ignored", "vga.c", "            ((s->sr[VGA_SEQ_MEMORY_MODE] ^ val) & VGA_SR04_CHN_4M))\n            vga_vbe_whole(s);", "            ((s->sr[VGA_SEQ_MEMORY_MODE] ^ val) & VGA_SR04_CHN_4M))\n            ;", "vbe"),
 ("chain4-request-dropped", "vga.c", "            s->vbe_whole_seen = r;\n            s->vbe_drawn = false;", "            s->vbe_whole_seen = r;", "vbe"),
]

def sh(cmd):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True)

HERE = os.path.dirname(os.path.abspath(__file__))
results = []
for name, f, old, new, how in M:
    d = os.path.join(WORK, name)
    shutil.rmtree(d, ignore_errors=True)
    shutil.copytree(TREE, d, ignore=shutil.ignore_patterns('.git', 'build*', 'managed_components'))
    p = os.path.join(d, f)
    s = open(p).read()
    if s.count(old) != 1:
        results.append((name, "NOT APPLIED (%d matches)" % s.count(old)))
        continue
    open(p, "w").write(s.replace(old, new))
    r = sh("bash %s/mut-one.sh %s %s %s" % (HERE, d, how, name))
    out = [l for l in r.stdout.strip().splitlines() if l.startswith(("caught", "NOT"))]
    results.append((name, out[-1] if out else "no result: " + (r.stdout + r.stderr)[-200:]))
for n, r in results:
    print("%-24s %s" % (n, r))
