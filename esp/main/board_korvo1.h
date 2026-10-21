// ESP32-S31-Korvo-1, built against ESP-IDF v6.1-dev-8142-g188e3e55bb.
// Pins and panel timings are the ones the esp32-s31-linux loader uses
// (annoyedmilk/esp32-s31-linux be5b725, bootloader/main/display.c and sd.c).
#define BUILD_ESP32

#define IRAM_ATTR_CPU_EXEC1 IRAM_ATTR

#define BPP 16
#define FULL_UPDATE
#define USE_LCD_RGB_KORVO1
#define LCD_WIDTH  800
#define LCD_HEIGHT 480

// The VGA code draws straight into the panel's frame buffer, and redraw()
// only writes the cache back (see lcd_korvo1.c).
#define LCD_ZERO_COPY

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
// for the panel's frame buffer (750 KiB) and ESP-IDF.
#define PSRAM_KEEP_FREE (768000 + 256 * 1024)

// No I2S: the Korvo-1 audio wiring is not described in the Linux sources.

// From the Linux route's patch (tiny386-korvo.patch), shared with it.
// No load or store of 16 or 32 bits at an address that is not aligned: one
// that is not is made of smaller ones that are (unaligned.h).  Nothing in
// ESP-IDF carries out a misaligned access by hand if the core traps on one,
// and under Linux on this board this build was the faster of the two.
#define I386_ALIGNED_ONLY
// A pixel is stored only where the frame buffer does not show it already;
// with BPP 16 this also turns on the quick way of drawing the 16-colour
// modes and the clear of a graphics screen put off until the next picture
// (vga.c, VGA_PLANAR_FAST).  Behind ESP-IDF's write-back cache a pixel that
// is not stored leaves its cache line clean, so lcd_flush() has less to
// write back to PSRAM.
#define VGA_STORE_CHANGED
