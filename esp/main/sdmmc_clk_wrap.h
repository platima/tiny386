/*
 * sdmmc_clk_wrap.h: v5.2, the SD card's clock left alone between commands
 * (sdmmc_clk_wrap.c).
 */
#ifndef SDMMC_CLK_WRAP_H
#define SDMMC_CLK_WRAP_H

#include <stdint.h>

/* 1 (the default): ESP-IDF's test before each command is answered as it
   was meant, and the clock is kept when it is already right; 0: as before */
extern int sd_clock_fix;
/* commands that kept the clock, and those that set it, since the start */
extern uint32_t sd_clock_kept, sd_clock_set;

#endif
