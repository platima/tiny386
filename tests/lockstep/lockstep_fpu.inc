/*
 * tests/lockstep: appended to a copy of fpu.c by build.sh: a hash of the
 * x87's state for the hook (lockstep.c), field by field (never the
 * structure's padding), read only.
 *
 * Offered under the licence of tiny386 (BSD 3-clause).
 */
#include <string.h>
#include "lockstep.h"

uint64_t lockstep_fpu_hash(void *p)
{
	FPU *fpu = p;
	uint64_t h = 0x243f6a8885a308d3ull;
	h = ls_mix(h ^ fpu->cw);
	h = ls_mix(h ^ fpu->sw);
	h = ls_mix(h ^ fpu->top);
	for (int i = 0; i < 8; i++) {
		uint64_t w;
		memcpy(&w, &fpu->st[i], 8);
		h = ls_mix(h ^ w);
		h = ls_mix(h ^ fpu->rawst[i].mant0);
		h = ls_mix(h ^ fpu->rawst[i].mant1);
		h = ls_mix(h ^ fpu->rawst[i].high);
	}
	h = ls_mix(h ^ fpu->rawtagr);
	h = ls_mix(h ^ fpu->rawtagw);
	return h;
}
