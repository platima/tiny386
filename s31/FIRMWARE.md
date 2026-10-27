# tiny386 v3: bare metal on the ESP32-S31

tiny386, an x86 PC emulator, running straight on the ESP32-S31 under ESP-IDF with no Linux under
it: on Plat's ESP32-S31-Korvo-1 beside Linux (hold a key at reset for tiny386, nothing for
Linux), and on the DOS board, the HDMI board being designed alongside it (in a repository of its own). One
source tree builds both. Written 2026-10-21; revised the same day after two rounds of four
independent reviews (`REVIEW-RESOLUTION.md` has every finding and what became of it), on
2026-10-22 for the status LED and the DOS board's line out (v3.3, `BUILD-NOTES.md` 14), and again
the same day after the review of v3.3 (`REVIEW-v32.md`, v3.4, `BUILD-NOTES.md` 15), once more
for the DOS board's rev A2c: its pins moved off GPIO2 to GPIO7, its speaker amplifier, and a
louder default sound on every output (v3.5, `BUILD-NOTES.md` 16), after the review of v3.5
(`REVIEW-v35.md`, v3.6, `BUILD-NOTES.md` 17), for the status LED, which v3.7 makes much dimmer
and adjustable (`BUILD-NOTES.md` 18), then for Windows 95 and the front panel: v3.8 fixes
Windows 95's crashes at their cause, makes the status LED a steady green with a blue disk light
while the PC runs, and makes the log quieter (`BUILD-NOTES.md` 19 and 20). Then v3.9 takes in the
two reviews of v3.8 (`REVIEW-v38-emulator-and-windows.md`, `REVIEW-v38-board-safety-and-features.md`,
`REVIEW-RESOLUTION.md` round 5): the flash scripts after a write cut short, three log lines that went
missing, and the notes (`BUILD-NOTES.md` 21). Now v4.0, 2026-10-23: v3.9 merged with tiny386's
master of 2026-10-22 (`research\UPSTREAM-MERGE-v4.md`), and Windows 95 at the panel's own size,
800x480 on the Korvo-1 (800x600 on the DOS board), in 256 colours, High Color or True Color, through
a free VBE display driver for this emulated VGA (VMDisp9x), with VBE pictures drawn again only where
the guest wrote and 800x480 modes added to the VGA BIOS (`BUILD-NOTES.md` 22,
`research\NATIVE-VBE-DESIGN.md`). Then v4.1, the same day, takes in the two reviews of v4.0
(`REVIEW-v40-emulator-vbe-merge.md`, `REVIEW-v40-board-and-package.md`, `REVIEW-RESOLUTION.md` round
6, `BUILD-NOTES.md` 23): mode 13h's stores no longer pay for the new drawing's notes, `vbe_blocks = 0`
now takes them away entirely, a guest switching chain 4 under VBE is drawn right, the comments on
the memory accessors are corrected, and a DOS board ini for Windows at 800x600. Then v4.2, the same
day, answers Plat's report on v4.1 at 800x480 ("flickering a LOT"): the flicker is the display
driver's own mouse pointer, which it rubs out and draws again around everything Windows draws, so
the cure is one byte of the driver, which hands the pointer to Windows itself; `qemumini-pointer\`
has a script that makes the change and the steps to make it on the board with no card out
(`BUILD-NOTES.md` 24 and 25, `research\FLICKER-REPRO.md`, `research\FLICKER-AUDIT.md`). tiny386 itself
now makes a clear of the screen with no black on the panel before the picture after it, and
Ctrl+Alt+F11 changes `vbe_blocks` while the PC runs. The app calls itself `v4.2`. A review of v4.2
the same night (`REVIEW-v42.md`) found nothing that needed the firmware changed: v4.2's images
stay, and its points are taken into the evidence and the docs (`BUILD-NOTES.md` 26: the harness now
sees v4.2's clears, and measures no black between two pictures; the install from the changed floppy
was run; a grey text screen at Windows' restart from 16 colours, older than v4.2, was newly seen).
Now v4.3, 2026-10-24: v4.2 with the information port at 0F1F8h that T386MON
(github.com/platima/T386Mon, a monitor for Windows 95 on tiny386: fps, the guest's processor load,
drawing tests) reads, merged
from the branch it was made on; nothing else changes for the guest (`BUILD-NOTES.md` 27,
`TEST-PLAN.md` 15). The app calls itself `v4.3`. A review of v4.3 the same day (`REVIEW-v43.md`)
found nothing that needed the firmware changed: v4.3's images stay, and its points are taken into
the docs (`BUILD-NOTES.md` 28: one count in the merge's notes put right, T386MON's `PROTOCOL.md`
now says the port is in v4.3 and later, and the harness's own pictures in the port's VBE counts
on the PC explained). Now v4.4, 2026-10-24, the first release made on this branch: from Plat's
T386MON log of v4.3 and his notes, Ctrl+Alt+F11 also saves `vbe_blocks` in the ini, the grey text
screen at Windows' restart from 16 colours is gone, a VBE row is converted only where it was
written, and two cures for tearing are there to try, both off by default: pictures at the
guest's quiet moments (`pace_quiet`, Ctrl+Alt+F10) and a second frame buffer on the Korvo-1
(`double_buffer`, Ctrl+Alt+F9), whose keys save their settings too (`BUILD-NOTES.md` 29). And the
guest's hard disk can be a partition of the card itself, read and written raw (`hda = sd:3`):
Windows 95's own FAT16 volume on p3 of a new card that `newcard\newcard.sh` lays out from the old
card's Linux root and `win95.img`, which a PC or Linux can check and copy files to while tiny386 is
off, and through which nothing else on the card can be written (`BUILD-NOTES.md` 30,
`newcard\README.md`, `TEST-PLAN.md` 16). The app called itself `v4.4`. Then v4.5, the same day,
takes in the two reviews of v4.4 (`REVIEW-v44-display.md`, `REVIEW-v44-storage-and-safety.md`,
`REVIEW-RESOLUTION.md` round 11, `BUILD-NOTES.md` 31): the ini is saved without renaming anything,
so a cut in power can never leave two names on one chain of the card's clusters; Ctrl+Alt+F9 on the
speed screen no longer leaves the console's text round a picture narrower than the panel; the speed
screen counts the double buffer's swaps; `sd:3` refuses the old card's WIN9X partition, the one
holding `win95.img`; `newcard.sh` gives p3 an LBA type when it lies past what CHS reaches, and
`export-root.sh` finds the root by its device number; and the docs say what the new card's
procedure really adds to the old card. The app called itself `v4.5`. Now v4.6, 2026-10-25, from
Plat's run of v4.5 on his board, where all he tried worked but double buffering, which crashed at
boot, and pacing, which did not help (`BUILD-NOTES.md` 32, `TEST-PLAN.md` 17): with
`double_buffer = 1` both frame buffers are made with the panel at power-on, after one restart of
tiny386 itself at each boot, and the panel is never started again; pacing stays off by default;
and upstream's fix that moves the devices' state 32 bytes up, with three checks of our own, keeps
the guest's stores and DMA out of that state. Three reviews of v4.6 before its release
(`REVIEW-v46-memory-safety.md`, `REVIEW-v46-display-boot.md`, `REVIEW-v46-package-flash.md`,
`REVIEW-RESOLUTION.md` round 12, `BUILD-NOTES.md` 32.14) changed it: no restart after a crash, a
watchdog or a power dip, so that the last run's report stays on the panel; one more restart, with
one frame buffer, when the panel will not start after the first; and the crash screen's report
into the log once. The app called itself `v4.6`. Now v5.0, 2026-10-27, for Plat's word that v4.6
works on his board but is too slow (his T386MON log at 800x480 in 16 bits: the guest busy all the
time, at 1.5 to 1.7 million instructions a second): a fast interpreter in front of tiny386's own,
Yejun Yang's from his tab5-pc, made to step exactly as tiny386 does (`i386_fast.h`,
`FAST-CPU.md`, `THIRD_PARTY.md`), and the CPU's own path for loads and stores into the VBE linear
frame buffer, with the drawing's marks kept, both put together and checked against v4.6 in
lockstep through Windows 95 (`BUILD-NOTES.md` 33, `TEST-PLAN.md` 18). `fast_cpu = 0` under `[esp]`
in the ini runs tiny386's own interpreter alone. The app calls itself `v5.0`. Every release is a
tag on this branch, `s31-v3` and `s31-v3.1` to `s31-v4.6`, and `s31-v5.0` once it is released
(`README.md` beside this file).

This file was the README of the v4.3 package; on 2026-10-24 it moved to the `esp32-s31` branch of
github.com/platima/tiny386 with the notes, scripts and evidence around it. The commit hashes named
here and in the other notes are the scratch repository's: `old-to-new.tsv` gives each one's hash on
this branch, and an old tag `vN` is `s31-vN` here. The branch's own history from `s31-v4.4` on was
rewritten on 2026-10-25 (`BUILD-NOTES.md` 32.6); the notes name the new hashes.

**Almost nothing here has been seen on a board yet.** Plat has written v3.6 to his Korvo-1: FreeDOS
and BENCH ran (2.231 million instructions a second), the status LED was far too bright (v3.7 answers
that) and Windows 95 failed every time before the desktop [reported: Plat, 2026-10-21 and 22], which
v3.8 answers: on the PC its cause was found and fixed, and Windows 95 reached the desktop in every
run (`BUILD-NOTES.md` 19, `research\WIN95-FIX.md`); v3.9's own runs give the same desktops at the
same virtual second (`BUILD-NOTES.md` 21). Plat had Windows 95 at its desktop on v3.9 on the board
[reported: 2026-10-23], and then on v4.1 at 800x480, where the mouse pointer and anything moving
flickered [reported: 2026-10-23], which v4.2 answers. Nothing of v4.2 or v4.3 had run on a board
when v4.3 was released; Plat has since run v4.3 on his Korvo-1 with T386MON [reported: 2026-10-24].
Plat ran v4.5 on 2026-10-25: all he tried worked, the new card too, but `double_buffer = 1`, which
crashed at boot, and pacing, which did not help [reported]; v4.6 answers both. Plat then ran v4.6:
it works, and it is too slow [reported], which v5.0 answers. Nothing of v5.0 has run on a board
yet.
Everything was built and checked on the PC: host tests, builds from nothing giving the same bytes
twice, the flash scripts' whole flow with a stand-in esptool, the Windows 95 runs, the driver's
install through Windows' own dialogs (since the review of v4.2 also from the changed floppy), the
pointer change typed in MS-DOS mode, and (v4.3) T386MON in Windows 95 reading the port. The record
of each board run is Plat's, with `TEST-PLAN.md`; its section 16 (v4.4 and v4.5) is the keys'
saves, the two cures for tearing, and the new card with Windows on p3, section 17 (v4.6) is
double buffering made at power-on, and section 18 (v5.0) is the fast interpreter, on and off.
From v3.6 to v4.6
(the dual boot already in), `flash-dualboot.bat COM10 app` is all v5.0 needs (`FLASH.txt` 3), with the card's
two inis copied again if they are older than v3.8's, and for section 13 three files more on the card
(`card\tiny386\`: `vgabios.bin`, `win95-800x480.ini`, `vbe-reg.img`); v4.2 and v4.3 changed no card
file, and v4.4 adds commented lines to the Windows inis and `win95-sd.ini` for the new card (v4.5:
two comment lines more in `win95-sd.ini`, which goes on the new card only; v4.6 changed no card
file; v5.0 adds `;fast_cpu = 1`, commented out, with its comment, to the four Korvo-1 inis, which
need not be copied: without the line the fast interpreter is on). To have tiny386 start with no key
held and Linux on VOL+ instead, one more small write, the partition table alone:
`flash-dualboot.bat COM10 table tiny386` (`FLASH.txt` 2; `table linux` turns it back). v4.6's
double buffering needs that table: its one restart at each boot would start Linux under the other.


## Start here

| Read | For |
|-|-|
| `TEST-PLAN.md` | first, its top section: every risk to your Linux install, plainly (power Linux off first; the scripts copy the flash before writing). Then the first boot on the Korvo-1, step by step: what to flash, what to see on the panel and on the status LED, what to photograph. Section 8 is Windows 95: a fresh copy of the image, what to photograph at the desktop or after a failure, safe mode and a logged start. Section 13 (v4.0) is Windows at 800x480: the driver to fetch, its install through Windows' dialogs, the registry file, what to photograph. Section 14 (v4.2) is the mouse pointer at 800x480: what it looks like now, the one-byte change to the driver, what should change, and Ctrl+Alt+F11. Section 15 (v4.3) is T386MON on the board: its tests at 640x480 in 16 colours and at 800x480 in 16 bits, what to photograph, and Ctrl+Alt+F11 seen in it. Section 16 (v4.4 and v4.5) is the keys' saves, the two cures for tearing, and the new card with Windows from its partition 3. Section 17 (v4.6) is double buffering made at power-on: the restart, the lines to expect, what to do if it goes another way, the memory lines, and Windows as before. Section 18 (v5.0) is the fast interpreter: the boot console's `CPU:` line, BENCH and T386MON at 800x480 with `fast_cpu = 1` and `0`, a restart and MS-DOS mode, what to photograph |
| `FAST-CPU.md` | v5.0: the fast interpreter from Yejun Yang's tab5-pc, how it is kept exactly tiny386 (the instruction count, the steps, the fusion, the TLB), the switch, where it runs on the S31 and what internal RAM it takes, its tests and its counts |
| `THIRD_PARTY.md` | v5.0: the code from other projects, tab5-pc's fast interpreter, with its MIT notice and what was changed |
| `newcard\README.md` | v4.4: a new card for the dual boot with Windows' own volume on p3, step by step: Linux's root off the old card (`export-root.sh` on the board's Linux), `newcard.sh` in WSL, writing the image, what to expect; the old card keeps its table, its Linux root and its `win95.img`, and only gets files added to its two FAT drives |
| T386MON's `README.md` (github.com/platima/T386Mon) | T386MON, the monitor for Windows 95 on tiny386 (`T386MON.EXE` on the floppy `t386mon.img`): what it shows, its tests, its log, and which firmware has the information port it reads (v4.3 and later). Its `PROTOCOL.md` is the port itself |
| `qemumini-pointer\README.txt` | v4.2: why the pointer flickers at 800x480 with VMDisp9x's QEMU driver, and three ways to give the pointer to Windows' DIB engine: six lines in MS-DOS mode on the board, `qemumini-pointer.bat` on the card's Windows image, or a changed copy of the driver's floppy |
| `research\FLICKER-REPRO.md`, `research\FLICKER-AUDIT.md` | the two investigations of the flicker on the PC: the reproduction, frame by frame, and the audit of the drawing and of the driver's source; `research\FLICKER-PLAT-NOTES.md` is Plat's reports as they came |
| `STATUS-LED.md` | the status LED's colours and blinks, on both boards; since v3.7 its brightness (section 7: `led_brightness` under `[esp]` in the ini, 0 to 255, 9 by default); since v3.8 the disk light (section 8: a steady green while the PC runs, blue for each disk access) |
| `FLASH.txt` | writing each image set, and every way back to Linux; section 3 is the app alone, all that v3.6 to v4.1 to v4.2 needs |
| `research\NATIVE-VBE-DESIGN.md` | v4.0's design, written before it was built: which Windows 9x display driver and why, what the drawing cost in v3.9 and what it should cost, the VGA BIOS, the board's side, the PC runs of the driver; `BUILD-NOTES.md` 22 is what was built from it |
| `research\WIN95-FIX.md` | why Windows 95 crashed (the guest's timer after a wait for the card) and what v3.8 changed, with the PC runs |
| `LINE-OUT.md` | the DOS board's 3.5 mm line out and, from rev A2c, its speakers: the level (`audio_shift`, now 2 by default), mute, what to try |
| `REVIEW-RESOLUTION.md` | every review round's findings, one line each: fixed, documented, or deferred and why; round 3 is the review of v3.3, round 4 the review of v3.5, round 5 the two reviews of v3.8, round 6 the two reviews of v4.0, round 7 the flicker on v4.1 (the brief's suspects and the two investigations' recommendations), round 8 the review of v4.2 (`REVIEW-v42.md`), round 9 (v4.3) the review of the information port (T386MON's own review, not in this tree) as it stands in the merged firmware, round 10 the review of v4.3 (`REVIEW-v43.md`), round 11 the two reviews of v4.4 (`REVIEW-v44-display.md`, `REVIEW-v44-storage-and-safety.md`), round 12 the three reviews of v4.6 (`REVIEW-v46-memory-safety.md`, `REVIEW-v46-display-boot.md`, `REVIEW-v46-package-flash.md`) |
| `BUILD-NOTES.md` | how v3 was put together, built and checked; sizes; what is still unknown; section 12 for the second round, section 14 for v3.3, section 15 for v3.4, section 16 for v3.5, section 17 for v3.6, section 18 for v3.7, sections 19 and 20 for v3.8 (19 the Windows 95 fix, 20 the disk light, the log and the images), section 21 for v3.9, section 22 for v4.0, section 23 for v4.1 (23.9: what each colour depth costs in guest memory and speed), section 24 for the flicker's investigations, section 25 for v4.2 (25.13: what the mouse pointer costs Windows, either way), section 26 for the review of v4.2 (no firmware change), section 27 for v4.3 (the information port merged), section 28 for the review of v4.3 (no firmware change), section 29 for v4.4's display work and section 30 for its storage work and the release (the card partition as the guest's disk, the new card, the package), section 31 for v4.5 (the two reviews of v4.4), section 32 for v4.6 (double buffering made at power-on, the guest kept out of the devices' state, the branch's history rewritten, the three reviews of v4.6), section 33 for v5.0 (the fast interpreter from tab5-pc and the frame buffer's own path, put together and checked against v4.6 in lockstep, what the counts say for the board) |


## What is in this folder, and what is in the releases

On this branch `s31\` holds text only. The images, the card's binaries, the patches and the VGA
BIOS's source are assets of the release for each `s31-v*` tag, as `README.md` beside this file says.

| Path | What |
|-|-|
| `images\` (release) | not in the tree. `korvo1-dualboot\`: the Korvo-1 beside Linux: dual-boot bootloader (0x2000), table (0x8000), `tiny386.bin` (0xE20000). `korvo1-dualboot-tiny386-default\`: the same set with tiny386 the default at reset and Linux on VOL+: the same bootloader and `tiny386.bin`, and a table with the two app partitions' subtypes swapped (SHA-256 `5b90d416...`, `dualboot\DUALBOOT.md` 13); since v3.8 `build.sh` makes this folder too, its `tiny386.bin` the same file as `korvo1-dualboot\`'s, which the scripts write for both tables, so it has no `tiny386.elf`. `korvo1-standalone\`: the Korvo-1 with tiny386 only: tiny386's own bootloader, table, `tiny386.bin` (0x10000). `dosboard\`: the DOS board, the same layout as the standalone set. Each has `flash_args` (the offsets) and `SHA256SUMS`, and each but the tiny386-default one has `tiny386.elf`, which turns the addresses on a crash screen into function names |
| `card\tiny386\` | for the card's tiny386 folder. Here: `tiny386.ini` (FreeDOS, the default), `win95-14m.ini` (Windows 95, 14 MB, the standard 640x480 VGA driver) and, since v4.0, `win95-800x480.ini` (Windows at the panel's size, `vga_mem_size = 768K`, `mem_size = max`, the install's two floppy lines commented out). Since v3.7 both of the first two carry `;led_brightness = 9` under `[esp]`, commented out, and since v3.8 v3.8's five new settings the same way (`uart_log`, `verbose_log`, `timer_catchup`, `fast_seek`, `memtest`), each at what it is without its line; a card with older inis works as before. Since v4.1 the two Windows inis say in a comment that an image with the driver set to 800x480 is started with `win95-800x480.ini` only (`TEST-PLAN.md` 13c). In the release: `bios.bin`, `vgabios.bin` (v4.0's, with the 800x480 VBE modes added; v3.9's still works) and `vbe-reg.img` (a floppy image with `VBE480.REG` and `VBE600.REG`, `TEST-PLAN.md` 13). Since v4.4 `win95-sd.ini`: Windows from the new card's partition 3 (`hda = sd:3`, `newcard\README.md`); and the Windows inis carry v4.4's new settings commented out (v4.5: `win95-sd.ini` says it is for the new card only). The display driver itself is in neither: `TEST-PLAN.md` 13a says where to fetch it |
| `newcard\` | v4.4, v4.5: `newcard.sh` (WSL or any Linux: the new card's image, p1 `KORVO_SD`, p3 the Windows volume out of `win95.img`, p2 Linux's root, with its checks), `export-root.sh` (the board's Linux: Linux's root and the card's first sector off the old card) and `README.md`, the steps |
| `card-dosboard\tiny386\` | `dosboard.ini`, the DOS board's own settings, every one with its default; since v4.1 `win95-800x600.ini`, Windows at the DOS board's 800x600 with VMDisp9x (`vga_mem_size = 1M`; 960K, 1408K for 24 bits and 1920K for 32 named; `TEST-PLAN.md` 13e) |
| `flash-dualboot.bat`, `flash-standalone.bat`, `back-to-linux.bat`, `flash-paths.cmd` | the Windows scripts; each needs the port (`COM10`), checks its files, reads the board first (MAC, partition table, and over Linux the bootloader) and stops if it is not one the script was made for, copies the whole flash into `backup\` before its first write over Linux, prints the esptool commands, and writes only after Y. `flash-dualboot.bat COM10 table tiny386` (or `table linux`) chooses which system starts with no key held, writing only the table at 0x8000; all three scripts know both dual-boot tables. Since v3.9 a first install cut short over Linux's table is finished by running the same script again, with the backup taken before it kept (none is ever taken of a cut state); `app` and `table` go on only with the dual-boot bootloader at 0x2000; and the DOS board's image is refused for the Korvo-1 (`dualboot\DUALBOOT.md` 14). They expect the image sets in `images\` beside them, as the release has them |
| `backup\` | not here yet: made by the first run of `flash-dualboot.bat` (or `flash-standalone.bat`) over Linux, the flash as it was. Keep it; `back-to-linux.bat` puts it back |
| `qemumini-pointer\` | v4.2: `README.txt` (why and how), `qemumini-pointer.bat` and `qemumini-pointer.ps1`, which change one byte of VMDisp9x 1.2025.0.119's `QEMUMINI.DRV` inside the driver's floppy image (a changed copy) or a Windows 95 disk image (in place, or a copy), after checking it is that file; `-Check`, `-Undo`. No byte of the driver is in the package |
| `tiny386-idf-v4.patch` (release) | since v4.0 the build's patch: everything against tiny386's master of 2026-10-22, 4ce2e06 (github.com/hchunhui/tiny386): the v5.0 firmware and its host tests (`git diff 4ce2e06 s31-v5.0 -- . ':(exclude)s31'` on this branch; up to v4.3 `git diff upstream-20261022 v4.3` in the scratch repository, the same bytes as `git diff 4ce2e06 s31-v4.3` here), which `build.sh` applies |
| `tiny386-idf-v3.patch` (release) | everything against the release tiny386 9ed786b, as every patch until v3.9 was: `git diff 9ed786b s31-v5.0 -- . ':(exclude)s31'`, the same v5.0 tree, for comparing with the earlier patches. The Windows 95 fix to the shared emulator core alone, for the Linux route or upstream (v3.8's and v3.9's `win95-core-*.patch`), is not in this tree; it no longer applies to upstream's master (`research\UPSTREAM-MERGE-v4.md`) |
| `build.sh`, `dependencies.lock` | the release build from nothing, in WSL; the ESP-IDF component versions. Since v3.4 it also checks the status LED's crash bit-bang in each built ELF (`BUILD-NOTES.md` 15); since v3.8 it makes the fourth image set, tiny386 the default (`BUILD-NOTES.md` 20); since v4.0 it clones tiny386 at 4ce2e06 and applies `tiny386-idf-v4.patch` (v4.1, v4.2 and v4.3 changed its first comment only). It wants the patch beside it and, in `dualboot\`, the dual-boot `bootloader.bin`, `partition-table.bin` and `partition-table-tiny386-default.bin`, all from the release. `README.md` beside this file says how to build from the branch itself |
| `bios\` (release) | not in this tree: v4.0's VGA BIOS from source (SeaBIOS is LGPL 3: these go with `vgabios.bin`): the source tree as built, the patches (the Linux route's and `seabios-korvo-800x480.patch`), `tools\build-bios.sh` and how it differs from the Linux route's, `README.txt` (since v4.1 it also says which kit makes the card's `bios.bin` exactly). The SeaBIOS changes are to go to github.com/platima/seabios |
| `checks\` | the evidence behind `BUILD-NOTES.md` and `REVIEW-RESOLUTION.md` (the second round's files end in `-v3.2`, v3.3's in `-v3.3`, v3.4's in `-v3.4`, v3.5's in `-v3.5`, v3.6's in `-v3.6`, v3.7's in `-v3.7`, v3.8's in `-v3.8`, v3.9's in `-v3.9`, v4.0's in `-v4.0`, v4.1's in `-v4.1`, v4.2's in `-v4.2`, among them `flicker-harness-v4.2.txt`, `pointer-cost-v4.2.txt` and `pointer-script-v4.2.txt`, and the review of v4.2's round in `-v4.2-review`, among them `flicker-harness-v4.2-review.txt` and `route-c-v4.2.txt`; v4.3's in `-v4.3`, among them `merge-v4.3.txt` (the merge and its two conflicts), `flicker-harness-v4.3.txt` and `t386mon-v4.3.txt`; `checks\t386mon-v4.3\` has the T386MON runs' scripts and the harness's change, `checks\win95-v4.3\` the standard Windows runs' scripts, and `checks\flicker\` v4.3's run list and scripts, `build-v4.3.diff` the flicker harness taught `perfport.c`; and the review of v4.3's round in `-v4.3-review`, among them `findings-v4.3-review.txt`, each finding checked); `checks\mode13x\` v4.2's mode 13h test program (`MODE13X.COM` and its source); `checks\flicker\` the flicker harness's changes for v4.2 (`headless-v4.2.diff`; since the review `harness-v4.2-review.diff`, the count of black between two pictures) and the run lists; `checks\vbe480\` the DOS VBE test program (`BVBE480.COM` and its source), its checker (v4.1's mends a log line the harness split), the harness's changes (`headless-v4.0.diff`, and `headless-v4.1.diff`, the whole of the harness v4.1's runs used) and their results; `checks\bootswap\` for tiny386 the default (the table, the scripts' flow, the flash simulation). The .bat and .cmd files changed for it on 2026-10-22, after v3.7 was built; the earlier ones, which every `previous-v3.x\SHA256SUMS` up to v3.7's names and which the v3.2 to v3.7 checks ran, are kept byte for byte in `checks\bootswap\scripts-before\` (`sha256sum -c` of those four lines passes there). On this branch `checks\` is text only. Left out of the tree: the two DOS test programs' binaries (`mode13x\MODE13X.COM`, `vbe480\BVBE480.COM`; their sources are here), and the records of runs that hold Windows' own output or code (`win95-pc-runs-*.txt`, `win95-driver-install-*.txt`, `flicker-harness-*.txt`, `pointer-script-v4.2.txt`, `pointer-cost-v4.2.txt`, `route-c-v4.2.txt`, `t386mon-v4.3.txt`). In `merge-v4.3.txt` an email address is taken out |
| `research\` | v3.8's investigations of Windows 95 (`WIN95-BAREMETAL-AUDIT.md`) and the fix (`WIN95-FIX.md`); v4.0's merge with upstream (`UPSTREAM-MERGE-v4.md`) and its display design (`NATIVE-VBE-DESIGN.md`); the flicker on v4.1 (`FLICKER-REPRO.md`, `FLICKER-AUDIT.md`, and Plat's reports, `FLICKER-PLAT-NOTES.md`). Only the notes are on this branch: `WIN95-PC-REPRO.md` and `WIN95-CRASH-DECODE.md` are not, as they hold Windows' own code and data from Plat's card, and neither are `WIN95-FIX-runs.txt`, the two core patches and `flicker-repro\` (the flicker harness and its runs) |
| Earlier releases | the package's `previous-v3.2\` to `previous-v4.6\` folders are not here: each release's source is its tag, `s31-v3` and `s31-v3.1` to `s31-v4.6`, and `BUILD-NOTES.md` and `REVIEW-RESOLUTION.md` here cover every one of them |
| `STATUS-LED.md`, `LINE-OUT.md` | the status LED (v3.4; v3.5 changed two pin numbers in it, v3.6 one line of its yellow 2 row, v3.7 added section 7, the brightness, v3.8 section 8, the disk light), and the DOS board's line out and speakers (v3.5; v3.6 rewrote its board point on R703) |
| `REVIEW-*.md` | the four reviews, as their authors left them: the second round first, the first round word for word as an appendix; `REVIEW-v32.md`, the review of v3.3 (named for the task that asked for it); `REVIEW-v35.md`, the review of v3.5; and the two reviews of v3.8, `REVIEW-v38-emulator-and-windows.md` and `REVIEW-v38-board-safety-and-features.md`; and the two reviews of v4.0, `REVIEW-v40-emulator-vbe-merge.md` and `REVIEW-v40-board-and-package.md`; `REVIEW-v42.md`, the review of v4.2; `REVIEW-v43.md`, the review of v4.3; and the two reviews of v4.4, `REVIEW-v44-display.md` and `REVIEW-v44-storage-and-safety.md`; and the three reviews of v4.6, `REVIEW-v46-memory-safety.md`, `REVIEW-v46-display-boot.md` and `REVIEW-v46-package-flash.md` |
| `NOTES-core1.md`, `NOTES-core2.md`, `core1\`, `core2\`, `dosboard\` | not in this tree: the work records of the firmware's core (panel, console, crash screen, keyboard and mouse, card and ini, boot menu, speed readout) and of the DOS board's drivers (MS7210 HDMI, CH9350L keyboard and mouse, 800x600, 48 kHz sound) |
| `dualboot\` | the dual boot: `DUALBOOT.md`, the two tables as CSV, the scripts that built and checked them, their checks and the build's text records. The bootloader and the two tables as built, which `build.sh` and the scripts check by SHA-256, are in the release. Linux's release images, which the package kept in `dualboot\linux-v2026.09.20\`, are in neither: their corresponding source is not to hand |
| `old-to-new.tsv` | every commit of the scratch repository that moved here: its old hash, its new one, the scratch tags at it, its subject |


## In short

What the code does [verified: source, builds and host tests, `BUILD-NOTES.md`]; whether the
board does it too is [unknown] until the first boot.

* On the Korvo-1: Linux stays the default; hold VOL+ (the bottom key of the ADC ladder) through
  RESET for tiny386. Or the other way round: `flash-dualboot.bat COM10 table tiny386` swaps the two
  roles in the partition table, so that tiny386 starts with no key held and VOL+ gives Linux
  (`FLASH.txt` 2). Installing it is, with Linux powered off: a copy of the whole flash into
  `backup\`, tiny386 at 0xE20000, then two small writes that leave the rest of Linux alone
  (`flash-dualboot.bat COM10`); `back-to-linux.bat COM10` puts those two pieces back from the
  copy, byte for byte.
* The panel shows a boot console from the first second: what was found, what is wrong in plain
  words, and a red crash report that stays if the firmware crashes.
* The status LED (GPIO37, the Korvo-1's RGB LED, D201 on the DOS board) says the same without a
  screen: a colour for each stage of the boot, red blinks for the card and its files, yellow
  blinks for the board, red and white by turns for a crash (kept going from the panic handler),
  and after a crash, a watchdog or a brownout the last run's code first; since v3.7 dim by default,
  its brightness set in the ini; since v3.8 a PC's front panel while the PC runs, a steady green
  with a blue flash for each disk access (`STATUS-LED.md`).
* With more than one ini in the card's tiny386 folder a boot menu comes up; with no key the last
  choice (or `tiny386.ini`) starts after 6 s.
* USB keyboard and mouse, through a hub or a wireless receiver, with key repeat and the Caps Lock
  light; the mouse reaches the guest at the rate its driver asks for, in whole packets, and a
  click is kept even when the emulator is held up. Ctrl+Alt+F12 shows the speed; BENCH.COM's
  results print as the Linux route prints them, to compare with its 0.358 million instructions a
  second [reported: Plat, 2026-10-21].
* Every FAT partition of the card is mounted (FAT16 or FAT32), so the Windows 95 image can sit on
  the WIN9X partition (`/sdcard3`). The guest gets the most memory that fits: 14 MB on the Korvo-1
  [inferred: sizes, `NOTES-core2.md` 3.4]. Nothing the guest does can make a disk or floppy image
  grow on the card.
* The DOS board build adds the HDMI transmitter, the CH9350L for keyboard and mouse, 800x600 and
  sound at 48 kHz, and stops at once on any board that is not a DOS board. From rev A2 the same
  sound also goes to the PCM5102A's line out, muted until the I2S clocks run and again in a crash
  (`LINE-OUT.md`).
* v3.5 follows the DOS board's rev A2c: I2S on GPIO51 to GPIO53, the MS7210's reset on GPIO49 and
  interrupt on GPIO54, the CH9350L's reset on GPIO56, so that GPIO2 to GPIO7 (the low-power
  core's pins), GPIO57 and GPIO60 are left free on the J202 header; the firmware configures none
  of them, and a host test checks the pin map against the board's netlist. The speaker amplifier
  shares GPIO48 with the DAC's mute, now held low from the board check until the I2S clocks run.
  The sound is mixed in 32 bits and held to 16 bits once, after the level, and the DOS board's
  default level is 1/4 (`audio_shift = 2`), 12 dB louder than v3.4 and the loudest at which
  nothing can clip; the Korvo-1 image has no sound path and is unchanged but for its version
  (`BUILD-NOTES.md` 16).
* v3.6, after the review of v3.5: until the MS7210 has answered, the DOS board image drives no pin
  but the I2C bus and what both boards share, the console's UART0 and the status LED on GPIO37
  (GPIO46 to GPIO57 are camera pins on a Korvo-1). If nothing answers at all, it reads GPIO46 for
  about 0.1 ms with the pin's weak pull-up on (R302 holds it low on a DOS board) and leaves it an
  input with no pull; only if it read low is RSTN (GPIO49) pulsed and the MS7210 looked for again. So a Korvo-1 with nothing on its bus no longer has GPIO49 driven, and
  something on J202's I2C pins at one of a Korvo-1's addresses no longer stops a DOS board. A new
  host test runs the board check against pretend Korvo-1s and DOS boards. The sound mix also
  refuses a block of no length, and the review's board point (R703 as a pull-down, so that the
  speakers stay off through every reset) is in `LINE-OUT.md` for the board step
  (`REVIEW-RESOLUTION.md` round 4, `BUILD-NOTES.md` 17).
* v3.7, for Plat's report that v3.6's status LED is far too bright: the colours are the same, but by
  default at about an eighth of v3.6's light (red 9 of 255, where v3.6 sent 70; white 5, 5, 5;
  yellow 8, 5, 0), with no colour turning into another. `led_brightness = 0` to `255` under `[esp]`
  in the ini sets it from the moment its line is read: 0 is dark, a crash included, 70 is v3.6's
  brightness, 255 full. The crash path uses the same level and the same code, from internal RAM.
  Nothing else changed, on either board (`STATUS-LED.md` 7, `BUILD-NOTES.md` 18).
* v3.8, for Plat's Windows 95 runs on v3.6, which all failed before the desktop: on the PC, with
  the same image, the cause was the guest's timer, which after any long wait for the card fired
  every tick it had missed back to back until Windows' stack ran out; the long waits came from the
  card's FAT being read on every seek back. Missed ticks are now dropped as a real PC drops them,
  the image's place on the card is mapped once at start, a card error is got over by opening the
  image again, and the emulator now handles a fault during an interrupt as a 386 does. Windows 95
  reached the desktop in all 51 PC runs that model the board's card; without the timer and card
  fixes, in 6 of the same 51. Ctrl+Alt+F12 and the crash screen show the card's waits and the timer's dropped ticks,
  and `[esp] memtest = 3` tests the PSRAM once (`BUILD-NOTES.md` 19, `research\WIN95-FIX.md`).
* Also v3.8, for Plat: the status LED is a steady green while the PC runs, with a short blue flash
  for each disk access, like a hard disk light; the error codes, the crash pattern and the stall
  check are as before (`STATUS-LED.md` 8). And the log is quieter: the UART is off while the PC runs
  unless `[esp] uart_log = 1` (a crash report still goes there), and then goes through a queue the
  emulator never waits for; of each kind of line the emulator repeats, the first 5 in 5 s are shown
  and the rest counted in one line, unless `[esp] verbose_log = 1`. On the PC, a 60 s storm of
  100,000 such lines comes to 144 lines, and a Windows 95 boot's 180 lines to 107; the emulator
  never waits for the UART, where v3.7 waited 87 us for each byte (`BUILD-NOTES.md` 20).
* One thing v3.8 changes for every guest: after the emulator has been held up (a slow card), the
  guest's clock, DOS's time of day and Windows' tick count, is behind real time by the length of
  the hold, where before the burst of ticks caught it up. A DOS program that times itself with the
  BIOS tick across a long disk access reads less time than passed. BENCH.COM is not affected: it
  masks the timer's interrupt and is timed by the emulator (`research\WIN95-FIX.md`).
* v3.9, after the two reviews of v3.8 (`REVIEW-RESOLUTION.md` round 5, `BUILD-NOTES.md` 21): a
  first install over Linux that is cut short in its second write is finished by running the
  script again, keeping the backup taken before it, where v3.8 stopped and advised renaming the
  backup, which led to a backup of the broken state; `app` and `table` stop if the bootloader is
  not the dual-boot one; the DOS board's image is refused for the Korvo-1. On the crash screen,
  after an emulator abort, the line that names the cause now also reaches the UART and comes after
  the counts of lines held back; after any other crash, the lines still held back are counted on
  the crash screen; held error lines are counted in red; and the status LED's stall line (yellow
  5) is in the log on the panel. The emulator behaves as v3.8 in every PC run: Windows 95 reaches
  the desktop at the same virtual second.
* v4.0, for Plat's request of 2026-10-23 ("ideally if we could get the resolution of the LCD itself,
  so it's not boxed to 4:3 ... 256 colour minimum, 16 bit preferred, 24 bit amazing"): Windows 95
  at 800x480 on the Korvo-1 and 800x600 on the DOS board, filling the screen, at 8, 16, 24 or 32
  bits a pixel, through VMDisp9x's driver for QEMU's standard VGA, the hardware tiny386 emulates
  (`TEST-PLAN.md` 13). The VGA BIOS stays SeaVGABIOS, with VBE modes 1A0h to 1A4h at 800x480 added.
  A VBE picture is now drawn again only in the rows the guest wrote into, each converted with no
  division (a still 800x480 picture was 4 to 9 million instructions a picture; now some thousands);
  the BIOS's bank window works at any video memory size; `[esp] vbe_blocks = 0` draws as before. On
  the PC the drawing's time at 800x480 fell 15 to 20 times, Windows reached the desktop at every
  depth, and the install through Windows' own dialogs worked; nothing else changed for the guest:
  the 27 standard Windows runs and BENCH give v4.0-base's results to the instruction
  (`BUILD-NOTES.md` 22). v4.0 also takes in tiny386's own fixes since its release (the timer's
  one-shot modes, which v3.9 stopped on; task switches; `research\UPSTREAM-MERGE-v4.md`).
* v4.1, after the two reviews of v4.0 (`REVIEW-RESOLUTION.md` round 6, `BUILD-NOTES.md` 23): a store
  into video memory is noted for the new drawing only where a picture reads the note, so DOS
  programs in mode 13h pay no memory fence at all, and `vbe_blocks = 0` leaves every store as
  before v4.0, which makes it a fair A/B on the board; a guest that switches chain 4 off and on under
  VBE has its next picture drawn whole; the accessors' comments say what the board's compiler
  really makes; `card-dosboard\tiny386\win95-800x600.ini` for Windows on the DOS board. And from
  the PC: an image with the driver in, started with `win95-14m.ini`'s 320K, runs at 640x480 for
  that start (it is back at 800x480 with `win95-800x480.ini`), so it is started with
  `win95-800x480.ini` only (`TEST-PLAN.md` 13c). Every PC run gives v4.0's results to the
  instruction and the picture.
* v4.2, for Plat's report on v4.1 at 800x480 ("flickering a LOT"; a still desktop fine, the
  pointer and anything moving not, at every VBE depth; the standard VGA driver's 16 colours fine;
  and slower): on the PC, VMDisp9x's QEMU driver as released draws the mouse pointer itself and
  rubs it out and draws it again around everything Windows draws, anywhere on the screen, about
  18 ms of the guest's time each at the board's speed, so many pictures catch it gone or half drawn
  and Windows spends most of its time on it while anything moves; tiny386 shows the screen exactly
  as the guest has it (v3.9's drawing and `vbe_blocks = 0` give the same pictures). One byte of the
  driver hands the pointer to Windows' own DIB engine: the pointer whole in every picture, typing,
  scrolling and window drawing as clean and as quick as with the standard VGA driver. A pointer
  moved without a break still costs Windows as much as before, or more (`BUILD-NOTES.md` 25.13).
  `qemumini-pointer\` makes the change (`TEST-PLAN.md` 14). In tiny386: a clear of the screen (a
  mode set, as Windows starts) no longer flashes black on the panel; Ctrl+Alt+F11 changes
  `vbe_blocks` at once; every PC run gives v4.1's results to the instruction and the picture
  (`BUILD-NOTES.md` 24, 25).
* v4.3, for Plat's monitor of Windows 95 on tiny386 (T386MON 1.2, github.com/platima/T386Mon: "a little test
  app ... that showed fps, CPU usage ... and all related performance metrics"): v4.2 with the
  information port at I/O 0F1F8h to 0F1FFh, merged from the branch it was made on, unchanged. A
  program in the PC reads the emulator's own figures there: the panel's frames a second, how much
  of the time the guest is halted, instructions a second, every picture's drawing time, the rows
  sent to the panel, the VBE drawing, the mode, memory on the board, the disk and the card, the
  timer, the keyboard and mouse, the firmware's version. v4.2's flicker fix stays as it was, and the
  port gives `vbe_blocks` as it is after a Ctrl+Alt+F11. Nothing else changes for the guest: every
  PC run of v4.2 gives the same results on v4.3, to the instruction and the picture, and T386MON in
  Windows 95 on v4.3 sees exactly what it saw on the branch (`BUILD-NOTES.md` 27, `TEST-PLAN.md`
  15). On v4.2 and earlier T386MON still runs, with Windows' own figures only.
* v5.0, for Plat's word that v4.6 is too slow: the fast interpreter from Yejun Yang's tab5-pc
  (`FAST-CPU.md`, `THIRD_PARTY.md`) does the common instructions itself and hands the rest to
  tiny386's own, one at a time; its loop is in internal RAM and tiny386's interpreter in flash.
  The emulated processor stores into the VBE frame buffer itself, with the drawing's marks, and a
  REP STOSD or REP MOVSD there is one fill or copy a page. `[esp] fast_cpu = 0` runs tiny386's own
  alone. On the PC a lockstep tester (`tests/lockstep`) finds no difference from v4.6, instruction
  for instruction, through Windows 95's boot, a restart, MS-DOS mode, BENCH, the frame buffer's test
  guest and T386MON's drawing tests, with the fast interpreter on and off; counted on the board's
  RV32 code, core 1's work a guest instruction falls to about half (`BUILD-NOTES.md` 33,
  `TEST-PLAN.md` 18). How fast on the board is [unknown] until Plat runs it.


## Building again

From this branch with `idf.py`, as `README.md` beside this file says: the Korvo-1 dual-boot, Korvo-1
standalone and DOS board builds of the v5.0 code give the v5.0 release build's `tiny386.bin` and
`tiny386.elf` files' bytes, and the standalone and DOS board builds their bootloaders and tables too
[verified: 2026-10-27, built from `git archive` of the branch's v5.0 code in WSL,
`BUILD-NOTES.md` 33.9; the same was checked for v4.6, v4.5, v4.4 and v4.3].

Or the release's way, in WSL, with ESP-IDF v6.1-dev-8142-g188e3e55bb at `~/esp/esp-idf`: put the
release's `tiny386-idf-v4.patch` beside `build.sh`, and its `bootloader.bin`, `partition-table.bin`
and `partition-table-tiny386-default.bin` in `dualboot\`, then

```
bash s31/build.sh ~/some-empty-folder
```

A few minutes on this PC (233 to 276 s in v5.0's four runs, with other work on the machine); the
images it makes are the bytes of the release, all four sets [verified: four runs in different
folders, `BUILD-NOTES.md` 33.9].
