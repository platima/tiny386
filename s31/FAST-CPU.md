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


## 12. v5.1: the profiler, and what it shows of the fast interpreter

The counts of section 7 are host instructions under qemu: they cannot see core 1 wait on PSRAM or
on flash. The profiler (`PROFILE.md`; `[esp] profile = 1`, off by default) samples core 1's PC from
core 0 on the board, about 2,000 times a second, with the guest's state at the same moment, and
writes the counts to the card. For the fast interpreter its report gives [verified: on the PC's
stand-in, `PROFILE.md` 8]:

* `fast_exec()`'s share of core 1, and each handler's (`op_8b`, `ALU6(op_38, ...) CMP`, `slow`,
  ...) by the source line each sample's PC is at, so a handler's waits on PSRAM count where they
  are;
* how much of core 1 is in flash (`cpu_exec1()` since v5.0, section 5) and in internal RAM, with
  the cache counters' lines fetched from flash and PSRAM each second and per 1,000 guest
  instructions;
* the instructions handed to `cpu_exec1()`, by their bytes and mode, weighted by the time they took
  rather than by how often they ran: what to take into the fast interpreter next;
* the guest's code by mode and region, and by area (the fast interpreter, handed over, halted).

It changes nothing here: `i386.c` gains `cpui386_prof_fields()`, which hands out the addresses of
the fields the profiler reads and is called once; every other function of `i386.c`, `fast_exec()`
with them, compiles to the same instructions as in v5.0 (`cpui386_new()`'s `assert()` line number
apart), and the guest runs as before, by the lockstep tester and the three Windows runs
[verified: `PROFILE.md` 7 and 8]. The guest's EIP the profiler reads while the fast interpreter
runs is where its step began (it keeps EIP to itself until the step ends); the code page it reads
is the one being run, from tiny386's fetch cache, which the fast interpreter keeps up to date.


## 13. v5.1: core 1 faster, the same guest

v5.1 makes the fast interpreter cheaper while the guest sees exactly what v4.6 shows it. Each part
is a work item on its own branch, proved by the host tests, the lockstep tester against v4.6 (seven
workloads, the fast interpreter on and off), the three Windows runs and the counts under qemu.

### 13.1 The loop's state in registers, and the code window (WI-1)

Written 2026-10-28 on the branch `v51-loop` (from `v51-lock800`, d5aa9c9). It takes the v5.1
ideas work's prototypes A to D and the structure design's ST4, tidied, with new tests written
first. The figures are host instructions a guest instruction, the board's compiler (GCC 16.1,
`-O3`, `-mtune=esp32s31`), counted under qemu.

What changed in `i386_fast.h` [verified: read in the diff]:

* **A, `pc` in a register.** `fast_window()` gives the code window back in a `struct fast_win`
  (the instruction's host address, its page's start, `ipd`). `fast_exec()` no longer takes the
  address of `pc`, `win_lo` or `ipd`, so GCC keeps `pc` in a register. v5.0 had it on the stack:
  1.8 to 2.3 loads and 2.5 to 3.1 stores a guest instruction (the v5.1 profile).
* **B, the window's one bound.** `NEXT` tests `pc >= win_hi` (the window's end). Between two
  refills `pc` only moves up, except in `JUMP_REL()` and `JUMP_ABS()`, which set `win_hi` to 0
  when the target is below the window, so that the next `NEXT` refills it. They are the only
  backward moves of `pc`; the comment at `NEXT` says any new one must go through them.
* **C, a new code page through the fast TLB.** With paging, `fast_window()` takes a new code page
  from the fast TLB's read entry for it, or from `fast_miss()` as a 1-byte read. An entry exists
  only while tiny386's TLB holds the page with its rights checked for this privilege class, so
  `peek8()` would find the same page there, with nothing changed. A host address outside guest RAM
  is the frame buffer's read entry: code there stays tiny386's. Then `peek8()`'s checks as before.
* **D, a TLB hit in one compare.** `FPTR()`, `FPTR_IO()` and `FPTR_ST()` compare the tag with
  `FAST_PL()`: the page of the access's last byte, with bit 20 set when the segment is null. An
  access across a page never hits (a tag is always the page of its own index), nor one through a
  null segment (no tag has bit 20). The null segment's test moved into the miss, before
  `fast_miss()` and before `FPTR_ST()`'s frame buffer test, which is one compare too. Compile-time
  checks: bit 20 is not `FAST_TAG_LFB`, and no `FAST_PL()` value is the empty tag. Jcc short has a
  handler for each condition (`JCC_B(c)`), so `fast_cond()` folds to it.
* **ST4, the window refilled inline.** At `refill:` an instruction in a page's last 16 bytes goes
  straight to tiny386. A page change with paging on, CS not null, and a fast TLB read entry that is
  RAM where `peek8()` would cache it, is done inline, with the fusion rule's saves, so the page's
  first instruction is never fused. Anything else calls `fast_window()`. The prototype also had
  the cached page inline; that case never reaches `refill:` (the window is always the cached page,
  and `refill:` comes only past its end or after a jump below its start) [inferred: the code;
  verified: a build that aborts on it ran `test_fast_cpu`'s 6,600 scenarios of seeds 1 to 300 and
  the `b1`, `msdos` and `t386mon800` workloads to their ends]. `fast_window()` still handles it.
* **Two saved values kept in memory.** `nofold_laddr` and `nofold_xaddr` (the fetch cache before
  a page was newly cached) are `volatile`: they are written at a page change and read only when an
  instruction goes to tiny386. GCC then allocates `fast_exec()`'s registers better: every memory
  operand form 4 to 5 host instructions cheaper, BENCH 64.6 to 63.5 [verified: mb and BENCH, the
  same code with and without].

A v5.0 difference from v4.6, found by the new mode l and fixed in `i386.c` [verified]: when
tiny386's TLB held a page with rights that refuse an access (at level 0 a store into a read-only
page with CR0.WP set, say), `fast_lpgno()` ran `translate_lpgno()`'s body, which drops the entry on
the fault. tiny386, doing the instruction again, then walked the page tables anew: it set the
accessed bits the guest had cleared without INVLPG, and took rights the guest had widened without
INVLPG, so a store went through where v4.6 faults once on the entry it holds. Now `fast_lpgno()`
leaves such an access to tiny386 untouched. On v5.0's code mode l fails 18 of 80 scenarios (modes
k and l, 40 seeds); with the fix none. None of the lockstep workloads showed it [inferred: Windows
does not change those bits and then store into the page without INVLPG there].

Tests, written before the code [verified: the commit order; `tests/host/cpu/test_fast_cpu.c`]:

* **Mode k**: jumps, calls and returns across code pages. Every backward move lands in the page
  before (or the one before that), in its last 16 bytes (tiny386 runs those) or below them (a page
  the fast interpreter caches anew): JMP short and near, Jcc short and near (a JZ or JNZ fused with
  its CMP), LOOP and JCXZ, CALL near, JMP and CALL through a register, PUSH and RET, PUSH, PUSH and
  RET n, RET and RET n from the next page; and JMP or CALL forward into the next page's last 16
  bytes. The tails hold CMP, JZ pairs that must not be fused as a page's first instruction. By the
  seed: 32-bit code at levels 0 and 3, 16-bit protected mode at levels 0 and 3, virtual-8086 mode;
  paging on; the linear pages mapped to physical pages out of order, so that code run from the
  wrong page runs other bytes.
* **Mode l**: a page read, then written. A user read-only page at level 3 or 1 and a read-only
  page at level 0 with CR0.WP set (the store faults; the handler makes the page writable), and
  CR0.WP clear (no fault); every store path; at level 0 the PTE changed without INVLPG (accessed
  and dirty bits cleared, or the page made writable) and CR0.WP changed; a segment register loaded
  with a null selector and the page read or written through it (#GP; the handler loads it again);
  with the frame buffer, code run from it.
* The frame buffer's host memory is now 4 GiB above guest RAM (on a 64-bit host), so that a host
  address in it taken as a 32-bit physical address lands in RAM, where the checks after it pass.
* Five faults planted in `run.sh` 24, each caught (40 seeds, 880 scenarios, the Korvo-1 header):
  `jumplo` (the jumps without the lower bound: mode k fails at its first step in seeds 1, 2 and 3,
  then the test crashes in mode l and the run ends), `codelfb` (a code page from the frame buffer's read entry taken as RAM:
  20), `plnull` (`FAST_PL()` without the null segment's bit: 38), `st4fold` (the inline refill
  without the fusion rule's saves: 40), `lpgno` (the fix above undone: 18). Modes 0 to j alone
  catch only `jumplo`, in 2 of 800.

The proof, on the branch's last commit [verified, WSL, 2026-10-28, with other v5.1 work loading
the machine]:

* `tests/host/run.sh`: 81 passed, 0 failed (v5.1's 76 and the five faults). `test_fast_cpu` on
  seeds 41 to 340: 6,600 scenarios in each build (the Korvo-1 header, and plain), 82.9 M guest
  instructions, 880,314 step ends compared, 0 failed.
* The lockstep tester against v4.6, all seven workloads, the fast interpreter on and off: 14
  comparisons, each "no difference", on the reference's figures (b1 588,301,033, restart
  1,304,341,904, msdos 1,337,723,352, bench 31,345,925, vbe 55,090,157, t386mon 674,307,567,
  t386mon800 792,272,554, each with its disk hash; T386MON's summary in `t386mon800` the same on
  both sides). The self-test passes with `FAST=1` and `FAST=0`.
* The three Windows runs of `regress50.sh`, the fast interpreter on and off: 588,301,033,
  1,304,341,904 and 1,337,723,352 instructions, the desktop each time.

Counts [verified: mb and the counting harness, the board's compiler]:

| | v5.0 | WI-1 |
|-|-|-|
| NOP | 17.4 | 12.4 |
| MOV r,[m], 32-bit code | 86.3 | 73.4 |
| MOV r,ES:[m], 16-bit code | 96.7 | 82.0 |
| ADD [m],r, 32-bit code | 102.9 | 88.0 |
| JB taken after CMP, 32-bit code | 54.8 | 34.8 |
| CALL and RET to another page, 32-bit code | 191.3 | 122.3 |
| all 165 forms of mb, on average | | 11.6 fewer (164 cheaper, 1 dearer) |
| BENCH without A4 and the boot, core 1 | 71.0 | 63.5 (1.12 times fewer) |
| Windows' boot, first 300 M, core 1 | 76.5 | 65.9 (1.16 times fewer) |

Every mb build ends on the same guest state (5,214,225 instructions), and the BENCH and Windows
runs log the same guest as v5.0's (300,000,198 instructions for the boot). The v5.0 figures are
the ideas work's runs of the same harness and compiler; the prototype A to D gave 64.4 and 68.1.
The one dearer form is a far CALL and RETF in 16-bit code, 793.7 to 796.2: after a far transfer
`reload:` copies the window out of `struct fast_win` (4.5 more in `fast_exec()`, 2 fewer in
`fast_window()`).

Sizes [verified: `i386.c` compiled with the Korvo-1 build's command line, and the ESP-IDF builds]:
`fast_exec()` 121,674 bytes (v5.0 121,434: +240), `cpu_exec1()` 132,338 (the same), `i386.o`'s
internal RAM code 148,010 (147,618). ESP-IDF's size report: internal RAM (DIRAM) used 262,660
bytes on the Korvo-1 (v5.0 262,300: +360) and 264,902 on the DOS board (264,542: +360). With
`v51-ram` merged in a scratch clone, 377,341 and 375,173 (`v51-ram` alone 376,981 and 374,813),
which leaves 126,403 and 128,571 bytes for ESP-IDF (v5.1's rule: at least 110,000). The Korvo-1
dual boot and standalone builds and the DOS board build exit 0, with v5.0's one old warning
(`i2s_task` unused, Korvo-1). The same `i386.c` functions are in flash as in v5.0 (and as in
`v51-ram` when merged), and `fast_exec()`'s only call into flash is still `cpu_exec1()` (none
with `v51-ram`, where `cpu_exec1()` is in internal RAM).

For the board [inferred: the counts, at v5.0's calibration of about 1.07 cycles a host
instruction]: Windows' CPU-bound work about 1.15 times v5.0's rate, BENCH about 1.12; T386MON's
text, sprite and full-picture tests about 1.1 times; fill, scroll and full copy a few per cent
(PSRAM bounds them). The board's T386MON log will say [unknown].

For the items after this one: the FPTR macros' miss branch now starts with the null segment's test
(`if (nullsegs & (1u << (sg))) goto slow;`), then `fast_miss()`; in `FPTR_ST()` it sets `msize`,
then tests the frame buffer's tag against the last byte's page with `FAST_TAG_LFB`, then calls
`fast_miss()`. A lean refill in `fast_miss()` (WI-4) must keep `fast_lpgno()`'s new rule: an access
that tiny386's entry refuses goes to tiny386 untouched.


### 13.2 The frame buffer's REP paths, the two-operand rule, REP STOS fills (WI-2)

Branch `v51-lfbrep`, from `v51-lock800`. Written 2026-10-28. Three changes to the fast
interpreter, the removal of a difference from v4.6 that v5.0 had, and their tests.

**What changed** [verified: read in `i386_fast.h` and `i386.c` on the branch]:

* **REP STOS, REP MOVS and MOVS find the frame buffer by its write tag** (`FPTR_IO_W()`). In v5.0
  every REP piece into the frame buffer missed the fast TLB: `FPTR_IO()` compared the write tag
  with the bare page, the frame buffer's tag carries `FAST_TAG_LFB`, so each piece went through
  `fast_miss()` and `fast_lpgno()`, and REP MOVS then looked both ends up again with
  `fast_where()`. Now a tagged page goes straight to the store with its physical address, the
  page's dirty bit set as `FPTR_ST()` sets it. The tag is there only while tiny386's TLB holds the
  page with store rights for this privilege level and the frame buffer has not moved, so tiny386's
  own lookup would hit and change only the dirty bit. REP MOVS calls `lfb_movs()` with the source
  it already found (`movs_rep_dio`); MOVS stores through `fast_io_store()` (`movs_dio`).
* **The two-operand rule** (`fast_miss2()`, on the miss path only). An instruction with two
  memory operands looks its first up, then its second. If the second misses, `fast_miss()` asks
  tiny386's TLB, which refills the entry at that index. When the instruction then went to tiny386
  (MMIO there, a fault, past the end of RAM, a piece `lfb_movs()` refuses), tiny386 translated
  the first operand again: a second refill, which set the page tables' accessed bits again. Now,
  with paging, the instruction goes to tiny386 before `fast_miss()` if tiny386's entry at that
  index holds the first operand's page. tiny386 then translates the first operand (a hit) and
  this one, in its order, as it does alone. Where the first operand is MMIO (MOVS from the VGA
  window, which has no fast entry), the destination is looked up only where tiny386's TLB holds
  its page (`FAST_DEST_HELD()`), so that the lookup changes nothing there.
* **`fast_lpgno()` leaves two kinds of entry alone** (`i386.c`, after WI-2's check). Every
  lookup the fast interpreter makes in tiny386's TLB with paging goes through it (`fast_miss()`
  for operands, `fast_window()` for a new code page). It now returns false with nothing changed,
  and the instruction goes to tiny386, when tiny386's entry for the page refuses the access
  (tiny386 alone faults with the entry it has), and when the refill would drop tiny386's entry
  for the page the code runs from (`cpu->ifetch`). The first was the rule's "first half" in
  `fast_miss2()`; in `fast_lpgno()` it covers single loads and stores, and code pages, too, and
  is WI-1's 0c08c97 line for line. The second keeps the code page's entry for the one instruction
  tiny386 fetches again through its TLB: the first in a page newly cached, which `slow:` hands
  over with tiny386's old fetch cache so that tiny386 fetches it the slow way (no fusion of CMP
  and JZ). Before, if that instruction's lookups had dropped the code page's entry, tiny386
  walked the code page a second time: its accessed bits set again, and, after pieces of a REP
  done here, the code page left in the TLB where tiny386 alone keeps the operand's page. (For
  `fast_window()`'s own lookup the code page is still the old one: a new code page at its TLB
  index goes to tiny386 for its first instruction, which is right and only slower.)
* **REP STOSW and STOSD fill by aligned word stores** (`fill_aligned()`, beside `lfb_stos()`):
  plain aligned 32-bit stores, eight a loop turn, with a 16-bit store first and last for STOSW
  where the piece starts or ends half way into a word, from the piece's lowest element whatever
  DF says. `lfb_stos()` and the fast interpreter's REP STOS piece into RAM use it for 8 elements
  or more; under 8 the element loop stays. Every element has the same value and nothing is read,
  so the memory after a piece is what the element loop leaves; the marks are set after the fence,
  as before.
* `fast_miss2()` and `fill_aligned()` are out of line and in internal RAM (`IRAM_ATTR_FAST_EXEC`,
  `IRAM_ATTR`), as `fast_miss()` and `lfb_stos()` are. In line at its twelve places the rule cost
  about 1.1 KB, the fill about 250 bytes more [verified: compile-only variants, the board's
  compiler].

**The difference from v4.6 that v5.0 had** [verified: `tests/host/lfb`'s probes, below]. A MOVS
(or CMPS, POP into memory, PUSH or CALL from memory, CALL FAR through memory) whose second operand
shares the first's TLB index and goes to tiny386 set the first operand's accessed bit again, where
v4.6 leaves it as the guest left it. It needs a guest that clears an accessed bit without INVLPG
and the shared index, which is why the lockstep runs never showed it [inferred].

**The audit** [verified: read in `i386_fast.h`; each case run, below]. Every handler that looks
up two memory operands, and whether it can hand over after the second lookup:

| handler | first operand, second | hands over after the second lookup when | now |
|-|-|-|-|
| MOVS, one | source, destination | the destination faults (rights) | `FPTR_IO_W()`, `fast_miss2()` |
| REP MOVS | source, destination | the VGA window there, a fault, a piece `lfb_movs()` refuses | the same |
| MOVS and REP MOVS from MMIO (`movs_io`, `movs_rep_io`) | source (no fast entry), destination | a fault there, past the end of RAM; every REP piece from the VGA window | `FAST_DEST_HELD()` |
| CMPS, REP CMPS | source, destination | MMIO there, a fault (level 3, a supervisor page) | `FPTR2()`, `fast_miss2()` |
| POP into memory | stack slot, destination | MMIO there (the frame buffer too), a fault | the same |
| PUSH from memory | operand, stack slot | the stack MMIO or past the end of RAM, a fault | `STACK_PTR2()` |
| CALL near through memory | operand, stack slot | the same | `STACK_PTR2()` |
| CALL FAR through memory (real and virtual-8086 mode) | operand, stack slots | the same | `STACK_PTR_N2()` |
| REP STOS | (one operand) | a fault on an entry tiny386 holds | `fast_lpgno()` leaves the entry alone |
| LES, LDS, MOV and POP of a segment register, far transfers in protected mode | operand, descriptor (`set_seg()`, `pmcall()`, `pmret()`) | never: a fault there is raised from the fast interpreter, not handed over | none needed |
| PUSHA, POPA, ENTER, LEAVE, IRET and RETF in real mode, INT | one block | (one lookup) | none needed |

In `op_grp5` this changed the lookup lines of PUSH, CALL and CALL FAR (six lines, the dispatch
left alone): CALL near and far have PUSH's shape.

Two cases of the same kind outside the handlers above, found while building this item and fixed
after WI-2's check, both in `fast_lpgno()` (above):

* **One access that faults on an entry tiny386 holds** (a store through `FPTR_ST()` or `FPTR()`,
  a load through `FPTR()`, `FPTR_IO()` or `fast_where()`; v5.0's). The fault dropped tiny386's
  entry; tiny386 then refilled it and set the accessed bits again, or read a page table entry the
  guest changed without INVLPG and then perhaps did not fault at all, and CR2 was left as the
  fast interpreter's own lookup set it where tiny386 then did not fault [verified: a scratch copy
  of case m with `MOV [O],EAX` and `ADD [O],EAX` into a read-only page, v5.0 and the branch before
  the fix both differ, PTE 21h against 01h; the check's random test, below; CR2 inferred from the
  code]. WI-1's 0c08c97 is the same test.
* **The first instruction of a newly cached code page**, handed over after its operand's refill
  dropped the code page's entry: tiny386 fetched it through its TLB again and walked the code page
  a second time [verified: case m's case 10 and mode x, below; v5.0 had it for its own hand-overs
  too, a REP STOSB whose second piece is past the end of RAM]. The two-operand rule had made it
  reachable in REP MOVS and REP CMPS, where v5.0 finished the instruction itself.

**Tests** [verified: `tests/host/run.sh`, and the runs below]:

* `tests/host/lfb`: eleven probes (`probe_abit_*`): the hot-spots design's five and one for each
  further handler (CMPSD, POP, PUSH, CALL with the VGA window, REP MOVSD and MOVSD from it). Each
  clears its first operand's accessed bit without INVLPG and runs one instruction; `run_lfb.sh` 4
  wants the same page table entry with the fast interpreter on, off and with v4.6's tree. On v5.0
  ten of the eleven differ (00100027 against 00100007, 001a0027 against 001a0007), the control
  does not. REP STOSW and STOSD of 1 to 40 elements at both alignments, DF either way, into the
  frame buffer with and without paging and into RAM: the same bytes as v4.6's. The frame buffer
  through the page tables at 800000h, and the paging benches `pfill` and `pcopy`.
* `test_fast_cpu`: case j goes on with REP STOSD and REP MOVSD into each page of the frame buffer
  with its dirty bit cleared without INVLPG between them, OUTs to the VGA's BAR between REP
  instructions, and level 3 storing by REP into a supervisor page of it, which must fault (v5.0
  differs there: the accessed bit set again). Case m, new: the rule by hand, one instruction a
  step, the first operand's accessed bit cleared in both machines before every step; v5.0 differs
  in all eleven of its first cases with the Korvo-1 header (ten without it: a 512-entry TLB gives
  the virtual-8086 case no shared index).
* `test_fast_cpu`, from WI-2's check: case m's case 10, by hand: a JMP to page 401h, whose first
  instruction is a REP MOVSB, REPNE CMPSB or REP STOSB with its first piece's operand in page 1h
  (the code page's TLB index) and its next piece handed to tiny386 (the rule, or a page the guest
  pointed past the end of RAM); then page 1h's PTE pointed elsewhere without INVLPG and a dword
  loaded through it; then, as the first instruction in page 403h, a CMP whose operand is the VGA
  window (tiny386 gets it) and a JZ after it that is taken, which tiny386 must not fuse. Mode x,
  at random: MOVS, CMPS, STOS, LODS and SCAS with and without REP, PUSH,
  POP and CALL through memory, single loads and stores, on pages that share TLB indices with each
  other and with the code (a pool at 800000h and A00000h, some aliasing one frame, the VGA window,
  the frame buffer, past the end of RAM), their page table entries changed without INVLPG (A, D,
  R/W, U/S, P, the frame), INVLPG, MOV CR3, CR0.WP, the frame buffer moved, levels 0, 1 and 3, and
  every accessed and dirty bit cleared in both machines before every step. v5.0's code against
  them: case 10 differs in its REP STOSB (a hand-over v5.0 makes itself), not in REP MOVSB and
  REPNE CMPSB, which v5.0 finishes itself; mode x differs in 59 of seeds 1 to 60 in each build.
  The branch before this fix differs in all four case 10 runs of seeds 1 to 48 [verified].
* Planted faults, each caught: no dirty bit where `FPTR_IO_W()` finds the frame buffer's tag
  (case j), the rule taken out of `FPTR_IO_W()` (the probe `abit_win`, case m), the rule taken
  out of CMPS (the probe `abit_cmps`, case m), the fill's last 16-bit store dropped (the REP STOS
  cases), the frame buffer's address a page off on the tag (the cases); after the check, in
  `fast_lpgno()` the code page's test taken out (`codepage`: case m's case 10, mode x) and the
  refusal test (`lpgno`, as WI-1 plants it: cases j and m, mode x), `slow:` not giving tiny386
  back its old fetch cache (`restore`: case 10's CMP and JZ), and MOVS from MMIO looking up a
  destination tiny386's TLB does not hold (`destheld`: case m, mode x).

**After WI-2's check** (S\CHECK-v51-lfbrep.md, six findings) [verified: each change run as below]:

* F1 (major), the first instruction of a newly cached code page: fixed in `fast_lpgno()`
  (above), with `slow:` and `fast_window()` as they were. The check's own cure, not giving
  tiny386 back its old fetch cache when the code page's entry is gone, lets tiny386 fuse a CMP
  with the JZ after it where tiny386 alone does not (the check saw it in case c, seed 214); with
  the entry kept, the slow way is a TLB hit and changes nothing. Two other places were tried
  and left [verified: each built and counted]. The same tests at the top of `fast_miss()` cost
  about 4 host instructions on every miss of tiny386's TLB (BENCH's A9 150.2 to 154.2), BENCH
  without A4 and the boot +0.202 against v5.0, over this item's 0.2. Telling tiny386 not to fuse
  (a flag read in `try_jcc8()`) instead of giving it back its old fetch cache costs nothing a
  miss, but moved `fast_exec()`'s register allocation: the micro-benchmark's REP STOSD of 4 went
  to +5.0 and MOV r,[esp+d] to +3.0 against v5.0, over the item's 2. In `fast_lpgno()` the tests
  share the entry and its compare with `translate_lpgno_body()`: A9 153.8, and `fast_exec()`
  is untouched.
* F2 (minor, v5.0's), one access that faults on an entry tiny386 holds: fixed in `fast_lpgno()`.
* F3 (minor, speed), MOVS from the VGA window with paging: `FAST_DEST_HELD()`, 22 bytes. The
  check's form (also refilling an index that does not hold the source's page) cost 88.
* F4 (note), no planted fault for the rule's first half: that half is now `fast_lpgno()`'s
  refusal test, planted as `lpgno`.
* F5 (note): this section.
* F6 (note), two measures of the paging bench: the targets and the figures below are host
  instructions counted with the board's ESP-IDF GCC 16.1 on `i386.c` (as the micro-benchmark
  builds it); `run_lfb.sh` 3 builds every file with the Linux route's GCC 15.3, and its paging
  rows run 0.16 to 0.32 higher (fill 4.4 against 4.17, RAM to the screen 6.9 against 6.59).
  `run_lfb.sh` says so now.

**Results** [verified, 2026-10-28, WSL, the branch after WI-2's check (4cf12a6); the counts for
the boards' RV32 code on 488493b, whose code for the boards is the same (the board's compiler
gives the same instructions, only line numbers in assert calls differ); the machine loaded by
other work]:

* Host tests: `run.sh` 88 passed, 0 failed (76 at the base; the probes and four
  planted faults in `run_lfb.sh` 5; seven planted faults of WI-2's in `run.sh` 24). `test_fast_cpu`
  on seeds 41 to 340: 6,600 scenarios in each build (the Korvo-1 header, and plain; modes 0 to j,
  m and x), 1,586,557 and 1,585,335 steps, 0 failed.
* The lockstep tester against v4.6, seven workloads with the fast interpreter on and off: no
  difference in any of the 14, each on v4.6's count and disk (b1 588,301,033, restart
  1,304,341,904, msdos 1,337,723,352, bench 31,345,925, vbe 55,090,157, t386mon 674,307,567,
  t386mon800 792,272,554), T386MON's summaries the same on both sides. The self-test passes with
  the fast interpreter on and off (every planted bug found where its interpreter runs).
* The three Windows 95 runs of the PC repro end on 588,301,033, 1,304,341,904 and 1,337,723,352
  instructions with the fast interpreter on and off.
* Host instructions, the board's compiler on `i386.c` (`tests/host/lfb`'s paging bench, a dword;
  v5.0 to this branch, the item's target): fill of 400x300 in rows 8.03 to 4.17 (4.3); RAM to the
  screen 8.47 to 6.59 (6.6); scroll up 8.15 to 6.07 (6.1); scroll down 8.02 to 5.84 (5.9); the
  screen to RAM 5.48 to 5.49 (unchanged). The micro-benchmark (165 forms, every build on the same
  guest state): REP STOSD of 256 1,645.5 to 475.5, REP STOSW of 256 1,653.5 to 309.5 (520), REP
  STOSD of 4 +2.0 (2), MOVSB -1 (5), REP MOVS -3, CALL and RET near -5; REP STOSW of 4 +8, the one
  form more than 2 worse (the fill's test on every piece and the element loop's layout; 16
  elements already gain 69). BENCH without A4 and the boot, core 1, 71.035 to 71.216 (+0.181:
  A9, a TLB miss every time, 150.2 to 153.8, the code page's test; A4, the REP MOVSW and STOSW
  piece, 2,827.7 to 854.4). Windows' boot, the first 300 M instructions, core 1, 76.498 to
  76.355 (-0.143; the branch before the check 76.512).
* Internal RAM (ESP-IDF builds, DIRAM used): +754 bytes on the Korvo-1 (dual boot and standalone,
  263,054) and the DOS board (265,296) against v5.0, 240,690 and 238,448 left; with `v51-ram`
  merged, +754 against it, 126,009 and 128,177 left. `fast_exec()` 120,960 bytes (+762),
  `fast_lpgno()` 290 (-162, as GCC lays it out with the two tests), `fast_miss2()` 68,
  `fill_aligned()` 108, `lfb_stos()` 316 (-16); `cpu_exec1()` unchanged; no new function in
  flash; no new warning.

**On the board** [inferred: the per-dword savings above, as the hot-spots design worked them out;
[unknown] until Plat runs T386MON]: scroll about 8 to 15 per cent faster, full copy 6 to 9, fill 2
to 4 before G, and G 0 to 18 per cent more on fill depending on how much of fill is PSRAM's time
(the sampler's data cache counters say).


### 13.3 Descriptor reads, the group opcodes and the lazy flags (WI-3)

Written 2026-10-28 on the branch `v51-segdisp` (from `v51-lock800`). Tags as at the top. "mb" is
the v5.1 ideas work's micro-benchmark: each instruction form's host instructions a guest
instruction, `i386.c` built with the board's compiler, run under qemu.

**What changed**

* **A descriptor read with one lookup** (`read_desc()`, `i386.c`). tiny386 read a descriptor's two
  words with two `translate_laddr()` calls and two `load32()`. When both words are wanted, the
  entry is inside the table's limit and in one page (always, with an 8-aligned table), and
  tiny386's TLB holds that page, it now takes that entry and loads the two words from RAM. That is
  what the two calls did there: both find that entry; a read at level 0 never faults on a present
  page (`pte_lookup[..][..][0][0]` is 0 in every row) and sets no dirty bit; `load32()` of RAM is
  `pload32()` [verified: read]. Everything else goes to `read_desc_slow()`, which is v5.0's
  `read_desc()` unchanged: a limit fault, the page not in the TLB (it refills it), an entry across
  a page, in MMIO or at the end of RAM, and the second word alone (LAR's and IRET's checks), whose
  fault has CR2 at base + offset + 4 [verified: read]. The new path makes no call, so it needs no
  stack frame, and GCC inlines it into `set_seg()`, `pmcall()`, `pmret()` and the LLDT, LAR, LSL,
  VERR and VERW helpers [verified: the Korvo-1 object's calls of `read_desc_slow()`]. tiny386's
  own interpreter takes it too. (A first form looked the page up through `translate_lpgno()`,
  which may call `tlb_refill()`, and so kept a frame: a segment load cost 194 host instructions,
  not 165.)
* **Segment loads in the fast interpreter** (`FAST_LOAD_SEG()`, `i386_fast.h`). The null-segment
  mask is kept for the one segment loaded, in place of `fast_nullsegs()`'s loop over six. The
  macro is used only in protected mode (`PROTECTED`) and never for CS, and `set_seg()` changes no
  other segment [verified: read]; a selector with bits above its RPL is never null. And the
  instruction count is brought up to date before `set_seg()`, as v5.0 already did before
  `pmcall()` and `pmret()`: v5.0 read a descriptor table in MMIO with the count of the step's
  start, where tiny386 has the instruction's (the host test's mode o found it: 95 against 97)
  [verified]. No guest puts its tables in MMIO, and no device here reads the count on a VGA or
  frame buffer read, so no guest saw it [inferred].
* **The lazy flags' operation and mask in one store** (`struct CPUI386`'s `cc`). Both are now 16
  bits and side by side, so `cc.op = A; cc.mask = B;` becomes one 32-bit store of one constant:
  CMP AX,imm16 was `lui; addi; sw; li; sw`, and is `lui 0x8d50; addi 3; sw` [verified: the
  disassembly, the Korvo-1's command line]. INC and DEC merge too; ADC and SBB, whose operation
  depends on CF, store the two halves apart. Every value fits: all 105 stores in `i386.c` and
  `i386_fast.h` are a `CC_` constant, a mask of the six flags (8D5h at most), 0, or one of those
  ANDed in [verified: read], and `_Static_assert` checks every `CC_` value, the six flags, and
  that the two fields make one aligned word. No value changes; the lockstep tester's hook copies
  the fields by value, and the sampler reads none.
* **Group opcodes through tables of labels** (80h to 83h, C0h, C1h, D0h to D3h, F6h, F7h). The
  handler decodes the operand and jumps to the operation through a table of 8 labels by modrm's
  reg field, a table for each form of the operand, `static const DRAM_ATTR` as `ops[]` is: 22
  tables, 704 bytes of internal RAM, none in `.rodata` [verified: `objdump -h`, `nm`]. It was a
  `switch`, which ESP-IDF's `-fno-jump-tables` made a chain of compares. The operand goes to the
  operation in `pfx` (now `uintptr_t`, the same 32 bits on the boards), which the next dispatch
  resets. A variable of its own was tried first: GCC lets any computed goto reach any label whose
  address is taken, so such a variable is live at every dispatch, and GCC kept it on the stack;
  memory forms got dearer (CMP [m],imm8 +5) and `fast_exec()` 7 KB larger [verified: mb, sizes].
  An immediate is read by the operation, after the operand's bytes as before, so 80h/82h, 81h and
  83h have operations of their own. The shifts have a handler for each place their count comes
  from (an immediate, one, CL); with a memory operand the count is in `gcnt`.

**Tests** (`tests/host/cpu/test_fast_cpu.c`, `run.sh` 24)

* Mode o, hand-written, at level 0 with paging: an LDT 4 bytes into a page and a second GDT 5
  bytes into one, each with an entry across the page and the second page not present, loaded by
  MOV, POP and LES (a page fault with CR2 at the second page, error code 0), by LAR (the fault is
  ZF clear, CR2 set), and IRETD to a CS there (CR2 at base + offset + 4); tables in the VGA
  window, in the frame buffer, in RAM's last 8 bytes and past RAM (#GP); entries at 0FF4h and
  0FF5h in their page; null selectors into FS, GS and ES by MOV, POP and LES. Its handlers log
  every page fault and #GP, the code logs what each segment points at, and both machines' logs
  must be what the layout says [verified: 40 seeds, and seeds 41 to 340].
* Mode o also calls `read_desc()` directly against a copy of v5.0's, 300 random trials a seed
  (tables in RAM, across pages, in MMIO, at RAM's end and past it, near 4 GiB; paging and WP on
  and off; the TLB emptied or filled near the entry): the same result, words, exception, CR2, TLB,
  page tables and MMIO accesses. 12,000 trials in 40 seeds, about 3,550 of them in RAM in one
  page [verified].
* The random code emits 82h, 80h's alias, too: one 80h in four, chosen without taking a number
  from the generator, so that each scenario's code is otherwise what it was. A first form took
  one, which moved every scenario's code, and seed 317 then met, in the Korvo-1 build, the v5.0
  difference that WI-2 removes: REP MOVSB from page 502h into the frame buffer at E00026C3h, the
  two pages at one index of the 256-entry TLB, and the source's accessed bit set again after the
  guest cleared it [verified: the same with v5.0's code; stepped to that instruction].
* Six planted faults, each caught [verified: 40 seeds, the Korvo-1 header]: the one lookup without
  its in-page test (`rdpage`, 72 FAIL lines), MMIO taken for RAM (`rdmmio`, 120), RAM's end not
  tested (`rdend`, 40), FS's null bit not kept (`nullfs`, 40, every seed), ROL and ROR swapped in
  the group 2 tables (`grp2swap`, 523), ADC's entry on ADD (`grp1op`, 520). `run.sh` plants them
  with `fcfault()`, which takes a copy of `i386.c` as well as of `i386_fast.h`.

**mb, v5.0 to now** [verified: the board's compiler, every build ending on the same guest state,
5,214,225 instructions]

| form | v5.0 | now | fast_cpu = 0: v5.0 | now |
|-|-|-|-|-|
| MOV ES,r (protected mode, 32-bit code) | 298.4 | 165.4 | 290.1 | 165.1 |
| LES r,[m] (16-bit) | 356.4 | 226.4 | 519.1 | 394.1 |
| PUSH DS + POP DS, a guest instruction | 197.9 | 131.4 | 242.1 | 179.6 |
| CALL FAR + RETF, a guest instruction | 793.7 | 601.2 | 686.6 | 498.6 |
| CMP r,imm8 (32-bit; 16-bit 67.8 to 51.8) | 65.8 | 50.8 | 113.6 | 112.6 |
| SHL r,imm | 71.9 | 56.9 | 93.7 | 91.7 |
| SHL r,1 | 68.9 | 52.9 | | |
| SHR r,CL | 66.4 | 54.5 | 88.2 | 86.3 |
| ADD r,imm8 | 56.9 | 47.9 | | |
| TEST r,imm | 55.4 | 49.4 | | |
| ADD [m],imm8 | 116.4 | 108.4 | | |
| CMP [m],imm8 | 107.3 | 106.3 | | |
| TEST [m],imm8 | 100.3 | 97.4 | | |

* No group form is worse; the 53 other arithmetic forms are 3.6 better on average (the lazy
  flags' store, and the layout); all 165 forms 4.4 better on average. With the fast interpreter
  off, no form is worse and all are 4.4 better on average [verified].
* Six string forms are worse with the fast interpreter on: MOVSB and MOVSD +3, MOVSW +2, REP STOSD
  of 4, 16 and 256 elements +5, +17 and +257 [verified]. Their source (`movs:`, `stos:`) is not
  changed here; the extra instructions are loads of locals that GCC now keeps on the stack (REP
  STOSD's value, `lw a5,72(sp)` once an element) [verified: the listings]. It came with the lazy
  flags' change, which changes code all through `fast_exec()` [verified: mb of that commit
  alone]. WI-2 rewrites both loops (REP STOS fills by word stores); the merged tree should be
  measured again [inferred].
* The count brought up to date costs 13 host instructions a segment load [verified: mb without
  it]; the frameless `read_desc()` won back more than that.

**Counts under the harness** (core 1, host instructions a guest instruction; the counting harness
of the v5.1 ideas work, `i386.c` built with the board's compiler; v5.0's figures are that work's
runs) [verified]

| | v5.0 | now |
|-|-|-|
| BENCH without A4 and the boot | 71.03 | 68.61 |
| Windows 95's boot, the first 300 M instructions | 76.5 | 73.2 |

Each 50 M of the boot is 2.9 to 5.1 lower; `i386.c` alone 73.1 to 69.8. Both runs end where v5.0's
did (the harness's final screen the same) [verified]. That is 3.4 per cent fewer host
instructions on BENCH and 4.3 per cent on the boot; the board should gain at most about that, less
where waits for PSRAM are a large share of the time [inferred]; how much is [unknown] until Plat
runs it.

**Internal RAM** [verified: in-tree builds, ESP-IDF v6.1-dev-8142-g188e3e55bb, the size report's
DIRAM; every build exits 0 with no new warning]

| left for ESP-IDF | v5.0 | WI-3 | v5.1's `v51-ram` | `v51-ram` with WI-3 |
|-|-|-|-|-|
| Korvo-1 (dual boot and standalone) | 241,444 | 241,614 | 126,763 | 128,611 |
| DOS board | 239,202 | 239,372 | 128,931 | 130,779 |

* `fast_exec()` 121,434 to 120,654 bytes; `cpu_exec1()` 132,338 to 130,660; `i386.c`'s internal
  RAM code 147,618 to 146,696 (with `v51-ram` 279,956 to 277,356); its internal RAM data 3,096 to
  3,800 (the tables) [verified: the boards' `i386.c.obj`].
* No `i386.c` function goes to flash that v5.0 had in internal RAM, on either board, with or
  without `v51-ram` [verified: the maps].

**Checks** [verified]

* The lockstep tester against v4.6, fast_cpu 1 and 0: b1, restart, msdos, bench, vbe, t386mon and
  t386mon800, no difference in any of the 14, each ending on v5.0's gate figures and disk hashes
  (588,301,033; 1,304,341,904; 1,337,723,352; 31,345,925; 55,090,157; 674,307,567; 792,272,554).
* v4.6's three Windows runs on the PC repro (`regress50.sh`), fast_cpu 1 and 0: v4.6's counts;
  every log line (but the host's own memory address), picture and disk check as v5.0's own runs.
* The lockstep tester's self-test, `FAST=1` and `FAST=0`: every planted bug named at the
  instruction section 6 of its README gives, every run without a bug the same.
* `tests/host/run.sh`: 82 passed; `test_fast_cpu` on seeds 41 to 340 in both builds, 6,300
  scenarios each, none failed.

**For the merge.** `pfx` is `uintptr_t` now (WI-1 and WI-2 touch `NEXT`, `DISPATCH` and the
`FPTR` macros, not its type). The new `ops[]` lines are override lines at the end of the
initializer. `read_desc()` and `FAST_LOAD_SEG()` are this item's; `stos:` and `movs:` are not
touched. Conflicts are expected only in `test_fast_cpu.c`'s mode list (mode o's lines are each on
their own) and `run.sh`'s fault list.


### 13.4 The lean TLB refill (WI-4, branch `v51-refill`)

Written 2026-10-28. When a guest access misses both TLBs, the fast interpreter now refills
tiny386's TLB itself, inline in `fast_miss()`, with tiny386's own walk of the page tables; and
`fast_tlb_flush()` clears four indexes a turn. Nothing was flashed, pushed or published for it.

* **Why.** Windows loads CR3 two to four times a picture in T386MON's fill and scroll, which
  empties both TLBs. tiny386's TLB is then refilled 71.4 times a 1,000 guest instructions in fill,
  63.8 in scroll and 103.2 in full copy [verified: `PROFILE-v51.md` 1.9]. Each refill went
  `fast_miss()`, `fast_lpgno()`, `translate_lpgno()`'s body, `tlb_refill()`, `fast_tlb_slot()`:
  three calls with their saves, and checks made twice.
* **What changed** [verified: `i386.c`, `i386_fast.h`]:
  * `tlb_refill()`'s walk is written once, as the macro `TLB_WALK`, and made twice: `tlb_walk()`
    (LEAN 0), which `tlb_refill()` calls, and `tlb_walk_lean()` (LEAN 1) for the fast
    interpreter. Both make the same checks, loads and stores in the same order: `pt_addr_ok()` on
    the directory entry's address, the entry loaded and tested, its accessed bit set, then the
    same for the table entry, then the fast interpreter's two entries at the index dropped and
    tiny386's entry filled. The directory entry's accessed bit is set before the table entry is
    loaded, which matters when a directory is its own page table.
  * The lean copy drops the fast interpreter's two entries inline instead of calling
    `fast_tlb_slot()`. It reads `phys_mem` and its size once: the walk's stores go into guest page
    tables, never into `*cpu`, a TLB or the fast interpreter's entries (`pt_addr_ok()` keeps them
    inside guest memory and, on the boards, out of the device window at A0000h, where tiny386's
    TLB may be). And it tests that window with one shift, `(addr >> 17) == 5`, the same addresses.
  * `translate_lpgno()`'s fault is `lpgno_fault()`, and its rights check and dirty bit
    `LPGNO_RIGHTS`, made twice in the same way. `fast_translate()` is `translate_lpgno()`'s body for
    `fast_miss()`, with the lean walk where `translate_lpgno()` calls `tlb_refill()`. It reads the
    privilege level after the walk, so that fewer values are held across it.
  * Long mode keeps the call of `fast_lpgno()`. `fast_window()` is not changed: a new code page is
    still refilled through `fast_lpgno()`, until WI-1 sends code pages through `fast_miss()`.
  * `fast_tlb_flush()` clears both tables' tags at an index together, four indexes a turn.
* **Why macros, not a function with a flag.** The first form was one always_inline function with
  a `lean` argument, a constant at each call. GCC 16.1 then compiled `tlb_refill()` and five
  functions it is inlined in with their registers allocated otherwise; and with the argument on
  `translate_lpgno()`'s body it inlined `tlb_refill()` everywhere, `cpu_exec1()` 54 KB larger
  [verified: the bench build's objects]. With the macros the code of tiny386's own functions is
  v5.0's (below).
* **The same code for tiny386** [verified: `objcmp.py`, every function of `i386.o` but the fast
  interpreter's, disassembled with relocations, addresses made relative to the function]: in the
  Korvo-1's ESP-IDF build and in the bench build, `cpu_exec1()` and `tlb_refill()` are the same
  code as v5.0's. 16 other functions in the ESP-IDF build (6 in the bench build) differ in one
  thing only: the line number they give `assert()`, 99 more, as `i386.c` has 99 more lines above
  them. The DOS board's build and the builds with `v51-ram` merged are below.
* **What it costs** (host instructions, the board's compiler, GCC 16.1 with `-mtune=esp32s31`
  on `i386.c`, counted under qemu as section 7) [verified: a scratch copy of `tests/host/lfb`
  with the frame buffer mapped through page tables, the hot-spots design's `bench_ptouch`, and a
  `bench_pflush` added to it here]:

  | | v5.0 | v5.1, WI-4 |
  |-|-|-|
  | a turn of a 4-instruction loop whose load misses both TLBs (512 pages in turn) | 457.2 | 386.3 |
  | the same turn when every load hits | 249.4 | 249.4 |
  | so a miss with refill | 207.9 | 136.9 |
  | a turn with MOV CR3 (both TLBs emptied) and one load | 3,673.3 | 2,755.3 |
  | of which `fast_tlb_flush()` (the old loop with the new refill: 3,589.3) | about 2,070 | about 1,240 |

  On the way [verified, the same bench]: the walk inlined through `translate_lpgno()`'s body with
  a flag, 157.9; `fast_translate()` with the window's shift, 145.9; the level read after the walk,
  136.9.
* **Counts** (the counting harness, `i386.c` by the board's compiler, section 7's method)
  [verified]:

  | work | v5.0, core 1 | WI-4, core 1 |
  |-|-|-|
  | BENCH without A4 and the boot | 71.03 | 69.54 |
  | BENCH's A9 (a TLB miss every time) | 148.53 | 120.22 |
  | BENCH, the whole run | 79.18 | 78.25 |
  | Windows' boot, first 300 M | 76.50 | 76.06 |

  Each run's guest output is v5.0's line for line, so it ends on the same state and count.
  `mb` (`IDEAS-v51.md` 7): 163 of its 165 forms the same, none more, and CALL and RET near 7.0
  fewer in 16 and 32-bit code. Each of those changes code page, through `fast_window()` and
  `fast_lpgno()` [verified: `mb`'s listing of the piece]; only `fast_window()` calls
  `fast_lpgno()` now, so GCC compiles a copy of it for a read alone (`fast_lpgno.constprop.0`)
  [verified: `nm`], which is where the 7 go [inferred]. Both builds end on 5,214,225 instructions
  with the same registers.
* **Internal RAM** [verified: the size reports, `nm`]: `fast_miss()` 350 to 688 bytes,
  `fast_tlb_flush()` 72 to 92, `fast_lpgno()` 452 to 384 (only `fast_window()` calls it now, with a
  constant), `tlb_refill()` 234 and `fast_exec()` the same: 284 bytes more of DIRAM on both boards.

  | build | DIRAM used | left for ESP-IDF |
  |-|-|-|
  | Korvo-1, v5.0 (0d37002) | 262,300 | 241,444 |
  | Korvo-1, WI-4 (dual boot and standalone) | 262,584 | 241,160 |
  | DOS board, v5.0 | 264,542 | 239,202 |
  | DOS board, WI-4 | 264,826 | 238,918 |
  | Korvo-1, `v51-ram` (29c07a0) | 376,981 | 126,763 |
  | Korvo-1, WI-4 with `v51-ram` merged | 377,265 | 126,479 |
  | DOS board, `v51-ram` | 374,813 | 128,931 |
  | DOS board, WI-4 with `v51-ram` merged | 375,097 | 128,647 |

  `i386.o`'s `.iram1` sections: 147,618 to 147,902 bytes (279,956 to 280,240 with `v51-ram`).
  `fast_exec()` 120,198 bytes (120,194 with `v51-ram`), `cpu_exec1()` 132,126 in flash (123,782 in
  internal RAM with `v51-ram`), both the same as before. Every build exits 0 with the same
  warnings as before (the Korvo-1's `i2s_task` unused, the DOS board none). No function of
  `i386.c` is in flash that was not there before: the same 42 on both boards (41 with `v51-ram`,
  which moves `cpu_exec1()` out) [verified: `nm` of each `tiny386.elf` against the functions of
  `i386.c.obj`]. In all four builds (each board, with and without `v51-ram`) `cpu_exec1()` and
  `tlb_refill()` are the same code as before and 16 more functions differ only in their line
  numbers for `assert()`, 99 more [verified: `objcmp.py`, `linecheck.py`]. Compiled by the
  host's gcc with `-Wall -Wextra`, `i386.c` gives v5.0's warnings and no other, with the board's
  header, with `I386_FAST` alone and with neither [verified].
* **The same guest** [verified]:
  * `tests/host/cpu/test_fast_cpu.c` mode n: first `fast_miss()` itself against
    `translate_lpgno()`, access by access (900 random actions a seed: accesses at levels 0 to 3,
    the page tables changed without INVLPG, directory entries pointed beyond RAM, at A0000h or at
    the directory, CR0.WP changed, CR3 pointed beyond RAM or at A0000h), comparing the result, CR2,
    the exception and its error code, all memory, tiny386's whole TLB and the fast TLB's
    invariants after each; then hand-written code through both interpreters (the directory as its
    own page table with accessed bits cleared without INVLPG, a directory entry and a table entry
    not present, a table beyond RAM and at A0000h, rights faults at level 3 on user and
    supervisor pages and at level 0 with CR0.WP clear and set, reads and writes), whose page fault
    handler logs CR2 and the error code: the log must be the planned 14 faults (13 without the
    board's header, which allows the table at A0000h). It passes on v5.0's code too.
  * `tests/host/run.sh`: 80 tests pass (the base's 76 and four new), in 1,177 s. Section 24 then
    puts four faults into the lean copies, and mode n catches each: the directory entry's
    accessed bit set after the table entry is loaded (37 of 40 seeds), the fast interpreter's
    entries left by the walk (40), no dirty bit on a store that refills (40), tiny386's entry
    kept after a rights fault (40). The first and the last only mode n's probes catch: wherever
    guest code meets them tiny386 does the faulting instruction again and sets what was left, so
    the older modes and mode n's own code run with them agree (10 seeds each, checked by hand).
  * `test_fast_cpu` on seeds 41 to 340, every mode: 6,300 scenarios in each build, no difference;
    with the Korvo-1's header 81.8 M guest instructions, 1,153,242 step ends compared, 9.3 M fast
    TLB entries checked; mode n's probes 188,970 accesses, 170,108 of them refills and 134,639
    page faults.
  * The lockstep tester against v4.6, the seven workloads with the fast interpreter on and off:
    no difference in any of the 14, each ending on v4.6's count and disk (b1 588,301,033, restart
    1,304,341,904, msdos 1,337,723,352, bench 31,345,925, vbe 55,090,157, t386mon 674,307,567,
    t386mon800 792,272,554); its self-test passes with `FAST=1` and `FAST=0`.
  * The three Windows 95 runs (`regress50.sh`'s), on and off: 588,301,033, 1,304,341,904 and
    1,337,723,352 instructions, each to the desktop.
* **On the board** [inferred: the counted savings times the profile's rates]. About 71 host
  instructions a refill through `fast_miss()` and 830 a flush: in fill about 5 host instructions a
  guest instruction (of 201), scroll about 5 (of 174), full copy about 7 (of 282), sprite about
  1.3, text about 0.6 (Windows' boot: 0.44, counted above). So fill, scroll and full copy about 1
  to 3 per cent faster, the lower end likelier, since the walk's two loads from PSRAM stay.
  Before WI-2 is merged, the frame buffer's REP pieces gain too (their first store misses the
  fast TLB and hits tiny386's): the bench's paging fill 3.6 per cent fewer host instructions, its
  paging copy 7.8 [verified]; WI-2 takes those misses away. How much faster the board is stays
  [unknown] until Plat's board runs T386MON.
