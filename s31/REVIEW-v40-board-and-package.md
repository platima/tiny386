# Review of v4.0: the board and the package

An independent review of tiny386-idf v4.0 (tag `v4.0`, 8d65b1d, tree 09f94dfc, on top of
`v4.0-base` b370a75), 2026-10-23. The lens: memory on the board at each colour depth, the drawing's
cost and the two cores, the DOS board at 800x600, the dual-boot images and scripts, the card files
and the new BIOS files, checksums, versions, and the docs held against the files. A second
reviewer has the emulator's own correctness; I touch it only where the board makes a difference.

Every claim is marked [verified] (I ran or read it myself today), [inferred] (follows from what I
read, not run) or [unknown]. Nothing in the package was changed but this file. Nothing was
flashed, no serial port was opened, nothing was committed, and nothing of mine is still running
in WSL [verified: `ps` after the last job]. My work is in WSL `~/v4-work/review-board-and-package`.


## Verdict

The package is sound. An independent `build.sh` run from nothing gives all 23 image files byte for
byte, the boot pieces and scripts are v3.9's byte for byte, every checksum list checks, the
patches are the git diffs they say they are, and the new `vgabios.bin` rebuilds byte for byte from
the source tarball shipped beside it. Memory on the board is no worse than v3.9 at any setting:
v4.0 takes nothing new from PSRAM or internal RAM at run time, and its new tables sit in the device
window with at least 38 KiB to spare.

No blocker and nothing major. The most useful finding is about cost the PC cannot see: every
chain-4 store (DOS mode 13h as well as the VBE window) and every frame buffer store now runs a
`fence rw,w`, the PC harness compiles that fence to nothing, and `vbe_blocks = 0` does not take it
away. The rest are doc points: one stale [verified] in TEST-PLAN's card table, a gap after the
driver install (two inis boot the same image with different video memory), the guest's 14M at 768K
stated more firmly than the numbers allow, the DOS board's sizes, and a merge-step decision about
memory accessors that was tested with compiler flags the board build does not use.


## Findings, most severe first

| # | Severity | Where | In one line |
|-|-|-|-|
| 1 | minor | `vga.c`, `pc.c`, `vga.h` (`vga_dirty_mark`); BUILD-NOTES 22.2, 22.13 | the mark and its fence are paid by every chain-4 and LFB store on the board, mode 13h included; invisible on the PC; no switch removes it |
| 2 | minor | TEST-PLAN.md section 0, "The card", line 217 | says `vgabios.bin` is the Linux route's, [verified] against its SHA256SUMS: no longer true |
| 3 | minor | TEST-PLAN.md 13c, 8; `win95-14m.ini` | after the driver install the same `win95.img` is still offered with 320K of video memory, too little for its new mode; untried |
| 4 | minor | `research\UPSTREAM-MERGE-v4.md` 4.5 and "For Plat"; `board_korvo1.h` | the accessor comparison used rv32gc flags; with the board's flags both forms are one `lw`/`sw` at any alignment |
| 5 | note | BUILD-NOTES 22.9, TEST-PLAN 13a | "14M again at 768K" holds only if the pool is at least 15,120 KiB; Plat's run bounds it at 14,352 to 15,375 |
| 6 | note | TEST-PLAN 13e, `vbe-reg.img` README.TXT, card files | no DOS board Windows ini; 1408K leaves 1,792 bytes over a 24-bit screen; 1408K and 1920K never run |
| 7 | note | `bios\README.txt` | the kit's `bios.bin` (TOOLCHAIN=host) is not the card's `bios.bin`; say which rebuilds it exactly |
| 8 | note | README.md lines 36 and 179 to 201 | small wording and order points |

### 1. The mark and its fence are paid on the board by more stores than the notes say (minor)

**What.** `vga_dirty_mark()` runs a release fence and one or more byte stores after each store into
video memory. BUILD-NOTES 22.2 describes its cost as "a 16-bit store into the linear frame buffer
... about 6 instructions more ... one of them the fence", and lists the chain-4 paths as "the bank
window, which VBE modes use". But the chain-4 path of `vga_mem_write*()` is also every store in
DOS mode 13h (320x200 in 256 colours, chained), the mode most simple DOS games draw in, which is
what the DOS board is for. Those marks are never read (only `vga_vbe_refresh()` and the shrunk VBE
path take them), so in mode 13h the fence is pure overhead.

**Evidence.**
* In the release ELF `images\korvo1-dualboot\tiny386.elf` each of the eight writers has exactly one
  `fence rw,w`: `vga_mem_write`, `vga_mem_write16`, `vga_mem_write32`, `vga_mem_write_string`,
  `iomem_write8`, `iomem_write16`, `iomem_write32`, `iomem_write_string` [verified: objdump]. In
  `vga_mem_write` the fence is on the chain-4 branch (the test of SR4 bit 3 at offset 41), byte
  store then `lw` of the map pointer, `fence rw,w`, `sb` [verified: objdump at 2f02653a to
  2f026554]. The planar and odd/even paths have no mark, so Windows' standard VGA driver, text
  mode and Doom's unchained mode are not affected [verified: one fence a function, on that branch].
* On x86-64 the same `__atomic_thread_fence(__ATOMIC_RELEASE)` compiles to no instruction at all
  [verified: Debian gcc 12.2 `-O3` on a copy of `vga_dirty_mark`, `~/v4-work/review-board-and-package/fence.c`].
  So every PC figure in 22.7 (host microseconds, BENCH, the Windows runs) is blind to the fence by
  construction; only the guest instruction counts carry over, and they do not include host work.
* `[esp] vbe_blocks = 0` only changes the drawing: "The blocks are noted either way" (22.2,
  `vga.h`). So there is no way on the board to compare v4.0 with and without the marks, short of
  flashing v3.9 again.

**How much.** [unknown]. At about 2.2 million guest instructions a second the emulator spends
roughly 145 cycles of core 1 on each guest instruction [inferred: 320 MHz / 2.2 M]; 6 instructions
and a fence on a store to video memory is a few per cent of that store's cost if the fence is
cheap, more if it waits for a PSRAM line fill the store started [inferred]. Over a whole DOS game
probably a few per cent at most [inferred], but it has never been measured anywhere.

**Fix.**
* Skip the mark when `vga_dirty_blocks` is 0, so that `vbe_blocks = 0` really is "as before v4.0" on
  the store side too and gives Plat a board A/B. The drawing already redraws in full in that case.
* Consider skipping it also while VBE is off (`vbe_regs[VBE_DISPI_INDEX_ENABLE] & VBE_DISPI_ENABLED`
  clear): the first VBE picture after an enable is drawn in full anyway (`vbe_drawn` is false after
  any non-VBE picture). The emulator reviewer should check the enable/disable path before this goes
  in; the `vga_dirty_blocks` half is the simple one.
* In 22.2 and 22.13 say that mode 13h stores pay it too, that the PC timings cannot show it, and
  that `vbe_blocks = 0` does not remove it. A quick board check: a DOS loop of single byte stores in
  mode 13h, timed with BENCH's mark mechanism or a stopwatch, on v3.9 and v4.0.

### 2. TEST-PLAN's card table still says `vgabios.bin` is the Linux route's (minor)

TEST-PLAN.md line 217, in section 0 "The card", the table of what to copy from `v3\card\tiny386\`:
"`bios.bin`, `vgabios.bin` | the Linux route's own [verified: SHA-256 against
`..\tiny386\SHA256SUMS`]; ... copying them again should change nothing". For v4.0 that is false:
`card\tiny386\vgabios.bin` is 4aeae74f... (40,960 bytes, `korvo-v4`), and `..\tiny386\SHA256SUMS`
has f658c3b0... (40,448 bytes) [verified: both files hashed]. The same section also says "With
three inis in the folder the boot menu comes up"; with `win95-800x480.ini` copied it is four. 13a is
right; section 0 was not updated.

**Fix.** Change the row: `bios.bin` the Linux route's; `vgabios.bin` v4.0's (the five 800x480
modes, `bios\README.txt`), v3.9's still works. Mention the fourth ini.

### 3. After the driver install, `win95-14m.ini` still boots the same image with 320K (minor)

Both Windows inis have `hda = /sdcard3/win95.img` [verified: the files]. TEST-PLAN 13 installs the
driver into that image (it suggests a copy, but the ini is not pointed at one) and VBE480.REG makes
800x480 in 16 bits the current mode, which needs 768,000 bytes of video memory. `win95-14m.ini`
gives 320K (327,680 bytes). The boot menu offers both, and TEST-PLAN 8 (8e, 8f) still names
`win95-14m.ini` for later runs. What Windows does when its mode does not fit is [unknown]: no PC run
tried it (the driver's mode check would refuse the mode [inferred]; Windows 95 usually falls back to
its VGA driver with a warning [inferred]).

**Fix.** In 13c say: from here on start this image with `win95-800x480.ini` only (or raise
`vga_mem_size` in `win95-14m.ini` to 768K), or give `win95-800x480.ini` its own image name
(`win95v.img`, a copy) so the two cannot meet. One PC run of the installed image at 320K would
turn the [unknown] into a sentence.

### 4. The accessor comparison in the merge note used flags the board does not (minor)

`research\UPSTREAM-MERGE-v4.md` 4.5 compared `unaligned.h` with upstream's packed accessors "on the
board's compiler (esp-16.1.0, rv32gc, `-O3`)" and found the packed stores slower (four byte stores);
its "For Plat" asks him to decide. The board build does not use rv32gc alone: its flags are
`-march=rv32imafcb_zicsr_zifencei_zcb_zcmp_zcmt_xesploop_xespv -mabi=ilp32f -mtune=esp32s31`
[verified: `toolchain/cflags` of the release build], and with them the toolchain marks the object
`unaligned_access 1`. Compiled with exactly those flags, the merge step's own `ua.c` gives one `lw`
for both loads, one `sw` for the packed store, and for `unaligned.h`'s `stu32` a branch whose two
arms are both a single `sw` [verified: `~/v4-work/review-board-and-package/ua/ua-board.s`]. The
release image shows the same: `iomem_write16` stores with one `sh` and `iomem_write32` with one
`sw` whatever the address, in v3.9 as in v4.0 [verified: objdump; `checks\vbe-loops-v4.0.txt` has
v3.9's `bexti` test followed by the same `sh` on both paths].

So on the board `I386_ALIGNED_ONLY` changes nothing but a dead branch, the packed form is not
slower, and the decision offered to Plat is moot. `board_korvo1.h`'s comment ("No load or store of
16 or 32 bits at an address that is not aligned") does not describe the built code. Since Windows
95 runs on v3.9 on the board, where every unaligned guest access is one such instruction, the S31
evidently does misaligned loads and stores in hardware [inferred: the board runs, the toolchain's
attribute; I found no emulation of them in ESP-IDF's `components/riscv` or `esp_system/port`, only
their names as exception causes, verified by grep]. Pre-existing, not a v4.0 defect,
and harmless; v4.0 only makes the frame buffer path hot, so 24-bit Windows will make many of
these stores at odd addresses [inferred].

**Fix.** Correct 4.5 and drop or restate the "For Plat" item; correct the header comment. If the
other session sends `unaligned.h` upstream, its benefit is for other targets, not this board build.

### 5. "14M again at 768K" is conditional (note)

TEST-PLAN 13a and BUILD-NOTES 22.9 say that with `vga_mem_size = 768K` "the guest should get 14M
again" [inferred]. The guest gets the whole megabytes of the pool less `round4k(vga_mem_size +
16K)` [verified: `settle_memory()`]. Plat's 13M at 1M (1,040 KiB taken) bounds the pool at 14,352
to 15,375 KiB [arithmetic]; 14M at 768K (784 KiB taken) needs 15,120 KiB or more, so it holds only
in the top quarter of that range. The estimate of about 15,300 KiB (NOTES-core2.md 3.4) puts it
there, by about 180 KiB [inferred]. `research\NATIVE-VBE-DESIGN.md` 4.1 says it properly ("14M if
the pool is 15,120 KiB or more, else 13M"); the two later docs dropped the condition. Nothing breaks
either way (`mem_size = max`), but Photo 1 is easier to read with the threshold in front of you.
The table in section A below has every depth.

### 6. The DOS board's Windows settings (note)

* There is no Windows ini for the DOS board. TEST-PLAN 13e says "in the ini `vga_mem_size = 1M`",
  meaning `win95-800x480.ini`, whose header says it is for the Korvo-1 and which has 768K. With 768K,
  VBE600.REG's 800x600 in 16 bits (960,000 bytes) does not fit; what Windows then does is [unknown],
  as in finding 3. A `card-dosboard\tiny386\win95-800x600.ini` with 1M (or 960K, which fits 960,000
  bytes and leaves 64K more for the guest) would remove the edit.
* 1408K for 24 bits at 800x600 (13e and the floppy's README.TXT) is 1,441,792 bytes against a screen
  of 1,440,000: 1,792 bytes over. VMDisp9x needs `pitch * height < vram` (NATIVE-VBE-DESIGN 4.1);
  with a pitch of 2,400 it fits, with any pitch rounding above 2,403 it does not [arithmetic]. How
  the driver works out its pitch is [unknown] to me.
* No PC run used 1408K or 1920K: BVBE480 ran at 768K, 1M, 1536K and 2M, the Windows DOS board runs
  only at 1M in 16 bits [verified: `checks\vbe480\results-v4.0.txt`, `checks\win95-pc-runs-v4.0.txt`].
  1536K (24 bits) and 2M (32 bits) are tried sizes; say so beside the untried ones, or run them.

### 7. `bios\README.txt` and the card's `bios.bin` (note)

`bios\README.txt` says the kit "leaves out/bios.bin, out/vgabios.bin". Built from the shipped
`seabios-korvo-v4-src.tar.xz` with Debian's gcc 12.2 and the script's fixed version stamps, the
`vgabios.bin` is the card's byte for byte (4aeae74f...), but the `bios.bin` is 2e3ad129..., not the
card's be36f0d3... [verified: my rebuild, `~/v4-work/review-board-and-package/biosrebuild`]. That
is expected (the card's is the Linux route's Ubuntu gcc 15.2 build, BUILD-NOTES 22.4) and the
source is the same (the 800x480 patch touches only `vgasrc/`), so the LGPL is met [inferred]. The
README should still say that the card's `bios.bin` is rebuilt exactly by the Linux route's kit (or
this script with `NO800=1` and `TOOLCHAIN=ubuntu`, which I did not run [unknown]), and that
`TOOLCHAIN=host` gives a different `bios.bin`.

### 8. README wording (note)

* Line 36: "From v3.6 to v3.9 the update is the app alone" reads as v3.9's sentence; FLASH.txt says
  it better ("From v3.6, v3.7, v3.8 or v3.9 ... all v4.0 needs").
* "In short" has v4.0's bullet (line 179) before v3.9's (line 192).
* Optional: `checks\vbe480\BVBE480.COM`, which TEST-PLAN 13f tells Plat to copy onto
  `freedos-hd.img`, is in no checksum list; its SHA-256 is 8cc09c33d5fbfb953e634339773544553a261c552ff4a354fbf9d81b544c0b1e [verified].


## A. Memory on the board at each depth

What v4.0 changes in memory [verified: source, `git diff v4.0-base v4.0`]: nothing in PSRAM outside
the pool, nothing in internal RAM at run time. The new tables (the block map, a byte per screen
row, the shrunk picture's column table, the last palette) all come from `pcmalloc()`, the 128 KiB
device window. Video memory still comes from the pool (`bigmalloc()`, `vga_mem_size` plus 16 KiB).

| Item | Korvo-1 | DOS board | v4.0 against v3.9 |
|-|-|-|-|
| Panel frame buffer (PSRAM, before the pool) | 768,000 bytes | 960,000 bytes | unchanged [verified: source] |
| `PSRAM_KEEP_FREE` (FatFs, console, the 16-colour cache) | 256 KiB | 256 KiB | unchanged; the 16-colour cache is `heap_caps_malloc` there, not the pool [verified: `vga.c` `seen_malloc`] |
| Emulator pool | 14,352 to 15,375 KiB [arithmetic from Plat's 13M at 1M]; about 15,300 [inferred] | about 15,110 KiB [inferred: 187.5 KiB less frame buffer]; no board run | unchanged |
| Device window, of 131,072 bytes | 90,592 to 92,512 in the harness at 256K to 2M | 91,488 at 1M | +3,488 at 768K, +3,744 at 1M [verified: harness lines, v4.0 against v4.0-base 87,616]; grows one byte a KiB of video memory, so even absurd sizes stay inside [arithmetic] |
| Internal RAM, static | 249,880 used, 253,864 left | 252,878 used, 250,866 left | +170 bytes (code and data) [verified: `checks\size-*-v4.0.txt`, my rebuild's are the same bytes] |
| `vga_task` stack (4,096 bytes) | largest frame `vga_graphic_refresh` 1,184 bytes | the same | unchanged; the new functions inline into it [verified: `-fstack-usage` with the release build's own command, both boards, both tags] |

The guest's memory with `mem_size = max`, P the pool in KiB [arithmetic; P itself inferred]:

| Board, depth | `vga_mem_size` | Taken | Guest |
|-|-|-|-|
| Korvo-1, standard VGA (`win95-14m.ini`, `mem_size = 14M`) | 320K | 336K | 14M if P >= 14,672, else lowered to 13M with a yellow line |
| Korvo-1, 800x480x16 (`win95-800x480.ini`) | 768K | 784K | 14M if P >= 15,120, else 13M |
| Korvo-1, as Plat runs now | 1M | 1,040K | 13M [reported] |
| Korvo-1, 800x480x24 | 1152K | 1,168K | 13M if P >= 14,480, else 12M |
| Korvo-1, 800x480x32 | 1536K | 1,552K | 13M if P >= 14,864, else 12M |
| DOS board, 800x600x16 | 1M (or 960K) | 1,040K (976K) | 13M if P >= 14,352 (14,288), else 12M |
| DOS board, 800x600x24 | 1408K | 1,424K | 13M if P >= 14,736, else 12M |
| DOS board, 800x600x32 | 1920K | 1,936K | 12M if P >= 14,224, else 11M |

The boot console's `Emulator memory: ... KiB` line gives P; one photo settles every row.


## B. The drawing's cost and the two cores

* **The counts.** The five loops of `vbe_row()` in the release image match `checks\vbe-loops-v4.0.txt`
  (I re-read the 16-bit loop at 4001c850 from `images\korvo1-dualboot\tiny386.elf`: 8 instructions
  for two pixels, 6 with no store) [verified]. A still 800x480 picture is now a look at about 750
  map bytes and 480 row bytes; a whole new 16-bit screen 1.15 million instructions, 32-bit 4.8
  million [verified arithmetic from the counts]. `vga_graphic_refresh` calls the 64-bit division
  helpers from 2 places where v3.9 had 6 [verified: objdump].
* **What the counts leave out** [inferred]: a full redraw also streams video memory (768,000 to
  1,536,000 bytes) and the panel's buffer through the 64 KiB cache both cores share, so its time on
  the board is set as much by PSRAM as by instructions. The gain is larger than the counts suggest
  for a still or nearly still screen: v3.9 swept the whole of video memory through the shared cache
  every 30 ms in VBE modes, evicting the emulator's working set on core 1; v4.0 does not. Board time
  for either is [unknown].
* **The ordering.** Writer (core 1): data, `fence rw,w`, mark byte. Drawing (core 0): read and clear
  marks, `fence rw,rw`, read video memory. That is the right pattern for one writer and one clearer
  [verified: source; the fences in the release image]. The map lives in PSRAM behind the same cache
  as video memory [verified: `pcmalloc()` is the guest's 0xA0000 window in the pool].
* **Back from the console.** Ctrl+Alt+F12 and the end of BENCH give the panel back through
  `give_back()`, which clears the buffer and sets `full_update = 2`, so the next VBE picture is drawn
  whole [verified: `speed.c`]. While the console owns the panel `vga_task` does not draw, and the
  marks simply wait.
* **The core 1 side** is finding 1.


## C. The DOS board at 800x600

* The DOS board image has `VGA_DIRTY_BLOCKS` and the new code [verified: `board_dosboard.h`;
  `vga_dirty_map` in `images\dosboard\tiny386.elf` returns the map; the console string for
  `vbe_blocks = 0` is in the image].
* 800x600 VBE pictures are drawn 1:1 there (stride 1,600, no offset); BVBE480 ran the DOS board build
  at four video memory sizes, every picture right, and Windows at 800x600x16 reached the desktop with
  every picture equal to a full redraw [verified: the check files, not re-run by me].
* PSRAM bandwidth: the HDMI scan already reads about 57.6 MB/s from PSRAM (800x600x2 at 60 Hz) and
  TEST-PLAN warns of underruns. In VBE modes v4.0 reads far less video memory than v3.9 did, so it
  should make underruns less likely, not more [inferred].
* The sound task shares core 0 at priority 0 with `vga_task`; v4.0 makes `vga_task` lighter in VBE
  modes and leaves other modes as they were [inferred: `REVIEW-concurrency-esp-idf.md`, source].
* Its ini and sizes: finding 6.


## D. Dual-boot images and scripts

* All four sets' `bootloader.bin`, `partition-table.bin` and `flash_args` are v3.9's byte for byte
  (`cmp` against `previous-v3.9\images\`), and the dual-boot pair and the tiny386-default table are
  `dualboot\`'s (b57469cd..., d41cba3c..., 5b90d416...) [verified]. `dualboot\SHA256SUMS`: 92 OK
  [verified].
* `flash-dualboot.bat`, `flash-standalone.bat`, `back-to-linux.bat` and `flash-paths.cmd` hash as in
  `previous-v3.9\SHA256SUMS` [verified]. I did not rerun the batch harness; with the scripts and the
  boot pieces unchanged, only the app's bytes differ in it [inferred].
* `images\korvo1-dualboot-tiny386-default\tiny386.bin` is `images\korvo1-dualboot\tiny386.bin`
  [verified: `cmp`]. The app's update range, 0xE20000 to 0xEFCFFF, fits the 0x1E0000 partition with
  54 per cent spare [verified: size 902,224].


## E. Card files and BIOS files

* `win95-800x480.ini` as described in README and 22.9 [verified]. `tiny386.ini`, `win95-14m.ini`,
  `bios.bin` and `dosboard.ini` are v3.9's [verified: hashes].
* The firmware's own `setup.c` and `ini.c` (tag v4.0) on a pretend card with the install lines of
  13a uncommented: `fda` and `fdb` resolve beside the ini, 0 problems; with the driver's image left
  off the card, 1 problem, so a forgotten file stops with a red line rather than a confusing boot
  [verified: `~/v4-work/review-board-and-package/cardcheck`]. FatFs has long file names on, so the
  driver image's 37-character name is fine [verified: `CONFIG_FATFS_LFN_HEAP`, `MAX_LFN 255`].
* `vbe-reg.img`: FAT12, label VBEREG, serial 2026-1023; `VBE480.REG` (682 bytes), `VBE600.REG` (307),
  `README.TXT` (1,510), contents as TEST-PLAN 13c says [verified: mtools].
* The driver image named in the docs: 1,474,560 bytes, SHA-256 96eed45b...a174a63, the same as the
  copy the VBE step fetched [verified: hashed `~/v4-work/vbe/dl-vmdisp`, read only]. Its INF lists
  modes up to 1920x1200; Windows keeps to 800x600 because VBE480.REG sets the monitor's limit there
  [inferred: NATIVE-VBE-DESIGN 3.3]. If Plat ever picks another monitor type, 1024x768 and up could
  be offered and would be drawn shrunk.
* `vgabios.bin`: 40,960 bytes, an option ROM of 80 blocks whose bytes sum to 0, version
  `rel-1.17.0-20-g81ec9ec0-dirty-20261023_000000-korvo-v4` [verified]. Rebuilt by me from
  `bios\seabios-korvo-v4-src.tar.xz` with Debian gcc 12.2 and the stamps of `build-bios.sh`: the
  same bytes [verified]. The tarball's `.config` is `bios\patches\seabios-tiny386.config`, and its
  `svgamodes.c` has the five modes [verified]. The three Linux-route patches in `bios\patches\` are
  the Linux route's byte for byte, and `bios\build-bios-v4.diff` is the difference between the two
  scripts [verified].


## F. Checksums, versions, docs against files

* `SHA256SUMS`: 64 lines, all OK; the 14 new entries as 22.12 lists them [verified].
* Each image folder's `SHA256SUMS`: OK [verified]. Every `previous-v3.x\SHA256SUMS`, file by file
  against the places README names: v3.2 37 of 37, v3.3 39, v3.4 40, v3.5 40, v3.6 41, v3.7 41,
  v3.8 45, v3.9 50, none missing [verified].
* Versions: all four `tiny386.bin` carry `v4.0`, project `tiny386`, ESP-IDF
  v6.1-dev-8142-g188e3e55bb, and the ELF SHA-256 in each app is the hash of the `tiny386.elf` beside
  it [verified: the app descriptor read directly]. `esp/version.txt` is `v4.0` at the tag [verified].
* `tiny386-idf-v4.patch` is `git diff upstream-20261022 v4.0` byte for byte, and
  `tiny386-idf-v3.patch` is `git diff pristine v4.0` [verified]; their diffstat totals match.
* `build.sh` differs from v3.9's only in its comment, default folder, `TINY_COMMIT` (4ce2e06...),
  patch name and tag name [verified: `diff`].
* **An independent build from nothing** in my own folder: exit 0 in 190 s, and all 23 files of the
  four sets identical to `images\`, ELFs and `SHA256SUMS` included, so no ELF carries its folder's
  name [verified: `~/v4-work/review-board-and-package/rebuild/compare.txt`]. The only compiler warning
  is the old `i2s_task` one on the two Korvo-1 builds [verified].
* The sizes table in 22.11 matches `checks\size-*-v4.0.txt` [verified].
* FLASH.txt's changes from v3.9 are the version and the v4.0 paragraphs only, CRLF kept [verified].
* Stale or loose text: findings 2, 5, 7 and 8.


## G. What only the board can say

* The pool size (Photo 1), and with it every row of the table in A.
* What a `fence rw,w` costs on the S31's core, and so what finding 1 is worth; a mode 13h store loop
  on v3.9 and v4.0 would show it.
* The `display:` line with `uart_log = 1`: pictures, write-backs and their time, at 800x480 still
  and moving.
* The device window's real figure (`PC built. Device memory ...`), expected near the harness's
  91 KiB.
* What Windows does when its mode does not fit its video memory (findings 3 and 6), if Plat ever
  boots the installed image with less.


## H. My work files

WSL `~/v4-work/review-board-and-package/`: `t-v4.0`, `t-v4.0-base`, `t-v3.9` (`git archive` of each
tag, read only use of the scratch repository); `su/` (stack usage, `su.py`); `ua/` (the accessor
test with the board's flags); `fence.c`; `card/` and `cardcheck/` (the floppy's files, the card check
with the install lines); `biosrebuild/` (the VGA BIOS from the shipped tarball); `rebuild/` (the
`build.sh` run, `build.out`, `compare.txt`); the `x*.sh` and `x*.py` scripts. One scratch file I
wrote to WSL's `/tmp` while testing was removed straight after.
