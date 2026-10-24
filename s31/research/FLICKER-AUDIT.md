# The 800x480 flicker: audit of v4.1's drawing and of VMDisp9x's DISPI use

Written 2026-10-23 for Plat's report on v4.1: "800x480 at 16-bit is flickering a LOT. I don't recall it
flickering previously", and his later reports in `research\FLICKER-PLAT-NOTES.md` (re-read before each
decision here and at the end: a still desktop does not flicker; motion does, the mouse pointer and
anything moving, "like tearing, but it does not look like normal frame tearing"; 256 colours and 640x480
through the same driver do it too; the standard VGA driver's 16 colours do not). This is investigation 2
of 2: the drawing and clear paths for VBE pictures, read in the code and measured in the PC harness.

Nothing was flashed, no serial port was opened, nothing was committed to the v3 repository. Work folder:
WSL `~/flicker/audit` (section 9). Every claim is marked [verified] (seen in a source line or measured in a
run named here), [inferred] or [unknown].


## 0. The answer in one screen

* **The cause is the display driver's mouse pointer, not v4.1's drawing.** VMDisp9x 1.2025.0.119's
  `qemumini` draws the pointer itself, in software, from its VxD, and erases it before and draws it again
  after **every** access the DIB engine makes to the screen, wherever on the screen that access is
  (`FBHDA_access_rect()` ignores its rectangle). Each erase and redraw is about 36,000 guest instructions
  (about 18 ms at the 2 million a second the harness and the earlier steps take for the board), up to
  25 times a second in these tests while anything is being drawn. tiny386 shows the guest's frame buffer
  as it is at each picture, so about a third of the pictures taken while something is drawn catch the
  pointer gone, or half drawn, or as a black silhouette (the AND mask applied, the XOR not yet). That is
  a pointer that blinks and breaks up whenever anything moves, and a still desktop that does not
  [verified: harness, section 4].
* **Measured with the pointer parked on plain desktop** and a list's selection moved twice a second
  elsewhere on the screen: 20.4 per cent of 3,847 pictures with no pointer and 16.5 per cent with part of
  one, 631 separate disappearances in 116 s (5.4 a second). The same with Windows' standard VGA driver:
  none. The same with v3.9's core: identical to v4.1's, picture for picture; and with `vbe_blocks = 0`:
  identical too. So v4.1 did not bring it; Plat had simply not used this driver before [verified].
* **The fix that works is in the driver.** Changing one byte of `qemumini.drv` (file offset 0x2068, the
  `1` of `mouse_vxd = TRUE`) makes the driver hand the pointer to Windows' DIB engine, a path already in
  the release, which excludes the pointer only where a drawing touches it. With that byte: 0 pictures
  without the pointer and 0 with part of it in the same tests (1 of 3,850 with the pointer jiggling),
  and the guest gets its spare time back (in the list test it went from saturated to as idle as with the
  standard VGA driver) [verified: harness]. The pointer code alone took 45 per cent of the guest's
  instructions in that test [verified], which is a large part of "definitely slower" (section 6).
* **Suspects (a) to (e) of the brief, settled** (section 3): no clear of the panel, no DISPI write, no
  page flip or pan, no palette write, no CRT controller write and no blanking happens at the desktop with
  this driver, still or moving; all happen only at start, at the BIOS's mode sets and the driver's two
  [verified: harness counters].
  The marks and fences cover every store path [verified: code] and could not cause a flicker anyway (a
  lost mark leaves stale pixels, it does not blink). Tearing and the picture pacing are as they were in
  v3.9's 640x480 path, and the write-back covers every row drawn [verified: code].
* **An emulator-side mitigation exists but costs pictures** (section 5.2): drawing only after the guest
  has left video memory alone for a few ms cut the bad pointer pictures from 25 to 37 per cent to 2 to
  10 per cent with the unchanged driver, at the price of 32 to 55 per cent fewer pictures [verified:
  harness]. With the driver fixed it is not needed for the pointer [inferred].


## 1. What was read and run

* tiny386 v4.1 (f61337e, `git archive` into `~/flicker/audit/v41`), v4.0 and v3.9 the same way: `vga.c`,
  `vga.h`, `pc.c`, the store paths of `i386.c`, `esp/main/display.c`, `lcd_korvo1.c`, `esp_main.c`,
  `board_korvo1.h`. Line numbers below are v4.1's.
* VMDisp9x at its release tag v1.2025.0.119 (739eb72), `git archive` from the VBE step's clone
  (`~/v4-work/vbe-research/drivers/vmdisp9x`, read only) into `~/flicker/audit/vmdisp`; its `qemumini`
  is `vxd_*_qemu.c` and `pm16_calls_qemu.c` (each one line, `#define QEMU` and the shared file) with
  `enable.c`, `init.c`, `dibcall.c`, `modes.c`, `palette.c`, `vxd_vbe.c`, `vxd_mouse.c`,
  `vxd_mouse_conv.h`, `vxd_vdd.c`, `vxd_main.c` (`makefile` 405 to 421 and 571 to 583). The released
  binaries checked against it: `qemumini.drv` in the driver floppy image (SHA-256 96eed45b..., as
  fetched by the VBE step) and in the zip are the same file, SHA-256 0b3584b1...b713d [verified].
* The PC harness (the VBE step's, copied to `~/flicker/audit/h`), Korvo-1 `esp` flavour, Windows 95 at
  2 million guest instructions a virtual second, pictures every 30 virtual ms, Enter on up to 8 dialogs,
  stopped 120 virtual seconds after the taskbar: the VBE step's offline install of `qemumini` at
  800x480x16 (`~/v4-work/vbe/w95/img/w16-800x480.img`, 768K, 14M) and the pristine copy for the
  standard VGA driver (320K, 14M). Each run copies its image. Added to copies only (section 9):
  * counters and an event log in `vga.c` (every clear and where it came from, every VBE picture drawn
    whole and why, every DISPI write, CRT controller writes in a graphics mode, blanking through the
    attribute index, sequencer, graphics and DAC writes, 3DAh reads, window and LFB accesses);
  * in the harness: pictures that differ from both neighbours ("odd" pictures: something there for one
    picture only); a parked pointer and a probe that counts, in every picture, the pixels of a patch of
    plain desktop around it that are not the desktop's colour (0: no pointer; 1 to 99: part of one; the
    whole arrow is 105, the largest shape seen 245); arrow keys pressed every 0.5 s; the guest's
    instruction pointer sampled at each emulator step; and a prototype of quiet-gated drawing.
* The plain v4.1 and v3.9 trees built with the probe give the instrumented build's `END` line and probe
  figures exactly, so the counters change nothing in the guest [verified: runs `c-v41-n16-keys`,
  `b-n16-keys`].


## 2. How a VBE picture reaches the panel in v4.1 [verified: code]

* The guest writes the linear frame buffer through `store8/16/32()` (`i386.c` 861 to 920) and the string
  paths (`rep movs` 2846 to 2859, `rep stos` 2718 to 2744 through `store*()`), every one of which goes to
  `iomem_write8/16/32()` or `iomem_write_string()` (`pc.c` 676 to 779) for an address at E0000000h or
  above (`in_iomem()`, 808 to 811). Each stores, then marks its 1 KiB block (`LFB_WRITTEN()`, 650 to
  660) through `vga_dirty_mark()` (`vga.h` 71 to 79): `fence rw,w`, then the byte. The window's chain-4
  stores do the same while VBE is on (`VRAM_WRITTEN()`, `vga.c` 1311 to 1314; the stores at 3051, 3101,
  3148, 3198). There is no path that stores into video memory without one of these.
* `vga_task` (core 0, `display.c` 61 to 117) calls `pc_vga_step()` every 30 ms with `xTaskDelayUntil()`
  (`LCD_FRAME_MS`, `board_korvo1.h` 23), so at most 33.3 pictures a second. `vga_refresh()` (2434 to
  2499) takes any clear asked for, then `vga_graphic_refresh()` reaches `vga_vbe_refresh()` (1476 to
  1536): `vga_take_marks()` (1360 to 1396) clears each marked block's byte, notes the screen rows the
  block's lines lie on, then `fence rw,rw`; each noted row is converted from video memory in full
  (`vbe_row()`, 1423 to 1463), a word stored only where it differs; the rows that changed go to
  `redraw()` in bands, and on the board `redraw()` is `lcd_flush()` (`esp_main.c` 233 to 238), which
  writes those rows back from the cache with `esp_cache_msync()` (`lcd_korvo1.c` 99 to 127).
* The panel: 18 MHz, 928 by 548 clocks, one frame buffer in PSRAM that the LCD's DMA reads continuously
  without the cache (`lcd_korvo1.c` 42 to 73): a frame every 28.25 ms (35.4 a second), of which 24.7 ms
  are the 480 visible lines.

So what the panel shows is the guest's video memory as it was when each row was read, a picture every
30 ms, not synchronised with the guest or with the panel.


## 3. The suspects

### (a) A clear of the panel asked for by the guest

* **In the code** [verified]: `vbe_update_vgaregs()` (890 to 936) asks for a clear (`force_clear_req`,
  930 to 935) on a write of ENABLE, XRES, YRES, BPP, VIRT_WIDTH or VIRT_HEIGHT while VBE is on (2870 to
  2882; it returns at 894 when VBE is off). X and Y offset do not ask since v4.0 (2854 to 2868). A CRT
  controller write in a graphics mode asks for a deferred clear (2757 to 2781) that the next picture
  leaves out when the picture is in the same place and the rest is black (1906 to 1919). A change of
  `graphic_mode` (the attribute index's bit 5 blanking, or GR06) clears (2475 to 2480). Chain 4 switched
  under VBE asks for a whole picture, not a clear (2686 to 2693). No DAC or palette write clears.
* **What the driver does** [verified: source]: `VBE_setmode()` (`vxd_vbe.c` 219 to 284) blanks through
  the attribute index (225), resets the sequencer, writes ENABLE 0, XRES, YRES, BPP, VIRT_WIDTH,
  VIRT_HEIGHT, BANK, X and Y offset, ENABLE E1h (enabled, 8-bit DAC, LFB, no clear of video memory),
  unblanks (269) and clears video memory itself (`VBE_clear()`, 278). It is called only for a mode set
  (`modes.c` 213 and 281, `vxd_main.c` 501). The DAC is written by `FBHDA_palette_set()` (286 to 292)
  for the 8-bit palette [inferred: GDI sets a palette only on a palettised device; none was written at
  the desktop in 16 bits, below]. `FBHDA_swap()` (306 to 326), the only writer of the offsets outside a
  mode set, is reached only through DirectDraw and the 3D HAL (`vxd_main.c` 397 and 844; README 111 to
  125, "double buffering").
* **Measured** [verified: runs `n16-still`, `n16-mouse`, 120 s at the desktop each]: 2 clears asked by
  `vbe_update_vgaregs()` and 8 `graphic_mode` changes, all before 62.4 virtual seconds (the BIOS's mode
  sets and the driver's two mode sets at 60.9 and 61.4 s); after that, in about 280 s, still or with the
  mouse moving every 50 ms: no clear, no DISPI write (13 ENABLE writes in all, every one at the BIOS's
  mode sets or the driver's two), no CRT controller write in a graphics mode after 40 s, no blanking
  after 62.4 s, no GR06 change after 38.3 s, and no VBE picture drawn whole except at the two mode sets
  and for the harness's own snapshots. The standard VGA driver: 6 and 8 `graphic_mode` changes (still,
  moving), no VBE picture.
* **On the panel**: a black frame and a whole repaint at each mode set, twice when Windows starts the
  driver [inferred: the clear is a `memset()` of the whole 750 KiB frame buffer through the 64 KiB
  write-back cache, so its zeros reach PSRAM, and so the panel, before the redraw that follows in the
  same picture]. Not a flicker. Present the same way in v3.9.

### (b) The clear and the redraw in separate pictures

Not in v4.1 [verified: code]: every clear (the forced one at 2456 to 2462, the mode change at 2475 to
2480, the deferred one at 1913 to 1914) is followed in the same `vga_refresh()` call by the picture drawn
whole (`full_update`, or `vbe_drawn` false from `vga_clear_screen()` 296 to 298). As (a) says, the panel
can still see the black between them, but only at a mode set. One more thing seen at the mode set
[verified: run `n16-still`, picture 2019 at 60.95 s]: a band of rows 24 to 54 differs for one picture
only; most likely the old 640x480 planar memory shown as 800x480x16 in the moment between the driver's
ENABLE (with no clear of video memory) and its own `VBE_clear()` [inferred]. Cosmetic, once per mode
set.

### (c) Marks lost between the cores, or a store path without a fence

* Every store path marks after storing, with the release fence first (section 2) [verified: code]. The
  drawing clears a mark, fences, then reads; the writer stores, fences, then marks. For the drawing to
  read old pixels and the mark to end cleared, the writer's mark would have to come before the drawing's
  clear in coherence order while the drawing's read comes before the writer's data store, a cycle that
  RVWMO's global memory order forbids with these two fences [inferred: the memory model; on x86 the
  harness cannot show it].
* The VBE step's and v4.1's audits held 154,878 periodic pictures against a full redraw with none
  differing (BUILD-NOTES 23.8), and here v3.9 (every pixel every picture) and v4.1 (from the blocks)
  give the same pointer figures picture for picture, as does `vbe_blocks = 0` [verified: runs
  `c-v39-n16-keys`, `c-v41-n16-keys`, `c-v41-n16-keys-vbeblocks0`].
* And a lost mark could not look like this: it leaves pixels stale until the guest writes the block
  again; it does not make something disappear and come back.
* Not present; not a cause.

### (d) The panel scanned while it is written, the write-back, and the pacing

* **Write-back** [verified: code]: every row `vbe_row()` stored into lies in a band given to `redraw()`
  (1519 to 1534; after a clear every row, 1531 to 1532), and `lcd_flush()` writes back `h` whole rows of
  1,600 bytes from row `y` with `ESP_CACHE_MSYNC_FLAG_UNALIGNED`, so every row drawn reaches PSRAM by the
  end of the picture. Before that, dirty lines reach PSRAM only when the cache evicts them, in any
  order, so for the moment between the first store into a band and its write-back the panel can show a
  mix of old and new 64-byte (32-pixel) pieces of it [inferred; for a pointer-sized band well under a
  panel frame].
* **Tearing**: nothing ties a picture to the panel's scan, so a change drawn while the scan passes
  through it shows old above and new below for one panel frame [inferred]. 33.3 pictures against 35.4
  panel frames a second: each picture lands 1.75 ms later in the panel's frame than the one before (about
  34 lines), so where a tear can fall moves down the screen and wraps every 16 pictures (0.48 s), and one
  panel frame in about 17 repeats a picture (a 2 Hz judder in steady motion) [arithmetic]. All of this
  is v3.9's too: the 640x480 path writes back changed rows the same way, and drew about as many: rows
  changed a picture 2.6 still and 11.7 with the mouse moving for the standard VGA driver, against 4.0 and
  13.8 for `qemumini`, most of the difference being the pointer's own erase and redraw [verified: runs
  `vga-still`, `vga-mouse`, `n16-still`, `n16-mouse`].
* Not the cause of what Plat sees; worth doing later only for tearing in moving windows (5.3).

### (e) The driver's DISPI pattern

* No page flipping, panning or repeated mode sets at the desktop (a) [verified: source and counters].
* **Its pointer is the cause** (section 4). `qemumini` sets `mouse_vxd = TRUE` whenever its VxD gives it
  a pointer buffer (`init.c` 203 to 208), which it always does; from then on `SetCursor` and `MoveCursor`
  go to the VxD (`dibcall.c` 58 to 126), and the DIB engine's access callbacks are `BeginAccess_VXD()`
  and `EndAccess_VXD()` (`enable.c` 146 to 169, set at 274 and 275). On every access with CURSOREXCLUDE
  they call `FBHDA_access_rect()` and `FBHDA_access_end()` (`vxd_main.c` 382 to 393). In the VxD,
  `FBHDA_access_rect()` throws its rectangle away and calls `FBHDA_access_begin()` (`vxd_vbe.c` 337 to
  340), which on the outermost of nested accesses calls `mouse_erase()` (328 to 335): the saved background is
  copied back over the pointer. `FBHDA_access_end()` (342 to 354) calls `mouse_blit()`: the background
  under the pointer is saved and the pointer drawn, AND mask then XOR mask, pixel by pixel through
  `screen_line[x] &=` and `^=` (`vxd_mouse.c` 285 to 304, `vxd_mouse_conv.h` from 47, 89 and 130). A
  pointer move is the same erase and redraw (`mouse_move()`, `vxd_mouse.c` 204 to 228).
* Unreleased source after the tag (767761d) has the erase and redraw in `FBHDA_access_begin()` and
  `FBHDA_access_end()` commented out and the pointer reworked [verified: `git diff v1.2025.0.119
  767761d`], so the author has moved away from this too; nothing released has it.

### Others looked at

* **The standard VGA driver waiting for the retrace**: no. 3DAh is read 538,058 times in every standard
  VGA run and 571,950 in every `qemumini` run whether the mouse is still, jiggling or keys are pressed,
  so neither driver polls the retrace for its pointer or its drawing; the difference is how they draw
  the pointer [verified: runs `h-vga-*`, `h-n16-*`].
* **The 1 KiB blocks not lining up with rows**: a marked block makes every row it touches be converted
  whole from video memory (1381 to 1392, 1505 to 1519), so the block size never decides what is shown;
  the half-drawn and black pointers are states the guest really had, caught by a picture. The harness
  draws between slices of guest instructions, never in the middle of one, and still shows them
  [verified: runs above; pictures in section 4].
* **BOXV9X for QEMU v1.1.0** (the design's second choice, which leaves the pointer to the DIB engine):
  bound offline the same way, its VxD did not load on this Windows 95 ("Dynamic load failed
  boxvmini.vxd: Not a valid device", BOOTLOG.TXT) and Windows fell back to the standard VGA driver
  [verified: run `x-boxv-still`]. Not a drop-in alternative.


## 4. The evidence for the pointer [verified: harness runs named]

**Odd pictures**, whole run, pointer still where Windows starts it (screen centre):

| Run | Pictures | Odd pictures | Of them pointer-sized (under 48x48) | After the desktop |
|-|-|-|-|-|
| `n16-still` (`qemumini` 800x480x16) | 11,275 | 549 | 491 | 285 in 120 s |
| `vga-still` (standard VGA) | 10,946 | 25 | 13 | 19 in 120 s |
| `n16-mouse` (jiggle every 50 ms) | 13,938 | 6,240 | 6,236 | |
| `vga-mouse` | 11,584 | 1,433 | 1,417 | |

(The jiggle itself, 2 pixels there and back, makes some odd pictures with any driver.) Pictures of the
pointer region in three consecutive pictures, from `n16-still`, are in WSL
`~/flicker/audit/pics-n16-still-aba.png` (kept off the package): hourglass, nothing, hourglass; nothing,
hourglass, nothing; hourglass, nothing, hourglass cut off at a row.

**Pointer probe**, pointer parked on plain desktop at the right of the screen, 116 virtual seconds after
the desktop (about 3,850 pictures); "keys" presses Down and Up in turn every 0.5 s, so a list redraws
elsewhere while the pointer does not move; "jiggle" moves it 2 pixels there and back every 50 ms:

| Driver, core | Scenario | No pointer | Part of one (1 to 99 pixels) | Disappearances | Run |
|-|-|-|-|-|-|
| `qemumini`, v4.1 | still | 148 (3.8 %) | 159 (4.1 %) | 138 | `q-n16-still` |
| `qemumini`, v4.1 | keys | 786 (20.4 %) | 635 (16.5 %) | 631 | `q-n16-keys` |
| `qemumini`, v4.1 | jiggle | 455 (11.8 %) | 493 (12.8 %) | 433 | `q-n16-jiggle` |
| `qemumini`, v3.9 | keys | 786 | 635 | 631 | `c-v39-n16-keys` |
| `qemumini`, v4.1, `vbe_blocks = 0` | keys | 786 | 635 | 631 | `c-v41-n16-keys-vbeblocks0` |
| `qemumini`, v4.1, drawing in its own thread | jiggle | 396 of 3,579 | 410 | 378 | `t-v41-n16-jiggle-thread` |
| standard VGA, v4.1 | still, keys, jiggle | 0, 0, 0 | 0, 0, 2 | 0, 0, 0 | `q-vga-*` |
| standard VGA, v3.9 | keys | 0 | 0 | 0 | `c-v39-vga-keys` |
| `qemumini` with the DIB engine's pointer (5.1) | still, keys, jiggle | 0, 0, 1 | 0, 0, 0 | 0, 0, 1 | `ed-dib-*` |

The same with the pointer pinned to the top right corner first, so it sits in the same place whatever a
run's timing (`k-*` runs): `qemumini` 156, 799 and 452 pictures without the pointer (145, 655 and 437
disappearances) in still, keys and jiggle; with the DIB engine's pointer 0, 0 and 1. (The standard VGA
runs pinned this way lost the pointer from the probe a few times for seconds at a time, 6, 7 and 24
times in 116 s, which looks like the pointer leaving the patch rather than blinking [inferred]; the
centre-right runs above are the clean comparison.)

**How much drawing the pointer is** (`b-*` and `g-*` runs, the 116 s window): stores into a 56 by 56
pixel box round the parked pointer were 1,993,728, 8,899,584 and 6,717,148 (still, keys, jiggle), each
matched by as many reads: 649, 2,897 and about 2,187 erase and redraw cycles of 3,072 stores (1,024 to
restore, 2 for each of 1,024 pixels to draw), 5.6, 25 and 19 a second. They were 99.4 and 98.5 per cent
of all 16-bit stores into the frame buffer in the still and keys runs. Sampling the guest's instruction
pointer at every emulator step puts 23.4 M of 190.2 M (12 per cent), 104.0 M of 232.0 M (45 per cent)
and 79.6 M of 232.0 M (34 per cent) of the guest's instructions in the VxD's pointer code, C0FD7800h to
C0FD87FFh: every one of the box stores came from C0FD7D00h (a third, the restore) and C0FD8000h (two
thirds, the draw), and C0FD7A00h, busy but with no stores, is most likely the save [inferred]. That is
about 36,000 guest instructions a cycle, not counting the ring transitions into the VxD.

**Why the standard VGA driver does not do it** [inferred]: its pointer is excluded only from drawing
that overlaps it and is moved in one pass, and it is quick; so is the DIB engine's, which is why the
one-byte change of 5.1 behaves like it.

**Why it looks "like tearing, but not normal tearing"** [inferred from the pictures]: the pointer is
erased and drawn a row at a time and a mask at a time, so a picture can hold the top of a pointer and not
the bottom, or the pointer's shape in black (AND applied, XOR not yet), at the place it still is; and
since the whole erase and redraw is repeated for every bit of drawing anywhere, it happens over and over
while anything moves, and not at all on a still desktop. Everything else that moves (a window outline
being dragged, a list scrolling) also pays an erase and redraw of the pointer for each drawing call, and
so moves in visibly slower steps.


## 5. The fix

### 5.1 In the driver: hand the pointer to the DIB engine (recommended) [verified: harness]

`qemumini.drv` already has the other path: with `mouse_vxd` FALSE, `SetCursor`, `MoveCursor` and
`CheckCursor` go to `DIB_SetCursorExt()`, `DIB_MoveCursorExt()` and `DIB_CheckCursorExt()`, and
`BeginAccess_VXD()` and `EndAccess_VXD()` call `DIB_BeginAccess()` and `DIB_EndAccess()` with the access's
rectangle (`dibcall.c` 58 to 138, `enable.c` 146 to 169); the VxD's own erase and redraw then do nothing,
since its pointer is never loaded (`mouse_valid` stays FALSE, `vxd_mouse.c` 285 to 304). The only line
that turns the VxD pointer on is `init.c` 207, `mouse_vxd = TRUE;`.

In the released `qemumini.drv` (SHA-256 0b3584b11420b3bc96652d9ae147df2188572e0d1b967a098ac8513eb97b713d,
15,534 bytes) that line is the six bytes `C7 06 B8 06 01 00` (`mov word ptr [06B8h],1`) at file offset
0x2064, right after the test of `mouse_buf` (`A1 B4 06 8B 1E B6 06 85 DB 75 04 85 C0 74 06`); 06B8h is
tested against 0 in five places, the five uses of `mouse_vxd` [verified: `findvxd.py`, and the bytes
read as 16-bit code]. The test: byte 0x2068 changed from 01 to 00, the file put into a copy of the
installed image (`~/flicker/audit/img/w16-dibcur.img`, `mkdibcur.sh`). Windows came up at 800x480x16 as
before, with the pointer drawn and moving; the probe's figures are in section 4 (0, 0 and 1 pictures
without the pointer); the guest's spare instructions over the run were 53.5 M in the keys test against
5.0 M with the VxD pointer and 49.2 M for the standard VGA driver; its 16-bit stores into the frame
buffer in the keys window fell from 9.04 M to 84,000 [verified: `k-dib-*`, `ed-dib-*`, `b-n16-keys`].

How Plat could get it; his choice:

* A small script in the package that makes a patched copy of the driver floppy (or of `QEMUMINI.DRV`):
  check the image's SHA-256 (96eed45b...), find `QEMUMINI.DRV` through the FAT, check its SHA-256 and the
  six bytes at 0x2064, write 00 at 0x2068, save under a new name and leave the original alone. Note: the
  same 21 bytes round the store occur four times in the floppy image (the four `.DRV` files share this
  code), so a search of the raw image is not enough; the file must be found by name [verified: the
  image searched]. The package would still carry no third-party binary. Then Have Disk from the
  patched floppy as in TEST-PLAN 13, or on an installed Windows the file copied over
  `C:\WINDOWS\SYSTEM\QEMUMINI.DRV` and a restart.
* Or `qemumini.drv` built from the v1.2025.0.119 source with `init.c` 207 removed (Open Watcom; the
  source's later commits build it on Linux). More work, the same result [inferred].
* VMDisp9x is MIT licensed: changing it and passing the change on is allowed, with its notice.
* What it costs [inferred, untested]: the VxD pointer is there for programs that write the frame buffer
  directly through VMDisp9x's DirectDraw and 3D parts; with the DIB engine's pointer such a program may
  draw over the pointer or leave pointer trails. The desktop and ordinary Windows programs use the DIB
  engine and are covered.
* Worth reporting upstream (JHRobotics/vmdisp9x): `FBHDA_access_rect()` ignores the rectangle, so every
  screen access erases and redraws the pointer. Upstream contact is Plat's to make.

### 5.2 In the emulator: draw only after a quiet moment (mitigation, not needed with 5.1) [verified: harness prototype]

A picture held back while the guest has stored into the frame buffer in the last Q ms, but drawn anyway
once B ms have passed since the last one (`PCR_QUIET`, harness only), with the unchanged driver, pointer
pinned (`k-*` runs):

| Scenario | Q, B | Pictures | No pointer or part of one | Pictures forced by B |
|-|-|-|-|-|
| keys | none | 3,847 | 1,417 (36.8 %) | |
| keys | 5, 100 ms | 2,522 | 169 (6.7 %) | 212 |
| keys | 10, 100 ms | 2,279 | 225 (9.9 %) | 384 |
| keys | 20, 250 ms | 1,717 | 70 (4.1 %) | 35 |
| jiggle | none | 3,850 | 964 (25.0 %) | |
| jiggle | 5, 100 ms | 2,619 | 44 (1.7 %) | 78 |
| jiggle | 10, 100 ms | 2,473 | 48 (1.9 %) | 112 |

So it helps a lot but not wholly (the pointer is often erased while the guest computes without storing,
and a busy guest forces pictures through B), and it costs a third to a half of the pictures and up to B
of delay. On the board it would need a cheap "written" flag set with each mark (`LFB_WRITTEN()` and
`VRAM_WRITTEN()`) and `vga_task` waking every few ms instead of every 30 [inferred]. With 5.1 the pointer
does not need it; it might still smooth other software that draws in visible steps. Measure on the board
after 5.1 before deciding.

### 5.3 Smaller emulator items, none of them this flicker

* **Black frame at a VBE mode set** (a): `vga_clear_screen()` (276 to 299) could skip the `memset()` when
  the next picture is a VBE picture of exactly the panel's size, since that picture is drawn whole and
  every row written back anyway (set `fb_all_dirty` and clear `vbe_drawn` only). Twice per Windows start;
  cosmetic [inferred].
* **The band of old memory for one picture at the mode set** (b): cosmetic, the driver's own order
  (ENABLE with no clear, then its clear); leave it.
* **Tearing in moving windows** (d): pictures could be timed against the panel's vertical blank (ESP-IDF's
  RGB panel `on_vsync` event) so that changed rows are written back behind the scan. As in v3.9; only if
  Plat finds tearing a problem once the pointer is fixed [inferred, untested on the board].

### 5.4 What not to do

* `vbe_blocks = 0`: the same pointer pictures exactly (section 4), and on the board 5 million
  instructions a picture on core 0 and 1.5 MB of PSRAM read through the shared cache each picture
  [inferred from BUILD-NOTES 22.8]. A runtime toggle for it (Plat's request) is still useful for
  measuring speed, but it will not change this flicker.
* Snapshotting each changed block in one go, or timing pictures to the emulated retrace: the bad pictures
  are the guest's own states, which a picture drawn between instructions still shows, and neither driver
  polls the retrace (section 3, others).


## 6. Speed, for Plat's second report

The VxD pointer is a large part of "definitely slower" [verified: harness, section 4; the board
inferred]: while a list redraws twice a second, 45 per cent of the guest's instructions go to erasing and
redrawing a pointer nobody moved, and the guest has no spare time left (5.0 M idle instructions over the
run, the same figure in every saturated run, so most likely all before the desktop [inferred], against
49.2 M for the standard VGA driver and 53.5 M for `qemumini` with the DIB engine's pointer). Every
drawing call anywhere costs another 36,000 instructions or so (18 ms at 2 million a second) before and
after it, so a window repaint of a few dozen calls carries most of a second of pointer work [inferred
from the per-cycle figure]. On the emulator's side each of those pointer stores is an `iomem_write16()` with its
mark and fence (20 instructions in v4.1, BUILD-NOTES 23.2) and each load an `iomem_read16()`: 18 million
calls in the keys window, nearly all of them the pointer. Section 5.1 removes nearly all of it. What
remains between 800x480x16 and 640x480x16 colours (2 bytes a pixel against half a byte, and the marks)
is investigation 1's question.


## 7. For Plat to check on the board (no new firmware needed)

1. Park the pointer on empty desktop and make something else redraw (hold an arrow key in a list or a
   folder window, or start something that animates). If the pointer blinks, breaks up or turns into a
   black shape while it is not being moved, this is it. With the standard VGA driver it should not.
2. Then the patched driver (5.1), the same test: the pointer should stay whole; dragging a window still
   shows its outline redrawn in steps, as on any slow PC.
3. If whole-screen black flashes or shifted, garbled lines show up as well, that is something else: not
   seen in any harness run, and the board's PSRAM shared between the panel's DMA and both cores is
   the one part the harness cannot model [unknown].


## 8. What is not known

* How the board's guest speed compares with the harness's 2 million instructions a virtual second, which
  sets how long each erase and redraw lasts in real time; slower makes it worse [unknown].
* Whether the board's PSRAM bandwidth ever starves the panel's DMA (display corruption rather than a
  blinking pointer) [unknown].
* Whether DirectDraw or 3D programs misbehave with the DIB engine's pointer (5.1) [unknown].
* The answer to Plat's first question (what exactly he sees) is in his third report as "motion, the
  mouse pointer and anything moving"; whether the patched driver makes the board look like the
  standard VGA driver is for the board to say [unknown].


## 9. Files

WSL `~/flicker/audit` (my work; nothing elsewhere was changed):

| Path | What |
|-|-|
| `v41/`, `v40/`, `v39/`, `vmdisp/` | the sources read (`git archive`; VMDisp9x at v1.2025.0.119) |
| `inst/` | a copy of v4.1 with the counters (`inst.py`, `inst3.py`, `inst4.py`, `inst8.py` applied) |
| `h/harness/` | the harness copy: odd pictures, the probe, parking, keys, step sampling, quiet gating (`inst.py`, `inst2.py`, `inst3.py` to `inst9.py`, `fix5.py`) |
| `h/bin/` | the builds: `inst*-korvo1` (instrumented), `v41p`, `v39p`, `v41t` (plain trees, probe harness) |
| `h/runs/` | every run: `log.txt`, `vgastats.txt` and `vgaev.txt` (counters, events), pictures; first pass `n16-*`, `vga-*`; probe `q-*`, `b-*`, `c-*`, `t-*`, `k-*`; step sampling `e-*`, `f-*`, `g-*`; DIB pointer `d-*`, `ed-*`, `k-dib-*`; quiet `h-*` (probe not over the pointer, discarded), `k-n16-*-q*`; BOXV9X `x-*` |
| `r1.sh` to `r8.sh`, `r5b.sh`, `s*.sh` | the scripts that built and ran them |
| `findvxd.py`, `mkdibcur.sh`, `files/` | the search for `mouse_vxd`, the one-byte test, the extracted and patched driver files |
| `mkboxreg.py`, `mkboximg.sh` | the BOXV9X attempt |
| `img/w16-dibcur.img`, `img/boxv-16-800x480.img` | two copies of Windows 95 made for these tests; WSL only, never public; delete when done |
| `pics-n16-still-aba.png`, `crop.py` | the three-picture crops of section 4 |

Every run ended by itself; nothing is left running.
