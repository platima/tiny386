# Review of firmware v3.5: rev A2c pins, the speakers, the mix and the Korvo-1 images

An independent review, 2026-10-22, of tag `v3.5` (commit 07e7784, tree c80d5d8b) against `v3.4` in WSL
`~/tiny386-idf-scratch/v3/tiny386`, and of the package in this folder. Nothing was flashed, no serial
port was opened, nothing has run on a board, and no process of mine is still running. I read the
worker's tree and build folders and wrote nothing in them; my scripts, exports and outputs are in WSL
`~/tiny386-idf-scratch/v35-review/` and in this session's scratchpad folder `v35-review`.

Tags: [verified] read in the named source or seen in a check I ran for this review; [inferred]
follows from those; [reported] someone else's statement, not checked; [unknown] only a board can say.


## Verdict

No blocker and no major finding. Every function in `board_dosboard.h` is on the right net of the
final netlist, the firmware configures none of the J202 header's GPIOs but the shared I2C pair, the
mute order is right in every state the firmware controls, the new mix cannot wrap or overflow from any
source, the new default is the loudest whole step that cannot clip, and the Korvo-1 images are v3.4's
with only the version character, the ELF hash in the app descriptor and the image checksum and hash
changed. Three minor points and four notes follow; none stops Plat building or trying v3.5.

| | Severity | Where | In short |
|-|-|-|-|
| F1 | minor (board) | R703, net AUD_XSMT | the pull-up keeps the speaker amplifier on from power-on to the board check and through every reset; a 10k pull-down would close the last unmuted edges |
| F2 | minor (docs) | `README.md` 96, `FLASH.txt` 210, `board_dosboard.h` 17 to 20 | "no pin but I2C before the MS7210 answers" is not true when nothing at all answers |
| F3 | minor (optional) | `dosboard.c` `board_early_init()` | a pin strap (GPIO46, R302 1k) would separate the boards without I2C and remove both corner cases |
| N1 | note | `audio_mix.c`, `audio_mix.h`, `audio_mix_test.c` | committed with mode 100755 |
| N2 | note | `audio_mix.c` 58 to 64 | no guard for n of 0 or less |
| N3 | note | `i2s.c` `i2s_task` | stack measured: about 960 bytes deep on the mix path, 384 more than v3.4 |
| N4 | note | `TEST-PLAN.md` | the Korvo-1 guard can be tried through the dual boot's test slot, Linux kept |


## Findings

### F1 (minor, board): R703 pulls AUD_XSMT up, so the speakers are live through every reset

R703 (10k) runs from the DAC's rail `/Audio_option/AUD_3V3` to `AUD_XSMT` [verified: `dosboard.net`
lines 7595 and 7831], and AUD_XSMT also drives the TPA2012D2's SDL and SDR (U702 pins 7 and 8)
[verified: my netlist dump of U702]. GPIO48 has no pull at reset [reported: CHANGES-A2.md section 3,
from the chip data sheet]. So from power-on until `board_early_init()` drives GPIO48 low, and again
from the moment any reset starts (the RESET key, a brownout, esptool's DTR and RTS reset when
flashing), the amplifier is enabled and the DAC unmuted while the I2S clocks stop or have not started.
After a crash the panic path has already muted and stopped the clocks, but the amplifier comes back on
through the reboot all the same. TI asks for XSMT low 150 sample times plus 0.2 ms before the clocks
or power go (TI SLAS859C 11.2.1, as `i2s.c` cites); a reset cannot give that. Whether it is audible is
[unknown]; the worker's `LINE-OUT.md` (lines 98 to 103) already raises it.

The firmware is right as it stands: it drives GPIO48 low at the board check, high 20 ms after the I2S
clocks start, and low again in the panic handler, and it never leaves the pin to R703 while running
[verified: `dosboard.c` 412 to 415, `i2s.c` 58 to 84 and 117 to 124; the built `audio_panic_quiet` in
`images\dosboard\tiny386.elf` stores 0 to GPIO48 through `rom_gpio_set_output_level`, waits 4000 us,
then clears the I2S TX start bit]. Only the board can close the reset edges:

* Fit R703 as 10k to GND instead of to AUD_3V3. Worst case at rest, with GPIO48 undriven: the
  PCM5102A's XSMT input current is at most 10 uA (TI SLAS859C p. 7, IIL) and each TPA2012D2
  shutdown pin at most 5 uA (TI SLOS438F p. 5, IIL), so 20 uA into 10k is 0.2 V, under the
  TPA2012D2's VIL of 0.35 V (p. 4) and the PCM5102A's 0.3 x DVDD [verified: the data sheet text;
  inferred: the sum]. Driven high, GPIO48 sources 0.33 mA into it [inferred]. No firmware change.
* Leaving R703 off is not enough by the data sheets: the line would rest on the TPA2012D2's two
  internal 300k pull-downs (150k together) against up to 10 uA from the PCM5102A, up to 1.5 V in the
  worst case, above 0.35 V [inferred from the limits; typical leakage is far lower, unknown].
* The price: any firmware that does not drive GPIO48 high (the Linux route, someone's own ESP-IDF
  program) gets neither line out nor speakers. `LINE-OUT.md` line 100 puts it as "a line out that
  plays only with the firmware running"; with no firmware there are no I2S clocks and so no sound
  anyway, so the pull-up only ever helps firmware that does not know GPIO48 [inferred].

This is Plat's call for the board step; nothing in v3.5 needs to change either way.

### F2 (minor, docs): the "no pin before the MS7210 answers" claim misses the fallback

`board_early_init()` drives GPIO49 before any MS7210 has answered when nothing answers at 0x59 or
0x2B and none of 0x10, 0x11, 0x14, 0x5D answers either: it sets RSTN high, makes it an output and
runs the probe with its reset pulse (about 70 ms), and on a Korvo-1 GPIO49 is camera D3 [verified:
`dosboard.c` 388 to 409; research `s31-module.md`, the pin table: 49 is camera D3]. `dosboard.c`'s own
comments, `BUILD-NOTES.md` 16.3 step 4 and the worker's risk list say so correctly. Three places say
otherwise:

* `README.md` line 96: "Before the MS7210 has answered, the image drives no pin but the I2C bus".
* `FLASH.txt` line 210: "Since v3.5 it drives no pin but the I2C bus before the transmitter has
  answered".
* `board_dosboard.h` lines 17 to 20: "it looks for the MS7210's chip ID at I2C 0x59 before driving any
  pin that is new on this board. GPIO49 to GPIO57 are the Korvo-1's camera inputs, so none of them is
  driven before that."

Suggested wording: "drives no pin but the I2C bus until the MS7210, or one of a Korvo-1's own I2C
devices, has answered; if nothing at all answers it pulses RSTN (GPIO49) once and probes again".
The contention risk itself is small: the DOS image never sets the Korvo-1's camera up, so its data
outputs should be idle [inferred: OmniVision sensors keep their pads disabled until told; not checked
for the OV3660].

### F3 (minor, optional): a pin strap would separate the boards without I2C

The board check now leans on which I2C addresses answer. That leaves the two corner cases the worker
listed: a header device at 0x10, 0x11, 0x14 or 0x5D (a common LPS22 or LPS25 pressure sensor sits at
0x5D) stops a real DOS board whose MS7210 only answers after its reset, and a Korvo-1 with nothing on
its bus gets GPIO49 pulsed (F2). A static strap would settle both: GPIO46 is `KM_S0` on the DOS board
with R302 1k to GND [verified: netlist, R302 between KM_S0 and GND], and camera D0 on the Korvo-1, an
input the camera alone drives [verified: `s31-module.md` pin table]. Read with its internal pull-up on,
it reads low on a DOS board (1k against the weak pull-up, about 0.07 V [inferred]) and, with the camera
not set up, high on a Korvo-1 [inferred]. The CH9350L samples S0 only at its own reset, and R303 (10k
to GND) keeps it out of reset meanwhile [verified: `ch9350.c` 456 to 457 comment; netlist R303], so a
weak pull-up for a few microseconds changes nothing there [inferred]. Use it only to gate the
fallback (GPIO46 high: stop without touching GPIO49) and to overrule a Korvo-1 address hit (GPIO46
low: carry on to the reset probe). Untested; optional.


## Notes

### N1: new source files committed executable

`git diff` from v3.4 to v3.5, with its summary option, shows `create mode 100755` for `esp/main/audio_mix.c`,
`esp/main/audio_mix.h` and `tests/host/dosboard/audio_mix_test.c` (and `check_pins.py`, which is a
script and may stay so); every other C source in `esp/main` is 100644 [verified]. Probably copied in
from a Windows folder. A `chmod 644` in the scratch tree and a regenerated patch tidies it; the bytes of
every image are unaffected.

### N2: `audio_mix_devices()` with n of 0 or less

`n &= ~1` keeps a negative n negative, and `memset(opl, 0, n)` would then take it as a huge size_t
[verified: `audio_mix.c` lines 58 to 64; gcc's sign-conversion warning on lines 64, 65 and 69]. Not
reachable: `i2s.c` passes the constant `MIXER_BUF_LEN` (128) under a `_Static_assert`. `if (n <= 0)
return 0;` would make the function safe on its own.

### N3: the sound task's stack, measured

I recompiled the sound path with the DOS board build's own command lines plus `-fstack-usage`, into my
folder [verified: `~/tiny386-idf-scratch/v35-review/su/*.su`, `su34/`]:

| Frame | v3.4 | v3.5 |
|-|-|-|
| `i2s_task` | 304 bytes | 352 bytes |
| `mixer_callback` (pc.c) / `audio_mix_devices` | 160 | 496 |
| `audio_mix_pc`, `audio_mix_block`, `adlib_callback` | | 0 (leaf or tail call) |
| `YM3812UpdateOne` (fmopl.c, the deepest device) | 112 | 112 |

So the mix path is about 960 bytes deep plus whatever `i2s_channel_write` takes, 384 bytes more than
v3.4 (the worker estimated about 450), in a 4096-byte task [verified: frames; inferred: the sum]. The
UART's 10 s `stacks, least free since start` line already lists `i2s_task` [verified: `display.c` line
42], so the first board run will show the real margin.

### N4: the Korvo-1 guard can be tried without losing Linux

`TEST-PLAN.md` has no step that runs the DOS image on a Korvo-1, and the worker marks the guard as
never tried. The dual boot gives a cheap way: `images\dosboard\tiny386.bin` (916,576 bytes) fits the
Korvo-1's test slot at 0xE20000 (0x1E0000 bytes) [verified: `partitions-korvo1-dualboot.csv`, image
size], and the board check is the first thing `app_main()` does after the status LED [verified:
`esp_main.c` 965 to 976]. Written there and started with VOL+ held at reset, it should halt with the
message naming the Korvo-1 device that answered and yellow 2 on the LED, with GPIO49 untouched; a
plain reset goes back to Linux, and `images\korvo1-dualboot\tiny386.bin` goes back into the slot
afterwards. That the dual boot's bootloader starts the DOS app from that slot is [inferred]. It writes
flash, so it is Plat's choice.


## What I checked and found right

1. **Pins against the final netlist.** My own S-expression parser (not the worker's `check_pins.py`)
   over `tiny386-hdmi-board\dosboard\out\dosboard.net` (rev A2c, 12:02) lists every module pad with
   its net and the parts on it [verified: `~/tiny386-idf-scratch/v35-review/net.py`]. All 43 pins in
   `board_dosboard.h` are on the right nets: I2C SDA 0 and SCL 1 (U601 SDA, SCL, J202 5 and 6), RSTN
   49 (U601.33, R603 27k up), INT 54 (U601.10, R604 27k up), I2S BCLK 51, WS 52, DOUT 53 (U601 and
   U701 BCK, LRCK, DIN), AUD_XSMT 48 (U701.17, U702.7, U702.8, R703), KM RX 47 from U301 TXD, TX 55 to
   U301 RXD, RST 56 (U301.3, R303 10k down), S0 46 (U301.4, R302 1k down), USB_VBUS_EN 38, LED 37
   (Q201), USER 42, SD power 39 and detect 50, SD 20 to 25, PCLK 40 through R209, DE 43, HSYNC 44,
   VSYNC 45, LCD_D0 to D15 on 8 to 19 and 33 to 36 in order [verified]. Every pin the firmware drives
   goes to an input or a pull resistor, never to another output [verified: the same dump]. The
   worker's test 17 also passes in my own export of the tag [verified].
2. **Nothing driven on the header.** J202 carries GPIO0 to 7, 57 and 60 [verified: netlist]. No file
   in `esp/main` that the DOS board build compiles configures any of 2 to 7, 57 or 60 [verified: grep
   of every `gpio_*`, `*_io_num`, `*_gpio_num` and `GPIO_NUM_` use; `lcd_axs15231b.c` and the other
   boards' drivers are not in this build]; the S31's default SD slot leaves D4 to D7 unconnected and
   ESP-IDF configures them only at 8-bit width [verified: ESP-IDF `sdmmc_default_configs.h` 154 to 170,
   `sd_host_sdmmc.c` 389 to 398]; UART1 sets no RTS or CTS pin [verified: `ch9350.c` 487 to 488]; the
   console is UART0's fixed pins and Wi-Fi is compiled out (`NO_WIFI`) [verified: the build's
   `sdkconfig.h` and compile commands]. That ESP-IDF's start-up leaves them alone is [inferred].
3. **The board check's logic.** First probe with `io_rstn()` gated by `s_rstn_ours`, so the probe's
   reset row runs its waits but drives nothing; `ms7210_probe()` clears its state, `errors` included,
   on every call; an answer at 0x2B the first time is found again after the reset; something else at
   0x59 still stops at once; a stuck bus falls through to the reset probe, which frees it; the halt
   messages fit their 96-byte buffer [verified: `dosboard.c` 315 to 423, `ms7210.c` 399 to 415].
   `globals.pc` is set once before BIT0 and never replaced, so `i2s_task` may take it once
   [verified: `esp_main.c` 447 to 484].
4. **Mute order.** GPIO48 is set low before it is made an output (no high glitch), in the board check
   and again in `i2s_main()`; it goes high only after 20 ms of running clocks, measured from
   `i2s_channel_enable()`; the panic path mutes first, waits 4 ms, then stops TX, from IRAM [verified:
   source and the built ELF]. `auto_clear_after_cb` does what the comment says: the TX interrupt
   clears each sent buffer before handing it back [verified: ESP-IDF `i2s_common.c` 937 to 940]. No
   `esp_restart()` exists in the firmware, so there is no software reset that skips the mute
   [verified: grep].
5. **The mix: no wrap, no overflow.** `audio_mix_devices()` asks the three devices exactly as `pc.c`'s
   `mixer_callback()` did for `free` = 256: AdLib 128 bytes, SB16 256 bytes, PC speaker 64 bytes, in
   the same order, each buffer pre-filled the same way [verified: `pc.c` 901 to 930 against
   `audio_mix.c` 50 to 74, and my stub run]. Every source reaches the mix as int16 or uint8: fmopl
   limits its output, and the SB16's U8 and U16 conversions land inside int16 [verified: `sb16.c` 1384
   to 1460]. My own exhaustive check of `audio_mix_block()` against a 64-bit model (every sum from
   -65,536 to 65,534, the PC speaker at both ends and at every value for one sum in 97, shifts -2 to
   18 to cover the clamping) made 50,844,867 comparisons with no difference and no wrong held count,
   under ASan and UBSan [verified: `~/tiny386-idf-scratch/v35-review/mixcheck.c`]. The 32-bit sum
   lies within -69,632 to 69,598 and the rounding term at shift 15 cannot overflow [verified].
6. **The default level.** At shift 2 the extreme sums give 17,399 and -17,408 with nothing held; at 1
   they are held [verified: my check]. The DOS image's initial `i2s_audio_shift` is 2, so the default
   holds even with no card, where `dosboard_load_config()` returns before copying its settings
   [verified: `.dram0.data` of `images\dosboard\tiny386.elf`; `dosboard.c` 531 to 566]. The
   amplifier cannot clip at 2: at most 0.531 of 2.97 V peak, doubled, is 3.15 V peak against about 4.7
   V [inferred: TI figures as `LINE-OUT.md` gives them; the 470 ohm and 10k network shaves about 0.5 dB
   more]. I agree 2 is the right default for speakers on 5 V; 1 would clip the amplifier on loud mixes.
7. **The Korvo-1 images.** For both Korvo-1 sets, `bootloader.bin`, `partition-table.bin` and
   `flash_args` are byte for byte v3.4's; `tiny386.bin` is the same length and differs in 66 bytes in
   three runs: the version character in the app descriptor, its 32-byte ELF hash, and the checksum
   byte with the appended SHA-256; in the ELF only `.flash.appdesc` differs among the loadable
   sections (`.debug_info` and `.debug_line` differ too) [verified:
   `~/tiny386-idf-scratch/v35-review/imgcmp.py`]. The flash scripts, `flash-paths.cmd`, the card files
   and the review files hash as in v3.4's SHA256SUMS [verified: diff of the two SHA256SUMS]. The
   flash simulation and the stub harness differ from v3.4's only in version and hash lines [verified:
   diff of the v3.4 and v3.5 check files].
8. **Reproducibility and the patch.** The four from-nothing image sets and the shipped one are
   identical [verified: one hash over each set's 18 files]. `tiny386-idf-v3.patch` applied to tiny386
   9ed786b gives tree c80d5d8b, the tag's [verified: `git write-tree` in my own copy]. `SHA256SUMS`
   verifies, 40 of 40, and each image folder's own list verifies [verified]. The build logs carry only
   the old `i2s_task` unused warning, on the two Korvo-1 builds [verified: grep of the logs].
9. **Host tests from the tag.** In a `git archive` export of v3.5 (no git, so the two pristine checks
   skip): 24 passed, 0 failed, tests 16 and 17 included [verified:
   `~/tiny386-idf-scratch/v35-review/host-tests-review.log`].
10. **Docs.** The changed docs and `dosboard.ini` have no em dash, en dash or double hyphen [verified:
    grep], and their pin numbers and levels match the code (but F2).

The working tree's `esp/dependencies.lock` shows as modified: it is the S31 build's lock, the same as
this folder's `dependencies.lock`, against upstream's committed esp32s3 one; it was so before v3.5
and touches nothing shipped [verified: cmp]. `SHA256SUMS` does not list this file.
