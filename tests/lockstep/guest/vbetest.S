/*
 * tests/lockstep: VBETEST.COM, a DOS program for the lockstep tester's vbe
 * workload.  It turns on a VBE mode with the linear frame buffer through
 * the emulated VGA's own registers (Bochs DISPI, ports 1CEh and 1CFh),
 * finds the frame buffer at the VGA's PCI BAR 0, and stores into it every
 * way the CPU can: bytes, words and dwords, aligned and not, across 4 KiB
 * pages, REP STOS and REP MOVS forwards and backwards, from RAM, within the
 * frame buffer and back into RAM, read-modify-write instructions, reads,
 * and the last bytes of video memory and past them.  Then it does it all
 * again at 640x480 in 32 bits.  None of it checks itself: the tester
 * compares two cores' records of it (video memory, the drawn picture, RAM).
 *
 * Real mode with 32-bit addresses (DS, ES and FS 0): tiny386 does not check
 * segment limits, so this reaches the frame buffer without a switch to
 * protected mode.  The stores go through the same store path as any
 * guest's.  Marks at 0000:04F0 (B1h ... B9h each pass, BFh at the end; the
 * pass at 0000:04F1), as BENCH.COM marks its pieces.
 *
 * Build: as --32 -o vbetest.o vbetest.S; ld -m elf_i386 -Ttext 0x100
 *        --oformat binary -o VBETEST.COM vbetest.o
 *
 * Offered under the licence of tiny386 (BSD 3-clause).
 */
	.code16
	.text
	.globl _start

.macro DISPI idx, val
	movw $0x1ce, %dx
	movw $\idx, %ax
	outw %ax, %dx
	movw $0x1cf, %dx
	movw $\val, %ax
	outw %ax, %dx
.endm

.macro MARK m
	movb $\m, 0x4f0
.endm

/* RAM the program uses, all below 640 KiB or at 4 MiB (bit 20 clear) */
	.set LFB,   0x500           /* the frame buffer's address */
	.set VRAM,  0x504           /* video memory, bytes */
	.set SEED,  0x508
	.set PASS,  0x4f1
	.set SUMS,  0x520           /* what the reads gave, a dword each */
	.set BUF,   0x400000        /* a source in RAM for MOVS */
	.set BACK,  0x480000        /* a copy of the frame buffer in RAM */

_start:
	cld
	xorw %ax, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %fs
	MARK 0xb1
	/* the emulated VGA is PCI 00:00.0; its BAR 0 is the frame buffer */
	movl $0x80000010, %eax
	movw $0xcf8, %dx
	outl %eax, %dx
	movw $0xcfc, %dx
	inl %dx, %eax
	andl $0xfffffff0, %eax
	movl %eax, LFB
	/* video memory in 64 KiB (DISPI index 0Ah) */
	movw $0x1ce, %dx
	movw $0xa, %ax
	outw %ax, %dx
	movw $0x1cf, %dx
	inw %dx, %ax
	movzwl %ax, %eax
	shll $16, %eax
	movl %eax, VRAM
	movl $0x12345678, SEED
	movb $0, PASS

	/* a pattern in RAM for the copies */
	movl $BUF, %edi
	movl $65536, %ecx
	movl $0x9e3779b9, %eax
1:	movl %eax, (%edi)
	roll $5, %eax
	addl %ecx, %eax
	addl $4, %edi
	decl %ecx
	jnz 1b

	/* 800x480, 16 bits a pixel, linear frame buffer */
	DISPI 4, 0
	DISPI 1, 800
	DISPI 2, 480
	DISPI 3, 16
	DISPI 4, 0x41
	movl $(800 * 480 * 2), %esi
	call passes

	/* 640x480, 32 bits a pixel */
	DISPI 4, 0
	DISPI 1, 640
	DISPI 2, 480
	DISPI 3, 32
	DISPI 4, 0x41
	movl $(640 * 480 * 4), %esi
	call passes

	MARK 0xbf
2:	hlt
	jmp 2b

/* four passes over a frame of ESI bytes */
passes:
	movl %esi, 0x50c
	movw $4, %cx
3:	pushw %cx
	call frame
	incb PASS
	addl $0x01020305, SEED
	popw %cx
	loop 3b
	ret

/* one pass; the frame's size at 0x50C */
frame:
	/* dwords, every one of the frame */
	MARK 0xb2
	movl LFB, %edi
	movl 0x50c, %ecx
	shrl $2, %ecx
	movl SEED, %eax
1:	movl %eax, %fs:(%edi)
	roll $3, %eax
	xorl %ecx, %eax
	addl $4, %edi
	decl %ecx
	jnz 1b

	/* words at odd addresses, every 6 bytes (some across a page) */
	MARK 0xb3
	movl LFB, %edi
	incl %edi
	movl 0x50c, %ecx
	movl $0, %edx
	movl %ecx, %eax
	movl $6, %ebx
	divl %ebx
	movl %eax, %ecx
	decl %ecx
	movw SEED, %ax
1:	movw %ax, %fs:(%edi)
	addw $0x0841, %ax
	addl $6, %edi
	decl %ecx
	jnz 1b
	/* at every page's end: a word across it, dwords across it at -3, -2, -1 */
	movl LFB, %edi
	addl $4096, %edi
	movl 0x50c, %ecx
	shrl $12, %ecx
	decl %ecx
	movl SEED, %eax
1:	movw %ax, %fs:-1(%edi)
	movl %eax, %fs:-3(%edi)
	notl %eax
	movl %eax, %fs:-2(%edi)
	rorl $7, %eax
	movl %eax, %fs:-1(%edi)
	addl $4096, %edi
	decl %ecx
	jnz 1b

	/* bytes, every third */
	MARK 0xb4
	movl LFB, %edi
	movl 0x50c, %ecx
	movl $0, %edx
	movl %ecx, %eax
	movl $3, %ebx
	divl %ebx
	movl %eax, %ecx
	movb SEED, %al
1:	movb %al, %fs:(%edi)
	addb $37, %al
	addl $3, %edi
	decl %ecx
	jnz 1b

	/* REP STOS: dwords, words at an odd address, bytes, and dwords backwards */
	MARK 0xb5
	movl LFB, %edi
	addl $(1600 * 100), %edi
	movl $(1600 * 20 / 4), %ecx
	movl SEED, %eax
	addr32 rep stosl
	movl LFB, %edi
	addl $(1600 * 130 + 1), %edi
	movl $(1600 * 10 / 2), %ecx
	notl %eax
	addr32 rep stosw
	movl LFB, %edi
	addl $(1600 * 150 + 3), %edi
	movl $5003, %ecx
	addr32 rep stosb
	std
	movl LFB, %edi
	addl $(1600 * 200 - 4), %edi
	movl $4099, %ecx
	roll $11, %eax
	addr32 rep stosl
	cld

	/* REP MOVS: RAM to the frame buffer (aligned, then not), within it,
	   back into RAM, and backwards */
	MARK 0xb6
	movl $BUF, %esi
	movl LFB, %edi
	addl $(1600 * 240), %edi
	movl $(1600 * 60 / 4), %ecx
	addr32 rep movsl
	movl $(BUF + 1), %esi
	movl LFB, %edi
	addl $(1600 * 300 + 1), %edi
	movl $(1600 * 10 / 2 + 3), %ecx
	addr32 rep movsw
	movl LFB, %esi
	addl $(1600 * 10), %esi
	movl LFB, %edi
	movl $(1600 * 50 / 4), %ecx
	addr32 rep movsl
	movl LFB, %esi
	movl $BACK, %edi
	movl $16384, %ecx
	addr32 rep movsl
	std
	movl $(BUF + 65536 - 4), %esi
	movl LFB, %edi
	addl $(1600 * 400 - 4), %edi
	movl $3000, %ecx
	addr32 rep movsl
	movl LFB, %esi
	addl $(1600 * 420 + 2), %esi
	movl LFB, %edi
	addl $(1600 * 440 + 2), %edi
	movl $2001, %ecx
	addr32 rep movsw
	cld

	/* read-modify-write, every 68 bytes (now and then across a page) */
	MARK 0xb7
	movl LFB, %edi
	addl $2, %edi
	movl 0x50c, %ecx
	shrl $7, %ecx
	movl SEED, %eax
1:	addl %eax, %fs:(%edi)
	xorw $0x5a5a, %fs:4(%edi)
	incb %fs:9(%edi)
	negb %fs:13(%edi)
	notl %fs:16(%edi)
	shll $1, %fs:24(%edi)
	orw %ax, %fs:31(%edi)
	xchgl %eax, %fs:40(%edi)
	subl $3, %fs:63(%edi)
	addl $68, %edi
	decl %ecx
	jnz 1b

	/* reads: a sum of every dword, LODS, SCAS and CMPS, into RAM */
	MARK 0xb8
	movl LFB, %esi
	movl 0x50c, %ecx
	shrl $2, %ecx
	xorl %edx, %edx
1:	addl %fs:(%esi), %edx
	roll $1, %edx
	addl $4, %esi
	decl %ecx
	jnz 1b
	movzbl PASS, %ebx
	movl %edx, SUMS(,%ebx,8)
	movl LFB, %esi
	addl $1001, %esi
	movl $4000, %ecx
	xorl %edx, %edx
1:	addr32 lodsw
	addw %ax, %dx
	decl %ecx
	jnz 1b
	movl LFB, %edi
	movl $70000, %ecx
	movl %fs:4000(%edi), %eax
	addr32 repne scasl
	movl LFB, %esi
	movl LFB, %edi
	addl $8, %edi
	movl $9000, %ecx
	addr32 repe cmpsw
	addl %ecx, %edx
	movl %edx, SUMS+4(,%ebx,8)

	/* the end of video memory: stores at its last bytes, across its end
	   and past it, then reads of the same */
	MARK 0xb9
	movl LFB, %edi
	addl VRAM, %edi
	movl SEED, %eax
	movb %al, %fs:-1(%edi)
	movw %ax, %fs:-2(%edi)
	movw %ax, %fs:-1(%edi)
	movl %eax, %fs:-4(%edi)
	movl %eax, %fs:-3(%edi)
	movl %eax, %fs:-1(%edi)
	movl %eax, %fs:(%edi)
	movl %eax, %fs:4096(%edi)
	movl %fs:-8(%edi), %edx
	addl %fs:-4(%edi), %edx
	addl %fs:-3(%edi), %edx
	addl %fs:-2(%edi), %edx
	addl %fs:-1(%edi), %edx
	addl %fs:(%edi), %edx
	movl %edx, 0x5f0
	ret
