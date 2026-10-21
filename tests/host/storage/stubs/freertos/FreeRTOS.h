/* host stand-in: all ESP-IDF's ffconf.h needs of FreeRTOS (the real
   FreeRTOS.h brings stddef.h, which ffconf.h relies on for size_t) */
#pragma once
#include <stddef.h>
#include <stdint.h>
#define portTICK_PERIOD_MS 10
