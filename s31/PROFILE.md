# The profiler (v5.1)

v5.0 made Windows about 1.7 times faster on the board, and Plat wants more. The counts made under
qemu (`FAST-CPU.md` 7) say how many host instructions a guest instruction takes, but not how long
they take on the board: they cannot see core 1 waiting on PSRAM, on flash, or on core 0. The
profiler shows that, on the board itself. With `[esp] profile = 1`, core 0 samples where core 1 is,
about 2,000 times a second, with what the guest was doing at the same moment, and writes the
counts to the card as `PROFILE.TXT`. A script on the PC (`s31/profile/profile_report.py`) turns
the file and the release's `tiny386.elf` into a report: where core 1's time went, function by
function and line by line, how much of it in flash, in internal RAM and in ROM, and what the guest
was running. Off by default, and then nothing of it runs. Written 2026-10-28, on the branch
`v51-sampler` from `s31-v5.0` (0d37002).

Tags, on every claim:

* [verified] seen here: read in the file named, or measured by the method named
* [inferred] reasoned from verified facts; not run
* [unknown] only a board, or the person named, can say


## 1. In one screen

* **Turning it on.** `profile = 1` under `[esp]` in the ini. The boot console then says
  `Profile: on ...`, and `Profile: sampling from now.` when the PC starts to run. `profile = 0`, or
  no line, is v5.0 [verified: `esp/main/esp_main.c`, `profile.c`].
* **How core 1 is sampled.** The ESP32-S31's bus monitor keeps a register with each core's program
  counter as it runs (`BUS_MONITOR_CORE_1_RCD_PDEBUGPC_REG`, 0x2D0020D0), and ESP-IDF's start-up
  already turns that recording on for both cores, in v5.0 too [verified: section 2]. A timer
  interrupt on core 0 reads it at random gaps between 250 and 750 microseconds. Core 1 runs none of
  the profiler's code between the files [verified: section 7]: what it does, its waits on PSRAM and
  flash included, is what the samples see [inferred: section 2.4 has what the register itself is
  not documented to do].
* **The guest at the same moment** [verified: `prof_cpu.h`, tests]: CS, EIP, the privilege level,
  the mode (real, virtual-8086, 16 or 32-bit protected), the code page, and whether the CPU was in
  the fast interpreter, in an instruction handed to tiny386's `cpu_exec1()`, in tiny386's
  interpreter alone (`fast_cpu = 0`) or halted. For an instruction in `cpu_exec1()`, its first bytes
  too, so the report can say which instructions are handed over, weighted by the time they take.
* **The cache counters** [verified: ESP-IDF's `esp_cache_cnt.h`]: every 10 ms the same interrupt
  adds up the S31's cache counters, each core's instruction and data traffic into the flash and
  PSRAM cache: accesses, lines fetched (the true misses), stall events (which grow with the time
  waited) and lines written back.
* **The files.** Every 60 s, and when the PC is switched off, `PROFILE.TXT` beside the ini
  (`/sdcard/tiny386/PROFILE.TXT` on the card). Ctrl+Alt+F8 writes it at once, and the same text to
  `PROF01.TXT`, `PROF02.TXT` and so on: two presses mark a stretch of the run, and the script's
  `--since` reports that stretch alone [verified: tests]. Each file has the counts since the PC
  started, the firmware's version and the SHA-256 of the ELF it was built as.
* **What it costs.** With `profile = 0`: no table, no timer, no interrupt, no key, and core 1 runs
  v5.0's instructions in every step [verified: the object code, section 7]; 3,016 bytes of internal
  RAM (the sampler's code and its counts' heads) and about 16 KB of flash are there whether it is
  on or not [verified: the size reports]. With `profile = 1`: 178 KiB of PSRAM for the tables, a
  few microseconds of core 0 a sample, and core 1 waits while a file is written, about 15 ms of
  formatting for full tables and then the card's own time [inferred: section 7; the board's own
  figures are in every file and on the console].
* **The guest sees no difference** [verified]: the three Windows 95 runs of v4.6's regression end
  on v4.6's counts with this branch, the fast interpreter on and off, and with a stand-in of the
  profiler sampling and writing files all through them; the lockstep tester finds no difference
  from v4.6 in its six workloads (section 8).
* **For Plat:** `TEST-PLAN.md` 19.


## 2. Sampling core 1's PC: the choice, and the evidence

### 2.1 The bus monitor's PC record

ESP-IDF (v6.1-dev-8142-g188e3e55bb, the boards' build) [verified: read in the files named]:

* `components/soc/esp32s31/register/soc/bus_monitor_reg.h`: `BUS_MONITOR_CORE_1_RCD_EN_REG`
  (`DR_REG_BUS_MONITOR_BASE` + 0xCC) has `RCD_RECORDEN` ("1: ... starts to record PC in real time")
  and `RCD_PDEBUGEN` ("1: HP CPU outputs PC"); `BUS_MONITOR_CORE_1_RCD_PDEBUGPC_REG` (+ 0xD0, read
  only) is "PC logging register". `DR_REG_BUS_MONITOR_BASE` is 0x2D002000 (`reg_base.h`).
  `include/soc/assist_debug_reg.h` gives them their older names, `ASSIST_DEBUG_CORE_1_RCD_*`, as on
  the ESP32-C3, C6 and P4. The field's own line says "the PC value at HP CPU reset", the use the
  register is kept for, the PC at a watchdog reset [inferred: the ESP32's bootloader reads its
  own chip's like register for that, `bootloader_esp32.c`]; the enable bit says it records in real
  time.
* `components/esp_system/port/cpu_start.c`, core 1's start (lines 272 and 273):
  `cpu_utility_ll_enable_debug(1); cpu_utility_ll_enable_record(1);`, which set those two bits
  (`hal/esp32s31/include/hal/cpu_utility_ll.h`).
* `components/esp_system/debug_assist.c`, run on each core at start-up when
  `CONFIG_ESP_SYSTEM_HW_PC_RECORD` (or the stack guard) is set: the bus monitor's clock on and its
  reset let go for that core, then `assist_debug_ll_enable_pc_recording(core_id, true)`.
* The release build's `sdkconfig` has `CONFIG_ESP_SYSTEM_HW_PC_RECORD=y` and
  `CONFIG_ESP_SYSTEM_HW_STACK_GUARD=y` (`~/org2/v50/res/rel/build-a`, v5.0 as released). So the
  recording has been on in every v5.0 boot, profiler or not.

So core 0 reads one peripheral register for each sample of core 1's PC. Nothing on core 1 changes
for it.

### 2.2 Its cost on core 1

None of its own: the register is written by the core's hardware as it runs, and has been since
start-up in v5.0 too [verified: 2.1]. Core 0's read goes through the peripheral bus, not core 1's
paths to its internal RAM, flash or PSRAM [inferred]. The rest of the profiler's work on core 0
touches core 1 in two places only: the tables in PSRAM pass through the data cache, which the two
cores share [inferred: ESP-IDF's S31 `hal/cache_ll.h` and `soc/cache_reg.h` have an instruction
cache for each core, `L1_CORE0` and `L1_CORE1`, and one `L1_DCACHE`], and the files, written by
core 1 between two steps (section 5).

### 2.3 What else there was

* **An interrupt on core 1 reading its own interrupted PC** (`mepc`): the usual sampling profiler.
  Every sample would cost core 1 an interrupt's entry and exit, a few hundred cycles with FreeRTOS's
  handler [inferred], and its cache's lines; it could not sample code that runs with interrupts off;
  and it would be core 1's time that the profile measures. Not taken.
* **The cores' own performance counters** (cycles, instructions retired): each is read only by its
  own core, so core 1 would have to read them [inferred]. The cache counters (section 1) do the
  part of this that matters here (misses and the waits), read from core 0.
* **The trace encoder** (`soc/esp32s31/register/soc/trace_reg.h`): an instruction trace into
  memory would give every instruction, not samples, but it takes memory bandwidth from the PSRAM
  that core 1 waits on, and a decoder [inferred]. Not taken.
* **JTAG** (OpenOCD's own PC sampling halts the core): needs a probe on the board and stops core 1
  at each sample [inferred]. Not taken.

### 2.4 What only the board can say [unknown]

* Whether the register follows core 1 at every instruction, and which instruction it holds while
  the core waits on a load from PSRAM or a fetch from flash (the waiting one, or the one before).
  Either way the time lands on the instruction that waits or the one next to it, which is the line
  that the report shows. The first file says whether the register moves at all: the console warns
  if 1,000 samples gave fewer than 8 places, or if more than a tenth of them fall outside the
  firmware's code (`profile.c`).
* How long a read of it takes core 0 (the file's `sampler_cost` has the whole sample's cycles).


## 3. The guest at the same moment

`prof_read_guest()` (`esp/main/prof_cpu.h`), in the same interrupt, right after the PC, reads the
fields of the CPU's state that `cpui386_prof_fields()` (`i386.c`, new, called once) gives the
addresses of. The CPU's state is in internal RAM, which no cache stands in front of, so core 0
reads what core 1 last wrote [verified: `cpui386_new()` takes it from `malloc()`, which the boards'
`CONFIG_SPIRAM_USE_CAPS_ALLOC` keeps in internal RAM; inferred: the memory's behaviour].

* **Area**: halted (`cpu->halt`); else tiny386 alone (`fast_on` false); else an instruction handed
  over (`exec1_one`, which the fast interpreter sets for the one instruction it gives
  `cpu_exec1()`); else the fast interpreter, or the loop and devices around it.
* **Mode**: CR0's PE, EFLAGS' VM, and the code segment's size (`code16`). **CPL**: `cpu->cpl`.
* **EIP**: in `cpu_exec1()` its own `cpu->ip`, set at each instruction's start, so the instruction
  itself; in the fast interpreter `cpu->next_ip`, where its step began (it keeps EIP in a register
  until the step ends or it hands over, so this can be up to 512 instructions back, the board's
  `PC_STEP_COUNT`) [verified: `i386_fast.h`, `i386.c`, `pc.c`].
* **Code page**: tiny386's fetch cache (`cpu->ifetch.laddr`), which the fast interpreter fills as
  it moves from page to page (`fast_window()`), so it is the page being run, not the step's start.
  The guest table counts the fast interpreter's samples by it, and by EIP's page when it is empty
  (marked as a guess); tiny386's by the instruction's own page.
* **The instruction's bytes**, in `cpu_exec1()` only: up to 8, from guest RAM, through the fetch
  cache's own mapping (`ifetch.xaddr`) and only when the instruction is in the page it is for, and
  never past that page's end. The key kept is its prefixes, its opcode (after 0Fh too) and its
  ModRM byte, with the mode [verified: tests].

Every field is one aligned word or byte, read whole; two of them may be from either side of a
change (a far jump between reading CS and EIP, say), so now and then a sample counts against the
wrong page. With thousands of samples that is noise [inferred].


## 4. PROFILE.TXT

Text, one item a line, written by `prof_write()` (`esp/main/prof_hist.c`). Format 1:

```
tiny386 profile 1
firmware v5.0                          the app's version
board the ESP32-S31-Korvo-1
idf v6.1-dev-8142-g188e3e55bb
elf_sha256 (64 hex digits)             the ELF the app was made from (esptool's "ELF file SHA256")
sampler core0-pc-record 2000           how, and samples a second asked for
ini /sdcard/tiny386/win95-sd.ini
fast_cpu 1
file 3 timer                           this run's files so far; why: timer, key, end
run 5eed0b07                           this run's own word, drawn at random when the board started
cpu_mhz 320
time_us uptime U sampling S paused P   microseconds: since power-on, since sampling began, of that paused for writes
last_write_us W                        the write before this one, from pause to resume (the guest waiting)
guest insns N run_us R halt_us H       the speed screen's counts since the PC started
sampler_cost samples N cycles C max M  core 0's cycles in the interrupt's sampling part
range iram|flash|rom LO HI             the firmware's code regions
samples N
area fast N exec1 N alone N halt N
mode real N v86 N pm16 N pm32 N
region iram N flash N rom N other N
area_region fast a b c d exec1 ...     area against region, every sample
mode_area real a b c d v86 ...         mode against area
op_unread N                            cpu_exec1() samples whose bytes could not be read
page_guess N                           fast interpreter samples counted at the step's start
table host|coarse|guest|ops slots N used N lost N
cache NAME CORE i|d hit N miss N conflict N nxtlvl_rd N nxtlvl_wr N|-
cache_reads N
[host] pc area count                   one line for each host PC and area
[coarse] line area count               PCs that found no room in host, by 64-byte line
[guest] area mode cpl cs page guess count
[ops] key count                        prof_op_key()'s key (prof_hist.h)
end
```

The counts only grow: a file has everything since sampling began. A table holds at most 3/4 of its
slots; a key that finds no room is counted as lost, and the totals (`samples`, `area`, `mode`,
`region` and the two matrices) still have every sample. `end` is the last line: a file without it
was cut short.

After the reviews of v5.1 (`BUILD-NOTES.md` 34.15) [verified: `tests/host/run.sh` 25]:

* `elf_sha256` has all 64 digits, taken from the app's description where esptool put them. v5.1's
  first images (never released) wrote 9, as ESP-IDF's `esp_app_get_elf_sha256()` gives them in
  this build (`CONFIG_APP_RETRIEVE_LEN_ELF_SHA`); the report takes 8 to 64 digits as the start of
  the ELF's SHA-256 and says how many it compared.
* `run` is new: a random word drawn when the profiler is set up, the same in every file of one
  boot, so that `--since` can refuse two files of two boots (their file numbers start again at 1).
  The format stays 1; the report reads files without the line.
* PROFILE.TXT is written as `PROFILE.NEW` and renamed over the old one only once it is whole, so
  a full card or a file that cannot be opened leaves the last good PROFILE.TXT. A `PROFnn.TXT` takes
  its number only when it was written whole; one that failed is removed. A power cut in the write
  can still leave `PROFILE.NEW` cut short, beside the last whole PROFILE.TXT.
* The report reads a file cut short (no `end`) as far as it goes: the line the cut fell in, and
  anything after a line that does not parse, are left out, and it says where it stopped.


## 5. On the board

* **Setup** (`profile_setup()`, `app_main()` on core 0, after the ini): the tables from PSRAM
  (8,192 host PCs, 2,048 lines, 4,096 guest keys, 512 instruction keys: 178,176 bytes, and a 4 KiB
  buffer for the file; half of each when the largest free block cannot spare that and 256 KiB
  besides, for the VGA's 16-colour cache and ESP-IDF); the cache counters started; a GPTimer at 1
  MHz whose interrupt is allocated there, so it runs on core 0; Ctrl+Alt+F8. The ini must be on
  `/sdcard` (the card's first FAT partition), never a partition the guest has whole: the file goes
  beside it. Anything missing: a yellow line, and the PC runs as with `profile = 0`.
* **Sampling** (`prof_alarm()`, core 0's interrupt): the next alarm first, 250 to 750 µs on, so
  that nothing periodic on core 1 lines up with the samples, counted from the timer's count when
  the interrupt ran (since the reviews of v5.1; counted from the alarm that fired, an interrupt
  held off for long set alarms already past and took a burst of samples); then, once the PC runs
  and unless a file is being written, the PC record, the guest, the tables, every 10 ms the cache
  counters, and
  every 60 s the request for a file. The samples count from when the PC starts to run, after the
  boot console's hold (`profile_start()` just before `run_pc()`), so the console's seconds, when
  core 1 waits in `vTaskDelay()`, are not in them.
* **Writing** (`profile_write()`, core 1, between two steps): a file is asked for as the Ctrl+Alt
  keys' saves are (`speed_ask_save(SPEED_SAVE_PROFILE)`, the same word `run_pc()` already looks at
  after each step), and written where they are written: the guest's own disk writes first, as
  for a save, then the sampling paused (the interrupt is let finish a sample it is in), the file
  written through FatFs (as `PROFILE.NEW`, then renamed: section 4), the sampling resumed. The
  console line says how long, and the next file has it (`last_write_us`). The speed screen leaves
  the time out, as it does for a save.
* **Console lines**: `Profile: on ...` at setup (yellow, so that it is not missed in a photo),
  `Profile: sampling from now.` when the PC starts, `Profile: /sdcard/tiny386/PROFILE.TXT written
  (timer, file 3): N samples, B bytes, in T ms, the guest waiting.` for each file (with `and
  .../PROF01.TXT` for a key press), and yellow lines if the PC record looks stuck or outside the
  code, or the tables are full. A write that fails says so in red and that the last PROFILE.TXT is
  kept. The figures in these lines are read before the sampling resumes.
* **Memory, on the sampler's own branch** (`v51-sampler`, on v5.0, before `v51-ram`) [verified:
  the size reports of the branch's two builds]: internal RAM used by code and static data 3,016
  bytes more than v5.0 on the Korvo-1 (238,428 bytes left for ESP-IDF, against 241,444) and 3,006
  on the DOS board (236,196, against 239,202), whether or not it is on: the sample's path in
  internal RAM (2,074 bytes: `prof_alarm()`, `prof_sample()`, `prof_op_key()` and the GPTimer's
  own handler) and the counts' heads (about 0.9 KB). The image grew by 16,048 bytes of flash on
  the Korvo-1 and 16,040 on the DOS board (the GPTimer driver, the file writer): the branch's
  Korvo-1 app was 1,078,224 bytes, its DOS board app 1,098,912.
* **Memory in v5.1** [verified: the release's builds, `nm`; `BUILD-NOTES.md` 34.8 has the sizes]:
  `v51-ram`'s sdkconfig lines put the GPTimer driver's code in flash
  (`CONFIG_GPTIMER_ISR_HANDLER_IN_IRAM` off), so its interrupt entry (`gptimer_default_isr()`) and
  the next alarm's call (`gptimer_set_alarm_action()`) run from flash on core 0, as do the other
  calls `prof_alarm()` makes outside the profiler (`speed_ask_save()`, `esp_cache_cnt_get()` and
  `esp_cache_cnt_clear()`). Only `prof_alarm()`, `prof_sample()`, `prof_op_key()` and their small
  helpers are in internal RAM. This is safe: the interrupt is not marked for internal RAM
  (`CONFIG_GPTIMER_ISR_CACHE_SAFE` is off), so ESP-IDF holds it off while the cache is off; the
  cost is a cache miss now and then on core 0, with `profile = 1` only [inferred].
  `PROFILE_IN_FLASH` (one commented line in each board header since the reviews of v5.1) moves the
  sample's path to flash too, 1,878 bytes back on the Korvo-1 and 1,878 on the DOS board
  [verified: built]; it is the first of the two RAM fallbacks (`BUILD-NOTES.md` 34.11).


## 6. On the PC: `s31/profile/profile_report.py`

```
python3 s31/profile/profile_report.py PROFILE.TXT images/korvo1-dualboot/tiny386.elf -o report.txt
python3 s31/profile/profile_report.py PROF02.TXT tiny386.elf --since PROF01.TXT
```

Python 3, its standard library alone; it reads the ELF itself. It refuses an ELF whose SHA-256
does not start with the file's `elf_sha256` (8 to 64 hex digits; the report says how many it
compared), and with `--since` two files of two runs (`run`), or the later one first (`--force`
goes on past the ELF's check alone). A file cut short is read as far as it goes, with a warning
(section 4). Optional: `addr2line` for the ELF's machine (for a
board's ELF `riscv32-esp-elf-addr2line`, from ESP-IDF's tools, which it looks for itself) for the
source lines; the chip's ROM ELF (`esp32s31_rev0_rom.elf`, from ESP-IDF's `esp-rom-elfs`, also
looked for) to name code in ROM such as `memcpy()`; `--src` the tree, for the lines' text and the
fast interpreter's labels (the tree the script is in, by default).

The report [verified: on the stand-in's files and a made-up file for a board's ELF, section 8]:

1. core 1 by area (fast interpreter, handed over, tiny386 alone, halted);
2. core 1 by region (internal RAM, flash, ROM), and area against region;
3. the cache counters, per second and per 1,000 guest instructions;
4. host functions, with each one's region and areas;
5. `fast_exec()` by handler: each PC's line in `fast_exec()` (addr2line's outermost frame), under
   the label above it in `i386_fast.h` (`op_8b`, `ALU6(op_38, ...) CMP`, `slow`, ...), and its own
   lines with their text; without addr2line, a rough guide from its jump tables instead;
6. source lines, inlined code at its own line;
7. the guest by mode and area, by region (Windows 95's: VMM and VxDs above C0000000h, the shared
   arena, an application's own, 16-bit code by CS, DOS memory and the BIOS below 1 MB), and by code
   page;
8. the instructions `cpu_exec1()` was doing, by their bytes, with their names, weighted by time;
9. the tables, and what the figures can and cannot say.

Every share has its 95 per cent margin from the number of samples (1.96 times the binomial standard
error), as +-.

**The ELF.** The release ships `tiny386.elf` beside each image set's `tiny386.bin` in
`images\korvo1-dualboot`, `images\korvo1-standalone` and `images\dosboard`; the
`korvo1-dualboot-tiny386-default` set has none, and its app is `korvo1-dualboot`'s, the same bytes,
so that set's ELF serves [verified: the v5.0 package at
`D:\Repos\esp32-s31-linux-release\tiny386-idf\v3\images`, and `build.sh`, which copies them; the
SHA-256 of each `tiny386.elf` is the `ELF file SHA256` of `esptool image-info` of its
`tiny386.bin`, checked on v5.0's three]. The dual-boot and standalone ELFs differ (their partition
tables are built in), so the right one is the one whose image was flashed. The script itself, and
this file, are not in the v5.0 package: the release step has to copy `s31/profile/` and
`s31/PROFILE.md` into it, and add them to `SHA256SUMS`.

**The stand-in** (`tests/host/profile/standin.c`, `standin.sh`): the PC repro's harness built from
this tree, linked again with a POSIX timer that sends the emulator's thread SIGPROF about 2,000
times a second; the handler takes the interrupted PC from the signal's context, then runs the
board's own `prof_read_guest()` and `prof_sample()`; the files are written between two steps with
the signal held off, with the board's `prof_write()`. It prints nothing while the PC runs: the
harness charges every byte on stderr as the board's UART time, which moves the guest's clock (a
first version printed one line, and its Windows boot ended 1.5 M instructions away from v4.6's
[verified]). Its files say `sampler standin-sigprof`, with no code regions (the PC's own program has
no internal RAM or flash), and its profile is the PC's, not the board's: the drawing runs on the
same thread there, which the board's core 0 does.


## 7. Cost and disturbance

**With `profile = 0`** [verified: `objdump -dr` of each object of the v5.0 build and of this
branch's, both boards, every function compared with its relocations by name and local labels
renumbered (`~/org2/v51/sampler/scripts/sameobj.sh`)]:

* `i386.c`: 96 of its 97 functions the same instructions; the 97th, `cpui386_new()`, differs only
  in the line number its `assert()` passes (the new function sits above it). `fast_exec()` is in
  `i386.c`, so the fast interpreter is the same code. `cpui386_prof_fields()` is new, in flash,
  and called once.
* `pc.c`, `vga.c`, `display.c`, `speed.c` (but its two new functions): the same.
* `run_pc()` (inlined into `i386_task()`): from `speed_run_init()` to the end of the loop the same
  65 instructions and registers; the only new calls are `profile_start()`, once before the loop,
  which returns at once, and `profile_write("end")`, once after it.
* `kbd_input.c`: Ctrl+Alt keys from 4 to 6 (`KBD_INPUT_HOTKEYS`), so `key_event()` and
  `kbd_input_set_hotkey()` differ; they run on core 0, for each key.
* Nothing is allocated, no timer started, no interrupt allocated and no key taken.

**With `profile = 1`**:

* **Core 0, each sample** [inferred, from counts]: the sampling part (`prof_read_guest()`,
  `prof_sample()` and what it calls) is about 204 RV32 instructions when the keys are in the
  tables already (counted under qemu-riscv32 with jtrace on the board's options,
  `~/org2/v51/sampler/rvbench`), about 0.7 µs at 320 MHz and 1.07 cycles an instruction, plus the
  interrupt's entry and exit, the next alarm (`gptimer_set_alarm_action()`) and up to two or three
  PSRAM lines for the tables: a few microseconds, so about 0.5 to 1 per cent of core 0 at 2,000 a
  second. Each file says the board's own (`sampler_cost`: cycles from the alarm's start to the
  sample's end, average and most) [unknown until then]. In v5.1 the driver's entry and the next
  alarm run from flash (section 5), and `sampler_cost`, counted from the start of `prof_alarm()`,
  leaves the driver's entry before it out, so core 0's whole cost is somewhat above it
  [inferred]. On this PC the sampling part takes 24 ns.
* **Core 1**: no instruction of the profiler between files. The tables' lines pass through the
  data cache that both cores share: a sample touches two or three lines, so about 6,000 lines a
  second at most, against the guest's own traffic [inferred: the cache counters in each file show
  core 1's misses, so a run with and without the profiler can be compared there].
* **A file**: formatting full tables (about 175 KB of text) takes about 4.5 M RV32 instructions
  (counted the same way), about 15 ms at 320 MHz [inferred]; then FatFs and the card, [unknown]
  (the console says each file's whole time). The guest waits for all of it, once a minute and at
  each key press; a stretch shorter than that is not sampled, which the file counts as `paused`.
  The guest's own disk writes are put on the card first, as for a save.
* **PSRAM**: 178 KiB (92 KiB when short), out of what `PSRAM_KEEP_FREE` keeps for ESP-IDF: Plat's
  v5.0 console had 509 KiB of PSRAM free after `PC built` (largest block 504) [reported].


## 8. Tests

[verified, 2026-10-28, WSL, on this branch's tree; the outputs are in `checks/profile-v5.1.txt`]:

* **Host tests** (`tests/host/run.sh` 25): `test_prof_hist.c`: the tables (a count for each key; a
  full table counting new keys as lost while the totals keep every sample; the coarse table taking
  what the host table could not), the guest's key (the code page while the fast interpreter runs,
  the step's start when there is none, CS:EIP for tiny386), the instruction key (16 byte sequences:
  prefixes, 0Fh, ModRM, bytes running out), the cache counters, the file (every line, `end` last,
  the size returned, a refused write and a too small buffer said, two files at once the same), the
  reader on a CPU's fields in every mode and area with the bytes read through the fetch cache's
  mapping; 64-bit, and 32-bit with unsigned char as on RV32. Then `profile_report.py` on a file
  whose host PCs are in the test's own functions: the functions, the instruction and the guest's
  page with their shares, another ELF refused, `--since` the stretch alone and the files the wrong
  way round refused. `test_kbd_input.c`: Ctrl+Alt+F8 beside F9 to F12, five keys at once. All 76
  host tests pass (v5.0's 72 and these four).
* **The guest unchanged** (the PC repro's harness built from this tree as the board builds the
  core, `~/org2/v51/sampler/scripts/regress51.sh`): v4.6's three Windows 95 runs end on
  588,301,033, 1,304,341,904 and 1,337,723,352 instructions, desktop at 211 s, with the disk checks
  of v5.0's runs, with the fast interpreter on, off, and on with the stand-in sampling about 2,000
  times a second and writing a file every 4,000 samples (4, 15 and 15 files during the runs).
* **The lockstep tester** (`tests/lockstep`, v4.6's build as the reference, this tree the
  candidate, the fast interpreter on and off, one workload at a time): no difference in any of the
  twelve (`~/org2/v51/sampler/gate51.txt`).
* **Builds**: the Korvo-1 and the DOS board, in the tree with `build.sh`'s folder names: no new
  warning (the Korvo-1's one old `i2s_task` warning); sizes in section 5.
* **The report** on the stand-in's three Windows runs (the PC's profile, not the board's):
  Windows' boot 65 per cent of the samples in `fast_exec()`, of it `op_8b` (MOV r,r/m) 12 per
  cent, the ADD family 8, Jcc 7; the MS-DOS round trip 40 per cent halted, and of the instructions
  handed to `cpu_exec1()` REP OUTSW/D 23 per cent, REP MOVSW/D 18, REP INSW/D 10, INT 4. And on a
  made-up file for this branch's Korvo-1 ELF (`~/org2/v51/sampler/rvbench/synth.py`: real
  addresses in `fast_exec()`, `cpu_exec1()`, `lfb_stos()` and the ROM's `memcpy()`, invented
  counts): the RV32 side, its jump tables of 4-byte pointers, its regions, ESP-IDF's
  `riscv32-esp-elf-addr2line` with the build's `/TINY386/` paths, and the ROM ELF naming `memcpy`.

**After the reviews of v5.1** [verified, 2026-10-29, WSL, `checks/host-tests-v5.1.log`]: `run.sh`
25 also gives the report a file that names the ELF by its first 9 hex digits (taken, and its
figures the same), 9 digits of another ELF and 7 digits (refused), the demo file cut at every 7th
byte (each read as far as it goes, those cut in the tables with the warning, never a traceback),
and `--since` with a file of another run (refused); `test_prof_hist.c` checks the `run` line. The
three Windows runs with the stand-in were made again on the release's tree (`BUILD-NOTES.md`
34.6). Not tried on the PC: `profile.c` itself (it needs ESP-IDF's GPTimer and FatFs); its
changes were read, and built for both boards with no new warning.

Not tried: the board (nothing here can run the S31's interrupt, its PC record, its cache counters or
its card).


## 9. For Plat

`TEST-PLAN.md` 19: `profile = 1` in the ini, the boot console's photo, Windows and T386MON's tests
with Ctrl+Alt+F8 before and after, a minute of ordinary use, then the files back: `PROFILE.TXT`,
`PROF01.TXT`, `PROF02.TXT`, and which image was flashed. The report then says where v5.1's work
should go.
