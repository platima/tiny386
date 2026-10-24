# Review: concurrency and ESP-IDF use in tiny386 bare-metal v3, round 2 (v3.1-tests)

Lens: two cores and interrupts, shared state, locks and atomics, frame buffer cache write-back
against the LCD DMA, ESP-IDF calls for esp32s31 in this IDF, task stacks and priorities,
watchdogs. Reviewed 2026-10-21, after the round 1 fixes.

* Tree: WSL `~/tiny386-idf-scratch/v3/tiny386`, tag `v3.1-tests` = 823d5cd, exported with
  `git archive` to `~/tiny386-idf-scratch/review-concurrency-esp-idf/round2/src` and read there.
  Between `v3.1` (80e64de) and `v3.1-tests` no file outside `tests/` changed, so everything
  below is about the firmware the images in `images\` were built from [verified: the names-only
  `git diff` from v3.1 to v3.1-tests, leaving out `tests/`, lists 0 files; the images are
  byte-identical to a build of this tree, reported by the integration step].
* ESP-IDF v6.1-dev-8142-g188e3e55bb (`~/esp/esp-idf`, commit 188e3e55bb), espressif/usb 1.6.0
  and espressif/usb_host_hid 1.2.0, read in the integration's from-nothing build
  `~/tiny386-idf-scratch/v3-work-integ/resume/rel1` [verified: `idf_component.yml` of each,
  `dependencies.lock`].
* Nothing ran on a board. No serial port was opened. Nothing in the tree or in `v3` was changed
  apart from this file. Work files: `~/tiny386-idf-scratch/review-concurrency-esp-idf/round2`
  (WSL).
* This file: round 2 first, then the round 1 report word for word as an appendix, because
  `REVIEW-RESOLUTION.md` refers to its F1 to F13. Round 2's findings are numbered R2-F1 and on.
  A byte-exact copy of the round 1 file is kept as `round2/round1-REVIEW-concurrency-esp-idf.md`
  in the work folder (SHA-256 405d74dd...e69a, the value `SHA256SUMS` lists). That line of
  `SHA256SUMS` no longer matches this file; I did not change `SHA256SUMS`.

Tags: [verified] read in the named source, or seen in a build or test I ran; [inferred];
[unknown]; [reported] (said by an earlier step, not checked by me).


## Summary (round 2)

The round 1 fixes are in the tree and do what `REVIEW-RESOLUTION.md` says, F1 to F8 and F10 to
F12; F9 and F13 stay deferred for the reasons given there [verified: code read at v3.1-tests;
the mouse ring also under ThreadSanitizer, three clean runs]. No major problem found this round.

One minor problem, in the USB path again: when a device is unplugged while usb_task is setting
one of its interfaces up, `on_connected()` goes on to send the next class request to a device the
HID driver has already freed, because it never looks at the `gone` flag the round 1 fix added
(R2-F1). A PC model of the unchanged `usb_input.c` shows SET_IDLE begun on a freed device after an
unplug that lands as SET_PROTOCOL completes, and SET_PROTOCOL begun on one after an unplug just
after the open [verified: model run]. The request that is in flight at the unplug also finishes
on freed memory inside usb_host_hid itself, which the firmware cannot fix alone [verified:
component source; inferred: effect].

The rest are notes: two unlocked reads of the PS/2 queue's count that ThreadSanitizer reports
(R2-F2, by design, harmless on this core [inferred]); `console_release_display()` not honouring
the hold (R2-F3, very narrow); the task watchdog only printing, so a starved core 0 never shows
the crash screen (R2-F4); "LCD underrun" lines from the RGB driver's interrupt that would fill
the log ring if the DOS board's scan-out runs short of PSRAM bandwidth (R2-F5); and a 10 ms SD
power-up wait that is 0 to 10 ms at 100 ticks a second (R2-F6).

One round 1 unknown is settled: this toolchain's picolibc gives every FILE a lock
[verified: picolibc headers], so two tasks' `printf` lines do not mix characters in the
console's line buffer [inferred: each call holds it]. Also, nothing needs setting for RGB
underrun recovery on the S31, as the IDF's restart workaround is ESP32-S3 only [verified: IDF
source].


## How I checked (round 2)

* Read at v3.1-tests every file the Korvo-1 and DOS board builds compile from `esp/main`
  (`esp_main.c`, `console.c`, `display.c`, `speed.c`, `usb_input.c`, `kbd_input.c`, `bootmenu.c`,
  `crash.c`, `lcd_korvo1.c`, `lcd_dosboard.c`, `dosboard.c`, `ch9350.c`, `i2s.c`, `storage.c`),
  `i8042.c` whole, and the parts of `vga.c` and `pc.c` that run on the other core [verified].
* Read the IDF and component sources named in each item [verified].
* `round2/usbmodel2/model2.c`: `usb_input.c` unchanged, compiled with the existing host test's
  stand-ins (`tests/host/usb/stubs`), a stand-in usb_host_hid that marks a device freed the way
  `hid_host_device_disconnected()` does, and an unplug placed inside or just after one chosen
  call. Seven runs, F to L; output `round2/usbmodel2/model2.log` [verified: ran, exit 2].
* `round2/kbdtsan/kbd_threads.c`: `i8042.c` built as on the board (`BUILD_ESP32`,
  `PS2_KBD_HOOKS`) with a USB-task thread typing through `ps2_put_keycode()` under a lock that
  stands for `kbd_input.c`'s mutex, and an emulator thread running `kbd_step()`, reading port
  60h and sending light and reset commands whose hooks take that lock. Three plain runs and four
  under `-fsanitize=thread` (one with every report, three with the known `kbd_step` report
  suppressed). Logs in `round2/kbdtsan/` [verified: ran].
* `round2/mousetsan/`: the tree's own two-thread mouse test under ThreadSanitizer, three runs
  [verified: ran, 0 warnings, 6 of 6 checks each].


## Round 1 findings at v3.1-tests

| # | Round 1 | Now | Evidence |
|-|-|-|-|
| F1 | major | fixed | `close_interface()` frees the slot, then both closes (`usb_input.c` 181 to 187); `if_callback()` acts only for the handle its slot holds (147); `on_gone()` checks the handle (298). The interface stays listed until the second close (`hid_host.c` 1474 to 1491), so no other interface can reuse that address while EV_GONE waits, and the queue is FIFO, so a stale EV_GONE cannot reach a newer interface [verified: code; model2 runs F to I end with the slot free] |
| F2 | minor | fixed as far as EV_LEDS goes | `gone` set before the post (148 to 149), `set_lights()` checks it (173). `on_connected()` does not: R2-F1 |
| F3 | minor | fixed | `usb_events` 4096 bytes (400); `stacks_line()` every 10 s (`display.c` 38 to 58); IDF counts stack in bytes (`portmacro.h` 95, 98: `StackType_t` is `uint8_t`) so the figures are bytes as the line says [verified] |
| F4 | minor | fixed | black instead of `abort()` with `VGA_TWO_CORES` (`vga.c` 1810 to 1815, 1856 to 1861) [verified] |
| F5 | minor | fixed | reports into a ring, packets made in `kbd_step()` (`i8042.c` 831 to 975); single producer under `kbd_input.c`'s lock, release and acquire on head and tail, compare-and-swap on the overflow word [verified: code]; three ThreadSanitizer runs clean [verified: `round2/mousetsan/run1-3.log`] |
| F6 | minor | fixed | `console_hold()` (`console.c` 613 to 617), `speed_screen_poll()` returns while held (`speed.c` 287). A very narrow window remains: R2-F3 |
| F7 | note | fixed | `speed_forget_requests()` before BIT0 (`esp_main.c` 446 to 447) [verified] |
| F8 | note | fixed | `esp_rom_vprintf()` when `!xPortCanYield()` (`console.c` 711 to 715); it is `ets_vprintf(esp_rom_output_to_channels, ...)` (`esp_rom_print.c` 293 to 296), so the UART and, through channel 2, the ring [verified] |
| F9 | note | deferred | unchanged; the display line's "longest" write-back is an upper bound on how long an SD transfer can wait for the msync mutex, so the planned measurement does answer it [inferred] |
| F10 | note | fixed | own binary semaphore, 5 s (`usb_input.c` 395, 403) [verified] |
| F11 | note | fixed | every creation checked (`esp_main.c` 1028 to 1032, `usb_input.c` 400 to 402 and 431 to 432, `kbd_input.c` 471 to 473, `ch9350.c` 554 to 555, `dosboard.c` 454 to 456, `i2s.c` 185 to 186) [verified] |
| F12 | note | fixed | the driver's tasks wait up to 100 ms (`usb_input.c` 149, 164), the emulator's core never (350), losses counted and shown (320 to 327) [verified] |
| F13 | note | deferred | unchanged; the resolution's reason for keeping `i2s_task` at priority 0 holds [inferred] |


## New findings

### R2-F1 (minor) `on_connected()` sends class requests to a device the HID driver has freed

Where: `esp/main/usb_input.c` 232 to 284 (`hid_host_device_open()`, then
`hid_class_request_set_protocol()` 244, `hid_class_request_set_idle()` 250,
`hid_host_get_report_descriptor()` 255, `hid_host_device_start()` 276, `set_lights()` 284), with
`if_callback()` 142 to 151.

What happens:

* usb_task (priority 2) and the HID driver's task (priority 5) are both on core 0
  [verified: `usb_input.c` 414 to 420, 431]. Every class request blocks usb_task until the HID
  driver's task has handled the control transfer (`hid_control_transfer()`, `hid_host.c` 936 to
  968, waiting at 953) [verified].
* In one pass the HID driver's task first completes the done control transfers, then handles
  the client events, DEV_GONE among them (`usb_host.c` 1299 to 1321) [verified]. On DEV_GONE it
  closes each interface once (our `if_callback()` sets `gone` and posts EV_GONE) and then frees
  the device: `usb_host_transfer_free(ctrl_xfer)`, `vSemaphoreDelete()` of `ctrl_xfer_done` and
  of `device_busy`, `free(hid_device)` (`hid_host.c` 536 to 565, 1242 to 1268) [verified]. The
  interface stays listed, in WAIT_USER_DELETION, until our second close (1474 to 1491)
  [verified].
* So an unplug that arrives while usb_task waits for a request is handled, device freed, before
  usb_task runs again [inferred: priorities and the order in `usb_host.c` 1299 to 1321]. usb_task
  then (a) finishes that request on freed memory: it returns from `xSemaphoreTake()` on the
  deleted `ctrl_xfer_done`, reads the freed transfer and gives the deleted `device_busy`
  (`hid_host.c` 953 to 966, 1114) [verified: code; inferred: effect]; and (b) goes on in
  `on_connected()` to the next request, which `get_iface_by_handle()` still lets through (the
  interface is listed) and which goes through `iface->parent`, the freed `hid_device_t`
  (`hid_host.c` 1836, 1854; `hid_class_request_set()` 1094) [verified].
* (b) is the firmware's: `on_connected()` never reads `hif->gone`. (a) is in usb_host_hid
  (`usb_host_transfer_free()` frees without looking at whether the transfer is in flight,
  `usb_host.c` 1952 to 1960) and is the "request already in flight" that round 1 F2 left as a
  limit of the component.

Evidence, model `round2/usbmodel2/model2.log` [verified: ran]:

* K: unplug as SET_PROTOCOL completes: "set_idle begun on if0 boot keyboard, whose
  hid_device_t is freed".
* J: unplug just after the open: "set_protocol begun ... freed".
* L: unplug as SET_IDLE completes: `hid_host_device_start()` refuses the WAIT_USER_DELETION
  state first, so nothing more is sent.
* F to I: unplug during SET_PROTOCOL, SET_IDLE, the first SET_REPORT, or an EV_LEDS SET_REPORT:
  only the request in flight meets the freed device (case (a)), and the slot ends free.
* Side effects seen in the model: after F and J the console says "not a keyboard or mouse,
  left alone" for a keyboard that was unplugged, and after G and K "could not be started"; on
  the board the second close in `close_interface()` would also print the component's red
  "HID interface handle not found" (`hid_host.c` 253), since the first close there already
  removed the interface [inferred: model state plus `hid_host.c` 1474 to 1491, 248 to 259].

How likely: it needs the unplug inside the few milliseconds of one device's set-up (three or
four control transfers) or of a light change. A loose plug, a hub pulled out with its devices
during boot, or a device that drops off and comes back while being set up would do it [inferred].
The result is heap use after free on usb_task: corruption that may show later as any crash, or
nothing [inferred].

Fix:

1. In `on_connected()`, after the open and after each request, return at once if
   `__atomic_load_n(&hif->gone, __ATOMIC_ACQUIRE)` is set, with a console line "interface N
   unplugged while being set up". Leave the slot as it is: the EV_GONE already queued carries
   the same handle, and `on_gone()` makes the second close and frees the slot. This removes (b)
   and the misleading lines; a few instructions of window stay between the check and the call.
2. For (a): either a local patch to usb_host_hid's `hid_host_uninstall_device()` that takes
   `device_busy` (with a timeout) before deleting it, so a request whose completion was just
   delivered can finish first; or report it to Espressif (Plat's call; Espressif's own example
   makes class requests from its application task the same way, round 1 F1). Without one of
   these the in-flight case stays [inferred].
3. Keep `model2.c` cases J and K as host tests of 1.

### R2-F2 (note) Two unlocked reads of the PS/2 queue's count; and the E0 and ACK case measured

Where: `i8042.c` 651 (`kbd_step()` reads `s->kbd->common.queue.count`), 705
(`ps2_kbd_pending()`), against the writes under the queue mutex at 530 (`ps2_queue()`) and 680
(`ps2_read_data()`).

ThreadSanitizer reports exactly these two races on the keyboard path and nothing else
[verified: `round2/kbdtsan/tsan.log` reports 651 against 530; `tsan-supp-3.log`, with 651
suppressed, reports 705 against 680; `tsan-supp-1.log` and `-2.log` clean]. Both are documented
hints ("no lock: a hint", 702). On RV32 an aligned `int` load is a single access, and each is
read once per call, so neither can tear or be cached across calls [inferred]; a missed count is
picked up by the next `kbd_step()` or poll [inferred]. Fix if wanted: `__atomic_load_n(...,
__ATOMIC_RELAXED)` at the two reads, which makes them defined in C11 without a lock.

Same runs, for the record:

* No deadlock between `kbd_input.c`'s mutex taken on the emulator's core by
  `ps2_kbd_leds_changed()` and `ps2_kbd_rate_changed()` (`kbd_input.c` 480 to 499, from
  `i8042.c` 697, 753, 767, 774) and the USB task spinning on `delay_flag` while holding that
  mutex (`i8042.c` 585 to 586): the emulator only holds `delay_flag` inside `kbd_step()`, never
  while it calls a hook [verified: code; seven runs, no alarm, 0 mutex errors].
* With the guest sending a light command every 2 ms (far more than any real guest), 618 to 663
  of 3,000 E0 pairs had an ACK or AA byte between the E0 and its second byte [verified: plain and
  suppressed runs]. That is round 1's E F3, still deferred; at real command rates it is rare
  [inferred].
* The one run with ThreadSanitizer reporting was missing 83 of the 6,000 key events and split
  one E0 pair: while ThreadSanitizer printed its report the emulator thread stalled and the
  256-byte queue overflowed (`i8042.c` 518 to 525 drops a byte when full) [verified: that run's
  counts in `tsan.log`; the three suppressed runs and the three plain runs lost nothing]. A test artefact, but it is what a long stall of core 1
  would do to fast typing [inferred].

### R2-F3 (note) `console_release_display()` does not honour the hold

Where: `console.c` 586 to 592 (sets `s_owner_console = false` whatever `s_held` says); callers
`speed.c` 268 (`give_back()`) and `esp_main.c` 443.

`speed_screen_poll()` checks `console_held()` once (`speed.c` 287), then may run `give_back()`. A
`console_fatal()` on another task that completes in between, including its full redraw, is then
undone: `give_back()` releases the panel, clears the frame buffer and the next picture is the PC's
(`speed.c` 265 to 279; `display.c` 78 to 84) [inferred: code paths]. The window is a few
microseconds of vga_task at priority 0, so it needs a fatal from another task at that moment
[inferred]. The same applies to `i386_task`'s release at `esp_main.c` 443 after a fatal from
`app_main` (only the "tasks could not be started" one at 1030 can come then) [inferred]. Fix:
`if (s_held) return;` at the top of `console_release_display()`.

### R2-F4 (note) The task watchdog only prints; `crash.c` says otherwise

Where: `sdkconfig.korvo1`, `sdkconfig.dosboard`; generated `sdkconfig.h` 748 to 751 has
`CONFIG_ESP_TASK_WDT_EN`, `_INIT`, `_TIMEOUT_S 5`, `_CHECK_IDLE_TASK_CPU0` and no
`CONFIG_ESP_TASK_WDT_PANIC` [verified: both builds]. `crash.c` 4 to 7: "every panic, abort,
failed assert and watchdog timeout comes here first".

Without the panic option a task watchdog timeout prints a backtrace and goes on
(`esp_system/task_wdt/task_wdt.c` 456 to 468) [verified]. Those lines reach the UART and, through
`esp_rom_printf()` and channel 2, the ring [inferred], but not the panel and not the crash
screen. So if core 0 were ever starved (IDLE0 not run for 5 s), the panel would freeze with no
message; only the interrupt watchdog (800 ms, both cores, `sdkconfig.h` 745 to 747) reaches the
crash screen [inferred]. Nothing in the code is known to starve core 0 [inferred: every core 0
task blocks or yields]. Fix: `CONFIG_ESP_TASK_WDT_PANIC=y` in both sdkconfig files, so a starved
core 0 shows the crash screen (Plat's call), or say in `crash.c` that the task watchdog only
logs.

### R2-F5 (note) "LCD underrun" from the RGB interrupt would fill the log ring

Where: IDF `esp_lcd/rgb/esp_lcd_panel_rgb.c` 1512 to 1516 (`ESP_EARLY_LOGE(TAG, "LCD
underrun")` in the interrupt handler), enabled with `LCD_LL_EVENT_RGB` at 704, which for
esp32s31 includes `LCD_LL_EVENT_UNDERRUN` (`esp_hal_lcd/esp32s31/include/hal/lcd_ll.h` 38 to 41)
[verified].

`ESP_EARLY_LOGE` is `esp_rom_printf()` (`log/include/esp_log.h` 251 to 258) [verified], so each
underrun goes to the UART from the interrupt and into the ring through `con_rom_putc()`
[inferred]. If the DOS board's scan-out (800x600 at 40 MHz, 16 bits, from PSRAM with no bounce
buffers by default, `board_dosboard.h` 42, 57) runs short of PSRAM bandwidth beside the emulator
and the write-backs, that is a line every frame, 60 a second, about 30 bytes each: the 8 KiB ring
would hold nothing else after five seconds or so, and the crash screen and the next boot would
show only underruns [inferred]. Whether it happens is [unknown] until the board runs. It is also
the most direct sign of a bandwidth shortfall, which nothing else measures. Fix: add to
TEST-PLAN for the DOS board (and the Korvo-1) a look for "LCD underrun" on the UART; if it
appears, try `lcd_bounce_px` or a lower load before anything else.

### R2-F6 (note) The SD card's 10 ms power-up wait is 0 to 10 ms

Where: `storage.c` 62 to 64: `gpio_set_level(SD_PWR_EN_GPIO, 0); vTaskDelay(pdMS_TO_TICKS(10));`
with `CONFIG_FREERTOS_HZ=100` (`sdkconfig.korvo1` 30, `sdkconfig.dosboard` 29) [verified].

`pdMS_TO_TICKS(10)` is one tick, and `vTaskDelay(1)` ends at the next tick, anywhere from almost
at once to 10 ms [verified: tick rate; inferred: FreeRTOS semantics]. The IDF waits two ticks
(`SDMMC_GO_IDLE_DELAY_MS` 20 over a 10 ms tick, so 10 to 20 ms) after CMD0
(`sdmmc/sdmmc_cmd.c` 128 to 130, `esp_private/sdmmc_common.h` 35) [verified], so a card still
powering up at CMD0 probably still initialises [inferred]. If it does not, the Korvo-1 tries again after 2 s, but on the DOS board
`dosboard_load_config()` makes one attempt and then uses the defaults instead of `dosboard.ini`
(`dosboard.c` 384 to 388) [inferred]. Fix: `pdMS_TO_TICKS(10) + 1`, as `dosboard.c` 153 to 160
already does for its waits.


## Checked and found right in round 2

* `stacks_line()` and deleted tasks: `xTaskGetHandle()` also searches tasks waiting to be freed
  and either core's idle task may free one (`tasks.c` 2858 to 2914, 4566 to 4615) [verified].
  i386_main is covered: it sets `s_held` and takes the display lock before it deletes itself,
  and `stacks_line()` reads nothing while held (`esp_main.c` 465 to 466, `console.c` 613 to 617,
  `display.c` 47 to 56) [verified]. The only other listed tasks that delete themselves, usb_events
  and ch9350, do so at boot only on a failed install (`usb_input.c` 376 to 378, `ch9350.c` 479 to
  483), at least 10 s before the first stacks line [inferred].
* The log hook's constrained check: IDF's own (log version 2) counts an interrupt, the cache off
  or the scheduler not running (`log/src/os/util.c` 20 to 27); the hook checks interrupt or
  critical section through `xPortCanYield()`, which on a CLIC core tests the interrupt level and
  threshold (`portmacro.h` 693 to 707) [verified]. With the scheduler suspended IDF's own tag check
  already takes a mutex with a timeout (`tag_log_level.c` 65 to 69), so the hook adds no new way
  to fail [inferred].
* picolibc in this toolchain defines `__STDIO_LOCKING` (`picolibc.h` 140) and every FILE has a
  recursive lock (`stdio.h` 103 to 105) [verified], so a `printf` holds the tee FILE for its whole
  output and two tasks' lines do not mix in `s_tee_line` [inferred]. Lock order on every path:
  tee FILE, then the console lock, then the UART's FILE [inferred: call sites].
* Keyboard path: no deadlock between `kbd_input.c`'s mutex on core 1 and the `delay_flag` spin
  (R2-F2) [verified: runs]. Mouse ring: clean under ThreadSanitizer [verified].
* Cache: one data bus (`CACHE_LL_DEFAULT_DBUS_MASK` is DBUS0) and one cache level
  (`esp_hal_cache/esp32s31/include/hal/cache_ll.h` 30 to 38) [verified], as round 1 found.
* RGB panel: the separate restart link and `CONFIG_LCD_RGB_RESTART_IN_VSYNC` are ESP32-S3 only
  (`esp_lcd_panel_rgb.c` 60 to 63, `esp_lcd/Kconfig` 15 to 22); `esp_lcd_rgb_panel_restart()`
  returns NOT_SUPPORTED elsewhere (542 to 547) [verified]. Nothing to set for the S31.
* SD mount retries: `esp_vfs_fat_sdmmc_mount()` deinitialises the host and frees the card on
  failure (`fatfs/vfs/vfs_fat_sdmmc.c` 300 to 308) [verified], so `mount_storage()`'s two-second
  retry starts clean.
* Panic path: the only `esp_rom_printf()` calls under `esp_system/port/arch/riscv` are in
  `print_task_backtrace()` (`debug_helpers.c` 53 to 71), which the panic handler does not use
  [verified: grep], so round 1's "the ring's spinlock cannot hang the panic output" stands.
* Timers: the USB host library's auto-suspend timer is created but reset only while active
  (`usb_host.c` 255 to 260, 675) [verified]; FreeRTOS's timer task is unpinned at priority 1
  (`sdkconfig.h` 887 to 889) and cannot preempt i386_main at 3 [inferred]. No esp_timer
  callbacks in the firmware [verified: grep of `esp/main`].
* DOS board tasks: the notes lock (`dosboard.c` 73 to 77, 86 to 92), the CH9350L lights through
  one atomic (`ch9350.c` 407, 441, 498), and the hot plug task as the only user of the MS7210 and
  its I2C bus after `ms7210_init()` (`dosboard.c` 424 to 457) [verified: code].


## Not checked, or unknown

* Whether usb_host_hid newer than 1.2.0 changes the free under a request in flight (R2-F1 (a))
  [unknown]; I downloaded nothing.
* Whether `FDEV_SETUP_STREAM` gives the tee FILE an initialised lock or picolibc makes it on first
  use, and if so whether two tasks' first `printf` at once could race on making it [unknown];
  the first use is in `app_main` before the other tasks print [inferred].
* Everything that needs the board: stack marks, LCD underruns, how real devices enumerate and how
  often an unplug lands inside set-up [unknown].


## Appendix: the round 1 report, word for word

# Review: concurrency and ESP-IDF use in tiny386 bare-metal v3

Lens: two cores and interrupts, shared state, locks and atomics, frame buffer cache
write-back against the LCD DMA, ESP-IDF calls for esp32s31 in this IDF, task stacks and
priorities, watchdogs. Reviewed 2026-10-21.

* Tree: WSL `~/tiny386-idf-scratch/v3/tiny386`, tag `v3` = 2bb1c76, exported with `git archive`
  to `~/tiny386-idf-scratch/review-concurrency-esp-idf/src` and read there [verified].
* ESP-IDF v6.1-dev-8142-g188e3e55bb (`~/esp/esp-idf`), espressif/usb 1.6.0 and
  espressif/usb_host_hid 1.2.0 as `v3/dependencies.lock` pins them, read in rel5's
  `managed_components` [verified].
* Nothing ran on a board. No serial port was opened. Nothing in the tree or in `v3` was changed
  apart from adding this file. My work files are in
  `~/tiny386-idf-scratch/review-concurrency-esp-idf` (WSL).

Tags: [verified] read in the named source, or seen in a build or test I ran; [inferred];
[unknown]; [reported] (said by an earlier step, not checked by me).

## Summary

The two-core design holds up. The S31 has a single L1 data cache [verified: `cache_ll.h`, see
below], which I take to serve both cores [inferred], so the emulator's video memory (written on
core 1) and the frame buffer (drawn on core 0, and by the console on either core) are coherent
between the cores; only the LCD DMA needs the write-back, and `lcd_flush()` gives it whole,
line-aligned rows under the display mutex [verified: code; inferred: alignment]. The crash
screen's path through the panic handler is sound for this IDF [verified: IDF source and the
shipped ELFs]. Locks are ordered so that nothing can wait for ever [inferred: every lock site
read].

One major problem: `usb_input.c` uses the HID driver's two-step close the wrong way. An
interface that `on_connected()` closes (one that is not a keyboard or mouse, or one that fails
to start) posts a disconnect event that is handled later by slot, not by handle, so it closes
whatever interface took that slot in the meantime [verified: code and a model run]. A PC model
of the unchanged file shows a keyboard closed straight after it was opened, for a device whose
first HID interface is vendor-defined, and a mouse closed when its connect event was queued
behind such an interface [verified: model run]. The fix is small (F1). How many real devices
are affected is [unknown].

The rest are minor or notes: a use-after-free window for the keyboard lights on unplug (F2),
unmeasured task stacks with `usb_events` below Espressif's own size (F3), an `abort()` reachable
from a cross-core VBE register race (F4), PS/2 mouse packets that two cores can interleave
(F5, pre-existing), and the speed screen able to lift a fatal message (F6). Each is tagged in
its own section.

## How I checked

* Read every file in `esp/main` that the Korvo-1 and DOS board builds compile, the v3 diff of
  `vga.c`, `pc.c`, `i8042.c`, `ide.c`, `sb16.c` against `pristine`, and the IDF and component
  sources named below [verified].
* PC model of `usb_input.c`: the file itself, unchanged, compiled with stand-ins for FreeRTOS
  and for usb_host_hid whose `hid_host_device_close()` follows hid_host.c lines 1441 to 1492,
  plus the real `hid_parse.c`. Three runs. Files: `usbmodel/model_test.c`, `usbmodel/stubs/`,
  output `usbmodel/model_test.log` in the work folder [verified: ran].
* Recompiled the firmware's own sources with `-fstack-usage` from rel5's
  `compile_commands.json`, objects into `su-korvo1/` and `su-dosboard/` in the work folder only
  [verified: ran].
* Looked at the shipped ELFs (`v3/images/*/tiny386.elf`) with the toolchain's `nm` and
  `objdump` [verified: ran].

## Tasks and interrupts as built

* `main` (app_main): core 0, 8192 bytes (sdkconfig); returns after creating the next two
  [verified: `esp_main.c` 850 to 985].
* `i386_main`: core 1, priority 3, 4096 bytes, never yields while the PC runs except in SD
  waits [verified: `esp_main.c` 983].
* `vga_task`: core 0, priority 0, 4096 bytes, one picture per 3 ticks (`LCD_FRAME_MS` 30,
  `CONFIG_FREERTOS_HZ` 100) [verified: `esp_main.c` 984, `display.c` 40 to 87].
* `i2s_task` (DOS board): core 0, priority 0, 4096 bytes [verified: `i2s.c` 177].
* `usb_events`: core 0, priority 2, 3072 bytes; HID driver task: core 0, priority 5, 4096;
  `usb_task`: core 0, priority 2, 4096 (Korvo-1) [verified: `usb_input.c` 349, 358 to 365, 375].
* `kbd_repeat`: core 0, priority 4, 2048 [verified: `kbd_input.c` 470].
* `ch9350`: core 0, priority 3, 4096; `hdmi`: core 0, priority 1, 4096 (DOS board)
  [verified: `ch9350.c` 530, `dosboard.c` 386].
* Every peripheral driver is installed from a task on core 0 (LCD and SD in app_main or its
  `lcd_init()`, USB in `usb_events`, UART1 in `ch9350`, I2S and I2C in app_main), so their
  interrupts are on core 0 and core 1 takes only the tick, IPC and cross-core interrupts
  [verified: call sites; inferred: IDF allocates an interrupt on the installing core].

## Findings

### F1 (major) `usb_input.c`: a close in `on_connected()` later closes another interface

Where: `esp/main/usb_input.c` lines 229 to 235 (interface left alone), 244 to 251 (start
failed), with `if_callback()` 126 to 129 and `on_gone()` 263 to 277.

What happens:

* usb_host_hid's `hid_host_device_close()` works in two steps. On an interface with a user
  callback, the first call sets `WAIT_USER_DELETION` and calls that callback with
  `HID_HOST_INTERFACE_EVENT_DISCONNECTED`, in the caller's task; only a second call removes the
  interface [verified: `managed_components/espressif__usb_host_hid/hid_host.c` 1474 to 1491].
  Espressif's example makes the second call from inside the callback [verified:
  `examples/peripherals/usb/host/hid/main/hid_host_example.c` 495 to 498].
* In `on_connected()` the first call runs `if_callback(DISCONNECTED)` on `usb_task`, which posts
  `EV_GONE(h, hif)` to the back of `usb_task`'s own queue. `on_connected()` then frees the slot
  (`hif->handle = NULL`, or `memset`). `on_gone()` looks only at the slot (line 265), never at
  the handle in the event [verified].
* If another `EV_CONNECTED` is already queued, it takes the lowest free slot (178 to 184), which
  is that one. The stale `EV_GONE` then runs `kbd_input_close()` and `hid_host_device_close()`
  on the new interface and prints "USB: keyboard on interface N unplugged" [verified: model run].
* The interface that was meant to be closed stays in `WAIT_USER_DELETION` until the device is
  unplugged [verified: model run; hid_host.c 536 to 565 removes it then].
* The driver announces a device's interfaces in descriptor order, all at once [verified:
  hid_host.c 399, 453 to 481, 426 to 445]. So the keyboard or mouse dies whenever a device's
  first HID interface is neither a boot keyboard or mouse nor parsed as one, and it dies again
  on every replug [verified: model run A]. Two devices whose connect events queue together can
  lose the second one [verified: model run C; inferred: how often real enumeration does that].
  Which real devices put a vendor or consumer interface first is [unknown].

Evidence: model output (`usbmodel/model_test.log`):

* A, interfaces [vendor, boot keyboard]: keyboard ends in `WAIT_USER_DELETION`, no slot,
  console "USB: keyboard on interface 1 unplugged". Vendor interface half closed.
* B, [boot keyboard, vendor]: keyboard fine; vendor interface half closed. B2, keyboard then
  unplugged: removed correctly.
* C, keyboard [boot keyboard, vendor] and a boot mouse queued behind it: mouse closed.

Fix: pass the handle to `on_gone()` and do nothing unless `hif->handle == e.handle`; in the two
failure paths of `on_connected()` clear `hif->handle`, then call `hid_host_device_close(h)`
twice (the first raises the DISCONNECTED that the check now ignores, the second removes the
interface). Keep the model as a host test of the order.

### F2 (minor) USB lights: SET_REPORT can reach a device the HID driver has already freed

Where: `usb_input.c` 292 to 297 (`EV_LEDS`) and 147 to 154 (`set_lights()`);
`NOTES-core2.md` 178 to 179.

On unplug the HID driver's task makes the first close step (the callback posts `EV_GONE`) and
then frees the device: `hid_host_uninstall_device()` frees `ctrl_xfer`, the `device_busy`
semaphore and the `hid_device_t` [verified: hid_host.c 536 to 565, 1242 to 1268]. The interface
stays listed until `usb_task`'s `on_gone()` makes the second call. An `EV_LEDS` ahead of
`EV_GONE` in the queue, or a `set_lights()` already running, calls
`hid_class_request_set_report()`, which goes through `iface->parent` to the freed device
[verified: hid_host.c 1799 to 1817, 1086 to 1117]. NOTES-core2's "a handle is never closed under
a SET_REPORT" is right about the close, not about the free. It needs a light change within a
few milliseconds of an unplug [inferred].

Fix: in `if_callback()`'s DISCONNECTED branch mark the slot gone (an atomic flag that
`set_lights()` checks) before posting. A request already in flight stays a limit of the
component, which frees under it [inferred].

### F3 (minor) Task stacks are not measured; `usb_events` is below Espressif's size

* `usb_events` has 3072 bytes (`usb_input.c` 349); Espressif's example and the component's own
  tests give it 4096 [verified: hid_host_example.c 657 to 661; test_hid_basic.c 833 to 836].
  The host library's enumeration and hub work run inside `usb_host_lib_handle_events()` on it
  [inferred], and any `ESP_LOGx` there runs the console hook on the same stack: `vsnprintf`,
  the panel drawing, `lcd_flush()`, `esp_cache_msync()` (`console.c` 695 to 712) [verified].
* `i386_main` keeps pristine's 4096 bytes [verified: pristine `esp_main.c` 306] but now also
  prints console lines, runs `speed_run_step()`, `ide_flush()` through FatFs and SDMMC, and
  `console_fatal()`. Frames from `-fstack-usage`: `vga_graphic_refresh` 1184, `sb16`
  `write_audio` 608, `on_connected` 512, `cpu_exec1` 336, `pmcall` 320, `call_isr` 240,
  `speed_run_step` 224 bytes [verified: `su-korvo1/*.su`]. IDF's frames under FatFs were not
  measured [unknown].
* The hardware stack guard is on (`CONFIG_ESP_SYSTEM_HW_STACK_GUARD`), so an overflow would end
  on the crash screen rather than in silent corruption [verified: generated sdkconfig.h;
  inferred behaviour].

Fix: 4096 for `usb_events`; print `uxTaskGetStackHighWaterMark()` of every task with the
10-second display line, and add the figures to TEST-PLAN's first boot.

### F4 (minor) `vga.c` can `abort()` on core 0 during a VBE register write on core 1

Where: `vga.c` 2440 to 2447 (store the guest's value, then fix it up), 783 to 808
(`vbe_fixup_regs()` turns an unknown BPP into 8), 1463 (core 0 reads BPP), 1810 to 1811 and
1848 to 1849 (default case: `fprintf`, then `abort()`).

`vbe_write()` stores the raw value first and corrects it afterwards; the enable bit is also
stored before the fix-up. `vga_task` reads `vbe_regs[]` on the other core without a lock, so a
guest that writes an unsupported BPP while VBE is on (or turns VBE on with one set) can be seen
half way, and the per-pixel switch aborts: the crash screen [verified: code paths; inferred:
timing]. Upstream code, safe where drawing and the CPU share a thread [inferred]. Needs an
unusual guest [inferred].

Fix: draw black in the default case instead of `abort()`, as `vga_pixel()` already does
(`vga.c` 1279 to 1313), or
compute the fixed-up value before storing it.

### F5 (minor, pre-existing) PS/2 mouse state and packets are written from both cores

Where: `i8042.c` 767 to 793 (`ps2_mouse_event()`, from `kbd_input_mouse()` on the HID or
CH9350L task, core 0) and 795 to 919 (`ps2_write_mouse()`, `i386_main`, core 1, no lock).

Both read and write `mouse_status`, `mouse_dx`, `mouse_dy`, `mouse_dz`, `mouse_buttons`, and
both queue bytes. The queue mutex is taken per byte (476 to 504), so a command reply can fall
between the three or four bytes of a movement packet, and the guest's driver loses packet
alignment until it resynchronises; likely while a driver sets rate or resolution with the mouse
moving [inferred]. Unchanged from pristine: v3's four hunks in `i8042.c` start at pristine
lines 482, 614, 659 and 670 (the full-queue fix and the `PS2_KBD_HOOKS` additions), and none
reaches the mouse functions [verified: git diff of `i8042.c`, pristine to v3].

Fix: on core 0 only add to atomic delta counters and a buttons word; let `kbd_step()` on core 1
build the packets.

### F6 (minor) The speed screen can lift a fatal message or the "switched off" screen

Where: `speed.c` 276 to 319 and 260 to 274; `console.c` 610 to 624; `esp_main.c` 418 to 421.

After `console_fatal()` or the PC switching off, the console owns the panel but `vga_task`
still calls `speed_screen_poll()`. Ctrl+Alt+F12 twice, or on the DOS board a press and release
of the USER key (GPIO42, read every picture, `speed.c` 285 to 300), runs `give_back()`, which
hands the panel back to the stopped PC's last picture; the message is then only on the UART and
in the ring [inferred: code paths].

Fix: a flag set by `console_fatal()` and the switch-off path that keeps the console up.

### F7 (note) A Ctrl+Alt+F12 from before the PC starts is cleared too late

`speed_run_init()` clears the request (`speed.c` 185) only in `run_pc()` (`esp_main.c` 417),
after BIT0 has woken `vga_task` (402). `vga_task` draws a whole picture before it polls, so
`i386_main` almost always wins [inferred]. Fix: clear the request before setting BIT0.

### F8 (note) `ESP_LOGx` from a critical section or an ISR is now dropped altogether

`con_log_vprintf()` returns 0 when `!xPortCanYield()` (`console.c` 697 to 698), so such a line
reaches neither the UART nor the ring [verified]. Fix: hand it to `esp_rom_vprintf()` (ROM, no
locks; declared in `esp_rom_sys.h` 56), which also reaches the ring through putc channel 2.

### F9 (note) One `esp_cache_msync()` mutex ties core 1's SD transfers to core 0's write-back

With `CONFIG_ESP_MM_CACHE_MSYNC_C2M_CHUNKED_OPS=y`, every task-context `esp_cache_msync()`
holds one global mutex for its whole length [verified: `esp_mm/esp_cache_msync.c` 30 to 32,
69 to 91, 127, 157]. A full-frame `lcd_flush()` (768,000 or 960,000 bytes) holds it; the SDMMC
driver syncs PSRAM data buffers through the same call [verified: `sd_trans_sdmmc.c` 545, 581],
and FatFs's buffers prefer PSRAM (`CONFIG_FATFS_ALLOC_PREFER_EXTRAM`) [verified], so a disk
access on core 1 can wait for a whole frame's write-back [inferred]. Measure with the display
line during a disk benchmark; if it matters, flush in bands with a yield between them.

### F10 (note) `usb_setup()` waits 2 s for a notification that may come after app_main returns

`usb_lib_task` notifies the task handle it was given (`usb_input.c` 329); `usb_setup()` waits
at most 2 s (351). If the install ever took longer, the late notification would go to the main
task, which IDF deletes when `app_main()` returns [inferred]. Fix: wait without a timeout, or use
a semaphore owned by `usb_input.c`.

### F11 (note) Task creation results are not checked

None of the `xTaskCreatePinnedToCore()` calls checks the result (`esp_main.c` 983 to 984,
`usb_input.c` 349 and 375, `kbd_input.c` 470, `ch9350.c` 530, `dosboard.c` 386, `i2s.c` 177)
[verified]. With about 260 KB of internal RAM free [reported: integration summary] a failure is
unlikely, but it would be silent (no picture, no keyboard). Fix: `console_fatal()` on failure.

### F12 (note) `usb_task`'s queue drops events when full

`post()` uses a zero wait on a 16-entry queue (`usb_input.c` 81, 348) [verified]. A lost
`EV_GONE` would leave that keyboard's keys held in `kbd_input.c`, repeating [inferred].
Unlikely with 16 entries [inferred].

### F13 (note, pre-existing) Sound state is read on core 0 without locks

`mixer_callback()` (`pc.c` 901) on `i2s_task` reads SB16, OPL and PC speaker state that
`i386_main` changes. The SB16 ring has one writer per index (`audio_q` on core 1, `sb16.c` 1224;
`audio_p` on core 0, 1495 and 1517) and the cores share the data cache, so it works in practice
[inferred]; an OPL register written mid-synthesis costs a click at most [inferred]. `i2s_task`
shares priority 0 with `vga_task` and IDLE0; the DMA holds 6 by 240 frames, 30 ms at 48 kHz
[verified: `esp_driver_i2s/include/driver/i2s_common.h` 22 to 34], and round robin gives it a
slice at least every 10 ms tick [inferred]. Priority 1 would keep sound ahead of drawing at no
cost, since it blocks on the DMA.

## Checked and found right

* One cache level; the data cache has only cache id 0 (the instruction cache has ids 0 and 1),
  64-byte lines, and every PSRAM address maps to level 1, id "all" [verified:
  `esp_hal_cache/esp32s31/include/hal/cache_ll.h` 30 to 34, 847 to 881, 1028 to 1043]. So one
  data cache serves both cores [inferred], core 1's video memory writes, core 0's drawing and
  console drawing from either core are coherent, and only the LCD DMA needs the write-back
  [inferred from the cache layout].
* `lcd_flush()`: C2M with UNALIGNED (and INVALIDATE when `lcd_invalidate = 1`) is accepted; only
  M2C refuses UNALIGNED [verified: `esp_cache_msync.c` 122 to 156]. A row is 1600 bytes, 25
  lines, and the driver allocates the frame buffer at the GDMA's external alignment [verified:
  `esp_lcd_panel_rgb.c` 203 to 219, 1257 to 1265]; at 64 bytes or more no write-back or
  invalidate reaches a neighbouring row or heap block [inferred]. Every frame buffer writer holds
  the display mutex (`display.c` 51 to 57, `console.c` 570 to 578, `speed.c` 265 to 272), runs
  before the tasks (`lcd_init()`), or is the panic handler [verified: call sites], so the gap
  between the write-back and the invalidate (separate critical sections, `esp_cache_msync.c`
  147 to 153) cannot lose another writer's stores.
* Chunked write-back: 0x8000 is set for esp32s31, which has no Kconfig default [verified:
  `esp_mm/Kconfig` 3 to 26; generated sdkconfig.h]; 32 KiB per critical section is far inside
  the 800 ms interrupt watchdog [verified: value; inferred: timing].
* ROM write-back erratum: esp32s31 needs the "sync twice" patch and IDF links a patched
  `Cache_WriteBack_All()` [verified: `esp_rom/esp32s31/esp_rom_caps.h` 34,
  `esp_rom/CMakeLists.txt` 70 to 71, `patches/esp_rom_cache_writeback_esp32p4_esp32s31.c`];
  `crash.c`'s `cache_ll_writeback_all()` reaches it through `cache_ll.h` 526 to 556 [inferred:
  strong symbol over the ROM's PROVIDE].
* Crash screen: `panic_handler` calls `__wrap_esp_panic_handler`, which is in IRAM, in both
  shipped ELFs [verified: objdump]. The other core is stalled and the cache turned back on first
  [verified: `esp_system/port/panic_handler.c` 264 to 270, 301 to 322]. The PSRAM cache-bypass
  alias (+0x70000000) is RW in PMA entry 13 and left unmatched by PMP, so the stores are allowed
  [verified: `esp_hw_support/port/esp32s31/cpu_region_protect.c` 97 to 101 and the PSRAM PMP
  comment]. With PRINT_HALT all watchdogs are turned off and
  `esp_system_reset_modules_on_exit()` only drains the UARTs and touches ECC and crypto clocks,
  not LCD_CAM [verified: `esp_system/panic.c` 296 to 301, 493 to 496;
  `port/soc/esp32s31/system_internal.c` 43 to 58]. Panic output goes straight to the UART FIFO,
  not through ROM putc channel 2, so the ring's spinlock cannot hang it [verified: `panic.c` 80
  to 85, 116 to 127].
* ROM putc hook: `con_rom_putc`, `xPortEnterCriticalTimeout` and `xPortInIsrContext` are in IRAM
  and `s_ring` is in LP RAM (0x2e000000), so `esp_rom_printf()` with the cache off is safe
  [verified: nm of the Korvo-1 ELF].
* Log hook: with `CONFIG_LOG_VERSION_1` the hook is called once per line with the whole format
  [verified: `log/src/log.c` 37 to 43], so `emit_text()` sees whole lines. Under picolibc in this
  IDF `stdout` and `stderr` are single global pointers, so the tee covers every task [verified:
  `esp_libc/src/picolibc/picolibc_init.c` 146 to 155; `CONFIG_LIBC_PICOLIBC=1`].
* Lock order: console mutex, then the display mutex with a 200 ms limit, then the msync mutex.
  `vga_task` prints while holding the display mutex only when the console does not own the
  panel, and `render()` then returns before the try (`console.c` 272 to 277).
  `console_take_display()` takes the display mutex with no console lock held (594 to 607). No
  wait without end found [verified: every `display_lock` call site; inferred: conclusion].
* PS/2 keyboard: the full-queue fix is right (`i8042.c` 485 to 493) [verified].
  `ps2_put_keycode()`'s busy wait on `delay_flag` (521 to 522) only meets `kbd_step()` on core 1,
  and every core-0 caller holds `kbd_input.c`'s mutex, so no core-0 task can spin on a flag held
  by a lower-priority task on its own core [verified: call sites; inferred: conclusion].
* `kbd_input.c`: one mutex over all its state; the sink, hotkey and lights callbacks run under it
  and only queue, store an atomic or post (`bootmenu.c` 98 to 103, `speed.c` 231 to 234,
  `usb_input.c` 304 to 308, `ch9350.c` 420 to 423); the boot menu deletes its queue only after
  `kbd_input_set_sink(NULL)` (`bootmenu.c` 156 to 158) [verified].
* VGA clears asked for on core 1: one writer per counter, release and acquire (`vga.c` 261 to
  262, 279 to 282, 1351, 1577 to 1580, 2065 to 2075) [verified]. Torn mode changes draw one bad
  picture; reads stay inside video memory plus `VGA_MEM_SLACK` (`vga.c` 1670 to 1677, 1378 to
  1382) [inferred], except for F4.
* CH9350L: parser state only in its task; the lights pass through one C11 atomic (`ch9350.c`
  399, 422, 479); UART1 is installed by that task on core 0 [verified].
* DOS board notes: one spinlock around a 112-byte copy (`dosboard.c` 64 to 97) [verified].
* Watchdogs: the task watchdog watches IDLE0 only, `i386_main` is not subscribed, and
  `vga_task` and `i2s_task` run at IDLE0's priority so IDLE0 gets its turn [verified: generated
  sdkconfig.h; inferred: scheduling]. Interrupt watchdog 800 ms on both cores [verified].
* RGB panel fields (`hsync_idle_low`, `vsync_idle_low`, `pclk_active_neg`, colour formats,
  `bounce_buffer_size_px`, `dma_burst_size`, `fb_in_psram`) exist with the meanings the code
  assumes, and `dosboard.c`'s bounce size check matches the driver's [verified:
  `esp_lcd_panel_rgb.h` 69 to 73, 144 to 165; `esp_lcd_panel_rgb.c` 371 to 375; `dosboard.c` 351
  to 357].
* SDMMC: on esp32s31 `SDMMC_HOST_DEFAULT()` is slot 0, whose IOMUX pins are 24, 25 and 20 to
  23, as both board headers give [verified: `sdmmc_default_configs.h` 25 to 32;
  `esp_hal_sd/esp32s31/include/soc/sdmmc_pins.h`].
* FatFs on several partitions: `FF_MULTI_PARTITION` 1, `FF_FS_REENTRANT` 1, and the VFS copies
  the base path and drive string [verified: `fatfs/src/ffconf.h` 242, 343; `vfs/vfs_fat.c` 219
  to 220]. All mounting runs in app_main before the PC; afterwards only `i386_main` uses the card
  [verified: call sites; inferred: no other user].
* USB host calls otherwise: the library is installed on the task that handles its events;
  open, start and class requests are made from `usb_task`, never from inside the driver's
  callbacks; the raw report is read inside the callback, as the API intends [verified].

## Not checked, or unknown

* Whether picolibc locks the tee `FILE` per call, so that `printf` from two tasks cannot mix
  characters in `s_tee_line` [unknown].
* Whether the LCD keeps scanning the frame buffer with interrupts off in the panic handler
  (stream mode) [unknown: needs the board].
* How real keyboards, mice and receivers order their HID interfaces (how often F1 bites)
  [unknown].
* Stack high-water marks (F3) [unknown: needs the board].
