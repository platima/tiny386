/*
 * sdmmc_clk_wrap.c: v5.2, the SD card's clock left alone between commands
 * ([esp] sd_clock_fix, on unless the ini says 0).
 *
 * Before each command ESP-IDF's sd_host_slot_sdmmc_do_transaction() asks
 * the hardware for the clock it runs at, in kHz, and sets the clock again
 * unless that equals the request: real_freq_khz != freq_hz / 1000.  On
 * the S31 the clock comes from 500 MHz, so the 40,000 kHz asked for is a
 * divider of 13 and 38,461 kHz, which never equals 40,000: every command
 * stops the clock, sets it and starts it again, with three clock update
 * commands to the controller.  The board's profile put that at about
 * 1.25% of core 1 in use and 2.1% in the boot (PROFILE-BOARD-v52.md,
 * CHECK-profan 4.10).
 *
 * The compare's one caller from outside sd_host_sdmmc.c is that test (the
 * legacy API's getter uses sd_host_slot_get_calc_real_freq(), and calls
 * inside sd_host_sdmmc.c are not wrapped).  So the wrap answers it as it
 * was meant: when the hardware runs at exactly the clock ESP-IDF itself
 * would set for the request, the request is what it reports, and the
 * clock is kept.  Anything else (the 400 kHz of the card's start, a new
 * request) still sets the clock.  sd_clock_fix = 0 passes the hardware's
 * figure through, as before.
 *
 * Linked by -Wl,--wrap=sd_host_slot_get_real_freq (CMakeLists.txt, the
 * S31 boards).
 */
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S31

#include "esp_err.h"
#include "esp_private/sd_host_private.h"
#include "sdmmc_clk_wrap.h"

int sd_clock_fix = 1;
uint32_t sd_clock_kept, sd_clock_set;

esp_err_t __real_sd_host_slot_get_real_freq(sd_host_sdmmc_slot_t *slot, int *real_freq_khz);

esp_err_t __wrap_sd_host_slot_get_real_freq(sd_host_sdmmc_slot_t *slot, int *real_freq_khz)
{
	esp_err_t ret = __real_sd_host_slot_get_real_freq(slot, real_freq_khz);
	if (ret != ESP_OK || !sd_clock_fix)
		return ret;
	int want = 0;
	if (sd_host_slot_get_calc_real_freq(slot, &want) == ESP_OK && want == *real_freq_khz) {
		*real_freq_khz = (int) (slot->freq.freq_hz / 1000);
		sd_clock_kept++;
	} else {
		sd_clock_set++;
	}
	return ret;
}

#endif
