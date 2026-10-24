# tiny386 v4.0-base: v3.9 merged with upstream tiny386

Written 2026-10-23, Perth time (+0800) throughout. The firmware brought up to upstream tiny386
as it stands, keeping everything v3.9 does: the upstream commits since 9ed786b, which of them
v3.9 already had, which conflicted and how each conflict was resolved, and the checks that
nothing regressed. The display work and the v4.0 package come next; this note builds no release
image and changes nothing in the package but itself. Nothing was flashed, no serial port was
opened, nothing was pushed, nothing was done on GitHub, and nothing was committed anywhere but
the scratch repository `~/tiny386-idf-scratch/v3/tiny386` (WSL).

Tags:

* [verified] read in the named source, or seen in a build, test or run made for this note
* [inferred] follows from verified facts; not built or run
* [unknown] only the board can say


## 0. In short

* **Where.** Branch `v4` in the v3 repository, made in a worktree of its own,
  `~/v4-work/merge/tree`, so that the v3 tree's own checkout (branch `integ` at v3.9) was never
  touched. Tags: `pristine` still 9ed786b; `upstream-20261022` is upstream master 4ce2e06, its tip
  when fetched at 07:49 on 2026-10-23 (committed 2026-10-22 20:26); `v4.0-base` is b370a75, three
  commits on v3.9: the merge (d2229a4), new host test cases (e8afd4b), and `esp/version.txt` set
  to `v4.0-base` (b370a75) [verified: `git log`, `git tag`].
* **A merge, not a rebase** (section 2): one resolution against the final state of each side,
  v3.9's 72 commits kept as they were tested, every hash in `BUILD-NOTES.md` still in v4's history,
  and `git diff upstream-20261022 v4.0-base` is still the whole patch: applied to upstream 4ce2e06
  it gives the `v4.0-base` tree exactly [verified].
* **Upstream since 9ed786b: 18 commits** (section 1). Seven are Plat's PRs 28 to 33, merged on
  2026-10-20, which v3.9 has in its own form; six are new fixes to the 386 and the PIT by Yejun
  Yang; five are Chunhui He's own (a Nuked OPL3 option, the Makefile, two for Android, the screen
  flicker).
* **Conflicts:** one in `i386.c` (the task gate's error code, which v3.8 and upstream fixed each
  in its own way) and six in `vga.c` (the cursor glyph, two comments, the line loop's end of video
  memory, the clear on a CRT controller write, a comment). One more change merged without a
  conflict but was set back: upstream's `I386_ALIGNED_ONLY` to the packed accessors, where this
  tree has `unaligned.h`. The PIT's catch-up cap and its ini key `timer_catchup` survive untouched
  beside upstream's one-shot modes, and a new test shows both together (section 4).
* **Checks** (section 5): the Korvo-1 dual-boot and standalone builds and the DOS board build,
  and the crash-test build of each board, exit 0 with v3.9's one old warning and no new one,
  also with `-Wall -Wextra` on the core files; 33 of 33 host tests (v3.9: 32); Windows 95 OSR2 in
  54 PC runs, 27 configurations on each of v3.9 and v4.0-base, every one at the desktop at the
  same virtual second and the same instruction count on both; FreeDOS with BENCH.COM the same to
  the instruction at each of BENCH's marks; no difference in speed on the PC.
* **Why nothing moved in Windows 95** (5.4): a probe build counted every path the merge changed,
  and Windows 95's boots to the desktop reach none of them: no near jump whose operand and address
  sizes differ, no task switch, the PIT's channel 0 only ever in mode 2, no VBE picture
  [verified]. The task expected the times and counts to move with the core; they did not, and
  that is the reason.
* **What upstream adds for us:** a DOS program that puts the PIT's channel 0 in mode 0 or 4 (or 1
  or 5) no longer stops the emulator with `abort()`, which on the board is a crash screen
  [verified: v3.9's `i8254.c` in the new test]; near jumps with a 66h prefix decoded right; a task
  switch's flags right; a VBE line that begins past the end of video memory drawn black. None of
  it is faster or slower [inferred: sizes, 5.1; PC timing, 5.6].
* **For the next step** (section 7): `build.sh` needs upstream 4ce2e06 and the patch from
  `upstream-20261022`; a finding for the display work: every write to a VBE offset register
  clears the whole panel (section 6).


## 1. Upstream since 9ed786b

`git fetch origin` in the v3 repository (origin is github.com/hchunhui/tiny386; the remote was
already there): `origin/master` moved from b7980ab to 4ce2e06 [verified]. Every commit in
`pristine..upstream-20261022`, oldest first; dates are when hchunhui committed them, in Perth
time [verified: `git log`]:

| Commit | Date | Author | What | v3.9 | Could it improve ours |
|-|-|-|-|-|-|
| 0e55dd6 | 10-15 21:53 | Chunhui He | adlib: an option to use Nuked OPL3 (`USE_FMOPL = nuked_opl3`, the library cloned into `build/Nuked-OPL3`, not in the tree); `free(s)` when `OPLCreate()` fails | new | The `free()` is a leak fixed on every build, taken. Nuked OPL3 is an exact OPL3 and sounds closer to a real AdLib; whether the DOS board's core 0 has the time for it is [unknown], and the board builds use fmopl as before (`USE_FMOPL`, 5.1) |
| 250c7d9 | 10-15 21:58 | Chunhui He | Makefile: an optional `config.mk`, a `show` target | new | No: the boards build with ESP-IDF's CMake. `make prepare` (the OPL tables) works as before [verified: run in the worktree] |
| 93f4958 | 10-17 00:01 | Chunhui He | android: orientation (`rawdraw/main.c`, the manifest) | new | No: Android only |
| 780905d | 10-17 00:05 | Chunhui He | android: a keyboard designer | new | No: Android only |
| 43b4b85 | 10-18 15:30 | Chunhui He | vga: no screen clear on a CRT controller write in a graphics mode (it flickered) | **conflicts**: v3.6 made that clear wait for the next picture and be left out when it would change nothing | Not for the boards, which keep v3.9's (4.4); upstream's builds of this tree now flicker no more |
| e6fa0b5 | 10-20 20:25 | Platima | vga: the cursor glyph long enough for 32 lines | has it, in its own form; **conflicts** in text only | No: the same 128 bytes. Upstream's text taken (4.2) |
| 03be7a7, 4348cfa | 10-20 20:32 | Platima | vga: no read past the end of video memory when drawing; the check overflow-safe, a skipped line black | has the first in its own form (slack behind video memory, `VGA_MEM_SLACK`); **conflicts** | Yes, a little: a VBE line that begins past the end is now black, where v3.9 left whatever was on the panel (4.3) |
| 949e506 | 10-20 20:33 | Platima | vga: video memory cleared only when VBE is turned on | has it; **conflicts** in a comment | No: the same code |
| 9e1975d | 10-20 20:33 | Platima | vga: mode 0Dh at its full width | has it; **conflicts** in a comment | No: the same code |
| 7588dac | 10-20 20:34 | Platima | kvm: `kvm.c` built only with `USE_CPUABS` | not in v3.9 (the boards never compile `kvm.c`) | No effect on the boards; taken |
| b7980ab | 10-20 20:34 | Platima | i386: `-DI386_ALIGNED_ONLY` uses the packed accessors | has its own way (`unaligned.h`); merged without a conflict but **set back** | No: the same loads, slower stores on the board (4.5) |
| d81e225 | 10-21 21:01 | Yejun Yang | i386: a near JMP or Jcc (E9, 0F 80 to 8F) takes a rel16 or rel32 by the operand size, not the address size | new | Correctness: `66 E9 rel32` in 16-bit code ran the rest of the displacement as code. Windows 95's boot never meets it (5.4); DOS extenders and mixed 16 and 32-bit code may [inferred] |
| 0642735 | 10-22 20:24 | Yejun Yang | i386: a task switch loads the new task's flags (the lazy flags' mask cleared); an exception through a task gate pushes its error code | the error code: v3.8 did it its own way, **conflicts**; the flags: new | Correctness for guests that switch tasks in hardware (some DPMI hosts, OS/2, old Linux) [inferred]; Windows 95's boot makes no task switch (5.4) |
| e2f9e9e | 10-22 20:24 | Yejun Yang | i386: that error code written at the new task's CPL | new | Taken into v3.9's form (4.1); no difference at CPL 0, where a double fault's task runs [inferred] |
| 7989c8f | 10-22 20:26 | Yejun Yang | i8254: channel 0 in mode 0 or 4 gives one interrupt when its count runs out; 1 and 5 none; 6 and 7 are 2 and 3; where it called `abort()` | new | **Yes**: v3.9 stops the emulator when a DOS program sets channel 0 to mode 0, 1, 4 or 5 [verified: test 19 against v3.9's `i8254.c`] |
| 16a060a | 10-22 20:26 | Yejun Yang | i8254: modes 6 and 7 read back as programmed; the first byte of a mode 0 reload stops the count | new | Correctness, as above |
| 4ce2e06 | 10-22 20:26 | Yejun Yang | i8254: the one-shot comparison safe across the clock's wrap | new | As above |

The PR numbers: the brief lists PRs 28 to 33 as the cursor glyph, the overread, the VBE clear,
mode 0Dh, `kvm.c` and `I386_ALIGNED_ONLY`; the seven commits above carry those subjects, the
overread in two (4348cfa says it answers the review of 03be7a7) [inferred]. Two of Yejun Yang's
commits are authored as "M Y" with a noreply address of the same GitHub user [verified: `git
log`]. None of the 18 touches `ide.c`, `i8042.c`, `misc.c`, `pc.c` or anything under `esp/`
[verified: the diff's file list, `pristine` to `upstream-20261022`].


## 2. Merge, not rebase

v3.9 is 72 commits on 9ed786b, in a line [verified: `git rev-list`]. A rebase would replay each
onto 4ce2e06: every commit of the 72 that touches `vga.c` or `i386.c` would meet the same
conflicts again in some intermediate form, and the result would be 72 new commits, none of them
built or tested as they stand, while every hash `BUILD-NOTES.md`, `REVIEW-RESOLUTION.md` and the
checks name (`v3`, `v3.9`, d370a7a and the rest) would no longer be in the history the firmware
is built from.

A merge resolves each conflict once, between the two final states, which are the two that were
tested (v3.9 on the PC and on Plat's board, upstream by its author). v3.9 and 4ce2e06 are both
ancestors of `v4.0-base` [verified: `git merge-base`], so `git log v3.9..v4.0-base` shows exactly
what came in, `git diff v3.9 v4.0-base` exactly what changed for the board, and `git diff
upstream-20261022 v4.0-base` is still one patch against upstream that gives the tree (5.7). The
cost is a history that is not a straight line; the upstream PRs are cut from upstream's master in
the other session anyway, so that cost falls nowhere.


## 3. The branch, the tags, the commits [verified: `git log`, `git tag`, `git worktree list`]

| Ref | Commit | What |
|-|-|-|
| `pristine` | 9ed786b | unchanged: tiny386 as v3 began from |
| `upstream-20261022` | 4ce2e06 | new: upstream master as merged |
| `v3.9` | d370a7a | unchanged |
| d2229a4 | | the merge, its message listing every resolution |
| e8afd4b | | `tests/host`: the PIT's one-shot modes beside the cap (test 19, and a run against v3.9's `i8254.c`), a VBE picture past the end of video memory (`test_vga.c`) |
| `v4.0-base`, branch `v4` | b370a75 | `esp/version.txt`: `v4.0-base`, so that an image built from it does not call itself v3.9 |

The commits are Plat's identity as the scratch tree's earlier ones are (Platima Tinkers), with no
co-author line, as before. `esp/dependencies.lock` is modified in the worktree, as in the v3 tree,
and left out of every commit. The worktree is left on a detached HEAD at `v4.0-base`, so that the
branch `v4` can be checked out in the v3 tree itself (`git checkout v4` there); the worktree can
also be used as it is (`git checkout v4` in it). If it is ever deleted, `git worktree prune` in
the v3 tree forgets it. The local branch `master` there (b7980ab) was left as it was.


## 4. The conflicts and their resolution

### 4.1 `i386.c`: the task gate's error code

Both sides make an exception delivered through a task gate push its error code onto the new
task's stack. v3.8's form keeps the error code from before the switch and writes it with TRY1
(an abort on a fault); upstream's reads `cpu->excerr` after the switch, returns false on a fault,
and (e2f9e9e) checks the write at the new task's CPL.

Kept: v3.8's form, with upstream's CPL. The reason for TRY1 is this tree's `call_isr()`, which
v3.8 wraps around `call_isr1()`: on a false return it puts back SS, ESP, EFLAGS and the stack mask
from before the delivery, which after a completed task switch would put the old task's stack
under the new task's code. Upstream has no such wrapper, and its own `TRY1(call_isr(...))` in
`cpui386_step()` aborts on that false return anyway, so the two behave alike there [verified:
source]. `task_switch()` itself never returns false (every failure in it is a TRY1) [verified].
The comment says all this.

Upstream's other half of 0642735, `cpu->cc.mask = 0` after loading the new task's EFLAGS, merged
without a conflict and is in [verified: `git diff v3.9 v4.0-base`].

### 4.2 `vga.c`: the cursor glyph and two comments

The cursor glyph: v3.9 filled 128 bytes with a GNU range initialiser, upstream (the PR's form)
with sixteen `FF8` macros; the same bytes. Upstream's taken, which shrinks the patch. The comments
on the half dot clock and on the VBE clear: upstream's text taken; the code below each is the
same on both sides [verified].

### 4.3 `vga.c`: the line loop at the end of video memory

v3.9's loop has panning, the split screen, the quick way for the 16-colour modes and the rows
redrawn, none of which upstream has; for the end of video memory it has `VGA_MEM_SLACK`, 16 KiB of
zeros behind video memory (`pc.c`, `esp_main.c` and the tests allocate it), more than any line
drawn there can read (a line is at most the screen's width: 3,200 bytes for 800 pixels of 32
bits; wider pictures go to the shrinking path, which has its own check), so a line that runs past
the end reads zeros. A line that begins past the end (only in a VBE mode, or with less than
256 KB) v3.9 did not draw at all, which left whatever was on the panel there. Upstream (03be7a7,
4348cfa) counts the bytes a line reads and draws the whole line black if any of them would be past
the end.

Kept: v3.9's loop, with one change: a line that begins past the end now starts reading at the
start of the slack, so it is drawn as zeros, black, as upstream draws it. A line that only runs
past the end is drawn as far as video memory goes and black after it, as v3.9 drew it, which
shows more of the picture than upstream's whole black line. The quick 16-colour way is not
affected (its lines are never past the end: the VGA modes wrap at 256 KB). `vga_graphic_refresh()`
is 72 bytes smaller in the board's code, the branch that skipped the line gone [verified: `nm`,
5.1]. The new case in `test_vga.c` (VBE 640x480x16 from line 400 of 1 MiB) passes, and fails
against v3.9's `vga.c` on exactly the rows that begin past the end [verified: 5.2].

### 4.4 `vga.c`: the clear on a write to the CRT controller

Upstream 43b4b85 deletes the screen clear that every write to a CRT controller register made in a
graphics mode (the BIOS writes two for every character it prints, so the screen flickered). v3.6
made that clear wait for the next picture and be left out when it would change nothing, on one
core or two.

Kept: v3.9's deferred clear in every build that has it (`VGA_TWO_CORES`, both boards and the PC
harness's board flavour; `VGA_PLANAR_FAST` with `vga_planar_fast` on, the Linux route's flags).
Only where v3.9 still cleared at once (one core, without `VGA_PLANAR_FAST` or with it turned off:
upstream's own flags) there is now no clear, as upstream has it. The board's code is unchanged
by this [verified: the source; the PC runs with upstream's and the Linux route's flags give the
same pictures and counts as v3.9, 5.5].

### 4.5 Merged without a conflict, and looked at

* **`I386_ALIGNED_ONLY` (b7980ab), set back.** Upstream's PR makes `-DI386_ALIGNED_ONLY` take the
  packed-struct guest memory accessors, MIPS's; the boards define it [verified: `board_korvo1.h`,
  `board_dosboard.h`]. Merged as it was, it would have moved the boards off v3.9's `unaligned.h`
  accessors, with which every board figure was measured. Compiled with the board's compiler
  (esp-16.1.0, rv32gc, `-O3`) the two give the same code for loads (four byte loads; the compiler
  takes `unaligned.h`'s alignment test out), while for a store `unaligned.h` makes one word store
  when the address is aligned and the packed form always makes four byte stores [verified:
  `~/v4-work/merge/ua/ua.s`]. So the condition is v3.9's again (`__mips__` only), with a comment
  saying why. The board's `i386.c` code is then v3.9's but for the merge's real changes.
  [Corrected in v4.1, from the two reviews of v4.0 (`REVIEW-RESOLUTION.md` round 6, EV M3 and BP 4):
  that comparison was compiled with plain rv32gc, whose objects say `unaligned_access, 0`, not with
  the board build's own flags (`-march=rv32imafcb_zicsr_zifencei_zcb_zcmp_zcmt_xesploop_xespv
  -mabi=ilp32f -mtune=esp32s31`, `unaligned_access, 1`). With those, both forms make one `lhu` or
  `lw` for a load and one `sw` for a store at any address, `unaligned.h`'s store behind an alignment
  test whose two arms are both that one `sw` [verified: the same `ua.c`, WSL `~/v4-work/v41/ua`]. The
  S31's cores do misaligned accesses themselves [inferred: ESP-IDF has no handler for the trap, and
  the board runs Windows 95]. So there is no speed reason either way; keeping v3.9's accessors, which
  the board has run, stays the safe choice, and the `i386.c` comment now says this
  (`BUILD-NOTES.md` 23.4).]
* **`i8254.c`, no conflict.** The cap (`i8254_max_owed`, `i8254_owed_max`, `i8254_dropped`, set
  from `[esp] timer_catchup`) sits in the branch for modes 2 and 3, which upstream changed only by
  switching on `pit_mode()` (6 and 7 as 2 and 3); upstream's one-shot modes are a new branch
  beside it, and `pit_load_count()` still zeroes `last_irq_count`, from which the cap counts.
  Test 19's new part (5.2): one interrupt in mode 0 and 4, held up or not, none counted as dropped;
  none in 1 and 5; 6 and 7 as 2 and 3 with the cap (a 200 ms hold at 1 kHz gives one tick and 199
  dropped); `i8254_max_owed` 0, which `timer_catchup = 0` sets, still makes up every tick; mode 2 at
  the reset count after them as before [verified].
* **`adlib.c`, no conflict.** v3.9's `AUDIO_RATE` (48 kHz on the DOS board) and upstream's
  `OPLGenerate` and `free(s)` together; with `USE_FMOPL`, as both boards build, `OPLGenerate` is
  `YM3812UpdateOne` as before [verified: source; `adlib_new` 2 bytes longer, 5.1].
* **`kvm.c`, `Makefile`, `android/`, `rawdraw/`, `adlib_nuked_opl3.h`**: taken as upstream has them;
  none is in a board build [verified: the build logs].


## 5. Checks

### 5.1 Builds, warnings, sizes [verified: `~/v4-work/merge/builds/`, `warn.out`, `elf1.out`, `elf2.out`]

In-tree ESP-IDF builds of the worktree at `v4.0-base` (`b1.sh`, the same commands as v3.9's
`b1.sh` and `build.sh`, the package's `dependencies.lock` copied in, `make prepare` for the OPL
tables), ESP-IDF v6.1-dev-8142-g188e3e55bb:

| Build | Board | Exit | Warnings outside ESP-IDF and its components |
|-|-|-|-|
| `build-v4k` | Korvo-1, the dual-boot table | 0 | `i2s.c`'s old "`i2s_task` defined but not used" only, as v3.9 |
| `build-v4ks` | Korvo-1, `-DPARTITION_CSV=partitions.csv` (standalone) | 0 | the same one |
| `build-v4d` | DOS board | 0 | none, as v3.9 |
| `build-v4kc`, `build-v4dc` | each board with `-DCRASH_TEST=1` | 0 | as their release builds |

The tiny386-default set is the dual-boot `tiny386.bin` with another table, so it needs no build
of its own; `build-v4k`'s partition table is `dualboot\partition-table.bin` byte for byte. Each
`tiny386.bin` passes `esptool image-info` (valid checksum and hash, app version `v4.0-base`), and
each ELF passes `build.sh`'s check of the status LED's panic bit-bang (both functions laid out as
`status_bb.h` wants).

The core files compile with `-Wno-all` in the board build, so `i386.c`, `vga.c`, `i8254.c`,
`adlib.c`, `pc.c`, `pcspk.c` and `misc.c` were compiled again with each board build's own command
and `-Wall -Wextra`, v3.9's build beside v4.0-base's (`warn.py`): `i386.c` 34 warnings in both,
`vga.c` 3, `pc.c` 1, the rest none, on both boards, and not one new kind. The PC harness builds of
the merged tree with upstream's own flags (`-Wall`) and the Linux route's give no warning either.

Sizes, against v3.9's release ELF files in `images\`:

| | Korvo-1 v4.0-base | Korvo-1 v3.9 | DOS board v4.0-base | DOS board v3.9 |
|-|-|-|-|-|
| `tiny386.bin` | 899,264 | 899,248 | 930,960 | 930,960 |
| Flash `.text` | 434,600 | 434,594 | 431,516 | 431,514 |
| Flash `.rodata` | 233,240 | 233,240 | 264,588 | 264,588 |
| Internal RAM used by code and static data | 249,710 | 249,710 | 252,708 | 252,708 |
| Internal RAM left | 254,034 | 254,034 | 251,036 | 251,036 |

The functions whose size changed: `i8254_update_irq` 248 to 324 bytes (the one-shot branch),
`i8254_ioport_write` 632 to 542, `i8254_ioport_read` 152 to 192, `task_switch` 2,086 to 2,090 (the
flags' mask), `vga_graphic_refresh` 4,458 to 4,386, `adlib_new` 112 to 114, and a few small PIT
helpers; `cpui386_step`, `deliver`, `vga_refresh` and every function in internal RAM are the size
they were. `i8254_update_irq` runs once a `pc_step()` (512 instructions) [verified: `pc.c`], so a
few more instructions there cost nothing measurable [inferred]. These are in-tree builds; the
v4.0 package's own builds from nothing are the next step's.

### 5.2 Host tests [verified: `~/v4-work/merge/host-tests-tests3.log`, `host-tests-from-patch.log`]

`tests/host/run.sh` at the merge commit, before any new case: 32 of 32, the host build's 11
warnings v3.9's same lines. With the new cases (e8afd4b): 33 of 33, the same 11 warnings.

* Test 19 (`test_pit.c`): 28 checks (v3.9: 16), the 12 new ones in 4.5's list. A new run builds the
  same test against v3.9's `i8254.c`, which has to pass all 13 checks before the one-shot modes and
  then stop at mode 0: it does, with `abort()`. The test now catches `abort()` and ends with its
  status (134) and no core dump. (One run before that catch was added, on this machine, did end in
  a core dump, which WSL's own crash handler, `/wsl-capture-crash`, takes; where it keeps them was
  not looked at.)
* `test_vga.c`: VBE 640x480x16 shown from line 400 of 1 MiB, over a screen of something else:
  rows 0 to 418 drawn from video memory, row 419 its first 128 pixels and then black, rows 420 to
  479 black. Against v3.9's `vga.c` the last check fails and the others pass [verified:
  `vga39.out`].
* Test 20 (the 386 cases: a double fault through a task gate with its error code, now written at
  the new task's CPL) passes unchanged.

In a tree made the way the next `build.sh` would make it (an export of `upstream-20261022`, tagged
`pristine` there, `git apply` of the patch): 29 of 29, the four runs that need the `v3`, `v3.1`,
`v3.5` and `v3.9` tags left out. With `pristine` at 4ce2e06 the tests that must see a bug in the
pristine file still do: upstream's `i8042.c` is 9ed786b's, and its `i8254.c` still raises every
missed tick back to back [verified].

### 5.3 Windows 95 OSR2 on the PC [verified: WSL `~/v4-work/merge/pc/runs-all`, `cmp1.out`, `tab.out`]

The emulator review's harness as the v3.9 check used it (a copy of `~/win95-debug/fix-v39/pc/harness`
in `~/v4-work/merge/pc/harness`, with one addition, `-marks`, used only for BENCH in 5.6), the
board's core flags (the `esp` flavour, 64-bit), the card's `bios.bin` and `vgabios.bin`, a fresh
copy of the pristine image or of a card image for every run. Both trees built by the same harness
from `git archive` of `v3.9` and of b370a75 (`v4.0-base`). 27 configurations, each run on both:
v3.9's eleven (`BUILD-NOTES.md` 21.6) and sixteen more from `WIN95-PC-REPRO.md`: the fast disk at
4M/256K, 13M/1M and 14M/320K in repro.sh's three timings (a: 2.0 M instructions a second, idle 16;
b: 2.2 M, 12; c: 1.8 M, 24), the fragmented card (`rand16`, 1 ms a read, 3 ms a write) and the
stall model (100 ms a disk access) in all three timings, and the card in one piece, in 4 KiB pieces
(`rand1`), the fragmented card at 4M and the stall model at 14M. 54 runs, 109 s on this PC, six at
a time.

| Run | Model | Desktop, v3.9 | Desktop, v4.0-base | Instructions at the end | Timer: most owed, dropped | Longest card access |
|-|-|-|-|-|-|-|
| t-fast-14M-a | 14M/320K, no hold | 211 s | 211 s | 588,301,033 | 1, 0 | |
| t-stall100-4M-a | 4M/256K, 100 ms a disk access | 8,720 s | 8,720 s | 526,774,571 | 51, 508,405 | |
| t-boardfrag-14M-a | 14M/320K, `rand16` | 266 s | 266 s | 588,808,164 | 233, 232 | 3.0 ms |
| t-spike5-4M-a | 4M/256K, every 5th card write 250 ms late | 1,148 s | 1,148 s | 480,243,730 | 19, 69,692 | 250.3 ms |
| n-stall100-4M-mouse | 4M/256K, holds, a mouse report every 10 ms | 8,622 s | 8,622 s | 607,229,632 | 51, 499,643 | |
| n-boardfrag-14M-mouse | 14M/320K, `rand16`, the mouse | 285 s | 285 s | 659,421,359 | 233, 232 | 3.0 ms |
| n-slow1M-fast-14M | 14M/320K, 1 M a second | 436 s | 436 s | 553,213,566 | 1, 0 | |
| n-slow1M-stall100-14M | 14M/320K, holds, 1 M a second | 3,583 s | 3,583 s | 470,846,446 | 52, 172,127 | |
| n-bootlog-boardfrag-14M | 14M/320K, `rand16`, a logged start | 278 s | 278 s | 596,547,400 | 233, 232 | 3.0 ms |
| n-stall100-13M-1M | 13M/1M, holds | 3,088 s | 3,088 s | 447,553,237 | 51, 156,188 | |
| n-boardfrag-13M-1M | 13M/1M, `rand16` | 265 s | 265 s | 588,274,546 | 233, 232 | 3.0 ms |
| x-fast-4M-a, b, c | 4M/256K, no hold | 225, 202, 248 s | 225, 202, 248 s | 651,550,502; 640,882,829; 646,580,315 | 1, 0 | |
| x-fast-13M-a, b, c | 13M/1M, no hold | 211, 190, 234 s | 211, 190, 234 s | 585,306,514; 675,659,669; 586,637,311 | 1, 0 | |
| x-fast-14M-b, c | 14M/320K, no hold | 191, 234 s | 191, 234 s | 583,964,097; 587,272,513 | 1, 0 | |
| x-boardfrag-14M-b, c | 14M/320K, `rand16` | 246, 290 s | 246, 290 s | 589,461,682; 591,021,164 | 233, 232 | 3.0 ms |
| x-stall100-4M-b, c | 4M/256K, holds | 8,607, 8,855 s | 8,607, 8,855 s | 524,656,227; 526,359,309 | 51; 502,764 and 515,180 | |
| x-stall100-14M-a | 14M/320K, holds | 3,126 s | 3,126 s | 452,358,143 | 51, 158,465 | |
| x-board-14M-a | 14M/320K, the card in one piece, 300 us a read | 225 s | 225 s | 588,311,873 | 4, 3 | 0.9 ms |
| x-boardrand1-14M-a | 14M/320K, 4 KiB pieces in random order | 284 s | 284 s | 585,793,513 | 1,117, 1,116 | 0.9 ms |
| x-boardfrag-4M-a | 4M/256K, `rand16` | 353 s | 353 s | 594,351,460 | 233, 232 | 3.0 ms |

54 of 54 at the desktop. For every configuration the two trees end with the same `END` line (the
virtual time, the instruction count, the registers), the same timer and card figures, the same
log, and a final picture that is the same file, byte for byte (the logs differ only in the host's
wall clock on the harness's own lines). The eleven v3.9 runs also end exactly as the v3.9 check's
own runs did (`~/win95-debug/fix-v39/pc/runs-v39`), so this copy of the harness is faithful. I
looked at three final pictures (a fast run in IE 4's first-run setup, a stall run's bare desktop,
the 4 KiB card's): the Windows 95 desktop with its icons, the Start button and a clock, no error
box. Pictures of Windows stay off the package.

The 4 KiB card's first minute is the image's cluster map being made (1,116 ticks dropped at once
at the start), in v3.9 as in v4.0-base.

### 5.4 Why nothing moved [verified: `runs-probe`, `probe.out`]

A probe copy of the `v4.0-base` tree (`probe.py`) counts, into a file of its own so that nothing
reaches the emulator's log or the harness's charged UART time, each path the merge changed. Six
runs (fast at 14M, 13M and 4M, the stall model at 4M, the fragmented card, the logged start) each
end with the same `END` line as the plain build's, and count:

| Path | Count in every Windows 95 run | In FreeDOS with BENCH |
|-|-|-|
| near JMP or Jcc whose operand size differs from its address size (d81e225 changes only these) | 0 (of 7.2 to 12.8 million near jumps) | 0 (of 197,761) |
| task switches, by JMP, CALL or IRET (0642735's flags) | 0 | 0 |
| exceptions or interrupts through a task gate (the error code, e2f9e9e) | 0 | 0 |
| PIT channel 0 programmed in mode 0, 1, 4, 5, 6 or 7 (the one-shot modes) | 0; mode 2 twice | 0; mode 2 once |
| lines of a picture that begin past the end of video memory | 0 | 0 |
| pictures in a VBE mode | 0 | 0 |

So these guests run the same instructions on both trees and meet the same interrupts at the same
moments, and the counts and times cannot move. What did change in the core is real but off the
path these boots take: it matters for a DOS program that uses the PIT's one-shot modes, for code
that jumps with a 66h prefix, for guests that switch tasks in hardware, and for a VBE picture
scrolled past the end of video memory. Windows 95 stays in its standard VGA driver here (mode
12h); a VBE driver, the next step's subject, will exercise the VBE paths.

### 5.5 Upstream's flags and the Linux route's [verified: `runs-flav`, `fl.out`]

The same two trees built with the harness's `upstream` flavour (tiny386's own Makefile flags, 32
bits a pixel, so without the deferred clear: the one place 4.4 changes behaviour) and its `linux`
flavour: Windows 95 at 14M/320K with no hold and at 4M/256K with 20 ms holds, 8 runs, all at the
desktop, each pair with the same `END` line and the same final picture.

### 5.6 FreeDOS and BENCH.COM [verified: `runs-bench`, `bench.out`, `perf.out`]

FreeDOS 4M, gen 4, no FPU, the emulator review's image (the Linux route's test disk with
`FDAUTO.BAT` running `bench`, copied), 250 M instructions, with `-marks`: the harness prints each
change of the byte at 0000:04F0, BENCH's marks, which the board's `speed.c` reads.

| Mark | v3.9 at instruction | v4.0-base at instruction |
|-|-|-|
| a1 | 11,790,158 | 11,790,158 |
| a2 | 13,887,310 | 13,887,310 |
| a3 | 15,657,294 | 15,657,294 |
| a4 | 18,213,198 | 18,213,198 |
| a5 | 18,235,219 | 18,235,219 |
| a6 | 23,949,062 | 23,949,062 |
| a7 | 25,948,934 | 25,948,934 |
| a8 | 27,718,406 | 27,718,406 |
| a9 | 30,318,854 | 30,318,854 |
| af (done) | 31,345,926 | 31,345,926 |

Both end in BENCH's final loop at 0008:00022e63 with the same registers, the same text screen and
the same picture. Speed, the two builds alternating, nothing else of mine running: the first
60 M instructions (boot and BENCH) took 294 to 319 ms on v3.9 (mean 303) and 296 to 309 ms on
v4.0-base (mean 303) in seven runs each, BENCH's own stretch (a1 to af) 121 to 139 ms (mean 127)
and 122 to 128 ms (mean 125); Windows 95's first 600 M instructions 4.8 to 4.9 s on each, but for
one v4.0-base run of 9.4 s that the next two did not repeat (the PC was busy elsewhere). No
measurable difference on x86-64; on the board the changed code is off BENCH's path (5.4) and the
same size where it runs (5.1), so none is expected there either [inferred].

### 5.7 The patch against the new base [verified: `tag.out`]

`git diff upstream-20261022 v4.0-base` (`~/v4-work/merge/tiny386-idf-v4.0-base.patch`): 141 files,
26,243 lines added and 719 taken out, 1,036,380 bytes, SHA-256 8978e351... (v3.9's against
9ed786b: 141 files, 26,128 and 709). Outside `esp/` and `tests/`: the same 25 files as v3.9's.
Applied with `git apply` (index and files) to an export of `upstream-20261022` it gives tree
b328e3f3, the `v4.0-base` tree exactly; the files at mode 755 are the ones that were already so
in v3.9 and upstream. Not put in the package: the v4.0 package's patch is the next step's.

### 5.8 The Windows 95 core patch for upstream [verified: `core.out`]

`research\win95-core-v3.9.patch` no longer applies to upstream master: it fails in `i386.c` at the
task gate, which upstream changed itself (0642735). That patch is the other session's subject; the
form that fits upstream now is the same files' part of `git diff upstream-20261022 v4.0-base`.


## 6. Found on the way, for the display work

* **Every VBE offset write clears the panel.** `vbe_write()` calls `vbe_update_vgaregs()` for a
  write to the X or Y offset (and the resolution, depth and virtual size), and that asks the
  drawing side for a full clear (`force_clear_req`) every time [verified: `vga.c`; the new
  `test_vga.c` case has to draw once after its offset write to see past it]. A display driver that
  pans or flips pages by the offset registers would get a black flash and a whole panel stored
  again for each; whether a Windows 9x VBE or Bochs driver does that is [unknown]. The deferred
  clear's rule (left out when the picture keeps its size and place) would fit an offset change.
* **Nuked OPL3** is now an option upstream (`USE_NUKED_OPL3`); the boards still build fmopl.
  Whether the DOS board's core 0 could run it beside the panel is [unknown].
* Upstream draws no clear at all on CRT controller writes since 43b4b85; the boards keep v3.6's
  deferred clear (4.4).


## 7. For the next step (v4.0: the display work, then the package)

* Start from `v4.0-base` (branch `v4`, b370a75), in the v3 tree (`git checkout v4`; `integ` stays
  at v3.9) or in the worktree `~/v4-work/merge/tree`, which has `fmopl.inc`, the lock file and
  `esp/managed_components` in place and five build folders.
* `build.sh`: `TINY_COMMIT` becomes 4ce2e06ccbaef6af2485fd31ae7c2e44856e7764 and the patch `git diff
  upstream-20261022 v4.0`; its `git tag pristine` then names 4ce2e06 in the clone, which the host
  tests accept (5.2). The tag `pristine` in the v3 repository still means 9ed786b, which the older
  notes and `previous-v3.x\` need.
* `esp/version.txt` says `v4.0-base`; the package's commit sets `v4.0`.
* The PC harness with `-marks` is `~/v4-work/merge/pc/harness`; its lists (`lists/all.lst`) run
  v3.9 and v4.0-base side by side and take two minutes.


## 8. What only a board can say [unknown]

* Nothing in Windows 95's boot changed (5.4), so v3.9's board results should hold for v4.0-base
  [inferred]; a run on the board is still the proof.
* Whether a DOS game that uses the PIT's mode 0 now runs on the board (on v3.9 it would stop on the
  crash screen with `abort()`).
* Everything `BUILD-NOTES.md` 21.12 lists.


## 9. For Plat to decide

* Whether to keep `unaligned.h`'s accessors on the boards (as here, v3.9's) or take upstream's
  packed ones (one line); the loads are the same code, the packed stores slower (4.5). [Corrected in
  v4.1: with the board's own flags both forms are one load or store at any address, so nothing to
  decide on speed; v4.1 keeps v3.9's (4.5).]
* Whether a line of a VBE picture that begins past the end of video memory should be black (as
  here and upstream) or left as v3.9 left it.
* Nuked OPL3 on the DOS board: worth a measurement on the board, not done.


## 10. Scripts and logs

WSL `~/v4-work/merge/`: `s1.sh` to `s28.sh` (looks), `resolve.py`, `fix2.py`, `fix3.py` (the
resolutions), `mergemsg.txt` (the merge's message), `c2.sh`, `c3.sh` (the commits), `tests1.py`
to `tests3.py` (the new test cases), `ht1.sh` and `host-tests-*.log`, `b1.sh`, `b1.out` and
`builds/` (build logs, sizes, `image-info`, the bit-bang layout), `bbfunc.sh` (`build.sh`'s
layout check, copied), `warn.py`, `warn/`, `warn.out`, `elf1.sh`, `elf2.sh`, `elf1.out`,
`elf2.out`, `nm-*.txt`, `ua/` (the accessors' code), `pc/` (the harness copy, `marks.py`'s
addition, `trees/`, `bin/`, `lists/`, `runs-all`, `runs-probe`, `runs-flav`, `runs-bench`,
`runs-perf`), `mklist.sh`, `pcrun.sh`, `pcrun.out`, `runs-all-summary.txt`, `cmp1.sh`, `cmp1.out`,
`tab.sh`, `tab.out`, `probe.py`, `probe.sh`, `probe.out`, `bench.sh`, `bench.out`, `perf.sh`,
`perf.out`, `fl.sh`, `fl.out`, `vga39.sh`, `vga39.out`, `tag.sh`, `tag.out`, `frompatch/`,
`tiny386-idf-v4.0-base.patch`, `core.sh`, `core.out`, `corecheck/`, `det.sh`, `chk.sh`, `save.sh`.
The worktree `tree/` is the v3 repository's (section 3). Nothing was started that is still
running.
