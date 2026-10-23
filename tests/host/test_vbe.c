/*
 * Host test for VBE pictures drawn from the blocks of video memory written
 * (VGA_DIRTY_BLOCKS, v4.0), against the old drawing, pixel for pixel.
 *
 * vga.c is linked three times over:
 *
 *  - "new": this tree's vga.c with the board header as the firmware builds
 *    it (VGA_DIRTY_BLOCKS on): only the rows written are drawn again;
 *  - "ref": the same vga.c with VGA_DIRTY_BLOCKS taken out, which draws
 *    every pixel of every picture with the old loop (its symbols renamed
 *    ref_*);
 *  - "base": vga.c of v4.0-base (base_*), when the build gives it, to show
 *    that "ref" draws what v4.0 drew before this change.
 *
 * Each gets its own copy of video memory and its own screen, and the same
 * things happen to all of them: VBE modes at 8, 15, 16, 24 and 32 bits a
 * pixel, smaller than the screen (centred), the screen's own size (1:1)
 * and larger (shrunk), video memory of 768K, 1M, 1536K and 2M, writes of
 * 8, 16 and 32 bits and of strings through the bank window and through the
 * linear frame buffer (stored straight into video memory and marked, as
 * pc.c does), palette changes, the display start, the line length, the
 * depth and the size changed under it, and the caller's full_update.
 * After every picture the screens must be the same, and every row of the
 * new screen that changed must have been given to redraw() (so that the
 * board writes it back to the panel).  Then a second thread writes the
 * linear frame buffer while the first draws, as the board's two cores do:
 * when it stops, one more picture must equal a full redraw.
 *
 * The bank window at 768K, 1152K and 1536K: every bank lands where its
 * number says (the mask of v4.0-base put banks 4 to 7 over 0 to 3 at 768K).
 *
 * v4.1: a store through the window marks nothing while VBE is off (mode
 * 13h's case) or vbe_blocks is 0, and what was stored unmarked is drawn
 * once VBE is on again; and stores made with chain 4 switched off under
 * VBE (which go through the planar path, unmarked) are drawn at the next
 * picture after chain 4 is back on.
 *
 * v4.2: vbe_blocks changed while the PC runs (vga_set_dirty_blocks(), the
 * board's Ctrl+Alt+F11): to 0 and back to 1 with no picture between, the
 * stores made while it was 0 (unmarked) are drawn; and a store that read
 * the old 0 and lands after the first picture is drawn by the second.
 *
 * Exit status 0 when every check passes.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "vga.h"
#include "pci.h"

#ifndef VGA_DIRTY_BLOCKS
#error "test_vbe.c is built with a board header that has VGA_DIRTY_BLOCKS"
#endif

/* ==== what vga.c needs from the rest of the emulator ==== */

static uint32_t fake_us;
uint32_t get_uticks(void)
{
	return __atomic_load_n(&fake_us, __ATOMIC_RELAXED);
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

/* ==== the three builds of vga.c ==== */

#define DECLARE(p) \
	VGAState *p##vga_init(char *, int, uint8_t *, int, int); \
	void p##vga_refresh(VGAState *, SimpleFBDrawFunc *, void *, int); \
	void p##vbe_write(VGAState *, uint32_t, uint32_t); \
	uint32_t p##vbe_read(VGAState *, uint32_t); \
	void p##vga_ioport_write(VGAState *, uint32_t, uint32_t); \
	uint32_t p##vga_ioport_read(VGAState *, uint32_t); \
	void p##vga_mem_write(VGAState *, uint32_t, uint8_t); \
	uint8_t p##vga_mem_read(VGAState *, uint32_t); \
	void p##vga_mem_write16(VGAState *, uint32_t, uint16_t); \
	void p##vga_mem_write32(VGAState *, uint32_t, uint32_t); \
	bool p##vga_mem_write_string(VGAState *, uint32_t, uint8_t *, int);
DECLARE(ref_)
#ifdef WITH_BASE
DECLARE(base_)
#endif

struct api {
	const char *name;
	VGAState *(*init)(char *, int, uint8_t *, int, int);
	void (*refresh)(VGAState *, SimpleFBDrawFunc *, void *, int);
	void (*vbe_write)(VGAState *, uint32_t, uint32_t);
	uint32_t (*vbe_read)(VGAState *, uint32_t);
	void (*io_write)(VGAState *, uint32_t, uint32_t);
	uint32_t (*io_read)(VGAState *, uint32_t);
	void (*mem_write)(VGAState *, uint32_t, uint8_t);
	uint8_t (*mem_read)(VGAState *, uint32_t);
	void (*mem_write16)(VGAState *, uint32_t, uint16_t);
	void (*mem_write32)(VGAState *, uint32_t, uint32_t);
	bool (*mem_write_string)(VGAState *, uint32_t, uint8_t *, int);
	bool marks;
};

#define API(n, p, m) { n, p##vga_init, p##vga_refresh, p##vbe_write, p##vbe_read, \
	p##vga_ioport_write, p##vga_ioport_read, p##vga_mem_write, p##vga_mem_read, \
	p##vga_mem_write16, p##vga_mem_write32, p##vga_mem_write_string, m }

static const struct api api_new = API("new", , true);
static const struct api api_ref = API("ref", ref_, false);
#ifdef WITH_BASE
static const struct api api_base = API("base", base_, false);
#endif

/* ==== checks ==== */

static int failures, checks;
static const char *out_dir;

static void check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		failures++;
		printf("  FAIL: %s\n", what);
	}
}

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

#define W LCD_WIDTH
#define H LCD_HEIGHT
#define VRAM_MAX (2048 * 1024)

/* one emulated VGA: its video memory and its screen */
struct inst {
	const struct api *a;
	VGAState *s;
	uint8_t *vram;
	uint16_t *fb;
	uint8_t *map;
	int size;
};

static uint8_t covered[H];      /* rows given to redraw() in this picture */
static int redraw_calls;

static void redraw(void *opaque, int x, int y, int w, int h)
{
	(void) opaque;
	redraw_calls++;
	if (x != 0 || w != W || y < 0 || h < 0 || y + h > H) {
		check(0, "redraw() gets whole rows inside the screen");
		return;
	}
	memset(covered + y, 1, h);
}

static void inst_init(struct inst *t, const struct api *a, int size)
{
	t->a = a;
	t->size = size;
	if (!t->vram) {
		t->vram = calloc(1, VRAM_MAX + VGA_MEM_SLACK);
		t->fb = calloc(W * H, sizeof(uint16_t));
	}
	memset(t->vram, 0, VRAM_MAX + VGA_MEM_SLACK);
	for (int i = 0; i < W * H; i++)
		t->fb[i] = 0x1234;      /* what was there before */
	t->s = a->init((char *) t->vram, size, (uint8_t *) t->fb, W, H);
	t->map = a->marks ? vga_dirty_map(t->s) : NULL;
	/* the VGA's text mode as vga_init() leaves it, drawn once */
	a->refresh(t->s, redraw, NULL, 0);
}

static int ninst;
static struct inst insts[3];

static void each_vbe(int index, int val)
{
	for (int i = 0; i < ninst; i++) {
		insts[i].a->vbe_write(insts[i].s, 0, index);
		insts[i].a->vbe_write(insts[i].s, 1, val);
	}
}

static void each_io(int port, int val)
{
	for (int i = 0; i < ninst; i++)
		insts[i].a->io_write(insts[i].s, port, val);
}

/* the linear frame buffer: straight into video memory, then the mark
   unless vga_dirty_blocks is 0, as pc.c's iomem_write*() do */
static void each_lfb(uint32_t off, const uint8_t *p, int n)
{
	for (int i = 0; i < ninst; i++) {
		struct inst *t = &insts[i];
		if (off + n > (uint32_t) t->size)
			continue;
		memcpy(t->vram + off, p, n);
		if (t->map && vga_dirty_blocks)
			vga_dirty_mark(t->map, off, n);
	}
}

/* the bank window at A0000h (vga_mem_write*() take the address less
   A0000h) */
static void each_win(uint32_t off, const uint8_t *p, int n, int how)
{
	for (int i = 0; i < ninst; i++) {
		struct inst *t = &insts[i];
		switch (how) {
		case 0:
			for (int k = 0; k < n; k++)
				t->a->mem_write(t->s, off + k, p[k]);
			break;
		case 1:
			for (int k = 0; k + 1 < n; k += 2)
				t->a->mem_write16(t->s, off + k, p[k] | (p[k + 1] << 8));
			break;
		case 2:
			for (int k = 0; k + 3 < n; k += 4)
				t->a->mem_write32(t->s, off + k, p[k] | (p[k + 1] << 8) |
						  (p[k + 2] << 16) | ((uint32_t) p[k + 3] << 24));
			break;
		default:
			if (!t->a->mem_write_string(t->s, off, (uint8_t *) p, n))
				check(0, "a string through the window in chain 4 is taken");
			break;
		}
	}
}

/* every instance draws a picture; the screens must be the same, and every
   row of the new one that changed must have gone to redraw() */
static uint16_t prev[W * H];
static int pictures, rows_drawn;

static bool picture(const char *what, int full_update)
{
	static char msg[300];
	fake_us += 30000;
	memcpy(prev, insts[0].fb, sizeof(prev));
	for (int i = 0; i < ninst; i++) {
		memset(covered, 0, sizeof(covered));
		redraw_calls = 0;
		insts[i].a->refresh(insts[i].s, redraw, NULL, full_update);
		if (i == 0) {
			int missed = -1;
			for (int y = 0; y < H; y++) {
				if (covered[y])
					rows_drawn++;
				if (!covered[y] && memcmp(prev + y * W, insts[0].fb + y * W, W * 2)) {
					missed = y;
					break;
				}
			}
			snprintf(msg, sizeof(msg), "%s: row %d changed and was not given to redraw()",
				 what, missed);
			check(missed < 0, msg);
		}
	}
	pictures++;
	bool same = true;
	for (int i = 1; i < ninst; i++) {
		if (memcmp(insts[0].fb, insts[i].fb, W * H * 2)) {
			int at = 0;
			while (insts[0].fb[at] == insts[i].fb[at])
				at++;
			snprintf(msg, sizeof(msg), "%s: %s and %s differ, first at (%d,%d): %04x %04x",
				 what, insts[0].a->name, insts[i].a->name, at % W, at / W,
				 insts[0].fb[at], insts[i].fb[at]);
			check(0, msg);
			same = false;
		}
	}
	return same;
}

static uint32_t rng = 12345;
static uint32_t rnd(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return rng;
}

static void set_mode(int w, int h, int bpp, bool dac8)
{
	each_vbe(4, 0);                         /* off, as the BIOS does first */
	each_vbe(1, w);
	each_vbe(2, h);
	each_vbe(3, bpp);
	each_vbe(4, 0x41 | (dac8 ? 0x20 : 0));  /* on, linear, video memory cleared */
}

static int bytes_of(int bpp)
{
	return bpp == 15 ? 2 : bpp / 8;
}

/* a palette for 8 bits a pixel, through the DAC */
static void set_palette(uint32_t seed)
{
	each_io(0x3c8, 0);
	for (int i = 0; i < 256 * 3; i++)
		each_io(0x3c9, (i * 7 + seed * (i / 3 + 1)) & 0x3f);
}

/* random writes: through the linear frame buffer or the bank window, 8, 16
   or 32 bits or a string, here and there in what the picture shows */
static void scribble(int rounds, uint32_t shown_from, uint32_t shown_bytes, int size)
{
	uint8_t buf[4096];
	for (int r = 0; r < rounds; r++) {
		int n;
		int kind = rnd() % 10;
		uint32_t off = shown_from + rnd() % (shown_bytes ? shown_bytes : 1);
		switch (kind) {
		case 0: n = 1; break;
		case 1: n = 2; break;
		case 2: n = 4; break;
		case 3: n = 1 + rnd() % 64; break;
		case 4: n = 512 + rnd() % 3584; break;
		default: n = 1 + rnd() % 16; break;
		}
		for (int k = 0; k < n; k++)
			buf[k] = rnd() >> 7;
		if (off + n > (uint32_t) size)
			off = size - n;
		if (rnd() % 2) {
			each_lfb(off, buf, n);
		} else {
			/* through the window: the bank, then at most to its end */
			uint32_t bank = off >> 16, in = off & 0xffff;
			if (in + n > 0x10000)
				n = 0x10000 - in;
			each_vbe(5, bank);
			int how = n >= 4 && kind >= 3 && rnd() % 2 ? 3 : (int) (rnd() % 3);
			each_win(in, buf, n, how);
		}
	}
}

/* one mode: drawn, then written at random picture after picture, with the
   things that change how it is shown */
static void run_mode(int size, int w, int h, int bpp, int rounds)
{
	char what[200];
	int bytes = bytes_of(bpp);
	uint32_t line = (uint32_t) w * bytes;
	snprintf(what, sizeof(what), "%dx%dx%d in %dK", w, h, bpp, size / 1024);
	set_mode(w, h, bpp, rnd() % 2);
	if (bpp == 8)
		set_palette(1);
	bool ok = picture(what, 0);
	/* a picture of every pixel first */
	uint32_t shown = line * h;
	if (shown > (uint32_t) size)
		shown = size;
	scribble(200, 0, shown, size);
	ok &= picture(what, 0);
	for (int r = 0; r < rounds && ok; r++) {
		int ev = rnd() % 20;
		uint32_t start = 0;
		if (ev == 0 && bpp == 8) {
			set_palette(rnd());
		} else if (ev == 1) {
			/* the display start, as a driver's page flip or panning */
			int yo = rnd() % 3 == 0 ? 0 : rnd() % 64;
			each_vbe(9, yo);
			each_vbe(8, rnd() % 2 ? 0 : (rnd() % 8) * 8);
		} else if (ev == 2) {
			/* a longer line */
			each_vbe(6, w + (rnd() % 4) * 8);
		} else if (ev == 3) {
			each_vbe(6, w);
			each_vbe(8, 0);
			each_vbe(9, 0);
		} else if (ev == 5) {
			/* the board's [esp] vbe_blocks: the old loop for a while,
			   and back */
			vga_dirty_blocks = !vga_dirty_blocks;
		}
		scribble(1 + rnd() % 40, start, shown, size);
		ok &= picture(what, ev == 4);
		/* a picture with nothing written: nothing changes on the screen */
		if (r % 7 == 0) {
			memcpy(prev, insts[0].fb, sizeof(prev));
			ok &= picture(what, 0);
		}
	}
	each_vbe(6, w);
	each_vbe(8, 0);
	each_vbe(9, 0);
	vga_dirty_blocks = 1;
	ok &= picture(what, 0);
	printf("  %-24s %s\n", what, ok ? "the same in every picture" : "DIFFERENT");
}

/* ==== the bank window at sizes that are not a power of two ==== */

/* true if every bank's write landed where its number says and the
   register reads back the last bank */
static bool bank_test(const struct api *a, int size)
{
	struct inst t = { 0 };
	inst_init(&t, a, size);
	ninst = 1;
	insts[0] = t;
	set_mode(800, 480, 16, false);
	int banks = size >> 16;
	for (int b = 0; b < banks; b++) {
		a->vbe_write(t.s, 0, 5);
		a->vbe_write(t.s, 1, b);
		a->mem_write16(t.s, 0x1234, 0xbe00 | b);
	}
	bool ok = true;
	for (int b = 0; b < banks; b++) {
		uint32_t at = ((uint32_t) b << 16) + 0x1234;
		if (t.vram[at] != b || t.vram[at + 1] != 0xbe)
			ok = false;
	}
	a->vbe_write(t.s, 0, 5);
	a->vbe_write(t.s, 1, banks - 1);
	a->vbe_write(t.s, 0, 5);
	ok &= a->vbe_read(t.s, 1) == (uint32_t) banks - 1;
	printf("  %s, %dK: %d banks %s\n", a->name, size / 1024, banks,
	       ok ? "each in its place" : "not each in its place");
	free(t.vram);
	free(t.fb);
	return ok;
}

/* ==== v4.1: marks only where a picture reads them ==== */

/* blocks of video memory marked in the new instance's map */
static int marked_blocks(void)
{
	struct inst *t = &insts[0];
	int n = 0;
	for (int b = 0; b < (t->size >> VGA_DIRTY_SHIFT); b++)
		n += t->map[b] != 0;
	return n;
}

static void clear_marks(void)
{
	memset(insts[0].map, 0, insts[0].size >> VGA_DIRTY_SHIFT);
}

/* stores through the window into banks 0 to banks - 1: 8, 16 and 32 bits
   and, with chain 4, a string (how up to hows - 1) */
static void window_stores(int banks, int hows, uint8_t seed)
{
	uint8_t buf[64];
	for (int k = 0; k < (int) sizeof(buf); k++)
		buf[k] = seed + k * 3;
	for (int b = 0; b < banks; b++) {
		each_vbe(5, b);
		for (int how = 0; how < hows; how++)
			each_win(0x400 * (how + 1) + b * 8, buf, sizeof(buf), how);
	}
}

static void marks_where_read(void)
{
	char msg[200];
	int size = 1024 * 1024;
	vga_dirty_blocks = 1;
	/* bank_test() freed what insts[0] had last */
	insts[0].vram = NULL;
	insts[0].fb = NULL;
	for (int sharp = 0; sharp < 2; sharp++) {
		ninst = 0;
		inst_init(&insts[ninst++], &api_new, size);
		inst_init(&insts[ninst++], &api_ref, size);
		set_mode(320, 200, 8, false);
		set_palette(5);
		picture("320x200x8, VBE on", 0);
		/* VBE off, chain 4 still on: a 256-colour picture drawn by the
		   loop every time, as mode 13h's, which reads no mark */
		each_vbe(4, 0);
		if (!sharp)
			picture("320x200x8, VBE off", 0);
		clear_marks();
		window_stores(1, 4, 0x40 + sharp);
		snprintf(msg, sizeof(msg), "VBE off, chain 4 on: stores through the window made "
			 "(%s) and %d blocks marked (none wanted)",
			 insts[0].vram[0x400] == 0x40 + sharp ? "yes" : "NO", marked_blocks());
		check(insts[0].vram[0x400] == 0x40 + sharp && marked_blocks() == 0, msg);
		printf("  VBE off, chain 4 on, %s: %d blocks marked by stores through the window\n",
		       sharp ? "no picture since VBE was on" : "a picture drawn", marked_blocks());
		/* VBE on again, video memory kept: turning it on asks for a whole
		   picture, which shows what was stored unmarked.  With sharp, the
		   last picture was a VBE one, so nothing else would draw it. */
		each_vbe(4, 0xc1);
		snprintf(msg, sizeof(msg), "320x200x8, VBE on again with video memory kept (%s)",
			 sharp ? "no picture while it was off" : "a picture while it was off");
		bool ok = picture(msg, 0);
		printf("  %-60s %s\n", msg, ok ? "the same" : "DIFFERENT");
	}

	/* vbe_blocks = 0: no store marks anything; back to 1 after a picture */
	ninst = 0;
	inst_init(&insts[ninst++], &api_new, size);
	inst_init(&insts[ninst++], &api_ref, size);
	set_mode(W, H, 16, false);
	scribble(50, 0, (uint32_t) W * H * 2, size);
	picture("vbe_blocks 1", 0);
	clear_marks();
	vga_dirty_blocks = 0;
	window_stores(8, 4, 0x77);
	scribble(50, 0, (uint32_t) W * H * 2, size);
	snprintf(msg, sizeof(msg), "vbe_blocks 0: %d blocks marked by stores (none wanted)",
		 marked_blocks());
	check(marked_blocks() == 0, msg);
	printf("  vbe_blocks 0, VBE on: %d blocks marked by stores through the window and "
	       "the frame buffer\n", marked_blocks());
	bool ok = picture("vbe_blocks 0", 0);
	vga_dirty_blocks = 1;
	ok &= picture("vbe_blocks back to 1", 0);
	scribble(50, 0, (uint32_t) W * H * 2, size);
	ok &= picture("vbe_blocks 1, written", 0);
	printf("  vbe_blocks 0, then 1 again: %s\n", ok ? "the same in every picture" : "DIFFERENT");
}

/* ==== v4.2: vbe_blocks changed while the PC runs ==== */

/* a store into the linear frame buffer that read vga_dirty_blocks as 0:
   straight into video memory, not marked */
static void each_lfb_unmarked(uint32_t off, const uint8_t *p, int n)
{
	for (int i = 0; i < ninst; i++)
		memcpy(insts[i].vram + off, p, n);
}

static void blocks_switched(void)
{
	int size = 1024 * 1024;
	static const int depths[] = { 8, 16, 32 };
	for (unsigned di = 0; di < sizeof(depths) / sizeof(depths[0]); di++) {
		int bpp = depths[di];
		char what[200];
		uint8_t buf[256];
		ninst = 0;
		inst_init(&insts[ninst++], &api_new, size);
		inst_init(&insts[ninst++], &api_ref, size);
		vga_set_dirty_blocks(1);
		set_mode(W, H, bpp, false);
		if (bpp == 8)
			set_palette(11);
		uint32_t shown = (uint32_t) W * H * bytes_of(bpp);
		scribble(100, 0, shown, size);
		picture("vbe_blocks 1", 0);
		picture("vbe_blocks 1", 0);
		/* 0 and back to 1 between two pictures, stores while it was 0 */
		vga_set_dirty_blocks(0);
		for (int k = 0; k < 20; k++) {
			for (int j = 0; j < (int) sizeof(buf); j++)
				buf[j] = rnd() >> 5;
			each_lfb((rnd() % (shown / 4 - 64)) * 4, buf, sizeof(buf));
		}
		vga_set_dirty_blocks(1);
		snprintf(what, sizeof(what), "%dx%dx%d, vbe_blocks 0 and back to 1, no picture "
			 "between", W, H, bpp);
		bool ok = picture(what, 0);
		/* a store that read the old value and lands after that picture */
		for (int j = 0; j < (int) sizeof(buf); j++)
			buf[j] = rnd() >> 5;
		each_lfb_unmarked((shown / 2) & ~3u, buf, sizeof(buf));
		snprintf(what, sizeof(what), "%dx%dx%d, a store unmarked after the first picture "
			 "since the change", W, H, bpp);
		ok &= picture(what, 0);
		/* and on as usual */
		scribble(20, 0, shown, size);
		ok &= picture("vbe_blocks 1 again", 0);
		/* to 0 and left there: every picture whole */
		vga_set_dirty_blocks(0);
		scribble(20, 0, shown, size);
		ok &= picture("vbe_blocks 0", 0);
		each_lfb_unmarked(0, buf, sizeof(buf));
		ok &= picture("vbe_blocks 0, a store unmarked", 0);
		vga_set_dirty_blocks(1);
		ok &= picture("vbe_blocks 1", 0);
		printf("  %dx%dx%d, vbe_blocks switched while drawing: %s\n", W, H, bpp,
		       ok ? "the same in every picture" : "DIFFERENT");
	}
	vga_set_dirty_blocks(1);
}

/* ==== v4.1: chain 4 switched off and on again under VBE ==== */

static void chain4_switched(void)
{
	static const int modes[][3] = {
		{ 640, 480, 8 }, { W, H, 8 }, { W, H, 16 }, { 640, 480, 32 },
		{ 800, 600, 16 }, { 1024, 768, 8 },
	};
	int size = 2048 * 1024;
	vga_dirty_blocks = 1;
	for (unsigned mi = 0; mi < sizeof(modes) / sizeof(modes[0]); mi++) {
		int w = modes[mi][0], h = modes[mi][1], bpp = modes[mi][2];
		for (int between = 0; between < 2; between++) {
			char what[200];
			snprintf(what, sizeof(what), "%dx%dx%d, chain 4 off and on, %s", w, h, bpp,
				 between ? "a picture between" : "no picture between");
			ninst = 0;
			inst_init(&insts[ninst++], &api_new, size);
			inst_init(&insts[ninst++], &api_ref, size);
			set_mode(w, h, bpp, false);
			if (bpp == 8)
				set_palette(7);
			uint32_t shown = (uint32_t) w * h * bytes_of(bpp);
			scribble(100, 0, shown, size);
			picture(what, 0);
			each_io(0x3c4, 4);
			uint32_t sr4 = insts[0].a->io_read(insts[0].s, 0x3c5);
			each_io(0x3c5, sr4 & ~8u);      /* chain 4 off */
			if (between)
				picture(what, 0);
			/* 4,000 bytes through the window, by the planar path: four
			   bytes of video memory each */
			uint8_t buf[4000];
			for (int k = 0; k < (int) sizeof(buf); k++)
				buf[k] = rnd() >> 9;
			each_vbe(5, 0);
			each_win(0x2000, buf, sizeof(buf), 0);
			each_io(0x3c4, 4);
			each_io(0x3c5, sr4);            /* chain 4 on again */
			bool ok = picture(what, 0);
			printf("  %-52s %s\n", what, ok ? "the same" : "DIFFERENT");
		}
	}
}

/* ==== two threads: the emulator writing while the drawing draws ==== */

static volatile int writer_stop;
static struct inst *writer_t;
static uint32_t writer_bytes;

static void *writer(void *arg)
{
	(void) arg;
	uint32_t x = 98765;
	struct inst *t = writer_t;
	while (!__atomic_load_n(&writer_stop, __ATOMIC_RELAXED)) {
		x ^= x << 13; x ^= x >> 17; x ^= x << 5;
		uint32_t off = x % writer_bytes;
		int n = 1 + (x >> 24) % 8;
		if (off + n > writer_bytes)
			off = writer_bytes - n;
		for (int k = 0; k < n; k++)
			__atomic_store_n(&t->vram[off + k], (uint8_t) (x >> (k * 3)), __ATOMIC_RELAXED);
		vga_dirty_mark(t->map, off, n);
	}
	return NULL;
}

static void two_threads(int w, int h, int bpp, int size, int pictures_n)
{
	char msg[200];
	static uint16_t periodic[W * H];
	struct inst t = { 0 };
	inst_init(&t, &api_new, size);
	ninst = 1;
	insts[0] = t;
	set_mode(w, h, bpp, false);
	if (bpp == 8)
		set_palette(3);
	picture("two threads", 0);
	writer_t = &t;
	writer_bytes = (uint32_t) w * bytes_of(bpp) * h;
	if (writer_bytes > (uint32_t) size)
		writer_bytes = size;
	writer_stop = 0;
	pthread_t th;
	pthread_create(&th, NULL, writer, NULL);
	for (int i = 0; i < pictures_n; i++) {
		memset(covered, 0, sizeof(covered));
		vga_refresh(t.s, redraw, NULL, 0);
	}
	__atomic_store_n(&writer_stop, 1, __ATOMIC_RELAXED);
	pthread_join(th, NULL);
	vga_refresh(t.s, redraw, NULL, 0);      /* the writes after the last */
	memcpy(periodic, t.fb, sizeof(periodic));
	vga_refresh(t.s, redraw, NULL, 1);      /* every pixel worked out again */
	int diff = 0;
	for (int i = 0; i < W * H; i++)
		diff += periodic[i] != t.fb[i];
	snprintf(msg, sizeof(msg), "two threads, %dx%dx%d: after the writer stops, one more "
		 "picture is the whole picture (%d pixels differ)", w, h, bpp, diff);
	check(diff == 0, msg);
	printf("  %dx%dx%d, %d pictures while writing: %d pixels differ from a full redraw\n",
	       w, h, bpp, pictures_n, diff);
	free(t.vram);
	free(t.fb);
}

int main(int argc, char **argv)
{
	if (argc > 1)
		out_dir = argv[1];
	static const int sizes[] = { 768 * 1024, 1024 * 1024, 1536 * 1024, 2048 * 1024 };
	static const int depths[] = { 8, 15, 16, 24, 32 };
	static const int modes[][2] = {
		{ W, H }, { 640, 480 }, { 320, 200 }, { 800, 600 }, { 1024, 768 },
		{ 800, 480 }, { 512, 384 },
	};

	printf("VBE pictures from the blocks written against every pixel every time, "
	       "on a %dx%d screen\n", W, H);
	for (unsigned si = 0; si < sizeof(sizes) / sizeof(sizes[0]); si++) {
		int size = sizes[si];
		ninst = 0;
		struct inst *list[3] = { &insts[0], &insts[1], &insts[2] };
		inst_init(list[ninst++], &api_new, size);
		inst_init(list[ninst++], &api_ref, size);
#ifdef WITH_BASE
		/* v4.0-base's bank register is a mask: the same only at powers
		   of two */
		if (!(size & (size - 1)))
			inst_init(list[ninst++], &api_base, size);
#endif
		for (unsigned mi = 0; mi < sizeof(modes) / sizeof(modes[0]); mi++) {
			for (unsigned di = 0; di < sizeof(depths) / sizeof(depths[0]); di++) {
				int w = modes[mi][0], h = modes[mi][1], bpp = depths[di];
				if ((uint32_t) w * h * bytes_of(bpp) > (uint32_t) size)
					continue;       /* the BIOS leaves such a mode out */
				if (mi > 0 && w == W && h == H)
					continue;       /* the screen's size, done first */
				run_mode(size, w, h, bpp, 60);
			}
		}
		if (size == 1024 * 1024) {
			char name[64];
			snprintf(name, sizeof(name), "vbe-%dx%d-last.ppm", W, H);
			write_ppm(name, insts[0].fb, W, H);
		}
	}
	printf("  %d pictures compared, %d rows given to redraw() by the new drawing\n",
	       pictures, rows_drawn);

	printf("the bank window\n");
	static const int bank_sizes[] = { 768 * 1024, 1152 * 1024, 1536 * 1024,
					  1024 * 1024, 2048 * 1024 };
	for (unsigned i = 0; i < sizeof(bank_sizes) / sizeof(bank_sizes[0]); i++) {
		int size = bank_sizes[i];
		char msg[200];
		snprintf(msg, sizeof(msg), "%dK: each bank written where its number says, "
			 "and the register reads back the last", size / 1024);
		check(bank_test(&api_new, size), msg);
#ifdef WITH_BASE
		/* v4.0-base's mask: right at a power of two, wrong otherwise, which
		   shows that the test sees the fault */
		bool pow2 = !(size & (size - 1));
		snprintf(msg, sizeof(msg), "%dK: v4.0-base's banks %s", size / 1024,
			 pow2 ? "in their places too" : "not in their places (the fault)");
		check(bank_test(&api_base, size) == pow2, msg);
#endif
	}

	printf("marks only where a picture reads them (v4.1)\n");
	marks_where_read();
	printf("chain 4 switched under VBE (v4.1)\n");
	chain4_switched();
	printf("vbe_blocks changed while the PC runs (v4.2)\n");
	blocks_switched();

	printf("two threads\n");
	for (unsigned di = 0; di < sizeof(depths) / sizeof(depths[0]); di++)
		two_threads(W, H, depths[di], 2048 * 1024, 300);
	two_threads(1024, 768, 16, 2048 * 1024, 100);

	if (failures) {
		printf("test_vbe: %d of %d checks FAILED\n", failures, checks);
		return 1;
	}
	printf("test_vbe: all %d checks passed\n", checks);
	return 0;
}
