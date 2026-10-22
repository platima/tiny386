/* Host stand-in for esp_heap_caps.h, for board_check_test.c (shim/README). */
#ifndef BC_SHIM_ESP_HEAP_CAPS_H
#define BC_SHIM_ESP_HEAP_CAPS_H
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 0
#define heap_caps_calloc(n, size, caps) calloc((n), (size))
#endif
