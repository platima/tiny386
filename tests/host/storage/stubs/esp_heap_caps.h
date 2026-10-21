/* host stand-in for ESP-IDF's esp_heap_caps.h */
#pragma once
#include <stdlib.h>
#define MALLOC_CAP_DMA (1 << 3)
#define MALLOC_CAP_INTERNAL (1 << 11)
static inline void *heap_caps_malloc(size_t size, unsigned caps)
{
	(void) caps;
	return malloc(size);
}
