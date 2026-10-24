# Dual boot on the Korvo-1: Linux by default, tiny386 on a key (or the other way round)

Written 2026-10-21 for FIRMWARE-PLAN.md section 2 and the dual-boot half of 3.7 and 3.9. Built
and checked in WSL and on Windows without a board. Nothing was flashed, no serial port was
opened, and nothing here has run on hardware.

Tags, as in the plan:

* [verified] read in the named source, or seen in a build, test or run made for this work
* [inferred] follows from verified facts; not built or run
* [reported] someone else's figure or statement, not checked at its origin
* [unknown] only the board can say

File names without a folder are in this folder (`v3\dualboot\`). `checks\N-*.txt` are the saved
outputs of the checks, `scripts\` the scripts that made them, `build\` the build records. The
WSL work tree is `~/tiny386-idf-scratch/dualboot` (first round: `build-dualboot.sh`,
`check-dualboot.sh`; second round, after the resumed run picked it up: `r2/`; third round: `r3/`).

How this note came to be, all on 2026-10-21. Round 1 built the bootloader and table and ran
checks 1 to 7. Round 2, after a usage-limit stop, added checks 8 to 14 and the first Windows
scripts. The v3 build then put this folder's bootloader and table, unchanged, beside its
`tiny386.bin` in `..\images\korvo1-dualboot\`, and the review of v3 changed the scripts
(section 8). Round 3, after a second stop, rechecked the whole set as it is on disk now: the v3.1
images, the scripts and FLASH.txt after the review, every hash pin and write list taken out of the
scripts themselves, and every flash path again, cut-off writes included. It found nothing to
change in the images, the table or the scripts, and added one paragraph to FLASH.txt (section 12).
Where an older section gives v2 figures, section 12 has the v3.1 ones.

Round 4, 2026-10-22: a second partition table with the two apps' roles swapped, so that tiny386
starts with no key held and VOL+ starts Linux, with the same bootloader; `flash-dualboot.bat`
gained a `table` mode to write it alone, and every script knows it. Section 13. Sections 1 to 12
describe the Linux-default set and still hold for it.

Round 5, 2026-10-22 and 23, after the board-safety review of v3.8: the scripts finish a first
install that was cut short over Linux's table, keeping the backup taken before it and never taking
one of the cut state; `app` checks the bootloader; the DOS board's image is refused. Section 14;
12.2 (cases 1c and 1d), 13.1 and 13.5 (C5) are corrected in place.


## 1. In one screen

* Three small writes put tiny386 beside Linux: tiny386 at 0xE20000 in one esptool run, then a
  bootloader at 0x2000 and a partition table at 0x8000 in a second. Nothing from 0x9000 to
  0xE1FFFF is written, which is all of Linux [verified: `checks\9-flash-simulation.txt` case 1;
  again with the v3.1 images and the scripts' own write lists, `checks\r3\r3-flash-simulation.txt`
  case 1].
* Linux stays the default. Holding VOL+ (GPIO42 at 0.38 V) through reset and for a second after
  boots tiny386 [verified: build config and disassembly, section 3; on hardware: unknown].
* The bootloader is the be5b725 Linux bootloader rebuilt from a copy of its own project, with
  five lines added to its sdkconfig and the table file name changed. The unchanged copy rebuilds
  the be5b725 `bootloader.bin` byte for byte apart from the build time and the hash over it
  [verified: `checks\3-bootloader-sizes.txt`].
* It is 22,352 bytes against a limit of 24,576 (0x2000 to 0x8000): 2,224 bytes spare
  [verified: `build\dual.log` line 426, "0x8b0 bytes (9%) free"].
* The table is the Linux table, the same bytes for its seven entries, plus
  `tiny386, app, test, 0xE20000, 0x1E0000` [verified: `checks\1-tables-bytes.txt`,
  `checks\2-tables-decoded.txt`].
* v2's `tiny386.bin` (1,482,096 bytes) fits the 1,966,080-byte partition with 483,984 bytes
  (24.6 %) to spare [verified: `checks\9-flash-simulation.txt`]; the v3.1 one (865,904 bytes)
  with 1,100,176 (56.0 %) [verified: `checks\r3\r3-checks.txt` H].
* The Linux release images and the be5b725 images never write at or above 0xC20000, so a
  tiny386 at 0xE20000 survives any Linux re-flash [verified: `checks\7-flash-map.txt`,
  `checks\9-flash-simulation.txt` cases 3 to 6].
* Windows files in `v3\`: `FLASH.txt`, `flash-dualboot.bat`, `flash-standalone.bat`,
  `back-to-linux.bat` and `flash-paths.cmd` (the one place the tiny386 image path is set). Each
  script needs the port (`COM10`), reads the board's partition table before it asks, and writes
  only after Y. Their logic was tested in harnesses with a stub in place of esptool; the .bat
  files themselves were not run against a board (section 8).


## 2. Which Linux this is for

* Plat's Korvo-1 runs the be5b725-era Linux, and its flash holds the be5b725 partition table
  [reported: Plat, via the task brief, 2026-10-21].
* The be5b725 table and the table in the fork release v2026.09.20 are the same file:
  SHA-256 `520538d3...bf16da` for both, and the be5b725 and fork c288bc9 `partitions.csv` files
  compare equal [verified: `checks\1-tables-bytes.txt`]. Decoded with ESP-IDF's
  `gen_esp32part.py`, they give the same seven lines [verified: `checks\2-tables-decoded.txt`]:

  ```
  nvs,data,nvs,0x11000,24K,
  otadata,data,ota,0x17000,8K,
  phy_init,data,phy,0x19000,4K,
  factory,app,factory,0x20000,2M,
  opensbi,data,64,0x220000,512K,
  linux,data,64,0x2a0000,7680K,
  initramfs,data,64,0xa20000,2M,
  ```

* So this set serves either Linux. Upstream v0.4.0 and later use other tables
  [verified: `..\..\research\firmware-design.md` 6.1] and need their own dual-boot table.


## 3. The bootloader

### 3.1 Where it comes from

* Copied from `~/esp32-s31-linux-main` (upstream be5b725, commit `be5b7253a044...`), folders
  `bootloader/` and `shared/`, into the scratch tree; the Linux tree was only read
  [verified: `build\provenance.txt`, `build\source-copy.SHA256SUMS`]. The copied
  `bootloader/sdkconfig` is git-ignored there (`!!`), so it is the one the be5b725 build
  generated [verified: provenance]. ESP-IDF is `~/esp/esp-idf` at 188e3e55bb with no local
  changes, the commit recorded in the project's own `esp-idf.version` [verified: provenance].
* Built with `idf.py bootloader partition-table` only (the second-stage bootloader and the
  table, not the Linux loader app), in ESP-IDF's clean environment [verified:
  `scripts\build-dualboot.sh`, `build\dual.log`].
* Reference build of the unchanged copy: its `bootloader.bin` differs from the be5b725 package's
  in 43 bytes, 10 in the build date and time string (offsets 0x48 to 0x5B, "Oct 21 2026 15:12:04"
  against "Sep 27 2026 08:05:42") and the last 33 (checksum and SHA-256 over the image)
  [verified: `checks\3-bootloader-sizes.txt`]. Its partition table is byte-identical to
  be5b725's [verified: `checks\1-tables-bytes.txt`]. The copy and this ESP-IDF therefore
  rebuild what is in Plat's flash [inferred].
* The fork release's bootloader (c288bc9, 23,024 bytes) was built with an older ESP-IDF
  ("v6.1"). Its sdkconfig has the same values as be5b725's for every setting a second-stage
  bootloader uses: log level, flash mode, speed and size, PSRAM boot init, single core, table
  offset, watchdog, RTC reserve, flash encryption, MMU page size, rollback, compile time; the
  other differences are ESP-IDF version symbols and app-side settings [verified:
  `checks\13-misc.txt`].

### 3.2 What was changed

Five lines in `sdkconfig`, and the table file name [verified: `build\sdkconfig.diff`]:

```
CONFIG_BOOTLOADER_APP_TEST=y
CONFIG_BOOTLOADER_NUM_PIN_APP_TEST=42
CONFIG_BOOTLOADER_APP_TEST_PIN_LOW=y
# CONFIG_BOOTLOADER_APP_TEST_PIN_HIGH is not set
CONFIG_BOOTLOADER_HOLD_TIME_GPIO=1
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions-korvo1-dualboot.csv"
CONFIG_PARTITION_TABLE_FILENAME="partitions-korvo1-dualboot.csv"
```

The same settings were appended to `sdkconfig.defaults` (`build\sdkconfig.defaults.diff`). A
second build that made its sdkconfig from scratch out of `sdkconfig.defaults` produced the same
sdkconfig, line for line, and the same bootloader apart from the build time (one byte at 0x5A
and the 33-byte tail) and the same partition table [verified: `checks\4-sdkconfig.txt`,
`checks\3-bootloader-sizes.txt`, `checks\1-tables-bytes.txt`]. Every other setting is the Linux
one, including `CONFIG_BOOTLOADER_LOG_LEVEL_INFO`, dio at 80 MHz, 16 MB, PSRAM boot init and
single-core mode [verified: `checks\4-sdkconfig.txt`]. The build time stays in the image
because the Linux config has `CONFIG_BOOTLOADER_COMPILE_TIME_DATE=y`; it was left as it is.

### 3.3 Size

| Bootloader | Bytes | Spare below 0x8000 |
|-|-|-|
| be5b725 (in Plat's flash) | 22,000 | 2,576 |
| release v2026.09.20 | 23,024 | 1,552 |
| tiny386 v2, its own | 22,016 | 2,560 |
| **dual boot** | **22,352** | **2,224** |

[verified: `checks\3-bootloader-sizes.txt`; ESP-IDF's own check in `build\dual.log` line 426:
"Bootloader binary size 0x5750 bytes. 0x8b0 bytes (9%) free."] The test-pin code costs 340
bytes of text (21,766 to 22,106) [verified: `checks\5-test-pin.txt`, `size` output]. No log
level change was needed.

### 3.4 The pin check, in the binary and in the source

* In `bootloader.elf`, `call_start_cpu0` reads the reset reason, skips the check on a deep-sleep
  wake (reason 5), then calls `bootloader_common_check_long_hold_gpio_level` with a0 = 42 (pin),
  a1 = 1 (seconds), a2 = 0 (level low) [verified: `checks\5-test-pin.txt`, disassembly]. The
  strings "Detect a boot condition of the test firmware" and "Test firmware is not found in
  partition table" are in the dual-boot image and not in the reference one [verified: same].
* The check [verified: ESP-IDF 188e3e55 `components/bootloader_support/src/bootloader_common.c`
  lines 37 to 54]: select the pad as GPIO, enable input and the internal pull-up, read once; not
  low means boot as usual; low means keep reading until one second has passed, and any high
  reading on the way means boot as usual. Only a key held for the whole second selects tiny386.
* The choice [verified: `components/bootloader/subproject/main/bootloader_start.c` lines 84 to
  130]: with the key held for the second, boot the `test` partition; without it, the normal
  choice, which for this table is `factory` because there are no `ota_N` apps
  (`components/bootloader_support/src/bootloader_utility.c` lines 376 to 411).
* A held key with a bad or missing tiny386 image: the bootloader logs "No bootable test partition
  in the partition table" and resets (`bootloader_utility.c` lines 582 to 589) [verified:
  source], so the board resets over and over while the key is held, and boots Linux once it is
  let go [inferred].
* The pin and its level: GPIO42 carries the Korvo-1's four ADC keys (10k to 3.3 V; keys to ground
  through 13k, 6.8k, 3.3k or 1.3k, printed as 1.87 V, 1.34 V, 0.82 V and 0.38 V) [reported:
  `..\..\research\firmware-design.md` 6.3, from schematic sheet 4]. The 0.38 V key, VOL+ on the
  schematic and SET in esp-board-manager [reported: same], reads about 0.45 V with the internal
  pull-up added, below the 0.825 V input-low limit [reported: FIRMWARE-PLAN.md section 2, module
  datasheet v0.7 Table 6-3; arithmetic inferred]. The 0.82 V key comes to roughly 0.95 V with the
  pull-up (taking it as about 45k, a typical figure, not this chip's), above the input-low limit
  but far below the input-high one, so it may or may not trigger [inferred]. GPIO42 is not a
  strapping pin; BOOT (GPIO61) is, which is why it cannot be used [reported: same].
* The pull-up stays on after the check. Nothing on the Linux side reads GPIO42: neither the
  be5b725 nor the c288bc9 kernel patches contain an ADC key, SAR ADC or GPIO key driver
  [verified: grep, `checks\13-misc.txt`], and the v2 Korvo-1 build of tiny386 does not use
  GPIO42 [verified: grep, same]. A later tiny386 that reads the key (plan 3.5) sets the pin up
  itself [inferred].

### 3.5 Things that differ from tiny386's own bootloader

* Single core. The Linux bootloader is built with `CONFIG_ESP_SYSTEM_SINGLE_CORE_MODE=y`, so this
  one is too ("Unicore bootloader" is logged); tiny386 is a dual-core app. ESP-IDF provides for
  that: a multicore app's `ext_mem_init` calls `do_multicore_settings`, which copies core 0's
  cache bus settings to core 1, on every chip without `SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE`
  [verified: `components/esp_system/port/cpu_start.c` lines 362 to 401 and 509 to 512], and
  `esp32s31`'s `soc_caps.h` does not define it (the P4's does) [verified: grep]. Whether it works
  on this chip is [unknown] until the board runs it. If tiny386's second core fails to start
  under this bootloader, the fallback is a multicore build of the dual-boot bootloader, which
  would change one Linux setting [inferred].
* Bootloader RAM. Both bootloaders use the same ESP-IDF linker script, which keeps the app's
  static RAM below 0x2F06CFB0 [verified: `components/bootloader/subproject/main/ld/esp32s31/
  bootloader.memory.ld.in` lines 27 to 52]. v2 tiny386's highest loaded RAM byte is 0x2F03E13F
  (segment 4, 0x2F038F80 plus 0x51C0) [verified: `checks\6-image-info.txt`]. Same limit as under
  its own bootloader [inferred].
* Rollback. The be5b725 config has `CONFIG_BOOTLOADER_APP_ROLLBACK=y` (a newer ESP-IDF symbol;
  `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` itself is off) [verified: `checks\13-misc.txt`]. With
  it the bootloader rewrites an `otadata` entry marked "pending verify" as "aborted"
  (`bootloader_utility.c` lines 393 to 402) [verified: source]. Linux's `otadata` is blank, so
  nothing is written [inferred]. Anti-rollback is off, which `CONFIG_BOOTLOADER_APP_TEST`
  requires (`components/bootloader/Kconfig.projbuild` line 220) [verified].


## 4. The partition table

`partitions-korvo1-dualboot.csv`, the plan's table (3.7) [verified: file]:

```
nvs,      data, nvs,     0x11000,  0x6000,
otadata,  data, ota,     0x17000,  0x2000,
phy_init, data, phy,     0x19000,  0x1000,
factory,  app,  factory, 0x20000,  0x200000,
opensbi,  data, 0x40,    0x220000, 0x80000,
linux,    data, 0x40,    0x2A0000, 0x780000,
initramfs,data, 0x40,    0xA20000, 0x200000,
tiny386,  app,  test,    0xE20000, 0x1E0000,
```

* Decoded with `gen_esp32part.py` and compared with the decoded Linux table, the only difference
  is the added line `tiny386,app,test,0xe20000,1920K,` [verified: `checks\2-tables-decoded.txt`].
* As bytes: the first 224 bytes (the seven Linux entries) are identical to the Linux table; the
  eighth entry is `tiny386` (type 0, subtype 0x20, 0xE20000, 0x1E0000); then the MD5 entry
  [verified: `checks\1-tables-bytes.txt`, hex dump]. Running `gen_esp32part.py` on the CSV gives
  the build's `partition-table.bin` byte for byte [verified: `checks\decoded\dual-from-csv.cmp`].
* 0xE20000 is 64 KiB aligned, as app partitions must be, and the partition ends exactly at the
  end of the 16 MB flash [verified: decode; `gen_esp32part.py` checks alignment].
* The Linux loader finds `opensbi`, `linux` and `initramfs` by type data, subtype 0x40 and name
  [verified: be5b725 `bootloader/main/main.c` lines 240 to 245], so an app entry of subtype
  `test` is invisible to it [inferred]. Linux itself has no access to the flash on the Korvo-1
  [reported: `..\..\research\firmware-design.md` 6.2].
* SHA-256 `d41cba3cf33bedee257c8709389c99d16101992439d7a6ed353967cd0fdd4346`.


## 5. What was proved without a board

| Check | Result | Where |
|-|-|-|
| Inputs copied for the checks match their originals (release zip, be5b725 package, v2 images) | all equal | `checks\8-inputs.txt` |
| be5b725 and release tables | byte-identical | `checks\1-tables-bytes.txt` |
| Dual table decodes as intended | Linux lines plus `tiny386` | `checks\2-tables-decoded.txt` |
| Bootloader size | 22,352 of 24,576 bytes | `checks\3-bootloader-sizes.txt` |
| Bootloader config shows the test pin | `CONFIG_BOOTLOADER_NUM_PIN_APP_TEST 42` in the bootloader's own `sdkconfig.h` | `checks\4-sdkconfig.txt` |
| The binary calls the check with 42, 1 s, low | yes | `checks\5-test-pin.txt` |
| `esptool image-info` on every image | all chip ID 32, checksum and hash valid | `checks\6-image-info.txt` |
| tiny386 v2 fits | 1,482,096 of 1,966,080 bytes | `checks\9-flash-simulation.txt` |
| Linux images stay below 0xE20000 | highest erased byte 0xC1FFFF | `checks\7-flash-map.txt` |
| Ten flash sequences simulated | section 6 | `checks\9-flash-simulation.txt` |
| esptool's own `merge-bin` of the dual-boot write list equals the simulation | yes | `checks\9-flash-simulation.txt`, end |
| esptool 5.4.0 accepts the short options used | yes; a wrong value is rejected | `checks\11-short-options.txt` |
| How image-info reports bad files, for the .bat guard | flipped byte: exit 0 with "(invalid)"; truncated, not an image, empty, missing: exit 2 | `checks\10-image-info-guard.txt` |
| .bat logic | all cases as intended | `checks\12-bat-tests.txt` |
| Round 3: every file, hash pin and write list in the scripts and FLASH.txt, against the files | 0 failed | `checks\r3\r3-checks.txt` |
| Round 3: every path again with the v3.1 images, plus five cut-off writes | section 12 | `checks\r3\r3-flash-simulation.txt` |

The flash simulation (`scripts\simulate-flash.py`) models a 16 MiB flash and applies each write
list the way esptool 5.4 `write-flash` does: data padded to 4 bytes, every 4 KiB sector it
touches erased first, the bytes before an unaligned start erased too, headers left alone with
`-fm keep -ff keep -fs keep` [verified: esptool 5.4.0 `cmds.py` lines 1600 to 1660 and 1728 to
1745, and `_update_image_flash_params` line 742]. The start state is be5b725's `flash.bin` at 0x0
with a marker byte from 0xC20000 up. After each sequence it decodes the table, names the
bootloader, works out what the bootloader would boot with and without the key, and runs
`esptool image-info` on that partition.


## 6. What each write changes

Ranges are what is erased and rewritten, in 4 KiB sectors, with the v3.1 `tiny386.bin`
[verified: `checks\r3\r3-flash-simulation.txt`, whose write lists were taken out of the scripts];
v2 figures, where they differ, in brackets [verified: `checks\9-flash-simulation.txt`].

| Set (script) | Writes | Erased and written | Linux partitions | 0xE20000 up |
|-|-|-|-|-|
| Dual boot (`flash-dualboot.bat COM10`) | run 1: 0xE20000 tiny386; run 2: 0x2000 bootloader, 0x8000 table | 0xE20000 to 0xEF3FFF (v2: to 0xF89FFF); 0x2000 to 0x8FFF | none | tiny386 |
| tiny386 update (`flash-dualboot.bat COM10 app`) | 0xE20000 tiny386 | 0xE20000 to the end of the image | none | tiny386 |
| Undo, small (`back-to-linux.bat COM10`, which is `small be5b725`) | 0x2000 bootloader, 0x8000 table | 0x2000 to 0x8FFF | none | left as it is |
| Undo, small, release files (`back-to-linux.bat COM10 small release`) | the same two, from the release | 0x2000 to 0x8FFF | none | left as it is |
| Linux, full release (`back-to-linux.bat COM10 full release`) | release `flash.bat` list plus 0xFF over 0x9000 to 0x1FFFF | 0x2000 to 0x1FFFF; 0x20000 to 0xDBFFF; 0x220000 to 0x240FFF; 0x2A0000 to 0x672FFF; 0xA1F000 to 0xC1FFFF | all rewritten | left as it is |
| Linux, full be5b725 (`back-to-linux.bat COM10 full`) | `flash.bin` at 0x0 | 0x0 to 0xC1FFFF | all rewritten | left as it is |
| Standalone (`flash-standalone.bat COM10`) | 0x2000 tiny386's bootloader, 0x8000 tiny386's table, 0x10000 tiny386 | 0x2000 to 0x8FFF; 0x10000 to 0xE3FFF (v2: to 0x179FFF) | nvs, otadata, phy_init and the loader overwritten; OpenSBI, kernel and initramfs intact but unreachable | left as it is |

What the round 2 simulation found for each path, with v2's images and the dual-boot writes as
one list [verified: `checks\9-flash-simulation.txt`]. Round 3 found the same outcome on every
path with the v3.1 images and the two esptool runs as the script now makes them (section 12):

1. Dual boot over be5b725: no Linux byte changes; the table has eight entries; no key boots
   `factory` (the be5b725 loader, valid), the key boots `tiny386` at 0xE20000 (valid).
2. A tiny386 update changes only 0xE20000 upwards.
3. Small undo with the release files: the Linux table again, the release bootloader, Linux and
   tiny386 bytes untouched; no key and key both boot `factory`.
4. Small undo with the be5b725 files: 0x0 to 0xC1FFFF equal to the start state, byte for byte.
5. Full release: release loader, OpenSBI, kernel and initramfs; nothing at or above 0xB95000
   changed against the start state (0xC1FFFF is the highest byte written); tiny386 still at
   0xE20000.
6. Full be5b725: 0x0 to 0xC1FFFF equal to the start state.
7. Standalone: tiny386's table (factory at 0x10000, 1792K), tiny386 boots with or without the
   key; Linux's nvs, otadata, phy_init and factory are changed.
8. Standalone, then full release: Linux back; the only tiny386 bytes left are 0xDC000 to
   0x179FFF (v3.1, worked out from the sizes: 0xDC000 to 0xE3FFF), inside the `factory` partition after the end of the
   release loader. The bootloader reads only the image's own length, so they are harmless
   [inferred].
9. Standalone, then full be5b725: 0x0 to 0xC1FFFF equal to the start state.
10. Dual boot over the release Linux (after 5): no key boots the release loader, the key boots
    tiny386.

Notes:

* The release's `linux.size` goes to 0xA1FFF4, which is not sector aligned, so esptool erases
  0xA1F000 to 0xA1FFF3 as well [verified: esptool `cmds.py` lines 1732 to 1745]. That is the
  release's own `flash.bat` behaviour, inside the `linux` partition, after the kernel.
* The 0xFF file over 0x9000 to 0x1FFFF in the full release write is not in the release's own
  `flash.bat`. It clears what a standalone tiny386 leaves in the unused gap, nvs, otadata and
  phy_init, giving what be5b725's `flash.bin` has there (all 0xFF) [verified:
  `checks\14-flashbin-blank.txt`, all 0xFF from 0x0 to 0x1FFFF outside the bootloader and
  table]. Linux keeps NVS off (`bootloader/main/wifi_ipc.c` lines 417 to 426) [verified], so
  nothing of Linux's is lost.


## 7. Going back

### 7.1 To plain Linux

* With the dual boot in: press RESET without the key. Nothing else.
* To take the dual boot out: `back-to-linux.bat COM10`, which means `small be5b725`. Two small
  writes; 0x0 to 0xC1FFFF is then what be5b725 put there, byte for byte, and only 0xE20000 up
  differs from the flash before the dual boot went in [verified: round 2 simulation case 4,
  round 3 case 3].
* Plat's release again: `back-to-linux.bat COM10 full release`, or the release's
  own `flash.bat COM10` from `esp32-s31-linux-flash-v2026.09.20.zip`. This puts the release's
  loader, kernel and initramfs in place of be5b725's. The card is not touched; the release card
  image has the same partition layout (64 MiB FAT32 and 192 MiB ext4) [verified:
  `..\..\..\RELEASE-NOTES.md` lines 32 to 34], but whether the release kernel runs the be5b725
  card's root file system has not been tried [unknown].
* be5b725 again in full: `back-to-linux.bat COM10 full` (be5b725 is the default; its `flash.bin`
  at 0x0).
* After the standalone set, only a full write brings Linux back: the standalone set overwrites
  the Linux loader [verified: simulation case 7].
* In every case the tiny386 image at 0xE20000 stays in flash, unused and outside the table,
  until the dual-boot pair is written again [verified: simulation cases 3 to 6].

### 7.2 To plain tiny386

`flash-standalone.bat COM10`: tiny386's own bootloader and table (`..\images\korvo1-standalone\`,
the same bytes as v2's pair, pinned by SHA-256 in the script) and the v3.1 tiny386 at 0x10000
(`SA_TINY386_BIN` in `flash-paths.cmd`). tiny386 then boots on every reset [verified: round 2
simulation case 7, round 3 case 7]. Its table puts `bios.bin`, `vgabios.bin`, `linuxstart.bin`, `vmlinux.bin`
and a 10 MB `storage` FAT over what was Linux; the set writes none of them [verified:
`checks\decoded\v2-standalone.csv`]. Over a be5b725 flash `bios.bin` (0x1D0000) and
`vgabios.bin` (0x1F0000) then hold 0xFF [verified: be5b725 `flash.bin` is 0xFF from the end of
the loader, 0xD4E80, to 0x21FFFF, `checks\14-flashbin-blank.txt`; inferred for Plat's
flash], so tiny386 must load both from the card here too (section 9).

To go from the standalone set to the dual boot: `back-to-linux.bat COM10 full` first, then
`flash-dualboot.bat COM10`.

### 7.3 If a write goes wrong

Download mode is in the ROM: hold BOOT, press RESET, let go of BOOT, then write again. A bad
bootloader or table cannot take that away [inferred: `..\..\research\firmware-design.md` 6.6].
esptool checks each write against an MD5 of the data and prints "Hash of data verified."
[verified: esptool 5.4.0 `cmds.py` lines 1650 to 1655 and 1907 to 1927]. What each cut-off
write leaves, and how to finish from there: section 12.2.


## 8. The Windows files

All in `v3\`, run from Windows, never run here. As they are now, after the review
(`..\REVIEW-RESOLUTION.md`, B M1, m1, m4, n1) [verified: read, and parsed in round 3,
`checks\r3\r3-checks.txt` B to D]:

* `flash-paths.cmd`: the paths, the one place to change them. `TINY386_BIN` (default
  `images\korvo1-dualboot\tiny386.bin`, the v3.1 build; for another image, edit the line or
  `set TINY386_BIN=...` before running), the standalone set `SA_BOOTLOADER`, `SA_TABLE`,
  `SA_TINY386_BIN` (default `images\korvo1-standalone\`) and `SA_APP_SIZE` (1,835,008, the
  factory partition of tiny386's own table), and `ESPTOOL` (default `esptool`; `python -m esptool`
  also works). A value already set in the environment wins.
* `flash-dualboot.bat COMport [all|app]`: `all` by default. `all` writes tiny386 at 0xE20000 in
  one esptool run, then the bootloader and table in a second, so a first run cut short leaves
  Linux as it was.
* `flash-standalone.bat COMport`.
* `back-to-linux.bat COMport [small|full] [be5b725|release]`: `small be5b725` by default. The
  release files are unpacked from the zip into `linux-v2026.09.20\` here (checked equal to the
  zip's, `checks\8-inputs.txt`, again in round 3); the be5b725 files are read where they are, in
  `..\..\main-be5b725\esp32s31-linux-main-be5b725-flash\`.
* The port has no default. The task asked for COM10 as the default; the review (B m1) made it a
  required argument, so that once the DOS board exists a Korvo-1 set is not written to it by
  leaving the port out. COM10 is in every usage message and every FLASH.txt command. Left as the
  review had it; Plat's call.

Each script, in order: finds esptool and requires 5.4 or later; checks every fixed file against
its SHA-256 with `certutil` (the dual-boot pair, the standalone pair, the release and be5b725
files, the 0xFF file); checks the tiny386 image exists, fits its partition, and passes `esptool
image-info` with chip ID 32 and no "(invalid)"; reads the board, which writes nothing (`read-mac`,
then `read-flash 0x8000 0xC00`), and goes on only if the table there is one it was made for
(Linux 520538d3, dual boot d41cba3c, tiny386's own afbe6410, as each script needs), saying what it
found otherwise; prints the addresses, files and the exact esptool command; reads a line and
writes only if it is Y. The write is

```
esptool -c esp32s31 -p COM10 -b 921600 -a hard-reset write-flash -fm keep -ff keep -fs keep <offset file>...
```

`-fm keep -ff keep -fs keep` and `-a hard-reset` are esptool's defaults [verified: esptool 5.4.0
`__init__.py` lines 308, 315, 340 and 428]; they are spelt out so that `ESPTOOL_FM`, `ESPTOOL_FF`,
`ESPTOOL_FS` or `ESPTOOL_AFTER` in the environment cannot change the images. With "keep" esptool
leaves the image headers alone, so the bootloader's hash stays valid [verified: `merge-bin`
check in `checks\11-short-options.txt`]. The reset before connecting has no short option and is left at its default,
`default-reset` [verified: `__init__.py` line 419]. The scripts call esptool with `call`, so a
`.cmd` or `.bat` wrapper returns to the script; the first harness run found that without it the
script would stop after the first esptool run, and the scripts were changed [verified:
`checks\12-bat-tests.txt`].

Testing: `scripts\battest\make_harness.py` copies each script's subroutines and argument loop
verbatim into a harness, and `make_flow.py` copies each script's main part up to the Y check
verbatim with the write cut out; `fake-esptool.cmd` stands in for esptool and prints image-info
output captured in WSL. Results: esptool 5.4, 5.10 and 6.0 accepted, 5.3, 4.8 and none refused;
right and wrong SHA-256 and a missing file; a good image, a flipped byte, a non-image, an image
too big and a missing image; every argument order; Y writes, N and an empty line stop; the
default `images\tiny386.bin` missing stops with a message [verified: `checks\12-bat-tests.txt`].
After the review the harness was rebuilt for the new flow, with the board read answered by the
stub: 23 cases, among them each table a script must accept or refuse, no port, no board on the
port, a missing image, an invalid image and N at the prompt [verified: `..\checks\bat-harness.txt`].
The scripts have not changed since (their SHA-256 are the ones in `..\SHA256SUMS`, round 3 A).
The real esptool write and the real board read were never run.


## 9. What the tiny386 build has to do for this layout

For the v3 work, not done here:

* Build with this CSV as its table (`esp/partitions-korvo1-dualboot.csv`, plan section 4), so
  `idf.py size` checks the image against 0x1E0000; keep the table at 0x8000 and the flash at
  16 MB [inferred: plan section 4].
* Name the BIOS files and disks by card path. v2 defaults `bios` and `vga_bios` to `bios.bin`
  and `vgabios.bin` (`pc.c` lines 780 to 781) and `load_rom()` looks any name without a leading
  `/` up as a flash partition and asserts that it exists (`esp/main/esp_main.c` lines 59 to 87)
  [verified: v2 tree]. The dual-boot table has no such partitions, so a default name stops
  tiny386 at start, and with v2's panic setting the board reboots into Linux [inferred]. Plan
  3.4 (paths resolved against the ini's folder) covers it.
* The flash FAT (`storage`) is absent; its mount fails without formatting
  (`esp/main/storage.c` lines 43, 236 and 281 set `format_if_mount_failed = false`, the mount is
  at line 284) [verified: v2 tree].
* NVS: only `wifi.c` touches it, and it erases the partition if it cannot mount it (`wifi.c`
  lines 247 to 250) [verified: v2 tree]. In this table `nvs` is Linux's, which Linux leaves
  unused (NVS off), so even that would cost Linux nothing [inferred]; the plan drops `wifi.c` for
  this board anyway.
* tiny386 writes no other flash and makes no OTA calls [verified: grep of `esp/main`,
  `checks\13-misc.txt`]. Added after the review (`..\REVIEW-boot-safety-flash.md` m2): that
  holds for the dual-boot build. The standalone and DOS board builds, with no card in, mount the
  flash `storage` partition, and its wear levelling writes its records at that partition's end,
  the last sectors below 0x1000000; no Linux partition, and in the dual-boot table the unused end
  of tiny386's [verified: the review's reading of `storage.c` and `WL_Flash.cpp`].


## 10. Unknowns and risks

* Nothing here has run on the board [unknown]: the pin check on GPIO42, the hold timing, the
  dual-core tiny386 under the single-core bootloader, the release loader under this bootloader.
* The key: whether the silkscreen's VOL+ is the 0.38 V key, and whether the 0.82 V key also
  triggers [unknown].
* How long after the RESET release the bootloader reads the pin [unknown]; FLASH.txt says to
  keep holding for two seconds, which covers the one-second hold with room.
* If the board holds a Linux other than be5b725 or the v2026.09.20 release, this table is wrong
  for it [verified: section 2]; check before writing.
* The release kernel with the be5b725 card's root file system [unknown] (section 7.1).
* The board read needs esptool's flasher stub. In esptool 5.4.0 `read-flash` without the stub
  goes to a ROM routine that raises "not supported in ROM" on this chip, and 5.4.0 ships an
  `esp32s31` stub [verified: `checks\r3\r3-checks.txt` J]. Whether that stub runs on Plat's chip
  is [unknown]; Plat's Linux flashes went through esptool's stub too if his esptool had one for
  this chip [inferred]. If it does not run, every script stops before writing, saying esptool
  could not read the board; the commands by hand in FLASH.txt still work, with esptool's
  no-stub option (listed in `checks\r3\esptool-help\main.txt`) added after `-b 921600` if the
  stub is what fails [inferred: esptool's ROM path for writes].
* A write cut off inside the 3 KB partition table leaves a table no script knows, and all three
  then stop; FLASH.txt now says to write the two small be5b725 files by hand first (section 12).
* The tiny386 image to write is `TINY386_BIN`: the v3.1 build by default. A later build only
  needs that line changed or the file replaced; the script checks it with image-info and the
  partition size, and pins only the bootloader and table.


## 11. Files

| File | What |
|-|-|
| `bootloader.bin` | the dual-boot bootloader, 22,352 bytes, SHA-256 `b57469cd...ccff29f` |
| `partition-table.bin` | the dual-boot table, SHA-256 `d41cba3c...0fdd4346` |
| `partitions-korvo1-dualboot.csv` | its source, for the v3 build too |
| `partition-table-tiny386-default.bin` | round 4: the same table with tiny386 the default, SHA-256 `5b90d416...a58226` (section 13) |
| `partitions-korvo1-dualboot-tiny386-default.csv` | its source |
| `blank-0x9000-0x1ffff.bin` | 94,208 bytes of 0xFF for the full release write |
| `linux-v2026.09.20\` | the release's images, unpacked from the zip, unchanged |
| `build\` | `bootloader.elf` and `.map`, both sdkconfigs and their diff, the defaults diff, build logs, provenance |
| `checks\` | outputs 1 to 14, decoded tables, esptool help |
| `checks\r3\` | round 3: `r3-checks.txt`, `r3-flash-simulation.txt`, `write-lists.json` (what each script writes, taken from the scripts), the three decoded tables, image-info for nine images, esptool 5.4.0 help, provenance |
| `scripts\` | `build-dualboot.sh`, `check-dualboot.sh`, `run-r2.sh`, `simulate-flash.py`, `neg-tests.sh`, `shortopts.sh`, `fixtures.sh`, `misc.sh`, `blankcheck.sh`, `stage.sh`, `esptool-help.sh`, and `battest\` |
| `scripts\r3\` | `run-r3.sh`, `check-r3.py`, `simulate-r3.py` |
| `SHA256SUMS` | every file here except itself and this note |

To rebuild in WSL: `bash ~/tiny386-idf-scratch/dualboot/build-dualboot.sh` then
`check-dualboot.sh` and `r2/run-r2.sh` (each skips work already done; delete `dual/` to rebuild
the bootloader). They read `~/esp32-s31-linux-main`, the release zip and the v2 images, and
write only in the scratch tree. `r3/run-r3.sh` rechecks the set in `v3\` as it is at the time
(about ten seconds); it reads the v3 folder, the release zip, the be5b725 package, the v2 images
and the first round's build, and writes only in `r3/out`.


## 12. Round 3: the set as it is on disk now

Run on 2026-10-21 at 20:53 by `scripts\r3\run-r3.sh`, after the v3.1 images and the review's
script changes, against the same trees as before: be5b725 in `~/esp32-s31-linux-main`, c288bc9
in `~/esp32-s31-linux`, ESP-IDF 188e3e55bb [verified: `checks\r3\provenance.txt`]. No serial
port was opened: the esptool commands used were `image-info`, `merge-bin`, `version` and help,
none of which connects (`merge_bin_cli` and the `cli` group in esptool 5.4.0 `__init__.py` call
no `prepare_esp_object`) [verified: read]. Result: none of the checks failed [verified:
`checks\r3\r3-checks.txt`].

### 12.1 What was checked (`checks\r3\r3-checks.txt`)

| Part | What | Result [verified] |
|-|-|-|
| A | The files the scripts write, against `..\SHA256SUMS` and this folder's `SHA256SUMS`; the bootloader and table here, in `..\images\korvo1-dualboot\` and in the WSL build output | the same bytes everywhere; the standalone bootloader and table are v2's (ec0a396b, afbe6410) |
| B | Every SHA-256 pinned in the three scripts, resolved through `flash-paths.cmd` and each script's own `set` lines to the file it names | every pin matches its file; `PT_LINUX` is the be5b725 table, the release table and 0x8000 to 0x8BFF of be5b725's `flash.bin`; every table file is the 0xC00 bytes the board read reads |
| C | The write lists, taken out of the scripts (`write-lists.json`) | as section 6. Run 1 of `flash-dualboot.bat` writes only 0xE20000, run 2 only 0x2000 and 0x8000; the image comes from `TINY386_BIN` alone; every write is `-c esp32s31 -p PORT -b 921600 -a hard-reset write-flash -fm keep -ff keep -fs keep`; each script reads the board, then asks, then writes; no erase, status-register or eFuse command anywhere |
| D | FLASH.txt's six commands by hand, against the scripts | sections 1, 3, 4 and 5 write what the scripts write: same offsets, same bytes, same order; section 6 (DOS board) names files that exist |
| E | The tables | be5b725 and release byte-identical; both trees' `bootloader/partitions.csv`, read again, equal the copy built from; decoded with `gen_esp32part.py`, the dual-boot table is the Linux table plus `tiny386,app,test,0xe20000,1920K,`; the first 224 bytes identical; entry 8 and the MD5 entry right; the CSV gives the table byte for byte |
| F | The bootloader | 22,352 bytes, 2,224 spare below 0x8000 (ESP-IDF's own line: "0x8b0 bytes (9%) free"); header bytes DIO, 80m, 16MB, as be5b725's; the second-stage `sdkconfig.h` has `CONFIG_BOOTLOADER_APP_TEST 1`, `CONFIG_BOOTLOADER_NUM_PIN_APP_TEST 42`, `CONFIG_BOOTLOADER_APP_TEST_PIN_LOW 1`, `CONFIG_BOOTLOADER_HOLD_TIME_GPIO 1`; the sdkconfig differs from the Linux one in those lines and the table file name only; `~/esp32-s31-linux-main/bootloader/sdkconfig`, read again, is the copy the build started from |
| G | `esptool -c esp32s31 image-info` (as the scripts run it) on nine images: this set's bootloader, the v3.1 apps (dual boot and standalone), the standalone bootloader, v2's app, be5b725's and the release's bootloader and Linux loader | all chip ID 32, DIO 80m 16MB, minimum chip revision v0.0, checksum and hash valid |
| H | Fit and RAM | v3.1: 865,904 of 1,966,080 bytes (56.0 % spare); standalone 865,904 of 1,835,008; highest RAM byte loaded 0x2F03735B (v2: 0x2F03E13F), below the dual-boot bootloader's lowest segment, 0x2F06A2B0 |
| I | Linux's reach | the release `flash.bat` (read from the zip), be5b725's `flash_args` and its `flash.bin`: highest byte 0xC1FFFF; every `back-to-linux.bat` set the same or lower; `linux-v2026.09.20\` equal to the zip |
| J | esptool 5.4.0's own help | `-c` with esp32s31, `-p`, `-b`, `-a` with hard-reset, `write-flash`, `read-flash ADDRESS SIZE OUTPUT`, `read-mac`, `image-info`, `version`; `-fm`, `-ff`, `-fs` take `keep`; `version` prints `5.4.0` on a line of its own, the line the scripts' version check reads; `read-flash` needs the stub on this chip, and 5.4.0 has one for it (section 10) |
| K | The exact options, parsed by esptool offline | `esptool -c esp32s31 -b 921600 -a hard-reset merge-bin ... -fm keep -ff keep -fs keep` over the dual-boot writes: exit 0, bootloader and tiny386 bytes unchanged |

### 12.2 Every path again, and cut-off writes (`checks\r3\r3-flash-simulation.txt`)

The round 2 model (section 5), fed the write lists from the scripts, the v3.1 images, and
`flash-dualboot.bat`'s two esptool runs, from be5b725's `flash.bin`. For each state it names the
bootloader at 0x2000, decodes the table, works out what boots with and without the key (ESP-IDF
188e3e55 `bootloader_start.c` lines 54 to 130, `bootloader_utility.c` lines 150 to 160 and 582 to
626), and runs image-info on that partition [verified: the output; on the board: unknown].

| Case | After | No key | VOL+ held |
|-|-|-|-|
| 1 | `flash-dualboot.bat` over be5b725 | be5b725 loader (factory) | tiny386 v3.1 (test, 0xE20000) |
| 1a | run 1 only | Linux | Linux: be5b725's bootloader has no test pin |
| 1b | run 1 cut half way through tiny386 | Linux | Linux |
| 1c | run 2 cut inside the bootloader | no valid second-stage bootloader | the same |
| 1d | run 2 stopped between the bootloader and the table | Linux | resets while held (no test partition in Linux's table), Linux when let go |
| 1e | run 2 cut at the start of the table, its sector erased | no table: "Failed to verify partition table", reset, over and over | the same |
| 2, 2a | `app` again; `all` again | nothing in the flash changes | |
| 3 | `back-to-linux.bat COM10` (small be5b725) after 1 | Linux; 0x0 to 0xE1FFFF equal to before the install | Linux |
| 4 | small release after 1 | release bootloader, be5b725 loader | the same |
| 5 | full release after 1 | release loader (c288bc9); tiny386 still at 0xE20000 | the same |
| 6 | full be5b725 after 1 | Linux; 0x0 to 0xC1FFFF as before | the same |
| 7 | `flash-standalone.bat` over be5b725 | tiny386 v3.1 (factory, 0x10000) | the same |
| 8, 9 | standalone, then full be5b725, or full release | Linux (be5b725: 0x0 to 0xC1FFFF as before) | the same |
| 10, 11 | standalone over the dual boot, then full be5b725 | tiny386, then Linux | the same |
| 12 | `flash-dualboot.bat` over the release Linux (after 5) | release loader | tiny386 |

esptool's own `merge-bin` of the dual-boot writes gives the model's bytes on an erased chip
[verified].

What the cut-off cases mean [inferred: the cases above and the scripts' table check]:

* 1a, 1b and 1d: Linux still starts, and `flash-dualboot.bat COM10` run again finds Linux's
  table and finishes the install. (Corrected in v3.9, section 14: for 1d this held only while no
  backup had been taken. From v3.2 the first run always takes one, and v3.2 to v3.8's scripts then
  found 0x2000 to 0x8FFF different from it and stopped. Since v3.9 the script recognises the
  dual-boot bootloader in front of Linux's table, keeps the backup and finishes.)
* 1c: the chip's ROM finds no valid second-stage bootloader. esptool's reset into download mode
  does not depend on the flash, and the table is still Linux's, so the script run again finishes
  the install. (The same correction: since v3.9, from the start of the dual-boot bootloader or an
  erased page at 0x2000; v3.2 to v3.8 stopped, section 14.)
* 1e: the window is the moment between erasing the 0x8000 sector and writing its 3 KB. The
  scripts then read a table they do not know and stop. Writing the be5b725 pair by hand
  (FLASH.txt 4) gives Linux's table back, after which any of the scripts runs again. The same
  holds for a cut inside the table during `back-to-linux.bat` or `flash-standalone.bat`. FLASH.txt
  now says this in "If something goes wrong".

### 12.3 What round 3 changed

* `..\FLASH.txt`: one paragraph at the end of "If something goes wrong", for the case above; its
  line in `..\SHA256SUMS` updated, and every other line there still verifies [verified:
  `sha256sum -c`].
* This note: the status at the top, the v3.1 figures, sections 7.1, 7.2, 8 and 10 brought up to
  the scripts as they are, and this section.
* `checks\r3\` and `scripts\r3\`, and this folder's `SHA256SUMS` made again to include them (every
  earlier entry unchanged).

Nothing else: the bootloader, the table, the images and the .bat files are as round 2 and the
review left them.


## 13. Round 4: tiny386 as the default (2026-10-22)

Plat finds holding VOL+ for every tiny386 boot tiresome while testing, so this round makes the
default at reset a choice. Nothing in the bootloader or in either app changed: one more partition
table, the scripts, and the notes. Built and checked in WSL and on Windows without a board; nothing
was flashed and no serial port was opened. The checks are in `..\checks\bootswap\`.

### 13.1 The table

`partitions-korvo1-dualboot-tiny386-default.csv` is `partitions-korvo1-dualboot.csv` with the two
app subtypes swapped and nothing else; the labels stay [verified: the diff in
`..\checks\bootswap\bootswap-table.txt` section 1]:

```
factory,  app,  test,    0x20000,  0x200000,      (the Linux loader, now the test app)
tiny386,  app,  factory, 0xE20000, 0x1E0000,      (tiny386, now the factory app)
```

* Made with the dual-boot work's own invocation (`gen_esp32part.py --flash-size 16MB`, ESP-IDF
  188e3e55bb, clean), which gives the existing dual-boot table byte for byte from its CSV
  [verified: `bootswap-table.txt` sections 2 and 3].
* Against the Linux-default dual-boot table, 17 bytes differ: the subtype byte of entry 4 (0x063,
  0x00 to 0x20), that of entry 8 (0x0E3, 0x20 to 0x00), and the MD5 digest (0x110 to 0x11F). The
  MD5 entry checks as `esp_partition_table_verify()` checks it, and 0x120 to 0xBFF are all 0xFF
  [verified: `bootswap-table.txt` section 5].
* `gen_esp32part.py` prints a WARNING, that a partition named `factory` has subtype test
  (`gen_esp32part.py` lines 586 to 594), and goes on. The label is kept on purpose: nothing reads it
  (13.3), and keeping it leaves the first 224 bytes of the table one byte away from Linux's own.
* SHA-256 `5b90d4166afc39d72a1d6c7b16f3a2e4a5cc0193a3e0eb50990a4dbb9da58226`, here as
  `partition-table-tiny386-default.bin` and in `..\images\korvo1-dualboot-tiny386-default\` beside
  the dual-boot bootloader and, when the boot swap made it, the v3.7 `tiny386.bin`, both byte for
  byte those of `..\images\korvo1-dualboot\` [verified: `bootswap-table.txt` section 7]. That
  folder now holds the current app (v3.9), made by `..\build.sh` with the rest (13.6;
  `..\BUILD-NOTES.md` 20.7 and 21).

### 13.2 Why swapping the subtypes is enough

Read in ESP-IDF 188e3e55bb, the tree the dual-boot bootloader was built from [verified: source]:

* `bootloader_utility_load_partition_table()` (`bootloader_utility.c` lines 141 to 270) keeps the
  position of the app with subtype factory as `bs->factory` and of the one with subtype test as
  `bs->test` (lines 173 to 182). Nothing else about them is looked at: not the label, not the
  offset or the size. The only rule on the offset is `gen_esp32part.py`'s 64 KiB alignment for app
  partitions, which 0xE20000 meets.
* `selected_boot_partition()` (`bootloader_start.c` lines 84 to 137) first asks
  `bootloader_utility_get_selected_boot_partition()`. That reads the otadata at 0x17000 (it is in
  the table, line 385); with `CONFIG_BOOTLOADER_APP_ROLLBACK=y` (`build\sdkconfig.dualboot` line
  1022) it would mark a PENDING_VERIFY entry ABORTED (lines 393 to 402; Linux's otadata is blank,
  section 3.5); then, as the table has no OTA app partitions (`app_count` 0), it logs "Defaulting
  to factory image" (lines 404 to 411), whatever the otadata holds.
* Then, unless waking from deep sleep, the test pin: GPIO42 low for a second
  (`build\sdkconfig.dualboot` lines 1100 to 1104) gives `TEST_APP_INDEX` if a test partition is in
  the table, else `INVALID_INDEX` and a reset (lines 115 to 130).
* `bootloader_utility_load_boot_image()` (lines 576 to 627): for the test index, the test app or a
  reset (lines 582 to 589); for the factory index, the factory app, and if that is not a valid
  image, any OTA app (none here), then the test app as the last resort, "Falling back to test app
  as only bootable partition" (lines 592 to 622).

So with the swapped table the unchanged bootloader starts tiny386 (factory, 0xE20000) with no key
and the Linux loader (test, 0x20000) with VOL+ held; and a tiny386 that is not valid in flash is
passed over for Linux, key or no key [inferred from the source above; modelled in 13.5; on the
board: unknown].

### 13.3 Neither app looks at its own subtype or label

* The Linux loader (be5b725 `bootloader/main/main.c`, read in `~/esp32-s31-linux-main` at
  be5b7253, no local changes): its only partition calls are `find_partition()` (lines 240 to 248),
  `esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, name)` for `linux`, `opensbi` and
  `initramfs` (lines 265, 333, 363), and `esp_partition_read()` on those. No `esp_ota_` call, no
  running-partition lookup, no app partition looked up by label or subtype; `shared\` holds only
  `esp32s31-wifi-ipc.h` [verified: grep of `bootloader/main` and `shared`]. Its `REQUIRES` list has
  no `app_update` and the project is a `MINIMAL_BUILD` [verified: its two `CMakeLists.txt`], so
  ESP-IDF's OTA code is most likely not in it at all [inferred; no map file in that read-only tree].
  When it cannot load OpenSBI, the kernel or the initramfs it calls `esp_restart()` (lines 400 to
  411) [verified]: with tiny386 the default, that restart comes up in tiny386.
* tiny386 v3.7: flash partitions are looked up by name only, for the BIOS files
  (`esp/main/esp_main.c` lines 136 to 138, 182 to 185, 618 to 621) and the `storage` FAT
  (`esp/main/storage.c` lines 489 to 491); neither exists in the dual-boot tables, so the card is
  used, as before. No OTA call in `esp/main` [verified: `git grep` at tag v3.7].
* ESP-IDF's own start-up code in an app built with `CONFIG_BOOTLOADER_APP_ROLLBACK_CONFIRM_ON_STARTUP`
  (tiny386's v3.7 Korvo-1 build has it, `esp/build-v37k/config/sdkconfig.h` lines 498 and 499 in
  the WSL tree, and its map has `esp_ota_auto_confirm` in it) runs
  `ota_auto_confirm.c`: `esp_ota_get_running_partition()`, which finds the running app among the
  app partitions of any subtype (`esp_partition/partition_target.c` lines 235 to 264,
  `ESP_PARTITION_SUBTYPE_ANY`), then `esp_ota_get_state_partition()`, which returns
  `ESP_ERR_NOT_SUPPORTED` for a factory or a test partition (`esp_ota_ops.c` lines 80 to 86 and 1252
  to 1260), and the handler returns. The same under either table [verified: source].
* An app image carries no subtype: the same `tiny386.bin` (and the same Linux loader) serves both
  tables [verified: the images, `esptool image-info` in 13.5].

### 13.4 The scripts

* `flash-dualboot.bat COMport table linux|tiny386` writes only `0x8000` (the chosen table) after
  Y. It goes on only if one of the two dual-boot tables is at 0x8000 and the dual-boot bootloader
  is at 0x2000: it reads 0x2000 for 22,352 bytes and compares the SHA-256 with `b57469cd...`. Under
  any other bootloader the key does nothing, and the tiny386-default table would leave no way into
  Linux short of flashing, so it stops and names `all` instead. If the board already has the
  default asked for, it writes nothing and says so.
* `flash-dualboot.bat COMport all [linux|tiny386]` writes the whole set with that table; with
  neither word, Linux over Linux's own table (as before) and over a dual boot the default the board
  has. `app` never changes the table and refuses a default word. Every mode prints "At RESET now"
  and "After this write".
* All three scripts know the new table (`PT_DUAL386`, reported as `dual386`): `app` accepts it,
  `back-to-linux.bat` (backup, small, full, whole) treats it as it treats the Linux-default dual
  boot, and `flash-standalone.bat` too. The reset note and the "Written" lines say which system a
  reset starts. `flash-paths.cmd` changed in a comment only.
* The harness (`..\checks\bootswap\bat-harness-bootswap.txt`, made by
  `..\checks\bootswap\scripts\make_flow_bootswap.py`): v3.7's 39 cases unchanged and 30 more, run
  with a stub esptool on this PC. Cases 1 to 39 give the same esptool calls, WOULD WRITE lines and
  exit codes as `..\checks\bat-harness-v3.7.txt`; no write reached the stub [verified]. The new
  cases:

| Cases | What | Result [verified] |
|-|-|-|
| 40, 41, 59, 60 | `table tiny386` over the Linux-default dual boot (Y; N; words in any order and case; no `tiny386.bin` needed) | one write, `0x8000` the tiny386-default table, `hard-reset`; N writes nothing |
| 42 | `table linux` over tiny386 the default | one write, `0x8000` the Linux-default table |
| 43, 44 | the default asked for is there already | nothing written, exit 0 |
| 45, 56 | `table` with no default; `app tiny386` | usage, no board read |
| 46, 49, 50 | `table` over Linux's own table, tiny386's own, one nobody knows | stops before writing; over Linux it names `all tiny386` |
| 47, 48 | either dual-boot table behind be5b725's bootloader (a cut write) | stops: not the dual-boot bootloader |
| 51 | `all tiny386` over Linux be5b725, no backup | backup first, then tiny386, then the bootloader and the tiny386-default table |
| 52, 53, 54 | `all` over tiny386 the default (keeps it); `all linux` over it; `all tiny386` over the Linux default | the set with the right table |
| 55 | `app` over tiny386 the default | tiny386 alone; "no key: tiny386" before and after |
| 57, 58 | no board; another board's MAC | stops before writing |
| 61 to 66 | `back-to-linux.bat` backup, small (also behind be5b725's bootloader), no backup, full, whole, from tiny386 the default | the same writes as from the Linux-default dual boot |
| 67, 68 | back-to-linux: another MAC; N | stops; nothing written |
| 69 | `flash-standalone.bat` over tiny386 the default | as over the Linux-default dual boot |

### 13.5 The flash simulation (`..\checks\bootswap\flash-simulation-bootswap.txt`)

The v3.7 model (section 5, `simulate-flash-v37.py`), extended: the tiny386-default write lists,
the backup's slice, cut-off writes, and the bootloader's fallback when the chosen app is not a
valid image. Its first ten paths are v3.7's, and their output is the same line for line
[verified]. The new ones, from be5b725's `flash.bin` with the Linux-default dual boot in (path 1):

| Case | After | Changed | No key | VOL+ held |
|-|-|-|-|-|
| 11 | `table tiny386` | 0x8000 to 0x8FFF only; 0x9000 to 0xE1FFFF equal to the start | tiny386 v3.7 (factory, 0xE20000) | be5b725 loader (test, 0x20000) |
| 12 | `table linux` after 11 | 0x8000 to 0x8FFF; the whole flash equal to path 1 again | Linux | tiny386 |
| 13 | `all tiny386` over be5b725 | 0x2000 to 0x8FFF and tiny386; the whole flash equal to 11 | tiny386 | Linux |
| 14 | `app` after 11 | nothing (same file) | tiny386 | Linux |
| 15 | `back-to-linux.bat` (backup) after 11 | 0x2000 to 0x8FFF; 0x0 to 0xE1FFFF equal to the start | Linux | Linux |
| 16 | `small` (be5b725) after 11 | 0x2000 to 0x8FFF; 0x0 to 0xC1FFFF equal to be5b725's | Linux | Linux |
| 17, 18 | `full` be5b725; `small release` after 11 | as from the Linux-default dual boot | Linux | Linux |
| 19 | `flash-standalone.bat` after 11 | as from the Linux-default dual boot | tiny386 (0x10000) | the same |
| C1, C2 | `table tiny386` cut inside the table, or just after the erase | no valid table, or one with no app in it | reset, over and over | the same |
| C3 | `back-to-linux small` after 11, cut after the bootloader with the table's sector erased | no table | reset, over and over | the same |
| C3b | the same, stopped between the two files | be5b725's bootloader (no key) and the tiny386-default table | tiny386 | tiny386: Linux not reachable until the script is run again |
| C4 | `app` after 11 cut half way | tiny386 not a valid image | falls back to Linux (test) | Linux |
| C5 | `all tiny386` over be5b725, stopped between run 2's bootloader and table | dual-boot bootloader, Linux's table | Linux | reset while held (no test partition), Linux when let go |

What C1 to C5 mean [inferred: the cases and the scripts' checks]: C1, C2 and C3 are the round 3
case 1e again, and are mended the same way, by writing a table the scripts know by hand
(`FLASH.txt`, "If something goes wrong"); after C1 or C2 either dual-boot table will do, since the
dual-boot bootloader is still there. C3b is the one new state where Linux cannot be started with
the key; `back-to-linux.bat` run again recognises the table (harness case 66) and finishes. C4 is
milder than its Linux-default twin, where holding VOL+ over a broken tiny386 made the board reset.
C5 is round 3's case 1d with the other table: Linux starts. Running the script again finishes it
only since v3.9; v3.8's stopped on its own backup there (section 14). `back-to-linux.bat COM10`
also mends it, from the backup.

### 13.6 Risks and unknowns

* Nothing of this has run on the board [unknown]. First thing to see after `table tiny386`: RESET
  with nothing held gives tiny386's boot console; VOL+ held through RESET gives Linux. If either
  does not, `flash-dualboot.bat COM10 table linux` (or `back-to-linux.bat COM10`) and a UART log.
* With tiny386 the default, Linux is one key away rather than the default: whatever resets the chip
  from Linux comes back up in tiny386 unless VOL+ is held. `reboot` most likely does so through
  OpenSBI's system reset [inferred]; the Linux loader's own `esp_restart()` on a load failure does
  [verified: `main.c` lines 400 to 411]. The card warnings stand as before: `poweroff` in Linux, or
  shut the guest down, before RESET.
* tiny386 halts on a crash (`CONFIG_ESP_SYSTEM_PANIC_PRINT_HALT=y`, `esp/sdkconfig.korvo1` line 41)
  [verified], so a crash does not loop; a tiny386 that stops at every start (a bad ini) means
  holding VOL+ for Linux until the card is put right.
* The tiny386-default table written by hand in front of a bootloader without the key (be5b725's,
  the release's, or tiny386's own) leaves Linux unreachable without flashing. The scripts refuse
  that; by hand it is up to whoever types it, which is why `FLASH.txt` 2 says to let the script
  check first.
* `..\images\korvo1-dualboot-tiny386-default\tiny386.bin` is a copy of
  `..\images\korvo1-dualboot\tiny386.bin` (v3.7) for anyone writing that folder by hand with its
  `flash_args`. The scripts always write `TINY386_BIN`; a rebuilt app needs the copy and that
  folder's `SHA256SUMS` made again. (Since v3.8 `..\build.sh` makes that folder itself, from
  `partition-table-tiny386-default.bin`, which it checks by SHA-256, and the korvo1-dualboot
  set it has just built: `..\BUILD-NOTES.md` 20.)

### 13.7 Files of this round

| File | What |
|-|-|
| `partitions-korvo1-dualboot-tiny386-default.csv`, `partition-table-tiny386-default.bin` | the table (13.1) |
| `..\images\korvo1-dualboot-tiny386-default\` | `bootloader.bin`, `partition-table.bin`, `tiny386.bin`, `flash_args`, `SHA256SUMS` |
| `..\flash-dualboot.bat`, `..\back-to-linux.bat`, `..\flash-standalone.bat`, `..\flash-paths.cmd` | 13.4 |
| `..\checks\bootswap\bootswap-table.txt` | the table made, decoded and compared |
| `..\checks\bootswap\bat-harness-bootswap.txt` | the scripts' flow, 69 cases |
| `..\checks\bootswap\flash-simulation-bootswap.txt` | the flash simulation, 19 paths and 6 cut-off cases |
| `..\checks\bootswap\scripts\` | `table-check.sh`, `make_flow_bootswap.py`, `simulate-flash-bootswap.py` and `mk_sim.py`, which made it from the v3.7 one |
| `..\checks\bootswap\scripts-before\` | the four Windows scripts as they were before this round, byte for byte, for the `previous-v3.x\SHA256SUMS` lists and the earlier checks |


## 14. v3.9: writes cut short over Linux's table, and two more checks (2026-10-22)

The board-safety review of v3.8 (`..\REVIEW-v38-board-safety-and-features.md`, finding F1, major)
found that round 3's 12.2 was wrong for cases 1c and 1d once the first run had taken its backup,
which it always has by then. This section is what v3.9 changed in the scripts, and the record.
Nothing in the bootloader, the tables or the apps' flash use changed. Nothing was flashed and no
serial port was opened: the scripts ran through cmd.exe with a stub esptool, as in 13.4.

### 14.1 What v3.8's scripts did after a cut [verified: the review's harness cases 84 to 88]

A first install over Linux writes tiny386 (run 1), then the bootloader and the table (run 2). Cut
inside run 2's bootloader (1c: nothing starts) or between the bootloader and the table (1d, 13.5
C5: Linux starts), the table at 0x8000 is still Linux's. `flash-dualboot.bat COM10` run again then
read 0x2000 to 0x8FFF, found it different from the backup it had taken (the bootloader had
changed), and stopped: "it is of another state of this board ... Rename that folder (keep it),
then run this again to take a new backup". Following that took a backup of the half-written
bootloader, and a later `back-to-linux.bat COM10` (backup, its default once a backup exists) would
write that broken bootloader back and say "Linux starts on every reset again". `flash-standalone.bat`
had the same logic.

### 14.2 What v3.9's do [verified: `..\flash-dualboot.bat`, `..\flash-standalone.bat`; harness, 14.4]

Over Linux's table both scripts now also read the first 256 bytes at 0x2000, one flash page. A write
by esptool goes from its start address up, so a write cut short leaves that page as the new
bootloader's first page, or erased (0xFF) if the cut came between the erase and the write, or
untouched if nothing had been written yet (and then nothing else had changed either). The page is
named by SHA-256: the dual-boot bootloader's first 256 bytes (`4fd69a58...`), tiny386's own
(`7d9e47c7...`, the standalone set's), or an erased page (`3d6876a0...`). The five bootloaders this
folder knows (be5b725's, the release's, the dual-boot one, tiny386's own, which is also the DOS
board's) all differ by byte 28 [verified: `..\checks\heads-v3.9.txt`], so the page alone tells them
apart. Then:

* A backup of this board is there, 0x2000 to 0x8FFF differ from it, and the page is one of those
  three: a write by these scripts was cut short after the backup was taken. The backup is kept, no
  new one is taken, and the run goes on and writes its set (`flash-dualboot.bat` finishes the
  install; `flash-standalone.bat` writes its set). Either script recognises the other's cut write.
* No backup is there and the page is one of those three: the script takes no backup of the cut
  state and writes nothing. It says how to go on: give the backup taken before the cut its name
  back (or point `BACKUP_DIR` at it), or put a Linux bootloader back first with
  `back-to-linux.bat COMport small` (be5b725's) or `small release`, then run again, which takes a
  fresh backup of that.
* A backup is there and holds this same cut state (only a script older than v3.9 can have taken
  one): the script stops, says that `back-to-linux.bat` would write the broken bootloader back from
  it, and gives the same two ways on.
* The page is none of those three and 0x2000 to 0x8FFF differ from the backup: Linux itself was
  changed after the backup (another Linux flashed). The script stops as before, now naming both
  ways: rename the backup folder (keep it) to take a new one, or `back-to-linux.bat COMport` to put
  the board back as the backup has it.
* `:write_failed_boot`, the message after a failed run 2, names `back-to-linux.bat COMport` first and
  then the same command again, and what to do if either finds a table it does not know (a cut in
  the table itself, 12.2 case 1e).

Two more checks from the same review:

* `app` over a dual-boot table now reads 0x2000 as `table` does (22,352 bytes against `b57469cd...`).
  Behind any other bootloader (13.5 C3b: be5b725's in front of the tiny386-default table) the key
  does nothing, so v3.8's "At RESET now: no key: tiny386; VOL+ held: Linux" was wrong there; v3.9
  stops and names `flash-dualboot.bat COMport all` (which keeps the table's default) or
  `back-to-linux.bat COMport` (finding F2).
* The app image named by `TINY386_BIN` (`SA_TINY386_BIN` for the standalone set) is refused if it is
  byte for byte `..\images\dosboard\tiny386.bin`, which is a valid ESP32-S31 image called tiny386
  with the same version and would pass every other check (finding F3). A DOS board image from
  elsewhere is not caught; the DOS image's own board check stops it on a Korvo-1 in any case
  (`..\BUILD-NOTES.md` 17).

### 14.3 What did not change

`back-to-linux.bat` and `flash-paths.cmd` are the boot swap's, byte for byte. The write lists, the
esptool options and the order of the writes are as before; every case of the earlier harness gives
the same exit code and the same WOULD WRITE lines, and differs only by the new reads (14.4). The
flash simulations, which follow the write lists, give the same results with the v3.9 images
(`..\checks\flash-simulation-v3.9.txt`, `..\checks\flash-simulation-bootswap-v3.9.txt`).

### 14.4 The harness (`..\checks\bat-harness-v3.9.txt`)

`..\checks\bat-harness-make-flow-v3.9.py`, made by `..\checks\mk_v39.py` from
`..\checks\bootswap\scripts\make_flow_bootswap.py`: the boot swap's 69 cases, the review's 19 (70 to
88, with its two cut states, `linux-dualbl` and `linux-cutbl`), and 15 more (89 to 103), two more
cut states (`linux-erasedbl`, an erased first page; `linux-owncut`, `flash-standalone.bat` cut in
its bootloader) and a backup folder holding a cut state, as an older script would have left it.
Every state also answers the new 256-byte read. Run through cmd.exe with PATH set to
`C:\Windows\System32` and `C:\Windows` only; no write reached the stub in any of the 103 cases.

| Cases | What | v3.8 | v3.9 [verified] |
|-|-|-|-|
| 1 to 69 | the boot swap's | | the same exit codes and WOULD WRITE lines; cases over Linux's table add the 256-byte read, `app` cases (8, 9, 55) the bootloader read |
| 84, 85 | run 2 stopped between the bootloader and the table; cut inside the bootloader; the backup there | stops, advises renaming | the backup kept, the set written (two WOULD WRITE lines) |
| 86 | `back-to-linux.bat` after the cut | the backup's slice back | the same |
| 87 | the cut, the backup folder renamed | a new backup of the cut state, the set written | stops: no backup of a cut state, nothing written, names the two ways on |
| 88 | `back-to-linux.bat` later with that folder | wrote the broken bootloader back | the folder is empty (87 took nothing), so `small be5b725` |
| 71, 91 | `app` behind be5b725's bootloader, either table | went on, said VOL+ gives Linux | stops, names `all` and `back-to-linux.bat` |
| 89 | the first page erased in front of Linux's table, the backup there | (not run) | the backup kept, the set written |
| 90 | a cut state with a backup of that same state | (not run) | stops, names `back-to-linux.bat small` |
| 92 | `app` over the dual boot (Plat's next step) | | the bootloader read, then the one write at 0xE20000 |
| 93 to 95 | the DOS board's image as `TINY386_BIN` or `SA_TINY386_BIN` | | refused before the board is read |
| 96 to 98 | either script after the other's cut write, or its own | | the backup kept, the set written |
| 99, 100 | a cut write, no backup, either script | | stops, nothing taken |
| 101 | `back-to-linux.bat small` from there | | be5b725's pair |
| 102 | another Linux (the release's bootloader) since the backup | stopped, advised renaming | stops, names renaming or `back-to-linux.bat` |
| 103 | `table tiny386` over the dual boot | | the one write at 0x8000 |

### 14.5 Unknowns

* None of this has run against a real cut write on the board [unknown]. That esptool leaves the
  first page as described rests on how it writes (erase, then pages in order from the start) and
  on the cases above [inferred].
* A backup taken by a v3.8 or older script after following its rename advice is caught only if the
  board is still in that cut state (case 90). If the board was changed since, the backup can still
  hold a broken bootloader and `back-to-linux.bat` would write it back; `back-to-linux.bat COMport
  small` avoids it. Plat's board has both systems starting from its dual boot, so its install was
  finished [reported: Plat's runs]; whether it ever met that path is his to say.

