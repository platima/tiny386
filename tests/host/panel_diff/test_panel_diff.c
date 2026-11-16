/*
 * esp/main/panel_diff.c on the PC: the ESP-Mosaico's panel driver sends
 * only the blocks of each piece of rows that changed (lcd_mosaico.c); this
 * plays that against a panel in memory and checks that the panel then shows
 * exactly the picture, after every change.
 *
 * A 640x480 frame and a 480x480 panel, pieces of 6 rows as lcd_mosaico.c
 * has them, rows sent in even numbers.  The panel shows one of two views,
 * as lcd_mosaico.c's do:
 *   - a window of the frame at px (a multiple of 4), its blocks compared
 *     where they lie in the frame and only the changed rectangle copied out,
 *     the bytes of each pixel swapped (Windows at 480x480, FOLLOW);
 *   - a picture made from the frame, a piece at a time in full (here the
 *     window at 160, made by a loop of this file's own), then compared and
 *     packed (FIT).
 * Each round changes the frame (a few pixels, rectangles, a whole row,
 * everything, or nothing), then sends the rows that changed as vga.c
 * reports them (whole rows, an even range, sometimes wider than the
 * change), or once in a while all rows with force, as a new view does.
 * The panel starts with noise, as after power-on.
 *
 * Built with -DPLANT=1 (pack drops the last row), -DPLANT=2 (the diff
 * misses a block that changed in the rightmost column) or -DPLANT=3 (the
 * copy drops the last row), it must fail.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "panel_diff.h"

#define FW 640
#define FH 480
#define W 480
#define H 480
#define CHUNK 6
#define MADE_PX 160     /* the made view: the window at 160 */

static uint16_t frame[FH][FW];  /* what vga.c draws */
static uint16_t panel[H][W];    /* what the panel shows, its byte order */
static uint32_t hashes[H * (W / PD_BLOCK)];
static uint8_t known[H];
static struct panel_diff pd;
static uint16_t buf[CHUNK * W];
static unsigned long sent_px, pieces, skipped, views[2];
static int view = 1, px = (FW - W) / 2;

static uint32_t rng = 12345;
static uint32_t rnd(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return rng;
}

static uint16_t swap16(uint16_t v)
{
	return (uint16_t) ((v >> 8) | (v << 8));
}

#if PLANT == 1
static size_t pack(uint16_t *b, int w, int ya, int yb, int x0, int x1)
{
	if (yb - ya > 1)
		yb--;
	return panel_diff_pack(b, w, ya, yb, x0, x1);
}
#else
#define pack panel_diff_pack
#endif

#if PLANT == 3
static size_t copy(uint16_t *d, const uint16_t *s, int stride, int ya, int yb,
		   int x0, int x1)
{
	if (yb - ya > 1)
		yb--;
	return panel_diff_copy_swapped(d, s, stride, ya, yb, x0, x1);
}
#else
#define copy panel_diff_copy_swapped
#endif

/* the made view's rows y to y + n - 1, in full, the panel's byte order */
static void make(uint16_t *b, int y, int n)
{
	for (int r = 0; r < n; r++)
		for (int x = 0; x < W; x++)
			b[r * W + x] = swap16(frame[y + r][MADE_PX + x]);
}

/* rows y0 to y1 - 1 (even) as lcd_mosaico.c's send_rows() does it */
static void send_rows(int y0, int y1, bool force)
{
	for (int y = y0; y < y1; y += CHUNK) {
		int n = y1 - y < CHUNK ? y1 - y : CHUNK;
		int ya, yb, x0, x1;
		size_t npx;
		if (view) {
			const uint16_t *src = &frame[y][px];
			if (!panel_diff_rows(&pd, src, FW, y, n, force, &ya, &yb, &x0, &x1)) {
				skipped++;
				continue;
			}
#if PLANT == 2
			if (x1 == W && x1 - x0 > PD_BLOCK)
				x1 -= PD_BLOCK;
#endif
			ya &= ~1;
			yb = (yb + 1) & ~1;
			if (yb > n)
				yb = n;
			npx = copy(buf, src, FW, ya, yb, x0, x1);
		} else {
			make(buf, y, n);
			if (!panel_diff_rows(&pd, buf, W, y, n, force, &ya, &yb, &x0, &x1)) {
				skipped++;
				continue;
			}
#if PLANT == 2
			if (x1 == W && x1 - x0 > PD_BLOCK)
				x1 -= PD_BLOCK;
#endif
			ya &= ~1;
			yb = (yb + 1) & ~1;
			if (yb > n)
				yb = n;
			npx = pack(buf, W, ya, yb, x0, x1);
		}
		/* the panel: CASET x0..x1-1, RASET y+ya..y+yb-1, the pixels */
		int cw = x1 - x0, rows = (int) npx / cw;
		for (int r = 0; r < rows; r++)
			memcpy(&panel[y + ya + r][x0], buf + (size_t) r * cw, (size_t) cw * 2);
		sent_px += npx;
		pieces++;
	}
}

static int check(int round)
{
	int at = view ? px : MADE_PX;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			if (panel[y][x] != swap16(frame[y][at + x])) {
				printf("round %d, view %d at %d: panel (%d,%d) is %04x, the "
				       "picture %04x\n", round, view, at, x, y, panel[y][x],
				       swap16(frame[y][at + x]));
				return 1;
			}
	return 0;
}

int main(void)
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			panel[y][x] = (uint16_t) rnd();
	panel_diff_init(&pd, W, H, hashes, known);
	/* the start: everything, as lcd_init() sends black */
	send_rows(0, H, true);
	if (check(-1))
		return 1;

	const int rounds = 20000;
	for (int round = 0; round < rounds; round++) {
		int kind = rnd() % 100;
		int ylo = H, yhi = -1;
		if (kind < 40) {                /* a few pixels */
			int k = 1 + rnd() % 6;
			while (k--) {
				int x = rnd() % FW, y = rnd() % FH;
				frame[y][x] ^= (uint16_t) (1u << (rnd() % 16));
				ylo = y < ylo ? y : ylo;
				yhi = y > yhi ? y : yhi;
			}
		} else if (kind < 80) {         /* a rectangle (a window, the pointer) */
			int x = rnd() % FW, y = rnd() % FH;
			int w = 1 + rnd() % 64, h = 1 + rnd() % 48;
			uint16_t c = (uint16_t) rnd();
			for (int yy = y; yy < y + h && yy < FH; yy++) {
				for (int xx = x; xx < x + w && xx < FW; xx++)
					frame[yy][xx] = (uint16_t) (c + xx);
				ylo = yy < ylo ? yy : ylo;
				yhi = yy > yhi ? yy : yhi;
			}
		} else if (kind < 88) {         /* a whole row */
			int y = rnd() % FH;
			for (int x = 0; x < FW; x++)
				frame[y][x] = (uint16_t) rnd();
			ylo = yhi = y;
		} else if (kind < 92) {         /* everything */
			for (int y = 0; y < FH; y++)
				for (int x = 0; x < FW; x++)
					frame[y][x] = (uint16_t) rnd();
			ylo = 0;
			yhi = FH - 1;
		} else if (kind < 96) {         /* nothing, but rows reported */
			ylo = rnd() % FH;
			yhi = ylo + rnd() % 40;
			if (yhi >= FH)
				yhi = FH - 1;
		}
		/* else: nothing at all */
		if (kind >= 98) {
			/* a new view (the other one, or the window elsewhere): sent
			   with force, as repaint() does; half the time forgotten
			   first as well */
			if (rnd() % 2)
				view ^= 1;
			px = (int) (rnd() % (FW - W + 4)) & ~3;
			if (rnd() % 2)
				panel_diff_forget(&pd);
			send_rows(0, H, true);
		} else if (kind == 97) {
			/* forgotten, then only the reported rows sent: the rest
			   must be sent in full the next time they come */
			panel_diff_forget(&pd);
			send_rows(0, H, false);
		} else if (yhi >= 0) {
			/* vga.c's range: even, sometimes wider */
			int y0 = ylo & ~1, y1 = (yhi + 2) & ~1;
			if (rnd() % 4 == 0) {
				y0 = (y0 - (int) (rnd() % 20)) & ~1;
				y1 = (y1 + (int) (rnd() % 20) + 1) & ~1;
			}
			y0 = y0 < 0 ? 0 : y0;
			y1 = y1 > H ? H : y1;
			send_rows(y0, y1, false);
		}
		views[view]++;
		if (check(round))
			return 1;
	}
	if (!views[0] || !views[1]) {
		printf("panel_diff: a view never shown (%lu, %lu rounds)\n", views[0], views[1]);
		return 1;
	}
	printf("panel_diff: %d rounds (%lu made in full, %lu from the frame), %lu pieces "
	       "sent, %lu skipped, %.1f%% of the pixels of whole pieces sent\n", rounds,
	       views[0], views[1], pieces, skipped,
	       100.0 * sent_px / ((pieces + skipped) * (double) CHUNK * W));
	return 0;
}
