/* Host stand-in for ESP-IDF's esp_ota_ops.h (tests/host/display): the
   partition the test says tiny386 runs from. */
#ifndef HOST_DISPLAY_ESP_OTA_OPS_H
#define HOST_DISPLAY_ESP_OTA_OPS_H
#include "esp_partition.h"
const esp_partition_t *esp_ota_get_running_partition(void);
#endif
