/* host stand-in: the card is an image file.  storage.c and ESP-IDF's
   diskio_sdmmc.c use only csd.capacity and csd.sector_size (and the erase
   argument's type, for FF_USE_TRIM). */
#pragma once
#include <stdint.h>
typedef struct {
	struct {
		int capacity;           /* sectors */
		int sector_size;        /* 512 */
	} csd;
	int fd;                         /* the image */
} sdmmc_card_t;
typedef enum {
	SDMMC_ERASE_ARG = 0,
	SDMMC_DISCARD_ARG = 1,
} sdmmc_erase_arg_t;
