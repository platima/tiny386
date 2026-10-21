/*
 * Host test for the display side of vga.c as the Korvo-1 builds it
 * (board_korvo1.h: BUILD_ESP32, VGA_RETRACE_REALTIME, VGA_REDRAW_DIRTY_ROWS,
 * VGA_STORE_CHANGED, no FULL_UPDATE), with a fake clock:
 *
 *  - the retrace bits of port 3DAh against the clock: how many vertical
 *    retraces a second in 80x25 text (70) and in 640x480 (60), how long
 *    each lasts, the share of time bit 0 is set; 60 a second in a VBE mode
 *    and with nonsense in the CRT controller;
 *  - vga_step() always says "draw";
 *  - which rows vga_refresh() hands to redraw(): all after a clear, only
 *    the changed rows after that, none when nothing changed, in text and in
 *    the 16-colour graphics mode;
 *  - the clears the emulator's core asks for: a CRT controller write in a
 *    graphics mode is left out when it would change nothing, a VBE mode set
 *    always clears, and both are done by the drawing side.
 *
 * Exit status 0 when every check passes.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vga.h"
#include "pci.h"

/* ---- what vga.c needs from the rest of the emulator ---- */

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

/* ---- checks ---- */

static int failures;

static void check(int ok, const char *what)
{
	if (!ok) {
		failures++;
		printf("  FAIL: %s\n", what);
	}
}

#define W 800
#define H 480
#define VRAM (256 * 1024)
static uint16_t fb[W * H];
static uint8_t vram[VRAM + VGA_MEM_SLACK];

/* ---- retrace ---- */

struct retrace {
	int edges;              /* vertical retraces begun */
	double vr_us;           /* the longest stretch with bit 3 set */
	double de_share;        /* the share of the time bit 0 is set */
};

static struct retrace measure(VGAState *s, uint32_t from_us, uint32_t for_us)
{
	struct retrace r = { 0 };
	int last = -1, step = 3;
	uint32_t high_since = 0, de_n = 0, n = 0;
	for (uint32_t t = 0; t < for_us; t += step) {
		fake_us = from_us + t;
		int st = vga_ioport_read(s, 0x3da);
		int vr = (st >> 3) & 1;
		if (last == 0 && vr) {
			r.edges++;
			high_since = t;
		}
		if (last == 1 && !vr && r.edges && t - high_since > r.vr_us)
			r.vr_us = t - high_since;
		last = vr;
		de_n += st & 1;
		n++;
	}
	r.de_share = (double) de_n / n;
	return r;
}

static void crtc(VGAState *s, int index, int value)
{
	vga_ioport_write(s, 0x3d4, index);
	vga_ioport_write(s, 0x3d5, value);
}

/* the registers of mode 12h (640x480, 16 colours), as a VGA BIOS sets them */
static void set_mode_12h(VGAState *s)
{
	static const uint8_t cr[25] = {
		0x5f, 0x4f, 0x50, 0x82, 0x54, 0x80, 0x0b, 0x3e,
		0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0xea, 0x8c, 0xdf, 0x28, 0x00, 0xe7, 0x04, 0xe3,
		0xff };
	static const uint8_t ar[21] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
		0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
		0x01, 0x00, 0x0f, 0x00, 0x00 };
	static const uint8_t sr[5] = { 0x03, 0x01, 0x0f, 0x00, 0x06 };
	static const uint8_t gr[9] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0f, 0xff };

	vga_ioport_write(s, 0x3c2, 0xe3);
	for (int i = 0; i < 5; i++) {
		vga_ioport_write(s, 0x3c4, i);
		vga_ioport_write(s, 0x3c5, sr[i]);
	}
	crtc(s, 0x11, 0x0c);            /* CR0-7 writable */
	for (int i = 0; i < 25; i++)
		crtc(s, i, cr[i]);
	for (int i = 0; i < 9; i++) {
		vga_ioport_write(s, 0x3ce, i);
		vga_ioport_write(s, 0x3cf, gr[i]);
	}
	vga_ioport_read(s, 0x3da);      /* the attribute flip-flop to index */
	for (int i = 0; i < 21; i++) {
		vga_ioport_write(s, 0x3c0, i);
		vga_ioport_write(s, 0x3c0, ar[i]);
	}
	vga_ioport_write(s, 0x3c0, 0x20);  /* the picture on */
}

/* ---- redraw ---- */

static int calls, last_y, last_h;

static void redraw(void *opaque, int x, int y, int w, int h)
{
	(void) opaque;
	calls++;
	last_y = y;
	last_h = h;
	check(x == 0 && w == W, "redraw() gets whole rows");
}

static void refresh(VGAState *s)
{
	calls = 0;
	last_y = last_h = -1;
	fake_us += 30000;
	vga_refresh(s, redraw, NULL, 0);
}

static void expect_rows(const char *what, int y, int h)
{
	char msg[200];
	if (h == 0)
		snprintf(msg, sizeof(msg), "%s: no redraw() (got %d calls, rows %d+%d)",
			 what, calls, last_y, last_h);
	else
		snprintf(msg, sizeof(msg), "%s: redraw() of rows %d+%d (got %d calls, rows %d+%d)",
			 what, y, h, calls, last_y, last_h);
	if (h == 0)
		check(calls == 0, msg);
	else
		check(calls == 1 && last_y == y && last_h == h, msg);
	printf("  %s: %d call(s), rows %d+%d\n", what, calls, last_y, last_h);
}

int main(void)
{
	VGAState *s;
	struct retrace r;

	/* ---- retrace ---- */
	printf("retrace in 80x25 text (as vga_init() leaves it: 28 MHz, 9 dots, 449 lines)\n");
	s = vga_init((char *) vram, VRAM, (uint8_t *) fb, W, H);
	r = measure(s, 123456, 1000000);
	printf("  %d retraces in 1 s, each %.0f us, bit 0 set %.1f%% of the time\n",
	       r.edges, r.vr_us, 100 * r.de_share);
	check(r.edges >= 69 && r.edges <= 71, "about 70 retraces a second in text mode");
	check(r.vr_us > 900 && r.vr_us < 1400, "each retrace about 1.1 ms");
	check(r.de_share > 0.25 && r.de_share < 0.33, "bit 0: blanking about 29% of the time");
	check(vga_step(s) == 1, "vga_step() says draw");

	printf("retrace in 640x480 (mode 12h registers: 25 MHz, 8 dots, 525 lines)\n");
	set_mode_12h(s);
	r = measure(s, 7777, 1000000);
	printf("  %d retraces in 1 s, each %.0f us, bit 0 set %.1f%% of the time\n",
	       r.edges, r.vr_us, 100 * r.de_share);
	check(r.edges >= 59 && r.edges <= 61, "about 60 retraces a second at 640x480");
	check(r.vr_us > 900 && r.vr_us < 1400, "each retrace about 1.1 ms");

	printf("retrace with nonsense in the CRT controller\n");
	crtc(s, 0x11, 0x0c);            /* CR0-7 writable */
	crtc(s, 0x06, 0x00);
	crtc(s, 0x07, 0x00);
	r = measure(s, 0, 1000000);
	printf("  %d retraces in 1 s\n", r.edges);
	check(r.edges >= 59 && r.edges <= 61, "60 a second when the registers make no sense");
	set_mode_12h(s);

	/* ---- which rows are redrawn: text ---- */
	printf("rows redrawn in 80x25 text (720x400 at 40,40)\n");
	memset(vram, 0, sizeof(vram));
	s = vga_init((char *) vram, VRAM, (uint8_t *) fb, W, H);
	refresh(s);
	expect_rows("first picture", 0, H);
	crtc(s, 0x0e, (10 * 80) >> 8);         /* the cursor to row 10 */
	crtc(s, 0x0f, (10 * 80) & 0xff);
	refresh(s);
	expect_rows("cursor moved from row 0 to row 10", 40, 11 * 16);
	refresh(s);
	expect_rows("nothing changed (the cursor's row only)", 40 + 160, 16);
	vram[(10 * 80 + 5) * 4] = 'A';
	refresh(s);
	expect_rows("a letter on the cursor's row", 40 + 160, 16);
	vram[(20 * 80 + 79) * 4] = 'B';
	refresh(s);
	expect_rows("a letter on row 20", 40 + 160, 11 * 16);

	/* ---- graphics ---- */
	printf("rows redrawn in 640x480x16 (at 80,0)\n");
	set_mode_12h(s);
	refresh(s);
	expect_rows("into graphics: cleared, all of it", 0, H);
	refresh(s);
	expect_rows("nothing changed", 0, 0);
	vram[(100 * 80 + 3) * 4 + 1] = 0x81;   /* plane 1 of 8 pixels on line 100 */
	refresh(s);
	expect_rows("eight pixels on line 100", 100, 1);
	vram[(300 * 80 + 79) * 4 + 3] = 0xff;
	refresh(s);
	expect_rows("eight more on line 300", 300, 1);

	/* ---- the clears the emulator's core asks for ---- */
	printf("clears asked for by the emulator\n");
	crtc(s, 0x0e, 0x12);    /* what the BIOS does for every character */
	refresh(s);
	expect_rows("CRT controller written, same picture: clear left out", 0, 0);
	fb[5] = 0x1234;         /* something left at the side */
	crtc(s, 0x11, 0x0c);    /* CR0-7 writable (mode 12h protects them) */
	crtc(s, 0x01, 0x3f);    /* 512 wide: another place */
	refresh(s);
	expect_rows("picture made narrower: cleared, all of it", 0, H);
	check(fb[5] == 0, "the old picture cleared away");
	crtc(s, 0x01, 0x4f);
	refresh(s);
	expect_rows("and back to 640 wide", 0, H);

	vbe_write(s, 0, 1); vbe_write(s, 1, 640);       /* xres */
	vbe_write(s, 0, 2); vbe_write(s, 1, 480);       /* yres */
	vbe_write(s, 0, 3); vbe_write(s, 1, 8);         /* bpp */
	fb[W * 10 + 100] = 0xabcd;
	vbe_write(s, 0, 4); vbe_write(s, 1, 0x41);      /* on, linear */
	check(fb[W * 10 + 100] == 0xabcd, "a VBE mode set does not clear on the emulator's core");
	refresh(s);
	expect_rows("VBE 640x480x8 on: cleared by the drawing, all of it", 0, H);
	r = measure(s, 0, 1000000);
	printf("  VBE: %d retraces in 1 s\n", r.edges);
	check(r.edges >= 59 && r.edges <= 61, "60 retraces a second in a VBE mode");

	if (failures) {
		printf("test_vga: %d checks FAILED\n", failures);
		return 1;
	}
	printf("test_vga: all checks passed\n");
	return 0;
}
