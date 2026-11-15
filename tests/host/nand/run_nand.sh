#!/bin/bash
# The ESP-Mosaico's NAND disk layer (esp/main/nand_vol.c) on the PC:
#   bash tests/host/nand/run_nand.sh [OUT]
# Exit status 0 when it passes.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
OUT=${1:-$HERE/../build}
CC=${CC:-gcc}
mkdir -p "$OUT" || exit 2
$CC -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I"$TREE/esp/main" \
    -o "$OUT/test_nand_vol" "$HERE/test_nand_vol.c" "$TREE/esp/main/nand_vol.c" || exit 1
"$OUT/test_nand_vol"
