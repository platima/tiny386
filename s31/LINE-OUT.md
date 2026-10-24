# The DOS board's line out and speakers

tiny386 v3.5, written 2026-10-22 for the DOS board rev A2c, where the PCM5102A DAC, its 3.5 mm jack
and now a TPA2012D2 speaker amplifier are fitted (`..\..\tiny386-hdmi-board\dosboard\out\CHANGES-A2.md`
sections 5, 19 and 20). v3.3 and v3.4 wrote this note for rev A2's line out alone; v3.4's copy is in
`previous-v3.4\`. v3.6 (after the review of v3.5) rewrote the board point on R703 below and changed
nothing else; v3.5's copy is in `previous-v3.5\`. Nothing here has been heard: the board does not
exist yet.

Tags: [verified] read in the named source or seen in a build or test made for this note; [inferred]
follows from those; [reported] someone else's statement; [unknown] only the board can say. "TI DAC" is
the PCM5102A datasheet SLAS859C (`..\..\tiny386-hdmi-board\research\dl-parts\ti-pcm5102a\pcm5102a.pdf`);
"TI amp" is the TPA2012D2 datasheet SLOS438F, read as text in the board step's scratch (WSL
`~/dosboard-a2b-delta/work/txt/tpa2012d2.txt`).


## What is on the board [verified: rev A2c netlist `out\dosboard.net`, CHANGES-A2.md 19 and 20]

* U701, a PCM5102A, on the same three I2S lines as the MS7210, which rev A2c moved off GPIO3 to GPIO5:
  BCK on GPIO51 (`I2S_BCLK`, TP937), LRCK on GPIO52 (`I2S_WS`, TP938), DIN on GPIO53 (`I2S_DOUT`,
  TP939). SCK, FMT, FLT and DEMP are tied to ground: no master clock (the DAC makes its own from BCK),
  standard I2S, the normal filter, no de-emphasis.
* `AUD_XSMT` on GPIO48 (TP949), with R703 10k up to the DAC's 3.3 V: the DAC's soft mute (XSMT) and,
  from rev A2c, the speaker amplifier's two shutdown pins (SDL, SDR) as well. Low: DAC muted, speakers
  off. High: both play.
* Each DAC output through 470 ohm and 2.2 nF to ground, TI's own filter, to J701.
* J701, now a Same Sky SJ1-3525N: 3.5 mm, three poles, through hole, fitted by Plat, with a switch
  contact on tip and on ring. With no plug in, the switches pass the line out on to the amplifier; a
  plug lifts them, and R706 and R707 (10k) hold the amplifier's inputs at ground, so **the speakers go
  quiet whenever headphones or a line lead are in the jack**, with no firmware involved.
* U702, a TI TPA2012D2: stereo, filter-free class D, from +5V, gain 6 dB (G0 and G1 to ground), fed
  single-ended through 470 ohm and 2.2 nF, then 33 nF (a 172 Hz high-pass with its 28.1 kohm input).
  Its outputs go through a ferrite and 1 nF on each line to J703, a Molex PicoBlade 1.25 mm 4-pin
  header for Waveshare's pair of 8 ohm 2 W speakers: pin 1 left +, 2 left -, 3 right -, 4 right +.
  Whether Waveshare's "PH1.25" plug mates with it is still [unknown] (CHANGES-A2.md 23).


## What it gives

* **Line level at J701**, not headphones. Full scale is 2.1 V RMS for a sine (2.97 V peak), centred on
  ground, into 1 kohm or more [verified: TI DAC p. 1, p. 4, p. 6]. Powered speakers, an amplifier's or
  a mixer's line in, a PC's line in. Ordinary 32 ohm headphones are far below 1 kohm: expect them quiet
  [inferred].
* **The speakers** play the same signal with 6 dB of gain, up to about 1.4 W a channel into 8 ohm
  (10 % THD at 5 V), that is about 3.35 V RMS or 4.7 V peak [verified: CHANGES-A2.md 20, from TI amp;
  inferred: the peak]. The DAC's full scale times two (5.9 V peak) would take it past that. The
  Waveshare speakers are rated 2 W, so the amplifier cannot overdrive them [inferred: CHANGES-A2.md 19].
* **The same sound as HDMI**, at 48 kHz, 16-bit stereo: one I2S stream feeds the MS7210 and the DAC, so
  HDMI, the line out and the speakers play the same thing at the same level [verified: `i2s.c`,
  `board_dosboard.h`].
* `hdmi_audio = 0` in `dosboard.ini` turns HDMI's sound off only [verified: `dosboard.c`].
* `i2s_slot_bits = 32` suits the DAC as well: BCK 3.072 MHz against 1.536 MHz [verified: TI DAC p. 25
  table 11, p. 15].


## The level (v3.5)

The firmware mixes the SB16, the AdLib and the PC speaker, then divides by 2 to the power `audio_shift`
(`dosboard.ini`, `[dosboard] audio_shift`, 0 to 8). Since v3.5 the three are added in 32 bits and held
to 16 bits only after that division, so sources that add up past full scale are held at full scale,
never wrapped round, and at 1/4 or quieter cannot be held at all [verified: `esp/main/audio_mix.c`,
host test 16]. v3.4, like tiny386 itself, held the sum to 16 bits before dividing, so two loud sources
were clipped even at a low level.

| `audio_shift` | Level | A lone source, full-scale sine: J701; speakers [inferred] | The three devices at their loudest together | Notes |
|-|-|-|-|-|
| 0 | 1 | 2.1 V RMS; the speakers clip | held at full scale | a quiet line input only |
| 1 | 1/2 | 1.05 V RMS; 2.1 V RMS, 0.55 W | held at full scale: 17,548 of 192,000 values in 2 s [verified: test 16] | the speakers clip on loud mixes |
| **2 (default)** | **1/4** | **0.53 V RMS; 1.05 V RMS, 0.14 W** | **at most 0.53 of full scale, nothing held** [verified: test 16]: 1.6 V peak at J701, 3.2 V peak at the speakers, under their 4.7 V [inferred] | the loudest level at which nothing can clip, in the mix or in the amplifier |
| 3 | 1/8 | 0.26 V RMS; 0.53 V RMS, 0.03 W | at most 0.27 of full scale | |
| 4 | 1/16 | 0.13 V RMS; 0.26 V RMS, 0.01 W | at most 0.13 of full scale | v3.4's default and tiny386's own |

"A lone source at full scale" is a full-scale sine from the SB16 or the AdLib; real game sound is
mostly well below that [inferred]. Speaker powers are V squared over 8 ohm, for a sine [inferred]. So
the default is 12 dB louder than v3.4 on HDMI, the line out and the speakers alike [verified: test 16,
part 4]. If the UART shows `i2s: N of the last 10 s's ... sample values held at full scale`, the level
is set louder than the sound allows: one more on `audio_shift` halves it.


## Mute, and when it plays [verified: `dosboard.c`, `i2s.c`; TI as named]

| When | AUD_XSMT (GPIO48) | The I2S clocks | What you should hear [inferred] |
|-|-|-|-|
| power-on or RESET until the board check (`board_early_init()`, the first second or two [unknown: not measured]) | high, by R703 | none | nothing: with no clocks the DAC stays in standby (TI DAC 11.5.2); the amplifier is on, its input silent, so only its own hiss (27 uV A-weighted at 6 dB, TI amp) |
| the board check, once the MS7210 has answered | driven low | none | nothing: the amplifier shut down (outputs 2 kohm, TI amp), the DAC muted. The amplifier turning off is the one edge here; TI gives no pop figure [unknown] |
| until the PC starts (boot menu included) | low | none yet | nothing |
| the PC starts | low | start | nothing for 20 ms, while the DAC's clock locks (16 LRCK periods, TI DAC 9.3.5.3) |
| 20 ms later, and from then on | driven high | running | the DAC fades the sound in over 104 samples, about 2 ms (TI DAC 9.3.3); the amplifier starts in 3.5 ms (TI amp, at 3.6 V) |
| silence from the guest | high | running | nothing: the DAC itself mutes after 1024 samples of zeros (TI DAC p. 16); the amplifier hisses faintly |
| the sound task falls behind | high | running | a gap of silence: since v3.5 each DMA buffer is cleared once sent, so the I2S lines carry zeros, not the last buffers again (a buzz) |
| a crash | driven low, then the I2S output stopped 4 ms later | stop | the line out fades over 104 samples; the speakers stop at once (the amplifier shuts down on the same pin), so a click is possible if sound was playing |
| tiny386 stops on an error (red on the screen) | high | running | whatever the stopped PC's sound devices give, usually nothing |
| power off or RESET | high (R703) | stop | possibly a click: TI asks for XSMT low 150 samples and 0.2 ms before power goes (TI DAC 11.2.1), which the firmware cannot know in advance; through RESET the amplifier stays on |

The board check drives GPIO48 low only once the MS7210 has answered: on a Korvo-1 GPIO48 is a camera
input [verified: `..\..\tiny386-hdmi-board\research\s31-module.md` 11]. GPIO48 is driven from then
on, never left to R703 while the PC runs: TI wants XSMT's edges under 20 ns (TI DAC 8.7) [verified:
`dosboard.c`, `i2s.c`].

A board point for Plat, from the review of v3.5 (its F1): because R703 pulls AUD_XSMT up, the
amplifier is on and the DAC unmuted from power-on until the board check, and again from the moment
any reset starts (the RESET key, a brownout, esptool's DTR and RTS reset when flashing), while the
I2S clocks stop or have not started. TI asks for XSMT low 150 sample times and 0.2 ms before the
clocks or the power go (TI DAC 11.2.1), which a reset cannot give. Whether any of it is audible is
[unknown].

* **Recommended: fit R703 as 10k to GND instead of to the DAC's 3.3 V.** With GPIO48 not driven, the
  PCM5102A's XSMT gives out at most 10 uA (TI DAC p. 7, IIL at 0 V) and each TPA2012D2 shutdown pin
  at most 5 uA (TI amp p. 5, IIL), so the line rests at no more than about 0.2 V, under the
  TPA2012D2's VIL of 0.35 V (TI amp p. 4) and the DAC's 0.3 x DVDD [verified: the data sheet figures;
  inferred: the sum]. Driven high, GPIO48 also feeds 0.33 mA into the 10k [inferred].
* Leaving R703 off is not enough by the data sheets: the line would rest on the TPA2012D2's two
  internal pull-downs, about 300 kohm each (150 kohm together, CHANGES-A2.md 20), and 10 uA from the
  DAC could hold it at 1.5 V, well over 0.35 V [inferred from the limits; typical leakage is far
  lower, unknown].
* The price of the pull-down: firmware that never drives GPIO48 high (the Linux route, someone's own
  ESP-IDF program) gets neither line out nor speakers. With no firmware at all there are no I2S
  clocks and so no sound anyway, so the pull-up only ever helps firmware that does not know about
  GPIO48 [inferred].

This firmware drives the pin both ways from the board check on and is right with either fitting; no
code change is needed, only two comments that name R703 as a pull-up (`board_dosboard.h`, `i2s.c`).
The change is the board step's (its generators in `..\..\tiny386-hdmi-board\dosboard\gen`), not made
here.

The UART shows `i2s: the line out (PCM5102A) and the speakers unmuted, 20 ms of clocks after the start;
level 1/4 (audio_shift 2)` once, when the PC starts.


## On the bench

1. Before the PC starts (boot menu): a scope on TP949 should read about 0 V, TP937 and TP938 flat. No
   hiss from the speakers.
2. The PC started: TP949 at 3.3 V; TP937 a 1.536 MHz clock (16-bit slots), TP938 48 kHz at 50 per
   cent.
3. FreeDOS's beep, or a game's sound, on the speakers at J703; then plug powered speakers or headphones
   into J701: the speakers must go quiet. Then the same sound on the HDMI monitor.
4. Note how loud the speakers are at the default level and whether anything distorts; `audio_shift = 1`
   is twice as loud (and can clip on loud mixes), `3` half as loud.
5. If the line out is silent and HDMI plays (or the other way round): try `i2s_slot_bits = 32` first;
   CHANGES-A2.md 3 warns that swapping BCLK and WS for the MS7210's sake would silence the DAC, which
   is wired the conventional way.
6. A click at power-on, at the board check, when the PC starts or at RESET is worth noting with when
   it came: the table above says which edge each would be.
