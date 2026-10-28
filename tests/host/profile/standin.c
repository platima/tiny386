/*
 * tests/host/profile/standin.c: the board's profiler (esp/main/profile.c)
 * stood in for on the PC, in the PC repro's headless harness, so that
 * profile_report.py can be tried on a PROFILE.TXT of a real run, and the
 * guest can be shown to run exactly as without it.
 *
 * Linked into the harness with -Wl,--wrap=pc_new,--wrap=pc_step
 * (standin.sh).  Where the board's core 0 reads core 1's PC record, here a
 * POSIX timer on the monotonic clock sends the emulator's thread SIGPROF
 * about PROF_HZ times a second (each gap drawn at random, as on the
 * board), and the handler takes the interrupted PC from the signal's
 * context and the guest's state from the CPU's fields: prof_read_guest()
 * and prof_sample(), the board's own code.  Every 60 s worth of samples,
 * and at the end, PROFILE.TXT is written between two steps with the signal
 * held off, as the board's core 1 writes it with its sampling paused.  The
 * handler only reads the emulator's state, so the run is the run without
 * it.
 *
 * It says nothing on stderr while the PC runs: the harness charges every
 * byte there as the board's UART time, which moves the guest's clock (a
 * first version printed one line at the start, and its Windows boot ended
 * 1.5 M instructions away from the run without it).  Its lines go to
 * PROF_OUT.log instead, and its times are the host's own clock, not the
 * harness's virtual one.
 *
 * Environment: PROF_OUT (default PROFILE.TXT), PROF_ELF_SHA (the
 * binary's SHA-256, for the file's elf_sha256 line), PROF_HZ (2000),
 * PROF_EVERY (samples between two files, default 60 s worth; small, to
 * have files written all through a run).
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <ucontext.h>
#include <sys/syscall.h>

#include "pc.h"
#include "prof_hist.h"
#include "prof_cpu.h"

PC *__real_pc_new(SimpleFBDrawFunc *redraw, void *redraw_data, u8 *fb, PCConfig *conf);
void __real_pc_step(PC *pc);

static struct prof P;
static struct cpui386_prof_fields F;
static void *mem;
static char *buf;
static timer_t timer;
static volatile sig_atomic_t armed, write_due;
static unsigned long hz = 2000;
static uint32_t rng = 0x2545f491u;
static uint64_t every, next_write;     /* samples between files, the next file's */
static struct timespec t0;
static unsigned files;
static uint32_t run_id;                 /* this run's word, as the board draws one */
static uint32_t last_write_us;
static uint64_t paused_us;
static PC *the_pc;

/* the host's clock (the harness gives clock_gettime() its virtual one) */
static void host_now(struct timespec *t)
{
	syscall(SYS_clock_gettime, CLOCK_MONOTONIC, t);
}

static uint64_t us_since(const struct timespec *a)
{
	struct timespec b;
	host_now(&b);
	int64_t us = (int64_t) (b.tv_sec - a->tv_sec) * 1000000 + (b.tv_nsec - a->tv_nsec) / 1000;
	return us > 0 ? (uint64_t) us : 0;
}

/* PROF_OUT.log: never stderr (above) */
static void say(const char *fmt, ...)
{
	static FILE *log;
	char path[512];
	if (!log) {
		snprintf(path, sizeof(path), "%s.log",
			 getenv("PROF_OUT") ? getenv("PROF_OUT") : "PROFILE.TXT");
		log = fopen(path, "a");
		if (!log)
			return;
	}
	va_list ap;
	va_start(ap, fmt);
	vfprintf(log, fmt, ap);
	va_end(ap);
	fflush(log);
}

static void arm_next(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	long period_ns = 1000000000L / (long) hz;
	long ns = period_ns / 2 + (long) (rng % (uint32_t) period_ns);
	struct itimerspec it = { { 0, 0 }, { 0, ns } };
	timer_settime(timer, 0, &it, NULL);
}

static void on_prof(int sig, siginfo_t *si, void *ctx)
{
	(void) sig;
	(void) si;
	if (armed) {
		ucontext_t *uc = ctx;
#if defined(__x86_64__)
		uint32_t pc = (uint32_t) uc->uc_mcontext.gregs[REG_RIP];
#elif defined(__i386__)
		uint32_t pc = (uint32_t) uc->uc_mcontext.gregs[REG_EIP];
#else
		uint32_t pc = 0;
		(void) uc;
#endif
		struct prof_guest g;
		prof_read_guest(&F, &g);
		prof_sample(&P, pc, &g);
		if (P.samples >= next_write) {
			next_write += every;
			write_due = 1;
		}
	}
	arm_next();
}

static void write_file(const char *why)
{
	sigset_t s, old;
	sigemptyset(&s);
	sigaddset(&s, SIGPROF);
	sigprocmask(SIG_BLOCK, &s, &old);
	struct timespec w0;
	host_now(&w0);
	files++;
	const char *path = getenv("PROF_OUT") ? getenv("PROF_OUT") : "PROFILE.TXT";
	struct prof_meta m = {
		.firmware = "standin", .board = "the PC (stand-in)", .idf = "-",
		.elf_sha256 = getenv("PROF_ELF_SHA"), .sampler = "standin-sigprof",
		.ini = "-", .why = why,
		.fast_cpu = the_pc ? cpui386_get_fast(the_pc->cpu) : -1,
		.rate_hz = (uint32_t) hz, .file_no = files, .run = run_id, .cpu_mhz = 0,
		.uptime_us = us_since(&t0), .sampling_us = us_since(&t0), .paused_us = paused_us,
		.last_write_us = last_write_us,
		.guest_insns = the_pc ? (uint64_t) cpui386_get_cycle(the_pc->cpu) : 0,
		.run_us = us_since(&t0), .halt_us = 0,
	};
	FILE *f = fopen(path, "w");
	long r = f ? prof_write(&P, &m, f, buf, 4096) : -1;
	if (f)
		fclose(f);
	last_write_us = (uint32_t) us_since(&w0);
	paused_us += last_write_us;
	say("[standin] %s written (%s): %llu samples, %ld bytes, %u us\n", path, why,
	    (unsigned long long) P.samples, r, last_write_us);
	sigprocmask(SIG_SETMASK, &old, NULL);
}

static void at_end(void)
{
	if (!armed)
		return;
	write_file("end");
	armed = 0;
}

PC *__wrap_pc_new(SimpleFBDrawFunc *redraw, void *redraw_data, u8 *fb, PCConfig *conf)
{
	PC *pc = __real_pc_new(redraw, redraw_data, fb, conf);
	the_pc = pc;
	if (getenv("PROF_HZ"))
		hz = strtoul(getenv("PROF_HZ"), NULL, 10);
	if (hz < 10)
		hz = 10;
	every = 60 * hz;
	if (getenv("PROF_EVERY"))
		every = strtoull(getenv("PROF_EVERY"), NULL, 10);
	if (every < 100)
		every = 100;
	next_write = every;
	struct prof_sizes sz = { 8192, 2048, 4096, 512 };       /* the board's */
	mem = calloc(1, prof_mem_bytes(&sz) + 4096);
	buf = (char *) mem + prof_mem_bytes(&sz);
	prof_init(&P, mem, &sz);
	cpui386_prof_fields(pc->cpu, &F);
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = on_prof;
	sa.sa_flags = SA_SIGINFO | SA_RESTART;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGPROF, &sa, NULL);
	struct sigevent ev;
	memset(&ev, 0, sizeof(ev));
	ev.sigev_notify = SIGEV_THREAD_ID;
	ev.sigev_signo = SIGPROF;
	ev._sigev_un._tid = (pid_t) syscall(SYS_gettid);
	/* the monotonic clock, at high resolution (a timer on the thread's
	   CPU time fires at most once a scheduler tick) */
	if (timer_create(CLOCK_MONOTONIC, &ev, &timer) != 0) {
		say("[standin] timer_create failed\n");
		return pc;
	}
	host_now(&t0);
	run_id = (uint32_t) t0.tv_nsec ^ ((uint32_t) getpid() << 12);
	atexit(at_end);
	armed = 1;
	arm_next();
	say("[standin] sampling at about %lu a second\n", hz);
	return pc;
}

void __wrap_pc_step(PC *pc)
{
	__real_pc_step(pc);
	if (write_due) {
		write_due = 0;
		write_file("timer");
	}
}
