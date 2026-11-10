/* host stand-in: the fields of ESP-IDF's sdmmc_command_t that
   sdmmc_timeout_wrap.c reads, with their types */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct {
	uint32_t opcode;
	uint32_t arg;
	uint32_t response[4];
	void *data;
	size_t datalen;
	size_t buflen;
	size_t blklen;
	int flags;
	esp_err_t error;
	uint32_t timeout_ms;
} sdmmc_command_t;
