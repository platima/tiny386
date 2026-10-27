#!/bin/bash
# Builds tiny386 v5.0 (v3.9 merged with tiny386's master of 2026-10-22, and
# Windows at the screen's own size: VBE pictures drawn from the blocks of
# video memory written; v4.1 takes in the two reviews of v4.0; v4.2 makes a
# clear of the screen with no black between it and the picture after it,
# and Ctrl+Alt+F11 changes vbe_blocks; v4.3 adds the information port at
# 0F1F8h that T386MON reads; v4.4 saves the Ctrl+Alt keys' settings in the
# ini, adds two cures for tearing and gives the guest a partition of the
# card as its whole disk; v4.5 takes in the two reviews of v4.4; v4.6
# makes double buffering's two frame buffers with the panel at power-on,
# and keeps the guest's stores and DMA out of the devices' state; v5.0 puts
# a fast interpreter, Yejun Yang's from tab5-pc, in front of tiny386's own
# and lets the CPU store into the VBE frame buffer itself), bare
# metal under ESP-IDF, for the ESP32-S31-Korvo-1
# and for the DOS board, from tiny386's master as its author left it on
# 2026-10-22 (4ce2e06) and tiny386-idf-v4.patch beside this script (on the
# esp32-s31 branch: git diff 4ce2e06 s31-v5.0 -- . ':(exclude)s31').  Until
# v3.9 the base was the release 9ed786b; tiny386-idf-v3.patch is still the
# whole of it against 9ed786b (now giving the v5.0 tree), for comparing
# with the earlier patches, and is not used here.  Build only: this script
# never flashes anything and never opens a serial port.
#
#   bash build.sh [WORKDIR]
#
# WORKDIR (default $HOME/tiny386-idf-v4-build) is made if it is not there;
# WORKDIR/tiny386 and WORKDIR/images must not be there yet.  Everything is
# cloned and built inside WORKDIR; nothing outside it is written.
#
# One tree, three builds:
#   build-korvo1             the Korvo-1 beside Linux: the dual-boot partition
#                            table, tiny386 in its test partition at 0xE20000
#   build-korvo1-standalone  the Korvo-1 alone: tiny386's own partitions.csv,
#                            tiny386 at 0x10000 (Linux is overwritten)
#   build-dosboard           the DOS board: tiny386's own partitions.csv
# and WORKDIR/images, laid out as v3\images is:
#   korvo1-dualboot\     the dual-boot bootloader and table from dualboot\
#                        beside this script (built from the Linux loader's
#                        project, checked here by SHA-256) and tiny386.bin
#                        from build-korvo1
#   korvo1-dualboot-tiny386-default\
#                        the same, with tiny386 the default at reset: the
#                        table dualboot\partition-table-tiny386-default.bin
#                        (checked by SHA-256), the same bootloader, app and
#                        offsets; no ELF (korvo1-dualboot's is the same)
#   korvo1-standalone\   build-korvo1-standalone's bootloader, table and app
#   dosboard\            build-dosboard's bootloader, table and app
# each with flash_args (offsets for esptool) and SHA256SUMS.  tiny386.elf is
# there too (but in korvo1-dualboot-tiny386-default), to turn the addresses on a
# crash screen into function names.
#
# Each image is checked with esptool image-info, and each tiny386.elf for
# the layout of the status LED's panic bit-bang (esp/main/status_bb.h):
# WORKDIR/bb-layout-*.txt has the waits and what follows each.
#
# It needs git, make and a host cc (for fmopl.inc), the boards' ESP-IDF at
# $IDF_PATH (default $HOME/esp/esp-idf, v6.1-dev-8142-g188e3e55bb) with its
# tools in $IDF_TOOLS_PATH (default $HOME/.espressif-upstream), and network
# access for the clone and for the component manager.  BUILD-NOTES.md says
# what to expect.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
W=${1:-$HOME/tiny386-idf-v4-build}
IDF_PATH=${IDF_PATH:-$HOME/esp/esp-idf}
IDF_TOOLS_PATH=${IDF_TOOLS_PATH:-$HOME/.espressif-upstream}
TINY_GIT=https://github.com/hchunhui/tiny386.git
TINY_COMMIT=4ce2e06ccbaef6af2485fd31ae7c2e44856e7764
PATCH=$HERE/tiny386-idf-v4.patch
LOCK=$HERE/dependencies.lock
DB=$HERE/dualboot
# the dual-boot pair as built and checked (dualboot/DUALBOOT.md)
DB_BL_SHA=b57469cd1542fe83a4f468f93a7e8e9bfa5af138f15a38e6867a0a54fccff29f
DB_PT_SHA=d41cba3cf33bedee257c8709389c99d16101992439d7a6ed353967cd0fdd4346
# the same table with the two apps' subtypes swapped: tiny386 the default
DB_PT386_SHA=5b90d4166afc39d72a1d6c7b16f3a2e4a5cc0193a3e0eb50990a4dbb9da58226
# the partitions the apps go in (bytes)
TEST_PART=$((0x1E0000))         # dual-boot table, tiny386 at 0xE20000
FACTORY_PART=$((1792 * 1024))   # tiny386's own partitions.csv, at 0x10000

[ -f "$PATCH" ] || { echo "no patch at $PATCH"; exit 1; }
[ -f "$LOCK" ] || { echo "no dependencies.lock at $LOCK"; exit 1; }
[ -f "$IDF_PATH/export.sh" ] || { echo "no ESP-IDF at $IDF_PATH"; exit 1; }
for f in bootloader.bin partition-table.bin partition-table-tiny386-default.bin; do
    [ -f "$DB/$f" ] || { echo "no $f in $DB"; exit 1; }
done
echo "$DB_BL_SHA  $DB/bootloader.bin" | sha256sum -c --quiet
echo "$DB_PT_SHA  $DB/partition-table.bin" | sha256sum -c --quiet
echo "$DB_PT386_SHA  $DB/partition-table-tiny386-default.bin" | sha256sum -c --quiet
mkdir -p "$W"
cd "$W"
[ ! -e tiny386 ] || { echo "$W/tiny386 is there already: give an empty WORKDIR"; exit 1; }
[ ! -e images ] || { echo "$W/images is there already: give an empty WORKDIR"; exit 1; }

echo "== tiny386 $TINY_COMMIT"
git clone -q "$TINY_GIT" tiny386
cd tiny386
git checkout -q "$TINY_COMMIT"
# "git diff upstream-20261022" makes the patch again from this tree
git tag upstream-20261022
git apply --check "$PATCH"
git apply --index "$PATCH"
# the OPL synthesiser's tables, made by tiny386's own Makefile with the
# host's compiler (the ESP-IDF build stops without them)
make prepare HOSTCC=cc
# the component versions the patch was built with
cp "$LOCK" esp/dependencies.lock
cd esp

# idf.py in a clean environment (export.sh has to be sourced from bash);
# the builds run one after another, since each copies its board's
# sdkconfig to esp/sdkconfig
idf() {
    env -i HOME="$HOME" PATH=/usr/bin:/bin:/usr/sbin:/sbin \
        IDF_PATH="$IDF_PATH" IDF_TOOLS_PATH="$IDF_TOOLS_PATH" \
        bash -c '. "$IDF_PATH/export.sh" >/dev/null 2>&1 && "$@"' idf "$@"
}
build() {       # name, board, more idf.py arguments
    local name=$1 board=$2
    shift 2
    echo "== ESP-IDF build: $name (BOARD=$board $*)"
    idf idf.py -B "build-$name" -DBOARD="$board" -DIDF_TARGET=esp32s31 "$@" build \
        > "$W/build-$name.log" 2>&1 || { tail -30 "$W/build-$name.log"; exit 1; }
    idf idf.py -B "build-$name" -DBOARD="$board" "$@" size > "$W/size-$name.txt" 2>&1
    grep -E "warning:" "$W/build-$name.log" | grep -v "/managed_components/\|/esp-idf/" || true
    grep -E "Total image size|DIRAM  " "$W/size-$name.txt" || true
}
build korvo1 korvo1
build korvo1-standalone korvo1 -DPARTITION_CSV=partitions.csv
build dosboard dosboard

echo "== images"
I=$W/images
fits() {        # file, bytes
    local n
    n=$(stat -c %s "$1")
    [ "$n" -le "$2" ] || { echo "$1 is $n bytes, more than its partition's $2"; exit 1; }
    echo "  $(basename "$(dirname "$1")")/$(basename "$1"): $n of $2 bytes"
}
# tiny386's own bootloader and table, from a build's flash_args
own_set() {     # build name, folder
    local b=build-$1 d=$I/$2
    mkdir -p "$d"
    cp "$b/bootloader/bootloader.bin" "$b/partition_table/partition-table.bin" \
       "$b/tiny386.bin" "$b/tiny386.elf" "$d/"
    sed 's#bootloader/##; s#partition_table/##' "$b/flash_args" > "$d/flash_args"
    fits "$d/tiny386.bin" $FACTORY_PART
}
mkdir -p "$I/korvo1-dualboot"
cmp build-korvo1/partition_table/partition-table.bin "$DB/partition-table.bin" ||
    { echo "build-korvo1's partition table is not dualboot/partition-table.bin"; exit 1; }
cp "$DB/bootloader.bin" "$DB/partition-table.bin" "$I/korvo1-dualboot/"
cp build-korvo1/tiny386.bin build-korvo1/tiny386.elf "$I/korvo1-dualboot/"
# not the build's own flash_args: those put tiny386.bin at the factory
# partition, 0x20000, which is the Linux loader's.  "keep": esptool leaves
# the image headers as they are, so the bootloader's hash stays right.
printf '%s\n' "-fm keep -ff keep -fs keep" \
    "0x2000 bootloader.bin" "0x8000 partition-table.bin" "0xe20000 tiny386.bin" \
    > "$I/korvo1-dualboot/flash_args"
fits "$I/korvo1-dualboot/tiny386.bin" $TEST_PART
own_set korvo1-standalone korvo1-standalone
own_set dosboard dosboard
for d in korvo1-dualboot korvo1-standalone dosboard; do
    (cd "$I/$d" && sha256sum flash_args bootloader.bin partition-table.bin \
        tiny386.bin tiny386.elf > SHA256SUMS)
    for f in bootloader.bin tiny386.bin; do
        idf python -m esptool --chip esp32s31 image-info "$I/$d/$f" > "$W/image-info-$d-$f.txt" 2>&1
        grep -q "(invalid)" "$W/image-info-$d-$f.txt" &&
            { echo "esptool image-info: $d/$f is not valid"; exit 1; }
        grep -q "Validation hash: .* (valid)" "$W/image-info-$d-$f.txt" ||
            { echo "esptool image-info: $d/$f has no valid hash"; exit 1; }
    done
done
# tiny386 the default at reset (dualboot/DUALBOOT.md 13): korvo1-dualboot's
# bootloader, tiny386.bin and offsets, the other table.  The scripts write
# korvo1-dualboot's tiny386.bin for either table; this copy keeps the set
# whole for writing by hand.
D386=$I/korvo1-dualboot-tiny386-default
mkdir -p "$D386"
cp "$I/korvo1-dualboot/bootloader.bin" "$I/korvo1-dualboot/tiny386.bin" \
   "$I/korvo1-dualboot/flash_args" "$D386/"
cp "$DB/partition-table-tiny386-default.bin" "$D386/partition-table.bin"
(cd "$D386" && sha256sum flash_args bootloader.bin partition-table.bin tiny386.bin > SHA256SUMS)

# The status LED's panic bit-bang as compiled.  In status_led_panic() and
# status_led_halt() each wait on the cycle counter is three instructions
# (rdcycle, sub, bltu back to the rdcycle); the store that raises GPIO37
# (GPIO out1_w1ts, GPIO+0x14) and the one that lowers it (out1_w1tc,
# GPIO+0x18) must each come straight after its wait's bltu, as
# tests/host/test_status.c charges them, and the third wait (the last
# bit's low) is followed by anything.  A compiler that put work between a
# wait and the rising store would shorten every high time (the v3.3
# review: under the WS2812B's 580 ns at 80 MHz).
bb_layout() {   # ELF, function
    idf riscv32-esp-elf-objdump -d --no-show-raw-insn --disassemble="$2" "$1" |
    awk -v fn="$2" '
        /^ *[0-9a-f]+:\t/ {
            sub(/^ */, "")
            k = split($0, p, "\t")
            n++
            addr[n] = substr(p[1], 1, length(p[1]) - 1)
            op[n] = p[2]
            arg[n] = k >= 3 ? p[3] : ""
        }
        END {
            rise = fall = other = 0
            for (i = 1; i + 3 <= n; i++) {
                if (op[i] != "rdcycle" || op[i + 1] != "sub" || op[i + 2] != "bltu")
                    continue
                split(arg[i + 1], s, ",")
                split(arg[i + 2], b, ",")
                t = b[3]
                sub(/ .*/, "", t)
                if (s[2] != arg[i] || b[1] != s[1] || t != addr[i])
                    continue
                if (op[i + 3] == "sw" && arg[i + 3] ~ /,20\(/)
                    rise++
                else if (op[i + 3] == "sw" && arg[i + 3] ~ /,24\(/)
                    fall++
                else
                    other++
                printf "  %s: a wait at %s, then %s %s\n", fn, addr[i], op[i + 3], arg[i + 3]
            }
            ok = rise == 1 && fall == 1 && other == 1
            printf "  %s: %d wait(s) straight before the rising store, %d before the falling one, %d before anything else: %s\n",
                fn, rise, fall, other, ok ? "as status_bb.h has it" : "NOT as status_bb.h has it"
            exit !ok
        }'
}
for d in korvo1-dualboot korvo1-standalone dosboard; do
    for fn in status_led_panic status_led_halt; do
        bb_layout "$I/$d/tiny386.elf" $fn >> "$W/bb-layout-$d.txt" ||
            { cat "$W/bb-layout-$d.txt"; echo "$d/tiny386.elf: the LED's bit-bang is not laid out as status_bb.h wants"; exit 1; }
    done
    echo "  $d/tiny386.elf: the LED's bit-bang stores each straight after its wait"
done

echo "== built, and not flashed"
for d in korvo1-dualboot korvo1-dualboot-tiny386-default korvo1-standalone dosboard; do
    echo "== images/$d (esptool write-flash offsets: flash_args)"
    sed 's/^/   /' "$I/$d/flash_args"
    sed 's/^/   /' "$I/$d/SHA256SUMS"
done
