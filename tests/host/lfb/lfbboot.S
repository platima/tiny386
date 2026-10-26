/*
 * LFBBOOT: a guest-level benchmark of the VBE linear frame buffer (v5.0),
 * for the PC harness (pc-repro's headless) or any PC that boots a disk.
 * A boot sector and the code after it, assembled by "as --32" and linked
 * as a flat binary at 0x7C00 (lfbboot.sh): SeaBIOS loads the boot sector,
 * which reads the rest with INT 13h AH=42h; the VGA BIOS sets VBE mode 111h
 * (640x480, 16 bits) with its linear frame buffer (INT 10h 4F01h for the
 * frame buffer's address, the BAR SeaBIOS gave the VGA; 4F02h with bit
 * 14); then 32-bit protected mode, no paging, and one piece of work, chosen
 * by the byte at "phase" (lfbboot.sh writes it into each image):
 *
 *   0  nothing (the boot alone, to take off the others' times)
 *   1  2,000 fills of the whole screen, a REP STOSD each
 *   2  2,000 scrolls of the screen up by a row, a REP MOVSD each
 *   3  200 passes of single 32-bit stores over the screen (a loop of four
 *      instructions a store)
 *   4  200 passes of single 32-bit loads from it
 *
 * Then HLT with interrupts off.  Port 402h (SeaBIOS's debug port, which the
 * harness prints) gets "V" once the mode is set, "D" when the work is done,
 * or "E" and a letter if the BIOS said no.
 */
	.intel_syntax noprefix
	.code16
	.text
	.globl _start
_start:
	cli
	xor ax, ax
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov sp, 0x7c00
	sti
	mov si, offset dap
	mov ah, 0x42
	int 0x13
	jc 1f
	jmp stage2
1:	mov al, 'R'
	jmp fail

/* the 15 sectors after this one, to 0x7E00 */
dap:	.byte 0x10, 0
	.word 15
	.word 0x7e00, 0
	.quad 1

fail:	push ax
	mov dx, 0x402
	mov al, 'E'
	out dx, al
	pop ax
	out dx, al
1:	cli
	hlt
	jmp 1b

	.org 446
	/* one partition over the image, for a BIOS that wants one */
	.byte 0x80, 0, 2, 0, 0xda, 0, 0, 0
	.long 1, 2047
	.org 510
	.word 0xaa55

	.equ MODE, 0x111
	.equ W, 640
	.equ H, 480
	.equ ROW, W * 2
	.equ MODEINFO, 0x6000

stage2:
	mov ax, 0x4f01
	mov cx, MODE
	mov di, MODEINFO
	int 0x10
	cmp ax, 0x004f
	mov al, 'I'
	jne fail
	mov eax, [MODEINFO + 0x28]
	mov [lfb], eax
	mov ax, 0x4f02
	mov bx, 0x4000 | MODE
	int 0x10
	cmp ax, 0x004f
	mov al, 'S'
	jne fail
	mov dx, 0x402
	mov al, 'V'
	out dx, al

	cli
	lgdt [gdtr]
	mov eax, cr0
	or al, 1
	mov cr0, eax
	.byte 0x66, 0xea		/* jmp far 08h:pm32, 32-bit offset */
	.long pm32
	.word 0x08

	.code32
pm32:
	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov esp, 0x7000
	mov ebx, [lfb]
	movzx eax, byte ptr [phase]
	cmp eax, 1
	je fills
	cmp eax, 2
	je scrolls
	cmp eax, 3
	je stores
	cmp eax, 4
	je loads
	jmp finish

fills:
	mov edx, 2000
1:	mov edi, ebx
	mov ecx, W * H / 2
	mov eax, edx
	imul eax, eax, 0x01010101
	rep stosd
	dec edx
	jnz 1b
	jmp finish

scrolls:
	mov edx, 2000
1:	lea esi, [ebx + ROW]
	mov edi, ebx
	mov ecx, (H - 1) * ROW / 4
	rep movsd
	dec edx
	jnz 1b
	jmp finish

stores:
	mov edx, 200
1:	mov edi, ebx
	mov ecx, W * H / 2
	mov eax, edx
2:	mov [edi], eax
	add edi, 4
	dec ecx
	jnz 2b
	dec edx
	jnz 1b
	jmp finish

loads:
	mov edx, 200
	xor eax, eax
1:	mov esi, ebx
	mov ecx, W * H / 2
2:	add eax, [esi]
	add esi, 4
	dec ecx
	jnz 2b
	dec edx
	jnz 1b
	mov [0x5000], eax

finish:
	mov dx, 0x402
	mov al, 'D'
	out dx, al
1:	cli
	hlt
	jmp 1b

	.p2align 3
gdt:
	.quad 0
	.quad 0x00cf9a000000ffff	/* 08h: code, flat */
	.quad 0x00cf92000000ffff	/* 10h: data, flat */
gdtr:
	.word gdtr - gdt - 1
	.long gdt
lfb:	.long 0
	.globl phase
phase:	.byte 0
