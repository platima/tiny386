/*
 * BOUND with a register operand (v5.3, the review of batch 4): 62h with
 * mod 3 has no memory operand, and the 386 raises #UD for it.  i386.c
 * decoded it as GvM does, gave BOUND_helper the register's value as an
 * address with no segment (curr_seg -1), and translate() asserted that it
 * had one; the boards keep assertions, so a guest that ran those bytes
 * stopped the emulator.  i386_fast.h hands BOUND to tiny386's own.
 *
 * The guest (real mode, written here byte by byte): BOUND AX,BX and BOUND
 * EAX,EBX with 66h, each of which must raise #UD; BOUND AX,[500h] with AX
 * in the bounds (no exception) and above them (#BR, vector 5).  The two
 * handlers count their exceptions and return past the instruction (its
 * length at [7F4h]).
 *
 *   test_bound [before]
 *
 * Runs the guest with tiny386's interpreter and with the fast one.  With
 * "before" (a copy of i386.c with GvM's decoding put back) the guest must
 * not get to its end the same way: the test is run in a child, which the
 * assertion ends.  Exit status 0 when every check passes.
 */
#define I386_FAST
#include "i386.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MEMSZ (2u << 20)

void *pcmalloc(long size)
{
	return calloc(1, (size_t) size);
}

static int pic_read(void *o) { return 8; }
uint32_t get_uticks(void) { return 0; }

static const uint8_t code[] = {
	0xfa,                                   /* 1000: CLI */
	0x31, 0xc0,                             /* XOR AX,AX */
	0x8e, 0xd8,                             /* MOV DS,AX */
	0x8e, 0xd0,                             /* MOV SS,AX */
	0xbc, 0x00, 0x70,                       /* MOV SP,7000h */
	0xc7, 0x06, 0xf4, 0x07, 0x02, 0x00,     /* MOV WORD [7F4h],2 */
	0x62, 0xc3,                             /* BOUND AX,BX: #UD */
	0xc7, 0x06, 0xf4, 0x07, 0x03, 0x00,     /* MOV WORD [7F4h],3 */
	0x66, 0x62, 0xc3,                       /* BOUND EAX,EBX: #UD */
	0xb8, 0x05, 0x00,                       /* MOV AX,5 */
	0x62, 0x06, 0x00, 0x05,                 /* BOUND AX,[500h]: 1 to 10, no exception */
	0xb8, 0x20, 0x00,                       /* MOV AX,20h */
	0xc7, 0x06, 0xf4, 0x07, 0x04, 0x00,     /* MOV WORD [7F4h],4 */
	0x62, 0x06, 0x00, 0x05,                 /* BOUND AX,[500h]: #BR */
	0xc7, 0x06, 0xfc, 0x07, 0x01, 0x00,     /* MOV WORD [7FCh],1: the end */
	0xf4,                                   /* HLT */
	0xeb, 0xfe,                             /* JMP $ */
};
static const uint8_t handlers[] = {
	0xff, 0x06, 0xf8, 0x07,                 /* 2000: #UD: INC WORD [7F8h] */
	0xeb, 0x04,                             /* JMP 200Ah */
	0xff, 0x06, 0xf6, 0x07,                 /* 2006: #BR: INC WORD [7F6h] */
	0x55,                                   /* 200A: PUSH BP */
	0x89, 0xe5,                             /* MOV BP,SP */
	0x50,                                   /* PUSH AX */
	0xa1, 0xf4, 0x07,                       /* MOV AX,[7F4h] */
	0x01, 0x46, 0x02,                       /* ADD [BP+2],AX: past the instruction */
	0x58,                                   /* POP AX */
	0x5d,                                   /* POP BP */
	0xcf,                                   /* IRET */
};

/* the guest's three counts after it ran: #UD, #BR, its end (or -1 when it
   never got there) */
static int run(int fast, unsigned *ud, unsigned *br)
{
	uint8_t *m = calloc(1, MEMSZ);
	memcpy(m + 0x1000, code, sizeof(code));
	memcpy(m + 0x2000, handlers, sizeof(handlers));
	static const uint8_t jmp[] = { 0xea, 0x00, 0x10, 0x00, 0x00 };
	memcpy(m + 0xffff0, jmp, sizeof(jmp));
	m[5 * 4] = 0x06; m[5 * 4 + 1] = 0x20;   /* vector 5 (#BR): 0000:2006h */
	m[6 * 4] = 0x00; m[6 * 4 + 1] = 0x20;   /* vector 6 (#UD): 0000:2000h */
	m[0x500] = 1; m[0x502] = 10;            /* the bounds */
	CPU_CB *cb;
	CPUI386 *cpu = cpui386_new(4, (char *) m, MEMSZ, &cb);
	cb->pic_read_irq = pic_read;
	cpui386_set_fast(cpu, fast);
	for (int i = 0; i < 200 && !m[0x7fc]; i++)
		cpui386_step(cpu, 10);
	*ud = m[0x7f8] | m[0x7f9] << 8;
	*br = m[0x7f6] | m[0x7f7] << 8;
	int end = m[0x7fc];
	free(m);
	return end;
}

int main(int argc, char **argv)
{
	int before = argc > 1 && !strcmp(argv[1], "before");
	int checks = 0, fails = 0;
	for (int fast = 0; fast <= 1; fast++) {
		/* in a child, so that an assertion's abort is a result */
		int p[2];
		if (pipe(p))
			return 2;
		fflush(stdout);
		pid_t pid = fork();
		if (pid == 0) {
			unsigned r[3];
			r[2] = run(fast, &r[0], &r[1]);
			if (write(p[1], r, sizeof(r)) != sizeof(r))
				_exit(3);
			_exit(0);
		}
		close(p[1]);
		unsigned r[3];
		ssize_t n = read(p[0], r, sizeof(r));
		close(p[0]);
		int ws;
		waitpid(pid, &ws, 0);
		int ok = n == sizeof(r) && WIFEXITED(ws) && WEXITSTATUS(ws) == 0;
		if (!ok) {
			printf("  %s: the guest did not finish (fast_cpu %d): %s %d\n", before ? "ok" : "FAIL", fast,
			       WIFSIGNALED(ws) ? "signal" : "exit status",
			       WIFSIGNALED(ws) ? WTERMSIG(ws) : WEXITSTATUS(ws));
			checks++;
			if (!before)
				fails++;
			continue;
		}
		int right = r[2] == 1 && r[0] == 2 && r[1] == 1;
		checks++;
		if (right == before)
			fails++;
		printf("  %s: fast_cpu %d: #UD %u times (2 wanted), #BR %u (1), the end %s\n",
		       right != before ? "ok" : "FAIL", fast, r[0], r[1], r[2] == 1 ? "reached" : "not reached");
	}
	printf("test_bound: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
