/*
 * CMPXCHG's flags (v5.2): the 386 sets them as CMP accumulator,
 * destination does.  i386.c subtracted the other way round, so CF, SF, OF,
 * AF and PF came out wrong whenever the two differed (ZF was right).
 * i386_fast.h hands CMPXCHG to tiny386's own.
 *
 * guest_cmpxchg.S does 8, 16 and 32 bits, with a register and with memory,
 * equal and not; the answers below are QEMU's (7.2, -cpu 486) for the same
 * guest.
 *
 *   test_cmpxchg guest.bin [before]
 *
 * Runs the guest with tiny386's interpreter and with the fast one.  Every
 * answer must be QEMU's; with "before" (a copy of i386.c without the fix)
 * the answers must be the ones of the old subtraction, which shows the
 * test sees the bug.  Exit status 0 when every check passes.
 */
#define I386_FAST
#include "i386.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEMSZ (8u << 20)

void *pcmalloc(long size)
{
	return calloc(1, (size_t) size);
}

static int pic_read(void *o) { return 8; }
uint32_t get_uticks(void) { return 0; }

static const char *what[9] = {
	"8 bits, registers, AL 1 and CL 2: the flags, and AL after",
	"16 bits, memory, AX 8000h and 1: the flags, and AX after",
	"32 bits, memory, equal: the flags",
	"32 bits, memory, equal: the destination after",
	"32 bits, registers, EAX 5 and EDX 3: the flags",
	"32 bits, registers, EAX 5 and EDX 3: EAX after",
	"8 bits, memory, AL 80h and 7Fh: the flags, and AL after",
	"16 bits, registers, AX 0 and BX FFFFh: the flags, and AX after",
	"CMP AL 1 with 2 and CMP EAX 5 with 3: the flags",
};
static const uint32_t want[9] = {
	0x00020095, 0x00010814, 0x00000044, 0xcafebabe, 0x00000000,
	0x00000003, 0x007f0810, 0xffff0011, 0x00000095,
};
/* i386.c's before the fix (destination less accumulator) */
static const uint32_t old[9] = {
	0x00020000, 0x00010881, 0x00000044, 0xcafebabe, 0x00000091,
	0x00000003, 0x007f0885, 0xffff0084, 0x00000095,
};

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: test_cmpxchg guest.bin [before]\n");
		return 2;
	}
	int before = argc > 2 && !strcmp(argv[2], "before");
	static uint8_t guest[0x8000];
	FILE *f = fopen(argv[1], "rb");
	if (!f) {
		perror(argv[1]);
		return 2;
	}
	size_t len = fread(guest, 1, sizeof(guest), f);
	fclose(f);
	if (!len)
		return 2;

	int checks = 0, fails = 0;
	for (int fast = 0; fast <= 1; fast++) {
		char *m = calloc(1, MEMSZ);
		memcpy(m + 0x1000, guest, len);
		static const uint8_t jmp[] = { 0xea, 0x00, 0x10, 0x00, 0x00 };
		memcpy(m + 0xffff0, jmp, sizeof(jmp));
		CPU_CB *cb;
		CPUI386 *cpu = cpui386_new(4, m, MEMSZ, &cb);
		cb->pic_read_irq = pic_read;
		cpui386_set_fast(cpu, fast);
		for (int i = 0; i < 2000 && !*(uint32_t *) (m + 0x7fc); i++)
			cpui386_step(cpu, 1000);
		checks++;
		if (*(uint32_t *) (m + 0x7fc) != 1)
			fails++;
		printf("  %s: the guest ran to its end (fast_cpu %d)\n",
		       *(uint32_t *) (m + 0x7fc) == 1 ? "ok" : "FAIL", fast);
		const uint32_t *r = (const uint32_t *) (m + 0x800);
		for (int i = 0; i < 9; i++) {
			uint32_t w = before ? old[i] : want[i];
			checks++;
			if (r[i] != w)
				fails++;
			printf("  %s: %s %08x (%s %08x), fast_cpu %d\n", r[i] == w ? "ok" : "FAIL",
			       what[i], r[i], before ? "before the fix:" : "QEMU:", w, fast);
		}
		free(m);
	}
	printf("test_cmpxchg: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
