/*
 * The status LED's logic on a PC: esp/main/status_code.c as the firmware has
 * it (STATUS_HOST), and esp/main/status_bb.h, the panic handler's bit-bang,
 * with a pretend cycle counter.
 *
 *   1. The RMT frame: a reset of 300 us or more, then 24 bits whose high and
 *      low times fall inside both LEDs' windows (Inolux IN-PI554FCH and
 *      Worldsemi WS2812B-2020-V6), decoding back to the colour, green first.
 *   2. The bit-bang, cycle by cycle: at 80 MHz (the second-stage
 *      bootloader's clock, which a panic before app_main() runs at), 160
 *      and 320 MHz, with each pass round the waiting loop costing 1 to 6
 *      cycles and each store to the GPIO register 1 to 24, and with the
 *      counter wrapping round in the middle of a frame: every edge inside
 *      both windows, the bits back.
 *   3. The patterns: each stage's colour, each error's colour and number of
 *      blinks, the crash, the heartbeat, the PC switched off.
 *   4. The error-code state machine: which error shows over which, the
 *      first fatal one staying, errors clearing themselves.
 *   5. The replay after a reset: the lead-in's flashes, the old code, then
 *      the present state.
 *   6. The beat watch: no progress for 10 s is a stall, only while running.
 *   7. The record kept over a reset: written, read back, refused when any
 *      byte of it is wrong.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "status_code.h"

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
/* Inolux IN-PI554FCH v2.2 p. 10: 0.3, 0.9, 0.6, 0.6 us each +-0.15 us, a
   bit 1.25 +-0.6 us, reset 80 us or more */
static const struct window inolux = {
	"Inolux IN-PI554FCH", 150, 450, 750, 1050, 450, 750, 450, 750, 650, 1850, 80000,
};
/* Worldsemi WS2812B-2020-V6: T0H 220-380, T1H 580-1000, T0L and T1L 580-1000
   ns, a bit 1250 ns or more, reset 280 us or more */
static const struct window worldsemi = {
	"Worldsemi WS2812B", 220, 380, 580, 1000, 580, 1000, 580, 1000, 1250, 1000000, 280000,
};
static const struct window *const windows[] = { &inolux, &worldsemi };

/* the least margin seen to any window edge, ns */
static double margin_min = 1e9;

/* lo < 0: the frame's last bit, whose low runs on into the reset: its
   high time only */
static void bit_in_windows(const char *where, int one, double hi, double lo)
{
	for (int k = 0; k < 2; k++) {
		const struct window *w = windows[k];
		int hl = one ? w->t1h_lo : w->t0h_lo, hh = one ? w->t1h_hi : w->t0h_hi;
		int ll = one ? w->t1l_lo : w->t0l_lo, lh = one ? w->t1l_hi : w->t0l_hi;
		CHECK(hi >= hl && hi <= hh, "%s: %s high %.1f ns, %s wants %d to %d",
		      where, one ? "1" : "0", hi, w->who, hl, hh);
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
			if (m[i] < margin_min)
				margin_min = m[i];
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
			bit_in_windows(where, one, hi, lo);
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
static int sim_cost_read, sim_cost_store;
static int sim_level;
static uint64_t sim_edge[64];
static int sim_edge_level[64];
static int sim_nedges;
static uint64_t sim_reads;

static uint32_t sim_now(void)
{
	sim_cycles += sim_cost_read;
	sim_reads++;
	return (uint32_t) sim_cycles;   /* the counter is 32 bits: it wraps */
}

static void sim_store(int level)
{
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

	for (unsigned f = 0; f < 3; f++) {
		struct status_bb t;
		status_bb_timing(&t, mhz[f]);
		for (unsigned k = 0; k < sizeof(costs) / sizeof(costs[0]); k++) {
			/* the slow costs are for a fast clock: 24 cycles a store at
			   80 MHz would be 300 ns, which no bus takes */
			if (mhz[f] < 160 && (costs[k][0] > 3 || costs[k][1] > 8))
				continue;
			for (unsigned c = 0; c < 4; c++) {
				uint32_t grb = colours[c];
				sim_cost_read = costs[k][0];
				sim_cost_store = costs[k][1];
				/* the counter wraps 20 us into the frame */
				sim_cycles = 0x100000000ull - (STATUS_RESET_US + 20) * mhz[f];
				sim_level = 1;          /* the line high before: R205 */
				sim_nedges = 0;
				sim_reads = 0;
				uint64_t start = sim_cycles;
				status_bb_frame(grb, &t);
				uint64_t end = sim_cycles;
				runs++;
				char where[96];
				snprintf(where, sizeof(where), "bit-bang %u MHz, read %d, store %d, %06x",
					 mhz[f], costs[k][0], costs[k][1], grb);
				CHECK(sim_nedges == 1 + 48, "%s: %d edges", where, sim_nedges);
				if (sim_nedges != 49)
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
					/* the last bit's low runs on into the
					   line's rest, or the next frame's reset */
					double lo = i < 23 ? (sim_edge[3 + 2 * i] - sim_edge[2 + 2 * i]) * ns : -1;
					int one = hi > 500;
					bit_in_windows(where, one, hi, lo);
					back = back << 1 | one;
				}
				if (margin_min < worst)
					worst = margin_min;
				if (before < margin_min)
					margin_min = before;
				CHECK(back == grb, "%s: decodes to %06x", where, back);
				if (c == 1 && k == 0)
					printf("bit-bang: %3u MHz: %llu cycles a frame (%.1f us), %llu reads "
					       "of the counter\n", mhz[f],
					       (unsigned long long) (end - start),
					       (end - start) * ns / 1000, (unsigned long long) sim_reads);
			}
		}
	}
	printf("bit-bang: %d frames, every edge checked; least margin to a window edge %.1f ns\n",
	       runs, worst);
	/* the cycle counts themselves */
	struct status_bb t;
	status_bb_timing(&t, 320);
	CHECK(t.t0h == 108 && t.t0 == 416 && t.t1h == 212 && t.t1 == 416 && t.reset == 96000,
	      "320 MHz: t0h %u t0 %u t1h %u t1 %u reset %u", t.t0h, t.t0, t.t1h, t.t1, t.reset);
	status_bb_timing(&t, 80);
	CHECK(t.t0h == 27 && t.t0 == 104 && t.t1h == 53 && t.t1 == 104 && t.reset == 24000,
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
	/* running: green, dark for 100 ms every 2 s */
	struct status_state s = { 0 };
	status_state_stage(&s, STATUS_STAGE_RUNNING);
	int dark = 0;
	for (uint32_t t = 0; t < 2000; t++)
		dark += status_pattern(&s, 4000 + t) == 0;
	CHECK(dark == 100, "running: dark %d ms of 2000", dark);
	CHECK(status_pattern(&s, 4500) == status_stage_rgb(STATUS_STAGE_BIOS),
	      "running is not the BIOS stage's green");
	/* the PC switched off: white a second, dark a second */
	status_state_stage(&s, STATUS_STAGE_PC_OFF);
	CHECK(status_pattern(&s, 500) == status_stage_rgb(STATUS_STAGE_STARTED) &&
	      status_pattern(&s, 1500) == 0, "PC switched off: not a slow white blink");

	/* errors: red for the card and its files, yellow for the board, the
	   number of blinks their place in each */
	static const struct { int err, blinks; uint32_t rgb; } table[] = {
		{ STATUS_ERR_NO_CARD, 1, 0x460000 }, { STATUS_ERR_NO_INI, 2, 0x460000 },
		{ STATUS_ERR_BIOS, 3, 0x460000 }, { STATUS_ERR_INI, 4, 0x460000 },
		{ STATUS_ERR_STOPPED, 5, 0x460000 }, { STATUS_ERR_PSRAM, 1, 0x3c2300 },
		{ STATUS_ERR_HDMI_CHIP, 2, 0x3c2300 }, { STATUS_ERR_NO_MONITOR, 3, 0x3c2300 },
		{ STATUS_ERR_KEYBOARD, 4, 0x3c2300 }, { STATUS_ERR_STALLED, 5, 0x3c2300 },
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
		CHECK(c == ((t / 250) & 1 ? 0x282828u : 0x460000u), "crash at %u ms: %06x", t, c);
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
	CHECK(status_pattern(&s, 0) == 0x460000, "no card: not red at 0 ms");
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
	CHECK(status_pattern(&r, 0) == 0x3c2300, "stall: not yellow");
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
		CHECK(c == 0 || c == 0x282828, "lead-in at %u ms: %06x", t, c);
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
		CHECK(c == 0 || c == 0x3c2300, "old code at %u ms: %06x", t, c);
		prev = c;
	}
	CHECK(blinks == 3, "old code: %d blinks", blinks);
	CHECK(status_pattern(&s, s.replay_end_ms) == status_stage_rgb(STATUS_STAGE_STARTED),
	      "after the replay: not the present stage");
	/* a present error during the replay waits for it */
	status_state_fatal(&s, STATUS_ERR_NO_INI);
	CHECK(status_pattern(&s, 100) == 0x282828, "a present error cut the lead-in short");
	CHECK(status_pattern(&s, s.replay_end_ms) == 0x460000, "after the replay: not red 2");

	/* a crash (1 flash), the last run showing nothing: its stage, 2 s */
	struct status_state c = { 0 };
	status_replay_setup(&c, STATUS_CAUSE_CRASH, STATUS_STAGE_CARD, 0);
	status_state_stage(&c, STATUS_STAGE_STARTED);
	CHECK(c.replay_end_ms == 300 + 600 + 2000, "crash replay ends at %u", c.replay_end_ms);
	CHECK(status_pattern(&c, 50) == 0x282828 && status_pattern(&c, 200) == 0 &&
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
	CHECK(n.replay_end_ms == 0 && status_pattern(&n, 0) == 0x282828, "no cause: a replay");
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

int main(void)
{
	test_rmt();
	test_bitbang();
	test_patterns();
	test_state();
	test_replay();
	test_watch();
	test_record();
	printf("status LED: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
