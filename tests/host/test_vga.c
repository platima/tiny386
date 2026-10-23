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

/* ==== what vga.c needs from the rest of the emulator ==== */

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

/* ==== checks ==== */

static int failures;
static const char *out_dir;

/* the frame buffer as a picture, for a person to look at */
static void write_ppm(const char *name, const uint16_t *p, int w, int h)
{
	char path[512];
	if (!out_dir)
		return;
	snprintf(path, sizeof(path), "%s/%s", out_dir, name);
	FILE *f = fopen(path, "wb");
	if (!f)
		return;
	fprintf(f, "P6\n%d %d\n255\n", w, h);
	for (int i = 0; i < w * h; i++) {
		unsigned r = (p[i] >> 11) & 31, g = (p[i] >> 5) & 63, b = p[i] & 31;
		unsigned char px[3] = { r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2 };
		fwrite(px, 1, 3, f);
	}
	fclose(f);
	printf("  wrote %s\n", path);
}

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

/* ==== retrace ==== */

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

/* an 800x600 picture: a red and green ramp, white lines every 100 pixels
   and a white frame round the edge, so that a picture of the screen shows
   whether all of it is there */
static uint16_t pattern(int x, int y)
{
	if (x % 100 == 0 || y % 100 == 0 || x >= 796 || y >= 596 || x < 4 || y < 4)
		return 0xffff;
	int r = x * 31 / 799, g = y * 63 / 599;
	return (uint16_t) ((r << 11) | (g << 5) | 8);
}

/* the registers of mode 13h (320x200, 256 colours) */
static void set_mode_13h(VGAState *s)
{
	static const uint8_t cr[25] = {
		0x5f, 0x4f, 0x50, 0x82, 0x54, 0x80, 0xbf, 0x1f,
		0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x9c, 0x8e, 0x8f, 0x28, 0x40, 0x96, 0xb9, 0xa3,
		0xff };
	static const uint8_t ar[21] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
		0x41, 0x00, 0x0f, 0x00, 0x00 };
	static const uint8_t sr[5] = { 0x03, 0x01, 0x0f, 0x00, 0x0e };
	static const uint8_t gr[9] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0f, 0xff };

	vga_ioport_write(s, 0x3c2, 0x63);
	for (int i = 0; i < 5; i++) {
		vga_ioport_write(s, 0x3c4, i);
		vga_ioport_write(s, 0x3c5, sr[i]);
	}
	crtc(s, 0x11, 0x0e);
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

/* ==== redraw ==== */

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

int main(int argc, char **argv)
{
	VGAState *s;
	struct retrace r;

	if (argc > 1)
		out_dir = argv[1];

	/* ==== retrace ==== */
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

	/* ==== which rows are redrawn: text ==== */
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
	/* a picture of the whole text screen: every cell written */
	for (int row = 0; row < 25; row++)
		for (int col = 0; col < 80; col++) {
			const char *t = "tiny386 80x25 text at (40,40)  ";
			vram[(row * 80 + col) * 4] = t[col % 31];
			vram[(row * 80 + col) * 4 + 1] = (row % 7 + 9) | (row % 2 ? 0x10 : 0);
		}
	refresh(s);
	expect_rows("every cell changed", 40, 400);
	check(fb[39 * W + 400] == 0 && fb[440 * W + 400] == 0 && fb[200 * W + 39] == 0 &&
	      fb[200 * W + 760] == 0, "text: black outside 40..759 x 40..439");
	write_ppm("vga-text-80x25.ppm", fb, W, H);

	/* ==== graphics ==== */
	printf("rows redrawn in 640x480x16 (at 80,0)\n");
	set_mode_12h(s);
	memset(vram, 0, VRAM);  /* a black picture (the text and font gone) */
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
	/* where they are: 640 wide in the middle of 800, so column 3's eight
	   pixels are at x 104 to 111, the first and the last of them set */
	check(fb[100 * W + 104] != 0 && fb[100 * W + 111] != 0 &&
	      fb[100 * W + 105] == 0 && fb[100 * W + 103] == 0,
	      "640x480: the picture's x 24 is the screen's x 104");
	check(fb[300 * W + 80 + 632] != 0 && fb[300 * W + 80 + 639] != 0 &&
	      fb[300 * W + 80 + 640] == 0, "640x480: its last column at x 719");

	/* ==== the clears the emulator's core asks for ==== */
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

	/* ==== mode 13h: 320x200 doubled to 640x400 in the middle ==== */
	printf("mode 13h (320x200x256) placement\n");
	memset(vram, 0, sizeof(vram));
	s = vga_init((char *) vram, VRAM, (uint8_t *) fb, W, H);
	set_mode_13h(s);
	memset(vram, 0, VRAM);
	vram[0] = 15;           /* pixel (0,0) */
	vram[320 * 199 + 319] = 15;     /* pixel (319,199) */
	refresh(s);
	expect_rows("into mode 13h", 0, H);
	check(fb[40 * W + 80] != 0 && fb[40 * W + 81] == fb[40 * W + 80] &&
	      fb[41 * W + 80] == fb[40 * W + 80] && fb[41 * W + 81] == fb[40 * W + 80],
	      "13h: pixel (0,0) is the 2x2 block at (80,40)");
	check(fb[40 * W + 82] == 0 && fb[42 * W + 80] == 0 && fb[39 * W + 80] == 0 &&
	      fb[40 * W + 79] == 0, "13h: and nothing round it");
	check(fb[439 * W + 719] != 0 && fb[438 * W + 718] != 0 && fb[440 * W + 719] == 0 &&
	      fb[439 * W + 720] == 0, "13h: pixel (319,199) is the 2x2 block at (718,438)");
	for (int y = 0; y < 200; y++)   /* a picture: the 256 colours in bands */
		for (int x = 0; x < 320; x++)
			vram[y * 320 + x] = (x / 20) + (y / 25) * 16;
	refresh(s);
	write_ppm("vga-13h-320x200.ppm", fb, W, H);

	/* ==== larger than the panel: VBE 800x600x16, scaled to 640x480 ==== */
	printf("VBE 800x600x16 on an 800x480 panel\n");
	static uint8_t bigvram[1024 * 1024 + VGA_MEM_SLACK];
	s = vga_init((char *) bigvram, 1024 * 1024, (uint8_t *) fb, W, H);
	refresh(s);
	vbe_write(s, 0, 1); vbe_write(s, 1, 800);
	vbe_write(s, 0, 2); vbe_write(s, 1, 600);
	vbe_write(s, 0, 3); vbe_write(s, 1, 16);
	vbe_write(s, 0, 4); vbe_write(s, 1, 0x41);
	for (int y = 0; y < 600; y++)
		for (int x = 0; x < 800; x++) {
			uint16_t p = pattern(x, y);
			bigvram[(y * 800 + x) * 2] = p;
			bigvram[(y * 800 + x) * 2 + 1] = p >> 8;
		}
	refresh(s);
	expect_rows("VBE 800x600 on", 0, H);
	int good = 1;
	for (int dy = 0; dy < 480; dy += 7)
		for (int dx = 0; dx < 640; dx += 5) {
			int x = dx * 800 / 640, y = dy * 600 / 480;
			uint16_t want = pattern(x, y);
			if (fb[dy * W + 80 + dx] != want)
				good = 0;
		}
	check(good, "800x600: each screen pixel is the nearest picture pixel, 640x480 at (80,0)");
	check(fb[100 * W + 79] == 0 && fb[100 * W + 720] == 0,
	      "800x600: black either side");
	write_ppm("vga-vbe-800x600-scaled.ppm", fb, W, H);
	refresh(s);
	expect_rows("800x600, nothing changed", 0, 0);
	bigvram[(300 * 800 + 400) * 2] ^= 0xff;  /* a pixel on line 300 = row 240 */
	refresh(s);
	expect_rows("800x600, one pixel changed", 240, 1);

	/* ==== a VBE picture that runs past the end of video memory ==== */
	/* v4.0-base, from the merge with upstream: a line that begins past the
	   end is drawn black (v3.9 left it as it was; upstream draws it black
	   since 4348cfa), a line that runs past it is drawn as far as video
	   memory goes, then black (the slack's zeros) */
	printf("VBE 640x480x16 shown from line 400 of 1 MiB: past the end\n");
	memset(bigvram, 0, sizeof(bigvram));
	s = vga_init((char *) bigvram, 1024 * 1024, (uint8_t *) fb, W, H);
	refresh(s);
	vbe_write(s, 0, 1); vbe_write(s, 1, 640);
	vbe_write(s, 0, 2); vbe_write(s, 1, 480);
	vbe_write(s, 0, 3); vbe_write(s, 1, 16);
	vbe_write(s, 0, 4); vbe_write(s, 1, 0x41);
	memset(bigvram, 0x55, 1024 * 1024);     /* every pixel 0x5555 */
	refresh(s);
	/* 1 MiB is 819.2 lines of 1,280 bytes: from line 400, the screen's rows
	   0 to 418 are in video memory, row 419 has its first 128 pixels
	   there, rows 420 to 479 begin past the end */
	vbe_write(s, 0, 9); vbe_write(s, 1, 400);       /* Y offset */
	refresh(s);     /* the clear the offset asks for, and the picture */
	for (int i = 0; i < W * H; i++)
		fb[i] = 0x1234;                 /* something else on the screen */
	refresh(s);     /* no clear: every row of the picture drawn over it */
	int in = 1, part = 1, past = 1;
	for (int x = 0; x < 640; x++) {
		for (int y = 0; y < 419; y++)
			if (fb[y * W + 80 + x] != 0x5555)
				in = 0;
		if (fb[419 * W + 80 + x] != (x < 128 ? 0x5555 : 0))
			part = 0;
		for (int y = 420; y < 480; y++)
			if (fb[y * W + 80 + x] != 0)
				past = 0;
	}
	check(in, "past the end: rows 0 to 418 drawn from video memory");
	check(part, "past the end: row 419 its first 128 pixels, then black");
	check(past, "past the end: rows 420 to 479, which begin past it, black");
	printf("  rows 0 to 418 %s, row 419 %s, rows 420 to 479 %s\n", in ? "drawn" : "WRONG",
	       part ? "half drawn" : "WRONG", past ? "black" : "WRONG");

	if (failures) {
		printf("test_vga: %d checks FAILED\n", failures);
		return 1;
	}
	printf("test_vga: all checks passed\n");
	return 0;
}
