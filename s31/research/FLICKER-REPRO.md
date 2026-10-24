# The 800x480 flicker, reproduced on the PC (investigation 1 of 2)

Written 2026-10-23 for Plat's report on v4.1: "800x480 at 16-bit is flickering a LOT. I don't recall it
flickering previously", and his later notes (`FLICKER-PLAT-NOTES.md`: a still desktop does not flicker,
motion does, "like tearing, but it does not look like normal frame tearing"; every VBE mode and size does
it, the 16-colour standard VGA path does not). Work folder: WSL Debian `~/flicker/repro`. Nothing was
flashed, no serial port was opened, nothing was committed to the v3 repository (the trees used are `git
archive` exports of the tags), and no picture of Windows is in this package: they stay in WSL and in the
session's scratch folder (section 8). Every claim is marked [verified] (seen in a run, a log or a source
line named here), [inferred] or [unknown].


## 0. In one screen

* **What flickers is the mouse pointer, and whatever the guest is part way through drawing.** With
  VMDisp9x's `qemumini` driver, Windows draws the pointer itself into video memory: it restores the 32x32
  box under the old position (1,024 stores), works for about 7,700 instructions, then writes the new
  32x32 box twice (2,048 stores). At the board's 2 million guest instructions a second that is about
  18 ms for each move of the pointer, about 41 moves a second, so for most of the time the pointer in
  video memory is missing or half drawn. tiny386 shows video memory as it is every 30 ms, so it shows
  that. And the driver hides and redraws the pointer around every drawing operation anywhere on the
  screen (each character typed, each line scrolled), so the pointer blinks even when the mouse is still
  [verified: section 3].
* **How much:** while the pointer moves over empty desktop, 47 per cent of pictures show it broken
  (missing or partly drawn), about 15 pictures a second, in a regular rhythm of about 6 blinks a second.
  While typing, scrolling or moving a window, 57 to 61 per cent of pictures are taken while the guest is
  part way through an update. The standard VGA driver at 640x480 in 16 colours, same session: 2.6 per cent
  (v4.1) and 1.3 per cent (v3.9) of pictures with the pointer broken, 1 to 7 per cent mid-update
  [verified: section 3.4].
* **The emulator's drawing is not at fault.** In every run, every picture the board would show (9,252 to
  9,386 a session run) equals the guest's whole screen converted afresh at that instant; v4.1, v4.1 with
  `vbe_blocks = 0` and v3.9 give the same pictures, picture for picture. No clear, no DISPI or CRT
  controller write, no palette or blanking write happens while Windows is in use. The block marks lose
  nothing, also with the drawing in a second thread. Suspects (a) to (e) of the brief are settled in
  section 5 [verified on the PC; the board's own memory ordering and panel timing are unknown].
* **So `vbe_blocks = 0` will not help Plat**, and v3.9 with this driver would flicker the same
  [inferred from the harness, which gives the same pictures for each].
* **It is also why Windows is slower at 800x480** (Plat's second report): the pointer costs the guest
  about 36,000 instructions a move, its pause included (the standard VGA driver: about 6,300), and about 28,000 more for
  every drawing operation while it is on screen. In the session, 55 to 64 per cent of the guest's
  instructions went into updates of video memory while anything moved, against 1 to 13 per cent with the
  VGA driver, and Windows fell about 20 s behind the typing; with the VGA driver it kept up [verified:
  3.6].
* **What a fix in tiny386 can do (evidence for the Fix agent, section 4):** holding a picture until the
  guest has made no store into video memory for 10,000 instructions (at most 30 ms late) brings the
  moving pointer from 47 to 1.6 per cent broken, at about 20 pictures a second instead of 33, but helps
  less while the guest keeps drawing (typing, scrolling, moving: 36 to 40 per cent mid-update, from 57
  to 61). Atomic snapshots, `vbe_blocks = 0` and following the guest's retrace do nothing here. The
  thorough cure is a pointer the guest does not draw (a hardware cursor), which needs an emulated adapter
  that VMDisp9x drives with one [inferred; not tried].
* **Rerun:** `bash ~/flicker/repro/repro.sh [TREE [LABEL]]` in WSL (section 6).


## 1. The harness

`~/flicker/repro/h/harness` is a copy of the v4.1 work's PC harness (`~/v4-work/v41/h/harness`: the
emulator's core built as the board builds it, BOARD=korvo1, Windows 95 headless on a virtual clock of
2 million guest instructions a second) with these additions [verified: the files; the diffs against the
v4.1 harness are in `flicker-repro\harness\*.diff` beside this note].

### 1.1 The board's display pipeline (`-board`)

* **vga_task** (`esp/main/display.c`, the same in v3.9 and v4.1 [verified: `git diff`]): every
  `LCD_FRAME_MS` (30 ms) of virtual time, `pc_vga_step()`, between two `pc_step()` calls (512 guest
  instructions each, as `PC_STEP_COUNT` on the board). The console never owns the display in these runs,
  and nothing sets `full_update`, so the display lock and the speed screen need no model.
* **The panel:** the frame buffer the VGA code draws into is the cache's view; a second buffer is what
  the panel's DMA reads from PSRAM. `redraw()` is `lcd_flush()`: the rows it is given are copied from the
  first to the second, as `esp_cache_msync()` writes them back. Nothing else reaches the second buffer,
  except at a clear: `vga_clear_screen()` is a memset of all 768,000 bytes through a 64 KiB write-back
  cache, so all but its last 64 KiB must be in PSRAM, black, before anything is drawn again; at each
  clear inside a step the panel's rows but the last 40 go black, and that state is kept as the step's
  mid-step picture (`-cache-rows`). [The model of the cache is inferred from its size; the clear path
  was never reached during use, section 3.1.]
* **Two modes:** the drawing in the run loop (the default: the guest waits while a picture is drawn, so
  each picture is an atomic snapshot and every run is the same to the instruction), and `-vgathread`,
  the drawing in a second thread as on core 0, where every 10th picture (`-check-every`) the guest is
  paused, the picture is checked, and one more is drawn and checked: a store whose mark was lost would
  still be missing then.

### 1.2 What is recorded

* **The reference:** after every step, the guest's whole screen converted afresh from a copy of the VGA
  state (`pcr_vga_reference()`: a copy of `VGAState` with its own marks, row marks, column table and
  16-colour cache, cleared and drawn with `full_update`, nothing of the real state touched), held pixel
  for pixel against the panel and against the cache's view.
* **`frames.tsv`**, a line a picture: the guest's mode and DISPI registers; rows written back; what
  happened since the last picture (DISPI writes, VGA register writes by kind, AR index writes with the
  palette address source off, clear and whole-picture requests, clears and where, pixels a clear turned
  black); pixels differing from the reference (and black where it is not), rows; pixels that changed
  for one picture only (blips), and whether the reference had them too; the white and black pixels in a
  box of empty desktop (`-cursor-box`: the pointer whole, in part or not at all); reads of 3DAh; how far
  the guest was into an update of video memory; a hash of the panel.
* **`events.tsv`**, a line an event, with the guest instruction count and virtual time: every DISPI
  write (index and value), every VGA register write but the graphics controller's, the map mask, the
  DAC's data and the bare index registers (`-vgalog-all` logs those too), every clear request and every
  clear with the line of `vga.c` it came from.
* **`bursts.tsv`**, a line for each update of video memory by the guest: stores less than 4,000 guest
  instructions apart are one update (`-burst-gap`); its start, length in instructions, bytes, stores,
  addresses, whether it went through the linear frame buffer, how many pictures were taken while it was
  under way (before its last store), and reads of 3DAh in it and before it.
* **Strips:** around a bad picture (a difference from the reference, a clear over a picture, a blank
  picture, a blip the reference does not have, the pointer broken, a picture taken mid-update; at most
  30 a run, spread out), the panel's pictures before and after, saved; `strips.py` makes sheets, crops
  and animations of them. `-dump T0:T1` saves every picture in a stretch of the session.
* **The hooks:** `instr_vga.py` and `instr_pc.py` edit the build's copies of `vga.c` and `pc.c` (never
  the tree): a call at the end of `vga_clear_screen()` and before each call of it, before each
  `vga_ask()`, at the top of `vga_ioport_write()`, `vbe_write()`, `vga_mem_write*()` and (for 3DAh)
  `vga_ioport_read()`, and after each of the four stores into the linear frame buffer in `pc.c`. Both
  work on v3.9's and v4.1's files [verified: `bin/*/instr.txt`: 7 and 8 clear sites, 4 and 3 requests, 4
  window stores, 4 LFB stores].

### 1.3 The session and the images

* **`sess/plat.txt`**, made into key and mouse files by `session.py`, times from the desktop seen plus
  10 s: idle 10 s; the pointer swept over empty desktop for 16 s (reports every 20 ms, at most 2.5
  pixels, below Windows' acceleration, so the script knows where the pointer is); still 8 s; the Start
  menu by Ctrl+Esc with the pointer over its items (a submenu opens) and closed; Run, `notepad
  c:\windows\win.ini`; two lines typed at the end; the window scrolled a line at a time (Ctrl+Home, 60
  presses of Down); the window moved by keyboard (Alt+Space, M, arrows: the outline); the pointer over
  the window and the desktop. Segments: `sess/plat-segments.txt`. Windows at 2 million instructions a
  second answers slowly, so each step has many seconds [verified: pictures at 6 s intervals of a trial
  run, looked at].
* **`img/base-n16.img`**: a read-only copy of the v4.1 install run's image
  (`~/v4-work/v41/h/runs/gi18s-w41/win95.img`): VMDisp9x installed through Windows' dialogs as
  `TEST-PLAN.md` 13 has it, `VBE480.REG` imported, 800x480 in 16 bits, shut down from the Start menu
  (BUILD-NOTES 23.8). Its start: the network logon (the harness presses Enter), the desktop at 74 s.
* **`img/base-vga.img`** (made by `mkvga.sh`): the pristine `win95.img` (copied) with the standard VGA
  driver at 14M and 320K, Enter on its four dialogs, shut down from the Start menu 12 s after the
  desktop, before IE4's first-run setup comes up (a first try let that setup run: it was still at 36 per
  cent after 2,400 virtual seconds). Started again: the logon, the desktop at 78 s, nothing else for
  250 s [verified: `runs/vga-check`, its final picture looked at].
* For 256 colours and the panel's size in 16 bits, the VBE step's offline installs
  (`~/v4-work/vbe/w95/img/w8-800x480.img`, `w16-800x480.img`, read only); for 640x480 in 16 bits through
  the driver, `img/off-v640x16.img`, made the same way (`controls2.sh`). These are first starts, so
  only the pointer sweep is run on them (`sess/sweep.txt`), which ends before IE4's setup appears.
* BIOS files: the package's `card\tiny386\bios.bin` and `vgabios.bin`; v3.9's VGA runs take
  `previous-v3.9\card\tiny386\vgabios.bin`. 14M with 768K (VBE) or 320K (VGA), gen 3, no FPU, as the
  inis have them.

### 1.4 What the harness does not model [inferred]

* On the board the drawing takes milliseconds on core 0 while the guest goes on (the in-loop pictures
  are instantaneous); the threaded mode covers that on x86, whose memory ordering is stronger than the
  S31's.
* The panel scans at about 35 Hz with no vsync, so a picture is also cut where the scan meets the
  drawing: ordinary tearing, the same on v3.9.
* The guest's speed on the board during this work [unknown]; section 3.7 shows how the results scale.


## 2. The runs [verified: `~/flicker/repro/h/runs/*`, `h/summary-w41.txt`; the tables in `flicker-repro\`]

| Run | Build | Image, mode | What |
|-|-|-|-|
| `w41-n16` | v4.1 (f61337e) | base-n16, 800x480x16, 768K | the session |
| `w41-n16-blocks0` | v4.1, `-vbe-blocks 0` | the same | control |
| `v39-n16` | v3.9 (d370a7a) | the same | control: the old drawing |
| `w41-vga`, `v39-vga` | v4.1, v3.9 | base-vga, 640x480x4 standard VGA, 320K | control: Plat's previous setup |
| `w41-n16-thread` | v4.1, `-vgathread -check-every 10` | base-n16 | the drawing beside the guest |
| `w41-off16`, `w41-off8`, `w41-v640x16` | v4.1 | offline installs: 800x480x16, 800x480x8, 640x480x16 | pointer sweep |
| `spd-n16-1000000`, `-4000000`, `spd-vga-*` | v4.1, `-ips` 1M and 4M | base-n16, base-vga | pointer sweep at other guest speeds |
| `w41-n16-q10k30`, `-q10k60`, `-q20k60`, `w41-vga-q10k30` | v4.1, `-quiet` | base-n16, base-vga | trial pacings (section 4) |
| `w41-n16-dump*`, `w41-vga-dump-sweep`, `lag-*` | v4.1, `-dump` | | pictures of stretches, looked at |

Every run reached the desktop and ended by itself. A session run takes 10 to 25 s of host time.


## 3. What the runs show

### 3.1 Every picture is the guest's video memory, exactly [verified]

* In-loop runs: `w41-n16` 9,252 pictures, `w41-vga` 9,386, `v39-vga` 9,386, every one checked: 0 differ
  from the reference, on the panel or in the cache's view; no row was left unflushed after a step.
* `w41-n16`, `w41-n16-blocks0` and `v39-n16` give the same table, segment for segment and picture for
  picture (the same counts, pointer states and update timings): the drawing from blocks, the full redraw
  of `vbe_blocks = 0` and v3.9's per-pixel loop show the same pictures.
* During the session (about 6,000 pictures a run, the four main runs): 0 DISPI writes, 0 CRT controller
  writes, 0 attribute index writes, 0 clear or whole-picture requests, 0 clears, 0 blank pictures, 0
  reads of 3DAh.
* The driver writes the DISPI registers only to set its mode: twice before the desktop, at 54.8 and
  55.3 s, each ENABLE 0, XRES 320h, YRES 1E0h, BPP 10h, VIRT_WIDTH 320h, VIRT_HEIGHT 1E0h, BANK 0, X and
  Y offset 0, ENABLE E1h [`runs/explore/events.tsv`]. Clears over a picture happen only while starting
  (text to graphics, VGA to VBE): 3 to 4 a run, all before the desktop.
* `w41-n16-thread`: 1,752 checks with the guest paused; 147 pictures had been drawn while the guest
  wrote (transient, as on the board), and in none was anything still missing after one more picture.
  This is x86, whose stores are not reordered as the S31's may be [the S31 unknown].

### 3.2 What the pictures show: the pointer [verified: strips and dumps, looked at]

Ten consecutive pictures of the pointer moving over empty desktop (`w41-n16-dump`, session 14 s; scratch
`pics\n16-sweep-10.png`, animation `pics\n16-sweep.gif`): the tip only, whole, whole, whole, nothing,
the tip only, whole, whole, whole, nothing. Picture by picture over the whole sweep (`W` whole, `p` in
part, `.` none):

    W.pWWp.pWWp.pWWp.pWWp.pWWp.pWWW.pWWp.pWWp.pWWp.WWWp.pWWWWpWWp.pWWp.pWWp.pWWW.pWWp.pWWp...

100 times in the 16 s the pointer goes from whole to broken: about 6 blinks a second, each about two
pictures (60 ms) long. The VGA driver, same stretch (`pics\vga-sweep-10.png`): whole in all ten, and
whole in all but 12 of 463 pictures of the sweep.

With the mouse still and Notepad being typed into (`w41-n16-dump-typing`, 2 s, `pics\typing-pointer.png`):
of 65 pairs of consecutive pictures 61 differ, 52 of them only where the still pointer is, at the foot
of the screen; the pointer is whole in 34 of the 66 pictures, gone in 26 and partly drawn in 6.

### 3.3 The guest's updates [verified: `bursts.tsv`]

* **A move of the pointer** (`w41-n16`, 94.03 to 94.17 s): an update of 1,024 stores (2,048 bytes over a
  32x32 box, the old position), 9,500 to 10,000 instructions; about 7,700 instructions with no store;
  an update of 2,048 stores (4,096 bytes over the 32x32 box at the new position), 18,261 to 18,780
  instructions; the next move about 24.5 ms later. At 2 million instructions a second: 4.8 ms erasing,
  3.9 ms with no pointer, 9.2 ms drawing, about 18 ms of every 24.5. The pointer is drawn from the top
  down, so a picture during the drawing shows its tip.
* **Text, a scroll, a window outline:** each of the guest's drawing operations comes with the pointer's
  erase and redraw, wherever the pointer is: a typed character is one update of rows 314 to 466 (the
  text at rows 314 to 358, the pointer at 435 to 466, its 2,048-byte erase) of about 10,000 instructions,
  then the pointer's 4,096-byte redraw of about 18,500.
* **The standard VGA driver:** one update a move of the pointer, median 6,260 instructions (3.1 ms), the
  pointer whole in 97 per cent of pictures; a typed character's update, median 1,035 instructions, with
  no pointer in it.
* Which code in the driver does this [unknown]: the source of VMDisp9x in `~/v4-work/vbe-research/
  drivers/vmdisp9x` is a later master (767761d) than the release used; it has the pointer drawn by its
  VxD (`vxd_mouse.c`) and `BeginAccess` hiding it for any rectangle, which fits what the runs show, but
  it was not matched to the 1.2025.0.119 binary. No registry setting for the QEMU adapter's pointer was
  found; `HWCursor` is read for the VMware SVGA adapter only (`vxd_svga.c` 434).

### 3.4 How often, by segment [verified: `table-session.txt`, `table-sweep.txt`]

Pictures taken while an update of the guest's was under way (mid), and the pointer in the sweep (whole,
in part, none; stretches with the pointer outside the box left out):

| Segment (seconds) | v4.1 800x480x16: mid | a second | v4.1 VGA 640x480x4: mid | a second |
|-|-|-|-|-|
| idle (10) | 3 % | 1.1 | 0 % | 0 |
| sweep (16) | 57 % | 18.9 | 14 % | 4.6 |
| still (8) | 0 % | 0 | 0 % | 0 |
| Start menu (28) | 21 % | 7.0 | 3 % | 1.0 |
| Notepad opening (42) | 16 % | 5.3 | 1 % | 0.4 |
| typing (24) | 57 % | 19.0 | 7 % | 2.3 |
| scroll (19) | 61 % | 20.2 | 7 % | 2.3 |
| window move (19) | 58 % | 19.4 | 1 % | 0.4 |
| pointer over window (15) | 64 % | 21.1 | 13 % | 4.3 |

| The pointer in the sweep | Whole | In part | None | Broken |
|-|-|-|-|-|
| v4.1 800x480x16 (`w41-n16`; `-blocks0` and `v39-n16` the same) | 269 | 132 | 105 | 46.8 % |
| v4.1 800x480x16, offline install | 259 | 138 | 115 | 49.4 % |
| v4.1 800x480x8 (256 colours) | 258 | 140 | 114 | 49.6 % |
| v4.1 640x480x16 through the driver | 229 | 130 | 103 | 50.4 % |
| v4.1 640x480x4, standard VGA | 451 | 12 | 0 | 2.6 % |
| v3.9 640x480x4, standard VGA | 457 | 6 | 0 | 1.3 % |
| v4.1 800x480x16, drawing in a second thread | 247 | 107 | 93 | 44.7 % |

Plat's four observations, all reproduced: a still desktop does not flicker (idle and still: 0 to 3 per
cent, the pointer whole); motion does; 256 colours and 640x480 through the same driver do it too; the
16-colour standard VGA path does not.

### 3.5 Why "I don't recall it flickering previously"

Previously was the standard VGA driver: its pointer update is a fifth as long and leaves the pointer
whole on screen, and it does not touch the pointer when it draws elsewhere (3.3). Nothing in v4.0 or
v4.1 made the flicker: the same driver on v3.9 gives the same pictures (3.1) [verified in the harness;
on the board inferred].

### 3.6 The guest's speed (Plat's second report) [verified]

Share of the guest's instructions spent inside its updates of video memory (from first to last store):

| | sweep | typing | scroll | move | pointer over window |
|-|-|-|-|-|-|
| 800x480x16 | 55 % | 56 % | 58 % | 56 % | 64 % |
| 640x480x4 VGA | 13 % | 7 % | 8 % | 1 % | 9 % |

The screens at session 134 s and 174 s (`runs/lag-*`, scratch `pics\lag.png`): at 134 s, 800x480 shows
the first typed line only part way ("The quick brown fox jumps over the"; its keys went in from 113 to
119.6 s), VGA both lines whole (keys ended 128.6 s); at 174 s, 800x480 is still scrolling (its keys ended
at 148.8 s), VGA has scrolled and moved the window (keys ended 166 s). So Windows at 800x480 through this
driver is markedly slower on the same core, and most of the difference is the pointer's drawing
[inferred from the shares above]. What the block marks cost on the board is small beside it: about 8
more board instructions a store (BUILD-NOTES 23.2), about 25,000 for a pointer move's 3,072 stores,
against the guest's 36,000 x86 instructions for the move [inferred].

### 3.7 The guest's speed and the flicker [verified: `spd-*`]

| Guest instructions a second | 800x480x16: pointer broken | VGA 640x480x4 |
|-|-|-|
| 1 million | 77.7 % (410 of 528) | 1.7 % |
| 2 million (the other runs) | 46.8 % | 2.6 % |
| 4 million | 23.1 % (120 of 520) | 1.5 % |

The share falls about as the guest's speed rises: the pointer's update is a fixed number of guest
instructions. On a real PC of the time it lasts a fraction of a millisecond and a CRT almost never shows
it [inferred].


## 4. What tiny386 can do about it (evidence for the Fix agent)

### 4.1 Picture pacing on the guest's quiet moments [verified: `-quiet`, table-quiet.txt]

`-quiet Q:D` is a trial pacing in the harness, not the board's: a picture that is due waits until the
guest has made no store into video memory for Q instructions, or until it is D ms late.

| Run | sweep: broken | sweep: pictures (a second) | typing / scroll / move: mid | their pictures a second | late, mean / most |
|-|-|-|-|-|-|
| v4.1 as built | 46.8 % | 531 (33) | 57 / 61 / 58 % | 33 | 0.1 / 0.3 ms |
| Q 10,000, D 30 | 1.6 % | 323 (20) | 36 / 40 / 37 % | 20 | 20 / 30 ms |
| Q 10,000, D 60 | 1.3 % | 323 (20) | 24 / 24 / 21 % | 17 | 30 / 60 ms |
| Q 20,000, D 60 | 45 % | 185 (12) | 32 / 32 / 31 % | 15 | 36 / 60 ms |
| VGA, Q 10,000, D 30 | 1.4 % | 508 (32) | 0 / 3 / 0 % | 31 | 1 to 3 / 30 ms |

* Q must be longer than the pointer's pause between erase and draw (7,700 instructions) and shorter than
  the quiet between moves (about 13,000 at 41 moves a second): 10,000 fits, 20,000 does not. The
  thresholds are in guest instructions, which keep the same meaning at any guest speed; in time they
  would have to follow it [inferred].
* It does little while the guest keeps drawing, where its pauses are shorter than Q, and it costs
  pictures and up to D of lag.
* On the board vga_task cannot see the guest's instruction count of the last store without help: the
  store paths would have to leave one (a store more each, cost on the S31 [unknown]), or vga_task could
  look at the mark map twice a few milliseconds apart [inferred; neither tried].

### 4.2 What does not help [verified unless marked]

* `vbe_blocks = 0`: the same pictures (3.1).
* Taking each block, or the whole picture, in one go: the in-loop pictures are already atomic
  snapshots, and they show the broken pointer; it is in video memory.
* Following the guest's retrace: neither driver reads 3DAh while Windows is in use (3.1), so there is
  nothing to follow.
* The X/Y offset clear, the CRT controller's deferred clear and the cache model: never reached during
  use.

### 4.3 Larger options [inferred, not tried]

* **A hardware pointer.** If the guest never drew the pointer, the drawing could put it on each picture
  itself: no flicker of the pointer and none of its cost (3.6). VMDisp9x drives a hardware cursor only
  for adapters that have one; in its release the VMware SVGA II driver (`vmwsmini.drv`, `HWCursor` in
  its registry configuration) [from the source and the release's file list, `~/v4-work/vbe-research/
  downloads/vmdisp9x-119b/x`]. tiny386 would need that adapter's registers and command FIFO, at least
  the cursor and screen-update commands: a large piece of work.
* **Settled blocks:** draw a block only once it has not been written for a while, other blocks at once.
  The pointer's erase and redraw share their updates with the text drawn near them (3.3), so this would
  behave like 4.1 for those rows and better elsewhere [inferred; the harness can measure it once a tree
  has it].
* **A faster guest** shrinks the windows in proportion (3.7).


## 5. The brief's suspects and Plat's leads

| | Finding |
|-|-|
| (a) a clear of the panel, blocks repainted only where written | no clear or request while Windows is in use; every picture equals the reference [verified] |
| (b) the clear and the redraw in separate pictures | no clears to separate; at the clears while starting, the redraw is in the same step [verified] |
| (c) marks racing between the cores, a missing fence | in-loop: nothing missing in 9,252 pictures; threaded: nothing missing after one more picture in 1,752 checks [verified on x86; RISC-V unknown] |
| (d) the panel scanned while written, msync incomplete | modelled: nothing left unflushed; real tearing and msync not modelled, and not needed to produce the flicker [inferred] |
| (e) the driver writing DISPI registers as mode changes | two mode sets while starting, none after [verified] |
| Pictures taken mid-update | yes: the cause [verified] |
| Blocks not lining up with rows, looking blocky | no: the same pictures with whole redraws; the blocky look is the pointer drawn part way [verified] |
| The VGA driver waiting for retrace | no 3DAh reads while in use; its pointer update is short and leaves the pointer whole [verified] |
| Snapshot each block in one go | does not help (4.2) |
| Take the picture when the guest is quiet, bounded | helps the moving pointer, costs pictures, partial elsewhere (4.1) |


## 6. Rerunning (for the Fix agent)

In WSL Debian:

    bash ~/flicker/repro/repro.sh                       # v4.1, the session, 800x480x16
    bash ~/flicker/repro/repro.sh ~/v4-work/v42/tree v42  # a fix's tree, runs named v42-*
    CONTROLS=1 THREADED=1 bash ~/flicker/repro/repro.sh ~/v4-work/v42/tree v42

It builds the tree with the board's flags and the hooks, makes what it needs once (the tag exports, the
BIOS files, `base-n16.img`; `base-vga.img` through `mkvga.sh` with `CONTROLS=1`), runs, analyses
(`analyse.py`, `table.py`), makes strips (`strips.py`), and writes `~/flicker/repro/h/summary-LABEL.txt`.
`CONTROLS=1` adds `vbe_blocks = 0`, v3.9 at 800x480x16 and both at 640x480x4; `THREADED=1` the threaded
run; `EXTRA="..."` passes options to the main run (for instance `-quiet 10000:30`). v4.1's summary is
`h/summary-w41.txt` (copy: `flicker-repro\summary-w41.txt`). A fix proves itself by the sweep's
"ptr w/p/n" and the "mid" columns of the typing, scroll and move segments, with "diff", "clr" and "blk"
still 0.

* A fix in `vga.c` or `pc.c` is built and measured as it is. If it renames what the hooks look for
  (`#include "unaligned.h"` and `"pc.h"`, the signatures of `vga_clear_screen()`, `vga_ioport_write()`,
  `vbe_write()`, `vga_mem_write*()`, `vga_ioport_read()`, the four LFB store statements, `vga_ask(&s->...)`),
  `instr_*.py` stop with a message naming it.
* A fix in `esp/main/display.c` (when vga_task draws) is not in the build: the harness's model of
  vga_task is `board_step()` and the run loop's test before it in `headless.c` (where `-quiet` sits), and
  has to be given the same rule.
* `controls2.sh` (256 colours, 640x480x16, the `-quiet` trials) and `speeds.sh` (1M and 4M) rerun the
  other tables. Pictures of a stretch: `-dump T0:T1`, then `anim.py RUNDIR X0 Y0 X1 Y1 ZOOM OUT`.
* Every run ends by itself (`-after-desktop`, `-vsecs`, a wall limit), and nothing is left running.


## 7. What is not known

* How the board's own timing changes the share: the guest's speed in this work, the drawing's duration
  on core 0, the panel's scan (1.4, 3.7).
* The S31's memory ordering for the marks (3.1, c).
* The driver code that draws the pointer, matched to the release binary (3.3).
* Whether a hardware cursor through an emulated VMware SVGA II would work with tiny386's PCI and
  Windows 95 (4.3).


## 8. Files

In WSL `~/flicker/repro` (scripts read-only to others; nothing outside it was changed):

| Path | What |
|-|-|
| `repro.sh` | the one script (section 6) |
| `mkvga.sh`, `controls2.sh`, `speeds.sh` | the VGA image; the other depths and the `-quiet` trials; the speeds |
| `setup.sh`, `deploy.sh`, `run*.sh`, `a*.sh`, `s*.sh` | how this investigation set up and looked (deploy copies from the session's scratch folder) |
| `t-v4.1`, `t-v3.9` | `git archive` of the two tags |
| `sess/` | `plat.txt`, `sweep.txt`, `shut.txt`, the segment files, the key and mouse files made from them |
| `h/harness/` | the harness: `headless.c`, `build.sh` (calls the hooks), `instr_vga.py`, `instr_pc.py`, `session.py`, `analyse.py`, `table.py`, `strips.py`, `anim.py` and the v4.1 harness's other files |
| `h/img/` | BIOS files, `base-n16.img`, `base-vga.img`, `off-v640x16.img` (read-only copies; pictures of Windows and Windows itself stay here) |
| `h/bin/`, `h/runs/`, `h/summary-w41.txt` | builds, runs (log, frames, events, bursts, analysis, strips, dumps), the summary |

In this package, `research\flicker-repro\`: the harness's text files and scripts, the session files,
the diffs against the v4.1 harness, `summary-w41.txt`, the three tables, and for each run its command,
analysis and summary lines; no pictures. `SHA256SUMS` was not made again for these files.

In the session's scratch folder (`C:\Users\Owner\AppData\Local\Temp\claude\D--Repos-esp32-s31-linux\
99e6c67c-c5d4-41b3-a911-64bbdd3332f8\scratchpad\fr\pics`), kept off the package: `n16-sweep-10.png`,
`n16-sweep.gif`, `n16-sweep-crop.gif`, `typing-pointer.png`, `n16-typing.gif`, `vga-sweep-10.png`,
`vga-sweep.gif`, `scroll2-12.png`, `lag.png`, and the run strips under `pics\n16`.
