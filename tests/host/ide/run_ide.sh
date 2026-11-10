#!/bin/bash
# v5.2: the blocks of an IDE command gathered and read ahead, so that a
# command costs the card a few big reads or writes rather than one for each
# block (ide.c's IDE_ENABLE_WRITE_GATHER, from upstream's PR 70, and
# IDE_ENABLE_READ_AHEAD, both on in the boards' builds).
#
# 1. test_ide_gather.c: ide.c with them (side B) against ide.c without (side
#    A), driven side by side by a pretend guest: every value read at the
#    ports, every change of the interrupt line and both disks after every
#    command must be the same.  (v5.3) The front end's flush,
#    ide_write_pending(), comes between commands and in the middle of them,
#    after which both disks must be the same at once.  The one difference allowed is PR 70's own,
#    a gathered write that fails, which is checked against a model of it.
#    Side A is ide.c at bd091f7 (v5.2 before this work) when the tree has
#    that commit, else this tree's ide.c without the two defines.  Side B:
#    this tree's ide.c as the boards build it, then with each define alone,
#    with READ_AHEAD_SECTORS 128, and with neither.  Then copies of ide.c
#    with a fault planted, each of which the test has to see.
# 2. ide_count.c: a made-up mix of commands to ide.c on a disk image on a
#    FAT volume, through esp/main/disk_image.c and ESP-IDF's own FatFs, with
#    and without the defines: the same data read and the same image after,
#    and fewer card commands with them; the counts are printed.  With
#    IDE_TRACE (a run's port traffic, from ide_trace.c) and IDE_TRACE_IMAGE
#    (the disk image the run started from), that traffic too, which must
#    read back what the run read.  Needs mkfs.fat, mtools and ESP-IDF's
#    source at IDF_PATH (default ~/esp/esp-idf), which is only read; without
#    them this part says SKIP.
#
#   bash tests/host/ide/run_ide.sh [OUT]
#
# Exit status 0 when every check passes.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
OUT=${1:-$HERE/build}
mkdir -p "$OUT" || exit 2
OUT=$(cd "$OUT" && pwd)
CC="gcc -D_FILE_OFFSET_BITS=64"
W="-Wall -Wextra -Wno-unused-parameter -Wno-sign-compare"
BOTH="-DIDE_ENABLE_WRITE_GATHER -DIDE_ENABLE_READ_AHEAD"

pass=0
fail=0
result() {
    if [ "$1" = 0 ]; then pass=$((pass + 1)); echo "  ok: $2"; else fail=$((fail + 1)); echo "  FAILED: $2"; fi
}

# ==== 1. the guest's view ====
# side NAME A|B ide.c WHAT [defines]: OUT/side-A-NAME.o, every symbol but
# the table local
side() {
    local name=$1 ab=$2 c=$3 what=$4
    shift 4
    local sym=ide_side_$(echo "$ab" | tr AB ab) wp=
    # (v5.3) the front end's flush, for an ide.c that has it
    grep -q "^void ide_write_pending" "$c" && wp=-DSIDE_WRITE_PENDING
    $CC -O1 -g -w -I"$HERE" -I"$TREE" -I"$TREE/tests/host/shim" -DIDE_C="\"$c\"" -DSIDE=$sym \
        -DSIDE_WHAT="\"$what\"" $wp "$@" -c -o "$OUT/side-$ab-$name.o" "$HERE/ide_side.c" || exit 1
    objcopy -G $sym "$OUT/side-$ab-$name.o" || exit 1
}
$CC -O1 -g $W -Werror -I"$HERE" -c -o "$OUT/test_ide_gather.o" "$HERE/test_ide_gather.c" || exit 1
$CC -O1 -g -w -I"$TREE" -c -o "$OUT/ide_stubs.o" "$HERE/ide_stubs.c" || exit 1
if git -C "$TREE" rev-parse -q --verify "bd091f7^{commit}" > /dev/null 2>&1; then
    git -C "$TREE" show bd091f7:ide.c > "$OUT/ide-bd091f7.c"
    side ref A "$OUT/ide-bd091f7.c" "ide.c at bd091f7"
else
    echo "(no bd091f7 here: side A is this tree's ide.c without the two defines)"
    side ref A "$TREE/ide.c" "this tree's ide.c without the defines"
fi
# diff NAME SEED SEQUENCES STEPS: B NAME against A ref; OUT/diff-NAME.log
diff_run() {
    $CC -o "$OUT/test_ide_gather-$1" "$OUT/test_ide_gather.o" "$OUT/ide_stubs.o" \
        "$OUT/side-A-ref.o" "$OUT/side-B-$1.o" || exit 1
    "$OUT/test_ide_gather-$1" "$2" "$3" "$4" > "$OUT/diff-$1.log" 2>&1
}
side board B "$TREE/ide.c" "this tree's ide.c as the boards build it" $BOTH
side gather B "$TREE/ide.c" "this tree's ide.c, IDE_ENABLE_WRITE_GATHER alone" -DIDE_ENABLE_WRITE_GATHER
side ahead B "$TREE/ide.c" "this tree's ide.c, IDE_ENABLE_READ_AHEAD alone" -DIDE_ENABLE_READ_AHEAD
side ahead128 B "$TREE/ide.c" "this tree's ide.c, both, READ_AHEAD_SECTORS 128" $BOTH \
    -DREAD_AHEAD_SECTORS=128
side neither B "$TREE/ide.c" "this tree's ide.c without the defines"

echo "== ide.c as the boards build it, against ide.c without the gathering and the read ahead"
diff_run board 1 200 300
st=$?
sed 's/^/    | /' "$OUT/diff-board.log"
result $st "the guest sees the same, but for PR 70's documented failed writes"
for v in "gather 2 80 300" "ahead 3 80 300" "ahead128 4 80 300" "neither 5 30 300"; do
    set -- $v
    diff_run $1 $2 $3 $4
    st=$?
    sed -n 's/^B: \(.*\) (gathers.*/    | \1:/p; s/^\(test_ide_gather: .*\)/    | \1/p; s/^  FAIL/    | FAIL/p' \
        "$OUT/diff-$1.log"
    result $st "$(sed -n 's/^B: \(.*\) (gathers.*/\1/p' "$OUT/diff-$1.log"): the same"
done

# planted NAME WHAT SED: a copy of ide.c with SED's fault in it, which the
# test must see
planted() {
    sed -e "$3" "$TREE/ide.c" > "$OUT/ide-$1.c"
    local nl
    nl=$(diff "$TREE/ide.c" "$OUT/ide-$1.c" | grep -c '^<')
    side $1 B "$OUT/ide-$1.c" "$2" $BOTH
    diff_run $1 6 60 300
    local st=$?
    grep -m1 "FAIL" "$OUT/diff-$1.log" | cut -c1-150 | sed 's/^ */    | /'
    tail -1 "$OUT/diff-$1.log" | sed 's/^/    | /'
    [ "$nl" -gt 0 ] && [ $st = 1 ]
    result $? "the test sees it: $2 ($nl line$([ "$nl" = 1 ] || echo s) of ide.c changed)"
}
echo "== faults planted in copies of ide.c"
planted stale "what was read ahead kept from one command to the next" \
    '/^static void ide_new_command/,/^}/{/^    s->rcount = 0;$/d}'
planted where "what was read ahead given without a look at where the guest asked" \
    's/if (s->rcount < n || s->rsector != sector_num) {/if (s->rcount < n) {/'
planted regs "a failed gathered write's registers left at its last block" \
    '/^            ide_set_sector(s, s->wsector);$/d'
planted left "a write command left before its end losing what it gathered (PR 70 as upstream has it)" \
    's/ide_write_gathered([sd]);/;/'
# v5.3: the data port straight at the read-ahead and gathering buffers
planted flushlost "the front end's flush writing nothing of a command left part way" \
    '/^void ide_write_pending/,/^}/s/ide_write_gathered(sif->drives\[i\]);/;/'
planted stale "the data port left at the read-ahead buffer when a transfer starts" \
    '/^static void ide_transfer_start(IDEState \*s, int size,/,/^}/{/IDE_DATA_AT/d}'
planted notmoved "a block the guest wrote at the gathering buffer's old end left there" \
    '/^        if (blk != s->wbuf + s->wcount \* 512)$/,/memmove/d'
planted aheadoff "a block read ahead given from one block on" \
    's/^    uint8_t \*p = s->rbuf + s->rpos \* 512;$/    uint8_t *p = s->rbuf + (s->rpos + 1) * 512;/'

# ==== 2. what it costs the card ====
IDF=${IDF_PATH:-$HOME/esp/esp-idf}
F=$IDF/components/fatfs
S=$TREE/tests/host/storage
export MTOOLS_SKIP_CHECK=1
export PATH=$PATH:/sbin:/usr/sbin
skip=
for t in mkfs.fat mcopy; do
    command -v $t > /dev/null || skip="no $t"
done
[ -f "$F/src/ff.c" ] || skip="no ESP-IDF FatFs at $F"
if [ -n "$skip" ]; then
    echo "SKIP ide card commands: $skip"
else
    INC="-I$S/stubs -I$F/src -I$F/diskio -I$S -I$TREE -I$TREE/esp/main"
    for f in src/ff.c src/ffunicode.c diskio/diskio.c diskio/diskio_sdmmc.c; do
        $CC -O1 -g -w $INC -c -o "$OUT/idf_$(basename $f .c).o" "$F/$f" || exit 1
    done
    $CC -O1 -g -w $INC -c -o "$OUT/host_glue.o" "$S/host_glue.c" || exit 1
    $CC -O1 -g $W -Werror $INC -c -o "$OUT/disk_image.o" "$TREE/esp/main/disk_image.c" || exit 1
    $CC -O1 -g $W -Werror $INC -c -o "$OUT/sd_part.o" "$TREE/esp/main/sd_part.c" || exit 1
    count_build() {     # NAME ide.c [defines]
        local name=$1 c=$2
        shift 2
        $CC -O1 -g -w -DIDE_C="\"$c\"" "$@" $INC -o "$OUT/ide_count-$name" "$HERE/ide_count.c" \
            "$OUT/host_glue.o" "$OUT/disk_image.o" "$OUT/sd_part.o" "$OUT"/idf_ff.o \
            "$OUT"/idf_ffunicode.o "$OUT"/idf_diskio.o "$OUT"/idf_diskio_sdmmc.o \
            -Wl,--wrap=f_read,--wrap=f_write,--wrap=ff_disk_read,--wrap=ff_disk_write || exit 1
    }
    count_build without "$TREE/ide.c"
    count_build with "$TREE/ide.c" $BOTH
    volume() {          # VOLUME BITS SECTORS-A-CLUSTER MIB IMAGE
        rm -f "$1"
        truncate -s $(($4 * 1024 * 1024)) "$1"
        mkfs.fat -F "$2" -s "$3" -n KORVO_SD "$1" > /dev/null || exit 1
        mcopy -o -i "$1" "$5" ::/IMAGE.IMG || exit 1
    }
    # from ide_count's line: the two numbers after a word ("card READS
    # WRITES"), and the hashes
    field() { sed -n "s/^\(.* \)*$2 \([0-9]*\) \([0-9]*\) .*/\2 \3/p" "$1"; }
    hashes() { sed -n 's/.*\(data [0-9a-f]* image [0-9a-f]*\).*/\1/p' "$1"; }
    compare() {         # WHAT: OUT/count-without.txt, OUT/count-with.txt
        local a=$OUT/count-without.txt b=$OUT/count-with.txt
        read rw_a ww_a <<< "$(field "$a" rw)"
        read rw_b ww_b <<< "$(field "$b" rw)"
        read cr_a cw_a <<< "$(field "$a" card)"
        read cr_b cw_b <<< "$(field "$b" card)"
        echo "    | without: $rw_a reads and $ww_a writes of the image, $cr_a and $cw_a card commands"
        echo "    | with:    $rw_b reads and $ww_b writes of the image, $cr_b and $cw_b card commands"
        [ -n "$(hashes "$a")" ] && [ "$(hashes "$a")" = "$(hashes "$b")" ]
        result $? "$1: the same data read and the same image after, with and without"
        [ "$cr_b" -lt "$cr_a" ] && [ "$cw_b" -lt "$cw_a" ]
        result $? "$1: fewer card commands with them"
    }
    head -c $((16 * 1024 * 1024)) /dev/zero | tr '\0' '\345' > "$OUT/mix.img"
    for v in "32 8 300 FAT32 with 8 sectors a cluster" "16 16 400 FAT16 with 16 sectors a cluster"; do
        set -- $v
        bits=$1 spc=$2 mib=$3
        shift 3
        echo "== a made-up mix of 3,000 commands, on $*"
        for side in without with; do
            volume "$OUT/vol.img" $bits $spc $mib "$OUT/mix.img"
            "$OUT/ide_count-$side" "$OUT/vol.img" $((mib * 2048)) mix 7 3000 \
                > "$OUT/count-$side.txt" 2>&1
        done
        compare "the mix on $*"
    done
    if [ -n "${IDE_TRACE:-}" ] && [ -n "${IDE_TRACE_IMAGE:-}" ]; then
        echo "== the port traffic in $IDE_TRACE, on FAT32 with 8 sectors a cluster"
        st=0
        for side in without with; do
            volume "$OUT/vol.img" 32 8 300 "$IDE_TRACE_IMAGE"
            "$OUT/ide_count-$side" "$OUT/vol.img" $((300 * 2048)) trace "$IDE_TRACE" \
                > "$OUT/count-$side.txt" 2>&1 || st=1
        done
        result $st "the traffic reads back, with and without, what the run read"
        compare "the traffic"
    fi
    rm -f "$OUT/vol.img"
fi

echo "ide: $pass passed, $fail failed"
[ $fail = 0 ]
