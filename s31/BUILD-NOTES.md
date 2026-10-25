# tiny386 v3: build notes

Written 2026-10-21. tiny386 (an x86 PC emulator) bare metal under ESP-IDF, for the
ESP32-S31-Korvo-1 beside Linux and for the DOS board, from one tree. This note covers the merge
of the three pieces of work before it (the firmware core in two parts, the dual boot, the DOS
board), the first review round that followed it (four independent reviews; their fixes made the
app version `v3.1`, section 10), the second review round (the same four lenses again; their fixes
make the app version `v3.2`, section 12), the build, the images and what was checked. Nothing was
flashed, no serial port was opened, nothing was committed anywhere but the scratch tree's own
git. Nothing here has run on a board.

Sections 1 to 11 are the record of v3 and v3.1 as they were written; where v3.2 changed a figure,
section 0, 4.1, 4.2, 6, 7, 8 and 9 give the v3.2 figure and section 12 the rest. Section 13 is a
check of v3.2 made the next day that changed nothing. Section 14 is v3.3 (2026-10-22): the status
LED on both boards and the DOS board's line out; its figures (sizes, image hashes, test counts)
replace section 0's, which are left as v3.2's. v3.2's own files are in `previous-v3.2\`. Section 15
is v3.4, the same day: the fixes from the review of v3.3 (`REVIEW-v32.md`); its figures replace
section 14's, and v3.3's own files are in `previous-v3.3\`. Section 16 is v3.5, the same day again:
the DOS board's rev A2c pins, its speaker amplifier and a louder sound; its figures replace section
15's for the DOS board, and v3.4's own files are in `previous-v3.4\`. Section 17 is v3.6, the same
day: the fixes from the review of v3.5 (`REVIEW-v35.md`); its figures replace section 16's, and
v3.5's own files are in `previous-v3.5\`. Section 18 is v3.7, the same day: the status LED much
dimmer by default and its brightness set in the ini (`[esp] led_brightness`); its figures replace
section 17's, and v3.6's own files are in `previous-v3.6\`. Section 19 is v3.8's first half, the
same evening: the fix for Windows 95's crashes (the PIT's catch-up, the card's FAT walks, the
386's double fault) and diagnostics, from the three investigations in `research\`; no image was
built for it. Section 20 is v3.8's second half, the same night: the status LED's disk light, a
quieter log, and v3.8 built and packaged, four image sets with tiny386 the default at reset as the
fourth; its figures replace section 18's, and v3.7's own files are in `previous-v3.7\`. Section 21
is v3.9, that night and the next morning: the fixes from the two reviews of v3.8 (the flash scripts
after a write cut short, the abort's cause on the UART, lines held back at a crash, the stall line
logged); its figures replace section 20's, and v3.8's own files are in `previous-v3.8\`. Section 22
is v4.0, 2026-10-23: v3.9 merged with tiny386's master of 2026-10-22, and Windows at the panel's own
size (800x480, 800x600 on the DOS board) through a VBE display driver, VBE pictures drawn again only
where the guest wrote, and 800x480 modes in the VGA BIOS; its figures replace section 21's, and
v3.9's own files are in `previous-v3.9\`. Section 23 is v4.1, the same day: the fixes from the two
reviews of v4.0 (no store marked where nothing reads the mark, mode 13h's above all; chain 4
switched under VBE; the accessors' comments; a DOS board ini for Windows; the card's and the
docs' points); its figures replace section 22's, and v4.0's own files are in `previous-v4.0\`.
Section 24 is Plat's report on v4.1 at 800x480 (the mouse pointer and anything moving flickering) and
the two investigations of it on the PC (`research\FLICKER-REPRO.md`, `research\FLICKER-AUDIT.md`).
Section 25 is v4.2, the same day: the cause in the display driver's own pointer and the one-byte
change to the driver that cures it (`qemumini-pointer\`), a clear of the screen with no black on the
panel, and Ctrl+Alt+F11 for `vbe_blocks`; its figures replace section 23's, and v4.1's own files are
in `previous-v4.1\`. Section 26 is the review of v4.2 (`REVIEW-v42.md`), the same night: no firmware
change (v4.2's images stay), the harness made to see a v4.2 clear, the install from the changed
floppy run, a text screen at Windows' restart from 16 colours newly seen (older than v4.2), and the
docs corrected; where it corrects section 25, section 25 says so. Section 27 is v4.3, 2026-10-24:
v4.2 with the information port at 0F1F8h (the branch `perfport`, which T386MON reads) merged in,
nothing else changed for the guest; its figures replace section 25's, and v4.2's own files are in
`previous-v4.2\`. Section 28 is the review of v4.3 (`REVIEW-v43.md`), the same day: no firmware
change. Section 29 is the first part of v4.4, the same day, on the branch `s31-v4.4` of Plat's
fork: the display (Ctrl+Alt+F11's setting saved in the ini, the grey text screen at a restart,
what a picture costs, the text test, double buffering and pacing for Plat to choose between); no
image is made for it, and part 2 adds the storage work, packages v4.4 and gives its figures.
Section 30 is that part 2, the same day: a partition of the card as the guest's whole disk
(`hda = sd:3`), the new card laid out for it (`newcard\`), and v4.4 built, checked and packaged;
its figures replace section 27's, v4.3's own files are in `previous-v4.3\`, and v4.4 is the tag
`s31-v4.4` on the `esp32-s31` branch, which from now on is the source of truth. Section 31 is v4.5,
the same day: the fixes from the two reviews of v4.4; its figures replace section 30's, and v4.4's
own files are in `previous-v4.4\`. Section 32 is v4.6, 2026-10-25: Plat's run of v4.5 answered
(double buffering's two frame buffers made at power-on, pacing left off), upstream's fix of the
device window with three memory checks of our own, and the branch's history rewritten; its figures
replace section 31's, and v4.5's own files are in `previous-v4.5\`.

Tags:

* [verified] read in the named source, or seen in a build, test or run made for this note
* [inferred] follows from verified facts; not built or run
* [reported] someone else's figure or statement, not checked at its origin
* [unknown] only the board can say

Where a line names one of the earlier notes (`NOTES-core1.md`, `NOTES-core2.md`,
`dosboard\NOTES-dosboard.md`, `dualboot\DUALBOOT.md`) or a review (`REVIEW-*.md`), that note's own
tag and source stand. The evidence for this note is in `checks\`.


## 0. In one screen

This section is v3.2's (section 12); the v3.1 figures it replaced are in sections 5 and 10 to 11.

* One tree, tiny386 9ed786b plus `tiny386-idf-v3.patch` (111 files, `git diff pristine v3.2`),
  builds for `BOARD=korvo1` and `BOARD=dosboard`. Tag `v3.2` (d3c839c) is `v3.1-tests` and five
  commits with the second review round's fixes; the app calls itself `v3.2` (section 12, and
  `REVIEW-RESOLUTION.md` for each finding). `build.sh` makes it from nothing; two runs in different
  folders gave the same bytes in all 18 files of the three image sets, ELF files included, and the
  patch applied to a fresh clone gives the `v3.2` tree exactly (tree 0292cb65) [verified:
  `checks\reproducible-v3.2.txt`].
* Compiler warnings: one, the old `i2s.c` "`i2s_task` defined but not used" on the Korvo-1, none on
  the DOS board. Every firmware file of the two review rounds, compiled again with `-Wall
  -Wextra`, gives none on either board; the shared core files this round changed give only what
  v3.1's same files give (tiny386's own) [verified: `checks\build-sh-v3.2-run1.txt`,
  `checks\warnings-wall-wextra-v3.2.txt`, `checks\warnings-core-files-v3.2.txt`].
* All 24 host tests pass: v3.1's 19 (several with new cases) and five new runs: `vga.c` on the DOS
  board's 800x600, the two-thread mouse test with the emulator held up, the `usb_input.c` test
  failing on v3.1's file, `ide.c`'s disk images through ESP-IDF's own FatFs (with a run against
  v3.1's `ide.c` that has to fail, and does), and the floppies. They pass again with an output
  folder 109 characters long, and in the tree `build.sh` made from the patch (22 of 22: the two
  runs that need the old tags are left out there). The two-thread mouse test runs clean under
  ThreadSanitizer, held up or not, and the disk test under AddressSanitizer and UBSan [verified:
  `checks\host-tests-v3.2.log`, `checks\host-tests-v3.2-long-out.log`,
  `checks\host-tests-v3.2-from-patch.log`, `checks\tsan-ps2-mouse-v3.2.txt`,
  `checks\asan-disk-v3.2.txt`].
* The Linux route, rebuilt from copies with the v3.2 core files put in, still gives a binary
  byte-identical to its release `tiny386-korvo` (dd34a04f; 0 of 21 objects differ: every change to
  a shared file is behind an ESP define), and its own 26 tests pass; `vgatest.c` runs 6,000 rounds
  against the v3.2 `vga.c` with nothing wrong [verified: `checks\linux-route-*-v3.2.txt`].
* Images in `images\`: `korvo1-dualboot\` (Linux stays; tiny386 at 0xE20000),
  `korvo1-standalone\` (tiny386 replaces Linux), `dosboard\`. The bootloaders and tables are the
  same files as v3's and v3.1's; only `tiny386.bin` and `tiny386.elf` changed [verified]. The
  scripts read the board before writing and stop on a table they were not made for; over Linux's
  table they name the bootloader and, the first time, copy the whole flash into `backup\`
  before writing; they keep the chip in download mode between their esptool runs; and
  `back-to-linux.bat COMx` puts the backup's 0x2000 to 0x8FFF back. Their flow, the backup
  included, was run with a stub esptool: 39 cases, all as intended, no write reached the stub
  [verified: `checks\bat-harness-v3.2.txt`]. The flash simulation, run again with these images,
  leaves every Linux partition as it was [verified: `checks\flash-simulation-v3.2.txt`]. The
  dual-boot build refuses `idf.py flash`, `dfu-flash` and the otadata targets among them
  [verified: `checks\cmake-flash-refusal-v3.2.txt`].
* Sizes: Korvo-1 `tiny386.bin` 867,392 bytes, 56 per cent of its 1,920 KiB partition free;
  internal RAM left after code and static data 262,454 bytes. DOS board 896,432 bytes and 259,424
  bytes [verified: `checks\size-*.txt`].
* `TEST-PLAN.md` is Plat's first boot, with every risk to his Linux install at its top;
  `card\tiny386\` the files for the card.


## 1. What v3 is

WSL tree `~/tiny386-idf-scratch/v3/tiny386`, branch `integ`, tag `v3` (2bb1c76), and tag `v3.1`
(80e64de) with the review round's five commits on top (section 10); `pristine` is tiny386 9ed786b
[verified: `git log`]. On top of the core's commits (`NOTES-core1.md` 1, `NOTES-core2.md` 1,
ending at f0e6ba2, tagged `core2-end`), v3 is:

| Commit | What |
|-|-|
| 61f476a | the DOS board work's c6f918f, picked across; `esp/main/CMakeLists.txt` and `esp/main/esp_main.c` kept as the core had them (they conflicted) |
| 3dd53da | the DOS board work's 1bfe79b (a host test argument) |
| 387939d | the merge proper (section 3) |
| eb4d5e4 | `esp/version.txt`: the app's version is `v3` |
| 3129162 | comment rules of `=` rather than `-` in the DOS board's files, as the core's |
| 2bb1c76 | `dosboard.c`: the lines kept for the console under a lock (the hot plug task can add one while `app_main()` does) |

`tiny386-idf-v3.patch` is `git diff pristine v3.1-tests`: 106 files, 16,839 lines added, 661
taken out [verified: `tiny386-idf-v3.diffstat.txt`]. `v3.1-tests` (823d5cd) is `v3.1` and one
commit that adds `tests/host/storage/` and its 17 lines in `tests/host/run.sh`, nothing else
(section 11). Applied with `git apply` (index and files) to a fresh clone at 9ed786b it gives the
`v3.1-tests` tree exactly (tree cec859c2) [verified: `checks\reproducible-v3.1-tests.txt`]. Before
the resumed run the patch was `git diff pristine v3.1`: 92 files, tree e5f89dcb [verified:
`checks\reproducible.txt`].

`esp/dependencies.lock` stays out of the patch, as in v2: `build.sh` copies `dependencies.lock`
from this folder (v2's, the resolved one) into the tree [verified: `cmp` against v2's].


## 2. Building

In WSL, with the boards' ESP-IDF (v6.1-dev-8142-g188e3e55bb at `~/esp/esp-idf`, its tools at
`~/.espressif-upstream`):

```
bash /mnt/d/Repos/esp32-s31-linux-release/tiny386-idf/v3/build.sh WORKDIR
```

It clones tiny386 at 9ed786b, applies the patch, runs `make prepare` (the OPL tables), copies
the lock file, and makes three builds of the one tree:

| Build folder | Board | Partition table | tiny386 at |
|-|-|-|-|
| `build-korvo1` | `korvo1` | `partitions-korvo1-dualboot.csv` (`sdkconfig.korvo1`) | 0xE20000, test partition, 1,920 KiB |
| `build-korvo1-standalone` | `korvo1`, `-DPARTITION_CSV=partitions.csv` | tiny386's own | 0x10000, factory, 1,792 KiB |
| `build-dosboard` | `dosboard` | tiny386's own (`sdkconfig.dosboard`) | 0x10000, factory, 1,792 KiB |

then puts the image sets together in `WORKDIR/images` (section 4) and checks each bootloader and
app with `esptool image-info` (valid checksum and hash). On this PC a run took three to eight
minutes, depending on what else was running [verified: file times of the runs]. It writes nothing outside WORKDIR and never
flashes.

`-DPARTITION_CSV=file` is new in `esp/CMakeLists.txt`: the board's sdkconfig with only the
partition table's file name changed (written into the build folder). The standalone and dual-boot
builds' `sdkconfig.h` differ in those two lines only [verified: `diff`].

Never `idf.py flash` the Korvo-1 dual-boot build: its own `flash_args` would put tiny386's own
bootloader at 0x2000, the otadata at 0x17000 and `tiny386.bin` at the factory partition,
0x20000, which is the Linux loader's (`NOTES-core1.md` 6, `REVIEW-boot-safety-flash.md` m3).
Since v3.1 the build refuses: with the dual-boot table selected, every flashing target depends on
a target that fails before esptool runs [verified from the build graph, `ninja -t inputs`, in all
three builds: `checks\cmake-flash-refusal.txt`; no flash target was run]. The image sets' own
`flash_args` are right (section 4).


## 3. The merge

### 3.1 The DOS board on the core's interfaces

What the DOS board work had built on v2, and where it is now [verified: source, builds]:

* **Panel.** `lcd_dosboard.c` now gives what `lcd.h` asks, as `lcd_korvo1.c` does: `lcd_init()`,
  `lcd_flush()` (write-back of whole rows with `esp_cache_msync()`, timed; nothing with bounce
  buffers), `lcd_set_invalidate()`, `lcd_get_stats()`. Its own `vga_task()` and `lcd_draw()` are
  gone: the drawing loop is `display.c`'s for both boards. One panel file per board.
* **Start-up order on the DOS board**, in `app_main()`:
  1. `board_early_init()`: the board check (MS7210 chip ID A2 20 0A at I2C 0x59); on any other
     board it stops here with a message on the UART, before a pin new to the DOS board is
     driven (unchanged from `NOTES-dosboard.md` 9).
  2. `lcd_init()`: mounts the card early and reads `dosboard.ini`, starts the 800x600 output
     (PCLK runs), draws the test card, sets the MS7210 up (it meters PCLK and locks to it),
     holds the test card `test_card_ms`, clears.
  3. The boot console, as on the Korvo-1, then `board_console_ready()`: the lines noted before the
     console existed (the board check, the settings, the MS7210's chip ID, PCLK meter, monitor
     and PLL lock) go onto it, in yellow where they say something failed.

  `board_early_init()` and `board_console_ready()` are weak and empty in `esp_main.c`; the
  Korvo-1 links the empty ones [verified: `nm` of both ELF files].
* **Keyboard and mouse.** `ch9350.c` hands its frames to `kbd_input.c`, the layer the Korvo-1's
  USB host uses: keyboard reports to `kbd_input_report()`, the mouse to `kbd_input_mouse()`,
  frame 0x86 to `kbd_input_release_all()`. So the DOS board gets typematic repeat, the keyboard
  lights (`kbd_input_set_leds_fn()` stores them for the CH9350L task's LED frame), the boot
  menu and Ctrl+Alt+F12. The stand-in `ch9350_kbd.c` and `.h` are gone, and with them the
  "one line in the 0xED hook" the DOS board note asked for. `ch9350_setup()` is called where the
  Korvo-1 calls `usb_setup()` (`input_setup()` in `esp_main.c`: for the boot menu, or once the
  ini is read), not from a drawing task; the chip is reset there and its devices attach again.
  `usb_input.c` is not built for the DOS board, whose own USB pins are not connected.
* **`[dosboard]` settings** come from `dosboard.ini` beside the inis (`/sdcard/tiny386/`, else
  `/sdcard/`), read before the panel starts, since the PCLK edge and the bounce buffers are
  needed then. The boot menu leaves `dosboard.ini` out (`setup.c`, host-tested); a `[dosboard]`
  section in a PC's ini draws a yellow line saying where it goes. `card-dosboard\tiny386\dosboard.ini`
  lists every key with its default. The DOS board work read them from `tiny386.ini` instead,
  which now is one of the PC inis.
* **`board_dosboard.h`**: no `FULL_UPDATE`; the core's `VGA_RETRACE_REALTIME`,
  `VGA_REDRAW_DIRTY_ROWS`, `VGA_SHRINK_LARGE`, `LCD_FRAME_MS 30`, `BOARD_NAME`;
  `PSRAM_KEEP_FREE` 256 KiB without the frame buffer (`NOTES-core2.md` 8); `USER_KEY_GPIO 42`, so
  the USER key held shows the speed console. Pins unchanged, as checked against the brief by the
  DOS board work (`DESIGN-BRIEF.md` as of 14:57 on 2026-10-21) [verified: file time].
* **`sdkconfig.dosboard`**: the core's `CONFIG_FATFS_VOLUME_COUNT=4` and `CONFIG_FATFS_USE_LABEL`
  added, comments brought into line; the PSRAM test at boot stays on for a new board's first
  boots, as the DOS board work set it.
* **`storage_sd_mount()`** returns at once once the card is mounted, so the DOS board's early
  mount and `app_main()`'s later one do not clash.
* The DOS board's host tests moved from `hosttest/dosboard` to `tests/host/dosboard` and run from
  `tests/host/run.sh`. The encoder table check reads `encoder.md` from `ENCODER_MD` (default its
  place on D:) and says SKIP without it.

### 3.2 What changed for the Korvo-1

Against core part 2 (f0e6ba2), for the Korvo-1 build [verified: `git diff core2-end v3`]: the
weak hooks (empty), `input_setup()` around the same `usb_setup()` calls, `setup.c` leaving
`dosboard.ini` out of the menu, the `[dosboard]` warning, `storage_sd_mount()` as above, the
version string, `-DPARTITION_CSV`. The review round's changes are section 10.

### 3.3 AUDIO_RATE

From the DOS board work, unchanged: `audio_rate.h` (44,100 by default), used by `i2s.c`,
`pcspk.c`, `adlib.c` and `sb16.c`; the DOS board header sets 48,000. The Linux route's binary
built with these files is byte-identical to its release (section 5.3), so at the default rate
they change nothing there [verified]. Its SB16 resampler fix (`dosboard\sb16-resampler.patch`) is
not applied: without it the SB16 plays 44,100 Hz sound about 8.8 per cent fast at 48 kHz, which
matters only on the DOS board, and with it the Korvo-1 and the Linux route change too
(`NOTES-dosboard.md` 8). That is Plat's call.

### 3.4 Left as the earlier work had it

The dual boot's bootloader and table, as the dual-boot work made and checked them
(`dualboot\DUALBOOT.md`). The core's notes stand for everything else, as section 10 amends them.


## 4. The images

### 4.1 What is in each set [verified: `images\*\SHA256SUMS`, `checks\image-info-*.txt`]

| Set | File | Offset | Bytes | SHA-256 (first 16) |
|-|-|-|-|-|
| `korvo1-dualboot` | `bootloader.bin` (the Linux loader's bootloader with the GPIO42 test-partition key) | 0x2000 | 22,352 | b57469cd1542fe83 |
| | `partition-table.bin` (Linux's seven partitions and tiny386, test, 0xE20000) | 0x8000 | 3,072 | d41cba3cf33bedee |
| | `tiny386.bin` (v3.2) | 0xE20000 | 867,392 | d0bc0520f1f914cc |
| `korvo1-standalone` | `bootloader.bin` (tiny386's own, the same bytes as v2's) | 0x2000 | 22,016 | ec0a396b5bd96406 |
| | `partition-table.bin` (tiny386's `partitions.csv`, the same bytes as v2's) | 0x8000 | 3,072 | afbe6410c2040c5e |
| | `tiny386.bin` (v3.2) | 0x10000 | 867,392 | 48d99ea17c6799c3 |
| `dosboard` | `bootloader.bin` (the same as the standalone one) | 0x2000 | 22,016 | ec0a396b5bd96406 |
| | `partition-table.bin` (the same as the standalone one) | 0x8000 | 3,072 | afbe6410c2040c5e |
| | `tiny386.bin` (v3.2) | 0x10000 | 896,432 | 2c85e71d2615853e |

(v3.1's were 865,904 bytes, edef0eee... and 80fe3b74..., and 894,128 bytes, a71bd4f2...; the
bootloaders and tables have not changed since v3 [verified: `checks\reproducible-v3.2.txt`].)

Each set also has `flash_args` (esptool's offsets, the files named as they are in the folder),
`SHA256SUMS` and `tiny386.elf`, which turns the addresses on a crash screen into function names.
The dual-boot pair is copied from `dualboot\`, checked by SHA-256 against the values the dual-boot
work pinned, and the Korvo-1 build's own table is checked to be the same bytes; its `flash_args`
are written by `build.sh` (with "keep", as the scripts use), not the build's, which name 0x20000.

The two Korvo-1 `tiny386.bin` files differ in 65 bytes only: the ELF file's SHA-256 that the app
carries (0xB0 to 0xCF), the checksum byte and the image's own SHA-256 at its end [verified:
`checks\reproducible-v3.2.txt`]. The code and data are the same; the ELF files differ because the
build folders' names and the table's file name differ [inferred]. Free space in v3.2: 1,098,688
bytes (56 per cent) in the 1,920 KiB test partition, 967,616 (53 per cent) and, for the DOS
board, 938,576 (51 per cent) in the 1,792 KiB factory partition [arithmetic].

### 4.2 The scripts

`flash-paths.cmd` names `images\korvo1-dualboot\tiny386.bin` (`TINY386_BIN`) and, for the
standalone set, `images\korvo1-standalone\` (`SA_BOOTLOADER`, `SA_TABLE`, `SA_TINY386_BIN`).
After the review [verified: the scripts; `REVIEW-RESOLUTION.md` B M1, m1, m4, n1]:

* the port is a required argument in all three;
* before the Y prompt each reads the board (`esptool read-mac`, then `read-flash 0x8000 0xC00`
  into a file in `%TEMP%`), prints the MAC and names the table it found by SHA-256: `linux`
  (520538d3, be5b725 and the release alike), `dual` (d41cba3c), `own` (afbe6410, tiny386's own:
  the standalone set or the DOS board) or `other`. `flash-dualboot.bat` needs `linux` or `dual`
  (`dual` for `app`); `flash-standalone.bat` takes any of the three it knows, warning on `own`;
  `back-to-linux.bat` needs `dual` or `linux` for `small`, and also takes `own` for `full`.
  Anything else stops, saying what was found;
* `flash-dualboot.bat` writes tiny386 at 0xE20000 first, in an esptool run of its own, then the
  bootloader and table;
* `back-to-linux.bat COMx` means `small be5b725`; the release files need the word `release`;
* `flash-standalone.bat` pins its bootloader and table by SHA-256, as `flash-dualboot.bat` pins
  the dual-boot pair.

All stay CRLF and ASCII [verified: `file`]. Their main flow, cut off at the Y check, was run with
a stub esptool whose `read-flash` hands back a chosen table: 23 cases, among them every table for
every script, no port, no board on the port, a missing or invalid image, a wrongly set
`SA_TABLE`, answers Y and N; each wrote (would have written) or stopped as intended, with the
paths of section 4.1 [verified: `checks\bat-harness.txt`, made by
`checks\bat-harness-make-flow.py`]. The scripts themselves were not run, and no esptool reached a
port.

Since v3.2 (section 12.3) [verified: the scripts; `checks\bat-harness-v3.2.txt`, 39 cases]:

* every board read, and the first of two writes, passes `-a no-reset`, so the chip stays in
  download mode from the first reset to the last write's hard reset; after N, or a stop after the
  board was read, the scripts say to press RESET;
* over Linux's table `flash-dualboot.bat` and `flash-standalone.bat` name the bootloader at
  0x2000 by SHA-256 (be5b725's 547c6a5b, the release's 41d5a687, or neither) and, the first time,
  read the whole flash (`read-flash 0x0 0x1000000`) and 0x2000 to 0x8FFF into `BACKUP_DIR`
  (`backup\` beside them) after Y and before any write, each with its SHA-256, the board's MAC
  and a note; a backup already there is kept only if it is whole, of this board, and of what the
  flash holds now; otherwise they stop;
* `back-to-linux.bat COMx` is `backup` (0x2000 to 0x8FFF from the backup) when there is one, else
  `small be5b725`; `whole` writes the 16 MB backup back; `backup` and `whole` check the files
  against their recorded SHA-256 and the board's MAC against the backup's;
* `KORVO1_MAC` in `flash-paths.cmd`, once set, makes the three scripts stop on any other board;
* the harness now runs each script whole with only the two write calls cut out, so the backup and
  its checks run too, and the stub logs every call it gets.

There is no script for the DOS board yet; `FLASH.txt` 6 gives the esptool command.


## 5. Checks

### 5.1 Builds and warnings

`build.sh` from nothing, twice, in `~/tiny386-idf-scratch/v3-final/rel1` and `rel2-second-run`
[verified: `checks\build-sh-run1.txt`, `run2.txt`, `reproducible.txt`]: one compiler warning in
each Korvo-1 build (`i2s.c`, `i2s_task` unused, v2's old one), none in the DOS board build. The
firmware files the review round touched (`esp_main.c`, `usb_input.c`, `console.c`, `speed.c`,
`display.c`, `kbd_input.c`, `i2s.c`, `dosboard.c`, `lcd_dosboard.c`, `ms7210.c`, `ch9350.c`,
`setup.c`, with `storage.c` and `lcd_korvo1.c`), compiled again with each build's own command
line plus `-Wall -Wextra`: none but that same `i2s.c` one, which shows the check sees warnings
[verified: `checks\warnings-wall-wextra.txt`].

### 5.2 Host tests

`bash tests/host/run.sh OUT` in the v3.1 tree: 18 passed, 0 failed [verified:
`checks\host-tests.log`]:

| # | Test | Result |
|-|-|-|
| 1, 2 | PS/2 queue lock; the same test fails against pristine `i8042.c` (9 checks) | pass |
| 3 | boot console and crash screen | pass |
| 4 | `vga.c`: retrace, rows redrawn, clears, placement | pass |
| 5 | `kbd_input.c`, 207 checks (the mouse now read through the controller's step) | pass |
| 6 | PS/2 mouse, 17 checks: the review's backlog case, clicks faster than a sample, the sample rate, remote mode, reset, the mouse off | pass |
| 7 | PS/2 mouse, a reporting thread against the emulator's thread, `i8042.c` built as on the board | pass |
| 8 | the test of 6 against v3's `i8042.c`: has to fail, and does (271 bytes, out of step) | pass |
| 9 | `hid_parse.c`, 21 checks | pass |
| 10 | `usb_input.c` against usb_host_hid's two-step close: five cases from the review's model and after it | pass |
| 11 | `setup.c`, 31 checks, with a `DOSBOARD.INI` that must not be listed and a folder too long to keep | pass |
| 12 | boot menu, 22 checks | pass |
| 13 | speed and BENCH lines, 26 checks | pass |
| 14 | MS7210 driver: as before, plus the EDID extension cases, a chip reset under the driver and `rb_swap` | pass |
| 15 | MS7210 bus log against `encoder.md` 8.4, 114 rows (the two meter reads and the 0x120C read back allowed as additions) | pass |
| 16 | CH9350L, 55 checks, through `ch9350.c`, `kbd_input.c` and `i8042.c` | pass |
| 17, 18 | AUDIO_RATE at 44,100 and 48,000 | pass |
| 19 | since `v3.1-tests`: the card's partitions, `storage.c` with ESP-IDF's own FatFs on eight card images, 22 checks (section 11) | pass |

All 18 pass again with an output folder of 109 characters (the setup test then makes its card in
a short folder of its own) [verified: `checks\host-tests-long-out.log`]. The two-thread mouse test
built with ThreadSanitizer runs clean [verified: `checks\tsan-ps2-mouse.txt`].

At `v3.1-tests`, in the scratch tree, all 19 pass, and the host build's compiler warnings are the
same 11 as at v3.1 [verified: `checks\host-tests-v3.1-tests.log`]. In a tree made by `build.sh`
from the patch, `run.sh` gives 18 passed, 0 failed: test 8 runs only where the tree has the `v3`
tag, and a clone made from the patch has not [verified: `checks\host-tests-from-patch.log`,
`run.sh` 4b].

### 5.3 The Linux route

`..\tiny386\BUILD.txt` "Tests": the core files changed since its release are now `vga.c`,
`i8042.c`, `i8042.h`, `ide.c`, `ide.h`, `misc.c`, `pcspk.c`, `adlib.c`, `sb16.c` and the new
`audio_rate.h`. On copies in `v3-final/lr` (nothing in `~/tiny386-scratch` or the release folder
was written; the board's compiler in `~/tiny386-scratch/toolchain` was only used) [verified:
`checks\linux-route-rebuild.txt`]:

* its build tree rebuilt as it was: the release `tiny386-korvo`, byte for byte (dd34a04f);
* the same with the v3.1 files put in: the same 21 objects and the same binary, byte for byte;
* `run-tests.sh` against that binary: 26 passed, 0 failed [verified:
  `checks\linux-route-run-tests.txt`];
* `vgatest.c 6000` against the v3.1 `vga.c`: 72,000 pictures, 0 rounds wrong [verified:
  `checks\linux-route-vgatest.txt`].

So the Linux route's results stand for v3.1's shared files: under its defines they compile to
what it has now [verified]. That also means the Linux route keeps tiny386's own mouse code, with
the backlog fault the review found (`REVIEW-emulator-correctness.md` F1) [inferred]. The `-ua`
build was not rebuilt; it differs only by `I386_ALIGNED_ONLY`, which no change touches [inferred].

### 5.4 The card

`checks\card-check.txt`: the firmware's own `setup.c` and tiny386's `ini.c` on a pretend card
made of `card\tiny386\`, the Linux route's `freedos.ini`, `dosboard.ini`, and empty
`freedos-hd.img` and `/sdcard3/win95.img` [verified]: the menu lists `freedos.ini`,
`tiny386.ini`, `win95-14m.ini` (not `dosboard.ini`), the default is `tiny386.ini`, every key is
one `esp_main.c` knows, every file named is found, every ini parses without an error line.
`bios.bin` and `vgabios.bin` are the Linux route's (be36f0d3..., f658c3b0...) [verified:
SHA-256 against `..\tiny386\SHA256SUMS`].

### 5.5 Reproducibility

Section 0: the two `build.sh` runs gave the same bytes in every file of every set, ELF files
included [verified]. The app version is fixed by `esp/version.txt` (`v3.1`), and the tree's paths
are mapped in the compiler's file names, so the folder the build runs in does not matter; the
build folder's name does (it is in the ELF file) [inferred from the 65-byte difference above and
`NOTES-core2.md` 6.2]. `build.sh` always uses the names in section 2.

A third run, in the resumed workflow, with the `v3.1-tests` patch in another folder again
(`v3-work-integ/resume/rel1`), gave the same bytes in all 18 files once more [verified:
`checks\reproducible-v3.1-tests.txt`, section 11].

### 5.6 The dual boot again, with the v3.1 images

The dual-boot work's flash simulation (`dualboot\scripts\simulate-flash.py`), run again with the
write lists pointed at `images\korvo1-dualboot\` and `images\korvo1-standalone\` (a copy in
`v3-final/sim`; its inputs read from `~/tiny386-idf-scratch/dualboot`, nothing written there)
[verified: `checks\flash-simulation.txt`]: the dual-boot install changes 0x2000 to 0x8FFF and
0xE20000 to 0xEF3FFF only, every Linux partition unchanged; with GPIO42 high the bootloader picks
the Linux loader at 0x20000, held low tiny386 at 0xE20000 (valid, version v3.1); 1,100,176 bytes
of the test partition spare; `small be5b725` (case 4, now the script's default) gives 0x0 to
0xC1FFFF back equal to the start; the ways back never reach 0xE20000; the standalone set changes
0x2000 to 0x8FFF and 0x10000 to 0xE3FFF, Linux's nvs, otadata, phy_init and loader among them.
The simulation writes each set at once; `flash-dualboot.bat` now makes the same writes in two
esptool runs, tiny386 first, which ends in the same flash [inferred]. Case 5's title still calls
the full release set "the default", as the dual-boot work wrote it; it no longer is.


## 6. Sizes [verified: `checks\size-*.txt`; earlier figures from the notes named]

| | Korvo-1, v3.2 | Korvo-1, v3.1 | Korvo-1, v3 | DOS board, v3.2 | DOS board, v3.1 | DOS board, v3 |
|-|-|-|-|-|-|-|
| `tiny386.bin` | 867,392 | 865,904 | 860,672 | 896,432 | 894,128 | 887,376 |
| Flash `.text` | 415,128 | 413,932 | 411,662 | 410,442 | 408,780 | 405,606 |
| Flash `.rodata` | 225,716 | 225,436 | 224,556 | 255,984 | 255,344 | 253,888 |
| Internal RAM used by code and static data | 241,290 | 241,282 | 239,172 | 244,320 | 244,296 | 242,148 |
| Internal RAM left | 262,454 | 262,462 | 264,572 | 259,424 | 259,448 | 261,596 |
| LP SRAM (the console's ring) | 8,232 | 8,232 | 8,232 | 8,232 | 8,232 | 8,232 |

The first review round cost about 5 to 7 KB of flash and about 2 KB of internal RAM on each
board, the second about 1.5 to 2.3 KB of flash and under 30 bytes of internal RAM [arithmetic].
At run time the second round adds about 1.2 KB of PSRAM on the DOS board (24 notes of 160 bytes
instead of 112) and 4 bytes to the PS/2 mouse's state [inferred: sizes]. At run time `usb_events` takes 1 KB more stack (4096 bytes, Korvo-1 only), and the
mouse's ring is about 200 bytes in the PS/2 state [inferred: sizes]. The DOS board image is bigger
than the Korvo-1's by the sound path, the MS7210 and CH9350L drivers and the I2C master, less the
USB host [inferred: `NOTES-dosboard.md` 0 finding 5]. Internal RAM free at run time is printed on
the console; the core's estimate was about 180 KB on the Korvo-1 [inferred: `NOTES-core2.md` 6.3].


## 7. What only a board can say [unknown]

All of `NOTES-core1.md` 7, `NOTES-core2.md` 7, `dosboard\NOTES-dosboard.md` 12 and
`dualboot\DUALBOOT.md` 10 stand. New with the merge and the review round:

* On the DOS board, whether mounting the card before the panel starts is quick enough with no
  card in (the SD driver's own timeout comes first), and whether the MS7210 lines on the console
  read as they should.
* The CH9350L reset at `ch9350_setup()`, earlier than the DOS board work had it (it waited for
  the PC), with the boot menu taking the keys.
* Every task's stack headroom (the 10 s `stacks` line on the UART), and whether a full-frame
  write-back makes disk access wait (the `display:` line during BENCH): TEST-PLAN 7b.
* How the mouse feels with packets at the guest's sample rate, in DOS and in Windows 95.
* Whether the keyboards Plat has put a non-keyboard HID interface first (the case the USB fix is
  for).
* New with the second review round: whether `LCD underrun` appears on the UART (TEST-PLAN 7b),
  more likely on the DOS board's 800x600 than on the Korvo-1; how long the 16 MB backup read takes
  at 921,600 baud (estimated at a few minutes [inferred]); which PCLK pair gives the DOS board a
  clean picture, and whether its delay neighbours do too; whether the MS7210 answers at 0x59 or
  0x2B; whether 0x120C reads back what D1 wrote (if not, the hot plug poll's reset watch turns
  itself off with a line saying so); the I2S frame with 32-bit slots on a scope at TP938; whether
  the MS7210's pad pull default after a cold power-up is a pull-down, which firmware cannot help;
  and PSRAM free after the PC is built, which decides whether the 800x600 16-colour cache fits.


## 8. For Plat to decide

* The SB16 resampler fix (section 3.3): apply it, for the DOS board's sound at 48 kHz, knowing it
  changes SB16 sound on the Korvo-1 and the Linux route as well; or leave it until the DOS board
  plays something.
* `dosboard.ini` as a file of its own (section 3.1), rather than a section of an ini.
* The ELF files in `images\` (about 11.6 MB each): kept for reading crash screens; they can go.
* From the review (`REVIEW-RESOLUTION.md`, deferred): the DOS board's USER key skipping the board
  check for bring-up (D F11); the HDMI sound level's default (`audio_shift`, 4 now); mouse
  scaling (E F7); leaving out the NE2000 on boards without Wi-Fi (E F8); the Linux route taking
  the mouse fix too.
* From the second round: `CONFIG_ESP_TASK_WDT_PANIC` (C2 4): set, a core 0 starved for 5 s halts
  with the crash screen instead of a frozen panel, but a stall that would have passed halts too;
  not set (as now), the task watchdog only prints on the UART. The usb_host_hid request in flight
  when a device is unplugged (C2 1): patch the managed component (`hid_host_uninstall_device()`
  taking `device_busy` with a timeout before deleting it) or report it to Espressif; neither was
  done, and no message was sent. The keyboard's E0 pair against an ACK and scancodes while
  scanning is off (E2 3, E2 5), the mouse's clamp at 127 counts a packet and scaling (E2 8, with E F7), and a mouse
  command's answer behind waiting packets (E2 9): each a change of what guests see from the
  keyboard or mouse, best made with a board to try them on.


## 9. Where things are

| Where | What |
|-|-|
| `tiny386-idf-v3.patch`, `.diffstat.txt` | the whole of `v3.2` against tiny386 9ed786b (until the second round, of `v3.1-tests`) |
| `backup\` | made by `flash-dualboot.bat` or `flash-standalone.bat` on Plat's PC, the first time either writes over Linux: the whole flash and 0x2000 to 0x8FFF as they were. Not in this folder until then; keep it |
| `build.sh`, `dependencies.lock` | the build from nothing, and the component versions |
| `images\` | the three image sets (section 4) |
| `card\tiny386\` | the Korvo-1's card files: `tiny386.ini` (FreeDOS), `win95-14m.ini`, `bios.bin`, `vgabios.bin` |
| `card-dosboard\tiny386\dosboard.ini` | the DOS board's settings, every key with its default |
| `FLASH.txt`, `flash-*.bat`, `back-to-linux.bat`, `flash-paths.cmd` | writing the images, and going back to Linux |
| `TEST-PLAN.md` | the risks to Linux, then the first boot on the Korvo-1 |
| `REVIEW-*.md`, `REVIEW-RESOLUTION.md` | the four reviews, each with its second round first and its first round as an appendix, and what became of each finding |
| `checks\` | this note's evidence |
| `NOTES-core1.md`, `NOTES-core2.md`, `core1\`, `core2\` | the core work, as it was before the merge |
| `dosboard\` | the DOS board work, as it was before the merge (its patch is against v2; its `images-old-v2\` are not for flashing) |
| `dualboot\` | the dual-boot bootloader and table, the release's Linux images, the dual-boot evidence |
| WSL `~/tiny386-idf-scratch/v3/tiny386` | the tree, branch `integ`, tags `v3`, `v3.1`, `v3.1-tests` and `v3.2` |
| WSL `~/tiny386-idf-scratch/final-r2` | the second round's scripts and runs (section 12.5) |
| WSL `~/tiny386-idf-scratch/v3-work-integ` | the merge's scripts and runs; `resume/` the resumed run's (section 11) |
| WSL `~/tiny386-idf-scratch/v3-final` | the review round's scripts (`b.sh`, `ht.sh`, `lr.sh`, `collect.sh`, `make_flow.py`, `q6.sh`), the two `build.sh` runs (`rel1`, `rel2-second-run`), the Linux route copies (`lr/`), the host test output |


## 10. The review round: v3.1

Four reviews of tag v3, each in its own `REVIEW-*.md`, found three major problems and a number of
smaller ones; `REVIEW-RESOLUTION.md` gives every finding one line. The fixes, as five commits on
v3 in the scratch tree [verified: `git log v3..v3.1`]:

| Commit | What |
|-|-|
| 16f9750 | `i8042.c`: with `PS2_KBD_HOOKS` the mouse's reports go into a ring and `kbd_step()` makes the packets on the emulator's thread: whole or not at all, at the guest's sample rate, one button state a packet, the backlog held to 1,023 counts. `tests/host/test_ps2_mouse.c` |
| 7d8588e | `usb_input.c`: both closes for an interface given up, the unplug acted on only for its own handle, no lights to a device being freed, 4096 bytes for `usb_events`, its own start semaphore, checked task creation, lost events counted. `tests/host/usb/` |
| 9b6af1c | the console held after a fatal message or switch-off, log lines from interrupts kept, the hotkey cleared before the picture starts, the stacks line, floppy flushes, card errors to the guest and the console, the 256 KiB BIOS limit for partitions, the raw-card disk refused, the VBE depth race drawn black, long setup paths, task creation checked |
| de15941 | the DOS board: EDID extension trouble gives HDMI, the bounce buffers limited and retried without, the PCLK meter read again, `rb_swap`, `i2s_slot_bits` and `audio_shift`, the MS7210 reset noticed, the LED, state 0 keyboard reports checked, status frames answered at most every 100 ms, the ini's values read without case |
| 80e64de | `esp/CMakeLists.txt`: the dual-boot build refuses `idf.py flash` and its relatives; `esp/version.txt`: `v3.1` |

Outside the tree: the three scripts (section 4.2), `FLASH.txt`, `TEST-PLAN.md` (the risks to Linux
at its top), `dosboard.ini`, `dualboot\DUALBOOT.md` 7.1, 8 and 9, the rename of
`dosboard\images\` to `images-old-v2\`, and this note.

What the review round did not change, and why, is in `REVIEW-RESOLUTION.md` under "deferred":
measurements first for the frame write-back's effect on disk access and the sound task's
priority; the keyboard's E0 pair against an ACK from the other core; 132-column text; mouse
scaling; the NE2000; the USER key skipping the DOS board's check.


## 11. The resumed run: the card's partitions test taken in (2026-10-21, evening)

The workflow was stopped by a usage limit and resumed. Everything in sections 0 to 10 was already
done. The four earlier pieces of work were each checked again at v3.1 and each added a dated
section to its own note: `NOTES-core1.md` 10, `NOTES-core2.md` 10, `dosboard\NOTES-dosboard.md`
15, `dualboot\DUALBOOT.md` 12. None of them changed the tree or the images. This section is the
merge's part. Nothing was flashed, no serial port was opened, nothing has run on a board.

### 11.1 What changed

* One commit on `integ`, 823d5cd, tag `v3.1-tests` [verified: `git log v3.1..v3.1-tests`]: core
  part 2's new host test (`NOTES-core2.md` 10.2; its own commit 26fa964, in a private clone), taken
  across with `git am`, with the new files at mode 644 as the tree's other tests are. It adds
  `tests/host/storage/` (14 files) and 17 lines in `tests/host/run.sh`. No file outside
  `tests/host/` changed [verified: `git diff` of the two tags, file names only], so the firmware, the
  images and the Linux route's results are v3.1's; the app still calls itself `v3.1`.
* `tiny386-idf-v3.patch` and its diffstat made again, as `git diff pristine v3.1-tests` (section 1).
* Beside it in `checks\`: `reproducible-v3.1-tests.txt`, `build-sh-run3.txt`,
  `build-checks-v3.1-tests.txt`, `host-tests-v3.1-tests.log`, `host-tests-from-patch.log`,
  `storage-sdkconfig-check.txt`.
* `SHA256SUMS`: the lines for the patch, the diffstat, `README.md` and this note made again.

### 11.2 What the new test covers

From `NOTES-core2.md` 10.2, whose tags stand: `storage.c`'s partition code, cut from the file as it
is, with ESP-IDF's own FatFs (R0.16) and SD card glue compiled for the host; only the sector reads
and writes and the VFS registration are stand-ins. On a card image laid out as Plat's (FAT32
`KORVO_SD` p1 at sector 8192, ext4 p2 at 139264, FAT32 `WIN9X` p3 at 532480), p3 mounts at
`/sdcard3` with its label, and `/mnt/win` is `/sdcard3`, which is what `card\tiny386\win95-14m.ini`
names. The firmware's kind of writes stay inside p1 and p3, `fsck.fat -n` finds both clean, and
the MBR and every sector of p2 are unchanged. Seven other cards cover a shared serial number, no
label, no logical drive left, an exFAT p3 (not mounted: this FatFs has no exFAT), four FAT
partitions, no partition table, and a Linux p1. The board's SD host driver, a real card and its
timing are not covered [unknown].

### 11.3 What was checked [verified unless marked]

| Check | Result |
|-|-|
| `build.sh` from nothing, with the new patch (a staged copy of this folder's `build.sh`, `dependencies.lock` and dual-boot pair beside it), in `v3-work-integ/resume/rel1` | exit 0, about three minutes; the patch makes the `v3.1-tests` tree exactly (tree cec859c2); all 18 files of the three image sets byte for byte the ones in `images\`, ELF files and `SHA256SUMS` included (`checks\reproducible-v3.1-tests.txt`, `checks\build-sh-run3.txt`) |
| Compiler warnings outside ESP-IDF | Korvo-1 and Korvo-1 standalone: one each, the old `i2s.c:19` "`i2s_task` defined but not used"; DOS board: none (`checks\build-checks-v3.1-tests.txt`) |
| Sizes | unchanged: Korvo-1 DIRAM 241,282 used and 262,462 left, LP SRAM 8,232; DOS board 244,296 and 259,448 (same file) |
| Host tests in the scratch tree at `v3.1-tests` | 19 passed, 0 failed; the host build's warnings the same 11 as at v3.1 (`checks\host-tests-v3.1-tests.log`) |
| Host tests in the tree `build.sh` made | 18 passed, 0 failed: test 8 (against v3's `i8042.c`) needs the `v3` tag, which that clone has not (`checks\host-tests-from-patch.log`) |
| The storage test's stand-in `sdkconfig.h` | the same FatFs lines as the from-nothing Korvo-1 build's generated one; 22 of 22 (`checks\storage-sdkconfig-check.txt`) |
| The Linux route | not run again: none of its shared core files changed since v3.1, so section 5.3 stands (`git diff` of the two tags, file names only) |
| `SHA256SUMS` here and in each image folder | every line verifies after the update (`sha256sum -c`) |

### 11.4 Answers to the other resumed checks

* `dosboard\NOTES-dosboard.md` 15 asked whether the merged tree took the SB16 resampler fix. It
  did not: `sb16.c` at `v3.1-tests` still has `approx_frac()` [verified: `git grep`]. It stays
  Plat's call (section 8).
* `NOTES-core2.md` 10 left its storage test for the next merge: it is in the patch now (11.1).
* `NOTES-core2.md` 10.4: `mem_size = 14.5M` reads as 14 MB (`atol()` stops at the point); to ask
  for 14.5 MiB write `mem_size = 14848K` [reported: `NOTES-core2.md` 10.4, from `pc.c` and
  `misc.c`; not checked again here]. `win95-14m.ini` asks for `14M` and is unchanged.
* `dualboot\DUALBOOT.md` 12: the scripts need the port named (`flash-dualboot.bat COM10`), by the
  review's choice (B m1), not a default of COM10; `FLASH.txt` now says what to do if a write is cut
  off inside the partition table.

Scripts and logs: WSL `~/tiny386-idf-scratch/v3-work-integ/resume/` (`s1.sh` to `s5.sh`, `b1.sh`,
`rel1/`, `ht-shared/`, `ht-rel1/`).


## 12. The second review round: v3.2 (2026-10-21, late evening)

The same four lenses read tag `v3.1-tests` (823d5cd) again. They found no blocker and nothing
major; 12 minor findings and 25 notes in all. `REVIEW-RESOLUTION.md` gives each one line: 28
fixed, 3 documented, 4 deferred with the reason (and the component's half of C2 1), 2 with no
change, none rejected [verified: that file]. The four reports now carry their round 2 first and round 1, word for word, as an appendix.
Nothing was flashed, no serial port was opened, nothing has run on a board.

### 12.1 The commits

Five commits on `v3.1-tests` in the scratch tree, tag `v3.2` (d3c839c) [verified: `git log
v3.1-tests..v3.2`]:

| Commit | What |
|-|-|
| e6c8b44 | `usb_input.c`: set-up stops the moment the interface is unplugged; the slot left to `on_gone()` (C2 1). Host test cases F to M |
| 461a8e4 | `i8042.c`: presses and releases kept through the mouse ring's overflow, in order (E2 1); a reset drops the ring and the overflow (E2 4); relaxed atomic reads of the queue count (C2 2). Host tests, and a two-thread run with the emulator held up |
| 79cff6b | `ide.c`: nothing past an image's end (B2 m1), ATA FLUSH CACHE (E2 7); `misc.c`: floppy drive and range checks, refused writes counted (B2 m1, E2 6); `esp_main.c`: one image for two drives refused (B2 n6), PSRAM free on the `PC built` line (E2 11); `vga.c`: a 16-colour cache that does not fit said once; `console.c`: the hold kept (C2 3); `speed.c`: BENCH runs begin with 0xA1 (E2 2); `storage.c`: the card's power-up wait (C2 6); `crash.c`: the watchdog comment (C2 4). Host tests: `tests/host/disk`, `test_floppy.c`, `test_vga_dosboard.c`, a FAT16 p1 card (B2 n5), `run.sh` |
| e9539a5 | the DOS board: I2S word select with 32-bit slots (D2 S1); B15 pull-ups (S2); the reset watch only when 0x120C reads back, three set-ups at most (S3); notes of 160 (S4); default PCLK pair `pclk_neg 1`, phase 4 (S5); DVI for a single non-CEA extension (note 6); range checks (note 7); the board check tries 0x2B (note 8). Host tests |
| d3c839c | `esp/CMakeLists.txt`: `dfu-flash` and the otadata targets refused too (B2 n4); `esp/version.txt`: `v3.2` |

`git diff v3.1-tests v3.2`: 32 files, 1,898 lines added, 105 taken out. The patch is now `git
diff pristine v3.2`: 111 files, 18,634 added, 663 taken out [verified: git's diff statistics,
`tiny386-idf-v3.diffstat.txt`].

### 12.2 Firmware changes that touch shared files, and the Linux route

Every change to a file the Linux route also builds (`i8042.c`, `ide.c`, `misc.c`, `misc.h`,
`vga.c`) is under `PS2_KBD_HOOKS`, `THREAD_SAFE`, `BUILD_ESP32` or `EMULINK_CHECKED` (which only
`BUILD_ESP32` turns on, and the host tests). The Linux route rebuilt from copies with the v3.2
files gives its release binary byte for byte (dd34a04f), 0 of 21 objects differ, its 26 tests pass
and `vgatest.c` runs 6,000 rounds with nothing wrong [verified:
`checks\linux-route-rebuild-v3.2.txt`, `checks\linux-route-run-tests-v3.2.txt`,
`checks\linux-route-vgatest-v3.2.txt`]. So the Linux route keeps tiny386's own mouse code, disk
and floppy handling, as before.

### 12.3 Outside the tree

* The scripts: `flash-dualboot.bat`, `flash-standalone.bat`, `back-to-linux.bat` and
  `flash-paths.cmd` (section 4.2): the backup, `-a no-reset`, the bootloader named, `backup` and
  `whole` for the way back, `KORVO1_MAC`. All CRLF and ASCII [verified: `file`].
* `FLASH.txt`, `TEST-PLAN.md` (the risks to Linux at its top, rewritten for the backup and the
  poweroff), `README.md`, `REVIEW-RESOLUTION.md` (round 2 first), this note.
* `card-dosboard\tiny386\dosboard.ini`: the new default PCLK pair and its order, the I2S frame,
  the DVI rule, the guard's range. `card\tiny386\` is unchanged.
* `build.sh`: only its first comment line (the app version) changed.

### 12.4 Checks [verified unless marked]

| Check | Result |
|-|-|
| `build.sh` from nothing, twice (`final-r2/rel1`, `rel2-second-run`), with the v3.2 patch | exit 0, about 170 s each; all 18 files of the three image sets the same in both, ELF files included; the patch applied to a fresh clone gives the `v3.2` tree exactly (tree 0292cb65) (`checks\reproducible-v3.2.txt`, `checks\build-sh-v3.2-run1.txt`, `run2.txt`) |
| The images in `images\` | rel1's; every image folder's `SHA256SUMS` verifies; the bootloaders and tables are v3's and v3.1's files, byte for byte (`checks\reproducible-v3.2.txt`) |
| `esptool image-info` | every bootloader and app valid, checksum and hash (`checks\image-info-*.txt`, made by `build.sh`) |
| Compiler warnings | Korvo-1: the old `i2s.c:19` one only; DOS board: none. Firmware files with `-Wall -Wextra`: none but that one. Shared core files: the same as v3.1's files give (`checks\warnings-wall-wextra-v3.2.txt`, `checks\warnings-core-files-v3.2.txt`) |
| The flashing targets | in the dual-boot build `flash`, `app-flash`, `bootloader-flash`, both partition table targets, `erase-flash`, both encrypted targets, `dfu-flash`, `otadata-flash`, `erase-otadata` and `erase_otadata` depend on the refusing target; the standalone and DOS board builds refuse nothing, as intended (`checks\cmake-flash-refusal-v3.2.txt`; no flash target was run) |
| Host tests in the scratch tree at `v3.2` | 24 passed, 0 failed; the host build's warnings the same 11 as at v3.1 (`checks\host-tests-v3.2.log`) |
| The same with an OUT folder of 109 characters | 24 passed (`checks\host-tests-v3.2-long-out.log`) |
| Host tests in the tree `build.sh` made from the patch | 22 passed, 0 failed: the two runs that need the `v3` and `v3.1` tags, and the disk test's v3.1 run, are left out there (`checks\host-tests-v3.2-from-patch.log`) |
| ThreadSanitizer, the two-thread mouse test | 0 warnings in 6 runs, 3 as is and 3 with the emulator held up; 10 of 10 presses in every run (`checks\tsan-ps2-mouse-v3.2.txt`) |
| AddressSanitizer and UBSan, the disk test | clean in 3 runs, 30 checks each (`checks\asan-disk-v3.2.txt`) |
| The Linux route | as 12.2 |
| The flash simulation with the v3.2 images | the dual-boot install changes 0x2000 to 0x8FFF and 0xE20000 upwards only, every Linux partition unchanged; GPIO42 high starts Linux, held low tiny386 v3.2 (`checks\flash-simulation-v3.2.txt`) |
| The card | the firmware's `setup.c` and `ini.c` on a pretend card with the v3.2 `dosboard.ini`: 0 problems (`checks\card-check-v3.2.txt`) |
| The scripts' flow | 39 cases with a stub esptool, every one as intended, no write reached the stub; every board read carries `-a no-reset` (`checks\bat-harness-v3.2.txt`, made by `checks\bat-harness-make-flow-v3.2.py`) |
| `SHA256SUMS` | made again for every file it lists, the four review reports included; `sha256sum -c` verifies every line, and every image folder's own |

Not covered by any test here [unknown or inferred, as marked in `REVIEW-RESOLUTION.md`]: the
duplicate image check (in `esp_main.c`, which does not build on the host), the DOS board's 0x2B
fallback in `dosboard.c` (the driver part is host-tested), the I2S frame, the PCLK pair, and
everything only a board can show (section 7).

### 12.5 Scripts and logs

WSL `~/tiny386-idf-scratch/final-r2/`: `t_usb.sh`, `t_mouse.sh`, `t_stall.sh` (the first runs of
the new tests, with v3.1's files beside them), `b2.sh` (the in-tree builds), `commit.sh`,
`stage.sh` (the patch and the staged `build.sh`), `rel.sh` (the two from-nothing builds),
`collect2.sh`, `warn4.sh`, `final-ht.sh`, `look4.sh` (ASan), `lr.sh` (the Linux route), and their
logs; `rel1/`, `rel2-second-run/`, `lr/`, `sim/`. The scripts' harness ran on Windows from the
session's scratch folder (`make_flow2.py`, copied to `checks\`). No AddressSanitizer binary was
left running: one start-up loop of ASan (address randomisation) was stopped by its own task ID
and rerun with `setarch -R`, and its log deleted.


## 13. The merge task run again: v3.2 checked (2026-10-22)

The workflow was started again on the morning of 2026-10-22, and its merge task was handed the
four earlier pieces' resume reports of the evening before, which describe v3.1. Everything the
task asks for was already done and taken further, to v3.2: the merge, both boards' builds, the
three image sets, the card files, the patch, `build.sh` and the docs (sections 0 and 12). So the
tree, the patch, the images, the scripts, the card files and the other docs are as they were; this
section only records what was checked. Nothing was flashed, no serial port was opened, nothing
was committed, nothing has run on a board.

### 13.1 What was there [verified unless marked]

* The scratch tree at tag `v3.2` (d3c839c, branch `integ`), clean but for `esp/dependencies.lock`,
  which is this folder's file as the in-tree builds copy it in (`cmp`). `tiny386-idf-v3.patch` is
  `git diff pristine v3.2` byte for byte.
* Nothing under WSL `~/tiny386-idf-scratch` or in this folder had changed since 23:10 on
  2026-10-21 (`find -newermt`), but the tree's `.git` folder, which this run's own `git status`
  touched [inferred: the time].
* `SHA256SUMS` here and in each image folder: every line verifies (`sha256sum -c`).
* What the resume reports left open is answered in 11.4 and stands: core part 2's storage test is
  in the patch (host test 13); the SB16 resampler fix is not applied (section 8, Plat's call);
  `mem_size = 14848K` for 14.5 MiB; the scripts need the port named.

### 13.2 Checks made today [verified]

| Check | Result |
|-|-|
| `build.sh` as shipped, run straight from this folder (`bash .../v3/build.sh WORKDIR`, as `README.md` says), into a new folder, WSL `~/tiny386-idf-scratch/v3-work-integ/r3/rel` | exit 0, 174 s; all 18 files of the three image sets byte for byte those in `images\`, ELF files and `SHA256SUMS` included; the patch makes the `v3.2` tree exactly (tree 0292cb65). This run was a day after the two of 12.4, so nothing in the images depends on the build date (`checks\build-sh-v3.2-run3.txt`) |
| Compiler warnings outside ESP-IDF | Korvo-1 and Korvo-1 standalone: one each, the old `i2s.c:19` "`i2s_task` defined but not used"; DOS board: none |
| Sizes | as section 6: Korvo-1 `tiny386.bin` 867,392 bytes, internal RAM 241,290 used and 262,454 left, LP SRAM 8,232; DOS board 896,432 bytes, 244,320 and 259,424 |
| App description | `v3.2` in both the Korvo-1 and the DOS board `tiny386.bin` |
| Host tests in that tree | 22 passed, 0 failed, the host build's same 11 warnings (`checks\host-tests-v3.2-run3.log`). Against `checks\host-tests-v3.2-from-patch.log` the only differences are the output folder and the two-thread mouse tests' packet and movement counts (289 against 287 packets as is; 3,741 and 5,952 against 3,722 and 5,992 counts held up), which follow thread timing; all 10 presses were seen in every run |
| Card files | `bios.bin` and `vgabios.bin` byte for byte the Linux route's (`..\..\tiny386\`, `cmp`); `tiny386.ini` (FreeDOS, 4M, gen 4, no FPU) and `win95-14m.ini` (14M, `/sdcard3/win95.img`, gen 3, no FPU) as in 5.4 |
| Docs | no em dash, en dash or double hyphen in `README.md`, this note, `TEST-PLAN.md`, `FLASH.txt`, `REVIEW-RESOLUTION.md` or the inis (`grep`) |

The Linux route's own tests were not run again: no file in the tree has changed since 12.2 ran
them against v3.2.

### 13.3 One thing for Plat

`TEST-PLAN.md` 7b asks, only if it is easy, for a minute of the UART during BENCH, for the
`display:`, `stacks` and `speed` lines and any `LCD underrun` or `task_wdt`. Otherwise the plan
asks for the COM10 log only if the panel stays black (step 11). 7b came from the reviews (C F3,
C F9, C2 5) and is optional; a first boot loses nothing it needs if it is skipped.

Scripts and logs: WSL `~/tiny386-idf-scratch/v3-work-integ/` (`look1.sh` to `look4.sh`, `r3.sh`,
`waitr3.sh`, `cmplog.sh`, `collect3.sh`, `r3.out`) and `r3/` (`rel/`, `ht/`, `rel.log`,
`ht.log`).


## 14. v3.3: the status LED and the DOS board's line out (2026-10-22)

The task asked for a status LED with error codes on both boards, an optional monitor on the S31's
low-power core, and the DOS board's PCM5102A line out, which rev A2 fits
(`..\..\tiny386-hdmi-board\dosboard\out\CHANGES-A2.md` sections 3 to 5). It called the result
v3.2; the tree's tag `v3.2` and the images' own version were already v3.2 (section 12), so this is
`v3.3`, tag `v3.3`, and v3.2's files are kept in `previous-v3.2\`. Nothing was flashed, no serial
port was opened, nothing was committed anywhere but the scratch tree, nothing has run on a board.

### 14.1 The commits [verified: `git log v3.2..v3.3`]

| Commit | What |
|-|-|
| 8537320 | the status LED: `status_code.c`/`.h` (codes, patterns, the RMT frame, the record; plain C), `status_led.c`/`.h` (RMT from a task on core 0, the panic path), `status_bb.h` (the panic bit-bang); hooks in `esp_main.c`, `console.c` (`console_fatal()`), `crash.c`, `dosboard.c`, `ch9350.c`, `usb_input.c`; `STATUS_LED_GPIO 37` in both board headers, `AUDIO_MUTE_GPIO 48` in the DOS board's; `main/CMakeLists.txt` |
| b13c4a0 | `i2s.c`: the line out's soft mute on GPIO48 |
| 5c6765e | `tests/host/test_status.c` and its place in `run.sh` (test 15) |
| 0291ace | `esp/version.txt`: `v3.3` |
| f8c05ec | `ch9350.c`: a comment made exact (what is reported about state 2, what is not known) |
| 7e0a36a | `test_status.c`: the stall time checked against the 10 s the docs give (the mutation run, 14.4, found the test took it from the header) |

`git diff v3.2 v3.3`: 18 files, 1,703 lines added, 23 taken out, all under `esp/` and `tests/`; no
file the Linux route builds changed, so its results of 12.2 stand [verified: git's diff statistics
from v3.2 to v3.3 outside `esp/` and `tests/` are empty]. The patch is `git diff pristine v3.3`: 117 files,
20,314 added, 663 taken out (`tiny386-idf-v3.diffstat.txt`).

### 14.2 What was built

* **The LED** (`STATUS-LED.md` for people, `status_code.h` for the codes). Boot stages by colour;
  errors by colour and blinks (red: no card 1, no ini 2, a BIOS 3, other ini problems 4, stopped 5;
  yellow: PSRAM or another failure before `app_main()` 1, MS7210 2, no monitor 3, keyboard path 4,
  emulator stalled 5); red and white by turns for a crash. One timing for the Korvo-1's WS2812B and
  the DOS board's Inolux IN-PI554FCH (T0H 337.5, T0L 962.5, T1H 662.5, T1L 637.5 ns, a 300 us reset
  before every frame), inside both datasheets' windows with room for the bit-bang's jitter
  [verified: Inolux v2.2 p. 10, Worldsemi WS2812B-2020-V6, `test_status`]. ESP-IDF's LED-strip
  example timing was not used: its 0.9 us T1H and 50 us reset are outside the Inolux part's
  [verified: CHANGES-A2.md 3].
* **Normal running**: RMT at 80 MHz (REF_F80M undivided), one 25-symbol frame per change through the
  copy encoder, from a task on core 0 (priority 1, 3,072 bytes of stack) that looks every 50 ms and
  sends again once a second. GPIO37 idles low. The LED starts first thing in `app_main()`, before the
  DOS board's check, since GPIO37 drives the same LED circuit on both boards [verified: Korvo-1
  schematic v1.1 p. 3 rendered at 400 dpi (U3 WS2812B, Q2 LBSS138LT1G, R93 10k, R91 1k, C79, R92 and
  R75 0R; WS2812_CTRL to GPIO37 through R21); rev A2 netlist: GPIO37 to R205 and Q201 only].
* **The panic path**: `__wrap_esp_panic_handler()` calls `status_led_panic()` before the crash
  screen: it notes the crash in the record, takes GPIO37 from the RMT through the IO MUX and the
  GPIO matrix, and bit-bangs the first frame against the cycle counter, interrupts off, from
  internal RAM. A second wrap, of `esp_system_reset_modules_on_exit()`, which ESP-IDF's halt stub
  (`CONFIG_ESP_SYSTEM_PANIC_PRINT_HALT`) calls before its empty loop, runs the LED's pattern loop
  instead. In all three images `esp_panic_handler` calls `__wrap_esp_system_reset_modules_on_exit`
  and `esp_restart_noos_inner` still calls the real one (its call is inside its own object, which a
  wrap does not reach); every function and table the loop uses is in internal RAM; each waiting
  loop is three instructions (rdcycle, sub, bltu) [verified: `checks\status-led-panic-path-v3.3.txt`,
  objdump and nm of the built ELF files]. A panic before `app_main()` (the PSRAM failing its start-up
  or its memory test aborts there) shows yellow 1; its clock is the second-stage bootloader's 80 MHz
  [verified: `CONFIG_BOOTLOADER_CPU_CLK_FREQ_MHZ 80`, IDF `cpu_start.c`: PSRAM chip init and memory
  test (lines 663, 764) come before `esp_clk_init()` (848)], which the bit-bang takes from
  `esp_rom_get_cpu_ticks_per_us()` [inferred: the bootloader sets it through `rtc_clk`].
* **The last run**: a 12-byte record (stage, the code shown, a check word) in `RTC_NOINIT` LP SRAM,
  written on every change and by the panic path. After a reset by a panic, a watchdog, a CPU lock-up
  or a brownout, or a RESET while a crash had the board halted, the LED first flashes white once,
  twice or three times for the cause and replays the old code twice; the console says the same.
* **The beat watch**: `run_pc()` counts its steps; the LED task shows yellow 5 when the count stands
  for 10 s with the PC running, and clears it when it moves. This is the HP-side part of the LP-core
  question: `..\research\status-led-lp-core.md` found that the LP core cannot drive GPIO37 (its pins
  are GPIO0 to 7) and that resetting a stuck chip is what the interrupt and RTC watchdogs already
  do, so no LP-core code was written, not even behind a define (that note, section 5, says why).
* **The line out** (`LINE-OUT.md`): XSMT (GPIO48) driven low from `i2s_main()`, high 20 ms after
  the I2S channel starts (by the clock, since the first writes only fill the DMA ring), low again
  in the panic handler, which then waits 4 ms (150 samples and 0.2 ms at 48 kHz, TI 11.2.1) and
  stops I2S0's output, but only if the channel was ever started. `dosboard.ini`'s comments now say
  that `audio_shift`, `i2s_slot_bits` and `hdmi_audio` concern the line out too; no setting
  changed [verified: the card check below].
* **DOS board**: the old plain-LED code (low = lit, blinking from the hot plug task) is gone; the
  hot plug task reports yellow 2 and 3, `wrong_board()` yellow 2, the CH9350L task yellow 4 for no
  byte 3 s after its reset. Korvo-1: `usb_setup()` yellow 4 when the host cannot start.

### 14.3 Sizes [verified: `checks\size-*-v3.3.txt`]

| | Korvo-1, v3.3 | Korvo-1, v3.2 | DOS board, v3.3 | DOS board, v3.2 |
|-|-|-|-|-|
| `tiny386.bin` | 885,552 | 867,392 | 914,624 | 896,432 |
| Flash `.text` | 425,192 | 415,128 | 420,500 | 410,442 |
| Flash `.rodata` | 229,792 | 225,716 | 260,140 | 255,984 |
| Internal RAM used by code and static data | 245,492 | 241,290 | 248,474 | 244,320 |
| Internal RAM left | 258,252 | 262,454 | 255,270 | 259,424 |
| LP SRAM (the console's ring, the LED's record) | 8,244 | 8,232 | 8,244 | 8,232 |

About 18 KB of flash and 4.2 KB of internal RAM on each board, most of it ESP-IDF's RMT driver
[inferred: sizes]; at run time also the LED task's 3 KB stack and the driver's channel [inferred].
The dual-boot `tiny386.bin` uses 885,552 of its partition's 1,966,080 bytes (55.0 per cent
spare) [verified: flash simulation].

### 14.4 Checks [verified unless marked]

| Check | Result |
|-|-|
| `build.sh` from nothing, twice (`v32-work/final-rel1`, `final-rel2-second-run`), with the patch at f8c05ec | exit 0, two to three minutes each; all 18 files of the three image sets the same in both, ELF files included; the patch gives f8c05ec's tree (2cfbe826). A third run (`final-rel3`) with the final patch (`git diff pristine v3.3`; the last commit touches only `test_status.c`) gave the same 18 files again, in 172 s, and the tag's tree exactly (a6079865) (`checks\reproducible-v3.3.txt`, `build-sh-v3.3-run1.txt` to `run3.txt`) |
| The images in `images\` | final-rel1's; each folder's `SHA256SUMS` verifies; the bootloaders and tables are v3.2's files byte for byte, so only `tiny386.bin` and `tiny386.elf` changed |
| `esptool image-info` | every bootloader and app valid, checksum and hash; the apps say `v3.3` (`checks\image-info-*-v3.3.txt`) |
| Compiler warnings | Korvo-1 and its standalone build: the old `i2s.c` "`i2s_task` defined but not used" only (now line 69); DOS board: none. The nine files v3.3 changed, again with `-Wall -Wextra`: none but that one (`checks\warnings-wall-wextra-v3.3.txt`) |
| Host tests, scratch tree at `v3.3` | 25 passed, 0 failed: v3.2's 24 and the new test 15 (`checks\host-tests-v3.3.log`); again with an OUT folder of 116 characters (`host-tests-v3.3-long-out.log`); in the tree `build.sh` made from the final patch, 23 of 23, the runs that need older tags left out (`host-tests-v3.3-from-patch.log`); the host build's warnings the same 11 as at v3.2 |
| Test 15, `test_status` | 18,011 checks: the RMT frame of eight colours, every bit against both LEDs' windows (least margin 37.5 ns); the bit-bang at 80, 160 and 320 MHz with each pass round the waiting loop costing 1 to 6 cycles and each GPIO store 1 to 24, the counter wrapping mid-frame, every edge against both windows (least margin 17.5 ns) and decoded back; every stage and error pattern, blink counts and colours; the state machine's priorities; the replay for each cause; the beat watch across a wrap of the clock; the record against every single-bit change and 100,000 noise records. Under AddressSanitizer and UBSan: clean (`checks\asan-status-v3.3.txt`) |
| Mutation run | twelve faults put into copies of `status_code.c`, `status_code.h` and `status_bb.h` (schedule drift, T0H and T1H out of window, a 50 us reset, RGB order, LSB first, a wrong blink count, a fatal error overwritten, a 5 s stall, a weak check word, no replay lead-in, short bit-bang cycles): all caught, after the stall test was fixed (7e0a36a) (`checks\status-led-mutants-v3.3.txt`) |
| The flashing targets | as v3.2: in the dual-boot build every flashing, erasing, DFU and otadata target depends on the refusing target; the standalone and DOS board builds refuse nothing (`checks\cmake-flash-refusal-v3.3.txt`; no flash target was run) |
| The flash simulation with the v3.3 images | the dual-boot install changes 0x2000 to 0x8FFF and 0xE20000 to 0xEF8FFF only, every Linux partition unchanged; GPIO42 high starts Linux, held low tiny386 v3.3; against v3.2's run only the app's size, hashes and version and the erased ranges that follow from the size differ (`checks\flash-simulation-v3.3.txt`, with the diff at its end) |
| The scripts' flow | the unchanged scripts against the v3.3 images with a stub esptool: 39 cases, no write reached the stub; against v3.2's run every esptool call, decision and exit is the same, only the image's size, hash, version and the dates differ (`checks\bat-harness-v3.3.txt`, made by `checks\bat-harness-make-flow-v3.3.py`) |
| The card | `setup.c` and `ini.c` on a pretend card with the v3.3 `dosboard.ini`: 0 problems; the file differs from v3.2's in comment lines only (`checks\card-check-v3.3.txt`) |
| `SHA256SUMS` | made again for every file it lists, `STATUS-LED.md` and `LINE-OUT.md` added; every line verifies, and every image folder's own |

Not covered by any test here: the RMT driver's and the GPIO peripheral's real timing (a scope on
the DOS board's TP948, or on the Korvo-1's LED data line, would show it), the store latency into
the GPIO block during the bit-bang [unknown], whether the halt loop really runs after ESP-IDF's
report on the board, the LED's colours as they look, the PCM5102A's start and mute, and the
CH9350L's behaviour with nothing plugged in. All of it is TEST-PLAN's, step by step.

### 14.5 What only a board can say [unknown]

* Whether the Korvo-1's WS2812B takes the timing (the revision is not known; the timing meets the
  WS2812B-2020-V6 datasheet's windows), and whether the Inolux LED lights blue at power-on.
* Whether GPIO stores keep their spacing in the bit-bang (posted writes through the HP bus).
* Whether `esp_rom_get_cpu_ticks_per_us()` is right in a panic before `app_main()`.
* The line out's level and noise, and whether the 20 ms wait before unmuting avoids a click.
* Whether a CH9350L in state 2 says anything with nothing plugged in (yellow 4 until it does).

### 14.6 For Plat to decide

* Brightness: about a quarter of full; one line per colour in `status_code.c`.
* The LP-core monitor: not built (research note section 5); section 6 there is the sketch if wanted.
* XSMT on GPIO48 was kept as CHANGES-A2.md 15 left it, and the firmware uses it. Strapping XSMT
  high instead would lose only the mute at start and in a crash.

### 14.7 Scripts and logs

WSL `~/tiny386-idf-scratch/v32-work/`: `b1.sh` (in-tree builds), `commit1.sh` to `commit3.sh`,
`stage.sh`, `rel.sh`, `rel2.sh`, `rel3.sh` (the from-nothing builds; `rel1/` and
`rel2-second-run/` were a first pair, before the last two commits, giving the same images),
`ht1.sh`, `ht2.sh` (host tests), `w1.sh`, `warn3.py`, `mutate.sh`, `asan.sh`, `card.sh`,
`sim.sh`, `q.sh`, `disasm.sh`, `install.sh`, `collect1.sh`, `harness-gen/`, the research text
extracts in `txt/` and the Korvo-1 schematic renders in `img/`. The harness ran on Windows from the
session's scratch folder. Nothing was started that is still running.


## 15. v3.4: the fixes from the review of v3.3 (2026-10-22)

The review of v3.3 (`REVIEW-v32.md`; the file has the name the task gave it) found one major fault
and eight smaller points; `REVIEW-RESOLUTION.md` round 3 has each one and what became of it. The
task asked for the fixes to be committed and tagged `v3.2`. Tags `v3.2` and `v3.3` both already name
images that were shipped in this folder, so the fixes are a new tag, `v3.4`, and the app calls
itself `v3.4`; moving an old tag would give two different images one name. That is easy to change
(`esp/version.txt` and these notes) if Plat prefers. v3.3's files are kept in `previous-v3.3\`.
Nothing was flashed, no serial port was opened, nothing was committed anywhere but the scratch
tree, nothing has run on a board.

### 15.1 The commits [verified: `git log v3.3..v3.4` in WSL `~/tiny386-idf-scratch/v3/tiny386`]

| Commit | What |
|-|-|
| 161ddc2 | `status_bb.h`: the bit's high and period picked while the line is still low, before the wait that ends in the rise, and the reset made the first wait of the same loop, so that each store follows its wait's branch directly (F1) |
| 4070459 | `status_code.h`, `status_code.c`: T1H 700 ns in a 1325 ns bit (F2); yellow 1 and white described exactly (F3); the Inolux timing table cited on page 11 (F9) |
| e0c0bc8 | `tests/host/test_status.c`: the instructions between each wait and its store charged; high times checked at the LED too; v3.3's layout run once and found short; the last bit's low checked (F1, F2) |
| 2177b06 | `status_led.c`, `status_led.h`, `crash.c`, `esp_main.c`: a 4096-byte LED task stack (F5); the LED sent black if its task cannot start (F6); the replay after RESET noted as resting on the record alone (F4); the halt wrap defined only with the LED (F7); `crash_test = 5` in crash-test builds (F1) |
| a33b7cf | `esp/version.txt`: `v3.4` |

`git diff v3.3 v3.4`: 9 files, 264 lines added, 69 taken out, all under `esp/` and `tests/`, so the
Linux route is not touched. The patch is `git diff pristine v3.4`: 117 files, 20,509 added, 663
taken out, 796,779 bytes (`tiny386-idf-v3.diffstat.txt`). `build.sh` outside the tree changed too
(15.2).

### 15.2 The crash bit-bang (F1)

What the review found, checked again here [verified: `riscv32-esp-elf-objdump` of v3.3's
`tiny386.elf` files, `checks\status-led-panic-path-v3.4.txt` part 1]: in `status_led_panic()` and
`status_led_halt()`, nine instructions (ten for a 1 bit) came between the period wait's last
counter read and the store that raises GPIO37, and two between the high wait's and the store that
lowers it, so every high time came out 7 or 8 cycles short. The host test charged neither, so it
could not see it. At 80 MHz, the clock while ESP-IDF's PSRAM checks run [verified: `cpu_start.c`
lines 663 and 766 before `esp_clk_init()` at 848; `CONFIG_BOOTLOADER_CPU_CLK_FREQ_MHZ 80`], a 1 bit
came out about 525 to 562 ns, under the WS2812B-2020's 580 ns [verified: datasheet p. 5, and this
round's test, below]. The finding stands.

What changed:

* `status_bb.h` picks the next bit's high time and period before the wait that ends in the rise,
  and treats the reset as the first such wait. In all three v3.4 images and in both crash-test
  builds each wait is `rdcycle`, `sub`, `bltu`, and the store comes straight after the `bltu`, for
  the rise as for the fall; the third wait (the last bit's low) is followed by the function's own
  code [verified: `checks\status-led-panic-path-v3.4.txt`]. A high time is now what was asked, give
  or take one pass round the waiting loop.
* `build.sh` checks this in every `tiny386.elf` it makes (`bb_layout()`: disassembles both
  functions, finds each three-instruction wait, and wants exactly one followed by the store to
  `GPIO+0x14`, one by the store to `GPIO+0x18`, and one by anything else), and stops if not. On
  v3.3's images the same check finds no wait before the rising store and stops [verified: the same
  file, part 1], so a compiler that moved work back between a wait and its store would be caught
  at build time. The register offsets are `GPIO_OUT1_W1TS_REG` 0x14 and `GPIO_OUT1_W1TC_REG` 0x18
  [verified: ESP-IDF `soc/esp32s31/register/soc/gpio_reg.h` lines 102 and 119].
* `test_status.c` charges the instructions between a wait's last counter read and its store as
  the built code has them (two before a rise, two before a fall), at one or two cycles each, at 80,
  160 and 320 MHz, on top of v3.3's costs for a pass round the loop and for a store. Then it runs
  v3.3's layout (nine or ten before a rise) with v3.3's times once at 80 MHz and requires it to come
  out short: T1H 562.5 ns, under 580 [verified: `checks\host-tests-v3.4.log`]. So the test now sees
  the fault the review found, and the build checks the layout the test assumes.
* `TEST-PLAN.md` 10b: a scope check of the LED's data line during a forced early crash (15.4).

Not known [unknown]: how many cycles the S31's core really takes for `rdcycle`, a taken branch or
a store into the GPIO block, and whether posted writes keep their spacing. The layout makes the
rise and the fall wait for the same two instructions, so those costs cancel in a high time
[inferred]; a scope (10b) would show it.

### 15.3 The bit timing (F2)

| | v3.3 | v3.4 | RMT ticks (12.5 ns) |
|-|-|-|-|
| 0: high, period, low | 337.5, 1300, 962.5 ns | the same | 27, 104, 77 |
| 1: high, period, low | 662.5, 1300, 637.5 ns | 700, 1325, 625 ns | 56, 106, 50 |
| reset | 300 us | the same | 2 x 12000 |

* T1H 700 ns, as the review suggested: about 662 ns at the LED with the level shifter's estimated
  38 ns [inferred: CHANGES-A2.md section 4], inside the Inolux (450 to 750), WS2812B-2020 (580 to
  1000) and older WS2812B (650 to 950 [reported]) windows both as sent and at the LED.
* The 1 bit's period is 1325 ns, not the 1.3 us the review suggested keeping: with 1.3 us T1L is
  600 ns, and the bit-bang's one pass round its loop at 80 MHz can take 25 ns off it, under the
  WS2812B-2020's 580 ns [verified: `test_status` fails with T1 1300 ns, the `t1-1300ns` mutant in
  `checks\status-led-mutants-v3.4.txt`]. 1325 ns keeps T1L at 625 ns, and the bit inside both
  datasheets' bit time (WS2812B-2020: 1.25 us or more [verified: datasheet p. 5, "Data cycle"];
  Inolux 1.25 +-0.6 us [verified: v2.2 p. 11]).
* The 0 bit is unchanged. Its low time, 962.5 ns sent, comes to about 1000 ns at the LED, the
  WS2812B-2020's maximum. No 0 bit fits every window at the LED as well: a bit of at least 1250 ns
  with T0L at the LED of at most 1000 ns needs T0H of at least about 340 ns once the bit-bang's
  jitter is counted, while T0H plus that jitter must stay at or under the WS2812B-2020's 380 ns;
  at 80 MHz that leaves a few nanoseconds, and with the test's slowest loop at 160 MHz none
  [inferred: the arithmetic, with the windows above]. A low time decides nothing in these parts
  while it stays far under the reset time [inferred], so T0 stays, and `STATUS-LED.md` 5 now says
  this plainly where v3.3 said the sent times leave room.
* `test_status.c` now checks every high time as the LED sees it too (38 ns shorter) against both
  datasheets, and the RMT frame's high times against the older WS2812B's as well [reported
  windows]. Least margin to any window edge: 12 ns in the RMT frame (T1H at the LED against the
  older WS2812B's 650 ns floor), 12.5 ns in the bit-bang [verified: `checks\host-tests-v3.4.log`].

### 15.4 `crash_test = 5`

The review asked for a scope check of a forced early panic. A real early panic (PSRAM not found,
or the DOS board's memory test failing) cannot be made on purpose short of a faulty chip, and none
of the app's own code runs before `esp_clk_init()` [inferred: `cpu_start.c`, where the app's init
functions and `app_main()` come after it]. So a crash-test build (`idf.py -DCRASH_TEST=1`, never the release images)
now takes `crash_test = 5`: on core 0, before the PC starts, it sets the CPU to 80 MHz
(`rtc_clk_cpu_freq_mhz_to_config(80)`, `rtc_clk_cpu_freq_set_config()`, which also sets the ROM's
ticks a microsecond [verified: ESP-IDF `esp_hw_support/port/esp32s31/rtc_clk.c` lines 243 to 276,
302, and 404 on]), tells the LED that `app_main()` has not run (`status_led_test_early()`), and calls
`abort()`. The panic path then sends yellow 1 at 80 MHz, as an early panic would [inferred]. Both
boards' crash-test builds compile and link with no new warning, and their bit-bang has the release
layout [verified: `checks\crash-test-builds-v3.4.txt`, `checks\warnings-wall-wextra-v3.4.txt`]; the
release images have no crash-test code [verified: `nm`, `strings`]. Not run [unknown]: whether
changing the clock at run time with both cores up goes cleanly. The UART's clock is REF_F80M, not
the CPU's [verified: `soc/esp32s31/include/soc/clk_tree_defs.h` line 301], so its text should stay
readable [inferred]. `TEST-PLAN.md` 10b says how to build, flash and probe it.

### 15.5 The smaller points

* F3: yellow 1 is the PSRAM chip not found, or on the DOS board its memory test failing; a PSRAM
  found but failing its set-up does not stop the start-up and most likely ends as red 5 [verified:
  `cpu_start.c` lines 662 to 680, both `sdkconfig.h`; the red 5 is inferred]. `STATUS-LED.md` 1
  and 2, `TEST-PLAN.md` 11, `status_code.h` and the console's name for the code say so.
* F4: the replay after RESET on a halted crash is tagged [unknown] in `STATUS-LED.md` 4 and
  `TEST-PLAN.md` 10, which asks Plat to note it; the code comment says the same.
* F5: the LED task's stack is 4096 bytes, as the project's other tasks; its use is still not
  measured [unknown].
* F6: if the LED task cannot be created, the LED is sent black, as the console then says.
* F7: `__wrap_esp_system_reset_modules_on_exit()` is defined only under `STATUS_LED_GPIO`, the
  boards the link wraps it for.
* F8: `README.md` says which lines of `previous-v3.2\SHA256SUMS` and `previous-v3.3\SHA256SUMS`
  check inside those folders and which from `v3\`.
* F9: the LP-core note's summary no longer tags "GPIO0 to 7 only" as verified beyond what its
  sources show (`..\research\status-led-lp-core.md` section 1); the Inolux timing table is cited on
  page 11 [verified: page footer "Page 11/19" in the v2.2 PDF the review downloaded]; section
  14.2 above still says p. 10, as it was written. The tree's `esp/dependencies.lock` shows as
  modified because the in-tree builds copy this folder's file in [verified: `cmp`], as before.
* `card-dosboard\tiny386\dosboard.ini` is unchanged; its first line still says it was written for
  v3.3, and every setting in it is the same for v3.4.

### 15.6 Sizes [verified: `checks\size-*-v3.4.txt`]

| | Korvo-1, v3.4 | Korvo-1, v3.3 | DOS board, v3.4 | DOS board, v3.3 |
|-|-|-|-|-|
| `tiny386.bin` | 885,616 | 885,552 | 914,688 | 914,624 |
| Flash `.text` | 425,204 | 425,192 | 420,512 | 420,500 |
| Flash `.rodata` | 229,824 | 229,792 | 260,172 | 260,140 |
| Internal RAM used by code and static data | 245,518 | 245,492 | 248,500 | 248,474 |
| Internal RAM left | 258,226 | 258,252 | 255,244 | 255,270 |
| LP SRAM | 8,244 | 8,244 | 8,244 | 8,244 |

At run time the LED task takes 1 KB more internal RAM than in v3.3 (its stack) [inferred]. The
dual-boot `tiny386.bin` uses 885,616 of its partition's 1,966,080 bytes [verified: flash
simulation].

### 15.7 Checks [verified unless marked]

| Check | Result |
|-|-|
| `build.sh` from nothing, twice (WSL `~/v34-fw/rel1`, `rel2-second-run`) | exit 0, 186 s and 179 s; all 18 files of the three image sets the same in both, ELF files included; the patch applied to a fresh clone gives the `v3.4` tag's tree exactly (420d2ffb); the bootloaders, tables and `flash_args` are v3.3's files byte for byte, so only `tiny386.bin` and `tiny386.elf` changed (`checks\reproducible-v3.4.txt`, `build-sh-v3.4-run1.txt`, `run2.txt`) |
| The images in `images\` | rel1's; each folder's `SHA256SUMS` verifies |
| `esptool image-info` | every bootloader and app valid, checksum and hash; the apps say `v3.4` (`checks\image-info-*-v3.4.txt`) |
| The bit-bang's layout | `build.sh`'s new check passes on all three ELF files and both crash-test builds, and fails on v3.3's (`checks\status-led-panic-path-v3.4.txt`); the panic path's code and data stay in internal RAM, and `esp_panic_handler` still calls the wrap (same file, part 2) |
| Compiler warnings | Korvo-1 and its standalone build: the old `i2s.c` "`i2s_task` defined but not used" only; DOS board: none. The files v3.3 and v3.4 changed, again with `-Wall -Wextra`: none but that one, in the release and the crash-test builds (`checks\warnings-wall-wextra-v3.4.txt`) |
| Host tests, scratch tree at `v3.4` | 25 passed, 0 failed (`checks\host-tests-v3.4.log`); again with an OUT folder of 98 characters, 25 of 25 (`host-tests-v3.4-long-out.log`); in the tree `build.sh` made from the patch, 23 of 23, the runs that need older tags left out (`host-tests-v3.4-from-patch.log`); the host build's warnings the same 11 as at v3.3 |
| Test 15, `test_status` | 34,460 checks (v3.3: 18,011), 0 failed: 128 bit-banged frames with the instructions before each store charged, v3.3's layout found short, high times at the LED, least margins 12 ns (RMT) and 12.5 ns (bit-bang). Under AddressSanitizer and UBSan: clean (`checks\asan-status-v3.4.txt`) |
| Mutation run | v3.3's twelve faults, moved to v3.4's text, and five new ones (T1H back to 662.5 ns, T1 back to 1300 ns, T0H 275 ns, the last low not waited out, a short reset): all 17 caught (`checks\status-led-mutants-v3.4.txt`). A first run missed the last-low fault; the test now checks the last bit's low, and catches it |
| The flashing targets | as v3.3, line for line: in the dual-boot build every flashing, erasing, DFU and otadata target depends on the refusing target; the standalone and DOS board builds refuse nothing (`checks\cmake-flash-refusal-v3.4.txt`; no flash target was run) |
| The flash simulation with the v3.4 images | the dual-boot install changes 0x2000 to 0x8FFF and 0xE20000 to 0xEF8FFF only, every Linux partition unchanged; GPIO42 high starts Linux, held low tiny386 v3.4; against v3.3's run only the app's size, hashes and version and the scratch folder differ (`checks\flash-simulation-v3.4.txt`, with the diff at its end) |
| The scripts' flow | the unchanged scripts against the v3.4 images with a stub esptool, run through cmd.exe with PATH set to `C:\Windows\System32` and `C:\Windows` only, so that no esptool but the stub could be found: 39 cases, no write reached the stub; against v3.3's run no esptool call, decision, WOULD WRITE line or exit differs, only the image's size, hashes, version, segment table and the dates (`checks\bat-harness-v3.4.txt`, made by `checks\bat-harness-make-flow-v3.4.py`) |
| The card | `setup.c` and `ini.c` on a pretend card: 0 problems; the card files are v3.3's byte for byte (`checks\card-check-v3.4.txt`) |
| `SHA256SUMS` | made again for every file it lists, `REVIEW-v32.md` added; every line verifies, and every image folder's own |

### 15.8 What only a board can say [unknown]

* Everything in 14.5, and: the bit-bang's real bit times at 80 and 320 MHz (`TEST-PLAN.md` 10b);
  whether `crash_test = 5`'s clock change runs cleanly; whether the LP SRAM keeps the record and
  the log ring through the RESET button; the LED task's stack use; how the Korvo-1's WS2812B takes
  T1H 700 ns and T0L at about 1 us.

### 15.9 For Plat to decide

* The name: v3.4 here, as a new tag. Retagging `v3.3` (or `v3.2`) instead is a one-line change to
  `esp/version.txt` and a rebuild.
* Whether to have crash-test images built and shipped beside the release ones; for now
  `TEST-PLAN.md` 10b builds one in WSL.

### 15.10 Scripts and logs

WSL `~/v34-fw/`: `b1.sh` (in-tree builds, release and with `CRASH=1` the crash-test ones),
`commit1.sh`, `rel.sh` (staging and the two from-nothing builds; `stage/` holds the staged
`build.sh`, patch, lock and dual-boot pair), `repro.sh`, `install.sh`, `t1.sh` (a quick
`test_status` run), `ht1.sh`, `checks1.sh` (host tests from the patch, the flash simulation in
`sim/`, the refusal query, the card check, the warnings), `w2.sh`, `mutate.sh` (`mutants/`),
`asan.sh`, `disasm.sh`, `bbt.sh`, `bbt2.sh`, `ct.sh`, `hgen.sh` and `harness-gen/`, `collect.sh`,
`hcmp.sh`, `collect2.sh`, the extracts in `txt/` and the disassemblies in `dis/`. The harness ran on
Windows from the session's scratch folder (`v34\harness`). Nothing was started that is still
running.


## 16. v3.5: the DOS board rev A2c, its speakers and a louder sound (2026-10-22)

The task: follow the DOS board's rev A2c GPIO moves (`..\..\tiny386-hdmi-board\dosboard\out\CHANGES-A2.md`
section 19), drive its new speaker amplifier (section 20), and make the default volume louder, with
the mix saturating rather than wrapping. v3.4's files are kept in `previous-v3.4\`. Nothing was
flashed, no serial port was opened, nothing was committed anywhere but the scratch tree, nothing has
run on a board.

### 16.1 The commits [verified: `git log v3.4..v3.5` in WSL `~/tiny386-idf-scratch/v3/tiny386`]

| Commit | What |
|-|-|
| db8eaca | `board_dosboard.h`, `dosboard.c`, `ch9350.c`, `i2s.c`: the rev A2c pins (16.2); the board check that drives no new pin before the MS7210 has answered (16.3); GPIO48 low from the board check, the speaker amplifier on it, the I2S DMA buffers cleared once sent (16.4) |
| b5c15df | `audio_mix.c`, `audio_mix.h` (new), `i2s.c`, `common.h`, `dosboard.h`, `dosboard.c`, `board_dosboard.h`, `main/CMakeLists.txt`: the mix in 32 bits, held to 16 bits once, after the level; `AUDIO_SHIFT_DEFAULT 2` on the DOS board (16.5) |
| 6c1a69d | `tests/host/run.sh`, `tests/host/dosboard/audio_mix_test.c` and `check_pins.py` (new): host tests 16 and 17 |
| 76394fa | `esp/version.txt`: `v3.5` |
| 8b4403f | `audio_mix_test.c`: the default checked to be the loudest level that cannot clip (the mutation run, 16.7, found a default of 3 passing before) |
| 07e7784 | `audio_mix_test.c`: its copy of v3.4's path multiplies by 32 where `pc.c` shifts a negative value left, which UBSan stops on; the test also frees its devices |

The tag `v3.5` was moved twice within this round, to 8b4403f and then to 07e7784, before anything was
shipped; both commits touch the test file only, and the images are the same bytes from all four
builds (16.7). `git diff v3.4 v3.5`: 13 files, 964 lines added, 72 taken out, all under `esp/` and
`tests/`, so the Linux route is not touched [verified: the same diff restricted to everything outside
`esp/` and `tests/` is empty]. The patch is `git diff pristine v3.5`: 121 files, 21,406 added, 668
taken out, 835,742 bytes (`tiny386-idf-v3.diffstat.txt`). `build.sh` changed in its first comment
only.

### 16.2 The pins [verified: `board_dosboard.h`; CHANGES-A2.md 19; host test 17]

| Function | v3.4 | v3.5 (rev A2c) |
|-|-|-|
| I2S BCLK, WS, DOUT (MS7210 and PCM5102A) | GPIO3, 4, 5 | GPIO51, 52, 53 |
| MS7210 RSTN (R603 27k to +3V3) | GPIO2 | GPIO49 |
| MS7210 INT (never configured; hot plug is polled) | GPIO6 | GPIO54 |
| CH9350L RST (high = reset) | GPIO7 | GPIO56 |
| AUD_XSMT: DAC soft mute, and now the speaker amplifier's shutdown | GPIO48 | GPIO48 |
| status LED, USB-A power, card power and detect, USER key, CH9350L S0, RX, TX, I2C, RGB, SD | as v3.4 | as v3.4 |

GPIO2 to GPIO7, GPIO57 and GPIO60 are on the J202 header and the firmware never configures them: they
stay as the chip leaves them, GPIO2 to GPIO7 with no pull and no input after reset, GPIO57 an input,
GPIO60 an input with a weak pull-up [verified: ESP32-S31 datasheet v0.5 table 2-1, read as text in
WSL `~/tiny386-idf-scratch/v35-work/txt/chip-ds.txt`]. GPIO0 and GPIO1 stay the MS7210's I2C bus,
shared with the header by design.

Host test 17 (`tests/host/dosboard/check_pins.py`) reads `board_dosboard.h` and the board's own KiCad
netlist (`..\..\tiny386-hdmi-board\dosboard\out\dosboard.net`, rev A2c, generated 2026-10-22 12:02):
each of the 43 pins the firmware names is on the net it should be (the module's pin names give the GPIO
numbers), each GPIO is named once, any pin-like name the script does not know fails, and no GPIO the
netlist puts on J202 is named, but GPIO0 and GPIO1. 0 failed [verified: `checks\host-tests-v3.5.log`].
Eight faulty copies of the header (I2S BCLK, RSTN or the CH9350L reset back on their old pins, the
CH9350L reset on GPIO57, WS and DOUT swapped, a new pin on GPIO5, the mute on GPIO60, two RGB data
pins swapped) all fail it [verified: `checks\audio-mix-mutants-v3.5.txt`]. The netlist lives outside
the tree, as `encoder.md` does for test 10; without it test 17 says SKIP (`DOSBOARD_NET` names it).

### 16.3 The board check: no new pin driven before the MS7210 answers

v3.4 made RSTN an output and pulsed it before looking for the MS7210, which was harmless when RSTN
was GPIO2 (the Korvo-1's I2S MCLK, an S31 output there). On rev A2c RSTN is GPIO49, the Korvo-1's
camera D3, an input to the S31 that the camera drives [verified:
`..\..\tiny386-hdmi-board\research\s31-module.md` 11], and CHANGES-A2.md 19 asks that the new outputs
be driven only after the MS7210 is found. Now [verified: `dosboard.c`; the built code,
`checks\dosboard-code-v3.5.txt` parts 1 to 4]:

1. The MS7210 is probed at 0x59, then 0x2B, with the probe's reset pulse (ms7210.c row A1) left out:
   `io_rstn()` touches GPIO49 only once `s_rstn_ours` is set. R603 has held RSTN high since power-on.
2. Found: RSTN is set high, made an output, and the chip is probed again, now with its reset pulse,
   so the MS7210 is set up from a clean reset as in v3.4.
3. Nothing answered, and an address probe finds one of a Korvo-1's own devices on the bus (its ES8389
   codec at 0x10 or 0x11, the panel kit's touch controller at 0x14 or 0x5D): the image stops, as it
   always has on a board that is not a DOS board, with RSTN never driven, and says which address
   answered. The 7-bit codec addresses are [inferred]: the Korvo-1 schematic writes "I2C ADDRESS:
   0x20" for the codec and "OV3660 I2C ADDRESS: 0x78" for the camera, whose 7-bit address is 0x3C, so
   the schematic gives 8-bit addresses [verified: the schematic's text, in the session scratchpad as
   `korvo1-sch.txt`, lines 700, 701 and 1597]; the touch controller's "0x14" may be either form, so both
   GT911 addresses are probed.
4. Nothing answered and none of those devices is there: perhaps a DOS board whose MS7210 needs the
   reset to answer (the data sheet wants reset released 10 ms or more after its supplies settle, which
   R603 alone does not give at power-on [verified: encoder.md 4.4]), so RSTN is driven and the probe
   with the reset runs, as in v3.4.
5. Something else answered at 0x59, or the bus could not be set up: the image stops as before.

The console says whether the MS7210 answered `before and after` its reset or `only after`, and a
chip found before its reset but not after stops the image with a message naming RSTN and GPIO49.
Only once the board is known are RSTN, the USB-A power (GPIO38), GPIO48 (low), the USER key and the
card detect configured. The extra probe adds about 70 ms to the start (row A1's waits, which still
run) [inferred: ms7210.c row A1, 20 ms and 50 ms].

### 16.4 The speakers [verified: `dosboard.c`, `i2s.c`, `LINE-OUT.md`]

The TPA2012D2's SDL and SDR are on AUD_XSMT with the PCM5102A's XSMT (CHANGES-A2.md 20), so the one
pin keeps the order v3.3 gave the DAC: low until the I2S clocks have run 20 ms, then high; low again in
a crash before the I2S output stops. What changed:

* GPIO48 is driven low from the board check, not only from `i2s_main()`, so the amplifier is off
  during the seconds of the test card, the card and the boot menu. Between power-on (or RESET) and the
  board check R703 holds it high and the amplifier is on with a silent input; only a board change can
  alter that (`LINE-OUT.md` gives the trade).
* The amplifier starts 3.5 ms after SDL and SDR rise [verified: TI SLOS438F, start-up time from
  shutdown], inside the 20 ms. TI gives no pop figure for it [unknown].
* The I2S channel now clears each DMA buffer once it has been sent (ESP-IDF's `auto_clear_after_cb`,
  "I2S will always send zero automatically if no data to send" [verified: ESP-IDF
  `components/esp_driver_i2s/include/driver/i2s_common.h` line 83 and `i2s_common.c` lines 937 to
  940]), so a sound task that falls behind leaves silence on HDMI, the line out and the speakers
  rather than its last buffers going round again. In a crash the interrupt that clears them cannot
  run, so the panic path's mute stays as it was.
* The UART line at the start now says `the line out (PCM5102A) and the speakers unmuted` with the
  level.

### 16.5 The level and the mix

What v3.4 did [verified: v3.4's `pc.c` `mixer_callback()` from line 901 and `i2s.c` lines 93 to 96]:
`pc.c` adds the SB16 and the AdLib and holds the sum to 16 bits, adds the PC speaker
(`(x - 0x80) << 5`, so plus or minus 4096) and holds again; `i2s.c` then divides by 16
(`audio_shift 4`). So the v3.4 path already saturated and could not wrap: the division only makes a
value smaller. Its fault was where the hold sat: before the level, so at any level two loud sources
together were clipped at the level's fraction of full scale (test 16: at level 2 the SB16 and the
AdLib each at 30,000 came out 8,191, the hold acting at 32,767 before the division) [verified:
`checks\host-tests-v3.5.log`].

What v3.5 does [verified: `esp/main/audio_mix.c`]: `audio_mix_devices()` asks the three devices as
`pc.c` does (same calls, same lengths, same order), adds them in 32 bits, divides by 2 to the power
`audio_shift` towards zero (as the old `/`), and only then holds to 16 bits, counting the values it
held. `i2s.c` calls it in place of `pc.c`'s `mixer_callback()` (left as it is, for the other front
ends) and the old division. The PC speaker's term is multiplied by 32 rather than shifted, since a left
shift of a negative value is undefined in C.

The default: `AUDIO_SHIFT_DEFAULT 2` in `board_dosboard.h`, used by `dosboard.c`'s default and
`i2s.c`; `dosboard.ini`'s `audio_shift` still sets it (0 to 8). Other boards keep 4. Why 2: the
largest sums the three devices can make are 69,598 and -69,632, 2.125 times full scale, so 2 is the
loudest whole step at which nothing can be held; at 2 those come out 17,399 and -17,408, 5.5 dB under
full scale, and one step louder they are held [verified: test 16 part 3]. On the speakers that is at
most about 3.2 V peak against the amplifier's 4.7 V at 5 V into 8 ohm, so it cannot clip the
amplifier either [inferred: `LINE-OUT.md`]. A lone source comes out exactly four times v3.4's, give or
take 3 in 32,768 of rounding: 12.0 dB louder [verified: test 16 part 4]. With the real devices at their
loudest (a full-scale SB16 square wave, all nine AdLib channels keyed on in step at full volume, the PC
speaker on), the AdLib's own output already reaches fmopl.c's 16-bit limit, and at level 2 the three
together peak at 17,151, nothing held; at level 1, 17,548 of 192,000 values are held; v3.4's path
held 72,626 at every level [verified: test 16 part 5].

The UART says, at most every 10 s and only if there were some, how many values were held at full
scale, a sign that `audio_shift` is set louder than the sound allows.

The Korvo-1: its board header defines no I2S ("No I2S: the Korvo-1 audio wiring is not described in
the Linux sources") [verified: `board_korvo1.h`], so it has no sound path to make louder, and nothing
was applied there. Its images differ from v3.4's in 66 bytes of each `tiny386.bin`: the version
character, the ELF hash in the app description, and the image's checksum and hash; in the ELF files
only `.flash.appdesc` differs (one byte) and no symbol differs [verified: `checks\reproducible-v3.5.txt`].

The task took the mixer to wrap; as above it did not, in v3.4 or in tiny386 itself, but its hold came
before the level. Test 16 would catch a wrapping mix (16.7).

### 16.6 Sizes [verified: `checks\size-*-v3.5.txt`]

| | Korvo-1, v3.5 | Korvo-1, v3.4 | DOS board, v3.5 | DOS board, v3.4 |
|-|-|-|-|-|
| `tiny386.bin` | 885,616 | 885,616 | 916,576 | 914,688 |
| Flash `.text` | 425,204 | 425,204 | 421,816 | 420,512 |
| Flash `.rodata` | 229,824 | 229,824 | 260,732 | 260,172 |
| Internal RAM used by code and static data | 245,518 | 245,518 | 248,512 | 248,500 |
| Internal RAM left | 258,226 | 258,226 | 255,232 | 255,244 |
| LP SRAM | 8,244 | 8,244 | 8,244 | 8,244 |

At run time the sound task's stack now holds about 450 bytes more (the three devices' buffers in
`audio_mix_devices()`) within its 4,096; its use is not measured [inferred: the arrays' sizes].

### 16.7 Checks [verified unless marked]

| Check | Result |
|-|-|
| `build.sh` from nothing, four times (WSL `~/tiny386-idf-scratch/v35-work/rel1` and `rel2-second-run` with the patch at 76394fa, `rel3` at 8b4403f, `rel4-final-patch` with the final patch) | exit 0, 170 to 184 s each; all 18 files of the three image sets the same in all four, ELF files included; each patch applied to a fresh clone gives its commit's tree, the final one the `v3.5` tag's (c80d5d8b); the bootloaders, tables and `flash_args` are v3.4's files byte for byte (`checks\reproducible-v3.5.txt`, `build-sh-v3.5-run1.txt` to `run4.txt`) |
| The images in `images\` | rel1's; each folder's `SHA256SUMS` verifies |
| `esptool image-info` | every bootloader and app valid, checksum and hash; the apps say `v3.5` (`checks\image-info-*-v3.5.txt`) |
| Compiler warnings | Korvo-1 and its standalone build: the old `i2s.c` "`i2s_task` defined but not used" only (now line 86); DOS board: none. The four files v3.5 changed in `esp/main`, again with `-Wall -Wextra`: none but that one, in the release and the crash-test builds (`checks\warnings-wall-wextra-v3.5.txt`) |
| Host tests, scratch tree at `v3.5` | 27 passed, 0 failed: v3.4's 25, test 16 (the mix) and test 17 (the pins) (`checks\host-tests-v3.5.log`); again with an OUT folder of 120 characters, 27 of 27 (`host-tests-v3.5-long-out.log`); in the tree `build.sh` made from the final patch, 25 of 25, the runs that need older tags left out (`host-tests-v3.5-from-patch.log`); the host build's warnings the same 11 lines as at v3.4 |
| Test 16, `audio_mix_test` | 7,753,016 checks, 0 failed: 10,752 corner cases at shifts -2 to 18 against a 64-bit model, 2,605,664 random values, v3.4's path matched wherever its holds did not act, the default level (2) holding nothing and one step louder holding, a lone source four times v3.4's, and the real SB16, AdLib and PC speaker through `audio_mix_devices()` at levels 0 to 4. Under AddressSanitizer and UBSan: clean; LeakSanitizer finds only fmopl.c's own table, allocated in `OPLOpenTable()` and never freed, 14 times as the test makes and frees 30 chips (`checks\asan-audio-mix-v3.5.txt`) |
| Mutation run | sixteen faults put into copies of `audio_mix.c` or the default (a 16-bit sum that wraps, no hold above or below, the hold before the level, rounding down, the PC speaker scaled or centred wrongly, the AdLib's right channel off by one, each device asked for half its block, no cap on the shift, held values not counted, defaults of 1, 3 and 4): all caught; the unchanged copy passes. Eight faulty pin maps: all caught by test 17 (`checks\audio-mix-mutants-v3.5.txt`) |
| The built DOS board code | `board_early_init()` calls `probe_ms7210()` and `i2c_master_probe()` before any GPIO call; `io_rstn()` drives GPIO49 only with `s_rstn_ours` set; `i2s_task()` calls `audio_mix_pc()`; `audio_panic_quiet` is in internal RAM and called from `status_led_panic`; the LED's bit-bang layout passes `build.sh`'s check in all three images (`checks\dosboard-code-v3.5.txt`) |
| The flashing targets | as v3.4, line for line (`checks\cmake-flash-refusal-v3.5.txt`; no flash target was run) |
| The flash simulation with the v3.5 images | the dual-boot install changes 0x2000 to 0x8FFF and 0xE20000 to 0xEF8FFF only, every Linux partition unchanged; GPIO42 high starts Linux, held low tiny386 v3.5; against v3.4's run only the apps' checksums, hashes and version and the scratch folder differ (`checks\flash-simulation-v3.5.txt`) |
| The scripts' flow | the unchanged scripts against the v3.5 images with a stub esptool, through cmd.exe with PATH set to `C:\Windows\System32` and `C:\Windows` only: 39 cases, no write reached the stub; against v3.4's run no esptool call, decision, WOULD WRITE line or exit differs, only the version, hashes, a checksum and the dates (`checks\bat-harness-v3.5.txt`, made by `checks\bat-harness-make-flow-v3.5.py`) |
| The card | `setup.c` and `ini.c` on a pretend card with the v3.5 `dosboard.ini`: 0 problems; the card files are v3.4's but `dosboard.ini`, whose only changed setting is `audio_shift = 2` (and its comments); every key in it is one `dosboard.c` knows (`checks\card-check-v3.5.txt`) |
| `SHA256SUMS` | made again for every file it lists; every line verifies, and every image folder's own |

Not covered by any test here: whether the MS7210 answers before its reset on a real board, the
TPA2012D2's and the DAC's clicks at each edge, how loud the default really is on the Waveshare
speakers and on a TV, the auto-cleared DMA buffers on the chip, and the sound task's stack.

### 16.8 What only a board can say [unknown]

* Whether the MS7210 answers before its reset with R603 alone (the console line says `before and
  after` or `only after`).
* Clicks: at power-on (amplifier on by R703), at the board check (amplifier off), when the PC starts
  (both on), in a crash (amplifier off at once) and at RESET.
* The loudness at `audio_shift = 2` on the speakers and on HDMI, and whether 1 suits better.
* On a Korvo-1 with the DOS board image by mistake: whether its codec or touch controller answers at
  the addresses probed. If none does, the image pulses GPIO49 once (about 70 ms) before stopping, as
  v3.4 pulsed GPIO2.
* Everything in 15.8 still stands.

### 16.9 For Plat to decide

* AUD_XSMT's pull: R703 pulls up, so the speaker amplifier is on from power-on until the board check
  and through every RESET. A pull-down would keep it off until the firmware turns it on, at the price
  of a line out that plays only with the firmware running (`LINE-OUT.md`). A board change, for the
  board step.
* The default level: 2 here; 1 is twice as loud but can clip loud mixes and the speakers.
* Sound on the Korvo-1: this firmware has none there. The research table puts the Korvo-1's codec
  (an ES8389 with two NS4150B amplifiers) on I2S GPIO2 to GPIO6 with its amplifier enable on GPIO7
  [verified: `..\..\tiny386-hdmi-board\research\s31-module.md` 7.6 and 11]; driving it would be a
  separate piece of work.

### 16.10 Scripts and logs

WSL `~/tiny386-idf-scratch/v35-work/`: `b1.sh` (in-tree builds, `CRASH=1` for the crash-test ones),
`mkstage.py` with `stage1/` and `final/` (the first commit's intermediate files), `rel.sh`, `rel3.sh`,
`rel4.sh` (staging and the four builds from nothing; `stage/` holds the staged `build.sh`, patch, lock
and dual-boot pair), `repro.sh`, `install.sh`, `checks1.sh` (host tests from the patch, the flash
simulation in `sim/`, the refusal query, the card check, the warnings), `ht1.sh`, `ht2.sh` (host
tests), `t1.sh` (the two new tests alone), `w1.sh`, `c2.sh`, `warn3.py`, `mutate.sh` (`mutants/`),
`asan.sh`, `dis.sh`, `hgen.sh` and `harness-gen/`, `collect.sh`, `collect2.sh`, `collect3.sh`, the
data sheet text in `txt/`. On Windows the session's scratch folder `v35\` holds the edited sources
(`src\`), the scripts as written and the harness (`v35\harness`). Nothing was started that is still
running.


## 17. v3.6: the fixes from the review of v3.5 (2026-10-22)

The review of v3.5 (`REVIEW-v35.md`) found no blocker and no major fault: three minor points and four
notes; `REVIEW-RESOLUTION.md` round 4 has each one and what became of it. v3.5's images had already
been written to this folder, so the fixes are a new tag, `v3.6`, as v3.4 was, and the app calls
itself `v3.6`. v3.5's files are kept in `previous-v3.5\`. Nothing was flashed, no serial port was
opened, nothing was committed anywhere but the scratch tree, nothing has run on a board.

### 17.1 The commits [verified: `git log v3.5..v3.6` in WSL `~/tiny386-idf-scratch/v3/tiny386`]

| Commit | What |
|-|-|
| 152a4a9 | `dosboard.c`, `board_dosboard.h`: when nothing answers on I2C, GPIO46 (R302) decides whether RSTN is pulsed; the header and the file's own comments say exactly what is driven before the MS7210 answers (F2, F3) |
| 92f61f7 | `audio_mix.c`, `audio_mix.h`: `audio_mix_devices()` makes nothing for an n of 0 or less; both files mode 644 (N1, N2) |
| 25933f6 | `tests/host`: host test 18, the board check, with its stand-in headers in `tests/host/dosboard/shim`; test 16 part 6, the mix asked for no block; `audio_mix_test.c` mode 644 |
| 3017d38 | `esp/version.txt`: `v3.6` |
| aff546d | `board_check_test.c`: case K6, a GPIO46 that reads high only on the second of its reads (the mutation run, 17.8, found a check that looked only at its last read passing every other case) |
| 9d6109e | `board_dosboard.h`: the comment before the pins says that until the MS7210 answers the image drives the I2C bus and what both boards share (the console's UART0, the status LED on GPIO37), not the I2C bus alone; a comment only |

The tag `v3.6` was moved twice within this round, from 3017d38 to aff546d and then to 9d6109e,
before anything was shipped: aff546d touches the test file only and 9d6109e a comment, and the
images are the same bytes from all five builds (17.8). `git diff v3.5 v3.6`: 18 files, 851 lines
added, 22 taken out, all under `esp/` and `tests/`, so the Linux route is not touched [verified:
the same diff restricted to everything outside `esp/` and `tests/` is empty]. `git diff` from v3.5
to v3.6 with its summary option shows the three mode changes, 100755 to 100644. The patch is
`git diff pristine v3.6`: 132 files, 22,235 added, 668 taken out, 869,092 bytes
(`tiny386-idf-v3.diffstat.txt`). `build.sh` changed in its first comment only.

An aside on checking a comment-only change: two in-tree DOS board builds (`idf.py -B`) of the same
sources in two build folders give different `tiny386.bin` files, since the ELF carries the build
folder's name and the app description carries the ELF's hash [verified: `build-v36d` against
`build-v36d3` in the scratch tree, the same sources, differ at byte 177; the folder name is in the
ELF's strings]. `build.sh`'s builds do not have that dependence (none of the five ELF files holds its
own work folder's name), so the builds from nothing are the ones to compare [verified].

### 17.2 The board check and GPIO46 (F2, F3)

v3.5's check pulsed RSTN (GPIO49) whenever nothing answered at 0x59 or 0x2B and none of a Korvo-1's
addresses (0x10, 0x11, 0x14, 0x5D) answered either, and it stopped whenever one of those addresses
answered. The review found two corner cases in that: a Korvo-1 with nothing on its bus got GPIO49
(its camera's D3) pulsed, and a DOS board with something on J202's I2C pins at one of those
addresses stopped if its MS7210 answers only after its reset. It also found three places that said
no pin but the I2C bus is driven before the MS7210 answers, which the first case contradicts.

What the board gives [verified: `..\..\tiny386-hdmi-board\dosboard\out\dosboard.net`, rev A2c,
12:02]: net KM_S0 joins the module's GPIO46 (U201 pad 56), the CH9350L's S0 (U301 pin 4), TP946 and
R302 pin 1; R302 (1k) has its pin 2 on GND. R303 (10k) holds KM_RST, the CH9350L's RST (high =
reset), at GND. On the Korvo-1, GPIO46 is camera D0, an input to the S31 [verified:
`..\..\tiny386-hdmi-board\research\s31-module.md` 11]. After reset GPIO46 is an input with no pull
[verified: ESP32-S31 datasheet v0.5, table 2-1, pin 59, "IE"]; the weak pull-up is 45 kohm typical,
VIL at most 0.25 x VDD and VIH at least 0.75 x VDD [verified: the same datasheet, table 5-4].

What v3.6 does [verified: `dosboard.c`; the built code, `checks\dosboard-code-v3.6.txt` part 1]:

1. As v3.5: the MS7210 is probed at 0x59, then 0x2B, without its reset pulse. Found: RSTN is made an
   output (set high first) and the chip probed again with the reset; GPIO46 is not touched.
2. As v3.5: no bus, or something else at 0x59: the image stops.
3. Nothing answered: the Korvo-1 addresses are probed (for the messages only), then GPIO46 is read:
   `gpio_config()` with input and the pull-up on, 100 us, four reads 10 us apart, then
   `gpio_config()` with input and no pull, as the chip had it. It counts as low only if every read
   was low. In the built code the two `gpio_config_t` constants carry the mask 1 << 46, mode 1
   (input), pull-up 1 and 0, pull-down 0 [verified: objdump].
4. GPIO46 high: no R302, so no DOS board. The image stops with RSTN never driven, whatever answered
   on the bus, and the message says `GPIO46 high (no R302)`, with the Korvo-1 address if one
   answered.
5. GPIO46 low: a DOS board. RSTN is driven and the probe with the reset runs, as before v3.5, even if
   a Korvo-1 address answered; such a device is then noted on the console as `a device at I2C 0x..
   too`, taken to be on J202.

On a DOS board, R302's 1k against the 45k pull-up puts GPIO46 at about 0.07 V [inferred], far under
VIL. The CH9350L reads S0 only at its own reset (`ch9350.c` line 456), and R303 keeps it out of reset
until `ch9350_setup()` gives one, so about 70 uA through R302 for 0.14 ms changes nothing there
[inferred]. On a Korvo-1 whose camera has not been set up, D0 should read high with the pull-up
[inferred: a sensor keeps its data pads off until told; not checked for the OV3660].

The one case the strap cannot tell apart (case K4 of test 18): a Korvo-1 whose camera holds D0 low
while its codec answers. v3.5 stopped there with GPIO49 untouched; v3.6 takes GPIO46's low for
R302 and pulses GPIO49 once (about 70 ms) before stopping. It needs the DOS image on a Korvo-1 by
mistake and a camera driving D0 low before anything has set it up [inferred: unlikely]. The trade
was made for the DOS board, whose J202 is meant for end users' devices; `TEST-PLAN.md` 10c lets Plat
see which way his Korvo-1 reads.

F2's three places now say what v3.6 does: `README.md` (In short), `FLASH.txt` 6 and
`board_dosboard.h` lines 17 to 25; `dosboard.c`'s own comments too. `board_dosboard.h` also
corrects v3.5's "GPIO49 to GPIO57 are the Korvo-1's camera inputs" to GPIO46 to GPIO57, all inputs
but GPIO55 (the camera's XCLK) [verified: `s31-module.md` 11].

Host test 18 (`tests/host/dosboard/board_check_test.c`) builds `dosboard.c` as the DOS board build
does (`board_dosboard.h` included first) with the real `ms7210.c`, against stand-in headers in
`tests/host/dosboard/shim` and a pretend I2C bus and pins behind them; each case runs in a child
process of its own, since `dosboard.c` keeps what it found in statics. 17 cases: six Korvo-1s (codec,
touch panel, nothing on the bus; camera idle, holding D0 low or high, or changing while read) and
eleven DOS-board-like ones (the MS7210 before and after its reset, only after, at 0x2B, with a
device at 0x5D or 0x10 on J202, without R302 either way, another chip at 0x59, no bus, an MS7210
lost after its reset, none at all). Each checks that no pin is touched but the I2C pair (through
the bus set-up), GPIO46, GPIO49 and, once the MS7210 has answered, GPIO38, GPIO48, GPIO42 and
GPIO50; that before the MS7210 answers only GPIO46 is touched, and GPIO49 only after GPIO46 read low
on every read; that GPIO46 is read only with its pull-up on and no pull-down, is never an output and
is left an input with no pull; that GPIO49 and GPIO38 are set high and GPIO48 low before each is made
an output, and GPIO48 never set high here; and the outcome: taken (at which address, before and
after or only after the reset, the J202 note) or stopped with yellow 2 and a message naming what was
seen. 17 of 17 as expected [verified: `checks\host-tests-v3.6.log`]. Against v3.5's `dosboard.c` it
fails 12 of 17: K3, K5 and K6 (GPIO49 driven on a Korvo-1 with nothing answering), D4 and D5 (a DOS
board stopped by a device on J202), D6 (taken without R302, where v3.6 stops), K4 (stopped with
GPIO49 untouched, which v3.6 gives up, below), and K1, K2, D2, D3 and D11 (GPIO46 not read, other
messages) [verified: the same log, run.sh's v3.5 comparison].

### 17.3 The mix asked for no block, and the file modes (N2, N1)

`audio_mix_devices()` now returns 0 straight after the clamp and the rounding to even when n comes to
0 or less: no device is asked and `out` is left alone [verified: `audio_mix.c`; the built code,
`checks\dosboard-code-v3.6.txt` part 5, a `blez` before the first `memset`]. `i2s.c` still passes
`MIXER_BUF_LEN` (128) under its `_Static_assert`, so the sound path itself does not change. Test 16
part 6 asks for n of 1, 0, -1, -2, -128, -129 and `INT_MIN` with every device playing: 0 returned,
`out` untouched, the SB16 not asked; and a block of 2 still asks them. Against v3.5's `audio_mix.c`
the same test stops in part 6: AddressSanitizer reports `negative-size-param: (size=-2)` at line 64
(`memset(opl, 0, n)`), and without sanitisers it dies of SIGSEGV [verified:
`checks\asan-audio-mix-board-check-v3.6.txt`]. Test 16 makes 7,753,038 checks, 22 more than v3.5's.

`audio_mix.c`, `audio_mix.h` and `audio_mix_test.c` are mode 644 in the tree and in the patch; the
tree `build.sh` makes from the patch has them 644 [verified: `git ls-files -s`,
`checks\reproducible-v3.6.txt`]. The images do not depend on the modes.

### 17.4 The sound task's stack (N3)

The review measured it; measured again here with the in-tree DOS board build's own command lines
plus `-fstack-usage` [verified: `checks\stack-sound-v3.6.txt`]: `i2s_task` 352 bytes,
`audio_mix_devices` 496, `YM3812UpdateOne` 112, `sb16_audio_callback` 16, `pcspk_callback` 32,
`audio_mix_pc`, `audio_mix_block` and `adlib_callback` 0 (leaves or tail calls), all as the review's
v3.5 figures. So the mix path is about 960 bytes deep plus `i2s_channel_write()`, in a 4,096-byte
task; 16.6's estimate of about 450 bytes more than v3.4 was a little high, the review's 384 is the
measured one [inferred: the sum of the frames]. The UART's 10 s stack line names `i2s_task`, so the
first board run shows the real margin [unknown until then]. `board_early_init()` (192 bytes, the
strap read inlined) runs on the main task, not this one.

### 17.5 The Korvo-1 guard on the board (N4)

`TEST-PLAN.md` 10c, new and optional: the DOS board image written to the dual boot's test slot at
0xE20000 only (917,296 of 1,966,080 bytes), started with VOL+ held, then the Korvo-1 image put back
with `flash-dualboot.bat COM10 app`; Linux is not touched. What to expect is from host test 18 (K1
to K6); that the dual-boot bootloader starts the DOS app from that slot is [inferred]: the two
builds' `sdkconfig.h` differ only in the partition table's name, `CONFIG_SPIRAM_MEMTEST` (DOS board)
and the USB host options [verified: diff of the two]. It writes flash, so it is Plat's choice.

### 17.6 R703 (F1)

A board change, for the board step: R703 as 10k to GND instead of to the DAC's 3.3 V, so that the
speaker amplifier and the DAC stay off from power-on to the board check and through every reset.
The review's figures were checked against the data sheets [verified: TPA2012D2 SLOS438F p. 4, VIL
0.35 V for SDL and SDR, and p. 5, |IIL| at most 5 uA; PCM5102A SLAS859C p. 7, IIL 10 uA at most in
size at 0 V, VIL 0.3 x DVDD; the netlist: R703 between /Audio_option/AUD_3V3 and AUD_XSMT, which reaches
U701.17, U702.7 and U702.8], and `LINE-OUT.md` now gives the recommendation, the reason a missing
R703 is not enough, and the price. The firmware drives GPIO48 both ways from the board check on and
needs no change for either fitting; two comments name R703 as a pull-up (`board_dosboard.h` at
`AUDIO_MUTE_GPIO`, `i2s.c`). Nothing was changed in the board's generators here.

### 17.7 Sizes [verified: `checks\size-*-v3.6.txt`]

| | Korvo-1, v3.6 | Korvo-1, v3.5 | DOS board, v3.6 | DOS board, v3.5 |
|-|-|-|-|-|
| `tiny386.bin` | 885,616 | 885,616 | 917,296 | 916,576 |
| Flash `.text` | 425,204 | 425,204 | 422,100 | 421,816 |
| Flash `.rodata` | 229,824 | 229,824 | 261,164 | 260,732 |
| Internal RAM used by code and static data | 245,518 | 245,518 | 248,512 | 248,512 |
| Internal RAM left | 258,226 | 258,226 | 255,232 | 255,232 |
| LP SRAM | 8,244 | 8,244 | 8,244 | 8,244 |

### 17.8 Checks [verified unless marked]

| Check | Result |
|-|-|
| `build.sh` from nothing, five times (WSL `~/tiny386-idf-scratch/v36-work/rel1` and `rel2-second-run` with the patch at 3017d38, `rel3-final-patch` at aff546d, `rel4-final-patch` and `rel5-final-patch-again` with the final patch at 9d6109e) | exit 0, 185 to 256 s each; all 18 files of the three image sets the same in all five, ELF files included; each patch applied to a fresh clone gives its commit's tree, the final one the `v3.6` tag's (7c24c86f); the bootloaders, tables and `flash_args` are v3.5's files byte for byte (`checks\reproducible-v3.6.txt`, `build-sh-v3.6-run1.txt` to `run5.txt`) |
| The Korvo-1 apps against v3.5's | the same length; 66 bytes of the dual-boot `tiny386.bin` differ and 65 of the standalone one (one byte of its ELF hash happens to match): the version character at 0x33, the ELF's SHA-256 at 0xB0 to 0xCF, and the checksum and appended SHA-256 at the end; in the ELF only `.flash.appdesc` differs among the loadable sections, and the symbol names are the same (`checks\reproducible-v3.6.txt`) |
| The DOS board app against v3.5's | `.flash.text`, `.flash.rodata`, `.iram0.text`, `.dram0.data` and `.flash.appdesc` differ; one symbol is new, `s_strap` (same file) |
| The images in `images\` | rel1's, which are rel4's file for file; each folder's `SHA256SUMS` verifies |
| `esptool image-info` | every bootloader and app valid, checksum and hash; the apps say `v3.6`; rel3's and rel4's image-info files are the same as rel1's (`checks\image-info-*-v3.6.txt`) |
| Compiler warnings | Korvo-1 and its standalone build: the old `i2s.c` "`i2s_task` defined but not used" only; DOS board: none. `dosboard.c`, `audio_mix.c`, `i2s.c` and `ch9350.c` again with `-Wall -Wextra` in rel4's builds: none but that one, in the release and the crash-test builds; the four in-tree builds (release and `-DCRASH_TEST=1`, both boards) the same (`checks\warnings-wall-wextra-v3.6.txt`) |
| Host tests, scratch tree at `v3.6` | 29 passed, 0 failed: v3.5's 27, test 18 and its run against v3.5's `dosboard.c` (`checks\host-tests-v3.6.log`); again with an OUT folder of 123 characters, 29 of 29 (`host-tests-v3.6-long-out.log`); in the tree `build.sh` made from the final patch, 26 of 26, the three runs that need the v3, v3.1 and v3.5 tags left out (`host-tests-v3.6-from-patch.log`); the host build's warnings the same 11 lines as at v3.5 |
| Test 18 | 17 of 17 cases as expected; against v3.5's `dosboard.c`, 12 of 17 fail, as 17.2 says. Under AddressSanitizer and UBSan: clean, every case (`checks\asan-audio-mix-board-check-v3.6.txt`) |
| Mutation run, test 18 | sixteen faults put into copies of `dosboard.c` (no pull-up, a pull-down, GPIO46 not put back, the strap's sense inverted, no gate, v3.5's stop on a Korvo-1 address, GPIO46 read on every board, GPIO46 an output, RSTN made an output before it is high, RSTN taken before the first probe, GPIO48 made an output before it is low, GPIO48 set high, the message without GPIO46, no J202 note, only the last read counted, no strap at all): all 16 caught; the unchanged file passes. Without case K6 the last-read fault would have gone through: K6 was added for it (`checks\board-check-mutants-v3.6.txt`, made by `board-check-mutants-v3.6.py`) |
| Test 16 | 7,753,038 checks, 0 failed, part 6 included. Under AddressSanitizer and UBSan: clean; LeakSanitizer finds only fmopl.c's own table from `OPLOpenTable()`, now 15 times, as part 6 makes one more AdLib. Against v3.5's `audio_mix.c` part 6 stops the program (same file) |
| The built DOS board code | `board_early_init()`: the I2C bus, `probe_ms7210()`, `i2c_master_probe()`, then GPIO46's `gpio_config`, four `gpio_get_level(46)`, `gpio_config`, and only then the branch to RSTN or to `wrong_board()`; `io_rstn()` drives GPIO49 only with `s_rstn_ours` set; the panic path's mute as in v3.5; the LED's bit-bang layout passes `build.sh`'s check in all three images (`checks\dosboard-code-v3.6.txt`) |
| The sound task's stack | as 17.4 (`checks\stack-sound-v3.6.txt`) |
| The flashing targets | as v3.5, line for line (`checks\cmake-flash-refusal-v3.6.txt`; no flash target was run) |
| The flash simulation with the v3.6 images | the dual-boot install changes 0x2000 to 0x8FFF and 0xE20000 to 0xEF8FFF only, every Linux partition unchanged; GPIO42 high starts Linux, held low tiny386 v3.6; against v3.5's run only the apps' checksums and hashes and the scratch folder differ (`checks\flash-simulation-v3.6.txt`) |
| The scripts' flow | the unchanged scripts against the v3.6 images with a stub esptool, through cmd.exe with PATH set to `C:\Windows\System32` and `C:\Windows` only: 39 cases, no write reached the stub; against v3.5's run no esptool call, decision, WOULD WRITE line or exit differs, only the version, hashes, a checksum and the dates (`checks\bat-harness-v3.6.txt`, made by `checks\bat-harness-make-flow-v3.6.py`) |
| The card | `setup.c` and `ini.c` on a pretend card: 0 problems; the card files are v3.5's byte for byte; every key in `dosboard.ini` is one `dosboard.c` knows (`checks\card-check-v3.6.txt`). `card-dosboard\tiny386\dosboard.ini` is v3.5's file: its first line still says v3.5, and every setting in it is the same for v3.6 |
| `SHA256SUMS` | made again for every file it lists, `REVIEW-v35.md` added; every line verifies, and every image folder's own |

Not covered by any test here: the strap's real levels on either board, the OV3660's pads before it
is set up, and everything in 16.8.

### 17.9 What only a board can say [unknown]

* What GPIO46 reads on Plat's Korvo-1 (`TEST-PLAN.md` 10c), and on the DOS board (the UART line
  `GPIO46 low (R302)` appears only if the MS7210 did not answer before its reset).
* Everything in 16.8 still stands.

### 17.10 For Plat to decide

* R703: the review and this round recommend 10k to GND (`LINE-OUT.md`), for the board step. The
  firmware is right either way.
* The trade in 17.2: a DOS board with a device on J202 at 0x10, 0x11, 0x14 or 0x5D starts (v3.6),
  against a Korvo-1 whose camera holds D0 low having GPIO49 pulsed once if the DOS image is put on it
  by mistake. Going back to v3.5's rule for that one case is one line in `dosboard.c`.
* Whether to run `TEST-PLAN.md` 10c; it writes only the dual boot's test slot.
* The name: v3.6 here, as a new tag, as v3.4 was.

### 17.11 Scripts and logs

WSL `~/tiny386-idf-scratch/v36-work/`: `sync.sh` (the edited sources from the session's scratch folder
`v36\src` into the tree, C files 644), `t18.sh` (test 18 alone, and against v3.5), `b1.sh` (in-tree
builds, `CRASH=1` for the crash-test ones), `ht1.sh`, `ht2.sh` (host tests), `asan.sh`, `su.sh` and
`su36.py` (the stack), `commit1.sh` to `commit4.sh` (`commit4.sh` amended the last commit's
message, bbc1558, which had said the images change with the comment, into 9d6109e; the tree is the
same, 7c24c86f), `mut.sh` (`mutants/`),
`b2.sh` (the in-tree comparison in 17.1's aside), `rel.sh`, `rel3.sh`, `rel45.sh` (staging and the
five builds from nothing; `stage/` holds the staged `build.sh`, patch, lock and dual-boot pair),
`repro.sh`, `install.sh`, `install2.sh`, `dis.sh`, `checks1.sh` (host tests from the patch, the
flash simulation in `sim/`, the refusal query, the card check, the warnings), `hgen.sh` and
`harness-gen/`, `collect.sh`, `collect2.sh`, `collect3.sh`, `collect4.sh`, `runfinal.sh`,
`runfinal2.sh`, the PCM5102A data sheet's text in `txt/`.
On Windows the session's scratch folder `v36\` holds the edited sources (`src\`), the scripts as
written, `mutants.py`, `hcol6.py`, `net.py` and the harness (`v36\harness`, run through its `go.cmd`).
Nothing was started that is still running.


## 18. v3.7: the status LED dimmer, its brightness in the ini (2026-10-22)

Plat wrote v3.6 to his Korvo-1 and found the status LED (the WS2812B on GPIO37) far too bright
[reported: Plat, 2026-10-22]; nothing else from that run has come back. The task: make the LED much
dimmer by default and its brightness a setting, as one scale applied wherever a colour becomes a
frame, in normal running and in the panic path alike. The app calls itself `v3.7`, a new tag as
v3.4 and v3.6 were; v3.6's images, patch and notes are kept in `previous-v3.6\`. Nothing was
flashed, no serial port was opened, nothing was committed anywhere but the scratch tree, and
nothing of v3.7 has run on a board.

### 18.1 The commits [verified: `git log v3.6..v3.7` in WSL `~/tiny386-idf-scratch/v3/tiny386`]

| Commit | What |
|-|-|
| 36e98d2 | `status_code.c`, `status_code.h`, `status_led.c`, `status_led.h`: the colour table at full scale; `status_scale()`, `status_frame_grb()` and the level, `STATUS_BRIGHTNESS_DEFAULT 9`; both paths' frames through `status_frame_grb()`; the LED task sends again when the level changes; the boot console's LED line gives the level (18.2) |
| e15fdc8 | `esp_main.c`: `[esp] led_brightness` (18.3) |
| 6afa75b | `tests/host/test_status.c`, `tests/host/run.sh`: test 15 part 8, the brightness (18.4) |
| 012158e | `esp/version.txt`: `v3.7` |

`git diff v3.6 v3.7`: 8 files, 569 lines added, 38 taken out, all under `esp/` and `tests/`, so the
Linux route is not touched [verified: the same diff restricted to everything outside `esp/` and
`tests/` is empty]. No file mode changed. The patch is `git diff pristine v3.7`: 132 files, 22,766
added, 668 taken out, 891,666 bytes (`tiny386-idf-v3.diffstat.txt`). `build.sh` changed in its
first comment only.

### 18.2 The scale [verified: `status_code.c`, `status_led.c`; test 15]

The colour table holds v3.6's colours multiplied together by 255/70 and rounded, so that red, the
brightest channel anywhere in it, is 255 and every colour keeps its hue and its share of light
against the others:

| Colour | v3.6 | v3.7 table | v3.7 at the default, 9 |
|-|-|-|-|
| white | 0x282828 | 0x929292 | 0x050505 |
| blue | 0x00003c | 0x0000db | 0x000008 |
| cyan | 0x002d2d | 0x00a4a4 | 0x000606 |
| magenta | 0x320032 | 0xb600b6 | 0x060006 |
| green | 0x003200 | 0x00b600 | 0x000600 |
| red | 0x460000 | 0xff0000 | 0x090000 |
| yellow | 0x3c2300 | 0xdb8000 | 0x080500 |

`status_pattern()` and the rest still give table colours. Where a colour becomes a frame,
`status_frame_grb(rgb)` scales it by the level and puts it in wire order: `send_rgb()` (the RMT
path) and both of the panic path's `status_bb_frame()` calls, in `status_led_panic()` and
`status_led_halt()`, take it, and nothing in `status_led.c` calls `status_grb()` alone any more.
`status_scale()` gives each channel `(c * level + 127) / 255`, the nearest whole step to c * level
/ 255 (255 being odd, there is never a tie), in 32 bits, the largest product 65,152; a channel that
is lit in the table but would come to 0 at a level above 0 is given 1. A level above 255 is taken as
255. So 0 is dark, 255 is the table, and 70 gives v3.6's colours exactly.

Why 9: v3.6's brightest channel is red's 70; the task asked for roughly an eighth of that light,
about 8 to 10 on the brightest channel. At 9 each colour's three channels add up to 0.120 to 0.137
of v3.6's sum (white 0.125, red 0.129, yellow 0.137), the duties the LED's three PWM channels run
at, so about an eighth for each colour, and no lit channel is under 5, so the floor of 1 does not
act at the default [verified: test 15]. At 8 red would be 8, white 5 and yellow 7:4; at 10 red 10,
white 6 and yellow 9:5 [verified: the same formula]; either fits the task too. Whether 9 looks right
is [unknown].

Telling the colours apart: the seven colours have seven different sets of lit channels, and the
floor keeps every lit channel lit at every level from 1 to 255, so no two colours are alike at any
level above 0 (yellow, red and green lit, never becomes red). From the default up, the ratio of any
two lit channels of a colour stays within 14.1 per cent of the table's; the most off is yellow at
level 11 (9:6 against 219:128); at the default yellow is 8:5, 1.6 to 1, where v3.6 sent 60:35, 1.71
to 1 [verified: test 15]. How 8:5 looks on either LED is [unknown].

The level is `s_level`, one aligned word in internal RAM (`STATUS_DRAM`), its initial value 9 in the
image's own data, so it is 9 from power-on, for the first white, the replay after a reset and a
panic before `app_main()`; `status_scale()` and `status_frame_grb()` are `STATUS_IRAM`. In the built
code `status_scale()` is inlined into `status_frame_grb()` (and, having no other caller, left out
on its own), which loads the level once, holds it to 255 with `minu`, multiplies with `mul` and,
where a channel can come to 1 or more, divides with the core's own `divu` instruction; it calls
nothing. On the Korvo-1 it sits at 0x2f026b7c in internal RAM and `s_level` at 0x2f03643c, on the
DOS board at 0x2f026bcc and 0x2f037154, the level's four bytes 09 00 00 00 in each image; nothing
of the panic path is outside internal RAM; `status_led_panic()`, `status_led_halt()` and
`send_rgb()` (compiled as `send_rgb.part.0`) each call it [verified:
`checks\status-led-brightness-v3.7.txt`, all three images]. The bit-bang's layout is as v3.4 to
v3.6 had it: `build.sh`'s check passes in all three images.

The LED task now also sends at once when the level differs from the one its last frame was sent
at, so a new level shows within one look, 50 ms; the level is read before the frame is made, so a
change in between only costs one more frame. `status_led_report()` adds the level to its line:
`Status LED on GPIO37: colours and blinks as in STATUS-LED.md, brightness 9 of 255 ([esp]
led_brightness).`

### 18.3 `[esp] led_brightness` [verified: `esp_main.c`; `checks\ini-led-brightness-v3.7.txt`]

A new key in `known_keys`, read in `parse_esp_ini()` with the other `[esp]` keys: a whole number,
`strtol()` base 10 with nothing after it but spaces. 0 to 255 is taken as it is; below 0 or above
255 the nearer end is used with a yellow line; anything that is not a number (`abc`, `20abc`,
`0x10`, `12.5`, nothing) is ignored with a yellow line. 0 adds `Status LED: dark (led_brightness =
0), a crash included.` It is applied when its line is read, before the PC is built; until then
(and on every boot until the ini is read again) the default holds. On a board with no status LED
the key is accepted and the console says it is ignored. The function was cut out of `esp_main.c`
at the tag and run on the PC with stand-ins for the console and the LED: 19 values, every one as
above. `dosboard.ini` reads only `[dosboard]`, so the key is not read there [verified: `dosboard.c`
`parse_cfg()`].

The card's `tiny386.ini` and `win95-14m.ini` carry `;led_brightness = 9` under `[esp]`, commented
out, with a line on what it does; nothing else in them changed, and a card with v3.6's inis works as
before. `dosboard.ini` is unchanged. `STATUS-LED.md` 7 is the description for Plat.

### 18.4 Test 15, part 8 [verified: `checks\host-tests-v3.7.log`]

`test_status` makes 47,085 checks (v3.6: 34,460), 0 failed. The new part:

* the table: every stage, error and crash colour is the full-scale one above, each channel v3.6's
  times 255/70 rounded, and the brightest 255;
* `status_scale()` against a model worked out another way (in 64 bits, (2 c level + 255) / 510
  rounded down, then the floor) for every value of every channel at every level 0 to 255, the other two channels
  set at the same time, and never darker one level up; levels above 255 give the table;
* 0 dark, 255 the table, 70 v3.6's colours exactly;
* the default: the level at start is 9, each colour's values as listed above, each 0.10 to 0.16 of
  v3.6's light, the brightest channel 8 to 10;
* at every level 1 to 255 no lit channel put out and no dark one lit, no two colours alike; from the
  default up each colour's channel ratios within a quarter of the table's;
* `status_brightness_set()` holds `INT_MIN`, -1, 256 and `INT_MAX` to 0 and 255;
* `status_frame_grb()` at levels 9, 0, 1, 70 and 255 is the model's colour in wire order; the
  crash's red and white and the early crash's yellow and dark, bit-banged with the pretend clock
  and sent through the RMT frame, read back to the same scaled bits at all five levels (40 frames);
  at the default the crash's red goes out as 0x000900 and yellow 1 as 0x050800 (green, red, blue on
  the wire);
* given the path of `esp/main` (`run.sh` now passes it), the test reads `status_led.c` and
  `status_code.c` with their comments taken out: every `status_rmt_frame()` and `status_bb_frame()`
  call takes `status_frame_grb()` (one and two calls), `status_grb()` is called nowhere there, the
  scale, `status_frame_grb()` and the level are marked `STATUS_IRAM` and `STATUS_DRAM`,
  `status_frame_grb()` scales by `s_level`, and the scale has no 64-bit or floating-point
  arithmetic in it.

Under AddressSanitizer and UBSan: clean (`checks\asan-status-v3.7.txt`). Mutation run: 21 faults
put into copies of `status_code.c`, `status_code.h` and `status_led.c` (no floor, a floor that
lights dark channels, rounding down, a shift by 8 in place of the division, no hold above 255 in
the scale or in the setter, `status_frame_grb()` without the level or at the default only, the
scale in 64 bits, the level or the scale not marked for internal RAM, the table at v3.6's quarter,
yellow's hue changed, white's share changed, defaults of 70, 20 and 4, the RMT path, the first
crash frame or the halt loop without the brightness, the halt loop at a fixed level of its own):
all 21 caught; the unchanged files pass (`checks\status-led-mutants-v3.7.txt`, made by
`status-led-mutants-v3.7.py`).

What the test cannot see: the scaled colours on the LEDs themselves, and the real code's call
sites beyond what their text says; the built code's are in 18.2.

### 18.5 Sizes [verified: `checks\size-*-v3.7.txt`]

| | Korvo-1, v3.7 | Korvo-1, v3.6 | DOS board, v3.7 | DOS board, v3.6 |
|-|-|-|-|-|
| `tiny386.bin` | 886,144 | 885,616 | 917,808 | 917,296 |
| Flash `.text` | 425,428 | 425,204 | 422,324 | 422,100 |
| Flash `.rodata` | 230,080 | 229,824 | 261,420 | 261,164 |
| Internal RAM used by code and static data | 245,552 | 245,518 | 248,554 | 248,512 |
| Internal RAM left | 258,192 | 258,226 | 255,190 | 255,232 |
| LP SRAM | 8,244 | 8,244 | 8,244 | 8,244 |

The dual-boot `tiny386.bin` uses 886,144 of its partition's 1,966,080 bytes, 54.9 per cent spare
[verified: flash simulation].

### 18.6 Checks [verified unless marked]

| Check | Result |
|-|-|
| `build.sh` from nothing, twice (WSL `~/tiny386-idf-scratch/v37-work/rel1` and `rel2-second-run`, the patch at 012158e) | exit 0, 184 s and 265 s; all 18 files of the three image sets the same in both, ELF files included, and none of the six ELF files holds its own work folder's name; the patch applied to a fresh clone gives the `v3.7` tag's tree exactly (16ac9ffe), with every file v3.7 changed at mode 644 (`checks\reproducible-v3.7.txt`, `build-sh-v3.7-run1.txt`, `run2.txt`) |
| The bootloaders and partition tables against v3.6 | the same files byte for byte (`cmp`) in all three sets, and the same `flash_args`: the dual-boot bootloader b57469cd... and table d41cba3c..., the SHA-256 `build.sh` and `flash-dualboot.bat` pin; the standalone and DOS board bootloader ec0a396b... and table afbe6410...; their `esptool image-info` the same as v3.6's (`checks\reproducible-v3.7.txt`, `image-info-*-bootloader.bin-v3.7.txt`). Only `tiny386.bin` and `tiny386.elf` changed |
| The apps against v3.6's | `.iram0.text`, `.dram0.data`, `.flash.text`, `.flash.rodata` and `.flash.appdesc` differ in all three; on the DOS board also `.flash.tdata`, whose three addresses in internal RAM each moved 8 bytes on, as `.dram0.data` grew by 8 [verified: objdump]; six new symbols in each (`s_level`, `s_sent_level`, `status_brightness`, `status_brightness_set`, `status_frame_grb`, `status_led_brightness`), none gone (same file) |
| The images in `images\` | rel1's; each folder's `SHA256SUMS` verifies |
| `esptool image-info` | every bootloader and app valid, checksum and hash; the apps say `v3.7`; rel2's image-info files the same as rel1's (`checks\image-info-*-v3.7.txt`) |
| The brightness in the built code | as 18.2, in all three images (`checks\status-led-brightness-v3.7.txt`); `build.sh`'s bit-bang layout check passes in all three |
| Compiler warnings | Korvo-1 and its standalone build: the old `i2s.c` "`i2s_task` defined but not used" only; DOS board: none; so no new warning. `status_code.c`, `status_led.c`, `esp_main.c` and `crash.c` again with `-Wall -Wextra` in rel1's builds: none, and none in the in-tree crash-test builds; the four in-tree builds (release and `-DCRASH_TEST=1`, both boards) give the old `i2s.c` line only (`checks\warnings-wall-wextra-v3.7.txt`) |
| Host tests, scratch tree at `v3.7` | 29 passed, 0 failed (`checks\host-tests-v3.7.log`); again with an OUT folder of 129 characters, 29 of 29 (`host-tests-v3.7-long-out.log`); in the tree `build.sh` made from the patch, 26 of 26, the three runs that need the v3, v3.1 and v3.5 tags left out (`host-tests-v3.7-from-patch.log`); the host build's warnings the same 11 lines as at v3.6 |
| Test 15 | 47,085 checks, 0 failed (18.4). Under AddressSanitizer and UBSan, with the source checks: clean (`checks\asan-status-v3.7.txt`) |
| Mutation run, test 15 | 21 of 21 faults caught; the unchanged files pass (`checks\status-led-mutants-v3.7.txt`, made by `status-led-mutants-v3.7.py`) |
| The ini key | `set_led_brightness()` as cut from `esp_main.c` at the tag, on the PC: 19 values, each as 18.3 says; without the LED, accepted and said to be ignored; the key in `known_keys` and called from `parse_esp_ini()` (`checks\ini-led-brightness-v3.7.txt`, `ini-led-brightness-harness-v3.7.c`) |
| The flashing targets | as v3.6, line for line (`checks\cmake-flash-refusal-v3.7.txt`; no flash target was run) |
| The flash simulation with the v3.7 images | the dual-boot install changes 0x2000 to 0x8FFF and 0xE20000 to 0xEF8FFF only, every Linux partition unchanged; GPIO42 high starts Linux, held low tiny386 v3.7; against v3.6's run only the apps' sizes, checksums and hashes and the scratch folder differ (`checks\flash-simulation-v3.7.txt`) |
| v3.6 to v3.7 by the app alone | from the dual boot as v3.6 leaves it, the one write `flash-dualboot.bat COM10 app` makes (0xE20000, the v3.7 `tiny386.bin`) changes sectors between 0xE20000 and 0xEF8FFF only; 0x0 to 0xE1FFFF, the bootloader, the table and all of Linux, stays as it was; the result is byte for byte what the whole v3.7 set written over be5b725 gives (`checks\app-update-v3.7.txt`, made by `app-update-v3.7.py`) |
| The scripts' flow | the unchanged scripts against the v3.7 images with a stub esptool, through cmd.exe with PATH set to `C:\Windows\System32` and `C:\Windows` only: 39 cases, no write reached the stub; against v3.6's run no esptool call, decision, WOULD WRITE line or exit differs, only the version, the size, hashes, a checksum, the segment table and the dates. Case 8 is `flash-dualboot.bat COM7 app` with the dual-boot table: it shows `App version: v3.7`, and after Y makes one write, `-a hard-reset write-flash -fm keep -ff keep -fs keep 0xE20000` with `images\korvo1-dualboot\tiny386.bin`; case 9, the same answered N, writes nothing; case 12, `app` over Linux's table, stops before writing (`checks\bat-harness-v3.7.txt`, made by `checks\bat-harness-make-flow-v3.7.py`) |
| The card | `setup.c` and `ini.c` on a pretend card: 0 problems; the card files are v3.6's but `tiny386.ini` and `win95-14m.ini`, whose only change is the commented `led_brightness` lines; every `[esp]` key they name, `led_brightness` included, is in `esp_main.c`'s `known_keys`; every key in `dosboard.ini` is one `dosboard.c` knows (`checks\card-check-v3.7.txt`) |
| `SHA256SUMS` | made again for the same 41 files as v3.6's list; every line verifies, and every image folder's own; each previous folder's lines verify inside it, from `v3\` or from a later previous folder, as `README.md` says |

Not covered by any test here: how the scaled colours look on either LED, and the RMT and the
bit-bang on the chip; both are as v3.6 had them but for the bits each frame carries.

### 18.7 What only a board can say [unknown]

* Whether the default, 9, is comfortable on Plat's Korvo-1 and on the DOS board's Inolux LED, and
  which `led_brightness` suits each (`TEST-PLAN.md` 12 asks).
* Whether every colour, yellow above all (8:5 at the default), is easy to tell at that level: each
  lit channel is then 5 to 9 steps of the LED's 255, and how evenly the WS2812B and the Inolux part
  give light at so few steps was not looked up for this round.
* Everything in 17.9 still stands.

### 18.8 For Plat to decide

* The default: 9 here (red 9 of 255); 8 or 10 fit the task as well, and `led_brightness` lets any
  card choose its own.
* Whether 0 should keep the crash pattern lit after all: as built, 0 turns the LED off everywhere,
  crashes included, as the task's single scale asks.
* The name: v3.7 here, as a new tag, as v3.4 and v3.6 were.

### 18.9 Scripts and logs

WSL `~/tiny386-idf-scratch/v37-work/`: `sync.sh` (the edited sources from the session's scratch
folder `v37\src` into the tree), `t1.sh` (test 15 alone), `b1.sh` (in-tree builds, `CRASH=1` for the
crash-test ones), `q1.sh` (a first look at the in-tree ELF files with `ledcode.sh`), `mut.sh`
(`mutants/`), `asan.sh`, `commit1.sh`, `ht2.sh` (host tests), `rel.sh` (staging and the two builds
from nothing; `stage/` holds the staged `build.sh`, patch, lock and dual-boot pair), `repro.sh`,
`install.sh`, `checks1.sh` (host tests from the patch, the flash simulation in `sim/`, the refusal
query, the card check, the warnings), `ledcode.sh`, `inicheck.sh` and `inicheck/`, `appupdate.sh`,
`tdata.sh`, `hgen.sh` and `harness-gen/`, `collect.sh`, `collect2.sh`, `collect3.sh`, `sums.sh`,
`prevcheck.sh`, and `look*.sh` and `getsrc.sh`, which only read. On Windows the session's scratch
folder `v37\` holds the edited sources (`src\`), v3.6's (`orig\`), the scripts as written (`scripts\`,
with `mutants.py`, `appupdate.py`, `hcol7.py` and the small Python edits made to `README.md`,
`TEST-PLAN.md` and `FLASH.txt`) and the harness (`v37\harness`, run through its `go.cmd`). Nothing
was started that is still running.

## 19. v3.8, fix 1 of 2: Windows 95's crashes, the timer and the card (2026-10-22)

Plat ran Windows 95 OSR2 on v3.6 and on the v2 baseline and met an emulator abort, protection
errors, a VMM blue screen and a text-mode stall [reported: Plat]. Three investigations followed
(`research\WIN95-PC-REPRO.md`, `WIN95-CRASH-DECODE.md`, `WIN95-BAREMETAL-AUDIT.md`). This round
acts on them: the cause fixed at its root, the board's long waits for the card removed, the
emulator's handling of faults in delivery made the 386's, and diagnostics added for what only the
board can show. `research\WIN95-FIX.md` is the full account; this section is the build record.
The app calls itself `v3.8` (`esp/version.txt`); no tag was made, since the next agent adds two
features to v3.8 and packages it, and no image in `images\` was built or replaced. Nothing was
flashed, no serial port was opened, nothing was committed anywhere but the scratch tree, and
nothing of v3.8 has run on a board.

Plat's question, whether UTC and local times are mixed up: no. Every time involved is Perth time,
+0800: the v2 app images 13:19 on the 21st, the dual-boot bootloader 16:02, tag `v3` 18:20, his
photo of the blue screen 18:29 [verified: file times, git; the photo's time reported]. So the run
that reached the desktop and then blue-screened was the v2 baseline he flashed before he went out,
and crash report (b) was the v3.0 app flashed that evening (`WIN95-CRASH-DECODE.md` 1.3).

### 19.1 The commits [verified: `git log v3.7..6714724` in WSL `~/tiny386-idf-scratch/v3/tiny386`]

| Commit | What |
|-|-|
| f8663e0 | `i8254.c`, `i8254.h`, `misc.c`: missed timer ticks dropped after a hold, one raised on the counter's phase (`i8254_max_owed`, 1); the counters `i8254_owed_max`, `i8254_dropped`; the CMOS periodic interrupt the same (19.2) |
| c5dd715 | `i386.c`, `i386.h`: the 386's double fault and shutdown (`deliver()`), the stack put back when delivery faults (`call_isr()` around `call_isr1()`), the task gate's error code, page tables outside the guest's memory not present, LLDT's checks, `read_desc` with CS:EIP and rate-limited, `cpu_abort_note` (19.3) |
| fcae04f | `esp/main/disk_image.c`, `disk_image.h`, `ide.c`, `storage.c`, `storage.h`, `main/CMakeLists.txt`, `sdkconfig.korvo1`, `sdkconfig.dosboard`: the card's images through FatFs itself, their clusters mapped, a card error got over, one internal DMA buffer (19.4) |
| 938913b | `esp_main.c`, `console.c`, `console.h`, `speed.c`, `speed.h`: `[esp] uart_log`, `fast_seek`, `timer_catchup`; the disk and timer lines on Ctrl+Alt+F12, the UART and the crash screen (19.5) |
| 7fe503b | `tests/host`: tests 19 (the timer) and 20 (faults in delivery, `tests/host/cpu`), and `disk_image.c` in 14 (disk), 13 (storage) and 2 (console) (19.6) |
| 40a1ec6 | `esp/version.txt`: `v3.8` |
| c412ab0 | `i386.c`, `disk_image.c`: comments set right by the PC runs (Windows' vector 8 is an interrupt gate when its stack runs out, 19.3); `disk_image.c`'s line buffer static, off the emulator task's stack |
| 6714724 | `esp_main.c`: `[esp] memtest`, a PSRAM test before the PC is built (19.5) |

`git diff v3.7 6714724`: 34 files, 2,227 lines added, 74 taken out; new files `disk_image.c`,
`disk_image.h`, `tests/host/test_pit.c` and `tests/host/cpu/` (all mode 644). Unlike every round
since v3, the shared core changes behaviour for every build, the Linux route's and upstream's too:
`i8254.c`, `misc.c` and `i386.c` are not behind an ESP define, on purpose, since their bug is
upstream's [verified: `git diff`]. The Linux route's own patch and release were not changed;
`research\win95-core-v3.8.patch` (`git diff v3.7 c412ab0` of the five core files, SHA-256
9f21620c...) applies to the Linux route's tree and to upstream 9ed786b and builds in both with no
warning (19.7). `esp/dependencies.lock` was already modified in the tree before this round and is
left out of every commit.

### 19.2 The timer [verified: `i8254.c`; test 19]

`i8254_update_irq()` raises at most one IRQ 0 a `pc_step()`. Before v3.8, after the emulator had
been held up for N periods it raised N, one a step, back to back; Windows 95 took them nested
until its ring 0 stack ran off its page (`WIN95-PC-REPRO.md` 6). Now, when it raises one, it counts
the periods ended since the last one raised (the mark is a whole number of periods from the
load); more than `i8254_max_owed` (1) and the mark moves to one period before the latest boundary,
so that one is raised now, the rest are dropped and counted, and the next comes at the counter's
next wrap. 0 is the old behaviour. The CMOS periodic interrupt moves its next timeout to its
latest boundary the same way. A real 8259 holds one request, and ticks a busy processor misses are
lost [inferred from the parts' design]; the guest's clock runs slow during a hold.

### 19.3 The CPU [verified: `i386.c`; test 20]

* `deliver()` (both former `TRY1(call_isr(...))` in `cpui386_step()`): a fault while delivering
  an exception or an external interrupt is classed as the 80386 manual's table 9-3 has it.
  Contributory on contributory (#DE, #TS, #NP, #SS, #GP), or contributory or page fault on a page
  fault, gives a double fault, vector 8, error code 0; anything else is delivered after it; a
  contributory or page fault while delivering the double fault is a shutdown, which prints
  "shutdown: ... vector 8 is ..." and calls `cpu_abort(cpu, -2)`, so the board still stops on its
  crash screen. A double fault prints one line (the first 8, then every 256th).
* `call_isr()` now wraps `call_isr1()` (the old function) and, when it fails, puts back SS with
  its cached descriptor, ESP, EFLAGS and `sp_mask`: an interrupt from ring 3 or V86 mode has
  switched to the TSS's ring 0 stack by then. Its IDT reads and the V86 path's pushes fault into
  the guest rather than abort.
* A task gate's handler finds the error code on its new stack.
* `tlb_refill()`: a page directory or table entry outside `phys_mem_size`, or on the board in the
  device window 0xA0000 to 0xBFFFF, is not present (a page fault), not a load or store there.
* LLDT: the null selector or a present LDT descriptor in the GDT, else #GP(selector).
* `read_desc:` lines: CS:EIP and a count; the first 16, then every 1024th.
* `cpu_abort_note`, if set, runs first in `cpu_abort()`.

What the PC runs showed about the double fault: with the cap off and 100 ms holds, Windows'
vector 8 was an interrupt gate on the same stack when its stack ran out, so the double fault
faulted too and the run ended in a shutdown, as a 386 would reset [verified:
`~/win95-debug/fix/runs-smoke/stall100-*-df2`]. The double fault is right for any guest but does
not save this Windows; the cap does.

### 19.4 The card's disk images [verified: `disk_image.c`; tests 13 and 14; the PC runs]

* `storage_fatfs_path()` (and `drv` in `struct storage_volume`): `/sdcard3/win95.img` is
  `1:/win95.img`.
* `ide.c`, on the board, opens a hard disk or CD image on a card volume with `disk_image_open()`:
  `f_open()`, then FatFs's cluster map (`f_lseek(CREATE_LINKMAP)`) in 256 words of internal RAM,
  or, for an image in more than 127 pieces, the size FatFs asks for (internal RAM up to 4 KiB,
  then PSRAM); with no room, no map and a yellow line. Images elsewhere (`/spiflash`) and
  snapshots keep C stdio as before. `CONFIG_FATFS_USE_FASTSEEK=y` on both boards; ESP-IDF's VFS
  then also maps files opened read only, with `CONFIG_FATFS_FAST_SEEK_BUFFER_SIZE` 64.
* An access: `f_lseek()` if the place changed, `f_read()` or `f_write()` through one 4 KiB
  DMA-capable buffer in internal RAM; nothing past the image's end; never made longer.
* A refused access: the image closed, opened again, mapped again, the access tried once more; a
  second refusal is the guest's error, as before.
* At open the console gives the image's pieces on the card and how long the map took; more than 64
  pieces is yellow.
* `disk_image_get_stats()`: reads, writes, errors, errors got over, the longest access and the
  accesses over 55 ms.

### 19.5 The firmware [verified: `esp_main.c`, `console.c`, `speed.c`; the memtest harness]

| `[esp]` key | Default | What |
|-|-|-|
| `uart_log` | 0 | 1: the log on the UART too while the PC runs; 0: the ring and the panel only (Ctrl+Alt+F12). ESP-IDF's crash report and a fatal message still go to the UART, and an emulator abort turns it back on first |
| `fast_seek` | 1 | 0: the disk images without their map, as before v3.8 |
| `timer_catchup` | 1 | `i8254_max_owed`; 0 makes up every missed tick, as before v3.8, and says so |
| `memtest` | 0 | N (up to 10) passes of a PSRAM test over the emulator's pool before the PC is built, with the panel's DMA running; a wrong word in red with its address and bits; the pool zeroed after; about a second a pass |

Each is a whole number; anything that is not one, or is out of range, keeps the default with a
yellow line (`led_brightness` instead holds an out-of-range number to the nearer end). The
memtest's second a pass is an estimate from the pool's size [inferred]. Ctrl+Alt+F12 prints, under "Speed:", `Disk: R reads, W writes, longest L ms, N over 55 ms;
card errors E (G got over), F refused` and `Timer: at most M ticks owed at once, D dropped
(timer_catchup = 1)`; the same two lines go to the UART every 10 s with `uart_log = 1`, and, after
"tiny386 v3.8 stopped the PC.", first thing in an emulator abort, so that the crash screen's last
lines show them and the version. The boot console's "The PC is starting" line says where the log
goes. `psram_test()` and `ini_whole()`, cut out of `esp_main.c` and run on the PC: a good pool
passes and is zeroed, a bit that flips during the read-back is found, nine ini values give what
the table says [verified: `~/win95-debug/fix/memtest.sh`].

For the packaging agent: the card's `tiny386.ini` and `win95-14m.ini` could carry, under `[esp]`,
commented lines like `led_brightness`'s:

    ; the log on the UART too while the PC runs; 0 without this line (Ctrl+Alt+F12 shows it)
    ;uart_log = 0
    ; timer ticks missed while the emulator waits: 1 dropped as a real PC drops them; 0 every
    ; one made up afterwards, as before v3.8 (Windows 95 can crash on that)
    ;timer_catchup = 1
    ; the disk images' clusters mapped at start (1); 0 as before v3.8
    ;fast_seek = 1
    ; passes of a PSRAM test before the PC starts, about a second each; 0 none
    ;memtest = 0

### 19.6 Host tests [verified: `~/win95-debug/fix/hostout/all.log`]

`tests/host/run.sh`: 32 passed, 0 failed (v3.7: 29); the host build's 11 warnings are v3.7's same
lines. New: 19, the timer (16 checks, and the pristine `i8254.c` run, which has to show the burst
and does); 20, faults in delivery (40 checks with the Korvo-1's header and 40 without, and v3.7's
`i386.c`, which stops at the first double fault as the board did: "@ ... cpui386_step 5142, abort:
-1"). Changed: 14 (disk, 13 results, 47 checks a volume through `disk_image.c`: a 300-piece image
read out of order with 0 FAT reads and one card read a sector, against 1,553 FAT reads unmapped;
a refused read and a refused write got over; a card refusing everything, then answering again; and
through stdio the old sticky error seen); 13 (storage: `storage_fatfs_path()`; the stub
`sdkconfig.h` has the v3.8 Korvo-1 build's FatFs lines exactly); 2 (console: the UART quiet).
Under AddressSanitizer and UBSan, with address-space randomisation off (gcc 12's ASan loops at
start on this WSL kernel otherwise): tests 19 and 20 (four runs) and 14's four cases pass, and
UBSan's only report is tiny386's own `CR0_PG (1<<31)` shift, which v3.7 has the same [verified:
`~/win95-debug/fix/hostout/asan/*3*.log`].

### 19.7 The PC harness [verified: WSL `~/win95-debug/fix`; `research\WIN95-FIX.md` 4]

Investigation 1's harness, copied to `~/win95-debug/fix/harness`, with `-pit-owed N`,
`-img-nofast` and `-img-stdio` added and the tree's `disk_image.c` built in with ESP-IDF's own
`ff.c`, so that a card model runs the board's very disk path. Windows 95 OSR2 from a fresh copy
of the pristine image every run, three timings each:

| Builds and models | Runs | At the desktop |
|-|-|-|
| v3.8 (`40a1ec6`), every model that failed in the investigation: holds of 100 and 500 ms on every access at 4M and 14M; the card with the image in one piece, in 64 KiB pieces in order and in random order, and in 4 KiB pieces in random order, at 100 us to 1 ms a read, at 14M, 13M, 8M and 4M; every 5th write 250 ms late; ide.c's old stdio path with glibc's buffer; and no hold | 51 | 51 |
| the cap alone (no map), the card models | 27 | 27 (slowly: up to 27,000 virtual s to the desktop, single accesses up to 89.5 s) |
| the map alone (no cap), the card models | 27 | 24; the slow-write card aborts x3, with the photo's four return addresses on the stack (16 of its 32 stack bytes; this said "the photo's stack bytes" until v3.9, 21.11) and now "shutdown: ... vector 8 is an interrupt gate" |
| neither (only the CPU changes) | 51 | 6 (the no-hold runs); every failure of the investigation back, the 500 ms runs ending as v3.6's did to the instruction |
| `research\win95-core-v3.8.patch` on the Linux route's tree and on upstream 9ed786b: no hold, 100 ms holds at 4M and 14M | 18 | 18 |
| v3.8 built 32-bit with unsigned `char`: no hold, 100 ms holds at 4M, and three card models | 15 | 15 |
| the last commit (`6714724`): repro.sh's `fast`, `board-frag` and `stall100` | 9 | 9, the same times as `40a1ec6` |

Every run that reached the desktop ran on 120 virtual seconds after it without a fault. (Corrected
in v3.9, 21.11: this said "into first-run setup", which holds for 30 of the 51 runs of the first
row, the `fast`, `frag16`, `rand16` and `rand1` ones; in the 21 `hold100`, `hold500`, `bufd` and
`spike5` runs setup's window had not come within the 120 s.) `research\WIN95-FIX.md` 4 has the
tables and `research\WIN95-FIX-runs.txt` every run's outcome, timer and card figures.

### 19.8 Sizes [verified: `~/win95-debug/fix/size-v38fix-*.txt`; v3.7's from `checks\size-*-v3.7.txt`]

Built in the scratch tree (`esp/build-v38fix-korvo1`, `esp/build-v38fix-dosboard`) for the compile
check only; not the images to ship, which `build.sh` makes from the patch after the next agent's
round.

| | Korvo-1, v3.8 here | Korvo-1, v3.7 | DOS board, v3.8 here | DOS board, v3.7 |
|-|-|-|-|-|
| `tiny386.bin` | 896,096 | 886,144 | 927,792 | 917,808 |
| Flash `.text` | 431,762 | 425,428 | 428,674 | 422,324 |
| Flash `.rodata` | 232,848 | 230,080 | 264,188 | 261,420 |
| Internal RAM used by code and static data | 249,716 | 245,552 | 252,730 | 248,554 |
| Internal RAM left | 254,028 | 258,192 | 251,014 | 255,190 |
| LP SRAM | 8,244 | 8,244 | 8,244 | 8,244 |

Of the 4,164 more bytes of internal RAM, 2,880 are `disk_image.c`'s six image slots (`s_img`: a
FatFs file object and two paths each) and 928 `deliver()`, which the build keeps in internal RAM
with `call_isr()` inlined into it [verified: `riscv32-esp-elf-nm` on the Korvo-1 ELF]; at run time
`disk_image.c` also takes its 4 KiB buffer and a map of up to 4 KiB from the heap [inferred from
the source]. Compiler warnings: the Korvo-1's first full build in this round gave the old `i2s.c`
line only, and the DOS board's none; the main component compiles with `-Wall -Werror` (unused
functions and a few others excepted), so most new warnings would have stopped the build
[verified: the build output; `build-v38fix-dosboard.log`,
the Korvo-1's log since overwritten by incremental builds]. The dual-boot `tiny386.bin` would use
896,096 of its partition's 1,966,080 bytes, 54.4 per cent spare [inferred: these builds].

### 19.9 What only a board can say [unknown]

* Whether Windows 95 now reaches the desktop at 14M/320K, 13M/1M and 4M/256K from a fresh copy.
* The pieces Plat's copy is in, and the `Disk:` and `Timer:` figures at the desktop; what the
  PSRAM test says (`memtest = 3`, once).
* The SD card's real times per read and write, and whether its write stalls are long.
* Whether `timer_catchup = 0` brings the failures back on the same card (the comparison that would
  tie them to the timer on the board).
* Everything in 18.7 still stands.

### 19.10 For Plat to decide

* Whether `uart_log = 0` is the right default (he asked for it; anyone reading the UART must now
  set it).
* Whether a shutdown (a fault while delivering the double fault) should reset the PC, as a real
  one would, instead of stopping on the crash screen as v3.7 did: kept as a stop here, so the
  evidence stays on the panel.
* Whether to send `research\win95-core-v3.8.patch` upstream (hchunhui/tiny386 has the same PIT
  and no double fault), in his name.

### 19.11 For the next agent (fix 2 of 2: two features, then packaging)

* The tree is at `6714724` on `integ`, app version `v3.8`, no tag; `esp/dependencies.lock` modified
  from before and not committed (as in earlier rounds).
* The card inis (19.5), `README.md`, `TEST-PLAN.md` (a Windows 95 step: a fresh copy onto a freshly
  formatted WIN9X partition, 14M/320K, the boot console's pieces line, Ctrl+Alt+F12 at the desktop,
  one boot with `memtest = 3`) and `FLASH.txt` need v3.8's words; `STATUS-LED.md` is unchanged (no
  new LED code).
* After replacing `images\korvo1-dualboot\tiny386.bin`, copy it to
  `images\korvo1-dualboot-tiny386-default\` and remake that folder's `SHA256SUMS` (the boot-swap
  task's note).
* `checks\` will want this round's logs: the host tests, the memtest harness, the PC runs' summary
  (`~/win95-debug/fix/runs-*/summary.txt`).

### 19.12 Scripts and logs

WSL `~/win95-debug/fix/`: `s1.sh` to `s60.sh` (looks and edits), `b1.sh`, `b2.sh` (the board
builds and sizes), `t_*.sh` (host tests), `asan*.sh`, `memtest.sh` with `memtest_harness.c`,
`commit1.sh` to `commit3.sh`, `mklists.sh`, `mkother.sh`, `m1.sh`, `mrest.sh`, `summ.sh`, `st*.sh`,
`wait*.sh`, `patch_headless.py`, `patch_harness2.py`; `harness/` (investigation 1's harness with
this round's additions), `trees/`, `bin/`, `lists/`, `runs-*` (each with `summary.txt`),
`hostout/`, `core-v3.8*.patch`. Nothing was started that is still running.

## 20. v3.8, fix 2 of 2: the disk light, quieter logging, and the package (2026-10-22)

Two things Plat asked for, then v3.8 built and packaged. The status LED becomes a PC's front panel
while the PC runs: a steady green, no dark blink every 2 s any more, and a short blue flash each
time the guest reads or writes a disk, like a hard disk light [reported: Plat, 2026-10-22]. And the
log gets quieter: Plat does not use the UART, and asked whether logging and the Ctrl+Alt+F12 overlay
cost the emulator speed. Fix 1 had already made the UART quiet while the PC runs (19.5); this round
holds back the lines the emulator repeats, makes the UART, when it is asked for, a queue that no
task waits for, and measures the cost. Then the images: four sets, built from nothing twice, the
boot swap's tiny386-default set now made by `build.sh` itself. The app calls itself `v3.8`, tag
`v3.8` (3bded9d) in the scratch tree; v3.7's images, patch and notes are kept in `previous-v3.7\`.
Nothing was flashed, no serial port was opened, nothing was committed anywhere but the scratch tree,
and nothing of v3.8 has run on a board.

Plat's question again, whether UTC and local times are mixed up: no (19, and the boot swap's own
check, `dualboot\DUALBOOT.md` 13). Every time in these notes is Perth time, +0800; this round's
builds and commits are stamped the same way [verified: `git log`, the build logs].

### 20.1 The commits [verified: `git log 6714724..v3.8` in WSL `~/tiny386-idf-scratch/v3/tiny386`]

| Commit | What |
|-|-|
| 5c47263 | `ide.c`, `ide.h`, `misc.c`, `misc.h`: `ide_activity` (BUILD_ESP32) moves on every read and write the guest asks of a disk or CD image in `bf_read_async()` and `bf_write_async()`; `emulink_activity` (EMULINK_CHECKED) on every floppy read or write that reaches the image (20.2) |
| 77e62c7 | `status_code.c`, `status_code.h`, `status_led.c`, `status_led.h`, `esp_main.c`: the PC running a steady green, the disk light, `STATUS_TICK_MS` 20 (20.2) |
| c810d92 | `console.c`, `console.h`, `speed.c`, `esp_main.c`: lines that repeat held back, the UART queue, `[esp] verbose_log`, the `Log:` line on Ctrl+Alt+F12 (20.3, 20.4) |
| 3bded9d | `tests/host`: test 15 part 9, test 2 parts 7 and 8, the counts in test 14 and `test_floppy` (20.6) |

`git diff 6714724 v3.8`: 18 files, 1,009 lines added, 58 taken out; `git diff v3.7 v3.8` (both
halves): 42 files, 3,216 added, 112 taken out, every changed file at mode 644. Outside `esp/` and
`tests/` this half changed only `ide.c`, `ide.h`, `misc.c` and `misc.h`, 23 lines, all behind
`BUILD_ESP32` or `EMULINK_CHECKED`: compiled as the Linux route and upstream compile them, `ide.c` and
`misc.c` give the same code, data and symbols at 6714724 and at v3.8 [verified:
`checks\linux-route-objects-v3.8.txt`], so fix 1's `research\win95-core-v3.8.patch` still stands as it
is. The patch is `git diff pristine v3.8`: 141 files, 25,910 added, 708 taken out, 1,019,448 bytes,
SHA-256 650e90a7... (`tiny386-idf-v3.diffstat.txt`). `esp/dependencies.lock` was modified in the tree
before this round and is left out of every commit, as before; the scratch tree's commits carry no
co-author line, as its earlier ones.

### 20.2 The disk light [verified: `status_code.c`, `status_led.c`, `ide.c`, `misc.c`, `esp_main.c`; test 15]

* `status_pattern()` gives the PC running as GREEN, or BLUE (the screen stage's colour) when
  `status_state.disk` is set; v3.7's `BEAT_PERIOD_MS`, `BEAT_DARK_MS` and the dark 0.1 s every 2 s
  are gone. An error, the replay after a reset and the crash pattern come first, as before; in any
  other stage the light is not shown.
* The LED task looks every `STATUS_TICK_MS`, now 20 ms (was 50): two FreeRTOS ticks at both boards'
  100 Hz, `li a1,2` before `xTaskDelayUntil` in the built `led_task` [verified:
  `checks\status-led-v3.8.txt`]. At each look, while the stage is "PC running", it calls the count
  function `esp_main.c` gave it (`status_led_disk_counter(disk_activity)`, just before the stage
  becomes running) and `status_disk_step()` compares the result with the last look's; if it moved,
  that look's copy of the state has `disk` set. So one access lights one look, 20 ms; accesses less
  than a look apart keep it lit; accesses further apart flicker.
* `disk_activity()` is `ide_activity + emulink_activity`, two words the emulator's core bumps with
  one add and one store per access (`BF_ACTIVITY()`, `FDD_ACTIVITY()`) and the LED task reads from
  core 0. Each of `bf_read_async()`, `bf_write_async()`, `emulink_data_read_string()` and
  `emulink_data_write_string()` refers to its count in all three images [verified:
  `checks\status-led-v3.8.txt`]. A guest access past an image's end counts too (the guest asked);
  a floppy access refused before the image is reached does not.
* The stall check is untouched: `status_watch_step()` still watches the emulator's beat
  (`run_pc()` once a step), so a stopped emulator shows a steady green, then yellow 5 after 10 s, and
  makes no disk accesses meanwhile. The dark blink never fed it.
* The brightness applies to the blue as to every colour (at the default, blue 0, 0, 8 against green
  0, 6, 0). Frames: one at each change of colour, as before; each is about 0.33 ms of the RMT
  peripheral's time.

`STATUS-LED.md` 8 is the description for Plat; its section 1 table, its yellow 5 row and section 5
(the look's period) changed with it.

### 20.3 Lines that repeat [verified: `console.c`; test 2]

After `console_log_limit(true)`, which `i386_task` calls as the PC starts unless `[esp]
verbose_log = 1`, every line that comes through the tee (printf, puts, fprintf(stderr): the
emulator's own debug lines and the guest BIOS's port 0x402 text) or through ESP_LOGx (the drivers')
goes through `lim_pass()`. A line's kind is FNV-1a over its text with every word that holds a digit
taken as `#` ("inw 0xa20 <= 0xffff" and "inw 0x3f <= 0xffff" are one kind, "set VGA mode 12" and
"set VGA mode 3" another, "atapi_cmd_check_status" its own). Of each kind the first 5 in a period of
5 s from its first line go out as before; the rest are counted, and the last of them kept (99
characters). When the period is over, at the kind's next line or at the speed screen's poll on core
0 (every frame, `console_log_tick()`, cheap while nothing is held), one line says it: `(not shown:
45 more lines like "inw 0x31 <= 0xffff" in 5 s)`. 16 kinds at a time, in PSRAM (1,920 bytes, taken
when the limit is first turned on); a new kind takes a free place or the one least lately used,
whose count is said first. `console_printf()`, `console_puts()` and the firmware's own lines are never
held back; nor is anything before the PC starts. `console_log_limit(false)` says every count held
and lets every line through again: an emulator abort does that first (20.4), so that its dump is
whole. (Since v3.9 the line that names the abort's cause is printed after that too, so it reaches the
UART as well, and a crash of any other kind says the counts still held on the crash screen, 21.3
and 21.4.)

### 20.4 The UART queue and the new key [verified: `console.c`, `esp_main.c`, `speed.c`; test 2]

* With `[esp] uart_log = 1`, as the PC starts, `console_uart_async(true)`: lines for the UART go into
  a 4 KiB queue in PSRAM; the `uart_log` task (core 0, priority 1, 3 KiB of stack) writes into the
  console UART's FIFO as much as it has room for, with ESP-IDF's own low-level calls, each newline as
  CR LF as ESP-IDF's console sends it, then sleeps a tick (the FIFO's 128 bytes last 11 ms at 115200).
  Putting a line in and taking bytes out are each a few microseconds under a spinlock of the queue's
  own; nothing that logs ever waits for the UART. A line that does not fit is dropped from the UART
  (the ring and the panel still have it) and counted, and once the queue is empty the task puts in
  `(N lines dropped here: the UART could not keep up; the ring and the panel have them)`. Without
  `uart_log = 1` the UART stays quiet while the PC runs, as fix 1 made it, and no queue or task is
  made.
* An emulator abort (`abort_note()`, first in `cpu_abort()`): the UART on, the counts said, the
  queue written out there and then (`console_uart_async(false)`, waiting on the FIFO, which is fine
  for a core that is stopping), and lines straight to the UART from then on, so that the whole dump
  is on the UART before the panic handler's report.
* `[esp] verbose_log`: 0 (the default) holds back what repeats; 1 shows every line while the PC
  runs, as before v3.8. A whole number 0 or 1 through `ini_whole()`, as `uart_log`; anything else
  keeps the default with a yellow line [verified: `checks\memtest-ini-v3.8.txt`, fix 1's harness
  run again at the tag].
* The boot console's "The PC is starting" line is now up to three: the hand-over, where the log
  goes, and, unless `verbose_log = 1`, `A line the emulator repeats is shown 5 times in 5 s, then
  counted ([esp] verbose_log = 1 shows every one).` Ctrl+Alt+F12 adds `Log: H repeated lines held back, D dropped from the UART's queue`
  under the `Disk:` and `Timer:` lines.
* The card's `tiny386.ini` and `win95-14m.ini` carry v3.8's five keys commented out, each at what it
  is without its line, with a line or two on what it does (fix 1's text for four, 19.5); a card with
  older inis works as before [verified: `checks\card-check-v3.8.txt`: 0 problems, every `[esp]` key
  they name is in `known_keys`].

### 20.5 The cost, before and after [verified: `checks\log-cost-v3.8.txt` unless marked]

What a logged line costs the emulator's core, and what reaches the UART, measured with `console.c`
built for the PC (`log-cost-v3.8.c`) on two inputs: the emulator's own lines from a Windows 95 OSR2
boot to the desktop in fix 1's harness (180 lines, 4,995 bytes on the UART with CR LF, over 80 s),
and a storm, an unknown port and a bad selector by turns, 100,000 lines in 60 s.

| | Lines out, boot | Bytes to the UART, boot | Lines out, storm | Bytes to the UART, storm |
|-|-|-|-|-|
| every line (v3.7, and v3.8 with `verbose_log = 1`) | 180 | 4,995 | 100,000 | 4,538,894 |
| the limit (v3.8's default) | 107 (75 held back, 2 count lines) | 3,690 | 144 (99,880 held back, 24 count lines) | 7,554 |

* **The UART.** 86.8 us a byte at 115200 baud, on the core that logs [verified: the audit, F10]. In
  v3.7 that was the emulator's core: the boot's lines held it 0.43 s in all, the storm 394 s, about
  6.6 times the storm's own length, so in practice the storm would have run at the UART's pace.
  Since fix 1 the UART is off while the PC runs by default: 0 s. With `uart_log = 1` now the
  emulator's core only copies the line into the queue [inferred: a copy of up to 256 bytes under a
  spinlock, a few microseconds]; the UART's time, 0.32 s for the boot and 0.66 s for the storm with
  the limit, is spent by the `uart_log` task on core 0, sleeping between ticks.
* **The console's code.** On this PC 70 to 160 ns a line, the held-back ones the cheaper (they skip
  the ring and the panel's text). On the board [inferred: the built code, `lim_pass.part.0` in the
  Korvo-1 ELF]: the kind is about 11 instructions a character, finding the kind's place at most 16
  steps of about 6, the clock one 64-bit division, and a held-back line's copy about 7 instructions a
  character: some 1,000 instructions for a 40-character line, 3 to 5 us at 320 MHz; a line that goes
  out also goes to the ring and the panel's text as before.
* **What is left.** The tee still takes and gives the console's recursive mutex for every character
  it is handed, as v3.7 does: with nothing contending, an estimated 1 to 1.5 us a character
  [inferred: FreeRTOS's mutex path, not measured], about 40 to 60 us for a 40-character line,
  whether it is shown or held back. That is now the largest cost of a logged line; at a normal
  boot's 180 lines it comes to about 10 ms in all [inferred]. A storm at the rate above would cost
  the emulator about 7 per cent [inferred]; the read_desc storm Plat saw on v3.6 is also held at its
  source since fix 1 (16 lines, then every 1024th, 19.3).
* **The overlay.** With Ctrl+Alt+F12 up, each line shown redraws the console on the core that
  logged it, an estimated 5 to 10 ms [inferred: the audit, F10]; with the limit, at most 5 lines of a
  kind every 5 s are shown. With the overlay down, the panel's text is updated in memory only.
  Without lines being logged, the overlay costs the emulator nothing [inferred: the audit].

### 20.6 Host tests [verified: `checks\host-tests-v3.8.log`]

32 passed, 0 failed, at the tag; again with an output folder of 117 characters, 32 of 32; in the tree
`build.sh` made from the patch, 29 of 29 (the three runs that need the v3, v3.1 and v3.5 tags left
out) [verified: `host-tests-v3.8-long-out.log`, `host-tests-v3.8-from-patch.log`]. The host
build's warnings are v3.7's same 11 lines.

* Test 15 (`test_status`, now 48,907 checks): the PC running a steady green over 10 s and blue with
  the light; part 9, the disk light: the look's period 20 to 30 ms in whole ticks;
  `status_disk_step()` at a move, no move and a wrap; the LED task's looks, as `status_led.c` makes
  them, against pretend accesses: none (green all along), one at 1003 ms (one blue look, the next),
  a burst every 5 ms for 300 ms (15 or 16 blue looks in a row), 20 accesses 50 ms apart (20 single
  blue looks), the emulator stopped at 4 s (yellow 5 from 14 s, no blue), the light under yellow 3,
  in the BIOS stage and in the replay (not shown); and, given `esp/main`, the sources: `led_task()`
  steps the beat watch and the disk light before the colour, `bf_read_async()`,
  `bf_write_async()` and the floppy's read and write each have one count, each a store, and
  `esp_main.c` gives the count before the PC runs.
* Test 2 (`test_console`): part 7, lines that repeat: every line with the limit off; 5 of 50 of a
  kind, the count at 5 s and not at 4.999 s, said once; a new period; four kinds side by side (a
  line with no number among them); `console_printf()` never held back; ESP_LOGx lines held back; 20
  kinds of 7 lines in 16 places (4 counts said as places went, all 20 by the end); the limit turned
  off with a count held; the total. Part 8, the UART queue: a line waiting, the FIFO taking 4, then
  5 (the line's end needing 2), then the CR LF; 100 lines of 60 bytes with no room, 68 kept and 32
  dropped (the ring has all 100), written out 128 bytes at a time with the dropped count after
  them; the queue written out when turned off, and lines straight again, in order.
* Test 14 (the disk, through ESP-IDF's FatFs) and `test_floppy`: the counts move once for each
  access (four for a read, a write, a read of two sectors and a read past the end; two for a floppy
  read and write, none for one refused before the image).

Mutation run: 25 faults put one at a time into copies of the files (the disk light ignored, v3.7's
blink back, the light in another colour, the light over an error, a 50 ms look, every look lit, the
count never kept, `led_task()` without the step, either IDE count left out or moved after the range
check, the floppy read uncounted, the count given after the PC runs; 6 of a kind, a 4 s period,
numbers kept in the kind, `console_printf()` held back, a count not reset, a place taken without
its count said, a tick that never says, counts not said when the limit goes off, the queue sending
LF only, overfilling the FIFO, dropped lines not counted, the queue not written out, the UART written
straight while queued): each caught by every test named for it; the unchanged files pass, the disk
test among them for real, not skipped [verified: `checks\mutants-v3.8.txt`, made by
`mutants-v3.8.py`]. Under AddressSanitizer and UBSan, with address-space randomisation off: tests 2
and 15, `test_floppy` and the logging benchmark run clean; the only leaks are the tests' own
(`console_init()` called twice, `emulink_init()`, the benchmark's passes), the same bytes as v3.7's
tests [verified: `checks\asan-v3.8.txt`].

What the tests cannot see: the LED's flashes on either LED, the queue's writes to the real FIFO, and
the cost on the board's own cores; 20.5 has the estimates.

### 20.7 `build.sh` and the four image sets [verified: `checks\reproducible-v3.8.txt`]

`build.sh` now also makes `images\korvo1-dualboot-tiny386-default\`: the bootloader, `tiny386.bin`
and `flash_args` of the korvo1-dualboot set it has just built and checked, and
`dualboot\partition-table-tiny386-default.bin`, which it checks by SHA-256 (5b90d416..., as
`flash-dualboot.bat` pins it) before anything is cloned; that folder's `SHA256SUMS` lists the four
files, no ELF, as the boot swap made it. So a rebuilt app can no longer leave a stale copy there (the
boot swap's note, `dualboot\DUALBOOT.md` 13.6). Its first comment and the closing listing changed
with it; nothing else.

Two runs from nothing (WSL `~/win95-debug/feat/rel1` and `rel2-second-run`, the patch at tag v3.8):
exit 0, 308 s and 302 s; all 22 files of the four sets the same in both, ELF files included, and no
ELF holds its work folder's name; the patch applied to a fresh clone gives the v3.8 tag's tree
exactly (c9db351d), every file v3.8 changed at mode 644. The bootloaders, the partition tables and
the `flash_args` of the three sets v3.7 had are v3.7's byte for byte; in the tiny386-default set, the
bootloader, table and `flash_args` are those the boot swap made by hand after v3.7, and `tiny386.bin`
is the korvo1-dualboot one.

### 20.8 Sizes [verified: `checks\size-*-v3.8.txt`; fix 1's compile check from 19.8]

| | Korvo-1 v3.8 | fix 1 only | v3.7 | DOS board v3.8 | fix 1 only | v3.7 |
|-|-|-|-|-|-|-|
| `tiny386.bin` | 899,024 | 896,096 | 886,144 | 930,720 | 927,792 | 917,808 |
| Flash `.text` | 434,366 | 431,762 | 425,428 | 431,286 | 428,674 | 422,324 |
| Flash `.rodata` | 233,160 | 232,848 | 230,080 | 264,500 | 264,188 | 261,420 |
| Internal RAM used by code and static data | 249,788 | 249,716 | 245,552 | 252,786 | 252,730 | 248,554 |
| Internal RAM left | 253,956 | 254,028 | 258,192 | 250,958 | 251,014 | 255,190 |
| LP SRAM | 8,244 | 8,244 | 8,244 | 8,244 | 8,244 | 8,244 |

This half adds 72 bytes of internal RAM on the Korvo-1 (the counts, the queue's lock and words);
the limit's places and the queue are in PSRAM, made only when used. The dual-boot `tiny386.bin` uses
899,024 of its partition's 1,966,080 bytes, 54.3 per cent spare [verified: flash simulation].

### 20.9 Checks [verified unless marked]

| Check | Result |
|-|-|
| `build.sh` from nothing, twice | as 20.7 (`checks\reproducible-v3.8.txt`, `build-sh-v3.8-run1.txt`, `run2.txt`) |
| `esptool image-info` | every bootloader and app valid, checksum and hash; the apps say `v3.8`; rel2's the same as rel1's; the bootloaders' the same as v3.7's (`checks\image-info-*-v3.8.txt`) |
| The apps against v3.7's | `.iram0.text`, `.dram0.data`, `.flash.text`, `.flash.rodata`, `.flash.appdesc` and `.flash.tdata` differ in all three; 66 new symbols and 6 gone, the same in each, both halves' (`checks\reproducible-v3.8.txt`) |
| The status LED in the built code | the panic path all in internal RAM, `s_level` 09 00 00 00, every frame through `status_frame_grb()`, as v3.7; the look's 2 ticks; the four counts (`checks\status-led-v3.8.txt`); `build.sh`'s bit-bang layout check passes in all three |
| Compiler warnings | `build.sh`'s logs and the four in-tree builds (release and `-DCRASH_TEST=1`, both boards): the old `i2s.c` line only, so no new warning. Every file v3.8 changed (both halves) and `crash.c` and `display.c`, again with `-Wall -Wextra`, beside the same file at v3.7: none in the firmware's files; `i386.c` 34 and 34, tiny386's own `%x` for a `uword` (`checks\warnings-wall-wextra-v3.8.txt`, made by `warnings-wall-wextra-v3.8.py`) |
| Host tests | 32 of 32; 32 with a long folder; 29 of 29 from the patch (20.6) |
| Mutation run, sanitizers | 25 of 25 caught; clean (20.6) |
| The Linux route | fix 2's shared files compile to the same objects without the ESP defines (20.1) |
| The flashing targets | as v3.7, line for line (`checks\cmake-flash-refusal-v3.8.txt`; no flash target was run) |
| The flash simulation, the dual-boot work's model | the install changes 0x2000 to 0x8FFF and 0xE20000 to 0xEFBFFF only, every Linux partition unchanged; GPIO42 high starts Linux, held low tiny386 v3.8; against v3.7's run only the apps' sizes, checksums, hashes and erased ranges and the scratch folder differ (`checks\flash-simulation-v3.8.txt`) |
| The flash simulation with tiny386 the default | the boot swap's 19 paths and 5 cut-off cases with the v3.8 images: against its v3.7 run the same differences only (`checks\flash-simulation-bootswap-v3.8.txt`) |
| v3.7 to v3.8 by the app alone | from the dual boot as v3.7 leaves it, the one write `flash-dualboot.bat COM10 app` makes changes sectors between 0xE20000 and 0xEFBFFF only; 0x0 to 0xE1FFFF stays as it was; the result is byte for byte what the whole v3.8 set written over be5b725 gives (`checks\app-update-v3.8.txt`) |
| The scripts' flow | the boot swap's harness (`checks\bootswap\scripts\make_flow_bootswap.py`, unchanged) with the v3.8 images, through cmd.exe with PATH set to `C:\Windows\System32` and `C:\Windows` only: 69 cases, no write reached the stub; against its v3.7 run no esptool call, WOULD WRITE line, exit or stop differs, only the version, sizes, hashes, the segment table and the dates. Case 8 (`app`) shows `App version: v3.8`; case 55 (`app` with tiny386 the default) keeps the default (`checks\bat-harness-v3.8.txt`) |
| The card | `setup.c` and `ini.c` on a pretend card: 0 problems; the two inis' only change is the commented v3.8 lines; every `[esp]` key they name is known; `dosboard.ini`'s keys all known (`checks\card-check-v3.8.txt`) |
| `SHA256SUMS` | made again: v3.7's 41 files and the four of the tiny386-default set, 45; every line verifies, and every image folder's own; each previous folder's lines verify inside it, from `v3\`, from a later previous folder or from `checks\bootswap\scripts-before\`, as `README.md` says |

Fix 1's own logs, as 19.11 asked: `checks\host-tests-v3.8-fix1.log` (its host tests) and
`checks\win95-pc-runs-v3.8.txt` (each set of PC runs' summary); `research\WIN95-FIX-runs.txt` has
every run.

### 20.10 What only a board can say [unknown]

* Whether the blue flashes are easy to see at the default brightness, and whether the light
  flickers as a hard disk light does while Windows or DOS loads.
* Whether `uart_log = 1` keeps up with a normal boot (it should: the boot's 3,690 bytes over 80 s
  are a hundredth of the UART's rate [inferred]), and what the queue's dropped count is in a storm.
* The tee's per-character cost on the board (20.5), and BENCH with v3.8 (not measured; the extra
  work while nothing is logged is the two counts' store per disk access [inferred]).
* Everything in 19.9, Windows 95 above all, and 18.7.

### 20.11 For Plat to decide

* The disk light's colour (blue, the screen stage's) and its look of 20 ms: 30 ms would make each
  flash longer and the flicker coarser.
* The limit: 5 lines of a kind in 5 s, 16 kinds at a time; `verbose_log = 1` turns it off.
* Whether the tee should stop taking the console's lock for every character (20.5): a line buffer
  per task would leave one lock a line, a change to `console.c` alone, not made here.
* Fix 1's points in 19.10 still stand: whether `uart_log = 0` is the right default, a shutdown's stop,
  and sending `research\win95-core-v3.8.patch` upstream in his name.

### 20.12 Scripts and logs

WSL `~/win95-debug/feat/`: `sync.sh` (the edited sources from the session's scratch folder
`feat\src` into the tree), `t1.sh` (tests 2, 15, 14 and the floppy), `b1.sh` (in-tree builds,
`CRASH=1` for the crash-test ones, `build-v38*.log`), `ht1.sh`, `ht2.sh` (host tests), `mut/`
(`mutants-v3.8.py`), `asan.sh`, `asan2.sh`, `bench/` and `bench.sh` (the logging benchmark),
`commit1.sh`, `rel.sh` (the tag, the staging in `stage/`, the two builds), `repro.sh`, `prev.sh`
(`previous-v3.7\`), `install.sh`, `checks1.sh` (host tests from the patch, the two flash simulations
in `sim/` and `simb/`, the refusal query, the card check), `warn.sh` and `warn38.py`, `hgen.sh`,
`hcol8.sh` and `hcol8.py` (the scripts' harness), `appupdate.sh`, `ledcode.sh`, `memtest.sh`,
`linuxroute.sh`, `collect.sh`, `collect2.sh`, `sums.sh`, and `a*.sh`, `dis*.sh`, `look1.sh`,
`sizes.sh`, which only read. On Windows the session's scratch folder `feat\` holds the edited sources
(`src\`), fix 1's (`orig\`), the new section 8 of `TEST-PLAN.md`, the staged `build.sh` and the
harness (`feat\harness`, run through its `go.cmd`). Nothing was started that is still running.

## 21. v3.9: the two reviews of v3.8 (2026-10-22 and 23)

Two independent reviews of tag `v3.8`, each with its own lens: `REVIEW-v38-emulator-and-windows.md`
(the emulator, Windows 95, the PC harness, the Linux route) and
`REVIEW-v38-board-safety-and-features.md` (the flash scripts and the tiny386-default table, the disk
light, the limit on repeated lines and the UART queue, the package). Neither raised a blocker; the
one major finding was the scripts' handling of a first install cut short, old since v3.2. Every
finding and what became of it is in `REVIEW-RESOLUTION.md` round 5; this section is the build
record. The app calls itself `v3.9`, tag `v3.9` (d370a7a) in the scratch tree; v3.8's images,
patch, scripts and notes are kept in `previous-v3.8\`. Nothing was flashed, no serial port was
opened, nothing was committed anywhere but the scratch tree, and nothing of v3.8 or v3.9 has run on
a board.

Plat's question once more, whether UTC and local times are mixed up: no. Windows is on W. Australia
Standard Time (+08:00) and WSL on Australia/Perth (AWST+0800); the v2 `tiny386.bin` was written
2026-10-21 13:19:18 +08:00 (05:19:18 UTC, the same instant), the dual-boot bootloader 16:02:33, the
commit tagged `v3` 18:20:20 +0800 [verified, again in this round: `Get-TimeZone`, `LastWriteTime`
and `LastWriteTimeUtc`, `date`, `git log`]. His photo of the blue screen is 18:29 [reported]. Nine
minutes after the first v3 commit, with no v3 image built, is far too short for a v3 app to have
been built, flashed, booted into Windows, taken through first-run setup and blue-screened while he
was out, so the blue screen was on the v2 app he flashed that afternoon [inferred], as he says.

### 21.1 The commits [verified: `git log v3.8..v3.9` in WSL `~/tiny386-idf-scratch/v3/tiny386`]

| Commit | What |
|-|-|
| 90e56ee | `i386.c`: the front end's note (`cpu_abort_note`) at most once and before the line that names the cause, TRY1 through `cpu_abort_at()`, `deliver()` before `shutdown:`; the `lldt_check()` comment. `i8254.c`: the comment on `i8254_max_owed` (21.3) |
| cc399af | `console.c`: what the limit still holds said into the ring at a crash, the count line at the held lines' level; `status_led.c`: the stall line through `console_printf()`; `display.c`: two more tasks in the stack report (21.4) |
| 78fa828 | `tests/host`: test 2 part 9, test 20's note order and `run_cpu.sh`'s TRY1 read and v3.8 run, test 15's stall line (21.5) |
| d370a7a | `esp/version.txt`: `v3.9` |

`git diff v3.8 v3.9`: 10 files, 243 lines added, 26 taken out, every file at mode 644. Outside `esp/`
and `tests/` only `i386.c` (33 added, 10 taken out) and `i8254.c` (a comment). The patch is `git diff
pristine v3.9`: 141 files, 26,128 added, 709 taken out, 1,029,732 bytes, SHA-256 daedf6ad...
(`tiny386-idf-v3.diffstat.txt`). `esp/dependencies.lock` was modified in the tree before this round
and is left out of every commit, as before; the scratch tree's commits carry no co-author line, as
its earlier ones, since they are what goes upstream in Plat's name.

The scripts are not in the tree: `flash-dualboot.bat` and `flash-standalone.bat` changed in this
folder (21.2); `back-to-linux.bat` and `flash-paths.cmd` did not.

### 21.2 The scripts after a write cut short (BS F1, F2, F3) [verified: the scripts; `checks\bat-harness-v3.9.txt`]

`dualboot\DUALBOOT.md` 14 is the full account. In short:

* Over Linux's table both scripts read the first 256 bytes at 0x2000, one flash page, and name them
  by SHA-256: the dual-boot bootloader's start (`4fd69a58...`), tiny386's own (`7d9e47c7...`), or an
  erased page (`3d6876a0...`). esptool writes from the start address up, so a write cut short
  leaves one of those there [inferred]; every bootloader this folder knows differs from the others
  by byte 28 [verified: `checks\heads-v3.9.txt`].
* With such a page and a backup of this board that differs from the flash, the backup is kept and
  the run writes its set. With such a page and no backup, nothing is taken or written, and the
  message names the ways on. A backup holding the cut state itself (only an older script can have
  taken one) is refused too. Otherwise, a backup that differs means Linux was changed since, and
  the stop now names `back-to-linux.bat COMport` beside renaming.
* `app` now reads 0x2000 and stops behind any bootloader but the dual-boot one (F2).
* The DOS board's image in `images\dosboard\` is refused as `TINY386_BIN` or `SA_TINY386_BIN` (F3).

The harness, `checks\bat-harness-make-flow-v3.9.py` (made by `checks\mk_v39.py` from the boot swap's
generator, unchanged), runs the boot swap's 69 cases, the review's 19 (70 to 88) and 15 more (89
to 103) through cmd.exe with PATH set to `C:\Windows\System32` and `C:\Windows` only: no write
reached the stub in any of the 103. Against v3.8's record (`checks\bat-harness-v3.8.txt`), 55 of the
69 cases give the same exit code, the same WOULD WRITE lines and the same esptool calls; the other 14
differ only by the new reads (the 256-byte read in the 11 cases over Linux's table, the bootloader
read in `app` cases 8, 9 and 55), with the same exits and writes. The review's cut cases 84 and 85
now finish with the backup kept, 87 takes nothing, 71 (as 91) stops. `dualboot\DUALBOOT.md` 14.4 has
the table.

The flash simulations follow the write lists, which did not change: both give the same results as
v3.8's with the v3.9 images, differing only in the app's size, hashes and version and the work
folder [verified: `checks\flash-simulation-v3.9.txt`, `checks\flash-simulation-bootswap-v3.9.txt`].
The update Plat makes next, v3.8 (or v3.7, v3.6) to v3.9 by `app`, changes sectors between 0xE20000
and 0xEFBFFF only, and gives byte for byte what the whole v3.9 set written over be5b725 gives
[verified: `checks\app-update-v3.9.txt`].

### 21.3 The abort's cause on the UART, and the order on the crash screen (BS L1, L5; EW N3, N6) [verified: `i386.c`; test 20]

v3.8 added `cpu_abort_note` to the core and called it first in `cpu_abort()`. But TRY1 prints `@ file
function line`, and `deliver()` prints `shutdown: ...`, before calling `cpu_abort()`; with
`uart_log = 0` the board's UART was still quiet then, so the line that names the cause reached the
ring and the panel but not the UART, and the counts of lines held back, said by the note, came
after it on the crash screen. Now:

* `cpu_abort_begin()` takes the note's pointer, clears it, and calls it: at most once.
* TRY1 is `cpu_abort_at(cpu, __FILE__, __FUNCTION__, __LINE__)`, a `noinline` function on the board
  that calls `cpu_abort_begin()`, prints the `@` line and calls `cpu_abort()`; `deliver()` calls
  `cpu_abort_begin()` before its `shutdown:` line; `cpu_abort()` calls it first, as before, for the
  aborts that print nothing before it.
* So on the board an emulator abort now reads: the counts of held lines, `tiny386 v3.9 stopped the
  PC.` with the `Disk:` and `Timer:` lines, the cause line, `abort: ...`, the registers; all of it on
  the UART as well.
* With no note (the Linux route, upstream, the PC harness, tiny386's own builds) the same lines come
  in the same order as before. The PC runs give v3.8's results to the instruction (21.6).
* The 50 TRY1 sites now call one function with four arguments, where each had a `fprintf` with five
  and a call of `cpu_abort()`: the board's `.iram0.text` is 78 bytes smaller (21.9).
* Comments: `lldt_check()` now says a not-present LDT descriptor is #NP from `set_seg()` (EW N6);
  `i8254_max_owed` says that N above 1 makes up to N owed ticks and drops more than N to one (EW N3).

`research\win95-core-v3.9.patch` is v3.8's core patch with this change: `git diff v3.7 v3.9` of
`i386.c`, `i386.h`, `i8254.c` and `i8254.h`, and `git diff v3.7 c412ab0` of `misc.c` (its CMOS hunk
alone, as in v3.8's), 18,363 bytes, SHA-256 7e04af75...; applied to v3.7 it gives v3.9's four files
and v3.8's `misc.c` change exactly, and it applies to upstream 9ed786b [verified: WSL
`~/win95-debug/fix-v39/core.sh`]. `research\win95-core-v3.8.patch` is kept as it was.

### 21.4 The console and the status LED (BS L2, L3, L4, L6) [verified: `console.c`, `status_led.c`, `display.c`; tests 2 and 15]

* **A crash with lines held** (L2): `console_draw_crash()` first walks the limit's 16 places and, for
  each kind still holding lines, writes `(not shown: N more lines like "..." before the crash)` into
  the ring, with the newest line held, then reads the log's last lines as before. In the panic
  handler: no lock, no stdio (`put_str()`, a `put_dec()` beside it). The counts are cleared once
  said. An emulator abort has said them already (`console_log_limit(false)` in its note), so this
  matters for the other crashes: a device model's load fault, the interrupt watchdog, a stack
  overflow, an assert.
* **Held errors** (L4): each place keeps the most serious level among the lines it held, and the
  count line is said at that level, so held `ESP_LOGE` lines give a red count, which also counts as a
  problem. Error lines still go through the limit, so a card that fails thousands of times a second
  costs a few lines, as before.
* **The stall line** (L3): `led_task()` says `status LED: the emulator has made no progress for 10 s
  (yellow 5)` with `console_printf(CON_WARN, ...)` and its end with `console_printf(CON_INFO, ...)`:
  ring, panel and, when it is on, UART. `console_uart_printf()` takes the console's lock as well, so
  the LED task waits for nothing it did not wait for before; drawing the line on the panel happens
  only while Ctrl+Alt+F12 is up [inferred: `emit()`, `render()`].
* **The stack report** (L6): `display.c`'s names add `status_led` and `uart_log`; its line is 288
  bytes (was 240). The report goes to the UART only, so it is there with `uart_log = 1`.

`STATUS-LED.md` 8 is rewritten for what blue and green mean (BS D2) and has the note on a frozen LED
(D1); its yellow 5 row says where the line now goes.

### 21.5 Host tests [verified: `checks\host-tests-v3.9.log`]

32 passed, 0 failed, at the tag; again with an output folder of 120 characters, 32 of 32; in the
tree `build.sh` made from the patch, 29 of 29 (the three runs that need older tags left out)
[verified: `host-tests-v3.9-long-out.log`, `host-tests-v3.9-from-patch.log`]. The host build's 11
warnings are v3.8's same lines.

* Test 2, part 9: 7 error lines of a kind in 5 s give 5 shown and, at 5 s, a count that is a problem
  too; 40 lines of a kind in 2 s, then the crash screen: `(not shown: 35 more lines like
  "read_desc: sel 77e3 ..." before the crash)` is in the ring before the crash report's own lines,
  on the screen among the log's last lines, and said once. v3.8's `console.c` fails these four checks
  [verified: WSL `~/win95-debug/fix-v39/v38c`].
* Test 20 (`tests/host/cpu`): CR3 past memory and vector 8 an interrupt gate run again with a front
  end's note, whose child's stderr is read back: the note once, before `shutdown:`, the abort line
  after it, in both builds (the Korvo-1's header and the Linux route's flags). `run_cpu.sh` reads
  TRY1's way out in `i386.c` (no guest here reaches a TRY1) and runs v3.8's `i386.c`, whose only
  failures must be the note's two checks: they are. `cpu: 5 passed, 0 failed`.
* Test 15: `led_task()`'s source says the stall line and its end with `console_printf()`.

Mutation run: 12 faults put one at a time into copies of an export of the tag (the crash's counts
not said, said grey, the level not kept, the counts said twice, the oldest line kept instead of the
newest, the note after `shutdown:`, the note called twice, the note after TRY1's line, v3.8's TRY1
macro, the stall line to the UART alone or grey, its end to the UART alone): 12 of 12 caught by the
test named for each; the unchanged files pass [verified: `checks\mutants-v3.9.txt`, made by
`checks\mutants-v3.9.py`]. Under AddressSanitizer and UBSan, with address-space randomisation off:
tests 2, 15 and `test_floppy` and the logging benchmark run clean; test 20 runs clean but for
tiny386's own `1<<31` shifts (`CR0_PG` and one in `i386ins.def`), which v3.8's `i386.c` gives the
same in the same test, and which with recovery off stop each child at its first report; the leaks
are the tests' own (`console_init()` called twice and the like) [verified: `checks\asan-v3.9.txt`].
The logging benchmark gives v3.8's figures: a boot's 180 lines to 107 with the limit, a storm's
100,000 to 144 [verified: `checks\log-cost-v3.9.txt`].

### 21.6 Windows 95 on the PC [verified: WSL `~/win95-debug/fix-v39/pc/runs-v39`; `checks\win95-pc-runs-v3.9.txt`]

The shared core changed (21.3), so v3.9 ran Windows 95 OSR2 again: the emulator review's harness
(investigation 1's, with fix 1's additions), the v3.9 tag built with the board's core flags
(`esp`, 64-bit), a fresh copy of the pristine image or card for each run, the review's nine v3.8
runs and two more at 13M with a 1 MiB VGA, which Plat ran on the board.

| Run | Model | Desktop at, v3.9 | v3.8 (the review) | Timer: most owed, dropped | Longest card access |
|-|-|-|-|-|-|
| t-fast-14M-a | no hold, 14M | 211 s | 211 s | 1, 0 | |
| t-stall100-4M-a | 100 ms a disk access, 4M | 8,720 s | 8,720 s | 51, 508,405 | |
| t-boardfrag-14M-a | `rand16`, 1 ms a card read, 14M | 266 s | 266 s | 233, 232 | 3.0 ms |
| t-spike5-4M-a | every 5th card write 250 ms late, 4M | 1,148 s | 1,148 s | 19, 69,692 | 250.3 ms |
| n-stall100-4M-mouse | 100 ms holds, a mouse report every 10 ms | 8,622 s | 8,622 s | 51, 499,643 | |
| n-boardfrag-14M-mouse | `rand16` and the mouse | 285 s | 285 s | 233, 232 | 3.0 ms |
| n-slow1M-fast-14M | no hold, the guest at 1 M a second | 436 s | 436 s | 1, 0 | |
| n-slow1M-stall100-14M | 100 ms holds at 1 M a second | 3,583 s | 3,583 s | 52, 172,127 | |
| n-bootlog-boardfrag-14M | `rand16`, a logged start | 278 s, `BOOTLOG.TXT` 21,913 bytes | 278 s | 233, 232 | 3.0 ms |
| n-stall100-13M-1M | 100 ms holds, 13M, VGA 1 MiB | 3,088 s | (new) | 51, 156,188 | |
| n-boardfrag-13M-1M | `rand16`, 13M, VGA 1 MiB | 265 s | (new) | 233, 232 | 3.0 ms |

11 of 11 at the desktop. The nine runs the review made end with the same `END` line, the same virtual
time and the same instruction count as the review's v3.8 runs, to the instruction [verified: their
logs side by side]. I looked at the final picture of all 11: each is the Windows 95 desktop with its
icons, the Start button and a clock; five show Internet Explorer 4.0 Active Setup (first-run setup),
the runs with holds the desktop alone, some with the hourglass; no error box [verified: a contact
sheet in the session's scratch folder, kept off the package, as are all pictures of Windows].

### 21.7 The Linux route with the v3.9 core [verified: WSL `~/win95-debug/fix-v39/lr.out`, `lrtests-v39*.log`]

The emulator review's copy of the Linux route's `build.sh` (its `lrbuild.sh`, the same steps, flags
and toolchain, tiny386 cloned from the scratch tree), with `research\win95-core-v3.9.patch` applied
after the route's own patch, and the route's tests through the review's copy of `run-tests.sh`, whose
one `pkill` by pattern is replaced by a line that records the PIDs (never reached; no guard left
running):

| Build | `tiny386-korvo` | SHA-256 | Warnings | Its own tests |
|-|-|-|-|-|
| + the v3.9 core patch | 591,508 bytes | 21628773... | the same 5 lines as the release's | 26 of 26 |
| + the v3.9 core patch, `VARIANT=ua` | 603,796 bytes (`-ua`) | 3818f5db... | the same 5 lines | 26 of 26 |

After the patch the route's `i386.c`, `i386.h`, `i8254.c` and `i8254.h` are v3.9's byte for byte.
The review's baseline (9ed786b with the route's patch) reproduced the release's binary byte for byte
(dd34a04f...) [reported: the review, 4.1], so the build is faithful. The tests include FreeDOS from
the disk and from the floppy, BENCH's nine marks, the VGA modes and the console guard.

### 21.8 `build.sh` and the four image sets [verified: `checks\reproducible-v3.9.txt`]

`build.sh` changed in its first comment only (the version, and that the tiny386-default folder has
no ELF). Two runs from nothing (WSL `~/win95-debug/fix-v39/rel1` and `rel2-second-run`): exit 0, 236
s and 218 s; all 23 files of the four sets the same in both, ELF files and `SHA256SUMS` included, and
no ELF holds its work folder's name; the patch applied to a fresh clone gives the tag's tree
exactly (b481594d), every file v3.9 changed at mode 644. The bootloaders, partition tables and
`flash_args` of all four sets are v3.8's byte for byte; the three apps differ in `.iram0.text`,
`.dram0.data`, `.flash.text`, `.flash.rodata`, `.flash.appdesc` (and `.flash.tdata` on the
Korvo-1), with 5 new symbols and none gone, the same in each: `cpu_abort_at` (four constant-
propagated copies) and `stacks_line`, which is no longer inlined.

### 21.9 Sizes [verified: `checks\size-*-v3.9.txt`]

| | Korvo-1 v3.9 | v3.8 | DOS board v3.9 | v3.8 |
|-|-|-|-|-|
| `tiny386.bin` | 899,248 | 899,024 | 930,960 | 930,720 |
| Flash `.text` | 434,594 | 434,366 | 431,514 | 431,286 |
| Flash `.rodata` | 233,240 | 233,160 | 264,588 | 264,500 |
| Internal RAM used by code and static data | 249,710 | 249,788 | 252,708 | 252,786 |
| Internal RAM left | 254,034 | 253,956 | 251,036 | 250,958 |
| LP SRAM | 8,244 | 8,244 | 8,244 | 8,244 |

78 bytes more internal RAM free on both boards, all of it IRAM code (TRY1's sites, 21.3). The
dual-boot `tiny386.bin` uses 899,248 of its partition's 1,966,080 bytes, 54.3 per cent spare
[verified: flash simulation].

### 21.10 Checks [verified unless marked]

| Check | Result |
|-|-|
| `build.sh` from nothing, twice | as 21.8 (`checks\reproducible-v3.9.txt`, `build-sh-v3.9-run1.txt`, `run2.txt`) |
| `esptool image-info` | every bootloader and app valid, checksum and hash; the apps say `v3.9`; rel2's the same as rel1's; the bootloaders' the same as v3.8's (`checks\image-info-*-v3.9.txt`) |
| In-tree builds, release and crash-test, both boards | exit 0; compiler warnings: the old `i2s.c` line only (`checks\warnings-wall-wextra-v3.9.txt`) |
| `-Wall -Wextra` on the files v3.9 changed and two that call into them, beside v3.8's | no new warning: in each of the three builds the firmware's files give none, and `i386.c` 34 beside v3.8's 34, all tiny386's own `%x` for a `uword` or `u32` (`checks\warnings-wall-wextra-v3.9.txt`, made by `warnings-wall-wextra-v3.9.py`) |
| The status LED and the console in the built code | the panic path all in internal RAM, `s_level` 09 00 00 00, as v3.8; `led_task()` calls `console_printf()` and no UART-only printf; `console_draw_crash()` reads the limit's places; the stall line's and the crash count's strings, and `status_led` and `uart_log` in the stack report, in all three images (`checks\status-led-v3.9.txt`); `build.sh`'s bit-bang layout check passes in all three |
| Host tests | 32 of 32; 32 with a long folder; 29 of 29 from the patch (21.5) |
| Mutation run, sanitizers, logging benchmark | 12 of 12 caught; clean but for tiny386's own shifts; v3.8's figures (21.5) |
| The PC's Windows 95 runs | 11 of 11 at the desktop, v3.8's times to the second (21.6) |
| The Linux route | 26 of 26 in both variants, the release's 5 warnings (21.7) |
| The flashing targets | as v3.8, line for line (`checks\cmake-flash-refusal-v3.9.txt`; no flash target was run) |
| The flash simulations | as v3.8's but for the app's size, hashes and version (21.2) |
| v3.8 to v3.9 by the app alone | 0xE20000 to 0xEFBFFF only; equal to the whole set over be5b725 (`checks\app-update-v3.9.txt`) |
| The scripts' flow | 103 cases, no write reached the stub; 1 to 69 as v3.8's but for the new reads (21.2, `checks\bat-harness-v3.9.txt`) |
| The card | `setup.c` and `ini.c` on a pretend card: 0 problems; the card files are v3.8's byte for byte; every `[esp]` key the inis name is known (`checks\card-check-v3.9.txt`) |
| `SHA256SUMS` | made again: v3.8's 45 files, the two reviews of v3.8 and three files in `research\` (the two core patches and `WIN95-FIX.md`), 50; every line verifies, and every image folder's own; `previous-v3.8\`'s list verifies, 30 files inside it and 15 from `v3\`; every earlier previous folder's list still verifies, `previous-v3.7\`'s and `previous-v3.6\`'s `REVIEW-RESOLUTION.md` line now from `previous-v3.8\`, as `README.md` says |

### 21.11 Corrections to earlier sections

* 19.7: "the photo's stack bytes" is the four return addresses, 16 of the 32 bytes of the photo's
  stack line, plus the frame's tail and SP on a page boundary; "into first-run setup" holds for 30 of
  the 51 runs (EW F2, F3). Both are marked where they stand; `research\WIN95-FIX.md` 0, 2, 4.1 and
  its new section 9 likewise.
* 20.3 and 20.4: "its dump is whole" now holds on the UART too (21.3).
* `dualboot\DUALBOOT.md` 12.2 (1c, 1d), 13.1 and 13.5 (C5), corrected in place with a pointer to its
  new section 14.

### 21.12 What only a board can say [unknown]

* Everything in 20.10 and 19.9, Windows 95 above all, and 18.7.
* Whether the failures are the timer's on the board too: `TEST-PLAN.md` 8e's run with
  `timer_catchup = 0` after a desktop, with the `Timer:` line photographed both times (EW F1).
* How a real cut write leaves the first page at 0x2000 (21.2 rests on how esptool writes).
* The stack use of `uart_log` and `status_led`, on the UART with `uart_log = 1`.

### 21.13 For Plat to decide

* Whether to send `research\win95-core-v3.9.patch` upstream in his name (hchunhui/tiny386 has the same
  PIT and no double fault), and whether the Linux route's release takes it: it passes the route's
  26 tests in both variants (21.7).
* A guest clock that stops while the emulator is held up, so that DOS's time of day stays right (EW
  N5): not needed for Windows; not made.
* 20.11 and 19.10 still stand.

### 21.14 Scripts and logs

WSL `~/win95-debug/fix-v39/`: `sync.sh` (the edited sources from the session's scratch folder
`fix39\src` into the tree), `t1.sh` to `t4.sh`, `b1.sh` (in-tree builds, `build-v39*.log`),
`commit1.sh`, `rel.sh` (the staging in `stage/` and the two builds), `repro.sh`, `prev.sh`
(`previous-v3.8\`), `install.sh`, `core.sh` (the core patch), `lr.sh` with `lrbuild39.sh` and
`run-tests-v39.sh` (the Linux route), `pcsetup.sh`, `pcrun.sh` and `pc/` (the Windows runs),
`hgen.sh`, `ii.sh` and `hgen/` (the scripts' harness generator), `checks39a.sh`, `checks39b.sh`,
`asan20.sh`, `asan20b.sh`, `mutants-v3.9.py` and `mut/`, `heads.py`, `collect.sh`, `sums.sh`, and the
`s*.sh` that only read. On Windows the session's scratch folder `fix39\` holds the edited sources
(`src\`), v3.8's (`orig\`), the scripts as edited (`bat\`, `stage-bat\`), the docs as edited
(`docs\`), the edit scripts, the harness (`fix39\harness`, run through its `go.cmd`) and `hcol9.py`,
which made its record. Nothing was started that is still running.

## 22. v4.0: Windows at the panel's own size (2026-10-23)

Plat asked, after Windows 95 reached its desktop on v3.9 at 640x480 in 16 colours: "Whatever video
bios you think is best - ideally if we could get the resolution of the LCD itself, so it's not
boxed to 4:3, that would be amazing. 256 colour minimum, 16 bit preferred, 24 bit amazing." v4.0
is the answer. Windows 95 now runs at 800x480 on the Korvo-1 (800x600 on the DOS board), filling
the screen, at 256 colours, High Color (16 bit) or True Color (24 and 32 bit), through a free
Windows 9x display driver for this emulated hardware, VMDisp9x by Jaroslav Hensl, which Plat
installs from its own floppy image (13 below). The VGA BIOS stays SeaVGABIOS, with five modes at
800x480 added for DOS programs. What made it worth doing on this board is the drawing: v3.9
worked out every pixel of a VBE picture at every picture, with a division in each, which at
800x480 is 4 to 9 million instructions a picture whether anything changed or not; v4.0 draws again
only the rows the guest wrote into, with no division.

v4.0 is also v3.9 merged with tiny386's master of 2026-10-22 (4ce2e06), the merge step's
`v4.0-base` (`research\UPSTREAM-MERGE-v4.md`); the display work is phase 1 of
`research\NATIVE-VBE-DESIGN.md`. The app calls itself `v4.0`, tag `v4.0` (8d65b1d) on the branch
`v4` of the scratch tree; v3.9's images, patch, `build.sh`, notes and VGA BIOS are kept in
`previous-v3.9\`. Nothing was flashed, no serial port was opened, nothing was committed anywhere but
the scratch tree, and nothing of v4.0 has run on a board.

### 22.1 The commits [verified: `git log v4.0-base..v4.0` in WSL `~/tiny386-idf-scratch/v3/tiny386`]

| Commit | What |
|-|-|
| be62009 | `vga.c`, `vga.h`, `pc.c`, `pc.h`: VBE pictures drawn from the blocks of video memory written (`VGA_DIRTY_BLOCKS`); shrunk pictures with no division a pixel; the bank window at any size; 16 and 32-bit reads of the linear frame buffer in one load; no clear for a display start (22.2) |
| 574334c | `esp/main`: `VGA_DIRTY_BLOCKS` in both boards' headers; `[esp] vbe_blocks`; a `vga_mem_size` that is not a whole number of 64K rounded down with a yellow line (22.2) |
| 46da196 | `tests/host`: `test_vbe.c` and its section 3c in `run.sh`; two pictures of `test_vga.c` (22.5) |
| 8d65b1d | `esp/version.txt`: `v4.0` |

`git diff v4.0-base v4.0`: 11 files, 1,145 lines added, 18 taken out, every file at mode 644. The
build's base is now tiny386's master: `tiny386-idf-v4.patch` is `git diff upstream-20261022 v4.0`
(143 files, 27,375 added, 724 taken out, 1,081,946 bytes, SHA-256 b051eb0d...), which `build.sh`
applies to 4ce2e06. `tiny386-idf-v3.patch` keeps its old meaning, everything against 9ed786b, now
`git diff pristine v4.0` (150 files, 28,005 added, 739 taken out, 1,109,953 bytes, SHA-256
d690e240...), for comparing with the earlier patches. Each, applied to a fresh clone at its base,
gives the v4.0 tag's tree exactly (09f94dfc) [verified: `checks\reproducible-v4.0.txt`].
`esp/dependencies.lock` stays out of every commit, as before; the scratch tree's commits carry no
co-author line, as its earlier ones.

### 22.2 The drawing [verified: `vga.c`, `pc.c`; tests and runs as named]

Every store the guest makes into video memory now also sets a byte for its 1 KiB block: the four
chain-4 paths of `vga_mem_write*()` (the bank window, which VBE modes use) and the four paths of the
linear frame buffer in `pc.c` (`iomem_write8/16/32()` and `iomem_write_string()`, which Windows'
driver uses). Each byte is set after the bytes it stands for, with a release fence between
(`vga_dirty_mark()` in `vga.h`; `fence rw,w` on the board). The map is in the device window with
the VGA's state: `(vga_mem_size + 16K) / 1K + 1` bytes, 1,041 for 1M.

When `vga_refresh()` draws a VBE picture of 8, 15, 16, 24 or 32 bits a pixel written through chain
4 or the linear frame buffer (Windows' driver, the BIOS's VBE modes), `vga_vbe_refresh()`:

1. draws all of it if anything that places or colours a pixel has changed since the last picture
   (size, depth, start, line length, place on the screen, the palette in 8 bits, a clear, another
   kind of picture in between, or the caller's `full_update`, which the board gives after its
   console);
2. otherwise clears the byte of each block the picture's lines lie in that was set, notes the rows
   those lines are on, and then fences (`fence rw,rw`), so that a store the guest makes after the
   drawing has read its block sets the block's byte again for the next picture;
3. converts only those rows, with one loop for each depth and no division (`vbe_row()`): 8 bits
   through the palette four pixels a word, 15 bits two pixels a word with green widened, 16 bits
   copied two pixels a word, 24 bits four pixels from three words, 32 bits two pixels from two
   words; each word of the screen is stored only where it differs, as before;
4. gives the rows that changed to `redraw()` in bands (rows less than 16 apart are one band), so
   that the board writes back from the cache only those.

The colours are exactly those of the old loop (the top 5, 6 and 5 bits; 15 bits with its green's
lowest bit 0; bit 15 of a 15-bit pixel and the fourth byte of a 32-bit one ignored): the host test
holds the two against each other pixel for pixel (22.5). A picture larger than the screen (800x600
on the Korvo-1, still shown shrunk to fit) takes each column from a table made once for each size,
where it worked out a 64-bit division for every pixel, and a VBE one of 8 to 32 bits is drawn again
only in the rows that show lines written.

Also in this commit, for every build (not only the boards'):

* **The bank window** (`VBE_DISPI_INDEX_BANK`): the bank is the register's value modulo the banks
  there are. With video memory not a power of two in size the old mask (the number of banks less
  one) put banks over each other: at 768K, banks 4 to 7 on 0 to 3, so a DOS program drawing 800x480
  in 16 bits through the window wrote rows 164 to 327 over rows 0 to 163. For a power of two the
  two are the same. Windows' driver uses the linear frame buffer and was never affected.
* **Reads** of 16 and 32 bits from the linear frame buffer are one load, as the writes were, where
  each was two or four calls of `iomem_read8()`. Windows' display drivers read the screen back (the
  mouse pointer's background, raster operations).

And with `VGA_DIRTY_BLOCKS` only:

* **No clear for a display start.** A write to the X or Y offset (a driver's page flip or panning,
  VBE 4F07h) asked for a clear of the whole panel, a black flash each time (the merge step's
  finding). The picture's size and place are the same, so it is now drawn again over itself.
* **`[esp] vbe_blocks`**: 1 without the line; 0 draws VBE pictures in full every time, as before
  v4.0 (the boot console says so in a line). A way back on the board if the new drawing misbehaves
  there, with no new firmware. The blocks are noted either way. [Corrected in v4.1: since v4.1
  `vbe_blocks = 0` notes no block, and a store through the window is noted only while VBE is on;
  23.2.]
* **`vga_mem_size`** that is not a whole number of 64K (VBE reports video memory in 64K, and the
  bank steps by it) is rounded down to one, with a yellow line on the boot console.

What the marks cost the emulator's core: in the built code, a 16-bit store into the linear frame
buffer (`iomem_write16()`) runs about 6 instructions more than in v3.9, one of them the fence
[verified: `checks\vbe-loops-v4.0.txt`, the two images' disassembly]; what a `fence rw,w` costs on
the S31's core is [unknown]. [Added in v4.1, from the reviews: so did every chain-4 store through
the window, mode 13h's included, 26 instructions in v3.9 and 34 in v4.0, though nothing reads
those marks; 23.2.] In the PC runs the guest makes no store more and no fewer, so its
instruction counts are v4.0-base's exactly (22.7). Memory: the device window (the guest's
0xA0000, 128 KiB) holds 3,488 bytes more at 768K and 3,744 more at 1M (the map, a table of the
screen's columns, a byte for each screen row, the last palette) [verified: the harness's "device
memory" line, wt against v40b]; internal RAM, 170 bytes more code [verified: 22.11].

### 22.3 What the design's phases 2 and 3 would add, and why they are not here [inferred]

* **Phase 2** (the panel scanning the guest's own 16-bit buffer, no copy) needs the RGB panel
  started again with user frame buffers after the PC is built, and the crash screen drawing into
  whichever buffer is on show. Neither can be tried on the PC, and a mistake there is a black panel
  or a crash screen that does not show. After phase 1, what it would save is the copy of the rows
  that changed: 3 instructions a pixel, about 1,200 a row of 800 (22.8), and nothing at all on a
  still screen. Worth it only if the board shows the copy matters (22.13).
* **Phase 3** (the S31's pixel processing accelerator for 24 and 32 bits): the CPU now converts a
  changed row of 32 bits in about 10,000 instructions (22.8). The same: only if the board says so.

### 22.4 The VGA BIOS [verified: `bios\`, `checks\bios-tests-v4.0.txt`, `checks\vbe480\results-v4.0.txt`]

The best VGA BIOS for this is the one there was: SeaVGABIOS, with its VBE 3.0 back end for exactly
this hardware (Bochs DISPI) and the Linux route's fixes. Windows' driver does not use the BIOS to set
its modes at all; DOS programs and VBE drivers that go through the BIOS find their modes in its list.
So v4.0's `card\tiny386\vgabios.bin` is SeaVGABIOS built from source with one patch more,
`bios\patches\seabios-korvo-800x480.patch`: modes 1A0h to 1A4h, 800x480 at 8, 15, 16, 24 and 32 bits.
The BIOS keeps or drops each by video memory as it does every mode: at 768K and 1M it lists 1A0h to
1A2h, at 1536K and 2M all five [verified: BVBE480's runs], at 1152K 1A3h as well [arithmetic].
800x600 (103h, 113h, 114h, 115h, 143h) was there already; every mode wider than 800 goes, as before
(the emulator answers 800 to the BIOS's question).

* Built twice with `bios\tools\build-bios.sh` (the Linux route's script with the 800x480 patch added
  after `seabios-korvo.patch` and its version string's time and host `20261023_000000` and
  `korvo-v4`): the same bytes both times. 40,960 bytes, SHA-256 4aeae74f...; version
  `rel-1.17.0-20-g81ec9ec0-dirty-20261023_000000-korvo-v4`.
* With Debian 12's gcc 12.2 and binutils 2.40 (`TOOLCHAIN=host`): the Linux route's own files were
  built with Ubuntu 26.04's gcc 15.2, which its script fetches from Ubuntu's archive, and this work
  fetched nothing from there. So the new file differs from the old in more than the five modes (it
  is 512 bytes longer, as BUILD.txt says a Debian build is). The same source without the patch,
  built the same way, was tested beside it.
* The Linux route's BIOS coverage tests (`tests\bios-tests.sh`, its board binary under
  qemu-riscv32-static, unchanged): the release's `vgabios.bin`, the Debian build without the modes
  and v4.0's each give 765 passed and the same 6 known failures, nothing new. Against the release's,
  v4.0's results differ only in lines that are for the record (`@info`): the font tables' addresses
  in the ROM (C000:97E0 against C000:9AC0; the compiler's layout), the number of modes in the list
  (31 to 34 at 1M, the three new ones that fit), three new `4F01h` lines, and two timing figures of
  the retrace and the cursor's blink that move from run to run.
* `bios.bin` is the Linux route's file, unchanged: the patch touches only the VGA BIOS. The Linux
  route keeps its own `vgabios.bin`.
* SeaBIOS is LGPL 3: `bios\` holds the source tree as built (`seabios-korvo-v4-src.tar.xz`), the
  four patches and the configuration, the script and how it differs from the Linux route's
  (`build-bios-v4.diff`), and `README.txt`.

### 22.5 Host tests [verified: `checks\host-tests-v4.0.log` and the files named]

35 passed, 0 failed, at the tag; again with an output folder of 116 characters, 35 of 35; in the
tree `build.sh` made from the patch, 29 of 29 (the runs that need the scratch tree's older tags left
out). The host build's 11 warnings are v3.9's same lines.

* **`test_vbe`** (new, run for each board's header): `vga.c` linked three times, as the board builds
  it, the same with `VGA_DIRTY_BLOCKS` taken out (the old loop, every pixel every picture), and
  v4.0-base's `vga.c`, the second and third with their symbols renamed. The same VBE modes go to
  each (8 to 32 bits; the screen's size, smaller and larger; 768K, 1M, 1536K and 2M), the same random
  stores through the bank window (8, 16 and 32 bits and strings) and the linear frame buffer, and
  palette, display start, line length, `vbe_blocks` and `full_update` changes; after every picture
  the screens must be the same, and every row of the new screen that changed must have gone to
  `redraw()`. 6,768 pictures, 6,790 checks on each board. Then a second thread writes the linear
  frame buffer while the first draws, 300 pictures at each depth: when it stops, one more picture
  is the same as a full redraw, 0 pixels differing. And the bank window at 768K, 1152K, 1536K, 1M
  and 2M: each bank written where its number says; v4.0-base's must fail at the first three and
  pass at the others, and does.
* **`test_vga`**: two of its pictures wrote video memory behind `vga.c`'s back; they now say so
  (`vga_dirty()`), or ask for a full redraw as the board does after its console.
* **Mutation run**: 19 faults put one at a time into copies of the tree (each kind of store unmarked,
  in `vga.c` and in `pc.c`; a block's rows worked out wrong or not clipped; a palette or start change
  missed; each depth's colours wrong; a band not given to `redraw()`; the shrunk rows unmapped; the
  bank mask back; the LFB read from the wrong place): 19 of 19 caught, by `test_vbe`, BVBE480 (22.6),
  `test_vbe` under AddressSanitizer, or a Windows 95 run's final picture [verified:
  `checks\mutants-v4.0.txt`].
* **Sanitizers**: `test_vbe` on both boards under AddressSanitizer and UBSan: clean. Under
  ThreadSanitizer it passes too, with 19 reports, every one the drawing's read of guest video memory
  against the writer thread's store into it: the race the design allows, since the store's mark is
  set again after it and the next picture draws it; none on the mark map or the row marks
  (ThreadSanitizer cannot model the fences; it says so) [verified: `checks\asan-v4.0.txt`].

### 22.6 BVBE480: every mode at every depth, pixel for pixel [verified: `checks\vbe480\results-v4.0.txt`]

`checks\vbe480\bvbe480.S` (BVBE480.COM, in the style of the Linux route's `tests\guest\bvbe.S`,
with its `bios.inc`) runs under FreeDOS in the PC harness. For each of 1A0h to 1A4h, the five
800x600 modes, five 640x480 and two 320x200: whether 4F00h lists it as it should for the video
memory, 4F01h's figures, then pictures, each said on the BIOS's debug port and waited on: pattern A
through the linear frame buffer by int 15h AH=87h (the string path); single stores of 8, 16 and 32
bits into the linear frame buffer from real mode with FS made flat (the other three paths), at odd
and even addresses; pattern B through the bank window, a line at a time by `movsb`, `movsw` or
`movsd` in turn, lines that cross a bank split there, then single stores through the window, each
size on a line far from the others; the display start 16 lines down where video memory holds it;
and in 8 bits another palette with nothing else changed. The harness (`checks\vbe480\
headless-v4.0.diff`) saves each picture as the periodic drawing leaves it, holds it against a full
redraw, and `checks\vbe480\vbe480check.py` holds it against the picture it works out itself, 1:1,
centred or shrunk as `vga.c` places it.

16 runs, both boards, 768K, 1M, 1536K and 2M, the drawing in the run loop and in a second thread:
every picture right (38, 52, 67 and 72 pictures a run), none differing from a full redraw. v4.0-base
the same way: right at 1M and 2M, and at 768K and 1536K 15 and 21 pictures wrong, every one drawn
through the window (the bank fault).

### 22.7 Windows 95 on the PC [verified: `checks\win95-pc-runs-v4.0.txt`, `checks\win95-driver-install-v4.0.txt`]

* **The merge step's 27 configurations** (the standard VGA driver, the pristine image, the fast
  disk, the card models, the stall and spike models, three timings): this tree with v4.0's VGA BIOS,
  27 of 27 at the desktop, each with the same `END` line (virtual time, instruction count,
  registers) and the same final picture as v4.0-base with the same BIOS. v4.0-base with the old VGA
  BIOS gives the merge step's own `END` lines, 27 of 27, so the harness is the same. The new VGA
  BIOS alone moves the timings a little (the stall model at 4M reaches the desktop at 8,376 virtual
  seconds against 8,720), as a different BIOS's own code would [verified].
* **VMDisp9x's `qemumini` at the screen's own size**, installed offline in copies of the image (the
  research step's way), 11 configurations on this tree and on v4.0-base: 800x480 in 8 bits at 768K,
  16 bits at 768K and 1M, 24 bits at 1152K, 32 bits at 1536K, the mouse still and moving, and
  800x600 in 16 bits with the DOS board's build. All 22 at the desktop (220 to 229 virtual seconds;
  300 to 304 with the mouse moving every 50 ms), each pair the same `END` line and final picture.
  In 6 of them, and in the two runs of the `.REG` files below, every periodic picture was also held
  against a full redraw: 93,766 pictures in 8 runs, none differing. I looked at the pictures: the
  desktop and Internet Explorer 4's first-run setup filling the screen, the taskbar 800 pixels wide,
  the colours right at every depth.
* **The install as Plat will make it**, through Windows' dialogs with the driver's own floppy image
  in B: and `card\tiny386\vbe-reg.img` in A:, keys at fixed virtual seconds: Have Disk, the QEMU
  adapter, a restart (16 colours as before, as expected), `VBE480.REG`, a restart: the driver sets
  800x480 in 16 bits and Windows comes up at the panel's size. One finding that changes the steps:
  Windows has this adapter as an ISA VGA, so the INF's compatible list offers only "VESA ISA
  Adapter" (the driver that copies the screen on a timer, which the design rejected); "Show all
  devices" lists the QEMU one. Another: with the driver's disk in A: the PC stops at "Invalid system
  disk"; `vbe-reg.img` in A: hands the start to the hard disk. Both are in TEST-PLAN.md 13. The run's
  periodic pictures held against full redraws: 35,842, one differing, at the moment of the first
  restart (the VGA's 16-colour mode going to text); v4.0-base gives the same picture at the same
  moment, so it is the old drawing's, and gone at the next picture.
* **The two `.REG` files** imported after the INF's own keys (offline): 800x480 and 800x600 in 16
  bits, at the desktop, the driver's register writes as expected.
* **BENCH.COM** (FreeDOS, the merge step's image): this tree and v4.0-base give the same marks at the
  same instruction counts with each VGA BIOS; with the new one every mark is 11,586 instructions
  later (the BIOS's start), and the stretches between them are the same [verified:
  `checks\bench-v4.0.txt`].

The drawing's host time on this PC (x86, the harness's `-O3`), one run at a time, microseconds a
periodic picture:

| Windows 95 | this tree | v4.0-base |
|-|-|-|
| 800x480x16, mouse still | 26.0 | 380.7 |
| 800x480x16, mouse every 50 ms | 23.3 | 392.0 |
| 800x480x32, mouse every 50 ms | 28.2 | 501.7 |
| DOS board 800x600x16, mouse every 50 ms | 23.3 | 474.7 |
| standard VGA 640x480x4, mouse still | 37.9 | 39.7 |
| standard VGA, mouse every 50 ms | 37.9 | 37.9 |

So at the panel's size the drawing costs less than the standard VGA's 16 colours did. The guest's
own work is another matter: Windows at 800x480 through this driver takes about 8 per cent more
guest instructions than the standard VGA driver with the mouse still, and 26 to 28 per cent more
with it moving all the time (the research step's figures, `research\NATIVE-VBE-DESIGN.md` 7.3;
v4.0 changes no guest instruction).

### 22.8 What a picture costs on the board [inferred from the built code; verified counts in `checks\vbe-loops-v4.0.txt`]

Instructions a pixel in the Korvo-1 image, the screen already showing the value (no store):

| Depth | v4.0 | v3.9 and v4.0-base |
|-|-|-|
| 8 | 6.5 (26 for four pixels) | 10, one a division |
| 15 | 5 (10 for two) | 18, one a division |
| 16 | 3 (6 for two) | 13, one a division |
| 24 | 13 (52 for four) | 23, one a division |
| 32 | 12.5 (25 for two) | 23, one a division |

And v4.0 converts only the rows written. A still 800x480 picture in 16 bits is now a look at 750
bytes of the map and the 480 row marks, some thousands of instructions, where it was 4.99 million
and 384,000 divisions. With the rows Windows changed in the harness (4 a picture with the mouse
still, 14 with it moving; the rows converted are somewhat more, a block reaching into a second row),
a picture at 16 bits is some tens of thousands of instructions. A whole new screen (a window
maximised) is 1.15 million at 16 bits and 4.8 million at 32, once.

### 22.9 For the card [verified: `checks\card-check-v4.0.txt`; the guest's memory inferred]

* `card\tiny386\vgabios.bin`: v4.0's (22.4). `bios.bin`, `tiny386.ini` and `win95-14m.ini`: as
  v3.9 had them. A card with v3.9's `vgabios.bin` still works with v4.0; it lacks only the 800x480
  BIOS modes.
* `card\tiny386\win95-800x480.ini`: Windows 95 for the native mode, `vga_mem_size = 768K` (16 bits;
  comments for 1152K and 1536K), `mem_size = max`, `vbe_blocks` and the floppies commented out.
  With 768K the boot console should give the guest 14M again, where Plat has 13M with 1M [inferred:
  the pool's size, `research\NATIVE-VBE-DESIGN.md` 4.1; the boot console's line says]. [Corrected in
  v4.1: 14M only if the pool is 15,120 KiB or more, else 13M; 23.9.]
* `card\tiny386\vbe-reg.img`: a 1.44 MB floppy image made here with mtools (the same bytes twice):
  `VBE480.REG`, `VBE600.REG` and `README.TXT`. The registry files add 800x480 to the driver's modes
  (800x600 it has), let the monitor go past 640x480, and make 16 bits at the screen's size the
  current mode.
* The driver is not in the package (a third-party binary): Plat takes
  `vmdisp9x-1.2025.0.119b-driver-2d.img` from github.com/JHRobotics/vmdisp9x/releases/tag/v1.2025.0.119
  (SHA-256 96eed45b78672d94f8a90dfa7806dd95a71c2114d22c568ec857a07efd174a63, as fetched 2026-10-23).
  `TEST-PLAN.md` 13 has the steps.
* Card check: `setup.c` and `ini.c` at the tag on a pretend card with these files: 0 problems; every
  `[esp]` and `[pc]` key the inis name is known; against v3.9's list only `vgabios.bin` differs.

### 22.10 `build.sh` and the four image sets [verified: `checks\reproducible-v4.0.txt`]

`build.sh` now clones tiny386 at 4ce2e06 and applies `tiny386-idf-v4.patch` (its first comment, the
default work folder, `TINY_COMMIT`, the patch's name and the tag it makes changed; nothing else).
Two runs from nothing (WSL `~/v4-work/vbe/rel/rel1` and `rel2-second-run`): exit 0, 276 s and 220 s;
all 23 files of the four sets the same in both, ELF files and `SHA256SUMS` included, and no ELF holds
its work folder's name; both patches give the tag's tree exactly (22.1). The bootloaders, partition
tables and `flash_args` of all four sets are v3.9's byte for byte; only `tiny386.bin` and
`tiny386.elf` changed. The update from v3.9 is the app alone.

### 22.11 Sizes [verified: `checks\size-*-v4.0.txt`]

| | Korvo-1 v4.0 | v3.9 | DOS board v4.0 | v3.9 |
|-|-|-|-|-|
| `tiny386.bin` | 902,224 | 899,248 | 933,904 | 930,960 |
| Flash `.text` | 437,216 | 434,594 | 434,132 | 431,514 |
| Flash `.rodata` | 233,416 | 233,240 | 264,756 | 264,588 |
| Internal RAM used by code and static data | 249,880 | 249,710 | 252,878 | 252,708 |
| Internal RAM left | 253,864 | 254,034 | 250,866 | 251,036 |
| LP SRAM | 8,244 | 8,244 | 8,244 | 8,244 |

These include the merge with upstream. The dual-boot `tiny386.bin` uses 902,224 of its partition's
1,966,080 bytes, 54.1 per cent spare [verified: flash simulation].

### 22.12 Checks [verified unless marked]

| Check | Result |
|-|-|
| `build.sh` from nothing, twice | as 22.10 (`checks\reproducible-v4.0.txt`, `build-sh-v4.0-run1.txt`, `run2.txt`) |
| `esptool image-info` | every bootloader and app valid, checksum and hash; the apps say `v4.0`; rel2's the same as rel1's (`checks\image-info-*-v4.0.txt`) |
| In-tree builds, release and crash-test, both boards | exit 0; compiler warnings: the old `i2s.c` line only, so no new warning (`checks\warnings-wall-wextra-v4.0.txt`) |
| `-Wall -Wextra` on `vga.c`, `pc.c` and `esp_main.c`, each board's own command, beside v4.0-base's | the same counts (3, 1 and 0 on each board), all tiny386's own (`checks\warnings-wall-wextra-v4.0.txt`) |
| The status LED in the built code | the panic path all in internal RAM, `s_level` as before; `build.sh`'s bit-bang layout check passes in all three (`checks\status-led-v4.0.txt`) |
| Host tests | 35 of 35; 35 with a long folder; 29 of 29 from the patch (22.5) |
| Mutation run, sanitizers | 19 of 19 caught; AddressSanitizer and UBSan clean; ThreadSanitizer: the allowed race only (22.5) |
| BVBE480 | 16 runs, every picture right; v4.0-base wrong only through the window at 768K and 1536K (22.6) |
| The BIOS tests | 765 passed and the 6 known failures with each of the three VGA BIOS files (22.4) |
| Windows 95 on the PC | 27 standard runs as v4.0-base; 22 native runs at the desktop; the install through the dialogs; the audits (22.7) |
| The flashing targets | as v3.9, line for line (`checks\cmake-flash-refusal-v4.0.txt`; no flash target was run) |
| The flash simulations | as v3.9's but for the app's size, hashes and version (`checks\flash-simulation-v4.0.txt`, `flash-simulation-bootswap-v4.0.txt`) |
| v3.9 to v4.0 by the app alone | 0xE20000 to 0xEFCFFF only; equal to the whole set over be5b725 (`checks\app-update-v4.0.txt`) |
| The scripts' flow | the scripts are v3.9's; the harness's 103 cases with the v4.0 images: no write reached the stub, and all 103 give v3.9's exit, WOULD WRITE lines and esptool calls (`checks\bat-harness-v4.0.txt`) |
| The card | 0 problems; every key known (22.9) |
| `SHA256SUMS` | made again: v3.9's 50 files and 14 new ones (the v4 patch and its diffstat, `win95-800x480.ini`, `vbe-reg.img`, the 8 files of `bios\`, the two research notes of v4.0), every line checked; each image folder's own list checked; `previous-v3.9\SHA256SUMS` checked, 27 files there and 23 from `v3\`; every earlier `previous-v3.x\` list checked file by file, its `vgabios.bin` now the copy in `previous-v3.9\card\tiny386\` (README) |

### 22.13 What only a board can say [unknown]

* Whether VMDisp9x behaves on the S31 as on the PC (it should: the guest sees the same emulated
  hardware [inferred]), and the install through the dialogs on the board: in the harness, at the
  board's 2 million instructions a second, it took about 13 minutes of the guest's time from the
  first dialog to the desktop at 800x480, two restarts included [inferred for the board].
* How fast Windows feels at 800x480 in 16 bits against the standard VGA driver: the guest's own
  extra work (22.7) against the drawing's saving. Ctrl+Alt+F12's speed line, and with `uart_log = 1`
  the UART's `display:` line every 10 s (pictures, write-backs and their time), say it.
* What a `fence rw,w` costs on the emulator's core, per store into video memory.
* Whether 24 and 32 bits are usable (more guest work, 1.5 or 2 times the bytes, and 13 instructions a
  changed pixel to draw).
* The guest's memory with `vga_mem_size = 768K` and `mem_size = max` (the boot console's line).

### 22.14 For Plat to decide

* The default: 16 bits at 768K (the ini as it is), or 24 or 32 bits (1152K or 1536K, a megabyte less
  for the guest at 32 bits [inferred]).
* Whether the Korvo-1 should still offer 800x600 (shown shrunk, and only at 8 bits with 768K): the
  driver lists it; removing its `MODES\...\800,600` keys in REGEDIT takes it away.
* Whether to send the bank fix, the LFB reads, the drawing from the blocks written and the 800x480
  BIOS modes upstream: that is for the other tiny386 session.
* Phases 2 and 3 of the design, if the board's numbers ask for them (22.3).

### 22.15 Scripts and logs

WSL `~/v4-work/vbe/`: `tree` (the worktree on branch `v4`), `b3.sh` (in-tree builds, `builds/`),
`h/` (the harness copy, its `bin/`, `runs/`, `runs-std-*`, `runs-bench`), `guest/` (BVBE480 and its
image), `bios/` (the kit, the builds, the three test sets), `w95/` (the offline installs), `gi/`
(the install through the dialogs: `gi18.txt` is its key script), `mut/` (the mutation run), `asan/`,
`rel/` (the stage, the two builds, `repro.sh`), `chk/` (`checks40.sh`'s output), `card/` (the
floppy's files), `dl-vmdisp/` (the driver's floppy image, used only inside copies of the Windows
image). The scripts are `s*.sh`, `g*.sh`, `w*.sh`, `p*.sh`, `m1.sh`, `rel.sh`, `repro.sh`,
`checks40.sh`, `collect.sh`, `install.sh`, `warn.py`, `mkreg.py`, `infreg.py`, `mkbat*.py`,
`mkkeys.py`, `loops.py`, `dis39.sh`. On Windows the session's scratch folder `vbe\` holds the staged `build.sh`,
the scripts as written, the batch-script harness (`bat-harness\`, run through its `go.cmd` with
PATH set to `C:\Windows\System32` and `C:\Windows` only), `hcol40.py`, the docs as edited, and the
pictures of Windows (`pics\`), kept off the package. Nothing was started that is still running.

## 23. v4.1: the two reviews of v4.0 (2026-10-23)

Two independent reviews of tag `v4.0`, each with its own lens: `REVIEW-v40-emulator-vbe-merge.md`
(the merge, the VBE drawing, the VGA BIOS, the PC evidence run again) and
`REVIEW-v40-board-and-package.md` (memory and cost on the board, the DOS board, the images and
scripts, the card and BIOS files, the docs). Neither raised a blocker or a major finding; between
them 7 minor findings (two of them the same one seen twice, and two more the same) and 11 notes.
Every finding and what became of it is in `REVIEW-RESOLUTION.md` round 6; this section is the build
record. The app calls itself `v4.1`, tag `v4.1` (f61337e) on the branch `v41` of the scratch tree,
made from `v4.0` in a worktree of its own (`~/v4-work/v41/tree`; the branch `v4` stays at `v4.0`).
v4.0's images, patches, `build.sh`, notes and the other files of its list that v4.1 changes are kept
in `previous-v4.0\`. Nothing was flashed, no serial port was opened, nothing was committed anywhere
but the scratch tree, and nothing of v4.1 has run on a board.

tiny386's master is still 4ce2e06 today [verified: `git ls-remote` of github.com/hchunhui/tiny386,
2026-10-23], so there was nothing new of its author's to take in: v4.1 rests on the same base as
v4.0, and what v4.0 took from it (22.1, `research\UPSTREAM-MERGE-v4.md`) is all there is.

### 23.1 The commits [verified: `git log v4.0..v4.1` in WSL `~/tiny386-idf-scratch/v3/tiny386`]

| Commit | What |
|-|-|
| 832f430 | `vga.c`, `vga.h`, `pc.c`: a store marked only where a picture reads the mark (23.2); chain 4 switched under VBE draws the next VBE picture whole (23.3); two comments (23.5) |
| ec1ea2a | `i386.c`, `unaligned.h`, `board_korvo1.h`: what `I386_ALIGNED_ONLY` does with the board's flags (comments only, 23.4) |
| 3fa82bd | `tests/host`: `test_vbe`'s two new parts (23.6) |
| f61337e | `esp/version.txt`: `v4.1` |

`git diff v4.0 v4.1`: 8 files, 262 lines added, 31 taken out, every file at mode 644. The patches:
`tiny386-idf-v4.patch` is `git diff upstream-20261022 v4.1` (143 files, 27,606 added, 724 taken
out, 1,092,010 bytes, SHA-256 f572ee48...), which `build.sh` applies to 4ce2e06;
`tiny386-idf-v3.patch` is `git diff pristine v4.1` (150 files, 28,236 added, 739 taken out,
1,120,017 bytes, SHA-256 7093789d...). Each, applied to a fresh clone at its base, gives the tag's
tree exactly (ef7457ec) [verified: `checks\reproducible-v4.1.txt`]. `esp/dependencies.lock` stays out
of every commit, and the scratch tree's commits carry no co-author line, as before.

### 23.2 A store marked only where a picture reads the mark (EV M1, BP 1) [verified: `vga.c`, `pc.c`; the built code as named]

v4.0 marked every chain-4 store through the VGA window, in every chain-4 mode. Only a VBE picture of
8 to 32 bits a pixel reads the marks; mode 13h and the BIOS's other 256-colour modes are drawn by
the old loop every time. So DOS games in mode 13h, what the DOS board is for, paid a mark and a
`fence rw,w` on every pixel for nothing, and `vbe_blocks = 0` left the marks and fences in place, so
the board had no way to tell what they cost. Now:

* `VRAM_WRITTEN()` in `vga.c` marks only if VBE is on and `vga_dirty_blocks` is 1;
  `LFB_WRITTEN()` in `pc.c` only if `vga_dirty_blocks` is 1 (the linear frame buffer is used
  under VBE; marking it with VBE off would be harmless, and testing VBE from `pc.c` is not worth a
  call).
* Why it is safe [inferred, as the emulator review argued; tested as below]: the only way into a
  VBE mode is a write of ENABLE, which always asks for a clear (`vbe_update_vgaregs()`), and a clear
  makes the next VBE picture whole; the test of the enable register runs on the core that writes
  it, so a store after the write is marked. A store made while VBE was off and never drawn by the
  old loop is drawn by that whole picture.
* `vga_dirty_blocks` is set before the PC is built on the board. A change of it between pictures
  (as `test_vbe` makes) is followed by a picture at the new setting before it changes back; a
  picture drawn with 0 makes the next one with 1 whole (`vga.h` says so).

What a store costs now, in the built Korvo-1 image [verified: `checks\vbe-loops-v4.1.txt`, the
three images' disassembly, counted from entry to return]:

| Store | v3.9 | v4.0 | v4.1 |
|-|-|-|-|
| chain-4 byte through the 64 KiB window, VBE off (mode 13h) | 26 | 34, one a `fence rw,w` | 31, no fence, no map store |
| the same with VBE on (a DOS program's VBE mode), `vbe_blocks` 1 | 26 | 34, one a fence | 39, one a fence |
| the same with VBE on, `vbe_blocks` 0 | 26 | 34, one a fence | 33, no fence |
| 16 bits into the linear frame buffer (Windows' driver), `vbe_blocks` 1 | 12 | 18, one a fence | 20, one a fence |
| the same, `vbe_blocks` 0 | 12 | 18, one a fence | 12 |

Mode 13h's five more than v3.9 are the test: the VBE enable register lies more than 2 KiB into the
VGA state, so its address takes a `lui` and an `add` before the `lhu`, `andi` and `beqz`. The
fence and the store into the map, the part whose cost on the S31 is [unknown], are gone. Windows'
16-bit store pays two more than in v4.0 (the load and test of `vga_dirty_blocks`), and with
`vbe_blocks = 0` exactly v3.9's 12: so `vbe_blocks` is now an A/B switch on the board for what the
marks and fences cost in Windows at 800x480 (23.15).

The guest's own work is unchanged: every PC run gives v4.0's `END` line (virtual time, instruction
count, registers) and v4.0's final picture (23.7, 23.8).

### 23.3 Chain 4 switched under VBE (EV M2) [verified: `vga.c`; test 3c]

A guest in a VBE mode of 8 to 32 bits that turns chain 4 off, stores through the window (the
odd/even or planar path, unmarked) and turns chain 4 on again before the next picture had those
stores left undrawn (the review's probe: 992 pixels). Now a write to SR04 that changes the chain-4
bit while VBE is on calls `vga_vbe_whole()`: with `VGA_TWO_CORES` it raises a count with release
(`vga_ask()`, as the clear requests do), and `vga_refresh()` reads it with acquire before the
picture and, if it moved, draws the VBE picture whole; on one core it clears `vbe_drawn` at once.
A store made with chain 4 off comes before the switch back on in program order, so the picture
that sees the count sees the store [inferred: RVWMO, as for the marks]. No known guest does this;
it costs nothing elsewhere (one test on an SR04 write).

### 23.4 What `I386_ALIGNED_ONLY` does on this board (EV M3, BP 4) [verified: WSL `~/v4-work/v41/ua`]

The merge step kept `unaligned.h` over upstream's packed accessors because the packed store was four
byte stores. That was compiled with plain rv32gc. Compiled again with the board build's own
`toolchain/cflags` (`-march=rv32imafcb_zicsr_zifencei_zcb_zcmp_zcmt_xesploop_xespv -mabi=ilp32f
-mtune=esp32s31`), the merge step's own `ua.c`:

| Function | board's flags (`unaligned_access, 1`) | rv32gc (`unaligned_access, 0`) |
|-|-|-|
| packed 16 and 32-bit loads | one `lhu`, one `lw` | two and four `lbu` |
| `ldu16`, `ldu32` (`unaligned.h`) | one `lhu`, one `lw` | two and four `lbu` |
| packed 32-bit store | one `sw` | four `sb` |
| `stu32` (`unaligned.h`) | an alignment test, each arm one `sw` | `sw` aligned, else `sb`, `sh`, `sb` or two `sh` |

So on the board both forms are one access at any address. ESP-IDF has no handler for a misaligned
load or store, only their names in the panic code [verified: grep of `components/riscv` and
`components/esp_system/port`], and the board runs Windows 95, whose code is full of misaligned
accesses, so the S31's cores carry them out themselves [inferred]. No code change: `unaligned.h`
stays as the board has run it. The comments in `i386.c`, `unaligned.h` and `board_korvo1.h` now say
this, and `research\UPSTREAM-MERGE-v4.md` 4.5 and 9 are corrected in place. (In v4.1's image
`iomem_write16()` has lost even the dead test: the compiler arranged its branches differently.)

### 23.5 Comments (EV N3, N5)

* A line past the end of video memory is drawn from the slack: black, or palette entry 0 at 8 bits,
  and it shows any bytes that a 16 or 32-bit store at the very last bytes of the last bank put
  there (the chain-4 stores do not split at the end). Said in `vga_graphic_refresh()` and
  `vga_vbe_refresh()`; not changed (`REVIEW-RESOLUTION.md` round 6, EV N3).
* An X or Y offset write does not set chain 4, the graphics mode and the CRT controller's size
  again as QEMU does: said in `vbe_write()`.

### 23.6 Host tests [verified: `checks\host-tests-v4.1.log` and the files named]

35 passed, 0 failed, at the tag; again with an output folder of 116 characters, 35 of 35; in the
tree `build.sh` made from the patch, 29 of 29 (the runs that need the scratch tree's older tags
left out). The host build's 11 warnings are v4.0's same lines.

* **`test_vbe`**, 6,832 checks on each board at the tag (v4.0's had 6,790; 6,827 and 6,785 in the
  tree made from the patch, without v4.0-base's five bank checks), two parts more:
  * *Marks only where a picture reads them*: a 320x200 picture in 8 bits with VBE turned off and
    chain 4 left on (mode 13h's case), stores of every size through the window: none may mark a
    block; then VBE on again with video memory kept (ENABLE with `NOCLEARMEM`), once after a
    picture with VBE off and once with none since VBE was on: the picture must equal the old
    loop's. And `vbe_blocks = 0` with VBE on: stores through the window and the frame buffer mark
    nothing; back to 1, every picture equals the old loop's.
  * *Chain 4 switched under VBE*: chain 4 off, 4,000 bytes through the planar path, chain 4 on, at
    640x480x8, the screen's own size at 8 and 16 bits, 640x480x32, 800x600x16 and 1024x768x8 (the
    last two shrunk on the Korvo-1), with and without a picture between: each picture must equal
    the old loop's.
  * v4.0's `vga.c` under the same test fails exactly the 9 new checks: the three mark checks and
    the six cases with no picture between [verified: WSL `~/v4-work/v41/qt`].
* **Mutation run** (`checks\mutants-v4.1.txt`): v4.0's 19 faults and 6 for v4.1 (the guard
  without its VBE test, without its `vbe_blocks` test, or upside down; `pc.c`'s guard upside down;
  the chain-4 switch asking nothing; the request not taken): 25 of 25 caught, by `test_vbe`,
  BVBE480, `test_vbe` under AddressSanitizer, or a Windows 95 run's final picture.
* **Sanitizers** (`checks\asan-v4.1.txt`): `test_vbe` on both boards under AddressSanitizer and
  UBSan, clean; under ThreadSanitizer as in v4.0, 19 reports in the same places as v4.0's, all
  the drawing's read of guest video memory against the writer thread's store (the race the design
  allows), none on the mark map or the row marks.

### 23.7 BVBE480 [verified: `checks\vbe480\results-v4.1.txt`]

31 runs in the PC harness: v4.1 on both boards at 768K, 1M, 1152K, 1536K and 2M, with the drawing in
a second thread at four sizes on each board, and with `-vbe-blocks 0` at 768K and 1M on the
Korvo-1 and 1M on the DOS board; v4.0 at the same five sizes on both boards. Every picture right
(38, 52, 55, 67 and 72 a run), none differing from a full redraw, and each v4.1 run's `END` line is
v4.0's. v4.1's checker also mends a line the harness split: on the emulator review's 1152K log,
v4.0's checker says 1 WRONG and v4.1's every one right (EV N1).

### 23.8 Windows 95 on the PC [verified: `checks\win95-pc-runs-v4.1.txt`, `checks\win95-driver-install-v4.1.txt`, `checks\bench-v4.1.txt`]

v4.1 and v4.0, each built from its tag with one harness (`~/v4-work/v41/h`, the VBE step's with the
change of 23.10), the card's BIOS files:

* **The merge step's 27 configurations** (the standard VGA driver): 27 of 27 at the desktop, each
  with v4.0's `END` line and final picture, against v4.0 built here and against the VBE step's own
  v4.0 runs.
* **The 11 native configurations** (VMDisp9x at 800x480 in 8, 16, 24 and 32 bits and the DOS
  board's 800x600 in 16): all at the desktop, the same `END` lines and pictures as v4.0 both ways.
* **The audits**: 154,878 periodic pictures held against a full redraw in 14 runs (the 6 native
  audit runs, the two `.REG` runs, three DOS board runs of 23.9 and the installed image three
  times at 768K), none differing. The install through the dialogs, 35,845 more, has v4.0's one
  difference at the first restart (the VGA's 16-colour mode going to text, the old drawing's).
* **The install through Windows' dialogs** (the VBE step's key script `gi18`), plain builds: v4.1's
  `END` line and final picture (the 800x480 desktop in High Color) are v4.0's and the emulator
  review's plain v4.0 rerun's (EV N6). The two `.REG` files' runs, plain builds this time: the
  same on v4.1 and v4.0.
* **BENCH.COM**: the same 11 marks at the same instruction counts on v4.1 and v4.0, and as the VBE
  step's v4.0 run with this VGA BIOS. (That run's log has its first mark inside the SeaBIOS
  banner's line, the harness's split line of 23.10, which is why v4.0's record lists 10.)
* **Host time of the drawing** (one run at a time; the PC's x86 compiles the fence to nothing, so
  this cannot show it): v4.1 and v4.0 within a few per cent of each other in every case, 23.6 to
  29.0 us a picture at the panel's size, 38 us for the standard VGA driver.

Two of the board review's open questions, answered here:

* **The installed image with `win95-14m.ini`'s 320K** (BP 3): the image as the install leaves it,
  shut down from the Start menu, started with 14M and 320K: Windows says "There is a problem with
  your display settings", opens Display Properties at 640 by 480 in 256 colours and asks to
  restart; accepted (the harness presses Enter), it comes back at 640x480, boxed in the middle of
  the panel (the VGA BIOS's mode 12h was the last set, so most likely the standard driver's 16
  colours [inferred]). The same image with 768K: the 800x480 desktop, 74 virtual seconds from
  power on. And the image that accepted the change at 320K, shut down and started again with 768K:
  the 800x480 desktop again, so nothing is lost but that start's size; `regedit A:\VBE480.REG` and a
  restart on that image, as `TEST-PLAN.md` 13c offers in case one ever stays at 640x480, keep it at
  800x480 too. So once the driver is in, the image is started with `win95-800x480.ini`, and
  `win95-14m.ini` and `win95-800x480.ini` say so (`TEST-PLAN.md` 13c).
* **The DOS board's sizes** (BP 6): 800x600 reaches the desktop at 960K and 1M in 16 bits, 1408K
  and 1536K in 24 and 1920K and 2M in 32, every audited picture equal to a full redraw; a
  `DEBUG_VBE` build shows the driver setting 320h by 258h at 10h, 18h and 20h bits a pixel at
  960K, 1408K and 1920K. With 768K (16 bits do not fit) the driver sets no VBE mode and Windows comes
  up at 640x480, boxed (on this first start of the offline image, with no complaint seen). `card-dosboard\tiny386\win95-800x600.ini` (new) has 1M and names the others.

### 23.9 What each depth costs (for the card) [arithmetic on `settle_memory()`; the PC runs as named; the board [unknown]]

The guest gets the whole megabytes of the pool P less `round4k(vga_mem_size + 16K)`. Plat's 13M
at 1M on the Korvo-1 puts P between 14,352 and 15,375 KiB [arithmetic on his report]; the boot
console's `Emulator memory` line gives it exactly (TEST-PLAN 13a, Photo 1). The DOS board's P is
about 187 KiB less (its larger frame buffer) [inferred].

| Board, mode | `vga_mem_size` | Guest with `mem_size = max` | Desktop on the PC (virtual s, mouse still / moving) | Drawing a changed pixel |
|-|-|-|-|-|
| Korvo-1, standard VGA, 640x480 in 16 colours | 320K | 14M if P >= 14,672 KiB | 210 / 229 | (the 16-colour path, v3.9's) |
| Korvo-1, 800x480, 256 colours | 768K | 14M if P >= 15,120, else 13M | 223 / not run | 6.5 instructions |
| Korvo-1, 800x480, High Color (16 bit) | 768K | 14M if P >= 15,120, else 13M | 220 / 300 | 3 |
| Korvo-1, 800x480, True Color (24 bit) | 1152K | 13M (12M if P < 14,480) | 229 / not run | 13 |
| Korvo-1, 800x480, True Color (32 bit) | 1536K | 13M if P >= 14,864, else 12M | 224 / 304 | 12.5 |
| DOS board, 800x600, 16 bit | 960K or 1M | 13M if P >= 14,288 or 14,352 | 222 to 224 / 300 | 3 (1:1) |
| DOS board, 800x600, 24 bit | 1408K | 13M if P >= 14,736, else 12M | 229 / not run | 13 |
| DOS board, 800x600, 32 bit | 1920K | 12M if P >= 14,224 | 224 / not run | 12.5 |

Speed: the desktop times (the harness runs the guest at the board's 2 million instructions a
second) are the guest's own work, and the depth hardly changes it at start; with the mouse moving
all the time Windows at 800x480 does about 30 per cent more than the standard VGA driver (300
against 229 virtual seconds; `research\NATIVE-VBE-DESIGN.md` 7.3 has 8 per cent with the mouse
still and 26 to 28 per cent moving). Moving a window or scrolling moves 2, 3 or 4 bytes a pixel in
the guest at 16, 24 and 32 bits, and the drawing then converts each changed row (a whole new
800x480 screen: 1.15 million instructions at 16 bits, 4.8 million at 32; 22.8). So 16 bits is the
quickest and leaves the guest the most memory (14M if the pool allows, where 24 and 32 bits leave
13M, or 12M on a small pool); 24 and 32 bits are more work on both sides. How it feels on the board
is [unknown] (23.15).

### 23.10 The harness and the checker (EV N1, N2) [verified: `checks\vbe480\headless-v4.1.diff`]

`pc.c` flushes the BIOS's debug port after every byte, and v4.0's harness wrote the emulator's
output through as it came, so a harness line could land inside a guest line ("@info[harness] beat
..."). v4.1's harness (`count_write()`) passes the emulator's output on a whole line at a time,
each thread its own line, the rest at exit; it charges the UART time per byte as before, so no run's
`END` line moves (23.7, 23.8). `checks\vbe480\vbe480check.py` also puts a split line back
together. `checks\vbe480\headless-v4.1.diff` is the whole of the harness used against the merge
step's (`-keyfile`, `FDA`/`FDB`, the line change), so the install run can be repeated from the
package; `headless-v4.0.diff` stays as it was.

### 23.11 `build.sh` and the four image sets [verified: `checks\reproducible-v4.1.txt`]

`build.sh` changed in its first comment only (the version). Two runs from nothing (WSL
`~/v4-work/v41/rel/rel1` and `rel2-second-run`): exit 0, 198 s each; all 23 files of the four sets
the same in both, ELF files and `SHA256SUMS` included, and no ELF holds its work folder's name;
both patches give the tag's tree exactly. The bootloaders, partition tables and `flash_args` of all
four sets are v4.0's byte for byte; only `tiny386.bin` and `tiny386.elf` changed (`.iram0.text`,
`.flash.text`, and the same-sized `.dram0.data`, `.flash.rodata`, `.flash.appdesc`; the same symbol
names). The update from v4.0, or from v3.6 on, is the app alone: 0xE20000 to 0xEFCFFF, and the
same bytes as the whole set written over be5b725 [verified: `checks\app-update-v4.1.txt`].

### 23.12 Sizes [verified: `checks\size-*-v4.1.txt`]

| | Korvo-1 v4.1 | v4.0 | DOS board v4.1 | v4.0 |
|-|-|-|-|-|
| `tiny386.bin` | 902,384 | 902,224 | 934,080 | 933,904 |
| Flash `.text` | 437,316 | 437,216 | 434,232 | 434,132 |
| Flash `.rodata` | 233,416 | 233,416 | 264,756 | 264,756 |
| Internal RAM used by code and static data | 249,948 | 249,880 | 252,946 | 252,878 |
| Internal RAM left | 253,796 | 253,864 | 250,798 | 250,866 |
| LP SRAM | 8,244 | 8,244 | 8,244 | 8,244 |

68 bytes more IRAM code on both boards (the tests in the window stores, which run from IRAM). The
VGA state is 8 bytes larger (the two counts), in the device window. The dual-boot `tiny386.bin`
uses 902,384 of its partition's 1,966,080 bytes, 54.1 per cent spare [verified: flash simulation].

### 23.13 Checks [verified unless marked]

| Check | Result |
|-|-|
| `build.sh` from nothing, twice | as 23.11 (`checks\reproducible-v4.1.txt`, `build-sh-v4.1-run1.txt`, `run2.txt`) |
| `esptool image-info` | every bootloader and app valid, checksum and hash; the apps say `v4.1`; rel2's the same as rel1's (`checks\image-info-*-v4.1.txt`) |
| In-tree builds, release and crash-test, both boards | exit 0; compiler warnings: the old `i2s.c` line only (`checks\warnings-wall-wextra-v4.1.txt`) |
| `-Wall -Wextra` on `vga.c`, `pc.c` and `i386.c`, each board's own command, beside v4.0's | the same counts (3, 1 and 34 on each board), all tiny386's own (`checks\warnings-wall-wextra-v4.1.txt`) |
| The status LED in the built code | the panic path all in internal RAM; `build.sh`'s bit-bang layout check passes in all three (`checks\status-led-v4.1.txt`) |
| Host tests | 35 of 35; 35 with a long folder; 29 of 29 from the patch (23.6) |
| Mutation run, sanitizers | 25 of 25 caught; AddressSanitizer and UBSan clean; ThreadSanitizer: the allowed race only (23.6) |
| BVBE480 | 31 runs, every picture right, v4.0's `END` lines (23.7) |
| The BIOS tests | not run again: they run the Linux route's own board binary with the BIOS files, and v4.1 changed neither (`card\tiny386\` holds v4.0's `bios.bin` and `vgabios.bin`, the same bytes); 22.4's results stand [inferred] |
| Windows 95 on the PC | 27 standard and 11 native runs as v4.0 to the instruction and picture; the install through the dialogs as v4.0's plain run; the audits; the 320K and DOS board runs (23.8) |
| The built code | the store counts of 23.2; the 28 loops of `vga_graphic_refresh()` v4.0's but for three offsets into the VGA state (`checks\vbe-loops-v4.1.txt`) |
| The flashing targets | as v4.0, line for line (`checks\cmake-flash-refusal-v4.1.txt`; no flash target was run) |
| The flash simulations | as v4.0's but for the app's size, hashes and version (`checks\flash-simulation-v4.1.txt`, `flash-simulation-bootswap-v4.1.txt`) |
| v4.0 to v4.1 by the app alone | 0xE20000 to 0xEFCFFF only; equal to the whole set over be5b725 (`checks\app-update-v4.1.txt`) |
| The scripts' flow | the scripts are v3.9's; the harness's 103 cases with the v4.1 images through cmd.exe, PATH `C:\Windows\System32` and `C:\Windows` only: no write reached the stub, and all 103 give v4.0's exit, WOULD WRITE lines and esptool calls (`checks\bat-harness-v4.1.txt`) |
| The card | `setup.c` and `ini.c` at the tag on a pretend card with this folder's files, `win95-800x600.ini` beside them: 0 problems, the boot menu lists five inis, every `[esp]` and `[pc]` key known; against v4.0's list only the two Windows inis' comments differ (`checks\card-check-v4.1.txt`) |
| `SHA256SUMS` | 23.16 |

### 23.14 Corrections to earlier sections

* 22.2: "The blocks are noted either way" held for v4.0 only, and v4.0's cost of the marks left out
  the chain-4 stores of mode 13h; both marked where they stand.
* 22.9: "the guest should get 14M again" at 768K only if the pool is 15,120 KiB or more (23.9);
  marked where it stands. `TEST-PLAN.md` 13a says the same.
* `research\UPSTREAM-MERGE-v4.md` 4.5 and 9: corrected in place (23.4).
* `TEST-PLAN.md` section 0: the card table said `vgabios.bin` was the Linux route's, with a
  [verified] that v4.0 made untrue, and that three inis bring up the menu.

### 23.15 What only a board can say [unknown]

* Everything in 22.13, Windows at 800x480 above all.
* What the marks and their fences cost in Windows at 800x480: the same moving window or scroll with
  `vbe_blocks = 1` and `0` (the drawing then redraws every picture whole, so a difference in the
  speed line is the marks' cost against the drawing's saving).
* Whether mode 13h DOS programs are as quick as on v3.9: they should be within the five
  instructions of 23.2 a pixel [inferred].
* The pool's size, and so the guest's memory at each depth (Photo 1).

### 23.16 `SHA256SUMS`

Made again after the docs: v4.0's 64 files, the two reviews of v4.0, `win95-800x600.ini` and the
`checks\vbe480\` files (`bvbe480.S`, `BVBE480.COM`, `vbe480check.py`, the two harness diffs); every
line checked, each image folder's own list checked, `previous-v4.0\SHA256SUMS` checked (33 files
in that folder, 31 from `v3\`), and every earlier `previous-v3.x\` list checked file by file in the
places `README.md` names, the two Windows inis' and `REVIEW-RESOLUTION.md`'s earlier copies now
in `previous-v4.0\` [verified: `checks\sums-v4.1.txt`].

### 23.17 For Plat to decide

* 22.14 stands: the default depth (16 bits at 768K, as the ini has it), 800x600 on the Korvo-1, and
  what goes upstream (the other tiny386 session's).
* If the other session sends the drawing from the blocks written upstream, 23.2's guard and 23.3's
  request belong with it, as the emulator review says.

### 23.18 Scripts and logs

WSL `~/v4-work/v41/`: `tree` (the worktree on branch `v41`), `sync.sh` (the edited sources from the
session's scratch folder `v41\src41` into the tree), `qt.sh` (`test_vbe` against v4.1's and v4.0's
`vga.c`), `commit1.sh`, `tag.sh` and `retag.sh`, `b3.sh` (in-tree builds, `builds/`), `ht.sh`, `ua/`
(23.4), `h/` (the harness copy, its `bin/`, `runs/`, `runs-std-*`, `runs-bench`; `h1.sh`, `h2.sh`,
`n1fix*.py` made it), `guest/` (the checker), `bv.sh` and `bv-one.sh` (BVBE480), `wr.sh`,
`nat-one.sh`, `ex.sh`, `ex2.sh`, `ex3.sh`, `ex4.sh`, `dbg.sh`, `p.sh` (Windows runs), `mkimg.sh` and `w95/` (the DOS
board's 24 and 32-bit images, used only in WSL), `gi/` (the install's keys and floppies), `mut/`
(the mutation run), `asan.sh`, `warn.py`, `rel.sh`, `repro.sh` and `rel/` (the stage and the two
builds), `install41.sh` and `docs-install.sh`, `checks41.sh`, `a4.sh` and `chk/`, `hgen.sh`, `dis*.sh`, `lp.sh` and `dis/`,
`collect41.sh`, `collect41b.sh`, `sums.sh`, and the `s*.sh` that only read. On Windows the session's
scratch folder `v41\` holds the edited sources (`src41\`), v4.0's (`src40\`), the docs as edited
(`docs\`), the batch-script harness (`bat-harness\`, run through its `go.cmd` with PATH set to
`C:\Windows\System32` and `C:\Windows` only), `hcol41.py`, and the pictures of Windows (`pics\`),
kept off the package. Nothing was started that is still running.

## 24. The 800x480 flicker: Plat's report and the two investigations (2026-10-23)

Plat wrote v4.1 to his Korvo-1 (the app alone), installed VMDisp9x as `TEST-PLAN.md` 13 has it,
imported `VBE480.REG` and restarted at 800x480 in 16 bits: "800x480 at 16-bit is flickering a LOT. I
don't recall it flickering previously" [reported]. Previously was Windows at 640x480 in 16 colours
with the standard VGA driver, on v3.9. His later reports, collected in
`research\FLICKER-PLAT-NOTES.md` as they came [reported]: a still desktop does not flicker; it is
motion, the mouse pointer and anything moving, "like tearing, but it does not look like normal frame
tearing"; 256 colours and 640x480 through the same driver do it too; the 16-colour standard VGA path
does not; and Windows feels slower than it was at 640x480 in 16 colours. He also asked for settings
that can be changed without pulling the card, `vbe_blocks` at least.

Two investigations followed, on the PC only (nothing flashed, no serial port opened, nothing
committed to the scratch tree), each with its own note and its own work folder:

* **Investigation 1, `research\FLICKER-REPRO.md`** (WSL `~/flicker/repro`): a copy of v4.1's PC
  harness with a model of the board's display pipeline (`-board`: `vga_task`'s pictures every 30
  virtual ms, `lcd_flush()` as the only way rows reach the panel, a clear's black through the 64 KiB
  cache), every picture held against the guest's screen converted afresh at that instant, every
  clear, DISPI and VGA register write logged, and the guest's updates of video memory timed; then a
  session of desktop use as Plat describes it (the pointer swept over empty desktop, the Start menu,
  Notepad, typing, scrolling, a window moved) on the image the install through the dialogs leaves.
  It reproduced all four of Plat's observations. Every picture equalled the reference; v4.1,
  v4.1 with `vbe_blocks = 0` and v3.9 gave the same pictures; and the pictures caught the mouse
  pointer missing or half drawn: 47 per cent of them while it moved over empty desktop, against
  2.6 per cent with the standard VGA driver.
* **Investigation 2, `research\FLICKER-AUDIT.md`** (WSL `~/flicker/audit`): the drawing and clear
  paths of v4.1 read line by line, VMDisp9x read at its release tag, and the harness with counters
  and a probe of the pixels round a parked pointer. It found the cause in the driver's source and
  in the released binary, and a one-byte change to the driver that removed it in the harness.

Both say the same thing [verified there; checked again for v4.2, 25.1]: VMDisp9x 1.2025.0.119's QEMU
driver draws the mouse pointer itself, from its VxD, and rubs it out and draws it again around every
access Windows' DIB engine makes to the screen, wherever that access is. Each time takes about
36,000 guest instructions, about 18 ms at the board's 2 million a second; tiny386 shows the guest's
screen as it is every 30 ms; so while anything is drawn a large share of the pictures catch the
pointer gone, cut off part way down, or as a black shape (the AND mask drawn, the XOR not yet). It is
also why Windows was slower: in the session, 55 to 64 per cent of the guest's instructions went on
updates of video memory while anything moved, against 1 to 13 per cent with the standard VGA driver.
None of the brief's other suspects (a clear of the panel, the clear and its picture apart, block marks
lost between the cores, tearing or an incomplete write-back, the driver writing DISPI registers as
mode changes) happened at the desktop.

The investigations' own runs, tables and scripts are in their notes and in
`research\flicker-repro\`; Plat's notes stay in `research\FLICKER-PLAT-NOTES.md`, which is added to as
he reports and so is not in `SHA256SUMS`. Section 25 is what was done about it.

## 25. v4.2: the mouse pointer of Windows' display driver, and clears with no black (2026-10-23)

v4.2 answers Plat's report on v4.1 (section 24): the flicker is the display driver's own mouse
pointer, so the cure is in the driver, one byte of it, and the package now carries a script that
makes that change and the steps to make it by hand on the board (`qemumini-pointer\`). tiny386
changes in two ways: a clear of the screen no longer shows black on the panel before the picture
after it (the brief's first requirement, and the audit's cosmetic point A3), and Ctrl+Alt+F11 turns
`vbe_blocks` the other way while the PC runs (Plat's request). Nothing else the guest sees changes:
every Windows 95, FreeDOS, BENCH, BVBE480 and DOS program run on the PC gives v4.1's results to the
instruction and the picture. The app calls itself `v4.2`, tag `v4.2` (6502063) on the branch `v42` of
the scratch tree, made from `v4.1` in a worktree of its own (`~/flicker/fix/tree`; the branch `v41`
stays at `v4.1`). v4.1's images, patches, `build.sh`, notes and the other files of its list that v4.2
changes are kept in `previous-v4.1\`. Nothing was flashed, no serial port was opened, nothing was
committed anywhere but the scratch tree, and nothing of v4.2 has run on a board.

### 25.0 In one screen

* **The cause** (25.1): VMDisp9x 1.2025.0.119's QEMU driver draws the mouse pointer from its VxD and
  rubs it out and draws it again around every access Windows' DIB engine makes to the screen,
  wherever it is; each time is about 36,000 guest instructions, 18 ms at the board's 2 million a
  second, and tiny386 shows the screen as it is every 30 ms. Checked again here in the driver's
  source and binary and in the harness: v4.2, v4.1 and `vbe_blocks = 0` give the same pictures to
  the pixel, as v3.9 did in investigation 1's run with the driver (section 24,
  `research\FLICKER-REPRO.md` 3.1; v4.2's own runs had v3.9 with the standard VGA driver only), every
  picture equal to the guest's screen at that instant [verified].
* **The cure** (25.2, 25.3): byte 0x2068 of `QEMUMINI.DRV` from 01 to 00 (`mouse_vxd = TRUE`, init.c
  207, left FALSE), which hands the pointer to the DIB engine. In the harness's session: the pointer
  whole in all 531 pictures while it moves over empty desktop (as released 269 of the 506
  counted, 47 per cent broken; the standard VGA driver 451 of 463, 2.6 per cent); pictures caught
  while the guest is part way through an update 7, 4 and 4 per cent while typing, scrolling and
  moving a window (57, 61 and 58 as released; 7, 7 and 1 with the standard VGA driver); the guest's
  share of time in updates of video memory 7, 4 and 4 per cent (56, 58 and 56); Windows keeps up
  with keys typed at 6.7 a second, where as released it was 10 s behind 13 s into the typing and
  40 s behind at session 166 s. The same at 8, 24 and 32 bits, at 640x480 through the driver and on
  the DOS board's 800x600 [verified: 25.9]. Three ways to make it: DEBUG in MS-DOS mode on the board, no card out (run in the harness
  exactly as `TEST-PLAN.md` 14 gives it); `qemumini-pointer.bat` on the card's Windows image; or a
  changed copy of the driver's floppy for a fresh install (an install from it run in the harness
  since the review, 26.2).
* **No black between a clear and its picture** (25.4): a clear is made by the picture after it,
  drawn whole over the old one, with only the rest of the screen made black, a pixel stored only
  where it differs. `test_clear` (new) fails v4.1's `vga.c` on it [verified]. The harness figure
  first given here, no clear over a picture in any v4.2 run, was true by construction: the harness
  counted a clear only after v4.1's `memset()`, which v4.2's build leaves out (the review's finding
  1). Since the review it counts every pixel a clear makes black while a picture is drawn: in every
  v4.2 run none of them is then drawn in another colour by that picture, where v4.1's `memset()`
  left 5,417 to 571,222 pixels a run black between two pictures, and a fault planted to make black
  inside the picture is caught [verified: 26.1]. Every picture equals the reference in both.
* **What it does not cure** (25.13): each move of the pointer costs the guest as much as the
  driver's own pointer did, or more (the DIB engine draws it with generic code); a pointer moved
  without a break keeps Windows busy with either. A pointer the guest never draws is the only cure
  for that, and a large piece of work (25.6).
* **Ctrl+Alt+F11** (25.5): `vbe_blocks` the other way at once, until the PC starts again, with a
  line on the console; the speed screen says which it is. Not saved to the ini (25.6).
* **Checks** (25.16): 39 host tests (four new) pass, 39 with a long folder, 31 in the tree made from
  the patch; the five in-tree builds give only the old `i2s.c` warning; `-Wall -Wextra` on the four
  changed files the same counts as v4.1's; `build.sh` from nothing twice gives the same bytes; the
  flash scripts' flow, the flash simulations and the app update as v4.1's.

### 25.1 The cause, checked again [verified unless marked]

* **The driver's source** at VMDisp9x's release tag v1.2025.0.119 (739eb72, the audit's export in
  WSL `~/flicker/audit/vmdisp`, read only): `init.c` 203 to 208 sets `mouse_vxd = TRUE` whenever the
  VxD hands over a pointer buffer; `enable.c` 146 to 169, `BeginAccess_VXD()` and `EndAccess_VXD()`,
  call `FBHDA_access_rect()` and `FBHDA_access_end()` on every access flagged CURSOREXCLUDE;
  `vxd_vbe.c` 328 to 354: `FBHDA_access_rect()` ignores its rectangle and calls
  `FBHDA_access_begin()`, which on the outermost access calls `mouse_erase()`; `FBHDA_access_end()`
  calls `mouse_blit()`; `vxd_mouse.c`: `mouse_move()` is an access begin and end around the new
  place, `mouse_blit()` a save and a draw. With `mouse_vxd` FALSE, `dibcall.c` 58 to 138 send
  `MoveCursor`, `SetCursor` and `CheckCursor` to the DIB engine, and `BeginAccess_VXD()` also calls
  `DIB_BeginAccess()` with the access's rectangle; the VxD's erase and blit then do nothing, its
  pointer never being loaded.
* **The binary**: `QEMUMINI.DRV` in the driver's floppy image (SHA-256 96eed45b..., as fetched by the
  VBE step) is 15,534 bytes, SHA-256 0b3584b1...b713d (the same as the zip's [reported: the audit]),
  and is the file the install of `TEST-PLAN.md` 13 left in `C:\WINDOWS\SYSTEM`; at 0x2064 the six bytes
  `C7 06 B8 06 01 00`, `mov word ptr [06B8h],1`, after the test of the buffer.
* **The harness** (25.9): the session of investigation 1 on v4.2, v4.1 and v3.9 (the standard VGA
  driver), and v4.2 with `vbe_blocks = 0` and with the drawing in its own thread: the same pointer
  states and updates in v4.2 and v4.1 to the picture; every picture equal to the reference.

### 25.2 The cure: one byte of the driver [verified: harness]

`qemumini-pointer\README.txt` says why and how, for Plat. The three ways:

* **A: DEBUG in MS-DOS mode, on the board, no card out.** Start, Shut Down, "Restart the computer in
  MS-DOS mode?", then `debug c:\windows\system\qemumini.drv`, `d 2160 l10`, `e 2168 0`, `d 2160
  l10`, `w`, `q`, `exit`. DEBUG loads the file at offset 100h, so file offset 2068h is 2168h. Run in
  the harness exactly so, by keys, on a copy of the image the install through the dialogs leaves
  (`base-n16.img`, the v4.1 install run's): MS-DOS mode came up, the two `d` lines showed
  `85 C0 74 06 C7 06 B8 06-01 00 B8 01 00 5A 5B 5D` and then `-00 00`, `w` said "Writing 03CAE
  bytes" (15,534), `exit` said "Windows is now restarting...", Windows came back at 800x480, and in
  the sweep that followed the pointer was whole in 531 of 531 pictures; the image afterwards held
  `QEMUMINI.DRV` with the changed file's SHA-256, e4c47e9a...8e9 [verified: run `v42-dosfix`, its
  text screens every 5 s].
* **B: `qemumini-pointer.bat` on the card's Windows image**, on the PC (25.3).
* **C: a changed copy of the driver's floppy**, by the same script, to install from as `TEST-PLAN.md`
  13 does. When v4.2 was made only the script's making of the copy was tried (25.3), not an install
  from it (the review's finding 4); since the review Windows' own install through the dialogs from
  such a copy has run in the harness, and left the changed file in the image and the pointer whole
  [verified: 26.2]. Have Disk over an installed driver was not tried [unknown].

What it costs [inferred]: the VxD's pointer is there for programs that write the screen directly
through VMDisp9x's DirectDraw and 3D parts; such a program may draw over the DIB engine's pointer.
The 2D driver disk has no 3D part, and DirectDraw was not tried.

VMDisp9x's later, unreleased source has the erase and draw taken out of the access calls and the
pointer reworked (the audit's `git diff v1.2025.0.119 767761d`), so its author has moved away from
this too. Telling him is Plat's call (25.18).

### 25.3 The script, tested [verified: WSL `~/flicker/fix/pt`, copies only]

`qemumini-pointer.ps1` (Windows PowerShell 5.1 and later; `qemumini-pointer.bat` runs it with the
execution policy set aside for that one run) finds a FAT volume in the image (the image itself, or
an MBR partition of type 01, 04, 06, 0B, 0C or 0E), finds `QEMUMINI.DRV` by name in the root or in
`WINDOWS\SYSTEM` through the directories and the FAT (FAT12, 16 and 32, long names skipped), reads it
through its cluster chain, and goes on only for the released file (or, with `-Undo`, the changed
one): 15,534 bytes, the SHA-256, the six bytes. A floppy gets a changed copy beside it; a hard disk
image is changed in place, the one byte, unless `-Out` names a copy; it asks first unless `-Yes`, and
reads the file back after. Run with Windows PowerShell 5.1.26100 against images in WSL:

| Case | Result |
|-|-|
| the driver's floppy image, `-Check` | FAT12, `QEMUMINI.DRV` as released, image offset 64,104; exit 0 |
| the same, change | `...-ptrfix.img` beside it, the file e4c47e9a...; the original untouched (`cmp`: the download's bytes); exit 0 |
| again, the copy there already | refused, nothing written; exit 1 |
| the copy, `-Check`, then change | "already changed", "Nothing to do"; exit 0 |
| the installed image (`base-n16.img` copy), through the .bat, `-Check` | FAT16 in partition 1 from sector 63, 8,192-byte clusters, offset 149,094,504; exit 0 |
| the same, change in place | the file e4c47e9a...; exit 0 |
| a FAT32 partition from sector 2048 (made with mtools) | changed; exit 0 |
| a FAT12 image, the file in 16 runs of clusters, `-Out` | the copy changed, the image not; exit 0 |
| a FAT16 image with no partition table, a long file name beside the driver | changed, then `-Undo`: the released file again; exit 0 |
| another build of the driver (one byte different elsewhere) | refused; exit 1 |
| no driver; random bytes; no such file | "no QEMUMINI.DRV", "no FAT volume", "no such file"; exit 2 |
| asked, answered n | "Nothing changed."; exit 1 |

Then from WSL: each changed image differs from its original in that one byte (`cmp -l`), mtools
reads the changed file back with SHA-256 e4c47e9a..., and the floppy copy's other four drivers are
the original's. (The harness's own `dib` images, 25.9, were made with mtools from the same changed
file, `mkdib.sh`, not with the script.) The `.bat` names Windows PowerShell by its full path,
`%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe`, so it works whatever PATH is: run
through cmd.exe with only `C:\Windows\System32` and `C:\Windows` on PATH, it checked the floppy and
refused the other build as above (a first version that called `powershell.exe` by name was not found
on that PATH, and was changed) [verified: `checks\pointer-script-v4.2.txt`].

### 25.4 A clear with no black (`vga.c`, VGA_CLEAR_AROUND) [verified: code, tests and runs named]

On the boards the screen `vga.c` draws into is the panel's frame buffer, which the panel shows all
the time. A clear (`vga_clear_screen()`) was a `memset()` of all of it, 750 KiB on the Korvo-1,
through a 64 KiB write-back cache, so most of the black reached the panel before the picture was
drawn again in the same `vga_refresh()` [inferred: the cache's size; the harness models it]. Clears
come at a VBE mode set (twice each time Windows starts its display driver, the second over a picture
with nothing changed), a change of mode or of the text's size, the screen blanked and back, and a CRT
controller write after the caller's `full_update`. Never at the desktop (24).

With `VGA_CLEAR_AROUND` (the boards' builds: BPP 16, `VGA_STORE_CHANGED`, `VGA_REDRAW_DIRTY_ROWS`,
nothing scaled) `vga_clear_screen()` stores nothing: it notes the clear, and the picture after it
makes it. Every picture after a clear is drawn whole already (the 16-colour words and a VBE
picture's blocks are not trusted after it, and the old loop draws every pixel), each pixel stored
only where it differs; `vga_clear_around()`, called by each kind of picture once its place on the
screen is known (text, the graphics modes, VBE 1:1 and centred, the shrunk pictures), makes the rest
of the screen black, a pixel stored only where it is not black already. A picture that draws nothing
(blanked, or text too large for the screen) leaves `vga_refresh()` to make all of it black.
`fb_all_dirty` is set as before, so every row still goes to `redraw()`. So the panel goes from the
old picture to the new one with nothing between, and a mode set again over an unchanged picture
stores nothing at all. (The evidence for this is the code, `test_clear` (25.8) and, since the
review, the harness's count of the black a clear stores, 26.1; the harness as first used here could
not see a v4.2 clear.) `vga_refresh()` now also gives every row to `redraw()` after a clear that no
picture followed in text mode (text too large), where v4.1 did that only when blanked.

What it costs: nothing at a picture with no clear; at a clear, a look at the pixels outside the
picture (none at 800x480; 160 by 480 at 640x480) where v4.1 stored 768,000 bytes of zeros, and the
picture's pixels are then stored only where they differ from the old picture rather than from black
[inferred: code]. 107 lines of `vga.c`, 23 of them comments.

`VGA_NO_CLEAR_AROUND` keeps the `memset()`, for the host test's reference. Tearing (the panel's
scan meeting a change) is as before: one frame buffer, no vsync, as v3.9 and v4.1; a mode set can now
be cut where the scan is for one panel frame instead of going black [inferred].

### 25.5 Ctrl+Alt+F11: `vbe_blocks` while the PC runs [verified: code, host tests]

Plat's request (`research\FLICKER-PLAT-NOTES.md`): the setting changeable without pulling the card,
at once. Ctrl+Alt+F11 now turns `vbe_blocks` the other way for as long as the PC runs; the console's
log gets "vbe_blocks = 0 now: VBE pictures are drawn in full every time and no store is marked, as
before v4.0. Ctrl+Alt+F11 changes it until the PC starts again; the ini is not changed." (or the line
for 1), and each time the speed screen (Ctrl+Alt+F12) is shown it says which it is.

* `kbd_input.c` holds up to four Ctrl+Alt hotkeys where it held one; F12 and F11 are set by
  `app_main()`. The key's press and release do not reach the PC, as with F12.
* The press goes from the USB task to `speed.c` as a request; `vga_task` takes it between two
  pictures (`speed_screen_poll()`), and a press from the boot menu's time is forgotten as F12's is.
* `vga_set_dirty_blocks()` (new) stores the setting and counts the change; `vga_refresh()` draws the
  two VBE pictures after a change whole. So stores made while it was 0 are drawn even if it goes back
  to 1 with no picture between, and a store on the emulator's core that read the old 0 just as it
  changed, and lands after the first picture, is drawn by the second [inferred for the board: a
  store lands within microseconds, a picture is 30 ms]. `pc.c`'s and `vga.c`'s stores still read
  `vga_dirty_blocks` with a plain load, as before.

### 25.6 What was not changed, and why

* **The block marks and fences** (the brief's suspect c): every store path is marked with the
  fence first and the drawing clears, fences, then reads [verified: code]; the fences make a lost
  update impossible under RVWMO [inferred]; the harness's threaded runs lost nothing [verified]; and
  a lost mark leaves stale pixels, not a blink. Untouched.
* **Picture pacing on the guest's quiet moments** (both investigations' emulator option): with the
  driver's byte changed the pictures caught mid-update are as few as with the standard VGA driver
  (25.9), so pacing would cost a third to a half of the pictures and up to 30 ms or more of lag for
  nothing; with the driver as released it cures the moving pointer but not typing or scrolling.
  Left out; only if the board still shows mid-update pictures after the driver change.
* **Pictures timed to the panel's vertical blank, or a second frame buffer**: they would take away
  the tearing that one frame buffer with no vsync has, at 640x480 as at 800x480, in v3.9 as in v4.1;
  not this flicker, and nothing the PC can try. For later, if Plat finds tearing a problem.
* **A hardware pointer** (an emulated VMware SVGA II, whose driver VMDisp9x has): the only cure for
  any driver that draws its own pointer, and a large piece of work; the DIB engine's pointer already
  behaves as the standard VGA driver's.
* **Saving `vbe_blocks` to the ini**: left out of v4.2. The reason first given here (the card holds
  the guest's disk image, open and being written while the PC runs, so rewriting an ini on it then
  is a risk to the card) is more cautious than it needs to be, as the review of v4.2 says: a small
  file written and synced on an explicit key press is ordinary FatFs use. With the pointer cured in
  the driver the setting compares two ways of drawing and fixes nothing, so it waits for Plat to ask
  (26.5); until then the ini is edited on the PC or from Linux on the dual boot.
* **The inis**: unchanged; the update from v4.1 is the app alone.

### 25.7 The commits [verified: `git log v4.1..v4.2` in WSL `~/tiny386-idf-scratch/v3/tiny386`]

| Commit | What |
|-|-|
| 411b6bf | `vga.c`: a clear is made by the picture after it, with no black between (25.4) |
| f77fc44 | `tests/host`: `test_clear` and its section 3d in `run.sh` (25.8) |
| 26da490 | `esp/main` (`kbd_input.c`, `kbd_input.h`, `speed.c`, `speed.h`, `esp_main.c`), `vga.c`, `vga.h`: Ctrl+Alt+F11 changes `vbe_blocks` while the PC runs (25.5) |
| cbabe92 | `tests/host`: the second hotkey in `test_kbd_input`; `vbe_blocks` changed while drawing in `test_vbe` (25.8) |
| 6502063 | `esp/version.txt`: `v4.2` |

`git diff v4.1 v4.2`: 12 files, 982 lines added, 19 taken out, every file at mode 644. The patches:
`tiny386-idf-v4.patch` is `git diff upstream-20261022 v4.2` (144 files, 28,569 added, 724 taken out,
1,126,566 bytes, SHA-256 d561f07c...), which `build.sh` applies to 4ce2e06; `tiny386-idf-v3.patch` is
`git diff pristine v4.2` (151 files, 29,199 added, 739 taken out, 1,154,623 bytes, SHA-256
373d1618...). Each, applied to a fresh clone at its base, gives the tag's tree exactly (f7beec4b)
[verified: `checks\reproducible-v4.2.txt`]. `esp/dependencies.lock` stays out of every commit, and
the scratch tree's commits carry no co-author line, as before.

### 25.8 Host tests [verified: `checks\host-tests-v4.2.log` and the files named]

39 passed, 0 failed, at the tag (35 in v4.1, and four new: `test_clear` and its run against v4.1's
`vga.c` on each board); again with an output folder of 116 characters, 39 of 39; in the tree
`build.sh` made from the patch, 31 of 31 (the runs that need the scratch tree's older tags left out). The
host build's 11 warnings are v4.1's same lines.

* **`test_clear`** (new, both boards): `vga.c` as the board builds it against the same `vga.c` with
  v4.1's `memset()` (`VGA_NO_CLEAR_AROUND`, symbols renamed). Ten VBE modes set again over themselves
  with video memory kept and their width written again with VBE on (800x480 at 8, 15, 16, 24 and 32
  bits, 640x480 at 8 and 16, 800x600 at 16, 1024x768 at 8 and 16, shrunk on the Korvo-1); modes 12h
  and 13h and three VBE pictures given the caller's `full_update` and then a CRT controller write; and
  three rounds of mode changes (text, 12h, 13h, VBE 640x480x8 over 13h, the screen's size in 16 bits,
  640x480x16 over it, 1024x768x8 shrunk, 800x600x32 and the screen's size in 24 bits over it,
  blanked and back, text again, the text made narrower and wider), with blanking during the mode
  sets in one round. After each of 117 pictures the two screens must be the same, every changed row
  must have gone to `redraw()`, and the new screen, made read-only before the picture (a store faults,
  the page is noted and made writable), must have no page stored into without a pixel in it changing,
  and none at all where the guest's picture is as it was. 163 checks on each board, all passed (0 of
  11,168 pages stored for nothing on the Korvo-1, 0 of 12,907 on the DOS board). v4.1's `vga.c` under
  the same test draws every picture right and stores 5,010 of 16,178 pages (Korvo-1) and 7,172 of
  20,079 (DOS board) for nothing, failing 60 and 71 checks: `run.sh` requires exactly that (exit
  status 2), 2 more passes.
* **`test_vbe`**, a part more: `vbe_blocks` to 0 and back to 1 through `vga_set_dirty_blocks()` with
  stores between and no picture, a store left unmarked after the first picture since the change, then
  0 and left there, at 8, 16 and 32 bits on both boards: every picture as the old loop draws it.
  6,851 checks on each board.
* **`test_kbd_input`**: Ctrl+Alt+F11 beside Ctrl+Alt+F12, each called for its own key and swallowed,
  F11 alone still to the PC, the hotkey taken away and set again. 221 checks.
* **Mutation run** (WSL `~/flicker/fix/mt.sh`): ten faults put one at a time into copies of v4.2's
  `vga.c` (the change count not raised, or one whole picture after it instead of two; the clear
  outside the picture not made black; none made at the end of `vga_refresh()`; the text's, the
  graphics' or the shrunk picture's place wrong; the rows not flushed after a clear; the clear not
  noted; the text not drawn in full after a clear): nine caught, by `test_vbe`, `test_clear` or both;
  the tenth (the text's `full_update`) changes nothing in the boards' builds, where every clear
  before a text picture already comes with `full_update` (it is there for a build with one core).
* **Sanitizers** (`checks\asan-v4.2.txt`): `test_vbe` and `test_clear` on both boards under
  AddressSanitizer and UBSan, clean; `test_vbe` under ThreadSanitizer: all 6,851 checks passed, with
  19 reports, every one a read of guest video memory by the drawing against the writer thread's
  store into it (the race the design allows: the store's block is marked again after it), as v4.1's
  19 in the same places; none on the marks, the row marks or the new count of changes. The run took
  69 minutes.

### 25.9 The flicker harness: the session and the sweep [verified: `checks\flicker-harness-v4.2.txt`]

Investigation 1's harness (`research\flicker-repro\`), copied to WSL `~/flicker/fix/h` with one fix
(25.10), each build compiled as the board compiles the core, with the hooks; Windows 95 at 2 million
guest instructions a virtual second; the board's display pipeline modelled (`-board`): pictures every
30 virtual ms, rows reaching the panel only through `redraw()`, a clear's `memset()` reaching it at
once; every picture held against the guest's screen converted afresh at that instant. `dib` is the
image with byte 0x2068 of `QEMUMINI.DRV` changed (made with mtools from the same file the script
writes, `mkdib.sh`).

The session (`sess/plat.txt`: idle, the pointer swept over empty desktop, still, the Start menu,
Notepad, typing, scrolling, a window moved by keys, the pointer over the window and the desktop), on
the image the install through the dialogs leaves; per segment the pictures taken while the guest was
part way through an update of video memory, and in the sweep the pointer whole, in part or not at
all (pictures with it outside the box left out):

| Run | sweep: pointer w/p/n | sweep | typing | scroll | move | sweep2 |
|-|-|-|-|-|-|-|
| v4.2, 800x480x16, driver as released | 269/132/105 (47 % broken) | 57 % | 57 % | 61 % | 58 % | 64 % |
| v4.1, the same | 269/132/105 | 57 % | 57 % | 61 % | 58 % | 64 % |
| v4.2, 800x480x16, `vbe_blocks = 0` | 269/132/105 | 57 % | 57 % | 61 % | 58 % | 64 % |
| **v4.2, 800x480x16, `dib`** | **531/0/0** | **0 %** | **7 %** | **4 %** | **4 %** | **0 %** |
| v4.1, `dib` | 531/0/0 | 0 % | 7 % | 4 % | 4 % | 0 % |
| v4.2, `dib`, `vbe_blocks = 0` | 531/0/0 | 0 % | 7 % | 4 % | 4 % | 0 % |
| v4.2, standard VGA, 640x480 in 16 colours | 451/12/0 (2.6 %) | 14 % | 7 % | 7 % | 1 % | 13 % |
| v4.1, the same | 451/12/0 | 14 % | 7 % | 7 % | 1 % | 13 % |
| v3.9, the same | 457/6/0 (1.3 %) | 10 % | 7 % | 8 % | 1 % | 6 % |

The share of the guest's time inside its updates of video memory, the same segments: as released
55, 56, 58, 56 and 64 per cent; `dib` 0, 7, 4, 4 and 0; the standard VGA driver 13, 7, 8, 1 and 9.
And the screens at 30 s steps: 13 s after the first line's keys began (session 126 s), the driver as
released had drawn 21 of its characters, about 10 s behind, and by 166 s it was 40 s behind,
still on the second line; with `dib` every key typed so far was on the screen at 128 s, and at 158 s
the scroll was done and the window's menu up, as with the standard VGA driver.

The sweep on the offline installs (`sess/sweep.txt`), every depth and size:

| Image | As released: pointer w/p/n, mid | `dib`: pointer w/p/n, mid |
|-|-|-|
| 800x480x8, 768K | 258/140/114, 55 % | 531/0/0, 1 % |
| 800x480x16, 768K (v4.1 the same) | 259/138/115, 56 % | 532/0/0, 0 % |
| 800x480x24, 1152K | 293/115/123, 51 % | 532/0/0, 0 % |
| 800x480x32, 1536K (v4.1 the same) | 274/128/110, 56 % | 531/0/0, 1 % |
| 640x480x16 through the driver | 229/130/103, 57 % | 532/0/0, 1 % |
| DOS board, 800x600x16, 1M (v4.1 the same) | 194/98/85, 56 % | 531/0/0, 0 % |

In all 28 runs (the session and the sweeps, and the DEBUG run of 25.2): every picture equal to the
reference (8,952 to 13,062 a run, every one checked where the drawing is in the run loop), no row left
unflushed, and each v4.2 run's `END`
line (virtual time, instruction count, idle count, registers) the same as v4.1's where both were run.
The clears the panel model shows over a picture: v4.1 3 a run as Windows starts (240,114 pixels gone
black; 385,647 with the standard VGA driver, 478,146 on the offline images), v4.2 none in any run;
but for v4.2 that is by construction, not a measurement: the harness counted a clear only after
v4.1's `memset()`, which v4.2 leaves out, so it could not see a v4.2 clear at all (the review's
finding 1). 26.1 has the count that can, run again on all of these.
With the drawing in its own thread (`-vgathread`, the guest paused every 10th picture for a check):
1,568 and 1,550 checks, nothing lost after one more picture, 135 and 18 pictures drawn while the
guest wrote (transient, as on the board; v4.1's run had 147).

### 25.10 DOS programs, BVBE480 and BENCH [verified: `checks\dos-programs-v4.2.txt`, `checks\vbe480\results-v4.2.txt`, `checks\bench-v4.2.txt`]

* **MODE13X.COM** (new, `checks\mode13x\`): mode 13h as games use it, a picture written straight into
  A000h, a 24x24 box moved over it 120 times on the retrace with the picture under it put back, the
  palette turned round 64 times, text mode and back, mode 13h set again over itself with video memory
  kept, mode 12h with bars through the map mask, and 13h over that; a picture after each, said on
  the BIOS's debug port. **BMODES.COM** (the Linux route's `tests\guest\bmodes.S`): every mode from 00h
  to 13h. Under FreeDOS in the flicker harness with `-board`, v4.2 and v4.1, at 256K and (MODE13X)
  768K: the same `END` lines and the same 8 and 15 pictures to the byte; every one of 421 and 433
  pictures a run equal to the reference; clears over a picture v4.1 6 and 11 a run (1,033,601 and
  2,253,286 pixels gone black), v4.2 none (by construction, as 25.9 says; 26.1 measures it: v4.2
  no pixel black between two pictures, v4.1 267,630 and 571,222).
* The flicker harness's `@shot` picture was drawn outside its panel model, so the rows it drew first
  stayed stale in the model until the guest wrote them again (74 and 19 pictures differing from the
  reference on v4.1 and v4.2 alike, before the fix): fixed in the copy (`guest_shot()` now counts as
  a board picture); a harness fault, not the firmware's, and no Windows run has an `@shot`.
* **BVBE480** (the v4.1 harness, copied to `~/flicker/fix/h41`): 21 runs, both boards, 768K to 2M, the
  drawing in a second thread at four sizes on each, `-vbe-blocks 0` at three: every picture right
  (38 to 72 a run), and every `END` line v4.1's.
* **BENCH.COM**: the same 11 marks at the same instruction counts as v4.1, the same `END` line and
  final picture.

### 25.11 Windows 95: the standard runs and the install [verified: `checks\win95-pc-runs-v4.2.txt`]

With the v4.1 work's harness (copied; the same `headless.c`), v4.2 built from the tag:

* **The merge step's 27 configurations** (the standard VGA driver, the pristine image, the card and
  stall models, three timings): 27 of 27 at the desktop, each with v4.1's `END` line and final
  picture.
* **The 11 native configurations** (VMDisp9x as released at 800x480 in 8, 16, 24 and 32 bits, the
  mouse still and moving, the DOS board's 800x600): all at the desktop, v4.1's `END` lines and final
  pictures; in the 6 audited, 70,916 periodic pictures held against a full redraw, none differing.
* **The install through Windows' dialogs** (`gi18`): v4.1's `END` line and final picture, and the
  same audit, 35,845 pictures with the one difference at picture 18,772 (t = 565.047 s, the VGA's
  16-colour mode going to text) that v4.1, v4.0 and v4.0-base all have. (The audit puts the screen
  right after each picture it checks; without that, the drawing's own pictures keep the difference
  for 51 s: 26.3.)

### 25.12 What a picture costs [verified: `checks\drawing-cost-v4.2.txt`; the board inferred]

v4.2 adds to every picture one load and test of the count of `vbe_blocks` changes (25.5); a clear
stores less than it did (25.4). The drawing's host time on this PC (x86, the v4.1 harness's `-O3`),
microseconds a periodic picture, each run alone, two rounds:

| Windows 95 | v4.2 | v4.1 |
|-|-|-|
| 800x480x16, mouse still | 26.4, 26.4 | 26.8, 26.3 |
| 800x480x16, mouse every 50 ms | 24.3, 23.9 | 24.0, 23.9 |
| 800x480x16 `dib`, mouse every 50 ms | 16.3, 16.0 | 16.0, 16.0 |
| 800x480x32, mouse every 50 ms | 28.9, 29.2 | 29.1, 29.0 |
| DOS board 800x600x16, mouse every 50 ms | 24.2, 23.8 | 24.4, 25.6 |
| standard VGA 640x480x4, mouse still | 39.6, 38.9 | 39.1, 41.2 |
| standard VGA, mouse every 50 ms | 38.3, 37.8 | 38.0, 38.6 |

Within 3 per cent either way in every case, as two runs of the same build differ. In the built
Korvo-1 image the store paths into video memory (`iomem_write8`, `16`, `32` and `_string`,
`vga_mem_write`, `16`, `32` and `_string`) are v4.1's instruction for instruction with the
addresses and offsets taken out (23, 32, 32, 55, 185, 81, 92 and 82 instructions); `vga_refresh()`,
with what the compiler put into it, is 835 instructions where it was 818, and
`vga_graphic_refresh()` 1,961 where it was 1,960 [verified: `dis42.sh`]. So the guest pays nothing
more for a store, and a picture with no clear a few instructions more.

### 25.13 What the pointer costs the guest [verified: `checks\pointer-cost-v4.2.txt`, the harness's instruction-pointer counts]

The flicker harness counts the guest's instructions by where it is at the end of each step (CS and
EIP to 256 bytes, `PCR_EIPHIST`, 25.20). The driver's pointer code is the VxD's C0FD7A00h to
C0FD80FFh (as the audit found); the DIB engine's is DIBENG.DLL's first code segment, offsets 700h
to AFFh (found by the code's bytes, which are DIBENG.DLL's from file offset D40h: byte-at-a-time
copies between the screen and a save area, and the masks). Shares of the guest's instructions:

| | As released | `dib` |
|-|-|-|
| the session's typing (mouse still) | 65.7 % | 5.5 % or less |
| scrolling (mouse still) | 68.7 % | 0.0 % |
| moving a window by keys (mouse still) | 66.4 % | 5.9 % or less |
| the pointer swept over empty desktop (50 reports a second) | 70 % | 76 % |
| starting Windows with the mouse jiggled every 50 ms (60 to 300 s) | 27 % | 69 % |

(For `dib` the whole of that DIBENG segment's 700h to AFFh is counted, so the first three are upper
bounds.) So the DIB engine's pointer costs nothing while the mouse is still, which is where the
speed of typing, scrolling and drawing comes back; but each move of it costs the guest more than
the driver's own did, about two and a half times as much: with the mouse jiggled every 50 ms from
power on, Windows reached the desktop at 609 virtual seconds with `dib`, 300 as released, 221 and
220 with the mouse left alone. While the pointer is moved without a break, either pointer keeps
the guest busy most of the time at the board's speed. That is the DIB engine's own code (a generic
16-bit pointer, written for every depth), not tiny386's; a pointer the guest never draws (25.6) is
the only thing that would take it away [inferred].

### 25.14 `build.sh` and the four image sets [verified: `checks\reproducible-v4.2.txt`]

`build.sh` changed in its first comment only. Two runs from nothing (WSL `~/flicker/fix/rel/rel1`
and `rel2-second-run`): exit 0, 194 s and 184 s; all 23 files of the four sets the same in both, ELF
files and `SHA256SUMS` included, and no ELF holds its work folder's name; both patches give the tag's
tree exactly (f7beec4b). The bootloaders, partition tables and `flash_args` of all four sets are
v4.1's byte for byte; only `tiny386.bin` and `tiny386.elf` changed (`.iram0.text`, `.dram0.data`,
`.flash.appdesc` of the same sizes, `.flash.text` and `.flash.rodata` larger; 2 symbol names gone,
`s_hotkey` and `s_hotkey_fn`, and 6 new). The update from v4.1, or from v3.6 on, is the app alone:
0xE20000 to 0xEFCFFF, and the same bytes as the whole set written over be5b725 [verified:
`checks\app-update-v4.2.txt`].

### 25.15 Sizes [verified: `checks\size-*-v4.2.txt`]

| | Korvo-1 v4.2 | v4.1 | DOS board v4.2 | v4.1 |
|-|-|-|-|-|
| `tiny386.bin` | 903,312 | 902,384 | 934,992 | 934,080 |
| Flash `.text` | 437,990 | 437,316 | 434,886 | 434,232 |
| Flash `.rodata` | 233,664 | 233,416 | 265,012 | 264,756 |
| Internal RAM used by code and static data | 249,980 | 249,948 | 252,978 | 252,946 |
| Internal RAM left | 253,764 | 253,796 | 250,766 | 250,798 |
| LP SRAM | 8,244 | 8,244 | 8,244 | 8,244 |

The internal RAM's code (`.iram0.text`, 217,166 and 220,404) is v4.1's size; its 32 bytes more are
`.bss` (the four hotkeys' table where there was one hotkey, the F11 request and the count of
changes) [inferred: the symbols]. The VGA state, in the device window, has a few bytes more (the
clear's flag, the count of changes seen and the pictures still to draw whole). The dual-boot `tiny386.bin`
uses 903,312 of its partition's 1,966,080 bytes, 54.1 per cent spare [verified: flash simulation].

### 25.16 Checks [verified unless marked]

| Check | Result |
|-|-|
| `build.sh` from nothing, twice | as 25.14 (`checks\reproducible-v4.2.txt`, `build-sh-v4.2-run1.txt`, `run2.txt`) |
| `esptool image-info` | every bootloader and app valid, checksum and hash; the apps say `v4.2`; rel2's the same as rel1's (`checks\image-info-*-v4.2.txt`) |
| In-tree builds, release and crash-test, both boards | exit 0; compiler warnings: the old `i2s.c` line only (`checks\warnings-wall-wextra-v4.2.txt`) |
| `-Wall -Wextra` on `vga.c`, `kbd_input.c`, `speed.c` and `esp_main.c`, each board's own command, beside v4.1's | the same counts (3, 0, 0 and 0 on each board), all of `vga.c`'s tiny386's own (`checks\warnings-wall-wextra-v4.2.txt`) |
| The status LED in the built code | the panic path all in internal RAM; `build.sh`'s bit-bang layout check passes in all three (`checks\status-led-v4.2.txt`) |
| Host tests | 39 of 39; 39 with a long folder; 31 of 31 from the patch (25.8) |
| Mutation run, sanitizers | 9 of 10 caught, the tenth equivalent in the boards' builds; AddressSanitizer and UBSan clean; ThreadSanitizer: the allowed race only (25.8) |
| The flicker harness | 28 runs: every picture equal to the reference; v4.2's pointer figures v4.1's; with the driver's byte changed the pointer whole and mid-update pictures as few as the standard VGA driver's (25.9); v4.2's clears, which this harness could not see, measured since the review (26.1) |
| The DEBUG way, typed in MS-DOS mode | the screens as `TEST-PLAN.md` 14 says, Windows back at 800x480, the pointer whole (25.2) |
| The script | 18 runs on copies with Windows PowerShell 5.1: each change one byte, read back through the FAT and checked from WSL; six refused or stopped as they should; the .bat with PATH `C:\Windows\System32` and `C:\Windows` only (25.3) |
| DOS programs, BVBE480, BENCH | MODE13X and BMODES: v4.1's pictures and `END` lines, v4.2's clears measured since the review (26.1); BVBE480 21 runs right; BENCH's marks v4.1's (25.10) |
| Windows 95 on the PC | 27 standard and 11 native runs and the install through the dialogs as v4.1's to the instruction and picture (25.11) |
| The drawing's cost | within 3 per cent of v4.1's; the store paths v4.1's instruction for instruction (25.12) |
| The flashing targets | as v4.1, line for line (`checks\cmake-flash-refusal-v4.2.txt`; no flash target was run) |
| The flash simulations | as v4.1's but for the app's size, hashes and version (`checks\flash-simulation-v4.2.txt`, `flash-simulation-bootswap-v4.2.txt`) |
| v4.1 to v4.2 by the app alone | 0xE20000 to 0xEFCFFF only; equal to the whole set over be5b725 (`checks\app-update-v4.2.txt`) |
| The scripts' flow | the scripts are v3.9's; the harness's 103 cases with the v4.2 images through cmd.exe, PATH `C:\Windows\System32` and `C:\Windows` only: no write reached the stub, and all 103 give v4.1's exit, WOULD WRITE lines and esptool calls (`checks\bat-harness-v4.2.txt`) |
| The card | no card file changed; `setup.c` and `ini.c` at the tag on a pretend card: 0 problems, every key known (`checks\card-check-v4.2.txt`) |
| `SHA256SUMS` | 25.19 |

### 25.17 What only a board can say [unknown]

* Whether the pointer change looks on the panel as it does in the harness: the pointer whole while
  it moves, no blinking while typing (`TEST-PLAN.md` 14b and 14d).
* How fast Windows feels with it: typing, scrolling and window drawing should be much quicker; the
  pointer moved without a break costs as much as before, or more (25.13).
* Whether anything draws wrong with the DIB engine's pointer (a trail; DirectDraw programs).
* The black frame at a mode set gone (`TEST-PLAN.md` 14f), and nothing else changed on the panel.
* What Ctrl+Alt+F11's two settings cost (the A/B of 13d, now without the card).
* Everything in 22.13 and 23.15 that is still open: the guest's real speed, the fence's cost, the
  PSRAM shared between the panel's DMA and both cores.

### 25.18 For Plat to decide

* Which way to make the pointer change (`qemumini-pointer\README.txt`: A on the board, B on the PC,
  C for a fresh install), or to keep the driver as released.
* Whether to tell VMDisp9x's author that `FBHDA_access_rect()` ignores its rectangle, so the pointer
  is rubbed out around every drawing (his later source has taken the erase and redraw out of the
  access calls already, 25.2).
* Whether a pointer the guest never draws is worth the large piece of work it is (an emulated VMware
  SVGA II, whose driver VMDisp9x has, with its hardware cursor), if the moving pointer's cost
  (25.13) matters on the board.
* 22.14 and 23.17 stand.

### 25.19 `SHA256SUMS`

Made again after the docs: v4.1's 72 files in v4.1's order, and 96 new: the three files of
`qemumini-pointer\`, the two investigations' notes (`research\FLICKER-REPRO.md`,
`research\FLICKER-AUDIT.md`) and the 88 files of `research\flicker-repro\`, `checks\mode13x\`'s two
and `checks\flicker\headless-v4.2.diff`. `research\FLICKER-PLAT-NOTES.md` is left out: it is added to
as Plat reports. Every line checked, each image folder's own list checked, `previous-v4.1\SHA256SUMS`
checked (29 files in that folder, 43 from `v3\`), and every earlier `previous-*\` list checked file by
file in the places `README.md` names [verified: `checks\sums-v4.2.txt`].

### 25.20 Scripts and logs

WSL `~/flicker/fix/`: `tree` (the worktree on branch `v42`), `h/` (the flicker harness copy with the
`@shot` fix, `PCR_EIPHIST` and `PCR_DUMPCS`; `bin/`, `img/` with the `dib` copies, `runs/`,
`summary-l1.txt` to `summary-l3.txt`), `h41/` (the v4.1 work's harness, copied, for the standard,
native, BVBE480, BENCH, install and cost runs), `sess/` (the session files and `dosfix.txt`),
`fr.sh` and `list1.txt` to `list3.txt` (the flicker runs), `std.sh` and `cmp42.sh`, `p.sh`,
`dosrun.sh` and `m13/` (MODE13X, BMODES), `eh.sh` to `eh4.sh` (the pointer's cost), `mkdib.sh` and
`drv/`, `mkpt.sh`, `vpt.sh` and `pt/` (the script's test images, WSL only), `ct.sh` and `mt.sh` (the
clear test and the mutants), `asan.sh`, `b3.sh` and `builds/`, `warn.py`, `dis42.sh`, `rel.sh`,
`repro42.sh` and `rel/`, `checks42.sh` and `chk/`, `install42.sh`, `sums42.sh`, `collect42.sh`, and
the `s*.sh` that only read. Copies of Windows (the images under `h/img/` and `pt/`) stay in WSL and
are never anywhere public. On Windows the session's scratch folder `fix\` holds `qp\` (the script
and its test driver `t1.ps1`, `t1.out`), `bat-harness\` (run through its `go.cmd` with PATH set to
`C:\Windows\System32` and `C:\Windows` only), `hcol42.py`, `docs\` (these notes as drafted) and
`pics\` (pictures of Windows from the runs, kept off the package). Nothing was started that is still
running.

## 26. The review of v4.2 (2026-10-23, night): no firmware change

`REVIEW-v42.md`, one reviewer on tag `v4.2` and this package: the cause proven on the PC, the fix at
the root, no black or stale region found, nothing slipped against v4.1, the package consistent, and
nothing that blocks giving v4.2 to Plat. Its findings are four minor faults in wording and evidence
and six notes (`REVIEW-RESOLUTION.md` round 8 has each). Every one was checked before it was acted
on, and none needed the firmware changed, so **the firmware stays v4.2** (tag `v4.2`, 6502063, tree
f7beec4b): the images, patches, `build.sh` and card files are the ones section 25 describes, made
again here byte for byte (26.4). What changed is the evidence (26.1, 26.2), one thing newly seen on
the PC that is older than v4.2 (26.3), and the docs. Nothing was flashed, no serial port was opened,
nothing was committed anywhere, and nothing of this has run on a board.
`research\FLICKER-PLAT-NOTES.md` was read again at the start, before each decision and at the end; it
has nothing new since the third report and the request for the fix release.

### 26.1 The harness made to see a v4.2 clear (finding 1) [verified: `checks\flicker-harness-v4.2-review.txt`]

The review's main point: the flicker harness counted a clear only from an event put after v4.1's
`memset()` (`instr_vga.py`), and v4.2's build (`VGA_CLEAR_AROUND`) compiles that line out, so every
v4.2 run said "clears in 0 steps" and "no clear over a picture in any v4.2 run" (25.0, 25.9, 25.10,
25.16, `TEST-PLAN.md` 14f, `REVIEW-RESOLUTION.md` round 7) was true by construction. The property
rested on the code and `test_clear`, which hold; the harness figure did not measure it.

So the harness (a copy of the one 25.9 used, WSL `~/flicker/apply42/h`; the change is
`checks\flicker\harness-v4.2-review.diff`) now counts what any clear makes black while a picture is
drawn, the worst case taken for v4.2: each such pixel reaches the panel at once (v4.1's `memset()`
as the model already had it: all but the last 40 rows, which the cache holds).

* `instr_vga.py` puts `pcr_black(d, n)` at the top of v4.2's `vga_black()`, which notes each pixel it
  is about to make black, and an event where `vga_clear_around()` takes a clear (so the harness sees
  v4.2's clears at all).
* `headless.c` notes the panel pixels v4.1's `memset()` turns black as well, and straight after each
  picture (in the run loop, the drawing's own thread and the DOS programs' `@shot` pictures alike)
  looks at every pixel noted: one that is not black in the finished picture was black on the panel
  between the old picture and the new one. Each run's log gets a line with both counts.

Planted faults first, to show it sees what it is for (two copies of v4.2's `vga.c`: every row made
black, the picture's too; and the black to the picture's right started 8 pixels inside it), with
MODE13X and BMODES (25.10): every picture of every run still equal to the reference, which is why
the reference alone could not see this; and the new count caught both faults in all three runs
(267,630, 274,064 and 571,222 pixels black between two pictures for the first, the same as v4.1's
`memset()` in the same runs; 3,656, 3,816 and 10,800 for the second). v4.2 itself: 0 in all three.

Then every run of 25.9's lists, the DEBUG run, the review's DOS box session (and v4.1 with the
standard VGA driver in it, new) and the install through Windows' dialogs (26.2), 37 runs, and a new
session at 256 colours set live (26.5):

| Run | v4.2: clears seen; pixels made black; of them not black once the picture was done | v4.1: pixels its `memset()` made black; of them not black once the picture was done |
|-|-|-|
| Plat's session, driver as released | 6; 3,654; **0** | 240,114; 5,417 |
| the same, the driver's byte changed | 6; 3,654; **0** | 240,114; 5,417 |
| the same, standard VGA driver | 7; 149,756; **0** | 385,647; 7,643 (v3.9: 243,535; 5,862) |
| the sweeps of the offline installs (8, 16, 24, 32 bits, 640x480, the DOS board; as released and changed) | 9 each; 8,103 to 147,112; **0** | 16 bits 478,146; 10,102. 32 bits 478,128; 11,750. DOS board 478,128; 10,615 |
| DEBUG in MS-DOS mode (25.2) | 11; 99,654; **0** | |
| the DOS box session, as released | 25; 626,308; **0** | 795,506; 6,070 |
| the same, the review's changed image | 22; 305,936; **0** | 1,007,658; 303,938 |
| the same, standard VGA driver | 17; 776,571; **0** | 939,966; 8,379 |
| the install through the dialogs (gi18) | 16; 260,185; **0** | 1,360,181; 376,741 |
| 256 colours set live from Display Properties, the sweep and the DOS box session at 8 bits, the driver's byte changed (26.5) | 24; 669,867; **0** | 1,006,754; 6,320 |
| MODE13X at 256K and 768K, BMODES | 7, 7, 12; 49,824, 49,824, 110,364; **0** | 1,033,601, 1,033,601, 2,253,286; 267,630, 274,064, 571,222 |

So no v4.2 run has a pixel black between two pictures, where v4.1's `memset()` left 5,417 to
571,222 a run: every pixel v4.2 makes black is black in the finished picture [verified], and by the
code it is all outside the picture (the margins of a 640x480 picture on the 800x480 panel, a text
screen's surround) [verified: `vga.c`]. Also in
every run: every picture equal to the reference (the install: 26.3), and every `END` line the same
as the fix agent's or the review's run of the same configuration (v4.1 with the standard VGA driver
in the DOS box is new); the harness's analysis table the same in all 168 rows of the runs that do
not depend on host timing (the 26 rows of the three runs with the drawing in its own thread differ
in their picture counts, as such runs do). With the drawing in its own thread, nothing lost after
one more picture, with the guest paused (119, 20 and 17 pictures drawn while the guest wrote, which
the next one put right: `a42-n16-thread`, `a42-n16dib-thread`, `d42-ps1-thread`) [verified].

### 26.2 Route C, an install from the changed floppy (finding 4) [verified: `checks\route-c-v4.2.txt`]

When v4.2 was made only the script's making of the changed floppy was tried, not an install from
it. Now, all on copies:

* The package's `qemumini-pointer.bat` on a copy of the driver's floppy (SHA-256 96eed45b...), with
  Windows PowerShell 5.1 through cmd.exe and `-Yes`: "Done", the copy
  `vmdisp9x-1.2025.0.119b-driver-2d-ptrfix.img` beside it (4cb72027...), the original untouched.
  `cmp -l`: one byte, image offset 64,104, 01 to 00. mtools: 17 files, all with the original's
  SHA-256 but `QEMUMINI.DRV`, which is e4c47e9a.... `-Check` on each says "as released" and
  "already changed".
* Windows' own install through its dialogs as `TEST-PLAN.md` 13 has it (the v4.1 work's key script
  `gi18s` with its shut down 60 s later: Have Disk from B:, the QEMU adapter, a restart,
  `VBE480.REG` from A:, a restart, then shut down from the Start menu), v4.2, 14M, 768K,
  `vbe-reg.img` in A: and that copy in B:: at the
  desktop at 800x480 in 16 bits, and `C:\WINDOWS\SYSTEM\QEMUMINI.DRV` in the image afterwards is the
  changed file, e4c47e9a.... The same install from the released floppy leaves 0b3584b1... and an
  image byte for byte `base-n16.img`, the v4.1 install run's image every session of 25.9 started
  from: v4.2 makes v4.1's install exactly.
* Plat's session on the image route C left: the pointer whole in 532 of 532 sweep pictures, the
  pictures caught mid-update 6, 4 and 3 per cent while typing, scrolling and moving a window, every
  one of 9,220 pictures equal to the reference: as with the byte changed in place (25.9).
* Two things seen on the way. Windows writes to a floppy it reads: after the install the floppy
  image differs from what went in at bytes 3 to 10 (the boot sector's name) and in six of the root
  directory's last-access dates, so its SHA-256 changes; the files in it do not. And the first try
  pressed the shut down keys at 1,000 s as the v4.1 work's script does, and Windows did not act on
  them (its desktop at 1,020 s has nothing open) [verified], why [unknown] (most likely still busy
  after that start); so the image was not shut down, and ScanDisk stopped every session started from
  it. With the keys 60 s later Windows shut down and the image was clean. On the board this is Plat
  shutting Windows down from the Start menu, as `TEST-PLAN.md` asks.

### 26.3 Newly seen, older than v4.2: the text screen at a restart from 16 colours [verified on the PC; the board inferred]

The install runs (26.2) are the first with the board's display model through Windows' restart from
640x480 in 16 colours. There, from 565 to 616 virtual seconds ("Windows is now restarting...",
"Please wait while Setup updates your configuration files..."), the drawing's pictures differ from
the guest's screen: where the guest's text screen is black, the panel shows light grey, every
character cell filled, with a dark line between the cells; the text itself is drawn right. 1,706
pictures, 51 virtual seconds; nothing black where the guest's screen is not. Then Windows sets its
16-colour mode again and every picture is right.

* v4.2 and v4.1 the same, to the picture (both the changed and the released floppy) [verified].
* It follows the VGA BIOS, not v4.x's drawing: v3.9 with its own VGA BIOS has none of it there (8
  pictures, a quarter of a second, at a later reboot); v3.9 with the v4 VGA BIOS
  (`card\tiny386\vgabios.bin` since v4.0) has the same 1,706 pictures; v4.2 with v3.9's VGA BIOS has
  none [verified: runs `c39-gi`, `x39-gi-bios42`, `x42-gi-bios39`].
* What happens: the text picture drawn as the mode is set equals the guest's screen at that
  instant, and the next one is 255,872 pixels off with only 128 of the panel's pixels changed
  [verified: the run's picture log]. So the guest's screen changed under unchanged characters, which
  is a new font in plane 2: the v4 BIOS loads it after the mode's first text picture was drawn from
  what the 16-colour picture left there [inferred], and the text drawing draws a cell again only
  when its character or attribute changes (`last_ch_attr`) or the whole screen is drawn [verified:
  `vga.c`]; a new font alone is neither, so blank cells keep the old glyphs until Windows changes
  mode [inferred].
* The audit of v4.0, v4.1 and v4.2 (25.11) saw this as one picture "gone at the next picture"; that
  was the audit itself putting the screen right after each check.
* On the board [inferred]: the same at a restart from Windows in 16 colours (after the driver's
  install, as section 13 has it, and whenever Windows runs with the standard VGA driver), up to a
  minute of grey behind the text. Not seen in any run at the 800x480 desktop, in the DOS box
  sessions, at MS-DOS mode from 800x480, or at Windows' shut down from 800x480 [verified].
* Seen again by the review of v4.3 (28.3): at Windows' shut down from 640x480 in 16 colours, the
  text area wrong for 8 pictures (a quarter of a second) on v4.3 and on the branch `perfport`
  alike, 4 on v4.2, nothing black. The same fault, briefer; older than v4.3 [verified on the PC].
* Not v4.2's change and not the flicker; nothing changed for it here. The cure belongs in the
  drawing (a text screen drawn whole when the font in plane 2 is written) and is for Plat to ask for
  in a later version (26.6). `TEST-PLAN.md` 14 asks him to say if he has seen it.
* v4.4 makes that cure (29.3): the install's 1,706 pictures and 28.3's 8 at the shut down are
  gone [verified on the PC; the board unknown].

### 26.4 Everything run again [verified: the `checks\...-v4.2-review...` files]

No commit was made, so the tag, the tree and the images are v4.2's; each was checked again:

| Check | Result |
|-|-|
| The patches | `git diff upstream-20261022 v4.2` and `git diff pristine v4.2` made again: the package's patches and diffstats byte for byte |
| `build.sh` from nothing (a fresh clone of 4ce2e06, the package's patch, ESP-IDF in a clean environment) | exit 0, 182 s; all 23 files of the four image sets the package's byte for byte, the same list; the only compiler warning the old `i2s.c` line (`build-sh-v4.2-review.txt`) |
| Host tests, at the tag (a clone of the scratch repository) | 39 of 39; again with an output folder of 109 characters, 39 of 39; the 11 host warnings v4.2's (`host-tests-v4.2-review.log`, `host-tests-v4.2-review-long-out.log`) |
| `test_vbe` and `test_clear` under AddressSanitizer and UBSan, both boards; `test_vbe` under ThreadSanitizer | AddressSanitizer and UBSan: all 6,851 and 163 checks passed on each board, no report. ThreadSanitizer: all 6,851 checks passed, 19 reports, every one the drawing's read of guest video memory against the test's writer storing into it (`vbe_row`, `vga_pixel`, the writer), the race the design allows, at the places of 25.8's 19; none on the marks or the counts; 86 minutes (`asan-v4.2-review.txt`) |
| The flicker harness | 26.1 to 26.3 and 26.5: 37 runs, then 14 for route C and the text screen (5 of them from images not shut down, which ScanDisk stopped, and 1 whose keys went astray: not evidence, 26.8), then 4 at 256 colours set live (the 2 as released lost the DOS box's keys); the new count of black 0 in every v4.2 run; every `END` line the earlier runs' where there is one |
| DOS programs | MODE13X and BMODES on v4.2 and v4.1: v4.1's `END` lines and shots, every picture equal to the reference; the planted faults (26.1) (`dos-programs-v4.2-review.txt`) |
| Windows 95 in the v4.1 work's harness | the 27 standard configurations, the 11 native ones, BVBE480's 21 runs, BENCH and the install through the dialogs: every `END` line and final picture v4.1's and the fix agent's v4.2 run's; BVBE480 every picture right; BENCH's 11 marks v4.1's (`win95-pc-runs-v4.2-review.txt`) |
| The pointer script | `-Check` and the change on copies of the floppy (26.2) |
| `SHA256SUMS` | 26.7 |

The status LED, flash simulations, flashing targets, the scripts' flow and the card check were not
run again: their inputs (the images, the scripts, the card files) are byte for byte the ones 25.16
checked [verified: `SHA256SUMS`].

### 26.5 The review's notes, and what became of them

* **5, v4.2 alone changes nothing in the pointer**: `TEST-PLAN.md` 14a now says so, and that 14c is
  the cure.
* **6, the DIB engine's pointer costs more per move**: 14d already asked Plat to note whether it
  matters; it now says the only real cure, if it does, is an emulated hardware pointer.
* **7, the DOS box at 8 bits not run**: run now, the way Plat got to 256 colours (his third
  report: changed live, no restart). A new session (`sess\depth8.txt`, in
  `checks\flicker\lists-v4.2-review.txt`'s list 8) on the image route C left (26.2): Display
  Properties, the colour palette from High Color to 256 Color, OK, "Apply the settings without
  restarting", OK, keep the setting; then the pointer swept over empty desktop; then the review's DOS
  box session (a window, full screen, 40 columns and back, the window again, four quick switches,
  closed). The VBE mode went to 800x480 in 8 bits at 264 s and stayed there, the full screen and
  quick switches as the session has them [verified: the run's picture log]. Every one of 19,030
  pictures equal to the reference; v4.2 24 clears seen and no pixel black between two pictures,
  v4.1 the same `END` line with 6,320 [verified: 26.1's table]. The pointer whole in every sweep
  picture: the analysis table says 537/94/0, but the 94 are the plain arrow (105 pixels in the box)
  beside Windows' busy arrow (245), which is the commoner just after the change and so taken as
  "whole"; panel and reference agree on every one [verified: `checks\flicker\ptr-v4.2-review.py`,
  `checks\flicker-harness-v4.2-review.txt` section 10]. The same keys on the image
  as released changed the depth too, but Windows, slower with its own pointer, lost the DOS box's
  keys, so those two runs (v4.2, v4.1) are not counted.
* **8, two board sights that are Windows' own**: 14d item 5 (the desktop repainted for seconds after
  a full-screen DOS box; up to a second of black at each switch while Windows has the picture off).
* **9, Ctrl+Alt+F11 a flag, not a count**: left, with the reason: the hotkey is called once for each
  press, on the key going down, never from the key repeat (`kbd_input.c` returns before the repeat
  is set up), and `vga_task` takes the request once a picture, every 30 ms; two presses in one
  picture would need F11 down, up and down again within 30 ms [inferred: not a human rate]. The
  console is held only after a fatal message or with the PC switched off, when it stays for good. A
  count would change nothing Plat can do and would need a new version.
* **10, saving `vbe_blocks` to the ini**: the reason in 25.6 is worded again as the review asks (more
  cautious than needed); whether to add it is Plat's call.

### 26.6 For Plat to decide

* The four points of 25.18 stand.
* Whether the text screen at a restart from 16 colours (26.3) is worth a later version: the drawing
  would draw a text screen whole when its font is written. Small in code, but in `vga.c`'s store
  path or its text picture, so it wants its own tests and review.
* Saving `vbe_blocks` to the ini (26.5).

### 26.7 `SHA256SUMS`

Made again after the docs: v4.2's 168 files in v4.2's order, and 2 more, `REVIEW-v42.md` (as every
earlier review is) and `checks\flicker\harness-v4.2-review.diff`. `research\FLICKER-PLAT-NOTES.md`
stays out, as before. Every line checked, each image folder's own list, and every `previous-*\`
list file by file in the places `README.md` names [verified: `checks\sums-v4.2-review.txt`].

### 26.8 Where things are

WSL `~/flicker/apply42/`: `h/` (the harness copy, `harness-apply42.diff` being the change, `bin/`
with `r42`, `r41`, `r39`, `r42d`, `r41d` and the two planted faults, `runs/`, `summary-l1.txt` to
`summary-l8.txt`), `ar.sh` and `l1.txt` to `l8.txt` (the runs; `sess/depth8.txt` and its segments
for list 8, found with the trial runs of `lt.txt`, `t42-depth` and `t42-depth8`, which are not
evidence; `ptr.py`, the pointer's pixels in the box; the `c42-n16ptr`, `c42-n16rel`,
`c42-ptr-dosbox`, `c42s-plat` and `c42s-dosbox` runs started from images that were not shut down
and stopped at ScanDisk, and `c42r-dosbox`'s keys went to the desktop instead of a DOS box: none of
them is evidence), `tab.sh`, `cmptab.sh`, `gd.py` (the pictures that differ, grouped), `dos.sh` and
`t-m1`, `t-m2` (the planted faults), `rc.sh`, `fd2.sh` and `fd3.sh` (route C's images and
floppies read), `collect.sh` and `sums.sh` (the checks files and `SHA256SUMS`), `drv/` (the two floppies),
`fd/` (each run's floppy copies), `t-v4.2`, `t-v4.1`, `t-v3.9` (`git archive` of the tags),
`clone/` and `ht/` (the host tests), `asan/`, `rel/` (`build.sh` from nothing), `h41/` and
`std42.sh` (the standard Windows runs), `patches/`. Copies of Windows stay there, never anywhere
public. On Windows the session's scratch folder `a42\` holds the scripts as written, `routec\` (the
two floppy images and the script's output) and `pics\` (pictures of Windows from the runs, kept off
the package). Nothing started for this is still running.

## 27. v4.3: the information port in the release (2026-10-24)

v4.3 is v4.2 with the information port at I/O 0F1F8h to 0F1FFh merged in: the port that T386MON,
Plat's monitor for Windows 95 on tiny386 (`..\t386mon\`), reads for the panel's frames a second,
the guest's load, the drawing, memory, the disk, the timer and input. The port was made on the
branch `perfport` from v4.1 (`..\t386mon\PROTOCOL.md`, version 1.1, reviewed in
`..\t386mon\REVIEW.md`) and is merged as it was; v4.2's flicker fix (a clear with no black,
Ctrl+Alt+F11 for `vbe_blocks`) stays as it was beside it. Nothing else changes for the guest: every
PC run of v4.2 repeated here gives v4.2's results to the instruction and the picture. The app calls
itself `v4.3`, tag `v4.3` (0f87c1c) on the new branch `v43` of the scratch tree, made from tag
`v4.2` in a worktree of its own (`~/v43/merge/tree`; the branches `v42` and `perfport` stay where
they were). v4.2's images, patches, `build.sh` and docs are kept in `previous-v4.2\`. Nothing was
flashed, no serial port was opened, nothing was committed anywhere but the scratch tree, and
nothing of v4.3 has run on a board. `research\FLICKER-PLAT-NOTES.md` has nothing new since the
review of v4.2 (unchanged since 2026-10-23, 18:25).

### 27.0 In one screen

* **The merge** (27.1): `git merge perfport` into v4.2 stopped at the two one-line conflicts the
  trial merge had foreseen (`esp/version.txt`, and the list of renamed functions in
  `tests/host/run.sh`, where each side adds one); both sides kept. Git merged `pc.c`, `vga.c`,
  `vga.h` and `speed.c` by itself, and every file the merge took from the branch adds to v4.2
  exactly the branch's lines [verified: `checks\merge-v4.3.txt`].
* **The port and Ctrl+Alt+F11** (27.3): the port reads `vga_dirty_blocks` itself at each snapshot,
  the variable v4.2's Ctrl+Alt+F11 changes, so it gives the setting as it is, with or without a
  picture since; nothing in the firmware needed changing for it. A new host test,
  `test_port_blocks`, shows it with `perfport.c` and `vga.c` as each board builds it, and the two
  whole pictures v4.2 draws after each change counted; seven planted faults, each caught; and in
  Windows on the harness T386MON's "VBE drawing" line follows three changes [verified].
* **Checks** (27.13): 45 host tests pass (v4.2's 39, the branch's 4, the new test on both
  boards); every picture of 28 flicker harness runs equals the reference and no pixel is black
  between two pictures, every `END` line v4.2's; the 27 standard and 11 native Windows runs, BENCH
  and the install through the dialogs give v4.2's `END` lines and final pictures; T386MON 1.2 on
  v4.3 finds the port, its values equal the harness's own counts, its tests complete at 640x480 in
  16 colours and at 800x480 in 16 bits with the pointer change, and its log is the branch's byte
  for byte; `build.sh` from nothing twice gives the same bytes; no new compiler warning; the flash
  scripts' flow, the flash simulations and the app update as v4.2's.
* **The cost** (27.9, 27.11): the store paths into video memory are v4.2's instruction for
  instruction; each picture is timed and counted (the drawing's host time within 3 per cent of
  v4.2's); a port access adds a few compares to `pc.c`'s port switches; 664 bytes more internal
  RAM, 3,280 bytes more app.

### 27.1 The merge [verified: `checks\merge-v4.3.txt`]

The trial merge of the T386MON step (`..\t386mon\PROTOCOL.md` 10) said what to expect, and the
merge did exactly that. In the worktree on the new branch `v43` at tag `v4.2` (6502063),
`git merge --no-ff perfport` (6e9a6f1; the merge base is v4.1, f61337e) merged 16 files by itself
(the 13 only the branch changed, and `vga.c`, `vga.h` and `esp/main/speed.c`, which both sides
changed; "15" here until the review of v4.3, 28.2) and stopped at two conflicts:

* `esp/version.txt`: `v4.2` against `v4.1-perfport`. Left at `v4.2` in the merge; the version is
  its own commit afterwards (27.2), as in every earlier round.
* `tests/host/run.sh`, the list of functions `vga_rename()` renames for the reference builds of
  `vga.c` (section 3c, and since v4.2 section 3d too): v4.2 added `vga_set_dirty_blocks`, the branch
  `vga_get_info`. Both kept, on one line. Without either, a reference build would define the same
  symbol twice and the test would not link.

Then each file the merge took from the branch was held against the branch's own change: for every
one but those two, the lines the merge added to and took from v4.2 are exactly the lines the branch
added to and took from v4.1, `vga.c`, `vga.h` and `esp/main/speed.c` among them, which v4.2 had
changed too. The places the T386MON step named as most
likely to meet v4.2 were read again in the merged tree: `pc_vga_step()` draws through
`perfport_redraw()` and times the picture, as on the branch; the port's counts in `vga.c`'s VBE
drawing sit beside v4.2's clear (`vga_take_marks()`, `vga_vbe_refresh()`, the shrunk path) and count
v4.2's whole pictures after a `vbe_blocks` change as whole, since those set `vbe_drawn` false
[verified: `vga.c`]; `speed.c` has both the port's `perf_fill()` and v4.2's Ctrl+Alt+F11 handling.
The host tests at the merge commit: 43 of 43 [verified: run `ht/merge` in WSL]. `perfport.c`,
`perfport.h` and the port's two tests in v4.3 are the branch's byte for byte, and
`..\t386mon\perfport.h` is v4.3's [verified].

### 27.2 The commits [verified: `git log v4.2..v4.3`]

| Commit | What |
|-|-|
| c044193 | the merge of `perfport` (e24e7b2, 4940f13, 32191dc, 1ea0539, 6e9a6f1: the port, the board's values, the host tests, the port's 1.1, the branch's version), the two conflicts resolved (27.1) |
| b267cd9 | `tests/host`: `test_port_blocks.c` and its part of section 8b in `run.sh` (27.3) |
| 0f87c1c | `esp/version.txt`: `v4.3` |

`git diff v4.2 v4.3`: 19 files, 1,767 lines added, 4 taken out, every file at mode 644 [verified:
`checks\reproducible-v4.3.txt`]. No firmware line was written for v4.3 beyond the merge's. The
commits carry the scratch tree's own identity and no co-author line, as before; `esp/dependencies.lock`
stays out of them. (The test commit was made once with a line of `run.sh` joined by a quoting slip in
the script that wrote it; it was made again with the line as the file's style has it before the tag
was set, and nothing had been built from the first one but the in-tree builds, whose sources did not
differ [verified: `git diff` of the two].)

The patches: `tiny386-idf-v4.patch` is `git diff upstream-20261022 v4.3` (149 files, 30,332 added,
724 taken out, 1,205,715 bytes, SHA-256 f2e010f5...), which `build.sh` applies to 4ce2e06;
`tiny386-idf-v3.patch` is `git diff pristine v4.3` (156 files, 30,962 added, 739 taken out,
1,233,772 bytes, SHA-256 5d289edd...). Each, applied to a fresh clone at its base, gives the tag's
tree exactly (284a9331) [verified: `checks\reproducible-v4.3.txt`].

### 27.3 The port and Ctrl+Alt+F11 [verified: code, host test, mutants, the harness]

The brief asked that the port report the current `vbe_blocks` after a Ctrl+Alt+F11. It does, with
no change: `snapshot()` in `perfport.c` puts `vga_dirty_blocks` into `VBE_BLOCKS_ON` each time a
guest asks for a snapshot, and v4.2's `vga_set_dirty_blocks()`, which Ctrl+Alt+F11 calls from
`speed_screen_poll()` between two pictures, stores that same variable [verified: `perfport.c`,
`vga.c`, `speed.c`]. No copy of the ini's value is kept anywhere on the way. The port's read is a
plain load on the emulator's core of a word the drawing's core stores, as `pc.c`'s and `vga.c`'s own
reads of it have been since v4.2 (25.5) [verified: code]; a snapshot a moment before the change's
store simply gives the old value [inferred].

`test_port_blocks` (new, both boards; `tests/host/test_port_blocks.c`, section 8b of `run.sh`)
includes `perfport.c` and links `vga.c` as the board builds it (test 3c's object), draws each
picture as `pc_vga_step()` does with the port (through `perfport_redraw()`, then
`perfport_picture()`), and turns `vbe_blocks` as `speed.c` does (`vga_set_dirty_blocks(!vga_dirty_blocks)`):
at 800x480 in 16 bits, the setting read through the port's ports at once after each change with no
picture between, and after pictures; a snapshot's value held until the next snapshot; with
`vbe_blocks` 0, no VBE picture counted and the changed rows still sent; back to 1, the two pictures
after the change counted whole and the third drawn from the blocks; and 20 changes, one a picture,
each read at once. 18 checks on each board, all passed [verified: `checks\host-tests-v4.3.log`].

Seven faults, one at a time in copies (`checks\mutants-v4.3.sh`): the port giving a constant 1, or
the value of its first snapshot; `vga_set_dirty_blocks()` not storing; no whole picture, or one,
after a change; whole pictures not counted; the change not counted. Each fails the test
[verified: `checks\mutants-v4.3.txt`].

In Windows on the harness (27.8), with the harness's new `-f11` doing what `speed.c` does at 400,
430 and 460 virtual seconds: the port read by the harness straight after each change has the new
value, and T386MON's Log now at 390, 415, 445 and 475 s wrote "blocks on", "off", "on", "off", at
800x480 in 16 bits and at 640x480 in 16 colours alike [verified: `checks\t386mon-v4.3.txt` 4].

### 27.4 Host tests, sanitizers [verified: `checks\host-tests-v4.3.log` and the files named]

45 passed, 0 failed, at the tag: v4.2's 39, the branch's 4 (`test_perfport`, `test_vga_info` on each
board, `perfport.h` as a guest's C89 header; the disk test's new sector count is inside an existing
test), and `test_port_blocks` on each board; the same lines as v4.2's log for every test they share.
Again with an output folder of 114 characters, 45 of 45; in the tree `build.sh` made from the patch,
37 of 37 (the runs that need the scratch tree's older tags left out). The host build's 11 warnings
are v4.2's same lines.

AddressSanitizer and UBSan (`checks\asan-v4.3.txt`): `test_vbe` (6,851 checks) and `test_clear`
(163) on both boards as v4.2 ran them, and the port's `test_vga_info` (14), `test_port_blocks` (18)
on both boards and `test_perfport` (291): all passed, no report. ThreadSanitizer on `test_vbe`:
all 6,851 checks passed, with 19 reports, every one the drawing's read of guest
video memory against the test's writer storing into it (`vbe_row`, `vga_pixel`, the writer), the race
the design allows, at v4.2's 19 places, as many at each (1, 2, 2, 6, 4 and 4; the line numbers moved
by the port's lines above them); none on the marks, the row marks, the change count or the port's VBE
counts, which only the drawing writes. The run took 76 minutes.

### 27.5 The flicker harness [verified: `checks\flicker-harness-v4.3.txt`]

apply42's copy of the flicker harness (26.1: the board's display model, every picture held against
the guest's screen converted afresh, every pixel a clear makes black checked once the picture is
done), with its `build.sh` taught to compile `perfport.c` with `TINY386_PERFPORT` as the board does
(`checks\flicker\build-v4.3.diff`), so that each picture goes through `perfport_redraw()` and is
timed as on the board. apply42's v4.2 runs of its lists 1 and 8, each made again with v4.3 (28 runs,
`checks\flicker\l1-v4.3.txt`): Plat's session on the image as released and with the driver's byte
changed, the standard VGA driver, `vbe_blocks = 0`, the drawing in its own thread, the sweeps at 8,
16, 24 and 32 bits, 640x480 through the driver and the DOS board's 800x600, DEBUG in MS-DOS mode,
the DOS box sessions, the install through the dialogs from the changed and the released floppy, and
256 colours set live.

* Every `END` line the same as v4.2's run of the same configuration (28 of 28).
* Every picture equal to the reference in every run but the two installs, which differ in 1,706
  pictures each, exactly as v4.2's do (the grey text screen at the restart from 16 colours, 26.3).
* The disk images the DEBUG run and the two installs left are v4.2's runs' byte for byte
  (`QEMUMINI.DRV` in them changed by DEBUG, changed from the changed floppy, as released from the
  released one).
* No pixel black between two pictures in any run: of the pixels a clear made black (3,654 to
  862,234 a run), none was drawn in another colour by the picture after it.
* Every "board:" line of the 25 runs not threaded is v4.2's run's (pictures, the reference, the
  clears and the black, the guest's updates of video memory, the blips); the analysis table's 129
  segment rows of those runs are apply42's (the pointer whole, in part or not at all; pictures
  mid-update; clears over a picture; blank pictures).
* The three runs with the drawing in its own thread: nothing lost after one more picture with the
  guest paused (120, 17 and 19 pictures drawn while the guest wrote, which the next one put right).

### 27.6 DOS programs [verified: `checks\dos-programs-v4.3.txt`]

MODE13X at 256K and 768K and BMODES under FreeDOS in the same harness with `-board`: v4.2's `END`
lines, v4.2's 8, 8 and 15 shots to the byte, every one of 421, 421 and 433 pictures equal to the
reference, no pixel black between two pictures, every board line v4.2's.

### 27.7 Windows 95 on the PC [verified: `checks\win95-pc-runs-v4.3.txt`, `checks\vbe480\results-v4.3.txt`, `checks\bench-v4.3.txt`]

In my copy of the T386MON step's harness (the v4.1 work's harness with the port's host part; v4.3
built with the port), each run's `END` line (the emulator's; the harness then reads the port at the
end as a guest would) and final picture against apply42's v4.2 runs and v4.1's own:

* **The 27 standard configurations**: 27 of 27 at the desktop, each with v4.2's and v4.1's `END`
  line and final picture.
* **The 11 native configurations** (VMDisp9x at 800x480 in 8, 16, 24 and 32 bits, the mouse still
  and moving, the DOS board's 800x600): v4.2's and v4.1's; in the 6 audited, 70,916 periodic
  pictures held against a full redraw, none differing.
* **BENCH**: the 11 marks at v4.2's instruction counts, v4.2's `END` line and final picture.
* **The install through Windows' dialogs** (`gi18`): v4.2's `END` line and final picture; the audit
  35,845 pictures with the one difference v4.1 and v4.2 have (25.11).
* **The port read at each run's end**: magic and version 1.1 in all 61 runs.
* **BVBE480**: 21 runs, every picture right; but every `END` line differs from v4.2's. Not v4.3's
  doing: this harness copy puts a dialog's Enter among a key file's keys in time order (the port
  step's change), and BVBE480's keys come a little differently. v4.2 built in the same harness copy
  gives v4.3's `END` lines in 21 of 21 runs, and v4.3 built in the v4.1 work's harness (its
  `build.sh` taught `perfport.c`, nothing else: `checks\win95-v4.3\build-v4.3.diff`) gives apply42's
  v4.2 `END` lines in 21 of 21; 42 of 42 runs every picture right [verified: `bvab-v4.3.sh`].

### 27.8 T386MON 1.2 in Windows 95, and PPTEST [verified: `checks\t386mon-v4.3.txt`]

T386MON 1.2 (`..\t386mon\`, unchanged) from its own floppy, on the same harness copy with v4.3, the
images of the T386MON step: the pristine image's read-only copy (the standard VGA driver, 640x480 in
16 colours) and its install of VMDisp9x at 800x480 in 16 bits, as released and with the one-byte
pointer change; each run on a fresh copy, deleted after. The harness copy has one change of mine:
`-f11 T,...`, Ctrl+Alt+F11 at those virtual seconds as `speed.c` does it, followed by a reading of
the port, and `vbe_blocks` in its own port line (`checks\t386mon-v4.3\harness-v4.3.diff`).

* **At 640x480 in 16 colours, and at 800x480 in 16 bits with the pointer change**: T386MON started
  from the floppy, Log now, all nine tests, the tests' view, the live view, Windows' processor figure
  on and off, compact and back, then Windows shut down with it open (the T386MON step's keys `m1`).
  T386MON found the port ("information port 1.1: pc-harness on PC harness, esp-korvo1"; on the board
  the firmware's version and the board's name), every test completed, and the log is whole with its
  closing line. The tests' frames a second (the harness's instruction counts, as T386MON's README 7
  explains): 640x480x4 fill 343.33, scroll 351.39, text 7.31, aside 7.41, no pointer 7.40, sprite
  57.87, full copy 23.47, full picture 0.45; 800x480x16 with the pointer change fill 480.21, scroll
  323.23, text 3.50, aside 3.78, no pointer 3.79, sprite 72.85, full copy 357.44, full picture 1.13.
  `/bench /quit` at each: the same within 3 per cent.
* **v4.3 against the branch's core**: the same four runs (those two, 800x480 as released, and
  `/bench /quit` with the pointer change) on the T386MON step's own build of `perfport` (6e9a6f1):
  the same `END` line, all 398 pictures the same, `C:\T386MON.LOG` the same bytes. Against the
  T386MON step's own runs a-vga, a-n16 and p-n16 (`..\t386mon\README.md` 8.2): the same `END` line
  and the same log. So T386MON sees on v4.3 exactly what it saw on the branch.
* **The port's values against the harness's own counters**: `CHANGED` equal to the harness's count
  of pictures that sent a row, and `PICTURES` to its count of pictures, in all 15 runs with the port
  (9 on v4.3: T386MON's 7 and PPTEST's 2; 6 on the branch's core).
* **PPTEST.COM** under FreeDOS and in a Windows DOS box at 800x480x16 (`..\t386mon\port-test\`): 562
  and 570 checks, 0 bad; the `END` lines the branch's.
* **Ctrl+Alt+F11**: 27.3.
* **The pictures**, looked at: the live view and the tests' table at both modes, compact, the `/bench`
  table, and the live view before and after each Ctrl+Alt+F11. T386MON's window, cropped, is in
  `..\t386mon\test-results\v4.3\`; the whole screens stay in WSL. One thing seen in them: with
  `vbe_blocks` 0 the rows a second sent to the panel are 326 against 86 with 1, because the drawing
  of v3.9 that `vbe_blocks = 0` brings back sends one band from the first changed row to the last
  (`vga_redraw_rows()`) where the blocks send each band [verified: `vga.c`]; as in v4.1 and v4.2.

### 27.9 What a picture costs [verified: `checks\drawing-cost-v4.3.txt`; the board inferred]

v4.3 adds to every picture what the branch added: `pc_vga_step()` reads the clock twice and hands
the picture to `perfport_picture()` (a count, a sum, the second's figures), and each band goes
through `perfport_redraw()` (two counts) on its way to the panel. The drawing's host time on this
PC, microseconds a periodic picture, each run alone, two rounds, v4.3 and v4.2 built in the v4.1
work's harness:

| Windows 95 | v4.3 | v4.2 |
|-|-|-|
| 800x480x16, mouse still | 26.0, 26.6 | 26.2, 26.0 |
| 800x480x16, mouse every 50 ms | 23.5, 24.2 | 23.5, 23.7 |
| 800x480x16 `dib`, mouse every 50 ms | 15.6, 15.7 | 15.6, 15.8 |
| 800x480x32, mouse every 50 ms | 28.3, 28.5 | 28.7, 29.3 |
| DOS board 800x600x16, mouse every 50 ms | 23.5, 23.9 | 24.0, 24.5 |
| standard VGA 640x480x4, mouse still | 38.3, 38.7 | 38.5, 38.3 |
| standard VGA, mouse every 50 ms | 37.8, 38.0 | 37.8, 37.9 |

Within 3 per cent either way in every case, and the 14 pairs' `END` lines the same. In the built
Korvo-1 image the store paths into video memory (`iomem_write8`, `16`, `32` and `_string`,
`vga_mem_write`, `16`, `32` and `_string`) are v4.2's instruction for instruction with the addresses
and offsets taken out, and `vga_refresh()` too (835). `pc_io_read` and `pc_io_write` and their 16-
and 32-bit forms are 9 to 14 instructions longer (the port's cases in their compare trees: a guest's
`in` or `out` at any port may take a compare or two more [inferred]); `pc_vga_step()` 39 where it
was 25; `vga_take_marks()` 171 where it was 151 and `vga_graphic_refresh()` 1,989 where it was
1,961 (the VBE counts) [verified: `dis43.sh`]. On the board the clock read is `esp_timer`'s, a few
hundred nanoseconds twice a picture of some milliseconds [inferred].

### 27.10 Warnings [verified: `checks\warnings-wall-wextra-v4.3.txt`]

`build.sh`'s two runs and the five in-tree builds (release and crash-test, both boards): the old
`i2s.c` line only. `-Wall -Wextra` on the files v4.3 changed that the boards compile, each board's
own command, beside v4.2's: `vga.c` 3, `pc.c` 1, `i8042.c` 1, `speed.c`, `disk_image.c` and
`status_led.c` 0 on each board, v4.2's same counts and kinds; `perfport.c` (new) 0. The new host
test compiles clean with the host tests' `-Wall -Wextra`. The harness builds, 0.

### 27.11 Sizes [verified: `checks\size-*-v4.3.txt`, `checks\reproducible-v4.3.txt`]

| | Korvo-1 v4.3 | v4.2 | DOS board v4.3 | v4.2 |
|-|-|-|-|-|
| `tiny386.bin` | 906,592 | 903,312 | 938,272 | 934,992 |
| Flash `.text` | 441,268 | 437,990 | 438,164 | 434,886 |
| Flash `.rodata` | 233,664 | 233,664 | 265,012 | 265,012 |
| Internal RAM used by code and static data | 250,644 | 249,980 | 253,642 | 252,978 |
| Internal RAM left | 253,100 | 253,764 | 250,102 | 250,766 |
| LP SRAM | 8,244 | 8,244 | 8,244 | 8,244 |

The internal RAM's code (`.iram0.text`, 217,166 and 220,404) is v4.2's size; the 664 bytes more are
`.bss`, the snapshot's 128 words and the counts, as the branch measured on v4.1
(`..\t386mon\PROTOCOL.md` 8) [verified: the same difference]. The VGA state, in the device window,
has 16 bytes more (the four VBE counts). 27 symbol names are new and none gone. The dual-boot
`tiny386.bin` uses 906,592 of its partition's 1,966,080 bytes, 53.9 per cent spare [verified: flash
simulation].

### 27.12 `build.sh` and the four image sets [verified: `checks\reproducible-v4.3.txt`]

`build.sh` changed in its first comment only. Two runs from nothing, each in a clean environment
(WSL `~/v43/merge/rel/rel1` and `rel2-second-run`): exit 0, 195 s and 183 s; all 23 files of the four
sets the same in both, ELF files and `SHA256SUMS` included, no ELF holding its work folder's name,
`v4.3` in each app; both patches give the tag's tree exactly (284a9331). The bootloaders, partition
tables and `flash_args` of all four sets are v4.2's byte for byte; only `tiny386.bin` and
`tiny386.elf` changed. The update from v4.2, or from v3.6 on, is the app alone: 0xE20000 to
0xEFDFFF, and the same bytes as the whole set written over be5b725 [verified:
`checks\app-update-v4.3.txt`].

### 27.13 Checks [verified unless marked]

| Check | Result |
|-|-|
| The merge | two conflicts, both sides kept; every other file the branch's lines exactly (27.1, `checks\merge-v4.3.txt`) |
| `build.sh` from nothing, twice | as 27.12 (`checks\reproducible-v4.3.txt`, `build-sh-v4.3-run1.txt`, `run2.txt`) |
| `esptool image-info` | every bootloader and app valid, checksum and hash; the apps say `v4.3`; rel2's the same as rel1's (`checks\image-info-*-v4.3.txt`) |
| In-tree builds, release and crash-test, both boards | exit 0; compiler warnings: the old `i2s.c` line only (`checks\warnings-wall-wextra-v4.3.txt`) |
| `-Wall -Wextra` on the changed files beside v4.2's | the same counts; `perfport.c` 0 (27.10) |
| The status LED in the built code | the panic path all in internal RAM; `build.sh`'s bit-bang layout check passes in all three (`checks\status-led-v4.3.txt`) |
| Host tests | 45 of 45; 45 with a long folder; 37 of 37 from the patch (27.4) |
| The port and Ctrl+Alt+F11 | `test_port_blocks` 18 checks on each board; 7 of 7 planted faults caught; T386MON follows three changes in Windows (27.3) |
| Sanitizers | AddressSanitizer and UBSan clean on nine test runs; ThreadSanitizer: the allowed race only, at v4.2's 19 places (27.4) |
| The flicker harness | 28 runs: every `END` line v4.2's, every picture equal to the reference (but the installs' 1,706, as v4.2's), no pixel black between two pictures (27.5) |
| DOS programs | MODE13X and BMODES: v4.2's `END` lines, shots and board lines (27.6) |
| Windows 95 on the PC | 27 standard, 11 native, BENCH and the install: v4.2's `END` lines and pictures; BVBE480 every picture right, its `END` lines v4.2's in either harness (27.7) |
| T386MON 1.2 | finds the port, values equal to the harness's counters in 9 runs (and the branch's 6), tests complete at both modes; logs and pictures the branch's byte for byte (27.8) |
| The drawing's cost | within 3 per cent of v4.2's; the store paths v4.2's instruction for instruction (27.9) |
| The flashing targets | as v4.2, line for line (`checks\cmake-flash-refusal-v4.3.txt`; no flash target was run) |
| The flash simulations | as v4.2's but for the app's size, hashes and version (`checks\flash-simulation-v4.3.txt`, `flash-simulation-bootswap-v4.3.txt`) |
| v4.2 to v4.3 by the app alone | 0xE20000 to 0xEFDFFF only; equal to the whole set over be5b725 (`checks\app-update-v4.3.txt`) |
| The scripts' flow | the scripts are v3.9's; the harness's 103 cases with the v4.3 images through cmd.exe, PATH `C:\Windows\System32` and `C:\Windows` only: no write reached the stub, and all 103 give v4.2's exit, WOULD WRITE lines and esptool calls (`checks\bat-harness-v4.3.txt`) |
| The card | no card file changed; `setup.c` and `ini.c` at the tag on a pretend card: 0 problems, every key known (`checks\card-check-v4.3.txt`) |
| `SHA256SUMS` | 27.16 |

### 27.14 What only a board can say [unknown]

* Everything T386MON shows that the harness cannot: the drawing's time on core 0 (`DRAW_*`),
  PSRAM and internal RAM free, the status code, the log's counts, the card's waits, and what a
  snapshot costs (`SNAP_US`; the heap walks of `..\t386mon\REVIEW.md` 7).
* The tests' frames a second in real time at 640x480 in 16 colours and at 800x480 in 16 bits, as
  released and with the pointer change: how much of the harness's picture carries over
  (`TEST-PLAN.md` 15).
* That a Win32 program on the board reaches the port as T386MON does on the PC (the same guest
  code [inferred]).
* Everything of 25.17 and 26 that is still open: the pointer change on the panel, Ctrl+Alt+F11's two
  settings, the grey text screen at a restart from 16 colours.

### 27.15 For Plat to decide

* The points of 25.18 and 26.6 stand.
* Whether `PROTOCOL.md` in `..\t386mon\` should be brought up to date too: its sections 1, 10 and 12
  were written while the port was on the branch alone; `..\t386mon\README.md` 2.5 now says which
  firmware has it, as asked, and the protocol itself is unchanged. (Done after the review of v4.3,
  which asked for it: 28.2.)

### 27.16 `SHA256SUMS`

Made again after the docs: v4.2's 170 files in v4.2's order, and 3 new, the harness changes of
v4.3's runs (`checks\flicker\build-v4.3.diff`, `checks\t386mon-v4.3\harness-v4.3.diff`,
`checks\win95-v4.3\build-v4.3.diff`). `research\FLICKER-PLAT-NOTES.md` stays out, as before. Every
line checked, each image folder's own list, `previous-v4.2\SHA256SUMS` (29 files in that folder, 141
from `v3\`), and every earlier `previous-*\` list file by file in the places `README.md` names
[verified: `checks\sums-v4.3.txt`]. The T386MON folder's own `SHA256SUMS` (`..\t386mon\`) made
again the same way: its 157 files in its order with `README.md` changed, and the four pictures of
`test-results\v4.3\` after them, 161 lines, every one checked [verified: the same file].

### 27.17 Where things are

WSL `~/v43/merge/`: `tree` (the worktree on branch `v43`, tag `v4.3`), `t-v4.3` (`git archive` of the
tag), `ht/` (the host tests at the merge and at v4.3), `builds/` and `b43.sh` (the in-tree builds),
`mt/` and `mt43.sh` (the mutants), `asan/` and `asan43.sh`, `warn43.py`, `dis/` and `dis43.sh`, `fh/`
(the flicker harness copy: `bin/r43`, `bin/r43d`, `runs/`, `summary-l1.txt`), `ar43.sh`, `l1-43.txt`,
`tab43.sh` and `fhchk.sh` (the flicker runs and their checks), `ph/` (the T386MON harness copy:
`bin/v43-*`, `bin/v42-*`, `bin/pp41-korvo1`, `runs/`, `runs-std-v43/`, `runs-bench/`, `gi/`), `keys/`,
`trun.sh`, `t43.sh`, `t43chk.sh` and `f1.txt` (T386MON), `std43.sh` and `cmp43.sh` (the standard
Windows runs), `h41/` and `bvab.sh` (the v4.1 work's harness copy, the BVBE480 A/B and `p43.sh`, the
drawing's cost), `rel/` (`stage/`, `rel1/`, `rel2-second-run/`, `rel43.sh`, `repro43.sh`), `chk/` and
`checks43.sh` (the host tests at the tag, the simulations, the refusal, the card, the LED, the app
update), `install43.sh`, `collect43.sh` and `sums43.sh`, and the `m*.sh` and `s*.sh` that made the
merge or only read. Copies of Windows (the run folders' disk images) were deleted after each run or
stay in WSL; none is anywhere public, and only T386MON's own window, cropped, left WSL. On Windows the
session's scratch folder `v43\` holds the scripts as written, `bat-harness\` (the flow harness, run
through its `go.cmd` with PATH set to `C:\Windows\System32` and `C:\Windows` only), `pics\` (pictures
of Windows from the T386MON runs, kept off the package) and `crops\`. Nothing started for this is
still running.

## 28. The review of v4.3 (2026-10-24): no firmware change

`REVIEW-v43.md`, one reviewer on tag `v4.3` and this package: the merge lost nothing from either
side, the port follows Ctrl+Alt+F11 and lives with `VGA_CLEAR_AROUND`, T386MON's own tests on the
board's display model leave no black between two pictures on v4.3 where the branch's core left
141,376 pixels, the emulator's core costs a few compares more on port I/O and nothing more on its
stores, the package consistent, and nothing that blocks giving v4.3 to Plat. Its findings are two
minor faults in the docs and five notes (`REVIEW-RESOLUTION.md` round 10 has each). Each was checked
before it was acted on, and none needed the firmware changed, so **the firmware stays v4.3** (tag
`v4.3`, 0f87c1c, tree 284a9331): the images, patches, `build.sh` and card files are the ones section
27 describes, made again here byte for byte (28.5). What changed is the docs: 26.3, 27.1, 27.15 and
this section, `TEST-PLAN.md` 15c, `README.md`, `REVIEW-RESOLUTION.md`, and in `..\t386mon\`
`PROTOCOL.md` and `README.md` 2.5 and 7. No commit was made, nothing was flashed, no serial port was
opened, and nothing of this has run on a board. `research\FLICKER-PLAT-NOTES.md` is unchanged since
2026-10-23 18:25, so nothing Plat has said since bears on it.

### 28.1 Each finding checked [verified: `checks\findings-v4.3-review.txt`]

Against the scratch tree's tags (read only) and the reviewer's own runs in WSL `~/v43/review`:

| # | Severity | What was found when checked |
|-|-|-|
| 1 | minor | The branch changed 18 files against v4.1, v4.2 changed 12, five on both sides (`esp/version.txt`, `tests/host/run.sh`, `vga.c`, `vga.h`, `esp/main/speed.c`), 25 in all; two conflicted, so git merged 16 of the branch's files by itself: the 13 only the branch changed, and `vga.c`, `vga.h` and `speed.c`. 27.1 said 15 |
| 2 | minor | `..\t386mon\PROTOCOL.md` 1 said the port "is not in any image of the package", and its `FIRMWARE` row gave "v4.1-perfport" |
| 3 | note | On v4.3 (run `fb-vga`) pictures 19,393 to 19,400, at 583.9 to 584.1 s as Windows shut down from 16 colours, differ from the guest's screen in 89,955 pixels of rows 40 to 439, none black, and the next picture sends 400 rows and is right; the branch's core (`fbp-vga`) the same pictures, the same `END` line (the host's wall time aside) and the same `T386MON.LOG`; v4.2 (`fb42-vga`) 4 such pictures at its shut down |
| 4 | note | The harness's `snapshot()` calls `vga_refresh(g_pc->vga, redraw, NULL, 1)` on the live VGA state, in every harness copy looked at; in the reviewer's run with the port read every second (`rv-f11n16`, `-snap 5`), `VBE_WHOLE` rises by one at 436, 441, 446, 451 and 456 s with blocks on, beside the two after the change back on at 430 s. The harness's own `redraw` does not pass through `perfport_redraw()`, so the rows sent and `CHANGED` are not counted, nor `PICTURES` (counted in `pc_vga_step()`). On the board `vga_refresh()` is called only in `pc_vga_step()` (`pc.c`; none in `esp/`) |
| 5 | note | `perfport.c` 236: `PERFPORT_PUT(v, ok, PP_I_VBE_BLOCKS_ON, vga_dirty_blocks)`, a plain read; `vga.c` 51: `__atomic_store_n(&vga_dirty_blocks, on ? 1 : 0, __ATOMIC_RELAXED)` |
| 6 | note | The top `SHA256SUMS` had 173 lines and no `REVIEW-v43.md` |
| 7 | note | Nothing to check on the PC: what a snapshot costs is the board's to say |

### 28.2 The docs put right (findings 1, 2, 4 and 6)

* 27.1: "merged 16 files by itself", and which.
* `..\t386mon\PROTOCOL.md`: its header says what was brought up to date and when; section 1 opens
  "In tiny386-idf v4.3 and later; the protocol is unchanged", and its bullet on the firmware side
  says the branch's code is in v4.3 and the branch's own builds were never in a package's images;
  section 2 says tiny386 before v4.3 (or built without `TINY386_PERFPORT`) has no port; the
  `FIRMWARE` row gives "v4.3"; section 5, and the harness's paragraph in section 9, say that on the
  PC the four VBE counts include the harness's own pictures (finding 4); section 10 opens with the
  merge into v4.3; section 12 says where v4.3's work is. The protocol, its values and `perfport.h`
  are unchanged; `..\t386mon\perfport.h` is still v4.3's byte for byte [verified].
* `..\t386mon\README.md` 2.5 says `PROTOCOL.md` now names v4.3; section 7 has the harness's own
  pictures in the VBE counts (the live view's "VBE drawing" line and the tests' `vbe_rows` and
  `whole` columns), and to leave them out when counting VBE pictures on the PC.
* 27.15: the question about `PROTOCOL.md` is answered (the review asked for it).
* `README.md`: the review in its opening, its file table (`REVIEW-v43.md`, and `REVIEW-v42.md`,
  which the row had missed), round 10 and section 28 in their rows, and the `SHA256SUMS` row.
* The harness folders in WSL (`~/win95-debug/pc-repro/harness`, the T386MON and flicker harness
  copies) were not changed: they belong to the steps that made them, and the note is in the
  package's docs, where anyone running the harness for the port reads it.

### 28.3 The text screen at Windows' shut down from 16 colours (finding 3) [verified on the PC; the board inferred]

The review ran T386MON's tests on the flicker harness's board model, which the merge step had not,
and so saw Windows' shut down from 640x480 in 16 colours with every picture held against the guest's
screen: 8 pictures, a quarter of a second, with the text area wrong and nothing black, then right.
It is the pattern of 26.3 (the text picture right as the mode is set, the next ones off with only a
band sent, until the whole text area is drawn again), seen this time at a shut down rather than a
restart, and much shorter [inferred, as the review has it]. The branch's core has it to the picture
and v4.2 has 4, so it is older than v4.3 and is not the port's. `TEST-PLAN.md` 15c now tells Plat
so, in one paragraph; 26.3 has a line for it. The cure is still the one 26.6 names, for a later
version.

### 28.4 Not changed (findings 5 and 7)

* **5, the plain read of `vga_dirty_blocks`**: deferred. It is one aligned 32-bit word, read once
  per snapshot on the emulator's core and stored only by `vga_set_dirty_blocks()` on the drawing's;
  on RV32 a relaxed atomic load of such a word is the same single `lw` the plain read compiles to
  [inferred: GCC's RISC-V mapping of relaxed atomics], so `__atomic_load_n(&vga_dirty_blocks,
  __ATOMIC_RELAXED)` would change no instruction the board runs, and v4.2's own reads of the
  variable in `vga.c` are the same kind (25.5). Making it now would need a commit, a new tag and new
  images for no change in behaviour, and the checks of 27 and of the review again. The review says
  it is not needed for v4.3; it is for the next version that changes `perfport.c` anyway, Plat's call
  (28.6).
* **7, what a snapshot costs on the board**: [unknown] until the board runs it. `TEST-PLAN.md` 15b's
  Photo 1 keeps asking for the "Its own cost" line (`SNAP_US`), as round 9 left it.

### 28.5 Run again [verified: the `checks\...-v4.3-review...` files]

No commit was made, so the tag, the tree and the images are v4.3's; each was checked again in a
clone of the scratch tree of this step's own (WSL `~/v43/apply/repo`, at `v4.3`):

| Check | Result |
|-|-|
| The tree | the clone at 0f87c1c, tree 284a9331, `esp/version.txt` `v4.3`; the scratch tree's `v42` (6502063), `perfport` (6e9a6f1) and `v43` (0f87c1c) where 27 left them |
| The patches | `tiny386-idf-v4.patch` is `git diff upstream-20261022 v4.3` and `tiny386-idf-v3.patch` `git diff pristine v4.3`, byte for byte; each applied to its base gives tree 284a9331 (`build-sh-v4.3-review.txt`) |
| `build.sh` from nothing (`~/v43/apply/rel/w`, ESP-IDF in a clean environment) | exit 0, 169 s; all 23 files of the four image sets the package's byte for byte, the same list; the only compiler warning the old `i2s.c` line; `v4.3` in each app (`build-sh-v4.3-review.txt`) |
| Host tests at the tag | 45 of 45 in 64 s (`host-tests-v4.3-review.log`) |
| The lists before the docs changed | `SHA256SUMS` 173 of 173, each image folder's own list, `..\t386mon\SHA256SUMS` 161 of 161 |
| `SHA256SUMS` after the docs | 28.7 |

Not run again: the harness runs of 27 (the review ran the flicker harness's 28 configurations,
MODE13X and BMODES, and T386MON on both harnesses, from its own copies and builds, and nothing they
rest on has changed since), the standard and native Windows runs, BENCH, BVBE480, the sanitizers,
the flash simulations and the scripts' flow; their inputs (the images, scripts and card files) are
byte for byte the ones 27 checked [verified: `SHA256SUMS`]. No Windows image was copied or run in
this step.

### 28.6 For Plat to decide

* The points of 25.18 and 26.6 stand; 27.15's question about `PROTOCOL.md` is answered (28.2).
* Whether the next version that touches `perfport.c` should read `vga_dirty_blocks` with
  `__atomic_load_n(..., __ATOMIC_RELAXED)` (28.4): no change on the board, a cleaner record for a
  thread checker.

### 28.7 `SHA256SUMS`

Made again after the docs: v4.3's 173 files in v4.3's order, and `REVIEW-v43.md` after them (as
`REVIEW-v42.md` was after its review), 174 lines. `research\FLICKER-PLAT-NOTES.md` stays out, as
before. Every line checked, each image folder's own list, and every `previous-*\` list file by file
in the places `README.md` names. The T386MON folder's own `SHA256SUMS` made again in its order, 161
lines, with `PROTOCOL.md` and `README.md` changed [verified: `checks\sums-v4.3-review.txt`].

### 28.8 Where things are

WSL `~/v43/apply/` (all made by this step): `repo` (a clone of the scratch tree at `v4.3`,
detached), `ht/` and `ht.log` (the host tests), `rel/` (`build.sh` from nothing: `build.out`,
`w/images`, `w/build-*.log`; the build tree itself deleted afterwards), `a1.sh` (the clone, the
patches, the lists), `a2.sh` (the host tests and the build), `f1.sh` and
`findings-v4.3-review.txt` (the findings checked), `sums.sh` (the lists), and the `look*.sh` that
only read. On Windows the session's scratch folder `v43apply\` holds the scripts as written. Nothing
started for this is still running; no copy of Windows was made.

## 29. v4.4, part 1 of 2: the display (2026-10-24)

Plat ran v4.3 with T386MON 1.2 and sent its log (board figures in 29.0), with three requests
about the display: Ctrl+Alt+F11 should also save `vbe_blocks` in the ini ("yeah that'd make
sense to me"); the grey text screen at a restart from 16 colours fixed "If you want"; and he saw
tearing in every mode: T386MON's sprite going black and coloured again, and in 16 bits a band of
mixed rows in the colour bands. This part makes those, looks at what each picture costs core 0
and where the guest's time goes in the text test, and adds two ways to take the tearing away
that he can turn on and off. Part 2 adds the storage work and packages v4.4; no image is made
here.

This is the first round on Plat's fork rather than the scratch tree: branch `s31-v4.4`, made from
`esp32-s31` (8d4b758, v4.3 and `s31/`) in its own worktree, WSL `~/fw/wt-v44` of the canonical
repository `~/fw/tiny386-s31`, every commit by Platima with his GitHub noreply address and the
Co-Authored-By line. The task asked for this section as 28; 28 is the review of v4.3, so it is
29. Nothing was flashed, no serial port was opened, nothing was pushed, and nothing of this has
run on a board.

### 29.0 In one screen

* **Plat's figures** [reported: his `T386MON.LOG`]: at 800x480, the drawing on core 0 takes up to
  41 ms a picture when Windows changes the whole screen, in 256 colours and in 16 bits alike
  (the panel then gets 14 pictures a second); the guest is busy all the time and runs 0.4 to 2.0
  million instructions a second; text is the slowest test, 3.7 frames a second in 256 colours
  and 2.9 in 16 bits against 6.6 with the standard VGA driver.
* **Ctrl+Alt+F11 saves** (29.2): the setting goes into the ini the PC was started from, that one
  line changed or added, the new file made whole beside the old one before they change places,
  on the emulator's core after the guest's disk writes are on the card. 28 shapes of ini tested.
* **The grey text screen** (29.3): fixed. A text screen is drawn whole when plane 2, the font,
  is written; the install's 1,706 wrong pictures are now none [verified: harness].
* **What a picture costs** (29.4): the drawing is bound by PSRAM, not by instructions (the 256
  colour picture converts twice the instructions of the 16-bit one and takes the same 41 ms).
  A VBE row is now converted only where its blocks were written, and the blocks are 256 bytes:
  T386MON's sprite converts 22 per cent of the pixels it did, its text 55, its fills 64, its
  full copy all of them [verified: harness]. What the board gains is [unknown].
* **Text** (29.5): Windows' DIB engine runs 571,000 instructions a frame of the text test at 16
  bits against 272,000 for the standard VGA driver, and takes about three quarters of them; our
  store path is a few per cent [inferred]. Nothing of ours worth changing.
* **Tearing between rows** (29.6): `[esp] double_buffer = 1` (Korvo-1 only), Ctrl+Alt+F9: a
  second frame buffer, swapped at the panel's frame. Off by default: 752 KiB of the emulator's
  memory, which on Plat's pool is 1M of the guest's [inferred], a frame's wait and a copy of each
  changed row. Modelled on the harness, every picture right [verified]; the ESP-IDF side
  [unknown].
* **Pictures caught part way through drawing** (29.7): `[esp] pace_quiet`, Ctrl+Alt+F10: a picture
  waits until the guest has not drawn for that many instructions, at most `pace_wait_ms`. Off by
  default: in Plat's session it takes such pictures from 7, 4 and 4 per cent to 2, 0 and 1, but
  T386MON's tests lose a third of their pictures and its sprite stays black in half of them
  [verified: harness].
* **Checks** (29.9 to 29.11): 52 host tests pass (seven new); every picture of v4.3's 28 flicker
  harness runs equals the guest's screen, with v4.3's END lines; the standard Windows runs give
  v4.3's results; the 8 wrong pictures of 28.3 (Windows' shut down from 16 colours) are gone;
  both boards build with the old `i2s.c` warning only.

### 29.1 The commits [verified: `git log esp32-s31..s31-v4.4` in WSL `~/fw/wt-v44`]

| Commit | What |
|-|-|
| 4758608 | `vga.c`, `tests/host`: a text screen drawn whole when its font changes; `test_font.c`; `run.sh` finds the s31- tags (29.3) |
| a1995d7 | `esp/main`: Ctrl+Alt+F11 also saves `vbe_blocks` (`ini_edit.c` new, `setup.c`, `speed.c`, `esp_main.c`); `test_ini_edit.c` (29.2) |
| 64a45b3 | `vga.c`, `vga.h`: VBE rows converted only where written, blocks of 256 bytes (29.4) |
| 63058e9 | `vga.c`, `pc.c`, `esp/main`: pictures at the guest's quiet moments, `[esp] pace_quiet`, Ctrl+Alt+F10 (`display.h` new); `test_pace.c` (29.7) |
| d5848e2 | `esp/main`, `vga.c`: double buffering on the Korvo-1, `[esp] double_buffer`, Ctrl+Alt+F9; the card inis (29.6) |
| 8c264e6 | `tests/host`: `vga_set_fb` renamed in the reference builds (d5848e2 alone did not link test 3c) |
| 3398146 | `esp/version.txt`: `v4.4-display` |
| 40b4513 | `vga.c`: the port's KiB found written counted as KiB (29.4); `test_vga_info.c` one check more |
| this one | `s31/BUILD-NOTES.md` 29 and `s31/checks/` (29.14) |

`git diff --stat esp32-s31 40b4513 -- . ':!s31'`: 26 files changed, 2,546 lines added, 64 taken
out. `esp/dependencies.lock` stays out of every commit, as before; every commit's files were
looked at, and none is a binary, a picture or anything of Windows'.

### 29.2 Ctrl+Alt+F11 saves `vbe_blocks` in the ini [verified: code, `test_ini_edit.c`]

* **The text** (`esp/main/ini_edit.c`, new, plain C): one setting changed and every other byte
  kept. The lines are read as `ini.c` (inih, with `ini.h`'s settings) reads them: `;` and `#`
  comments, an inline comment after a space, `=` or `:`, a line that begins with a space after a
  setting continuing that setting, section names exactly as written (`[ esp ]` is not `[esp]`),
  a byte order mark. Where the setting is, the value on each of its lines in `[esp]` is replaced
  and the spaces, any inline comment and the line's end kept; lines continuing it go. Where it
  is not, a line `vbe_blocks = N` goes into the last `[esp]`: after a comment line that names it
  (`;vbe_blocks = 1`, as the package's `win95-800x480.ini` has), else after the section's last line
  that is not blank, and never where it would take over a following continuation line. With no
  `[esp]`, the section and the line go at the end. New lines end as the file's first line does
  (CR LF or LF). Nothing is written when the value is there already.
* **The file** (`setup.c`, `setup_ini_save()`): the new text into `NAME.ini.t386new`, flushed,
  `fsync()`ed and read back; then the ini renamed to `NAME.ini.t386old`, the new one to its name,
  and the old one removed. ESP-IDF's FAT `rename()` does not replace a file, so the three steps
  are needed, and a cut in power between any two leaves one of four states;
  `setup_ini_tidy()`, which `setup_find()` runs for each folder at start-up, puts each right
  (`X` and `X.t386new`: the new one removed; `X.t386old` and `X.t386new`: the new one, whole and
  read back, takes the name; `X` and `X.t386old`: the old one removed; `X.t386old` alone: back to
  `X`). Only names with those two endings are touched; `X.t386new` alone is left. Inis up to
  32 KiB; the two copies in PSRAM.
  (v4.5: a cut in power inside a FatFs rename can leave two names on one chain, so v4.5 renames
  nothing: a whole copy with a check line beside the ini, then the ini rewritten in place, then the
  copy removed; 31.2.)
* **When** (`speed.c`, `esp_main.c`): Ctrl+Alt+F11 changes the setting at once, as since v4.2,
  and asks for the save; `run_pc()`, on the emulator's core between two steps, first puts the
  guest's unflushed disk writes on the card (as at a quiet moment), then saves, so the save never
  meets a flush of the guest's: both are on the same core, one after the other. The guest waits
  meanwhile; that time is left out of the speed figures. The console says what was saved and
  where ("vbe_blocks = 0 saved in /sdcard/tiny386/win95-800x480.ini (a line added to [esp])"),
  that the ini had it already, or why not. Presses close together are saved once, with the
  setting the last left. Ctrl+Alt+F10 and F9 (29.6, 29.7) save the same way.
* **Tested** (`tests/host/test_ini_edit.c`, section 6b, 106 checks): 28 shapes of ini, each result
  byte for byte and then read back with `ini.c` (`vbe_blocks` the new value, every other setting
  as before and in order): present, LF and CR LF, inline comments and spaces, quoted, `:`, empty,
  twice, in two `[esp]` sections, continuation lines, a last line without a line end, absent with
  `;` and `#` comments, absent with and without blank lines and continuations, an empty `[esp]`,
  a byte order mark, only in `[pc]`, names that only look alike, no `[esp]` (LF and CR LF),
  `[ esp ]`, an empty file, and the package's own `win95-800x480.ini`. Then the file: saved,
  changed, not written when the same (the same inode); a missing ini, one too large, a folder
  that cannot be written (the ini as it was, nothing left beside it); and every state a cut can
  leave, with a file of Plat's named `keep.ini.old` left alone and `setup_find()` listing an ini
  put back. On the board: FatFs, the card's timing and a real cut are [unknown].

### 29.3 The grey text screen at a restart from 16 colours [verified: harness, `test_font.c`]

26.3 found it: a text cell is drawn again only when its character or attribute changes, the
glyphs are in plane 2, and the VGA BIOS loads its font after the text mode's first picture, over
what the 16-colour picture left there; so blank cells kept grey glyphs for up to a minute. Now:

* a write of the map mask (SR02) that makes plane 2 writable or stops it, while the graphics
  controller is not in a graphics mode (text, or a font being loaded; SeaVGABIOS's
  `get_font_access()` and `release_font_access()` both qualify), counts a request on the
  emulator's core, as the clears do (`vga_ask()`, release);
* the text picture after a request (taken with acquire, so the font's stores, made before the
  map mask went back, are seen), every text picture while plane 2 is writable, and the picture
  after the character map select (SR03) changes, are drawn whole.

No store into video memory pays anything; Windows' 16-colour drawing, which writes the map mask
all the time in a graphics mode, pays a test. `test_font.c` (section 3e, both boards) holds
each text picture against a whole redraw: a font loaded between two pictures, a picture half way
through a load, another map B through SR03 (the cells' attributes pick B), nothing changed after
(the cursor's row only), and SR02 written in a graphics mode; v4.3's `vga.c` fails five of its
checks, v4.4's none.

In the flicker harness: the install through Windows' dialogs (v4.3's run `c43-gi`, and `g43-gi`
on my copy), which had 1,706 pictures unlike the guest's screen at the restart, has none, with
v4.3's END line (`fc-gi`, `g44-gi`, `gc-gi`). And 28.3's find, Windows' shut down from 640x480
in 16 colours after T386MON's tests: the review's run made again on my copy gives, for v4.3, its
8 wrong pictures to the pixel (from picture 19,393, 719,640 pixels), and for v4.4 none, with the
same END line; T386MON's whole session (keys `m1`) the same, and `C:\T386MON.LOG` the same bytes
(`fbv-v43`, `fbv-fin`, `fbv-fin2`, `m1-v43-vga`, `m1-fin-vga`, `m1-fin2-vga`). The standard
install run of the T386MON harness (`gi18`) had 1 picture of 35,845 unlike a full redraw on
v4.3 and has none (29.10). So `TEST-PLAN.md` 15c's paragraph on the text area at shut down, and
26.3's line, are out of date once v4.4 is packaged; part 2 should change them with the package.

### 29.4 What a picture costs on core 0 [verified: harness; the board inferred]

**Plat's figures** (`T386MON.LOG`, the board): `draw_ms` is 41.4 in 256 colours and 41.2 in 16
bits in the full-copy test, where every row changes every picture (480 rows; the panel gets 14.2
pictures a second, 6,827 rows a second), 19.4 in the 16-bit fill (about 300 rows of which 400
pixels change), 1.3 to 1.7 in text and sprite (40 to 45 rows a picture). A full 16-bit row is
about 86 us, a row with little changed 34 us. The 256-colour picture reads half the video memory
and runs about twice the instructions of the 16-bit one (6.5 against 3 a pixel, 22.8) and takes
the same time: the drawing waits on PSRAM, not on instructions [inferred]. Each converted pixel
is read from video memory and from the frame buffer (for the store-only-where-it-differs test),
and each changed word is written back: about 2.3 MB a full picture at 16 bits, so about 56 MB/s.

**What changed: only the part of a row that was written** (64a45b3). A marked row was converted
whole, though most drawing changes part of a row. `vga_take_marks()` now also gives each marked
row of a 1:1 picture the pixels its blocks reach (`rowx0`, `rowx1`, multiples of 4, the union of
its blocks; a block reaching only bytes past the pixels shown marks nothing), and
`vga_vbe_refresh()` converts only those. The blocks are 256 bytes where they were 1 KiB, so that
the part is close to what changed; the map is four times as large (3 KiB at 768K, in the device
window, which has 40 KiB spare), and `vga_take_marks()` looks at four times as many bytes a
picture (about 3,000 at 768K). A store costs the same. The shrunk pictures still convert whole
rows. The information port's `VBE_BLOCKS` stays in KiB, so `perfport.h` is unchanged and still
T386Mon's byte for byte: 64a45b3 divided the count of blocks by 4, which would wrap at 2^30 KiB
rather than the port's 2^32 (a reader taking differences would see one wrong second, after
about 1 TiB found written); 40b4513 counts KiB in the same one word, carrying the blocks of less
than 1 KiB (a 64-bit count would be read in two halves while the drawing's core writes it). The
values are the same until then: T386MON's log at 16 bits is the same bytes on both [verified:
`m1-fin-p16`, `m1-fin2-p16`].

Pixels converted a picture, T386MON 1.2's tests on the board model, VMDisp9x with the pointer's
byte changed [verified: runs `x-h43-*` (a1995d7, v4.3's VBE drawing), `x-s10-*`, `x-s8-*` (what
64a45b3 committed); `checks\drawing-v4.4-part1.txt`]:

| Test | 16 bits: v4.3 | rows' parts, 1 KiB blocks | rows' parts, 256 bytes (v4.4) | 256 colours: v4.3 | v4.4 |
|-|-|-|-|-|-|
| fill 400x300 | 240,000 | 239,616 | 153,600 (64 %) | 241,600 | 191,488 (79 %) |
| scroll 400x300 | 240,000 | 239,616 | 153,600 (64 %) | 241,600 | 191,488 (79 %) |
| text 400x300 | 36,907 | 34,522 | 20,160 (55 %) | 45,445 | 31,933 (70 %) |
| sprite 48x48 | 43,940 | 30,698 | 9,793 (22 %) | 47,365 | 18,272 (39 %) |
| full copy 800x480 | 384,000 | 384,000 | 384,000 (100 %) | 384,000 | 384,000 (100 %) |
| full picture | 13,473 | 12,723 | 12,003 (89 %) | 16,584 | 14,769 (89 %) |

Every picture of those runs equals the guest's screen. On the board the reads fall by the same
shares for those tests, and with them core 0's share of PSRAM, which the guest on core 1 shares
[inferred]; how many milliseconds that is, and whether the panel then keeps 33 pictures a
second in the fills (22 now), is [unknown]: T386MON's `draw_ms` will say. The whole-screen copy
is unchanged: that needs the frame buffer filled without the CPU (the S31's 2D DMA or pixel
processing accelerator), a larger piece of work that only a board can test.

**Looked at and not changed** [inferred]:

* **The per-row write-back**: `lcd_flush()` writes back bands of rows (rows less than 16 apart
  are one band), and `esp_cache_msync()` writes back the dirty lines of the band; a row whose
  part was not stored leaves its lines clean, so it costs a look, not a write. With the rows'
  parts, fewer lines are dirty. The UART's `display:` line (every 10 s with `uart_log = 1`) gives
  the write-back's own time on the board; Plat's log has none.
* **The 8-bit palette lookup**: four table loads and two stores for four pixels, 6.5 instructions
  a pixel; a two-pixel table would be 256 KiB, larger than the cache. The 8 and 16-bit pictures
  taking the same time says the instructions are not what costs.
* **Wider stores for 16-bit rows**: the row loop already moves two pixels a 32-bit word and
  stores only the words that differ; RV32's integer registers are 32 bits; the S31's vector
  unit (128-bit loads) cannot be tried on the PC, and the loop waits on PSRAM.
* **The zero-copy panel mode**: left out, as asked: it brings back the tearing.

### 29.5 Text in VBE modes: where the guest's time goes [verified: harness; the board inferred]

T386MON's text test draws its 400x300 window in 23 lines of `ExtTextOut()` with an opaque
background, every pixel of it every frame. On the harness (Windows at 2 million instructions a
second, the board's drawing modelled), counted in the test's own seconds (`PCR_WIN`, the
instruction pointer sampled at each step, `PCR_EIPHIST`):

| | 640x480, 16 colours (VGA.DRV) | 800x480, 256 colours | 800x480, 16 bits |
|-|-|-|-|
| frames a second (harness; board) | 7.35; 6.64 | 4.47; 3.70 | 3.50; 2.90 |
| guest instructions a frame | 272,000 | 447,000 | 571,000 |
| stores into the frame buffer a frame | 15,500 bytes and 5,400 words through the VGA window, planar | 29,200 of 32 bits into the LFB | 60,000 of 32 bits into the LFB |
| reads of the LFB a frame | (none) | 1,400 | 2,800 |
| the guest's instructions in the display code | VGA.DRV, about 70 % | DIBENG.DLL, about 73 % | DIBENG.DLL, about 78 %: 41 % in one 256-byte routine |

The hot code was found by its bytes in the image's own DLLs (`DIBENG.DLL`, `VGA.DRV`; read with
mtools in WSL, never out of it). So at 16 bits Windows' DIB engine runs 2.1 times the guest's
instructions of the standard VGA driver for the same text (the board's ratio of frames a second
is 2.3), because VGA.DRV writes eight pixels a store through the planar hardware and the DIB
engine writes two. That is Windows' work, not ours.

Ours is the store path: each of the 60,000 32-bit stores a frame goes through `iomem_write32()`
(36 instructions in v4.4's image, 32 in v4.3's) after `i386.c`'s own path; with the call, perhaps
2.5 million instructions a frame of a frame's 110 million cycles at 320 MHz: about 2 to 3 per
cent, plus what the `fence rw,w` of each mark costs on the S31, which is [unknown]. The board's
own figures bound all of it: the guest runs 1.68 million instructions a second in the 16-bit
text test and 1.81 at 640x480, 7 per cent apart, drawing included. Nothing of ours is worth
changing for it; a faster guest (the JIT research) is what would help. The LFB's reads are
1 to 2 per cent of the stores.

No setting on the board shows the marks' and the fence's cost alone: `vbe_blocks = 0` stops the
marks but redraws every picture whole, which loads core 0 and PSRAM, so it is not a clean
comparison. (At 640x480 in 16 colours `vbe_blocks` changes nothing: the standard VGA driver
never uses VBE, and T386MON shows `vbe_rows` 0 there [verified: harness].)

### 29.6 Tearing between rows: double buffering (`[esp] double_buffer`, Ctrl+Alt+F9) [code verified; the board unknown]

**Why**: one frame buffer, which the panel's DMA reads 35 times a second while core 0 rewrites
it, up to 41 ms a picture in Plat's 16-bit colour bands. A panel frame then shows the new picture
above the row being drawn and the old one below, or a band of rows written back in between: his
photos [inferred: the band is rows of the two pictures].

**How** (`lcd_korvo1.c`, `display.c`, `esp_main.c`, `vga.c`, `console.c`, `crash.c`):

* `lcd_double_start()`, once after the ini is read and before the guest's memory is worked out
  (`double_buffer_start()`): the first buffer is 752 KiB from the end of the emulator's pool,
  with the console's picture copied in and written back; the panel is deleted (its driver frees
  its own buffer) and started again with two user frame buffers, the second allocated in the
  room the first one left; the panel goes dark for a moment. If anything fails, the panel is
  started as before with the console's picture, the pool gets its 752 KiB back, and the console
  says so. Done under the display lock, so no console line lands in a buffer while it goes.
* After each picture that changed rows, `lcd_vga_present()` hands the back buffer to the driver
  (`esp_lcd_panel_draw_bitmap()` with a pointer into it: the driver points every buffer's DMA
  list at it, so the DMA takes it at the end of the frame it is sending, and asks for the link
  switch event), waits for the driver's frame-buffer-complete event (at most 100 ms; a frame is
  28 ms), swaps the two, and copies the rows that picture changed into the new back buffer, where
  the next picture is drawn over it. The VGA code is told its buffer (`vga_set_fb()`).
* The console and the crash screen draw into the front buffer (`console_set_fb()`); the crash
  screen also into the other, for a crash between a swap asked for and made. After the speed
  screen the picture is drawn whole into the back buffer, which the console did not touch, and
  all of it copied over the console: no black, where one buffer still clears.
* Ctrl+Alt+F9 turns the swapping off (the VGA code draws into the buffer on show, as with one;
  both hold the last picture after each copy, so nothing is lost) and on (the back buffer made
  the front one's copy first); without the second buffer it sets `double_buffer` for the next
  start. Either way it is saved in the ini (29.2).
* The UART's `display:` line every 10 s (with `uart_log = 1`): swaps, rows copied, the copy's and
  the wait's average time, and swaps the panel did not report; the speed screen says whether it
  is on.
* The DOS board has one frame buffer (no `LCD_DOUBLE_BUFFER`); the key is ignored there with a
  line.

**What it costs**:

* **Memory**: 752 KiB of the emulator's pool, `round4k(800 x 480 x 2)`. The guest gets the whole
  megabytes of what is left after `round4k(vga_mem_size + 16K)` (23.9), so the buffer costs 1M
  when (pool less that) is less than 752 KiB above a whole megabyte, and nothing otherwise. The
  boot console's "Emulator memory" line gives the pool. For each `vga_mem_size`, the pool the
  guest's memory needs (KiB) [arithmetic on `settle_memory()`]:

  | `vga_mem_size` | 14M, one buffer | 14M, two | 13M, one | 13M, two | 12M, two |
  |-|-|-|-|-|-|
  | 256K | 14,608 | 15,360 | 13,584 | 14,336 | |
  | 320K | 14,672 | 15,424 | 13,648 | 14,400 | |
  | 768K | 15,120 | 15,872 | 14,096 | 14,848 | 13,824 |
  | 1M | 15,376 | | 14,352 | 15,104 | 14,080 |
  | 1152K | | | 14,480 | 15,232 | 14,208 |
  | 1536K | | | 14,864 | 15,616 | 14,592 |

  Plat has 13M at 320K and at 768K (Windows' 12,824 KB in his log), which puts his pool at 14,352
  to 14,671 KiB [inferred]; then at 768K double buffering leaves him 12M. (v4.5: `TEST-PLAN.md`
  16c said 13M rather than 14M; both now say only that it costs 1M or nothing, and that the boot
  console's "Emulator memory" and `mem_size` lines, which 16c asks Plat to photograph, decide it:
  his pool itself is [unknown], and `win95-14m.ini` says 14M fits beside 320K only if the pool
  allows. 31.5.)
* **Time**: each changed row copied, 1,600 bytes read and written again, on top of its drawing:
  for a whole-screen picture 750 KiB more, so the full copy's 41 ms becomes perhaps 55 to 65
  [inferred]; and a picture reaches the panel at its next frame, up to 28 ms later than now.
* **Not cured**: a picture caught while the guest is part way through its drawing (29.7) is
  still shown, whole.

**What the board will show** [inferred from the driver's code; nothing of it can run on the PC]:
every panel frame one whole picture, no line or band between two pictures, in every mode, the
16-colour standard VGA path included; motion in steps of the panel's frames, a little later;
fewer pictures a second in full-screen changes; a black moment at start-up; 1M less memory in
Windows on Plat's pool. If the driver's event never comes (the counts of swaps not reported
rise and the picture stops changing) it is not working on this chip, and `double_buffer = 0`
(or Ctrl+Alt+F9) goes back to one buffer.

**On the harness** (`-double`, modelling `lcd_vga_present()`: the VGA code draws into the back
buffer, the panel shows the whole front buffer from the swap, the rows noted copied)
[verified: `checks\flicker-harness-v4.4-part1.txt`]: every picture equals the guest's screen in
12 runs, Plat's session with the pointer's byte changed and as released, the standard VGA
driver, the sweeps at 8, 24 and 32 bits and at 640x480 through the driver, the DOS box with VMDisp9x and with
the standard VGA driver, the install through the dialogs (text, 16 colours and VBE, two
restarts), 256 colours set live, and the session paced as well; and in T386MON's tests. Rows
copied: Plat's session 71,260 in 2,533 swaps of 9,187 pictures (28 a swap); T386MON's tests at 16
bits 191,090 in 2,082 swaps (92 a swap; 480 in the full copy). So the copy's work on the board is
as large as the rows drawn: twice the frame buffer's writes [inferred].

### 29.7 Pictures caught part way through drawing: pacing (`[esp] pace_quiet`, Ctrl+Alt+F10) [verified: harness; the board inferred]

**How**: the flicker research's remedy (24, `research\FLICKER-REPRO.md` 4.1), made the board's.
Every store into video memory sets `vga_wrote` (`pc.c`'s four stores into the LFB, `vga.c`'s four
window paths, every mode); after each step (512 guest instructions) `run_pc()` calls
`display_pace_step()`, which clears it and notes the guest's count at its last store
(`vga_pace_after_step()`). When a picture is due and pacing is on, `vga_task` waits until the
emulator's core finds the guest quiet (no store for `pace_quiet` instructions, or halted) and
wakes it with a task notification, at most `pace_wait_ms`, counted in FreeRTOS's 10 ms ticks
rounded up (15 ms is two ticks: 10 to 20 ms as the tick falls). A step pays a call more
(`display_pace_step()`, 24 instructions, and `vga_pace_after_step()`, 21); a store pays 1 to 7
instructions more for the flag (29.11). The UART and the speed screen
give the pictures drawn at a quiet moment and late. The harness's `-pace Q:D` runs the same
`vga_pace_after_step()` in its run loop.

**Plat's session** (the image the driver's install leaves, its byte changed; pictures taken while
an update of the guest's was under way, and the pictures in each segment) [verified:
`summary-pace.txt`, `summary-pace2.txt`]:

| Pacing (Q instructions, D ms) | typing: mid, pictures | scroll | move | late: typing's average, the most in any segment |
|-|-|-|-|-|
| off (v4.3, v4.4's default) | 7 %, 796 | 4 %, 630 | 4 %, 630 | 0.1, 0.3 ms |
| 2,000, 15 | 1 %, 788 | 1 %, 627 | 2 %, 625 | 0.4, 15.1 ms |
| 2,000, 30 | 1 %, 788 | 1 %, 627 | 2 %, 624 | 0.4, 30.2 ms |
| 5,000, 15 | 2 %, 776 | 0 %, 622 | 1 %, 621 | 0.9, 15.3 ms |
| 5,000, 30 | 0 %, 774 | 0 %, 622 | 1 %, 619 | 1.0, 30.2 ms |
| 10,000, 30 | 0 %, 735 | 0 %, 601 | 0 %, 615 | 2.7, 30.2 ms |
| 10,000, 60 | 0 %, 735 | 0 %, 601 | 0 %, 614 | 2.6, 60.2 ms |

With the driver as released, 10,000 and 30 take the broken pointer in the sweep from 47 per cent
to 2, and mid-update pictures from 57, 61 and 58 per cent to 36, 40 and 37, at 40 per cent fewer
pictures; with the standard VGA driver, 5,000 and 30 take typing, scrolling and moving from 7, 7
and 1 per cent to 0.

**T386MON's tests** (16 bits, the byte changed; pictures a second of the board model, and the
sprite as each picture shows it: whole, black, gone, part) [verified: runs `y-p16*`]:

| Pacing | fill | text | sprite | full copy | the sprite: whole, black, gone, part |
|-|-|-|-|-|-|
| off | 33.3 | 33.1 | 33.2 | 33.3 | 16, 54, 16, 14 % |
| 2,000, 15 | 29.4 | 30.8 | 25.4 | 27.3 | 27, 35, 32, 6 % |
| 5,000, 15 | 22.4 | 29.4 | 22.5 | 22.7 | 18, 48, 18, 18 % |
| 5,000, 30 | 17.9 | 29.4 | 18.2 | 17.0 | 16, 49, 28, 8 % |
| 10,000, 30 | 17.6 | 18.1 | 16.8 | 17.3 | 17, 57, 17, 9 % |

T386MON's sprite is erased, masked black and painted, frame after frame with no pause, and the
black lasts most of each frame, so a picture at any moment is most likely black; Plat's
"goes black, then coloured again" is the guest's own drawing, on any PC without a vertical blank
to wait for, and pacing cannot find a quiet moment in it.

**The default** is off: desktop use with the pointer's byte changed gains a few per cent of
clean pictures, and anything that draws without a pause (T386MON's tests, a game, a video) loses
a third of its pictures or more with no gain. Ctrl+Alt+F10 turns it on with 5,000 instructions
and `pace_wait_ms` 15 (or the ini's own values), the best of the measured trade-offs for using
Windows [verified: the tables]; Plat can judge on the panel.

### 29.8 The settings and the keys

| `[esp]` key | Default | Range | Key while the PC runs | Saved by the key |
|-|-|-|-|-|
| `vbe_blocks` | 1 | 0, 1 | Ctrl+Alt+F11 | yes (v4.4) |
| `pace_quiet` | 0 (off) | 0 to 10,000,000 instructions | Ctrl+Alt+F10: 0, or the ini's, else 5,000 | yes |
| `pace_wait_ms` | 15 | 1 to 200 | | |
| `double_buffer` | 0 | 0, 1 (Korvo-1) | Ctrl+Alt+F9: at once if made at the start, else for the next start | yes |

Ctrl+Alt+F12 (the speed screen) shows a line for each. `kbd_input.c` holds four Ctrl+Alt keys,
now all used (F9 to F12). The card inis (`card\tiny386\win95-800x480.ini`, `win95-14m.ini`,
`card-dosboard\tiny386\win95-800x600.ini`) have the new lines commented out, with what each does,
so that a key's save lands beside its comment.

### 29.9 Host tests [verified: `checks\host-tests-v4.4-part1.log`, at 40b4513]

52 passed, 0 failed (v4.3's 45 and seven new): `test_font` on both boards and against v4.3's
`vga.c` on both boards (which has to fail, and does), `test_ini_edit`, `test_pace` on both boards.
`test_vga_info` now counts the port's KiB, and over three pictures, so that a part of a KiB one
picture leaves is counted with the next; the clear test against v4.1's `vga.c` builds its own
test and reference with v4.1's 1 KiB blocks; `vga_rename()` renames v4.4's five new symbols.
`run.sh`'s `tagref()` finds an earlier release by its old name or its s31- name, so a clone of the
fork, which has only the s31- tags, still runs the comparisons with v3, v3.1, v3.5, v3.9, v4.1
and v4.3. `test_vbe` (pixel for pixel against the old loop, random stores, both boards) passes
with the rows' parts and the 256-byte blocks.

### 29.10 The PC harnesses [verified: `checks\flicker\harness-v4.4.diff` and the summaries named]

My copy of the flicker harness (`~/v43/merge/fh`'s, WSL `~/org2/v44disp/fh`) with these
additions: `PCR_WIN` (per window of virtual time: the guest's instructions, its LFB stores by size,
LFB reads, window store calls, pictures, the drawing's host time, VBE rows and pixels converted),
the drawing's host time of each picture in `frames.tsv`, the code bytes at the hottest places of
`PCR_EIPHIST` (kept in WSL), the T386MON harness's `put_key()` (before, a dialog's Enter waited
behind a key file's last key, so T386MON's runs could not use `-dialogs`), the cursor box's
yellow pixels (T386MON's sprite), `-pace Q:D` (29.7) and `-double` (29.6).

| Runs | Result |
|-|-|
| v4.3's 28 flicker runs (Plat's session, sweeps at every depth, 640x480, the DOS board, `vbe_blocks = 0`, the drawing in its own thread, DEBUG in MS-DOS mode, the DOS boxes, both installs, 256 colours set live), on 3398146 and again on 40b4513 | every picture of every run equals the guest's screen (v4.3: 1,706 wrong in each install, 29.3); the END lines of 28 of 28 v4.3's, both times |
| MODE13X at 256K and 768K, BMODES (the review's DOS runs) | every picture right; the END lines and the screenshots v4.3's on the same harness, on both commits (against the review's own runs on its harness copy the END lines differ, for v4.3 as for v4.4: the harness, not the port) |
| Windows' shut down after T386MON's tests, 16 colours (28.3) | v4.3 8 wrong pictures, v4.4 none, the same END line (29.3) |
| T386MON's whole session (`m1`), v4.3 and v4.4 | 16 colours: the same END line and `T386MON.LOG` the same bytes; 16 bits: every picture right on both; the END lines and the logs differ a little, since T386MON prints the port's counts, which differ with the blocks (fill 487.7 and 477.1 frames a second, scroll 321.5 and 325.7, sprite 71.9 and 72.9, full copy 350.5 and 348.2, text 3.49 on both) |
| the double-buffer model, 12 runs and T386MON's tests | every picture equals the guest's screen (29.6) |
| pacing, 13 runs and T386MON's tests at five settings | 29.7's tables; every picture equals the guest's screen |

The standard Windows 95 runs in my copy of the T386MON harness (`~/v43/merge/ph`'s), 3398146
against v4.3's runs in the same harness [verified: `checks\win95-pc-runs-v4.4-part1.txt`]: the 27
standard configurations at the desktop, each with v4.3's END line and final picture; the 11
native ones (16, 24 and 32 bits, 13M and 14M, with the mouse, the DOS box) v4.3's, and the six
audits 0 pictures of about 11,300 to 13,900 unlike a full redraw; BVBE480's 21 runs (both boards,
768K to 2M, the drawing in its own thread, `vbe_blocks = 0`) every picture right and v4.3's END
lines; BENCH 11 of 11 marks at v4.3's instruction counts and v4.3's END and picture; the install
through the dialogs (`gi18`) v4.3's END line and picture, its audit 0 pictures of 35,845 unlike
a full redraw (v4.3: 1). 40b4513 changes only what the port reports after 2^30 KiB found
written, which none of these runs comes near.

### 29.11 Builds, sizes, the store paths [verified: `checks\size-*-v4.4-part1.txt`, `checks\dis-v4.4-part1.txt`, `checks\warnings-wall-wextra-v4.4-part1.txt`]

In-tree builds at 40b4513 (`idf.py`, ESP-IDF in a clean environment, `s31\dependencies.lock`, as
`s31\build.sh` makes them), both exit 0, the only warning outside ESP-IDF v4.3's `i2s_task`
defined but not used:

| Board | Image, v4.3 | v4.4 part 1 | DIRAM used, v4.3 | v4.4 part 1 |
|-|-|-|-|-|
| Korvo-1 | 906,416 bytes | 920,062 (+13,646) | 250,644 | 251,354 (+710) |
| DOS board | 938,090 | 946,620 (+8,530) | 253,642 | 253,816 (+174) |

`-Wall -Wextra` on every file v4.4 changed that the boards compile, each board's own command, beside
v4.3's copy compiled the same way: the counts and kinds of v4.3 (`vga.c` 3, the unused
`scale_3_2`, `scale_3_3`, `scale_2_1`; `pc.c` 1, a `%x` for a `u32`; `console.c`, `crash.c`,
`display.c`, `esp_main.c`, `lcd_korvo1.c`, `setup.c`, `speed.c` 0; `ini_edit.c`, new, 0).

Instructions in the built Korvo-1 image, v4.3's release ELF against 40b4513's [verified: `dis44.sh`]:

| Function | v4.3 | v4.4 part 1 | What the difference is |
|-|-|-|-|
| `iomem_write8`, `16`, `32` (the LFB's stores) | 23, 32, 32 | 30, 36, 36 | the `vga_wrote` flag (`lui`, `li`, `sb`) |
| `iomem_write_string` | 55 | 57 | the flag |
| `vga_mem_write`, `16`, `32`, `_string` (the VGA window) | 185, 81, 92, 82 | 187, 82, 93, 84 | the flag |
| `pc_step`, `pc_vga_step` | 39, 39 | 39, 39 | the same instructions |
| `vga_take_marks` | 171 | 213 | the rows' parts, the KiB carried |
| `vga_graphic_refresh`, `vga_refresh` | 1,989, 835 | 2,030, 852 | the rows' parts; the font's test |
| `vga_ioport_write` | 253 | 271 | the map mask's request (29.3) |
| `vga_pace_after_step`, `display_pace_step`, `vga_set_fb` | | 21, 24, 3 | new |

### 29.12 What only a board can say [unknown]

* The drawing's time with the rows' parts (`draw_ms` in T386MON's fill, scroll, text and sprite
  at 800x480), and whether the fills then keep 33 pictures a second.
* Double buffering: that the panel comes back after the restart at start-up, that the driver's
  frame-buffer-complete event comes for each swap (the `display:` line's swaps not reported stay
  0), that no frame is torn (Plat's colour bands), the copy's and the wait's time, and the
  pictures a second in the full copy.
* Pacing: how it looks; the pictures drawn late and at a quiet moment (the `display:` line).
* The save: on Plat's card, a save after Ctrl+Alt+F11, F10 and F9, the file afterwards, and the
  boot console after a restart.
* The grey text screen gone at a restart from 16 colours.

### 29.13 For Plat to try and decide

* **Try**: T386MON's tests at 800x480 again: the drawing's milliseconds should fall in fill,
  scroll, text and sprite (29.4). Then Ctrl+Alt+F10 on and off while moving windows and typing;
  and `double_buffer = 1` (or Ctrl+Alt+F9 and a restart) for the colour bands, with the boot
  console's memory lines photographed (the pool and the guest's memory, 29.6).
* **Decide**: whether double buffering is worth 1M of Windows' memory; whether pacing should be
  on by default for Windows use.
* **The hardware mouse pointer** (his "judge this based on the test numbers"): T386MON's text test
  runs 3.79 frames a second with the pointer hidden against 3.50 with it over the text at 16 bits
  on the harness, and 3.09 against 2.90 on the board: a pointer the guest never draws would win
  about 7 per cent there and nothing where the pointer is still, for an emulated VMware SVGA II
  (25.6). Not worth its size now [inferred].

### 29.14 Where things are

On the branch: this section, and `s31\checks\` (text only): `host-tests-v4.4-part1.log`,
`flicker\harness-v4.4.diff`, `flicker-harness-v4.4-part1.txt` (the summaries and tables of the
runs named here), `drawing-v4.4-part1.txt`, `win95-pc-runs-v4.4-part1.txt`, `dis-v4.4-part1.txt`,
`size-korvo1-v4.4-part1.txt`, `size-dosboard-v4.4-part1.txt`,
`warnings-wall-wextra-v4.4-part1.txt`.

WSL `~/org2/v44disp/` (all made here, throw-away): `fh/` (the flicker harness copy, `harness-v44.diff`,
`bin/`, `runs/`), `ph/` (the T386MON harness copy, the standard runs), `t-*` (`git archive` of each
build), `bt-*` (the in-tree firmware builds), `ht*` (host test outputs), `w95files/` (Windows'
DLLs read to find the hot code: never to leave WSL), the scripts (`mk.sh`, `fr.sh`, `tm.sh`,
`tab.sh`, `seg.py`, `tl.py`, `sprite.py`, `sp2.py`, `winpics.py`, `std44.sh`, `finfh.sh`, `bld.sh`,
`dis44.sh`, `wall.py`, the `hpatch*.py` and `ed*.py` that made the changes), and the numbered
`s*.sh`, one step each (a look, a set of runs, a commit), with their outputs beside them.
The harnesses delete each run's copy of the Windows image when the run ends, except where v4.3's
list marks a run `@keep` (the installs) and the DOS fix runs: 9 copies of 400 MB are left in
`fh/runs/` (`*-gi`, `*-gi-rel`, `*-dosfix` of the `f`, `g` and `s` sets), not deleted here; with
them the folder is 11 GB, all of it throw-away. On Windows the session's scratch folder `v44d\`
holds the scripts as written. Nothing started for this is still running.

## 30. v4.4, part 2 of 2: a card partition as the guest's whole disk, the new card, and v4.4 (2026-10-24)

Plat on the guest's disk: "I love the idea of giving the guest raw SD card access, perhaps on a
partition though, instead of the whole card? This would also make it easier to run fsck on, copy
files to/from, etc. I've not moved it to the faster SD card yet, that would probably be at the same
time I guess." This part gives the guest one partition of the card, raw, lays out the new card (a
V30 A1 U3) for it with a script, proves both on the PC harness, and builds and packages v4.4 with
part 1's display work (section 29). Same branch, `s31-v4.4` in WSL `~/fw/wt-v44`, every commit by
Platima with his GitHub noreply address and the Co-Authored-By line. Nothing was flashed, no
serial port was opened, and nothing of v4.4 has run on a board. The tag `s31-v4.4` and the branch
go to Plat's fork at the end (30.13).

### 30.0 In one screen

* **`hda = sd:3`** (30.2): the guest's hard disk is card partition 3 itself, read and written raw.
  Its LBA 0 is a master boot record made for it (our own boot code, the card's disk signature, one
  active entry listing the partition at the sector where it starts on the card); the partition's
  sectors are the card's, at the same numbers; every other sector reads as zeros and refuses
  writes. The disk ends at the partition's end, rounded up to a cylinder. So the volume stays an
  ordinary FAT16 or FAT32 volume of the card, its hidden-sectors field right for the guest and for
  a PC alike.
* **Why that shape works for Windows 95** (30.2): SeaBIOS as tiny386 builds it uses no CHS
  translation, and gives INT 13h's LBA calls for everything; Windows 95 OSR2's FAT16 boot sector
  finds its own partition entry by the hidden-sectors field and reads by LBA when that entry's
  type is 0Eh or 0Ch [verified: its bytes, read on the PC]. With p3 at 516 MiB, past all CHS can
  reach, Windows 95 starts and runs with C: in protected mode, not "MS-DOS compatibility mode"
  [verified: harness, `IOS.LOG`].
* **Refused, each in plain words** (30.3): a card with a GPT or no table; a partition that is not
  FAT16 or FAT32 (Linux's root among them); one that overlaps another, starts at sector 0, runs
  past the card or past 128 GiB, is under 1 MiB, or FAT16 over 4 GiB; a FAT volume bigger than its
  partition; `sd:N` for a CD or floppy, or with anything else the ini names on that partition; and
  any partition tiny386's own FatFs has mounted. The firmware unmounts `/sdcardN` for the guest,
  and never `/sdcard`, where its inis are.
* **The new card** (30.4): `s31/newcard/newcard.sh` (WSL or any Linux; an image file, never a
  device) lays out p1 `KORVO_SD`, then p3 Windows' own FAT16 volume taken out of `win95.img`
  sector for sector, then p2 Linux's root copied from the old card; the table lists them p1, p2, p3,
  so Linux's `root=/dev/mmcblk0p2` holds. `export-root.sh` copies the root off the old card on the
  board's Linux. `s31/newcard/README.md` is the procedure. The old card's table, Linux root and
  `win95.img` are not changed; it gets files added to its two FAT partitions (v4.5: "never
  written", as this line said, was not so; 31.6).
* **Proved on the PC** (30.5): Windows 95 reached the desktop from p3 of card images `newcard.sh`
  made from a copy of the pristine `win95.img`, with p3 at 516 MiB and at 68 MiB, writing what it
  writes from `win95.img`, the card's table, p1 and p2 unchanged to the sector.
* **v4.4 packaged** (30.9 to 30.12): `build.sh` twice from nothing, the same bytes, and the same
  bytes as an in-tree build of the tag; 53 host tests pass; the dual-boot checks give v4.3's
  results with the new image; the standard Windows 95 set, the flicker harness's 28 runs and
  T386MON give v4.3's and part 1's END lines, every picture right. No new warning.

### 30.1 The commits [verified: `git log de24383..s31-v4.4`]

| Commit | What |
|-|-|
| 65ddbe4 | `esp/main`, `ide.c`: a card partition as the guest's whole disk: `sd_part.c`, `sd_part.h`, `sd_part_mbr.s` and its bytes `sd_part_mbr.h` (new); `storage.c` (the card's FatFs drive and size, a partition mounted or not, `/sdcardN` let go); `disk_image.c` (`disk_image_count()`: the partition's accesses in the same figures); `ide.c` (`sd:N` attached); `esp_main.c` (the ini's checks and console lines) |
| 09e7e9f | `tests/host`: `sdpart/run_sdpart.sh` and `test_sd_part.c` (30.7); the disk test links `sd_part.c` |
| f97686c | `s31/newcard`: `newcard.sh`, `export-root.sh` |
| 633af04 | `s31/card`: `win95-sd.ini`; the DOS board's ini gets `;hda = sd:3` |
| 6981df3 | `esp/version.txt`: `v4.4` |
| 5ddf26f | `sd_part.c`: its state taken from PSRAM when a partition is opened (it was 4.8 KiB of static internal RAM; 30.8) |
| e83f0da | `tests/host`: `test_ini_edit` in a short folder when the output folder is too long for `setup_find()` (part 1's test failed the long-folder run of the checks; 30.7) |
| (the tag) | `s31/`: this section and 26.3's last line, `TEST-PLAN.md` 16 and three lines of 14e, 14f and 15c, `FIRMWARE.md`, `FLASH.txt`, `README.md`, `build.sh`'s first comment, `newcard/README.md`, and the checks (30.17) |

`git diff --stat de24383 e83f0da -- . ':!s31'`: 18 files changed, 2,037 lines added, 21 taken out.
For the whole of v4.4, `git diff --stat esp32-s31 e83f0da -- . ':!s31'`: 39 files changed, 4,580
lines added, 82 taken out. Every commit's files were looked at: no binary, no picture, nothing of
Windows'.

### 30.2 What the guest sees, and why [verified: code, `test_sd_part.c`, the harness]

**The disk.** `sd_part.c` reads the card's sector 0 and the partition's first sector through
FatFs's disk functions on the card's own drive (the one `/sdcard` is on; `disk_read()` of the
physical drive, so the host test and the PC harness run the same file on a card image), checks
them (30.3), and keeps:

* LBA 0: our boot code (356 bytes, `sd_part_mbr.s`), the card's disk signature, entry 1 active
  with the card's own type byte, CHS bytes at 16 heads and 63 sectors (1023/15/63 past CHS), the
  partition's real start and length, entries 2 to 4 empty, 55AAh;
* the partition's sectors: the card's, at the same numbers, through a 2 KiB buffer in internal RAM
  the SD host reaches by DMA; a card error is tried once more and counted with the images'
  (`disk_image_count()`: the speed screen's Disk line and the information port see them);
* everything else: zeros on reading, refused on writing, every sector of a write inside the
  partition or none of it, for each transfer of up to four sectors as `ide.c` passes them (v4.5:
  so a guest command that runs across the partition's end has its sectors inside written and then
  fails; 31.4). A refused write is the guest's mistake, said on the console (the first
  four, then every hundredth) and not counted as the card's;
* the size: to the partition's end, rounded up to a whole cylinder of 16 x 63. The card's size
  would add only sectors that read zeros and refuse writes, and would hand Windows 95 a disk over
  32 GB, or the IDE emulation one past its 28-bit LBA (tiny386 has no 48-bit commands), on a big
  card. With the default layout the guest's disk is 916 MiB.

The boot code moves itself to 0000:0600, finds the active entry, reads the partition's first
sector by INT 13h AH=42h if the BIOS has the LBA calls (AH=41h), else by the entry's CHS bytes,
checks 55AAh and jumps to it with DL the drive and DS:SI and DS:BP the entry, as a DOS MBR does;
on a failure it says so and hands the boot back (INT 18h). `run_sdpart.sh` assembles the source
with GNU as every run and checks that `sd_part_mbr.h` holds the same bytes.

**Why the real start, and not the partition moved to sector 63.** The volume's boot sector says
where it is: the hidden-sectors field. A PC and Linux ignore it; Windows 95's boot sector uses it,
and more than for its arithmetic: the OSR2 FAT16 boot sector (OEM name MSWIN4.1, as in
`win95.img`) reads the MBR, looks for the entry whose start equals its own hidden-sectors count,
and takes that entry's type into its third byte, where 0Eh (0Ch is made 0Eh) switches all its
reads to INT 13h AH=42h [verified: its bytes 0x6D to 0x93 and 0x1F5 read on the PC; not copied
here]. So with the partition at its real start and the field set to it (`newcard.sh` does), the
same volume boots in tiny386 and mounts on a PC; with the partition shifted, one of the two would
be wrong.

**SeaBIOS.** tiny386's SeaBIOS is built with `CONFIG_QEMU=y` (`seabios/config`), so it takes the
disk's CHS translation from CMOS byte 39h, which tiny386 never sets: translation none [verified:
`seabios/config`, SeaBIOS's `get_translation()`, `ide_fill_cmos()`]. INT 13h's CHS calls then reach
the first 1024 x 16 x 63 sectors (504 MiB), and its LBA calls everything. Our boot code and
Windows 95's boot sector both use the LBA calls for a type 0E partition, so the partition can be
anywhere below 128 GiB; with p3 at 516 MiB on the harness Windows 95 started in every run (30.5).

**Windows 95 itself.** With a type 0E partition its real-mode IO.SYS reads by the LBA calls, and
its protected-mode IDE driver by IDENTIFY's LBA [inferred: the harness run starts and writes
normally with p3 past CHS]. `IOS.LOG`, which Windows writes at each start, names the units that go
through real-mode drivers: after a run from p3 at 516 MiB only units 00 and 01, the two floppy
drives, as after a run from `win95.img`; C: (unit 02) is not there, so it is in
protected mode, not "MS-DOS compatibility mode" [verified: harness]. The pristine image's own
`IOS.LOG`, written at its install before `fill_cmos`, lists unit 02 as well.

**Linux on the dual boot.** Linux mounts p1 at `/mnt/sd` and its root by number
(`root=/dev/mmcblk0p2`, `S10sdcard` in the fork) [verified: D:\Repos\esp32-s31-linux, read only];
the order of the partitions on the card does not matter to it. tiny386 and Linux never run
together, so p3 may be mounted by Linux too (it is an ordinary FAT16 volume) [inferred].

### 30.3 What is refused, and when [verified: `test_sd_part.c`, the harness]

`esp_main.c`, after reading the ini and before the PC is built, for each `hdX = sd:N`:

1. `sd:` and one digit 1 to 4, else a red line saying how to write it (`sd:0`, `sd:5`, `sd:33`,
   `sd:p3` all refused; `SD:3` is a file name, as before);
2. a hard disk only: `cda` or `fda` naming `sd:3` is refused;
3. nothing else the ini names may be on `/sdcardN` (a floppy, another disk, the BIOS);
4. `storage_part_release(N)`: unmounts `/sdcardN` (FatFs, the VFS, the logical drive), says so on
   the console, and refuses if N is `/sdcard`;
5. `sd_part_check(N)`, which `ide.c`'s `sd_part_open()` repeats when the PC is built. It refuses:
   no card; the partition still mounted by FatFs (so tiny386 never starts with both); a card with
   no 55AAh or one FAT volume without a table; a GPT (an EEh entry); an empty slot; a type other
   than 04, 06, 0E (FAT16) or 0B, 0C (FAT32), named in words for Linux (83), extended (05, 0F, 85),
   NTFS or exFAT (07), FAT12 (01), swap, LVM and EFI; a start of 0; an end past the card or past
   2^28 sectors; under 2048 sectors; FAT16 over 4 GiB; an overlap with any other entry; a FAT volume
   with sectors other than 512 bytes, or bigger than its partition.
   It warns, in yellow, without stopping: a hidden-sectors field that is not the partition's start
   (the guest may not boot from it); no FAT volume yet (the guest sees an empty disk to FORMAT);
   a type that says FAT16 for a FAT32 volume or the other way.

Step 5's refusals and warnings are cases of `test_sd_part.c` on made-up sectors (40 checks; swap,
LVM and EFI go the way Linux's type does and are not each tried, nor is the FAT16-for-FAT32 warning),
and the mounted partition, p1, Linux's root and the empty slot also on card images there and on
the harness, each stopping the PC with its line [verified]. (v4.5: steps 1 to 3 are now
`sd_part_ini_check()` in `sd_part.c`, in test 14b, and the kernel, initrd and linuxstart keys are
among the files checked; 31.4.) Steps 1 to 4 are `esp_main.c`'s own and
in no host test: they were read, and the board's console is where they show [inferred].

### 30.4 The new card [verified: `newcard.sh` on the PC; the board unknown]

`newcard.sh --win95 WIN95.IMG --root ROOT --out NEW-CARD.IMG [--p1 DIR] [--p1-mib N] [--root-mib N]
[--mbr OLD-MBR]`: the Windows volume is the active (or first) FAT partition of the image's own MBR,
its length from its boot sector; the root is a bare ext4 image or a whole-card image's first ext4
partition (type 83). Layout: p1 FAT32 `KORVO_SD` from 8192 (512 MiB by default; one sector a cluster
below 260 MiB, where FAT32 would otherwise have too few clusters), p3 straight after it (aligned to
1 MiB, the volume's size), p2 after p3 (the root's size, or grown with `resize2fs`). It copies a
folder onto p1 with `mcopy` (attributes and times kept), the volume onto p3 with `dd`, sets the
boot sector's hidden-sectors field (and FAT32's backup boot sector's) to p3's start, copies the
root onto p2, keeps the old card's disk signature if given its first sector, and then checks: the
table read back; `fsck.fat -n` on p1 clean; on p3 the same findings as on the image's own volume
(Windows 95 leaves its label blank, which `fsck.fat` calls not valid on both); p3 against the
volume sector for sector but the one field; every file and folder as `mtools` lists them (1,801
lines for the pristine install); p2 against the root sector for sector, and `e2fsck -fn` clean. It
writes only `OUT` and a work folder beside it, refuses an `OUT` under `/dev`, and ends `ready`
only if every check passed.

From a copy of the pristine `win95.img` (400 MiB volume, 818,433 sectors), a stand-in root (192
MiB ext4) and a first sector with disk signature deadbeef: p1 from 8192, p3 from 1,056,768 (516
MiB), p2 from 1,875,968, a 1,108 MiB image; with `--p1-mib 64`, p3 from 139,264 (68 MiB) and a 660
MiB image. Every check passed both times, and the signature was kept
[verified: `checks\partition-passthrough-v4.4.txt` 1].

`export-root.sh` (POSIX sh, BusyBox): finds p2's mount point (also a root the kernel mounted, which
`/proc/mounts` calls `/dev/root`), makes it read-only for the copy and read-write after, or stops
without copying; copies p2 and sector 0 to the WIN9X partition (mounting it at `/mnt/win` if need
be) with a SHA-256 of each. Run on the PC in its test mode (`EXPORT_DEV`, files for the card) under
`dash` and `bash`: both files byte for byte; a missing folder and a missing p2 refused in words;
and `newcard.sh` took its output [verified]. On the board [unknown].

### 30.5 Windows 95 from p3 on the PC harness [verified: `checks\partition-passthrough-v4.4.txt` 2 and 3]

The harness (part 1's T386MON harness copy) builds `sd_part.c` beside the firmware's core, answers
`storage.c`'s three questions from the card image (`diskfat.c`), and for `SDPART=3` runs `hda =
sd:3` with p1 mounted by FatFs, as the board mounts `/sdcard`; afterwards it puts the partition
back into the image's shape (the image's first 63 sectors, the volume, the field back to 63) so
that `diskcheck.py` can set it against the pristine image. 14M and 320K as `win95-14m.ini`, the
board's card timing (300 us a read, 900 a write):

| Run | The disk | Outcome | Virtual s at the END line | Sectors Windows changed | `IOS.LOG`: real-mode units |
|-|-|-|-|-|-|
| `pp-sd512` | `sd:3`, p3 at 1,056,768 (516 MiB) | desktop at 222 s | 342.0 | 6,508 in 146 ranges | 00, 01 (the floppies) |
| `pp-sd64` | `sd:3`, p3 at 139,264 (68 MiB) | desktop at 223 s | 343.0 | 6,864 in 138 ranges | 00, 01 |
| `pp-filecard` | `win95.img` on a FAT32 p3, as Plat's card is now | desktop at 224 s | 344.3 | 7,220 in 153 ranges | (not read) |
| `pp-file` | `win95.img`, a plain file | desktop at 210 s | 330.0 | 6,533 in 139 ranges | 00, 01 |

* After each `sd:3` run the card's sector 0 to p1, p1 and p2 are as `newcard.sh` made them, every
  sector, and `fsck.fat -n` finds on p3 what it finds after a run from `win95.img`: the blank label
  and the dirty bit Windows sets while it runs (the runs end at the desktop, not shut down), the
  same 1,645 files.
* The first changed ranges of `pp-sd512` are `pp-file`'s; the others differ a little, as the card's
  timing moves Windows' own writes about. The guest instructions to the END line are within half a
  per cent of each other [verified].
* Refused, each stopping the PC with its line: `sd:3` with p3 mounted by FatFs ("card partition 3
  is mounted by tiny386's own FatFs, so the guest cannot have it whole as well"), `sd:2` ("a Linux
  partition (Linux's root is one) (type 83): only a FAT16 partition ... or a FAT32 one ... can be
  the guest's disk"), `sd:1` (mounted, as `/sdcard` is on the board), `sd:4` ("the card has no
  partition 4") [verified]. On the board `esp_main.c` says the first three in red before the PC is
  built (30.3) [inferred: the same code].

### 30.6 What a PC and Linux make of the new card [inferred, unknown]

* Windows 10 and 11 mount every FAT partition of a removable card since 1703, so p1 and p3 get
  letters and p2 none [inferred]. `chkdsk X:` without `/f` only reads.
* If Windows 95 was switched off without shutting down, its volume's dirty bit is set (the
  harness's runs end at the desktop, and `fsck.fat` says so on p3 and on the image alike): Windows
  10 may then offer to scan the drive [inferred].
* The board's Linux mounts p1 and boots its root as before; it can mount p3 as `vfat` [inferred].

### 30.7 Host tests [verified: `checks\host-tests-v4.4.log`]

53 tests, all pass (68 s): part 1's 52 and `sd_part.c` (14b). 14b,
`tests/host/sdpart/run_sdpart.sh`, 12 results: the boot code against its source; 40 checks of
`sd_part_plan()`, `sd_part_name()` and the CHS bytes on made-up sectors; then with ESP-IDF's own
FatFs, `storage.c`'s partition code as the file has it and `ide.c` as the board builds it, a card
laid out as `newcard.sh` makes it (53 checks: refused while mounted, p1 never let go, p2 and p4
refused, IDENTIFY and READ and WRITE SECTORS through the IDE ports, reads outside zeros, nine kinds
of write outside refused with nothing reaching the card, a refused read and a refused write got
over and counted, a card that refuses everything, the guest's own view of its disk (FatFs finding
C: from the guest's LBA 0) writing two files, and a second `sd:3` with p3 mounted again stopping),
after which `mtools` reads both files back, `fsck.fat` finds p1 and p3 clean, and the card's table,
p1 and p2 are as they were, every sector; and Plat's card as it is (p3 FAT32 WIN9X, 9 checks).

With the 115-character output folder of the checks (as every round runs them), part 1's
`test_ini_edit` failed one check: `setup_find()` keeps its folder in 96 bytes and found nothing
there. On the board the folder is `/sdcard/tiny386`; the test now works in a short folder in
`TMPDIR` when the output folder is too long, as `test_setup.c` does (e83f0da) [verified:
`checks\host-tests-v4.4-long-out.log`, 53 of 53]. From the tree `build.sh` makes from the patch, 43
pass: the ten that set this tree against an earlier release need its git tags (`pristine`,
`v4.0-base`, `s31-v4.1`, `s31-v4.3` and the like), which a tree made from the patch has not got, as
v4.3's 37 of 45 [verified: `checks\host-tests-v4.4-from-patch.log`]. The host build's warnings are
v4.3's lines [verified].

### 30.8 Builds, warnings, sizes [verified: `checks\size-*-v4.4.txt`, `checks\warnings-wall-wextra-v4.4.txt`]

The five builds of every round (the Korvo-1 dual boot and standalone, the DOS board, and the two
crash-test builds) build with the one old warning (`i2s_task` unused, the Korvo-1). `-Wall -Wextra`
on the files part 2 changed (`sd_part.c`, `storage.c`, `disk_image.c`, `esp_main.c`, `ide.c`):
no warning, on both boards, as part 1's copies [verified].

| Build | DIRAM used | v4.4 part 1 | v4.3 | Total image size |
|-|-|-|-|-|
| Korvo-1 (dual boot and standalone) | 251,594 | 251,354 | 250,644 | 929,278 |
| DOS board | 254,040 | 253,816 | 253,642 | 955,836 |

The first build of `sd_part.c` kept its state static: 4.8 KiB more internal RAM on both boards for
a feature most inis never use. 5ddf26f takes it from PSRAM when a partition is opened; until then
the passthrough costs no internal RAM, and then 2 KiB (the DMA buffer) [verified: the sizes above].

### 30.9 `build.sh` and the images [verified: `checks\reproducible-v4.4.txt`]

`build.sh` from `s31/` with its first comment moved on (v4.4, and where the patch comes from on the
branch), and the release's two patches made from the branch with `s31/` left out: `git diff 4ce2e06
e83f0da -- . ':(exclude)s31'` (161 files, 34,829 lines added, 723 taken out; 1,375,076 bytes) and
the same against 9ed786b (168 files, 35,459 lines added, 738 taken out; 1,403,183 bytes); neither
holds a binary. v4.3's patch made the same way from `s31-v4.3` is the package's v4.3 patch byte for
byte, so the branch's history gives the release's tree [verified]. `dependencies.lock` is v4.3's.
Two runs from nothing, each in a clean environment: 174 s and 175 s, both exit 0, the 23 files of
the two image trees the same. Built in the tree at the same code with the same build folder names
(`build-korvo1`, `build-korvo1-standalone`, `build-dosboard`), the three `tiny386.bin` are
`build.sh`'s byte for byte, and the standalone and DOS board's bootloader and table too. (With other
folder names, `build-k` say, the ELF and so the app's ELF hash differ: the folder's name is in the
build; same names, same bytes.)

| Image set | `tiny386.bin` | of its partition | SHA-256 | v4.3's |
|-|-|-|-|-|
| `korvo1-dualboot` (and `-tiny386-default`) | 929,456 bytes | 1,966,080 (47.3 per cent) | `1ca5672cdee56e42` | 906,592 |
| `korvo1-standalone` | 929,456 bytes | 1,835,008 (50.7 per cent) | `2c981ad95cf1f0e2` | 906,592 |
| `dosboard` | 956,016 bytes | 1,835,008 (52.1 per cent) | `ee5ea4ecde85f888` | 938,272 |

Every bootloader, table and `flash_args` is v4.3's byte for byte; only `tiny386.bin` and `tiny386.elf`
changed. `esptool image-info` finds each image and bootloader valid, App version `v4.4`; its output
is the same in both runs [verified: `checks\image-info-*-v4.4.txt`].

### 30.10 The dual-boot checks [verified: the files named]

Each of v4.3's, run again with the v4.4 images in the package's `images\`:

* `flash-simulation-v4.4.txt`, `flash-simulation-bootswap-v4.4.txt`: the dual-boot work's flash
  model and the boot swap's (paths 1 to 19, cut-off cases C1 to C5): against v4.3's, only the app's
  size, checksums, hashes and erase ends differ (every line that differs is of one of those kinds,
  counted in `checks44`'s B3).
* `bat-harness-v4.4.txt`: the three scripts and `flash-paths.cmd` (unchanged since v3.9) run in
  `cmd.exe` with a stub esptool, PATH limited to `C:\Windows\System32` and `C:\Windows`: 103 cases,
  every exit code, write line and esptool call v4.3's; no write reached the stub; 47 App version
  lines `v4.4`.
* `app-update-v4.4.txt`: v4.3 to v4.4 by `flash-dualboot.bat COM10 app` alone, simulated: every byte
  changed is inside tiny386's partition (0xE20000 up); the bootloader, the table and all of Linux
  as before; the result the same as writing the whole set with v4.4 from be5b725.
* `cmake-flash-refusal-v4.4.txt`: the dual-boot build's flash targets still refuse, as v4.3's.
* `status-led-v4.4.txt`: the LED's panic path in internal RAM; `build.sh`'s bit-bang layout check
  passes for all three ELFs, both runs.
* `card-check-v4.4.txt`: `setup.c` and `ini.c` of v4.4 (now with `ini_edit.c`) on a pretend card
  with the package's card files: the boot menu lists six inis, `win95-sd.ini` among them,
  `tiny386.ini` the default; every file each names is there, `hda = sd:3` taken as a partition;
  every `[esp]` and `[pc]` key the inis name, commented or not, is one `esp_main.c` knows; 0
  problems. The checker, `~/tiny386-idf-scratch/v3-final/cardcheck.c`, resolved `sd:3` as a file
  beside the ini until it was taught what `esp_main.c` does with it (`checks\card-check-v4.4.c`);
  the three Windows inis differ from v4.3's by their commented lines, as they should.

### 30.11 The PC harness runs [verified: `checks\win95-pc-runs-v4.4.txt`, `checks\flicker-harness-v4.4.txt`]

v4.4 as packaged (e83f0da), built into both harness copies, each run against v4.3's run of the same
configuration and against part 1's:

* **The standard Windows 95 set** (`std44s.sh`, as v4.3's `std43.sh`): the 27 standard
  configurations at the desktop with v4.3's END line and final picture, 27 of 27; the 11 native
  (VMDisp9x) configurations at the desktop, v4.3's, the audited ones with no picture differing from
  a full redraw; BVBE480's 21 runs, every picture right and v4.3's END lines; BENCH, 11 of 11 marks at
  v4.3's instruction counts; the install through the dialogs (gi18) at the desktop at v4.3's second,
  35,845 pictures held against a full redraw and none different (v4.3 had 1, the grey screen of
  26.3). Against part 1's runs: all the same (27, 11, 21, BENCH, gi18).
* **The flicker harness** (`finfh.sh`, as part 1's `s99.sh`): v4.3's 28 runs, every picture equal to
  the guest's screen and v4.3's END line, 28 of 28, and part 1's END line, 28 of 28; the DOS programs
  (mode 13h at 256K and 768K, the BIOS modes), the review's fb-vga, and T386MON 1.2's m1 session at
  16 colours and at 16 bits: every picture right, part 1's END lines, and `T386MON.LOG` the same bytes
  as part 1's.

So nothing of part 2 reaches the emulator's behaviour with a disk image, as it should not: `ide.c`
takes the `sd:` path only for an `sd:N` name.

### 30.12 The package folder [verified: `checks\sums-v4.4.txt`]

`D:\Repos\esp32-s31-linux-release\tiny386-idf\v3`, where Plat flashes from: v4.3 as released moved
to `previous-v4.3\` (its four image sets, both patches, `build.sh`, the docs and its `SHA256SUMS`,
and v4.3's three card inis that v4.4 changes, so that its list still finds every file: 174 files, 32
inside it and 142 unchanged in `v3\`); the v4.4 images, patches and `build.sh` in their places; from
the branch the card files that changed (`win95-14m.ini`, `win95-800x480.ini`, `win95-800x600.ini`)
and `win95-sd.ini`, `newcard\`, `BUILD-NOTES.md`, `TEST-PLAN.md`, `FLASH.txt`,
`REVIEW-RESOLUTION.md` and the v4.4 checks. Its `README.md` now says first that the `esp32-s31`
branch is the source of truth and this folder a copy to flash from. `SHA256SUMS`: 180 lines, all OK
(30.16).

### 30.13 The tag and the push [verified: `git`]

The tag `s31-v4.4` (annotated, tagger Platima) is on the commit that adds this section and the
checks; its tree's code is e83f0da's (`git diff e83f0da s31-v4.4 -- . ':!s31'` is empty), so the
patch, the images and every run above are the tag's. In the canonical repository `~/fw/tiny386-s31`
the branch `esp32-s31` moves on to it (a fast-forward from 8d4b758, v4.3 and `s31/`). The branch and
the tag then go to github.com/platima/tiny386 from Plat's clone `D:\Repos\tiny386` (WSL's git has no
credentials; the clone fetches both from the canonical repository), once
`git log --format='%an <%ae> | %cn <%ce>' origin/esp32-s31..esp32-s31` shows
Platima <13729856+platima@users.noreply.github.com> alone, author and committer, the tagger is the
same, and no other address is named in a message. Only those two refs go: the scratch repository's
old tags `v3` to `v4.3`, which `~/fw/tiny386-s31` also holds, and the work branch `s31-v4.4` stay on
this PC. The push comes after this commit, so its result is in the round's report, not here.

### 30.14 What only a board can say [unknown]

* Everything of 29.12.
* `hda = sd:3` on the card: the console's lines, Windows' start and speed from p3 (no FAT, no cluster
  map: likely no slower than from `win95.img` [inferred]), the speed screen's Disk line, a clean
  shutdown, `chkdsk` on the PC after.
* `export-root.sh` on the board's Linux (whether its root goes read-only), the image written with
  Plat's image writer, Linux from the new card, the new card's speed (V30 A1 U3) against the old.
* The DOS board with `sd:3` (its `storage.c` is the same) [inferred].

### 30.15 For Plat

* **Try** (`TEST-PLAN.md` 16): the keys' saves, the two cures for tearing, then the new card by
  `newcard\README.md`; photographs as 16f lists.
* **Decide**: 29.13's two (double buffering for 1M of Windows' memory, pacing by default); and how
  big p1 should be on the new card (512 MiB by default; `--p1-mib`), and whether the root should
  grow (`--root-mib`).
* **Keep the old card** as it is until the new one has run Windows and Linux both.

### 30.16 `SHA256SUMS`

The top list is v4.3's 174 files in v4.3's order, then six new ones: `newcard\`'s three files,
`card\tiny386\win95-sd.ini`, and the two harness changes, `checks\flicker\harness-v4.4.diff` (part 1's)
and `checks\sdpart-harness-v4.4.diff`; 180 lines, every one OK. Against v4.3's list, 19 files'
hashes changed: the docs (`README.md`, `BUILD-NOTES.md`, `TEST-PLAN.md`, `FLASH.txt`), `build.sh`, the
three Windows inis, the two patches and their diffstats, and the seven `tiny386.bin` and
`tiny386.elf` files; every bootloader, table and `flash_args` is v4.3's. Each image folder's own list
checks. Every earlier list, `previous-v4.3\` to `previous-v3.2\`, finds every one of its files with its
hash, in itself, in `v3\` or in a later `previous-*` folder: not one missing. The T386MON folder's
list, unchanged by v4.4, checks: 161 of 161 [verified: `checks\sums-v4.4.txt`]. As before the list
leaves out `research\FLICKER-PLAT-NOTES.md` and `checks\` but its harness changes.

### 30.17 Where things are

On the branch (`s31\`): this section; `newcard\` (`newcard.sh`, `export-root.sh`, `README.md`);
`card\tiny386\win95-sd.ini`; `TEST-PLAN.md` 16; `checks\` (text only): `host-tests-v4.4.log`,
`host-tests-v4.4-long-out.log`, `host-tests-v4.4-from-patch.log`, `partition-passthrough-v4.4.txt`,
`reproducible-v4.4.txt`, `build-sh-v4.4-run1.txt`, `build-sh-v4.4-run2.txt`, `image-info-*-v4.4.txt`
(six), `size-*-v4.4.txt` (three), `warnings-wall-wextra-v4.4.txt`, `flash-simulation-v4.4.txt`,
`flash-simulation-bootswap-v4.4.txt`, `bat-harness-v4.4.txt`, `app-update-v4.4.py` and `.txt`,
`cmake-flash-refusal-v4.4.txt`, `status-led-v4.4.txt`, `card-check-v4.4.txt` and the checker's copy
`card-check-v4.4.c`, `win95-pc-runs-v4.4.txt`, `flicker-harness-v4.4.txt`,
`sdpart-host-test-v4.4.log` (test 14b's own log), `bat-harness-collect-v4.4.py`, `sums-v4.4.txt`,
and `sdpart-harness-v4.4.diff` (the harnesses' change). The package's `checks\` has the same files,
and part 1's.

WSL `~/org2/v44sd/` (all made here, throw-away): `nc/` (the card images, the stand-in root, the
copy of `win95.img`: never to leave WSL), `ph/` and `fh/` (the harness copies, their bins and runs),
`rel/` (the stage and the two `build.sh` runs), `bt-c3/` (the in-tree builds), `chk/` (the checks),
`t-final`, `t-part1`, the scripts and their outputs. On Windows the session's scratch folder `v44s\`
holds the scripts as written and the batch harness's copy. Nothing started for this is still running.

## 31. v4.5: the two reviews of v4.4 (2026-10-24)

Two reviewers read tag `s31-v4.4` and its package, each with a lens of its own:
`REVIEW-v44-display.md` (the keys' saves, the double buffering, the pacing, the drawing, the grey
screen) and `REVIEW-v44-storage-and-safety.md` (`hda = sd:N`, the guest's disk, the refusals, the
new card, the dual-boot images, the push). No blocker and no major finding; eight minor findings
and thirteen notes, each checked against the tag before it was acted on [verified:
`checks\findings-v4.4-review.txt`]. Four minor findings needed the firmware changed, so this is
v4.5, a new tag: `s31-v4.4` was already on github.com/platima/tiny386 with its images in the
package, and a published tag is never moved. `REVIEW-RESOLUTION.md` round 11 has every finding and
what became of it. Same branch, a new work branch `s31-v4.5` in WSL `~/fw/wt-v45` of the canonical
repository `~/fw/tiny386-s31`, every commit by Platima with his GitHub noreply address and the
Co-Authored-By line. Nothing was flashed, no serial port was opened, and nothing of v4.4 or v4.5 has
run on a board.

### 31.0 In one screen

* **The ini saved without renaming anything** (31.2): a whole copy of the new text with a check
  line beside the ini, then the ini rewritten in place, then the copy removed. A cut in power at any
  step leaves the ini whole, as it was or saved, and never two names on one chain of clusters, which
  a cut inside v4.4's FatFs renames could [verified: a cut at each step on the PC; FatFs on a card
  unknown].
* **Double buffering** (31.3): Ctrl+Alt+F9 on the speed screen no longer leaves the console's text
  round a picture narrower than the panel; the speed screen counts the swaps and those the panel
  did not report; a panel that cannot even be started again is said in red, and the console stops
  drawing into a freed buffer [verified: the review's model, code; the board unknown].
* **`sd:N`** (31.4): refuses the old card's WIN9X partition (its label, or `win95.img` or
  `korvo-root.img` on it); a type 00 entry with a size counts as a partition; the guest's disk is
  capped at 28-bit LBA's 0FFFFFFFh sectors; a yellow note for a type Windows 95 reads by CHS past
  504 MiB; the ini's own checks moved into `sd_part.c` and tested [verified: host test 14b, the
  harness].
* **The new card** (31.6): `newcard.sh` gives p3 type 0E (0C) past 504 MiB; `export-root.sh` finds
  the root by its device number; the docs say what the procedure adds to the old card, and three
  things more to know [verified: stand-in images, test mode; the board unknown].
* **Checks** (31.7 to 31.10): 53 host tests pass, also in a clone with only the fork's tags; the
  release built twice from nothing gives the same bytes, and the same as an in-tree build; on the
  PC harness Windows 95 from p3 gives v4.4's END lines and writes to the sector; the dual-boot
  checks give v4.4's results; no new warning.

### 31.1 The commits [verified: `git log esp32-s31..s31-v4.5`]

| Commit | What |
|-|-|
| 47290db | `esp/main`: double buffering after the speed screen, its counts, a panel that does not come back (`speed.c`, `lcd.h`, `lcd_korvo1.c`, `esp_main.c`) (31.3) |
| 421aef6 | `esp/main`, `tests/host`: the ini saved with no rename (`setup.c`, `setup.h`, `esp_main.c`, `test_ini_edit.c`, `run.sh` 6b) (31.2) |
| f76aaab | `esp/main`, `tests/host`: `sd:N` refuses the WIN9X partition; the review's notes on `sd_part.c` (`sd_part.c`, `sd_part.h`, `esp_main.c`, `tests/host/sdpart`) (31.4) |
| 6e5e150 | `tests/host`: `run.sh` finds pristine and v4.0-base by their commits, and says when it skips (31.7) |
| dd51a62 | `s31/newcard`: p3's type past CHS; the root found by its device number (31.6) |
| 8196e20 | `esp/version.txt`: `v4.5` |
| (the tag) | `s31/`: this section, round 11 of `REVIEW-RESOLUTION.md`, the two reviews, `TEST-PLAN.md` 16, `FIRMWARE.md`, `FLASH.txt`, `README.md`, `build.sh`'s first comment, `newcard/README.md`, `card/tiny386/win95-sd.ini`, the corrections in 29.2, 29.6, 30.0, 30.2 and 30.3, and the checks (31.14) |

`git diff --stat esp32-s31 8196e20 -- . ':!s31'`: 13 files changed, 846 lines added, 172 taken out.
Every commit's files were looked at: no binary, no picture, nothing of Windows'.

### 31.2 The ini saved with no rename (display finding 2) [verified: code, `test_ini_edit.c`; the board unknown]

**Why.** ESP-IDF's FatFs `f_rename()` registers the new directory entry, copying the old one's
start cluster, and only then removes the old entry, between its own comments "Start of critical
section where an interruption can cause a cross-link" and "End of the critical section". When the
two entries are in different sectors, a cut between the two writes leaves both names on one chain.
v4.4 renamed twice, and its tidy then removed the leftover name, which frees the clusters the ini
still uses [verified: `ff.c`, the tag's `setup.c`]. `f_unlink()` removes the entry before the
chain, so a cut there leaves lost clusters at worst.

**v4.5's save** (`setup.c`, `setup_ini_save()`):

1. the new text into `NAME.ini.t386new`, followed by a line `;t386new LLLLLLLL CCCCCCCC` (between
   CR LF), the text's length and CRC-32 in hex, flushed, synced and read back;
2. the ini itself rewritten in place (`fopen "wb"`: FatFs keeps the name and its entry, frees the
   old chain and writes a new one), flushed, synced and read back;
3. `NAME.ini.t386new` removed. It was never renamed, so it shares no cluster with anything.

Before step 1 the save tidies its own ini's leftovers (a whole copy is put in place first, so a
second save never starts from a half-written ini), and it refuses, saying why, while what v4.4 may
have left wants a check.

**v4.5's tidy** (`setup_ini_tidy()`, which `setup_find()` runs for each folder at start-up), for
each `X` with a leftover:

| Found | Means | Done |
|-|-|-|
| `X`, `X.t386new` whole (its line checks) | a cut in step 2 or 3 | `X` made the copy's text if it is not, then the copy removed |
| `X`, `X.t386new` not whole or with no line | a cut in step 1, or v4.4 before its first rename | the copy removed; `X` untouched |
| `X.t386new`, `X.t386old`, no `X` | v4.4, a cut between its two renames (its new text had been read back) | `X` written with that text, then both removed |
| `X`, `X.t386old` with other bytes | v4.4, a cut before the old one was removed | the old one removed |
| `X`, `X.t386old` with `X`'s very bytes | v4.4, a cut inside a rename, perhaps one chain | both left; a yellow line: check the card on a PC (`chkdsk` with `/f`), then delete the `.t386old` |
| all three | v4.4, a cut inside a rename | all left, the same line |
| `X.t386old` alone | v4.4 | `X` written with its bytes, then it removed |
| `X.t386new` alone | `X` gone some other way | left |

Files are only ever written whole and read back before anything is removed, and only names with
the two endings are touched (and `X` only from them). The yellow line is `setup_tidy_warning()`,
printed after the boot menu's list is made. The warning's 320 bytes are the only internal RAM the
change costs (31.9).

**Tested** (`tests/host/test_ini_edit.c`, built with `-DSETUP_TEST_CUT`, which lets a test stop the
save at a step as a cut would; the board's build has none of it): 149 checks, 0 failed (v4.4: 106).
The ini rewritten in place, the same file before and after a save; a leftover copy cut short
removed and a whole one put in place before a save; v4.4's `.t386old` with other bytes removed and
with the ini's very bytes refused; then a cut at each of the five steps (the copy half written; the
copy whole; the ini emptied; the ini half written; the ini whole and the copy not removed),
followed by the next start's tidy: the ini as it was after step 1's cut and saved after the others,
nothing beside it, no warning, and `setup_find()` listing it with its description; the tidy's eight
states, v4.5's copy made by the test with a CRC-32 of its own; the two warnings. The same under
AddressSanitizer and UBSan: 149, 0 failed [verified: `checks\host-tests-v4.5.log`]. POSIX
`rename()` and `fopen` are not FatFs: what a real cut does on the card is [unknown], but no step of
v4.5's save renames, so FatFs's critical section is never entered.

### 31.3 Double buffering (display findings 1, 3 and 6) [verified: code, the review's model; the board unknown]

* **Ctrl+Alt+F9 on the speed screen** (finding 1). `lcd_double_set(true)` makes the back buffer the
  front one's copy, and while the speed screen is up the front holds the console. `give_back()`
  now clears the back buffer and writes it back (`memset`, `lcd_vga_flush()` of every row) before
  asking for the copy and the full update, as the one-buffer path clears its buffer; the back is not
  on show, so the panel never goes black. The review's model (`test_dbuf.c`, the review's own,
  rebuilt here against v4.5's `vga.c` objects from the host tests): 465 of 3,004 checks fail
  without the fix, every one after F9 turned double buffering on during the speed screen, and 0
  with the fix the model makes, which is what v4.5's `give_back()` does [verified:
  `checks\findings-v4.4-review.txt`].
* **The counts on the speed screen** (finding 3). The `double_buffer = 1` line now reads "... (so
  far N swaps, M of them not reported by the panel) ..." from two totals that are never reset
  (`lcd_double_totals()`); the UART's `display:` line still has the same per 10 s with the copy's
  and the wait's time.
* **No panel at all** (note 6). If the panel cannot even be started again with one frame buffer,
  the console is told there is none (`console_set_fb(NULL)`: it draws nothing, the UART still has
  every line), the pool gets its 752 KiB back, and the boot console says in red that only the UART
  shows what follows.

### 31.4 `sd:N` (storage finding 2, the firmware's side of finding 4, notes 6, 7, 8 and 10) [verified: host test 14b, the harness]

* **The WIN9X partition refused.** `sd_part_plan()` refuses a FAT volume labelled WIN9X (any case):
  "card partition 3 is labelled WIN9X: on a card laid out for the Linux route it holds win95.img,
  which the guest would see raw and could overwrite; sd:3 is for a card made by newcard.sh". And
  `esp_main.c`, while FatFs still has the partition at `/sdcardN`, refuses one holding `win95.img`
  or `korvo-root.img` (`sd_part_route_files()`), before it lets the partition go.
* **A type Windows 95 reads by CHS, past 504 MiB.** A FAT16 partition of type 04 or 06, or FAT32
  0B, that ends past sector 1,032,191 (`SD_PART_CHS_END`, 1024 x 16 x 63) gets a yellow note saying
  the guest may not start from it and which type to make it.
* **A type 00 entry with a size** now counts in the overlap check; only an entry with no size is
  passed over, as Linux reads a table.
* **28-bit LBA.** `SD_PART_LBA28` is now 0FFFFFFFh, the most sectors IDENTIFY's words 60 and 61 may
  give; a partition must end by then, and the guest's disk, rounded up to a cylinder, is cut there
  (v4.4 allowed an end at 2^28 and could round the disk up to 268,436,448 sectors).
* **The ini's checks in `sd_part.c`.** `check_card_partition()`'s steps 1 to 3 (the name, a hard
  disk only, nothing else the ini names on `/sdcardN`) are `sd_part_ini_check()`, which the host
  test calls; the kernel, initrd and linuxstart keys are among the files checked. Several files on
  the partition give one red line naming the first and how many more.
* **The wording.** "Every sector or none" is per transfer of up to four sectors, as `ide.c` passes
  them (`sd_part.h`, `sd_part.c`, 30.2).

Host test 14b (`tests/host/sdpart/run_sdpart.sh`): 14 results, all pass (v4.4: 12). The unit part
67 checks (v4.4: 40): the cases above (a partition ending at 0FFFFFFFh accepted with the disk at
0FFFFFFFh, one ending ten sectors short of it in the last part cylinder cut there, one ending at
2^28 refused; 06 and 04 past 504 MiB with the note, 0E without, 06 ending at sector 1,032,191
without; a type 00 entry with no size passed over, one with a size refused; a volume labelled
WIN9X, and win9x, refused) and `sd_part_ini_check()` and `sd_part_route_files()` (17 checks: the
names, `cda` and `fda`, one and two files on the partition, the kernel, initrd and linuxstart,
names that only look alike, the two route files asked for by their paths); the new card as v4.4's
(53 checks); Plat's card as it is now refused, `sd_part_check()` and `sd_part_open()` saying why,
`ide_attach()` stopping, nothing written and p2 and p3 the same to the sector; and the same card
with p3 relabelled DATA32 given whole and read as v4.4 gave it [verified:
`checks\sdpart-host-test-v4.5.log`].

### 31.5 The memory double buffering costs (display finding 4)

`TEST-PLAN.md` 16c said 13M rather than 14M at 768K and 29.6 13M becoming 12M. Plat's pool is
[unknown]: 29.6's figure rests on his 13M at 320K, which `win95-14m.ini`'s `mem_size = 14M` and its
comment do not settle. 16c and 29.6 now say only that the second buffer costs Windows 1M or
nothing, and that the boot console's "Emulator memory" and `mem_size` lines, which 16c asks him to
photograph with and without double buffering, decide it.

### 31.6 The new card (storage findings 1 and 4, notes 5 and 9) [verified: stand-in images and the test mode; the board unknown]

* **What the old card gets** (finding 1). `newcard/README.md` (its first lines, step 1 and step 7),
  `TEST-PLAN.md` 16e, 30.0 and `FIRMWARE.md`'s row now say it: files added to its KORVO_SD drive
  (`export-root.sh`) and its WIN9X drive (`korvo-root.img`, 192 MiB on Plat's layout,
  `korvo-mbr.bin`, `korvo-export.txt`), `/mnt/win` made on the root if it is not there, the root
  read-only for the copy; its table, its Linux root and `win95.img` not changed.
* **p3's type** (finding 4). Past sector 1,032,191 `newcard.sh` writes the LBA twin of the image's
  type (04 or 06 to 0E, 0B to 0C) and says so; when the volume's boot sector is not Windows 95
  OSR2's (`MSWIN4.1`) and p3 lies past that point, a note suggests `--p1-mib 64`. On stand-in
  images made with `mkfs.fat` (no Windows in them): type 06 with the default p1, p3 at 1,056,768,
  became 0E (with the note, since `mkfs.fat` names itself in the boot sector); 06 with `--p1-mib 64`
  stayed 06; 06 with the boot sector's name `MSWIN4.1` became 0E with no note; 0E stayed 0E; 0B
  became 0C past the point and stayed 0B below it; every run "ready" and every check passed
  [verified: `checks\newcard-v4.5.txt`]. The pristine image is 0E, so Plat's card is as v4.4 made
  it [inferred: his image comes from the same install].
* **The root found by its device number** (note 5). `export-root.sh` looks for p2 by name in
  `/proc/mounts`, else by its major:minor (from `/sys/class/block/mmcblk0p2/dev`) in
  `/proc/self/mountinfo`, and stops when `/` is `/dev/root` and cannot be tied to p2 or not. In its
  test mode, under `dash` and `bash`, with stand-in `mounts` and `mountinfo`: `/dev/root` found by
  179:2 as `/` (it says it would make it read-only), another root left alone and p2 copied, no
  `mountinfo` or no device number refused, p2 by name found; both files byte for byte each time
  [verified: `checks\newcard-v4.5.txt`].
* **Three things more** (note 9). Step 4 and "Afterwards": one card in the PC at a time (both carry
  the same disk signature, which Windows treats as a collision), the image writer's target checked
  by its size, and `korvo-root.img` (the root's Wi-Fi settings, SSH host keys, history) deleted from
  the old card's WIN9X drive and from WSL once the new card works.
* `win95-sd.ini` says it goes on the new card only, and `TEST-PLAN.md` 16a.

### 31.7 Host tests [verified: `checks\host-tests-v4.5.log` and the files named]

53 tests, all pass (72 s), at 8196e20, the tag's code: v4.4's 53, with `test_ini_edit` and `sdpart`
grown (31.2, 31.4). With the 113-character output folder of every round: 53 of 53
(`checks\host-tests-v4.5-long-out.log`). In the tree `build.sh` makes from the patch: 45 pass (v4.4:
43). That tree is a clone of upstream, which has 9ed786b, so the two pristine comparisons now run
by its commit; the seven that need this branch's own releases (v3, v3.1, v3.5, v3.9, v4.0-base,
v4.1, v4.3) are skipped, and `run.sh` now says so for each (`checks\host-tests-v4.5-from-patch.log`).
In a clone that has what the fork has, the branch and only its `s31-` tags (display note 10): 53 of
53, the pristine and v4.0-base comparisons among them, found by their commits
(`checks\host-tests-v4.5-fork-clone.log`). The host build's warnings are v4.4's 11 lines.

### 31.8 Not changed

* Display note 9 (a font loaded through the graphics mapping wholly between two pictures): deferred;
  counting every SR02 bit 2 change would put a store into Windows' 16-colour drawing, which writes
  the map mask all the time, for a loader nobody is known to use.
* Display notes 5, 7 and 11: no change, as the review found. Its remark that the card inis' comment
  ("5000 keeps out pictures caught part way through Windows' drawing") says more than 7, 4 and 4 per
  cent becoming 2, 0 and 1: the comment stays (a change would be one more card file to copy), and
  `TEST-PLAN.md` 16c gives the figures.
* Storage finding 3: the nine lines of `IOS.LOG` are paraphrased in `checks\partition-passthrough-v4.4.txt`
  here and on the branch. The branch's history kept the old text until 2026-10-25, when Plat had it
  rewritten and force-pushed: no commit holds it now (32.6).

### 31.9 Builds, warnings, sizes [verified: `checks\size-*-v4.5.txt`, `checks\warnings-wall-wextra-v4.5.txt`]

The five builds of every round (the Korvo-1 dual boot and standalone, the DOS board, the two
crash-test builds), from `git archive` of 8196e20: all exit 0 with the one old warning (`i2s_task`
unused, the Korvo-1). `-Wall -Wextra` on the five files v4.5 changed (`speed.c`, `lcd_korvo1.c`,
`esp_main.c`, `setup.c`, `sd_part.c`), each board's own command: 0 warnings, as v4.4's copies.

| Build | DIRAM used | v4.4 | Total image size | v4.4 |
|-|-|-|-|-|
| Korvo-1 (dual boot and standalone) | 251,922 | 251,594 | 932,096 | 929,278 |
| DOS board | 254,360 | 254,040 | 958,160 | 955,836 |

The 328 bytes more internal RAM on the Korvo-1 are the tidy's warning (320, both boards) and the two
swap totals (8).

### 31.10 `build.sh`, the images, the dual-boot checks [verified: `checks\reproducible-v4.5.txt` and the files named]

`build.sh` from `s31/` (its first comment moved on to v4.5) with the release's two patches made from
the branch with `s31/` left out: `git diff 4ce2e06 8196e20 -- . ':(exclude)s31'` (161 files, 35,503
lines added, 723 taken out; 1,406,121 bytes) and the same against 9ed786b (168 files, 36,133 lines
added, 738 taken out; 1,434,228 bytes); neither holds a binary. v4.4's patch made the same way from
`s31-v4.4` is the package's v4.4 patch byte for byte. Two runs from nothing, each in a clean
environment: 170 s and 174 s, both exit 0, the 23 files of the two image trees the same; and the
three `tiny386.bin`, and the standalone and DOS board's bootloader and table, the same bytes as the
in-tree builds of 8196e20.

| Image set | `tiny386.bin` | of its partition | SHA-256 | v4.4's |
|-|-|-|-|-|
| `korvo1-dualboot` (and `-tiny386-default`) | 932,272 bytes | 1,966,080 (47.4 per cent) | `35378fec69fcdf85` | 929,456 |
| `korvo1-standalone` | 932,272 bytes | 1,835,008 (50.8 per cent) | `78f68dfee86b14e5` | 929,456 |
| `dosboard` | 958,336 bytes | 1,835,008 (52.2 per cent) | `ef05ba38cbfc6b28` | 956,016 |

Every bootloader, table and `flash_args` is v4.4's (and so v4.3's) byte for byte; only `tiny386.bin`
and `tiny386.elf` changed. `esptool image-info`: every image and bootloader valid, App version
`v4.5`, the same output in both runs. The dual-boot checks, each of v4.4's run again with the v4.5
images in the package's `images\`:

* `flash-simulation-v4.5.txt`, `flash-simulation-bootswap-v4.5.txt`: the dual-boot work's flash
  model and the boot swap's (paths 1 to 19, cut-off cases C1 to C5): against v4.4's, only the app's
  size, checksums, hashes and erase ends differ (every kind counted in `chk`'s B3).
* `app-update-v4.5.txt`: v4.4 to v4.5 by `flash-dualboot.bat COM10 app` alone, simulated: every
  byte changed is inside tiny386's partition (0xE20000 up); the bootloader, the table and all of
  Linux as before; the result the same as writing the whole set with v4.5 from be5b725.
* `cmake-flash-refusal-v4.5.txt`: the dual-boot build's flash targets still refuse, as v4.4's.
* `status-led-v4.5.txt`: the LED's panic path in internal RAM in all three ELFs; `build.sh`'s
  bit-bang layout check passes in both runs.
* `card-check-v4.5.txt`: `setup.c` and `ini.c` of v4.5 on a pretend card with the package's card
  files (v4.4's checker, `card-check-v4.4.c`, unchanged): 0 problems; against v4.4's list only
  `win95-sd.ini` changed, as it should.
* Not run again: the batch harness (`bat-harness-v4.4.txt`). The three scripts and
  `flash-paths.cmd` are v4.4's byte for byte (and v3.9's), and what they read from an image is
  `esptool image-info`'s App version, which gives `v4.5` for all three apps (above).

### 31.11 Windows 95 from p3 on the PC harness [verified: `checks\partition-passthrough-v4.5.txt`]

v4.5's `headless` (8196e20) in a copy of v4.4's partition harness, the card images made again by
v4.5's `newcard.sh` from v4.4's inputs (a copy of the pristine `win95.img`, the stand-in root, the
old first sector): they differ from v4.4's in 12 bytes, all in p1 (`mkfs.fat`'s volume serial and
the copy's times); the table sector, p3's type 0E and p2 are v4.4's. Windows 95 reached the desktop
from p3 at 516 MiB and at 68 MiB with v4.4's END lines to the instruction and v4.4's sectors written
(6,508 in 146 ranges; 6,864 in 138), the card's table, p1 and p2 as made, C: in protected mode
(only the two floppies through real-mode drivers, paraphrased from `IOS.LOG`). Refused, each
stopping the PC with its line: `sd:3` with p3 mounted, `sd:2` (Linux's root), `sd:1`, `sd:4`, and
(v4.5) a card whose p3 is labelled WIN9X. The emulator's core is v4.4's (no file of it changed), so
the standard Windows 95 set and the flicker harness's runs were not made again: they would run the
same code.

### 31.12 The package folder and `SHA256SUMS` [verified: `checks\sums-v4.5.txt`]

`D:\Repos\esp32-s31-linux-release\tiny386-idf\v3`, where Plat flashes from: v4.4 as released moved
to `previous-v4.4\` (its four image sets, both patches, `build.sh`, the docs and its `SHA256SUMS`,
and v4.4's `win95-sd.ini` and `newcard\` files that v4.5 changes, so that its list still finds every
file: 180 files, 33 inside it and 147 unchanged in `v3\`); the v4.5 images, patches and `build.sh`
in their places; from the branch `win95-sd.ini`, `newcard\`, `BUILD-NOTES.md`, `TEST-PLAN.md`, `FLASH.txt`, `REVIEW-RESOLUTION.md` and
the v4.5 checks; its `README.md` edited to match `FIRMWARE.md`. The top `SHA256SUMS` is v4.4's 180
files in v4.4's order, then the two reviews of v4.4 (as `REVIEW-v43.md` was added after its review):
182 lines, every one OK. Against v4.4's list 21 files' hashes changed: the docs (`README.md`,
`BUILD-NOTES.md`, `TEST-PLAN.md`, `FLASH.txt`, `REVIEW-RESOLUTION.md`), `build.sh`, the two patches
and their diffstats, `win95-sd.ini`, `newcard\`'s three files, and the seven `tiny386.bin` and
`tiny386.elf`; every bootloader, table, `flash_args` and script is v4.4's. Each image folder's own
list checks; every earlier list, `previous-v4.4\` to `previous-v3.2\`, finds every one of its files
with its hash; the T386MON folder's list, unchanged by v4.5, checks.

### 31.13 For Plat

* **Try** (`TEST-PLAN.md` 16, now for v4.5): the keys' saves (wait for the "saved in" line), the two
  cures for tearing (the speed screen's double-buffer line has the swap counts), then the new card
  by `newcard\README.md`; photographs as 16f lists.
* **Decide**: 29.13's two (double buffering for 1M or nothing of Windows' memory, pacing by
  default), 30.15's two (p1's size, the root's), and whether the branch's history should be
  rewritten to drop the nine `IOS.LOG` lines that came in with `s31-v4.4` (31.8). (2026-10-25:
  pacing stays off, from Plat's run of v4.5 (32.5); the history was rewritten and force-pushed
  (32.6).)

### 31.14 Where things are

On the branch (`s31\`): this section; `REVIEW-v44-display.md` and `REVIEW-v44-storage-and-safety.md`
(the reviewers' own); `checks\` (text only): `findings-v4.4-review.txt`, `host-tests-v4.5.log`,
`host-tests-v4.5-long-out.log`, `host-tests-v4.5-from-patch.log`, `host-tests-v4.5-fork-clone.log`,
`sdpart-host-test-v4.5.log`, `newcard-v4.5.txt`, `partition-passthrough-v4.5.txt`,
`reproducible-v4.5.txt`, `build-sh-v4.5-run1.txt`, `build-sh-v4.5-run2.txt`, `image-info-*-v4.5.txt`
(six), `size-*-v4.5.txt` (three), `warnings-wall-wextra-v4.5.txt`, `flash-simulation-v4.5.txt`,
`flash-simulation-bootswap-v4.5.txt`, `app-update-v4.5.py` and `.txt`, `cmake-flash-refusal-v4.5.txt`,
`status-led-v4.5.txt`, `card-check-v4.5.txt`, `sums-v4.5.txt`. The package's `checks\` has the same.

WSL `~/org2/v45/` (all made here, throw-away): `bt-c1/` (the in-tree builds), `rel/` (the stage and
the two `build.sh` runs), `chk/` (the checks), `ph/` and `ncw/` (the harness copy and the card images
made from a copy of the pristine `win95.img`: never to leave WSL), `nc/` and `nc2/` (the stand-in images),
`dbuf/` (the review's model rebuilt), `t1/`, `ht1/`, `sdp1/`, `ht-fork/`, `fork-clone/`, `t-c1/`,
`t-v44/`, and the scripts with their outputs. On Windows the session's scratch folder `v45\` holds
the scripts as written. Nothing started for this is still running.

## 32. v4.6: double buffering made at power-on, and the guest kept out of the devices' state (2026-10-25)

Plat ran v4.5 on his Korvo-1 on 2026-10-25, Windows 95 from the new card's p3 with `win95-sd.ini`
[reported]. Everything he tried worked, the new card too. Pacing (Ctrl+Alt+F10, `pace_quiet`)
changed none of the visual glitches and lowered the panel's frame rate. `double_buffer = 1` stopped
the boot with a load access fault, `mtval 0000fa50`. v4.6 answers both, and takes in upstream's fix
of 2026-10-23 (97a450b) with three memory checks of our own. Two series were made, each in its own
WSL clone of the canonical repository `~/fw/tiny386-s31` from c8528f2 (the branch after the history
rewrite, 32.6): the core series (`~/org2/v46/core/repo`, branch `v46-core`) and the display series
(`~/org2/v46/disp/repo`, branch `v46-disp`). They were put together in a third clone,
`~/org2/v46/int/repo`, branch `v46`: core first, then display, by cherry-pick, each commit keeping
its author and message. Every commit of ours is by Platima with his GitHub noreply address and the
Co-Authored-By line; upstream's keeps its own author and message. Nothing was flashed, no serial
port was opened, nothing of v4.6 has run on a board, and nothing was pushed or tagged in this round.

### 32.0 In one screen

* **Why `double_buffer = 1` crashed** (32.2): v4.5 deleted the panel once the ini was read, to
  start it again with two frame buffers. On Plat's board that start failed, with two and then with
  one, and `vga_task` then handed the VGA code a frame buffer at address 0. 0xFA50 is the first
  text cell's first pixel: row 40 at 1,600 bytes a row, plus 40 pixels [verified: disassembly,
  arithmetic, and the host test faults at the same address; that the board took this path is
  inferred]. Why the panel would not start again is [unknown]. Memory is the likeliest: PSRAM
  broken up round the freed buffer, or internal DMA memory [inferred].
* **Double buffering made at power-on** (32.3): both frame buffers come with the panel, before the
  emulator's memory or anything else, and the panel is never started again. The ini is only known
  after the boot menu, so a start whose ini asks for it notes the ini in the LP SRAM and restarts
  once; the next start makes the panel with two frame buffers and takes that ini with no menu. If
  the panel will not start with two, it starts with one, as at every power-on of v4.5, and a yellow
  line gives ESP-IDF's error and the memory free. Never more than one restart in a row, and none
  under the Linux-default table. `double_buffer = 0` starts as v4.5 does [verified: code, host test
  3g, objdump; the board unknown].
* **The guest kept out of the devices' state** (32.4): upstream's 97a450b starts the devices' state
  at 0xA0020; a 16 or 32-bit access that crosses a 4 KiB page goes a byte at a time, each byte to
  its own page; DMA writes into 0xA0000 to 0xBFFFF are dropped; a floppy request for a cylinder,
  head or sector the disk does not have is refused. On the PC, Windows 95 gives v4.5's instruction
  counts to the instruction in three runs [verified].
* **Pacing** stays off by default (32.5), from Plat's run. No code change.
* **The history rewrite** (32.6) was done and force-pushed on 2026-10-25. The notes here now name the
  new hashes.
* **Checks** (32.7 to 32.10): 61 host tests pass (v4.5: 53); the release built twice from nothing
  gives the same bytes, and the same as in-tree builds of the code; every bootloader, table and
  `flash_args` is v4.5's byte for byte; the dual-boot app is 936,192 bytes of its 1,966,080; the one
  old warning, no new one.

### 32.1 The commits [verified: `git log c8528f2..v46`]

| Commit | Author | What |
|-|-|-|
| 876af9c | Chunhui He (upstream 97a450b, "fix", cherry-picked as it is) | `pc.c`: the devices' state from 0xA0020, 0xC0000 less that long (32.4) |
| a67bb47 | Platima | `i386.c`, `tests/host`: 16 and 32-bit accesses that cross a page go a byte at a time; `test_page_cross.c`, `run.sh` 21 (32.4) |
| 8ea8200 | Platima | `i8257.c`, `tests/host`: DMA writes into the 0xA0000 window dropped; `test_dma_window.c`, `run.sh` 22 (32.4) |
| 2dfa515 | Platima | `misc.c`, `tests/host`: a floppy cylinder, head or sector the disk does not have refused; `test_floppy.c` (32.4) |
| cdf934a | Platima | `esp/main`, `tests/host`: the VGA code keeps its buffer when double buffering was not made (`display.c`, `lcd.h`); `tests/host/display` (new: `test_double_start.c` and its stubs), `run.sh` 3g (32.2) |
| 5b5c1fb | Platima | `esp/main`: the panel not started again: restart once (`esp_main.c`, `lcd_korvo1.c`, `lcd.h`, `speed.c`, `speed.h`; `TEST-PLAN.md` 16c); mostly replaced by 635dc04 |
| b8dc026 | Platima | `esp/main`, `tests/host`: the crash screen with no panel draws nothing (`console.c`, `test_console.c`) (32.2) |
| 89573ce | Platima | `esp/main`: no panel: the PC's own picture buffer counted (`esp_main.c`) (32.2) |
| 635dc04 | Platima | `esp/main`, `tests/host`: both frame buffers made with the panel at power-on: `dbuf_boot.c` and `dbuf_boot.h` (new), `lcd_korvo1.c`, `lcd.h`, `esp_main.c`, `speed.c`, `display.c`, `board_korvo1.h`, `CMakeLists.txt`; `test_double_start.c` and three stubs more, `run.sh` 3g; `TEST-PLAN.md` 16c (32.3) |
| 190ec33 | Platima | `esp/version.txt`: `v4.6` |
| (the next commit) | Platima | `s31/`: this section, `TEST-PLAN.md` 16 and 17, `FLASH.txt`, `FIRMWARE.md`, `README.md`, `dualboot/DUALBOOT.md` 13.3, 13.6 and 13.8, `build.sh`'s first comment, `REVIEW-RESOLUTION.md`, the hashes of 32.6, and the checks (32.14) |

`git diff --stat s31-v4.5 190ec33 -- . ':!s31'`: 40 files changed, 2,832 lines added, 254 taken out,
c8528f2's `esp/dependencies.lock` among them. Every commit's files were looked at: no binary,
nothing of Windows'.

The five display commits are the display series' with one line changed in each, the same line.
`run.sh` 3g, which compares the display test with v4.5's `display.c`, named v4.5's commit by its
hash from before the rewrite, as a fall-back when the tree has no tag for v4.5. That hash names one
of the commits the rewrite left behind, so before anything was built it was changed to 99074b7,
the rewritten `s31-v4.5`, which has the same tree, in all five (`git filter-branch` on `v46` alone).
Each commit's message, author, committer and dates are as they were, and nothing else in any tree
changed [verified: the old and new commits compared pair by pair]. Where the tag is there, as in
the canonical repository and on the fork, the test takes the tag, as before.

### 32.2 The crash with `double_buffer = 1` [verified: code, disassembly, host test; the board's path inferred]

Plat's photo of the next boot's console: a load access fault on core 0, `mepc 4001b87e`, `mtval
0000fa50`, and two white flashes of the status LED, which mean the last run ended in a watchdog or
lock-up reset [reported].

* The fault is in `vga_draw_glyph9()`, inlined into `vga_draw_cell()`, reading the first pixel of
  the first text cell from the VGA code's frame buffer, whose address was 0. 0xFA50 = 40 x 1,600 +
  40 x 2: row 40 of an 800x480 RGB565 buffer, 40 pixels in, where 80x25 text starts [verified:
  v4.5's ELF from the package, disassembly; arithmetic].
* v4.5's `vga_task` handed `lcd_vga_present()` to the VGA code after every picture, whether or not
  double buffering had been made. When `double_start()` could not start the panel again, with two
  frame buffers or with one, there was no panel frame buffer, `lcd_vga_present()` returned NULL,
  and the next text cell faulted at 0xFA50 [verified: v4.5's `display.c` in the host test faults
  there]. v4.5's answer to that case (31.3, display note 6) told the console, not `vga_task`.
* cdf934a: `vga_task` hands the panel's buffer over only when double buffering was made. b8dc026:
  the crash screen draws nothing when there is no panel; the two white flashes were most likely
  the crash screen faulting inside the panic handler [inferred]. 89573ce: with no panel the PC's
  own picture buffer, 752 KiB, is counted before the guest's memory is settled, so a `mem_size`
  that fits is no longer stopped later with a message that blames it. 5b5c1fb's restart for that
  case is replaced by 635dc04's design (32.3).
* Why the panel would not start again on Plat's board is [unknown]. The panel at power-on with one
  frame buffer works there; the second start came after the emulator's pool, the card and the rest
  had taken their memory. So memory is the likeliest cause: PSRAM broken up round the hole the first
  panel left, or internal DMA memory for the driver [inferred]. 32.3 takes the question away rather
  than answering it, by making the frame buffers first; if even that fails, its yellow line names
  the error and the memory free.

### 32.3 Double buffering made at power-on (635dc04) [verified: code, host test 3g, objdump; the board unknown]

1. `app_main()`'s first call, `dbuf_boot_take()`, copies a 180-byte note from the LP SRAM
   (`.rtc_noinit`, beside the console's ring) and clears it, and keeps the reset's cause.
2. `dbuf_boot_panel()` takes `lcd_init()`'s place. With a whole note asking for two frame buffers,
   it marks the note TRYING, calls `lcd_init_two()` (the RGB panel with `num_fbs = 2`, both buffers
   the driver's own in PSRAM, and the swap event) and clears the note again. If that start fails,
   `lcd_init_two()` deletes the panel nobody has seen and starts it exactly as `lcd_init()` does,
   keeping ESP-IDF's error. Without a note: `lcd_init()`, v4.5's start, unchanged.
3. With a note, the boot menu is skipped for the ini it names, if it is still there, and
   `lastboot.txt` is not written.
4. `dbuf_boot_start()` takes `double_buffer_start()`'s place. Both buffers made and the ini asking:
   swapping on. Not made: the reason, in yellow. Or, at a start that found no note at all and whose
   reset was not a software restart, with a panel, room in the pool, and a restart that comes back
   to tiny386: the note (the ini's folder and name) is written, a grey line says so, and after 1 s
   `esp_restart()`. That is after `ini_parse()` and before `settle_memory()`, `check_config()`
   (which hands a card partition to the PC) and the PC.

**No loop.** A restart needs a start that found no note at all, so a whole note, a TRYING note or a
broken one stops it. It also needs a reset that was not a software restart, so the start after
tiny386's own restart never restarts, whether the LP SRAM kept the note or not (`esp_restart()`
reads back as `ESP_RST_SW` on the S31 [verified: ESP-IDF's `reset_reason.c`]). A reset inside the
two-buffer start leaves TRYING, so the start after it has one buffer and does not try again. So
there is at most one restart after any reset that is not itself a software restart.

**Where a restart would start Linux** (tiny386 the dual boot's test app, under the Linux-default
table) there is none: `restart_comes_back()` reads the running partition's subtype
(`dualboot\DUALBOOT.md` 13.8), and a yellow line says that double buffering needs the
tiny386-default table. Holding GPIO42 low through the reset to steer the bootloader was thought of
and rejected: VOL+ is on a resistor ladder, and whether a pad hold survives a system reset on the
S31 is not known.

**The memory.** The pool is sized after the panel, so with both frame buffers it is already about
750 KiB smaller, and `settle_memory()` and `check_config()` count the second buffer once;
`double_buffer_start()` no longer takes 752 KiB from the pool as v4.5's did. With Plat's pool of
15,104 KiB (his photo of the crash), `win95-sd.ini` (`mem_size = max`, 768K) gets 13M with or
without double buffering, and `win95-14m.ini` (14M, 320K) is lowered to 13M with it [inferred:
arithmetic on `settle_memory()`]. The note takes 180 bytes of the LP SRAM (8,244 to 8,424 of
32,768).

**The lines** Plat will see, case by case, are in `TEST-PLAN.md` 17c and 17d. With
`double_buffer = 0` there is no new line.

**Tested** (`tests/host/display/test_double_start.c`, `run.sh` 3g, rewritten): 19 cases, each a chain
of starts in child processes, the note and the reset's cause carried between them, with a pretend
RGB driver that allocates from a pretend PSRAM and can refuse a start, refuse the swap event, or
reset the chip inside the start. Checked in each: the note cleared, never more than one restart,
two buffers asked for only with a whole note, the pool 750 to 754 KiB smaller with two, the console
line the case expects, every picture's buffer, and Ctrl+Alt+F9's state. All 19 pass. Against v4.5's
`display.c` the three cases with no panel fault at 0xfa50, as the board did, and with a panel the
16 cases give v4.5's 192 pictures and 80 presses of Ctrl+Alt+F9, line for line. Each of 14 deliberate
breaks of `dbuf_boot.c` and `lcd_korvo1.c` makes the test fail [verified: the display series'
runs; the 19 cases and the comparison with v4.5 again here, 32.7].

**In the built app** [verified: objdump of `korvo1-dualboot\tiny386.elf`,
`checks\reproducible-v4.6.txt`]: `app_main`'s first call is `dbuf_boot_take`; `dbuf_note` is 180
bytes at 0x2E002010 in the LP SRAM, beside `s_ring` and `s_rec`; `dbuf_boot_start` calls
`esp_ota_get_running_partition` and `esp_restart`.

### 32.4 The guest kept out of the devices' state [verified: host tests 21 and 22, the floppy test, the PC harness; the board unknown]

On the board the devices' state (`pcmalloc()`: the IDE channels, the NE2000, the dirty map and the
rest) lives in the guest's own memory from 0xA0000 to 0xBFFFF, where the guest's own stores go to
the VGA instead. Three ways in were left.

* **876af9c (upstream 97a450b).** The state starts at 0xA0020, so a 16 or 32-bit store that starts
  in the last bytes below 0xA0000 lands in a 32-byte gap, not in the primary IDE channel's `irq`
  field. The window is 131,040 bytes, and the boot console's line reads `Device memory (the 0xA0000
  window): N of 131040 bytes used` where v4.5 said `of 131072`. N is v4.5's: 91,616 on the PC
  harness with `win95-14m.ini`'s settings, on both [verified]; 93,344 for `win95-sd.ini` and
  93,408 for `win95-800x480.ini` [inferred: the board ELF's sizes]. Every allocation is a multiple
  of 32 and no configuration of the Korvo-1 fills the window exactly, so the 32 bytes can never be
  the ones that stop a boot [verified: every mix of disks and video memory sizes, by arithmetic].
* **a67bb47, the byte split** (after yejun's hchunhui/tiny386 PR #49). An access that runs on into
  the next 4 KiB page goes through `load8` or `store8` a byte at a time, each byte to its own
  physical address: under paging, the second page's for the bytes past the boundary. With v4.5's
  `i386.c` such accesses put bytes into the devices' state in the window (0xA0000 to 0xA0002 and
  0xA1000 to 0xA1002 in the test) and past the end of RAM, and loads read the window's bytes
  [verified: host test 21's "before" mode]. The old split code is gone. The crossing
  test is written so that the board's compiler makes it three instructions with no load (`lui`,
  `slli`, `bltu`), so the common path of `load16`, `load32`, `store16` and `store32` is one
  instruction longer than v4.5's with one memory load fewer (15, 15, 17, 17 instructions become 16,
  16, 18, 18). The byte path is in flash, and internal RAM's code is 266 bytes smaller. Host test 21
  (`test_page_cross.c`): 17 guests, each a crossing case, pass on this `i386.c` as the board and as
  tiny386's Makefile build it, and v4.5's `i386.c` in the test's "before" mode shows the fault
  [verified].
* **8ea8200, DMA.** `i8257_dma_write_memory()` drops a byte for 0xA0000 to 0xBFFFF, in both
  directions; the rest of a transfer goes to RAM as before. Its only writer there is SB16 command
  E2h, which writes one byte to channel 1's address [verified: code]. Host test 22
  (`test_dma_window.c`): 7 transfers pass, and v4.5's `i8257.c` fails 4 of them [verified].
* **2dfa515, the floppy.** A request for a cylinder, head or sector the disk does not have is
  refused: the two lines of upstream's 81e09ee (Plat's hchunhui/tiny386 PR #50) that this tree
  lacked. `test_floppy.c`: 5 more checks, 28 in all, pass [verified].
* **Windows 95 on the PC harness** (the core series' runs, WSL `~/org2/v46/core/pc/runs`, each on a
  fresh copy of the pristine image, deleted after): to the desktop and 120 s idle, 588,301,033
  instructions; Start, Shut Down, Restart, 1,304,341,904; Restart in MS-DOS mode with `mem`, `dir`
  and `exit`, 1,337,723,352. Each is v4.5's count to the instruction, with the same final picture
  [verified]. So those runs made no crossing access into the window, no DMA write there and no
  floppy request, and the checks cost them nothing.

### 32.5 Pacing on the board [reported]

Plat on v4.5: pacing changed none of the visual glitches and lowered the panel's frame rate. On the
PC it took the pictures caught part way through drawing from 7, 4 and 4 per cent to 2, 0 and 1
(29.7), so what he sees is something else. `pace_quiet` stays 0 by default, and its code is v4.5's.
`TEST-PLAN.md` 16c says so.

### 32.6 The branch's history rewritten (2026-10-25) [verified: git]

Storage finding 3 of round 11: nine lines quoted from Windows 95's `IOS.LOG` came in with
`s31-v4.4`, in `checks\partition-passthrough-v4.4.txt`, and v4.5 paraphrased them, but the history
kept them. On 2026-10-25 Plat had the branch rewritten from `s31-v4.4` on and force-pushed. The
seven commits that held the lines carry v4.5's paraphrased copy of that file, and nothing else in
them changed; the commit after them changed only because its parent did, and its tree is the same.
The two tags were made again with the same name, message, tagger and date. Older commits and tags
keep their hashes. c8528f2 then adds the S31 `esp/dependencies.lock`.

| Commit now | Tag | Subject |
|-|-|-|
| 6d619a0 | `s31-v4.4` (tag object 7d7febe) | s31: v4.4 part 2 (the card partition, the new card, the release) and its checks |
| 47290db | | esp/main: double buffering after the speed screen, its counts, a panel that does not come back |
| 421aef6 | | esp/main, tests/host: the ini saved with no rename |
| f76aaab | | esp/main, tests/host: sd:N refuses the WIN9X partition; the review's notes on sd_part.c |
| 6e5e150 | | tests/host: run.sh finds pristine and v4.0-base by their commits, and says when it skips |
| dd51a62 | | s31/newcard: p3's type past CHS; the root found by its device number |
| 8196e20 | | esp/version.txt: v4.5, v4.4 with the fixes of its two reviews |
| 99074b7 | `s31-v4.5` (tag object 13995ba) | s31: v4.5 (the two reviews of v4.4) and its checks |
| c8528f2 | | esp/dependencies.lock: the S31 one, as the first build writes it |

The notes in this folder named the old hashes: 31.1, 31.7, 31.9, 31.10, 31.11 and 31.13 here,
`REVIEW-RESOLUTION.md` round 11, the two reviews of v4.4, `checks\findings-v4.4-review.txt`,
`checks\partition-passthrough-v4.5.txt` and `checks\reproducible-v4.5.txt`. Each now names the new
one, and each review says so in a line added at its top. The old hashes are named nowhere in the
tree, nor in v4.6's commits (32.1); the map from old to new stays out of the tree, as its old column
names the commits the rewrite left behind. 31.8, 31.13 and storage finding 3 in
`REVIEW-RESOLUTION.md` now say that the rewrite was done. GitHub may still show the old commits by
their hashes until it collects them; only GitHub Support can purge them sooner [inferred].

v4.5 as released, in the package's `previous-v4.5\`, keeps its docs as they were, old hashes and
all, so that its `SHA256SUMS` still checks (32.11).

### 32.7 Host tests [verified: `checks\host-tests-v4.6.log` and the files named]

61 tests, all pass (89 s), at 190ec33: v4.5's 53; three in 3g (the 19 double-start cases, v4.5's
fault at 0xfa50 seen by the test, and with a panel v4.5's pictures line for line); three in 21 (the
page crossing on `i386.c` as the Korvo-1 builds it and without the board header, and v4.5's bytes
seen in "before" mode); and two in 22 (DMA, and v4.5's bytes seen). With the 113-character output
folder: the same 61 (`checks\host-tests-v4.6-long-out.log`). In the tree `build.sh` makes from the
patch: 49 pass (v4.5: 45), the comparisons that need this branch's own releases skipped and said
(`checks\host-tests-v4.6-from-patch.log`). The host build's warnings are v4.5's 11 lines. A run at
f7f7492, the version commit before 32.1's one-line change, gave the same 61, 61 and 49.

### 32.8 Builds, warnings, sizes [verified: `checks\size-*-v4.6.txt`, `checks\reproducible-v4.6.txt`]

The five builds of every round (the Korvo-1 dual boot and standalone, the DOS board, the two
crash-test builds), from `git archive` of 190ec33 with `s31/dependencies.lock`: all exit 0. The
Korvo-1 builds have the one old warning (`i2s_task` unused), the DOS board builds none. The host
build compiles every C file v4.6 changed with `-Wall -Wextra` and gives v4.5's lines only (32.7);
the board's own `-Wall -Wextra` pass of earlier rounds was not run.

| Build | DIRAM used | v4.5 | LP SRAM | v4.5 | Total image size | v4.5 |
|-|-|-|-|-|-|-|
| Korvo-1 (dual boot and standalone) | 251,872 | 251,922 | 8,424 | 8,244 | 936,012 | 932,096 |
| DOS board | 254,094 | 254,360 | 8,244 | 8,244 | 958,424 | 958,160 |

Internal RAM: the byte split takes 266 bytes of code out of it on both boards, and the display
series adds 216 on the Korvo-1. The LP SRAM's 180 bytes are the note (Korvo-1 only: the DOS board
has no `LCD_DOUBLE_BUFFER`, so `dbuf_boot.c` builds to nothing there).

### 32.9 `build.sh`, the images, the dual-boot set [verified: `checks\reproducible-v4.6.txt`]

`build.sh` from `s31/` (its first comment moved on to v4.6), with the release's two patches made from
the branch with `s31/` left out: `git diff 4ce2e06 190ec33 -- . ':(exclude)s31'` (185 files, 38,181
lines added, 823 taken out; 1,518,481 bytes) and the same against 9ed786b (192 files, 38,811 lines
added, 838 taken out; 1,546,588 bytes); neither holds a binary. v4.5's patches made the same way
from `s31-v4.5` (99074b7) are the package's v4.5 patches byte for byte, so the rewritten history
gives v4.5's release tree. `dependencies.lock` is v4.5's, and the patched tree's
`esp/dependencies.lock` (c8528f2's) is the same bytes. Two runs from nothing, each in a clean
environment, into WSL `~/org2/v46/int/build-a` and `build-b`: 171 s and 171 s, both exit 0,
the 23 files of the two image trees the same, and the two runs' `esptool image-info`, size and
bit-bang outputs the same but for the folder's name. Built in the tree at the same code with the
same build folder names, all three `tiny386.bin` and `tiny386.elf` are `build.sh`'s byte for byte,
and the standalone and DOS board's bootloader and table too. The images of the run at f7f7492 are
these bytes too: the one-line change of 32.1 is in a test.

| Image set | `tiny386.bin` | of its partition | SHA-256 | v4.5's |
|-|-|-|-|-|
| `korvo1-dualboot` (and `-tiny386-default`) | 936,192 bytes | 1,966,080 (47.6 per cent; 1,029,888 free) | `73136176737fae0a` | 932,272 |
| `korvo1-standalone` | 936,192 bytes | 1,835,008 (51.0 per cent) | `6e0439cd01a09433` | 932,272 |
| `dosboard` | 958,608 bytes | 1,835,008 (52.2 per cent) | `828ca9ae95b04389` | 958,336 |

Every bootloader, table and `flash_args` of the four sets is v4.5's byte for byte (and so v4.4's and
v4.3's): the dual-boot bootloader `b57469cd`, the Linux-default table `d41cba3c`, the tiny386-default
table `5b90d416`; only `tiny386.bin` and `tiny386.elf` changed. `esptool image-info`: every image and
bootloader valid, App version `v4.6`. `build.sh`'s check of the status LED's bit-bang passes for all
three ELFs in both runs.

### 32.10 Not run again, and why

* The dual-boot flash simulations, the batch harness, the app update simulation, the CMake flash
  refusal and the card check: the scripts, `flash-paths.cmd`, the bootloaders, the tables,
  `flash_args` and every card file are v4.5's byte for byte, and what the scripts read from an image
  is `esptool image-info`'s App version, which says `v4.6` for all three apps (32.9).
  `flash-dualboot.bat COM10 app` writes 936,192 bytes at 0xE20000, inside tiny386's partition.
* The standard Windows 95 set, the flicker harness, T386MON and the partition harness on the PC: the
  three Windows runs of 32.4 give v4.5's counts to the instruction, the display series changes only
  the board's own code (`esp/main`), which the PC harness does not build, and `sd_part.c` is v4.5's.

### 32.11 The package, staged [verified: `checks\sums-v4.6.txt`]

`D:\Repos\esp32-s31-linux-release\tiny386-idf\v3`, where Plat flashes from, was only read. v4.6's
package is staged in WSL `~/org2/v46/pkg`, laid out as that folder and holding only what v4.6
changes or adds (92 files), for the release step to copy over it:

* `previous-v4.5\`: v4.5 as released, copied from the package (36 files): its four image sets, both
  patches and their diffstats, `build.sh`, `README.md`, `BUILD-NOTES.md`, `TEST-PLAN.md`,
  `FLASH.txt`, `REVIEW-RESOLUTION.md` and its `SHA256SUMS`, and v4.5's copies of the two reviews of
  v4.4, which v4.6 changes (32.6). Its docs keep the hashes from before the rewrite, as released.
* The v4.6 images (build-a's), both patches and their diffstats, and `build.sh`, in their places.
* From the branch: `BUILD-NOTES.md`, `TEST-PLAN.md`, `FLASH.txt`, `REVIEW-RESOLUTION.md`, the two
  reviews of v4.4, `dualboot\DUALBOOT.md`, and in `checks\` v4.6's checks and the three of v4.5's
  whose hashes moved on. The package's own `README.md`, edited to match `FIRMWARE.md`.
* No card file, flash script, `newcard\` file, `STATUS-LED.md`, `LINE-OUT.md` or
  `dependencies.lock` changed, so none is staged.

The top `SHA256SUMS` is v4.5's 182 files in v4.5's order, as v4.6 adds none: 182 lines, every one OK
when each file is read from the stage if it is there and from the package folder if not, as the
folder will be. Against v4.5's list 19 files' hashes changed: the docs (`README.md`,
`BUILD-NOTES.md`, `TEST-PLAN.md`, `FLASH.txt`, `REVIEW-RESOLUTION.md`, the two reviews of v4.4),
`build.sh`, the two patches and their diffstats, and the seven `tiny386.bin` and `tiny386.elf`.
Every bootloader, table, `flash_args`, script and card file is v4.5's. Each image folder's own list
checks. Every earlier list, `previous-v4.5\` to `previous-v3.2\`, finds every one of its files with
its hash, in itself, in `v3\` as it will be, or in a later `previous-*` folder: not one missing
(`previous-v4.5\`'s 182: 31 inside it, 151 from `v3\`). As before the list leaves out `checks\` and
`dualboot\`.

### 32.12 What only a board can say [unknown]

* Whether the panel starts with two frame buffers at power-on, and if not, why (the yellow line's
  error and memory figures).
* The swap itself: v4.4's mechanism has never run on a board.
* Whether the LP SRAM keeps the note through `esp_restart()`. If not, Plat sees one restart and a
  yellow line, never a loop.
* That `esp_ota_get_running_partition()` gives the test subtype for tiny386 under the Linux-default
  table [inferred: ESP-IDF's source].
* The boot console's device memory figure, and whether the byte split's one instruction shows in
  T386MON's instructions a second.

### 32.13 For Plat

* **Try** `TEST-PLAN.md` 17: the memory lines with `double_buffer = 0`, then `double_buffer = 1` from
  power-on with tiny386 the default, photographs as 17f lists; Windows as before.
* **Decide**, once double buffering runs: whether it is worth its memory and its restart at every
  boot.
* **The release**: the tag `s31-v4.6` goes on the commit that adds this section, and the branch and
  the tag go to the fork from `D:\Repos\tiny386` as before; the staged package goes into the package
  folder (32.11).

### 32.14 Where things are

On the branch (`s31\`): this section; `TEST-PLAN.md` 17; `dualboot\DUALBOOT.md` 13.8; `checks\` (text
only): `host-tests-v4.6.log`, `host-tests-v4.6-long-out.log`, `host-tests-v4.6-from-patch.log`,
`reproducible-v4.6.txt`, `build-sh-v4.6-run1.txt`, `build-sh-v4.6-run2.txt`, `image-info-*-v4.6.txt`
(six), `size-*-v4.6.txt` (three), `sums-v4.6.txt`. The package's `checks\` has the same.

WSL `~/org2/v46/` (all made here, throw-away): `core/` and `disp/` (the two series: their clones,
builds, host test runs and the Windows runs), `dbuf/` (the first fix of the crash, which the display
series starts from), `int/` (the clone that puts them together, `stage/`, `build-a/` and `build-b/`,
`bt/` (the in-tree builds), `ht/` (the host test runs), `prelim-f7f7492/` (the same runs at the
version commit before 32.1's change) and `work/` (the scripts)), and `pkg/` (the package, staged).
On Windows the session's scratch folder `v46\` holds the notes of each step and the scripts as
written. Nothing started for this is still running.
