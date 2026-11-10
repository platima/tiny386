/* host stand-in: the heap calls sdmmc_cmd.c makes; every block 64-byte
   aligned, its size kept in front of it for heap_caps_get_allocated_size() */
#pragma once
#include <stddef.h>
#define MALLOC_CAP_DMA (1 << 3)
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
void *heap_caps_malloc(size_t size, unsigned caps);
void heap_caps_free(void *p);
size_t heap_caps_get_allocated_size(void *p);
