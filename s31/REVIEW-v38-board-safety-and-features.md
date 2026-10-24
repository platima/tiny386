# Review of v3.8: board safety and the two new features

Independent review, 2026-10-22, of tiny386 v3.8 (tag `v3.8`, 3bded9d, in WSL
`~/tiny386-idf-scratch/v3/tiny386`) and of this package as the feature and packaging step left it.
Lens: the tiny386-default partition table and the flashing scripts (can any path leave Plat without
a Linux or a tiny386 that boots), the disk light (its cost on the emulator's core, the stall check,
the panic path), the limit on repeated log lines and the UART queue (no lost fatal or error lines,
no UART wait on the emulator's core), and the package (checksums, versions, the docs against the
files).

Nothing was flashed, no serial port was opened, and nothing in the scratch tree or in this package
was changed except this file. My copies, scripts and outputs are in WSL
`~/win95-debug/review-board-safety-and-features/` and in this session's scratch folder
`C:\Users\Owner\AppData\Local\Temp\claude\D--Repos-esp32-s31-linux\99e6c67c-c5d4-41b3-a911-64bbdd3332f8\scratchpad\rbsf\`.
No process was left running.

Tags: [verified] seen in a run, a build or a named source line in this review; [inferred] follows
from those; [unknown] only the board can say.

Severity as used here: **blocker**, stops the flashing Plat is about to do; **major**, a path to a
board that boots nothing, or to a lost record, that should be fixed before the next release;
**minor**, wrong or missing information, or a cost, with a way round it; **note**, worth knowing.


## Plat's question: are UTC and local times mixed up?

No [verified]. Windows is on W. Australia Standard Time and WSL on Australia/Perth, `+0800`. The
v2 `tiny386.bin` was written 2026-10-21 13:19:18 +08:00 (05:19:18 UTC), the dual-boot bootloader
16:02:33 +0800, and the commits behind the tags are v3 18:20:20 +0800, v3.1 19:52:38, v3.6
2026-10-22 14:22:06, v3.7 18:14:22, v3.8 22:15:36. Your photo at 18:29 on the 21st (local, as you
took it) comes nine minutes after v3 was committed and before any v3 image was in the package, so
the blue screen you came home to was on the v2 app you flashed before you went out. That agrees
with both earlier steps.


## In short

* **No blocker.** The two writes Plat is about to make, `flash-dualboot.bat COM10 app` and, if he
  wants it, `flash-dualboot.bat COM10 table tiny386`, behave as documented in every case I ran,
  and neither can leave the board without a system that starts [verified: harness cases 8, 40, 55,
  59 and 81; flash simulation paths 11 and 14].
* **One major finding, old but in this lens**: after a cut write in the second esptool run of a
  first install over Linux, running `flash-dualboot.bat` again stops with advice that, if followed,
  replaces the good backup with a copy of the half-written state; a later `back-to-linux.bat`
  with no options then writes a broken bootloader back (F1). It does not touch the `app` and
  `table` writes.
* **The logging limit and the UART queue do what they say for the emulator's own abort**, but
  three lines can go missing: the line that names the cause of an abort is not on the UART by
  default (L1), the newest repeated lines and their count never reach the crash screen after a
  panic that is not the emulator's own abort (L2), and the status LED's stall line goes nowhere
  by default (L3). All minor.
* **The disk light costs the emulator one load, add and store per guest disk access, in internal
  RAM; the stall check and the panic path are unchanged** [verified]. Two notes and one minor on
  how the light reads.
* **The package checks out**: every SHA256SUMS line verifies, the images rebuild from nothing byte
  for byte, the patch is `git diff pristine v3.8`, the host tests pass 32 of 32 [verified]. Two
  small doc mismatches (P1, P2).


## What I ran

| Check | Result |
|-|-|
| `sha256sum -c` of every SHA256SUMS | top level 45 of 45; `images\*\SHA256SUMS` 5, 5, 4 and 5, all OK; `dualboot\SHA256SUMS` 92 of 92; `previous-v3.7\SHA256SUMS` 25 of 41 inside the folder, and the other 16 match: 12 the files in `v3\`, 4 the copies in `checks\bootswap\scripts-before\` [verified] |
| Every partition table decoded by my own decoder (not `gen_esp32part.py`) | 9 tables: both dual-boot tables in `images\` and `dualboot\`, the standalone and DOS board ones, be5b725's, the release's and v3.7's. Every MD5 entry right, the rest of each 3 KB all 0xFF, no overlap, every app 64 KiB aligned, nothing past 16 MB. The tiny386-default table is the Linux-default one with only the subtypes of entry 4 (the Linux loader at 0x20000: factory to test) and entry 8 (tiny386 at 0xE20000: test to factory) swapped, labels kept [verified] |
| The bootloader's choice, read in ESP-IDF 188e3e55bb | `bootloader_utility.c` keeps factory and test by subtype only; with no OTA apps it picks factory; GPIO42 low for a second picks test; a factory app that fails its check falls back to the test app; a test app that fails resets. The dual-boot bootloader's sdkconfig has the test pin on, no factory-reset pin and no skipped image checks [verified: `bootloader_utility.c` lines 165 to 190, 376 to 411, 576 to 627; `bootloader_start.c` lines 84 to 137; `dualboot\build\sdkconfig.dualboot` lines 1099 to 1118] |
| Flash simulation with tiny386 the default, run again myself | `checks\bootswap\scripts\simulate-flash-bootswap.py` unchanged, with the v3.8 images: exit 0, and its 742 lines are the same as the first 742 of `checks\flash-simulation-bootswap-v3.8.txt` (that file adds a comparison after them) [verified] |
| The scripts' flow, run again myself | `make_flow_bootswap.py` unchanged plus 19 cases of my own, through cmd.exe with PATH set to `C:\Windows\System32` and `C:\Windows` only: cases 1 to 69 give the package's `checks\bat-harness-v3.8.txt` line for line (0 lines differ once dates and hashes are folded); 88 cases in all; no write or erase reached the stub esptool [verified] |
| `build.sh` from nothing, once more | exit 0 in 266 s; all 23 files of the four image sets the same bytes as `images\` (the four `SHA256SUMS` included); its LED bit-bang layout check passed for all three ELFs [verified] |
| The patch | `tiny386-idf-v3.patch` is byte for byte `git diff pristine v3.8` (SHA-256 650e90a7...); applied to 9ed786b it gives tree c9db351d, the tag's tree. `research\win95-core-v3.8.patch` is 9f21620c..., the figure BUILD-NOTES gives [verified] |
| Host tests in my own clone of the tag | 32 passed, 0 failed [verified] |
| `esptool image-info` on every app | all valid, `App version: v3.8`, `Project name: tiny386`; each ELF's SHA-256 in the image matches the ELF in its folder (400d4f3b, 9def5734, e7d4441b) [verified] |
| The new words in the built Korvo-1 ELF | `ide_activity` 0x2F03CE0C and `emulink_activity` 0x2F03CE1C, both in internal DRAM `.bss`; `lim_pass` and `uq_put` in flash [verified: `riscv32-esp-elf-nm`] |
| A probe of `console.c` (CONSOLE_HOST), around an abort and a panic | results in L1, L2, L3 and L4 [verified: `probe/review_console.c` in the WSL work folder] |


## Findings

### F1 (major, old): after a cut write over Linux's table, "run this again" stops, and its advice spoils the backup

Where: `flash-dualboot.bat` lines 200 to 211 (`:backup_compare`) and 318 to 324
(`:write_failed_boot`); `dualboot\DUALBOOT.md` 12.2, the bullets at lines 569 to 576, and 13.5's
case C5. The same logic is in `flash-standalone.bat`.

The first install over Linux takes the backup, writes tiny386, then writes the bootloader and the
table in a second esptool run. If that second run is cut inside the bootloader (DUALBOOT 12.2 case
1c: nothing starts) or between the bootloader and the table (case 1d and 13.5 case C5: Linux starts),
the table at 0x8000 is still Linux's, so the next run of `flash-dualboot.bat` takes the board for
plain Linux and compares 0x2000 to 0x8FFF with the backup. They differ, because the bootloader has
changed, so it stops with "holds a backup whose 0x2000 to 0x8FFF differ from this flash now: it is
of another state of this board ... Rename that folder (keep it), then run this again to take a new
backup."

Evidence [verified: my harness, cases 84 to 88]:

* 84 and 85: `flash-dualboot.bat COM10` (and `all tiny386`) after either cut: stops as above. So the
  failure message's first suggestion, "run this again", does not work, and DUALBOOT 12.2's "the
  script run again finishes the install" (1c) and "run again finds Linux's table and finishes the
  install" (1d) are wrong once the backup exists, which it always does by then.
* 86: `back-to-linux.bat COM10` after the cut writes the good backup's 0x2000 to 0x8FFF back. This
  is the way out, and it works.
* 87: with the backup folder renamed, as the stop says, the script takes a new "before" backup of
  the half-written bootloader and goes on to write the set.
* 88: later, `back-to-linux.bat COM10` (backup, the default once a backup exists) writes that
  half-written bootloader back and prints "Written. Linux starts on every reset again": the board
  then starts nothing [inferred: the bytes it writes are the cut state's]. `back-to-linux.bat
  COM10 small` or `full`, through download mode, still brings Linux back, and the renamed original
  backup is still there.

Plat's board has the dual boot in already, and `app` and `table` never take or compare a backup,
so this does not touch what he is about to do. It matters for the next first install (another
board, or after `back-to-linux.bat`).

Fix: when the table is Linux's and the backup is of this MAC but its 0x2000 to 0x8FFF differ only
in 0x2000 to 0x7FFF (its table part, 0x8000 to 0x8BFF, equal to the table read now), treat it as an
interrupted install: keep the existing backup and go on, or stop and name `back-to-linux.bat
COMport` as the first step. Change `:write_failed_boot` to name `back-to-linux.bat` first, and never
suggest renaming the backup when the table read is Linux's and the bootloader read is the dual-boot
one or none known. Correct DUALBOOT 12.2 (1c, 1d) and 13.5 (C5). Add the two cut states to the
harness (my `extra_cases2.py` adds `linux-dualbl` and `linux-cutbl`).

### L1 (minor): the line that names the cause of an emulator abort is not on the UART by default

Where: `i386.c` line 137 (`TRY1`: `dolog("@ %s %s %d" ...)` then `cpu_abort()`), lines 5092 to 5097
(`deliver()`: `dolog("shutdown: ...")` then `cpu_abort()`); `cpu_abort()` at line 214 calls
`cpu_abort_note` (`abort_note()`, `esp/main/esp_main.c` line 417) only after those lines.

With `[esp] uart_log = 0`, the default since v3.8, the UART is quiet when `dolog()` prints the
`shutdown:` or `@ file function line` line, so it goes to the ring and the panel only;
`abort_note()` then turns the UART on and the rest of the dump follows. Probe A [verified]: the
UART gets the count of held lines, `tiny386 v3.8 stopped the PC.` and `abort: -1 ...`, but not
the `shutdown:` line nor the `@` line; the ring, and so the crash screen, has both. With
`uart_log = 1` all of them reach the UART in order (the queue is written out first).
BUILD-NOTES 20.3 says the abort's dump is whole on the UART, FLASH.txt's last paragraph says "a
crash report always goes there"; for these two lines it does not. Plat reads the crash screen,
so this costs nothing in his runs, but the UART report loses the most telling line.

Fix: call the note before the line, or have `abort_note()` write the ring's last few lines since
the PC started to the UART before anything else (`console_ring_tail()` gives them).

### L2 (minor): after a panic that is not the emulator's own abort, the newest repeated lines and their count are missing from the crash screen

Where: `console.c` `lim_pass()` lines 712 to 748 (held lines go nowhere until a count is said) and
`console_draw_crash()` lines 1279 to 1289 (the crash screen is the ring's tail).

Only `abort_note()` says the held counts before the end. A panic of another kind (a load fault in a
device model, the interrupt watchdog at 800 ms, a stack overflow, an `assert()` that calls
`abort()` directly) runs the crash screen with up to 5 s of each kind's lines still held. Probe B
[verified]: 40 lines of one kind over 2 s, then `console_draw_crash()`: the ring holds the first 5
of them; the last one (`sel 77e3`) and the count of 35 are not there. In v3.7 all 40 were in the
ring, so the crash screen's "last lines of the log" showed the newest. The ring and the crash
screen are what Plat photographs.

Fix: in `console_draw_crash()` (panic context: no locks, but the frame buffer in PSRAM is already
written there), walk `s_lim` and put a `(not shown: N more lines like "...")` line for each kind
holding lines into the ring before `console_ring_tail()`, as `ring_note()` does.

### L3 (minor): the stall line (yellow 5) goes nowhere by default

Where: `esp/main/status_led.c` lines 204 to 210 (`console_uart_printf("status LED: the emulator has
made no progress for %d s (yellow 5)")`); `console.c` lines 831 to 840 (UART only) and 603 to 604.

`console_uart_printf()` sends to the UART alone. With `uart_log = 0` the UART is quiet while the PC
runs, so the stall line, and "the emulator goes on again", reach neither the UART nor the ring nor
the panel. Probe C [verified]: neither. The LED's yellow 5 still shows. TEST-PLAN 8c asks Plat to
note yellow 5; the log then has nothing to match it with.

Fix: log those two lines with `console_printf(CON_WARN, ...)` (ring, panel, and the UART when it is
on), or have `console_uart_printf()` keep a copy in the ring.

### F2 (minor): `app` over the tiny386-default table behind a bootloader with no key says VOL+ gives Linux

Where: `flash-dualboot.bat` lines 135 to 152 and 165 to 189: `app` reads the table but not the
bootloader.

The state is DUALBOOT 13.5's C3b: `back-to-linux.bat small` stopped between be5b725's bootloader and
its table, leaving be5b725's bootloader (no key) in front of the tiny386-default table. There
tiny386 starts with or without VOL+ and Linux cannot be reached. `flash-dualboot.bat COM10 app`
goes on and prints "At RESET now: no key: tiny386; VOL+ held: Linux", then "For Linux: hold the
VOL+ key" [verified: my harness case 71]. The write itself is harmless; the message is wrong and
could send Plat looking for a key fault. `all` mends the state (case 72), as does
`back-to-linux.bat` again (case 79), which FLASH.txt says.

Fix: in `app` mode over `dual386`, read 0x2000 as `table` mode does (`:read_dual_boot`) and, if it
is not the dual-boot bootloader, say so and name `back-to-linux.bat COMport` or `all`.

### D2 (minor): STATUS-LED.md's reading guide overstates what blue and green mean

Where: `STATUS-LED.md` section 8, lines 297 to 303.

The count moves when an access is asked for (`BF_ACTIVITY()` at the top of `bf_read_async()` and
`bf_write_async()`, before the card is reached), and the card access then runs on the emulator's
core [verified: `ide.c` 5c47263]. So a single long card wait shows one 20 ms blue look, then green
for the rest of the wait (and yellow 5 past 10 s); and a guest stuck retrying a refused access
shows steady blue. "A guest that seems to hang with the LED still blue is busy on its disk, not
stuck" and "Green with no blue at all ... means the guest is not reaching its disk at all" do not
hold in those cases, which are the ones v3.8 is about [inferred].

Fix: say that the light marks each access as it starts; a long wait for the card shows green, then
yellow 5; steady blue is many accesses, which may be a retry loop; the `Disk:` line tells them apart.

### P1 (minor): README says every image folder has `tiny386.elf`

Where: `README.md` line 75. The tiny386-default folder has no ELF, by design, as line 50 of the
same README, `build.sh` and BUILD-NOTES 20.7 say [verified: the folder holds 5 files]. Fix: "Each
image folder but the tiny386-default one ...".

### L4 (note): error lines are held back like any other

`ESP_LOGE` lines from the drivers go through the limit. Probe D [verified]: 12 lines of one
`E (...) diskio_sdmmc: ...` kind in 5 s give 5 lines, then one count line at `CON_INFO` (grey, not
red), and `console_problems()` counts 5. Nothing is lost outright (the count and the last line are
kept, and the `Disk:` line counts card errors separately), and `console_problems()` is only read
before the PC starts [verified: `esp_main.c` line 515]. If errors should never be held, exempt
lines whose level is `CON_ERROR` from `lim_pass()`, or say the count line at the held lines' level.

### L5 (note): at an abort the counts land after the abort's own lines

Probe A [verified]: in the ring the order is the first 5 of a storm, `shutdown: ...`, `@ ...`,
then `(not shown: 7 more lines like "inw 0xa2b <= 0xffff" in 1 s)`. Someone reading the crash screen
could take those 7 lines for something after the shutdown. It goes with L1's fix: say the counts
first.

### D1 (note): the steady green no longer shows that the LED's task is alive

v3.3 to v3.7's dark blink came from the LED task's own clock, so it never showed the emulator's
progress (the stall check does that, unchanged [verified: `git diff v3.7 v3.8`, test 15 part 9
passes]); but its stopping did show a core 0 that had stopped scheduling the LED task. Now a stuck
core 0 leaves the last colour on the LED, green or blue. A core 0 stuck with interrupts off is
still caught by the interrupt watchdog and the panic path's own crash pattern [inferred: 800 ms,
`CONFIG_ESP_INT_WDT_TIMEOUT_MS`]; a core 0 starved by a busy task of higher priority is not. The
frozen panel would show it too. Plat asked for a steady light; this is the price, worth one line in
STATUS-LED.md 8.

### F3 (note): `TINY386_BIN` set to the DOS board image passes the script's checks

`check_image` (`flash-dualboot.bat` lines 394 to 416) wants a valid ESP32-S31 image that fits:
the DOS board's `tiny386.bin` is valid, `Project name: tiny386`, `App version: v3.8`, 930,720 bytes
[verified: `esptool image-info`]. With tiny386 the default it would then start at every reset. The
DOS image's own board check stops it on a Korvo-1 before it drives anything but I2C, the UART and
the LED (BUILD-NOTES 17), and VOL+ still gives Linux, so the harm is a confusing boot [inferred].
Only a wrong `TINY386_BIN` gets there; the default is right. If wanted: refuse an image whose ELF
SHA-256 (in its app description) is the DOS board's, or check the size against the Korvo-1 set's.

### P2 (note): the top `SHA256SUMS` does not cover `research\`

`research\win95-core-v3.8.patch` is meant to go upstream in Plat's name, and BUILD-NOTES gives its
SHA-256 (which matches [verified]), but `SHA256SUMS` lists no file in `research\`. README line 61
says the list covers the docs, scripts, patch, card files and images, which it does. Adding the
patch would let `sha256sum -c` vouch for it too.

### P3 (note): DUALBOOT.md 13.1 still says the tiny386-default folder holds v3.7's app

13.1's third bullet ("beside the dual-boot bootloader and the v3.7 `tiny386.bin`") is the boot
swap's record; 13.6 has the v3.8 update. The folder now holds v3.8's `tiny386.bin`, the same file
as `images\korvo1-dualboot\` [verified: SHA-256 90ab146c in both].

### L6 (note): the new `uart_log` task's stack is not in the stack report

`esp/main/display.c` line 40 lists the tasks whose stack high-water marks go to the UART;
`uart_log` (3 KiB, `console.c` line 575, created only with `uart_log = 1`) is not among them, nor
is `status_led`. Its stack use on the board is [unknown]; it calls `snprintf()` (picolibc in this
build [verified: `sdkconfig.h`]). Add both names.


## Checked and found right

**The tiny386-default table and the scripts**

* The table is the Linux-default dual-boot table with two subtype bytes swapped and its MD5 entry
  made again; the first 224 bytes differ from Linux's own table in one byte [verified: my decoder,
  `checks\bootswap\bootswap-table.txt`].
* Under the dual-boot bootloader: no key starts tiny386 (factory); VOL+ held for the second starts
  the Linux loader (test); a tiny386 that fails its image check is passed over for Linux, key or no
  key; a Linux loader that fails its check with VOL+ held gives a reset while held, tiny386 when
  let go [verified: ESP-IDF source above; modelled by the flash simulation, paths 11 to 14 and C4;
  on the board: unknown].
* Neither app reads its own subtype or label: v3.8's `esp/main` calls no `esp_ota_` function and
  finds partitions by name only, for BIOS files and `storage`, which the dual-boot tables do not
  have [verified: `git grep` at the tag]. Its only flash write is NVS from `wifi.c` when an SSID is
  set, as before (DUALBOOT 9) [verified].
* `table` mode writes 0x8000 alone, and only over a dual-boot table with the dual-boot bootloader
  at 0x2000 (SHA-256 over 22,352 bytes); it refuses Linux's own table, tiny386's own, an unknown
  one, either dual-boot table behind be5b725's bootloader, and conflicting words; with the default
  already as asked it writes nothing [verified: harness cases 40 to 50, 57 to 60, 74, 77, 81].
* `all tiny386` over Linux: backup first, tiny386 first, then the bootloader and the
  tiny386-default table; a failed 16 MB read writes nothing [verified: cases 51, 70, 82]. `all`
  over a dual boot keeps the board's default unless told [verified: cases 52, 72].
* `back-to-linux.bat` treats `dual386` as it treats `dual` in every scope [verified: cases 61 to 68,
  78, 79]. `flash-standalone.bat` knows `dual386` and still asks [verified: cases 69, 80].
* Every esptool call in the three scripts is `read-mac`, `read-flash`, `image-info`, `version` or a
  `write-flash` that the harness cuts out; no erase command [verified: the stub's call logs, 88
  cases].
* App update from v3.7 (what Plat does next): only sectors between 0xE20000 and 0xEFBFFF change, and
  the result equals the whole v3.8 set written over be5b725 [verified: `checks\app-update-v3.8.txt`,
  read; the simulation it rests on rerun above].

**The disk light**

* Cost on the emulator's core: one load, add and store to a word in internal DRAM per guest request
  to a disk or CD image (`bf_read_async()`, `bf_write_async()`) and per floppy string transfer
  [verified: `ide.c`, `misc.c`, the ELF's symbol addresses]. Against a card access that follows, it
  is nothing [inferred].
* The LED task reads the two words from core 0 every 20 ms; `status_disk_step()` compares with the
  last look; only the task's own copy of the state gets `disk`, so the record and the replay are
  untouched [verified: `status_led.c` lines 186 to 225].
* The stall check: `status_watch_step()` and the emulator's beat unchanged; errors, the replay and
  the crash pattern still take precedence over green and blue [verified: `status_code.c`,
  `status_pattern()`; test 15 passes, its part 9 including a stopped emulator giving yellow 5].
* The panic path: `git diff v3.7 v3.8` touches no line of `crash.c`, `status_bb.h`,
  `status_led_panic()`, `status_led_halt()` or the panic pattern; `status_led.c` changes only in its
  header comment, a new setter and `led_task()`; `build.sh`'s bit-bang layout check passes in my
  rebuild for all three ELFs [verified].
* `send_rgb()` waits at most 20 ms for the last frame, blocking rather than spinning, so the 20 ms
  look cannot starve `vga_task` [verified: `status_led.c` line 172; `vga_task` priority 0, the LED
  task 1, `esp_main.c` line 1313, `status_led.c` line 283].

**The limit on repeated lines and the UART queue**

* `console_printf()`, `console_puts()` and `console_fatal()` are never held back; a fatal message
  turns the UART on [verified: `console.c` lines 815 to 847 and 994 to 1003].
* An emulator abort says every held count, writes the queue out, and writes the UART directly from
  then on, so `abort: ...` and the CPU dump reach the UART whole [verified: `esp_main.c` lines 417 to
  422; probe A]. L1 is the exception.
* No task waits for the UART while the PC runs: with `uart_log = 0` nothing is written to it; with
  `uart_log = 1` a line is copied into a 4 KiB queue under a spinlock and dropped (and counted) if it
  does not fit; the `uart_log` task on core 0, priority 1, writes only what the FIFO has room for
  [verified: `console.c` lines 488 to 563, 599 to 617]. The emulator's core can spin on the queue's
  lock while that task fills the FIFO, at most 128 one-byte register writes [inferred: some
  microseconds]. The only direct UART writes left are ROM printf from interrupts or critical
  sections and one `esp_rom_printf()` in `vga.c` line 1636 at start-up, as before [verified: grep].
* A limit or queue that cannot get its memory falls back to every line, or to the UART written
  directly, as before v3.8 [verified: `console.c` lines 566 to 580, 764 to 777].

**The package**

* Versions: `esp/version.txt` v3.8; every app says v3.8; `build.sh`, README, FLASH.txt, TEST-PLAN and
  STATUS-LED say v3.8; the dual-boot bootloader and both tables are v3.7's and the boot swap's,
  byte for byte, as FLASH.txt and TEST-PLAN say [verified: `previous-v3.7\SHA256SUMS`].
* Sizes: Korvo-1 `tiny386.bin` 899,024 bytes, 54.3 per cent of its 1,966,080-byte partition spare;
  standalone 899,024 of 1,835,008; DOS board 930,720 [verified: files and my rebuild's log].
* The card inis carry the five new keys commented out, each at its default [verified: read].
* The README's account of `previous-v3.7\` (25 inside, 12 from `v3\`, 4 from `scripts-before\`) is
  right [verified].


## Files of this review

WSL `~/win95-debug/review-board-safety-and-features/`: `tree/` (clone of the scratch tree at v3.8),
`ptree/` (pristine plus the package patch), `rb/` (`build.sh` from nothing) and `rb.log`, `simb/`
(the flash simulation), `hgen/` (the harness generators: `make_flow_review.py`,
`make_flow_review2.py`, `extra_cases.py`, `extra_cases2.py`, `ii-raw.txt`), `probe/`
(`review_console.c` and its UART files), `ht-out/` and `ht.log` (host tests), and the small `.sh`
scripts that ran them.

Windows scratch folder `...\scratchpad\rbsf\`: `decode.py` (the table decoder), `harness\` and
`harness2\` (the generated harnesses and their `runflow-out.txt`, 83 and 88 cases), `hcmp.py` (the
comparison with the package's harness record), and copies of the scripts above.
