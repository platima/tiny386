/*
 * idf_card.h: a pretend SD card behind a pretend S31 SD host, for tests
 * that build ESP-IDF's own sdmmc_cmd.c and sdmmc_common.c on the PC
 * (idf_stubs/README): the host's do_transaction, a clock in nanoseconds
 * that the delays, esp_timer and every look at D0 move on, the card's
 * states as the SD spec has them for reads, writes and status, and D0
 * held low while the card programs.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sdmmc_cmd.h"

#define IDF_TICK_US 10000               /* the boards' 100 Hz */

enum { D0_WORKS, D0_STUCK_BUSY, D0_STUCK_FREE };

struct idf_card {
	uint8_t *mem;                   /* the card's sectors */
	unsigned sectors;
	int64_t busy_until;             /* ns: programming until then */
	uint32_t prog_us;               /* how long the next write programs */
	int d0;                         /* D0_* */
	unsigned cmds[64];              /* commands by opcode */
	unsigned cmd13_fail_at;         /* that CMD13 (counted from 1) fails; 0 none */
	unsigned read_fail_at;          /* that read command gives a CRC error; 0 none */
	int64_t last13;                 /* ns of the last CMD13 */
	int64_t least13_gap;            /* ns, the shortest time between two CMD13s */
	unsigned reads_while_busy;      /* reads or writes sent while the card programmed */
	bool app;                       /* the last command was CMD55 */
	uint32_t written;               /* blocks of the last write, for ACMD22 */
	unsigned refused;               /* buffers the host refused for its DMA */
	/* the pretend PSRAM, where the host takes a buffer only on a cache
	   line at both ends (kept by idf_card_start()) */
	const uint8_t *psram_lo, *psram_hi;
};
extern struct idf_card idf_card;
extern int64_t idf_now;                 /* ns */
extern int idf_log_errors;
extern void *idf_task;                  /* what xTaskGetCurrentTaskHandle() gives */

/* a card of n sectors at its start (TRAN, D0 working), and card set up as
   the SD host's with it */
void idf_card_start(sdmmc_card_t *card, unsigned n);
/* whether the card is programming now */
bool idf_card_busy(void);
