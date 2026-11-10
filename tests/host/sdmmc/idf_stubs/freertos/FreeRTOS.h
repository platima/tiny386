/* host stand-in: the tick (100 Hz, as the boards'), and the delay and task
   handle sdmmc_cmd.c, sdmmc_common.c and the wraps use (idf_card.c) */
#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
#define configTICK_RATE_HZ 100
#define portTICK_PERIOD_MS ((TickType_t) 1000 / configTICK_RATE_HZ)
#define portMAX_DELAY ((TickType_t) 0xffffffffUL)
void vTaskDelay(TickType_t ticks);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
TickType_t xTaskGetTickCount(void);
