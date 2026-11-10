/*
 * idf_card.c: the pretend SD card and host of idf_card.h, and the stand-ins
 * of idf_stubs/ (heap, delays, clock, task, D0).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "hal/sdmmc_ll.h"
#include "sd_protocol_defs.h"
#include "idf_card.h"

struct idf_card idf_card;
int64_t idf_now = 1000000000;           /* 1 s in */
int idf_log_errors;
void *idf_task;

#define CMD_NS 18000                    /* a command and its answer, with the driver's own time */
#define SECTOR_NS 25600                 /* 512 bytes at 4 bits and 40 MHz */

/* a run that never ends is a failure of the code under test, not a hang */
static void time_check(void)
{
	if (idf_now > 600 * 1000000000LL) {
		printf("  FAIL: the pretend clock passed 10 minutes: a wait that never ends\n");
		exit(3);
	}
}

/* ==== the stand-ins ==== */

void *heap_caps_malloc(size_t size, unsigned caps)
{
	(void) caps;
	size_t n = (size + 63) / 64 * 64;
	uint8_t *p = aligned_alloc(64, n + 64);
	if (!p)
		return NULL;
	memcpy(p, &size, sizeof(size));
	return p + 64;
}

void heap_caps_free(void *p)
{
	if (p)
		free((uint8_t *) p - 64);
}

size_t heap_caps_get_allocated_size(void *p)
{
	size_t size;
	memcpy(&size, (uint8_t *) p - 64, sizeof(size));
	return size;
}

void esp_rom_delay_us(uint32_t us)
{
	idf_now += (int64_t) us * 1000;
	time_check();
}

int64_t esp_timer_get_time(void)
{
	idf_now += 100;                 /* what a read of the timer takes */
	time_check();
	return idf_now / 1000;
}

void vTaskDelay(TickType_t ticks)
{
	/* to the start of the next tick, and whole ticks after it */
	int64_t tick = (int64_t) IDF_TICK_US * 1000;
	idf_now = (idf_now / tick + ticks) * tick;
	time_check();
}

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
	return idf_task;
}

TickType_t xTaskGetTickCount(void)
{
	idf_now += 50;
	time_check();
	return (TickType_t) (idf_now / ((int64_t) IDF_TICK_US * 1000));
}

bool idf_card_busy(void)
{
	return idf_now < idf_card.busy_until;
}

bool sdmmc_ll_is_card_data_busy(sdmmc_dev_t *hw)
{
	(void) hw;
	idf_now += 50;                  /* a register read */
	time_check();
	if (idf_card.d0 == D0_STUCK_BUSY)
		return true;
	if (idf_card.d0 == D0_STUCK_FREE)
		return false;
	return idf_card_busy();
}

/* ==== the card ==== */

static uint32_t r1(void)
{
	bool busy = idf_card_busy();
	uint32_t state = busy ? 7 : 4;  /* prg, tran */
	return (state << 9) | (busy ? 0 : MMC_R1_READY_FOR_DATA) | (idf_card.app ? MMC_R1_APP_CMD : 0);
}

static bool aligned(int slot, const void *buf, size_t size);

static esp_err_t transaction(int slot, sdmmc_command_t *cmd)
{
	unsigned op = cmd->opcode & 63;
	/* as the S31's host refuses a buffer its DMA cannot take as it is
	   (sd_host_slot_sdmmc_do_transaction()) */
	if (cmd->data && !aligned(slot, cmd->data, cmd->buflen)) {
		idf_card.refused++;
		return ESP_ERR_INVALID_ARG;
	}
	idf_card.cmds[op]++;
	idf_now += CMD_NS;
	cmd->error = ESP_OK;
	bool app = idf_card.app;
	idf_card.app = false;
	switch (cmd->opcode) {
	case MMC_APP_CMD:
		idf_card.app = true;
		cmd->response[0] = r1();
		break;
	case MMC_SEND_STATUS:
		if (idf_card.last13 && idf_now - idf_card.last13 < idf_card.least13_gap)
			idf_card.least13_gap = idf_now - idf_card.last13;
		idf_card.last13 = idf_now;
		if (idf_card.cmd13_fail_at && idf_card.cmds[op] == idf_card.cmd13_fail_at)
			return ESP_ERR_TIMEOUT;
		cmd->response[0] = r1();
		break;
	case MMC_READ_BLOCK_SINGLE:
	case MMC_READ_BLOCK_MULTIPLE:
	case MMC_WRITE_BLOCK_SINGLE:
	case MMC_WRITE_BLOCK_MULTIPLE: {
		bool write = cmd->opcode == MMC_WRITE_BLOCK_SINGLE || cmd->opcode == MMC_WRITE_BLOCK_MULTIPLE;
		uint32_t n = cmd->datalen / 512;
		if (idf_card_busy()) {
			/* not in tran: an illegal command, no answer */
			idf_card.reads_while_busy++;
			return ESP_ERR_TIMEOUT;
		}
		if (cmd->arg + n > idf_card.sectors) {
			cmd->error = ESP_ERR_INVALID_RESPONSE;
			break;
		}
		cmd->response[0] = r1();
		idf_now += (int64_t) n * SECTOR_NS;
		if (write) {
			memcpy(idf_card.mem + (size_t) cmd->arg * 512, cmd->data, (size_t) n * 512);
			idf_card.written = n;
			idf_card.busy_until = idf_now + (int64_t) idf_card.prog_us * 1000;
		} else {
			unsigned reads = idf_card.cmds[MMC_READ_BLOCK_SINGLE] +
					 idf_card.cmds[MMC_READ_BLOCK_MULTIPLE];
			if (idf_card.read_fail_at && reads == idf_card.read_fail_at) {
				memset(cmd->data, 0x5A, (size_t) n * 512);
				cmd->error = ESP_ERR_INVALID_CRC;
				break;
			}
			memcpy(cmd->data, idf_card.mem + (size_t) cmd->arg * 512, (size_t) n * 512);
		}
		break;
	}
	case SD_APP_SEND_NUM_WR_BLOCKS:
		if (app && cmd->data && cmd->datalen >= 4) {
			uint32_t be = __builtin_bswap32(idf_card.written);
			memcpy(cmd->data, &be, 4);
		}
		cmd->response[0] = r1();
		break;
	default:
		cmd->response[0] = r1();
		break;
	}
	return ESP_OK;
}

/* the S31's check: a cache line at both ends in the pretend PSRAM, else
   4 bytes (internal RAM, which no cache is in front of) */
static bool aligned(int slot, const void *buf, size_t size)
{
	(void) slot;
	const uint8_t *p = buf;
	unsigned a = p >= idf_card.psram_lo && p < idf_card.psram_hi ? 64 : 4;
	return buf && size && (uintptr_t) buf % a == 0 && size % a == 0;
}

void idf_card_start(sdmmc_card_t *card, unsigned n)
{
	free(idf_card.mem);
	const uint8_t *lo = idf_card.psram_lo, *hi = idf_card.psram_hi;
	memset(&idf_card, 0, sizeof(idf_card));
	idf_card.psram_lo = lo;
	idf_card.psram_hi = hi;
	idf_card.mem = calloc(n, 512);
	idf_card.sectors = n;
	idf_card.least13_gap = INT64_MAX;
	memset(card, 0, sizeof(*card));
	card->host.flags = SDMMC_HOST_FLAG_4BIT | SDMMC_HOST_FLAG_1BIT;
	card->host.slot = 1;
	card->host.do_transaction = transaction;
	card->host.check_buffer_alignment = aligned;
	card->host.max_freq_khz = 40000;
	card->ocr = SD_OCR_SDHC_CAP;
	card->csd.capacity = (int) n;
	card->csd.sector_size = 512;
	card->rca = 1;
}
