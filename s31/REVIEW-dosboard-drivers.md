# Review: the DOS board drivers in tiny386 v3.1 (second pass)

Written 2026-10-21 by an independent reviewer, on the `dosboard-drivers` lens: `ms7210.c` against
`tiny386-hdmi-board\research\encoder.md` section 8 row by row, and against the vendor sources for
copying; the 800x600 timing and the PCLK edge; HPD and EDID; the audio set-up and AUDIO_RATE;
`ch9350.c` against the CH9350L manual (DS26) and `research\ch9350.md`; `board_dosboard.h` against the
schematic netlist `tiny386-hdmi-board\dosboard\out\dosboard.net`; and the board check.

Tree: WSL `~/tiny386-idf-scratch/v3/tiny386`, branch integ, tag v3.1-tests (823d5cd). The DOS board
code there is v3.1's: the review fixes are commit de15941, on top of the tag v3 (2bb1c76) that the
first pass read. This pass reads the whole of the DOS board code again, the fix commit line by line,
and the schematic track's later reviews where they touch this firmware. Nothing in the tree was
changed, and nothing in `tiny386-idf\v3` apart from this file. My scripts, test programs and outputs
are in WSL `~/tiny386-idf-scratch/review-dosboard-drivers/pass2` (the first pass's are one level up).

Nothing here has run on hardware. No serial port was opened and nothing was flashed.

This file replaces the first pass's file of the same name. The first pass is kept, word for word, in
the appendix (its headings one level down), and a byte-exact copy is in WSL
`~/tiny386-idf-scratch/review-dosboard-drivers/pass2/REVIEW-dosboard-drivers.first-pass.md`, SHA-256
a699f9fc..., the value `v3\SHA256SUMS` lists [verified: sha256sum]. That line of `v3\SHA256SUMS` no
longer matches this file; I did not edit SHA256SUMS, which is outside this task, so the next
integration step should regenerate it.

Tags: [verified] read in the named source, or seen in a test or run I made; [inferred] follows from
verified facts; [reported] someone else's statement, not checked at its origin; [unknown] only the
board can say.


## 1. Short answer

The review fixes in de15941 do what REVIEW-RESOLUTION says for F1, F2, F9, F10 and rb_swap, the
table still matches `encoder.md` 8.4 in all 114 rows, every pin still matches the netlist (now the
20:51 one), AUDIO_RATE is 48000 in all four audio files of the from-nothing build, and there is no
sign of copying. All 19 host tests pass. There is no blocker and nothing major.

Five minor findings are worth fixing before first light, three of them in the new fix code:

1. S1: `i2s_slot_bits = 32`, the documented fallback for "no HDMI sound", leaves the WS line high
   for 16 of 64 bit clocks, which is not an I2S frame. The try can fail even when 32-bit slots are
   what the MS7210 wants.
2. S2: row B15 still sets the MS7210's video pad pulls to pull-down. The schematic track passed the
   change (pull-up or floating) to this track, because those pull-downs load the S31's GPIO36
   VDD_SPI strap after a RESET press. It was never picked up.
3. S3: the new reset check in the hot plug poll relies on register 0x120C reading back what D1
   wrote. No source reads that register. If it does not read back, the chip is set up again every
   half second for ever and the monitor never holds a picture.
4. S4: the first-light line that F3's fix added is cut at 111 characters by `dosboard_note()`; on a
   bad first light the PCLK meter value itself is lost, on the UART and the console alike.
5. S5: ESP-IDF's own register description settles which edge `pclk_active_neg = 1` launches data on.
   On that reading the default pair (plan A) launches and samples on the same edge, the pairing
   `encoder.md` 9 calls marginal. Make the reference tables' pair the default.

And five notes (S6 to S10). Section 3 gives the status of each first-pass finding.


## 2. Findings

Ranked, most important first.

### S1 (minor). `i2s_slot_bits = 32` gives a WS line high for 16 of 64 bit clocks

* Where: `esp/main/i2s.c` lines 161 to 181 (163 the Philips helper macro, 179 to 180 the 32-bit
  slot); `dosboard.c` line 418; `card-dosboard\tiny386\dosboard.ini` (the i2s_slot_bits comment);
  `TEST-PLAN.md` line 388.
* What: the F4 fix sets only `slot_bit_width` to 32. The helper macro has already set `ws_width`
  to the sample width, 16 (ESP-IDF `esp_driver_i2s/include/driver/i2s_std.h` lines 140 to 151,
  the branch for SOC_I2S_HW_VERSION_2, which esp32s31 has: `soc_caps.h` line 669). ESP-IDF does not
  adjust it: `i2s_std.c` only normalises the slot width (lines 135 to 142), and the HAL writes
  `ws_width` straight into the TX WS width field (`esp_hal_i2s/i2s_hal.c` line 155,
  `esp32s31/include/hal/i2s_ll.h` lines 783 to 786). ESP-IDF's I2S guide says that when the slot
  width is changed after a helper macro, `ws_width` must be set to the slot width for the Philips
  and MSB formats, to keep WS at 50 per cent (`docs/en/api-reference/peripherals/i2s.rst` line 464).
  The DOS board build itself confirms the value: `i2s.c` preprocessed with the from-nothing build's
  own compile command has `.ws_width = I2S_DATA_BIT_WIDTH_16BIT` (`pass2/pp/i2s.c.i` line 7152).
  [verified]
* Why it matters: with `i2s_slot_bits = 32` the frame is 64 bit clocks with WS high for 16 of them,
  not the 32-bit-slot Philips frame the knob is meant to try [inferred from the verified register
  path]. TEST-PLAN and dosboard.ini give this as the step after a cable replug when there is no
  sound, so a failure there could rule out the very setting the MS7210 needs [inferred].
* Fix: `tx_std_cfg.slot_cfg.ws_width = 32;` beside line 180. At first light, TP938 (I2S_WS) on a
  scope should show a 50 per cent duty cycle in both settings. While there: schematic review V13
  suggests swapping BCLK and WS if sound stays silent, which today needs a rebuild
  (`board_dosboard.h` lines 93 and 94 are #defines) [verified]; an ini key for that swap would save
  one.

### S2 (minor). Row B15 still turns on the MS7210 pad pull-downs that load the GPIO36 strap

* Where: `esp/main/ms7210.c` line 49, `R("B15", RMW, 0, 0x0079, 0x30, 0x20, 0)`; the host test's
  expectation and `tests/host/dosboard/check_ms7210_table.py`, which compare it with `encoder.md`
  8.4 row B15.
* What: the schematic track found that LCD_D15 (GPIO36, the S31's VDD_SPI strap, which must read 1
  at reset) also drives MS7210 D23 and D18 (pins 36 and 41), that the vendor's set-up puts those
  pads on pull-down, and that R603 keeps the MS7210 running across a reset of the S31 alone (RESET
  button, esptool's EN pulse, a watchdog or panic restart), so the pull-downs load the strap when
  it is sampled at the next boot. Its resolution fitted R206 2.2 k and passed the firmware half to
  this track: 1 or 0, not 2, in register 0x0079 bits 5:4 [verified:
  `dosboard\out\REVIEW-SCH-RESOLUTION.md` row V1, `dosboard\SCHEMATIC-NOTES.md` section 10; the
  later `REVIEW-SCH-video-pins.md` finding V10 says it was still not done]. At v3.1-tests the row
  still writes 2 [verified: `ms7210.c` line 49; my test `pass2/ms_pass2.c` case C]. In the SDK the
  field is 0 floating, 1 pull-up, 2 pull-down, for the video data, sync, DE and clock pads
  [verified: `research\dl-encoder\d-robotics-x5-kernel-ms7210\ms7210\misc\ms7210_drv_misc.h` lines
  53 to 64, `misc\ms7210_drv_misc.c` lines 188 to 191]. Nothing in `tiny386-idf\v3` mentions 0x0079
  apart from the table and its test logs [verified: grep], and REVIEW-RESOLUTION has no row for it.
  The net is as the schematic review says: LCD_D15 joins U201 pad 45, U601 pins 36 and 41 and R206
  2.2 k to +3V3, and HDMI_RSTN has R603 27 k to +3V3 [verified: `dosboard.net` of 20:51].
* Why it matters: the strap margin then rests on the MS7210's pull-down strength, which is not
  published; with R206 2.2 k each pad must be at least about 10.8 k [reported: schematic review
  arithmetic]. A strap read as 0 selects the 1.8 V flash supply for a 3.3 V flash
  [reported: `research\s31-module.md` section 3], so a board could boot from cold and fail after a
  RESET press, the hardest kind of fault to read at the bench [inferred].
* Fix: value 0x10 (pull-up, which also helps the strap) or 0x00 (floating); every pad the field
  covers is driven by the S31 once the RGB output runs [inferred]. This departs from `encoder.md`
  8.4 on purpose, so mark it as the PCLK meter reads are marked: in the row's comment, in
  `check_ms7210_table.py` and in the host test, with the reason in BUILD-NOTES. Firmware cannot help
  a cold boot if the MS7210's reset default is itself pull-down [unknown].

### S3 (minor). The reset check assumes that 0x120C reads back what D1 wrote

* Where: `esp/main/ms7210.c` lines 493 to 512 (`ms7210_poll()`); `dosboard.c` lines 424 to 437
  (`hdmi_task()`, every 500 ms).
* What: the F8 fix reads 0x120C on every poll and, if it is not 0x20, takes the chip as reset and
  runs `ms7210_init()` (sections B to E and the EDID read). No source I have reads 0x120C: the SDK
  only writes the DVIN timing block (`dvin\ms7210_drv_dvin.c` line 259) and reads timing from
  separate detect registers, 0x1208 to 0x120A (lines 182 to 184), and the two flat tables skip
  section D altogether (`encoder.md` 8.4 D) [verified]. Whether 0x120C reads back is [unknown].
* Evidence of the consequence: with a fake MS7210 whose 0x120C reads 0x00 whatever was written, six
  polls returned 2 six times, and each ran B1 to E72 and the EDID read again; at the nominal bus
  rates that is about 234 ms of I2C traffic and waits in every 500 ms
  [verified: `pass2/ms_pass2.c` case B2]. B1 holds the MS7210's video input in reset and E1 turns
  the TMDS clock off each time, so a monitor would lose the picture twice a second, with a yellow
  "was reset" line each time [inferred]. Only a rebuild cures it. The host test cannot see this,
  because its fake chip reads back every register it was given (`tests/host/dosboard/ms7210_test.c`
  lines 79 to 96) [verified]. With a normal read-back the check is quiet: six polls, no set-up
  (case B) [verified].
* Likelihood: low. The SDK read-modify-writes other registers in the same block (0x1201, 0x1202,
  0x1206, 0x120B: rows B6, B7, B14, D13, D14), so that block's settings do read back there
  [verified: `encoder.md` 8.4 sources]; that 0x120C behaves the same is [inferred]. Separately, a
  reset that happens to leave 0x120C at 0x20 is not noticed (case B3) [verified: test; the reset
  value is unknown].
* Fix: after each `ms7210_init()`, read 0x120C once; if it does not hold 0x20 straight after being
  written, log once that the reset check is off and skip it from then on. Also stop after a few
  set-ups in a row with no HPD change. A `[dosboard]` key to turn the check off would be cheap.

### S4 (minor). The first-light line is cut short by `dosboard_note()`

* Where: `esp/main/dosboard.c` lines 43 and 65 to 82 (`NOTE_LEN` 112, one `vsnprintf()` for the
  UART and the console); `ms7210.c` lines 173 to 184 (`logf_()`, 160 bytes) and 453 to 460
  (`report()`).
* What: `report()`'s line is 119 characters on a good first light and 129 on a bad one;
  `dosboard_note()` keeps 111. The good line loses the end of the MHz figure; the bad one (PLL not
  locked, input not stable, monitor not attached) ends at "PCLK meter now " with the value gone
  [verified: `pass2/notelen.c`, using `report()`'s format]. That value is the reading F3's fix added
  for `encoder.md` 8.7 step 2. Every MS7210 line reaches the UART only through `dosboard_note()`
  (`io_log()`, lines 162 to 165), so the UART is cut the same way [verified]. The EDID line fits,
  at 110 characters with a 13-letter monitor name [verified: same test].
* Fix: `NOTE_LEN` 160 to match `logf_()` (24 notes of 160 bytes is under 4 KB of PSRAM), or end
  `report()`'s line after the monitor and log the meter on a line of its own.

### S5 (minor). The default PCLK pairing is probably the same-edge one

* Where: `board_dosboard.h` lines 49 to 52 (`LCD_PCLK_ACTIVE_NEG 1`); `dosboard.c` lines 101 and 102
  (`pclk_phase 0`); `lcd_dosboard.c` line 68; the order of the four pairs in `dosboard.ini` and in
  `TEST-PLAN.md` line 384.
* What: the first pass (F5) left open which of ESP-IDF's two descriptions of `pclk_active_neg` is
  right. The register description settles the S31 side. The flag goes to `lcd_ck_out_edge`
  (`esp_lcd/rgb/esp_lcd_panel_rgb.c` line 658, `lcd_ll.h` line 301), and the esp32s31 register
  headers describe that bit, when set, as PCLK being high in the first half of each data cycle
  (`soc/esp32s31/register/soc/lcd_cam_struct.h` lines 31 to 35, `lcd_cam_reg.h` lines 38 to 41;
  the esp32s3 header describes the bit the same way) [verified]. Then with `pclk_neg = 1` the data changes on the
  rising edge and is meant to be taken on the falling edge, as `lcd_ll.h` line 298 says, and not
  "clocked out on the falling edge" as the public header line 72 reads [inferred from the register
  text]. The MS7210 at 0x1281 = 0 samples on the rising edge if its data sheet figure 4 shows the
  un-inverted case (`encoder.md` 4.6) [unknown]. On those readings plan A (`pclk_neg` 1, phase 0)
  launches and samples on the same edge, which `encoder.md` 9 calls the only marginal pairing
  [inferred]. A marginal pairing can look clean on a cool bench and sparkle when warm, or on another
  of the ten hand-built boards [inferred].
* Fix: make `pclk_neg = 1, pclk_phase = 4` the default (what both reference tables write, and
  opposite edges on the reading above), then `pclk_neg = 0, pclk_phase = 0`; keep the other two
  pairs last. At first light, try all four, and prefer a pair whose neighbours in delay (phase 5, 6,
  7 are 4 with delay, per `dosboard.ini`) also give a clean picture, which shows margin rather than
  luck [inferred]. No code change beyond the default, `dosboard.ini` and TEST-PLAN.

### S6 (note). F1's fix also sends HDMI to a DVI monitor with a good extension that is not CEA-861

* Where: `ms7210.c` `choose_mode()` lines 422 to 431.
* What: any block 1 that is not a CEA-861 block with a good checksum now means "HDMI assumed". My
  test gives HDMI with sound and both InfoFrames for a DVI-only EDID whose block 1 is a valid
  DisplayID extension (tag 0x70), and for a block map in block 1 with a CEA block without an HDMI
  vendor block in block 2 [verified: `pass2/ms_pass2.c` case A]. Some DVI-only monitors show
  nothing when sent HDMI data islands [inferred]. Such EDIDs are rare on DVI-only monitors
  [inferred], and `hdmi_mode = dvi` is the way round it [verified: `dosboard.c` lines 321 to 329].
* Fix (optional): keep "HDMI assumed" for a block 1 that does not answer or fails its checksum, and
  take DVI for a good block 1 that is not CEA-861 when `edid[0x7E]` is 1. A block map with more
  blocks needs the E-DDC segment pointer to read block 2, so HDMI there as now.

### S7 (note). Two settings still have no range check; a negative `kbd_zero_guard_ms` sticks keys

* Where: `dosboard.c` lines 315 and 346 to 347; `ch9350.c` line 489 and `kbd()` lines 85 to 95.
* What: `kbd_zero_guard_ms` takes any integer and is passed on as `zero_guard_ms * 1000u`, so -1
  becomes 4,294,966,296 us. Every all-zero keyboard frame after any mouse frame is then dropped:
  in my test a key let go a minute after the last mouse move was never released (release passed on
  0 times, dropped by the guard once) [verified: `pass2/ch_pass2.c`]. With typematic repeat the key
  would then repeat until the next keyboard frame [inferred]. `pclk_phase` is `atoi(value) & 7`, so
  `8` or `yes` silently give 0 [verified: source]. The F12 fix's "a value that cannot be read is
  named" covers the yes/no keys, `hdmi_mode`, `i2s_slot_bits` and `audio_shift` only [verified].
* Fix: 0 to 50 for the guard and 0 to 7 for the phase, through the existing "could not be read"
  path.

### S8 (note). The board check could also try address 0x2B before halting

* Where: `ms7210.h` line 18 (`MS7210_I2C_ADDR 0x59`, used by every register access); `dosboard.c`
  lines 274 to 279.
* What: schematic review V14 points out that MacroSilicon's data sheet figure 3 draws SA to ground,
  against its own text; the board pulls SA up with R602 10 k, so 0x59 [reported:
  `REVIEW-SCH-video-pins.md` V14; R602 and U601 pin 32 verified in the netlist]. If SA's sense were
  the other way, a good DOS board would halt with "nothing answered" [inferred]. On a Korvo-1 nothing
  answers at 0x2B either [inferred: its bus holds the codec and the touch controller, reported in
  `research\firmware-design.md`].
* Fix (optional): probe 0x2B after 0x59 and say which answered; the address would then have to be a
  field of `ms7210_t` rather than a constant.

### S9 (note). CH9350L state 0, the fallback: two leftovers

* A report-protocol mouse with 16-bit axes still moves wrongly in state 0: a move of +5, -3 came out
  as dx 5, dy 0, wheel -3 [verified: `pass2/ch_pass2.c`]. REVIEW-RESOLUTION D F9 accepts this on
  purpose, so that a 5-byte mouse still works in the fallback [verified]. No change while state 0 is
  only a fallback.
* The answer to a 0x82 in state 0 is `57 AB 12 00 00 00 00 LED 00 AC 20` [verified: `ch9350.c`
  lines 315 to 329 and the test]. DS26 7.2.4.2 gives `57 AB 12 00 00 00 00 FF 80 00 20` as the
  frame that makes a lone chip in state 0 stop its once-a-second requests (DS26 p.11) [verified].
  Answering every request is harmless at one a second [inferred]; whether the LEDs work in state 0
  at all is [unknown].

### S10 (note). The first pass's table named only R204 for the status LED

* The LED is D201 with its cathode on GPIO37 and its anode through R204 1 k to +3V3, and R205 10 k
  pulls GPIO37 up so that the LED is off until the firmware drives it [verified: `dosboard.net`].
  Low = lit, as `board_dosboard.h` line 119 says. Only the first pass's wording was short; the code
  is right.


## 3. The first pass's findings at v3.1-tests

| # | First pass | Now | Evidence |
|-|-|-|-|
| F1 | DVI and no sound for an unusable EDID extension | fixed; side effect S6 | `pass2` case A: NAK twice and bad checksum give HDMI with sound (0x0503 00, 0x0531 84, 0x050E 60); a DVI EDID with no extension still gives DVI [verified] |
| F2 | a large lcd_bounce_px leaves no picture | fixed | divides-480000 check, then the clamp to 16,000 (which divides 480000) in `dosboard.c` lines 404 to 416; a failed start is retried without bounce buffers (`lcd_dosboard.c` lines 116 to 123); ESP-IDF's error path frees the panel, frame buffer, DMA and GPIOs before the retry (`esp_lcd_panel_rgb.c`, `lcd_rgb_panel_destroy()`) [verified: source; not run] |
| F3 | PCLK meter read straight after it is switched on | fixed, but see S4 | `report()` reads 0x001E and 0x001F again and logs them [verified] |
| F4 | knobs needing a rebuild | rb_swap and audio_shift fixed; i2s_slot_bits wrong, S1 | rb_swap writes the row's mask, 0x10, and keeps the other bits (host test) [verified]; `i2s.c` lines 36 to 39 divide by 1 << audio_shift, 0 to 8 checked in `dosboard.c` lines 353 to 357 [verified] |
| F5 | two ESP-IDF readings of pclk_active_neg | documented; S5 takes it further | `dosboard.ini` lists the four pairs [verified] |
| F6 | audio FIFO reset before I2S runs | documented | TEST-PLAN line 386 [verified] |
| F7 | I2S clock from the crystal | documented | TEST-PLAN line 389 [verified] |
| F8 | a reset MS7210 never noticed; LED | fixed, but see S3 | case B: six quiet polls when 0x120C reads back [verified]; LED lit while the poll fails (`dosboard.c` lines 432 to 435) [verified] |
| F9 | state 0 keyboard reports trusted | fixed for the keyboard; mouse left on purpose (S9) | `ch9350.c` line 133; an NKRO report is counted as bad [verified: `pass2` test] |
| F10 | LED answers to every 0x80 | fixed | one answer in 100 ms, status frames counted and logged (`ch9350.c` lines 417 to 434, 509 to 527) [verified: source] |
| F11 | USER key to skip the board check | deferred (Plat's call) | no change [verified] |
| F12 | small things | fixed in part (S7) | case-blind words and the bad-value path for most keys; `ch9350_ps2_leds_to_usb()` host-only; FLASH.txt names the CH340C [verified] |


## 4. What I checked and found right in this pass

* Host tests at v3.1-tests, run into my folder: 19 passed, 0 failed, including the MS7210, the table
  check against `encoder.md`, the CH9350L, AUDIO_RATE at both rates and the storage test.
  `git status` of the tree was the same before and after (only `esp/dependencies.lock`, as the
  integration found) [verified: `pass2/hostout.log`, `status-before.txt`, `status-after.txt`].
* The table: my own `cmp_table.py` against `encoder.md` 8.4 finds 114 rows in both, the same order,
  0 hard mismatches; B7, now flagged for rb_swap, still writes 0x00 by default. The nine rows it
  leaves for a check by hand (A1, B22, C1, E38, E40, E46, E58, E64, E68) are the ones the first pass
  checked by hand, and the fix commit did not touch them [verified: script output, `git diff`].
* The fix commit adds no register write that is not in 8.4: the new accesses are reads only (0x001E,
  0x001F, 0x120C), and the table check allows exactly those as additions [verified:
  `git diff v3 v3.1-tests`, `check_ms7210_table.py` lines 86 to 90].
* No copying: the copy check over the current `ms7210.c` and `ms7210.h` against the 19 vendor files
  finds no five-word comment phrase in common, only the two EDID terms of four words found before,
  and only generic identifiers; the same for `ch9350.c` and `ch9350.h` against Zephyr's driver: none
  [verified: `pass2/more.sh` output]. The structure is still a table interpreter keyed by 8.4's row
  names, unlike the SDK's function per block [verified].
* Pins against the netlist of 20:51: the module's nets are identical to the 18:32 netlist the first
  pass used, and every pin in `board_dosboard.h` matches, as in the first pass's table (appendix 3.4)
  [verified: `netpins.py`, `diff`]. The MS7210 data inputs, pin by pin: B7..B3 from LCD_D4..D0, B2..B0
  from D4..D2, G7..G2 from D10..D5, G1 and G0 from D10 and D9, R7..R3 from D15..D11, R2..R0 from
  D15..D13, as `encoder.md` 4.6 [verified]. CH9350L: SEL pulled up (lower mode), S0 1 k to ground,
  S1 and the BAUD pins open (state 2, 115200), RST 10 k to ground, TXD to GPIO47, RXD from GPIO55
  with 10 k to +3V3 [verified]. S0 on GPIO46 is not a strap; the straps are GPIO36, 37, 60 and 61
  [reported: `research\s31-module.md` section 3, from the hardware design guide].
* Timing: `lcd_ll_set_horizontal_timing()` and `..._vertical_timing()` (`lcd_ll.h` lines 817 and
  834) give totals of 128 + 88 + 800 + 40 = 1056 and 4 + 23 + 600 + 1 = 628, with the driver passing
  800 as the active width; 40 MHz is the 160 MHz PLL divided by 4 [verified: source; arithmetic].
* AUDIO_RATE: `adlib.c`, `pcspk.c`, `sb16.c`, `audio_rate.h` and `board_dosboard.h` are unchanged
  since v3, and `i2s.c` changed only for the slot width and the level [verified: `git diff`], and
  preprocessing them with the compile commands of the integration's from-nothing DOS
  board build gives `s->freq = 48000`, `48000 * s->pit_count`, 48000 six times in `sb16.c` and
  `.sample_rate_hz = 48000` [verified: `pass2/pp`]. Rows E50 to E52 still set 48 kHz, 16 bits, two
  channels [verified].
* HPD and EDID: F1's fix reads block 1 twice before giving up and logs what it assumed; the DDC
  pass-through order and bus speeds are as the first pass found [verified: source and `pass2`
  case A].
* CH9350L against DS26 pages 10 to 14, read again: opcode lengths, the state 0 and 1 data frame with
  its length, label and sum over data and sequence, the 0x81 layout and sum, the state 2 keyboard
  and mouse frames, and the 0x12 answer frame [verified]. A mouse frame holding `57 AB` is still not
  split, and a report-ID keyboard in state 0 has its ID taken off [verified: `pass2` test].
* Board check: still only GPIO0 to GPIO2 before the chip ID is seen; the DOS board's own pins after
  it [verified: `dosboard.c` lines 252 to 288].
* The I2S pins and format: BCLK GPIO3, WS GPIO4, DOUT GPIO5, no MCLK, Philips 16-bit stereo by
  default, matching U601 pins 7, 6 and 5 [verified: `board_dosboard.h`, `i2s.c`, netlist].


## 5. Files

In WSL `~/tiny386-idf-scratch/review-dosboard-drivers/pass2`:

* `ms_pass2.c`, `ms_pass2.out`: EDID cases, the reset check with and without read-back, row B15.
* `ch_pass2.c`, `ch_pass2.out`: state 0 and state 2 frames, the zero guard at -1, LED frames.
* `notelen.c`, `notelen.out`: which MS7210 lines `dosboard_note()` cuts.
* `more.sh`: copy check, table check, LCD timing and edge sources, AUDIO_RATE preprocessing.
* `edge.sh`, `ln.sh`: the PCLK edge register text and line numbers.
* `host.sh`, `hostout.log`, `hostout/`, `status-before.txt`, `status-after.txt`: the host test run.
* `pp/`: the four audio files preprocessed with the from-nothing build's commands.
* `REVIEW-dosboard-drivers.first-pass.md`: the first pass, byte for byte.

The first pass's own files are one level up (see the appendix, section 4).


## Appendix: the first pass (tag v3), as written

The text below is the first pass's file unchanged, apart from each heading moved one level down.
Its line numbers refer to tag v3.

## Review: the DOS board drivers in tiny386 v3

Written 2026-10-21 by an independent reviewer, on the `dosboard-drivers` lens: `ms7210.c` and
`ms7210.h`, `ch9350.c` and `ch9350.h`, `dosboard.c`, `lcd_dosboard.c`, `board_dosboard.h`, the
AUDIO_RATE path, and the board check. Tree: WSL `~/tiny386-idf-scratch/v3/tiny386`, tag v3
(2bb1c76). Nothing in the tree or in this folder was changed apart from this file. My scripts,
test programs and outputs are in WSL `~/tiny386-idf-scratch/review-dosboard-drivers`.

Nothing here has run on hardware. No serial port was opened and nothing was flashed.

Tags: [verified] read in the named source, or seen in a test or run I made; [inferred] follows
from verified facts; [reported] someone else's statement, not checked at its origin; [unknown]
only the board can say.


### 1. Short answer

The drivers are in good shape. Every one of the 114 MS7210 rows matches `encoder.md` 8.4 in
order, register, mask and value; the InfoFrames are right; the 800x600 timing gives exactly the
VESA totals in the ESP-IDF register arithmetic; every pin in `board_dosboard.h` matches the
schematic netlist; AUDIO_RATE 48000 reaches all four audio files; the CH9350L frame formats match
the datasheet; and I found no sign of copying from the vendor sources. There is no blocker.

The findings worth acting on before first light are small:

1. A monitor whose EDID has a good block 0 but an unreadable or bad extension is driven as DVI with
   no sound, the opposite of what the code's comment and the notes say (finding F1).
2. A `lcd_bounce_px` that divides 480000 but is too big for internal RAM leaves the board with no
   picture and no HDMI set-up, with no retry (F2).
3. The PCLK meter is read the moment it is switched on, so the first-light line can say 0 MHz on a
   good board (F3).
4. Some first-light unknowns (R/B order, I2S slot width, HDMI sound level) need a rebuild to try,
   where the PCLK edge already has ini keys (F4).

On Plat's question about the third USB-A port: the netlist confirms that flashing and the console
both go through the CH340C on UART0, with the DTR/RTS transistor pair driving EN and BOOT
(GPIO61). The S31's own USB Serial/JTAG pins (GPIO33, GPIO34) carry RGB data on this board and
its high-speed USB pads (module pads 40 and 41) are not connected, so no S31 USB is used for
anything. A third USB-A port is not needed for flashing. [verified: `dosboard.net`, U201 pads 68,
69, 71, 5 and U401 pins 2, 3, 13, 14, Q401; module pads 33 to 41 carry no net;
`esp/sdkconfig.dosboard` lines 22 to 25]


### 2. Findings

Ranked, most important first. None is a blocker.

#### F1 (minor). An HDMI monitor with an unusable EDID extension gets DVI and no sound

* Where: `esp/main/ms7210.c` `choose_mode()` lines 403 to 419, `ms7210_read_edid()` lines 504 to
  519, `ms7210_edid_parse()` lines 573 to 591.
* What: when block 0 is valid but block 1 cannot be read, or fails its checksum, or is not a
  CEA-861 block (a block map, tag 0xF0), the parse leaves `hdmi = 0` and `choose_mode()` picks DVI:
  0x0503 = 0x01, audio off, no InfoFrames. The log line then says "DVI (no HDMI vendor block)",
  which is not what happened.
* Evidence: my test `edidpol.c` (fake MS7210 and EDID, the real `ms7210.c`) [verified]:

  | EDID | Mode chosen | 0x0503 | 0x0531 | 0x050E |
  |-|-|-|-|-|
  | good block 0, good HDMI extension | HDMI | 00 | 84 | 60 |
  | good block 0, extension read NACKs | DVI (no sound) | 01 | 04 | 00 |
  | good block 0, extension checksum wrong | DVI (no sound) | 01 | 04 | 00 |
  | good block 0, block map in block 1 | DVI (no sound) | 01 | 04 | 00 |

  The code's own comment (line 413 to 414) and `dosboard/NOTES-dosboard.md` section 6 say "No
  EDID, or a bad one, gives HDMI, as the vendor's code does". The vendor code does treat a bad
  block 1 as an HDMI sink: block 1 checksum failure sets `bSucc = FALSE`, and `bSucc == FALSE`
  sets `u8_hd_sink = TRUE` (`research\dl-encoder\...\hdmi_tx\ms7210_drv_hd_tx.c` lines 1777 to
  1783 and 1804 to 1808) [verified]. The host test accepts the current behaviour
  (`tests/host/dosboard/ms7210_test.c` line 433) [verified].
* Why it matters: the EDID is read at 20 kHz through the MS7210 pass-through and the TPD12S521;
  if a TV's second block read is marginal on the bench, the TV gets DVI and the sound is silently
  lost. [inferred]
* Fix: in `choose_mode()`, when `edid[0x7E] >= 1` and `!edid_info.ext_ok`, take HDMI (as for no
  EDID) and log "EDID extension unreadable, HDMI assumed"; optionally retry the block 1 read once.
  Make the test at line 433 expect HDMI from `ms7210_init()`.

#### F2 (minor). A large `lcd_bounce_px` leaves the board with no picture

* Where: `esp/main/dosboard.c` lines 351 to 357 (the only check), `esp/main/lcd_dosboard.c`
  lines 89 to 96 and 112 to 117.
* What: `dosboard_load_config()` accepts any value that divides 480000. ESP-IDF allocates two
  bounce buffers of `bounce_px x 2` bytes each from internal RAM and fails with `ESP_ERR_NO_MEM`
  when it cannot (`components/esp_lcd/rgb/esp_lcd_panel_rgb.c` lines 227 to 233, 371 to 374 in
  ESP-IDF v6.1-dev-8142) [verified]. For example 96000 needs 384,000 bytes against the 261,596 the
  integration measured free [verified: integration report; arithmetic mine]. `lcd_init()` then
  returns NULL: no frame buffer, no MS7210 set-up, no picture, and only the UART says why. The
  card has to be edited on a PC to recover. [verified: source]
* Fix: clamp `bounce_px` in `dosboard_load_config()` (for example to at most 800 x 20 = 16000,
  with a note), and in `lcd_init()` retry `panel_start()` once with `bounce_px = 0` if the first
  attempt fails with bounce buffers.

#### F3 (minor). The PCLK meter is read straight after it is switched on

* Where: `esp/main/ms7210.c` rows B19 (line 52, meter on) to C1 (line 58, read), and
  `ms7210_init()` lines 453 to 458; no wait between them.
* What: the vendor code switches the meter on at init (`research\dl-encoder\...\ms7210\ms7210.c`
  line 83) and reads it later, in its polling path (line 112) [verified]. How long the meter takes
  to produce its first count is not documented [unknown]. If it needs a measuring window, the
  first-light line reads "PCLK meter 0, about 0.00 MHz (40.00 expected)" on a good board, and
  `encoder.md` 8.7 step 2 would send Plat looking for a fault that is not there. [inferred]
* Fix: add a `WAIT_MS` row of about 20 ms before C1 (it is not in 8.4, so note it as an
  addition), or read the meter again in `report()` and log both values.

#### F4 (minor). Some first-light unknowns can only be tried with a rebuild

* Where: `esp/main/dosboard.c` `parse_cfg()` lines 288 to 318; `esp/main/i2s.c` lines 34 and
  157 to 159; `ms7210.c` row B7 (line 40).
* What: the PCLK edge, HDMI mode and sound have ini keys, which is good. These do not:
  * R/B order: `encoder.md` 4.6 says a swap can be fixed with 0x1201 bit 4 "without a board
    change"; row B7 hard-codes 0 [verified]. The netlist mapping is right (section 3.4), so a
    swap is unlikely [inferred].
  * I2S frame: `encoder.md` 4.7 and 11.3 leave the MS7210's expected slot width unknown and say
    "if no sound, try 32-bit slots"; `i2s.c` line 159 fixes 16-bit Philips slots [verified].
  * Level: `i2s.c` line 34 divides every sample by 16 (tiny386's own line, unchanged since
    pristine), so HDMI sound peaks 24 dB below full scale. That suits a small speaker amplifier
    better than a TV's line input. [verified: source and `git show pristine:esp/main/i2s.c`;
    level inferred]
* Fix: add `[dosboard] rb_swap = 0|1` (row B7's value), `i2s_slot_bits = 16|32` and an audio
  shift for the DOS board (for example `#define AUDIO_SHIFT 2` in `board_dosboard.h`, 4 elsewhere).

#### F5 (note). ESP-IDF describes `pclk_active_neg` two different ways

* Where: `board_dosboard.h` line 52, `lcd_dosboard.c` line 68, `encoder.md` section 9 plan A.
* What: the public header says the flag means "the display data is clocked out on the falling edge
  of PCLK" (`esp_lcd/rgb/include/esp_lcd_panel_rgb.h` line 72); the esp32s31 low-level driver it
  sets says "True: sample on negedge, False: sample on posedge" (`esp_hal_lcd/esp32s31/include/
  hal/lcd_ll.h` lines 298 to 301) [verified]. Plan A (pclk_neg 1, 0x1281 = 0) is built on the
  header's reading. If the low-level comment is the true one, plan A is the same-edge pairing that
  `encoder.md` 9 calls marginal. Both reference tables (Loongson, Meyesemi) write 0x1281 = 0x04
  [verified: `encoder.md` 4.6]. Which is right only the board can say [unknown].
* Fix: none needed in code, since both knobs are in `dosboard.ini`. In TEST-PLAN and the
  `dosboard.ini` comment, list the four pairs to try in order (pclk_neg 1 with phase 0, then 4;
  pclk_neg 0 with phase 0, then 4) and mention that the two reference tables use 4.

#### F6 (note). The MS7210 audio path is set up before any I2S clock runs

* Where: `esp/main/i2s.c` lines 22 to 28 (the task waits for event bit 0 before
  `i2s_channel_enable()`), `esp/main/esp_main.c` line 402 (bit 0 set once the PC is built);
  `lcd_dosboard.c` line 129 (`dosboard_video_start()`, rows E44 to E69).
* What: rows E66 to E69 reset and release the MS7210's audio FIFO while BCLK and WS are still
  idle; I2S starts seconds later. Whether the MS7210 picks the stream up by itself is not
  documented [unknown]. A hot plug reruns E66 to E69 with I2S running [verified: source].
* Fix: if there is no sound at first light, rerun E66 to E69 once the I2S channel is enabled (or
  start I2S with silence before `dosboard_video_start()`). Worth a line in TEST-PLAN: "no sound,
  unplug and replug the HDMI cable" tells the two cases apart.

#### F7 (note). The I2S clock comes from the crystal through a fractional divider

* Where: `esp/main/i2s.c` line 158 (`I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_RATE)`).
* What: on esp32s31 `I2S_CLK_SRC_DEFAULT` is the 40 MHz crystal
  (`soc/esp32s31/include/soc/clk_tree_defs.h` line 566); 256 x 48 kHz = 12.288 MHz is not a whole
  divisor of 40 MHz, so BCLK carries divider jitter. The APLL is supported
  (`soc_caps.h` line 671) [verified]. An HDMI transmitter rebuilds the audio clock from N and CTS,
  which is more sensitive to jitter than the PCM5102A's own PLL. [inferred]
* Fix: if HDMI sound clicks or drops out, set `.clk_cfg.clk_src = I2S_CLK_SRC_APLL` on the DOS
  board.

#### F8 (note). Hot plug: 500 ms polling, and a reset MS7210 is never noticed

* Where: `esp/main/dosboard.c` lines 362 to 372; `ms7210.c` `ms7210_poll()` lines 466 to 485.
* What: an HPD low pulse shorter than 500 ms (a sink signalling an EDID change, some TVs changing
  input) is not seen [inferred]. More usefully, if the MS7210 itself is reset (ESD when a cable
  goes in, a dip on its 1.2 V), sections B and D are lost and are never written again: the poll
  only reruns section E, and only on an HPD rise [verified: source]. The status LED blinks
  whatever `ms7210_poll()` returns, though the comment says "while all is well" (line 369 to 370)
  [verified].
* Fix: in the poll, read back one register only section D writes (0x120C should read 0x20) and
  rerun B to E when it differs; blink only when the poll returned 0 or 1. INT on GPIO6 is wired
  (R604, `dosboard.net`) if polling ever proves too slow.

#### F9 (note). CH9350L state 0 fallback trusts the report layout

* Where: `esp/main/ch9350.c` `data_frame()` lines 111 to 146, against `dispatch()` lines 154 to
  163.
* What: state 2 keyboard frames go through `boot_report_ok()`, state 0 ones do not, and both are
  read as an 8-byte boot report. In state 0 the chip forwards the device's raw report (DS26 p.12,
  "下位机模式状态 0 为键盘、鼠标原始的数据") [verified], so an NKRO keyboard (bitmap report) or a
  mouse with 16-bit axes makes wrong keys or wrong moves instead of being dropped [inferred].
  State 0 is only the fallback (`ch9350_state = 0`).
* Fix: run `boot_report_ok()` in `data_frame()` too, and count, rather than use, reports longer than
  a boot report that have no report ID.

#### F10 (note). LED answers to 0x80 frames

* Where: `esp/main/ch9350.c` `sink_status()` lines 409 to 415, keep-alive lines 479 to 488, log
  lines 490 to 503.
* What: every 0x80 gets an LED frame back, as DS26 7.2.4.7 says it has an answer [verified]. KVMS
  reports that the chip echoes `80 3N` after an LED frame [reported: `ch9350.md` 6.5]. If a chip
  answered every 0x12 with a 0x80, the two sides would trade frames at wire speed. DS26 calls 0x80
  a "status change" frame, so it should stop once nothing changes [inferred]. The 10 s log prints
  only when an error count changes, so such a loop would not show.
* Fix: answer a 0x80 only when its low nibble differs from the LEDs last sent, or at most once per
  100 ms; add the 0x80 count to the log line.

#### F11 (note). Bring-up on a hand-assembled board

* Where: `esp/main/dosboard.c` `board_early_init()` lines 248 to 284.
* What: the board check rests on the MS7210 answering, and the MS7210 is the hardest part on the
  board to hand-solder (QFN-64 with exposed pad, `encoder.md` 4.1). A board with one bad MS7210
  joint halts before the card, the CH9350L or the boot console can be tried. [verified: source;
  consequence inferred]
* Fix (optional): let USER (GPIO42) held through reset skip the check and run with the UART
  console only, saying so loudly. On a Korvo-1 that key is the dual-boot key, so reaching this
  path there takes a deliberate act and a wrong image. [inferred]

#### F12 (note). Small things

* `parse_cfg()` compares `hdmi_mode` case-sensitively and turns any unknown word (`HDMI`, `Dvi`)
  into `auto` without a warning; `pclk_neg = yes` reads as 0 (`dosboard.c` lines 296 to 300)
  [verified]. Use `strcasecmp()` and warn on a value it does not know.
* `ch9350_ps2_leds_to_usb()` (`ch9350.c` lines 327 to 332) is not used by the firmware; the
  conversion is done in `kbd_input.c` lines 478 to 490. Only the host test calls it [verified:
  grep]. Remove it or mark it test-only.
* `FLASH.txt` "If something goes wrong" names the CP2102N, which is the Korvo-1's bridge; on the
  DOS board it is the CH340C (U401). [verified: `FLASH.txt`, `dosboard.net`]


### 3. What I checked and found right

#### 3.1 MS7210 against `encoder.md` section 8

* All 114 rows, independently of the existing checker: my `cmp_table.py` parses the A to E tables
  in 8.4 and the `R(...)` rows in `ms7210.c`: same 114 rows, same order, every register, mask and
  value equal, 0 hard mismatches. The six rows it could not compare mechanically are right by hand:
  A1 (low 20 ms, high, 50 ms; 8.4 asks at least 10 and 50), B22 (value from `pclk_phase`), C1
  (reads 0x001E then 0x001F), E38 and E40 (10 ms), E68 (100 us), E46 (value from the DVI flag).
  [verified]
* InfoFrames: AVI `45 12 18` then 11 zeros, audio `70 01` then 9 zeros, as E58, E64 and 8.5; the
  checksums are computed from the fields. [verified: my test output]
* Spot checks of 8.4 against the vendor SDK, for the rows that decide sound and the run-time
  duties: 48 kHz is 0x0531 bit 2 and 44.1 kHz uses the same setting (`hd_tx.c` 443 to 447);
  16-bit samples are 0x0535 bits 5:4 = 01 when `HD_AUD_LENGTH_16BITS` = 0 (`hd_tx.c` 465,
  `ms7210_typedef.h` 352); two channels 0x0532 = 0x01 (`hd_tx.c` 479 to 496); DDC 0x0013 = 0xFF
  then 0x0012 = ~1 & 0x0F = 0x0E, off with 0x0F (`hd_tx.c` 952 to 968); HPD 0x0502 bit 0 (870);
  input stable 0x051D bit 4 (892); PLL lock 0x091C bit 7 (267); the retry path reruns
  `ms7210drv_hd_tx_phy_config()`, which is E7 to E40 (`ms7210.c` 121, `hd_tx.c` 787 to 802); DVI
  is 0x0503 bit 0 (306). [verified]
* Order of section E against the SDK's `ms7210_hdtx_output_config()` (`ms7210.c` 165 to 181) and
  the functions it calls: same order. [verified]
* Wire format: write `addr, index low, index high, value`, read with the two index bytes, a
  repeated start and one byte; 16-bit values low byte first (`ms7210.c` lines 194 to 214,
  338 to 341), as 8.2. [verified]
* EDID parser: header, block checksum, vendor letters, established timing byte 0x23 bit 0,
  standard timing `45 40`, detailed timings at 0x36, 0x48, 0x5A, 0x6C, the 0xFC name, the CEA
  data block walk with the vendor block OUI `03 0C 00`, basic audio in byte 3 bit 6. All as the
  EDID 1.3 and CEA-861 layouts give them. [inferred: from my own knowledge of the layouts, as the
  author's notes also say]
* No copying from `research\dl-encoder`: the driver is a table interpreter keyed by 8.4's row
  names, unlike the SDK's function-per-block style. My `copycheck.py` found no 5-word phrase shared
  between the driver's comments and the 19 vendor files; the only 4-word ones are the EDID terms
  "a detailed timing descriptor" and "active format information present". Shared identifiers are
  only C keywords, standard types and the names `MS7210_I2C_ADDR`, `ms7210_init`, `ms7210_probe`;
  no `HAL_`, `MSRT_`, `u8_` style tokens. The same check against Zephyr's CH9350L driver found
  nothing shared. [verified]

#### 3.2 800x600, PCLK and the panel driver

* ESP-IDF's esp32s31 `lcd_ll_set_horizontal_timing()` and `..._vertical_timing()` (`lcd_ll.h`
  line 817 onwards) give totals of 128 + 88 + 800 + 40 = 1056 and 4 + 23 + 600 + 1 = 628, so the
  back porch values are the VESA ones, excluding the sync. 40 MHz is PLL_F160M / 4. 60.32 Hz.
  [verified: source; arithmetic mine]
* `hsync_idle_low` and `vsync_idle_low` set: positive syncs, as VESA 800x600 and rows D13, D14
  expect. DE is high while active (`de_idle_high` left 0), as row B14 uses the DE pin. [verified]
* Test card colours and the grey ramp are correct RGB565. The frame buffer comes from ESP-IDF
  zeroed, so `test_card_ms = 0` sends black. [verified: source]
* ESP-IDF's only bounce buffer rule in this version is that the frame divides by it
  (`esp_lcd_panel_rgb.c` line 374); `dosboard_load_config()` checks exactly that (but see F2).
  [verified]

#### 3.3 I2C, HPD and EDID on the ESP side

* Two device handles at 0x59 are allowed (no address check in `i2c_master_bus_add_device()`,
  `i2c_master.c` lines 1181 to 1199) and the speed is set per transaction (line 701). DDC reads use
  the 20 kHz handle, register writes after the pass-through is on use the slow handle, and the bus
  goes back to 100 kHz after 0x0012 = 0x0F, as 8.6 and the SDK order it. [verified]
* EDID timeout 500 ms against about 58 ms for 128 bytes at 20 kHz. [verified: source; arithmetic
  mine]
* The hot plug task starts only after `ms7210_init()` returns, so the MS7210 has one user at a
  time. Its console lines go to the UART and the ring once the emulator has the display
  (`console.h` lines 11 to 14). [verified]

#### 3.4 Pins: `board_dosboard.h` against `dosboard/out/dosboard.net`

Every pin matches the netlist (generated 2026-10-21 18:32) and DESIGN-BRIEF section 4. [verified:
my `netpins.py` over the netlist]

| Header | GPIO | Net in `dosboard.net` | Far end |
|-|-|-|-|
| LCD_PCLK_GPIO | 40 | LCD_PCLK_PAD, R209 22R, LCD_PCLK | U601 (MS7210) pin 51 |
| LCD_DE, HSYNC, VSYNC | 43, 44, 45 | LCD_DE, LCD_HSYNC, LCD_VSYNC | U601 pins 62, 63, 64 |
| LCD_DATA_GPIOS | 8 to 19, 33 to 36 | LCD_D0 to LCD_D15 | U601 B3..B7 from D0..D4, G2..G7 from D5..D10, R3..R7 from D11..D15; copies B2..B0 from D4..D2, G1, G0 from D10, D9, R2..R0 from D15..D13, as `encoder.md` 4.6 |
| SD_CLK, CMD, D0 to D3 | 24, 25, 20 to 23 | SD_CLK, SD_CMD, SD_D0 to SD_D3 | through R502 to R507; slot 0 IO MUX pins in ESP-IDF `esp_hal_sd/esp32s31/include/soc/sdmmc_pins.h`, and slot 0 is `SDMMC_HOST_DEFAULT()`'s slot |
| SD_PWR_EN_GPIO | 39 | SD_PWR_EN_N | R501 10k to GND, R514 to Q501 (AO3401A) gate: low = on |
| SD_CD_GPIO | 50 | SD_CD_N | R513 10k to +3V3, J501 pin 9 |
| I2S_BCLK, WS, DOUT | 3, 4, 5 | I2S_BCLK, I2S_WS, I2S_DOUT | U601 pins 7, 6, 5; U701 (PCM5102A, DNP) pins 13, 15, 14 |
| HDMI_I2C_SDA, SCL | 0, 1 | I2C_SDA, I2C_SCL | U601 pins 11, 12; R207, R208 4.7k; J202 |
| HDMI_RSTN_GPIO | 2 | HDMI_RSTN | U601 pin 33, R603 27k to +3V3 |
| HDMI_INT_GPIO | 6 | HDMI_INT | U601 pin 10, R604 27k to +3V3 |
| HDMI_I2C_ADDR 0x59 | | /Video/MS_SA | U601 pin 32 with R602 10k to +3V3 (SA high) |
| KM_RX_GPIO | 47 | KM_RX | U301 (CH9350L) pin 28 TXD |
| KM_TX_GPIO | 55 | KM_TX | U301 pin 27 RXD, R304 10k to +3V3 |
| KM_RST_GPIO | 7 | KM_RST | U301 pin 3, R303 10k to GND |
| KM_S0_GPIO | 46 | KM_S0 | U301 pin 4, R302 1k to GND (state 2) |
| (no GPIO) | | /USB_host/CH_SEL | U301 pin 45, R301 10k to +3V3 (lower mode); S1, BAUD0, BAUD1 not connected (state 2, 115200) |
| USB_VBUS_EN_GPIO | 38 | USB_VBUS_EN | U302, U303 EN, R309 10k to +3V3 |
| LED_STATUS_GPIO | 37 | /Module/LED_STATUS | D201 cathode, anode through R204 1k to +3V3: low = lit |
| USER_KEY_GPIO | 42 | /Module/USER_BTN | R203 10k to +3V3, SW203 |

The CH9350L straps agree with DS26: S1 S0 = 1 0 is state 2 (7.1.6, p.6), RST high resets, 100 ns
typical (7.1.2, p.5), BAUD pins high give 115200 (7.1.7, p.6). [verified: DS26 pages 5 and 6,
pdftotext]

#### 3.5 AUDIO_RATE

* The build forces `board_dosboard.h` into every file of the main component
  (`esp/main/CMakeLists.txt` line 66), the emulator's own files included. Preprocessing `adlib.c`,
  `pcspk.c`, `sb16.c` and `i2s.c` with the exact commands from the rel5 DOS board build's
  `compile_commands.json` gives `s->freq = 48000`, `48000 * s->pit_count`, 48000 in all six
  resampler calls and `.sample_rate_hz = 48000`. [verified]
* Rows E50 to E52 set the MS7210 to 48 kHz, 16-bit, two channels, and the audio InfoFrame says two
  channels, matching what `i2s.c` sends (Philips, 16-bit, stereo). [verified]
* The integration's 8.8 per cent figure for 44.1 kHz SB16 sound: `approx_frac(48000, 44100)` stops
  at 1/1 because the next convergent's denominator, 11, is over the limit of 8, so the input is
  taken one frame per output frame. [verified: `sb16.c` lines 1305 to 1333; arithmetic mine]

#### 3.6 CH9350L against DS26 and `ch9350.md`

* Opcodes and parameter lengths (DS26 7.2.4, p.10), the state 0 and 1 data frame (length byte
  counts label, data, sequence and sum; sum over data and sequence; label bits 5:4 type, 2:1
  protocol, 0 port; p.12), the 0x81 layout with its sum over payload and the two id bytes (p.10),
  the state 2 keyboard and mouse frames (p.13), and the 0x12 answer frame with LED bits 0 Num, 1
  Caps, 2 Scroll and status byte high nibble 1010 (pp.13 to 14): all as the code has them.
  [verified: DS26 pages 10 to 14, pdftotext]
* The parser resyncs only after an unknown opcode, a bad length or sum, or a 100 ms gap, so a
  mouse frame holding `57 AB` is not split. Body bounds hold: the longest state 0 body is 79 bytes
  in an 80-byte buffer. [verified: source]
* PS/2 to USB LED order in `kbd_input.c` lines 478 to 490; the wheel sign; ErrorRollOver kept out
  in `kbd_input_report()`. [verified: source]

#### 3.7 Board check and start-up

* On a Korvo-1 the check touches only GPIO0 to GPIO2: GPIO0 and GPIO1 are its own I2C bus (codec
  0x10, touch 0x14, nothing at 0x59) and GPIO2 is its I2S MCLK, an S31 output, so the RSTN pulse
  drives nothing against another chip. [verified: DESIGN-BRIEF section 4 rows for GPIO0 to 2;
  Korvo-1 devices reported via `research/firmware-design.md` lines 920 to 923]
* `board_early_init()` drives only RSTN before the check; LED, USB_VBUS_EN, USER and card detect
  are configured after it. `dosboard.ini` keys in `card-dosboard\tiny386\dosboard.ini` are exactly
  the ones `parse_cfg()` knows. `enable_usb` defaults to 1, so the CH9350L starts without any ini
  line. [verified: source]

#### 3.8 Host tests

`tests/host/run.sh` run from the v3 tree with its output in my folder: 14 passed, 0 failed,
including the MS7210 (sequence, wire, PLL retry, hot plug, EDID, DVI, InfoFrames), the bus log
against `encoder.md` 8.4, the CH9350L through `kbd_input.c` and `i8042.c`, and AUDIO_RATE at both
rates. `git status` of the tree was the same before and after. [verified:
`~/tiny386-idf-scratch/review-dosboard-drivers/hostout.log`]


### 4. Files

In WSL `~/tiny386-idf-scratch/review-dosboard-drivers`:

* `cmp_table.py`: `ms7210.c` rows against `encoder.md` 8.4.
* `edidpol.c` and `t/`: the EDID mode test behind F1.
* `copycheck.py`: wording and identifier overlap with the vendor sources.
* `netpins.py`, `u201.txt`: module and part nets from `dosboard.net`.
* `pp/`, `pp.py`: the four audio files preprocessed with the DOS board build's commands.
* `ds26-p5-6.txt`, `ds26-p10-14.txt`: DS26 pages as text.
* `hostout/`, `hostout.log`: the host test run.
