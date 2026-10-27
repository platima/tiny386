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
 * paging at privilege level 0, and the same at level 3: arithmetic and
 * logic on registers and on memory (operands that cross a page), shifts,
 * multiplies and safe divides, bit tests, string instructions with and
 * without REP across pages and segments' ends, the stack, CMP or TEST then
 * JZ or JNZ (tiny386's fusion of the two) anywhere in a page, jumps and
 * calls near and far, INT and IRET, PUSHF and POPF, STI and CLI, loads of
 * segment registers and of SS, port I/O, video memory (the VGA window and
 * a linear frame buffer), pages that are not present until a page fault's
 * handler maps them, the page tables' accessed and dirty bits cleared
 * without INVLPG, INVLPG and MOV CR3, and instructions only tiny386 does.
 * Interrupts are raised between steps and from inside an OUT, at random.
 *
 *   test_fast_cpu [SEEDS [FIRST_SEED]]
 *
 * Built with i386.c included (I386_FAST), so that tiny386's internals can
 * be compared.  Exit status 0 when the two agree at every step.
 */
#define I386_FAST
#include "i386.c"

#include <stdarg.h>

/* == the machines */
#define MEM (2 * 1024 * 1024)
#define LFB_BASE 0xe0000000u
#define LFB_SIZE 0x10000

struct mach {
	CPUI386 *cpu;
	u8 *mem;
	u8 vga[0x20000];        /* behind the window at A0000h */
	u8 lfb[LFB_SIZE];       /* behind E0000000h */
	uint64_t log;           /* a hash of every port and video memory access */
	unsigned nlog;
	int vec;                /* the interrupt the PIC gives */
	int panic;              /* a port F0h write: an exception the guest did not expect */
};
static struct mach M[2];

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
		u16 cs = g32 ? (guser ? 0x1b : 0x08) : R_CS;
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
	case 43: {              /* ports (not at level 3); OUT E0h raises an interrupt there and then */
		if (guser) break;
		switch (rnd(5)) {
		case 0: e8(0xe4 + rnd(2)); e8(0x40); break;
		case 1: e8(0xba); ev(0x41, !g32); e8(0xec + rnd(2)); break;
		case 2: e8(0xe6 + rnd(2)); e8(0x80); break;
		case 3: e8(0xe6); e8(0xe0); break;
		default: e8(0xba); ev(0x80, !g32); e8(0xee + rnd(2)); break;
		}
		break;
	}
	case 44: {              /* STI, CLI (not at level 3) */
		if (guser) break;
		e8(chance(2) ? 0xfb : 0xfa);
		break;
	}
	case 45: {              /* video memory: the VGA window (and in protected mode the frame buffer) */
		if (g32) {
			uword a = chance(2) ? 0xa0000 + rnd(0x1fff0) : LFB_BASE + rnd(LFB_SIZE - 8);
			if (guser) break;
			switch (rnd(4)) {
			case 0: e8(0x88 + rnd(2)); e8(0x05); e32(a); break;         /* MOV [a],r */
			case 1: e8(0x8a + rnd(2)); e8(0x05 | rreg() << 3); e32(a); break;
			case 2: e8(0xc7); e8(0x05); e32(a); e32((unsigned) rs); break;
			default: /* REP STOSD into it */
				e8(0xbf); e32(a & ~3u); e8(0xb9); e32(rnd(20)); e8(0xf3); e8(0xab);
				e8(0xbf); e32(P_DATA);
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
		if (!g32 || guser) break;
		switch (rnd(3)) {
		case 0: e8(0x81); e8(0x25); e32(P_PT + 0x1000 + ((P_DATA >> 12) & 1023) * 4 + rnd(4) * 4); e32(0xffffff9f); break;
		case 1: e8(0x0f); e8(0x01); e8(0x3b); break;            /* INVLPG [EBX] */
		default: e8(0x0f); e8(0x20); e8(0xd8); e8(0x0f); e8(0x22); e8(0xd8); break; /* MOV EAX,CR3; MOV CR3,EAX */
		}
		break;
	}
	case 47: {              /* ARPL (protected mode), LSL, and instructions only tiny386 does */
		if (g32) { if (chance(2)) { e8(0x63); rm(rnd(3), 0); } else { e8(0x0f); e8(0x03); e8(0xc0); } }
		else { static const u8 t[] = { 0x27, 0x2f, 0x37, 0x3f }; e8(t[rnd(4)]); }
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
	for (int i = 0; i < LFB_SIZE / 4096; i++)
		put32(mem, PTL_PHYS + i * 4, (LFB_BASE + i * 0x1000) | 0x003);

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

/* one scenario: mode 0 real, 1 protected at level 0, 2 at level 3 */
static long total_insns, total_steps;
static int verbose_runs = 1;
static int run(int mode, int seed)
{
	static const char *const names[] = { "real mode", "protected mode", "level 3" };
	for (int i = 0; i < 2; i++)
		mach_new(&M[i], i == 0);
	rs = 0x9e3779b97f4a7c15ull ^ ((uint64_t) seed * 0x100000001b3ull) ^ mode;
	for (int k = 0; k < 4; k++) rnd(0);
	gm = M[0].mem;
	g32 = mode != 0;
	guser = mode == 2;
	if (mode == 0) {
		setup_real(M[0].mem, M[0].cpu);
		gcode_phys = R_CS << 4;
		gbase_lin = 0;   /* (IP offsets: glin() - gbase_lin) */
	} else {
		setup_prot(M[0].mem, M[0].cpu, guser);
		gcode_phys = 0x100000;
		gbase_lin = P_CODE;
	}
	/* random data, then the code: a run of random instructions up to
	   about 60 KiB, then JMP $ */
	for (uword a = 0; a < 0x8000; a++)
		M[0].mem[(mode ? P_DATA_PHYS : (R_DS << 4)) + a] = rnd(256);
	if (!mode)
		for (uword a = 0; a < 0x8000; a++)
			M[0].mem[(R_DS << 4) + 0x8000 + a] = rnd(256);
	gp = gcode_phys;
	uword limit = gcode_phys + (mode ? 0xf000 : 0xf000);
	while (gp < limit)
		one();
	uword end = gp;
	e8(0xeb); e8(0xfe);
	/* machine 1 the same (CPU and memory) */
	memcpy(M[1].mem, M[0].mem, MEM);
	CPUI386 *a = M[0].cpu, *b = M[1].cpu;
	if (mode == 0) setup_real(M[1].mem, b); else setup_prot(M[1].mem, b, guser);
	for (int i = 0; i < 2; i++) {
		M[i].vec = mode ? 0x20 : 8;
		M[i].cpu->cycle = 0;
	}
	uword end_ip = mode ? end - gcode_phys + P_CODE : end - gcode_phys;
	long step = 0;
	int bad = 0;
	char why[96] = "the step limit";
	while (step < 50000) {
		int n = chance(4) ? 1 + rnd(8) : 1 + rnd(700);
		if (chance(5)) {
			cpui386_raise_irq(a);
			cpui386_raise_irq(b);
		}
		uword from = a->next_ip, from_lin = a->seg[SEG_CS].base + a->next_ip;
		cpui386_step(a, n);
		cpui386_step(b, n);
		step++;
		if (compare(names[mode], seed, step)) {
			bad = 1;
			snprintf(why, sizeof(why), "a difference");
			if (!mode) {
				printf("    the step (%d instructions) started at %08x:", n, (unsigned) from);
				for (int i = 0; i < 16; i++)
					printf(" %02x", M[0].mem[(from_lin + i) & (MEM - 1)]);
				printf("\n");
			}
			for (int i = 0; i < 2; i++) {
				CPUI386 *c = M[i].cpu;
				uword l = c->seg[SEG_CS].base + c->ip;
				printf("    %s: ip %08x next %08x ax %08x cx %08x dx %08x bx %08x sp %08x bp %08x si %08x di %08x fl %08x cc %d/%x ds %04x es %04x ss %04x; at ip:",
				       i ? "tiny386" : "fast   ", c->ip, c->next_ip, c->gprx[0].r32, c->gprx[1].r32, c->gprx[2].r32,
				       c->gprx[3].r32, c->gprx[4].r32, c->gprx[5].r32, c->gprx[6].r32, c->gprx[7].r32, c->flags,
				       c->cc.op, c->cc.mask, c->seg[SEG_DS].sel, c->seg[SEG_ES].sel, c->seg[SEG_SS].sel);
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
		if (a->next_ip == end_ip && a->seg[SEG_CS].sel == (mode == 0 ? R_CS : mode == 1 ? 0x08 : 0x1b)) {
			snprintf(why, sizeof(why), "the end of the code");
			break;
		}
	}
	if (step >= 50000)
		snprintf(why, sizeof(why), "the step limit (at %04x:%08x, CR2 %08x)", (unsigned) a->seg[SEG_CS].sel,
			 (unsigned) a->next_ip, (unsigned) a->cr2);
	if (verbose_runs)
		printf("  seed %d, %s: %s after %ld steps, %ld instructions, %u port and video memory accesses\n",
		       seed, names[mode], why, step, a->cycle, M[0].nlog);
	total_insns += a->cycle;
	total_steps += step;
	for (int i = 0; i < 2; i++)
		mach_free(&M[i]);
	return bad;
}

int main(int argc, char **argv)
{
	int seeds = argc > 1 ? atoi(argv[1]) : 40;
	int first = argc > 2 ? atoi(argv[2]) : 1;
	int bad = 0;
	setvbuf(stdout, NULL, _IOLBF, 0);
	for (int s = first; s < first + seeds; s++)
		for (int mode = 0; mode < 3; mode++)
			bad += run(mode, s);
	printf("test_fast_cpu: %d scenarios, %ld steps, %ld instructions, %d checks, %d failed\n",
	       seeds * 3, total_steps, total_insns, checks, fails);
	return bad ? 1 : 0;
}
