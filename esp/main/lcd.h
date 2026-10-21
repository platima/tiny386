/*
 * What a board's panel file gives the rest of the firmware, for boards
 * whose frame buffer the VGA code and the boot console draw into directly
 * (LCD_ZERO_COPY): lcd_korvo1.c for the Korvo-1; a DOS board file gives the
 * same functions for its panel timing.
 *
 * The frame buffer is LCD_WIDTH x LCD_HEIGHT pixels of RGB565, rows next to
 * each other (stride LCD_WIDTH * 2 bytes), in PSRAM behind ESP-IDF's
 * write-back data cache.  Whoever draws into it calls lcd_flush() for the
 * rows drawn, which writes them back so that the panel's DMA sees them.
 */
#ifndef LCD_H
#define LCD_H

#include <stdbool.h>
#include <stdint.h>

/* Brings the panel up and returns its frame buffer (cached address), or
   NULL if the panel could not be started.  Called first thing in
   app_main(), before the boot console. */
void *lcd_init(void);

/* Rows y to y + h - 1 of the frame buffer were drawn: write them back from
   the cache.  x and w are ignored (whole rows are written back). */
void lcd_flush(int x, int y, int w, int h);

/* After the write-back, also drop the rows from the cache, which gives the
   cache back to the emulator (ESP_CACHE_MSYNC_FLAG_INVALIDATE).  Only safe
   while nothing else writes those rows at the same time. */
void lcd_set_invalidate(bool on);

/* What lcd_flush() has cost, counted with the CPU's cycle counter. */
struct lcd_stats {
	uint32_t flushes;       /* calls that wrote something back */
	uint32_t rows;          /* rows written back */
	uint64_t cycles;        /* in esp_cache_msync() */
	uint32_t cycles_max;    /* the longest call */
};
/* Copies the counts, and starts them again from zero if reset is set. */
void lcd_get_stats(struct lcd_stats *st, bool reset);

#endif /* LCD_H */
