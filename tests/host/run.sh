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
echo
$CC $CFLAGS -DCONSOLE_HOST -I"$TREE/esp/main" \
    -o "$OUT/test_console" "$HERE/test_console.c" "$TREE/esp/main/console.c" \
    "$TREE/vgafont.c" || exit 1
"$OUT/test_console" "$OUT"
result $? "boot console: text, colours, scrolling, ring, hand-over, crash screen"
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
if git -C "$TREE" rev-parse -q --verify v3 >/dev/null; then
    git -C "$TREE" show v3:i8042.c > "$OUT/i8042-v3.c"
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
if git -C "$TREE" rev-parse -q --verify v3.1 >/dev/null; then
    git -C "$TREE" show v3.1:esp/main/usb_input.c > "$OUT/usb_input-v3.1.c"
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

# ==== 15. the status LED (v3.3) ====
# status_code.c as the firmware has it, and status_bb.h (the panic handler's
# bit-bang) with a pretend cycle counter: the RMT frame and every bit-banged
# edge against both LEDs' timing, the colours and blinks, the error-code
# state machine, the replay after a reset, the beat watch, the record.
echo
$CC $CFLAGS -DSTATUS_HOST -I"$TREE/esp/main" -o "$OUT/test_status" \
    "$HERE/test_status.c" "$TREE/esp/main/status_code.c" || exit 1
"$OUT/test_status"
result $? "status LED: RMT and bit-banged timing for both LEDs, patterns, state machine, replay, beat watch, record"

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

echo
echo "host tests: $pass passed, $fail failed"
[ $fail = 0 ]
