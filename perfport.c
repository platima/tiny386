/*
 * tiny386's information port: perfport.h is the protocol a guest program
 * reads; this is the emulator's side.
 *
 * Two threads touch it.  The emulator's (core 1 on the board) runs the
 * guest's in and out, and takes the snapshots.  The drawing's (core 0 on
 * the board, vga_task) counts each picture in perfport_redraw() and
 * perfport_picture().  Each figure has one writer and is a 32-bit word,
 * stored and loaded whole, so the snapshot reads the drawing's figures
 * without a lock; a set of them may be a picture apart.
 *
 * The values come from three places: the emulator's own counters (the
 * processor's instruction count, the VGA, the 8254, the PS/2 side), the
 * drawing's counts here, and the front end's (the board's speed.c, the PC
 * harness), which runs last and may replace any of them.  Nothing in a
 * snapshot changes the guest or the emulator.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "pc.h"
#include "perfport.h"

#if PP_COUNT > 32 * PP_VALID_WORDS
#error "PP_COUNT has outgrown PP_I_VALID0 to 3"
#endif

/* ==== the drawing's figures (its thread only writes them) ==== */
static struct {
	volatile uint32_t pictures, draw_us, draw_max;
	volatile uint32_t flushes, flush_rows;
	/* the second being counted */
	bool win_on;
	uint32_t win_t0, win_n, win_us, win_max;
	/* the last whole one, and when it ended */
	volatile uint32_t pps, avg_1s, max_1s, win_end;
} pp_draw;

/* ==== the emulator's thread ==== */
static uint32_t pp_val[PP_COUNT];
static uint32_t pp_valid[PP_VALID_WORDS];
static uint32_t pp_snaps;
static uint32_t pp_io;
static uint16_t pp_index;
static PerfPortFill *pp_fill;
static void *pp_fill_opaque;
/* the instruction count made 64 bits where long is 32 */
static uint64_t pp_insns;
static uint32_t pp_insns_last;
static bool pp_insns_seen;

void perfport_reset(void)
{
	memset((void *) &pp_draw, 0, sizeof(pp_draw));
	memset(pp_val, 0, sizeof(pp_val));
	memset(pp_valid, 0, sizeof(pp_valid));
	pp_snaps = 0;
	pp_io = 0;
	pp_index = 0;
	pp_fill = NULL;
	pp_fill_opaque = NULL;
	pp_insns = 0;
	pp_insns_last = 0;
	pp_insns_seen = false;
}

void perfport_set_fill(PerfPortFill *fn, void *opaque)
{
	pp_fill_opaque = opaque;
	pp_fill = fn;
}

void perfport_put_text(uint32_t *val, uint32_t *valid, int first, const char *s)
{
	size_t n = s ? strlen(s) : 0;
	for (int i = 0; i < PP_TEXT_INDEXES; i++) {
		uint32_t w = 0;
		for (int b = 0; b < 4; b++) {
			size_t k = (size_t) i * 4 + b;
			/* the last byte stays 0: 31 characters at most */
			uint8_t c = k < n && k < 4 * PP_TEXT_INDEXES - 1 ? (uint8_t) s[k] : 0;
			w |= (uint32_t) c << (8 * b);
		}
		PERFPORT_PUT(val, valid, first + i, w);
	}
}

/* ==== the drawing ==== */

void perfport_redraw(void *opaque, int x, int y, int w, int h)
{
	PC *pc = opaque;
	pp_draw.flushes = pp_draw.flushes + 1;
	if (h > 0)
		pp_draw.flush_rows = pp_draw.flush_rows + (uint32_t) h;
	pc->redraw(pc->redraw_data, x, y, w, h);
}

void perfport_picture(uint32_t start_us, uint32_t us)
{
	pp_draw.pictures = pp_draw.pictures + 1;
	pp_draw.draw_us = pp_draw.draw_us + us;
	if (us > pp_draw.draw_max)
		pp_draw.draw_max = us;
	if (!pp_draw.win_on) {
		pp_draw.win_on = true;
		pp_draw.win_t0 = start_us;
	}
	pp_draw.win_n++;
	pp_draw.win_us += us;
	if (us > pp_draw.win_max)
		pp_draw.win_max = us;
	uint32_t now = start_us + us;
	if (now - pp_draw.win_t0 >= 1000000) {
		pp_draw.pps = pp_draw.win_n;
		pp_draw.avg_1s = pp_draw.win_us / pp_draw.win_n;
		pp_draw.max_1s = pp_draw.win_max;
		pp_draw.win_end = now;
		pp_draw.win_t0 = now;
		pp_draw.win_n = 0;
		pp_draw.win_us = 0;
		pp_draw.win_max = 0;
	}
}

/* ==== the snapshot ==== */

/* A 32-bit instruction count made 64 bits: what it has moved since the
   last snapshot is added, which is right while snapshots are less than
   2^32 instructions apart (35 minutes at 2 million a second).  The
   board's front end puts its own 64-bit count over it. */
static uint64_t extend_insns(uint32_t c)
{
	if (pp_insns_seen)
		pp_insns += (uint32_t) (c - pp_insns_last);
	else
		pp_insns = c;
	pp_insns_seen = true;
	pp_insns_last = c;
	return pp_insns;
}

static uint64_t guest_insns(PC *pc)
{
#if defined(USE_AMD64) || defined(USE_CPUABS)
	(void) pc;
	return 0;
#else
	long c = cpui386_get_cycle(pc->cpu);
	if (sizeof(long) >= 8)
		return (uint64_t) c;
	return extend_insns((uint32_t) c);
#endif
}

static void snapshot(PC *pc)
{
	uint32_t *v = pp_val, *ok = pp_valid;
	uint32_t t0 = get_uticks();

	memset(pp_val, 0, sizeof(pp_val));
	memset(pp_valid, 0, sizeof(pp_valid));
	pp_snaps++;

	PERFPORT_PUT(v, ok, PP_I_MAGIC, PP_MAGIC);
	PERFPORT_PUT(v, ok, PP_I_VERSION, PP_VERSION);
	PERFPORT_PUT(v, ok, PP_I_COUNT, PP_COUNT);
	PERFPORT_PUT(v, ok, PP_I_CLOCK_US, t0);
	PERFPORT_PUT(v, ok, PP_I_PORT_IO, pp_io);

	uint64_t n = guest_insns(pc);
	PERFPORT_PUT(v, ok, PP_I_INSNS_LO, (uint32_t) n);
	PERFPORT_PUT(v, ok, PP_I_INSNS_HI, (uint32_t) (n >> 32));

	/* the drawing's; its last whole second is stale (the console has the
	   screen, or nothing is drawn) once two seconds have gone by */
	PERFPORT_PUT(v, ok, PP_I_PICTURES, pp_draw.pictures);
	PERFPORT_PUT(v, ok, PP_I_DRAW_US, pp_draw.draw_us);
	PERFPORT_PUT(v, ok, PP_I_DRAW_MAX_US, pp_draw.draw_max);
	PERFPORT_PUT(v, ok, PP_I_FLUSHES, pp_draw.flushes);
	PERFPORT_PUT(v, ok, PP_I_FLUSH_ROWS, pp_draw.flush_rows);
	uint32_t end = pp_draw.win_end;
	bool fresh = pp_draw.pps && (int32_t) (t0 - end) < 2000000;
	PERFPORT_PUT(v, ok, PP_I_PICTURES_1S, fresh ? pp_draw.pps : 0);
	PERFPORT_PUT(v, ok, PP_I_DRAW_AVG_US_1S, fresh ? pp_draw.avg_1s : 0);
	PERFPORT_PUT(v, ok, PP_I_DRAW_MAX_US_1S, fresh ? pp_draw.max_1s : 0);

	struct vga_info vi;
	vga_get_info(pc->vga, &vi);
	PERFPORT_PUT(v, ok, PP_I_MODE_KIND, vi.kind);
	PERFPORT_PUT(v, ok, PP_I_MODE_WIDTH, vi.width);
	PERFPORT_PUT(v, ok, PP_I_MODE_HEIGHT, vi.height);
	PERFPORT_PUT(v, ok, PP_I_MODE_BPP, vi.bpp);
	PERFPORT_PUT(v, ok, PP_I_MODE_LINE_BYTES, vi.line_bytes);
	PERFPORT_PUT(v, ok, PP_I_MODE_START, vi.start);
	PERFPORT_PUT(v, ok, PP_I_SCREEN_WIDTH, vi.screen_w);
	PERFPORT_PUT(v, ok, PP_I_SCREEN_HEIGHT, vi.screen_h);
	PERFPORT_PUT(v, ok, PP_I_VBE_PICTURES, vi.vbe_pictures);
	PERFPORT_PUT(v, ok, PP_I_VBE_WHOLE, vi.vbe_whole);
	PERFPORT_PUT(v, ok, PP_I_VBE_BLOCKS, vi.vbe_blocks);
	PERFPORT_PUT(v, ok, PP_I_VBE_ROWS, vi.vbe_rows);
	PERFPORT_PUT(v, ok, PP_I_VBE_BLOCKS_ON, vga_dirty_blocks);
	PERFPORT_PUT(v, ok, PP_I_BIOS_MODE, (uint8_t) pc->phys_mem[0x449]);

	PERFPORT_PUT(v, ok, PP_I_GUEST_MEM_KB, (uint32_t) (pc->phys_mem_size / 1024));
	PERFPORT_PUT(v, ok, PP_I_VGA_MEM_KB, (uint32_t) (pc->vga_mem_size / 1024));

	PERFPORT_PUT(v, ok, PP_I_TIMER_OWED_MAX, i8254_owed_max);
	PERFPORT_PUT(v, ok, PP_I_TIMER_DROPPED, i8254_dropped);
	PERFPORT_PUT(v, ok, PP_I_TIMER_CATCHUP, i8254_max_owed);

	PERFPORT_PUT(v, ok, PP_I_KEY_EVENTS, ps2_key_events);
	PERFPORT_PUT(v, ok, PP_I_MOUSE_REPORTS, ps2_mouse_reports);
	PERFPORT_PUT(v, ok, PP_I_MOUSE_PACKETS, ps2_mouse_packets);

	if (pp_fill)
		pp_fill(pp_fill_opaque, v, ok);

	/* the identity is this file's, whatever the front end did */
	PERFPORT_PUT(v, ok, PP_I_MAGIC, PP_MAGIC);
	PERFPORT_PUT(v, ok, PP_I_VERSION, PP_VERSION);
	PERFPORT_PUT(v, ok, PP_I_COUNT, PP_COUNT);
	PERFPORT_PUT(v, ok, PP_I_SNAPSHOTS, pp_snaps);
	PERFPORT_PUT(v, ok, PP_I_SNAP_US, get_uticks() - t0);
	PERFPORT_PUT(v, ok, PP_I_VALID0, 0);
	PERFPORT_PUT(v, ok, PP_I_VALID1, 0);
	PERFPORT_PUT(v, ok, PP_I_VALID2, 0);
	PERFPORT_PUT(v, ok, PP_I_VALID3, 0);
	for (int i = 0; i < PP_VALID_WORDS; i++)
		v[PP_I_VALID0 + i] = ok[i];
}

/* the value at an index, as DATA gives it */
static uint32_t value(int i)
{
	switch (i) {
	case PP_I_MAGIC:        return PP_MAGIC;
	case PP_I_VERSION:      return PP_VERSION;
	case PP_I_COUNT:        return PP_COUNT;
	case PP_I_SNAPSHOTS:    return pp_snaps;
	}
	if (i >= PP_COUNT || !(pp_valid[i >> 5] & (1u << (i & 31))))
		return 0;
	return pp_val[i];
}

/* the eight ports as bytes: INDEX, the snapshots' low 16 bits, DATA */
uint32_t perfport_io_read(void *opaque, int offset, int size)
{
	(void) opaque;
	pp_io++;
	uint8_t b[PP_PORT_COUNT];
	uint32_t d = offset + size > 4 ? value(pp_index) : 0;
	b[0] = (uint8_t) pp_index;
	b[1] = (uint8_t) (pp_index >> 8);
	b[2] = (uint8_t) pp_snaps;
	b[3] = (uint8_t) (pp_snaps >> 8);
	for (int k = 0; k < 4; k++)
		b[4 + k] = (uint8_t) (d >> (8 * k));
	uint32_t r = 0;
	for (int k = 0; k < size; k++) {
		int p = offset + k;
		uint32_t x = p >= 0 && p < PP_PORT_COUNT ? b[p] : 0xff;
		r |= x << (8 * k);
	}
	return r;
}

static void command(PC *pc, uint32_t cmd)
{
	if (cmd == PP_CMD_SNAPSHOT)
		snapshot(pc);
}

void perfport_io_write(void *opaque, int offset, uint32_t val, int size)
{
	PC *pc = opaque;
	pp_io++;
	if (offset == 0) {
		pp_index = (uint16_t) (size == 1 ? val & 0xff : val & 0xffff);
		if (size == 4)
			command(pc, val >> 16);
	} else if (offset == 2) {
		command(pc, size == 1 ? val & 0xff : val & 0xffff);
	}
}
