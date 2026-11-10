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
# From v3.8 ide.c opens an image on a card volume through FatFs itself
# (esp/main/disk_image.c): each volume is tested that way, with the image's
# clusters mapped (no FAT read for any seek, in an image of 300 pieces) and
# with a card that refuses a read or a write (the image opened again, the
# access done); and again through C stdio, ide.c's other path, where one
# refused read makes FatFs refuse the image from then on (the test seeing
# the fault that disk_image.c removes).  v3.8 also counts every guest read
# and write for the status LED's disk light (ide_activity), one past the
# image's end included.
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
INC="-I$S/stubs -I$F/src -I$F/diskio -I$S -I$TREE -I$TREE/esp/main"
for f in src/ff.c src/ffunicode.c diskio/diskio.c diskio/diskio_sdmmc.c; do
    $CC -O1 -g -w $INC -c -o "$OUT/idf_$(basename $f .c).o" "$F/$f" || exit 1
done
$CC -O1 -g -w $INC -c -o "$OUT/host_glue.o" "$S/host_glue.c" || exit 1
# disk_image.c as the board's main component builds it: warnings are errors
$CC -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Werror $INC \
    -c -o "$OUT/disk_image.o" "$TREE/esp/main/disk_image.c" || exit 1
# v4.4: ide.c also reaches a card partition the guest has whole (sd_part.c;
# its own test is tests/host/sdpart); linked here, never opened
$CC -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Werror $INC \
    -c -o "$OUT/sd_part.o" "$TREE/esp/main/sd_part.c" || exit 1
build() {       # name ide.c
    # ide.c's own warnings are not this test's business (-w after -Wall)
    $CC -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -w \
        -DIDE_C="\"$2\"" $INC -o "$OUT/test_ide_fatfs-$1" "$HERE/test_ide_fatfs.c" \
        "$OUT/host_glue.o" "$OUT/disk_image.o" "$OUT/sd_part.o" "$OUT"/idf_ff.o "$OUT"/idf_ffunicode.o \
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
one() {         # name ide-build bits sectors-per-cluster [stdio]: OUT/name.log, OUT/name.status
    local img=$OUT/vol-$1.img
    volume "$img" "$3" "$4"
    "$OUT/test_ide_fatfs-$2" "$img" 131072 ${5:-} > "$OUT/$1.log" 2>&1
    local st=$?
    fsck.fat -n "$img" > "$OUT/$1-fsck.log" 2>&1
    local fk=$?
    [ "$(mtype -i "$img" ::/OTHER.TXT)" = "another file on the volume" ]
    local other=$?
    rm -f "$img"
    echo "$st $fk $other" > "$OUT/$1.status"
}

echo "== FAT32, 1 sector a cluster (the review's case), through disk_image.c"
one fat32 now 32 1
sed 's/^/    | /' "$OUT/fat32.log"
read st fk other < "$OUT/fat32.status"
result $st "FAT32: past-end reads and writes refused, the image and the free space unchanged; mapped, no FAT read for a seek; a refused read or write got over"
result $fk "FAT32: fsck.fat -n finds the volume clean"
result $other "FAT32: another file on the volume intact"

echo "== FAT16, 4 sectors a cluster (Plat's p1, KORVO_SD), through disk_image.c"
one fat16 now 16 4
sed 's/^/    | /' "$OUT/fat16.log"
read st fk other < "$OUT/fat16.status"
result $st "FAT16: past-end reads and writes refused, the image and the free space unchanged; mapped, no FAT read for a seek; a refused read or write got over"
result $fk "FAT16: fsck.fat -n finds the volume clean"
result $other "FAT16: another file on the volume intact"

for v in "32 1" "16 4"; do
    set -- $v
    echo "== FAT$1, through C stdio (ide.c's path for an image on no card volume)"
    one fat$1-stdio now $1 $2 stdio
    sed 's/^/    | /' "$OUT/fat$1-stdio.log"
    read st fk other < "$OUT/fat$1-stdio.status"
    result $st "FAT$1, stdio: past-end reads and writes refused, the image and the free space unchanged; the old sticky error seen"
    result $fk "FAT$1, stdio: fsck.fat -n finds the volume clean"
    result $other "FAT$1, stdio: another file on the volume intact"
done

# ==== v5.3: the card's DMA straight into and out of the guest's disk buffers ====
# test_image_dma.c: esp/main/disk_image.c with the SD driver's buffer check
# (host_glue.c's stand-in), on a 300 MiB FAT32 volume with 4 KiB clusters
# and a 16 MiB image in it:
# disk_image_read() and _write() from buffers the check takes and ones it
# refuses, and IDE commands through the ports; then with no internal RAM
# for v5.2's 16 sectors; then v5.2's disk_image.c (3e386ff), which must
# give the same log and the same image, every byte; then three faults.
echo "== v5.3: an image's runs straight between the card and the guest's buffers"
dma_build() {   # NAME disk_image.o
    $CC -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -w \
        -DIDE_C="\"$TREE/ide.c\"" $INC -o "$OUT/test_image_dma-$1" "$HERE/test_image_dma.c" \
        "$OUT/host_glue.o" "$2" "$OUT/sd_part.o" "$OUT"/idf_ff.o "$OUT"/idf_ffunicode.o \
        "$OUT"/idf_diskio.o "$OUT"/idf_diskio_sdmmc.o -Wl,--wrap=f_read,--wrap=f_write || exit 1
}
dma_volume() {
    rm -f "$1"
    truncate -s $((614400 * 512)) "$1"
    mkfs.fat -F 32 -s 8 -n KORVO_SD "$1" > /dev/null || exit 1
    python3 -I -c "import sys; sys.stdout.buffer.write(bytes((i * 131 + (i >> 9) * 7 + 3) & 255 for i in range(16 << 20)))" > "$OUT/dma-image.img"
    mcopy -o -i "$1" "$OUT/dma-image.img" ::/IMAGE.IMG || exit 1
    rm -f "$OUT/dma-image.img"
}
dma_run() {     # NAME COPY,MORE DMA [short]: OUT/dma-NAME.out, OUT/dma-NAME.log
    dma_volume "$OUT/dma-vol-$1.img"
    "$OUT/test_image_dma-$1" "$OUT/dma-vol-$1.img" 614400 0x7a11 "$OUT/dma-$1.log" $2 $3 ${4:-} \
        > "$OUT/dma-$1.out" 2>&1
    local st=$?
    fsck.fat -n "$OUT/dma-vol-$1.img" > "$OUT/dma-$1-fsck.log" 2>&1 || st=1
    rm -f "$OUT/dma-vol-$1.img"
    return $st
}
cp "$OUT/disk_image.o" "$OUT/disk_image-tree.o"
dma_build tree "$OUT/disk_image-tree.o"
dma_run tree 4,16 1
st=$?
grep -E "said|reads:|operations|commands|FAIL|test_image_dma" "$OUT/dma-tree.out" | cut -c1-170 | sed 's/^/    | /'
result $st "disk_image.c: an image's reads and writes right with every kind of buffer; a buffer the driver takes in one FatFs call, one it refuses through internal RAM (4 sectors a call, 16 once a big one is refused); none handed to the driver to copy; fsck.fat clean"
dma_build lowram "$OUT/disk_image-tree.o"
dma_run lowram 4,4 1 short
st=$?
grep -E "FAIL|test_image_dma" "$OUT/dma-lowram.out" | sed 's/^/    | low RAM: /'
result $st "disk_image.c with no internal RAM for v5.2's 16 sectors when a big buffer is refused: right, 4 sectors a call"
cmp -s "$OUT/dma-tree.log" "$OUT/dma-lowram.log"
result $? "  the guest saw the same as with 16, and the image ended the same"
if git -C "$TREE" rev-parse -q --verify "3e386ff^{commit}" > /dev/null 2>&1; then
    git -C "$TREE" show 3e386ff:esp/main/disk_image.c > "$OUT/disk_image-v52.c"
    $CC -O1 -g -w $INC -c -o "$OUT/disk_image-v52.o" "$OUT/disk_image-v52.c" || exit 1
    dma_build v52 "$OUT/disk_image-v52.o"
    dma_run v52 16,16 0
    st=$?
    grep -E "FAIL|test_image_dma|reads:|card commands" "$OUT/dma-v52.out" | cut -c1-170 | sed 's/^/    | v5.2: /'
    result $st "v5.2's disk_image.c (3e386ff), the same operations: right, 16 sectors a call"
    cmp -s "$OUT/dma-tree.log" "$OUT/dma-v52.log"
    result $? "the guest saw the same with both, every read, status and error, and the image ended the same, every byte ($(wc -l < "$OUT/dma-tree.log") operations)"
else
    echo "    | (no 3e386ff in this tree: not held against v5.2's disk_image.c)"
fi
dma_planted() {         # NAME WHAT SED
    sed -e "$3" "$TREE/esp/main/disk_image.c" > "$OUT/disk_image-$1.c"
    local nl st
    nl=$(diff "$TREE/esp/main/disk_image.c" "$OUT/disk_image-$1.c" | grep -c '^<')
    $CC -O1 -g -w $INC -c -o "$OUT/disk_image-$1.o" "$OUT/disk_image-$1.c" || exit 1
    dma_build $1 "$OUT/disk_image-$1.o"
    dma_run $1 4,16 1
    st=$?
    grep -m1 "FAIL" "$OUT/dma-$1.out" | cut -c1-150 | sed 's/^ */    | /'
    [ "$nl" -gt 0 ] && [ $st != 0 ]
    result $? "the test sees it: $2 ($nl line$([ "$nl" = 1 ] || echo s) of disk_image.c changed)"
}
dma_planted unasked "every buffer straight to FatFs, the driver's check not asked" \
    's/bool direct = !s_buf || storage_card_dma_ok(buf, n);/bool direct = true;/'
dma_planted stride "an access through the internal buffer, each piece at the start of the guest's" \
    's/^\t\tbuf += len;$/\t\t(void) 0;/'
dma_planted nomore "v5.2's 16 sectors not taken when a big buffer is refused" \
    's/^\tuint8_t \*more = heap_caps_aligned_alloc(64, DI_BUF_MORE \* 512,$/\tuint8_t *more = NULL; (void) heap_caps_aligned_alloc(64, DI_BUF_MORE * 512,/'

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
