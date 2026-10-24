T=~/tiny386-idf-scratch/v2/tiny386
W=~/tiny386-idf-scratch/dualboot
{
echo "### 13. odds and ends checked for DUALBOOT.md"
echo "== v2 tree esp/main: flash writes, OTA calls, partition lookups"
grep -rn -E "esp_ota_|esp_partition_(write|erase)|esp_flash_(write|erase)|spi_flash_(write|erase)|wl_mount|esp_vfs_fat_spiflash|nvs_flash" $T/esp/main | sed "s#$HOME#~#"
echo "== v2 tree: who calls load_rom, and with what names"
grep -rn "load_rom" $T/*.c $T/esp/main/*.c | sed "s#$HOME#~#" | head
grep -n -E "\"(bios|vgabios)\.bin\"|bios =|vga_bios =" $T/pc.c $T/esp/main/*.c 2>/dev/null | sed "s#$HOME#~#" | head
echo "== GPIO42 in the v2 tree"
grep -rn -E "\b42\b|GPIO_NUM_42" $T/esp/main | sed "s#$HOME#~#" | head
echo "== be5b725 Linux kernel patches: any ADC or key driver (adc-keys, saradc, gpio-keys)"
grep -l -i -E "adc-keys|saradc|gpio-keys" ~/esp32-s31-linux-main/linux/patches/*.patch || echo "  none"
echo "== fork c288bc9 kernel patches, same search"
ls ~/esp32-s31-linux/linux/patches/ 2>/dev/null | head -3
grep -l -i -E "adc-keys|saradc|gpio-keys" ~/esp32-s31-linux/linux/patches/*.patch 2>/dev/null || echo "  none"
echo "== fork c288bc9 vs be5b725 bootloader sdkconfig, CONFIG_SOC_ lines left out"
f() { grep -E '^(CONFIG_|# CONFIG_)' "$1" | grep -v -E '^(# )?CONFIG_SOC_' | sort; }
diff <(f $W/src/linux-be5b725/bootloader/sdkconfig) <(f $W/src/fork-c288bc9/sdkconfig)
echo "== the same keys that a second-stage bootloader uses, both files"
for k in BOOTLOADER_LOG_LEVEL= ESPTOOLPY_FLASHMODE= ESPTOOLPY_FLASHFREQ= ESPTOOLPY_FLASHSIZE= SPIRAM_BOOT_HW_INIT SPIRAM_BOOT_INIT ESP_SYSTEM_SINGLE_CORE_MODE PARTITION_TABLE_OFFSET BOOTLOADER_WDT_TIME_MS BOOTLOADER_RESERVE_RTC_SIZE SECURE_BOOT= FLASH_ENCRYPTION_ENABLED MMU_PAGE_SIZE= BOOTLOADER_APP_ROLLBACK_ENABLE BOOTLOADER_COMPILE_TIME_DATE; do
  a=$(grep -E "^(# )?CONFIG_$k" $W/src/linux-be5b725/bootloader/sdkconfig | head -1)
  b=$(grep -E "^(# )?CONFIG_$k" $W/src/fork-c288bc9/sdkconfig | head -1)
  [ "$a" = "$b" ] && echo "  same: $a" || echo "  DIFFERENT: [$a] [$b]"
done
} > $W/r2/13-misc.txt 2>&1
cat $W/r2/13-misc.txt
