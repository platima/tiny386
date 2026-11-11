/*
 * sdmmc_wait_wrap.h: v5.3, after a write the card asked again as soon as
 * it lets go of D0 (sdmmc_wait_wrap.c).
 */
#ifndef SDMMC_WAIT_WRAP_H
#define SDMMC_WAIT_WRAP_H

#include <stdint.h>

/* 1 (the default): as above; 0: ESP-IDF's own wait, as before v5.3
   ([esp] sd_wait_fix) */
extern int sd_wait_fix;
/* the task that never yields its core (the emulator's): it watches D0
   past ESP-IDF's busy-wait too, where others sleep a tick at a time */
extern void *sd_wait_spin_task;
/* since the start: waits for a card still programming, the CMD13s sent in
   them, of those the ones sent as D0 went from busy to free, and the ones
   sent at once because D0 was free when the wait began */
extern uint32_t sd_wait_waits, sd_wait_polls, sd_wait_early, sd_wait_free;
/* v5.3: card reads sent with no CMD13 after them (sdmmc_read_wrap.c) */
extern uint32_t sd_read_direct;

#endif
