# tiny386 v3: first boot on the Korvo-1

Written 2026-10-21 for Plat's first run of tiny386 bare metal on the ESP32-S31-Korvo-1, with Linux
kept beside it (FIRMWARE-PLAN.md 3.8). The images are v3 with two review rounds' fixes
(`REVIEW-RESOLUTION.md` lists every finding and what became of it) and, since 2026-10-22, the
status LED (v3.3, then v3.4 with that round's review fixes), v3.5 for the DOS board's rev A2c
(its pins, its speakers, a louder default sound: nothing changes on the Korvo-1 but the version the
app gives, `BUILD-NOTES.md` 16), v3.6 with the fixes from the review of v3.5 (the DOS board's
check and a guard in the sound mix; again nothing changes on the Korvo-1 but the version,
`BUILD-NOTES.md` 17), v3.7, which makes the status LED much dimmer, about an eighth of v3.6's
light, and its brightness a setting, `[esp] led_brightness` (`BUILD-NOTES.md` 18), and v3.8. v3.8
answers Plat's Windows 95 runs on v3.6 (an emulator abort, protection errors, a VMM blue screen, a
text-mode stall, no `BOOTLOG.TXT` [reported: Plat, 2026-10-21 and 22]): on the PC the cause was the
guest's timer, which after any long wait for the card fired every missed tick back to back until
Windows' stack ran out, and the long waits came from the card's FAT being walked on every seek;
both are fixed, a card error is got over instead of stopping the disk for good, and Windows 95
reached the desktop in every PC run (`BUILD-NOTES.md` 19, `research\WIN95-FIX.md`). v3.8 also
turns the status LED into a PC's front panel while the PC runs, a steady green with a blue flash for
each disk access, and makes the log quieter: the UART off while the PC runs unless the ini asks,
and lines that repeat counted rather than shown (`BUILD-NOTES.md` 20). v3.9 takes in the two
reviews of v3.8 (`REVIEW-RESOLUTION.md` round 5, `BUILD-NOTES.md` 21): the flash scripts after a write
cut short, the line that names an emulator abort's cause on the UART too, lines held back counted on
the crash screen, and the status LED's stall line in the log; the emulator itself behaves as v3.8's
in every PC run. Plat has written v3.6 to his Korvo-1 and run FreeDOS, BENCH and Windows 95 on it
[reported], and then v3.9, on which Windows 95 reached its desktop [reported: 2026-10-23], and v4.1,
on which Windows ran at 800x480 with the mouse pointer flickering [reported: 2026-10-23]. Nothing of
v4.2 or v4.3 has run on a board, so every "you should see" below is still what the code, the host
tests and the PC runs say. Each step says what to photograph or note; the photos and notes are what
the next round works from. Section 8, Windows 95, is the base of section 13, section 13 of section
14, and both of section 15.

**v4.0 (2026-10-23)** is v3.9 merged with tiny386's own latest work (its author's master of
2026-10-22: the timer's one-shot modes, task switch fixes; `research\UPSTREAM-MERGE-v4.md`) and the
answer to Plat's question of the same day: Windows 95 at the panel's own size, 800x480 on the
Korvo-1 (800x600 on the DOS board), in 256 colours, High Color (16 bit, the default) or True Color,
through a free display driver for this emulated VGA, VMDisp9x, which you install from its own floppy
image. The VGA BIOS is still SeaVGABIOS, with 800x480 modes added for DOS programs. What made it
worth doing on the board is the drawing: a VBE picture is now drawn again only where the guest
wrote, with no division a pixel, where v3.9 worked out every pixel of it every picture
(`BUILD-NOTES.md` 22). **Section 13 is the new step**, after Windows has reached its desktop as in
section 8. Everything else behaves as in v3.9: on the PC, Windows 95 with the standard VGA driver
and FreeDOS with BENCH give v4.0-base's results to the instruction, and v4.0-base gives v3.9's (the
new VGA BIOS alone moves the timings a little, as any change to the BIOS's own code would); and a
DOS program that puts the timer in a one-shot mode no longer stops the emulator [verified:
`BUILD-NOTES.md` 22.7, `research\UPSTREAM-MERGE-v4.md`].

**v4.1 (2026-10-23, the same day)** takes in the two reviews of v4.0 (`REVIEW-RESOLUTION.md` round 6,
`BUILD-NOTES.md` 23). For you: DOS programs in mode 13h no longer pay for the new drawing's notes at
all (v4.0 put a mark and a memory fence on every pixel they wrote, which nothing read);
`vbe_blocks = 0` now takes the notes away in Windows too, so it compares the two ways of drawing
fairly (13d); a guest that switches chain 4 under VBE is drawn right; a DOS board ini for Windows at
800x600 (13e); and what happens if the image with the driver in is started with `win95-14m.ini`
(13c). Windows, FreeDOS and BENCH do exactly what they did on v4.0 in every PC run. The app calls
itself `v4.1`; nothing of it has run on a board.

**v4.2 (2026-10-23, the same day)** answers your report on v4.1 at 800x480, "flickering a LOT": on
the PC the flicker is the display driver's own mouse pointer, which VMDisp9x's QEMU driver rubs out
and draws again around everything Windows draws; tiny386 shows the screen as the guest has it. One
byte of the driver hands the pointer to Windows' own DIB engine, and `qemumini-pointer\` makes that
change (**section 14 is the new step**, with a way that needs no card out). In tiny386 itself: a clear
of the screen no longer flashes black on the panel (14f), and Ctrl+Alt+F11 changes `vbe_blocks` at
once (14e). Windows, FreeDOS and BENCH do exactly what they did on v4.1 in every PC run. The app
calls itself `v4.2`; nothing of it has run on a board.

**v4.3 (2026-10-24)** is v4.2 with the information port that T386MON reads (`..\t386mon\`, the
monitor for Windows 95 you asked for: fps, how busy the guest is, drawing tests, a log): a program
in the PC can now read tiny386's own figures. **Section 15 is the new step**: T386MON's tests at
640x480 in 16 colours and at 800x480 in 16 bits, photographed. v4.2's flicker fix and Ctrl+Alt+F11
are as they were. Windows, FreeDOS and BENCH do exactly what they did on v4.2 in every PC run. The
app calls itself `v4.3`; nothing of it has run on a board.

**From v3.6 to v4.3** only the app changes: the dual-boot bootloader and partition
table are v3.7's byte for byte, and the tiny386-default table is the one added after v3.7 [verified:
`checks\reproducible-v4.3.txt`]. With the dual boot
already in, the one write is `flash-dualboot.bat COM10 app` (Linux powered off first, as always): it
checks `images\korvo1-dualboot\tiny386.bin` (its lines should say `App version: v4.3`), reads the
board, goes on only if a dual-boot table is at 0x8000 (either default) and, since v3.9, the
dual-boot bootloader at 0x2000 (`bootloader at 0x2000: the dual-boot one (b57469cd), with the VOL+
key`), and after Y writes that one file at 0xE20000 and nothing else, then resets the board; no
backup is taken, none being needed, since nothing of Linux is written, and the default at reset
stays as it is [verified: `flash-dualboot.bat`; `checks\bat-harness-v4.3.txt` cases 8, 9, 12, 55
and 92]. FLASH.txt 3 has the command by hand. From v3.6 or v3.7, copy the two inis from
`card\tiny386\` to the card as well (section 0): they list v3.8's settings, commented out; v3.9 and
v4.0 did not change them, and v4.1 only added a comment to `win95-14m.ini`. For section 13, v4.0 adds
three files to `card\tiny386\`: a new `vgabios.bin`, `win95-800x480.ini` and `vbe-reg.img` (13a);
v4.1 changed only comments in the two Windows inis, so copying them again is optional; v4.2 and
v4.3 changed no card file. For section 15, `..\t386mon\t386mon.img` goes in the card's tiny386
folder too (15a).

**tiny386 the default at reset (optional; added 2026-10-22, no new app).** Holding VOL+ for every
tiny386 boot gets tiresome while testing, so the dual boot now has a second partition table with
the two apps' roles swapped (`images\korvo1-dualboot-tiny386-default\`). With the dual boot in
(any app from v3.6 on) and Linux powered off: `flash-dualboot.bat COM10 table tiny386`.
It reads the board, goes on only if a dual-boot table is at 0x8000 and the dual-boot bootloader at
0x2000, and after Y writes the 3 KB table at 0x8000 and nothing else [verified:
`checks\bootswap\bat-harness-bootswap.txt` case 40; `checks\bootswap\flash-simulation-bootswap.txt`
case 11, where only 0x8000 to 0x8FFF change]. From then on RESET with nothing held starts tiny386,
and VOL+ held through RESET (two seconds, as before) starts Linux [inferred: ESP-IDF's bootloader
source, `dualboot\DUALBOOT.md` 13; not yet seen on the board]. In the steps below, read "RESET with
nothing held" and "RESET with VOL+ held" the other way round. `flash-dualboot.bat COM10 table linux`
turns it back, and `back-to-linux.bat COM10` works from either. FLASH.txt 2 has the commands by hand
and what a reset does on its own with each default.

**The status LED** is the Korvo-1's RGB LED on GPIO37. Each step below says what it should show
(`STATUS-LED.md` has the whole table). A phone video of the first fifteen seconds after RESET, with
the LED and the panel both in it, is the easiest record. Since v3.7 it is dim by default (red 9 of
255, where v3.6 sent 70); if it is now too dim or still too bright, put `led_brightness = N` under
`[esp]` in `tiny386.ini`, N from 1 to 255 (70 is v3.6's brightness, 0 turns the LED off, a crash
included), and note the number that suits (`STATUS-LED.md` 7). Since v3.8, while the PC runs, it is a
PC's front panel: a steady green (no dark blink any more) with a short blue flash each time the guest
reads or writes its disk, like a hard disk light (`STATUS-LED.md` 8).

**The UART** (v3.8): the boot console's lines still go to the UART as before, but once the PC starts
the log stays off it unless `uart_log = 1` is under `[esp]` in the ini; a crash report always goes
there (since v3.9 with the line that names an emulator abort's cause). Nothing in this plan needs
the UART.

Claude never opens a serial port and never flashes: every write and every log capture here is
Plat's.

Tags, as in the other notes: [verified] read in the named source or seen in a build or test made
for these notes; [inferred] follows from those; [reported] someone else's statement; [unknown]
only the board can say.


## Risks to your Linux install, plainly

What this first boot can and cannot do to the Linux on your Korvo-1 and its card:

1. **Power Linux off first.** Every script resets the board into download mode before it reads
   it. If Linux is running then, with the card's FAT partition p1 mounted for writing (be5b725's
   `S10sdcard` mounts it read and write at every boot [reported: the boot safety review, round 2,
   read from be5b725's card image]),
   whatever it had not yet written to p1 is cut off, and p1 can be left half written. So: in
   Linux, `poweroff`, and wait for it to finish, before step 1 and before any script. The scripts
   now keep the board in download mode from their first reset to the last write's reset
   (`esptool -a no-reset`), so Linux never starts in between; if you answer N, or a script
   stops after reading the board, press RESET to start Linux again [verified: the stub's call
   log, `checks\bat-harness-v3.9.txt`].
2. **Step 1 first copies the flash.** Before its first write over Linux, `flash-dualboot.bat`
   reads the whole flash (16 MB, a few minutes) into `backup\korvo1-before.bin`, and 0x2000 to
   0x8FFF (Linux's bootloader and partition table) into `backup\korvo1-2000-8fff-before.bin`,
   each with its SHA-256 and the board's MAC beside it. If that read fails, nothing is written.
   It never overwrites a backup of another board or of another state of this one; it stops and
   says so. Since v3.9 it also never takes a backup of a write cut short (the next item), and keeps
   the one taken before it. **Keep the `backup\` folder** (a copy somewhere else is better): it is
   the exact way back [verified: harness cases 1 to 6, 18, 19, 84 to 101].
3. **Step 1 then rewrites two pieces of Linux's flash**: the bootloader (0x2000 to 0x7FFF) and the
   partition table (0x8000 to 0x8FFF). If that write is cut off (a cable pulled, power lost), Linux
   may not start until they are written again. Download mode is in the chip's ROM and always
   there, so this is fixed by running step 1 again or `back-to-linux.bat COM10` [verified: no image
   here burns an eFuse, `REVIEW-boot-safety-flash.md`]. Since v3.9 running step 1 again does
   finish it: the script finds the start of the dual-boot bootloader in front of Linux's table,
   keeps the backup it took before, and writes the set; v3.8's stopped there and advised renaming
   the backup, which a review of v3.8 found would end in a backup of the broken state [verified:
   harness cases 84, 85, 89; `dualboot\DUALBOOT.md` 14]. tiny386 itself is written first, in an
   esptool run of its own, at 0xE20000, outside Linux, so a failure there leaves Linux as it was.
4. **The new bootloader is be5b725's Linux bootloader plus the VOL+ check**, nothing else [verified:
   `REVIEW-boot-safety-flash.md` round 1, 3.1]. The script names the bootloader your flash has now
   by SHA-256: be5b725's, the fork release v2026.09.20's, or neither. With be5b725's, nothing
   changes but the key. With the release's, the dual-boot bootloader should still start the
   release's loader (same memory layout) [inferred: the boot safety review; not tried]. With
   neither, the backup is the only exact way back. With no key held it starts Linux as before
   [inferred; not yet seen on the board].
5. **Nothing from 0x9000 to 0xE1FFFF is written**: Linux's nvs, otadata, phy_init, loader, OpenSBI,
   kernel and initramfs stay byte for byte [verified: flash simulations with the v3.9 images,
   `checks\flash-simulation-v3.9.txt`, and the boot safety review's own].
6. **The way back is exact.** `back-to-linux.bat COM10` puts 0x2000 to 0x8FFF back from the backup:
   exactly what was there, whichever Linux it was. Without a backup it writes be5b725's own
   bootloader and table (`small be5b725`), which is exact only if your flash held be5b725's
   `flash.bin`. `back-to-linux.bat COM10 whole` writes the whole 16 MB backup back [verified: the
   scripts' flow, `checks\bat-harness-v3.9.txt` cases 26 to 38].
7. **The card is shared, and that is the real risk.** tiny386 mounts every FAT partition and
   writes to two of them: p1 (`KORVO_SD`, FAT16 or FAT32: `lastboot.txt` after a boot menu choice,
   and whatever the guest writes inside `freedos-hd.img`) and p3 (`WIN9X`: writes inside
   `win95.img`). It never mounts or writes p2 (Linux's ext4 root), and never formats or
   repartitions [verified: `storage.c`, the boot safety review, `tests/host/storage` on FAT16 and
   FAT32 p1]. If you press RESET while Linux still has FAT changes in its cache, p1's FAT can be
   left half written, and tiny386 writing to p1 afterwards could then cross-link files there
   [inferred]. So again: `poweroff` in Linux before you hold VOL+ and press RESET.
8. **The guest cannot make an image grow any more.** In v3.1 a guest that read or wrote past the
   end of its disk image made the image file grow on the card, up to filling the partition (p1
   is Linux's too) [verified: `tests/host/disk` against v3.1's `ide.c`]; a floppy image the same
   [inferred: the boot safety review, round 2]. Since v3.2 such a sector is refused, as a disk with
   no such sector would [verified: `tests/host/disk` through ESP-IDF's own FatFs, on FAT16 and
   FAT32; `test_floppy`]. One image named for two drives is refused too [verified: build only].
9. **The guest's own writes** reach the card two seconds after the guest last wrote, or at once
   when the guest asks (ATA FLUSH CACHE, which Windows 98 and later send at shutdown; Windows 95
   does not [inferred]) [verified: `esp_main.c` `run_pc()`, `tests/host/disk`]. A RESET sooner
   than that loses those writes inside the disk image. Shut the guest down, or wait five seconds
   after its disk activity, before RESET.
10. **Every script reads the board before writing**, which writes nothing: it prints the MAC and
    reads the partition table at 0x8000. If that table is not Linux's (or, as each script needs,
    the dual-boot one or tiny386's own), it stops [verified: harness cases 10 to 12, 22, 30, 37]. Once
    you put the Korvo-1's MAC in `flash-paths.cmd` (`KORVO1_MAC`), the three Korvo-1 scripts also
    stop on any other board, the DOS board among them [verified: harness cases 16, 17].
11. **`idf.py flash` from the dual-boot build** would have written tiny386's own bootloader, the
    otadata and tiny386 over the Linux loader. The build refuses every flashing target before
    esptool runs, `dfu-flash` and the otadata targets included since v3.2 [verified: the build
    graph, `checks\cmake-flash-refusal-v3.9.txt`]. Use the scripts.
12. **The standalone set** (not part of this first boot) replaces the Linux loader; it takes the
    same backup first, and the way back is `back-to-linux.bat COM10 whole` (or `full be5b725`).
    With no card in, it (and the DOS board image) writes its wear levelling records at the end of
    the flash's `storage` partition, which is no Linux partition [verified: round 1 m2].
13. **With tiny386 the default** (optional, above and FLASH.txt 2), Linux is one key away rather
    than the default: anything that resets the chip from Linux (`reboot`, most likely, or the Linux
    loader's own restart when it cannot load its files) comes back up in tiny386 unless VOL+ is
    held, so `poweroff` before RESET matters as much as ever [inferred: the loader's `main.c`
    lines 400 to 411, `dualboot\DUALBOOT.md` 13]. The switch writes only the 3 KB partition table
    at 0x8000. A write cut inside it leaves no valid table and the board resets over and over until
    either dual-boot table is written by hand (FLASH.txt, "If something goes wrong"); a
    `back-to-linux.bat` stopped between its bootloader and its table leaves tiny386 starting with or
    without the key until the script is run again, which it can be [verified: the cut-off cases C1
    to C5 in `checks\bootswap\flash-simulation-bootswap.txt`; harness case 66].
14. **Not known until the board runs it**: everything in sections 3 to 11 below [unknown].


## 0. Before you start

* esptool 5.4 or later on Windows (`esptool version`).
* Which Linux is in the flash: be5b725 or the fork release v2026.09.20 (same partition table,
  byte for byte [verified: `dualboot\DUALBOOT.md` section 4]). Step 1 says which bootloader it
  finds. Upstream v0.4.0 or later is not for this set; the script would see it and stop.
* Room for the backup: 16 MB beside the scripts, in `v3\backup\`.
* The key: VOL+ on the schematic, the one that pulls GPIO42 to 0.38 V; esp-board-manager calls
  it SET [reported: `research\firmware-design.md` 6.3]. Find it on the silkscreen first.
* A USB keyboard, ideally a plain wired one for the first round; a hub and a wireless receiver
  are for later steps. A USB mouse for the Windows 95 round.
* A camera, and a clock or stopwatch.

### The card

Your card as it is [reported: your card, `G:\`]: p1 `KORVO_SD` with `tiny386\` (bios.bin,
vgabios.bin, freedos-hd.img, freedos.ini), p2 ext4, and p3, the new FAT32 WIN9X partition from
sector 532480 with `win95.img` on it. p1 is FAT16 in be5b725's card image (4 sectors a cluster,
label KORVO_SD, in a type 0x0C partition) [verified: its sector 8192, read from
`main-be5b725\sdcard.img`, as the boot safety review found]; FAT16 and FAT32 both work
[verified: `tests/host/storage`].

Copy into `G:\tiny386\`, beside what is there:

| From `v3\card\tiny386\` | What it is |
|-|-|
| `tiny386.ini` | FreeDOS from freedos-hd.img with 4 MB, enough for BENCH. The boot menu's default |
| `win95-14m.ini` | Windows 95 from `/sdcard3/win95.img` (the WIN9X partition) with 14 MB. Both inis list v3.8's settings under `[esp]` with a `;` in front, each at what it is without its line; section 8 says when to take one off |
| `bios.bin` | the Linux route's own [verified: SHA-256 against `..\tiny386\SHA256SUMS`]; the one already on the card came from there too, so copying it again changes nothing |
| `vgabios.bin` | since v4.0 this package's own: SeaVGABIOS with the five 800x480 modes added (`bios\README.txt`), 40,960 bytes, where the Linux route's is 40,448 [verified: both hashed]. It replaces the Linux route's on the card; v3.9's (the Linux route's) still works with v4.1, without the 800x480 modes |
| `win95-800x480.ini`, `vbe-reg.img` | for section 13 (Windows at 800x480); 13a says when |

Leave the Linux route's `freedos.ini` where it is: the menu lists it too, and it still works
under this firmware (2 MB) [verified: host check `cardcheck`, BUILD-NOTES.md 5.4]. `win95.img`
goes in the top folder of the WIN9X partition. Eject the card properly in Windows.

With more than one ini in the folder the boot menu comes up: with `freedos.ini`, `tiny386.ini` and
`win95-14m.ini` it lists those three, and with `win95-800x480.ini` copied too, four, with
`tiny386.ini` the default [verified: `cardcheck` with the firmware's own `setup.c`,
`checks\card-check-v4.1.txt`]. After the first choice made with a key, a small `lastboot.txt` appears beside them:
the firmware remembers the last choice there. It is the only file the firmware itself writes
[verified: `NOTES-core2.md` 9].


## 1. Flash (a copy of the flash, tiny386, then two small writes; Linux stays)

In Linux: `poweroff`, and wait for it to finish (risk 1). Then, from `v3\`:

```
flash-dualboot.bat COM10
```

It checks the files, then reads the board (nothing written): you should see the board's `MAC:`
line, `partition table at 0x8000: linux`, and `bootloader at 0x2000:` with the bootloader it
found. It prints what it will do and waits for Y. After Y: first the backup (`Reading the whole
flash into ...backup\korvo1-before.bin`, a few minutes, then `kept: ...`), then tiny386 at
0xE20000, then the dual-boot bootloader and partition table (0x2000 to 0x8FFF). Nothing of
Linux [verified: flash simulation, `checks\flash-simulation-v3.9.txt`; the script's flow,
`checks\bat-harness-v3.9.txt` case 1].

Note: the MAC line, and the `bootloader at 0x2000` line; if anything goes wrong, esptool's last
lines (copy them, or a photo). Then, if you like, put the MAC in `flash-paths.cmd` as
`KORVO1_MAC` (the scripts then refuse any other board), and copy `v3\backup\` somewhere safe.


## 2. Linux, as before

Press RESET with nothing held.

You should see: Linux boots exactly as it did [inferred: the bootloader is the Linux build's with
the test-pin settings added, and boots the factory partition when GPIO42 is not held low;
`dualboot\DUALBOOT.md` 3].

The status LED: whatever Linux does with it. After a tiny386 run (steps 9 and later) it may keep
tiny386's last colour under Linux, since a WS2812 holds its colour until it is sent another or loses
power, and a RESET does not take its 5 V away [inferred]. Not a fault.

Note: yes or no. If no: `back-to-linux.bat COM10` puts the old bootloader and table back from the
backup (FLASH.txt 4), and tell me what Linux did.

Then, in Linux, `poweroff` and wait, before the next step (risks 1 and 7).


## 3. tiny386: the boot console

Hold VOL+, press and release RESET, keep holding VOL+ for two seconds, let go.

You should see, within a second or two: text on the panel, light grey on black, a blue title row at
the top (` tiny386 on the ESP32-S31-Korvo-1   v4.1   ESP-IDF v6.1-dev-8142-g188e3e55bb`), and a
strip of colour along the bottom edge. The lines, roughly in this order [verified: the messages
in `esp_main.c`; the values are the board's to fill in]:

* `tiny386 for the ESP32-S31-Korvo-1, version v4.1, ESP-IDF ..., ELF ...`
* `Reset by ...` (which reason the RESET key gives is [unknown])
* `Chip: esp32s31 revision v..., 2 cores, CPU 320 MHz; PSRAM ... KiB.`
* `Internal RAM free ... KiB (largest block ... KiB); PSRAM free ... KiB.`
* `Status LED on GPIO37: colours and blinks as in STATUS-LED.md, brightness 9 of 255 ([esp]
  led_brightness).` (and, after a crash or a watchdog reset, a second `Status LED:` line saying what
  it replays first). The 9 is the default; the ini's own `led_brightness` line, if it has one, comes
  later with its settings
* `Card mounted at /sdcard.` then one line for each partition: p1 at `/sdcard`, p2 Linux
  "not FAT: not used", p3 FAT32 "WIN9X" at `/sdcard3`; then `In an ini, /mnt/sd/... means ...`
* `Emulator memory: ... KiB, room for mem_size up to ...M with vga_mem_size 256K.` (14M expected
  [inferred: `NOTES-core2.md` 3.4])
* `USB host started (full and low speed, hubs allowed) ...`, and later a line for each keyboard
  or mouse as it is found
* `3 setups in /sdcard/tiny386; choose one ...`, the three inis with their first comment lines

Photograph: the whole console, sharp enough to read, before the menu's 6 s run out (any key but
Shift, Ctrl or Alt stops the countdown, so press an arrow key once to hold it).

The status LED through this step [verified: `esp_main.c`, `status_code.c`, host test
`test_status`; the colours as the LED shows them are [unknown]]: white at once (tiny386 started),
blue a moment later (the console is up), cyan when the card mounts, magenta when the inis are found;
it stays magenta while the menu waits. Note each colour you see and roughly when. If it stops on one
of them, or blinks instead, that is the finding: `STATUS-LED.md` says what each means. Red 1 (one red
blink, then a pause, over and over) with the card in means it is not being read. All of this is at the
default brightness, 9, since the ini is read only after the menu; a `led_brightness` line takes effect
once the PC's ini is read (`STATUS-LED.md` 7). Note whether each colour is easy to tell at the default,
yellow from red above all (yellow is 8:5 red to green, red 9:0).

The colour strip: on the left half eight bars, black, blue, green, cyan, red, magenta, yellow,
white; on the right half sixteen greys from black to white [verified: `console.c` `draw_card()`].
Photograph it close. If the second bar is red and the fifth blue, red and blue are swapped.

If a line is yellow or red, the console stays 10 s before the PC starts; photograph those lines.
A device's interfaces that are not a keyboard or mouse are closed properly, and a device
unplugged while it is being set up is let go cleanly (`USB: interface N of ... was unplugged while
it was being set up.`) [verified: host test `test_usb_input`; how many real devices need either is
[unknown]].

If nothing appears on the panel at all: section 11.


## 4. The boot menu

The title row reads like ` tiny386:  > 2 tiny386.ini   starts in 6 s.  Up, Down, 1 to 3, Enter.`
[verified: `bootmenu.c`].

* Up and Down move (both wrap round), a digit 1 to 3 starts that one at once, Enter starts the
  one shown; Esc only stops the countdown [verified: host test `test_bootmenu`].
* With no key, `tiny386.ini` starts after 6 s.

Note: did the keyboard work in the menu? If the countdown ran out before the keyboard's line came
up, note roughly how long after reset the keyboard's `USB:` line appeared [unknown: enumeration
time].

For this step let `tiny386.ini` start (or press 2).


## 5. FreeDOS

After the menu: the settings lines (`Settings from /sdcard/tiny386/tiny386.ini:` and each line of
it, with the file each name became), `hda: /sdcard/tiny386/freedos-hd.img, 16384 KiB`,
`PC built. ... Internal RAM free ... PSRAM free ... KiB (largest block ... KiB).`, `The PC starts
in 2 seconds.`, then the PC's picture takes the panel.

The status LED: steady green during `The PC starts in 2 seconds.` (the BIOS is loaded), then still a
steady green from the moment the PC's picture takes the panel, for as long as the emulator runs, with
a short blue flash each time the guest reads or writes its disk: a flicker of blue while SeaBIOS and
FreeDOS load, then green at the prompt [verified: `test_status`; how it looks is [unknown]]. v3.7
and earlier went dark for 0.1 s every 2 s instead; that blink is gone. A red blink pattern instead
means the settings stopped it: red 3 a BIOS file, red 4 something else in the ini (the screen lists
it).

Note: does the LED flicker blue while FreeDOS loads, and when you type `dir`? Are the flashes easy to
see at the default brightness (each is 20 ms or more)?

You should see: the SeaBIOS screen, then FreeDOS starting, at 80x25 text in the middle of the
panel (720x400 at x 40, y 40), then `C:\>` [inferred: `vga.c` placement, host test `test_vga`].

Note: seconds from RESET to the `C:\>` prompt (a stopwatch is fine); the `PC built` line's
figures (photo of the console in step 3 covers it).
Photograph: the SeaBIOS screen if you catch it; the prompt.

Then type:

| Type | Expect | Note |
|-|-|-|
| `ver` | FreeDOS's version | |
| `mem` | about 4 MB of memory in all | photo |
| `dir` | the root of the image | |
| hold a letter key two seconds | one letter, then after half a second the letter repeating, about 11 a second [verified: `test_kbd_input`, PS/2 default 500 ms and 10.9 a second] | does it repeat? does it stop at once when let go? |
| Caps Lock, then a few letters, then Caps Lock again | capitals while it is on | does the keyboard's Caps Lock light follow? (boot-protocol keyboards only [verified: `NOTES-core2.md` 2.1]) |

Note also which keyboard: its make, wired or wireless, through a hub or not, and its `USB:` line
on the console (photo from step 3).


## 6. Pictures

On the image, in `C:\DOS` [verified: `..\tiny386\tools\make-freedos-image.sh` lines 136 to 146]:

| Type | You should see [inferred: `vga.c` placement; the pictures as `tests\guest\*.S` describe them] | Photograph |
|-|-|-|
| `vga13` | a 320x200 picture in 256 colours, doubled to 640x400, at x 80, y 40 | yes |
| `vga12` | sixteen bands of colour, 640x480, the full height of the panel, at x 80; a white box steps along the top band | yes |
| `vga12t` | the same with forty characters printed on one band | yes |

A key ends each one and puts the text screen back [verified: the comments at the top of
`..\tiny386\tests\guest\vga12.S` and `vga13.S`]. Note any tearing, flicker, black flashes, or a
picture that is shifted or cut. If `vga12` seems slow to draw, look for `vga: no room in PSRAM for
... 16-colour picture cache` on the UART (7b).


## 7. Speed: Ctrl+Alt+F12 and BENCH

At `C:\>`, press Ctrl+Alt+F12.

You should see: the boot console over the picture, with the title row reading
` 0.xxx million instructions a second, halted nn %, nn pictures a second   Ctrl+Alt+F12: back`,
renewed every second [verified: `speed.c`]. At the idle prompt the halted share may be high, if
FreeDOS waits for keys in HLT [unknown]. Photograph it, then Ctrl+Alt+F12 again: the DOS screen
comes back whole.

Then:

```
bench
```

BENCH times nine pieces of work. When it ends the console comes up over the picture by itself
with nine lines in the Linux route's own format and two totals [verified: `speed.c` and host test
`test_speed`, compared character for character with the Linux route's `-marks` lines]:

```
tiny386: a1 16-bit arithmetic in registers ... thousand a second
...
tiny386: N instructions in X s of running in BENCH (without a4): Y million a second
tiny386: N instructions in X s of running since the PC started: Y million a second
```

Photograph all of it. Under Linux on this board the same BENCH ran at 0.358 million instructions
a second [reported: Plat, 2026-10-21]; this is the number to beat. Ctrl+Alt+F12 goes back. Since
v3.2 only a run that starts with BENCH's first mark counts, so another program that happens to
write 0000:04F0 no longer brings this screen up [verified: `test_speed`].

The status LED keeps its steady green through BENCH and the speed screen (the emulator runs under
both; BENCH hardly touches the disk, so little blue). Yellow 5 (five yellow blinks, a pause, over and
over) would mean the emulator itself stopped for 10 s or more; note when, and take the UART log if
you can (7b).

The speed screen since v3.8 also has a `Disk:` line, a `Timer:` line and a `Log:` line (section 8
says what they mean); photograph them with the rest.

The Korvo-1 has no USER key for this (GPIO42 is the ADC ladder); the DOS board will have one.

### 7b. If you can: one minute of the UART

Only if it is easy (the panel shows all a first boot needs). Since v3.8 the log stays off the UART
once the PC runs, unless the ini asks: put `uart_log = 1` under `[esp]` in the ini for this (take
it out again afterwards). With it, the lines go through a queue that the emulator never waits for;
if the UART cannot keep up, some are left out there and a line says how many (the panel and the
crash screen still have them) [verified: `console.c`, test 2]. Then COM10 at 115200 8N1, logging to
a file, while BENCH runs and for a minute after. Every 10 s these lines go there and nowhere else
[verified: `display.c`, `speed.c`]:

* `display: N pictures in 10 s, ... write-backs ..., X us each on average, Y us the longest`
* `stacks, least free since start (bytes): i386_main ... vga_task ... usb_events ...`: how
  close each task has come to the end of its stack. Nothing has measured this yet [unknown]; a
  few hundred bytes or less for any task is worth telling me.
* `speed: ... million instructions a second ...`
* `Disk: ...` and `Timer: ...`, as on the speed screen (section 8)

Look also for two lines that should not be there:

* `LCD underrun`: the panel's DMA ran short of PSRAM bandwidth. ESP-IDF prints it from an
  interrupt for each underrun, so if it appears it may come many times a second and fill the
  log ring that the crash screen and the next boot show [verified: ESP-IDF's RGB driver;
  whether it happens is [unknown]]. Note how often.
* `task_wdt`: the task watchdog. It prints its report on the UART and carries on; it does not
  bring up the crash screen (`CONFIG_ESP_TASK_WDT_PANIC` is not set) [verified: sdkconfig,
  ESP-IDF's `task_wdt.c`]. A panel that freezes with no message may have this on the UART.

Whether opening the port resets the board depends on the terminal's DTR and RTS settings
[unknown]: if it resets when you connect, start the log first, then press RESET with VOL+ as in
step 3.


## 8. Windows 95

This is the step v3.8 is for, and v3.9 changes nothing in it. On v3.6 every run from a fresh copy
of the image failed before the desktop, at 4M, 13M and 14M, and a logged boot left no `BOOTLOG.TXT`
[reported: Plat, 2026-10-22]. On the PC, with the same image and models of the card's waits, v3.6's
core failed the same ways, and v3.8 reached the desktop in every run, 51 of 51 [verified:
`BUILD-NOTES.md` 19.7]; v3.9 reached it in all 11 of its runs, at the same virtual second as v3.8
where the run was the same [verified: `BUILD-NOTES.md` 21]. What changed for Windows [verified:
`BUILD-NOTES.md` 19]:

* the guest's timer no longer fires every tick it missed while the emulator waited, back to back,
  which is what ran Windows' ring 0 stack out (the abort on core 1, the protection errors, the VMM
  blue screen, the text-mode stall); missed ticks are dropped, as a real PC drops them;
* the image's place on the card is mapped once at start, so no access reads the card's FAT any more
  (v3.6 walked the FAT on every seek back, which on a fragmented image could take seconds);
* a card error is got over: the image is opened again and the access tried once more, where v3.6
  refused every later read and write of the image until RESET (most likely the missing
  `BOOTLOG.TXT` [inferred]);
* the speed screen (Ctrl+Alt+F12) and the crash screen now say what the card and the timer did.

Whether the board now reaches the desktop is [unknown] until you try it.

One more thing seen on the PC since [verified: the emulator review of v3.8, its run
`c37-bootlog-boardfrag-14M`]: v3.7, on a model of a fragmented card, with a logged start and no card
pulled, stopped on a text screen reading `While initializing device VFBACKUP:`. That is the message
Plat saw on the 22nd and put down to pulling the card; it is most likely the same failure as the
others [inferred]. If it comes up on v3.9, treat it as a failure (8c), not as a pulled card.

Once the display driver of section 13 is in an image, start that image with `win95-800x480.ini`,
not `win95-14m.ini` (13c says why); for 8e and 8f on it, make the same changes in
`win95-800x480.ini`. A fresh copy of the pristine image (8a) is still for `win95-14m.ini`.

### 8a. The card, freshly

The image on the card may have been damaged by the failed runs, and copies made onto a partition
that was repaired with fsck may be in many pieces. So, on the PC:

1. Format the card's WIN9X partition as FAT32 (Windows' own format is fine; any cluster size).
2. Copy your pristine `win95.img` onto it (the one in `..\tiny386\win95\`, which stays as it is).
   Copied onto an empty partition it should be in one piece.
3. Copy `card\tiny386\win95-14m.ini` and `tiny386.ini` (v3.8's, with the new settings commented out)
   into the card's `tiny386` folder.
4. Note the partition's allocation unit: `chkdsk X:` with the WIN9X partition's letter, without
   `/f`, only reads, and prints "bytes in each allocation unit". Note also, if you remember, which
   card and which partition the v2 run on the 21st used (v2 could only reach the first FAT
   partition, so it was not this one [verified: v2's `storage.c`]).

Use a fresh copy like this for every try below that follows a failure: Windows writes to its disk
from the first second, and a run that fails can leave it half written.

### 8b. The first try: 14 MB, a normal start

Press RESET with VOL+ held as in step 3 (or with nothing held if tiny386 is the default); in the
menu press 3 (or Down to `win95-14m.ini`, Enter).

On the boot console, before the PC starts [verified: `esp_main.c`, `disk_image.c`]:

* `[pc] hda = /sdcard3/win95.img`, `hda: /sdcard3/win95.img, 409248 KiB`, `[pc] mem_size = 14M`,
  and no yellow line about memory;
* `/sdcard3/win95.img: 1 piece on the card, mapped in N ms: a seek reads no FAT.` A yellow line
  `... is in N pieces on the card ...` instead means the copy is fragmented (over 64 pieces); it
  still works, it only starts more slowly. **Photograph this line.**
* `While the PC runs the log stays off the UART ...` and `A line the emulator repeats is shown 5
  times in 5 s, then counted ...`: v3.8's quieter log.

Then Windows starts. The status LED: a steady green with a great deal of blue flicker while Windows
loads (each flash is a disk access), less once it settles. The first start of this image runs
Windows' first-run setup (time zone and the like); go through it with the mouse and keyboard.

Note: the time from RESET to the desktop; whether first-run setup came and finished. At the desktop:

* Ctrl+Alt+F12 and **photograph the console**: the `Speed:` line, and the three lines under it
  [verified: `speed.c`]:
  * `Disk: R reads, W writes, longest L ms, N over 55 ms; card errors E (G got over), F refused`:
    how long the card made the emulator wait at worst, how often over 55 ms (a timer tick), and
    whether the card ever refused an access (E), how many of those a second try got over (G), and
    how many the guest was told of (F). With the image in one piece L should be a few ms and E 0
    [inferred: the PC runs];
  * `Timer: at most M ticks owed at once, D dropped (timer_catchup = 1)`: the most timer ticks that
    piled up during one wait, and how many were dropped since the PC started. Before v3.8 every one
    of them would have been fired back to back;
  * `Log: H repeated lines held back, D dropped from the UART's queue`.
* Ctrl+Alt+F12 again: the desktop comes back.
* Then the mouse:

| Check | Note |
|-|-|
| pointer moves the way the mouse moves (up is up) | yes or no |
| left click selects, right click opens a menu | yes or no |
| fast circles for ten seconds, then a click: the pointer keeps up, and no click or menu comes from nowhere | yes or no |
| a click while the disk light is busy (Windows loading something): it still counts | yes or no |
| the wheel, if Windows does anything with it | |

The mouse goes to the guest at the rate the guest's driver asks for (100 packets a second until
it asks), whole packets only, and a click is never merged into the movement before it
[verified: host test `test_ps2_mouse`]. A click made while the emulator is held up (a long card
access, say) still reaches the guest, press and release in order; what is cut short then is
movement, past 255 counts [verified: the same test, with the emulator held up].

Shut Windows 95 down from its Start menu and wait for "It is now safe to turn off your computer"
before pressing RESET. The status LED stays green through that screen: it leaves the emulated PC
running [inferred: Windows 95 on a PC without APM power-off stops there]. White blinking slowly (1 s
on, 1 s off) would mean the guest switched the PC off itself; the console then says `The PC was
switched off. Press RESET to start again.`

If Windows reached the desktop, start it once more from the same image (it should be quicker, with
no first-run setup), then try 4 MB: `mem_size = 4M` and `vga_mem_size = 256K` in `win95-14m.ini`, the
setting the v2 run used, from a fresh copy.

### 8c. If it fails: what to photograph

Leave the board as it is until you have photographed it; nothing here needs a RESET first.

* **A red crash screen** (` tiny386 has stopped: a crash on core ...`): every line, as in section 10.
  When the emulator itself stopped (an `abort()` on core 1, as on the 22nd), its last lines now
  start `tiny386 v4.1 stopped the PC.` and carry the `Disk:` and `Timer:` lines, then the line that
  names the cause (`shutdown: ...` or `@ ... i386.c ...`); those are the important ones.
* **A blue screen, a Windows Protection Error, or text that stays** (the SeaBIOS lines and
  `Starting Windows 95...` with a blinking cursor): photograph it, then press Ctrl+Alt+F12 and
  photograph the console: the `Disk:`, `Timer:` and `Log:` lines and the log lines above them (the
  emulator's own complaints, such as `read_desc:` or `double fault:`, are there, a few of each kind
  and then a count). Then RESET.
* **Windows still loading after minutes** (the disk light busy, nothing new on the screen):
  Ctrl+Alt+F12 and photograph, wait two more minutes, photograph again. The two `Disk:` lines show
  whether the card is the bottleneck.
* **The status LED**: yellow 5 (five yellow blinks and a pause) means the emulator itself stopped for
  10 s or more (its own loop stuck, or one card access that long); note it. Since v3.9 the log on
  Ctrl+Alt+F12 then has `status LED: the emulator has made no progress for 10 s (yellow 5)`, and
  `... goes on again` if it did.

Then use a fresh copy of the image (8a) before the next try.

### 8d. Fallbacks: safe mode and a logged start

Both come from Windows' own Startup Menu: right after the SeaBIOS lines, when `Starting Windows
95...` appears, press F8 (a few times is fine). The keyboard reaches the guest as any key does
[verified: `kbd_input.c`; that Windows sees F8 in time is [unknown]]. The menu lists Normal, Logged
(`\BOOTLOG.TXT`), Safe mode, Step-by-step confirmation, Command prompt only and others.

* **Safe mode**: Windows with its plainest drivers, 640x480 in 16 colours. If a normal start fails and
  safe mode reaches the desktop, the fault is in something only a normal start loads; note which.
* **Logged**: a normal start that writes `C:\BOOTLOG.TXT` line by line as it goes. After a failure,
  start again in safe mode or "Command prompt only" and look at the end of it (`type C:\BOOTLOG.TXT |
  more`, or Notepad in safe mode), or copy `win95.img` off the card and open it on the PC (7-Zip
  opens such disk images). Photograph or copy the last 20 lines; a `LoadFail` or the last
  `Loading Device` line names what it was doing. On v3.6 the file was not there at all; on v3.8
  and v3.9 it should be (the PC runs' logged starts wrote it [verified: `BUILD-NOTES.md` 21]) [inferred: the card's writes and their errors, `BUILD-NOTES.md` 19.4]; if it is missing
  again, the `Disk:` line's error counts say whether the card refused the writes.
* **Step-by-step confirmation** asks before each driver; useful only if the logged start points at
  one.

### 8e. After a desktop in 8b: the old timer, once (needed)

On the PC the failures are tied to the timer beyond doubt: with missed ticks made up back to back
Windows fails, with them dropped it reaches the desktop. On the board that is not shown yet: a
desktop in 8b, on a freshly formatted card, could come from the fresh format, the cluster map or the
quieter UART as much as from the timer [inferred: the emulator review of v3.8, its finding F1]. So,
if 8b reached the desktop, this one run is the comparison:

1. A fresh copy of `win95.img` onto the same partition (8a step 2; no new format).
2. In `win95-14m.ini` under `[esp]`, take the `;` off `timer_catchup` and make it `timer_catchup = 0`:
   v3.7's timer, every missed tick made up.
3. Start Windows 95 as in 8b. At the desktop, or at whatever stops it, Ctrl+Alt+F12 and
   **photograph the `Disk:` and `Timer:` lines** (the `Timer:` line then ends `(timer_catchup = 0)`).
   You have the same two lines from 8b with the default; keep both photos side by side.
4. Put the `;` back in front of `timer_catchup`.

What it says [inferred: the PC runs, `research\WIN95-FIX.md` 4]: the old failures back with 0 and
not with the default tie them to the timer on the board. A desktop both times, with `at most 1 ticks
owed` on the 8b photo, means this card never held the emulator up long enough to matter: the map
and the fresh format were enough here, and the timer's fix is still needed for a slower or more
fragmented card. A desktop both times with a large `at most M ticks owed` would mean the guest got
over the bursts this time; the photos are then worth sending as they are.

### 8f. Once, each, if there is time (each from a fresh copy)

* `memtest = 3` under `[esp]` in the ini: before the PC starts, three passes of a PSRAM test over the
  emulator's memory, about a second each, with the panel running. The console says `PSRAM test: 3
  passes over N KiB, every word right` in green, or in red how many words were wrong and where.
  **Photograph the line**, then put the `;` back. A red line would explain random crashes and
  damaged images that no software fix touches.
* `uart_log = 1` (and, only if asked, `verbose_log = 1`) if you take a UART log (7b).


## 9. Back to Linux

Shut the guest down first (the "safe to turn off" paragraph of 8b), then press RESET with nothing held (with
tiny386 the default: hold VOL+ through RESET instead).

You should see: Linux, as in step 2. Note: yes or no.

The dual boot stays installed; nothing else to do. To take it out altogether:
`back-to-linux.bat COM10`, which puts 0x2000 to 0x8FFF back from the backup (FLASH.txt 4).


## 10. A red crash screen

If at any point a red report appears, beginning ` tiny386 has stopped: a crash on core ...`,
then `What happened:` and `Where:` lines: it stays until RESET [verified: `console.c`
`console_draw_crash()`, `crash.c`, `CONFIG_ESP_SYSTEM_PANIC_PRINT_HALT`; that it stays on this
panel is [unknown]].

Photograph all of it, every line, sharp: the addresses on it are turned into function names
with `images\korvo1-dualboot\tiny386.elf`. Note what you were doing. Since v3.8, when it was the
emulator that stopped (`abort() was called` on core 1), the last lines of the log on it begin
`tiny386 v4.1 stopped the PC.`, then the `Disk:` and `Timer:` lines (section 8b says what they
mean), then (since v3.9 in this order, and on the UART too) the line that names the cause,
`shutdown: ...` or `@ ... i386.c ...`, then `abort:` and the guest's registers; and every line the
emulator logged in its last moments is there, the counts of lines held back said first [verified:
`esp_main.c` `abort_note()`, `i386.c` `cpu_abort_at()`, `console.c`; host test 20]. After any other
crash, since v3.9, the lines still held back are said there too, as `(not shown: N more lines like
"..." before the crash)` [verified: `console.c`, host test 2].

The status LED: red and white by turns, four changes a second, for as long as the board is halted;
it starts before the crash screen is drawn and goes on after ESP-IDF's report on the UART [verified:
`crash.c`, `status_led.c`, the built ELF; that it does so on the board is [unknown]]. Note whether
it does. A crash before tiny386 even starts (no panel yet) shows yellow 1 instead. Both are at the
brightness the ini set, or at the default (9) if the crash came before the ini was read; with
`led_brightness = 0` the LED stays dark in a crash too [verified: `test_status`].

Then RESET with VOL+ held: the next tiny386 boot should show the crashed run's last lines in cyan
under a heading; photograph those too. The LED should first give one quick white flash (the last
run crashed), then red and white for 4 s, then the present boot's colour, by then usually magenta
(the boot carries on underneath). That is what the code does when the record and the log ring
are still in the LP SRAM after the RESET [verified: `test_status`, `status_led.c`, `console.c`];
whether the LP SRAM keeps them through the RESET button, which pulls the chip's enable pin low,
is [unknown]; ESP-IDF most likely reports that reset as a power-on [inferred: `STATUS-LED.md` 4],
so nothing but the record says a crash came before. Note both: did the cyan lines appear, and did the LED give the white flash and the
red and white? (RESET with nothing held boots Linux, or tiny386 if it is the default: then
RESET with nothing held is the tiny386 boot to watch.)

A red message that ends `tiny386 has stopped. Put this right, then press RESET.` is not a crash
but something the firmware could not go on from (a missing file, a card that refuses writes, an
image named for two drives); it stays on the panel even if Ctrl+Alt+F12 is pressed [verified:
`console_hold()`].

A panel that simply freezes, with no red report, is not a crash the firmware caught: the task
watchdog only prints on the UART (7b). Take the UART log as in section 11.


### 10b. Only if a scope is at hand: the LED's bits in a crash

The crash pattern is sent by the CPU itself, bit by bit against its cycle counter, and the one
case that runs at the bootloader's 80 MHz (a crash before tiny386 starts, yellow 1) cannot be
made to happen on purpose. A crash-test build gets close: `crash_test = 5` sets the CPU to 80 MHz,
tells the LED that tiny386 has not started, and crashes, so the LED gets the early crash's frames
at the early crash's clock [verified: built, `BUILD-NOTES.md` 15; not run]. It is not in the
release images; to make one, in WSL, in the tree `build.sh` leaves (`WORKDIR/tiny386/esp`):

```
export IDF_TOOLS_PATH=$HOME/.espressif-upstream
. ~/esp/esp-idf/export.sh
idf.py -B build-crash -DBOARD=korvo1 -DIDF_TARGET=esp32s31 -DCRASH_TEST=1 build
```

(`-DBOARD=dosboard` for the DOS board), then flash `build-crash/tiny386.bin` in place of the
release `tiny386.bin` (0xE20000 on the Korvo-1's dual boot, 0x10000 on the DOS board), and add
`crash_test = 5` under `[esp]` in the ini, with `led_brightness = 255` beside it: at the default
brightness yellow 1's frame carries only three 1 bits (green 5, red 8), at 255 seven (green 128, red
219), which gives the scope more to look at; the ini is read before the crash [verified: `esp_main.c`,
`status_code.c`].
Probe the LED's data line: on the DOS board TP948
(`LED_DIN`, 5 V), on the Korvo-1 the WS2812B's DIN pad. Once the crash has happened, catch any
frame: the halt loop sends one at each change of colour and once a second.

You should see, at the LED's pin: a low of 300 us before each frame, then 24 bits; a 0 high about
300 ns (337.5 sent, less the level shifter's rise), a 1 high about 660 ns (700 sent), each bit
1.3 or 1.325 us, give or take about 40 ns at this clock [verified: `test_status`; the shifter's
share is an estimate, `STATUS-LED.md` 5]. Any 1 high under 580 ns, or a 0 high over 380 ns, is
outside the WS2812B-2020's window: photograph the screen. The LED should blink yellow once every
1.8 s (yellow 1). Then `crash_test = 1` the same way gives the normal crash at 320 MHz (red and
white), for comparison. Put the release `tiny386.bin` back afterwards (on the Korvo-1,
`flash-dualboot.bat COM10 app`). Whether the UART's lines after the clock change stay readable is
[unknown]; they should [inferred: the UART's clock, `UART_SCLK_DEFAULT`, is REF_F80M in ESP-IDF's
`clk_tree_defs.h` for the S31, not the CPU's clock].


### 10c. Optional: the DOS board image's guard, on the Korvo-1

The DOS board image is meant to stop at once on any board that is not a DOS board. That guard has
never run on a board; this step tries it on the Korvo-1 without touching Linux, since it writes only
tiny386's own slot at 0xE20000. It writes flash, so it is your choice. Linux powered off first
(risk 1), with the dual boot installed (step 1).

`images\dosboard\tiny386.bin` is 930,960 bytes (v3.9), and the slot at 0xE20000 holds 1,966,080
[verified: the image, `partitions-korvo1-dualboot.csv`]. Its configuration is the Korvo-1 build's
but for the partition table's name, the PSRAM memory test (on) and the USB host options [verified:
the two builds' `sdkconfig.h`], and the board check is the first thing `app_main()` does after the
status LED [verified: `esp_main.c`]. That the dual-boot bootloader starts it from that slot is
[inferred]. By hand, from this folder, as FLASH.txt 3 writes the Korvo-1 image:

```
esptool -c esp32s31 -p COM10 -b 921600 write-flash -fm keep -ff keep -fs keep 0xE20000 images\dosboard\tiny386.bin
```

Then hold VOL+, press RESET, keep holding two seconds, with COM10 open at 115200 8N1 and logging to
a file (section 11).

You should see [verified: host test 18 for the code's side; the board's is [unknown]]: on the
UART, ESP-IDF's start-up lines with `App version:      v3.9` among them [inferred: ESP-IDF
`esp_app_desc.c` line 137, at the INFO log level the build sets]; perhaps a pause of a second or
more while ESP-IDF tests the PSRAM, which the DOS build does and the Korvo-1's does not [inferred:
`CONFIG_SPIRAM_MEMTEST`]; then `dosboard: looking for the MS7210 at I2C 0x59`, two `not found`
lines, and a halt message, again every 10 s, that ends `Halted; nothing else was started.`
(tiny386's own version line never comes: the check stops before it.) In the brackets of the halt message's first line, most likely `GPIO46 high (no R302) and a
device at 0x10, as on a Korvo-1` (the codec; 0x11, 0x14 or 0x5D are possible too). The status LED:
yellow 2, two blinks and a pause, over and over. The panel stays dark [inferred: the DOS image has
no driver for the Korvo-1's panel]. GPIO49, the camera's D3, is never driven.

If instead the UART says `GPIO46 low (R302) ... trying again with the reset`, the Korvo-1's camera
is holding its D0 low, and the image pulsed GPIO49 (camera D3) once for about 70 ms before stopping:
note it and send the log, since that is the one case the board check cannot tell apart (host test 18,
case K4). Either way, nothing else is started.

(`flash-dualboot.bat` itself refuses that image since v3.9, so this step stays by hand.) Then put
the Korvo-1 image back, and check that tiny386 starts as in step 3:

```
flash-dualboot.bat COM10 app
```

A plain RESET goes to Linux throughout, since only the slot at 0xE20000 was written.


## 11. If the panel stays black

First look at the status LED: it says how far tiny386 got without the panel [verified:
`STATUS-LED.md`, `test_status`]. Dark for more than 10 s: tiny386 never started (the bootloader
did not pick it, or start-up hung). Yellow 1: it stopped before starting, most likely the PSRAM
chip not found (a PSRAM found but not set up lets tiny386 start and ends as red 5 instead).
White and nothing more: the panel's start hung. Blue or later: tiny386 got past the panel's start
and is running; either the panel's driver failed (the UART then says `the panel did not start`) or
the fault is between it and the glass. Note the colour or the blinks, then take the UART log.

Then the UART log, which you capture yourself:

1. Open COM10 at 115200 8N1 in your terminal (PuTTY or the like), with logging to a file.
2. Hold VOL+, press RESET, keep holding two seconds, let go.
3. Wait 30 s, save the log, send it.

What it can show: whether the second-stage bootloader picked the test partition (its lines,
if it prints any at its log level, come first), whether tiny386 started at all (`tiny386 for the
ESP32-S31-Korvo-1, version v3.9`), whether the panel driver failed (`the panel did not start`),
and everything else the console would have shown, since every console line also goes to the UART
until the PC starts [verified: `console.h`]; after that, only with `uart_log = 1` in the ini (since
v3.8), but a crash report always.

If even the bootloader's lines are missing, or the board resets over and over while VOL+ is
held, there is no valid tiny386 in flash: run `flash-dualboot.bat COM10 app` again and send its
output.


## 12. What to send back

| From step | What |
|-|-|
| 1 | the MAC line and the `bootloader at 0x2000` line; esptool's output, if it complained |
| 2, 9 | Linux boots: yes or no |
| 3 | photo of the whole boot console; close photo of the colour strip; the LED's colours in order (a short video is best) |
| 3 to 10 | anything the LED did that this plan does not say it should |
| 3 to 5 | the LED's brightness: comfortable at the default (9) or not; if not, the `led_brightness` that suits; whether yellow and red are easy to tell apart |
| 5, 8 | the disk light: does the LED flicker blue while the guest reads its disk, and stay a steady green otherwise; are the flashes easy to see |
| 4 | did the menu take the keyboard; how long the keyboard took to appear |
| 5 | seconds from RESET to `C:\>`; photo of `mem`; key repeat yes or no; Caps Lock light yes or no; which keyboard |
| 6 | photos of `vga13`, `vga12`, `vga12t` |
| 7 | photo of the Ctrl+Alt+F12 title row; photo of the BENCH lines; the UART log of 7b if you took one, and whether `LCD underrun` or `task_wdt` appear in it |
| 8 | the most important this round. The console's `win95.img: N pieces` line; the WIN9X partition's allocation unit; time to the Windows 95 desktop, or what stopped it; the photo of Ctrl+Alt+F12 at the desktop or after the failure (`Disk:`, `Timer:`, `Log:`); after a desktop, the same photo from 8e's run with `timer_catchup = 0` and what that run did; mouse directions, buttons, the fast-circles and busy-disk checks; any error photo; the `PSRAM test:` line if you ran `memtest = 3`; `BOOTLOG.TXT`'s last lines if you made a logged start |
| 10, 11 | the crash photos, or the UART log, if either happened; after a crash and RESET with VOL+, whether the cyan lines and the LED's white flash came |
| 10b | only if a scope was used: photos of a frame at 80 MHz (`crash_test = 5`) and at 320 MHz (`crash_test = 1`), with the time base readable |
| 10c | only if done: the UART log of the DOS image on the Korvo-1, the LED's blinks, and whether the Korvo-1 image started again afterwards |
| 13 | Photos 1 to 5 (the memory lines, the Adapter tab after the install, the desktop at 800x480, Ctrl+Alt+F12 there, Display Properties' Settings); time to the desktop against section 8's; how windows, the mouse and scrolling feel; a minute of the UART's `display:` lines if you can; anything drawn wrong, and whether `vbe_blocks = 0` changes it; Photo 6 if you tried True Color; the A/B of 13d (the speed line with `vbe_blocks` 1 and 0) if you did it; whether a mode 13h game feels as on v3.9 |


## 13. Windows 95 at the panel's own size (v4.0, v4.1)

Since v4.2, section 14 has the mouse pointer at 800x480, after the steps of this section, and since
v4.3 section 15 has T386MON's tests at 640x480 and at 800x480.

This is the step v4.0 is for, after Windows 95 has reached its desktop on the board (section 8; Plat
had it there on v3.9 [reported]). It puts Windows at 800x480 on the Korvo-1, the panel's own size,
in High Color (16 bit), through a free display driver, VMDisp9x, written for exactly the emulated
VGA tiny386 has (QEMU's standard VGA, the Bochs VBE registers). Everything here was done on the PC
first, through Windows' own dialogs as below, and Windows came up at 800x480 in 16 bits, the colours
right [verified: `BUILD-NOTES.md` 22.7 and 23.8, `checks\win95-driver-install-v4.1.txt`]. On the board it is
[unknown] until you try it. It changes the image (the driver goes into Windows), so do it on a copy
you are happy to change, or keep a copy of `win95.img` as it is now.

### 13a. What goes on the card

1. The v4.1 app (`flash-dualboot.bat COM10 app`, FLASH.txt 3).
2. From `card\tiny386\` into the card's tiny386 folder: `vgabios.bin` (v4.0's; it adds the 800x480
   modes for DOS programs: Windows' driver does not need it, but take it), `win95-800x480.ini` and
   `vbe-reg.img` (a floppy image with the two registry files for step 13c).
3. The driver's floppy image, from its author's GitHub release (not in this package: it is not ours
   to pass on): github.com/JHRobotics/vmdisp9x/releases/tag/v1.2025.0.119, the file
   `vmdisp9x-1.2025.0.119b-driver-2d.img` (1,474,560 bytes; SHA-256 96eed45b78672d94f8a90dfa7806dd95
   a71c2114d22c568ec857a07efd174a63 as fetched on 2026-10-23), into the same folder.
4. In `win95-800x480.ini` take the `;` off the two floppy lines, so that they read
   `fda = vbe-reg.img` and `fdb = vmdisp9x-1.2025.0.119b-driver-2d.img`. That way round: with the
   driver's disk in A: the PC stops at "Invalid system disk" before Windows (if that happens, press
   Esc at "Press ESC for boot menu" and then the number of the hard disk); `vbe-reg.img` in A: hands
   the start on to the hard disk by itself.

The ini has `vga_mem_size = 768K`, room for one 800x480 screen in 16 bits, and `mem_size = max`.
**Photo 1**: the boot console's memory lines (`mem_size: ...M, the most there is room for` and
`Emulator memory`). With 768K the guest gets 14M if `Emulator memory` is 15,120 KiB or more, else
13M as with 1M; your 13M at 1M puts it between 14,352 and 15,375 KiB, so either can happen
[arithmetic: `BUILD-NOTES.md` 23.9, which has every depth]. The photo settles it.

### 13b. Install the driver (in Windows, at 640x480 as now)

Start Windows with `win95-800x480.ini` (the boot menu lists it). At the desktop:

1. Start, Run, type `control desk.cpl,,3` and Enter (or Control Panel, Display, the Settings tab).
2. **Advanced Properties** (Alt+A), the **Adapter** tab: "Standard Display Adapter (VGA)".
   **Change...**. The list takes a while (on the PC about a minute of the guest's time).
3. **Have Disk...**, type `B:\` over the `A:\`, OK.
4. The list says "VESA ISA Adapter" only. **Do not take that one** (it is VMDisp9x's other driver,
   which copies the whole screen on a timer). Click **Show all devices**, choose **QEMU STD VGA PCI
   Adapter** (the first), OK. If Windows warns that the driver was not written for this hardware,
   go on (Yes): it was, but Windows has this VGA as an ISA card.
5. "Copying Files..." from B:. The Adapter tab then says QEMU STD VGA PCI Adapter, JHRobotics,
   `qemumini.drv,*vdd,qemumini.vxd`. **Photo 2**: this tab.
6. **Close**, then **Close** on Display Properties, and **Yes** to restart.

Windows comes back at 640x480 in 16 colours, as before: that is expected (the driver is in, but the
mode Windows had, 16 colours, is still the standard VGA driver's).

### 13c. 800x480 in High Color

1. Start, Run, type `regedit A:\VBE480.REG` and Enter (on the DOS board `A:\VBE600.REG`). "Information
   in A:\VBE480.REG has been successfully entered into the registry." OK.
2. Start, Shut Down, **Restart the computer?**, Yes.
3. Windows comes back at 800x480 in High Color: the logon box and then the desktop fill the panel,
   no black bars at the sides, the taskbar the panel's whole width.

**Photo 3**: the desktop, the whole panel. **Photo 4**: Ctrl+Alt+F12 at the desktop (the speed and
`Disk:` lines), then let it go. **Photo 5**: Display Properties, Settings: "Desktop area 800 by 480
pixels", "Color palette High Color (16 bit)".

Then take the `;` back in front of the two floppy lines in `win95-800x480.ini` (they are only for the
install), and start Windows once more without them: the same desktop.

**From now on start this image with `win95-800x480.ini` only** (v4.1, from the review of v4.0). On
the PC, the image as this install leaves it, started with `win95-14m.ini` (320K of video memory, too
little for 800x480 in 16 bits): Windows says "There is a problem with your display settings", opens
Display Properties at 640 by 480 in 256 colours and asks to restart; accepted, it comes back at
640x480, boxed in the middle of the panel. Nothing is lost: started again with
`win95-800x480.ini` it is back at 800x480 [verified: `BUILD-NOTES.md` 23.8]. Should it ever stay
at 640x480, take the `;` off `fda = vbe-reg.img` in `win95-800x480.ini`, start with it, run `regedit
A:\VBE480.REG` again and restart, as in steps 1 and 2 (on the PC that does no harm to an image
already at 800x480 [verified]; that it mends one stuck at 640x480 is [inferred], since no PC run
got stuck). If you want the standard VGA driver too, keep two images on the card, one for each
ini, with `hda` changed in one of them.

The registry file did three things [verified: `card\tiny386\vbe-reg.img`, `VBE480.REG`]: it put
800x480 at 8, 16, 24 and 32 bits into the driver's list of modes (`MODES` under
`HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\Class\Display\0000`), let the monitor
("Unknown Monitor", 640x480 only) go to 800x600, and made 800x480 in 16 bits the current mode. If
Display Properties does not offer 800 by 480 afterwards, the adapter is under another number than
0000 there: say so and send Photo 2.

### 13d. Using it, and what to note

* **Speed.** Windows at this size does more work of its own than at 640x480 in 16 colours (on the
  PC about 8 per cent more guest instructions with the mouse still, up to 28 per cent with it moving
  all the time [verified: `research\NATIVE-VBE-DESIGN.md` 7.3]), and the drawing on the board's other
  core is now far cheaper [inferred: `BUILD-NOTES.md` 22.8]. How it feels is [unknown]: note how long
  the start takes to the desktop against section 8's, and how windows, the mouse and scrolling feel.
  With `uart_log = 1` under `[esp]` the UART says every 10 s `display: N pictures in 10 s, ...`:
  **send a minute of it** (still desktop, then moving the mouse, then a window dragged), if you can.
* **Anything that draws wrong**: a part of the screen that stays stale after something moved over it,
  a smear, the mouse pointer leaving a trail. Photo, and what was done just before. Then try once with
  `vbe_blocks = 0` under `[esp]` (the whole picture drawn every time, as before v4.0, slower): if the
  fault goes, it is the new drawing's.
* **If you have time, the A/B** (v4.1): since v4.1 `vbe_blocks = 0` also takes away the note each
  store into video memory makes for the drawing (a memory fence among it), so the same thing done
  with `1` and with `0` (drag a window round in circles for ten seconds, or scroll a long text) and
  Ctrl+Alt+F12's speed line photographed each time compares the two ways of drawing as a whole: how
  much the notes cost the emulator against how much they save the drawing [unknown until then;
  `BUILD-NOTES.md` 23.2, 23.15].
* **True Color**, if you want it: `vga_mem_size = 1152K` (24 bits) or `1536K` (32 bits) in the ini,
  then Display Properties, Settings, Color palette, True Color, OK, restart. Each takes video memory
  from the guest (at most a megabyte less, depending on the pool: `BUILD-NOTES.md` 23.9) and is
  slower to draw than 16 bits. On the PC both depths ran to the desktop at 800x480, every picture
  right [verified: `BUILD-NOTES.md` 22.7, 23.8], with the depth set in the registry; choosing it in
  Display Properties, as here, was not run [inferred: the driver lists both modes].
  **Photo 6**: the same desktop in True Color, if you try it.
* **A DOS box** (Start, Programs, MS-DOS Prompt; then Alt+Enter for full screen and back; `exit`):
  does Windows' picture come back whole afterwards? Not tried on the PC [unknown].
* **To go back** to the standard VGA driver: Display Properties, Settings, Advanced Properties,
  Adapter, Change, Show all devices, "(Standard display types)", "Standard Display Adapter (VGA)";
  or F8 at "Starting Windows 95..." and Safe mode, which always uses VGA.
* **800x600 on the Korvo-1**: the driver also offers 800x600 (at 768K only in 256 colours). It is
  drawn shrunk to 640x480 in the middle of the panel, as v3 drew every picture larger than the
  panel; nothing gained.

### 13e. The DOS board

The same steps with `VBE600.REG` in 13c and, since v4.1, the DOS board's own ini,
`card-dosboard\tiny386\win95-800x600.ini`, beside the Korvo-1's files in the card's tiny386 folder
(it has `vga_mem_size = 1M`, 16 bits at 800x600; 960K is just enough too, 1408K is for 24 bits and
1920K for 32). The driver lists 800x600 already. On the PC the DOS board's build came up at 800x600
at each of those sizes and depths, and at 1536K (24 bits) and 2M (32 bits) as well; with 768K, too
little for 800x600 in 16 bits, the driver sets no mode and Windows comes up at 640x480
[verified: `BUILD-NOTES.md` 22.7 and 23.8].

### 13f. DOS programs at 800x480 (optional)

The VGA BIOS now has VBE modes 1A0h to 1A4h, 800x480 at 8, 15, 16, 24 and 32 bits (the 16 and
32-bit ones only with the video memory for them), drawn 1:1. A DOS program that lists the BIOS's
modes (4F00h) finds them. `checks\vbe480\BVBE480.COM` (2,687 bytes, SHA-256
8cc09c33d5fbfb953e634339773544553a261c552ff4a354fbf9d81b544c0b1e, also in `SHA256SUMS`) draws each mode's test pictures and waits for a key after each; to see them on
the panel it has to be copied onto `freedos-hd.img` first (mtools in WSL, as the Linux route's
tests do). On the PC every picture was right, pixel for pixel [verified: `BUILD-NOTES.md` 22.6,
23.7]. A DOS game in mode 13h (320x200, 256 colours) on v4.1 should run as on v3.9 [inferred:
`BUILD-NOTES.md` 23.2]; if you have one you played on v3.9, a word on how it feels is worth having.


## 14. The mouse pointer at 800x480 (v4.2)

Your report on v4.1, "800x480 at 16-bit is flickering a LOT", and the notes after it (a still
desktop is fine; the pointer and anything moving flicker, at 256 colours and at 640x480 through the
same driver too; the standard VGA driver's 16 colours do not; and it is slower) are explained, on the
PC: the display driver, VMDisp9x's QEMU driver as released, draws the mouse pointer itself and rubs
it out and draws it again around every bit of drawing Windows does anywhere on the screen. On a real
PC that takes a fraction of a millisecond; here about 18 ms each time, so many of the pictures the
panel shows catch the pointer gone, cut off part way down or black, and a large part of Windows' time
goes on it. tiny386's drawing shows exactly what the guest's screen holds: v3.9's drawing and
`vbe_blocks = 0` give the same pictures [verified: `BUILD-NOTES.md` 24 and 25].

The cure is one byte of the driver, which hands the pointer to Windows' own DIB engine (the way the
driver already has built in). On the PC, with it: the pointer whole in every picture while it moves,
typing, scrolling and moving a window drawn as cleanly as with the standard VGA driver, and Windows
keeping up with the keys again [verified: `BUILD-NOTES.md` 25.9]. `qemumini-pointer\README.txt` has
the why and three ways to make the change; the first needs no card out.

v4.2 also changes two things in tiny386 itself: a clear of the screen (a mode set, twice each time
Windows starts the driver) no longer shows black on the panel before the new picture, and
**Ctrl+Alt+F11** turns `vbe_blocks` the other way at once, for as long as the PC runs (your request).

### 14a. The app

`flash-dualboot.bat COM10 app` (FLASH.txt 3), as for v4.1; its lines should say
`App version: v4.2`. No card file changed: the inis, `vgabios.bin` and `vbe-reg.img` are v4.1's.

Flashing v4.2 alone changes nothing you will see in the pointer: on the PC its pictures are v4.1's,
picture for picture, until the driver is changed [verified: `BUILD-NOTES.md` 25.9 and 26]. The cure
is the driver change in 14c, so do 14b first, then 14c, then 14d.

### 14b. Before the change: what it looks like now (two minutes)

With Windows at 800x480 as section 13 left it (the driver as released):

1. Move the pointer slowly over empty desktop. **Video 1** (ten seconds, the pointer in the middle
   of the frame): on the PC about half the pictures showed it missing or cut off.
2. Leave the mouse still, pointer on empty desktop, and make something else draw: Start, Run,
   `notepad`, and type a line, or hold an arrow key in a long list. **Video 2**: on the PC the still
   pointer blinked with each key.

If what you see is something else (whole-screen black flashes, shifted or garbled lines, parts of
windows left behind), say so before going on: that is not this cause [unknown on the board].

### 14c. The change (pick one; `qemumini-pointer\README.txt` has each step and what it shows)

* **A, on the board, no card out**: Start, Shut Down, "Restart the computer in MS-DOS mode?", then
  type `debug c:\windows\system\qemumini.drv`, `d 2160 l10` (look for `C7 06 B8 06-01 00` in it;
  if it is not there, `q` and stop), `e 2168 0`, `d 2160 l10` (now `-00 00`), `w`, `q`, `exit`.
  This exact sequence ran on the PC on the image the install of section 13 leaves, and Windows came
  back at 800x480 with the pointer whole [verified]. **Photo 1**: the DEBUG screen after the second
  `d`, before `w`.
* **B, on the PC with the card**: `qemumini-pointer\qemumini-pointer.bat X:\...\win95.img` (Windows
  shut down from its Start menu first; the script checks everything, asks, changes one byte, reads
  it back).
* **C, for a fresh install**: the same script on the driver's floppy image makes a changed copy
  beside it (`...-ptrfix.img`) to install from. On the PC, Windows' own install through section
  13's dialogs from such a copy, made by the script, left the changed file in `C:\WINDOWS\SYSTEM`,
  and the pointer was whole afterwards [verified: `BUILD-NOTES.md` 26.2].

To undo: A with `e 2168 1`, or B with `-Undo`.

### 14d. After the change

1. Windows comes up at 800x480 as before. Display Properties, Settings, Advanced, Adapter still
   says QEMU STD VGA PCI Adapter.
2. The two tests of 14b again: **Video 3** and **Video 4**. The pointer should stay whole while it
   moves (a real PC's pointer can still tear for one panel frame where the scan meets it, as it did
   at 640x480 in 16 colours), and should not blink while you type.
3. Speed: type a line in Notepad as fast as you like and see whether Windows keeps up; drag a window
   (its outline) and scroll a long text. On the PC, with the change, Windows kept up with keys sent
   at 6.7 a second, where as released it was 10 s behind 13 s into the typing and 40 s behind a
   little later; compare with section 13's impression. With `uart_log = 1` under `[esp]`, a minute
   of the UART's `display:` and `speed:` lines, as in 13d, is welcome.
   **Moving the pointer itself is no cheaper**: the DIB engine draws each move with generic code,
   about two and a half times the work of the driver's own move, so waving the mouse about while
   Windows works still slows it, more than before (on the PC, Windows started with the mouse
   jiggled the whole time reached its desktop after 609 s with the change, 300 s without it, 220 s
   with the mouse left alone) [verified: `BUILD-NOTES.md` 25.13]. Note whether that matters in use.
   If it does, the only real cure is a pointer the guest never draws at all (an emulated VMware
   SVGA II with its hardware pointer), a large piece of work (`BUILD-NOTES.md` 25.6).
4. Anything that now draws wrong, a pointer trail above all: photo, and what was done just before.
   The DIB engine's pointer is the one Windows draws for most display drivers; DirectDraw programs
   that write the screen directly may draw over it [inferred, not tried].
5. Two things you may see that are Windows' own doing, not tiny386's, and not this flicker: after
   a full-screen DOS box (Alt+Enter) goes back to its window, Windows repaints the desktop over
   several seconds and the screen is black where it has not repainted yet; and at each switch to or
   from full screen the screen is black for up to about a second while Windows has the picture
   switched off. On the PC both happen exactly so in Windows' own screen, picture for picture
   [verified: `REVIEW-v42.md` 5].

### 14e. Ctrl+Alt+F11 (vbe_blocks)

Ctrl+Alt+F11 turns `vbe_blocks` the other way at once; the console's log says `vbe_blocks = 0 now`
or `1 now` (Ctrl+Alt+F12 shows the log, and since v4.2 its info lines say which it is). Up to v4.3 it
lasted until the PC started again and the ini was not changed; since v4.4 the key also saves it in
the ini the PC was started from (16b). With the pointer change in, the A/B of 13d becomes a
two-key job: the same window drag or scroll with 1, Ctrl+Alt+F11, the same with 0, Ctrl+Alt+F12's
speed line photographed each time. On the PC `vbe_blocks` changes nothing in the pictures (the
same pictures either way, to the pixel) [verified]; on the board it changes what the drawing costs
and so perhaps the speed [unknown].

### 14f. The mode set (cosmetic)

Each time Windows starts the driver it sets the mode twice. v4.1 cleared the panel each time with a
memset that the panel showed as a black flash before the picture came back; v4.2 draws the new
picture straight over the old one and makes only the rest black. If you watch the panel as Windows
starts (or a DOS program changes mode): one flash fewer at each mode set, nothing else different
[verified on the PC: the harness counts every pixel a clear makes black before the picture is
done; in no v4.2 run was one of them then drawn in another colour, where v4.1 left 5,417 to 571,222
such pixels a run black between two pictures (`BUILD-NOTES.md` 26.1)].

One more thing, older than v4.2 and not the flicker: when Windows restarts from 16 colours (after
the driver's install in section 13, or whenever it runs with the standard VGA driver), the screen
behind "Windows is now restarting..." and Setup's lines may be light grey, with dark lines between
the character cells, for up to a minute, where it should be black; the text itself is right. On
the PC it comes with the VGA BIOS of v4.0 on (`BUILD-NOTES.md` 26.3) [verified on the PC; on the
board, unknown]. v4.4 fixes it (16d); if you still see it, a photo is enough.


## 15. T386MON on the board (v4.3)

T386MON (`..\t386mon\README.md`) is the monitor you asked for: one window in Windows 95 with the
panel's frames a second, how busy the emulated processor is, tiny386's drawing, memory, disk, timer
and input figures, nine drawing tests, and a log. On v4.3 it reads tiny386's own figures through the
information port; on v4.2 and earlier it shows Windows' own figures only. On the PC, in Windows 95 on
v4.3's core, it found the port, its figures equalled the harness's own counts, and its tests ran to
the end at 640x480 in 16 colours and at 800x480 in 16 bits [verified: `BUILD-NOTES.md` 27.8,
`..\t386mon\README.md` 8.3]. What it shows on the board is the point of this section [unknown]: the
tests' frames a second in real time, the board's drawing time on core 0, the memory left, what
reading the port costs.

### 15a. The app and the floppy

1. `flash-dualboot.bat COM10 app` (FLASH.txt 3), as for v4.2; its lines should say
   `App version: v4.3`. No card file changed.
2. Copy `..\t386mon\t386mon.img` (a 1.44 MB floppy with `T386MON.EXE`) into the card's tiny386
   folder, and add `fda = t386mon.img` under `[pc]`, beside `hda`, in the ini that starts Windows
   (`win95-14m.ini` for 16 colours, `win95-800x480.ini` for 800x480). Its boot sector hands the start
   on to the hard disk, so Windows starts as usual [verified on the PC: `..\t386mon\README.md` 2.1].
   (With `fda = vbe-reg.img` kept, `fdb = t386mon.img` works too, from B:; B: alone does not.)
3. In Windows: Start, Run, `A:\T386MON.EXE`. It takes some seconds of the guest's time to start (12
   to 25 s on the PC). To keep it, copy `T386MON.EXE` to `C:\` in Explorer and take the floppy line
   out again.

### 15b. First look

Leave the mouse still for ten seconds, then maximise the window (its title bar's middle button).

* The **Emulator** line should say `v4.3 on ESP32-S31-Korvo-1` [inferred: `speed.c` gives the
  port the app's version and the board's name; on the PC it says `pc-harness on PC harness,
  esp-korvo1`]. If it says "not running on tiny386", the app on the board is not v4.3: photograph
  it and stop there.
* **Photo 1**: the whole live view. The lines the PC cannot fill and the board can: Panel pictures
  (the milliseconds each picture took to draw, the longest), Board's drawing (core 0's share),
  Board memory (PSRAM and internal RAM free), Board (processor clock, minutes up, the status LED's
  code), and on the Its own cost line the snapshot's microseconds. On the PC a still desktop gave 1
  to 3 fps of 33 or 34 pictures drawn a second, the guest 14 to 17 per cent busy.

### 15c. The tests at 640x480 in 16 colours

With Windows at 640x480 in 16 colours (`win95-14m.ini`: the standard VGA driver):

1. Press **F5** (Run all tests). Leave the mouse alone until the status line says "Tests done";
   T386MON parks the pointer itself and puts it back. About 80 s of the guest's time on the PC.
2. Maximise, and **Photo 2**: the table.

On the PC (frames a second by the harness's clock, where each instruction is 0.5 us and a whole
`rep movs` or `rep stos` counts as one): fill 343, scroll 351, text 7.31, text with the pointer
aside 7.41, with none 7.40, sprite 57.9, full copy 23.5, full picture 0.45; panel fps 33; board
ms/pic 0.0, the PC drawing in no time [verified: `..\t386mon\README.md` 8.3]. On the board the
frames a second are real time; fills and copies cost there what their length costs, so they will be
far lower; text and the pointer should be nearer [inferred: `..\t386mon\README.md` 7].

When you later shut Windows down from 16 colours, the text screen it goes to may look wrong for a
moment (a quarter of a second on the PC; nothing black). That is the known text-screen fault of
14f (`BUILD-NOTES.md` 26.3), older than v4.3, not the port [verified on the PC: `BUILD-NOTES.md`
28.3]. v4.4 fixes it (16d).

### 15d. The tests at 800x480 in 16 bits

With Windows at 800x480 as section 13 left it, the pointer change of 14c in:

1. **F5**, the mouse alone, maximise when done, **Photo 3**: the table.
2. A minute of ordinary use (drag a window, scroll a long text), then **Photo 4**: the live view.

On the PC with the pointer change: fill 480, scroll 323, text 3.50, aside 3.78, no pointer 3.79,
sprite 72.9, full copy 357, full picture 1.13; with the driver as released: fill 47.6, scroll 24.4,
text 1.38, aside 1.39, no pointer 3.80, sprite 14.2, full copy 46.0, full picture 1.11 [verified:
`..\t386mon\README.md` 7.2 and 8.3]. If you still have an image with the driver as released, its
table too is worth a photo: the two say how much of the pointer's cost carries over to the board.

### 15e. Ctrl+Alt+F11 seen in T386MON

In the live view at 800x480 the **VBE drawing** line ends `blocks on`. Press Ctrl+Alt+F11: within a
second it should end `blocks off` (and Ctrl+Alt+F12's log says `vbe_blocks = 0 now`); again, and
`blocks on`. **Photo 5**: the line with `off`. On the PC exactly so, and the port gives the setting
as it is at once, a picture between or not [verified: `BUILD-NOTES.md` 27.8, the harness's
Ctrl+Alt+F11; host test `test_port_blocks`]. With blocks off the rows a second sent to the panel go
up (326 against 86 on the PC): `vbe_blocks = 0` brings back v3.9's drawing, which sends one band from
the first row that changed to the last [verified: `vga.c`]; not a fault. The tests of 15d with each
setting (F5, Ctrl+Alt+F11, F5) are the A/B of 13d with numbers, if there is time.

### 15f. The log, and what to send back

`C:\T386MON.LOG` has a line every 5 s, both test tables and a summary; it is written to the disk
every 60 s, after the tests and when T386MON closes, so close it (or shut Windows down) before the
board goes off. To read it on the PC, from a copy of the card's `win95.img`:
`mcopy -i win95.img@@32256 ::/T386MON.LOG .` in WSL (`..\t386mon\README.md` 2.4). Photos 1 to 5 and
the log are what the next round works from; and anything that looked wrong: T386MON's window drawn
wrong, Windows much slower with it open (its own cost on the PC: about 6 per cent of the guest with
the live view at 800x480, 4 at 640x480, less when compact or minimised).


## 16. v4.4 and v4.5: settings saved, two cures for tearing, and Windows on a partition of its own

v4.4 answers your T386MON log and your notes on v4.3. Ctrl+Alt+F11 now saves `vbe_blocks` in the
ini (and the two new keys save theirs); the grey screen at a restart from 16 colours is gone; VBE
pictures cost core 0 less; two ways to stop the tearing you saw are there to try, both off unless
you turn them on; and Windows can run from a partition of the card instead of `win95.img`, on a new
card laid out for it, with the old card kept as it is. v4.5 is v4.4 with the fixes of its two
reviews (`BUILD-NOTES.md` 31): the ini saved without renaming anything, the speed screen and
Ctrl+Alt+F9 together, the swap counts on the speed screen, the old card's WIN9X partition refused
for `sd:3`, and the new card's p3 type. Nothing of v4.4 has run on a board, so flash v4.5 and take
this section with it. On the PC: all of it [verified: `BUILD-NOTES.md` 29, 30 and 31]. On the
board: none of it yet [unknown]; this section is what to try. (2026-10-25: you have run v4.5, and
all of it worked, the new card too, but `double_buffer = 1`, which crashed at boot, and pacing,
which did not help [reported]. v4.6 answers both: section 17.)

### 16a. The app, and the card's files

1. `flash-dualboot.bat COM10 app` (FLASH.txt 3), as for v4.3; its lines should say
   `App version: v4.5` (v4.6 now: 17a).
2. The card: `card\tiny386\win95-14m.ini` and `win95-800x480.ini` have new lines, all commented
   out (what each new setting does, so that a key's save lands beside its comment): copy them over
   your old ones if you have not changed those, or leave yours, which work as before.
   `win95-sd.ini` is new, for 16e, and goes only onto the new card: on the old card its partition
   3 is the WIN9X drive with `win95.img` in it, which v4.5 refuses to hand the guest raw (a red
   line, and the PC does not start). No other card file changed.

### 16b. The Ctrl+Alt keys save their settings

Start Windows at 800x480 (`win95-800x480.ini`) and press Ctrl+Alt+F11. Ctrl+Alt+F12's log should
show `vbe_blocks = 0 now`, then `vbe_blocks = 0 saved in /sdcard/tiny386/win95-800x480.ini (a line
added to [esp]).`: the ini has the setting only as a comment, `;vbe_blocks = 1`, and the new line
goes straight under it. Press it again: `vbe_blocks = 1 saved in ... (its line in [esp]
changed).` The same for Ctrl+Alt+F10 (`pace_quiet`) and Ctrl+Alt+F9 (`double_buffer`). A red line, `... could not be saved in ...`, says why, and the
setting still holds until the PC starts again.

Wait for the `saved in` line before you switch the board off.

**Photo**: the log with the saved line. Then, after Windows is shut down, the ini on the PC: only
that one line should differ, with its CRLF or LF kept [verified on the PC: `test_ini_edit.c`, 28
shapes of ini, 149 checks; `BUILD-NOTES.md` 29.2 and 31.2]. v4.5 makes a whole copy of the new
text beside the ini (`NAME.ini.t386new`, with a check line at its end), then rewrites the ini in
place, then removes the copy; nothing is renamed. If the card loses power part way, the next start
finds the ini whole, as it was or saved, and puts the copy in place or removes it [verified on the
PC: a cut at each step]. A yellow line at start-up about `.t386old` and `.t386new` files means a
v4.4 save was cut inside a rename; then check the card on the PC (`chkdsk X: /f`) and delete those
files (`BUILD-NOTES.md` 31.2).

### 16c. Tearing: two cures to compare

Both are off by default. Try each with the pointer change of 14c in:

* **Pacing** (`pace_quiet`, Ctrl+Alt+F10): a picture waits until Windows has stopped drawing for a
  moment (5000 of its instructions), at most 15 ms. Press Ctrl+Alt+F10 while you move windows and
  type, and again to turn it off: does the sprite of T386MON's test, or a window you drag, still go
  black and back? On the PC it takes the pictures caught part way through drawing from 7, 4 and 4
  per cent of your session's to 2, 0 and 1, but T386MON's tests get a third fewer pictures
  [verified on the PC: `BUILD-NOTES.md` 29.7]. (2026-10-25: on your board it changed none of the
  glitches you see and lowered the panel's frame rate [reported], so it stays off by default.)
* **Double buffering** (`double_buffer`, Ctrl+Alt+F9, the Korvo-1 only): Windows' picture goes to a
  second frame buffer, shown at the panel's next frame, so no frame shows two pictures at once (your
  16-bit colour bands). Set `double_buffer = 1` in the ini (or press Ctrl+Alt+F9, which saves it, and
  restart). (v4.6: both frame buffers are now made with the panel at power-on, after tiny386
  starts itself again once, and only with tiny386 the default at reset; section 17 has the steps,
  the lines to expect, what to do if it goes wrong, and the photos. v4.5's way crashed on your
  board, the load access fault at `0000fa50`.)
  **Photo** the boot console's memory lines: the second buffer takes about 750 KiB of the emulator's
  memory, which costs Windows 1M or nothing, depending on your pool (v4.6: with your pool,
  nothing for `win95-sd.ini` and 1M for `win95-14m.ini`, 17c [inferred]); the boot console's
  `Emulator memory` and `mem_size` lines in that photo decide it
  (`BUILD-NOTES.md` 29.6 and 31.5). Then T386MON's full-picture test at 16 bits: the band of mixed
  rows should be gone. Ctrl+Alt+F12's `double_buffer = 1` line counts the swaps since the start and
  how many the panel did not report (v4.5); if that second count goes up and the picture stops
  changing, set `double_buffer = 0` and tell me. (With `uart_log = 1` the UART's `display:` line
  has the same every 10 s, with the copy's and the wait's time.) T386MON's `draw_ms` leaves out
  that copy and that wait, so with double buffering on it shows less than core 0 spends on each
  picture.

Then T386MON's tests at 800x480 again, both cures off (**Photo** the table): the drawing's
milliseconds should fall in fill, scroll, text and sprite, and stay in the full copy
[verified on the PC: `BUILD-NOTES.md` 29.4; the board unknown].

Two choices are yours after this: whether double buffering is worth 1M of Windows' memory, and
whether pacing should be on by default for Windows. (2026-10-25: pacing stays off, from your run of
v4.5. Double buffering's is still yours, once it runs: 17.)

### 16d. The grey screen at a restart from 16 colours

Fixed: a text screen is drawn again whole when Windows loads its font. If you still see the grey
screen with dark lines behind "Windows is now restarting..." (14f), photograph it [verified on the
PC: the install's 1,706 wrong pictures are none; `BUILD-NOTES.md` 29.3].

### 16e. The new card: Windows from partition 3

`newcard\README.md` has the steps: Linux's root copied off the old card on the board's Linux
(`export-root.sh`), everything into WSL, `newcard.sh` builds the new card as an image file, and
you write it with your image writer. The old card keeps its partition table, its Linux root and
its `win95.img`; it only gets files added to its two FAT drives (`export-root.sh` itself on
KORVO_SD; `korvo-root.img`, `korvo-mbr.bin` and `korvo-export.txt` on WIN9X), and its root is
read-only for the few seconds of the copy. What the new card holds: p1
`KORVO_SD` with the tiny386 folder (512 MiB), p3 Windows' own FAT16 volume straight after it,
taken out of your `win95.img` sector for sector, and p2 your Linux root after that; the table
lists them as p1, p2, p3, so Linux finds its root where it always has.

1. **Linux** from the new card, as usual: it should come up as before, with the tiny386 folder in
   `/mnt/sd`. `poweroff`.
2. **tiny386**, VOL+ through reset, `win95-sd.ini` in the menu. On the boot console, after the
   card's partitions: `Card partition 3: FAT16, 400 MiB, at /sdcard3.`; after the ini's settings:
   `Card partition 3 unmounted from /sdcard3: the guest has it whole.` and `hda: card partition 3,
   FAT16, 400 MiB from sector 1056768, read and written raw: the guest's disk is 916 MiB, this
   partition alone in its table, nothing outside it writable.` (the default layout's numbers). No
   yellow line. **Photo** the console.
3. Windows should start and run as it did from `win95.img`. Ctrl+Alt+F12's `Disk:` line counts the
   partition's reads and writes as it counted the image's. **Photo** the speed screen at the desktop.
4. Shut Windows down, switch the board off once Windows says it is safe, and put the card in the
   PC: p3 shows up as a FAT16 drive beside KORVO_SD. `chkdsk X:` with its letter, without `/f`
   (it only reads): **copy** what it prints. Copying a file onto p3 and seeing it in Windows 95 at
   the next start is worth a try too.

On the PC: Windows 95 reached the desktop from p3 of a card image made by `newcard.sh` from a copy
of the pristine `win95.img`, with p3 at 516 MiB (the default) and at 68 MiB, writing what it writes
from `win95.img`, with C: in protected mode, not "MS-DOS compatibility mode", and the card's table,
p1 and p2 unchanged to the sector [verified: `BUILD-NOTES.md` 30.5, and v4.5 again in 31.11]. What
tiny386 refuses, each with a plain red line and the PC not started: `sd:1` (where the inis are),
Linux's root, an empty slot, a partition that is not FAT16 or FAT32, one that overlaps another, p3
mounted by its own FatFs, and (v4.5) the old card's WIN9X partition [verified: host test `sdpart`,
the harness].

5. Afterwards (`newcard\README.md`, "Afterwards"): only one of the two cards in the PC at a time
   (they share a disk signature), and once the new card works, delete `korvo-root.img` from the old
   card's WIN9X drive and from `\\wsl$\Debian\home\owner\newcard`: it holds your Linux root's Wi-Fi
   settings, SSH host keys and shell history.

The DOS board takes `hda = sd:3` too (commented in `win95-800x600.ini`) [inferred: the same
`storage.c` and `sd_part.c`; not run on the PC for that board].

### 16f. What to send back

The photos of 16b to 16e, the ini after a save, T386MON's log and tables with each cure, the speed
screen with double buffering on (its `double_buffer = 1` line has the swap counts), the boot
console's memory lines with and without double buffering, the two choices of 16c, and for the new
card the console, the speed screen and `chkdsk`'s output; and anything that looked wrong.


## 17. v4.6: double buffering made at power-on, and the guest kept out of the devices' state

v4.6 answers your run of v4.5 on 2026-10-25 [reported]. Everything worked, the new card too, but
`double_buffer = 1` stopped the boot with a load access fault at `0000fa50`, and pacing changed none
of the glitches and lowered the panel's frame rate. The crash: v4.5 deleted the panel once the ini
was read and started it again with two frame buffers. On your board that start failed, with two
and then with one, and the VGA code was then handed a frame buffer at address 0 [verified on the
PC: the host test faults at the same address; that your board took this path is inferred]. Why the
panel would not start again is not known. Memory is the likeliest cause [inferred].

v4.6 makes both frame buffers with the panel at power-on, before anything else takes memory, and
never starts the panel again (`BUILD-NOTES.md` 32.3). The ini is only known after the boot menu, so
a boot with `double_buffer = 1` starts tiny386 again once by itself. Pacing stays off by default,
unchanged (16c). v4.6 also takes in upstream's fix that moves the devices' state 32 bytes up, and
three checks of our own that keep the guest's stores and DMA out of it (`BUILD-NOTES.md` 32.4).
On the PC Windows 95 does exactly what it did on v4.5 [verified: the same instruction counts in
three runs]. Nothing of v4.6 has run on a board [unknown]; this section is what to try.

### 17a. The app

1. `flash-dualboot.bat COM10 app` (FLASH.txt 3), as for v4.5; its lines should say
   `App version: v4.6`. No card file changed.
2. For double buffering tiny386 must be the default at reset: `flash-dualboot.bat COM10 table
   tiny386` (FLASH.txt 2), if it is not already. tiny386 restarts itself once for double
   buffering, and with Linux the default that restart would start Linux, so there it does not
   restart and keeps one frame buffer (17d).

### 17b. First with double_buffer = 0: the memory lines

First check on the PC that `win95-sd.ini` has `double_buffer = 0`, or the line commented out: you
set it to 1 for v4.5. Then boot it. Everything should be as on v4.5, with no new line about double
buffering. One line reads differently:

    PC built. Device memory (the 0xA0000 window): 93344 of 131040 bytes used. ...

v4.5 said `of 131072`: upstream's fix leaves 32 bytes between the guest's own memory and the
devices' state, so a store that runs over the end of the guest's memory there cannot reach it. The
used figure should be v4.5's [verified on the PC harness: 91616 of 131040 with `win95-14m.ini`'s
settings, 91616 of 131072 on v4.5; 93344 for `win95-sd.ini` and 93408 for `win95-800x480.ini`
inferred from the build's sizes].

**Photo** the boot console's memory lines: `Emulator memory: ... KiB` (15,104 KiB on v4.5, from
your photo of the crash), `mem_size: 13M, ...` and the `PC built.` line.

### 17c. double_buffer = 1, from power-on

With tiny386 the default: set `double_buffer = 1` under `[esp]` in `win95-sd.ini` (on the PC, or
Ctrl+Alt+F9 once Windows runs, which saves it). Switch the board off, then on again (power, not
only RESET), and let the menu start `win95-sd.ini` or choose it.

1. After the ini's settings, a grey line:

       double_buffer = 1: tiny386 starts again now, once, so that the panel gets both frame
       buffers before anything else takes memory. The next start takes win95-sd.ini without the
       menu.

   One second later the panel goes dark for a moment and the boot console starts again by itself.
   A phone video catches this best.
2. The second start's banner says `Reset by a software restart.`, then, in grey: `This start is
   tiny386's own restart for double_buffer = 1: the panel was started with two frame buffers,
   before anything else.` No boot menu: `Starting win95-sd.ini, as chosen before the restart (no
   menu).` in green. After the settings: `double_buffer = 1: two frame buffers of 750 KiB, both
   made with the panel at power-on, before the emulator's memory (N KiB); pictures go to the panel
   at its frame.` **Photo** the whole console.
3. The memory lines in that photo: `Emulator memory:` about 750 KiB less than in 17b (about 14,350
   KiB), and still `mem_size: 13M`. With `win95-sd.ini`'s `vga_mem_size = 768K` the second buffer
   costs Windows nothing on your board; with `win95-14m.ini`'s 14M at 320K it would cost 1M, and a
   yellow line would lower `mem_size` to 13M [inferred: arithmetic on the code, with your pool].
   The `PC built.` line as in 17b.
4. In Windows, T386MON's full-picture test at 16 bits: the band of mixed rows should be gone. Note
   the panel's fps against 17b's. After a few minutes, **photo** Ctrl+Alt+F12's `double_buffer = 1`
   line: it counts the swaps, and those the panel did not report. If that second count goes up and
   the picture stops changing, set `double_buffer = 0` and tell me.
5. RESET once: the same restart and the same lines. Every boot with `double_buffer = 1` makes this
   one restart, but a boot after a crash, a watchdog or a power dip (17d).

### 17d. If it goes another way

* **A yellow line** in place of step 2's grey ones:

      double_buffer = 1: one frame buffer at this start. The panel would not start with two at
      power-on (ESP_ERR_...), with PSRAM free ... KiB (largest block ... KiB) and internal DMA
      memory free ... KiB; it started with one, as with double_buffer = 0. ...

  **Photo** it. The error name and the three memory figures say why the panel will not take two
  frame buffers, which no one knows yet. The PC starts 10 s later with one frame buffer, as with
  `double_buffer = 0`; one Ctrl+Alt+F9 once Windows runs saves `double_buffer = 0`.
* **`It followed a software restart with no note of tiny386's own ...`** straight after step 1's grey
  line: the LP SRAM did not keep the note through the restart. One restart only, no loop. Tell me.
* **`The last start was cut short while it started the panel with two frame buffers ...`**: the
  chip reset inside that start. **Photo** the banner's reset line too.
* **Two restarts in a row**, the panel dark from the first until the third start: after the first
  restart the panel would not start, with two frame buffers or with one, so tiny386 started again
  by itself, with one. The third start says in grey `This start is tiny386's second restart for
  double_buffer = 1: ...`, and in yellow:

      double_buffer = 1: one frame buffer at this start. After tiny386's restart for it the panel
      would not start with two frame buffers (ESP_ERR_...), nor with one after that (ESP_ERR_...),
      with PSRAM free ... KiB (largest block ... KiB) and internal DMA memory free ... KiB, ...

  **Photo** it: the two error names and the memory figures are the best clue yet to why v4.5 could
  not start the panel again [inferred]. The PC starts 10 s later with one frame buffer. Set `double_buffer = 0` in `win95-sd.ini` on the PC,
  or press Ctrl+Alt+F9 once Windows runs, which saves it.
* **After a crash, a watchdog or a power dip** (the banner's `Reset by ...` line in yellow, or `The
  last run ended with the crash screen. Its last N lines:`): that start does not restart, so that
  the last run's lines stay on the panel for the 10 s hold, and a yellow line says so,
  `double_buffer = 1: one frame buffer at this start. The last run ended with a crash, a watchdog or
  a power dip (above), ...`. **Photo** the whole console. RESET then makes both frame buffers again.
* **A dark panel that stays dark, with the status LED's crash code**, after the restart: a crash
  inside the start with two frame buffers, before the boot console was up, so nothing could be
  drawn, and the chip halts. Switch off, set `double_buffer = 0` in `win95-sd.ini` on the PC, and
  send a photo of the LED's code (`STATUS-LED.md`). Whether RESET alone would give a start with one
  frame buffer depends on the LP SRAM keeping the note through RESET [unknown].
* **A dark panel with a green LED**, Windows running with nothing to see: it should not happen
  now. Do not press RESET while the disk light flickers. When it has stayed off for a minute, switch
  off, set `double_buffer = 0` on the PC, and tell me. Windows may run ScanDisk at its next start.
* **More than two restarts in a row**, or step 1's grey line twice in a row: it should not happen,
  as a start that follows a software restart never restarts for the note, and the second restart
  comes only straight after the first, before the boot console is up. Switch off, set
  `double_buffer = 0` on the PC, and tell me.
* **With Linux the default** (VOL+ for tiny386): no restart, and a yellow line, `double_buffer = 1:
  one frame buffer. The second is made with the panel at power-on, by one restart, and tiny386 is
  the dual boot's VOL+ app here, so a restart would start Linux. ...`. That is expected.
* **Linux's `reboot` into tiny386**, with tiny386 the default: one frame buffer, and the yellow
  "software restart with no note" line, which says to press RESET. Also expected [inferred: which
  reset cause Linux's reboot gives tiny386 is not known; if it is not a software restart, tiny386
  makes its one restart, which is also safe].

### 17e. Windows 95 as before

The memory checks change nothing Windows does on the PC [verified: the same instruction counts to
the desktop, through a restart and in MS-DOS mode]. On the board, with `double_buffer = 0`: start
Windows from p3, restart it once from the Start menu, and once into MS-DOS mode (`mem`, `dir`,
`exit`), as on v4.5. Anything different is worth a photo. T386MON's instructions a second, against
your v4.5 figure on the same test, would show whether the change to each 16 or 32-bit access costs
anything you can see: one instruction more and one memory load fewer [verified: the board's
compiled code; the time on the board unknown].

### 17f. What to send back

The photos of 17b to 17d (the boot console with each setting, the speed screen with double
buffering on), T386MON's full-picture test with double buffering on, any yellow line, how many
times the board restarted, and whether the band of mixed rows is gone. Then the one choice left
from 16c: whether double buffering is worth what it costs.


## 18. v5.0: the fast interpreter, and the frame buffer's own path

v5.0 answers your word on v4.6: it works, and it is too slow [reported]. Your T386MON log at
800x480 in 16 bits had the guest busy all the time, at 1.5 to 1.7 million instructions a second;
the text test about 580,000 guest instructions a picture; fill at 65 pictures a second but only
0.40 million instructions a second, each store into the frame buffer going the long way through
tiny386's memory-mapped path. Halving the panel's drawing rate did not change the instruction
rate [reported], so core 1, the PC's processor, is what holds it back [inferred]. v5.0 makes two
changes there (`BUILD-NOTES.md` 33):

* **The fast interpreter** (`FAST-CPU.md`): Yejun Yang's, from his tab5-pc, in front of tiny386's
  own. It does the common instructions itself, about twice as cheaply, and hands the rest to
  tiny386 one at a time. `[esp] fast_cpu = 0` runs tiny386's own alone.
* **The frame buffer's own path**: loads and stores into the VBE frame buffer are done by the CPU
  itself, with the drawing's marks, and a REP STOS or REP MOVS into it is one fill or copy for each
  page, where each dword was a call through `pc.c`.

On the PC the guest sees exactly what v4.6 shows it, with `fast_cpu` 1 or 0: a lockstep tester
compares the two through Windows' boot, a restart, MS-DOS mode, BENCH, the frame buffer and
T386MON's tests, and finds no difference [verified: `BUILD-NOTES.md` 33.4]. Counted on the board's
RV32 code under qemu, core 1's work for a guest instruction falls to about half (`BUILD-NOTES.md`
33.6). What that is in time on the board is [unknown]; this section is how to find out. Nothing of
v5.0 has run on a board.

### 18a. The app

1. `flash-dualboot.bat COM10 app` (FLASH.txt 3), as for v4.6; its lines should say
   `App version: v5.0`.
2. No card file has to change. The four Korvo-1 inis in `card\tiny386\` now have a commented
   `;fast_cpu = 1` line under `[esp]`, with what it does; copy them only if you want the comment.
   For the A/B below, add `fast_cpu = 0` under `[esp]` in the ini you start, and take it out again
   (or set it to 1) afterwards.
3. Keep `double_buffer`, `pace_quiet` and `vbe_blocks` as you had them on v4.6, and the same for
   both halves of each A/B.

### 18b. The boot console

Start your usual ini (`win95-sd.ini`). After the ini's settings and before `PC built.`:

    CPU: the fast interpreter (from tab5-pc, by Yejun Yang) in front of tiny386's own; fast_cpu = 0 in the ini for tiny386's alone.

With `fast_cpu = 0`: `CPU: tiny386's own interpreter alone (fast_cpu = 0), as v4.6.`

**Photo 1**: the whole console, with the `CPU:` line and the two memory lines: `Internal RAM free
... KiB (largest block ... KiB); PSRAM free ... KiB.` near the top, and `PC built. ... Internal RAM
free ... KiB (largest block ... KiB); PSRAM free ...` at the end. v5.0 keeps the fast interpreter
(120 KB, and its 8 KB TLB) in internal RAM and moves tiny386's own interpreter (132 KB) to flash,
which leaves 241,778 bytes of internal RAM after code and static data, 10 KB less than v4.6's
251,846 [verified: the build's size report, `BUILD-NOTES.md` 33.8]; so both lines should read
about 10 KiB lower than on v4.6. What is free at run time is [unknown] until this photo. If the
second figure is 150 KiB or more, a build with tiny386's interpreter in internal RAM too (it would
take about 128 KiB of it) is worth trying (`FAST-CPU.md` 5).

### 18c. BENCH in FreeDOS

`tiny386.ini` (FreeDOS), BENCH as on v3.6: at its end the console shows `BENCH.COM is done ...` and
a line ` in BENCH (without a4)` with the instructions a second.

1. With the default ini (`fast_cpu` not set, so 1): **Photo 2**, the BENCH lines.
2. With `fast_cpu = 0` under `[esp]` in `tiny386.ini`: **Photo 3**, the same lines.

v3.6 gave 2.231 million instructions a second [reported]. Expected [inferred: `BUILD-NOTES.md`
33.6, from the qemu counts and v3.6's figure]: about 4 million with the fast interpreter, anywhere
from 3.3 to 4.8 for the reasons 33.6 gives; with `fast_cpu = 0` somewhat below v3.6's, because
tiny386's own interpreter now runs from flash. If `fast_cpu = 1` is not faster than `fast_cpu = 0`,
that is worth knowing at once.

### 18d. T386MON at 800x480 in 16 bits

As 15d, with Windows at 800x480 and the pointer change in:

1. With `fast_cpu = 1` (the default): start Windows, T386MON, **F5**, leave the mouse alone until
   "Tests done", maximise, **Photo 4**: the table. Then a minute of ordinary use and **Photo 5**:
   the live view (the Guest processor line: instructions a second, and how busy).
2. Shut Windows down, set `fast_cpu = 0` in the ini, start again, and the same: **Photo 6** (the
   table) and **Photo 7** (the live view).
3. Close T386MON before switching off, so that its log is on the disk; `C:\T386MON.LOG` has both
   runs' tables (15f says how to read it on the PC).

What to look for [inferred: `BUILD-NOTES.md` 33.3 and 33.6]: the text tests, which were bound by
the guest's own work, should gain most from the fast interpreter, up to about twice; fill, scroll
and the full copy, done by REP STOSD and REP MOVSD into the frame buffer, should gain from the
frame buffer's path with `fast_cpu` 1 and 0 alike, by how much depends on how fast the PSRAM takes
the stores; the sprite test's AND and OR into the screen are handed to tiny386 whole, and may gain
least. Against your v4.6 log, the same tests.

### 18e. Windows as before

With `fast_cpu = 1`: start Windows, Start, Shut Down, Restart; then Start, Shut Down, Restart in
MS-DOS mode, `mem`, `dir`, `exit`, back to the desktop. On the PC both give v4.6's instruction
counts exactly [verified: `BUILD-NOTES.md` 33.5]. Anything that looks different from v4.6, or
different between `fast_cpu = 1` and `0`, is worth a photo.

### 18f. If it goes another way

* **A crash with `fast_cpu = 1` that does not come with `fast_cpu = 0`**: a difference the PC's
  tests missed. **Photo** the crash screen (its CS:EIP and the lines above it) and note what was
  running; then `fast_cpu = 0` to carry on.
* **The `CPU:` line says `this build has no fast one`**: the app is not v5.0's. Check the `App
  version` line of 18a.
* **Slower with `fast_cpu = 1` than with `0`** on any test: a photo of both tables; that would say
  where the hand-over to tiny386 (now in flash) costs more than the fast interpreter saves.
* **`Internal RAM free` much lower than on v4.6** (by more than about 20 KiB) or a yellow line
  about memory: a photo of the console.

### 18g. What to send back

Photos 1 to 7, `C:\T386MON.LOG`, and how each felt: Windows' desktop, dragging a window, typing
in Notepad, with `fast_cpu` 1 and 0. The figures decide what comes next: the board's own
instructions a second against the PC's counts calibrate every estimate in `BUILD-NOTES.md` 33.6.


## Other ways to run it

* tiny386 alone, replacing Linux: `flash-standalone.bat COM10` (FLASH.txt 5), Linux powered off
  first. It takes the same backup first if there is none. The card and everything from step 3
  on are the same, without the key: tiny386 starts on every reset. Back to Linux is then
  `back-to-linux.bat COM10 whole` (or `full be5b725`) (FLASH.txt 4).
* The DOS board, once it exists: FLASH.txt 6, and its own settings in `dosboard.ini`. Its first
  light follows `..\..\tiny386-hdmi-board\research\encoder.md` 8.7 and is a plan of its own. What
  this firmware adds to it [verified: `dosboard.c`, `card-dosboard\tiny386\dosboard.ini`]:
  * the board check looks for the MS7210 at I2C 0x59, and at 0x2B if nothing answers there (the
    data sheet's figure 3 draws SA the other way from its text); the UART and the console say
    which one answered. Since v3.5 it asks first without the reset pulse (RSTN is GPIO49 on rev
    A2c, a camera input on a Korvo-1, so it is not driven unless the MS7210 has answered or, since
    v3.6, GPIO46 has read low), then again with it. The line `MS7210 found at I2C 0x59, before and after its reset` is the
    expected one; `only after` means the chip needed the reset to answer at all (worth noting);
    `found before its reset, not after it` stops the image and points at RSTN. Since v3.6, if
    nothing answers at all, the check reads GPIO46 (the CH9350L's S0, held low by R302) with its
    weak pull-up on for a moment: low, and the UART says `GPIO46 low (R302): trying again with the
    reset`; high, and the image stops with `GPIO46 high` in its message and RSTN never driven, so
    look at R302. Something on J202's I2C pins at 0x10, 0x11, 0x14 or 0x5D no longer stops a DOS board: the
    console notes it as `a device at I2C 0x.. too` [verified: host test 18];
  * the PCLK pairing: the default is now `pclk_neg = 1` with `pclk_phase = 4`, opposite edges by
    ESP-IDF's register description, as both reference tables have the MS7210. At first light try
    all four pairs listed in `dosboard.ini`, in order, and prefer a pair whose `pclk_phase` 5 to 7
    neighbours give a clean picture too: a same-edge pair can pass a quick look and sparkle when
    warm or on another board [inferred: the DOS board review, round 2];
  * look on the UART for `LCD underrun` (7b): at 800x600 from PSRAM the bandwidth is tighter
    than on the Korvo-1. If it appears, try `lcd_bounce_px = 8000` before anything else;
  * red and blue swapped on the colour card: `rb_swap = 1`, no rebuild;
  * no sound: unplug and plug in the HDMI cable once (that sets the MS7210's audio up again with
    the sound already running, which tells the two possible causes apart), then
    `i2s_slot_bits = 32`, which since v3.2 makes a proper 64-clock frame (word select high for a
    whole slot; a scope on TP938 should show 50 per cent duty in both settings); the level is now
    `audio_shift = 2` by default (v3.5: 12 dB louder than before, and nothing can clip); too quiet
    on a TV: `1` or `0`, and if the UART then says values were `held at full scale`, go back one;
    clicks or drop-outs: the I2S clock from the APLL instead of the crystal, a rebuild [inferred:
    round 1 review F7];
  * a DVI monitor that shows nothing: `hdmi_mode = dvi`. Since v3.2 a monitor whose one EDID
    extension is not a CEA-861 block (DisplayID, say) gets DVI by itself;
  * the first-light line `PCLK meter now ...` is read after set-up, so a 0 in the first meter line
    alone is not a fault; the line is no longer cut short;
  * the MS7210's video input pads are set to pull-ups, not the vendor's pull-downs, to help the
    S31's GPIO36 strap (DOS board review round 2, S2);
  * an MS7210 that was reset is noticed at the next hot plug poll (every half second) and set up
    again, but only if register 0x120C reads back what was written at set-up, and at most three
    times in a row: a line `no watch for a reset chip` says the watch is off;
  * the status LED (rev A2's D201, the Korvo-1's circuit on the same pin) is often the first thing
    to look at, since a board with no picture has nothing else: yellow 2 for an MS7210 not found
    (the image stops) or not answering, yellow 3 for no monitor on the hot plug line, yellow 4 for
    a CH9350L that sent nothing 3 s after its reset, yellow 1 for a PSRAM that failed its memory
    test (on for the first boots), and the Korvo-1's colours for the rest (`STATUS-LED.md`). The
    Inolux LED may light blue by itself for a moment at power-on [reported: its datasheet]. A
    scope on TP948 shows the LED's 5 V data (STATUS-LED.md 5);
  * the line out and, from rev A2c, the speakers on J703 (`LINE-OUT.md` has the bench steps): XSMT
    (TP949) low from the board check until the PC starts, high 20 ms after the I2S clocks start;
    the same sound as HDMI at the same level (`audio_shift`). A plug in J701 must silence the
    speakers by itself. Note any click at power-on, at the board check, when the PC starts, at a
    crash and at RESET, and how loud the speakers are at the default level;
  * the J202 header (rev A2c): GPIO2 to GPIO7, GPIO57 and GPIO60 are left as the chip leaves them
    at reset, not driven (the chip data sheet gives GPIO2 to GPIO7 no pull and no input after
    reset, GPIO57 input only, GPIO60 a weak pull-up [verified: ESP32-S31 datasheet v0.5 table
    2-1]). With tiny386 running, a 10k resistor from any of them to GND, then to 3V3, should pull
    it all the way each time; GPIO0 and GPIO1 are the I2C bus with its 4.7k pull-ups.
