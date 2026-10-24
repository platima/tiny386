/* Host stand-in for ESP-IDF's esp_cpu.h (tests/host/display). */
#ifndef HOST_DISPLAY_ESP_CPU_H
#define HOST_DISPLAY_ESP_CPU_H
#include <stdint.h>
uint32_t esp_cpu_get_cycle_count(void);
#endif
