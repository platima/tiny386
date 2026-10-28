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
 * v5.1 (WI-1: the loop's state in registers, the code window's one
 * bound, new code pages through the fast TLB, its one-compare hit test):
 * mode k, jumps, calls and returns across code pages, landing in the last
 * 16 bytes of the page before (or the one before that) and below them,
 * and forward into the last 16 bytes of the next, in 16 and 32-bit code
 * at levels 0 and 3, with the code's pages mapped out of order; mode l, a
 * page read and then written, where the write must fault (a user
 * read-only page at level 3 or 1, a read-only page at level 0 with
 * CR0.WP set) and where it must not (CR0.WP clear), stores through each
 * kind of store path, accesses through a segment register loaded with a
 * null selector, and code run from the frame buffer.  The frame buffer's
 * host memory is 4 GiB above guest RAM (on a 64-bit host), so that a host
 * address of it taken as a 32-bit physical address lands in RAM.
 *
 * v5.1 (WI-2, the frame buffer's REP paths and the two-operand rule): case j
 * also does REP STOSD and REP MOVSD into each page of the frame buffer (and
 * over two of its pages) with the page's dirty bit cleared without INVLPG
 * between them, OUTs to port E4h (the VGA's BAR) between REP instructions,
 * and ends at level 3 storing by REP into a supervisor page of it, which
 * must fault.  Case m (hand-written, one instruction a step): an instruction
 * with two memory operands whose second is at the first's TLB index and
 * goes to tiny386 (past the end of RAM, a read-only page, the VGA window),
 * with the first's page table entry's accessed bit cleared in both machines
 * before every step, so that a second translation of the first operand
 * shows: MOVS, REP MOVS (from RAM and from the VGA window), CMPS, POP,
 * PUSH and CALL through memory, and CALL FAR through memory in
 * virtual-8086 mode; the seed picks which.
 *
 * v5.1 (WI-2's check): case m also runs, as the first instruction in a
 * page newly cached, a REP string instruction handed to tiny386 part way
 * after a piece whose operand dropped tiny386's entry for the code page
 * (two_op_newpage()); and mode x is random code of instructions with two
 * memory operands on pages that share TLB indices, their page table
 * entries changed without INVLPG, at levels 0, 1 and 3, with every
 * accessed and dirty bit cleared before every step (fz_one()).
 *
 *   test_fast_cpu [SEEDS [FIRST_SEED [MODES]]]
 *
 * MODES: the scenarios for each seed, one character each (run() names
 * them; the default is all of main()'s list).
 *
 * Built with i386.c included (I386_FAST), so that tiny386's internals can
 * be compared.  Exit status 0 when the two agree at every step.
 */
#define I386_FAST
#include "i386.c"

#include <stdarg.h>
#include <signal.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/mman.h>

/* the scenario and step under way, for on_crash() (v5.1) */
static const char *volatile crash_name = "(none)";
static volatile int crash_seed;
static volatile long crash_step;

/* == the machines */
#define MEM (2 * 1024 * 1024)
#define LFB_BASE 0xe0000000u
#define LFB_SIZE 0x10000
#define LFB_MARK_SHIFT 9        /* a mark for each 512 bytes, as VGA_DIRTY_SHIFT */
/* v5.1: where the frame buffer's host memory is, from guest RAM's: 4 GiB
   and this above it (a part of guest RAM that no scenario uses) */
#define LFB_ALIAS 0x1d0000u

struct mach {
	CPUI386 *cpu;
	u8 *mem;
	u8 *blk;                /* v5.1: guest RAM and the frame buffer in one reservation (or NULL) */
	size_t blklen;
	u8 *lfbp;               /* the frame buffer's host memory: in blk, or lfb[] */
	u8 vga[0x20000];        /* behind the window at A0000h */
	u8 lfb[LFB_SIZE];       /* behind E0000000h, when blk could not be had */
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
			cpu_cb_set_lfb(cb, LFB_BASE + 0x8000, LFB_SIZE - 0x8000, m->lfbp + 0x8000);
		else
			cpu_cb_set_lfb(cb, LFB_BASE, LFB_SIZE, m->lfbp);
	}
}
static void io_w16(void *o, int port, u16 v) { io_w8(o, port, v); io_w8(o, port + 1, v >> 8); }
static void io_w32(void *o, int port, u32 v) { io_w16(o, port, v); io_w16(o, port + 2, v >> 16); }

static u8 *mmio(struct mach *m, uword a)
{
	if (a >= LFB_BASE && a - LFB_BASE < LFB_SIZE)
		return &m->lfbp[a - LFB_BASE];
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

/* v5.1: guest RAM, and the frame buffer's host memory 4 GiB plus
   LFB_ALIAS above it, in one reservation of address space (only the two
   parts made usable): a host address in the frame buffer that some code
   took as a physical address (32 bits, guest RAM's host address taken
   from it) would then be in RAM, below its end less 4 KiB, which the
   checks after it would pass; so mode l's code in the frame buffer would
   show it.  Where there is no such room (a 32-bit host), the frame buffer
   is the machine's lfb[] as before. */
static int layout_note;
static void mach_mem(struct mach *m)
{
#if UINTPTR_MAX > 0xffffffffu
	size_t len = ((size_t) 1 << 32) + LFB_ALIAS + LFB_SIZE;
	void *b = mmap(NULL, len, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (b != MAP_FAILED) {
		u8 *lfb = (u8 *) b + ((size_t) 1 << 32) + LFB_ALIAS;
		if (mprotect(b, MEM, PROT_READ | PROT_WRITE) == 0 &&
		    mprotect(lfb, LFB_SIZE, PROT_READ | PROT_WRITE) == 0) {
			m->blk = b;
			m->blklen = len;
			m->mem = b;
			m->lfbp = lfb;
			return;
		}
		munmap(b, len);
	}
#endif
	if (!layout_note++)
		printf("  (the frame buffer is not 4 GiB above guest RAM here: mode l's code in it shows less)\n");
	m->mem = calloc(1, MEM);
	m->lfbp = m->lfb;
}

static void mach_new(struct mach *m, int fast)
{
	memset(m, 0, sizeof(*m));
	mach_mem(m);
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
		cpu_cb_set_lfb(cb, LFB_BASE, LFB_SIZE, m->lfbp);
	}
}

static void mach_free(struct mach *m)
{
	cpui386_delete(m->cpu);
	if (m->blk)
		munmap(m->blk, m->blklen);
	else
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

/* == v5.1 (WI-1), mode k: jumps, calls and returns across code pages.
   The code window (i386_fast.h) has one bound: straight-line code only
   moves up, and every jump below the window's start goes through
   JUMP_REL() or JUMP_ABS(), which make the next instruction refill it.
   Here every kind of backward move lands in the page before (or the one
   before that): in its last 16 bytes, which tiny386 runs, or below them,
   from a page the fast interpreter caches anew; a fault there runs the
   wrong bytes, since the pages are mapped out of order (linear page i at
   physical page 5 i + 3 mod 16).  Sixteen pages, each:
     0     JMP (E)SI, the "dispatcher" a tail runs into (page 0: JMP to 20h)
     2     INC (E)DX; RET
     4     INC (E)DX; RET 2 or 4
     20h   the main flow: tests, then a near JMP to the next page's 20h
     FC0h  the tail, run into the next page's 0: one-byte instructions,
           CMP (E)AX,(E)AX; JZ $+2 pairs (fused where tiny386 would fuse
           them, never as the first instruction of a page newly cached),
           and at times a CALL (after a PUSH) of the next page's RET (RET
           n), returning into the last 16 bytes
   The tests: MOV (E)SI to where the dispatcher must go, then JMP short or
   near, Jcc short or near (taken; a JZ or JNZ after CMP fused), LOOP or
   JCXZ, CALL near, JMP or CALL through (E)AX, PUSH and RET, or PUSH, PUSH
   and RET n, into a tail below; a CALL of the next page's RET or RET n
   (which lands back here, below that page's window); a JMP or CALL into
   the next page's last 16 bytes.  By the seed: 32-bit code at level 0 and
   at level 3, 16-bit protected mode at level 0 and at level 3, and
   virtual-8086 mode; paging on; IF set, for the random interrupts. */
#define KPAGES 16
#define KMAIN 0x20
#define KTAIL 0xfc0
#define KTLEN (0x1000 - KTAIL)
static int ksub;                  /* mode k's setting: seed % 5 */
static u8 kkind[KPAGES][KTLEN];   /* each tail byte: 0 a one-byte instruction, 1 a longer one's first, 2 inside one */
static u8 kpair[KPAGES][KTLEN];   /* 1 where a pair's CMP is */
static int kperm(int i) { return (i * 5 + 3) & (KPAGES - 1); }
static uword koff(void) { return gp - gcode_phys; }        /* the offset in CS */
static uword kip(uword off) { return g32 ? P_CODE + off : off; }
static void kat(uword off) { gp = gcode_phys + off; }
static int kisz(void) { return g32 ? 4 : 2; }
static void kimm(uword v) { ev(v, !g32); }
static void krel(uword target) { kimm(target - (koff() + kisz())); }   /* rel16 or rel32 */
static int kfits8(uword target, uword at, int len)   /* a rel8 in an instruction of len bytes at at */
{
	sword d = (sword) (target - (at + len));
	return d >= -128 && d < 128;
}
static void kpatch(uword at, uword v) { uword s = gp; gp = at; kimm(v); gp = s; }

/* a Jcc's condition after CMP (E)AX,b with (E)AX = a, as the CPU has it */
static int kcond(int cc, u32 a, u32 b)
{
	u32 m = g32 ? 0xffffffffu : 0xffff, sb = g32 ? 0x80000000u : 0x8000;
	a &= m;
	b &= m;
	u32 r = (a - b) & m;
	int cf = a < b, zf = r == 0, sf = !!(r & sb), of = !!((a ^ b) & (a ^ r) & sb);
	int pf = !(__builtin_popcount(r & 0xff) & 1), c;
	switch (cc >> 1) {
	case 0: c = of; break;
	case 1: c = cf; break;
	case 2: c = zf; break;
	case 3: c = cf || zf; break;
	case 4: c = sf; break;
	case 5: c = pf; break;
	case 6: c = sf != of; break;
	default: c = zf || sf != of; break;
	}
	return c ^ (cc & 1);
}
static u32 kval(void)
{
	static const u32 v[] = { 0, 1, 2, 0x7f, 0x80, 0xff, 0x7fff, 0x8000, 0xffff, 0x7fffffff, 0x80000000u, 0xffffffffu };
	u32 x = chance(2) ? v[rnd(12)] : (u32) rs;
	return g32 ? x : x & 0xffff;
}

static void ktail(int p)
{
	static const u8 one[] = { 0x90, 0x42, 0x4a, 0x92, 0xf5, 0xfc };   /* NOP, INC/DEC (E)DX, XCHG (E)AX,(E)DX, CMC, CLD */
	u8 *kd = kkind[p], *pr = kpair[p];
	memset(kd, 0, KTLEN);
	memset(pr, 0, KTLEN);
	kat(p * 0x1000 + KTAIL);
	for (int i = 0; i < KTLEN; i++)
		e8(one[rnd(6)]);
	if (p + 1 < KPAGES && rnd(3)) {
		/* CALL the next page's RET, or PUSH (E)AX and CALL its RET n:
		   the return address in the last 16 bytes; a landing may be at
		   the first byte, not inside */
		int retn = chance(2), clen = 1 + kisz();
		int r = KTLEN - 16 + rnd(16), c = r - clen, s = retn ? c - 1 : c;
		kd[s] = 1;
		for (int i = s + 1; i < r; i++)
			kd[i] = 2;
		kat(p * 0x1000 + KTAIL + s);
		if (retn)
			e8(0x50);
		e8(0xe8); krel((p + 1) * 0x1000 + (retn ? 4 : 2));
	}
	for (int k = rnd(5); k > 0; k--) {   /* CMP (E)AX,(E)AX; JZ $+2 */
		int i = rnd(KTLEN - 3);
		if (kd[i] || kd[i + 1] || kd[i + 2] || kd[i + 3])
			continue;
		kd[i] = kd[i + 2] = 1;
		kd[i + 1] = kd[i + 3] = 2;
		pr[i] = 1;
		kat(p * 0x1000 + KTAIL + i);
		e8(0x39); e8(0xc0); e8(0x74); e8(0x00);
	}
}

/* a landing in page q's tail: in its last 16 bytes, or below them (at
   times at a pair's CMP, which is then the first instruction run in a
   page the fast interpreter caches anew) */
static uword kland(int q, int last)
{
	u8 *kd = kkind[q], *pr = kpair[q];
	int i;
	if (!last && chance(3))
		for (int t = 0; t < 20; t++) {
			i = rnd(KTLEN - 16);
			if (pr[i])
				return q * 0x1000 + KTAIL + i;
		}
	do
		i = last ? KTLEN - 16 + rnd(16) : rnd(KTLEN - 16);
	while (kd[i] == 2);
	return q * 0x1000 + KTAIL + i;
}

static void ktest(int p)
{
	int isz = kisz(), k;
	uword at = koff(), t = 0, cpos = 0;
	if (p == 0)
		k = 11 + rnd(3);
	else if ((at & 0xfff) < 0x60 && chance(2))
		k = rnd(4);   /* the short forms, which reach the page before only from here */
	else
		k = rnd(14);
	if ((k == 11 || k == 12) && p + 1 >= KPAGES)
		k = 4;
	if (k == 13 && p + 2 >= KPAGES)
		k = 4;
	int q = p >= 2 && chance(4) ? p - 2 : p - 1;
	if (k <= 10)
		t = kland(q, chance(2));
	if (k == 0 && !kfits8(t, at + 1 + isz, 2))
		k = 4;
	if ((k == 1 || k == 3) && !kfits8(t, at + 3 * (1 + isz), 2))
		k = 5;
	if (k == 2 && !kfits8(t, at + 2 * (1 + isz), 2))
		k = 4;
	switch (k) {
	case 0:   /* JMP short back */
		e8(0xbe); cpos = gp; kimm(0);
		e8(0xeb); e8((u8) (t - (koff() + 1)));
		break;
	case 1: case 3: {   /* Jcc short back, taken (3: JZ or JNZ, fused with the CMP) */
		u32 a = kval(), b = k == 3 ? (chance(2) ? a : kval()) : chance(3) ? a : chance(2) ? a + 1 - 2 * rnd(2) : kval();
		int cc = k == 3 ? 4 : rnd(16);
		if (!kcond(cc, a, b))
			cc ^= 1;
		e8(0xbe); cpos = gp; kimm(0);
		e8(0xb8); kimm(a);
		e8(0x3d); kimm(b);
		e8(0x70 + cc); e8((u8) (t - (koff() + 1)));
		break;
	}
	case 2: {   /* LOOP (CX 2) or JCXZ (CX 0) back */
		int z = chance(3);
		e8(0xbe); cpos = gp; kimm(0);
		e8(0xb9); kimm(z ? 0 : 2);
		e8(z ? 0xe3 : 0xe2); e8((u8) (t - (koff() + 1)));
		break;
	}
	case 4:   /* JMP near back */
		e8(0xbe); cpos = gp; kimm(0);
		e8(0xe9); krel(t);
		break;
	case 5: {   /* Jcc near back, taken */
		u32 a = kval(), b = chance(3) ? a : kval();
		int cc = rnd(16);
		if (!kcond(cc, a, b))
			cc ^= 1;
		e8(0xbe); cpos = gp; kimm(0);
		e8(0xb8); kimm(a);
		e8(0x3d); kimm(b);
		e8(0x0f); e8(0x80 + cc); krel(t);
		break;
	}
	case 6:   /* CALL near back: the dispatcher goes to this page's RET */
		e8(0xbe); kimm(kip(p * 0x1000 + 2));
		e8(0xe8); krel(t);
		break;
	case 7:   /* JMP (E)AX back */
		e8(0xbe); cpos = gp; kimm(0);
		e8(0xb8); kimm(kip(t));
		e8(0xff); e8(0xe0);
		break;
	case 8:   /* CALL (E)AX back */
		e8(0xbe); kimm(kip(p * 0x1000 + 2));
		e8(0xb8); kimm(kip(t));
		e8(0xff); e8(0xd0);
		break;
	case 9:   /* PUSH t; RET */
		e8(0xbe); cpos = gp; kimm(0);
		e8(0x68); kimm(kip(t));
		e8(0xc3);
		break;
	case 10:  /* PUSH x; PUSH t; RET 2 or 4 */
		e8(0xbe); cpos = gp; kimm(0);
		e8(0x68); kimm((u32) rs);
		e8(0x68); kimm(kip(t));
		e8(0xc2); e16(isz);
		break;
	case 11:  /* CALL the next page's RET: it lands here, below that page's window */
		e8(0xe8); krel((p + 1) * 0x1000 + 2);
		break;
	case 12:  /* PUSH (E)AX; CALL the next page's RET 2 or 4 */
		e8(0x50);
		e8(0xe8); krel((p + 1) * 0x1000 + 4);
		break;
	default:  /* 13: JMP, or CALL, forward into the next page's last 16 bytes */
		t = kland(p + 1, 1);
		if (chance(2)) {
			e8(0xbe); cpos = gp; kimm(0);
			e8(0xe9); krel(t);
		} else {
			e8(0xbe); kimm(kip(p * 0x1000 + 2));
			e8(0xe8); krel(t);
		}
		break;
	}
	if (cpos)
		kpatch(cpos, kip(koff()));
}

static void gen_k(void)
{
	memset(gm + gcode_phys, 0xf4, KPAGES * 0x1000);   /* HLT where nothing should run */
	for (int p = 0; p < KPAGES; p++) {
		kat(p * 0x1000);
		if (p == 0) {
			e8(0xeb); e8(KMAIN - 2);                /* the start: JMP to the main flow */
		} else {
			e8(0xff); e8(0xe6);                     /* JMP (E)SI */
		}
		e8(0x42); e8(0xc3);                             /* 2: INC (E)DX; RET */
		e8(0x42); e8(0xc2); e16(kisz());                /* 4: INC (E)DX; RET 2 or 4 */
		ktail(p);
	}
	for (int p = 0; p < KPAGES; p++) {
		kat(p * 0x1000 + KMAIN);
		e8(0x39); e8(0xc0); e8(0x74); e8(0x00);         /* CMP (E)AX,(E)AX; JZ $+2 first */
		for (int n = 4 + rnd(12); n > 0 && (koff() & 0xfff) < KTAIL - 0x40; n--)
			ktest(p);
		if (p + 1 < KPAGES) {
			e8(0xe9); krel((p + 1) * 0x1000 + KMAIN);
		}
	}
	/* (run() puts the JMP $ here, in page 15, then kscatter()) */
}

/* the code's linear pages to their physical pages (setup_mode() maps them so) */
static void kscatter(void)
{
	static u8 buf[KPAGES * 0x1000];
	memcpy(buf, gm + gcode_phys, sizeof(buf));
	for (int i = 0; i < KPAGES; i++)
		memcpy(gm + gcode_phys + kperm(i) * 0x1000, buf + i * 0x1000, 0x1000);
}

static void setup_k(u8 *mem, CPUI386 *cpu)
{
	if (ksub < 2)
		setup_prot(mem, cpu, ksub);
	else if (ksub == 3)
		setup_v86(mem, cpu);
	else
		setup_pm16(mem, cpu, 1);
	if (ksub == 4) {   /* 16-bit protected mode at level 3 */
		desc(mem, GDT16_PHYS + R_CS, R_CS << 4, 0xffff, 0x0fa);
		desc(mem, GDT16_PHYS + R_DS, R_DS << 4, 0xffff, 0x0f2);
		desc(mem, GDT16_PHYS + R_SS, R_SS << 4, 0xffff, 0x0f2);
		desc(mem, GDT16_PHYS + 0xa000, 0xa0000, 0xffff, 0x0f2);
		for (int i = 0; i < 1024; i++)
			put32(mem, PT_PHYS + i * 4, (i << 12) | 0x007);
		static const u16 sel[6] = { R_DS, R_CS, R_SS, R_DS, 0xa000, R_DS };
		for (int s = 0; s < 6; s++) {
			cpu->seg[s].sel = sel[s] | 3;
			cpu->seg[s].flags = s == SEG_CS ? 0xfa : 0xf2;
		}
		cpu->cpl = 3;
	}
	for (int i = 0; i < KPAGES; i++) {   /* the code's pages out of order */
		if (ksub < 2)
			put32(mem, PT2_PHYS + (((P_CODE >> 12) + i) & 1023) * 4, (0x100000 + kperm(i) * 0x1000) | 0x007);
		else
			put32(mem, PT_PHYS + ((R_CS >> 8) + i) * 4, ((R_CS << 4) + kperm(i) * 0x1000) | (ksub == 2 ? 0x003 : 0x007));
	}
	tlb_clear(cpu);
	cpu->flags |= IF;
}

/* == v5.1 (WI-1), mode l: a page read, then written.  The read makes the
   page's entries (tiny386's TLB and the fast interpreter's read entry);
   the store must still be checked for writing: a user read-only page at
   level 3 or 1 faults, and so does a read-only page at level 0 with
   CR0.WP set; with CR0.WP clear level 0 writes it.  The stores take each
   path (MOV of a register, an immediate, by its offset, 8 to 32 bits, and
   one STOS: FPTR_ST(); ADD, INC, XCHG, SHL, MOVS: FPTR(); REP STOS:
   FPTR_IO()), at times across into the next page, and at level 0 with
   the page's PTE changed in between without INVLPG (its accessed and
   dirty bits cleared, or the page made writable: tiny386's entry still
   refuses the store, which faults once and is then let through by the
   handler's INVLPG), or CR0.WP changed.  The page fault's handler makes the page writable (INVLPG,
   counted); at times a segment register is loaded with a null selector
   and the page read or written through it (#GP, whose handler loads it
   again); with the frame buffer, code run from it (each instruction by
   tiny386).  By the seed: level 3, level 0 with CR0.WP set and clear,
   level 1.  The rights of the 16 pages from the seed too. */
#define LPAGES 16
#define L_LIN 0x00540000u       /* the 16 pages (linear), from physical 190000h */
#define L_PHYS 0x190000u
static int lsub;                /* mode l's setting: (seed / 2) % 4, so that each has odd seeds (the frame buffer) and even */
static u32 lright[LPAGES];      /* each page's PTE rights */
static void lrights(int seed)
{
	uint64_t h = 0x243f6a8885a308d3ull ^ ((uint64_t) seed * 0x100000001b3ull);
	for (int i = 0; i < LPAGES; i++) {
		h ^= h << 13; h ^= h >> 7; h ^= h << 17;
		unsigned v = (h >> 20) & 7;
		if (lsub == 0 || lsub == 3)   /* levels 1 to 3: user pages, most read-only */
			lright[i] = v < 6 ? 0x005 : 0x007;
		else
			lright[i] = v < 3 ? 0x001 : v < 6 ? 0x005 : v == 6 ? 0x003 : 0x007;
	}
}

static void setup_l(u8 *mem, CPUI386 *cpu)
{
	if (lsub == 3)
		setup_cpl1(mem, cpu);
	else
		setup_prot(mem, cpu, lsub == 0);
	if (lsub == 1)
		cpu->cr0 |= CR0_WP;
	for (int i = 0; i < LPAGES; i++)
		put32(mem, PT2_PHYS + (((L_LIN >> 12) + i) & 1023) * 4, (L_PHYS + i * 0x1000) | lright[i]);
	if (lsub == 0 || lsub == 3) {   /* the frame buffer's pages user */
		put32(mem, PD_PHYS + ((LFB_BASE >> 22) * 4), PTL_PHYS | 0x007);
		for (int i = 0; i < LFB_SIZE / 4096 + 2; i++)
			put32(mem, PTL_PHYS + i * 4, (LFB_BASE + i * 0x1000) | 0x007);
	}
	/* page faults: PUSH EAX; MOV EAX,CR2; SHR EAX,12; AND EAX,3FFh;
	   OR DWORD [P_PT+1000h+EAX*4],2 (writable); MOV EAX,CR2; INVLPG [EAX];
	   INC DWORD [CNT+8]; POP EAX; ADD ESP,4; IRET */
	static const u8 pf[] = { 0x50, 0x0f, 0x20, 0xd0, 0xc1, 0xe8, 0x0c, 0x25, 0xff, 0x03, 0, 0,
				 0x81, 0x0c, 0x85, 0, 0, 0, 0, 2, 0, 0, 0, 0x0f, 0x20, 0xd0, 0x0f, 0x01, 0x38,
				 0xff, 0x05, 0, 0, 0, 0, 0x58, 0x83, 0xc4, 0x04, 0xcf };
	memcpy(mem + HND_PHYS + 0x280, pf, sizeof(pf));
	put32(mem, HND_PHYS + 0x280 + 15, P_PT + 0x1000);
	put32(mem, HND_PHYS + 0x280 + 31, CNT_PHYS + 8);
	/* #GP: PUSH EAX; MOV AX,data; MOV ES,AX; MOV FS,AX; MOV GS,AX;
	   INC DWORD [CNT+12]; POP EAX; ADD ESP,4; IRET */
	u8 gph[] = { 0x50, 0x66, 0xb8, lsub == 0 ? 0x23 : lsub == 3 ? 0x39 : 0x10, 0, 0x8e, 0xc0, 0x8e, 0xe0,
		     0x8e, 0xe8, 0xff, 0x05, 0, 0, 0, 0, 0x58, 0x83, 0xc4, 0x04, 0xcf };
	memcpy(mem + HND_PHYS + 0x2c0, gph, sizeof(gph));
	put32(mem, HND_PHYS + 0x2c0 + 13, CNT_PHYS + 12);
	gate(mem, 14, 0x08, HND_PHYS + 0x280, 0x8e);
	gate(mem, 13, 0x08, HND_PHYS + 0x2c0, 0x8e);
	tlb_clear(cpu);
}

static void lstore(uword a)
{
	switch (rnd(12)) {
	case 0: e8(0x89); e8(0x05); e32(a); break;                  /* MOV [a],EAX: FPTR_ST() */
	case 1: e8(0xc7); e8(0x05); e32(a); e32((u32) rs); break;   /* MOV DWORD [a],imm */
	case 2: e8(0xa3); e32(a); break;                            /* MOV [a],EAX by its offset */
	case 3: e8(0x66); e8(0x89); e8(0x05); e32(a); break;        /* MOV [a],AX */
	case 4: e8(0x88); e8(0x05); e32(a); break;                  /* MOV [a],AL */
	case 5: e8(0xbf); e32(a); e8(0xab); break;                  /* STOSD */
	case 6: e8(0x01); e8(0x05); e32(a); break;                  /* ADD [a],EAX: FPTR() */
	case 7: e8(0xff); e8(0x05); e32(a); break;                  /* INC DWORD [a] */
	case 8: e8(0x87); e8(0x05); e32(a); break;                  /* XCHG [a],EAX */
	case 9: e8(0xc1); e8(0x25); e32(a); e8(1 + rnd(31)); break; /* SHL DWORD [a],n */
	case 10:                                                    /* REP STOSD: FPTR_IO() */
		e8(0xbf); e32(a & ~3u); e8(0xb9); e32(1 + rnd(8)); e8(0xf3); e8(0xab);
		break;
	default:                                                    /* MOVSD from the data */
		e8(0xbe); e32(P_DATA + rnd(0x800)); e8(0xbf); e32(a); e8(0xa5);
		break;
	}
}

static void gen_l(void)
{
	int cpl0 = lsub == 1 || lsub == 2;
	for (int i = 0; i < LPAGES; i++) {
		uword p = L_LIN + i * 0x1000, o1 = rnd(0x1000 - 4), o2 = rnd(0x1000 - 0x40);
		if (i + 1 < LPAGES && chance(8))
			o2 = 0x1000 - 1 - rnd(3);   /* across into the next page */
		switch (rnd(5)) {   /* the read */
		case 0: e8(0x8b); e8(0x05); e32(p + o1); break;               /* MOV EAX,[p] */
		case 1: e8(0xa0); e32(p + o1); break;                         /* MOV AL,[p] */
		case 2: e8(0x03); e8(0x05); e32(p + o1); break;               /* ADD EAX,[p] */
		case 3: e8(0x0f); e8(0xb6); e8(0x05); e32(p + o1); break;     /* MOVZX EAX,BYTE [p] */
		default: e8(0x80); e8(0x3d); e32(p + o1); e8(rnd(256)); break; /* CMP BYTE [p],imm */
		}
		if (cpl0) {   /* at level 0 the PTE changed, no INVLPG: */
			uword pte = P_PT + 0x1000 + ((p >> 12) & 1023) * 4;
			switch (rnd(4)) {
			case 0:   /* its accessed and dirty bits cleared */
				e8(0x81); e8(0x25); e32(pte); e32(0xffffff9f);
				break;
			case 1:   /* made writable (tiny386's entry still has it read-only) */
				e8(0x81); e8(0x0d); e32(pte); e32(2);
				break;
			default:
				break;
			}
		}
		lstore(p + o2);
		e8(0x8b); e8(0x15); e32(p + o2);                              /* MOV EDX,[it] */
		if (chance(2))
			lstore(p + rnd(0x1000 - 0x40));                       /* again (the write entry) */
		if (chance(3)) {   /* a null selector in ES, FS or GS, and the page through it */
			static const u8 sr[3] = { 0xc1, 0xe1, 0xe9 }, pfx[3] = { 0x26, 0x64, 0x65 };
			int s = rnd(3);
			uword a = L_LIN + rnd(LPAGES) * 0x1000 + rnd(0x1000 - 4);
			e8(0x8b); e8(0x05); e32(a);                           /* MOV EAX,[a]: its read entry */
			e8(0x31); e8(0xc9);                                   /* XOR ECX,ECX */
			if (chance(2)) { e8(0xb1); e8(rnd(4)); }              /* MOV CL,RPL */
			e8(0x8e); e8(sr[s]);                                  /* MOV ES/FS/GS,CX */
			switch (rnd(4)) {
			case 0: e8(pfx[s]); e8(0x8b); e8(0x05); e32(a); break;   /* MOV EAX,seg:[a] */
			case 1: e8(pfx[s]); e8(0x89); e8(0x05); e32(a); break;   /* MOV seg:[a],EAX */
			case 2: e8(pfx[s]); e8(0x03); e8(0x05); e32(a); break;   /* ADD EAX,seg:[a] */
			default:
				if (s == 0) { e8(0xbf); e32(a); e8(0xab); }       /* STOSD */
				else { e8(pfx[s]); e8(0x01); e8(0x05); e32(a); }  /* ADD seg:[a],EAX */
				break;
			}
		}
		if (cpl0 && chance(4)) {   /* CR0.WP changed */
			e8(0x0f); e8(0x20); e8(0xc0); e8(0x35); e32(CR0_WP); e8(0x0f); e8(0x22); e8(0xc0);
		}
	}
	if (glfb) {   /* code in the frame buffer: INC EDX three times and JMP back, after a read of its page or not */
		int k = rnd(LFB_SIZE / 4096);
		uword at = LFB_BASE + k * 0x1000 + rnd(0xfc0);
		if (chance(2)) { e8(0x8b); e8(0x05); e32(LFB_BASE + k * 0x1000 + rnd(0xffc)); }
		e8(0xe9); e32(at - (glin() + 4));
		uword back = glin(), rel = back - (at + 8);
		u8 *s = M[0].lfbp + (at - LFB_BASE);
		s[0] = s[1] = s[2] = 0x42;
		s[3] = 0xe9;
		memcpy(s + 4, &rel, 4);
	}
	e8(0xbf); e32(P_DATA);
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
	if (!why[0] && (memcmp(M[0].vga, M[1].vga, sizeof(M[0].vga)) || memcmp(M[0].lfbp, M[1].lfbp, LFB_SIZE)))
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

/* == v5.1 (WI-2): the frame buffer's REP paths, and the two-operand rule */

/* case j, after its single stores: REP STOSD and REP MOVSD into each page
   of the frame buffer (through the write tag once the page has one), the
   page's A and D cleared without INVLPG between them, one MOVSD, and a REP
   STOSD over two pages after both were cleared (two pieces); then OUTs to
   port E4h (the VGA's BAR: odd seeds move or take away the frame buffer)
   between REP instructions; then level 3 (IRETD), storing by REP into a
   supervisor page of the frame buffer, which must fault, again and again
   (the page fault handler cannot map it) */
static void frame_buffer_reps(int seed)
{
	for (int k = 0; k < 16; k++) {
		uword pgl = LFB_BASE + k * 0x1000, pte = P_PT + 0x3000 + k * 4;
		uword other = LFB_BASE + ((k + 5) & 15) * 0x1000;
		e8(0xb8); e32(0x01020304 * (k + 1));                                   /* MOV EAX,n */
		e8(0xbf); e32(pgl + 0x100); e8(0xb9); e32(8); e8(0xf3); e8(0xab);      /* REP STOSD */
		e8(0x81); e8(0x25); e32(pte); e32(0xffffff9f);                         /* AND [pte],~60h */
		e8(0xbf); e32(pgl + 0x204); e8(0xb9); e32(9); e8(0xf3); e8(0xab);      /* again */
		e8(0x81); e8(0x25); e32(pte); e32(0xffffff9f);
		e8(0xbe); e32(P_DATA + 0x100); e8(0xbf); e32(pgl + 0x300);
		e8(0xb9); e32(8); e8(0xf3); e8(0xa5);                                  /* REP MOVSD from RAM */
		e8(0x81); e8(0x25); e32(pte); e32(0xffffff9f);
		e8(0xbe); e32(other + 0x400); e8(0xbf); e32(pgl + 0x500);
		e8(0xb9); e32(8); e8(0xf3); e8(0xa5);                                  /* from another page of it */
		e8(0x81); e8(0x25); e32(pte); e32(0xffffff9f);
		e8(0xbe); e32(P_DATA + 0x200); e8(0xbf); e32(pgl + 0x600); e8(0xa5);   /* MOVSD */
		e8(0x81); e8(0x25); e32(pte); e32(0xffffff9f);
		e8(0x66); e8(0xa5);                                                    /* MOVSW */
		if (k < 15) {
			e8(0x81); e8(0x25); e32(pte); e32(0xffffff9f);
			e8(0x81); e8(0x25); e32(pte + 4); e32(0xffffff9f);
			e8(0xbf); e32(pgl + 0xff0); e8(0xb9); e32(8); e8(0xf3); e8(0xab);   /* over two pages */
		}
	}
	/* E4h: 0 takes it away, 2 leaves its upper half (from E0008000h), 1 all */
	static const u8 moves[] = { 0, 2, 1 };
	for (int i = 0; i <= 3; i++) {
		uword o = i * 0x40;
		e8(0xbf); e32(LFB_BASE + 0x3800 + o); e8(0xb9); e32(8); e8(0xf3); e8(0xab);
		e8(0xbf); e32(LFB_BASE + 0x9800 + o); e8(0xb9); e32(8); e8(0xf3); e8(0xab);
		e8(0xbe); e32(P_DATA + 0x300); e8(0xbf); e32(LFB_BASE + 0x9a00 + o);
		e8(0xb9); e32(8); e8(0xf3); e8(0xa5);
		e8(0xbe); e32(LFB_BASE + 0x9a00 + o); e8(0xbf); e32(LFB_BASE + 0x3a00 + o);
		e8(0xb9); e32(8); e8(0xf3); e8(0xa5);
		if (i < 3) { e8(0xb0); e8(moves[i]); e8(0xe6); e8(0xe4); }             /* MOV AL,n; OUT E4h,AL */
	}
	/* level 3: IRETD to the next instruction with CS 1Bh, SS 23h */
	e8(0x6a); e8(0x23);
	e8(0x68); e32(P_USTACK - 0x100);
	e8(0x9c);
	e8(0x6a); e8(0x1b);
	uword at = glin();
	e8(0x68); e32(at + 6);
	e8(0xcf);
	e8(0x66); e8(0xb8); e16(0x23); e8(0x8e); e8(0xd8); e8(0x8e); e8(0xc0);    /* MOV AX,23h; MOV DS,AX; MOV ES,AX */
	e8(0xbf); e32(LFB_BASE + 0x2100); e8(0xb9); e32(8);
	if (seed & 2) { e8(0xbe); e32(P_DATA + 0x100); e8(0xf3); e8(0xa5); }    /* REP MOVSD into it */
	else { e8(0xf3); e8(0xab); }                                              /* REP STOSD into it */
}

/* case m: the two-operand rule.  An instruction looks its first memory
   operand up, then its second; if the second is at the same TLB index and
   the instruction then goes to tiny386, tiny386 must find the first's entry
   still there.  The pages (equal modulo 512, so the same index on the
   boards' 256 entries and the host's 512; away from index 0, the code's):
   S = P_DATA + 2000h (page 502h, RAM), R = 302000h (past the end of RAM),
   O = 102000h (RAM, made read-only with CR0.WP for a fault), the VGA window
   (page A0h) and R2 = 2A0000h (past the end of RAM).  Before every step the
   harness clears the accessed bit of S's and the window's page table
   entries (and of page 101h's in virtual-8086 mode) in both machines
   (two_op_clear()), so that a second translation of the first operand sets
   it in one machine and not the other.  One case a seed.  (Case 10, from
   WI-2's check: two_op_newpage().) */
#define TWO_OP_N 12
static int two_op_v86;
#define TO_S (P_DATA + 0x2000)
#define TO_R 0x302000u
#define TO_O 0x102000u
#define TO_R2 0x2a0000u
static void two_op_wp_on(void)
{
	e8(0x0f); e8(0x20); e8(0xc0); e8(0x0d); e32(0x10000); e8(0x0f); e8(0x22); e8(0xc0);   /* CR0.WP */
}
/* case 10 (WI-2's check, its F1): the first instruction in a page newly
   cached, reached by a JMP (page 401h, TLB index 1 on the boards' 256
   entries and the host's 512), is a REP string instruction whose first
   piece's operand is at the code page's index (page 1h, linear =
   physical: its refill drops the code page's entry) and whose next piece
   goes to tiny386 part way: REP MOVSB and REPNE CMPSB by the two-operand
   rule (the source page 2h and the destination page 402h share index 2),
   and REP STOSB into page 2h, which the guest has pointed past the end of
   RAM (a hand-over v5.0 had already).  tiny386, which fetches the
   instruction again through its TLB, must find the code page's entry
   there, or it walks the code page again and keeps it where tiny386 alone
   keeps page 1h.  Then page 1h's PTE is
   pointed at another frame without INVLPG and a dword is loaded through
   it: tiny386 alone still has the old frame.  The seed picks which of the
   three.  Each then runs, as the first instruction in page 403h, a CMP
   with an MMIO operand (tiny386 gets it) and a JZ after it that is taken:
   tiny386 alone fetched the CMP the slow way and does not fuse the two. */
static void two_op_newpage(int seed)
{
	int v = (seed / TWO_OP_N) % 3;
	put32(gm, 0x1000, 0x11111111);      /* page 1h's frame, and the frame it is pointed at after */
	put32(gm, 0x3000, 0x33333333);
	gm[0x1fff] = 0x5a;                  /* (REPNE CMPSB: its first bytes differ) */
	if (v == 2) {
		e8(0xc7); e8(0x05); e32(PT_PHYS + 2 * 4); e32(0x300000 | 0x003);   /* page 2h past the end of RAM */
		e8(0x0f); e8(0x01); e8(0x3d); e32(0x2000);                          /* INVLPG [2000h] */
	}
	e8(0xbe); e32(0x1fff);                                              /* MOV ESI,1FFFh */
	e8(0xbf); e32(v == 2 ? 0x1fff : P_CODE + 0x2100);                   /* MOV EDI */
	e8(0xb9); e32(4);                                                   /* MOV ECX,4 */
	uword at = glin();
	e8(0xe9); e32(P_CODE + 0x1000 - (at + 5));                          /* JMP 401000h */
	gp = gcode_phys + 0x1000;
	if (v == 0) { e8(0xf3); e8(0xa4); }                                 /* REP MOVSB */
	else if (v == 1) { e8(0xf2); e8(0xa6); }                            /* REPNE CMPSB */
	else { e8(0xf3); e8(0xaa); }                                        /* REP STOSB */
	e8(0xc7); e8(0x05); e32(PT_PHYS + 1 * 4); e32(0x3000 | 0x003);      /* page 1h to 3000h, no INVLPG */
	e8(0xa1); e32(0x1000);                                              /* MOV EAX,[1000h] */
	e8(0xa3); e32(P_DATA + 0x10);                                       /* MOV [P_DATA+10h],EAX */
	/* then a CMP whose operand is MMIO, so that tiny386 gets it, as the
	   first instruction in another page newly cached, with a JZ after it
	   that is taken: tiny386 alone fetched the CMP the slow way and does
	   not fuse the two */
	e8(0x31); e8(0xc0);                                                 /* XOR EAX,EAX */
	at = glin();
	e8(0xe9); e32(P_CODE + 0x3000 - (at + 5));                          /* JMP 403000h */
	gp = gcode_phys + 0x3000;
	e8(0x39); e8(0x05); e32(0xa0000);                                   /* CMP [A0000h],EAX (the VGA window) */
	e8(0x74); e8(0x02);                                                 /* JZ +2 */
	e8(0x43); e8(0x43);                                                 /* INC EBX, twice: jumped over */
}
static void two_op_case(int seed)
{
	int k = seed % TWO_OP_N;
	if (two_op_v86) {
		/* virtual-8086 mode: the far pointer at FFFF:1010h (linear 101000h,
		   page 101h, index 1 on the boards), the stack at 0:1800h (page 1,
		   read-only: the push faults); the pointer is the JMP $ after it
		   (31 bytes on) */
		uword t = gp - gcode_phys + 31;
		e8(0xb8); e16(0xffff); e8(0x8e); e8(0xc0);                         /* MOV AX,FFFFh; MOV ES,AX */
		e8(0x26); e8(0xc7); e8(0x06); e16(0x1010); e16(t);                 /* MOV WORD ES:[1010h],t */
		e8(0x26); e8(0xc7); e8(0x06); e16(0x1012); e16(R_CS);              /* MOV WORD ES:[1012h],R_CS */
		e8(0x31); e8(0xc0); e8(0x8e); e8(0xd0); e8(0xbc); e16(0x1800);     /* XOR AX,AX; MOV SS,AX; MOV SP,1800h */
		e8(0x26); e8(0xff); e8(0x1e); e16(0x1010);                         /* CALL FAR ES:[1010h] */
		return;
	}
	e8(0xb8); e32(0x12345678);                         /* (the page's first instruction has no operand) */
	if (k == 10) {
		two_op_newpage(seed);
		return;
	}
	if (k == 2 || k == 3) {
		/* a write fault: S read-only (2), or O (3), with CR0.WP; the
		   MOV CR0 empties tiny386's TLB */
		if (k == 2) { e8(0x81); e8(0x25); e32(P_PT + 0x1000 + ((TO_S >> 12) & 1023) * 4); e32(~2u); }
		else { e8(0x81); e8(0x25); e32(PT_PHYS + (TO_O >> 12) * 4); e32(~2u); }
		two_op_wp_on();
	}
	if (k >= 8) { e8(0xa1); e32(0xa0000); }            /* MOV EAX,[A0000h]: the window's page in the TLB */
	else { e8(0xa1); e32(TO_S + 0x100); }              /* MOV EAX,[S+100h] */
	switch (k) {
	case 0: /* MOVSD S -> R */
		e8(0xbe); e32(TO_S + 0x100); e8(0xbf); e32(TO_R + 0x100); e8(0xa5); break;
	case 1: /* REP MOVSD S -> R */
		e8(0xbe); e32(TO_S + 0x100); e8(0xbf); e32(TO_R + 0x100); e8(0xb9); e32(8); e8(0xf3); e8(0xa5); break;
	case 2: /* MOVSD within S, read-only: the same page, a write fault */
		e8(0xbe); e32(TO_S + 0x100); e8(0xbf); e32(TO_S + 0x200); e8(0xa5); break;
	case 3: /* MOVSD S -> O, read-only: a refill, then a write fault */
		e8(0xbe); e32(TO_S + 0x100); e8(0xbf); e32(TO_O + 0x100); e8(0xa5); break;
	case 4: /* CMPSD S, R */
		e8(0xbe); e32(TO_S + 0x100); e8(0xbf); e32(TO_R + 0x100); e8(0xa7); break;
	case 5: /* POP [R], the stack in S */
		e8(0xbc); e32(TO_S + 0x800); e8(0x8f); e8(0x05); e32(TO_R + 0x100);
		e8(0xbc); e32(P_STACK - 0x100); break;
	case 6: /* PUSH [S], the stack in R */
		e8(0xbc); e32(TO_R + 0x800); e8(0xff); e8(0x35); e32(TO_S + 0x100);
		e8(0xbc); e32(P_STACK - 0x100); break;
	case 7: { /* CALL [S], the stack in R: [S+100h] the next instruction */
		uword at = glin();
		e8(0xc7); e8(0x05); e32(TO_S + 0x100); e32(at + 10 + 5 + 6);
		e8(0xbc); e32(TO_R + 0x800);
		e8(0xff); e8(0x15); e32(TO_S + 0x100);
		e8(0xbc); e32(P_STACK - 0x100); break;
	}
	case 8: /* MOVSD from the window -> R2 */
		e8(0xbe); e32(0xa0100); e8(0xbf); e32(TO_R2 + 0x100); e8(0xa5); break;
	default: /* REP MOVSD from the window -> R2 */
		e8(0xbe); e32(0xa0100); e8(0xbf); e32(TO_R2 + 0x100); e8(0xb9); e32(8); e8(0xf3); e8(0xa5); break;
	}
}
/* before each step: the first operands' accessed bits cleared in both
   machines, as a guest clears them without INVLPG */
static void two_op_clear(void)
{
	for (int i = 0; i < 2; i++) {
		u8 *m = M[i].mem;
		if (two_op_v86) {
			m[PT_PHYS + 0x101 * 4] &= ~0x20;
		} else {
			m[PT2_PHYS + ((TO_S >> 12) & 1023) * 4] &= ~0x20;
			m[PT_PHYS + 0xa0 * 4] &= ~0x20;
		}
	}
}

/* == v5.1 (WI-2's check): mode x, two memory operands at random.  Random
   code made of instructions with two memory operands (MOVS and CMPS with
   and without REP, DF either way, bytes, words and dwords, 0 to 1,100
   elements, segment prefixes; PUSH, POP and CALL through memory with the
   stack moved into the pool), REP STOS, LODS, SCAS and single loads and
   stores, on pages that share TLB indices: a pool at 800000h to 805000h,
   900000h, 901000h, 8A0000h and A00000h to A05000h (some aliasing one
   frame, one the VGA window, one the frame buffer, one past the end of
   RAM), P_DATA, the frame buffer, the VGA window, past the end of RAM;
   near a page's end (crossing at times) and start, aligned or not.  Their
   page table entries are changed without INVLPG (A, D, R/W, U/S, P and the
   frame, through a recursive page directory entry, PD[3FFh]); INVLPG, MOV
   CR3 and CR0.WP; OUTs that move the frame buffer.  Level 0, 1 or 3 (seed
   % 3); a page fault handler that makes the page present, writable and
   user.  Before every step every accessed and dirty bit of the page
   directory and tables is cleared in both machines, so that a page walk
   made twice, or a dirty bit set where tiny386 does not, shows at once.
   The code at P_CODE shares TLB indices with the pool (page 401h with
   801h and A01h, and so on). */
#define PT3_PHYS 0x1f4000u      /* the page table for 800000h to BFFFFFh */
#define FZ_PHYS 0x140000u       /* the pool's frames, 64 KiB of random bytes */
static int fz_level;
static const uword fz_pages[] = {
	0x800, 0x801, 0x802, 0x803, 0x804, 0x805,
	0xa00, 0xa01, 0xa02, 0xa03, 0xa04, 0xa05,
	0x900, 0x901, 0x8a0,
	0x500, 0x501, 0x502, 0x503,
	0xe0001, 0xe0002, 0xe0003, 0xe0004,
	0xa1, 0xa2, 0xa4,
	0x2a0, 0x300,
};
#define FZ_N ((unsigned) (sizeof(fz_pages) / sizeof(fz_pages[0])))
static int fz_pool(uword pg) { return pg >= 0x800 && pg < 0xb00; }
static uword fz_pte(uword pg) { return 0xffc00000u + pg * 4; }
static uword fz_page(void) { return fz_pages[rnd(FZ_N)]; }
/* a stack page: at level 0 one that never faults (an exception pushes onto it) */
static uword fz_stack_page(void)
{
	static const uword safe[] = { 0x500, 0xa1, 0xa2, 0xe0001, 0xe0002 };
	return fz_level ? fz_page() : safe[rnd(5)];
}
/* memory that reads back what was stored (for CALL's pointer) */
static uword fz_keep_page(void)
{
	static const uword keep[] = { 0x500, 0x501, 0x502, 0x503, 0xa1, 0xa2, 0xa4, 0xe0001, 0xe0002, 0xe0003, 0xe0004 };
	return keep[rnd(11)];
}
static uword fz_addr(uword pg, int size)
{
	uword off;
	switch (rnd(5)) {
	case 0: off = 0x1000 - size * (1 + rnd(3)) + (chance(3) ? rnd(size) : 0); break;   /* the end (crossing at times) */
	case 1: off = rnd(16) & (chance(3) ? ~0u : ~(uword) (size - 1)); break;           /* the start */
	default: off = rnd(0x1000 - 8) & (chance(4) ? ~0u : ~(uword) (size - 1)); break;
	}
	return (pg << 12) + off;
}
static uword fz_frame(void)
{
	switch (rnd(6)) {
	case 0: case 1: return FZ_PHYS + rnd(16) * 0x1000;
	case 2: return P_DATA_PHYS + rnd(4) * 0x1000;
	case 3: return LFB_BASE + rnd(16) * 0x1000;
	case 4: return 0xa0000 + rnd(32) * 0x1000;
	default: return 0x300000 + rnd(4) * 0x1000;
	}
}
static void fz_one(void)
{
	int r = rnd(100);
	if (r < 40) {   /* a string instruction, with or without REP */
		static const u8 ops[] = { 0xa4, 0xa5, 0xa4, 0xa5, 0xaa, 0xab, 0xaa, 0xab, 0xa6, 0xa7, 0xac, 0xad, 0xae, 0xaf };
		u8 op = ops[rnd(sizeof(ops))];
		int o16 = (op & 1) && chance(3);
		int size = !(op & 1) ? 1 : o16 ? 2 : 4;
		uword s = fz_addr(fz_page(), size), d = fz_addr(fz_page(), size);
		if (chance(5)) d = (s & ~0xfffu) | (d & 0xfff);   /* the same page */
		uword c = chance(3) ? rnd(20) : chance(2) ? rnd(300) : rnd(1100);
		e8(chance(3) ? 0xfd : 0xfc);
		e8(0xbe); e32(s); e8(0xbf); e32(d); e8(0xb9); e32(c);
		if (chance(6)) { static const u8 sp[] = { 0x26, 0x2e, 0x36, 0x3e, 0x64, 0x65 }; e8(sp[rnd(6)]); }
		if (o16) e8(0x66);
		int pr = rnd(4);
		int cmp = op == 0xa6 || op == 0xa7 || op == 0xae || op == 0xaf;
		if (pr) e8(cmp && pr == 3 ? 0xf2 : 0xf3);
		e8(op);
		e8(0xfc);
	} else if (r < 55) {   /* PUSH, POP and CALL through memory, the stack somewhere in the pool */
		uword sp = fz_addr(fz_stack_page(), 4) & ~3u;
		if ((sp & 0xfff) < 8) sp += 8;
		if (fz_level == 0)   /* room below for an exception's frame, in the same page */
			sp = (sp & ~0xfffu) + 0x100 + (rnd(0xe00) & ~3u);
		e8(0x89); e8(0xe5);                                  /* MOV EBP,ESP */
		e8(0xbc); e32(sp);                                   /* MOV ESP,sp */
		uword m = fz_addr(fz_page(), 4);
		switch (rnd(4)) {
		case 0: e8(0xff); e8(0x35); e32(m); break;           /* PUSH DWORD [m] */
		case 1: e8(0x66); e8(0xff); e8(0x35); e32(m); break; /* PUSH WORD [m] */
		case 2: e8(0x8f); e8(0x05); e32(m); break;           /* POP DWORD [m] */
		default: {
			m = fz_addr(fz_keep_page(), 4);
			uword at = glin();
			e8(0xc7); e8(0x05); e32(m); e32(at + 10 + 6);   /* MOV DWORD [m],next */
			e8(0xff); e8(0x15); e32(m);                     /* CALL DWORD [m] */
			break;
		}
		}
		e8(0x89); e8(0xec);                                  /* MOV ESP,EBP */
	} else if (r < 72) {   /* a page table entry changed without INVLPG */
		uword pg = fz_page();
		uword pte = fz_pte(pg);
		int k = rnd(10);
		if (!fz_pool(pg) && k >= 3) k = rnd(3);
		switch (k) {
		case 0: e8(0x81); e8(0x25); e32(pte); e32(~0x20u); break;   /* A */
		case 1: e8(0x81); e8(0x25); e32(pte); e32(~0x40u); break;   /* D */
		case 2: e8(0x81); e8(0x25); e32(pte); e32(~0x60u); break;
		case 3: e8(0x81); e8(0x35); e32(pte); e32(0x02); break;     /* R/W */
		case 4: e8(0x81); e8(0x35); e32(pte); e32(0x04); break;     /* U/S */
		case 5: e8(0x81); e8(0x35); e32(pte); e32(0x06); break;
		case 6: e8(0x81); e8(0x35); e32(pte); e32(0x01); break;     /* P */
		default: e8(0xc7); e8(0x05); e32(pte); e32(fz_frame() | (chance(4) ? 0x5 : 0x7)); break;   /* the frame */
		}
	} else if (r < 77) {   /* INVLPG, MOV CR3, CR0.WP (level 0) */
		if (fz_level == 0) {
			int k = rnd(6);
			if (k < 4) { e8(0x0f); e8(0x01); e8(0x3d); e32(fz_page() << 12); }
			else if (k == 4) { e8(0x0f); e8(0x20); e8(0xd8); e8(0x0f); e8(0x22); e8(0xd8); }
			else { e8(0x0f); e8(0x20); e8(0xc0); e8(0x35); e32(0x10000); e8(0x0f); e8(0x22); e8(0xc0); }
		}
	} else if (r < 79) {   /* the frame buffer moved (port E4h, IOPL 3) */
		e8(0xb0); e8(rnd(4)); e8(0xe6); e8(0xe4);
	} else {   /* one load or store */
		uword m = fz_addr(fz_page(), 4);
		switch (rnd(7)) {
		case 0: e8(0xa3); e32(m); break;                     /* MOV [m],EAX */
		case 1: e8(0xa1); e32(m); break;                     /* MOV EAX,[m] */
		case 2: e8(0x01); e8(0x05); e32(m); break;           /* ADD [m],EAX */
		case 3: e8(0x66); e8(0xa3); e32(m); break;           /* MOV [m],AX */
		case 4: e8(0xa2); e32(m); break;                     /* MOV [m],AL */
		case 5: e8(0x87); e8(0x05); e32(m); break;           /* XCHG [m],EAX */
		default: e8(0xb8); e32((unsigned) rs); break;        /* MOV EAX,n */
		}
	}
}
static void fz_setup(u8 *mem, CPUI386 *cpu)
{
	if (fz_level == 1) setup_cpl1(mem, cpu);
	else setup_prot(mem, cpu, fz_level == 3);
	cpu->flags |= 3 << 12;   /* IOPL 3: OUT E4h at any level */
	put32(mem, PD_PHYS + 2 * 4, PT3_PHYS | 0x007);
	put32(mem, PD_PHYS + 0x380 * 4, PTL_PHYS | 0x007);
	put32(mem, PD_PHYS + 0x3ff * 4, PD_PHYS | 0x007);
	for (int i = 0; i < 1024; i++)
		put32(mem, PT3_PHYS + i * 4, 0);
	for (int k = 0; k < 4; k++) {
		put32(mem, PT3_PHYS + k * 4, (FZ_PHYS + k * 0x1000) | 0x007);                        /* 800h.. */
		put32(mem, PT3_PHYS + (0x200 + k) * 4, (FZ_PHYS + ((k + 1) & 3) * 0x1000) | 0x007);   /* A00h.., aliases */
	}
	put32(mem, PT3_PHYS + 4 * 4, 0xa0000 | 0x007);                        /* 804h: the VGA window */
	put32(mem, PT3_PHYS + 5 * 4, 0x300000 | 0x007);                       /* 805h: past the end of RAM */
	put32(mem, PT3_PHYS + 0x204 * 4, (LFB_BASE + 0x1000) | 0x007);        /* A04h: the frame buffer */
	put32(mem, PT3_PHYS + 0x205 * 4, (FZ_PHYS + 0xd000) | 0x007);
	put32(mem, PT3_PHYS + 0x100 * 4, (FZ_PHYS + 0x8000) | 0x007);         /* 900h */
	put32(mem, PT3_PHYS + 0x101 * 4, (FZ_PHYS + 0x9000) | 0x007);
	put32(mem, PT3_PHYS + 0xa0 * 4, (FZ_PHYS + 0xc000) | 0x007);          /* 8A0h */
	/* page faults: PUSH EAX; MOV EAX,CR2; SHR EAX,12; OR DWORD [FFC00000h + EAX*4],7; POP EAX; ADD ESP,4; IRETD */
	static const u8 pf[] = { 0x50, 0x0f, 0x20, 0xd0, 0xc1, 0xe8, 0x0c, 0x81, 0x0c, 0x85, 0x00, 0x00, 0xc0, 0xff,
				 0x07, 0, 0, 0, 0x58, 0x83, 0xc4, 0x04, 0xcf };
	memcpy(mem + HND_PHYS + 0x300, pf, sizeof(pf));
	gate(mem, 14, 0x08, HND_PHYS + 0x300, 0x8e);
	tlb_clear(cpu);
}
static void fz_clear1(u8 *m, uword table)
{
	for (uword k = 0; k < 1024; k++)
		m[table + k * 4] &= ~0x60;
}
static void fz_clear(void)
{
	for (int i = 0; i < 2; i++) {
		u8 *m = M[i].mem;
		for (int k = 0; k < 1024; k++)
			m[PD_PHYS + k * 4] &= ~0x20;
		fz_clear1(m, PT_PHYS);
		fz_clear1(m, PT2_PHYS);
		fz_clear1(m, PT3_PHYS);
		fz_clear1(m, PTL_PHYS);
	}
}

/* one scenario: mode 0 real, 1 protected at level 0, 2 at level 3;
   since the v5.0 reviews: 3 virtual-8086, 4 16-bit protected with paging, 5
   level 1, 6 16-bit protected without paging, 7 level 1 reading a
   supervisor page that level 0 used (hand-written), 8 self-modifying real
   mode code (hand-written), 9 CR0.WP set after a supervisor store into a
   read-only page (hand-written); v5.1 (WI-1): 20 (k) jumps, calls and
   returns across code pages, 21 (l) a page read, then written */
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
	/* v5.1 (WI-1): k, jumps, calls and returns across code pages */
	case 20: setup_k(mem, cpu); break;
	/* v5.1 (WI-1): l, a page read, then written */
	case 21: setup_l(mem, cpu); break;
	/* v5.1: the two-operand rule (two_op_case()): level 0 with paging, or
	   virtual-8086 mode with page 1 read-only for its stack */
	case 22: if (two_op_v86) {
			setup_v86(mem, cpu);
			put32(mem, PT_PHYS + 1 * 4, 0x1000 | 0x005);
			tlb_clear(cpu);
		} else {
			setup_prot(mem, cpu, 0);
		}
		break;
	case 33: fz_setup(mem, cpu); break;   /* v5.1 (WI-2's check): two memory operands at random */
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
					      "real mode, FS's base the frame buffer (unreal)", "the frame buffer's dirty bits (hand)",
					      "jumps, calls and returns across code pages (k)",
					      "a page read, then written (hand, l)",
					      [22] = "the two-operand rule (hand)",
					      [33] = "two memory operands at random" };
	glfb = seed & 1;
	ksub = seed % 5;   /* (mode k) */
	lsub = (seed >> 1) & 3;   /* (mode l) */
	lrights(seed);
	fz_level = (const int[]) { 0, 1, 3 }[seed % 3];
	for (int i = 0; i < 2; i++)
		mach_new(&M[i], i == 0);
	rs = 0x9e3779b97f4a7c15ull ^ ((uint64_t) seed * 0x100000001b3ull) ^ mode;
	for (int k = 0; k < 4; k++) rnd(0);
	gm = M[0].mem;
	g32 = mode == 1 || mode == 2 || mode == 5 || mode == 7 || mode == 9 || mode == 12 || mode == 13 || mode == 16 || mode == 17 || mode == 19;
	two_op_v86 = mode == 22 && seed % TWO_OP_N == TWO_OP_N - 1;   /* (v5.1) its last case is in virtual-8086 mode */
	g32 |= mode == 22 && !two_op_v86;
	g32 |= mode == 33;
	guser = mode == 2 || mode == 16 || (mode == 33 && fz_level == 3);
	glfbuser = mode == 16;
	gv86 = mode == 3 || mode == 10;
	gpm16 = mode == 4 || mode == 6 || mode == 11 || mode == 14 || mode == 15;
	gcpl1 = mode == 5 || mode == 7 || mode == 13 || mode == 17;
	gnopriv = gcpl1;
	gcs_far = mode == 1 || mode == 9 || mode == 12 || mode == 19 || (mode == 22 && g32) ? 0x08 : mode == 2 || mode == 16 ? 0x1b : mode == 13 ? 0x42 : gcpl1 ? 0x31 : R_CS;
	if (mode == 20) {   /* k: 32-bit at level 0 or 3, 16-bit protected mode at 0, V86 mode, 16-bit protected mode at 3 */
		g32 = ksub < 2;
		guser = ksub == 1 || ksub >= 3;
		gv86 = ksub == 3;
		gpm16 = ksub == 2 || ksub == 4;
		gcs_far = ksub == 0 ? 0x08 : ksub == 1 ? 0x1b : ksub == 4 ? (R_CS | 3) : R_CS;
	}
	if (mode == 21) {   /* l: level 3, level 0 with CR0.WP set and clear, level 1 */
		g32 = 1;
		guser = lsub == 0;
		gcpl1 = gnopriv = lsub == 3;
		gcs_far = lsub == 0 ? 0x1b : lsub == 3 ? 0x31 : 0x08;
	}
	if (mode == 33)
		gcs_far = fz_level == 0 ? 0x08 : fz_level == 1 ? 0x31 : 0x1b;
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
		frame_buffer_reps(seed);
	} else if (mode == 22) {
		two_op_case(seed);
	} else if (mode == 33) {
		for (uword a = 0; a < 0x10000; a++)
			M[0].mem[FZ_PHYS + a] = rnd(256);
		uword limit = gcode_phys + 0xf000;
		while (gp < limit)
			fz_one();
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
	} else if (mode == 20) {
		gen_k();
	} else if (mode == 21) {
		gen_l();
	} else {
		uword limit = gcode_phys + 0xf000;
		while (gp < limit)
			one();
	}
	uword end = gp;
	e8(0xeb); e8(0xfe);
	if (mode == 20)
		kscatter();
	/* machine 1 the same (CPU, memory and the frame buffer) */
	memcpy(M[1].mem, M[0].mem, MEM);
	memcpy(M[1].lfbp, M[0].lfbp, LFB_SIZE);
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
	   step and end by 200 steps (l by 4,000); the random code and k end
	   by 50,000 */
	int hand = (mode >= 7 && mode <= 9) || mode == 13 || mode == 17 || mode == 19 || mode == 21 || mode == 22;
	/* (v5.1: case j's REPs and its level 3 part, about 900 instructions) */
	long steps_max = mode == 21 ? 4000 : mode == 19 ? 800 : hand ? 200 : 50000;
	crash_name = names[mode];
	crash_seed = seed;
	while (step < steps_max) {
		crash_step = step;
		int n = chance(4) ? 1 + rnd(8) : 1 + rnd(700);
		if (hand)
			n = mode == 22 ? 1 : 1 + rnd(3);
		if (mode == 33)   /* (short steps, so that the bits cleared before each show more) */
			n = chance(8) ? 1 + rnd(200) : 1 + rnd(6);
		if (((mode == 7 || mode == 13 || mode == 17) && step == 0) ||
		    ((mode < 7 || (mode >= 10 && mode != 13 && mode != 17 && mode != 19 && mode != 21 && mode != 22 && mode != 33)) && chance(5))) {
			cpui386_raise_irq(a);
			cpui386_raise_irq(b);
		}
		if (glfb && chance(6))
			for (int i = 0; i < 2; i++) {
				memset(M[i].marks, 0, sizeof(M[i].marks));
				M[i].wrote = 0;
			}
		uword from = a->next_ip, from_lin = a->seg[SEG_CS].base + a->next_ip;
		if (mode == 22)
			two_op_clear();
		if (mode == 33)
			fz_clear();
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
			} else if (mode == 22 || mode == 33) {
				/* the code's bytes, tiny386's TLB entries that differ and
				   its fetch cache (v5.1, WI-2's check) */
				printf("    the step (%d instructions) started at %08x:", n, (unsigned) from_lin);
				for (int i = -24; i < 24; i++)
					printf(" %02x", from_lin >= P_CODE && from_lin < P_CODE + 0x10000 ?
					       M[0].mem[0x100000 + from_lin - P_CODE + i] : M[0].mem[(from_lin + i) & (MEM - 1)]);
				printf("\n");
				for (int t = 0; t < tlb_size; t++) {
					struct tlb_entry *x = &a->tlb.tab[t], *y = &b->tlb.tab[t];
					if (x->lpgno != y->lpgno || (x->lpgno != (uword) -1 && x->xaddr != y->xaddr))
						printf("    TLB %d: fast %08x (%08x), tiny386 %08x (%08x)\n", t, x->lpgno, x->xaddr ^ (x->lpgno << 12),
						       y->lpgno, y->xaddr ^ (y->lpgno << 12));
				}
				printf("    ifetch: fast laddr %08lx xaddr %08x; tiny386 laddr %08lx xaddr %08x\n",
				       (unsigned long) a->ifetch.laddr, (unsigned) a->ifetch.xaddr, (unsigned long) b->ifetch.laddr, (unsigned) b->ifetch.xaddr);
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
		u32 c0, c1, c2, c3, p21, p2b;
		memcpy(&c0, M[0].mem + CNT_PHYS, 4); memcpy(&c1, M[0].mem + CNT_PHYS + 4, 4);
		memcpy(&c2, M[0].mem + CNT_PHYS + 8, 4); memcpy(&c3, M[0].mem + CNT_PHYS + 12, 4);
		memcpy(&p21, M[0].mem + PT_PHYS + 0x21 * 4, 4); memcpy(&p2b, M[0].mem + PT_PHYS + 0x2b * 4, 4);
		char more[64] = "";
		if (mode == 20)
			snprintf(more, sizeof(more), "; setting %d", ksub);
		if (mode == 21)   /* (the page fault's and #GP's handlers count) */
			snprintf(more, sizeof(more), "; setting %d, #PF %u, #GP %u", lsub, c2, c3);
		printf("  seed %d, %s: %s after %ld steps, %ld instructions, %u port and video memory accesses"
		       " [VM %d cpl %d code16 %d; IRQs %u, INT 30h %u; PTE 21 %03x 2b %03x%s]\n",
		       seed, names[mode], why, step, a->cycle, M[0].nlog, !!(a->flags & VM), a->cpl, a->code16,
		       c0, c1, p21 & 0xfff, p2b & 0xfff, more);
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
	/* argv[3]: the modes, a character each (0 to 9, a to m) */
	const char *modes = argc > 3 ? argv[3] :
		"0123456789abcdefghij"
		"k"     /* v5.1 (WI-1): jumps, calls and returns across code pages */
		"l"     /* v5.1 (WI-1): a page read, then written */
		"m"     /* v5.1: the two-operand rule */
		"x"     /* v5.1 (WI-2's check): two memory operands at random */
		;
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
