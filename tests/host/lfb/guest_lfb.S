/*
 * The guest for test_lfb.c (v5.0, the linear frame buffer's own path in
 * i386.c): small pieces of 32-bit protected-mode code that store into and
 * load from video memory every way the CPU can, each ending at "done".
 * Loaded at 0x10000 and started at case_NAME by cpui386_reset_pm(); flat
 * segments, interrupts off.  The test puts the size of video memory at
 * 0x8000 and the VGA's PCI configuration address (bus 0, its device and
 * function, register 0) at 0x8004; RAM from 0x100000 holds a pattern, video
 * memory another (test_lfb.c), and the page tables for PAGING_ON are at
 * 0x20000 (linear 0x400000 onwards as test_lfb.c lays them out).  Loads
 * are put into RAM from 0x9100, which the test compares.
 *
 * Assembled by run_lfb.sh with "as --32" and linked at 0x10000 as a flat
 * binary; test_lfb.c finds each case_ and bench_ by its symbol.
 */
	.intel_syntax noprefix
	.code32
	.text

	.equ LFB, 0xE0000000
	.equ VSIZE, 0x8000		/* the size of video memory */
	.equ PCICFG, 0x8004		/* 0x80000000 | devfn << 8 */
	.equ OUT, 0x9100		/* where loaded values go */
	.equ RAMPAT, 0x100000		/* RAM with a pattern in it */

/* VBE register idx = val, through the ports at 1CEh */
	.macro VBE idx, val
	mov dx, 0x1ce
	mov ax, \idx
	out dx, ax
	mov dx, 0x1cf
	mov ax, \val
	out dx, ax
	.endm

	.macro CASE name
	.globl case_\name
case_\name:
	mov esp, 0x9f000
	call vbe_on
	mov ebp, [VSIZE]
	.endm

	.macro PAGING_ON
	mov eax, 0x20000
	mov cr3, eax
	mov eax, cr0
	or eax, 0x80000000
	mov cr0, eax
	.endm

	.macro MARK
	out 0x80, al
	.endm

	.globl done
done:
	hlt
	jmp done

/* The VGA's BAR 0 at E0000000h with memory decoding on, the misc output's
   RAM enable, and VBE at 800x480 in 16 bits with the linear frame buffer,
   video memory left as it is (NOCLEARMEM), bank 0 */
vbe_on:
	push eax
	push edx
	mov eax, [PCICFG]
	or eax, 0x10
	mov dx, 0xcf8
	out dx, eax
	mov eax, LFB
	mov dx, 0xcfc
	out dx, eax
	mov eax, [PCICFG]
	or eax, 0x04
	mov dx, 0xcf8
	out dx, eax
	mov ax, 3
	mov dx, 0xcfc
	out dx, ax
	mov dx, 0x3c2
	mov al, 0x63
	out dx, al
	VBE 4, 0
	VBE 1, 800
	VBE 2, 480
	VBE 3, 16
	VBE 4, 0xc1
	pop edx
	pop eax
	ret

/* BAR 0 at eax, memory decoding on */
bar_at:
	push edx
	push eax
	mov eax, [PCICFG]
	or eax, 0x10
	mov dx, 0xcf8
	out dx, eax
	pop eax
	mov dx, 0xcfc
	out dx, eax
	pop edx
	ret

/* memory decoding off: the BAR is taken away (pc.c's -1) */
bar_off:
	push eax
	push edx
	mov eax, [PCICFG]
	or eax, 0x04
	mov dx, 0xcf8
	out dx, eax
	mov ax, 1
	mov dx, 0xcfc
	out dx, ax
	pop edx
	pop eax
	ret

gdt:
	.quad 0
	.quad 0x00cf9a000000ffff	/* 08h: code, flat */
	.quad 0x00cf92000000ffff	/* 10h: data, flat */
	.quad 0xe0cf92000000ffff	/* 18h: data, base E0000000h */
gdtr:
	.word gdtr - gdt - 1
	.long gdt

/* ==== single stores ==== */

CASE st8
	mov byte ptr [LFB + 0], 0x11
	mov byte ptr [LFB + 1], 0x22
	mov byte ptr [LFB + 0x100], 0x33
	mov byte ptr [LFB + 0x1ff], 0x44
	mov byte ptr [ebp + LFB - 1], 0x55	/* the last byte */
	mov byte ptr [ebp + LFB], 0x66		/* past the end: nowhere */
	mov byte ptr [ebp + LFB + 0x10], 0x77
	jmp done

CASE st16
	mov word ptr [LFB + 0x1000], 0x1111
	mov word ptr [LFB + 0x1101], 0x2222
	mov word ptr [LFB + 0x12ff], 0x3333	/* two blocks */
	mov word ptr [LFB + 0x13fe], 0x4444
	mov word ptr [ebp + LFB - 2], 0x5555
	mov word ptr [ebp + LFB - 1], 0x6666	/* runs past the end: nowhere */
	mov word ptr [ebp + LFB], 0x7777
	jmp done

CASE st32
	mov dword ptr [LFB + 0x2000], 0x11111111
	mov dword ptr [LFB + 0x2101], 0x22222222
	mov dword ptr [LFB + 0x2202], 0x33333333
	mov dword ptr [LFB + 0x23fd], 0x44444444	/* two blocks */
	mov dword ptr [LFB + 0x24fe], 0x55555555
	mov dword ptr [LFB + 0x25ff], 0x66666666
	mov dword ptr [ebp + LFB - 4], 0x77777777
	mov dword ptr [ebp + LFB - 3], 0x88888888	/* past the end: nowhere */
	mov dword ptr [ebp + LFB - 2], 0x99999999
	mov dword ptr [ebp + LFB - 1], 0xaaaaaaaa
	jmp done

/* read-modify-write, the stack, a segment register, XCHG */
CASE rmw
	mov eax, 0x01020304
	add dword ptr [LFB + 0x3000], eax
	inc word ptr [LFB + 0x3101]
	xor byte ptr [LFB + 0x32ff], 0x5a
	mov ecx, 0xdeadbeef
	xchg dword ptr [LFB + 0x3400], ecx
	mov [OUT], ecx
	mov esi, esp
	mov esp, LFB + 0x3600
	push eax
	mov bx, 0x1234
	push bx
	xor bx, bx
	pop bx
	pop edx
	mov esp, esi
	mov [OUT + 4], edx
	mov word ptr [OUT + 8], bx
	mov word ptr [LFB + 0x3700], ds
	not dword ptr [LFB + 0x37fe]
	jmp done

/* loads: whole, at the end, and part past it (pc.c's split reads) */
CASE ld
	mov eax, [LFB + 0x10]
	mov [OUT], eax
	movzx eax, word ptr [LFB + 0x101]
	mov [OUT + 4], eax
	movzx eax, byte ptr [ebp + LFB - 1]
	mov [OUT + 8], eax
	mov eax, [ebp + LFB - 4]
	mov [OUT + 12], eax
	mov eax, [ebp + LFB - 3]
	mov [OUT + 16], eax
	mov eax, [ebp + LFB - 1]
	mov [OUT + 20], eax
	movzx eax, word ptr [ebp + LFB - 1]
	mov [OUT + 24], eax
	mov eax, [ebp + LFB + 8]
	mov [OUT + 28], eax
	mov eax, [LFB + 0x3ff]
	add eax, [LFB + 0x7fe]
	mov [OUT + 32], eax
	jmp done

/* ==== with paging: linear 0x400000 on (test_lfb.c's page_map) ==== */

/* each byte of an access that crosses a page goes where its own page is */
CASE pgx
	PAGING_ON
	mov dword ptr [0x400ffe], 0x44332211	/* LFB 3FFEh, LFB 8000h */
	mov word ptr [0x401fff], 0x6655		/* LFB 8FFFh, RAM 150000h */
	mov dword ptr [0x402ffd], 0xaa998877	/* RAM 150FFDh, LFB 4000h */
	mov dword ptr [0x404ffe], 0xeeddccbb	/* the last page's end, then past video memory */
	mov word ptr [0x405fff], 0x2211		/* past video memory, then the window */
	mov dword ptr [0x400010], 0x55555555	/* plain ones through the page tables */
	mov word ptr [0x400020], 0x6666
	mov byte ptr [0x400030], 0x77
	mov eax, [0x400ffe]
	mov [OUT], eax
	mov eax, [0x401ffe]
	mov [OUT + 4], eax
	mov eax, [0x402ffd]
	mov [OUT + 8], eax
	mov eax, [0x404ffe]
	mov [OUT + 12], eax
	movzx eax, word ptr [0x405fff]
	mov [OUT + 16], eax
	mov eax, [0x408010]			/* the same video memory as 0x400010 */
	mov [OUT + 20], eax
	jmp done

/* ==== REP STOS ==== */

CASE stosb_f
	mov edi, LFB + 0x5003
	mov ecx, 1000
	mov al, 0xa5
	rep stosb
	mov [OUT], edi
	mov [OUT + 4], ecx
	jmp done

CASE stosw_f
	mov edi, LFB + 0x6002
	mov ecx, 2777				/* on into the next page */
	mov ax, 0x5aa5
	rep stosw
	mov [OUT], edi
	jmp done

CASE stosd_f
	mov edi, LFB + 0x8000
	mov ecx, 0x1000				/* four pages */
	mov eax, 0x12345678
	rep stosd
	mov [OUT], edi
	jmp done

CASE stosd_b
	std
	mov edi, LFB + 0xc0fc
	mov ecx, 100				/* down over a page's start */
	mov eax, 0x87654321
	rep stosd
	cld
	mov [OUT], edi
	jmp done

CASE stosw_b
	std
	mov edi, LFB + 0xd00e
	mov ecx, 300
	mov ax, 0x1357
	rep stosw
	cld
	mov [OUT], edi
	jmp done

CASE stosb_b
	std
	mov edi, LFB + 0xe010
	mov ecx, 333
	mov al, 0x24
	rep stosb
	cld
	mov [OUT], edi
	jmp done

/* not aligned: the element by element path, each through store32 */
CASE stosd_mis
	mov edi, LFB + 0xf001
	mov ecx, 1200
	mov eax, 0xa1b2c3d4
	rep stosd
	jmp done

/* 16-bit addressing: ES at E0000000h, DI wraps from FFF8h to 0 */
CASE stosd_a16
	lgdt [gdtr]
	mov ax, 0x18
	mov es, ax
	mov edi, 0x1234fff8
	mov ecx, 0x56780004
	mov eax, 0x0f0e0d0c
	.byte 0x67				/* addr16 */
	rep stosd
	mov [OUT], edi
	mov [OUT + 4], ecx
	mov ax, 0x10
	mov es, ax
	jmp done

/* over the end of video memory: what is past it goes nowhere */
CASE stosd_end
	lea edi, [ebp + LFB - 8]
	mov ecx, 4
	mov eax, 0x13579bdf
	rep stosd
	lea edi, [ebp + LFB - 0x1002]
	mov ecx, 0x1004
	mov ax, 0x2468
	rep stosw
	jmp done

CASE stos_short
	mov edi, LFB + 0x17000
	xor ecx, ecx
	mov eax, 0xffffffff
	rep stosd				/* none */
	mov ecx, 1
	rep stosd				/* one */
	mov edi, LFB + 0x17100
	stosd					/* without REP */
	stosw
	stosb
	mov [OUT], edi
	jmp done

/* with paging, LFB pages far apart */
CASE stosd_pg
	PAGING_ON
	mov edi, 0x400f00
	mov ecx, 0x200				/* LFB 3F00h, then 8000h */
	mov eax, 0x0badf00d
	rep stosd
	mov edi, 0x404f80
	mov ecx, 0x40				/* the last page, then past video memory */
	rep stosd
	std
	mov edi, 0x403010
	mov ecx, 0x10				/* LFB 4010h down to 4000h, then RAM 150FFCh */
	rep stosd
	cld
	jmp done

/* the bank window at A0000h: bank 2 */
CASE stos_win
	VBE 5, 2
	mov edi, 0xa0100
	mov ecx, 300
	mov eax, 0x11223344
	rep stosd
	mov edi, 0xafffc
	mov ecx, 4				/* past the window's 64 KiB: nowhere */
	rep stosd
	mov byte ptr [0xa0001], 0x99
	mov word ptr [0xa0003], 0x8877
	mov dword ptr [0xa0006], 0x66554433
	movzx eax, word ptr [0xa0003]
	mov [OUT], eax
	jmp done

/* ==== REP MOVS ==== */

CASE movsd_r2l_f
	mov esi, RAMPAT
	mov edi, LFB + 0x10000
	mov ecx, 0x800
	rep movsd
	mov [OUT], esi
	mov [OUT + 4], edi
	jmp done

CASE movsd_r2l_b
	std
	mov esi, RAMPAT + 0xffc
	mov edi, LFB + 0x12ffc
	mov ecx, 0x400
	rep movsd
	cld
	mov [OUT], esi
	mov [OUT + 4], edi
	jmp done

CASE movsw_r2l
	mov esi, RAMPAT + 0x202
	mov edi, LFB + 0x13802
	mov ecx, 0x501				/* on into the next page */
	rep movsw
	std
	mov esi, RAMPAT + 0x2000
	mov edi, LFB + 0x15006
	mov ecx, 0x99
	rep movsw
	cld
	jmp done

CASE movsb_r2l
	mov esi, RAMPAT + 0x333
	mov edi, LFB + 0x16001
	mov ecx, 0x777
	rep movsb
	std
	mov esi, RAMPAT + 0x3000
	mov edi, LFB + 0x16fff
	mov ecx, 0x123
	rep movsb
	cld
	jmp done

CASE movsd_l2r
	mov esi, LFB + 0x14000
	mov edi, 0x160000
	mov ecx, 0x800
	rep movsd
	std
	mov esi, LFB + 0x18ffc
	mov edi, 0x170ffc
	mov ecx, 0x300
	rep movsd
	cld
	mov esi, LFB + 0x19001
	mov edi, 0x180000
	mov ecx, 0x100				/* not aligned: element by element */
	rep movsd
	jmp done

CASE movsd_l2l
	mov esi, LFB + 0x20000
	mov edi, LFB + 0x30000
	mov ecx, 0x1000				/* apart */
	rep movsd
	jmp done

/* a row up (a scroll): destination below the source, going up */
CASE movsd_l2l_up
	mov esi, LFB + 0x40640
	mov edi, LFB + 0x40000
	mov ecx, 0x1000
	rep movsd
	jmp done

/* destination above the source, going up: each element read may be one
   stored already (the pattern repeats) */
CASE movsd_l2l_rep
	mov esi, LFB + 0x50000
	mov edi, LFB + 0x50010
	mov ecx, 0x100
	rep movsd
	mov esi, LFB + 0x51000
	mov edi, LFB + 0x51004
	mov ecx, 0x80
	rep movsd
	jmp done

/* a row down: destination above the source, going down */
CASE movsd_l2l_down
	std
	mov esi, LFB + 0x63ffc
	mov edi, LFB + 0x6463c
	mov ecx, 0x1000
	rep movsd
	cld
	jmp done

/* destination below the source, going down: repeats again */
CASE movsd_l2l_back
	std
	mov esi, LFB + 0x707fc
	mov edi, LFB + 0x707f8
	mov ecx, 0x80
	rep movsd
	cld
	jmp done

CASE movsw_l2l
	mov esi, LFB + 0x72002
	mov edi, LFB + 0x72fa2
	mov ecx, 0x300
	rep movsw
	std
	mov esi, LFB + 0x74ffe
	mov edi, LFB + 0x7400e
	mov ecx, 0x255
	rep movsw
	cld
	mov esi, LFB + 0x75001
	mov edi, LFB + 0x75000
	mov ecx, 0x999
	rep movsb
	jmp done

/* with paging: 0x408000 is the same video memory as 0x400000 */
CASE movsd_pg
	PAGING_ON
	mov esi, 0x408100
	mov edi, 0x400000
	mov ecx, 0x300				/* down by 100h in the same memory */
	rep movsd
	mov esi, 0x408000
	mov edi, 0x400010
	mov ecx, 0x40				/* up, overlapping: repeats */
	rep movsd
	mov esi, 0x400f00
	mov edi, 0x401f80
	mov ecx, 0x100				/* LFB to LFB over two page ends */
	rep movsd
	mov esi, 0x402f00
	mov edi, 0x404f00
	mov ecx, 0x80				/* RAM to the last page and past it */
	rep movsd
	jmp done

/* past the end of video memory, from RAM */
CASE movsd_end
	mov esi, RAMPAT
	lea edi, [ebp + LFB - 8]
	mov ecx, 4
	rep movsd
	lea esi, [ebp + LFB - 0x10]
	mov edi, 0x190000
	mov ecx, 8
	rep movsd
	jmp done

/* the window at one end, the frame buffer or RAM at the other */
CASE movs_win
	VBE 5, 1
	mov esi, LFB + 0x80000
	mov edi, 0xa0200
	mov ecx, 0x100				/* frame buffer to window */
	rep movsd
	mov esi, 0xa1000
	mov edi, LFB + 0x81000
	mov ecx, 0x100				/* window to frame buffer */
	rep movsd
	mov esi, RAMPAT
	mov edi, 0xa2000
	mov ecx, 0x100				/* RAM to window (pc.c's string path) */
	rep movsd
	jmp done

/* more of the window: window to window, RAM to window going down (no
   string path there), REP STOSB and STOSW, an overlapping MOVSB */
CASE win_more
	VBE 5, 3
	mov esi, 0xa0100
	mov edi, 0xa0800
	mov ecx, 0x80
	rep movsd
	std
	mov esi, RAMPAT + 0x1ffc
	mov edi, 0xa1ffc
	mov ecx, 0x200
	rep movsd
	cld
	mov edi, 0xa3001
	mov ecx, 0x101
	mov al, 0x3c
	rep stosb
	mov edi, 0xa4002
	mov ecx, 0x99
	mov ax, 0x4c4d
	rep stosw
	mov esi, 0xa5000
	mov edi, 0xa5003
	mov ecx, 0x40
	rep movsb
	jmp done

CASE movs_short
	mov esi, RAMPAT
	mov edi, LFB + 0x82000
	xor ecx, ecx
	rep movsd
	movsd
	movsw
	movsb
	mov esi, LFB + 0x82100
	mov edi, LFB + 0x82200
	movsd
	mov [OUT], esi
	mov [OUT + 4], edi
	jmp done

/* LODS, SCAS, CMPS: loads only */
CASE lods
	mov esi, LFB + 0x83000
	lodsd
	mov [OUT], eax
	lodsw
	mov [OUT + 4], eax
	lodsb
	mov [OUT + 8], eax
	mov esi, LFB + 0x84000
	mov edi, LFB + 0x84000
	mov ecx, 0x40
	repe cmpsd
	mov [OUT + 12], ecx
	mov [OUT + 16], esi
	mov edi, LFB + 0x85000
	mov eax, [LFB + 0x85040]
	mov ecx, 0x100
	repne scasd
	mov [OUT + 20], ecx
	mov [OUT + 24], edi
	jmp done

/* ==== the BAR ==== */

/* memory decoding off: the frame buffer's addresses go to pc.c as before */
CASE bar_off
	call bar_off
	mov dword ptr [LFB + 0x100], 0x11111111
	mov word ptr [LFB + 0x204], 0x2222
	mov byte ptr [LFB + 0x307], 0x33
	mov edi, LFB + 0x400
	mov ecx, 0x40
	mov eax, 0x44444444
	rep stosd
	mov esi, RAMPAT
	mov edi, LFB + 0x800
	mov ecx, 0x40
	rep movsd
	mov eax, [LFB + 0x100]
	mov [OUT], eax
	jmp done

/* BAR 0 at F0000000h, then back */
CASE bar_move
	mov eax, 0xf0000000
	call bar_at
	mov dword ptr [0xf0000100], 0x11111111
	mov edi, 0xf0000400
	mov ecx, 0x40
	mov eax, 0x22222222
	rep stosd
	mov dword ptr [LFB + 0x900], 0x33333333	/* not the frame buffer now */
	mov eax, [0xf0000100]
	mov [OUT], eax
	mov eax, LFB
	call bar_at
	mov dword ptr [LFB + 0xa00], 0x44444444
	jmp done

/* ==== for counting (bench mode): MARK before and after each piece ==== */

	.macro BENCH name
	.globl bench_\name
bench_\name:
	mov esp, 0x9f000
	call vbe_on
	mov ebp, [VSIZE]
	.endm

/* 4,096 single stores of each size into the frame buffer, and into RAM */
BENCH stores
	mov eax, 0x12345678
	MARK
	mov edi, LFB + 0x10000
	mov ecx, 4096
1:	mov [edi], eax
	add edi, 4
	dec ecx
	jnz 1b
	MARK
	mov edi, 0x100000
	mov ecx, 4096
1:	mov [edi], eax
	add edi, 4
	dec ecx
	jnz 1b
	MARK
	mov edi, LFB + 0x20000
	mov ecx, 4096
1:	mov [edi], ax
	add edi, 2
	dec ecx
	jnz 1b
	MARK
	mov edi, LFB + 0x30000
	mov ecx, 4096
1:	mov [edi], al
	inc edi
	dec ecx
	jnz 1b
	MARK
	mov esi, LFB + 0x10000
	mov ecx, 4096
	xor ebx, ebx
1:	add ebx, [esi]
	add esi, 4
	dec ecx
	jnz 1b
	MARK
	mov esi, 0x100000
	mov ecx, 4096
	xor ebx, ebx
1:	add ebx, [esi]
	add esi, 4
	dec ecx
	jnz 1b
	MARK
	jmp done

/* a fill of 400x300 in 16 bits on an 800x480 screen (T386MON's fill
   test's size): a REP STOSD a row; then the whole screen in one */
BENCH fill
	mov eax, 0x5aa55aa5
	MARK
	mov edx, LFB + 0x10000
	mov ebx, 300
1:	mov edi, edx
	mov ecx, 200
	rep stosd
	add edx, 1600
	dec ebx
	jnz 1b
	MARK
	mov edi, LFB
	mov ecx, 800 * 480 / 2
	rep stosd
	MARK
	jmp done

/* copies of 400x300: RAM to the screen, a scroll up by a row (the screen
   to itself), a scroll down, the screen to RAM */
BENCH copy
	MARK
	mov edx, LFB + 0x10000
	mov eax, RAMPAT
	mov ebx, 300
1:	mov edi, edx
	mov esi, eax
	mov ecx, 200
	rep movsd
	add edx, 1600
	add eax, 800
	dec ebx
	jnz 1b
	MARK
	mov edx, LFB + 0x10000
	mov ebx, 300
1:	mov edi, edx
	lea esi, [edx + 1600]
	mov ecx, 200
	rep movsd
	add edx, 1600
	dec ebx
	jnz 1b
	MARK
	std
	mov edx, LFB + 0x10000 + 300 * 1600 + 796
	mov ebx, 300
1:	mov edi, edx
	lea esi, [edx - 1600]
	mov ecx, 200
	rep movsd
	sub edx, 1600
	dec ebx
	jnz 1b
	cld
	MARK
	mov edx, LFB + 0x10000
	mov eax, RAMPAT
	mov ebx, 300
1:	mov esi, edx
	mov edi, eax
	mov ecx, 200
	rep movsd
	add edx, 1600
	add eax, 800
	dec ebx
	jnz 1b
	MARK
	jmp done

/* the bank window: single stores (as before), REP STOSD and REP MOVSD */
BENCH window
	VBE 5, 1
	mov eax, 0x12345678
	MARK
	mov edi, 0xa0000
	mov ecx, 4096
1:	mov [edi], eax
	add edi, 4
	dec ecx
	jnz 1b
	MARK
	mov edi, 0xa0000
	mov ecx, 0x4000
	rep stosd
	MARK
	mov esi, 0xa0000
	mov edi, 0xa8000
	mov ecx, 0x2000
	rep movsd
	MARK
	jmp done
