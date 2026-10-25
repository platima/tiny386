# Review of v4.6: CPU and memory safety (2026-10-25)

Lens: the four core commits of branch `v46` (WSL `~/org2/v46/int/repo`, tip 2d076f6): 876af9c
(upstream 97a450b, the devices' state from 0xA0020), a67bb47 (16 and 32-bit accesses that cross a
page go a byte at a time), 8ea8200 (DMA writes into 0xA0000 to 0xBFFFF dropped) and 2dfa515 (the
floppy residual of upstream 81e09ee). Work was done in my own clone, WSL
`~/org2/v46/rev-memory-safety/repo` (branch `v46`, nothing committed). The package stage
`~/org2/v46/pkg` was only read. Nothing was flashed, pushed or tagged, and no serial port was opened.

## Verdict

**Ready, for this lens.** I found no blocker and no major problem. The byte split is right for every
width and path I could find, its bytes are in the right order and go to the right pages, a fault on
the second page still leaves memory as it was, and the common path costs one instruction more and one
memory load fewer, as the notes say. One minor wording fix in `BUILD-NOTES.md` 32.4 and four notes
follow. Two of the notes are old faults that v4.6 neither adds nor fixes.

## What was checked

### 1. The commits as they stand [verified]

* 876af9c has upstream 97a450b's author, date and message ("fix") and nothing added; its patch-id is
  97a450b's (`3672fe74`, from `~/org2/pcm/upstream.git`). Commits 2 to 4 are by Platima with the
  Co-Authored-By line; the committer of all four is Platima.
* 2dfa515's two added lines are upstream 81e09ee's line for line (`c >= f->tracks || h >= f->heads ||
  s < 1 || s > f->sectors`). They sit under `EMULINK_CHECKED`, which `misc.c` turns on with
  `BUILD_ESP32`; the board ELF has `emulink_error_count`, so the check is in the board build.
* The four files outside `esp/` and `s31/` (`i386.c`, `i8257.c`, `misc.c`, `pc.c`) are the core
  series' (v46-core f7a1131) byte for byte, and 190ec33 and 2d076f6 differ only in `s31/`.
* The staged patches carry this code: `tiny386-idf-v4.patch` applied to 4ce2e06, and
  `tiny386-idf-v3.patch` applied to 9ed786b, each give 190ec33's tree with `s31/` left out.

### 2. Every 16 and 32-bit access path [verified: code, a script]

* A script paired each 16 or 32-bit access in `i386.c` with the translate before it. 168 accesses
  name their width (`load16`, `laddr32`, `saddr16` and so on), and every one is translated with that
  same width. The rest are in macros where the access and its translate take the same `BIT` token
  (`E_helper`, `EG_helper`, the push and pop macros, the string macros). So `CROSSES_PAGE(res, n)`
  and `translate_laddr()`'s ADDR_OK2 test always agree, and "an access that does not cross a page is
  always ADDR_OK1" holds. The mixed cases are right too: `Ep` (a 32-bit offset at `addr` and a 16-bit
  selector at `addr + 4`, each translated at its own width), LDS and its kind, SGDT and SIDT, LGDT
  and LIDT, BOUND and CMPXCHG8B.
* Paging off: `byte_paddr()` gives `addr1 + i`, so each byte meets `load8`/`store8`'s own window and
  RAM size tests. At 4 GiB it wraps to 0, as a 386 does.
* Paging on: `addr2` is the second page's base (`translate_lpgno()` is given `lpgno << 12`), and
  `byte_paddr()` gives `addr2 + (off - 0x1000)` for the bytes past the boundary.
* V86 mode is the paged path at CPL 3; nothing in the byte split depends on the mode.
* The string fast paths (STOS, MOVS, INS, OUTS) take the element loop only when the first address is
  aligned to the element, and stop at the page's end, so none of their accesses crosses. Their bulk
  copies into or out of `phys_mem` (`io_read_string`, `io_write_string`, `iomem_write_string`) test the
  whole page against the window and the end of RAM, and the window is page aligned.
* Instruction fetch: the cached paths read inside one page only (offset below 4080 for `ifetch.paddr`,
  4095 and 4093 for `fetch16` and `fetch32`); the rest go through `translate16/32` and `load16/32`, so
  an immediate that runs into the window now comes from the VGA, where v4.5 read the devices' state.
* The FPU goes through `cpu_load16/32` and `cpu_store16/32`, so each part meets the split.
* The other writers of `phys_mem` on the board: `tlb_refill()`'s accessed and dirty bits (already kept
  out of the window by `pt_addr_ok()`), `load_rom()` (256 KiB at most, from 0xC0000 up), and the DMA
  write (8ea8200). The only DMA writer is SB16 command E2h, as the commit says.

### 3. Byte order and where each byte goes [verified: disassembly, a fuzzer]

* The board's `load_bytes.constprop.0` (at 0x40010002, in flash) does what the C says: res minus 1,
  offset at or past 0x1000, then `addr2 - 0x1000 + off`; the window test, the 0xE0000000 test, the RAM
  size test; each byte shifted left by `8 * i` and ORed in. Little endian.
* `fuzz_split.c` includes `i386.c` and checks `load16/32` and `store16/32` against a byte model of my
  own, with paging on and off. Linear pages are mapped at random to RAM, to the window, to the page
  below it, past the end of RAM and to the linear frame buffer at 0xE0000000. Each world has 1,000
  accesses, mostly at the last bytes of a page, and RAM is compared in full after each. With the board
  header and ASan and UBSan: eight seeds of 200,000 accesses, about 37.5 per cent of them crossing a
  page, 0 mismatches. Plain build: 500,000 accesses, 186,718 crossing, 0 mismatches. v4.5's `i386.c`
  under the same fuzzer stops with SIGSEGV at its first split store whose second page is past RAM
  (`pstore16` at guest physical 0x409000 with 4 MiB of RAM).
* Note on v4.5's old fault: under paging its second page had no bound at all. A page table entry can
  name any physical address, so on the 32-bit board the bytes could land at `phys_mem` plus anything,
  which wraps round the whole address space [inferred: code; the fuzzer's crash]. The commit message's
  "or go past the end of guest RAM" is true, but this is the stronger reason for the fix, and worth a
  line in the upstream PR.

### 4. A fault half way through a split access [verified: `test_split_fault.c`]

Guests in 32-bit protected mode with paging, CR0.WP on, a GDT and an IDT whose page fault handler
keeps the error code and CR2 and halts. The second page is either not present or read only. Run on
v4.6's `i386.c` and on v4.5's, both built as the board builds them:

| Case | v4.6 | v4.5 |
|-|-|-|
| T1 dword store, second page not present | fault, error 2, CR2 401000h, nothing written | the same |
| T2 dword store, second page read only (WP) | fault, error 3, CR2 403000h, nothing written | the same |
| T3 word store at the page's last byte | fault, nothing written | the same |
| T4 ADD to memory (read, modify, write) | fault, nothing written | the same |
| T4b XCHG, second page read only | fault, memory and EAX as they were | the same |
| T5 REP MOVSD, misaligned, destination crosses | fault, nothing written, ESI and EDI as they were | the same |
| T9 STOSD across | fault, nothing written, EDI as it was | the same |
| T14 dword load, second page not present | fault, error 0, EAX as it was | the same |
| T7 FSTP m64 whose first dword crosses | fault, nothing written | the same |
| T6 FSTP m64, first dword in page 1, second in page 2 | fault, first dword written | the same (an old fault, note 3) |
| T15 store and load, RAM then the window | two bytes to RAM, two to the VGA, read back 77773344h | stores into the window's `phys_mem` |

`translate_laddr()` translates both pages before `store_bytes()` writes a byte, and `store8` cannot
fault, so the split adds no partial write. The dirty bit of the first page is set before the second
page faults, as in v4.5.

### 5. The cost on the common path [verified: disassembly of both ELFs]

`cpu_exec1` is 131,456 bytes in v4.5's ELF and in v4.6's, and with call offsets and address constants
taken out the two listings are the same line for line. It calls the standalone `load16`, `load32`,
`store16` and `store32` (in IRAM), so these four functions are the whole difference on the hot path.
For an aligned access to RAM:

| Function | v4.5 | v4.6 | memory loads, v4.5 to v4.6 |
|-|-|-|-|
| `load16` | 15 instructions | 16 | 5 to 4 |
| `load32` | 15 | 16 | 5 to 4 |
| `store16` | 17 | 18 | 4 to 3 |
| `store32` | 17 | 18 | 4 to 3 |

The crossing test is `lui`, `slli`, `bltu`, in place of the load of `res->res` and its branch. This is
what a67bb47's message and `BUILD-NOTES.md` 32.4 say ("15, 15, 17, 17 instructions become 16, 16, 18,
18"), and IRAM's code is 266 bytes smaller (216,218 to 215,952), as 32.8 says. Whether the extra
instruction shows on the board is [unknown], as 32.12 says. My guess is not measurably: one ALU
instruction for one load, and the `slli` now sits one instruction after the load of `addr1` [inferred].
The byte path is in flash; the emulator already ran flash code in v4.5 (`enter_helper`,
`task_switch`, `cpu_load32`), so no new kind of risk.

### 6. The Windows 95 regression counts [verified: my own runs]

* The core series' runs (WSL `~/org2/v46/core/pc/runs`): v4.5 (c8528f2) and v4.6 (f7a1131) give the
  same instruction counts, 588,301,033, 1,304,341,904 and 1,337,723,352, and the same final pictures
  (`final.png` MD5 b05a5040, 82464aaf, 82464aaf).
* I built a v4.6 harness binary that counts the split accesses (a patch to a copy of `i386.c` only) and
  ran the same three workloads on fresh copies of the pristine image (copies deleted): the same three
  counts and pictures. Split accesses in them:

| Workload | 16-bit loads | 32-bit loads | 16-bit stores | 32-bit stores | of these paged | bytes to the window or past RAM |
|-|-|-|-|-|-|-|
| desktop, 120 s idle | 760 | 12,652 | 88 | 1,119 | 14,151 of 14,619 | 0 |
| Start, Shut Down, Restart | 1,510 | 33,515 | 131 | 1,431 | 35,637 of 36,587 | 0 |
| MS-DOS mode, `mem`, `dir`, `exit` | 2,175 | 39,202 | 136 | 1,585 | 42,293 of 43,098 | 0 |

  So the runs push tens of thousands of split accesses through the byte path, all RAM to RAM, in real
  mode and under paging, and get v4.5's results to the instruction. They make none into the window or
  past RAM, so those cases rest on host tests 21 and 22, `test_split_fault.c` and the fuzzer. About 25
  split accesses per million guest instructions: the byte path's own cost cannot matter [inferred].
* Host time on this PC, run one after the other (the desktop workload, three times each): v4.5 6.0,
  6.0, 6.0 s; v4.6 5.9, 5.9, 5.9 s. The core series' logs give v4.6 8.2 s and 29.1 s where v4.5 had
  6.1 s and 22.9 s; those runs were made side by side, and the gap is not there when they are run alone.

### 7. The DMA window and the floppy [verified: code, host tests]

* `I8257_HOLE()` tests the very index that is written, in both directions, so no byte of a transfer
  reaches 0xA0000 to 0xBFFFF whatever the page, address or count; `hwaddr` is 32 bits and the
  existing end-of-RAM test stops a wrapped address. Reads are left alone and only feed the sound.
* The floppy check refuses a cylinder, head or first sector the format lacks, before the seek. The
  cylinder test adds nothing that the image's length did not already refuse; the head and sector
  tests are the new part, as `test_floppy.c` shows (head 2 and sector 19 refused, a run from head 0's
  last sector into head 1 accepted).
* The VGA side of the split: inside the 0xA0000 window an access that crosses a 4 KiB page now reaches
  `vga_mem_write()` a byte at a time, where v4.5 called `vga_mem_write16/32()`. Outside chain 4 those
  were already four byte writes. In chain 4, v4.5 wrote the whole word or dword if the first byte's
  plane was enabled; now each byte meets its own plane, as on a real VGA. Reads were already made of
  byte reads.

### 8. Tests and sanitizers [verified]

* Host tests in my clone: 61 of 61 (89 s).
* `test_page_cross.c` with nothing past RAM in the allocation, under ASan and UBSan: 17 of 17 on v4.6;
  v4.5's `i386.c` overflows the heap at case D. `test_split_fault.c` under the same: 10 of 10.

## Findings

1. **Minor.** `BUILD-NOTES.md` 32.4, the Windows bullet, ends: "So those runs made no crossing access
   into the window, no DMA write there and no floppy request, and the checks cost them nothing." The
   section is headed [verified], but the sentence is an inference from equal guest instruction counts.
   Those counts are fixed by the guest and the harness's virtual clock, so they say nothing of cost,
   and the core series' own logs (runs made side by side) even show v4.6 slower. Suggested wording:
   "The same runs with a counting build made 14,619, 36,587 and 43,098 split accesses, every one RAM
   to RAM, none into the window or past RAM, and no DMA write or floppy request; run one after the
   other on the PC the desktop run took 5.9 s of host time on v4.6 and 6.0 s on v4.5 [verified]."
2. **Note.** None of the PC runs uses Plat's own setting (`win95-sd.ini`: 800x480 through the VBE
   linear frame buffer, `vga_mem_size = 768K`), and 32.10 skips the flicker harness because the
   640x480 counts matched. At 800x480 a store that crosses a page inside the frame buffer is now four
   `iomem_write8()` calls where it was one `iomem_write32()`. By the code the bytes and the dirty marks
   come out the same; the only difference is at the very end of video memory, where v4.5 dropped the
   whole dword and v4.6 keeps the bytes that fit [verified: code; not run]. Either run the 800x480 set
   once on v4.6 against v4.5's counts, or give that as 32.10's reason.
3. **Note, old and not for v4.6.** `fpu.c` stores a 64 or 80-bit value as two or three separate
   translated stores, so a page fault on a later part leaves the first part written (case T6, the
   same on v4.5). A real 386 writes nothing. Plat's inis have `fpu = 0`. An upstream matter.
4. **Note, old and not for v4.6.** For an access at linear FFFFFFFDh to FFFFFFFFh, `translate_laddr()`
   asks for page 0x100000, which `tlb_refill()` looks up as directory entry 1024, one past the page
   directory. `pt_addr_ok()` keeps that read inside guest RAM, so it is wrong for the guest only, not
   unsafe for the host. A 386 wraps to page 0.
5. **Note, tooling.** ASan from GCC 12 on this WSL kernel (6.18.33.2) sometimes dies at start with an
   endless "AddressSanitizer:DEADLYSIGNAL": an empty program did so in 11 of 30 runs. Run sanitizer
   checks under `setarch -R` (no address randomisation), and send their output to a file with a size
   cap: one such run wrote 2.7 GB in 100 s before its timeout.

## What only the board can say [unknown]

* Whether one more instruction in `load16/32` and `store16/32` shows in T386MON's instructions a second.
* The boot console's `of 131040 bytes` line, which should read as `TEST-PLAN.md` 17 says.

## Where things are

* My clone and work: WSL `~/org2/v46/rev-memory-safety` (`repo`, `work`, `bin`, `runs`, `runs.log`,
  `ht.log`). No disk image copy is left there.
* Test sources: `C:\Users\Owner\AppData\Local\Temp\claude\D--Repos-esp32-s31-linux\99e6c67c-c5d4-41b3-a911-64bbdd3332f8\scratchpad\v46\rev-ms\test_split_fault.c`,
  `C:\Users\Owner\AppData\Local\Temp\claude\D--Repos-esp32-s31-linux\99e6c67c-c5d4-41b3-a911-64bbdd3332f8\scratchpad\v46\rev-ms\fuzz_split.c` and
  `C:\Users\Owner\AppData\Local\Temp\claude\D--Repos-esp32-s31-linux\99e6c67c-c5d4-41b3-a911-64bbdd3332f8\scratchpad\v46\rev-ms\mkcount.py` (the counting patch).
