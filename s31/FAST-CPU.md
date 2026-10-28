# The fast interpreter (v5.0)

v5.0 puts a fast interpreter in front of tiny386's own. It is Yejun Yang's, from his tab5-pc (a
PC emulated on the M5Stack Tab5, built on tiny386), ported to this tree: `i386_fast.h`, with a
few hooks in `i386.c`. It does the common instructions itself, much more cheaply, and hands
everything else to tiny386 one instruction at a time. The guest sees no difference: the same
registers, flags, memory, faults and interrupts, at the same instruction counts. Written
2026-10-27, on the branch `v50-fast` from `s31-v4.6`; brought up to date the same day after the two
reviews of v5.0 (section 11, and `BUILD-NOTES.md` 33.15).

Tags, on every claim:

* [verified] seen here: read in the file named, or measured by the method named
* [inferred] reasoned from verified facts; not run
* [unknown] only a board, or the person named, can say


## 1. In one screen

* **What it is.** tab5-pc's `t386_fast.h` (github.com/yejun/tab5-pc, commit `98a442e`, the one
  `research\JIT-OPTIONS-2026-10-24.md` measured: its clone is at that commit), its
  `cpui386_step` and its patch 0010 (a hook in `tlb_clear()`), on this tree's `i386.c`. tab5-pc's
  translator to RISC-V is not taken [verified].
* **The same guest as tiny386.** The three Windows 95 runs of the PC repro that v4.6 was checked
  with end on v4.6's instruction counts exactly (588,301,033, 1,304,341,904 and 1,337,723,352),
  with the same log line for line, every picture the same and the disk the same, with the fast
  interpreter on and with it off [verified, section 6]. A new host test runs the two side by side
  on random guest code and compares everything after every step; it found one difference in the
  port, now fixed. A review of v5.0 found a second, with a test of its own: code at privilege
  levels 1 and 2 could use a page that tiny386 would fault on (section 11); fixed, and none since
  in 9,520 runs of the wider test [verified, section 6].
* **Fewer host instructions.** Counted under qemu on the board's RV32 code (section 7), on what
  the board's core 1 does: BENCH without its string piece 134.6 host instructions a guest
  instruction with v4.6, 74.7 with the fast interpreter (1.8 times fewer); the first 300 M
  instructions of Windows 95's boot 155.3 and 81.0 (1.9 times fewer). On the CPU's own code
  alone (`i386.c`) the factor is 1.8 on BENCH and 2.0 on Windows [verified].
* **On the board** a guest instruction should take roughly half the time on CPU-bound work
  (Windows booting, applications computing), so about 1.8 times the instructions a second
  [inferred]. Since the reviews the frame buffer's pages have entries in its TLB too: an
  instruction reads the screen as it reads RAM, and a MOV or STOS stores there and sets the
  drawing's marks itself (section 8) [verified: code, tests]. How fast on the board is [unknown]
  until Plat runs it.
* **Where it runs.** On the Korvo-1 and the DOS board the fast interpreter's loop (120 KB) and the
  tiny386 helpers it calls most are in internal RAM; tiny386's `cpu_exec1()` (132 KB), which it
  hands about one instruction in 500 on Windows' boot and one in 40 in T386MON's tests at 16
  colours, moves to flash. Internal RAM left after code and static data: 241,444 bytes in v5.0 as
  released, against v4.6's 251,846 [verified: the build's size report, `BUILD-NOTES.md` 33.8;
  section 5's table has the `v50-fast` branch's own figures]. A build with `cpu_exec1()` in
  internal RAM too leaves about 113,000 [verified on `v50-fast`]; what ESP-IDF then still finds
  free at run time is [unknown] (section 5).
* **The switch.** `[esp] fast_cpu = 0` in the ini runs tiny386's own interpreter alone: the same
  guest as v4.6, but slower than v4.6, because tiny386's interpreter now runs from flash (section
  5); `1` (the default) runs the fast one. The boot console says which. Built without `I386_FAST`
  the firmware is v4.6's interpreter only, in internal RAM [verified].
* **For Plat:** the boot console's new `CPU:` line and its `Internal RAM free` line; BENCH and
  T386MON with the default `fast_cpu = 1`, against v4.6's own figures (section 9).


## 2. Where it came from

* **tab5-pc** is Yejun Yang's port of tiny386 to the M5Stack Tab5 (ESP32-P4), released on
  2026-10-22. Yejun (GitHub `yejun`) also wrote tiny386 PRs #40 to #42, which this branch has
  [verified: `research\JIT-OPTIONS-2026-10-24.md` 1.1].
* **The licence.** tab5-pc's own code is under the MIT licence, Copyright (c) 2026 Yejun Yang. The
  parts of `t386_fast.h` built on tiny386's internals, and the `cpui386_step` adapted from
  tiny386's, are Copyright (c) 2024-2025 Chunhui He under tiny386's BSD-3-Clause; his patches to
  tiny386 are under the licence of the file each changes [verified: tab5-pc's `LICENSE` and the
  file's header]. `i386_fast.h` keeps both notices and the MIT licence's text; the changes made
  here are under the same terms.
* **What was taken:** `t386_fast.h` (the decoder, the handlers, the TLB, the code window),
  its `cpui386_step` (here folded into `i386.c`'s own, which keeps this tree's double fault
  handling), patch 0010's hook in `tlb_clear()` [verified].
* **What was left out:** the translator (`t386_jit.h`, `t386_fpu_jit.h`) and its hooks on writes
  to guest RAM (patch 0005); tab5-pc's own writes and reads of VGA memory (`t386_vga_ext.h`),
  which skip this tree's dirty marks; its counters of instructions by opcode; its patch 0006
  (this tree has its own, v4.6's `CROSSES_PAGE`) [verified].


## 3. How it stays tiny386

tab5-pc's fast interpreter already computes every result with tiny386's own macros (`ADDw`,
`SHLd`, `DIVw`, ...), on tiny386's own CPU state and lazy flags, and hands tiny386 whatever it
does not do, before changing anything. For the count of instructions to be the same too, and
everything a device can see, the port does more [verified: read in `i386_fast.h` and `i386.c`;
the tests of section 6 check each]:

| what | tiny386 | tab5-pc | here |
|-|-|-|-|
| the instruction count (`cpu->cycle`) | one per instruction, at its first byte, as it goes | added at the end of each run, roughly | exact, and brought up to date before every port and MMIO access and every instruction handed over (the PIT and the clock read it) |
| the end of a step | after `stepcount` instructions; STI keeps the next one in, and ends the step after it with an interrupt waiting; MOV SS and POP SS keep the next one in; IRET and POPF end it with an interrupt waiting; HLT ends it | also left at a change of V86 mode, after far transfers and at any interrupt waiting after a handed-over instruction | the same as tiny386, nothing else; `cpu_exec1()` run for one instruction (`cpu->exec1_one`) reports STI, SS loads and the end of a step back (`STEP_HOLD`, `STEP_STOP`) instead of acting on them |
| CMP or TEST, then JZ or JNZ | a JZ or JNZ right after, if taken, is part of the CMP's step (`try_jcc8()`), when the CMP was fetched the quick way | not done | done where tiny386 does it |
| the instruction fetch | a cache of one linear page (`cpu->ifetch`), filled by `peek8()` when an instruction starts in another page | its own window | only in the page tiny386's cache holds, filled as `peek8()` fills it, and given back as it was when the first instruction of a page goes to tiny386 |
| the TLB | 256 entries (on the boards), refilled on a miss, setting the page tables' accessed bits; the dirty bit set on every store; a page's rights checked alike for levels 1 to 3 | 1,024 entries of its own a privilege level | one entry for each of tiny386's own, in two tables (level 0, and levels 1 to 3, as tiny386 checks rights), kept only while tiny386's holds the same page (`fast_tlb_slot()`); the dirty bit set on every store; tiny386's TLB and the page tables come out the same |
| video memory and MMIO | through `load8()` to `store32()` and the callbacks; v5.0's frame buffer path in them, with the drawing's dirty marks | partly straight into video memory | the VGA window through `load8()` to `store32()`; the frame buffer's pages through TLB entries (since the reviews): loads read video memory, MOV and STOS store and then set the marks as `store8()` to `store32()` do, everything else through tiny386's functions |
| a 16 or 32-bit access across a page | a byte at a time (v4.6) | its own | to tiny386 |
| 16-bit stack slots that wrap | each slot's offset masked on its own | one block | to tiny386 |
| INT in real mode | ESP's top half cleared | kept | cleared |
| BT with an immediate beyond the operand, in memory | the address moves by it | not moved | moved |
| ARPL in memory | read, then written through the read, without a write check | a write check | as tiny386 |
| MOV SS, POP SS in protected mode, failed segment loads | `set_seg()` | to tiny386 | `set_seg()`, the fault raised as tiny386 raises it |

Interrupts and exceptions are taken at the same instruction boundaries as tiny386's, with
`cpu->ip` and `cpu->next_ip` as tiny386 leaves them [verified: section 6, which compares both
after every step]. There is no known place where they differ.


## 4. The switch

* `[esp] fast_cpu` in the ini: 1 (the default) the fast interpreter, 0 tiny386's own alone. Read
  at boot; the console says `CPU: the fast interpreter (from tab5-pc, by Yejun Yang) in front of
  tiny386's own` or `CPU: tiny386's own interpreter alone (fast_cpu = 0): the same guest as v4.6,
  but slower, since it now runs from flash` [verified: `esp_main.c`]. So `fast_cpu = 0` is the
  check that the guest behaves the same, not a measure of v4.6's speed (section 5).
* `I386_FAST` (compile time) puts it in the build: defined in `board_korvo1.h` and
  `board_dosboard.h`. Without it, `i386.c` compiles to v4.6's code [verified: the object's
  `cpu_exec1()` the same size as v4.6's].
* `I386_FAST_DEFAULT` (compile time, 1 if not given) is whether a new CPU runs it; for a PC build
  that should start with tiny386's own, `-DI386_FAST_DEFAULT=0`.
* `cpui386_set_fast(cpu, on)` and `cpui386_get_fast(cpu)` (`i386.h`) change and read it for one
  CPU, at any step boundary: a lockstep tester can run one machine each way.
* `I386_FAST_EXEC1_IRAM` (compile time, in the board header) keeps `cpu_exec1()` in internal RAM
  as well (section 5).


## 5. Where it runs on the S31

The Korvo-1 dual-boot app, `idf.py -B build-korvo1 -DBOARD=korvo1`, the boards' ESP-IDF
(v6.1-dev-8142-g188e3e55bb), built clean [verified: the size reports, `nm`]:

| | v4.6 | v5.0 (default) | v5.0, `I386_FAST_EXEC1_IRAM` |
|-|-|-|-|
| `fast_exec()` | | 121,006 bytes, internal RAM | 120,998, internal RAM |
| `cpu_exec1()` | 131,456, internal RAM | 136,102, flash | 128,042, internal RAM |
| internal RAM (DIRAM) used by code and static data | 251,898 | 262,700 | 390,738 |
| internal RAM left for ESP-IDF at run time | 251,846 | 241,044 | 113,006 |
| flash (code and read-only data) | 706,866 | 835,566 | 699,468 |
| image | 937,884 | 1,069,178 | 1,061,118 |
| warnings (not ESP-IDF's or managed components') | 1 (`i2s_task` unused) | the same 1 | the same 1 |

The DOS board's app: 249,640 bytes of internal RAM left with v4.6, 238,802 with v5.0, and the
same warnings (none) [verified]. (These are the branch `v50-fast`'s builds. v5.0 as released,
with the frame buffer's path and the reviews' changes too, leaves 241,444 bytes on the Korvo-1
and 239,202 on the DOS board, with `fast_exec()` at 120,198 bytes and `cpu_exec1()` at 132,126:
`BUILD-NOTES.md` 33.8.)

* **In internal RAM** (HP SRAM, which no cache stands in front of): `fast_exec()` and its jump
  tables (2 KB), its TLB (8 KB, `fast_state0`, in .bss), its helpers (`fast_miss()`,
  `fast_where()`, `fast_window()`, `fast_lpgno()`, `fast_tlb_flush()`, and since the reviews
  `fast_io_load()` and `fast_io_store()`), and the tiny386 functions it calls most: the lazy
  flags' readers (`get_CF()`, `get_OF()`, `get_AF()`, `get_PF()`, `refresh_flags()`), `set_seg()`,
  `read_desc()`, `pmcall()` and `pmret()` (which v4.6 kept in flash), the load and store functions
  and the frame buffer's `lfb_stos()` and `lfb_movs()` [verified: the map]. Over the first 300 M
  instructions of Windows 95's boot those were 2.7 host instructions a guest instruction under
  qemu, 1.1 of them `get_CF()` [verified, section 7].
* **In flash:** `cpu_exec1()`, and of the functions `fast_exec()` calls, nothing else [verified:
  the disassembly of v5.0's `fast_exec()`, every call; `BUILD-NOTES.md` 33.8]. The first build of
  v5.0 had three more there, which a review found: `fast_io_store()` (the path of every single
  store into the frame buffer and the VGA window), `ldu32()` and `stu32()`, "static inline" copies
  the compiler had kept out of line without `IRAM_ATTR`. The two `fast_io_` functions are now
  `noinline` in internal RAM (inlined, as the review suggested, they made the RV32 bench's loops
  dearer, RAM's too), and `unaligned.h`'s loads and stores are `always_inline`. The rest of
  tiny386 that is in flash is cold: the string and page-crossing byte paths, the BCD and segment
  limit helpers, task switches, the debug printer, and `cpui386_*()`.
* **The hand-overs.** With the fast interpreter on, `cpu_exec1()` runs one guest instruction in
  about 526 on Windows 95's boot (0.29 host instructions a guest instruction there under qemu,
  about 154 for each hand-over) and one in 40 in T386MON's tests at 640x480 in 16 colours (the
  16-colour driver's read-modify-write instructions on the VGA window) [verified: the counted
  runs, as the board review counted them]. Each runs through core 1's 16 KB instruction cache. A
  model of that cache (16 KB, 4 ways, 64-byte lines, fed with the counted runs; the board review's)
  puts the cost at about 1 per cent in both, 3 to 6 per cent if the cache behaved as 2 ways
  [verified: model; the cost of a miss, 300 to 600 cycles, inferred].
* **`fast_cpu = 0` is slower than v4.6.** It runs all of tiny386 from flash. The same model puts
  it at about 150 flash misses a 1,000 guest instructions on BENCH (v4.6 about 0.001), so BENCH
  at about 1.4 to 1.7 million instructions a second where v4.6 gave about 2.2, FreeDOS's DOS calls
  at a third to a half of v4.6's speed, and Windows' boot at a half to two thirds [inferred: the
  model]. Compare speed with v4.6's own figures, never with `fast_cpu = 0`.
* **Why not both in internal RAM:** that leaves 113,006 bytes before ESP-IDF's allocations at run
  time (the tasks' stacks, the USB host, the card's buffers), and the firmware has never measured
  those. v4.6 leaves 251,846, and an older note estimated about 180 KB free at run time
  (`BUILD-NOTES.md` 6) [inferred]: about 40 KB would be left, too tight to ship untested
  [inferred]. The boot console's `Internal RAM free ... KiB` line on v5.0 tells; if it shows 150
  KiB or more, `I386_FAST_EXEC1_IRAM` is worth a build [inferred].
* **`cpu_exec1()` the same as v4.6 when it is in internal RAM:** the fast interpreter calls
  `translate_lpgno()` through its own copy (`fast_lpgno()`), and the TLB hook is a call, so that
  the compiler inlines into `cpu_exec1()` as it did for v4.6 (a second caller had made it 17 KB
  larger) [verified: the object sizes].


## 6. Tests

* **The host tests** (`tests/host/run.sh`): 65 pass on the branch `v50-fast`, v4.6's 61 and four
  new [verified]; with the frame buffer's tests beside them in v5.0, 69, and 72 since the reviews
  (`BUILD-NOTES.md` 33.7). Section 24 (23 on `v50-fast`)
  is `tests/host/cpu/test_fast_cpu.c`: two machines, one with the fast interpreter and one with
  tiny386's `cpu_exec1()` alone, run the same random guest code and get the same interrupts,
  between steps and from inside an OUT; after every step it compares the registers, the lazy
  flags, the instruction count, CR2, tiny386's fetch cache and its whole TLB, all of memory (the
  page tables' accessed and dirty bits too, which the guest clears without INVLPG), the frame
  buffer's marks and flag, and every port and video memory access with the instruction count at
  it. Since the reviews each seed runs fourteen scenarios (the CPU review's own extended copy of
  the test, taken in): real mode with 66h and 67h; 32-bit protected mode with paging at levels 0,
  1 and 3; virtual-8086 mode at IOPL 3, and at IOPL 0 to its first sensitive instruction; 16-bit
  protected mode with and without paging; 16-bit code on a 32-bit stack and the reverse; and four
  hand-written cases (levels 1 and 2 reading a supervisor page that level 0 has used,
  self-modifying real-mode code, CR0.WP set after a supervisor store into a read-only page). The
  random code has arithmetic on registers and memory (operands across pages and segment ends),
  shifts, multiplies and divides, bit tests, string instructions with and without REP (DF either
  way, video memory too), the stack, CMP or TEST with JZ or JNZ after, near and far jumps, calls
  and returns direct and indirect, PUSH from memory, INT and IRET, PUSHF and POPF, STI and CLI,
  segment and SS loads, ports, the VGA window and a linear frame buffer, pages that fault until a
  handler maps them, INVLPG and MOV CR3, and instructions only tiny386 does. With an odd seed the
  CPU is given the frame buffer, with marks, as `pc.c` gives it, so the fast interpreter's entries
  for it are compared too: loads, MOV, STOS, MOVS and REP into it and out of it, arithmetic from
  it and into it, ARPL, and an OUT that moves the frame buffer or takes it away, as a write to the
  VGA's BAR does. It runs as the Korvo-1 builds `i386.c` and without the board header, 560
  scenarios each, then on five copies of `i386_fast.h` with a fault put in, which it must catch:
  the fusion bug below, the count left behind at port accesses, levels 1 and 2 given level 0's
  table, a MOV into the frame buffer through its entry without its marks, and a frame buffer page
  given a plain write entry [verified: `BUILD-NOTES.md` 33.7].
* **What it found:** the first instruction of a page that the fast interpreter hands to tiny386
  saw tiny386's fetch cache already filled, so tiny386 fused a CMP with the JZ after it where,
  alone, it would not have (4 of 60 runs, all in real mode). Fixed (the cache is given back as it
  was); none in 2,100 runs since, about 45 M instructions compared at 800,000 step ends, in three
  builds (the board's flags on x86-64, x86-64 without the board header, 32-bit x86) [verified].
* **What the CPU review found with its wider copy** (section 11): code at levels 1 and 2 used the
  fast interpreter's table for level 3, so an entry level 0 had made for a supervisor page let it
  read or write where tiny386 gives a page fault. Fixed; the hand-written cases differ at their
  first step without the fix and agree with it. Since then: 560 scenarios in each of the two
  builds in `run.sh`, and 4,200 more in each (seeds 41 to 340, 56 M guest instructions, 641,707
  step ends compared), no difference [verified: `BUILD-NOTES.md` 33.7].
* **Windows 95 on the PC repro** (`~/win95-debug/pc-repro`, the pristine image copied for each run,
  v4.6's three runs of `regress46.sh`: boot to the desktop; Start, Restart; an MS-DOS mode round
  trip): with the fast interpreter on and with it off, each ends on v4.6's count of instructions
  (588,301,033; 1,304,341,904; 1,337,723,352), its log is v4.6's line for line (the harness's state
  line every 5 virtual seconds has CS:EIP, ESP, SS, CR3 and the flags), all its pictures (11, 149
  and 150) are v4.6's byte for byte, and its disk check is v4.6's [verified].
* **Under qemu, on the board's RV32 code** (section 7): BENCH's pieces and the first 300 M
  instructions of Windows' boot end on the same state and counts with v4.6, the fast interpreter,
  and the fast interpreter off [verified].
* **Not tried:** the board; task-switch code beyond what Windows 95's runs exercise; tiny386's FPU
  (handed over whole).
* **The lockstep tester** (`tests/lockstep`, v5.0): v5.0 with the fast interpreter on against v4.6,
  through the boot to the desktop, Start and Restart, the MS-DOS mode round trip, BENCH, the frame
  buffer's guest and T386MON's drawing tests: no difference in any record or interrupt
  [verified: `tests/lockstep/README.md` 9, `BUILD-NOTES.md` 33]. Its self-test plants two faults in
  `i386_fast.h` (the fusion bug above, and the count left behind at port accesses) and names the
  instruction where each first shows in SeaBIOS.
* **v5.1, Plat's own setup** (`tests/lockstep/README.md` 10): the lockstep tester's `t386mon800`
  runs Windows 95 at 800x480 in 16 bits through VMDisp9x with his pointer change, on the frame
  buffer, and T386MON's tests to "Tests done". v5.0 with the fast interpreter on and off against
  v4.6: no difference, 792,272,554 instructions [verified, 2026-10-28]. A lost mark planted in
  `lfb_st` is named at its instruction; faults in what the fast interpreter does when the frame
  buffer moves, or when its dirty bit is cleared, do not show there (Windows does neither while it
  draws), and are the host test's.
* **v5.1, the host test** (`run.sh` 24, from v5.0's recheck, its RC-1 and RC-2): twenty scenarios a
  seed, the six new ones the frame buffer from 16-bit protected mode with and without paging, at
  level 3 with IOPL 3, at level 1 after level 0, from unreal mode, and each of its pages stored into
  with its dirty bit cleared in between; tiny386 moves it too (OUTSB to port E4h); the fast TLB's
  invariants are checked after every step, and a crash is reported as the scenario's failure. 800
  scenarios in each build, then nine planted faults, all caught, four of them new (no flush after
  the fast OUT moves the frame buffer, no `lfb_gen` test in `fast_tlb_check()`, no dirty bit on a
  store through a frame buffer write tag, an old write tag kept by `fast_miss()`'s frame buffer
  branch), which the fourteen scenarios of v5.0 did not catch; and 10,000 more scenarios in each
  build (seeds 1001 to 1500, 135.5 M instructions, 1,306,991 step ends, 14.2 M fast TLB entries
  checked with the board header, 0.41 M of them the frame buffer's), no difference and no
  invariant broken [verified, 2026-10-28].


## 7. Host instructions a guest instruction (qemu)

`research\JIT-OPTIONS-2026-10-24.md` 7's method: the PC repro's harness (`headless.c`, with marks
where qemu's log is cut) and this tree, cross-built as a riscv32 Linux program with the Linux
route's compiler (GCC 15.3) and the board's options as far as qemu 7.2 runs them (RV32IMAC with
Zba, Zbb and Zbs; `-O3` for `i386.c`, `vga.c`, `pc.c`, `-O2` for the rest, the Korvo-1's board
header), run under `qemu-riscv32-static -d in_asm,exec,nochain`, every host instruction counted by
`jtrace`. Machine: 14M, 320K, gen 3, no FPU, the clock made of guest instructions. "core 1" leaves
out what the board's core 0 does (drawing the picture) and the harness's own code; "i386.c" is the
CPU alone [verified]. These are the `v50-fast` branch's counts; v5.0's, before and after the
reviews, are in `BUILD-NOTES.md` 33.6 (BENCH and Windows' boot within a few tenths of these).

BENCH (FreeDOS, `BENCH.COM`):

| piece | guest instructions | v4.6 core 1 | v5.0 core 1 | v4.6 i386.c | v5.0 i386.c |
|-|-|-|-|-|-|
| boot (BIOS, FreeDOS) | 11,779,454 | 141.0 | 90.7 | 118.1 | 67.7 |
| A1 16-bit arithmetic | 2,097,152 | 86.9 | 57.3 | 85.4 | 55.7 |
| A2 16-bit loads and stores | 1,769,472 | 163.8 | 86.9 | 162.2 | 85.4 |
| A3 16-bit calls and stack | 2,555,904 | 119.3 | 62.5 | 117.8 | 61.0 |
| A4 `rep movsw/stosw` (a whole REP is one instruction) | 22,017 | 29,142 | 3,201 | 29,141 | 2,681 |
| A5 DOS calls | 5,714,360 | 138.5 | 74.5 | 137.0 | 72.8 |
| A6 32-bit arithmetic, paging | 1,999,872 | 85.9 | 56.9 | 84.4 | 55.4 |
| A7 32-bit loads and stores, paging | 1,769,472 | 179.1 | 83.6 | 177.6 | 82.1 |
| A8 32-bit calls and stack, paging | 2,599,936 | 140.4 | 64.2 | 138.8 | 62.7 |
| A9 a TLB miss every time | 1,027,072 | 202.3 | 166.3 | 200.7 | 164.8 |
| **without A4 and the boot** | | **134.6** | **74.7** | **133.1** | **73.1** |

All host instructions, without A4 and the boot: v4.6 136.2, v5.0 76.3 (1.79 times fewer). The
whole run: 164.4 and 90.3 (1.82) [verified].

Windows 95's boot, the first 300 M instructions (the pristine image):

| guest instructions | v4.6 all | v5.0 all | v4.6 core 1 | v5.0 core 1 | v4.6 i386.c | v5.0 i386.c |
|-|-|-|-|-|-|-|
| 0 to 50 M | 178.8 | 118.9 | 139.4 | 79.1 | 129.7 | 69.2 |
| 50 to 100 M | 153.7 | 90.0 | 137.5 | 73.8 | 134.8 | 71.0 |
| 100 to 150 M | 183.2 | 99.7 | 176.9 | 93.4 | 174.4 | 90.8 |
| 150 to 200 M | 166.0 | 86.9 | 158.8 | 79.6 | 157.1 | 77.9 |
| 200 to 250 M | 167.0 | 87.2 | 159.8 | 79.9 | 158.1 | 78.3 |
| 250 to 300 M | 167.0 | 87.2 | 159.8 | 79.9 | 158.1 | 78.3 |
| **first 300 M** | **169.3** | **95.0** | **155.3** | **81.0** | **152.0** | **77.6** |

The fast interpreter: 1.78 times fewer host instructions in all, 1.92 on core 1's work, 1.96 in
`i386.c` [verified]. From about 150 M on, Windows waits at its first dialog, which nobody answers
in these runs, so the last stretches are its idle loop [inferred: the PC repro's runs see the first
dialog at 77 virtual seconds; the counted runs press no key]. "all" carries the harness's drawing
of the 640x480 16-colour picture every 30 ms (7 to 13 per cent), which the board does on core 0.

Where the fast interpreter's work went on Windows' boot [verified: the attribution of every host
instruction to its function]: `fast_exec()` 71 per cent; finding the next page of code
(`fast_window()`) 4.3; tiny386's TLB refills through `fast_lpgno()` 1.5; `get_CF()` 1.1;
segment loads and far transfers (`read_desc()`, `set_seg()`, `pmcall()`, `pmret()`) 1.3 host
instructions a guest instruction together; `cpu_exec1()` 0.3 per cent. With v4.6, `cpu_exec1()`
was 42 per cent and its fetch, decode and address helpers most of the rest.

What the counts say [inferred, from the tables]:

* The fast interpreter about halves the CPU's own work on everything but TLB misses, close to
  what the research measured for tab5-pc's own build (2.0 times on BENCH without A4). The
  exactness costs a little: tiny386's TLB size (256 entries, where tab5-pc keeps 1,024 a
  privilege level), the dirty bit looked at on every store, and the fusion and fetch rules.
* A9 gains least (1.2 times): every access there misses tiny386's TLB, which the fast interpreter
  now follows entry for entry.
* `rep movsw` and `stosw` are 9 to 11 times cheaper (a page's worth at a time with `memcpy` and
  `memset`).


## 8. With the rest of v5.0

This section was written on the branch `v50-fast`, before v5.0 was put together; `BUILD-NOTES.md`
33 is what happened.

* **The frame buffer's fast path** went into tiny386's load and store functions, and the fast
  interpreter's single MMIO loads and stores reached it through `load8()` to `store32()`. Its own
  REP STOS and REP MOVS into video memory now take tiny386's v5.0 string paths too (`lfb_stos()`,
  `lfb_movs()`, the VGA window's own calls) [verified: `BUILD-NOTES.md` 33.3].
* **Since the reviews, the frame buffer's pages are in the fast interpreter's TLB.** A page all
  inside the frame buffer gets an entry as a RAM page does (only while tiny386's TLB holds it, the
  rights and the dirty bit as tiny386 has them). For reads it is a host address in video memory,
  so any instruction reads the screen as it reads RAM (`ADD EBX,[ESI]` no longer goes to tiny386).
  For writes it is only a tag, which no RAM store path takes: MOV and single STOS find it, store
  the bytes and then set the marks and the flag with `i386.c`'s own `lfb_marked()`, the same
  bytes, marks and order as `store8()` to `store32()`. Every other store there (read-modify-write
  instructions such as `AND [EDI],EAX`, single MOVS, crossing a page) still goes through tiny386's
  store or string functions; ARPL, which writes through its read entry as tiny386 does, sets the
  marks too. The entries go when the frame buffer moves: `cpu_cb_set_lfb()` counts its calls
  (`lfb_gen`), and the fast interpreter compares the count after every OUT and whenever it checks
  its TLB. The VGA window (A0000h) gets no entries: its planar memory needs `vga.c` on every
  access [verified: `BUILD-NOTES.md` 33.3 and 33.15].
* **The descriptor cache** was left out of v5.0: on Windows' boot it saved 0.8 per cent of host
  instructions under tiny386, about 2 per cent with the fast interpreter [inferred], under the bar
  set for it. Its branch, `v50-desc`, is kept.
* **The lockstep tester** can run one CPU each way (`cpui386_set_fast()`); the two take turns at
  any step boundary [verified: section 6]. v5.0 against v4.6: section 6.


## 9. For the board

1. Flash the Korvo-1 dual-boot image as usual; the boot console should say `CPU: the fast
   interpreter (from tab5-pc, by Yejun Yang) in front of tiny386's own`, and `Internal RAM free
   ... KiB` (a photograph of both, please).
2. FreeDOS: BENCH with the default ini. v3.6 gave 2.231 million a second; v5.0 should give about
   4 [inferred: section 7's factor]. With `fast_cpu = 0` it will be slower than v4.6 (section 5):
   that run only shows that the guest behaves the same.
3. Windows 95 at 800x480: T386MON's tests and the speed screen with the default ini, against your
   v4.6 log; and anything that looks different from v4.6 (it should not be).
4. If anything goes wrong with `fast_cpu = 1` and not with `fast_cpu = 0`, that is a difference
   the tests here missed: the CS:EIP on the crash screen, or what was running, helps most. To
   carry on at v4.6's speed meanwhile, write v4.6's app back (`TEST-PLAN.md` 18f).


## 10. Repeating it

Scratch of this work: WSL `~/org2/v50/fast` (not in any repository).

* `bash tests/host/run.sh` (72 tests in v5.0; `FCSEEDS=` sets the lockstep test's seeds, 40 by
  default).
* `bash tests/lockstep/lockstep.sh REF CAND WORKLOAD OUT` (`tests/lockstep/README.md`).
* The Windows runs: the PC repro's harness (`~/org2/pcm/canary/harness`) built from this tree
  (`build.sh TREE esp OUT rv`, and with `EXTRA_CFLAGS=-DI386_FAST_DEFAULT=0` for the fast
  interpreter off), then `regress50.sh` (v4.6's `regress46.sh` with the binary as an argument).
* The counts: `qh/qbuild.sh TREE OUT` (the harness cross-built, with marks), `qh/qrun.sh BIN NAME
  win|bench [trace]` (the image or BENCH's disk copied for the run and removed after), and
  `qh/qreport.py`. A traced Windows run takes 1 to 3 hours.


## 11. After the two reviews of v5.0

Two reviews of v5.0 (a CPU correctness review and a board, speed and package review) read this
port on 2026-10-27; `BUILD-NOTES.md` 33.15 lists every finding and what was done. For the fast
interpreter:

* **Levels 1 and 2** (the CPU review's CPU-1). The port chose its TLB table by `cpl == 3`, where
  tiny386 checks a page's rights by `cpl > 0`. An entry level 0 had made for a supervisor page
  was then used at level 1 or 2 without tiny386's check: the guest read or wrote where tiny386
  (and v4.6) gives a page fault. Only paged code at level 1 or 2 could see it (OS/2's IOPL code,
  say); Windows 95 and DOS extenders use levels 0 and 3 [inferred], and the release gate never
  reached it. Now levels 1 to 3 share one table, as tiny386's rule has it; levels 0 and 3 choose
  the tables they chose before. The test has levels 1 and 2 now, and catches the old rule.
* **Helpers in flash** (R1): section 5.
* **The frame buffer's single stores and its reads** (R2): section 8. tests/host/lfb's RV32
  bench, host instructions for each guest store or load (v4.6, v5.0 before the reviews, v5.0 now):
  `MOV [EDI],EAX` into the frame buffer 463.1, 376.0, 281.2; `ADD EBX,[ESI]` from it 479.1, 664.0,
  260.0; the same two on RAM 438.1, 252.1, 254.1 and 464.1, 258.0, 260.0 [verified:
  `BUILD-NOTES.md` 33.3].
* **`fast_cpu = 0`** (R3) is the same guest as v4.6 but slower: section 5.
* **The test** took in the CPU review's wider copy, and the frame buffer given to the CPU (section 6).

What tiny386 itself does otherwise than the 386 manual, and so both interpreters do, as v4.6 did
[verified: read in `i386.c` and `i8042.c` by the CPU review]: no single step on the trap flag; the
debug registers stored only; no segment limit or type checks (only null selectors fault); the A20
gate does nothing; levels 1 and 2 are user for paging; BT with an immediate adds a byte offset,
and a register's bit offset is unsigned; REP is never interrupted part way; a 66h near jump is
masked by the code size; an instruction longer than 16 bytes reads on into the next physical page;
code fetched from the cached code page is not checked again after a change of privilege level.
Programs that depend on any of these behave as under v4.6 [inferred].
