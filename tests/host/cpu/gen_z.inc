/* == v5.3 (batch 4, fast_exec()'s 16-bit paths), mode z, by hand, a few
   instructions a step, with IF set for the random interrupts.  By the seed
   (seed % 4): real mode, virtual-8086 mode, 16-bit protected mode with
   paging and without it; in each CS's base is FF20h, off a page boundary,
   so that one page
   holds both the segment's start and the end of the 64 KiB below it, and
   another its end and what lies past it.  Jumps across offset FFFFh and 0
   (QW_J16: 16-bit code's window clamped to the segment, the mask where pc
   becomes EIP), each from a block of the main flow (at 400h up) that sets
   its registers and goes by a near JMP to a launch, which jumps across the
   wrap to a pad, which goes back by a near JMP:
     short jumps both ways ((seed / 3) % 2: forward from FFxxh to 00xxh, or
       backward from 00xxh to FFxxh): JMP short, JZ after XOR, JZ after CMP
       and after TEST (fused where tiny386 fuses them), JNZ right after DEC
       CX (QW_DECJ), LOOP, JCXZ
     near ones both ways (from FE00h to 100h up, from 300h to FD00h up):
       JMP and CALL near (the pad a RET, the launch then a JMP back), Jcc
       near after XOR, and JMP near and Jcc near with 66h, whose 32-bit
       displacement also crosses 64 KiB a few times over
     absolute targets past the segment's end, which tiny386 does not mask:
       RET with 66h to 10000h up, RET n with 66h (since the review of batch
       4), JMP EAX with 66h to 10000h up, each a JMP
       near there back to the main flow (whose target tiny386 masks); JMP AX
       to FFF4h, NOPs on past FFFFh, a JMP near back from 10000h up (each
       with interrupts off: one there would push IP cut to 16 bits)
   Then, in the main flow, the lazy flags (QW_INCL, and the conditions of
   fast_cond()): an ALU operation (ADD, ADC, SUB, SBB, CMP, AND, OR, XOR,
   TEST, NEG, SHR, SHL) of AX with BX or an immediate, the two from edge
   values (0, 1, 7Fh to 81h, 7FFFh to 8001h, FFFFh), at times with 66h;
   none, one or two INC or DEC of a register or of a word or dword at
   [BP+d8], at times with 66h; then a Jcc short over a NOP, of each of the
   sixteen conditions in turn, and PUSHF; POP DX.
   Then REP MOVS and REP STOS of bytes, words and dwords (QW_REPMIS), ES
   DS's segment, SI and DI from a few bytes either side of DS's 1000h and
   B000h (the pages at 21000h and 2B000h, which V86 mode and 16-bit
   protected mode map only at their first fault, so that some runs fault
   in their second page) and of 7FFFh, at odd offsets or not, CX 1 to 40,
   forwards and backwards (STD; CLD after); at times SI or DI near FFFFh,
   where a 16-bit run wraps.  Then the same with DS and ES at 2010h (the
   review of batch 4), a base off a page boundary (20100h; in 16-bit
   protected mode a selector with that base), so that a 16-bit SI or DI
   wraps inside a page and the pieces stop at the wrap, not at a page's
   end: SI and DI near FFFFh going up, near 0 going down, or about 800h.
   Then (the review of batch 4) a JMP FAR to 9800h:7F00h (in 16-bit
   protected mode a selector of base 98000h), the page below the VGA
   window, and six times a near JMP from there into the window (A0010h
   up), which tiny386 runs, and one back: the inline refill's page change
   into MMIO, with and without paging (QW_RMREF's in_iomem() tests).  RAM
   under the window holds INC AX, so that code run from there differs.
   Last, without paging (QW_RMREF's page changes), a JMP FAR into RAM's last
   page but one, six times a near JMP from there into its last page, which
   tiny386 runs, and one back (more than once, since only a step that goes
   on past the JMP shows the page cached), then NOPs that run on into the
   last page, and a JMP FAR back. */
#define Z_BASE 0xff20u          /* CS's base */
#define Z_SEG 0x0ff2            /* ... as a real-mode and virtual-8086 segment */
#define Z_SEL 0x0fe8            /* ... as a selector of GDT16 */
#define Z_TSEL 0x0fe0           /* 16-bit protected mode: a segment at RAM's end (MEM - 2000h) */
#define Z_DSEG 0x2010           /* data at 20100h, off a page boundary: a segment, and a selector of GDT16 */
#define Z_VSEG 0x9800           /* code at 98000h, its offset 8000h the VGA window: a real-mode segment */
#define Z_VSEL 0x0fd8           /* ... and a selector of GDT16 with that base */
static int zsub;                /* mode z's setting: seed % 4 */
static int zdir;                /* (seed / 4) % 2: the short jumps forward (0) or backward (1) */
static uword z_off(void) { return gp - gcode_phys; }
static void z_at(uword off) { gp = gcode_phys + off; }
static void z_jmp_near(uword to) { e8(0xe9); e16(to - (z_off() + 2)); }   /* JMP near to (16 bits wrap) */
/* the launch and pad slots: bumped by each test */
static uword z_sl, z_sp;        /* the short jumps' next launch (going down or up) and pad */
static uword z_nl, z_np;        /* the near jumps' forward launch and pad */
static uword z_bl, z_bp;        /* ... and backward */
static uword z_xp;              /* the next target past the segment's end */
static int z_tests, z_ax_done;
static u32 z_edge(void)
{
	static const u32 v[] = { 0, 1, 2, 0x7f, 0x80, 0x81, 0xff, 0x7fff, 0x8000, 0x8001, 0xffff,
				 0x7fffffff, 0x80000000, 0xffffffff, 0x12345678 };
	return chance(4) ? (u32) rs : v[rnd(sizeof(v) / sizeof(v[0]))];
}
/* (the second part) an ALU result, INC and DEC after it, a Jcc of condition c */
static void z_flags(int c)
{
	int o32 = chance(3);
	if (o32) e8(0x66);
	e8(0xb8); if (o32) e32(z_edge()); else e16(z_edge());   /* MOV (E)AX */
	if (o32) e8(0x66);
	e8(0xbb); if (o32) e32(z_edge()); else e16(z_edge());   /* MOV (E)BX */
	if (chance(3)) e8(0xf9);                                /* STC (for ADC, SBB) */
	int alu = rnd(12);
	if (chance(4)) e8(0x66);
	switch (alu) {
	case 0: e8(0x01); e8(0xd8); break;                      /* ADD AX,BX */
	case 1: e8(0x11); e8(0xd8); break;                      /* ADC */
	case 2: e8(0x29); e8(0xd8); break;                      /* SUB */
	case 3: e8(0x19); e8(0xd8); break;                      /* SBB */
	case 4: e8(0x39); e8(0xd8); break;                      /* CMP */
	case 5: e8(0x21); e8(0xd8); break;                      /* AND */
	case 6: e8(0x09); e8(0xd8); break;                      /* OR */
	case 7: e8(0x31); e8(0xd8); break;                      /* XOR */
	case 8: e8(0x85); e8(0xd8); break;                      /* TEST */
	case 9: e8(0xf7); e8(0xd8); break;                      /* NEG AX */
	case 10: e8(0xd1); e8(0xe8); break;                     /* SHR AX,1 */
	default: e8(0x83); e8(0xc0 | (rnd(8) << 3)); e8(z_edge()); break;   /* 83h /r AX,imm8 */
	}
	for (int n = rnd(3); n > 0; n--) {
		if (chance(3)) e8(0x66);
		switch (rnd(4)) {
		case 0: e8(0x40 + rnd(2) * 8); break;                   /* INC or DEC AX */
		case 1: e8(0x41 + rnd(2) * 8); break;                   /* ... CX */
		case 2: e8(0xff); e8(0x46 | rnd(2) << 3); e8(0x10 + 2 * rnd(8)); break;   /* INC or DEC [BP+d8] */
		default: e8(0xfe); e8(0xc0 | rnd(2) << 3 | rnd(4)); break;   /* INC or DEC AL to BL */
		}
	}
	e8(0x70 + c); e8(1); e8(0x90);                          /* Jcc over a NOP */
	e8(0x9c); e8(0x5a);                                     /* PUSHF; POP DX */
}
static void setup_z(u8 *mem, CPUI386 *cpu)
{
	switch (zsub) {
	case 0: setup_real(mem, cpu); break;
	case 1: setup_v86(mem, cpu); break;
	default: setup_pm16(mem, cpu, zsub == 2);
		desc(mem, GDT16_PHYS + Z_SEL, Z_BASE, 0xfffff, 0x09a);   /* (limit 1 MiB: code past FFFFh) */
		desc(mem, GDT16_PHYS + Z_TSEL, MEM - 0x2000, 0xffff, 0x09a);
		desc(mem, GDT16_PHYS + Z_DSEG, Z_DSEG << 4, 0xffff, 0x092);
		desc(mem, GDT16_PHYS + Z_VSEL, Z_VSEG << 4, 0xffff, 0x09a);
		break;
	}
	cpu->seg[SEG_CS].sel = zsub >= 2 ? Z_SEL : Z_SEG;
	cpu->seg[SEG_CS].base = Z_BASE;
	if (zsub >= 2)
		cpu->seg[SEG_CS].limit = 0xfffff;
}
/* a short jump of kind k from the launch at the current place to the pad at offset to; the
   block before it set CX for it */
static void z_short(int k, uword to)
{
	int len = k == 1 || k == 2 || k == 6 ? 4 : k == 3 ? 3 : 2;
	sword d = (sword) (s16) (u16) (to - (z_off() + len));
	if (d < -128 || d > 127) {   /* (not in reach: a plain near JMP instead) */
		z_jmp_near(to);
		return;
	}
	switch (k) {
	case 0: e8(0xeb); break;                                  /* JMP short */
	case 1: e8(0x31); e8(0xc0); e8(0x74); break;              /* XOR AX,AX; JZ */
	case 2: e8(0x39); e8(0xc0); e8(0x74); break;              /* CMP AX,AX; JZ (fused) */
	case 3: e8(0x49); e8(0x75); break;                        /* DEC CX; JNZ (CX 2) */
	case 4: e8(0xe2); break;                                  /* LOOP (CX 2) */
	case 5: e8(0xe3); break;                                  /* JCXZ (CX 0) */
	default: e8(0xa8); e8(0x00); e8(0x74); break;            /* TEST AL,0; JZ (fused) */
	}
	e8((u8) d);
}
static void gen_z(void)
{
	z_at(0);
	z_jmp_near(0x400);
	z_at(0x400);
	e8(0xfb);       /* STI */
	z_sl = zdir ? 0x0004 : 0xffee;
	z_sp = zdir ? 0xffee : 0x0004;
	z_nl = 0xfe00; z_np = 0x0100;
	z_bl = 0x0300; z_bp = 0xfd00;
	z_xp = 0x10010;           /* (10000h to 10005h: the run on past FFFFh) */
	z_tests = z_ax_done = 0;
	for (int round = 0; round < 160; round++) {
		int what = rnd(6);
		uword next;
		if (what <= 1) {        /* a short jump across the wrap */
			int k = rnd(7);
			uword l = z_sl, p = z_sp;
			if (zdir ? (l > 0x70 || p < 0xff80) : (l < 0xff90 || p > 0x60))
				continue;
			/* the block: CX for the kind, a JMP near to the launch */
			e8(0xb9); e16(k == 5 ? 0 : 2);   /* MOV CX,n */
			z_jmp_near(l);
			next = z_off();
			uword s = gp;
			z_at(l);
			z_short(k, p);
			if (zdir) z_sl = z_off() + 1; else z_sl = l - 5;
			z_at(p);
			z_jmp_near(next);
			if (zdir) z_sp = p - 4; else z_sp = z_off() + 1;
			gp = s;
		} else if (what <= 3) { /* a near one, forward (2) or backward (3) */
			int k = rnd(6);
			int fwd = what == 2;
			uword l = fwd ? z_nl : z_bl, p = fwd ? z_np : z_bp;
			if (fwd ? (l > 0xff60 || p > 0x2f0) : (l > 0x3e0 || p > 0xfde0))
				continue;
			e8(0xb9); e16(2);
			z_jmp_near(l);
			next = z_off();
			uword s = gp;
			z_at(l);
			/* the displacement crosses 64 KiB, and with 66h a few times more */
			sword big = (sword) ((rnd(7) - 3) * 0x10000);
			switch (k) {
			case 0: z_jmp_near(p); break;                                          /* JMP near */
			case 1: e8(0xe8); e16(p - (z_off() + 2)); z_jmp_near(next); break;     /* CALL near; then back */
			case 2: e8(0x31); e8(0xc0); e8(0x0f); e8(0x84); e16(p - (z_off() + 2)); break;   /* XOR; JZ near */
			case 3: e8(0x66); e8(0xe9); e32((u32) ((s16) (u16) (p - (z_off() + 4)) + big)); break;   /* JMP near, 66h */
			case 4: e8(0x31); e8(0xc0); e8(0x66); e8(0x0f); e8(0x84);
				e32((u32) ((s16) (u16) (p - (z_off() + 4)) + big)); break;               /* XOR; JZ near, 66h */
			default: e8(0x31); e8(0xc0); e8(0x0f); e8(0x85); e16(p - (z_off() + 2)); z_jmp_near(p); break;   /* JNZ not taken; JMP */
			}
			if (fwd) z_nl = z_off() + 1; else z_bl = z_off() + 1;
			z_at(p);
			if (k == 1) e8(0xc3);                   /* RET */
			else z_jmp_near(next);
			if (fwd) z_np = z_off() + 1; else z_bp = z_off() + 1;
			gp = s;
		} else if (what == 4) { /* an absolute target past the segment's end */
			if (z_xp > 0x100c0)
				continue;
			uword x = z_xp;
			e8(0xfa);  /* CLI: an interrupt there would push IP cut to 16 bits (STI back here) */
			int kx = rnd(3);
			if (kx == 0) {     /* PUSH DWORD x; RET with 66h */
				e8(0x66); e8(0x68); e32(x);
				e8(0x66); e8(0xc3);
			} else if (kx == 1) {   /* PUSH AX once or twice; PUSH DWORD x; RET n with 66h, n the AXs */
				int n = 2 << rnd(2);
				for (int j = 0; j < n; j += 2)
					e8(0x50);
				e8(0x66); e8(0x68); e32(x);
				e8(0x66); e8(0xc2); e16(n);
			} else {           /* MOV EAX,x; JMP EAX with 66h */
				e8(0x66); e8(0xb8); e32(x);
				e8(0x66); e8(0xff); e8(0xe0);
			}
			next = z_off();
			uword s = gp;
			z_at(x);
			z_jmp_near(next);       /* (tiny386 masks its target) */
			z_xp = z_off() + 2;
			gp = s;
			e8(0xfb);       /* STI */
		} else {                /* JMP AX to FFF4h, on past FFFFh (once) */
			if (z_ax_done)
				continue;
			z_ax_done = 1;
			e8(0xfa);                       /* CLI (as above) */
			e8(0xb8); e16(0xfff4);          /* MOV AX,FFF4h */
			e8(0xff); e8(0xe0);             /* JMP AX */
			next = z_off();
			uword s = gp;
			z_at(0xfff4);
			while (z_off() < 0x10000 + 3)
				e8(0x90);
			z_jmp_near(next);
			gp = s;
			e8(0xfb);                       /* STI */
		}
		z_tests++;
	}
	for (int i = 0; i < 128; i++)
		z_flags(i & 15);
	e8(0x1e); e8(0x07);                                     /* PUSH DS; POP ES */
	for (int i = 0; i < 64; i++) {
		static const u16 at[] = { 0x1000, 0xb000, 0x8000, 0x1000, 0xb000 };
		int down = chance(3);
		u16 si = at[rnd(5)] - 8 + rnd(16), di = at[rnd(5)] - 8 + rnd(16);
		if (chance(10)) di = 0xfff8 + rnd(8);
		if (chance(10)) si = 0xfff8 + rnd(8);
		e8(0xbe); e16(si);                              /* MOV SI */
		e8(0xbf); e16(di);                              /* MOV DI */
		e8(0xb9); e16(1 + rnd(40));                     /* MOV CX */
		if (down) e8(0xfd);                             /* STD */
		int k = rnd(6);
		if (k == 2 || k == 5) e8(0x66);                 /* dwords */
		e8(0xf3);                                       /* REP */
		e8(k < 3 ? (k == 0 ? 0xa4 : 0xa5) : (k == 3 ? 0xaa : 0xab));   /* MOVS or STOS */
		if (down) e8(0xfc);                             /* CLD */
	}
	e8(0x1e);                                               /* PUSH DS */
	e8(0xb8); e16(Z_DSEG); e8(0x8e); e8(0xd8); e8(0x8e); e8(0xc0);   /* MOV AX,2010h; MOV DS,AX; MOV ES,AX */
	for (int i = 0; i < 64; i++) {
		int down = chance(2);
		u16 si = down ? rnd(9) : 0xfff0 + rnd(16), di = down ? rnd(9) : 0xfff0 + rnd(16);
		if (chance(3)) si = 0x0800 + rnd(16);
		if (chance(3)) di = 0x0800 + rnd(16);
		e8(0xbe); e16(si);                              /* MOV SI */
		e8(0xbf); e16(di);                              /* MOV DI */
		e8(0xb9); e16(1 + rnd(40));                     /* MOV CX */
		if (down) e8(0xfd);                             /* STD */
		int k = rnd(6);
		if (k == 2 || k == 5) e8(0x66);                 /* dwords */
		e8(0xf3);                                       /* REP */
		e8(k < 3 ? (k == 0 ? 0xa4 : 0xa5) : (k == 3 ? 0xaa : 0xab));   /* MOVS or STOS */
		if (down) e8(0xfc);                             /* CLD */
	}
	e8(0x1f); e8(0x1e); e8(0x07);                           /* POP DS; PUSH DS; POP ES */
	{   /* (offsets of Z_VSEG: 7F00h up in RAM, 8010h up the VGA window) */
		e8(0xea); e16(0x7f00); e16(zsub >= 2 ? Z_VSEL : Z_VSEG);   /* JMP FAR to 7F00h */
		uword next = z_off();
		uword s = gp;
		uword t = (uword) Z_VSEG << 4;
		for (int i = 0; i < 6; i++) {
			gp = t + 0x7f00 + 0x10 * i;
			e8(0x90);                                       /* NOP; JMP near into the window */
			e8(0xe9); e16(0x8010 + 0x20 * i - (0x7f00 + 0x10 * i + 4));
			for (int m = 0; m < 2; m++) {                   /* there: 3 NOPs, a JMP near back */
				u8 *v = M[m].vga + 0x10 + 0x20 * i;
				v[0] = v[1] = v[2] = 0x90;
				uword to = 0x7f10 + 0x10 * i, at = 0x8010 + 0x20 * i + 6;
				v[3] = 0xe9; v[4] = (u8) (to - at); v[5] = (u8) ((to - at) >> 8);
			}
			for (int j = 0; j < 6; j++)                     /* the RAM under it: INC AX */
				gm[0xa0010 + 0x20 * i + j] = 0x40;
		}
		gp = t + 0x7f60;
		e8(0xea); e16(next); e16(gcs_far);              /* JMP FAR back */
		gp = s;
	}
	if (zsub == 3) {   /* (Z_TSEL's offsets: F00h up and FE0h in RAM's last page but one, 1012h up in its last) */
		e8(0xea); e16(0x0f00); e16(Z_TSEL);             /* JMP FAR Z_TSEL:F00h */
		uword next = z_off();
		uword s = gp;
		uword t = MEM - 0x2000;
		for (int i = 0; i < 6; i++) {
			gp = t + 0x0f00 + 0x10 * i;
			e8(0x90);                                       /* NOP; JMP near into the last page */
			e8(0xe9); e16(0x1012 + 0x10 * i - (0x0f00 + 0x10 * i + 4));
			gp = t + 0x1012 + 0x10 * i;                     /* there: 3 NOPs, a JMP near back */
			e8(0x90); e8(0x90); e8(0x90);
			e8(0xe9); e16(0x0f10 + 0x10 * i - (0x1012 + 0x10 * i + 6));
		}
		gp = t + 0x0f60;
		e8(0xe9); e16(0x0fe0 - (0x0f60 + 3));           /* JMP near on to FE0h */
		gp = t + 0x0fe0;
		for (int i = 0; i < 40; i++)                    /* NOPs on into the last page */
			e8(0x90);
		e8(0xea); e16(next); e16(Z_SEL);                /* JMP FAR back */
		gp = s;
	}
	e8(0xfa);       /* CLI */
}
