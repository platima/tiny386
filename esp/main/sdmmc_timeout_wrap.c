/*
 * sdmmc_timeout_wrap.c: v5.3, a card command that timed out is reported
 * as one, and a multi-block write that failed as failed (the S31 boards).
 *
 * In this tree's ESP-IDF (188e3e55bb), sd_host_slot_sdmmc_do_transaction()
 * syncs the cache after each transfer to or from a buffer behind the cache
 * (on the S31, PSRAM only), and the sync's result takes the place of the
 * transfer's.  So when no event comes from the controller within the
 * command's timeout_ms (handle_event()'s ESP_ERR_TIMEOUT, which leaves
 * cmd->error at ESP_OK), the command comes back as done, and
 * sdmmc_read_sectors() or sdmmc_write_sectors() reports a read or a write
 * that did not happen as a success.  ESP-IDF has since fixed it upstream
 * (e00861c3f4, "do not let cache sync mask a failed transaction").  Since
 * v5.3 the guest's disk runs for hda = sd:N go to the card straight from
 * ide.c's buffer in PSRAM (sd_part.c), so guest data takes this path; so do
 * FatFs's own sector buffers, the profile files and the ini.
 *
 * The result cannot tell such a command from a good one, but the time can.
 * For each event the driver waits timeout_ms / portTICK_PERIOD_MS ticks,
 * and a wait that ends with no event has lasted longer than all of those
 * ticks but one.  A command with data behind the cache that comes back as
 * done after at least that long is reported here as ESP_ERR_TIMEOUT, as
 * the fixed driver reports it.  The commands that move sectors wait 1 s
 * for a read and 5 s for a write (sdmmc_common.h) and take a few
 * milliseconds when they work, so a good one is taken for a lost one only
 * if the card has stalled for about as long as the driver waits.  With a
 * fixed ESP-IDF such a command fails before this test, which then does
 * nothing.  Commands without data, or with it in internal RAM, are not
 * timed: the driver keeps their errors.
 *
 * And when a multi-block write (CMD25) fails, this ESP-IDF's
 * sdmmc_write_sectors_dma() asks the card how many blocks it wrote
 * (ACMD22) and returns that command's result in place of the write's, so a
 * write that failed comes back as done once the card answers the count
 * (upstream keeps the write's error).  That has been so for every failed
 * CMD25 since sd:N came in (v4.4), through v5.2's internal buffer as well,
 * and it would undo the timeout above for every guest write of more than
 * one sector.  So a failed CMD25 is noted for the task that sent it, and
 * that task's next ACMD22, which sdmmc_write_sectors_dma() sends only
 * after one, gives the write's error.  sd_part.c then tries the run once
 * more and counts it, and FatFs gives a disk error.
 *
 * Linked by -Wl,--wrap=sdmmc_host_do_transaction (CMakeLists.txt, the S31
 * boards): SDMMC_HOST_DEFAULT() in storage.c names it, so this is the
 * card's host.do_transaction, which every command goes through.  In
 * internal RAM with the rest of the card's read path (linker_hot.lf); what
 * runs only on a failure is out of line, in flash.
 */
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S31

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_cache.h"
#include "sd_protocol_defs.h"
#include "sd_protocol_types.h"
#include "sdmmc_setup_wrap.h"

#define TAG "sdmmc_timeout_wrap"

esp_err_t __real_sdmmc_host_do_transaction(int slot, sdmmc_command_t *cmdinfo);
esp_err_t __wrap_sdmmc_host_do_transaction(int slot, sdmmc_command_t *cmdinfo);

static esp_err_t __attribute__((noinline)) said_timeout(const sdmmc_command_t *cmd, int64_t us)
{
	ESP_LOGE(TAG, "card command %u (%u bytes at %p) came back as done after %u ms, as "
		 "long as its %u ms wait for the card: reported as a timeout",
		 (unsigned) cmd->opcode, (unsigned) cmd->datalen, cmd->data,
		 (unsigned) (us / 1000), (unsigned) cmd->timeout_ms);
	return ESP_ERR_TIMEOUT;
}

/* the failed multi-block writes whose ACMD22 is still to come, one for
   each task that sends card commands (the emulator's, and whichever task
   writes a file); with more than this, the oldest goes */
#define FAILED_WRITES 4
static struct {
	TaskHandle_t task;
	esp_err_t err;
} s_failed[FAILED_WRITES];
static unsigned s_failed_next;
static portMUX_TYPE s_failed_lock = portMUX_INITIALIZER_UNLOCKED;

static void __attribute__((noinline)) write_failed(esp_err_t err)
{
	TaskHandle_t me = xTaskGetCurrentTaskHandle();
	portENTER_CRITICAL(&s_failed_lock);
	int i = 0;
	while (i < FAILED_WRITES && s_failed[i].task != me)
		i++;
	if (i == FAILED_WRITES) {
		i = 0;
		while (i < FAILED_WRITES && s_failed[i].task)
			i++;
	}
	if (i == FAILED_WRITES)
		i = (int) (s_failed_next++ % FAILED_WRITES);
	s_failed[i].task = me;
	s_failed[i].err = err;
	portEXIT_CRITICAL(&s_failed_lock);
}

/* the task's ACMD22: the error of the write it follows, if this one went
   through */
static esp_err_t __attribute__((noinline)) written_count(esp_err_t ret, bool ok)
{
	TaskHandle_t me = xTaskGetCurrentTaskHandle();
	esp_err_t err = ESP_OK;
	portENTER_CRITICAL(&s_failed_lock);
	for (int i = 0; i < FAILED_WRITES; i++)
		if (s_failed[i].task == me) {
			err = s_failed[i].err;
			s_failed[i].task = NULL;
		}
	portEXIT_CRITICAL(&s_failed_lock);
	if (!ok || err == ESP_OK)
		return ret;
	ESP_LOGE(TAG, "a multi-block write failed (0x%x); its error kept, not the card's "
		 "count of the blocks written", (unsigned) err);
	return err;
}

esp_err_t __wrap_sdmmc_host_do_transaction(int slot, sdmmc_command_t *cmdinfo)
{
	if (!cmdinfo)
		return __real_sdmmc_host_do_transaction(slot, cmdinfo);
	bool timed = cmdinfo->data && esp_cache_get_line_size_by_addr(cmdinfo->data) > 0;
	int64_t t0 = timed ? esp_timer_get_time() : 0;
	esp_err_t ret = __real_sdmmc_host_do_transaction(slot, cmdinfo);
	bool ok = ret == ESP_OK && cmdinfo->error == ESP_OK;
	if (timed && ok) {
		/* the least a wait that saw no event lasts: all its ticks but one */
		uint32_t ticks = cmdinfo->timeout_ms / portTICK_PERIOD_MS;
		int64_t us = esp_timer_get_time() - t0;
		if (ticks >= 2 && us >= (int64_t) (ticks - 1) * portTICK_PERIOD_MS * 1000) {
			ret = said_timeout(cmdinfo, us);
			ok = false;
		}
	}
	/* v5.3: after a failed command the next one sets the slot's settings
	   again (sdmmc_setup_wrap.c) */
	if (!ok)
		sd_setup_forget();
	if (cmdinfo->opcode == MMC_WRITE_BLOCK_MULTIPLE && !ok)
		write_failed(ret != ESP_OK ? ret : cmdinfo->error);
	else if (cmdinfo->opcode == SD_APP_SEND_NUM_WR_BLOCKS)
		ret = written_count(ret, ok);
	return ret;
}

#endif
