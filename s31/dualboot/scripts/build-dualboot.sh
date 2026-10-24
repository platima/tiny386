#!/bin/bash
# Dual-boot bootloader and partition table for the ESP32-S31-Korvo-1:
# Linux (be5b725 layout) as the default, tiny386 in a "test" app partition at
# 0xE20000, booted while GPIO42 is held low through reset.
#
# Build only.  Never flashes, never opens a serial port.  Reads the Linux
# trees and the release files, writes only under $W.
#
#   bash build-dualboot.sh
set -eu

W=$HOME/tiny386-idf-scratch/dualboot
SRC_MAIN=$HOME/esp32-s31-linux-main          # upstream be5b725, read only
SRC_FORK=$HOME/esp32-s31-linux               # fork c288bc9 (v2026.09.20), read only
IDF_PATH=$HOME/esp/esp-idf                   # read only
IDF_TOOLS_PATH=$HOME/.espressif-upstream     # read only
REL_ZIP=/mnt/d/Repos/esp32-s31-linux-release/esp32-s31-linux-flash-v2026.09.20.zip
BE_PKG=/mnt/d/Repos/esp32-s31-linux-release/main-be5b725/esp32s31-linux-main-be5b725-flash
V2=/mnt/d/Repos/esp32-s31-linux-release/tiny386-idf/v2

cd "$W"
mkdir -p logs

idf() {   # idf DIR args...: idf.py in a clean environment, from bash
    local d=$1; shift
    env -i HOME="$HOME" PATH=/usr/bin:/bin:/usr/sbin:/sbin \
        IDF_PATH="$IDF_PATH" IDF_TOOLS_PATH="$IDF_TOOLS_PATH" \
        bash -c '. "$IDF_PATH/export.sh" >/dev/null 2>&1 && cd "$0" && idf.py "$@"' "$d" "$@"
}

echo "== inputs (copies; the originals are only read)"
if [ ! -d inputs ]; then
    mkdir -p inputs/release-v2026.09.20 inputs/be5b725 inputs/v2
    unzip -q "$REL_ZIP" -d inputs/release-v2026.09.20
    cp -a "$BE_PKG"/. inputs/be5b725/
    cp -a "$V2"/images/. inputs/v2/
    sha256sum "$REL_ZIP" > inputs/release-zip.sha256
fi

echo "== source copy of the be5b725 loader project"
if [ ! -d src ]; then
    mkdir -p src/linux-be5b725 src/fork-c288bc9
    cp -a "$SRC_MAIN"/bootloader "$SRC_MAIN"/shared src/linux-be5b725/
    cp -a "$SRC_FORK"/bootloader/partitions.csv "$SRC_FORK"/bootloader/sdkconfig \
          "$SRC_FORK"/bootloader/sdkconfig.defaults src/fork-c288bc9/
    { git -C "$SRC_MAIN" rev-parse HEAD
      git -C "$SRC_MAIN" status --porcelain --ignored -- bootloader shared
      git -C "$SRC_FORK" rev-parse HEAD
      git -C "$IDF_PATH" rev-parse HEAD
      git -C "$IDF_PATH" status --porcelain | wc -l; } > src/provenance.txt
    (cd src && find . -type f -print0 | sort -z | xargs -0 sha256sum) > src/SHA256SUMS
fi

echo "== reference build: the copy, unchanged"
if [ ! -f ref/build/bootloader/bootloader/bootloader.bin ]; then
    rm -rf ref; mkdir ref; cp -a src/linux-be5b725/. ref/
    cp ref/bootloader/sdkconfig ref/sdkconfig.as-copied
    idf "$W/ref/bootloader" -B ../build/bootloader bootloader partition-table > logs/ref.log 2>&1
fi

echo "== dual-boot build"
if [ ! -f dual/build/bootloader/bootloader/bootloader.bin ]; then
    rm -rf dual; mkdir dual; cp -a src/linux-be5b725/. dual/
    B=dual/bootloader
    cat > $B/partitions-korvo1-dualboot.csv <<'CSV'
# Name,   Type, SubType, Offset,   Size,      Flags
nvs,      data, nvs,     0x11000,  0x6000,
otadata,  data, ota,     0x17000,  0x2000,
phy_init, data, phy,     0x19000,  0x1000,
factory,  app,  factory, 0x20000,  0x200000,
opensbi,  data, 0x40,    0x220000, 0x80000,
linux,    data, 0x40,    0x2A0000, 0x780000,
initramfs,data, 0x40,    0xA20000, 0x200000,
tiny386,  app,  test,    0xE20000, 0x1E0000,
CSV
    # the new lines, in the generated sdkconfig (kept, so every other line is
    # the one be5b725 was built with) and in sdkconfig.defaults (so a fresh
    # sdkconfig gets them too)
    python3 - "$B/sdkconfig" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
old_test = "# CONFIG_BOOTLOADER_APP_TEST is not set\n"
new_test = ("CONFIG_BOOTLOADER_APP_TEST=y\n"
            "CONFIG_BOOTLOADER_NUM_PIN_APP_TEST=42\n"
            "CONFIG_BOOTLOADER_APP_TEST_PIN_LOW=y\n"
            "CONFIG_BOOTLOADER_HOLD_TIME_GPIO=1\n")
assert s.count(old_test) == 1, "APP_TEST line not found once"
s = s.replace(old_test, new_test)
for key in ("CONFIG_PARTITION_TABLE_CUSTOM_FILENAME", "CONFIG_PARTITION_TABLE_FILENAME"):
    old = key + '="partitions.csv"\n'
    assert s.count(old) == 1, key
    s = s.replace(old, key + '="partitions-korvo1-dualboot.csv"\n')
open(p, "w").write(s)
PY
    cat >> $B/sdkconfig.defaults <<'DEF'
# Dual boot with tiny386 (tiny386-idf v3, DUALBOOT.md): Linux by default,
# the "test" app partition (tiny386 at 0xE20000) while GPIO42 is held low
# for a second after reset.  GPIO42 is the Korvo-1's ADC key line; the
# key at 0.38 V (the bottom of the ladder) reads low.
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions-korvo1-dualboot.csv"
CONFIG_BOOTLOADER_APP_TEST=y
CONFIG_BOOTLOADER_NUM_PIN_APP_TEST=42
CONFIG_BOOTLOADER_APP_TEST_PIN_LOW=y
CONFIG_BOOTLOADER_HOLD_TIME_GPIO=1
DEF
    # keep sdkconfig newer than the defaults, as the Linux Makefile expects
    touch $B/sdkconfig
    cp $B/sdkconfig dual/sdkconfig.as-edited
    idf "$W/dual/bootloader" -B ../build/bootloader bootloader partition-table > logs/dual.log 2>&1
fi

echo "== dual-boot build again, sdkconfig made fresh from sdkconfig.defaults"
if [ ! -f dual-fresh/build/bootloader/bootloader/bootloader.bin ]; then
    rm -rf dual-fresh; mkdir dual-fresh; cp -a dual/bootloader dual/shared dual-fresh/
    rm -rf dual-fresh/bootloader/sdkconfig
    idf "$W/dual-fresh/bootloader" -B ../build/bootloader bootloader partition-table > logs/dual-fresh.log 2>&1
fi

echo "== done"
ls -l ref/build/bootloader/bootloader/bootloader.bin ref/build/bootloader/partition_table/partition-table.bin \
      dual/build/bootloader/bootloader/bootloader.bin dual/build/bootloader/partition_table/partition-table.bin \
      dual-fresh/build/bootloader/bootloader/bootloader.bin dual-fresh/build/bootloader/partition_table/partition-table.bin
