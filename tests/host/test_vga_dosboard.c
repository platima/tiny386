/*
 * Host test of where vga.c puts each picture on the DOS board's 800x600
 * frame buffer, vga.c built with board_dosboard.h (test_vga.c builds it
 * with board_korvo1.h).  Also 40-column text and its retrace rate, VBE
 * 800x600 whole, VBE 1024x768 scaled down and VBE 640x480x8.
 *
 * The emulator correctness review's rev_vga_dos.c (rounds 1 and 2), taken
 * into the tree as its round 2 note 10 asked, otherwise unchanged.
 *
 *   test_vga_dosboard      exit status 0 when every placement is right
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vga.h"
#include "pci.h"

static uint32_t fake_us;
uint32_t get_uticks(void) { return fake_us; }
void *pcmalloc(long size) { return calloc(1, size); }
PCIDevice *pci_register_device(PCIBus *b, const char *name, int devfn,
			       uint16_t vendor_id, uint16_t device_id,
			       uint8_t revision_id, uint16_t class_id) { return NULL; }
void pci_register_bar(PCIDevice *d, unsigned int bar_num, uint32_t size, int type,
		      void *opaque, void (*set_bar)(void *opaque, int bar_num,
						    uint32_t addr, bool enabled)) { }

#define W LCD_WIDTH
#define H LCD_HEIGHT
static uint16_t fb[W * H];
static int failures;

static void check(int ok, const char *what)
{
	printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		failures++;
}

static void redraw(void *o, int x, int y, int w, int h) { }
static void refresh(VGAState *s)
{
	fake_us += 30000;
	vga_refresh(s, redraw, NULL, 0);
}

/* the bounding box of every pixel that is not black */
static void bbox(int *x0, int *y0, int *x1, int *y1)
{
	*x0 = W; *y0 = H; *x1 = -1; *y1 = -1;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			if (fb[y * W + x]) {
				if (x < *x0) *x0 = x;
				if (y < *y0) *y0 = y;
				if (x > *x1) *x1 = x;
				if (y > *y1) *y1 = y;
			}
}

static void crtc(VGAState *s, int i, int v)
{
	vga_ioport_write(s, 0x3d4, i);
	vga_ioport_write(s, 0x3d5, v);
}

static void set_regs(VGAState *s, int msr, const uint8_t *sr, const uint8_t *cr,
		     const uint8_t *gr, const uint8_t *ar)
{
	vga_ioport_write(s, 0x3c2, msr);
	for (int i = 0; i < 5; i++) {
		vga_ioport_write(s, 0x3c4, i);
		vga_ioport_write(s, 0x3c5, sr[i]);
	}
	crtc(s, 0x11, 0x0c);
	for (int i = 0; i < 25; i++)
		crtc(s, i, cr[i]);
	for (int i = 0; i < 9; i++) {
		vga_ioport_write(s, 0x3ce, i);
		vga_ioport_write(s, 0x3cf, gr[i]);
	}
	vga_ioport_read(s, 0x3da);
	for (int i = 0; i < 21; i++) {
		vga_ioport_write(s, 0x3c0, i);
		vga_ioport_write(s, 0x3c0, ar[i]);
	}
	vga_ioport_write(s, 0x3c0, 0x20);
}

static void mode_12h(VGAState *s)
{
	static const uint8_t cr[25] = { 0x5f, 0x4f, 0x50, 0x82, 0x54, 0x80, 0x0b, 0x3e,
		0x00, 0x40, 0, 0, 0, 0, 0, 0, 0xea, 0x8c, 0xdf, 0x28, 0x00, 0xe7, 0x04, 0xe3, 0xff };
	static const uint8_t ar[21] = { 0, 1, 2, 3, 4, 5, 0x14, 7, 0x38, 0x39, 0x3a, 0x3b,
		0x3c, 0x3d, 0x3e, 0x3f, 0x01, 0x00, 0x0f, 0x00, 0x00 };
	static const uint8_t sr[5] = { 0x03, 0x01, 0x0f, 0x00, 0x06 };
	static const uint8_t gr[9] = { 0, 0, 0, 0, 0, 0, 0x05, 0x0f, 0xff };
	set_regs(s, 0xe3, sr, cr, gr, ar);
}

static void mode_13h(VGAState *s)
{
	static const uint8_t cr[25] = { 0x5f, 0x4f, 0x50, 0x82, 0x54, 0x80, 0xbf, 0x1f,
		0x00, 0x41, 0, 0, 0, 0, 0, 0, 0x9c, 0x8e, 0x8f, 0x28, 0x40, 0x96, 0xb9, 0xa3, 0xff };
	static const uint8_t ar[21] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
		0x41, 0x00, 0x0f, 0x00, 0x00 };
	static const uint8_t sr[5] = { 0x03, 0x01, 0x0f, 0x00, 0x0e };
	static const uint8_t gr[9] = { 0, 0, 0, 0, 0, 0x40, 0x05, 0x0f, 0xff };
	set_regs(s, 0x63, sr, cr, gr, ar);
}

/* mode 01h, 40x25 text: SR01 bit 3 (half dot clock), CR01 = 0x27 */
static void mode_01h(VGAState *s)
{
	static const uint8_t cr[25] = { 0x2d, 0x27, 0x28, 0x90, 0x2b, 0xa0, 0xbf, 0x1f,
		0x00, 0x4f, 0x0d, 0x0e, 0, 0, 0, 0, 0x9c, 0x8e, 0x8f, 0x14, 0x1f, 0x96, 0xb9, 0xa3, 0xff };
	static const uint8_t ar[21] = { 0, 1, 2, 3, 4, 5, 0x14, 7, 0x38, 0x39, 0x3a, 0x3b,
		0x3c, 0x3d, 0x3e, 0x3f, 0x0c, 0x00, 0x0f, 0x08, 0x00 };
	static const uint8_t sr[5] = { 0x03, 0x08, 0x03, 0x00, 0x02 };
	static const uint8_t gr[9] = { 0, 0, 0, 0, 0, 0x10, 0x0e, 0x00, 0xff };
	set_regs(s, 0x67, sr, cr, gr, ar);
}

static void vbe(VGAState *s, int w, int h, int bpp)
{
	vbe_write(s, 0, 4); vbe_write(s, 1, 0);
	vbe_write(s, 0, 1); vbe_write(s, 1, w);
	vbe_write(s, 0, 2); vbe_write(s, 1, h);
	vbe_write(s, 0, 3); vbe_write(s, 1, bpp);
	vbe_write(s, 0, 4); vbe_write(s, 1, 0x41);
}

int main(void)
{
	static uint8_t vram[4 * 1024 * 1024 + VGA_MEM_SLACK];
	VGAState *s;
	int x0, y0, x1, y1;
	char msg[200];

	printf("vga.c with board_dosboard.h: frame buffer %dx%d\n", W, H);

	/* 80x25 text, every cell a white block */
	s = vga_init((char *) vram, 256 * 1024, (uint8_t *) fb, W, H);
	for (int i = 0; i < 80 * 25; i++) {
		vram[i * 4] = 0xdb;
		vram[i * 4 + 1] = 0x0f;
	}
	/* the font: glyph 0xdb all lines lit (plane 2, 32 bytes a glyph) */
	for (int l = 0; l < 16; l++)
		vram[(0xdb * 32 + l) * 4 + 2] = 0xff;
	refresh(s);
	bbox(&x0, &y0, &x1, &y1);
	snprintf(msg, sizeof(msg), "80x25 text: drawn x %d..%d, y %d..%d (want 40..759, 100..499)",
		 x0, x1, y0, y1);
	check(x0 == 40 && x1 == 759 && y0 == 100 && y1 == 499, msg);

	/* 40x25 text */
	mode_01h(s);
	memset(fb, 0, sizeof(fb));
	for (int i = 0; i < 40 * 25; i++) {
		vram[i * 4] = 0xdb;
		vram[i * 4 + 1] = 0x0f;
	}
	refresh(s);
	refresh(s);
	bbox(&x0, &y0, &x1, &y1);
	snprintf(msg, sizeof(msg), "40x25 text: drawn x %d..%d, y %d..%d (want 40..759, 100..499: "
		 "each dot doubled)", x0, x1, y0, y1);
	check(x0 == 40 && x1 == 759 && y0 == 100 && y1 == 499, msg);
	int r = 0, edges = 0, last = 0;
	for (uint32_t t = 0; t < 1000000; t += 3) {
		fake_us = 5000000 + t;
		int vr = (vga_ioport_read(s, 0x3da) >> 3) & 1;
		if (vr && !last)
			edges++;
		last = vr;
	}
	snprintf(msg, sizeof(msg), "40x25 text: %d retraces a second (want 70)", edges);
	check(edges >= 69 && edges <= 71, msg);
	(void) r;

	/* mode 12h */
	memset(vram, 0, 256 * 1024);
	mode_12h(s);
	for (int p = 0; p < 4; p++)
		for (int i = 0; i < 80 * 480; i++)
			vram[i * 4 + p] = 0xff;
	refresh(s);
	bbox(&x0, &y0, &x1, &y1);
	snprintf(msg, sizeof(msg), "640x480x16: drawn x %d..%d, y %d..%d (want 80..719, 60..539)",
		 x0, x1, y0, y1);
	check(x0 == 80 && x1 == 719 && y0 == 60 && y1 == 539, msg);

	/* mode 13h */
	memset(vram, 0, 256 * 1024);
	memset(fb, 0, sizeof(fb));
	s = vga_init((char *) vram, 256 * 1024, (uint8_t *) fb, W, H);
	mode_13h(s);
	for (int i = 0; i < 320 * 200; i++)
		vram[i] = 15;
	refresh(s);
	bbox(&x0, &y0, &x1, &y1);
	snprintf(msg, sizeof(msg), "320x200x256: drawn x %d..%d, y %d..%d (want 80..719, 100..499)",
		 x0, x1, y0, y1);
	check(x0 == 80 && x1 == 719 && y0 == 100 && y1 == 499, msg);

	/* VBE 800x600x16: whole */
	memset(fb, 0, sizeof(fb));
	s = vga_init((char *) vram, 4 * 1024 * 1024, (uint8_t *) fb, W, H);
	refresh(s);
	vbe(s, 800, 600, 16);
	for (int i = 0; i < 800 * 600; i++) {
		vram[i * 2] = 0xff;
		vram[i * 2 + 1] = 0xff;
	}
	refresh(s);
	bbox(&x0, &y0, &x1, &y1);
	snprintf(msg, sizeof(msg), "VBE 800x600x16: drawn x %d..%d, y %d..%d (want 0..799, 0..599)",
		 x0, x1, y0, y1);
	check(x0 == 0 && x1 == 799 && y0 == 0 && y1 == 599, msg);

	/* VBE 1024x768x16: scaled to 800x600 */
	memset(fb, 0, sizeof(fb));
	vbe(s, 1024, 768, 16);
	for (int i = 0; i < 1024 * 768; i++) {
		vram[i * 2] = 0xff;
		vram[i * 2 + 1] = 0xff;
	}
	refresh(s);
	bbox(&x0, &y0, &x1, &y1);
	snprintf(msg, sizeof(msg), "VBE 1024x768x16: drawn x %d..%d, y %d..%d (want 0..799, 0..599, scaled)",
		 x0, x1, y0, y1);
	check(x0 == 0 && x1 == 799 && y0 == 0 && y1 == 599, msg);

	/* VBE 640x480x8 */
	memset(fb, 0, sizeof(fb));
	vbe(s, 640, 480, 8);
	memset(vram, 15, 640 * 480);
	refresh(s);
	bbox(&x0, &y0, &x1, &y1);
	snprintf(msg, sizeof(msg), "VBE 640x480x8: drawn x %d..%d, y %d..%d (want 80..719, 60..539)",
		 x0, x1, y0, y1);
	check(x0 == 80 && x1 == 719 && y0 == 60 && y1 == 539, msg);

	printf(failures ? "test_vga_dosboard: %d FAILED\n" : "test_vga_dosboard: all passed\n",
	       failures);
	return failures != 0;
}
