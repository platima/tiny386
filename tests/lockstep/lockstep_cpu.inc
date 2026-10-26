/*
 * tests/lockstep: appended to a copy of i386.c by build.sh (never to the
 * tree's own file), so that it sees struct CPUI386 and tiny386's own
 * helpers.  It only reads (refresh_flags()'s one write is undone at once).
 *
 * Offered under the licence of tiny386 (BSD 3-clause).
 */
#include "lockstep.h"

void lockstep_cpu_read(struct CPUI386 *cpu, struct ls_cpu *s)
{
	/* the lazy flags worked out as cpui386_get_state() would; refresh_flags()
	   writes only cpu->flags, which is put back at once, so the CPU's own
	   state (cc and flags) stays exactly as it was */
	uword saved_flags = cpu->flags;
	refresh_flags(cpu);
	uword flags = cpu->flags;
	cpu->flags = saved_flags;
	for (int i = 0; i < 8; i++)
		s->gpr[i] = cpu->gprx[i].r32;
	s->eip = cpu->next_ip;
	s->ip = cpu->ip;
	s->flags = flags;
	s->cpl = cpu->cpl;
	s->code16 = cpu->code16;
	s->sp_mask = cpu->sp_mask;
	s->halt = cpu->halt;
	s->intr = cpu->intr;
	for (int i = 0; i < 8; i++) {
		s->seg[i].sel = cpu->seg[i].sel;
		s->seg[i].base = cpu->seg[i].base;
		s->seg[i].limit = cpu->seg[i].limit;
		s->seg[i].flags = cpu->seg[i].flags;
	}
	s->gdt_base = cpu->gdt.base;
	s->gdt_limit = cpu->gdt.limit;
	s->idt_base = cpu->idt.base;
	s->idt_limit = cpu->idt.limit;
	s->cr0 = cpu->cr0;
	s->cr2 = cpu->cr2;
	s->cr3 = cpu->cr3;
	for (int i = 0; i < 8; i++)
		s->dr[i] = cpu->dr[i];
	s->sysenter[0] = cpu->sysenter.cs;
	s->sysenter[1] = cpu->sysenter.eip;
	s->sysenter[2] = cpu->sysenter.esp;
	s->excno = cpu->excno;
	s->excerr = cpu->excerr;
	s->cc_op = cpu->cc.op;
	s->cc_mask = cpu->cc.mask;
	s->cycle = (uint64_t) (unsigned long) cpu->cycle;
	s->has_fpu = 0;
	s->fpu = 0;
#ifdef I386_ENABLE_FPU
	if (cpu->fpu) {
		s->has_fpu = 1;
		s->fpu = lockstep_fpu_hash(cpu->fpu);
	}
#endif
}

void *lockstep_cpu_cb(struct CPUI386 *cpu)
{
	return &cpu->cb;
}

/* the bytes at CS:EIP, through the CPU's own translation; this can set
   accessed bits and fill the TLB, so the hook calls it only in a process
   it throws away (the children of a split step) */
int lockstep_cpu_code(struct CPUI386 *cpu, uint8_t *buf, int n)
{
	uword mask = cpu->code16 ? 0xffff : 0xffffffff;
	int i;
	for (i = 0; i < n; i++) {
		u8 b;
		if (!cpu_load8(cpu, SEG_CS, (cpu->next_ip + i) & mask, &b))
			break;
		buf[i] = b;
	}
	return i;
}
