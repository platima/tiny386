// The DOS board: ESP32-S31-WROOM-3-N16R16V, MacroSilicon MS7210 HDMI
// transmitter fed from the 16-bit RGB565 bus at 800x600 60 Hz, WCH CH9350L
// USB keyboard and mouse host on UART1, microSD on SDMMC slot 0, CH340C on
// UART0 for the console and for flashing.  Built against ESP-IDF
// v6.1-dev-8142-g188e3e55bb.
//
// Pins are DESIGN-BRIEF.md section 4 (tiny386-hdmi-board, 2026-10-21).  Every
// shared function keeps its Korvo-1 pin, so the RGB bus stays on the IO MUX.
// A DOS board image on a Korvo-1 halts at once (board_early_init() in
// dosboard.c): it looks for the MS7210's chip ID at I2C 0x59 before driving
// any pin that is new on this board.
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

// Sound goes out over HDMI.  The MS7210 sends 44.1 kHz audio with its 48 kHz
// settings, so the whole audio path runs at 48 kHz (FIRMWARE-PLAN 5.6).
#define AUDIO_RATE 48000
// I2S0 to the MS7210 (and to the PCM5102A footprint, not fitted): bit clock,
// word select and data only.  In I2S mode the MS7210's SCK_MCLK pin is the
// bit clock, so there is no MCLK.
#define I2S_NUM  0
#define I2S_MCLK I2S_GPIO_UNUSED
#define I2S_BCLK 3
#define I2S_WS   4
#define I2S_DOUT 5

// MS7210 on I2C: registers at 7-bit 0x59 (SA pulled up), and the monitor's
// EDID at 0x50 through the MS7210's DDC pass-through.
#define HDMI_I2C_PORT   0
#define HDMI_I2C_SDA    0
#define HDMI_I2C_SCL    1
#define HDMI_I2C_ADDR   0x59
#define HDMI_I2C_HZ     100000     /* registers */
#define HDMI_DDC_HZ     20000      /* while the DDC pass-through is on */
#define HDMI_RSTN_GPIO  2          /* MS7210 RSTN, low = reset */
#define HDMI_INT_GPIO   6          /* MS7210 INT, not used: HPD is polled */

// CH9350L in lower mode, state 2, 115200 baud, on UART1: the keyboard and
// mouse, through kbd_input.c as the Korvo-1's USB host has them.  The S31's
// own USB pins are not connected on this board.
#define KM_UART_NUM  1
#define KM_RX_GPIO   47            /* from CH9350L TXD */
#define KM_TX_GPIO   55            /* to CH9350L RXD */
#define KM_RST_GPIO  7             /* CH9350L RST, high = reset */
#define KM_S0_GPIO   46            /* CH9350L S0: low = state 2, high = state 0 */
#define KM_BAUD      115200

#define USB_VBUS_EN_GPIO 38        /* high = both USB-A ports powered */
#define LED_STATUS_GPIO  37        /* low = lit */
// The USER key, low = pressed: held, the boot console comes up over the
// picture with the speed on its title row (speed.c), as Ctrl+Alt+F12 does.
#define USER_KEY_GPIO    42
