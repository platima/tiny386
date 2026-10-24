#!/bin/bash
# Checks for the dual-boot set, without a board.  Reads the builds made by
# build-dualboot.sh and the copied inputs; writes only under $W/checks.
set -u
W=$HOME/tiny386-idf-scratch/dualboot
C=$W/checks
IDF_PATH=$HOME/esp/esp-idf
IDF_TOOLS_PATH=$HOME/.espressif-upstream
mkdir -p "$C"
cd "$W"

REF=ref/build/bootloader
DUAL=dual/build/bootloader
FRESH=dual-fresh/build/bootloader
BE=inputs/be5b725
REL=inputs/release-v2026.09.20
V2=inputs/v2

run_idf() {   # run a command line with ESP-IDF's environment, from bash
    env -i HOME="$HOME" PATH=/usr/bin:/bin:/usr/sbin:/sbin \
        IDF_PATH="$IDF_PATH" IDF_TOOLS_PATH="$IDF_TOOLS_PATH" W="$W" \
        bash -c '. "$IDF_PATH/export.sh" >/dev/null 2>&1 && cd "$W" && '"$1"
}

{
echo "### 1. partition tables, as bytes"
sha256sum $BE/partition-table.bin $REL/bootloader/partition_table/partition-table.bin \
          $REF/partition_table/partition-table.bin $DUAL/partition_table/partition-table.bin \
          $FRESH/partition_table/partition-table.bin $V2/partition_table/partition-table.bin
echo "be5b725 csv vs fork c288bc9 csv:"; cmp src/linux-be5b725/bootloader/partitions.csv src/fork-c288bc9/partitions.csv && echo "  identical"
echo "be5b725 bin vs release bin:"; cmp $BE/partition-table.bin $REL/bootloader/partition_table/partition-table.bin && echo "  identical"
echo "ref build bin vs be5b725 bin:"; cmp $REF/partition_table/partition-table.bin $BE/partition-table.bin && echo "  identical"
echo "dual vs dual-fresh bin:"; cmp $DUAL/partition_table/partition-table.bin $FRESH/partition_table/partition-table.bin && echo "  identical"
echo "first 224 bytes (7 entries of 32) of dual vs be5b725:"
cmp -n 224 $DUAL/partition_table/partition-table.bin $BE/partition-table.bin && echo "  identical"
echo "dual table, first 288 bytes:"; xxd -l 288 $DUAL/partition_table/partition-table.bin
echo "be5b725 table, first 256 bytes:"; xxd -l 256 $BE/partition-table.bin
} > "$C/1-tables-bytes.txt" 2>&1

run_idf '
P=$IDF_PATH/components/partition_table/gen_esp32part.py
mkdir -p checks/decoded
for n in be5b725:inputs/be5b725/partition-table.bin release:inputs/release-v2026.09.20/bootloader/partition_table/partition-table.bin ref:ref/build/bootloader/partition_table/partition-table.bin dual:dual/build/bootloader/partition_table/partition-table.bin v2-standalone:inputs/v2/partition_table/partition-table.bin; do
  python "$P" --flash-size 16MB "${n#*:}" "checks/decoded/${n%%:*}.csv" > "checks/decoded/${n%%:*}.log" 2>&1
done
python "$P" --flash-size 16MB dual/bootloader/partitions-korvo1-dualboot.csv checks/decoded/dual-from-csv.bin > checks/decoded/dual-from-csv.log 2>&1
cmp checks/decoded/dual-from-csv.bin dual/build/bootloader/partition_table/partition-table.bin && echo "gen_esp32part on the CSV gives the build output, byte for byte" > checks/decoded/dual-from-csv.cmp
python "$P" --help > checks/decoded/gen_esp32part-help.txt 2>&1
'
{
echo "### 2. decoded tables (gen_esp32part.py, ESP-IDF 188e3e55)"
for f in be5b725 release ref dual v2-standalone; do echo "== $f"; cat checks/decoded/$f.csv; cat checks/decoded/$f.log; done
echo "== diff be5b725 release"; diff checks/decoded/be5b725.csv checks/decoded/release.csv && echo "  no difference"
echo "== diff be5b725 dual"; diff checks/decoded/be5b725.csv checks/decoded/dual.csv
cat checks/decoded/dual-from-csv.cmp 2>/dev/null
} > "$C/2-tables-decoded.txt" 2>&1

{
echo "### 3. bootloaders: sizes and room below the table at 0x8000 (0x6000 = 24576 bytes from 0x2000)"
for f in $BE/bootloader.bin $REL/bootloader/bootloader/bootloader.bin $REF/bootloader/bootloader.bin $DUAL/bootloader/bootloader.bin $FRESH/bootloader/bootloader.bin $V2/bootloader/bootloader.bin; do
  s=$(stat -c %s $f); printf '%-60s %6d bytes  0x%05x  room %5d bytes  ends 0x%05x  erased to 0x%05x\n' $f $s $s $((24576-s)) $((0x2000+s)) $(( (0x2000+s+4095)/4096*4096 - 1 ))
done
echo
echo "ref (unchanged copy) vs be5b725 bootloader.bin, differing bytes (offset, octal values):"
cmp -l $REF/bootloader/bootloader.bin $BE/bootloader.bin | wc -l
cmp -l $REF/bootloader/bootloader.bin $BE/bootloader.bin | awk '{printf "  0x%04x %s %s\n", $1-1, $2, $3}' | head -80
echo "compile-time strings:"
for f in $BE/bootloader.bin $REF/bootloader/bootloader.bin $DUAL/bootloader/bootloader.bin $FRESH/bootloader/bootloader.bin; do
  printf '%s: ' $f; strings -t x $f | grep -E '[A-Z][a-z][a-z] [ 0-9][0-9] 20[0-9][0-9][0-9:]*|[0-9][0-9]:[0-9][0-9]:[0-9][0-9]' | head -3 | tr '\n' ' '; echo
done
echo
echo "dual vs dual-fresh bootloader.bin, differing bytes:"; cmp -l $DUAL/bootloader/bootloader.bin $FRESH/bootloader/bootloader.bin | wc -l
cmp -l $DUAL/bootloader/bootloader.bin $FRESH/bootloader/bootloader.bin | awk '{printf "  0x%04x\n", $1-1}' | head -60
} > "$C/3-bootloader-sizes.txt" 2>&1

{
echo "### 4. sdkconfig: dual-boot vs the Linux one"
echo "== src (as copied from ~/esp32-s31-linux-main) vs ref after its build"; diff src/linux-be5b725/bootloader/sdkconfig ref/bootloader/sdkconfig && echo "  no difference"
echo "== ref vs dual, both after their builds"; diff ref/bootloader/sdkconfig dual/bootloader/sdkconfig
echo "== dual vs dual-fresh (made from sdkconfig.defaults alone)"; diff dual/bootloader/sdkconfig dual-fresh/bootloader/sdkconfig && echo "  no difference"
echo "== sdkconfig.defaults: src vs dual"; diff src/linux-be5b725/bootloader/sdkconfig.defaults dual/bootloader/sdkconfig.defaults
echo "== the bootloader's own sdkconfig.h, dual build"
grep -n -E 'APP_TEST|HOLD_TIME|RESERVE_RTC|ROLLBACK|BOOTLOADER_LOG_LEVEL |FLASHMODE|FLASHFREQ |FLASHSIZE |SINGLE_CORE|PARTITION_TABLE_OFFSET|SPIRAM_BOOT' $DUAL/config/sdkconfig.h
echo "== same keys, ref build"
grep -n -E 'APP_TEST|HOLD_TIME|RESERVE_RTC|ROLLBACK|BOOTLOADER_LOG_LEVEL |FLASHMODE|FLASHFREQ |FLASHSIZE |SINGLE_CORE|PARTITION_TABLE_OFFSET|SPIRAM_BOOT' $REF/config/sdkconfig.h
echo "== the bootloader subproject's sdkconfig.h, dual build"
grep -n -E 'APP_TEST|HOLD_TIME' $DUAL/bootloader/config/sdkconfig.h 2>/dev/null || find $DUAL -name sdkconfig.h
} > "$C/4-sdkconfig.txt" 2>&1

run_idf '
B=dual/build/bootloader/bootloader
OD=$(command -v riscv32-esp-elf-objdump)
NM=$(command -v riscv32-esp-elf-nm)
{
echo "### 5. the test-pin check in the dual-boot bootloader.elf"
echo "objdump: $OD"
echo "== symbols"
$NM -S --size-sort $B/bootloader.elf | grep -i -E "long_hold|select|call_start_cpu0|test"
echo "== calls to bootloader_common_check_long_hold_gpio_level, with the 12 lines before each"
$OD -d $B/bootloader.elf | grep -B12 "<bootloader_common_check_long_hold_gpio_level>\$" | grep -v "^--\$"
echo "== the message strings"
strings $B/bootloader.bin | grep -E "test firmware|Test firmware|test app|Unicore"
echo "== the same strings in the reference (unchanged) bootloader"
strings ref/build/bootloader/bootloader/bootloader.bin | grep -E "test firmware|Test firmware|test app|Unicore"
echo "== section sizes, ref then dual"
riscv32-esp-elf-size ref/build/bootloader/bootloader/bootloader.elf $B/bootloader.elf
} > checks/5-test-pin.txt 2>&1

{
echo "### 6. esptool image-info (esptool $(esptool version | tail -1))"
for f in dual/build/bootloader/bootloader/bootloader.bin ref/build/bootloader/bootloader/bootloader.bin inputs/be5b725/bootloader.bin inputs/release-v2026.09.20/bootloader/bootloader/bootloader.bin inputs/v2/bootloader/bootloader.bin inputs/v2/tiny386.bin inputs/be5b725/s31-linux-loader.bin inputs/release-v2026.09.20/bootloader/s31-linux-loader.bin; do
  echo; echo "=================== $f"; sha256sum $f
  esptool -c esp32s31 image-info $f 2>&1
done
} > checks/6-image-info.txt 2>&1
'

run_idf 'python - <<'"'"'PY'"'"' > checks/7-flash-map.txt 2>&1
import os, re
W = os.environ["W"]
def sz(p): return os.path.getsize(os.path.join(W, p))
SEC = 0x1000
TINY = 0xE20000
def show(name, writes):
    print("== " + name)
    print("   %-9s %-48s %10s  %-10s %-10s %s" % ("offset", "file", "bytes", "last byte", "erased to", "note"))
    hi = 0
    for off, path in writes:
        n = sz(path)
        end = off + n - 1
        er = ((off + n + SEC - 1) // SEC) * SEC - 1
        hi = max(hi, er)
        print("   0x%07x %-48s %10d  0x%07x  0x%07x" % (off, path, n, end, er))
    print("   highest byte erased or written: 0x%07x; below 0xE20000: %s" % (hi, hi < TINY))
    print()
    return hi

rel = "inputs/release-v2026.09.20/"
bat = open(os.path.join(W, rel + "flash.bat")).read()
pairs = re.findall(r"(0x[0-9a-fA-F]+)\s+(\S+)", bat)
show("Linux release v2026.09.20, flash.bat as written", [(int(a, 16), rel + f.replace("\\", "/")) for a, f in pairs])
be = "inputs/be5b725/"
fa = open(os.path.join(W, be + "flash_args")).read().split()
show("Linux be5b725, flash_args", [(int(fa[i], 16), be + fa[i + 1]) for i in range(0, len(fa), 2)])
show("Linux be5b725, flash.bin at 0x0", [(0, be + "flash.bin")])
dual = [(0x2000, "dual/build/bootloader/bootloader/bootloader.bin"),
        (0x8000, "dual/build/bootloader/partition_table/partition-table.bin"),
        (TINY, "inputs/v2/tiny386.bin")]
show("dual-boot set (tiny386.bin from v2)", dual)
show("standalone set (v2 images, tiny386 own table)",
     [(0x2000, "inputs/v2/bootloader/bootloader.bin"),
      (0x8000, "inputs/v2/partition_table/partition-table.bin"),
      (0x10000, "inputs/v2/tiny386.bin")])
n = sz("inputs/v2/tiny386.bin")
print("tiny386.bin (v2): %d bytes (0x%x)" % (n, n))
print("  test partition 0x1E0000 = %d bytes: fits %s, %d bytes (%.1f per cent) spare" % (0x1E0000, n <= 0x1E0000, 0x1E0000 - n, 100.0 * (0x1E0000 - n) / 0x1E0000))
print("  standalone factory 0x1C0000 = %d bytes: fits %s, %d bytes (%.1f per cent) spare" % (0x1C0000, n <= 0x1C0000, 0x1C0000 - n, 100.0 * (0x1C0000 - n) / 0x1C0000))
# what the standalone set overwrites of a Linux layout
print()
print("standalone set against the Linux layout:")
lin = [("nvs",0x11000,0x6000),("otadata",0x17000,0x2000),("phy_init",0x19000,0x1000),("factory",0x20000,0x200000),("opensbi",0x220000,0x80000),("linux",0x2A0000,0x780000),("initramfs",0xA20000,0x200000),("tiny386 (dual-boot)",0xE20000,0x1E0000)]
er_lo, er_hi = 0x10000, ((0x10000 + n + SEC - 1)//SEC)*SEC
for name, o, s in lin:
    if o < er_hi and er_lo < o + s:
        print("  tiny386.bin at 0x10000 (erased 0x10000 to 0x%x) overlaps %s (0x%x to 0x%x)" % (er_hi - 1, name, o, o + s - 1))
PY
'
echo "checks written to $C"; ls -l "$C"
