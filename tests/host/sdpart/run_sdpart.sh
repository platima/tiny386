#!/bin/bash
# v4.4: a card partition as the guest's whole disk (esp/main/sd_part.c,
# hda = sd:3).
#
# 1. The guest's boot code: esp/main/sd_part_mbr.s assembled with GNU as
#    must give the bytes in sd_part_mbr.h (REGEN=1 writes the header again).
# 2. test_sd_part.c with ESP-IDF's own FatFs and SD card glue, storage.c's
#    partition code as the file has it, sd_part.c and disk_image.c as the
#    board's main component builds them (warnings are errors) and ide.c as
#    the board builds it: sd_part_plan()'s checks on made-up sectors, then
#    two card images:
#      new  the layout s31/newcard makes: p1 FAT32 KORVO_SD at 8192, p3
#           FAT16 (type 0E) straight after it, p2 (ext4) after p3, the
#           table in partition order
#      old  Plat's card as it is: p1 FAT32, p2 ext4, p3 FAT32 WIN9X, which
#           v4.5 refuses (the Linux route's partition holding win95.img);
#           then the same card with p3 relabelled DATA32, given whole
#    On "new" the guest's own view of its disk writes two files on p3;
#    then mtools reads them back, fsck.fat -n finds p1 and p3 clean, and
#    the card's sector 0, p1 and p2 are as they were, every sector.
#
#   bash tests/host/sdpart/run_sdpart.sh [OUT]
#
# Needs gcc, as and ld (binutils), mkfs.fat and fsck.fat, mtools, sfdisk,
# python3, and ESP-IDF's source at IDF_PATH (default ~/esp/esp-idf), which
# is only read; without them it says SKIP.  Exit status 0 when every check
# passes.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
S=$TREE/tests/host/storage
M=$TREE/esp/main
OUT=${1:-$HERE/build}
IDF=${IDF_PATH:-$HOME/esp/esp-idf}
F=$IDF/components/fatfs
export MTOOLS_SKIP_CHECK=1
export PATH=$PATH:/sbin:/usr/sbin
mkdir -p "$OUT" || exit 2
for t in gcc as ld mkfs.fat fsck.fat mcopy mtype mlabel sfdisk python3; do
    command -v $t >/dev/null || { echo "SKIP sdpart: no $t"; exit 0; }
done
[ -f "$F/src/ff.c" ] || { echo "SKIP sdpart: no ESP-IDF FatFs at $F"; exit 0; }

pass=0
fail=0
result() {
    if [ "$1" = 0 ]; then pass=$((pass + 1)); echo "  ok: $2"; else fail=$((fail + 1)); echo "  FAILED: $2"; fi
}

# ==== 1. the boot code ====
echo "== the guest's boot code: sd_part_mbr.s against sd_part_mbr.h"
as --32 -o "$OUT/mbr.o" "$M/sd_part_mbr.s" || exit 1
ld -m elf_i386 -Ttext=0x600 --oformat=binary -e start -o "$OUT/mbr.bin" "$OUT/mbr.o" || exit 1
python3 -I - "$OUT/mbr.bin" "$M/sd_part_mbr.h" "${REGEN:-}" <<'PY'
import re, sys
b = open(sys.argv[1], 'rb').read()
if len(b) > 440:
    sys.exit('the boot code is %d bytes, more than the 440 before the disk signature' % len(b))
if sys.argv[3]:
    rows = ['\t' + ', '.join('0x%02x' % x for x in b[i:i + 12]) + ',' for i in range(0, len(b), 12)]
    open(sys.argv[2], 'w').write("""/*
 * v4.4: the boot code of the guest's master boot record for a card
 * partition given to it whole, as GNU as assembles sd_part_mbr.s (%d
 * bytes).  Made by tests/host/sdpart/run_sdpart.sh with REGEN=1, which
 * also checks, every run, that these bytes are still the source's.
 */
#ifndef SD_PART_MBR_H
#define SD_PART_MBR_H

#include <stdint.h>

static const uint8_t sd_part_mbr_code[%d] = {
%s
};

#endif /* SD_PART_MBR_H */
""" % (len(b), len(b), '\n'.join(rows)))
    print('    | sd_part_mbr.h written again')
h = open(sys.argv[2]).read()
body = h[h.index('{') + 1:h.rindex('}')]
got = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-f]{2})', body))
print('    | %d bytes assembled, %d in the header' % (len(b), len(got)))
sys.exit(0 if got == b else 1)
PY
result $? "sd_part_mbr.h holds the bytes GNU as makes of sd_part_mbr.s"

# ==== 2. the program ====
ST=$TREE/esp/main/storage.c
a=$(grep -n "^ \* The card's other partitions" "$ST" | cut -d: -f1)
b=$(grep -n "^/\* Bit 0: the card is mounted at /sdcard" "$ST" | cut -d: -f1)
[ -n "$a" ] && [ -n "$b" ] || { echo "FAIL sdpart: the partition code was not found in storage.c"; exit 1; }
sed -n "$((a - 1)),$((b - 1))p" "$ST" > "$OUT/storage_parts.c"
printf '#include <stdio.h>\n#include <string.h>\n#include <strings.h>\n#include "sdkconfig.h"\n#include "esp_err.h"\n#include "esp_vfs_fat.h"\n#include "sdmmc_cmd.h"\n#include "storage.h"\nvoid *rawsd;\n#include "%s"\n' \
    "$OUT/storage_parts.c" > "$OUT/storage_host.c"
CC="gcc -D_FILE_OFFSET_BITS=64"
INC="-I$S/stubs -I$F/src -I$F/diskio -I$S -I$TREE -I$M"
W="-Wall -Wextra -Wno-unused-parameter -Wno-sign-compare"
for f in src/ff.c src/ffunicode.c diskio/diskio.c diskio/diskio_sdmmc.c; do
    $CC -O1 -g -w $INC -c -o "$OUT/idf_$(basename $f .c).o" "$F/$f" || exit 1
done
$CC -O1 -g -w $INC -c -o "$OUT/host_glue.o" "$S/host_glue.c" || exit 1
$CC -O1 -g $W -Wno-format-truncation -DSD_CLK=1 $INC -c -o "$OUT/storage_host.o" "$OUT/storage_host.c" || exit 1
# sd_part.c and disk_image.c as the board's main component builds them
$CC -O1 -g $W -Werror $INC -c -o "$OUT/sd_part.o" "$M/sd_part.c" || exit 1
$CC -O1 -g $W -Werror $INC -c -o "$OUT/disk_image.o" "$M/disk_image.c" || exit 1
# ide.c's own warnings are not this test's business (-w after -Wall)
$CC -O1 -g $W -w -DIDE_C="\"$TREE/ide.c\"" $INC -o "$OUT/test_sd_part" "$HERE/test_sd_part.c" \
    "$OUT/sd_part.o" "$OUT/disk_image.o" "$OUT/storage_host.o" "$OUT/host_glue.o" \
    "$OUT"/idf_ff.o "$OUT"/idf_ffunicode.o "$OUT"/idf_diskio.o "$OUT"/idf_diskio_sdmmc.o || exit 1

echo "== sd_part_plan(), sd_part_name(), the CHS bytes"
"$OUT/test_sd_part" unit > "$OUT/unit.log" 2>&1
st=$?
sed 's/^/    | /' "$OUT/unit.log"
result $st "the partitions the guest may and may not have, on made-up sectors"

# ==== 3. the cards ====
sum() {         # image start sectors: the region's SHA-256
    dd if="$1" bs=512 skip="$2" count="$3" status=none | sha256sum | cut -c1-16
}
table() {       # image, then "type start count" for entries 1 to 4, in that order
    python3 -I - "$@" <<'PY'
import struct, sys
img, rest = sys.argv[1], sys.argv[2:]
with open(img, 'r+b') as f:
    m = bytearray(f.read(512))
    m[440:444] = b'\x5e\x31\x86\x0d'
    for i in range(4):
        m[446 + 16 * i:462 + 16 * i] = bytes(16)
    for i in range(0, len(rest), 3):
        t, s, n = int(rest[i], 16), int(rest[i + 1]), int(rest[i + 2])
        e = 446 + 16 * (i // 3)
        m[e + 4] = t
        m[e + 8:e + 16] = struct.pack('<II', s, n)
    m[510:512] = b'\x55\xaa'
    f.seek(0)
    f.write(m)
PY
}
ext4ish() {     # image start: ext4's superblock magic and some bytes, no boot sector
    python3 -I - "$1" "$2" <<'PY'
import sys
with open(sys.argv[1], 'r+b') as f:
    f.seek(int(sys.argv[2]) * 512)
    f.write(bytes((i * 31 + 7) & 255 for i in range(64 * 1024)))
    f.seek(int(sys.argv[2]) * 512 + 1080)
    f.write(b'\x53\xef')
PY
}

echo "== the new card: p1 FAT32 at 8192, p3 FAT16 at 139264 (straight after p1), p2 at 270336"
P1=8192; P1N=131072; P3=139264; P3N=131072; P2=270336; P2N=131072
TOT=$((P2 + P2N))
IMG=$OUT/card-new.img
rm -f "$IMG"; truncate -s $((TOT * 512)) "$IMG"
table "$IMG" 0c $P1 $P1N 83 $P2 $P2N 0e $P3 $P3N
mkfs.fat -F 32 -n KORVO_SD -i 1a2b3c4d -s 1 -h $P1 --offset=$P1 "$IMG" $((P1N / 2)) > "$OUT/mkfs.log" 2>&1 || exit 1
mkfs.fat -F 16 -n WIN95 -i 5e6f7a8b -h $P3 --offset=$P3 "$IMG" $((P3N / 2)) >> "$OUT/mkfs.log" 2>&1 || exit 1
mmd -i "$IMG@@$((P1 * 512))" ::/tiny386 || exit 1
printf '[pc]\nhda = sd:3\n' > "$OUT/win95-sd.ini"
mcopy -o -i "$IMG@@$((P1 * 512))" "$OUT/win95-sd.ini" ::/tiny386/win95-sd.ini || exit 1
printf 'on the windows volume\n' > "$OUT/P3.TXT"
mcopy -o -i "$IMG@@$((P3 * 512))" "$OUT/P3.TXT" ::/P3.TXT || exit 1
ext4ish "$IMG" $P2
m0=$(sum "$IMG" 0 $P1); p1=$(sum "$IMG" $P1 $P1N); p2=$(sum "$IMG" $P2 $P2N)
"$OUT/test_sd_part" new "$IMG" $TOT $P1 $P3 $P3N $P2 > "$OUT/new.log" 2>&1
st=$?
sed 's/^/    | /' "$OUT/new.log"
result $st "new card: refusals, p3 let go and given whole, reads, writes fenced to p3, a card error got over, the guest's view"
[ "$(sum "$IMG" 0 $P1)" = "$m0" ]
result $? "new card: sector 0 to p1's start (the card's table) as it was, every sector"
[ "$(sum "$IMG" $P1 $P1N)" = "$p1" ]
result $? "new card: p1 as it was, every sector"
[ "$(sum "$IMG" $P2 $P2N)" = "$p2" ]
result $? "new card: p2 as it was, every sector"
dd if="$IMG" of="$OUT/new-p3.img" bs=512 skip=$P3 count=$P3N conv=sparse status=none
fsck.fat -n "$OUT/new-p3.img" > "$OUT/new-fsck-p3.log" 2>&1
result $? "new card: fsck.fat -n finds p3 clean ($(tail -1 "$OUT/new-fsck-p3.log"))"
dd if="$IMG" of="$OUT/new-p1.img" bs=512 skip=$P1 count=$P1N conv=sparse status=none
fsck.fat -n "$OUT/new-p1.img" > "$OUT/new-fsck-p1.log" 2>&1
result $? "new card: fsck.fat -n finds p1 clean ($(tail -1 "$OUT/new-fsck-p1.log"))"
rm -f "$OUT/new-p1.img" "$OUT/new-p3.img"
[ "$(mtype -i "$IMG@@$((P3 * 512))" ::/GUEST.TXT 2>/dev/null)" = "written by the guest through sd:3" ]
result $? "new card: GUEST.TXT on p3 as mtools reads it"
mcopy -n -o -i "$IMG@@$((P3 * 512))" ::/BIG.BIN "$OUT/big.bin" 2>/dev/null &&
python3 -I -c "import sys; sys.exit(0 if open(sys.argv[1],'rb').read() == bytes((k * 13 + 5) & 255 for k in range(65536)) else 1)" "$OUT/big.bin"
result $? "new card: BIG.BIN on p3 as mtools reads it, every byte"
rm -f "$OUT/big.bin"

echo "== Plat's card as it is: p1 FAT32, p2 ext4, p3 FAT32 WIN9X at 532480"
IMG=$OUT/card-old.img
TOT=$((532480 + 1048576))
rm -f "$IMG"; truncate -s $((TOT * 512)) "$IMG"
printf 'label: dos\nstart=8192, size=131072, type=c\nstart=139264, size=393216, type=83\nstart=532480, size=1048576, type=c\n' |
    sfdisk -q --no-reread --no-tell-kernel "$IMG" > /dev/null || exit 1
mkfs.fat -F 32 -n KORVO_SD -i 1a2b3c4d -s 1 -h 8192 --offset=8192 "$IMG" 65536 >> "$OUT/mkfs.log" 2>&1 || exit 1
mkfs.fat -F 32 -n WIN9X -i 5e6f7a8b -h 532480 --offset=532480 "$IMG" 524288 >> "$OUT/mkfs.log" 2>&1 || exit 1
printf 'p3 marker\n' > "$OUT/P3OLD.TXT"
mcopy -o -i "$IMG@@$((532480 * 512))" "$OUT/P3OLD.TXT" ::/P3.TXT || exit 1
ext4ish "$IMG" 139264
p2=$(sum "$IMG" 139264 393216)
p3=$(sum "$IMG" 532480 1048576)
"$OUT/test_sd_part" old "$IMG" $TOT > "$OUT/old.log" 2>&1
st=$?
sed 's/^/    | /' "$OUT/old.log"
result $st "Plat's card: p2 and p4 refused, p1 kept, p3 (WIN9X) refused (v4.5)"
[ "$(sum "$IMG" 139264 393216)" = "$p2" ] && [ "$(sum "$IMG" 532480 1048576)" = "$p3" ]
result $? "Plat's card: p2 and p3 as they were, every sector"
echo "== the same card, p3 relabelled DATA32"
mlabel -i "$IMG@@$((532480 * 512))" ::DATA32 || exit 1
p2=$(sum "$IMG" 139264 393216)
"$OUT/test_sd_part" fat32 "$IMG" $TOT > "$OUT/fat32.log" 2>&1
st=$?
sed 's/^/    | /' "$OUT/fat32.log"
result $st "a FAT32 p3 not labelled WIN9X: given whole and read"
[ "$(sum "$IMG" 139264 393216)" = "$p2" ]
result $? "the FAT32 card: p2 as it was, every sector"

rm -f "$OUT"/card-*.img
echo "sdpart: $pass passed, $fail failed"
[ $fail = 0 ]
