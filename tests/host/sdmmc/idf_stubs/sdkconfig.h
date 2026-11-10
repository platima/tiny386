/* host stand-in: the settings of the board's build that sdmmc_cmd.c,
   sdmmc_common.c and the wraps read; and bool, which ESP-IDF's
   sd_protocol_types.h uses without including stdbool.h (on the board it
   comes in through other headers first) */
#pragma once
#include <stdbool.h>
#define CONFIG_IDF_TARGET_ESP32S31 1
#define CONFIG_SD_READY_POLL_PERIOD_START_US 100
#define CONFIG_FREERTOS_HZ 100
