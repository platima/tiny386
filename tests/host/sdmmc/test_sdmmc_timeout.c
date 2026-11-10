/*
 * v5.3: esp/main/sdmmc_timeout_wrap.c, a card command that timed out
 * reported as one, and a multi-block write that failed as failed.  The
 * wrap is built as the S31 boards build it, around a stand-in for
 * ESP-IDF's sdmmc_host_do_transaction() that takes as long as each case
 * says, on a pretend clock, and gives what the case says: the board's
 * ESP-IDF, which gives ESP_OK for a command whose event never came when
 * its buffer is in PSRAM; the fixed one, which gives ESP_ERR_TIMEOUT; data
 * errors, which the driver keeps in cmd->error; and good commands.
 *
 * 1. A lost event is the driver's wait of timeout_ms / portTICK_PERIOD_MS
 *    ticks run out: begun at any point in a tick, it lasts more than all
 *    those ticks but one.  Every such command with data in PSRAM must come
 *    back as ESP_ERR_TIMEOUT, whatever else it took; every good one, and
 *    every one with no data or with data in internal RAM, as the driver
 *    gave it, with cmd->error untouched; and the clock read only for data
 *    in PSRAM.
 * 2. sdmmc_write_sectors_dma() as the board's ESP-IDF has it, which returns
 *    the result of ACMD22 (the card's count of blocks written) for a failed
 *    multi-block write: through the wrap it must return the write's own
 *    error, for each task its own, and nothing else may change.
 *
 *   test_sdmmc_timeout
 *
 * Exit status 0 when every check passes.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "sd_protocol_defs.h"
#include "sd_protocol_types.h"

esp_err_t __wrap_sdmmc_host_do_transaction(int slot, sdmmc_command_t *cmdinfo);

int host_log_lines, host_crit;
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

static uint8_t s_psram[4096], s_internal[4096], s_count[4];
static int64_t s_now = 1000000;         /* microseconds */
static int s_clock_reads;
static int s_tasks[8];
static TaskHandle_t s_task = &s_tasks[0];

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

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
	return s_task;
}

/* what the driver does with each command: one step for the next command,
   or a script of them, one for each command in turn */
struct step {
	int64_t takes;
	esp_err_t gives, error;
};
static struct step s_step, s_script[8];
static int s_script_n, s_script_i;
static int s_calls, s_slot;
static uint32_t s_opcodes[16];

esp_err_t __real_sdmmc_host_do_transaction(int slot, sdmmc_command_t *cmdinfo)
{
	struct step st = s_script_i < s_script_n ? s_script[s_script_i++] : s_step;
	if (s_calls < 16)
		s_opcodes[s_calls] = cmdinfo->opcode;
	s_calls++;
	s_slot = slot;
	s_now += st.takes;
	cmdinfo->error = st.error;
	return st.gives;
}

static void script(int n, const struct step *steps)
{
	memcpy(s_script, steps, (size_t) n * sizeof(*steps));
	s_script_n = n;
	s_script_i = 0;
	s_calls = 0;
}

/* ==== 1. one command at a time ==== */

enum where { NONE, INTERNAL, PSRAM };
static const char *const wheres[] = { "no data", "internal RAM", "PSRAM" };

/* one command through the wrap: what it returned */
static esp_err_t run(enum where w, uint32_t timeout_ms, int64_t takes, esp_err_t gives,
		     esp_err_t error, int *clock_reads)
{
	sdmmc_command_t cmd;
	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = MMC_READ_BLOCK_MULTIPLE;
	cmd.data = w == PSRAM ? (void *) s_psram : w == INTERNAL ? (void *) s_internal : NULL;
	cmd.datalen = w == NONE ? 0 : 4096;
	cmd.timeout_ms = timeout_ms;
	s_step = (struct step) { takes, gives, error };
	s_script_n = s_script_i = 0;
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

static void part1(void)
{
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
}

/* ==== 2. a multi-block write, as the board's ESP-IDF sends it ==== */

/* sdmmc_send_cmd() as the board's ESP-IDF has it, cut to what this needs */
static esp_err_t send_cmd(sdmmc_command_t *cmd)
{
	if (cmd->timeout_ms == 0)
		cmd->timeout_ms = 1000;
	esp_err_t err = __wrap_sdmmc_host_do_transaction(1, cmd);
	return err != ESP_OK ? err : cmd->error;
}

static esp_err_t send(uint32_t opcode, void *data, size_t len, uint32_t timeout_ms)
{
	sdmmc_command_t cmd;
	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = opcode;
	cmd.data = data;
	cmd.datalen = len;
	cmd.timeout_ms = timeout_ms;
	return send_cmd(&cmd);
}

/* the card's count of blocks written: CMD55, then ACMD22 into a 4-byte
   buffer in internal RAM (sdmmc_send_cmd_num_of_written_blocks()) */
static esp_err_t num_written(void)
{
	esp_err_t err = send(MMC_APP_CMD, NULL, 0, 0);
	return err != ESP_OK ? err : send(SD_APP_SEND_NUM_WR_BLOCKS, s_count, 4, 0);
}

/* sdmmc_write_sectors_dma() as the board's ESP-IDF has it (sdmmc_cmd.c at
   188e3e55bb), cut to the commands it sends: CMD25 from buf, CMD13, and
   for a failed CMD25 the count, whose result it returns in place of the
   write's; its wait for the card after a good write is left out */
static esp_err_t idf_write_multiple(void *buf)
{
	esp_err_t err = send(MMC_WRITE_BLOCK_MULTIPLE, buf, 4096, 5000);
	send(MMC_SEND_STATUS, NULL, 0, 0);
	if (err != ESP_OK)
		err = num_written();
	return err;
}

static const struct step good = { 150, ESP_OK, ESP_OK };

static void part2(void)
{
	/* CMD25's step, then CMD13, CMD55 and ACMD22 good */
	struct step w[4] = { good, good, good, good };
	esp_err_t r;

	/* a good write: two commands, done */
	script(4, w);
	r = idf_write_multiple(s_psram);
	CHECK(r == ESP_OK && s_calls == 2, "a good write from PSRAM: %#x, %d commands", r, s_calls);
	script(4, w);
	r = idf_write_multiple(s_internal);
	CHECK(r == ESP_OK && s_calls == 2, "a good write from internal RAM: %#x", r);

	/* a CRC error the driver keeps, from either buffer: the board's
	   ESP-IDF alone would give ACMD22's ESP_OK */
	enum where bw[2] = { INTERNAL, PSRAM };
	for (int k = 0; k < 2; k++) {
		void *buf = bw[k] == PSRAM ? (void *) s_psram : (void *) s_internal;
		w[0] = (struct step) { 2000, ESP_OK, ESP_ERR_INVALID_CRC };
		script(4, w);
		int lines = host_log_lines;
		r = idf_write_multiple(buf);
		CHECK(r == ESP_ERR_INVALID_CRC && s_calls == 4 && s_opcodes[3] == SD_APP_SEND_NUM_WR_BLOCKS,
		      "a write from %s with a CRC error: %#x after %d commands", wheres[bw[k]], r,
		      s_calls);
		CHECK(host_log_lines == lines + 1, "one log line for it");
		/* the same task's next count, with no failed write before it:
		   the card's answer, as it is */
		script(2, w + 2);
		r = num_written();
		CHECK(r == ESP_OK, "a count with no failed write before it: %#x", r);
		/* the fixed driver's timeout, from internal RAM as v5.2 sent
		   every write */
		w[0] = (struct step) { 5000000, ESP_ERR_TIMEOUT, ESP_OK };
		script(4, w);
		r = idf_write_multiple(buf);
		CHECK(r == ESP_ERR_TIMEOUT, "a write from %s the driver times out: %#x",
		      wheres[bw[k]], r);
	}
	/* the board's ESP-IDF with a lost event on a write from PSRAM:
	   ESP_OK from the driver, ESP_ERR_TIMEOUT from the wrap, and the
	   write's timeout from the count */
	w[0] = (struct step) { 5000000, ESP_OK, ESP_OK };
	script(4, w);
	r = idf_write_multiple(s_psram);
	CHECK(r == ESP_ERR_TIMEOUT && s_calls == 4, "a write from PSRAM whose event never "
	      "came: %#x after %d commands", r, s_calls);
	/* a count that fails itself: its own error */
	w[0] = (struct step) { 2000, ESP_OK, ESP_ERR_INVALID_CRC };
	w[3] = (struct step) { 1000000, ESP_ERR_TIMEOUT, ESP_OK };
	script(4, w);
	r = idf_write_multiple(s_internal);
	CHECK(r == ESP_ERR_TIMEOUT, "a failed write whose count times out: %#x", r);
	w[3] = good;
	script(2, w + 2);
	r = num_written();
	CHECK(r == ESP_OK, "and after it, a count with no failed write before it: %#x", r);
	/* CMD55 refused: no count sent, the write's error from CMD55 */
	w[0] = (struct step) { 2000, ESP_OK, ESP_ERR_INVALID_CRC };
	w[2] = (struct step) { 100, ESP_ERR_TIMEOUT, ESP_OK };
	script(4, w);
	r = idf_write_multiple(s_internal);
	CHECK(r == ESP_ERR_TIMEOUT && s_calls == 3, "a failed write whose CMD55 fails: %#x", r);
	w[2] = good;

	/* tasks: each count gives its own task's failed write.  Two tasks,
	   their commands one after the other as the driver's mutex lets
	   them through */
	TaskHandle_t a = &s_tasks[1], b = &s_tasks[2];
	s_step = good;
	s_script_n = 0;
	s_task = a;
	s_step.error = ESP_ERR_INVALID_CRC;
	r = send(MMC_WRITE_BLOCK_MULTIPLE, s_internal, 4096, 5000);
	s_step = good;
	CHECK(r == ESP_ERR_INVALID_CRC, "task a's write fails");
	s_task = b;
	s_step = (struct step) { 5000000, ESP_OK, ESP_OK };
	r = send(MMC_WRITE_BLOCK_MULTIPLE, s_psram, 4096, 5000);
	s_step = good;
	CHECK(r == ESP_ERR_TIMEOUT, "task b's write's event never comes");
	s_task = &s_tasks[3];
	r = num_written();
	CHECK(r == ESP_OK, "a third task's count, with no failed write: %#x", r);
	s_task = b;
	r = num_written();
	CHECK(r == ESP_ERR_TIMEOUT, "task b's count: b's timeout, %#x", r);
	s_task = a;
	r = num_written();
	CHECK(r == ESP_ERR_INVALID_CRC, "task a's count: a's CRC error, %#x", r);
	/* five tasks with a failed write: room for four, the oldest goes */
	for (int t = 0; t < 5; t++) {
		s_task = &s_tasks[3 + t];
		s_step = (struct step) { 100, ESP_OK, 0x200 + t };
		send(MMC_WRITE_BLOCK_MULTIPLE, s_internal, 4096, 5000);
	}
	s_step = good;
	unsigned kept = 0;
	for (int t = 0; t < 5; t++) {
		s_task = &s_tasks[3 + t];
		r = num_written();
		if (r == 0x200 + t)
			kept++;
		else
			CHECK(t == 0 && r == ESP_OK, "five tasks: task %d's count gave %#x", t, r);
	}
	CHECK(kept == 4, "five tasks with a failed write: the last four kept (%u)", kept);
	s_task = &s_tasks[0];
	CHECK(host_crit == 0, "every critical section left (%d)", host_crit);
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	printf("tick %u ms (%u Hz)\n", (unsigned) portTICK_PERIOD_MS, (unsigned) configTICK_RATE_HZ);
	printf("== 1. one command at a time\n");
	part1();
	printf("== 2. a multi-block write, as the board's ESP-IDF sends it\n");
	part2();
	printf("test_sdmmc_timeout: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
