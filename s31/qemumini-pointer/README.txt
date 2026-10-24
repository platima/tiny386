qemumini-pointer: the mouse pointer of VMDisp9x's QEMU driver, given to Windows
================================================================================

Written 2026-10-23 for tiny386-idf v4.2 (BUILD-NOTES.md section 25, TEST-PLAN.md section 14), and
corrected the same night after the review of v4.2 (BUILD-NOTES.md section 26).


Why

At 800x480 (and at every other VBE size and depth) Windows 95 with VMDisp9x 1.2025.0.119's QEMU
driver (QEMUMINI.DRV, the one TEST-PLAN.md 13 installs) shows the mouse pointer blinking, cut off
part way down, or as a black shape, whenever anything moves; a still desktop is fine. Windows is
also slow then, and falls behind the keys when typing.

The cause is the driver, not tiny386's drawing. The driver draws the pointer itself and rubs it
out and draws it again around every drawing Windows does, anywhere on the screen, a pixel at a
time. On a real PC that takes a fraction of a millisecond. On tiny386, at about 2 million guest
instructions a second, each time takes about 18 ms, so a large share of the pictures the board
shows catch the pointer gone or half drawn, and a large share of Windows' time goes on it
(research\FLICKER-REPRO.md, research\FLICKER-AUDIT.md, BUILD-NOTES.md 24 and 25).

The driver has the other way built in: Windows' own DIB engine draws the pointer, in one go, and
takes it off only where a drawing overlaps it. One line of the driver's source (init.c line 207,
"mouse_vxd = TRUE;") turns that way off. In the released QEMUMINI.DRV that line is the six bytes
C7 06 B8 06 01 00 at offset 0x2064; making the 01 at 0x2068 a 00 gives the pointer to the DIB
engine. Nothing else changes.

On the PC, with that byte changed [verified: the PC harness, BUILD-NOTES.md 25]: the pointer is
whole in every picture while it moves over the desktop (531 of 531 pictures, against 269 of 506
with the driver as released, the pictures with the pointer outside the part of the screen measured
left out), typing, scrolling and moving a window are drawn as cleanly as with
Windows' standard VGA driver, and Windows keeps up with the keys again. On the board this is
[unknown] until you try it.


Three ways; pick one

A. On the board, no card out: six lines typed in MS-DOS mode. Tried exactly so in the PC harness
   on the image the install of TEST-PLAN.md 13 leaves [verified: the screens below are the
   harness's].

   1. In Windows: Start, Shut Down, "Restart the computer in MS-DOS mode?", Yes.
   2. At the C:\WINDOWS> prompt type (the "l" in "l10" is a small L):

         debug c:\windows\system\qemumini.drv
         d 2160 l10

      The line it shows must have these sixteen bytes (the number before the colon will differ):

         ....:2160  85 C0 74 06 C7 06 B8 06-01 00 B8 01 00 5A 5B 5D   ..t..........Z[]

      If it does not, type q and Enter and stop here: nothing has been changed. Send a photo.
   3. Then:

         e 2168 0
         d 2160 l10

      The same line now with 00 where the 01 was:

         ....:2160  85 C0 74 06 C7 06 B8 06-00 00 B8 01 00 5A 5B 5D   ..t..........Z[]

   4. Then w (it says "Writing 03CAE bytes"), q, and exit. "Windows is now restarting..." and
      Windows comes back at 800x480 with the DIB engine's pointer.

   To put it back: the same, with e 2168 1 in step 3 (the line then shows 01 again).

B. On the PC, the card's Windows image (this folder's script). Windows shut down from its Start
   menu the last time it ran, the card in the PC:

         qemumini-pointer.bat X:\path\to\win95.img

   It finds C:\WINDOWS\SYSTEM\QEMUMINI.DRV inside the image through its FAT, checks that it is the
   released file, says what it will change, and after y changes that one byte in place and reads
   the file back. -Check only looks; -Undo puts the 01 back; -Out FILE writes a changed copy
   instead and leaves the image as it is (a copy is the size of the image: hundreds of MB).

C. A fresh install: a changed copy of the driver's floppy, then TEST-PLAN.md 13 with it:

         qemumini-pointer.bat X:\path\to\vmdisp9x-1.2025.0.119b-driver-2d.img

   writes vmdisp9x-1.2025.0.119b-driver-2d-ptrfix.img beside it (the original is not touched);
   use that file as fdb in win95-800x480.ini for the install. Tried so in the PC harness: the copy
   made by this script, then Windows' own install through the dialogs of TEST-PLAN.md 13, left the
   changed QEMUMINI.DRV in C:\WINDOWS\SYSTEM, and the pointer was whole in every picture afterwards
   [verified: BUILD-NOTES.md 26.2]. Windows writes to a floppy it reads (the boot sector's name and
   some last-access dates), so the floppy image's own SHA-256 changes after the install; the files
   in it do not.
   Have Disk over a driver already installed was not tried [unknown]; for an image that has the
   driver in already, use A or B.

The script runs with the Windows PowerShell every Windows 10 and 11 has; the .bat starts it with
the execution policy set aside for that one run (nothing is changed for good). Running the .ps1
from a PowerShell window works too: powershell -ExecutionPolicy Bypass -File qemumini-pointer.ps1
IMAGE.


What the script checks before it writes

* The image is a FAT12, FAT16 or FAT32 volume, or has one in its MBR partition table.
* QEMUMINI.DRV is found by name: in the root (the driver's floppy) or in WINDOWS\SYSTEM (an
  installed Windows), through the directories and the FAT. The same bytes are in the other drivers
  on the floppy, so it never looks for them by content.
* The file is 15,534 bytes, its SHA-256 is the released file's
  (0b3584b11420b3bc96652d9ae147df2188572e0d1b967a098ac8513eb97b713d) or the changed one's
  (e4c47e9aa983db1cebbeb09e625c53e71986e78868b2ff9094a22abfec0538e9), and the six bytes are where
  they should be; anything else and it stops without writing.
* After the write it reads the file back through the FAT; its SHA-256 must be the changed file's
  (with -Undo the released one's).

Tried on copies only, in WSL [verified: BUILD-NOTES.md 25.3]: the driver's floppy image, the
Windows image the install of TEST-PLAN.md 13 left (FAT16 in partition 1), a FAT32 partition, a
FAT12 image with the file in scattered clusters, a FAT16 image with no partition table, -Undo, a
changed driver build (refused), no driver (refused), not FAT (refused), an answer of n. Each
changed image differs from the original in that one byte only, and mtools reads the changed file
back with the changed file's SHA-256.


What the change costs

While the pointer moves, the DIB engine's pointer costs Windows as much as the driver's own did, or
more: each move is drawn by the DIB engine's generic code, about two and a half times the work of
the driver's own move. So a pointer moved without a break keeps Windows busy either way; what the
change gives back is everything drawn while the mouse is still (typing, scrolling, a window redrawn),
where the driver's own pointer took two thirds of Windows' time [verified: the PC harness,
BUILD-NOTES.md 25.13].

The driver's own pointer is there for programs that write the screen directly through VMDisp9x's
DirectDraw and 3D parts: with the DIB engine's pointer such a program may draw over the pointer or
leave a trail of it [inferred from the driver's source; not tried]. The desktop and ordinary
Windows programs go through the DIB engine and are covered. The 2D driver disk has no 3D part.


Licence

VMDisp9x is Jaroslav Hensl's, MIT licensed (github.com/JHRobotics/vmdisp9x). Nothing of it is in
this folder but the six bytes the script looks for and the two SHA-256 sums. The scripts are
offered under the licence of tiny386 (BSD 3-clause).
