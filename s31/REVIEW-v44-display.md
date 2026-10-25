# Review of v4.4: the display (part 1)

Written 2026-10-24 as an independent, sceptical review of v4.4's display work: branch `s31-v4.4`
of Plat's fork (WSL `~/fw/tiny386-s31`, worktree `~/fw/wt-v44`), commits 4758608 to de24383, as
released in tag `s31-v4.4` (6d619a0). The lens: Ctrl+Alt+F11 saving (the ini rewrite against a cut
in power and against a flush of the guest's disk), the double-buffered panel (memory, the swap at
the panel's frame, both buffers kept right), pacing at the guest's quiet moments (no starvation, a
bound on the lag), the drawing speed-ups (pictures identical), the text investigation and the grey
screen. Part 2's storage work is another review's (`REVIEW-v44-storage-and-safety.md`).

Nothing was flashed, no serial or COM port was opened, nothing was committed, pushed or deleted.
The worktree and the earlier steps' scratch were only read (`git status` clean after my runs). My
own work is in WSL `~/org2/review-display` (section 10). The only file written outside WSL is this
one. Every claim is marked [verified] (seen in a file or measured in a run named here), [inferred]
or [unknown].

(Added 2026-10-25: the commit hashes in this review are the ones the branch has had since its
history from `s31-v4.4` on was rewritten and force-pushed that day (`BUILD-NOTES.md` 32.6). The code
they name is the same. Nothing else in this review was changed.)


## 0. The answer in one screen

* **The display work is sound in its main paths, and nothing here blocks v4.4.** The host tests
  pass at the tag, 53 of 53 [verified: section 1]. My own model of the double buffering around the
  board's `vga.c` (text, mode 12h, mode 13h, VBE at 8, 16 and 24 bits, centred, full screen and
  shrunk, with Ctrl+Alt+F9 and the speed screen thrown in at random) keeps the panel's buffer equal
  to a full redraw and the back buffer equal to the front after every swap: of 3,004 checks, the
  only ones that fail are those after the one case below [verified: section 3.4].
* **One real fault in the double buffering (minor):** Ctrl+Alt+F9 turning double buffering *on*
  while the speed screen is shown copies the console's text into the back buffer, and after the
  speed screen that text stays in the black border round any picture narrower than the panel
  (DOS text, 640x480 in 16 colours, mode 13h, VBE 640x480) until the next mode change. The speed
  screen's own line invites the key ("Ctrl+Alt+F9 changes it and saves it in the ini"). Windows at
  800x480 fills the panel and is not affected. 465 failed checks in my model, every one of them in
  that case; with a small fix in `give_back()` (clear the back buffer, as one buffer is cleared),
  0 of 3,004 [verified: section 3.4].
* **The ini save is right between its steps, not inside them (minor):** a cut in power *inside* a
  FatFs `rename()` can leave two directory entries sharing one cluster chain (FatFs marks that
  stretch of `f_rename()` itself as the place "where an interruption can cause a cross-link").
  `setup_ini_tidy()` then removes the leftover name, which frees the clusters the ini still uses;
  the ini is lost when those clusters are next written. Rare (a cut within milliseconds of the
  save), but it is the case the design set out to cover [verified: FatFs source; the board
  unknown]. Section 2 gives a design with no rename at all.
* **The swap waits for the right event on the S31** [verified: ESP-IDF source]: on this chip the
  RGB driver raises `on_frame_buf_complete` from the GDMA's link-switch interrupt, which
  `esp_lcd_panel_draw_bitmap()` asks for after it repoints the DMA lists, not from the per-frame EOF.
  Whether the hardware's link switch means what the driver's comments say is [unknown] until the
  board's "swaps not reported" count is seen.
* **Pacing cannot starve anything and its lag is bounded** by `pace_wait_ms` rounded up to whole
  10 ms ticks (15 ms is 20 at most), whatever the guest does [verified: code].
* **The drawing speed-ups draw the same pictures:** `test_vbe`'s 6,856 checks a board (random
  stores of every size through the LFB and the window, every depth, centred, shrunk, longer lines,
  display start changes) pass with the rows' parts and 256-byte blocks; a part can never run past
  the picture, since VBE widths are kept to multiples of 8 and the 1:1 path needs a multiple of 4
  [verified: section 5].
* **The grey screen fix is right and costs nothing at rest** [verified: `test_font` passes here and
  fails v4.3's `vga.c` with 5 checks]. Two corners it does not cover are notes (section 7).
* **The docs need two corrections (minor):** `TEST-PLAN.md` 16c sends Plat to "Ctrl+Alt+F12's
  `display:` line" for the swaps not reported, but that line goes only to the UART; and 16c's
  memory figure (14M to 13M at 768K) disagrees with `BUILD-NOTES.md` 29.6 (13M to 12M).


## 1. What I ran

* **Host tests** at 6d619a0, `bash tests/host/run.sh ~/org2/review-display/ht` from the worktree:
  `host tests: 53 passed, 0 failed` [verified: `~/org2/review-display/ht.log`]. Among them
  `test_vbe: all 6856 checks passed` on both boards, `test_font: all checks passed` on both boards
  and v4.3's `vga.c` failing it (5 checks) on both, `test_pace` on both, `test_ini_edit: 106 checks,
  0 failed`. My run had the v4.0-base comparison, because the WSL repository still holds the old
  tags. One gap: `run.sh` asks for `v4.0-base` and `pristine` by those names, and the fork
  (`D:\Repos\tiny386`, 22 tags, none of them those) does not have them, so in a clone of the fork
  `test_vbe` runs without its v4.0-base comparison and the pristine checks drop out, saying nothing
  [verified: `run.sh` sections 1 and 3c; both repositories' tag lists].
* **A double-buffering model** of my own, `~/org2/review-display/dbuf/test_dbuf.c` (section 3.4),
  linked with the objects the host tests built (`vbe-new-korvo1.o`, `vbe-ref-korvo1.o`).
* **Source reading** of everything part 1 changed (`git diff 8d4b758 de24383`, 3,505 lines without
  the notes and checks), of the ESP-IDF the build rules name (`~/esp/esp-idf`,
  v6.1-dev-8142-g188e3e55bb: `esp_lcd/rgb/esp_lcd_panel_rgb.c`, the S31's AXI GDMA, TLSF, FatFs
  `ff.c`), and of the board build's `sdkconfig` (`~/org2/v44disp/bt-fin2/src/esp/sdkconfig`). The
  firmware's two IDF patches touch `wlanif.c`, `esp_psram.c`, `ffconf.h` and an Xtensa header, not
  the LCD, GDMA or heap code [verified: their file lists].
* **Not rerun:** the flicker harness, the T386MON harness and the board builds (part 1's and part
  2's evidence in `s31/checks` was read, not repeated). My model covers the double buffering's
  bookkeeping, which the harness's `-double` models only in its main path (no Ctrl+Alt+F9, no speed
  screen) [verified: `s31/checks/flicker/harness-v4.4.diff`].


## 2. Ctrl+Alt+F11 saving

**What holds** [verified: code]:

* The save runs on the emulator's core between two steps, after `disk_flush()` of any unflushed
  guest writes, so it never meets a FatFs operation of the guest's; the disk images are FatFs `FIL`s
  of their own and the save uses one stdio file at a time, inside the mount's `max_files = 5`.
* `setup_ini_save()` writes `X.t386new`, `fflush()`es, `fsync()`s (FatFs `f_sync`), closes, reads
  it back and compares, before anything else changes. ESP-IDF's FAT `rename()` does not replace an
  existing name, so removing `X.t386old` first is needed, as the notes say.
* `ini_edit.c` reads lines as tiny386's `ini.c` does (`INI_ALLOW_MULTILINE`, `;` and `#` start
  comments, inline `;` after a space, BOM). One small difference: `ini.c` reads at most
  `INI_MAX_LINE` (200) bytes a line and treats the rest as a new line; `ini_edit.c` treats a line
  as everything to its `\n`. Only a line over 199 bytes inside `[esp]` could be read differently
  [verified: `ini.h`] (note).
* Each of the four states a cut *between* two steps leaves is put right by `setup_ini_tidy()`
  [verified: code and `test_ini_edit`].

**What does not** [verified: `~/esp/esp-idf/components/fatfs/src/ff.c`, `f_rename()`]: FatFs makes
the new directory entry (`dir_register()`), copies the old entry's start cluster into it, and only
then marks the old entry deleted (`dir_remove()`), with a comment around that stretch: "Start of
critical section where an interruption can cause a cross-link". When the two entries are in
different directory sectors (likely in `tiny386\`, which has many files; each long name takes three
slots), the first sector is written before the second, and a cut between them leaves both names
pointing at one chain. Then at the next start:

* cut inside `rename(X, X.t386old)`: `X` and `X.t386old` share the old text's clusters, and
  `X.t386new` is whole. `setup_ini_tidy()` sees `X`, removes `X.t386new` (the good new copy) and
  removes `X.t386old`, and `f_unlink()` frees the chain that `X` still points at;
* cut inside `rename(X.t386new, X)`: `X` and `X.t386new` share the new text's clusters.
  `setup_ini_tidy()` removes `X.t386new` (freeing `X`'s chain) and `X.t386old` (the old copy).

Either way `X` reads correctly at that start, then turns into whatever the card next writes into
those clusters, and no copy is left. The next save's own `remove(X.t386old)` can do the same.
The host test cannot reach this: POSIX `rename()` is atomic [inferred]. The window is milliseconds
of a save that takes perhaps a few hundred, so the realistic trigger is Plat pressing a key and
switching the board off straight away [inferred].

**Fix** (no rename, so no cross-link, so every `unlink` is safe): write `X.t386new`, sync and read
it back as now; then rewrite `X` in place (`fopen(X, "wb")`, write, `fflush`, `fsync`, close, read
back); then remove `X.t386new`, which was never renamed and so shares nothing. A cut while `X` is
rewritten leaves `X` short or empty and `X.t386new` whole (at worst some lost clusters, never a
cross-link). `setup_ini_tidy()` then needs one rule: `X.t386new` present and `X` different from it
(or missing) means copy its bytes into `X` and sync, then remove `X.t386new`; `X` the same means
just remove `X.t386new`. If the rename design stays, the least change is to never unlink a leftover
whose bytes equal `X`'s (it may be the same chain), to leave all three names alone when all three
exist, and to say on the console that the card wants a `chkdsk`. And `TEST-PLAN.md` 16b could ask
Plat to wait for the "saved in" line before switching off.


## 3. Double buffering

### 3.1 Memory [verified: code]

* The first buffer is `round4k(800 x 480 x 2)` = 770,048 bytes (752 KiB) from the top of the
  emulator's pool, which is allocated 4 KiB-aligned, so it is aligned; `pcmalloc()`,
  `settle_memory()` and `psram_test()` all use the reduced `psram_len`, so nothing of the guest's can
  reach it. The pool must keep 2 MiB beyond it, or double buffering is refused with a line.
* The second buffer is `heap_caps_calloc()`ed where the driver's own buffer was, after
  `esp_lcd_panel_del()` frees that. ESP-IDF's TLSF allocates the request rounded up to its size
  class (`block_locate_free()` passes the rounded size on to `block_prepare_used()`), and both
  allocations are the same request through `tlsf_malloc()`, so the freed block fits the new one
  exactly [verified: `heap/tlsf/tlsf.c`, `heap_caps_base.c`; the board unknown].
* The driver checks user buffers only for DMA reach; the AXI GDMA's alignment for PSRAM is 1 here
  (no flash encryption, no PSRAM ECC, no weighted arbitration in the build's `sdkconfig`), so the
  calloc'd buffer is accepted [verified: `gdma.c`, `esp_mspi_align.c`, `sdkconfig`; flash
  encryption is an eFuse, assumed off on Plat's board].
* What it costs the guest is 1M whenever the pool, less `round4k(vga_mem_size + 16K)`, lies less
  than 752 KiB above a whole megabyte. Plat's own pool is [unknown]: `BUILD-NOTES.md` 29.6 infers
  13M becoming 12M at 768K, `TEST-PLAN.md` 16c says 14M becoming 13M, and the card's
  `win95-14m.ini` says 14M fits beside 320K only "if" the pool allows it. The boot console photo
  that 16c asks for will settle it (finding F4).

### 3.2 The swap [verified: ESP-IDF source]

* `lcd_vga_present()` calls `esp_lcd_panel_draw_bitmap(panel, 0, 0, 1, 1, back)`. With a pointer
  inside one of the panel's buffers the driver copies nothing: it sets `cur_fb_index`, writes back
  row 0's cache lines, points every buffer's DMA list at that buffer (`gdma_link_concat()`), and
  calls `gdma_request_link_switch_event()`.
* On the S31 `RGB_LCD_USE_GDMA_LINK_SWITCH_EVENT` is 1 (`SOC_AXI_GDMA_SUPPORTED`, the S31's
  `axi_dma_ll.h` has `AXI_DMA_LL_SUPPORT_TX_LINK_SWITCH 1`, and the LCD's DMA trigger is on the AXI
  bus), so the driver registers no per-frame EOF callback in stream mode and calls
  `on_frame_buf_complete` from the link-switch interrupt. The firmware's binary semaphore therefore
  stands for "the DMA has moved to the buffer it was told", which is what the swap needs; the
  stale-give race of a per-frame EOF does not arise.
* Two things remain open [inferred]: if the DMA passes the end of its frame between the driver's
  `gdma_link_concat()` and its request for the event, the switch has happened but the event may not
  come, so `lcd_vga_present()` waits its 100 ms, counts a swap "not reported" and carries on
  correctly; and what the S31's link-switch interrupt reports exactly is [unknown] until the board
  shows the count staying at 0. While it waits, the display lock is held for up to a frame (28 ms,
  100 ms at worst); only `vga_task`, start-up and the console's own drawing take it, and the console draws only
  while it owns the panel, when no swap happens [verified: `console.c` `render()`].

### 3.3 Both buffers kept right [verified: code]

* Every row `vga.c` writes goes to `redraw()` (its clears set `fb_all_dirty` and the single-buffer
  board already relies on this for the cache write-back), `lcd_vga_flush()` notes those rows, and
  after the swap exactly those rows are copied from the new front to the new back, so the back
  always holds the last picture, which `vga.c`'s store-only-where-it-differs and the rows' parts
  assume. The copy loop's `y = y1` skips only an unmarked row.
* The console and the crash screen follow the front buffer (`console_set_fb()` at each swap,
  `lcd_crash_second_fb()` for a crash between the request and the swap). After the speed screen,
  `give_back()` asks for a copy of every row and a full update.
* `vga_set_fb()` only replaces `fb_dev->fb_data`; every drawing path in `vga.c` reads the pointer
  from there (no other copy of it is kept), and `pc.c` keeps none.

### 3.4 My model, and the one fault it found [verified: run]

`test_dbuf.c` drives vga.c (the Korvo-1 build) and a reference build without `VGA_DIRTY_BLOCKS`
asked for a full update at every picture. Around the board build it models `lcd_vga_flush()`,
`lcd_vga_present()`, `lcd_double_set()` (Ctrl+Alt+F9) and the speed screen (the console drawing
over the front buffer, then `give_back()`, with and without a turn in which `vga_step()` gives no
picture). After every picture: the panel's buffer must equal the reference, the back must equal the
front, and no row may change without reaching `redraw()`. Eight modes, each run 120 turns starting
with double buffering off and again on, then a fixed scenario per mode:

| | checks | failed | failed after F9 turned it on during the speed screen | failed otherwise |
|-|-|-|-|-|
| as v4.4 is | 3,004 | 465 | 465 | 0 |
| with the fix below | 3,004 | 0 | 0 | 0 |

(1,936 pictures, 880 swaps, 156,558 rows copied in the random runs; in the fixed scenario, the
speed screen with no key fails nothing in any mode, and with F9 pressed twice on it fails 3 checks
in each mode narrower than the panel.)

The failures (`dbuf/dbuf.log`): the console's text left in the border, for example "80x25 text,
after the speed screen (double on): the panel's buffer differs from a full redraw in 31680 pixels,
first at (4,0)", and it stays for every later picture of the run. Text, mode 12h, mode 13h and VBE
640x480 at 16 and 24 bits fail; VBE 800x480 at 8 and 16 bits and 800x600 shrunk do not, because a
full update redraws every pixel of a picture that fills the panel. The cause: `lcd_double_set(true)`
copies the front buffer into the back to make them equal, and while the speed screen is up the
front holds the console; `give_back()` then relies on the back buffer being one "which the console
did not touch" (its comment, and `BUILD-NOTES.md` 29.6), and the full update repaints only the
picture, not the border.

**Fix** (tested in the model as `test_dbuf_fix.c`, 0 of 3,004 failed): in `give_back()`'s double
path, clear the back buffer and write it back before asking for the copy and the full update, as
the single-buffer path clears its one buffer:

```c
if (lcd_double_on()) {
	memset(lcd_vga_fb(), 0, LCD_WIDTH * LCD_HEIGHT * 2);
	lcd_vga_flush(0, 0, LCD_WIDTH, LCD_HEIGHT);   /* written back, every row noted */
	lcd_vga_copy_all();
}
```

The back buffer is not on show, so this brings no black to the panel. (Clearing the back in the
F9 handler when the console owns the panel would do too.)

### 3.5 Notes

* `double_start()`'s last fallback (the driver cannot even allocate its own buffer again) sets
  `s_fb` to NULL but never tells the console, which keeps the freed buffer's address and would
  draw into freed PSRAM; `esp_main.c` then says "one is used" though there is no panel. Section 3.1
  makes the path very unlikely; `console_set_fb(NULL)` there and a plainer line would close it.
* At start-up `lcd_double_start()` holds the display lock and calls `console_set_fb()`, which takes
  the console's lock; a task printing a line holds the console's lock and waits up to 200 ms for
  the display lock (`display_trylock_draw()`). The timeout ends it, the line is drawn later: a
  bounded stall, not a deadlock.
* T386MON's `draw_ms` (the information port) times `vga_refresh()` only, so with
  `double_buffer = 1` it leaves out the wait for the panel and the copy; the UART's `display:` line
  has them. Worth saying when Plat compares the two cures.


## 4. Pacing [verified: code]

* `display_pace_step()` runs after every `pc_step()` (512 guest instructions on the ESP32 build);
  `vga_pace_after_step()` clears `vga_wrote` and notes the count at the last store. Every store path
  sets the flag: the four LFB stores in `pc.c` (on the ESP build the LFB is never mapped straight
  into the CPU: `cpu_register_mem()` is a no-op there) and the four window paths in `vga.c`.
* `pace_wait()` clears a stale notification, sets `s_pace_want`, and blocks in `ulTaskNotifyTake()`
  for `ceil(pace_wait_ms / 10 ms)` ticks; the emulator's core clears the flag and notifies once,
  when it finds the guest quiet. Nothing waits without a timeout and the emulator's core never
  waits at all, so neither side can starve the other; a picture is late by at most the wait
  (20 ms with the default 15), and with double buffering on top a picture reaches the panel at most
  about wait + draw + one frame + copy after it was due.
* With pacing off the step still pays a call and a load (about 45 instructions per 512) [verified:
  BUILD-NOTES 29.11's disassembly counts].
* Note: the counts are 32 bits, so after 2^32 instructions with no store (about 36 minutes at 2
  million a second) a quiet guest looks busy for `pace_quiet` instructions once.
* Keeping it off by default is supported by part 1's own tables (T386MON's tests lose a third of
  their pictures and the sprite stays black); the card inis' "5000 keeps out pictures caught part
  way through Windows' drawing" overstates 7, 4 and 4 per cent becoming 2, 0 and 1 (note).


## 5. The drawing speed-ups [verified: code and `test_vbe`]

* `vga_take_marks()` gives each marked row of a 1:1 picture the pixels its blocks reach, rounded
  out to multiples of 4, as the union of its blocks; `vga_vbe_refresh()` converts only those. The
  1:1 path is taken only when `w` is a multiple of 4 and the line length a multiple of 4, and
  `vbe_write()` keeps VBE widths to multiples of 8, so `x1` never passes `w` and the source and
  destination stay word-aligned (24 bits: `x0 * 3` is a multiple of 12).
* Rows are only ever marked in non-whole pictures and every marked row is consumed in the same
  picture, so `rowx0`/`rowx1` are never read stale; the shrunk path still marks whole rows.
* `test_vbe` (both boards, 6,856 checks each) compares every picture pixel for pixel with the old
  whole-picture loop under random stores of 1 byte to 4 KiB through the LFB and the window, at 8,
  15, 16, 24 and 32 bits, at the screen's size, centred (320x200, 512x384, 640x480) and shrunk,
  with longer lines and display start changes, with `vbe_blocks` flipped, and with a second thread
  writing; my model adds 640x480 at 24 bits centred with double buffering. All pass.
* 40b4513's KiB count for the information port is carried correctly (`test_vga_info`'s added
  check passes). The 256-byte map is four times larger (3 KiB at 768K): a still picture reads
  about 3,000 bytes, negligible.
* The claim that the drawing is bound by PSRAM rather than instructions (29.4) rests on 256 colours
  and 16 bits taking the same 41 ms; that is also what a cost dominated by reading and writing the
  frame buffer itself would give. [inferred] is the right mark, and nothing depends on it.


## 6. The text investigation and the pointer [not rerun]

I did not repeat the harness's instruction sampling. The reasoning holds as written: the board's
guest runs 1.68 million instructions a second in the 16-bit text test and 1.81 at 640x480 in 16
colours, so the difference in frames a second has to come from instructions a frame, and a DIB
engine that writes two pixels a store against a planar driver writing eight explains the factor
[inferred]. Our store path's share (2 to 3 per cent) is [inferred] from instruction counts.
Leaving the hardware pointer alone fits Plat's own words ("Pointer movement has never felt slow,
just using the system does") and the 7 per cent the pointer costs only when it sits over drawing.


## 7. The grey screen [verified: code and `test_font`]

The fix counts map mask (SR02) writes that change plane 2's write enable while GR06 says text, and
redraws the whole text screen at the picture after one, at every picture while plane 2 is writable,
and when SR03 changes. Ordering between the cores is right (`vga_ask()`'s release store, an acquire
load in the drawing). It also makes right something v4.3 got wrong without anyone noticing: DOS
programs that draw a graphical mouse pointer in text mode by rewriting glyphs now show every glyph
change, not only those that come with a new character code [inferred]. Notes:

* A font loaded with GR06's graphics bit set while SR02 is written (a hand-made loader using the
  64 KiB graphics mapping), wholly between two pictures, is not counted and would leave old glyphs;
  the BIOS and every loader I know use GR06 = 04h [inferred].
* A text program that leaves SR02 bit 2 set gets a whole text redraw every picture: a cost on
  core 0, not a wrong picture.


## 8. Findings

| # | Severity | Where | Problem | Evidence | Fix |
|-|-|-|-|-|-|
| F1 | minor | `esp/main/speed.c` `give_back()`, `lcd_korvo1.c` `lcd_double_set()` | Ctrl+Alt+F9 turning double buffering on during the speed screen leaves the console's text in the border of pictures narrower than the panel until the next mode change | model: 465 of 465 failures in that case, 0 elsewhere; text, 12h, 13h, VBE 640x480 | clear and write back the back buffer in `give_back()`'s double path (section 3.4); 0 failures in the model |
| F2 | minor | `esp/main/setup.c` `setup_ini_save()`, `setup_ini_tidy()` | a cut inside a FatFs rename leaves two entries on one chain; tidy (or the next save) unlinks one and frees the live ini's clusters | `ff.c` `f_rename()` and its own cross-link comment; tidy's rules | rewrite the ini in place with `X.t386new` as the safe copy, no rename (section 2) |
| F3 | minor | `s31/TEST-PLAN.md` 16c | tells Plat to read "swaps not reported" on Ctrl+Alt+F12's `display:` line; that line is UART only (`console_uart_printf()`, `TO_UART`), and the speed screen's double-buffering line has no counts | `display.c`, `console.c` | point to the UART with `uart_log = 1`, or add the counts to `double_line()` |
| F4 | minor | `s31/TEST-PLAN.md` 16c, `BUILD-NOTES.md` 29.6 | the memory double buffering costs is given as 14M to 13M in one and 13M to 12M in the other | the two texts | make them agree, and say the boot console's lines decide |
| F5 | note | `lcd_korvo1.c` `lcd_vga_present()` | a switch made between the driver's relink and its event request may never be reported: a 100 ms stall and a count, the swap itself right | IDF `rgb_panel_update_dma_link()` | none needed; read the count on the board |
| F6 | note | `lcd_korvo1.c` `double_start()` | last fallback leaves the console pointing at the freed driver buffer | code | `console_set_fb(NULL)` and a plainer line |
| F7 | note | `lcd_double_start()` with `console_set_fb()` | the two locks taken in opposite orders at start-up; ended by the 200 ms timeout | `console.c` | none needed; a console call that takes both locks in the console's own order would remove it |
| F8 | note | information port, TEST-PLAN 16c | `draw_ms` leaves out the double buffer's wait and copy | `pc.c` `pc_vga_step()` | say so where Plat compares |
| F9 | note | `vga.c` font check | a font load with GR06's graphics bit set between two pictures is not seen | code | count SR02 bit 2 changes whatever GR06 says, if it ever matters |
| F10 | note | `tests/host/run.sh` | the v4.0-base and pristine comparisons silently skip in a clone of the fork | `run.sh` 3c | give the two commits `s31-` tags on the fork and find them with `tagref()`, or print that the comparison was skipped |
| F11 | note | `ini_edit.c` | lines over 199 bytes are split by `ini.c` but not by the editor | `ini.h` `INI_MAX_LINE` | none needed |


## 9. For Plat (beyond TEST-PLAN 16)

* With `double_buffer = 1`, avoid pressing Ctrl+Alt+F9 on the speed screen until F1 is fixed; if you
  do and see old log text round a DOS or 640x480 picture, a mode change clears it.
* After pressing Ctrl+Alt+F11, F10 or F9, wait for the "saved in" line before switching off (F2).
* The swaps "not reported" count is on the UART's `display:` line every 10 s with `uart_log = 1`,
  not on the speed screen (F3).
* T386MON's `draw_ms` with double buffering on does not include the wait for the panel or the copy;
  the UART line does (F8).


## 10. Where things are

WSL `~/org2/review-display/` (mine, 50 MB, throw-away): `ht/` and `ht.log` (the host tests
at 6d619a0), `part1.diff` (the part 1 diff read here), `dbuf/test_dbuf.c` and `dbuf/dbuf.log`
(the model and its run), `dbuf/test_dbuf_fix.c` and `dbuf/dbuf-fix.log` (with F1's fix), and the
small scripts (`x.sh`, `ht.sh`, `db.sh`, `dbf.sh`). Nothing I started is still running.
