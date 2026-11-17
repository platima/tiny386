#!/bin/bash
# The ESP-Mosaico's battery readings (esp/main/battery_mosaico.c) on the PC,
# against a fake BQ27220:
#   bash tests/host/battery/run_battery.sh [OUT]
# Exit status 0 when it passes.
set -u
# GCC 12's AddressSanitizer loops for ever, printing DEADLYSIGNAL, on about
# one start in four under the address randomisation of newer kernels (WSL2
# 6.18, 2026-11-17: 102 of 400 starts of an empty program; one such loop
# filled the disk with an 88 GB log).  The tests do not need it, so they
# run without it.
if [ -z "${HOST_NORAND:-}" ] && setarch -R true 2>/dev/null; then
	HOST_NORAND=1 exec setarch -R bash "$0" "$@"
fi
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
OUT=${1:-$HERE/../build}
CC=${CC:-gcc}
mkdir -p "$OUT" || exit 2
# battery_mosaico.c comes in through the test (its own console.h and
# perfport.h from esp/main); the ESP-IDF and gauge headers from stub/
$CC -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined \
    -DUSE_LCD_QSPI_MOSAICO -DTINY386_PERFPORT \
    -DMOSAICO_I2C_SDA=56 -DMOSAICO_I2C_SCL=3 -DMOSAICO_I2C_PORT=0 \
    -I"$HERE/stub" -I"$TREE/esp/main" \
    -o "$OUT/test_battery" "$HERE/test_battery.c" || exit 1
# well under a second; the limit stops a run that never ends
timeout 60 "$OUT/test_battery"
