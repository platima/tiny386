# Review of v4.6: display and boot

2026-10-25. An adversarial review of branch `v46` (tip 2d076f6) before release, through one lens:
the dbuf series and the new power-on double buffering. The RTC note, the two partition tables,
the memory figures, `double_buffer = 0` against v4.5, Ctrl+Alt+F9, the speed screen, the crash
screen, the two cores, and what Plat sees on the panel.

Where: my own clone, WSL `~/org2/v46/rev-display-boot/repo` (branch `v46`, 2d076f6). Probes in
`~/org2/v46/rev-display-boot/probe`. Host test output in `~/org2/v46/rev-display-boot/out`. The
staged package `~/org2/v46/pkg` was only read. Nothing was flashed, pushed or tagged, and no serial
port was opened.

Verdict: **needs changes**. One major finding (1), three minor (2 to 4), four notes. The loop
guard itself is sound.

## What holds

* **No loop of tiny386's own restarts** [verified: code, host test 3g, ESP-IDF source]. A restart
  needs a start that found no note at all and whose reset was not a software restart. On the S31,
  `esp_restart()` ends in `esp_rom_software_reset_system()`, which reads back as
  `RESET_REASON_CORE_SW`, and ESP-IDF's `esp32s31/reset_reason.c` maps that to `ESP_RST_SW`. So the
  start after the restart never restarts, whether the LP SRAM kept the note or not. A reset inside
  `lcd_init_two()` leaves TRYING. A note that is not whole is ignored with no restart. The panic
  handler halts (`CONFIG_ESP_SYSTEM_PANIC_PRINT_HALT=y`), so a crash does not reset by itself.
* **Power cut, RESET, noise** [verified: code; inferred for the board]. A power cut loses the LP
  SRAM: the magic, its inverse, the form and an FNV-1a check must all match, so noise reads as no
  note. On the S31 the RESET button reads as `ESP_RST_POWERON` (there is no `ESP_RST_EXT` in the S31
  map), so a RESET always allows the one restart, and the note is zero after every start anyway.
* **The two tables** [verified: code, both CSVs, sdkconfig, nm]. Under the tiny386-default table
  tiny386 at 0xE20000 is `factory`, so `restart_comes_back()` is true. Under the Linux-default
  table it is `test`, so there is no restart and a yellow line says why. The lookup
  (`esp_partition_get_running_partition()`, via `spi_flash_cache2phys()`) has no PSRAM XIP in the
  way (`CONFIG_SPIRAM_XIP_FROM_PSRAM` is not set, no `FLASH_LOAD_TO_PSRAM`), and the function sits in
  flash code at 0x4000105E, so the lookup should not hit its `abort()`. It is only called when a
  restart is about to happen. On the board it is still [unknown].
* **`double_buffer = 0` is v4.5's display** [verified: code, host test 3g]. With no note,
  `dbuf_boot_panel()` calls `lcd_init()`, which is v4.5's start (the base config has `num_fbs = 1`,
  as before). `vga_task` no longer calls `vga_set_fb()` without both buffers, but with one buffer
  v4.5 handed over the same pointer it was built with, and `vga_set_fb()` only stores it. No new
  line on the console. With a panel `own_screen_bytes()` is 0, so the memory sums are v4.5's. The
  host test's pictures with a panel equal v4.5's, line for line. The only differences Plat can see
  are from the core series (`of 131040`) and the new wording of Ctrl+Alt+F9's line.
* **The memory figures** [verified: code; the board figures inferred]. Both frame buffers are taken
  before the pool is sized, so `psram_len` already pays for the second one, and `settle_memory()`
  and `check_config()` count it once. TEST-PLAN 17c's sums are right: with a pool of 14,352 KiB,
  `win95-sd.ini` (max, 768K) gets 13M, as with one buffer, and `win95-14m.ini` (14M, 320K) is
  lowered to 13M. The restart check (`pool - FB_TAKE < 2 MiB`) is made with one buffer in place.
* **The LP SRAM layout** [verified: nm of v4.5's and v4.6's ELF]. `dbuf_note` is 180 bytes at
  0x2E002010, ending exactly where `s_rec` now starts (0x2E0020C4); the clear does not touch it.
  v4.5 had `s_rec` at 0x2E002010, so the first boot after flashing v4.6 reads v4.5's status record
  as no note (its magic is "SDEL") and cannot replay v4.5's last LED code. Once only; harmless.
* **The two cores** [verified: code]. Everything new runs either in `app_main()` before the
  emulator's tasks exist, or in `vga_task` on core 0, which waits for BIT0 (the PC built) before its
  first picture. Ctrl+Alt+F9 is handled in `vga_task` under the display lock, as in v4.5. Core 1
  never touches the new state.
* **The crash screen with no frame buffer** [verified: code, host test 2]. `ct_put()` returns at
  once with no base, and never draws at a row below 0. The ring says "no panel".
* **Host tests**: 61 of 61 pass in my clone [verified: `~/org2/v46/rev-display-boot/hosttests.log`].
* **The restart is short and visible** [inferred]. The grey line stays 1 s. GPIO38 (the panel's
  DISP pin) is released by the reset, so the panel most likely goes dark until the next start
  brings it up, which is early in `app_main()`, before the card is mounted. There is no second boot
  menu. The extra time should be a couple of seconds; only the board can say.

## Findings

### 1. (major) After a crash, a watchdog, a lock-up or a brownout, the restart hides the last run's report

With `double_buffer = 1` and tiny386 the default, the first start after a bad reset shows the
report: `Reset by a CPU lock-up` (or a brownout, or a watchdog) in yellow, then "The last run ended
with ... Its last N lines", and the status LED replays the last run's code. That start then reaches
`dbuf_boot_start()`, finds no note and a reset that was not a software restart, and restarts after
the boot menu (6 s) or, with no menu, about a second after the settings. The PC is not built, so
the 10 s hold in `i386_task` that a warning brings never comes.

The start after the restart shows none of it [verified: code and a probe]:

* `banner()` shows the last run's lines only if the reset was bad or the ring says it crashed
  (`esp_main.c` 1233). This start says `Reset by a software restart.`, which is not bad.
* `console_init()` resets the ring (`console.c` 1040), and the old lines and their heading are
  written to the panel and the UART only, not into the ring again (`console.c` 902). So the ring
  this start finds holds only the first start's own lines, with no crash flag.
* The status LED's record is rewritten by the first start, and a software restart is no cause, so
  there is no replay either.
* Ctrl+Alt+F12 cannot bring the lines back: they were never in this start's log.

Probe `~/org2/v46/rev-display-boot/probe/probe_ring.c`, with the real `console.c`: a ring left by
a crash, then `console_init()` twice with the first start's lines between them. Start 1 sees the
crash (crashed 1, 4 lines). Start 2 sees crashed 0 and only the first start's 3 lines, so
`banner()` would show nothing.

This is the class of event the release asks Plat to report. BUILD-NOTES 32.2 says the boot after
v4.5's crash should read "a CPU lock-up", and Plat does not watch the UART. A brownout loop, or a
lock-up that comes back at each boot, would also look like "more than one restart in a row" with
"software restart" each time, which TEST-PLAN 17d puts down to the note.

Suggested change: in `dbuf_boot_start()`, before the restart, keep one frame buffer when this start
follows a bad reset or a crash: `s_reset` is a panic, a watchdog, a lock-up, a brownout or a power
glitch, or `console_previous_crashed()`. A yellow line says so ("one frame buffer at this start, so
that the last run's report stays on the panel; RESET makes both"), which also brings the 10 s hold.
Add a case to `test_double_start.c` and a line to TEST-PLAN 17d.

### 2. (minor) Two refused and then one refused at the restart: a dark panel and Windows run blind

If the start with two frame buffers fails, `lcd_init_two()` deletes that panel and starts it again
with one (`lcd_korvo1.c` 266). That is the path v4.5 took on Plat's board, deleting the panel and
starting it again, which then failed with one too [inferred in cdf934a, by elimination]. If it
fails here, there is no panel: every line, the yellow one with both error names and the memory
figures included, goes to the UART only, and the PC starts Windows from p3 with a green LED and a
dark panel [verified: code; host test case "two and then one refused"]. 5b5c1fb handled this case
with a restart into one buffer and a visible reason; 635dc04 dropped that.

TEST-PLAN 17d's advice for it is to give Windows a minute, press Ctrl+Alt+F9 and then RESET, which
resets a running Windows (ScanDisk at the next boot at best).

Suggested change: when `dbuf_boot_panel()` ends with no panel after a TWO note, write a note of a
third stage holding both error codes and restart. The start after it (a software restart with that
note) calls `lcd_init()` only, makes no restart, and shows both errors in yellow. That is two
restarts at most, still bounded. Or stop before the PC with `status_led_fatal()`, so the LED says
it. In 17d, prefer setting `double_buffer = 0` on the PC to RESET under a running Windows.

### 3. (minor) The crash screen with both frame buffers puts the report into the ring twice

`crash.c` (lines 120 to 123) draws the crash screen into the second frame buffer first, then into
the one on show, with two calls of `console_draw_crash()`. Each call writes the report into the
ring and reads the ring's tail for "The last lines of the log". So the screen on show lists the
first copy of the report (4 lines or more) among the log's last lines, pushing out older lines, and
the next boot's 25 last-run lines carry the report twice [verified: probe
`~/org2/v46/rev-display-boot/probe/probe_crash2.c` with the real `console.c`: the ring ends with
the four report lines twice]. This is v4.4's code, but no crash with both buffers made ever reached
Plat; in v4.6 it can, also when both are made and swapping is off.

Suggested change: draw the second buffer without the ring writes (a flag to `console_draw_crash()`),
or draw once and copy the front buffer into the second.

### 4. (minor) TEST-PLAN 17d has no case for a crash inside the start with two frame buffers

A panic inside `lcd_init_two()` comes before `console_init()`, so the crash screen draws nothing
(`console_fb()` is NULL), the panel stays dark, the LED shows the crash, and the chip halts
[verified: code]. Ctrl+Alt+F9 does nothing then. Whether RESET gives the one-buffer start depends
on the LP SRAM keeping the TRYING note through RESET, which is [unknown]; if it does not, each RESET
brings the menu, the restart and the same halt. 17d's "dark panel" bullet assumes the PC still
starts. Suggested change: one bullet, "a dark panel and the LED's crash code after the restart:
switch off, set `double_buffer = 0` on the PC, and send a photo of the LED's code".

### 5. (note) `lcd_double_set(true)` is called without the display lock

`lcd.h` (line 59) says every function but `lcd_init_two()` is called with the display lock held.
`dbuf_boot_start()` calls `lcd_double_set(true)` from `app_main()` without it (`dbuf_boot.c` 228).
`vga_task` does not exist yet, so the only overlap is a console line from another task while the
front buffer is copied into the back one; the back buffer would then hold half a line until it is
drawn over [inferred]. Take the lock around the call, or change the comment.

### 6. (note) `board_korvo1.h`'s figure is not true with double buffering

Line 42 says about 15,300 KiB is left for the guest, "mem_size 14M with vga_mem_size 256K or 320K",
right after saying both buffers are taken with `double_buffer = 1`. With both the pool is about
14,350 KiB and 14M does not fit; with one, Plat's photo gave 15,104 KiB. TEST-PLAN 17c and
BUILD-NOTES 32.3 have it right. Comment only.

### 7. (note) Ctrl+Alt+F9's next-start line under the Linux-default table

With one buffer, a press that sets 1 says the second buffer "is made with the panel at the next
start" (`speed.c` 433). Under the Linux-default table it will not be; the next boot's yellow line
says why. `restart_comes_back()` could choose the wording.

### 8. (note) A stale note through Linux, and Linux's reset reason

If VOL+ is held in the second between the note and the restart, the bootloader starts Linux; if
Linux leaves the LP SRAM alone, its `reboot` brings tiny386 back with the note, which takes that
ini without the menu and says it is tiny386's own restart [inferred: code]. Harmless. What reset
reason Linux's `reboot` gives tiny386 is [unknown]; the yellow line and TEST-PLAN 17d say it is a
software restart. If it is not, tiny386 would simply make its one restart, which is also safe.

## What Plat sees, case by case

| Case | On the panel |
|-|-|
| `double_buffer = 0`, either table | As v4.5. No new line. |
| 1, tiny386 the default, power-on or RESET | The menu (or the ini alone), the grey restart line for 1 s, dark for a moment, then a start that says `Reset by a software restart.`, the grey "own restart" line, the green `Starting ... (no menu)`, the grey "two frame buffers" line, the usual hold, the PC. |
| 1, two refused at the restart | The grey line that it started with one, the yellow line with the error and the memory figures, a 10 s hold, the PC with one buffer. |
| 1, two and then one refused | Dark from the restart on, a green LED, Windows running blind (finding 2). |
| 1, Linux the default (VOL+) | The yellow line at every boot and a 10 s hold. One buffer. |
| 1, Linux's `reboot` into tiny386 | The yellow "software restart with no note" line, a 10 s hold, one buffer. |
| 1, after a crash, watchdog, lock-up or brownout | The report for the menu's 6 s or about a second, then the restart; the next start shows none of it (finding 1). |
| 1, a reset inside the start with two | The grey "cut short" line and a yellow line; one buffer, no restart. |
| 1, a panic inside the start with two | Dark, the LED's crash code, halted (finding 4). |
| A note not whole | A grey line and, with 1, a yellow one; no restart. |

## Files

* This review: `C:\Users\Owner\AppData\Local\Temp\claude\D--Repos-esp32-s31-linux\99e6c67c-c5d4-41b3-a911-64bbdd3332f8\scratchpad\v46\REVIEW-v46-display-boot.md`
* Probes: WSL `~/org2/v46/rev-display-boot/probe/probe_ring.c` and `probe_crash2.c` (built
  against the clone's `esp/main/console.c` with `-DCONSOLE_HOST`).
* Host tests: WSL `~/org2/v46/rev-display-boot/hosttests.log`, output in
  `~/org2/v46/rev-display-boot/out`.
