/* Host stand-in for FreeRTOS event groups (see FreeRTOS.h beside this
   file): every wait is over at once. */
#ifndef HOST_DISPLAY_EVENT_GROUPS_H
#define HOST_DISPLAY_EVENT_GROUPS_H

#include "freertos/FreeRTOS.h"

typedef struct HostEventGroup *EventGroupHandle_t;
typedef uint32_t EventBits_t;

EventBits_t xEventGroupWaitBits(EventGroupHandle_t group, EventBits_t bits,
				BaseType_t clear, BaseType_t all, TickType_t wait);

#endif
