#!/bin/bash
# Copy the dual-boot results to the Windows output folder.  Writes only there.
set -eu
W=$HOME/tiny386-idf-scratch/dualboot
O=/mnt/d/Repos/esp32-s31-linux-release/tiny386-idf/v3/dualboot
mkdir -p "$O"/build "$O"/scripts "$O"/checks/decoded "$O"/checks/esptool-help
cd "$W"
cp dual/build/bootloader/bootloader/bootloader.bin "$O"/bootloader.bin
cp dual/build/bootloader/partition_table/partition-table.bin "$O"/partition-table.bin
cp dual/bootloader/partitions-korvo1-dualboot.csv "$O"/partitions-korvo1-dualboot.csv
cp r2/blank-0x9000-0x1ffff.bin "$O"/blank-0x9000-0x1ffff.bin
R="$O"/linux-v2026.09.20
mkdir -p "$R"/bootloader/bootloader "$R"/bootloader/partition_table
for f in bootloader/bootloader/bootloader.bin bootloader/partition_table/partition-table.bin \
         bootloader/s31-linux-loader.bin opensbi.bin Image linux.size initramfs.cpio flash.bat; do
  cp inputs/release-v2026.09.20/$f "$R"/$f
done
# build records
cp dual/build/bootloader/bootloader/bootloader.elf "$O"/build/bootloader.elf
cp dual/build/bootloader/bootloader/bootloader.map "$O"/build/bootloader.map
cp src/linux-be5b725/bootloader/sdkconfig "$O"/build/sdkconfig.linux-be5b725
cp dual/bootloader/sdkconfig "$O"/build/sdkconfig.dualboot
cp dual/bootloader/sdkconfig.defaults "$O"/build/sdkconfig.defaults.dualboot
cp src/provenance.txt "$O"/build/provenance.txt
cp src/SHA256SUMS "$O"/build/source-copy.SHA256SUMS
diff -u src/linux-be5b725/bootloader/sdkconfig dual/bootloader/sdkconfig \
  | sed -e "s#$HOME#~#g" > "$O"/build/sdkconfig.diff || true
diff -u src/linux-be5b725/bootloader/sdkconfig.defaults dual/bootloader/sdkconfig.defaults \
  | sed -e "s#$HOME#~#g" > "$O"/build/sdkconfig.defaults.diff || true
sed -e "s#$HOME#~#g" logs/dual.log > "$O"/build/dual.log
sed -e "s#$HOME#~#g" logs/ref.log > "$O"/build/ref.log
sed -e "s#$HOME#~#g" logs/dual-fresh.log > "$O"/build/dual-fresh.log
# scripts
cp build-dualboot.sh check-dualboot.sh esptool-help.sh r2/run-r2.sh r2/simulate-flash.py r2/neg-tests.sh "$O"/scripts/
# checks
for f in checks/*.txt; do sed -e "s#$HOME#~#g" "$f" > "$O"/checks/$(basename "$f"); done
for f in r2/8-inputs.txt r2/9-flash-simulation.txt r2/10-image-info-guard.txt; do sed -e "s#$HOME#~#g" "$f" > "$O"/checks/$(basename "$f"); done
for f in checks/decoded/*.csv checks/decoded/*.cmp; do cp "$f" "$O"/checks/decoded/; done
cp esptool-help/*.txt "$O"/checks/esptool-help/
(cd "$O" && find . -type f ! -name SHA256SUMS ! -name DUALBOOT.md -print0 | sort -z | xargs -0 sha256sum) > "$O"/SHA256SUMS
cat "$O"/SHA256SUMS
cat "$O"/build/sdkconfig.diff
cat "$O"/build/sdkconfig.defaults.diff
