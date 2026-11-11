/*
 * EQU.COM: what the BIOS and the CMOS say about a maths coprocessor, and
 * whether one answers.  Prints one line, on the screen and to port 0x402
 * (tiny386's debug port, its headless build's standard output):
 *
 *   EQU int11=XXXX bda=XXXX cmos14=XX x87=yes|no
 *
 * int11: AX from INT 11h; bda: the word at 0040:0010; cmos14: CMOS 14h;
 * x87: FNINIT then FNSTSW to a word holding 5A5Ah, which a coprocessor sets
 * to 0.  "EQU OFF" (anything after the name) then powers the PC off
 * through tiny386's port 0x8900, for runs of the headless build that end
 * by themselves; "EQU" alone just returns to DOS, for the board.
 *
 * Build: as --32 -o equ.o equ.S
 *        ld -m elf_i386 -Ttext 0x100 --oformat binary -o EQU.COM equ.o
 */
	.intel_syntax noprefix
	.code16
	.text
	.globl _start
_start:
	int	0x11
	mov	di, offset v_int11
	call	hex4
	push	ds
	mov	ax, 0x40
	mov	ds, ax
	mov	ax, word ptr ds:[0x10]
	pop	ds
	mov	di, offset v_bda
	call	hex4
	mov	al, 0x14
	out	0x70, al
	in	al, 0x71
	mov	di, offset v_cmos
	call	hex2
	mov	word ptr [sw], 0x5a5a
	fninit
	fnstsw	word ptr [sw]
	cmp	word ptr [sw], 0
	jne	1f
	mov	byte ptr [v_x87], 'y'
	mov	byte ptr [v_x87 + 1], 'e'
	mov	byte ptr [v_x87 + 2], 's'
1:
	/* to the debug port, up to the '$' */
	mov	si, offset line
2:	lodsb
	cmp	al, '$'
	je	3f
	mov	dx, 0x402
	out	dx, al
	jmp	2b
3:	/* and the screen */
	mov	dx, offset line
	mov	ah, 0x09
	int	0x21
	/* power off, if asked */
	cmp	byte ptr ds:[0x80], 0
	je	5f
	mov	si, offset off
	mov	dx, 0x8900
4:	lodsb
	or	al, al
	jz	5f
	out	dx, al
	jmp	4b
5:	mov	ax, 0x4c00
	int	0x21

/* AX as four hex digits at DI */
hex4:	push	ax
	mov	al, ah
	call	hex2
	pop	ax
	/* fall through */
/* AL as two hex digits at DI */
hex2:	push	ax
	shr	al, 4
	call	nib
	pop	ax
	and	al, 0x0f
nib:	add	al, '0'
	cmp	al, '9'
	jbe	6f
	add	al, 'A' - '0' - 10
6:	mov	byte ptr [di], al
	inc	di
	ret

sw:	.word	0
line:	.ascii	"EQU int11="
v_int11: .ascii	"0000 bda="
v_bda:	.ascii	"0000 cmos14="
v_cmos:	.ascii	"00 x87="
v_x87:	.ascii	"no \r\n$"
off:	.asciz	"Shutdown"
