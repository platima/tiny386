/*
 * The guest for test_rep_wrap.c: REP string instructions with 16-bit
 * addresses across the 64 KiB wrap, in real mode, each segment's base off
 * a page boundary (on one, the run's end at the page hides the wrap).
 * Loaded at 0x1000 and started there by a far jump at the reset vector;
 * it sets the dword at 0x7FC when done.  Assembled by run.sh with
 * "as --32" and linked at 0x1000 as a flat binary.  Results, nine dwords
 * at 0x800, and the bytes written to port E9h:
 *  r0 STOSW forward, ES 2345h, DI FFFCh, 4 words: dword at ES:0 (wrap: BEEFBEEF)
 *  r1                the dword at ES + 10000h (no wrap: BEEFBEEF)
 *  r2 STOSB backward (STD), ES 4567h, DI 1, 4 bytes: word at ES:FFFEh (wrap: 7777)
 *  r3                the word at ES - 2 (no wrap: 7777)
 *  r4 MOVSW forward, source DS 5678h:FFFCh wraps, 4 words to 7000h:100h: the
 *     dword at 7000h:104h (wrap: 44443333, no wrap: 66665555)
 *  r5 MOVSD (a16) forward, destination ES 7891h:FFF8h wraps, 4 dwords: ES:0 (wrap: C3C3C3C3)
 *  r6                the dword at ES + 10000h (no wrap: C3C3C3C3)
 *  r7 INSB forward from port 300h (reads FFh), ES 2468h:FFFEh, 4 bytes: the
 *     word at ES:0 in the low half, the word at ES + 10000h in the high
 *     (wrap: 0000FFFF, no wrap: FFFF0000)
 *  then OUTSB forward to port E9h, DS 1A10h:FFFEh, 4 bytes (wrap: abcd, no wrap: abXY)
 *  then MOVSB backward, DS 2B10h:1 to ES 7000h:203h, 4 bytes: bytes at 7000h:200h
 *     in r8 (wrap: from DS:1, 0, FFFF, FFFE; no wrap: from DS - 1 and DS - 2)
 */
.code16
.globl _start
_start:
	cli
	cld
	xor %ax, %ax
	mov %ax, %fs

	# r0, r1
	mov $0x2345, %ax
	mov %ax, %es
	movl $0, %es:0
	mov $0x3345, %ax
	mov %ax, %es
	movl $0, %es:0
	mov $0x2345, %ax
	mov %ax, %es
	mov $0xfffc, %di
	mov $4, %cx
	mov $0xbeef, %ax
	rep stosw
	movl %es:0, %eax
	mov %eax, %fs:0x800
	mov $0x3345, %ax
	mov %ax, %es
	movl %es:0, %eax
	mov %eax, %fs:0x804

	# r2, r3
	mov $0x4567, %ax
	mov %ax, %es
	movw $0, %es:0xfffe
	mov $0x3567, %ax
	mov %ax, %es
	movw $0, %es:0xfffe
	mov $0x4567, %ax
	mov %ax, %es
	std
	mov $1, %di
	mov $4, %cx
	mov $0x77, %al
	rep stosb
	cld
	movzwl %es:0xfffe, %eax
	mov %eax, %fs:0x808
	mov $0x3567, %ax
	mov %ax, %es
	movzwl %es:0xfffe, %eax
	mov %eax, %fs:0x80c

	# r4
	mov $0x5678, %ax
	mov %ax, %ds
	movl $0x22221111, 0xfffc
	movl $0x44443333, 0
	mov $0x6678, %ax
	mov %ax, %ds
	movl $0x66665555, 0
	mov $0x5678, %ax
	mov %ax, %ds
	mov $0x7000, %ax
	mov %ax, %es
	mov $0xfffc, %si
	mov $0x100, %di
	mov $4, %cx
	rep movsw
	movl %es:0x104, %eax
	mov %eax, %fs:0x810

	# r5, r6
	xor %ax, %ax
	mov %ax, %ds
	movl $0xa1a1a1a1, 0x900
	movl $0xb2b2b2b2, 0x904
	movl $0xc3c3c3c3, 0x908
	movl $0xd4d4d4d4, 0x90c
	mov $0x7891, %ax
	mov %ax, %es
	movl $0, %es:0
	mov $0x8891, %ax
	mov %ax, %es
	movl $0, %es:0
	mov $0x7891, %ax
	mov %ax, %es
	mov $0x900, %si
	mov $0xfff8, %di
	mov $4, %cx
	rep movsl
	movl %es:0, %eax
	mov %eax, %fs:0x814
	mov $0x8891, %ax
	mov %ax, %es
	movl %es:0, %eax
	mov %eax, %fs:0x818

	# r7
	mov $0x2468, %ax
	mov %ax, %es
	movw $0, %es:0
	mov $0x3468, %ax
	mov %ax, %es
	movw $0, %es:0
	mov $0x2468, %ax
	mov %ax, %es
	mov $0xfffe, %di
	mov $4, %cx
	mov $0x300, %dx
	rep insb
	movzwl %es:0, %eax
	mov $0x3468, %bx
	mov %bx, %es
	movzwl %es:0, %ebx
	shl $16, %ebx
	or %ebx, %eax
	mov %eax, %fs:0x81c

	# OUTSB to E9h
	mov $0x1a10, %ax
	mov %ax, %ds
	movw $0x6261, 0xfffe
	movw $0x6463, 0
	mov $0x2a10, %ax
	mov %ax, %ds
	movw $0x5958, 0
	mov $0x1a10, %ax
	mov %ax, %ds
	mov $0xfffe, %si
	mov $4, %cx
	mov $0xe9, %dx
	rep outsb

	# r8: MOVSB backward, the source wrapping below 0
	mov $0x2b10, %ax
	mov %ax, %ds
	movw $0x2211, 0		# DS:0 11h, DS:1 22h
	movw $0x4433, 0xfffe	# DS:FFFEh 33h, DS:FFFFh 44h
	mov $0x1b10, %ax
	mov %ax, %ds
	movw $0x6655, 0xfffe	# DS - 2 55h, DS - 1 66h
	mov $0x2b10, %ax
	mov %ax, %ds
	mov $0x7000, %ax
	mov %ax, %es
	movl $0, %es:0x200
	std
	mov $1, %si
	mov $0x203, %di
	mov $4, %cx
	rep movsb
	cld
	movl %es:0x200, %eax
	mov %eax, %fs:0x820

	xor %ax, %ax
	mov %ax, %ds
	movl $1, 0x7fc
1:	jmp 1b
