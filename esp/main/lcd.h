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
	/* v4.4, double buffering: frame buffers swapped at the panel's
	   frame, the rows copied into the new back buffer, the cycles of the
	   copy and of the wait for the swap */
	uint32_t flips, copied_rows;
	uint64_t copy_cycles, wait_cycles;
	uint32_t flip_timeouts;
};
/* Copies the counts, and starts them again from zero if reset is set. */
void lcd_get_stats(struct lcd_stats *st, bool reset);

/*
 * v4.4: double buffering ([esp] double_buffer, Ctrl+Alt+F9), on a board
 * whose panel file has it (LCD_DOUBLE_BUFFER in the board header: the
 * Korvo-1).  The VGA code draws into a second frame buffer, the back one,
 * while the panel shows the front one; after a picture that changed rows,
 * the panel's DMA is told to take the back buffer from its next frame on,
 * and once it has (ESP-IDF's frame-buffer-complete event) the two change
 * places and the rows the picture changed are copied into the new back
 * buffer, so that both hold the picture and the next one is drawn over it.
 * The panel never shows a frame buffer being written: no tearing between
 * rows.  The console and the crash screen draw into the front buffer.
 * Every function but lcd_double_start() is called with the display lock
 * held, on core 0.
 */
/* Made once, before the PC is built: fb (LCD_WIDTH x LCD_HEIGHT x 2
   bytes, from the emulator's pool) becomes the front buffer with the
   console's picture copied into it, the panel is started again with two
   frame buffers (a moment of black), and the second one is allocated where
   the first one was.  false (and the panel as it was) if it cannot be. */
bool lcd_double_start(void *fb);
/* true while pictures are drawn into the back buffer and swapped */
bool lcd_double_on(void);
/* both buffers exist (lcd_double_start() worked) */
bool lcd_double_made(void);
/* swapping on or off (Ctrl+Alt+F9); off, the VGA code draws into the front
   buffer as without double buffering.  Returns where it is to draw now. */
void *lcd_double_set(bool on);
/* the buffer the VGA code draws into: the back one, or the only one */
void *lcd_vga_fb(void);
/* the VGA code's redraw(): rows y to y + h - 1 of its buffer drawn */
void lcd_vga_flush(int x, int y, int w, int h);
/* after a picture: if it changed rows, the swap and the copy; returns
   where the VGA code is to draw the next picture */
void *lcd_vga_present(void);
/* everything in the back buffer is to be copied at the next swap: the
   console has drawn over the front one */
void lcd_vga_copy_all(void);

#endif /* LCD_H */
