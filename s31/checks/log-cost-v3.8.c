/*
 * The cost of the emulator's log lines, before and after v3.8's limit on
 * repeated lines and the UART queue: esp/main/console.c built for the PC
 * (CONSOLE_HOST), fed with
 *
 *   boot   the emulator's own lines from a Windows 95 OSR2 boot to the
 *          desktop in the PC harness (the guest BIOS's port 0x402 text and
 *          tiny386's fprintf(stderr) lines; the harness's own [harness]
 *          lines taken out), each at the harness's last beat time;
 *   storm  a guest stuck on an unknown port and a bad selector for 60 s:
 *          "inw 0xa20 <= 0xffff" and a read_desc line by turns, 100,000
 *          lines, one every 0.6 ms of the clock.
 *
 * For each, with the limit off (every line, as v3.7 and fix 1 of v3.8) and
 * on: the lines and bytes that reach the UART (the same lines go to the
 * ring and the panel's text); and the PC's time a line in console.c (the
 * median of 7 runs).  The UART's own time is not measured here: at 115200
 * baud it is 86.8 us a byte (the audit, F10), on the emulator's core
 * before the queue, on core 0 after it.
 *
 *   bench_log BOOTLOG
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "console.h"

void console_host_set_uart(FILE *f);
void console_host_tee_line(const char *s);
void console_host_set_ms(uint32_t ms);

struct line { uint32_t ms; char *s; };
static struct line *lines;
static int nlines;

static uint64_t ns_now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t) t.tv_sec * 1000000000u + t.tv_nsec;
}

static void add(uint32_t ms, const char *s)
{
	lines = realloc(lines, (nlines + 1) * sizeof(*lines));
	lines[nlines].ms = ms;
	lines[nlines].s = strdup(s);
	nlines++;
}

/* one pass of every line through the tee; returns ns, and the bytes the
   UART file got and the lines held back */
static uint64_t pass(bool limit, FILE *uart, long *bytes, unsigned *held)
{
	static uint16_t fb[800 * 480];
	console_init(fb, 800, 480, NULL);       /* the panel, as on the Korvo-1 */
	console_release_display();              /* the PC's picture has it */
	console_host_set_uart(uart);
	rewind(uart);
	console_host_set_ms(lines[0].ms);
	console_log_limit(limit);
	unsigned h0, d0;
	console_log_stats(&h0, &d0);
	uint64_t t0 = ns_now();
	for (int i = 0; i < nlines; i++) {
		console_host_set_ms(lines[i].ms);
		console_host_tee_line(lines[i].s);
		if (limit && (i & 63) == 0)
			console_log_tick();     /* the speed screen's poll */
	}
	uint64_t t = ns_now() - t0;
	console_host_set_ms(lines[nlines - 1].ms + 5000);
	console_log_tick();                     /* the last counts */
	console_log_limit(false);
	fflush(uart);
	*bytes = ftell(uart);
	unsigned h1, d1;
	console_log_stats(&h1, &d1);
	*held = h1 - h0;
	return t;
}

static int cmp64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *) a, y = *(const uint64_t *) b;
	return x < y ? -1 : x > y;
}

static void report(const char *name)
{
	long in_bytes = 0;
	for (int i = 0; i < nlines; i++)
		in_bytes += strlen(lines[i].s) + 2;     /* CR LF on the UART */
	printf("== %s: %d lines, %ld bytes with CR LF, over %.1f s of the clock\n", name,
	       nlines, in_bytes, (lines[nlines - 1].ms - lines[0].ms) / 1000.0);
	FILE *uart = tmpfile();
	for (int limit = 0; limit < 2; limit++) {
		uint64_t t[7];
		long bytes = 0;
		unsigned held = 0;
		for (int r = 0; r < 7; r++)
			t[r] = pass(limit, uart, &bytes, &held);
		qsort(t, 7, sizeof(t[0]), cmp64);
		/* what the UART got: its lines (the count lines among them),
		   and its bytes as sent, each newline as CR LF */
		char *got = malloc(bytes + 1);
		rewind(uart);
		long n = (long) fread(got, 1, bytes, uart);
		got[n] = 0;
		long out_lines = 0, counts = 0;
		for (long i = 0; i < n; i++)
			out_lines += got[i] == 10;
		for (char *p = got; (p = strstr(p, "(not shown: ")); p++)
			counts++;
		free(got);
		long wire = n + out_lines;
		printf("  limit %s: %ld lines out (%u of the %d held back, %ld count lines added), "
		       "%ld bytes to the UART; on the PC %.0f ns a line in console.c (median of 7: "
		       "%.2f ms in all); at 86.8 us a byte the UART takes %.2f s\n",
		       limit ? "on " : "off", out_lines, held, nlines, counts, wire,
		       (double) t[3] / nlines, t[3] / 1e6, wire * 86.8e-6);
	}
	fclose(uart);
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: bench_log BOOTLOG\n");
		return 2;
	}
	/* the boot: the harness's log without its own lines; the clock from
	   its beat lines (t=...) */
	FILE *f = fopen(argv[1], "r");
	if (!f) {
		perror(argv[1]);
		return 2;
	}
	char buf[1024];
	uint32_t ms = 0;
	while (fgets(buf, sizeof(buf), f)) {
		buf[strcspn(buf, "\r\n")] = 0;
		char *h = strstr(buf, "[harness]");
		if (h) {
			double t;
			char *b = strstr(h, " t=");
			if (b && sscanf(b, " t=%lf", &t) == 1)
				ms = (uint32_t) (t * 1000);
			*h = 0;                 /* a line a beat cut: its first part */
			if (!buf[0])
				continue;
		}
		add(ms, buf);
	}
	fclose(f);
	report("boot (Windows 95 OSR2 to the desktop, PC harness)");
	for (int i = 0; i < nlines; i++)
		free(lines[i].s);
	free(lines);
	lines = NULL;
	nlines = 0;

	/* the storm */
	for (int i = 0; i < 100000; i++) {
		char s[160];
		if (i & 1)
			snprintf(s, sizeof(s), "read_desc: sel 77bc base 0 limit 0 off 77b8 at "
				 "0028:c0039dc8 (%d)", i / 2 + 1);
		else
			snprintf(s, sizeof(s), "inw 0xa20 <= 0xffff");
		add(1000 + i * 6 / 10, s);
	}
	report("storm (an unknown port and a bad selector by turns, 100,000 lines in 60 s)");
	return 0;
}
