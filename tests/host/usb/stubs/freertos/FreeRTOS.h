/* Host stand-in for the FreeRTOS calls usb_input.c makes (test_usb_input.c,
   from the concurrency review's model). */
#ifndef STUB_FREERTOS_H
#define STUB_FREERTOS_H
#include <stdint.h>
#include <stddef.h>
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
typedef struct stub_queue *QueueHandle_t;
typedef struct stub_sem *SemaphoreHandle_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS pdTRUE
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(x) ((TickType_t) (x))
#endif
