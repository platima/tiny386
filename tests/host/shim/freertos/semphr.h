/* Host stand-in for FreeRTOS mutexes (see FreeRTOS.h beside this file). */
#ifndef HOST_SHIM_SEMPHR_H
#define HOST_SHIM_SEMPHR_H

#include "freertos/FreeRTOS.h"

typedef struct HostMutex *SemaphoreHandle_t;

SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t m, TickType_t ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t m);

/* for the tests: 1 while some thread holds it */
int host_mutex_held(SemaphoreHandle_t m);
/* takes that failed: deadlocks seen (the same thread taking it again, or
   no give within the time limit) and gives by a thread that did not hold it */
extern int host_mutex_errors;

#endif
