/* Host stand-in for FreeRTOS.h, for board_check_test.c (shim/README). */
#ifndef BC_SHIM_FREERTOS_H
#define BC_SHIM_FREERTOS_H
#include <stdint.h>
typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void) (m))
#define portEXIT_CRITICAL(m) ((void) (m))
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS pdTRUE
#define portMAX_DELAY ((TickType_t) 0xffffffffu)
#define pdMS_TO_TICKS(ms) ((TickType_t) (ms))
#endif
