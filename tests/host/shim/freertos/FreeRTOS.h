/* Host stand-in for the parts of FreeRTOS that i8042.c uses, for the tests
   in tests/host.  A FreeRTOS mutex is not recursive: a task that takes it
   twice waits for ever.  Here it is an error-checking pthread mutex, so the
   same mistake is reported at once instead of hanging the test. */
#ifndef HOST_SHIM_FREERTOS_H
#define HOST_SHIM_FREERTOS_H

#include <stdint.h>

typedef int BaseType_t;
typedef uint32_t TickType_t;
#define pdTRUE  1
#define pdFALSE 0
#define portMAX_DELAY ((TickType_t) 0xffffffffu)

#endif
