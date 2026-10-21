#!/bin/bash
# ide.c's disk images through ESP-IDF's own FatFs (boot safety review,
# round 2, m1): a guest sector past the end of its image must be refused,
# and must not make the image grow on the card.  Also the guest's ATA FLUSH
# CACHE (emulator correctness review, round 2, note 7): done, with the
# image's last writes put on the card.  On a FAT32 volume as the
# review's test had it, and on a FAT16 one laid out as Plat's p1 (KORVO_SD,
# 64 MiB, 4 sectors a cluster).  If the tree has the v3.1 tag, the same test
# is also run against v3.1's ide.c, where it has to fail.
#
#   bash tests/host/disk/run_disk.sh [OUT]
#
# Needs gcc, mkfs.fat, fsck.fat, mtools, and ESP-IDF's source at IDF_PATH
# (default ~/esp/esp-idf), which is only read; without them it says SKIP.
# Exit status 0 when every case passes.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
S=$TREE/tests/host/storage
OUT=${1:-$HERE/build}
IDF=${IDF_PATH:-$HOME/esp/esp-idf}
F=$IDF/components/fatfs
export MTOOLS_SKIP_CHECK=1
mkdir -p "$OUT" || exit 2
for t in mkfs.fat fsck.fat mcopy mdir mtype; do
    command -v $t >/dev/null || { echo "SKIP disk: no $t"; exit 0; }
done
[ -f "$F/src/ff.c" ] || { echo "SKIP disk: no ESP-IDF FatFs at $F"; exit 0; }

CC="gcc -D_FILE_OFFSET_BITS=64"
INC="-I$S/stubs -I$F/src -I$F/diskio -I$S -I$TREE"
for f in src/ff.c src/ffunicode.c diskio/diskio.c diskio/diskio_sdmmc.c; do
    $CC -O1 -g -w $INC -c -o "$OUT/idf_$(basename $f .c).o" "$F/$f" || exit 1
done
$CC -O1 -g -w $INC -c -o "$OUT/host_glue.o" "$S/host_glue.c" || exit 1
build() {       # name ide.c
    # ide.c's own warnings are not this test's business (-w after -Wall)
    $CC -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -w \
        -DIDE_C="\"$2\"" $INC -o "$OUT/test_ide_fatfs-$1" "$HERE/test_ide_fatfs.c" \
        "$OUT/host_glue.o" "$OUT"/idf_ff.o "$OUT"/idf_ffunicode.o \
        "$OUT"/idf_diskio.o "$OUT"/idf_diskio_sdmmc.o || exit 1
}
build now "$TREE/ide.c"

pass=0
fail=0
result() {
    if [ "$1" = 0 ]; then pass=$((pass + 1)); echo "  ok: $2"; else fail=$((fail + 1)); echo "  FAILED: $2"; fi
}
volume() {      # image bits sectors-per-cluster: 64 MiB, an 8 MiB image, another file
    rm -f "$1"
    truncate -s $((131072 * 512)) "$1"
    mkfs.fat -F "$2" -s "$3" -n KORVO_SD "$1" > /dev/null || exit 1
    head -c $((8 * 1024 * 1024)) /dev/zero > "$OUT/freedos.img"
    mcopy -o -i "$1" "$OUT/freedos.img" ::/FREEDOS.IMG || exit 1
    printf 'another file on the volume\n' > "$OUT/other.txt"
    mcopy -o -i "$1" "$OUT/other.txt" ::/OTHER.TXT || exit 1
    # a second image, for the ATA FLUSH CACHE check
    head -c $((1024 * 1024)) /dev/zero > "$OUT/flush.img"
    mcopy -o -i "$1" "$OUT/flush.img" ::/FLUSH.IMG || exit 1
}
one() {         # name ide-build bits sectors-per-cluster: OUT/name.log, OUT/name.status
    local img=$OUT/vol-$1.img
    volume "$img" "$3" "$4"
    "$OUT/test_ide_fatfs-$2" "$img" 131072 > "$OUT/$1.log" 2>&1
    local st=$?
    fsck.fat -n "$img" > "$OUT/$1-fsck.log" 2>&1
    local fk=$?
    [ "$(mtype -i "$img" ::/OTHER.TXT)" = "another file on the volume" ]
    local other=$?
    rm -f "$img"
    echo "$st $fk $other" > "$OUT/$1.status"
}

echo "== FAT32, 1 sector a cluster (the review's case)"
one fat32 now 32 1
sed 's/^/    | /' "$OUT/fat32.log"
read st fk other < "$OUT/fat32.status"
result $st "FAT32: past-end reads and writes refused, the image and the free space unchanged"
result $fk "FAT32: fsck.fat -n finds the volume clean"
result $other "FAT32: another file on the volume intact"

echo "== FAT16, 4 sectors a cluster (Plat's p1, KORVO_SD)"
one fat16 now 16 4
sed 's/^/    | /' "$OUT/fat16.log"
read st fk other < "$OUT/fat16.status"
result $st "FAT16: past-end reads and writes refused, the image and the free space unchanged"
result $fk "FAT16: fsck.fat -n finds the volume clean"
result $other "FAT16: another file on the volume intact"

if git -C "$TREE" rev-parse -q --verify v3.1 >/dev/null; then
    git -C "$TREE" show v3.1:ide.c > "$OUT/ide-v3.1.c"
    build v3.1 "$OUT/ide-v3.1.c"
    echo "== FAT32 again, v3.1's ide.c (where the image has to grow)"
    one fat32-v3.1 v3.1 32 1
    grep "FAIL\|test_ide_fatfs\|FREEDOS.IMG" "$OUT/fat32-v3.1.log" | head -8 | sed 's/^/    | /'
    read st fk other < "$OUT/fat32-v3.1.status"
    [ "$st" != 0 ]
    result $? "the test sees the fault in v3.1's ide.c"
fi

echo "disk: $pass passed, $fail failed"
[ $fail = 0 ]
