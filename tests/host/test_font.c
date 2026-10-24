/*
 * Host test for the text screen and its font (v4.4, BUILD-NOTES 26.3 and
 * 29): vga.c as the Korvo-1 builds it (or the DOS board, by its header)
 * draws a text cell again only when its character or attribute changes,
 * and the glyphs are in plane 2, so a font loaded under an unchanged
 * screen left the old glyphs there (the grey text screen of a restart
 * from Windows in 16 colours).  Here every text picture made after a
 * change of font is held against a whole redraw of the same state
 * (vga_refresh() with full_update): they must be the same, pixel for
 * pixel.
 *
 *  A  a font loaded as the VGA BIOS loads one (SR02 = 04h, SR04 = 07h,
 *     GR04 = 02h, GR05 = 00h, GR06 = 04h, the glyphs stored, then the
 *     registers back), with no picture while it was loaded;
 *  B  the same with a picture part way through the loading;
 *  C  another character map selected (SR03, map B, which the cells'
 *     attributes pick) with nothing else changed;
 *  D  once the font has settled, a picture with nothing changed draws no
 *     more than before (the cursor's row only): the font check costs no
 *     whole pictures;
 *  E  the map mask written in a graphics mode (planar drawing) and the
 *     text mode back: the text screen is drawn whole there anyway (a change
 *     of mode), and right.
 *
 * Built against an older vga.c (OLD defined by run.sh) the same test must
 * fail A, B and C: that shows it sees the fault.
 *
 * Exit status 0 when every check passes, 1 when any fails.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vga.h"
#include "pci.h"

static uint32_t fake_us;
uint32_t get_uticks(void)
{
	return fake_us;
}

void *pcmalloc(long size)
{
	return calloc(1, size);
}

PCIDevice *pci_register_device(PCIBus *b, const char *name, int devfn,
			       uint16_t vendor_id, uint16_t device_id,
			       uint8_t revision_id, uint16_t class_id)
{
	return NULL;
}

void pci_register_bar(PCIDevice *d, unsigned int bar_num, uint32_t size, int type,
		      void *opaque, void (*set_bar)(void *opaque, int bar_num,
						    uint32_t addr, bool enabled))
{
}

static int failures;

static void check(int ok, const char *what)
{
	printf("  %s: %s\n", ok ? "ok" : "FAIL", what);
	if (!ok)
		failures++;
}

#define W LCD_WIDTH
#define H LCD_HEIGHT
#define VRAM (256 * 1024)
static uint16_t fb[W * H], before[W * H];
static uint8_t vram[VRAM + VGA_MEM_SLACK];
static int rows_drawn;

static void redraw(void *opaque, int x, int y, int w, int h)
{
	(void) opaque; (void) x; (void) w; (void) y;
	rows_drawn += h;
}

/* one picture as the board makes it; the rows given to redraw() counted */
static void picture(VGAState *s)
{
	rows_drawn = 0;
	fake_us += 30000;
	vga_refresh(s, redraw, NULL, 0);
}

/* the picture just made against a whole redraw of the same state: the
   pixels that differ (the whole redraw then stays on the screen) */
static long against_whole(VGAState *s)
{
	memcpy(before, fb, sizeof(fb));
	int keep = rows_drawn;
	vga_refresh(s, redraw, NULL, 1);
	rows_drawn = keep;
	long d = 0;
	for (int i = 0; i < W * H; i++)
		d += before[i] != fb[i];
	return d;
}

static void sr(VGAState *s, int i, int v)
{
	vga_ioport_write(s, 0x3c4, i);
	vga_ioport_write(s, 0x3c5, v);
}

static void gr(VGAState *s, int i, int v)
{
	vga_ioport_write(s, 0x3ce, i);
	vga_ioport_write(s, 0x3cf, v);
}

/* the VGA BIOS's get_font_access() and release_font_access() (SeaVGABIOS
   vgasrc/stdvga.c) */
static void font_open(VGAState *s)
{
	sr(s, 0x00, 0x01);
	sr(s, 0x02, 0x04);
	sr(s, 0x04, 0x07);
	sr(s, 0x00, 0x03);
	gr(s, 0x04, 0x02);
	gr(s, 0x05, 0x00);
	gr(s, 0x06, 0x04);
	gr(s, 0x08, 0xff);
}

static void font_close(VGAState *s)
{
	sr(s, 0x00, 0x01);
	sr(s, 0x02, 0x03);
	sr(s, 0x04, 0x03);
	sr(s, 0x00, 0x03);
	gr(s, 0x06, 0x0e);
	gr(s, 0x04, 0x00);
	gr(s, 0x05, 0x10);
}

/* glyph line j of character c in character map m, stored through the
   window as the BIOS stores it (the font open: plane 2 only, A000:0000) */
static void glyph(VGAState *s, int m, int c, int j, int bits)
{
	vga_mem_write(s, m * 0x2000 + c * 32 + j, bits);
}

/* a new font for characters lo to hi - 1: each line's bits turned over and
   stripes added, so that every cell of them looks different */
static void load_font(VGAState *s, int m, int lo, int hi, int salt)
{
	for (int c = lo; c < hi; c++)
		for (int j = 0; j < 16; j++)
			glyph(s, m, c, j, (j & 1 ? 0xaa : 0x55) ^ (c * 7 + j + salt));
}

/* the screen: every cell a letter, in colours */
static void fill_text(void)
{
	for (int row = 0; row < 25; row++)
		for (int col = 0; col < 80; col++) {
			const char *t = "The quick brown fox jumps over the lazy dog 0123456789 ";
			vram[(row * 80 + col) * 4] = t[(row + col) % 55];
			vram[(row * 80 + col) * 4 + 1] = (row % 7 + 9) | (row % 2 ? 0x10 : 0);
		}
}

int main(int argc, char **argv)
{
	(void) argc; (void) argv;
	long d;
	char msg[160];

	memset(vram, 0, sizeof(vram));
	VGAState *s = vga_init((char *) vram, VRAM, (uint8_t *) fb, W, H);
	fill_text();
	picture(s);
	picture(s);
	d = against_whole(s);
	check(d == 0, "80x25 text with vga_init()'s font: the picture is the whole redraw's");

	printf("A: a font loaded between two pictures\n");
	font_open(s);
	load_font(s, 0, 'A', 'z' + 1, 1);
	font_close(s);
	picture(s);
	d = against_whole(s);
	snprintf(msg, sizeof(msg), "the picture after the font is the whole redraw's "
		 "(%ld pixels differ, %d rows drawn)", d, rows_drawn);
	check(d == 0, msg);

	printf("B: a picture while the font is loaded\n");
	font_open(s);
	load_font(s, 0, ' ', 'm', 2);   /* the space, the digits, T and a to l */
	picture(s);
	d = against_whole(s);
	snprintf(msg, sizeof(msg), "the picture half way through is the whole redraw's "
		 "(%ld pixels differ)", d);
	check(d == 0, msg);
	load_font(s, 0, 'm', 'z' + 1, 2);
	font_close(s);
	picture(s);
	d = against_whole(s);
	snprintf(msg, sizeof(msg), "the picture after the rest of the font is the whole "
		 "redraw's (%ld pixels differ, %d rows drawn)", d, rows_drawn);
	check(d == 0, msg);

	printf("C: another character map (SR03)\n");
	font_open(s);
	load_font(s, 1, ' ', 128, 3);   /* map 1, at 8 KiB into plane 2 */
	font_close(s);
	picture(s);
	picture(s);
	d = against_whole(s);
	check(d == 0, "a font in map 1, map 0 still shown: the picture is the whole redraw's");
	/* every cell's attribute has bit 3 set (fill_text()), which picks
	   character map B: SR03 bit 5 makes map B map 1 */
	sr(s, 0x03, 0x20);
	picture(s);
	d = against_whole(s);
	snprintf(msg, sizeof(msg), "SR03 to map 1: the picture is the whole redraw's "
		 "(%ld pixels differ, %d rows drawn)", d, rows_drawn);
	check(d == 0, msg);
	sr(s, 0x03, 0x00);
	picture(s);
	d = against_whole(s);
	snprintf(msg, sizeof(msg), "SR03 back to map 0: the picture is the whole redraw's "
		 "(%ld pixels differ)", d);
	check(d == 0, msg);

	printf("D: nothing changed once the font has settled\n");
	picture(s);
	picture(s);
	picture(s);
	snprintf(msg, sizeof(msg), "a picture with nothing changed draws the cursor's row "
		 "at most (%d rows drawn)", rows_drawn);
	check(rows_drawn <= 16, msg);
	vram[(12 * 80 + 40) * 4] = '#';
	picture(s);
	snprintf(msg, sizeof(msg), "one cell changed: one text row, or two with the cursor's "
		 "(%d rows drawn)", rows_drawn);
	check(rows_drawn <= 13 * 16 && rows_drawn >= 16, msg);
	d = against_whole(s);
	check(d == 0, "and the picture is the whole redraw's");

	printf("E: the map mask in a graphics mode, then text again\n");
	gr(s, 0x06, 0x05);              /* graphics, A000h 64K */
	for (int i = 0; i < 50; i++) {
		sr(s, 0x02, i & 1 ? 0x04 : 0x0f);
		vga_mem_write(s, i, 0xff);
	}
	picture(s);
	sr(s, 0x02, 0x03);
	gr(s, 0x06, 0x0e);              /* text again */
	picture(s);
	d = against_whole(s);
	snprintf(msg, sizeof(msg), "back in text: the picture is the whole redraw's "
		 "(%ld pixels differ)", d);
	check(d == 0, msg);

	if (failures) {
		printf("test_font: %d checks FAILED\n", failures);
		return 1;
	}
	printf("test_font: all checks passed\n");
	return 0;
}
