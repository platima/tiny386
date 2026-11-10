/* host stand-in: 64 for the test's pretend PSRAM, 0 elsewhere (on the S31
   only PSRAM is behind the cache) */
#pragma once
#include <stddef.h>
size_t esp_cache_get_line_size_by_addr(const void *addr);
