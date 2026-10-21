/*
 * --hostbench: what the things an emulator does all day cost on this
 * machine.
 *
 * Two questions decide how fast tiny386 can be on a board like the
 * ESP32-S31-Korvo-1, and neither can be answered without the board:
 *
 *  - What does a store cost beside a load?  With the LCD on, the port runs
 *    its cache write-through, and every store goes out to the PSRAM.
 *  - Does the processor do a load or a store at an address that is not
 *    aligned by itself, or does it trap and have the firmware do it?  x86
 *    code is full of them, and the emulator can be built either to pass
 *    them on to the host or to take them apart first (I386_ALIGNED_ONLY).
 *
 * It touches nothing but 2 MiB of its own memory and takes some seconds.
 *
 * After tools/storecost.c of the Wolfenstein 3D port for the same board.
 * Offered under the licence of tiny386 (BSD 3-clause, see LICENSE).
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

#include "hostbench.h"

#define KB 1024
#define MB (1024 * 1024)

static uint8_t *mem;
static uint32_t sink;

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* something the compiler cannot see through, so that a loop stays a loop
   and a pointer keeps the alignment it was given */
#define OPAQUE(p) __asm__ volatile ("" : "+r" (p))

/*
 * Each of these does its thing `count` times, `step` bytes on each time,
 * over `size` bytes that begin `off` bytes into the memory.
 */
#define WALK(type, body) \
	uint32_t sum = 0; \
	size_t at = 0; \
	long i; \
	for (i = 0; i < count; i++) { \
		type *p = (type *) (void *) (mem + off + at); \
		OPAQUE(p); \
		body; \
		at += step; \
		if (at >= size) \
			at = 0; \
	} \
	return sum;

#define TEST(name) \
	static uint32_t __attribute__((noinline)) \
	name(size_t off, size_t size, size_t step, long count)

TEST(nothing)	{ WALK(uint8_t,  sum += (uint32_t) (uintptr_t) p) }
TEST(load8)	{ WALK(uint8_t,  sum += *p) }
TEST(load16)	{ WALK(uint16_t, sum += *p) }
TEST(load32)	{ WALK(uint32_t, sum += *p) }
TEST(store8)	{ WALK(uint8_t,  *p = (uint8_t) i) }
TEST(store16)	{ WALK(uint16_t, *p = (uint16_t) i) }
TEST(store32)	{ WALK(uint32_t, *p = (uint32_t) i) }

TEST(read_clock)
{
	struct timespec ts;
	long i;

	(void) off; (void) size; (void) step;
	for (i = 0; i < count; i++)
		clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_nsec;
}

TEST(short_nap)
{
	long i;

	(void) off; (void) size; (void) step;
	for (i = 0; i < count; i++)
		usleep(1);
	return 0;
}

typedef uint32_t (*test_fn)(size_t, size_t, size_t, long);

static sigjmp_buf refused;

static void on_refused(int sig)
{
	siglongjmp(refused, sig);
}

/* Runs it for long enough to time and prints the time for one.  Returns
   that, or -1 if the machine would not do it at all. */
static double run(const char *what, test_fn fn, size_t off, size_t size,
		  size_t step, long count, long most)
{
	double t, each;

	if (sigsetjmp(refused, 1)) {
		printf("  %-46s  refused\n", what);
		return -1;
	}
	/* once over the whole piece, untimed */
	sink += fn(off, size, step, step ? (long) (size / step) + 1 : 1);
	for (;;) {
		t = now();
		sink += fn(off, size, step, count);
		t = now() - t;
		if (t >= 0.25 || count >= most)
			break;
		/* aim past a quarter of a second, with at most 16 times
		   as many */
		if (t < 0.3 / 16)
			count *= 16;
		else
			count = (long) (count * 0.3 / t) + 1;
		if (count > most)
			count = most;
	}
	each = t / count * 1e9;
	if (each >= 100000)
		printf("  %-46s %9.2f ms\n", what, each / 1e6);
	else
		printf("  %-46s %9.1f ns\n", what, each);
	fflush(stdout);
	return each;
}

static double over(double with, double without)
{
	return with > without ? with - without : 0;
}

int hostbench(void)
{
	static const int caught[] = { SIGBUS, SIGSEGV, SIGILL };
	const long many = 400000000L;
	double loop, la, lu, sa, su, s8, big;
	size_t i;

	mem = mmap(NULL, 2 * MB + 4 * KB, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mem == MAP_FAILED) {
		puts("tiny386: no 2 MiB of memory to try things in");
		return 1;
	}
	memset(mem, 0x5a, 2 * MB + 4 * KB);
	for (i = 0; i < sizeof(caught) / sizeof(caught[0]); i++)
		signal(caught[i], on_refused);

	puts("tiny386: what one of each costs on this machine");
	puts("");
	puts("the loop that all of these are, with nothing in it");
	loop = run("one turn", nothing, 0, 8 * KB, 4, 20000, many);
	puts("");
	puts("in the same 8 KB over and over, aligned");
	run("load of 8 bits", load8, 0, 8 * KB, 1, 20000, many);
	run("load of 16 bits", load16, 0, 8 * KB, 2, 20000, many);
	la = run("load of 32 bits", load32, 0, 8 * KB, 4, 20000, many);
	s8 = run("store of 8 bits", store8, 0, 8 * KB, 1, 20000, many);
	run("store of 16 bits", store16, 0, 8 * KB, 2, 20000, many);
	sa = run("store of 32 bits", store32, 0, 8 * KB, 4, 20000, many);
	puts("");
	puts("the same, one byte off");
	run("load of 16 bits", load16, 1, 8 * KB, 2, 20000, many);
	lu = run("load of 32 bits", load32, 1, 8 * KB, 4, 20000, many);
	run("store of 16 bits", store16, 1, 8 * KB, 2, 20000, many);
	su = run("store of 32 bits", store32, 1, 8 * KB, 4, 20000, many);
	puts("");
	puts("through 2 MiB, aligned");
	run("load of 32 bits", load32, 0, 2 * MB, 4, 20000, many);
	run("load of 8 bits, one in every 64", load8, 0, 2 * MB, 64, 20000, many);
	big = run("store of 32 bits", store32, 0, 2 * MB, 4, 20000, many);
	run("store of 8 bits", store8, 0, 2 * MB, 1, 20000, many);
	puts("");
	puts("and two things the emulator asks the kernel for");
	run("reading the clock", read_clock, 0, 0, 0, 2000, 2000000);
	run("the shortest sleep (a halted guest's)", short_nap, 0, 0, 0, 4, 400);

	puts("");
	printf("Less the loop itself: a load of 32 bits %.0f ns, a store %.0f ns"
	       " (%.0f ns through 2 MiB),\na store of 8 bits %.0f ns.\n",
	       over(la, loop), over(sa, loop), over(big, loop), over(s8, loop));
	if (lu < 0 || su < 0) {
		puts("This machine refuses loads or stores that are not aligned."
		     " Only a build that\ntakes them apart first can run here.");
	} else {
		/* whole turns, loop and all: on a fast machine there is
		   nothing left to compare once the loop is taken off */
		double lr = lu / la;
		double sr = su / sa;

		printf("One byte off, a turn with a load of 32 bits takes %.1f"
		       " times as long and one with a\nstore %.1f times.\n",
		       lr, sr);
		if (lr > 8 || sr > 8)
			puts("That is a trap for each one: use the build that"
			     " takes them apart first.");
		else
			puts("The processor does them by itself: the build that"
			     " leaves them to it may be the\nfaster one.");
	}
#ifdef I386_ALIGNED_ONLY
	puts("This build takes them apart first.");
#else
	puts("This build leaves them to the processor.");
#endif
	/* so that the sums are used */
	if (*(volatile uint32_t *) &sink == 12345)
		fflush(stdout);
	return 0;
}
