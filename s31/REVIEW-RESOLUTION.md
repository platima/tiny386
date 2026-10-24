# tiny386 v3: what became of each review finding

Written 2026-10-21. Two review rounds, four reviewers each, with the same four lenses:
`REVIEW-concurrency-esp-idf.md` (C), `REVIEW-boot-safety-flash.md` (B), `REVIEW-emulator-correctness.md`
(E) and `REVIEW-dosboard-drivers.md` (D). Each of those files has its second round first and its
first round, word for word, as an appendix.

* Round 1 read tag `v3` (2bb1c76); its fixes are tag `v3.1` (80e64de). Its table is the second
  half of this file, as it was written.
* Round 2 read tag `v3.1-tests` (823d5cd), whose firmware is v3.1's. Its fixes are tag `v3.2`
  (d3c839c, five commits on `v3.1-tests`, in the WSL tree `~/tiny386-idf-scratch/v3/tiny386`),
  and the app calls itself `v3.2`.
* Round 3 (2026-10-22) was one reviewer, `REVIEW-v32.md`, on tag `v3.3` (7e0a36a: the status LED
  and the DOS board's line out, `BUILD-NOTES.md` 14) against `v3.2`, and the dual boot against
  v3.1. Its fixes are tag `v3.4` (a33b7cf, five commits on `v3.3`), and the app calls itself
  `v3.4` (`BUILD-NOTES.md` 15 says why the tag is new rather than a moved `v3.2` or `v3.3`).
  v3.3's own files are in `previous-v3.3\`.
* Round 4 (2026-10-22) was one reviewer, `REVIEW-v35.md`, on tag `v3.5` (07e7784: the DOS board's
  rev A2c pins, its speakers and the louder mix, `BUILD-NOTES.md` 16) against `v3.4`. Its fixes are
  tag `v3.6` (9d6109e, six commits on `v3.5`), and the app calls itself `v3.6`. v3.5's own files
  are in `previous-v3.5\`.
* Round 5 (2026-10-22) was two reviewers on tag `v3.8` (3bded9d: the Windows 95 fix, the disk light
  and the quieter log, `BUILD-NOTES.md` 19 and 20), each with a lens of its own:
  `REVIEW-v38-emulator-and-windows.md` (here EW: the emulator, Windows 95, the PC harness, the Linux
  route) and `REVIEW-v38-board-safety-and-features.md` (here BS: the flash scripts and the
  tiny386-default table, the disk light, the limit on repeated lines and the UART queue, the
  package). Its fixes are tag `v3.9` (d370a7a, four commits on `v3.8`) and the scripts beside this
  file, and the app calls itself `v3.9` (`BUILD-NOTES.md` 21). v3.8's own files are in
  `previous-v3.8\`.
* Round 6 (2026-10-23) was two reviewers on tag `v4.0` (8d65b1d: v3.9 merged with tiny386's master
  of 2026-10-22, and Windows at the panel's own size, `BUILD-NOTES.md` 22), each with a lens of its
  own: `REVIEW-v40-emulator-vbe-merge.md` (here EV: the merge, the VBE drawing, the VGA BIOS, the PC
  evidence) and `REVIEW-v40-board-and-package.md` (here BP: memory and cost on the board, the DOS
  board, the images and scripts, the card and BIOS files, the docs). Its fixes are tag `v4.1`
  (f61337e, four commits on `v4.0`, branch `v41`), and the app calls itself `v4.1`
  (`BUILD-NOTES.md` 23). They are in this folder's patches, images, card files and notes; v4.0's
  own are in `previous-v4.0\`.
* Round 7 (2026-10-23) was not a review but Plat's report on v4.1 at 800x480 (the mouse pointer and
  anything moving flickering), the brief's five suspects for it, and two investigations on the PC:
  `research\FLICKER-REPRO.md` (here R) and `research\FLICKER-AUDIT.md` (here A). Its fixes are tag
  `v4.2` (6502063, five commits on `v4.1`, branch `v42`) and `qemumini-pointer\`, and the app calls
  itself `v4.2` (`BUILD-NOTES.md` 24 and 25); v4.1's own files are in `previous-v4.1\`.
* Round 8 (2026-10-23, night) was one reviewer on tag `v4.2` and its package, `REVIEW-v42.md`: no
  blocker, no major, four minor findings and six notes. None needed the firmware changed, so the app
  is still `v4.2` and its images, patches and card files are as round 7 left them; the evidence and
  the docs changed (`BUILD-NOTES.md` 26).
* Round 9 (2026-10-24) is not a new review but v4.3: v4.2 with the information port merged from the
  branch `perfport`, whose review (`..\t386mon\REVIEW.md`, one reviewer on the port and T386MON 1.1)
  was answered on that branch and in T386MON 1.2 (`..\t386mon\README.md` 11). This round says where
  each of its findings stands in the released firmware. v4.3 is tag `v4.3` (0f87c1c, branch `v43`:
  the merge and two commits on `v4.2`), and the app calls itself `v4.3` (`BUILD-NOTES.md` 27); v4.2's
  own files are in `previous-v4.2\`.
* Round 10 (2026-10-24) was one reviewer on tag `v4.3` and its package, `REVIEW-v43.md`: no
  blocker, no major, two minor findings and five notes. None needed the firmware changed, so the app
  is still `v4.3` and its images, patches and card files are as round 9 left them; the docs changed
  (`BUILD-NOTES.md` 28).
* Round 11 (2026-10-24) was two reviewers on tag `s31-v4.4` (58f868d: the display work and the card
  partition, `BUILD-NOTES.md` 29 and 30, the first release made on the fork's `esp32-s31` branch),
  each with a lens of its own: `REVIEW-v44-display.md` (here D) and
  `REVIEW-v44-storage-and-safety.md` (here S). No blocker, no major; four of the minor findings
  needed the firmware changed, so its fixes are tag `s31-v4.5` (six commits on `s31-v4.4` and the
  notes), and the app calls itself `v4.5` (`BUILD-NOTES.md` 31); v4.4's own files are in
  `previous-v4.4\`.

Nothing of these rounds' fixes has run on a board (Plat has run v3.6, v3.9, v4.1 and v4.3 [reported]), no
serial port was opened, nothing was flashed.

Status: **fixed** (changed, and checked as the line says), **documented** (no code change; the
behaviour is now stated where Plat reads it), **deferred** (left, with the reason), **no change**
(the review itself found it right), **rejected** (with the evidence; none in any round so far).

Tags as everywhere in this folder: [verified], [inferred], [reported], [unknown]. The evidence named
`checks\...-v3.2...` is round 2's (`BUILD-NOTES.md` 12 lists it), `checks\...-v3.4...` round 3's
(`BUILD-NOTES.md` 15), `checks\...-v3.6...` round 4's (`BUILD-NOTES.md` 17), `checks\...-v3.9...`
round 5's (`BUILD-NOTES.md` 21), `checks\...-v4.1...` round 6's (`BUILD-NOTES.md` 23),
`checks\...-v4.2...` round 7's (`BUILD-NOTES.md` 25), `checks\...-v4.2-review...` and
`checks\route-c-v4.2.txt` round 8's (`BUILD-NOTES.md` 26), `checks\...-v4.3...` round 9's
(`BUILD-NOTES.md` 27), `checks\...-v4.3-review...` round 10's (`BUILD-NOTES.md` 28),
`checks\...-v4.5...` and `checks\findings-v4.4-review.txt` round 11's (`BUILD-NOTES.md` 31).


## Round 11: the two reviews of v4.4 (`REVIEW-v44-display.md`, `REVIEW-v44-storage-and-safety.md`)

Two reviewers, each with a lens of its own, on tag `s31-v4.4` (58f868d) and its package:
`REVIEW-v44-display.md` (here D: the keys' saves, the double buffering, the pacing, the drawing
speed-ups, the grey screen) and `REVIEW-v44-storage-and-safety.md` (here S: `hda = sd:N`, the
guest's MBR and geometry, the refusals, the new-card procedure, the dual-boot images, what was
pushed). No blocker and no major finding in either, so there was none to reproduce or reject; each
point was checked against the tag before it was acted on [verified:
`checks\findings-v4.4-review.txt`]. Four of the minor findings needed the firmware changed (D1, D2,
S2, and the firmware's side of S4), so v4.5 is a new tag, `s31-v4.5`, rather than v4.4 again:
`s31-v4.4` was already on github.com/platima/tiny386 and its images in this folder, and a published
tag is never moved (`BUILD-NOTES.md` 15 says the same of v3.4). The app calls itself `v4.5`; v4.4's
own files are in `previous-v4.4\` (`BUILD-NOTES.md` 31).

The storage reviewer's own accident (a probe filling WSL's disk between about 14:12 and 14:17) came
before any of this round's work started; `git fsck` of the canonical repository found nothing wrong
[verified], and nothing of this round was running then.

### Display (D)

| # | Severity | Status | What was done, or why not |
|-|-|-|-|
| 1 | minor | fixed | Confirmed: `lcd_double_set(true)` copies the front buffer, the console's while the speed screen is up, into the back, and v4.4's `give_back()` asked only for the copy and a full update [verified: the tag's code]. `give_back()` now clears the back buffer and writes it back first (`memset`, `lcd_vga_flush()` of every row, then `lcd_vga_copy_all()`), the review's fix; the back buffer is not on show, so the panel never goes black (71f9ba0). The review's own model rebuilt here against v4.5's `vga.c` objects: 465 of 3,004 checks fail without the fix and 0 with it, and v4.5's `give_back()` does what the model's fix does [verified: `checks\findings-v4.4-review.txt`]. On the board [unknown] |
| 2 | minor | fixed | Confirmed: ESP-IDF's FatFs `f_rename()` registers the new entry, then removes the old, inside its own comment "Start of critical section where an interruption can cause a cross-link"; `f_unlink()` removes the entry before the chain [verified: `ff.c`]. `setup_ini_save()` now renames nothing: a whole copy of the new text with a check line (its length and CRC-32) beside the ini, then the ini rewritten in place, then the copy removed; `setup_ini_tidy()` puts a whole copy in place, removes one cut short, still handles every state v4.4's saves can leave, and leaves what may be two names on one chain (all three names, or a `.t386old` with the ini's very bytes) with a yellow line saying to check the card on a PC; a save tidies its own ini first and refuses while v4.4's leftovers want that check (9bf35d0). `test_ini_edit.c`: the ini rewritten in place (the same file), a cut at each of the five steps followed by the next start (the ini whole, as it was or saved, nothing beside it, listed), the tidy's states, the warnings; 149 checks, 0 failed, also under AddressSanitizer and UBSan [verified: `checks\host-tests-v4.5.log`, `BUILD-NOTES.md` 31.2]. `TEST-PLAN.md` 16b asks Plat to wait for the "saved in" line before switching off. FatFs on a real card and a real cut [unknown] |
| 3 | minor | fixed | Confirmed: the swaps not reported were on the UART's `display:` line only, and the speed screen's line had no counts [verified]. The speed screen's `double_buffer` line now gives the swaps since the start and how many the panel did not report (`lcd_double_totals()`, 71f9ba0), and `TEST-PLAN.md` 16c points there, and to the UART line for the copy's and the wait's time |
| 4 | minor | fixed (docs) | Confirmed: 16c said 13M rather than 14M at 768K, 29.6 13M becoming 12M [verified]. Both now say it costs Windows 1M or nothing, that Plat's pool is [unknown], and that the boot console's "Emulator memory" and `mem_size` lines in 16c's photo decide it (`BUILD-NOTES.md` 29.6, 31.5) |
| 5 | note | no change; [unknown] on the board | A switch made between the driver's relink and its request for the event costs a 100 ms wait and a count, the swap itself right, as the review says. The count is now on the speed screen too (row 3), so Plat's photo shows it |
| 6 | note | fixed | The last fallback (the panel not even started again with one buffer) now tells the console there is no frame buffer (`console_set_fb(NULL)`), and the boot console says in red that only the UART shows what follows (71f9ba0) |
| 7 | note | no change | The two locks taken in opposite orders at start-up end in the console's 200 ms timeout: a bounded stall once, before the PC runs, as the review found |
| 8 | note | documented | `TEST-PLAN.md` 16c: T386MON's `draw_ms` leaves out the double buffer's copy and wait; the UART's `display:` line has them |
| 9 | note | deferred | A font loaded through the graphics mapping (GR06's graphics bit set) wholly between two pictures is not seen. Counting every SR02 bit 2 change would add a store to Windows' 16-colour drawing, which writes the map mask all the time, for loaders that the BIOS and every known program do not use; as the review says, "if it ever matters" (`BUILD-NOTES.md` 31.8) |
| 10 | note | fixed | Confirmed: the fork clone has neither tag [verified: `git tag` in `D:\Repos\tiny386`]. `run.sh`'s `tagref()` now takes the commit itself as a last resort (9ed786b for pristine, 6c22084 for v4.0-base, both on the branch) and says when it skips (39c3d99); in a clone of the fork with only its `s31-` tags, all the comparisons run [verified: `checks\host-tests-v4.5-fork-clone.log`] |
| 11 | note | no change | Only a line over 199 bytes inside `[esp]` could be read differently by `ini.c` and `ini_edit.c`; none of the card's inis has one |

### Storage and safety (S)

| # | Severity | Status | What was done, or why not |
|-|-|-|-|
| 1 | minor | fixed (docs) | Confirmed: `export-root.sh` writes three files onto WIN9X (and the procedure copies itself onto KORVO_SD), may make `/mnt/win`, and makes the root read-only and back; the docs said "never written" and "untouched" [verified]. `newcard\README.md` (intro, step 1, step 7), `TEST-PLAN.md` 16e, `BUILD-NOTES.md` 30.0 and `FIRMWARE.md`'s row now say what happens: files added to the old card's two FAT drives, its table, Linux root and `win95.img` not changed; "Afterwards" says to delete `korvo-root.img` once the new card works |
| 2 | minor | fixed | Confirmed: on Plat's card `hda = sd:3` handed the guest the WIN9X partition [verified: the tag's host test 14b]. `sd_part_plan()` refuses a volume labelled WIN9X, and `esp_main.c`, while FatFs still has the partition, refuses one holding `win95.img` or `korvo-root.img` (`sd_part_route_files()`) (076114f). Test 14b: Plat's card as it is now refused, nothing written, and the same card with p3 relabelled given whole as before; on the PC harness a new card whose p3 is labelled WIN9X stops with the line [verified: `checks\sdpart-host-test-v4.5.log`, `checks\partition-passthrough-v4.5.txt` 3]. `TEST-PLAN.md` 16a and `win95-sd.ini` say the ini goes on the new card only |
| 3 | minor | fixed | The nine lines are paraphrased ("unit 00: through real-mode drivers") in `checks\partition-passthrough-v4.4.txt` on the branch and here; this round's harness check prints the units the same way. The old text stays in the branch's history (58f868d): rewriting a pushed branch is Plat's decision |
| 4 | minor | fixed | Confirmed: `newcard.sh` copied the image's type as it was [verified: the tag's code]. Past the 504 MiB CHS reaches, p3 now gets the LBA twin of its type (06 or 04 to 0E, 0B to 0C), with a line, and a note when the boot sector is not Windows 95 OSR2's (8123d17); `sd_part_plan()` gives a yellow note for such a partition (076114f). On stand-in images: 06 and 0B become 0E and 0C past 504 MiB and stay below it, every run "ready" [verified: `checks\newcard-v4.5.txt`] |
| 5 | note | fixed | `export-root.sh` finds p2's mount point by name, else by its device number in `/proc/self/mountinfo`, and stops rather than copy a `/dev/root` it cannot tie to p2 or not (8123d17). In its test mode under `dash` and `bash`: the root found by number, another root left alone, no `mountinfo` refused, p2 by name found [verified: `checks\newcard-v4.5.txt`]. On the board [unknown] |
| 6 | note | fixed | The overlap check passes over only an entry with no size; a type 00 entry with a size inside p3 is refused (076114f; test 14b) |
| 7 | note | fixed | The guest's disk is capped at 0FFFFFFFh sectors, IDENTIFY's 28-bit limit, and a partition must end by then; one ending at 2^28 is refused, one ending in the last part cylinder gets the cap (076114f; test 14b) |
| 8 | note | documented | "Every sector or none" is per transfer of up to four sectors: `sd_part.h`, `sd_part.c` and `BUILD-NOTES.md` 30.2 say so |
| 9 | note | documented | `newcard\README.md` step 4 and "Afterwards", `TEST-PLAN.md` 16e: one card in the PC at a time (the shared disk signature), the image writer's target checked by its size, `korvo-root.img`'s secrets deleted afterwards |
| 10 | note | fixed | `check_card_partition()`'s steps 1 to 3 are `sd_part_ini_check()` now, in test 14b (the name, a hard disk only, nothing else on the partition, names that only look alike), and the kernel, initrd and linuxstart keys are among the files checked (076114f). Step 4, the release, is `storage.c`'s, already in 14b |


## Round 10: the review of v4.3 (`REVIEW-v43.md`)

One reviewer, on tag `v4.3` (0f87c1c, tree 284a9331), its package, `BUILD-NOTES.md` 27 and
`..\t386mon\`: the merge lost nothing from either side, the port follows Ctrl+Alt+F11 and lives
with `VGA_CLEAR_AROUND`, v4.3 brings v4.2's flicker fix to T386MON's own workload, the emulator's
core costs only a few compares more on port I/O, the package consistent, and nothing that blocks
giving v4.3 to Plat. No blocker and no major finding, so there was none to reproduce or reject;
each point was checked before it was acted on, against the scratch tree's tags and the reviewer's
own runs in WSL `~/v43/review` [verified: `checks\findings-v4.3-review.txt`]. No firmware change
was needed, so no new tag: the app is still `v4.3`, and the package's images and patches were made
again from the tag byte for byte (`BUILD-NOTES.md` 28.5). `research\FLICKER-PLAT-NOTES.md` has
nothing new since 2026-10-23 18:25.

| # | Severity | Status | What was done, or why not |
|-|-|-|-|
| 1 | minor | fixed | Confirmed: the branch changed 18 files against v4.1 and v4.2 changed 12; five on both sides; two conflicted, so git merged 16 by itself, the 13 only the branch changed and `vga.c`, `vga.h` and `esp/main/speed.c` [verified: the file lists of `git diff` from v4.1 to each side, `checks\findings-v4.3-review.txt` 1]. `BUILD-NOTES.md` 27.1 now says 16 and which |
| 2 | minor | fixed | Confirmed: `..\t386mon\PROTOCOL.md` 1 said "It is not in any image of the package", and its `FIRMWARE` row gave "v4.1-perfport" [verified]. Its header now says what was brought up to date; section 1 opens "In tiny386-idf v4.3 and later; the protocol is unchanged" and says the branch's code is in v4.3; section 2 names tiny386 before v4.3 as having no port; the `FIRMWARE` row gives "v4.3"; section 10 opens with the merge into v4.3; section 12 says where v4.3's work is. `perfport.h` is untouched (still v4.3's byte for byte). `..\t386mon\README.md` 2.5 and `BUILD-NOTES.md` 27.15 say so (`BUILD-NOTES.md` 28.2) |
| 3 | note | documented | Confirmed in the reviewer's runs on the board model: on v4.3 (`fb-vga`) pictures 19,393 to 19,400 at 583.9 to 584.1 s differ from the guest's screen in 89,955 pixels of rows 40 to 439, none black; the branch's core (`fbp-vga`) the same pictures, the same `END` line (the host's wall time aside) and the same `T386MON.LOG`; v4.2 (`fb42-vga`) 4 such pictures [verified: `checks\findings-v4.3-review.txt` 3]. `TEST-PLAN.md` 15c now says a moment of wrong text as Windows shuts down from 16 colours is the known fault of 14f and `BUILD-NOTES.md` 26.3, not the port; 26.3 has a line for it (`BUILD-NOTES.md` 28.3) |
| 4 | note | documented | Confirmed: the harness's `snapshot()` (each `-snap` picture and each dialog) calls `vga_refresh(g_pc->vga, redraw, NULL, 1)` on the live VGA state, in the T386MON harness copies and the earlier ones; in the reviewer's run with the port read every second, `VBE_WHOLE` rises by one at 436, 441, 446, 451 and 456 s with nothing else drawing whole; the harness's own `redraw` does not pass through `perfport_redraw()`, so `PICTURES`, `CHANGED` and the rows sent are not counted; on the board only `pc_vga_step()` calls `vga_refresh()` [verified: `headless.c`, `perfport.c`, `pc.c` at v4.3, `checks\findings-v4.3-review.txt` 4]. `..\t386mon\PROTOCOL.md` 5 and 9 and `..\t386mon\README.md` 7 say so, and to leave the harness's pictures out when counting VBE pictures on the PC. The harness folders in WSL were not changed: they belong to the steps that made them |
| 5 | note | deferred | Confirmed: `perfport.c` 236 reads `vga_dirty_blocks` with a plain load; `vga.c` 51 stores it with `__atomic_store_n(..., __ATOMIC_RELAXED)` [verified]. Harmless on the board [inferred: an aligned 32-bit word read once per snapshot; on RV32 a relaxed atomic load of it is the same single `lw` the plain read compiles to], and the same kind as v4.2's own reads of the variable in `vga.c` (`BUILD-NOTES.md` 25.5). Changing it would mean a commit, a new tag and new images for no change in behaviour; as the review says, not needed for v4.3. Left for the next version that changes `perfport.c` anyway, Plat's call (`BUILD-NOTES.md` 28.4) |
| 6 | note | fixed | `REVIEW-v43.md` added to the top `SHA256SUMS` after v4.3's 173 files, as `REVIEW-v42.md` was after its review, and to `README.md`'s file table (with `REVIEW-v42.md`, which that row had missed) and its `SHA256SUMS` row (`BUILD-NOTES.md` 28.7) |
| 7 | note | documented; [unknown] on the board | The snapshot's heap walks inside the guest's `out` are still unmeasured; `TEST-PLAN.md` 15b's Photo 1 keeps asking for the "Its own cost" line (`SNAP_US`), as round 9 row 7 has it |


## Round 9: v4.3, the information port in the release (`..\t386mon\REVIEW.md` against v4.3)

The port's review read the branch `perfport` at 1.0 and T386MON 1.1: three major findings, nine
minor and four notes, none rejected; the firmware's part of the answer is the port's 1.1 (1ea0539,
the panel's frames a second and the second after a pause) and the version's name (6e9a6f1), and the
app's part is T386MON 1.2 (`..\t386mon\README.md` 11). v4.3 merges the branch as it was at 6e9a6f1:
`perfport.c`, `perfport.h` and the port's tests are its files byte for byte [verified: `git diff
6e9a6f1 v4.3`], so each firmware answer holds in v4.3 as it did on the branch, and the host tests
and harness runs that showed it were run again on v4.3 [verified: `BUILD-NOTES.md` 27]. Rows marked
"app" are T386MON's own and are listed only so that the round is whole; T386MON is unchanged (1.2).

| # | Severity | Where | Status in v4.3 | What, and the evidence on v4.3 |
|-|-|-|-|-|
| 1 | major | firmware and app | fixed | Port 1.1's `CHANGED` and `CHANGED_1S` (pictures in which a row went to the panel) are in v4.3; T386MON 1.2 shows fps from them. On v4.3's harness core `CHANGED` equals the harness's own count of pictures that sent a row in all 9 runs with the port (T386MON's 7, PPTEST's 2; and in the branch's 6 beside them), and `PICTURES` its count of pictures [verified: `checks\t386mon-v4.3.txt` 3] |
| 2 | major | app, docs | documented | T386MON's harness figures are instruction counts; `TEST-PLAN.md` 15c says so again for the board |
| 3 | major | app | fixed (1.2) | the pointer parked for the tests; v4.3's runs give 1.2's figures to the digit (`checks\t386mon-v4.3.txt` 2) |
| 4 | minor | app | fixed (1.2) | the log written once a minute and never inside a test |
| 5 | minor | app | fixed (1.2) | the fonts freed after a stock font is selected |
| 6 | minor | firmware | fixed | `perfport_picture()`'s new second after a pause is in v4.3; `test_perfport` (291 checks) passes at the tag, and under AddressSanitizer and UBSan [verified: `checks\host-tests-v4.3.log`, `checks\asan-v4.3.txt`] |
| 7 | minor | firmware, docs | documented; [unknown] on the board | the snapshot still asks the heap for its largest free blocks inside ESP-IDF's critical section; `SNAP_US` (T386MON's "Its own cost" line) shows what it costs, and `TEST-PLAN.md` 15b asks Plat to photograph it |
| 8 | minor | app | fixed (1.2) | frames a second by the emulator's clock |
| 9 | minor | firmware | fixed | the release calls itself `v4.3` (`esp/version.txt`; `esptool image-info` of all three apps and the string in each `tiny386.bin` [verified: `checks\reproducible-v4.3.txt`, `checks\image-info-*-v4.3.txt`]) |
| 10 to 13 | minor, note | app, docs | fixed or documented (1.2) | as `..\t386mon\README.md` 11 has them |
| 14 | note | app | no change | T386MON's one blit a second converts about 440 VBE rows a second on core 0 |
| 15 | note | app | documented | the probe on a PC that is not tiny386 |
| 16 | note | docs | documented | what "busy" and "M instr/s" rest on |

New with the merge, and so not the review's:

| # | What | Status | What was done |
|-|-|-|-|
| N1 | Ctrl+Alt+F11 (v4.2) changes `vbe_blocks` while the PC runs; the port's `VBE_BLOCKS_ON` must follow it | no change needed; a test added | The port reads `vga_dirty_blocks` itself at each snapshot, the variable `vga_set_dirty_blocks()` stores [verified: `perfport.c`, `vga.c`]. A new host test, `test_port_blocks`, puts `perfport.c` with `vga.c` as each board builds it and changes the setting as `speed.c` does: the snapshot gives the setting of that moment with or without a picture between, and the two whole pictures v4.2 draws after each change are counted; seven faults planted in copies, each caught. In Windows on the harness T386MON's "VBE drawing" line follows three changes [verified: `BUILD-NOTES.md` 27.3 and 27.8] |
| N2 | the merge's two conflicts | resolved, both sides kept | `esp/version.txt` (left at `v4.2` in the merge, `v4.3` in its own commit) and `tests/host/run.sh`'s list of renamed functions (both `vga_set_dirty_blocks` and `vga_get_info`); git merged `pc.c`, `vga.c`, `vga.h` and `speed.c` by itself, and each merged file adds to v4.2 exactly the branch's lines [verified: `checks\merge-v4.3.txt`] |
| N3 | BVBE480's `END` lines on the T386MON harness differ from v4.2's | no change needed | That harness copy orders a key file's keys differently (the port step's change); v4.2 built in the same copy gives v4.3's `END` lines in all 21 runs, and v4.3 built in the v4.1 work's harness gives v4.2's [verified: `BUILD-NOTES.md` 27.7, `checks\vbe480\results-v4.3.txt`] |


## Round 8: the review of v4.2 (`REVIEW-v42.md`)

One reviewer, on tag `v4.2` (6502063, tree f7beec4b), its package and `BUILD-NOTES.md` 24 and 25:
the cause proven on the PC, the fix at the root, no black or stale region found, nothing slipped
against v4.1, the package consistent, and nothing that blocks giving v4.2 to Plat. No blocker and no
major finding, so there was none to reproduce or reject; each point was checked before it was acted
on [verified unless marked]. Finding 1 was checked by reading the harness's hook and the built
`vga.c` (the clear's event sits in the branch v4.2's build leaves out) and then mended in the
harness rather than only in the words: the harness now counts the black any clear stores, a fault
planted to make black inside the picture is caught, and every v4.2 run gives 0 (`BUILD-NOTES.md`
26.1). Finding 4 was checked by running the install it said had not been run (26.2). No firmware
change was needed, so no new tag: the app is still `v4.2`, and the package's images and patches
were made again from the tag byte for byte (26.4). `research\FLICKER-PLAT-NOTES.md` has nothing new
since the third report.

One thing newly seen on the way, not one of the review's: with the board's display model through
Windows' restart from 16 colours, the text screen there is drawn light grey where it should be black
for 51 virtual seconds; it is older than v4.2 (v4.1 the same, and it comes with v4.0's VGA BIOS), not
the flicker, and is left for Plat to decide on (`BUILD-NOTES.md` 26.3; `TEST-PLAN.md` 14f asks him
whether he has seen it).

| # | Severity | Status | What was done, or why not |
|-|-|-|-|
| 1 | minor | fixed (the evidence and the docs) | Confirmed: `instr_vga.py` puts the clear's event after `simplefb_clear()`, which `VGA_CLEAR_AROUND` compiles out, so every v4.2 run said "clears in 0 steps" and "no clear over a picture in v4.2" measured nothing [verified: the built `vga.c`, the runs' logs]. The harness (copy) now hooks `vga_black()` and the clear's taking in `vga_clear_around()`, notes every pixel a clear makes black (v4.1's `memset()` too) and checks each once the picture is done (`checks\flicker\harness-v4.2-review.diff`). Two planted faults (every row black; the black 8 pixels inside the picture) are caught in MODE13X and BMODES while every picture still equals the reference; v4.2 gives 0 pixels black between two pictures in all 37 runs of 25.9's lists, the DEBUG run, the DOS box session and the install, where v4.1's `memset()` gives 5,417 to 571,222 a run [verified: `BUILD-NOTES.md` 26.1, `checks\flicker-harness-v4.2-review.txt`]. Reworded with that in `BUILD-NOTES.md` 25.0, 25.4, 25.9, 25.10 and 25.16, `TEST-PLAN.md` 14f, and round 7 (a) here |
| 2 | minor | fixed | `qemumini-pointer\README.txt`: "against 269 of 506 with the driver as released, the pictures with the pointer outside the part of the screen measured left out" [verified: 25.9's table, and this round's rerun gives 269/132/105 again] |
| 3 | minor | fixed | `BUILD-NOTES.md` 25.0: v4.2, v4.1 and `vbe_blocks = 0` the same pictures, "as v3.9 did in investigation 1's run with the driver"; v4.2's own runs had v3.9 with the standard VGA driver only [verified: `checks\flicker\list3.txt`] |
| 4 | minor | fixed, and now verified | Rather than marking route C [inferred], it was run: the package's script made the changed floppy from a copy of the download (one byte, 01 to 00 at image offset 64,104; the 16 other files the same), Windows' own install through the dialogs from it left `QEMUMINI.DRV` e4c47e9a... in `C:\WINDOWS\SYSTEM`, and Plat's session on that image had the pointer whole in 532 of 532 sweep pictures and every picture equal to the reference; the same install from the released floppy gives `base-n16.img` byte for byte [verified: `BUILD-NOTES.md` 26.2, `checks\route-c-v4.2.txt`]. `qemumini-pointer\README.txt` route C, `BUILD-NOTES.md` 25.2 C and `TEST-PLAN.md` 14c say so |
| 5 | note | documented | `TEST-PLAN.md` 14a: flashing v4.2 alone changes nothing in the pointer (its pictures are v4.1's), the cure is 14c [verified: all of this round's v4.2 and v4.1 pairs give the same pictures and `END` lines] |
| 6 | note | documented | `TEST-PLAN.md` 14d already asked Plat to note whether the DIB engine's dearer pointer matters; it now adds that the only real cure is an emulated hardware pointer (VMware SVGA II), a large piece of work (`BUILD-NOTES.md` 25.6) |
| 7 | note | done | Run the way Plat reached 256 colours (live, no restart): a new session on the image route C left sets 256 colours from Display Properties ("Apply the settings without restarting", keep), sweeps the pointer, then runs the review's DOS box session at 8 bits (full screen, 40 columns, back, four quick switches). Every one of 19,030 pictures equal to the reference; v4.2 no pixel black between two pictures in 24 clears, v4.1 (the same `END` line) 6,320; the pointer whole in every sweep picture (the table's 94 "part" are the plain arrow beside Windows' busy arrow just after the change; panel and reference agree on each) [verified: `BUILD-NOTES.md` 26.5]. On the image as released the same keys changed the depth, but Windows, slower, lost the DOS box's keys: not counted |
| 8 | note | documented | `TEST-PLAN.md` 14d item 5: the desktop repainted over seconds after a full-screen DOS box, and up to a second of black at each switch, are Windows' own [verified there: `REVIEW-v42.md` 5] |
| 9 | note | deferred | The hotkey is called once per press, on the key going down and never from the key repeat (`kbd_input.c` returns before the repeat is set up), and `vga_task` takes the request once a picture, every 30 ms, so two presses would have to fall within 30 ms [verified: code; inferred: not a human rate]; the console is held only after a fatal message or with the PC switched off, for good [verified: `console.c`, `speed.c`]. A count would change nothing Plat can do and would need a new version (`BUILD-NOTES.md` 26.5) |
| 10 | note | for Plat | `BUILD-NOTES.md` 25.6 and P2 below now say the reason given was more cautious than needed (a small file written and synced on a key press is ordinary FatFs use); whether to add the save is Plat's call (26.6) |


## Round 7: the 800x480 flicker (Plat's report on v4.1, and the two investigations)

Not a review of a tag but Plat's report from the board ("800x480 at 16-bit is flickering a LOT. I
don't recall it flickering previously"; his later notes in `research\FLICKER-PLAT-NOTES.md`), the
brief's five suspects for it, and two investigations on the PC: `research\FLICKER-REPRO.md` (here R,
the reproduction) and `research\FLICKER-AUDIT.md` (here A, the audit of the drawing and of the
driver). Every point was checked again before it was acted on [verified unless marked]: the
driver's source at its release tag and the released binary's bytes; the reproduction's harness rerun
on v4.1, v4.2, v3.9 and with the driver's byte changed (`BUILD-NOTES.md` 25.1 and 25.9). Its fixes
are tag `v4.2` (6502063, five commits on `v4.1`, branch `v42`), the app calls itself `v4.2`
(`BUILD-NOTES.md` 25), and the driver change is `qemumini-pointer\` beside this file. v4.1's own
files are in `previous-v4.1\`.

### The brief's suspects

| # | Suspect | Status | What was found, and done |
|-|-|-|-|
| a | Something asks for a clear of the panel (DISPI X/Y offset, size, depth or virtual size writes; the CRT controller's deferred clear; a palette or DAC write; a mode or chain 4 change), and with block drawing the panel stays black where the guest does not write again | not the flicker; fixed where it showed | At the desktop, still or moving, no clear, no clear request, no DISPI, CRT controller or attribute index write happens with this driver; clears come only with mode sets (the BIOS's, and the driver's two as Windows starts) [verified: R 3.1, A 3(a), and v4.2's runs]. Every clear is followed by a whole picture in the same `vga_refresh()`, so the screen is never left black [verified: code]. But the clear was a `memset()` of the panel's frame buffer, which reaches the panel before the picture (the buffer is twelve times the cache), so each mode set flashed black: v4.2 makes the clear with the picture after it, drawn whole over the old one, and blackens only the rest; `test_clear` (new) fails v4.1's `vga.c` on exactly this [verified]. This line first gave the harness's panel model as evidence too ("no clear over a picture in any v4.2 run, where v4.1 had 3 to 11"), but that harness counted a clear only after v4.1's `memset()` and so could not see a v4.2 clear (round 8, finding 1). Since round 8 it counts every pixel a clear makes black: in no v4.2 run is one of them drawn in another colour by the picture, where v4.1's `memset()` left 5,417 to 571,222 a run black between two pictures; every picture equal to the reference in both [verified: `BUILD-NOTES.md` 25.4, 26.1] |
| b | The clear and the redraw shown as separate pictures | fixed (as a) | In v4.1 they were in one `vga_refresh()`, but the panel showed the `memset()`'s black between them (a). With v4.2 nothing is stored for a clear before the picture: a mode set again over an unchanged picture stores nothing at all, and every page of the screen that is stored into has a pixel changed [verified: `test_clear`, both boards, v4.1's `vga.c` fails it] |
| c | Block marks lost between the cores, or a store path without the fence | no change | Every store into video memory goes through a marked path with the release fence first; the drawing clears the mark, fences, then reads [verified: code, A 3(c)]. Under RVWMO the writer's store, fence and mark against the drawing's clear, fence and read cannot leave old pixels with the mark gone [inferred: the memory model; x86 cannot show it]. In the harness, with the drawing in its own thread, 1,568 and 1,550 checks with the guest paused found nothing lost after one more picture (v4.2) [verified]. A lost mark would leave pixels stale, not blink. v4.2's new `vga_set_dirty_blocks()` (Ctrl+Alt+F11) draws the two pictures after a change whole, so a store that read the old setting is drawn too [verified: `test_vbe`'s new part; without it, or with one whole picture, the test fails] |
| d | The panel written while it scans (tearing), or an incomplete write-back | no change; inherent, as in v3.9 | Every row a picture changes goes to `lcd_flush()`, which writes back whole rows [verified: code, A 3(d)]. One frame buffer with no vsync tears where the scan meets a change, at 640x480 in 16 colours on v3.9 as at 800x480 [inferred]; v4.2 adds no tearing (it removes the black of a clear; a mode set now goes from the old picture to the new, which can be cut where the scan is, for one panel frame). Pictures timed to the panel's vertical blank or a second frame buffer would be the way; neither was needed for this flicker, and neither can be tried on the PC (`BUILD-NOTES.md` 25.6) |
| e | The driver writing DISPI registers in a pattern tiny386 takes as mode changes | no change | `VBE_setmode()` runs only for a mode set; the X/Y offsets are written only by `FBHDA_swap()`, which DirectDraw and 3D reach, not the desktop [verified: VMDisp9x 1.2025.0.119's source, A 3(e)]; no DISPI write at the desktop [verified: R, A, v4.2's runs] |
| | The cause: the driver's own pointer | fixed (in the driver, `qemumini-pointer\`) | `init.c` 207 sets `mouse_vxd = TRUE`; then every DIB engine access with CURSOREXCLUDE calls the VxD's `FBHDA_access_rect()`, which ignores its rectangle, and `mouse_erase()`; `FBHDA_access_end()` calls `mouse_blit()`, a save and an AND then XOR blit a pixel at a time [verified: source at tag v1.2025.0.119, 739eb72]. The six bytes `C7 06 B8 06 01 00` at 0x2064 of the released `QEMUMINI.DRV` are that line [verified]. With 0x2068 made 00: the pointer whole in all 531 pictures of the sweep (as released 269 of the 506 counted), the share of pictures caught mid-update while typing, scrolling and moving 7, 4 and 4 per cent (57, 61 and 58 as released; 7, 7 and 1 with the standard VGA driver), the same at 8, 16, 24 and 32 bits, at 640x480 through the driver and on the DOS board's 800x600; and Windows keeps up with the keys [verified: `BUILD-NOTES.md` 25.9]. Not cured: each move of the pointer costs the guest as much as before or more (the DIB engine's generic code), so a pointer moved without a break keeps Windows busy either way [verified: `BUILD-NOTES.md` 25.13]. The change typed in MS-DOS mode with DEBUG, as `TEST-PLAN.md` 14 gives it, was run on the installed image in the harness, screen for screen [verified] |

### Investigation 1 (R), `research\FLICKER-REPRO.md`

| # | Recommendation | Status | What was done, or why not |
|-|-|-|-|
| R1 | Do not spend the fix on the block marks, fences, clears or DISPI handling | followed in part | Nothing changed in the marks or fences. The clears were changed, not for this flicker (the harness has none at the desktop) but for the black the panel shows at each mode set, which the brief's first requirement rules out (a, b) |
| R2 | Pace VBE pictures on the guest's quiet moments, bounded (`-quiet`) | deferred | With the driver's byte changed the pictures caught mid-update are as few as with the standard VGA driver (7, 4 and 4 per cent typing, scrolling, moving; 0 to 1 per cent in every sweep), so pacing would cost pictures and up to 30 ms of lag for nothing. With the driver as released it helps the moving pointer but not typing or scrolling (36 to 40 per cent still mid-update, R 4.1). Not built |
| R3 | Settled blocks | deferred | As R2 |
| R4 | A hardware pointer (an emulated VMware SVGA II) | deferred | The full cure for a driver that draws its pointer; a large piece of work (the adapter's registers, FIFO and cursor commands), and the DIB engine's pointer already gives the standard VGA driver's behaviour |
| R5 | Prove a fix with `repro.sh`'s summary | done | `BUILD-NOTES.md` 25.9: the session and the sweep on v4.2, v4.1 and v3.9, with and without the driver change, every depth; the harness copy has one fix (a guest's `@shot` picture now reaches the panel model, 25.10) |
| R6 | `SHA256SUMS` for `research\flicker-repro\` and the note | done | In v4.2's list (`BUILD-NOTES.md` 25.19) |

### Investigation 2 (A), `research\FLICKER-AUDIT.md`

| # | Recommendation | Status | What was done, or why not |
|-|-|-|-|
| A1 | The driver: byte 0x2068 of `QEMUMINI.DRV` from 01 to 00, delivered as a script that finds the file through the FAT and writes a changed copy | done | `qemumini-pointer\qemumini-pointer.bat` and `.ps1`: on the driver's floppy image a changed copy beside it; on a Windows hard disk image in place after every check (or a copy with `-Out`); `-Undo`, `-Check`. Tried on copies of the floppy, the installed image, FAT32, FAT12 with scattered clusters, FAT16 with no partition table, and five refusals; each changed image differs in that one byte only [verified: `BUILD-NOTES.md` 25.3]. And the no-card way in MS-DOS mode with DEBUG (A's "rebuild" is not needed) |
| A2 | Quiet-gated pictures in the emulator, if the board still shows mid-update artefacts | deferred | As R2: only if the board says so after A1 |
| A3 | Skip the clear's `memset()` before a VBE picture of the panel's size | done, for every picture | v4.2's clear is made by the picture after it, for every kind of picture (text, 16 colours, 256 colours, VBE 1:1, centred and shrunk), not only VBE at the panel's size (a, b) |
| A4 | Pictures timed to the panel's vertical blank | deferred | As d: tearing is v3.9's too, and the PC cannot try it; worth it only if Plat finds tearing a problem once the pointer is fixed |
| A5 | Do not rely on `vbe_blocks = 0`, snapshots or the emulated retrace | followed | None of them is used; `vbe_blocks = 0` gives the same pictures in every v4.2 run [verified] |
| A6 | Report upstream that `FBHDA_access_rect()` ignores its rectangle | for Plat | VMDisp9x's later source has the erase and redraw taken out of the access calls (A 3(e)); whether to write to its author is Plat's call (`BUILD-NOTES.md` 25.18) |

### Plat's request for the fix release

| # | Request | Status | What was done, or why not |
|-|-|-|-|
| P1 | `vbe_blocks` changeable without pulling the card, taking effect at once | done | Ctrl+Alt+F11 turns it the other way for as long as the PC runs, with a line on the console; the speed screen says what it is. `kbd_input.c` holds up to four Ctrl+Alt hotkeys; the change is made between two pictures and the two after it are drawn whole [verified: host tests; on the board, unknown] |
| P2 | Save the choice back to the ini on the card | deferred | The reason first given (the card holds the guest's disk image, open and being written while the PC runs, so rewriting an ini then is a risk to the card) is more cautious than needed, as round 8 says: a small file written and synced on an explicit key press is ordinary FatFs use. With the pointer cured in the driver the setting compares two ways of drawing and fixes nothing, so the save waits for Plat to ask (`BUILD-NOTES.md` 25.6, 26.6). The ini can be edited on the PC or from Linux on the dual boot (`/mnt/sd/tiny386/*.ini`) |


## Round 6: the two reviews of v4.0 (`REVIEW-v40-emulator-vbe-merge.md`, `REVIEW-v40-board-and-package.md`)

No blocker and no major finding in either review, so there was none to reproduce or reject; every
minor point and note was checked here before it was acted on [verified unless marked]: EV M1 and
BP 1 in the release image's disassembly (`vga_mem_write()`'s path for mode 13h, 26 instructions in
v3.9 and 34 with a `fence rw,w` in v4.0); EV M2 by a host test that puts the reviewer's case into
`test_vbe` (v4.0's `vga.c` fails it); EV M3 and BP 4 by compiling the merge step's own `ua.c` with
the board build's `toolchain/cflags` and with plain rv32gc; EV N1 by running v4.0's checker on the
reviewer's log; BP 3 and BP 6 by Windows 95 runs on the PC that try exactly those cases. No finding
was rejected. Upstream's master is still 4ce2e06 today (`git ls-remote`), so v4.1 has the same base
as v4.0. Every check v4.0 made was made again on v4.1 (`BUILD-NOTES.md` 23.13), the PC's Windows 95
runs and BVBE480 included, and the scripts' harness through cmd.exe with only `C:\Windows\System32`
and `C:\Windows` on PATH; all but the Linux route's BIOS tests, which run that route's own binary
with the BIOS files, neither of which v4.1 changed.

Two findings, EV M1 and BP 1, are the same one seen through each lens; so are EV M3 and BP 4. The
two reviews' notes on the fence's cost (EV N7, BP 1's "How much") stand: only the board can say.

### Emulator, VBE and merge (EV)

| # | Severity | Status | What was done, or why not |
|-|-|-|-|
| M1 | minor | fixed | A chain-4 store through the window is marked only while VBE is on and `vga_dirty_blocks` is 1 (`VRAM_WRITTEN()` in `vga.c`); a store into the linear frame buffer only while `vga_dirty_blocks` is 1 (`LFB_WRITTEN()` in `pc.c`). Mode 13h's byte store is now 31 instructions with no fence and no map store, where v4.0 had 34 with a `fence rw,w` and v3.9 26 (the five more: a load of the VBE enable register, at an offset past 2 KiB, and its test) [verified: disassembly of the v4.1 image, `checks\vbe-loops-v4.1.txt`]. Safe because turning VBE on always asks for a clear and so a whole picture (`vbe_update_vgaregs()`), and the store and its test run on the core that sets the register [inferred, as the review argued]; `test_vbe` now checks that no store through the window marks anything with VBE off or `vbe_blocks = 0`, and that what was stored unmarked is drawn once VBE is on again, also with no picture between (v4.0's `vga.c` fails the three mark checks) [verified]. The guest's work is unchanged: the 27 standard and 11 native Windows runs and BVBE480 give v4.0's `END` lines and pictures exactly (`BUILD-NOTES.md` 23.7, 23.8) |
| M2 | minor | fixed | A write to SR04 that switches chain 4 while VBE is on asks for the next VBE picture to be drawn whole (`vga_vbe_whole()`: a count the emulator's core raises with release, the drawing reads with acquire in `vga_refresh()`). `test_vbe`: chain 4 off, 4,000 bytes through the planar path, chain 4 on, at six modes (640x480x8, 800x480x8 and x16, 640x480x32, 800x600x16, 1024x768x8), with and without a picture between: every picture equals the old loop's; v4.0's `vga.c` fails the six cases with no picture between, as the reviewer's probe did [verified] |
| M3 | minor | fixed (comments) | Checked: the merge step's `ua.c` compiled with the board's flags (`-march=rv32imafcb_zicsr_zifencei_zcb_zcmp_zcmt_xesploop_xespv -mtune=esp32s31`, `.attribute unaligned_access, 1`) gives one `lhu`/`lw` for both forms' loads and one `sw` for both stores, `unaligned.h`'s behind an alignment test whose two arms are each one `sw`; with plain rv32gc (`unaligned_access, 0`) the packed store is four `sb` [verified: WSL `~/v4-work/v41/ua`]. ESP-IDF has no handler for a misaligned trap (the names in its panic code only) [verified: grep], and the board runs Windows 95, so the S31's cores do misaligned accesses themselves [inferred]. No code change: `unaligned.h` stays as v3.9 had it. The comments in `i386.c`, `unaligned.h` and `board_korvo1.h` now say this, and `research\UPSTREAM-MERGE-v4.md` 4.5 and 9 are corrected in place (BP 4 is the same finding) |
| N1 | note | fixed (tooling) | v4.0's harness wrote the emulator's output through to the log a byte at a time when `pc.c` flushes the BIOS's debug port, so its own lines could land inside a guest line. v4.1's harness copy (`checks\vbe480\headless-v4.1.diff`) passes the emulator's output on a whole line at a time; `checks\vbe480\vbe480check.py` also puts a split line back together, and on the reviewer's 1152K log now says 55 pictures, every one right, where v4.0's checker said 1 WRONG [verified]. v4.1's own 31 BVBE480 runs had no split line |
| N2 | note | fixed | `checks\vbe480\headless-v4.1.diff` is made from the harness the v4.1 runs used, against the merge step's: `-keyfile` and `run.sh`'s `FDA`/`FDB` are in it, so the install run can be repeated from the package. `headless-v4.0.diff` is kept as it was |
| N3 | note | documented, deferred | The comment in `vga_graphic_refresh()` now says that a line past the end of video memory is palette entry 0 at 8 bits, and shows any bytes a 16 or 32-bit store at the very end of the last bank put into the slack. The stores are not bounded: a guest must write past its own video memory for it to show, and the bound would put a test on every chain-4 16 and 32-bit store, which M1 has just made cheaper. Old since v3; cosmetic |
| N4 | note | no change | Provenance only: v4.1's PC runs use binaries built from the tags with one harness (`BUILD-NOTES.md` 23.8) |
| N5 | note | documented | `vbe_write()`'s comment on the X and Y offsets says that, unlike QEMU, they do not set chain 4, the graphics mode and the CRT controller's size again, and why that matters only to a guest no one knows of |
| N6 | note | no change | Done again: v4.1's install through the dialogs is a plain build (no `DEBUG_VBE`) and gives the reviewer's plain v4.0 run's audit to the picture (35,845 pictures, the one difference at picture 18,772, t = 565.047 s) [verified: `checks\win95-driver-install-v4.1.txt`] |
| N7 | note | documented | `BUILD-NOTES.md` 23.15 and `TEST-PLAN.md` 13d: since v4.1 mode 13h pays no fence at all, and `vbe_blocks = 0` takes the marks and fences away in VBE modes too, so the fence's cost on the board can be seen as the difference between 1 and 0 there |

### Board and package (BP)

| # | Severity | Status | What was done, or why not |
|-|-|-|-|
| 1 | minor | fixed | As EV M1. `vbe_blocks = 0` now stores nothing in the map: a 16-bit store into the linear frame buffer is then 12 instructions, v3.9's own, against 20 with the marks on (v4.0 had 18 either way; the 2 more in v4.1 are the test of `vga_dirty_blocks`) [verified: disassembly, `checks\vbe-loops-v4.1.txt`]. So `vbe_blocks` is an A/B switch for the fence on the board. `BUILD-NOTES.md` 23.2 and `TEST-PLAN.md` 13d say so; the PC cannot time a fence (x86 compiles it to nothing), as the review says |
| 2 | minor | fixed | `TEST-PLAN.md` section 0: `bios.bin` is the Linux route's; `vgabios.bin` is v4.0's (the 800x480 modes, `bios\README.txt`), and v3.9's still works; with `win95-800x480.ini` copied the boot menu lists four inis |
| 3 | minor | fixed | Run on the PC [verified: `checks\win95-pc-runs-v4.1.txt` part 5]: the image as the install through the dialogs leaves it (shut down from the Start menu), started with `win95-14m.ini`'s 14M and 320K: Windows says "There is a problem with your display settings", opens Display Properties at 640x480 in 256 colours and asks to restart; accepted, it comes back at 640x480, boxed. Started again with 768K (shut down first) it is back at 800x480, and the same image with 768K and never with 320K is at 800x480 too: so the cost is that start's size, nothing kept. `win95-14m.ini` and `win95-800x480.ini` now say in a comment that an image with the driver set to 800x480 starts with `win95-800x480.ini` only, and `TEST-PLAN.md` 13c says so, what is seen otherwise, and, should an image ever stay at 640x480, `regedit A:\VBE480.REG` again and a restart (run on the PC on the image after the 320K start: it stays at 800x480 [verified]; that it mends a stuck one is [inferred], none got stuck) |
| 4 | minor | fixed (comments) | As EV M3 |
| 5 | note | documented | `TEST-PLAN.md` 13a and `BUILD-NOTES.md` 23.9: at 768K the guest gets 14M if the boot console's `Emulator memory` is 15,120 KiB or more, else 13M; Photo 1 settles it. The table of every depth and both boards is in 23.9 |
| 6 | note | fixed | `card-dosboard\tiny386\win95-800x600.ini`, new: 800x600 for the DOS board with `vga_mem_size = 1M`, 960K, 1408K and 1920K named. Each of the four ran on the PC with the DOS board's build to the 800x600 desktop at its depth (16, 16, 24 and 32 bits), 1536K and 2M as well, every periodic picture equal to a full redraw where audited; and with 768K (VBE600.REG's 800x600 in 16 bits not fitting) the driver sets no mode and Windows comes up at 640x480 [verified: `checks\win95-pc-runs-v4.1.txt` part 6]. `TEST-PLAN.md` 13e and `vbe-reg.img`'s README.TXT agree (the floppy is unchanged; its sizes were right) |
| 7 | note | fixed | `bios\README.txt` says that the card's `bios.bin` is the Linux route's, built with Ubuntu's gcc 15.2 by that route's own kit, and that this kit with `TOOLCHAIN=host` makes its `vgabios.bin` exactly but a `bios.bin` of other bytes from the same source (the 800x480 patch touches `vgasrc\` only). `NO800=1 TOOLCHAIN=ubuntu` was not run here [unknown] |
| 8 | note | fixed | `README.md`: the update line reads as `FLASH.txt` puts it; "In short" has v4.0 and v4.1 after v3.9. `checks\vbe480\BVBE480.COM` (and the checker, the program's source and the two harness diffs) are in the top `SHA256SUMS`, and `TEST-PLAN.md` 13f gives its SHA-256 |


## Round 5: the two reviews of v3.8 (`REVIEW-v38-emulator-and-windows.md`, `REVIEW-v38-board-safety-and-features.md`)

Plat's question with this round, whether UTC and local times are mixed up: no. Both reviewers
checked it, and it was checked again here [verified: Windows on W. Australia Standard Time, +08:00;
WSL on Australia/Perth, AWST+0800; the v2 `tiny386.bin` written 2026-10-21 13:19:18 +08:00, which is
05:19:18 UTC, the same instant; the dual-boot bootloader 16:02:33 +08:00; the commit tagged `v3`
18:20:20 +0800; his photo 18:29, reported]. Nine minutes after the first v3 commit, with no v3 image
built yet, is far too short for a v3 app to have been built, flashed, booted into Windows, taken
through first-run setup and blue-screened while he was out, so the blue screen he came home to was
on the v2 app he flashed that afternoon [inferred], as he says.

No blocker in either review. The one major finding (BS F1, old: it dates from v3.2's backup) was
checked here before it was acted on: by reading `flash-dualboot.bat` lines 200 to 211 and
`flash-standalone.bat`'s same lines, and by the review's own harness cases 84 to 88, which this
round's harness runs again (cases 84 to 88 of `checks\bat-harness-v3.9.txt`, with v3.8's outcome in
`dualboot\DUALBOOT.md` 14.4) [verified]. No finding was rejected. Every check v3.8 made was made
again on v3.9 (`BUILD-NOTES.md` 21.10), the scripts' harness through cmd.exe with only
`C:\Windows\System32` and `C:\Windows` on PATH; and, since the shared core changed, the PC's Windows
95 runs (11) and the Linux route's build and tests (both variants) [verified: `BUILD-NOTES.md` 21.6,
21.7].

### Board safety and features (BS)

| # | Severity | Status | What was done, or why not |
|-|-|-|-|
| F1 | major (old) | fixed | Over Linux's table both scripts read the first 256 bytes at 0x2000 and name them by SHA-256: the start of the dual-boot bootloader, of tiny386's own, or an erased page, which is what a write by these scripts leaves when it is cut short (every bootloader this folder knows differs from the others by byte 28 [verified: `checks\heads-v3.9.txt`]). With such a page and a backup of this board that differs from the flash, the backup is kept and the run finishes (`flash-dualboot.bat`) or writes its set (`flash-standalone.bat`); either recognises the other's cut write. With such a page and no backup, no backup is taken and nothing is written, and the message names the two ways on (the old backup's name back, or `back-to-linux.bat COMport small` first). A backup that itself holds the cut state (only an older script can have taken one) is refused the same way. A page that is none of those, with a backup that differs, is Linux changed since the backup: the stop now names both renaming and `back-to-linux.bat COMport`. `:write_failed_boot` names `back-to-linux.bat` first, then the same command again, then the hand-written table for a cut inside the table. The review's cases 84 and 85 now finish with the backup kept; 87 takes nothing; 88 then finds no backup and writes `small be5b725` instead of a broken bootloader; cases 89 to 101 cover the erased page, both scripts after each other's cut, no backup, and an older script's backup of the cut state; no write reached the stub in any of the 103 cases [verified: `checks\bat-harness-v3.9.txt`]. `dualboot\DUALBOOT.md` 12.2 (1c, 1d) and 13.5 (C5) corrected, and its new section 14; `FLASH.txt` and `TEST-PLAN.md` risk 2 and 3 say it |
| L1 | minor | fixed | In `i386.c`, `cpu_abort_note` (the front end's note) now runs at most once, before the line that names the cause: TRY1 goes through `cpu_abort_at()`, which calls it, prints `@ file function line` and calls `cpu_abort()`; `deliver()` calls it before `shutdown:`. On the board the note turns the UART on, so both lines now reach it. A front end with no note (the Linux route, upstream, the PC harness) prints the same lines in the same order as before [verified: test 20 runs two shutdowns with a note and checks its order, and `run_cpu.sh` reads TRY1's way out; v3.8's `i386.c` fails exactly those two checks; the PC runs give v3.8's results to the instruction, `BUILD-NOTES.md` 21.6]. This changes the shared core, so `research\win95-core-v3.9.patch` replaces v3.8's for upstream (kept beside it) |
| L2 | minor | fixed | `console_draw_crash()` first says what each kind of repeated line still holds into the ring, `(not shown: N more lines like "..." before the crash)`, with no lock and no stdio, before it reads the log's last lines. Test 2 part 9: 40 lines of a kind in 2 s, then the crash screen: the count and the newest line are in the ring before the crash report's lines and on the screen, once; v3.8's `console.c` fails these [verified: `checks\host-tests-v3.9.log`, `checks\mutants-v3.9.txt`] |
| L3 | minor | fixed | `status_led.c`: the stall line and its end through `console_printf()` (`CON_WARN`, `CON_INFO`), so the ring and the panel have them, and the UART when it is on. Test 15 checks `led_task()`'s source for it [verified]. `STATUS-LED.md` 2 (yellow 5) and 8, `TEST-PLAN.md` 8c say where to find the line |
| F2 | minor | fixed | `app` over either dual-boot table now reads 0x2000 as `table` does and stops if it is not the dual-boot bootloader, naming `flash-dualboot.bat COMport all` (which keeps the table's default) or `back-to-linux.bat COMport` [verified: harness cases 71 and 91; case 92 and the old cases 8, 9 and 55 go on as before over the dual boot, with one more read] |
| D2 | minor | documented | `STATUS-LED.md` 8, "Reading it", rewritten: the light marks each access as it is asked for; steady blue is many accesses, which can be a retry loop; one flash then green for seconds can be one long wait for the card, which past 10 s is yellow 5; the `Disk:` line tells these apart |
| P1 | minor | fixed | `README.md`: every image folder has `flash_args` and `SHA256SUMS`, and each but the tiny386-default one `tiny386.elf` |
| L4 | note | fixed | A kind's count line is said at the level of the most serious line it held (held `ESP_LOGE` lines give a red count, and count as a problem); held lines still go through the limit, so a card error storm cannot flood the emulator. Test 2 part 9 checks it; v3.8 fails [verified] |
| L5 | note | fixed | With L1: at an emulator abort the counts of held lines are now said before the line that names the cause [verified: test 20's order check; `abort_note()` says the counts first] |
| D1 | note | documented | `STATUS-LED.md` 8: a steady LED with a frozen panel means core 0 stopped scheduling the LED's task; the interrupt watchdog still catches a core stuck with interrupts off, a starved one it does not. No liveness cue was added back (Plat asked for a steady light) |
| F3 | note | fixed | Both scripts refuse an app image that is byte for byte `images\dosboard\tiny386.bin` (`TINY386_BIN`, `SA_TINY386_BIN`), before the board is read [verified: harness cases 93 to 95]. A DOS image from elsewhere is not caught; its own board check stops it on a Korvo-1 |
| P2 | note | fixed | The top `SHA256SUMS` now also lists `research\win95-core-v3.8.patch`, `research\win95-core-v3.9.patch` and `research\WIN95-FIX.md`, and the two reviews of v3.8 |
| P3 | note | fixed | `dualboot\DUALBOOT.md` 13.1 says that bullet is the boot swap's record and that the folder now holds the current app |
| L6 | note | fixed | `display.c`'s stack report also names `status_led` and `uart_log` (its line 288 bytes, was 240). Both tasks' use on the board stays [unknown] until a UART log with `uart_log = 1` |

### Emulator and Windows 95 (EW)

| # | Severity | Status | What was done, or why not |
|-|-|-|-|
| F1 | minor | documented | `TEST-PLAN.md` 8e is now "After a desktop in 8b: the old timer, once (needed)": a fresh copy on the same partition with `timer_catchup = 0`, the `Disk:` and `Timer:` lines photographed beside 8b's, and what each outcome means. `TEST-PLAN.md` 12 asks for both photos. The board test itself is Plat's [unknown until then] |
| F2 | minor | documented | `research\WIN95-FIX.md` 0 and 2 now say what matches: the four return addresses, the frame tail, SP on a page boundary with CR2 = SP less 4 (16 of the 32 stack bytes), not "byte for byte"; its new section 9 gives both stack lines. `BUILD-NOTES.md` 19.7's "the photo's stack bytes" is corrected the same way |
| F3 | minor | documented | `research\WIN95-FIX.md` 4.1 and `BUILD-NOTES.md` 19.7 corrected: first-run setup's window came within the 120 s in 30 of 51 runs; the 21 hold, `bufd` and `spike5` runs ended at the desktop before it; the review's two longer runs cited |
| F4 | minor | documented | The VFBACKUP run is in `research\WIN95-FIX.md` 9 and `TEST-PLAN.md` 8 (treat that message as a failure, not as a pulled card); Plat is told with this round |
| N1 | note | for Plat | `research\win95-core-v3.9.patch` (v3.8's with L1's change and two comments) applies to upstream 9ed786b and to the Linux route's tree; there both variants pass the route's 26 tests with the release's same 5 warnings [verified: `BUILD-NOTES.md` 21.7]. Whether the Linux route's release and upstream take it is Plat's call (`BUILD-NOTES.md` 21.13) |
| N2 | note | no change | Done here as well: v3.9's 11 PC runs give the review's v3.8 desktop times to the second and the same instruction counts at the end [verified: `BUILD-NOTES.md` 21.6] |
| N3 | note | fixed | `i8254.c`: the comment on `i8254_max_owed` says that N above 1 makes up to N owed ticks and drops more than N to one (test 19's 40 owed with N = 4) |
| N4 | note | no change | `TEST-PLAN.md` 8a keeps the fresh format first |
| N5 | note | documented | `README.md` "In short": after a hold the guest's clock is behind by the hold's length; BENCH is not affected. A guest clock that stops during holds is left for later |
| N6 | note | fixed | `lldt_check()`'s comment: a not-present LDT descriptor is #NP (from `set_seg()`), the rest #GP |
| N7 | note | deferred | The task gate's error code is pushed as 32 bits for a 16-bit TSS too. Only a 286-style guest could notice; nothing here runs one |
| N8 | note | deferred | The static line budgets in `deliver()` and `read_desc()` are not reset when the guest reboots: cosmetic, and resetting them needs a reset hook in the core that upstream does not have |


## Round 4: the review of v3.5 (`REVIEW-v35.md`)

The review raised no blocker and no major finding, so there was none to reproduce or reject. Each
minor point and note was checked against the source, the netlist or the data sheet before it was
acted on: F1's figures in TI's two data sheets and in `dosboard.net`, F2's three sentences against
`dosboard.c`, F3's strap in the netlist and the S31 data sheet, N1 in `git ls-files -s`, N2 by
building the review's case (v3.5's `audio_mix.c` stops in `memset()`), N3 by measuring again
[verified: `BUILD-NOTES.md` 17]. Every check v3.5 made was made again on v3.6 (`BUILD-NOTES.md` 17.8),
the bat harness included, through cmd.exe with only `C:\Windows\System32` and `C:\Windows` on PATH.
The Linux route's checks were not repeated: v3.6 changes nothing they build [verified: `git diff
v3.5 v3.6` touches only `esp/` and `tests/`].

| # | Severity | Status | What was done, or why not |
|-|-|-|-|
| F1 | minor (board) | deferred | A change to the board's netlist (R703 to GND), which the board step's generators own (`..\..\tiny386-hdmi-board\dosboard\gen`); nothing was changed there. The review's case was checked and stands [verified: `dosboard.net`, R703 between /Audio_option/AUD_3V3 and AUD_XSMT, which reaches U701.17 XSMT, U702.7 SDL and U702.8 SDR; TI SLOS438F p. 4 VIL 0.35 V for SDL and SDR, p. 5 \|IIL\| at most 5 uA; TI SLAS859C p. 7 IIL 10 uA at most in size, VIL 0.3 x DVDD]: a 10k pull-down holds the line at no more than about 0.2 V with GPIO48 undriven [inferred: the sum], and leaving R703 off is not enough by the data sheets [inferred]. `LINE-OUT.md` now recommends 10k to GND, gives the reasons and the price, and rewords line 100 as the review asked; `BUILD-NOTES.md` 17.6 and 17.10 put it to Plat for the board step. The firmware drives GPIO48 both ways and needs no change for either fitting [verified: `dosboard.c`, `i2s.c`, host test 18's checks on GPIO48] |
| F2 | minor (docs) | fixed | `README.md` (In short), `FLASH.txt` 6 and `board_dosboard.h` lines 17 to 25 now say what v3.6 does: no pin but the I2C bus and what both boards share (the console's UART0, the status LED on GPIO37) until the MS7210 answers; if nothing answers, GPIO46 read with its pull-up for about 0.1 ms and left an input with no pull, and RSTN (GPIO49) pulsed only if it read low. `dosboard.c`'s top comment and `wrong_board()`'s say the same. `board_dosboard.h` also had "GPIO49 to GPIO57 are the Korvo-1's camera inputs"; it now says GPIO46 to GPIO57, all inputs but GPIO55, the camera's XCLK [verified: research `s31-module.md` 11]. `STATUS-LED.md`'s yellow 2 row and `TEST-PLAN.md`'s DOS board list say what the UART reports |
| F3 | minor (optional) | fixed | Taken as the review suggests [verified: `dosboard.c`; the built code, `checks\dosboard-code-v3.6.txt`]: when nothing answers at 0x59 or 0x2B, GPIO46 is read with its weak pull-up on (four reads after 100 us; low only if all are low) and left an input with no pull. High: the image stops with RSTN never driven, a Korvo-1 with nothing on its bus included. Low: the reset is tried even if a Korvo-1 address answered, and such a device is noted on the console as being on J202. The I2C probe stays the main test. New host test 18 runs `board_early_init()` itself, with the real `ms7210.c`, against pretend buses and pins in 17 cases (Korvo-1s and DOS boards) and checks every pin touched and its order; 17 of 17, and 12 of 17 fail against v3.5's `dosboard.c`; sixteen faults put into `dosboard.c` are all caught; clean under AddressSanitizer and UBSan [verified: `checks\host-tests-v3.6.log`, `checks\board-check-mutants-v3.6.txt`, `checks\asan-audio-mix-board-check-v3.6.txt`]. One case is given up for it: a Korvo-1 whose camera holds D0 low while its codec answers now has GPIO49 pulsed once (about 70 ms) where v3.5 stopped without it (test 18 case K4); it needs the DOS image on a Korvo-1 by mistake [inferred: unlikely], and `BUILD-NOTES.md` 17.10 puts the trade to Plat. Which way GPIO46 reads on a real Korvo-1 is [unknown] (`TEST-PLAN.md` 10c) |
| N1 | note | fixed | `audio_mix.c`, `audio_mix.h` and `audio_mix_test.c` are 100644 (`git diff` from v3.5 to v3.6 with its summary option: three mode changes); the new files are 100644 too; the tree `build.sh` makes from the patch has them 644 [verified: `checks\reproducible-v3.6.txt`]. `check_pins.py` stays 100755, a script |
| N2 | note | fixed | `audio_mix_devices()`: `if (n <= 0) return 0;` after the clamp and the rounding to even, before any device is asked; the header says so. Test 16 part 6: n of 1, 0, -1, -2, -128, -129 and `INT_MIN` return 0, leave `out` untouched and ask no device; against v3.5's file the test stops there (AddressSanitizer: `negative-size-param` at line 64; SIGSEGV without it) [verified: `checks\asan-audio-mix-board-check-v3.6.txt`]. `i2s.c` still passes `MIXER_BUF_LEN` |
| N3 | note | documented | Measured again with the DOS board build's own command lines and `-fstack-usage`: the same frames as the review's (`i2s_task` 352, `audio_mix_devices` 496, `YM3812UpdateOne` 112 bytes), about 960 bytes on the mix path plus `i2s_channel_write()`, in 4,096 [verified: `checks\stack-sound-v3.6.txt`]. `BUILD-NOTES.md` 17.4 replaces 16.6's estimate. No change; the UART's stack line shows the margin on the board [unknown until then] |
| N4 | note | documented | `TEST-PLAN.md` 10c, optional: the DOS image written only to the dual boot's test slot at 0xE20000 (917,296 of 1,966,080 bytes), started with VOL+ held, what to expect (from host test 18), what it would mean if GPIO46 reads low, and the Korvo-1 image put back with `flash-dualboot.bat COM10 app`; Linux is not touched. That the dual-boot bootloader starts the DOS app from that slot is [inferred]: the two builds' `sdkconfig.h` differ only in the table's name, the PSRAM memory test and the USB host options [verified]. It writes flash, so it is Plat's choice; table 12 has a row for it |

The review's other checks (the pins against the final netlist, nothing on J202 but the I2C pair, the
mute order, the mix, the default level, the Korvo-1 images, the patch and the builds, the host tests)
were run again on v3.6 or rest on files v3.6 did not change, and still hold [verified: host tests 16
and 17 in `checks\host-tests-v3.6.log`, `checks\reproducible-v3.6.txt`, host test 18's checks on
GPIO48 and the J202 pins].


## Round 3: the review of v3.3 (`REVIEW-v32.md`)

The major finding was checked here before it was acted on: the objdump of v3.3's own images shows
the nine or ten instructions before each rising store and the two before each falling one, as the
review says [verified: `checks\status-led-panic-path-v3.4.txt` part 1]. The fix has a build-time
check that fails on v3.3's images and a host test that now finds v3.3's layout short [verified]. No
finding was rejected. Every check v3.3 made was made again on v3.4 (`BUILD-NOTES.md` 15.7), the
bat harness included, through cmd.exe with only `C:\Windows\System32` and `C:\Windows` on PATH, so
that no esptool but the stub could be reached. The Linux route's checks and round 2's sanitiser
runs of other tests were not repeated: v3.4 changes nothing they build [verified: `git diff v3.3
v3.4` touches only `esp/` and `tests/host/test_status.c`].

| # | Severity | Status | What was done, or why not |
|-|-|-|-|
| F1 | major | fixed | `status_bb.h`: the bit's high time and period are picked while the line is low, before the wait that ends in the rise, and the reset is the first wait of the same loop; in all three v3.4 images and both crash-test builds each store to GPIO37's set or clear register comes straight after its wait's `bltu` [verified: objdump, `checks\status-led-panic-path-v3.4.txt`]. `build.sh` now checks that layout in every ELF it builds and stops otherwise; on v3.3's images the same check stops [verified]. `test_status.c` charges the instructions between each wait and its store as built (two and two) at one or two cycles each, at 80, 160 and 320 MHz, and runs v3.3's layout and times once, which it finds short (T1H 562.5 ns at 80 MHz) [verified: `checks\host-tests-v3.4.log`]. The review's second way (a lead of about 8 cycles) was not taken: it would rest on cycle costs nobody has measured [inferred]. `TEST-PLAN.md` 10b: a scope check on TP948 (or the Korvo-1's LED data pad) of a forced early crash, `crash_test = 5`, new in crash-test builds only: the CPU set to 80 MHz, the LED told `app_main()` has not run, then `abort()` (built for both boards [verified: `checks\crash-test-builds-v3.4.txt`]; not run [unknown]) |
| F2 | minor | fixed | T1H 700 ns (56 RMT ticks), as suggested. The 1 bit's period is 1325 ns (106 ticks) rather than the suggested 1.3 us: with 1.3 us, T1L is 600 ns and the bit-bang's one pass of jitter at 80 MHz takes it to 575 ns, under the WS2812B-2020's 580 [verified: `test_status` fails on that value, mutant `t1-1300ns`]. `test_status.c` checks every high time at the LED too (38 ns shorter [inferred: CHANGES-A2.md section 4]) against both datasheets, and the RMT frame's against the older WS2812B's windows [reported]; least margin 12 ns. `STATUS-LED.md` 5: the "leave room" sentence is gone; it now gives the times at the LED and says plainly that T0L comes to about the WS2812B-2020's 1 us maximum there, and why the 0 bit was left (`BUILD-NOTES.md` 15.3). The colours in the first boot's video: `TEST-PLAN.md` 3 and 12 already ask for it |
| F3 | minor | fixed | Checked: with `CONFIG_SPIRAM_BOOT_INIT` set and `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` not, `esp_psram_init()` failing does not abort [verified: `cpu_start.c` lines 673 to 680, both `sdkconfig.h`]. `STATUS-LED.md` 1 and 2, `TEST-PLAN.md` 11, `status_code.h` and the console's name for yellow 1 now say "PSRAM not found, or (DOS board) failing its memory test", and that other PSRAM trouble most likely ends as red 5 [inferred] |
| F4 | minor | fixed | `TEST-PLAN.md` 10 and `STATUS-LED.md` 4 tag the replay after RESET [unknown], say why (ESP-IDF calls the chip's power-on reset a power-on [verified: `reset_reason.c` line 22], most likely what the RESET button gives [inferred], so only the record tells), and step 10 and the table in 12 ask Plat to note whether the cyan lines and the LED's white flash came. `status_led.c` says the same in a comment |
| F5 | minor | fixed | The LED task's stack is 4096 bytes, as the project's other tasks. Its use is still not measured [unknown]; no high-water log was added to the release images |
| F6 | note | fixed | If the LED task cannot be created, `status_led_init()` sends black before giving up, so the LED is dark as the console line says |
| F7 | note | fixed | `crash.c`: `__wrap_esp_system_reset_modules_on_exit()` and its `__real` declaration are under `#ifdef STATUS_LED_GPIO`, matching the link's wrap option, which `main/CMakeLists.txt` gives for `TINY386_STATUS_LED_BOARDS` only [verified: both boards build and link, release and crash-test] |
| F8 | note | fixed | `README.md` says, for `previous-v3.2\` and the new `previous-v3.3\`, which lines of their `SHA256SUMS` check inside the folder and which from `v3\` or another folder |
| F9 | note | fixed | (1) `..\research\status-led-lp-core.md` section 1: the GPIO row says "through its own LP IO MUX and LP GPIO matrix" for the verified part and tags the HP GPIO registers [unknown]; section 5 likewise. (2) The Inolux timing table is page 11 of 19 [verified: page footer in the v2.2 PDF]: `status_code.h`, `test_status.c` and `STATUS-LED.md` corrected; `BUILD-NOTES.md` 14.2 left as written, 15.5 notes it. (3) No action: `esp/dependencies.lock` is the in-tree builds' copy of this folder's file |

The review's other checks (the dual boot unchanged byte for byte since v3.1, the reproducible build,
the refusal of every flashing target in the dual-boot build, the RMT use, the LP SRAM record, the
panic wiring, the PCM5102A mute) were run again or rest on files v3.4 did not change, and still hold
[verified: `BUILD-NOTES.md` 15.7].


## Round 2

Every minor finding was checked against the source before it was acted on, and each code fix has a
host test that fails on v3.1's file where that could be done (C2 1, B2 m1, E2 1, E2 2, E2 4)
[verified: `checks\host-tests-v3.2.log`]. No blocker or major finding was raised in this round.

### Concurrency and ESP-IDF (C2)

| # | Severity | Status | What was done, or why not |
|-|-|-|-|
| C2 1 | minor | fixed | `usb_input.c` `on_connected()`: after the open, after SET_PROTOCOL, after SET_IDLE, after the report descriptor and after the start it returns at once if the interface's `gone` flag is set, with a console line; the slot is left for `on_gone()`, which makes the second close (reworded for an interface not yet set up: "device on interface N unplugged"). `tests/host/usb/test_usb_input.c` takes the review's model cases: unplugged while SET_PROTOCOL, SET_IDLE or the first SET_REPORT is in flight, and just after the open, SET_PROTOCOL, SET_IDLE or the report descriptor: no request begun on the freed device, both closes made, no keyboard slot held. The same test against v3.1's `usb_input.c` fails 5 cases (G, J, K, L, M) [verified: `checks\host-tests-v3.2.log`, run.sh 5b]. The request already in flight when the unplug lands still finishes on freed memory inside usb_host_hid 1.2.0: **deferred** as round 1 left it, a limit of the component; patching `hid_host_uninstall_device()` or reporting it to Espressif is Plat's call (no message was sent to anyone) |
| C2 2 | note | fixed | `i8042.c` lines 651, 653 and 705 (v3.1 numbering): the unlocked reads of the PS/2 queue's count are relaxed atomic loads. They sit under `THREAD_SAFE` and `PS2_KBD_HOOKS`, so the Linux route compiles to the same binary [verified: `checks\linux-route-rebuild-v3.2.txt`]. E F3 (the ACK between E0 and its second byte) stays deferred, as E2 3 |
| C2 3 | note | fixed | `console_release_display()` returns at once once the console is held, and looks again after it has given the panel up: a hold that came meanwhile (`console_hold()` sets the flag before it takes the panel, both sequentially consistent) takes the panel back and draws it whole. No lock is nested in a new order [verified: code; the timing window itself is [inferred]] |
| C2 4 | note | documented | `crash.c`'s comment now says that the task watchdog does not reach the crash screen: without `CONFIG_ESP_TASK_WDT_PANIC` a timeout prints on the UART and goes on. Setting it is left to Plat (`BUILD-NOTES.md` 8): it would turn a core 0 starved for 5 s into a halt with a crash screen instead of a frozen panel, and also halt a stall that would have passed |
| C2 5 | note | documented | `TEST-PLAN.md` 7b and the DOS board section: look for `LCD underrun` on the UART; if it is there, the crash screen and the next boot's log ring may hold little else, and `lcd_bounce_px` (or a lighter load) comes first [unknown: whether underruns happen at all] |
| C2 6 | note | fixed | `storage.c`: the card's power-up wait is `pdMS_TO_TICKS(10) + 1`, at least 10 ms at any tick rate, as `dosboard.c` does its own waits |

### Boot safety and flash (B2)

| # | Severity | Status | What was done, or why not |
|-|-|-|-|
| B2 m1 | minor | fixed | `ide.c` on `BUILD_ESP32`: `bf_read_async()` (all modes) and the read-write branch of `bf_write_async()` refuse any sector past the image's end, an error for the guest, not counted as the card's. `misc.c` with `EMULINK_CHECKED` (which `BUILD_ESP32` turns on): the floppy drive number must be 0 or 1 before `fdd[]` is looked at, and the guest's cylinder, head, sector and count must lie inside the image. New host test `tests/host/disk/run_disk.sh`: `ide.c` built as on the board, its FILE calls going through a stand-in for `vfs_fat.c` onto ESP-IDF's own FatFs, on FAT32 (the review's volume) and on FAT16 laid out as Plat's p1: reads and writes past the end refused, the image's size and the free clusters unchanged, `fsck.fat -n` clean, another file intact; against v3.1's `ide.c` the image grows (to 8,389,120 bytes on the first stray write) and the test fails, as it should. Under AddressSanitizer and UBSan: clean, three runs. `tests/host/test_floppy.c`: drive 2 and 7 refused, cylinders and sectors past the end refused, the image stays 1,474,560 bytes [verified: `checks\host-tests-v3.2.log`, `checks\asan-disk-v3.2.txt`]. Round 1's E F4 line ("a read past the image's end gives zeros") is corrected below; `TEST-PLAN.md` and `FLASH.txt` say what is true now |
| B2 m2 | minor | fixed | `flash-dualboot.bat` (and `flash-standalone.bat`), over Linux's table: names the bootloader at 0x2000 by SHA-256 (be5b725's 547c6a5b, the release's 41d5a687, or neither) and, before the first write, reads the whole flash (16 MB) and 0x2000 to 0x8FFF into `backup\`, each with its SHA-256 and the board's MAC beside it; it stops before writing if that fails, and never overwrites a backup of another board or another state. `back-to-linux.bat COMx` now puts 0x2000 to 0x8FFF back from that backup when there is one (`backup`), and `whole` writes the whole 16 MB back; without a backup it is `small be5b725` as before, now worded "exactly, if the flash held be5b725's flash.bin". The scripts' flow, the backup included, was run with a stub esptool: 39 cases [verified: `checks\bat-harness-v3.2.txt`]. That the be5b725 bootloader starts the release's loader stays untried [inferred, the review's reading] |
| B2 m3 | minor | fixed | every board read and the first write pass `-a no-reset`, so the chip stays in download mode from the first reset to the last write's hard reset and Linux never runs in between; after N, or a stop after the board was read, the scripts say to press RESET. `TEST-PLAN.md` step 1 and `FLASH.txt` 1 and 4: "in Linux, poweroff and wait" first, and the risk is stated at the top of `TEST-PLAN.md` [verified: the stub's call log in `checks\bat-harness-v3.2.txt`] |
| B2 n4 | note | fixed | `esp/CMakeLists.txt`: `dfu-flash`, `otadata-flash`, `erase-otadata` (and `partition_table-flash`, `erase_otadata` where they exist) also depend on the refusing target [verified: `ninja -t inputs` in the from-nothing builds, `checks\cmake-flash-refusal-v3.2.txt`; no flash target was run]. The build folder's own `flash_args` and `flasher_args.json` still list the build's writes, for tools that read them directly: said in `FLASH.txt` |
| B2 n5 | note | fixed | `tests/host/storage`: a "plat16" card, p1 FAT16 with 4 sectors a cluster in a type 0x0C partition as be5b725's card image has it: mounted, written, both FAT volumes clean, the MBR and p2 unchanged [verified: `checks\host-tests-v3.2.log`]. The docs say "FAT16 or FAT32" for p1 |
| B2 n6 | note | fixed | `esp_main.c` `check_config()`: an image named for two drives (two disks, or a disk and a floppy; compared without case, as FAT names are) is refused with a red line naming both keys [verified: build; not host-tested: `esp_main.c` does not build on the host] |
| B2 n7 | note | fixed | `flash-paths.cmd` `KORVO1_MAC`: empty by default; once Plat puts the MAC from step 1 there, all three Korvo-1 scripts stop on any other board. A backup also records its MAC, and is used only on that board [verified: harness cases 16, 17, 18, 32] |
| B2 n8 | note | documented | `FLASH.txt` 1: the image set's `flash_args` is a reference list in address order; the scripts, or the two commands given there, write tiny386 first. Harmless either way, as the review found |
| B2 n9 | note | fixed | `SHA256SUMS` made again for every file it lists, the four review reports included; `sha256sum -c` verifies every line [verified] |

### Emulator correctness (E2)

| # | Severity | Status | What was done, or why not |
|-|-|-|-|
| E2 1 | minor | fixed | `i8042.c`: the overflow word also carries the left, right and middle buttons that went down and up in the reports merged into it, and `kbd_step()` sends those edges, each alone and in order (releases of held buttons, then presses of free ones), before the merged state. One word, so an edge is never taken apart from the movement it came with; to make room it holds -255..255 counts each way (was -1,023) and the wheel -1..1 (was -7..7), only in the overflow. Host tests: the review's case (a click in reports 40 to 49 of 100 with the emulator held up) gives one press and the release; a held button let go and pressed again plus a right click, all in the overflow, come out in order; a two-thread run with the emulator held up 120 ms at a time gets 10 of 10 clicks in every run (v3.1's `i8042.c`: 7 or 8), clean under ThreadSanitizer in three runs [verified: `checks\host-tests-v3.2.log`, `checks\tsan-ps2-mouse-v3.2.txt`]. Left: two clicks of one button inside one overflow come out as one, and movement past 255 counts in one overflow is dropped [verified: design; inferred: how often] |
| E2 2 | minor | fixed | `speed.c`: only a run that begins with 0xA1 counts; pieces are recorded only inside it, 0xAF ends it only after at least one piece, and a new 0xA1 always starts a new table. `test_speed.c`: a lone 0xAF and 0xA5 then 0xAF give nothing, a run cut short and begun again gives the second run's table; BENCH's own lines are unchanged, character for character against the Linux route's [verified: `checks\host-tests-v3.2.log`] |
| E2 3 | minor | deferred | E F3, the keyboard half: as round 1 decided. The cure is to hand keyboard events to the emulator's thread as the mouse now is, which moves the typematic timing and wants its own round of tests; the race needs an ACK to land within 1 ms of an E0 prefix [inferred: rare] |
| E2 4 | note | fixed | `mouse_forget()` (AUX_RESET, AUX_SET_DEFAULT, on the emulator's thread, the ring's only reader) also drops the reports still in the ring and the overflow word, keeping their newest button state so that a release among them cannot leave a button held. The review's case F now gives nothing after the reset, and a later move comes through [verified: `test_ps2_mouse.c`; the same test fails on v3.1's `i8042.c`] |
| E2 5 | note | deferred | scancodes while scanning is off (F5): tiny386's own behaviour, and the Linux route's. Dropping them changes what every guest sees from the keyboard and risks a dead keyboard if some guest leaves scanning off and relies on the old leniency [inferred]; to go with E2 3's move of the keyboard to the emulator's thread and its tests |
| E2 6 | note | fixed | `misc.c` with `EMULINK_CHECKED`: a floppy write the C library takes but the card refuses at the flush, a read error, and a failed `fflush()` or `fsync()` in `emulink_flush()` are counted (`emulink_error_count()`) and are an error for the guest; `disk_errors()` in `esp_main.c` adds them to the console's red card-error line. `test_floppy.c` writes to `/dev/full`: an error for the guest, and counted [verified] |
| E2 7 | note | fixed | `ide.c` on `BUILD_ESP32`: ATA FLUSH CACHE (E7h) and FLUSH CACHE EXT (EAh) call `ide_flush()` (fflush and fsync of every image) and complete with READY and an interrupt, or with an error if the card refused. Host test: both commands through the IDE registers on an image in ESP-IDF's FatFs: ready, no error, what FatFs held back written to the card [verified: `tests/host/disk`]. Windows 95 does not send it [inferred, the review's] |
| E2 8 | note | deferred | packets of at most 127 counts each way and no scaling: with E F7 (mouse scaling), a choice about feel to make with a mouse on the board. The clamp is QEMU's too, and older drivers may not take 9-bit values [inferred] |
| E2 9 | note | deferred | a mouse command's answer queued behind packets the guest has not read: tiny386's own behaviour; with packets paced at the sample rate the queue grows only while the guest is not reading. Changing the queue's order touches every driver's handshake and wants a mouse on the board to test with |
| E2 10 | note | fixed | `tests/host/test_vga_dosboard.c`: the review's `rev_vga_dos.c`, vga.c built with `board_dosboard.h`, now in `run.sh` (3b): 8 of 8 placements [verified] |
| E2 11 | note | fixed | the `PC built` line also gives PSRAM free and its largest block, from which the 16-colour cache (150 KB at 640x480, 234 KB at 800x600) is taken; if that cache does not fit, `vga.c` says so once on the UART (`esp_rom_printf`, so also the log ring) [verified: build; the figures on the board are [unknown]] |
| E2 12 | note | fixed | as B2 n9 |

### DOS board drivers (D2)

| # | Severity | Status | What was done, or why not |
|-|-|-|-|
| D2 S1 | minor | fixed | `i2s.c`: with `i2s_slot_bits = 32`, `slot_cfg.ws_width = 32` as well, so word select is high for 32 of 64 bit clocks, as ESP-IDF's I2S guide gives for Philips format [verified: build; the frame on a scope at TP938 is [unknown] until first light] |
| D2 S2 | minor | fixed | `ms7210.c` row B15 writes 0x10: pull-ups on the video input pads, which help the GPIO36 (VDD_SPI) strap instead of loading it. It departs from `encoder.md` 8.4 on purpose, which the row's comment, `check_ms7210_table.py` (a DEPARTURES entry, printed on the row), `ms7210_test.c` (bits 5:4 = 1, the others left alone) and `BUILD-NOTES.md` say. The other 113 rows still match 8.4 exactly, and the check prints B15 as the departure [verified: `checks\host-tests-v3.2.log`]. Whether the chip's own reset default is a pull-down, which firmware cannot help on a cold boot, is [unknown]; R206 2.2k stays the hardware margin |
| D2 S3 | minor | fixed | `ms7210_init()` reads 0x120C straight after D1 writes it; only if it reads back is the hot plug poll's watch for a reset chip on, and a read-back that fails turns it off for good with one log line. The poll also stops after `MS7210_RESET_TRIES` (3) set-ups in a row. Host tests: 0x120C reading 0x00: six polls, no set-up; lost before every poll: three set-ups, then the watch off; the round 1 reset case still set up again once [verified] |
| D2 S4 | minor | fixed | `NOTE_LEN` 160, as `logf_()`'s lines: the first-light summary keeps its PCLK meter figure (24 notes of 160 bytes, 3,840 bytes of PSRAM) |
| D2 S5 | minor | fixed | the default pair is `pclk_neg 1` with `pclk_phase 4` (`dosboard.c`, `dosboard.ini`, `board_dosboard.h`'s comment), opposite edges by ESP-IDF's register description and as both reference tables write 0x1281; then `pclk_neg 0` with phase 0, then the two same-edge pairs. `TEST-PLAN.md` asks to try all four at first light and prefer a pair whose delays 5 to 7 are clean too. Which edge the MS7210 samples at phase 0 stays [unknown] |
| D2 6 | note | fixed | `choose_mode()`: one extension that reads well (checksum right, a tag that is not 0x00 or 0xFF) and is not CEA-861 gives DVI; an unreadable or bad extension, or several extensions, still give HDMI. Host tests: block map or DisplayID with one extension: DVI; block map with two: HDMI; the round 1 cases unchanged [verified] |
| D2 7 | note | fixed | `pclk_phase` must be one digit 0 to 7 and `kbd_zero_guard_ms` 0 to 50; anything else is named on the console and the default kept |
| D2 8 | note | fixed | the board check probes 0x2B when nothing answers at 0x59 (the address is now a field of `ms7210_cfg_t`), accepts only the MS7210's chip ID there, and says which address answered. Host test: a chip at 0x2B is found there and set up without errors [verified]. On a Korvo-1 this adds one probe at 0x2B on its codec bus; only the MS7210's chip ID passes [inferred: no device at 0x2B there] |
| D2 9 | note | no change | as the review advises, while state 0 is only a fallback |
| D2 10 | note | no change | wording in the first pass's pin table only; the code was right (R205 10k pulls GPIO37 up, LED off at reset) |


## Round 1 (as written on 2026-10-21, with one correction)

Correction, round 2: E F4's last clause, "a read past the image's end gives zeros", was not what the
board would do. Through ESP-IDF's FatFs a seek past the end of an image open for writing stretches
the file, so v3.1 grew the image instead (B2 m1). Since v3.2 a sector past the end is refused.

Four independent reviews read tag v3 (2bb1c76) and this folder:
`REVIEW-concurrency-esp-idf.md` (C), `REVIEW-boot-safety-flash.md` (B), `REVIEW-emulator-correctness.md`
(E) and `REVIEW-dosboard-drivers.md` (D). The fixes are in the WSL tree
`~/tiny386-idf-scratch/v3/tiny386`, tag `v3.1` (80e64de, five commits on v3), and in this folder's
patch, images, scripts and notes. The app calls itself `v3.1`. Nothing has run on a board, no
serial port was opened, nothing was flashed.

Each finding below was checked against the source before it was acted on; every major one was
reproduced first (C F1 with the review's own model, E F1 with a new host test that fails on v3's
`i8042.c`, B M1 by reading the script's defaults) [verified].

Status: **fixed** (changed, and checked as the line says), **documented** (no code change; the
behaviour is now stated where Plat reads it), **deferred** (left, with the reason), **no change**
(the review itself found it right).

Tags as everywhere in this folder: [verified], [inferred], [reported], [unknown].


### Concurrency and ESP-IDF (C)

| # | Severity | Status | What was done, or why not |
|-|-|-|-|
| C F1 | major | fixed | `usb_input.c`: an interface usb_task gives up gets both closes, after its slot is freed (`close_interface()`); `if_callback()` posts an unplug only for the handle its slot holds, and `on_gone()` checks the handle too. `tests/host/usb/test_usb_input.c`, the review's model made a host test, passes all five cases, including [vendor, keyboard] and a mouse queued behind [vendor] [verified: `checks\host-tests.log`] |
| C F2 | minor | fixed | the slot is marked gone in the DISCONNECTED callback before the unplug is posted, and `set_lights()` skips it; host test case D: no SET_REPORT after the unplug [verified]. A request already in flight when the driver frees the device stays a limit of usb_host_hid [inferred] |
| C F3 | minor | fixed | `usb_events` has 4096 bytes; every task's least free stack goes to the UART every 10 s (`display.c`), and TEST-PLAN 7b asks for the figures. The figures themselves are [unknown] until the board runs |
| C F4 | minor | fixed | `vga.c`: with `VGA_TWO_CORES` (the ESP builds) an unknown VBE depth draws black instead of `abort()`; the Linux route compiles to the same binary as before [verified: `checks\linux-route-rebuild.txt`] |
| C F5 | minor | fixed | the mouse's state and packets are now the emulator's thread's only: `ps2_mouse_event()` puts reports in a ring, `kbd_step()` makes whole packets. A reporting thread against an emulator thread with the board's locking: framing, movement and clicks all right, also under ThreadSanitizer [verified: `checks\host-tests.log`, `checks\tsan-ps2-mouse.txt`]. The keyboard's E0 pair against an ACK: see E F3 |
| C F6 | minor | fixed | `console_hold()`: a fatal message or the PC switched off keeps the panel; `speed_screen_poll()` gives nothing back once the console is held |
| C F7 | note | fixed | the Ctrl+Alt+F12 request is cleared (`speed_forget_requests()`) before vga_task is let go |
| C F8 | note | fixed | an `ESP_LOGx` from an interrupt or a critical section goes to `esp_rom_vprintf()` (UART and, through the ROM putc hook, the ring) instead of nowhere |
| C F9 | note | deferred | needs a measurement first: the 10 s `display:` line gives the write-back times; TEST-PLAN 7b asks for a minute of it during BENCH. Band flushing only if disk I/O is seen to wait [unknown] |
| C F10 | note | fixed | `usb_setup()` waits on usb_input.c's own semaphore, 5 s, with a console error if the host does not start; a late give reaches only that semaphore |
| C F11 | note | fixed | every task creation is checked: `console_fatal()` for the emulator, USB, key repeat and CH9350L tasks, a console line for the sound task, a boot note for the hot plug task (it starts before the console) |
| C F12 | note | fixed | the driver's tasks wait up to 100 ms for room in usb_task's queue (the emulator's core still never waits); lost events are counted and shown on the console with what to do |
| C F13 | note | deferred | the lock-free sound state is tiny386's own and works on the shared cache [inferred]. `i2s_task` stays at priority 0: its mixing cost on the board is unmeasured, and at priority 1 a mixer that took the whole core would starve IDLE0 and trip the task watchdog, which at priority 0 it cannot [inferred] |


### Boot safety and flash (B)

| # | Severity | Status | What was done, or why not |
|-|-|-|-|
| B M1 | major | fixed | `back-to-linux.bat COMx` now means `small be5b725`, the way back the simulations give byte for byte; the release set needs the word `release` [verified: `checks\bat-harness.txt` cases 17 to 21] |
| B m1 | minor | fixed | every script reads the board before the prompt (`read-mac`, `read-flash 0x8000 0xC00`) and accepts only the tables it was made for (Linux 520538d3, dual boot d41cba3c, tiny386's own afbe6410, as each needs), saying what it found otherwise; the port must be named [verified: harness cases 3, 4, 5, 8, 9, 13, 16, 18, 22, 23, with a stub esptool] |
| B m2 | minor | documented | the standalone and DOS board builds still mount the flash `storage` partition when there is no card (that is how they find an ini there); its wear levelling writes in no Linux partition. Now said in FLASH.txt 5, TEST-PLAN risk 9 and `dualboot\DUALBOOT.md` 9 |
| B m3 | minor | fixed | FLASH.txt names all three writes of `idf.py flash`; with the dual-boot table selected, `esp/CMakeLists.txt` makes every flashing target (flash, app-flash, bootloader-flash, partition-table-flash, erase-flash, the encrypted ones) depend on a target that fails before esptool [verified: `ninja -t inputs` on the build graph, `checks\cmake-flash-refusal.txt`; no flash target was run] |
| B m4 | minor | fixed | `flash-standalone.bat` pins the bootloader (ec0a396b) and table (afbe6410) by SHA-256 [verified: harness case 14] |
| B m5 | minor | fixed | `sync` or `poweroff` in Linux, and a guest shut down or quiet for five seconds, before RESET: FLASH.txt 2, TEST-PLAN risks 4 and 5, steps 2 and 9 |
| B m6 | minor | fixed | `ide_attach()` refuses `/dev/mmcblk*` with a plain message, and the raw card block device is built only with `IDE_RAW_SD`, which nothing defines |
| B n1 | note | fixed | `flash-dualboot.bat` writes tiny386 first in its own esptool run, then the bootloader and table; FLASH.txt 1 gives the two commands in that order |
| B n2 | note | no change | GPIO42's pull-up after the check: nothing on the Linux side reads GPIO42 (the review's own finding) |
| B n3 | note | no change | the flash's Quad Enable bit: already set by the Linux bootloader on every boot (the review's own finding) |
| B n4 | note | fixed | FLASH.txt 6, the DOS board command by hand, has `-fm keep -ff keep -fs keep` |
| B n5 | note | fixed | `dosboard\images\` renamed `dosboard\images-old-v2\`; `dosboard\NOTES-dosboard.md` and FLASH.txt 6 say so |
| B n6 | note | fixed | `rom_exists()` holds a BIOS named as a flash partition to 256 KiB, as a file is; `bios = linux` now stops with a message before the PC is built |


### Emulator correctness (E)

| # | Severity | Status | What was done, or why not |
|-|-|-|-|
| E F1 | major | fixed | `i8042.c` (with `PS2_KBD_HOOKS`, the ESP builds): packets made in `kbd_step()`, a whole packet or none, no faster than the guest's sample rate, the backlog held to 1,023 counts, a click never merged into the movement before it, a reset dropping what the guest had not had. `tests/host/test_ps2_mouse.c` runs the review's case: 270 bytes, 90 whole packets, the last one `28 00 FB`; the same test against v3's `i8042.c` fails, 271 bytes [verified: `checks\host-tests.log`]. The Linux route (no `PS2_KBD_HOOKS`) compiles to its old code and keeps the old behaviour [verified: byte-identical rebuild] |
| E F2 | minor | fixed | `run_pc()` counts and flushes the floppies with the disks; `emulink_flush()` calls `fsync()` on `BUILD_ESP32` |
| E F3 | minor | fixed in part | the mouse: as C F5. The keyboard: an ACK from the emulator's core can still fall between an E0 prefix and its second byte, from the USB task. Deferred: rare [inferred], and the cure is the same move for the keyboard (reports to the emulator's thread), which touches the typematic timing and wants its own round of tests |
| E F4 | minor | fixed | on `BUILD_ESP32` a read, write or flush the card refuses is counted and is an IDE error for the guest; a red console line says so (again at most every 10 s); a read past the image's end gives zeros |
| E F5 | note | deferred | 132-column text: a scaled text path, or a clear and a notice from `vga.c` on core 0, is work of its own; already listed in `NOTES-core1.md` 3.7 |
| E F6 | note | fixed | `setup_find()` passes over a place whose path does not fit instead of cutting it short; `test_setup.c` makes its card under a short folder (in `TMPDIR`) when OUT is long [verified: all host tests pass with a 109-character OUT, `checks\host-tests-long-out.log`] |
| E F7 | note | deferred | mouse scaling (the guest's resolution, or an ini setting) is a choice about feel to make with a mouse on the board; pacing at the sample rate is done (E F1) |
| E F8 | note | deferred | leaving the NE2000 out on boards without Wi-Fi changes `pc.c`, shared with the Linux route, and the PC the guest sees; the device window has room as it is (about 85,600 to 92,300 of 131,072 bytes [inferred: the review's sizes]) |
| E F9 | note | fixed | as C F3 |
| E F10 | note | documented | a boot-protocol mouse's wheel only if it sends a fourth byte; JIS keys not mapped; scancode set 1 always. None matters for DOS or Windows 95 with Plat's keyboards [inferred]; listed here, no change |


### DOS board drivers (D)

| # | Severity | Status | What was done, or why not |
|-|-|-|-|
| D F1 | minor | fixed | `ms7210.c`: block 0 good and the extension unreadable, bad or not CEA-861 now gives HDMI, as no EDID does, with its own log line; the extension is read twice before giving up. Host test: NAK, bad checksum and block map give HDMI with sound (0x0503 00, 0x0531 84); a valid EDID with no extension still gives DVI [verified] |
| D F2 | minor | fixed | `lcd_bounce_px` held to 16,000 (20 rows); if the RGB output does not start with bounce buffers it is started again without, with a note |
| D F3 | minor | fixed | the PCLK meter is read again after section E and logged (`PCLK meter now ...`); no WAIT row was added, so the rows stay exactly encoder.md 8.4's; the table check allows the two extra reads, marked as an addition [verified: 114 rows still match] |
| D F4 | minor | fixed | `[dosboard] rb_swap`, `i2s_slot_bits` and `audio_shift`; the defaults are the old behaviour (no swap, 16-bit slots, divided by 16) [verified: host test for rb_swap; the I2S settings are built, not run] |
| D F5 | note | documented | the four PCLK pairs, in order, in `dosboard.ini` and TEST-PLAN; both reference tables use phase 4 |
| D F6 | note | documented | TEST-PLAN: no sound, unplug and plug in the HDMI cable once to tell the two cases apart. Rerunning E66 to E69 after I2S starts waits for first light to show it is needed |
| D F7 | note | documented | the APLL as the next step if HDMI sound clicks (TEST-PLAN); a rebuild |
| D F8 | note | fixed | the hot plug poll reads 0x120C back and sets the chip up again from section B if it lost D1's value; the LED blinks only while the MS7210 answers. Host test: a reset under the driver is seen and set up again [verified]. 500 ms polling and the unused INT pin stay |
| D F9 | note | fixed | state 0 keyboard reports must be 8-byte boot reports (`boot_report_ok()`), as state 2's are. Mouse reports unchanged: a 5-byte report-protocol mouse would otherwise be dropped in the fallback state |
| D F10 | note | fixed | a status frame is answered at most every 100 ms; status frames are counted, and the 10 s log also prints when they climb |
| D F11 | note | deferred | skipping the board check with USER held: on a Korvo-1 that has the DOS board image by mistake, GPIO42 is the dual-boot key, and skipping would drive DOS board pins there; the UART already says why the check failed. For Plat to decide at bring-up |
| D F12 | note | fixed | `hdmi_mode` and the 0/1 keys compared without case (yes, no, on, off too); a value that cannot be read is named and the default kept; `ch9350_ps2_leds_to_usb()` is for the host test only; FLASH.txt names the CH340C for the DOS board |


### Plat's question (in the first review round)

A third USB-A port on the S31's own high-speed pins is not needed for flashing: the DOS board
flashes and logs through the CH340C on UART0, with DTR and RTS driving EN and BOOT (GPIO61), and
the firmware does not use the S31's own USB at all on that board [verified: the DOS board review
1, from `dosboard.net`; `esp/main/CMakeLists.txt` builds no `usb_input.c` for it].
