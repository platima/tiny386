/*
 * tests/lockstep: appended to a copy of i8259.c by build.sh: the two PICs'
 * registers for the hook (lockstep.c), read only.
 *
 * Offered under the licence of tiny386 (BSD 3-clause).
 */
#include "lockstep.h"

void lockstep_pic_read(void *pic, uint8_t *out)
{
	PicState2 *s2 = pic;
	for (int i = 0; i < 2; i++) {
		PicState *s = &s2->pics[i];
		uint8_t *o = out + 15 * i;
		o[0] = s->last_irr;
		o[1] = s->irr;
		o[2] = s->imr;
		o[3] = s->isr;
		o[4] = s->priority_add;
		o[5] = s->irq_base;
		o[6] = s->read_reg_select;
		o[7] = s->poll;
		o[8] = s->special_mask;
		o[9] = s->init_state;
		o[10] = s->auto_eoi;
		o[11] = s->rotate_on_auto_eoi;
		o[12] = s->special_fully_nested_mode;
		o[13] = s->init4;
		o[14] = s->single_mode;
	}
}
