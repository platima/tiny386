// ESP-Mosaico (CoreBoard V1.2), built against ESP-IDF 188e3e55bb (v6.1-dev-8142)
// with ESP-IDF's fixes e00861c3f497 and 007945125db7 on top (s31/README.md),
// as the Korvo-1.  The same ESP32-S31 (S31NRV16, 16 MB PSRAM in the package),
// with a 480x480 QSPI AMOLED (CO5300), CST9220 touch, an ES8311 codec, no SD
// card, a 16 MB NOR flash that the chip boots from and a 1 Gbit SPI NAND
// (GD5F1GM7UE) that holds the guest's disk.  Pins are the V1.2 ones of
// Espressif's ESP-Mosaico BSP (esp-mosaico/esp-mosaico-bsp, bsp/esp_mosaico.h)
// and user guide; V1.0 boards differ (mosaico.c refuses to start on one).
// s31/MOSAICO.md has the rest.
#define BUILD_ESP32
#define BOARD_NAME "the ESP-Mosaico"

// The emulator's core as on the Korvo-1 (board_korvo1.h says why each).
#define I386_FAST
#define I386_FAST_EXEC1_IRAM
//#define I386_EXEC1_SMALL
//#define PROFILE_IN_FLASH
#if !defined(I386_FAST) || defined(I386_FAST_EXEC1_IRAM)
#define IRAM_ATTR_CPU_EXEC1 IRAM_ATTR
#else
#define IRAM_ATTR_CPU_EXEC1
#endif
#define IRAM_ATTR_FAST_EXEC IRAM_ATTR

#define BPP 16
// The VGA code and the boot console draw into a 640x480 RGB565 frame in
// PSRAM, the size DOS and Windows expect; the panel shows a 480x480 window
// of it, or the whole of it at three quarters (lcd_mosaico.c).
#define USE_LCD_QSPI_MOSAICO
#define LCD_WIDTH  640
#define LCD_HEIGHT 480
#define LCD_ZERO_COPY
#define LCD_FRAME_MS 30
#define MOSAICO_PANEL_W 480
#define MOSAICO_PANEL_H 480

// The CO5300 on SPI2, quad: CS, clock, D0 to D3, reset, tearing effect.
#define MOSAICO_LCD_CS   50
#define MOSAICO_LCD_SCL  42
#define MOSAICO_LCD_D0   36
#define MOSAICO_LCD_D1   51
#define MOSAICO_LCD_D2   35
#define MOSAICO_LCD_D3   9
#define MOSAICO_LCD_RST  44
#define MOSAICO_LCD_TE   43
// The BSP's clock for it, 40 MHz quad: 20 MB/s, a whole 480x480 in 23 ms
#define MOSAICO_LCD_HZ   (40 * 1000 * 1000)
// VCC_3V3_CTRL, active low: the panel's supply and the headers' 3.3 V and 5 V
#define MOSAICO_PWR_GPIO 60
// POWER_SWITCH: driven low it asks the SAM8108 to turn the board off;
// left alone (input) otherwise
#define MOSAICO_POWER_OFF_GPIO 57
// the AI button, active low: held at power-on for USB disk mode (usb_disk.c)
#define MOSAICO_AI_BUTTON 7
// the BOOT button, active low (read only)
#define MOSAICO_BOOT_BUTTON 61

// I2C0 inside the board: touch 0x5A, codec 0x19, IMU, magnetometers, gauge
#define MOSAICO_I2C_PORT 0
#define MOSAICO_I2C_SDA  56
#define MOSAICO_I2C_SCL  3
#define MOSAICO_TOUCH_ADDR 0x5A
#define MOSAICO_TOUCH_INT  6

// The SPI NAND on SPI3 through the GPIO matrix (the SD pins of the chip):
// CLK, DI (SIO0), DO (SIO1), CS, HOLD (SIO3), WP (SIO2).  Single wire for
// now: HOLD and WP are driven high (nand_disk.c).
#define MOSAICO_NAND_CLK  20
#define MOSAICO_NAND_D    21
#define MOSAICO_NAND_Q    22
#define MOSAICO_NAND_CS   23
#define MOSAICO_NAND_HOLD 24
#define MOSAICO_NAND_WP   25

// The guest's hard disk on the NAND (hda = nand: in an ini): ide.c asks
// nand_disk.c for it (IDE_BOARD_DISK).
#define IDE_BOARD_DISK

// The ini files, bios.bin and vgabios.bin are on a FAT volume in the NOR
// flash's storage partition, at /spiflash (storage.c), in its tiny386
// folder with the boot menu, or tiny386.ini at its top.  It is made empty
// the first time, and the PC reaches it in USB disk mode.
#define BOARD_SPIFLASH_INIS
#define BOARD_SPIFLASH_FORMAT

// Sound: the ES8311 on I2S (BSP: SDOUT 52 to the codec, DSIN 40 from it),
// its amplifier's enable on GPIO45.  The I2C bus is the board's (above),
// made by mosaico.c before i2s_main() runs.
#define I2S_MCLK 54
#define I2S_BCLK 37
#define I2S_WS   49
#define I2S_DOUT 52
#define USE_ES8311
#define ES8311_I2C_NUM MOSAICO_I2C_PORT
#define ES8311_I2C_SDA MOSAICO_I2C_SDA
#define ES8311_I2C_SCL MOSAICO_I2C_SCL
#define ES8311_PA 45
// esp_codec_dev takes the address shifted, 0x19 << 1 (CE high on this board)
#define ES8311_ADDR (0x19 << 1)
#define ES8311_SHARED_BUS

// The mouse's movement goes to the panel too, which keeps the window round
// the pointer (kbd_input.c, lcd_mosaico.c)
#define BOARD_MOUSE_HOOK
/* mosaico.c has board_early_init() and board_console_ready(): no weak
   defaults for them in esp_main.c, so that the link takes mosaico.c */
#define BOARD_HOOKS 1

// As on the Korvo-1 (board_korvo1.h)
#define IDE_ENABLE_WRITE_GATHER
#define IDE_ENABLE_READ_AHEAD
// The emulator takes the largest PSRAM block but this much: ESP-IDF's own,
// FatFs, the console, and the NAND disk's buffers (opened after the pool is
// sized).  The panel's frame (600 KiB) is taken before.
#define PSRAM_KEEP_FREE (320 * 1024)
#define I386_ALIGNED_ONLY
#define VGA_STORE_CHANGED
#define VGA_RETRACE_REALTIME
#define VGA_REDRAW_DIRTY_ROWS
#define VGA_SHRINK_LARGE
#define VGA_DIRTY_BLOCKS
