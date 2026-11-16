/*
 * panel_diff: which parts of a panel's rows changed since they were last
 * sent, for a panel that keeps its own picture (the ESP-Mosaico's CO5300,
 * lcd_mosaico.c), so that only those go over the bus.
 *
 * Each row is cut into blocks of PD_BLOCK pixels, and a 32-bit hash of each
 * block as last sent is kept.  For a piece of rows, panel_diff_rows() says
 * which rows and blocks differ, as one rectangle.  The rows can be where
 * they are drawn (a window into a wider frame), and then
 * panel_diff_copy_swapped() makes the rectangle ready to send, the bytes of
 * each pixel the panel's way round; or a piece made ready in full, and then
 * panel_diff_pack() moves the rectangle to the front of its buffer.  Either
 * way it goes out as one transfer.  Pure C, no ESP-IDF: tested on the PC
 * (tests/host/panel_diff).
 */
#ifndef PANEL_DIFF_H
#define PANEL_DIFF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PD_BLOCK 32             /* pixels a block: a multiple of 4 */

struct panel_diff {
	int w, h;               /* the panel's size, w a multiple of PD_BLOCK */
	int blocks;             /* blocks a row, w / PD_BLOCK */
	uint32_t *hash;         /* h * blocks hashes, as last sent */
	uint8_t *known;         /* h flags: the row's hashes are what it shows */
};

/* The storage: h * (w / PD_BLOCK) * 4 bytes for hash and h bytes for
   known, from the caller; every row unknown to begin with. */
void panel_diff_init(struct panel_diff *pd, int w, int h, uint32_t *hash,
		     uint8_t *known);

/* Every row unknown: the next panel_diff_rows() of each counts all of it as
   changed (another view, or the panel's picture otherwise not known). */
void panel_diff_forget(struct panel_diff *pd);

/* Rows r0 to r0 + n - 1 unknown (a transfer of them that failed). */
void panel_diff_forget_rows(struct panel_diff *pd, int r0, int n);

/* Rows r0 to r0 + n - 1 of the panel, as they are to be shown: n rows of w
   pixels at buf, stride pixels from the start of one to the next (w or
   more; buf and stride such that each block's start is 4-byte aligned).
   Returns false if none differs from what was last sent; else true, with
   the rectangle that covers every block that differs: rows *ya to *yb - 1
   of the piece (0 to n) and pixels *x0 to *x1 - 1 (multiples of PD_BLOCK).
   force: all of the piece counts as changed.  The hashes become those of
   buf either way. */
bool panel_diff_rows(struct panel_diff *pd, const uint16_t *buf, int stride,
		     int r0, int n, bool force, int *ya, int *yb, int *x0, int *x1);

/* Rows ya to yb - 1, pixels x0 to x1 - 1 of buf (rows w pixels long) moved
   to the start of buf, one row after another.  Returns the pixels. */
size_t panel_diff_pack(uint16_t *buf, int w, int ya, int yb, int x0, int x1);

/* Rows ya to yb - 1, pixels x0 to x1 - 1 of the rows at src (stride pixels
   apart) to dst, one row after another, the two bytes of each pixel
   swapped (the CO5300 takes the high byte first).  src + x0 and stride
   such that every row's start is 4-byte aligned, x1 - x0 even.  Returns
   the pixels. */
size_t panel_diff_copy_swapped(uint16_t *dst, const uint16_t *src, int stride,
			       int ya, int yb, int x0, int x1);

#endif /* PANEL_DIFF_H */
