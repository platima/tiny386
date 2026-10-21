#!/bin/bash
# storage.c's card partitions (FIRMWARE-PLAN 3.4) against ESP-IDF's own
# FatFs, on card images laid out as Plat's card and some others.
#
#   bash tests/host/storage/run_storage.sh [OUT]
#
# Needs gcc, sfdisk, mkfs.fat and fsck.fat (dosfstools), mtools and
# python3, and ESP-IDF at IDF_PATH (default ~/esp/esp-idf), which is only
# read.  The card images go to OUT: sparse,
# about 800 MB each as ls shows them, a few MB each on the disk.
# Exit status 0 when every scenario passes; a missing tool is a SKIP.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
OUT=${1:-$HERE/build}
IDF=${IDF_PATH:-$HOME/esp/esp-idf}
F=$IDF/components/fatfs
export MTOOLS_SKIP_CHECK=1
mkdir -p "$OUT" || exit 2
for t in sfdisk mkfs.fat fsck.fat mcopy mmd mtype python3; do
    command -v $t >/dev/null || { echo "SKIP storage: no $t"; exit 0; }
done
[ -f "$F/src/ff.c" ] || { echo "SKIP storage: no ESP-IDF FatFs at $F"; exit 0; }

# ==== the program ====
# storage.c's partition code as it is in the file: from the comment that
# begins it to the one that begins storage_init()
S=$TREE/esp/main/storage.c
a=$(grep -n "^ \* The card's other partitions" "$S" | cut -d: -f1)
b=$(grep -n "^/\* Bit 0: the card is mounted at /sdcard" "$S" | cut -d: -f1)
if [ -z "$a" ] || [ -z "$b" ]; then
    echo "FAIL storage: the partition code was not found in storage.c"
    exit 1
fi
sed -n "$((a - 1)),$((b - 1))p" "$S" > "$OUT/storage_parts.c"
CC="gcc -D_FILE_OFFSET_BITS=64"
INC="-I$HERE/stubs -I$F/src -I$F/diskio -I$TREE/esp/main -I$HERE"
for f in src/ff.c src/ffunicode.c diskio/diskio.c diskio/diskio_sdmmc.c; do
    # ESP-IDF's own files: its warnings are not this test's business
    $CC -O1 -g -w $INC -c -o "$OUT/idf_$(basename $f .c).o" "$F/$f" || exit 1
done
# -Wno-format-truncation: storage.c copies FatFs's label (a 24-byte buffer)
# into 16 bytes; a FAT label is 11 characters at most, which gcc cannot see
$CC -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-format-truncation \
    -DSD_CLK=1 -DSTORAGE_PARTS_C="\"$OUT/storage_parts.c\"" $INC -o "$OUT/test_storage" \
    "$HERE/test_storage.c" "$HERE/host_glue.c" "$OUT"/idf_ff.o "$OUT"/idf_ffunicode.o \
    "$OUT"/idf_diskio.o "$OUT"/idf_diskio_sdmmc.o || exit 1
if [ -n "${KORVO1_SDKCONFIG_H:-}" ]; then
    # the stand-in sdkconfig.h against a real build's
    if diff <(grep -E "CONFIG_(FATFS|WL)_" "$KORVO1_SDKCONFIG_H") \
            <(grep -E "^#define CONFIG_" "$HERE/stubs/sdkconfig.h") >/dev/null; then
        echo "stubs/sdkconfig.h: the same FatFs lines as $KORVO1_SDKCONFIG_H"
    else
        echo "FAIL storage: stubs/sdkconfig.h differs from $KORVO1_SDKCONFIG_H"
        exit 1
    fi
fi

# ==== the cards ====
fat() {         # image start sectors bits label id [sectors per cluster]
    local s=
    [ -n "${7:-}" ] && s="-s $7"
    mkfs.fat -F "$4" -n "$5" -i "$6" -h "$2" $s --offset="$2" "$1" $(($3 / 2)) \
        > "$OUT/mkfs.log" 2>&1 || { cat "$OUT/mkfs.log"; exit 1; }
}
put() {         # image start file path-on-the-partition
    mcopy -o -i "$1@@$(($2 * 512))" "$3" "::$4" || exit 1
}
mkd() {         # image start folder
    mmd -i "$1@@$(($2 * 512))" "::$3" || exit 1
}
mbr() {         # image, sfdisk's script on stdin
    sfdisk -q --no-reread --no-tell-kernel "$1" > /dev/null || exit 1
}
ext4ish() {     # image start: ext4's superblock magic (at byte 1080), no boot sector
    python3 - "$1" "$2" <<'PY'
import sys
with open(sys.argv[1], 'r+b') as f:
    f.seek(int(sys.argv[2]) * 512 + 1080)
    f.write(b'\x53\xef')
PY
}
exfat_vbr() {   # image start: an exFAT boot sector
    python3 - "$1" "$2" <<'PY'
import sys
s = bytearray(512)
s[0:3] = b'\xeb\x76\x90'
s[3:11] = b'EXFAT   '
s[510:512] = b'\x55\xaa'
with open(sys.argv[1], 'r+b') as f:
    f.seek(int(sys.argv[2]) * 512)
    f.write(s)
PY
}
new_image() {   # image sectors
    rm -f "$1"
    truncate -s $(($2 * 512)) "$1"
}

printf '; p1 freedos.ini\n' > "$OUT/freedos.ini"
for n in 2 3 4; do printf 'p%s marker\n' $n > "$OUT/P$n.TXT"; done
# win95.img: 4 MiB of a pattern test_storage.c knows (byte i is i * 7 + i / 512)
python3 -c "import sys; sys.stdout.buffer.write(bytes((i * 7 + (i >> 9)) & 255 for i in range(4 << 20)))" \
    > "$OUT/win95.img"

# Plat's card: FAT32 KORVO_SD p1 at 8192 (64 MiB), ext4 p2 at 139264
# (192 MiB), FAT32 WIN9X p3 at 532480 (512 MiB here; its real size is
# Plat's choice).  be5b725's own card image has p1 as FAT16 with 4 sectors
# a cluster, in a type 0x0C partition (boot safety review, round 2, note
# 5): p1-bits 16 makes that.
plat_card() {   # image p3-label p3-serial [p3-type: c, or 7 for exFAT] [p1-bits: 32 or 16]
    local img=$1 t=${4:-c} p1=${5:-32}
    new_image "$img" $((532480 + 1048576))
    printf 'label: dos\nstart=8192, size=131072, type=c\nstart=139264, size=393216, type=83\nstart=532480, size=1048576, type=%s\n' "$t" | mbr "$img"
    if [ "$p1" = 16 ]; then
        fat "$img" 8192 131072 16 KORVO_SD 1a2b3c4d 4
    else
        fat "$img" 8192 131072 32 KORVO_SD 1a2b3c4d 1
    fi
    mkd "$img" 8192 /tiny386
    put "$img" 8192 "$OUT/freedos.ini" /tiny386/freedos.ini
    ext4ish "$img" 139264
    if [ "$t" = c ]; then
        fat "$img" 532480 1048576 32 "$2" "$3"
        put "$img" 532480 "$OUT/win95.img" /win95.img
        put "$img" 532480 "$OUT/P3.TXT" /P3.TXT
    else
        exfat_vbr "$img" 532480
    fi
}

four_card() {
    local img=$1
    new_image "$img" $((1253376 + 262144))
    printf 'label: dos\nstart=8192, size=131072, type=c\nstart=139264, size=65536, type=e\nstart=204800, size=1048576, type=c\nstart=1253376, size=262144, type=c\n' | mbr "$img"
    fat "$img" 8192 131072 32 KORVO_SD 1a2b3c4d 1
    mkd "$img" 8192 /tiny386
    put "$img" 8192 "$OUT/freedos.ini" /tiny386/freedos.ini
    fat "$img" 139264 65536 16 P2 22222222
    put "$img" 139264 "$OUT/P2.TXT" /P2.TXT
    fat "$img" 204800 1048576 32 WIN9X 33333333
    put "$img" 204800 "$OUT/P3.TXT" /P3.TXT
    fat "$img" 1253376 262144 32 P4 44444444 1
    put "$img" 1253376 "$OUT/P4.TXT" /P4.TXT
}

superfloppy_card() {
    local img=$1
    new_image "$img" 262144
    fat "$img" 0 262144 32 KORVO_SD 1a2b3c4d 1
    mkd "$img" 0 /tiny386
    put "$img" 0 "$OUT/freedos.ini" /tiny386/freedos.ini
}

p1linux_card() {
    local img=$1
    new_image "$img" $((139264 + 131072))
    printf 'label: dos\nstart=8192, size=131072, type=83\nstart=139264, size=131072, type=c\n' | mbr "$img"
    ext4ish "$img" 8192
    fat "$img" 139264 131072 32 KORVO_SD 1a2b3c4d 1
    mkd "$img" 139264 /tiny386
    put "$img" 139264 "$OUT/freedos.ini" /tiny386/freedos.ini
}

# ==== the scenarios ====
pass=0
fail=0
result() {
    if [ "$1" = 0 ]; then pass=$((pass + 1)); echo "  ok: $2"; else fail=$((fail + 1)); echo "  FAILED: $2"; fi
}
sum() {         # image start sectors: the region's SHA-256
    dd if="$1" bs=512 skip="$2" count="$3" status=none | sha256sum | cut -c1-16
}

# after test_storage has written to Plat's card: both FAT volumes clean,
# the writes there, the MBR and p2 untouched
check_written() {
    local img=$1 before_mbr=$2 before_p2=$3
    [ "$(sum "$img" 0 1)" = "$before_mbr" ]
    result $? "$4: the MBR is as it was"
    [ "$(sum "$img" 139264 393216)" = "$before_p2" ]
    result $? "$4: p2 (ext4) is as it was, every sector"
    dd if="$img" of="$OUT/$4-p1.img" bs=512 skip=8192 count=131072 conv=sparse status=none
    dd if="$img" of="$OUT/$4-p3.img" bs=512 skip=532480 count=1048576 conv=sparse status=none
    fsck.fat -n "$OUT/$4-p1.img" > "$OUT/$4-fsck-p1.log" 2>&1
    result $? "$4: fsck.fat -n finds p1 clean ($(tail -1 "$OUT/$4-fsck-p1.log"))"
    fsck.fat -n "$OUT/$4-p3.img" > "$OUT/$4-fsck-p3.log" 2>&1
    result $? "$4: fsck.fat -n finds p3 clean ($(tail -1 "$OUT/$4-fsck-p3.log"))"
    rm -f "$OUT/$4-p1.img" "$OUT/$4-p3.img"
    mcopy -o -i "$img@@$((532480 * 512))" ::/win95.img "$OUT/$4-win95-after.img" &&
    python3 - "$OUT/win95.img" "$OUT/$4-win95-after.img" <<'PY'
import sys
want = bytearray(open(sys.argv[1], 'rb').read())
want[1 << 20:(1 << 20) + 4096] = b'\xa5' * 4096
sys.exit(0 if open(sys.argv[2], 'rb').read() == bytes(want) else 1)
PY
    result $? "$4: win95.img as mtools reads it: the pattern, with the 4 KiB written at 1 MiB"
    [ "$(mtype -i "$img@@$((532480 * 512))" ::/NEW.TXT 2>/dev/null)" = "written on the WIN9X partition by test_storage" ]
    result $? "$4: NEW.TXT on p3 as mtools reads it"
    [ "$(mtype -i "$img@@$((8192 * 512))" ::/tiny386/lastboot.txt 2>/dev/null)" = "win95.ini" ]
    result $? "$4: tiny386/lastboot.txt on p1 as mtools reads it"
    rm -f "$OUT/$4-win95-after.img"
}

run() {         # scenario image
    "$OUT/test_storage" "$1" "$2"
    result $? "$1: test_storage"
}

echo "== Plat's card"
IMG=$OUT/card-plat.img
plat_card "$IMG" WIN9X 5e6f7a8b
m=$(sum "$IMG" 0 1); p2=$(sum "$IMG" 139264 393216)
run plat "$IMG"
check_written "$IMG" "$m" "$p2" plat

echo "== Plat's card, p1 FAT16 as be5b725's card image has it"
IMG=$OUT/card-plat16.img
plat_card "$IMG" WIN9X 5e6f7a8b c 16
m=$(sum "$IMG" 0 1); p2=$(sum "$IMG" 139264 393216)
run plat16 "$IMG"
check_written "$IMG" "$m" "$p2" plat16

echo "== Plat's card, p3 with p1's serial number"
IMG=$OUT/card-sameserial.img
plat_card "$IMG" WIN9X 1a2b3c4d
m=$(sum "$IMG" 0 1); p2=$(sum "$IMG" 139264 393216)
run sameserial "$IMG"
check_written "$IMG" "$m" "$p2" sameserial

echo "== Plat's card, p3 labelled DATA"
IMG=$OUT/card-nolabel.img
plat_card "$IMG" DATA 5e6f7a8b
run nolabel "$IMG"

echo "== Plat's card, no logical drive left"
IMG=$OUT/card-nodrive.img
plat_card "$IMG" WIN9X 5e6f7a8b
run nodrive "$IMG"

echo "== Plat's card, p3 exFAT"
IMG=$OUT/card-exfat.img
plat_card "$IMG" WIN9X 5e6f7a8b 7
run exfat "$IMG"

echo "== four FAT partitions"
IMG=$OUT/card-four.img
four_card "$IMG"
run four "$IMG"

echo "== no partition table"
IMG=$OUT/card-superfloppy.img
superfloppy_card "$IMG"
run superfloppy "$IMG"

echo "== p1 Linux, p2 FAT32"
IMG=$OUT/card-p1linux.img
p1linux_card "$IMG"
run p1linux "$IMG"

rm -f "$OUT"/card-*.img
echo "storage: $pass passed, $fail failed"
[ $fail = 0 ]
