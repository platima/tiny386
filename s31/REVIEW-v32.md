# Review: firmware v3.3 (status LED, DOS board line out), dual boot against v3.1

Written 2026-10-22 by an independent reviewer for Plat. Report only: nothing in the firmware, the images
or the other notes was changed, nothing was flashed, and no serial port was opened. Nothing here has run
on a board.

Tags: [verified] read in the named source, or seen in a build, test or run made for this review;
[inferred] follows from verified facts; [reported] someone else's statement, not checked at its origin;
[unknown] not established.

## 0. What was reviewed, and the name

The task asked for "v3.2 (tag v3.2, against v3.1)". The work step found tag v3.2 and its images already
shipped and named its work v3.3: tag `v3.3` (7e0a36a) in WSL `~/tiny386-idf-scratch/v3/tiny386`, six
commits on `v3.2` (d3c839c) [verified: `git log`, `git tag`]. So this review covers the v3.2 to v3.3
change (18 files, 1703 lines added, 23 removed [verified: the stat of `git diff v3.2 v3.3`]) and, for the
dual boot, compares the v3.3 boot files with v3.1's. The v3.1 to v3.2 changes were reviewed in the earlier
rounds and are not reviewed again here, beyond the flash refusal and the boot files. The file is called
`REVIEW-v32.md` because that is the name the task gave it.

Nothing outside `esp/` and `tests/` changed from v3.2 to v3.3, so the Linux-hosted route is not affected
[verified: `git diff v3.2 v3.3` with those two folders left out is empty].

My scripts, logs and build are in WSL `~/review-v33-fw/` (my own folder; nothing else was written).

## 1. Verdict

The work is careful and mostly right. The dual boot is untouched: the bootloaders, partition tables and
`flash_args` are byte for byte v3.1's and v3.2's, and a build from nothing reproduces all 18 shipped image
files exactly. The RMT path, the LP SRAM record, the error-code table, the panic wiring and the PCM5102A
mute all check out against their sources.

One finding matters: the panic path's bit-banged high times come out shorter than the host test models,
because the test does not charge the loop's own instructions between a wait ending and the next rising
edge. At 320 MHz (a normal crash) the times stay inside both LEDs' windows, with a thin margin. At 80 MHz,
which is the clock during the PSRAM checks that give yellow 1, the "1" high time falls under the WS2812B
minimum. The rest are small: margins against the Korvo-1's WS2812B once the level shifter is counted, a
few tags and wordings, an unmeasured task stack.

## 2. Findings

### F1 (major) The bit-bang shortens every high time by the loop's own instructions; at 80 MHz a "1" falls under the WS2812B window

Where: `esp/main/status_bb.h` lines 32 to 46 as compiled into `status_led_panic()` and `status_led_halt()`;
`tests/host/test_status.c` lines 137 to 165 (the pretend clock).

What the built code does [verified: `riscv32-esp-elf-objdump` of `images\dosboard\tiny386.elf`,
`status_led_panic` 0x2f0035f8 to 0x2f003662, and of the Korvo-1 ELF, `status_led_halt` 0x2f0036f0 to
0x2f003740; my build's ELF files are byte-identical to the shipped ones]:

* after the period wait's last `rdcycle`, nine instructions run before the store that raises the line
  (`sub`, `bltu`, `addi`, `add`, `bne` taken, `bext`, `bnez`, `mv`, `mv`; for a 1 bit the `bnez` is taken
  and a `j` follows, so ten);
* after the high wait's last `rdcycle`, two instructions run before the store that lowers it (`sub`,
  `bltu`);
* the first bit has eleven (four `lw`, three `li`, `bext`, `bnez`, two `mv`) between taking `edge` and its
  rising store.

So each high time is at least 7 cycles (a 0) or 8 cycles (a 1) shorter than `t0h` or `t1h`, more with any
taken-branch or CSR-read stall [inferred: one cycle an instruction at best; the S31 core's real costs are
[unknown]]. The low time grows by the same amount, and the bit period stays exact because edges are
scheduled. The host test charges only the counter reads and the stores, the same cost for both edges, so
this difference cannot show there [verified: `test_status.c` lines 145 to 165]. Its "every edge inside
both windows" is true of `status_bb.h` with that clock, not of the code as compiled.

How large [inferred: my model, WSL `~/review-v33-fw/bbgap/bbgap.c` and `out.txt`, which is `status_bb.h`
and `status_code.c` unchanged with 7 extra cycles before a rising store for a 0, 9 for a 1, and 1 or 3
cycles a pass round the wait]:

| CPU clock | T0H sent | T1H sent | WS2812B-2020-V6 window | Inolux window |
|-|-|-|-|-|
| 80 MHz | 225 to 262.5 ns | 525 to 562.5 ns | T0H 220 to 380, T1H 580 to 1000 | T0H 150 to 450, T1H 450 to 750 |
| 320 MHz | 309 to 319 ns | 628 to 638 ns | as above | as above |

The level shifter then takes roughly another 38 ns off each high time at the LED [reported: STATUS-LED.md
section 5, CHANGES-A2.md section 4]. At 320 MHz that leaves T1H about 590 to 600 ns at the LED, inside
580 but only just. At 80 MHz it leaves T1H about 490 to 525 ns, under the WS2812B-2020's 580 ns [verified:
WS2812B-2020-V6 datasheet p. 5, "T1H 580 ns to 1 us"] and near the Inolux 450 ns floor.

When 80 MHz applies: a PSRAM chip that is not found, or the DOS board's memory test failing, aborts in
`cpu_start.c` before `esp_clk_init()` raises the clock [verified: ESP-IDF `components/esp_system/port/
cpu_start.c` lines 662 to 670 (`esp_psram_chip_init`, `abort()`), 764 to 770 (`esp_psram_extram_test`,
`abort()`), 848 (`esp_clk_init()`); `CONFIG_BOOTLOADER_CPU_CLK_FREQ_MHZ 80` in both builds' `sdkconfig.h`].
That is exactly the yellow 1 case, the code STATUS-LED.md and TEST-PLAN give for a first DOS board boot
that stops early. On the Korvo-1's WS2812B the "1" bits may read as 0, so yellow 1 could show as another
colour or dark [unknown: where a real WS2812B puts its 0/1 threshold is not in any datasheet to hand].

Fix:
1. In `status_bb.h`, work out the next bit's `high` and `period` before the period wait (during the low
   time), so that the rising store follows the wait's `bltu` as directly as the falling store does; then
   check in the disassembly that both stores sit straight after their `bltu`. Or, more simply, add a
   lead of about 8 cycles to `t0h` and `t1h` in `status_bb_timing()` and measure it from the objdump.
2. In `test_status.c`, charge the instructions between each wait's exit and its store (the counts above,
   from the objdump), and keep the 80 MHz runs; then the test sees what the board will.
3. Add a scope check of TP948 during a forced early panic to TEST-PLAN (a `CRASH_TEST` build exists).

### F2 (minor) The bit times sit on the Korvo-1 LED's window edges once the shifter is counted

Where: `status_code.h` lines 151 to 167; STATUS-LED.md section 5.

* RMT path: T0L 962.5 ns sent becomes about 1000 ns at the LED with the shifter's ~38 ns [inferred], on
  the WS2812B-2020's 1000 ns maximum [verified: datasheet p. 5]. STATUS-LED.md says "the sent times leave
  room for it"; for T0L they do not quite.
* The Korvo-1's LED is a WS2812B [verified: Korvo-1 schematic text layer, U3 "WS2812B", Q2 "LBSS138LT1G",
  R93, R91, `WS2812_CTRL` to GPIO37 through R21], of a revision not known [reported: status_code.h]. Older
  Worldsemi WS2812B datasheets give T1H 0.8 us and T1L 0.45 us, each plus or minus 0.15 us [reported:
  components101.com summary; not read at Worldsemi]. Against those, 662.5 ns less ~38 ns is under 650 ns,
  and T1L 637.5 ns is over 600. Espressif's own RMT LED examples use T1H 0.9 us [reported: CHANGES-A2.md
  line 54, citing ESP-IDF `led_strip_encoder.c`].
* Suggestion [inferred]: T1H 700 ns (56 RMT ticks) is inside every window named here (Inolux 450 to 750,
  WS2812B-2020 580 to 1000, older WS2812B 650 to 950) before and after the shifter; keep the 1.3 us bit.
  Low times matter far less to these parts than high times [inferred], so T1L can give.

Whether any of this shows on the board is [unknown]; TEST-PLAN already asks for a video of the first boot.

### F3 (minor) Yellow 1 does not cover every PSRAM failure

Where: STATUS-LED.md section 1 ("A PSRAM failure gives yellow 1, not dark") and section 2 row yellow 1.

With `CONFIG_SPIRAM_BOOT_INIT` set and `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` not set, a failure in
`esp_psram_init()` (after the chip was found) does not abort; start-up goes on without PSRAM [verified:
`cpu_start.c` lines 673 to 680; both `sdkconfig.h`]. tiny386 then starts (white) and stops later on its
PSRAM allocation, red 5 [inferred: `esp_main.c` line 1015]. Yellow 1 covers the chip not being found
(`CONFIG_SPIRAM_BOOT_HW_INIT` set, `CONFIG_SPIRAM_IGNORE_NOTFOUND` not set) and, on the DOS board only,
the memory test (`CONFIG_SPIRAM_MEMTEST` set for `dosboard`, not for `korvo1`) [verified: both
`sdkconfig.h`]. Fix: say "PSRAM not found, or (DOS board) failing its memory test"; other PSRAM trouble
ends as red 5.

### F4 (minor) The replay after RESET is tagged as verified; LP SRAM through the RESET button is not known

Where: TEST-PLAN.md lines 421 to 424 ("[verified: `test_status`]"); STATUS-LED.md section 4, untagged.

The test verifies the pattern. Whether the LP SRAM keeps the record (and the console ring) through the
Korvo-1's RESET button, which pulls CHIP_PU, is [unknown]; ESP-IDF maps that reset to
`ESP_RST_POWERON` [verified: `esp_system/port/soc/esp32s31/reset_reason.c` line 22], and the code then
relies on the record alone [verified: `status_led.c` lines 232 to 234]. Fix: tag it [unknown] in both
files and make it one of the things step 10 notes.

### F5 (minor) The LED task's stack is 3 KB and not measured

Where: `status_led.c` line 246. The task calls `console_uart_printf()` (newlib `vsnprintf`) and the RMT
driver; the project's other tasks use 4096 bytes [verified: `esp_main.c` 1092, `dosboard.c` 520,
`ch9350.c` 581, `i2s.c` 261]. Overflow is [unknown]. Fix: 4096, or log
`uxTaskGetStackHighWaterMark()` once in a test build.

### F6 (note) If the LED task cannot be created, the LED is not dark

Where: `status_led.c` lines 245 to 247 and 253 to 256. The first frame (white, or the replay's first
white flash) has already gone out, so the LED stays lit, while the console says it "stays dark but for a
crash" [inferred]. Fix: the wording, or send black before giving up.

### F7 (note) The second wrap is defined for every console board but linked only for LED boards

Where: `crash.c` lines 127 to 134; `esp/main/CMakeLists.txt` lines 9, 54, 89 to 96. Today both lists are
`korvo1 dosboard` [verified], so it links. A console board added without an LED would leave
`__real_esp_system_reset_modules_on_exit` undefined [inferred]. Fix: put the function under
`#ifdef STATUS_LED_GPIO`.

### F8 (note) previous-v3.2\SHA256SUMS names files that are not in that folder

14 of its 37 lines (the REVIEW files, `dependencies.lock`, the .bat and .cmd files, `card\tiny386\*`) name
files that live in `v3\`, so `sha256sum -c` run inside `previous-v3.2\` reports them missing; run from
`v3\` all 14 verify, and the other 23 verify inside the folder [verified]. README.md line 45 could say so.

### F9 (note) Small source and tag points

* The LP-core summary says the LP core "can only drive GPIO0 to GPIO7" [verified]. The LP IO MUX covers
  only those [verified: `soc_caps.h` line 257, `SOC_RTCIO_PIN_COUNT 8`], but whether the LP core can write
  the HP GPIO registers while the HP domain is up is marked [unknown] in the research note itself (section
  3). The decision not to build it stands on the other reasons.
* Inolux page: the timing table is on page 11 of 19 of datasheet v2.2; page 10 has the 80 us reset text
  [verified: `IN-PI554FCH.pdf` from inolux-corp.com, text layer]. `status_code.h` line 152 and
  `test_status.c` line 50 say p. 10.
* The WSL tree has `esp/dependencies.lock` modified against `v3.3` (the copy build.sh makes) [verified:
  `git status`]. Harmless; noted so that nobody reads it as an uncommitted change.

## 3. Checked and found right

WS2812 protocol and timing

* Order green, red, blue, most significant bit first, in both datasheets [verified: WS2812B-2020-V6 text
  "transmit data in the GRB order"; Inolux v2.2 "sequence of GRB, and the MSB is sent first"].
* The Inolux numbers in the code match datasheet v2.2: T0H 0.3, T1H 0.6, T0L 0.9, T1L 0.6 us, each plus or
  minus 0.15 us, bit 1.25 plus or minus 0.6 us, reset 80 us or more [verified: datasheet downloaded from
  inolux-corp.com to WSL `~/review-v33-fw/dl-inolux/`]. The WS2812B-2020-V6 numbers match its p. 5
  [verified].
* The RMT frame is exact: 27 and 77 ticks for a 0, 53 and 51 for a 1, a 300 us low lead of two
  12000-tick halves (inside the 15-bit field), 25 symbols in a 48-word block [verified: `status_code.c`,
  `test_status` output; `soc_caps.h` line 287].

RMT on the S31 in this ESP-IDF (6.2.0, 188e3e55bb)

* `SOC_RMT_SUPPORTED 1`, 48 words a channel, `RMT_CLK_SRC_DEFAULT` is REF_F80M, so 80 MHz is a divider of 1
  [verified: `soc_caps.h` lines 62 and 287, `clk_tree_defs.h` line 284]. The symbol layout is checked at
  run time before use (`status_led.c` lines 118 to 120); the line idles low (`init_level 0`, `eot_level
  0`); the buffer is not touched until the last frame is done (`rmt_tx_wait_all_done`) [verified].

Panic path

* Interrupts are off and the other core is stalled before `esp_panic_handler()` runs [verified:
  `esp_system/port/panic_handler.c` lines 256 to 274; inferred for the trap's own MIE clear].
* Everything `status_led_panic()`, `status_led_halt()` and `audio_panic_quiet()` call is in internal RAM
  or ROM: `status_panic_pattern`, `status_record_put`, `status_bb_timing`, ROM `esp_rom_get_cpu_ticks_per_us`,
  `rom_gpio_set_output_level`, `esp_rom_delay_us` [verified: my objdump of both ELF files]. ESP-IDF's
  own `esp_panic_handler` runs from flash (the wrap's call is at 0x40063dbc) after `panic_enable_cache()`
  [verified: objdump; `panic_handler.c` lines 298 to 334], so internal RAM is a margin here, not a need.
* The wrap is reached only from the halt stub: `esp_system_reset_modules_on_exit` has no caller outside
  `system_internal.c` but `panic.c` [verified: grep of ESP-IDF], the restart path calls the real one
  inside its own object [verified: objdump, `esp_restart_noos_inner`]. On the S31 that function only
  drains the UARTs, sets the ECC control and the crypto clock [verified: `system_internal.c` lines 43 to
  58], so GPIO37's routing survives it. All watchdogs are disabled before the halt [verified: `panic.c`
  lines 493 to 496], so the endless LED loop is not reset.
* The ROM's ticks-a-microsecond follows every CPU clock change [verified: `esp_hw_support/port/esp32s31/
  rtc_clk.c` lines 181, 193, 212, 276]; the cycle counter is the standard `cycle` CSR (no
  `SOC_CPU_HAS_CSR_PC` on the S31) and is rescaled on a clock change [verified: `rv_utils.h` line 120,
  `rtc_clk_init.c` line 89]. Each wait is three instructions [verified: objdump].
* The leading 300 us low covers a frame the RMT was part way through when the pin was taken [inferred].

LP SRAM and the console ring

* `.rtc_noinit` runs 0x2e000000 to 0x2e00201c: `s_ring` (8208 bytes) then `s_rec` (12 bytes); ESP-IDF's
  `.rtc_reserved` is at 0x2e007fe8; LP RAM is 0x2e000000 to 0x2e008000 [verified: `readelf -S`, `nm`;
  `soc.h` lines 163 and 164]. The ring's indices are masked (`console.c` lines 120 and 163), so nothing
  reaches the record. `CONFIG_BOOTLOADER_RESERVE_RTC_SIZE 0` [verified].
* No bootloader, no tiny386 app and not the Linux loader (`s31-linux-loader.bin`, be5b725) has a load
  segment in LP RAM, so no boot overwrites the record [verified: esptool `image-info` of each]. What Linux
  itself does with LP SRAM at run time is [unknown], as STATUS-LED.md says; the check word turns any
  overwrite into "no record".

Error codes against the code

* Every `status_led_fatal` and `status_led_error` call site matches its STATUS-LED.md row: red 1 in
  `mount_storage()`, red 2 in `choose_ini()` and the ini open, red 3 from `check_config()` and
  `load_rom()`, red 4 the other `check_config()` problems, red 5 from `console_fatal()` (which keeps an
  earlier named error), yellow 1 the early panic, yellow 2 `wrong_board()` and `hdmi_task`, yellow 3 the
  hot plug, yellow 4 the USB host (Korvo-1) and the silent CH9350L (DOS board), yellow 5 the beat watch
  [verified: `esp_main.c`, `dosboard.c`, `ch9350.c`, `usb_input.c`, `console.c`]. The priority order, the
  blink and pause times and the replay length (2.9 to 9.9 s) match the code [verified: `status_code.c`].
* The reset reasons the replay uses are mapped on the S31 (watchdogs, brownout, glitch, lock-up)
  [verified: `reset_reason.c` lines 19 to 72].
* The CH9350L timer uses `int64_t` microseconds from `esp_timer_get_time()` [verified: `ch9350.c` line
  504]. GPIO37 and GPIO48 are not used by anything else in either board [verified: grep].

PCM5102A mute

* XSMT is set low before the pin is made an output, from `i2s_main()` on; raised 20 ms after
  `i2s_channel_enable()`; in a panic it goes low first, then after 4 ms the TX is stopped (`tx_start`
  cleared, bit 2 of I2S0 at offset 0x24 in the objdump), and only if the channel was started [verified:
  `i2s.c`; objdump of `audio_panic_quiet`]. 4 ms covers TI's 150 samples plus 0.2 ms at 48 kHz (3.33 ms);
  the ramp is 104 samples; zero detect is 1024 LRCK [verified: SLAS859C text layer]. The firmware has no
  `esp_restart()` [verified: grep], so the only unmuted stops are power and RESET, as LINE-OUT.md says.

Dual boot, unchanged

* Bootloader, partition table and `flash_args` of all three sets are byte for byte the same in v3.1
  (WSL `~/tiny386-idf-scratch/v3-work-integ/resume/rel1/images`, App version v3.1), v3.2
  (`previous-v3.2\images`) and v3.3 (`images\`) [verified: `cmp`]. The dual-boot pair is also the pinned
  pair in `dualboot\` [verified: `cmp`, build.sh's SHA-256 check]. Their inputs did not change: from v3.1
  to v3.3 only `esp/CMakeLists.txt` (the refusal) and `version.txt` changed outside `esp/main`, and no
  sdkconfig or partition file [verified: the stat of `git diff v3.1 v3.3`].
* The flash simulation, rerun on the shipped images in my own folder, gives the work step's
  `checks\flash-simulation-v3.3.txt` line for line apart from the folder name and its appended comparison:
  the dual-boot install writes only 0x2000 to 0x8FFF and 0xE20000 to 0xEF8FFF, every Linux partition
  unchanged, GPIO42 high boots the Linux loader and low boots tiny386 v3.3 [verified: WSL
  `~/review-v33-fw/sim/flash-simulation-review.txt`]. tiny386.bin uses 885,552 of the test partition's
  1,966,080 bytes [verified].
* In the dual-boot build every write target (`flash`, `app-flash`, `bootloader-flash`,
  `partition-table-flash`, `partition_table-flash`, `dfu-flash`, both encrypted ones, `erase-flash`,
  `otadata-flash`, `erase-otadata`, `erase_otadata`) depends on `tiny386_no_flash`, which echoes the
  refusal and runs `cmake -E false` [verified: `build.ninja` of my build, read only; no flash target was
  run].
* The three .bat files and `flash-paths.cmd` are byte for byte v3.2's (their SHA-256 in
  `previous-v3.2\SHA256SUMS`), and they check `tiny386.bin` by size and `image-info`, not by a fixed hash
  [verified: `flash-dualboot.bat` lines 83 and 262 to 277]; with the boot files unchanged their flow is
  the same as v3.2's [inferred].

Build and tests, rerun

* `build.sh` from nothing in WSL `~/review-v33-fw/build`: exit 0 in 170 s; all 18 files of the three
  image sets (ELF and SHA256SUMS included) byte for byte the shipped ones [verified: `cmp`]. The shipped
  patch is exactly `git diff pristine v3.3` [verified]; the tree build.sh leaves differs from it only by
  `esp/dependencies.lock`, which build.sh copies in [verified]. The only compiler warning in tiny386's
  own code is the old unused `i2s_task` on the Korvo-1 [verified: build logs].
* Host tests in the tree made from the patch: 23 of 23 pass; `test_status` 18,011 checks, 0 failed, and
  clean under AddressSanitizer and UndefinedBehaviorSanitizer [verified: `~/review-v33-fw/
  host-tests-patch.log`].
* `v3\SHA256SUMS`: all 39 lines verify [verified].

## 4. Not done

* The bat harness was not rerun: it runs the .bat flows through cmd.exe against a stub esptool, and I did
  not want any chance of a real esptool reaching a COM port. The scripts and the files they check are
  unchanged, above.
* The mutation run and the `-Wall -Wextra` rebuild were not repeated. The Linux route was not rebuilt
  (nothing it builds changed).
* No hardware: every colour, the bit timing at the LED, the crash loop and the line out are [unknown]
  until Plat runs the board.
