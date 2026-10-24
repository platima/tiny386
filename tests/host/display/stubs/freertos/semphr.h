/* Host stand-in for FreeRTOS's binary semaphores (see FreeRTOS.h beside
   this file): a count of 0 or 1, given by the pretend panel's switch. */
#ifndef HOST_DISPLAY_SEMPHR_H
#define HOST_DISPLAY_SEMPHR_H

#include "freertos/FreeRTOS.h"

typedef struct HostSem *SemaphoreHandle_t;

SemaphoreHandle_t xSemaphoreCreateBinary(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait);
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t s, BaseType_t *woken);

#endif
