/*
 * sdmmc_wait_wrap.c: v5.3, after a write the card is asked again as soon
 * as it lets go of D0, rather than on a fixed schedule (the S31 boards;
 * [esp] sd_wait_fix, on unless the ini says 0).
 *
 * After every write ESP-IDF's sdmmc_write_sectors_dma() asks the card's
 * status (CMD13) and, while the card is still programming, waits in
 * sdmmc_wait_for_idle() (sdmmc_common.c): CMD13 again after 100, 200, 400
 * ... 6,400 us, busy-waiting in esp_rom_delay_us() between them
 * (sdmmc_poll_delay_and_backoff(), CONFIG_SD_READY_POLL_PERIOD_START_US
 * 100), then once a 10 ms tick with vTaskDelay(1).  So a card that is
 * ready after 1.6 ms is seen at 3.1 ms, and one ready after 13 ms at up to
 * 23 ms.  The Windows profile of v52-test3 had 4,126 to 5,678 samples in
 * that ROM busy-wait, 0.84% of core 1's busy time; T386MON's log gave
 * each page-out a wait of 1.3 to 1.9 ms (swap-workflow, board-test3).
 *
 * While it programs, the card holds D0 low, and the S31's SD host shows
 * that in its status register (sdmmc_ll_is_card_data_busy(), which
 * ESP-IDF's own wait_for_busy_cleared() reads for R1b commands).  This
 * wrap keeps ESP-IDF's loop as it is, with its schedule as the latest time
 * for each CMD13 and its 5 s limit, and watches D0 meanwhile: once D0 has
 * been seen busy and goes free, the CMD13 goes at once.  The card is taken
 * as ready only on that CMD13's answer, as before, never on D0 alone.  If
 * D0 was free when the wait began, the first CMD13 goes at once too.  A D0
 * that never moves, stuck either way, leaves ESP-IDF's schedule as it was,
 * with one CMD13 more at most.
 *
 * The counts tell the three apart on the board (profile.c's line): a CMD13
 * sent as D0 went from busy to free (sd_wait_early) only comes from a D0
 * that works, while one sent at once because D0 was free when the wait
 * began (sd_wait_free) is all a D0 stuck free gives, one in every wait.
 * A D0 stuck busy gives neither.
 *
 * Past ESP-IDF's busy-wait (about 12.7 ms), where it sleeps a tick at a
 * time, the emulator's own task (sd_wait_spin_task, set by esp_main.c)
 * goes on watching D0 a tick at a time instead: it never yields core 1
 * anyway (CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1 is off), and the guest
 * is stopped in its disk command meanwhile.  Every other task sleeps a
 * tick, as before.
 *
 * Linked by -Wl,--wrap=sdmmc_wait_for_idle (CMakeLists.txt, the S31
 * boards): sdmmc_cmd.c calls it in sdmmc_common.c, another object file.
 * Only for the SD host; a card in SPI mode, and sd_wait_fix = 0, go to
 * ESP-IDF's own.  tests/host/sdmmc builds ESP-IDF's sdmmc_cmd.c and
 * sdmmc_common.c around a pretend card to hold the two against each other.
 */
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S31

#include <stdbool.h>
#include <stdint.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "hal/sdmmc_ll.h"
#include "esp_private/sdmmc_common.h"
#include "sdmmc_wait_wrap.h"

int sd_wait_fix = 1;
void *sd_wait_spin_task;
uint32_t sd_wait_waits, sd_wait_polls, sd_wait_early, sd_wait_free;

esp_err_t __real_sdmmc_wait_for_idle(sdmmc_card_t *card, uint32_t status);
esp_err_t __wrap_sdmmc_wait_for_idle(sdmmc_card_t *card, uint32_t status);

/* what D0 has shown since the wait began or since the last CMD13: nothing
   yet that calls for a CMD13 at once, free when the wait began, busy */
enum { D0_NOT_YET, D0_FREE_AT_START, D0_SEEN_BUSY };

static inline bool d0_busy(void)
{
	return sdmmc_ll_is_card_data_busy(SDMMC_LL_GET_HW(0));
}

/* until the time due: true as soon as D0 is free with *armed set (D0 was
   free when the wait began, or has been seen busy since) */
static inline bool wait_d0(int64_t due, uint8_t *armed)
{
	for (;;) {
		if (d0_busy())
			*armed = D0_SEEN_BUSY;
		else if (*armed)
			return true;
		if (esp_timer_get_time() >= due)
			return false;
	}
}

/* the same until the tick count moves, when vTaskDelay(1) would end: past
   ESP-IDF's busy-wait, for the task that never yields (and at most two
   ticks by esp_timer, should the tick count not move) */
static inline bool wait_d0_tick(int64_t t1, uint32_t tick_us, uint8_t *armed)
{
	TickType_t t = xTaskGetTickCount();
	for (;;) {
		if (d0_busy())
			*armed = D0_SEEN_BUSY;
		else if (*armed)
			return true;
		if (xTaskGetTickCount() != t || esp_timer_get_time() - t1 >= 2 * (int64_t) tick_us)
			return false;
	}
}

static esp_err_t __attribute__((noinline)) status_failed(esp_err_t err)
{
	ESP_LOGE("sdmmc_wait_wrap", "sdmmc_wait_for_idle: sdmmc_send_cmd_send_status returned 0x%x",
		 err);
	return err;
}

esp_err_t __wrap_sdmmc_wait_for_idle(sdmmc_card_t *card, uint32_t status)
{
	if (!sd_wait_fix || host_is_spi(card))
		return __real_sdmmc_wait_for_idle(card, status);
	if (sdmmc_ready_for_data(status))
		return ESP_OK;
	sd_wait_waits++;
	const uint32_t tick_us = portTICK_PERIOD_MS * 1000;
	bool spin = sd_wait_spin_task && xTaskGetCurrentTaskHandle() == sd_wait_spin_task;
	uint32_t period = SDMMC_READY_POLL_PERIOD_START_US;
	int64_t due = 0;                /* when ESP-IDF's next CMD13 would go */
	uint8_t armed = d0_busy() ? D0_NOT_YET : D0_FREE_AT_START;
	int64_t t0 = esp_timer_get_time();
	do {
		/* ESP-IDF's loop: its limit, then its delay, then CMD13; the
		   delay cut short by D0 going free.  After such a CMD13 the
		   next is still due when ESP-IDF's would be, so that none comes
		   later than it would have. */
		int64_t t1 = esp_timer_get_time();
		if (t1 - t0 > SDMMC_READY_FOR_DATA_TIMEOUT_US)
			return ESP_ERR_TIMEOUT;
		uint32_t delay = MIN(period, tick_us);
		if (!due)
			due = t1 + delay;
		bool early = false;
		if (delay < tick_us)
			early = wait_d0(due, &armed);
		else if (spin)
			early = wait_d0_tick(t1, tick_us, &armed);
		else
			vTaskDelay(1);
		if (early) {
			if (armed == D0_SEEN_BUSY)
				sd_wait_early++;
			else
				sd_wait_free++;
		} else {
			period = MIN(delay * 2, tick_us);
			due = 0;
		}
		armed = D0_NOT_YET;
		sd_wait_polls++;
		esp_err_t err = sdmmc_send_cmd_send_status(card, &status);
		if (err != ESP_OK)
			return status_failed(err);
	} while (!sdmmc_ready_for_data(status));
	return ESP_OK;
}

#endif
