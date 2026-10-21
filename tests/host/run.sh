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

# ---- 1. the PS/2 queue's lock (FIRMWARE-PLAN 3.3.1) ----
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

echo
echo "host tests: $pass passed, $fail failed"
[ $fail = 0 ]
