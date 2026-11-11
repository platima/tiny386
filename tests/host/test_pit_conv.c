/*
 * test_pit_conv.c: v5.3 (QW_PITX), i8254.c's two conversions by 32-bit
 * multiplies against the 64-bit multiply and division each stands for,
 * for every 32-bit input:
 *
 *   pit_ticks(t)   (uint32_t) ((uint64_t) t * PIT_FREQ / 1000000), the
 *                  count at each count read, output read, tick and plan
 *   pit_us_up(y)   (uint32_t) (((uint64_t) y + 1) * 1000000 + PIT_FREQ - 1)
 *                  / PIT_FREQ), pit_plan()'s rounded-up time
 *
 * i8254.c is included whole (PIT_C, its path), for its static functions.
 * The 2^32 inputs are split over JOBS child processes.
 *
 *   test_pit_conv [jobs]       exit status 0 when both agree everywhere
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

uint32_t get_uticks(void);
uint32_t get_uticks(void)
{
	return 0;
}

#ifndef PIT_C
#define PIT_C "i8254.c"
#endif
#include PIT_C

#if !QW_PITX
#error "the test is for i8254.c with QW_PITX"
#endif

struct part {
	uint64_t bad;           /* inputs that differ */
	uint32_t first;         /* the first of them */
	uint32_t got, want;
};

static void check_ticks(uint64_t lo, uint64_t hi, struct part *p)
{
	for (uint64_t i = lo; i < hi; i++) {
		uint32_t t = (uint32_t) i;
		uint32_t want = (uint32_t) ((uint64_t) t * PIT_FREQ / 1000000);
		uint32_t got = pit_ticks(t);
		if (got != want && !p->bad++) {
			p->first = t;
			p->got = got;
			p->want = want;
		}
	}
}

static void check_up(uint64_t lo, uint64_t hi, struct part *p)
{
	for (uint64_t i = lo; i < hi; i++) {
		uint32_t y = (uint32_t) i;
		uint32_t want = (uint32_t) ((((uint64_t) y + 1) * 1000000 + PIT_FREQ - 1) / PIT_FREQ);
		uint32_t got = pit_us_up(y);
		if (got != want && !p->bad++) {
			p->first = y;
			p->got = got;
			p->want = want;
		}
	}
}

/* every input of one function, in jobs children; the parts summed */
static int all(const char *name, void (*f)(uint64_t, uint64_t, struct part *), int jobs)
{
	int fds[64][2];
	pid_t pids[64];
	uint64_t n = 1ull << 32, each = (n + jobs - 1) / jobs;
	for (int j = 0; j < jobs; j++) {
		if (pipe(fds[j]) != 0) {
			perror("pipe");
			exit(2);
		}
		pids[j] = fork();
		if (pids[j] < 0) {
			perror("fork");
			exit(2);
		}
		if (pids[j] == 0) {
			struct part p;
			memset(&p, 0, sizeof(p));
			uint64_t lo = each * j, hi = lo + each < n ? lo + each : n;
			f(lo, hi, &p);
			if (write(fds[j][1], &p, sizeof(p)) != (ssize_t) sizeof(p))
				_exit(2);
			_exit(0);
		}
		close(fds[j][1]);
	}
	struct part sum;
	memset(&sum, 0, sizeof(sum));
	int broken = 0;
	for (int j = 0; j < jobs; j++) {
		struct part p;
		int st;
		if (read(fds[j][0], &p, sizeof(p)) != (ssize_t) sizeof(p))
			broken = 1;
		close(fds[j][0]);
		waitpid(pids[j], &st, 0);
		if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
			broken = 1;
		if (p.bad && !sum.bad) {
			sum.first = p.first;
			sum.got = p.got;
			sum.want = p.want;
		}
		sum.bad += p.bad;
	}
	if (broken) {
		printf("  FAIL: %s: a child process failed\n", name);
		return 1;
	}
	if (sum.bad) {
		printf("  FAIL: %s: %llu of the 2^32 inputs differ; the first, %u, gives %u for %u\n",
		       name, (unsigned long long) sum.bad, sum.first, sum.got, sum.want);
		return 1;
	}
	printf("  ok: %s: all 2^32 inputs give what the 64-bit division gives\n", name);
	return 0;
}

int main(int argc, char **argv)
{
	int jobs = argc > 1 ? atoi(argv[1]) : 8;
	if (jobs < 1)
		jobs = 1;
	if (jobs > 64)
		jobs = 64;
	int bad = all("pit_ticks(t), the count in t us", check_ticks, jobs);
	bad |= all("pit_us_up(y), the time the count y + 1 is reached", check_up, jobs);
	printf("pit_conv: %s\n", bad ? "FAILED" : "both exact");
	return bad;
}
