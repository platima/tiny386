# Review: boot safety and flash (tiny386 v3.1), round 2

Written 2026-10-21 as a second independent review with the same lens as the first: can anything
here brick Plat's Korvo-1 or lose his Linux. It reads the firmware as released (tag v3.1, 80e64de;
the images in `images\` are its bytes), the tree as it is now (tag v3.1-tests, 823d5cd, which adds
only `tests/host/`: 15 files, none outside it [verified: the names `git diff` gives between the
two tags]), the scripts, FLASH.txt, TEST-PLAN.md and `dualboot\`. Nothing was flashed, no serial
port was opened, nothing ran on a board. Nothing in the tree or in this folder was changed apart from
this file.

The first round's review (written at 19:00 against tag v3) is kept unchanged at the end of this file
(section 7), because `REVIEW-RESOLUTION.md` refers to its finding numbers. Adding this round on top
means the line for this file in `SHA256SUMS` (8429fb99...) no longer matches; the round 1 text, with
that SHA-256, is also kept as
`~/tiny386-idf-scratch/review-boot-safety-flash/r2/REVIEW-boot-safety-flash.round1.md` in WSL
[verified: sha256sum]. Work files for this round are in
`~/tiny386-idf-scratch/review-boot-safety-flash/r2` (`stretch_test.c`, `st.sh`, `st16.sh`,
`stretch/st-out.txt`, `pt-r2.txt`, `img-r2.txt`, the `g*.sh` greps, `src/` copies of the v3.1
files read).

Tags: [verified] read in the named source, or seen in a check I ran for this review; [inferred]
follows from verified facts; [reported] someone else's statement, not checked at its origin;
[unknown] only the board can say.


## 1. Verdict

No blocker and no major finding. I found nothing that can brick the Korvo-1 or take its Linux away:

* The dual-boot install writes 0xE20000 upwards (tiny386) and 0x2000 to 0x8FFF (bootloader and
  table) and nothing else; Linux ends at 0xC1FFFF [verified: section 4.3].
* The dual-boot bootloader is the Linux bootloader with the five test-pin lines added, nothing else;
  the table is Linux's 224 bytes plus one entry [verified: section 4.1, 4.2].
* No image burns an eFuse, so the ROM download mode stays [verified: section 4.5].
* The dual-boot tiny386 writes no flash at all, never formats, never writes the card's MBR, never
  mounts p2 [verified: section 4.5].
* Every way back works from every state the scripts can leave, and tiny386 never boots itself
  without the key [verified: section 4.4].

The first round's fixes are all in v3.1 (section 3). Three minor findings and five notes follow. The
one that is new in kind (m1) was confirmed with ESP-IDF's own FatFs: a guest read or write past the
end of a disk image makes the image file grow on the card, up to filling the partition. It cannot
touch Linux's root (p2) or the flash, but it is a write to the shared FAT partition that the docs say
never happens.


## 2. Findings

### Minor

**m1. A guest access past the end of a disk image grows the image on the card, up to filling the
partition.**

* tiny386 opens every hard disk image `"r+b"` (`ide.c` lines 2100 to 2106) [verified]. On the ESP
  build, a guest sector read seeks first and then reads (`bf_read_async()`, `ide.c` 2013 to 2027),
  and a write seeks and writes (`bf_write_async()`, 2047 to 2067). Neither checks the sector against
  the image's size in read-write mode; only the snapshot mode does (2071 to 2072). The IDE layer
  does not either: `ide_get_sector()` (557 to 570) gives any 28-bit LBA, and `ide_sector_read()` and
  `ide_sector_write_cb1()` (590 to 610, 648 to 668) pass it straight on [verified: source].
* ESP-IDF's VFS turns `"r+b"` into `FA_READ | FA_WRITE` (`components/fatfs/vfs/vfs_fat.c` lines 315
  to 335) and an lseek into `f_lseek()` with no bound (661 to 695). FatFs's `f_lseek()` on a file
  open for writing allocates clusters up to the new position, "forced stretch" (`src/ff.c` line
  4714), and sets the file size to it (4735); it stops at disk full and still returns FR_OK
  [verified: source, ESP-IDF 188e3e55]. So on the board even a read past the end extends the file,
  where on Linux or the host tests an lseek past the end allocates nothing [inferred: POSIX lseek].
  That is why no host test saw it: the "zeros" path of the review fix E F4 was reasoned, not run
  through FatFs (commit 9b6af1c) [verified: `git grep`, no such test].
* Checked with ESP-IDF's own `ff.c`, `diskio.c` and `diskio_sdmmc.c` built for the host with the
  storage test's stubs, the image file opened as the VFS opens `"r+b"`, then `f_lseek()`, a 512-byte
  `f_read()` and `f_sync()` (what `ide_flush()` does), then mounted again [verified:
  `r2/stretch/st-out.txt`]:

  | Volume | Seek to | Image after | Free clusters |
  |-|-|-|-|
  | 64 MiB FAT32, 8 MiB image | 1,000 sectors past the end | 8,900,608 bytes | 112,632 to 111,632 |
  | 64 MiB FAT32, 8 MiB image | 0xFFFFFE00 | 66,056,192 bytes | 0 |
  | 64 MiB FAT16 laid out as be5b725's p1, 8 MiB image | 0x6000000 | 66,955,264 bytes | 0 |

  `fsck.fat -n` finds each volume consistent and the other file on it unchanged [verified]. The
  read itself returns 0 bytes, so the guest does get zeros; the damage is the allocation. 0xFFFFFE00
  is what a 32-bit `off_t` makes of LBA 0x0FFFFFFF (`typedef long _off_t`, newlib `sys/_types.h`
  line 46 in the ESP toolchain) [verified: header; inferred: the truncation].
* The floppies are the same: `emulink_attach_floppy()` opens `"r+b"` (`misc.c` 478) and the
  guest's cylinder, head and sector become a seek with no bound (526 to 531) [verified]. A floppy
  image grown that way no longer matches a known size (483 to 490), and `pc.c` 862 to 863 then
  asserts at every boot of that ini until the image is cut back on a PC [inferred]. Separately, the
  drive number `args[0]` indexes `fdd[2]` with no bound in `exec_cmd()` (520) and in
  `emulink_data_write_string()` (569, 575), so a bad value reads `fdfmti[]` or the heap as a
  `FILE *` and the following `fwrite()` faults [inferred: struct layout, `misc.c` 416 to 426]. This
  is tiny386's own code, unchanged by v3.
* Who can trigger it: only a guest that goes past the size it is told. tiny386's geometry keeps CHS
  inside the file (`ide_hddrive_init()`, 1833 to 1840: cylinders rounded down), and Plat's images fit
  it exactly or with room: `freedos-hd.img` is 16 MiB raw (32,768 sectors, 32 cylinders of 1,008),
  `win95.img` is 818,496 sectors, 812 cylinders exactly [verified: sizes, no tiny386 header]. So
  ordinary FreeDOS and Windows 95 use stays inside [inferred]; a DOS disk tool that probes past the
  end, a guest bug, or an image copied in with another size can go past it [inferred].
* What it costs: p1 (`KORVO_SD`) is optional for be5b725's Linux, whose root is p2; its
  `/etc/init.d/S10sdcard` mounts p1 if it can and never stops the boot for it [verified: read from
  a copy of p2 of `main-be5b725\sdcard.img` with debugfs]. So a full p1 does not stop Linux
  [inferred]. It does fill the partition Plat's PC and Mac also write, the clusters it takes hold old
  deleted data that the guest can then read, and the allocation reaches the card only at the next
  flush two seconds after the last write, so a RESET in between leaves lost clusters for fsck
  [inferred]. It also makes FLASH.txt line 165 ("writes only lastboot.txt and the disk and floppy
  images the inis name"), TEST-PLAN.md risk 5 ("writing inside an image file allocates nothing") and
  line 102 untrue in this case.
* Fix: on `BUILD_ESP32`, in `bf_read_async()` and the read-write branch of `bf_write_async()`,
  return -1 when `sector_num + n > bf->nb_sectors` (the guest gets an IDE error, as in snapshot
  mode). In `misc.c`, refuse `args[0] >= 2` in both places, and a sector past the floppy format's
  end. Add a host test through ESP-IDF's FatFs (the storage test's harness has everything needed;
  `r2/stretch_test.c` is a start), and correct E F4's line in `REVIEW-RESOLUTION.md`.

**m2. Nothing is backed up before the first write, and "exactly what was there" assumes be5b725's
flash.bin is what is there.**

* The scripts read only the table, 0x8000 to 0x8BFF (`flash-dualboot.bat` 207 to 223, the same
  in the other two) [verified]. be5b725 and the fork release v2026.09.20 have the same table
  (520538d3...) [verified: `r2/pt-r2.txt`], so the scripts cannot tell which Linux is in the flash,
  and TEST-PLAN.md section 0 says either is fine [verified]. Their bootloaders differ: be5b725's is
  547c6a5b..., 22,000 bytes; the release's is 41d5a687..., 23,024 bytes [verified: `r2/img-r2.txt`].
* `back-to-linux.bat COM10` writes be5b725's bootloader and table (lines 154 to 158) [verified].
  FLASH.txt lines 96 to 97 and TEST-PLAN.md risk 6 say this "gives back exactly what was in 0x0 to
  0xC1FFFF before step 1", tagged [verified: simulation]; the simulation starts from be5b725's
  `flash.bin`, but which Linux is in Plat's flash is [reported] ("be5b725-era"). If it is the
  release, the way back leaves be5b725's bootloader in front of the release loader: that still
  boots [inferred: both bootloaders have the same RAM layout, and the release loader's highest RAM
  byte, 0x2F017837, is far below the bootloader's 0x2F06A2B0; `r2/img-r2.txt`], but it is not the
  same bytes.
* Fix: in `flash-dualboot.bat`, before the first write over a Linux table, read the whole flash to
  a file beside the scripts (`esptool -c esp32s31 -p PORT -b 921600 read-flash 0x0 0x1000000
  backup\korvo1-before.bin`, a few minutes [inferred]) and stop if that read fails; let
  `back-to-linux.bat` take that file as a source (`backup`: its 0x2000 to 0x8FFF for small, its
  0x0 to 0xC1FFFF for full). At the least, read 0x2000 to 0x8FFF and name the bootloader found by
  its SHA-256 (be5b725, release, dual boot, tiny386's own). Until then, write "exactly" in FLASH.txt
  4 and TEST-PLAN risk 6 as "exactly, if the flash held be5b725's flash.bin".

**m3. The scripts reset the board up to four times while Linux may be running with p1 mounted read
and write, and step 1 does not ask for a poweroff first.**

* be5b725's Linux mounts p1 read and write at every boot: `mount -t vfat -o rw,noatime
  /dev/mmcblk0p1 /mnt/sd` in `/etc/init.d/S10sdcard` [verified: debugfs, as in m1]. Plat's notes say
  a reset with it mounted leaves it dirty [reported].
* `flash-dualboot.bat` runs esptool four times: `read-mac` (line 212), `read-flash` (215), then the
  two writes (89 to 90, called at 110 and 113) [verified: script]. Each starts with esptool's
  default reset into download mode and ends with a hard reset [reported: DUALBOOT.md 8, from esptool
  5.4.0's `__init__.py`], so the first one resets a running Linux, and Linux starts again between
  the runs only to be reset by the next [inferred]. `back-to-linux.bat` does the same three times
  (206, 209, 112).
* TEST-PLAN.md risk 4 and step 2 (line 135) ask for `sync` or `poweroff` before the RESET into
  tiny386, but step 1 (line 106) and FLASH.txt 1 and 4 say nothing about it before the scripts
  [verified]. The hazard is the first round's m5, at the flashing step instead: Linux's unwritten FAT
  changes on p1 cut off by the first reset [inferred].
* Fix: TEST-PLAN step 1 and FLASH.txt 1 and 4: "in Linux, `poweroff` and wait, then run the
  script". In the scripts, give `read-mac`, `read-flash` and the first write `-a no-reset` (one of
  esptool 5.4.0's values for `-a` [verified: `dualboot\checks\esptool-help\main-help.txt` 25 to
  26]), so the chip stays in download mode until the last write's hard reset; after N at the
  prompt, say "press RESET" (or reset with one more esptool call).

### Notes

* **n1. `idf.py dfu-flash` is not refused.** The refusal in `esp/CMakeLists.txt` (45 to 57) covers
  flash, app-flash, bootloader-flash, partition-table-flash and the erase and encrypted variants
  named there, and `partition_table-flash` through its alias [verified: `ninja -t inputs` in the
  rel1 build-korvo1]. Not covered: `dfu-flash` (dfu-util over the chip's USB DFU, writing the build's
  bootloader, table and tiny386 at 0x20000, over the Linux loader); `otadata-flash` and
  `erase-otadata`, which write or erase 8 KiB of 0xFF at 0x17000, Linux's blank otadata, so change
  nothing [verified: `ota_data_initial.bin` is 8,192 bytes of 0xFF; inferred for the flash]; the
  other `encrypted-*-flash` targets stop by themselves without flash encryption development mode
  [verified: `ninja -t commands`]. `build-korvo1/flash_args` and `flasher_args.json` still list the
  harmful writes for any tool that reads them directly [verified: files]. Fix: add `dfu-flash` (and,
  for tidiness, `otadata-flash`, `erase-otadata`) to the list.
* **n2. Plat's p1 is probably FAT16, and the storage test only tries FAT32.** p1 of
  `main-be5b725\sdcard.img` is FAT16 (mkfs.fat, "FAT16" at 0x36, 2 KiB clusters, 512 root
  entries, label KORVO_SD) in a type 0x0C partition [verified: boot sector read]. The brief,
  TEST-PLAN.md and NOTES-core2.md call it FAT32, and `run_storage.sh` makes p1 FAT32 (`plat_card()`,
  lines 109 to 113) [verified]. `storage.c` handles FAT16 (`fat_vbr()` tests "FAT" at 0x36,
  `vbr_serial()` reads 0x27; lines 173 to 185) [verified: source], so no fault is expected
  [inferred]; add a FAT16 p1 to the test and say "FAT16 or FAT32" in the docs.
* **n3. The same image named twice is opened twice.** `check_config()` (`esp_main.c` 658 to 680)
  does not compare paths, and FatFs's file lock is off (`CONFIG_FATFS_FS_LOCK 0`, the Korvo-1
  build's `sdkconfig.h` line 868) [verified]. `hda` and `hdb` (or a disk and a floppy) on one file
  give two FatFs file objects with their own caches; with m1 both can extend it, leaving lost chains
  [inferred]. Fix: refuse a resolved path named twice.
* **n4. Only the MAC tells a standalone Korvo-1 from the DOS board.** Their tables are the same file
  (afbe6410...) [verified], and `flash-standalone.bat` (65 to 69) and `back-to-linux.bat COMx full`
  (87 to 90) accept it after printing the MAC for the eye [verified]. Once Plat has read the
  Korvo-1's MAC in step 1, pin it in `flash-paths.cmd` and have the Korvo-1 scripts refuse any
  other.
* **n5. `images\korvo1-dualboot\flash_args` writes in one sorted run.** Used as `esptool
  write-flash @flash_args`, it writes the bootloader first and tiny386 last, the order the first
  round's n1 moved away from in the script [verified: file, `build.sh` 128 to 130]. Harmless (a cut
  then leaves Linux booting without the key) [inferred]; FLASH.txt 1 already gives the two commands
  in the safer order.


## 3. The first round's findings in v3.1

| # | What I checked | Result |
|-|-|-|
| M1 | `back-to-linux.bat` 45 to 53: `SCOPE=small`, `LSET=be5b725` by default; `release` must be named | in [verified] |
| m1 | each script reads `read-mac` and 0x8000 to 0x8BFF before the prompt and accepts only its tables (`flash-dualboot.bat` 63 to 85, `flash-standalone.bat` 50 to 70, `back-to-linux.bat` 73 to 91); no port, no run | in [verified]; the reset side of it is m3 |
| m2 | FLASH.txt 133 to 138, TEST-PLAN risk 9, DUALBOOT.md 9 say it. I checked the claim further: a failed FAT mount does not call `wl_unmount()` (`vfs_fat_spiflash.c` 146 to 226, the `fail:` path), so the wear levelling's dummy sector is never moved; only `WL_Flash::init()`'s state and config sectors at the partition's end are written, once (`WL_Flash.cpp` `init()`, `updateWL()`; `wear_levelling.cpp` 186 to 210) | right as written [verified] |
| m3 | `esp/CMakeLists.txt` 45 to 57; FLASH.txt 35 to 40 | in [verified]; gaps in n1 |
| m4 | `flash-standalone.bat` 34 to 35, 46 to 47 pin ec0a396b and afbe6410 | in [verified] |
| m5 | FLASH.txt 70 to 77, TEST-PLAN risks 4 and 5, step 2 line 135 | in [verified]; not for step 1 (m3) |
| m6 | the raw card device exists only under `IDE_RAW_SD` (`ide.c` 2164 to 2240), which nothing defines; `ide_attach()` 2408 to 2415 refuses `/dev/mmcblk*` | in [verified: `git grep IDE_RAW_SD`] |
| n1 | `flash-dualboot.bat` 87 to 114: tiny386 in run 1, bootloader and table in run 2 | in [verified] |
| n4 | FLASH.txt 147 has `-fm keep -ff keep -fs keep` | in [verified] |
| n5 | `dosboard\images-old-v2\` | in [verified: listing] |
| n6 | `rom_exists()` (`esp_main.c` 132 to 148) refuses a flash partition over 256 KiB | in [verified] |


## 4. What I checked and found right

### 4.1 The dual-boot bootloader against the Linux one

* `dualboot\build\sdkconfig.linux-be5b725` against `sdkconfig.dualboot`: the only differences are
  the five `CONFIG_BOOTLOADER_APP_TEST`, `NUM_PIN_APP_TEST=42`, `APP_TEST_PIN_LOW`,
  `APP_TEST_PIN_HIGH` (not set) and `HOLD_TIME_GPIO=1` lines and the two table file names [verified:
  diff]. Settings that matter, the same in both: QIO 80 MHz 16 MB, bootloader at 0x2000, table at
  0x8000, single core, RTC watchdog 9 s, region protection, `SPI_FLASH_DANGEROUS_WRITE_ABORTS`,
  rollback symbols, RTC reserve 0, no skip-validate option of any kind, no factory reset pin, no
  secure boot, no flash encryption [verified: grep]. The UART ROM download mode choice is not there
  at all, since it depends on secure boot or flash encryption (ESP-IDF
  `components/bootloader/Kconfig.projbuild` 1236 to 1241); `CONFIG_SECURE_ROM_DL_MODE_ENABLED=y`
  is only the helper default, in both files and in the tiny386 build [verified].
* Headers [verified: `r2/img-r2.txt`]: magic E9, three segments, DIO, size code 4 (16 MB), chip 32,
  revisions 0 to 99, hash appended, the same entry point and segment addresses as be5b725's; one
  segment 0x78 bytes and another 0xDC bytes longer, the loader's segment the same 0x314C. ESP-IDF
  version string `v6.1-dev-8142-g188e3e55bb` in both.
* Size: 22,352 bytes from 0x2000 is 0x7750, below 0x8000, 2,224 bytes spare [verified: file size].
* The app disables the bootloader's RTC watchdog itself, since
  `CONFIG_BOOTLOADER_WDT_DISABLE_IN_USER_CODE` is not set (`esp_system/startup_funcs.c` 145 to 154)
  [verified], so tiny386 under the Linux bootloader is not reset after 9 s [inferred].
* `CONFIG_BOOTLOADER_APP_ROLLBACK_CONFIRM_ON_STARTUP` is on in the tiny386 build, and its hook
  returns at once for a partition that is not an OTA one (`app_update/ota_auto_confirm.c` 22 to 27,
  `esp_ota_ops.c` 1258 to 1260), so tiny386 never writes Linux's otadata [verified: source].

### 4.2 The partition tables

* be5b725's and the release's tables are one file; the dual-boot table's first 224 bytes equal it;
  entry 8 is `tiny386`, app, subtype 0x20, 0xE20000, 0x1E0000, ending at 0x1000000; the MD5 entry
  is right and the rest is 0xFF; `images\korvo1-dualboot\` and `dualboot\` hold the same table;
  the standalone and DOS board tables are one file [verified: `r2/pt-r2.txt`].
* Both Linux loaders find their partitions by type data, subtype 0x40 and name: be5b725
  `bootloader/main/main.c` 242, c288bc9 `main.c` 243 to 249 [verified: read only, in the two WSL
  trees], so an app entry is invisible to them [inferred].

### 4.3 Every flash address

* FLASH.txt 1, 3, 4, 5 and 6 and the three scripts write 0xE20000, 0x2000 and 0x8000 (dual boot),
  0x2000, 0x8000 and 0x10000 (standalone, DOS board), and the be5b725 or release lists for the way
  back, as each image folder's `flash_args` and the release's own `flash.bat` have them [verified:
  read line by line].
* be5b725's `flash.bin` is 0xC20000 bytes, 0xFF from 0x0 to 0x1FFF, with its bootloader at 0x2000,
  its table at 0x8000 and its loader at 0x20000 [verified: `r2/pt-r2.txt`]. Nothing written by any
  Linux set reaches 0xC20000; the release kernel (4,008,448 bytes) ends at 0x672A00, so its
  `linux.size` write at 0xA1FFF4 (whose sector erase starts at 0xA1F000) cannot meet it [verified:
  sizes].
* Every `SHA256SUMS` here checks: the top level (37 files), the three image folders (5 each) and
  `dualboot\` (90) [verified: `sha256sum -c`, before this file was written].
* Sizes: tiny386 865,904 bytes against 1,966,080 (dual boot) and 1,835,008 (standalone); the DOS
  board's 894,128 against 1,835,008 [verified].

### 4.4 The scripts, line by line, and the ways back

* All three: CRLF line endings throughout (cmd.exe's label search needs them) [verified: byte count];
  the port is required; an unknown word becomes the port, so a typo makes the board read fail and
  the script stop before writing [verified: argument loops]; esptool 5.4 or later; the fixed files
  pinned by SHA-256; the image checked for size and by `image-info`; the board read before the
  prompt; Y alone writes [verified].
* Each state the scripts leave has a way back: a cut tiny386 write leaves Linux's bootloader and
  table; a cut bootloader write leaves the old table, which the script accepts again; only a cut
  inside the table sector needs the two be5b725 files by hand, which FLASH.txt says [verified:
  scripts; inferred: esptool writes in address order, as DUALBOOT.md 12.2 found].
* With the key held and no valid tiny386, the bootloader resets until the key is let go, then boots
  Linux; with the Linux loader invalid, it falls back to tiny386 without the key (the first round's
  note) [reported: DUALBOOT.md 3.4 and the first round, from ESP-IDF's `bootloader_utility.c`].
* tiny386 never restarts itself: no `esp_restart()` call in the tree [verified: `git grep`]; panics
  halt (`CONFIG_ESP_SYSTEM_PANIC_PRINT_HALT`) [verified: `sdkconfig.h`]; a brownout resets the chip
  and the next boot is Linux unless the key is held [inferred].

### 4.5 Does tiny386 write flash, burn eFuses, or format anything

* Flash: the only writer linked is the wear levelling, reached only through a `storage` partition
  (`storage.c` 487 to 497), which the dual-boot table does not have [verified]. No NVS, no core dump,
  no OTA write (4.1) [verified: `sdkconfig.h`, `nm`].
* eFuses: no eFuse programming function is in the text of either ELF; the ROM's `ets_efuse_program`
  and `ets_efuse_write_key` appear only as the ROM linker script's fixed addresses
  (`esp32s31.rom.ld` 421 to 423, plain assignments) and nothing calls them [verified: `nm`, a full
  `objdump -d` of the Korvo-1 ELF].
* Card: every mount has `format_if_mount_failed = false` (`storage.c` 52, 443, 492) [verified]; the
  MBR is read only (`storage.c` 202); FatFs writes inside volumes only; `f_mkfs()` and `f_fdisk()`
  are linked but reachable only through the format flag [verified: source]. The MBR of be5b725's
  card image has zero boot code [verified: sector 0 of `main-be5b725\sdcard.img`], so neither FatFs
  nor `fat_vbr()` can take a card written from it for one volume [inferred]. FatFs trims the
  clusters of a file it truncates (`FF_USE_TRIM 1`, `ffconf.h` 276), which happens only to
  `lastboot.txt` [verified: source].
* Shared memory across a RESET: tiny386 keeps its log ring in LP SRAM, 0x2E000000 to 0x2E00200F
  (`console.c` 85 to 115) [verified: `nm`]; the Linux device tree uses PSRAM (0x50000000) and a
  DMA pool at 0x2F040000, nothing at 0x2E000000, and neither Linux loader uses RTC memory
  [verified: `esp32s31.dtsi` 15 to 50, grep of both loaders]. The ring is checked by a magic and its
  inverse before use [verified: `console.c` 104 to 107].

### 4.6 Odd cards, inis and keys

* No card: a message, then a retry every 2 s, forever; nothing written (`esp_main.c` 814 to 852)
  [verified].
* No ini: a stop with a message (870 to 876); corrupt ini: the first bad line warned (979 to 983),
  unknown keys warned, and `check_config()` stops on a missing BIOS or disk, a bad CPU or a memory
  size that does not fit, before the PC is built (634 to 701, 1000 to 1003) [verified].
* WIN9X missing: `/mnt/win` gives an error (549 to 556), and `/sdcard3/win95.img` fails `stat()`
  (670 to 675) [verified].
* exFAT: FatFs is built without it (`FF_FS_EXFAT 0`, `ffconf.h` 294), and `fat_vbr()` does not accept
  an exFAT boot sector, so such a partition is listed and left alone; an exFAT p1 gives the no-card
  loop [verified: source; inferred: the outcome].
* Two mounts of one partition cannot happen with distinct volume serials: the partition FatFs chose
  for `/sdcard` is found by its serial before any other is mounted (`storage.c` 205 to 248)
  [verified: source].
* A flash partition named as a disk fails `stat()` and stops; as a BIOS, only partitions up to
  256 KiB are read, never written [verified: `esp_main.c` 116 to 190, 669 to 675].


## 5. On how the risk ranks

The flash side is in good order; the remaining risks are on the card, where both systems write p1.
m3 and the first round's m5 together are the realistic way to damage p1: a reset with Linux's FAT
changes unwritten. m1 needs an unusual guest. None of them reaches p2 or the flash [inferred:
sections 2 and 4].


## 6. Summary of fixes, smallest first

1. TEST-PLAN step 1 and FLASH.txt 1 and 4: `poweroff` in Linux before any script (m3).
2. FLASH.txt 4 and TEST-PLAN risk 6: "exactly, if the flash held be5b725's flash.bin" (m2).
3. `esp/CMakeLists.txt`: add `dfu-flash` to the refused targets (n1).
4. `ide.c` and `misc.c`: range checks on the read-write path and the floppy drive number (m1), with
   a FatFs host test.
5. Scripts: a full read-back before the first write, `-a no-reset` between esptool runs, and the
   Korvo-1's MAC pinned (m2, m3, n4).
6. Storage test: a FAT16 p1 (n2); `check_config()`: one path, one disk (n3).


## 7. The first round's review, as written at 19:00 (unchanged)

The text below is the round 1 file exactly as it was; its section numbers and finding numbers are
the ones `REVIEW-RESOLUTION.md` uses.

# Review: boot safety and flash (tiny386 v3)

Written 2026-10-21 as an independent review of the v3 tree (WSL `~/tiny386-idf-scratch/v3/tiny386`,
tag v3 = 2bb1c76) and of the files in this folder. The question: can anything here brick Plat's
Korvo-1 or lose his Linux. Nothing was flashed, no serial port was opened, nothing ran on a board.
Nothing in the tree or in this folder was changed apart from adding this file. Work files are in
WSL `~/tiny386-idf-scratch/review-boot-safety-flash` (`img.py`, `pt.py`, `sim.py`, `cp.sh` and the
small `*.sh` greps).

Tags: [verified] read in the named source, or seen in a check I ran for this review; [inferred]
follows from verified facts; [reported] someone else's statement, not checked at its origin;
[unknown] only the board can say.


## 1. Verdict

No blocker found. The dual-boot set cannot touch Linux's flash beyond the bootloader and table
sectors, the dual-boot bootloader is Plat's own Linux bootloader plus the test-pin check and nothing
else, the table is Linux's byte for byte plus one entry, and the dual-boot tiny386 writes nothing to
flash and never formats or repartitions the card. The ROM download mode stays available whatever is
written, because no image burns an eFuse.

One major point: `back-to-linux.bat` with no arguments swaps Plat's be5b725 Linux for the release
Linux, which FLASH.txt itself calls the untried way back. Six minor points and a few notes follow;
none of them can brick the board, and each has a small fix.


## 2. Findings

### Major

**M1. back-to-linux.bat defaults to the untried Linux.** With no arguments it writes the whole
release v2026.09.20 set (`back-to-linux.bat` lines 7 to 14 and 36 to 38: `SCOPE=full`,
`LSET=release`) [verified]. Plat's board runs be5b725 [reported: task brief], and FLASH.txt
section 4 says "be5b725 is what this board ran, so 'small be5b725' or 'full be5b725' are the safe
ways back", and that the release kernel with the be5b725 card has not been tried [verified:
FLASH.txt; `dualboot\DUALBOOT.md` 7.1 marks it unknown]. TEST-PLAN.md step 2 uses
`small be5b725` [verified]. So the one-word command a person is most likely to type replaces the
loader, OpenSBI, kernel and initramfs in flash with a combination that has never booted this card.
What I checked to size the risk: both initramfs images only mount `/dev/mmcblk0p2` as ext4 and
`switch_root`, and call no fdisk, mkfs, resize or dd [verified: unpacked both `initramfs.cpio`,
read `init`], so the card is not at risk; the worst case is a Linux that does not come up, put right
with `back-to-linux.bat COM10 full be5b725` [inferred]. Fix: make `small be5b725` the default (it
restores 0x0 to 0xC1FFFF byte for byte, section 4.4 below), and require the word `release` for the
release set.

### Minor

**m1. Nothing checks which board, or which table, is on the port before writing.** All three
scripts default to COM10 and write after a Y, without reading the board [verified:
`flash-dualboot.bat` 21 to 27, `flash-standalone.bat` 22 to 23, `back-to-linux.bat` 36 to 44].
Cases this misses, all recoverable through ROM download mode [inferred]:
* the DOS board on COM10 once it exists: the Korvo-1 set would land on it, or `back-to-linux`
  would write Linux to it;
* a Korvo-1 with upstream v0.4.0 Linux (initramfs 0x3F0000 long, `persist` at 0xE10000
  [reported: `research\firmware-design.md` 6.1]): the dual-boot table would cut its initramfs to
  2 MiB and drop `persist`, and `small be5b725` would not undo that;
* the standalone set still in flash: `flash-dualboot.bat` then leaves no Linux loader at 0x20000,
  and the bootloader falls back to tiny386 with no key held (`bootloader_utility.c` lines 619 to
  621, "Falling back to test app as only bootable partition") [verified: ESP-IDF 188e3e55].
Fix: before the Y prompt, have each script run `esptool -p PORT read-flash 0x8000 0xC00
%TEMP%\table-now.bin` and accept only SHA-256 `520538d3...` (Linux) or `d41cba3c...` (dual boot)
for the Korvo-1 scripts, and print `esptool read-mac` so the board can be told apart. Make the port
a required argument once there are two boards.

**m2. The standalone build writes the flash when there is no card.** `storage_init()` mounts the
flash partition `storage` with wear levelling when the card does not mount (`storage.c` lines 487
to 497) [verified]. The standalone table (also the DOS board's) has `storage` from 0x600000 to
0xFFFFFF [verified: decoded `images\korvo1-standalone\partition-table.bin`]. `wl_mount()` runs
before FatFs (`vfs_fat_spiflash.c` line 164) and, finding no valid state, erases and writes its
state and config sectors at the end of the partition (`WL_Flash.cpp` lines 81 to 83 and 267 to
297) [verified: ESP-IDF source], so the last few 4 KiB sectors below 0x1000000 get written on the
first boot without a card [inferred]. That is the unused tail of the dual-boot tiny386 partition
(the v3 image ends at 0xEF2200) and no Linux partition, so it costs Linux nothing and a later
dual-boot install nothing [inferred]. But `dualboot\DUALBOOT.md` section 9 says "tiny386 writes no
other flash", which is true of the dual-boot build only. Fix: say so in FLASH.txt section 5 and
DUALBOOT.md 9; or mount `storage` only when the standalone ini list needs it.

**m3. `idf.py flash` on the Korvo-1 build does more damage than FLASH.txt says.** The build's own
`flash_args` write tiny386's own bootloader (no test pin, SHA-256 `ec0a396b...`) at 0x2000,
`ota_data_initial.bin` at 0x17000 (Linux's `otadata`) and tiny386 at 0x20000 [verified:
`rel5/tiny386/esp/build-korvo1/flash_args` and the bootloader's SHA-256]. FLASH.txt warns only about
0x20000. Recoverable with a full Linux write [inferred]. Fix: name all three in the warning, and
make the dual-boot build refuse to flash, for example a target that fails, added as a dependency
of `flash` and `app-flash` in `esp/CMakeLists.txt` when the dual-boot table is selected.

**m4. flash-standalone.bat does not pin its files.** The table is checked by size alone (3072
bytes, lines 121 to 133) and the bootloader by `image-info` alone (lines 98 to 119) [verified],
where `flash-dualboot.bat` pins both by SHA-256 (lines 32, 33, 43, 44) [verified]. Any 3072-byte
file passes, the dual-boot or Linux table included. Fix: pin `afbe6410...` and `ec0a396b...` (the
values in `images\korvo1-standalone\SHA256SUMS`), as the dual-boot script does.

**m5. Switching by RESET can leave p1's FAT half written.** Linux keeps p1 (FAT32 `KORVO_SD`)
mounted, and a reset leaves it dirty [reported: Plat's board notes]. tiny386 then writes p1:
`lastboot.txt` after a menu choice (`setup.c` 139 to 151, `bootmenu.c` 162 to 166) and the guest's
writes to `freedos-hd.img` [verified]. If Linux had FAT or directory changes still in its cache at
the reset, FatFs allocating a cluster on that FAT can cross-link files on p1 [inferred]. The other
way, tiny386 hands guest writes to the card only after 2 s with no new write (`esp_main.c` lines 50
to 52 and 337 to 361) [verified], so a RESET sooner than that loses the guest's last writes inside
its image, though not the card's FAT, since writes inside an existing image file allocate no
clusters [inferred: the guest sees the disk size the IDE emulation reports from the file]. Fix, in
TEST-PLAN.md and FLASH.txt section 2: in Linux run `sync` (better, `poweroff`) before holding VOL+
and pressing RESET; in tiny386 shut the guest down (or wait a few seconds after its disk light) before
RESET.

**m6. The raw whole-card disk is still in the build.** `ide_attach()` turns `/dev/mmcblk0` into a
raw block device over the whole card from sector 0 (`ide.c` lines 2357 to 2365, writes at 2152 to
2164) [verified], and `rawsd` is set to the FAT-mounted card (`storage.c` line 137) [verified]. A
guest given it could write the MBR, Linux's p1 and the ext4 root while FatFs has p1 and p3 mounted.
Today it cannot be reached: `check_config()` calls `stat()` on every disk path and stops on failure
(`esp_main.c` lines 617 to 631) [verified], and no ESP-IDF VFS has a prefix that matches
`/dev/mmcblk0` (registrations are `/dev/null`, `/dev/uart`, `/dev/usbserjtag`, `/dev/usj`; path
matching in `vfs.c` lines 841 to 870 needs a whole-component prefix) [verified: source], so
`stat()` fails and tiny386 stops before building the PC [inferred]. It is one change to
`check_config()` away from live. Fix: refuse `/dev/mmcblk0` in `ide_attach()` on the ESP build
when the card is FAT-mounted, or drop the branch.

### Notes

* **n1. Write order.** esptool sorts the address list (`esptool/cli_util.py` line 174, esptool
  5.4.0) [verified], so the dual-boot install writes the bootloader, then the table, then tiny386.
  A cut cable after the bootloader leaves the table sector erased and no Linux until the write is
  redone [inferred]; the ROM download mode is there for that (section 3) and FLASH.txt says so.
  Writing tiny386 first in its own esptool run would shorten the window; not needed.
* **n2. GPIO42 keeps its internal pull-up after the check** (`bootloader_common.c` line 43)
  [verified]. Nothing on the Linux side reads GPIO42 [reported: `dualboot\checks\13-misc.txt`;
  I found no GPIO42 use in the be5b725 loader's `bootloader/main/*.c` by grep, verified].
* **n3. Flash status register.** tiny386 is built QIO (`CONFIG_ESPTOOLPY_FLASHMODE_QIO`), so its
  flash driver sets the Quad Enable bit if it is clear (`spi_flash_common_set_io_mode` is linked)
  [verified: sdkconfig.h, nm]. The Linux bootloader has the same QIO setting [verified:
  `dualboot\build\sdkconfig.linux-be5b725`], so the bit is already set on every boot and the write
  never happens [inferred].
* **n4. FLASH.txt section 6** (DOS board, by hand) leaves out the three `keep` options that the
  .bat files spell out on purpose against `ESPTOOL_FM` and friends in the environment [verified:
  FLASH.txt; DUALBOOT.md 8]. Harmless with a clean environment [inferred]; add them for consistency.
* **n5. Stale DOS board image.** `dosboard\images\` (the DOS board work's v2-era build) sits beside
  `images\dosboard\` [verified: listing]. The integration notes flag it; renaming the folder to
  `images-old-v2` would stop a mix-up.
* **n6. BIOS from a flash partition.** `load_rom()` reads a whole partition named in `bios =` or
  `vga_bios =` with no size limit, "backward" below the BIOS top (`esp_main.c` 164 to 179), where a
  card file is capped at 256 KiB (lines 125 to 129) [verified]. `bios = linux` would read 7.5 MiB
  below guest memory and crash [inferred]. Reads only, so no flash risk; the crash halts and RESET
  gives Linux. On the standalone set the `bios.bin` partition holds old Linux loader bytes or 0xFF
  [reported: DUALBOOT.md 7.2], which only matters if the card has no `bios.bin` beside the ini.


## 3. What I checked and found right

### 3.1 The dual-boot bootloader against the Linux one

* Plat's flash holds be5b725's `bootloader.bin` (SHA-256 `547c6a5b...`, 22,000 bytes), and
  be5b725's `flash.bin` carries the same bytes at 0x2000 [verified: `pt.py`].
* The unchanged copy of the be5b725 bootloader project, built with ESP-IDF 188e3e55, differs from
  that file in 43 bytes: 0x48 to 0x5B (the build date and time string) and 0x55CF to 0x55EF
  (checksum and SHA-256) [verified: byte compare of `dualboot/ref/.../bootloader.bin`]. So the copy
  and ESP-IDF reproduce what is in the flash [inferred].
* The dual-boot build's generated `sdkconfig.h` differs from that reference build's in exactly four
  defines (`CONFIG_BOOTLOADER_APP_TEST`, `..._NUM_PIN_APP_TEST 42`, `..._APP_TEST_PIN_LOW`,
  `CONFIG_BOOTLOADER_HOLD_TIME_GPIO 1`) and the table file name [verified: diff of the two
  `config/sdkconfig.h`]; `sdkconfig.linux-be5b725` against `sdkconfig.dualboot` gives the same
  [verified]. The built `bootloader.bin` there is the shipped `b57469cd...` [verified: sha256sum].
* Settings that matter, identical in both [verified: sdkconfig]: QIO flash (header says dio, as
  ESP-IDF writes it), 80 MHz, 16 MB, table at 0x8000, bootloader at 0x2000, PSRAM octal 250 MHz with
  boot init, single core, RTC WDT 9 s, region protection, rollback symbols, no secure boot, no flash
  encryption, ROM log always on, no ROM download mode change, no factory reset pin, anti-rollback off.
* Headers [verified: `img.py`]: both bootloaders magic E9, three segments, dio, size code 4
  (16 MB), freq code 0xF, chip ID 32, chip revision 0 to 99, hash appended. Segment load addresses
  are the same; the dual-boot one is 0x78 bytes longer in DRAM and 0xDC in IRAM; the loader IRAM
  segment is the same length (0x314C).
* Size: 22,352 bytes against 24,576 (0x2000 to 0x8000), 2,224 spare [verified: file size; ESP-IDF's
  own check is quoted in DUALBOOT.md 3.3].
* The pin check [verified: ESP-IDF 188e3e55 `bootloader_common.c` 37 to 54,
  `bootloader_start.c` 84 to 130]: pull-up on, one read, low for the whole second picks the `test`
  partition, anything else boots as before. With the key held and no bootable image at 0xE20000 it
  resets (`bootloader_utility.c` 582 to 588), so the board resets until the key is let go, then
  boots Linux [inferred]. FLASH.txt section 2 says exactly this.
* Rollback: the bootloader rewrites an `otadata` entry only when it reads "pending verify"
  (`bootloader_utility.c` 393 to 399) [verified]; Linux's `otadata` is 0xFF in `flash.bin`
  [verified], and the Linux bootloader does the same thing anyway.

### 3.2 Will each bootloader load each app

ESP-IDF refuses an app segment that overlaps the bootloader's loader text, its static data or its
stack (`esp_image_format.c` lines 425 to 519) [verified]. For the dual-boot bootloader those are
0x2F06CFB0 to 0x2F0700FC and 0x2F073FB0 to 0x2F07581C [verified: nm of `bootloader.elf`], with
the app limit fixed at 0x2F06CFB0 (`bootloader.memory.ld.in` ASSERT) [verified]. Highest RAM
byte loaded [verified: `img.py`]: be5b725 loader 0x2F0174BF, release loader 0x2F017837, tiny386
Korvo-1 0x2F036B1F, DOS board 0x2F0378AF. All far below. Every app has chip revision 0 to 99,
eFuse block revision 0 to 99 and a 64 KiB MMU page, as the bootloaders expect [verified]. The
release bootloader (ESP-IDF "v6.1") has the same layout, so `small release` would also load the
be5b725 loader [inferred].

### 3.3 The partition tables

* be5b725's and the release's tables are the same file (`520538d3...`) [verified].
* The dual-boot table's first 224 bytes (the seven Linux entries) equal Linux's; entry eight is
  `tiny386`, type app, subtype 0x20 (test), 0xE20000, 0x1E0000, ending at 0x1000000; the MD5 entry
  is right; the rest is 0xFF [verified: `pt.py`]. `images\korvo1-dualboot\partition-table.bin`
  is the same file as `dualboot\partition-table.bin` [verified].
* 0xE20000 to 0xFFFFFF is outside every Linux table from be5b725 to v0.6.0 [reported:
  `research\firmware-design.md` 6.1], so tiny386 there never overwrites any Linux's data.
* The Linux loader finds `opensbi`, `linux` and `initramfs` by name (be5b725
  `bootloader/main/main.c` 240 to 245, 265, 333, 363) [verified], so an extra app entry is invisible
  to it [inferred].
* The standalone and DOS board tables are one file (`afbe6410...`): factory 0x10000, 0x1C0000,
  which matches `SA_APP_SIZE` 1,835,008 [verified].
* `build.sh` line 120 refuses to package a Korvo-1 build whose own table is not the dual-boot one
  [verified].

### 3.4 Every flash address

* `flash-dualboot.bat`: 0x2000 bootloader, 0x8000 table, 0xE20000 tiny386 (`app`: 0xE20000 only);
  pins `b57469cd...` and `d41cba3c...`, which are the files; `APP_MAX` 1,966,080 is 0x1E0000
  [verified]. FLASH.txt sections 1 and 3 give the same commands [verified].
* `flash-standalone.bat`: 0x2000, 0x8000, 0x10000, as in `images\korvo1-standalone\flash_args`
  [verified].
* `back-to-linux.bat`: the release list is the release's own `flash.bat` list plus 94,208 bytes of
  0xFF at 0x9000 (to 0x1FFFF); the be5b725 lists are its `flash_args` pair and `flash.bin` at 0x0;
  all ten distinct pinned SHA-256 values match the files [verified: `sha256sum -c` on
  `dualboot\SHA256SUMS`, sha256sum of the be5b725 folder].
* `images\*\SHA256SUMS` and the top-level `SHA256SUMS` all check out [verified].
* My own simulation (`sim.py`, a 16 MiB flash starting from be5b725's `flash.bin`, sorted writes,
  4 KiB erase) [verified]: the dual-boot install changes only 0x2000 to 0x8FFF and 0xE20000 to
  0xEF2FFF, 0x9000 to 0xE1FFFF untouched; `small be5b725` then gives back 0x0 to 0xC1FFFF byte
  for byte; after the standalone set, `full be5b725` gives back 0x0 to 0xC1FFFF byte for byte.
  This agrees with `checks\flash-simulation.txt`.
* esptool rewrites a header only at 0x2000 and only when the options are not all `keep`
  (`cmds.py` 740 to 743) [verified], so the pinned bootloader's hash survives.

### 3.5 Does tiny386 write flash, burn eFuses, or format anything

* Linked into each ELF [verified: nm]: `esp_partition_write`, `wl_mount`, `f_mkfs`, `f_fdisk`,
  `sdmmc_write_sectors`, the OTA confirm code. Not linked: `nvs_flash_init`, any
  `esp_efuse_write*`, `esp_vfs_fat_sdcard_format`.
* Flash, dual-boot build: `wl_mount` is reached only through the `storage` partition, which the
  dual-boot table does not have (`storage.c` 487 to 489) [verified]; `wifi.c` (the only NVS user)
  is not built for the Korvo-1 or the DOS board (`esp/main/CMakeLists.txt` 10 and 46 to 48)
  [verified]; the OTA auto-confirm returns at once for a `test` or `factory` partition
  (`ota_auto_confirm.c` 22 to 27, `esp_ota_ops.c` 1258 to 1260) [verified]; core dump is off
  (`CONFIG_ESP_COREDUMP_ENABLE_TO_NONE`) [verified]. The standalone build: see m2.
* eFuses: the start-up code burns only under secure boot, flash encryption, ROM download mode or
  ROM log settings that are all off (`esp_efuse_startup.c` 91 to 155) [verified], and no eFuse
  write function is linked [verified: nm]. So ROM download mode cannot be lost [inferred].
* Card: every mount has `format_if_mount_failed = false` (`storage.c` 52, 443, 492) [verified],
  and ESP-IDF partitions or formats only with that flag (`vfs_fat_sdmmc.c` 112 to 117) [verified].
  The firmware's own writes are `lastboot.txt` and the guest's disk and floppy images (`ide.c`
  2016 to 2022, `misc.c` 572 to 575) [verified]. No UHS mode is asked for, so the card never leaves
  3.3 V signalling and Linux can initialise it after a RESET [verified: `storage.c` 76 to 121 sets
  no UHS flag; inferred for the rest].

### 3.6 Odd cards, inis and keys

* No card: retried every 2 s, forever, nothing written (`esp_main.c` 767 to 807) [verified].
* No ini: stops with a message and waits for RESET (`esp_main.c` 826 to 831, `console.c` 610 to
  623) [verified]. Panics also halt (`CONFIG_ESP_SYSTEM_PANIC_PRINT_HALT`) [verified]; tiny386
  never restarts itself, so RESET without the key always gives Linux [inferred].
* Corrupt ini: the first bad line is reported, unknown keys are warned about, and `check_config()`
  stops on a missing BIOS or disk before the PC is built (`esp_main.c` 929 to 958, 589 to 656)
  [verified].
* WIN9X missing: `/mnt/win/...` is an error (504 to 511); `win95-14m.ini`'s `/sdcard3/win95.img`
  fails `stat()` and stops (617 to 631) [verified].
* exFAT partition: FatFs here has `FF_FS_EXFAT 0` (`ffconf.h` 294) [verified], so exFAT is skipped
  [inferred], and `storage_sd_mount_others()` mounts only partitions with a FAT boot sector
  (`storage.c` 173 to 180, 238 to 240) [verified]. The ext4 p2 is never mounted or written
  [verified: same lines]. An exFAT-only card gives the "no card" loop [inferred].
* Key held, no tiny386 image: reset loop until let go, then Linux (3.1).
* A DOS board image put on the Korvo-1 by mistake: before the probe it drives only GPIO2 (the
  Korvo-1's codec MCLK, an S31 output there) and I2C on GPIO0 and 1, and it halts on a missing
  MS7210 (`dosboard.c` 248 to 275) [verified: code; reported: Korvo-1 pin use in
  `research\s31-module.md`]. Electrically harmless [inferred]; the flash damage is the Linux loader,
  as FLASH.txt section 6 says.


## 4. On the third USB-A port and flashing (Plat's question)

From this review's side: the DOS board image does not build the S31's own USB host
(`esp/main/CMakeLists.txt` lines 52 to 59: `usb_input.c` only for other boards) [verified], so the
firmware has no use for the S31's high-speed USB pins. Flashing goes through UART0 and the CH340C,
with esptool entering the ROM's download mode through DTR and RTS, as it does through the CP2102N on
the Korvo-1 [inferred from the Korvo-1 practice in FLASH.txt]. The download mode is chosen by the
GPIO61 strap at reset, so no firmware can block it while the eFuses stay as they are, and none of
these images writes an eFuse (3.5) [verified for the images; reported for the strap:
`research\s31-module.md` section 3]. A BOOT button on GPIO61 is the fallback if the auto-program
circuit ever misbehaves [reported: same table].
