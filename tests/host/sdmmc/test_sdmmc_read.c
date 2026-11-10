/*
 * v5.3: esp/main/sdmmc_read_wrap.c, no status command after a card read.
 * ESP-IDF's own sdmmc_cmd.c and sdmmc_common.c (188e3e55bb) built with
 * their real headers (idf_stubs/README) and linked with
 * -Wl,--wrap=sdmmc_read_sectors and --wrap=sdmmc_wait_for_idle as the S31
 * boards link them, around idf_card.c's pretend card and host.  Every read
 * is made twice, through ESP-IDF's own sdmmc_read_sectors() and through the
 * wrap, from a buffer in the pretend PSRAM on a cache line (the host takes
 * it as it is), 4 bytes off one (ESP-IDF's bounce buffer), and in internal
 * RAM; 0 to 130 sectors, at the card's start, its end and past it; and
 * reads the card fails with a CRC error.  For each:
 *
 * - the same result and the same bytes in the buffer, the card's own;
 * - through the wrap, for a buffer the host takes, one read command and no
 *   CMD13 when it works (ESP-IDF: the read and a CMD13), and a CMD13 after
 *   one that failed, with the same one error line as ESP-IDF's;
 * - the card in tran after it, so that the next command works;
 * - with sd_wait_fix = 0, ESP-IDF's own, command for command.
 * Then reads and writes mixed, as FatFs and sd_part.c make them, with the
 * wait after writes (sdmmc_wait_wrap.c) linked too: nothing sent to a card
 * still programming, and every read gives what was last written.
 *
 *   test_sdmmc_read
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

esp_err_t __real_sdmmc_read_sectors(sdmmc_card_t *card, void *dst, size_t start_block,
				    size_t block_count);

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

#define SECTORS 4096
#define MAXN 130

static sdmmc_card_t s_card;
static uint8_t *s_psram;                /* the pretend PSRAM: MAXN sectors and a line more */
static uint8_t *s_internal;

static uint64_t s_rng = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void)
{
	s_rng ^= s_rng << 13;
	s_rng ^= s_rng >> 7;
	s_rng ^= s_rng << 17;
	return (uint32_t) (s_rng >> 16);
}

static void card_fill(void)
{
	for (unsigned i = 0; i < SECTORS * 512; i++)
		idf_card.mem[i] = (uint8_t) (i * 131 + (i >> 9) * 7 + 3);
}

struct res {
	esp_err_t err;
	unsigned reads, cmd13, log;
	bool data_ok, tran;
};

/* one read of n at start into buf, through ESP-IDF's own (wrap 0) or the
   wrap (1) */
static struct res one_read(int wrap, uint8_t *buf, size_t start, size_t n, unsigned fail)
{
	struct res r;
	unsigned rd0 = idf_card.cmds[MMC_READ_BLOCK_SINGLE] + idf_card.cmds[MMC_READ_BLOCK_MULTIPLE];
	unsigned s0 = idf_card.cmds[MMC_SEND_STATUS];
	int l0 = idf_log_errors;
	memset(buf, 0xC3, MAXN * 512);
	idf_card.read_fail_at = fail ? rd0 + 1 : 0;
	r.err = wrap ? sdmmc_read_sectors(&s_card, buf, start, n) :
		__real_sdmmc_read_sectors(&s_card, buf, start, n);
	idf_card.read_fail_at = 0;
	r.reads = idf_card.cmds[MMC_READ_BLOCK_SINGLE] + idf_card.cmds[MMC_READ_BLOCK_MULTIPLE] - rd0;
	r.cmd13 = idf_card.cmds[MMC_SEND_STATUS] - s0;
	r.log = (unsigned) (idf_log_errors - l0);
	r.data_ok = r.err != ESP_OK || !memcmp(buf, idf_card.mem + start * 512, n * 512);
	r.tran = !idf_card_busy();
	return r;
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	s_psram = aligned_alloc(64, (MAXN + 1) * 512);
	s_internal = aligned_alloc(64, MAXN * 512 + 64);
	idf_card.psram_lo = s_psram;
	idf_card.psram_hi = s_psram + (MAXN + 1) * 512;
	idf_card_start(&s_card, SECTORS);
	card_fill();
	sd_wait_fix = 1;

	static const unsigned counts[] = { 0, 1, 2, 3, 8, 16, 63, 64, 65, 127, 128, 129, 130 };
	const unsigned ncounts = sizeof(counts) / sizeof(counts[0]);
	struct { uint8_t *buf; const char *what; bool taken; } bufs[] = {
		{ NULL, "PSRAM, on a cache line", true },
		{ NULL, "PSRAM, 4 bytes on", false },
		{ NULL, "internal RAM, 4 bytes on", true },
	};
	bufs[0].buf = s_psram;
	bufs[1].buf = s_psram + 4;
	bufs[2].buf = s_internal + 4;
	unsigned cases = 0, saved = 0, idf_cmds = 0, wrap_cmds = 0;

	printf("== every read twice, ESP-IDF's own and the wrap\n");
	for (unsigned b = 0; b < 3; b++)
		for (unsigned c = 0; c < ncounts; c++)
			for (unsigned w = 0; w < 6; w++) {
				size_t n = counts[c], start;
				switch (w) {
				case 0: start = 0; break;
				case 1: start = 1 + rnd() % 100; break;
				case 2: start = SECTORS - n; break;
				case 3: start = SECTORS - n + 1; break;        /* one past the end */
				case 4: start = SECTORS + 5; break;
				default: start = 200 + rnd() % 3000; break;
				}
				for (unsigned fail = 0; fail <= 1; fail++) {
					struct res a = one_read(0, bufs[b].buf, start, n, fail);
					struct res r = one_read(1, bufs[b].buf, start, n, fail);
					cases++;
					idf_cmds += a.reads + a.cmd13;
					wrap_cmds += r.reads + r.cmd13;
					const char *what = bufs[b].what;
					CHECK(a.err == r.err && a.data_ok && r.data_ok, "%s, %zu at %zu%s: ESP-IDF "
					      "%#x, the wrap %#x, data %s", what, n, start, fail ? ", failing" :
					      "", a.err, r.err, r.data_ok ? "right" : "WRONG");
					CHECK(a.reads == r.reads && r.tran, "%s, %zu at %zu: %u read commands "
					      "(ESP-IDF %u), the card %s after", what, n, start, r.reads, a.reads,
					      r.tran ? "in tran" : "busy");
					if (bufs[b].taken && n && a.reads) {
						CHECK(r.cmd13 == (r.err != ESP_OK) && a.cmd13 >= 1, "%s, %zu at %zu%s: "
						      "%u CMD13s after the read (ESP-IDF %u)", what, n, start,
						      fail ? ", failing" : "", r.cmd13, a.cmd13);
						CHECK(r.log == a.log, "%s, %zu at %zu: %u error lines, ESP-IDF %u", what,
						      n, start, r.log, a.log);
						if (r.err == ESP_OK)
							saved += a.cmd13 - r.cmd13;
					} else {
						CHECK(r.cmd13 == a.cmd13 && r.log == a.log, "%s, %zu at %zu: as "
						      "ESP-IDF's own (%u CMD13s, ESP-IDF %u)", what, n, start,
						      r.cmd13, a.cmd13);
					}
				}
			}
	printf("%u reads made twice: ESP-IDF sent %u card commands, the wrap %u (%u CMD13s fewer)\n",
	       cases, idf_cmds, wrap_cmds, saved);

	printf("== sd_wait_fix = 0\n");
	sd_wait_fix = 0;
	for (unsigned c = 1; c < ncounts; c++) {
		struct res a = one_read(0, s_psram, 300, counts[c], 0);
		struct res r = one_read(1, s_psram, 300, counts[c], 0);
		CHECK(r.err == a.err && r.data_ok && r.reads == a.reads && r.cmd13 == a.cmd13,
		      "%u sectors: as ESP-IDF's own (%u CMD13s, ESP-IDF %u)", counts[c], r.cmd13,
		      a.cmd13);
	}
	sd_wait_fix = 1;

	printf("== reads and writes mixed, the wait after writes linked too\n");
	idf_card.reads_while_busy = 0;
	unsigned ops = 0;
	for (int i = 0; i < 3000; i++) {
		size_t n = 1 + rnd() % MAXN, start = rnd() % (SECTORS - MAXN);
		uint8_t *buf = rnd() % 4 ? s_psram : s_internal + 4;
		idf_card.prog_us = rnd() % 5 == 0 ? rnd() % 20000 : rnd() % 2000;
		if (rnd() % 100 < 45) {
			for (size_t k = 0; k < n * 512; k++)
				buf[k] = (uint8_t) (k * 29 + i);
			esp_err_t e = sdmmc_write_sectors(&s_card, buf, start, n);
			CHECK(e == ESP_OK && !memcmp(idf_card.mem + start * 512, buf, n * 512) &&
			      !idf_card_busy(), "op %d: write %zu at %zu: %#x", i, n, start, e);
		} else {
			esp_err_t e = sdmmc_read_sectors(&s_card, buf, start, n);
			CHECK(e == ESP_OK && !memcmp(idf_card.mem + start * 512, buf, n * 512), "op %d: "
			      "read %zu at %zu: %#x", i, n, start, e);
		}
		ops++;
	}
	CHECK(idf_card.reads_while_busy == 0, "no command sent to a card still programming (%u)",
	      idf_card.reads_while_busy);
	printf("%u reads and writes, %u read commands sent with no CMD13 after them\n", ops,
	       (unsigned) sd_read_direct);

	printf("test_sdmmc_read: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
