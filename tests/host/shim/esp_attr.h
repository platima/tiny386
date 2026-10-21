/* Host stand-in for ESP-IDF's esp_attr.h, for vga.c built with a board
   header that defines BUILD_ESP32 (tests/host/test_vga.c). */
#ifndef HOST_SHIM_ESP_ATTR_H
#define HOST_SHIM_ESP_ATTR_H
#define IRAM_ATTR
#endif
