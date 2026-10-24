/* Host stand-in for the parts of FreeRTOS that esp/main/display.c and
   esp/main/lcd_korvo1.c use, for tests/host/display/test_double_start.c.
   One thread: critical sections are nothing, the tick count is the test's
   and the drawing loop ends when the test says (xTaskDelayUntil()). */
#ifndef HOST_DISPLAY_FREERTOS_H
#define HOST_DISPLAY_FREERTOS_H

#include <stdbool.h>
#include <stdint.h>

typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
#define pdTRUE  1
#define pdFALSE 0
#define pdPASS  1
#define portMAX_DELAY ((TickType_t) 0xffffffffu)
#define configTICK_RATE_HZ 100
#define portTICK_PERIOD_MS (1000 / configTICK_RATE_HZ)
#define pdMS_TO_TICKS(ms) ((TickType_t) ((uint64_t) (ms) * configTICK_RATE_HZ / 1000))

typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL_SAFE(m) ((void) (m))
#define portEXIT_CRITICAL_SAFE(m) ((void) (m))

#ifndef BIT0
#define BIT0 0x00000001
#define BIT1 0x00000002
#endif

#endif
