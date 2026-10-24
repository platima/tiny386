# Which parts of be5b725's flash.bin are 0xFF (for the 0x9000..0x1FFFF blank and section 7.2)
W=$HOME/tiny386-idf-scratch/dualboot
python3 - "$W/inputs/be5b725/flash.bin" > $W/r2/14-flashbin-blank.txt <<'PY'
import sys
b = open(sys.argv[1], "rb").read()
print("### 14. be5b725 flash.bin: regions that are all 0xFF")
print("flash.bin length 0x%x" % len(b))
def ff(a, e): return all(x == 0xFF for x in b[a:e])
for a, e, n in [(0x0, 0x2000, "0x0..0x1FFF"), (0x2000 + 22000, 0x8000, "after the bootloader..0x7FFF"),
                (0x8C00, 0x9000, "after the table..0x8FFF"), (0x9000, 0x11000, "gap 0x9000..0x10FFF"),
                (0x11000, 0x17000, "nvs"), (0x17000, 0x19000, "otadata"), (0x19000, 0x1A000, "phy_init"),
                (0x1A000, 0x20000, "gap 0x1A000..0x1FFFF"),
                (0x20000 + 740992, 0x220000, "factory after the loader (0x%x..0x21FFFF)" % (0x20000 + 740992)),
                (0x1D0000, 0x200000, "v2 table's bios.bin and vgabios.bin (0x1D0000..0x1FFFFF)")]:
    print("  %-62s all 0xFF: %s" % (n, ff(a, e)))
PY
cat $W/r2/14-flashbin-blank.txt
