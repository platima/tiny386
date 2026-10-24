# Windows 95 on the Korvo-1: the fix (v3.8, fix 1 of 2)

2026-10-22. What the three investigations found (`WIN95-PC-REPRO.md`, `WIN95-CRASH-DECODE.md`,
`WIN95-BAREMETAL-AUDIT.md`), what was decided from them, what was changed in the scratch tree
(WSL `~/tiny386-idf-scratch/v3/tiny386`, branch `integ`, commits `f8663e0` to `6714724` on top of
`v3.7`), and how it was checked. Nothing was flashed, no serial port was opened, and nothing of
v3.8 has run on a board. The final images are not built here: the next agent adds two features
and packages. `BUILD-NOTES.md` section 19 is the build record of the same work.

Tags: [verified] seen in a run, a build, a test or a named source line made for this note;
[inferred] follows from verified facts; [reported] someone else's figure, not checked at its
origin; [unknown] only the board can say.

## 0. In one screen

* **Plat's question: are UTC and local times being mixed up? No.** Every time involved is Perth
  time, +0800, and says so: the v2 app images were written at 13:19 on the 21st, the dual-boot
  bootloader at 16:02, the v3 tag at 18:20 (git `+0800`), and Plat's photo of the blue screen is
  18:29 the same day; Windows is on W. Australia Standard Time and WSL on Australia/Perth
  [verified, section 1]. So the build that reached the desktop and then blue-screened on the
  afternoon of the 21st was the v2 baseline, flashed before Plat left, as he said; and his crash
  report (b) fits the v3.0 app of the evening of the 21st, so "I definitely flashed it yesterday"
  fits too [reported: `WIN95-CRASH-DECODE.md` 1.3].
* **The cause.** tiny386's PIT (`i8254.c`, unchanged since upstream) made up every timer tick the
  emulator missed while it was held up, one after another, one every 512 guest instructions.
  Windows 95's VMM takes such a burst nested, each tick inside the last, until its ring 0 stack
  runs off its page. Then tiny386 stopped with "abort: -1" at `cpui386_step` (Plat's photo), or
  Windows itself stopped with a protection error, a VMM blue screen or a text screen. The board
  holds the emulator up far longer than a PC: ESP-IDF's FatFs, as these boards configured it,
  walked `win95.img`'s cluster chain from its start on every seek back, one card read per FAT
  sector, thousands of reads when the image is in pieces. Investigation 1 reproduced all four
  failures on the PC this way and matched the photo's stack: the same four return addresses, the
  same frame tail, SP on a page boundary with CR2 = SP less 4 [reported, verified there; worded so
  since v3.9: 16 of the 32 bytes of the photo's stack line match, not all of them, section 9];
  this note reproduced the abort again and removed every failure (section 4) [verified].
* **The fixes** (section 3):
  1. `i8254.c`: when more than one period is owed, the missed ticks are dropped and one is
     raised, on the counter's own phase, as a real 8254 and 8259 behave (`i8254_max_owed`, 1).
     The CMOS periodic interrupt does the same. This is the fix for the crashes.
  2. `esp/main/disk_image.c`: the card's disk images are opened with FatFs itself and their
     clusters mapped once (fast seek), so no guest access reads the FAT; a card error is got over
     by opening the image again and trying once more (FatFs had refused the image for the rest of
     the run after one); the guest's sectors go through one DMA-capable buffer in internal RAM.
     This is the fix for the long holds.
  3. `i386.c`: the 386's double fault, the stack put back when an interrupt from ring 3 faults on
     its way in, a clean shutdown line, page tables outside the guest's memory taken as not
     present, LLDT's checks, `read_desc` lines with CS:EIP and rate-limited. Correctness: not what
     saves Windows here. In the PC runs Windows 95 OSR2 had an interrupt gate on vector 8 when its
     stack ran out, so its double fault faults too and the PC shuts down, as a 386 would
     [verified, section 4.4].
  4. `[esp] uart_log = 0` (the default): while the PC runs its log stays off the UART, where each
     byte held the emulator's core for 87 us; Ctrl+Alt+F12 still shows it.
  5. Diagnostics: the card's longest wait, its errors got over and refused, and the timer ticks
     dropped, on Ctrl+Alt+F12, on the UART every 10 s (with `uart_log = 1`) and on the crash
     screen with the app's version; the image's pieces on the card at boot; `[esp] memtest = N`,
     a PSRAM test before the PC is built; `[esp] timer_catchup` and `[esp] fast_seek` to undo
     fixes 1 and 2 for a comparison.
* **The PC checks** (section 4): Windows 95 reached the desktop in every configuration that failed
  in the investigation, three timings each: 51 of 51 runs with v3.8 as built, from 100 ms and
  500 ms holds on every disk access to an image in 102,312 random pieces at 1 ms a card read, and
  every 5th card write 250 ms late. With the cap alone, 27 of 27 card runs reached the desktop
  (slowly); with the map alone the fragmented cards did, but the slow-write card still aborted
  with the board's signature; with neither, every failure came back as before. The core patch on
  the Linux route's tree and on upstream 9ed786b: 18 of 18 at the desktop, the 100 ms holds that
  aborted upstream included. The 32-bit build: 15 of 15.
* **On the board**, Plat should see (section 6): a boot console line giving the pieces of
  `win95.img` on the card; Windows 95 at 14M/320K reaching the desktop from a fresh copy; and on
  Ctrl+Alt+F12 the disk and timer lines. If it still fails, the crash screen's last lines now
  carry the version, the longest card wait and the ticks dropped.

## 1. The times (Plat's question)

| What | When (Perth, +0800) | Source |
|-|-|-|
| v2 app image, `tiny386-idf\v2\images\tiny386.bin` | 2026-10-21 13:19:18 | file time [verified] |
| v2 baseline commit `c2f415f` | 2026-10-21 15:05:57 +0800 | git [verified] |
| dual-boot bootloader, `v3\dualboot\bootloader.bin` | 2026-10-21 16:02:33 | file time [verified] |
| v3.0 dual-boot app built | 2026-10-21 18:05 | [reported: `WIN95-CRASH-DECODE.md` 1.2] |
| tag `v3` | 2026-10-21 18:20:20 +0800 | git [verified] |
| Plat's photo of the blue screen | 2026-10-21 18:29 | Plat [reported] |
| tag `v3.1` | 2026-10-21 19:52:38 +0800 | git [verified] |
| tag `v3.6` | 2026-10-22 14:22:06 +0800 | git [verified] |
| tag `v3.7` | 2026-10-22 18:14:22 +0800 | git [verified] |

Windows' zone is W. Australia Standard Time and WSL's `/etc/timezone` is Australia/Perth (`date`
prints AWST) [verified]. No time here is UTC. A v3 app tagged at 18:20 could not have been
flashed, booted Windows, been through first-run setup and blue-screened by 18:29 while Plat was
out; the app in the dual-boot slot that afternoon was the v2 baseline [inferred, as all three
investigations]. Crash report (b), whose addresses fit only the v3.0 app, ran on a v3.0 flashed on
the evening of the 21st: "yesterday" [reported]. One more thing the times cannot settle: v2
mounts only the card's first FAT partition, so its Windows run used another card or partition
than today's `/sdcard3` [reported: investigations 1 and 3].

## 2. What was decided, and why

| Candidate | Evidence | Decision |
|-|-|-|
| PIT catch-up burst after a hold, nesting VMM's timer interrupts until its ring 0 stack overflows | Reproduced on the PC with held-up disk access, card models and slow writes; the abort matches the photo in the stack's four return addresses (c003a6c4, c0039247, c003a6c4, c15200e8), the frame's tail and SP on a page boundary with CR2 = SP less 4, 16 of the photo's 32 stack bytes (corrected in v3.9: this said "byte for byte", section 9); `pit-cap.patch` removed all 24 failures; upstream, v2 and v3.7 fail alike [reported: investigation 1]. Here: v3.8 with the cap reaches the desktop in all 51 runs; with the cap set off the same aborts come back (section 4) [verified] | Root cause. Fixed at the root, in `i8254.c` (and the CMOS) |
| Long holds from FatFs walking the image's cluster chain on every seek back | FatFs without fast seek, the VFS mapping only read-only files [verified: `vfs_fat.c` 413 to 444, `ff.c`]; 120,000 to 1,000,000 extra FAT reads to the desktop by the audit's model [reported]; in this note's runs without the map up to 27 million FAT reads to the desktop (`rand1`) and single accesses of up to 89.5 s; with it, none once the map is made at open [verified] | Fixed at the root: `disk_image.c` maps the clusters; no guest access reads the FAT (host test, section 5) |
| One card error making FatFs refuse the image until RESET | Code path [reported: audit F1, verified here in a host test: one refused read, then every read refused, through stdio]; whether it happened on the board [unknown] | Fixed: the image opened again and the access tried once more; counted on Ctrl+Alt+F12 |
| No double fault in tiny386 | `cpui386_step` TRY1 at 5142 [verified]; Windows 95 has a task gate for vector 8 in VMM's code [reported: investigation 2]; but at the overflow in the PC runs vector 8 was an interrupt gate [verified, 4.4] | Implemented as a 386 has it (correct for any guest), but not counted on for Windows; a shutdown line says what vector 8 was |
| UART logging holding the emulator's core | 87 us a byte, a `read_desc` loop held to 256 lines a second [reported: audit 5] | Runtime UART off by default (Plat asked); `read_desc` rate-limited |
| PSRAM keeping wrong data (blue screen 0D, corruption) | No evidence either way [reported: audit candidate 4] | Not proven: `[esp] memtest` tests the pool before the PC is built |
| ESP-IDF reporting a timed-out PSRAM transfer as done | `sd_trans_sdmmc.c` 565 to 586 [reported: audit F4] | The IDF is read only here. With the map, FatFs reads its window (PSRAM) only while mapping at open; the guest's sectors go through internal RAM, which has no cache sync, so a timeout there is reported [inferred]. To report upstream |
| Page-table walks storing outside the guest's memory | `tlb_refill()` unbounded [verified] | Fixed: such a table is not present (a page fault) |
| A regression in the v3 work | None between v2 and v3.7 on the PC [reported: investigation 1] | Nothing to revert |

The v2 blue screen (fatal exception 0D at VMM+89DD, a `pop gs` in VMM's thread switch) and the
13M/14M `read_desc: sel 77bc` stall were not reproduced exactly by investigation 1; both fit a
corrupted ring 0 stack or thread state after an overflow into the page below [inferred]. They are
covered by the same fix if that reading is right, and by the new diagnostics if it is not.

## 3. The changes

### 3.1 The timer (`i8254.c`, `i8254.h`, `misc.c`)

`i8254_update_irq()` runs once a `pc_step()` and raises at most one IRQ 0. When it raises one it
now works out how many periods have ended since the last one raised (`owed`; the mark
`last_irq_count` is always a whole number of periods from the load). If `owed` is more than
`i8254_max_owed` (1 by default), the mark moves to one period before the latest boundary: one
tick is raised now, the rest are dropped and counted, and the next tick comes at the counter's
next wrap, so the IRQ stays in phase with the counter the guest reads. `i8254_max_owed = 0` is
tiny386 as it was. `i8254_owed_max` and `i8254_dropped` record what was seen. The CMOS periodic
interrupt (`cmos_update_irq()`) moves its next timeout to the latest boundary the same way. A real
8259 holds one request; ticks a processor is too busy to take are lost [inferred from the
hardware's design]. The guest's clock then runs slow while the emulator is held up, as an old PC's
does with interrupts off for long; Windows 95 took no harm from that in any run here [verified for
these runs].

### 3.2 The card's images (`esp/main/disk_image.c`, `disk_image.h`, `ide.c`, `storage.c`)

* `storage_fatfs_path()` turns a VFS path on a volume `storage.c` mounted into FatFs's own
  (`/sdcard3/win95.img` into `1:/win95.img`); `struct storage_volume` gains `drv`.
* `ide.c` on the board opens a disk or CD image with `disk_image_open()` when it is on such a
  volume; elsewhere (`/spiflash`) and for snapshots it keeps C stdio, unchanged.
* `disk_image_open()`: `f_open()`, then `f_lseek(CREATE_LINKMAP)` with 256 words in internal RAM
  (127 pieces); if the image is in more pieces, the exact size FatFs asks for, internal RAM up to
  4 KiB, else PSRAM; if there is no room the image is used without a map, as before, with a
  yellow line. FatFs allows fast seek on a file open for writing as long as it is not made longer
  [verified: `ff.c` `f_write`, `f_lseek` fast-seek branch clips at the size]; `disk_image.c`
  refuses any access past the end, as `ide.c` already did. `CONFIG_FATFS_USE_FASTSEEK=y` in
  `sdkconfig.korvo1` and `sdkconfig.dosboard` (which also lets ESP-IDF's VFS map files opened
  read only, with 64 words).
* Reads and writes: `f_lseek()` only when the place changes, then `f_read()` or `f_write()` of
  whole sectors through one 4 KiB buffer from `heap_caps_aligned_alloc(64, ...)` with
  `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`, so ESP-IDF's SD driver neither bounces nor syncs a cache
  for the guest's data [reported: the audit's reading of `sd_host_check_buffer_alignment()`;
  inferred for the board].
* An access FatFs refuses: the image is closed (FatFs's error flag goes with the file object),
  opened again, mapped again, and the access tried once more; only a second refusal reaches the
  guest. Counted as errors and as errors got over; the first four lines of each, then every
  hundredth, go to the console.
* At open the console says how many pieces the image is in on the card and how long the map took;
  more than 64 pieces is a yellow line suggesting a freshly formatted partition.
* `[esp] fast_seek = 0` opens the images without a map.

### 3.3 The CPU core (`i386.c`, `i386.h`)

* `deliver()` replaces the two `TRY1(call_isr(...))` in `cpui386_step()`: a fault while delivering
  is classed as the 80386 Programmer's Reference, table 9-3, has it (benign; contributory: #DE,
  #TS, #NP, #SS, #GP; page fault). Contributory on contributory, or contributory or #PF on #PF, is a
  double fault, vector 8, error code 0; otherwise the second fault is delivered. A contributory or
  page fault while delivering the double fault is a shutdown: a line "shutdown: ... vector 8 is
  ..." and `cpu_abort(cpu, -2)`, so the board stops on its crash screen as before, now saying what
  happened. A double fault prints a line too (the first 8, then every 256th). External interrupts
  get the same handling.
* `call_isr()` is now a wrapper of `call_isr1()` that puts back SS (its cached descriptor), ESP,
  EFLAGS and `sp_mask` when delivery fails: an interrupt from ring 3 or V86 mode has switched to
  the TSS's ring 0 stack by then (and cleared VM), and before v3.8 a fault there was taken with
  the ring 0 stack in place and the ring 3 one lost. The IDT reads and the V86 path's pushes now
  fault into the guest instead of aborting.
* A task gate's handler gets the error code pushed on its new stack (32-bit TSS), as a 386 does.
* `tlb_refill()` takes a page directory or table entry outside `phys_mem_size` (and, on the board,
  in the 0xA0000 to 0xBFFFF device window) as not present.
* LLDT takes the null selector or a present LDT descriptor in the GDT only; anything else is
  #GP(selector), as on a 386.
* `read_desc:` lines carry CS:EIP and a running count and are printed for the first 16, then every
  1024th.
* `cpu_abort_note`, if set, runs first in `cpu_abort()`.

### 3.4 The firmware (`esp_main.c`, `console.c`, `speed.c`)

| `[esp]` key | Default | What |
|-|-|-|
| `uart_log` | 0 | 1: the log goes to the UART too while the PC runs. 0: to the ring and the panel only (Ctrl+Alt+F12); a fatal message and ESP-IDF's crash report still go to the UART, and an emulator abort turns the UART back on first |
| `fast_seek` | 1 | 0: the disk images without their cluster map, as before v3.8 |
| `timer_catchup` | 1 | `i8254_max_owed`: 0 makes up every missed tick, as before v3.8 (said on the console) |
| `memtest` | 0 | N passes (up to 10) of a PSRAM test over the emulator's pool before the PC is built |

Each is a whole number checked like `led_brightness` (anything else keeps the default with a
yellow line). Ctrl+Alt+F12 now prints two more lines under "Speed:":

    Disk: 27083 reads, 9788 writes, longest 3 ms, 0 over 55 ms; card errors 0 (0 got over), 0 refused
    Timer: at most 232 ticks owed at once, 231 dropped (timer_catchup = 1)

(the figures of a PC run of `rand16` at 1 ms a read). The same two lines go to the UART every
10 s when `uart_log = 1`, and, with the app's version, first thing when the emulator aborts, so
that the crash screen's last lines show them.

## 4. The PC checks

The harness is investigation 1's, copied to WSL `~/win95-debug/fix/harness` with three options
added to `headless.c` (`-pit-owed N`, `-img-nofast`, `-img-stdio`) and `build.sh` compiling the
tree's `esp/main/disk_image.c` with ESP-IDF's own `ff.c` and the board's FatFs settings
(`diskfat.c` gives `storage_fatfs_path()`: `/card/X` is `0:/X`). So on a card model the fixed
tree's runs go through the very `disk_image.c` the board runs, on ESP-IDF's FatFs, with each card
read and write charged as guest time. Builds: the tree exported at `40a1ec6` (`trees/v38a`), the
board's core flags, 64-bit (`bin/v38a-esp-64`) and 32-bit with unsigned `char` (`bin/v38a-esp-rv`);
0 warnings. Every run starts from a fresh copy of the pristine image (or of a card laid out as
Plat's), runs to 120 virtual seconds after the desktop or 900 million instructions, and is classed
by `classify.py`. Timings a, b, c: 2.0, 2.2 and 1.8 million instructions a second.

### 4.1 v3.8 as built: every configuration that failed, three timings each

The models are investigation 1's. "hold" charges a fixed time for every read and write of a plain
image file; the card models put the image on a FAT32 partition laid out as Plat's (p3 WIN9X at
sector 532,480, 2,048 MiB, 4 KiB clusters) in one piece (`contig`), in 64 KiB pieces in order with
gaps (`frag16`) or in random order (`rand16`), or in 4 KiB clusters in random order (`rand1`),
each card read and write charged (`-card-us READ:WRITE`); `spike5` makes every 5th card write
250 ms late; `bufd` is ide.c's old stdio path over FatFs with glibc's buffered stream, the model
of investigation 1's runs-B and runs-E. "Before" is what investigation 1 recorded for upstream,
v2, v3.6 or v3.7 under the same model [reported]; "v3.8" is this note's [verified:
`~/win95-debug/fix/runs-full/summary.txt`].

| Model | Memory | Before v3.8 | v3.8, a b c | Desktop at (virtual s) | Timer: most owed, dropped | Longest card access |
|-|-|-|-|-|-|-|
| no hold (`fast`) | 4M/256K, 14M/320K | desktop | desktop x3 each | 191 to 248 | 1, 0 | none |
| hold 100 ms | 4M/256K | abort (upstream, v2, v3.6) | desktop x3 | 8,607 to 8,855 | 51, about 510,000 | (hold) |
| hold 100 ms | 14M/320K | abort | desktop x3 | 3,058 to 3,155 | 51, about 157,000 | (hold) |
| hold 500 ms | 4M/256K | abort (upstream), still at the logo (v2, v3.6) | desktop x3 | 40,647 to 40,848 | 255, about 2,700,000 | (hold) |
| hold 500 ms | 14M/320K | abort, logo | desktop x3 | 14,497 to 14,798 | 255, about 840,000 | (hold) |
| `frag16`, 1 ms a read | 14M/320K | blue screen, exception 0E in VMM (v3.6) | desktop x3 | 236 to 280 | 58, 57 | 3 ms |
| `rand16`, 1 ms (repro.sh `board-frag`) | 14M/320K | protection errors, a text-mode stall (v3.7, v2, upstream) | desktop x3 | 246 to 290 | 233, 232 | 3 ms |
| `rand16`, 1 ms | 13M/1M | not run | desktop x3 | 245 to 292 | 233, 232 | 3 ms |
| `rand16`, 1 ms | 8M/256K | not run | desktop x3 | 253 to 300 | 233, 232 | 3 ms |
| `rand16`, 1 ms | 4M/256K | not run | desktop x3 | 334 to 382 | 233, 232 | 3 ms |
| `rand1`, 100 us | 14M/320K | protection error (v3.6) | desktop x3 | 217 to 260 | 372, 371 | 0.3 ms |
| `rand1`, 300 us (repro.sh `board-rand1`) | 14M/320K | abort, the board's signature (v3.6) | desktop x3 | 266 to 308 | 1,117, 1,116 | 0.9 ms |
| `rand1`, 1 ms | 14M/320K | abort (v3.6) | desktop x3 | 438 to 483 | 3,724, 3,723 | 3 ms |
| `contig`, every 5th write 250 ms late | 4M/256K | abort (v3.6) | desktop x3 | 1,126 to 1,163 | 19, about 69,000 | 250 ms (about 4,000 over 55 ms) |
| `bufd`, 100 us | 4M/256K | abort (v2, v3.6) | desktop x3 | 3,327 to 3,486 | 22, about 150,000 | (stdio path) |
| `bufd`, 120 us | 4M/256K | abort (v2 x3, v3.6 x2 of 3) | desktop x3 | 3,939 to 4,076 | 26, about 190,000 | (stdio path) |

51 runs, 51 at the desktop, each run on for 120 virtual seconds after it with no blue screen,
protection error or abort [verified]. (Corrected in v3.9, after the emulator review of v3.8, F3:
this said "into first-run setup". In 30 of the 51, the `fast`, `frag16`, `rand16` and `rand1` runs,
Internet Explorer 4.0 Active Setup's window, first-run setup, came up in those 120 s; in the 21
`hold100`, `hold500`, `bufd` and `spike5` runs it had not come yet, the desktop still drawing in
some. The review's two longer runs, `n-stall100-14M-long` and `n-spike5-4M-long`, show Windows 95
Setup's window coming under holds with no fault [reported: the review, section 6].) In the card runs the large "most owed"
comes from the map being made before the PC starts (12.8 s for `rand16` at 1 ms a read, 204.5 s for
`rand1`), and is dropped in one go at the first step; after that the longest card access is a
few milliseconds, since no access reads the FAT. The `rand1` card needs a map of 102,312 pieces
(800 KiB), which the harness can give and the board cannot (section 7).

### 4.2 Each fix alone

The same binary, with the harness's options undoing one fix or both: `-img-nofast` opens the
card's image without its map (as `[esp] fast_seek = 0`), `-pit-owed 0` makes up every missed tick
(as `[esp] timer_catchup = 0`). The plain-file models (`fast`, `hold`) and `bufd` never use the
map, so they appear only where they mean something [verified: `~/win95-debug/fix/runs-pitonly`,
`runs-mapnopit`, `runs-dfonly`].

| Model (14M/320K unless named) | The cap, no map | The map, no cap | Neither (only the CPU changes) |
|-|-|-|-|
| `fast` 4M and 14M | | | desktop x6 |
| hold 100 ms, 4M and 14M | | | abort x6 |
| hold 500 ms, 4M and 14M | | | no desktop x6: Windows asked APM to switch the PC off |
| `frag16`, 1 ms | desktop x3, at 641 to 701 s; longest access 1.4 s | desktop x3 | no desktop x3: switched off the same way |
| `rand16`, 1 ms, at 14M, 13M, 8M | desktop x9, at 1,859 to 2,298 s; longest access 5.6 s | desktop x9 | protection error x2, text-mode stall x7 |
| `rand16`, 1 ms, 4M | desktop x3, at 7,502 to 7,920 s | desktop x3 | protection error x3 |
| `rand1`, 100 us | desktop x3, at 2,804 to 2,913 s; longest 9.0 s | desktop x3 | protection error x3 |
| `rand1`, 300 us | desktop x3, at 8,070 to 8,244 s; longest 26.9 s | desktop x3 | abort x3 |
| `rand1`, 1 ms | desktop x3, at 26,597 to 27,076 s; longest 89.5 s | desktop x3 | abort x3 |
| `contig`, every 5th write 250 ms, 4M | desktop x3 | **abort x3** | abort x3 |
| `bufd`, 100 and 120 us, 4M | | | abort x6 |

* **The cap alone** gets every model to the desktop, even with single card accesses of 89 seconds
  (27 of 27). The most ticks owed at once in these runs, 408, 652, 1,956 and 6,522, are the very
  backlogs investigation 1 measured before its failures [verified here; reported there]; the cap
  dropped them. Start-up takes far longer: `rand1` at 1 ms reached the desktop at about 27,000
  virtual seconds, against 456 with the map.
* **The map alone** removes the FAT walks, and with them the holds of a fragmented image (24 of 24
  at the desktop, no access over 3 ms), but not a card that is slow to write: every 5th write 250 ms
  late still aborts, three times of three, with the board's signature (the stack bytes
  `c4 a6 03 c0 47 92 03 c0 c4 a6 03 c0 e8 00 52 c1` of Plat's photo) and now the lines
  "double fault: #PF (error 2) while delivering #PF at 0028:c00039cc, ESP c0ecf000, CR2 c0eceffc"
  and "shutdown: ... vector 8 is an interrupt gate" [verified: `runs-mapnopit/spike5-4M-*`].
* **Neither** fails as v3.7 did, every abort now a double fault followed by a shutdown, vector 8
  an interrupt gate (21 of 21). The 500 ms runs end exactly as investigation 1's v3.6 runs did,
  "APM shutdown request" at virtual t = 4,147.998 s after 180,149,716 instructions at 4M, the same
  numbers to the instruction [verified: `runs-dfonly/hold500-4M-a` against
  `~/win95-debug/pc-repro/runs-C/st-v3.6-esp-64_4M_500000`]: until a fault in delivery, v3.8's
  CPU runs Windows exactly as v3.6's. So the CPU changes alone neither cause nor cure anything here.

So both fixes are needed for a board whose card is both fragmented and slow to write, and either
the cap alone, at the cost of a slow start, would have stopped the crashes.

### 4.3 The Linux route, upstream, the 32-bit build, the last commit

`research\win95-core-v3.8.patch` (the five core files) was applied to investigation 1's tree of
the Linux route (9ed786b with `tiny386-korvo.patch`) and to upstream 9ed786b, each built in its
own flavour (the Linux route's flags; upstream's own, FPU and SIMD on) with no warning [verified:
`~/win95-debug/fix/trees/linux-fix`, `pristine-fix`, `bin/*/build.txt`]. Investigation 1 had
upstream abort under 100 ms holds; it did not run the Linux route under holds [reported].

| Build | Model | Outcome | Desktop at | Most owed, dropped |
|-|-|-|-|-|
| Linux route + core patch | `fast`, 14M | desktop x3 | 191 to 236 s | 1, 0 |
| Linux route + core patch | hold 100 ms, 4M | desktop x3 | 8,303 to 8,453 s | 328, about 490,000 |
| Linux route + core patch | hold 100 ms, 14M | desktop x3 | 3,066 to 3,123 s | 328, about 158,000 |
| upstream + core patch | `fast`, 14M | desktop x3 | 195 to 236 s | 1, 0 |
| upstream + core patch | hold 100 ms, 4M | desktop x3 | 8,409 to 8,690 s | 590, about 497,000 |
| upstream + core patch | hold 100 ms, 14M | desktop x3 | 3,060 to 3,132 s | 466, about 158,000 |
| v3.8, 32-bit, unsigned `char` | `fast`, 14M | desktop x3 | 191 to 234 s | 1, 0 |
| v3.8, 32-bit, unsigned `char` | hold 100 ms, 4M | desktop x3 | 8,607 to 8,855 s | 51, about 510,000 |
| v3.8, 32-bit, unsigned `char`, large files | `rand16` 1 ms, 14M | desktop x3 | 246 to 290 s | 233, 232 |
| v3.8, 32-bit, unsigned `char`, large files | `rand1` 300 us, 14M | desktop x3 | 266 to 308 s | 1,117, 1,116 |
| v3.8, 32-bit, unsigned `char`, large files | every 5th write 250 ms, 4M | desktop x3 | 1,126 to 1,163 s | 19, about 69,000 |
| v3.8 at `6714724` (the last commit) | `fast` 14M, `rand16` 1 ms 14M (`board-frag`), hold 100 ms 4M (`stall100`) | desktop x9 | the same times as at `40a1ec6` | |

[verified: `runs-other`, `runs-rv`, `runs-final`]. The 32-bit build's card runs were made again
with `-D_FILE_OFFSET_BITS=64` (`bin/v38a-esp-rv-lfs`), since the harness's own card code could not
open the 2.3 GB card image with a 32-bit `off_t`; that is the harness's limit, not the emulator's.
Its figures are the 64-bit build's to the run.

### 4.4 What vector 8 was when Windows' stack ran out

With the cap off (`-pit-owed 0`) and 100 ms holds, v3.8 delivers the double fault and Windows'
vector 8 turns out to be an interrupt gate on the same ring 0 stack, so delivering the double
fault faults again: "shutdown: #PF (error 2) while delivering #DF at 0028:c00039f9, ESP c139c000,
CR2 c139bffc; vector 8 is an interrupt gate" (4M; at 14M the same at 0028:c0039edd) [verified:
`runs-smoke/stall100-*-df2`]. A 386 would shut down and the PC would reset. Investigation 2 found
the code in VMM that writes a task gate on vector 8 [reported]; it was not in force in this IDT
at this moment [verified], perhaps another VM's IDT or a debug-only path [unknown]. The double
fault therefore cannot save this Windows from the overflow; the cap prevents the overflow.

## 5. Host tests

`tests/host/run.sh` in the tree: 32 of 32 pass (v3.7: 29), the host build's 11 warnings the same
lines as v3.7's [verified: `~/win95-debug/fix/hostout/all.log`]. New or changed:

| Test | What it shows |
|-|-|
| 19, `test_pit.c` | after a 100-period hold, 1,000 steps with no time passing raise one tick (not 100), 99 dropped, the next tick one period on and within one step of the counter's wrap; 20 holds of 0.6 periods drop nothing; `i8254_max_owed` 4 makes up 3 and caps 40; 0 gives 30 back to back; a 1 kHz timer held 200 ms gives one; the CMOS at 1024 Hz held 500 ms gives one, then its usual rate. Against pristine `i8254.c` the 100 are there (the test sees the bug) |
| 20, `tests/host/cpu` | a guest in protected mode on `i386.c`, built with the Korvo-1's header and without: a push off a not-present stack page, a #GP whose delivery is a #GP, an interrupt whose delivery faults, and INT from ring 3 with the ring 0 stack missing each reach a double fault task through a task gate with error code 0 on its own stack, TR and back link right, and the main task saved at the faulting instruction (the ring 3 SS:ESP for the INT, which before v3.8 would have been the ring 0 one); vector 8 an interrupt gate, and CR3 past the guest's memory, each a clean `abort()`; LLDT of a code selector #GP(8), of an LDT and of 0 no fault. v3.7's `i386.c` stops at the first, "@ ... cpui386_step 5142, abort: -1", as the board did, and with the bad CR3 it reads outside its memory (SIGSEGV) |
| 14, `tests/host/disk` | each volume (FAT32 1 sector a cluster, FAT16 4) through `disk_image.c`: the old checks, then an image in 300 pieces read out of order, every sector right, 0 FAT reads, one card read a sector (without the map 1,553 FAT reads); one refused read and one refused write got over with the right data and no guest error; a card refusing everything fails the guest's read once, and when it answers again the next read is right. Through stdio, one refused read makes the next two fail too (the sticky error, the fault the test must see) |
| 13, `tests/host/storage` | `storage_fatfs_path()` on Plat's card, the four-partition card and the card without a table; FatFs finds what the path names; prefixes like `/sdcard30` and too small a buffer refused. The stub `sdkconfig.h` has the same FatFs lines as the v3.8 Korvo-1 build's |
| 2, `test_console.c` | with the UART quiet a line still goes to the ring and none reaches the UART; loud again, it does |

Under AddressSanitizer and UBSan (copies of the build commands, `~/win95-debug/fix/asan*.sh`): the
timer test 16 of 16, the CPU test 40 of 40 four times, the disk test in its four cases, all pass;
UBSan's only report is tiny386's own `CR0_PG (1<<31)`, a shift into the sign bit that v3.7 has the
same; LeakSanitizer reports the timer test's own 160 bytes (a PIT it never frees) [verified]. These
runs were made with address-space randomisation off (`setarch -R`): with it on, gcc 12's ASan on
this WSL kernel looped on "DEADLYSIGNAL" at start in two runs, which were stopped and their logs
deleted (a tool fault: the same binaries pass with it off) [verified that they pass; the cause
inferred].

`psram_test()` and `ini_whole()` cut out of `esp_main.c` as they are and run on the PC: a good
pool passes twice and is zeroed; a bit that flips during the read-back is found with its address
and bits; the ini values 2, 0, 11, -1, x, "1 ", "", 1000, 0x10 give what section 3.4 says
[verified: `~/win95-debug/fix/memtest.sh`].

## 6. On the board

### 6.1 What Plat should see

1. On the boot console, after the settings: `/sdcard3/win95.img: N pieces on the card, mapped in
   T ms: a seek reads no FAT.` A copy made onto a freshly formatted partition says 1 piece. More
   than 64 pieces is yellow; too many to map is yellow and says so.
2. "The PC is starting: ... While the PC runs the log stays off the UART ([esp] uart_log = 1 puts
   it there); a crash report goes there still."
3. Windows 95 from a fresh copy of the pristine image, at 14M/320K (and 13M/1M, 4M/256K), reaching
   the desktop and first-run setup without a protection error, blue screen or crash screen. Start-up
   should be quicker than before, since no guest access waits for FAT reads [inferred].
4. Ctrl+Alt+F12 at the desktop: the `Disk:` and `Timer:` lines. "longest" is the longest single
   card access in ms; "over 55 ms" how many held the emulator up for more than a timer tick;
   "card errors (got over)" errors that a second try fixed; "refused" ones the guest saw. "ticks
   dropped" is the number of timer ticks v3.8 did not make up; before v3.8 each of those would have
   reached Windows in a burst.
5. If it still fails on the crash screen: the last lines now include "tiny386 v3.8 stopped the PC.
   Disk: ... Timer: ...", and, for a stack overflow, "double fault: ..." and "shutdown: ... vector 8
   is ...". A `read_desc:` line now has CS:EIP. A photo of that screen says which of the causes
   above it was.

### 6.2 One run that settles the rest

* For the cause: the run of 6.1. If Windows then reaches the desktop, the PIT and the holds were it
  [inferred]. For a comparison, `timer_catchup = 0` in the ini should bring the failures back on
  the same card, with "ticks dropped 0" and a large "owed at once".
* For PSRAM: one boot with `[esp] memtest = 3` (about 3 s more): "every word right" in green, or
  a red line with the address and bits. Then remove the line.
* For the card: the `Disk:` line's errors and longest wait; a card with "over 55 ms" in the
  thousands is slow at writing (investigation 1's every-5th-write model gave 4,000).
* Which card and partition held `win95.img` for the v2 run on the 21st, and with what cluster size,
  is still a question for Plat (v2 could not have used `/sdcard3`).

## 7. Still open

* None of this has run on a board: the SD card's real timing, how many pieces Plat's copies are
  in, and what the counters show are [unknown].
* The v2 blue screen (0D at VMM+89DD) and the repeating `read_desc: sel 77bc` stall were not
  reproduced exactly on the PC (by investigation 1; nothing here changed that).
* The double fault is the 386's, but this Windows' vector 8 at the overflow is an interrupt gate,
  so it does not recover Windows (4.4); VMM's task gate path was not traced.
* ESP-IDF's masked timeout on PSRAM transfers (`sd_trans_sdmmc.c`) is still in the IDF, which is
  read only here; with the map it is reached only while mapping at open [inferred]. Worth an
  upstream report.
* A fragmented image costs two walks of its cluster chain at open (one to learn the map's size):
  6,395 pieces took 12,786 FAT reads (12.8 s at the model's 1 ms a read); 102,312 pieces 204,528
  (the map then needs 800 KiB, which the board's PSRAM cannot spare, so there the image runs
  without a map after one walk) [verified on the PC; board inferred].
* The Linux route's own patch and release were not changed here: `win95-core-v3.8.patch` beside
  this note is the core part (`i8254.c`, `i8254.h`, `misc.c`, `i386.c`, `i386.h`), which applies to
  the Linux route's tree and to upstream 9ed786b (section 4.3). Upstream tiny386 has the same PIT
  and the same missing double fault.
* BENCH.COM's speed with v3.8 was not measured; the cap costs a division per raised tick, the
  wrapper a few stores per interrupt, the page-table bound two compares per TLB refill [inferred].

## 8. Files

* Scratch tree commits: `f8663e0` (timer), `c5dd715` (CPU), `fcae04f` (card images), `938913b`
  (ini keys, diagnostics), `7fe503b` (host tests), `40a1ec6` (version v3.8), `c412ab0` (comments,
  a static buffer), `6714724` (`[esp] memtest`).
* `research\win95-core-v3.8.patch`: `git diff v3.7 c412ab0` for the five core files (SHA-256
  9f21620c...).
* `research\WIN95-FIX-runs.txt`: every PC run of section 4, one line each.
* WSL `~/win95-debug/fix`: `harness/` (investigation 1's, with the additions above), `trees/v38a`,
  `trees/linux-fix`, `trees/pristine-fix`, `bin/`, `lists/` (`mklists.sh`, `mkother.sh` make them),
  `runs-full`, `runs-pitonly`, `runs-mapnopit`, `runs-dfonly`, `runs-other`, `runs-rv`,
  `runs-final`, `runs-smoke`,
  `hostout/` (host tests, sanitizers), `memtest/`, `size-v38fix-*.txt`, the scripts (`s*.sh`,
  `b1.sh`, `b2.sh`, `m1.sh`, `summ.sh`, `asan.sh`, `memtest.sh`, `commit*.sh`).
* Board builds for the compile check only (not images to ship): `esp/build-v38fix-korvo1` and
  `esp/build-v38fix-dosboard` in the tree (git-excluded).

## 9. After v3.8 (v3.9, 2026-10-22)

Two reviews of v3.8 (`..\REVIEW-v38-emulator-and-windows.md`, `..\REVIEW-v38-board-safety-and-
features.md`; `..\REVIEW-RESOLUTION.md` round 5). What they changed here:

* **The match with the photo** (the emulator review, F2): sections 0 and 2 said the PC's abort
  matched the photo "byte for byte in the stack". It does not: the photo's stack line begins
  `51 ac 03 c0`, the PC's `60 b0 03 c0 8e 01 00 00 a9 04 00 00 6c ae 03 c0`. What matches is what
  `WIN95-PC-REPRO.md` 5 says: the four return addresses (c003a6c4, c0039247, c003a6c4, c15200e8,
  16 of the 32 bytes of the stack line), the last 16 bytes of `stkf`, and SP on a page boundary with
  CR2 = SP less 4 [verified: the review, against the logs]. That places the board's abort on the
  same nested-interrupt path in VMM and VPICD; it does not say which interrupt nested there on the
  board, which only the board's `Timer:` line can (`..\TEST-PLAN.md` 8e).
* **First-run setup** (F3): 4.1's "120 virtual seconds into first-run setup" is corrected there.
* **VFBACKUP** (F4): the review ran v3.7 on the fragmented-card model (`rand16`, 1 ms a read,
  14M/320K) with a logged start: it stopped on a text screen reading "While initializing device
  VFBACKUP:" and "Windows", back in real mode at f000:ff53, with no card pulled; its longest single
  access waited 6,393 ms and it read 1,897,863 FAT sectors. v3.8 on the same model and the same
  logged start reached the desktop at 278 s and wrote `BOOTLOG.TXT` (21,913 bytes) [reported: the
  review's runs `c37-bootlog-boardfrag-14M` and `n-bootlog-boardfrag-14M`; v3.9's run of the second,
  below, gives the same]. Plat saw "While initializing device VFBACKUP: VFBACKUP could not load
  VFD.VXD" on v3.6 and put it down to pulling the card; it is most likely the same failure as the
  others [inferred]. Both of the review's runs wrote `BOOTLOG.TXT`, so the timer does not explain
  "no `BOOTLOG.TXT`" on the board; the board audit's reading (the file is hidden and system, and
  shows only with those shown) still stands [inferred].
* **The core patch** (the board-safety review, L1 and L5): `cpu_abort_note`, which v3.8 added to
  `i386.c` and ran first in `cpu_abort()`, now runs at most once and before the line that names the
  cause (TRY1's `@ file function line`, `deliver()`'s `shutdown:`), so that on the board that line
  reaches the UART and comes after the counts of lines held back. A front end with no note, as the
  Linux route and upstream are, prints the same lines in the same order as before. Two comments
  changed with it (`lldt_check()`, `i8254_max_owed`). `win95-core-v3.9.patch` beside this note is
  v3.8's patch with that change (`git diff v3.7 v3.9` of `i386.c`, `i386.h`, `i8254.c`, `i8254.h`,
  and `git diff v3.7 c412ab0` of `misc.c`, the CMOS hunk alone, as before; SHA-256 7e04af75...). It
  applies to upstream 9ed786b, and to the Linux route's tree, where both variants pass their 26
  tests with the same 5 warnings as the release [verified: `..\BUILD-NOTES.md` 21.7].
  `win95-core-v3.8.patch` is kept as it was.
* **v3.9 on the PC** [verified: `..\BUILD-NOTES.md` 21.6]: the review's nine v3.8 runs again on the
  v3.9 tag, and two more at 13M with a 1 MiB VGA: 11 of 11 at the desktop; the nine give the
  review's desktop times to the second (211, 8,720, 266, 1,148, 8,622, 285, 436, 3,583 and 278 s)
  and the same instruction counts at their end; the logged start wrote `BOOTLOG.TXT`.
* **Still open**: as section 7, and the review's F1: on the board the failures are not yet tied to
  the timer. `..\TEST-PLAN.md` 8e now makes the run with `timer_catchup = 0` a needed step after a
  desktop, with the `Timer:` line photographed both times.

