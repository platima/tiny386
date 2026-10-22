/*
 * The PIT's timer interrupts after the emulator has been held up (v3.8,
 * research\WIN95-PC-REPRO.md 6): i8254.c with a pretend clock and a
 * pretend PIC that counts the IRQ 0 edges it is given, one
 * i8254_update_irq() a step, as pc_step() calls it.
 *
 * tiny386 as released raised every tick it had missed, one a step, back to
 * back; Windows 95's VMM took them nested until its ring 0 stack ran off its
 * page.  With i8254_max_owed (default 1) the ticks beyond that are dropped
 * and one is raised, on the counter's own phase, as a real 8254 and 8259
 * would.  Also the CMOS periodic interrupt (misc.c), which caught up the
 * same way.
 *
 *   test_pit [pristine]     "pristine": i8254.c as tiny386 released it, where
 *                           the burst must be seen (the test sees the bug)
 *
 * Exit status 0 when every check passes.
 */
#define _GNU_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "i8254.h"

static int checks, fails;
#define CHECK(c, ...) do { \
	checks++; \
	if (!(c)) { \
		fails++; \
		printf("  FAIL line %d: ", __LINE__); \
	} else { \
		printf("  ok: "); \
	} \
	printf(__VA_ARGS__); \
	printf("\n"); \
} while (0)

/* the clock, in microseconds */
static uint64_t s_us;
uint32_t get_uticks(void)
{
	return (uint32_t) s_us;
}

/* the PIC: rising edges on IRQ 0, and when */
static int s_edges;
static int s_level;
static uint64_t s_edge_us[4096];
static void set_irq(void *pic, int irq, int level)
{
	(void) pic;
	if (irq == 0 && level && !s_level) {
		if (s_edges < 4096)
			s_edge_us[s_edges] = s_us;
		s_edges++;
	}
	s_level = level;
}

/* the period of channel 0 at its reset count (65536), in microseconds */
#define PERIOD_US (65536.0 * 1000000.0 / PIT_FREQ)

/* n steps of pc_step()'s kind with no time passing between them: the
   edges they raise */
static int burst(PITState *pit, int n)
{
	int e0 = s_edges;
	for (int i = 0; i < n; i++)
		i8254_update_irq(pit);
	return s_edges - e0;
}

/* time moving on in 232 us steps (512 instructions at 2.2 million a
   second) for a while: the edges raised */
static int run_for(PITState *pit, double us)
{
	int e0 = s_edges;
	uint64_t end = s_us + (uint64_t) us;
	while (s_us < end) {
		s_us += 232;
		i8254_update_irq(pit);
	}
	return s_edges - e0;
}

#ifndef PIT_PRISTINE
/* the CMOS periodic interrupt (misc.c), on the same pretend clock */
#include "misc.h"
int clock_gettime(clockid_t id, struct timespec *ts)
{
	(void) id;
	ts->tv_sec = s_us / 1000000;
	ts->tv_nsec = (s_us % 1000000) * 1000;
	return 0;
}
static int s_rtc_edges;
static void rtc_irq(void *pic, int irq, int level)
{
	(void) pic;
	if (irq == 8 && level)
		s_rtc_edges++;
}

static void cmos_checks(void)
{
	printf("== the CMOS periodic interrupt (misc.c)\n");
	CMOS *c = cmos_init(0, 8, NULL, rtc_irq);
	/* 1024 Hz (rate 6), then PIE on */
	cmos_ioport_write(c, 0x70, 10);
	cmos_ioport_write(c, 0x71, 0x26);
	cmos_ioport_write(c, 0x70, 11);
	cmos_ioport_write(c, 0x71, 0x42);
	s_rtc_edges = 0;
	uint64_t t0 = s_us;
	while (s_us < t0 + 100000) {
		s_us += 232;
		cmos_update_irq(c);
	}
	CHECK(s_rtc_edges >= 101 && s_rtc_edges <= 103, "100 ms at 1024 Hz, steps of 232 us: "
	      "%d interrupts (about 102)", s_rtc_edges);
	s_us += 500000;         /* held up for half a second: 512 periods */
	s_rtc_edges = 0;
	for (int i = 0; i < 1000; i++)
		cmos_update_irq(c);
	CHECK(s_rtc_edges == 1, "after a 500 ms hold, 1,000 steps with no time passing: %d "
	      "interrupt (1 wanted, not 512)", s_rtc_edges);
	s_rtc_edges = 0;
	t0 = s_us;
	while (s_us < t0 + 100000) {
		s_us += 232;
		cmos_update_irq(c);
	}
	CHECK(s_rtc_edges >= 101 && s_rtc_edges <= 103, "and then 100 ms at the usual rate: %d",
	      s_rtc_edges);
}
#endif

int main(int argc, char **argv)
{
	bool pristine = argc > 1 && !strcmp(argv[1], "pristine");
	setvbuf(stdout, NULL, _IONBF, 0);
	s_us = 1000000;
	PITState *pit = i8254_init(0, NULL, set_irq);

	printf("== channel 0 at its reset count, 18.2 Hz\n");
	int n = run_for(pit, 1000000);
	CHECK(n >= 18 && n <= 19, "1 s in 232 us steps: %d ticks (18.2 a second)", n);

	/* held up for 100 periods (5.5 s: a card that walked the FAT) */
	s_us += (uint64_t) (100 * PERIOD_US) + 1000;
	n = burst(pit, 1000);
	printf("  after a hold of 100 periods, 1,000 steps with no time passing: %d ticks\n", n);
	if (pristine) {
		/* the bug, as released: every missed tick back to back */
		CHECK(n >= 99, "pristine i8254.c: the burst is there (%d ticks back to back)", n);
		printf("test_pit (pristine): %d checks, %d failed\n", checks, fails);
		return fails ? 1 : 0;
	}
#ifndef PIT_PRISTINE
	CHECK(n == 1, "one tick raised, not a burst (%d)", n);
	CHECK(i8254_owed_max >= 100 && i8254_owed_max <= 101, "the most owed at once: %u",
	      (unsigned) i8254_owed_max);
	CHECK(i8254_dropped >= 99 && i8254_dropped <= 100, "ticks dropped: %u",
	      (unsigned) i8254_dropped);

	/* the next one a period later, on the counter's own phase: the
	   counter, read through the ports, wraps when it comes */
	int e0 = s_edges;
	n = run_for(pit, PERIOD_US * 1.5);
	CHECK(n == 1, "the next tick within a period and a half (%d)", n);
	if (n == 1) {
		uint64_t at = s_edge_us[e0];
		/* periods since the count was loaded at s_us = 1000000 */
		double ph = (double) (at - 1000000) / PERIOD_US;
		double frac = ph - (long) ph;
		CHECK(frac < 232.0 / PERIOD_US + 0.0001, "on the counter's phase: %.4f periods "
		      "after a wrap (within one step)", frac);
	}
	n = run_for(pit, 1000000);
	CHECK(n >= 18 && n <= 19, "then 1 s as usual: %d ticks", n);

	/* a hold shorter than a period ends with at most one tick owed, the
	   allowance: nothing dropped, as before */
	uint32_t d0 = i8254_dropped;
	int got = 0;
	for (int k = 0; k < 20; k++) {
		s_us += (uint64_t) (0.6 * PERIOD_US);
		got += burst(pit, 50);
	}
	CHECK(got >= 11 && got <= 13 && i8254_dropped == d0, "20 holds of 0.6 periods: %d ticks "
	      "(12 periods went by), none dropped", got);

	/* a larger allowance catches up that many */
	i8254_max_owed = 4;
	run_for(pit, PERIOD_US * 2);
	s_us += (uint64_t) (3 * PERIOD_US) + 100;
	n = burst(pit, 50);
	CHECK(n == 3, "i8254_max_owed 4, a hold of 3 periods: %d ticks made up (3)", n);
	s_us += (uint64_t) (40 * PERIOD_US) + 100;
	n = burst(pit, 50);
	CHECK(n == 1, "i8254_max_owed 4, a hold of 40 periods: %d tick (1)", n);

	/* 0: every one, as tiny386 was */
	i8254_max_owed = 0;
	run_for(pit, PERIOD_US * 2);
	s_us += (uint64_t) (30 * PERIOD_US) + 100;
	n = burst(pit, 100);
	CHECK(n >= 30 && n <= 31, "i8254_max_owed 0: a hold of 30 periods gives %d ticks back "
	      "to back, as before v3.8", n);
	i8254_max_owed = 1;

	/* a fast timer (Windows' 1 ms multimedia timer): reprogrammed, then
	   held up */
	i8254_ioport_write(pit, 0x43, 0x34);    /* channel 0, both bytes, mode 2 */
	i8254_ioport_write(pit, 0x40, 1193 & 0xff);
	i8254_ioport_write(pit, 0x40, 1193 >> 8);
	n = run_for(pit, 100000);
	CHECK(n >= 99 && n <= 101, "1 kHz for 100 ms in 232 us steps: %d ticks", n);
	s_us += 200000;
	n = burst(pit, 500);
	CHECK(n == 1, "1 kHz, held up 200 ms: %d tick (1, not 200)", n);

	cmos_checks();
#endif
	printf("test_pit: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
