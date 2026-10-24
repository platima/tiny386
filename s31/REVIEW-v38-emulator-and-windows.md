# Review of v3.8: the emulator and Windows 95

2026-10-22. An independent review of v3.8 (tag `v3.8` = `3bded9d` in WSL
`~/tiny386-idf-scratch/v3/tiny386`), through one lens: the emulator core and Windows 95. Is the
cause of the Windows 95 failures proven by the evidence, does the fix address it at the root,
could it break FreeDOS, BENCH or the Linux route that shares the core files, and does the PC
harness really show Windows reaching the desktop. I changed nothing in the tree or the package
except this file. Nothing was flashed, no serial port was opened, and every process I started
ended by itself.

Tags: [verified] seen in a run, a build or a named source line in this review; [inferred]
follows from verified facts; [reported] someone else's figure, not checked here; [unknown] only
the board can say.

## 0. In one screen

* **Plat's question (UTC or local time): no mix-up.** I checked it again myself: every time
  involved is Perth time, +0800, and the 18:29 blue screen on the 21st was the v2 baseline
  (section 1) [verified, except the photo's time, reported].
* **The cause is proven on the PC, not yet on the board.** On the PC the mechanism is shown
  beyond reasonable doubt: after the emulator is held up, tiny386's PIT raised every missed tick
  back to back and Windows 95 nested them until its ring 0 stack ran off its page; with the
  missed ticks dropped, every model reaches the desktop; with them made up again, the same models
  fail again (section 2) [verified]. For the board, the crash photo shows the same overflow on the
  same VMM path, but which interrupts nested there, and how long the board's emulator was held
  up, has not been measured [unknown]. v3.8's new Disk and Timer lines and `timer_catchup = 0`
  are the right way to close that; I would make the comparison a required step (finding F1).
* **The fix is at the root.** The PIT change makes tiny386 do what an 8254 and 8259 do (one
  request held, missed ticks lost) and touches nothing else: it drops ticks only when the
  emulator has not looked at the clock for more than a period (section 3) [verified by reading
  and by runs]. The cluster map removes the main source of the board's holds. I found no defect
  in `i8254.c`, the CMOS change or the `i386.c` changes.
* **Nothing broken that I could find.** The Linux route rebuilt from its own patch plus
  `research\win95-core-v3.8.patch`: same 5 warnings as its release build, its own tests 26 of 26
  in both variants, FreeDOS from disk and floppy and BENCH's nine marks among them (section 4)
  [verified]. On the PC with the board's core flags, v3.8 and v3.7 run FreeDOS with BENCH, and
  Windows to 600 M instructions, identically to the instruction and at the same speed [verified].
  Host tests 32 of 32 in my own clone of the tag [verified].
* **The pictures are real desktops.** I looked at the final picture of all 93 runs fix 1 counted
  as desktops, and at every one of the 816 dialogs the harness pressed Enter on, grouped by title
  bar: all desktops, and no error box among the dialogs (section 5) [verified].
* **My own runs on the final tag** (the feature round did not run Windows): 11 runs of v3.8 at
  the desktop, among them combinations nobody had run (holds with the mouse in use, a guest at
  half the speed, a logged boot, long runs into first-run setup under holds). And one new fact:
  v3.7 on the fragmented-card model, logged boot, stops with "While initializing device
  VFBACKUP:", the message Plat put down to pulling the card (section 6) [verified on the PC].
* **Findings:** no blocker, no major; 4 minor, 8 notes (section 7).

## 1. Plat's question: UTC or local time

| What | Time | How I checked |
|-|-|-|
| WSL zone | Australia/Perth, `date +%Z%z` gives AWST+0800 | `/etc/timezone` [verified] |
| Windows zone | W. Australia Standard Time | `Get-TimeZone` [verified] |
| v2 `tiny386-idf\v2\images\tiny386.bin` | 2026-10-21 13:19:18 +08:00 (05:19:18 UTC) | `LastWriteTime` and `LastWriteTimeUtc` [verified] |
| v2 baseline commit `c2f415f` | 2026-10-21 15:05:57 +0800 | git [verified] |
| `v3\dualboot\bootloader.bin` | 2026-10-21 16:02:33 +0800 | `stat` [verified] |
| commit tagged `v3` | 2026-10-21 18:20:20 +0800 | git [verified] |
| Plat's photo of the blue screen | 2026-10-21 18:29 | photo metadata [reported] |
| commit tagged `v3.1` | 2026-10-21 19:52:38 +0800 | git [verified] |

Everything is local Perth time and says so; the one UTC figure is the same instant as its local
twin. The tags are lightweight, so "tag v3 at 18:20" in the earlier notes is the time of the
commit the tag names; that does not change anything. Nine minutes after the v3 commit is too
short for a v3 app to have been built, flashed, booted into Windows, taken through first-run
setup and blue-screened while Plat was out, so the run he came home to was the v2 baseline
flashed in the afternoon [inferred]. Only a phone set to another zone could change this; a phone
writes its own local time into a photo.

## 2. Is the cause proven by the evidence?

### 2.1 What the PC shows, beyond reasonable doubt [verified]

* **The failure comes and goes with the PIT.** Fix 1's runs, which I reread: with neither fix,
  every model with holds fails (21 aborts, plus protection errors, text-mode stalls and APM
  power-offs); with the cap alone all 27 card runs reach the desktop; with the map alone the
  fragmented cards reach it but the slow-write card aborts 3 of 3. I counted the abort lines
  myself: all 24 aborts (21 in `runs-dfonly`, 3 in `runs-mapnopit`) print "double fault: #PF ...
  while delivering #PF" and then "shutdown: ... vector 8 is an interrupt gate".
* **v3.8 changes nothing in an undisturbed boot.** With no holds, v3.8 and v3.7 reach the desktop
  at the same virtual second in all six runs (4M: 225, 202, 248 s; 14M: 211, 191, 234 s), and in
  my own speed runs they reach the same state after exactly 600,000,104 instructions at virtual
  t = 302.919 s. So the fix does not hide the fault by changing Windows' path in general; it acts
  only when the emulator has been held up.
* **The crash on the PC is the board's crash.** In the photo and in investigation 1's v3.6 abort
  the stack holds the same four return addresses (c003a6c4, c0039247, c003a6c4, c15200e8), the
  last 16 bytes of `stkf` are the same (`a9 04 00 00 30 ea 00 c0 0e 00 00 00 0e 00 00 00`), SP sits
  on a page boundary with CR2 = SP less 4, and the v2 PC run's code at IP is the photo's
  instruction sequence (`pushf; mov eax,[eax]; mov ax,[eax+4]; cli; ...`). That places the board's
  abort in the same nested interrupt path in VMM and VPICD [verified against the logs].

### 2.2 What is not yet proven

* **The source of the nesting on the board.** The match above identifies the overflow and the
  code path, not which interrupt nested. On the PC it is IRQ 0 after a hold. On the board the
  holds are inferred from the FatFs code (no fast seek: the image's cluster chain walked from its
  start on every seek back) and from the UART cost, not measured [unknown]. The match is also
  not "byte for byte in the stack", as `WIN95-FIX.md` 2 and fix 1's summary put it: the photo's
  stack line begins `51 ac 03 c0`, the PC's `60 b0 03 c0 8e 01 00 00 a9 04 00 00 6c ae 03 c0`;
  16 of the 32 bytes match. Investigation 1's own table (`WIN95-PC-REPRO.md` 5) says it correctly
  (finding F2).
* **The v2 blue screen (0D at VMM+89DD) and the `read_desc: sel 77bc` stall** were not reproduced
  exactly by anyone; both fit an overflow [inferred, as fix 1 says].
* **What closes it:** on the board, the same card with `timer_catchup = 1` reaching the desktop
  with a large "dropped" on the Timer line, and with `timer_catchup = 0` failing again. In
  `TEST-PLAN.md` the second half is in 8e, "Once, each, if there is time" (finding F1).

### 2.3 A new piece of evidence: VFBACKUP [verified on the PC]

Plat pulled the card during a long boot and saw "While initializing device VFBACKUP: VFBACKUP
could not load VFD.VXD", which everyone took for a result of the pull. On the PC, v3.7 on the
fragmented-card model (`rand16`, 1 ms a card read, 14M/320K) with a logged boot (F8, item 2)
stops on a text screen that reads "While initializing device VFBACKUP:" and "Windows", with no
card pulled; the guest is back in real mode at f000:ff53 at 900 M instructions. Its longest
single access waited 6,393 ms and the run read 1,897,863 FAT sectors. v3.8 on the same model
and the same logged boot reaches the desktop at 278 s (run `c37-bootlog-boardfrag-14M` against
`n-bootlog-boardfrag-14M`). So Plat's VFBACKUP message is most likely the same failure, not the
pull [inferred]. Both runs wrote `BOOTLOG.TXT` (v3.7: 14,787 bytes, ending at "Init Success
esdi_506.pdr"; v3.8: 21,913 bytes) and made `BOOTLOG.PRV`, so the timer does not explain "no
`BOOTLOG.TXT`" on the board; the audit's reading (the file is hidden and system, and only shows
with those shown) still stands [inferred].

## 3. Does the fix address it at the root?

### 3.1 The timer (`i8254.c`, `misc.c`) [verified by reading]

* `last_irq_count` is always a whole number of periods from the load (0 at load, `+= count`
  each tick), so `owed = (d - last_irq_count) / count` counts whole periods since the last tick
  raised; the branch runs only when a tick is due, so owed is at least 1. With more than one
  owed, `mark = d - d % count - count` leaves exactly one tick to raise now, `dropped` grows by
  owed less 1, and after `last_irq_count += count` the next tick is due at the next boundary:
  the phase the guest reads from the counter is kept. The arithmetic stays inside 32 bits (d is
  at most about 2.56e9 before the existing reload at 2^31 us).
* **It acts only on holds.** While the guest runs, `pc_step()` calls `i8254_update_irq()` every
  512 instructions on the board (0.25 ms at 2 M a second), so a tick is raised to the PIC as
  soon as it is due, even with interrupts off in the guest (the PIC holds it, as before). Two
  periods can be owed only when the emulator has not looked at the clock for more than a period:
  a hold on the card or the UART, or a guest timer faster than one `pc_step()`. In every no-hold
  run here the Timer line reads "at most 1 owed, 0 dropped" [verified].
* **It is what the hardware does.** An 8259 holds one request per line; ticks the processor
  cannot take are lost. QEMU's own 8254 model schedules its next interrupt from the current time
  and so loses missed ticks the same way [inferred from QEMU's design; not checked here].
* The CMOS periodic interrupt: `irq_timeout` is aligned to `irq_period`, a power of 2, so
  `d & ~(irq_period - 1)` is the whole periods missed; correct.
* A deeper alternative would be a guest clock that stops while the emulator is held up, which
  would also keep the guest's time of day right. Not needed for Windows; noted (N5).

### 3.2 The card (`disk_image.c`)

The map removes the FAT walks, which are the large holds; the cap covers what is left (slow card
writes, `f_sync`, the UART). Both are needed for a card that is fragmented and slow to write, as
fix 1 found. One thing to keep in front of Plat: where the map does not fit (fix 1's `rand1`, an
image in 102,312 pieces, needs 800 KiB), the cap alone keeps Windows alive but start-up took
about 27,000 virtual seconds in the harness [reported: fix 1, `runs-pitonly`]; so the fresh
format of `TEST-PLAN.md` 8a is essential, not a nicety (N4). The rest of `disk_image.c` belongs
to the board audit's lens; from mine I note only that every flush runs on the emulator's own task
(`run_pc()` in `esp_main.c`), so the image is never touched from two tasks [verified].

### 3.3 The CPU (`i386.c`) [verified by reading; test 20]

* `deliver()` follows the 80386 table 9-3; at most four tries; a fault while delivering the
  double fault is a shutdown. Every failing return inside `call_isr1()` goes through `THROW`, so
  `excno` is always the second fault's (I checked `__call_isr_check_cs()` too).
* The `call_isr()` wrapper puts back SS (selector and cached descriptor), ESP, EFLAGS and
  `sp_mask`. That is enough: in each path every `TRY` comes before `refresh_flags()` and before
  any store to the guest, CPL changes only with CS in the `TRY1` at the end, and
  `task_switch()` uses `TRY1` throughout, so it can never come back half done.
* The page-table bound, the LLDT check and the task gate's error code are right for a 386, with
  two small inaccuracies (N6, N7).
* As fix 1 found and the logs confirm, none of this saves Windows 95 OSR2: its vector 8 was an
  interrupt gate on the same stack in all 24 aborts.

## 4. Could it break FreeDOS, BENCH or the Linux route?

### 4.1 The Linux route rebuilt with the v3.8 core [verified]

My copy of the Linux route's `build.sh` (`lrbuild.sh`: the same steps, flags and toolchain, with
tiny386 cloned from the local scratch repo instead of GitHub) into my own folders:

| Build | `tiny386-korvo` | SHA-256 | Warnings | Its own tests (`tests/run-tests.sh`) |
|-|-|-|-|-|
| baseline: 9ed786b + `tiny386-korvo.patch` | 587,412 bytes | dd34a04f... | 5 | 26 of 26 |
| + `win95-core-v3.8.patch` | 591,508 bytes | 867d40d8... | the same 5 lines | 26 of 26 |
| + the core patch, `VARIANT=ua` | 603,796 bytes (`-ua`) | 954ad26a... | the same 5 lines | 26 of 26 |

* The baseline is the release's `tiny386-korvo` byte for byte (SHA256SUMS: dd34a04f...), so the
  build is faithful.
* The Linux route's `i386.c`, `i386.h`, `i8254.c` and `i8254.h` are v3.7's byte for byte; after
  the core patch they are v3.8's byte for byte; `misc.c` gets the CMOS hunk only. Its `ide.c` and
  `misc.c` differ from v3's elsewhere, and v3.8's other changes to them (the disk light's
  counters) are for the board only, so the core patch is all that applies.
* The tests ran under qemu-riscv32-static: FreeDOS from the hard disk and from FreeDOS's boot
  floppy to a prompt, `ver`, `mem`, `dir`, "off"; BENCH's nine pieces timed by `-marks`; BENCH
  refusing 2M; the VGA modes read back off the fake frame buffer; the console guard after
  Ctrl+Alt+Backspace, SIGTERM and kill -9. BENCH's per-piece instruction counts agree with the
  baseline's to the emulator's 4,096-instruction look.
* One change to the test script, in my copy only: its one `pkill -9 -f` by pattern (line 205,
  reached only if a console guard outlives a case) became a line that records the PIDs, under
  Plat's rule against killing by name. It was never reached; no guard or qemu process was left.

### 4.2 FreeDOS, BENCH and speed with the board's core flags [verified]

The PC harness's `esp` flavour (the board's defines, `PC_STEP_COUNT` 512), v3.7 and v3.8 tags
built alike, run one after the other on a quiet machine:

| Workload | v3.7 | v3.8 |
|-|-|-|
| FreeDOS 4M, gen 4, booting and running BENCH.COM, 60 M instructions (BENCH done, in its final loop at 0008:00022e63, CR0 80000001, in both) | 330 to 363 ms, mean 345 | 332 to 355 ms, mean 341 |
| Windows 95, 14M/320K, the first 600 M instructions (virtual t = 302.919 s in both) | 5.5 to 5.6 s | 5.2 to 5.5 s |

No measurable cost on x86-64. On the board the new code runs only when an interrupt or exception
is delivered and on a TLB refill (two compares), so the cost there should be as small
[inferred]. BENCH cannot be touched by the cap: it masks IRQ 0 and 1 at the PIC before its first
piece (`bench.S`: `in al, 0x21; or al, 0x03; out 0x21, al`) and is timed by the emulator, not by
the guest's clock [verified].

### 4.3 What does change for a guest

* After a hold the guest's clock (DOS's time of day, Windows' tick count) is now behind real time
  by the hold, where before the burst caught it up. A DOS program that times itself with the BIOS
  tick across disk access reads less time (N5).
* LLDT is stricter: a selector in the LDT, or one that is not an LDT descriptor, is #GP. Correct
  for a 386; only a broken guest would notice.
* A fault while delivering an interrupt or exception is now delivered to the guest instead of
  stopping the emulator; programs that ran before never got there, so they cannot be broken by it.

## 5. Does the harness really show Windows at the desktop?

* **Final pictures.** I made contact sheets of `final.png` for all 51 runs of `runs-full`, the 24
  of `runs-other` that have one (the six 32-bit card runs without one are the harness's 32-bit
  file limit, run again in `runs-rv`), the 9 of `runs-rv` and the 9 of `runs-final`, and looked at
  every one: each is the Windows 95 desktop with My Computer, Network Neighborhood, Inbox, Recycle
  Bin, The Internet and Online Services, the Start button and a clock. In 30 of the 51 (`fast`,
  `frag16`, `rand16`, `rand1`) Internet Explorer 4.0 Active Setup's "Active Channel Selection" is up:
  first-run setup. The other 21 (`hold100`, `hold500`, `bufd`, `spike5`) stop with the desktop
  up and, in several, an hourglass and icons still being drawn; 120 virtual seconds at 100 to 500
  ms a disk access is not long enough for setup's window to come (finding F3).
* **The classifier** calls a picture "desktop" when over 20% is teal and over 70% of the bottom
  strip is the taskbar's grey; an error box on the desktop would still pass, so the pictures had
  to be looked at. They show no error box.
* **The dialogs the harness pressed Enter on.** 816 pictures (up to 8 a run) fall into 10 kinds of
  title bar: "Setting up Hardware" (398, and 46 in upstream's font), "Internet Explorer 4.0 Active
  Setup" (150 and 10), "Enter Network Password" (91 and 6), "Windows 95 Setup" (53 and 3), and two
  untitled kinds caught while being drawn (52 the size of "Setting up Hardware", 7 with the help
  and close buttons of the password box). No error dialog was pressed away [verified:
  `dlggroup.py`, `sheets/dialogs.png`].
* **The logs.** None of the 93 desktop runs logs a double fault, a shutdown, a `read_desc` line or
  an abort [verified].

## 6. My own runs

The v3.8 tag (`3bded9d`, with the feature round's changes, which no Windows run had used) and the
v3.7 tag, built by fix 1's harness (copied to my folder) with the board's core flags, 64-bit, each
run from a fresh copy of the pristine image or card. "a" timing: 2 M instructions a virtual second.

| Run | Build, model | Outcome | Desktop at | Timer: most owed, dropped | Longest card access |
|-|-|-|-|-|-|
| t-fast-14M-a | v3.8, no hold, 14M | desktop | 211 s | 1, 0 | |
| t-stall100-4M-a | v3.8, 100 ms a disk access, 4M | desktop | 8,720 s | 51, 508,405 | |
| t-boardfrag-14M-a | v3.8, `rand16` 1 ms, 14M | desktop | 266 s | 233, 232 | 3.0 ms |
| t-spike5-4M-a | v3.8, every 5th card write 250 ms late, 4M | desktop | 1,148 s | 19, 69,692 | 250.3 ms (4,048 over a tick) |
| n-stall100-4M-mouse | v3.8, 100 ms holds and a mouse report every 10 ms | desktop | 8,622 s | 51, 499,643 | |
| n-boardfrag-14M-mouse | v3.8, `rand16` 1 ms and the mouse | desktop | 285 s | 233, 232 | 3.0 ms |
| n-slow1M-fast-14M | v3.8, no hold, the guest at 1 M a second | desktop | 436 s | 1, 0 | |
| n-slow1M-stall100-14M | v3.8, 100 ms holds at 1 M a second | desktop | 3,583 s | 52, 172,127 | |
| n-bootlog-boardfrag-14M | v3.8, `rand16` 1 ms, logged boot | desktop, `BOOTLOG.TXT` 21,913 bytes | 278 s | 233, 232 | 3.0 ms |
| c37-bootlog-boardfrag-14M | v3.7, the same | "While initializing device VFBACKUP:", real mode | none | (no counters) | 6,393 ms |
| n-stall100-14M-long | v3.8, 100 ms holds, 1,500 s after the desktop | desktop, then Windows 95 Setup's window | 3,108 s | 51, 191,808 | |
| n-spike5-4M-long | v3.8, slow writes, 900 s after the desktop | desktop, then Windows 95 Setup's window | 1,145 s | 19, 90,970 | 250.3 ms |

* The first four give fix 1's times to the second (211, 8,720, 266, 1,148 s), so the feature
  round's changes leave the emulator's behaviour as it was [verified].
* The mouse, the slower guest and the logged boot change nothing for v3.8; the long runs show
  first-run setup coming up under holds with no fault. I looked at every final picture.
* 1 M a second is about half the board's measured speed; the cap still holds the most owed at 52
  under 100 ms holds, and the guest still reached the desktop with no drop when not held up.

## 7. Findings

| # | Severity | Where | Finding | Fix |
|-|-|-|-|-|
| F1 | minor | `TEST-PLAN.md` 8e | The only board test that ties the failures to the timer, `timer_catchup = 0` on the same card, is "once, if there is time". Without it a v3.8 success on a freshly formatted card cannot tell the cap from the map, the fresh format or `uart_log = 0`. | Make it a required step after 8b succeeds, and ask for a photo of the Timer line both times. |
| F2 | minor | `research\WIN95-FIX.md` 2 and 0, fix 1's summary | "matches the photo byte for byte in the stack" overstates: 16 of 32 bytes of the stack line match (the four return addresses), plus the last 16 bytes of `stkf`. | Say what investigation 1's table says: the same return addresses, the same frame tail, SP on a page boundary. |
| F3 | minor | `WIN95-FIX.md` 4.1, `BUILD-NOTES.md` 19.7 | "each run on for 120 virtual seconds into first-run setup with no blue screen" holds for 30 of 51; the 21 hold, `bufd` and `spike5` runs end at the desktop before setup's window comes. | Reword; my two long runs (section 6) show setup's window coming under holds, which could be cited. |
| F4 | minor | `TEST-PLAN.md` 8, research notes | The VFBACKUP message Plat saw after pulling the card is reproduced on the PC by v3.7 on the fragmented-card model with no pull (2.3). Nobody has it on record. | Tell Plat; add it to the research notes as one more face of the same failure. |
| N1 | note | `research\win95-core-v3.8.patch` | Applies to the Linux route and passes all 26 of its tests in both variants (4.1). The Linux route's release is unchanged. | Plat's call whether the Linux route and upstream take it. |
| N2 | note | the feature round | Neither round ran Windows on the final tag; my four standard runs on `3bded9d` give fix 1's figures to the second. | None now. |
| N3 | note | `i8254.c`, `esp_main.c` | `i8254_max_owed` N above 1 means "make up the missed ticks if no more than N are owed, otherwise raise one", not "at most N" (test 19 checks 40 owed with N = 4 gives 1). The console text "up to that many ... are made up" is right, the variable's name less so. | A comment, if anyone sets N above 1. |
| N4 | note | `disk_image.c`, `TEST-PLAN.md` 8a | An image too fragmented to map runs on the cap alone and starts very slowly (about 27,000 virtual s in fix 1's `rand1` 1 ms runs). | Keep the fresh format first in 8a, as it is. |
| N5 | note | `i8254.c`, `misc.c` | The guest's clock now loses the time of each hold (DOS time of day, Windows' ticks). Harmless for Windows and BENCH. | Worth one line in README for DOS users; a guest clock that stops during holds would be the fuller cure. |
| N6 | note | `i386.c` `lldt_check()` | The comment says anything but a present LDT descriptor is #GP; a not-present one is #NP (from `set_seg()`), which is what a 386 does. | Comment only. |
| N7 | note | `i386.c` `call_isr1()` task gate | The error code is always pushed as 32 bits, also for a 16-bit TSS. | Only if a 286-style guest ever needs it. |
| N8 | note | `i386.c` `deliver()`, `read_desc()` | The line budgets (first 8 double faults, first 16 `read_desc` lines) are static and not reset when the guest reboots. | Cosmetic. |

## 8. What I did, and did not do

* **Did:** read `i8254.c`, `misc.c`'s CMOS change, `i386.c`'s diff and `call_isr1()`,
  `__call_isr_check_cs()`, `set_seg()`, `task_switch()`, `disk_image.c` and the flush loop; read
  `WIN95-FIX.md`, `BUILD-NOTES.md` 19, `TEST-PLAN.md` 8 and investigation 1's comparison with the
  photo; reread fix 1's run logs and summaries; looked at 93 final pictures, 816 dialog pictures
  and my own 12 runs' pictures; rebuilt the Linux route three ways and ran its tests three times;
  ran the v3.8 host tests (32 of 32, 11 warnings as reported); ran 12 Windows runs and two rounds
  of speed runs (28 runs, the finer round in the table of 4.2) in fix 1's harness.
* **Did not:** run anything on a board; build board images; check `disk_image.c` against the
  SDMMC driver or the board's concurrency (other lenses); reproduce the v2 0D blue screen or the
  `sel 77bc` stall; measure BENCH on the board.
* **Where it is:** WSL `~/win95-debug/review-emulator-and-windows`: `lrbuild.sh`,
  `run-tests-review.sh` (the copy without `pkill`), `lr-base`, `lr-v38`, `lr-v38-ua` (builds and
  their `tests`), `lrtests-*.log`, `clone-v38` and `hosttests-v38.log`, `pc/` (the harness copy,
  `trees/v37tag`, `trees/v38tag`, `bin/`, `runs-review`, `runs-review2`, `runs-perf`), `sheets/`
  (contact sheets and `dialogs.png`; Windows pictures, kept off the package), `sheet.py`,
  `sheet2.py`, `dlggroup.py`, `perf.sh`, `perf2.sh`, `mylist.lst`, `mylist2.lst`. The pristine
  image was only read, through fix 1's read-only copy.
