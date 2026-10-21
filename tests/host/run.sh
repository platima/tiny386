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

# ==== 5. HID report descriptors (FIRMWARE-PLAN 3.3) ====
echo
$CC $CFLAGS -I"$TREE/esp/main" -o "$OUT/test_hid_parse" "$HERE/test_hid_parse.c" \
    "$TREE/esp/main/hid_parse.c" || exit 1
"$OUT/test_hid_parse"
result $? "hid_parse.c: boot keyboard, wheel mouse, receiver with report IDs, NKRO, vendor, tablet"

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

echo
echo "host tests: $pass passed, $fail failed"
[ $fail = 0 ]
