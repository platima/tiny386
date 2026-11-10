/*
 * sdmmc_timeout_wrap.c: v5.3, a card command that timed out is reported
 * as one (the S31 boards).
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
 * the fixed driver reports it: sd_part.c then tries the run once more and
 * counts it, and FatFs gives a disk error.  The commands that move sectors
 * wait 1 s for a read and 5 s for a write (sdmmc_common.h) and take a few
 * milliseconds when they work, so a good one is taken for a lost one only
 * if the card has stalled for about as long as the driver waits.  With a
 * fixed ESP-IDF such a command fails before this test, which then does
 * nothing.  Commands without data, or with it in internal RAM, are passed
 * through untimed: the driver keeps their errors.
 *
 * Linked by -Wl,--wrap=sdmmc_host_do_transaction (CMakeLists.txt, the S31
 * boards): SDMMC_HOST_DEFAULT() in storage.c names it, so this is the
 * card's host.do_transaction, which every command goes through.  In
 * internal RAM with the rest of the card's read path (linker_hot.lf); the
 * log line is out of line, in flash.
 */
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S31

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_cache.h"
#include "sd_protocol_types.h"

esp_err_t __real_sdmmc_host_do_transaction(int slot, sdmmc_command_t *cmdinfo);
esp_err_t __wrap_sdmmc_host_do_transaction(int slot, sdmmc_command_t *cmdinfo);

static esp_err_t __attribute__((noinline)) said_timeout(const sdmmc_command_t *cmd, int64_t us)
{
	ESP_LOGE("sdmmc_timeout_wrap", "card command %u (%u bytes at %p) came back as done "
		 "after %u ms, as long as its %u ms wait for the card: reported as a timeout",
		 (unsigned) cmd->opcode, (unsigned) cmd->datalen, cmd->data,
		 (unsigned) (us / 1000), (unsigned) cmd->timeout_ms);
	return ESP_ERR_TIMEOUT;
}

esp_err_t __wrap_sdmmc_host_do_transaction(int slot, sdmmc_command_t *cmdinfo)
{
	if (!cmdinfo || !cmdinfo->data || esp_cache_get_line_size_by_addr(cmdinfo->data) == 0)
		return __real_sdmmc_host_do_transaction(slot, cmdinfo);
	int64_t t0 = esp_timer_get_time();
	esp_err_t ret = __real_sdmmc_host_do_transaction(slot, cmdinfo);
	if (ret != ESP_OK || cmdinfo->error != ESP_OK)
		return ret;
	/* the least a wait that saw no event lasts: all its ticks but one */
	uint32_t ticks = cmdinfo->timeout_ms / portTICK_PERIOD_MS;
	int64_t us = esp_timer_get_time() - t0;
	if (ticks >= 2 && us >= (int64_t) (ticks - 1) * portTICK_PERIOD_MS * 1000)
		return said_timeout(cmdinfo, us);
	return ESP_OK;
}

#endif
