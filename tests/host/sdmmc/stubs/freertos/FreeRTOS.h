/* host stand-in: the tick, as ESP-IDF's FreeRTOS gives it (100 Hz on the
   S31 boards; run.sh builds the test at 1000 Hz too), and the spinlock
   counted (the test is one thread) */
#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ 100
#endif
#define portTICK_PERIOD_MS ((TickType_t) 1000 / configTICK_RATE_HZ)
typedef struct {
	int held;
} portMUX_TYPE;
extern int host_crit;                   /* critical sections entered and not left */
#define portMUX_INITIALIZER_UNLOCKED { 0 }
#define portENTER_CRITICAL(m) ((m)->held++, host_crit++)
#define portEXIT_CRITICAL(m) ((m)->held--, host_crit--)
