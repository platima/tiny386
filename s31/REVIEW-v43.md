# Review of v4.3: the information port merged into v4.2

Written 2026-10-24 as an independent, sceptical review of v4.3 (tag `v4.3`, 0f87c1c, branch `v43`,
tree 284a9331): the merge of the branch `perfport` (6e9a6f1) into v4.2 (6502063), the port's
counters beside v4.2's Ctrl+Alt+F11 and `VGA_CLEAR_AROUND`, what the port costs the emulator, the
tests, and the package in this folder with `BUILD-NOTES.md` 27, `README.md`, `FLASH.txt`,
`TEST-PLAN.md` 15, `REVIEW-RESOLUTION.md` round 9 and `..\t386mon\README.md`.

Nothing was flashed, no serial or COM port was opened, and nothing was committed anywhere. The
scratch tree, the merge step's folder (`~/v43/merge`) and the earlier steps' folders were only read.
My own work is in WSL `~/v43/review` (section 10): my own clone of the scratch tree, my own exports of
the tags, my own copies of both harnesses, my own builds and runs. `research\FLICKER-PLAT-NOTES.md`
is unchanged since 2026-10-23 18:25 (the request for the fix release), so nothing Plat has said since
bears on this review. Every claim is marked [verified] (seen in a file or measured in a run named
here), [inferred] or [unknown].


## 0. The answer in one screen

* **The merge lost nothing from either side.** For every file, the lines added and taken out
  between v4.2 and v4.3 are exactly the lines `perfport` added and took out against v4.1, and the
  lines between `perfport` and v4.3 are exactly v4.2's against v4.1; the only exceptions are the two
  conflicts (`esp/version.txt`, one line of `tests/host/run.sh`) and the new test. A three-way merge
  of each of the 25 files the two sides touched, made again with `git merge-file` from v4.1, v4.2 and
  `perfport`, gives the merge commit's file byte for byte in 23 and stops at the same two one-line
  conflicts, which the merge resolved by keeping both sides [verified: section 1].
* **The port follows Ctrl+Alt+F11 and lives with `VGA_CLEAR_AROUND`.** The snapshot reads
  `vga_dirty_blocks` itself, so the setting shows at once [verified: code, the host test, my
  harness run]. In my own T386MON run with three Ctrl+Alt+F11 presses, the port's `VBE_BLOCKS_ON`
  changed in the very next snapshot each time, the VBE picture count stood still while blocks were
  off, exactly two whole pictures followed the change back on, and T386MON's log reads on, off, on,
  off [verified: section 4].
* **v4.3 brings v4.2's flicker fix to T386MON's own workload.** On the flicker harness's board
  model, T386MON's nine tests at 800x480 in 16 bits leave 141,376 pixels black between two pictures
  on the branch's core and none on v4.3, with the same `END` line and the same `T386MON.LOG` bytes;
  at 640x480 in 16 colours, 3,016 against none [verified: section 4]. The merge step ran T386MON
  only on the T386MON harness, without the board model; these six runs are new evidence.
* **The flicker harness, rerun from my own copy and builds:** 28 configurations each on v4.3 and on
  my own v4.2 build: every `END` line the same as my v4.2's, apply42's v4.2's and the merge step's
  v4.3's; every board line of the 25 runs not threaded the same as v4.2's; no pixel black between two
  pictures in any run; the kept disk images v4.2's byte for byte. MODE13X and BMODES the same
  [verified: section 3].
* **The cost on the emulator core is a few compares on port I/O.** In the released ELFs the CPU
  step and every store path into video memory are v4.2's instruction for instruction (with addresses
  and offsets taken out); `pc_io_read` and its kin gain 9 to 14 instructions (the port's cases), the
  PS/2 entry points 4 each [verified: section 5]. The drawing core gains two clock reads and a few
  counts a picture [verified]. What a snapshot costs on the board, with its heap walks, is [unknown].
* **The package is consistent.** `SHA256SUMS` 173 of 173 OK, every image folder's list OK,
  `previous-v4.2\` 170 of 170 found with their hashes, T386MON's list 161 of 161; `build.sh` from
  nothing in my own folder gives all 23 image files byte for byte; both patches are the tag's diff
  and rebuild its tree; the apps say `v4.3` [verified: section 7].
* **Nothing blocks giving v4.3 to Plat.** The findings (section 8) are one wrong count in the notes,
  a stale protocol document, and notes: a known text-screen fault (`BUILD-NOTES.md` 26.3) now also
  seen for a quarter of a second at Windows' shut down from 16 colours, older than v4.3; and a harness
  effect anyone reading the port's VBE counts on the PC should know.


## 1. The merge, hunk by hunk

Done in my own clone of the scratch tree (`~/v43/review/repo`), on the tags and on 6e9a6f1.

**History and identity** [verified]. `v4.3` is 0f87c1c (a plain tag, like `v4.2`), tree 284a9331; its
parents are b267cd9 (the test), c044193 (the merge, parents 6502063 and 6e9a6f1). The merge base of
v4.2 and `perfport` is v4.1 (f61337e). The reflogs of `v42` and `perfport` end at their own last
commits of 2026-10-23 (19:55 and 21:37): neither branch was moved. The reflog of `v43` shows the remake
that `BUILD-NOTES.md` 27.2 describes: a first test and version commit (98f8d62, f292408), a reset to
c044193, then b267cd9 and 0f87c1c. All three commits carry the scratch tree's identity, Platima
Tinkers, and no co-author line.

**Line by line, both ways** [verified]. For each file, the sequence of added and removed lines of
one diff against the other, compared as a whole (line numbers and context aside):

| Compared | Files | The same | Different, and why |
|-|-|-|-|
| `perfport` against v4.1, with v4.3 against v4.2 | 19 | 16 | `esp/version.txt` (the version), `tests/host/run.sh` (the conflict, and the new test's lines), `tests/host/test_port_blocks.c` (new) |
| v4.2 against v4.1, with v4.3 against `perfport` | 13 | 10 | the same three |

So every hunk of the branch is in v4.3, and every hunk of v4.2 is in v4.3, with nothing else added
but the test and the version.

**The merge made again** [verified]. For each of the 25 files either side changed against v4.1 (the
branch 18, v4.2 12, five of them both), I ran `git merge-file` on v4.2's file, v4.1's and the
branch's and compared the result with c044193's file: 23 the same byte for byte, `pc.c`, `vga.c`, `vga.h` and `esp/main/speed.c` among them; `esp/version.txt`
and `tests/host/run.sh` conflict, each in one line. The merge resolved them as `v4.2` (the version
commit then made it `v4.3`) and as `vga_set_dirty_blocks vga_get_info; do` on one line, both sides'
names, which the reference builds of `vga.c` need [verified: `git diff v4.2 c044193`]. b267cd9 adds
`test_port_blocks.c` and ten lines of `run.sh`, nothing else [verified].

**Where the two sides meet in the code** [verified: reading the merged files]:

* `pc_vga_step()` draws through `perfport_redraw()` and times the picture; v4.2's clear is made
  inside `vga_refresh()`, and the rows it makes black reach the panel with the picture (every row is
  redrawn after a clear), so they pass through `perfport_redraw()` like any other row: a picture
  after a clear counts as changed, as the branch's `memset()` clear did. The T386MON logs being
  byte for byte the branch's (section 4) bear this out.
* v4.2's two whole pictures after a `vbe_blocks` change set `vbe_drawn` false, so `all` is true in
  `vga_vbe_refresh()` and `n_vbe_whole += all` counts them; the shrunk path counts the same way.
* `snapshot()` puts `vga_dirty_blocks` into `VBE_BLOCKS_ON` at each snapshot; Ctrl+Alt+F11 changes
  that variable through `vga_set_dirty_blocks()` on the drawing's core between two pictures. No copy
  of the setting is kept anywhere.
* `speed.c`: `perf_fill()` and v4.2's Ctrl+Alt+F11 handling are separate; `speed_forget_requests()`
  clears both requests; `perf_fill()` touches neither.
* Only `pc_vga_step()` calls `vga_refresh()` on the board (`esp/main` and `pc.c` searched), so the
  port's counts see every picture the board draws and nothing else.

`BUILD-NOTES.md` 27.1 says git "merged 15 files by itself"; the branch changed 18 files, two
conflicted, so it is 16 (finding 1).


## 2. The port with Ctrl+Alt+F11 and `VGA_CLEAR_AROUND`: the host test

* 45 of 45 host tests pass in my clone at the tag in 64 s, the same 45 `PASS` lines as the package's
  `checks\host-tests-v4.3.log`, and v4.2's 39 lines among them unchanged; the 11 host-build warnings
  are v4.2's same lines [verified: `~/v43/review/ht43.log`].
* `test_port_blocks` links `vga.c` as the board builds it (test 3c's object), which has
  `VGA_CLEAR_AROUND` (that object is the one `test_clear` holds against the `memset()` build)
  [verified: `run.sh`]. It draws each picture as `pc_vga_step()` does and changes `vbe_blocks` as
  `speed.c` does. It is single-threaded: the host build has no `VGA_TWO_CORES`, so the two cores'
  ordering is argued, not tested (note in section 8).
* The seven planted faults of `checks\mutants-v4.3.sh`, run again against my clone: each fails the
  test. Three of my own, each also caught: the blocks found not counted (1 check fails), the VBE
  pictures not counted (5), and snapshots after the third ignored (7) [verified: `~/v43/review/mt`].
* Under AddressSanitizer and UBSan, `test_port_blocks` on both boards and `test_perfport` pass with
  no report [verified]. (My first try without `setarch -R` looped on ASan's start-up under this
  kernel's address-space randomisation; the merge step's script already uses `setarch -R`. I stopped
  my own runaway process by its PID and deleted its output.)


## 3. The flicker harness, rerun

My own copy of apply42's harness (`~/flicker/apply42/h/harness`, which has the review of v4.2's
count of black), with `build.sh` taught by me to compile `perfport.c` with `TINY386_PERFPORT` when
the tree has it, as `esp/main/CMakeLists.txt` does. My builds of v4.3 compile the same sources with
the same commands as the merge step's, and my builds of v4.2 the same as apply42's [verified: file
and command comparisons]. The merge step's list (apply42's lists 1 and 8 with v4.3), run on my v4.3
build and, with only the binary changed, on my own v4.2 build: 56 runs, 229 and 227 s.

* **`END` lines**: 28 of 28 the same on v4.3 and my v4.2; the same as apply42's v4.2 runs and as the
  merge step's v4.3 runs [verified].
* **Pictures**: every picture equal to the reference in every run but the two installs, which
  differ in 1,706 pictures each on both (the text screen of `BUILD-NOTES.md` 26.3) [verified].
* **Black between two pictures**: none in any run; the pixels a clear made black (3,654 to 776,571 a
  run) were all black once the picture was done [verified].
* **Board lines**: all of them the same as my v4.2's and as the merge step's v4.3's in the 25 runs
  not threaded; the three threaded runs check right with the guest paused [verified].
* **Disk images**: what the DEBUG run and the two installs left is my v4.2 runs' byte for byte
  [verified]; then deleted.
* **`vbe_blocks = 0` really applies**: the board lines cannot show it, so I summed the rows sent to
  the panel: 248,390 rows in 5,103 bands with it, 122,297 in 6,005 without, on v4.3 and v4.2 alike
  [verified: `frames.tsv`].
* **MODE13X at 256K and 768K, BMODES** (FreeDOS, `-board`): v4.2's `END` lines (mine and apply42's),
  8, 8 and 15 shots the same, every picture equal to the reference, no black left, the board lines
  v4.2's [verified].


## 4. T386MON, rerun, and on the board's display model

T386MON 1.2 from `..\t386mon\t386mon.img`, unchanged, each run on a fresh copy of the image, the copy
deleted after `T386MON.LOG` was read off it.

**On the T386MON harness** (my copy of `~/perfapp/pfix/h/harness` with the merge step's `-f11`
change, the same files as the merge step's copy; v4.3 built from my export, the same sources and
commands as its build). A run the merge step did not make: 800x480 in 16 bits with the driver **as
released**, the keys `f1` (T386MON from B:, Log now at 390, 415, 445 and 475 s, shut down at 490 s),
Ctrl+Alt+F11 at 400, 430 and 460 s, and the port read by the harness every virtual second:

| t (s) | Pictures | VBE pictures | Whole | `VBE_BLOCKS_ON` |
|-|-|-|-|-|
| 400.000 | 13,271 | 11,300 | 78 | 1 |
| 400.011, after Ctrl+Alt+F11 | 13,272 | 11,302 | 79 | 0 |
| 430.000 | 14,270 | 11,302 | 79 | 0 |
| 430.001, after Ctrl+Alt+F11 | 14,271 | 11,302 | 79 | 1 |
| 431.000 | 14,304 | 11,335 | 81 | 1 |
| 460.020, after Ctrl+Alt+F11 | 15,271 | 12,308 | 87 | 0 |

* The setting reached the port in the snapshot straight after each change; no VBE picture was
  counted while blocks were off; the two whole pictures after the change back on are the 79 to 81
  [verified: the run's port lines]. The other whole pictures, one every 5 s (436, 441 ... 461), are
  the harness's own: its picture capture (`-snap 5`) redraws through `vga_refresh()` on the live
  state (finding 4) [verified: `headless.c`].
* T386MON found the port ("information port 1.1: pc-harness on PC harness, esp-korvo1"), its log is
  whole (140 lines, the closing line written at shut down), and its four Log now blocks say
  "blocks on", "off", "on", "off" [verified].
* The port's `CHANGED` 2,087 equals the harness's pictures that sent a row (16,709 less 14,622), and
  `PICTURES` 16,709 its pictures. The same run again gives the same `END` line and the same log bytes;
  without the per-second reads, 2,093 and 16,705, equal again [verified].

**On the flicker harness's board model** (`-board`: every picture against the guest's screen, the
count of black; the merge step's T386MON runs had neither). T386MON's nine tests (keys `m1`, the dialogs' Enters put in the key
file, since that harness puts a dialog's Enter after a key file's keys), on v4.3 and on the branch's
own core (6e9a6f1, built by me in the same harness copy):

| Run | Core | Pictures | Differ from the reference | Black between two pictures | `END`, `T386MON.LOG` |
|-|-|-|-|-|-|
| 800x480x16, pointer change | v4.3 | 19,501 | 0 | 0 (54,150 made black around pictures, all black after) | the branch's, byte for byte |
| the same | branch | 19,501 | 0 | **141,376**, in 3 pictures (7 `memset()` clears over a picture) | |
| 640x480x4 | v4.3 | 19,517 | 8, at shut down | 0 (406,305 made black around pictures, all black after) | the branch's, byte for byte |
| the same | branch | 19,517 | the same 8 | **3,016**, in 1 picture (7 `memset()` clears over a picture) | |

[verified: `~/v43/review/fh/runs/fb-*`]. T386MON's test figures in these runs (fill 480.99, scroll
318.61 ... full picture 1.13 at 800x480; fill 343.75 ... 0.45 at 640x480) are within 2 per cent of
`BUILD-NOTES.md` 27.8's from the other harness [verified].

The 8 pictures at 640x480 are at Windows' shut down (583.9 s): the mode goes to text, the first text
picture is right, the next 8 (a quarter of a second) differ in 89,955 pixels of the text area, none
black, until the whole text area is drawn again [verified: `frames.tsv`]. The branch's core has them
to the picture, and v4.2 (no port, so T386MON takes another path) 4 at its shut down [verified]. It
is the pattern of `BUILD-NOTES.md` 26.3 (the text picture right as the mode is set, the next one off
with only one band sent) [inferred], older than v4.3 (finding 3).


## 5. The cost on the emulator core, and on the drawing

From the released ELFs (`images\` against `previous-v4.2\images\`), my own disassembly with the RISC-V
`objdump` of the boards' toolchain, addresses and offsets of globals taken out [verified]:

* **The same, instruction for instruction**, on both boards: `cpui386_step`, `pc_step`,
  `i8254_update_irq`, `iomem_read8`, `16` and `32`, `iomem_write8`, `16`, `32` and `_string`,
  `vga_mem_read`, `vga_mem_write`, `16`, `32` and `_string`, `vga_refresh` (835), `vga_step`.
* **Longer**, emulator's core: `pc_io_read` 305 to 317, `pc_io_write` 386 to 400, `pc_io_read16` 83
  to 96, `pc_io_write16` 65 to 74, `pc_io_read32` 67 to 77, `pc_io_write32` 44 to 53 (the port's eight
  cases in each compare tree; a guest's `in` or `out` may take a compare or two more [inferred]);
  `ps2_put_keycode` and `ps2_mouse_event` 4 more each (one count); `access_image` 22 bytes more (the
  disk's sector and time sums).
* **Longer**, drawing's core: `pc_vga_step` 25 to 39 (two clock reads and `perfport_picture()`
  a picture), `vga_take_marks` 151 to 171, `vga_graphic_refresh` 1,961 to 1,989 (the VBE counts), and
  `perfport_redraw()` (44 bytes of code: two counts and the call on) on the way to the panel for
  each band.
* 27 functions differ in size or are new, 2,562 of 2,589 are unchanged; the app is 3,280 bytes
  bigger on each board, internal RAM left 253,100 on the Korvo-1 (664 less) [verified: sizes, my
  build's size report].

So the guest's own instructions cost what they did; port I/O, key and mouse events and disk accesses
cost a few instructions more, and a picture a few dozen plus two clock reads. The snapshot itself runs
only when a guest asks; on the board it walks the heaps (`..\t386mon\REVIEW.md` 7), and what that
costs core 1 is [unknown] until `TEST-PLAN.md` 15b's photograph of `SNAP_US`.


## 6. The merge step's evidence, read against my reruns

| Claim (`BUILD-NOTES.md` 27) | Mine |
|-|-|
| 27.1 the merge, every file the branch's lines | confirmed both ways and by a three-way merge made again; "15 files" is 16 (finding 1) |
| 27.3 `test_port_blocks` 18 checks a board, 7 of 7 faults | confirmed, and 3 more faults of mine caught |
| 27.4 45 of 45; sanitizers clean | 45 of 45 confirmed; ASan and UBSan on the port's three tests confirmed; `test_vbe` and `test_clear` under sanitizers and TSan not rerun |
| 27.5 the flicker harness, 28 runs | confirmed from my own copy and builds, against my own v4.2 too |
| 27.6 MODE13X, BMODES | confirmed |
| 27.7 the 27 standard and 11 native Windows runs, BENCH, BVBE480 | not rerun |
| 27.8 T386MON: the port found, values equal to the harness's, Ctrl+Alt+F11 followed | confirmed in a configuration of my own (the driver as released, port read each second) |
| 27.9 the store paths v4.2's; 9 to 14 more in the port switches | confirmed, and the other changed functions listed |
| 27.11 sizes | confirmed |
| 27.12 `build.sh` from nothing gives the package's bytes | confirmed in my own folder, a third build (196 s) |
| 27.16 `SHA256SUMS` | confirmed |

The drawing's host time (27.9), the 114-character output folder, the flash simulations, the
batch-script harness and the card check were not rerun; the bootloaders, partition tables,
`flash_args` and scripts they rest on are v4.2's byte for byte [verified], so those results carry
[inferred].


## 7. The package

* `SHA256SUMS`: 173 lines, 173 OK; each image folder's list 5, 5, 4 and 5 OK; `previous-v4.2\SHA256SUMS`:
  170 files, 29 in that folder and 141 in `v3\` with the listed hashes; `..\t386mon\SHA256SUMS` 161 of
  161 [verified]. This file is new and not in the list (finding 6).
* The three apps' descriptors say `v4.3`, project `tiny386`; the dual-boot default set's app is the
  dual-boot app; bootloaders, partition tables and `flash_args` of all four sets are v4.2's byte for
  byte [verified].
* `build.sh` (first comment changed only, against `previous-v4.2\`) run from nothing in
  `~/v43/review/rel`: exit 0, 196 s, the 23 files of the four sets equal to `images\` byte for byte,
  the only compiler warning the old `i2s.c` line [verified]. I then deleted my build tree, keeping its
  images and logs.
* `tiny386-idf-v4.patch` is `git diff upstream-20261022 v4.3` byte for byte and `tiny386-idf-v3.patch`
  `git diff pristine v4.3`; each applied to its base gives tree 284a9331; the diffstats' totals match
  [verified].
* The docs: the new and changed lines of `README.md`, `FLASH.txt`, `TEST-PLAN.md`,
  `REVIEW-RESOLUTION.md` and `BUILD-NOTES.md` have no em or en dash; the one double hyphen is in a git
  command quoted in 27.1 [verified]. The version, the update path (`flash-dualboot.bat COM10 app` from
  v3.6 to v4.2), the card (no file changed) and the board name T386MON will show (`v4.3 on
  ESP32-S31-Korvo-1`, `BOARD_NAME` with "the " taken off) agree with the code and the images
  [verified]. `TEST-PLAN.md` 15 is clear and asks for the right photographs. `..\t386mon\README.md` 2.5
  says which firmware has the port; `PROTOCOL.md` does not (finding 2).
* The cropped pictures in `..\t386mon\test-results\v4.3\` show T386MON's window only [verified: one
  looked at]. No copy of Windows is left in the merge step's folder or in mine [verified].


## 8. Findings

| # | Severity | Where | What | Fix |
|-|-|-|-|-|
| 1 | minor | `BUILD-NOTES.md` 27.1 | "merged 15 files by itself and stopped at two conflicts": the branch changed 18 files against v4.1, two conflicted, so git merged 16 by itself (13 the branch's alone, 3 changed on both sides) [verified: the file lists of `git diff v4.1 6e9a6f1` and `git diff v4.1 v4.2`; `checks\merge-v4.3.txt`'s own list of 17 files besides the version] | "merged 16 files by itself" |
| 2 | minor | `..\t386mon\PROTOCOL.md` 1, 10, 12, and the `FIRMWARE` row (70h to 77h) | Still say the port is on a branch and in no image, and give "v4.1-perfport" as the example version. `README.md` 2.5 and `BUILD-NOTES.md` 27.15 point this out, but a reader of the protocol alone (a programmer writing another guest program) is told the port is in no firmware [verified] | A short note at the top of section 1: "In tiny386-idf v4.3 and later; the protocol is unchanged", and "v4.3" as the example; or Plat's call, as 27.15 asks |
| 3 | note | `BUILD-NOTES.md` 26.3; `TEST-PLAN.md` 15c | The text screen of 26.3 also shows at Windows' shut down from 640x480 in 16 colours: 8 pictures (a quarter of a second) of the text area wrong, none black, on v4.3 and to the picture on the branch's core; 4 on v4.2. Not v4.3's [verified: section 4]; 26.3 had it as inferred for the standard VGA driver | One line in 15c (T386MON at 16 colours ends with Windows shut down): a moment of wrong text as Windows goes is the known 26.3 fault, not the port |
| 4 | note | the PC harnesses; `..\t386mon\README.md` 7 | The harnesses' picture capture (`snapshot()`, at every `-snap` interval and each dialog) redraws through `vga_refresh()` on the live VGA state, so on the PC the port's `VBE_PICTURES`, `VBE_WHOLE`, `VBE_BLOCKS` and `VBE_ROWS` include the harness's own pictures (one whole picture each 5 s with `-snap 5`); `PICTURES` and `CHANGED`, counted in `pc_vga_step()`, do not. On the board only `pc_vga_step()` draws [verified]. Nothing in 27 is wrong for it, but "whole pictures" counted on the PC are not the guest's alone | A sentence in T386MON's README 7 and in the harness's notes; take the snapshots out when counting VBE pictures |
| 5 | note | `perfport.c` `snapshot()` | `vga_dirty_blocks` is read with a plain load on the emulator's core while `vga_set_dirty_blocks()` stores it atomically on the drawing's core: a data race by C11's rules, harmless for an aligned word on the board [inferred], and the same as v4.2's own reads of it (`BUILD-NOTES.md` 25.5) | `__atomic_load_n(&vga_dirty_blocks, __ATOMIC_RELAXED)` would make it clean at no cost; Plat's call, not needed for v4.3 |
| 6 | note | `SHA256SUMS`, `README.md` | This file is new; the top list does not have it | Add it, as `REVIEW-v42.md` was after its review |
| 7 | note | `perfport.c`, `speed.c` (`..\t386mon\REVIEW.md` 7) | The snapshot's heap walks on the board, inside the guest's `out`, are still unmeasured | `TEST-PLAN.md` 15b asks for `SNAP_US`; keep it there |


## 9. What only the board can say [unknown]

As `BUILD-NOTES.md` 27.14: the port on the board (T386MON's Emulator line naming v4.3 and the
board), the drawing's time on core 0, PSRAM and internal RAM free, what a snapshot costs, T386MON's
tests in real time at both modes, and Ctrl+Alt+F11 seen in its VBE drawing line. My runs say the
port, the counts and the flicker fix behave on the PC; nothing here has touched a board.


## 10. Where things are

WSL `~/v43/review/` (all made by this review):

* `repo`: my clone of the scratch tree, at `v4.3` (detached); `t-v4.3`, `t-v4.2`, `t-perfport`:
  `git archive` of the tags and of 6e9a6f1; `ht43`, `ht43.log`: the host tests.
* `fh/`: my flicker harness copy (`harness/`, `bin/r43`, `r43d`, `r42`, `r42d`, `pp41`, `runs/`,
  `summary-l43.txt`, `summary-l42.txt`); `ar.sh`, `l43.txt`, `l42.txt`, `frun.sh` (the 56 runs),
  `fchk.sh` (their checks), `m13.sh` (MODE13X and BMODES), `tm4.sh`, `tm5.sh`, `tm6.sh` (T386MON on
  the board model: `runs/fb-p16`, `fbp-p16`, `fb-vga`, `fb-vga2`, `fbp-vga`, `fb42-vga`).
* `ph/`: my T386MON harness copy (`bin/v43-korvo1`, `runs/rv-f11n16`, `rv-f11n16b`, `rv-f11n16c`);
  `tm.sh`, `tm2.sh`, `tm3.sh`, `tchk.sh`; `keys/` (the key files, with the dialogs' Enters added for
  the board model runs).
* `mt/`, `mt.sh` (the planted faults), `san/`, `san2.sh` (the sanitizers), `rel/` (`build.sh` from
  nothing: `build.out`, `w/images`, `w/build-*.log`, `w/size-*.txt`), `elf/` (function sizes), and the
  `s*.sh`, `setup.sh`, `fsetup.sh`, `fbuild.sh`, `psetup.sh`, `patchk.sh`, `sums.sh` and `elf*.sh`
  that set up, built or only read.

Every Windows image copy my runs made was deleted after its run; no Windows picture left WSL but two
looked at in my scratch folder on this PC, not in the package. Nothing I started is still running.
