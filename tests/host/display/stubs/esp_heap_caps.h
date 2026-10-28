/* Host stand-in for ESP-IDF's esp_heap_caps.h (tests/host/display): the
   test's pretend PSRAM gives the free sizes. */
#ifndef HOST_DISPLAY_ESP_HEAP_CAPS_H
#define HOST_DISPLAY_ESP_HEAP_CAPS_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#define MALLOC_CAP_DMA (1 << 3)
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
size_t heap_caps_get_free_size(uint32_t caps);
size_t heap_caps_get_largest_free_block(uint32_t caps);
size_t heap_caps_get_minimum_free_size(uint32_t caps);
#endif
