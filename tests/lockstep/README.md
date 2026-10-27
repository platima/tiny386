# Lockstep tester

A tool that proves a changed CPU core gives the guest exactly what tiny386's own interpreter
gives it, instruction for instruction, through a whole Windows 95 boot. It runs two builds of
the PC harness (a reference tree and a candidate tree) on the same workload, from the same disk
image, with the same scripted keys, and compares what each did. If they differ, it runs both again
around the first difference and names the first instruction whose result differs, with both
states.

It was written for v5.0's three changes (tab5-pc's fast interpreter, a fast path for stores to the
VBE frame buffer, a segment descriptor cache), but it takes any two trees the harness can build.
Nothing in a tree is changed: the hook goes into copies of five of its files at build time. v5.0
took in the first two (the descriptor cache was left out: it saved under 1 per cent); section 9
is v5.0 against v4.6, the release's gate.

Tags: [verified] seen or measured here, by the method named; [inferred] reasoned, not run.


## 1. What you need

* WSL Debian with gcc, python3, binutils (`objdump`, `as`, `ld`) and mtools.
* The PC harness, `~/org2/pcm/canary/harness` by default (`HARNESS=` for another): the copy of
  `~/win95-debug/pc-repro/harness` whose `build.sh` builds v4.x trees. It is not in this tree.
* The images, never in this tree and never to be published: the PC repro's pristine Windows 95
  image (`W95_IMG`, default `~/win95-debug/pc-repro/img/win95.pristine.img`) and a FreeDOS disk
  with BENCH.COM run from FDAUTO.BAT (`BENCH_IMG`, default `~/org2/jit/bench/run/bench-hd.img`).
  Every run works on its own copy, which is deleted after it.
* For the `t386mon` workload, `T386MON.EXE` (`T386MON_EXE`): Plat's monitor and benchmark for
  Windows 95 on tiny386, github.com/platima/T386Mon, built by its `build.sh` or taken from its
  release.


## 2. Use

```
# the reference (s31-v4.6) against a candidate, on the boot to the desktop
bash tests/lockstep/lockstep.sh ~/fw/tiny386-s31 ~/path/to/candidate b1 ~/out/lock-b1

# the other workloads
bash tests/lockstep/lockstep.sh REF CAND restart OUT
bash tests/lockstep/lockstep.sh REF CAND msdos OUT
bash tests/lockstep/lockstep.sh REF CAND bench OUT
bash tests/lockstep/lockstep.sh REF CAND vbe OUT
T386MON_EXE=~/T386MON.EXE bash tests/lockstep/lockstep.sh REF CAND t386mon OUT

# a v5.0 candidate with its fast interpreter off: build that side alone, then give it
EXTRA_CFLAGS=-DI386_FAST_DEFAULT=0 bash tests/lockstep/build.sh CAND ~/out/cand-off
CAND_BUILD=~/out/cand-off bash tests/lockstep/lockstep.sh REF CAND b1 ~/out/lock-b1-off

# the tool's own test: seven planted bugs, each must be found (or is skipped, and says why)
bash tests/lockstep/selftest.sh . ~/out/selftest
FAST=0 bash tests/lockstep/selftest.sh . ~/out/selftest-off
```

A v5.0 tree builds with its fast interpreter on, as the boards do (the harness's `esp` flavour
takes `board_korvo1.h`, which defines `I386_FAST`); `EXTRA_CFLAGS` reaches every compile of a
build, so a build with it off is made on its own and given as `CAND_BUILD` (or `REF_BUILD`).

`lockstep.sh` exits 0 when the two agree everywhere, 1 when it found a difference, 2 on an error.
`OUT/report.txt` has the whole story; `OUT/p1.txt`, `p2.txt` and `p3.txt` each pass's comparison.

Settings (environment): `EVERY` (instructions between records in pass 1, 1,000,000), `PASSES=1`
(stop after pass 1, for a quick yes or no), `REF_BUILD` and `CAND_BUILD` (use a `build.sh` output
as it is), `IGNORE` (fields left out of the comparison, with commas), and `REF_SED_I386`,
`CAND_SED_I386` and the like for `PC`, `VGA`, `IDE`, `I8042`, `I8254`, `MISC` (a sed script for one
side's copy of that file; the self-test plants its bugs this way), and `I386_FAST` for
`i386_fast.h` (a header, so `build.sh` puts its sed into the patch it makes).

The parts can be used alone:

| script | what it does |
|-|-|
| `build.sh TREE OUT` | the harness's headless driver for one tree, with the hook (5 s) |
| `run.sh BUILD WORKLOAD RUNDIR` | one run; the hook's files in `RUNDIR/ls`; `NOHOOK=1` runs with the hook off |
| `compare.py records A B` | compare two runs' records and interrupts |
| `compare.py children A B` | compare two split steps, prefix by prefix |
| `lockstep.sh` | the three passes below |
| `selftest.sh` | the planted bugs |


## 3. How it works

### 3.1 The hook

`build.sh` makes a patch that adds one `#include` at the end of copies of five files, and links
with `--wrap=cpui386_step --wrap=pc_new`:

| file | what is added | why |
|-|-|-|
| `i386.c` | `lockstep_cpu.inc` | reads `struct CPUI386`: registers, EIP (`next_ip`), EFLAGS with the lazy flags worked out by tiny386's own `refresh_flags()` (whose one write to `cpu->flags` is put back at once), CPL, segment selectors with their cached bases, limits and flags (LDT and TR too), GDT, IDT, CR0, CR2, CR3, DR0 to DR7, the halt and interrupt-waiting flags, the instruction count |
| `i8259.c` | `lockstep_pic.inc` | both PICs' registers |
| `i8254.c` | `lockstep_pit.inc` | the timer's three channels |
| `fpu.c` | `lockstep_fpu.inc` | a hash of the x87's state, field by field, when the ini turns it on |
| `pc.c` | `lockstep.c` | the hook itself |

Every step of the CPU (`pc_step()`'s call of `cpui386_step()`, whatever core is behind it) passes
through `__wrap_cpui386_step()`. At the first step the hook puts its own functions in front of the
CPU's callbacks (`cpu->cb`): every port access, every memory-mapped access and every hardware
interrupt the CPU acknowledges passes through them unchanged. The hook only reads; with `LS_OUT`
unset the program runs as the harness's own build does.

[verified] It changes nothing the guest sees: b1 ends on 588,301,033 instructions, restart on
1,304,341,904 and msdos on 1,337,723,352 with and without the hook, as v4.6's regression did, with
the same disk image after the run.

### 3.2 What is recorded

`records.txt`, one line per record, `key=value`. A key that starts with `_` is shown but never
compared; every other key is compared.

| key | what |
|-|-|
| `i`, `step`, `cyc`, `ut` | the record's number, steps done, instructions done, the guest's clock (µs) |
| `eip` ... `edi`, `fl`, `cpl`, `c16`, `spm`, `halt`, `intr` | the CPU |
| `es` ... `gs`, `ldt`, `tr` | `selector:base:limit:flags` |
| `gdt`, `idt`, `cr0`, `cr2`, `cr3`, `dr`, `sys`, `fpu` | the rest of the CPU |
| `sh` | a hash of the CPU's state after every step since the start (each state it passes through at a step boundary, once) |
| `ram`, `rr` | a hash of guest RAM, and one for each MiB (the devices' window A0020h to BFFFFh, where the ESP-IDF port keeps their state, is left out) |
| `vram` | a hash of video memory |
| `fb` | a hash of the picture the VGA code drew (the harness's frame buffer) |
| `dirty` | a hash of the marks of what was written into video memory since the last picture (`VGA_DIRTY_BLOCKS`): which rows the next picture draws again |
| `pic`, `pit` | the PICs' and the timer's registers |
| `irqn`, `irqh` | interrupts taken so far, and a hash of each one's instruction count, vector and EIP |
| `ion`, `ioh` | port accesses so far, and a hash of each (direction, size, port, value); a string transfer counts as its single accesses, so a core that does them one at a time gives the same hash |
| `dk` | the harness's disk counters: reads, writes, bytes read and written, seeks, flushes |
| `_mmn`, `_mmh` | memory-mapped accesses: shown, not compared, because a fast path may store to video memory without the callbacks; what it stored is compared in `vram`, `fb` and `dirty` |
| `_ip`, `_exc`, `_cc`, `_wall`, `_why` | dead state (the last instruction's start, the last exception, the lazy flags' own form), host seconds, why the run ended |

A record is written at the first step boundary at or after every `LS_EVERY` instructions, after
every step whose end lies in `[LS_DENSE_FROM, LS_DENSE_TO]` (in a halt, where the instruction count
stands still, without the memory hashes, in both runs alike), and when the program ends (`F`: at
its end, at `LS_EXIT_*`, or at an emulator abort or crash, which the hook catches before the
harness's own handler). `irq.txt` has every hardware interrupt taken (instruction count, vector,
the CS:EIP and flags it interrupts, the clock). `io.txt` has every port access in the dense window.

The hook's settings (environment, read when the PC is built; `run.sh` and `lockstep.sh` set them):

| variable | what |
|-|-|
| `LS_OUT` | the directory for the hook's files; the hook is off without it |
| `LS_EVERY` | a record every so many instructions (1,000,000; 0 for none) |
| `LS_DENSE_FROM`, `LS_DENSE_TO` | instruction counts: a record after every step that ends between them, and `io.txt` |
| `LS_SPLIT_STEP` | S: fork a child for each prefix of step S+1 (`children.txt`); `LS_SPLIT_MAX` children (the step's length and 2) |
| `LS_EXIT_CYCLE`, `LS_EXIT_STEP` | end the run after the first step past this instruction count, or after this many steps |
| `LS_STOP_BYTE` | `address:value`: log every change of that byte of guest RAM (`M` lines) and end the run when it takes the value (BENCH's and VBETEST's marks) |

### 3.3 The three passes

1. **Pass 1**: both runs to the end, a record every `EVERY` instructions, every interrupt. If every
   record, every interrupt and the disk image after the run agree, that is the answer.
2. **Pass 2**: both runs again (the runs are deterministic), a record after every step from the
   last record that agreed to the first that did not, and every port access there. It names the
   first step whose end differs.
3. **Pass 3**: both runs again to the start of that step, where the hook forks one child for each
   prefix of the step: child m runs the first m instructions (`cpui386_step(cpu, m)`) and writes
   the state, the memory written since the step began (address, old and new bytes), and the next
   instruction's bytes. The first m whose state differs names the instruction; `objdump`
   disassembles it.

A child shares nothing it could change with the parent: every file the process can write (the disk
image among them) is opened again read only in the child alone, its output goes to `/dev/null`,
and it ends with `_exit()`. [verified] A split at a step that writes the disk and at one that reads
it leaves the main run as it was: the same records, interrupts, end and disk image.

### 3.4 Workloads (`workloads.sh`)

| name | what | guest instructions |
|-|-|-|
| `b1` | Windows 95 from power-on to the desktop, then 120 s | 588,301,033 |
| `restart` | to the desktop, Start, Shut Down, Restart, the desktop again | 1,304,341,904 |
| `msdos` | to the desktop, Restart in MS-DOS mode, MEM, DIR, EXIT, the desktop again | 1,337,723,352 |
| `bench` | FreeDOS, then BENCH.COM to its last mark (0AFh at 0000:04F0) | 31,345,925 |
| `vbe` | FreeDOS, then `guest/vbetest.S`: the VBE linear frame buffer at 800x480x16 and 640x480x32, stored to every way the CPU can (bytes, words, dwords, unaligned, across pages, REP STOS and MOVS both ways, read-modify-write, reads, the end of video memory and past it), to its last mark (0BFh) | 55,090,157 |
| `t386mon` (v5.0) | Windows 95 to the desktop, then T386MON (`T386MON_EXE`, put in the image copy's root) from Start, Run at 90 s after the taskbar is first seen, with `/top /bench /quit`: its drawing tests (fill, scroll, text three ways, a sprite, a full copy, a full picture) through the VGA window at 640x480 in 16 colours, its log written, closed; to 320 s after the taskbar. Two key releases that do nothing, at 30 s and 300 s, keep the harness's dialog poller paused in between, so that Internet Explorer's setup wizard waits on its first page and T386MON's window is never sent an Enter | 674,307,567 |

The three Windows runs are v4.6's regression runs: the same options, the pristine image, mem 14M,
vga_mem 320K, gen 3, no FPU. That image has the standard VGA driver (640x480, 16 colours, planar),
so they never touch the linear frame buffer: `vbe` is the workload for a fast path there.


## 4. What a candidate core must keep

The tool compares at step boundaries and after each prefix of a step, so a candidate has to step as
tiny386 does. Where it does not, the tool says so (a different instruction count for a prefix, or a
step boundary at another instruction), rather than a wrong register.

* `cpu->cycle` goes up by one for each instruction, as `cpu_exec1()` counts: once its first byte
  has been fetched (an instruction that faults after that is counted, and again when it runs
  again), and a REP string instruction once.
* `cpui386_step(cpu, n)` takes a waiting interrupt first (if IF), returns at once if halted, then
  runs up to n instructions, and ends early exactly where tiny386 does: after HLT, on an exception
  (delivered before it returns), after IRET or POPF when an interrupt is waiting and IF is set.
  STI runs the next instruction too (`stepcount = 2` when an interrupt waits or the step would end),
  and MOV SS and POP SS add one to the step.
* `cpui386_step(cpu, m)` for m < n runs the first m instructions of what `cpui386_step(cpu, n)`
  would run (pass 3 depends on it).
* At every step boundary the state is in `struct CPUI386` as tiny386 keeps it: the lazy flags in
  `cc`, the next instruction in `next_ip`.
* Port accesses go through `cpu->cb.io_*` and hardware interrupts are acknowledged through
  `cpu->cb.pic_read_irq`. Memory-mapped accesses may bypass `cpu->cb.iomem_*`, if what they leave in
  video memory and in the marks is the same.
* A core that prints costs guest time in this build (the board's console UART is charged, as
  `-uart` says), so a line the reference does not print shows as a difference in `ut`.
* A core in a new `.c` file has to reach the harness's build: include it from `i386.c` (tab5-pc
  compiles `i386.c` and its `t386_fast.h` as one file), or give `HARNESS` a copy of the harness
  whose `build.sh` lists it.

tab5-pc's fast interpreter as published does not keep all of this [verified: read in its
`t386_fast.h`]: it leaves a step when the VM flag changes and after an instruction it hands to
tiny386 when an interrupt is waiting, and it calls its own instruction count approximate around STI
and MOV SS. Its port has to step as tiny386 does for the tool to compare it instruction for
instruction. v5.0's port does (`s31/FAST-CPU.md` 3), and the tool finds no difference between it
and tiny386's own interpreter (section 9).


## 5. Reading a report

From the self-test (bug `page-cross`, b1), pass 3:

```
first instruction whose result differs: instruction 476 of step 385711
  it is at 0028:c0375ffd, cycle 36509248 -> 36509249
  bytes (16 of them, from Windows' own code: left out here)
  mov    ecx,0xffffffff   (b9 ff ff ff ff)
  before (both): eip=c0375ffd eax=c13b2068 ecx=00000000 ...
  after ref : eip=c0376002 eax=c13b2068 ecx=ffffffff ...
  after cand: eip=c0376002 eax=c13b2068 ecx=0d8bffff ...
  fields that differ: ecx
```

The instruction's immediate runs across a page boundary in Windows' own code, and the planted bug
fetched its last two bytes from the wrong page.


## 6. The self-test

Each file in `bugs/` is one line of sed with a header (`# file:`, `# workload:`, since v5.0 `#
needs:` for a bug only one of the two interpreters runs, then what it is), and plants its bug in
the candidate's copy of the file only. `FAST=1` (the default) builds a v5.0 tree as it is, with its
fast interpreter on; `FAST=0` with it off. A bug that needs the other is skipped, and the summary
says so. Before v5.0 [verified, 2026-10-27, on v4.6, 76 s for all five and the two runs without a
bug]:

| bug | where it showed | what the tool named | the candidate's own run |
|-|-|-|-|
| `af-dec32`: AF after a 32-bit DEC wrong | b1, SeaBIOS, instruction 27,676 | `dec edx`, AF | the desktop, ending on the same instruction with the same disk |
| `movs-count`: REP MOVS one element short at a page's end | b1, SeaBIOS, instruction 8,283 | `rep movsb`, the byte at DB4FFFh | aborts in SeaBIOS at 2,092,570 |
| `page-cross`: a 32-bit access across a page not split | b1, Windows' VMM at 0028:C0375FFD, instruction 36,509,249 | `mov ecx,0xffffffff`, ECX | no desktop by 900 M instructions |
| `sti-shadow`: two instructions after STI, not one | b1, SeaBIOS, instruction 1,206,582 | `sti`, the prefix ran one more instruction | the desktop 2 s of guest time later |
| `lost-mark`: a dword store to the frame buffer marks no block (in `pc.c`) | vbe, instruction 12,120,836 | `mov fs:[edi],eax` to FEA00000h, `dirty` | the guest and the disk as the reference's; only the picture is stale |

`af-dec32` and `lost-mark` are the kind a boot test passes: only the comparison shows them. The
same workloads with no bug show no difference. `page-cross` does not show on `bench` (BENCH never
makes such an access across pages that are not next to each other), which is right.

**v5.0** [verified, 2026-10-27, on v5.0's tree, `FAST=1` and `FAST=0`, both passed]. `lost-mark`
went: v5.0's CPU stores into the frame buffer itself, so `pc.c`'s mark is never reached there, and
`lost-mark-cpu` plants the same bug where v5.0 stores (`lfb_marked()` in `i386.c`). `sti-shadow`
now changes the STI line as v5.0 has it, and only tiny386's own interpreter runs that line (the fast
interpreter keeps the rule itself). Two bugs go into `i386_fast.h`:

| bug | `FAST=1` | `FAST=0` |
|-|-|-|
| `af-dec32` | b1, instruction 27,676, `dec edx`, AF | the same |
| `movs-count` | b1, instruction 144,807,271, Win16 code at 02F7:3045, `rep movsb`, video memory | b1, instruction 8,283, as before |
| `page-cross` | b1, instruction 36,509,249, as before | the same |
| `sti-shadow` | skipped (needs tiny386's own) | b1, instruction 1,206,582, as before |
| `lost-mark-cpu`: a dword store to the frame buffer marks no block (in `i386.c`) | vbe, instruction 12,120,836, `mov fs:[edi],eax`, `dirty` | the same |
| `fast-fold`: CMP or TEST fused with a JZ or JNZ where tiny386 would not (the bug `tests/host/cpu/test_fast_cpu.c` found in the port) | b1, SeaBIOS, instruction 6,908, `test eax,eax`, EIP (the jump joined the step) | skipped (needs the fast one) |
| `fast-sync`: the instruction count not brought up to date before a port access | b1, SeaBIOS, instruction 488,271, `out 0x40,al`, the timer's load time | skipped (needs the fast one) |

With the fast interpreter on, `movs-count` shows later: the fast interpreter does most REP MOVS
itself, and the first one it hands to tiny386 that ends a piece at a page's end is in Windows' 16-bit
code.


## 7. Measured

[verified, 2026-10-27, this machine, WSL; v4.6 (35a201c) against itself]. Host seconds of one run
alone, with the machine quiet at first (b1, restart) and then shared with other work (a load of
about 19 on 28 threads) for the rest:

| workload | guest instructions | without the hook | with the hook | `lockstep.sh`, pass 1 | records | interrupts |
|-|-|-|-|-|-|-|
| b1 | 588,301,033 | 6.0 s | 7.5 s | 10.9 s (loaded) | 590 | 51,878 |
| restart | 1,304,341,904 | 23.0 s | 28.7 s | 50.5 s (loaded) | 1,306 | 238,269 |
| msdos | 1,337,723,352 | 32.4 s (loaded) | 47.5 s (loaded) | 46.8 s (loaded) | 1,339 | 238,758 |
| bench | 31,345,925 | | 0.4 s | 0.6 s | 33 | 111 |
| vbe | 55,090,157 | | 0.7 s | 0.9 s | 57 | 505 |

Every workload agrees with itself, both with the same program on both sides and with a candidate
built at `-O1` (another program from the same source); the end, every record, every interrupt and
the disk image after the run are the same. A found difference costs one more run of each side up
to it (pass 2) and one more to the step (pass 3, 515 children, 2 to 3 s): the planted AF bug
took 23 s on b1 from the start to the named instruction, the two builds included.


## 8. Limits

* A difference that heals between two records shows in pass 1 only if it passes a step boundary
  (`sh` takes in the CPU's state after every step) or reaches memory, ports or interrupts. A flag
  that is wrong inside a step and overwritten before the step ends, and never read, is not seen;
  it also changes nothing the guest can see.
* The VGA's registers and latches and the other devices' state (IDE, keyboard, CMOS, DMA) are not
  hashed themselves, only through the port accesses that change them and what they do after.
* The x87 is compared only when the ini turns it on; the workloads run without it, as Plat's
  board does.
* Pass 3's children cannot write the disk: a child whose prefix writes it sees an error the main
  run does not. Both sides' children see the same, so the comparison stays fair, but a state after
  such a write is not the main run's.
* Both runs must use the same harness, image and options: `lockstep.sh` runs both sides with one
  harness and one workload, and stops if an image prepared for the run (vbe) is not the same bytes
  on both sides.
* `LS_SPLIT_STEP` splits one step; for the next, run again.
* A planted bug is found only where a workload runs it: `sti-shadow` with the fast interpreter on,
  and the fast interpreter's own bugs with it off, are skipped for that reason (section 6).


## 9. v5.0 against v4.6

The release gate of v5.0 [verified, 2026-10-27, WSL]: v4.6 (35a201c) as the reference, v5.0's
integrated tree (`s31/BUILD-NOTES.md` 33) as the candidate, built twice: as the boards build it,
with the fast interpreter on, and with `-DI386_FAST_DEFAULT=0`, tiny386's own interpreter alone,
which is what `[esp] fast_cpu = 0` runs. Pass 1 of every workload; no difference anywhere, so no
pass 2 or 3 was needed:

| workload | guest instructions | records | interrupts | disk image after (sha256) | host s, v4.6 / fast on | v4.6 / fast off |
|-|-|-|-|-|-|-|
| b1 | 588,301,033 | 590 | 51,878 | `248cdd0473f55199` | 12.1 / 8.3 | 12.1 / 12.5 |
| restart | 1,304,341,904 | 1,306 | 238,269 | `0991749feee656d8` | 36.0 / 30.6 | 35.5 / 35.2 |
| msdos | 1,337,723,352 | 1,339 | 238,758 | `36979e52115af450` | 35.5 / 30.4 | 34.9 / 35.2 |
| bench | 31,345,925 | 33 | 111 | `72fe423a80f365fb` | 0.5 / 0.4 | 0.5 / 0.5 |
| vbe | 55,090,157 | 57 | 505 | `482c032f53c3a479` | 0.6 / 0.5 | 0.6 / 0.6 |
| t386mon | 674,307,567 | 676 | 64,677 | `35c2db9b72859c03` | 13.5 / 11.1 | 13.4 / 13.1 |

In each, both sides end on the same instruction at the same CS:EIP, every record agrees (the CPU
with its segment caches, RAM by MiB, video memory, the picture, the dirty marks, the PICs, the
timer, the disk counters, every port access, the hash of the CPU's state after every step) and so
does every interrupt. Twelve comparisons, about 4.0 thousand million guest instructions for each of
the two candidates. The host seconds were taken with other work on the machine, so they say only that the fast
interpreter is in the candidate's build; `gdb` showed `fast_exec()` running from the first step
in the build with it on, and never in the build with it off.

What the gate does not cover: the board itself (PSRAM, two cores, the card, USB), the fast
interpreter's paths that none of these workloads reach (`s31/FAST-CPU.md` 6 lists what its own
host test covers), and Windows at 800x480 through VMDisp9x, whose image is not in WSL (`vbe` and
`tests/host/lfb` cover the frame buffer's paths).
