/*
 * MODE13X.COM (tiny386-idf v4.2): mode 13h used the way DOS games use it,
 * and the mode changes round it, for the PC harness to hold every picture
 * against the guest's screen converted afresh (the flicker harness's
 * -board), v4.1 against v4.2.
 *
 *   m13-picture   mode 13h, a picture straight into A000h: colour
 *                 (x xor y) + y / 4
 *   m13-box       a 24x24 box moved across it 120 times, each move after
 *                 the start of a vertical retrace (3DAh bit 3), the picture
 *                 under it put back first, as a game's sprite
 *   m13-palette   the 256 colours turned round 64 times through the DAC
 *                 (3C8h, 3C9h), a retrace each
 *   text          mode 03h and a line of text from DOS
 *   m13-again     mode 13h again (the BIOS clears video memory) and the
 *                 picture again
 *   m13-kept      mode 13h set over itself with video memory kept (93h):
 *                 nothing on the screen changes
 *   m12           mode 12h (640x480, 16 colours), sixteen bars through the
 *                 map mask
 *   m13-over-m12  mode 13h with video memory kept (93h) over it: the planes
 *                 as 256-colour pixels
 * then mode 03h and @done.  Each picture is said with @shot NAME on the
 * BIOS's debug port, and the program waits for the key the harness gives
 * (tests/guest/bios.inc of the Linux route's tiny386 package).
 *
 * Build:
 *   as --32 -I DIR_WITH_BIOS_INC -o mode13x.o mode13x.S
 *   ld -m elf_i386 -Ttext 0x100 --oformat binary -o MODE13X.COM mode13x.o
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

save	= 0x8000		/* the picture under the box: 24x24 bytes */

main:
	cld
	mov	ax, 0x0013
	int	0x10
	call	picture
	SHOT	m13-picture

	/* the box: 120 moves, 2 pixels right and 1 down each */
	push	0xa000
	pop	es
	mov	word ptr [bx_x], 10
	mov	word ptr [bx_y], 20
	call	box_save
	mov	cx, 120
1:	call	retrace
	call	box_restore
	add	word ptr [bx_x], 2
	inc	word ptr [bx_y]
	call	box_save
	call	box_draw
	loop	1b
	SHOT	m13-box

	/* the palette turned round: entry i gets i + k's colour */
	mov	cx, 64
2:	call	retrace
	mov	dx, 0x3c8
	xor	al, al
	out	dx, al
	inc	dx
	xor	bx, bx
3:	mov	ax, bx
	add	ax, cx
	and	al, 63
	out	dx, al			/* R */
	mov	al, bl
	shr	al, 2
	out	dx, al			/* G */
	mov	al, bl
	not	al
	shr	al, 2
	out	dx, al			/* B */
	inc	bx
	cmp	bx, 256
	jb	3b
	loop	2b
	SHOT	m13-palette

	mov	ax, 0x0003
	int	0x10
	mov	dx, offset msg
	mov	ah, 0x09
	int	0x21
	SHOT	text

	mov	ax, 0x0013
	int	0x10
	call	picture
	SHOT	m13-again

	mov	ax, 0x0093
	int	0x10
	SHOT	m13-kept

	/* mode 12h: bar b (40 pixels wide, all 480 lines) in colour b, one
	   plane at a time through the map mask */
	mov	ax, 0x0012
	int	0x10
	push	0xa000
	pop	es
	mov	dx, 0x3c4
	mov	cx, 4			/* planes 3 to 0 */
4:	mov	al, 2
	out	dx, al
	inc	dx
	mov	al, 1
	push	cx
	dec	cl
	shl	al, cl
	pop	cx
	out	dx, al			/* map mask: this plane */
	dec	dx
	mov	ah, al			/* the plane's bit */
	xor	di, di
	mov	si, 480
5:	xor	bx, bx			/* bar */
6:	mov	al, 0
	test	bl, ah
	jz	7f
	mov	al, 0xff
7:	stosb				/* 8 pixels of bar bx */
	stosb
	stosb
	stosb
	stosb				/* 40 pixels */
	inc	bx
	cmp	bx, 16
	jb	6b
	dec	si
	jnz	5b
	loop	4b
	mov	al, 2
	out	dx, al
	inc	dx
	mov	al, 0x0f
	out	dx, al
	SHOT	m12

	mov	ax, 0x0093
	int	0x10
	SHOT	m13-over-m12

	mov	ax, 0x0003
	int	0x10
	DONE

/* the picture: pixel (x, y) is (x xor y) + y / 4 */
picture:
	push	es
	push	0xa000
	pop	es
	xor	di, di
	xor	dx, dx			/* y */
1:	xor	bx, bx			/* x */
2:	mov	al, bl
	xor	al, dl
	mov	ah, dl
	shr	ah, 2
	add	al, ah
	stosb
	inc	bx
	cmp	bx, 320
	jb	2b
	inc	dx
	cmp	dx, 200
	jb	1b
	pop	es
	ret

/* the start of the next vertical retrace */
retrace:
	push	ax
	push	dx
	mov	dx, 0x3da
1:	in	al, dx
	test	al, 8
	jnz	1b
2:	in	al, dx
	test	al, 8
	jz	2b
	pop	dx
	pop	ax
	ret

/* di = the box's top left in A000h */
box_at:
	push	ax
	mov	ax, [bx_y]
	imul	ax, ax, 320
	add	ax, [bx_x]
	mov	di, ax
	pop	ax
	ret

/* the 24x24 bytes under the box into save */
box_save:
	push	cx
	push	si
	push	di
	push	ds
	call	box_at
	mov	si, save
	push	es
	pop	ds
	mov	cx, 24
1:	push	cx
	push	di
	mov	cx, 24
2:	mov	al, [di]
	mov	cs:[si], al
	inc	si
	inc	di
	loop	2b
	pop	di
	add	di, 320
	pop	cx
	loop	1b
	pop	ds
	pop	di
	pop	si
	pop	cx
	ret

box_restore:
	push	cx
	push	si
	push	di
	call	box_at
	mov	si, save
	mov	cx, 24
1:	push	cx
	push	di
	mov	cx, 24
	rep	movsb
	pop	di
	add	di, 320
	pop	cx
	loop	1b
	pop	di
	pop	si
	pop	cx
	ret

/* the box: colour 15 with a frame of colour 4 */
box_draw:
	push	ax
	push	cx
	push	di
	call	box_at
	mov	cx, 24
1:	push	cx
	push	di
	mov	al, 15
	cmp	cx, 24
	je	2f
	cmp	cx, 1
	je	2f
	mov	byte ptr es:[di], 4
	inc	di
	push	cx
	mov	cx, 22
	rep	stosb
	pop	cx
	mov	byte ptr es:[di], 4
	jmp	3f
2:	mov	al, 4
	push	cx
	mov	cx, 24
	rep	stosb
	pop	cx
3:	pop	di
	add	di, 320
	pop	cx
	loop	1b
	pop	di
	pop	cx
	pop	ax
	ret

bx_x:	.word	0
bx_y:	.word	0
msg:	.ascii	"MODE13X: text mode between two 256-colour pictures.\r\n$"
