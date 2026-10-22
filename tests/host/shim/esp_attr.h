/* Host stand-in for ESP-IDF's esp_attr.h, for vga.c and i386.c built with a
   board header that defines BUILD_ESP32 (tests/host/test_vga.c,
   tests/host/cpu). */
#ifndef HOST_SHIM_ESP_ATTR_H
#define HOST_SHIM_ESP_ATTR_H
#define IRAM_ATTR
#define DRAM_ATTR
#endif
