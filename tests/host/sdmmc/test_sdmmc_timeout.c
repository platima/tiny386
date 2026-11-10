/*
 * v5.3: esp/main/sdmmc_timeout_wrap.c, a card command that timed out
 * reported as one.  The wrap is built as the S31 boards build it, around a
 * stand-in for ESP-IDF's sdmmc_host_do_transaction() that takes as long as
 * each case says, on a pretend clock, and gives what the case says: the
 * board's ESP-IDF, which gives ESP_OK for a command whose event never came
 * when its buffer is in PSRAM; the fixed one, which gives ESP_ERR_TIMEOUT;
 * data errors, which the driver keeps in cmd->error; and good commands.
 *
 * A lost event is the driver's wait of timeout_ms / portTICK_PERIOD_MS
 * ticks run out: begun at any point in a tick, it lasts more than all those
 * ticks but one.  Every such command with data in PSRAM must come back as
 * ESP_ERR_TIMEOUT, whatever else it took; every good one, and every one with
 * no data or with data in internal RAM, as the driver gave it, with
 * cmd->error untouched; and the clock read only for data in PSRAM.
 *
 *   test_sdmmc_timeout
 *
 * Exit status 0 when every check passes.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#include "sd_protocol_types.h"

esp_err_t __wrap_sdmmc_host_do_transaction(int slot, sdmmc_command_t *cmdinfo);

int host_log_lines;
static int checks, fails;
#define CHECK(c, ...) do { \
	checks++; \
	if (!(c)) { \
		fails++; \
		printf("  FAIL line %d: ", __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} \
} while (0)

/* ==== the stand-ins ==== */

static uint8_t s_psram[4096], s_internal[4096];
static int64_t s_now = 1000000;         /* microseconds */
static int s_clock_reads;

int64_t esp_timer_get_time(void)
{
	s_clock_reads++;
	return s_now;
}

size_t esp_cache_get_line_size_by_addr(const void *addr)
{
	const uint8_t *p = addr;
	return p >= s_psram && p < s_psram + sizeof(s_psram) ? 64 : 0;
}

static int64_t s_takes;                 /* how long the next command takes */
static esp_err_t s_gives, s_error;      /* what it returns, and leaves in cmd->error */
static int s_calls, s_slot;

esp_err_t __real_sdmmc_host_do_transaction(int slot, sdmmc_command_t *cmdinfo)
{
	s_calls++;
	s_slot = slot;
	s_now += s_takes;
	cmdinfo->error = s_error;
	return s_gives;
}

/* ==== the cases ==== */

enum where { NONE, INTERNAL, PSRAM };
static const char *const wheres[] = { "no data", "internal RAM", "PSRAM" };

/* one command through the wrap: what it returned */
static esp_err_t run(enum where w, uint32_t timeout_ms, int64_t takes, esp_err_t gives,
		     esp_err_t error, int *clock_reads)
{
	sdmmc_command_t cmd;
	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = 18;
	cmd.data = w == PSRAM ? (void *) s_psram : w == INTERNAL ? (void *) s_internal : NULL;
	cmd.datalen = w == NONE ? 0 : 4096;
	cmd.timeout_ms = timeout_ms;
	s_takes = takes;
	s_gives = gives;
	s_error = error;
	int calls = s_calls, reads = s_clock_reads;
	esp_err_t r = __wrap_sdmmc_host_do_transaction(1, &cmd);
	CHECK(s_calls == calls + 1 && s_slot == 1, "the driver called once, for slot 1");
	CHECK(cmd.error == error, "cmd->error left as the driver left it");
	*clock_reads = s_clock_reads - reads;
	return r;
}

/* the least and the most a wait that saw no event can last, begun at the
   start of a tick or just before its end */
static int64_t lost_least(uint32_t timeout_ms)
{
	uint32_t ticks = timeout_ms / portTICK_PERIOD_MS;
	return (int64_t) (ticks - 1) * portTICK_PERIOD_MS * 1000 + 1;
}

static int64_t lost_most(uint32_t timeout_ms)
{
	return (int64_t) (timeout_ms / portTICK_PERIOD_MS) * portTICK_PERIOD_MS * 1000;
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	printf("tick %u ms (%u Hz)\n", (unsigned) portTICK_PERIOD_MS, (unsigned) configTICK_RATE_HZ);
	static const uint32_t timeouts[] = { 1000, 5000 };     /* reads and writes, sdmmc_common.h */
	int reads;
	unsigned caught = 0, passed = 0;

	for (unsigned t = 0; t < 2; t++) {
		uint32_t to = timeouts[t];
		int64_t least = lost_least(to), most = lost_most(to);
		/* a lost event: the board's ESP-IDF gives ESP_OK, the fixed one
		   ESP_ERR_TIMEOUT; both must come back as ESP_ERR_TIMEOUT, and
		   so must the same with time before the wait (the driver's
		   mutex, the clock, the card's other events) */
		static const int64_t before[] = { 0, 3, 1500, 250000 };
		for (unsigned b = 0; b < 4; b++)
			for (int64_t w = least; w <= most; w += (most - least) / 7 + 1) {
				esp_err_t r = run(PSRAM, to, before[b] + w, ESP_OK, ESP_OK, &reads);
				CHECK(r == ESP_ERR_TIMEOUT, "%u ms timeout, a lost event (%lld us, "
				      "%lld before): %#x", (unsigned) to, (long long) w,
				      (long long) before[b], r);
				r = run(PSRAM, to, before[b] + w, ESP_ERR_TIMEOUT, ESP_OK, &reads);
				CHECK(r == ESP_ERR_TIMEOUT, "%u ms timeout, a lost event the driver "
				      "reports: %#x", (unsigned) to, r);
				caught++;
			}
		/* good commands: a few milliseconds, and as long as can be */
		static const int64_t good[] = { 0, 180, 3000, 25000, 500000 };
		for (unsigned g = 0; g < 5; g++) {
			esp_err_t r = run(PSRAM, to, good[g], ESP_OK, ESP_OK, &reads);
			CHECK(r == ESP_OK && reads == 2, "%u ms timeout, a good command in %lld us: "
			      "%#x, %d clock reads", (unsigned) to, (long long) good[g], r, reads);
			passed++;
		}
		esp_err_t r = run(PSRAM, to, least - 2, ESP_OK, ESP_OK, &reads);
		CHECK(r == ESP_OK, "%u ms timeout, a good command 1 us short of the least a lost "
		      "event takes: %#x", (unsigned) to, r);
		passed++;
		/* data errors: the driver's own, in cmd->error, given back as
		   they were */
		r = run(PSRAM, to, most, ESP_OK, ESP_ERR_INVALID_CRC, &reads);
		CHECK(r == ESP_OK, "%u ms timeout, a CRC error in cmd->error: returned %#x, as "
		      "the driver gave it", (unsigned) to, r);
		r = run(PSRAM, to, 2000, ESP_FAIL, ESP_OK, &reads);
		CHECK(r == ESP_FAIL, "%u ms timeout, ESP_FAIL: %#x", (unsigned) to, r);
		/* no data, or data in internal RAM: through untimed */
		for (enum where w = NONE; w <= INTERNAL; w++) {
			r = run(w, to, most, ESP_OK, ESP_OK, &reads);
			CHECK(r == ESP_OK && reads == 0, "%u ms timeout, %s, %lld us: %#x, %d clock "
			      "reads", (unsigned) to, wheres[w], (long long) most, r, reads);
			r = run(w, to, most, ESP_ERR_TIMEOUT, ESP_OK, &reads);
			CHECK(r == ESP_ERR_TIMEOUT && reads == 0, "%u ms timeout, %s, the driver's "
			      "timeout: %#x", (unsigned) to, wheres[w], r);
		}
	}
	/* timeouts of under two ticks cannot be told from the time: given
	   back as they came */
	int lines = host_log_lines;
	esp_err_t r = run(PSRAM, portTICK_PERIOD_MS, 2000000, ESP_OK, ESP_OK, &reads);
	CHECK(r == ESP_OK && host_log_lines == lines, "a timeout of one tick: %#x", r);
	r = run(PSRAM, 0, 2000000, ESP_OK, ESP_OK, &reads);
	CHECK(r == ESP_OK, "a timeout of 0: %#x", r);
	r = run(PSRAM, 2 * portTICK_PERIOD_MS, portTICK_PERIOD_MS * 1000, ESP_OK, ESP_OK, &reads);
	CHECK(r == ESP_ERR_TIMEOUT, "a timeout of two ticks, one tick taken: %#x", r);
	r = run(PSRAM, 2 * portTICK_PERIOD_MS, portTICK_PERIOD_MS * 1000 - 1, ESP_OK, ESP_OK,
		&reads);
	CHECK(r == ESP_OK, "a timeout of two ticks, 1 us under one tick taken: %#x", r);
	/* the board's log says each one it reports */
	lines = host_log_lines;
	r = run(PSRAM, 1000, 1000000, ESP_OK, ESP_OK, &reads);
	CHECK(r == ESP_ERR_TIMEOUT && host_log_lines == lines + 1, "one log line for each");

	printf("%u lost events caught, %u good commands passed\n", caught, passed);
	printf("test_sdmmc_timeout: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
