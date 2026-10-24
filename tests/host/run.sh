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
# empty when the tree has neither
tagref() {
    local t
    for t in "$1" "s31-$1"; do
        if git -C "$TREE" rev-parse -q --verify "$t^{commit}" >/dev/null 2>&1; then
            echo "$t"
            return
        fi
    done
}

# ==== 1. the PS/2 queue's lock (FIRMWARE-PLAN 3.3.1) ====
# i8042.c as it is now must pass; i8042.c as tiny386 released it (the
# pristine tag) must fail, which shows that the test sees the bug.
$CC $CFLAGS -Wno-unused-function -DBUILD_ESP32 -I"$HERE/shim" -I"$TREE" \
    -o "$OUT/test_ps2_queue" "$HERE/test_ps2_queue.c" "$TREE/i8042.c" -lpthread || exit 1
"$OUT/test_ps2_queue"
result $? "ps2 queue lock, i8042.c of this tree"

if git -C "$TREE" rev-parse -q --verify pristine >/dev/null; then
    git -C "$TREE" show pristine:i8042.c > "$OUT/i8042-pristine.c"
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
             vga_set_dirty_blocks vga_get_info; do
        printf ' -D%s=%s%s' "$s" "$p" "$s"
    done
}
printf '#undef VGA_DIRTY_BLOCKS\n' > "$OUT/no_dirty_blocks.h"
VBE_BASE=
if git -C "$TREE" rev-parse -q --verify v4.0-base >/dev/null; then
    mkdir -p "$OUT/v40base"
    git -C "$TREE" show v4.0-base:vga.c > "$OUT/v40base/vga.c"
    git -C "$TREE" show v4.0-base:vga.h > "$OUT/v40base/vga.h"
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
        $CC -O2 -g -w -c -I"$OUT/v41" -I"$HERE/shim" -I"$TREE" -include "$hdr" \
            -o "$OUT/clear-v41-$board.o" "$OUT/v41/vga.c" || exit 1
        $CC -o "$OUT/test_clear_v41-$board" "$OUT/test_clear-$board.o" "$OUT/clear-v41-$board.o" \
            "$OUT/clear-ref-$board.o" "$OUT/vgafont.o" || exit 1
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
    "$TREE/esp/main/setup.c" "$TREE/ini.c" || exit 1
"$OUT/test_setup" "$OUT" "$HERE/data/linux-route"
result $? "setup.c: ini search, the menu's list and default, lastboot.txt, paths of the Linux route's inis"

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
echo
$CC -O1 -g -w -DEMULINK_CHECKED -I"$TREE" -c -o "$OUT/misc-checked.o" "$TREE/misc.c" || exit 1
$CC $CFLAGS -DEMULINK_CHECKED -I"$TREE" -o "$OUT/test_floppy" "$HERE/test_floppy.c" \
    "$OUT/misc-checked.o" || exit 1
"$OUT/test_floppy" "$OUT"
result $? "misc.c: floppy drives 0 and 1 only, nothing past the image's end, refused writes counted"

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
if git -C "$TREE" rev-parse -q --verify pristine >/dev/null; then
    git -C "$TREE" show pristine:i8254.c > "$OUT/i8254-pristine.c"
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

echo
echo "host tests: $pass passed, $fail failed"
[ $fail = 0 ]
