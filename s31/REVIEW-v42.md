# Review of v4.2: the 800x480 flicker fix

Written 2026-10-23 as an independent review of v4.2 (tag `v4.2`, 6502063, branch `v42`, tree
f7beec4b), its package in this folder and the claims of `BUILD-NOTES.md` 24 and 25. The question: is
the cause proven, is the fix at the root, can anything still show a black or stale region, did the
drawing's cost or correctness slip anywhere, and is the package consistent.

Nothing was flashed, no serial or COM port was opened, nothing was committed anywhere. The scratch
tree, the fix agent's folders (`~/flicker/fix`) and the investigations' folders were only read. My
own work is in WSL `~/flicker/review` (section 11). `research\FLICKER-PLAT-NOTES.md` was read at the
start, before each decision and again at the end; its last entry is still the third report and the
request for the fix release (file dated 18:25), so nothing Plat has said since changes this review.
Every claim is marked [verified] (seen in a file or measured in a run named here), [inferred] or
[unknown].


## 0. The answer in one screen

* **The cause is proven on the PC, not yet on the board.** I read the driver's source at its
  release tag and its released binary, and reran the harness myself from my own exports of the tags.
  VMDisp9x 1.2025.0.119's `QEMUMINI.DRV` sets `mouse_vxd = TRUE` (the only store to it, file offset
  0x2064, `C7 06 B8 06 01 00`; five readers, all `cmp word [06B8h],0`), and its VxD's
  `FBHDA_access_rect()` ignores its rectangle, so the VxD rubs the pointer out and draws it again around
  every drawing Windows does [verified: source, binary disassembly]. My reruns give the fix agent's
  figures to the picture: as released 269 whole, 132 part and 105 no pointer while it sweeps empty
  desktop, 57, 61 and 58 per cent of pictures mid-update while typing, scrolling and moving a window;
  v4.1 and v4.2 the same to the instruction [verified: section 2]. It matches all of Plat's
  observations (still desktop fine, motion bad, every VBE depth and 640x480 through the driver, the
  standard VGA driver fine, slower). On the board it is [unknown] until `TEST-PLAN.md` 14b and 14d.
* **The fix is at the root for the flicker, and it is in the driver, not in tiny386.** With the byte
  at 0x2068 made 00 by the package's own `qemumini-pointer.bat` (run by me on a copy of the image the
  install leaves), the pointer is whole in 531 of 531 sweep pictures and the mid-update share falls to
  7, 4 and 3 per cent, as with the standard VGA driver [verified: sections 2 and 4]. I looked at the pictures:
  as released the pointer is missing, cut off at its middle or in fragments; changed, it is whole in
  every one [verified: `pics\`]. The v4.2 firmware on its own changes nothing in the flicker: every
  v4.2 run gives v4.1's pictures and `END` line [verified]. Plat has to make the driver change; the
  docs say so.
* **No black or stale region found.** A randomised differential test of `VGA_CLEAR_AROUND` against
  v4.1's `memset()` (mode sets, VBE writes including the X and Y offsets, CRT controller, attribute,
  sequencer and graphics registers, palette and DAC mask writes, blanking, `vbe_blocks` and
  `planar_fast` changed, video memory written, `full_update`) found no difference in 3.2 million steps
  and about 585,000 pictures on both boards, and catches planted faults (within 232 to 18,009 steps)
  [verified: section 5]. A DOS box session (windowed, full screen, 40 columns and back, the window
  again, full screen and back twice quickly, closed) on v4.2 and v4.1, with the driver as released and
  changed: every one of about 10,400 pictures a run equal to the reference [verified].
* **Cost and correctness did not slip.** The store paths in the built Korvo-1 ELF are v4.1's
  instruction for instruction (my own disassembly of the package's ELF against `previous-v4.1\`'s);
  host drawing time per picture within 3 per cent of v4.1 in the 5 pairs I timed; 39 of 39 host tests;
  `build.sh` from nothing gives the package's 23 image files byte for byte [verified].
* **The package is consistent**, with a few wording faults (sections 8 and 9): `SHA256SUMS` 168 of 168 OK and
  each image folder's list OK [verified]. The one real fault in the evidence: the harness cannot see a
  v4.2 clear at all, so "no clear over a picture in any v4.2 run" is true by construction, not a
  measurement (finding 1). The property itself holds by other evidence.
* **Nothing blocks giving v4.2 to Plat.** The findings are wording and evidence labels (findings 1 to
  4) and notes for Plat (5 to 10).


## 1. What Plat said, and what v4.2 has to answer

From `research\FLICKER-PLAT-NOTES.md` [reported]: v4.1 at 800x480 in 16 bits "flickering a LOT"; a
still desktop does not flicker; motion does, the pointer and anything moving, "like tearing, but it does
not look like normal frame tearing"; 256 colours and 640x480 through the same driver do it too; 16
colours through the standard VGA driver does not, "fairly smooth"; slower than before; and a request:
`vbe_blocks` changeable without pulling the card, ideally saved back to the ini.

v4.2's answer: the driver's own pointer (one byte to change), a clear with no black flash, and
Ctrl+Alt+F11 for `vbe_blocks` while the PC runs, not saved.


## 2. The harness, rerun

My own copy of the flicker harness (`~/flicker/review/h`, copied from `~/flicker/fix/h/harness`, the
fix agent's `@shot` fix and instruction counts in it), my own `git archive` of tags `v4.1` and `v4.2`
(tree f7beec4b checked), the package's `card\tiny386\bios.bin` and `vgabios.bin` (the harness's copies
have the same SHA-256), Windows 95 at 2 million instructions a virtual second, `-board`, pictures every
30 virtual ms, every picture held against the reference. Images are copied afresh by `run.sh` for
every run; the pristine image was not touched.

The session of investigation 1 (`plat`), segments as `BUILD-NOTES.md` 25.9:

| Run | sweep: pointer w/p/n | sweep | typing | scroll | move | sweep2 | pictures, differing |
|-|-|-|-|-|-|-|-|
| v4.2, as released | 269/132/105 | 57 % | 57 % | 61 % | 58 % | 64 % | 9,252, 0 |
| v4.1, as released | 269/132/105 | 57 % | 57 % | 61 % | 58 % | 64 % | 9,252, 0 |
| v4.2, my `ps1` image | 531/0/0 | 0 % | 7 % | 4 % | 3 % | 0 % | 9,219, 0 |
| v4.1, my `ps1` image | 531/0/0 | 0 % | 7 % | 4 % | 3 % | 0 % | 9,219, 0 |
| v4.2, standard VGA | 451/12/0 | 14 % | 7 % | 7 % | 1 % | 13 % | 9,386, 0 |
| v4.2, `ps1`, drawing in its own thread | 441/0/0 | (paused checks) | | | | | 1,712 checked, 0 lost |

[verified: `~/flicker/review/h/summary-l1.txt`, `rr-l1.out`]

* v4.2 and v4.1 have identical `END` lines (time, instruction count, idle count, registers) in each
  pair: v4.2 changes nothing the guest sees [verified].
* My `ps1` image is not the fix agent's `dib` image: I made it with the package's
  `qemumini-pointer.bat` (Windows PowerShell 5.1, through `cmd.exe`) on a copy of `base-n16.img`;
  `cmp -l` shows the one byte (image offset 149,094,504, 01 to 00), and mtools reads back
  `QEMUMINI.DRV` with SHA-256 e4c47e9a...38e9 [verified]. The fix agent's `dib` images were written with
  mtools, so the file's clusters differ, which is likely why the move segment reads 3 per cent here and 4
  there and the `END` lines differ between the two images [inferred].
* The pictures: `pics\n16-strip03.png` (as released, 12 pictures of the pointer sweeping: whole, gone,
  only its top half, a few pixels of it) and `pics\ps1-strip02.png` (changed: whole in all 12);
  `pics\finals.png` (session's end: as released Windows is still dragging the window's outline, behind;
  changed, the window has moved and the I-beam is over Notepad) [verified: looked at].


## 3. The cause

* **Source** (`~/flicker/audit/vmdisp`, the audit's export at v1.2025.0.119, read only): `init.c` 203
  to 208 sets `mouse_vxd = TRUE` whenever the VxD hands over a pointer buffer, which the VxD always
  does (`vxd_mouse.c` `mouse_buffer()` allocates it on first call; there is no registry or ini setting
  to turn the VxD's pointer off, unlike the SVGA driver's `HWCursor`); `enable.c` `BeginAccess_VXD()`
  calls `FBHDA_access_rect()` on every CURSOREXCLUDE access; `vxd_vbe.c` `FBHDA_access_rect()` ignores
  its four arguments and calls `FBHDA_access_begin()`, which erases the pointer on the outermost access;
  `FBHDA_access_end()` draws it again [verified]. So a binary change is the only way short of another
  driver build [inferred: no setting found where the flag or the buffer is set].
* **Binary** (`QEMUMINI.DRV` from the driver floppy in `~/v4-work/vbe/dl-vmdisp`, floppy SHA-256
  96eed45b..., file 15,534 bytes, SHA-256 0b3584b1...b713d): the only `C7 06 B8 06` in the file is at
  0x2064, after `mov ax,[06B4h]; mov bx,[06B6h]; test bx,bx; jnz; test ax,ax; jz` (the far pointer
  `mouse_buf` tested for NULL), exactly `if (mouse_buf != NULL) mouse_vxd = TRUE;`. The other five
  references to 06B8h are `cmp word [06B8h],0` followed by a jump (MoveCursor, SetCursor, CheckCursor,
  BeginAccess, EndAccess). Byte 0x2068 to 00 makes it `mov word [06B8h],0`, and the patched file's
  SHA-256 is the e4c47e9a... the package names [verified: objdump, python].
* **Harness**: section 2; and investigation 1's speed table (`research\FLICKER-REPRO.md` 3.7: 78, 47 and
  23 per cent of pointers broken at 1, 2 and 4 million instructions a second) shows it is a fixed amount
  of guest work, so a board slower than 2 million makes it worse, not better [verified there].
* **What the PC cannot rule out** [unknown]: anything on the board's side of the frame buffer (the
  panel's DMA starved of PSRAM, the cache write-back). Plat's description fits the pointer and nothing
  in the harness points elsewhere, but `TEST-PLAN.md` 14b's two videos before the change and 14d's after
  it are what proves it on the glass. 14b already asks him to say if he sees whole-screen black, shifted
  lines or windows left behind.


## 4. The cure: the driver's byte

* **Route B, the script, run by me** on a copy of the installed image through
  `qemumini-pointer.bat` with `-Check`, then `-Yes`, then again with no option: it found FAT16 in
  partition 1 (type 0x0E, sector 63), the file at the expected place, changed one byte, read it back,
  and the third run said "already changed... Nothing to do" (exit 0, 0, 0). The same on a copy of the
  offline 8-bit image (image offset 135,979,112) [verified]. I read the whole `.ps1` before running it:
  it opens the image read-only for every check, writes the one byte with `FileShare None`, asks first
  unless `-Yes`, refuses unknown files by size, SHA-256 and the instruction bytes, and checks the FAT's
  view of the byte against the file's [verified: code].
* **Route A, DEBUG in MS-DOS mode**: not rerun by me; the fix agent's `v42-dosfix` run typed it and
  `checks\flicker\dosfix.txt` is the session. The steps are right for DEBUG: a file that is not .EXE or
  .HEX is loaded at offset 100h, so file offset 2068h is 2168h, and `w` writes BX:CX bytes (3CAEh,
  15,534) back [verified: the README's lines against the file's bytes; inferred: DEBUG's behaviour].
* **Route C, an install from the changed floppy**: the script part was tested; an install of Windows'
  driver from that floppy was not run [inferred that it works: Setup copies the file as it is]. The
  README does not mark it either way (finding 4).
* **What it costs**: the fix agent's figures (`checks\pointer-cost-v4.2.txt`) are in the package and in
  `TEST-PLAN.md` 14d: the DIB engine's pointer costs nothing while the mouse is still, about two and a
  half times the VxD's per move, and Windows started with the mouse jiggled the whole time took 609 s
  to its desktop against 300 s. I did not rerun those. On the board, with a guest slower than the
  harness's, a pointer waved about could lag [unknown] (note 6).


## 5. Trying to break `VGA_CLEAR_AROUND`

The code (`vga.c` at the tag, read in full where it changed): `vga_clear_screen()` now only notes the
clear; `vga_clear_around()` blackens, store-if-different, only the screen outside the picture's
rectangle, and is called by each kind of picture with its own rectangle (text: `x1, y1, width1,
height1`; graphics: from `i0`, `w`, `h`; shrunk: `ox, oy, sw, sh`) before it draws; the end of
`vga_refresh()` blackens all of it when no picture took the clear (blanked, text too wide). After a
clear every path draws every pixel of its rectangle: `vbe_drawn` is false so `vga_vbe_refresh()` takes
`all`; `seen_valid` is false so the 16-colour words are not trusted; text gets `full_update`; the old
loop draws every pixel anyway [verified: code]. Each pixel inside the rectangle is stored at most once,
with its final value, and every store outside it is black, so no pixel can show black on its way from
the old picture to the new one [inferred from the code; the board's cache can only write back a value
the pixel ends up with].

**A randomised differential test** (`~/flicker/review/fuzz_clear.c`, built with each board's header as
`tests\host\run.sh` builds `test_clear`): the board's `vga.c` against the same `vga.c` with
`VGA_NO_CLEAR_AROUND` (v4.1's `memset()`), the same random operations to both, after every picture
the two screens compared pixel for pixel and every changed row checked to have gone to `redraw()`.
The operations: text, mode 12h and 13h set as a BIOS does (sometimes with a picture while blanked),
VBE modes from 8x1 to 1600x1000 at 4, 8, 15, 16, 24 and 32 bits and odd depths, kept or cleared
memory, VBE off, any DISPI register written (the X and Y offsets among them), CRT controller writes
(mostly the size and start registers), attribute writes and PAS off and on, sequencer, graphics, misc
output, palettes, the DAC mask, `vbe_blocks` changed with `vga_set_dirty_blocks()`, `planar_fast`
changed, video memory written (zeros, patterns, random, up to 64 KiB anywhere in 2 MiB), and pictures
with and without `full_update`.

| Run | Steps | Pictures | Differences |
|-|-|-|-|
| Korvo-1, seeds 11 to 18 | 1,600,000 | 292,554 | 0 |
| DOS board, seeds 11 to 18 | 1,600,000 | 292,554 | 0 |
| Korvo-1 and DOS board under AddressSanitizer and UBSan, address-space randomisation off, seeds 21 to 23 | 360,000 | about 66,000 | 0, and no sanitizer report |

[verified: `fz-long.txt`, `fz-san2.txt`]

A warning for whoever runs a sanitizer build in this WSL next: my first sanitizer run left
randomisation on, and one start in four of GCC 12's AddressSanitizer on this kernel
(6.18.33.2-microsoft-standard-WSL2) loops on "AddressSanitizer:DEADLYSIGNAL" before `main()` (7 of 30
starts; 0 of 30 under `setarch -R`) [verified: `asanflake.sh`, `asanflake2.sh`]. One such start wrote
14 GB of that line in nine minutes before I stopped it, by its PID, and deleted its log. Not a
tiny386 fault: the same seed runs clean with randomisation off and without the sanitizer. The fix
agent's `asan.sh` already uses `setarch -R`.

Planted faults, to show the test can see one (`mut.sh`, two seeds of 20,000 steps on each board):
the rest of the screen not made black in the rows above and below the picture (caught within 232 to
365 steps), not made black to the picture's right (880 to 1,153 steps), the shrunk picture's rectangle
8 pixels too wide (2,706 to 18,009 steps): all caught. Not caught, as expected: the text's
`full_update` after a clear (the fix agent's equivalent mutant, m6) and the count of whole pictures
after a `vbe_blocks` change (both builds share it; `test_vbe` is what catches that one, and the fix
agent's `mutants-v4.2.txt` shows it does) [verified].

**The DOS box session** (`sess\dosbox.txt`: Start, Run, `command`, `dir`, Alt+Enter to full screen,
`dir`, `mode co40`, `mode co80`, Alt+Enter back to the window, the pointer moved, Alt+Enter four times
six seconds apart, the pointer moved, `exit`): it did what it says [verified: the 5-second snapshots,
`pics\d42-ps1-a.png` and `-b.png`].

| Run | Pictures, differing | Clears over a picture (harness) | Blank pictures | quick: mid-update, pointer w/p/n |
|-|-|-|-|-|
| v4.2, as released | 10,411, 0 | 0 (cannot be seen, finding 1) | 68 | 30 %, 864/263/0 |
| v4.1, as released | 10,411, 0 | 10 (795,506 pixels black) | 68 | 30 %, 864/263/0 |
| v4.2, `ps1` | 10,378, 0 | 0 (as above) | 70 | 7 %, 1,125/2/0 |
| v4.1, `ps1` | 10,378, 0 | 10 (1,007,658 pixels black) | 70 | 7 %, 1,125/2/0 |
| v4.2, standard VGA | 10,544, 0 | 0 | 16 | 2 % |
| v4.2, `ps1`, drawing in its own thread | 1,898 checked, 0 lost | | 45 | |

[verified: `summary-l2.txt`]

So the full-screen DOS box, 40 columns, the return to the window (the driver setting its VBE mode
again) and four quick switches are drawn right on v4.2 with either pointer, and v4.1's ten black
flashes a run in that session are the clears v4.2 takes away. The blank pictures are the guest's own:
it turns the picture off through the attribute index while it switches (69 to 366 such writes a
segment), and a VGA shows black then too.

Two things that look like faults on the board and are not tiny386's (note 8): after the return to the
window, Windows repaints the desktop over several seconds at this speed, and until it has, the screen is
black where its video memory is black (`pics\d42-ps1-b.png`, first picture); and at each switch the
screen is black for up to a second while the guest has it blanked.

**At 8 bits a pixel** the same session ran on the offline 8-bit install, as released and changed, on
v4.2 and v4.1: Internet Explorer 4's first-run setup came up and took the keys, so the DOS box never
opened. What did run (the setup's dialogs and animated globe at 256 colours, about 15,300 pictures a
run) gave every picture equal to the reference, and v4.2 and v4.1 the same `END` lines [verified:
`summary-l3.txt`]. A DOS box at 8 bits in the harness is [unknown]; at `vga.c`'s level the palette and
8-bit VBE cases are in the random test.

**Page flips** are not used at the desktop (the X and Y offsets are written only by `FBHDA_swap()`,
which DirectDraw reaches) [verified: source]; in `vga.c` an offset write asks no clear and the next
VBE picture is drawn whole because its start moved, and the random test writes both offsets.


## 6. Ctrl+Alt+F11

Read in full (`kbd_input.c`, `speed.c`, `vga.c`, `esp_main.c`) [verified: code]:

* The hotkey table replaces the single hotkey with up to four; the search runs only with Ctrl and Alt
  down and swallows the key as F12 was. F11 is HID usage 44h. `test_kbd_input` passes.
* The press is a flag the USB task sets and `vga_task` takes between pictures; `vga_set_dirty_blocks()`
  stores the setting and bumps a count, and `vga_refresh()` draws the next two VBE pictures whole. A
  store that read the old value on the other core lands within microseconds of the change, so the
  second whole picture, 30 ms later, has it [inferred, as the fix agent says]. `vga_dirty_blocks` is
  in internal RAM, so both cores see the change at once [inferred].
* Two presses within one picture, or presses while the console is held, make one change, and a press
  while held takes effect when it is let go (note 9).
* Saving to the ini was not done (Plat said "ideally"); the reason given (the card holds the open
  guest disk image) is cautious rather than necessary, since a small file written and synced on a key
  press is ordinary FatFs use. Plat's call (note 10).


## 7. Cost and correctness against v4.1

* **Machine code** (my `dis.sh`, the package's `images\korvo1-dualboot\tiny386.elf` against
  `previous-v4.1\images\korvo1-dualboot\tiny386.elf`, numbers taken out): `iomem_write8`, `16`, `32`
  and `_string`, `vga_mem_write`, `16`, `32` and `_string` the same, 23, 32, 32, 55, 185, 81, 92 and 82
  instructions; `vga_refresh()` 818 to 835 and `vga_graphic_refresh()` 1,960 to 1,961 [verified]. The
  guest pays nothing more per store.
* **Host drawing time** per picture, v4.2 against v4.1, from my runs (4 at a time, so noisy): 36.4
  and 36.2, 33.2 and 33.8, 64.1 and 66.3 (the DOS box session, with its clears), 72.2 and 72.9, 248.9
  and 251.9 us [verified]. Within 3 per cent, and slightly less where clears happen.
* **The guest**: identical `END` lines in all 6 v4.2 and v4.1 pairs I ran [verified].
* **Host tests**: 39 of 39 in a clone of the scratch repository at `v4.2` (`~/flicker/review/ht.log`),
  including `test_clear` failing v4.1's `vga.c` with exit status 2 on both boards [verified]. I did not
  rerun ThreadSanitizer (69 minutes) or the 27 standard Windows runs.
* **Build from nothing**: the package's `build.sh` run by me into `~/flicker/review/rel` (a fresh clone
  of 4ce2e06 from GitHub, the patch, ESP-IDF in a clean environment): exit 0, and all 23 files of the
  four image sets byte for byte the package's, the same file list; the only compiler warning the old
  `i2s.c` one [verified: `rel.out`, `cmpimg.sh`].


## 8. The package

* `SHA256SUMS`: `sha256sum -c` passes for all 168 lines; each of the four image folders' lists passes;
  `previous-v4.1\SHA256SUMS` checks 29 files inside that folder, and the other 43 it names match their
  copies in `v3\` [verified].
* The four image sets are what `build.sh` makes from the patch today (section 7) [verified].
* `card\tiny386\bios.bin` and `vgabios.bin` are the files the harness runs used [verified: SHA-256].
* The patched driver's SHA-256 in the script and README is right [verified: computed].
* The faults found are in the findings: the clear evidence (1), "269 of 531" (2), and two labels (3,
  4).


## 9. Findings

| # | Severity | Where | What | Fix |
|-|-|-|-|-|
| 1 | minor | `BUILD-NOTES.md` 25.0, 25.4, 25.9, 25.10, 25.16; `TEST-PLAN.md` 14f; `REVIEW-RESOLUTION.md` round 7 (a) | "No clear over a picture in any v4.2 run, where v4.1 had 3 to 11" is cited as harness evidence [verified]. The harness's clear event is inserted after `simplefb_clear()` (`instr_vga.py`), which `VGA_CLEAR_AROUND` compiles out; in the built copy the `pcr_ev(PCR_EV_CLEARED...)` line sits in the `#else` branch, and every v4.2 run reports "clears in 0 steps", not only none over a picture [verified: `~/flicker/review/h/bin/r42/src/vga.c` line 327; every v4.2 run's log]. So the figure is true by construction. The property holds by other evidence: the code, `test_clear` (a mode set over an unchanged picture stores into no page), and this review's random test | Say instead that v4.2 has no `memset()` for the harness to count, and cite the code and `test_clear` (and section 5 here) for "no black". If the harness is used again, hook `vga_black()` and count black stored inside the picture's rectangle (it must be 0) |
| 2 | minor | `qemumini-pointer\README.txt` line 28 | "531 of 531 pictures, against 269 of 531 with the driver as released": as released 506 pictures were counted (269/132/105; the rest had the pointer outside the box), as `BUILD-NOTES.md` 25.0 says | "against 269 of 506" |
| 3 | minor | `BUILD-NOTES.md` 25.0 ("Checked again here ...: v4.2, v4.1, v3.9 and `vbe_blocks = 0` give the same pictures to the pixel") | For v3.9 with the driver that rests on investigation 1's `v39-n16` run (`research\FLICKER-REPRO.md` 3.1), not on v4.2's runs: v4.2's list ran v3.9 only with the standard VGA driver, where its figures differ from v4.1's (457/6/0 against 451/12/0; the guest's timing differs with v3.9's BIOS and core) [verified: 25.9's table, `checks\flicker\list3.txt`]. `TEST-PLAN.md` 14 and `README.md` cite section 24 for it, which is right | In 25.0, "v3.9 (investigation 1)" |
| 4 | minor | `qemumini-pointer\README.txt` route C; `BUILD-NOTES.md` 25.2 C | An install from the changed floppy was not run; only the script's making of it was [verified: no such run in the lists] | Mark route C [inferred] |
| 5 | note | `TEST-PLAN.md` 14 | v4.2 alone does nothing for the flicker (pictures as v4.1's to the picture). The docs say the cure is the driver byte, but Plat could flash v4.2, see no change and think it failed | One line in 14a: "flashing v4.2 alone changes nothing you will see in the pointer; 14c does" |
| 6 | note | `TEST-PLAN.md` 14d | The DIB engine's pointer costs more per move; at the board's speed a pointer moved without a break may trail behind the hand more than before [unknown]. 14d asks Plat to note whether it matters | Keep; if it matters, the hardware pointer (an emulated VMware SVGA II) is the only real cure |
| 7 | note | the review's 8-bit run | The DOS box at 8 bits was not exercised in the harness (IE4's setup took the keys); `vga.c` is covered by the random test | If an 8-bit image that has settled is ever made, run `sess\dosbox.txt` on it |
| 8 | note | `TEST-PLAN.md` 14 | Two board sights that are the guest's, not tiny386's: the desktop repainting for seconds after a full-screen DOS box, black until repainted; and a second or so of black while a switch has the picture blanked | A line in 14d so Plat does not report them as the flicker |
| 9 | note | `speed.c` | Ctrl+Alt+F11 presses are a flag, not a count: two within 30 ms, or several while the console is held, make one change | A count taken modulo 2, if it ever matters |
| 10 | note | `speed.c`, `BUILD-NOTES.md` 25.6 | Saving `vbe_blocks` to the ini was left out as a risk to the card; a write on an explicit key press, synced, is ordinary FatFs use | Plat's call |


## 10. What only the board can say [unknown]

As `BUILD-NOTES.md` 25.17: whether the pointer change looks on the glass as it does in the harness
(14b against 14d), how fast Windows feels, whether the DIB engine's pointer trails or lags, the mode
set's black gone (14f), and what Ctrl+Alt+F11's two settings cost.


## 11. Where things are

WSL `~/flicker/review/`:

* `t-v4.2`, `t-v4.1`: `git archive` of the tags; `clone`: a clone of the scratch repository at `v4.2`
  for the host tests (`ht/`, `ht.log`).
* `h/`: the harness copy (`harness/`, `bin/r42`, `bin/r41`, `img/` with the BIOS files, `runs/`,
  `summary-l1.txt` to `summary-l3.txt`); `rr.sh` and `l1.txt` to `l3.txt` (the runs); `sess/` (the fix
  agent's sessions copied, and `dosbox.txt` with its segments).
* `img/`: `n16-ps1.img` and `w8-ps1.img`, copies changed by the package's script (WSL only, never
  anywhere public); `drv/`: the driver file from the floppy and its changed copy.
* `fuzz_clear.c`, `fz.sh`, `fz2.sh`, `fz-long.txt`, `fz-san2.txt`, `fz-plain/`, `fz-asan/`,
  `asanflake.sh`, `asanflake2.sh`, `mut.sh` and `mut-m1` to `mut-m5`: the random test, its sanitizer
  run and its planted faults (`fz-san.txt` is the first sanitizer run, stopped; see section 5).
* `dis.sh`: the machine code comparison; `rel/`, `rel.out`: `build.sh` from nothing.
* `pics/`: the montages. Copies of `n16-strip03.png`, `ps1-strip02.png`, `finals.png`,
  `d42-ps1-a.png`, `d42-ps1-b.png` and `e42-w8ps1.png` are in this session's scratch folder on Windows
  (`scratchpad\review\pics\` under the session's folder in `%LOCALAPPDATA%\Temp\claude\`), and are not
  in the package (they show Windows); `pics\` in the text above means that folder.

Nothing I started is still running.
