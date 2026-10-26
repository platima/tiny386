/*
 * tests/lockstep: appended to a copy of i8254.c by build.sh: the timer's
 * three channels for the hook (lockstep.c), read only (no call that looks
 * at the clock).
 *
 * Offered under the licence of tiny386 (BSD 3-clause).
 */
#include "lockstep.h"

void lockstep_pit_read(void *pit, uint32_t *out)
{
	PITState *p = pit;
	for (int i = 0; i < 3; i++) {
		PITChannelState *s = &p->channels[i];
		uint32_t *o = out + 17 * i;
		o[0] = s->count;
		o[1] = s->latched_count;
		o[2] = s->count_latched;
		o[3] = s->status_latched;
		o[4] = s->status;
		o[5] = s->read_state;
		o[6] = s->write_state;
		o[7] = s->write_latch;
		o[8] = s->rw_mode;
		o[9] = s->mode;
		o[10] = s->bcd;
		o[11] = s->gate;
		o[12] = s->reload_pending;
		o[13] = s->stopped_count;
		o[14] = s->count_load_time;
		o[15] = s->last_irq_count;
		o[16] = (uint32_t) s->irq;
	}
}
