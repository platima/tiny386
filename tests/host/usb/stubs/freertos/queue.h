#ifndef STUB_QUEUE_H
#define STUB_QUEUE_H
#include "freertos/FreeRTOS.h"
QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t item);
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait);
#endif
