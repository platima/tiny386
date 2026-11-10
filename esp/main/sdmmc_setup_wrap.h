/*
 * sdmmc_setup_wrap.h: v5.3, the SD slot's settings left alone between
 * commands when nothing has changed (sdmmc_setup_wrap.c).
 */
#ifndef SDMMC_SETUP_WRAP_H
#define SDMMC_SETUP_WRAP_H

#include <stdint.h>

/* commands that kept the four settings, and those that set them, since
   the start */
extern uint32_t sd_setup_kept, sd_setup_set;
/* a command failed: the next one sets the four again (sdmmc_timeout_wrap.c) */
void sd_setup_forget(void);

#endif
