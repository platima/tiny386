/* host stand-in: the test's pretend clock, in microseconds */
#pragma once
#include <stdint.h>
int64_t esp_timer_get_time(void);
