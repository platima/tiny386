/* Host stand-in for ESP-IDF's esp_timer.h (tests/host/display): the
   test's clock. */
#ifndef HOST_DISPLAY_ESP_TIMER_H
#define HOST_DISPLAY_ESP_TIMER_H
#include <stdint.h>
int64_t esp_timer_get_time(void);
#endif
