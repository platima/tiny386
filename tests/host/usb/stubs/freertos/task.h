#ifndef STUB_TASK_H
#define STUB_TASK_H
#include "freertos/FreeRTOS.h"
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name,
				   uint32_t stack, void *arg, UBaseType_t prio,
				   TaskHandle_t *out, BaseType_t core);
void vTaskDelete(TaskHandle_t t);
#endif
