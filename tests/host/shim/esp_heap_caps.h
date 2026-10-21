/* Host stand-in for ESP-IDF's esp_heap_caps.h (see esp_attr.h here). */
#ifndef HOST_SHIM_ESP_HEAP_CAPS_H
#define HOST_SHIM_ESP_HEAP_CAPS_H
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 0
#define heap_caps_malloc(size, caps) malloc(size)
#endif
