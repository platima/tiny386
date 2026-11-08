/* == v5.2 (the fastx review), mode y: the edges of the fast interpreter's
   switches after v5.2 (qw.h: QW_STK, QW_EAP, QW_FOLD2, QW_DECJ, QW_NOFOLD),
   by hand, a few instructions a step, with IF set for the random
   interrupts.  By the seed (seed % 6): real mode, 32-bit code at level 0,
   32-bit code with a 16-bit stack at linear 0, virtual-8086 mode, 16-bit
   protected mode with a 32-bit stack (B set), 16-bit protected mode; with
   paging, but in real mode.  In turn:
     the stack at its edges: (E)SP at 0 to 4, FFFDh to FFFFh, and (with a
       32-bit stack, or ESP's top half kept by a 16-bit one) 10000h, 10001h,
       1FFFEh and 2FFFFh; at level 0, the four bytes either side of a page
       of the stack.  At each, PUSH and POP r, PUSH imm8 and imm, CALL and
       RET, CALL and RET n, LEAVE, PUSH r and m through FFh, CALL through
       (E)AX, PUSH DS, PUSH CS and PUSH SP, at times with 66h, each balanced
       (a 16-bit SP that wraps at 64 KiB, and ESP's top half, which a
       16-bit stack keeps: QW_STK's 16-bit copy)
     ALU, MOV and 80h, 81h, 83h with a memory operand after up to three
       prefixes (66h, 67h, segments, REP, REPNE, LOCK): QW_EAP's one test of
       the prefixes, and a JZ or JNZ after each CMP
     then, page after page: a near JMP to 1 to 22 bytes before a page's end
       (less the MOV of a loop's count), and there one of: DEC CX or ECX;
       JNZ back, a loop; DEC DX; JZ or JNZ; CMP AX,AX or AX,DX; JZ or JNZ;
       TEST AL,1; JZ or JNZ; CMP byte [m],imm (80h /7); JZ or JNZ; then one
       more, across or after the page's end (QW_FOLD2 and QW_DECJ: the JZ
       done inline, its window test, the step's end at it); a few of the
       prefixed instructions above; and at times MOV SS,AX or STI as the
       first instruction of a page reached by a JMP (so that tiny386 fetches
       it the slow way), then CMP AX,AX; JZ, taken, which tiny386 fuses only
       when the CMP is not that first instruction (QW_NOFOLD: the fusion
       rule's instruction kept as a value of steps, moved by STEPS_SS() and
       STEPS_STI()) */
static int ysub;                /* mode y's setting: seed % 6 */
static uword y_off(void) { return gp - gcode_phys; }   /* the offset in CS (its base on a page) */
static void y_mov_esp(u32 v) { if (!g32) e8(0x66); e8(0xbc); e32(v); }   /* MOV ESP,v */
static void y_mov_ebp(u32 v) { if (!g32) e8(0x66); e8(0xbd); e32(v); }   /* MOV EBP,v */
static u32 y_sp0(void) { return ysub == 1 ? P_STACK - 0x100 : 0xf000; }
static u32 y_bp0(void) { return ysub == 1 || ysub == 2 ? P_DATA + 0x2fc0 : 0x0ffe; }
/* the stack at (E)SP v, then (E)SP as it was */
static void y_stack(u32 v)
{
	for (int k = 0; k < 12; k++) {
		y_mov_esp(v);
		int pf = rnd(3) == 0;   /* 66h: the other operand size */
		if (k == 7 || (g32 && (k == 3 || k == 4)))   /* (not where it would cut EIP to 16 bits) */
			pf = 0;
		int o16 = pf ? g32 : !g32;
#define YP() do { if (pf) e8(0x66); } while (0)
		switch (k) {
		case 0: YP(); e8(0x50); YP(); e8(0x5a); break;                  /* PUSH AX; POP DX */
		case 1: YP(); e8(0x6a); e8(0x85); YP(); e8(0x5a); break;        /* PUSH -7Bh; POP DX */
		case 2: YP(); e8(0x68); if (o16) e16(0x1234); else e32(0x12345678); YP(); e8(0x5a); break;
		case 3:         /* CALL t; JMP over t; t: RET */
			YP(); e8(0xe8); if (o16) e16(2); else e32(2);
			e8(0xeb); e8(pf ? 2 : 1); YP(); e8(0xc3);
			break;
		case 4:         /* PUSH AX; CALL t; JMP over t; t: RET n */
			YP(); e8(0x50);
			YP(); e8(0xe8); if (o16) e16(2); else e32(2);
			e8(0xeb); e8(pf ? 4 : 3); YP(); e8(0xc2); e16(o16 ? 2 : 4);
			break;
		case 5:         /* MOV EBP,v - 2 to v + 2; LEAVE; EBP back */
			y_mov_ebp(v - 2 + rnd(5));
			YP(); e8(0xc9);
			y_mov_ebp(y_bp0());
			break;
		case 6: YP(); e8(0xff); e8(0xf0); YP(); e8(0x5a); break;        /* PUSH AX (FFh /6); POP DX */
		case 7: {       /* MOV EAX,t; CALL EAX; JMP +1; t: RET */
			uword s = g32 ? glin() : y_off();
			if (!g32) e8(0x66);
			e8(0xb8); e32(s + (g32 ? 5 : 6) + 4);
			e8(0xff); e8(0xd0); e8(0xeb); e8(0x01); e8(0xc3);
			break;
		}
		case 8: YP(); e8(0x1e); YP(); e8(0x5a); break;                  /* PUSH DS; POP DX */
		case 9: YP(); e8(0x0e); YP(); e8(0x5a); break;                  /* PUSH CS; POP DX */
		case 10: YP(); e8(0xff); e8(0x30 | (g32 ? 3 : 7)); YP(); e8(0x5a); break;   /* PUSH [(E)BX]; POP DX */
		default: YP(); e8(0x54); YP(); e8(0x5a); break;                 /* PUSH SP; POP DX */
		}
#undef YP
	}
	y_mov_esp(y_sp0());
}
/* a memory operand for modrm's reg field reg, from (E)BX or (E)BP or a fixed address */
static void y_mem(int reg, int a16)
{
	if (a16) {
		switch (rnd(3)) {
		case 0: e8(0x47 | reg << 3); e8(rnd(64)); break;                /* [bx+d8] */
		case 1: e8(0x43 | reg << 3); e8(rnd(64)); break;                /* [bp+di+d8], SS */
		default: e8(0x06 | reg << 3); e16(0x100 + rnd(0x100)); break;   /* [d16] */
		}
	} else {
		switch (rnd(3)) {
		case 0: e8(0x43 | reg << 3); e8(rnd(64)); break;                /* [ebx+d8] */
		case 1: e8(0x45 | reg << 3); e8(rnd(64)); break;                /* [ebp+d8], SS */
		default: e8(0x05 | reg << 3); e32(data_lin() + 0x100 + rnd(0x100)); break;   /* [d32] */
		}
	}
}
/* an ALU, MOV or group 1 instruction with a memory operand, after up to
   three prefixes (as tiny386: 66h and 67h set the size, they do not toggle
   it); a JZ or JNZ after CMP */
static void y_eap(void)
{
	static const u8 opc[] = { 0x01, 0x03, 0x09, 0x0b, 0x21, 0x23, 0x29, 0x2b, 0x31, 0x33, 0x39, 0x3b, 0x38, 0x3a,
				  0x11, 0x13, 0x19, 0x1b, 0x00, 0x02, 0x88, 0x89, 0x8a, 0x8b, 0x80, 0x81, 0x83 };
	static const u8 segs[] = { 0x26, 0x2e, 0x36, 0x3e, 0x65 };
	int n = rnd(4), a16 = !g32, o16 = !g32;
	for (int i = 0; i < n; i++) {
		switch (rnd(7)) {
		case 0: e8(0x66); o16 = g32; break;
		case 1: if (!g32) { e8(0x67); a16 = 0; } else e8(0x3e); break;   /* (16-bit addresses in 32-bit code would leave the data) */
		case 2: e8(segs[rnd(5)]); break;
		case 3: e8(0xf3); break;
		case 4: e8(0xf2); break;
		case 5: e8(0xf0); break;
		default: e8(0x3e); break;
		}
	}
	u8 op = opc[rnd(sizeof(opc))];
	e8(op);
	int grp = op >= 0x80 && op <= 0x83;
	int reg = grp ? rnd(8) : rnd(3);
	y_mem(reg, a16);
	if (op == 0x80 || op == 0x83) e8(rnd(256));
	else if (op == 0x81) { if (o16) e16(rnd(0x10000)); else e32((u32) rs); }
	if ((op >= 0x38 && op <= 0x3b) || (grp && reg == 7)) {
		e8(0x74 + rnd(2)); e8(chance(2)); e8(0x90);
	}
}
/* a pair of kind 0 to 4 (above); y_pre(): the bytes before its DEC */
static int y_pre(int kind) { return kind == 0 ? (g32 ? 5 : 6) : 0; }
static void y_pair(int kind)
{
	switch (kind) {
	case 0: {       /* MOV ECX,1 to 3; L: DEC CX or ECX; JNZ L */
		if (!g32) e8(0x66);
		e8(0xb9); e32(1 + rnd(3));
		int p = chance(3);
		if (p) e8(0x66);
		e8(0x49); e8(0x75); e8(p ? 0xfc : 0xfd);
		break;
	}
	case 1:         /* DEC DX or EDX; JZ or JNZ +1; NOP */
		if (chance(3)) e8(0x66);
		e8(0x4a); e8(0x74 + rnd(2)); e8(1); e8(0x90);
		break;
	case 2:         /* CMP AX,AX or AX,DX; JZ or JNZ +1; NOP */
		e8(0x39); e8(chance(2) ? 0xc0 : 0xd0); e8(0x74 + rnd(2)); e8(1); e8(0x90);
		break;
	case 3:         /* TEST AL,1; JZ or JNZ +1; NOP */
		e8(0xa8); e8(1); e8(0x74 + rnd(2)); e8(1); e8(0x90);
		break;
	default:        /* CMP byte [m],imm; JZ or JNZ +1; NOP */
		e8(0x80); y_mem(7, !g32); e8(rnd(256)); e8(0x74 + rnd(2)); e8(1); e8(0x90);
		break;
	}
}
/* a near JMP to back bytes before the next page's end (INT3s between) */
static void y_jmp_to_end(int back)
{
	int jl = g32 ? 5 : 3;
	uword t = (y_off() & ~0xfffu) + 0x1000 - back;
	if (t < y_off() + jl)
		t += 0x1000;
	e8(0xe9);
	if (g32) e32(t - (y_off() + 4)); else e16(t - (y_off() + 2));
	while (y_off() < t)
		e8(0xcc);
}
static void gen_y(void)
{
	e8(0xfb);       /* STI */
	if (ysub == 1) {
		for (int d = -3; d <= 4; d++)
			y_stack(P_STACK - 0x1000 + d);
		y_stack(P_STACK - 0x100);
	} else {
		static const u32 sps[] = { 0, 1, 2, 3, 4, 0xfffe, 0xffff, 0xfffd, 0x10000, 0x10001, 0x1fffe, 0x2ffff };
		for (unsigned i = 0; i < sizeof(sps) / sizeof(sps[0]); i++)
			if (sps[i] < 0x10000 || ysub == 2 || ysub == 4)
				y_stack(sps[i]);
	}
	for (int i = 0; i < 60; i++)
		y_eap();
	if (!g32)       /* Jcc near with 66h in 16-bit code */
		for (int i = 0; i < 8; i++) {
			e8(0x66); e8(0x0f); e8(0x80 + rnd(16)); e32(1); e8(0x90);
		}
	static const int at[] = { 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 5, 4, 3, 2, 1 };
	while (y_off() < 0xd000) {
		int kind = rnd(5);
		y_jmp_to_end(at[rnd(sizeof(at) / sizeof(at[0]))] + y_pre(kind));
		y_pair(kind);
		y_pair(rnd(5));
		for (int i = 0; i < 4; i++)
			y_eap();
		if (chance(2)) {
			e8(0x8c); e8(0xd0);                     /* MOV AX,SS */
			y_jmp_to_end(0);
			if (chance(2)) { e8(0x8e); e8(0xd0); }  /* MOV SS,AX */
			else e8(0xfb);                          /* STI */
			e8(0x39); e8(0xc0); e8(0x74); e8(1); e8(0x90);   /* CMP AX,AX; JZ +1; NOP */
		}
	}
	e8(0xfa);       /* CLI */
}
