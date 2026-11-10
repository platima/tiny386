/* host stand-in for ESP-IDF's esp_heap_caps.h */
#pragma once
#include <stdlib.h>
#define MALLOC_CAP_DMA (1 << 3)
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_CACHE_ALIGNED (1 << 19)
/* v5.3: a DMA buffer, or one asked for aligned, on a 64-byte boundary
   and a whole number of 64 bytes long, as ESP-IDF gives it on the S31
   (its cache line) */
static inline void *heap_caps_malloc(size_t size, unsigned caps)
{
	if (caps & (MALLOC_CAP_DMA | MALLOC_CAP_CACHE_ALIGNED))
		return aligned_alloc(64, (size + 63) / 64 * 64);
	return malloc(size);
}
/* v5.3: non-zero, no internal RAM for heap_caps_aligned_alloc() (host_glue.c;
   tests/host/sdpart's test_sd_dma sets it) */
extern int host_heap_no_internal;
/* disk_image.c's buffer and cluster maps, sd_part.c's buffer */
static inline void *heap_caps_aligned_alloc(size_t align, size_t size, unsigned caps)
{
	if (host_heap_no_internal && (caps & MALLOC_CAP_INTERNAL))
		return NULL;
	return aligned_alloc(align, (size + align - 1) / align * align);
}
static inline void heap_caps_free(void *p)
{
	free(p);
}
