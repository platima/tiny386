// The DOS board: ESP32-S31-WROOM-3-N16R16V, MacroSilicon MS7210 HDMI
// transmitter fed from the 16-bit RGB565 bus at 800x600 60 Hz, WCH CH9350L
// USB keyboard and mouse host on UART1, microSD on SDMMC slot 0, CH340C on
// UART0 for the console and for flashing.  Built against ESP-IDF
// v6.1-dev-8142-g188e3e55bb.
//
// Pins are rev A2c's (tiny386-hdmi-board dosboard/out/CHANGES-A2.md section
// 19, 2026-10-22; DESIGN-BRIEF.md section 4 for the rest).  Every shared
// function keeps its Korvo-1 pin, so the RGB bus stays on the IO MUX.  Rev
// A2c moved I2S, the MS7210's reset and interrupt and the CH9350L's reset
// off GPIO2 to GPIO7 to GPIO49 to GPIO56, so that GPIO2 to GPIO7 (the LP
// core's pins), GPIO57 and GPIO60 are free on the 2 x 8 header J202 with
// the I2C bus (GPIO0, GPIO1).  This firmware never configures a header pin
// but GPIO0 and GPIO1: the others stay as the chip leaves them at reset
// (not driven).  tests/host/dosboard/check_pins.py checks this file against
// the board's netlist.
// A DOS board image on a Korvo-1 halts at once (board_early_init() in
// dosboard.c).  GPIO46 to GPIO57 are the Korvo-1's camera pins, all inputs
// to the S31 there but GPIO55, its XCLK, so until the MS7210 has answered
// (at I2C 0x59, then 0x2B) the image drives no pin but the I2C bus and what
// both boards share (the console's UART0, the status LED on GPIO37).  If
// nothing answers at all, it reads GPIO46 with the weak pull-up on for
// about 0.1 ms (R302 holds it low on this board; camera D0 on a Korvo-1),
// leaves it an input with no pull, and pulses RSTN (GPIO49) and looks again
// only if GPIO46 read low.
#define BUILD_ESP32
#define BOARD_NAME "the DOS board"

#define IRAM_ATTR_CPU_EXEC1 IRAM_ATTR

#define BPP 16
// No FULL_UPDATE, as on the Korvo-1 (board_korvo1.h says why).
#define USE_LCD_RGB_DOSBOARD
#define LCD_WIDTH  800
#define LCD_HEIGHT 600

// The VGA code and the boot console draw straight into the RGB driver's
// frame buffer, and lcd_flush() only writes the cache back (lcd_dosboard.c,
// lcd.h).  The drawing loop is display.c's.
#define LCD_ZERO_COPY
// Most pictures a second the VGA code draws: one every LCD_FRAME_MS (whole
// ticks of 10 ms).  The HDMI output itself runs at 60 Hz.
#define LCD_FRAME_MS 30

// RGB565 bus: data, PCLK, DE and syncs on the Korvo-1's pins (IO MUX).
// GPIO38 (the Korvo-1's LCD_CS) is USB_VBUS_EN here, so no DISP pin.
#define LCD_PCLK_GPIO   40
#define LCD_DE_GPIO     43
#define LCD_HSYNC_GPIO  44
#define LCD_VSYNC_GPIO  45
#define LCD_DATA_GPIOS  8, 9, 10, 11, 12, 13, 14, 15, \
                        16, 17, 18, 19, 33, 34, 35, 36

// VESA DMT 800x600 at 60 Hz: 40.000 MHz (PLL_F160M / 4), H 800/40/128/88,
// V 600/1/4/23, both syncs positive.  FIRMWARE-PLAN 5.2.
#define LCD_PCLK_HZ        40000000
#define LCD_HSYNC_FRONT    40
#define LCD_HSYNC_WIDTH    128
#define LCD_HSYNC_BACK     88
#define LCD_VSYNC_FRONT    1
#define LCD_VSYNC_WIDTH    4
#define LCD_VSYNC_BACK     23
// pclk_active_neg = 1, as the Korvo-1 panel runs.  ESP-IDF's header says the
// data then changes on the falling edge, its register description (and
// lcd_ll.h) that it changes on the rising edge, to be sampled on the falling
// one.  Since the second DOS board review (S5) the default pairs it with
// MS7210 register 0x1281 = 0x04 (PCLK inverted inside the MS7210, as both
// reference tables have it), which puts launch and capture on opposite
// edges by the register description; pclk_neg 0 with 0x1281 = 0x00 is the
// other such pair and the next to try, then the two same-edge ones
// (encoder.md section 9's plan A among them).  Both can be changed in
// dosboard.ini ([dosboard] pclk_neg, pclk_phase; the default phase is in
// dosboard.c).
#define LCD_PCLK_ACTIVE_NEG 1
// Bounce buffers: 0 = the LCD DMA reads the PSRAM frame buffer directly.
// If the picture tears or shifts, set 800 * 10 (two 16,000-byte buffers in
// internal RAM, filled by the CPU on core 0), or [dosboard] lcd_bounce_px
// in dosboard.ini.  The frame (480,000 pixels) must be a multiple of it.
#define LCD_BOUNCE_BUFFER_PX 0

// microSD on SDMMC slot 0 through the IO MUX, as the Korvo-1.
#define SD_CLK 24
#define SD_CMD 25
#define SD_D0 20
#define SD_D1 21
#define SD_D2 22
#define SD_D3 23
#define SD_PWR_EN_GPIO 39          /* card power, low = on */
#define SD_CD_GPIO     50          /* card detect, low = card in (logged only) */

// The emulator takes the largest PSRAM block but this much, which is left
// for ESP-IDF.  The 800x600 frame buffer (960,000 bytes) is not in it:
// lcd_init() has taken that before the pool is sized (app_main), as on the
// Korvo-1.
#define PSRAM_KEEP_FREE (256 * 1024)

// From the Linux route's patch, as on the Korvo-1 (board_korvo1.h says why).
#define I386_ALIGNED_ONLY
#define VGA_STORE_CHANGED
// vga.c, as on the Korvo-1 (board_korvo1.h says what each does).  At
// 800x600: 80x25 text at (40,100), 640x480 at (80,60), 320x200 doubled at
// (80,100), 800x600 whole; anything larger scaled down to fit.
#define VGA_RETRACE_REALTIME
#define VGA_REDRAW_DIRTY_ROWS
#define VGA_SHRINK_LARGE
#define VGA_DIRTY_BLOCKS

// Sound goes out over HDMI.  The MS7210 sends 44.1 kHz audio with its 48 kHz
// settings, so the whole audio path runs at 48 kHz (FIRMWARE-PLAN 5.6).
#define AUDIO_RATE 48000
// I2S0 to the MS7210 and, from rev A2, to the PCM5102A line out (U701,
// fitted, on the same three lines; CHANGES-A2.md section 3): bit clock, word
// select and data only.  In I2S mode the MS7210's SCK_MCLK pin is the bit
// clock, so there is no MCLK; the PCM5102A has SCK tied low and runs its PLL
// from the bit clock, which 48 kHz at 16 or 32 bit slots (32 or 64 bit
// clocks a frame) suits (TI SLAS859C 9.3.5.3).  Rev A2c: GPIO51 to GPIO53
// (were GPIO3 to GPIO5), through the GPIO matrix.
#define I2S_NUM  0
#define I2S_MCLK I2S_GPIO_UNUSED
#define I2S_BCLK 51
#define I2S_WS   52
#define I2S_DOUT 53
// AUD_XSMT: the PCM5102A's XSMT (pin 17), rev A2, and from rev A2c also the
// speaker amplifier's SDL and SDR (U702, a TI TPA2012D2, fed from the line
// out through the jack's switch contacts; CHANGES-A2.md section 20).  Low =
// the DAC soft-muted and both speakers off, high = both playing; R703 10k
// pulls it up (so with no firmware both play).  dosboard.c drives it low as
// soon as the board is known, i2s.c raises it once the I2S clocks run, and
// the panic handler pulls it low again.
#define AUDIO_MUTE_GPIO 48
// The sound's level: every sample is divided by 2 to this power (i2s.c,
// audio_mix.c), unless dosboard.ini's audio_shift says otherwise.  tiny386's
// own is 4 (1/16, 24 dB down), quiet on HDMI and the line out; 2 (1/4) is the
// loudest whole step at which the SB16, the AdLib and the PC speaker all at
// full scale together (2.125 times full scale) cannot clip, and at which the
// line out (at most about 1.1 V RMS) cannot drive the speaker amplifier
// (6 dB of gain, about 3.35 V RMS at most from 5 V into 8 ohm) into
// clipping either (LINE-OUT.md).
#define AUDIO_SHIFT_DEFAULT 2

// MS7210 on I2C: registers at 7-bit 0x59 (SA pulled up), and the monitor's
// EDID at 0x50 through the MS7210's DDC pass-through.
#define HDMI_I2C_PORT   0
#define HDMI_I2C_SDA    0
#define HDMI_I2C_SCL    1
#define HDMI_I2C_ADDR   0x59
#define HDMI_I2C_HZ     100000     /* registers */
#define HDMI_DDC_HZ     20000      /* while the DDC pass-through is on */
// Rev A2c: RSTN on GPIO49 (was GPIO2), R603 27k to +3V3, so the MS7210 runs
// before the firmware drives it (the board check reads it then); INT on
// GPIO54 (was GPIO6), R604 27k to +3V3, never configured.
#define HDMI_RSTN_GPIO  49         /* MS7210 RSTN, low = reset */
#define HDMI_INT_GPIO   54         /* MS7210 INT, not used: HPD is polled */

// CH9350L in lower mode, state 2, 115200 baud, on UART1: the keyboard and
// mouse, through kbd_input.c as the Korvo-1's USB host has them.  The S31's
// own USB pins are not connected on this board.
#define KM_UART_NUM  1
#define KM_RX_GPIO   47            /* from CH9350L TXD */
#define KM_TX_GPIO   55            /* to CH9350L RXD */
#define KM_RST_GPIO  56            /* CH9350L RST, high = reset (rev A2c; was GPIO7) */
#define KM_S0_GPIO   46            /* CH9350L S0: low = state 2, high = state 0 */
#define KM_BAUD      115200

#define USB_VBUS_EN_GPIO 38        /* high = both USB-A ports powered */
// The status LED, rev A2: D201, an Inolux IN-PI554FCH (WS2812 protocol) at
// 5 V, its data from GPIO37 through Q201 (BSS138) with R205 10k to 3V3 and
// R204 1k to 5V, not inverted: the Korvo-1's circuit (CHANGES-A2.md section
// 4).  Rev A's plain LED (low = lit) is gone; status_led.c drives this one.
#define STATUS_LED_GPIO  37
// The USER key, low = pressed: held, the boot console comes up over the
// picture with the speed on its title row (speed.c), as Ctrl+Alt+F12 does.
#define USER_KEY_GPIO    42
