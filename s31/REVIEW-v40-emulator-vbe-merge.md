# Review of v4.0: the upstream merge, the VBE drawing and the VGA BIOS

2026-10-23, Perth time. An independent review of v4.0 (tag `v4.0` = 8d65b1d on branch `v4` in WSL
`~/tiny386-idf-scratch/v3/tiny386`, on `v4.0-base` = b370a75 and the merge d2229a4), through one
lens: the merge with upstream tiny386 4ce2e06 (every conflict, the PIT cap on the new `i8254.c`,
the task switch fixes, nothing of v3.9 lost), the VBE work (DISPI registers, mode list, depth
conversions, the bank window, the marks and their fences, the 16-bit loop and the write-back of
changed rows), the SeaVGABIOS change, and the PC evidence, rerun here.

I changed nothing in the tree or the package except this file. My work is in WSL
`~/v4-work/review-emulator-vbe-merge` (trees exported with `git archive`, a private clone, my own
copy of the PC harness, runs, probes). Nothing was flashed, no serial port was opened, nothing was
committed or pushed, `~/tiny386-upstream` and `D:\Repos\tiny386` were not touched, and every
process I started ended by itself (none left running, checked with `ps`).

Tags: [verified] seen in a run, a build, a disassembly or a named source line in this review;
[inferred] follows from verified facts; [reported] someone else's figure, not checked here;
[unknown] only the board can say.


## 0. In one screen

* **The merge is right.** I redid it in a private clone: exactly two files conflict (`i386.c`, one
  region; `vga.c`, six), and each is resolved as `research\UPSTREAM-MERGE-v4.md` says. The PIT cap
  and `timer_catchup` sit unchanged beside upstream's one-shot modes; upstream's task switch fixes
  (the new task's flags, the error code written at the new task's CPL) are in; `git diff v3.9 v4.0`
  touches nothing of v3.9 outside the merged files and the VBE work. Upstream master is still
  4ce2e06 today (`git ls-remote`) [verified].
* **The VBE drawing is sound.** I read every line of be62009 and 574334c. Every store path the guest
  has into video memory in a VBE packed-pixel mode is marked; the marks and fences are ordered
  correctly for two cores under RISC-V's memory model; the depth conversions are exact; the bank
  fix is right [verified by reading and by runs; the two-core ordering inferred from RVWMO, the PC
  cannot show it].
* **The evidence reproduces.** From my own `git archive` of the tag and my own harness copy:
  host tests 35 of 35; BVBE480 16 runs (both boards, 768K, 1M, 1152K, 1536K, 2M, threaded, and
  `vbe_blocks = 0`), every picture right; v4.0-base wrong at 768K as claimed; 7 Windows 95 runs at
  8, 16, 24 and 32 bits and the DOS board's 800x600 give the VBE step's END lines and final pictures
  exactly; 184,622 periodic pictures held against full redraws, none differing; the driver install
  through Windows' dialogs again at an 800x480 desktop; `vgabios.bin` rebuilt from the package's
  kit byte for byte [verified].
* **I looked at the pictures myself**: 800x480 desktops at 8, 16, 24 bits, the DOS board's 800x600,
  the install's dialogs, the registry import, the restart. Colours right, taskbar the full width,
  no black bars once the driver is in [verified].
* **Findings: no blocker, no major; 3 minor, 7 notes** (section 6). The one I would fix before
  release: v4.0 puts a mark and a `fence rw,w` on every chain-4 store into video memory, including
  mode 13h's, where nothing reads the marks (M1, a one-line guard). The merge's reason for keeping
  `unaligned.h` over upstream's packed accessors was measured with the wrong compiler flags; the
  outcome is harmless but the note and the new comment in `i386.c` say something untrue of the
  board build (M3).


## 1. What I did

| Step | Where (WSL, under `~/v4-work/review-emulator-vbe-merge`) | Result |
|-|-|-|
| Export v3.9, v4.0-base, v4.0, `upstream-20261022`, `pristine` | `src/` (`git archive`) | [verified] |
| Merge v3.9 with `upstream-20261022` again, in a private clone | `clone/`, `conflicts.txt`, `merge-attempt.txt` | 2 files conflict, as reported [verified] |
| Hunk by hunk: `git diff v3.9 v4.0-base`, `pristine..upstream`, `v4.0-base..v4.0` | `d/`, `r3.sh` to `r6.sh`, `r28.sh` | section 2 and 3 |
| Host tests at the tag, in my clone | `ht.log` | 35 of 35, test_vbe 6,790 checks on each board [verified] |
| Package patches against the tags; v4 patch applied to an export of 4ce2e06 | `r49.sh`, `applytest/` | both patches equal the git diffs; the applied tree equals v4.0 [verified] |
| Harness: a copy of `~/v4-work/vbe/h/harness`, binaries built from my export | `h/`, `hc/` (diff check) | its `headless.c` is the package's `headless-v4.0.diff` plus `-keyfile`, `run.sh` plus `FDA`/`FDB` [verified] |
| BVBE480 rebuilt from `checks\vbe480\bvbe480.S` | `guest/` | `BVBE480.COM` identical to the package's [verified] |
| 16 BVBE480 runs | `h/runs/vbe480-*`, `r23.sh` | section 5.1 |
| 12 Windows 95 runs at the native size, and the install through the dialogs on both trees | `h/runs/W-*`, `X-*`, `gi18-*` | section 5.2 |
| Edge-case probe of the drawing | `probe.c`, `probe/` | section 3.6 |
| `vgabios.bin` rebuilt with the package's `bios\tools\build-bios.sh`, `TOOLCHAIN=host` | `biosbuild/` | byte for byte [verified] |
| Disassembly of the release ELF and test compiles with the board's own flags | `dis/`, `ua/` | sections 3.5 and M3 |


## 2. The merge [verified unless marked]

### 2.1 The conflicts, redone

`git merge upstream-20261022` on v3.9 in my clone: auto-merges `adlib.c`, `i8254.c`, `kvm.c`,
`Makefile`, `rawdraw/main.c` and the Android files; conflicts in `i386.c` and `vga.c` only.

| Conflict | Resolution in d2229a4 | Verdict |
|-|-|-|
| `i386.c` task gate: v3.8's error code saved before `task_switch()` and pushed with TRY1, against upstream's TRY and `cpu->excerr` after the switch at `cpu->cpl` | v3.8's form with upstream's `cpu->cpl` in the `translate()` call | right: `task_switch()` never returns false (every failure in it is TRY1) and upstream's own `TRY1(call_isr(...))` aborts on a false return, so the two behave alike; `call_isr()`'s restore after a completed switch would be wrong, which TRY1 avoids |
| `vga.c` cursor glyph (text only) | upstream's `FF8` form | same 128 bytes |
| `vga.c` two comments, VBE clear comment | upstream's text | code identical on both sides |
| `vga.c` line loop (upstream's `line_bytes` and black line against v3.9's panning, split screen, fast 16-colour path, dirty rows, slack) | v3.9's loop; a line beginning past the end now reads the slack from its start | right; see note N3 for "black" |
| `vga.c` clear on a CRT controller write | v3.9's deferred clear where it had it; no clear where v3.9 cleared at once (one core, upstream's flags) | right; the board's code path is unchanged |

Upstream's half that merged cleanly is present: `cpu->cc.mask = 0` after loading the new task's
EFLAGS (0642735), and `Jv` sized by `opsz16` (d81e225).

### 2.2 The PIT

`git diff v3.9 v4.0-base` of `i8254.c` is upstream's `pristine..upstream-20261022` change to the line,
hunk for hunk (the same 10 hunks, the same text, only the line offsets differ). The cap's region
(`i8254_max_owed` to `i8254_init`) differs from v3.9 only by `switch(pit_mode(s))`, the new
`case 0: case 4:` branch and the `abort()` replaced. `pit_load_count()` still zeroes
`last_irq_count`, from which the cap counts; the "avoid wraparound" reload in modes 2 and 3 now also
clears `reload_pending`, harmlessly. `esp_main.c` still sets `i8254_max_owed` from
`timer_catchup` (line 1299). Host test 19 (28 checks) passes in my clone, and its run against v3.9's
`i8254.c` passes 13 and stops with `abort()` at mode 0, as the merge step said.

### 2.3 Nothing of v3.9 lost

`diff -rq` of my exports v3.9 and v4.0: outside `tests/host` and the Android files, the only files
that differ are `adlib.c`, `i386.c`, `i8254.c`, `kvm.c`, `Makefile`, `rawdraw/main.c`, `vga.c`
(merge and VBE), `pc.c`, `pc.h`, `vga.h`, `esp/main/board_korvo1.h`, `board_dosboard.h`,
`esp_main.c` and `esp/version.txt` (VBE and version). I read each hunk: every `esp/main` change is
the VBE work's (`VGA_DIRTY_BLOCKS`, `vbe_blocks`, the 64K rounding); `adlib.c` and `kvm.c` are
upstream's text exactly, v3.9's `AUDIO_RATE` kept. The one clean auto-merge the merge step set back
(b7980ab, `I386_ALIGNED_ONLY` to packed accessors) is set back as described; its stated reason is
wrong for the board (M3), its effect is nil.


## 3. The VBE work [verified by reading unless marked]

### 3.1 DISPI registers

* **X and Y offset** (`vga.c` 2802 to 2813): with `VGA_DIRTY_BLOCKS` a write stores the register
  and runs `vbe_fixup_regs()` only, so no clear and no black flash. The drawing sees the new start
  (`vbe_start_addr`) and draws the whole picture again (`all` in `vga_vbe_refresh()`). QEMU also
  runs `vbe_update_vgaregs()` here, which re-asserts chain 4, the graphics mode and the CRTC size
  registers; leaving it out is harmless unless a guest rewrote those under VBE (note N5). My probe:
  an odd start (X offset 1 at 8 bits, which falls back to the old loop), page flips by Y offset
  with the hidden page written: every picture equal to a full redraw (3.6).
* **Enable, size, depth, virtual size**: unchanged, each asks for a clear, which also sets
  `vbe_drawn = false`, so the next picture is drawn in full. This is what makes the marks safe at
  every mode change.
* **Bank** (2827 to 2839): `val % banks`; for a power of two the same as the old mask, otherwise
  every existing bank where its number says. BVBE480 at 768K and 1152K and the host test show it
  [verified].
* **GETCAPS** unchanged: XRES the panel's width (800), YRES its height, BPP 32. SeaVGABIOS checks
  only the width and the memory, so 800x600 stays listed on the Korvo-1 and is shown shrunk, as
  documented.

### 3.2 Coverage of the marks

Every store into video memory that can change a VBE packed-pixel picture is marked: the four
chain-4 paths of `vga_mem_write*()` (2990 to 2996, 3040 to 3046, 3086 to 3093, 3136 to 3148) and
`iomem_write8/16/32()` and `iomem_write_string()` in `pc.c` (650 to 772). I checked that nothing
else reaches the frame buffer: `in_iomem()` sends every access at or above 0xE0000000 to these
callbacks (`i386.c` 806); `cpu_register_mem()` is a no-op outside the KVM build; the string fast
path is taken only for MOVS from RAM (a screen-to-screen copy goes element by element through
`store32()`); STOS has no string path; DMA reaches only RAM below `phys_mem_size`. The VBE clear on
enable writes behind the marks but always comes with a full redraw. The planar and odd/even write
paths are not marked, which matters only in the case of note M2.

### 3.3 Two cores

Writer (core 1): data store, `fence rw,w`, then the block's byte(s) set. Drawing (core 0): reads a
byte, clears it, and after the whole scan one `fence rw,rw`, then reads video memory. Under RVWMO:
if the drawing reads the byte set by a store, that store's data is visible to its later reads; if
the writer's byte store lands before the drawing's clear in the byte's coherence order without
being read, then data store, byte store, clear and the drawing's read are in that order in global
memory order and the read sees the data; if it lands after, the byte stays set for the next
picture. So no write can be lost [inferred: RVWMO's ordering rules applied to `vga.h` 63 to 71 and
`vga_take_marks()` 1332 to 1370]. The PC (x86, TSO) cannot test this; ThreadSanitizer cannot model
the fences, as the VBE step says. The S31's shared data cache makes the two cores coherent in any
case [inferred].

### 3.4 Depth conversions and the rows

`vbe_row()` (1395 to 1446) against the old loop (2171 to 2211): 8 bits through the same palette; 15
bits `(v & 0x001f001f) | ((v & 0x7fe07fe0) << 1)`, two pixels at a time with bit 15 of the low pixel
masked so it cannot spill, the old loop's `(k & 0x1f) | ((k & ~0x1f) << 1)` truncated to 16 bits;
16 bits copied; 24 bits four pixels from three words with the byte shuffles right; 32 bits the
top 5, 6 and 5 bits. Identical colours [verified: by hand, and test_vbe's 6,790 checks against the
old loop on each board]. `vga_take_marks()`: the rows of a block are `ya..yb` of the picture, and
for a shrunk picture the screen rows `ceil(ya*sh/h)` to `floor(((yb+1)*sh - 1)/h)`, which is exactly
the set of rows whose source line falls in the block; nothing overflows for these sizes.

### 3.5 The 16-bit loop and the write-back

The 16-bit loop is 6 instructions for two pixels when the screen already shows them, 8 with the
store [verified: `checks\vbe-loops-v4.0.txt`, the release ELF]. Rows that changed go to
`vga_redraw_rows()` in bands (rows under 16 apart merged); on the Korvo-1 that is `lcd_flush()`,
an `esp_cache_msync()` write-back of those rows only; unchanged rows are neither stored nor written
back, and after the board's console `give_back()` clears the panel, flushes it and asks
`full_update`, which redraws everything. I found no path where a changed row is not handed to
`redraw()`.

What a store costs [verified: disassembly of `images\korvo1-dualboot\tiny386.elf` and
`previous-v3.9\...\tiny386.elf`]: `iomem_write16()` 12 instructions in v3.9, 18 in v4.0 with one
`fence rw,w`; `vga_mem_write()`'s chain-4 byte store through the 64 KiB window at A0000h (mode
13h's and the VBE bank window's) 26 in v3.9 and 34 in v4.0 with one fence (24 and 32 with the
128 KiB map), in every chain-4 mode, VBE or not (M1).

### 3.6 My probe of the edges [verified: `probe.c` linked with v4.0's `vga.c` and the Korvo-1 header]

| Case | Pixels differing from a full redraw |
|-|-|
| 640x480x8 filled through the frame buffer | 0 |
| chain 4 turned off, 4,000 bytes written through the window, chain 4 back on, all between two pictures | **992** (M2) |
| display start moved to an odd byte (old loop), writes there, moved back | 0, 0, 0 |
| 800x480x16, virtual height 960: hidden page written, Y offset flip, page 0 changed while hidden, flip back | 0, 0, 0, 0 |
| a 16 and a 32-bit store at the last bytes of the last bank (768K) | 1 byte of the slack no longer zero; a picture whose lines all begin past the end then has 480 pixels not black (N3) |

### 3.7 Other details checked

* `vga.h` turns `VGA_DIRTY_BLOCKS` off for any build the loop does not fit (not BPP 16, no
  `VGA_REDRAW_DIRTY_ROWS`, scaling, big-endian); `vga.c` and `pc.c` both include it first and get
  the board header through `-include`, so they cannot disagree and call `vga_dirty_mark()` on a
  NULL map.
* The 1:1 path requires every alignment it assumes (width, start, line length, both buffers);
  anything else falls to the old loop and resets `vbe_drawn`. A line read never runs past the
  16 KiB slack (`w * bytes <= VGA_MEM_SLACK`, a start past the end clamped to the slack).
* Allocations: the map `(vga_mem_size + 16K) / 1K + 1` bytes (at least the 128 KiB window's), the
  row marks, the shrink table, all in the device window; sizes match 22.2's 3,488 and 3,744 bytes
  [inferred: arithmetic].
* `settle_memory()` rounds `vga_mem_size` down to 64K before anything is sized from it.
* The new 16 and 32-bit LFB reads (`pc.c` 688 to 723) keep the bounds of the byte path; their
  overflow edge at an address of 0xFFFFFFFF is the byte path's too.


## 4. SeaVGABIOS [verified]

* `bios\patches\seabios-korvo-800x480.patch`: five entries appended to `svga_modes[]` (1A0h
  MM_PACKED 8 bits, 1A1h to 1A4h MM_DIRECT 15, 16, 24, 32), 8x16 characters, after 19Eh. Nothing
  else. `bochsvga_setup()` drops each by width (800 from GETCAPS), depth and memory as for every
  mode; the 4F00h list lives in the info block's 222 reserved bytes (110 modes), far above the 34
  that fit at 1M.
* BVBE480 at 1152K, a size nobody had run: 1A3h (24 bits) listed and every one of its pictures
  right, 1A4h not listed, as 22.4 worked out by arithmetic.
* `TOOLCHAIN=host bash bios/tools/build-bios.sh` with the package's own kit (it clones SeaBIOS
  81ec9ec0 from github.com/coreboot/seabios): `vgabios.bin` SHA-256 4aeae74f..., byte for byte the
  card's; `seabios-korvo-v4-src.tar.xz` byte for byte the package's; version string
  `rel-1.17.0-20-g81ec9ec0-dirty-20261023_000000-korvo-v4`.


## 5. The PC evidence, rerun

### 5.1 BVBE480 [verified: `h/runs/vbe480-*`; the package's `vgabios.bin` and `bios.bin`]

| Build, board, video memory | Pictures | Result |
|-|-|-|
| v4.0 Korvo-1 768K, 1M, 1536K, 2M | 38, 52, 67, 72 | every one right, none differing from a full redraw |
| v4.0 Korvo-1 768K and 1536K, drawing in a second thread | 38, 67 | every one right |
| v4.0 Korvo-1 1152K | 55 | 54 right; the checker says "1 WRONG" because one guest log line was split by the harness's own heartbeat line (N1) |
| v4.0 DOS board 768K, 1M, 1536K, 2M; 1M threaded | 38, 52, 67, 72; 52 | every one right |
| v4.0 Korvo-1 1M and 768K with `-vbe-blocks 0` | 52, 38 | every one right |
| v4.0-base Korvo-1 768K; 1M | 38; 52 | 15 wrong (all through the window: the bank fault); every one right |

These match `checks\vbe480\results-v4.0.txt` line for line where both ran.

### 5.2 Windows 95 at the native size [verified: `h/runs/W-*`, `X-*`, `gi18-*`]

The images with VMDisp9x installed offline are the VBE step's (`~/v4-work/vbe/w95/img`, read only,
copied for every run by `run.sh`); the driver was used only inside those copies, in WSL.

* **Seven runs repeating the VBE step's** (16 bits at 768K with the mouse moving and an audit of
  every picture, the DOS board at 1M with the mouse and with the audit, 8 bits at 768K, 24 bits at
  1152K, 32 bits at 1536K with the mouse and with the audit): each gives the step's own `END` line
  (virtual time, instruction count, registers) and its final picture byte for byte. All at the
  desktop; 59,640 audited pictures, none differing.
* **Five longer runs** (the mouse moving every 50 ms, each depth and the DOS board, to 1,500 M
  instructions): 124,982 audited pictures, none differing. (Without `PCR_IGNORE_MODE=1` the
  harness does not press Enter on the network logon, so these sat at it with the mouse moving; the
  drawing was exercised all the same.)
* **The install through Windows' dialogs** (the step's key script `gi18`, a fresh copy of the
  pristine image, `vbe-reg.img` in A:, the driver's floppy, SHA-256 96eed45b..., in B:): the plain
  v4.0 build reaches the 800x480 16-bit desktop with the same final picture as the step's run; the
  step's run was a `DEBUG_VBE` build, whose prints are charged as UART time, so its `END` line
  differs (N6). One audited picture differs from a full redraw, at the first restart (565 s, 255,872
  pixels), and v4.0-base shows the same at the same moment (564.7 s): the text-mode drawing's, not
  VBE's, and gone at the next picture.

The pictures I looked at: the final pictures of the 8, 24 and 32-bit runs and the DOS board's, the
16 and 24-bit reruns, and the install's dialogs (640x480 at 16 colours centred before the driver,
Display Properties, the registry import, the restart dialog, the 800x480 desktop after). The
desktops fill the panel, the taskbar runs the full width, icons and colours are right at every
depth, the 800x600 picture fills the DOS board's screen. Pictures of Windows stay in the session's
scratch folder, off the package.


## 6. Findings

### Minor

**M1. Every chain-4 store pays the mark and a fence, VBE or not.** `VRAM_WRITTEN()` in the four
chain-4 paths of `vga_mem_write*()` runs whenever chain 4 is on: mode 13h and every BIOS 256-colour
mode, where the marks are never read (those pictures are drawn by the old loop). On the board a
chain-4 byte store through mode 13h's window went from 26 to 34 instructions, one of them
`fence rw,w` [verified: disassembly of both release images, `vga_mem_write()`'s memory map 1
path]. DOS games in mode 13h (what the DOS board is for) pay this on
every pixel they write; what it costs is [unknown], the fence's price on the S31 being the
unknown 22.13 already names. Mode X games, Windows' standard 16-colour driver and text are not
affected (planar and odd/even paths, unmarked) [verified: source]. Fix: mark only while VBE is on,
`if (vbe_enabled(s)) VRAM_WRITTEN(...)` in `vga.c` (and, if wanted, `vga_dirty_blocks` as well in
`vga.c` and `pc.c`). It is safe: the only way into a VBE mode is `vbe_write()` of ENABLE, which
always asks for a clear and so a full redraw, and the check runs on the core that sets the
register [inferred: `vbe_write()`, `vbe_update_vgaregs()`, `vga_refresh()`]. With
`vga_dirty_blocks` in the guard too, `vbe_blocks = 0` would also become a true A/B for the fence's
cost on the board; today the marks and fences stay with it (22.2 says so), so it cannot tell.

**M2. Writes with chain 4 switched off under VBE can be missed.** If a guest in a VBE 8 to 32-bit
mode turns chain 4 off, writes through the window (odd/even or planar paths, unmarked) and turns
it on again before the next picture, those writes are not drawn until their rows are written again
or something forces a full redraw [verified: probe, 992 pixels]. If a picture falls while chain 4
is off, the old loop draws it and the next VBE picture is full, so only a toggle within one
picture's 30 ms is missed. No Windows driver or VBE program I know does this [inferred]; QEMU would
draw it (its dirty pages cover every write). Fix, if wanted: count a change of SR04's chain-4 bit
under VBE as a request for a full redraw without a clear (a counter like `force_clear_req`), or
leave it documented; `vbe_blocks = 0` is the way round on the board.

**M3. The merge's case for keeping `unaligned.h` was measured with the wrong flags.**
`UPSTREAM-MERGE-v4.md` 4.5 and the comment the merge put in `i386.c` (v4.0 lines 267 to 272) say
that with the board's compiler `unaligned.h` makes one store for an aligned word where upstream's
packed form always makes four byte stores. The test (`~/v4-work/merge/ua/ua.s`) was compiled with
the toolchain's default `rv32gc` (`.attribute unaligned_access, 0`), not the board's
`toolchain/cflags` (`-march=rv32imafcb_zicsr_zifencei_zcb_zcmp_zcmt_xesploop_xespv -mtune=esp32s31`,
under which `-mstrict-align` is off). With the board's flags [verified: `ua/t3.o`, `ua/p.o`]:

* `unaligned.h`'s `ldu16/ldu32` compile to one `lhu`/`lw` at any address, its `stu16/stu32` to
  one `sh`/`sw` at any address behind a dead alignment test (`bexti` and a branch);
* upstream's packed accessors compile to one `lhu`/`lw`/`sh`/`sw`, without the dead test;
* the whole `i386.c` is 202,990 bytes of text with `unaligned.h` and 208,057 with the packed form
  (different inlining), so they are not identical either.

So on the board `I386_ALIGNED_ONLY` does not keep 16 and 32-bit accesses aligned, despite what
`unaligned.h` and `board_korvo1.h` say: guest RAM's `load32()` is a plain `lw` at any address in the
release image, and v4.0's new LFB reads are plain `lhu` and `lw` (`iomem_read16` at 40018e5c,
`iomem_read32` at 40018fe2) [verified: disassembly]. The S31 evidently does misaligned scalar
accesses in hardware, or v3.9 could not run Windows 95 at 2 M instructions a second [inferred], so
nothing is broken and keeping v3.9's code (measured on the board) is still the safe choice. What
should change is the text: the comment in `i386.c`, 4.5 and 9 of the merge note, and the "packed
stores slower" decision put to Plat, which is no reason either way.

### Notes

**N1. The BVBE480 checker counts a split log line as a wrong picture.** At 1152K the guest's
`@info m0143.listed 0` reached `log.txt` cut in two by the harness's `[harness] beat` line, so
`vbe480check.py` reported `m0143: not looked at` and "1 WRONG" although all 54 pictures were right
[verified: `h/runs/vbe480-wt-korvo1-1152K/log.txt` lines 359 to 361]. Tooling only; the harness's
lines and the guest's debug port share a stream unsynchronised.

**N2. `checks\vbe480\headless-v4.0.diff` is not the whole harness used.** The live harness also has
`-keyfile` (the install run's keys) and `FDA`/`FDB` in `run.sh` [verified: `hc/`]. Worth adding to
the diff so the install run can be repeated from the package alone.

**N3. "Black" past the end of video memory is mostly black.** The chain-4 16 and 32-bit paths do
not bound a store at the window's last bytes, so up to 3 bytes land in the slack, and a line that
begins past the end then shows them (probe: 1 byte, 480 pixels). At 8 bits such a line is palette
index 0, not black. Both predate v4.0; upstream's `memset` gives true black. Cosmetic.

**N4. The DOS board's harness binary predates the `vbe_blocks` switch.** The VBE step's
`bin/wt-dosboard` (built 09:05) has a `vga.c` that differs from the tag's only by the
`vga_dirty_blocks` switch, at its default [verified: diff]. My DOS board runs at the tag give the
same BVBE480 results and the same Windows `END` lines and pictures, so nothing rests on it.

**N5. Offset writes no longer re-assert the VGA registers.** QEMU's `vbe_ioport_write_data()` runs
`vbe_update_vgaregs()` for X and Y offset writes; v4.0 does not, to avoid the clear. Harmless unless
a guest changes chain 4, the graphics mode or the CRTC size under VBE and then relies on a pan to
restore them [inferred]. If it ever matters, split `vbe_update_vgaregs()` into the register part and
the clear request.

**N6. The install evidence was gathered on a `DEBUG_VBE` build.** The record says so
(`checks\win95-driver-install-v4.0.txt` line 3) and the `.REG` runs used it too; its prints are
charged as UART time, so its instruction counts are not the release build's. My rerun with the
plain build and v4.0-base (5.2) closes that.

**N7. The board unknowns stand as 22.13 lists them**, with one addition from M1: the fence's cost
shows up in DOS mode 13h programs as well as in Windows at 800x480. If Plat runs BENCH or a mode 13h
game on v3.9 and v4.0, the difference is that cost [unknown].


## 7. For Plat

* Nothing in this review stops you trying v4.0 as TEST-PLAN.md 13 says.
* If a DOS game feels slower on v4.0 than on v3.9, M1 is the first suspect; the guard is one line
  and takes the cost away outside VBE modes.
* The `unaligned.h` against packed decision (merge note 9): on the board both compile to the same
  single loads and stores; there is no speed reason to choose. Keeping v3.9's (as now) is fine.
* The upstream PRs (bank fix, LFB reads, the drawing from marks, 800x480 modes) are for the other
  session; if the drawing goes upstream, M1's guard and M2's limit belong with it.


## 8. Files

* This report: `D:\Repos\esp32-s31-linux-release\tiny386-idf\v3\REVIEW-v40-emulator-vbe-merge.md`
* WSL `~/v4-work/review-emulator-vbe-merge/`: `r*.sh` (each step), `src/` (exports), `clone/` (my
  clone, at v4.0), `conflicts.txt`, `d/` (diffs), `ht.log` and `ht/` (host tests), `h/` (harness
  copy, `bin/`, `runs/`), `hc/` (harness diff check), `guest/` (BVBE480 and its FreeDOS image),
  `one.sh`, `w1.sh`, `list1.txt` to `list3.txt`, `gi/` (install run inputs), `probe.c`, `probe/`,
  `ua/` (accessor and `i386.c` compiles with the board's flags), `dis/`, `biosbuild/` (the BIOS
  rebuild), `applytest/` (the patch applied to 4ce2e06), `v4-mine.patch`.
* Windows pictures I looked at: the session's scratch folder `rev40vbe\pics\` and `vbe\pics\`, off
  the package.
