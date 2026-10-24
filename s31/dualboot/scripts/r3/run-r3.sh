#!/bin/bash
# Third round of the dual-boot checks (2026-10-21, second resumed run).
# Rechecks the set as it is on disk now: the v3.1 images and the scripts and
# FLASH.txt after the review.  Reads the originals (v3 folder, release zip,
# be5b725 package, v2 images, the first round's build); writes only under
# $R/out.  No serial port is opened; no board is involved.
#
#   bash ~/tiny386-idf-scratch/dualboot/r3/run-r3.sh
set -u
W=$HOME/tiny386-idf-scratch/dualboot
R=$W/r3
V3WIN='D:\Repos\esp32-s31-linux-release\tiny386-idf\v3'
IDF_PATH=$HOME/esp/esp-idf
IDF_TOOLS_PATH=$HOME/.espressif-upstream
cd "$R" || exit 1
rm -rf "$R/out"          # this round's own output only
mkdir -p "$R/out"

idfenv() {
    env -i HOME="$HOME" PATH=/usr/bin:/bin:/usr/sbin:/sbin IDF_PATH="$IDF_PATH" IDF_TOOLS_PATH="$IDF_TOOLS_PATH" \
        bash -c '. "$IDF_PATH/export.sh" >/dev/null 2>&1 && cd "$0" && "$@"' "$R" "$@"
}

idfenv python -I "$R/check-r3.py" "$R/out" "$W" "$V3WIN" "$IDF_PATH" > "$R/out/r3-checks.txt" 2>&1
echo "check-r3.py exit $?" >> "$R/out/r3-checks.txt"
idfenv python -I "$R/simulate-r3.py" "$R/out/sim" "$R/out/write-lists.json" > "$R/out/r3-flash-simulation.txt" 2>&1
echo "simulate-r3.py exit $?" >> "$R/out/r3-flash-simulation.txt"
rmdir "$R/out/sim" 2>/dev/null
{ echo "esp32-s31-linux-main HEAD: $(git -C "$HOME/esp32-s31-linux-main" rev-parse HEAD)"
  echo "esp32-s31-linux HEAD:      $(git -C "$HOME/esp32-s31-linux" rev-parse HEAD)"
  echo "esp-idf HEAD:              $(git -C "$IDF_PATH" rev-parse HEAD)"
  date -Is; } > "$R/out/provenance.txt" 2>&1
tail -3 "$R/out/r3-checks.txt"
tail -2 "$R/out/r3-flash-simulation.txt"
