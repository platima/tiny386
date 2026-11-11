/*
 * v5.3: esp/main/sdmmc_wait_wrap.c, the card asked again after a write as
 * soon as it lets go of D0.  ESP-IDF's own sdmmc_cmd.c and sdmmc_common.c
 * (188e3e55bb) are built here with their real headers (idf_stubs/README),
 * linked with -Wl,--wrap=sdmmc_wait_for_idle as the S31 boards link them,
 * around idf_card.c's pretend card and clock.  Each case is one
 * sdmmc_write_sectors() of 1 or 64 sectors with the card programming for a
 * given time afterwards, run twice: sd_wait_fix = 0 (ESP-IDF's own wait)
 * and 1 (the wrap).  With the wrap, for every case:
 *
 * - the same result (done, timed out, the CMD13 error), the data on the
 *   card, and the card no longer programming when a write comes back done;
 * - never later than ESP-IDF's own (by more than the pretend clock's own
 *   overheads, or with a D0 stuck busy, a tick for the emulator's task);
 * - at most one CMD13 more than ESP-IDF's, and none closer together than
 *   ESP-IDF's own closest ones unless D0 has just gone free;
 * - with D0 working, done within 60 us of the card's end of programming,
 *   for the emulator's task at any length and for any other task up to
 *   ESP-IDF's busy-wait (12.7 ms), past which they sleep a tick as before;
 * - with D0 stuck busy or stuck free, ESP-IDF's schedule (plus one CMD13);
 * - the counts that profile.c gives, which must tell the three apart: with
 *   D0 stuck free, no CMD13 counted as sent as D0 went from busy to free
 *   and one at once in every wait; stuck busy, neither; working, the end of
 *   every wait that watched D0 to the end counted as one of the two.
 *
 *   test_sdmmc_wait
 *
 * Exit status 0 when every check passes.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "sdmmc_cmd.h"
#include "sd_protocol_defs.h"
#include "idf_card.h"
#include "sdmmc_wait_wrap.h"

static int checks, fails;
#define CHECK(c, ...) do { \
	checks++; \
	if (!(c)) { \
		if (fails++ < 40) { \
			printf("  FAIL line %d: ", __LINE__); \
			printf(__VA_ARGS__); \
			printf("\n"); \
		} \
	} \
} while (0)

static sdmmc_card_t s_card;
static int s_emu;                       /* the emulator's task, and another */
static int s_other;

struct res {
	esp_err_t err;
	int64_t end;                    /* ns from the write's start */
	int64_t late;                   /* ns past the card's end of programming */
	unsigned cmd13;
	int64_t gap;                    /* the least ns between two CMD13s */
	bool busy_at_end;
	bool data_ok;
};

static struct res one_write(int fix, uint32_t prog_us, int d0, bool spin, unsigned n,
			    unsigned fail13)
{
	static uint8_t *buf;
	if (!buf)
		buf = heap_caps_malloc(64 * 512, 0);
	idf_card_start(&s_card, 4096);
	idf_card.prog_us = prog_us;
	idf_card.d0 = d0;
	idf_card.cmd13_fail_at = fail13;
	sd_wait_fix = fix;
	idf_task = spin ? (void *) &s_emu : (void *) &s_other;
	sd_wait_spin_task = &s_emu;
	for (unsigned i = 0; i < n * 512; i++)
		buf[i] = (uint8_t) (i * 7 + prog_us + n);
	struct res r;
	/* both runs of a case from the same point in a tick, 3.3 ms in, so
	   that their sleeps to the next tick are alike */
	int64_t tick = (int64_t) IDF_TICK_US * 1000;
	idf_now = (idf_now / tick + 1) * tick + 3300000;
	int64_t t0 = idf_now;
	r.err = sdmmc_write_sectors(&s_card, buf, 100, n);
	r.end = idf_now - t0;
	r.late = idf_now - idf_card.busy_until;
	r.cmd13 = idf_card.cmds[MMC_SEND_STATUS];
	r.gap = idf_card.least13_gap;
	r.busy_at_end = idf_card_busy();
	r.data_ok = !memcmp(idf_card.mem + 100 * 512, buf, n * 512);
	return r;
}

static const uint32_t progs[] = {
	0, 3, 20, 60, 99, 100, 101, 150, 299, 300, 450, 700, 1000, 1300, 1499, 1500, 1600, 1900,
	2500, 3100, 4000, 6300, 6400, 9000, 12000, 12700, 13000, 15000, 25000, 40000, 100000,
	1000000, 4990000, 6000000,
};
static const char *const d0s[] = { "D0 works", "D0 stuck busy", "D0 stuck free" };

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	unsigned nprogs = sizeof(progs) / sizeof(progs[0]);
	unsigned cases = 0, sooner = 0;
	int64_t late_a[2] = { 0 }, late_b[2] = { 0 };
	/* the wrap's counts over all the cases, for each kind of D0 */
	uint32_t c_waits[3] = { 0 }, c_polls[3] = { 0 }, c_early[3] = { 0 }, c_free[3] = { 0 };

	printf("== writes of 1 and 64 sectors, the card programming for a while after\n");
	printf("  programming us | ESP-IDF's wait ends, us late, CMD13s | the wrap's (emulator's task)\n");
	for (int d0 = D0_WORKS; d0 <= D0_STUCK_FREE; d0++)
		for (int spin = 0; spin <= 1; spin++)
			for (unsigned n = 1; n <= 64; n += 63)
				for (unsigned p = 0; p < nprogs; p++) {
					uint32_t pu = progs[p];
					struct res a = one_write(0, pu, d0, spin, n, 0);
					uint32_t w0 = sd_wait_waits, p0 = sd_wait_polls;
					uint32_t e0 = sd_wait_early, f0 = sd_wait_free;
					struct res b = one_write(1, pu, d0, spin, n, 0);
					uint32_t dw = sd_wait_waits - w0, de = sd_wait_early - e0;
					uint32_t df = sd_wait_free - f0;
					c_waits[d0] += dw;
					c_polls[d0] += sd_wait_polls - p0;
					c_early[d0] += de;
					c_free[d0] += df;
					cases++;
					const char *w = d0s[d0];
					/* what the board's profile line can tell */
					if (d0 == D0_STUCK_FREE)
						CHECK(de == 0 && df == dw, "%s, %u us: waits %u, sent as D0 "
						      "went free %u, at once %u", w, pu, dw, de, df);
					else if (d0 == D0_STUCK_BUSY)
						CHECK(de == 0 && df == 0, "%s, %u us: sent as D0 went free "
						      "%u, at once %u", w, pu, de, df);
					else if (b.err == ESP_OK && (spin || pu < 12000))
						CHECK(de + df == dw, "%s, %s task, %u sectors, %u us: waits %u, "
						      "sent as D0 went free %u, at once %u", w, spin ?
						      "emulator's" : "another", n, pu, dw, de, df);
					CHECK(a.err == b.err, "%s, %s task, %u sectors, %u us: ESP-IDF %#x, "
					      "the wrap %#x", w, spin ? "emulator's" : "another", n, pu,
					      a.err, b.err);
					CHECK(b.data_ok && (b.err != ESP_OK || !b.busy_at_end), "%s, %u us: "
					      "the data on the card, and done only once programmed", w, pu);
					/* ns: the pretend clock's own costs; and a tick
					   where both sleep to the next one, which a few
					   us either side of it can move */
					int64_t slack = 5000;
					if (!spin && pu >= 12000)
						slack += (int64_t) IDF_TICK_US * 1000;
					CHECK(b.end <= a.end + slack, "%s, %s task, %u sectors, %u us: the "
					      "wrap done %lld ns after ESP-IDF", w, spin ? "emulator's" :
					      "another", n, pu, (long long) (b.end - a.end));
					CHECK(b.cmd13 <= a.cmd13 + 1, "%s, %u us: %u CMD13s, ESP-IDF %u", w,
					      pu, b.cmd13, a.cmd13);
					if (d0 == D0_WORKS && b.err == ESP_OK && (spin || pu < 12000))
						CHECK(b.late <= 60000, "%s task, %u sectors, %u us: done %lld ns "
						      "after the card", spin ? "emulator's" : "another", n, pu,
						      (long long) b.late);
					if (d0 == D0_STUCK_BUSY && b.cmd13 > 2)
						CHECK(b.gap >= a.gap - 5000, "%s, %u us: CMD13s %lld ns apart, "
						      "ESP-IDF %lld", w, pu, (long long) b.gap, (long long) a.gap);
					if (b.end + 1000 < a.end)
						sooner++;
					if (d0 == D0_WORKS && spin && n == 64 && a.err == ESP_OK) {
						printf("  %9u | %9lld %9lld %3u | %9lld %6lld %3u\n", pu,
						       (long long) a.end / 1000, (long long) a.late / 1000,
						       a.cmd13, (long long) b.end / 1000,
						       (long long) b.late / 1000, b.cmd13);
						if (pu >= 100 && pu <= 40000) {
							int k = pu > 12700;
							late_a[k] += a.late;
							late_b[k] += b.late;
						}
					}
				}
	printf("%u cases, %u done sooner with the wrap.  Past the card's end of programming, in "
	       "the cases above: ESP-IDF %lld us and the wrap %lld us for 0.1 to 12.7 ms, ESP-IDF "
	       "%lld us and the wrap %lld us for 13 to 40 ms\n", cases, sooner,
	       (long long) late_a[0] / 1000, (long long) late_b[0] / 1000,
	       (long long) late_a[1] / 1000, (long long) late_b[1] / 1000);
	for (int d0 = D0_WORKS; d0 <= D0_STUCK_FREE; d0++)
		printf("%s, the profile line's figures over these cases: waits %u, with %u status "
		       "commands: %u sent as D0 went from busy to free, %u at once\n", d0s[d0],
		       (unsigned) c_waits[d0], (unsigned) c_polls[d0], (unsigned) c_early[d0],
		       (unsigned) c_free[d0]);

	printf("== a CMD13 that fails in the wait\n");
	for (unsigned k = 2; k <= 5; k++) {
		struct res a = one_write(0, 3000, D0_WORKS, true, 8, k);
		struct res b = one_write(1, 3000, D0_WORKS, true, 8, k);
		/* the wrap may be done before the k-th CMD13 is sent */
		CHECK(a.err == ESP_ERR_TIMEOUT && (b.err == ESP_ERR_TIMEOUT || (b.err == ESP_OK &&
		      b.cmd13 < k)), "CMD13 number %u fails: ESP-IDF %#x, the wrap %#x after %u", k,
		      a.err, b.err, b.cmd13);
	}
	struct res a = one_write(0, 3000, D0_WORKS, true, 8, 2);
	struct res b = one_write(1, 3000, D0_STUCK_BUSY, true, 8, 2);
	CHECK(a.err == ESP_ERR_TIMEOUT && b.err == ESP_ERR_TIMEOUT, "the second CMD13 failing, D0 "
	      "stuck busy: both give its error (%#x, %#x)", a.err, b.err);

	printf("== the counts\n");
	/* one wait of 1.6 ms with each kind of D0: its CMD13s, the last sent
	   as D0 went free; the first sent at once; neither */
	static const unsigned want_early[3] = { 1, 0, 0 }, want_free[3] = { 0, 0, 1 };
	uint32_t w0;
	for (int d0 = D0_WORKS; d0 <= D0_STUCK_FREE; d0++) {
		w0 = sd_wait_waits;
		uint32_t e0 = sd_wait_early, f0 = sd_wait_free, p0 = sd_wait_polls;
		b = one_write(1, 1600, d0, true, 64, 0);
		CHECK(sd_wait_waits == w0 + 1 && sd_wait_polls - p0 == b.cmd13 - 1 &&
		      sd_wait_early - e0 == want_early[d0] && sd_wait_free - f0 == want_free[d0],
		      "%s: one wait (%u), its CMD13s counted (%u of %u), sent as D0 went free %u, at "
		      "once %u", d0s[d0], (unsigned) (sd_wait_waits - w0), (unsigned) (sd_wait_polls - p0),
		      b.cmd13, (unsigned) (sd_wait_early - e0), (unsigned) (sd_wait_free - f0));
	}
	w0 = sd_wait_waits;
	b = one_write(1, 0, D0_WORKS, true, 1, 0);
	CHECK(sd_wait_waits == w0, "a card done before its first CMD13: no wait");
	b = one_write(0, 1600, D0_WORKS, true, 64, 0);
	CHECK(sd_wait_waits == w0, "sd_wait_fix = 0: ESP-IDF's own wait, nothing counted");

	printf("test_sdmmc_wait: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
