/*
 * The guest for test_cmpxchg.c: CMPXCHG in real mode, 8, 16 and 32 bits,
 * with a register and with memory, equal and not.  Loaded at 0x1000 and
 * started there by a far jump at the reset vector; it sets the dword at
 * 0x7FC when done.  Assembled by run.sh with "as --32" and linked at
 * 0x1000 as a flat binary.  The flags kept are OF SF ZF AF PF CF (8D5h).
 * Results, nine dwords at 0x800:
 *  r0 8 bits, registers, not equal: AL 1, CL 2: flags (OF SF ZF AF PF CF) in the low word, AL after in the high
 *  r1 16 bits, memory, not equal: AX 8000h, [900h] 1: flags, AX after
 *  r2 32 bits, memory, equal: EAX 12345678h = [904h]: flags; r3 [904h] after (ECX's CAFEBABEh)
 *  r4 32 bits, registers, not equal: EAX 5, EDX 3: flags; r5 EAX after
 *  r6 8 bits, memory, not equal: AL 80h, [908h] 7Fh: flags, AL after
 *  r7 16 bits, registers, not equal: AX 0, BX FFFFh: flags, AX after
 *  r8 the flags of the same subtractions done by CMP, packed: CMP AL 1 with 2 (low byte), CMP EAX 5 with 3 (next)
 */
.code16
.globl _start
_start:
	cli
	cld
	xor %ax, %ax
	mov %ax, %ds
	mov %ax, %ss
	mov $0x7000, %sp

	# r0
	mov $0x01, %al
	mov $0x02, %cl
	mov $0x55, %bl
	cmpxchg %bl, %cl
	pushf
	pop %dx
	and $0x8d5, %dx
	movzbl %al, %eax
	shl $16, %eax
	mov %dx, %ax
	mov %eax, 0x800

	# r1
	movw $0x0001, 0x900
	mov $0x8000, %ax
	mov $0x1234, %cx
	cmpxchg %cx, 0x900
	pushf
	pop %dx
	and $0x8d5, %dx
	movzwl %ax, %eax
	shl $16, %eax
	mov %dx, %ax
	mov %eax, 0x804

	# r2, r3
	movl $0x12345678, 0x904
	mov $0x12345678, %eax
	mov $0xcafebabe, %ecx
	cmpxchg %ecx, 0x904
	pushf
	pop %dx
	and $0x8d5, %dx
	movzwl %dx, %edx
	mov %edx, 0x808
	movl 0x904, %eax
	mov %eax, 0x80c

	# r4, r5
	mov $5, %eax
	mov $3, %edx
	mov $0x77, %ecx
	cmpxchg %ecx, %edx
	pushf
	pop %bx
	and $0x8d5, %bx
	movzwl %bx, %ebx
	mov %ebx, 0x810
	mov %eax, 0x814

	# r6
	movb $0x7f, 0x908
	mov $0x80, %al
	mov $0x11, %cl
	cmpxchg %cl, 0x908
	pushf
	pop %dx
	and $0x8d5, %dx
	movzbl %al, %eax
	shl $16, %eax
	mov %dx, %ax
	mov %eax, 0x818

	# r7
	xor %ax, %ax
	mov $0xffff, %bx
	mov $0x2222, %cx
	cmpxchg %cx, %bx
	pushf
	pop %dx
	and $0x8d5, %dx
	movzwl %ax, %eax
	shl $16, %eax
	mov %dx, %ax
	mov %eax, 0x81c

	# r8: CMP for the same pairs
	mov $0x01, %al
	cmp $0x02, %al
	pushf
	pop %dx
	and $0xd5, %dx
	mov $5, %eax
	cmp $3, %eax
	pushf
	pop %bx
	and $0xd5, %bx
	shl $8, %bx
	or %bx, %dx
	movzwl %dx, %edx
	mov %edx, 0x820

	movl $1, 0x7fc
1:	jmp 1b
