/* host stand-in for ESP-IDF's esp_heap_caps.h */
#pragma once
#include <stdlib.h>
#define MALLOC_CAP_DMA (1 << 3)
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
static inline void *heap_caps_malloc(size_t size, unsigned caps)
{
	(void) caps;
	return malloc(size);
}
/* disk_image.c's buffer and cluster maps */
static inline void *heap_caps_aligned_alloc(size_t align, size_t size, unsigned caps)
{
	(void) caps;
	return aligned_alloc(align, (size + align - 1) / align * align);
}
static inline void heap_caps_free(void *p)
{
	free(p);
}
