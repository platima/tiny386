#!/bin/bash
# Host tests for the ESP-IDF port: plain gcc, no board, no ESP-IDF.
#
#   bash tests/host/run.sh [OUT]
#
# OUT is where the programs and pictures go (tests/host/build by default).
# Exit status 0 when every test passes.

set -u
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../.." && pwd)
OUT=${1:-$HERE/build}
CC=${CC:-gcc}
CFLAGS="-O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare"
mkdir -p "$OUT" || exit 2

pass=0
fail=0
result() {
    if [ "$1" = 0 ]; then pass=$((pass + 1)); echo "PASS $2"; else fail=$((fail + 1)); echo "FAIL $2"; fi
}
# an earlier release's tag: its name in the firmware's scratch repository
# (v4.1), or on Plat's fork, where the same commits carry s31-v4.1 (v4.4);
# or, given as a second argument, the commit itself, which any clone of the
# branch has (v4.5: pristine and v4.0-base have no s31- tag on the fork).
# Empty when the tree has none of them, and then it says so (v4.5: the
# comparisons were skipped without a word before).
tagref() {
    local t
    for t in "$1" "s31-$1" ${2:-}; do
        if git -C "$TREE" rev-parse -q --verify "$t^{commit}" >/dev/null 2>&1; then
            echo "$t"
            return
        fi
    done
    echo "(no $1 here, as a tag or a commit: the comparison with it is skipped)" >&2
}
# upstream's release 9ed786b, which the work started from, and v4.0's merge
# base (v3.9 with upstream's master of 2026-10-22)
T_PRISTINE=$(tagref pristine 9ed786b4dd9698306dccdb7595858dee10949994)
T_V40BASE=$(tagref v4.0-base 6c220847f9053d607d296f32ba8b93f3cbd9e5f4)

# ==== 1. the PS/2 queue's lock (FIRMWARE-PLAN 3.3.1) ====
# i8042.c as it is now must pass; i8042.c as tiny386 released it (the
# pristine tag) must fail, which shows that the test sees the bug.
$CC $CFLAGS -Wno-unused-function -DBUILD_ESP32 -I"$HERE/shim" -I"$TREE" \
    -o "$OUT/test_ps2_queue" "$HERE/test_ps2_queue.c" "$TREE/i8042.c" -lpthread || exit 1
"$OUT/test_ps2_queue"
result $? "ps2 queue lock, i8042.c of this tree"

if [ -n "$T_PRISTINE" ]; then
    git -C "$TREE" show "$T_PRISTINE":i8042.c > "$OUT/i8042-pristine.c"
    $CC $CFLAGS -w -DBUILD_ESP32 -I"$HERE/shim" -I"$TREE" \
        -o "$OUT/test_ps2_queue_pristine" "$HERE/test_ps2_queue.c" "$OUT/i8042-pristine.c" -lpthread || exit 1
    echo "(the same test against i8042.c at the pristine tag, where it has to fail)"
    "$OUT/test_ps2_queue_pristine" > "$OUT/pristine.log" 2>&1
    st=$?
    grep -v "take failed\|give failed" "$OUT/pristine.log" | sed 's/^/    | /'
    echo "    | ($(grep -c 'take failed' "$OUT/pristine.log") takes and $(grep -c 'give failed' "$OUT/pristine.log") gives failed; exit status $st)"
    [ $st != 0 ]
    result $? "ps2 queue lock test catches the bug in pristine i8042.c"
fi

# ==== 2. the boot console and the crash screen (FIRMWARE-PLAN 3.2) ====
# console.c as the firmware has it, built with CONSOLE_HOST: no ESP-IDF.
# Pictures of each stage go to OUT as PPM (and PNG if ImageMagick is there).
# Since v3.8 also the UART quiet, the limit on repeated lines and the
# UART's queue.
echo
$CC $CFLAGS -DCONSOLE_HOST -I"$TREE/esp/main" \
    -o "$OUT/test_console" "$HERE/test_console.c" "$TREE/esp/main/console.c" \
    "$TREE/vgafont.c" || exit 1
"$OUT/test_console" "$OUT"
result $? "boot console: text, colours, scrolling, ring, hand-over, crash screen, repeated lines, UART queue"
if command -v convert >/dev/null; then
    for p in "$OUT"/console-*.ppm; do
        convert "$p" "${p%.ppm}.png"
    done
fi

# ==== 3. the display side of vga.c (FIRMWARE-PLAN 3.1) ====
# vga.c with the Korvo-1's board header, a fake clock and stand-ins for the
# few ESP-IDF headers it includes.  The emulator's own warnings are not
# this test's business (-w for vga.c), the test's are.
echo
$CC $CFLAGS -c -I"$HERE/shim" -I"$TREE" -include "$TREE/esp/main/board_korvo1.h" \
    -o "$OUT/test_vga.o" "$HERE/test_vga.c" || exit 1
$CC -O1 -g -w -c -I"$HERE/shim" -I"$TREE" -include "$TREE/esp/main/board_korvo1.h" \
    -o "$OUT/vga.o" "$TREE/vga.c" || exit 1
$CC -O1 -g -w -c -o "$OUT/vgafont.o" "$TREE/vgafont.c" || exit 1
$CC -o "$OUT/test_vga" "$OUT/test_vga.o" "$OUT/vga.o" "$OUT/vgafont.o" || exit 1
"$OUT/test_vga" "$OUT"
result $? "vga.c: retrace against the clock, rows redrawn, clears from the other core, placement"
if command -v convert >/dev/null; then
    for p in "$OUT"/vga-*.ppm; do
        convert "$p" "${p%.ppm}.png"
    done
fi

# ==== 3b. vga.c on the DOS board's 800x600 (review round 2) ====
# The same vga.c built with board_dosboard.h: where text, the 16- and
# 256-colour modes and VBE pictures land on 800x600.
echo
$CC $CFLAGS -c -I"$HERE/shim" -I"$TREE" -include "$TREE/esp/main/board_dosboard.h" \
    -o "$OUT/test_vga_dosboard.o" "$HERE/test_vga_dosboard.c" || exit 1
$CC -O1 -g -w -c -I"$HERE/shim" -I"$TREE" -include "$TREE/esp/main/board_dosboard.h" \
    -o "$OUT/vga-dosboard.o" "$TREE/vga.c" || exit 1
$CC -o "$OUT/test_vga_dosboard" "$OUT/test_vga_dosboard.o" "$OUT/vga-dosboard.o" \
    "$OUT/vgafont.o" || exit 1
"$OUT/test_vga_dosboard"
result $? "vga.c on the DOS board: text, 16 and 256 colours, VBE whole, scaled and 8-bit, at 800x600"

# ==== 3c. VBE pictures from the blocks written (v4.0, VGA_DIRTY_BLOCKS) ====
# vga.c three times: as the board builds it, the same with
# VGA_DIRTY_BLOCKS taken out (every pixel of every picture, the old loop),
# and v4.0-base's vga.c when the tag is there; the second and third have
# their symbols renamed.  The same modes, writes and changes go to each, and
# the pictures must be the same; on both boards' screens.
echo
vga_rename() {
    local p=$1 s
    for s in vga_init vga_set_force_8dm vga_step vga_refresh vga_ioport_write \
             vga_ioport_read vbe_write vbe_read vga_mem_write vga_mem_read \
             vga_mem_write16 vga_mem_write32 vga_mem_write_string vga_pci_init \
             vga_planar_fast vga_set_text_ops vga_dirty_map vga_dirty vga_dirty_blocks \
             vga_set_dirty_blocks vga_get_info vga_wrote vga_pace_quiet vga_pace_after_step vga_set_fb; do
        printf ' -D%s=%s%s' "$s" "$p" "$s"
    done
}
printf '#undef VGA_DIRTY_BLOCKS\n' > "$OUT/no_dirty_blocks.h"
VBE_BASE=
if [ -n "$T_V40BASE" ]; then
    mkdir -p "$OUT/v40base"
    git -C "$TREE" show "$T_V40BASE":vga.c > "$OUT/v40base/vga.c"
    git -C "$TREE" show "$T_V40BASE":vga.h > "$OUT/v40base/vga.h"
    VBE_BASE=1
fi
for board in korvo1 dosboard; do
    hdr="$TREE/esp/main/board_$board.h"
    $CC $CFLAGS -c -I"$HERE/shim" -I"$TREE" -include "$hdr" ${VBE_BASE:+-DWITH_BASE} \
        -o "$OUT/test_vbe-$board.o" "$HERE/test_vbe.c" || exit 1
    $CC -O2 -g -w -c -I"$HERE/shim" -I"$TREE" -include "$hdr" \
        -o "$OUT/vbe-new-$board.o" "$TREE/vga.c" || exit 1
    $CC -O2 -g -w -c -I"$HERE/shim" -I"$TREE" -include "$hdr" -include "$OUT/no_dirty_blocks.h" \
        $(vga_rename ref_) -o "$OUT/vbe-ref-$board.o" "$TREE/vga.c" || exit 1
    objs="$OUT/test_vbe-$board.o $OUT/vbe-new-$board.o $OUT/vbe-ref-$board.o $OUT/vgafont.o"
    if [ -n "$VBE_BASE" ]; then
        $CC -O2 -g -w -c -I"$OUT/v40base" -I"$HERE/shim" -I"$TREE" -include "$hdr" \
            $(vga_rename base_) -o "$OUT/vbe-base-$board.o" "$OUT/v40base/vga.c" || exit 1
        objs="$objs $OUT/vbe-base-$board.o"
    fi
    $CC -o "$OUT/test_vbe-$board" $objs -lpthread || exit 1
    "$OUT/test_vbe-$board" "$OUT"
    st=$?
    against="the old loop"
    [ -n "$VBE_BASE" ] && against="the old loop and v4.0-base"
    result $st "vga.c, $board: VBE pictures from the blocks written are as $against draws them, pixel for pixel; the bank window"
done

# ==== 3d. clears made by the picture after them (v4.2, VGA_CLEAR_AROUND) ====
# vga.c as the board builds it against the same vga.c with v4.1's memset()
# clears (VGA_NO_CLEAR_AROUND, symbols renamed): the same pictures, and no
# page of the screen stored into without a pixel in it changing, none at all
# where the guest's picture is as it was (a VBE mode set again over itself).
# v4.1's vga.c under the same test, when the tag is there, has to give
# every picture right and fail the stores (exit status 2).
echo
CLEAR_V41=
V41=$(tagref v4.1)
if [ -n "$V41" ]; then
    mkdir -p "$OUT/v41"
    git -C "$TREE" show "$V41:vga.c" > "$OUT/v41/vga.c"
    git -C "$TREE" show "$V41:vga.h" > "$OUT/v41/vga.h"
    CLEAR_V41=1
fi
for board in korvo1 dosboard; do
    hdr="$TREE/esp/main/board_$board.h"
    $CC $CFLAGS -c -I"$HERE/shim" -I"$TREE" -include "$hdr" \
        -o "$OUT/test_clear-$board.o" "$HERE/test_clear.c" || exit 1
    $CC -O2 -g -w -c -I"$HERE/shim" -I"$TREE" -include "$hdr" -DVGA_NO_CLEAR_AROUND \
        $(vga_rename ref_) -o "$OUT/clear-ref-$board.o" "$TREE/vga.c" || exit 1
    $CC -o "$OUT/test_clear-$board" "$OUT/test_clear-$board.o" "$OUT/vbe-new-$board.o" \
        "$OUT/clear-ref-$board.o" "$OUT/vgafont.o" || exit 1
    "$OUT/test_clear-$board" > "$OUT/clear-$board.log" 2>&1
    st=$?
    grep "FAIL\|pictures,\|test_clear" "$OUT/clear-$board.log" | head -12
    result $st "vga.c, $board: a clear is made by the picture after it, the same pictures as a memset(), no page stored into for nothing"
    if [ -n "$CLEAR_V41" ]; then
        # v4.1's blocks were 1 KiB (v4.4's 256 bytes): the test and the
        # reference that mark its map are built with that size too
        $CC -O2 -g -w -c -I"$OUT/v41" -I"$HERE/shim" -I"$TREE" -include "$hdr" \
            -o "$OUT/clear-v41-$board.o" "$OUT/v41/vga.c" || exit 1
        $CC $CFLAGS -c -DVGA_DIRTY_SHIFT=10 -I"$HERE/shim" -I"$TREE" -include "$hdr" \
            -o "$OUT/test_clear-s10-$board.o" "$HERE/test_clear.c" || exit 1
        $CC -O2 -g -w -c -DVGA_DIRTY_SHIFT=10 -I"$HERE/shim" -I"$TREE" -include "$hdr" \
            -DVGA_NO_CLEAR_AROUND $(vga_rename ref_) -o "$OUT/clear-ref-s10-$board.o" "$TREE/vga.c" || exit 1
        $CC -o "$OUT/test_clear_v41-$board" "$OUT/test_clear-s10-$board.o" "$OUT/clear-v41-$board.o" \
            "$OUT/clear-ref-s10-$board.o" "$OUT/vgafont.o" || exit 1
        echo "(the same test against vga.c at the v4.1 tag, which has to draw every picture right and fail the stores)"
        "$OUT/test_clear_v41-$board" > "$OUT/clear-v41-$board.log" 2>&1
        st=$?
        grep "pictures,\|test_clear" "$OUT/clear-v41-$board.log" | sed 's/^/    | /'
        echo "    | ($(grep -c 'FAIL' "$OUT/clear-v41-$board.log") checks failed; exit status $st)"
        [ $st = 2 ]
        result $? "vga.c, $board: the clear test catches v4.1's memset() (pictures right, pages stored into for nothing)"
    fi
done

# ==== 3e. the text screen when its font changes (v4.4, BUILD-NOTES 26.3) ====
# vga.c as each board builds it (test 3c's objects): every text picture
# after a font is loaded into plane 2, with and without a picture during the
# loading, and after another character map is chosen, the same as a whole
# redraw; a picture with nothing changed still draws only what changed.
# v4.3's vga.c under the same test, when the tag is there, has to fail.
echo
FONT_V43=
V43=$(tagref v4.3)
if [ -n "$V43" ]; then
    mkdir -p "$OUT/v43"
    git -C "$TREE" show "$V43:vga.c" > "$OUT/v43/vga.c"
    git -C "$TREE" show "$V43:vga.h" > "$OUT/v43/vga.h"
    FONT_V43=1
fi
for board in korvo1 dosboard; do
    hdr="$TREE/esp/main/board_$board.h"
    $CC $CFLAGS -c -I"$HERE/shim" -I"$TREE" -include "$hdr" \
        -o "$OUT/test_font-$board.o" "$HERE/test_font.c" || exit 1
    $CC -o "$OUT/test_font-$board" "$OUT/test_font-$board.o" "$OUT/vbe-new-$board.o" \
        "$OUT/vgafont.o" || exit 1
    "$OUT/test_font-$board" > "$OUT/font-$board.log" 2>&1
    st=$?
    grep "FAIL\|test_font" "$OUT/font-$board.log" | head -12
    result $st "vga.c, $board: a text picture after a new font or character map is a whole redraw's, and no more is drawn when nothing changed"
    if [ -n "$FONT_V43" ]; then
        $CC -O2 -g -w -c -I"$OUT/v43" -I"$HERE/shim" -I"$TREE" -include "$hdr" \
            -o "$OUT/font-v43-$board.o" "$OUT/v43/vga.c" || exit 1
        $CC -o "$OUT/test_font_v43-$board" "$OUT/test_font-$board.o" "$OUT/font-v43-$board.o" \
            "$OUT/vgafont.o" || exit 1
        echo "(the same test against vga.c at the v4.3 tag, which has to fail)"
        "$OUT/test_font_v43-$board" > "$OUT/font-v43-$board.log" 2>&1
        st=$?
        grep "FAIL" "$OUT/font-v43-$board.log" | sed 's/^/    | /'
        echo "    | (exit status $st)"
        [ $st = 1 ]
        result $? "vga.c, $board: the font test catches v4.3's text drawing (old glyphs left after a new font)"
    fi
done

# ==== 3f. pictures at the guest's quiet moments (v4.4, vga.h) ====
# vga.c as each board builds it (test 3c's objects): vga_pace_after_step()
# and the stores through the VGA window that set vga_wrote, in every mode.
echo
for board in korvo1 dosboard; do
    hdr="$TREE/esp/main/board_$board.h"
    $CC $CFLAGS -c -I"$HERE/shim" -I"$TREE" -include "$hdr" \
        -o "$OUT/test_pace-$board.o" "$HERE/test_pace.c" || exit 1
    $CC -o "$OUT/test_pace-$board" "$OUT/test_pace-$board.o" "$OUT/vbe-new-$board.o" \
        "$OUT/vgafont.o" || exit 1
    "$OUT/test_pace-$board" > "$OUT/pace-$board.log" 2>&1
    st=$?
    grep "FAIL\|test_pace" "$OUT/pace-$board.log" | head -12
    result $st "vga.c, $board: quiet moments counted from the last store, every store through the window noted"
done

# ==== 3g. double buffering made at power-on, and the drawing loop (v4.6) ====
# dbuf_boot.c, lcd_korvo1.c and display.c's vga_task as the Korvo-1 builds
# them, vga.c as test 3c built it, and a pretend RGB panel driver whose
# starts fail, or reset the chip, when asked (tests/host/display): each case
# a run of starts in child processes, the LP SRAM's note carried between
# them; never more than one restart (and one more, after the review of
# v4.6, when the panel started with neither two frame buffers nor one), none
# after a crash, a watchdog or a power dip, the note cleared once taken, a
# note not whole ignored, the panel with two frame buffers after the restart
# or with one and the yellow line, no restart under the Linux-default table;
# after every picture the VGA code draws into a frame buffer that is there.
# Then display.c at v4.5, where a start with no panel has to end in a load
# at 0xFA50, the address of the board's fault with double_buffer = 1 (the
# same arithmetic: any NULL frame buffer under 80x25 text faults there, so
# this shows the path can give it, not that the board took it).  Last, both
# with Ctrl+Alt+F9 and VBE at 16 and 8 bits during the run (--trace): with a
# panel the fix must hand over the same buffers and show the same pictures
# as v4.5, and with none draw every picture into the PC's own buffer.
echo
DSP="$HERE/display"
DSPFLAGS="-I$DSP/stubs -I$TREE/esp/main -I$TREE -include $TREE/esp/main/board_korvo1.h"
$CC $CFLAGS $DSPFLAGS -c -o "$OUT/dstart-test.o" "$DSP/test_double_start.c" || exit 1
$CC $CFLAGS $DSPFLAGS -c -o "$OUT/dstart-lcd.o" "$TREE/esp/main/lcd_korvo1.c" || exit 1
$CC $CFLAGS $DSPFLAGS -c -o "$OUT/dstart-dbuf.o" "$TREE/esp/main/dbuf_boot.c" || exit 1
$CC $CFLAGS $DSPFLAGS -Dvga_set_fb=host_vga_set_fb -c -o "$OUT/dstart-display.o" \
    "$TREE/esp/main/display.c" || exit 1
$CC -o "$OUT/test_double_start" "$OUT/dstart-test.o" "$OUT/dstart-lcd.o" "$OUT/dstart-dbuf.o" \
    "$OUT/dstart-display.o" "$OUT/vbe-new-korvo1.o" "$OUT/vgafont.o" || exit 1
"$OUT/test_double_start" > "$OUT/double-start.log" 2>&1
st=$?
grep "FAIL\|test_double_start" "$OUT/double-start.log" | head -12
result $st "dbuf_boot.c, lcd_korvo1.c and display.c: two frame buffers at power-on after one restart at most (two when the panel starts with neither, none after a crash), the note, the fallback, and a frame buffer there for every picture"
T_V4_5=$(tagref v4.5 99074b7347bec45d743a56254df218fe616c7d29)
if [ -n "$T_V4_5" ]; then
    git -C "$TREE" show "$T_V4_5":esp/main/display.c > "$OUT/display-v4.5.c"
    $CC $CFLAGS -w $DSPFLAGS -Dvga_set_fb=host_vga_set_fb -c -o "$OUT/dstart-display-v4.5.o" \
        "$OUT/display-v4.5.c" || exit 1
    $CC -o "$OUT/test_double_start_v4.5" "$OUT/dstart-test.o" "$OUT/dstart-lcd.o" \
        "$OUT/dstart-dbuf.o" "$OUT/dstart-display-v4.5.o" "$OUT/vbe-new-korvo1.o" \
        "$OUT/vgafont.o" || exit 1
    echo "(the same test against display.c at the v4.5 tag, where a start with no panel has to fault at 0xfa50, the board's address)"
    "$OUT/test_double_start_v4.5" > "$OUT/double-start-v4.5.log" 2>&1
    st=$?
    grep "faulted\|test_double_start" "$OUT/double-start-v4.5.log" | sed 's/^/    | /' | head -6
    [ $st != 0 ] && grep -q "faulted (SIGSEGV) at address 0xfa50" "$OUT/double-start-v4.5.log"
    result $? "the display test sees v4.5's fault: no panel frame buffer, then a load at 0xfa50"
    "$OUT/test_double_start" --trace > "$OUT/double-start-trace.log" 2>&1
    st=$?
    "$OUT/test_double_start_v4.5" --trace > "$OUT/double-start-trace-v4.5.log" 2>&1
    # the "[panel]" cases, without the last line (all passed, or how many
    # failed: v4.5 fails the cases with no panel)
    awk '/^\[/ { keep = /^\[panel\]/ } /^test_double_start:/ { keep = 0 } keep' \
        "$OUT/double-start-trace.log" > "$OUT/double-start-trace-panel.log"
    awk '/^\[/ { keep = /^\[panel\]/ } /^test_double_start:/ { keep = 0 } keep' \
        "$OUT/double-start-trace-v4.5.log" > "$OUT/double-start-trace-panel-v4.5.log"
    grep "FAIL\|test_double_start" "$OUT/double-start-trace.log" | head -6
    echo "    | $(grep -c '^    picture' "$OUT/double-start-trace-panel.log") pictures with a panel" \
        "and $(grep -c '^    Ctrl+Alt+F9' "$OUT/double-start-trace-panel.log") presses of" \
        "Ctrl+Alt+F9, line for line against v4.5's"
    [ $st = 0 ] && [ -s "$OUT/double-start-trace-panel.log" ] &&
        ! grep -q "FAIL" "$OUT/double-start-trace-panel-v4.5.log" &&
        cmp -s "$OUT/double-start-trace-panel.log" "$OUT/double-start-trace-panel-v4.5.log"
    result $? "the display test with Ctrl+Alt+F9 and VBE at 16 and 8 bits: with a panel the same buffers and pictures as v4.5, with none the PC's own"
fi

# ==== 4. the keyboard and mouse layer (FIRMWARE-PLAN 3.3) ====
# kbd_input.c with i8042.c as the emulator has it, without BUILD_ESP32 (no
# locks) and with PS2_KBD_HOOKS; usb_hid.c for v2's key table, to compare.
echo
$CC -O1 -g -w -DPS2_KBD_HOOKS -I"$TREE" -c -o "$OUT/i8042-hooks.o" "$TREE/i8042.c" || exit 1
$CC $CFLAGS -DKBD_INPUT_HOST -DPS2_KBD_HOOKS -I"$TREE" -I"$TREE/esp/main" \
    -o "$OUT/test_kbd_input" "$HERE/test_kbd_input.c" "$TREE/esp/main/kbd_input.c" \
    "$TREE/esp/main/usb_hid.c" "$OUT/i8042-hooks.o" || exit 1
"$OUT/test_kbd_input"
result $? "kbd_input.c: scancodes, sequences, typematic, keyboards, lights, hotkey, mouse"

# ==== 4b. the PS/2 mouse: whole packets, the sample rate, two threads ====
# The same i8042.c object through kbd_input.c with a fake clock; then
# i8042.c built as on the board (BUILD_ESP32, the stand-in mutex) with a
# reporting thread and an emulator thread.  If the tree has the v3 tag (the
# review's starting point), the first part is also run against v3's
# i8042.c, where the backlog has to put the packets out of step.
echo
$CC $CFLAGS -DKBD_INPUT_HOST -DPS2_KBD_HOOKS -I"$TREE" -I"$TREE/esp/main" \
    -o "$OUT/test_ps2_mouse" "$HERE/test_ps2_mouse.c" "$TREE/esp/main/kbd_input.c" \
    "$OUT/i8042-hooks.o" || exit 1
"$OUT/test_ps2_mouse"
result $? "PS/2 mouse: backlog, clicks, sample rate, remote mode, reset, mouse off"
$CC $CFLAGS -Wno-unused-function -DBUILD_ESP32 -DPS2_KBD_HOOKS -DTWO_THREADS \
    -I"$HERE/shim" -I"$TREE" -o "$OUT/test_ps2_mouse_threads" \
    "$HERE/test_ps2_mouse.c" "$TREE/i8042.c" -lpthread || exit 1
"$OUT/test_ps2_mouse_threads"
result $? "PS/2 mouse: a reporting thread and the emulator's thread"
# the emulator's thread stopped for 120 ms at a time: the ring's overflow
# used while the other thread reports (review round 2)
$CC $CFLAGS -Wno-unused-function -DBUILD_ESP32 -DPS2_KBD_HOOKS -DTWO_THREADS -DSTALLS \
    -I"$HERE/shim" -I"$TREE" -o "$OUT/test_ps2_mouse_stalls" \
    "$HERE/test_ps2_mouse.c" "$TREE/i8042.c" -lpthread || exit 1
"$OUT/test_ps2_mouse_stalls"
result $? "PS/2 mouse: two threads, the emulator held up: every click through the ring's overflow"
T_V3=$(tagref v3)
if [ -n "$T_V3" ]; then
    git -C "$TREE" show "$T_V3":i8042.c > "$OUT/i8042-v3.c"
    $CC -O1 -g -w -DPS2_KBD_HOOKS -I"$TREE" -c -o "$OUT/i8042-v3-hooks.o" "$OUT/i8042-v3.c" || exit 1
    $CC $CFLAGS -DKBD_INPUT_HOST -DPS2_KBD_HOOKS -I"$TREE" -I"$TREE/esp/main" \
        -o "$OUT/test_ps2_mouse_v3" "$HERE/test_ps2_mouse.c" "$TREE/esp/main/kbd_input.c" \
        "$OUT/i8042-v3-hooks.o" || exit 1
    echo "(the same test against i8042.c at the v3 tag, where it has to fail)"
    "$OUT/test_ps2_mouse_v3" > "$OUT/mouse-v3.log" 2>&1
    st=$?
    grep "FAIL\|last packet\|bytes read" "$OUT/mouse-v3.log" | head -8 | sed 's/^/    | /'
    [ $st != 0 ]
    result $? "PS/2 mouse test catches the backlog fault in v3's i8042.c"
fi

# ==== 5. HID report descriptors (FIRMWARE-PLAN 3.3) ====
echo
$CC $CFLAGS -I"$TREE/esp/main" -o "$OUT/test_hid_parse" "$HERE/test_hid_parse.c" \
    "$TREE/esp/main/hid_parse.c" || exit 1
"$OUT/test_hid_parse"
result $? "hid_parse.c: boot keyboard, wheel mouse, receiver with report IDs, NKRO, vendor, tablet"

# ==== 5b. usb_input.c against the HID driver's two-step close ====
# usb_input.c itself, with stand-ins for FreeRTOS and usb_host_hid
# (tests/host/usb/stubs) and the real hid_parse.c.
echo
$CC $CFLAGS -Wno-unused-function -DUSB_INPUT_C="\"$TREE/esp/main/usb_input.c\"" \
    -I"$HERE/usb/stubs" -I"$TREE/esp/main" -I"$TREE" -o "$OUT/test_usb_input" \
    "$HERE/usb/test_usb_input.c" "$TREE/esp/main/hid_parse.c" || exit 1
"$OUT/test_usb_input"
result $? "usb_input.c: interfaces given up, unplugged, queued behind others; lights after an unplug; unplugged while being set up"
# the round 2 cases (unplugged while being set up) against usb_input.c at
# the v3.1 tag, where they have to fail
T_V3_1=$(tagref v3.1)
if [ -n "$T_V3_1" ]; then
    git -C "$TREE" show "$T_V3_1":esp/main/usb_input.c > "$OUT/usb_input-v3.1.c"
    $CC $CFLAGS -w -DUSB_INPUT_C="\"$OUT/usb_input-v3.1.c\"" \
        -I"$HERE/usb/stubs" -I"$TREE/esp/main" -I"$TREE" -o "$OUT/test_usb_input_v3.1" \
        "$HERE/usb/test_usb_input.c" "$TREE/esp/main/hid_parse.c" || exit 1
    echo "(the same test against usb_input.c at the v3.1 tag, where it has to fail)"
    "$OUT/test_usb_input_v3.1" > "$OUT/usb-v3.1.log" 2>&1
    st=$?
    grep "FAILS\|checks wrong" "$OUT/usb-v3.1.log" | sed 's/^/    | /'
    [ $st != 0 ]
    result $? "usb_input.c test catches requests to a freed device in v3.1's usb_input.c"
fi

# ==== 6. which ini, and the files it names (FIRMWARE-PLAN 3.4) ====
# setup.c with tiny386's own ini.c, on a pretend card under OUT/card, with
# the Linux route's ini files (tests/host/data/linux-route).
echo
$CC $CFLAGS -I"$TREE" -I"$TREE/esp/main" -o "$OUT/test_setup" "$HERE/test_setup.c" \
    "$TREE/esp/main/setup.c" "$TREE/esp/main/ini_edit.c" "$TREE/ini.c" || exit 1
"$OUT/test_setup" "$OUT" "$HERE/data/linux-route"
result $? "setup.c: ini search, the menu's list and default, lastboot.txt, paths of the Linux route's inis"

# ==== 6b. a setting saved in the ini (v4.4: Ctrl+Alt+F11 saves vbe_blocks) ====
# ini_edit.c on inis of every shape, each result read back with tiny386's
# own ini.c; setup_ini_save() and setup_ini_tidy() on files in OUT/inisave:
# no write when nothing changes, errors leave the ini as it was, and every
# state a cut in power can leave part way through a save is put right.
# v4.5: no rename (a whole copy beside the ini, then the ini rewritten in
# place), a cut at each step of a save (-DSETUP_TEST_CUT), v4.4's leftovers.
echo
$CC $CFLAGS -DSETUP_TEST_CUT -I"$TREE" -I"$TREE/esp/main" -o "$OUT/test_ini_edit" \
    "$HERE/test_ini_edit.c" "$TREE/esp/main/setup.c" "$TREE/esp/main/ini_edit.c" "$TREE/ini.c" || exit 1
"$OUT/test_ini_edit" "$OUT" "$TREE" > "$OUT/ini-edit.log" 2>&1
st=$?
grep "FAIL\|test_ini_edit:\|errors:\|refused:\|cut at step" "$OUT/ini-edit.log" | head -24
result $st "ini_edit.c, setup.c: vbe_blocks saved in the ini (present, absent, commented, CR LF and LF, and more), written safely with no rename, a cut at each step, leftovers put right"

# ==== 7. the boot menu's state machine ====
echo
$CC $CFLAGS -DBOOTMENU_HOST -I"$TREE/esp/main" -o "$OUT/test_bootmenu" \
    "$HERE/test_bootmenu.c" "$TREE/esp/main/bootmenu.c" || exit 1
"$OUT/test_bootmenu"
result $? "bootmenu.c: countdown, keys, wrap, digits"

# ==== 8. the speed readout and BENCH.COM's marks (FIRMWARE-PLAN 3.5) ====
echo
$CC $CFLAGS -DSPEED_HOST -I"$TREE/esp/main" -o "$OUT/test_speed" \
    "$HERE/test_speed.c" "$TREE/esp/main/speed.c" || exit 1
"$OUT/test_speed"
result $? "speed.c: instructions a second, BENCH marks, lines as the Linux route prints them"

# ==== 8b. the information port (perfport.c, perfport.h) ====
# The protocol as a guest program sees it, against stand-ins for the
# emulator's counters; vga_get_info() with vga.c as each board builds it
# (test 3c's objects); and perfport.h as a guest program's C89 compiler
# takes it, with nothing else included.
echo
NPPI=$(grep -c '^#define PP_I_' "$TREE/perfport.h")
$CC $CFLAGS -I"$TREE" -o "$OUT/test_perfport" "$HERE/test_perfport.c" -lpthread || exit 1
"$OUT/test_perfport" "$NPPI"
result $? "perfport: found, snapshots, in and out of every width, the front end's values, the drawing's figures, two threads"
for board in korvo1 dosboard; do
    $CC $CFLAGS -I"$HERE/shim" -I"$TREE" -include "$TREE/esp/main/board_$board.h" \
        -o "$OUT/test_vga_info-$board" "$HERE/test_vga_info.c" "$OUT/vbe-new-$board.o" \
        "$OUT/vgafont.o" || exit 1
    "$OUT/test_vga_info-$board"
    result $? "vga_get_info(), $board: text, 16 and 256 colours, VBE, blank; VBE pictures, blocks and rows counted"
done
# v4.3: the port and Ctrl+Alt+F11 together, perfport.c with vga.c as each
# board builds it (test 3c's objects): a snapshot gives vbe_blocks as it is
# at that moment, and the two whole pictures after each change are counted
for board in korvo1 dosboard; do
    $CC $CFLAGS -I"$HERE/shim" -I"$TREE" -include "$TREE/esp/main/board_$board.h" \
        -o "$OUT/test_port_blocks-$board" "$HERE/test_port_blocks.c" "$OUT/vbe-new-$board.o" \
        "$OUT/vgafont.o" || exit 1
    "$OUT/test_port_blocks-$board"
    result $? "perfport and Ctrl+Alt+F11, $board: vbe_blocks read as it is, the whole pictures after a change counted"
done
printf '#include "perfport.h"\nunsigned long magic = PP_MAGIC, version = PP_VERSION;\nunsigned port = PP_PORT_DATA;\nint count = PP_COUNT, text = PP_I_BOARD + PP_TEXT_INDEXES;\n' \
    > "$OUT/perfport_guest.c"
$CC -std=c89 -pedantic -Werror -Wall -Wextra -fsyntax-only -I"$TREE" "$OUT/perfport_guest.c"
result $? "perfport.h in C89 with nothing else, as a guest program includes it"

# ==== 9 to 12. the DOS board (FIRMWARE-PLAN 5): tests/host/dosboard ====
D="$HERE/dosboard"
DOSCFLAGS="-O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare"

# 9. the MS7210 driver against a fake I2C bus; its bus logs go to OUT
echo
$CC $DOSCFLAGS -I"$TREE/esp/main" -o "$OUT/ms7210_test" \
    "$D/ms7210_test.c" "$TREE/esp/main/ms7210.c" || exit 1
"$OUT/ms7210_test" "$OUT"
result $? "ms7210.c: sequence, wire format, PLL retry, hot plug, EDID, DVI, InfoFrames"

# 10. the HDMI scenario's bus log, row by row, against the table in
# encoder.md 8.4 (outside this tree: ENCODER_MD names it)
echo
ENCODER_MD=${ENCODER_MD:-/mnt/d/Repos/esp32-s31-linux-release/tiny386-hdmi-board/research/encoder.md}
if [ -f "$ENCODER_MD" ]; then
    python3 "$D/check_ms7210_table.py" "$ENCODER_MD" "$OUT/ms7210_hdmi.log" 0
    result $? "ms7210.c: the bus log against encoder.md 8.4, every row in order"
else
    echo "SKIP the MS7210 table check: no encoder.md at $ENCODER_MD (set ENCODER_MD)"
fi

# 11. the CH9350L's frames through ch9350.c, kbd_input.c and i8042.c, as
# test 4 builds the last two
echo
$CC $DOSCFLAGS -Wno-unused-function -Wno-implicit-fallthrough -Wno-missing-field-initializers \
    -DKBD_INPUT_HOST -DPS2_KBD_HOOKS -I"$TREE/esp/main" -I"$TREE" -o "$OUT/ch9350_test" \
    "$D/ch9350_test.c" "$TREE/esp/main/ch9350.c" "$TREE/esp/main/kbd_input.c" \
    "$OUT/i8042-hooks.o" || exit 1
"$OUT/ch9350_test"
result $? "ch9350.c: frames into kbd_input.c, as a DOS program reads them from ports 60h and 64h"

# 12. AUDIO_RATE: the PC speaker, the AdLib and the SB16 at 44,100 and
# 48,000 (SB16_PATCHED=path/to/sb16.c runs it against another sb16.c too)
for R in 44100 48000; do
    echo
    $CC -O2 -g -w -DUSE_FMOPL -DAUDIO_RATE=$R -I"$TREE" -o "$OUT/audio_rate_test_$R" \
        "$D/audio_rate_test.c" "$TREE/pcspk.c" "$TREE/adlib.c" "$TREE/fmopl.c" -lm || exit 1
    "$OUT/audio_rate_test_$R"
    result $? "AUDIO_RATE $R: PC speaker, AdLib and SB16 pitch"
    if [ -n "${SB16_PATCHED:-}" ]; then
        $CC -O2 -g -w -DUSE_FMOPL -DAUDIO_RATE=$R -DSB16_C="\"$SB16_PATCHED\"" -I"$TREE" \
            -o "$OUT/audio_rate_test_patched_$R" \
            "$D/audio_rate_test.c" "$TREE/pcspk.c" "$TREE/adlib.c" "$TREE/fmopl.c" -lm || exit 1
        "$OUT/audio_rate_test_patched_$R"
        result $? "AUDIO_RATE $R with $SB16_PATCHED"
    fi
done

# ==== 13. the card's partitions (FIRMWARE-PLAN 3.4): tests/host/storage ====
# storage.c's partition code with ESP-IDF's own FatFs and SD card glue, on
# card images laid out as Plat's card and others.  Needs dosfstools, mtools
# and ESP-IDF's source at IDF_PATH; without them it says SKIP.
echo
if bash "$HERE/storage/run_storage.sh" "$OUT/storage" > "$OUT/storage.log" 2>&1; then
    st=0
else
    st=1
fi
if grep -q "^SKIP" "$OUT/storage.log"; then
    grep "^SKIP" "$OUT/storage.log"
else
    grep -E "FAILED|^FAIL|^storage:" "$OUT/storage.log"
    result $st "storage.c: the card's partitions with ESP-IDF's FatFs, on images laid out as Plat's card"
fi

# ==== 14. the disk images and the floppies: nothing past their end (review round 2) ====
# ide.c's disk images through ESP-IDF's own FatFs (tests/host/disk), as
# test 13 builds it: a guest sector past the end is refused and the image
# does not grow on the card.  Then misc.c's floppies with the port's checks.
echo
if bash "$HERE/disk/run_disk.sh" "$OUT/disk" > "$OUT/disk.log" 2>&1; then
    st=0
else
    st=1
fi
if grep -q "^SKIP" "$OUT/disk.log"; then
    grep "^SKIP" "$OUT/disk.log"
else
    grep -E "FAILED|^disk:" "$OUT/disk.log"
    result $st "ide.c: disk images through ESP-IDF's FatFs, nothing read or written past their end"
fi

# ==== 14b. a card partition as the guest's whole disk (v4.4): tests/host/sdpart ====
# sd_part.c with ESP-IDF's own FatFs, storage.c's partition code and ide.c
# as the board builds them: the guest's boot code against its source, the
# partitions refused and taken, and on card images laid out as s31/newcard
# makes them and as Plat's card is, the guest's reads and writes (nothing
# written outside the partition, the volume clean after).
echo
if bash "$HERE/sdpart/run_sdpart.sh" "$OUT/sdpart" > "$OUT/sdpart.log" 2>&1; then
    st=0
else
    st=1
fi
if grep -q "^SKIP" "$OUT/sdpart.log"; then
    grep "^SKIP" "$OUT/sdpart.log"
else
    grep -E "FAILED|^sdpart:" "$OUT/sdpart.log"
    result $st "sd_part.c: a card partition as the guest's whole disk, nothing outside it written"
fi
echo
$CC -O1 -g -w -DEMULINK_CHECKED -I"$TREE" -c -o "$OUT/misc-checked.o" "$TREE/misc.c" || exit 1
$CC $CFLAGS -DEMULINK_CHECKED -I"$TREE" -o "$OUT/test_floppy" "$HERE/test_floppy.c" \
    "$OUT/misc-checked.o" || exit 1
"$OUT/test_floppy" "$OUT"
result $? "misc.c: floppy drives 0 and 1 only, nothing past the image's end, refused writes counted"

# ==== 14c. a command's blocks gathered and read ahead (v5.2): tests/host/ide ====
# ide.c with IDE_ENABLE_WRITE_GATHER (upstream's PR 70) and
# IDE_ENABLE_READ_AHEAD, as the boards build it, against ide.c without them:
# random command sequences driven side by side at the IDE ports, every value
# read, every interrupt and both disks compared (PR 70's failed writes
# against a model of them), and faults planted in copies of ide.c that the
# test has to see.  Then the card commands through disk_image.c and
# ESP-IDF's own FatFs, as test 13 builds them (SKIP without them).
echo
if bash "$HERE/ide/run_ide.sh" "$OUT/ide" > "$OUT/ide.log" 2>&1; then
    st=0
else
    st=1
fi
grep "^SKIP" "$OUT/ide.log"
grep -E "FAILED|^ide:" "$OUT/ide.log"
result $st "ide.c: a command's blocks gathered and read ahead, the guest sees the same; fewer card commands"

# ==== 14d. a card command that timed out reported as one (v5.3): tests/host/sdmmc ====
# esp/main/sdmmc_timeout_wrap.c as the S31 boards build it, around a
# stand-in for ESP-IDF's sdmmc_host_do_transaction() on a pretend clock.
# Its two parts are each built only for an ESP-IDF without the upstream
# fix they stand in for (esp/main/sdmmc_fixes.cmake), so the test runs in
# the four ways.  Both parts, as for 188e3e55bb, at the boards' tick of
# 100 Hz and at 1000 Hz: a command whose event never came, which that
# ESP-IDF gives back as done when its buffer is in PSRAM, comes back as a
# timeout; good commands, data errors and buffers in internal RAM as the
# driver gave them.  Then sdmmc_write_sectors_dma() as 188e3e55bb sends a
# multi-block write: a failed one must give its own error, not the result
# of the card's count of blocks written, for each task its own.  Neither
# part, as for the boards' ESP-IDF with e00861c3f497 and 007945125db7:
# every result as the driver gave it, with no clock read and no table.
# And each part alone.  In all four the slot's settings are forgotten
# after a failed command, and only then.  Last, neither given, as a build
# outside esp/main/CMakeLists.txt would make it: both parts in.
echo
# timeout_test NAME TIMEOUT CMD25 HZ: OUT/sdmmc_timeout-NAME.log; with
# TIMEOUT and CMD25 empty, neither -D given
timeout_test() {
    local parts=""
    [ -n "$2" ] && parts="$parts -DSD_WRAP_TIMEOUT=$2"
    [ -n "$3" ] && parts="$parts -DSD_WRAP_CMD25=$3"
    $CC $CFLAGS -DconfigTICK_RATE_HZ=$4 $parts -I"$HERE/sdmmc/stubs" \
        -o "$OUT/test_sdmmc_timeout-$1" "$HERE/sdmmc/test_sdmmc_timeout.c" \
        "$TREE/esp/main/sdmmc_timeout_wrap.c" || exit 1
    "$OUT/test_sdmmc_timeout-$1" > "$OUT/sdmmc_timeout-$1.log" 2>&1
    st=$?
    grep -E "FAIL|caught|passed through|test_sdmmc_timeout" "$OUT/sdmmc_timeout-$1.log" | sed 's/^/    | /'
}
for hz in 100 1000; do
    timeout_test $hz 1 1 $hz
    result $st "sdmmc_timeout_wrap.c with both parts (ESP-IDF 188e3e55bb) at $hz Hz: a card command whose event never came reported as a timeout, a failed multi-block write as failed; the rest as the driver gave them"
done
timeout_test fixed 0 0 100
result $st "sdmmc_timeout_wrap.c with neither part (ESP-IDF with both fixes): every result as the driver gave it, no clock read, no table"
timeout_test timeout-only 1 0 100
result $st "sdmmc_timeout_wrap.c with the timeout part alone (ESP-IDF with 007945125db7 only)"
timeout_test cmd25-only 0 1 100
result $st "sdmmc_timeout_wrap.c with the CMD25 part alone (ESP-IDF with e00861c3f497 only)"
timeout_test default "" "" 100
[ $st = 0 ] && grep -q "the timeout part in, the CMD25 part in$" "$OUT/sdmmc_timeout-default.log"
result $? "sdmmc_timeout_wrap.c with neither part given: both built in, as for 188e3e55bb"
# with neither part, the wrap's object is the pass-through alone: no data,
# and nothing called but the driver and sd_setup_forget()
$CC $CFLAGS -DSD_WRAP_TIMEOUT=0 -DSD_WRAP_CMD25=0 -I"$HERE/sdmmc/stubs" -c \
    -o "$OUT/sdmmc_timeout_wrap-fixed.o" "$TREE/esp/main/sdmmc_timeout_wrap.c" || exit 1
syms=$(nm -P "$OUT/sdmmc_timeout_wrap-fixed.o" | awk '{print $2, $1}' | LC_ALL=C sort | tr '\n' ' ')
echo "    | $syms"
[ "$syms" = "T __wrap_sdmmc_host_do_transaction U __real_sdmmc_host_do_transaction U sd_setup_forget " ]
result $? "sdmmc_timeout_wrap.c with neither part: the object holds only the pass-through, which calls only the driver and sd_setup_forget()"
# faults planted in copies of the wrap, which the test has to see: no time
# taken (188e3e55bb alone), a lost event taken only after all its ticks
# rather than all but one, and a failed write's error left to the count;
# and with neither part, no sd_setup_forget() after a failed command, and
# each part built in all the same; and with neither given, both left out
planted_wrap() {
    sed -e "$2" "$TREE/esp/main/sdmmc_timeout_wrap.c" > "$OUT/sdmmc_timeout_wrap-$1.c"
    $CC $CFLAGS -w ${4:-} -I"$HERE/sdmmc/stubs" -I"$TREE/esp/main" -o "$OUT/test_sdmmc_timeout-$1" \
        "$HERE/sdmmc/test_sdmmc_timeout.c" "$OUT/sdmmc_timeout_wrap-$1.c" || exit 1
    "$OUT/test_sdmmc_timeout-$1" > "$OUT/sdmmc_timeout-$1.log" 2>&1
    local st=$?
    grep -m1 "FAIL" "$OUT/sdmmc_timeout-$1.log" | cut -c1-150 | sed 's/^ */    | /'
    ! cmp -s "$TREE/esp/main/sdmmc_timeout_wrap.c" "$OUT/sdmmc_timeout_wrap-$1.c" && [ $st != 0 ]
    result $? "the sdmmc_timeout_wrap.c test sees it: $3"
}
planted_wrap none 's/if (timed && ok) {/if (0) {/' \
    "a command whose event never came given back as done, as the driver gives it"
planted_wrap late 's/(ticks - 1) \* portTICK_PERIOD_MS/ticks * portTICK_PERIOD_MS/' \
    "a lost event taken only after all its ticks"
planted_wrap count 's/ret = written_count(ret, ok);/(void) ok;/' \
    "a failed multi-block write given the result of the card's count"
NEITHER="-DSD_WRAP_TIMEOUT=0 -DSD_WRAP_CMD25=0"
planted_wrap noforget 's/^\t\tsd_setup_forget();$/\t\t(void) 0;/' \
    "with neither part, the slot's settings kept after a failed command" "$NEITHER"
planted_wrap timeoutin 's/^#if SD_WRAP_TIMEOUT$/#if 1/' \
    "with neither part, the timeout part built in all the same" "$NEITHER"
planted_wrap cmd25in 's/^#if SD_WRAP_CMD25$/#if 1/' \
    "with neither part, the CMD25 part built in all the same" "$NEITHER"
planted_wrap defaultout 's/^#define SD_WRAP_\(TIMEOUT\|CMD25\) 1$/#define SD_WRAP_\1 0/' \
    "with neither part given, both left out"
# esp/main/sdmmc_fixes.cmake, which decides the parts (cmake -P,
# tests/host/sdmmc/test_sdmmc_fixes.cmake): the two files it reads as
# ESP-IDF 188e3e55bb has them (from the ESP-IDF's git at IDF_PATH), with
# each fix and with both (s31/esp-idf, the patches as GitHub gives them);
# copies with a fix altered so that the fault stays, or with it only in a
# comment, must count as not fixed; and the ESP-IDF at IDF_PATH itself
IDF=${IDF_PATH:-$HOME/esp/esp-idf}
FX=$OUT/sdmmc_fixes
TRANS=components/esp_driver_sdmmc/src/sd_trans_sdmmc.c
CMD=components/sdmmc/sdmmc_cmd.c
if command -v cmake > /dev/null && command -v patch > /dev/null &&
   GIT_OPTIONAL_LOCKS=0 git -C "$IDF" cat-file -e 188e3e55bb:$CMD 2> /dev/null; then
    rm -rf "$FX"
    for c in 188e3e55bb sync cmd25 both; do
        for f in $TRANS $CMD; do
            mkdir -p "$FX/$c/$(dirname $f)"
            GIT_OPTIONAL_LOCKS=0 git -C "$IDF" show 188e3e55bb:$f > "$FX/$c/$f" || exit 1
        done
    done
    for c in sync both; do
        patch -s -p1 -d "$FX/$c" < "$TREE/s31/esp-idf/e00861c3f497.patch" || exit 1
    done
    for c in cmd25 both; do
        patch -s -p1 -d "$FX/$c" < "$TREE/s31/esp-idf/007945125db7.patch" || exit 1
    done
    cmake -DTREE="$TREE" -DOUT="$FX" -DIDF="$IDF" -P "$HERE/sdmmc/test_sdmmc_fixes.cmake" \
        > "$OUT/sdmmc_fixes.log" 2>&1
    st=$?
    grep -E "FAIL|ESP-IDF at|none of the copies|same files|test_sdmmc_fixes" "$OUT/sdmmc_fixes.log" | sed 's/^/    | /'
    result $st "sdmmc_fixes.cmake: each fix found in ESP-IDF's fixed source and only there, not when altered so the fault stays"
else
    echo "SKIP sdmmc_fixes.cmake: no cmake or patch, or no ESP-IDF 188e3e55bb in git at $IDF"
fi

# v5.3: esp/main/sdmmc_setup_wrap.c, the slot's width, sampling mode and
# delays kept between commands, around stand-ins for ESP-IDF's five that
# write a pretend controller, and a model of the start of its
# sd_host_slot_sdmmc_do_transaction(): before every command the controller
# must hold what ESP-IDF's own four would leave there, through the card's
# start, each setting changed, clock sets, failed commands, one of the four
# failing and the host set up again; and the four are skipped when nothing
# changed.  Three planted faults, which the test has to see.
$CC $CFLAGS -I"$HERE/sdmmc/stubs" -o "$OUT/test_sdmmc_setup" "$HERE/sdmmc/test_sdmmc_setup.c" \
    "$TREE/esp/main/sdmmc_setup_wrap.c" || exit 1
"$OUT/test_sdmmc_setup" > "$OUT/sdmmc_setup.log" 2>&1
st=$?
grep -E "FAIL|set .* times|test_sdmmc_setup" "$OUT/sdmmc_setup.log" | sed 's/^/    | /'
result $st "sdmmc_setup_wrap.c: the slot's settings as ESP-IDF would leave them before every command, set only when something changed"
planted_setup() {
    sed -e "$2" "$TREE/esp/main/sdmmc_setup_wrap.c" > "$OUT/sdmmc_setup_wrap-$1.c"
    $CC $CFLAGS -w -I"$HERE/sdmmc/stubs" -I"$TREE/esp/main" -o "$OUT/test_sdmmc_setup-$1" \
        "$HERE/sdmmc/test_sdmmc_setup.c" "$OUT/sdmmc_setup_wrap-$1.c" || exit 1
    "$OUT/test_sdmmc_setup-$1" > "$OUT/sdmmc_setup-$1.log" 2>&1
    local st=$?
    grep -m1 "FAIL" "$OUT/sdmmc_setup-$1.log" | cut -c1-150 | sed 's/^ */    | /'
    ! cmp -s "$TREE/esp/main/sdmmc_setup_wrap.c" "$OUT/sdmmc_setup_wrap-$1.c" && [ $st != 0 ]
    result $? "the sdmmc_setup_wrap.c test sees it: $3"
}
planted_setup nokey 's/s_skip = sd_clock_fix \&\& s_right_era == s_era \&\& setup_same(slot, \&s_set);/s_skip = sd_clock_fix \&\& s_right_era == s_era;/' \
    "the four skipped although a setting changed"
planted_setup noclk '/^esp_err_t __wrap_sd_host_slot_set_card_clk/,/^}/s/\ts_era++;//' \
    "the four skipped after a clock set"
planted_setup noforget '/^void sd_setup_forget/,/^}/s/\ts_era++;//' \
    "the four skipped after a failed command"

# ==== 14e. after a write, the card asked again as D0 goes free; no status after a read (v5.3): tests/host/sdmmc ====
# ESP-IDF's own sdmmc_cmd.c and sdmmc_common.c (IDF_PATH, read only), built
# with their real headers and the stand-ins of tests/host/sdmmc/idf_stubs,
# linked as the S31 boards link them, around idf_card.c's pretend card and
# clock.  esp/main/sdmmc_wait_wrap.c against ESP-IDF's own wait after
# writes of 1 and 64 sectors, the card programming for 0 to 6 s, with D0
# working, stuck busy and stuck free, for the emulator's task and another:
# the same results, never later, at most one CMD13 more, and with D0
# working done within 60 us of the card; and counts that tell a D0 stuck
# free from one that works; other tasks sleeping as before, the emulator's
# never.  Seven planted faults.
echo
IDF=${IDF_PATH:-$HOME/esp/esp-idf}
IC=$IDF/components
SI="-I$HERE/sdmmc/idf_stubs -I$HERE/sdmmc -I$IC/sdmmc/include -I$IC/esp_hal_sd/include -I$IC/esp_common/include -I$IC/esp_blockdev/include -I$TREE/esp/main"
if [ -f "$IC/sdmmc/sdmmc_cmd.c" ] && [ -f "$IC/sdmmc/sdmmc_common.c" ]; then
    for f in sdmmc_cmd sdmmc_common; do
        $CC -O1 -g -w -ffunction-sections $SI -c -o "$OUT/idf_$f.o" "$IC/sdmmc/$f.c" || exit 1
    done
    $CC $CFLAGS -ffunction-sections $SI -c -o "$OUT/idf_card.o" "$HERE/sdmmc/idf_card.c" || exit 1
    # wait_test NAME WRAP.c: test_sdmmc_wait with that wrap, OUT/sdmmc_wait-NAME.log
    wait_test() {
        $CC $CFLAGS -w -ffunction-sections $SI -o "$OUT/test_sdmmc_wait-$1" "$HERE/sdmmc/test_sdmmc_wait.c" \
            "$2" "$OUT/idf_card.o" "$OUT/idf_sdmmc_cmd.o" "$OUT/idf_sdmmc_common.o" \
            -Wl,--gc-sections -Wl,--wrap=sdmmc_wait_for_idle || exit 1
        "$OUT/test_sdmmc_wait-$1" > "$OUT/sdmmc_wait-$1.log" 2>&1
    }
    $CC $CFLAGS -Werror -ffunction-sections $SI -fsyntax-only "$TREE/esp/main/sdmmc_wait_wrap.c" || exit 1
    wait_test tree "$TREE/esp/main/sdmmc_wait_wrap.c"
    st=$?
    grep -E "FAIL|cases|1600 \||profile line|test_sdmmc_wait" "$OUT/sdmmc_wait-tree.log" | sed 's/^/    | /'
    result $st "sdmmc_wait_wrap.c with ESP-IDF's own sdmmc_cmd.c: after a write, done as the card lets go of D0, never later than ESP-IDF's wait, the same results"
    planted_wait() {
        sed -e "$2" "$TREE/esp/main/sdmmc_wait_wrap.c" > "$OUT/sdmmc_wait_wrap-$1.c"
        wait_test $1 "$OUT/sdmmc_wait_wrap-$1.c"
        local st=$?
        grep -m1 "FAIL" "$OUT/sdmmc_wait-$1.log" | cut -c1-150 | sed 's/^ */    | /'
        ! cmp -s "$TREE/esp/main/sdmmc_wait_wrap.c" "$OUT/sdmmc_wait_wrap-$1.c" && [ $st != 0 ]
        result $? "the sdmmc_wait_wrap.c test sees it: $3"
    }
    planted_wait noarm 's/else if (\*armed)/else/' \
        "a CMD13 whenever D0 is free, not only as it goes free"
    planted_wait d0ready 's/^\t\tif (early) {$/\t\tif (early) { return ESP_OK;/' \
        "the card taken as ready on D0 alone"
    planted_wait nolimit 's/return ESP_ERR_TIMEOUT;/(void) 0;/' \
        "no 5 s limit"
    planted_wait late 's/^\t\tif (early) {$/\t\tif (early) { due = 0;/' \
        "after a CMD13 at D0's release, the next on a schedule of its own"
    planted_wait freecount 's/if (armed == D0_SEEN_BUSY)$/if (armed)/' \
        "a CMD13 sent at once with D0 free at the start counted as one at D0's release"
    planted_wait nostartfree 's/? D0_NOT_YET : D0_FREE_AT_START;/? D0_NOT_YET : D0_NOT_YET;/' \
        "no CMD13 at once when D0 is free as the wait begins"
    planted_wait spinall 's/bool spin = sd_wait_spin_task \&\& xTaskGetCurrentTaskHandle() == sd_wait_spin_task;/bool spin = true;/' \
        "every task watching D0 past ESP-IDF's busy-wait, none sleeping"

    # v5.3: esp/main/sdmmc_read_wrap.c, no CMD13 after a read, against
    # ESP-IDF's own sdmmc_read_sectors(): every read made through both, from
    # buffers the host takes and ones it refuses, 0 to 130 sectors, at the
    # card's ends and past them, reads that fail; then reads and writes
    # mixed with the wait after writes linked too; the same CMD17s and
    # CMD18s as ESP-IDF's.  Five planted faults.
    $CC $CFLAGS -ffunction-sections $SI -c -o "$OUT/sdmmc_wait_wrap.o" "$TREE/esp/main/sdmmc_wait_wrap.c" || exit 1
    read_test() {
        $CC $CFLAGS -w -ffunction-sections $SI -o "$OUT/test_sdmmc_read-$1" "$HERE/sdmmc/test_sdmmc_read.c" \
            "$2" "$OUT/sdmmc_wait_wrap.o" "$OUT/idf_card.o" "$OUT/idf_sdmmc_cmd.o" "$OUT/idf_sdmmc_common.o" \
            -Wl,--gc-sections -Wl,--wrap=sdmmc_wait_for_idle -Wl,--wrap=sdmmc_read_sectors || exit 1
        "$OUT/test_sdmmc_read-$1" > "$OUT/sdmmc_read-$1.log" 2>&1
    }
    $CC $CFLAGS -Werror -ffunction-sections $SI -fsyntax-only "$TREE/esp/main/sdmmc_read_wrap.c" || exit 1
    read_test tree "$TREE/esp/main/sdmmc_read_wrap.c"
    st=$?
    grep -E "FAIL|made twice|no CMD13|test_sdmmc_read" "$OUT/sdmmc_read-tree.log" | sed 's/^/    | /'
    result $st "sdmmc_read_wrap.c with ESP-IDF's own sdmmc_cmd.c: every read as ESP-IDF's, with no CMD13 after a good one into a buffer the host takes"
    planted_read() {
        sed -e "$2" "$TREE/esp/main/sdmmc_read_wrap.c" > "$OUT/sdmmc_read_wrap-$1.c"
        read_test $1 "$OUT/sdmmc_read_wrap-$1.c"
        local st=$?
        grep -m1 "FAIL" "$OUT/sdmmc_read-$1.log" | cut -c1-150 | sed 's/^ */    | /'
        ! cmp -s "$TREE/esp/main/sdmmc_read_wrap.c" "$OUT/sdmmc_read_wrap-$1.c" && [ $st != 0 ]
        result $? "the sdmmc_read_wrap.c test sees it: $3"
    }
    planted_read nocheck 's/!card->host.check_buffer_alignment(card->host.slot, dst, block_size \* block_count))/0)/' \
        "every buffer sent to the card as it is, the host's check not asked"
    planted_read bytes 's/cmd.arg = (card->ocr \& SD_OCR_SDHC_CAP) ? start_block : start_block \* block_size;/cmd.arg = start_block * block_size;/' \
        "a byte address for an SDHC card"
    planted_read short 's/\.datalen = block_count \* block_size,/.datalen = block_count * block_size \/ 2,/' \
        "half the sectors asked for"
    planted_read nolimit 's/return ESP_ERR_INVALID_SIZE;/(void) 0;/' \
        "no check against the card's size"
    planted_read multi 's/cmd.opcode = block_count == 1 ? MMC_READ_BLOCK_SINGLE : MMC_READ_BLOCK_MULTIPLE;/cmd.opcode = MMC_READ_BLOCK_MULTIPLE;/' \
        "a one-sector read sent as CMD18"
else
    echo "SKIP sdmmc_wait_wrap.c: no ESP-IDF sdmmc sources at $IC/sdmmc"
fi

# ==== 15. the status LED (v3.3; the brightness v3.7; the disk light v3.8) ====
# status_code.c as the firmware has it, and status_bb.h (the panic handler's
# bit-bang) with a pretend cycle counter: the RMT frame and every bit-banged
# edge against both LEDs' timing, the colours and blinks, the error-code
# state machine, the replay after a reset, the beat watch, the record, the
# brightness: the scale at every level, the default, both paths' frames
# at the same level (esp/main given, so that status_led.c and status_code.c
# are read for how their frames are made); and the disk light: the LED
# task's looks against pretend disk accesses, and where ide.c, misc.c and
# esp_main.c count them and hand them over.
echo
$CC $CFLAGS -DSTATUS_HOST -I"$TREE/esp/main" -o "$OUT/test_status" \
    "$HERE/test_status.c" "$TREE/esp/main/status_code.c" || exit 1
"$OUT/test_status" "$TREE/esp/main"
result $? "status LED: RMT and bit-banged timing for both LEDs, patterns, state machine, replay, beat watch, record, brightness, disk light"

# ==== 16. the sound mix (v3.5): tests/host/dosboard/audio_mix_test.c ====
# esp/main/audio_mix.c with the emulator's own pcspk.c, adlib.c, fmopl.c
# and sb16.c at the DOS board's 48 kHz, and the DOS board's default level
# taken from board_dosboard.h: the 32-bit mix against a model, against
# v3.4's path, nothing held at the default level, held and not wrapped
# beyond it, and the devices at their loudest.
echo
SHIFT=$(sed -n 's/^#define AUDIO_SHIFT_DEFAULT  *\([0-9][0-9]*\).*/\1/p' "$TREE/esp/main/board_dosboard.h")
[ -n "$SHIFT" ] || { echo "no AUDIO_SHIFT_DEFAULT in board_dosboard.h"; exit 1; }
$CC $DOSCFLAGS -DAUDIO_MIX_HOST -I"$TREE/esp/main" -c -o "$OUT/audio_mix.o" \
    "$TREE/esp/main/audio_mix.c" || exit 1
$CC -O2 -g -w -DUSE_FMOPL -DAUDIO_RATE=48000 -DAUDIO_MIX_HOST -DAUDIO_SHIFT_DEFAULT_TEST=$SHIFT \
    -I"$TREE" -I"$TREE/esp/main" -o "$OUT/audio_mix_test" "$D/audio_mix_test.c" \
    "$OUT/audio_mix.o" "$TREE/pcspk.c" "$TREE/adlib.c" "$TREE/fmopl.c" -lm || exit 1
"$OUT/audio_mix_test"
result $? "audio_mix.c: 32-bit mix, held at full scale and never wrapped, default level $SHIFT, the devices at their loudest"

# ==== 17. the DOS board's pins against its netlist (v3.5) ====
# board_dosboard.h against the board's KiCad netlist, outside this tree
# (DOSBOARD_NET names it): each pin on its net, and the GPIOs the board
# gives the J202 header (GPIO2 to GPIO7, GPIO57, GPIO60) named nowhere.
echo
DOSBOARD_NET=${DOSBOARD_NET:-/mnt/d/Repos/esp32-s31-linux-release/tiny386-hdmi-board/dosboard/out/dosboard.net}
if [ -f "$DOSBOARD_NET" ]; then
    python3 "$D/check_pins.py" "$TREE/esp/main/board_dosboard.h" "$DOSBOARD_NET"
    result $? "board_dosboard.h against the DOS board's netlist: every pin on its net, the header's GPIOs left free"
else
    echo "SKIP the DOS board's pins: no netlist at $DOSBOARD_NET (set DOSBOARD_NET)"
fi

# ==== 18. the DOS board image's board check (v3.6) ====
# dosboard.c's board_early_init() as the DOS board build has it
# (board_dosboard.h first), with the real ms7210.c, against a pretend I2C
# bus and pretend pins (tests/host/dosboard/shim): Korvo-1s and DOS boards,
# which pins are touched and when, and what it decides.  If the tree has the
# v3.5 tag, the same cases against v3.5's dosboard.c, which has to fail
# (it pulsed GPIO49 on a Korvo-1 with nothing on its bus, and stopped a DOS
# board with a device on J202 at one of a Korvo-1's addresses).
echo
BCFLAGS="$DOSCFLAGS -I$D/shim -I$TREE/esp/main -I$TREE"
$CC $BCFLAGS -include "$TREE/esp/main/board_dosboard.h" -c -o "$OUT/bc_dosboard.o" \
    "$TREE/esp/main/dosboard.c" || exit 1
$CC $BCFLAGS -c -o "$OUT/bc_ms7210.o" "$TREE/esp/main/ms7210.c" || exit 1
$CC -O2 -g -w -c -o "$OUT/bc_ini.o" "$TREE/ini.c" || exit 1
$CC $BCFLAGS -include "$TREE/esp/main/board_dosboard.h" -o "$OUT/board_check_test" \
    "$D/board_check_test.c" "$OUT/bc_dosboard.o" "$OUT/bc_ms7210.o" "$OUT/bc_ini.o" || exit 1
"$OUT/board_check_test"
result $? "dosboard.c board check: pins touched and when, Korvo-1s and DOS boards, J202 devices, GPIO46"
T_V3_5=$(tagref v3.5)
if [ -n "$T_V3_5" ]; then
    git -C "$TREE" show "$T_V3_5":esp/main/dosboard.c > "$OUT/dosboard-v3.5.c"
    $CC $BCFLAGS -w -include "$TREE/esp/main/board_dosboard.h" -c -o "$OUT/bc_dosboard_v35.o" \
        "$OUT/dosboard-v3.5.c" || exit 1
    $CC $BCFLAGS -DBOARD_CHECK_V35 -include "$TREE/esp/main/board_dosboard.h" \
        -o "$OUT/board_check_test_v35" "$D/board_check_test.c" "$OUT/bc_dosboard_v35.o" \
        "$OUT/bc_ms7210.o" "$OUT/bc_ini.o" || exit 1
    echo "(the same cases against dosboard.c at the v3.5 tag, where it has to fail)"
    "$OUT/board_check_test_v35" > "$OUT/board-check-v3.5.log" 2>&1
    st=$?
    grep -E "^    (K|D)[0-9]+: |cases as expected" "$OUT/board-check-v3.5.log" | sed 's/^ */    | /'
    [ $st != 0 ]
    result $? "board check test catches v3.5's GPIO49 pulse on a bare Korvo-1 and its stop on a J202 device"
fi

# ==== 19. the timer after the emulator is held up (v3.8) ====
# i8254.c and misc.c's CMOS periodic interrupt with a pretend clock: after a
# hold of many periods one tick is raised and the rest dropped, on the
# counter's phase (i8254_max_owed); 0 gives every one back to back, as
# before.  Then i8254.c as tiny386 released it, where the burst has to be
# seen.
echo
$CC $CFLAGS -I"$TREE" -c -o "$OUT/pit_i8254.o" "$TREE/i8254.c" || exit 1
$CC -O1 -g -w -DEMULINK_CHECKED -I"$TREE" -c -o "$OUT/pit_misc.o" "$TREE/misc.c" || exit 1
$CC $CFLAGS -I"$TREE" -o "$OUT/test_pit" "$HERE/test_pit.c" "$OUT/pit_i8254.o" \
    "$OUT/pit_misc.o" || exit 1
"$OUT/test_pit"
result $? "i8254.c and the CMOS: missed timer ticks dropped after a hold, one raised, on the counter's phase"
if [ -n "$T_PRISTINE" ]; then
    git -C "$TREE" show "$T_PRISTINE":i8254.c > "$OUT/i8254-pristine.c"
    $CC -O1 -g -w -I"$TREE" -c -o "$OUT/pit_i8254_pristine.o" "$OUT/i8254-pristine.c" || exit 1
    $CC $CFLAGS -DPIT_PRISTINE -I"$TREE" -o "$OUT/test_pit_pristine" "$HERE/test_pit.c" \
        "$OUT/pit_i8254_pristine.o" || exit 1
    echo "(the same hold against i8254.c at the pristine tag, where the burst has to be there)"
    "$OUT/test_pit_pristine" pristine | sed 's/^/    | /'
    result ${PIPESTATUS[0]} "the PIT test sees the burst in pristine i8254.c"
fi
# v4.0-base: the one-shot modes came with upstream; v3.9's i8254.c has to
# pass everything before them and then stop at mode 0 (abort())
T_V3_9=$(tagref v3.9)
if [ -n "$T_V3_9" ]; then
    git -C "$TREE" show "$T_V3_9":i8254.c > "$OUT/i8254-v3.9.c"
    $CC -O1 -g -w -I"$TREE" -c -o "$OUT/pit_i8254_v39.o" "$OUT/i8254-v3.9.c" || exit 1
    $CC $CFLAGS -I"$TREE" -o "$OUT/test_pit_v39" "$HERE/test_pit.c" \
        "$OUT/pit_i8254_v39.o" "$OUT/pit_misc.o" || exit 1
    echo "(the same test against v3.9's i8254.c, which has to stop at mode 0)"
    "$OUT/test_pit_v39" > "$OUT/pit-v3.9.log" 2>&1
    st=$?
    tail -2 "$OUT/pit-v3.9.log" | sed 's/^/    | /'
    echo "    | ($(grep -c '^  ok:' "$OUT/pit-v3.9.log") checks passed, $(grep -c 'FAIL' "$OUT/pit-v3.9.log") failed; exit status $st)"
    [ $st != 0 ] && ! grep -q FAIL "$OUT/pit-v3.9.log" &&
        grep -q "^== the one-shot modes" "$OUT/pit-v3.9.log" &&
        tail -1 "$OUT/pit-v3.9.log" | grep -q "^abort() called"
    result $? "the PIT test sees v3.9's i8254.c stop at mode 0"
fi

# ==== 19b. the PIT's conversions by 32-bit multiplies (v5.3, QW_PITX) ====
# i8254.c's pit_ticks() and pit_us_up() against the 64-bit multiply and
# division each stands for, at every one of the 2^32 inputs
# (test_pit_conv.c, in 16 processes).  Then four planted faults, each in a
# copy of i8254.c, have to differ somewhere: no correction step, the
# multiplier one too big, the rounding up at its boundary, and the
# multiply alone as yejun/tab5-pc's patch 0007 has it (us * 2562338834 >>
# 31), which is a tick short for some inputs.
echo
$CC -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -I"$TREE" \
    -DPIT_C="\"$TREE/i8254.c\"" -o "$OUT/test_pit_conv" "$HERE/test_pit_conv.c" || exit 1
"$OUT/test_pit_conv" 16
result $? "i8254.c: the count and the plan's time by 32-bit multiplies, the same as the 64-bit division for all 2^32 inputs"
sed 's/if (t \* 193182u - q \* 1000000u >= 1000000u)/if (0)/' "$TREE/i8254.c" > "$OUT/i8254-pitx-nofix.c"
sed 's/829710372u/829710373u/' "$TREE/i8254.c" > "$OUT/i8254-pitx-mul.c"
sed 's/return u + 1 + (r > 193182u);/return u + 1 + (r >= 193182u);/' "$TREE/i8254.c" > "$OUT/i8254-pitx-up.c"
awk '/uint32_t q = \(uint32_t\) \(\(\(uint64_t\) t \* 829710372u\) >> 32\);/ {
        print "\treturn (uint32_t) (((uint64_t) t * 2562338834u) >> 31);"; skip = 3; next }
     skip > 0 { skip--; next } { print }' "$TREE/i8254.c" > "$OUT/i8254-pitx-0007.c"
for f in nofix mul up 0007; do
    if cmp -s "$OUT/i8254-pitx-$f.c" "$TREE/i8254.c"; then
        result 1 "planted fault pitx-$f put in i8254.c's copy"
        continue
    fi
    $CC -O2 -g -w -I"$TREE" -DPIT_C="\"$OUT/i8254-pitx-$f.c\"" \
        -o "$OUT/test_pit_conv_$f" "$HERE/test_pit_conv.c" || exit 1
    "$OUT/test_pit_conv_$f" 16 > "$OUT/pit-conv-$f.log" 2>&1
    st=$?
    grep FAIL "$OUT/pit-conv-$f.log" | sed 's/^ */    | /'
    [ $st != 0 ]
    result $? "the conversion test sees planted fault pitx-$f"
done

# ==== 20. faults while an exception or interrupt is delivered (v3.8) ====
# i386.c with a small protected-mode guest (tests/host/cpu): the double
# fault through a task gate, as Windows 95's VMM has it, with its error
# code; the ring 3 stack put back before the fault; a shutdown (and a CR3
# outside the guest's memory) stopping the emulator with abort(), not a
# crash; LLDT's checks.  v3.7's i386.c has to stop at the first double
# fault, as the board did.
echo
if bash "$HERE/cpu/run_cpu.sh" "$OUT/cpu" > "$OUT/cpu.log" 2>&1; then
    st=0
else
    st=1
fi
if grep -q "^SKIP" "$OUT/cpu.log"; then
    grep "^SKIP" "$OUT/cpu.log"
else
    grep -E "FAILED|^cpu:|^  ok:" "$OUT/cpu.log"
    result $st "i386.c: double faults delivered through the task gate, the ring 3 stack put back, shutdown and a bad CR3 stop cleanly, LLDT checked"
fi

# ==== 21. 16 and 32-bit accesses that cross a page (v4.6) ====
# i386.c as the Korvo-1 builds it and as tiny386's own Makefile builds it,
# with the small guests in tests/host/cpu/test_page_cross.c: an access that
# runs on into the next 4 KiB page goes a byte at a time, so its bytes in
# the VGA window go to the VGA (not into phys_mem, where the board keeps
# the devices' state) and those past the end of RAM nowhere, and the
# crossings from RAM to RAM are as before.  Then v4.5's i386.c, where those
# bytes have to land in phys_mem.
echo
PCFLAGS="-O2 -g -w -I$TREE"
PCBOARD="-include $TREE/esp/main/board_korvo1.h -I$HERE/shim -DI386_ENABLE_FPU"
pcbuild() {     # name i386.c [flags]: OUT/test_page_cross-name
    local name=$1 src=$2
    shift 2
    # i386.c's own warnings are not this test's business
    $CC $PCFLAGS "$@" -c -o "$OUT/pc_i386-$name.o" "$src" || exit 1
    $CC $PCFLAGS "$@" -c -o "$OUT/pc_fpu-$name.o" "$TREE/fpu.c" || exit 1
    $CC $CFLAGS -I"$TREE" "$@" -o "$OUT/test_page_cross-$name" "$HERE/cpu/test_page_cross.c" \
        "$OUT/pc_i386-$name.o" "$OUT/pc_fpu-$name.o" -lm || exit 1
}
pcbuild korvo1 "$TREE/i386.c" $PCBOARD
"$OUT/test_page_cross-korvo1" > "$OUT/page-cross-korvo1.log" 2>&1
st=$?
grep -v "^  eax \|^  window " "$OUT/page-cross-korvo1.log"
result $st "i386.c as the Korvo-1 builds it: accesses that cross a page go a byte at a time, none into the window's phys_mem or past RAM"
pcbuild plain "$TREE/i386.c" -DI386_ALIGNED_ONLY
"$OUT/test_page_cross-plain" > "$OUT/page-cross-plain.log" 2>&1
st=$?
tail -1 "$OUT/page-cross-plain.log" | sed 's/^/    | /'
result $st "i386.c without the board header: the same"
T_V4_5=$(tagref v4.5 99074b7347bec45d743a56254df218fe616c7d29)
if [ -n "$T_V4_5" ]; then
    git -C "$TREE" show "$T_V4_5":i386.c > "$OUT/i386-v4.5.c"
    pcbuild v4.5 "$OUT/i386-v4.5.c" $PCBOARD
    echo "(the same guests against v4.5's i386.c, where those bytes have to land in phys_mem)"
    "$OUT/test_page_cross-v4.5" before > "$OUT/page-cross-v4.5.log" 2>&1
    st=$?
    grep "ok: v4.5\|FAIL\|checks" "$OUT/page-cross-v4.5.log" | sed 's/^ */    | /'
    result $st "the page-crossing test sees v4.5's bytes in the window's phys_mem and past RAM"
fi

# ==== 22. ISA DMA into the 0xA0000 window (v4.6) ====
# i8257.c with tests/host/dma/test_dma_window.c: a transfer to memory drops
# the bytes for 0xA0000 to 0xBFFFF, where the board keeps the devices'
# state (SB16 command E2h writes a byte wherever channel 1 points), and
# writes the rest to RAM as before.  Then v4.5's i8257.c, where those bytes
# have to land in phys_mem.
echo
$CC -O1 -g -w -I"$TREE" -c -o "$OUT/dma_i8257.o" "$TREE/i8257.c" || exit 1
$CC $CFLAGS -I"$TREE" -o "$OUT/test_dma_window" "$HERE/dma/test_dma_window.c" \
    "$OUT/dma_i8257.o" || exit 1
"$OUT/test_dma_window" > "$OUT/dma-window.log" 2>&1
st=$?
grep -v "^  window " "$OUT/dma-window.log"
result $st "i8257.c: DMA writes into the 0xA0000 window dropped, the rest of a transfer to RAM as before"
if [ -n "$T_V4_5" ]; then
    git -C "$TREE" show "$T_V4_5":i8257.c > "$OUT/i8257-v4.5.c"
    $CC -O1 -g -w -I"$TREE" -c -o "$OUT/dma_i8257_v45.o" "$OUT/i8257-v4.5.c" || exit 1
    $CC $CFLAGS -I"$TREE" -o "$OUT/test_dma_window_v45" "$HERE/dma/test_dma_window.c" \
        "$OUT/dma_i8257_v45.o" || exit 1
    echo "(the same transfers against v4.5's i8257.c, where those bytes have to land in phys_mem)"
    "$OUT/test_dma_window_v45" before > "$OUT/dma-window-v4.5.log" 2>&1
    st=$?
    grep "ok: v4.5\|FAIL\|checks" "$OUT/dma-window-v4.5.log" | sed 's/^ */    | /'
    result $st "the DMA test sees v4.5's bytes in the window's phys_mem"
fi

# ==== 23. the linear frame buffer's own path in i386.c (v5.0) ====
# tests/host/lfb: the whole PC (pc_new() with the Korvo-1's header), VBE at
# 800x480 in 16 bits set by the guest, and the guests of guest_lfb.S storing
# into and loading from video memory every way the CPU can, on 768K and 1M
# of video memory, with vbe_blocks 1 and 0.  Each case checked for itself
# (video memory changed only where it stores, the marks exactly its blocks,
# vga_wrote), and the output the same as v4.6's tree gives, whose path was
# pc.c's iomem calls (every changed block of video memory and page of RAM by
# its hash, every mark, the flag, the registers).  Then, with the board's
# compiler and qemu-riscv32 (SKIP without them), the host instructions of
# each bench piece, v4.6 against this tree.
echo
bash "$HERE/lfb/run_lfb.sh" "$OUT/lfb" > "$OUT/lfb.log" 2>&1
st=$?
grep "^SKIP\|^==\|^    |" "$OUT/lfb.log"
n=0
while read -r line; do
    n=$((n + 1))
    case "$line" in
    "ok: "*) result 0 "lfb: ${line#ok: }" ;;
    *) result 1 "lfb: ${line#FAILED: }" ;;
    esac
done < <(grep "^  ok: \|^  FAILED: " "$OUT/lfb.log" | sed 's/^  //')
if [ $st != 0 ] && ! grep -q "^  FAILED: " "$OUT/lfb.log"; then
    tail -20 "$OUT/lfb.log" | sed 's/^/    | /'
    result 1 "lfb: tests/host/lfb/run_lfb.sh stopped (exit status $st)"
elif [ $n = 0 ] && ! grep -q "^SKIP lfb:" "$OUT/lfb.log"; then
    result 1 "lfb: tests/host/lfb/run_lfb.sh ran nothing"
fi

# ==== 24. the fast interpreter against tiny386's own, in lockstep (v5.0) ====
# tests/host/cpu/test_fast_cpu.c: two machines, one with the fast
# interpreter (i386_fast.h, from tab5-pc) and one with tiny386's cpu_exec1()
# alone, run the same random guest code (real mode, protected mode with
# paging at levels 0, 1 and 3, virtual-8086 mode, 16-bit protected mode with
# and without paging, mixed code and stack sizes; since the v5.0 reviews,
# with the frame buffer given to the CPU for odd seeds) and four
# hand-written cases, with the same interrupts, and after every step their
# registers, lazy flags, instruction counts, CR2, tiny386's fetch cache and
# TLB, all memory, the frame buffer's marks and every port and video memory
# access (with the count at the access) must be the same.  As the Korvo-1
# builds i386.c and without the board header.  Then copies of i386_fast.h
# with a fault put in, which the test has to catch: CMP fused with a JZ
# after it where tiny386 fetched the CMP the slow way (a bug found by this
# test), the instruction count left behind at port accesses, levels 1 and 2
# given level 3's TLB (the CPU review's finding), a MOV into the frame
# buffer through its TLB entry without its marks, and a frame buffer page
# given a plain write entry.  v5.1 (v5.0's recheck, RC-1 and RC-2): the
# frame buffer from 16-bit protected mode, level 3 and unreal mode, moved
# by tiny386 too (OUTSB to port E4h), its dirty bits (case j), and the fast
# TLB's invariants after every step; and four more faults: no flush after
# the fast OUT moves the frame buffer, no lfb_gen test in fast_tlb_check(),
# no dirty bit on a store through a frame buffer page's write tag, and an
# old write tag kept when fast_miss() gives an entry a frame buffer page.
# (The lockstep tester's t386mon800 finds none of these four: Windows never
# moves the frame buffer while it draws, nor clears its dirty bits.)
# v5.1 (WI-1: the loop's pc in a register, the code window's one bound,
# new code pages through the fast TLB, the one-compare hit, the window
# refilled inline): modes k (jumps, calls and returns across code pages,
# into the last 16 bytes of the page before and below them) and l (a page
# read, then written where the write must fault and where it must not;
# null segments; code in the frame buffer), and five more faults: the
# jumps without the window's lower bound (jumplo), a new code page taken
# from the frame buffer's read entry (codelfb), FAST_PL() without the null
# segment's bit (plnull), the inline refill without the fusion rule's
# saves (st4fold), and, in i386.c, fast_lpgno() without its test of an
# entry that refuses the access (lpgno, the v5.0 difference mode l found).
# Of these five the 800 scenarios of modes 0 to j catch only jumplo, in 2.
# v5.1 (WI-2): case j's REP STOSD and REP MOVSD into the frame buffer with
# its dirty bits cleared between them, OUTs to its BAR between REP
# instructions and level 3 against a supervisor page of it, and case m, the
# two-operand rule by hand; and three more faults: no dirty bit where a REP
# piece or MOVS finds the frame buffer's write tag (FPTR_IO_W()), the
# two-operand rule taken out of MOVS's destination, and out of CMPS.
# v5.1 (WI-2's check): case m's REP string instruction handed to tiny386
# part way as the first in a page newly cached, and a CMP and JZ there, and
# mode x, two memory operands at random on pages that share TLB indices;
# and four more faults: tiny386 not given back its fetch cache for the
# first instruction in a page newly cached (restore: it fuses a CMP and JZ
# that it fetched the slow way alone), MOVS from MMIO looking its
# destination up where tiny386's TLB does not hold it (destheld), and, in
# i386.c, fast_lpgno() without its test of an entry that refuses the
# access (lpgno, as WI-1's) or of the code page's entry (codepage).
# v5.1 (WI-3): mode o, descriptor tables across pages with the second page
# not present, in MMIO, at RAM's end and past it, null selectors, and
# read_desc() against v5.0's own; faults put into i386.c too (fcfault).
# v5.1, after the three reviews of the merged tree (the CPU review's M1 to
# M3): modes p to s (WI-3's check: the group opcodes with every prefix,
# DIV and IDIV with #DE, segment loads), mode t (WI-1's check: code
# fetches, level 3 into a page tiny386 holds as supervisor among them),
# and in mode l stores through a null ES, FS or GS into a frame buffer page
# that has a write tag; and six more faults: the null segment's test after
# the frame buffer's write tag in FPTR_IO_W() (lfbwnull) and in FPTR_ST()
# (stnull), fast_window() and the inline refill given level 0's table
# (fwcpl, st4cpl), DIV without its return address for #DE (divip), and the
# inline refill caching RAM's last page (st4end).  A guest shutdown now
# ends its scenario, not the run.
# v5.2 (the fastx review of the switches after v5.2 in qw.h): mode y, those
# switches at their edges by hand (tests/host/cpu/gen_y.inc: the stack with
# SP at 64 KiB's ends and ESP's top half set, memory operands after mixed
# prefixes, a JZ or JNZ after DEC, CMP and TEST up to a page's end and across
# it, MOV SS or STI first in a page newly cached before a CMP and a JZ that
# tiny386 fuses); and four more faults, which the other modes miss: DEC's
# inline JZ without its start (decjins), QW_STK's 16-bit copy storing all of
# ESP (stk16), and the fusion rule's instruction not moved by a load of SS
# (nofoldss) or by STI (nofoldsti).
# v5.3 (batch 4, fast_exec()'s 16-bit paths): QW_EA16's one test taking a
# segment override for none (ea16ov), and its form for 32-bit code's 16-bit
# operands loading 32 bits (ea16do); mode z (tests/host/cpu/gen_z.inc: jumps
# across offset FFFFh and 0 in real, virtual-8086 and 16-bit protected mode
# with CS's base off a page boundary, targets past the segment's end) and
# QW_J16's mask at a jump out of the window never made (j16eip), and its
# instructions at FFF1h and above not sent to tiny386 (j16end); mode z's
# INC and DEC after each kind of result and every condition after it, and
# QW_INCL's CF after AND, OR and XOR set, not cleared (incl); QW_EA16T's
# [BP+d] with DS for its default segment (ea16ss, which only mode z's INC
# and DEC of [BP+d8] catch); QW_COND2's parity after AND, OR and XOR
# inverted (cond2p); QW_RMREF's page change without paging, with no fusion
# rule's saves (rmfold).  After the review of batch 4: mode z's RET n with
# 66h past FFFFh, and fast_j16_eip() masking its target (c2ret); mode z's
# near JMPs into the VGA window and, repeated, into RAM's last page, and
# QW_RMREF's block without its MMIO tests (rmiomem) or its test of RAM's
# end (rmend).
echo
FCSEEDS=${FCSEEDS:-40}
fcbuild() {     # name dir-with-i386.c-and-i386_fast.h [flags]: OUT/test_fast_cpu-name
    local name=$1 dir=$2
    shift 2
    $CC -O2 -g -w -I"$dir" -I"$TREE" "$@" -o "$OUT/test_fast_cpu-$name" \
        "$HERE/cpu/test_fast_cpu.c" "$TREE/fpu.c" -lm || exit 1
}
fcbuild korvo1 "$TREE" $PCBOARD
"$OUT/test_fast_cpu-korvo1" $FCSEEDS > "$OUT/fast-cpu-korvo1.log" 2>&1
st=$?
grep "FAIL\|^test_fast_cpu" "$OUT/fast-cpu-korvo1.log"
result $st "the fast interpreter as the Korvo-1 builds it: the same as tiny386's own at every step of random guest code"
fcbuild plain "$TREE" -DI386_ALIGNED_ONLY -DI386_ENABLE_FPU
"$OUT/test_fast_cpu-plain" $FCSEEDS > "$OUT/fast-cpu-plain.log" 2>&1
st=$?
grep "FAIL\|^test_fast_cpu" "$OUT/fast-cpu-plain.log"
result $st "the fast interpreter without the board header: the same"
for m in fold:'s/!NOFOLD_IS() \&\& \\/1 \&\& \\/;s/if (_t \&\& !NOFOLD_IS()) { \\/if (_t) { \\/' sync:'s/^\tSYNC_IN();$/\t(void) 0;/' \
         cpl:'s/cpu->cpl == 3\];/cpu->cpl > 0];/' \
         lfbmark:'/^lfb_st: {/,/NEXT; }/s/^\tlfb_marked(cpu, .*$/\t(void) 0;/;s/^\(lfb_st[124]: [^;]*;\) lfb_marked([^;]*);/\1/' \
         lfbwtag:'s/^\t\t\te->wtag = pg | FAST_TAG_LFB;$/\t\t\te->wtag = pg;/' \
         outflush:'s/if (unlikely(cpu->cb.lfb_gen != cpu->fast->lfb_gen_seen))/if (0)/' \
         checkgen:'s/cpu->cb.lfb_gen != fs->lfb_gen_seen))/0))/' \
         lfbdirty:'/mhost = (u8 \*) (uintptr_t) (_lin + _e->delta);/,/goto LFBLABEL;/s/\*_e->ppte |= 0x40;/(void) 0;/' \
         lfbkeepw:'s/^\t\tif (e->rtag != pg || e->delta != delta)$/\t\tif (0)/' \
         jumplo:'s/if (unlikely((uintptr_t) pc < (uintptr_t) win_lo)) win_hi = 0;/(void) 0;/' \
         codelfb:'/^\t\t\tif ((uintptr_t) (h - (const u8 \*) cpu->phys_mem) >= (uintptr_t) cpu->phys_mem_size)$/{N;d}' \
         plnull:'s/^#define FAST_PL(sg, lin, size) ((((lin) + (size) - 1) >> 12) | FAST_NULL20(sg))$/#define FAST_PL(sg, lin, size) (((lin) + (size) - 1) >> 12)/' \
         st4fold:'s/^\t*NOFOLD_SET(); nofold_laddr = cpu->ifetch.laddr; nofold_xaddr = cpu->ifetch.xaddr;.*$/\t\t\t\t(void) 0;/' \
         lpgno:'s/^#define FAST_REFUSED(ent, cpl, wr) .*$/#define FAST_REFUSED(ent, cpl, wr) 0/' \
         repdirty:'/^#define FPTR_IO_W(/,/goto LABEL;/{/FAST_TAG_LFB)) { \\$/,/goto LABEL;/s/\*_e->ppte |= 0x40;/(void) 0;/}' \
         n2:'s/1, \&mpaddr, (H1)))) { \\$/1, \&mpaddr, (NULL)))) { \\/' \
         cmpsrule:'s/^\t\tFPTR2(d, SEG_ES, DI_, n, 0, s);$/\t\tFPTR(d, SEG_ES, DI_, n, 0);/' \
         restore:'s/^\tif (NOFOLD_IS()) {$/\tif (0) {/' \
         destheld:'s/|| cpu->tlb.tab\[((lin) >> 12) % tlb_size\].lpgno == ((lin) >> 12))$/|| 1)/' \
         codepage:'s/^#define FAST_CODE_PAGE(cpu, ent) .*$/#define FAST_CODE_PAGE(cpu, ent) 0/' \
         lfbwnull:'/^#define FPTR_IO_W(/,/} while (0)/{/^\t\t\tif (nullsegs & (1u << (sg))) goto slow; \\$/d;s/^\t\t\tif (!(p = fast_miss2(cpu, ftlb, _lin, (size), 1, &mpaddr, (H1)))) { \\$/\t\t\tif (nullsegs \& (1u << (sg))) goto slow; \\\n&/}' \
         stnull:'/^#define FPTR_ST(/,/} while (0)/{/^\t\t\tif (nullsegs & (1u << (sg))) goto slow; \\$/d;s/^\t\t\tif (!(p = fast_miss(cpu, ftlb, _lin, (size), 1, &mpaddr))) { \\$/\t\t\tif (nullsegs \& (1u << (sg))) goto slow; \\\n&/}' \
         fwcpl:'s/int _r = fast_window(cpu, ftlb, eip, &_w);/int _r = fast_window(cpu, cpu->fast->tab[0], eip, \&_w);/' \
         st4cpl:'/^refill:/,/^\tWINDOW();$/s/^\t\tstruct fast_tlb_entry \*e = &ftlb\[(lin >> 12) % tlb_size\];$/\t\tstruct fast_tlb_entry *e = \&cpu->fast->tab[0][(lin >> 12) % tlb_size];/' \
         divip:'s/^P##6: { cpu->ip = EIP_OF(ins); DIV/P##6: { DIV/' \
         st4end:'s/if (po < (uintptr_t) cpu->phys_mem_size - 4096 && !in_iomem((uword) po)/if (po < (uintptr_t) cpu->phys_mem_size \&\& !in_iomem((uword) po)/' \
         decjins:'/^#define JZNZ_INLINE() do {/,/} while (0)/s/^\t\t\tins = pc; \\$/\t\t\t(void) 0; \\/' \
         stk16:'s/sp_mask == 0xffff) sreg16(4, (v)); \\$/sp_mask == 0xffff) sreg32(4, (v)); \\/' \
         nofoldss:'s/^#define STEPS_SS() do { steps++; sadj++; nofold_st++; } while (0)$/#define STEPS_SS() do { steps++; sadj++; } while (0)/' \
         nofoldsti:'s/ sadj += steps - _o; nofold_st += steps - _o; }/ sadj += steps - _o; }/' \
         ea16ov:'s/^#define EAP_DW(...) else if ((pfx & 0x73) == 3)/#define EAP_DW(...) else if ((pfx \& 3) == 3)/' \
         ea16do:'/EAP_DO(EA_OFF32(off, m) mreg = r;/s/sreg16(r, fm16(p))/sreg32(r, fm32(p))/' \
         j16eip:'s/^\treturn eip \& 0xffff;$/\treturn eip;/' \
         j16end:'s/if (unlikely(cpu->code16) \&\& eip >= FAST_J16_END)/if (0)/;s/^#define FAST_J16_SLOW(e) (unlikely(code16) \&\& (e) >= FAST_J16_END)$/#define FAST_J16_SLOW(e) 0/' \
         incl:'s/else if (_op >= CC_AND) cpu->flags \&= ~CF;/else if (_op >= CC_AND) cpu->flags |= CF;/' \
         ea16ss:'s/^\t3 | 6 << 3 | 0x40, 3 | 7 << 3 | 0x40, 5 | 6 << 3 | 0xc0, 5 | 7 << 3 | 0xc0, 6, 7, 5 | 0x80, 3 };$/\t3 | 6 << 3 | 0x40, 3 | 7 << 3 | 0x40, 5 | 6 << 3 | 0xc0, 5 | 7 << 3 | 0xc0, 6, 7, 5, 3 };/' \
         cond2p:'s/case 5: cond = !__builtin_parity(d \& 0xff); break;/case 5: cond = __builtin_parity(d \& 0xff); break;/' \
         rmfold:'/^\t\telse if (!(cpu->cr0 \& CR0_PG) \&\& !(nullsegs/,/DISPATCH;/s/^\t\t\t\tNOFOLD_SET(); nofold_laddr = cpu->ifetch.laddr; nofold_xaddr = cpu->ifetch.xaddr;$/\t\t\t\t(void) 0;/' \
         c2ret:'s/if (\*p == 0xc3 || \*p == 0xc2 || \*p == 0xff)/if (*p == 0xc3 || *p == 0xff)/' \
         rmiomem:'s/if (po < (uword) cpu->phys_mem_size - 4096 \&\& !in_iomem(po) \&\& !in_iomem(po + 16)) {/if (po < (uword) cpu->phys_mem_size - 4096) {/' \
         rmend:'s/if (po < (uword) cpu->phys_mem_size - 4096 \&\& !in_iomem(po) \&\& !in_iomem(po + 16)) {/if (po < (uword) cpu->phys_mem_size \&\& !in_iomem(po) \&\& !in_iomem(po + 16)) {/'; do
    mname=${m%%:*}
    mfile=i386_fast.h       # (v5.1: lpgno's fault is in i386.c)
    [ "$mname" = lpgno ] && mfile=i386.c
    [ "$mname" = codepage ] && mfile=i386.c
    mkdir -p "$OUT/mutant-$mname"
    cp "$TREE/i386.c" "$TREE/i386.h" "$TREE/i386_fast.h" "$OUT/mutant-$mname/"
    sed "${m#*:}" "$TREE/$mfile" > "$OUT/mutant-$mname/$mfile"
    if cmp -s "$TREE/$mfile" "$OUT/mutant-$mname/$mfile"; then
        echo "    | (the $mname fault was not put in: the sed found nothing)"
        result 1 "the lockstep test catches a fault in the fast interpreter ($mname)"
        continue
    fi
    fcbuild "mutant-$mname" "$OUT/mutant-$mname" $PCBOARD
    "$OUT/test_fast_cpu-mutant-$mname" $FCSEEDS > "$OUT/fast-cpu-mutant-$mname.log" 2>&1
    st=$?
    nf=$(grep -c FAIL "$OUT/fast-cpu-mutant-$mname.log")
    nsc=$(sed -n 's/^test_fast_cpu: \([0-9]*\) scenarios.*/\1/p' "$OUT/fast-cpu-mutant-$mname.log")
    if [ -n "$nsc" ]; then
        echo "    | ($mname: $nf of $nsc scenarios differ; exit status $st)"
    else    # v5.1: a crash (a store through a stale entry) ends the run, and is its last FAIL
        echo "    | ($mname: $nf FAIL lines, the last a crash, which ends the run; exit status $st)"
    fi
    [ $st != 0 ]
    result $? "the lockstep test catches a fault in the fast interpreter ($mname)"
done
# v5.1 (WI-3): a fault in a copy of either file (i386.c or i386_fast.h),
# which the test has to catch: read_desc()'s one lookup without its test
# that the entry is in one page (rdpage), with MMIO taken for RAM
# (rdmmio), with RAM's end not tested (rdend); FS's null bit not kept when
# FS is loaded (nullfs); ROL and ROR swapped in the group 2 tables
# (grp2swap); ADC's entry on ADD in the group 1 tables (grp1op)
fcfault() {     # name file sed-expression
    local mname=$1 file=$2 d="$OUT/mutant-$1" st nf
    mkdir -p "$d"
    cp "$TREE/i386.c" "$TREE/i386.h" "$TREE/i386_fast.h" "$d/"
    sed "$3" "$TREE/$file" > "$d/$file"
    if cmp -s "$TREE/$file" "$d/$file"; then
        echo "    | (the $mname fault was not put in: the sed found nothing)"
        result 1 "the lockstep test catches a fault in $file ($mname)"
        return
    fi
    fcbuild "mutant-$mname" "$d" $PCBOARD
    "$OUT/test_fast_cpu-mutant-$mname" $FCSEEDS > "$OUT/fast-cpu-mutant-$mname.log" 2>&1
    st=$?
    nf=$(grep -c FAIL "$OUT/fast-cpu-mutant-$mname.log")
    echo "    | ($mname: $nf FAIL lines; exit status $st)"
    [ $st != 0 ]
    result $? "the lockstep test catches a fault in $file ($mname)"
}
fcfault rdpage i386.c 's/if (w1 \&\& (la \& 0xfff) <= 0x1000 - 8) {/if (w1) {/'
fcfault rdmmio i386.c 's/if (!in_iomem(pa) \&\& pa <= /if (pa <= /'
fcfault rdend i386.c 's/ \&\& pa <= (uword) cpu->phys_mem_size - 8) {/) {/'
fcfault nullfs i386_fast.h 's/nullsegs = (nullsegs \& ~(1u << (sg)))/if ((sg) != SEG_FS) &/'
# v5.2: fast_load_seg() (QW_SEG) without each of its ways out to set_seg()
# (CHECK-qw section 5): its limit test (seglimit), the limit one byte
# short (segoff1), its page test (segpage), its look at the TLB entry's
# rights (segrights), its MMIO test (segmmio) and RAM's end (segend), each
# caught by mode o's cases by hand
fcfault seglimit i386_fast.h 's/^\tif (off + 7 > limit || (la \& 0xfff) > 0x1000 - 8)$/\tif ((la \& 0xfff) > 0x1000 - 8)/'
fcfault segoff1 i386_fast.h 's/^\tif (off + 7 > limit || (la \& 0xfff) > 0x1000 - 8)$/\tif (off + 6 > limit || (la \& 0xfff) > 0x1000 - 8)/'
fcfault segpage i386_fast.h 's/^\tif (off + 7 > limit || (la \& 0xfff) > 0x1000 - 8)$/\tif (off + 7 > limit)/'
fcfault segrights i386_fast.h 's/^\t\tif (ent->lpgno != la >> 12 || ent->pte_lookup\[0\]\[0\])$/\t\tif (ent->lpgno != la >> 12)/'
fcfault segmmio i386_fast.h 's/^\tif (in_iomem(pa) || pa > (uword) cpu->phys_mem_size - 8)$/\tif (pa > (uword) cpu->phys_mem_size - 8)/'
fcfault segend i386_fast.h 's/^\tif (in_iomem(pa) || pa > (uword) cpu->phys_mem_size - 8)$/\tif (in_iomem(pa))/'
fcfault grp2swap i386_fast.h 's/^#define FAST_GRP2_TAB(P) { \&\&P##0, \&\&P##1,/#define FAST_GRP2_TAB(P) { \&\&P##1, \&\&P##0,/'
fcfault grp1op i386_fast.h 's/^\(#define FAST_GRP1_TAB(P) { \&\&P##0, \&\&P##1, \)\&\&P##2,/\1\&\&P##0,/'

# v5.1 (WI-4, the lean TLB refill): four faults in the fast interpreter's own
# refill, put into i386.c's lean copies of the walk and the rights check
# (TLB_WALK and LPGNO_RIGHTS with LEAN 1) and into fast_translate(), which
# mode n has to catch: the directory entry's accessed bit set after the table
# entry is loaded, the fast interpreter's two entries at the index left by
# the walk, no dirty bit on a store that refills, and tiny386's entry kept
# after a rights fault.  The first and the last only mode n's probes of
# fast_miss() can catch: wherever guest code meets them tiny386 does the
# faulting instruction again, and sets what was left unset.
for m in pdeacc:'s/^\tmem\[base_addr + i \* 4\] |= 1 << 5; \/\* accessed \*\/ \\$/\tif (!(LEAN)) mem[base_addr + i * 4] |= 1 << 5; \\/;s/^\(\tuword pte = (LEAN) ? ldu32.*\)$/\1\n\tif (LEAN) mem[base_addr + i * 4] |= 1 << 5; \\/' \
         slot:'s/^\t\tFAST_TLB_SLOT_LEAN(cpu, lpgno % tlb_size); \\$/\t\t(void) 0; \\/' \
         dirty:'/^static inline __attribute__((always_inline)) bool fast_translate(/,/^}/{s/^\tif (ent->lpgno != lpgno) {$/\tint refilled = ent->lpgno != lpgno;\n\tif (refilled) {/;s/^\treturn lpgno_rights_lean(\(.*\));$/\tu8 d0 = **ppte \& 0x40;\n\tbool r = lpgno_rights_lean(\1);\n\tif (r \&\& write \&\& refilled \&\& !d0)\n\t\t**ppte \&= ~0x40;\n\treturn r;/}' \
         nodrop:'s/^\t\tent->lpgno = -1; \\$/\t\tif (!(LEAN)) ent->lpgno = -1; \\/'; do
    mname=${m%%:*}
    mkdir -p "$OUT/mutant-$mname"
    cp "$TREE/i386_fast.h" "$TREE/i386.h" "$OUT/mutant-$mname/"
    sed "${m#*:}" "$TREE/i386.c" > "$OUT/mutant-$mname/i386.c"
    if cmp -s "$TREE/i386.c" "$OUT/mutant-$mname/i386.c"; then
        echo "    | (the $mname fault was not put in: the sed found nothing)"
        result 1 "the lockstep test catches a fault in the fast interpreter's own TLB refill ($mname)"
        continue
    fi
    fcbuild "mutant-$mname" "$OUT/mutant-$mname" $PCBOARD
    "$OUT/test_fast_cpu-mutant-$mname" $FCSEEDS 1 n > "$OUT/fast-cpu-mutant-$mname.log" 2>&1
    st=$?
    nf=$(grep -c FAIL "$OUT/fast-cpu-mutant-$mname.log")
    echo "    | ($mname: $nf FAIL lines in $FCSEEDS seeds of mode n; exit status $st)"
    [ $st != 0 ]
    result $? "the lockstep test catches a fault in the fast interpreter's own TLB refill ($mname)"
done

# ==== 24b. the same, as the board's own compiler makes it (v5.1, optional) ====
# test_fast_cpu.c (and with it i386.c and i386_fast.h) compiled by the
# board's compiler as the Korvo-1 builds i386.c (ESP-IDF's GCC, -O3
# -mtune=esp32s31, -fno-jump-tables, the board header), linked with a
# riscv32 Linux C library, and run as RV32 code under qemu-riscv32: the
# only run that compares the fast interpreter as the board's compiler
# makes it with tiny386 at every step (the v5.1 CPU review's T3 and T5).
# Only when ESP_GCC (riscv32-esp-elf-gcc) and RV32_GCC (a riscv32 Linux
# gcc, whose C library headers and libraries are used) are set and
# qemu-riscv32-static is on PATH; FCSEEDS_RV seeds (default FCSEEDS), split
# over up to 8 processes.
echo
if [ -n "${ESP_GCC:-}" ] && [ -n "${RV32_GCC:-}" ] && command -v qemu-riscv32-static > /dev/null; then
    RVMARCH="-march=rv32imac_zba_zbb_zbs -mabi=ilp32"
    RVSYS=$("$RV32_GCC" -print-sysroot)
    RVSEEDS=${FCSEEDS_RV:-$FCSEEDS}
    if "$ESP_GCC" $RVMARCH -mtune=esp32s31 -nostdinc -isystem "$RVSYS/usr/include" \
            -isystem "$("$ESP_GCC" -print-file-name=include)" -O3 -w -g -ffunction-sections \
            -fdata-sections -fno-jump-tables -fno-tree-switch-conversion -fstrict-volatile-bitfields \
            -std=gnu2x -D_GNU_SOURCE $PCBOARD -I"$TREE" -c "$HERE/cpu/test_fast_cpu.c" \
            -o "$OUT/test_fast_cpu-rv.o" &&
        "$RV32_GCC" $RVMARCH -O2 -w $PCBOARD -I"$TREE" -c "$TREE/fpu.c" -o "$OUT/fpu-rv.o" &&
        "$RV32_GCC" $RVMARCH -static -o "$OUT/test_fast_cpu-rv" "$OUT/test_fast_cpu-rv.o" \
            "$OUT/fpu-rv.o" -lm -latomic; then
        echo "    | (built by $("$ESP_GCC" --version | head -1))"
        per=$(( (RVSEEDS + 7) / 8 ))
        st=0
        for i in 0 1 2 3 4 5 6 7; do
            f=$((1 + i * per)); n=$per
            [ $((f + n)) -gt $((1 + RVSEEDS)) ] && n=$((1 + RVSEEDS - f))
            [ $n -le 0 ] && continue
            qemu-riscv32-static -cpu rv32,zba=true,zbb=true,zbs=true "$OUT/test_fast_cpu-rv" $n $f \
                > "$OUT/fast-cpu-rv-$i.log" 2>&1 &
        done
        for j in $(jobs -p); do wait $j || st=1; done
        grep -h "FAIL" "$OUT"/fast-cpu-rv-*.log | head -20
        cat "$OUT"/fast-cpu-rv-*.log | awk '/^test_fast_cpu:/ { s += $2; f += $(NF - 1) } END { printf "    | (%d scenarios, %d failed, under qemu-riscv32)\n", s, f }'
        result $st "the fast interpreter as the board's compiler makes it (RV32 under qemu): the same as tiny386's own at every step"
    else
        result 1 "the fast interpreter as the board's compiler makes it: the build"
    fi
else
    echo "    | (no ESP_GCC and RV32_GCC, or no qemu-riscv32-static: the board compiler's run is skipped)"
fi

# ==== 24b. REP string instructions across a 16-bit wrap (v5.2) ====
# tests/host/cpu/test_rep_wrap.c with the guest in guest_rep_wrap.S: REP
# STOS, MOVS, INS and OUTS with 16-bit addresses, each segment's base off a
# page boundary, wrap SI and DI at 64 KiB as QEMU does, forwards and
# backwards, with tiny386's interpreter and the fast one, as the Korvo-1
# builds i386.c and without the board header.  Then a copy of i386.c with
# the fix taken out (wrap16_cap() never ending a run), which has to run on
# past FFFFh as v5.1 did.
echo
if command -v as > /dev/null && command -v ld > /dev/null &&
        as --32 -o "$OUT/guest_rep_wrap.o" "$HERE/cpu/guest_rep_wrap.S" 2> "$OUT/rep-wrap-as.log" &&
        ld -m elf_i386 -Ttext=0x1000 -e _start --oformat binary \
            -o "$OUT/guest_rep_wrap.bin" "$OUT/guest_rep_wrap.o"; then
    rwbuild() {     # name dir-with-i386.c-and-i386_fast.h [flags]: OUT/test_rep_wrap-name
        local name=$1 dir=$2
        shift 2
        $CC -O2 -g -w -I"$dir" -I"$TREE" "$@" -o "$OUT/test_rep_wrap-$name" \
            "$HERE/cpu/test_rep_wrap.c" "$TREE/fpu.c" -lm || exit 1
    }
    rwbuild korvo1 "$TREE" $PCBOARD
    "$OUT/test_rep_wrap-korvo1" "$OUT/guest_rep_wrap.bin" > "$OUT/rep-wrap-korvo1.log" 2>&1
    st=$?
    grep "FAIL\|^test_rep_wrap" "$OUT/rep-wrap-korvo1.log"
    result $st "REP STOS, MOVS, INS and OUTS with 16-bit addresses wrap at 64 KiB as QEMU does, in both interpreters as the Korvo-1 builds them"
    rwbuild plain "$TREE" -DI386_ALIGNED_ONLY -DI386_ENABLE_FPU
    "$OUT/test_rep_wrap-plain" "$OUT/guest_rep_wrap.bin" > "$OUT/rep-wrap-plain.log" 2>&1
    st=$?
    tail -1 "$OUT/rep-wrap-plain.log" | sed 's/^/    | /'
    result $st "the same without the board header"
    mkdir -p "$OUT/rep-wrap-off"
    sed 's/^\t\tif (ABIT == 16) { \\$/\t\tif (0) { \\/' "$TREE/i386.c" > "$OUT/rep-wrap-off/i386.c"
    cp "$TREE/i386_fast.h" "$OUT/rep-wrap-off/"
    nl=$(diff "$TREE/i386.c" "$OUT/rep-wrap-off/i386.c" | grep -c '^>')
    rwbuild off "$OUT/rep-wrap-off" $PCBOARD
    "$OUT/test_rep_wrap-off" "$OUT/guest_rep_wrap.bin" before > "$OUT/rep-wrap-off.log" 2>&1
    st=$?
    grep "FAIL\|^test_rep_wrap" "$OUT/rep-wrap-off.log" | sed 's/^/    | /'
    [ "$nl" = 1 ] && [ $st = 0 ]
    result $? "the wrap test sees runs that go on past FFFFh (i386.c with the fix taken out: $nl line)"
else
    sed 's/^/    | /' "$OUT/rep-wrap-as.log" 2> /dev/null
    echo "    | (no 32-bit as and ld: the wrap test is skipped)"
fi

# ==== 24c. CMPXCHG's flags (v5.2) ====
# tests/host/cpu/test_cmpxchg.c with the guest in guest_cmpxchg.S: CMPXCHG
# sets the flags as CMP accumulator, destination does, as QEMU, at 8, 16
# and 32 bits, with a register and with memory, with tiny386's interpreter
# and the fast one (which hands it to tiny386's), as the Korvo-1 builds
# i386.c and without the board header.  Then a copy of i386.c with the old
# subtraction (destination less accumulator), which the test has to see.
echo
if command -v as > /dev/null && command -v ld > /dev/null &&
        as --32 -o "$OUT/guest_cmpxchg.o" "$HERE/cpu/guest_cmpxchg.S" 2> "$OUT/cmpxchg-as.log" &&
        ld -m elf_i386 -Ttext=0x1000 -e _start --oformat binary \
            -o "$OUT/guest_cmpxchg.bin" "$OUT/guest_cmpxchg.o"; then
    cxbuild() {     # name dir-with-i386.c-and-i386_fast.h [flags]: OUT/test_cmpxchg-name
        local name=$1 dir=$2
        shift 2
        $CC -O2 -g -w -I"$dir" -I"$TREE" "$@" -o "$OUT/test_cmpxchg-$name" \
            "$HERE/cpu/test_cmpxchg.c" "$TREE/fpu.c" -lm || exit 1
    }
    cxbuild korvo1 "$TREE" $PCBOARD
    "$OUT/test_cmpxchg-korvo1" "$OUT/guest_cmpxchg.bin" > "$OUT/cmpxchg-korvo1.log" 2>&1
    st=$?
    grep "FAIL\|^test_cmpxchg" "$OUT/cmpxchg-korvo1.log"
    result $st "CMPXCHG sets the flags as CMP accumulator, destination, as QEMU, in both interpreters as the Korvo-1 builds them"
    cxbuild plain "$TREE" -DI386_ALIGNED_ONLY -DI386_ENABLE_FPU
    "$OUT/test_cmpxchg-plain" "$OUT/guest_cmpxchg.bin" > "$OUT/cmpxchg-plain.log" 2>&1
    st=$?
    tail -1 "$OUT/cmpxchg-plain.log" | sed 's/^/    | /'
    result $st "the same without the board header"
    mkdir -p "$OUT/cmpxchg-old"
    sed -e '/^#define CMPXCH_helper/,/cc\.src2); $/{s/src1 = sext ## BIT(lreg ## BIT(0))/src1 = sext ## BIT(la(a))/;s/src2 = sext ## BIT(la(a))/src2 = sext ## BIT(lreg ## BIT(0))/;s/sreg ## BIT(0, cpu->cc\.src2); $/sreg ## BIT(0, cpu->cc.src1); /}' \
        "$TREE/i386.c" > "$OUT/cmpxchg-old/i386.c"
    cp "$TREE/i386_fast.h" "$OUT/cmpxchg-old/"
    nl=$(diff "$TREE/i386.c" "$OUT/cmpxchg-old/i386.c" | grep -c '^>')
    cxbuild old "$OUT/cmpxchg-old" $PCBOARD
    "$OUT/test_cmpxchg-old" "$OUT/guest_cmpxchg.bin" before > "$OUT/cmpxchg-old.log" 2>&1
    st=$?
    grep "FAIL\|^test_cmpxchg" "$OUT/cmpxchg-old.log" | sed 's/^/    | /'
    [ "$nl" = 3 ] && [ $st = 0 ]
    result $? "the CMPXCHG test sees the old subtraction (i386.c with it put back: $nl lines)"
else
    sed 's/^/    | /' "$OUT/cmpxchg-as.log" 2> /dev/null
    echo "    | (no 32-bit as and ld: the CMPXCHG test is skipped)"
fi

# ==== 24d. tiny386's REP MOVS and STOS by pieces against element by element (v5.3) ====
# QW_REPMIS does misaligned REP MOVS and REP STOS in tiny386's own string
# loops by pieces, where they went an element at a time, each element's ends
# translated again.  Both of test_fast_cpu's machines run those loops, so it
# cannot see a change there: here it is built from the tree and from a copy
# with QW_REPMIS 0, each writes a hash of both machines' state after every
# step and of their memory at each scenario's end (TFC_DUMP), and the two
# must be the same (mode z has the misaligned REPs across page ends, some
# into a page not yet present, and since the review of batch 4 with DS and
# ES off a page boundary, where a 16-bit SI or DI wraps inside a page).
# Then a fault in the pieces' count (repmis), and the count without its
# stop at the 16-bit wrap (repmisw16), each of which has to make them
# differ.
echo
repmis_run() {  # name dir: OUT/repmis-name.dump from the seeds in 8 pieces
    local name=$1 dir=$2 st=0
    fcbuild "repmis-$name" "$dir" $PCBOARD
    local per=$(( (FCSEEDS + 7) / 8 )) pids=""
    for i in 0 1 2 3 4 5 6 7; do
        local f=$((1 + i * per)) n=$per
        [ $((f + n)) -gt $((1 + FCSEEDS)) ] && n=$((1 + FCSEEDS - f))
        [ $n -le 0 ] && continue
        TFC_DUMP="$OUT/repmis-$name-$i.dump" "$OUT/test_fast_cpu-repmis-$name" $n $f > "$OUT/repmis-$name-$i.log" 2>&1 &
        pids="$pids $!"
    done
    for p in $pids; do wait $p || st=1; done
    cat "$OUT"/repmis-$name-[0-7].dump > "$OUT/repmis-$name.dump"
    rm -f "$OUT"/repmis-$name-[0-7].dump
    return $st
}
for v in off bad w16; do
    mkdir -p "$OUT/repmis-$v"
    cp "$TREE/i386.c" "$TREE/i386.h" "$TREE/i386_fast.h" "$TREE/qw.h" "$TREE/qw-config.h" "$OUT/repmis-$v/"
done
sed -i 's/^#define QW_REPMIS 1$/#define QW_REPMIS 0/' "$OUT/repmis-off/qw-config.h"
sed -i 's/^\tc = dir > 0 ? (4096 - (a1 \& 4095)) \/ n : 1 + (a1 \& 4095) \/ n;$/\tc = dir > 0 ? (4096 - (a1 \& 4095)) \/ n + 1 : 1 + (a1 \& 4095) \/ n;/' "$OUT/repmis-bad/i386.c"
sed -i '/^static uword repmis_count(/,/^}/s/^\tif (a16) {$/\tif (0) {/' "$OUT/repmis-w16/i386.c"
st=0
grep -q '^#define QW_REPMIS 0$' "$OUT/repmis-off/qw-config.h" || st=1
cmp -s "$TREE/i386.c" "$OUT/repmis-bad/i386.c" && st=1
repmis_run on "$TREE" || st=1
repmis_run off "$OUT/repmis-off" || st=1
repmis_run bad "$OUT/repmis-bad"
repmis_run w16 "$OUT/repmis-w16"
echo "    | ($(wc -l < "$OUT/repmis-on.dump") steps and scenario ends compared)"
cmp -s "$OUT/repmis-on.dump" "$OUT/repmis-off.dump" || st=1
result $st "tiny386's REP MOVS and STOS by pieces (QW_REPMIS): the same at every step as element by element"
if cmp -s "$OUT/repmis-on.dump" "$OUT/repmis-bad.dump"; then st=1; else st=0; fi
echo "    | (repmis: $(diff "$OUT/repmis-on.dump" "$OUT/repmis-bad.dump" | grep -c '^<') lines differ)"
result $st "the step by step comparison catches a fault in the pieces' count (repmis)"
if cmp -s "$OUT/repmis-on.dump" "$OUT/repmis-w16.dump"; then st=1; else st=0; fi
cmp -s "$TREE/i386.c" "$OUT/repmis-w16/i386.c" && st=1
echo "    | (repmisw16: $(diff "$OUT/repmis-on.dump" "$OUT/repmis-w16.dump" | grep -c '^<') lines differ)"
result $st "the step by step comparison catches the pieces without their stop at the 16-bit wrap (repmisw16)"

# ==== 25. the profiler's counts and PROFILE.TXT (v5.1, s31/PROFILE.md) ====
# esp/main/prof_hist.c (the tables, the instruction key, the file) and
# prof_cpu.h (the guest's state read from a CPU's fields) as the board
# builds them, on the PC: 64-bit, and 32-bit with plain char unsigned as on
# RV32.  Then s31/profile/profile_report.py on a PROFILE.TXT whose host PCs
# are in the test program's own functions: the report must name them with
# their shares, and refuse an ELF that is not the one the file names
# (python3 needed; addr2line too for the source lines).
echo
$CC $CFLAGS -DPROF_HOST -I"$TREE/esp/main" -no-pie -o "$OUT/test_prof_hist" \
    "$HERE/profile/test_prof_hist.c" "$TREE/esp/main/prof_hist.c" || exit 1
"$OUT/test_prof_hist" --time
result $? "prof_hist.c and prof_cpu.h: the tables, the guest's and the instruction's keys, PROFILE.TXT, the reader"
if $CC -m32 -funsigned-char $CFLAGS -DPROF_HOST -I"$TREE/esp/main" -no-pie \
    -o "$OUT/test_prof_hist32" "$HERE/profile/test_prof_hist.c" "$TREE/esp/main/prof_hist.c" \
    2> "$OUT/test_prof_hist32.log"; then
    "$OUT/test_prof_hist32" | tail -1
    result $? "prof_hist.c and prof_cpu.h, 32-bit with unsigned char (as on RV32): the same"
else
    echo "    | (no 32-bit gcc here: the 32-bit build is skipped)"
fi
if command -v python3 > /dev/null; then
    sha=$(sha256sum "$OUT/test_prof_hist" | cut -d' ' -f1)
    "$OUT/test_prof_hist" --demo "$OUT/DEMO-PROFILE.TXT" "$sha" &&
    python3 "$TREE/s31/profile/profile_report.py" "$OUT/DEMO-PROFILE.TXT" "$OUT/test_prof_hist" \
        -o "$OUT/demo-report.txt" &&
    grep -Eq '^demo_hot +75\.0% \+-2\.7 +750 ' "$OUT/demo-report.txt" &&
    grep -Eq '^demo_warm +25\.0% \+-2\.7 +250 ' "$OUT/demo-report.txt" &&
    grep -Eq '^EE +OUT DX,AL +v86 +exec1 +100\.0%' "$OUT/demo-report.txt" &&
    grep -Eq '^v86 +3 +1234 +00012000 +25\.0%' "$OUT/demo-report.txt" &&
    ! python3 "$TREE/s31/profile/profile_report.py" "$OUT/DEMO-PROFILE.TXT" "$OUT/test_bootmenu" \
        > /dev/null 2>&1
    result $? "profile_report.py: the functions, the instruction and the guest's page with their shares; another ELF refused"
    # the stretch between two files (Ctrl+Alt+F8's PROFnn.TXT): 250 hot, 250 warm
    python3 "$TREE/s31/profile/profile_report.py" "$OUT/DEMO-PROFILE.TXT" "$OUT/test_prof_hist" \
        --since "$OUT/DEMO-PROFILE.TXT.early" -o "$OUT/demo-since.txt" &&
    grep -q 'A stretch of the run (--since): from file 2 (key) to file 3 (end)' "$OUT/demo-since.txt" &&
    grep -Eq '^demo_hot +50\.0% \+-4\.4 +250 ' "$OUT/demo-since.txt" &&
    grep -Eq '^demo_warm +50\.0% \+-4\.4 +250 ' "$OUT/demo-since.txt" &&
    ! python3 "$TREE/s31/profile/profile_report.py" "$OUT/DEMO-PROFILE.TXT.early" "$OUT/test_prof_hist" \
        --since "$OUT/DEMO-PROFILE.TXT" > /dev/null 2>&1
    result $? "profile_report.py --since: the stretch between two files alone; the files the wrong way round refused"
    # v5.1, after the sampler review (R1, R3, R8): the ELF named by its first
    # 9 hex digits (as ESP-IDF's esp_app_get_elf_sha256() gives them, which
    # v5.1's first images wrote) is taken, 9 digits of another ELF and 7
    # digits are refused; a file cut short anywhere (every 7th byte) is read
    # as far as it goes, with a warning, never a Python traceback; and
    # --since refuses a file of another boot (another run word)
    sed 's/^\(elf_sha256 .........\).*$/\1/' "$OUT/DEMO-PROFILE.TXT" > "$OUT/DEMO9.TXT"
    sed 's/^\(elf_sha256 .......\).*$/\1/' "$OUT/DEMO-PROFILE.TXT" > "$OUT/DEMO7.TXT"
    python3 "$TREE/s31/profile/profile_report.py" "$OUT/DEMO9.TXT" "$OUT/test_prof_hist" \
        -o "$OUT/demo9-report.txt" &&
    grep -q "matches the file's 9 hex digits" "$OUT/demo9-report.txt" &&
    grep -Eq '^demo_hot +75\.0% \+-2\.7 +750 ' "$OUT/demo9-report.txt" &&
    ! python3 "$TREE/s31/profile/profile_report.py" "$OUT/DEMO9.TXT" "$OUT/test_bootmenu" > /dev/null 2>&1 &&
    ! python3 "$TREE/s31/profile/profile_report.py" "$OUT/DEMO7.TXT" "$OUT/test_prof_hist" > /dev/null 2>&1
    result $? "profile_report.py: the ELF's first 9 hex digits, as the board's ESP-IDF call gives them, taken; another ELF's and 7 digits refused"
    sz=$(stat -c %s "$OUT/DEMO-PROFILE.TXT")
    tables=$(grep -b '^\[host\]' "$OUT/DEMO-PROFILE.TXT" | cut -d: -f1)
    cutbad=0; ncut=0; nwarn=0
    for c in $(seq 20 7 $((sz - 1))); do
        head -c $c "$OUT/DEMO-PROFILE.TXT" > "$OUT/CUT.TXT"
        python3 "$TREE/s31/profile/profile_report.py" "$OUT/CUT.TXT" "$OUT/test_prof_hist" \
            -o "$OUT/cut-report.txt" > /dev/null 2> "$OUT/cut.err"
        st=$?
        ncut=$((ncut + 1))
        if grep -q Traceback "$OUT/cut.err"; then
            echo "    | (cut at byte $c: a traceback)"; cutbad=1
        elif [ "$c" -gt "$tables" ]; then
            if [ $st = 0 ] && grep -q 'WARNING: the file has no "end" line' "$OUT/cut-report.txt"; then
                nwarn=$((nwarn + 1))
            else
                echo "    | (cut at byte $c, in the tables: exit $st, or no warning)"; cutbad=1
            fi
        fi
    done
    echo "    | ($ncut cuts, $nwarn of them in the tables read with the warning; no traceback)"
    [ $cutbad = 0 ]
    result $? "profile_report.py: a file cut short is read as far as it goes, with a warning"
    sed 's/^run .*$/run 00000001/' "$OUT/DEMO-PROFILE.TXT.early" > "$OUT/DEMO-PROFILE.TXT.otherrun"
    ! python3 "$TREE/s31/profile/profile_report.py" "$OUT/DEMO-PROFILE.TXT" "$OUT/test_prof_hist" \
        --since "$OUT/DEMO-PROFILE.TXT.otherrun" > /dev/null 2>&1 &&
    grep -q '^run 5eed0b07$' "$OUT/DEMO-PROFILE.TXT.early"
    result $? "profile_report.py --since: a file of another boot (another run word) refused"
else
    echo "    | (no python3 here: profile_report.py is not tried)"
fi

# ==== 26. the NE2000's receive ring and the ESP32 filter (v5.3): tests/host/ne2000 ====
# ne2000.c driven through its ports as a guest's driver drives it, built as
# the headless build has it and with BUILD_ESP32, under AddressSanitizer and
# UBSan: frames read back whole whether the guest keeps up or falls behind;
# no endless loop and nothing written past the card's memory for any ring
# registers, nor for one written while a frame is copied in; the ESP32
# filter's port read after the IP options, and only from a first fragment.
# Then upstream's ne2000.c at a080c5d, where the test has to see the loops,
# the writes, the frames lost and the frames sent the wrong way.  Needs
# AddressSanitizer and setarch; without them it says SKIP.
echo
if bash "$HERE/ne2000/run_ne2000.sh" "$OUT/ne2000" > "$OUT/ne2000.log" 2>&1; then
    st=0
else
    st=1
fi
if grep -q "^SKIP" "$OUT/ne2000.log"; then
    grep "^SKIP" "$OUT/ne2000.log"
else
    grep -E "FAILED|^ne2000:|^  ok:" "$OUT/ne2000.log"
    result $st "ne2000.c: the receive ring with any registers, frames whole when the guest falls behind, the ESP32 filter after IP options"
fi

# ==== 27. core 1's clock from its cycle counter (v5.3, QW_UCLK): esp/main/uclock.h ====
# uclock.h against a pretend core (test_uclock.c): a cycle counter, a
# systimer whose read latches part way through, waits in which the counter
# runs or stops, interrupts that read the clock or set the pair between
# any two of a read's loads, a tick every 10 ms, long stretches with no
# read, both counters across their wraps, and counters that run fast or
# slow.  No value below one read before it, none ahead of the true clock
# or more than 4 us behind it, the reciprocal exact over the span, and the
# true read for good with a counter that does not keep time.  Then each
# planted fault has to fail.
echo
$CC $CFLAGS -I"$TREE/esp/main" -o "$OUT/test_uclock" "$HERE/test_uclock.c" || exit 1
"$OUT/test_uclock"
result $? "uclock.h: core 1's clock never back, never ahead, under 4 us behind, through waits, interrupts and wraps"
for f in NOGEN ORDER NOIDLE NOTICK NORATE SPAN; do
    $CC $CFLAGS -DUCLK_FAULT_$f -I"$TREE/esp/main" -o "$OUT/test_uclock_$f" "$HERE/test_uclock.c" || exit 1
    "$OUT/test_uclock_$f" > "$OUT/uclock-$f.log" 2>&1
    st=$?
    echo "    | planted fault $f: exit status $st, $(grep -c 'FAIL' "$OUT/uclock-$f.log") failure lines"
    [ $st != 0 ]
    result $? "the clock test sees planted fault $f"
done

echo
echo "host tests: $pass passed, $fail failed"
[ $fail = 0 ]
