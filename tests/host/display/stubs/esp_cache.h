/* Host stand-in for ESP-IDF's esp_cache.h (tests/host/display): no cache. */
#ifndef HOST_DISPLAY_ESP_CACHE_H
#define HOST_DISPLAY_ESP_CACHE_H
#include <stddef.h>
#include "esp_err.h"
#define ESP_CACHE_MSYNC_FLAG_INVALIDATE 1
#define ESP_CACHE_MSYNC_FLAG_UNALIGNED 2
#define ESP_CACHE_MSYNC_FLAG_DIR_C2M 4
esp_err_t esp_cache_msync(void *addr, size_t size, int flags);
#endif
