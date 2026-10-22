/* Host stand-in for FreeRTOS task.h, for board_check_test.c (shim/README). */
#ifndef BC_SHIM_TASK_H
#define BC_SHIM_TASK_H
#include "freertos/FreeRTOS.h"
void vTaskDelay(TickType_t ticks);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack,
				   void *arg, int prio, TaskHandle_t *handle, int core);
#endif
