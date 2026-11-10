/* host stand-in: idf_card.c's pretend clock, in microseconds */
#pragma once
#include <stdint.h>
int64_t esp_timer_get_time(void);
