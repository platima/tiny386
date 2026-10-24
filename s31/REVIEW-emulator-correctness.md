# Review: emulator correctness, tiny386 bare metal v3.1 (round 2)

Written 2026-10-21 by an independent reviewer (Claude), for Plat. Lens: what the emulated PC
sees. The i8042 fix, `kbd_input.c` (scancodes, typematic, the lights), the PS/2 mouse (packets,
the wheel, the IntelliMouse knocks), the `vga.c` display changes, disk flushing and data safety,
the ini path mapping, the speed readout and BENCH marks against the Linux route, and memory for a
14M guest.

Round 1 of this review read tag v3 (2bb1c76); its text is kept unchanged (headings moved down
one level) in the appendix at the end, because `REVIEW-RESOLUTION.md` refers to its numbers (E F1
to E F10). This round reads tag v3.1-tests (823d5cd), whose firmware is v3.1's byte for byte
[reported: BUILD-NOTES 11, `checks\reproducible-v3.1-tests.txt`]. Nothing here ran on a board: no
flashing, no serial port. The shared tree (`~/tiny386-idf-scratch/v3/tiny386`) and this folder
were only read; I worked in a clone in WSL `~/tiny386-idf-scratch/review-emulator-correctness/r2`.
The only file of this folder I changed is this one, so its line in `SHA256SUMS` (888a7df2...,
round 1's text) no longer verifies; I left `SHA256SUMS` alone, as the task is report only. The
appendix, with one `#` taken off each heading, gives round 1's file back byte for byte (SHA-256
888a7df2...) [verified].

Tags: [verified] read in the named source or seen in a test I ran; [inferred] follows from
those; [reported] someone else's statement; [unknown] only the board can say.


## 1. What I did

* Cloned the shared tree into my folder and checked out v3.1-tests (the shared tree's own
  status is unchanged: only `esp/dependencies.lock`, as before [verified: `git status`]). Diffs
  of the emulator files against `pristine` and against `v3` are in `r2/diffs/`.
* Host tests in the clone: 19 passed, 0 failed [verified: `r2/host-tests.log`]; again built
  with AddressSanitizer and UBSan (`-fno-sanitize-recover=undefined`, run under `setarch -R`):
  19 passed, no sanitizer report [verified: `r2/host-tests-asan.log`]. The two-thread mouse test
  built with ThreadSanitizer: 6 checks passed, 0 warnings [verified: `r2/tsan-ps2-mouse.log`].
* Read in full: `i8042.c` (the new deferred mouse, the queue, the keyboard), `kbd_input.c`,
  `esp_main.c`, `speed.c` beside `korvo/main.c`, `setup.c`, `display.c`, the `hid_parse.c` mouse
  path, `ch9350.c`'s mouse hand-over, and the `vga.c`, `ide.c`, `misc.c`, `pc.c` and `i386.c`
  changes.
* Applied the Linux route's `tiny386-korvo.patch` (SHA-256 fe2e9e0a...) to `pristine` again and
  compared the result with v3.1-tests file by file.
* Wrote two review tests (in my folder, not in the tree): `r2/rev2_mouse.c` (IMEX, the wheel,
  the ring's overflow, commands behind packets, a reset with reports waiting, a double click at a
  low sample rate, the keyboard with scanning off) and `r2/rev2_speed.c` (the mark watch). Ran
  round 1's `rev_vga_dos.c` again against v3.1-tests.
* Read struct sizes from the v3.1 release ELFs with the toolchain's gdb (`r2/sizes.gdb`).


## 2. Round 1's findings at v3.1-tests

| Round 1 | Status now | What I checked |
|-|-|-|
| E F1 (major) mouse stream out of step after a backlog | fixed | `i8042.c` 791 to 975 (with `PS2_KBD_HOOKS`): packets made in `kbd_step()`, whole or none (`ps2_queue_bytes()`, 543 to 567), paced at the guest's sample rate, backlog clamped to 1,023, a button change never merged into earlier movement. `test_ps2_mouse` passes and the same test fails on v3's `i8042.c` [verified: my run]. Two edge cases of the new ring remain: N1 and N3 below |
| E F2 (minor) floppy writes not flushed | fixed | `esp_main.c` 344 to 354 and 377 to 403 count and flush the floppies with the disks; `misc.c` 455 to 466 calls `fsync()` on `BUILD_ESP32` [verified: source] |
| E F3 (minor) PS/2 state changed from two cores | mouse fixed, keyboard open | the mouse state is now the emulator thread's only (ring, TSan clean). `ps2_put_keycode()` still queues E0 at once and the second byte 1 or 10 ms later (`i8042.c` 594 to 613), so an ACK from the emulator's core can still fall between them [verified: source]; deferred by REVIEW-RESOLUTION |
| E F4 (minor) card errors silent | fixed for the IDE disks | `ide.c` 1966 to 1972, 2014 to 2027, 2049 to 2058: a refused flush is counted; a refused read or write is counted and returns -1, which `ide_sector_read()` and `ide_sector_write_cb1()` turn into an aborted command (605 to 610, 659 to 664); `esp_main.c` 359 to 372 puts a red line on the console [verified: source]. Not the floppies: N5 |
| E F5 (note) 132-column text not drawn | open | `vga.c` still returns when the text is wider than the frame buffer [verified]; deferred |
| E F6 (note) setup test and long output folders | fixed | `setup.c` 68 to 71 passes over a folder whose path does not fit [verified: source] |
| E F7 (note) no mouse scaling | open | `AUX_SET_RES` still only stored [verified]; deferred. See also N7 |
| E F8 (note) NE2000 without Wi-Fi | open | `pc.c` unchanged [verified]; deferred |
| E F9 (note) stack headroom | fixed (figures unknown) | `display.c` 38 to 58 logs every task's least free stack every 10 s [verified: source]; the figures are the board's [unknown] |
| E F10 (note) boot mouse wheel, JIS keys, set 1 | documented, unchanged | [verified: source] |


## 3. New findings, most serious first

### N1 (minor): a click can vanish in the mouse ring's overflow

What happens [verified: `i8042.c` 474, 854 to 867, 950 to 975]: reports wait for `kbd_step()` in
a 32-entry ring. When it is full, and from then on until the emulator takes it, every report is
merged into one word (`carry`), which keeps the summed movement but only the newest button
state (`carry_pack(..., buttons_state)`, lines 961 and 963). The emulator takes that word only
once the ring is empty. A press and a release that both land in the merged word give no packet
with the button down: the click never reaches the guest.

The test [verified: `r2/rev2_mouse.c` case C]: 100 reports of 1 count right while `kbd_step()`
does not run, the left button down in reports 40 to 49. The guest gets one packet, 100 counts
right, no button. The same click in reports 10 to 19 (inside the ring) arrives. Case G, a double
click while moving with a 1,000 Hz mouse and the guest's sample rate at 20, keeps both clicks
[verified], so the pacing alone does not lose them.

When it bites [inferred]: 32 reports while the drain is held up: 32 ms with a 1,000 Hz mouse,
256 ms at 125 Hz. Either the emulator's thread is held up that long (a slow card access, a
reset reloading the BIOS from the card), or the drain stops at a button change behind a packet
that cannot go (`ps2_mouse_step()` 924 to 926) because the guest is not reading. Rare in use.

Fix: keep the presses that are merged. For example a second word that the reporting side ORs
each newly pressed button into, and that `ps2_mouse_step()` takes with an exchange and sends as
a press before the merged state; or simply a larger ring (256 reports are 1,536 bytes of
internal RAM in `PS2MouseState`, which is `malloc()`ed once).

### N2 (minor): the BENCH end screen comes up for any program that writes 0xAF to 0000:04F0

What happens [verified: `speed.c` 47 to 66, 196 to 227]: the firmware watches the byte at 04F0h
all the time; the Linux route only with `-marks` (`korvo/main.c` 930). A change of that byte to
0xAF counts as the end of BENCH when no BENCH run has ended yet, even with no mark before it.
The console then takes the panel with "BENCH.COM is done: 0 pieces".

The test [verified: `r2/rev2_speed.c`]: `speed_step()` with 0 for 100 steps and then one 0xAF
returns `SPEED_MARKS_DONE` with 0 marks; a lone 0xA5 followed by 0xAF gives a table of one piece.

When it bites [unknown]: 0040:00F0 to 00FF is the BIOS's area for programs to talk to each other,
so any program may put any byte there; which programs do is unknown. The picture comes back with
Ctrl+Alt+F12, and only once until a new 0xA1.

Fix: count 0xAF as the end only after a run that began with 0xA1 (for example `s->nmarks > 0`
and a flag set by 0xA1), and drop pieces that did not start from 0xA1. `test_speed.c` could take
the two cases above.

### N3 (note): movement from before a mouse reset can follow it

[verified: `i8042.c` 1128 to 1148, 897 to 903; `r2/rev2_mouse.c` case F] `AUX_RESET` and
`AUX_SET_DEFAULT` clear the movement already taken from the ring (`mouse_forget()`), but not the
reports still in the ring or the merged word. They are dropped only because `mouse_apply()`
ignores reports while the mouse is off. If the guest resets the mouse, reads FA AA 00 and enables
it again within one `pc_step()` (512 instructions, so a driver polling port 64h can), the next
`kbd_step()` sends the old movement: in the test, `08 64 00`, 100 counts that happened before
the reset. Effect [inferred]: a small pointer jump as a driver starts. Fix: in those two commands,
which run on the emulator's thread (the ring's consumer), also move `ring_tail` to `ring_head`
and exchange `carry` with 0.

### N4 (note): the keyboard sends scancodes while the guest has scanning off

[verified: `i8042.c` 460 declares `scan_enabled` and 694, 732, 742, 747 set it; nothing reads it; `ps2_put_keycode()`
582 to 622 does not look at it; `r2/rev2_mouse.c` case E: after F5, a key press reaches port 60h
as `1E 9E`] A real keyboard sends nothing between F5 and F4 [inferred: what "disable" means].
Pristine tiny386, and the Linux route
has it too [inferred: same code]. Effect [inferred]: keys typed while a guest resets or sets up
the keyboard reach it; typematic repeat does too. Fix: in `ps2_put_keycode()`, drop the code
(not the delayed second byte already owed) while `scan_enabled` is 0; under `PS2_KBD_HOOKS` if
the Linux route's binary is to stay as it is.

### N5 (note): floppy write and flush failures are still silent

[verified: `misc.c` 575 to 579, 455 to 466] `emulink_data_write_string()` reports success when
`fwrite()` has taken the bytes even if the following `fflush()` fails, and `emulink_flush()`
ignores `fflush()` and `fsync()`. Neither counts toward the console's "the card refused" line,
which reads `ide_error_count()` only (`esp_main.c` 363). A floppy write whose bytes sat in the
C library's buffer when the card went away is lost without a word [inferred]. Fix: on
`BUILD_ESP32`, a failed `fflush()` sets `e->status = -1`, and failures are counted beside
`bf_errors`.

### N6 (note): ATA FLUSH CACHE is refused rather than used

[verified: `ide.c` 179, 183 define `WIN_FLUSH_CACHE` and `_EXT`; `ide_exec_cmd()` 715 to 783 has
no case for them, so they are aborted] Windows 95 does not send it [inferred]; Windows 98 and
later, and Linux guests, send it at shutdown [inferred]. Answering E7h and EAh with `ide_flush()` (and
`emulink_flush()`) would put the image's directory entry on the card at once rather than 2 s
later. Pristine behaviour; an improvement, not a fault.

### N7 (note): mouse throughput is capped at 127 counts a packet

[verified: `i8042.c` 802 and 803 clamp each packet to -127..127; `kbd_input.c` 413 to 431 clamp a
report to 512; the backlog to 1,023] The PS/2 packet's 9-bit fields allow -256..255. With packets
now paced at the guest's sample rate, the most the guest can be given is 127 times the rate
(12,700 counts a second at 100); a faster flick of a high-dpi mouse fills the 1,023-count
backlog and the rest is dropped [inferred]. Together with E F7 (no scaling), a 1,600 dpi mouse
moves the pointer much further than a PS/2 mouse would, and then stops short on fast moves.
Clamping to -255..255 doubles the room; scaling is the real cure.

### N8 (note): a mouse command's answer waits behind packets already queued

[verified: `r2/rev2_mouse.c` case D: 60 packets queued unread, then F5: the ACK is byte 181]
A real mouse stops and answers [inferred]. Pristine behaviour; drivers that read until they see FA cope
[inferred]. With the pacing, the queue only grows while the guest is not reading, so this
matters only after a pause [inferred].

### N9 (note): the DOS board's picture placement is not in the host tests

`tests/host/run.sh` builds `vga.c` with `board_korvo1.h` only (lines 64 to 71). Round 1's
`rev_vga_dos.c` (in my folder, not the tree) still passes 8 of 8 at v3.1-tests with
`board_dosboard.h` [verified: `r2/rev_vga_dos.log`]: 80x25 and 40x25 text at x 40 to 759, y 100
to 499; 640x480x16 at (80,60); 320x200 doubled at (80,100); VBE 800x600 whole; VBE 1024x768
scaled to 800x600; VBE 640x480x8 at (80,60). Worth adding to `run.sh`.

### N10 (note): PSRAM left after the PC is built is not shown

[verified: `esp_main.c` 423 to 427] The "PC built" line gives internal RAM only. The 16-colour
cache comes from the 256 KiB left outside the pool (`board_korvo1.h`, `board_dosboard.h`
`PSRAM_KEEP_FREE`): 153,600 bytes at 640x480 and 240,000 bytes for the DOS board's 800x600
planar mode [verified: `vga.c` 1620 to 1628, cols x rows x 4]. If it does not fit, `vga.c` draws
the slow way without a word. A PSRAM free figure on the same line, and in the 10 s UART line,
would show whether 800x600x16 on the DOS board has its cache [unknown until the board runs].


## 4. Checked and found right

Keyboard and mouse
* The i8042 queue fix: `test_ps2_queue` passes on this tree and fails on `pristine` [verified].
* `kbd_input.c` is unchanged since v3 apart from the repeat task's start check [verified: `git
  diff v3`], so round 1's checks of set 1, the sequences, typematic and the lights stand. I read
  `set1_of[]` and `set1_mod[]` (52 to 96) again and traced Pause: `0xE11D` makes E1 then 1D at
  the next code, so a press gives E1 1D 45 E1 9D C5 [verified: source]. Typematic: delay
  (1 + bits 5..6) x 250 ms, period (8 + bits 0..2) x 2^(bits 3..4) x 4.167 ms, default 0x2B
  (500 ms, 91.674 ms) at start and on FF, F5, F6 [verified: 107 to 113, `i8042.c` 692 to 699].
* The deferred mouse [verified: source and tests]: one producer (the USB HID task through
  `kbd_input.c`'s lock, or the CH9350L task on the DOS board; `wifi.c` is not built for these
  boards and `lcd_elecrow7s3.c` is inside `#ifdef USE_LCD_RGB_ELECROW7S3`); ring entries written
  before a release store of `ring_head` and read after an acquire load; `ring_tail` likewise.
  IMEX: the 200, 200, 80 knock gives ID 4; wheel away is Z = -1 (`08 00 00 0F`), towards is +1;
  button 4 is bit 4 and button 5 bit 5 of the fourth byte; ten notches go out as -7 and -3
  [verified: `rev2_mouse.c` case A]. IMPS/2 (ID 3) and its wheel sign: `test_kbd_input`. A wheel
  turn on a plain PS/2 mouse gives one empty packet `08 00 00`, as pristine tiny386 does
  [verified: case B]. The HID side: wheel is Generic Desktop 0x38, relative, signed when the
  logical minimum is negative; buttons from usage 1 of the Button page, up to 8 (`hid_parse.c`
  148 to 161, 249 to 255) [verified].

Display
* `vga.c` changed since v3 only in the VBE default depth branch, inside `VGA_TWO_CORES`
  [verified: `git diff v3 v3.1-tests`].
* Against the Linux route's patched sources at v3.1-tests [verified: `r2/lr-vs-v31-*.diff`]:
  `vga.h`, `misc.h`, `pc.c`, `pc.h`, `i386.c`, `unaligned.h`, `term.c`, `term.h`, `fmopl.c`,
  `i8254.c`, `i8259.c` are identical; every changed line of `vga.c` is inside `VGA_TWO_CORES`,
  `VGA_REDRAW_DIRTY_ROWS`, `VGA_SHRINK_LARGE`, `VGA_RETRACE_REALTIME` or `BUILD_ESP32`, apart from
  blank lines, one comment and `seen_malloc()` (plain `malloc()` off the board); `ide.c` and
  `misc.c` differ only inside `BUILD_ESP32` or `IDE_RAW_SD`. Nothing of the Linux route's VGA
  work is lost.
* Retrace from the clock, worked through again: mode 03h, CR00 0x5F gives 100 characters of 9
  dots at 28.322 MHz, 31.77 us a line; 449 lines, 14.26 ms, 70 Hz; retrace from line 412 to the
  end, about 1.2 ms. Mode 13h 70 Hz, 12h 60 Hz, VBE 60 Hz [verified: `vga.c`
  `vga_retrace_status()` and `test_vga`].

Disks
* Each IDE write goes `fwrite()`, `fflush()` (`ide.c` 2049 to 2058), so whole sectors reach FatFs
  at once, and `f_write()` writes whole sectors straight to the card (ESP-IDF `ff.c` 4197 to
  4211, "Write maximum contiguous sectors directly") [verified: source]; the 2 s quiet flush
  (`f_sync()`, ESP-IDF `vfs_fat.c` 622 to 628) then writes the directory entry. A RESET within
  those 2 s should lose at most the image's timestamp [inferred: not run]. Floppies are now in the count and the flush; the final flush on power-off is there
  [verified: `esp_main.c` 377 to 403].

ini, speed, memory
* Paths: `/mnt/sd` and `/mnt/win` mapped on a whole path component; `/mnt/win` is the partition
  labelled WIN9X, else the first other mounted FAT partition (`storage.c` 312 to 323);
  `win95-14m.ini` names `/sdcard3/win95.img` [verified; `test_setup` covers the mapping]. A
  value with `..` would not open: ESP-IDF's FatFs has `FF_FS_RPATH 0` (`ffconf.h` 199), and the
  console then says there is no such file [verified: source]; none of the inis has one.
* Speed and BENCH against `korvo/main.c` 594 to 652 and 892 to 946: the same counter, the same
  run and halt split, the same mark rules, line formats and rounding, wrap-safe 32-bit steps
  [verified: source, `test_speed`]. Differences: the firmware watches the marks all the time
  (N2), checks every 512 instructions where the Linux route's build checks every 4,096
  (`-DPC_STEP_COUNT=4096`, its `build.sh` line 127), and sets the flush time aside as the
  Linux route sets drawing and card waits aside; drawing is on the other core here, so it is not
  in the running time on either route [verified: source].
* Memory: struct sizes on the v3.1 ELFs are round 1's (VGAState 18,324, NE2000State 49,224,
  SB16State 4,844, FM_OPL 4,788, IDEState 2,144, PCIDevice 380, PCIBus 1,156); the new
  `PS2MouseState` is 520 bytes, from `malloc()`, not the device window [verified: gdb]. The
  device window holds about 92 KB of 128 KiB with one disk [inferred: sum of the `pcmalloc()`
  calls, each rounded to 32]; the OPL tables are static on `BUILD_ESP32` (`fmopl.c` 48 to 51)
  [verified]. 14M with vga_mem_size 320K needs 14,672 KiB of the pool, checked exactly as it is
  later taken (`esp_main.c` 584 to 609, 686 to 699) [verified]; the pool is about 15,378 KiB on
  the Korvo-1 and 15,190 on the DOS board (16 MiB less the frame buffer less 256 KiB) [inferred].
  Internal RAM: 262,462 and 259,448 bytes left after static use [verified:
  `checks\build-checks-v3.1-tests.txt`]; what is left at run time is on the boot console
  [unknown].


## 5. What the host tests do not cover

* `ide.c`'s board paths: the error returns, `fflush()` and `fsync()`, and `run_pc()`'s 2 s flush
  are never built on the host; only reading shows them right.
* The mouse ring's overflow (N1), a reset with reports waiting (N3), IMEX and buttons 4 and 5
  (my case A passes), commands behind packets (N8). `test_ps2_mouse`'s two-thread case calls
  `ps2_mouse_event()` directly, not through `kbd_input.c`'s lock.
* The keyboard's E0 pair against an ACK from the other core (E F3, open); scanning off (N4); the
  repeat task's tick rounding (the tests call `kbd_input_poll()` with a fake clock).
* `vga.c` with the DOS board's header (N9); 132-column text; split screen with VBE; two cores'
  caches.
* A lone 0xAF in the speed watch (N2).
* `esp_main.c`'s `parse_ini()` glue: the BIOS-in-flash fallback, the missing WIN9X red line, the
  200-character `resolved` buffer.
* The device window and the PSRAM pool: arithmetic only.


## 6. Files

All in WSL `~/tiny386-idf-scratch/review-emulator-correctness/r2/`:
* `tree/` (clone at v3.1-tests), `diffs/` (emulator files against `pristine` and `v3`)
* `host-tests.log`, `host-tests-asan.log`, `tsan-ps2-mouse.log`
* `rev2_mouse.c`, `rev2_mouse.log` (cases A to G), `rev2_speed.c`, `rev2_speed.log`,
  `rev_vga_dos.log` (round 1's test, this tree), `rt/` (their builds)
* `lr/` (pristine with the Linux route's patch), `lr-vs-v31-vga.diff`, `lr-vs-v31-i8042.diff`,
  `lr-vs-v31-ide.diff`, `lr-vs-v31-misc.diff`
* `sizes.gdb`; `round1-original.md` (round 1's file as it was, SHA-256 888a7df2...)

Round 1's files are one level up, as its own list says.


# Appendix: round 1 (tag v3), as written

## Review: emulator correctness, tiny386 bare metal v3

Written 2026-10-21 by an independent reviewer (Claude), for Plat. Lens: what the emulated PC sees.
The i8042 fix, the keyboard and mouse layer (`kbd_input.c`), the `vga.c` display changes, disk
flushing, the ini path mapping, the speed readout and BENCH marks, and memory sizing for a 14M
guest. Nothing here ran on a board: no flashing, no serial port. The tree
(`~/tiny386-idf-scratch/v3/tiny386`, tag v3 = 2bb1c76) and this folder were read only; my work
files are in WSL `~/tiny386-idf-scratch/review-emulator-correctness`.

Tags: [verified] read in the named source or seen in a test I ran; [inferred] follows from those;
[reported] someone else's statement; [unknown] only the board can say.


### 1. What I did

* Read the diffs against `pristine` and against the v2 baseline (c2f415f) for `i8042.c`,
  `vga.c`, `ide.c`, `pc.c`, `misc.c`, `sb16.c`, `pcspk.c`, `adlib.c`, and read `kbd_input.c`,
  `usb_input.c`, `esp_main.c`, `display.c`, `speed.c`, `setup.c`, the board headers and the
  sdkconfig of both builds.
* Ran the host tests myself: 14 of 14 pass with a short output folder [verified:
  `host-tests-short.log`], and 14 of 14 again built with AddressSanitizer and UBSan
  (`-fno-sanitize-recover=undefined`), no runtime errors [verified: `host-tests-asan.log`]. The
  first sanitizer attempt looped printing `AddressSanitizer:DEADLYSIGNAL` from the boot menu test
  onwards (GCC 12's ASan with this WSL kernel's address randomisation, an environment problem,
  not the code) [inferred]; I stopped my own task, removed the 14.8 GB log it had written in my
  folder, and reran with randomisation off (`setarch -R`), which passed.
* Wrote two review tests (in my folder, not in the tree): `rev_mouse_test.c` (mouse packets
  after a backlog) and `rev_vga_dos.c` (`vga.c` built with `board_dosboard.h`, picture placement
  at 800x600, 40-column text and its retrace rate).
* Applied the Linux route's `tiny386-korvo.patch` to `pristine` and compared the result with
  v3's emulator files, line by line for `vga.c`.
* Read struct sizes from both release ELFs with the toolchain's gdb, and compiled the emulator
  and port files with the build's own flags plus `-fstack-usage`.


### 2. Findings, most serious first

#### F1 (major): the PS/2 mouse stream goes out of step after a backlog

What happens [verified: `i8042.c` lines 767 to 793, 485 to 493]: `ps2_mouse_event()` adds each
movement to `mouse_dx`/`mouse_dy`; if the mouse queue holds fewer than 240 bytes it then sends
packets in a loop until the whole accumulated movement is gone, at most 127 counts a packet. The
240 check is made once, before the loop. While the queue is at 240 or more nothing is sent and
the movement keeps accumulating, with no limit. When the guest reads a little, the next event
sends the whole backlog at once; `ps2_queue()` drops every byte past 256 one at a time, so a
3-byte packet is cut part way and every packet after it is framed wrongly by the guest.

The test [verified: `rev_mouse_test.c`, through `kbd_input.c` and `i8042.c` as the host tests
build them]: mouse enabled, 200 reports of 100 counts right with nothing read, the guest reads
four packets, one more report, the guest reads everything, then the mouse moves 5 down.

* 3-byte packets (plain PS/2, which Windows 95's own mouse driver uses [inferred]): 259 bytes
  read, not a multiple of 3. The last packet should be `28 00 FB` (5 down); the guest frames it
  as `08 28 00`, and from then on every packet is shifted. The shifted first byte `FB` has bit 3
  set, so it passes the usual sync check, and its bits 0 and 1 read as both buttons down.
* 4-byte packets (after the IntelliMouse knock): 260 bytes, whole packets in this run, because
  the free space happened to be a multiple of 4. Movement was lost but the framing held.

`kbd_input.c` lines 413 to 433 clamp each report to 512 counts so that "one report cannot fill
the queue", which holds for a single report; the backlog path is not covered [verified].

When it bites [inferred]: whenever the guest reads IRQ 12 more slowly than reports arrive. The
emulator sends one packet per USB or CH9350L report (`usb_input.c` lines 96 to 106,
`kbd_input.c` 415 to 433), not at the guest's sample rate, so a 500 or 1,000 Hz mouse sends 1,500
to 3,000 bytes a second, each byte an interrupt. At the 0.358 million instructions a second the
Linux route measured [reported: Plat], Windows 95 is unlikely to keep up with that, and a long
disk operation or masked IRQ 12 does the same with any mouse. The code is pristine tiny386
(`i8042.c` differs from the Linux route's only by the hooks and the mutex fix [verified: my
diff]), so the Linux route has it too [inferred].

Fix: make a packet all or nothing. A `ps2_queue_packet()` that takes the queue's mutex once,
checks there is room for the whole packet and queues all of its bytes or none (this also cures
F3's interleaving); stop the send loop when a whole packet does not fit; clamp the backlog
(`mouse_dx`, `mouse_dy` to about 1,024, `mouse_dz` to 127). Better still, send no more often than
`mouse_sample_rate` and leave the rest in the backlog. `rev_mouse_test.c` can become a host test.

#### F2 (minor): writes to a floppy image are never put on the card with f_sync on the board

[verified] `run_pc()` (`esp_main.c` lines 337 to 361) counts `ide_write_count()` only and calls
`ide_flush()` only. `emulink_flush()` (`misc.c` lines 455 to 466) has `fdatasync()` for Linux and
nothing for `BUILD_ESP32`, and nothing in `esp/main` calls `emulink_write_count()` or
`emulink_flush()` (grep). The Linux route counts and flushes both (`korvo/main.c` lines 894 and
986 to 1004). So `fda`/`fdb`, which the ini accepts, are written through `fflush()` only.

Consequence [inferred]: with `CONFIG_FATFS_PER_FILE_CACHE=1` [verified: build `sdkconfig.h`] a
sector written in pieces stays in the file's FatFs buffer until the next access to another
sector of that image or a sync. DOS writes the FAT and directory sectors last, so a RESET or
power-off after copying a file to A: can lose exactly the sectors that make the file visible.

Fix: in `run_pc()`, count `ide_write_count() + emulink_write_count()` and call
`emulink_flush(pc->emulink)` beside `ide_flush()`; in `misc.c`, an `#elif defined(BUILD_ESP32)`
branch with `fsync(fileno(...))`, as `ide.c` line 1962 has. The Linux route's build is not
touched by either.

#### F3 (minor): PS/2 state is changed from two cores without a lock

[verified] `ps2_mouse_event()` runs on core 0 (the HID driver's task, `usb_input.c` line 362; the
CH9350L task, `ch9350.c` line 530) and changes `PS2MouseState` and queues a packet one
`ps2_queue()` call per byte. Core 1 runs `ps2_write_mouse()` (queues ACKs and replies, changes
`mouse_status` and `mouse_type`, sends a packet on AUX_POLL) and `ps2_read_data()`.
`kbd_input.c`'s mutex orders only the core 0 callers.

Effects [inferred]: a command reply can land inside a movement packet if the guest sends a mouse
command while the mouse streams (a BIOS INT 15h C2 reset is one way); in remote mode two cores
change `mouse_dx`. On the keyboard side `ps2_put_keycode()` queues E0 at once and its second byte
up to 10 ms later (`i8042.c` lines 530 to 534, 570 to 573), so an ACK from `ps2_write_keyboard()`
on core 1 can fall between them. Rare. tiny386's own ESP32 USB path has the same shape
[inferred].

Fix: the all-or-nothing packet of F1; or hand input to the emulator's core (`kbd_input.c` queues
events, `kbd_step()` or `run_pc()` drains them), which removes the cross-core access altogether.

#### F4 (minor): card read and write errors are silent

[verified] `bf_write_async()` ignores what `fwrite()` returns and reports success to the guest
(`ide.c` lines 2005 to 2023); `bf_read_async()` ignores `fread()` (line 1999); `ide_flush()`
ignores `fsync()` (line 1962). Pristine behaviour. The Korvo-1 has no card detect, so a card
pulled while the PC runs, or one that fails, is noticed by nobody: the guest believes its
writes [inferred].

Fix: return -1 from `bf_write_async()` on a short write so the guest's IDE driver sees an error,
and count failures for one red console line (through a hook, as `tiny386_fatal()` is, since
`ide.c` is shared with the Linux route).

#### F5 (note): text modes wider than the frame buffer are not drawn at all

[verified] `vga_text_refresh()` returns at line 1041 ("not enough space") when the text is wider
than the panel, so 132-column text leaves the previous picture frozen, on both boards (1,056 or
1,188 pixels against 800). Already listed by the core work [verified: `NOTES-core1.md` 3.7].
A clear and a one-line console notice would at least say why.

#### F6 (note): the setup host test depends on the length of its output folder

[verified] With `OUT` = `.../review-emulator-correctness/hosttest-out` the card folder is 103
characters, `setup_list.dir` is 96 bytes (`setup.h` line 42) and `setup_find()` truncates it with
`snprintf` (`setup.c` line 87): the checks at `test_setup.c` lines 175 and 182 fail. With a
92-character folder all 14 tests pass. Card paths are short (`/sdcard/tiny386`), so the board is
not affected [inferred]. Fix: the test makes its card under a short `mkdtemp()` folder, and
`setup_find()` passes over a place whose path does not fit instead of truncating it.

#### F7 (note): mouse counts go to the guest one for one

[verified] AUX_SET_RES and AUX_SET_SAMPLE are stored but never used (`i8042.c` lines 840 to 843,
882 to 917), and `kbd_input_mouse()` does no scaling. A 1,000 to 1,600 dpi USB mouse moves the
guest pointer several times further than a PS/2 mouse at the default 4 counts per mm would
[inferred]. The Linux route does the same [inferred]. A scale setting in the ini, or honouring
the resolution command, would help; pacing at the sample rate belongs with F1.

#### F8 (note): the NE2000 is built on boards without Wi-Fi

[verified] `pc_new()` creates it unconditionally (`pc.c`, `isa_ne2000_init`), taking 49,248
bytes of the 131,072-byte device window (gdb: `sizeof(NE2000State)` 49,224 on both ELFs), while
`NO_WIFI` means nothing is behind it. The guest finds a card at 0x300, IRQ 9 that never passes a
packet [inferred]. Leaving it out on `NO_WIFI` boards frees 38 per cent of the window.

#### F9 (note): task stack headroom not measured

`i386_main` and `vga_task` have 4,096 bytes each (`esp_main.c` lines 983, 984). Largest frames
from `-fstack-usage` with the build's own flags [verified: `su/*.su`]: `vga_graphic_refresh`
1,184 bytes (vga_task); `cpu_exec1` 336, `pmcall` 320, `call_isr` 240, `speed_run_step` 224 plus
`speed_mark_line` 208 at the end of BENCH (i386_main, with `console_printf` and picolibc's
printf below them). The disk path's FatFs and SDMMC frames were not counted [unknown]. Likely
fine [inferred], but cheap to know: add `uxTaskGetStackHighWaterMark()` for both tasks to the
10-second speed line on the UART.

#### F10 (note): smaller keyboard and mouse points

* Boot-protocol mice give a wheel only if they put it in byte 3 (`usb_input.c` lines 96 to 101);
  one that keeps strictly to the boot report gives none [inferred]. Matters only with a
  wheel-aware guest driver.
* The JIS keys (usages 0x87 to 0x8B) are not mapped, and could not be passed as plain codes:
  `ps2_put_keycode()` treats codes 0x60 to 0x7F as Linux keycodes (`i8042.c` lines 535 to 549)
  [verified]. No effect on Plat's keyboards [inferred].
* Scancode set 1 is sent whatever the controller's translation bit says, and command F0 (select
  set) is only acknowledged; pristine behaviour. Fine for DOS and Windows 95; a guest that turns
  translation off would misread keys [inferred].


### 3. Checked and found right

Keyboard
* The i8042 fix gives the mutex back on the queue-full path (`i8042.c` 485 to 493); its host test
  fails on `pristine` and passes on v3 [verified: my run].
* tiny386 takes scancode set 1 (`i8042.c` 516 to 517; the `translate` flag is never read)
  [verified], and `kbd_input.c` produces set 1. Every entry of `set1_of[]` and `set1_mod[]`
  (`kbd_input.c` 52 to 96) read against the usual USB usage to set 1 table: letters, digits,
  punctuation, ISO # and \ keys, F1 to F12, the E0 navigation block, the keypad, Menu, the eight
  modifiers [inferred: checked by hand against the Microsoft translation table as I know it, not
  fetched; the host test's own table agrees, verified].
* Print Screen (plain, with Ctrl or Shift, with Alt), Pause and Ctrl+Pause traced through
  `ps2_put_keycode()`'s E0/E1 handling: E0 2A E0 37 / E0 B7 E0 AA, E0 37, 54/D4,
  E1 1D 45 E1 9D C5, E0 46 E0 C6 [verified: source and host test].
* Typematic: (1 + D) x 250 ms and (8 + A) x 2^B x 4.17 ms (`kbd_input.c` 107 to 113); default
  0x2B at start and on keyboard commands FF, F5, F6; only the last key repeats; a repeat is
  skipped while 8 or more bytes wait; the repeat task's tick rounding never repeats early and
  keeps the average rate [verified: source; inferred for the timing].
* Lights: PS/2 bit order turned into USB order (`kbd_input.c` 478 to 490); the callbacks only
  post (`usb_input.c` 304 to 308, queue wait 0) or store (`ch9350.c` 420 to 423), so the emulator
  core never waits on USB or the UART; the lock order has no cycle (core 0: kbd_input mutex,
  then delay flag, then queue mutex; core 1 never holds the delay flag while taking the
  kbd_input mutex) [verified: source; inferred for the lock order].

Mouse
* IntelliMouse knock 200, 100, 80 gives ID 3; 200, 200, 80 gives ID 4; F2 replies with it; wheel
  sign `-wheel` (USB away from the user positive, PS/2 towards positive); buttons 4 and 5 in
  bits 4 and 5 of the IMEX byte; dy negated for PS/2 [verified: `i8042.c` 719 to 765, 882 to
  917; host test covers ID 3].

Display
* Retrace from the clock (`vga.c`, `vga_retrace_status()`): CR00 + 5, CR06 with CR07 bits 0 and
  5, CR12 with bits 1 and 6, CR10 with bits 2 and 7, MSR clock select, SR01 bits 0 and 3. Mode
  03h and 13h give 449 lines at 31.8 us, 70 Hz; 12h gives 525 lines, 60 Hz; 40-column text
  70 Hz [verified: host test, and my 40-column check in `rev_vga_dos.c`].
* Port 3BAh shares 3DAh's path (`vga.c` 2200 to 2201), and the monochrome ports answer 0xFF in
  colour mode (2127 to 2128, 2226 to 2227), so routing 3B4h, 3B5h and 3BAh to the VGA in `pc.c`
  cannot make a colour VGA look like an MDA [verified].
* Placement on the DOS board, `vga.c` built with `board_dosboard.h` [verified: `rev_vga_dos.c`,
  8 of 8]: 80x25 and 40x25 text at x 40 to 759, y 100 to 499; 640x480x16 at (80,60); 320x200
  doubled at (80,100); VBE 800x600x16 whole; VBE 1024x768 scaled to fill 800x600; VBE 640x480x8 at
  (80,60). Built with `board_korvo1.h` the same program shows the Korvo-1 places its header
  gives (text at y 40, 640x480 at (80,0), 800x600 and 1024x768 scaled to 640x480 at (80,0))
  [verified].
* Nothing of the Linux route's VGA work is lost: against the Linux-route-patched `vga.c`, every
  changed line of v3's is inside `VGA_TWO_CORES`, `VGA_REDRAW_DIRTY_ROWS`, `VGA_SHRINK_LARGE`,
  `VGA_RETRACE_REALTIME` or `BUILD_ESP32`, except `seen_malloc()`, which is `malloc()` off the
  board [verified: my diff script]. `pc.c`, `misc.c`, `i386.c`, `term.c`, `vga.h`, `ide.h` and
  `unaligned.h` are identical to the Linux route's [verified]. Both boards define `BPP 16`,
  `VGA_STORE_CHANGED` and `I386_ALIGNED_ONLY`, so the quick 16-colour path, the deferred clear
  and the aligned-only CPU paths are on [verified: board headers].

Disks, ini, speed
* `ide_flush()`: after 2 s with no guest write, as the Linux route; `fsync()` (FatFs `f_sync`)
  on the board; the flush time is kept out of the speed figures (`speed_run_aside()`); a final
  flush when the guest powers off [verified: source]. Whether whole sectors reach the card before
  that depends on how picolibc splits `fwrite()` [unknown].
* ini paths: values are `strdup`'d by `parse_conf_ini()` before the shared `resolved` buffer is
  reused; `/mnt/sd` and `/mnt/win` mapped only on a whole path component; a missing WIN9X
  partition is a red line, not a wrong path; `mem_size = max` parses to 0 and means "the most
  that fits" [verified: `setup.c` 153 to 183, `pc.c` 961 to 990, `esp_main.c` 486 to 564].
* Speed and BENCH against the Linux route's `-marks` (`korvo/main.c` 594 to 672, 892 to 946):
  the same counter (`cpu->cycle`, one per instruction, `i386.c` 3988), the same run and halt
  split, the same mark rules including 0xAF, the same line formats and rounding, wrap-safe 32-bit
  differences [verified: source and `test_speed`]. Per-step cost is small beside 512
  instructions [inferred].

Memory for a 14M guest
* Struct sizes from gdb on both ELFs [verified]: VGAState 18,324 (the notes say 18,316, from
  before `shrunk_w`/`shrunk_h`; rounded to 32 it is 18,336 either way), NE2000State 49,224,
  SB16State 4,844, PCSpkState 1,820, FM_OPL 4,788 plus 9 x 200, IDEState 2,144, PCIBus 1,156,
  PCIDevice 380. The device window then holds about 85,600 to 92,300 of 131,072 bytes, as the
  core notes say [inferred]. The TLB is 256 x 16 = 4,096 bytes by `malloc()`, internal RAM
  (`i386.c` 534 to 538, 5277 to 5284) [verified]. The OPL tables are static on `BUILD_ESP32`, not
  in the window (`fmopl.c` 48 to 50) [verified].
* PSRAM: `mem_size` 14M with `vga_mem_size` 320K needs 14,672 KiB (each rounded to 4 KiB, plus
  16 KiB slack); 16,384 KiB less the frame buffer (750 KiB Korvo-1, 937.5 KiB DOS board) less
  the 256 KiB left outside the pool leaves about 15,378 and 15,190 KiB before small allocations
  [inferred]. `check_config()` counts exactly what is later taken from the pool [verified]. The
  16-colour cache (153,600 bytes at 640x480; 240,000 at 800x600 planar on the DOS board) comes
  from the 256 KiB outside the pool and falls back to the plain path if it does not fit
  (`vga.c` 1620 to 1628) [verified]. FatFs buffers prefer PSRAM
  (`CONFIG_FATFS_ALLOC_PREFER_EXTRAM`) [verified]. The real figures are the board's [unknown].
* Internal RAM: 264,572 bytes (Korvo-1) and 261,596 (DOS board) left after static use
  [verified: `checks/size-*.txt`]; the runtime figure is on the boot console [unknown].


### 4. What the host tests do not cover

* Threads: `test_kbd_input` and `ch9350_test` build `kbd_input.c` with `KBD_INPUT_HOST`, which
  removes its lock, and `i8042.c` without `BUILD_ESP32`; only `test_ps2_queue` runs two threads,
  and only for the queue-full case. Nothing tests the cross-core paths of F3.
* The mouse queue near full, and a backlog (F1: my test fails today); the ID 4 knock and five
  buttons; remote mode; resolution and sample rate.
* `vga.c` with the DOS board header (my `rev_vga_dos.c` fills this, all pass); 132-column text;
  a planar VBE mode in the scaled path; split screen in any mode; anything about the two cores'
  caches, which only the board can show.
* `run_pc()`: the 2 s flush, the floppy gap (F2), write errors (F4).
* The device-window total and the PSRAM pool: worked out from sizes, never run.
* `test_setup` only passes with an output folder under about 60 characters (F6).


### 5. Files

* `~/tiny386-idf-scratch/review-emulator-correctness/rev_mouse_test.c` and `mouse/` (F1)
* `~/tiny386-idf-scratch/review-emulator-correctness/rev_vga_dos.c` and `vgados/`
* `~/tiny386-idf-scratch/review-emulator-correctness/host-tests-short.log`, `host-tests.log`
  (the long-path run of F6), `host-tests-asan.log`
* `~/tiny386-idf-scratch/review-emulator-correctness/lr/` (the Linux route's patched sources),
  `lr-vs-v3-*.diff`
* `~/tiny386-idf-scratch/review-emulator-correctness/su/*.su` (stack usage), `sizes.gdb`
