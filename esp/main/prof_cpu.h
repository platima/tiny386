/*
 * The guest's state for one profiler sample (v5.1, s31/PROFILE.md), read
 * from another core (or a signal handler) while the CPU runs: CS, EIP, the
 * privilege level, the mode, what the CPU is doing (the fast interpreter,
 * an instruction handed to tiny386, tiny386 alone, halted), the code page,
 * and for an instruction tiny386 is doing its first bytes.  Shared by the
 * board's sampler (profile.c) and the PC's stand-in.
 *
 * Every field is one aligned word or byte that core 1 writes as it runs:
 * each is read whole, but two of them may be from either side of a change
 * (a far jump between reading CS and EIP, say).  A sample is one moment
 * among thousands; such a sample counts against the wrong page, now and
 * then.  The instruction's bytes are read only from guest RAM, through the
 * fetch cache's own mapping, and only when the instruction is in the
 * page that mapping is for.
 */
#ifndef PROF_CPU_H
#define PROF_CPU_H

#include "../../i386.h"
#include "prof_hist.h"

#ifndef PROF_CPU_ATTR
#define PROF_CPU_ATTR
#endif

static inline void PROF_CPU_ATTR prof_read_guest(const struct cpui386_prof_fields *f,
						 struct prof_guest *g)
{
	uint32_t cr0 = *f->cr0, fl = *f->flags;
	bool halt = *f->halt;
	bool fast = f->fast_on && *f->fast_on;
	bool one = f->exec1_one && *f->exec1_one;
	g->cs_sel = *f->cs_sel & 0xffff;
	g->cs_base = *f->cs_base;
	g->cpl = (uint8_t) (*f->cpl & 3);
	if (!(cr0 & 1))
		g->mode = PROF_MODE_REAL;
	else if (fl & 0x20000)
		g->mode = PROF_MODE_V86;
	else
		g->mode = *f->code16 ? PROF_MODE_PM16 : PROF_MODE_PM32;
	g->area = halt ? PROF_AREA_HALT : !fast ? PROF_AREA_ALONE :
		one ? PROF_AREA_EXEC1 : PROF_AREA_FAST;
	/* cpu_exec1() sets ip to each instruction as it starts it; the fast
	   interpreter sets next_ip when its step ends or it hands over */
	bool tiny = g->area == PROF_AREA_EXEC1 || g->area == PROF_AREA_ALONE;
	g->eip = tiny ? *f->ip : *f->next_ip;
	unsigned long laddr = *f->code_laddr;
	uint32_t xaddr = *f->code_xaddr;
	g->code_page = laddr == (unsigned long) -1 ? PROF_NO_PAGE : (uint32_t) (laddr >> 12);
	g->nop = 0;
	if (tiny && laddr != (unsigned long) -1) {
		uint32_t lin = g->cs_base + g->eip;
		if (((unsigned long) lin ^ laddr) < 4096) {
			uint32_t phys = xaddr ^ lin;
			if (f->phys_mem_size > 16 && phys < (uint32_t) (f->phys_mem_size - 16)) {
				/* up to 8 bytes, not past the page's end */
				int n = 4096 - (int) (lin & 4095);
				if (n > 8)
					n = 8;
				const volatile uint8_t *b = f->phys_mem + phys;
				for (int i = 0; i < n; i++)
					g->op[i] = b[i];
				g->nop = (uint8_t) n;
			}
		}
	}
}

#endif /* PROF_CPU_H */
