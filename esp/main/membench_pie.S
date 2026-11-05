/* membench.c's PIE (xespv) kernels, in internal RAM: 64 bytes a turn.
   esp.vld.128 and esp.vst.128 ignore the low 4 bits of the address, so
   destinations (and the source but for copy64u) are 16-byte aligned; the
   callers keep to that.

   v5.2: plain branches, not esp.lp.setup.  ESP-IDF's
   components/freertos/test_apps/freertos/port/test_hwlp_routines.S gives
   the hardware loop's rules: the label names the LAST instruction of the
   body, which is 4-byte aligned, not compressed and not a coprocessor
   instruction, and the body is at least six 32-bit instructions.  A 64-byte
   PIE body breaks the last two, and the study's first kernels had the
   label on the RET (CHECK-mem section 1).  ESP-IDF's notes also put a cost
   on every context switch of a task that uses the hardware loop. */
	.section .iram1.membench, "ax"

	.balign 4
	.globl mb_pie_rd64
/* a0 = source, a1 = blocks of 64 (> 0) */
mb_pie_rd64:
	slli	a1, a1, 6
	add	a1, a1, a0
1:	esp.vld.128.ip	q0, a0, 16
	esp.vld.128.ip	q1, a0, 16
	esp.vld.128.ip	q2, a0, 16
	esp.vld.128.ip	q3, a0, 16
	bne	a0, a1, 1b
	ret

	.balign 4
	.globl mb_pie_fill64
/* a0 = destination, a1 = 32-bit value, a2 = blocks of 64 (> 0) */
mb_pie_fill64:
	addi	sp, sp, -16
	sw	a1, 0(sp)
	mv	a3, sp
	esp.vldbc.32.ip	q0, a3, 0
	addi	sp, sp, 16
	slli	a2, a2, 6
	add	a2, a2, a0
1:	esp.vst.128.ip	q0, a0, 16
	esp.vst.128.ip	q0, a0, 16
	esp.vst.128.ip	q0, a0, 16
	esp.vst.128.ip	q0, a0, 16
	bne	a0, a2, 1b
	ret

	.balign 4
	.globl mb_pie_copy64
/* a0 = destination, a1 = source, both aligned; a2 = blocks of 64 (> 0) */
mb_pie_copy64:
	slli	a2, a2, 6
	add	a2, a2, a0
1:	esp.vld.128.ip	q0, a1, 16
	esp.vld.128.ip	q1, a1, 16
	esp.vld.128.ip	q2, a1, 16
	esp.vld.128.ip	q3, a1, 16
	esp.vst.128.ip	q0, a0, 16
	esp.vst.128.ip	q1, a0, 16
	esp.vst.128.ip	q2, a0, 16
	esp.vst.128.ip	q3, a0, 16
	bne	a0, a2, 1b
	ret

	.balign 4
	.globl mb_pie_copy64u
/* a0 = aligned destination, a1 = source at any byte; a2 = blocks of 64 */
mb_pie_copy64u:
	slli	a2, a2, 6
	add	a2, a2, a0
	esp.ld.128.usar.ip	q0, a1, 16
1:	esp.ld.128.usar.ip	q1, a1, 16
	esp.src.q.qup	q4, q0, q1
	esp.vst.128.ip	q4, a0, 16
	esp.ld.128.usar.ip	q1, a1, 16
	esp.src.q.qup	q4, q0, q1
	esp.vst.128.ip	q4, a0, 16
	esp.ld.128.usar.ip	q1, a1, 16
	esp.src.q.qup	q4, q0, q1
	esp.vst.128.ip	q4, a0, 16
	esp.ld.128.usar.ip	q1, a1, 16
	esp.src.q.qup	q4, q0, q1
	esp.vst.128.ip	q4, a0, 16
	bne	a0, a2, 1b
	ret
