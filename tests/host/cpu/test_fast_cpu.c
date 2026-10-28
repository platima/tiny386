/*
 * v5.0: the fast interpreter (i386_fast.h) against tiny386's own, in
 * lockstep.  Two machines, one running cpui386_step() with the fast
 * interpreter and one with tiny386's cpu_exec1() alone, are given the same
 * guest code and the same interrupts; after every step everything is
 * compared: the registers, the flags as tiny386 keeps them (its lazy
 * flags), the instruction count, CR2, tiny386's instruction fetch cache
 * and its whole TLB, all guest memory (the page tables' accessed and dirty
 * bits with it), and what each machine did at its ports and in video
 * memory (with the instruction count at each access, which the devices'
 * clocks read).
 *
 * The guest code is made here at random from valid instructions, for real
 * mode (16-bit, with 66h and 67h prefixes), for 32-bit protected mode with
 * paging at privilege levels 0, 1 and 3, virtual-8086 mode (IOPL 3, and
 * IOPL 0 to its first sensitive instruction), 16-bit protected mode with
 * and without paging, 16-bit code on a 32-bit stack and the reverse
 * (v5.0, after the reviews, from the CPU review's own extended copy of this
 * test); and four short hand-written cases: levels 1 and 2 reading a
 * supervisor page that level 0 has used (the fast interpreter's TLB once
 * took them as level 3), self-modifying real-mode code, and CR0.WP set
 * after a supervisor store into a read-only page.  The random code has
 * arithmetic and
 * logic on registers and on memory (operands that cross a page), shifts,
 * multiplies and safe divides, bit tests, string instructions with and
 * without REP across pages and segments' ends, the stack, CMP or TEST then
 * JZ or JNZ (tiny386's fusion of the two) anywhere in a page, jumps and
 * calls near and far, INT and IRET, PUSHF and POPF, STI and CLI, loads of
 * segment registers and of SS, port I/O, video memory (the VGA window and
 * a linear frame buffer), pages that are not present until a page fault's
 * handler maps them, the page tables' accessed and dirty bits cleared
 * without INVLPG, INVLPG and MOV CR3, indirect near and far CALL and JMP
 * and PUSH from memory, and instructions only tiny386 does.  Interrupts
 * are raised between steps and from inside an OUT, at random.
 *
 * The frame buffer at E0000000h: with an even seed it goes through the
 * memory-mapped calls; with an odd seed the CPU is given it
 * (cpu_cb_set_lfb(), as pc.c does), with marks, so that tiny386's own path
 * and the fast interpreter's TLB entries for it are compared too (the bytes,
 * every mark, the flag), with loads, MOV, STOS, MOVS and REP into it and out
 * of it, arithmetic from it and into it, and ARPL; an OUT to port E4h moves
 * it or takes it away, as a write to the VGA's BAR does, and the marks are
 * cleared between steps at random, as the drawing does.
 *
 * v5.1 (the open findings RC-1 and RC-2 of v5.0's recheck, from its own
 * extended copy of this test): the frame buffer from 16-bit protected mode
 * without paging and with it (a selector at A000h and FS based at it), at
 * level 3 with IOPL 3 (OUT allowed there, so moves made at level 3, and
 * INT 30h's handler at level 0 using the same pages), and from real mode
 * with FS's base at it (unreal mode); OUTSB to port E4h, so that tiny386
 * moves it too, not only the fast OUT; its page tables' accessed and dirty
 * bits cleared without INVLPG, and two present pages after it; ARPL with a
 * segment prefix in 16-bit protected mode; and two more hand-written cases:
 * level 1 storing into a supervisor page of the frame buffer that level 0
 * has just stored into, and each of its 16 pages stored into through each
 * kind of entry with its dirty bit cleared in between.  After every step
 * the fast interpreter's TLB is checked against what it must be (its
 * invariants, check_fast_tlb()), and a machine that crashes (a store
 * through a stale host address) is reported as a failure of the scenario
 * it was in.
 *
 *   test_fast_cpu [SEEDS [FIRST_SEED [MODES]]]
 *
 * MODES: the scenarios for each seed, one character each (run() names
 * them; the default is all twenty, 0123456789abcdefghij).
 *
 * Built with i386.c included (I386_FAST), so that tiny386's internals can
 * be compared.  Exit status 0 when the two agree at every step.
 */
#define I386_FAST
#include "i386.c"

#include <stdarg.h>
#include <signal.h>
#include <unistd.h>

/* the scenario and step under way, for on_crash() (v5.1) */
static const char *volatile crash_name = "(none)";
static volatile int crash_seed;
static volatile long crash_step;

/* == the machines */
#define MEM (2 * 1024 * 1024)
#define LFB_BASE 0xe0000000u
#define LFB_SIZE 0x10000
#define LFB_MARK_SHIFT 9        /* a mark for each 512 bytes, as VGA_DIRTY_SHIFT */

struct mach {
	CPUI386 *cpu;
	u8 *mem;
	u8 vga[0x20000];        /* behind the window at A0000h */
	u8 lfb[LFB_SIZE];       /* behind E0000000h */
	uint64_t log;           /* a hash of every port and video memory access */
	unsigned nlog;
	int vec;                /* the interrupt the PIC gives */
	int panic;              /* a port F0h write: an exception the guest did not expect */
	u8 marks[LFB_SIZE >> LFB_MARK_SHIFT];   /* the frame buffer's marks, when the CPU has it */
	u8 wrote;               /* ... and its flag */
};
static struct mach M[2];
static int glfb;                /* this scenario gives the CPU the frame buffer (odd seeds) */
static const int marks_on = 1;

static uint64_t mix(uint64_t h, uint64_t v)
{
	h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
	return h * 0xff51afd7ed558ccdull;
}

static struct mach *of(void *o) { return o; }
static void note(struct mach *m, unsigned kind, unsigned a, unsigned v)
{
	m->log = mix(m->log, ((uint64_t) kind << 56) ^ ((uint64_t) a << 24) ^ v);
	m->log = mix(m->log, (uint64_t) m->cpu->cycle);
	m->nlog++;
}

static u8 io_r8(void *o, int port)
{
	struct mach *m = of(o);
	u8 v = (u8) (m->cpu->cycle * 7 + port);   /* as a timer would: the instruction count */
	note(m, 1, port, v);
	return v;
}
static u16 io_r16(void *o, int port) { return io_r8(o, port) | io_r8(o, port + 1) << 8; }
static u32 io_r32(void *o, int port) { return io_r16(o, port) | (u32) io_r16(o, port + 2) << 16; }
static void io_w8(void *o, int port, u8 v)
{
	struct mach *m = of(o);
	note(m, 2, port, v);
	if (port == 0xe0)
		cpui386_raise_irq(m->cpu);     /* an interrupt from inside an instruction */
	if (port == 0xf0)
		m->panic = 0x100 | v;
	if (port == 0xe4 && glfb) {
		/* the frame buffer moved, as by a write to the VGA's BAR: 0 none
		   (the calls take every address), 2 its upper half from
		   E0008000h, 1 and 3 all of it; the calls' mapping (mmio()) stays */
		CPU_CB *cb = &m->cpu->cb;
		if ((v & 3) == 0)
			cpu_cb_set_lfb(cb, 0, 0, NULL);
		else if ((v & 3) == 2)
			cpu_cb_set_lfb(cb, LFB_BASE + 0x8000, LFB_SIZE - 0x8000, m->lfb + 0x8000);
		else
			cpu_cb_set_lfb(cb, LFB_BASE, LFB_SIZE, m->lfb);
	}
}
static void io_w16(void *o, int port, u16 v) { io_w8(o, port, v); io_w8(o, port + 1, v >> 8); }
static void io_w32(void *o, int port, u32 v) { io_w16(o, port, v); io_w16(o, port + 2, v >> 16); }

static u8 *mmio(struct mach *m, uword a)
{
	if (a >= LFB_BASE && a - LFB_BASE < LFB_SIZE)
		return &m->lfb[a - LFB_BASE];
	if (a >= 0xa0000 && a < 0xc0000)
		return &m->vga[a - 0xa0000];
	return NULL;
}
static u8 mm_r8(void *o, uword a)
{
	struct mach *m = of(o);
	u8 *p = mmio(m, a);
	u8 v = p ? *p : 0xff;
	note(m, 3, a, v);
	return v;
}
static void mm_w8(void *o, uword a, u8 v)
{
	struct mach *m = of(o);
	u8 *p = mmio(m, a);
	if (p)
		*p = v;
	note(m, 4, a, v);
}
static u16 mm_r16(void *o, uword a) { return mm_r8(o, a) | mm_r8(o, a + 1) << 8; }
static void mm_w16(void *o, uword a, u16 v) { mm_w8(o, a, v); mm_w8(o, a + 1, v >> 8); }
static u32 mm_r32(void *o, uword a) { return mm_r16(o, a) | (u32) mm_r16(o, a + 2) << 16; }
static void mm_w32(void *o, uword a, u32 v) { mm_w16(o, a, v); mm_w16(o, a + 2, v >> 16); }
static bool mm_ws(void *o, uword a, uint8_t *buf, int len)
{
	struct mach *m = of(o);
	for (int i = 0; i < len; i++)
		mm_w8(m, a + i, buf[i]);
	return true;
}
static int pic_irq(void *o) { return of(o)->vec; }

/* i386.c's allocations on the board (BUILD_ESP32) */
void *pcmalloc(long size) { return calloc(1, (size_t) size); }

/* == a random number generator, the same sequence for every run */
static uint64_t rs;
static unsigned rnd(unsigned n)
{
	rs ^= rs << 13;
	rs ^= rs >> 7;
	rs ^= rs << 17;
	return n ? (unsigned) ((rs >> 11) % n) : 0;
}
static bool chance(unsigned in) { return rnd(in) == 0; }

/* == the guest code, written into both machines' memory */
static u8 *gm;          /* where it is written: machine 0's memory */
static uword gp;        /* the next byte's physical address */
static uword gbase_lin; /* linear address of physical address gcode_phys */
static uword gcode_phys;
static int g32;         /* 32-bit code */
static int guser;       /* privilege level 3 */
/* the modes added after the v5.0 reviews */
static int gv86;        /* virtual-8086 mode (IOPL 3, paging) */
static int gpm16;       /* 16-bit protected mode (code and stack 16-bit) */
static int gcpl1;       /* 32-bit protected mode at privilege level 1 */
static int gnopriv;     /* no CLI/STI, ports, CR3, INVLPG or video memory */
static int glfbuser;    /* v5.1: level 3 (IOPL 3) with ports and the frame buffer */
static u16 gcs_far;     /* the code selector for far jumps */
static uword glin(void) { return gp - gcode_phys + gbase_lin; }
static void e8(unsigned v) { gm[gp++] = v; }
static void e16(unsigned v) { e8(v); e8(v >> 8); }
static void e32(unsigned v) { e16(v); e16(v >> 16); }
static void ev(unsigned v, int o16) { if (o16) e16(v); else e32(v); }

/* the layout */
#define R_CS 0x1000             /* real mode: code at 10000h */
#define R_DS 0x2000             /* data at 20000h: near the end of its 64 KiB too */
#define R_SS 0x4000
#define R_HANDLER 0x0800        /* 8000h: the interrupt handlers */
#define P_CODE 0x00400000u      /* protected mode: code (linear), from physical 100000h */
#define P_DATA 0x00500000u      /* data: four pages, the second and fourth not present at first */
#define P_DATA_PHYS 0x180000u
#define P_STACK 0x00600000u     /* ring 0's stack top (linear) */
#define P_USTACK 0x00680000u    /* ring 3's */
#define P_PT 0x00700000u        /* the page tables, mapped (linear) */
#define PD_PHYS 0x1f0000u
#define PT_PHYS 0x1f1000u       /* the page table for 0..4 MiB: identity */
#define PT2_PHYS 0x1f2000u      /* for 4 MiB up (code, data, stacks, the tables) */
#define PTL_PHYS 0x1f3000u      /* for E0000000h (the frame buffer) */
#define GDT_PHYS 0x1e0000u
#define IDT_PHYS 0x1e1000u
#define TSS_PHYS 0x1e2000u
#define HND_PHYS 0x1e3000u      /* the handlers (linear = physical) */
#define CNT_PHYS 0x1e4000u      /* their counters */

/* registers the random code may change: AX, CX, DX (and AL, CL, DL, AH);
   BX, SI, DI, BP hold pointers into the data and stay */
static int rreg(void) { return rnd(3); }
static int rreg8(void) { static const int r[] = { 0, 1, 2, 4 }; return r[rnd(4)]; }

static uword data_lin(void) { return g32 ? P_DATA : 0; }
static int a32;         /* real mode: the next memory operand after a 67h prefix */
static void pfx67(void)
{
	if (!g32 && chance(8)) {
		e8(0x67);
		a32 = 1;
	}
}

/* a memory operand for modrm's reg field reg: one of the forms that stay
   in the data (from BX, BP or a fixed address, crossing a page often) */
static void memop(int reg)
{
	int w32 = g32 || a32;
	a32 = 0;
	if (!w32) {
		switch (rnd(5)) {
		case 0: e8(0x40 | reg << 3 | 0); e8(rnd(256)); break;     /* [bx+si+d8] */
		case 1: e8(0x40 | reg << 3 | 7); e8(rnd(256)); break;     /* [bx+d8] */
		case 2: e8(0x40 | reg << 3 | 3); e8(rnd(256)); break;     /* [bp+di+d8], SS */
		case 3: e8(0x80 | reg << 3 | 4); e16(rnd(0x10000)); break; /* [si+d16]: anywhere in DS, its end too */
		default: e8(0x00 | reg << 3 | 6); e16(0xfff0 + rnd(16)); break; /* [d16] at the segment's end */
		}
	} else {
		switch (rnd(5)) {
		case 0: e8(0x40 | reg << 3 | 3); e8(rnd(256)); break;     /* [ebx+d8] */
		case 1: e8(0x44 | reg << 3); e8(rnd(4) << 6 | 6 << 3 | 3); e8(rnd(256)); break; /* [ebx+esi*s+d8] */
		case 2: e8(0x40 | reg << 3 | 5); e8(rnd(256)); break;     /* [ebp+d8], SS */
		case 3: e8(0x80 | reg << 3 | 7); e32(rnd(0x3f00)); break; /* [edi+d32]: all four pages */
		default: e8(0x00 | reg << 3 | 5); e32(data_lin() + rnd(0x4000 - 4)); break; /* [d32] */
		}
	}
}

/* modrm for an operand that is a register (one the code may change) or memory */
static void rm(int reg, int byte)
{
	if (chance(2)) {
		a32 = 0;
		e8(0xc0 | reg << 3 | (byte ? rreg8() : rreg()));
	}
	else
		memop(reg);
}

static void seg_prefix(void)
{
	/* in real mode DS, ES and SS cover the data; FS is the VGA window
	   (A000h), so that some accesses are MMIO */
	static const u8 p16[] = { 0x26, 0x3e, 0x36, 0x64 };
	static const u8 p32[] = { 0x26, 0x3e, 0x36, 0x2e };
	if (chance(6))
		e8(g32 ? p32[rnd(4)] : p16[rnd(4)]);
}

static void mov_imm(int reg, unsigned v)
{
	e8(0xb8 + reg);
	ev(v, !g32);
}

/* a JZ or JNZ right after (tiny386 fuses it with a CMP or TEST before it):
   over a one-byte instruction, or none */
static void jz_after(void)
{
	if (chance(3))
		return;
	e8(0x74 + rnd(2));
	if (chance(2)) { e8(1); e8(0x90); } else e8(0);
}

static int o16pfx(void)
{
	/* 66h toggles the operand size */
	if (chance(4)) { e8(0x66); return g32; }
	return !g32;
}

/* one random instruction (or a few that go together) */
static void one(void)
{
	int r = rnd(64);
	int o16;
	a32 = 0;
	switch (r) {
	case 0 ... 9: {         /* ALU r/m,r and r,r/m */
		pfx67();
		seg_prefix();
		o16 = o16pfx();
		int op = rnd(8) << 3 | rnd(4);
		e8(op);
		int byte = !(op & 1);
		if (op & 2)
			rm(byte ? rreg8() : rreg(), byte);
		else
			rm(rnd(8), byte);
		if ((op >> 3) == 7)
			jz_after();
		(void) o16;
		break;
	}
	case 10 ... 13: {       /* ALU r/m,imm */
		seg_prefix();
		o16 = o16pfx();
		int k = rnd(3), op = rnd(8);
		e8(k == 0 ? 0x80 : k == 1 ? 0x81 : 0x83);
		rm(op, k == 0);
		if (k == 1) ev(rnd(0) | (unsigned) rs, o16); else e8(rnd(256));
		if (op == 7)
			jz_after();
		break;
	}
	case 14: {              /* AL/eAX, imm and TEST */
		o16 = o16pfx();
		int op = rnd(9);
		if (op == 8) { e8(0xa8 + rnd(2)); } else e8(op << 3 | 4 | rnd(2));
		if ((gm[gp - 1] & 1)) ev((unsigned) rs, o16); else e8(rnd(256));
		jz_after();
		break;
	}
	case 15: {              /* TEST r/m,r and F6/F7 /0 */
		seg_prefix();
		o16 = o16pfx();
		if (chance(2)) { e8(0x84 + rnd(2)); rm(rnd(8), !(gm[gp - 1] & 1)); }
		else { int w = rnd(2); e8(0xf6 + w); rm(rnd(2), !w); if (w) ev((unsigned) rs, o16); else e8(rnd(256)); }
		jz_after();
		break;
	}
	case 16 ... 18: {       /* MOV */
		int k = rnd(6);
		if (k < 5) pfx67();
		seg_prefix();
		o16 = o16pfx();
		if (k < 2) { e8(0x88 + rnd(2)); rm(rnd(8), !(gm[gp - 1] & 1)); }
		else if (k < 4) { int w = rnd(2); e8(0x8a + w); rm(w ? rreg() : rreg8(), !w); }
		else if (k == 4) { int w = rnd(2); e8(0xc6 + w); rm(0, !w); if (w) ev((unsigned) rs, o16); else e8(rnd(256)); }
		else { e8(0xa0 + rnd(4)); if (g32) e32(data_lin() + rnd(0x4000 - 4)); else e16(rnd(0x10000)); }
		break;
	}
	case 19: {              /* MOV r,imm */
		o16 = o16pfx();
		if (chance(2)) { e8(0xb0 + rreg8()); e8(rnd(256)); }
		else { e8(0xb8 + rreg()); ev((unsigned) rs, o16); }
		break;
	}
	case 20 ... 21: {       /* shifts and rotates */
		int k = rnd(6);
		static const u8 opc[] = { 0xc0, 0xc1, 0xd0, 0xd1, 0xd2, 0xd3 };
		if (k >= 4 && chance(2)) { e8(0xb1); e8(rnd(40)); }   /* (MOV CL first) */
		seg_prefix();
		o16 = o16pfx();
		e8(opc[k]);
		rm(rnd(8), !(opc[k] & 1));
		if (k < 2) e8(rnd(3) ? rnd(40) : rnd(256));
		break;
	}
	case 22: {              /* INC, DEC */
		seg_prefix();
		o16 = o16pfx();
		if (chance(2)) e8((chance(2) ? 0x40 : 0x48) + rreg());
		else { int w = rnd(2); e8(0xfe + w); rm(rnd(2), !w); }
		break;
	}
	case 23: {              /* MUL, IMUL, NOT, NEG */
		seg_prefix();
		o16 = o16pfx();
		int w = rnd(2);
		e8(0xf6 + w);
		rm(2 + rnd(4), !w);
		break;
	}
	case 24: {              /* IMUL r,r/m(,imm) */
		seg_prefix();
		o16 = o16pfx();
		int k = rnd(3);
		if (k == 0) { e8(0x0f); e8(0xaf); rm(rreg(), 0); }
		else if (k == 1) { e8(0x69); rm(rreg(), 0); ev((unsigned) rs, o16); }
		else { e8(0x6b); rm(rreg(), 0); e8(rnd(256)); }
		break;
	}
	case 25: {              /* safe DIV and IDIV: DX 0, a divisor of 2 or more in CX (no overflow) */
		int pf = chance(4);
		o16 = pf ? g32 : !g32;
		if (pf) e8(0x66);
		e8(0x31); e8(0xd2);                     /* XOR (e)DX,(e)DX */
		if (pf) e8(0x66);
		e8(0xb9); ev(2 + rnd(1000), o16);       /* MOV (e)CX,n */
		if (pf) e8(0x66);
		e8(0xf7); e8(chance(2) ? 0xf1 : 0xf9);  /* DIV / IDIV (e)CX */
		break;
	}
	case 26: {              /* MOVZX, MOVSX, SETcc, BSF, BSR */
		seg_prefix();
		o16 = o16pfx();
		int k = rnd(4);
		e8(0x0f);
		if (k == 0) { e8(0xb6 + rnd(2)); rm(rreg(), gm[gp - 1] == 0xb6); }
		else if (k == 1) { e8(0xbe + rnd(2)); rm(rreg(), gm[gp - 1] == 0xbe); }
		else if (k == 2) { e8(0x90 + rnd(16)); rm(0, 1); }
		else { e8(0xbc + rnd(2)); rm(rreg(), 0); }
		break;
	}
	case 27: {              /* BT BTS BTR BTC: by a small register, and by an immediate (beyond the operand too) */
		if (chance(2)) {
			e8(0xb1); e8(rnd(128));          /* MOV CL,n */
			if (g32) { e8(0x0f); e8(0xb6); e8(0xc9); }   /* MOVZX ECX,CL */
			else { e8(0x66); e8(0x0f); e8(0xb6); e8(0xc9); }   /* MOVZX ECX,CL */
			seg_prefix();
			o16 = o16pfx();
			e8(0x0f); e8(0xa3 + 8 * rnd(4));
			rm(1, 0);
		} else {
			seg_prefix();
			o16 = o16pfx();
			e8(0x0f); e8(0xba);
			rm(4 + rnd(4), 0);
			e8(rnd(128));
		}
		break;
	}
	case 28: {              /* SHLD, SHRD */
		seg_prefix();
		o16 = o16pfx();
		int k = rnd(4);
		static const u8 opc[] = { 0xa4, 0xa5, 0xac, 0xad };
		e8(0x0f); e8(opc[k]);
		rm(rnd(8), 0);
		if (!(k & 1)) e8(rnd(40));
		break;
	}
	case 29 ... 30: {       /* the stack, kept level: the same operand size for both halves */
		int pf = chance(4);
		o16 = pf ? g32 : !g32;
#define P() do { if (pf) e8(0x66); } while (0)
		switch (rnd(7)) {
		case 0: P(); e8(0x50 + rnd(8)); P(); e8(0x58 + rreg()); break;
		case 1: P(); e8(0x68); ev((unsigned) rs, o16); P(); e8(0x58 + rreg()); break;
		case 2: P(); e8(0x6a); e8(rnd(256)); P(); e8(0x58 + rreg()); break;
		case 3: P(); e8(0x9c); P(); e8(0x58 + rreg()); P(); e8(0x9c); P(); e8(0x9d); break;   /* PUSHF, POP, PUSHF, POPF */
		case 4: P(); e8(0x60); P(); e8(0x61); break;           /* PUSHA, POPA */
		case 5: P(); e8(0x50 + rreg()); P(); e8(0x8f); memop(0); break;   /* PUSH, POP r/m */
		default: P(); e8(0x9c); P(); e8(0x9d); break;          /* PUSHF, POPF */
		}
#undef P
		break;
	}
	case 31: {              /* XCHG, LEA, XLAT, CBW, CWD, LAHF, SAHF, flags */
		seg_prefix();
		o16 = o16pfx();
		switch (rnd(8)) {
		case 0: e8(0x86 + rnd(2)); rm(rnd(3), !(gm[gp - 1] & 1)); break;
		case 1: e8(0x8d); memop(rreg()); break;
		case 2: e8(0xd7); break;
		case 3: e8(0x98 + rnd(2)); break;
		case 4: e8(0x9e + rnd(2)); break;
		case 5: e8(0xf5 + rnd(2) * 3); break;                 /* CMC, CLC */
		case 6: e8(0xf9); break;                               /* STC */
		default: e8(0x91 + rnd(2)); break;                     /* XCHG eAX with CX or DX */
		}
		break;
	}
	case 32 ... 34: {       /* string instructions, with DF either way */
		uword base = g32 ? P_DATA : 0;
		e8(chance(3) ? 0xfd : 0xfc);                       /* STD or CLD */
		if (g32) {
			e8(0xbe); e32(base + 0x800 + rnd(0x3000));     /* MOV ESI */
			e8(0xbf); e32(base + 0x800 + rnd(0x3000));     /* MOV EDI */
			e8(0xb9); e32(rnd(4) ? rnd(40) : rnd(400));    /* MOV ECX */
		} else {
			e8(0xbe); e16(chance(4) ? 0xfff0 + rnd(16) : rnd(0x10000));
			e8(0xbf); e16(chance(4) ? 0xfff0 + rnd(16) : rnd(0x10000));
			e8(0xb9); e16(rnd(4) ? rnd(40) : rnd(400));
		}
		int vga = !g32 && chance(4);
		if (vga) { e8(0x06); e8(0x68); e16(0xa000); e8(0x07); }  /* PUSH ES, PUSH A000h, POP ES */
		static const u8 ops[] = { 0xa4, 0xa5, 0xaa, 0xab, 0xac, 0xad, 0xa6, 0xa7, 0xae, 0xaf };
		int op = ops[rnd(10)];
		if (!vga) seg_prefix();
		if (chance(3)) e8(0x66);
		int pr = rnd(3);
		if (pr) e8(pr == 1 ? 0xf3 : 0xf2);
		e8(op);
		if (vga) e8(0x07);                                     /* POP ES */
		if (g32) { e8(0xbe); e32(rnd(16)); e8(0xbf); e32(P_DATA); }
		else { e8(0xbe); e16(rnd(16)); e8(0xbf); e16(rnd(0x8000)); }
		e8(0xfc);
		break;
	}
	case 35: {              /* LOOP, JCXZ, a short jump */
		/* MOV (E)CX,n (all of it: LOOP takes the address size's); INC AX; LOOPx back */
		if (chance(2)) { e8(0xb9); ev(1 + rnd(5), !g32); e8(0x40); e8(0xe2 - rnd(3)); e8(0xfd); }
		else if (chance(2)) { e8(0xe3); e8(1); e8(0x90); }
		else { e8(0xeb); e8(1); e8(0x90); }
		break;
	}
	case 36: {              /* Jcc short and near */
		if (chance(2)) { e8(0x70 + rnd(16)); e8(1); e8(0x90); }
		else { e8(0x0f); e8(0x80 + rnd(16)); if (g32) e32(1); else e16(1); e8(0x90); }
		break;
	}
	case 37: {              /* CALL and RET near: CALL to a RET over a JMP */
		if (g32) { e8(0xe8); e32(2); e8(0xeb); e8(chance(2) ? 1 : 3); if (gm[gp - 1] == 3) { e8(0xc2); e16(0); } else e8(0xc3); }
		else { e8(0xe8); e16(2); e8(0xeb); e8(1); e8(0xc3); }
		break;
	}
	case 38: {              /* far: JMP to the next instruction, PUSH CS + PUSH + RETF, CALL FAR to a RETF */
		u16 cs = gcs_far;
		switch (rnd(3)) {
		case 0: e8(0xea); if (g32) { e32(glin() + 6); } else { e16((gp - gcode_phys) + 4); } e16(cs); break;
		case 1:
			e8(0x0e); e8(0x68);
			if (g32) { e32(glin() + 5); } else { e16((gp - gcode_phys) + 3); }
			e8(0xcb);
			break;
		default:
			/* CALL FAR to a RETF placed after a JMP over it */
			e8(0x9a);
			if (g32) { e32(glin() + 6 + 2); e16(cs); e8(0xeb); e8(1); e8(0xcb); }
			else { e16((gp - gcode_phys) + 4 + 2); e16(cs); e8(0xeb); e8(1); e8(0xcb); }
			break;
		}
		break;
	}
	case 39: {              /* INT 30h (its handler counts and returns) */
		e8(0xcd); e8(0x30);
		break;
	}
	case 40: {              /* ENTER, LEAVE, of the same size (16-bit ones only on a 16-bit stack) */
		int pf = !g32 && chance(4);
		if (pf) e8(0x66);
		e8(0xc8); e16(rnd(64)); e8(0);
		if (pf) e8(0x66);
		e8(0xc9);
		break;
	}
	case 41: {              /* segment registers: MOV and POP, SS too (the next instruction kept in the step) */
		switch (rnd(4)) {
		case 0: e8(0x8c); e8(0xd8); e8(0x8e); e8(0xc0); break;  /* MOV AX,DS; MOV ES,AX */
		case 1: e8(0x1e); e8(0x07); break;                      /* PUSH DS; POP ES */
		case 2: e8(0x8c); e8(0xd0); e8(0x8e); e8(0xd0); break;  /* MOV AX,SS; MOV SS,AX */
		default: e8(0x16); e8(0x17); break;                     /* PUSH SS; POP SS */
		}
		break;
	}
	case 42: {              /* LES/LDS from a pointer the code stores first, then the register back */
		if (g32) break;
		e8(0xc7); e8(0x06); e16(0x100); e16(rnd(0x10000));      /* MOV [100h],off */
		e8(0xc7); e8(0x06); e16(0x102); e16(R_DS);              /* MOV [102h],DS */
		e8(0xc4); e8(0x06); e16(0x100);                         /* LES AX,[100h] */
		break;
	}
	case 43: {              /* ports (not at level 3, but for mode g); OUT E0h raises an interrupt there and then */
		if ((guser && !glfbuser) || gnopriv || gv86) break;
		if (!guser && chance(3)) {   /* v5.1: OUTSB to E4h (a byte from DS:SI), done by tiny386: it moves the frame buffer too */
			e8(0xba); ev(0xe4, !g32); e8(0x6e);
			break;
		}
		switch (rnd(6)) {
		case 0: e8(0xe4 + rnd(2)); e8(0x40); break;
		case 1: e8(0xba); ev(0x41, !g32); e8(0xec + rnd(2)); break;
		case 2: e8(0xe6 + rnd(2)); e8(0x80); break;
		case 3: e8(0xe6); e8(0xe0); break;
		case 4: e8(0xb0); e8(rnd(4)); e8(0xe6); e8(0xe4); break;   /* MOV AL,n; OUT E4h,AL: the frame buffer moves */
		default: e8(0xba); ev(0x80, !g32); e8(0xee + rnd(2)); break;
		}
		break;
	}
	case 44: {              /* STI, CLI (not at level 3) */
		if (guser || gnopriv) break;
		e8(chance(2) ? 0xfb : 0xfa);
		break;
	}
	case 45: case 50 ... 51: {   /* video memory: the VGA window (and in protected mode the frame buffer) */
		if (g32) {
			int lfb = glfbuser || !chance(3);
			uword a = !lfb ? 0xa0000 + rnd(0x1fff0) : chance(8) ? LFB_BASE + 0x1000 * rnd(16) - 2 + rnd(4) :
				LFB_BASE + rnd(LFB_SIZE - 8);   /* (across a page too) */
			uword a2 = !lfb ? 0xa0800 + rnd(0x1f000) : LFB_BASE + 0x800 + rnd(LFB_SIZE - 0x1000);   /* for strings */
			if ((guser && !glfbuser) || gnopriv) break;
			if (lfb && a < LFB_BASE + 4) a = LFB_BASE + 4;
			if (lfb && a > LFB_BASE + LFB_SIZE - 0x60) a = LFB_BASE + LFB_SIZE - 0x60;   /* (its page tables end there) */
			switch (rnd(13)) {
			case 0: e8(0x88 + rnd(2)); e8(0x05); e32(a); break;         /* MOV [a],r */
			case 1: e8(0x8a + rnd(2)); e8(0x05 | rreg() << 3); e32(a); break;
			case 2: e8(0xc7); e8(0x05); e32(a); e32((unsigned) rs); break;
			case 3: /* REP STOSD into it */
				e8(0xbf); e32(a & ~3u); e8(0xb9); e32(rnd(20)); e8(0xf3); e8(0xab);
				e8(0xbf); e32(P_DATA);
				break;
			case 4: /* 16 bits: MOV [a],r16 or imm16 */
				e8(0x66);
				if (chance(2)) { e8(0x89); e8(0x05 | rnd(8) << 3); e32(a); }
				else { e8(0xc7); e8(0x05); e32(a); e16(rnd(0x10000)); }
				break;
			case 5: /* arithmetic from it: ADD ... CMP r,[a] */
				e8(0x03 | rnd(8) << 3); e8(0x05 | rreg() << 3); e32(a);
				if (chance(2)) jz_after();
				break;
			case 6: /* read-modify-write into it: ADD ... CMP [a],r, or a byte's */
				e8((rnd(8) << 3) | rnd(2)); e8(0x05 | rnd(8) << 3); e32(a);
				break;
			case 7: /* STOSB, STOSW or STOSD, one */
				e8(0xbf); e32(a); if (chance(3)) e8(0xfd);
				{ int k = rnd(3); if (k == 1) e8(0x66); e8(k ? 0xab : 0xaa); }
				e8(0xfc); e8(0xbf); e32(P_DATA);
				break;
			case 8: { /* MOVS, one or REP, between it and the data or within it, DF either way */
				int k = rnd(3), n = 1 + rnd(400), down = chance(3);
				uword d = P_DATA + 0x800 + rnd(0x2000);
				uword from = k == 0 ? a2 : d, to = k == 1 ? a2 : k == 0 ? d : LFB_BASE + 0x800 + rnd(LFB_SIZE - 0x1000);
				if (k == 2) from = a2;
				if (k == 2 && !lfb) to = 0xa0800 + rnd(0x1f000);
				e8(0xbe); e32(from); e8(0xbf); e32(to); e8(0xb9); e32(n);
				if (down) e8(0xfd);
				int sz = rnd(3);
				if (sz == 1) e8(0x66);
				if (!chance(4)) e8(0xf3);
				e8(sz ? 0xa5 : 0xa4);
				e8(0xfc);
				e8(0xbe); e32(rnd(16)); e8(0xbf); e32(P_DATA);
				break;
			}
			case 9: /* MOVZX or MOVSX from it; PUSH it, POP r; LODS */
				switch (rnd(3)) {
				case 0: e8(0x0f); e8(0xb6 + 8 * rnd(2) + rnd(2)); e8(0x05 | rreg() << 3); e32(a); break;
				case 1: e8(0xff); e8(0x35); e32(a); e8(0x58 + rreg()); break;
				default: e8(0xbe); e32(a); e8(0xac + rnd(2)); e8(0xbe); e32(rnd(16)); break;
				}
				break;
			case 10: /* ARPL [a],r16: written when its RPL is the lower */
				e8(0x63); e8(0x05 | rreg() << 3); e32(a);
				break;
			case 11: /* XCHG [a],r */
				e8(0x86 + rnd(2)); e8(0x05 | rreg() << 3); e32(a);
				break;
			default: /* CMPS or SCAS on it */
				e8(0xbe); e32(a2); e8(0xbf); e32(chance(2) ? a2 + 4 * rnd(64) : P_DATA + rnd(0x2000));
				e8(0xb9); e32(rnd(40));
				if (chance(2)) e8(0xf3 - rnd(2));
				e8(chance(2) ? 0xa7 : 0xaf);
				e8(0xbe); e32(rnd(16)); e8(0xbf); e32(P_DATA);
				break;
			}
		} else {
			e8(0x64);                       /* FS: (A000h) */
			int op = 0x88 + rnd(4);
			e8(op);
			memop((op & 2) ? ((op & 1) ? rreg() : rreg8()) : rnd(8));
		}
		break;
	}
	case 46: {              /* the page tables' accessed and dirty bits cleared without INVLPG; INVLPG; MOV CR3 */
		if (!g32 || guser || gnopriv) break;
		switch (rnd(4)) {
		case 3: e8(0x81); e8(0x25); e32(P_PT + 0x3000 + rnd(16) * 4); e32(0xffffff9f); break;   /* v5.1: the frame buffer's PTEs */
		case 0: e8(0x81); e8(0x25); e32(P_PT + 0x1000 + ((P_DATA >> 12) & 1023) * 4 + rnd(4) * 4); e32(0xffffff9f); break;
		case 1: e8(0x0f); e8(0x01); e8(0x3b); break;            /* INVLPG [EBX] */
		default: e8(0x0f); e8(0x20); e8(0xd8); e8(0x0f); e8(0x22); e8(0xd8); break; /* MOV EAX,CR3; MOV CR3,EAX */
		}
		break;
	}
	case 47: {              /* ARPL (protected mode), LSL, and instructions only tiny386 does */
		if (g32) { if (chance(2)) { e8(0x63); rm(rnd(3), 0); } else { e8(0x0f); e8(0x03); e8(0xc0); } }
		else if (gpm16 && chance(2)) { seg_prefix(); e8(0x63); rm(rnd(3), 0); }   /* ARPL in 16-bit protected mode */
		else { static const u8 t[] = { 0x27, 0x2f, 0x37, 0x3f }; e8(t[rnd(4)]); }
		break;
	}
	case 48 ... 49: {       /* FF /2 /3 /4 /5 /6 (indirect CALL and JMP, near and far; PUSH Ev) */
		uword s = g32 ? glin() : gp - gcode_phys;   /* this group's first offset */
		switch (rnd(5)) {
		case 0:         /* MOV eAX,t; CALL eAX; JMP +1; t: RET */
			if (g32) { e8(0xb8); e32(s + 9); } else { e8(0xb8); e16(s + 7); }
			e8(0xff); e8(0xd0); e8(0xeb); e8(0x01); e8(0xc3);
			break;
		case 1:         /* MOV eAX,t; JMP eAX; t: */
			if (g32) { e8(0xb8); e32(s + 7); } else { e8(0xb8); e16(s + 5); }
			e8(0xff); e8(0xe0);
			break;
		case 2:         /* the far pointer stored, CALL FAR [it]; JMP +1; t: RETF */
			if (g32) {
				e8(0xc7); e8(0x05); e32(P_DATA + 0x100); e32(s + 27);
				e8(0x66); e8(0xc7); e8(0x05); e32(P_DATA + 0x104); e16(gcs_far);
				e8(0xff); e8(0x1d); e32(P_DATA + 0x100);
			} else {
				e8(0xc7); e8(0x06); e16(0x100); e16(s + 18);
				e8(0xc7); e8(0x06); e16(0x102); e16(gcs_far);
				e8(0xff); e8(0x1e); e16(0x100);
			}
			e8(0xeb); e8(0x01); e8(0xcb);
			break;
		case 3:         /* the far pointer stored, JMP FAR [it]; t: */
			if (g32) {
				e8(0xc7); e8(0x05); e32(P_DATA + 0x100); e32(s + 25);
				e8(0x66); e8(0xc7); e8(0x05); e32(P_DATA + 0x104); e16(gcs_far);
				e8(0xff); e8(0x2d); e32(P_DATA + 0x100);
			} else {
				e8(0xc7); e8(0x06); e16(0x100); e16(s + 16);
				e8(0xc7); e8(0x06); e16(0x102); e16(gcs_far);
				e8(0xff); e8(0x2e); e16(0x100);
			}
			break;
		default:        /* PUSH Ev; POP r */
			seg_prefix();
			o16 = o16pfx();
			e8(0xff); rm(6, 0);
			if (o16 != !g32) e8(0x66);
			e8(0x58 + rreg());
			break;
		}
		break;
	}
	default: {              /* more of the common ones */
		pfx67();
		seg_prefix();
		o16 = o16pfx();
		int op = rnd(8) << 3 | 1 | (rnd(2) << 1);
		e8(op);
		rm((op & 2) ? rreg() : rnd(8), 0);
		if ((op >> 3) == 7) jz_after();
		break;
	}
	}
}

/* == the machines set up */
static void put32(u8 *mem, uword a, u32 v) { memcpy(mem + a, &v, 4); }
static void desc(u8 *mem, uword at, u32 base, u32 limit, u32 tf)
{
	u32 w1 = (limit & 0xffff) | (base << 16);
	u32 w2 = ((base >> 16) & 0xff) | ((tf & 0xff) << 8) | (limit & 0xf0000) |
		((tf & 0xf00) << 12) | (base & 0xff000000);
	put32(mem, at, w1);
	put32(mem, at + 4, w2);
}
static void gate(u8 *mem, int vec, u32 sel, u32 off, u32 type)
{
	put32(mem, IDT_PHYS + vec * 8, (off & 0xffff) | (sel << 16));
	put32(mem, IDT_PHYS + vec * 8 + 4, (off & 0xffff0000) | (type << 8));
}

static void mach_new(struct mach *m, int fast)
{
	memset(m, 0, sizeof(*m));
	m->mem = calloc(1, MEM);
	CPU_CB *cb;
	m->cpu = cpui386_new(3, (char *) m->mem, MEM, &cb);
	/* cpui386_new() leaves the lazy flags' operation and operands as
	   malloc() gave them (the guest never sees them: cc.mask is 0); with
	   musl (the RV32 build) the two machines then start with different
	   garbage there, which compare() would report (the CPU review of v5.0) */
	m->cpu->cc.op = 0;
	m->cpu->cc.dst = m->cpu->cc.src1 = m->cpu->cc.src2 = m->cpu->cc.dst2 = 0;
	cpui386_set_fast(m->cpu, fast);
	cb->pic = m;
	cb->pic_read_irq = pic_irq;
	cb->io = m;
	cb->io_read8 = io_r8; cb->io_write8 = io_w8;
	cb->io_read16 = io_r16; cb->io_write16 = io_w16;
	cb->io_read32 = io_r32; cb->io_write32 = io_w32;
	cb->iomem = m;
	cb->iomem_read8 = mm_r8; cb->iomem_write8 = mm_w8;
	cb->iomem_read16 = mm_r16; cb->iomem_write16 = mm_w16;
	cb->iomem_read32 = mm_r32; cb->iomem_write32 = mm_w32;
	cb->iomem_write_string = mm_ws;
	if (glfb) {
		cpu_cb_set_lfb_marks(cb, m->marks, LFB_MARK_SHIFT, &marks_on, &m->wrote);
		cpu_cb_set_lfb(cb, LFB_BASE, LFB_SIZE, m->lfb);
	}
}

static void mach_free(struct mach *m)
{
	cpui386_delete(m->cpu);
	free(m->mem);
}

/* real mode: CS:IP 1000h:0, data at DS 2000h, the stack at SS 4000h; the
   vector table's handlers at 800h:x: an IRQ (vector 8) and INT 30h count
   and IRET */
static void setup_real(u8 *mem, CPUI386 *cpu)
{
	/* vector 8 and 30h: INC WORD [CS:n]; IRET */
	for (int v = 0; v < 256; v++) {
		u16 off = v == 8 ? 0x10 : v == 0x30 ? 0x20 : 0x30;
		put32(mem, v * 4, (R_HANDLER << 16) | off);
	}
	u8 *h = mem + (R_HANDLER << 4);
	const u8 inc_iret[] = { 0x2e, 0xff, 0x06, 0x00, 0x01, 0xcf };
	memcpy(h + 0x10, inc_iret, sizeof(inc_iret));
	memcpy(h + 0x20, inc_iret, sizeof(inc_iret));
	h[0x20 + 3] = 0x02;
	/* anything else: OUT F0h,AL; HLT */
	const u8 panic[] = { 0xb0, 0xee, 0xe6, 0xf0, 0xf4, 0xeb, 0xfd };
	memcpy(h + 0x30, panic, sizeof(panic));
	cpu->seg[SEG_CS].sel = R_CS; cpu->seg[SEG_CS].base = R_CS << 4;
	cpu->seg[SEG_CS].limit = 0xffff; cpu->seg[SEG_CS].flags = 0;
	for (int s = 0; s < 6; s++) {
		if (s == SEG_CS) continue;
		u16 sel = s == SEG_SS ? R_SS : s == SEG_FS ? 0xa000 : R_DS;
		cpu->seg[s].sel = sel; cpu->seg[s].base = sel << 4; cpu->seg[s].limit = 0xffff; cpu->seg[s].flags = 0;
	}
	cpu->next_ip = 0;
	REGi(3) = 0xffc0;       /* BX near the segment's end: [bx+d8] runs past it */
	REGi(6) = 8;            /* SI */
	REGi(7) = 0x8000;       /* DI */
	REGi(5) = 0x0ffe;       /* BP */
	REGi(4) = 0xf000;       /* SP */
	cpu->flags = 0x2;
}

/* protected mode with paging: flat segments for levels 0 and 3, a TSS
   for the stack of level 0, handlers for the IRQ (vector 20h), INT 30h,
   page faults (which map the data page that was not present) and the
   rest (a panic) */
static void setup_prot(u8 *mem, CPUI386 *cpu, int user)
{
	desc(mem, GDT_PHYS + 0x08, 0, 0xfffff, 0xc9a);   /* code, level 0 */
	desc(mem, GDT_PHYS + 0x10, 0, 0xfffff, 0xc92);   /* data, level 0 */
	desc(mem, GDT_PHYS + 0x18, 0, 0xfffff, 0xcfa);   /* code, level 3 */
	desc(mem, GDT_PHYS + 0x20, 0, 0xfffff, 0xcf2);   /* data, level 3 */
	desc(mem, GDT_PHYS + 0x28, TSS_PHYS, 0x67, 0x089);   /* TSS */
	put32(mem, TSS_PHYS + 4, P_STACK);
	put32(mem, TSS_PHYS + 8, 0x10);
	/* the handlers, linear = physical (identity in the first 4 MiB) */
	u8 *h = mem + HND_PHYS;
	/* IRQ: INC DWORD [CNT]; IRET */
	const u8 irq[] = { 0xff, 0x05, 0, 0, 0, 0, 0xcf };
	memcpy(h, irq, sizeof(irq));
	put32(mem, HND_PHYS + 2, CNT_PHYS);
	memcpy(h + 0x10, irq, sizeof(irq));
	put32(mem, HND_PHYS + 0x12, CNT_PHYS + 4);
	/* page fault: PUSH EAX; MOV EAX,CR2; SHR EAX,12; OR DWORD [PT2_LIN + (EAX & 3FFh)*4],1 (present); POP EAX; ADD ESP,4; IRET */
	const u8 pf[] = { 0x50, 0x0f, 0x20, 0xd0, 0xc1, 0xe8, 0x0c, 0x25, 0xff, 0x03, 0, 0,
			  0x81, 0x0c, 0x85, 0, 0, 0, 0, 1, 0, 0, 0, 0x58, 0x83, 0xc4, 0x04, 0xcf };
	memcpy(h + 0x20, pf, sizeof(pf));
	put32(mem, HND_PHYS + 0x20 + 15, P_PT + 0x1000);
	/* anything else: MOV AL,vector; OUT F0h,AL; HLT */
	for (int v = 0; v < 32; v++) {
		u8 *p = h + 0x100 + v * 8;
		p[0] = 0xb0; p[1] = v; p[2] = 0xe6; p[3] = 0xf0; p[4] = 0xf4; p[5] = 0xeb; p[6] = 0xfd;
	}
	for (int v = 0; v < 256; v++) {
		u32 off = HND_PHYS + 0x100 + (v < 32 ? v : 31) * 8;
		u32 type = 0x8e;   /* interrupt gates */
		if (v == 0x20) off = HND_PHYS;
		if (v == 0x30) { off = HND_PHYS + 0x10; type = 0xee; }   /* DPL 3: INT 30h from level 3 too */
		if (v == 14) off = HND_PHYS + 0x20;
		gate(mem, v, 0x08, off, type);
	}
	/* the page tables: 0..4 MiB identity (supervisor), 4 MiB up for the
	   code, data and stacks (user, writable), the tables themselves at
	   P_PT, the frame buffer at E0000000h */
	put32(mem, PD_PHYS + 0, PT_PHYS | 0x007);
	put32(mem, PD_PHYS + ((P_CODE >> 22) * 4), PT2_PHYS | 0x007);
	put32(mem, PD_PHYS + ((LFB_BASE >> 22) * 4), PTL_PHYS | 0x003);
	for (int i = 0; i < 1024; i++)
		put32(mem, PT_PHYS + i * 4, (i << 12) | 0x003);
	/* code: 64 KiB at P_CODE from physical 100000h; data: 4 pages; the stacks: 4 pages each */
	for (int i = 0; i < 16; i++)
		put32(mem, PT2_PHYS + (((P_CODE >> 12) + i) & 1023) * 4, (0x100000 + i * 0x1000) | 0x007);
	for (int i = 0; i < 4; i++)
		put32(mem, PT2_PHYS + (((P_DATA >> 12) + i) & 1023) * 4,
		      (P_DATA_PHYS + i * 0x1000) | ((i & 1) ? 0x006 : 0x007));   /* 2nd and 4th not present */
	for (int i = 1; i <= 4; i++) {
		put32(mem, PT2_PHYS + (((P_STACK >> 12) - i) & 1023) * 4, (0x1c0000 - i * 0x1000) | 0x007);
		put32(mem, PT2_PHYS + (((P_USTACK >> 12) - i) & 1023) * 4, (0x1d0000 - i * 0x1000) | 0x007);
	}
	put32(mem, PT2_PHYS + (((P_PT >> 12) + 0) & 1023) * 4, PD_PHYS | 0x003);
	put32(mem, PT2_PHYS + (((P_PT >> 12) + 1) & 1023) * 4, PT2_PHYS | 0x003);
	/* the frame buffer's pages and (v5.1) two present pages after it, and
	   its page table at P_PT + 3000h, so that the guest can clear their
	   accessed and dirty bits */
	for (int i = 0; i < LFB_SIZE / 4096 + 2; i++)
		put32(mem, PTL_PHYS + i * 4, (LFB_BASE + i * 0x1000) | 0x003);
	put32(mem, PT2_PHYS + (((P_PT >> 12) + 3) & 1023) * 4, PTL_PHYS | 0x003);

	cpu->gdt.base = GDT_PHYS; cpu->gdt.limit = 0x2f;
	cpu->idt.base = IDT_PHYS; cpu->idt.limit = 0x7ff;
	cpu->cr3 = PD_PHYS;
	cpu->cr0 = 0x80000001;
	tlb_clear(cpu);
	int cs = user ? 0x1b : 0x08, ds = user ? 0x23 : 0x10;
	for (int s = 0; s < 6; s++) {
		int sel = s == SEG_CS ? cs : ds;
		cpu->seg[s].sel = sel; cpu->seg[s].base = 0; cpu->seg[s].limit = 0xffffffff;
		cpu->seg[s].flags = 0xcf00 | (s == SEG_CS ? (user ? 0xfa : 0x9a) : (user ? 0xf2 : 0x92));
	}
	cpu->seg[SEG_TR].sel = 0x28; cpu->seg[SEG_TR].base = TSS_PHYS; cpu->seg[SEG_TR].limit = 0x67;
	cpu->seg[SEG_TR].flags = 0x8b;
	cpu->cpl = user ? 3 : 0;
	cpu->code16 = false;
	cpu->sp_mask = 0xffffffff;
	cpu->next_ip = P_CODE;
	REGi(3) = P_DATA + 0xfc0;       /* EBX: [ebx+d8] crosses into the second page */
	REGi(6) = 3;                    /* ESI */
	REGi(7) = P_DATA;               /* EDI */
	REGi(5) = P_DATA + 0x2fc0;      /* EBP */
	REGi(4) = user ? P_USTACK - 0x100 : P_STACK - 0x100;
	cpu->flags = 0x2;
}

/* == the modes added after the v5.0 reviews: virtual-8086 mode, 16-bit
   protected mode, level 1 (and 2) */

/* handlers that address their counters through CS (flat, 0x08), for
   interrupts that arrive with DS null (from V86 mode) or 16-bit */
#define HND2 (HND_PHYS + 0x40)
static void cs_handlers(u8 *mem)
{
	u8 *h = mem + HND2;
	/* IRQ and INT 30h: INC DWORD CS:[CNT]; IRETD */
	const u8 irq[] = { 0x2e, 0xff, 0x05, 0, 0, 0, 0, 0xcf };
	memcpy(h, irq, sizeof(irq));
	put32(mem, HND2 + 3, CNT_PHYS);
	memcpy(h + 0x08, irq, sizeof(irq));
	put32(mem, HND2 + 0x08 + 3, CNT_PHYS + 4);
	/* page fault: PUSH EAX; MOV EAX,CR2; SHR EAX,12; AND EAX,3FFh;
	   OR DWORD CS:[P_PT+2000h + EAX*4],1; POP EAX; ADD ESP,4; IRETD
	   (the page table for 0..4 MiB, mapped at P_PT + 2000h) */
	const u8 pf[] = { 0x50, 0x0f, 0x20, 0xd0, 0xc1, 0xe8, 0x0c, 0x25, 0xff, 0x03, 0, 0,
			  0x2e, 0x81, 0x0c, 0x85, 0, 0, 0, 0, 1, 0, 0, 0, 0x58, 0x83, 0xc4, 0x04, 0xcf };
	memcpy(h + 0x20, pf, sizeof(pf));
	put32(mem, HND2 + 0x20 + 16, P_PT + 0x2000);
	gate(mem, 0x20, 0x08, HND2, 0x8e);
	gate(mem, 0x30, 0x08, HND2 + 0x08, 0xee);
	gate(mem, 14, 0x08, HND2 + 0x20, 0x8e);
	put32(mem, PT2_PHYS + (((P_PT >> 12) + 2) & 1023) * 4, PT_PHYS | 0x003);
}

static void real_layout_regs(CPUI386 *cpu)
{
	cpu->next_ip = 0;
	REGi(3) = 0xffc0; REGi(6) = 8; REGi(7) = 0x8000; REGi(5) = 0x0ffe; REGi(4) = 0xf000;
}

/* virtual-8086 mode at IOPL 3 with paging: the real-mode layout, the first
   4 MiB user pages, two of DS's pages not present until the page fault
   handler (ring 0) maps them */
static void setup_v86(u8 *mem, CPUI386 *cpu)
{
	setup_prot(mem, cpu, 0);
	cs_handlers(mem);
	for (int i = 0; i < 1024; i++)
		put32(mem, PT_PHYS + i * 4, (i << 12) | 0x007);
	put32(mem, PT_PHYS + 0x21 * 4, 0x21000 | 0x006);
	put32(mem, PT_PHYS + 0x2b * 4, 0x2b000 | 0x006);
	tlb_clear(cpu);
	static const u16 sel[6] = { R_DS, R_CS, R_SS, R_DS, 0xa000, R_DS };   /* ES CS SS DS FS GS */
	for (int s = 0; s < 6; s++) {
		cpu->seg[s].sel = sel[s]; cpu->seg[s].base = sel[s] << 4;
		cpu->seg[s].limit = 0xffff; cpu->seg[s].flags = 0;
	}
	cpu->cpl = 3;
	cpu->code16 = true;
	cpu->sp_mask = 0xffff;
	cpu->flags = 0x2 | VM | (3 << 12);
	real_layout_regs(cpu);
}

/* 16-bit protected mode at level 0: a GDT whose selectors are the
   real-mode layout's segments (1000h code, 2000h data, 4000h stack, A000h
   the VGA window), with or without paging */
#define GDT16_PHYS 0x1a0000u
static void setup_pm16(u8 *mem, CPUI386 *cpu, int paging)
{
	setup_prot(mem, cpu, 0);
	cs_handlers(mem);
	memcpy(mem + GDT16_PHYS, mem + GDT_PHYS, 0x30);
	desc(mem, GDT16_PHYS + R_CS, R_CS << 4, 0xffff, 0x09a);
	desc(mem, GDT16_PHYS + R_DS, R_DS << 4, 0xffff, 0x092);
	desc(mem, GDT16_PHYS + R_SS, R_SS << 4, 0xffff, 0x092);
	desc(mem, GDT16_PHYS + 0xa000, 0xa0000, 0xffff, 0x092);
	cpu->gdt.base = GDT16_PHYS; cpu->gdt.limit = 0xffff;
	put32(mem, PT_PHYS + 0x21 * 4, 0x21000 | 0x002);
	put32(mem, PT_PHYS + 0x2b * 4, 0x2b000 | 0x002);
	cpu->cr0 = paging ? 0x80000001 : 0x1;
	tlb_clear(cpu);
	static const u16 sel[6] = { R_DS, R_CS, R_SS, R_DS, 0xa000, R_DS };
	for (int s = 0; s < 6; s++) {
		cpu->seg[s].sel = sel[s]; cpu->seg[s].base = sel[s] << 4;
		cpu->seg[s].limit = 0xffff; cpu->seg[s].flags = s == SEG_CS ? 0x9a : 0x92;
	}
	cpu->cpl = 0;
	cpu->code16 = true;
	cpu->sp_mask = 0xffff;
	cpu->flags = 0x2;
	real_layout_regs(cpu);
}

/* 32-bit protected mode with paging at level 1 (code 31h, data and stack
   39h, the stack in user pages) */
static void setup_cpl1(u8 *mem, CPUI386 *cpu)
{
	setup_prot(mem, cpu, 0);
	desc(mem, GDT_PHYS + 0x30, 0, 0xfffff, 0xcba);
	desc(mem, GDT_PHYS + 0x38, 0, 0xfffff, 0xcb2);
	cpu->gdt.limit = 0x3f;
	for (int s = 0; s < 6; s++) {
		cpu->seg[s].sel = s == SEG_CS ? 0x31 : 0x39;
		cpu->seg[s].flags = 0xcf00 | (s == SEG_CS ? 0xba : 0xb2);
	}
	cpu->cpl = 1;
	REGi(4) = P_USTACK - 0x100;
}

/* == the fast interpreter's TLB (v5.1, from v5.0's recheck): its
   invariants, on machine 0 after each step.  First fast_tlb_check(), which
   the fast interpreter itself calls at the start of every step: it empties
   the entries that a change of paging, a flush of tiny386's TLB or a move
   of the frame buffer has made stale (so that a check that does not, shows
   here).  Then each entry with a tag must map its linear page where
   tiny386's TLB (with paging) or linear = physical (without) puts it, into
   RAM or into the frame buffer as cb.lfb_* say now; a plain write tag only
   for RAM, a FAST_TAG_LFB write tag only for the frame buffer; ppte
   tiny386's; with paging, the rights of the entry's table allow the read
   (and the write). */
static long inv_entries, inv_lfb_r, inv_lfb_w, inv_runs;
static const char *check_fast_tlb(void)
{
	static char why[200];
	CPUI386 *c = M[0].cpu;
	struct fast_state *fs = c->fast;
	if (!c->fast_on) return NULL;
	fast_tlb_check(c);
	inv_runs++;
	int pg_on = !!(c->cr0 & CR0_PG);
	for (int k = 0; k < 2; k++)
		for (int i = 0; i < tlb_size; i++) {
			struct fast_tlb_entry *e = &fs->tab[k][i];
			if (e->rtag == (uword) -1) {
				if (e->wtag != (uword) -1) { snprintf(why, sizeof why, "tab %d %d: wtag %x without rtag", k, i, e->wtag); return why; }
				continue;
			}
			if (!pg_on && k == 1) { snprintf(why, sizeof why, "tab 1 used without paging"); return why; }
			inv_entries++;
			uword pg = e->rtag, paddr;
			if ((pg % tlb_size) != (uword) i) { snprintf(why, sizeof why, "tab %d %d: page %x at the wrong index", k, i, pg); return why; }
			if (pg_on) {
				struct tlb_entry *t = &c->tlb.tab[i];
				if (t->lpgno != pg) { snprintf(why, sizeof why, "tab %d %d: page %x, tiny386 has %x", k, i, pg, t->lpgno); return why; }
				paddr = (t->xaddr ^ (pg << 12)) & ~0xfffu;
				if (e->ppte != t->ppte) { snprintf(why, sizeof why, "tab %d %d: ppte differs", k, i); return why; }
				if (t->pte_lookup[k][0]) { snprintf(why, sizeof why, "tab %d %d: page %x not readable at this table's level", k, i, pg); return why; }
				if (e->wtag != (uword) -1 && t->pte_lookup[k][1]) { snprintf(why, sizeof why, "tab %d %d: page %x not writable at this table's level", k, i, pg); return why; }
			} else {
				paddr = pg << 12;
			}
			uintptr_t host = (uintptr_t) (pg << 12) + e->delta;
			uword off = paddr - c->cb.lfb_base;
			int lfb = in_high_iomem(paddr) && off < c->cb.lfb_lim1;
			if (lfb) {
				inv_lfb_r++;
				if (c->cb.lfb_lim1 - off < 0x1000) { snprintf(why, sizeof why, "tab %d %d: frame buffer page %x only partly inside", k, i, paddr); return why; }
				if (host != (uintptr_t) c->cb.lfb_mem + off) { snprintf(why, sizeof why, "tab %d %d: frame buffer page %x at the wrong host address", k, i, paddr); return why; }
				if (e->wtag != (uword) -1 && e->wtag != (pg | FAST_TAG_LFB)) { snprintf(why, sizeof why, "tab %d %d: frame buffer page %x with write tag %x", k, i, paddr, e->wtag); return why; }
				if (e->wtag != (uword) -1) inv_lfb_w++;
			} else {
				if (in_iomem(paddr) || paddr > (uword) c->phys_mem_size - 0x1000) { snprintf(why, sizeof why, "tab %d %d: page %x -> %x neither RAM nor the frame buffer", k, i, pg, paddr); return why; }
				if (host != (uintptr_t) c->phys_mem + paddr) { snprintf(why, sizeof why, "tab %d %d: RAM page %x at the wrong host address", k, i, paddr); return why; }
				if (e->wtag != (uword) -1 && e->wtag != pg) { snprintf(why, sizeof why, "tab %d %d: RAM page %x with write tag %x", k, i, paddr, e->wtag); return why; }
			}
		}
	return NULL;
}

/* == the comparison */
static int fails, checks;
static int compare(const char *what, int seed, long step)
{
	CPUI386 *a = M[0].cpu, *b = M[1].cpu;
	char why[256] = "";
#define F(field) if (!why[0] && memcmp(&a->field, &b->field, sizeof(a->field))) snprintf(why, sizeof(why), "%s", #field)
	F(gprx); F(ip); F(next_ip); F(flags); F(cpl); F(code16); F(sp_mask); F(halt);
	F(seg); F(idt); F(gdt); F(cr0); F(cr2); F(cr3); F(cc.op); F(cc.mask); F(cycle);
	F(intr); F(ifetch.laddr); F(ifetch.xaddr);
	if (!why[0] && a->cc.mask && (a->cc.dst != b->cc.dst || a->cc.src1 != b->cc.src1 || a->cc.src2 != b->cc.src2 ||
				      a->cc.dst2 != b->cc.dst2))
		snprintf(why, sizeof(why), "cc operands");
#undef F
	for (int i = 0; !why[0] && i < tlb_size; i++) {
		struct tlb_entry *x = &a->tlb.tab[i], *y = &b->tlb.tab[i];
		if (x->lpgno != y->lpgno || (x->lpgno != (uword) -1 && (x->xaddr != y->xaddr || x->pte_lookup != y->pte_lookup ||
								     x->ppte - M[0].mem != y->ppte - M[1].mem)))
			snprintf(why, sizeof(why), "tiny386's TLB entry %d (%08x / %08x)", i, x->lpgno, y->lpgno);
	}
	if (!why[0] && memcmp(M[0].mem, M[1].mem, MEM)) {
		long i = 0;
		while (M[0].mem[i] == M[1].mem[i]) i++;
		snprintf(why, sizeof(why), "memory at %06lx (%02x / %02x)", i, M[0].mem[i], M[1].mem[i]);
	}
	if (!why[0] && (memcmp(M[0].vga, M[1].vga, sizeof(M[0].vga)) || memcmp(M[0].lfb, M[1].lfb, sizeof(M[0].lfb))))
		snprintf(why, sizeof(why), "video memory");
	if (!why[0] && (memcmp(M[0].marks, M[1].marks, sizeof(M[0].marks)) || M[0].wrote != M[1].wrote))
		snprintf(why, sizeof(why), "the frame buffer's marks or flag");
	if (!why[0] && (M[0].log != M[1].log || M[0].nlog != M[1].nlog))
		snprintf(why, sizeof(why), "port and video memory accesses (%u / %u)", M[0].nlog, M[1].nlog);
	checks++;
	if (why[0]) {
		fails++;
		printf("  FAIL %s seed %d step %ld: %s differs; fast at %04x:%08x, tiny386 at %04x:%08x, cycles %ld / %ld\n",
		       what, seed, step, why, (unsigned) a->seg[SEG_CS].sel, (unsigned) a->next_ip,
		       (unsigned) b->seg[SEG_CS].sel, (unsigned) b->next_ip, a->cycle, b->cycle);
		return 1;
	}
	return 0;
}

/* one scenario: mode 0 real, 1 protected at level 0, 2 at level 3;
   since the v5.0 reviews: 3 virtual-8086, 4 16-bit protected with paging, 5
   level 1, 6 16-bit protected without paging, 7 level 1 reading a
   supervisor page that level 0 used (hand-written), 8 self-modifying real
   mode code (hand-written), 9 CR0.WP set after a supervisor store into a
   read-only page (hand-written) */
static long total_insns, total_steps;
static int verbose_runs = 1;
static void setup_mode(int mode, u8 *mem, CPUI386 *cpu)
{
	switch (mode) {
	case 0: case 8: setup_real(mem, cpu); break;
	case 1: case 9: setup_prot(mem, cpu, 0); break;
	case 2: setup_prot(mem, cpu, 1); break;
	case 3: case 10: setup_v86(mem, cpu); if (mode == 10) cpu->flags &= ~(3 << 12); break;
	case 4: setup_pm16(mem, cpu, 1); break;
	case 5: case 7: setup_cpl1(mem, cpu); break;
	case 13: setup_cpl1(mem, cpu);   /* the same at level 2 (code 42h, data 4Ah) */
		desc(mem, GDT_PHYS + 0x40, 0, 0xfffff, 0xcda); desc(mem, GDT_PHYS + 0x48, 0, 0xfffff, 0xcd2);
		cpu->gdt.limit = 0x4f;
		for (int s = 0; s < 6; s++) { cpu->seg[s].sel = s == SEG_CS ? 0x42 : 0x4a; cpu->seg[s].flags = 0xcf00 | (s == SEG_CS ? 0xda : 0xd2); }
		cpu->cpl = 2; break;
	case 11: setup_pm16(mem, cpu, 1);   /* 16-bit code with a 32-bit stack (B set) */
		desc(mem, GDT16_PHYS + R_SS, R_SS << 4, 0xffff, 0x492);
		cpu->seg[SEG_SS].flags = 0x4092; cpu->sp_mask = 0xffffffff; break;
	case 12: setup_prot(mem, cpu, 0);   /* 32-bit code with a 16-bit stack at linear 0 */
		desc(mem, GDT_PHYS + 0x40, 0, 0xffff, 0x092); cpu->gdt.limit = 0x47;
		cpu->seg[SEG_SS].sel = 0x40; cpu->seg[SEG_SS].limit = 0xffff; cpu->seg[SEG_SS].flags = 0x0092;
		cpu->sp_mask = 0xffff; REGi(4) = 0xf000; break;
	/* v5.1 (e to j): the frame buffer from 16-bit protected mode without
	   paging and with it (selector A000h, FS loaded with its base); at
	   level 3 with IOPL 3; at level 1 after level 0; from unreal mode; and
	   its dirty bits (the code is in run()) */
	case 14: case 15: setup_pm16(mem, cpu, mode == 15);
		desc(mem, GDT16_PHYS + 0xa000, LFB_BASE, 0xffff, 0x092);
		cpu->seg[SEG_FS].base = LFB_BASE; break;
	case 16: setup_prot(mem, cpu, 1);
		put32(mem, PD_PHYS + ((LFB_BASE >> 22) * 4), PTL_PHYS | 0x007);
		for (int i = 0; i < LFB_SIZE / 4096 + 2; i++)
			put32(mem, PTL_PHYS + i * 4, (LFB_BASE + i * 0x1000) | 0x007);
		cpu->flags |= (3 << 12) | IF; {
		/* INT 30h's handler (level 0) uses the frame buffer too: PUSH EAX;
		   MOV [E0000020h],EAX; MOV EAX,[E0001000h]; MOV [E0002000h],EAX;
		   POP EAX; INC DWORD [CNT+4]; IRET */
		u8 *h = mem + HND_PHYS + 0x240;
		const u8 c[] = { 0x50, 0xa3, 0x20, 0, 0, 0xe0, 0xa1, 0, 0x10, 0, 0xe0, 0xa3, 0, 0x20, 0, 0xe0,
				 0x58, 0xff, 0x05, 0, 0, 0, 0, 0xcf };
		memcpy(h, c, sizeof(c));
		put32(mem, HND_PHYS + 0x240 + 19, CNT_PHYS + 4);
		gate(mem, 0x30, 0x08, HND_PHYS + 0x240, 0xee);
		} break;
	case 17: setup_cpl1(mem, cpu); {
		/* the IRQ handler stores into the frame buffer first (A3: MOV [E0000010h],EAX) */
		u8 *h = mem + HND_PHYS + 0x200;
		const u8 irq[] = { 0xa3, 0x10, 0, 0, 0xe0, 0xff, 0x05, 0, 0, 0, 0, 0xcf };
		memcpy(h, irq, sizeof(irq));
		put32(mem, HND_PHYS + 0x200 + 7, CNT_PHYS);
		gate(mem, 0x20, 0x08, HND_PHYS + 0x200, 0x8e);
		} break;
	case 18: setup_real(mem, cpu); cpu->seg[SEG_FS].base = LFB_BASE; break;
	case 19: setup_prot(mem, cpu, 0); break;
	default: setup_pm16(mem, cpu, 0); break;
	}
	if (mode == 7 || mode == 13 || mode == 17)     /* interrupts on, so that the IRQ's handler (level 0) runs first */
		cpu->flags |= IF;
	if (mode == 9) {   /* the first data page read-only (user), WP clear */
		put32(mem, PT2_PHYS + (((P_DATA >> 12) + 0) & 1023) * 4, P_DATA_PHYS | 0x005);
		cpu->cr0 &= ~CR0_WP;
		tlb_clear(cpu);
	}
}
static int run(int mode, int seed)
{
	static const char *const names[] = { "real mode", "protected mode", "level 3", "V86 mode",
					      "16-bit protected mode", "level 1", "16-bit protected mode, no paging",
					      "level 1 after level 0 (hand)", "self-modifying code (hand)",
					      "CR0.WP set after a store (hand)", "V86 mode, IOPL 0 (#GP ends it)",
					      "16-bit protected mode, 32-bit stack", "32-bit protected mode, 16-bit stack",
					      "level 2 after level 0 (hand)",
					      "16-bit PM no paging, A000h/FS the frame buffer", "16-bit PM paging, A000h/FS the frame buffer",
					      "level 3 IOPL 3 with the frame buffer", "level 1 stores into a frame buffer page level 0 stored into (hand)",
					      "real mode, FS's base the frame buffer (unreal)", "the frame buffer's dirty bits (hand)" };
	glfb = seed & 1;
	for (int i = 0; i < 2; i++)
		mach_new(&M[i], i == 0);
	rs = 0x9e3779b97f4a7c15ull ^ ((uint64_t) seed * 0x100000001b3ull) ^ mode;
	for (int k = 0; k < 4; k++) rnd(0);
	gm = M[0].mem;
	g32 = mode == 1 || mode == 2 || mode == 5 || mode == 7 || mode == 9 || mode == 12 || mode == 13 || mode == 16 || mode == 17 || mode == 19;
	guser = mode == 2 || mode == 16;
	glfbuser = mode == 16;
	gv86 = mode == 3 || mode == 10;
	gpm16 = mode == 4 || mode == 6 || mode == 11 || mode == 14 || mode == 15;
	gcpl1 = mode == 5 || mode == 7 || mode == 13 || mode == 17;
	gnopriv = gcpl1;
	gcs_far = mode == 1 || mode == 9 || mode == 12 || mode == 19 ? 0x08 : mode == 2 || mode == 16 ? 0x1b : mode == 13 ? 0x42 : gcpl1 ? 0x31 : R_CS;
	setup_mode(mode, M[0].mem, M[0].cpu);
	if (!g32) {
		gcode_phys = R_CS << 4;
		gbase_lin = 0;   /* (IP offsets: glin() - gbase_lin) */
	} else {
		gcode_phys = 0x100000;
		gbase_lin = P_CODE;
	}
	/* random data, then the code: a run of random instructions up to
	   about 60 KiB, then JMP $ */
	for (uword a = 0; a < 0x8000; a++)
		M[0].mem[(g32 ? P_DATA_PHYS : (R_DS << 4)) + a] = rnd(256);
	if (!g32)
		for (uword a = 0; a < 0x8000; a++)
			M[0].mem[(R_DS << 4) + 0x8000 + a] = rnd(256);
	gp = gcode_phys;
	if (mode == 17) {
		/* MOV [E0000010h],EAX at level 1 (a supervisor page of the frame buffer) */
		e8(0xa3); e32(LFB_BASE + 0x10);
	} else if (mode == 19) {
		/* v5.1: the frame buffer's dirty bits.  For each of its 16
		   pages: MOV [page],EAX (the first store, through tiny386's
		   store: the entry made, D set); AND its PTE with ~60h (A and D
		   cleared, no INVLPG); MOV [page+8],EAX (through the entry's
		   FAST_TAG_LFB: D set again, as tiny386 sets it); the PTE cleared
		   again; MOV EDI,page+16; STOSD; cleared again; MOV [page+20],AX */
		e8(0xb8); e32(0x12345678);
		for (int k = 0; k < 16; k++) {
			uword pgl = LFB_BASE + k * 0x1000, pte = P_PT + 0x3000 + k * 4;
			e8(0xa3); e32(pgl);
			e8(0x81); e8(0x25); e32(pte); e32(0xffffff9f);
			e8(0xa3); e32(pgl + 8);
			e8(0x81); e8(0x25); e32(pte); e32(0xffffff9f);
			e8(0xbf); e32(pgl + 16); e8(0xab);
			e8(0x81); e8(0x25); e32(pte); e32(0xffffff9f);
			e8(0x66); e8(0x89); e8(0x05); e32(pgl + 20);
		}
		e8(0xbf); e32(P_DATA);
	} else if (mode == 7 || mode == 13) {
		/* MOV EAX,[CNT] (a supervisor page, which the IRQ handler at
		   level 0 has just written); then JMP $ */
		e8(0xa1); e32(CNT_PHYS);
	} else if (mode == 8) {
		/* MOV BYTE CS:[7],42h (the immediate of the MOV AL after it);
		   MOV AL,0; PUSH CS; POP ES; MOV DI,18h; MOV CX,8; MOV AH,90h;
		   XCHG AL,AH; REP STOSB (NOPs over the HLTs at 18h); XCHG AL,AH;
		   JMP +0; 8 x HLT; then JMP $ */
		const u8 c[] = { 0x2e, 0xc6, 0x06, 0x07, 0x00, 0x42, 0xb0, 0x00, 0x0e, 0x07,
				 0xbf, 0x18, 0x00, 0xb9, 0x08, 0x00, 0xb4, 0x90, 0x86, 0xc4,
				 0xf3, 0xaa, 0x86, 0xc4, 0xeb, 0x00,
				 0xf4, 0xf4, 0xf4, 0xf4, 0xf4, 0xf4, 0xf4, 0xf4 };
		/* the HLTs must start at 18h: pad the JMP's distance */
		memcpy(gm + gp, c, sizeof(c));
		gp += sizeof(c);
	} else if (mode == 9) {
		/* MOV [P_DATA],11111111h (allowed: WP clear); MOV EAX,CR0;
		   OR EAX,10000h; MOV CR0,EAX; MOV [P_DATA+4],22222222h (a page
		   fault now, again and again) */
		e8(0xc7); e8(0x05); e32(P_DATA); e32(0x11111111);
		e8(0x0f); e8(0x20); e8(0xc0);
		e8(0x0d); e32(0x10000);
		e8(0x0f); e8(0x22); e8(0xc0);
		e8(0xc7); e8(0x05); e32(P_DATA + 4); e32(0x22222222);
	} else {
		uword limit = gcode_phys + 0xf000;
		while (gp < limit)
			one();
	}
	uword end = gp;
	e8(0xeb); e8(0xfe);
	/* machine 1 the same (CPU and memory) */
	memcpy(M[1].mem, M[0].mem, MEM);
	CPUI386 *a = M[0].cpu, *b = M[1].cpu;
	setup_mode(mode, M[1].mem, b);
	for (int i = 0; i < 2; i++) {
		M[i].vec = (mode == 0 || mode == 8 || mode == 18) ? 8 : 0x20;
		M[i].cpu->cycle = 0;
	}
	uword end_ip = g32 ? end - gcode_phys + P_CODE : end - gcode_phys;
	u16 end_cs = gcs_far;
	long step = 0;
	int bad = 0;
	char why[96] = "the step limit";
	/* the hand-written cases (7, 8, 9, d, h, j) take a few instructions a
	   step and end by 200 steps; the random code ends by 50,000 */
	int hand = (mode >= 7 && mode <= 9) || mode == 13 || mode == 17 || mode == 19;
	long steps_max = hand ? 200 : 50000;
	crash_name = names[mode];
	crash_seed = seed;
	while (step < steps_max) {
		crash_step = step;
		int n = chance(4) ? 1 + rnd(8) : 1 + rnd(700);
		if (hand)
			n = 1 + rnd(3);
		if (((mode == 7 || mode == 13 || mode == 17) && step == 0) || ((mode < 7 || (mode >= 10 && mode != 13 && mode != 17 && mode != 19)) && chance(5))) {
			cpui386_raise_irq(a);
			cpui386_raise_irq(b);
		}
		if (glfb && chance(6))
			for (int i = 0; i < 2; i++) {
				memset(M[i].marks, 0, sizeof(M[i].marks));
				M[i].wrote = 0;
			}
		uword from = a->next_ip, from_lin = a->seg[SEG_CS].base + a->next_ip;
		cpui386_step(a, n);
		cpui386_step(b, n);
		step++;
		{
			const char *w = check_fast_tlb();
			if (w) { printf("  FAIL %s seed %d step %ld: fast TLB invariant: %s\n", names[mode], seed, step, w); fails++; bad = 1; break; }
		}
		if (compare(names[mode], seed, step)) {
			bad = 1;
			snprintf(why, sizeof(why), "a difference");
			if (!g32) {
				printf("    the step (%d instructions) started at %08x:", n, (unsigned) from);
				for (int i = 0; i < 16; i++)
					printf(" %02x", M[0].mem[(from_lin + i) & (MEM - 1)]);
				printf("\n");
			}
			for (int i = 0; i < 2; i++) {
				CPUI386 *c = M[i].cpu;
				uword l = c->seg[SEG_CS].base + c->ip;
				printf("    %s: cs %04x cpl %d ip %08x next %08x ax %08x cx %08x dx %08x bx %08x sp %08x bp %08x si %08x di %08x fl %08x cc %d/%x ds %04x es %04x ss %04x cr2 %08x cycle %ld; at ip:",
				       i ? "tiny386" : "fast   ", (unsigned) c->seg[SEG_CS].sel, c->cpl, c->ip, c->next_ip, c->gprx[0].r32, c->gprx[1].r32, c->gprx[2].r32,
				       c->gprx[3].r32, c->gprx[4].r32, c->gprx[5].r32, c->gprx[6].r32, c->gprx[7].r32, c->flags,
				       c->cc.op, c->cc.mask, c->seg[SEG_DS].sel, c->seg[SEG_ES].sel, c->seg[SEG_SS].sel,
				       (unsigned) c->cr2, c->cycle);
				for (int k = 0; k < 12; k++)
					printf(" %02x", M[i].mem[(l + k) & (MEM - 1)]);
				printf("\n");
			}
		}
		if (bad)
			break;
		if (M[0].panic) {
			snprintf(why, sizeof(why), "exception %02x at %04x:%08x", M[0].panic & 0xff,
				 (unsigned) a->seg[SEG_CS].sel, (unsigned) a->ip);
			break;
		}
		if (a->halt) { a->halt = b->halt = false; }
		if (a->next_ip == end_ip && a->seg[SEG_CS].sel == end_cs) {
			snprintf(why, sizeof(why), "the end of the code");
			break;
		}
	}
	if (step >= steps_max)
		snprintf(why, sizeof(why), "the step limit (at %04x:%08x, CR2 %08x)", (unsigned) a->seg[SEG_CS].sel,
			 (unsigned) a->next_ip, (unsigned) a->cr2);
	if (verbose_runs) {
		u32 c0, c1, p21, p2b;
		memcpy(&c0, M[0].mem + CNT_PHYS, 4); memcpy(&c1, M[0].mem + CNT_PHYS + 4, 4);
		memcpy(&p21, M[0].mem + PT_PHYS + 0x21 * 4, 4); memcpy(&p2b, M[0].mem + PT_PHYS + 0x2b * 4, 4);
		printf("  seed %d, %s: %s after %ld steps, %ld instructions, %u port and video memory accesses"
		       " [VM %d cpl %d code16 %d; IRQs %u, INT 30h %u; PTE 21 %03x 2b %03x]\n",
		       seed, names[mode], why, step, a->cycle, M[0].nlog, !!(a->flags & VM), a->cpl, a->code16,
		       c0, c1, p21 & 0xfff, p2b & 0xfff);
	}
	total_insns += a->cycle;
	total_steps += step;
	for (int i = 0; i < 2; i++)
		mach_free(&M[i]);
	return bad;
}

/* v5.1: a store through a stale entry of the fast interpreter's TLB (a
   fault planted there, say) can write outside the guest's memory, the
   frame buffer or its marks, and crash the test itself: the crash is
   reported as a failure of the scenario and step it happened in */
static void on_crash(int sig)
{
	char b[256];
	int n = snprintf(b, sizeof(b), "  FAIL %s seed %d step %ld: the test crashed in this step (signal %d)\n",
			 crash_name, crash_seed, crash_step + 1, sig);
	if (n > 0 && write(1, b, (size_t) n) < 0)
		n = 0;
	_exit(1);
}

int main(int argc, char **argv)
{
	int seeds = argc > 1 ? atoi(argv[1]) : 40;
	int first = argc > 2 ? atoi(argv[2]) : 1;
	/* argv[3]: the modes, a character each (0 to 9, a to j) */
	const char *modes = argc > 3 ? argv[3] : "0123456789abcdefghij";
	int bad = 0, nsc = 0;
	setvbuf(stdout, NULL, _IOLBF, 0);
	signal(SIGSEGV, on_crash);
	signal(SIGBUS, on_crash);
	for (int s = first; s < first + seeds; s++)
		for (const char *m = modes; *m; m++, nsc++)
			bad += run(*m >= 'a' ? *m - 'a' + 10 : *m - '0', s);
	printf("test_fast_cpu: %d scenarios, %ld steps, %ld instructions, %d checks, %d failed\n",
	       nsc, total_steps, total_insns, checks, fails);
	printf("fast TLB invariants: %ld checks, %ld entries, %ld of the frame buffer, %ld of them with a write tag\n", inv_runs, inv_entries, inv_lfb_r, inv_lfb_w);
	return bad ? 1 : 0;
}
