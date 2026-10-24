# The status LED

tiny386 v3.9, written 2026-10-22 for v3.3 and revised the same day after its review (`REVIEW-v32.md`,
`REVIEW-RESOLUTION.md` round 3): the bit timing (section 5), yellow 1's wording, the replay after
RESET. v3.5 changes nothing in the LED's code; only the DOS board's pin numbers in the yellow 2 and
yellow 4 rows of section 2 changed (rev A2c moved RSTN to GPIO49 and the CH9350L's RST to GPIO56).
v3.6 changes nothing in it either; the yellow 2 row now also says what the board check reads on
GPIO46. v3.7 makes the LED much dimmer, about an eighth of v3.6's light, and its brightness a
setting, `led_brightness` under `[esp]` in the ini (section 7); the colours, blinks and timing are
as before. v3.8, at Plat's asking, makes it a PC's front panel while the PC runs: a steady green, no
dark blink every 2 s any more, and a short blue flash each time the guest reads or writes a disk,
like a hard disk light (section 8); the LED's task looks every 20 ms instead of 50 for it. The
error codes, the crash pattern, the stall check (yellow 5) and the brightness are as before. v3.9
changes no colour or timing: the stall check's line now reaches the log on the panel, and section 8
says more exactly what blue and green mean (`REVIEW-RESOLUTION.md` round 5). v3.4's copy is in
`previous-v3.4\`, v3.5's in `previous-v3.5\`, v3.6's in `previous-v3.6\`, v3.7's in
`previous-v3.7\`, v3.8's in `previous-v3.8\`.

Both boards have one RGB LED on GPIO37 that the firmware drives: on the Korvo-1 the addressable "RGB
LED" (U3, a WS2812B), on the DOS board from rev A2 D201 (an Inolux IN-PI554FCH). The same code drives
both. Nothing here has been seen on a board yet: every colour below is
what the code and its host test say [verified: `esp/main/status_code.c`, `tests/host/test_status.c`],
and what the LED really looks like is [unknown] until the first boot.

Tags: [verified] read in the named source or seen in a build or test made for this note; [inferred]
follows from those; [reported] someone else's statement; [unknown] only the board can say.


## How to read it

* **A steady colour**: how far the boot has come.
* **Green with blue flickers**: the PC is running, and the blue is its disk being read or written, as
  on a PC's hard disk light (since v3.8; section 8).
* **Blinks, then a longer pause, over and over**: something is wrong. Count the blinks between two
  pauses. **Red** is the card and the files on it; **yellow** is the board.
* **Red and white by turns, four changes a second**: a crash (the red crash screen). It keeps going
  until RESET, even with the screen gone.
* **Quick white flashes right after a reset, then a code**: the last run's code, shown again because
  that run ended in a crash, a watchdog reset or a power dip (section 4).

Each blink is 0.3 s on and 0.3 s off, and a group ends with a 1.5 s dark pause, so red 3 reads "blink,
blink, blink, pause" [verified: `status_code.c`, `test_status`]. Since v3.7 the LED is dim by default:
its brightest colour, red, at 9 of 255, where v3.3 to v3.6 sent 70. `led_brightness = 0` to `255`
under `[esp]` in the ini changes that, and 0 turns it off (section 7).


## 1. The boot, by colour

| Colour | Stage | Set when | If it stays like this |
|-|-|-|-|
| dark | not started | before tiny386 runs: the bootloader, ESP-IDF's start-up, and on the DOS board the PSRAM memory test, which writes all 16 MB first | over 10 s or so: the chip never reached tiny386 (no valid image, or a hang in start-up). A PSRAM chip that is not found, or on the DOS board one that fails its memory test, gives yellow 1, not dark; other PSRAM trouble most likely ends as red 5 (section 2). The UART says more (TEST-PLAN 11) |
| white | started | the first thing tiny386 does; the PSRAM chip was found (and on the DOS board passed its memory test) | the DOS board's MS7210 check or the picture's start hangs |
| blue | screen up | the panel (Korvo-1) or the HDMI picture (DOS board) and the boot console are running | the card is being looked for |
| cyan | card mounted | the card's FAT partitions are mounted | the ini folder is being read |
| magenta | ini found | the card's tiny386 folder has at least one ini | normal while the boot menu waits (6 s, or for ever once a key stopped the countdown) |
| green | BIOS loaded | the PC is built and its BIOS loaded; the console's hold before the PC starts | 2 s, or 10 s when the console has yellow or red lines |
| green, with a short blue flash for each disk access | PC running | the PC's picture has taken the screen | this is normal running. Since v3.8 the green is steady (v3.3 to v3.7 went dark for 0.1 s every 2 s), and blue flickers in it while the guest reads or writes its disks (section 8). A steady green with no blue at all while the guest should be busy with its disk: section 8 says what that means |
| white, 1 s on and 1 s off | PC switched off | the guest switched the PC off (Windows "It is now safe...", or a soft power-off) | press RESET |

On the DOS board the Inolux LED may light blue by itself at power-on until it gets its first frame [reported:
CHANGES-A2.md section 4, from the datasheet's unclear wording]; tiny386 sends white within its first moments, so a
blue that lasts under a second at power-on is the LED's own, and a steady blue after white is "screen up".


## 2. Errors: red is the card, yellow is the board

| Pattern | What | Clears itself? | What to do |
|-|-|-|-|
| red 1 | no card can be read; tried again every 2 s | yes, when a card mounts | put in the card, FAT16 or FAT32, pushed fully in |
| red 2 | no ini file on the card (no `tiny386` folder with an ini, no `tiny386.ini` at the top), or the chosen one cannot be opened | no: tiny386 has stopped | put the files on the card, press RESET |
| red 3 | `bios.bin` or `vgabios.bin` missing, unreadable, or not a BIOS | no | copy them beside the ini (or name them in it) |
| red 4 | other problems in the ini: a disk image that is not there, one image named for two drives, `mem_size` that cannot fit, `[cpu] gen` out of range, `/mnt/win` with no second FAT partition | no | the screen lists each one in red |
| red 5 | tiny386 stopped for another reason (out of memory, a task that could not start, a disk that failed while the PC ran; a PSRAM that was found but could not be set up most likely ends here too [inferred: the emulator's memory comes from PSRAM, `esp_main.c`]) | no | the red line on the screen says which |
| yellow 1 | the chip stopped before tiny386 started: most likely the PSRAM chip not found, or (DOS board) the PSRAM failing its memory test, which is on for its first boots; any other failure in ESP-IDF's start-up looks the same. A PSRAM that is found but fails its set-up does not stop here: the start-up goes on without it [verified: ESP-IDF `cpu_start.c` lines 662 to 680 and 764 to 770; `CONFIG_SPIRAM_IGNORE_NOTFOUND` and `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` set in neither build, `CONFIG_SPIRAM_MEMTEST` in the DOS board's only], tiny386 starts (white) and stops later, most likely on red 5 [inferred] | no: the chip is halted | the UART names it (`Failed to init external RAM!` for a chip not found, `External RAM failed memory test!`) |
| yellow 2 | DOS board: the MS7210 HDMI transmitter not found at start-up (the image stops, as it does on any board that is not a DOS board), or not answering on I2C later | at start-up no; later, yes, when it answers again | the MS7210's 1.2 V and 3.3 V, RSTN (GPIO49 from rev A2c, held high by R603) and the I2C pull-ups; the UART has the details, including whether the MS7210 answered before its reset, only after it, or not at all, whether something answered at one of a Korvo-1's addresses (v3.5), and, when nothing answered at all, what GPIO46 read (v3.6: low, as R302 holds it on a DOS board, lets the reset be tried; high stops the image with RSTN never driven, so look at R302) |
| yellow 3 | DOS board: no monitor, the HDMI hot plug line is low | yes, when a monitor is plugged in | plug in or switch on the monitor; try another cable |
| yellow 4 | the keyboard path. Korvo-1: the USB host could not start (stays). DOS board: not one byte from the CH9350L 3 s after its reset (clears with the first byte) | Korvo-1 no; DOS board yes | DOS board: plug in a keyboard. In state 2 the chip is said to answer the keyboard-light frame tiny386 sends it every second [reported: research `ch9350.md` 6.5], which would clear this at once; whether a chip with nothing plugged in says anything at all is [unknown]. If it stays with a keyboard in: the CH9350L's 3.3 V, RST (GPIO56 from rev A2c) and its UART (GPIO47, GPIO55) |
| yellow 5 | the emulator has made no progress for 10 s while the PC should be running (tiny386's own loop stuck, or one card access that long; a guest that hangs does not do this, since the emulator still runs it) | yes, when it moves again | note what the PC was doing; since v3.9 the log on the panel (Ctrl+Alt+F12) has `status LED: the emulator has made no progress`, and the UART too with `[esp] uart_log = 1` (in v3.8 only the UART had it, and it is quiet while the PC runs unless the ini asks) |
| red and white by turns | a crash: the red crash screen | no: halted until RESET | photograph the screen (TEST-PLAN 10) |

Which one shows when several apply [verified: `status_code.c` `status_pattern()`, `test_status`]:

1. a crash, always;
2. for the first few seconds after a reset by a crash, a watchdog or a power dip: the replay (section 4);
3. an error tiny386 has stopped on (red 2 to 5, yellow 1, yellow 2 at start-up); the first one stays;
4. the first, in the table's order, of those that clear themselves (red 1, yellow 2 later, yellow 3, 4, 5);
5. otherwise the stage colour.

So on a DOS board with no monitor, yellow 3 shows in place of the running green, which says why
there is no picture.


## 3. The crash

On a panic the firmware, with interrupts off and before it draws the crash screen, takes GPIO37 back
from the LED driver and sends the LED its frames itself, timing each bit against the CPU's cycle
counter; when ESP-IDF has printed its report on the UART and would halt, the same loop keeps the
pattern going [verified: `crash.c`, `status_led.c`; the halt stub calls the wrapped function, seen in
the built ELF's disassembly, BUILD-NOTES.md 14]. A panic before tiny386 started (no screen yet, so the
LED is all there is) shows yellow 1 instead of red and white.


## 4. After a reset: the last run's code

The stage and the code the LED shows are kept in memory that a reset does not clear (the LP SRAM, beside
the console's log ring), with a check word so that power-on noise is never taken for a record [verified:
`test_status`: no one of 100,000 random records, and no record with any one bit changed, passes].

At the next boot, if the reset was a crash, a watchdog or a power dip, or if RESET was pressed while a
crash had the board halted, the LED first shows the following. The pattern is [verified: `test_status`];
the RESET case is [unknown] on the board: ESP-IDF reports the chip's power-on reset reason as a
power-on [verified: `esp_system/port/soc/esp32s31/reset_reason.c` line 22], which is most likely
what the RESET button gives [inferred], so only the record says the run before had crashed
[verified: `status_led.c`], and whether the LP SRAM keeps the record through the RESET button, which
pulls the chip's enable pin low, is not known until it is tried (TEST-PLAN 10 asks):

* white flashes for the cause: **1** a crash, **2** a watchdog (or a CPU lock-up), **3** a brownout or
  a power glitch; then a dark 0.6 s;
* then the last run's code twice (for example yellow 3 twice), or, if it showed none, the colour of the
  stage it had reached for 2 s;
* then the present boot as usual. The boot carries on underneath; the replay takes about 3 to 10 s.

The boot console says the same in words (`Status LED: first N white flash(es) ... then that run's code`),
beside its own `The last run ended with ...` lines. After a normal RESET, a power-on, or a RESET from a
stopped run (red 2 to 5), there is no replay. On the Korvo-1's dual boot a record can outlive a run of
Linux in between [inferred: unless Linux writes that spot of LP SRAM], so a crash, then Linux, then
tiny386 again replays that crash once; the console's own `The last run ended with` lines come from the
same memory and behave the same way [inferred: `console.c`].


## 5. What the LED gets on the wire

Both LEDs take the same timing (v3.4; v3.3's T1H was 662.5 ns in a 1.3 us bit) [verified:
`test_status`, every bit of the RMT frame and of the bit-banged one against both datasheets, sent and,
for the high times, at the LED]:

| | Sent | At the LED (about) | Inolux IN-PI554FCH (datasheet v2.2 p. 11) | Worldsemi WS2812B-2020-V6 (p. 5) | Older WS2812B [reported] |
|-|-|-|-|-|-|
| 0: high, low | 337.5 ns, 962.5 ns | 300 ns, 1000 ns | 0.3, 0.9 us, each +-0.15 us | 220 to 380 ns, 580 to 1000 ns | 0.4, 0.85 us, each +-0.15 us |
| 1: high, low | 700 ns, 625 ns | 662 ns, 663 ns | 0.6, 0.6 us, each +-0.15 us | 580 to 1000 ns each | 0.8, 0.45 us, each +-0.15 us |
| a bit | 1.3 us (0), 1.325 us (1) | the same | 1.25 us +-0.6 us | 1.25 us or more | not checked here |
| reset (low) | 300 us before every frame | the same | 80 us or more | 280 us or more | 50 us or more |

Order green, red, blue, most significant bit first. The data line rests low between frames (a high
rest would read as the start of a bit), so about 5 mA flows from 5 V through the 1k into GPIO37 while
it rests [inferred: CHANGES-A2.md section 3]. The Korvo-1's WS2812B is a Worldsemi part of a revision
not known here [unknown]; the WS2812B-2020-V6 datasheet is the nearest one to hand, and the older
WS2812B column is the older Worldsemi WS2812B datasheet's table as others give it, not read at
Worldsemi [reported: the v3.3 review, `REVIEW-v32.md` F2, from a components101 summary (T1H and T1L);
a web search's summary of third-party copies of that datasheet (all four times, each +-150 ns, reset
above 50 us), 2026-10-22].

The 5 V level shifter (a BSS138 and a 1k pull-up on both boards) slows each rise at the LED by about
38 ns [inferred: CHANGES-A2.md section 4, an estimate from the pull-up and the line's capacitance], so
the LED sees each high time that much shorter and each low time that much longer: the "At the LED"
column. A WS2812-type LED tells a 0 from a 1 by the high time, so the high times are set inside every
window above both as sent and at the LED (T1H 700 ns is about 662 ns at the LED: 12 ns over the older
WS2812B's 650 ns floor, the least margin anywhere in the RMT frame [verified: `test_status`]). The low
times are inside both datasheets' windows as sent; at the LED, T0L comes to about the WS2812B-2020's
1 us maximum, and T1L to more than the older WS2812B's 0.6 us. Those maxima are left as they are: a low
time decides nothing in these parts as long as it stays far under the reset time [inferred], and no
0 bit of 1.25 us or more fits every window at the LED for its low as well as its high once the
bit-bang's jitter is counted [inferred: the arithmetic in `BUILD-NOTES.md` 15.3].

In normal running the RMT peripheral sends each frame (80 MHz, 12.5 ns steps); a task on core 0 looks
every 20 ms (50 ms before v3.8) and sends when the colour changes, and once a second anyway. In a crash the CPU sends them
(section 3): each wait is a three-instruction loop on the cycle counter, and since v3.4 each store
comes straight after its wait, for the rise as for the fall, so a high or low time comes out as set
give or take one pass round the loop: 9.4 ns at 320 MHz, 37.5 ns at the 80 MHz the bootloader leaves
the CPU at before tiny386 starts [verified: the built code, `checks\status-led-panic-path-v3.4.txt`,
which `build.sh` now checks in every image; `CONFIG_BOOTLOADER_CPU_CLK_FREQ_MHZ 80`]. In v3.3 the rise
came nine or ten instructions after its wait and the fall two, so every high time came out 7 or 8
cycles short: at 80 MHz a 1 was about 525 to 562 ns, under the WS2812B-2020's 580 ns, so yellow 1 for
an early PSRAM failure might not have shown as yellow [verified: objdump of the v3.3 images, the v3.3
review F1; that the LED would misread it is inferred].

A scope on the DOS board's TP948 (`LED_DIN`, the LED's 5 V data line), or on the Korvo-1's LED data
pin, shows all of this. TEST-PLAN 10b has the bench check for the 80 MHz case: a crash-test build with
`crash_test = 5`, which sets the CPU to 80 MHz and crashes as before tiny386 started (yellow 1).


## 6. Not done: an LP-core monitor

The S31's low-power core could watch the main cores, but GPIO37 is not one of its own pins (those are
GPIO0 to 7) and the indirect ways to reach it are untried [unknown], and resetting a stuck chip is what
the watchdogs already do. So the LED is driven from the main
cores only, and the one gap the watchdogs leave, the emulator's own loop stuck while the chip runs, is
yellow 5. `..\research\status-led-lp-core.md` has the reasons and what an LP monitor would take.


## 7. Brightness (v3.7)

v3.3 to v3.6 sent every colour at about a quarter of full, red at 70 of 255, and on Plat's Korvo-1
that is far too bright [reported: Plat, 2026-10-22, with v3.6]. Since v3.7 the colour table is at full
scale, the same colours multiplied together by 255/70 so that red is at 255 and each colour keeps its
hue and its share of light against the others, and every frame is scaled by one brightness level, 0
to 255, on its way to the LED: in normal running and in a crash alike, through the same function
[verified: `status_code.c`, `status_led.c`, `test_status`]. The default level is 9:

| Colour | v3.6 (red, green, blue) | v3.7 table (level 255) | v3.7 default (level 9) | Of v3.6's light |
|-|-|-|-|-|
| white | 40, 40, 40 | 146, 146, 146 | 5, 5, 5 | 0.125 |
| blue | 0, 0, 60 | 0, 0, 219 | 0, 0, 8 | 0.133 |
| cyan | 0, 45, 45 | 0, 164, 164 | 0, 6, 6 | 0.133 |
| magenta | 50, 0, 50 | 182, 0, 182 | 6, 0, 6 | 0.120 |
| green | 0, 50, 0 | 0, 182, 0 | 0, 6, 0 | 0.120 |
| red | 70, 0, 0 | 255, 0, 0 | 9, 0, 0 | 0.129 |
| yellow | 60, 35, 0 | 219, 128, 0 | 8, 5, 0 | 0.137 |

"Of v3.6's light" is the sum of the three channels against v3.6's sum, which is what the LED's
three PWM duties add up to: about an eighth for every colour [verified: `test_status`]. Whether that
is comfortable to look at, and whether yellow at 8:5 still reads plainly as yellow and not orange on
the Korvo-1's WS2812B and on the DOS board's Inolux LED, is [unknown] until Plat sees it.

How a level is applied: each channel becomes its table value times level / 255, to the nearest step,
except that a channel that is lit in the table never drops to 0 at a level above 0. So at every level
from 1 to 255 every colour keeps its lit channels and stays unlike the others (yellow never turns
red, white never magenta), and from the default up the ratio of any two lit channels of a colour stays
within 14.1 per cent of the table's (the most off: yellow at level 11) [verified: `test_status`, every
channel value at every level against a model worked out another way]. Level 255 is the table, about
3.6 times v3.6's light; level 70 gives v3.6's colours exactly [verified: `test_status`].

### Setting it

In the ini the PC starts from (`tiny386.ini`, or the one picked in the boot menu), under `[esp]`:

```
[esp]
led_brightness = 20
```

* 0 to 255, a whole number. **0 is dark**, a crash's red and white included, so the LED then says
  nothing at all, even after a crash. 70 is v3.6's brightness.
* A number outside 0 to 255 is held to the nearer end, and a value that is not a number is ignored;
  either gives a yellow line on the boot console, which then stays up 10 s [verified: `esp_main.c`].
* It takes effect when its line is read, and the LED shows it within 20 ms [inferred: the LED's task
  looks every 20 ms since v3.8 and sends at once when the level has changed, `status_led.c`]. Until the ini is
  read the default holds: the first white, the replay after a reset (section 4), cyan and magenta while
  the boot menu waits, and a crash before the ini is read. Nothing keeps the level over a reset; the
  next boot starts at the default again [verified: `status_code.c`, the level is in ordinary RAM that
  the image sets to 9].
* The boot console's first LED line gives the level as it starts: `Status LED on GPIO37: colours and
  blinks as in STATUS-LED.md, brightness 9 of 255 ([esp] led_brightness).` The ini's own line
  (`[esp] led_brightness = 20`) appears among its settings, and with 0 also `Status LED: dark
  (led_brightness = 0), a crash included.` [verified: `status_led.c`, `esp_main.c`].
* On the DOS board the same line in the same ini sets D201. `dosboard.ini` reads only its own
  `[dosboard]` settings, so a `led_brightness` line there does nothing [verified: `dosboard.c`
  `parse_cfg()`].
* `card\tiny386\tiny386.ini` and `win95-14m.ini` carry the line with a `;` in front (`;led_brightness =
  9`); take the `;` off and change the number.

The crash path (section 3) scales with the same function and the same level as normal running, from
internal RAM with interrupts off, and in 32 bits only [verified: `test_status`, which bit-bangs the
crash's frames and sends them through the RMT at five levels and reads both back; the built code,
`checks\status-led-brightness-v3.7.txt`].


## 8. The disk light (v3.8)

Plat asked for the LED to behave like a PC's front panel while the PC runs [reported: Plat,
2026-10-22]: a steady power light and a hard disk light. So, from the moment the PC's picture takes
the panel:

* **Green, steady.** v3.3 to v3.7 went dark for 0.1 s every 2 s; that blink is gone [verified:
  `status_code.c`, test 15].
* **Blue for a moment each time the guest reads or writes a disk**: a hard disk image, a CD image
  or a floppy image, a read or a write alike. The blue is the screen stage's blue, at the same
  brightness (at the default, 9: blue 0, 0, 8 against green 0, 6, 0) [verified: `status_code.c`,
  test 15].

How it is timed [verified: `status_led.c`, `status_code.c`, test 15, which runs the LED task's
looks against pretend accesses]: the LED's task looks every 20 ms. Each look asks whether the
guest has made any disk access since the look before; if so, that look is blue, if not, green. So
one access lights the LED blue for one look, 20 ms; accesses less than 20 ms apart keep it blue for
as long as they go on; accesses further apart give one flash each with green between. That is how
a PC's hard disk light flickers: lit while the disk is busy, dark between [inferred: a real light
follows the drive's activity line directly]. Whether 20 ms flashes are easy to see at the default
brightness is [unknown] until the board shows it; `led_brightness` makes them brighter.

What counts as an access [verified: `ide.c`, `misc.c`, tests 14 and 15 and `test_floppy`]: every read
and every write the guest asks of a hard disk or CD image through the IDE controller, one the
emulator refuses (past the image's end) included, and every floppy read or write that reaches the
floppy image. Each is counted by one store in the emulator's code and nothing more; the counting
costs the emulator nothing it would notice [inferred: one add and one store an access, against the
card read or write that follows it].

What it does not change:

* **The errors and the crash**: a code that shows (yellow 3, no monitor, on the DOS board, or any
  other) goes over the green and the blue alike, as it went over the old green; the crash pattern
  and the replay after a reset are as before [verified: test 15].
* **The stall check.** Yellow 5 never came from the old blink: the LED's task watches a count the
  emulator moves every step, and if it stands still for 10 s while the PC should be running the LED
  shows yellow 5. That is unchanged, so a stopped emulator still shows: steady green, then yellow 5
  after 10 s; a stopped emulator also makes no disk accesses, so the blue stops with it [verified:
  test 15, a pretend emulator stopped at 4 s gives yellow 5 at 14 s].
* **The brightness**: one level for every colour, the blue included (section 7).

Reading it (corrected in v3.9 after the board-safety review of v3.8, D2): the light marks each
access as the guest asks for it, before the card is reached, and the card access then runs on the
emulator's own core [verified: `BF_ACTIVITY()` is the first statement of `bf_read_async()` and
`bf_write_async()` in `ide.c`]. So:

* Green with blue flickers while Windows or DOS starts, loads a program or swaps, then mostly green
  when the guest sits idle: normal.
* Steady blue is many accesses close together: a long stretch of disk work (a first boot of
  Windows 95 has a great deal), or a guest retrying an access the card keeps refusing. The `Disk:`
  line of Ctrl+Alt+F12 tells these apart: a retry loop shows card errors there.
* One short blue flash, then green for seconds, can be a single access waiting a long time for the
  card: during that wait the emulator does nothing else, so no more accesses come. If the wait
  passes 10 s the stall check shows yellow 5 [inferred: the stall check below].
* Green with no blue at all for a long time while the guest should be loading something: either the
  guest is not reaching its disk (the emulator runs and the guest waits on something else), or one
  card access is taking that long. The `Disk:` line's longest access and its count of accesses over
  55 ms say which (`TEST-PLAN.md` 8).
* A steady colour that never changes while the panel's picture is frozen too means core 0 has
  stopped scheduling the LED's task. The dark blink every 2 s of v3.3 to v3.7 came from the LED
  task's own clock, so its stopping showed that; the steady green cannot. A core 0 stuck with its
  interrupts off is still caught by the 800 ms interrupt watchdog and shows the crash pattern; one
  starved by a busy task is not [inferred: `CONFIG_ESP_INT_WDT_TIMEOUT_MS`, the board-safety review's
  note D1].

Since v3.9 the stall check's own line, `status LED: the emulator has made no progress for 10 s
(yellow 5)`, and `status LED: the emulator goes on again` go to the log on the panel (Ctrl+Alt+F12)
and its ring, and to the UART when `[esp] uart_log = 1`; in v3.8 they went to the UART alone, so with
the default `uart_log = 0` nothing recorded them [verified: `status_led.c`, test 15].

The cost on the LED's side: 50 looks a second instead of 20, each a few microseconds on core 0, and
a frame (about 0.33 ms of the RMT peripheral's time, not the CPU's) only when the colour changes
[inferred: `status_led.c`; the frame's length from section 5].
