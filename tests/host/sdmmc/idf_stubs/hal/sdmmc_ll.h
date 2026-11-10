/* host stand-in: the S31 SD host's data-busy bit (the card's D0, inverted),
   idf_card.c's pretend line */
#pragma once
#include <stdbool.h>
typedef struct sdmmc_dev_t sdmmc_dev_t;
#define SDMMC_LL_GET_HW(id) ((sdmmc_dev_t *) 0)
bool sdmmc_ll_is_card_data_busy(sdmmc_dev_t *hw);
