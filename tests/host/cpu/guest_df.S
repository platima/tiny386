/*
 * The guest for test_double_fault.c: 32-bit protected mode, loaded at
 * 0x10000 and started there by cpui386_reset_pm().  The test lays out the
 * GDT (0x1000), the IDT (0x2000), the two TSSs (0x3000, 0x3100) and the page
 * tables (0x4000, 0x5000; page 7, 0x7000 to 0x7FFF, not present) before it
 * starts; the scenario's number is the dword at 0x600.  Assembled by
 * run_cpu.sh with "as --32" and linked at 0x10000 as a flat binary.
 */
	.code32
	.text
	.globl _start
_start:
	lgdt gdtr
	lidt idtr
	mov $0x18, %ax
	ltr %ax
	mov $0x10, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs
	mov %ax, %ss
	mov $0xA000, %esp
	mov $0x4000, %eax
	mov %eax, %cr3
	mov %cr0, %eax
	or $0x80000000, %eax
	mov %eax, %cr0
	jmp 1f
1:	mov 0x600, %eax
	jmp *jtab(, %eax, 4)

jtab:	.long s_stackpf, s_gp_gp, s_irq, s_ring3, s_lldt_bad, s_lldt_ok, s_cr3bad

/* 0: a push off the bottom of a ring 0 stack page: #PF, whose delivery on
   the same stack faults again: a double fault */
s_stackpf:
	mov $0x8000, %esp
	.globl at_push
at_push:
	push %eax
	jmp .

/* 1: a #GP whose delivery is a #GP (vector 13 past the IDT's limit): a
   contributory fault on a contributory one, a double fault */
s_gp_gp:
	lidt idtr_small
	mov $0x1234, %ax
	.globl at_movds
at_movds:
	mov %ax, %ds
	jmp .

/* 2: an external interrupt whose delivery faults on the stack: the #PF,
   whose delivery faults too: a double fault */
s_irq:
	mov $0x8000, %esp
	sti
	.globl at_irqloop
at_irqloop:
	jmp at_irqloop

/* 3: INT from ring 3, the TSS's ring 0 stack (esp0 0x8000) at a page that
   is not there: the push faults after the switch to it; the #PF must be
   taken from ring 3 as the guest was, and its delivery faults the same
   way: a double fault, with the ring 3 state saved in the TSS */
s_ring3:
	pushl $0x2b
	pushl $0xB000
	pushl $0x2
	pushl $0x23
	pushl $r3code
	iret
r3code:
	.globl at_int
at_int:
	int $0x40
	jmp .

/* 4: LLDT of a code segment's selector: #GP(8) at the LLDT */
s_lldt_bad:
	mov $0x08, %ax
	.globl at_lldt
at_lldt:
	lldt %ax
	jmp .

/* 5: LLDT of an LDT descriptor and of the null selector: no fault */
s_lldt_ok:
	mov $0x38, %ax
	lldt %ax
	xor %ax, %ax
	lldt %ax
	movl $0x1D7, 0x518
	hlt
	jmp .

/* 6: CR3 far past the guest's memory: the next instruction fetch finds no
   page directory there, a page fault, and so on to a shutdown, which
   stops the emulator: never a load from outside phys_mem */
s_cr3bad:
	mov $0x7ffff000, %eax
	mov %eax, %cr3
	jmp .

gdtr:	.word 0x3f
	.long 0x1000
idtr:	.word 0x7ff
	.long 0x2000
idtr_small:
	.word 71                /* vectors 0 to 8 */
	.long 0x2000

/* the double fault's task (TSS 0x30): its own stack, 0x9000 */
	.org 0x200
	.globl df_task
df_task:
	pop %eax                /* the error code */
	mov %eax, 0x500
	movl $0x12345678, 0x504
	mov %esp, 0x508
	str %ax
	mov %eax, 0x50c
	hlt
	jmp .

/* #GP (an interrupt gate, ring 0, used where the stack is good) */
	.org 0x300
	.globl gp_handler
gp_handler:
	mov (%esp), %eax
	mov %eax, 0x510
	movl $0x600DF00D, 0x514
	hlt
	jmp .

/* #PF and the interrupts: never reached with a good stack in these
   scenarios */
	.org 0x400
	.globl other_handler
other_handler:
	movl $0xBAD, 0x520
	hlt
	jmp .
