/* Host stand-in for ESP-IDF's esp_attr.h (tests/host/display). */
#ifndef HOST_DISPLAY_ESP_ATTR_H
#define HOST_DISPLAY_ESP_ATTR_H
#define IRAM_ATTR
#define DRAM_ATTR
/* v4.6: the LP SRAM is the test's to carry from one start to the next */
#define RTC_NOINIT_ATTR
#endif
