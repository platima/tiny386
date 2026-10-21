#!/bin/bash
# Host tests for the DOS board's firmware files (FIRMWARE-PLAN 5): the MS7210
# driver against encoder.md 8.4, the CH9350L parser through tiny386's own
# i8042.c, and AUDIO_RATE.  Builds with the host's cc; touches nothing
# outside OUTDIR.
#
#   bash hosttest/dosboard/run.sh OUTDIR [ENCODER_MD]
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
T=$(cd "$HERE/../.." && pwd)
O=${1:?give an output directory}
MD=${2:-/mnt/d/Repos/esp32-s31-linux-release/tiny386-hdmi-board/research/encoder.md}
mkdir -p "$O"
CC=${CC:-cc}
CFLAGS="-O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare"
rc=0

echo "== compiler: $($CC --version | head -1)"

echo "== ms7210_test"
$CC $CFLAGS -I"$T/esp/main" -o "$O/ms7210_test" \
    "$HERE/ms7210_test.c" "$T/esp/main/ms7210.c" || exit 2
"$O/ms7210_test" "$O" > "$O/ms7210_test.txt" 2>&1 || rc=1
cat "$O/ms7210_test.txt"

echo "== check_ms7210_table.py (HDMI scenario, phase 0)"
python3 "$HERE/check_ms7210_table.py" "$MD" "$O/ms7210_hdmi.log" --phase 0 \
    > "$O/check_table.txt" 2>&1 || rc=1
cat "$O/check_table.txt"

echo "== ch9350_test"
$CC $CFLAGS -Wno-unused-function -Wno-implicit-fallthrough -Wno-missing-field-initializers \
    -I"$T/esp/main" -I"$T" -o "$O/ch9350_test" \
    "$HERE/ch9350_test.c" "$T/esp/main/ch9350.c" "$T/esp/main/ch9350_kbd.c" \
    "$T/esp/main/usb_hid.c" "$T/i8042.c" || exit 2
"$O/ch9350_test" > "$O/ch9350_test.txt" 2>&1 || rc=1
cat "$O/ch9350_test.txt"

for R in 44100 48000; do
    echo "== audio_rate_test AUDIO_RATE=$R"
    $CC -O2 -g -w -DUSE_FMOPL -DAUDIO_RATE=$R -I"$T" -o "$O/audio_rate_test_$R" \
        "$HERE/audio_rate_test.c" "$T/pcspk.c" "$T/adlib.c" "$T/fmopl.c" -lm || exit 2
    "$O/audio_rate_test_$R" > "$O/audio_rate_$R.txt" 2>&1 || rc=1
    cat "$O/audio_rate_$R.txt"
done

# Optional: the same test against a changed sb16.c (SB16_PATCHED=path/sb16.c)
if [ -n "${SB16_PATCHED:-}" ]; then
    for R in 44100 48000; do
        echo "== audio_rate_test AUDIO_RATE=$R with $SB16_PATCHED"
        $CC -O2 -g -w -DUSE_FMOPL -DAUDIO_RATE=$R -DSB16_C="\"$SB16_PATCHED\"" -I"$T" \
            -o "$O/audio_rate_test_patched_$R" \
            "$HERE/audio_rate_test.c" "$T/pcspk.c" "$T/adlib.c" "$T/fmopl.c" -lm || exit 2
        "$O/audio_rate_test_patched_$R" > "$O/audio_rate_patched_$R.txt" 2>&1 || rc=1
        cat "$O/audio_rate_patched_$R.txt"
    done
fi

echo "== all: $([ $rc = 0 ] && echo PASS || echo FAIL)"
exit $rc
