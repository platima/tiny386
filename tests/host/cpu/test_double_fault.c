/*
 * i386.c's faults during the delivery of an exception or an interrupt
 * (v3.8, research\WIN95-CRASH-DECODE.md 2): the 386's double fault, the task
 * gate's error code, the stack an interrupt from ring 3 had switched to put
 * back before the fault is taken, a shutdown that stops the emulator
 * cleanly, page tables outside the guest's memory, and LLDT's checks.
 *
 * tiny386 as released never raised a double fault: when delivering an
 * exception faulted, cpui386_step() called cpu_abort() (the crash screen in
 * Plat's photo, "@ i386.c cpui386_step 5142, abort: -1").  Windows 95's VMM
 * puts a task gate on vector 8 for exactly that case.
 *
 * A small guest (guest_df.S, linked at 0x10000) runs on i386.c with 1 MiB
 * of memory; the tables are laid out here.  Each scenario runs in a child
 * process, so that one that stops the emulator (abort()) is seen as such.
 *
 *   test_double_fault GUEST.BIN SYMBOLS [before]
 *
 * SYMBOLS: "name address" lines (nm) for the guest's labels.  "before":
 * i386.c as v3.7 had it, where the double faults must stop the emulator
 * (the test sees the fault).  Exit status 0 when every check passes.
 */
#define _GNU_SOURCE
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "i386.h"

static int checks, fails;
#define CHECK(c, ...) do { \
	checks++; \
	if (!(c)) { \
		fails++; \
		printf("  FAIL line %d: ", __LINE__); \
	} else { \
		printf("  ok: "); \
	} \
	printf(__VA_ARGS__); \
	printf("\n"); \
} while (0)

/* i386.c's allocations on the board (BUILD_ESP32) */
void *pcmalloc(long size)
{
	return calloc(1, (size_t) size);
}

#define MEM (1024 * 1024)
static uint8_t *mem;
static uint8_t guest[4096];
static size_t guest_len;

static uint32_t sym(const char *syms, const char *name)
{
	FILE *f = fopen(syms, "r");
	char line[256], n[128];
	unsigned long a;
	uint32_t found = 0;
	while (f && fgets(line, sizeof(line), f))
		if (sscanf(line, "%lx %*s %127s", &a, n) == 2 && !strcmp(n, name))
			found = (uint32_t) a;
	if (f)
		fclose(f);
	if (!found) {
		fprintf(stderr, "no symbol %s\n", name);
		exit(2);
	}
	return found;
}

static void put32(uint32_t a, uint32_t v)
{
	memcpy(mem + a, &v, 4);
}

static uint32_t get32(uint32_t a)
{
	uint32_t v;
	memcpy(&v, mem + a, 4);
	return v;
}

static void desc(uint32_t at, uint32_t base, uint32_t limit, uint32_t type_flags)
{
	/* type_flags: access byte in 0xff, flags (G, D) in 0xf00 */
	uint32_t w1 = (limit & 0xffff) | (base << 16);
	uint32_t w2 = ((base >> 16) & 0xff) | ((type_flags & 0xff) << 8) |
		(limit & 0xf0000) | ((type_flags & 0xf00) << 12) | (base & 0xff000000);
	put32(at, w1);
	put32(at + 4, w2);
}

static void gate(int vec, uint32_t sel, uint32_t off, uint32_t type)
{
	put32(0x2000 + vec * 8, (off & 0xffff) | (sel << 16));
	put32(0x2000 + vec * 8 + 4, (off & 0xffff0000) | (type << 8));
}

static uint32_t a_df_task, a_gp, a_other;

/* the machine before the guest starts: GDT, IDT, TSSs, page tables */
static void lay_out(int scenario, bool df_task_gate)
{
	memset(mem, 0, MEM);
	memcpy(mem + 0x10000, guest, guest_len);
	put32(0x600, (uint32_t) scenario);
	/* GDT at 0x1000 */
	desc(0x1000 + 0x08, 0, 0xfffff, 0xC9A);         /* code, ring 0 */
	desc(0x1000 + 0x10, 0, 0xfffff, 0xC92);         /* data, ring 0 */
	desc(0x1000 + 0x18, 0x3000, 0x67, 0x089);       /* the main task's TSS */
	desc(0x1000 + 0x20, 0, 0xfffff, 0xCFA);         /* code, ring 3 */
	desc(0x1000 + 0x28, 0, 0xfffff, 0xCF2);         /* data, ring 3 */
	desc(0x1000 + 0x30, 0x3100, 0x67, 0x089);       /* the double fault's TSS */
	desc(0x1000 + 0x38, 0x6000, 0xff, 0x082);       /* an LDT */
	/* IDT at 0x2000 */
	for (int v = 0; v < 256; v++)
		gate(v, 0x08, a_other, 0x8E);
	if (df_task_gate)
		gate(8, 0x30, 0, 0x85);                 /* task gate, as VMM has it */
	else
		gate(8, 0x08, a_other, 0x8E);           /* an interrupt gate, same stack */
	gate(13, 0x08, a_gp, 0x8E);
	gate(0x40, 0x08, a_other, 0xEE);                /* INT 40h from ring 3 */
	/* the main TSS: its ring 0 stack at the page that is not there */
	put32(0x3000 + 4, 0x8000);
	put32(0x3000 + 8, 0x10);
	/* the double fault's TSS */
	put32(0x3100 + 0x1c, 0x4000);                   /* CR3 */
	put32(0x3100 + 0x20, a_df_task);                /* EIP */
	put32(0x3100 + 0x24, 0x2);                      /* EFLAGS */
	put32(0x3100 + 0x38, 0x9000);                   /* ESP */
	put32(0x3100 + 0x48, 0x10);                     /* ES */
	put32(0x3100 + 0x4c, 0x08);                     /* CS */
	put32(0x3100 + 0x50, 0x10);                     /* SS */
	put32(0x3100 + 0x54, 0x10);                     /* DS */
	put32(0x3100 + 0x58, 0x10);                     /* FS */
	put32(0x3100 + 0x5c, 0x10);                     /* GS */
	/* page directory at 0x4000, one table at 0x5000: the first MiB,
	   user and writable, but page 7 (0x7000 to 0x7fff) not present */
	put32(0x4000, 0x5000 | 7);
	for (int i = 0; i < 256; i++)
		put32(0x5000 + i * 4, i == 7 ? 0 : (uint32_t) (i * 0x1000) | 7);
}

/* the CPU's callbacks: no devices; the PIC gives vector 20h */
static int pic_read(void *p) { (void) p; return 0x20; }
static u8 io_r8(void *o, int a) { (void) o; (void) a; return 0xff; }
static void io_w8(void *o, int a, u8 v) { (void) o; (void) a; (void) v; }
static u16 io_r16(void *o, int a) { (void) o; (void) a; return 0xffff; }
static void io_w16(void *o, int a, u16 v) { (void) o; (void) a; (void) v; }
static u32 io_r32(void *o, int a) { (void) o; (void) a; return 0xffffffff; }
static void io_w32(void *o, int a, u32 v) { (void) o; (void) a; (void) v; }
static u8 mm_r8(void *o, uword a) { (void) o; (void) a; return 0xff; }
static void mm_w8(void *o, uword a, u8 v) { (void) o; (void) a; (void) v; }
static u16 mm_r16(void *o, uword a) { (void) o; (void) a; return 0xffff; }
static void mm_w16(void *o, uword a, u16 v) { (void) o; (void) a; (void) v; }
static u32 mm_r32(void *o, uword a) { (void) o; (void) a; return 0xffffffff; }
static void mm_w32(void *o, uword a, u32 v) { (void) o; (void) a; (void) v; }

/* runs a scenario in a child: 0 if it ran to its end (the results in the
   shared memory), else the signal that stopped it */
static int run(int scenario, bool df_task_gate, bool irq_after_start)
{
	lay_out(scenario, df_task_gate);
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		CPU_CB *cb;
		CPUI386 *cpu = cpui386_new(3, (char *) mem, MEM, &cb);
		cb->pic = NULL;
		cb->pic_read_irq = pic_read;
		cb->io = NULL;
		cb->io_read8 = io_r8;
		cb->io_write8 = io_w8;
		cb->io_read16 = io_r16;
		cb->io_write16 = io_w16;
		cb->io_read32 = io_r32;
		cb->io_write32 = io_w32;
		cb->iomem = NULL;
		cb->iomem_read8 = mm_r8;
		cb->iomem_write8 = mm_w8;
		cb->iomem_read16 = mm_r16;
		cb->iomem_write16 = mm_w16;
		cb->iomem_read32 = mm_r32;
		cb->iomem_write32 = mm_w32;
		cpui386_reset_pm(cpu, 0x10000);
		for (int i = 0; i < 200; i++) {
			cpui386_step(cpu, 64);
			if (irq_after_start && i == 20)
				cpui386_raise_irq(cpu);
		}
		_exit(0);
	}
	int st;
	waitpid(pid, &st, 0);
	if (WIFSIGNALED(st))
		return WTERMSIG(st);
	return WEXITSTATUS(st) ? 1000 + WEXITSTATUS(st) : 0;
}

static void df_results(const char *what, uint32_t at_ip, uint32_t want_cs,
		       uint32_t want_ss, uint32_t want_esp)
{
	CHECK(get32(0x504) == 0x12345678, "%s: the double fault's task ran", what);
	CHECK(get32(0x500) == 0, "%s: with error code 0 on its own stack (%x)", what,
	      get32(0x500));
	CHECK(get32(0x508) == 0x9000, "%s: its stack, the TSS's (ESP %x after the pop)", what,
	      get32(0x508));
	CHECK((get32(0x50c) & 0xffff) == 0x30, "%s: TR is its TSS's selector, 30h (%x)", what,
	      get32(0x50c) & 0xffff);
	CHECK((get32(0x3100) & 0xffff) == 0x18, "%s: its back link the main task, 18h (%x)",
	      what, get32(0x3100) & 0xffff);
	CHECK(get32(0x3000 + 0x20) == at_ip, "%s: the main task saved at the faulting "
	      "instruction, %x (%x)", what, at_ip, get32(0x3000 + 0x20));
	CHECK((get32(0x3000 + 0x4c) & 0xffff) == want_cs &&
	      (get32(0x3000 + 0x50) & 0xffff) == want_ss && get32(0x3000 + 0x38) == want_esp,
	      "%s: saved CS:ESP SS %x:%x %x, %x:%x %x wanted", what,
	      get32(0x3000 + 0x4c) & 0xffff, get32(0x3000 + 0x38), get32(0x3000 + 0x50) & 0xffff,
	      want_cs, want_esp, want_ss);
	CHECK(get32(0x520) == 0, "%s: no other handler ran", what);
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: test_double_fault GUEST.BIN SYMBOLS [before]\n");
		return 2;
	}
	bool before = argc > 3 && !strcmp(argv[3], "before");
	setvbuf(stdout, NULL, _IONBF, 0);
	FILE *g = fopen(argv[1], "rb");
	if (!g) {
		perror(argv[1]);
		return 2;
	}
	guest_len = fread(guest, 1, sizeof(guest), g);
	fclose(g);
	/* shared with the children, so that their results can be read */
	mem = mmap(NULL, MEM, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (mem == MAP_FAILED)
		return 2;
	a_df_task = sym(argv[2], "df_task");
	a_gp = sym(argv[2], "gp_handler");
	a_other = sym(argv[2], "other_handler");
	uint32_t at_push = sym(argv[2], "at_push"), at_movds = sym(argv[2], "at_movds");
	uint32_t at_irqloop = sym(argv[2], "at_irqloop"), at_int = sym(argv[2], "at_int");
	int r;

	printf("== 0: a push off the bottom of a ring 0 stack page (crash report (b)'s case)\n");
	r = run(0, true, false);
	if (before) {
		CHECK(r == SIGABRT, "i386.c before v3.8: the emulator stops there (signal %d, "
		      "abort)", r);
		r = run(6, true, false);
		printf("  i386.c before v3.8, CR3 past the guest's memory: signal %d (%s)\n", r,
		       r == SIGSEGV ? "a load outside phys_mem" : "other");
		goto done;
	}
	CHECK(r == 0, "the emulator goes on (%d)", r);
	df_results("push", at_push, 0x08, 0x10, 0x8000);

	printf("== 1: a #GP whose delivery is a #GP (vector 13 past the IDT's limit)\n");
	r = run(1, true, false);
	CHECK(r == 0, "the emulator goes on (%d)", r);
	df_results("#GP on #GP", at_movds, 0x08, 0x10, 0xA000);

	printf("== 2: an interrupt whose delivery faults on the stack\n");
	r = run(2, true, true);
	CHECK(r == 0, "the emulator goes on (%d)", r);
	df_results("interrupt", at_irqloop, 0x08, 0x10, 0x8000);

	printf("== 3: INT from ring 3, the ring 0 stack not there\n");
	r = run(3, true, false);
	CHECK(r == 0, "the emulator goes on (%d)", r);
	df_results("ring 3", at_int, 0x23, 0x2b, 0xB000);

	printf("== 4: LLDT of a code segment's selector\n");
	r = run(4, true, false);
	CHECK(r == 0 && get32(0x514) == 0x600DF00D && get32(0x510) == 8,
	      "#GP(8) at the LLDT (error code %x, handler %s)", get32(0x510),
	      get32(0x514) == 0x600DF00D ? "ran" : "did not run");

	printf("== 5: LLDT of an LDT descriptor, then of the null selector\n");
	r = run(5, true, false);
	CHECK(r == 0 && get32(0x518) == 0x1D7 && get32(0x514) == 0, "no fault (%d, %x)", r,
	      get32(0x518));

	printf("== 6: CR3 far past the guest's memory\n");
	r = run(6, true, false);
	CHECK(r == SIGABRT, "a shutdown: the emulator stops with abort(), not a load in "
	      "the host's memory (signal %d)", r);

	printf("== 0 again, vector 8 an interrupt gate on the same stack\n");
	r = run(0, false, false);
	CHECK(r == SIGABRT, "a fault while delivering the double fault shuts down: abort() "
	      "(signal %d)", r);
done:
	printf("test_double_fault%s: %d checks, %d failed\n", before ? " (before)" : "",
	       checks, fails);
	return fails ? 1 : 0;
}
