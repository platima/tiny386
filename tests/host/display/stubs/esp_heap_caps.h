/* Host stand-in for ESP-IDF's esp_heap_caps.h (tests/host/display): the
   test's heap_caps_calloc() can be told to fail. */
#ifndef HOST_DISPLAY_ESP_HEAP_CAPS_H
#define HOST_DISPLAY_ESP_HEAP_CAPS_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#define MALLOC_CAP_DMA (1 << 3)
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_SPIRAM (1 << 10)
void *heap_caps_calloc(size_t n, size_t size, uint32_t caps);
#endif
