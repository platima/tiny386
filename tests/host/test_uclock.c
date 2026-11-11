/*
 * test_uclock.c: esp/main/uclock.h, core 1's clock from its cycle counter
 * (QW_UCLK), against a pretend core: a cycle counter, a systimer whose
 * read latches part way through and takes a while, waits in which the
 * counter runs or stops, interrupts that read the clock (or set the pair)
 * between any two of a read's loads, a tick every 10 ms, long stretches
 * with no read, both counters across their wraps,
 * and counters that run fast or slow against the clock.
 *
 * Every value read is checked: never below a value read before it began,
 * never above the true clock at the moment it was taken (when the counter
 * keeps time), and behind it by no more than LAG us.  With a counter that
 * does not keep time the clock has to go back to the true read, and still
 * never go back.  The reciprocal is checked for every difference below
 * UCLK_SPAN at several frequencies.  v5.3 (the review of batch 4): a wait
 * at times comes with a read between the idle hook and the wait, as when
 * an interrupt there wakes a task that reads the clock; uclock_start()'s
 * test (uclock_refuse()) on the pretend core has to keep the clock off
 * where the counter stops in waits, and let it on where it runs.
 *
 *   test_uclock [seed]      exit status 0 when every check holds
 *
 * Planted faults (tests/host/run.sh 27): UCLK_FAULT_NOGEN (no second look
 * at gen), UCLK_FAULT_ORDER (the cycles before the true read),
 * UCLK_FAULT_NOIDLE (no idle hook), UCLK_FAULT_NOTICK (no tick hook),
 * UCLK_FAULT_NORATE (no check of the counter's rate), UCLK_FAULT_SPAN
 * with UCLK_SPAN 2^28 (the reciprocal not exact over the span),
 * UCLK_FAULT_IDLEOK (the start's test does not look at the idle part),
 * UCLK_FAULT_NOFLOOR (no check of a stale pair's floor against the true
 * read, so that a counter running fast takes the clock back).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ==== the pretend core ==== */

#define MHZ 320u
static uint64_t real;           /* the true time, in 1/MHZ us */
static uint32_t sys_off;        /* the systimer's reading at real 0 */
static uint32_t cnt;            /* the cycle counter */
static uint64_t acc;            /* its fraction of a cycle, in 1/den */
static uint32_t num = 1, den = 1;       /* its rate against real */
static int running = 1;
static uint64_t rng = 88172645463325252ull;

static uint32_t rnd(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 7;
	rng ^= rng << 17;
	return (uint32_t) (rng >> 16);
}
static uint32_t rnd_in(uint32_t lo, uint32_t hi)
{
	return lo + rnd() % (hi - lo + 1);
}

static void advance_raw(uint64_t d)
{
	real += d;
	if (running) {
		acc += d * num;
		cnt += (uint32_t) (acc / den);
		acc %= den;
	}
}

/* core 1's tick, every 10 ms, put off while interrupts are masked */
#define TICK (10000ull * MHZ)
static uint64_t next_tick = TICK;
static int locked, tick_owed;
static unsigned long ticks;
static void tick_isr(void);

static void advance(uint64_t d)
{
	while (real + d >= next_tick) {
		uint64_t step = next_tick - real;
		advance_raw(step);
		d -= step;
		next_tick += TICK;
		if (locked)
			tick_owed = 1;
		else
			tick_isr();
	}
	advance_raw(d);
}

static uint32_t true_at(uint64_t r)
{
	return sys_off + (uint32_t) (r / MHZ);
}

#define DEPTH 4
static int depth;               /* 0: the main loop's read, 1: one in an interrupt */
static uint64_t sample[DEPTH];  /* when the read at each depth took its last sample */

static uint32_t sim_cycles(void)
{
	uint32_t v = cnt;
	sample[depth] = real;
	advance(rnd_in(1, 3));
	return v;
}

static uint32_t sim_true_us(void)
{
	advance(rnd_in(20, 200));
	uint32_t v = true_at(real);
	sample[depth] = real;
	advance(rnd_in(50, 400));
	return v;
}

static uint32_t sim_lock(void)
{
	locked++;
	return 0;
}
static void sim_unlock(uint32_t s)
{
	(void) s;
	if (--locked == 0 && tick_owed) {
		tick_owed = 0;
		tick_isr();
	}
}

static void sim_hook(int n);

#define UCLK_CYCLES() sim_cycles()
#define UCLK_TRUE_US() sim_true_us()
#define UCLK_LOCK() sim_lock()
#define UCLK_UNLOCK(s) sim_unlock(s)
#define UCLK_HOOK(n) sim_hook(n)
#define UCLK_DEFINE_SYNC
#ifdef UCLK_FAULT_SPAN
#define UCLK_SPAN (1u << 28)
#endif
#include "uclock.h"

/* ==== the checks ==== */

static struct uclock k;

static void tick_isr(void)
{
	if (depth + 1 >= DEPTH)
		return;
	depth++;
	ticks++;
	advance_raw(rnd_in(30, 300));
	uclock_tick(&k);
	advance_raw(rnd_in(30, 300));
	depth--;
}
static uint32_t done_max;       /* the highest value of the reads finished */
static int any_done;
static unsigned long reads, fails, nested, forced;
static int keeps_time = 1;      /* the counter runs at MHZ: the strict checks */
#define LAG 4u
static const char *scen = "";

static void fail(const char *what, uint32_t v, uint32_t ref)
{
	if (fails++ < 12)
		printf("  FAIL (%s, read %lu): %s: value %u, against %u (%+d)\n", scen, reads, what,
		       v, ref, (int32_t) (v - ref));
}

static uint32_t checked_get(void)
{
	int d = depth;
	uint32_t lo = done_max;
	int had = any_done;
	uint64_t start = real;
	sample[d] = real;
	uint32_t v = uclock_get(&k);
	uint32_t at = true_at(sample[d]);
	reads++;
	if (had && (int32_t) (v - lo) < 0)
		fail("below a value read before", v, lo);
	if (keeps_time || k.recip == 0) {
		if ((int32_t) (v - at) > 0 && !(k.recip == 0 && (int32_t) (v - at) <= (int32_t) UCLK_SLACK))
			fail("ahead of the true clock", v, at);
		uint32_t st = true_at(start);
		if (keeps_time && (int32_t) (st - v) > (int32_t) LAG)
			fail("behind the true clock", v, st);
	}
	if (!any_done || (int32_t) (v - done_max) > 0)
		done_max = v;
	any_done = 1;
	return v;
}

static int nest_pct = 2;        /* in a hundred: an interrupt at a hook */

static void sim_hook(int n)
{
	if (locked || depth + 1 >= DEPTH || (int) (rnd() % 100) >= nest_pct)
		return;
	depth++;
	advance(rnd_in(50, 500));
	if (rnd() % 3 == 0) {
		/* as if this interrupt's read found the span over: the pair set */
		forced++;
		sample[depth] = real;
		uint32_t v = uclock_sync(&k);
		uint32_t at = true_at(sample[depth]);
		if (any_done && (int32_t) (v - done_max) < 0)
			fail("a sync below a value read before", v, done_max);
		if (keeps_time && (int32_t) (v - at) > 0)
			fail("a sync ahead of the true clock", v, at);
		if (!any_done || (int32_t) (v - done_max) > 0)
			done_max = v;
		any_done = 1;
	} else {
		nested++;
		checked_get();
	}
	advance(rnd_in(50, 500));
	depth--;
	(void) n;
}

/* a wait for an interrupt: the idle hook once or more, then the wait;
   with preempt, 1 time in 8 a read between the last hook and the wait (an
   interrupt there woke a task that read the clock, and it blocked again) */
static int stops_in_wait, preempt;
static void wait_for(uint64_t d)
{
	int n = rnd_in(1, 3);
	for (int i = 0; i < n; i++) {
		uclock_idle(&k);
		advance(rnd_in(10, 100));
	}
	if (preempt && rnd() % 8 == 0) {
		checked_get();
		advance(rnd_in(10, 100));
	}
	if (stops_in_wait)
		running = 0;
	advance(d);
	running = 1;
}

static uint64_t gap(void)
{
	uint32_t r = rnd() % 1000;
	if (r < 700)
		return rnd_in(50, 50000);               /* a step's worth or less */
	if (r < 950)
		return rnd_in(50000, 3 * UCLK_SPAN);    /* about a span */
	if (r < 998)
		return (uint64_t) rnd_in(1, 400) * 1000000ull;  /* up to 1.25 s */
	return (1ull << 32) + rnd_in(0, 1u << 30);        /* past the counter's wrap */
}

static uint32_t off_at;         /* the sync at which the clock went off */

/* uclock_start()'s test on the pretend core: 20 ms busy, then 20 ms in a
   wait, the time gone by less the cycles' time for each (the counter's
   rate as set); and its verdict.  When it refuses, the clock is left off
   (recip 0), as uclock_start() leaves it. */
static const char *start_test(int32_t *busy, int32_t *idle)
{
	for (int i = 0; i < 2; i++) {
		uint32_t s = sim_lock();
		uint32_t t0 = sim_true_us(), c0 = sim_cycles();
		sim_unlock(s);
		if (i && stops_in_wait)
			running = 0;
		advance(20000ull * MHZ);
		running = 1;
		s = sim_lock();
		uint32_t t1 = sim_true_us(), c1 = sim_cycles();
		sim_unlock(s);
		*(i ? idle : busy) = (int32_t) ((t1 - t0) - (c1 - c0) / MHZ);
	}
	const char *why = uclock_refuse(*busy, *idle);
	if (why) {
		uint32_t f = k.floor;
		memset(&k, 0, sizeof(k));
		k.gen = 1;
		k.floor = f;
	}
	return why;
}

static void start(const char *name, uint32_t n_, uint32_t d_, int stops)
{
	scen = name;
	real = 0;
	next_tick = TICK;
	tick_owed = 0;
	sys_off = 0xffffffffu - 5000000u;       /* the systimer wraps 5 s in */
	cnt = 0xffffffffu - 100000000u;         /* the counter 0.3 s in */
	acc = 0;
	num = n_;
	den = d_;
	running = 1;
	stops_in_wait = stops;
	keeps_time = n_ == d_;
	memset(&k, 0, sizeof(k));
	if (!uclock_setup(&k, MHZ)) {
		fails++;
		printf("  FAIL (%s): uclock_setup() refused %u MHz\n", name, MHZ);
	}
	k.floor = true_at(real);
	any_done = 0;
	off_at = 0;
}

static void run(unsigned long n, int waits)
{
	for (unsigned long i = 0; i < n; i++) {
		checked_get();
		if (k.off && !off_at)
			off_at = k.syncs;
		if (waits && rnd() % 50 == 0) {
			uint32_t r = rnd() % 100;
			wait_for(r < 90 ? rnd_in(320, 320 * 20000) :
				 r < 99 ? (uint64_t) rnd_in(1, 2000) * 320000ull :
				 (1ull << 32) + rnd_in(0, 1u << 31));
		} else {
			advance(gap());
		}
	}
}

int main(int argc, char **argv)
{
	if (argc > 1)
		rng ^= strtoull(argv[1], NULL, 0) * 0x9e3779b97f4a7c15ull;

	/* the reciprocal: the floor of the division for every difference below the span */
	static const uint32_t mhzs[] = { 80, 160, 240, 320, 360, 400, 480 };
	for (unsigned i = 0; i < sizeof(mhzs) / sizeof(mhzs[0]); i++) {
		struct uclock t;
		memset(&t, 0, sizeof(t));
		if (!uclock_setup(&t, mhzs[i])) {
			printf("  FAIL: uclock_setup() refused %u MHz\n", mhzs[i]);
			fails++;
			continue;
		}
		uint32_t bad = 0, first = 0;
		for (uint32_t dc = 0; dc < UCLK_SPAN; dc++)
			if (uclock_scale(&t, dc) != dc / mhzs[i] && !bad++)
				first = dc;
		if (bad) {
			printf("  FAIL: %u MHz: %u differences below the span scaled wrong, the first %u\n",
			       mhzs[i], bad, first);
			fails++;
		}
	}
	printf("  ok: the reciprocal, every difference below %u cycles at 7 frequencies\n", UCLK_SPAN);

	unsigned long f0 = fails, s0;
	int32_t busy, idle;
	const char *why;
	/* v5.3 (the review of batch 4): with reads between the idle hook and
	   the wait.  Where the counter runs in waits the start lets the clock
	   on and every check holds; where it stops, the start keeps it off. */
	start("keeps time, runs in waits", 1, 1, 0);
	why = start_test(&busy, &idle);
	printf("  %s: the start's test busy %+d us, idle %+d us: %s\n", scen, busy, idle,
	       why ? why : "the clock on");
	if (why) {
		printf("  FAIL: the start's test refused a counter that runs in waits\n");
		fails++;
	}
	preempt = 1;
	run(3000000, 1);
	printf("  %s: %lu reads (%lu in interrupts, %lu forced syncs), %u syncs, %u waits; %lu failed\n",
	       scen, reads, nested, forced, k.syncs, k.waits, fails - f0);
	if (k.recip == 0) {
		printf("  FAIL: the clock went off (%d, %d us)\n", k.off, k.err);
		fails++;
	}
	s0 = fails;
	start("keeps time, stops in waits", 1, 1, 1);
	reads = nested = forced = 0;
	why = start_test(&busy, &idle);
	printf("  %s: the start's test busy %+d us, idle %+d us: %s\n", scen, busy, idle,
	       why ? why : "the clock on");
	if (!why) {
		printf("  FAIL: the start's test let on a counter that stops in waits\n");
		fails++;
	}
	run(3000000, 1);
	printf("  %s: %lu reads (%lu in interrupts, %lu forced syncs), %u syncs, %u waits; %lu failed\n",
	       scen, reads, nested, forced, k.syncs, k.waits, fails - s0);
	/* the idle hook itself, where it matters (a counter that stops), with
	   nothing read between it and the wait: the clock on, as if the start
	   had let it on */
	s0 = fails;
	start("stops in waits, nothing between the hook and the wait", 1, 1, 1);
	reads = nested = forced = 0;
	preempt = 0;
	run(3000000, 1);
	printf("  %s: %lu reads (%lu in interrupts, %lu forced syncs), %u syncs, %u waits; %lu failed\n",
	       scen, reads, nested, forced, k.syncs, k.waits, fails - s0);
	if (k.recip == 0) {
		printf("  FAIL: the clock went off (%d, %d us)\n", k.off, k.err);
		fails++;
	}
	preempt = 1;

	/* over two hours of steps of 100 us or so: no drift */
	s0 = fails;
	start("two hours of steps", 1, 1, 0);
	reads = 0;
	nest_pct = 1;
	while (real < 7300ull * 1000000 * MHZ) {
		checked_get();
		advance(rnd_in(20000, 44000));
	}
	uint32_t v = checked_get(), tr = true_at(real);
	printf("  %s: %lu reads, %u syncs; at the end %u us behind the clock, %llu s gone by; %lu failed\n",
	       scen, reads, k.syncs, tr - v, (unsigned long long) (real / MHZ / 1000000), fails - s0);
	if (k.recip == 0) {
		printf("  FAIL: the clock went off (%d, %d us)\n", k.off, k.err);
		fails++;
	}
	nest_pct = 2;

	/* counters that do not keep time: off, and never back in time */
	static const struct { const char *name; uint32_t n, d; int off; } bad[] = {
		{ "the counter 0.1% fast", 1001, 1000, 1 },
		{ "the counter 0.1% slow", 999, 1000, 2 },
		{ "the counter twice as fast", 2, 1, 1 },
		{ "the counter at half", 1, 2, 2 },
	};
	for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		s0 = fails;
		start(bad[i].name, bad[i].n, bad[i].d, 0);
		reads = 0;
		run(200000, 0);
		printf("  %s: off %d at sync %u (%+d us in a span); %lu failed\n",
		       scen, k.off, off_at, k.err, fails - s0);
		if (k.recip != 0 || k.off != bad[i].off) {
			printf("  FAIL: %s: the clock is %s (off %d, not %d)\n", scen,
			       k.recip ? "still on the counter" : "off", k.off, bad[i].off);
			fails++;
		}
	}
	/* a counter twice as fast whose span the tick ends: reads for 6 ms (the
	   pair up to 6 ms ahead), none until just after the tick at 10 ms, which
	   marks the pair stale, then a read (a sync with no rate check); no
	   interrupt reads, so that nothing else ends the span */
	s0 = fails;
	start("twice as fast, the span ended by the tick", 2, 1, 0);
	reads = 0;
	nest_pct = 0;
	checked_get();
	for (int i = 0; i < 6; i++) {
		advance(1000ull * MHZ);
		checked_get();
	}
	if (real < TICK + 100ull * MHZ)
		advance(TICK + 100ull * MHZ - real);
	nest_pct = 2;
	for (int i = 0; i < 20; i++) {
		checked_get();
		if (k.off && !off_at)
			off_at = k.syncs;
		advance(rnd_in(50, 50000));
	}
	printf("  %s: off %d at sync %u (%+d us); %lu failed\n", scen, k.off, off_at, k.err, fails - s0);
	if (k.recip != 0 || k.off != 1) {
		printf("  FAIL: %s: not off as it should be\n", scen);
		fails++;
	}
	/* the frequency changed part way */
	s0 = fails;
	start("keeps time, then twice as fast", 1, 1, 0);
	reads = 0;
	run(100000, 1);
	keeps_time = 0;
	num = 2;
	run(100000, 1);
	printf("  %s: off %d at sync %u (%+d us in a span); %lu failed\n", scen, k.off, off_at,
	       k.err, fails - s0);
	if (k.recip != 0 || k.off != 1) {
		printf("  FAIL: %s: not off as it should be\n", scen);
		fails++;
	}

	printf("uclock: %s, %lu failed\n", fails ? "FAILED" : "all checks hold", fails);
	return fails != 0;
}
