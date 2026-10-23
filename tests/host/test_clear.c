/*
 * Host test for the clears of v4.2 (VGA_CLEAR_AROUND in vga.c): a clear of
 * the screen is made by the picture that follows it, which is drawn whole
 * over what the screen shows and makes only the rest of the screen black.
 * On the board the screen is the panel's frame buffer, which the panel
 * shows all the time; v4.1's memset() of it reached the panel in part before
 * the picture was drawn again, a black flash at every clear (a VBE mode set
 * above all, twice each time Windows starts its display driver).
 *
 * vga.c is linked twice over:
 *
 *  - "new": the vga.c under test with the board header, as the firmware
 *    builds it;
 *  - "ref": this tree's vga.c with VGA_NO_CLEAR_AROUND, whose clears are
 *    the memset() of v4.1 (its symbols renamed ref_*).
 *
 * The same things happen to both: text, the 16-colour mode 12h, mode 13h,
 * VBE modes of 8 to 32 bits smaller than the screen, its own size and
 * larger (shrunk), the screen blanked and back, the caller's full_update,
 * the CRT controller written, and VBE modes set again over themselves.
 * After every picture:
 *
 *  1. the two screens must be the same: a clear made by the picture after it
 *     leaves what a memset() and that picture leave;
 *  2. every row of the new screen that changed must have gone to redraw();
 *  3. no page of the new screen may have been stored into without a pixel in
 *     it changing.  The screen is made read-only before each picture; a
 *     store faults, the page is noted and made writable, and the store goes
 *     on.  A page stored into whose pixels are all as they were means pixels
 *     stored and stored back within the picture: on the board, black that
 *     the panel can show.  And where the guest's picture has not changed at
 *     all (a mode set again over itself, the console's full_update), no page
 *     may be stored into.
 *
 * Exit status 0 when every check passes; 2 when only the checks of 3 fail
 * (the pictures right, the stores not: v4.1's vga.c, which the run checks
 * gives exactly that); 1 when a picture is wrong.
 */
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "vga.h"
#include "pci.h"

#ifndef VGA_DIRTY_BLOCKS
#error "test_clear.c is built with a board header that has VGA_DIRTY_BLOCKS"
#endif

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

/* ==== the two builds of vga.c ==== */

VGAState *ref_vga_init(char *, int, uint8_t *, int, int);
void ref_vga_refresh(VGAState *, SimpleFBDrawFunc *, void *, int);
void ref_vbe_write(VGAState *, uint32_t, uint32_t);
void ref_vga_ioport_write(VGAState *, uint32_t, uint32_t);
uint32_t ref_vga_ioport_read(VGAState *, uint32_t);
uint8_t *ref_vga_dirty_map(VGAState *);
extern int ref_vga_dirty_blocks;

struct api {
	const char *name;
	VGAState *(*init)(char *, int, uint8_t *, int, int);
	void (*refresh)(VGAState *, SimpleFBDrawFunc *, void *, int);
	void (*vbe_write)(VGAState *, uint32_t, uint32_t);
	void (*io_write)(VGAState *, uint32_t, uint32_t);
	uint32_t (*io_read)(VGAState *, uint32_t);
	uint8_t *(*dirty_map)(VGAState *);
	int *dirty_blocks;
};

static const struct api api_new = { "new", vga_init, vga_refresh, vbe_write,
	vga_ioport_write, vga_ioport_read, vga_dirty_map, &vga_dirty_blocks };
static const struct api api_ref = { "ref", ref_vga_init, ref_vga_refresh, ref_vbe_write,
	ref_vga_ioport_write, ref_vga_ioport_read, ref_vga_dirty_map, &ref_vga_dirty_blocks };

/* ==== checks ==== */

static int failures, store_failures, checks;

static void check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		failures++;
		printf("  FAIL: %s\n", what);
	}
}

static void check_stores(int ok, const char *what)
{
	checks++;
	if (!ok) {
		store_failures++;
		printf("  FAIL: %s\n", what);
	}
}

#define W LCD_WIDTH
#define H LCD_HEIGHT
#define VRAM (2048 * 1024)

/* ==== the new screen, watched page by page ==== */

static uint8_t *wfb;            /* the new instance's screen, page aligned */
static size_t wfb_len, pagesz, npages;
static uint8_t *touched;        /* a byte a page: stored into this picture */
static volatile sig_atomic_t watching;

static void on_segv(int sig, siginfo_t *si, void *ctx)
{
	(void) ctx;
	uint8_t *a = si->si_addr;
	if (watching && a >= wfb && a < wfb + wfb_len) {
		size_t pg = (size_t) (a - wfb) / pagesz;
		touched[pg] = 1;
		mprotect(wfb + pg * pagesz, pagesz, PROT_READ | PROT_WRITE);
		return;
	}
	/* a fault of anything else: the default action */
	signal(sig, SIG_DFL);
}

static void watch_setup(void)
{
	pagesz = (size_t) sysconf(_SC_PAGESIZE);
	wfb_len = ((size_t) W * H * 2 + pagesz - 1) / pagesz * pagesz;
	npages = wfb_len / pagesz;
	wfb = mmap(NULL, wfb_len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (wfb == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	touched = calloc(npages, 1);
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = on_segv;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGSEGV, &sa, NULL);
}

/* ==== the two instances ==== */

struct inst {
	const struct api *a;
	VGAState *s;
	uint8_t *vram;
	uint16_t *fb;
	uint8_t *map;
};

static struct inst insts[2];
static uint8_t covered[H];      /* rows given to redraw() in this picture */

static void redraw(void *opaque, int x, int y, int w, int h)
{
	(void) opaque;
	if (x != 0 || w != W || y < 0 || h < 0 || y + h > H) {
		check(0, "redraw() gets whole rows inside the screen");
		return;
	}
	memset(covered + y, 1, h);
}

static void start(int fill)
{
	for (int i = 0; i < 2; i++) {
		struct inst *t = &insts[i];
		t->a = i ? &api_ref : &api_new;
		if (!t->vram) {
			t->vram = calloc(1, VRAM + VGA_MEM_SLACK);
			t->fb = i ? calloc(W * H, sizeof(uint16_t)) : (uint16_t *) wfb;
		}
		memset(t->vram, 0, VRAM + VGA_MEM_SLACK);
		for (int k = 0; k < W * H; k++)
			t->fb[k] = fill;        /* what was there before: the console */
		*t->a->dirty_blocks = 1;
		t->s = t->a->init((char *) t->vram, VRAM, (uint8_t *) t->fb, W, H);
		t->map = t->a->dirty_map(t->s);
	}
}

static void each_vbe(int index, int val)
{
	for (int i = 0; i < 2; i++) {
		insts[i].a->vbe_write(insts[i].s, 0, index);
		insts[i].a->vbe_write(insts[i].s, 1, val);
	}
}

static void each_io(int port, int val)
{
	for (int i = 0; i < 2; i++)
		insts[i].a->io_write(insts[i].s, port, val);
}

static void each_ior(int port)
{
	for (int i = 0; i < 2; i++)
		insts[i].a->io_read(insts[i].s, port);
}

static void each_crtc(int index, int val)
{
	each_io(0x3d4, index);
	each_io(0x3d5, val);
}

/* video memory written straight, as the guest's stores do; marked as pc.c
   marks the linear frame buffer */
static void each_vram(uint32_t off, const uint8_t *p, uint32_t n)
{
	for (int i = 0; i < 2; i++) {
		memcpy(insts[i].vram + off, p, n);
		if (*insts[i].a->dirty_blocks)
			vga_dirty_mark(insts[i].map, off, n);
	}
}

static uint32_t rng = 4242;
static uint32_t rnd(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return rng;
}

static void each_random(uint32_t off, uint32_t n)
{
	static uint8_t buf[VRAM];
	for (uint32_t k = 0; k < n; k++)
		buf[k] = rnd() >> 11;
	each_vram(off, buf, n);
}

/*
 * One picture by each instance.  watch: the new screen is watched page by
 * page; still: the guest's picture has not changed, so no page may be
 * stored into at all.  Returns the pages stored into.
 */
static int pictures, watched_pictures, pages_stored, pages_needless;

static int picture(const char *what, int full_update, bool still)
{
	static uint16_t old[W * H];
	char msg[300];
	fake_us += 30000;
	memcpy(old, insts[0].fb, sizeof(old));
	memset(touched, 0, npages);
	memset(covered, 0, sizeof(covered));
	watching = 1;
	mprotect(wfb, wfb_len, PROT_READ);
	insts[0].a->refresh(insts[0].s, redraw, NULL, full_update);
	mprotect(wfb, wfb_len, PROT_READ | PROT_WRITE);
	watching = 0;
	insts[1].a->refresh(insts[1].s, redraw, NULL, full_update);
	pictures++;
	watched_pictures++;

	/* 1: the same as a memset() and the picture */
	if (memcmp(insts[0].fb, insts[1].fb, W * H * 2)) {
		int at = 0;
		while (insts[0].fb[at] == insts[1].fb[at])
			at++;
		snprintf(msg, sizeof(msg), "%s: new and ref differ, first at (%d,%d): %04x %04x",
			 what, at % W, at / W, insts[0].fb[at], insts[1].fb[at]);
		check(0, msg);
	}
	/* 2: every changed row to redraw() (the ref's redraw() came after the
	   new one's: covered is the union, and a superset is fine for 2) */
	for (int y = 0; y < H; y++) {
		if (!covered[y] && memcmp(old + y * W, insts[0].fb + y * W, W * 2)) {
			snprintf(msg, sizeof(msg), "%s: row %d changed and was not given to redraw()",
				 what, y);
			check(0, msg);
			break;
		}
	}
	/* 3: no page stored into without a change */
	int stored = 0, needless = 0, first = -1;
	for (size_t pg = 0; pg < npages; pg++) {
		if (!touched[pg])
			continue;
		stored++;
		size_t off = pg * pagesz, n = pagesz;
		if (off + n > (size_t) W * H * 2)
			n = (size_t) W * H * 2 - off;
		if (!memcmp((uint8_t *) old + off, (uint8_t *) insts[0].fb + off, n)) {
			needless++;
			if (first < 0)
				first = (int) pg;
		}
	}
	pages_stored += stored;
	pages_needless += needless;
	snprintf(msg, sizeof(msg), "%s: %d pages stored into with nothing in them changed, "
		 "the first at row %d", what, needless,
		 first < 0 ? -1 : (int) ((size_t) first * pagesz / (W * 2)));
	check_stores(needless == 0, msg);
	if (still) {
		snprintf(msg, sizeof(msg), "%s: the guest's picture as it was, %d pages stored into "
			 "(none wanted)", what, stored);
		check_stores(stored == 0, msg);
	}
	printf("  %-62s %4d pages stored, %d needlessly%s\n", what, stored, needless,
	       still ? " (a picture as it was)" : "");
	return stored;
}

/* ==== the modes, as a VGA BIOS sets them ==== */

static void set_regs(int misc, const uint8_t *sr, const uint8_t *cr, const uint8_t *gr,
		     const uint8_t *ar, bool blank_between)
{
	each_vbe(4, 0);                 /* VBE off */
	each_ior(0x3da);                /* the attribute flip-flop to index */
	each_io(0x3c0, 0x00);           /* the picture off while the mode is set */
	if (blank_between)
		picture("blanked while the mode is set", 0, false);
	each_io(0x3c2, misc);
	for (int i = 0; i < 5; i++) {
		each_io(0x3c4, i);
		each_io(0x3c5, sr[i]);
	}
	each_crtc(0x11, 0x0c);          /* CR0-7 writable */
	for (int i = 0; i < 25; i++)
		each_crtc(i, cr[i]);
	for (int i = 0; i < 9; i++) {
		each_io(0x3ce, i);
		each_io(0x3cf, gr[i]);
	}
	each_ior(0x3da);
	for (int i = 0; i < 21; i++) {
		each_io(0x3c0, i);
		each_io(0x3c0, ar[i]);
	}
	each_io(0x3c0, 0x20);           /* the picture on */
}

/* 80x25 text, 720x400, the cursor off (CR0Ah bit 5) */
static void set_text(bool blank_between)
{
	static const uint8_t sr[5] = { 0x03, 0x00, 0x03, 0x00, 0x02 };
	static const uint8_t cr[25] = {
		0x5f, 0x4f, 0x50, 0x82, 0x55, 0x81, 0xbf, 0x1f,
		0x00, 0x4f, 0x20, 0x0e, 0x00, 0x00, 0x00, 0x00,
		0x9c, 0x8e, 0x8f, 0x28, 0x1f, 0x96, 0xb9, 0xa3,
		0xff };
	static const uint8_t gr[9] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0e, 0x00, 0xff };
	static const uint8_t ar[21] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
		0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
		0x0c, 0x00, 0x0f, 0x08, 0x00 };
	set_regs(0x67, sr, cr, gr, ar, blank_between);
}

static void set_12h(bool blank_between)
{
	static const uint8_t sr[5] = { 0x03, 0x01, 0x0f, 0x00, 0x06 };
	static const uint8_t cr[25] = {
		0x5f, 0x4f, 0x50, 0x82, 0x54, 0x80, 0x0b, 0x3e,
		0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0xea, 0x8c, 0xdf, 0x28, 0x00, 0xe7, 0x04, 0xe3,
		0xff };
	static const uint8_t gr[9] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0f, 0xff };
	static const uint8_t ar[21] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
		0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
		0x01, 0x00, 0x0f, 0x00, 0x00 };
	set_regs(0xe3, sr, cr, gr, ar, blank_between);
}

static void set_13h(bool blank_between)
{
	static const uint8_t sr[5] = { 0x03, 0x01, 0x0f, 0x00, 0x0e };
	static const uint8_t cr[25] = {
		0x5f, 0x4f, 0x50, 0x82, 0x54, 0x80, 0xbf, 0x1f,
		0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x9c, 0x8e, 0x8f, 0x28, 0x40, 0x96, 0xb9, 0xa3,
		0xff };
	static const uint8_t gr[9] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0f, 0xff };
	static const uint8_t ar[21] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
		0x41, 0x00, 0x0f, 0x00, 0x00 };
	set_regs(0x63, sr, cr, gr, ar, blank_between);
}

/* a palette for the 256-colour modes, through the DAC */
static void set_palette(uint32_t seed)
{
	each_io(0x3c8, 0);
	for (int i = 0; i < 256 * 3; i++)
		each_io(0x3c9, (i * 5 + seed * (i / 3 + 1)) & 0x3f);
}

/* a VBE mode as a driver sets it: off, the size, on (linear, 8-bit DAC);
   keep: video memory not cleared (VBE_DISPI_NOCLEARMEM) */
static void set_vbe(int w, int h, int bpp, bool keep)
{
	each_vbe(4, 0);
	each_vbe(1, w);
	each_vbe(2, h);
	each_vbe(3, bpp);
	each_vbe(4, 0x61 | (keep ? 0x80 : 0));
}

static int bytes_of(int bpp)
{
	return bpp == 15 ? 2 : bpp / 8;
}

/* text: characters, colours and a font, no blinking or underlined ones
   (the underline and the blink are drawn over a cell after it, which is
   stored twice whatever the clear does) */
static void text_content(uint32_t seed)
{
	static uint8_t cells[80 * 25 * 4];
	static const uint8_t attrs[] = { 0x07, 0x1f, 0x2e, 0x4f, 0x70, 0x30, 0x0a, 0x5e };
	rng = seed;
	for (int i = 0; i < 80 * 25; i++) {
		cells[i * 4] = 32 + rnd() % 96;
		cells[i * 4 + 1] = attrs[rnd() % 8];
		cells[i * 4 + 2] = 0;
		cells[i * 4 + 3] = 0;
	}
	each_vram(0, cells, sizeof(cells));
	/* the font: plane 2, 32 lines a character */
	static uint8_t glyph[4];
	for (int ch = 32; ch < 128; ch++)
		for (int line = 0; line < 16; line++) {
			uint32_t off = (uint32_t) (ch * 32 + line) * 4;
			memcpy(glyph, insts[0].vram + off, 4);
			glyph[2] = (uint8_t) (rnd() >> 13);
			each_vram(off, glyph, 4);
		}
}

static void vbe_content(int w, int h, int bpp)
{
	uint32_t n = (uint32_t) w * h * bytes_of(bpp);
	if (n > VRAM)
		n = VRAM;
	each_random(0, n);
}

/* ==== the cases ==== */

/* a VBE mode set again over itself, video memory kept (Windows' driver sets
   its mode twice as it starts, the second time with nothing changed), and
   the size written again with VBE on */
static void vbe_again(int w, int h, int bpp)
{
	char what[200];
	start(0x1234);
	set_vbe(w, h, bpp, false);
	if (bpp == 8)
		set_palette(9);
	vbe_content(w, h, bpp);
	snprintf(what, sizeof(what), "VBE %dx%dx%d", w, h, bpp);
	picture(what, 0, false);
	picture(what, 0, true);
	set_vbe(w, h, bpp, true);
	snprintf(what, sizeof(what), "VBE %dx%dx%d set again, video memory kept", w, h, bpp);
	picture(what, 0, true);
	each_vbe(1, w);
	snprintf(what, sizeof(what), "VBE %dx%dx%d, its width written again", w, h, bpp);
	picture(what, 0, true);
	/* and a change after it is drawn, as any */
	each_random((uint32_t) (h / 2) * w * bytes_of(bpp), 64);
	snprintf(what, sizeof(what), "VBE %dx%dx%d, 64 bytes written", w, h, bpp);
	picture(what, 0, false);
}

/* the caller's full_update (the board's console gave the screen back over
   a black screen), then the CRT controller written, which the BIOS does
   for every character it prints: the picture is as it was */
static void full_then_crtc(const char *name, void (*set)(bool), int vbe_w, int vbe_h, int vbe_bpp)
{
	char what[200];
	start(0);
	if (set) {
		set(false);
		if (set == set_13h)
			set_palette(3);
		each_random(0, 256 * 1024);
	} else {
		set_vbe(vbe_w, vbe_h, vbe_bpp, false);
		if (vbe_bpp == 8)
			set_palette(4);
		vbe_content(vbe_w, vbe_h, vbe_bpp);
	}
	picture(name, 0, false);
	picture(name, 0, true);
	snprintf(what, sizeof(what), "%s, the caller's full_update", name);
	picture(what, 1, true);
	each_crtc(0x0e, 0x12);
	snprintf(what, sizeof(what), "%s, then the CRT controller written", name);
	picture(what, 0, true);
}

/* a run of mode changes with something on the screen each time */
static void mode_changes(void)
{
	char what[200];
	start(0x5aa5);
	set_text(false);
	text_content(1);
	picture("80x25 text over the console", 0, false);
	picture("80x25 text, nothing changed", 0, true);
	for (int round = 0; round < 3; round++) {
		bool blank = round == 1;
		set_12h(blank);
		each_random(0, 256 * 1024);
		picture("640x480x16 (mode 12h)", 0, false);
		set_13h(blank);
		set_palette(round);
		each_random(0, 64000);
		picture("320x200x256 (mode 13h)", 0, false);
		set_vbe(640, 480, 8, true);
		picture("VBE 640x480x8 over mode 13h, video memory kept", 0, false);
		vbe_content(640, 480, 8);
		picture("VBE 640x480x8 written", 0, false);
		set_vbe(W, H, 16, false);
		vbe_content(W, H, 16);
		snprintf(what, sizeof(what), "VBE %dx%dx16, the screen's size", W, H);
		picture(what, 0, false);
		set_vbe(640, 480, 16, true);
		picture("VBE 640x480x16 over it, video memory kept", 0, false);
		set_vbe(1024, 768, 8, false);
		set_palette(round + 7);
		vbe_content(1024, 768, 8);
		picture("VBE 1024x768x8, shrunk", 0, false);
		set_vbe(800, 600, 32, true);
		picture("VBE 800x600x32 over it, video memory kept", 0, false);
		set_vbe(W, H, 24, true);
		snprintf(what, sizeof(what), "VBE %dx%dx24 over it, video memory kept", W, H);
		picture(what, 0, false);
		/* blanked through the attribute index and back */
		each_ior(0x3da);
		each_io(0x3c0, 0x00);
		picture("blanked", 0, false);
		each_ior(0x3da);
		each_io(0x3c0, 0x20);
		snprintf(what, sizeof(what), "VBE %dx%dx24 back", W, H);
		picture(what, 0, false);
		set_text(blank);
		text_content(round + 2);
		picture("80x25 text again", 0, false);
		/* the CRT controller made narrower and wider again, as a program
		   that sets its own text size (CR0-7 made writable first) */
		each_crtc(0x11, 0x0e);
		each_crtc(0x01, 0x3f);
		picture("text 64 columns wide", 0, false);
		each_crtc(0x01, 0x4f);
		picture("text 80 columns wide again", 0, false);
	}
}

int main(int argc, char **argv)
{
	(void) argc;
	(void) argv;
	watch_setup();
	printf("clears made by the picture after them, on a %dx%d screen\n", W, H);

	printf("a VBE mode set again over itself (nothing may be stored)\n");
	static const int modes[][3] = {
		{ W, H, 16 }, { W, H, 8 }, { W, H, 15 }, { W, H, 24 }, { W, H, 32 },
		{ 640, 480, 16 }, { 640, 480, 8 }, { 800, 600, 16 }, { 1024, 768, 8 },
		{ 1024, 768, 16 },
	};
	for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); i++)
		vbe_again(modes[i][0], modes[i][1], modes[i][2]);

	printf("the caller's full_update and the CRT controller (nothing may be stored)\n");
	full_then_crtc("640x480x16 (mode 12h)", set_12h, 0, 0, 0);
	full_then_crtc("320x200x256 (mode 13h)", set_13h, 0, 0, 0);
	full_then_crtc("VBE 640x480x16", NULL, 640, 480, 16);
	full_then_crtc("VBE 1024x768x16, shrunk", NULL, 1024, 768, 16);
	full_then_crtc("VBE 800x600x8", NULL, 800, 600, 8);

	printf("mode changes (every page stored into must change)\n");
	mode_changes();

	printf("  %d pictures, %d pages stored into, %d of them with nothing changed\n",
	       watched_pictures, pages_stored, pages_needless);
	if (failures) {
		printf("test_clear: %d of %d checks FAILED, pictures wrong\n",
		       failures + store_failures, checks);
		return 1;
	}
	if (store_failures) {
		printf("test_clear: %d of %d checks FAILED, every picture right but pages "
		       "stored into with nothing changed\n", store_failures, checks);
		return 2;
	}
	printf("test_clear: all %d checks passed\n", checks);
	return 0;
}
