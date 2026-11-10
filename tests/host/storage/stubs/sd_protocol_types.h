/* host stand-in: the card is an image file.  storage.c and ESP-IDF's
   diskio_sdmmc.c use only csd.capacity and csd.sector_size (and the erase
   argument's type, for FF_USE_TRIM); v5.3's storage_card_dma_ok() the
   host's buffer check. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
	struct {
		int capacity;           /* sectors */
		int sector_size;        /* 512 */
	} csd;
	int fd;                         /* the image */
	struct {
		int slot;
		/* NULL: none (every buffer copied, as before v5.3);
		   host_glue.c's host_dma_check() stands in for ESP-IDF's */
		bool (*check_buffer_alignment)(int slot, const void *buf, size_t size);
	} host;
} sdmmc_card_t;
typedef enum {
	SDMMC_ERASE_ARG = 0,
	SDMMC_DISCARD_ARG = 1,
} sdmmc_erase_arg_t;
