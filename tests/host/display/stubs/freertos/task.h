/* Host stand-in for FreeRTOS tasks (see FreeRTOS.h beside this file): the
   test gives these. */
#ifndef HOST_DISPLAY_TASK_H
#define HOST_DISPLAY_TASK_H

#include "freertos/FreeRTOS.h"

typedef struct HostTask *TaskHandle_t;

TaskHandle_t xTaskGetCurrentTaskHandle(void);
TaskHandle_t xTaskGetHandle(const char *name);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
BaseType_t xTaskNotifyGive(TaskHandle_t task);
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait);
TickType_t xTaskGetTickCount(void);
BaseType_t xTaskDelayUntil(TickType_t *last, TickType_t period);
void vTaskDelay(TickType_t ticks);

#endif
