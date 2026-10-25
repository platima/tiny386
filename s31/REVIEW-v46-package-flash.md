# Review of v4.6: the package and flash safety

Written 2026-10-25. Labels: [verified] = I ran it or read it; [inferred] = reasoning, not measured.

Lens: the staged package in WSL `~/org2/v46/pkg` against the package folder
`D:\Repos\esp32-s31-linux-release\tiny386-idf\v3`. Bootloader and tables, the app's fit,
`flash-dualboot.bat COM10 app`, the checksum lists, the two builds, the docs against the code, the
hashes after the rewrite, Windows content, personal addresses and the style rules.

Nothing was flashed, no COM port was opened, nothing was pushed, tagged or committed. The package
folder, `~/org2/v46/int`, `~/org2/v46/pkg`, `~/fw` and `D:\Repos\tiny386` were only read. My work is
in WSL `~/org2/v46/rev-package-flash` (a clone of the integration repo on `v46`, an overlay of the
folder with the stage copied over it, and one build) and in `scratchpad\v46\rev-pf` (the scripts and
a sandbox for the batch file). Nothing I started is still running.

(Added 2026-10-25, when this review was taken into the branch: in 2.7 the old `s31-v4.5` commit
and the two old tag objects, which the history rewrite left behind, are named in words rather than
by their hashes, as the branch names none of them (`BUILD-NOTES.md` 32.6). Nothing else in this
review was changed.)


## 1. Verdict

**Ready.** Nothing found puts the board, Linux or the card at risk, and the package is what the
integration report says. Six small findings, all in the docs or the folder around the stage, none
in what gets flashed (section 3).


## 2. What I checked, and how

### 2.1 The bootloader, the tables and `flash_args`

* Every `bootloader.bin`, `partition-table.bin` and `flash_args` in the stage's four image sets is
  the package folder's current file byte for byte (SHA-256 compared file by file) [verified]. The
  dual-boot bootloader `b57469cd`, the Linux-default table `d41cba3c`, the tiny386-default table
  `5b90d416`, the standalone and DOS board's `ec0a396b` and `afbe6410`.
* The same hashes are the ones pinned in `flash-dualboot.bat` (`BL_SHA`, `PT_SHA`, `PT386_SHA`,
  `PT_DUAL`, `PT_DUAL386`, `PT_OWN`) and `flash-standalone.bat` (`SA_BL_SHA`, `SA_PT_SHA`), which
  v4.6 does not change [verified].
* Decoded with ESP-IDF's `gen_esp32part.py`: tiny386 is app/test at 0xE20000, 1920K, under the
  Linux-default table, and app/factory at the same place under the tiny386-default one; the
  standalone and DOS board tables have the factory app at 0x10000, 1792K [verified].

### 2.2 The apps fit

| Image | Size | Partition | |
|-|-|-|-|
| `korvo1-dualboot\tiny386.bin` (and `-tiny386-default`) | 936,192 | 1,966,080 | 47.6 per cent |
| `korvo1-standalone\tiny386.bin` | 936,192 | 1,835,008 | 51.0 per cent |
| `dosboard\tiny386.bin` | 958,608 | 1,835,008 | 52.2 per cent |

[verified: file sizes, the decoded tables.] The app's image header (chip ID 32, flash mode DIO,
80m, 16MB, chip revision v0.0 to v0.99, five segments) is v4.5's apart from sizes, hashes and the
version [verified: the two `image-info` outputs compared].

### 2.3 The two builds, and a third of my own

* `build-a` and `build-b`: the 23 image files and the six `image-info` outputs are identical
  [verified: `diff -r`]. The stage's `images\` is `build-a`'s [verified].
* I ran the staged `build.sh` myself from the overlay (the folder with the stage copied over it),
  into a third folder: exit 0, 173 s, and all 23 image files and the six `image-info` outputs are
  the stage's byte for byte [verified]. So the build is the same in three different folders, and
  the package as it will be builds its own images.
* The tree `build.sh` made from the patch is v46 (2d076f6) with `s31\` left out, tree for tree
  (`77b5145`) [verified: `git write-tree` of the patched index against v46's tree without `s31`].
* Both staged patches and both diffstats are the git diffs of 4ce2e06 and 9ed786b against v46 with
  `s31\` left out, byte for byte [verified]. v4.5's two patches made the same way from the rewritten
  `s31-v4.5` (99074b7) are `previous-v4.5\`'s byte for byte [verified].
* `esp/dependencies.lock` at v46 is the package's `dependencies.lock` [verified].

### 2.4 `flash-dualboot.bat COM10 app`

I ran the unchanged script in a sandbox (`scratchpad\v46\rev-pf\batsim`) with a stand-in for
esptool that never opens a port: `version` and `image-info` go to the real esptool 5.4.0 on this PC
(they only read a file), `read-mac` and `read-flash` return the board's bytes for the table chosen,
and `write-flash` is only logged. TEMP and the backup folder pointed into the sandbox.

* Over the tiny386-default table (Plat's board now), answer Y: the script printed
  `App version: v4.6` from esptool's own `image-info` of `images\korvo1-dualboot\tiny386.bin`,
  `tiny386, 936192 of 1966080 bytes`, the dual-boot bootloader recognised, then one
  `write-flash` with `-a hard-reset` and one file, `0xE20000`, 936,192 bytes [verified: the
  stand-in's log]. Nothing else was written, and nothing was read but the MAC, 0x8000 (0xC00 bytes)
  and 0x2000 (22,352 bytes).
* Over the Linux-default table: the same single write [verified].
* Answer N: `Stopped.  Nothing was written.`, no write in the log [verified].

### 2.5 The checksum lists

On the overlay (the folder as it will be once the stage is copied over it), with a checker of my
own (`rev-pf\lists.py`):

* The top `SHA256SUMS`: 182 lines, 182 OK. Same names in the same order as v4.5's list; 19 hashes
  changed (the seven docs, `build.sh`, four patch files, seven `tiny386.bin` and `.elf`) [verified].
* Each image folder's own list: OK. `dualboot\SHA256SUMS`: OK [verified].
* Every earlier list, `previous-v4.5\` to `previous-v3.2\`: not one file missing.
  `previous-v4.5\`: 31 inside, 151 from `v3\` [verified]. The same check on the folder as it is now
  gives the same counts for the older lists [verified].
* `previous-v4.5\` (36 files) is the folder's current v4.5 files byte for byte, and its docs are the
  rewritten `s31-v4.5` tag's [verified].
* Nothing needs deleting from the folder: no file in its four image folders is missing from the
  stage's [verified].

### 2.6 The new code, from the flash side

* v4.6 writes nothing to flash. The only new flash-related call is
  `esp_ota_get_running_partition()` in `restart_comes_back()`; there is no OTA write, no
  `esp_ota_set_boot_partition()`, no partition write or erase in `esp/main` [verified: git grep].
  The note lives in the LP SRAM (`dbuf_note`, 180 bytes at 0x2E002010) [verified: nm].
* That lookup already runs before `app_main()` at every boot, in v4.5 too: ESP-IDF's
  `esp_ota_auto_confirm` is registered as a pre-app-main handler
  (`_esysev_PRE_APP_MAIN_esp_ota_auto_confirm`) and calls it, in both ELFs [verified: nm, objdump,
  ESP-IDF's `ota_auto_confirm.c`]. The result is cached. So the call cannot crash on Plat's board
  [inferred: v4.5 booted there]; only the subtype it returns is untested there.
* The restart comes after the boot menu and the ini, and before the card partition is handed over.
  The menu writes `lastboot.txt` only when a key chose, and closes it before the ini is read; then
  one second passes before `esp_restart()` [verified: code]. No file is left open over the restart
  [inferred: no long-lived card FILE in `esp/main` but the UART's].
* A reset in a two-buffer start, a broken note, a software restart or the Linux-default table all
  stop a second restart [verified: code]. The worst case is one restart per RESET, never a loop.

### 2.7 Hashes after the rewrite

* The 32.6 table: each new hash has the subject given, the tags resolve to 6d619a0 and 99074b7, and
  against the old commits (in `~/org2/v46/rewrite-verify/both.git`, read only) the seven differ in
  `s31/checks/partition-passthrough-v4.4.txt` alone and 99074b7 has the old `s31-v4.5` commit's tree [verified].
* No old commit hash and neither old tag object's hash is in v46's tree, in its 11
  commit messages, or in the stage outside `previous-v4.5\` [verified: grep of all ten].
* The renamed hashes in `REVIEW-RESOLUTION.md` and the reviews point at the commits the text
  describes (`lcd_double_totals()` in 47290db, `sd_part_route_files()` in f76aaab, the newcard
  scripts in dd51a62, `tagref` in 6e5e150) [verified].
* Every commit hash in v4.6's new doc lines resolves, but three: 97a450b and 81e09ee are upstream's
  (97a450b's patch-id equals 876af9c's [verified]), and f7f7492 (finding 1).
* The bundle `~/org2/v46/int/v46.bundle` verifies, needs c8528f2 and carries `v46` at 2d076f6
  [verified].

### 2.8 Windows content, personal addresses, commits, style

* The stage outside `previous-v4.5\`: the only addresses are the GitHub noreply one and Anthropic's
  noreply [verified: grep]. None in the images' strings. The 11 patches carry those two and
  upstream's own author address on 0001 [verified].
* No Windows file content in v4.6's additions or the stage: `IOS.LOG` appears only as a file name
  and in the paraphrased check; no binary in v4.6's diff [verified: grep, numstat].
* Commits 2 to 11: author and committer Platima with the noreply address, ending with the
  Co-Authored-By line; 876af9c keeps Chunhui He and "fix" with nothing added [verified].
* No em dash, en dash or double hyphen in v4.6's added doc lines or commit messages (the one double
  hyphen is git's own separator inside `build.sh`'s command), and no US spelling from my list
  [verified: grep]. `FLASH.txt` keeps CRLF on every line; the other docs LF, as before [verified].

### 2.9 The docs against the code

Checked line by line where the code can say: `TEST-PLAN.md` 17's quoted lines (the grey restart
line, `Reset by a software restart.` in grey, the green `Starting ... (no menu).`, the two buffers of
750 KiB, the yellow lines of 17d, the 10 s hold when the console has a yellow line), 17b's
`PC built. Device memory ...` line and the window of 131,040 bytes from 0xA0020, the Ctrl+Alt+F9
behaviour in each state, the defaults (`pace_quiet = 0`, `double_buffer = 0`, both commented out
in every card ini), and `BUILD-NOTES.md` 32's figures (sizes, DIRAM, LP SRAM, patch sizes and counts,
the diff stat of 40 files, the image hashes, v4.5's sizes). All agree [verified]. `FLASH.txt` 2 and
3, `dualboot\DUALBOOT.md` 13.8 and the package `README.md` agree with the script run of 2.4 and the
code of 2.6 [verified].


## 3. Findings

### 1. minor: f7f7492 is named, but exists nowhere

`BUILD-NOTES.md` 32.7, 32.9 and 32.14 and `checks\reproducible-v4.6.txt` (lines 281, 282, 290) name
f7f7492, "the version commit before 32.1's one-line change". It is in neither the integration clone
(its refs were deleted and the clone pruned) nor `~/fw/tiny386-s31`, and it will never be on GitHub
[verified: `git cat-file` in both]. A reader cannot look it up. It is the same kind of loose hash
that the integration took out of `run.sh`, though harmless here.

Suggested: name it without a hash, for example "the version commit before 32.1's one-line change
(not kept)", or say plainly that its hash is not in the history. The folder name
`prelim-f7f7492/` in 32.14 can stay.

### 2. note: TEST-PLAN 17b assumes the ini still has double_buffer commented out

17b says "Boot `win95-sd.ini` as it is (`double_buffer` commented out, so 0)". Plat set
`double_buffer = 1` for v4.5's test, and v4.5 crashed with it, so his card's `win95-sd.ini` now holds
whatever he changed it back to, perhaps `double_buffer = 0` uncommented, perhaps still 1 if he only
used VOL+ [inferred]. If it is still 1, the first v4.6 boot takes 17c's restart path, not 17b's.
Nothing goes wrong, but the order of the steps does.

Suggested: "Check that `win95-sd.ini` has `double_buffer = 0`, or the line commented out (you set it
to 1 for v4.5)."

### 3. note: a personal address in two folder files the stage does not touch

`checks\merge-v4.3.txt` in the package folder lists eight commits with Plat's personal address; the
branch's copy of the same file has it replaced by "address removed before publishing" [verified:
diff]. `core2\resume-check\test-storage-host.patch` has the same address [verified: grep]. Both date
from before v4.6, no `SHA256SUMS` names either, and the folder is not published, so nothing leaks
now.

Suggested: in the release step, copy the branch's `s31/checks/merge-v4.3.txt` over the folder's
(no list changes), and take the address out of the core2 patch, or at least do both before the
folder or a release from it goes anywhere public. `previous-v4.5\`'s old hashes (already flagged in
the integration notes) belong to the same decision.

### 4. note: five docs lost their executable bit in 2d076f6

git's diff summary from `s31-v4.5` to v46: mode 100755 to 100644 for `s31/BUILD-NOTES.md`,
`FLASH.txt`, `REVIEW-RESOLUTION.md`, `TEST-PLAN.md` and `dualboot/DUALBOOT.md` [verified]. The other
20 files at the top of `s31/` stay 100755. Harmless, and the right mode for a text file, but not
mentioned in the notes or the commit message.

Suggested: leave it and add a line to 32.14 or the commit message, or put the modes back before
the push so that v4.6's diff shows content only.

### 5. note: "the list leaves out `checks\`" is not quite so

`BUILD-NOTES.md` 32.11 ("As before the list leaves out `checks\` and `dualboot\`") and
`checks\sums-v4.6.txt` say the same. The top list names 14 files under `checks\` (the five of
`vbe480\`, two of `mode13x\`, four of `flicker\`, one each of `t386mon-v4.3\` and `win95-v4.3\`, and
`sdpart-harness-v4.4.diff`) [verified]. The package `README.md`'s `SHA256SUMS` row says this right.

Suggested: "leaves out `dualboot\` and most of `checks\` (it names the 14 files `README.md` lists)".

### 6. note: FLASH.txt still says "seven review rounds"

Its first paragraph reads "App version v4.6: v3 with seven review rounds' fixes"; `REVIEW-RESOLUTION.md`
has 11 rounds now. The count was already behind in earlier releases [verified: v4.5's text is the
same but for the version].

Suggested: "v3 with the review rounds' fixes" or the right number.

### Not findings

* `REVIEW-v44-storage-and-safety.md` now reads as if `git ls-remote` showed 6d619a0 and 7d7febe on
  2026-10-24, which GitHub did not have then. The line added at its top explains that every hash
  was replaced, so I would leave it.
* `s31-v4.6` is named in `build.sh`, the README and the patch rows before the tag exists. The tag
  is meant for 2d076f6, and the patches are the same from 190ec33 and 2d076f6 [verified], so that
  holds once the release step is done.
* 32.12 and 13.8 call the running-partition lookup untested on a board. Section 2.6 shows the call
  itself already runs at every boot since v4.5; only its subtype is untested. Worth a line, not a
  change.


## 4. Where things are

* WSL `~/org2/v46/rev-package-flash/`: `repo/` (my clone, branch `v46` at 2d076f6, unchanged,
  22 MB), `overlay/` (the folder with the stage copied over it, 622 MB), `build/` (my `build.sh` run,
  1.1 GB), `build.out`. All throw-away.
* `scratchpad\v46\rev-pf\`: `lists.py` (the checksum list checker), `batsim\` (the batch file
  sandbox: `run.cmd`, `tool\fake_esptool.py`, `tool\fake-esptool.cmd`, the copied script and images,
  and the stand-in's logs in `tmp\`).
