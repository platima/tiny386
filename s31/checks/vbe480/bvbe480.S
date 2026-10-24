/*
 * BVBE480.COM (tiny386-idf v4.0): VBE pictures at the panel's own size and
 * round it, for the PC harness to check pixel for pixel.
 *
 * For each mode of the table at the end (800x480 at 8, 15, 16, 24 and 32
 * bits a pixel, 1A0h to 1A4h, which v4.0's VGA BIOS adds; 800x600, 640x480
 * and 320x200 at the same depths): whether 4F00h lists it, and if it does,
 * 4F01h's figures, then four or five pictures, each said with "@shot NAME"
 * on the BIOS's debug port, after which the program waits for a key:
 *
 *   mMMMM-lfb      the mode set with its linear frame buffer (4F02h with
 *                  bit 14), pattern A drawn a line at a time through int
 *                  15h AH=87h (the emulator's string copy into video
 *                  memory)
 *   mMMMM-poke     then single stores of 8, 16 and 32 bits into the linear
 *                  frame buffer from real mode, FS made flat (unreal mode):
 *                  the emulator's other three ways in
 *   mMMMM-win      the mode set again without it, pattern B through the
 *                  64 KiB window at A000h with 4F05h, a line at a time by
 *                  movsb, movsw or movsd in turn, lines that cross a bank
 *                  split there; then single stores of 8, 16 and 32 bits
 *                  through the window
 *   mMMMM-start16  if video memory holds 16 lines more (which are drawn
 *                  too): the display start 16 lines down (4F07h)
 *   mMMMM-pal2     8 bits a pixel only: another palette (4F09h), nothing
 *                  else changed
 *
 * The patterns (x and y the pixel, s 0 for A and 1 for B):
 *   8 bits   x + 2y + 77s, through palette P1 (4F09h after each mode set)
 *   15 bits  (33x + 1031y + 12345s) mod 32768, bit 15 set where x is odd
 *   16 bits  (33x + 1031y + 12345s) mod 65536
 *   24 bits  B = x xor y, G = y, R = x + 64s (each mod 256)
 *   32 bits  the same and a fourth byte 3x + y
 * and white (all bits of the pixel set; 15 bits 7FFFh) at x 0 and w - 1 and
 * on lines 0 and h - 1.  P1 is entry i: R = i, G = 3i, B = 7i; P2: R = 5i,
 * G = i / 4, B = 255 - i (all mod 64, a 6-bit DAC).  The stores and where
 * they go are in lfb_pokes and win_pokes; tests/vbe480check.py works out
 * the same pictures and holds the harness's against them.
 *
 * Lines that begin with @ are as tests/guest/bios.inc says (the Linux
 * route's tiny386 package); this file includes it.
 *
 * Build:
 *   as --32 -I DIR_WITH_BIOS_INC -o bvbe480.o bvbe480.S
 *   ld -m elf_i386 -Ttext 0x100 --oformat binary -o BVBE480.COM bvbe480.o
 *
 * Offered under the licence of tiny386 (BSD 3-clause).
 */
	.intel_syntax noprefix
	.code16
	.text
	.globl	_start
_start:
	jmp	main
	.include "bios.inc"

info	= 0x9000		/* 4F00h: 512 bytes */
minfo	= 0x9200		/* 4F01h: 256 bytes */
gdt	= 0x9380		/* int 15h AH=87h: 48 bytes */
fgdt	= 0x93c0		/* the flat descriptor for FS: 16 bytes */
palbuf	= 0x9400		/* 4F09h: 256 entries of 4 */
list	= 0x9c00		/* the mode list, copied: up to 512 bytes */
linebuf	= 0xa000		/* one line: up to 3,200 bytes */

main:
	cld
	push	cs
	pop	es
	call	flat_fs
	mov	dword ptr [info], 0x32454256	/* "VBE2" */
	mov	ax, 0x4f00
	mov	di, info
	int	0x10
	CK16	vbe480.00.ax, ax, 0x004f
	mov	ax, [info+0x12]
	mov	[mem64k], ax
	INFO16	vbe480.memory-64k, ax
	call	copy_list
	INFO16	vbe480.modes, [nmodes]
	mov	si, offset modetab
1:	mov	cx, [si]
	jcxz	2f
	push	si
	call	test_mode
	pop	si
	add	si, 2
	jmp	1b
2:	mov	ax, 0x0003
	int	0x10
	push	cs
	pop	es
	DONE

/* ---------------------------------------------------------------- a mode */

/* cx: the mode */
test_mode:
	mov	[mode], cx
	mov	si, offset k_info
	call	mhead
	call	in_list
	jnc	1f
	PUTS	".listed 0\n"
	ret
1:	PUTS	".listed 1\n"
	/* 4F01h */
	mov	ax, 0x4f01
	mov	cx, [mode]
	mov	di, minfo
	int	0x10
	push	ax
	mov	si, offset k_info
	call	mhead
	PUTS	".4f01 "
	pop	ax
	call	puthex16
	call	putnl
	mov	ax, [minfo+0x12]
	mov	[w], ax
	mov	ax, [minfo+0x14]
	mov	[h], ax
	movzx	ax, byte ptr [minfo+0x19]
	mov	[bpp], ax
	add	ax, 7
	shr	ax, 3
	mov	[bytespp], ax
	mul	word ptr [w]
	mov	[lbytes], ax
	mov	ax, [minfo+0x10]
	mov	[bpl], ax
	mov	eax, [minfo+0x28]
	mov	[lfb], eax
	/* @info mMMMM.mode W H BPP BPL LFB ATTRIBUTES MODEL MASKS */
	mov	si, offset k_info
	call	mhead
	PUTS	".mode "
	mov	ax, [w]
	call	puthex16
	call	putsp
	mov	ax, [h]
	call	puthex16
	call	putsp
	mov	ax, [bpp]
	call	puthex16
	call	putsp
	mov	ax, [bpl]
	call	puthex16
	call	putsp
	mov	eax, [lfb]
	call	puthex32
	call	putsp
	mov	ax, [minfo]
	call	puthex16
	call	putsp
	mov	al, [minfo+0x1b]
	call	puthex8
	call	putsp
	mov	eax, [minfo+0x1f]	/* red size, place, green size, place */
	call	puthex32
	mov	al, [minfo+0x23]	/* blue size, place */
	call	puthex8
	mov	al, [minfo+0x24]
	call	puthex8
	call	putnl

	/* ---- A: the linear frame buffer, through int 15h AH=87h */
	mov	bx, [mode]
	or	bx, 0x4000
	call	set_mode
	mov	byte ptr [seed], 0
	xor	bp, bp
1:	call	make_line
	call	lfb_line
	inc	bp
	cmp	bp, [h]
	jb	1b
	mov	si, offset k_lfb
	call	shot
	/* ---- and single stores into it */
	call	lfb_pokes
	mov	si, offset k_poke
	call	shot

	/* ---- B: the window */
	mov	bx, [mode]
	call	set_mode
	mov	byte ptr [seed], 1
	/* 16 lines more if video memory holds them */
	mov	ax, [h]
	mov	[nl], ax
	add	ax, 16
	movzx	eax, ax
	movzx	ebx, word ptr [bpl]
	imul	eax, ebx
	movzx	ebx, word ptr [mem64k]
	shl	ebx, 16
	cmp	eax, ebx
	ja	2f
	add	word ptr [nl], 16
2:	mov	si, offset k_info
	call	mhead
	PUTS	".lines "
	mov	ax, [nl]
	call	puthex16
	call	putnl
	xor	bp, bp
1:	call	make_line
	call	win_line
	inc	bp
	cmp	bp, [nl]
	jb	1b
	call	win_pokes
	mov	si, offset k_win
	call	shot
	mov	ax, [h]
	cmp	ax, [nl]
	je	3f
	/* the display start 16 lines down, and back */
	mov	ax, 0x4f07
	xor	bx, bx
	xor	cx, cx
	mov	dx, 16
	int	0x10
	mov	si, offset k_start
	call	shot
	mov	ax, 0x4f07
	xor	bx, bx
	xor	cx, cx
	xor	dx, dx
	int	0x10
3:	cmp	word ptr [bpp], 8
	jne	4f
	mov	byte ptr [palsel], 2
	call	set_pal
	mov	si, offset k_pal2
	call	shot
4:	ret

/* 4F02h with bx; the window's bank forgotten; palette P1 in 8 bits */
set_mode:
	mov	ax, 0x4f02
	int	0x10
	push	cs
	pop	es
	push	ax
	mov	si, offset k_info
	call	mhead
	PUTS	".4f02 "
	pop	ax
	call	puthex16
	call	putnl
	mov	word ptr [bank], 0xffff
	cmp	word ptr [bpp], 8
	jne	1f
	mov	byte ptr [palsel], 1
	call	set_pal
1:	ret

/* "@KIND mMMMM" (no end of line), cs:si the kind */
mhead:
	push	ax
	push	si
	mov	al, '@'
	call	putc
	call	puts
	PUTS	" m"
	mov	ax, [mode]
	call	puthex16
	pop	si
	pop	ax
	ret

/* "@shot mMMMM-SUFFIX", cs:si the suffix; then the key */
shot:
	push	si
	mov	si, offset k_shot
	call	mhead
	pop	si
	call	puts
	call	putnl
	call	waitkey
	ret

k_info:	.asciz	"info"
k_shot:	.asciz	"shot"
k_lfb:	.asciz	"-lfb"
k_poke:	.asciz	"-poke"
k_win:	.asciz	"-win"
k_start: .asciz	"-start16"
k_pal2:	.asciz	"-pal2"

/* ------------------------------------------------------------- patterns */

/* line bp of the pattern ([seed]) into linebuf */
make_line:
	pushad
	push	es
	push	cs
	pop	es
	mov	di, linebuf
	mov	cx, [w]
	xor	bx, bx			/* x */
	mov	dx, bp			/* y */
	mov	ax, [bpp]
	cmp	ax, 8
	je	ml8
	cmp	ax, 15
	je	ml15
	cmp	ax, 16
	je	ml16
	cmp	ax, 24
	je	ml24
	jmp	ml32
ml8:	movzx	si, byte ptr [seed]
	imul	si, si, 77
	mov	ax, bp
	shl	ax, 1
	add	ax, si
1:	stosb
	inc	al
	loop	1b
	jmp	ml_border
ml16:	movzx	si, byte ptr [seed]
	imul	si, si, 12345
	imul	ax, bp, 1031
	add	ax, si
1:	stosw
	add	ax, 33
	loop	1b
	jmp	ml_border
ml15:	movzx	si, byte ptr [seed]
	imul	si, si, 12345
	imul	ax, bp, 1031
	add	ax, si
1:	mov	si, ax
	and	si, 0x7fff
	test	bl, 1
	jz	2f
	or	si, 0x8000
2:	xchg	ax, si
	stosw
	xchg	ax, si
	add	ax, 33
	inc	bx
	loop	1b
	jmp	ml_border
ml24:	mov	ah, [seed]
	shl	ah, 6
1:	mov	al, bl
	xor	al, dl
	stosb
	mov	al, dl
	stosb
	mov	al, bl
	add	al, ah
	stosb
	inc	bx
	loop	1b
	jmp	ml_border
ml32:	mov	ah, [seed]
	shl	ah, 6
1:	mov	al, bl
	xor	al, dl
	stosb
	mov	al, dl
	stosb
	mov	al, bl
	add	al, ah
	stosb
	mov	al, bl
	add	al, bl
	add	al, bl
	add	al, dl
	stosb
	inc	bx
	loop	1b
ml_border:
	/* white: the first and last pixel, or the whole line on the first
	   and last line */
	mov	ax, [h]
	dec	ax
	cmp	bp, ax
	je	1f
	test	bp, bp
	jz	1f
	xor	bx, bx
	call	white_px
	mov	bx, [w]
	dec	bx
	call	white_px
	jmp	2f
1:	xor	bx, bx
3:	call	white_px
	inc	bx
	cmp	bx, [w]
	jb	3b
2:	pop	es
	popad
	ret

/* pixel bx of linebuf white; es is cs */
white_px:
	pusha
	mov	ax, bx
	mul	word ptr [bytespp]
	mov	di, ax
	add	di, linebuf
	mov	cx, [bytespp]
	mov	al, 0xff
	rep	stosb
	cmp	word ptr [bpp], 15
	jne	1f
	and	byte ptr [di - 1], 0x7f
1:	popa
	ret

/* palette [palsel] (1: P1, 2: P2) by 4F09h */
set_pal:
	pusha
	push	es
	push	cs
	pop	es
	mov	di, palbuf
	xor	cx, cx
1:	cmp	byte ptr [palsel], 2
	je	2f
	mov	al, cl			/* P1: B 7i, G 3i, R i */
	mov	ah, 7
	mul	ah
	and	al, 63
	stosb
	mov	al, cl
	mov	ah, 3
	mul	ah
	and	al, 63
	stosb
	mov	al, cl
	and	al, 63
	stosb
	jmp	3f
2:	mov	al, 255			/* P2: B 255 - i, G i / 4, R 5i */
	sub	al, cl
	and	al, 63
	stosb
	mov	al, cl
	shr	al, 2
	and	al, 63
	stosb
	mov	al, cl
	mov	ah, 5
	mul	ah
	and	al, 63
	stosb
3:	xor	al, al
	stosb
	inc	cx
	cmp	cx, 256
	jb	1b
	mov	ax, 0x4f09
	xor	bl, bl
	mov	cx, 256
	xor	dx, dx
	mov	di, palbuf
	int	0x10
	push	cs
	pop	es
	push	ax
	mov	si, offset k_info
	call	mhead
	PUTS	".4f09 "
	pop	ax
	call	puthex16
	call	putnl
	pop	es
	popa
	ret

/* ------------------------------------------------- the linear frame buffer */

/* linebuf to line bp, by int 15h AH=87h */
lfb_line:
	pushad
	movzx	eax, bp
	movzx	ebx, word ptr [bpl]
	imul	eax, ebx
	add	eax, [lfb]
	mov	edi, eax
	mov	si, linebuf
	mov	cx, [lbytes]
	shr	cx, 1
	call	move_up
	popad
	ret

/* one store into the linear frame buffer from real mode: line [py], byte
   bx of it, dl bytes (1, 2 or 4) of [pval] */
lfb_poke:
	pushad
	movzx	eax, word ptr [py]
	movzx	esi, word ptr [bpl]
	imul	eax, esi
	movzx	ebx, bx
	add	eax, ebx
	add	eax, [lfb]
	mov	edi, eax
	mov	eax, [pval]
	cmp	dl, 2
	jb	1f
	je	2f
	mov	fs:[edi], eax
	jmp	9f
1:	mov	fs:[edi], al
	jmp	9f
2:	mov	fs:[edi], ax
9:	popad
	ret

lfb_pokes:
	pushad
	/* bytes on line h/4, at 7k, 29k + 5, k 0 to 36 */
	mov	ax, [h]
	shr	ax, 2
	mov	[py], ax
	xor	cx, cx
1:	imul	bx, cx, 7
	imul	ax, cx, 29
	add	ax, 5
	mov	[pval], al
	mov	dl, 1
	call	lfb_poke
	inc	cx
	cmp	cx, 37
	jb	1b
	/* words on line h/2, at 2 + 5k (at odd and even addresses), 1234h +
	   0F0Fh k, k 0 to 22 */
	mov	ax, [h]
	shr	ax, 1
	mov	[py], ax
	xor	cx, cx
1:	imul	bx, cx, 5
	add	bx, 2
	imul	ax, cx, 0x0f0f
	add	ax, 0x1234
	mov	[pval], ax
	mov	dl, 2
	call	lfb_poke
	inc	cx
	cmp	cx, 23
	jb	1b
	/* double words on line 3h/4, at 1 + 13k, 89ABCDEFh xor 01010101h k,
	   k 0 to 18 */
	mov	ax, [h]
	mov	bx, 3
	mul	bx
	shr	ax, 2
	mov	[py], ax
	xor	cx, cx
1:	imul	bx, cx, 13
	inc	bx
	movzx	eax, cx
	imul	eax, eax, 0x01010101
	xor	eax, 0x89abcdef
	mov	[pval], eax
	mov	dl, 4
	call	lfb_poke
	inc	cx
	cmp	cx, 19
	jb	1b
	/* 50 double words of 5A3C96F0h on line h - 2, from 40 */
	mov	ax, [h]
	sub	ax, 2
	mov	[py], ax
	mov	dword ptr [pval], 0x5a3c96f0
	xor	cx, cx
1:	mov	bx, cx
	shl	bx, 2
	add	bx, 40
	mov	dl, 4
	call	lfb_poke
	inc	cx
	cmp	cx, 50
	jb	1b
	popad
	ret

/* FS: base 0 and a limit of 4G (unreal mode), so that fs:[edi] reaches the
   linear frame buffer from real mode.  FS is never loaded again here. */
flat_fs:
	pushad
	mov	dword ptr [fgdt], 0
	mov	dword ptr [fgdt + 4], 0
	mov	dword ptr [fgdt + 8], 0x0000ffff
	mov	dword ptr [fgdt + 12], 0x00cf9200
	xor	eax, eax
	mov	ax, cs
	shl	eax, 4
	add	eax, offset fgdt
	mov	word ptr [fgdtr], 15
	mov	[fgdtr + 2], eax
	cli
	lgdt	[fgdtr]
	mov	eax, cr0
	or	al, 1
	mov	cr0, eax
	jmp	1f
1:	mov	bx, 8
	mov	fs, bx
	and	al, 0xfe
	mov	cr0, eax
	jmp	2f
2:	sti
	popad
	ret
fgdtr:	.word	0
	.long	0

/* cx words from cs:si to the linear address in edi, through int 15h
   AH=87h (as tests/guest/bvbe.S) */
move_up:
	pushad
	push	es
	push	cs
	pop	es
	push	cx
	push	edi
	mov	di, gdt
	mov	cx, 24
	xor	ax, ax
	rep	stosw
	pop	edi
	pop	cx
	xor	eax, eax
	mov	ax, cs
	shl	eax, 4
	movzx	esi, si
	add	eax, esi
	mov	word ptr [gdt + 0x10], 0xffff
	mov	[gdt + 0x12], eax
	mov	byte ptr [gdt + 0x15], 0x93
	shr	eax, 24
	mov	[gdt + 0x17], al
	mov	eax, edi
	mov	word ptr [gdt + 0x18], 0xffff
	mov	[gdt + 0x1a], eax
	mov	byte ptr [gdt + 0x1d], 0x93
	shr	eax, 24
	mov	[gdt + 0x1f], al
	mov	ah, 0x87
	mov	si, gdt
	int	0x15
	jnc	1f
	inc	word ptr [movebad]
1:	pop	es
	popad
	ret
movebad: .word	0

/* --------------------------------------------------------------- window */

/* the window at A000h moved to bank dx, if it is not there */
setbank:
	cmp	dx, [bank]
	je	1f
	pusha
	mov	[bank], dx
	mov	ax, 0x4f05
	xor	bx, bx
	int	0x10
	popa
1:	ret

/* linebuf to line bp through the window: by movsb, movsw or movsd as the
   line's number is 0, 1 or 2 more than a multiple of 3, split where the
   line crosses into the next bank */
win_line:
	pushad
	push	es
	mov	ax, bp
	xor	dx, dx
	mov	bx, 3
	div	bx
	mov	[method], dl
	movzx	eax, bp
	movzx	ebx, word ptr [bpl]
	imul	eax, ebx		/* the line's address */
	mov	si, linebuf
	mov	cx, [lbytes]
1:	jcxz	9f
	push	eax
	shr	eax, 16
	mov	dx, ax
	call	setbank
	pop	eax
	mov	di, ax			/* where in the window */
	mov	bx, di
	neg	bx			/* bytes to the window's end (0: all 64K) */
	mov	dx, cx
	test	bx, bx
	jz	2f
	cmp	bx, cx
	jae	2f
	mov	dx, bx
2:	push	cx
	push	0xa000
	pop	es
	mov	cx, dx
	call	copy_by
	pop	cx
	sub	cx, dx
	movzx	edx, dx
	add	eax, edx
	jmp	1b
9:	pop	es
	popad
	ret

/* cx bytes from ds:si to es:di, by [method] */
copy_by:
	cmp	byte ptr [method], 1
	jb	3f
	je	2f
	push	cx
	shr	cx, 2
	rep	movsd
	pop	cx
	and	cx, 3
	rep	movsb
	ret
2:	push	cx
	shr	cx, 1
	rep	movsw
	pop	cx
	and	cx, 1
	rep	movsb
	ret
3:	rep	movsb
	ret

/* one store through the window: line [py], byte bx of it, dl bytes of
   [pval]; left out if it would cross into the next bank */
win_poke:
	pushad
	push	es
	movzx	eax, word ptr [py]
	movzx	esi, word ptr [bpl]
	imul	eax, esi
	movzx	ebx, bx
	add	eax, ebx
	movzx	ecx, dl
	movzx	edi, ax
	add	edi, ecx
	cmp	edi, 0x10000
	ja	9f
	push	eax
	shr	eax, 16
	mov	dx, ax
	call	setbank
	pop	eax
	mov	di, ax
	push	0xa000
	pop	es
	mov	eax, [pval]
	cmp	cl, 2
	jb	1f
	je	2f
	mov	es:[di], eax
	jmp	9f
1:	mov	es:[di], al
	jmp	9f
2:	mov	es:[di], ax
9:	pop	es
	popad
	ret

/* Each size on a line of its own, far enough from the others that no
   block of video memory (1 KiB) holds two of them: a store of one size
   that is not noted then shows */
win_pokes:
	pushad
	mov	ax, [h]
	xor	dx, dx
	mov	bx, 5
	div	bx
	mov	[py], ax
	/* bytes on line h/5, at 5 + 11k, 41k + 3, k 0 to 30 */
	xor	cx, cx
1:	imul	bx, cx, 11
	add	bx, 5
	imul	ax, cx, 41
	add	ax, 3
	mov	[pval], al
	mov	dl, 1
	call	win_poke
	inc	cx
	cmp	cx, 31
	jb	1b
	/* words on line 2h/5, at 1 + 7k (odd and even), A5A5h + 0123h k,
	   k 0 to 20 */
	mov	ax, [h]
	shl	ax, 1
	xor	dx, dx
	mov	bx, 5
	div	bx
	mov	[py], ax
	xor	cx, cx
1:	imul	bx, cx, 7
	inc	bx
	imul	ax, cx, 0x0123
	add	ax, 0xa5a5
	mov	[pval], ax
	mov	dl, 2
	call	win_poke
	inc	cx
	cmp	cx, 21
	jb	1b
	/* double words on line 3h/5, at 4 + 17k (every alignment),
	   13579BDFh + 02040810h k, k 0 to 15 */
	mov	ax, [h]
	mov	bx, 3
	mul	bx
	mov	bx, 5
	div	bx
	mov	[py], ax
	xor	cx, cx
1:	imul	bx, cx, 17
	add	bx, 4
	movzx	eax, cx
	imul	eax, eax, 0x02040810
	add	eax, 0x13579bdf
	mov	[pval], eax
	mov	dl, 4
	call	win_poke
	inc	cx
	cmp	cx, 16
	jb	1b
	popad
	ret

/* ---------------------------------------------------------------- lists */

/* the mode list from info into list, and how many in nmodes */
copy_list:
	pusha
	push	ds
	lds	si, [info + 0x0e]
	mov	di, list
	xor	bx, bx
1:	lodsw
	stosw
	cmp	ax, 0xffff
	je	2f
	inc	bx
	cmp	bx, 250
	jb	1b
	mov	word ptr es:[di], 0xffff
2:	pop	ds
	mov	[nmodes], bx
	popa
	ret

/* CF clear if mode [mode] is in the list */
in_list:
	push	si
	push	cx
	mov	cx, [mode]
	mov	si, list
1:	cmp	word ptr [si], 0xffff
	je	2f
	cmp	[si], cx
	je	3f
	add	si, 2
	jmp	1b
2:	pop	cx
	pop	si
	stc
	ret
3:	pop	cx
	pop	si
	clc
	ret

	.p2align 2
pval:	.long	0
lfb:	.long	0
mode:	.word	0
w:	.word	0
h:	.word	0
bpp:	.word	0
bytespp: .word	0
bpl:	.word	0
lbytes:	.word	0
mem64k:	.word	0
nmodes:	.word	0
nl:	.word	0
py:	.word	0
bank:	.word	0xffff
seed:	.byte	0
method:	.byte	0
palsel:	.byte	1

/* 800x480 first, then 800x600, 640x480 and 320x200 */
modetab:
	.word	0x1a0, 0x1a1, 0x1a2, 0x1a3, 0x1a4
	.word	0x103, 0x113, 0x114, 0x115, 0x143
	.word	0x101, 0x110, 0x111, 0x112, 0x142
	.word	0x146, 0x10e
	.word	0
