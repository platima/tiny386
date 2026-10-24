#!/bin/bash
# Second round of dual-boot checks (2026-10-21, resumed run).  Reads the first
# round's builds and inputs; writes only under $W/r2.  No serial port, no board.
set -u
W=$HOME/tiny386-idf-scratch/dualboot
R=$W/r2
IDF_PATH=$HOME/esp/esp-idf
IDF_TOOLS_PATH=$HOME/.espressif-upstream
REL_ZIP=/mnt/d/Repos/esp32-s31-linux-release/esp32-s31-linux-flash-v2026.09.20.zip
BE_PKG=/mnt/d/Repos/esp32-s31-linux-release/main-be5b725/esp32s31-linux-main-be5b725-flash
V2_IMG=/mnt/d/Repos/esp32-s31-linux-release/tiny386-idf/v2/images
cd "$W"

{
echo "### 8. the copied inputs against their originals"
echo "== release zip"; sha256sum "$REL_ZIP"; cat inputs/release-zip.sha256
for f in Image linux.size flash.bat initramfs.cpio opensbi.bin flash.sh bootloader/s31-linux-loader.bin \
         bootloader/partition_table/partition-table.bin bootloader/bootloader/bootloader.bin; do
  a=$(unzip -p "$REL_ZIP" "$f" | sha256sum | cut -c1-64); b=$(sha256sum < "inputs/release-v2026.09.20/$f" | cut -c1-64)
  [ "$a" = "$b" ] && echo "  same  $a  $f" || echo "  DIFFERENT  $f"
done
echo "== be5b725 package"
for f in bootloader.bin partition-table.bin s31-linux-loader.bin opensbi.bin Image linux.size initramfs.cpio flash.bin flash_args; do
  cmp -s "$BE_PKG/$f" "inputs/be5b725/$f" && echo "  same  $(sha256sum < "$BE_PKG/$f" | cut -c1-64)  $f" || echo "  DIFFERENT  $f"
done
echo "== v2 images"
for f in bootloader/bootloader.bin partition_table/partition-table.bin tiny386.bin flash_args; do
  cmp -s "$V2_IMG/$f" "inputs/v2/$f" && echo "  same  $(sha256sum < "$V2_IMG/$f" | cut -c1-64)  $f" || echo "  DIFFERENT  $f"
done
} > "$R/8-inputs.txt" 2>&1

# 0xFF over 0x9000..0x1FFFF: the unused gap after the table, nvs, otadata,
# phy_init and the gap before factory, as in be5b725's flash.bin
python3 -c 'import sys; open(sys.argv[1],"wb").write(b"\xff"*0x17000)' "$R/blank-0x9000-0x1ffff.bin"

env -i HOME="$HOME" PATH=/usr/bin:/bin:/usr/sbin:/sbin IDF_PATH="$IDF_PATH" IDF_TOOLS_PATH="$IDF_TOOLS_PATH" R="$R" W="$W" \
  bash -c '. "$IDF_PATH/export.sh" >/dev/null 2>&1 && cd "$R" && python -I simulate-flash.py "$R/sim" "$W"' > "$R/9-flash-simulation.txt" 2>&1
echo "simulation exit $?" >> "$R/9-flash-simulation.txt"

env -i HOME="$HOME" PATH=/usr/bin:/bin:/usr/sbin:/sbin IDF_PATH="$IDF_PATH" IDF_TOOLS_PATH="$IDF_TOOLS_PATH" R="$R" W="$W" \
  bash -c '. "$IDF_PATH/export.sh" >/dev/null 2>&1 && cd "$R" && bash neg-tests.sh' > "$R/10-image-info-guard.txt" 2>&1
ls -l "$R"
