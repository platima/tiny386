/* host stand-in: the fields of ESP-IDF's sd_host_sdmmc_slot_t that
   sdmmc_setup_wrap.c reads, with their types (esp_driver_sdmmc's
   esp_private/sd_host_private.h at 188e3e55bb) */
#pragma once
#include "esp_err.h"
typedef enum sd_host_sdmmc_slot_state_t {
	SD_HOST_SLOT_STATE_INIT,
	SD_HOST_SLOT_STATE_READY,
} sd_host_sdmmc_slot_state_t;
typedef int sd_sampling_mode_t;         /* SD_SAMPLING_MODE_SDR 1, _DDR 2 */
typedef int sdmmc_delay_phase_t;
typedef int sdmmc_delay_line_t;
typedef int soc_periph_sdmmc_clk_src_t;
typedef struct sd_host_sdmmc_ctlr_t sd_host_sdmmc_ctlr_t;
typedef struct sd_host_sdmmc_slot_t {
	int slot_id;
	struct {
		int width;
		sd_host_sdmmc_slot_state_t width_state;
	} width;
	struct {
		int freq_hz;
		sd_host_sdmmc_slot_state_t freq_state;
		int real_freq_hz;
	} freq;
	struct {
		sd_sampling_mode_t mode;
		sd_host_sdmmc_slot_state_t sampling_mode_state;
	} sampling_mode;
	struct {
		sdmmc_delay_phase_t delayphase;
		sd_host_sdmmc_slot_state_t delay_phase_state;
	} delay_phase;
	struct {
		sdmmc_delay_line_t delayline;
		sd_host_sdmmc_slot_state_t delay_line_state;
	} delay_line;
	soc_periph_sdmmc_clk_src_t clk_src;
	sd_host_sdmmc_ctlr_t *ctlr;
} sd_host_sdmmc_slot_t;
