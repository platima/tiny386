/* host stand-in for ESP-IDF's sdmmc_cmd.h: sector reads and writes of the
   card image (host_glue.c); the card says it can neither trim nor discard */
#pragma once
#include <stddef.h>
#include "esp_err.h"
#include "sd_protocol_types.h"
esp_err_t sdmmc_read_sectors(sdmmc_card_t *card, void *dst, size_t start_sector,
			     size_t sector_count);
esp_err_t sdmmc_write_sectors(sdmmc_card_t *card, const void *src, size_t start_sector,
			      size_t sector_count);
esp_err_t sdmmc_get_status(sdmmc_card_t *card);
esp_err_t sdmmc_can_trim(sdmmc_card_t *card);
esp_err_t sdmmc_can_discard(sdmmc_card_t *card);
esp_err_t sdmmc_erase_sectors(sdmmc_card_t *card, size_t start_sector,
			      size_t sector_count, sdmmc_erase_arg_t arg);
