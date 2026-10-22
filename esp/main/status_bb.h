/*
 * The panic handler's WS2812 frame, a bit at a time against the CPU's cycle
 * counter: no RMT and no interrupts (the panic handler has them off).
 * status_led.c includes it with STATUS_BB_NOW() reading the cycle counter
 * and STATUS_BB_HIGH() / STATUS_BB_LOW() storing to GPIO37's set and clear
 * registers; tests/host/test_status.c includes it with a pretend clock that
 * charges each of those a number of cycles, and the instructions between a
 * wait's end and its store, and checks every edge.
 *
 * Each edge is placed against a running schedule (edge += the time before
 * it), not against the moment the last store happened, so the time a store
 * or a pass round a loop takes does not add up along the frame.
 *
 * Both edges of a bit come straight after their wait: the bit's high and
 * low times are picked while the line is still low, before the wait that
 * ends in the rising edge, so that nothing but the wait's own compare and
 * branch comes between a wait ending and its store, for the rising edge as
 * for the falling one.  A high time is then what was asked, give or take
 * one pass round the waiting loop.  (In v3.3 the bit was picked after that
 * wait, which put nine or ten instructions before each rising store and two
 * before each falling one: every high time came out 7 or 8 cycles short,
 * about 90 to 100 ns at 80 MHz.)  build.sh checks this layout in the built
 * code.
 *
 * The frame starts with the line low for t->reset (the LED then takes what
 * follows as a new frame, whatever the line or the RMT did before) and ends
 * low; the LED shows the colour once the line has been low for its reset
 * time after the last bit.
 */
#ifndef STATUS_BB_ATTR
#define STATUS_BB_ATTR
#endif

static inline STATUS_BB_ATTR void status_bb_frame(uint32_t grb, const struct status_bb *t)
{
	/* edge: where the schedule has the last rise (at first, the frame's
	   start); to_rise: from there to the next rise, the reset at first,
	   then each bit's period */
	uint32_t edge = STATUS_BB_NOW();
	uint32_t to_rise = t->reset;

	STATUS_BB_LOW();
	for (int i = 23; i >= 0; i--) {
		uint32_t high = t->t0h, period = t->t0;

		if ((grb >> i) & 1) {
			high = t->t1h;
			period = t->t1;
		}
		while ((uint32_t) (STATUS_BB_NOW() - edge) < to_rise)
			;
		STATUS_BB_HIGH();
		edge += to_rise;
		while ((uint32_t) (STATUS_BB_NOW() - edge) < high)
			;
		STATUS_BB_LOW();
		to_rise = period;
	}
	/* the last bit's low time, before anything else is done */
	while ((uint32_t) (STATUS_BB_NOW() - edge) < to_rise)
		;
}
