/*
 * The status LED's logic on a PC: esp/main/status_code.c as the firmware has
 * it (STATUS_HOST), and esp/main/status_bb.h, the panic handler's bit-bang,
 * with a pretend cycle counter.
 *
 *   1. The RMT frame: a reset of 300 us or more, then 24 bits whose high and
 *      low times fall inside both LEDs' windows (Inolux IN-PI554FCH and
 *      Worldsemi WS2812B-2020-V6), decoding back to the colour, green first;
 *      the high times also as the LED sees them, about 38 ns shorter after
 *      the level shifter, against both windows and against an older
 *      WS2812B datasheet's (reported).
 *   2. The bit-bang, cycle by cycle: at 80 MHz (the second-stage
 *      bootloader's clock, which a panic before app_main() runs at), 160
 *      and 320 MHz, with each pass round the waiting loop costing 1 to 6
 *      cycles, each store to the GPIO register 1 to 24, and the
 *      instructions between a wait's last counter read and its store
 *      charged as the built code has them (two before a rise and two before
 *      a fall, sub and bltu; build.sh checks that layout in the ELF files)
 *      at one or two cycles each, and with the counter wrapping round in
 *      the middle of a frame: every edge inside both windows, sent and (the
 *      high times) at the LED, the bits back.  Then v3.3's layout (nine or
 *      ten instructions before a rise) with v3.3's times, which this test
 *      must find short at 80 MHz: the fault the v3.3 review found.
 *   3. The patterns: each stage's colour, each error's colour and number of
 *      blinks, the crash, the PC running (a steady green), the PC switched
 *      off.
 *   4. The error-code state machine: which error shows over which, the
 *      first fatal one staying, errors clearing themselves.
 *   5. The replay after a reset: the lead-in's flashes, the old code, then
 *      the present state.
 *   6. The beat watch: no progress for 10 s is a stall, only while running.
 *   7. The record kept over a reset: written, read back, refused when any
 *      byte of it is wrong.
 *   8. The brightness (v3.7): the colour table at full scale with v3.6's
 *      hues; the scale against an exact model at every level for every
 *      channel value; 0 dark, 255 the table, 70 v3.6's colours; at the
 *      default, the brightest channel 8 to 10 of 255, every colour about an
 *      eighth of v3.6's light, no lit channel put out and no colour turned
 *      into another (at any level above 0); the level held to 0 to 255; and
 *      the panic path's bit-banged frames and the RMT frames carrying the
 *      same scaled bits.  Given the path of esp/main (run.sh gives it), it
 *      also reads status_led.c and status_code.c: every frame made there
 *      goes through status_frame_grb(), which with its level is in
 *      internal RAM.
 *   9. The disk light (v3.8): the PC running is a steady green; a look of
 *      the LED task in which the guest's disk count moved is blue; one
 *      access lights one look, a burst lights it all through, accesses
 *      further apart than a look flicker; an error, the replay and other
 *      stages go over it; a stopped emulator still gives yellow 5 with no
 *      blink.  Given the path of esp/main, it also reads status_led.c,
 *      ide.c, misc.c and esp_main.c for where the count is kept and read.
 */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "status_code.h"

/* The colour table at full scale (status_code.c), as this test expects it */
#define WHITE_FULL  0x929292u
#define RED_FULL    0xff0000u
#define YELLOW_FULL 0xdb8000u

static int fails, checks;

#define CHECK(cond, ...) do { \
	checks++; \
	if (!(cond)) { \
		fails++; \
		printf("FAIL %s:%d: ", __FILE__, __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} \
} while (0)

/* ==== the two LEDs' windows, in ns ==== */
struct window {
	const char *who;
	int t0h_lo, t0h_hi, t0l_lo, t0l_hi, t1h_lo, t1h_hi, t1l_lo, t1l_hi;
	int bit_lo, bit_hi, reset_lo;
};
/* Inolux IN-PI554FCH v2.2 p. 11: 0.3, 0.9, 0.6, 0.6 us each +-0.15 us, a
   bit 1.25 +-0.6 us, reset 80 us or more */
static const struct window inolux = {
	"Inolux IN-PI554FCH", 150, 450, 750, 1050, 450, 750, 450, 750, 650, 1850, 80000,
};
/* Worldsemi WS2812B-2020-V6 p. 5: T0H 220-380, T1H 580-1000, T0L and T1L
   580-1000 ns, a bit 1250 ns or more, reset 280 us or more */
static const struct window worldsemi = {
	"Worldsemi WS2812B", 220, 380, 580, 1000, 580, 1000, 580, 1000, 1250, 1000000, 280000,
};
static const struct window *const windows[] = { &inolux, &worldsemi };

/* The level shifter (BSS138, 1k pull-up to 5 V) slows each rise at the LED
   by about this much: CHANGES-A2.md section 4, an estimate (R204 and about
   33 pF to the Inolux part's 3.4 V).  The LED sees each high time this much
   shorter; it tells a 0 from a 1 by the high time. */
#define SHIFT_NS 38
/* An older Worldsemi WS2812B datasheet's high times, 0.4 and 0.8 us +-0.15
   us (reported in the v3.3 review from a summary, not read at Worldsemi):
   the Korvo-1's WS2812B is of a revision not known.  Checked for the RMT
   frame at the LED only. */
#define OLD_T0H_LO 250
#define OLD_T0H_HI 550
#define OLD_T1H_LO 650
#define OLD_T1H_HI 950

/* the least margin seen to any window edge, ns */
static double margin_min = 1e9;

static void margin(double m)
{
	if (m < margin_min)
		margin_min = m;
}

/* One bit's high and low times, sent, against both windows, and the high
   time at the LED too (lo < 0: the high time only).  older: the high time
   at the LED against the older WS2812B's as well. */
static void bit_in_windows(const char *where, int one, double hi, double lo, int older)
{
	double led = hi - SHIFT_NS;

	for (int k = 0; k < 2; k++) {
		const struct window *w = windows[k];
		int hl = one ? w->t1h_lo : w->t0h_lo, hh = one ? w->t1h_hi : w->t0h_hi;
		int ll = one ? w->t1l_lo : w->t0l_lo, lh = one ? w->t1l_hi : w->t0l_hi;
		CHECK(hi >= hl && hi <= hh, "%s: %s high %.1f ns, %s wants %d to %d",
		      where, one ? "1" : "0", hi, w->who, hl, hh);
		CHECK(led >= hl && led <= hh, "%s: %s high %.1f ns at the LED, %s wants %d to %d",
		      where, one ? "1" : "0", led, w->who, hl, hh);
		margin(led - hl);
		margin(hh - led);
		double m[] = { hi - hl, hh - hi, 1e9, 1e9, 1e9, 1e9 };
		if (lo >= 0) {
			CHECK(lo >= ll && lo <= lh, "%s: %s low %.1f ns, %s wants %d to %d",
			      where, one ? "1" : "0", lo, w->who, ll, lh);
			CHECK(hi + lo >= w->bit_lo && hi + lo <= w->bit_hi,
			      "%s: a bit of %.1f ns, %s wants %d to %d", where, hi + lo,
			      w->who, w->bit_lo, w->bit_hi);
			m[2] = lo - ll;
			m[3] = lh - lo;
			m[4] = hi + lo - w->bit_lo;
			m[5] = w->bit_hi - hi - lo;
		}
		for (int i = 0; i < 6; i++)
			margin(m[i]);
	}
	if (older) {
		int hl = one ? OLD_T1H_LO : OLD_T0H_LO, hh = one ? OLD_T1H_HI : OLD_T0H_HI;
		CHECK(led >= hl && led <= hh, "%s: %s high %.1f ns at the LED, the older "
		      "WS2812B wants %d to %d", where, one ? "1" : "0", led, hl, hh);
		margin(led - hl);
		margin(hh - led);
	}
}

/* ==== 1. the RMT frame ==== */

static void test_rmt(void)
{
	static const uint32_t colours[] = {
		0x000000, 0xffffff, 0x123456, 0x800001, 0x3c2300, 0x002d2d, 0xa5a5a5, 0x5a5a5a,
	};
	const double tick = 1e9 / STATUS_RMT_HZ;
	double rmt_margin = 1e9;

	CHECK(STATUS_RMT_SYMBOLS <= 48, "a frame of %d symbols does not fit one RMT block of 48",
	      STATUS_RMT_SYMBOLS);
	for (unsigned c = 0; c < sizeof(colours) / sizeof(colours[0]); c++) {
		uint32_t grb = colours[c], out[STATUS_RMT_SYMBOLS], back = 0;
		status_rmt_frame(grb, out);
		uint32_t d0 = out[0] & 0x7fff, d1 = (out[0] >> 16) & 0x7fff;
		CHECK(!(out[0] & 0x8000) && !(out[0] >> 31), "the reset symbol is not low");
		CHECK((d0 + d1) * tick >= STATUS_RESET_US * 1000.0 && (d0 + d1) * tick >= 280000,
		      "reset %.0f ns", (d0 + d1) * tick);
		for (int i = 0; i < 24; i++) {
			uint32_t s = out[1 + i];
			CHECK((s & 0x8000) && !(s >> 31), "bit %d: not high then low", i);
			double hi = (s & 0x7fff) * tick, lo = ((s >> 16) & 0x7fff) * tick;
			int one = hi > 500;
			char where[64];
			snprintf(where, sizeof(where), "RMT colour %06x bit %d", grb, i);
			double before = margin_min;
			margin_min = 1e9;
			bit_in_windows(where, one, hi, lo, 1);
			if (margin_min < rmt_margin)
				rmt_margin = margin_min;
			if (before < margin_min)
				margin_min = before;
			back = back << 1 | one;
		}
		CHECK(back == grb, "RMT frame of %06x decodes to %06x", grb, back);
	}
	printf("rmt: 25 symbols a frame, %d checks so far; least margin to a window edge %.0f ns\n",
	       checks, rmt_margin);
	CHECK(status_grb(0x112233) == 0x221133, "status_grb(112233) = %06x", status_grb(0x112233));
}

/* ==== 2. the bit-bang with a pretend clock ==== */

static uint64_t sim_cycles;      /* the true time, in cycles */
static int sim_cost_read, sim_cost_store, sim_cost_insn;
/* the instructions between a wait's last counter read and the store after
   it, before a rise (for a 0 bit and for a 1) and before a fall */
static int sim_rise0, sim_rise1, sim_fall;
static uint32_t sim_grb;        /* the frame's bits, for which rise is which */
static int sim_rises;
static int sim_level;
static uint64_t sim_edge[64];
static int sim_edge_level[64];
static int sim_nedges;
static uint64_t sim_reads;

/* The layout of the built code (riscv32-esp-elf-objdump of
   status_led_panic() and status_led_halt() in all three images): each
   store straight after its wait's bltu, so sub and bltu before a rise and
   before a fall.  build.sh stops if the built code is not so. */
#define BB_RISE_INSNS 2
#define BB_FALL_INSNS 2
/* v3.3's layout, which the v3.3 review found: nine instructions before a
   rise of a 0 bit, ten of a 1 bit, two before a fall */
#define V33_RISE0_INSNS 9
#define V33_RISE1_INSNS 10
#define V33_FALL_INSNS 2

static uint32_t sim_now(void)
{
	sim_cycles += sim_cost_read;
	sim_reads++;
	return (uint32_t) sim_cycles;   /* the counter is 32 bits: it wraps */
}

static void sim_store(int level)
{
	if (level) {
		/* the frame's first store is the low before the reset; each
		   rise after it is the next bit's, from bit 23 down */
		int bit = sim_rises < 24 ? (sim_grb >> (23 - sim_rises)) & 1 : 0;
		sim_rises++;
		sim_cycles += (uint64_t) (bit ? sim_rise1 : sim_rise0) * sim_cost_insn;
	} else {
		sim_cycles += (uint64_t) sim_fall * sim_cost_insn;
	}
	sim_cycles += sim_cost_store;   /* the pin changes as the store lands */
	if (level != sim_level && sim_nedges < 64) {
		sim_edge[sim_nedges] = sim_cycles;
		sim_edge_level[sim_nedges++] = level;
	}
	sim_level = level;
}

#define STATUS_BB_NOW() sim_now()
#define STATUS_BB_HIGH() sim_store(1)
#define STATUS_BB_LOW() sim_store(0)
#include "status_bb.h"

/* One frame with the pretend clock; the edges in sim_edge.  Returns the
   number of edges. */
static int sim_frame(uint32_t grb, const struct status_bb *t, uint32_t mhz)
{
	sim_grb = grb;
	sim_rises = 0;
	/* the counter wraps 20 us into the frame */
	sim_cycles = 0x100000000ull - (STATUS_RESET_US + 20) * mhz;
	sim_level = 1;          /* the line high before: R205 */
	sim_nedges = 0;
	sim_reads = 0;
	status_bb_frame(grb, t);
	return sim_nedges;
}

static void test_bitbang(void)
{
	static const uint32_t mhz[] = { 80, 160, 320 };
	/* a pass round the waiting loop (rdcycle, sub, bltu: 3 instructions in
	   the firmware's code) and a store reaching the pin, in cycles */
	static const int costs[][2] = {
		{ 1, 1 }, { 2, 4 }, { 3, 8 }, { 3, 1 }, { 4, 16 }, { 6, 24 },
	};
	static const uint32_t colours[] = { 0x000000, 0xffffff, 0x00a5c3, 0x280000 };
	int runs = 0;
	double worst = 1e9;

	sim_rise0 = sim_rise1 = BB_RISE_INSNS;
	sim_fall = BB_FALL_INSNS;
	for (unsigned f = 0; f < 3; f++) {
		struct status_bb t;
		status_bb_timing(&t, mhz[f]);
		for (unsigned k = 0; k < sizeof(costs) / sizeof(costs[0]); k++) {
			/* the slow costs are for a fast clock: 24 cycles a store at
			   80 MHz would be 300 ns, which no bus takes */
			if (mhz[f] < 160 && (costs[k][0] > 3 || costs[k][1] > 8))
				continue;
			for (int ci = 1; ci <= 2; ci++)
			for (unsigned c = 0; c < 4; c++) {
				uint32_t grb = colours[c];
				sim_cost_read = costs[k][0];
				sim_cost_store = costs[k][1];
				sim_cost_insn = ci;
				uint64_t start = 0x100000000ull - (STATUS_RESET_US + 20) * mhz[f];
				int n = sim_frame(grb, &t, mhz[f]);
				uint64_t end = sim_cycles;
				runs++;
				char where[112];
				snprintf(where, sizeof(where), "bit-bang %u MHz, read %d, store %d, "
					 "instruction %d, %06x", mhz[f], costs[k][0], costs[k][1], ci, grb);
				CHECK(n == 1 + 48, "%s: %d edges", where, n);
				if (n != 49)
					continue;
				CHECK(sim_edge_level[0] == 0, "%s: does not start low", where);
				double ns = 1000.0 / mhz[f];
				double reset = (sim_edge[1] - sim_edge[0]) * ns;
				CHECK(reset >= 280000 && reset >= STATUS_RESET_US * 1000.0,
				      "%s: reset %.0f ns", where, reset);
				uint32_t back = 0;
				double before = margin_min;
				margin_min = 1e9;
				for (int i = 0; i < 24; i++) {
					double hi = (sim_edge[2 + 2 * i] - sim_edge[1 + 2 * i]) * ns;
					/* the last bit's low: the frame waits it
					   out before it returns, so up to where a
					   rise straight after the return would
					   land (the line in fact rests low, or
					   the next frame's reset follows) */
					uint64_t next = i < 23 ? sim_edge[3 + 2 * i] :
						end + (uint64_t) sim_rise0 * sim_cost_insn + sim_cost_store;
					double lo = (next - sim_edge[2 + 2 * i]) * ns;
					int one = hi > 500;
					bit_in_windows(where, one, hi, lo, 0);
					back = back << 1 | one;
				}
				if (margin_min < worst)
					worst = margin_min;
				if (before < margin_min)
					margin_min = before;
				CHECK(back == grb, "%s: decodes to %06x", where, back);
				if (c == 1 && k == 0 && ci == 1)
					printf("bit-bang: %3u MHz: %llu cycles a frame (%.1f us), %llu reads "
					       "of the counter\n", mhz[f],
					       (unsigned long long) (end - start),
					       (end - start) * ns / 1000, (unsigned long long) sim_reads);
			}
		}
	}
	printf("bit-bang: %d frames, %d instructions charged before a rise and %d before a fall, "
	       "every edge checked; least margin to a window edge %.1f ns\n",
	       runs, BB_RISE_INSNS, BB_FALL_INSNS, worst);

	/* The test sees the v3.3 fault: v3.3's layout and times (T1H 53
	   cycles at 80 MHz), one cycle an instruction, give a 1 bit's high
	   time under the WS2812B-2020's 580 ns as sent. */
	{
		const struct status_bb v33 = { 27, 104, 53, 104, 24000 };
		sim_rise0 = V33_RISE0_INSNS;
		sim_rise1 = V33_RISE1_INSNS;
		sim_fall = V33_FALL_INSNS;
		sim_cost_read = 1;
		sim_cost_store = 1;
		sim_cost_insn = 1;
		int n = sim_frame(0x00a5c3, &v33, 80);
		double t1h_min = 1e9;
		for (int i = 0; n == 49 && i < 24; i++) {
			double hi = (sim_edge[2 + 2 * i] - sim_edge[1 + 2 * i]) * 12.5;
			if (((0x00a5c3u >> (23 - i)) & 1) && hi < t1h_min)
				t1h_min = hi;
		}
		CHECK(n == 49 && t1h_min < 580, "v3.3's layout at 80 MHz: T1H %.1f ns, which this "
		      "test should find under 580", t1h_min);
		printf("bit-bang: v3.3's layout and times at 80 MHz: T1H %.1f ns as sent, under the "
		       "WS2812B-2020's 580 (the v3.3 review's finding, seen)\n", t1h_min);
		sim_rise0 = sim_rise1 = BB_RISE_INSNS;
		sim_fall = BB_FALL_INSNS;
	}

	/* the cycle counts themselves */
	struct status_bb t;
	status_bb_timing(&t, 320);
	CHECK(t.t0h == 108 && t.t0 == 416 && t.t1h == 224 && t.t1 == 424 && t.reset == 96000,
	      "320 MHz: t0h %u t0 %u t1h %u t1 %u reset %u", t.t0h, t.t0, t.t1h, t.t1, t.reset);
	status_bb_timing(&t, 80);
	CHECK(t.t0h == 27 && t.t0 == 104 && t.t1h == 56 && t.t1 == 106 && t.reset == 24000,
	      "80 MHz: t0h %u t0 %u t1h %u t1 %u reset %u", t.t0h, t.t0, t.t1h, t.t1, t.reset);
	printf("bit-bang: cycles at 80 MHz: T0H %u, T0 %u, T1H %u, T1 %u, reset %u; at 320 MHz "
	       "four times as many\n", t.t0h, t.t0, t.t1h, t.t1, t.reset);
}

/* ==== 3. the patterns ==== */

/* blinks of one cycle of err's pattern, by sampling every ms */
static int count_blinks(int err, uint32_t *rgb_seen, uint32_t *longest_dark)
{
	uint32_t cycle = status_error_cycle_ms(err);
	int n = 0;
	uint32_t prev = 0, dark = 0;
	*rgb_seen = 0;
	*longest_dark = 0;
	/* from the cycle's start, where the first blink begins */
	for (uint32_t t = 0; t < cycle; t++) {
		uint32_t c = status_error_pattern(err, 5 * cycle + t);
		if (c && !prev)
			n++;
		if (c)
			*rgb_seen = c;
		dark = c ? 0 : dark + 1;
		if (dark > *longest_dark)
			*longest_dark = dark;
		prev = c;
	}
	return n;
}

static void test_patterns(void)
{
	/* stages: steady colours, different from each other where they must be */
	static const int steady[] = { STATUS_STAGE_STARTED, STATUS_STAGE_SCREEN, STATUS_STAGE_CARD,
				      STATUS_STAGE_INI, STATUS_STAGE_BIOS };
	for (unsigned i = 0; i < 5; i++) {
		struct status_state s = { 0 };
		status_state_stage(&s, steady[i]);
		uint32_t c = status_pattern(&s, 0);
		CHECK(c != 0, "stage %s is dark", status_stage_name(steady[i]));
		for (uint32_t t = 0; t < 10000; t += 37)
			CHECK(status_pattern(&s, t) == c, "stage %s not steady at %u ms",
			      status_stage_name(steady[i]), t);
		for (unsigned j = 0; j < i; j++)
			CHECK(status_stage_rgb(steady[j]) != c, "stages %s and %s look the same",
			      status_stage_name(steady[j]), status_stage_name(steady[i]));
	}
	/* running: a steady green, the BIOS stage's (v3.8; v3.3 to v3.7 went
	   dark for 100 ms every 2 s), and blue, the screen stage's colour,
	   for a look with the disk light on */
	struct status_state s = { 0 };
	status_state_stage(&s, STATUS_STAGE_RUNNING);
	int dark = 0, other = 0;
	for (uint32_t t = 0; t < 10000; t++) {
		uint32_t c = status_pattern(&s, t);
		dark += c == 0;
		other += c != status_stage_rgb(STATUS_STAGE_BIOS);
	}
	CHECK(dark == 0 && other == 0, "running: not a steady green (%d ms dark, %d ms another "
	      "colour, of 10,000)", dark, other);
	s.disk = 1;
	CHECK(status_pattern(&s, 4500) == status_stage_rgb(STATUS_STAGE_SCREEN) &&
	      status_pattern(&s, 4500) != status_stage_rgb(STATUS_STAGE_RUNNING),
	      "running with the disk light: not the screen stage's blue");
	s.disk = 0;
	/* the PC switched off: white a second, dark a second */
	status_state_stage(&s, STATUS_STAGE_PC_OFF);
	CHECK(status_pattern(&s, 500) == status_stage_rgb(STATUS_STAGE_STARTED) &&
	      status_pattern(&s, 1500) == 0, "PC switched off: not a slow white blink");

	/* errors: red for the card and its files, yellow for the board, the
	   number of blinks their place in each */
	static const struct { int err, blinks; uint32_t rgb; } table[] = {
		{ STATUS_ERR_NO_CARD, 1, RED_FULL }, { STATUS_ERR_NO_INI, 2, RED_FULL },
		{ STATUS_ERR_BIOS, 3, RED_FULL }, { STATUS_ERR_INI, 4, RED_FULL },
		{ STATUS_ERR_STOPPED, 5, RED_FULL }, { STATUS_ERR_PSRAM, 1, YELLOW_FULL },
		{ STATUS_ERR_HDMI_CHIP, 2, YELLOW_FULL }, { STATUS_ERR_NO_MONITOR, 3, YELLOW_FULL },
		{ STATUS_ERR_KEYBOARD, 4, YELLOW_FULL }, { STATUS_ERR_STALLED, 5, YELLOW_FULL },
	};
	for (unsigned i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
		uint32_t rgb, longest;
		int n = count_blinks(table[i].err, &rgb, &longest);
		CHECK(n == table[i].blinks, "%s: %d blinks, not %d", status_error_name(table[i].err),
		      n, table[i].blinks);
		CHECK(rgb == table[i].rgb, "%s: colour %06x, not %06x", status_error_name(table[i].err),
		      rgb, table[i].rgb);
		/* the pause between groups is plainly longer than a gap in one */
		CHECK(longest >= 1200, "%s: pause %u ms", status_error_name(table[i].err), longest);
		CHECK(status_error_blinks(table[i].err) == table[i].blinks, "%s: blinks in the table",
		      status_error_name(table[i].err));
	}
	/* the crash: red and white by turns, a quarter second each */
	for (uint32_t t = 0; t < 4000; t += 50) {
		uint32_t c = status_panic_pattern(false, t);
		CHECK(c == ((t / 250) & 1 ? WHITE_FULL : RED_FULL), "crash at %u ms: %06x", t, c);
	}
	/* the crash before app_main(): yellow 1 */
	uint32_t rgb, longest;
	CHECK(count_blinks(STATUS_ERR_PSRAM, &rgb, &longest) == 1, "early crash: not one blink");
	for (uint32_t t = 0; t < 3000; t += 10)
		CHECK(status_panic_pattern(true, t) == status_error_pattern(STATUS_ERR_PSRAM, t),
		      "early crash at %u ms", t);
}

/* ==== 4. the state machine ==== */

static void test_state(void)
{
	struct status_state s = { 0 };

	CHECK(status_state_stage(&s, STATUS_STAGE_STARTED) == 0, "started: an error shows");
	/* no card yet: red 1, over the stage */
	CHECK(status_state_error(&s, STATUS_ERR_NO_CARD, true) == STATUS_ERR_NO_CARD, "no card");
	CHECK(status_pattern(&s, 0) == RED_FULL, "no card: not red at 0 ms");
	/* the card goes in */
	CHECK(status_state_error(&s, STATUS_ERR_NO_CARD, false) == 0, "card in: still an error");
	status_state_stage(&s, STATUS_STAGE_CARD);
	CHECK(status_pattern(&s, 0) == status_stage_rgb(STATUS_STAGE_CARD), "card in: not cyan");
	/* two that clear themselves: the first in the table shows */
	status_state_error(&s, STATUS_ERR_KEYBOARD, true);
	CHECK(status_state_error(&s, STATUS_ERR_NO_MONITOR, true) == STATUS_ERR_NO_MONITOR,
	      "no monitor and keyboard: not no monitor");
	CHECK(status_state_error(&s, STATUS_ERR_NO_MONITOR, false) == STATUS_ERR_KEYBOARD,
	      "monitor in: keyboard should show");
	/* a fatal one goes over them, and the first stays */
	CHECK(status_state_fatal(&s, STATUS_ERR_BIOS) == STATUS_ERR_BIOS, "BIOS missing");
	CHECK(status_state_fatal(&s, STATUS_ERR_STOPPED) == STATUS_ERR_BIOS,
	      "console_fatal's own red 5 replaced the named error");
	CHECK(status_state_error(&s, STATUS_ERR_KEYBOARD, false) == STATUS_ERR_BIOS,
	      "a cleared error cleared the fatal one");
	CHECK(status_state_stage(&s, STATUS_STAGE_RUNNING) == STATUS_ERR_BIOS,
	      "a later stage hid the fatal one");
	/* nonsense is ignored */
	CHECK(status_state_error(&s, 99, true) == STATUS_ERR_BIOS && s.active == 0,
	      "error 99 changed the state");
	CHECK(status_state_fatal(&s, -1) == STATUS_ERR_BIOS, "fatal -1 changed the state");
	/* the stall, while running */
	struct status_state r = { 0 };
	status_state_stage(&r, STATUS_STAGE_RUNNING);
	CHECK(status_state_error(&r, STATUS_ERR_STALLED, true) == STATUS_ERR_STALLED, "stall");
	CHECK(status_pattern(&r, 0) == YELLOW_FULL, "stall: not yellow");
	CHECK(status_state_error(&r, STATUS_ERR_STALLED, false) == 0, "stall cleared");
	CHECK(status_pattern(&r, 500) == status_stage_rgb(STATUS_STAGE_RUNNING), "running again");
}

/* ==== 5. the replay ==== */

static void test_replay(void)
{
	struct status_state s = { 0 };

	/* a watchdog (2 flashes), the last run showing no monitor (yellow 3) */
	status_replay_setup(&s, STATUS_CAUSE_WATCHDOG, STATUS_STAGE_RUNNING, STATUS_ERR_NO_MONITOR);
	status_state_stage(&s, STATUS_STAGE_STARTED);
	CHECK(s.replay_end_ms == 2 * 300 + 600 + 2 * 3000, "replay ends at %u", s.replay_end_ms);
	int flashes = 0;
	uint32_t prev = 0;
	for (uint32_t t = 0; t < 1200; t++) {
		uint32_t c = status_pattern(&s, t);
		CHECK(c == 0 || c == WHITE_FULL, "lead-in at %u ms: %06x", t, c);
		if (c && !prev)
			flashes++;
		prev = c;
	}
	CHECK(flashes == 2, "lead-in: %d flashes", flashes);
	int blinks = 0;
	prev = 0;
	for (uint32_t t = 1200; t < 1200 + 3000; t++) {
		uint32_t c = status_pattern(&s, t);
		if (c && !prev)
			blinks++;
		CHECK(c == 0 || c == YELLOW_FULL, "old code at %u ms: %06x", t, c);
		prev = c;
	}
	CHECK(blinks == 3, "old code: %d blinks", blinks);
	CHECK(status_pattern(&s, s.replay_end_ms) == status_stage_rgb(STATUS_STAGE_STARTED),
	      "after the replay: not the present stage");
	/* a present error during the replay waits for it */
	status_state_fatal(&s, STATUS_ERR_NO_INI);
	CHECK(status_pattern(&s, 100) == WHITE_FULL, "a present error cut the lead-in short");
	CHECK(status_pattern(&s, s.replay_end_ms) == RED_FULL, "after the replay: not red 2");

	/* a crash (1 flash), the last run showing nothing: its stage, 2 s */
	struct status_state c = { 0 };
	status_replay_setup(&c, STATUS_CAUSE_CRASH, STATUS_STAGE_CARD, 0);
	status_state_stage(&c, STATUS_STAGE_STARTED);
	CHECK(c.replay_end_ms == 300 + 600 + 2000, "crash replay ends at %u", c.replay_end_ms);
	CHECK(status_pattern(&c, 50) == WHITE_FULL && status_pattern(&c, 200) == 0 &&
	      status_pattern(&c, 400) == 0, "crash lead-in");
	CHECK(status_pattern(&c, 1000) == status_stage_rgb(STATUS_STAGE_CARD), "old stage");
	/* a brownout: 3 flashes */
	struct status_state b = { 0 };
	status_replay_setup(&b, STATUS_CAUSE_POWER, STATUS_STAGE_RUNNING, STATUS_ERR_PANIC);
	flashes = 0;
	prev = 0;
	for (uint32_t t = 0; t < 1500; t++) {
		uint32_t x = status_pattern(&b, t);
		if (x && !prev)
			flashes++;
		prev = x;
	}
	CHECK(flashes == 3, "brownout lead-in: %d flashes", flashes);
	CHECK(b.replay_end_ms == 3 * 300 + 600 + 2 * 2000, "brownout replay %u", b.replay_end_ms);
	/* no cause: no replay */
	struct status_state n = { 0 };
	status_replay_setup(&n, STATUS_CAUSE_NONE, STATUS_STAGE_RUNNING, STATUS_ERR_NO_INI);
	status_state_stage(&n, STATUS_STAGE_STARTED);
	CHECK(n.replay_end_ms == 0 && status_pattern(&n, 0) == WHITE_FULL, "no cause: a replay");
}

/* ==== 6. the beat watch ==== */

static void test_watch(void)
{
	struct status_watch w = { 0 };
	uint32_t beats = 0, t = 0;

	/* not running: never a stall, however long */
	for (; t < 30000; t += STATUS_TICK_MS)
		CHECK(!status_watch_step(&w, beats, t, false), "a stall while not running, %u ms", t);
	/* running and beating */
	for (; t < 60000; t += STATUS_TICK_MS)
		CHECK(!status_watch_step(&w, beats += 1000, t, true), "a stall while beating");
	/* stopped: a stall from 10 s on, not before */
	uint32_t stop = t, seen = 0;
	for (; t < stop + 20000; t += STATUS_TICK_MS)
		if (status_watch_step(&w, beats, t, true) && !seen)
			seen = t;
	/* 10 s, as STATUS-LED.md says, give or take one look of the LED task */
	CHECK(seen == stop + 10000 - STATUS_TICK_MS || seen == stop + 10000,
	      "stall seen %u ms after the last beat", seen - stop);
	/* beating again: cleared at once */
	CHECK(!status_watch_step(&w, ++beats, t, true), "not cleared by a beat");
	/* the clock and the count wrapping round */
	struct status_watch v = { .last_beats = 0xffffffffu, .last_change_ms = 0xfffff000u };
	CHECK(!status_watch_step(&v, 0xffffffffu, 0xfffffff0u, true), "stall over 4 s");
	CHECK(status_watch_step(&v, 0xffffffffu, 0x00002000u, true), "no stall across the wrap");
}

/* ==== 7. the record ==== */

static void test_record(void)
{
	struct status_record r;
	memset(&r, 0, sizeof(r));
	CHECK(!status_record_valid(&r), "zeros are a record");
	memset(&r, 0xff, sizeof(r));
	CHECK(!status_record_valid(&r), "0xff is a record");
	for (int stage = 0; stage < STATUS_STAGE_COUNT; stage++)
		for (int err = 0; err < STATUS_ERR_COUNT; err++) {
			status_record_put(&r, stage, err);
			CHECK(status_record_valid(&r) && r.stage == stage && r.error == err,
			      "record %d %d not read back", stage, err);
		}
	status_record_put(&r, STATUS_STAGE_RUNNING, STATUS_ERR_PANIC);
	for (unsigned i = 0; i < sizeof(r); i++)
		for (int bit = 0; bit < 8; bit++) {
			struct status_record x = r;
			((uint8_t *) &x)[i] ^= 1 << bit;
			CHECK(!status_record_valid(&x), "byte %u bit %d flipped: still a record", i, bit);
		}
	/* noise, as the LP SRAM holds it after power-on */
	srand(386);
	int taken = 0;
	for (int i = 0; i < 100000; i++) {
		for (unsigned j = 0; j < sizeof(r); j++)
			((uint8_t *) &r)[j] = rand();
		taken += status_record_valid(&r);
	}
	CHECK(taken == 0, "%d of 100000 noise records taken", taken);
	CHECK(sizeof(struct status_record) == 12, "the record is %zu bytes", sizeof(struct status_record));
}

/* ==== 8. the brightness ==== */

/* v3.3 to v3.6's colours (about a quarter of full), the full-scale table
   v3.7 has, and what the default level gives */
static const struct hue {
	const char *name;
	uint32_t v36, full, at_default;
} hues[] = {
	{ "white", 0x282828, WHITE_FULL, 0x050505 },
	{ "blue", 0x00003c, 0x0000db, 0x000008 },
	{ "cyan", 0x002d2d, 0x00a4a4, 0x000606 },
	{ "magenta", 0x320032, 0xb600b6, 0x060006 },
	{ "green", 0x003200, 0x00b600, 0x000600 },
	{ "red", 0x460000, RED_FULL, 0x090000 },
	{ "yellow", 0x3c2300, YELLOW_FULL, 0x080500 },
};
#define NHUES ((int) (sizeof(hues) / sizeof(hues[0])))

/* channel k of a colour: 0 red, 1 green, 2 blue */
static uint32_t chan(uint32_t rgb, int k)
{
	return (rgb >> (16 - 8 * k)) & 0xff;
}

/* The scale worked out another way: the whole step nearest c * level / 255
   (never a tie, 255 being odd), in 64 bits, then 1 for a channel that is
   lit but would come to 0 at a level above 0. */
static uint32_t model(uint32_t c, uint32_t level)
{
	if (level > 255)
		level = 255;
	uint32_t v = (uint32_t) ((2ull * c * level + 255) / 510);
	if (!v && c && level)
		v = 1;
	return v;
}

static uint32_t model_rgb(uint32_t rgb, uint32_t level)
{
	return model(chan(rgb, 0), level) << 16 | model(chan(rgb, 1), level) << 8 |
	       model(chan(rgb, 2), level);
}

/* the 24 bits an RMT frame of grb carries, read back by their high times */
static uint32_t rmt_bits(uint32_t grb)
{
	uint32_t out[STATUS_RMT_SYMBOLS], back = 0;
	status_rmt_frame(grb, out);
	for (int i = 0; i < 24; i++)
		back = back << 1 | ((out[1 + i] & 0x7fff) * (1e9 / STATUS_RMT_HZ) > 500);
	return back;
}

/* the same for the panic path's bit-bang (status_bb.h) at 80 MHz, the
   built code's layout, one cycle a step; ~0 if the frame is not whole */
static uint32_t bb_bits(uint32_t grb)
{
	struct status_bb t;
	status_bb_timing(&t, 80);
	sim_cost_read = sim_cost_store = sim_cost_insn = 1;
	sim_rise0 = sim_rise1 = BB_RISE_INSNS;
	sim_fall = BB_FALL_INSNS;
	if (sim_frame(grb, &t, 80) != 49)
		return 0xffffffffu;
	uint32_t back = 0;
	for (int i = 0; i < 24; i++)
		back = back << 1 | ((sim_edge[2 + 2 * i] - sim_edge[1 + 2 * i]) * 12.5 > 500);
	return back;
}

/* a source file with its comments taken out (strings kept), or NULL */
static char *read_code(const char *dir, const char *name)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = malloc(n + 1);
	if (!buf || fread(buf, 1, n, f) != (size_t) n) {
		fclose(f);
		free(buf);
		return NULL;
	}
	fclose(f);
	buf[n] = 0;
	char *o = buf;
	for (char *p = buf; *p;) {
		if (p[0] == '/' && p[1] == '*') {
			char *e = strstr(p + 2, "*/");
			p = e ? e + 2 : p + strlen(p);
			*o++ = ' ';
		} else if (p[0] == '/' && p[1] == '/') {
			while (*p && *p != '\n')
				p++;
		} else {
			*o++ = *p++;
		}
	}
	*o = 0;
	return buf;
}

static int ident_char(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/* the calls of fn in src (the name on its own, then a bracket), and in
   *first how many have an argument list that starts with arg */
static int calls_of(const char *src, const char *fn, const char *arg, int *first)
{
	size_t fl = strlen(fn);
	int n = 0;
	*first = 0;
	for (const char *p = src; (p = strstr(p, fn)); p += fl) {
		if ((p > src && ident_char(p[-1])) || ident_char(p[fl]))
			continue;
		const char *q = p + fl;
		while (*q == ' ' || *q == '\t' || *q == '\n')
			q++;
		if (*q != '(')
			continue;
		q++;
		while (*q == ' ' || *q == '\t' || *q == '\n')
			q++;
		n++;
		if (arg && !strncmp(q, arg, strlen(arg)))
			(*first)++;
	}
	return n;
}

/* the body of the function whose definition starts with head, up to the
   first line that is only a closing brace; "" if not found */
static const char *body_of(const char *src, const char *head, char *out, size_t len)
{
	const char *p = strstr(src, head), *e = p ? strstr(p, "\n}") : NULL;
	out[0] = 0;
	if (p && e && (size_t) (e - p) < len) {
		memcpy(out, p, e - p);
		out[e - p] = 0;
	}
	return out;
}

static void test_brightness(const char *src_dir)
{
	const uint32_t d = STATUS_BRIGHTNESS_DEFAULT;

	/* nothing has set it yet: the default, as from power-on */
	CHECK(status_brightness() == d, "the level at start is %u, not %u", status_brightness(), d);

	/* the table at full scale: v3.6's colours times 255/70, red at 255 */
	static const struct { int stage, hue; } stages[] = {
		{ STATUS_STAGE_STARTED, 0 }, { STATUS_STAGE_SCREEN, 1 }, { STATUS_STAGE_CARD, 2 },
		{ STATUS_STAGE_INI, 3 }, { STATUS_STAGE_BIOS, 4 }, { STATUS_STAGE_RUNNING, 4 },
		{ STATUS_STAGE_PC_OFF, 0 },
	};
	for (unsigned i = 0; i < sizeof(stages) / sizeof(stages[0]); i++)
		CHECK(status_stage_rgb(stages[i].stage) == hues[stages[i].hue].full,
		      "stage %s: %06x, not %s at full scale %06x", status_stage_name(stages[i].stage),
		      status_stage_rgb(stages[i].stage), hues[stages[i].hue].name,
		      hues[stages[i].hue].full);
	for (int e = STATUS_ERR_NO_CARD; e <= STATUS_ERR_STOPPED; e++)
		CHECK(status_error_rgb(e) == RED_FULL, "%s: %06x", status_error_name(e), status_error_rgb(e));
	for (int e = STATUS_ERR_PSRAM; e <= STATUS_ERR_STALLED; e++)
		CHECK(status_error_rgb(e) == YELLOW_FULL, "%s: %06x", status_error_name(e),
		      status_error_rgb(e));
	CHECK(status_panic_pattern(false, 0) == RED_FULL && status_panic_pattern(false, 250) == WHITE_FULL &&
	      status_panic_pattern(true, 0) == YELLOW_FULL, "the crash's colours are not the table's");
	uint32_t top = 0;
	for (int h = 0; h < NHUES; h++)
		for (int k = 0; k < 3; k++) {
			uint32_t a = chan(hues[h].v36, k), b = chan(hues[h].full, k);
			CHECK(b == (a * 255 + 35) / 70, "%s channel %d: %u at full scale, not v3.6's %u "
			      "times 255/70", hues[h].name, k, b, a);
			if (b > top)
				top = b;
		}
	CHECK(top == 255, "the table's brightest channel is %u, not 255", top);

	/* the scale against the model: every value of every channel at every
	   level, the other two channels set at the same time */
	int bad_levels = 0;
	for (uint32_t level = 0; level <= 255; level++) {
		int bad = 0;
		uint32_t first_rgb = 0, first_got = 0;
		for (uint32_t c = 0; c <= 255; c++) {
			/* red takes every value, green every value (7c is a
			   permutation of 0 to 255), blue every value */
			uint32_t rgb = c << 16 | ((c * 7 + level) & 0xff) << 8 | (255 - c);
			uint32_t got = status_scale(rgb, level);
			if (got != model_rgb(rgb, level) && !bad++) {
				first_rgb = rgb;
				first_got = got;
			}
			if (level < 255 && status_scale(c, level + 1) < status_scale(c, level) && !bad++) {
				first_rgb = c;
				first_got = status_scale(c, level + 1);
			}
		}
		CHECK(!bad, "level %u: %d channel values scaled wrongly, the first %06x to %06x, the "
		      "model says %06x", level, bad, first_rgb, first_got, model_rgb(first_rgb, level));
		bad_levels += bad != 0;
	}
	printf("brightness: every channel value at every level 0 to 255 as the model has it "
	       "(%d levels wrong), and never darker one level up\n", bad_levels);
	static const uint32_t over[] = { 256, 1000, 0x7fffffffu, 0xffffffffu };
	for (unsigned i = 0; i < 4; i++)
		for (int h = 0; h < NHUES; h++)
			CHECK(status_scale(hues[h].full, over[i]) == hues[h].full,
			      "level %u: %s is %06x", over[i], hues[h].name, status_scale(hues[h].full, over[i]));

	/* 0 dark, 255 the table, 70 v3.6's colours exactly */
	for (int h = 0; h < NHUES; h++) {
		CHECK(status_scale(hues[h].full, 0) == 0, "level 0: %s lit", hues[h].name);
		CHECK(status_scale(hues[h].full, 255) == hues[h].full, "level 255: %s is %06x",
		      hues[h].name, status_scale(hues[h].full, 255));
		CHECK(status_scale(hues[h].full, 70) == hues[h].v36, "level 70: %s is %06x, not v3.6's %06x",
		      hues[h].name, status_scale(hues[h].full, 70), hues[h].v36);
	}

	/* the default: the brightest channel 8 to 10 of 255 (v3.6: 70), each
	   colour about an eighth of v3.6's light (its channels' sum, which is
	   what the LED's three PWM duties add up to), as STATUS-LED.md lists */
	uint32_t top_d = 0;
	printf("brightness: the default, %u of 255:", d);
	for (int h = 0; h < NHUES; h++) {
		uint32_t s = status_scale(hues[h].full, d), sum = 0, sum36 = 0;
		for (int k = 0; k < 3; k++) {
			sum += chan(s, k);
			sum36 += chan(hues[h].v36, k);
			if (chan(s, k) > top_d)
				top_d = chan(s, k);
		}
		double share = (double) sum / sum36;
		printf(" %s %06x (%.3f of v3.6's)", hues[h].name, s, share);
		CHECK(s == hues[h].at_default, "the default: %s is %06x, not %06x", hues[h].name, s,
		      hues[h].at_default);
		CHECK(share >= 0.10 && share <= 0.16, "the default: %s gives %.3f of v3.6's light, "
		      "not about an eighth", hues[h].name, share);
	}
	printf("\n");
	CHECK(top_d >= 8 && top_d <= 10, "the default's brightest channel is %u, not 8 to 10", top_d);

	/* at every level above 0: no lit channel put out, no dark one lit, so
	   every colour stays itself and unlike the others (yellow never red,
	   white never magenta); from the default up, every two lit channels of
	   a colour within a quarter of the table's ratio (yellow stays yellow) */
	double worst = 0;
	const char *worst_name = "";
	uint32_t worst_level = 0;
	for (uint32_t level = 1; level <= 255; level++) {
		for (int h = 0; h < NHUES; h++) {
			uint32_t s = status_scale(hues[h].full, level);
			for (int k = 0; k < 3; k++)
				CHECK(!chan(s, k) == !chan(hues[h].full, k), "level %u: %s channel %d is %u",
				      level, hues[h].name, k, chan(s, k));
			for (int j = 0; j < h; j++)
				CHECK(s != status_scale(hues[j].full, level), "level %u: %s and %s the same",
				      level, hues[h].name, hues[j].name);
			if (level < d)
				continue;
			for (int a = 0; a < 3; a++)
				for (int b = a + 1; b < 3; b++) {
					if (!chan(hues[h].full, a) || !chan(hues[h].full, b))
						continue;
					double want = (double) chan(hues[h].full, a) / chan(hues[h].full, b);
					double got = (double) chan(s, a) / chan(s, b);
					double off = got > want ? got / want - 1 : want / got - 1;
					if (off > worst) {
						worst = off;
						worst_name = hues[h].name;
						worst_level = level;
					}
					CHECK(off <= 0.25, "level %u: %s's channels %d and %d %.3f, the table's %.3f",
					      level, hues[h].name, a, b, got, want);
				}
		}
	}
	printf("brightness: at every level 1 to 255 each colour keeps its lit channels and differs "
	       "from the others; from %u up its channels' ratios are within %.1f%% of the table's "
	       "(the most off: %s at %u; yellow at the default %u:%u, the table's %u:%u)\n", d,
	       worst * 100, worst_name, worst_level, chan(status_scale(YELLOW_FULL, d), 0),
	       chan(status_scale(YELLOW_FULL, d), 1), chan(YELLOW_FULL, 0), chan(YELLOW_FULL, 1));

	/* the level as set: held to 0 to 255 */
	static const struct { int in; uint32_t out; } sets[] = {
		{ INT_MIN, 0 }, { -1, 0 }, { 0, 0 }, { 1, 1 }, { 70, 70 }, { 255, 255 }, { 256, 255 },
		{ INT_MAX, 255 }, { 9, 9 },
	};
	for (unsigned i = 0; i < sizeof(sets) / sizeof(sets[0]); i++) {
		status_brightness_set(sets[i].in);
		CHECK(status_brightness() == sets[i].out, "set to %d: the level is %u, not %u", sets[i].in,
		      status_brightness(), sets[i].out);
	}

	/* every frame at the level set, and the panic path's frames with the
	   same bits as the RMT's: the crash's red and white, the early crash's
	   yellow and its dark, at the default, 0, 1, 70 and 255 */
	static const uint32_t levels[] = { STATUS_BRIGHTNESS_DEFAULT, 0, 1, 70, 255 };
	int frames = 0;
	for (unsigned l = 0; l < sizeof(levels) / sizeof(levels[0]); l++) {
		status_brightness_set((int) levels[l]);
		for (int h = 0; h < NHUES; h++)
			CHECK(status_frame_grb(hues[h].full) == status_grb(model_rgb(hues[h].full, levels[l])),
			      "level %u: %s's frame %06x", levels[l], hues[h].name,
			      status_frame_grb(hues[h].full));
		static const struct { bool early; uint32_t ms; } shown[] = {
			{ false, 0 }, { false, 250 }, { true, 0 }, { true, 400 },
		};
		for (unsigned i = 0; i < 4; i++) {
			uint32_t rgb = status_panic_pattern(shown[i].early, shown[i].ms);
			uint32_t want = status_grb(model_rgb(rgb, levels[l]));
			uint32_t grb = status_frame_grb(rgb), bb = bb_bits(grb), rmt = rmt_bits(grb);
			frames += 2;
			CHECK(bb == want && rmt == want, "level %u, %s crash at %u ms (%06x): the bit-bang "
			      "sends %06x, the RMT %06x, not %06x", levels[l], shown[i].early ? "an early" :
			      "a", shown[i].ms, rgb, bb, rmt, want);
		}
	}
	status_brightness_set(STATUS_BRIGHTNESS_DEFAULT);
	CHECK(bb_bits(status_frame_grb(status_panic_pattern(false, 0))) == status_grb(0x090000) &&
	      bb_bits(status_frame_grb(status_panic_pattern(true, 0))) == status_grb(0x080500),
	      "the default: the crash's red or the early crash's yellow is not as STATUS-LED.md says");
	printf("brightness: %d frames of the crash patterns, bit-banged and RMT, at 5 levels: the same "
	       "scaled bits\n", frames);

	/* the code itself: every frame in status_led.c made through
	   status_frame_grb(), and status_code.c keeping it, the scale and the
	   level in internal RAM, with no 64-bit arithmetic in the scale */
	if (!src_dir) {
		printf("brightness: no esp/main path given, so status_led.c and status_code.c not read\n");
		return;
	}
	char *led = read_code(src_dir, "status_led.c"), *code = read_code(src_dir, "status_code.c");
	CHECK(led && code, "status_led.c or status_code.c not found in %s", src_dir);
	if (!led || !code) {
		free(led);
		free(code);
		return;
	}
	int good, n;
	n = calls_of(led, "status_rmt_frame", "status_frame_grb(", &good);
	CHECK(n >= 1 && good == n, "status_led.c: %d of its %d status_rmt_frame() calls take "
	      "status_frame_grb()", good, n);
	int bbn, bbgood;
	bbn = calls_of(led, "status_bb_frame", "status_frame_grb(", &bbgood);
	CHECK(bbn >= 2 && bbgood == bbn, "status_led.c: %d of its %d status_bb_frame() calls take "
	      "status_frame_grb()", bbgood, bbn);
	int raw = calls_of(led, "status_grb", NULL, &good);
	CHECK(raw == 0, "status_led.c calls status_grb() %d times, a colour without the brightness", raw);
	CHECK(strstr(code, "STATUS_IRAM uint32_t status_scale(") &&
	      strstr(code, "STATUS_IRAM uint32_t status_frame_grb(") &&
	      strstr(code, "static STATUS_DRAM volatile uint32_t s_level"),
	      "status_code.c: the scale, status_frame_grb() or the level not marked for internal RAM");
	char body[2048];
	body_of(code, "STATUS_IRAM uint32_t status_frame_grb(", body, sizeof(body));
	CHECK(strstr(body, "s_level") && calls_of(body, "status_scale", NULL, &good) == 1,
	      "status_frame_grb() does not scale by the level");
	body_of(code, "STATUS_IRAM uint32_t status_scale(", body, sizeof(body));
	CHECK(body[0] && !strstr(body, "uint64_t") && !strstr(body, "long") && !strstr(body, "double") &&
	      !strstr(body, "float"), "status_scale(): 64-bit or floating-point arithmetic, or not found");
	printf("brightness: status_led.c makes its %d RMT and %d bit-banged frames through "
	       "status_frame_grb() and calls status_grb() nowhere; status_code.c has the scale, "
	       "status_frame_grb() and the level in internal RAM, the scale in 32 bits\n", n, bbn);
	free(led);
	free(code);
}

/* ==== 9. the disk light (v3.8) ==== */

/* one look of the LED task as status_led.c makes it: the beat watch (and
   yellow 5 on or off), then the disk count against the last look, then the
   colour */
static uint32_t look(struct status_state *base, struct status_watch *w, struct status_disk *d,
		     uint32_t beats, uint32_t count, uint32_t now)
{
	bool running = base->stage == STATUS_STAGE_RUNNING;
	bool was = base->active & (1u << STATUS_ERR_STALLED);
	bool stalled = status_watch_step(w, beats, now, running);
	if (stalled != was)
		status_state_error(base, STATUS_ERR_STALLED, stalled);
	struct status_state st = *base;
	st.disk = running && status_disk_step(d, count);
	return status_pattern(&st, now);
}

/* the guest's accesses as times in ms (sorted): how many came before t */
static uint32_t accesses_before(const uint32_t *at, int n, uint32_t t)
{
	uint32_t k = 0;
	while ((int) k < n && at[k] < t)
		k++;
	return k;
}

/* a running PC from 0 to end ms, the emulator beating until beat_end,
   looked at every STATUS_TICK_MS: how many looks were blue, green, yellow
   (any blink of it) and other; the longest run of blue looks; the first
   yellow look's time */
struct sim_out {
	int blue, green, yellow, other, longest_blue;
	uint32_t first_yellow, first_blue;
};

static struct sim_out sim_run(const uint32_t *at, int n, uint32_t end, uint32_t beat_end)
{
	struct status_state s = { 0 };
	struct status_watch w = { 0 };
	struct status_disk d = { 0 };
	struct sim_out o = { 0 };
	const uint32_t blue = status_stage_rgb(STATUS_STAGE_SCREEN);
	const uint32_t green = status_stage_rgb(STATUS_STAGE_RUNNING);
	int run = 0;
	o.first_yellow = o.first_blue = 0xffffffffu;
	status_state_stage(&s, STATUS_STAGE_RUNNING);
	for (uint32_t t = STATUS_TICK_MS; t <= end; t += STATUS_TICK_MS) {
		uint32_t beats = (t < beat_end ? t : beat_end) / 10;
		uint32_t c = look(&s, &w, &d, beats, accesses_before(at, n, t), t);
		if (c == blue) {
			o.blue++;
			if (++run > o.longest_blue)
				o.longest_blue = run;
			if (o.first_blue == 0xffffffffu)
				o.first_blue = t;
			continue;
		}
		run = 0;
		if (c == green)
			o.green++;
		else if (c == YELLOW_FULL || (c == 0 && (s.active & (1u << STATUS_ERR_STALLED))))
			o.yellow++, o.first_yellow = o.first_yellow == 0xffffffffu ? t : o.first_yellow;
		else
			o.other++;
	}
	return o;
}

static void test_disk_light(const char *src_dir)
{
	CHECK(STATUS_TICK_MS >= 20 && STATUS_TICK_MS <= 30 && STATUS_TICK_MS % 10 == 0,
	      "a look every %d ms: not 20 to 30 ms in whole 10 ms ticks", STATUS_TICK_MS);

	/* the count against the last look */
	struct status_disk d = { 0 };
	CHECK(!status_disk_step(&d, 0), "no access yet: lit");
	CHECK(status_disk_step(&d, 1) && !status_disk_step(&d, 1), "one access: not one look lit");
	CHECK(status_disk_step(&d, 5000) && !status_disk_step(&d, 5000), "many: not one look lit");
	d.last = 0xffffffffu;
	CHECK(status_disk_step(&d, 0) && !status_disk_step(&d, 0), "the count wrapping round");

	/* no access: green all along, never dark */
	struct sim_out o = sim_run(NULL, 0, 20000, 20000);
	CHECK(o.green == 20000 / STATUS_TICK_MS && !o.blue && !o.yellow && !o.other,
	      "no access: %d green, %d blue, %d yellow, %d other looks", o.green, o.blue, o.yellow,
	      o.other);
	/* one access: one look blue, the next one after it */
	static const uint32_t one[] = { 1003 };
	o = sim_run(one, 1, 5000, 5000);
	CHECK(o.blue == 1 && o.first_blue == (1003 / STATUS_TICK_MS + 1) * STATUS_TICK_MS &&
	      !o.other && !o.yellow, "one access at 1003 ms: %d blue looks, the first at %u ms",
	      o.blue, o.first_blue);
	/* a burst, an access every 5 ms for 300 ms: lit all through it */
	uint32_t burst[61];
	for (int i = 0; i < 61; i++)
		burst[i] = 2000 + 5 * i;
	o = sim_run(burst, 61, 5000, 5000);
	CHECK(o.blue == o.longest_blue && o.blue >= 300 / STATUS_TICK_MS &&
	      o.blue <= 300 / STATUS_TICK_MS + 1, "a 300 ms burst: %d blue looks, %d in a row",
	      o.blue, o.longest_blue);
	/* accesses 50 ms apart for a second: a flash each, green between */
	uint32_t apart[20];
	for (int i = 0; i < 20; i++)
		apart[i] = 3001 + 50 * i;
	o = sim_run(apart, 20, 5000, 5000);
	CHECK(o.blue == 20 && o.longest_blue == 1 && !o.other,
	      "20 accesses 50 ms apart: %d blue looks, at most %d in a row (a flicker wanted)",
	      o.blue, o.longest_blue);
	/* the emulator stopped at 4 s with the light green: yellow 5 at 14 s,
	   with no blink of the old kind to show it */
	o = sim_run(one, 1, 20000, 4000);
	CHECK(o.first_yellow >= 4000 + STATUS_STALL_MS - STATUS_TICK_MS &&
	      o.first_yellow <= 4000 + STATUS_STALL_MS + STATUS_TICK_MS && !o.other,
	      "stopped at 4000 ms: yellow 5 from %u ms (14,000 wanted)", o.first_yellow);
	/* accesses while yellow 5 shows (they cannot come, but the order is
	   the point): the error goes over the light */
	uint32_t late[100];
	for (int i = 0; i < 100; i++)
		late[i] = 15000 + 30 * i;
	o = sim_run(late, 100, 18000, 4000);
	CHECK(!o.blue, "yellow 5 and the disk light: %d blue looks (the error goes over it)", o.blue);
	/* the light in another stage, or under an error: not shown */
	struct status_state s = { 0 };
	status_state_stage(&s, STATUS_STAGE_BIOS);
	s.disk = 1;
	CHECK(status_pattern(&s, 0) == status_stage_rgb(STATUS_STAGE_BIOS),
	      "the BIOS stage with the light: not its green");
	status_state_stage(&s, STATUS_STAGE_RUNNING);
	status_state_error(&s, STATUS_ERR_NO_MONITOR, true);
	CHECK(status_pattern(&s, 0) == YELLOW_FULL && status_pattern(&s, 400) == 0,
	      "no monitor with the light: not yellow 3");
	status_replay_setup(&s, STATUS_CAUSE_CRASH, STATUS_STAGE_RUNNING, 0);
	status_state_error(&s, STATUS_ERR_NO_MONITOR, false);
	CHECK(status_pattern(&s, 50) == WHITE_FULL && status_pattern(&s, 1000) == status_stage_rgb(
	      STATUS_STAGE_RUNNING), "the replay with the light: not the replay");
	printf("disk light: a look every %d ms; one access lights one look, a burst lights it all "
	       "through, accesses %d ms apart flicker, a stopped emulator still gives yellow 5 after "
	       "%d s\n", STATUS_TICK_MS, 50, STATUS_STALL_MS / 1000);

	/* the code: the LED task looks at the count, ide.c and misc.c count
	   every access with one store, esp_main.c gives the LED the count
	   before the PC runs */
	if (!src_dir) {
		printf("disk light: no esp/main path given, so the sources not read\n");
		return;
	}
	char *led = read_code(src_dir, "status_led.c"), *ide = read_code(src_dir, "../../ide.c");
	char *misc = read_code(src_dir, "../../misc.c"), *esp = read_code(src_dir, "esp_main.c");
	CHECK(led && ide && misc && esp, "status_led.c, ide.c, misc.c or esp_main.c not found from %s",
	      src_dir);
	if (led && ide && misc && esp) {
		static char body[8192];
		int good;
		body_of(led, "static void led_task(", body, sizeof(body));
		const char *watch = strstr(body, "status_watch_step("), *step = strstr(body, "status_disk_step(");
		const char *set = strstr(body, "st.disk ="), *pat = strstr(body, "status_pattern(&st");
		CHECK(watch && step && set && pat && watch < step && step < pat && set < pat &&
		      !strstr(body, "BEAT"), "led_task(): the beat watch, the disk step and st.disk "
		      "before the colour");
		body_of(ide, "static int bf_read_async(", body, sizeof(body));
		CHECK(calls_of(body, "BF_ACTIVITY", NULL, &good) == 1, "bf_read_async(): not one "
		      "BF_ACTIVITY()");
		body_of(ide, "static int bf_write_async(", body, sizeof(body));
		CHECK(calls_of(body, "BF_ACTIVITY", NULL, &good) == 1, "bf_write_async(): not one "
		      "BF_ACTIVITY()");
		CHECK(strstr(ide, "#define BF_ACTIVITY() (ide_activity++)") != NULL,
		      "ide.c: BF_ACTIVITY() is not one store to ide_activity");
		body_of(misc, "int emulink_data_write_string(", body, sizeof(body));
		int fw = calls_of(body, "FDD_ACTIVITY", NULL, &good);
		body_of(misc, "int emulink_data_read_string(", body, sizeof(body));
		int fr = calls_of(body, "FDD_ACTIVITY", NULL, &good);
		CHECK(fw == 1 && fr == 1 &&
		      strstr(misc, "#define FDD_ACTIVITY() (emulink_activity++)") != NULL,
		      "misc.c: not one FDD_ACTIVITY() in the floppy's write (%d) and read (%d), each a "
		      "store", fw, fr);
		body_of(esp, "static uint32_t disk_activity(", body, sizeof(body));
		CHECK(strstr(body, "ide_activity + emulink_activity") != NULL,
		      "esp_main.c: disk_activity() does not add the two counts");
		const char *give = strstr(esp, "status_led_disk_counter(disk_activity)");
		const char *run = strstr(esp, "status_led_stage(STATUS_STAGE_RUNNING)");
		CHECK(give && run && give < run, "esp_main.c: the count not given before the PC runs");
		printf("disk light: led_task() looks at the count; bf_read_async(), bf_write_async() and "
		       "the floppy's read and write count each access with one store; esp_main.c gives "
		       "the LED the count before the PC runs\n");
	}
	free(led);
	free(ide);
	free(misc);
	free(esp);
}

int main(int argc, char **argv)
{
	test_rmt();
	test_bitbang();
	test_patterns();
	test_state();
	test_replay();
	test_watch();
	test_record();
	test_brightness(argc > 1 ? argv[1] : NULL);
	test_disk_light(argc > 1 ? argv[1] : NULL);
	printf("status LED: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
