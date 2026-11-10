/*
 * sdmmc_setup_wrap.c: v5.3, the SD slot's settings left alone between
 * commands when nothing has changed (the S31 boards; [esp] sd_clock_fix,
 * as the clock in sdmmc_clk_wrap.c).
 *
 * Before every command ESP-IDF's sd_host_slot_sdmmc_do_transaction()
 * (sd_trans_sdmmc.c) sets four things again, whatever the last command
 * left: the bus width (sd_host_slot_set_bus_width(), which at 4 bits also
 * gives D3 to the SD host again, four GPIO calls), the sampling mode
 * (sd_host_slot_set_bus_sampling_mode()), the delay phase
 * (sd_host_set_delay_phase(), with a clock-tree query and two divisions
 * that only feed its debug lines) and the delay line
 * (sd_host_set_delay_line(), which acts only at SDR104).  The board's
 * Windows profile put the clock tree and GPIO part alone at 0.07% of core
 * 1's busy time (swap-workflow, board-test3).
 *
 * Each of the four writes what it works out from the slot's own fields,
 * and nothing else: so when those fields are what they were for the last
 * command that set all four without an error, the hardware already holds
 * what they would write, and the four are skipped.  They are all set again
 * when any of those fields changes (the card's start at 400 kHz and 1 bit,
 * the switch to 40 MHz and 4 bits, a new slot after the host is set up
 * again), after any clock set (sd_host_slot_set_card_clk(), which a new
 * request or a host set up again brings), after a command that failed
 * (sd_setup_forget(), from sdmmc_timeout_wrap.c), when one of them failed,
 * and with sd_clock_fix = 0.
 *
 * Linked by -Wl,--wrap for the five functions (CMakeLists.txt, the S31
 * boards): sd_trans_sdmmc.c calls them in sd_host_sdmmc.c, another object
 * file.  The four wraps are in internal RAM (linker_hot.lf), the four
 * functions themselves now in flash, as they run only when something
 * changed.
 */
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S31

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_private/sd_host_private.h"
#include "sdmmc_clk_wrap.h"
#include "sdmmc_setup_wrap.h"

uint32_t sd_setup_kept, sd_setup_set;

esp_err_t __real_sd_host_slot_set_card_clk(sd_host_sdmmc_slot_t *slot);
esp_err_t __real_sd_host_slot_set_bus_width(sd_host_sdmmc_slot_t *slot);
esp_err_t __real_sd_host_slot_set_bus_sampling_mode(sd_host_sdmmc_slot_t *slot);
esp_err_t __real_sd_host_set_delay_phase(sd_host_sdmmc_slot_t *slot);
esp_err_t __real_sd_host_set_delay_line(sd_host_sdmmc_slot_t *slot);
esp_err_t __wrap_sd_host_slot_set_card_clk(sd_host_sdmmc_slot_t *slot);
esp_err_t __wrap_sd_host_slot_set_bus_width(sd_host_sdmmc_slot_t *slot);
esp_err_t __wrap_sd_host_slot_set_bus_sampling_mode(sd_host_sdmmc_slot_t *slot);
esp_err_t __wrap_sd_host_set_delay_phase(sd_host_sdmmc_slot_t *slot);
esp_err_t __wrap_sd_host_set_delay_line(sd_host_sdmmc_slot_t *slot);

/* every field of the slot that the four read */
struct setup {
	const void *slot, *ctlr;
	int width, width_state;
	int freq_hz, freq_state;
	int mode, mode_state;
	int phase, phase_state;
	int line, line_state;
	int clk_src;
};

static struct setup s_set;              /* what the four were last set for */
static unsigned s_set_done;             /* which of them worked since (1, 2, 4, 8) */
static unsigned s_era;                  /* moved by a clock set or a failed command */
static unsigned s_set_era;              /* s_era when the four were set for s_set */
static unsigned s_right_era = ~0u;      /* s_set_era once all four have worked */
static bool s_skip;                     /* this command: the four left as they are */

static inline void setup_of(const sd_host_sdmmc_slot_t *slot, struct setup *k)
{
	k->slot = slot;
	k->ctlr = slot->ctlr;
	k->width = slot->width.width;
	k->width_state = slot->width.width_state;
	k->freq_hz = slot->freq.freq_hz;
	k->freq_state = slot->freq.freq_state;
	k->mode = slot->sampling_mode.mode;
	k->mode_state = slot->sampling_mode.sampling_mode_state;
	k->phase = slot->delay_phase.delayphase;
	k->phase_state = slot->delay_phase.delay_phase_state;
	k->line = slot->delay_line.delayline;
	k->line_state = slot->delay_line.delay_line_state;
	k->clk_src = slot->clk_src;
}

/* field by field against the slot, the first difference ending it */
static inline bool setup_same(const sd_host_sdmmc_slot_t *slot, const struct setup *k)
{
	return k->slot == slot && k->ctlr == slot->ctlr && k->width == slot->width.width &&
	       k->width_state == (int) slot->width.width_state &&
	       k->freq_hz == slot->freq.freq_hz &&
	       k->freq_state == (int) slot->freq.freq_state &&
	       k->mode == (int) slot->sampling_mode.mode &&
	       k->mode_state == (int) slot->sampling_mode.sampling_mode_state &&
	       k->phase == (int) slot->delay_phase.delayphase &&
	       k->phase_state == (int) slot->delay_phase.delay_phase_state &&
	       k->line == (int) slot->delay_line.delayline &&
	       k->line_state == (int) slot->delay_line.delay_line_state &&
	       k->clk_src == (int) slot->clk_src;
}

void sd_setup_forget(void)
{
	s_era++;
}

esp_err_t __wrap_sd_host_slot_set_card_clk(sd_host_sdmmc_slot_t *slot)
{
	s_era++;
	return __real_sd_host_slot_set_card_clk(slot);
}

/* the four to be set: the fields they are set for noted, and the first
   of them (out of line, so that the test before it stays short) */
static esp_err_t __attribute__((noinline)) set_width(sd_host_sdmmc_slot_t *slot)
{
	sd_setup_set++;
	s_right_era = ~0u;
	setup_of(slot, &s_set);
	s_set_era = s_era;
	s_set_done = 0;
	esp_err_t ret = __real_sd_host_slot_set_bus_width(slot);
	if (ret == ESP_OK)
		s_set_done |= 1;
	return ret;
}

/* the first of the four for every command: whether to skip them all */
esp_err_t __wrap_sd_host_slot_set_bus_width(sd_host_sdmmc_slot_t *slot)
{
	s_skip = sd_clock_fix && s_right_era == s_era && setup_same(slot, &s_set);
	if (!s_skip)
		return set_width(slot);
	sd_setup_kept++;
	return ESP_OK;
}

esp_err_t __wrap_sd_host_slot_set_bus_sampling_mode(sd_host_sdmmc_slot_t *slot)
{
	if (s_skip)
		return ESP_OK;
	esp_err_t ret = __real_sd_host_slot_set_bus_sampling_mode(slot);
	if (ret == ESP_OK)
		s_set_done |= 2;
	return ret;
}

esp_err_t __wrap_sd_host_set_delay_phase(sd_host_sdmmc_slot_t *slot)
{
	if (s_skip)
		return ESP_OK;
	esp_err_t ret = __real_sd_host_set_delay_phase(slot);
	if (ret == ESP_OK)
		s_set_done |= 4;
	return ret;
}

/* the last of the four: if all of them worked for the fields they were
   set for, and nothing moved the era meanwhile, the next command may skip
   them */
esp_err_t __wrap_sd_host_set_delay_line(sd_host_sdmmc_slot_t *slot)
{
	if (s_skip)
		return ESP_OK;
	esp_err_t ret = __real_sd_host_set_delay_line(slot);
	if (ret == ESP_OK)
		s_set_done |= 8;
	if (s_set_done == 15 && setup_same(slot, &s_set) && s_set_era == s_era)
		s_right_era = s_set_era;
	return ret;
}

#endif
