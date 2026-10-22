/*
 * The panic handler's WS2812 frame, a bit at a time against the CPU's cycle
 * counter: no RMT and no interrupts (the panic handler has them off).
 * status_led.c includes it with STATUS_BB_NOW() reading the cycle counter
 * and STATUS_BB_HIGH() / STATUS_BB_LOW() storing to GPIO37's set and clear
 * registers; tests/host/test_status.c includes it with a pretend clock that
 * charges each of those a number of cycles, and checks every edge.
 *
 * Each edge is placed against a running schedule (edge += the bit's period),
 * not against the moment the last store happened, so the time a store or a
 * pass round a loop takes does not add up along the frame: a high time is
 * its own length give or take one pass round the waiting loop, and the bit
 * periods are exact on average.
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
	uint32_t start = STATUS_BB_NOW();

	STATUS_BB_LOW();
	while ((uint32_t) (STATUS_BB_NOW() - start) < t->reset)
		;
	uint32_t edge = STATUS_BB_NOW();
	for (int i = 23; i >= 0; i--) {
		uint32_t high = t->t0h, period = t->t0;

		if ((grb >> i) & 1) {
			high = t->t1h;
			period = t->t1;
		}
		STATUS_BB_HIGH();
		while ((uint32_t) (STATUS_BB_NOW() - edge) < high)
			;
		STATUS_BB_LOW();
		while ((uint32_t) (STATUS_BB_NOW() - edge) < period)
			;
		edge += period;
	}
}
