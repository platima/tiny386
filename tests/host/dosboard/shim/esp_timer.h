/* Host stand-in for esp_timer.h, for board_check_test.c (shim/README). */
#ifndef BC_SHIM_ESP_TIMER_H
#define BC_SHIM_ESP_TIMER_H
#include <stdint.h>
int64_t esp_timer_get_time(void);
#endif
