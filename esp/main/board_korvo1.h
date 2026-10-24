// ESP32-S31-Korvo-1, built against ESP-IDF v6.1-dev-8142-g188e3e55bb.
// Pins and panel timings are the ones the esp32-s31-linux loader uses
// (annoyedmilk/esp32-s31-linux be5b725, bootloader/main/display.c and sd.c).
#define BUILD_ESP32
#define BOARD_NAME "the ESP32-S31-Korvo-1"

#define IRAM_ATTR_CPU_EXEC1 IRAM_ATTR

#define BPP 16
// No FULL_UPDATE: a text screen is drawn again only where it changed.  That
// puts last_ch_attr (16,896 bytes) into VGAState, which pcmalloc() takes
// from the 128 KiB device window; the boot console prints how much of the
// window is used.
#define USE_LCD_RGB_KORVO1
#define LCD_WIDTH  800
#define LCD_HEIGHT 480

// The VGA code draws straight into the panel's frame buffer, and redraw()
// only writes the cache back (see lcd_korvo1.c).
#define LCD_ZERO_COPY
// Most pictures a second the VGA code draws: one every LCD_FRAME_MS.  The
// panel shows about 35 a second (18 MHz, 928 x 548 clocks).
#define LCD_FRAME_MS 30

// microSD on SDMMC slot 0 through the IO MUX.  No card detect or write protect.
#define SD_CLK 24
#define SD_CMD 25
#define SD_D0 20
#define SD_D1 21
#define SD_D2 22
#define SD_D3 23
// The slot's power switch, active low.
#define SD_PWR_EN_GPIO 39

// The emulator takes the largest PSRAM block but this much, which is left
// for ESP-IDF (FatFs buffers, the console's text).  The panel's frame
// buffer (750 KiB) is not in it: lcd_init() has taken that before the
// pool is sized (app_main).  About 15,300 KiB is left for the guest:
// mem_size 14M with vga_mem_size 256K or 320K.
#define PSRAM_KEEP_FREE (256 * 1024)

// No I2S: the Korvo-1 audio wiring is not described in the Linux sources.

// The status LED (status_led.c, STATUS-LED.md): U3, a WS2812B, its data from
// GPIO37 (WS2812_CTRL, through R21 0R) by Q2, an LBSS138LT1G, to 5 V levels
// with R93 10k to 3V3 and R91 1k to 5V, not inverted (Korvo-1 schematic
// v1.1 page 3).  The DOS board has the same circuit on the same pin.
#define STATUS_LED_GPIO 37

// From the Linux route's patch (tiny386-korvo.patch), shared with it: the
// emulator's 16 and 32-bit accesses to guest memory through unaligned.h,
// which makes one that is not aligned of smaller ones that are.  With this
// board's flags (-mtune=esp32s31) the compiler joins them into one again,
// so in this build each is one load or store at any address (a store behind
// a test whose two arms are alike), as without the define.  The S31's cores
// do misaligned accesses themselves: ESP-IDF has no handler for the trap,
// and the board runs Windows 95, whose code is full of them.  Kept as the
// board has run it.
#define I386_ALIGNED_ONLY
// A pixel is stored only where the frame buffer does not show it already;
// with BPP 16 this also turns on the quick way of drawing the 16-colour
// modes and the clear of a graphics screen put off until the next picture
// (vga.c, VGA_PLANAR_FAST).  Behind ESP-IDF's write-back cache a pixel that
// is not stored leaves its cache line clean, so lcd_flush() has less to
// write back to PSRAM.
#define VGA_STORE_CHANGED

// vga.c: the retrace bits of port 3DAh follow the clock and the CRT
// controller's timing (about 70 a second in 400-line modes, 60 in 480-line
// modes), however often the picture is drawn; vga_step() then only says
// "draw now", and vga_task paces the pictures (LCD_FRAME_MS).
#define VGA_RETRACE_REALTIME
// vga.c: redraw() is given only the rows of the screen that changed, so
// lcd_flush() writes back only those.
#define VGA_REDRAW_DIRTY_ROWS
// vga.c: a graphics picture larger than the panel (800x600 and up) is
// scaled down to fit and centred (800x600 as 640x480), rather than cut.
// Smaller ones are centred as they are: 720x400 text at (40,40), 640x480
// at (80,0), 320x200 doubled to 640x400 at (80,40).
#define VGA_SHRINK_LARGE
// vga.c, pc.c (v4.0): what the guest writes into video memory is noted a
// block at a time (256 bytes since v4.4, 1 KiB before), and a VBE picture of
// 8, 15, 16, 24 or 32 bits a pixel (Windows at 800x480 through a VBE display
// driver, the BIOS's VBE modes) is drawn again only in the rows written,
// and since v4.4 only in the part of each row the blocks reach, converted to
// RGB565 with no division; a still picture costs a look at a byte for each
// block.  Since v4.1 a store through the VGA window is noted only while VBE
// is on (not in mode 13h), and none at all with [esp] vbe_blocks = 0.
#define VGA_DIRTY_BLOCKS
