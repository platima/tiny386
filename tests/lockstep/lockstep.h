/*
 * tests/lockstep: what the hook (lockstep.c) reads from the core's own
 * files.  build.sh appends lockstep_cpu.inc to a copy of i386.c,
 * lockstep_pic.inc to i8259.c, lockstep_pit.inc to i8254.c and
 * lockstep_fpu.inc to fpu.c, so that the hook can read their private
 * structures without any change to the tree's files.  See README.md.
 *
 * Offered under the licence of tiny386 (BSD 3-clause).
 */
#ifndef LOCKSTEP_H
#define LOCKSTEP_H
#include <stdint.h>

/* the CPU at a step boundary, as tiny386's struct CPUI386 holds it */
struct ls_cpu {
	uint32_t gpr[8];
	uint32_t eip;           /* next_ip: the next instruction */
	uint32_t ip;            /* the last instruction's start (dead state) */
	uint32_t flags;         /* EFLAGS with the lazy flags worked out */
	uint32_t cpl, code16, sp_mask, halt, intr;
	struct { uint32_t sel, base, limit, flags; } seg[8];
	uint32_t gdt_base, gdt_limit, idt_base, idt_limit;
	uint32_t cr0, cr2, cr3, dr[8];
	uint32_t sysenter[3];
	uint32_t excno, excerr; /* the last exception (dead state) */
	uint32_t cc_op, cc_mask;/* the lazy flags' own form (dead state) */
	uint64_t cycle;
	uint64_t fpu;           /* a hash of the x87's state, 0 without one */
	int has_fpu;
};

/* lockstep_cpu.inc (i386.c) */
struct CPUI386;
void lockstep_cpu_read(struct CPUI386 *cpu, struct ls_cpu *s);
void *lockstep_cpu_cb(struct CPUI386 *cpu);     /* &cpu->cb, a CPU_CB */
int lockstep_cpu_code(struct CPUI386 *cpu, uint8_t *buf, int n);

/* lockstep_pic.inc (i8259.c): the two PICs' registers, 15 bytes each */
#define LS_PIC_BYTES 30
void lockstep_pic_read(void *pic, uint8_t *out);

/* lockstep_pit.inc (i8254.c): the three channels, 17 words each */
#define LS_PIT_WORDS 51
void lockstep_pit_read(void *pit, uint32_t *out);

/* lockstep_fpu.inc (fpu.c) */
uint64_t lockstep_fpu_hash(void *fpu);

/* the hash the hook uses everywhere: 64 bits, four lanes; each word goes
   through a bijection of the lane, so one word that differs always gives
   another hash */
static inline uint64_t ls_mix(uint64_t h)
{
	h ^= h >> 31;
	h *= 0x7fb5d329728ea185ull;
	h ^= h >> 27;
	h *= 0x81dadef4bc2dd44dull;
	h ^= h >> 33;
	return h;
}
#endif
