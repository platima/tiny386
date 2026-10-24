#!/bin/bash
# The tiny386-by-default dual-boot table: made from the dual-boot CSV with
# the two app subtypes swapped, then decoded and compared with Linux's table
# and the Linux-by-default dual-boot table.  Reads the v3 package and
# ESP-IDF; writes only in ~/win95-debug/bootswap/table and the output file.
#
#   bash table-check.sh OUTFILE
set -u
OUTF=$1
W=~/win95-debug/bootswap/table2
V3=/mnt/d/Repos/esp32-s31-linux-release/tiny386-idf/v3
BE=/mnt/d/Repos/esp32-s31-linux-release/main-be5b725/esp32s31-linux-main-be5b725-flash
cd $W
{
echo "The tiny386-by-default dual-boot table, made and checked $(date '+%Y-%m-%d %H:%M') in WSL by"
echo "table-check.sh (v3/checks/bootswap/scripts).  ESP-IDF: ~/esp/esp-idf at $(git -C ~/esp/esp-idf rev-parse --short=10 HEAD),"
echo "$(git -C ~/esp/esp-idf status --short | wc -l) files changed in it; gen_esp32part.py run as the dual-boot work ran it"
echo "(--flash-size 16MB, MD5 entry on by default)."
echo
echo "### 1. the CSV: the dual-boot CSV with the two app subtypes swapped"
echo "diff dualboot/partitions-korvo1-dualboot.csv dualboot/partitions-korvo1-dualboot-tiny386-default.csv"
echo "(comment lines at the top of the new file left out):"
grep -v '^# [A-Z].*[a-z]' partitions-korvo1-dualboot-tiny386-default.csv | grep -v '^# [a-z(]' > body.csv
diff $V3/dualboot/partitions-korvo1-dualboot.csv body.csv
echo
env -i HOME=$HOME PATH=/usr/bin:/bin:/usr/sbin:/sbin IDF_PATH=$HOME/esp/esp-idf IDF_TOOLS_PATH=$HOME/.espressif-upstream bash -c '
. $HOME/esp/esp-idf/export.sh >/dev/null 2>&1 || { echo "no ESP-IDF"; exit 1; }
P=$IDF_PATH/components/partition_table/gen_esp32part.py
V3=/mnt/d/Repos/esp32-s31-linux-release/tiny386-idf/v3
BE=/mnt/d/Repos/esp32-s31-linux-release/main-be5b725/esp32s31-linux-main-be5b725-flash
echo "### 2. the same invocation on the existing dual-boot CSV gives the existing table"
python $P --flash-size 16MB $V3/dualboot/partitions-korvo1-dualboot.csv dual-from-csv.bin > dual-from-csv.log 2>&1; echo "gen_esp32part exit $?"; sed "s/^/  /" dual-from-csv.log
cmp dual-from-csv.bin $V3/dualboot/partition-table.bin && echo "  equal to dualboot/partition-table.bin, byte for byte"
cmp dual-from-csv.bin $V3/images/korvo1-dualboot/partition-table.bin && echo "  equal to images/korvo1-dualboot/partition-table.bin, byte for byte"
echo
echo "### 3. the new table"
python $P --flash-size 16MB partitions-korvo1-dualboot-tiny386-default.csv partition-table-tiny386-default.bin > new.log 2>&1; echo "gen_esp32part exit $?"; sed "s/^/  /" new.log
echo "  (the WARNING is about the label: the Linux loader keeps its label \"factory\" with subtype test."
echo "  gen_esp32part.py lines 586 to 594 print it and go on; neither app reads its own label: dualboot/DUALBOOT.md 13.2.)"
python $P --flash-size 16MB body.csv from-body.bin > /dev/null 2>&1
cmp from-body.bin partition-table-tiny386-default.bin && echo "  the comment lines make no difference: the CSV without them gives the same bytes"
echo
echo "### 4. decoded with gen_esp32part.py"
for n in linux:$BE/partition-table.bin dual:$V3/images/korvo1-dualboot/partition-table.bin dual386:partition-table-tiny386-default.bin; do
  python $P --flash-size 16MB "${n#*:}" "decoded-${n%%:*}.csv" > "decoded-${n%%:*}.log" 2>&1
  echo "== ${n%%:*}: ${n#*:} (exit $?)"; grep -v "^#" "decoded-${n%%:*}.csv"; sed "s/^/  /" "decoded-${n%%:*}.log"
done
echo
echo "== diff decoded linux decoded dual386 (Linux table against the new one)"
diff decoded-linux.csv decoded-dual386.csv
echo "== diff decoded dual decoded dual386 (Linux-by-default dual boot against the new one)"
diff decoded-dual.csv decoded-dual386.csv
echo
echo "### 5. bytes"
'
for f in $BE/partition-table.bin $V3/images/korvo1-dualboot/partition-table.bin partition-table-tiny386-default.bin; do
  printf '  %s  %5d bytes  %s\n' "$(sha256sum < $f | cut -c1-64)" $(stat -c %s $f) $f
done
echo "Differing bytes, dual (Linux by default) against dual386 (tiny386 by default), cmp -l"
echo "(offset in the table, then the two values in octal):"
cmp -l $V3/images/korvo1-dualboot/partition-table.bin partition-table-tiny386-default.bin | awk '{printf "  0x%03x  %s  %s\n", $1-1, $2, $3}'
echo "Offsets: entry N starts at 32*(N-1); byte 3 of an entry is its subtype (magic 2 bytes, type, subtype)."
echo "  0x063 = entry 4 (factory, 0x20000) subtype: 0x00 factory -> 0x20 test"
echo "  0x0e3 = entry 8 (tiny386, 0xE20000) subtype: 0x20 test -> 0x00 factory"
echo "  0x110 to 0x11f = the MD5 entry's digest (entry 9, digest at offset 16 of it)"
echo "First 224 bytes against Linux's table (the seven Linux entries):"
cmp -n 224 $BE/partition-table.bin partition-table-tiny386-default.bin && echo "  identical" || cmp -l -n 224 $BE/partition-table.bin partition-table-tiny386-default.bin | awk '{printf "  0x%03x  %s  %s\n", $1-1, $2, $3}'
echo "Bytes 0x120 to 0xBFF (after the MD5 entry) all 0xFF in the new table:"
python3 -I -c "
import sys
b = open(sys.argv[1], 'rb').read()
print('  ', all(x == 0xFF for x in b[0x120:0xC00]), len(b))
" partition-table-tiny386-default.bin
echo "The MD5 entry checked as the bootloader checks it (esp_partition_table_verify, flash_partitions.c"
echo "lines 33 to 57: MD5 over the entries before it, compared with bytes 16 to 31 of the 0xEBEB entry):"
python3 -I -c "
import hashlib, struct, sys
for p in sys.argv[1:]:
    b = open(p, 'rb').read()
    n = 0
    while struct.unpack('<H', b[32*n:32*n+2])[0] == 0x50AA:
        n += 1
    e = b[32*n:32*n+32]
    ok = struct.unpack('<H', e[:2])[0] == 0xEBEB and e[16:32] == hashlib.md5(b[:32*n]).digest()
    end = b[32*(n+1):32*(n+2)] == b'\xff' * 32
    print('  %s: %d entries, MD5 entry %s, terminating 0xFF entry after it: %s' % (p.split('/')[-1], n, 'right' if ok else 'WRONG', end))
" $V3/images/korvo1-dualboot/partition-table.bin partition-table-tiny386-default.bin
echo "Hex, first 0x120 bytes, dual then dual386:"
xxd -l 288 $V3/images/korvo1-dualboot/partition-table.bin
echo
xxd -l 288 partition-table-tiny386-default.bin
echo
echo "### 6. the bootloader beside it"
for f in $V3/dualboot/bootloader.bin $V3/images/korvo1-dualboot/bootloader.bin; do
  printf '  %s  %5d bytes  %s\n' "$(sha256sum < $f | cut -c1-64)" $(stat -c %s $f) $f
done
} > "$OUTF" 2>&1
echo "written $OUTF"
